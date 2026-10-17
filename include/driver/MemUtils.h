#ifndef DRIVER_MEMUTILS_H
#define DRIVER_MEMUTILS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// (Pro7's, __stdcall in 2.x: Wii's publics are _xmalloc@8 ...)
extern void* __stdcall xmalloc(const char* what, int size);
extern void* __stdcall xcalloc(const char* what, int size);
extern void* __stdcall xrealloc(const char* what, void* old, int size);
extern char* __stdcall xstrdup(const char* str);
extern void __stdcall xfree(void* ptr);

#ifdef __cplusplus
}
#endif

#endif
