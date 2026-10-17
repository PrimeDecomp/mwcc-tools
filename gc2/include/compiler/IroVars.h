#ifndef COMPILER_IROVARS_H
#define COMPILER_IROVARS_H

#include "compiler/common.h"
#include "compiler/enode.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2) // Pro7: options align=mac68k
struct VarRecord {
    UInt16 index;
    Object* object;
    int unused; // TODO: invented name (Pro7: x6); IRO_FindVar clears it, nothing reads it
    // TODO: invented name (Pro7: xA); IroLoop's induction states 0, 1 and 2
    Boolean inductionState;
    // TODO: invented name (Pro7: xB); IRO_FindVar sets it from VarInfo noregister
    Boolean noregister;
    Boolean usedAtCall; // TODO: invented name (Pro7: xC); IROUseDef's MarkUses sets it at calls
    VarRecord* next;
    IRODef* defs;
    IROUse* uses;
    // TODO: invented name (Pro7: x1A); the bit-field type IRO_RewriteBitFieldTemps' temporary
    // stands for
    TypeBitfield* bitfieldType;
    // IRO_RewriteBitFieldTemps clones this expression to replace the bit-field temporary
    IROLinear* bitFieldReplacement; // TODO: invented name (Pro7: x1E)
};
#pragma pack(pop)

extern VarRecord* IRO_FirstVar;
extern SInt32 IRO_NumVars;
extern Boolean IRO_IsBitField;
extern SInt32 IRO_BaseTerms;
extern SInt32 IRO_VarTerms;
extern Boolean IRO_IsModifyOp[MAXEXPR];
extern Boolean IRO_IsAssignOp[MAXEXPR];

extern void IRO_InitializeIRO_IsModifyOpArray(void);
extern void IRO_InitializeIRO_IsAssignOpArray(void);
extern VarRecord* IRO_FindVar(Object* object, Boolean create, Boolean mode);
extern void ResetVars(void);
extern void IRO_FindAllVars(void);
extern void IRO_ZapVarPtrs(void);
extern void IRO_UpdateVars(void);
extern void IRO_AddElmToList(IROLinear* linear, IROElmList** list);
extern void IRO_DecomposeAddressExpression(IROLinear* linear, IROAddrRecord* rec);
extern void IRO_DecomposeAddressExpression_Cheap(IROLinear* linear);
extern VarRecord* IRO_FindAssigned(IROLinear* linear);
extern void IRO_GetKills(IROLinear* linear);
extern void IRO_CheckInit(void);
extern void IRO_RewriteBitFieldTemps(void);
extern void IRO_ScalarizeClassDataMembers(void);

#ifdef __cplusplus
}
#endif

#endif
