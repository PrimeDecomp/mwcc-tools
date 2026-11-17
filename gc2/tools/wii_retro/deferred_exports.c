#include <stddef.h>

typedef struct Object Object;
typedef struct NameSpace NameSpace;
typedef struct CallGraphNode CallGraphNode;
typedef struct GraphEdge GraphEdge;

/* RC1's CMid_DefineFunction and CMid_ObjectAddrRef prepend to this list.
 * Its deferred-function drain also takes entries from the head.
 */
typedef struct DeferredExport {
    struct DeferredExport* next;
    const Object* object;
} DeferredExport;

/* OLinkList in CClass_ConstructVTable. */
typedef struct VTableRef {
    const struct VTableRef* next;
    const Object* object;
    int offset;
    int addend;
} VTableRef;

extern DeferredExport* RC1_deferred_exports;
extern unsigned char retro_GenerateClassMembers(void* instance, unsigned char explicit_instantiation);
extern void CInit_DeclareData(void* object, const void* data, const VTableRef* refs, unsigned size);

static int has_boundary(const DeferredExport* entry, const DeferredExport* boundary) {
    while (entry && entry != boundary)
        entry = entry->next;
    return entry == boundary;
}

/* Called by CTempl_Instantiate for each ordinary or partially specialized
 * class instance. Retain the existing order between classes and emit each
 * class's new exports in the order its members were instantiated.
 */
unsigned char retro_GenClassInstance(void* instance, unsigned char explicit_instantiation) {
    DeferredExport* previous = RC1_deferred_exports;
    unsigned char generated = retro_GenerateClassMembers(instance, explicit_instantiation);

    if (!has_boundary(RC1_deferred_exports, previous))
        return generated;

    DeferredExport* ordered = previous;
    DeferredExport* entry = RC1_deferred_exports;
    while (entry != previous) {
        DeferredExport* next = entry->next;
        entry->next = ordered;
        ordered = entry;
        entry = next;
    }
    RC1_deferred_exports = ordered;
    return generated;
}

static int highest_vtable_slot(const VTableRef* refs, const Object* object) {
    int offset = -1;
    for (const VTableRef* ref = refs; ref; ref = ref->next) {
        if (ref->object == object && ref->offset > offset)
            offset = ref->offset;
    }
    return offset;
}

/* The vtable's relocations are collected in namespace traversal order.
 * Keep the data emission intact and use descending slot order for the new
 * weak bodies, including when an override appears in multiple slots.
 */
void retro_DeclareVTable(void* object, const void* data, const VTableRef* refs, unsigned size) {
    DeferredExport* previous = RC1_deferred_exports;
    CInit_DeclareData(object, data, refs, size);
    DeferredExport* entry = RC1_deferred_exports;
    if (!entry || entry == previous || entry->next == previous)
        return;

    /* Only reorder a segment whose exports can all be tied to table slots. */
    for (; entry != previous; entry = entry->next) {
        if (!entry || highest_vtable_slot(refs, entry->object) < 0)
            return;
    }

    DeferredExport* ordered = previous;
    entry = RC1_deferred_exports;
    while (entry != previous) {
        DeferredExport* next = entry->next;
        int offset = highest_vtable_slot(refs, entry->object);
        DeferredExport** insert = &ordered;
        while (*insert != previous && highest_vtable_slot(refs, (*insert)->object) >= offset)
            insert = &(*insert)->next;
        entry->next = *insert;
        *insert = entry;
        entry = next;
    }
    RC1_deferred_exports = ordered;
}

/* The structures below use only the fields needed by these call sites. */
#pragma pack(push, 2)
typedef struct ReferenceType {
    unsigned char kind;
    unsigned char unused[5];
    const void* target;
    unsigned qualifiers;
} ReferenceType;

typedef struct FunctionType {
    unsigned char kind;
    unsigned char unused[0xd];
    const ReferenceType* result;
    unsigned char unused_after_result[8];
    unsigned flags;
    unsigned char unused_after_flags[4];
    const void* owner;
    unsigned char unused_after_owner[8];
    unsigned char is_static;
} FunctionType;

typedef struct ClassTemplateInstance {
    unsigned char unused_before_flags[0x22];
    unsigned flags;
    unsigned char unused_before_template[0xe];
    const void* template_info;
} ClassTemplateInstance;

struct NameSpace {
    NameSpace* parent;
    unsigned char unused[8];
    const ClassTemplateInstance* theclass;
    unsigned char unused_after_class[0x12];
    unsigned char is_unnamed;
};

typedef struct HashName {
    unsigned char unused[0xa];
    char name[1];
} HashName;

struct Object {
    unsigned char unused_before_datatype[2];
    unsigned char datatype;
    unsigned char unused[5];
    const NameSpace* scope;
    const HashName* name;
    FunctionType* type;
    unsigned qualifiers;
    unsigned short storage_class;
    unsigned short flags;
    CallGraphNode* graph_node;
};

typedef struct ObjectList {
    struct ObjectList* next;
    Object* object;
} ObjectList;

typedef struct SourceLocation {
    unsigned words[3];
} SourceLocation;

typedef struct FunctionData {
    struct Statement* statements;
    Object* object;
    unsigned char old_style_parameters;
    unsigned char call_on_module_bind;
    ObjectList* arguments;
    ObjectList* locals;
    SourceLocation source_location;
    unsigned char unused[0x18];
} FunctionData;

typedef struct ENode {
    unsigned char kind;
    unsigned char unused[0xf];
    Object* object;
} ENode;

typedef struct InitExpression {
    struct InitExpression* next;
    ENode* expression;
    Object* object;
    SourceLocation location;
} InitExpression;

/* RC1's CExcept_CheckStackRefs dispatches on the byte at offset 0x1c. */
typedef struct ExceptionAction {
    const struct ExceptionAction* previous;
    const Object* object_pointer;
    Object* routine;
    union {
        Object* other_routine;
        int member_offset;
    } operand;
    unsigned char unused[0xc];
    unsigned char kind;
} ExceptionAction;

typedef struct CLabel CLabel;

typedef struct Statement {
    const struct Statement* next;
    unsigned char kind;
    unsigned char unused[5];
    ENode* expression;
    CLabel* label;
    const ExceptionAction* cleanups;
    unsigned char unused_after_cleanups[0xc];
} Statement;

struct CLabel {
    CLabel* next;
    Statement* statement;
};

typedef struct BackEndOptions {
    unsigned char bytes[0xce];
} BackEndOptions;

typedef struct ScopeSave {
    void* current;
    void* currentclass;
    const Object* currentfunc;
    unsigned char is_member_func;
} ScopeSave;

typedef struct AsmRelocation {
    const struct AsmRelocation* next;
    unsigned offset;
    unsigned char is_label;
} AsmRelocation;

typedef struct PackedAsmStatement {
    const unsigned char* code;
    unsigned size;
    const AsmRelocation* relocations;
} PackedAsmStatement;

struct GraphEdge {
    GraphEdge* next;
    CallGraphNode* node;
    unsigned reference_count;
    unsigned short flags;
};
#pragma pack(pop)

struct CallGraphNode {
    struct CallGraphNode* next;
    struct CallGraphNode* previous;
    const Object* object;
    unsigned char unused[4];
    GraphEdge* uses;
    GraphEdge* used_by;
    unsigned index;
    unsigned char kind;
    unsigned char is_initializer;
    unsigned char marked;
    unsigned char flagged;
};

typedef struct PendingDummyFunction {
    struct PendingDummyFunction* next;
    Object* function;
    Object* constructor;
    ENode* expression;
} PendingDummyFunction;

typedef struct CompilerParameters {
    unsigned char unused[0x258];
    unsigned char precompile;
} CompilerParameters;

#pragma pack(push, 2)
typedef struct DeferredFunctionInstance {
    void* templ;
    unsigned char unused[9];
    unsigned char is_specialized;
} DeferredFunctionInstance;

typedef struct PackedReference {
    Object* object;
    unsigned char kind;
    unsigned char flags;
    unsigned short reference_count;
} PackedReference;

typedef struct PackedInlineCode {
    unsigned char unused[0x14];
    unsigned reference_count;
    unsigned char unused_after_count[2];
    PackedReference references[];
} PackedInlineCode;

typedef struct InlineBody {
    const PackedInlineCode* code;
    unsigned char unused[8];
    BackEndOptions options;
    unsigned char unused_after_options[4];
} InlineBody;

typedef struct FunctionObject {
    Object object;
    unsigned char unused_before_body[0x20];
    const InlineBody* inline_body;
    unsigned char unused_after_body[4];
    DeferredFunctionInstance* instance;
    unsigned char unused_after_instance[0x14];
    const Object* template_declaration;
    void* point_of_instantiation;
} FunctionObject;

typedef struct ObjectAlias {
    Object object;
    unsigned char unused[0x20];
    Object* target;
} ObjectAlias;

typedef struct TokenStream {
    unsigned words[2];
} TokenStream;

typedef struct InitData {
    SourceLocation location;
    const void* data;
    const VTableRef* references;
    unsigned size;
    unsigned char is_read_only;
} InitData;

typedef struct ThunkData {
    Object* target;
    int this_delta;
    int return_delta;
    int constructor_offset;
} ThunkData;

typedef struct TemplateMember {
    unsigned char unused[4];
    void* scope;
    SourceLocation location;
    TokenStream stream;
    const void* definition;
} TemplateMember;

typedef struct DeferredAction {
    struct DeferredAction* next;
    Object* object;
    union {
        struct {
            void* instance_scope;
            SourceLocation location;
            TokenStream stream;
            void* scope;
        } function;
        struct {
            void* point_of_instantiation;
            ClassTemplateInstance* instance;
            const TemplateMember* member;
            const Object* template_declaration;
        } member;
        struct {
            unsigned char unused[4];
            void* instance;
            void* member;
        } friend_function;
    } payload;
    unsigned char kind;
} DeferredAction;
#pragma pack(pop)

