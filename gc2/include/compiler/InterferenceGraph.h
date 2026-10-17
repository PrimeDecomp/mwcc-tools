#ifndef COMPILER_INTERFERENCEGRAPH_H
#define COMPILER_INTERFERENCEGRAPH_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// IGNode.flags
enum {
    fSpilled = 1,
    fPushed = 2,
    fCoalesced = 4,
    fCoalescedInto = 8,
    fPairHigh = 0x10,
    fPairLow = 0x20,
    fMultipleDefs = 0x40 // written more than once: no single defining instr (TODO: invented name)
};

// 2.6: IGNode.flags for a paired-single register, from its operands' EffectPS0/EffectPS1
// (insertspillcode: a node with the second half spills both halves, with psq_st/psq_l; TODO:
// invented names)
enum {
    fPairedSingle1 = 0x800,
    fPairedSingle0 = 0x1000
};

typedef struct IGNode {
    struct IGNode* next;
    Object* spillTemporary;
    // TODO: invented member names (Pro7: instr8, x10, x12, x14). The register's only definition
    // while it is location-independent (it can be recomputed instead of spilled)
    PCode* instr;
    int spillCost;
    short reg; // (the node's own register)
    short degree; // (its neighbours not yet removed by simplification)
    short color; // (the register it is given; -1 until colored)
    UInt16 flags;
    short arraySize;
    short array[1];
} IGNode;

extern IGNode** interferencegraph;

extern int interferes(UInt32 a, UInt32 b);
extern void buildinterferencegraph(Object* proc);

#ifdef __cplusplus
}
#endif

#endif
