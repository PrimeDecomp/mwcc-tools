/* Wii RC1 scheduler dependencies and the aggregate-copy alias rule. */
#include "rc1_local.h"
#include "rc1_alias.h"
#include "rc1_pcode.h"

enum {
    SpecialPurposeRegister = 0,
    Read = 1,
    Write = 2,
    RegisterClassCount = GeneralRegister + 1,
    LoadFloatDouble = 0x92,
    StoreFloatDouble = 0x9a,
    WiiOpcodeCount = 0x390,
    TimingRecordCount7400 = 0x191,
    IsBranch = 0x1,
    IsCall = 0x8,
    IsVolatile = 0x80,
    SideEffects = 0x100,
    ReadsMemory = 0x20000,
    WritesMemory = 0x40000,
    Link = 0x800000,
    PrologOrEpilog = 3,
    Scheduled = 8,
};

#pragma pack(push, 2)
typedef struct SchedulerModel {
    int issue_width;
    int renames_registers;
    int branch_latency;
    int (*latency)(PCode*);
    void (*reset)(void);
    int (*can_issue)(PCode*);
    void (*issue)(PCode*);
    void (*advance_clock)(uint16_t cycle);
    int (*serializes)(const PCode*);
    unsigned char reserved[4];
    void (*initialize)(void);
} SchedulerModel;

typedef struct SchedulerTiming {
    int8_t stage;
    int8_t latency;
    int8_t cycles[4];
    int8_t serializes;
} SchedulerTiming;

typedef struct SchedulerTimingRecord {
    uint16_t opcode;
    SchedulerTiming timing;
} SchedulerTimingRecord;

typedef struct DependencyNode {
    struct DependencyNode* next;
    struct DependencyNode* previous;
    struct DependencyEdge* successors;
    PCode* instruction;
    uint16_t latency;
    uint16_t earliest;
    uint16_t latest;
    uint16_t height;
    uint16_t predecessor_count;
} DependencyNode;

typedef struct DependencyEdge {
    struct DependencyEdge* next;
    DependencyNode* successor;
    uint16_t latency;
} DependencyEdge;

typedef struct DependencyList {
    struct DependencyList* next;
    DependencyNode* node;
} DependencyList;

typedef struct SizedType {
    Type type;
    uint32_t size;
} SizedType;
#pragma pack(pop)

_Static_assert(offsetof(SizedType, size) == 2, "RC1 type size");
_Static_assert(sizeof(DependencyNode) == 0x1a, "RC1 scheduler node size");
_Static_assert(offsetof(DependencyNode, instruction) == 0xc, "RC1 scheduled instruction");
_Static_assert(offsetof(DependencyNode, height) == 0x16, "RC1 dependency height");
_Static_assert(sizeof(DependencyEdge) == 0xa, "RC1 scheduler edge size");
_Static_assert(sizeof(DependencyList) == 8, "RC1 scheduler list size");
_Static_assert(offsetof(SchedulerModel, latency) == 0xc, "RC1 scheduler latency callback");
_Static_assert(offsetof(SchedulerModel, advance_clock) == 0x1c, "RC1 scheduler clock callback");
_Static_assert(offsetof(SchedulerModel, serializes) == 0x20, "RC1 scheduler serialization callback");
_Static_assert(offsetof(SchedulerModel, initialize) == 0x28, "RC1 scheduler initialization callback");
_Static_assert(sizeof(SchedulerTiming) == 7, "RC1 7400 timing stride");
_Static_assert(sizeof(SchedulerTimingRecord) == 10, "RC1 7400 timing record stride");

typedef struct Access {
    const PCode* instruction;
    const Object* object;
    uint32_t offset;
    unsigned width;
    uint16_t base;
    uint16_t value;
    unsigned char reg_class;
    int load;
} Access;

typedef struct CopyTag {
    const PCode* instruction;
    const PCode* group;
} CopyTag;

