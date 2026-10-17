#ifndef COMPILER_CONSTANTPROPAGATION_H
#define COMPILER_CONSTANTPROPAGATION_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int propagatedconstants;

extern void propagateconstants(void);

#ifdef __cplusplus
}
#endif

#endif