_Static_assert(offsetof(ClassTemplateInstance, template_info) == 0x34, "RC1 class template instance");
_Static_assert(offsetof(CallGraphNode, is_initializer) == 0x1d, "RC1 initialization graph node flag");
_Static_assert(offsetof(CallGraphNode, kind) == 0x1c, "RC1 graph node kind");
_Static_assert(offsetof(CallGraphNode, index) == 0x18, "RC1 graph node index");
_Static_assert(sizeof(CallGraphNode) == 0x20, "RC1 graph node size");
_Static_assert(offsetof(CallGraphNode, uses) == 0x10, "RC1 graph uses");
_Static_assert(offsetof(CallGraphNode, used_by) == 0x14, "RC1 graph users");
_Static_assert(sizeof(GraphEdge) == 0xe, "RC1 graph edge size");
_Static_assert(offsetof(GraphEdge, reference_count) == 0x8, "RC1 graph edge count");
_Static_assert(offsetof(GraphEdge, flags) == 0xc, "RC1 graph edge flags");
_Static_assert(offsetof(Object, graph_node) == 0x1c, "RC1 object's graph node");
_Static_assert(offsetof(InitData, data) == 0xc, "RC1 graph data bytes");
_Static_assert(offsetof(InitData, references) == 0x10, "RC1 graph data references");
_Static_assert(offsetof(InitData, size) == 0x14, "RC1 graph data size");
_Static_assert(offsetof(InitData, is_read_only) == 0x18, "RC1 read-only graph data flag");
_Static_assert(sizeof(ThunkData) == 0x10, "RC1 virtual dispatch thunk data");
_Static_assert(sizeof(DeferredExport) == 0x8, "RC1 deferred export size");
_Static_assert(sizeof(PendingDummyFunction) == 0x10, "RC1 pending dummy function size");
_Static_assert(offsetof(PendingDummyFunction, expression) == 0xc, "RC1 pending dummy expression");
_Static_assert(offsetof(CompilerParameters, precompile) == 0x258, "RC1 precompile flag");
_Static_assert(offsetof(FunctionData, arguments) == 0xa, "RC1 function arguments");
_Static_assert(offsetof(FunctionData, locals) == 0xe, "RC1 function locals");
_Static_assert(offsetof(FunctionData, source_location) == 0x12, "RC1 function source location");
_Static_assert(sizeof(FunctionData) == 0x36, "RC1 code generation parameters");
_Static_assert(sizeof(ObjectList) == 0x8, "RC1 reference list entry");
_Static_assert(offsetof(Object, scope) == 0x8, "RC1 object namespace");
_Static_assert(offsetof(NameSpace, theclass) == 0xc, "RC1 namespace class");
_Static_assert(offsetof(NameSpace, is_unnamed) == 0x22, "RC1 unnamed namespace flag");
_Static_assert(offsetof(Object, storage_class) == 0x18, "RC1 object storage class");
_Static_assert(offsetof(InitExpression, location) == 0xc, "RC1 initializer location");
_Static_assert(sizeof(InitExpression) == 0x18, "RC1 initializer entry size");
_Static_assert(offsetof(ENode, object) == 0x10, "RC1 object reference expression");
_Static_assert(offsetof(Statement, expression) == 0xa, "RC1 statement expression");
_Static_assert(offsetof(Statement, cleanups) == 0x12, "RC1 statement exception actions");
_Static_assert(offsetof(Statement, label) == 0xe, "RC1 statement label");
_Static_assert(sizeof(Statement) == 0x22, "RC1 statement size");
_Static_assert(sizeof(ScopeSave) == 0xe, "RC1 scope save size");
_Static_assert(sizeof(AsmRelocation) == 0xa, "RC1 inline assembly relocation");
_Static_assert(sizeof(PackedAsmStatement) == 0xc, "RC1 packed inline assembly");
_Static_assert(offsetof(TemplateMember, definition) == 0x1c, "RC1 template member definition");
_Static_assert(sizeof(DeferredAction) == 0x26, "RC1 deferred action size");
_Static_assert(offsetof(DeferredAction, kind) == 0x24, "RC1 deferred action");
_Static_assert(offsetof(DeferredAction, payload.function.location) == 0xc, "RC1 streamed function location");
_Static_assert(offsetof(DeferredAction, payload.function.stream) == 0x18, "RC1 function token stream");
_Static_assert(offsetof(DeferredAction, payload.function.scope) == 0x20, "RC1 streamed function scope");
_Static_assert(offsetof(DeferredAction, payload.member.member) == 0x10, "RC1 deferred template member");
_Static_assert(offsetof(DeferredAction, payload.member.template_declaration) == 0x14, "RC1 template declaration");
_Static_assert(offsetof(DeferredAction, payload.friend_function.member) == 0x10, "RC1 deferred friend member");
_Static_assert(offsetof(FunctionObject, inline_body) == 0x40, "RC1 inline body");
_Static_assert(offsetof(FunctionObject, instance) == 0x48, "RC1 function template instance");
_Static_assert(offsetof(FunctionObject, template_declaration) == 0x60, "RC1 function template declaration");
_Static_assert(offsetof(FunctionObject, point_of_instantiation) == 0x64, "RC1 point of instantiation");
_Static_assert(sizeof(InlineBody) == 0xde, "RC1 packed function data");
_Static_assert(offsetof(InlineBody, options) == 0xc, "RC1 packed backend options");
_Static_assert(offsetof(ObjectAlias, target) == 0x40, "RC1 alias target");
_Static_assert(offsetof(PackedInlineCode, reference_count) == 0x14, "RC1 packed reference count");
_Static_assert(offsetof(PackedInlineCode, references) == 0x1a, "RC1 packed references");
_Static_assert(sizeof(PackedReference) == 0x8, "RC1 packed reference size");
_Static_assert(offsetof(PackedReference, flags) == 0x5, "RC1 packed reference flags");
_Static_assert(offsetof(PackedReference, reference_count) == 0x6, "RC1 packed reference count");
_Static_assert(offsetof(DeferredFunctionInstance, is_specialized) == 0xd, "RC1 specialized function template flag");
_Static_assert(offsetof(FunctionType, result) == 0xe, "RC1 function result type");
_Static_assert(offsetof(FunctionType, is_static) == 0x2e, "RC1 static method flag");

enum {
    DestroyLocalAction = 1,
    DestroyMemberAction = 7,
    DataObject = 0,
    AbsoluteObject = 2,
    OrdinaryFunction = 3,
    VirtualFunction = 4,
    InlineFunctionObject = 5,
    AliasObject = 6,
    FunctionTypeKind = 7,
    PointerTypeKind = 12,
    PackedObjectReference = 1,
    FunctionDefined = 0x2,
    Inline = 0x10,
    ReferenceQualifier = 0x20,
    Method = 0x10,
    ObjectDefined = 0x4,
    ObjectGraphRoot = 0x8,
    StaticStorageClass = 0x102,
    AutoGenerated = 0x100,
    Intrinsic = 0x200,
    AutoInline = 0x800,
    ClassIsTemplateInstance = 0x800,
    Constructor = 0x1000,
    Destructor = 0x2000,
    Referenced = 0x2,
    InlineData = 0x10000,
    ImplicitWeak = 0x20000,
    FunctionTemplate = 0x100000,
    TemplateInstance = 0x400000,
    ThunkQualifier = 0x800000,
    FunctionTemplateInstance = 0x800000,
    FileIPA = 1,
    OrderBuckets = 1024,
};

enum {
    StreamedFunctionAction = 0,
    ClassTemplateMemberAction = 1,
    FunctionTemplateAction = 2,
    ImplicitSpecialMemberAction = 3,
    FriendFunctionAction = 4,
};

enum {
    NopStatement = 1,
    LabelStatement = 2,
    GotoStatement = 3,
    ExpressionStatement = 4,
    SwitchStatement = 5,
    IfGotoStatement = 6,
    IfNotGotoStatement = 7,
    ReturnStatement = 8,
    BeginCatchStatement = 12,
    EndCatchStatement = 13,
    EndCatchDestructorStatement = 14,
    GotoExpressionStatement = 15,
    AsmStatement = 16,
    ObjectReferenceExpression = 0x3b,
};

enum {
    FunctionGraphNode = 1,
    ThunkGraphNode = 2,
    DataGraphNode = 3,
};

enum { DirectVirtualDestructor = 0x200 };

typedef enum CleanupOrder {
    OtherCleanup,
    DirectMemberCleanup,
    TemporaryCleanup,
    DeferredMemberCleanup,
    IndirectMemberCleanup,
} CleanupOrder;

typedef struct OrderRecord {
    struct OrderRecord* next;
    const void* key;
    const Object* first_referrer;
    const DeferredExport* member_cleanup_entry;
    const Object* member_cleanup_object;
    const DeferredExport* temporary_cleanup_entry;
    const CallGraphNode* serialized_node;
    const Object* serialized_referrer;
    const CallGraphNode* ipa_cleanup_node;
    struct OrderRecord* ipa_cleanup_owner;
    const DeferredAction* ipa_pending_member;
    CallGraphNode* last_ipa_member;
    CallGraphNode* last_ipa_temporary;
    int member_cleanup_offset;
    int ipa_member_offset;
    CallGraphNode* ipa_copy_source;
    const CallGraphNode* ipa_early_copy_node;
    const Object* ipa_ctor_referrer;
    unsigned ipa_ctor_reference;
    unsigned ipa_ctor_declaration;
    unsigned ipa_ctor_definition;
    unsigned definition;
    unsigned first_reference;
    unsigned helper_declaration;
    unsigned char ipa_cleanup_kind;
    unsigned char direct_member_cleanup;
    unsigned char delay_vtable;
    unsigned char from_template_action;
} OrderRecord;

extern unsigned char RC1_ipa_kind;
extern unsigned char cmid_in_ipa_mode;
extern unsigned char RC1_defer_codegen;
extern unsigned char anyerrors;
extern const CompilerParameters* cparamblkptr;
extern unsigned char RC1_process_deferred_after_errors;
extern unsigned char RC1_nosyminline;
extern unsigned char RC1_filesyminfo;
extern unsigned char RC1_generation_needs_statement_pass;
extern unsigned char RC1_allow_weak_graph_roots;
extern const char RC1_CMiddleLayer_source_file[];
extern const char RC1_CCallGraph_source_file[];
extern DeferredAction* RC1_deferred_actions;
extern PendingDummyFunction* RC1_pending_dummy_functions;
extern const Object* cscope_currentfunc;
extern NameSpace* cscope_current;
extern NameSpace* cscope_root;
extern FunctionType rt_func;
extern InitExpression* init_expressions;
extern SourceLocation sourceref;
extern SourceLocation cparser_fileoffset;
extern SourceLocation cparser_decloffset;
extern SourceLocation cfunc_funcdefnref;
extern SourceLocation cfunc_funcbodybeginref;
extern SourceLocation cfunc_funcbodyendref;
extern FunctionData cgparams;
extern ObjectList* arguments;
extern ObjectList* RC1_locals;
extern const HashName* this_name_node;
extern CallGraphNode* ccallgraph;
extern CallGraphNode* RC1_call_graph_tail;
extern unsigned RC1_call_graph_next_index;
extern void* galloc(unsigned size);
extern void* lalloc(unsigned size);
extern void* galloc2(unsigned size);
extern void memclrw(void* ptr, unsigned size);
extern void gfree2(void* ptr, unsigned size);
extern void CError_Error(int error, ...);
extern void CError_Internal(const char* source_file, int line);
extern void CMid_Setup(void);
extern void CMid_FreeLocalHeap(void);
extern void retro_ResetTemplateVTableActions(void);
extern unsigned char retro_DeclarationWorkItem(void);
extern void CMid_AddDefaultFunctionAction(Object* object);
extern void CExpr_IRTreeWalk(Statement* statements, void (*visitor)(ENode*));
extern void CExpr_ENodeTreeWalk(ENode* expression, void (*visitor)(ENode*));
extern void RC1_RewriteDefinitionExpression(ENode* expression);
extern void RC1_RewriteGenerationExpression(ENode* expression);
extern void RC1_ProcessGenerationStatements(Statement* statements);
extern void InlineAsm_NewPackAsmStatement(const Statement* statement, PackedAsmStatement* packed);
extern unsigned char CParser_IsDataObject(const Object* object);
extern unsigned char CInline_CheckInlineability(Object* object, Statement* statements,
                                                unsigned char* prevented);
extern void COpt_SimpleOptimizer(Object* object, Statement* statements);
extern void CInline_SeqStringRewrite(Statement* statements);
extern void CInline_PackIFunctionData(FunctionData* data, InlineBody* body);
extern void CInline_UnpackIFunctionData(Object* object, const InlineBody* body,
                                        Statement* first_statement, unsigned char expanding);
extern void CInline_Expand(FunctionData* data);
extern void CMach_GetBackEndOptions(BackEndOptions* options);
extern void CMach_SetBackEndOptions(const BackEndOptions* options);
extern void ObjGen_SetupSym(void);
extern void CScope_SetFunctionScope(Object* object, ScopeSave* save);
extern void CScope_RestoreScope(const ScopeSave* save);
extern void* CFunc_FuncGenSetup(void* scope, Statement* statement, Object* object,
                                unsigned char flag);
