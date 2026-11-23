#ifndef COMPILER_CIRSTREAMCODE_H
#define COMPILER_CIRSTREAMCODE_H

#include "compiler/CIRStreamCallGraph.h"
#include "compiler/enode.h"

typedef struct CIRStreamSourceLocation {
    UInt32 fileID;
    UInt32 offset;
    UInt32 line;
} CIRStreamSourceLocation;

typedef struct CIRStreamCodeInfo {
    struct CIRStreamCodeInfo* next;
    Statement* statement;
    ENode* operand;
    Type* type;
    UInt32 flags;
    UInt8 cost;
    UInt8 kind;
} CIRStreamCodeInfo;

typedef struct CIRStreamStatementLocation {
    struct CIRStreamStatementLocation* next;
    const Statement* statement;
    CIRStreamSourceLocation location;
} CIRStreamStatementLocation;

typedef struct CIRStreamLocalInfo {
    struct CIRStreamLocalInfo* next;
    const Object* object;
    UInt32 fileID;
    UInt8 extraStorageFlags;
    Boolean hasSourceType;
} CIRStreamLocalInfo;

typedef struct CIRStreamExpressionInfo {
    struct CIRStreamExpressionInfo* next;
    ENode* expression;
    UInt32 extraFlags;
    UInt8 stringKind;
} CIRStreamExpressionInfo;

typedef enum CIRStreamAssemblyPatchKind {
    CIRStreamAssemblyObject,
    CIRStreamAssemblyLabel,
    CIRStreamAssemblyType
} CIRStreamAssemblyPatchKind;

typedef struct CIRStreamAssemblyPatch {
    struct CIRStreamAssemblyPatch* next;
    UInt32 offset;
    CIRStreamAssemblyPatchKind kind;
    union {
        Object* object;
        CLabel* label;
        Type* type;
    } value;
} CIRStreamAssemblyPatch;

typedef struct CIRStreamAssembly {
    struct CIRStreamAssembly* next;
    const Statement* statement;
    CIRStreamReader instruction;
    CIRStreamAssemblyPatch* patches;
} CIRStreamAssembly;

typedef struct CIRStreamFunctionCode {
    Statement* statements;
    ObjectList* arguments;
    ObjectList* locals;
    CIRStreamCodeInfo* info;
    CIRStreamStatementLocation* sourceLocations;
    CIRStreamLocalInfo* localInfo;
    CIRStreamExpressionInfo* expressionInfo;
    // Stock assembly bytes require target-specific translation before codegen.
    // ST_ASM statements in imported bodies use this list instead of expr.
    CIRStreamAssembly* assembly;
    CIRStreamSourceLocation location[3];
    UInt8 oldStyleParameters;
    UInt8 callOnModuleBind;
} CIRStreamFunctionCode;

Boolean CIRStream_DecodeExpressionKind(UInt8 tag, ENodeType* type);

// Decode an ordinary function definition. The caller may supply a graph-indexed
// object table after resolving external symbols and implicit runtime objects.
Boolean CIRStream_UnpackCode(
    const CIRStreamImportedGraph* graph,
    UInt32 nodeID,
    Object* const* objects,
    CIRStreamFunctionCode* code
);
Boolean CIRStream_UnpackPrecompiledCode(
    Object* function,
    NameSpace* root,
    Boolean pairedSingles,
    const CIRStreamFunctionDefinition* definition,
    HashNameNode* const* fileNames,
    CIRStreamFunctionCode* result
);
Boolean CIRStream_TranslateAssembly(CIRStreamFunctionCode* code, Boolean functionAssembly);

// Generate the body and its references from statements and objects. The input
// definition supplies backend options, the inline-size estimate, and serialized
// file references. Consult the graph's objects for global symbol identities.
// On success the definition points into the output buffer and compiler heap.
Boolean CIRStream_PackCode(
    CIRStreamWriter* stream,
    const CIRStreamImportedGraph* graph,
    const CIRStreamFunctionCode* code,
    CIRStreamFunctionDefinition* definition
);

#endif
