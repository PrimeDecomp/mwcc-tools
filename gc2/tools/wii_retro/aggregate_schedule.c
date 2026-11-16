/* Preserve memory order within mixed scalar and array member copies during RC1 scheduling. */
#include "rc1_local.h"

enum {
    DirectRegister = 0,
    Immediate = 2,
    FloatRegister = 3,
    GeneralRegister = 4,
    Read = 1,
    Write = 2,
    AliasObjectPart = 1,
    IsBranch = 0x1,
    IsLoad = 0x2,
    IsStore = 0x4,
    IsCall = 0x8,
    IsVolatile = 0x80,
    SideEffects = 0x100,
    ReadsMemory = 0x20000,
    WritesMemory = 0x40000,
};

#pragma pack(push, 2)
typedef struct PCodeArg {
    unsigned char kind;
    unsigned char reg_class;
    union {
        struct {
            uint16_t effect;
            uint16_t number;
        } reg;
        struct {
            uint32_t displacement;
            const Object* symbol;
        } immediate;
        unsigned char storage[12];
    } value;
} PCodeArg;

typedef struct Alias {
    unsigned char header[0x14];
    const Object* object;
    unsigned char payload[0x18];
    unsigned char kind;
} Alias;

typedef struct PCode {
    const struct PCode* next;
    unsigned char links[8];
    uint32_t flags;
    unsigned char state[0xc];
    const Alias* alias;
    const void* source_location;
    unsigned char source_state[4];
    uint16_t opcode;
    uint16_t arg_count;
    PCodeArg args[];
} PCode;

typedef struct PCodeBlock {
    unsigned char header[0x14];
    const PCode* first;
} PCodeBlock;

typedef struct SizedType {
    Type type;
    uint32_t size;
} SizedType;
#pragma pack(pop)

_Static_assert(sizeof(PCodeArg) == 14, "RC1 PCode operand stride");
_Static_assert(offsetof(PCode, alias) == 0x1c, "RC1 PCode alias");
_Static_assert(offsetof(PCode, args) == 0x2c, "RC1 PCode operands");
_Static_assert(offsetof(Alias, kind) == 0x30, "RC1 alias kind");
_Static_assert(offsetof(SizedType, size) == 2, "RC1 type size");

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
extern unsigned char may_alias(const PCode* earlier, const PCode* later);
extern unsigned nbytes_loaded_or_stored_by(const PCode* instruction);
extern void scheduleinstructions(unsigned char include_prolog_epilog);
extern void RC1_ScheduleBlock(PCodeBlock* block);

static const Object* scheduled_function;
static CopyTag* tags;
static size_t tag_count;
static int collect_groups;

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

void retro_schedule_block(PCodeBlock* block) {
    if (collect_groups)
        record_groups(block);
    RC1_ScheduleBlock(block);
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
