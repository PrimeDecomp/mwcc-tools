#include <stddef.h>

/* Recovered RC1 CTemplateFuncInst visitors and function-instance generator.
 * Their traversal and callback order feed the deferred function queues.
 * deferred_exports.c retains the compatibility wrappers around these workers.
 */
#pragma pack(push, 2)
typedef struct NameSpace {
    struct NameSpace* parent;
} NameSpace;

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
    unsigned char unused[0x19];
    unsigned flags;
} FunctionType;

typedef struct Object {
    unsigned char unused[2];
    unsigned char datatype;
    unsigned char unused_before_scope[5];
    NameSpace* scope;
    unsigned char unused_before_type[4];
    FunctionType* type;
    unsigned qualifiers;
    unsigned short storage_class;
    unsigned short flags;
} Object;

typedef struct TemplateObject {
    Object object;
    unsigned char unused[0x60-sizeof(Object)];
    const Object* parent;
    unsigned char unused_after_parent[4];
    unsigned char skip_instantiation;
} TemplateObject;

typedef struct ClassInstance {
    unsigned char unused[6];
    const void* scope;
    unsigned char unused_after_scope[0x18];
    unsigned flags;
    unsigned char unused_before_next[0xa];
    struct ClassInstance* next;
    unsigned char unused_before_specialized[0xd];
    unsigned char is_specialized;
    unsigned char is_extern;
} ClassInstance;

typedef struct PartialSpecialization PartialSpecialization;
typedef struct ClassTemplate {
    unsigned char unused[0x30];
    struct ClassTemplate* next;
    unsigned char unused_before_instances[0xe];
    ClassInstance* instances;
    unsigned char unused_before_partials[4];
    PartialSpecialization* partials;
} ClassTemplate;

struct PartialSpecialization {
    PartialSpecialization* next;
    ClassTemplate* templ;
};

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

typedef struct PackedInstanceContext {
    FunctionInstance* instance;
    unsigned char unused[0x22];
} PackedInstanceContext;
#pragma pack(pop)

typedef struct ObjectIterator {
    void* scope;
    void* next_name;
    void* objects;
    int bucket;
} ObjectIterator;

_Static_assert(offsetof(Object, type) == 0x10, "RC1 object type");
_Static_assert(offsetof(Object, scope) == 8, "RC1 object namespace");
_Static_assert(offsetof(Object, qualifiers) == 0x14, "RC1 object qualifiers");
_Static_assert(offsetof(Object, flags) == 0x1a, "RC1 object flags");
_Static_assert(offsetof(FunctionType, flags) == 0x1a, "RC1 function flags");
_Static_assert(offsetof(TemplateObject, parent) == 0x60, "RC1 template member declaration");
_Static_assert(offsetof(TemplateObject, skip_instantiation) == 0x68, "RC1 member instantiation gate");
_Static_assert(offsetof(ClassInstance, scope) == 6, "RC1 class namespace");
_Static_assert(offsetof(ClassInstance, is_extern) == 0x42, "RC1 extern template instance");
_Static_assert(offsetof(ClassInstance, flags) == 0x22, "RC1 template class flags");
_Static_assert(offsetof(ClassInstance, next) == 0x30, "RC1 class instance link");
_Static_assert(offsetof(ClassInstance, is_specialized) == 0x41, "RC1 specialized class instance");
_Static_assert(offsetof(ClassTemplate, next) == 0x30, "RC1 class template link");
_Static_assert(offsetof(ClassTemplate, instances) == 0x42, "RC1 class template instances");
_Static_assert(offsetof(ClassTemplate, partials) == 0x4a, "RC1 class template partial specializations");
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

extern const char RC1_CTemplateFuncInst_source_file[];
extern void CError_Internal(const char* file, int line);
extern void CError_Warning(int error, ...);
extern void CScope_InitObjectIterator(ObjectIterator* iterator, const void* scope);
extern Object* CScope_NextObjectIteratorObject(ObjectIterator* iterator);
extern unsigned char CParser_IsDataObject(const Object* object);
extern const void* CTemplTool_GetTemplateMember(const Object* object);
extern void CTempl_InstantiateMember(void* instance, const void* member,
                                    const Object* declaration, void* point_of_instantiation,
                                    Object* object, unsigned char explicit_instantiation);
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
extern void CError_Error(int error, ...);
extern void CPrep_StreamInsert(TokenStream* stream, int* state);
extern void CPrep_StreamRemove(TokenStream* stream, int* state);
extern void CParser_GetDeclSourceInfo(const SourceLocation* location);
extern unsigned char RC1_nosyminline;
extern unsigned char RC1_filesyminfo;
extern SourceLocation cparser_fileoffset;
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

enum {
    AliasObject = 6,
    FunctionTypeKind = 7,
    Referenced = 2,
    ObjectDefined = 4,
    FunctionDefined = 2,
    AutoGenerated = 0x100,
    Inline = 0x10,
    InlineData = 0x10000,
    TemplateInstance = 0x400000,
    ClassTemplateInstance = 0x800,
    StaticStorageClass = 0x102,
    ImplicitWeak = 0x20000,
    ObjectGraphRoot = 8,
};

unsigned char retro_GenerateMemberInstance(void* instance, Object* object,
                                           unsigned char explicit_instantiation) {
    if (!(object->qualifiers & TemplateInstance))
        return 0;
    const TemplateObject* templated = (const TemplateObject*)object;
    const Object* parent = templated->parent;
    if (!parent)
        CError_Internal(RC1_CTemplateFuncInst_source_file, 1217);
    if (templated->skip_instantiation)
        return 0;
    const void* member = CTemplTool_GetTemplateMember(parent);
    if (!member) {
        if (explicit_instantiation)
            CError_Warning(0x27f9, object);
        return 0;
    }
    if (parent->qualifiers & Inline)
        object->qualifiers |= Inline;
    CTempl_InstantiateMember(instance, member, parent, 0, object, explicit_instantiation);
    return 1;
}

unsigned char retro_GenerateClassMembers(void* instance, unsigned char explicit_instantiation) {
    const ClassInstance* klass = instance;
    if (!explicit_instantiation && klass->is_extern)
        return 0;
    unsigned char generated = 0;
    ObjectIterator iterator;
    CScope_InitObjectIterator(&iterator, klass->scope);
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
