/* Preserve local register order across RC1's file-IPA optimizations. */
#include "rc1_iro.h"

enum {
    FileIpa = 1,
    HasMemory = 0x80,
};

typedef struct Reference {
    const Object* object;
    Object* source;
    unsigned uses;
    int chained;
} Reference;

typedef struct Replacement {
    Local* reference;
    Local* temporary;
    const Identifier* name;
} Replacement;

typedef enum TempOrigin {
    OrdinaryTemp,
    CommonSubexpression,
    ScalarizedMember,
    LoopInvariant,
    LinkedIterator,
} TempOrigin;

typedef struct Temp {
    unsigned number;
    TempOrigin origin;
} Temp;

typedef struct TempSlot {
    Local* entry;
    Object* original;
    int iterator;
} TempSlot;

extern unsigned char RC1_ipa_kind;
extern unsigned RC1_unique_name_counter;
extern const Linear* RC1_first_linear;
extern Local* RC1_locals;
extern Local* arguments;
extern Local* toclist;
extern int optimizing;
extern Object* create_temp_object(const Type* type);
extern VarInfo* Registers_GetVarInfo(Object* object);
extern void assign_register_by_type(Object* object);
extern unsigned char IsTempName(const Identifier* name);
extern void RC1_Linearize(void* statements);
extern void RC1_CopyAndConstantPropagation(void);
extern void* RC1_Delinearize(void* statements, void* options);
extern void retro_AssignInlineLocals(Local* locals, void (*assign)(void));

static Reference* references;
static size_t count;
static size_t capacity;

static Temp* temps;
static size_t temp_count;
static size_t temp_capacity;

static Object* create_temp(const Type* type, TempOrigin origin) {
    Object* object = create_temp_object(type);
    if (RC1_ipa_kind != FileIpa)
        return object;

    if (temp_count == temp_capacity) {
        if (temp_capacity > SIZE_MAX / (2 * sizeof(*temps)))
            return object;
        size_t new_capacity = temp_capacity ? temp_capacity * 2 : 32;
        Temp* expanded = realloc(temps, new_capacity * sizeof(*temps));
        if (!expanded)
            return object;
        temps = expanded;
        temp_capacity = new_capacity;
    }
    temps[temp_count++] = (Temp){RC1_unique_name_counter - 1, origin};
    return object;
}

Object* retro_CreateTemp(const Type* type) {
    return create_temp(type, OrdinaryTemp);
}

Object* retro_CreateCommonSubexpression(const Type* type) {
    return create_temp(type, CommonSubexpression);
}

Object* retro_CreateScalarizedMember(const Type* type) {
    return create_temp(type, ScalarizedMember);
}

Object* retro_CreateLoopInvariant(const Type* type) {
    return create_temp(type, LoopInvariant);
}

static Temp* find_temp(const Object* object) {
    const Identifier* name = object->name;
    if (!name || name->text[0] != '@')
        return NULL;
    const char* digit = name->text + 1;
    unsigned number = 0;
    if (*digit < '0' || *digit > '9')
        return NULL;
    while (*digit >= '0' && *digit <= '9')
        number = number * 10 + (*digit++ - '0');
    if (*digit)
        return NULL;

    size_t first = 0;
    size_t end = temp_count;
    while (first < end) {
        size_t middle = first + (end - first) / 2;
        if (temps[middle].number < number)
            first = middle + 1;
        else
            end = middle;
    }
    return first < temp_count && temps[first].number == number ? &temps[first] : NULL;
}

static Object* loaded_through(const Linear* load) {
    if (!load || load->kind != Unary || load->opcode != Indirect)
        return NULL;
    const Linear* address = load->input.left;
    if (!address || address->kind != Binary || address->opcode != Add)
        return indirect_operand(address);
    const Linear* offset = address->right;
    return offset && offset->kind == Operand && offset->opcode == IntConstant ?
           indirect_operand(address->input.left) : NULL;
}

void* retro_Delinearize(void* statements, void* options) {
    if (temp_count && RC1_ipa_kind == FileIpa) {
        for (const Linear* node = RC1_first_linear; node; node = node->next) {
            if (node->kind != Binary || node->opcode != Assign)
                continue;
            Object* object = indirect_operand(node->input.left);
            if (!object || object != loaded_through(node->right))
                continue;
            Temp* temp = find_temp(object);
            if (temp && temp->origin == ScalarizedMember)
                temp->origin = LinkedIterator;
        }
    }
    return RC1_Delinearize(statements, options);
}

