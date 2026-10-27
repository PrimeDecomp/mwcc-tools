#ifndef DRIVER_UTILS_H
#define DRIVER_UTILS_H

#include <setjmp.h>
#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int to_lowercase(int character);
/* to_lowercase called as int (*)(char), as the original does. WebAssembly traps on that call (its parameter is
 * a char there), so there it is a plain call. */
#ifdef NONMATCHING
#define TO_LOWERCASE_CHAR(c) to_lowercase(c)
#else
#define TO_LOWERCASE_CHAR(c) ((int (*)(char))to_lowercase)(c)
#endif
extern unsigned int Utils_IsDigit(char c);
extern int Utils_IsAlpha(char c);
extern int Utils_IsAlnum(char character);
extern int Utils_IsHexDigit(char c);
extern char *Utils_FormatOptions(char *options, char *destination, SInt8 flags);
extern int Utils_MatchStringSegments(char *s1, char *s2, int casesens, int abbrev);

#ifdef __cplusplus
}
#endif

#endif
