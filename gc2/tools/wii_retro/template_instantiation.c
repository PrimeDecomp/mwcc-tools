#include <stddef.h>

/* Recovered RC1 CTemplateFuncInst visitors and instantiation workers.
 * Their traversal and callback order feed the deferred function queues.
 * deferred_exports.c retains the compatibility wrappers around these workers.
 */
#pragma pack(push, 2)
typedef struct HashNameNode {
    unsigned char unused[0xa];
    char name[2];
} HashNameNode;

typedef struct NameSpace {
    struct NameSpace* parent;
    const HashNameNode* name;
    unsigned char unused_before_class[4];
    struct ClassInstance* theclass;
    unsigned char unused_before_template[0x13];
    unsigned char is_template;
} NameSpace;

typedef struct TypedefObject {
    unsigned char unused_before_type[4];
    const void* type;
    unsigned char unused_before_scope[4];
    NameSpace* scope;
} TypedefObject;

typedef struct TokenStream {
    const void* tokens;
    unsigned size;
} TokenStream;

typedef struct SourceLocation {
    const void* file;
    unsigned words[2];
} SourceLocation;

typedef struct FunctionType {
    unsigned char kind;
    unsigned char unused_before_size;
    int size;
    unsigned char unused_before_result[8];
    void* result;
    unsigned char unused_before_flags[8];
    unsigned flags;
    unsigned char unused_before_owner[4];
    struct ClassInstance* owner;
    unsigned char unused_before_static[8];
    unsigned char is_static;
} FunctionType;

typedef struct Object {
    unsigned char unused[2];
    unsigned char datatype;
    unsigned char unused_before_declaration;
    unsigned declaration_data;
    NameSpace* scope;
    const void* name;
    FunctionType* type;
    unsigned qualifiers;
    unsigned short storage_class;
    unsigned short flags;
} Object;

typedef struct TemplateObject {
    Object object;
    unsigned char unused[0x60-sizeof(Object)];
    const Object* parent;
    Object* point_of_instantiation;
    unsigned char skip_instantiation;
} TemplateObject;

typedef struct ObjMemberVar {
    unsigned char unused[3];
    unsigned char has_path;
    struct ObjMemberVar* next;
    const void* name;
} ObjMemberVar;

typedef struct TemplateInstanceLink TemplateInstanceLink;

typedef struct ClassInstance {
    unsigned char kind;
    unsigned char unused_before_scope[5];
    NameSpace* scope;
    const HashNameNode* name;
    unsigned char unused_before_ivars[8];
    ObjMemberVar* ivars;
    unsigned char unused_before_flags[8];
    unsigned flags;
    unsigned char unused_before_align[6];
    short align;
    unsigned char mode;
    unsigned char unused_before_next;
    struct ClassInstance* next;
    const TemplateInstanceLink* template_object;
    const TemplateInstanceLink* parent_instance;
    const void* arguments;
    unsigned char is_instantiated;
    unsigned char is_specialized;
    unsigned char is_extern;
    unsigned char unused_before_original_arguments;
    const void* original_arguments;
} ClassInstance;

typedef struct PartialSpecialization PartialSpecialization;
typedef struct ClassTemplate {
    unsigned char unused_before_scope[6];
    NameSpace* scope;
    const HashNameNode* name;
    unsigned char unused_before_ivars[8];
    ObjMemberVar* ivars;
    unsigned char unused_before_flags[8];
    unsigned flags;
    unsigned char unused_before_align[6];
    short align;
    unsigned char mode;
    unsigned char unused_before_next;
    struct ClassTemplate* next;
    const TemplateInstanceLink* parent_template;
    const TemplateInstanceLink* parent_instance;
    const void* parameters;
    unsigned char unused_before_instances[2];
    ClassInstance* instances;
    const struct ClassTemplate* partial_owner;
    PartialSpecialization* partials;
} ClassTemplate;

typedef struct ObjTypeTag {
    unsigned char kind;
    unsigned char access;
    ClassInstance* type;
} ObjTypeTag;

struct PartialSpecialization {
    PartialSpecialization* next;
    ClassTemplate* templ;
};

typedef struct TemplateMember {
    const void* parameters;
    const void* scope;
    SourceLocation location;
    TokenStream stream;
    const void* packed_definition;
    const void* packed_initializer;
    const void* source_file;
    int source_offset;
} TemplateMember;

typedef struct ScopeSave {
    NameSpace* current;
    ClassInstance* current_class;
    Object* current_function;
    unsigned char is_member_function;
} ScopeSave;

typedef struct FunctionInstance {
    unsigned char unused[8];
    const void* arguments;
    unsigned char is_instantiated;
    unsigned char is_specialized;
    unsigned char is_extern;
    unsigned char unused_before_next;
    struct FunctionInstance* next;
    void* point_of_instantiation;
    Object* object;
} FunctionInstance;

typedef struct FunctionTemplate {
    Object* fallback_object;
    const void* fallback_context;
    const void* parameters;
    unsigned char unused[2];
    struct FunctionTemplate* next;
    unsigned char unused_before_stream[4];
    TokenStream stream;
    SourceLocation location;
    const void* packed_definition;
    Object* function;
    FunctionInstance* instances;
    const void* source_file;
    int start_offset;
    int end_offset;
} FunctionTemplate;

typedef struct FunctionObject {
    Object object;
    unsigned char unused[0x50-sizeof(Object)];
    Object* secondary_function;
} FunctionObject;

typedef struct DeclInfo {
    unsigned char unused[0x90];
    const void* file;
    const void* source_file;
    int source_offset;
    unsigned char unused_after_source[8];
} DeclInfo;

typedef struct ObjectRemap {
    struct ObjectRemap* next;
    const Object* original;
    Object* replacement;
    unsigned local_index;
} ObjectRemap;

typedef struct VLABound VLABound;

struct TemplateInstanceLink {
    struct TemplateInstanceLink* parent;
    struct TemplateInstanceLink* next;
    unsigned char unused[4];
    unsigned char is_class;
};

typedef struct PackedInstanceContext {
    void* instance;
    unsigned char unused_before_remaps[0x10];
    ObjectRemap* remaps;
    VLABound* array_bounds;
    unsigned char unused_before_capture[7];
    unsigned char capture_array_bounds;
    unsigned char unused_after_capture[2];
} PackedInstanceContext;

typedef struct LocalObject {
    Object object;
    unsigned char unused_before_location[8];
    SourceLocation location;
    unsigned char unused_before_index[0x14];
    unsigned index;
    unsigned char unused_before_typedef[4];
    void* typedef_object;
    unsigned char unused_after_typedef[0x10];
} LocalObject;

typedef struct ArrayType {
    unsigned char kind;
    unsigned char unused_before_size;
    int size;
    FunctionType* element;
    unsigned char unused_before_variable_length[8];
    unsigned char variable_length;
} ArrayType;

typedef struct VLAInfo {
    struct VLAInfo* next;
    const void* type;
    unsigned char unused_before_pointer[4];
    Object* pointer;
    Object* element_count;
} VLAInfo;

typedef struct VLARegistry {
    VLAInfo* head;
} VLARegistry;

typedef struct ObjectLink {
    struct ObjectLink* next;
    Object* object;
} ObjectLink;

typedef struct ExceptionAction {
    struct ExceptionAction* previous;
    Object* object;
    union {
        Object* routine;
        unsigned char needs_destructor;
    } operation;
    union {
        Object* destructor;
        int offset;
        int count;
    } operand;
    int element_size;
    void* catch_type;
    unsigned catch_qualifiers;
    unsigned char kind;
} ExceptionAction;
typedef struct MemberInitializer MemberInitializer;
typedef struct Label Label;

typedef struct ConstantInteger {
    unsigned words[2];
} ConstantInteger;

typedef struct Expression {
    unsigned char kind;
    unsigned char attributes[3];
    FunctionType* type;
    unsigned char unused_before_payload[8];
    union {
        ConstantInteger integer;
        MemberInitializer* initializers;
        struct {
            Object* object;
            void* initializer;
        } declaration;
        struct {
            struct Expression* expression;
            SourceLocation location;
            unsigned char unused[0xc];
            unsigned char kind;
        } dependent;
    } data;
} Expression;

typedef struct InitializerExpression {
    struct InitializerExpression* next;
    Expression* expression;
} InitializerExpression;

struct MemberInitializer {
    MemberInitializer* next;
    union {
        Expression* expression;
        InitializerExpression* list;
    } arguments;
    union {
        ObjMemberVar* member;
        FunctionType* base;
    } target;
    unsigned char is_member;
    unsigned char has_single_expression;
};

typedef struct InitializerNode {
    struct InitializerNode* next;
    struct InitializerNode* children;
    Expression* expression;
} InitializerNode;

typedef struct StoredInitializer {
    union {
        Expression* expression;
        InitializerExpression* list;
        InitializerNode* tree;
    } data;
    unsigned char kind;
} StoredInitializer;

typedef struct PendingCase {
    struct PendingCase* next;
    Label* label;
    Expression *first, *last;
} PendingCase;

typedef struct SwitchInfo {
    unsigned char unused[4];
    PendingCase* pending;
    unsigned char unused_before_type[4];
    FunctionType* type;
} SwitchInfo;

typedef struct Statement {
    struct Statement* next;
    unsigned char kind;
    unsigned char attributes[5];
    Expression* expression;
    union {
        Label* label;
        SwitchInfo* switch_info;
    } target;
    ExceptionAction* cleanups;
    unsigned char unused_after_cleanups[0xc];
} Statement;
#pragma pack(pop)

