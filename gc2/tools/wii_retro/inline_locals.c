/* Preserve the Wii Retro register order when file IPA clones inlined locals. */
#include "rc1_local.h"

enum {
    FromFormal = 1,
    SourceNamed = 2,
    NamedFormal = 4,
    FormalOrder = 8,
    CopyOrder = 16,
    Forwarded = 32,
};

typedef struct Origin {
    unsigned first_clone;
    unsigned last_clone;
    unsigned flags;
} Origin;

typedef struct Member {
    Object* object;
    Origin origin;
    unsigned ordered;
    unsigned deferred;
} Member;

typedef struct Clone {
    unsigned number;
    size_t count;
    Member* members;
    size_t formal_count;
    const Object** named_formals;
} Clone;

typedef struct Walk {
    unsigned first_name;
    unsigned clone_count;
    size_t capacity;
    Origin* origins;
    Clone* active_clone;
} Walk;

typedef struct RegisterLocal {
    Local* entry;
    Object* original;
    const Origin* origin;
} RegisterLocal;

typedef struct InlineArgument InlineArgument;

extern unsigned RC1_unique_name_counter;
extern const Object* RC1_expanding_function;
extern const Object* cscope_currentfunc;
extern unsigned char CClass_IsConstructor(const Object* object);
extern unsigned char CMach_GetFunctionResultClass(const Type* type);
extern void RC1_GraphInlining(void);
extern void RC1_AppendLocal(Object* object);
extern void* RC1_InlineSetup(const Object* callee, void* statements, const Local* formals,
                             Local* locals, const InlineArgument* arguments, void* cost, void* data, void* result);
extern const InlineArgument* retro_inline_arguments;
extern void* memset(void* memory, int value, size_t size);

static Walk* walk;
static Walk completed_walk;

static size_t name_offset(const Walk* state, const Identifier* name) {
    if (!name || name->text[0] != '@')
        return SIZE_MAX;
    const char* digit = name->text + 1;
    if (*digit < '0' || *digit > '9')
        return SIZE_MAX;
    unsigned number = 0;
    do {
        number = number * 10 + (*digit++ - '0');
    } while (*digit >= '0' && *digit <= '9');
    return !*digit && number >= state->first_name ? number - state->first_name : SIZE_MAX;
}

static int reserve_origins(size_t index) {
    size_t capacity = walk->capacity ? walk->capacity : 128;
    while (capacity <= index) {
        if (capacity > SIZE_MAX / (2 * sizeof(Origin)))
            return 0;
        capacity *= 2;
    }
    Origin* origins = realloc(walk->origins, capacity * sizeof(*origins));
    if (!origins)
        return 0;
    memset(origins + walk->capacity, 0, (capacity - walk->capacity) * sizeof(*origins));
    walk->origins = origins;
    walk->capacity = capacity;
    return 1;
}

void retro_CallGraphInlining(void) {
    Walk state = {0};
    state.first_name = RC1_unique_name_counter;
    Walk* saved = walk;
    walk = &state;
    RC1_GraphInlining();
    walk = saved;
    if (saved) {
        free(state.origins);
    } else {
        free(completed_walk.origins);
        completed_walk = state;
    }
}

static Origin get_origin(const Object* object) {
    size_t offset = name_offset(walk, object->name);
    if (offset < walk->capacity && walk->origins[offset].last_clone)
        return walk->origins[offset];
    Origin origin = {0};
    if (named(object->name))
        origin.flags = SourceNamed;
    return origin;
}

static void append_with_origin(Object* object, Origin origin) {
    size_t offset = name_offset(walk, object->name);
    if (offset != SIZE_MAX && (offset < walk->capacity || reserve_origins(offset))) {
        origin.last_clone = walk->active_clone->number;
        if (!origin.first_clone)
            origin.first_clone = origin.last_clone;
        walk->origins[offset] = origin;
    }
    RC1_AppendLocal(object);
}

void retro_AppendInlineLocal(Object* object) {
    if (walk && walk->active_clone) {
        const Clone* clone = walk->active_clone;
        for (size_t index = 0; index < clone->count; ++index) {
            const Member* member = &clone->members[index];
            if (member->object == object) {
                Origin origin = member->origin;
                unsigned inherited = origin.flags & CopyOrder;
                origin.flags &= ~(FormalOrder | CopyOrder | Forwarded);
                origin.flags |= member->deferred;
                if (inherited && !member->deferred)
                    origin.flags |= CopyOrder | Forwarded;
                append_with_origin(object, origin);
                return;
            }
        }
    }
    RC1_AppendLocal(object);
}

