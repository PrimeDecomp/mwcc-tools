#ifndef COMPILER_IROFLOWGRAPH_H
#define COMPILER_IROFLOWGRAPH_H

#include "compiler/common.h"
#include "compiler/CError.h"
#include "compiler/CompilerTools.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2) // Pro7: options align=mac68k
struct IRONode {
    UInt16 index;
    UInt16 numsucc;
    UInt16* succ;
    UInt16 numpred;
    UInt16* pred;
    IROLinear* first;
    IROLinear* last;
    // The data-flow sets, named as IRO_DumpGraph labels them (Pro7: x16, x1A, x1E, x22)
    BitVector* in;
    BitVector* out;
    BitVector* gen;
    BitVector* kill;
    UInt32 unused; // TODO: invented name (Pro7: x26); only StartNode writes it, nothing reads it
    // TODO: invented name (Pro7: x2A); IroPropagate's available-copies out set, dumped as "AA"
    BitVector* copyOut;
    BitVector* dom;
    IRONode* nextnode;
    Boolean reachable; // TODO: invented name (Pro7: x36); set by IRO_MakeReachable
    Boolean visited; // TODO: invented name (Pro7: x37); IRO_MakeReachable's worklist mark
    Boolean mustreach;
    Boolean referenced; // TODO: invented name (Pro7: x39); a label or catch entry jumps here
    UInt16 loopdepth;
    // TODO: invented name (Pro7: x3C); a visited mark (IroExprRegeneration, the pointer analysis)
    Boolean marked;
    struct ObjectSet* addressed;
    Boolean mustreach1;
};

// A queue of flowgraph node indices (IroExprRegeneration's worklist). Its functions asserting with
// this header's name (line 93) say they are this header's inlines, as in Pro7; Wii names the one
// the original keeps out of line, IRO_FNodeWorkStack_Add, after which the others are named.
// TODO: invented names (Pro7: IROFlowgraph_sub_4C2140, _4C20E0, _4C2040, _4C2100), and the type's
// is Pro7's (Ninji's) name
typedef struct IRONodes {
    UInt16* indices;
    UInt16 num;
    UInt16 base;
} IRONodes;
#pragma pack(pop)

extern UInt16 IRO_NumNodes;
extern IRONode* IRO_FirstNode;
extern IRONode* IRO_LastNode;
extern IRONode* IRO_EndNode;
extern IRONode** IRO_NodeTable;
extern BitVector* IRO_VarKills;
extern BitVector* IRO_Avail;
extern BitVector* IRO_FuncKills;
extern BitVector* IRO_ExprKills;

extern void IRO_BuildFlowgraph(IROLinear* source);
extern void IRO_ComputeDom(void);
extern void IRO_ComputeSuccPred(void);
extern IRONode* IRO_NewFlowGraphNode(void);
extern IRONode* IRO_MergeFlowGraphNodes(IRONode* a, IRONode* b);

inline void IRO_FNodeWorkStack_Init(IRONodes* nodes) {
    nodes->indices = oalloc(sizeof(UInt16) * IRO_NumNodes);
    nodes->num = 0;
    nodes->base = 0;
}

inline void IRO_FNodeWorkStack_Free(IRONodes* nodes) {}

inline UInt16 IRO_FNodeWorkStack_Count(IRONodes* nodes) {
    return nodes->num;
}

inline UInt16 IRO_FNodeWorkStack_Remove(IRONodes* nodes) {
    UInt16 result = -1;
    if (nodes->num) {
        result = nodes->indices[nodes->base];
        nodes->base = (nodes->base + 1) % IRO_NumNodes;
        nodes->num--;
    }
    return result;
}

inline void IRO_FNodeWorkStack_Add(IRONodes* nodes, UInt16 index) {
    if (nodes->num < IRO_NumNodes) {
        nodes->indices[(nodes->base + nodes->num) % IRO_NumNodes] = index;
        nodes->num++;
    } else {
        CError_FATAL(93);
    }
}

#ifdef __cplusplus
}
#endif

#endif