extern const Object* cscope_currentfunc;
extern unsigned char CParser_ObjectCompare(const Object* a, const Object* b);
extern int bitvectorintersectionisempty(const uint32_t* a, const uint32_t* b, int count);
extern void CError_Internal(const char* source_file, int line);
extern const char RC1_Alias_source_file[], RC1_Scheduler_source_file[];
extern signed char RC1_scheduling_cpu;
extern unsigned char RC1_altivec_model;
extern unsigned char RC1_check_disjoint_memory;
extern unsigned char RC1_optimize_size;
extern SchedulerModel* RC1_scheduler_model;
extern uint16_t RC1_critical_path;
extern DependencyNode* RC1_default_successor;
extern DependencyList *RC1_volatile_refs, *RC1_side_effects, *RC1_memory_defs, *RC1_memory_uses;
extern DependencyList **RC1_register_defs[], **RC1_register_uses[];
extern int used_virtual_registers[RegisterClassCount];
extern int coloring;
extern SchedulerModel machine7450, machine7400, machine603, machine603e, machine604;
extern SchedulerModel machine750, machine601, machine821, machineELF, machineZen;
extern unsigned char RC1_7400_timing_initialized;
extern const SchedulerTimingRecord RC1_7400_timing_records[TimingRecordCount7400];
extern SchedulerTiming RC1_7400_instruction_timing[WiiOpcodeCount];
extern int is_same_operand(const PCodeArg* a, const PCodeArg* b);
extern unsigned char RC1_DisjointMemory(const PCode* a, const PCode* b);
extern void* oalloc(unsigned int size);
extern void freeoheap(void);
extern void appendpcode(PCodeBlock* block, PCode* instruction);

static const Object* scheduled_function;
static CopyTag* tags;
static size_t tag_count;
static int collect_groups;

void RC1_Initialize7400Timing(void) {
    if (RC1_7400_timing_initialized)
        return;
    RC1_7400_timing_initialized = 1;

    SchedulerTiming fallback = RC1_7400_timing_records[0].timing;
    for (int opcode = 0; opcode < WiiOpcodeCount; ++opcode)
        RC1_7400_instruction_timing[opcode] = fallback;
    for (int i = 1; i < TimingRecordCount7400; ++i) {
        const SchedulerTimingRecord* record = &RC1_7400_timing_records[i];
        RC1_7400_instruction_timing[record->opcode] = record->timing;
    }

    // Paired-single transfers use the floating-point load/store unit.
    // Keep the ordering barrier for instructions outside the 7400 ISA.
    for (int opcode = FirstPairedSingleMemoryOpcode; opcode <= LastPairedSingleMemoryOpcode; ++opcode) {
        int memory = opcode < FirstPairedSingleStoreOpcode ? LoadFloatDouble : StoreFloatDouble;
        SchedulerTiming timing = RC1_7400_instruction_timing[memory];
        timing.serializes = RC1_7400_instruction_timing[opcode].serializes;
        RC1_7400_instruction_timing[opcode] = timing;
    }
}

static int alias_set_contains(const Alias* set, const Alias* member) {
    return (set->member_bits[member->index >> 5] >> (member->index & 31)) & 1;
}

static const Alias* whole_object_alias(const Alias* alias) {
    if (alias->kind == AliasObject)
        return alias;
    if (alias->kind == AliasObjectPart) {
        for (const AliasMember* member = alias->parents; member; member = member->next_parent) {
            const Alias* parent = member->parent;
            if (parent->kind == AliasObject && parent->object == alias->object)
                return parent;
        }
    }
    CError_Internal(RC1_Alias_source_file, 343);
    return NULL;
}

static int alias_ranges_overlap(const Alias* a, const Alias* b) {
    int32_t a_end = (int32_t)((uint32_t)a->offset + a->size);
    int32_t b_end = (int32_t)((uint32_t)b->offset + b->size);
    return a->offset == b->offset ||
           (a->offset > b->offset && a->offset < b_end) ||
           (b->offset > a->offset && b->offset < a_end);
}

unsigned char RC1_MayAliasPair(const Alias* a, const Alias* b) {
    switch (a->kind * 3 + b->kind) {
        case AliasObject * 3 + AliasObject:
            if (a == b)
                return 1;
            break;
        case AliasObject * 3 + AliasObjectPart:
        case AliasObjectPart * 3 + AliasObject:
            if (CParser_ObjectCompare(a->object, b->object))
                return 1;
            a = whole_object_alias(a);
            b = whole_object_alias(b);
            break;
        case AliasObjectPart * 3 + AliasObjectPart:
            if (CParser_ObjectCompare(a->object, b->object))
                return alias_ranges_overlap(a, b);
            a = whole_object_alias(a);
            b = whole_object_alias(b);
            break;
        case AliasObject * 3 + AliasSet:
        case AliasObjectPart * 3 + AliasSet:
            return alias_set_contains(b, a);
        case AliasSet * 3 + AliasObject:
        case AliasSet * 3 + AliasObjectPart:
            return alias_set_contains(a, b);
        case AliasSet * 3 + AliasSet:
            return a == b || !bitvectorintersectionisempty(a->member_bits, b->member_bits, RC1_alias_count);
        default:
            CError_Internal(RC1_Alias_source_file, 1791);
    }
    if (a->member_bits)
        return alias_set_contains(a, b);
    return b->member_bits && alias_set_contains(b, a);
}

