#ifndef COMPILER_IROUSEDEF_H
#define COMPILER_IROUSEDEF_H

#include "compiler/common.h"
#include "compiler/enode.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2) // Pro7: options align=mac68k
// A use of a variable (FindDefsAndUses; chained from IRO_FirstVarUse).
struct IROUse {
    SInt32 index;
    IRONode* node;
    IROLinear* linear;
    VarRecord* var;
    IROUse* globalnext;
    IROUse* varnext;
    BitVector* reachingDefs; // TODO: invented name (Pro7: x18); the definitions reaching it
    UInt16 reachingDefCount; // TODO: invented name (Pro7: x1C); their count
};
// A definition of a variable (MakeDef; chained from FirstDef).
struct IRODef {
    SInt32 index;
    IRONode* node;
    IROLinear* linear;
    VarRecord* var;
    IRODef* globalnext;
    IRODef* varnext;
    UInt16 useCount; // TODO: invented name (Pro7: x18); the uses it reaches
    Boolean global; // TODO: invented name (Pro7: x1A)
    Boolean noregister; // TODO: invented name (Pro7: x1B); MakeDef copies the variable's
    Boolean definite; // TODO: invented name (Pro7: x1C)
    Boolean removable; // TODO: invented name (Pro7: x1D); set by MarkRemovableLoopDefs
};
#pragma pack(pop)

extern ENodeType IRO_NonAssignmentOp[MAXEXPR];
extern IROUse* IRO_FirstVarUse;
extern IROUse* IRO_LastVarUse;

extern CInt64 IRO_GetSelfAssignmentVal(IROLinear* linear);
extern void IRO_InitializeNonAssignmentOpArray(void);
extern void IRO_InitializeAssignmentFoldingFunctionArray(void);
extern Boolean IRO_UseDef(Boolean optDeadAssignments, Boolean optPropagation);
extern void IRO_SplitLifetimes(void);

#ifdef __cplusplus
}
#endif

#endif
