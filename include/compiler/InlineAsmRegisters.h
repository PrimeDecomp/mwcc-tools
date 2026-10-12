#ifndef COMPILER_INLINEASMREGISTERS_H
#define COMPILER_INLINEASMREGISTERS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#if VERSION >= VERSION_GC_1_3
#pragma options align = mac68k /* (1.3: 18 bytes, the int at +0xA) */
#endif
struct RegisterBinding {
    struct RegisterBinding *next;
    unsigned int key;
#if VERSION >= VERSION_GC_1_3
    unsigned char attribute1;
    int registerNumber;
#else
    unsigned short attribute1;
    unsigned short registerNumber;
#endif
    struct Object *object;
};

extern void *find_register_binding_key(unsigned int *key);
extern void CTemplateNew_InsertRegisterBinding(const char *key, unsigned int attribute1, short registerNumber,
                                               Object *object);
extern void CTemplateNew_ClearGlobalArray(void);

#ifdef __cplusplus
}
#endif

#endif
