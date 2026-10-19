#ifndef COMPILER_SPILLCODE_H
#define COMPILER_SPILLCODE_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void estimatespillcosts(void);
extern void insertspillcode(void);

#ifdef __cplusplus
}
#endif

#endif
