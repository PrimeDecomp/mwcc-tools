#include "ansi_prefix.Win32.h"
#include "time.c"
#ifdef LINKER
/* (the 2.x linker's link kept the whole unit, time() and its GetLocalTime among it, although nothing calls it;
   this reference keeps it in the equivalent link too) */
time_t (*const __time_kept)(time_t *) = time;
#endif
