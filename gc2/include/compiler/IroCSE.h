#ifndef COMPILER_IROCSE_H
#define COMPILER_IROCSE_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2) // Pro7: options align=mac68k
struct IROExpr {
    Boolean state; // TODO: invented name (Pro7: x0)
    UInt16 index;
    IROLinear* linear;
    Object* temp; // TODO: invented name (Pro7: x8)
    IRONode* node;
    BitVector* depends;
    IROExpr* use; // TODO: invented name (Pro7: x14)
    // IRO_AddExpression copies IRO_CouldError (unknown indirection or possibly zero divisor);
    // IroLoop requires mustreach before hoisting
    Boolean couldError;
    // IRO_AddExpression copies IRO_NotSubable (volatile access, effectful operator or call);
    // IroCSE excludes these candidates
    Boolean notSubable;
    IROLinear* factor; // TODO: invented name (Pro7: x1A)
    VarRecord* var; // TODO: invented name (Pro7: x1E)
    IROLinear* expr; // TODO: invented name (Pro7: x22)
    IROExpr* next;
};
#pragma pack(pop)

extern BitVector* IRO_Depends;
extern Boolean IRO_NotSubable;
extern Boolean IRO_IsVolatile;
extern Boolean IRO_CouldError;
extern IROExpr* IRO_FirstExpr;
extern IROExpr* IRO_LastExpr;
extern SInt32 IRO_NumExprs;

extern void IRO_FindDepends_NoAlloc(IROLinear* linear);
extern void IRO_FindDepends(IROLinear* linear);
extern void IRO_FindExpressions(BitVector* bv, Boolean flag);
extern void IRO_RemoveExpr(IROExpr* expr);
extern void IRO_ComputeAvail(void);
extern void IRO_CommonSubs(void);

#ifdef __cplusplus
}
#endif

#endif
