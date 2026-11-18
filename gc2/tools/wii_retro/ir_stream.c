#include <stddef.h>

/* Wii 1.0RC1's packed body and object-reference table. Deferred function
 * discovery follows this table in index order, so preserve first encounter
 * order even though the temporary patch list prepends its entries.
 */
typedef struct Object {
    unsigned char unused[2];
    unsigned char datatype;
} Object;

typedef struct ObjectList {
    const struct ObjectList* next;
    Object* object;
} ObjectList;

#pragma pack(push, 2)
typedef struct SourceLocation {
    const void* file;
    unsigned words[2];
} SourceLocation;

typedef struct FunctionData {
    const void* statements;
    Object* object;
    signed char old_style_parameters;
    signed char call_on_module_bind;
    const ObjectList* arguments;
    const ObjectList* locals;
    SourceLocation locations[3];
} FunctionData;

typedef struct PackedReference {
    Object* object;
    unsigned char kind;
    unsigned char flags;
    unsigned short reference_count;
} PackedReference;

typedef struct PackedCode {
    void* data;
    unsigned size;
    unsigned local_count;
    unsigned label_count;
    unsigned type_count;
    unsigned reference_count;
    unsigned char owns_data;
    unsigned char unused;
    PackedReference references[];
} PackedCode;

typedef struct ReferencePatch {
    struct ReferencePatch* next;
    PackedReference reference;
    unsigned index;
} ReferencePatch;
#pragma pack(pop)

typedef struct LocalReference {
    const struct LocalReference* next;
    Object* object;
    unsigned index;
} LocalReference;

typedef struct StreamBuffer {
    const void* const* data;
    unsigned size;
} StreamBuffer;

_Static_assert(sizeof(FunctionData) == 0x36, "RC1 code generation parameters");
_Static_assert(sizeof(SourceLocation) == 0xc, "RC1 source location");
_Static_assert(offsetof(FunctionData, arguments) == 0xa, "RC1 argument list");
_Static_assert(offsetof(FunctionData, locals) == 0xe, "RC1 local list");
_Static_assert(offsetof(FunctionData, locations) == 0x12, "RC1 source locations");
_Static_assert(sizeof(PackedReference) == 0x8, "RC1 packed reference");
_Static_assert(offsetof(PackedReference, kind) == 0x4, "RC1 packed reference kind");
_Static_assert(offsetof(PackedReference, flags) == 0x5, "RC1 packed reference flags");
_Static_assert(offsetof(PackedReference, reference_count) == 0x6, "RC1 packed reference count");
_Static_assert(offsetof(PackedCode, references) == 0x1a, "RC1 packed reference table");
_Static_assert(offsetof(PackedCode, reference_count) == 0x14, "RC1 reference table size");
_Static_assert(offsetof(PackedCode, owns_data) == 0x18, "RC1 packed stream ownership");
_Static_assert(sizeof(ReferencePatch) == 0x10, "RC1 reference patch");
_Static_assert(offsetof(ReferencePatch, reference) == 0x4, "RC1 reference patch value");
_Static_assert(offsetof(ReferencePatch, index) == 0xc, "RC1 reference patch index");
_Static_assert(sizeof(LocalReference) == 0xc, "RC1 local reference");

extern ReferencePatch* cirstream_patches;
extern unsigned cirstream_patchid;
extern StreamBuffer cirstream_glist;
extern unsigned cirstream_typeid;
extern unsigned RC1_ir_local_count;
extern unsigned RC1_ir_label_count;
extern const LocalReference* RC1_ir_local_references;
extern Object* const* RC1_ir_local_objects;
extern const PackedCode* RC1_ir_unpack_code;
extern unsigned char RC1_ir_pack_flag;
extern const char RC1_CIRStream_source_file[];

extern void* lalloc(unsigned size);
extern void* CTool_Alloc(unsigned size);
extern void memclrw(void* data, unsigned size);
extern void* memcpy(void* destination, const void* source, unsigned size);
extern void CError_Internal(const char* file, int line);
extern void CIRStream_InitGlobals(void);
extern void CIRStream_PackByte(signed char value);
extern void CIRStream_PackUInt32(unsigned value);
extern const unsigned char* CIRStream_UnpackUInt32(const unsigned char* code, unsigned* value);
extern void CIRStream_PackData(const void* data, unsigned size);
extern void RC1_PackFileReference(const void* file);
extern void RC1_PackObjectList(const ObjectList* objects);
extern void RC1_PackStatements(const void* statements);

