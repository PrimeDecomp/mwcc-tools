#ifndef COMPILER_COPYPROPAGATION_H
#define COMPILER_COPYPROPAGATION_H

// CopyPropagation.c: the generic propagation pass (propagateinstructions), which copy, add and
// load-immediate propagation and the alias pass each run with a Propagation record of their own,
// and copy propagation itself (Pro7's CopyPropagation.c, deferred)

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*IsCandidateFunc)(PCode* pcode);
typedef int (*PropagatesToUseFunc)(int candidateID, int useID);
typedef void (*PropagateAndFinishFunc)(int candidateID);

// 2-byte packing (Pro7: mac68k alignment): a Propagation is 30 bytes in the records' .data
#pragma pack(push, 2)
typedef struct Propagation {
    IsCandidateFunc is_candidate;
    PropagatesToUseFunc propagatestouse;
    PropagateAndFinishFunc propagateandfinish;
    char* name;
    char* nameplural;
    char* format;
    UInt32 mask; // 2.6: a candidate with one of these instruction flags is not propagated
    Boolean computesUseDefChains;
} Propagation;
#pragma pack(pop)

struct Candidate {
    PCode* pcode;
    RegUseOrDef* list;
};

// TODO: invented member names (Pro7's vec0, vec4, vec8, vecC): the dataflow sets of a block
struct PropInfo {
    UInt32* gen;
    UInt32* kill;
    UInt32* in;
    UInt32* out;
};

extern int propagatedcopies;
extern int propagated_instructions;
extern int recursive_propagation;
// (2.6: SInt32, Pro7: int; propagateandremoveadd's `i < number_of_candidates` compares a long)
extern SInt32 number_of_candidates;
extern Candidate* Candidates;
extern PropInfo* propinfo;

extern void propagateinstructions(
    Object* proc, Propagation* config, int passCount, Boolean localflag
);
extern void propagatecopyinstructions(Object* proc, int flag);

#ifdef __cplusplus
}
#endif

#endif
