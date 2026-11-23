#ifndef COMPILER_CCALLGRAPH_H
#define COMPILER_CCALLGRAPH_H

#include "compiler/CInline.h"
#include "compiler/CIRStream.h"

typedef struct CallGraphNode CallGraphNode;
typedef struct CallGraphEdge CallGraphEdge;

typedef enum CallGraphNodeKind {
    CGFunction = 1,
    CGThunk,
    CGData
} CallGraphNodeKind;

struct CallGraphEdge {
    CallGraphEdge* next;
    CallGraphNode* node;
    UInt32 callCount;
    UInt8 flags;
};

struct CallGraphNode {
    CallGraphNode* next;
    CallGraphNode* hashNext;
    Object* object;
    CallGraphEdge* uses;
    CallGraphEdge* usedBy;
    UInt32 id;
    CallGraphNodeKind kind;
    Boolean marked;
    UInt8 exceptionFlags;
    Boolean root;
    Boolean forceActive;
    Boolean initializer;
    const struct FSSpec* sourceFile;
    union {
        struct {
            CI_FuncData* body;
            PackedReferences references;
            UInt8 oldStyleParameters;
        } function;
        struct {
            Object* target;
            SInt32 thisDelta;
            SInt32 returnDelta;
            SInt32 ctorOffset;
        } thunk;
        struct {
            const void* bytes;
            OLinkList* references;
            SInt32 size;
            UInt32 qualifiers;
            FileOffsetInfo file;
            Boolean readOnly;
            Boolean filesyminfo;
        } data;
    } definition;
};

extern CallGraphNode* ccallgraph;
extern UInt32 ccallgraph_node_count;

void CCallGraph_Setup(void);
Boolean CCallGraph_IsDefined(Object* object);
CallGraphNode* CCallGraph_NewNode(Object* object, CallGraphNodeKind kind);
CallGraphNode* CCallGraph_NewInitCodeNode(void);
void CCallGraph_Unmark(void);
void CCallGraph_ColorUses(CallGraphNode* root);
void CCallGraph_Compute(void);
void CCallGraph_Renumber(void);
void CCallGraph_ResolveReferences(void);
void CCallGraph_RemoveDeadNodes(void);

#endif