typedef struct ObjectIterator {
    void* scope;
    void* next_name;
    void* objects;
    int bucket;
} ObjectIterator;

_Static_assert(offsetof(Object, type) == 0x10, "RC1 object type");
_Static_assert(offsetof(Object, declaration_data) == 4, "RC1 object declaration metadata");
_Static_assert(offsetof(Object, scope) == 8, "RC1 object namespace");
_Static_assert(offsetof(Object, name) == 0xc, "RC1 object name");
_Static_assert(offsetof(Object, qualifiers) == 0x14, "RC1 object qualifiers");
_Static_assert(offsetof(Object, flags) == 0x1a, "RC1 object flags");
_Static_assert(offsetof(FunctionType, flags) == 0x1a, "RC1 function flags");
_Static_assert(offsetof(FunctionType, size) == 2, "RC1 type size");
_Static_assert(offsetof(FunctionType, result) == 0xe, "RC1 function result");
_Static_assert(offsetof(FunctionType, owner) == 0x22, "RC1 member function owner");
_Static_assert(offsetof(FunctionType, is_static) == 0x2e, "RC1 static member flag");
_Static_assert(offsetof(TemplateObject, parent) == 0x60, "RC1 template member declaration");
_Static_assert(offsetof(TemplateObject, point_of_instantiation) == 0x64, "RC1 template instantiation point");
_Static_assert(offsetof(TemplateObject, skip_instantiation) == 0x68, "RC1 member instantiation gate");
_Static_assert(offsetof(ClassInstance, scope) == 6, "RC1 class namespace");
_Static_assert(sizeof(ClassInstance) == 0x48, "RC1 class instance size");
_Static_assert(offsetof(ClassInstance, parent_instance) == 0x38, "RC1 parent class instance record");
_Static_assert(offsetof(ClassInstance, arguments) == 0x3c, "RC1 instance arguments");
_Static_assert(offsetof(ClassInstance, original_arguments) == 0x44, "RC1 original instance arguments");
_Static_assert(offsetof(ClassInstance, is_extern) == 0x42, "RC1 extern template instance");
_Static_assert(offsetof(ClassInstance, flags) == 0x22, "RC1 template class flags");
_Static_assert(offsetof(ClassInstance, next) == 0x30, "RC1 class instance link");
_Static_assert(offsetof(ClassInstance, template_object) == 0x34, "RC1 class template declaration");
_Static_assert(offsetof(ClassInstance, ivars) == 0x16, "RC1 instantiated member variables");
_Static_assert(offsetof(ClassTemplate, ivars) == 0x16, "RC1 template member variables");
_Static_assert(offsetof(ObjMemberVar, has_path) == 3, "RC1 member path flag");
_Static_assert(offsetof(ObjMemberVar, next) == 4, "RC1 member variable link");
_Static_assert(offsetof(ObjMemberVar, name) == 8, "RC1 member variable name");
_Static_assert(offsetof(TemplateInstanceLink, next) == 4, "RC1 template instance link");
_Static_assert(offsetof(TemplateInstanceLink, is_class) == 0xc, "RC1 class template record flag");
_Static_assert(offsetof(ClassInstance, is_specialized) == 0x41, "RC1 specialized class instance");
_Static_assert(offsetof(ClassTemplate, next) == 0x30, "RC1 class template link");
_Static_assert(offsetof(ClassTemplate, flags) == 0x22, "RC1 class template flags");
_Static_assert(offsetof(ClassTemplate, parent_template) == 0x34, "RC1 enclosing template record");
_Static_assert(offsetof(ClassTemplate, parent_instance) == 0x38, "RC1 enclosing class instance");
_Static_assert(offsetof(ClassTemplate, parameters) == 0x3c, "RC1 class template parameters");
_Static_assert(offsetof(ClassTemplate, instances) == 0x42, "RC1 class template instances");
_Static_assert(offsetof(ClassTemplate, partial_owner) == 0x46, "RC1 partial specialization owner");
_Static_assert(offsetof(ClassTemplate, partials) == 0x4a, "RC1 class template partial specializations");
_Static_assert(offsetof(ClassTemplate, mode) == 0x2e, "RC1 class template mode");
_Static_assert(offsetof(ClassInstance, mode) == 0x2e, "RC1 class instance mode");
_Static_assert(offsetof(NameSpace, name) == 4, "RC1 namespace name");
_Static_assert(offsetof(NameSpace, theclass) == 0xc, "RC1 namespace class binding");
_Static_assert(offsetof(NameSpace, is_template) == 0x23, "RC1 namespace template flag");
_Static_assert(offsetof(HashNameNode, name) == 0xa, "RC1 hashed name text");
_Static_assert(sizeof(ObjTypeTag) == 6 && offsetof(ObjTypeTag, type) == 2, "RC1 type tag");
_Static_assert(offsetof(TemplateMember, location) == 8, "RC1 member source position");
_Static_assert(offsetof(TemplateMember, stream) == 0x14, "RC1 member token stream");
_Static_assert(offsetof(TemplateMember, packed_definition) == 0x1c, "RC1 member stored definition");
_Static_assert(offsetof(TemplateMember, packed_initializer) == 0x20, "RC1 member stored initializer");
_Static_assert(offsetof(TemplateMember, source_file) == 0x24, "RC1 member browser file");
_Static_assert(offsetof(TemplateMember, source_offset) == 0x28, "RC1 member source offset");
_Static_assert(offsetof(ScopeSave, is_member_function) == 0xc, "RC1 saved class scope");
_Static_assert(offsetof(FunctionInstance, is_instantiated) == 0xc, "RC1 instantiated function flag");
_Static_assert(offsetof(FunctionInstance, is_specialized) == 0xd, "RC1 specialized function flag");
_Static_assert(offsetof(FunctionInstance, next) == 0x10, "RC1 function instance link");
_Static_assert(offsetof(FunctionInstance, object) == 0x18, "RC1 function instance object");
_Static_assert(offsetof(FunctionInstance, arguments) == 8, "RC1 function template arguments");
_Static_assert(offsetof(FunctionInstance, point_of_instantiation) == 0x14, "RC1 function instantiation point");
_Static_assert(offsetof(FunctionTemplate, next) == 0xe, "RC1 function template link");
_Static_assert(offsetof(FunctionTemplate, instances) == 0x32, "RC1 function template instances");
_Static_assert(offsetof(FunctionTemplate, stream) == 0x16, "RC1 function template stream");
_Static_assert(offsetof(FunctionTemplate, location) == 0x1e, "RC1 function template location");
_Static_assert(offsetof(FunctionTemplate, packed_definition) == 0x2a, "RC1 packed template definition");
_Static_assert(offsetof(FunctionTemplate, function) == 0x2e, "RC1 template function declaration");
_Static_assert(offsetof(FunctionTemplate, source_file) == 0x36, "RC1 template source file");
_Static_assert(offsetof(FunctionTemplate, start_offset) == 0x3a, "RC1 template source start");
_Static_assert(offsetof(FunctionTemplate, end_offset) == 0x3e, "RC1 template source end");
_Static_assert(offsetof(FunctionObject, secondary_function) == 0x50, "RC1 secondary template function");
_Static_assert(sizeof(ObjectIterator) == 0x10, "RC1 namespace iterator");
_Static_assert(sizeof(DeclInfo) == 0xa4, "RC1 declaration state");
_Static_assert(offsetof(DeclInfo, file) == 0x90, "RC1 declaration browser file");
_Static_assert(sizeof(PackedInstanceContext) == 0x26, "RC1 packed instantiation context");
_Static_assert(offsetof(PackedInstanceContext, remaps) == 0x14, "RC1 packed object remaps");
_Static_assert(offsetof(PackedInstanceContext, array_bounds) == 0x18, "RC1 dependent array bounds");
_Static_assert(offsetof(PackedInstanceContext, capture_array_bounds) == 0x23, "RC1 array-bound capture");
_Static_assert(sizeof(ObjectRemap) == 0x10, "RC1 packed object remap size");
_Static_assert(sizeof(LocalObject) == 0x60, "RC1 local data object size");
_Static_assert(offsetof(LocalObject, location) == 0x24, "RC1 local source reference");
_Static_assert(offsetof(LocalObject, index) == 0x44, "RC1 packed local index");
_Static_assert(offsetof(LocalObject, typedef_object) == 0x4c, "RC1 local typedef");
_Static_assert(offsetof(TypedefObject, type) == 4, "RC1 typedef type");
_Static_assert(offsetof(TypedefObject, scope) == 0xc, "RC1 typedef scope");
_Static_assert(offsetof(ArrayType, variable_length) == 0x12, "RC1 variable-length array flag");
_Static_assert(offsetof(ArrayType, element) == 6, "RC1 array element type");
_Static_assert(offsetof(VLAInfo, type) == 4, "RC1 VLA type");
_Static_assert(_Alignof(VLARegistry) == 2, "RC1 VLA registry alignment");
_Static_assert(offsetof(VLAInfo, pointer) == 0xc, "RC1 VLA storage object");
_Static_assert(offsetof(VLAInfo, element_count) == 0x10, "RC1 VLA element count");
_Static_assert(sizeof(ExceptionAction) == 0x1e, "RC1 cleanup action size");
_Static_assert(offsetof(ExceptionAction, kind) == 0x1c, "RC1 cleanup action kind");
_Static_assert(offsetof(ExceptionAction, catch_type) == 0x14, "RC1 catch type");
_Static_assert(offsetof(InitializerNode, expression) == 8, "RC1 initializer tree expression");
_Static_assert(offsetof(StoredInitializer, kind) == 4, "RC1 stored initializer kind");
_Static_assert(sizeof(MemberInitializer) == 0xe, "RC1 constructor initializer size");
_Static_assert(offsetof(MemberInitializer, arguments) == 4, "RC1 constructor initializer arguments");
_Static_assert(offsetof(MemberInitializer, target) == 8, "RC1 constructor initializer target");
_Static_assert(offsetof(MemberInitializer, is_member) == 0xc, "RC1 constructor initializer member flag");
_Static_assert(offsetof(MemberInitializer, has_single_expression) == 0xd, "RC1 constructor initializer expression flag");
_Static_assert(sizeof(ObjectLink) == 8, "RC1 object list link");
_Static_assert(sizeof(Statement) == 0x22, "RC1 statement size");
_Static_assert(offsetof(Statement, expression) == 0xa, "RC1 statement expression");
_Static_assert(offsetof(Statement, target) == 0xe, "RC1 statement label");
_Static_assert(offsetof(Statement, cleanups) == 0x12, "RC1 statement cleanup stack");
_Static_assert(offsetof(Expression, type) == 4, "RC1 expression type");
_Static_assert(offsetof(Expression, data) == 0x10, "RC1 expression payload");
_Static_assert(offsetof(Expression, data.declaration.initializer) == 0x14, "RC1 stored declaration initializer");
_Static_assert(offsetof(Expression, data.dependent.location) == 0x14, "RC1 dependent expression source");
_Static_assert(offsetof(Expression, data.dependent.kind) == 0x2c, "RC1 dependent expression kind");
_Static_assert(offsetof(SwitchInfo, pending) == 4, "RC1 pending case list");
_Static_assert(offsetof(SwitchInfo, type) == 0xc, "RC1 switch result type");
_Static_assert(offsetof(PendingCase, first) == 8 && offsetof(PendingCase, last) == 0xc, "RC1 case expressions");

