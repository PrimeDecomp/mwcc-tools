#include "version.h"

/* The time and date the release was built (this file's __TIME__ and __DATE__ when it was). */
#if VERSION == VERSION_GC_1_0
char *build_time = "14:30:41";
char *build_date = "Apr 13 2000";
#elif VERSION < VERSION_GC_1_2_5
char *build_time = "12:08:38";
char *build_date = "Feb  7 2001";
#elif LINKER >= 27
/* (the 2.7 linker's) */
char *build_time = "10:40:59";
char *build_date = "Aug 13 2004";
#elif LINKER == 13
/* (the 1.3.2 linker's) */
char *build_time = "23:43:34";
char *build_date = "May  7 2002";
#elif defined(LINKER) && VERSION >= VERSION_GC_2_7
/* (the 2.x linker's) */
char *build_time = "14:20:31";
char *build_date = "Jul 14 2003";
#elif VERSION < VERSION_GC_1_3
char *build_time = "10:58:30";
char *build_date = "Apr 23 2001";
#else
char *build_time = "17:24:36";
char *build_date = "Jan 10 2002";
#endif
