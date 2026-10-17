#ifndef COMPILER_LOOPDETECTION_H
#define COMPILER_LOOPDETECTION_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// 2-byte packing (Pro7: mac68k alignment): a BasicInductionVar is 26 bytes (addbasicinductionvariable's
// oalloc)
#pragma pack(push, 2)
struct BlockList {
    BlockList* next;
    PCodeBlock* block;
};

struct InstrList {
    InstrList* next;
    PCode* instr;
};

// Pro7's LoopBound: how a counting loop's lower or upper bound is known (Loop.lowerType, .upperType)
typedef enum LoopBound {
    LOOP_BOUND_INDETERMINATE,
    LOOP_BOUND_CONSTANT,
    LOOP_BOUND_VARIABLE
} LoopBound;

struct Loop {
    Loop* parent;
    Loop* nextSibling;
    Loop* children;
    PCodeBlock* body; // repeated block
    PCodeBlock* preheader; // block at the start of the loop
    PCodeBlock* footer; // block at the end of the loop
    PCode* inductionUpdate; // TODO: invented name (Pro7's pc18): the addi stepping a counting loop
    BlockList* blocks;
    UInt32* memberblocks;
    // TODO: invented names (Pro7's vec24, vec28, vec2C)
    UInt32* exitblocks; // the blocks with a successor outside the loop
    UInt32* exit_dominators; // the blocks that dominate every exit block
    UInt32* backedge_dominators; // the blocks that dominate every back edge
    BasicInductionVar* basicInductionVars;
    int loopWeight;
    SInt32 bodySize; // the instructions in the body (Pro7: int; 2.6's unrollunknownBDNZ: SInt32)
    SInt32 iterationCount;
    SInt32 lower;
    SInt32 upper;
    SInt32 step; // the value added in each iteration
    UInt8 unknownCondition;
    // TODO: invented names (Pro7's x4D, x4E, x4F, x52, x53, x54, x57)
    UInt8 has_call;
    UInt8 uses_count_register;
    UInt8 is_straight_line; // every block but the body has one predecessor and one successor
    UInt8 isUnknownCountingLoop; // is a counting loop with non-constant iteration count
    UInt8 isKnownCountingLoop;
    UInt8 has_unrolled_code; // a block has fUnrolled (code the front end unrolled)
    UInt8 has_indexed_load;
    UInt8 has_indexed_store;
    LoopBound lowerType;
    LoopBound upperType;
    UInt8 has_memory_barrier;
};

struct BasicInductionVar {
    BasicInductionVar* next;
    Loop* loop;
    InductionVar* inductionVars;
    InstrList* instrs; // TODO: invented name (Pro7's instrsC): the instructions that step it
    PCode* initializer;
    SInt32 step;
    short reg;
};

struct InductionVar {
    InductionVar* next;
    BasicInductionVar* basicVar;
    PCode* instr;
    // TODO: invented names (Pro7's instrC, x18, x1A, x1C, x1E)
    PCode* replacement_instruction; // the instruction initializeinductionvariable generates
    Loop* someloop;
    SInt32 step;
    short base_operand; // the index in instr of the base register operand
    short source_operand; // the index in instr of the source operand
    short source_register; // the register initializeinductionvariable sets up
    short destination_register; // the register the reduced value is kept in
};
#pragma pack(pop)

extern Loop* loopsinflowgraph;
extern int loopdetection_nblocks;
extern struct BitVector* LoopTemp;
extern struct LoopList* LoopList_First;

extern void addblocktoloop(Loop* loop, PCodeBlock* block);
extern void insertpreheaderblock(Loop* loop);
extern void findloopsinflowgraph(void);
extern void analyzeForCountableLoops(Loop* loop);
extern void analyzeloop(Loop* loop);
extern void analyzeloopsinflowgraph(void);

#ifdef __cplusplus
}
#endif

#endif