static const Temp* register_temp(const Object* object) {
    if (!object->var || object->var->no_register || !object->var->used || object->var->reg || !object->type)
        return NULL;
    switch (object->type->kind) {
    case TypeInteger:
    case TypeFloat:
    case TypeEnum:
    case TypePointer:
        return find_temp(object);
    default:
        return NULL;
    }
}

static TempSlot* order_iterators(size_t* length) {
    size_t eligible = 0;
    size_t limit = 0;
    int have_common_expression = 0;
    int have_iterator = 0;
    int move_iterator = 0;
    for (Local* entry = RC1_locals; entry; entry = entry->next) {
        const Temp* temp = register_temp(entry->object);
        if (!temp)
            continue;
        ++eligible;
        have_common_expression |= temp->origin == CommonSubexpression;
        have_iterator |= temp->origin == LinkedIterator;
        if (temp->origin == CommonSubexpression ||
            (temp->origin == LoopInvariant && entry->object->type->kind == TypeInteger)) {
            limit = eligible;
            move_iterator |= have_iterator;
        }
    }
    if (!have_common_expression || !move_iterator || limit > SIZE_MAX / sizeof(TempSlot))
        return NULL;
    TempSlot* slots = malloc(limit * sizeof(*slots));
    if (!slots)
        return NULL;
    size_t next = 0;
    for (Local* entry = RC1_locals; next < limit; entry = entry->next) {
        const Temp* temp = register_temp(entry->object);
        if (temp)
            slots[next++] = (TempSlot){entry, entry->object, temp->origin == LinkedIterator};
    }
    /* Keep all other temporaries in order within the prefix ending at the last anchor. */
    next = 0;
    for (size_t i = 0; i < limit; ++i) {
        if (!slots[i].iterator)
            slots[next++].entry->object = slots[i].original;
    }
    for (size_t i = 0; i < limit; ++i) {
        if (slots[i].iterator)
            slots[next++].entry->object = slots[i].original;
    }
    *length = limit;
    return slots;
}

static const PointerType* pointer_type(const Object* object) {
    return object && object->type && object->type->kind == TypePointer ?
           (const PointerType*)object->type : NULL;
}

static int named_reference(const Object* object) {
    const PointerType* pointer = pointer_type(object);
    return pointer && (pointer->qualifiers & QualReference) && named(object->name);
}

static Reference* find_reference(const Object* object, int add) {
    for (size_t i = 0; i < count; ++i)
        if (references[i].object == object)
            return &references[i];
    if (!add)
        return NULL;

    if (count == capacity) {
        if (capacity > SIZE_MAX / (2 * sizeof(*references)))
            return NULL;
        size_t new_capacity = capacity ? capacity * 2 : 16;
        Reference* expanded = realloc(references, new_capacity * sizeof(*references));
        if (!expanded)
            return NULL;
        references = expanded;
        capacity = new_capacity;
    }
    Reference* reference = &references[count++];
    *reference = (Reference){object, NULL, 0, 0};
    return reference;
}

static void scan_references(int initial) {
    for (const Linear* node = RC1_first_linear; node; node = node->next) {
        if (initial && node->kind == Operand) {
            Object* object = operand(node);
            if (named_reference(object)) {
                Reference* reference = find_reference(object, 1);
                if (reference)
                    ++reference->uses;
            }
        }
        if (node->kind != Binary || node->opcode != Assign)
            continue;
        Object* left = indirect_operand(node->input.left);
        Object* right = indirect_operand(node->right);
        if (!right || !named_reference(left))
            continue;
        Reference* reference = find_reference(left, initial);
        if (reference) {
            reference->source = right;
            if (initial)
                reference->chained = named_reference(right);
        }
    }
}

void retro_Linearize(void* statements) {
    count = 0;
    RC1_Linearize(statements);
    if (RC1_ipa_kind == FileIpa)
        scan_references(1);
}

void retro_CopyAndConstantPropagation(void) {
    RC1_CopyAndConstantPropagation();
    if (count && RC1_ipa_kind == FileIpa)
        scan_references(0);
}

static Local* replacement_for(const Object* object) {
    const Reference* reference = find_reference(object, 0);
    if (!reference || reference->uses != 2 || !reference->source || !object->var || object->var->used)
        return NULL;
    if ((object->qualifiers & QualConst) && !reference->chained)
        return NULL;

    Object* source = reference->source;
    for (size_t depth = 0; source && named(source->name) && depth < count; ++depth) {
        const Reference* alias = find_reference(source, 0);
        source = alias ? alias->source : NULL;
    }
    const PointerType* type = pointer_type(source);
    if (!type || named(source->name) || type->target != pointer_type(object)->target ||
        !source->var || source->var->no_register || !source->var->used || source->var->reg)
        return NULL;
    for (Local* entry = RC1_locals; entry; entry = entry->next)
        if (entry->object == source)
            return entry;
    return NULL;
}