extern const char RC1_CTemplateFuncInst_source_file[];
extern const char RC1_CTemplateTools_source_file[];
extern const char RC1_CTemplateClass_source_file[];
extern void CError_Internal(const char* file, int line);
extern void CError_Warning(int error, ...);
extern void CScope_InitObjectIterator(ObjectIterator* iterator, const void* scope);
extern Object* CScope_NextObjectIteratorObject(ObjectIterator* iterator);
extern unsigned char CParser_IsDataObject(const Object* object);
extern TemplateMember* CTemplTool_GetTemplateMember(const Object* object);
extern ClassTemplate* ctempl_ctemplates;
extern FunctionTemplate* ctempl_ftemplates;
extern unsigned char retro_GenClassInstance(void* instance, unsigned char explicit_instantiation);
unsigned char retro_GenerateFunctionInstance(void* templ, void* instance,
                                             unsigned char explicit_instantiation);
extern void CMid_FreeLocalHeap(void);
extern unsigned char CIMan_NeedsTemplateInstance(Object* object);
extern FunctionTemplate* CTemplTool_Template2TemplFunc(const Object* object);
extern unsigned char RC1_InstantiateStoredDefinition(PackedInstanceContext* context, Object* object,
                                                     void* point, const void* definition,
                                                     unsigned char explicit_instantiation);
extern void memclrw(void* data, unsigned size);
extern void CError_SetErrorSourceRef(const SourceLocation* location);
extern SourceLocation cerror_lockref;
extern void CError_Error(int error, ...);
extern void CPrep_StreamInsert(TokenStream* stream, int* state);
extern void CPrep_StreamRemove(TokenStream* stream, int* state);
extern void CParser_GetDeclSourceInfo(const SourceLocation* location);
extern unsigned char RC1_nosyminline;
extern unsigned char RC1_filesyminfo;
extern unsigned char gForceSourceLoc;
extern SourceLocation cparser_fileoffset;
extern SourceLocation cparser_decloffset, cfunc_funcdefnref;
extern short tk;
extern short lex(void);
extern const void* CPrep_BrowserCurrentFile(void);
extern void* ctempl_curinstance;
extern void* CTemplTool_PushInstance(const void* klass, Object* object);
extern void CTemplTool_PopInstance(void* state);
extern void CTemplTool_MergeArgNames(FunctionType* source, FunctionType* target);
extern NameSpace* CTemplTool_SetupTemplateArgumentNameSpace(const void* parameters,
                                                          const void* arguments, unsigned char flag);
extern void CTemplTool_SetupOuterTemplateArgumentNameSpace(NameSpace* scope);
extern void CTemplTool_RemoveOuterTemplateArgumentNameSpace(NameSpace* scope);
extern void CFunc_ParseFuncDef(Object* object, DeclInfo* declaration, const void* arg,
                               unsigned char flag1, unsigned char flag2, NameSpace* scope);
extern void CBrowse_NewFunction(Object* object, const void* file, const void* source_file,
                                int start_offset, int end_offset);
extern void CDecl_CompleteType(void* type);
extern void* CInline_GetInitializationCopy(const void* initialization, unsigned char generate);
extern void CError_LockErrorSourceRef(const SourceLocation* location, SourceLocation* saved);
extern void CError_UnlockErrorSourceRef(const SourceLocation* saved);
extern void CScope_SetClassScope(ClassInstance* instance, ScopeSave* saved);
extern void CScope_RestoreScope(ScopeSave* saved);
extern void RC1_InstantiateStoredInitializer(PackedInstanceContext* context, StoredInitializer* initializer);
extern void CInit_GlobalInitialization(Object* object, void* initialization, void* context, unsigned char flag);
extern ClassTemplate* CTemplTool_Template2TemplClass(const TemplateInstanceLink* record);
extern ClassTemplate* CTemplClass_GetMasterTemplate(ClassTemplate* templ);
extern unsigned char CTemplTool_EqualArgs(const void* left, const void* right);
extern const HashNameNode* CMangler_TemplateInstanceName(const HashNameNode* base, const void* arguments);
extern const HashNameNode* CParser_AppendUniqueNameFile(const char* prefix);
extern NameSpace* CScope_NewListNameSpace(const HashNameNode* name, unsigned char is_global);
extern void CScope_AddObject(NameSpace* scope, const HashNameNode* name, ObjTypeTag* object);
extern unsigned char CClass_IsLocal(const ClassTemplate* templ);
extern NameSpace* CTemplTool_InsertTemplateArgumentNameSpace(const void* parameters,
                                                            ClassInstance* instance, ScopeSave* saved);
extern void CTemplTool_RemoveTemplateArgumentNameSpace(NameSpace* scope, ClassInstance* instance,
                                                      ScopeSave* saved);
extern void CParser_UpdateObject(Object* object, DeclInfo* declaration);
extern void CInit_InitializeData(Object* object);
extern ObjectLink* arguments;
extern ObjectLink* RC1_pending_array_initializers;
extern const void* no_name_node;
extern const void* temp_argument_name;
extern struct Type stsignedshort;
extern NameSpace* cscope_current;
extern ClassInstance* cscope_currentclass;
extern Object* cscope_currentfunc;
extern unsigned char cscope_is_member_func;
typedef struct NoSideEffectWarningOptions {
    unsigned char enabled;
    unsigned char as_error;
} NoSideEffectWarningOptions;

extern NoSideEffectWarningOptions RC1_warn_no_side_effect;
extern unsigned char fatalerrors;
extern NameSpace* CFunc_FuncGenSetup(NameSpace* scope, Statement* first, Object* func, unsigned char keep_state);
extern void CInline_UnpackIFunctionData(Object* object, const void* definition, Statement* first,
                                       unsigned char template_instantiation);
extern Object* CParser_NewLocalDataObject(const void* name, const void* type);
extern void CFunc_SetupLocalVarInfo(Object* object);
extern void* lalloc(unsigned size);
extern void* CTemplTool_FixTypedefObject(PackedInstanceContext* context, void* object);
extern unsigned char CTemplTool_IsTypeDepType(const void* type);
extern void* CTemplTool_InstantiateType(PackedInstanceContext* context, const void* type, unsigned* qualifiers);
extern void CFunc_ReferenceArgumentRewrite(Object* object, Statement* first);
extern unsigned char CMach_GetFunctionResultClass(const FunctionType* type);
extern void CFunc_InsertFuncResultArgument(Object* object);
extern void RC1_InstantiateStoredStatement(PackedInstanceContext* context, Statement* statement);
extern void CFunc_FinishFunction(Object* object, Statement* first, Statement* body,
                                 unsigned char generate, unsigned char suppress);
extern ExceptionAction* cexcept_dobjstack;
extern Expression* CTemplTool_InstantiateExpr(PackedInstanceContext* context, Expression* expression);
extern unsigned char CTemplTool_IsValueDepExpr(const Expression* expression);
extern Expression* CExpr_ConvertToCondition(Expression* expression);
extern Expression* CExpr_ConvertToIntegral(Expression* expression);
extern void CExpr_CheckUnusedExpression(Expression* expression);
extern Expression* CFunc_ConvertResult(Expression* expression);
extern Statement* CFunc_InsertStatement(int kind, Statement* previous);
extern void CFunc_NewCaseLabel(SwitchInfo* info, ConstantInteger* low, ConstantInteger* high, Label* label);
extern void CFunc_TransMemInit(MemberInitializer* initializer);
extern void CFunc_CheckMemInit(ClassInstance* klass, MemberInitializer* list);
extern void RC1_InstantiateLocalDeclaration(PackedInstanceContext* context, Statement* statement,
                                           SourceLocation* saved, unsigned char* locked);
