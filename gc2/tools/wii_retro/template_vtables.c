#include <stddef.h>

/* Run CClass_Finalize's template paths at top-level declaration boundaries.
 * RC1 still runs the original finalizer over both lists at end of file.
 */
#pragma pack(push, 2)
typedef struct FunctionType {
    unsigned char kind;
    unsigned char unused[0x19];
    unsigned flags;
} FunctionType;

typedef struct Object {
    unsigned char unused[2];
    unsigned char datatype;
    unsigned char unused_after_datatype[0xd];
    FunctionType* type;
    unsigned qualifiers;
    short storage_class;
    unsigned short flags;
} Object;

typedef struct VTable {
    Object* object;
} VTable;

typedef struct TypeClass {
    unsigned char unused[6];
    void* nspace;
    unsigned char unused_after_namespace[0x10];
    VTable* vtable;
    Object* type_info;
    unsigned flags;
    unsigned char unused_after_flags[9];
    unsigned char action;
} TypeClass;

typedef struct FinalizeEntry {
    struct FinalizeEntry* next;
    TypeClass* klass;
    unsigned char processed;
} FinalizeEntry;

typedef struct TemplateInstance {
    TypeClass klass;
    struct TemplateInstance* next;
    unsigned char unused[0xd];
    unsigned char is_specialized;
} TemplateInstance;

typedef struct ClassTemplate {
    unsigned char unused[0x30];
    struct ClassTemplate* next;
    unsigned char unused_after_next[0xe];
    TemplateInstance* instances;
} ClassTemplate;
#pragma pack(pop)

typedef struct ObjectIterator {
    void* nspace;
    void* next_name;
    void* objects;
    int bucket;
} ObjectIterator;

_Static_assert(offsetof(Object, type) == 0x10, "RC1 Object type");
_Static_assert(offsetof(Object, flags) == 0x1a, "RC1 Object flags");
_Static_assert(offsetof(FunctionType, flags) == 0x1a, "RC1 function flags");
_Static_assert(offsetof(TypeClass, vtable) == 0x1a, "RC1 class vtable");
_Static_assert(offsetof(TypeClass, type_info) == 0x1e, "RC1 class RTTI object");
_Static_assert(offsetof(TypeClass, flags) == 0x22, "RC1 class flags");
_Static_assert(offsetof(TypeClass, action) == 0x2f, "RC1 class action");
_Static_assert(offsetof(FinalizeEntry, processed) == 8, "RC1 finalizer node");
_Static_assert(offsetof(TemplateInstance, next) == 0x30, "RC1 template instance link");
_Static_assert(offsetof(TemplateInstance, is_specialized) == 0x41, "RC1 template instance");
_Static_assert(offsetof(ClassTemplate, instances) == 0x42, "RC1 class template");
_Static_assert(sizeof(ObjectIterator) == 0x10, "RC1 namespace iterator");

enum {
    ClassCompleted = 2,
    ClassTemplateInstance = 0x800,
    ClassSuppressVTable = 0x4000,
    ClassActionNone = 0,
    ClassActionKeyFunction = 1,
    VirtualFunction = 4,
    FunctionKind = 7,
    FunctionDefined = 2,
    KeyFunction = 4,
    Inline = 0x10,
    ImplicitWeak = 0x20000,
    Weak = 0x40000,
    Referenced = 2,
    Defined = 4,
    WeakStaticStorage = 0x102,
};

extern FinalizeEntry* RC1_classlist;
extern FinalizeEntry* RC1_classlist2;
extern ClassTemplate* RC1_templates;
extern unsigned char RC1_frontend_errors;
extern const char RC1_CClass_source_file[];
#ifdef WII_RETRO_RELINK
extern unsigned char retro_RunDeclarationActions(void);
extern unsigned char retro_GenerateClassMembers(void* inst, unsigned char explicit_instantiation);
#define run_declaration_actions retro_RunDeclarationActions
#else
extern unsigned char RC1_GlobalDeclarationActions(void);
#define run_declaration_actions RC1_GlobalDeclarationActions
#endif
extern unsigned char RC1_GenClassInstance(TemplateInstance* inst, unsigned char explicit_instantiation);
extern void RC1_FreeLocalHeap(void);
extern void RC1_ConstructVTable(TypeClass* klass, TypeClass* base, int offset,
                               Object* object, unsigned char emit);
extern void RC1_InitObjectIterator(ObjectIterator* iterator, void* nspace);
extern Object* RC1_NextObjectIteratorObject(ObjectIterator* iterator);
extern void RC1_GenerateTypeInfoData(Object* object, TypeClass* klass, unsigned char flag);
extern _Noreturn void RC1_ClassInternalError(const char* source_file, int line);

#ifdef WII_RETRO_RELINK
extern void retro_BeginTemplateDeclaration(void);
extern int retro_DelayTemplateVTable(const void* object);
extern unsigned char retro_RunDeferredAction(void);
static int processing_declaration;
#endif

static int finalize_early(const TypeClass* klass) {
    if (!(klass->flags & ClassTemplateInstance))
        return 0;
#ifdef WII_RETRO_RELINK
    if (klass->vtable && retro_DelayTemplateVTable(klass->vtable->object))
        return 0;
#endif
    return 1;
}

