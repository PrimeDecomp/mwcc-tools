#ifndef COMPILER_CTEMPLATEFUNC_H
#define COMPILER_CTEMPLATEFUNC_H

#include "compiler/common.h"
#include "compiler/CTemplateTools.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Boolean CTempl_CanDeduceFunc(Object* object, TypeFunc* tfunc, TemplArg* args);
extern TemplFuncInstance* CTempl_DeduceFunc(
    Object* object1, TypeFunc* tfunc, TemplArg* args, Object* object2, Boolean flag
);
extern Boolean CTempl_FuncIsMoreSpecialized(Object* object1, Object* object2);
extern Object* CTempl_PartialOrdering(Object* object, ObjectList* list, int count);
extern int CTempl_GetTemplateArgumentExpressionIndex(TemplArg* arg);
extern Boolean CTempl_DeduceType(
    Type* type1,
    UInt32 qual1,
    Type* type2,
    UInt32 qual2,
    TemplArg* argArray,
    Boolean flag1,
    Boolean flag2
);
extern void CTempl_FuncMatch(
    NameSpaceObjectList* list,
    TemplArg* args,
    ENodeList* argexprs,
    struct ArgMatch* match13ptr,
    ENode* expr
);
extern Object* CTempl_DeduceFromFunctionCall(
    Object* funcobj, TemplArg* templargs, ENodeList* argexprs
);
extern Object* CTempl_DeduceFromConversion(Object* funcobj, Type* type, UInt32 qual);
extern Object* CTempl_TemplateFunctionCheck(DeclInfo* di, NameSpaceObjectList* nsol);

#ifdef __cplusplus
}
#endif

#endif