extern void RC1_InstantiateStaticDeclaration(PackedInstanceContext* context, Statement* statement,
                                            SourceLocation* saved, unsigned char* locked);
extern void RC1_InstantiateCleanups(PackedInstanceContext* context, ExceptionAction** cleanups);
extern NameSpace* cscope_root;
extern ObjectLink* locals;
extern Object* CParser_NewGlobalDataObject(const void* name);
extern void CFunc_NameLocalStaticDataObject(Object* object);
extern unsigned char CTTool_CanCreateTypeInstance(const void* type);
extern void CInit_AutoInitialization(Object* object, void* initializer, Statement* statement,
                                    unsigned char instantiate);
extern Statement* CFunc_DefineVLASizeObjects(VLABound* bounds, Statement* statement);
extern Statement* CFunc_DefineVLA(Object* object, Statement* statement);
extern void RC1_UpdateVLACleanups(Statement* statement, Object* object);
extern void* galloc(unsigned size);
extern Object* CClass_Destructor(const void* klass);
extern VLARegistry RC1_vla_registry;
extern const char RC1_CDecl_source_file[];
extern Object* vla_free_func;
extern unsigned char cexcept_hasdobjects;

enum {
    DataObject = 0,
    LocalDataObject = 1,
    FunctionObjectKind = 3,
    VirtualFunctionObject = 4,
    AliasObject = 6,
    FunctionTypeKind = 7,
    Referenced = 2,
    ObjectDefined = 4,
    FunctionDefined = 2,
    Method = 0x10,
    Constructor = 0x1000,
    AutoGenerated = 0x100,
    Inline = 0x10,
    InlineData = 0x10000,
    TemplateInstance = 0x400000,
    ClassTemplateInstance = 0x800,
    ClassNestedTemplate = 0x200,
    ClassLocal = 0x400,
    TypeTagObject = 2,
    AccessPublic = 0,
    ClassHasVirtualBases = 0x20,
    IndirectResultClass = 1,
    StaticStorageClass = 0x102,
    ImplicitWeak = 0x20000,
    ObjectGraphRoot = 8,
    TypeVoid = 0,
    TypeClass = 6,
    TypeArray = 0xd,
    IntegerConstant = 0x34,
    DependentExpression = 0x4c,
    ConstructorInitializers = 0x4f,
    ExpressionSource = 6,
    DependentLocalDeclaration = 0x17,
    DependentStaticDeclaration = 0x18,
    ST_NOP = 1,
    ST_LABEL,
    ST_GOTO,
    ST_EXPRESSION,
    ST_SWITCH,
    ST_IFGOTO,
    ST_IFNGOTO,
    ST_RETURN,
    ST_OVF,
    ST_EXIT,
    ST_ENTRY,
    ST_BEGINCATCH,
    ST_ENDCATCH,
    ST_ENDCATCHDTOR,
    ST_GOTOEXPR,
    ST_ASM,
    InvalidCaseConstant = 0x278c,
    VariableLengthArrayInitializer = 0x27be,
    InitializerSingleExpression = 0,
    InitializerExpressionList = 1,
    InitializerTree = 2,
    DestroyLocal = 1,
    DestroyLocalOffset = 3,
    DestroyLocalArray = 5,
    DeleteLocalPointer = 11,
    CatchBlock = 13,
    DestroyVLA = 21,
    DependentCleanup = 22,
};

static inline void suppress_member_debug_info(const SourceLocation* location) {
    if (RC1_nosyminline || !location || !location->file)
        RC1_filesyminfo = 0;
}

void CTempl_InstantiateMember(ClassInstance* instance, TemplateMember* member, const Object* declaration,
                              Object* point, Object* object, unsigned char explicit_instantiation) {
    ScopeSave saved_scope;
    int stream_state;
    ScopeSave initializer_scope;
    unsigned char saved_debug;
    SourceLocation saved_source;
    PackedInstanceContext initializer_context;
    PackedInstanceContext context;
    void* saved_instance;
    const void* parameters;
    NameSpace* scope;
    void* stack;
    void* initializer_stack;
    DeclInfo info;
    void* initialization;
    unsigned char saved_force_source;

    const TemplateInstanceLink* template_object = instance->template_object;
    if (!explicit_instantiation && !CIMan_NeedsTemplateInstance(object)) {
        object->qualifiers |= ImplicitWeak;
        return;
    }
    if (object->storage_class != StaticStorageClass) {
        if (explicit_instantiation)
            object->flags |= ObjectGraphRoot;
        object->qualifiers |= ImplicitWeak;
    }
    if (!point && (object->qualifiers & TemplateInstance))
        point = ((const TemplateObject*)object)->point_of_instantiation;

    if (member->packed_definition != 0)
        goto packed_definition;
    if (member->packed_initializer != 0)
        goto packed_initializer;
    if (point) {
        saved_instance = ctempl_curinstance;
        ctempl_curinstance = point;
    }
    stack = CTemplTool_PushInstance(0, object);
    parameters = member->parameters;
    if (!parameters)
        parameters = CTemplClass_GetMasterTemplate(CTemplTool_Template2TemplClass(template_object))->parameters;
    scope = CTemplTool_InsertTemplateArgumentNameSpace(parameters, instance, &saved_scope);
    cparser_fileoffset = member->location;
    cparser_decloffset = cparser_fileoffset;
    cfunc_funcdefnref = cparser_fileoffset;
    CPrep_StreamInsert(&member->stream, &stream_state);
    saved_force_source = gForceSourceLoc;
    gForceSourceLoc = 1;
    tk = lex();
    saved_debug = RC1_filesyminfo;
    suppress_member_debug_info(&cfunc_funcdefnref);

    memclrw(&info, sizeof(info));
    info.source_file = member->source_file;
    info.file = CPrep_BrowserCurrentFile();
    info.source_offset = member->source_offset;
    switch (object->datatype) {
    case FunctionObjectKind:
    case VirtualFunctionObject:
        if (declaration)
            CTemplTool_MergeArgNames(declaration->type, object->type);
        CFunc_ParseFuncDef(object, &info, instance, 0, 0, 0);
        break;
    case DataObject:
        CParser_UpdateObject(object, 0);
        CDecl_CompleteType(object->type);
        CInit_InitializeData(object);
        break;
    default:
        CError_Internal(RC1_CTemplateFuncInst_source_file, 1186);
        break;
    }
    CTemplTool_PopInstance(stack);
    if (point)
        ctempl_curinstance = saved_instance;
    CTemplTool_RemoveTemplateArgumentNameSpace(scope, instance, &saved_scope);
    CPrep_StreamRemove(&member->stream, &stream_state);
    RC1_filesyminfo = saved_debug;
    gForceSourceLoc = saved_force_source;
    return;

packed_definition:
    CDecl_CompleteType(instance);
    memclrw(&context, sizeof(context));
    context.instance = &instance->template_object;
    RC1_InstantiateStoredDefinition(&context, object, point, member->packed_definition, explicit_instantiation);
    return;

packed_initializer:
    CDecl_CompleteType(instance);
    initialization = CInline_GetInitializationCopy(member->packed_initializer, 0);
    if (initialization) {
        memclrw(&initializer_context, sizeof(initializer_context));
        initializer_context.instance = &instance->template_object;
        if (point) {
            saved_instance = ctempl_curinstance;
            ctempl_curinstance = point;
        }
        initializer_stack = CTemplTool_PushInstance(0, object);
        CError_LockErrorSourceRef(&member->location, &saved_source);
        CScope_SetClassScope(instance, &initializer_scope);
        RC1_InstantiateStoredInitializer(&initializer_context, initialization);
        CScope_RestoreScope(&initializer_scope);
        CError_UnlockErrorSourceRef(&saved_source);
        CTemplTool_PopInstance(initializer_stack);
        if (point)
            ctempl_curinstance = saved_instance;
    }
    CInit_GlobalInitialization(object, initialization, 0, 0);
    return;
}

unsigned char retro_GenerateMemberInstance(void* instance, Object* object,
                                           unsigned char explicit_instantiation) {
    if (!(object->qualifiers & TemplateInstance))
        return 0;
    const Object* parent = ((const TemplateObject*)object)->parent;
    if (!parent)
        CError_Internal(RC1_CTemplateFuncInst_source_file, 1217);
    if (((const TemplateObject*)object)->skip_instantiation)
        return 0;
    TemplateMember* member = CTemplTool_GetTemplateMember(parent);
    if (member) {
        if (parent->qualifiers & Inline)
            object->qualifiers |= Inline;
        CTempl_InstantiateMember(instance, member, parent, 0, object, explicit_instantiation);
        return 1;
    }
    if (explicit_instantiation)
        CError_Warning(0x27f9, object);
    return 0;
}