static int referenced_undefined(const Object* object) {
    return object && (object->flags & (Defined | Referenced)) == Referenced;
}

static int can_emit_vtable(const TypeClass* klass) {
    return klass->vtable && !(klass->flags & ClassSuppressVTable);
}

static int needs_weak_vtable(const TypeClass* klass) {
    return klass->action == ClassActionNone && can_emit_vtable(klass) &&
           referenced_undefined(klass->vtable->object);
}

/* CClass_Finalize's key-function path at 0x552D70. An inline definition
 * demotes the key function and lets the referenced-weak path run.
 */
static int finalize_key_function(TypeClass* klass) {
    ObjectIterator iterator;
    RC1_InitObjectIterator(&iterator, klass->nspace);
    for (Object* method = RC1_NextObjectIteratorObject(&iterator); method;
         method = RC1_NextObjectIteratorObject(&iterator)) {
        FunctionType* type = method->type;
        if (method->datatype != VirtualFunction || type->kind != FunctionKind ||
            !(type->flags & KeyFunction))
            continue;
        if (method->qualifiers & Inline) {
            type->flags &= ~KeyFunction;
            klass->action = ClassActionNone;
            return 0;
        }
        if (!(type->flags & FunctionDefined) || !can_emit_vtable(klass))
            return 0;
        if (klass->vtable->object->flags & Defined)
            RC1_ClassInternalError(RC1_CClass_source_file, 0xe28);
        RC1_ConstructVTable(klass, 0, 0, klass->vtable->object, 1);
        return 1;
    }
    RC1_ClassInternalError(RC1_CClass_source_file, 0xe13);
}

/* CClass_Finalize at 0x552C20. Visit the original lists in place so skipped
 * classes keep their positions and there is no size limit on the lists.
 */
static int finalize_templates(void) {
    if (RC1_frontend_errors)
        return 0;
    int progress = 0;
    for (FinalizeEntry* node = RC1_classlist; node; node = node->next) {
        TypeClass* klass = node->klass;
        if (!finalize_early(klass))
            continue;
        if (!node->processed) {
            if (klass->action == ClassActionKeyFunction)
                progress |= finalize_key_function(klass);
            node->processed = 1;
        }
        if (needs_weak_vtable(klass)) {
            Object* table = klass->vtable->object;
            table->storage_class = WeakStaticStorage;
            table->qualifiers |= ImplicitWeak;
            RC1_ConstructVTable(klass, 0, 0, table, 1);
            progress = 1;
        }
        if (referenced_undefined(klass->type_info)) {
            klass->type_info->storage_class = WeakStaticStorage;
            klass->type_info->qualifiers |= ImplicitWeak;
            RC1_GenerateTypeInfoData(klass->type_info, klass, 0);
            progress = 1;
        }
    }
    for (FinalizeEntry* node = RC1_classlist2; node; node = node->next) {
        TypeClass* klass = node->klass;
        if (!finalize_early(klass) || node->processed || (klass->flags & ClassCompleted) ||
            !referenced_undefined(klass->type_info))
            continue;
        klass->type_info->qualifiers |= ImplicitWeak | Weak;
        RC1_GenerateTypeInfoData(klass->type_info, klass, 0);
        node->processed = 1;
        progress = 1;
    }
    return progress;
}

static int instantiate_polymorphic(void) {
    int progress = 0;
    for (ClassTemplate* templ = RC1_templates; templ; templ = templ->next) {
        for (TemplateInstance* inst = templ->instances; inst; inst = inst->next) {
            if (!(inst->klass.flags & ClassTemplateInstance) || inst->is_specialized || !inst->klass.vtable)
                continue;
#ifdef WII_RETRO_RELINK
            progress |= retro_GenerateClassMembers(inst, 0);
#else
            progress |= RC1_GenClassInstance(inst, 0);
#endif
            RC1_FreeLocalHeap();
        }
    }
    return progress;
}

#ifdef WII_RETRO_RELINK
void retro_ResetTemplateVTableActions(void) {
    processing_declaration = 0;
}

/* A deferred work item triggers a pass only when a weak table has become
 * referenced. The declaration boundary runs the pass unconditionally.
 */
static int has_pending_template_vtable(void) {
    for (const FinalizeEntry* node = RC1_classlist; node; node = node->next) {
        if (finalize_early(node->klass) && needs_weak_vtable(node->klass))
            return 1;
    }
    return 0;
}

unsigned char retro_DeclarationWorkItem(void) {
    unsigned char ran = retro_RunDeferredAction();
    /* A deferred factory can reference a new table while generating its body. */
    if (ran && processing_declaration && !RC1_frontend_errors &&
        has_pending_template_vtable() && finalize_templates()) {
        instantiate_polymorphic();
    }
    return ran;
}
#endif

int retro_GlobalDeclarationActions(void) {
#ifdef WII_RETRO_RELINK
    int previous = processing_declaration;
    processing_declaration = 1;
    retro_BeginTemplateDeclaration();
#endif
    int r = run_declaration_actions();
    for (;;) {
        int progress = finalize_templates();
        progress |= instantiate_polymorphic();
        if (!progress)
            break;
        run_declaration_actions();
    }
#ifdef WII_RETRO_RELINK
    processing_declaration = previous;
#endif
    return r;
}
