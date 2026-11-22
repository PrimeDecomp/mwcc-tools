#ifndef COMPILER_CIRSTREAMCALLGRAPH_H
#define COMPILER_CIRSTREAMCALLGRAPH_H

#include "compiler/CIRStreamType.h"

enum {
    CIRStreamBackendHeaderSize = 36,
    CIRStreamBackendSectionSize = 124,
    CIRStreamBackendSectionAttributeSize = 24,
    CIRStreamBackendInterruptSize = 44,
    CIRStreamBackendDefaultsSize = 48,
    CIRStreamBackendFileSize = 16,
    CIRStreamFunctionOptionsSize = 208,
    CIRStreamThunkQualifier = 0x800000
};

typedef struct CIRStreamGraphHeader {
    UInt32 typeCount;
    UInt8 complexUsed;
    CIRStreamReader mainFile;
    CIRStreamReader mainFileName;
    UInt32 fileCount;
    CIRStreamReader backend;
    UInt32 nodeCount;
    CIRStreamReader nodes;
} CIRStreamGraphHeader;

typedef struct CIRStreamBackendHeader {
    UInt32 flags;
    CIRStreamReader sections;
    CIRStreamReader sectionAttributes;
    CIRStreamReader interrupts;
    CIRStreamReader defaultSections;
    CIRStreamReader strings;
    CIRStreamReader files;
} CIRStreamBackendHeader;

typedef enum CIRStreamSectionRole {
    CIRStreamCodeSection,
    CIRStreamDataSection,
    CIRStreamConstSection,
    CIRStreamSmallConstSection,
    CIRStreamSmallDataSection,
    CIRStreamSectionRoleCount
} CIRStreamSectionRole;

typedef struct CIRStreamSection {
    const char* name;
    const char* linkedName;
    UInt32 permissions;
    UInt8 dataMode;
    UInt8 codeMode;
} CIRStreamSection;

typedef struct CIRStreamNodeHeader {
    UInt32 implicitObject;
    Boolean root;
    UInt8 access;
    UInt8 datatype;
    SInt32 section;
} CIRStreamNodeHeader;

typedef struct CIRStreamObjectDecl {
    CIRStreamNodeHeader node;
    NameSpace* nspace;
    HashNameNode* name;
    Type* type;
    UInt32 qualifiers;
    UInt16 storageClass;
    UInt16 flags;
} CIRStreamObjectDecl;

typedef struct CIRStreamFunctionHeader {
    UInt32 inlineSize;
    CIRStreamReader options;
    CIRStreamReader code;
    UInt32 localCount;
    UInt32 labelCount;
    UInt32 typeCount;
    UInt32 referenceCount;
} CIRStreamFunctionHeader;

typedef enum CIRStreamReferenceKind {
    CIRStreamRefType,
    CIRStreamRefObject,
    CIRStreamRefFile,
    CIRStreamRefSourceType,
    CIRStreamRefName
} CIRStreamReferenceKind;

typedef struct CIRStreamReferenceHeader {
    UInt16 callCount;
    UInt8 flags;
    UInt8 kind;
} CIRStreamReferenceHeader;

typedef struct CIRStreamReference {
    CIRStreamReferenceHeader header;
    union {
        Type* type;
        Object* object;
        UInt32 objectID;
        UInt32 fileID;
        HashNameNode* name;
    } value;
    CIRStreamReader fileRecord;
} CIRStreamReference;

typedef struct CIRStreamFunctionDefinition {
    CIRStreamFunctionHeader header;
    CIRStreamReference* references;
} CIRStreamFunctionDefinition;

typedef struct CIRStreamRelocation {
    UInt32 objectID;
    SInt32 offset;
    SInt32 addend;
} CIRStreamRelocation;

typedef struct CIRStreamDataDefinition {
    UInt32 size;
    UInt32 fileID;
    UInt32 location[2];
    CIRStreamReader fileRecord;
    CIRStreamReader initializer;
    UInt32 relocationCount;
    CIRStreamRelocation* relocations;
    UInt8 flags;
} CIRStreamDataDefinition;