unsigned char retro_GenerateClassMembers(void* instance, unsigned char explicit_instantiation) {
    unsigned char generated = 0;
    if (!explicit_instantiation && ((const ClassInstance*)instance)->is_extern)
        return 0;
    ObjectIterator iterator;
    CScope_InitObjectIterator(&iterator, ((const ClassInstance*)instance)->scope);
    Object* object;
    while ((object = CScope_NextObjectIteratorObject(&iterator))) {
        if (object->type->kind == FunctionTypeKind && object->datatype != AliasObject) {
            if ((explicit_instantiation || (object->flags & Referenced)) &&
                !(object->type->flags & (FunctionDefined | AutoGenerated)) &&
                retro_GenerateMemberInstance(instance, object, explicit_instantiation) &&
                (object->type->flags & FunctionDefined))
                generated = 1;
        } else if (CParser_IsDataObject(object) && !(object->qualifiers & InlineData) &&
                   !(object->flags & ObjectDefined) &&
                   (explicit_instantiation || (object->flags & Referenced)) &&
                   retro_GenerateMemberInstance(instance, object, explicit_instantiation) &&
                   (object->flags & ObjectDefined)) {
            generated = 1;
        }
    }
    return generated;
}

unsigned char retro_GenerateFunctionInstance(void* raw_template, void* raw_instance,
                                             unsigned char explicit_instantiation) {
    FunctionTemplate* templ = raw_template;
    FunctionInstance* instance = raw_instance;
    if (instance->is_extern && !explicit_instantiation)
        return 0;
    while (!templ->packed_definition && !templ->stream.tokens &&
           templ->fallback_context && templ->fallback_object)
        templ = CTemplTool_Template2TemplFunc(templ->fallback_object);

    if (!explicit_instantiation && !CIMan_NeedsTemplateInstance(instance->object)) {
        /* CIMan can change the instance's object; use its current object. */
        instance->object->qualifiers |= ImplicitWeak;
        instance->is_instantiated = 1;
        return 0;
    }
    if (instance->object->storage_class != StaticStorageClass) {
        if (explicit_instantiation)
            instance->object->flags |= ObjectGraphRoot;
        instance->object->qualifiers |= ImplicitWeak;
    }
    if (templ->packed_definition) {
        PackedInstanceContext context;
        memclrw(&context, sizeof(context));
        context.instance = instance;
        return RC1_InstantiateStoredDefinition(&context, instance->object, instance->point_of_instantiation,
                                               templ->packed_definition, explicit_instantiation);
    }
    if (!templ->stream.tokens) {
        if (explicit_instantiation) {
            CError_SetErrorSourceRef(&templ->location);
            CError_Error(0x27f9, instance->object);
        }
        return 0;
    }

    instance->is_instantiated = 1;
    int stream_state;
    CPrep_StreamInsert(&templ->stream, &stream_state);
    CParser_GetDeclSourceInfo(&templ->location);
    unsigned char saved_debug = RC1_filesyminfo;
    if (RC1_nosyminline || !cparser_fileoffset.file)
        RC1_filesyminfo = 0;
    tk = lex();
    if (tk != '{' && tk != ':' && tk != 0x15e)
        CError_Internal(RC1_CTemplateFuncInst_source_file, 1002);
    DeclInfo declaration;
    memclrw(&declaration, sizeof(declaration));
    declaration.source_file = templ->source_file;
    declaration.file = CPrep_BrowserCurrentFile();
    declaration.source_offset = templ->start_offset;
    void* saved_instance = 0;
    if (instance->point_of_instantiation) {
        saved_instance = ctempl_curinstance;
        ctempl_curinstance = instance->point_of_instantiation;
    }
    /* RC1's helper takes two arguments and returns the pop token. */
    void* stack = CTemplTool_PushInstance(0, instance->object);
    CTemplTool_MergeArgNames(templ->function->type, instance->object->type);
    NameSpace* scope = CTemplTool_SetupTemplateArgumentNameSpace(templ->parameters, instance->arguments, 0);
    scope->parent = instance->object->scope;
    instance->object->scope = scope;
    CTemplTool_SetupOuterTemplateArgumentNameSpace(scope);
    CFunc_ParseFuncDef(instance->object, &declaration, 0, 0, 0, scope);
    CTemplTool_RemoveOuterTemplateArgumentNameSpace(scope);
    instance->object->scope = scope->parent;
    CTemplTool_PopInstance(stack);
    if (instance->point_of_instantiation)
        ctempl_curinstance = saved_instance;
    CPrep_StreamRemove(&templ->stream, &stream_state);
    RC1_filesyminfo = saved_debug;
    CBrowse_NewFunction(instance->object, declaration.file, declaration.source_file,
                        declaration.source_offset, templ->end_offset);
    return 1;
}

unsigned char retro_InstantiateTemplateDefinitions(void) {
    unsigned char generated = 0;
    for (ClassTemplate* templ = ctempl_ctemplates; templ; templ = templ->next) {
        for (ClassInstance* instance = templ->instances; instance; instance = instance->next) {
            if ((instance->flags & ClassTemplateInstance) && !instance->is_specialized) {
                if (retro_GenClassInstance(instance, 0))
                    generated = 1;
                CMid_FreeLocalHeap();
            }
        }
        for (PartialSpecialization* partial = templ->partials; partial; partial = partial->next) {
            for (ClassInstance* instance = partial->templ->instances; instance; instance = instance->next) {
                if ((instance->flags & ClassTemplateInstance) && !instance->is_specialized) {
                    if (retro_GenClassInstance(instance, 0))
                        generated = 1;
                    CMid_FreeLocalHeap();
                }
            }
        }
    }
    for (FunctionTemplate* templ = ctempl_ftemplates; templ; templ = templ->next) {
        for (FunctionInstance* instance = templ->instances; instance; instance = instance->next) {
            if (instance->is_instantiated || instance->is_specialized)
                continue;
            Object* object = instance->object;
            if ((object->flags & Referenced) && !(object->type->flags & FunctionDefined)) {
                instance->is_instantiated = 1;
                if (retro_GenerateFunctionInstance(templ, instance, 0))
                    generated = 1;
                CMid_FreeLocalHeap();
            }
            Object* secondary = ((const FunctionObject*)object)->secondary_function;
            if (secondary && (secondary->flags & Referenced) && !(secondary->type->flags & FunctionDefined)) {
                instance->is_instantiated = 1;
                if (retro_GenerateFunctionInstance(templ, instance, 0))
                    generated = 1;
                CMid_FreeLocalHeap();
            }
        }
    }
    return generated;
}

ClassTemplate* CTemplTool_Template2TemplClass(const TemplateInstanceLink* record) {
    if (!record)
        return 0;
    if (!record->is_class)
        CError_Internal(RC1_CTemplateTools_source_file, 76);
    return (ClassTemplate*)((const char*)record - offsetof(ClassTemplate, parent_template));
}

ClassInstance* CTemplTool_Class2TemplClassInst(ClassInstance* instance) {
    if (!instance)
        return 0;
    if (!(instance->flags & ClassTemplateInstance))
        CError_Internal(RC1_CTemplateTools_source_file, 141);
    return instance;
}

ClassInstance* CTemplTool_TemplInst2TemplClassInst(const TemplateInstanceLink* record) {
    if (!record)
        return 0;
    if (!record->parent->is_class)
        CError_Internal(RC1_CTemplateTools_source_file, 88);
    return (ClassInstance*)((const char*)record - offsetof(ClassInstance, template_object));
}

ClassTemplate* CTemplTool_InstClass2Template(ClassInstance* instance) {
    return CTemplTool_Template2TemplClass(CTemplTool_Class2TemplClassInst(instance)->template_object);
}

void* CTemplTool_FixTypedefObject(PackedInstanceContext* context, void* object) {
    TypedefObject* entry = (TypedefObject*)object;
    NameSpace* scope;
    (void)context;
    if (!entry)
        return 0;
    for (scope = entry->scope; scope; scope = scope->parent) {
        if (scope->is_template)
            return 0;
    }
    if (CTemplTool_IsTypeDepType(entry->type))
        return 0;
    return entry;
}

ClassTemplate* CTemplClass_GetMasterTemplate(ClassTemplate* templ) {
    if (templ->parent_instance && (templ->flags & ClassNestedTemplate))
        return CTemplTool_Template2TemplClass(templ->parent_template);
    return templ;
}

ClassInstance* CTemplClass_NewInstance(ClassTemplate* templ, const void* arguments, const void* original_arguments) {
    ClassTemplate* master = CTemplClass_GetMasterTemplate(templ);
    if (master->partial_owner)
        CError_Internal(RC1_CTemplateClass_source_file, 310);

    ClassInstance* instance = galloc(sizeof(*instance));
    memclrw(instance, sizeof(*instance));
    instance->next = master->instances;
    master->instances = instance;

    const HashNameNode* scope_name = master->name;
    if (master->parameters)
        scope_name = CMangler_TemplateInstanceName(master->name, original_arguments ? original_arguments : arguments);
    instance->arguments = arguments;
    instance->original_arguments = original_arguments;
    instance->parent_instance = templ->parent_instance;

    NameSpace* scope = CScope_NewListNameSpace(scope_name, 1);
    scope->theclass = instance;
    if (master->parent_template && instance->parent_instance) {
        scope->parent = CTemplTool_TemplInst2TemplClassInst(instance->parent_instance)->scope;
    } else {
        NameSpace* parent = master->scope->parent;
        while (parent->is_template)
            parent = parent->parent;
        scope->parent = parent;
    }

    instance->kind = TypeClass;
    instance->flags = ClassTemplateInstance;
    instance->scope = scope;
    instance->name = master->name;
    instance->mode = master->mode;
    instance->align = master->align;
    instance->template_object = (const TemplateInstanceLink*)&templ->parent_template;
    if (CClass_IsLocal(master)) {
        instance->name = CParser_AppendUniqueNameFile(instance->name->name);
        scope->name = instance->name;
        instance->flags |= ClassLocal;
    }

    ObjTypeTag* tag = galloc(sizeof(*tag));
    memclrw(tag, sizeof(*tag));
    tag->kind = TypeTagObject;
    tag->access = AccessPublic;
    tag->type = instance;
    CScope_AddObject(scope, master->name, tag);
    return instance;
}

