#ifndef COMPILER_LOOPOPTIMIZATION_H
#define COMPILER_LOOPOPTIMIZATION_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int optimizedloops;
extern int optimizedloop_full_unroll;
extern int optimizedloop_trans_regs;

extern void pccomputepredecessors1(PCodeBlock* block);
extern void changearraytoregisters(void);
extern void optimizeloops(void);
#if VERSION == VERSION_WII_1_0RC1
extern Boolean rewrite_memory_references(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
