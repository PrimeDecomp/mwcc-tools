#ifndef COMPILER_INLINEASM_H
#define COMPILER_INLINEASM_H

#include <setjmp.h>
#include "compiler/common.h"
#include "compiler/PCodeInfo.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
// Pro7's IAMnemonic (InlineAsmMnemonicsPPC.c's table)
struct IAMnemonic {
    char* name;
    // TODO: invented name (Pro7's x4): the opcodeinfo index
    UInt32 opcode;
    char* format;
    UInt32 cpu;
    // TODO: invented name (Pro7's x10): the instruction word, its low bits flagging the forms
    // (1 link, 2 absolute, 0x400 overflow)
    UInt32 instruction;
};

// Pro7's IARegister
struct IARegister {
    char* name;
    RegClass rclass;
    SInt32 num;
    struct Object* object;
};

typedef enum IAOperandType {
    // TODO: invented names for Pro7's IAOpnd_0, 3, 4, 6 and 7
    IAOpnd_Placeholder, // no operand (PCOp_PLACEHOLDEROPERAND)
    IAOpnd_Imm,
    IAOpnd_Reg,
    IAOpnd_GlobalObj, // an object other than a local, with a relocation (u.obj)
    IAOpnd_LocalObj, // a local (u.obj)
    IAOpnd_Lab,
    IAOpnd_GCCReg, // u.gccreg
    IAOpnd_GCCImm, // u.gccimm
    IAOpnd_LabDiff
} IAOperandType;

typedef struct IAOperand {
    IAOperandType type;
    Boolean negated;
    union {
        // IAOpnd_Imm
        struct {
            SInt32 value;
        } imm;
        // IAOpnd_Reg
        struct {
            // (2.6: -3 for an @hiword reference to a register pair's high half)
            SInt32 num;
            struct Object* object;
            short effect;
            short rclass;
        } reg;
        // IAOpnd_GlobalObj, IAOpnd_LocalObj
        struct {
            SInt32 offset;
            struct Object* obj;
            // the relocation: an IAExprType, which numbers them as PCodeArg.arg does for a
            // memory operand (TODO: invented name; Pro7's unk)
            short reftype;
        } obj;
        // IAOpnd_Lab
        struct {
            struct CLabel* label;
        } lab;
        // IAOpnd_GCCReg and IAOpnd_GCCImm: a GCC-style `%N` operand, the Nth of the asm
        // statement's operand list, replaced once the list is known (GCCInlineAsm.c). The two
        // share a layout: `kind` is 2 for a register and 1 for an immediate. (TODO: invented
        // names; Pro7's unk6 {num, unk4, effect, rclass} and unk7 {value, unk1, unk2, unk3})
        struct {
            SInt32 num;
            UInt8 kind;
            short effect;
            short rclass;
        } gccreg;
        struct {
            SInt32 value;
            Boolean kind;
            short effect;
            short rclass;
        } gccimm;
        // IAOpnd_LabDiff
        struct {
            struct CLabel* label1;
            struct CLabel* label2;
            SInt32 offset;
        } labdiff;
    } u;
} IAOperand;

// An assembler expression's kind, also an object operand's relocation (IAOperand.u.obj.reftype,
// numbered as RefType): IAExpr_5 a plain value, IAExpr_6 its @l, IAExpr_7 its @h, IAExpr_8 its @ha
// (TODO: Pro7's placeholder names)
typedef enum IAExprType {
    IAExpr_0,
    IAExpr_1,
    IAExpr_2,
    IAExpr_3,
    IAExpr_4,
    IAExpr_5,
    IAExpr_6,
    IAExpr_7,
    IAExpr_8,
    IAExpr_9,
    IAExpr_10
} IAExprType;

typedef struct IAExpr {
    IAExprType type;
    UInt8 flags;
    short reg; // the base register of `var` (1 for a local)
    SInt32 value;
    struct Object* object; // a register variable pointing to a struct, the base of `value`
    // `var - var2 + value`, `label - label2 + value` (TODO: invented names; Pro7's xC, x10, x18)
    struct Object* var;
    struct Object* var2;
    struct CLabel* label;
    struct CLabel* label2;
} IAExpr;

enum {
    IAFlag1 = 1,
    IAFlag2 = 2
};

// ia->flags2: the mnemonic's suffixes and `volatile` (TODO: invented names; Pro7's IAFlagsB_1
// to IAFlagsB_40)
enum {
    IAFlagsB_Record = 1, // `.`
    IAFlagsB_Overflow = 2, // `o`
    IAFlagsB_Absolute = 4, // `a`
    IAFlagsB_Link = 8, // `l`
    IAFlagsB_HintPlus = 0x10, // a conditional branch's `+`
    IAFlagsB_HintMinus = 0x20, // `-`
    IAFlagsB_Volatile = 0x40 // keep the instruction as written (side effects)
};

typedef struct InlineAsm {
    Opcode opcode;
    unsigned char flags;
    unsigned char flags2;
    short argcount;
    IAOperand args[0];
} InlineAsm;

