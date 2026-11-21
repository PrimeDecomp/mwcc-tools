/* Discard aggregate construction temporaries after their field loads are forwarded. */
#include "rc1_local.h"
#include "rc1_pcode.h"

extern void globallyoptimizepcode(const Object*);
extern void deletepcode(PCode*);
extern unsigned char is_volatile_object(const Object*);
extern unsigned char IsTempName(const Identifier*);
extern unsigned char RC1_ipa_kind;

enum {
    FileIpa = 1,
    LocalAddress = 1,
    IsVolatile = 0x80,
    HasSideEffects = 0x100,
    FloatSize = 4,
};

typedef struct AggregateTemp {
    const Object* object;
    const PCodeBlock* block;
    int size;
    int writes;
    int reads;
    int eligible;
    const Object* member_source;
    int source_offset;
    int members_copied;
    const Object* destination;
    int destination_offset;
} AggregateTemp;

#pragma pack(push, 2)
typedef struct SizedType {
    Type type;
    unsigned char reserved;
    int size;
} SizedType;
#pragma pack(pop)

_Static_assert(offsetof(SizedType, size) == 2, "RC1 object type size");

static const Object* operand_object(const PCodeArg* arg) {
    if (arg->kind == MemoryOperand)
        return arg->value.memory.object;
    if (arg->kind == Immediate)
        return arg->value.immediate.symbol;
    return NULL;
}

static int float_access(const PCode* code, const PCodeArg* arg, unsigned flag) {
    return arg->kind == MemoryOperand &&
           (code->flags & (IsLoad | IsStore | IsVolatile | HasSideEffects)) == flag &&
           code->arg_count >= 2 && code->args[0].kind == DirectRegister &&
           code->args[0].reg_class == FloatRegister && nbytes_loaded_or_stored_by(code) == FloatSize;
}

static int local_class(const Object* object) {
    return object && object->address_kind == LocalAddress && object->type && object->type->kind == TypeClass;
}

static int object_size(const Object* object) {
    return ((const SizedType*)object->type)->size;
}

/* A member copy reads the complete source object in order before starting the next member. */
static int copied_member(AggregateTemp* temporary, const PCode* store) {
    const PCode* load = store->previous;
    if (!load || load->arg_count < 2 || load->args[0].value.reg.number != store->args[0].value.reg.number)
        return 0;

    for (int i = 0; i < load->arg_count; ++i) {
        const PCodeArg* arg = &load->args[i];
        if (!float_access(load, arg, IsLoad))
            continue;
        const Object* source = arg->value.memory.object;
        if (!local_class(source) || source == temporary->object)
            return 0;
        if (!temporary->member_source || temporary->source_offset == object_size(temporary->member_source)) {
            temporary->member_source = source;
            temporary->source_offset = 0;
            ++temporary->members_copied;
        }
        if (source != temporary->member_source || arg->value.memory.offset != temporary->source_offset ||
            temporary->source_offset >= object_size(source))
            return 0;
        temporary->source_offset += FloatSize;
        return 1;
    }
    return 0;
}

static int copied_to_local_member(AggregateTemp* temporary, const PCode* load) {
    const PCode* store = load->next;
    if (!store || store->arg_count < 2 || load->args[0].value.reg.number != store->args[0].value.reg.number)
        return 0;

    for (int i = 0; i < store->arg_count; ++i) {
        const PCodeArg* arg = &store->args[i];
        if (!float_access(store, arg, IsStore))
            continue;
        const Object* destination = arg->value.memory.object;
        if (!local_class(destination) || object_size(destination) <= temporary->size)
            return 0;
        if (!temporary->destination) {
            temporary->destination = destination;
            temporary->destination_offset = arg->value.memory.offset;
        }
        return destination == temporary->destination && temporary->destination_offset >= 0 &&
               temporary->destination_offset <= object_size(destination) - temporary->size &&
               arg->value.memory.offset == temporary->destination_offset + temporary->reads;
    }
    return 0;
}