static void assign_with_references(void (*assign)(void)) {
    size_t used = 0;
    Replacement* changes = count && RC1_ipa_kind == FileIpa &&
                           count <= SIZE_MAX / sizeof(*changes) ?
                           malloc(count * sizeof(*changes)) : NULL;
    if (changes) {
        for (Local* entry = RC1_locals; entry && used < count; entry = entry->next) {
            Local* source = replacement_for(entry->object);
            if (!source)
                continue;
            Object* temporary = source->object;
            changes[used++] = (Replacement){entry, source, temporary->name};
            temporary->name = entry->object->name;
            source->object = entry->object;
            entry->object = temporary;
        }
    }
    size_t temp_slots = 0;
    TempSlot* slots = temp_count && RC1_ipa_kind == FileIpa ? order_iterators(&temp_slots) : NULL;
    if (RC1_ipa_kind == FileIpa)
        retro_AssignInlineLocals(RC1_locals, assign);
    else
        assign();
    for (size_t i = 0; i < temp_slots; ++i)
        slots[i].entry->object = slots[i].original;
    free(slots);
    temp_count = 0;
    while (used) {
        const Replacement* swap = &changes[--used];
        Object* temporary = swap->reference->object;
        swap->reference->object = swap->temporary->object;
        swap->temporary->object = temporary;
        temporary->name = swap->name;
    }
    free(changes);
    free(references);
    references = NULL;
    capacity = 0;
    count = 0;
}

static inline int pointer_or_array(const Type* type) {
    int result = 1;
    if (type->kind != TypePointer && type->kind != TypeArray)
        result = 0;
    return result;
}

static inline int is_volatile(const Object* object) {
    unsigned mask;
    const Type* type;
    type = object->type;
    mask = pointer_or_array(type);
    if (mask) {
        mask = ((const PointerType*)type)->qualifiers & QualVolatile;
        return mask;
    }
    mask = object->qualifiers & QualVolatile;
    return mask;
}

static inline void assign_local(Object* object) {
    const VarInfo* info = Registers_GetVarInfo(object);
    if ((info->flags & HasMemory) && !info->reg && info->used && !info->no_register && !is_volatile(object))
        assign_register_by_type(object);
}

static inline void reassign_local(Object* object) {
    const VarInfo* info = Registers_GetVarInfo(object);
    if (info->used && !info->no_register && !is_volatile(object) && !info->reg)
        assign_register_by_type(object);
}

/* Both native passes visit arguments, named locals, temporaries, then TOC entries. */
void RC1_AssignLocals(void) {
    int saved = optimizing;
    Local* local;
    Object* object;
    const VarInfo* info;
    optimizing = 1;

    for (local = arguments; local; local = local->next)
        assign_local(local->object);
    for (local = RC1_locals; local; local = local->next) {
        object = local->object;
        if (!IsTempName(object->name))
            assign_local(object);
    }
    for (local = RC1_locals; local; local = local->next) {
        object = local->object;
        if (IsTempName(object->name))
            assign_local(object);
    }
    for (local = toclist; local; local = local->next) {
        object = local->object;
        info = Registers_GetVarInfo(object);
        /* The native TOC scan tests the loaded register twice. */
        if (!info->reg && (info->flags & HasMemory) && !info->reg)
            assign_register_by_type(object);
    }
    optimizing = saved;
}

void RC1_ReassignLocals(void) {
    Local* local;
    Object* object;
    const VarInfo* info;

    for (local = arguments; local; local = local->next)
        reassign_local(local->object);
    for (local = RC1_locals; local; local = local->next) {
        object = local->object;
        if (!IsTempName(object->name))
            reassign_local(object);
    }
    for (local = RC1_locals; local; local = local->next) {
        object = local->object;
        if (IsTempName(object->name))
            reassign_local(object);
    }
    for (local = toclist; local; local = local->next) {
        object = local->object;
        info = Registers_GetVarInfo(object);
        if (!info->reg && info->used && info->usage > 1)
            assign_register_by_type(object);
    }
}

void retro_AssignLocals(void) {
    assign_with_references(RC1_AssignLocals);
}

void retro_ReassignLocals(void) {
    assign_with_references(RC1_ReassignLocals);
}
