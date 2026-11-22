#ifndef COMPILER_CIRSTREAM_H
#define COMPILER_CIRSTREAM_H

#include "compiler/common.h"

enum {
    CIRStream_ObjectReference = 1,
    CIRStream_AddressTaken = 1
};

typedef struct PackedReference {
    Object* object;
    UInt8 kind;
    UInt8 flags;
    UInt16 callCount;
} PackedReference;

typedef struct PackedReferences {
    PackedReference* entries;
    UInt32 count;
    Boolean opaque;
} PackedReferences;

void CIRStream_PackReferences(CI_FuncData* body, PackedReferences* references);

#endif