extern Statement* CFunc_AppendStatement(unsigned char kind);
extern CLabel* newlabel(void);
extern void CFunc_CodeCleanup(Statement* first_statement);
extern ENode* CInline_CopyExpression(ENode* expression, unsigned char mode);
extern void CPrep_MakeDebugSourceRef(const SourceLocation* location);
extern const HashName* CMangler_GetLinkName(Object* object);
extern Object* CABI_NewGuardVariable(Object* object);
extern ENode* CABI_AcquireGuardVariable(Object* guard, unsigned char flag);
extern ENode* CABI_ReleaseGuardVariable(Object* guard, unsigned char flag);
extern Object* CParser_NewFunctionObject(void* declaration);
extern const HashName* CParser_AppendUniqueName(const char* prefix);
FunctionData* retro_GetGraphIR(FunctionData* data, const CallGraphNode* node);
extern unsigned char CFunc_IsOldStyleFunction(Object* object);
extern void CParser_FuncDebugSetup(void* decl, void* type, void* scope);
extern void CIRStream_Free(const PackedInlineCode* code);
extern void CIRStream_UnpackCode(const PackedInlineCode* code, FunctionData* data,
                                 unsigned char mode);
extern const PackedInlineCode* retro_PackCode(const FunctionData* data);
extern const InitData* CMid_GetInitData(const Object* object);
extern const ThunkData* CMid_GetThunkData(const Object* object);
extern void CodeGen_GenVDispatchThunk(Object* object, Object* target, int this_delta,
                                      int return_delta, int constructor_offset);
extern void ObjGen_DeclareData(Object* object, const void* data,
                               const VTableRef* references, unsigned size);
extern void ObjGen_DeclareReadOnlyData(Object* object, const void* data,
                                       const VTableRef* references, unsigned size);
extern unsigned char CIMan_NeedsObjectInstance(Object* object);
void retro_GenerateFunction(FunctionData* data);
void retro_RecordIRDataReference(Object* object);
void retro_RecordIPASerializedReference(Object* routine);
extern void CInit_ExportConst(Object* object);
extern void CExcept_CheckStackRefs(const ExceptionAction* actions);
extern void CExcept_CallGraphExceptionOptimizer(void);
extern unsigned char RC1_IsCopyConstructor(const Object* object);
unsigned char retro_IsGraphNodeDefined(const CallGraphNode* node);
extern void CIRTrans_TransformC99Complex(Statement* statements);
extern void CPrep_SetSourceFile(const SourceLocation* location);
extern void CodeGen_Generator(FunctionData* data);
extern void CFunc_GenerateStreamedFunction(Object* object, void* scope, void* instance_scope,
                                           void* stream, const void* location, unsigned char flag);
extern void CFunc_GenerateDefaultFunc(Object* object);
extern void CFunc_GenerateDummyCtorFunc(Object* function, Object* constructor);
extern void CFunc_GenerateSingleExprFunc(Object* function, ENode* expression);
extern void CFunc_ParseFriendFunction(void* instance, void* member);
extern unsigned char CClass_Finalize(void);
extern unsigned char retro_InstantiateTemplateDefinitions(void);
extern void CTempl_InstantiateMember(ClassTemplateInstance* instance, const TemplateMember* member,
                                     const Object* template_declaration,
                                     void* point_of_instantiation, Object* object,
                                     unsigned char explicit_instantiation);
extern const TemplateMember* CTemplTool_GetTemplateMember(const Object* object);
extern ClassTemplateInstance* CTemplTool_Class2TemplClassInst(const void* type);
extern void* CTemplTool_Template2TemplFunc(void* templ);
extern void* CTemplTool_GetPointOfInstatiation(void);
extern unsigned char retro_GenerateFunctionInstance(void* templ, void* instance,
                                           unsigned char explicit_instantiation);

static OrderRecord* order_records[OrderBuckets];
static const Object* generating_function;
static const Object* ipa_defining_function;
static struct IPAConstructor* ipa_constructor;
static unsigned definition_count;
static unsigned reference_count;
static unsigned declaration_number;
static unsigned char defining_template_action;
static ObjectList* definition_references;

static int is_constructor(const Object* object);
static void define_ipa_constructor(FunctionData* data);

static OrderRecord* find_order(const void* key, int create) {
    unsigned address = (unsigned)key;
    OrderRecord** slot = &order_records[((address >> 2) ^ (address >> 16)) % OrderBuckets];
    for (OrderRecord* record = *slot; record; record = record->next) {
        if (record->key == key)
            return record;
    }
    if (!create)
        return 0;
    OrderRecord* record = galloc(sizeof(*record));
    record->key = key;
    record->first_referrer = 0;
    record->member_cleanup_entry = 0;
    record->member_cleanup_object = 0;
    record->temporary_cleanup_entry = 0;
    record->serialized_node = 0;
    record->serialized_referrer = 0;
    record->ipa_cleanup_node = 0;
    record->ipa_cleanup_owner = 0;
    record->ipa_pending_member = 0;
    record->last_ipa_member = 0;
    record->last_ipa_temporary = 0;
    record->member_cleanup_offset = 0;
    record->ipa_member_offset = 0;
    record->ipa_copy_source = 0;
    record->ipa_early_copy_node = 0;
    record->ipa_ctor_referrer = 0;
    record->ipa_ctor_reference = 0;
    record->ipa_ctor_declaration = 0;
    record->ipa_ctor_definition = 0;
    record->definition = 0;
    record->first_reference = 0;
    record->helper_declaration = 0;
    record->ipa_cleanup_kind = OtherCleanup;
    record->direct_member_cleanup = 0;
    record->delay_vtable = 0;
    record->from_template_action = 0;
    record->next = *slot;
    *slot = record;
    return record;
}

void retro_MidSetup(void) {
    /* The compiler's global heap owns the records for each translation unit. */
    for (unsigned i = 0; i < OrderBuckets; ++i)
        order_records[i] = 0;
    generating_function = 0;
    ipa_defining_function = 0;
    ipa_constructor = 0;
    definition_count = 0;
    reference_count = 0;
    declaration_number = 1;
    defining_template_action = 0;
    retro_ResetTemplateVTableActions();
    CMid_Setup();
}

void retro_BeginTemplateDeclaration(void) {
    ++declaration_number;
}

const InlineBody* retro_GetFuncData(const Object* object) {
    if (object->datatype == InlineFunctionObject)
        return 0;
    if (object->datatype != OrdinaryFunction && object->datatype != VirtualFunction)
        CError_Internal(RC1_CMiddleLayer_source_file, 293);
    if (object->qualifiers & ThunkQualifier)
        CError_Internal(RC1_CMiddleLayer_source_file, 294);
    if (object->type->kind != FunctionTypeKind)
        CError_Internal(RC1_CMiddleLayer_source_file, 295);
    if (object->type->flags & FunctionTemplate)
        CError_Internal(RC1_CMiddleLayer_source_file, 296);
    return object->type->flags & Intrinsic ? 0 : ((const FunctionObject*)object)->inline_body;
}

static void check_stored_function(const Object* object, int source_line) {
    if (object->datatype != OrdinaryFunction && object->datatype != VirtualFunction)
        CError_Internal(RC1_CMiddleLayer_source_file, source_line);
    if (object->qualifiers & ThunkQualifier)
        CError_Internal(RC1_CMiddleLayer_source_file, source_line + 1);
    if (object->type->kind != FunctionTypeKind)
        CError_Internal(RC1_CMiddleLayer_source_file, source_line + 2);
    if (object->type->flags & FunctionTemplate)
        CError_Internal(RC1_CMiddleLayer_source_file, source_line + 3);
    if (object->type->flags & Intrinsic)
        CError_Internal(RC1_CMiddleLayer_source_file, source_line + 4);
}

void retro_SetFuncData(Object* object, const InlineBody* body) {
    check_stored_function(object, 311);
    FunctionObject* function = (FunctionObject*)object;
    InlineBody* stored = (InlineBody*)function->inline_body;
    if (!stored) {
        stored = galloc2(sizeof(*stored));
        function->inline_body = stored;
    } else if (stored->code != body->code) {
        CIRStream_Free(stored->code);
    }
    *stored = *body;
}

void retro_FreeFuncData(Object* object) {
    check_stored_function(object, 338);
    FunctionObject* function = (FunctionObject*)object;
    const InlineBody* body = function->inline_body;
    if (body) {
        CIRStream_Free(body->code);
        gfree2((void*)body, sizeof(*body));
        function->inline_body = 0;
    }
}

CallGraphNode* retro_NewGraphNode(Object* object) {
    if (object->graph_node)
        CError_Internal(RC1_CCallGraph_source_file, 345);
    unsigned char kind = 0;
    switch (object->datatype) {
        case OrdinaryFunction:
        case VirtualFunction:
            kind = object->qualifiers & ThunkQualifier ? ThunkGraphNode : FunctionGraphNode;
            break;
        case InlineFunctionObject:
            kind = FunctionGraphNode;
            break;
        case DataObject:
        case AbsoluteObject:
            kind = DataGraphNode;
            break;
        default:
            CError_Internal(RC1_CCallGraph_source_file, 365);
            break;
    }
    CallGraphNode* node = galloc2(sizeof(*node));
    memclrw(node, sizeof(*node));
    if (RC1_call_graph_tail) {
        RC1_call_graph_tail->next = node;
        node->previous = RC1_call_graph_tail;
        RC1_call_graph_tail = node;
    } else {
        RC1_call_graph_tail = node;
        ccallgraph = node;
    }
    node->object = object;
    node->index = RC1_call_graph_next_index++;
    object->graph_node = node;
    node->kind = kind;
    return node;
}

void retro_NewInitCodeNode(const InlineBody* body) {
    Object* object = CParser_NewFunctionObject(0);
    object->scope = cscope_root;
    object->name = CParser_AppendUniqueName("<init-code>");
    object->type = &rt_func;
    object->flags |= ObjectGraphRoot;
    retro_SetFuncData(object, body);
    CallGraphNode* node = retro_NewGraphNode(object);
    node->is_initializer = 1;
}

void retro_RenumberGraph(void) {
    unsigned index = 0;
    for (CallGraphNode* node = ccallgraph; node; node = node->next)
        node->index = index++;
    RC1_call_graph_next_index = index;
}

static void remove_graph_edge(GraphEdge** link, const CallGraphNode* node, int source_line) {
    while (*link && (*link)->node != node)
        link = &(*link)->next;
    if (!*link)
        CError_Internal(RC1_CCallGraph_source_file, source_line);
    GraphEdge* edge = *link;
    GraphEdge* next = edge->next;
    gfree2(edge, sizeof(*edge));
    *link = next;
}

void retro_RemoveGraphNode(CallGraphNode* node) {
    if (node->previous)
        node->previous->next = node->next;
    else
        ccallgraph = node->next;
    if (node->next)
        node->next->previous = node->previous;
    else
        RC1_call_graph_tail = node->previous;

    for (const GraphEdge* edge = node->used_by; edge; edge = edge->next)
        remove_graph_edge(&edge->node->uses, node, 450);
    for (const GraphEdge* edge = node->uses; edge; edge = edge->next)
        remove_graph_edge(&edge->node->used_by, node, 464);
    GraphEdge* edge = node->uses;
    while (edge) {
        GraphEdge* next = edge->next;
        gfree2(edge, sizeof(*edge));
        edge = next;
    }
    edge = node->used_by;
    while (edge) {
        GraphEdge* next = edge->next;
        gfree2(edge, sizeof(*edge));
        edge = next;
    }
    ((Object*)node->object)->graph_node = 0;
    gfree2(node, sizeof(*node));
}

static unsigned char graph_object_is_root(Object* object) {
    if (!object || (object->flags & ObjectGraphRoot))
        return 1;
    if (object->qualifiers & ImplicitWeak)
        return RC1_allow_weak_graph_roots && CIMan_NeedsObjectInstance(object);
    if ((object->qualifiers & Inline) || object->storage_class == StaticStorageClass)
        return 0;
    for (const NameSpace* scope = object->scope; scope; scope = scope->parent) {
        if (scope->is_unnamed)
            return 0;
    }
    return 1;
}

