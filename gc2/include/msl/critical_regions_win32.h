#ifndef MSL_CRITICAL_REGIONS_WIN32_H
#define MSL_CRITICAL_REGIONS_WIN32_H

#include "compiler/win32.h"

extern void __kill_critical_regions(void);
extern void __begin_critical_region(unsigned int a);
extern void __end_critical_region(unsigned int a);
extern void __init_critical_regions(void);

#endif