void retro_AppendInlineFormal(Object* object) {
    if (!walk || !walk->active_clone) {
        RC1_AppendLocal(object);
        return;
    }
    Origin origin = {0, 0, FromFormal};
    const Clone* clone = walk->active_clone;
    for (size_t i = 0; i < clone->formal_count; ++i) {
        if (clone->named_formals[i] == object) {
            origin.flags |= NamedFormal;
            break;
        }
    }
    append_with_origin(object, origin);
}

static int eligible(const Member* member) {
    const Object* object = member->object;
    if (!object->type || !object->var || object->var->no_register)
        return 0;
    unsigned kind = object->type->kind;
    return kind == TypeInteger || kind == TypeFloat || kind == TypeEnum || kind == TypePointer;
}

static const PointerType* pointer_to_order(const Member* member) {
    if (member->ordered || !member->origin.first_clone ||
        member->origin.first_clone != member->origin.last_clone ||
        !(member->origin.flags & SourceNamed) || !eligible(member) ||
        member->object->type->kind != TypePointer)
        return NULL;
    return (const PointerType*)member->object->type;
}

/* Correct the second prepend of source locals copied from one inline expansion. */
static void order_pointer_groups(Member* members, size_t count, size_t* positions) {
    for (size_t first = 0; first < count; ++first) {
        const PointerType* type = pointer_to_order(&members[first]);
        if (!type || !type->target)
            continue;
        unsigned clone = members[first].origin.last_clone;
        size_t size = 0;
        size_t constants = 0;
        for (size_t i = first; i < count; ++i) {
            const PointerType* next = pointer_to_order(&members[i]);
            if (!next || next->target != type->target || members[i].origin.last_clone != clone)
                continue;
            positions[size++] = i;
            constants += (next->qualifiers & QualConst) != 0;
        }
        /* The search-loop case has a const result and two mutable pointers of the same type. */
        if (!constants || size - constants < 2)
            continue;
        for (size_t i = 0; i < size / 2; ++i) {
            size_t left = positions[i], right = positions[size - i - 1];
            Member saved = members[left];
            members[left] = members[right];
            members[right] = saved;
        }
        for (size_t i = 0; i < size; ++i)
            members[positions[i]].ordered = 1;
    }
}

/* A pair of source iterators from one copy keeps its existing register order. */
static int has_cloned_pointer_peer(const Member* member, const Type* target,
                                   const Member* members, size_t count) {
    if (!(member->origin.flags & SourceNamed))
        return 0;
    for (size_t i = 0; i < count; ++i) {
        const Member* other = &members[i];
        if (other != member && (other->origin.flags & SourceNamed) &&
            other->origin.first_clone == member->origin.first_clone &&
            other->origin.last_clone == member->origin.last_clone &&
            eligible(other) && other->object->type->kind == TypePointer &&
            ((const PointerType*)other->object->type)->target == target)
            return 1;
    }
    return 0;
}

static int partition_group(const Member* member, const Member* members, size_t count,
                           int reorder_cloned_pointers) {
    if (!eligible(member))
        return 0;
    const Type* type = member->object->type;
    const Origin* origin = &member->origin;
    if (type->kind != TypePointer)
        return !origin->last_clone && (origin->flags & SourceNamed) ? 1 : 0;
    if (!origin->last_clone)
        return 0;
    const Type* target = ((const PointerType*)type)->target;
    if (origin->flags & FromFormal)
        return target && target->kind == TypeVoid ? 2 : 0;
    return reorder_cloned_pointers && !has_cloned_pointer_peer(member, target, members, count) ? 2 : 0;
}

/* Place the callee's named scalars ahead of eligible pointer temporaries from nested calls. */
static void partition_locals(Member* members, size_t count, size_t* positions, Member* scratch) {
    size_t named_pointers = 0, named_scalars = 0;
    for (size_t i = 0; i < count; ++i) {
        const Member* member = &members[i];
        if (member->origin.last_clone || !(member->origin.flags & SourceNamed) || !eligible(member))
            continue;
        if (member->object->type->kind == TypePointer)
            ++named_pointers;
        else
            ++named_scalars;
    }
    if (!named_scalars)
        return;
    /* A counted copy can have two iterators. Preserve groups with competing scalars and pointers. */
    int reorder_cloned_pointers = named_pointers <= 1 || named_scalars == 1;

    size_t used = 0;
    size_t scalars = 0;
    size_t end = count;
    for (size_t i = 0; i < count; ++i) {
        int group = partition_group(&members[i], members, count, reorder_cloned_pointers);
        if (!group)
            continue;
        positions[used++] = i;
        if (group == 1)
            scratch[scalars++] = members[i];
        else
            scratch[--end] = members[i];
    }
    for (size_t i = 0; i < scalars; ++i)
        members[positions[i]] = scratch[i];
    for (size_t i = scalars; i < used; ++i)
        members[positions[i]] = scratch[count - (i - scalars) - 1];
}