void retro_ColorGraphUses(CallGraphNode* node) {
    /* RC1 descends six nodes before recursing. Retain that bound so long
     * dependency chains do not require a C stack frame for every node.
     */
    GraphEdge* edges[6];
    unsigned depth = 0;
    node->marked = 1;
    edges[0] = node->uses;
    for (;;) {
        GraphEdge* edge = edges[depth];
        if (!edge) {
            if (!depth)
                return;
            --depth;
            edges[depth] = edges[depth]->next;
            continue;
        }
        CallGraphNode* target = edge->node;
        if (!target->marked) {
            if (depth == 5) {
                retro_ColorGraphUses(target);
            } else {
                target->marked = 1;
                edges[++depth] = target->uses;
                continue;
            }
        }
        edges[depth] = edge->next;
    }
}

void retro_UnmarkGraph(void) {
    for (CallGraphNode* node = ccallgraph; node; node = node->next)
        node->marked = 0;
}

void retro_UnmarkAndUnflagGraph(void) {
    for (CallGraphNode* node = ccallgraph; node; node = node->next) {
        node->marked = 0;
        node->flagged = 0;
    }
}

void retro_RemoveDeadGraphNodes(void) {
    for (CallGraphNode* node = ccallgraph; node; node = node->next)
        node->marked = 0;
    for (CallGraphNode* node = ccallgraph; node; node = node->next) {
        if (!node->marked && (node->is_initializer || graph_object_is_root((Object*)node->object)))
            retro_ColorGraphUses(node);
    }
    unsigned char removed = 0;
    for (CallGraphNode* node = ccallgraph; node;) {
        CallGraphNode* next = node->next;
        if (!node->marked) {
            retro_RemoveGraphNode(node);
            removed = 1;
        }
        node = next;
    }
    if (removed)
        retro_RenumberGraph();
}

static void free_graph_edges(GraphEdge* edge) {
    while (edge) {
        GraphEdge* next = edge->next;
        gfree2(edge, sizeof(*edge));
        edge = next;
    }
}

static unsigned char has_graph_object_type(const Object* object) {
    switch (object->datatype) {
        case DataObject:
        case AbsoluteObject:
        case OrdinaryFunction:
        case VirtualFunction:
        case InlineFunctionObject:
            return 1;
        default:
            return 0;
    }
}

static CallGraphNode* graph_reference_node(Object* object) {
    return object->graph_node ? object->graph_node : retro_NewGraphNode(object);
}

static GraphEdge* prepend_graph_edge(GraphEdge** list, CallGraphNode* target,
                                    unsigned count, unsigned short flags) {
    GraphEdge* edge = galloc2(sizeof(*edge));
    memclrw(edge, sizeof(*edge));
    edge->next = *list;
    edge->node = target;
    edge->reference_count = count;
    edge->flags = flags;
    *list = edge;
    return edge;
}

void retro_ComputeGraph(void) {
    retro_RenumberGraph();
    /* Nodes discovered by these references append to the same list and are
     * visited later in this pass. Rebuild incoming edges only after that.
     */
    for (CallGraphNode* node = ccallgraph; node; node = node->next) {
        free_graph_edges(node->uses);
        node->uses = 0;
        free_graph_edges(node->used_by);
        node->used_by = 0;
        switch (node->kind) {
            case FunctionGraphNode: {
                const InlineBody* body = retro_GetFuncData(node->object);
                if (!body)
                    break;
                if (!body->code)
                    CError_Internal(RC1_CCallGraph_source_file, 609);
                const PackedInlineCode* code = body->code;
                unsigned count = code->reference_count;
                for (unsigned i = 0; i < count; ++i) {
                    const PackedReference* ref = &code->references[i];
                    if (ref->kind == PackedObjectReference && has_graph_object_type(ref->object))
                        prepend_graph_edge(&node->uses, graph_reference_node(ref->object),
                                           ref->reference_count, ref->flags & 1);
                }
                break;
            }
            case ThunkGraphNode: {
                const ThunkData* thunk = CMid_GetThunkData(node->object);
                if (!thunk)
                    CError_Internal(RC1_CCallGraph_source_file, 627);
                if (has_graph_object_type(thunk->target))
                    prepend_graph_edge(&node->uses, graph_reference_node(thunk->target), 1, 0);
                break;
            }
            case DataGraphNode: {
                const InitData* data = CMid_GetInitData(node->object);
                if (!data)
                    break;
                for (const VTableRef* ref = data->references; ref; ref = ref->next) {
                    if (!has_graph_object_type(ref->object))
                        continue;
                    CallGraphNode* target = graph_reference_node((Object*)ref->object);
                    GraphEdge* edge = node->uses;
                    while (edge && edge->node != target)
                        edge = edge->next;
                    if (!edge)
                        edge = prepend_graph_edge(&node->uses, target, 0, 0);
                    edge->flags |= 1;
                }
                break;
            }
            default:
                CError_Internal(RC1_CCallGraph_source_file, 642);
                break;
        }
    }
    for (CallGraphNode* node = ccallgraph; node; node = node->next) {
        for (const GraphEdge* edge = node->uses; edge; edge = edge->next)
            prepend_graph_edge(&edge->node->used_by, node, edge->reference_count, edge->flags);
    }
}

unsigned char retro_IsGraphNodeDefined(const CallGraphNode* node) {
    switch (node->kind) {
        case FunctionGraphNode:
            return retro_GetFuncData(node->object) != 0;
        case ThunkGraphNode:
            return 1;
        case DataGraphNode:
            return CMid_GetInitData(node->object) != 0;
        default:
            CError_Internal(RC1_CCallGraph_source_file, 248);
            return 0;
    }
}

FunctionData* retro_GetGraphIR(FunctionData* data, const CallGraphNode* node) {
    const InlineBody* body = retro_GetFuncData(node->object);
    if (!body)
        CError_Internal(RC1_CCallGraph_source_file, 490);
    FunctionData unpacked;
    CIRStream_UnpackCode(body->code, &unpacked, 0);
    if (!node->is_initializer)
        unpacked.object = (Object*)node->object;
    *data = unpacked;
    return data;
}

void retro_SetGraphIR(const CallGraphNode* node, const FunctionData* data) {
    InlineBody* body = (InlineBody*)retro_GetFuncData(node->object);
    if (!body)
        CError_Internal(RC1_CCallGraph_source_file, 509);
    CIRStream_Free(body->code);
    body->code = retro_PackCode(data);
}

static void queue_deferred_export(const Object* object) {
    DeferredExport* entry = galloc2(sizeof(*entry));
    entry->next = RC1_deferred_exports;
    entry->object = object;
    RC1_deferred_exports = entry;
}

static DeferredAction* allocate_deferred_action(Object* object) {
    DeferredAction* action = galloc2(sizeof(*action));
    memclrw(action, sizeof(*action));
    action->next = RC1_deferred_actions;
    action->object = object;
    return action;
}

static void queue_deferred_action(Object* object, unsigned char kind) {
    DeferredAction* action = allocate_deferred_action(object);
    action->kind = kind;
    RC1_deferred_actions = action;
}

static void add_definition_to_call_graph(Object* object, void (*record_reference)(Object*)) {
    retro_NewGraphNode(object);
    const InlineBody* body = retro_GetFuncData(object);
    if (!body)
        return;
    if (!body->code)
        CError_Internal(RC1_CMiddleLayer_source_file, 1108);
    const PackedInlineCode* code = body->code;
    unsigned count = code->reference_count;
    for (unsigned i = 0; i < count; ++i) {
        const PackedReference* reference = &code->references[i];
        if (reference->kind == PackedObjectReference)
            record_reference(reference->object);
    }
}

void retro_ObjectAddrRef(Object* object) {
    object->flags |= Referenced;

    switch (object->datatype) {
        case DataObject:
            if (object->qualifiers & InlineData)
                CInit_ExportConst(object);
            return;
        case AliasObject:
            retro_ObjectAddrRef(((ObjectAlias*)object)->target);
            return;
        case OrdinaryFunction:
        case VirtualFunction:
            break;
        default:
            return;
    }

    if (object->flags & ObjectDefined)
        return;

    FunctionObject* function = (FunctionObject*)object;
    if ((object->qualifiers & Inline) && retro_GetFuncData(object) &&
        !(object->type->flags & FunctionTemplate)) {
        object->flags |= ObjectDefined;
        if (cparamblkptr->precompile) {
            CError_Error(0x2908, object);
            return;
        }
        if (cmid_in_ipa_mode)
            add_definition_to_call_graph(object, retro_ObjectAddrRef);
        else
            queue_deferred_export(object);
        return;
    }

    if ((object->qualifiers & TemplateInstance) && !function->point_of_instantiation)
        function->point_of_instantiation = CTemplTool_GetPointOfInstatiation();
    if ((object->type->flags & (AutoGenerated | FunctionDefined)) == AutoGenerated) {
        CMid_AddDefaultFunctionAction(object);
        object->flags |= ObjectDefined;
    } else if (function->instance) {
        queue_deferred_action(object, FunctionTemplateAction);
    }
}

static void collect_definition_reference(Object* object) {
    if (object->datatype != DataObject && object->datatype != OrdinaryFunction &&
        object->datatype != VirtualFunction)
        return;
    for (const ObjectList* ref = definition_references; ref; ref = ref->next) {
        if (ref->object == object)
            return;
    }
    ObjectList* ref = lalloc(sizeof(*ref));
    ref->object = object;
    ref->next = definition_references;
    definition_references = ref;
}

static void collect_expression_reference(ENode* expression) {
    if (expression->kind == ObjectReferenceExpression)
        collect_definition_reference(expression->object);
}

static void collect_cleanup_references(const ExceptionAction* action) {
    for (; action; action = action->previous) {
        switch (action->kind) {
            case DestroyLocalAction: case 3: case 4: case 5: case DestroyMemberAction:
            case 9: case 10: case 11: case 12: case 17: case 19: case 20:
                collect_definition_reference(action->routine);
                break;
            case 2: case 6: case 8: case 21:
                collect_definition_reference(action->operand.other_routine);
                break;
            case 13: case 14: case 15: case 16: case 18: case 22:
                break;
            default:
                CError_Internal(RC1_CMiddleLayer_source_file, 512);
                break;
        }
    }
}

static void collect_asm_references(const Statement* statement) {
    PackedAsmStatement packed;
    InlineAsm_NewPackAsmStatement(statement, &packed);
    for (const AsmRelocation* ref = packed.relocations; ref; ref = ref->next) {
        if (!ref->is_label) {
            Object* object;
            __builtin_memcpy(&object, packed.code + ref->offset, sizeof(object));
            collect_definition_reference(object);
        }
    }
}

static void collect_statement_references(const Statement* stmt) {
    for (; stmt; stmt = stmt->next) {
        if (stmt->cleanups)
            collect_cleanup_references(stmt->cleanups);
        switch (stmt->kind) {
            case NopStatement: case LabelStatement: case GotoStatement: case BeginCatchStatement:
            case EndCatchStatement: case EndCatchDestructorStatement:
                break;
            case ReturnStatement:
                if (!stmt->expression)
                    break;
                /* fall through */
            case ExpressionStatement: case SwitchStatement: case IfGotoStatement:
            case IfNotGotoStatement: case GotoExpressionStatement:
                CExpr_ENodeTreeWalk(stmt->expression, collect_expression_reference);
                break;
            case AsmStatement:
                collect_asm_references(stmt);
                break;
            default:
                CError_Internal(RC1_CMiddleLayer_source_file, 552);
                break;
        }
    }
}

