/* Wii 1.0RC1 virtual parameter aliases and alias membership. */
#include "rc1_local.h"
#include "rc1_alias.h"

enum {
    RegularObject = 5,
    DataObject = 0,
    QualRestrict = 0x200000,
    VirtualArraySize = 0xfedcba0,
    VirtualAliasOffset = 0x7ffff00,
};

typedef struct AliasArray {
    PointerType type;
    unsigned char attributes[0x16 - sizeof(PointerType)];
} AliasArray;

typedef struct AliasObjectStorage {
    Object object;
    unsigned char attributes[0x60 - sizeof(Object)];
} AliasObjectStorage;

_Static_assert(sizeof(AliasArray) == 0x16, "RC1 synthetic array size");
_Static_assert(sizeof(AliasObjectStorage) == 0x60, "RC1 synthetic alias object size");
_Static_assert(offsetof(Object, address_kind) == 2, "RC1 object address kind");
_Static_assert(offsetof(Object, name) == 0xc, "RC1 object identifier");
_Static_assert(offsetof(Object, type) == 0x10, "RC1 object type");
_Static_assert(offsetof(Object, qualifiers) == 0x14, "RC1 object qualifiers");

extern void* lalloc(unsigned size);
extern void memclrw(void* memory, unsigned size);
extern unsigned CParser_GetTypeQualifiers(const Type* type, unsigned qualifiers);
extern unsigned char CParser_IsReadOnly(const Type* type, unsigned qualifiers);
extern unsigned char CClass_IsPODClass(const Type* type);
extern const Identifier* CParser_GetUniqueName(void);
extern Alias* create_alias_set(const char* name, const Type* type);

extern Alias* set_aliases;
extern int RC1_gathered_aliases;
extern uint32_t* RC1_alias_scratch;
extern uint32_t* RC1_alias_scratch_aux;
extern void propagatealiasinfo(void);
extern void bitvectorinitialize(uint32_t* bits, int count, int value);
extern int bitvectorunion_changed(uint32_t* bits, const uint32_t* source, int count);
extern void bitvectorunion(uint32_t* bits, const uint32_t* source, int count);

Alias* make_distinct_virtual_alias(const Object* object) {
    const Type* type = object->type;
    if (type->kind != TypePointer && type->kind != TypeArray)
        return NULL;

    const PointerType* pointer = (const PointerType*)type;
    int in_type_set = 0;
    int read_only = 0;
    if (!(CParser_GetTypeQualifiers(object->type, object->qualifiers) & QualRestrict)) {
        if (CParser_IsReadOnly(pointer->target, object->qualifiers)) {
            if (type->kind == TypeClass && !CClass_IsPODClass(type))
                in_type_set = 1;
            read_only = 1;
        } else {
            in_type_set = 1;
        }
    }

    AliasArray* array = lalloc(sizeof(*array));
    memclrw(array, sizeof(*array));
    array->type.type.kind = TypeArray;
    array->type.size = VirtualArraySize;
    array->type.target = pointer->target;

    AliasObjectStorage* storage = lalloc(sizeof(*storage));
    memclrw(storage, sizeof(*storage));
    Object* synthetic = &storage->object;
    synthetic->object_kind = RegularObject;
    synthetic->type = &array->type.type;
    synthetic->address_kind = DataObject;
    synthetic->name = CParser_GetUniqueName();
    if (read_only)
        synthetic->qualifiers = QualConst;

    Alias* alias = make_alias(synthetic, VirtualAliasOffset, 1);
    if (in_type_set)
        add_alias_member(create_alias_set(NULL, pointer->target), alias);
    return alias;
}

static inline int mark_alias_index(uint32_t* bits, int index) {
    uint32_t* word = &bits[index >> 5];
    uint32_t mask = 1u << (index & 31);
    if (*word & mask)
        return 0;
    *word |= mask;
    return 1;
}

/* RC1 visits three levels of parents before recursing. */
void RC1_AddAliasAncestors(Alias* destination, const Alias* alias) {
    for (const AliasMember* parent = alias->parents; parent; parent = parent->next_parent) {
        if (!mark_alias_index(destination->member_bits, parent->parent->index))
            continue;
        for (const AliasMember* grandparent = parent->parent->parents; grandparent;
             grandparent = grandparent->next_parent) {
            if (!mark_alias_index(destination->member_bits, grandparent->parent->index))
                continue;
            for (const AliasMember* ancestor = grandparent->parent->parents; ancestor;
                 ancestor = ancestor->next_parent) {
                if (mark_alias_index(destination->member_bits, ancestor->parent->index))
                    RC1_AddAliasAncestors(destination, ancestor->parent);
            }
        }
    }
}

static unsigned bitvector_size(int count) {
    return sizeof(uint32_t) * ((count + 31) >> 5);
}

void RC1_RefreshAliasMembership(void) {
    for (Alias* alias = set_aliases; alias; alias = alias->next_set) {
        if (!alias->member_bits || bitvector_size(RC1_gathered_aliases) != bitvector_size(RC1_alias_count))
            alias->member_bits = lalloc(bitvector_size(RC1_alias_count));
        bitvectorinitialize(alias->member_bits, RC1_alias_count, 0);
        mark_alias_index(alias->member_bits, alias->index);
        for (const AliasMember* member = alias->children; member; member = member->next_child) {
            const Alias* child = member->child;
            mark_alias_index(alias->member_bits, child->index);
            if (child->kind != AliasSet) {
                for (const AliasMember* nested = child->children; nested; nested = nested->next_child)
                    mark_alias_index(alias->member_bits, nested->child->index);
            }
        }
        RC1_AddAliasAncestors(alias, alias);
    }

    int changed;
    do {
        changed = 0;
        for (Alias* alias = set_aliases; alias; alias = alias->next_set) {
            for (const AliasMember* member = alias->children; member; member = member->next_child) {
                if (member->child->member_bits &&
                    bitvectorunion_changed(alias->member_bits, member->child->member_bits, RC1_alias_count))
                    changed = 1;
            }
        }
    } while (changed);

    for (Alias* alias = set_aliases; alias; alias = alias->next_set) {
        if (alias->kind != AliasObject)
            continue;
        for (const AliasMember* member = alias->parents; member; member = member->next_parent) {
            if (member->parent->member_bits)
                bitvectorunion(alias->member_bits, member->parent->member_bits, RC1_alias_count);
        }
    }

    if (!RC1_alias_scratch || bitvector_size(RC1_gathered_aliases) != bitvector_size(RC1_alias_count)) {
        RC1_alias_scratch = lalloc(bitvector_size(RC1_alias_count));
        RC1_alias_scratch_aux = lalloc(bitvector_size(RC1_alias_count));
    }
    RC1_gathered_aliases = RC1_alias_count;
}

void gather_alias_info(void) {
    propagatealiasinfo();
    RC1_RefreshAliasMembership();
}