static int direct_named_copy(const Member* member) {
    return !member->ordered && !member->deferred && eligible(member) &&
           member->origin.first_clone && member->origin.first_clone == member->origin.last_clone &&
           (member->origin.flags & (SourceNamed | NamedFormal));
}

/* The counter's position distinguishes the two-iterator forms of the copy loop. */
static void mark_copy_groups(Member* members, size_t count, size_t* positions) {
    for (size_t first = 0; first < count; ++first) {
        const Member* seed = &members[first];
        if (!direct_named_copy(seed) || (seed->origin.flags & FromFormal) ||
            seed->object->type->kind != TypePointer)
            continue;
        const PointerType* pointer = (const PointerType*)seed->object->type;
        if (!pointer->target || pointer->qualifiers)
            continue;

        size_t total = 0;
        size_t local_pointers = 0, formal_pointers = 0, formal_integers = 0, local_integers = 0;
        size_t counter = count, first_pointer = count;
        int const_pointer = 0;
        for (size_t i = 0; i < count; ++i) {
            const Member* member = &members[i];
            if (!direct_named_copy(member) || member->origin.last_clone != seed->origin.last_clone)
                continue;
            const Type* type = member->object->type;
            int formal = (member->origin.flags & FromFormal) != 0;
            if (type->kind == TypeInteger) {
                formal_integers += formal;
                local_integers += !formal;
                if (!formal)
                    counter = i;
            } else if (type->kind == TypePointer) {
                const PointerType* candidate = (const PointerType*)type;
                if (candidate->qualifiers || candidate->target != pointer->target)
                    continue;
                const_pointer |= (member->object->qualifiers & QualConst) != 0;
                formal_pointers += formal;
                local_pointers += !formal;
                if (!formal && first_pointer == count)
                    first_pointer = i;
            } else {
                continue;
            }
            positions[total++] = i;
        }
        if (const_pointer || local_integers != 1 || formal_integers != 1 || formal_pointers != 2 ||
            !local_pointers || local_pointers > 2 || (local_pointers == 2 && counter >= first_pointer))
            continue;
        for (size_t i = 0; i < total; ++i) {
            Member* member = &members[positions[i]];
            if (local_pointers == 1 || !(member->origin.flags & FromFormal))
                member->deferred = CopyOrder;
        }
    }
}

/* Keep the optimizer's local list intact. Only the live members participate in register assignment. */
static void mark_formal_groups(Member* members, size_t count, size_t* positions) {
    for (size_t first = 0; first < count; ++first) {
        const Member* seed = &members[first];
        if (!direct_named_copy(seed) || !(seed->origin.flags & FromFormal) ||
            seed->object->type->kind != TypePointer)
            continue;
        const PointerType* pointer = (const PointerType*)seed->object->type;
        if (pointer->qualifiers)
            continue;

        size_t total = 0;
        size_t formal_pointers = 0, formal_integers = 0, local_integers = 0;
        for (size_t i = 0; i < count; ++i) {
            const Member* member = &members[i];
            if (!direct_named_copy(member) || member->origin.last_clone != seed->origin.last_clone)
                continue;
            positions[total++] = i;
            const Type* type = member->object->type;
            int formal = (member->origin.flags & FromFormal) != 0;
            if (type->kind == TypeInteger) {
                formal_integers += formal;
                local_integers += !formal;
            } else if (formal && type->kind == TypePointer) {
                const PointerType* candidate = (const PointerType*)type;
                formal_pointers += !candidate->qualifiers && candidate->target == pointer->target;
            }
        }
        if (total != 3 || (formal_pointers != 3 &&
                           (formal_pointers != 1 || formal_integers != 1 || local_integers != 1)))
            continue;
        for (size_t i = 0; i < total; ++i)
            members[positions[i]].deferred = FormalOrder;
    }
}

static const Origin* register_origin(const Object* object, int memory_result) {
    if (!object->var || object->var->no_register || !object->var->used || object->var->reg)
        return NULL;
    size_t offset = name_offset(&completed_walk, object->name);
    if (offset >= completed_walk.capacity)
        return NULL;
    const Origin* origin = &completed_walk.origins[offset];
    /* A copied loop can retain its order through calls that construct a return value. */
    if (!(origin->flags & (FormalOrder | CopyOrder)) || ((origin->flags & Forwarded) && !memory_result))
        return NULL;
    return origin;
}

