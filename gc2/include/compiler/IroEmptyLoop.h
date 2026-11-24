#ifndef COMPILER_IROEMPTYLOOP_H
#define COMPILER_IROEMPTYLOOP_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void IRO_FindEmptyLoops(void);
#if VERSION == VERSION_WII_1_0RC1
extern Boolean IRO_RemoveAllEmptyLoops(Boolean removeDynamic);
#endif

#ifdef __cplusplus
}
#endif

#endif