unsigned char may_alias(const PCode* earlier, const PCode* later) {
    return RC1_MayAliasPair(earlier->alias, later->alias);
}

unsigned char may_alias_object(const PCode* instruction, const Object* object) {
    return RC1_MayAliasPair(instruction->alias, make_alias(object, 0, 0));
}

unsigned char may_alias_worst_case(const PCode* instruction) {
    return RC1_MayAliasPair(instruction->alias, worst_case_ptr);
}

unsigned char instr_may_alias_alias(const PCode* instruction, const Alias* alias) {
    return RC1_MayAliasPair(instruction->alias, alias);
}

unsigned char uniquely_aliases(const PCode* a, const PCode* b) {
    if (!may_alias(a, b) || !a->alias || !b->alias || a->alias != b->alias || a->alias->kind == AliasSet)
        return 0;
    int size = a->alias->size;
    return size == nbytes_loaded_or_stored_by(a) && size == nbytes_loaded_or_stored_by(b);
}

unsigned char instr_must_alias_alias(const PCode* instruction, const Alias* alias) {
    if (!RC1_MayAliasPair(instruction->alias, alias) || !alias || instruction->alias != alias || alias->kind == AliasSet)
        return 0;
    int size = alias->size;
    return size == nbytes_loaded_or_stored_by(instruction);
}

static const PCode* next_memory(const PCode* instruction) {
    for (; instruction; instruction = instruction->next) {
        if (instruction->flags & (IsBranch | IsCall | SideEffects))
            return NULL;
        if (instruction->flags & (IsLoad | IsStore | ReadsMemory | WritesMemory))
            return instruction;
    }
    return NULL;
}

static int decode(const PCode* instruction, Access* access) {
    if (!instruction || instruction->arg_count != 3)
        return 0;
    uint32_t flags = instruction->flags & (IsLoad | IsStore | IsVolatile | ReadsMemory | WritesMemory);
    if (flags != IsLoad && flags != IsStore)
        return 0;
    const PCodeArg* data = &instruction->args[0];
    const PCodeArg* base = &instruction->args[1];
    const PCodeArg* offset = &instruction->args[2];
    int load = flags == IsLoad;
    if (data->kind != DirectRegister ||
        (data->reg_class != GeneralRegister && data->reg_class != FloatRegister) ||
        (data->value.reg.effect & (Read | Write)) != (load ? Write : Read) ||
        base->kind != DirectRegister || base->reg_class != GeneralRegister ||
        (base->value.reg.effect & (Read | Write)) != Read ||
        offset->kind != Immediate || offset->value.immediate.symbol)
        return 0;

    const Alias* alias = instruction->alias;
    if (!alias || alias->kind != AliasObjectPart || !alias->object || !alias->object->type)
        return 0;
    unsigned width = nbytes_loaded_or_stored_by(instruction);
    if (!width || width > 8)
        return 0;
    *access = (Access){instruction, alias->object, offset->value.immediate.displacement, width,
                       base->value.reg.number, data->value.reg.number, data->reg_class, load};
    return 1;
}

static uint32_t readonly_aggregate_size(const Access* source) {
    if (source->offset || !(source->object->qualifiers & QualConst) ||
        source->object->type->kind != TypeArray)
        return 0;
    const Type* type = ((const PointerType*)source->object->type)->target;
    if (!type || (type->kind != TypeStruct && type->kind != TypeClass))
        return 0;
    return ((const SizedType*)type)->size;
}