typedef enum CIRStreamNodeKind {
    CIRStreamImplicitNode,
    CIRStreamFunctionNode,
    CIRStreamDataNode,
    CIRStreamIntrinsicNode,
    CIRStreamThunkNode,
    CIRStreamAbsoluteNode,
    CIRStreamAssemblyNode
} CIRStreamNodeKind;

typedef struct CIRStreamNode {
    CIRStreamObjectDecl decl;
    Object* object;
    HashNameNode* linkName;
    CIRStreamNodeKind kind;
    UInt16 attributes;
    UInt8 extra;
    union {
        CIRStreamFunctionDefinition function;
        CIRStreamDataDefinition data;
        struct {
            UInt32 targetID;
            SInt32 thisDelta;
            SInt32 returnDelta;
            SInt32 ctorOffset;
        } thunk;
        SInt32 intrinsic;
        UInt32 address;
        CIRStreamReader assembly;
    } definition;
} CIRStreamNode;

typedef struct CIRStreamImportedGraph {
    CIRStreamGraphHeader header;
    CIRStreamBackendHeader backend;
    CIRStreamNode* nodes;
    HashNameNode** fileNames;
    UInt32 fileCount;
    NameSpace* root;
    Boolean pairedSingles;
    CIRStreamTypeDetails* wireTypes;
} CIRStreamImportedGraph;

// Pack the envelope around serialized file, backend, and node records.
Boolean CIRStream_PackCallGraph(CIRStreamWriter* stream, const CIRStreamGraphHeader* graph);
Boolean CIRStream_CheckHeader(const UInt8* bytes, UInt32 size);
Boolean CIRStream_UnpackCallGraphHeader(
    const UInt8* bytes, UInt32 size, CIRStreamGraphHeader* header
);
Boolean CIRStream_PackSourceFile(
    CIRStreamWriter* stream,
    const HashNameNode* path,
    const HashNameNode* include,
    UInt32 timeLow,
    UInt32 timeHigh,
    Boolean systemInclude
);
Boolean CIRStream_UnpackBackendHeader(CIRStreamReader backend, CIRStreamBackendHeader* header);
const char* CIRStream_BackendString(const CIRStreamBackendHeader* backend, UInt32 offset);
Boolean CIRStream_ResolveSection(
    const CIRStreamBackendHeader* backend,
    SInt32 selector,
    CIRStreamSectionRole role,
    CIRStreamSection* section
);
Boolean CIRStream_UnpackNodeHeader(CIRStreamReader* stream, CIRStreamNodeHeader* header);
Boolean CIRStream_UnpackObjectDecl(
    CIRStreamReader* stream, CIRStreamTypeContext* context, CIRStreamObjectDecl* decl
);
Boolean CIRStream_UnpackFunctionHeader(CIRStreamReader* stream, CIRStreamFunctionHeader* header);
// Restore the RC1 scalar options used by the source backend. The caller supplies
// defaults for options and context-specific references outside this mapping.
Boolean CIRStream_UnpackFunctionOptions(
    const CIRStreamFunctionHeader* header, struct CInlineBackendOptions* options
);
Boolean CIRStream_PackFunctionOptions(
    CIRStreamWriter* stream,
    const struct CInlineBackendOptions* options,
    UInt8 canInline,
    Boolean initializer
);
Boolean CIRStream_UnpackReferenceHeader(CIRStreamReader* stream, CIRStreamReferenceHeader* header);
// Decode a single translation unit using a fresh type context. Code and initializer
// spans refer to the input. The caller discards the context if decoding fails.
Boolean CIRStream_ImportCallGraph(
    const UInt8* bytes,
    UInt32 size,
    NameSpace* root,
    Boolean pairedSingles,
    CIRStreamImportedGraph* graph
);
// Re-encode the graph from its declarations and definitions. The backend,
// source file records and packed function bodies supply their serialized bytes.
Boolean CIRStream_ExportCallGraph(CIRStreamWriter* stream, const CIRStreamImportedGraph* graph);
UInt32 CIRStream_ImplicitObjectID(const Object* object);

#endif