static void queue_deferred_member(Object* object, const TemplateMember* member) {
    void* scope = member->scope;
    ClassTemplateInstance* instance;
    const Object* template_declaration = 0;
    if (scope) {
        instance = CTemplTool_Class2TemplClassInst(scope);
    } else {
        if (object->type->kind != FunctionTypeKind || !(object->qualifiers & TemplateInstance))
            CError_Internal(RC1_CMiddleLayer_source_file, 701);
        template_declaration = ((const FunctionObject*)object)->template_declaration;
        if (!(template_declaration->qualifiers & Inline))
            return;
        if (!(object->type->flags & Method))
            CError_Internal(RC1_CMiddleLayer_source_file, 707);
        instance = CTemplTool_Class2TemplClassInst(object->type->owner);
        if (!instance)
            CError_Internal(RC1_CMiddleLayer_source_file, 708);
        scope = instance;
    }

    DeferredAction* action = allocate_deferred_action(object);
    void* point = object->qualifiers & TemplateInstance ? CTemplTool_GetPointOfInstatiation() : 0;
    if (member->definition) {
        action->kind = ClassTemplateMemberAction;
        action->payload.member.point_of_instantiation = point;
        action->payload.member.instance = instance;
        action->payload.member.member = member;
        action->payload.member.template_declaration = template_declaration;
    } else {
        action->kind = StreamedFunctionAction;
        action->payload.function.instance_scope = point;
        action->payload.function.location = member->location;
        action->payload.function.stream = member->stream;
        action->payload.function.scope = scope;
    }
    RC1_deferred_actions = action;
}

static unsigned char queue_referenced_function(Object* object) {
    for (const DeferredAction* action = RC1_deferred_actions; action; action = action->next) {
        if (action->object == object)
            return 1;
    }
    if (object->type->kind != FunctionTypeKind)
        CError_Internal(RC1_CMiddleLayer_source_file, 780);
    if (object->type->flags & FunctionDefined)
        return 0;
    if (object->type->flags & AutoGenerated) {
        queue_deferred_action(object, ImplicitSpecialMemberAction);
        return 1;
    }
    const TemplateMember* member = CTemplTool_GetTemplateMember(object);
    if (member) {
        queue_deferred_member(object, member);
        return 1;
    }
    if ((object->qualifiers & Inline) && ((const FunctionObject*)object)->instance) {
        queue_deferred_action(object, FunctionTemplateAction);
        return 1;
    }
    return 0;
}

unsigned char retro_RecordDefinitionReferences(const Statement* statements, ENode* expression) {
    definition_references = 0;
    if (statements)
        collect_statement_references(statements);
    else
        CExpr_ENodeTreeWalk(expression, collect_expression_reference);

    unsigned char pending_functions = 0;
    for (const ObjectList* ref = definition_references; ref; ref = ref->next) {
        Object* object = ref->object;
        if (object->datatype == OrdinaryFunction || object->datatype == VirtualFunction)
            pending_functions |= queue_referenced_function(object);
        else if (cmid_in_ipa_mode && CParser_IsDataObject(object))
            retro_RecordIRDataReference(object);
    }
    return pending_functions;
}

static const FunctionType* template_method(const Object* object) {
    if (!object || !(object->qualifiers & TemplateInstance))
        return 0;
    const FunctionType* type = object->type;
    return type && type->kind == FunctionTypeKind && (type->flags & Method) ? type : 0;
}

static int free_function_template(const Object* object) {
    const FunctionType* type = object->type;
    return type && type->kind == FunctionTypeKind &&
           (type->flags & (FunctionTemplateInstance | Method)) == FunctionTemplateInstance;
}

static const OrderRecord* sibling_template_action(const DeferredExport* entry,
                                                  const OrderRecord* action) {
    if (!entry || !action || !action->first_referrer)
        return 0;
    const OrderRecord* queued = find_order(entry->object, 0);
    return queued && queued->from_template_action && queued->first_referrer == action->first_referrer
               ? queued : 0;
}

static void pack_definition(FunctionData* data) {
    InlineBody body;
    COpt_SimpleOptimizer(data->object, data->statements);
    CInline_SeqStringRewrite(data->statements);
    CInline_PackIFunctionData(data, &body);
    retro_SetFuncData(data->object, &body);
}

static void define_function(FunctionData* data) {
    if (!data->object)
        CError_Internal(RC1_CMiddleLayer_source_file, 1163);
    cscope_currentfunc = data->object;
    data->object->type->flags |= FunctionDefined;
    arguments = data->arguments;
    RC1_locals = data->locals;
    CExpr_IRTreeWalk(data->statements, RC1_RewriteDefinitionExpression);
    data->arguments = arguments;
    data->locals = RC1_locals;

    unsigned char inline_prevented;
    unsigned char can_inline = CInline_CheckInlineability(data->object, data->statements,
                                                         &inline_prevented);
    if (can_inline)
        pack_definition(data);
    retro_RecordDefinitionReferences(data->statements, 0);

    Object* object = data->object;
    if (can_inline && !(object->type->flags & AutoInline) && !(object->flags & Referenced))
        return;
    object->flags |= ObjectDefined;

    if (RC1_deferred_actions || cmid_in_ipa_mode || RC1_defer_codegen) {
        if (!can_inline)
            pack_definition(data);
        if (cparamblkptr->precompile) {
            CError_Error(0x2908, data->object);
        } else if (cmid_in_ipa_mode) {
            add_definition_to_call_graph(data->object,
                                         can_inline ? retro_ObjectAddrRef
                                                    : retro_RecordIPASerializedReference);
        } else {
            queue_deferred_export(data->object);
        }
        return;
    }

    if (cparamblkptr->precompile) {
        CError_Error(0x2908, data->object);
        return;
    }
    CInline_Expand(data);
    if (CIMan_NeedsObjectInstance(data->object) && !anyerrors)
        retro_GenerateFunction(data);
}

void retro_ExportDeferredFunction(Object* object) {
    const InlineBody* body = retro_GetFuncData(object);
    if (!body)
        CError_Internal(RC1_CMiddleLayer_source_file, 1649);
    BackEndOptions previous_options;
    CMach_GetBackEndOptions(&previous_options);
    CMach_SetBackEndOptions(&body->options);
    ObjGen_SetupSym();
    ScopeSave previous_scope;
    CScope_SetFunctionScope(object, &previous_scope);
    Statement first_statement;
    CFunc_FuncGenSetup(0, &first_statement, object, 0);
    CInline_UnpackIFunctionData(object, body, &first_statement, 0);

    unsigned char previous_debug = RC1_filesyminfo;
    if (RC1_nosyminline || !cfunc_funcdefnref.words[0])
        RC1_filesyminfo = 0;
    memclrw(&cgparams, sizeof(cgparams));
    cgparams.statements = &first_statement;
    cgparams.object = object;
    cgparams.arguments = arguments;
    cgparams.locals = RC1_locals;
    cgparams.old_style_parameters = CFunc_IsOldStyleFunction(object);
    cgparams.call_on_module_bind = 0;
    CParser_FuncDebugSetup(0, 0, 0);
    CInline_Expand(&cgparams);
    if (!anyerrors && CIMan_NeedsObjectInstance(object))
        retro_GenerateFunction(&cgparams);
    CScope_RestoreScope(&previous_scope);
    RC1_filesyminfo = previous_debug;
    CMach_SetBackEndOptions(&previous_options);
    if (!(object->qualifiers & Inline) && !(object->type->flags & AutoInline))
        retro_FreeFuncData(object);
}

void retro_EmitIPAFunction(const CallGraphNode* node) {
    Object* object = (Object*)node->object;
    if (!anyerrors && (node->is_initializer || CIMan_NeedsObjectInstance(object))) {
        const InlineBody* body = retro_GetFuncData(object);
        if (!body)
            CError_Internal(RC1_CMiddleLayer_source_file, 2209);
        if (!body->code)
            CError_Internal(RC1_CMiddleLayer_source_file, 2210);
        BackEndOptions previous_options;
        CMach_GetBackEndOptions(&previous_options);
        CMach_SetBackEndOptions(&body->options);
        ObjGen_SetupSym();
        ScopeSave previous_scope;
        CScope_SetFunctionScope(object, &previous_scope);
        FunctionData data;
        retro_GetGraphIR(&data, node);
        if (!anyerrors)
            retro_GenerateFunction(&data);
        CScope_RestoreScope(&previous_scope);
        CMach_SetBackEndOptions(&previous_options);
    }
    if (!(object->qualifiers & Inline) && !(object->type->flags & AutoInline))
        retro_FreeFuncData(object);
}

void retro_GenerateCGNode(const CallGraphNode* node) {
    Object* object = (Object*)node->object;
    object->flags |= Referenced | ObjectDefined;
    switch (node->kind) {
        case FunctionGraphNode:
            retro_EmitIPAFunction(node);
            break;
        case ThunkGraphNode: {
            const ThunkData* thunk = CMid_GetThunkData(object);
            object->flags |= ObjectDefined;
            CodeGen_GenVDispatchThunk(object, thunk->target, thunk->this_delta,
                                      thunk->return_delta, thunk->constructor_offset);
            break;
        }
        case DataGraphNode: {
            const InitData* init = CMid_GetInitData(object);
            if (!init)
                CError_Internal(RC1_CMiddleLayer_source_file, 2311);
            if (RC1_filesyminfo) {
                cparser_decloffset = init->location;
                cparser_fileoffset = cparser_decloffset;
                CPrep_SetSourceFile(&cparser_fileoffset);
            }
            unsigned char is_read_only = init->is_read_only;
            const void* data = init->data;
            const VTableRef* references = init->references;
            unsigned size = init->size;
            object->flags |= ObjectDefined;
            for (const VTableRef* ref = references; ref; ref = ref->next)
                retro_ObjectAddrRef((Object*)ref->object);
            unsigned qualifiers = object->qualifiers;
            if (is_read_only)
                ObjGen_DeclareReadOnlyData(object, data, references, size);
            else
                ObjGen_DeclareData(object, data, references, size);
            object->qualifiers = qualifiers;
            break;
        }
        default:
            CError_Internal(RC1_CMiddleLayer_source_file, 2327);
            break;
    }
}

void retro_DefineFunction(FunctionData* data) {
    if (RC1_ipa_kind == FileIPA) {
        const Object* previous = ipa_defining_function;
        ipa_defining_function = data->object;
        if (is_constructor(data->object)) {
            OrderRecord* record = find_order(data->object, 0);
            if (record && record->ipa_ctor_referrer)
                record->ipa_ctor_definition = declaration_number;
            define_ipa_constructor(data);
        } else {
            const FunctionType* method = template_method(data->object);
            if (method && method->owner && !(method->flags & (Constructor | Destructor)) &&
                (data->object->qualifiers & ImplicitWeak)) {
                find_order(method->owner, 1)->helper_declaration = declaration_number;
            }
            define_function(data);
        }
        ipa_defining_function = previous;
        return;
    }
    int defer_sibling = 0;
    if (!RC1_ipa_kind && data->object) {
        OrderRecord* record = find_order(data->object, 1);
        record->definition = ++definition_count;
        /* The final action can generate its body directly. Keep it behind
         * earlier definitions from the same action group.
         */
        defer_sibling = defining_template_action && !RC1_defer_codegen && !RC1_deferred_actions &&
                        free_function_template(data->object) &&
                        sibling_template_action(RC1_deferred_exports, record);
    }
    if (defer_sibling)
        RC1_defer_codegen = 1;
    define_function(data);
    if (defer_sibling)
        RC1_defer_codegen = 0;
}

/* File IPA sees the constructor's reference before that constructor is
 * inlined. A weak method defined for this instance in the current
 * declaration identifies the out-of-line helper path.
 */