static const PCode* find_copy(const Access* source, uint32_t size, size_t* length) {
    const PCode* cursor = source->instruction;
    const PCode* end = cursor;
    Access destination = {0};
    uint32_t covered = 0;
    unsigned widths = 0;
    size_t accesses = 0;
    int scalar_member = 0, array_member = 0;
    const void* previous_pair = NULL;
    while (covered < size) {
        Access loads[2], next;
        unsigned count = 0;
        while (count < 2 && decode(cursor, &next) && next.load &&
               next.object == source->object && next.base == source->base &&
               next.offset == covered && next.width <= size - covered) {
            loads[count++] = next;
            covered += next.width;
            widths |= 1u << next.width;
            cursor = next_memory(cursor->next);
        }
        if (!count)
            return NULL;

        unsigned used = 0;
        for (unsigned i = 0; i < count; ++i) {
            if (!decode(cursor, &next) || next.load)
                return NULL;
            if (!destination.instruction)
                destination = next;
            if (next.object != destination.object || next.base != destination.base)
                return NULL;
            unsigned j = 0;
            for (; j < count; ++j) {
                if (!(used & (1u << j)) && loads[j].value == next.value &&
                    loads[j].reg_class == next.reg_class && loads[j].width == next.width &&
                    destination.offset + loads[j].offset == next.offset)
                    break;
            }
            if (j == count)
                return NULL;
            used |= 1u << j;
            end = cursor;
            cursor = next_memory(cursor->next);
        }
        if (count == 1) {
            scalar_member = 1;
            previous_pair = NULL;
        } else {
            const void* location = loads[0].instruction->source_location;
            array_member |= location && location == previous_pair;
            previous_pair = location;
        }
        accesses += 2 * count;
    }
    if (!scalar_member || !array_member || !(widths & (widths - 1)))
        return NULL;
    *length = accesses;
    return end;
}

static void record_groups(const PCodeBlock* block) {
    for (const PCode* instruction = block->first; instruction; instruction = instruction->next) {
        Access source;
        if (!decode(instruction, &source) || !source.load)
            continue;
        uint32_t size = readonly_aggregate_size(&source);
        if (!size)
            continue;
        size_t length;
        const PCode* last = find_copy(&source, size, &length);
        if (!last || length > SIZE_MAX / sizeof(*tags) - tag_count)
            continue;
        CopyTag* expanded = realloc(tags, (tag_count + length) * sizeof(*tags));
        if (!expanded)
            return;
        tags = expanded;
        for (const PCode* member = instruction; ; member = next_memory(member->next)) {
            tags[tag_count++] = (CopyTag){member, instruction};
            if (member == last)
                break;
        }
        instruction = last;
    }
}

static const PCode* copy_group(const PCode* instruction) {
    for (size_t i = 0; i < tag_count; ++i) {
        if (tags[i].instruction == instruction)
            return tags[i].group;
    }
    return NULL;
}

static unsigned char aggregate_may_alias(const PCode* earlier, const PCode* later) {
    if (may_alias(earlier, later))
        return 1;
    if (!tag_count || !((earlier->flags ^ later->flags) & IsLoad))
        return 0;
    const PCode* group = copy_group(earlier);
    return group && copy_group(later) == group;
}

static void add_dependency_node(DependencyList** list, DependencyNode* node) {
    DependencyList* entry = oalloc(sizeof(*entry));
    entry->node = node;
    entry->next = *list;
    *list = entry;
}

void RC1_AddSuccessor(DependencyNode* node, DependencyNode* successor, unsigned char has_latency) {
    if (node == successor)
        return;

    int latency = 0;
    if (has_latency) {
        latency = node->latency;
        const PCode* next = successor->instruction;
        if ((next->flags & (IsStore | WritesMemory)) &&
            is_same_operand(next->args, node->instruction->args) && latency > 0)
            --latency;
        if (next->flags & IsBranch)
            latency += RC1_scheduler_model->branch_latency;
    }

    DependencyEdge* edge = node->successors;
    while (edge && edge->successor != successor)
        edge = edge->next;
    if (edge) {
        if (edge->latency >= latency)
            return;
    } else {
        edge = oalloc(sizeof(*edge));
        edge->next = node->successors;
        edge->successor = successor;
        node->successors = edge;
        ++successor->predecessor_count;
    }
    edge->latency = (uint16_t)latency;
    int height = successor->height + edge->latency;
    if (height > node->height)
        node->height = (uint16_t)height;
}

static void serialize_list(DependencyNode* node, const DependencyList* list, unsigned char has_latency) {
    for (; list; list = list->next) {
        if (list->node != node)
            RC1_AddSuccessor(node, list->node, has_latency);
    }
}

