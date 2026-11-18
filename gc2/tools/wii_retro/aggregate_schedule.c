/* Preserve memory order within mixed scalar and array member copies during RC1 scheduling. */
#include "rc1_local.h"
#include "rc1_alias.h"
#include "rc1_pcode.h"

enum {
    Read = 1,
    Write = 2,
    IsBranch = 0x1,
    IsCall = 0x8,
    IsVolatile = 0x80,
    SideEffects = 0x100,
    ReadsMemory = 0x20000,
    WritesMemory = 0x40000,
    PrologOrEpilog = 3,
    Scheduled = 8,
};

#pragma pack(push, 2)
typedef struct SchedulerModel {
    unsigned char header[0x28];
    void (*initialize)(void);
} SchedulerModel;

typedef struct SizedType {
    Type type;
    uint32_t size;
} SizedType;
#pragma pack(pop)

_Static_assert(offsetof(SizedType, size) == 2, "RC1 type size");
_Static_assert(offsetof(SchedulerModel, initialize) == 0x28, "RC1 scheduler initialization callback");

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
extern SchedulerModel* RC1_scheduler_model;
extern SchedulerModel machine7450, machine7400, machine603, machine603e, machine604;
extern SchedulerModel machine750, machine601, machine821, machineELF, machineZen;
extern void RC1_ScheduleBlock(PCodeBlock* block);

static const Object* scheduled_function;
static CopyTag* tags;
static size_t tag_count;
static int collect_groups;

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

unsigned char retro_aggregate_may_alias(const PCode* earlier, const PCode* later) {
    if (may_alias(earlier, later))
        return 1;
    if (!tag_count || !((earlier->flags ^ later->flags) & IsLoad))
        return 0;
    const PCode* group = copy_group(earlier);
    return group && copy_group(later) == group;
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