ClassInstance* CTemplClass_GetInstance(ClassTemplate* templ, const void* arguments, const void* original_arguments) {
    ClassInstance* instance;
    ClassTemplate* master;

    if (!templ->parent_instance || !(templ->flags & ClassNestedTemplate))
        goto use_original_template;
    master = CTemplTool_Template2TemplClass(templ->parent_template);

lookup_instances:
    instance = master->instances;
    for (; instance; instance = instance->next) {
        const void* cached_arguments;
        if (original_arguments)
            CError_Internal(RC1_CTemplateClass_source_file, 386);
        cached_arguments = instance->original_arguments ? instance->original_arguments : instance->arguments;
        if (CTemplTool_EqualArgs(arguments, cached_arguments) && templ->parent_instance == instance->parent_instance)
            return instance;
    }
    return CTemplClass_NewInstance(templ, arguments, original_arguments);

use_original_template:
    master = templ;
    goto lookup_instances;
}

ClassInstance* CTemplTool_GetTemplClassInst(TemplateInstanceLink* link) {
    for (;; link = link->next) {
        if (!link)
            CError_Internal(RC1_CTemplateTools_source_file, 59);
        if (link->parent->is_class)
            return (ClassInstance*)((char*)link - offsetof(ClassInstance, template_object));
    }
}

static inline ClassTemplate* enclosing_class_template(TemplateInstanceLink* link) {
    for (;; link = link->parent) {
        if (!link)
            return 0;
        if (link->is_class)
            return (ClassTemplate*)((char*)link - offsetof(ClassTemplate, parent_template));
    }
}

ObjMemberVar* CTemplTool_InstantiateMemberVar(PackedInstanceContext* context, ObjMemberVar* member) {
    if (!context->instance)
        CError_Internal(RC1_CTemplateTools_source_file, 2947);
    if (!member->name)
        CError_Internal(RC1_CTemplateTools_source_file, 2948);
    if (member->has_path)
        return member;

    ClassTemplate* templ = enclosing_class_template(((TemplateInstanceLink*)context->instance)->parent);
    if (!templ)
        return member;

    ObjMemberVar* variable = CTemplClass_GetMasterTemplate(templ)->ivars;
    for (;; variable = variable->next) {
        if (!variable)
            CError_Internal(RC1_CTemplateTools_source_file, 2960);
        if (variable == member)
            break;
    }

    variable = CTemplTool_GetTemplClassInst(context->instance)->ivars;
    for (;; variable = variable->next) {
        if (!variable)
            CError_Internal(RC1_CTemplateTools_source_file, 2966);
        if (variable->name == member->name)
            return variable;
    }
}

InitializerExpression* CTemplTool_InstantiateExprList(PackedInstanceContext* context, InitializerExpression* list) {
    InitializerExpression* result = 0;
    InitializerExpression* last;
    for (; list; list = list->next) {
        if (result) {
            last->next = lalloc(sizeof(*last));
            last = last->next;
        } else {
            last = lalloc(sizeof(*last));
            result = last;
        }
        *last = *list;
        last->expression = CTemplTool_InstantiateExpr(context, last->expression);
    }
    return result;
}

MemberInitializer* RC1_InstantiateMemberInitializers(PackedInstanceContext* context, MemberInitializer* list) {
    if (!list)
        return 0;
    MemberInitializer* initializer = lalloc(sizeof(*initializer));
    *initializer = *list;
    initializer->next = RC1_InstantiateMemberInitializers(context, initializer->next);
    if (initializer->has_single_expression)
        initializer->arguments.expression = CTemplTool_InstantiateExpr(context, initializer->arguments.expression);
    else
        initializer->arguments.list = CTemplTool_InstantiateExprList(context, initializer->arguments.list);
    if (initializer->is_member) {
        initializer->target.member = CTemplTool_InstantiateMemberVar(context, initializer->target.member);
    } else {
        unsigned qualifiers = 0;
        initializer->target.base = CTemplTool_InstantiateType(context, initializer->target.base, &qualifiers);
    }
    CFunc_TransMemInit(initializer);
    return initializer;
}

void CError_UnlockErrorSourceRef(const SourceLocation* saved) {
    cerror_lockref = *saved;
}

void CError_LockErrorSourceRef(const SourceLocation* location, SourceLocation* saved) {
    *saved = cerror_lockref;
    /* Keep the empty-source return ahead of the update for the CodeWarrior layout. */
    if (location && location->file)
        goto update;
done:
    return;

update:
    cerror_lockref = *location;
    goto done;
}

VLAInfo* CDecl_RemoveVLAInfo(VLAInfo* head, const void* type) {
    VLAInfo* previous;
    VLAInfo* entry;
    if (head) {
        if (head->type == type)
            return head->next;
        for (previous = head; previous->next; previous = entry) {
            entry = previous->next;
            if (entry->type == type) {
                previous->next = entry->next;
                break;
            }
        }
    }
    return head;
}

VLAInfo* CDecl_FindVLAInfo(const void* type) {
    VLAInfo* entry;
    for (entry = RC1_vla_registry.head; ; entry = entry->next) {
        if (!entry)
            CError_Internal(RC1_CDecl_source_file, 90);
        if (entry->type == type)
            return entry;
    }
}

void CDecl_RegisterVLAInfo(VLAInfo* entry) {
    entry->next = RC1_vla_registry.head;
    RC1_vla_registry.head = entry;
}

void CDecl_DiscardVLAInfos(void) {
    RC1_vla_registry.head = 0;
}

void RC1_InstantiateInitializerTree(PackedInstanceContext* context, InitializerNode* node) {
    for (; node; node = node->next) {
        InitializerNode* child;
        if (node->expression)
            node->expression = CTemplTool_InstantiateExpr(context, node->expression);
        child = node->children;
        if (child)
            RC1_InstantiateInitializerTree(context, child);
    }
}

void RC1_InstantiateStoredInitializer(PackedInstanceContext* context, StoredInitializer* initializer) {
    if (initializer) {
        switch (initializer->kind) {
        case InitializerSingleExpression:
            initializer->data.expression = CTemplTool_InstantiateExpr(context, initializer->data.expression);
            break;
        case InitializerExpressionList:
            for (InitializerExpression* entry = initializer->data.list; entry; entry = entry->next)
                entry->expression = CTemplTool_InstantiateExpr(context, entry->expression);
            break;
        case InitializerTree:
            RC1_InstantiateInitializerTree(context, initializer->data.tree);
            break;
        default:
            CError_Internal(RC1_CTemplateFuncInst_source_file, 293);
            break;
        }
    }
}

void RC1_UpdateVLACleanups(Statement* statement, Object* object) {
    VLAInfo* info = CDecl_FindVLAInfo(object->type);
    if (!info->pointer)
        CError_Internal(RC1_CTemplateFuncInst_source_file, 311);
    Object* destructor = 0;
    FunctionType* element = object->type;
    while (element->kind == TypeArray)
        element = ((ArrayType*)element)->element;
    if (element->kind == TypeClass)
        destructor = CClass_Destructor(element);

    for (; statement; statement = statement->next) {
        for (ExceptionAction* action = statement->cleanups; action; action = action->previous) {
            if (action->kind != DependentCleanup || action->object != object)
                continue;
            if (destructor) {
                action->kind = DestroyVLA;
                action->object = info->pointer;
                action->operation.routine = info->element_count;
                action->operand.destructor = destructor;
                action->element_size = element->size;
                ExceptionAction* release = lalloc(sizeof(*release));
                memclrw(release, sizeof(*release));
                release->kind = DeleteLocalPointer;
                release->previous = action->previous;
                release->object = info->pointer;
                release->operation.routine = vla_free_func;
                action->previous = release;
            } else {
                action->kind = DeleteLocalPointer;
                action->object = info->pointer;
                action->operation.routine = vla_free_func;
            }
        }
    }
}

unsigned char RC1_ResolveDependentCleanup(ExceptionAction* action, ExceptionAction** link) {
    Object* object;
    FunctionType* type;
    FunctionType* element;
    Object* destructor;
    int count;
    int i;
    ObjectLink* pending;

    if (!action->operation.needs_destructor)
        return 0;
    object = action->object;
    type = object->type;
    if (type->kind == TypeClass) {
        destructor = CClass_Destructor(type);
        if (!destructor)
            return 0;
        action->kind = DestroyLocal;
        action->object = object;
        action->operation.routine = destructor;
        cexcept_hasdobjects = 1;
        return 1;
    }
    if (type->kind != TypeArray)
        return 0;

    element = ((ArrayType*)type)->element;
    while (element->kind == TypeArray)
        element = ((ArrayType*)element)->element;
    if (element->kind != TypeClass)
        goto no_destructor;
    destructor = CClass_Destructor(element);
    if (!destructor)
        goto no_destructor;
    cexcept_hasdobjects = 1;
    if (!type->size || !element->size)
        CError_Internal(RC1_CTemplateFuncInst_source_file, 108);
    count = type->size / element->size;

    pending = RC1_pending_array_initializers;
    for (;;) {
        if (pending == 0) {
            action->kind = DestroyLocalArray;
            action->object = object;
            action->operation.routine = destructor;
            action->operand.count = type->size / element->size;
            action->element_size = element->size;
            return 1;
        }
        if (pending->object == object)
            break;
        pending = pending->next;
    }
    for (i = 0; i < count; ++i) {
        int offset;
        if (i > 0) {
            ExceptionAction* previous = lalloc(sizeof(*previous));
            *previous = *action;
            action->previous = previous;
        }
        offset = i * element->size;
        if (offset) {
            action->kind = DestroyLocalOffset;
            action->object = object;
            action->operation.routine = destructor;
            action->operand.offset = offset;
        } else {
            action->kind = DestroyLocal;
            action->object = object;
            action->operation.routine = destructor;
        }
    }
    *link = action;
    return 1;

no_destructor:
    return 0;
}

