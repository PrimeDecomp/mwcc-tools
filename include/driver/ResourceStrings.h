#ifndef DRIVER_RESOURCESTRINGS_H
#define DRIVER_RESOURCESTRINGS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ResourceRegistration {
    char *data;
    short resourceId;
    char **value;
};
extern int ResourceStrings_AddResource(char *resourceData, short resourceId, char **resourceValue);
extern char *ResourceStrings_GetString(short id, short index);

#if VERSION >= VERSION_GC_1_3
/* the compiler's own version resource (packed: it starts at an odd address) */
#pragma options align = packed
typedef struct CompilerVersionInfo13 {
    char *company;
    char *product;
    char *description;
    char *copyright;
    char *version;
    UInt16 numbers[4];
    UInt8 valid;
} CompilerVersionInfo13;
#pragma options align = reset
extern CompilerVersionInfo13 data_005d89be;
extern char *fn_00408f30(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