void retro_AssignInlineLocals(Local* locals, void (*assign)(void)) {
    const Type* function_type = cscope_currentfunc ? cscope_currentfunc->type : NULL;
    int memory_result = function_type && function_type->kind == TypeFunction &&
                        CMach_GetFunctionResultClass(function_type) == 1;
    size_t count = 0;
    for (Local* entry = locals; entry && completed_walk.capacity; entry = entry->next)
        count += register_origin(entry->object, memory_result) != NULL;
    size_t bytes_per_local = sizeof(RegisterLocal) + sizeof(size_t);
    RegisterLocal* registers = count > 1 && count <= SIZE_MAX / bytes_per_local ?
                               malloc(count * bytes_per_local) : NULL;
    if (!registers) {
        assign();
        return;
    }

    size_t next = 0;
    for (Local* entry = locals; entry; entry = entry->next) {
        const Origin* origin = register_origin(entry->object, memory_result);
        if (origin)
            registers[next++] = (RegisterLocal){entry, entry->object, origin};
    }
    size_t* positions = (size_t*)(registers + count);
    for (size_t first = 0; first < count; ++first) {
        const Origin* origin = registers[first].origin;
        if (!origin)
            continue;
        size_t group = 0;
        for (size_t i = first; i < count; ++i) {
            const Origin* other = registers[i].origin;
            if (other && other->first_clone == origin->first_clone && other->last_clone == origin->last_clone)
                positions[group++] = i;
        }
        for (size_t i = 0; i < group; ++i) {
            RegisterLocal* entry = &registers[positions[i]];
            entry->entry->object = registers[positions[group - i - 1]].original;
            entry->origin = NULL;
        }
    }
    assign();
    for (size_t i = 0; i < count; ++i)
        registers[i].entry->object = registers[i].original;
    free(registers);
}

static void* setup_inline_arguments(const Object* callee, void* statements, const Local* formals,
                                    Local* locals, const InlineArgument* arguments, void* cost, void* data,
                                    void* result) {
    const InlineArgument* saved = retro_inline_arguments;
    retro_inline_arguments = arguments;
    void* setup = RC1_InlineSetup(callee, statements, formals, locals, arguments, cost, data, result);
    retro_inline_arguments = saved;
    return setup;
}

void* retro_InlineSetup(const Object* callee, void* statements, const Local* formals,
                        Local* locals, const InlineArgument* arguments, void* cost, void* data, void* result) {
    if (!walk)
        return RC1_InlineSetup(callee, statements, formals, locals, arguments, cost, data, result);

    size_t count = 0;
    for (const Local* item = locals; item; item = item->next)
        ++count;
    const size_t bytes_per_local = 2 * sizeof(Member) + sizeof(size_t);
    Member* members = count && count <= SIZE_MAX / bytes_per_local ?
                      malloc(count * bytes_per_local) : NULL;
    if (count && !members)
        return setup_inline_arguments(callee, statements, formals, locals, arguments, cost, data, result);
    if (count) {
        size_t index = 0;
        for (const Local* item = locals; item; item = item->next)
            members[index++] = (Member){item->object, get_origin(item->object), 0, 0};

        Member* scratch = members + count;
        size_t* positions = (size_t*)(scratch + count);
        partition_locals(members, count, positions, scratch);
        order_pointer_groups(members, count, positions);
        if (CClass_IsConstructor(RC1_expanding_function))
            mark_copy_groups(members, count, positions);
        mark_formal_groups(members, count, positions);
        index = 0;
        for (Local* item = locals; item; item = item->next)
            item->object = members[index++].object;
    }

    size_t formal_count = 0;
    for (const Local* formal = formals; formal; formal = formal->next)
        formal_count += named(formal->object->name);
    const Object** names = formal_count && formal_count <= SIZE_MAX / sizeof(*names) ?
                           malloc(formal_count * sizeof(*names)) : NULL;
    if (names) {
        size_t i = 0;
        for (const Local* formal = formals; formal; formal = formal->next) {
            if (named(formal->object->name))
                names[i++] = formal->object;
        }
    }
    Clone clone = {++walk->clone_count, count, members, names ? formal_count : 0, names};
    Clone* saved = walk->active_clone;
    walk->active_clone = &clone;
    void* setup = setup_inline_arguments(callee, statements, formals, locals, arguments, cost, data, result);
    walk->active_clone = saved;
    free(members);
    free(names);
    return setup;
}
