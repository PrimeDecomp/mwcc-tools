#ifndef COMPILER_CODEMOTION_H
#define COMPILER_CODEMOTION_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int movedloopinvariantcode;
extern int unswitchedinvariantcode;

extern void moveloopinvariantcode(void);

#ifdef __cplusplus
}
#endif

#endif