enum {
    DataObject = 0,
    LocalObject = 1,
    AbsoluteObject = 2,
    OrdinaryFunction = 3,
    VirtualFunction = 4,
    InlineFunctionObject = 5,
    PackedObjectReference = 1,
};

void retro_PackObjectReference(Object* object, unsigned char is_call, unsigned char is_address) {
    if (!object) {
        CIRStream_PackByte(0);
        return;
    }
    switch (object->datatype) {
        case DataObject:
        case AbsoluteObject:
        case OrdinaryFunction:
        case VirtualFunction:
        case InlineFunctionObject: {
            CIRStream_PackByte(1);
            ReferencePatch* patch = cirstream_patches;
            for (;;) {
                if (!patch) {
                    patch = lalloc(sizeof(*patch));
                    memclrw(patch, sizeof(*patch));
                    patch->next = cirstream_patches;
                    patch->index = cirstream_patchid++;
                    patch->reference.kind = PackedObjectReference;
                    patch->reference.object = object;
                    cirstream_patches = patch;
                    break;
                }
                if (patch->reference.object == object && patch->reference.kind == PackedObjectReference)
                    break;
                patch = patch->next;
            }
            if (is_call && patch->reference.reference_count != 0xffff)
                ++patch->reference.reference_count;
            if (is_address)
                patch->reference.flags |= 1;
            CIRStream_PackUInt32(patch->index);
            break;
        }
        case LocalObject: {
            CIRStream_PackByte(2);
            const LocalReference* local = RC1_ir_local_references;
            for (;;) {
                if (!local)
                    CError_Internal(RC1_CIRStream_source_file, 865);
                if (local->object == object)
                    break;
                local = local->next;
            }
            CIRStream_PackUInt32(local->index);
            break;
        }
        default:
            CError_Internal(RC1_CIRStream_source_file, 875);
            break;
    }
}

const unsigned char* retro_UnpackObjectReference(const unsigned char* code, Object** object) {
    unsigned index;
    switch (*(const signed char*)code++) {
        case 0:
            *object = 0;
            break;
        case 1:
            code = CIRStream_UnpackUInt32(code, &index);
            /* Retain RC1's inclusive comparison and its internal checks. */
            if (index > RC1_ir_unpack_code->reference_count ||
                RC1_ir_unpack_code->references[index].kind != PackedObjectReference)
                CError_Internal(RC1_CIRStream_source_file, 897);
            *object = RC1_ir_unpack_code->references[index].object;
            break;
        case 2:
            code = CIRStream_UnpackUInt32(code, &index);
            if (index >= RC1_ir_local_count || !RC1_ir_local_objects[index])
                CError_Internal(RC1_CIRStream_source_file, 903);
            *object = RC1_ir_local_objects[index];
            break;
        default:
            CError_Internal(RC1_CIRStream_source_file, 908);
            break;
    }
    return code;
}

const PackedCode* retro_PackCode(const FunctionData* data) {
    CIRStream_InitGlobals();
    RC1_ir_pack_flag = 0;
    RC1_PackFileReference(data->locations[0].file);
    CIRStream_PackData(data->locations[0].words, sizeof(data->locations[0].words));
    RC1_PackFileReference(data->locations[1].file);
    CIRStream_PackData(data->locations[1].words, sizeof(data->locations[1].words));
    RC1_PackFileReference(data->locations[2].file);
    CIRStream_PackData(data->locations[2].words, sizeof(data->locations[2].words));
    RC1_PackObjectList(data->arguments);
    RC1_PackObjectList(data->locals);
    CIRStream_PackByte(data->old_style_parameters);
    CIRStream_PackByte(data->call_on_module_bind);
    RC1_PackStatements(data->statements);

    PackedCode* code = CTool_Alloc(offsetof(PackedCode, references) +
                                  cirstream_patchid * sizeof(PackedReference));
    code->data = CTool_Alloc(cirstream_glist.size);
    code->size = cirstream_glist.size;
    code->local_count = RC1_ir_local_count;
    code->label_count = RC1_ir_label_count;
    code->reference_count = cirstream_patchid;
    code->type_count = cirstream_typeid - 0x3f;
    code->owns_data = 1;
    memcpy(code->data, *cirstream_glist.data, code->size);
    for (const ReferencePatch* patch = cirstream_patches; patch; patch = patch->next)
        code->references[patch->index] = patch->reference;
    return code;
}