void retro_RecordIRDataReference(Object* object) {
    if (RC1_ipa_kind == FileIPA && !(object->flags & Referenced) &&
        (object->qualifiers & ImplicitWeak)) {
        const FunctionType* caller = template_method(cscope_currentfunc);
        if (caller && caller->owner && (caller->flags & Constructor)) {
            const OrderRecord* owner = find_order(caller->owner, 0);
            if (!owner || owner->helper_declaration != declaration_number)
                find_order(object, 1)->delay_vtable = 1;
        }
    }
    retro_ObjectAddrRef(object);
}

int retro_DelayTemplateVTable(const Object* table) {
    const OrderRecord* record = find_order(table, 0);
    return record && record->delay_vtable;
}

/* Let the end-of-file pass emit tables first referenced by ordinary functions. */
void retro_RecordFirstReference(Object* object) {
    if (generating_function && !(generating_function->qualifiers & TemplateInstance) &&
        !(object->flags & Referenced) && (object->qualifiers & ImplicitWeak) &&
        object->type && object->type->kind != FunctionTypeKind) {
        find_order(object, 1)->delay_vtable = 1;
    }
    if (generating_function && !(object->flags & Referenced) &&
        ((object->qualifiers & TemplateInstance) || free_function_template(object))) {
        OrderRecord* record = find_order(object, 1);
        if (!record->first_referrer) {
            record->first_referrer = generating_function;
            record->first_reference = ++reference_count;
        }
    }
    retro_ObjectAddrRef(object);
}

/* CMid_DefineFunction prepends new exports. Keep the exports from template
 * actions for one referrer in reverse first-reference order.
 */
unsigned char retro_GenDeferredFuncInstance(void* templ, void* instance,
                                            unsigned char explicit_instantiation) {
    DeferredExport* previous = RC1_deferred_exports;
    unsigned char previous_action = defining_template_action;
    defining_template_action = 1;
    unsigned char generated = retro_GenerateFunctionInstance(templ, instance, explicit_instantiation);
    defining_template_action = previous_action;

    DeferredExport* entry = RC1_deferred_exports;
    if (RC1_ipa_kind || !entry || entry == previous || entry->next != previous ||
        !free_function_template(entry->object))
        return generated;

    OrderRecord* record = find_order(entry->object, 0);
    if (!record || !record->first_referrer)
        return generated;

    record->from_template_action = 1;
    DeferredExport** position = &entry->next;
    for (;;) {
        const OrderRecord* sibling = sibling_template_action(*position, record);
        if (!sibling || sibling->first_reference < record->first_reference)
            break;
        position = &(*position)->next;
    }
    if (position != &entry->next) {
        RC1_deferred_exports = previous;
        entry->next = *position;
        *position = entry;
    }
    return generated;
}

/* CMiddleLayer's declaration worker pops one item and can enqueue more
 * work while defining it. Export bodies drain when the action list is empty.
 */
unsigned char retro_RunDeferredAction(void) {
    if (anyerrors && !RC1_process_deferred_after_errors)
        return 0;

    DeferredAction* action = RC1_deferred_actions;
    if (action) {
        RC1_deferred_actions = action->next;
        Object* object = action->object;
        switch (action->kind) {
            case StreamedFunctionAction:
                if (!(object->flags & ObjectDefined)) {
                    CFunc_GenerateStreamedFunction(object, action->payload.function.scope,
                                                   action->payload.function.instance_scope,
                                                   &action->payload.function.stream,
                                                   &action->payload.function.location, 0);
                }
                break;
            case ClassTemplateMemberAction:
                if (!(object->type->flags & FunctionDefined)) {
                    CTempl_InstantiateMember(action->payload.member.instance, action->payload.member.member,
                                             action->payload.member.template_declaration,
                                             action->payload.member.point_of_instantiation, object, 0);
                }
                break;
            case FunctionTemplateAction:
                if (!(object->type->flags & FunctionDefined)) {
                    DeferredFunctionInstance* instance = ((FunctionObject*)object)->instance;
                    if (!instance->is_specialized)
                        retro_GenDeferredFuncInstance(CTemplTool_Template2TemplFunc(instance->templ), instance, 0);
                }
                break;
            case ImplicitSpecialMemberAction:
                CFunc_GenerateDefaultFunc(object);
                break;
            case FriendFunctionAction:
                CFunc_ParseFriendFunction(action->payload.friend_function.instance,
                                          action->payload.friend_function.member);
                break;
            default:
                CError_Internal(RC1_CMiddleLayer_source_file, 2012);
                break;
        }
        gfree2(action, sizeof(*action));
        return 1;
    }

    DeferredExport* entry = RC1_deferred_exports;
    if (!entry || RC1_defer_codegen || cmid_in_ipa_mode || anyerrors)
        return 0;
    RC1_deferred_exports = entry->next;
    retro_ExportDeferredFunction((Object*)entry->object);
    gfree2(entry, sizeof(*entry));
    return 1;
}

/* Mirror the native visitor's operand selection to identify the first action
 * that referenced each newly enqueued function.
 */
static int cleanup_references(const ExceptionAction* action, const Object* object) {
    switch (action->kind) {
    case DestroyLocalAction: case 3: case 4: case 5: case DestroyMemberAction: case 9: case 10:
    case 11: case 12: case 17:
        return action->routine == object;
    case 2: case 6: case 8: case 21:
        return action->operand.other_routine == object;
    case 19: case 20:
        return action->routine == object || action->operand.other_routine == object;
    default:
        return 0;
    }
}

static int is_constructor(const Object* object) {
    return object && object->type && object->type->kind == FunctionTypeKind &&
           (object->type->flags & Constructor);
}

static int is_temporary(const Object* object) {
    /* RC1's temporary constructors use CParser_GetUniqueName's @ names. */
    return object && object->name && object->name->name[0] == '@';
}

static int is_this_object(const Object* object) {
    return object && object->name && object->name == this_name_node;
}

void retro_CollectCleanupReferences(const ExceptionAction* actions) {
    DeferredExport* previous = RC1_deferred_exports;
    CExcept_CheckStackRefs(actions);
    if (!is_constructor(generating_function) ||
        !has_boundary(RC1_deferred_exports, previous))
        return;

    for (const DeferredExport* entry = RC1_deferred_exports; entry != previous; entry = entry->next) {
        const ExceptionAction* action = actions;
        while (action && !cleanup_references(action, entry->object))
            action = action->previous;
        if (action && action->kind == DestroyMemberAction) {
            const Object* object = action->object_pointer;
            OrderRecord* record = find_order(entry->object, 1);
            record->member_cleanup_entry = entry;
            record->member_cleanup_object = object;
            record->member_cleanup_offset = action->operand.member_offset;
            record->direct_member_cleanup = is_this_object(object);
        } else if (action && action->kind == DestroyLocalAction && is_temporary(action->object_pointer)) {
            find_order(entry->object, 1)->temporary_cleanup_entry = entry;
        }
    }
}

static const OrderRecord* member_cleanup(const DeferredExport* entry) {
    const OrderRecord* record = find_order(entry->object, 0);
    return record && record->member_cleanup_entry == entry ? record : 0;
}

static const Object* member_cleanup_object(const DeferredExport* entry) {
    const OrderRecord* record = member_cleanup(entry);
    return record ? record->member_cleanup_object : 0;
}

/* Emit adjacent member destructors for one object in member layout order.
 * The object pointer distinguishes inlined subobjects from the constructor's
 * own members. Local and base cleanups also delimit these runs.
 */
static void order_member_cleanups(DeferredExport** link, DeferredExport* end) {
    while (*link != end) {
        DeferredExport* start = *link;
        const Object* object = member_cleanup_object(start);
        DeferredExport* stop = start->next;
        if (object && stop != end && member_cleanup_object(stop) == object) {
            do {
                stop = stop->next;
            } while (stop != end && member_cleanup_object(stop) == object);

            DeferredExport* sorted = stop;
            while (start != stop) {
                DeferredExport* next = start->next;
                int offset = member_cleanup(start)->member_cleanup_offset;
                DeferredExport** position = &sorted;
                while (*position != stop && member_cleanup(*position)->member_cleanup_offset <= offset)
                    position = &(*position)->next;
                start->next = *position;
                *position = start;
                start = next;
            }
            *link = sorted;
        }
        while (*link != stop)
            link = &(*link)->next;
    }
}

/* Place temporary cleanups after the constructor's direct member cleanups.
 * Keep the relative order of the temporaries and of exports left in place.
 */
static void order_temporary_cleanups(DeferredExport** head, DeferredExport* end) {
    DeferredExport* last_member = 0;
    for (DeferredExport* entry = *head; entry != end; entry = entry->next) {
        const OrderRecord* record = member_cleanup(entry);
        if (record && record->direct_member_cleanup)
            last_member = entry;
    }
    if (!last_member)
        return;

    DeferredExport** link = head;
    DeferredExport** after = &last_member->next;
    while (*link != last_member) {
        DeferredExport* entry = *link;
        const OrderRecord* record = find_order(entry->object, 0);
        if (!record || record->temporary_cleanup_entry != entry) {
            link = &entry->next;
            continue;
        }
        *link = entry->next;
        entry->next = *after;
        *after = entry;
        after = &entry->next;
    }
}

typedef struct IPACleanup {
    struct IPACleanup* next;
    const Object* routine;
    CleanupOrder kind;
    int offset;
} IPACleanup;

typedef struct IPAConstructor {
    const Object* function;
    const IPACleanup* cleanups;
    const Object* pending_copy;
} IPAConstructor;

static CleanupOrder ipa_cleanup_order(const ExceptionAction* action) {
    if (action->kind == DestroyMemberAction && is_this_object(action->object_pointer))
        return DirectMemberCleanup;
    if (action->kind == DestroyLocalAction && is_temporary(action->object_pointer))
        return TemporaryCleanup;
    return OtherCleanup;
}

static const IPACleanup* direct_ipa_member(const Object* routine) {
    if (ipa_constructor) {
        for (const IPACleanup* cleanup = ipa_constructor->cleanups; cleanup; cleanup = cleanup->next)
            if (cleanup->routine == routine && cleanup->kind == DirectMemberCleanup)
                return cleanup;
    }
    return 0;
}

/* The packed reference list visits the copy before the accessor that
 * supplies its source. Preserve adjacent copies and remember the slot for
 * a deferred definition first reached through a non-static member.
 */
static void record_ipa_copy_source(const Object* routine, CallGraphNode* previous) {
    if (!ipa_constructor || ipa_defining_function != ipa_constructor->function)
        return;

    const Object* copy = ipa_constructor->pending_copy;
    ipa_constructor->pending_copy = 0;
    const FunctionType* type = routine->type;
    if (!type || type->kind != FunctionTypeKind)
        return;

    const OrderRecord* copy_order = copy ? find_order(copy, 0) : 0;
    const CallGraphNode* early = copy_order ? copy_order->ipa_early_copy_node : 0;
    if (copy && previous != RC1_call_graph_tail && (type->flags & Method) &&
        (early ? early == previous : !type->is_static) &&
        type->result && type->result->kind == PointerTypeKind &&
        (type->result->qualifiers & 0xa0) == ReferenceQualifier &&
        type->result->target == copy->type->owner) {
        for (CallGraphNode* node = previous ? previous->next : ccallgraph; node; node = node->next) {
            if (node->object == routine) {
                find_order(copy, 1)->ipa_copy_source = node;
                break;
            }
        }
    }

    if (previous == RC1_call_graph_tail && (type->flags & (Constructor | FunctionDefined)) == Constructor &&
        (routine->qualifiers & Inline)) {
        const OrderRecord* record = find_order(routine, 0);
        if ((!record || !record->ipa_ctor_referrer) && RC1_IsCopyConstructor(routine))
            ipa_constructor->pending_copy = routine;
    }
}

