#ifndef COMPILER_IROSUBABLE_H
#define COMPILER_IROSUBABLE_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void IRO_InitializeIsSubableOpArray(void);
extern Boolean IRO_IsSubableExpression(IROLinear* nd);
#if VERSION == VERSION_WII_1_0RC1
extern Boolean IRO_IsSubableExpressionDefault(IROLinear* nd);
#endif

#ifdef __cplusplus
}
#endif

#endif