void RC1_SerializeRegister(int reg_class, DependencyNode* node, DependencyList** defs,
                          DependencyList** uses, int write) {
    if (write)
        serialize_list(node, *uses, 1);
    serialize_list(node, *defs,
                   reg_class == SpecialPurposeRegister || !RC1_scheduler_model->renames_registers);
    add_dependency_node(write ? defs : uses, node);
}

static void serialize_memory(DependencyNode* node, const DependencyList* list) {
    const PCode* instruction = node->instruction;
    for (; list; list = list->next) {
        const PCode* successor = list->node->instruction;
        if (RC1_check_disjoint_memory && RC1_DisjointMemory(instruction, successor))
            continue;
        if (aggregate_may_alias(instruction, successor))
            RC1_AddSuccessor(node, list->node, 1);
    }
}

void RC1_FindSuccessors(DependencyNode* nodes, DependencyNode* node) {
    const PCode* instruction = node->instruction;
    for (int i = 0; i < instruction->arg_count; ++i) {
        const PCodeArg* arg = &instruction->args[i];
        if (arg->kind != DirectRegister || !(arg->value.reg.effect & (Read | Write)))
            continue;
        int reg_class = (int8_t)arg->reg_class;
        int reg = (int16_t)arg->value.reg.number;
        RC1_SerializeRegister(reg_class, node, &RC1_register_defs[reg_class][reg],
                              &RC1_register_uses[reg_class][reg], arg->value.reg.effect & Write);
    }

    if (instruction->flags & (IsLoad | ReadsMemory)) {
        serialize_memory(node, RC1_memory_defs);
        add_dependency_node(&RC1_memory_uses, node);
    } else if (instruction->flags & (IsStore | WritesMemory)) {
        serialize_memory(node, RC1_memory_uses);
        serialize_memory(node, RC1_memory_defs);
        add_dependency_node(&RC1_memory_defs, node);
        if (instruction->flags & WritesMemory)
            add_dependency_node(&RC1_memory_uses, node);
    }

    if (instruction->flags & IsVolatile) {
        serialize_list(node, RC1_volatile_refs, 0);
        add_dependency_node(&RC1_volatile_refs, node);
    }

    if (((instruction->flags & (IsBranch | IsCall)) && (instruction->extended_flags & Link)) ||
        (instruction->flags & SideEffects) || RC1_scheduler_model->serializes(instruction)) {
        for (DependencyNode* successor = nodes; successor; successor = successor->next)
            RC1_AddSuccessor(node, successor, 0);
        add_dependency_node(&RC1_side_effects, node);
    }
    serialize_list(node, RC1_side_effects, 0);

    if (!node->successors && RC1_default_successor)
        RC1_AddSuccessor(node, RC1_default_successor, 0);
    if (node->height > RC1_critical_path)
        RC1_critical_path = node->height;
}

static void init_dependencies(void) {
    for (int reg_class = 0; reg_class < RegisterClassCount; ++reg_class) {
        int count = used_virtual_registers[reg_class];
        RC1_register_uses[reg_class] = oalloc(count * sizeof(DependencyList*));
        RC1_register_defs[reg_class] = oalloc(count * sizeof(DependencyList*));
        for (int reg = 0; reg < count; ++reg) {
            RC1_register_defs[reg_class][reg] = NULL;
            RC1_register_uses[reg_class][reg] = NULL;
        }
    }
    RC1_memory_defs = RC1_memory_uses = NULL;
    RC1_side_effects = RC1_volatile_refs = NULL;
    RC1_critical_path = 0;
    RC1_default_successor = NULL;
}

static int ready_to_issue(const DependencyNode* node, uint16_t cycle) {
    return !node->predecessor_count && node->earliest <= cycle &&
           RC1_scheduler_model->can_issue(node->instruction);
}

static int released_successors(const DependencyNode* node) {
    int count = 0;
    for (const DependencyEdge* edge = node->successors; edge; edge = edge->next) {
        if (edge->successor->predecessor_count == 1)
            ++count;
    }
    return count;
}