void RC1_InstantiateCleanups(PackedInstanceContext* context, ExceptionAction** link) {
    ExceptionAction* action;
    for (;;) {
        action = *link;
        if (!action)
            break;
        switch (action->kind) {
        case DestroyLocal:
        case 2:
        case DestroyLocalOffset:
        case 4:
        case DestroyLocalArray:
        case DeleteLocalPointer:
        case 14:
        case DestroyVLA:
            cexcept_hasdobjects = 1;
            break;
        case DependentCleanup:
            if (!RC1_ResolveDependentCleanup(action, link)) {
                *link = action->previous;
                continue;
            }
            break;
        case CatchBlock:
            if (action->catch_type != 0 && CTemplTool_IsTypeDepType(action->catch_type))
                action->catch_type = CTemplTool_InstantiateType(context, action->catch_type, &action->catch_qualifiers);
            cexcept_hasdobjects = 1;
            break;
        case 16:
        case 18:
            break;
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 12:
        case 15:
        case 17:
        case 19:
        case 20:
            CError_Internal(RC1_CTemplateFuncInst_source_file, 212);
            break;
        default:
            CError_Internal(RC1_CTemplateFuncInst_source_file, 221);
            break;
        }
        link = &action->previous;
    }
}

static int has_expression_source(const Expression* expression) {
    return expression->kind == DependentExpression && expression->data.dependent.kind == ExpressionSource;
}

static inline void check_packed_declaration(const Expression* expression) {
    if (expression->kind != DependentExpression ||
        (expression->data.dependent.kind != DependentLocalDeclaration &&
         expression->data.dependent.kind != DependentStaticDeclaration))
        CError_Internal(RC1_CTemplateFuncInst_source_file, 259);
}

static Expression* unpack_declaration(Statement* statement, SourceLocation* saved, unsigned char* locked) {
    Expression* expression = statement->expression;
    if (has_expression_source(expression)) {
        CError_LockErrorSourceRef(&expression->data.dependent.location, saved);
        expression = expression->data.dependent.expression;
        *locked = 1;
    }
    check_packed_declaration(expression);
    return expression;
}

void RC1_InstantiateStaticDeclaration(PackedInstanceContext* context, Statement* statement,
                                     SourceLocation* saved, unsigned char* locked) {
    Expression* expression;
    Object* object;
    Object* original;
    void* initializer;
    ObjectRemap* remap;

    expression = statement->expression;
    if (expression->kind == DependentExpression && expression->data.dependent.kind == ExpressionSource) {
        CError_LockErrorSourceRef(&expression->data.dependent.location, saved);
        expression = expression->data.dependent.expression;
        *locked = 1;
    }
    check_packed_declaration(expression);
    initializer = expression->data.declaration.initializer;
    original = expression->data.declaration.object;
    object = CParser_NewGlobalDataObject(0);
    object->scope = cscope_root;
    object->declaration_data = original->declaration_data;
    object->type = original->type;
    object->name = original->name;
    object->qualifiers = original->qualifiers;
    object->storage_class = original->storage_class;
    if (CTemplTool_IsTypeDepType(object->type)) {
        object->type = CTemplTool_InstantiateType(context, object->type, &object->qualifiers);
        CDecl_CompleteType(object->type);
    }
    CFunc_NameLocalStaticDataObject(object);
    CParser_UpdateObject(object, 0);
    remap = lalloc(sizeof(*remap));
    remap->original = original;
    remap->replacement = object;
    remap->local_index = ~0u;
    remap->next = context->remaps;
    context->remaps = remap;
    statement->kind = ST_NOP;
    RC1_InstantiateStoredInitializer(context, initializer);
    CInit_GlobalInitialization(object, initializer, statement, 1);
    CTTool_CanCreateTypeInstance(object->type);
}

void RC1_InstantiateLocalDeclaration(PackedInstanceContext* context, Statement* statement,
                                    SourceLocation* saved, unsigned char* locked) {
    Expression* expression = unpack_declaration(statement, saved, locked);
    void* initializer = expression->data.declaration.initializer;
    const LocalObject* original = (const LocalObject*)expression->data.declaration.object;
    if (original->object.datatype != LocalDataObject)
        CError_Internal(RC1_CTemplateFuncInst_source_file, 368);
    statement->kind = ST_NOP;
    ObjectLink* entry = locals;
    for (;; entry = entry->next) {
        if (!entry)
            CError_Internal(RC1_CTemplateFuncInst_source_file, 373);
        if (entry->object->datatype == LocalDataObject && ((const LocalObject*)entry->object)->index == original->index)
            break;
    }
    ObjectRemap* remap = lalloc(sizeof(*remap));
    remap->original = &original->object;
    remap->replacement = entry->object;
    remap->local_index = original->index;
    remap->next = context->remaps;
    context->remaps = remap;

    LocalObject* local = (LocalObject*)entry->object;
    Object* object = &local->object;
    local->typedef_object = CTemplTool_FixTypedefObject(context, local->typedef_object);
    if (CTemplTool_IsTypeDepType(object->type)) {
        context->capture_array_bounds = 1;
        context->array_bounds = 0;
        object->type = CTemplTool_InstantiateType(context, object->type, &object->qualifiers);
        context->capture_array_bounds = 0;
        if (context->array_bounds) {
            statement = CFunc_DefineVLASizeObjects(context->array_bounds, statement);
            if (object->type->kind == TypeArray && ((const ArrayType*)object->type)->variable_length) {
                if (initializer)
                    CError_Error(VariableLengthArrayInitializer);
                statement = CFunc_DefineVLA(object, statement);
                RC1_UpdateVLACleanups(statement, object);
                return;
            }
        }
        CDecl_CompleteType(object->type);
    }
    RC1_InstantiateStoredInitializer(context, initializer);
    if (initializer && object->type->kind == TypeArray) {
        ObjectLink* pending = lalloc(sizeof(*pending));
        pending->object = object;
        pending->next = RC1_pending_array_initializers;
        RC1_pending_array_initializers = pending;
    }
    CInit_AutoInitialization(object, initializer, statement, 1);
    CTTool_CanCreateTypeInstance(object->type);
    if (CParser_IsDataObject(object)) {
        LocalObject* stored = galloc(sizeof(*stored));
        remap->replacement = &stored->object;
        *stored = *local;
        ObjectLink** position = &locals;
        while (*position && (*position)->object != object)
            position = &(*position)->next;
        if (!*position)
            CError_Internal(RC1_CTemplateFuncInst_source_file, 450);
        *position = (*position)->next;
    }
}

