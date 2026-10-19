#ifndef COMPILER_IROUTIL_H
#define COMPILER_IROUTIL_H

#include "compiler/common.h"
#include "compiler/CInt64.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2) // Pro7: options align=mac68k
struct IROList {
    IROLinear* head;
    IROLinear* tail;
};
struct IROListNode {
    IROList list;
    IROListNode* nextList;
};
struct IROElmList {
    IROLinear* element; // Pro7: void*; every 2.6 use holds a node
    IROElmList* next;
};
// An address expression split into its terms (IRO_InitAddrRecordPointer creates one,
// IRO_DecomposeAddressExpression collects the operands of its EADD tree): the object references,
// the integer constants and everything else.
struct IROAddrRecord {
    IROLinear* linear;
    UInt16 numObjRefs;
    IROElmList* objRefs;
    UInt16 numMisc;
    IROElmList* misc;
    UInt16 numInts;
    IROElmList* ints;
    // TODO: invented name (Pro7: x16); IRO_InitAddrRecordPointer clears it, nothing reads it
    int unused;
};
#pragma pack(pop)

extern Object* FunctionName;
extern Boolean IRO_IsLeafFunction;
extern Boolean IRO_FunctionHasReturn;
extern Boolean DisableDueToAsm;
extern Boolean LoopOptimizerRun;

extern Object* IRO_IsVariable(IROLinear* linear);
extern Boolean IRO_IsConstant(IROLinear* linear);
// 2.6: int * (Pro7: SInt32 *): its callers' locals are int (SInt32 ones, October 16: DoTransformations
// 88.3, UpdateUseForOtherSelfAssignment 96.7, BuildOrigIterationCount 97.1)
extern Boolean IRO_IsPow2(IROLinear* linear, int* powvalue);
extern Boolean IRO_IsIntConstant(IROLinear* linear);
extern Boolean IRO_IsFloatConstant(IROLinear* linear);
extern Boolean IRO_IsAssignment(IROLinear* linear);
extern Boolean IRO_TypesEqual(Type* a, Type* b);
extern Type* IRO_UnsignedType(Type* type);
extern Boolean IRO_is_CPtypeequal(Type* a, Type* b);
extern Boolean IRO_ExprsSame(IROLinear* a, IROLinear* b);
extern CLabel* IRO_NewLabel(void);
extern Boolean IRO_ExprsSameSemantically(IROLinear* a, IROLinear* b);
extern IROLinear* IRO_FindFirst(IROLinear* linear);
extern void IRO_CutAndPasteAfter(IROLinear* a, IROLinear* b, IROLinear* c);
extern Boolean IRO_IsConstantZero(IROLinear* linear);
extern Boolean IRO_IsConstantOne(IROLinear* linear);
extern Boolean IRO_IsConstantNegativeOne(IROLinear* linear);
extern void IRO_NopOut(IROLinear* linear);
extern void IRO_NopNonSideEffects(IROLinear* linear, SInt32 level);
typedef void (*IROWalkTreeFunc)(IROLinear* linear, Boolean isEntry);
extern void IRO_WalkTree(IROLinear* linear, IROWalkTreeFunc func);
extern void IRO_WalkInts(IROLinear* a, IROLinear* b, IROWalkTreeFunc func);
extern void IRO_Paste(IROLinear* a, IROLinear* b, IROLinear* c);
extern void IRO_PasteAfter(IROLinear* a, IROLinear* b, IROLinear* c);
extern void IRO_ClipExpr(IROExpr* expr);
extern void IRO_MoveExpression(IROExpr* expr, IROLinear* linear);
extern void IRO_InitList(IROList* list);
extern void IRO_AddToList(IROLinear* linear, IROList* list);
extern IROLinear* IRO_FindLabelNode(CLabel* label, IROLinear* linear);
extern void IRO_DuplicateExprRange(IROLinear* start, IROLinear* end, IROList* list);
extern IROLinear* IRO_DuplicateExpr(IROLinear* linear, IROList* list);
extern IROLinear* IRO_TempReference(Object* obj, IROList* list);
extern IROLinear* IRO_LocateFather(IROLinear* linear);
extern IROLinear* IRO_LocateFather_Cut_And_Paste(IROLinear* a, IROLinear* b);
extern IROLinear* IRO_LocateFather_Cut_And_Paste_Without_Nopping(IROLinear* a, IROLinear* b);
extern void IRO_ReplaceReference(IROLinear* a, Object* obj, IROLinear* b);
extern void IRO_ReplaceReferenceWithNode(IROLinear* a, IROLinear* b);
extern VarRecord* IRO_GetTemp(IROExpr* expr);
extern IROLinear* IRO_AssignToTemp(IROExpr* expr);
extern IROLinear* IRO_FindStart(IROLinear* linear);
extern IROAddrRecord* IRO_InitAddrRecordPointer(IROLinear* linear);
extern IROLinear* IRO_HasSideEffect(IROLinear* linear);
extern IROLinear* IRO_CheckSideEffect(IROLinear* linear);
typedef void (*WalkObjFunc)(Object* obj);
extern void IRO_WalkExcActions(ExceptionAction* action, WalkObjFunc func);
extern Boolean IRO_FunctionCallMightThrowException(IROLinear* linear);
extern IROLinear* IRO_NewIntConst(CInt64 val, Type* type);
extern IROLinear* IRO_NewFloatConst(const Float val, Type* type);
extern Boolean IRO_IsAddressMultiply(IROLinear* linear);
extern void IRO_SetupForUserBreakChecking(void);
extern void IRO_CheckForUserBreak(void);

inline CInt64 IRO_MakeULong(UInt32 i) {
    CInt64 val;
    CInt64_SetULong(&val, i);
    return val;
}

// 2.6's stores the halves directly, where Pro7's calls CInt64_SetLong
inline CInt64 IRO_MakeLong(SInt32 i) {
    CInt64 val;
    val.lo = i;
    val.hi = (i < 0) ? -1 : 0;
    return val;
}

#ifdef __cplusplus
}
#endif

#endif
