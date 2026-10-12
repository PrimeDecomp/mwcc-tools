#ifndef DRIVER_MEMUTILS_H
#define DRIVER_MEMUTILS_H

#include <setjmp.h>
#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#if VERSION >= VERSION_GC_1_3
#define XMEMCALL __stdcall
#else
#define XMEMCALL
#endif
extern void *XMEMCALL xmalloc(const char *text, unsigned int size);
extern void *XMEMCALL xcalloc(const char *pool, unsigned int size);
extern void *XMEMCALL xrealloc(const char *detail, void *block, unsigned int size);
extern char *XMEMCALL xstrdup(const char *s);
#if VERSION >= VERSION_GC_1_3
extern void __stdcall xfree(void *block);
#endif
extern int __stdcall MemUtils_CallPluginEntry(Plugin *entry);

#ifdef __cplusplus
}
#endif

#endif
