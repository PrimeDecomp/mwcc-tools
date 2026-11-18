#ifndef WII_RETRO_RC1_LOCAL_H
#define WII_RETRO_RC1_LOCAL_H

#include <stddef.h>
#include <stdint.h>

enum {
    TypeVoid = 0,
    TypeInteger = 1,
    TypeFloat = 2,
    TypeEnum = 3,
    TypeStruct = 5,
    TypeClass = 6,
    TypeFunction = 7,
    TypePointer = 12,
    TypeArray = 13,
    QualConst = 1,
    QualReference = 0x20,
};

typedef struct Identifier {
    unsigned char header[10];
    char text[];
} Identifier;

typedef struct Type {
    unsigned char kind;
} Type;

#pragma pack(push, 2)
typedef struct PointerType {
    Type type;
    uint32_t size;
    const Type* target;
    unsigned qualifiers;
} PointerType;
#pragma pack(pop)

typedef struct VarInfo {
    unsigned char header[8];
    unsigned char no_register;
    unsigned char used;
    unsigned char flags[6];
    short reg;
} VarInfo;

typedef struct Object {
    unsigned char header[0xc];
    const Identifier* name;
    const Type* type;
    unsigned qualifiers;
    unsigned char attributes[0x28];
    const VarInfo* var;
} Object;

typedef struct Local {
    struct Local* next;
    Object* object;
} Local;

_Static_assert(offsetof(PointerType, target) == 6, "RC1 pointer layout");
_Static_assert(offsetof(Object, var) == 0x40, "RC1 object layout");
_Static_assert(offsetof(VarInfo, reg) == 0x10, "RC1 local register");

static inline int named(const Identifier* name) {
    return name && name->text[0] != '@' && name->text[0] != '$';
}

extern void* malloc(size_t size);
extern void* realloc(void* memory, size_t size);
extern void free(void* memory);

#endif
