#ifndef COMPILER_CIRSTREAMTYPE_H
#define COMPILER_CIRSTREAMTYPE_H

#include "compiler/CIRStreamTools.h"

typedef struct CIRStreamNamespace CIRStreamNamespace;
typedef struct CIRStreamTypeEntry CIRStreamTypeEntry;
typedef struct CIRStreamTypeDetails CIRStreamTypeDetails;

// The type table belongs to one translation unit. A function body can also use
// type patches from its packed reference table.
typedef struct CIRStreamTypeContext {
    Type** types;
    UInt32 typeCapacity;
    UInt32 typeCount;
    Type** typePatches;
    UInt32 typePatchCount;
    CIRStreamNamespace* namespaces;
    UInt32 namespaceCount;
    NameSpace* root;
    Boolean pairedSingles;
    Boolean keepWireDetails;
    CIRStreamTypeDetails* wireDetails;
} CIRStreamTypeContext;

Boolean CIRStream_UnpackType(CIRStreamReader* stream, CIRStreamTypeContext* context, Type** type);
Boolean CIRStream_UnpackNameSpace(
    CIRStreamReader* stream, CIRStreamTypeContext* context, NameSpace** nspace
);
Boolean CIRStream_UnpackName(CIRStreamReader* stream, HashNameNode** name);

typedef struct CIRStreamTypeWriter {
    CIRStreamTypeEntry** types;
    UInt32 typeCount;
    CIRStreamNamespace* namespaces;
    UInt32 namespaceCount;
    NameSpace* root;
    Boolean pairedSingles;
    const CIRStreamTypeDetails* wireDetails;
    Boolean (*patchType)(void* context, Type* type, UInt32* index);
    void* patchContext;
} CIRStreamTypeWriter;

// Initialize the writer context to zero before setting its root and target.
// Its tables live on the local heap. Discard the stream and context if packing fails.
// Set patchType when writing a function body to register non-builtin types as references.
Boolean CIRStream_PackType(CIRStreamWriter* stream, CIRStreamTypeWriter* context, Type* type);
Boolean CIRStream_PackNameSpace(
    CIRStreamWriter* stream, CIRStreamTypeWriter* context, NameSpace* nspace
);
Boolean CIRStream_PackTypeHashNameNode(CIRStreamWriter* stream, const HashNameNode* name);

#endif
