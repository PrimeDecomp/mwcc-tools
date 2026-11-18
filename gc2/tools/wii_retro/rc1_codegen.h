#ifndef WII_RETRO_RC1_CODEGEN_H
#define WII_RETRO_RC1_CODEGEN_H

#include <stddef.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#pragma pack(push, 2)
typedef struct {
    u8 kind;
    int size;
} Type;

typedef struct {
    Type base;
    u8 integral;
} TypeIntegral;

typedef struct {
    Type base;
    Type* target;
} TypeMemberPointer;

typedef struct {
    Type base;
    u8 reserved[8];
    u16 alignment;
    u8 subtype;
} TypeStruct;

typedef struct {
    Type base;
    u8 reserved[36];
    u16 alignment;
} TypeClass;

typedef struct {
    u8 reserved[14];
    u8 flags;
    u8 rclass;
    short reg;
    short reg_hi;
} VarInfo;

typedef struct {
    u8 tag[2];
    u8 address_kind;
    u8 reserved[13];
    Type* type;
    u32 qualifiers;
    u8 attributes[0x28];
    VarInfo* local_info;
    u8 data_attributes[0x14];
    VarInfo* data_info;
} Object;

typedef struct {
    u32 first_word;
    u32 second_word;
} FloatBits;

typedef struct ENode ENode;
struct ENode {
    u8 kind;
    u8 flags;
    u8 ignored;
    u8 hascall;
    Type* type;
    u8 reserved[8];
    union {
        ENode* monadic;
        Object* object;
        FloatBits floating;
        struct { u32 high; u32 low; } integer;
        struct { ENode* left; ENode* right; } diadic;
        struct { ENode* condition; ENode* left; ENode* right; } conditional;
    } data;
};

typedef struct {
    u8 kind;
    u8 attributes;
    short reg;
    short reg_hi;
    short reg_offset;
    int displacement;
    int immediate;
    Object* object;
    u32 flags;
    u32 extra;
} Operand;
#pragma pack(pop)

_Static_assert(offsetof(Type, size) == 0x02, "RC1 Type size offset");
_Static_assert(offsetof(TypeIntegral, integral) == 0x06, "RC1 integral subtype offset");
_Static_assert(offsetof(TypeMemberPointer, target) == 0x06, "RC1 member pointer target offset");
_Static_assert(offsetof(TypeStruct, alignment) == 0x0e, "RC1 TypeStruct alignment offset");
_Static_assert(offsetof(TypeStruct, subtype) == 0x10, "RC1 TypeStruct subtype offset");
_Static_assert(offsetof(TypeClass, alignment) == 0x2a, "RC1 TypeClass alignment offset");
_Static_assert(offsetof(Object, address_kind) == 0x02, "RC1 Object address kind offset");
_Static_assert(offsetof(Object, type) == 0x10, "RC1 Object type offset");
_Static_assert(offsetof(Object, qualifiers) == 0x14, "RC1 Object qualifiers offset");
_Static_assert(offsetof(Object, local_info) == 0x40, "RC1 local VarInfo offset");
_Static_assert(offsetof(Object, data_info) == 0x58, "RC1 data VarInfo offset");
_Static_assert(sizeof(VarInfo) == 0x14, "RC1 VarInfo size");
_Static_assert(offsetof(VarInfo, flags) == 0x0e, "RC1 VarInfo flags offset");
_Static_assert(offsetof(VarInfo, reg) == 0x10, "RC1 VarInfo register offset");
_Static_assert(offsetof(VarInfo, reg_hi) == 0x12, "RC1 VarInfo high register offset");
_Static_assert(offsetof(ENode, type) == 0x04, "RC1 ENode type offset");
_Static_assert(offsetof(ENode, data) == 0x10, "RC1 ENode data offset");
_Static_assert(offsetof(ENode, data.integer.low) == 0x14, "RC1 integer literal low word offset");
_Static_assert(sizeof(Operand) == 0x1c, "RC1 Operand size");
_Static_assert(offsetof(Operand, object) == 0x10, "RC1 Operand object offset");

enum {
    VoidType = 0,
    IntegerType = 1,
    FloatType = 2,
    EnumType = 4,
    StructureType = 5,
    ClassType = 6,
    FunctionType = 7,
    MemberPointerType = 11,
    PointerType = 12,
    FirstNonScalarIntegral = 23,
    PairedSingle = 25,
};

