/* CMach_GetObjectAlignment in Wii 1.0RC1. Use the declared alignment
 * for non-local operands and keep RC1's alignment for local storage.
 */
#include <stddef.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#pragma pack(push, 2)
typedef struct {
    u8 kind;
} Type;

typedef struct {
    u8 tag[2];
    u8 address_kind;
    u8 reserved[13];
    Type* type;
    u32 qualifiers;
} Object;

typedef struct {
    u8 reserved[14];
    u8 flags;
} VarInfo;

typedef struct {
    Type type;
    u8 reserved[13];
    u16 alignment;
} TypeStruct;

typedef struct {
    Type type;
    u8 reserved[41];
    u16 alignment;
} TypeClass;
#pragma pack(pop)

_Static_assert(offsetof(Object, type) == 0x10, "RC1 Object type offset");
_Static_assert(offsetof(Object, qualifiers) == 0x14, "RC1 Object qualifiers offset");
_Static_assert(offsetof(TypeStruct, alignment) == 0x0e, "RC1 TypeStruct alignment offset");
_Static_assert(offsetof(TypeClass, alignment) == 0x2a, "RC1 TypeClass alignment offset");

extern u8 RC1_structalignment;
extern u8 RC1_oldalignment;
extern int RC1_GetQUALalign(Type*, u32);
extern short RC1_GetQualifiedTypeAlign(Type*, int);
extern int RC1_AllocationAlignment(Type*, u32);
extern VarInfo* RC1_Registers_GetVarInfo(const Object*);

int CMach_GetObjectAlignment(const Object* object) {
    Type* type = object->type;
    if (object->address_kind != 1) {
        short alignment = (short)RC1_GetQUALalign(type, object->qualifiers);
        return alignment ? alignment : RC1_GetQualifiedTypeAlign(type, 1);
    }

    VarInfo* info = RC1_Registers_GetVarInfo(object);
    if (!info || !(info->flags & 1))
        return RC1_AllocationAlignment(type, object->qualifiers);

    if (type->kind == 5)
        return ((TypeStruct*)type)->alignment;
    if (type->kind == 6)
        return ((TypeClass*)type)->alignment;

    u8 mode = RC1_structalignment;
    u8 oldalignment = RC1_oldalignment;
    RC1_structalignment = 2;
    RC1_oldalignment = 0;
    short alignment = RC1_GetQualifiedTypeAlign(type, 0);
    RC1_structalignment = mode;
    RC1_oldalignment = oldalignment;
    return alignment;
}
