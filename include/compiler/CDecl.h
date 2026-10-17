#ifndef COMPILER_CDECL_H
#define COMPILER_CDECL_H

#include "compiler/common.h"
#include "compiler/tokens.h"
#include "compiler/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
struct ClassLayout {
    ObjBase** objlist;
    ObjMemberVar* vtable_ivar;
    UInt16 lex_order_count;
    // TODO: invented name (Pro7: xA): the lowest index in objlist of a virtual function whose
    // vtable slot is not yet allocated (CABI_AllocateVTable starts there)
    UInt16 firstVirtualSlot;
    Boolean has_vtable;
};

// TODO: invented names for Pro7's offset-named members (x10 ... x57, x64), from what their
// users do with them; the others are Pro7's.
struct DeclInfo {
    Type* thetype;
    UInt32 qual;
    NameSpace* nspace;
    HashNameNode* name;
    Object* resolvedObject;
    NameSpaceObjectList* resolvedObjects;
    FuncArg* parameterNames;
    NameSpace* parameterScope;
    Type* templateType;
    ENode* arrayBound;
    TemplClass* pendingClass;
    TemplArg* expltargs;
    TemplParam* templateParameters;
    struct TemplateScopeState* templateScope;
    TemplateFunction* templateFunction;
    // CTempl_ExplicitSpecialization sets it; the specialization lookup clears it once matched
    UInt8 requireTemplateClassMember;
    SInt16 operator_token;
    SInt16 storageclass;
    // 2.6: Pro7's section, a halfword (a #pragma section's pseudo-section id)
    SInt16 section;
    UInt8 exportflags; // EXPORT_FLAGS_*
    Boolean hasParameterNames;
    Boolean oldStyleParameters;
    Boolean isNewExpression;
    Boolean hasArrayDimension;
    Boolean missingTypeSpecifier;
    Boolean isType;
    Boolean parserOption;
    Boolean isConstructor;
    Boolean in_friend_decl;
    Boolean isFriendDeclaration;
    Boolean is_extern_c;
    Boolean isNewTypeId;
    Boolean allowZeroLengthArray;
    Boolean allowTemplateArguments;
    Boolean has_expltargs;
    Boolean hasTypename;
    // the operator CDecl_ParseOperatorDecl parsed is a conversion function
    Boolean isConversionOperator;
    // CParser_ParseTypeID's (C++): `()` is an abstract function declarator, and a dependent
    // qualified name without typename is an error
    Boolean isTypeID;
    // CParser_ParseTypeID's caller accepts a template name without arguments ...
    Boolean allowTemplateName;
    // ... and CParser_GetDeclSpecs found one
    Boolean isTemplateName;
    CPrepFileInfo* file;
    CPrepFileInfo* file2;
    SInt32 sourceoffset;
    // the function's first declaration (CDecl_GetFunctionObject's pflag, which
    // IRO_FunctionCallObject keeps here): CDecl_FunctionDeclarator then warns of a missing
    // prototype
    Boolean firstDeclaration;
    // 2.6: __declspec(interrupt ...)'s flags, as #pragma interrupt's
    // TODO: invented name
    UInt8 interruptflags;
    // 2.6: a global register variable's register (see Object.u.data.reginfo)
    ObjRegInfo reginfo;
};

struct BigDeclInfo {
    DeclInfo declinfo;
    DeclInfo declinfo2;
    // TODO: invented names (Pro7: xCC, xCD)
    Boolean unused;
    Boolean valid;
};
#pragma pack(pop)

extern AccessType global_access;

extern Type* CDecl_NewStructType(SInt32 size, SInt16 align);
extern Type* CDecl_NewArrayType(Type* type, SInt32 size);
extern Type* CDecl_NewPointerType(Type* type);
extern Type* CDecl_NewRefPointerType(Type* type);
// (2.6 returns the TypeTemplDep; Pro7: Type*)
extern TypeTemplDep* CDecl_NewTemplDepType(TypeTemplDepType tdt);
extern void CDecl_SetResultReg(TypeFunc* tfunc);
extern void CDecl_SetFuncFlags(TypeFunc* tfunc, UInt32 flags);
extern void CDecl_NewConvFuncType(DeclInfo* declinfo);
extern void CDecl_CompleteType(Type* type);
extern Boolean IsCompleteType(Type* type);
extern Boolean CanAllocObject(Type* type);
extern Boolean CanCreateObject(Type* type);
extern void makethetypepointer(DeclInfo* declinfo, UInt32 qual);
extern void CDecl_AddThisPointerArgument(TypeFunc* tfunc, TypeClass* tclass);
extern void CDecl_MakePTMFuncType(TypeFunc* tfunc);
extern void CDecl_AddArgument(TypeFunc* tfunc, Type* argtype);
extern Boolean CDecl_CheckArrayIntegr(Type* type);
extern void CDecl_ParseDirectFuncDecl(DeclInfo* declinfo);
extern void makememberpointertype(DeclInfo* declinfo, TypeClass* tclass, UInt32 qual);
extern void scandeclarator(DeclInfo* declinfo);
extern void conversion_type_name(DeclInfo* declinfo);
extern Object* CDecl_GetFunctionObject(
    DeclInfo* declinfo, NameSpace* nspace, Boolean* pflag, Boolean someotherflag
);
extern void CDecl_TypedefDeclarator(DeclInfo* declinfo);
extern Boolean CDecl_FunctionDeclarator(
    DeclInfo* declinfo, NameSpace* nspace, Boolean flag, Boolean flag2
);
extern void scandeclaratorlist(DeclInfo* declinfo);
extern void CDecl_ComputeUnderlyingEnumType(TypeEnum* tenum);
extern void scanenum(DeclInfo* declinfo);
extern void CDecl_ScanStructDeclarator(BigDeclInfo* bde);
extern void scanstruct(DeclInfo* declinfo, short structtype);
extern TypeMemberFunc* CDecl_MakeTypeMemberFunc(TypeFunc* tfunc, TypeClass* tclass, Boolean flag);
extern void CDecl_PackDeclInfo(PackedDeclInfo* packed, DeclInfo* declinfo);
extern void CDecl_UnpackDeclInfo(DeclInfo* declinfo, PackedDeclInfo* packed);
extern void CDecl_AddFriend(TypeClass* tclass, Object* friendfunc, TypeClass* friendclass);
// (2.6: FALSE for a copy constructor taking its class by value, the callers reporting it; Pro7:
// void, reporting it here)
extern Boolean CDecl_CheckCtorIntegrity(FuncArg* args, TypeClass* tclass);
extern void CDecl_MakeVBaseList(TypeClass* tclass);
extern Boolean CDecl_CheckNewBase(TypeClass* tclass, TypeClass* baseclass, Boolean is_virtual);
extern TypeMemberFunc* CDecl_MakeDefaultDtorType(TypeClass* tclass, Boolean is_virtual);
extern void CDecl_CompleteClass(ClassLayout* layout, TypeClass* tclass);
extern TypeClass* CDecl_DefineClass(
    NameSpace* nspace,
    HashNameNode* name,
    TypeClass* tclass,
    short mode,
    Boolean flag2,
    Boolean flag3
);
extern void CDecl_ParseClassDeclSpec(UInt8* declspec);
extern void CDecl_ParseClass(DeclInfo* declinfo, short mode, Boolean flag1, UInt8 class_declspec);

#ifdef __cplusplus
}
#endif

#endif
