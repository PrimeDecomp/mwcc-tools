#include "ansi_prefix.Win32.h"
#include "NewHand.cpp"
/* (the 2.x linker's runtime has the bad_alloc flag in this unit's data) */
#include "version.h"
#if defined(LINKER) && VERSION >= VERSION_GC_2_7
#include "throwsbad.cpp"
#endif
