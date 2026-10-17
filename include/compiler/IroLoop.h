#ifndef COMPILER_IROLOOP_H
#define COMPILER_IROLOOP_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// IROLoop.flags, as the empty-loop pass's dump names them (LP_...); Pro7's comments guess the others.
typedef enum IROLoopFlags {
    LoopFlags_1 = 1, // LP_INDUCTION_AT_LEFT
    LP_LOOP_HAS_CALLS = 2,
    LP_LOOP_HAS_CNTRLFLOW = 4,
    LoopFlags_8 = 8, // LP_HAS_NONASSIGN
    LP_INDUCTION_NOT_FOUND = 0x10,
    LP_IFEXPR_NON_CANONICAL = 0x20,
    LP_HAS_MULTIPLE_INDUCTIONS = 0x40,
    LP_LOOP_HDR_HAS_SIDEEFFECTS = 0x80,
    LP_LOOP_STEP_ISADD = 0x100,
    LoopFlags_200 = 0x200, // LP_HEADER_FOLLOWS_UPDATE?
    LP_LOOP_STEP_ISPOS = 0x400,
    LoopFlags_800 = 0x800, // LP_IND_USED_IN_LOOP
    LoopFlags_1000 = 0x1000, // LP_HAS_MULTIPLE_EXITS
    LoopFlags_2000 = 0x2000, // inverse of LP_LOOP_STEP_ISADD?
    LP_LOOP_STEP_ISNEG = 0x4000,
    LP_LOOP_HAS_ASM = 0x8000,
    LoopFlags_10000 = 0x10000, // LP_WHILE_LOOP
    LoopFlags_20000 = 0x20000, // maybe LP_RECURSIVE_LOOP?
    LoopFlags_40000 = 0x40000 // LP_IS_REDUCTION_CAND
} IROLoopFlags;

typedef enum IROLoopIndFlags {
    LoopInd_HasMod = 1,
    LoopInd_HasDiv = 2,
    LoopInd_4 = 4,
    LoopInd_8 = 8
} IROLoopIndFlags;

#pragma pack(push, 2) // Pro7: options align=mac68k
struct IROLoopInd {
    IROLoopIndFlags flags;
    VarRecord* var;
    IRONode* fnode;
    IROLinear* nd;
    SInt32 addConst;
    IROLinear* addNode;
    IROLoopInd* next;
};

// A loop's analysis (ExtractLoopInfo): its blocks, its test and the induction variable the test
// uses.
struct IROLoop {
    SInt32 flags;
    IRONode* fnode;
    int unused; // TODO: invented name (Pro7: x8); ExtractLoopInfo clears it, nothing reads it
    IRONode* preheader; // TODO: invented name (Pro7: xC)
    IRONode* body; // TODO: invented name (Pro7: x10)
    IROLinear* init; // TODO: invented name (Pro7: nd14); sets the induction's initial value
    IROLinear* cond; // TODO: invented name (Pro7: nd18)
    IROLoopInd* induction;
    int lo; // TODO: invented name (Pro7: index20)
    int hi; // TODO: invented name (Pro7: index24)
    // TODO: invented names (Pro7: x28, x30), from IroEmptyLoop's dump ("# of iterations",
    // "FinalStoreVal")
    CInt64 iterCount;
    CInt64 finalStoreVal;
    int count; // TODO: invented name (Pro7: sizeBySomeMeasurement)
};
#pragma pack(pop)

extern IRONode* LoopNode;
extern Boolean ConditionalHeaderAtBottom;
extern IROLoopInd* FirstInd;
extern BitVector* InLoop;

extern void FindMustReach(void);
extern void AddPreds(IRONode* fnode);
extern void IRO_SetLoopDepth(void);
extern void IRO_FindLoops(void);
extern void ComputeLoopKills(void);
extern void ComputeLoopInvariance(void);
extern void ComputeLoopInduction(void);
extern void FindAssignmenttoInductionVar(IROLoop* loop, IRONode* fnode);
extern IROLoop* ExtractLoopInfo(IRONode* fnode);
extern CLabel* BuildLabel(IROList* list);

#ifdef __cplusplus
}
#endif

#endif
