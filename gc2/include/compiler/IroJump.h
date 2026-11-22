#ifndef COMPILER_IROJUMP_H
#define COMPILER_IROJUMP_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Boolean IRO_DoJumpChaining(void);
#if VERSION == VERSION_WII_1_0RC1
extern Boolean IRO_OptimizeSwitches(void);
#endif
extern void IRO_MakeReachable(IRONode* node);
extern Boolean IRO_RemoveUnreachable(void);
extern Boolean IRO_RemoveRedundantJumps(void);
extern Boolean IRO_RemoveLabels(void);

#ifdef __cplusplus
}
#endif

#endif