static AggregateTemp* find_temporary(AggregateTemp* entries, size_t count, const Object* object) {
    for (size_t i = 0; i < count; ++i) {
        if (entries[i].object == object)
            return &entries[i];
    }
    return NULL;
}

static void record_access(AggregateTemp* entry, const PCodeBlock* block, const PCode* code, const PCodeArg* arg) {
    if (!entry->eligible)
        return;
    if (block == entry->block && float_access(code, arg, IsStore) && !entry->reads &&
        arg->value.memory.offset == entry->writes && entry->writes < entry->size && copied_member(entry, code)) {
        entry->writes += FloatSize;
    } else if (block == entry->block && float_access(code, arg, IsLoad) && entry->writes == entry->size &&
               arg->value.memory.offset == entry->reads && entry->reads < entry->size &&
               copied_to_local_member(entry, code)) {
        entry->reads += FloatSize;
    } else {
        entry->eligible = 0;
    }
}

static AggregateTemp* collect_temporaries(size_t* count) {
    AggregateTemp* entries = NULL;
    size_t capacity = 0;
    for (const PCodeBlock* block = pcbasicblocks; block; block = block->next) {
        for (const PCode* code = block->first; code; code = code->next) {
            for (int i = 0; i < code->arg_count; ++i) {
                const PCodeArg* arg = &code->args[i];
                const Object* object = operand_object(arg);
                if (!local_class(object) || !object->name || !IsTempName(object->name))
                    continue;
                AggregateTemp* entry = find_temporary(entries, *count, object);
                if (!entry) {
                    if (*count == capacity) {
                        size_t next = capacity ? capacity * 2 : 16;
                        AggregateTemp* grown = next <= SIZE_MAX / sizeof(*entries) ?
                                              realloc(entries, next * sizeof(*entries)) : NULL;
                        if (!grown) {
                            free(entries);
                            *count = 0;
                            return NULL;
                        }
                        entries = grown;
                        capacity = next;
                    }
                    int size = object_size(object);
                    entry = &entries[(*count)++];
                    *entry = (AggregateTemp){.object = object, .block = block, .size = size,
                                            .eligible = size > FloatSize && !is_volatile_object(object)};
                }
                record_access(entry, block, code, arg);
            }
        }
    }

    /* Require a complete composite copied straight into a larger stack object. */
    size_t eligible = 0;
    for (size_t i = 0; i < *count; ++i) {
        const AggregateTemp* entry = &entries[i];
        if (entry->eligible && entry->reads == entry->size && entry->members_copied >= 2 &&
            entry->source_offset == object_size(entry->member_source))
            entries[eligible++] = *entry;
    }
    *count = eligible;
    return entries;
}

void retro_OptimizePCode(const Object* function) {
    size_t count = 0;
    AggregateTemp* entries = RC1_ipa_kind == FileIpa ? collect_temporaries(&count) : NULL;
    globallyoptimizepcode(function);
    if (!count) {
        free(entries);
        return;
    }

    /* Keep the temporary if any read or address use survived value numbering. */
    for (const PCodeBlock* block = pcbasicblocks; block; block = block->next) {
        for (const PCode* code = block->first; code; code = code->next) {
            for (int i = 0; i < code->arg_count; ++i) {
                const PCodeArg* arg = &code->args[i];
                AggregateTemp* entry = find_temporary(entries, count, operand_object(arg));
                if (entry && entry->eligible && !float_access(code, arg, IsStore))
                    entry->eligible = 0;
            }
        }
    }

    for (PCodeBlock* block = pcbasicblocks; block; block = block->next) {
        for (PCode* code = block->first; code;) {
            PCode* next = code->next;
            for (int i = 0; i < code->arg_count; ++i) {
                const AggregateTemp* entry = find_temporary(entries, count, operand_object(&code->args[i]));
                if (entry && entry->eligible) {
                    deletepcode(code);
                    break;
                }
            }
            code = next;
        }
    }
    free(entries);
}
