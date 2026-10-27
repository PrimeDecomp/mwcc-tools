#include "ansi_prefix.Win32.h"
#include "version.h"

/* (the 2.x tools' runtime is the Pro 5.3 updater's) */
#if VERSION >= VERSION_GC_2_7
#include "../../lib/runtime53/arrcondes.cpp"
#else
#include "arrcondes.cpp"
#endif
