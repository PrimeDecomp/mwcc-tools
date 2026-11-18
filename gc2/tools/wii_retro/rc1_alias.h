#ifndef WII_RETRO_RC1_ALIAS_H
#define WII_RETRO_RC1_ALIAS_H

#include <stddef.h>
#include <stdint.h>

struct Object;
typedef struct Alias Alias;

enum { AliasObject = 0, AliasObjectPart = 1, AliasSet = 2 };

#pragma pack(push, 2)
typedef struct AliasMember {
    struct AliasMember* next_child;
    struct AliasMember* next_parent;
    Alias* parent;
    Alias* child;
} AliasMember;

struct Alias {
    unsigned char links[4];
    Alias* next_set;
    unsigned char attributes[4];
    AliasMember* parents;
    AliasMember* children;
    const struct Object* object;
    int32_t offset;
    int32_t size;
    unsigned char value_information[8];
    uint32_t* member_bits;
    int index;
    unsigned char kind;
};
#pragma pack(pop)

_Static_assert(offsetof(AliasMember, next_parent) == 4, "RC1 alias parent list");
_Static_assert(offsetof(AliasMember, parent) == 8, "RC1 alias parent link");
_Static_assert(offsetof(AliasMember, child) == 0xc, "RC1 alias child link");
_Static_assert(offsetof(Alias, next_set) == 4, "RC1 alias set list");
_Static_assert(offsetof(Alias, parents) == 0xc, "RC1 alias parents");
_Static_assert(offsetof(Alias, children) == 0x10, "RC1 alias children");
_Static_assert(offsetof(Alias, object) == 0x14, "RC1 alias object");
_Static_assert(offsetof(Alias, size) == 0x1c, "RC1 alias range size");
_Static_assert(offsetof(Alias, member_bits) == 0x28, "RC1 alias membership");
_Static_assert(offsetof(Alias, index) == 0x2c, "RC1 alias index");
_Static_assert(offsetof(Alias, kind) == 0x30, "RC1 alias kind");

extern int RC1_alias_count;
extern Alias* worst_case_ptr;
extern Alias* make_alias(const struct Object* object, int32_t offset, int32_t size);
void add_alias_member(Alias* parent, Alias* child);
void gather_alias_info(void);

#endif
