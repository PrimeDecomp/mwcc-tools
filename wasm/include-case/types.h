/* types.h as MSL's stat.win32.h spells it, for case-sensitive file systems: the library tree's sys/TYPES.H, or
   else the next types.h (gc2's include/compiler/types.h) */
#if __has_include_next(<TYPES.H>)
#include_next <TYPES.H>
#else
#include_next <types.h>
#endif
