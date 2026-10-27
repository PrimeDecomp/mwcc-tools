#ifndef DRIVER_PARSERHELPERS_H
#define DRIVER_PARSERHELPERS_H

#include <setjmp.h>
#include "compiler/common.h"
#include "compiler/win32.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int get_spec_from_signature_callback(const char *path, OSSpec *spec);
extern char *ParserHelpers_GetFirstEnvironmentVariable(char *list, char verbose, char **out);
/* The end of a list of names for ParserHelpers_GetFirstEnvironmentVariable, which reads names up to an empty one.
 * The original ends with the literal's padding; in WebAssembly the next literal follows, so there the list ends
 * with one more NUL. */
#ifdef NONMATCHING
#define ENVIRONMENT_LIST_END "\0"
#else
#define ENVIRONMENT_LIST_END
#endif
extern Boolean match_extension_pattern(char *pattern, char *name);
extern int fn_0040c9e7(char *name, int recursive, char *override);
extern char data_0058851b;
extern void report_environment_variable_message(void (*report)(char *msg, va_list ap), char *where, int id, ...);
extern int ParserHelpers_ParsePathList(char *path, char separator, char alternateSeparator, int mode, char *name,
                                       char option, int flags, unsigned char toggle);
extern int fn_0040cd55(char *name, char *filter, char *override);
extern int data_00587e14;
extern int data_00587e18;
extern Boolean data_00587e20;
extern void ParserHelpers_AppendText(struct StorageHandle **hp, char *text);
extern int fn_0040cff6(void);
extern int fn_0040d002(void);
extern short data_0054a0b8;

#ifdef __cplusplus
}
#endif

#endif