void RC1_InstantiateStoredStatement(PackedInstanceContext* context, Statement* statement) {
    SourceLocation saved;
    unsigned char locked = 0;
    cexcept_dobjstack = statement->cleanups;
    switch (statement->kind) {
    case ST_NOP:
    case ST_LABEL:
    case ST_GOTO:
    case ST_ASM:
        break;
    case ST_BEGINCATCH:
    case ST_ENDCATCH:
    case ST_ENDCATCHDTOR:
    case ST_GOTOEXPR: {
        Expression* expression = statement->expression;
        if (expression->kind == DependentExpression && expression->data.dependent.kind == ExpressionSource) {
            CError_LockErrorSourceRef(&expression->data.dependent.location, &saved);
            expression = expression->data.dependent.expression;
            locked = 1;
        }
        statement->expression = CTemplTool_InstantiateExpr(context, expression);
        break;
    }
    case ST_EXPRESSION: {
        Expression* expression;
        Expression* original;
        unsigned char kind;
        original = statement->expression;
        kind = original->kind;
        expression = original;
        if (kind == DependentExpression && original->data.dependent.kind == ExpressionSource)
            expression = original->data.dependent.expression;
        if (expression->kind == DependentExpression) {
            unsigned char subtype = expression->data.dependent.kind;
            if (subtype == DependentLocalDeclaration) {
                RC1_InstantiateLocalDeclaration(context, statement, &saved, &locked);
                break;
            }
            if (subtype == DependentStaticDeclaration) {
                RC1_InstantiateStaticDeclaration(context, statement, &saved, &locked);
                break;
            }
        }
        if (expression->kind == ConstructorInitializers) {
            if (kind == DependentExpression) {
                CError_LockErrorSourceRef(&original->data.dependent.location, &saved);
                locked = 1;
            }
            if (!cscope_currentclass)
                CError_Internal(RC1_CTemplateFuncInst_source_file, 646);
            statement->expression = expression;
            expression->data.initializers = RC1_InstantiateMemberInitializers(context, expression->data.initializers);
            CFunc_CheckMemInit(cscope_currentclass, expression->data.initializers);
            break;
        }
        expression = original;
        if (kind == DependentExpression && original->data.dependent.kind == ExpressionSource) {
            CError_LockErrorSourceRef(&original->data.dependent.location, &saved);
            expression = original->data.dependent.expression;
            locked = 1;
        }
        statement->expression = CTemplTool_InstantiateExpr(context, expression);
        CExpr_CheckUnusedExpression(statement->expression);
        break;
    }
    case ST_IFGOTO:
    case ST_IFNGOTO: {
        Expression* expression = statement->expression;
        if (expression->kind == DependentExpression && expression->data.dependent.kind == ExpressionSource) {
            CError_LockErrorSourceRef(&expression->data.dependent.location, &saved);
            expression = expression->data.dependent.expression;
            locked = 1;
        }
        statement->expression = CTemplTool_InstantiateExpr(context, expression);
        statement->expression = CExpr_ConvertToCondition(statement->expression);
        break;
    }
    case ST_RETURN:
        if (statement->expression) {
            Expression* original = statement->expression;
            Expression* expression = original;
            if (original->kind == DependentExpression && original->data.dependent.kind == ExpressionSource) {
                CError_LockErrorSourceRef(&original->data.dependent.location, &saved);
                expression = original->data.dependent.expression;
                locked = 1;
            }
            original = CTemplTool_InstantiateExpr(context, expression);
            expression = original;
            if (CTemplTool_IsValueDepExpr(statement->expression))
                expression = CFunc_ConvertResult(original);
            statement->expression = expression;
            if (statement->expression->type->kind == TypeVoid) {
                statement->kind = ST_EXPRESSION;
                if (statement->cleanups)
                    RC1_InstantiateCleanups(context, &statement->cleanups);
                statement = CFunc_InsertStatement(ST_RETURN, statement);
                statement->expression = 0;
                if (locked)
                    CError_UnlockErrorSourceRef(&saved);
                return;
            }
        }
        break;
    case ST_SWITCH: {
        /* Keep the original location until the end of the statement. Later
         * source locks in a switch save into separate temporary locations. */
        PendingCase* label;
        if (CTemplTool_IsValueDepExpr(statement->expression)) {
            SourceLocation nested;
            Expression* expression = statement->expression;
            if (expression->kind == DependentExpression && expression->data.dependent.kind == ExpressionSource) {
                if (locked) {
                    CError_LockErrorSourceRef(&expression->data.dependent.location, &nested);
                    expression = expression->data.dependent.expression;
                } else {
                    CError_LockErrorSourceRef(&expression->data.dependent.location, &saved);
                    expression = expression->data.dependent.expression;
                    locked = 1;
                }
            }
            statement->expression = CTemplTool_InstantiateExpr(context, expression);
            statement->expression = CExpr_ConvertToIntegral(statement->expression);
            statement->target.switch_info->type = statement->expression->type;
        }
        for (label = statement->target.switch_info->pending; label; label = label->next) {
            SourceLocation nested_first;
            Expression* first = label->first;
            if (first->kind == DependentExpression && first->data.dependent.kind == ExpressionSource) {
                if (locked) {
                    CError_LockErrorSourceRef(&first->data.dependent.location, &nested_first);
                    first = first->data.dependent.expression;
                } else {
                    CError_LockErrorSourceRef(&first->data.dependent.location, &saved);
                    first = first->data.dependent.expression;
                    locked = 1;
                }
            }
            label->first = CTemplTool_InstantiateExpr(context, first);
            first = label->first;
            if (first->kind == IntegerConstant) {
                if (label->last) {
                    SourceLocation nested_range;
                    Expression* range = first;
                    if (first->kind == DependentExpression && first->data.dependent.kind == ExpressionSource) {
                        if (locked) {
                            CError_LockErrorSourceRef(&first->data.dependent.location, &nested_range);
                            range = first->data.dependent.expression;
                        } else {
                            CError_LockErrorSourceRef(&first->data.dependent.location, &saved);
                            range = first->data.dependent.expression;
                            locked = 1;
                        }
                    }
                    /* RC1 instantiates the low endpoint again before checking the high endpoint. */
                    label->first = CTemplTool_InstantiateExpr(context, range);
                    if (label->last->kind == IntegerConstant)
                        CFunc_NewCaseLabel(statement->target.switch_info, &label->first->data.integer,
                                           &label->last->data.integer, label->label);
                    else
                        CError_Error(InvalidCaseConstant);
                } else {
                    CFunc_NewCaseLabel(statement->target.switch_info, &first->data.integer, 0, label->label);
                }
            } else {
                CError_Error(InvalidCaseConstant);
            }
        }
        statement->target.switch_info->pending = 0;
        break;
    }
    default:
        CError_Internal(RC1_CTemplateFuncInst_source_file, 721);
        break;
    }
    if (statement->cleanups != 0)
        RC1_InstantiateCleanups(context, &statement->cleanups);
    if (locked)
        CError_UnlockErrorSourceRef(&saved);
}

unsigned char RC1_InstantiateStoredDefinition(PackedInstanceContext* context, Object* object,
                                              void* point, const void* definition,
                                              unsigned char explicit_instantiation) {
    ObjectLink* argument;
    Statement* statement;
    Object* hidden_argument;
    ObjectLink* link;
    void* typedef_object;
    NameSpace* saved_scope;
    void* stack;
    void* saved_instance;
    unsigned char saved_side_effect;
    unsigned char saved_side_effect_as_error;
    Statement first;
    unsigned class_flags;
    FunctionType* signature;

    RC1_pending_array_initializers = 0;
    CParser_UpdateObject(object, 0);
    if (object->storage_class != StaticStorageClass) {
        if (explicit_instantiation)
            object->flags |= ObjectGraphRoot;
        object->qualifiers |= ImplicitWeak;
    }

    if (point) {
        saved_instance = ctempl_curinstance;
        ctempl_curinstance = point;
    }
    stack = CTemplTool_PushInstance(0, object);
    saved_side_effect = RC1_warn_no_side_effect.enabled;
    saved_side_effect_as_error = RC1_warn_no_side_effect.as_error;
    RC1_warn_no_side_effect.enabled = 0;
    RC1_warn_no_side_effect.as_error = 0;
    if (!context->instance)
        CError_Internal(RC1_CTemplateFuncInst_source_file, 817);
    /* Function instances and the embedded class instantiation record share this byte. */
    ((unsigned char*)context->instance)[offsetof(FunctionInstance, is_instantiated)] = 1;

    saved_scope = cscope_current;
    cscope_current = object->scope;
    if (!cscope_current)
        CError_Internal(RC1_CTemplateFuncInst_source_file, 822);
    CFunc_FuncGenSetup(0, &first, object, 0);
    cscope_currentfunc = object;
    cscope_currentclass = 0;
    cscope_is_member_func = 0;
    signature = object->type;
    if (signature->kind != FunctionTypeKind)
        CError_Internal(RC1_CTemplateFuncInst_source_file, 827);
    if (object->type->flags & Method) {
        cscope_currentclass = object->type->owner;
        cscope_is_member_func = !object->type->is_static;
    }
    CInline_UnpackIFunctionData(object, definition, &first, 1);

    if ((object->type->flags & Constructor) && cscope_currentclass != 0) {
        class_flags = cscope_currentclass->flags;
        if ((class_flags & ClassHasVirtualBases) && (class_flags & ClassTemplateInstance) &&
            !(CTemplTool_InstClass2Template(cscope_currentclass)->flags & ClassHasVirtualBases)) {
            if (!arguments)
                CError_Internal(RC1_CTemplateFuncInst_source_file, 856);
            hidden_argument = CParser_NewLocalDataObject(0, 0);
            hidden_argument->name = no_name_node;
            hidden_argument->type = (FunctionType*)&stsignedshort;
            CFunc_SetupLocalVarInfo(hidden_argument);
            link = lalloc(sizeof(*link));
            link->next = arguments->next;
            link->object = hidden_argument;
            arguments->next = link;
        }
    }

    for (argument = arguments; argument; argument = argument->next) {
        if (argument->object->datatype != LocalDataObject)
            CError_Internal(RC1_CTemplateFuncInst_source_file, 739);
        typedef_object = CTemplTool_FixTypedefObject(
            context, ((LocalObject*)argument->object)->typedef_object);
        ((LocalObject*)argument->object)->typedef_object = typedef_object;
        if (CTemplTool_IsTypeDepType(argument->object->type)) {
            SourceLocation saved;
            Object* variable;
            void* type;
            CError_LockErrorSourceRef(&((LocalObject*)argument->object)->location, &saved);
            variable = argument->object;
            type = CTemplTool_InstantiateType(context, variable->type, &variable->qualifiers);
            argument->object->type = type;
            CError_UnlockErrorSourceRef(&saved);
            CFunc_ReferenceArgumentRewrite(argument->object, &first);
        }
    }

    CDecl_CompleteType(object->type->result);
    if (CMach_GetFunctionResultClass(object->type) == IndirectResultClass) {
        for (argument = arguments; ; argument = argument->next) {
            if (!argument) {
                CFunc_InsertFuncResultArgument(object);
                break;
            }
            if (argument->object->name == temp_argument_name)
                break;
        }
    }
    for (statement = &first; statement; statement = statement->next)
        RC1_InstantiateStoredStatement(context, statement);
    if (!fatalerrors)
        CFunc_FinishFunction(object, &first, &first, 0, 0);

    CTemplTool_PopInstance(stack);
    if (point)
        ctempl_curinstance = saved_instance;
    cscope_current = saved_scope;
    cscope_currentfunc = 0;
    cscope_currentclass = 0;
    RC1_warn_no_side_effect.enabled = saved_side_effect;
    RC1_warn_no_side_effect.as_error = saved_side_effect_as_error;
    return 1;
}
