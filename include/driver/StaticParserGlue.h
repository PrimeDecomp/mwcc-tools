#ifndef DRIVER_STATICPARSERGLUE_H
#define DRIVER_STATICPARSERGLUE_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int RegisterStaticParserPlugins(void);
extern int RegisterStaticParserResources(void);

#ifdef __cplusplus
}
#endif

#endif
