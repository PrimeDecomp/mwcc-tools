#ifndef DRIVER_TOOLHELPERS_CC_H
#define DRIVER_TOOLHELPERS_CC_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int Opt_DummyLinkerRoutine(const char* opt, void* var, const char* arg, int flags);
extern void FinishCompilerTool(void);

#ifdef __cplusplus
}
#endif

#endif