void retro_RecordIPASerializedReference(Object* routine) {
    const IPACleanup* member = direct_ipa_member(routine);
    CallGraphNode* before = RC1_call_graph_tail;
    retro_ObjectAddrRef(routine);
    record_ipa_copy_source(routine, before);
    if (ipa_constructor && is_constructor(routine) && (routine->qualifiers & Inline)) {
        OrderRecord* record = find_order(routine, 1);
        if (!record->ipa_ctor_referrer) {
            record->ipa_ctor_referrer = ipa_constructor->function;
            record->ipa_ctor_reference = ++reference_count;
            record->ipa_ctor_declaration = declaration_number;
            if (before != RC1_call_graph_tail && (routine->qualifiers & ImplicitWeak) &&
                (routine->type->flags & AutoGenerated) && RC1_IsCopyConstructor(routine)) {
                for (CallGraphNode* node = before ? before->next : ccallgraph; node; node = node->next) {
                    if (node->object == routine) {
                        record->ipa_early_copy_node = node;
                        if (ipa_defining_function == ipa_constructor->function)
                            ipa_constructor->pending_copy = routine;
                        break;
                    }
                }
            }
        }
    }
    if (!ipa_defining_function || before == RC1_call_graph_tail)
        return;

    const FunctionType* type = routine->type;
    int weak_inline = type && type->kind == FunctionTypeKind &&
                      (routine->qualifiers & (Inline | ImplicitWeak)) &&
                      !(type->flags & (Constructor | Destructor));
    if (!member && !weak_inline)
        return;

    for (CallGraphNode* node = before ? before->next : ccallgraph; node; node = node->next) {
        const Object* object = node->object;
        if (object == routine) {
            if (weak_inline) {
                OrderRecord* record = find_order(routine, 1);
                record->serialized_node = node;
                record->serialized_referrer = ipa_defining_function;
                break;
            }
            continue;
        }
        /* A member cleanup can append destructors reached through its inline body. */
        if (!member || !object || !(object->qualifiers & ImplicitWeak) ||
            !object->type || object->type->kind != FunctionTypeKind ||
            !(object->type->flags & Destructor) || direct_ipa_member(object))
            continue;
        OrderRecord* record = find_order(object, 1);
        record->ipa_cleanup_node = node;
        record->ipa_cleanup_owner = find_order(ipa_constructor->function, 1);
        record->ipa_cleanup_kind = IndirectMemberCleanup;
        record->ipa_member_offset = member->offset;
    }
}

static void record_ipa_member_actions(const DeferredAction* boundary) {
    /* The definition prepends implicit special-member actions. A matching
     * routine is relevant only if this constructor added the action.
     */
    const DeferredAction* action = RC1_deferred_actions;
    while (action && action != boundary)
        action = action->next;
    if (action != boundary)
        return;

    for (action = RC1_deferred_actions; action != boundary; action = action->next) {
        const IPACleanup* member = action->kind == ImplicitSpecialMemberAction
                                      ? direct_ipa_member(action->object) : 0;
        if (member) {
            OrderRecord* record = find_order(action->object, 1);
            record->ipa_pending_member = action;
            record->ipa_cleanup_owner = find_order(ipa_constructor->function, 1);
            record->ipa_member_offset = member->offset;
        }
    }
}

static unsigned char run_ipa_deferred_cleanup(void) {
    const DeferredAction* action = RC1_deferred_actions;
    if (RC1_ipa_kind != FileIPA || !action || action->kind != ImplicitSpecialMemberAction)
        return retro_DeclarationWorkItem();

    const Object* object = action->object;
    OrderRecord* record = find_order(object, 0);
    if (!record || record->ipa_pending_member != action)
        return retro_DeclarationWorkItem();

    CallGraphNode* before = RC1_call_graph_tail;
    record->ipa_pending_member = 0;
    unsigned char ran = retro_DeclarationWorkItem();
    for (CallGraphNode* node = before ? before->next : ccallgraph; node; node = node->next) {
        if (node->object == object) {
            record->ipa_cleanup_node = node;
            record->ipa_cleanup_kind = DeferredMemberCleanup;
            break;
        }
    }
    return ran;
}

/* The declaration boundary drains constructor wrappers and single-expression
 * functions first. The later work items can leave new wrappers for the next
 * boundary. CMid_FreeLocalHeap protects token streams that are still queued.
 */
unsigned char retro_RunDeclarationActions(void) {
    unsigned char progress = 0;
    CMid_FreeLocalHeap();
    while (RC1_pending_dummy_functions) {
        PendingDummyFunction* pending = RC1_pending_dummy_functions;
        RC1_pending_dummy_functions = pending->next;
        if (pending->expression)
            CFunc_GenerateSingleExprFunc(pending->function, pending->expression);
        else
            CFunc_GenerateDummyCtorFunc(pending->function, pending->constructor);
        gfree2(pending, sizeof(*pending));
        CMid_FreeLocalHeap();
        progress = 1;
    }
    while (run_ipa_deferred_cleanup()) {
        CMid_FreeLocalHeap();
        progress = 1;
    }
    return progress;
}

/* CMid_DefineFunction serializes the constructor and can discover a
 * temporary's destructor through an earlier member's inline references.
 * Record cleanup provenance only for nodes appended by this definition.
 */
static void define_ipa_constructor(FunctionData* data) {
    const Object* constructor = data->object;
    CallGraphNode* previous = RC1_call_graph_tail;
    IPACleanup* cleanups = 0;
    for (const Statement* stmt = data->statements; stmt; stmt = stmt->next) {
        for (const ExceptionAction* action = stmt->cleanups; action; action = action->previous) {
            CleanupOrder kind = ipa_cleanup_order(action);
            if (kind == OtherCleanup)
                continue;
            const IPACleanup* known = cleanups;
            while (known && known->routine != action->routine)
                known = known->next;
            if (!known) {
                IPACleanup* cleanup = galloc(sizeof(*cleanup));
                cleanup->routine = action->routine;
                cleanup->kind = kind;
                cleanup->offset = kind == DirectMemberCleanup ? action->operand.member_offset : 0;
                cleanup->next = cleanups;
                cleanups = cleanup;
            }
        }
    }
    IPAConstructor context = {constructor, cleanups, 0};
    IPAConstructor* previous_constructor = ipa_constructor;
    const DeferredAction* action_boundary = RC1_deferred_actions;
    ipa_constructor = &context;
    define_function(data);
    if (cleanups)
        record_ipa_member_actions(action_boundary);
    ipa_constructor = previous_constructor;
    if (!cleanups)
        return;

    OrderRecord* owner = find_order(constructor, 1);
    for (CallGraphNode* node = previous ? previous->next : ccallgraph; node; node = node->next) {
        const IPACleanup* cleanup = cleanups;
        while (cleanup && cleanup->routine != node->object)
            cleanup = cleanup->next;
        if (cleanup) {
            OrderRecord* record = find_order(node->object, 1);
            /* The member's inline cleanup can introduce a virtual element
             * destructor also used by a later temporary in the same body. */
            if (cleanup->kind == TemporaryCleanup && node->object->datatype == VirtualFunction &&
                record->ipa_cleanup_node == node && record->ipa_cleanup_owner == owner &&
                record->ipa_cleanup_kind == IndirectMemberCleanup)
                continue;
            record->ipa_cleanup_node = node;
            record->ipa_cleanup_owner = owner;
            record->ipa_cleanup_kind = cleanup->kind;
        }
    }
}

static const OrderRecord* ipa_cleanup(const CallGraphNode* node) {
    const OrderRecord* record = find_order(node->object, 0);
    return record && record->ipa_cleanup_node == node && retro_IsGraphNodeDefined(node) ? record : 0;
}

static void move_graph_node_after(CallGraphNode* node, CallGraphNode* after) {
    if (node->previous)
        node->previous->next = node->next;
    else
        ccallgraph = node->next;
    if (node->next)
        node->next->previous = node->previous;
    else
        RC1_call_graph_tail = node->previous;
    node->previous = after;
    node->next = after->next;
    if (after->next)
        after->next->previous = node;
    else
        RC1_call_graph_tail = node;
    after->next = node;
}

static const void* destructor_template(const Object* object) {
    const ClassTemplateInstance* instance = object->type->owner;
    return instance && (instance->flags & ClassIsTemplateInstance) ? instance->template_info : 0;
}

/* A later member's inline cleanup can discover a specialization before
 * the deferred definitions for earlier members of the same template.
 * Keep those earlier members first. Walk backwards to retain the order
 * of indirect cleanups that move to the same position.
 */
static void order_indirect_ipa_members(void) {
    for (CallGraphNode* node = RC1_call_graph_tail; node;) {
        CallGraphNode* previous = node->previous;
        const OrderRecord* indirect = ipa_cleanup(node);
        if (indirect && indirect->ipa_cleanup_kind == IndirectMemberCleanup) {
            const void* templ = destructor_template(node->object);
            CallGraphNode* after = 0;
            for (CallGraphNode* next = templ ? node->next : 0; next; next = next->next) {
                const OrderRecord* member = ipa_cleanup(next);
                if (member && member->ipa_cleanup_kind == DeferredMemberCleanup &&
                    member->ipa_cleanup_owner == indirect->ipa_cleanup_owner &&
                    member->ipa_member_offset < indirect->ipa_member_offset &&
                    destructor_template(next->object) == templ)
                    after = next;
            }
            if (after)
                move_graph_node_after(node, after);
        }
        node = previous;
    }
}

/* Emit an adjacent weak virtual ahead of a free function when both were
 * first appended by their own references in the same serialized body.
 * Defined data and unrelated functions delimit these pairs.
 */
static void order_serialized_ipa_functions(void) {
    CallGraphNode* previous = 0;
    const OrderRecord* previous_record = 0;
    for (CallGraphNode* node = ccallgraph; node;) {
        CallGraphNode* next = node->next;
        if (retro_IsGraphNodeDefined(node)) {
            const Object* object = node->object;
            const OrderRecord* record =
                object && object->type && object->type->kind == FunctionTypeKind
                    ? find_order(object, 0) : 0;
            if (record && record->serialized_node != node)
                record = 0;
            if (previous_record && record &&
                previous_record->serialized_referrer == record->serialized_referrer &&
                !(previous->object->type->flags & Method) && object->datatype == VirtualFunction) {
                move_graph_node_after(previous, node);
            } else {
                previous = node;
                previous_record = record;
            }
        }
        node = next;
    }
}

/* Serialization can append a synthesized copy before earlier member
 * constructors are defined. The declaration-work hook advances the counter
 * before processing the pending definitions. Preserve the earlier references
 * when that batch ends with an ordinary class-template constructor.
 */
static void order_ipa_copy_constructors(void) {
    for (CallGraphNode* node = RC1_call_graph_tail; node;) {
        CallGraphNode* previous = node->previous;
        const OrderRecord* record = find_order(node->object, 0);
        if (record && record->ipa_early_copy_node == node && !record->ipa_copy_source &&
            retro_IsGraphNodeDefined(node)) {
            CallGraphNode* after = 0;
            for (CallGraphNode* next = node->next; next; next = next->next) {
                const OrderRecord* deferred = find_order(next->object, 0);
                if (deferred && deferred->ipa_ctor_referrer == record->ipa_ctor_referrer &&
                    deferred->ipa_ctor_reference < record->ipa_ctor_reference &&
                    deferred->ipa_ctor_definition == record->ipa_ctor_declaration + 1 &&
                    retro_IsGraphNodeDefined(next))
                    after = next;
            }
            if (after && template_method(after->object) && !RC1_IsCopyConstructor(after->object))
                move_graph_node_after(node, after);
        }
        node = previous;
    }
}