enum {
    EINDIRECT = 4,
    EADD = 15,
    ESUB = 16,
    ELESS = 19,
    EGREATER = 20,
    ELESSEQU = 21,
    EGREATEREQU = 22,
    EEQU = 23,
    ENOTEQU = 24,
    EBITFIELD = 51,
    EINTCONST = 52,
    EFLOATCONST = 53,
    EOBJREF = 59,
    ECONDASS = 88,
};

enum { OpGPR = 0, OpGPRPair = 3, OpAbsolute = 4, OpFPR = 5, OpVR = 6, OpIndirect = 9 };
enum { DataObject = 0, LocalObject = 1, AbsoluteObject = 2, FunctionObject = 3 };
enum { RegVR = 2, RegFPR = 3, RegGPR = 4, RegClassCount = 5, NoRegister = 0xfe };
enum { InParameterArea = 1, GekkoProcessor = 0x16 };

typedef struct PCode PCode;

extern void memclrw(void*, u32);
extern void (*cgdispatch[])(ENode*, short, short, Operand*);
extern void indirect(Operand*, ENode*);
extern void move_block(Operand*, Operand*, int, int);
extern int CMach_GetObjectAlignment(Object*);
extern short CMach_GetTypeAlign(Type*, int);
extern int RC1_AllocationAlignment(Type*, u32);
extern VarInfo* Registers_GetVarInfo(Object*);
extern u8 Registers_ClassForType(Type*);
extern void Coerce_to_register(Operand*, Type*, short);
extern void Coerce_to_fp_register(Operand*, Type*, short);
extern void Coerce_to_v_register(Operand*, Type*, short);
extern void store_small_block_from_reg(short, Operand*, Type*, short);
extern void store_small_block_from_reg_pair(short, short, Operand*, Type*, short);
extern u8 CMach_PassResultInHiddenArg(Type*);
extern PCode* emitpcode(short, ...);
extern void CError_Internal(const char*, int);
extern const char RC1_InstrSelection_source_file[];
extern u8 RC1_soft_float, RC1_use_paired_moves_for_double;

#ifdef WII_RETRO_STOCK_ALIGNMENT
extern u8 RC1_legacy_struct_alignment;
#endif

static inline int type_operand_alignment(Type* type) {
#ifdef WII_RETRO_STOCK_ALIGNMENT
    if (!RC1_legacy_struct_alignment)
        return RC1_AllocationAlignment(type, 0);
#endif
    return CMach_GetTypeAlign(type, 0);
}

static inline short operand_alignment(Object* object, Type* type) {
    if (object)
        return CMach_GetObjectAlignment(object);
    return type_operand_alignment(type);
}

static inline void init_operand(Operand* operand) {
    memclrw(operand, sizeof(*operand));
    operand->reg = -1;
    operand->reg_hi = -1;
}

#define IS(e, k) ((e)->kind == (k))
#define INTEGRAL(t) (((TypeIntegral*)(t))->integral)
#define VECTOR(t) ((t)->kind == StructureType && ((TypeStruct*)(t))->subtype > 3 && ((TypeStruct*)(t))->subtype < 22)
#define SMALL_VECTOR_CPU(cpu) ((cpu) == 0x17 || (cpu) == 0x1c || (cpu) == 0x26)
#define SCALAR_INT(t) ((t)->kind == IntegerType && INTEGRAL(t) < FirstNonScalarIntegral)
#define SCALAR_FLOAT(t) ((t)->kind == FloatType && INTEGRAL(t) < FirstNonScalarIntegral)
#define IS_64BIT_INT(t) ((SCALAR_INT(t) || (t)->kind == EnumType) && (t)->size == 8)
#define SCALAR_TYPE(t) (SCALAR_INT(t) || (t)->kind == EnumType || (t)->kind == PointerType || \
                        ((t)->kind == MemberPointerType && ((TypeMemberPointer*)(t))->target->kind != FunctionType))
#define GEN(e, out) cgdispatch[(e)->kind]((e), -1, -1, (out))
#define ENSURE(op, k, coerce, t, r) \
    do { \
        if ((op).kind != (k)) \
            coerce(&(op), (t), (r)); \
    } while (0)

#endif
