#ifndef DRIVER_CLIENTGLUE_H
#define DRIVER_CLIENTGLUE_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

struct PluginRequiredInputRecord {
    unsigned char bytes[36];
};
#if VERSION >= VERSION_GC_1_3
#define GLUECALL __stdcall
#else
#define GLUECALL
#endif
extern int GLUECALL fn_004050e0(char *left, char *right, int count);
extern int ClientGlue_CompareLowercaseStrings(const char *left, const char *right);
#if VERSION >= VERSION_GC_1_3
extern int __stdcall fn_00412660(const char *left, const char *right);
#define STRINGS_EQUAL(left, right) fn_00412660(left, right)
#define STRINGS_DIFFER(left, right) (!fn_00412660(left, right))
extern int __stdcall CLIO_CompareStringsIgnoreCase(char *left, char *right);
#define CLIO_STRINGS_EQUAL(left, right) (CLIO_CompareStringsIgnoreCase((char *)(left), (char *)(right)) == 0)
#else
#define CLIO_STRINGS_EQUAL(left, right) (!ClientGlue_CompareLowercaseStrings(left, right))
#define STRINGS_EQUAL(left, right) (!ClientGlue_CompareLowercaseStrings(left, right))
#define STRINGS_DIFFER(left, right) (ClientGlue_CompareLowercaseStrings(left, right) != 0)
#endif
extern char *ClientGlue_DuplicateString(const char *a0);
extern int __stdcall ClientGlue_AddResourceStrings(char *arg1, SInt16 arg2, char **arg3);
#ifdef NONMATCHING
extern int __stdcall ClientGlue_AddPlugin(PluginRequiredInputRecord *a0);
#else
extern void __stdcall ClientGlue_AddPlugin(PluginRequiredInputRecord *a0);
#endif
extern int __stdcall ClientGlue_CreateAndAddPlugin(void *a0, void *a1);
#ifdef NONMATCHING
extern int __stdcall fn_00405280(PluginRequiredInputRecord *a0, PluginQueryTable *a1);
#else
extern void __stdcall fn_00405280(PluginRequiredInputRecord *a0, PluginQueryTable *a1);
#endif
extern unsigned int __stdcall ClientGlue_SetNamesAndRun(unsigned int argumentCount, char **arguments, char *buildDate,
                                                        char *buildTime);
extern int ClientGlue_InitializeAndParseCommandLine(void);
extern void __stdcall fn_004052a0(unsigned int a0, unsigned int a1);
extern unsigned int __stdcall fn_004052c0(unsigned int a0);
extern void __stdcall fn_004052d0(unsigned int a0, unsigned int a1);

#ifdef __cplusplus
}
#endif

#endif
