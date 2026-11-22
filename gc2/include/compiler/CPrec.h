#ifndef COMPILER_CPREC_H
#define COMPILER_CPREC_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern char* precomp_target_str;

extern void SetupPrecompiler(Boolean isPrecompiling);
extern void CleanupPrecompiler(void);
#if VERSION == VERSION_WII_1_0RC1
extern void PreComp_StaticData(
    Object* obj, const void* data, OLinkList* links, SInt32 size, Boolean readOnly
);
#else
extern void PreComp_StaticData(Object* obj, const void* data, OLinkList* links, SInt32 size);
#endif
extern void PrecompilerWrite(void);
#if VERSION == VERSION_WII_1_0RC1
extern Boolean PrecompilerRead(short refnum, void* buffer, SInt32 size);
#else
extern void PrecompilerRead(short refnum, void* buffer);
#endif

#ifdef __cplusplus
}
#endif

#endif