static void order_ipa_reference_copies(void) {
    for (CallGraphNode* node = ccallgraph; node;) {
        CallGraphNode* next = node->next;
        const OrderRecord* record = find_order(node->object, 0);
        CallGraphNode* source = record ? record->ipa_copy_source : 0;
        if (source && !record->ipa_early_copy_node && source->previous && source->previous != node &&
            record->ipa_ctor_definition == record->ipa_ctor_declaration + 1 &&
            (node->object->qualifiers & (ImplicitWeak | TemplateInstance)) &&
            retro_IsGraphNodeDefined(node) && retro_IsGraphNodeDefined(source)) {
            for (const CallGraphNode* prior = node->previous; prior; prior = prior->previous) {
                if (prior == source) {
                    move_graph_node_after(node, source->previous);
                    break;
                }
            }
        }
        node = next;
    }
}

/* Preserve the native graph order during optimization. */
void retro_OrderIPAFunctions(void) {
    CExcept_CallGraphExceptionOptimizer();
    if (RC1_ipa_kind != FileIPA)
        return;

    order_indirect_ipa_members();
    order_ipa_copy_constructors();
    order_ipa_reference_copies();
    for (CallGraphNode* node = ccallgraph; node; node = node->next) {
        const OrderRecord* record = ipa_cleanup(node);
        if (record && record->ipa_cleanup_kind == DirectMemberCleanup) {
            record->ipa_cleanup_owner->last_ipa_member = node;
            record->ipa_cleanup_owner->last_ipa_temporary = 0;
        }
    }
    for (CallGraphNode* node = ccallgraph; node;) {
        CallGraphNode* next = node->next;
        const OrderRecord* record = ipa_cleanup(node);
        if (record) {
            OrderRecord* owner = record->ipa_cleanup_owner;
            if (node == owner->last_ipa_member)
                owner->last_ipa_member = 0;
            else if (record->ipa_cleanup_kind == TemporaryCleanup && owner->last_ipa_member) {
                CallGraphNode* after = owner->last_ipa_temporary;
                move_graph_node_after(node, after ? after : owner->last_ipa_member);
                owner->last_ipa_temporary = node;
            }
        }
        node = next;
    }
    order_serialized_ipa_functions();
}

static unsigned definition_order(const DeferredExport* entry) {
    const OrderRecord* record = find_order(entry->object, 0);
    return record ? record->definition : 0;
}

static unsigned declaration_kind(const DeferredExport* entry, const Object* referrer) {
    unsigned flags = entry->object->type->flags;
    const OrderRecord* record = find_order(entry->object, 0);
    int direct_destructor = entry->object->datatype == VirtualFunction && (flags & Destructor) &&
                            referrer && record && record->first_referrer == referrer;
    return (flags & (Method | AutoGenerated)) | (direct_destructor ? DirectVirtualDestructor : 0);
}

static DeferredExport* order_definitions(DeferredExport* entry, DeferredExport* end) {
    DeferredExport* ordered = end;
    while (entry != end) {
        DeferredExport* next = entry->next;
        unsigned definition = definition_order(entry);
        DeferredExport** position = &ordered;
        while (*position != end && definition_order(*position) <= definition)
            position = &(*position)->next;
        entry->next = *position;
        *position = entry;
        entry = next;
    }
    return ordered;
}

static void generate_function(FunctionData* data) {
    if (cparamblkptr->precompile) {
        if (data->object)
            CError_Error(0x2908, data->object);
        else
            CError_Error(0x27c4);
        return;
    }

    RC1_generation_needs_statement_pass = 0;
    arguments = data->arguments;
    RC1_locals = data->locals;
    CExpr_IRTreeWalk(data->statements, RC1_RewriteGenerationExpression);
    data->arguments = arguments;
    data->locals = RC1_locals;
    if (RC1_generation_needs_statement_pass) {
        RC1_ProcessGenerationStatements(data->statements);
        if (anyerrors)
            return;
    }

    CIRTrans_TransformC99Complex(data->statements);
    for (const Statement* stmt = data->statements; stmt; stmt = stmt->next) {
        if (stmt->cleanups)
            retro_CollectCleanupReferences(stmt->cleanups);
    }
    if (RC1_filesyminfo)
        CPrep_SetSourceFile(&data->source_location);
    CodeGen_Generator(data);
}

void retro_GenerateFunction(FunctionData* data) {
    if (RC1_ipa_kind) {
        generate_function(data);
        return;
    }
    DeferredExport* previous = RC1_deferred_exports;
    const Object* parent = generating_function;
    generating_function = data->object;
    int constructor = is_constructor(generating_function);
    generate_function(data);
    generating_function = parent;

    /* Definition order is comparable within consecutive functions of the
     * same kind. Outside constructors, preserve the separation between a
     * directly referenced virtual destructor and ordinary calls.
     */
    DeferredExport* entry = RC1_deferred_exports;
    if (!entry || entry == previous || entry->next == previous)
        return;
    for (; entry != previous; entry = entry->next) {
        if (!entry || !definition_order(entry))
            return;
    }
    DeferredExport** link = &RC1_deferred_exports;
    while (*link != previous) {
        DeferredExport* start = *link;
        DeferredExport* end = start->next;
        const Object* referrer = constructor ? 0 : data->object;
        unsigned kind = declaration_kind(start, referrer);
        while (end != previous && declaration_kind(end, referrer) == kind)
            end = end->next;
        *link = order_definitions(start, end);
        while (*link != end)
            link = &(*link)->next;
    }
    if (constructor) {
        order_member_cleanups(&RC1_deferred_exports, previous);
        order_temporary_cleanups(&RC1_deferred_exports, previous);
    }
}

static const void* member_class(const DeferredExport* entry) {
    const FunctionType* type = entry->object->type;
    if (type && type->kind == FunctionTypeKind && (type->flags & Method))
        return type->owner;
    return 0;
}

static const Object* first_class_referrer(const DeferredExport* first, const DeferredExport* end) {
    const OrderRecord* earliest = 0;
    for (const DeferredExport* entry = first; entry != end; entry = entry->next) {
        const OrderRecord* record = find_order(entry->object, 0);
        if (record && record->first_referrer &&
            (!earliest || record->first_reference < earliest->first_reference))
            earliest = record;
    }
    return earliest ? earliest->first_referrer : 0;
}

static DeferredExport* class_group_end(DeferredExport* entry, DeferredExport* boundary) {
    const void* owner = member_class(entry);
    DeferredExport* end = entry->next;
    if (owner)
        while (end != boundary && member_class(end) == owner)
            end = end->next;
    return end;
}

/* Preserve the class visitor's order among instances first used by the same
 * function. A later reference to a different member can come from another
 * function without changing the class's first use.
 */
unsigned char retro_InstantiateTemplates(void) {
    DeferredExport* previous = RC1_deferred_exports;
    unsigned char generated = retro_InstantiateTemplateDefinitions();
    if (RC1_ipa_kind || !has_boundary(RC1_deferred_exports, previous))
        return generated;

    DeferredExport** link = &RC1_deferred_exports;
    while (*link != previous) {
        DeferredExport* first = *link;
        DeferredExport* group = class_group_end(first, previous);
        const Object* referrer = member_class(first) ? first_class_referrer(first, group) : 0;
        if (!referrer) {
            while (*link != group)
                link = &(*link)->next;
            continue;
        }
        DeferredExport* end = group;
        while (end != previous && member_class(end)) {
            DeferredExport* next = class_group_end(end, previous);
            if (first_class_referrer(end, next) != referrer)
                break;
            end = next;
        }

        DeferredExport* ordered = end;
        while (first != end) {
            DeferredExport* next = class_group_end(first, end);
            DeferredExport* last = first;
            while (last->next != next)
                last = last->next;
            last->next = ordered;
            ordered = first;
            first = next;
        }
        *link = ordered;
        while (*link != end)
            link = &(*link)->next;
    }
    return generated;
}

/* A finalizer or template body can enqueue definitions for the next round. */
void retro_RunEndOfFileActions(void) {
    int progress;
    do {
        progress = retro_RunDeclarationActions();
        if (!cparamblkptr->precompile && CClass_Finalize()) {
            CMid_FreeLocalHeap();
            progress = 1;
        }
        if (retro_InstantiateTemplates()) {
            CMid_FreeLocalHeap();
            progress = 1;
        }
    } while (progress && (!anyerrors || RC1_process_deferred_after_errors));
}

static InitExpression* emit_template_initializers(InitExpression* init) {
    Object* object = init->object;
    CMangler_GetLinkName(object);
    Object* guard = CABI_NewGuardVariable(object);
    Statement* stmt = CFunc_AppendStatement(IfGotoStatement);
    stmt->expression = CABI_AcquireGuardVariable(guard, 0);
    CLabel* label = newlabel();
    stmt->label = label;
    do {
        stmt = CFunc_AppendStatement(ExpressionStatement);
        stmt->expression = CInline_CopyExpression(init->expression, 0);
        init = init->next;
    } while (init && init->object == object);
    stmt = CFunc_AppendStatement(ExpressionStatement);
    stmt->expression = CABI_ReleaseGuardVariable(guard, 0);
    stmt = CFunc_AppendStatement(LabelStatement);
    stmt->label = label;
    label->statement = stmt;
    return init;
}

void retro_EmitInitializers(void) {
    if (!init_expressions)
        return;
    for (const InitExpression* init = init_expressions; init; init = init->next)
        retro_RecordDefinitionReferences(0, init->expression);
    while (retro_RunDeferredAction())
        CMid_FreeLocalHeap();

    Statement first_statement;
    NameSpace* scope = CFunc_FuncGenSetup(0, &first_statement, 0, 0);
    InitExpression* init = init_expressions;
    while (init) {
        sourceref = init->location;
        cparser_fileoffset = sourceref;
        if (init == init_expressions) {
            cfunc_funcbodybeginref = cparser_fileoffset;
            cfunc_funcdefnref = cfunc_funcbodybeginref;
        }
        CPrep_MakeDebugSourceRef(&sourceref);
        const ClassTemplateInstance* klass = init->object->scope->theclass;
        if (klass && (klass->flags & ClassIsTemplateInstance)) {
            init = emit_template_initializers(init);
        } else {
            Statement* stmt = CFunc_AppendStatement(ExpressionStatement);
            stmt->expression = CInline_CopyExpression(init->expression, 0);
            init = init->next;
        }
    }
    cfunc_funcbodyendref = cparser_fileoffset;
    cscope_currentfunc = 0;
    CExpr_IRTreeWalk(&first_statement, RC1_RewriteDefinitionExpression);
    CFunc_CodeCleanup(&first_statement);
    memclrw(&cgparams, sizeof(cgparams));
    cgparams.statements = &first_statement;
    cgparams.arguments = arguments;
    cgparams.locals = RC1_locals;
    cgparams.call_on_module_bind = 1;
    CParser_FuncDebugSetup(0, 0, 0);
    if (cmid_in_ipa_mode) {
        if (!anyerrors) {
            InlineBody body;
            COpt_SimpleOptimizer(cgparams.object, cgparams.statements);
            CInline_PackIFunctionData(&cgparams, &body);
            retro_NewInitCodeNode(&body);
            if (!body.code)
                CError_Internal(RC1_CMiddleLayer_source_file, 1108);
            const PackedInlineCode* code = body.code;
            unsigned count = code->reference_count;
            for (unsigned i = 0; i < count; ++i) {
                const PackedReference* reference = &code->references[i];
                if (reference->kind == PackedObjectReference)
                    retro_ObjectAddrRef(reference->object);
            }
        }
    } else {
        CInline_Expand(&cgparams);
        if (!anyerrors)
            retro_GenerateFunction(&cgparams);
    }
    cscope_current = scope->parent;
}
