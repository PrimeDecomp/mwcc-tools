#ifndef COMPILER_CEXPR_H
#define COMPILER_CEXPR_H

#include "compiler/common.h"
#include "compiler/CScope.h"
#include "compiler/enode.h"
#include "compiler/types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ConIteratorList {
    struct ConIteratorList* next;
    struct ConIterator* iter;
};
struct ConIterator {
    struct ConIterator* parent;
    struct ConIteratorList* children;
    struct TypeClass* tclass;
};
struct ConversionIterator {
    CScopeObjectIterator objiter;
    struct ConIterator myconiter;
    struct ConIterator* coniter;
};
// How an argument converts to a parameter type, by C++ rank (assign_check and friends)
// TODO: invented names (Pro7: CheckResult0..4)
enum {
    CheckResult_NoMatch,
    CheckResult_Exact,
    CheckResult_Promotion,
    CheckResult_Conversion,
    CheckResult_UserConversion
};

// (Pro7: mac68k alignment; 2-byte packing puts ArgMatch.list at 0x1a)
#pragma pack(push, 2)
// Pro7's Match5: how many arguments need each rank of standard conversion
// TODO: invented names (Pro7: Match5, x0..x8)
struct ComparisonValues {
    SInt16 exactMatches;
    SInt16 promotions;
    SInt16 conversions;
    // the conversions' summed assign_value (pointer conversions rank by class depth)
    SInt16 conversionWeight;
    SInt16 qualifierMatches;
};
// Pro7's Match13: the best match so far of an overload resolution
// TODO: invented names (Pro7: Match13, anotherm5, xE, match5)
struct ArgMatch {
    struct Object* object;
    // the standard conversions of the arguments
    ComparisonValues standard;
    // the arguments that need a user-defined conversion (CheckResult_UserConversion)
    SInt16 userconversions;
    // the standard conversions inside those user-defined conversions
    ComparisonValues userStandard;
    struct ObjectList* list;
};
#pragma pack(pop)
struct FuncMatchArgs {
    struct ENodeList* exprs;
    struct FuncArg* args;
};
// TODO: Pro7's name (its members: x0, left, right); the operator's result expression and its
// converted operands
struct Conversion {
    ENode* expression;
    ENode* left;
    ENode* right;
};

/* CExpr.c */
typedef Boolean (*NameObjCheckCB)(HashNameNode*, struct Object*);
extern NameObjCheckCB name_obj_check;
extern Boolean disallowgreaterthan;

extern ENode* CExpr_RewriteConst(ENode* expr);
extern void optimizecomm(ENode* expr);
extern short canadd(ENode* expr, SInt32 value);
extern ENode* checkreference(ENode* expr);
extern ENode* pointer_generation(ENode* expr);
extern ENode* CExpr_PointerGeneration(ENode* expr);
extern ENode* oldassignmentpromotion(ENode* expr, Type* type, short qual, Boolean flag);
extern ENode* argumentpromotion(ENode* expr, Type* type, short qual, Boolean flag);
extern ENode* classargument(ENode* expr);
extern ENodeList* CExpr_ScanExpressionList(Boolean is_parens);
extern ENode* CExpr_DoExplicitConversion(Type* type, UInt32 qual, ENodeList* list);
extern ENode* CExpr_MakeNameLookupResultExpr(NameResult* pr);
extern SInt32 scansizeof(void);
extern ENode* getnodeaddress(ENode* expr, Boolean flag);
extern ENode* getpointertomemberfunc(ENode* expr, Type* type, Boolean flag);
extern ENode* CExpr_New_ELOGNOT_Node(ENode* expr);
extern ENode* CExpr_New_EMONMIN_Node(ENode* expr);
extern ENode* CExpr_New_EBINNOT_Node(ENode* expr);
extern ENode* unary_expression(void);
extern ENode* do_castnullcheck(ENode* condexpr, ENode* nullcheckexpr);
extern ENode* CExpr_SafeClassPointerCast(
    ENode* expr, TypeClass* a, TypeClass* b, Boolean typconflag, Boolean pathcheckflag
);
extern ENode* PointerToMemberCast(
    ENode* expr, TypeMemberPointer* tm1, TypeMemberPointer* tm2, Boolean flag
);
extern ENode* CExpr_MemberPointerConversion(ENode* expr, TypeMemberPointer* type, Boolean flag1);
extern ENode* do_typecast(ENode* expr, Type* type, UInt32 qual);
extern ENode* cast_expression(void);
extern ENode* CExpr_New_ELESSEQU_Node(ENode* left, ENode* right);
extern ENode* memberpointercompare(ENodeType nt, ENode* left, ENode* right);
extern ENode* CExpr_New_EEQU_Node(ENode* left, ENode* right);
extern ENode* CExpr_NewDyadicNode(ENode* left, ENodeType nt, ENode* right);
extern ENode* CExpr_New_ECOND_Node(ENode* cond, ENode* expr1, ENode* expr2);
extern ENode* assignment_expression(void);
extern ENode* conv_assignment_expression(void);
extern void CExpr_CheckUnusedExpression(ENode* expr);
extern ENode* s_expression(void);
extern ENode* expression(void);
extern CInt64 CExpr_IntegralConstExprType(Type** tint);
extern ENode* CExpr_IntegralConstOrDepExpr(void);
extern CInt64 CExpr_IntegralConstExpr(void);
extern void CExpr_CheckUnwantedAssignment(ENode* expr);

