#ifndef COMPILER_CSOM_H
#define COMPILER_CSOM_H

#include "compiler/common.h"
#include "compiler/som.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
typedef struct CSOMStub {
    struct CSOMStub* next;
    Object* object;
    TypeClass* tclass;
    SInt32 offset;
    // TODO: invented name (Pro7's x10): the glue routine CSOM_Cleanup has CodeGen_SOMStub call,
    // 0 to 2 for rt_som_glue1 to rt_som_glue3
    UInt8 kind;
} CSOMStub;
#pragma pack(pop)

extern CSOMStub* csom_stubs;

extern void CSOM_Setup(Boolean flag);
extern void CSOM_Cleanup(void);
extern void CSOM_CheckFuncType(TypeFunc* tfunc);
extern void CSOM_MakeSOMClass(TypeClass* tclass);
extern void CSOM_ClassComplete(TypeClass* tclass);
extern void CSOM_GenerateClassStructures(TypeClass* tclass);
extern void CSOM_PragmaReleaseOrder(void);
extern void CSOM_PragmaClassVersion(void);
extern void CSOM_PragmaMetaClass(void);
extern void CSOM_PragmaCallStyle(void);
extern void CSOM_FixNewDeleteFunctype(TypeFunc* tfunc);
extern ENode* CSOM_New(TypeClass* tclass);
extern ENode* CSOM_Delete(TypeClass* tclass, ENode* objExpr);
extern void CSOM_InitAutoClass(Object* object);
extern ENode* CSOM_SOMSelfObjectExpr(TypeClass* tclass);
extern void CSOM_InitSOMSelf(TypeClass* tclass, Statement* stmt);
extern ENode* CSOM_EnvCheck(ENode* funccall, ENodeList* checkArg);
extern ENode* CSOM_MemberVarAccess(BClassList* path, ObjMemberVar* ivar, ENode* thisExpr);
extern ENode* CSOM_MethodAccess(BClassList* path, Object* func, Boolean flag);

#ifdef __cplusplus
}
#endif

#endif