// Pro7's IALookupResult (InlineAsm_LookupSymbol's result)
typedef struct IALookupResult {
    struct HashNameNode* name;
    struct CLabel* label;
    struct Object* object;
    struct Type* type;
    UInt32 value;
    Boolean has_value;
} IALookupResult;

// An `entry` directive: an InlineAsm with opcode IADirective_Entry, written through its InlineAsm
// header (TODO: Pro7's x0, x2 and x4 named after the InlineAsm fields they overlay)
typedef struct IAEntryPoint {
    short opcode;
    char flags;
    int argcount;
    struct Object* object;
    SInt32 size;
} IAEntryPoint;

// What an asm instruction does to a variable (CodeGen_GetAsmEffects, for the IR optimizer's use-def
// analysis; TODO: invented names for Pro7's IAEffect_0 to IAEffect_4)
typedef enum IAEffectType {
    IAEffect_Read, // a use
    IAEffect_Write, // a definition, of all of it when it covers the variable's size
    IAEffect_Clobber, // a partial definition (2.6's assembler sets none)
    IAEffect_AddressOf, // its address taken: an operand neither read nor written (`la r3, x`)
    IAEffect_ReadWrite // a use and a definition (2.6's assembler sets none)
} IAEffectType;

struct IAEffect {
    IAEffectType type;
    struct Object* object;
    SInt32 offset;
    SInt32 size;
};

enum {
    IAMaxOperands = 16,
    IAMaxLabels = 16
};

struct IAEffects {
    // Pro7's x0: an optimization barrier (side effects, or a branch to an absolute address)
    UInt8 optimizationBarrier;
    // Pro7's x1 (2.6 sets it for an instruction writing memory); IroVars invalidates noregister
    // variables as for branchWithLink
    UInt8 writesMemory;
    // Pro7's x2 (2.6 sets it for an instruction reading memory); IROUseDef marks reaching callDefs
    UInt8 readsMemory;
    // Pro7's x3: indirect branches, label differences, branches without labels
    UInt8 unmodeledControlFlow;
    // Pro7's x4
    UInt8 branchWithLink;
    // Pro7's x5: a `b`, which has no fallthrough
    UInt8 noFallthrough;
    SInt32 numoperands;
    SInt32 numlabels;
    IAEffect operands[IAMaxOperands];
    struct CLabel* labels[IAMaxLabels];
};
#pragma pack(pop)

extern int backtracking;
extern jmp_buf backtrack;
extern jmp_buf InlineAsm_assemblererror;
extern int allow_array_expressions;
extern Boolean InlineAsm_setjmp_valid;

extern void AssemblerError(void);
extern void InlineAsm_SyntaxError(short code);
extern CLabel* InlineAsm_LookupLabel(HashNameNode* name);
extern CLabel* InlineAsm_DeclareLabel(HashNameNode* name);
extern Boolean InlineAsm_LookupSymbolOrTag(
    HashNameNode* name, IALookupResult* result, Boolean allow_tag
);
extern Boolean InlineAsm_LookupSymbol(HashNameNode* name, IALookupResult* result);
extern SInt32 InlineAsm_StructMemberOffset(Type* type);
extern SInt32 InlineAsm_StructArrayMemberOffset(Type* type);
extern SInt32 InlineAsm_StructPointerMemberOffset(Type* type);
extern SInt32 InlineAsm_ConstantExpression(void);
extern void InlineAsm_ScanStatements(volatile short endToken);
extern void InlineAsm_ScanFunction(volatile short endToken);
extern void InlineAsm_Assemble(void);
extern void InlineAsm_PackAsmStatement(
    Statement* stmt, Statement* first, void** output, SInt32* outsize
);
extern void InlineAsm_UnpackAsmStatement(
    Statement* stmt, CLabel** labelArray, Boolean flag, void* data, SInt32 size
);
extern void InlineAsm_CheckLocalUsage(Statement* stmt);
extern CLabel* InlineAsm_GetReferencedLabel(Statement* stmt);
extern CLabel* InlineAsm_GetReferencedLabel2(Statement* stmt);
extern Object* InlineAsm_GetObjectOffset(void* p, SInt32 index, SInt32* offset);
extern char* InlineAsm_DumpStatement(Statement* stmt);

// Pro7's helpers (unknown names)
inline void InlineAsm_InitExpr5(IAExpr* expr, SInt32 value) {
    expr->type = IAExpr_5;
    expr->flags = 0;
    expr->value = value;
    expr->reg = 0;
    expr->object = NULL;
    expr->var = NULL;
    expr->var2 = NULL;
    expr->label = NULL;
    expr->label2 = NULL;
}

inline int InlineAsm_CheckExpr(IAExpr* expr) {
    return (expr->var == NULL && expr->object == NULL && expr->label == NULL);
}

inline SInt32 InlineAsm_GetExprValue(IAExpr* expr) {
    switch (expr->type) {
        case IAExpr_8:
            return HIGH_PART(expr->value);
        case IAExpr_7:
            return (short)(expr->value >> 16);
        case IAExpr_6:
            return (short)expr->value;
        default:
            return expr->value;
    }
}

#ifdef __cplusplus
}
#endif

#endif