static DependencyNode* select_instruction(DependencyNode* head, uint16_t cycle) {
    DependencyNode* selected = head;
    while (selected && !ready_to_issue(selected, cycle))
        selected = selected->next;
    if (!selected)
        return NULL;

    if (RC1_optimize_size) {
        for (DependencyNode* node = selected->next; node; node = node->next)
            ready_to_issue(node, cycle);
    }

    for (DependencyNode* node = selected->next; node; node = node->next) {
        if (!ready_to_issue(node, cycle))
            continue;
        int selected_due = selected->latest <= cycle;
        int node_due = node->latest <= cycle;
        if (selected_due != node_due) {
            if (node_due)
                selected = node;
            continue;
        }
        int node_releases = released_successors(node);
        int selected_releases = released_successors(selected);
        if (node_releases != selected_releases) {
            if (node_releases > selected_releases)
                selected = node;
            continue;
        }
        if (node->height > selected->height ||
            (node->height == selected->height && coloring &&
             opcodeinfo[node->instruction->opcode].rank < opcodeinfo[selected->instruction->opcode].rank))
            selected = node;
    }
    return selected;
}

void RC1_ScheduleBlock(PCodeBlock* block) {
    init_dependencies();
    DependencyNode* head = NULL;
    for (PCode* instruction = block->last; instruction; instruction = instruction->previous) {
        DependencyNode* node = oalloc(sizeof(*node));
        *node = (DependencyNode){0};
        node->instruction = instruction;
        node->latency = node->height = (uint16_t)RC1_scheduler_model->latency(instruction);
        RC1_FindSuccessors(head, node);
        if (instruction->flags & IsBranch)
            RC1_default_successor = node;
        if (head)
            head->previous = node;
        node->next = head;
        head = node;
    }
    for (DependencyNode* node = head; node; node = node->next)
        node->latest = RC1_critical_path - node->height;

    block->first = block->last = NULL;
    block->instruction_count = 0;
    RC1_scheduler_model->reset();
    for (uint16_t cycle = 0; head; ++cycle) {
        for (int issued = 0; head && issued < RC1_scheduler_model->issue_width; ++issued) {
            DependencyNode* node = select_instruction(head, cycle);
            if (!node)
                break;
            for (DependencyEdge* edge = node->successors; edge; edge = edge->next) {
                DependencyNode* successor = edge->successor;
                --successor->predecessor_count;
                if (successor->earliest < cycle + edge->latency)
                    successor->earliest = (uint16_t)(cycle + edge->latency);
            }
            appendpcode(block, node->instruction);
            RC1_scheduler_model->issue(node->instruction);
            if (node->previous)
                node->previous->next = node->next;
            else
                head = node->next;
            if (node->next)
                node->next->previous = node->previous;
        }
        RC1_scheduler_model->advance_clock(cycle);
    }
    freeoheap();
}

static void schedule_block(PCodeBlock* block) {
    if (collect_groups)
        record_groups(block);
    RC1_ScheduleBlock(block);
}

void scheduleinstructions(unsigned char include_prolog_epilog) {
    int cpu = RC1_scheduling_cpu;
    if (cpu == 10)
        RC1_scheduler_model = &machine7450;
    else if (RC1_altivec_model || cpu == 7)
        RC1_scheduler_model = &machine7400;
    else if (cpu == 2)
        RC1_scheduler_model = &machine603;
    else if (cpu == 5)
        RC1_scheduler_model = &machine603e;
    else if (cpu == 3 || cpu == 6)
        RC1_scheduler_model = &machine604;
    else if (cpu == 4)
        RC1_scheduler_model = &machine750;
    else if (cpu == 1)
        RC1_scheduler_model = &machine601;
    else if (cpu == 9)
        RC1_scheduler_model = &machine821;
    else if (cpu == 11)
        RC1_scheduler_model = &machineELF;
    else if (cpu == 13)
        RC1_scheduler_model = &machineZen;
    else
        RC1_scheduler_model = &machine603;

    if (!RC1_scheduler_model->initialize)
        CError_Internal(RC1_Scheduler_source_file, 976);
    RC1_scheduler_model->initialize();
    gather_alias_info();

    for (PCodeBlock* block = pcbasicblocks; block; block = block->next) {
        if (block->instruction_count > 2 && (include_prolog_epilog || !(block->flags & PrologOrEpilog))) {
            schedule_block(block);
            block->flags |= Scheduled;
        }
    }
}

void retro_scheduleinstructions(unsigned char include_prolog_epilog) {
    if (!include_prolog_epilog || scheduled_function != cscope_currentfunc)
        tag_count = 0;
    scheduled_function = cscope_currentfunc;
    collect_groups = !include_prolog_epilog;
    scheduleinstructions(include_prolog_epilog);
    collect_groups = 0;
    if (include_prolog_epilog) {
        free(tags);
        tags = NULL;
        tag_count = 0;
        scheduled_function = NULL;
    }
}
