#ifndef COMPILER_IROLINEARFORM_H
#define COMPILER_IROLINEARFORM_H

#include "compiler/common.h"
#include "compiler/enode.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2) // Pro7: options align=mac68k
typedef struct IROLinearIRSave {
    IROLinear* firstLinear;
    IROLinear* lastLinear;
    UInt32 numLinear;
    Statement* curStat;
    Boolean disableDueToAsm;
    Boolean isLeafFunction;
    Boolean functionHasReturn;
    void* nullCheckList;
    Statement* currStmt;
    Statement* prevStmt;
} IROLinearIRSave;

// The kinds of IR node (IROLinear.type), as the optimizer's dump prints them.
typedef enum IROLinearType {
    IROLinearNop,
    IROLinearOperand,
    IROLinearOp1Arg,
    IROLinearOp2Arg,
    IROLinearGoto,
    IROLinearIf,
    IROLinearIfNot,
    IROLinearReturn,
    IROLinearLabel,
    IROLinearSwitch,
    IROLinearOp3Arg,
    IROLinearFunccall,
    IROLinearEntry,
    IROLinearExit,
    IROLinearBeginCatch,
    IROLinearEndCatch,
    IROLinearEndCatchDtor,
    IROLinearAsm,
    IROLinear18,
    IROLinear19,
    IROLinearEnd
} IROLinearType;

// An IR node's flags (IROLinear.flags), as the optimizer's dump prints them (<reffed>, <assigned>,
// <used>, ...); its other bits are not named.
enum {
    IROLF_1 = 0x1,
    IROLF_Reffed = 0x2,
    IROLF_Assigned = 0x4,
    IROLF_8 = 0x8,
    IROLF_Used = 0x10,
    IROLF_Ind = 0x20,
    IROLF_Subs = 0x40,
    IROLF_80 = 0x80,
    IROLF_LoopInvariant = 0x100,
    IROLF_BeginLoop = 0x200,
    IROLF_EndLoop = 0x400,
    IROLF_Ris = 0x800,
    IROLF_Immind = 0x1000,
    IROLF_VecOp = 0x2000,
    IROLF_4000 = 0x4000,
    IROLF_8000 = 0x8000,
    IROLF_VecOpBase = 0x10000,
    IROLF_20000 = 0x20000,
    IROLF_CounterLoop = 0x40000,
    IROLF_BitfieldIndirect = 0x80000,
    IROLF_CouldError = 0x100000
};

// An IR node (mwccppc 8's name is LinearNode).
struct IROLinear {
    IROLinearType type;
    ENodeType nodetype;
    SInt32 flags;
    UInt16 nodeflags;
    UInt16 index;
    Statement* stmt;
    Type* rtype;
    IROExpr* expr;
    struct ERange* range; // TODO: invented name (Pro7: x16); IroRangePropagation's value range
    struct PointsToFunction* pointsToFunction;
    // TODO: invented name (Pro7: x1E); the pointer analysis' EvalProcAction marks a node evaluated
    Boolean evaluated;
    union {
        // Operand
        ENode* node;
        // Op1Arg (and the catch kinds, where ctch.linear is the same pointer)
        IROLinear* monadic;
        // Op2Arg
        struct {
            IROLinear* left;
            IROLinear* right;
        } diadic;
        // Op3Arg
        struct {
            IROLinear* a;
            IROLinear* b;
            IROLinear* c;
        } args3;
        // Funccall
        struct {
            char ispascal; // an EFUNCCALLP
            short argCount;
            IROLinear** args;
            IROLinear* callee; // TODO: invented name (Pro7: linear8, "funcref")
            TypeFunc* functype;
            struct LocationSetSet* returnedLocs;
        } funccall;
        // Asm
        Statement* asm_stmt;
        // If, IfNot, Goto, Label
        struct {
            CLabel* label;
            IROLinear* cond; // TODO: invented name (Pro7: x4); If and IfNot only
        } label;
        // Switch
        struct {
            SwitchInfo* info;
            IROLinear* cond; // TODO: invented name (Pro7: x4)
        } swtch;
        // BeginCatch, EndCatch, EndCatchDtor
        struct {
            IROLinear* linear;
            int unused1; // TODO: invented name (Pro7: x4); only IRO_Linearize writes it (0)
            int unused2; // TODO: invented name (Pro7: x8); only IRO_Linearize writes it (0)
        } ctch;
    } u;
    IROLinear* next;
};
#pragma pack(pop)

extern IROLinear* IRO_FirstLinear;
extern IROLinear* IRO_LastLinear;
extern UInt32 IRO_NumLinear;
extern Statement* CurStat;

extern IROLinear* IRO_NewLinear(IROLinearType type);
extern void IRO_PreLinearize(Statement* stmt);
extern void IRO_Linearize(Statement* stmt);
extern ENode* IRO_NewENode(ENodeType nodetype);
extern Statement* IRO_Delinearize(IRONode* node, IROLinear* linear);
extern void IRO_RenumberInts(void);
extern void IRO_UpdateFlagsOnInts(void);
extern void IRO_SaveLinearIR(IROLinearIRSave* save);
extern void IRO_RestoreLinearIR(IROLinearIRSave* save);

#define IS_LINEAR_ENODE(_linear, _nodetype) \
    (((_linear)->type == IROLinearOperand) && ((_linear)->u.node->type) == (_nodetype))
#define IS_LINEAR_MONADIC(_linear, _nodetype) \
    (((_linear)->type == IROLinearOp1Arg) && ((_linear)->nodetype) == (_nodetype))
#define IS_LINEAR_MONADIC_2(_linear, _nodetype1, _nodetype2) \
    (((_linear)->type == IROLinearOp1Arg) && \
     (((_linear)->nodetype) == (_nodetype1) || ((_linear)->nodetype) == (_nodetype2)))
#define IS_LINEAR_DIADIC(_linear, _nodetype) \
    (((_linear)->type == IROLinearOp2Arg) && ((_linear)->nodetype) == (_nodetype))
#define IS_LINEAR_DIADIC_2(_linear, _nodetype1, _nodetype2) \
    (((_linear)->type == IROLinearOp2Arg) && \
     (((_linear)->nodetype) == (_nodetype1) || ((_linear)->nodetype) == (_nodetype2)))
#define IS_LINEAR_DIADIC_3(_linear, _nodetype1, _nodetype2, _nodetype3) \
    (((_linear)->type == IROLinearOp2Arg) && \
     (((_linear)->nodetype) == \
      (_nodetype1) || \
      ((_linear)->nodetype) == \
      (_nodetype2) || \
      ((_linear)->nodetype) == (_nodetype3)))

#ifdef __cplusplus
}
#endif

#endif
