#include "compiler/CIRStream.h"
#include "compiler/CInline.h"
#include "compiler/CompilerTools.h"
#include "compiler/objects.h"

typedef struct ReferencePatch {
    struct ReferencePatch* next;
    PackedReference reference;
} ReferencePatch;

static ReferencePatch* cirstream_patches;
static UInt32 cirstream_patchid;

// Collect the object portion of the packed body table while bodies still use CI_FuncData.
static void CIRStream_PackObjectReference(Object* object, Boolean isCall) {
    ReferencePatch* patch;

    while (object && object->datatype == DALIAS) {
        if (object->u.alias.offset) {
            isCall = FALSE;
        }
        object = object->u.alias.object;
    }
    if (!object || object->datatype == DLOCAL) {
        return;
    }

    for (patch = cirstream_patches; patch; patch = patch->next) {
        if (patch->reference.object == object) {
            break;
        }
    }
    if (!patch) {
        patch = lalloc(sizeof(ReferencePatch));
        patch->next = cirstream_patches;
        patch->reference.object = object;
        patch->reference.kind = CIRStream_ObjectReference;
        patch->reference.flags = 0;
        patch->reference.callCount = 0;
        cirstream_patches = patch;
        ++cirstream_patchid;
    }
    if (!isCall) {
        patch->reference.flags |= CIRStream_AddressTaken;
    } else if (patch->reference.callCount != 0xffff) {
        ++patch->reference.callCount;
    }
}

void CIRStream_PackReferences(CI_FuncData* body, PackedReferences* references) {
    ReferencePatch* patch;
    UInt32 index;

    cirstream_patches = NULL;
    cirstream_patchid = 0;
    references->opaque = !CInline_WalkFunctionReferences(body, CIRStream_PackObjectReference);
    references->count = index = cirstream_patchid;
    references->entries = index ? galloc(index * sizeof(PackedReference)) : NULL;
    for (patch = cirstream_patches; patch; patch = patch->next) {
        references->entries[--index] = patch->reference;
    }
    cirstream_patches = NULL;
}
