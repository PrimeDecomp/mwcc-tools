#ifndef COMPILER_TEMPLATES_H
#define COMPILER_TEMPLATES_H

#include "compiler/common.h"
#include "compiler/objects.h"
#include "compiler/types.h"
#include "compiler/CInline.h"
#include "compiler/CPrep.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
struct PackedDeclInfo {
    Type* thetype;
    UInt32 qual;
    NameSpace* nspace;
    HashNameNode* name;
    TemplArg* expltargs;
    SInt16 storageclass;
    // 2.6: Pro7's Section, a halfword
    SInt16 section;
    UInt8 exportflags;
    Boolean has_expltargs;
};

struct TemplateFriend {
    PackedDeclInfo decl;
    FileOffsetInfo fileoffset;
    TokenStream stream;
};

struct TemplateMember {
    TemplateMember* next;
    TemplParam* params;
    Object* object;
    FileOffsetInfo fileoffset;
    TokenStream stream;
    CPrepFileInfo* srcfile;
    SInt32 startoffset;
    SInt32 endoffset;
};

struct TemplParam {
    TemplParam* next;
    HashNameNode* name;
    TemplParamID pid;
    union {
        struct {
            Type* type;
            UInt32 qual;
        } typeparam;
        struct {
            Type* type;
            UInt32 qual;
            ENode* defaultarg;
        } paramdecl;
        struct {
            TemplParam* plist;
            Type* defaultarg;
        } templparam;
    } data;
};

struct TemplArg {
    TemplArg* next;
    TemplParamID pid;
    union {
        struct {
            Type* type;
            UInt32 qual;
        } typeparam;
        struct {
            ENode* expr;
            Boolean is_ref;
        } paramdecl;
        Type* ttargtype;
    } data;
    Boolean is_deduced;
};

struct DeduceInfo {
    TemplArg* args;
    TemplArg argBuffer[16];
    int maxCount;
    int count;
    // TODO: invented name (Pro7: x12C): the deduced parameters' nindex
    UInt8 nindex;
};

struct DefAction {
    DefAction* next;
    TemplateAction* action;
    ObjBase* refobj;
    // TODO: Pro7's placeholder name; never read or written
    int xC;
    TypeEnum* enumtype;
};

struct TypeDeduce {
    TemplClass* tmclass;
    TemplClassInst* inst;
    TemplParam* params;
    TemplArg* args;
    DefAction* defActions;
    // TODO: invented name (Pro7: x14)
    Boolean processingClassTypes;
    // TODO: invented name (Pro7: x15)
    Boolean processingArgument;
    // TODO: invented name (Pro7: x16, never set): quiets errors 124 and 232 when set
    Boolean suppressErrors;
    // TODO: invented name (Pro7: x17): the instance has virtual bases its template lacks
    Boolean hasNewVBases;
    UInt8 nindex;
};

struct TemplPartialSpec {
    TemplPartialSpec* next;
    TemplClass* templ;
    TemplArg* args;
};

struct TemplStack {
    TemplStack* next;
    union {
        Object* func;
        TypeClass* theclass;
    } u;
    Boolean is_func;
    Boolean is_poi;
};

/* Classes */

struct TemplClass {
    TypeClass theclass;
    TemplClass* next;
    TemplClass* templ_parent;
    TemplClassInst* inst_parent;
    TemplParam* templ__params;
    TemplateMember* members;
    TemplClassInst* instances;
    TemplClass* pspec_owner;
    TemplPartialSpec* pspecs;
    TemplateAction* actions;
    UInt16 lex_order_count;
    SInt8 align;
    UInt8 flags;
};

// TemplClass::flags
enum {
    TEMPLCLASS_HAS_VTABLE = 1,
    TEMPLCLASS_FLAGS_2 = 2
};

struct TemplClassInst {
    TypeClass theclass;
    TemplClassInst* next;
    TemplClassInst* parent;
    TemplClass* templ;
    TemplArg* inst_args;
    TemplArg* oargs;
    Boolean is_instantiated;
    Boolean is_specialized;
    Boolean is_extern;
    Boolean static_instantiated;
};

#define TEMPL_CLASS(ty) ((TemplClass*)(ty))
#define TEMPL_CLASS_INST(ty) ((TemplClassInst*)(ty))
#define IS_TEMPL_CLASS(ty) (IS_TYPE_CLASS(ty) && (TYPE_CLASS(ty)->flags & CLASS_IS_TEMPL))
#define IS_TEMPL_CLASS_INST(ty) (IS_TYPE_CLASS(ty) && (TYPE_CLASS(ty)->flags & CLASS_IS_TEMPL_INST))

/* Functions */

struct TemplateFunction {
    TemplateFunction* next;
    // TODO: invented name (Pro7: unk4): the template this one was copied from
    TemplateFunction* original;
    HashNameNode* name;
    TemplParam* params;
    TokenStream stream;
    TStreamElement deftoken;
    Object* tfunc;
    TemplFuncInstance* instances;
    CPrepFileInfo* srcfile;
    SInt32 startoffset;
    SInt32 endoffset;
};

struct TemplFuncInstance {
    TemplFuncInstance* next;
    Object* object;
    TemplArg* args;
    Boolean is_instantiated;
    Boolean is_specialized;
    Boolean is_extern;
};

#define TEMPL_FUNC(ty) ((TemplateFunction*)(ty))
#define TEMPL_FUNC_INST(ty) ((TemplFuncInstance*)(ty))
#define IS_TEMPL_FUNC(ty) (IS_TYPE_FUNC(ty) && (TYPE_FUNC(ty)->flags & FUNC_IS_TEMPL))

/* Actions */

typedef enum TemplateActionType {
    TAT_NESTEDCLASS,
    TAT_ENUMTYPE,
    TAT_FRIEND,
    TAT_ENUMERATOR,
    TAT_BASE,
    TAT_OBJECTINIT,
    TAT_USINGDECL,
    TAT_OBJECTDEF,
    TAT_ILLEGAL
} TemplateActionType;

struct TemplateAction {
    TemplateAction* next;
    TStreamElement source_ref;
    union {
        TemplClass* tclasstype;
        TypeEnum* enumtype;
        TemplateFriend* tfriend;
        struct {
            ObjEnumConst* objenumconst;
            ENode* initexpr;
        } enumerator;
        struct {
            Type* type;
            ClassList* insert_after;
            AccessType access;
            Boolean is_virtual;
        } base;
        struct {
            Object* object;
            ENode* initexpr;
        } objectinit;
        struct {
            TypeTemplDep* type;
            AccessType access;
        } usingdecl;
        ObjBase* refobj;
    } u;
    TemplateActionType type;
};

// TODO: Pro7's DeclFucker (CDecl.h): the scope a template declaration is parsed in
struct TemplateScopeState {
    NameSpace* scope;
    NameSpace* linkedNamespace;
};
#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif
