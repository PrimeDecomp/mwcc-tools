#include "compiler/CCallGraph.h"
#include "compiler/CError.h"
#include "compiler/CInit.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CScope.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/tokens.h"
#include "compiler/types.h"

enum {
    GraphBuckets = 1024
};

CallGraphNode* ccallgraph;
UInt32 ccallgraph_node_count;
static CallGraphNode* graphTail;
static CallGraphNode* graphNodes[GraphBuckets];

void CCallGraph_Setup(void) {
    ccallgraph = graphTail = NULL;
    ccallgraph_node_count = 0;
    memclrw(graphNodes, sizeof(graphNodes));
}

static unsigned graph_bucket(const Object* object) {
    return ((unsigned long)object >> 2) % GraphBuckets;
}

static CallGraphNode* find_node(Object* object) {
    CallGraphNode* node;

    if (!object) {
        return NULL;
    }
    for (node = graphNodes[graph_bucket(object)]; node; node = node->hashNext) {
        if (node->object == object) {
            return node;
        }
    }
    return NULL;
}

Boolean CCallGraph_IsDefined(Object* object) {
    return find_node(object) != NULL;
}

CallGraphNode* CCallGraph_NewNode(Object* object, CallGraphNodeKind kind) {
    CallGraphNode* node = find_node(object);
    unsigned bucket;
    if (node) {
        CError_ASSERT(365, node->kind == kind);
        return node;
    }

    node = galloc(sizeof(CallGraphNode));
    memclrw(node, sizeof(CallGraphNode));
    node->object = object;
    node->kind = kind;
    node->forceActive = copts.force_active;
    node->root = node->forceActive || (object->flags & OBJECT_EXPORT);

    bucket = graph_bucket(object);
    node->hashNext = graphNodes[bucket];
    graphNodes[bucket] = node;
    if (graphTail) {
        graphTail->next = node;
    } else {
        ccallgraph = node;
    }
    graphTail = node;
    return node;
}

CallGraphNode* CCallGraph_NewInitCodeNode(void) {
    Object* object = CParser_NewCompilerDefFunctionObject();
    CallGraphNode* node;
    object->nspace = cscope_root;
    object->name = CParser_GetUniqueName();
    object->type = TYPE(&rt_func);

    node = CCallGraph_NewNode(object, CGFunction);
    node->root = node->initializer = TRUE;
    return node;
}

void CCallGraph_Unmark(void) {
    CallGraphNode* node;
    for (node = ccallgraph; node; node = node->next) {
        node->marked = FALSE;
    }
}

static void add_reference(CallGraphNode* source, Object* object, UInt32 callCount, UInt8 flags) {
    CallGraphNode* target;
    CallGraphEdge* edge;
    CallGraphEdge* reverse;

    while (object && object->datatype == DALIAS) {
        object = object->u.alias.object;
    }
    target = find_node(object);
    if (!target) {
        return;
    }
    for (edge = source->uses; edge; edge = edge->next) {
        if (edge->node == target) {
            return;
        }
    }
    edge = galloc(sizeof(CallGraphEdge));
    edge->node = target;
    edge->callCount = callCount;
    edge->flags = flags;
    edge->next = source->uses;
    source->uses = edge;
    reverse = galloc(sizeof(CallGraphEdge));
    reverse->node = source;
    reverse->callCount = callCount;
    reverse->flags = flags;
    reverse->next = target->usedBy;
    target->usedBy = reverse;
}

void CCallGraph_Renumber(void) {
    CallGraphNode* node;
    UInt32 id = 0;

    for (node = ccallgraph; node; node = node->next) {
        node->id = id++;
    }
    ccallgraph_node_count = id;
}

void CCallGraph_Compute(void) {
    CallGraphNode* node;
    OLinkList* ref;

    CCallGraph_Renumber();
    for (node = ccallgraph; node; node = node->next) {
        node->uses = node->usedBy = NULL;
    }
    for (node = ccallgraph; node; node = node->next) {
        switch (node->kind) {
            case CGFunction: {
                const PackedReferences* references = &node->definition.function.references;
                UInt32 index;
                for (index = 0; index < references->count; index++) {
                    const PackedReference* reference = &references->entries[index];
                    if (reference->kind == CIRStream_ObjectReference) {
                        add_reference(
                            node, reference->object, reference->callCount, reference->flags
                        );
                    }
                }
                break;
            }
            case CGThunk:
                add_reference(node, node->definition.thunk.target, 1, 0);
                break;
            case CGData:
                for (ref = node->definition.data.references; ref; ref = ref->next) {
                    add_reference(node, ref->obj, 0, CIRStream_AddressTaken);
                }
                break;
        }
    }
}

void CCallGraph_ColorUses(CallGraphNode* node) {
    CallGraphEdge* edge;

    node->marked = TRUE;
    for (edge = node->uses; edge; edge = edge->next) {
        if (!edge->node->marked) {
            CCallGraph_ColorUses(edge->node);
        }
    }
}

static Boolean is_root(const CallGraphNode* node) {
    const Object* object = node->object;
    const NameSpace* scope;
    if (node->root || (object->flags & OBJECT_IPA_ROOT)) {
        return TRUE;
    }
    if ((object->qual & (Q_INLINE | Q_IMPLICIT_WEAK)) || object->sclass == TK_STATIC) {
        return FALSE;
    }
    if (node->kind == CGFunction && (object->qual & Q_WEAK) && object->u.func.inst) {
        return FALSE;
    }
    for (scope = object->nspace; scope; scope = scope->parent) {
        if (scope->is_unnamed) {
            return FALSE;
        }
    }
    return TRUE;
}

static void mark_used_nodes(void) {
    CallGraphNode* node;

    CCallGraph_Unmark();
    for (node = ccallgraph; node; node = node->next) {
        if (!node->marked && is_root(node)) {
            CCallGraph_ColorUses(node);
        }
    }

    // Unresolved overloads and intrinsic instructions can reference objects outside the tree.
    for (node = ccallgraph; node; node = node->next) {
        if (node->marked &&
            node->kind == CGFunction &&
            node->definition.function.references.opaque) {
            for (node = ccallgraph; node; node = node->next) {
                node->marked = TRUE;
            }
            return;
        }
    }
}

void CCallGraph_ResolveReferences(void) {
    CallGraphNode* last;
    CallGraphNode* node;

    do {
        last = graphTail;
        CCallGraph_Compute();
        mark_used_nodes();
        // The parser can have pending definitions for vtables and template instances.
        for (node = ccallgraph; node && !anyerrors; node = node->next) {
            if (node->marked && node->kind == CGFunction) {
                const PackedReferences* references = &node->definition.function.references;
                UInt32 index;
                for (index = 0; index < references->count; index++) {
                    const PackedReference* reference = &references->entries[index];
                    if (reference->kind == CIRStream_ObjectReference) {
                        CInline_ObjectAddrRef(reference->object);
                    }
                }
            }
        }
        CParser_FlushIPADefinitions();
    } while (last != graphTail && !anyerrors);
}

static void unlink_edge(CallGraphEdge** edges, CallGraphNode* node) {
    while ((*edges)->node != node) {
        edges = &(*edges)->next;
    }
    *edges = (*edges)->next;
}

static void CCallGraph_RemoveNode(CallGraphNode* node) {
    CallGraphEdge* edge;
    CallGraphNode** entry;

    for (edge = node->uses; edge; edge = edge->next) {
        unlink_edge(&edge->node->usedBy, node);
    }
    for (edge = node->usedBy; edge; edge = edge->next) {
        unlink_edge(&edge->node->uses, node);
    }
    entry = &graphNodes[graph_bucket(node->object)];
    while (*entry != node) {
        entry = &(*entry)->hashNext;
    }
    *entry = node->hashNext;
}

void CCallGraph_RemoveDeadNodes(void) {
    CallGraphNode** link = &ccallgraph;
    graphTail = NULL;
    while (*link) {
        CallGraphNode* node = *link;
        if (node->marked) {
            graphTail = node;
            link = &node->next;
        } else {
            *link = node->next;
            CCallGraph_RemoveNode(node);
        }
    }
}

void CCallGraph_MoveDeferredDataToEnd(void) {
    CallGraphNode** link = &ccallgraph;
    CallGraphNode* deferred = NULL;
    CallGraphNode* lastDeferred = NULL;

    while (*link) {
        CallGraphNode* node = *link;
        if (node->kind == CGData && node->definition.data.deferred) {
            *link = node->next;
            node->next = deferred;
            deferred = node;
            if (!lastDeferred) {
                lastDeferred = node;
            }
        } else {
            link = &node->next;
        }
    }
    if (deferred) {
        *link = deferred;
        graphTail = lastDeferred;
    }
}