/* CExpr2.c */
extern ENode* assign_node;
extern Boolean temp_reference_init;

typedef void (*CExprSearchCB)(ENode* expr);
typedef ENode* (*CExprReplaceCB)(ENode* expr);

extern ENode* intconstnode(Type* valueType, SInt32 value);
extern ENode* makemonadicnode(ENode* inner, ENodeType ty);
extern ENode* create_objectnode(Object* object);
extern ENode* CExpr_ConstructObject(
    TypeClass* tclass,
    ENode* addr_expr,
    ENodeList* args,
    Boolean flag1,
    Boolean flag2,
    Boolean flag3,
    Boolean flag4,
    Boolean flag5
);
extern Boolean CExpr_CheckOperator(short token, ENode* left, ENode* right, Conversion* out);
extern Boolean CExpr_CheckOperatorConversion(
    short token, ENode* left, ENode* right, ENodeList* args, Conversion* out
);
extern ENode* CExpr_MakeFunctionCall(ENode* expr, ENodeList* args);
extern Boolean CExpr_GetFuncMatchArgs(
    Object* obj, ENodeList* arguments, ENode* instance, FuncMatchArgs* result
);
extern Boolean CExpr_MatchCompare(Object* obj, ArgMatch* dst, ArgMatch* src);
extern short assign_check(
    ENode* expr, Type* type, UInt32 qual, Boolean flag1, Boolean flag2, Boolean flag3
);
extern ENode* get_address_of_temp_copy(ENode* expr, Boolean flag);
extern void CExpr_CheckArithmConversion(ENode* node, Type* type);
extern Object* CExpr_ConversionIteratorNext(ConversionIterator* ctx);
extern void CExpr_ConversionIteratorInit(ConversionIterator* iter, TypeClass* tclass);
extern ENode* CExpr_ConvertToBool(ENode* node, Boolean flag);
extern ENode* CExpr_ConvertToCondition(ENode* expr);
extern ENode* CExpr_ConvertToIntegral(ENode* expr);
extern ENode* CExpr_GetDefaultArgument(ENode* funcexpr, FuncArg* arg);
extern ENode* CExpr_GenericPtmfCall(Object* obj, TypeFunc* tfunc, ENodeList* arg_exprs);
extern ENode* integralpromote(ENode* expr);
extern ENode* CExpr_GetClassAccessNode(
    BClassList* scope,
    BClassList* baseList,
    ENode* node,
    Object* member,
    AccessType access,
    Boolean checkAccess
);
extern ENode* CExpr_ClassPointerCast(BClassList* path, ENode* node, Boolean checkNull);
extern Boolean CExpr_MatchAssign(
    Type* target, UInt32 qualifiers, ENode* expression, ArgMatch* scores
);
extern void CExpr_MatchCV(Type* ty1, UInt32 quals1, Type* ty2, UInt32 quals2, ArgMatch* ctx);
extern ENode* CExpr_FuncCallSix(
    Object* obj, ENode* a1, ENode* a2, ENode* a3, ENode* a4, ENode* a5, ENode* a6
);
extern ENode* funccallexpr(Object* func, ENode* arg1, ENode* arg2, ENode* arg3, ENode* arg4);
extern ENode* CExpr_AdjustFunctionCall(ENode* p);
extern ENode* CExpr_IsTempConstruction(ENode* e, Type* type, ENode** out);
extern ENode* create_objectnode2(Object* obj);
extern ENode* create_objectrefnode(Object* obj);
extern ENode* CExpr_MakeObjRefNode(Object* obj, Boolean flag);
extern Boolean CExpr_IsLValue(ENode* expr);
extern ENode* CExpr_TempModifyExpr(ENode* expr);
extern ENode* promote(ENode* expr, Type* type);
extern void CExpr_ArithmeticConversion(ENode** left, ENode** right);
extern CInt64 CExpr_IntConstConvert(Type* type, Type* otherType, CInt64 value);
extern ENode* CExpr_GetETEMPCopy(ENode* node);
extern void CExpr_AliasTransform(ENode* expr);
extern Boolean CExpr_IsOne(ENode* expr);
extern short isnotzero(ENode* expr);
extern short iszero(ENode* expr);
extern ENode* makecommaexpression(ENode* a, ENode* b);
// TODO: Pro7 takes ENodeType; with it, CExpr's pointercompare (which passes its ENodeType `nt`)
// loses its match
extern ENode* makediadicnode(ENode* left, ENode* right, UInt8 ty);
extern ENode* CExpr_BinaryFloatExpression(ENode* node);
extern ENode* CExpr_UnaryFloatExpression(ENode* ene);
extern ENode* CExpr_NewENode(ENodeType ty);
extern ENode* nullnode(void);
extern ENode* CExpr_NewETEMPNode(Type* type, Boolean assign_id);
extern ENode* CExpr_NewTemplDepENode(TemplDepSubType t);
extern ENode* forceintegral(ENode* expr);
extern Boolean CExpr_AllBitsSet(ENode* expr);
// (Pro7: int count; with it the definition compiles another way)
extern void CExpr_SearchExprTree(ENode* expr, CExprSearchCB callback, SInt32 count, ...);
extern ENode* CExpr_VarArgPromotion(ENode* expr, Boolean allowWarning);
extern ENode* CExpr_GenericFuncCall(
    BClassList* path,
    ENode* funcexpr,
    Boolean flag1,
    Object* obj,
    NameSpaceObjectList* nsol,
    TemplArg* templargs,
    ENodeList* nodes,
    Boolean flag2,
    Boolean flag3,
    Boolean flag4
);
extern SInt16 user_assign_check(
    ENode* operand,
    Type* targetType,
    UInt32 targetQual,
    Boolean diagnose,
    Boolean allowExplicit,
    Boolean conversionMode
);
extern ENode* CExpr_LValue(ENode* expr, Boolean checkConst, Boolean reportError);
extern ENode* scannew(Boolean flag);
extern ENode* scandelete(Boolean flag);

/* CExprConvMatch.c */
extern ENode* CExpr_AssignmentPromotion(ENode* expr, Type* type2, UInt32 qual2, Boolean flag);
extern Boolean CExpr_CanImplicitlyConvert(ENode* expr, Type* type2, UInt32 qual2);
extern ENode* CExpr_Convert(
    ENode* expr, Type* type, UInt32 qual, Boolean isExplicit, Boolean flag2
);
extern void CExpr_FuncArgMatch(
    NameSpaceObjectList* list,
    TemplArg* templargs,
    ENodeList* argexprs,
    struct ArgMatch* match13,
    ENode* expr,
    Boolean flag
);
extern Boolean CExpr_CondOperatorMatch(ENode* left, ENode* right, Conversion* conv);
extern Boolean CExpr_OperatorMatch(short token, ENode* left, ENode* right, Conversion* conv);

#ifdef __cplusplus
}
#endif

#endif
