#ifndef COMPILER_INLINEASMMNEMONICSPPC_H
#define COMPILER_INLINEASMMNEMONICSPPC_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)
struct AsmOperandPattern {
    char *name;
    unsigned int opcode;
#if VERSION >= VERSION_GC_1_3
    /* (1.3: a format string such as "=r,r" in place of the operand kinds) */
    char *operands;
#else
    unsigned char operands[6];
#endif
    unsigned int processorMask;
    unsigned int instruction;
};
#pragma pack(pop)
struct NameLookupLink {
    struct NameLookupLink *next;
    struct AsmOperandPattern *record;
};

extern struct NameLookupLink *asmOperandPatternLookup[BY_VERSION(256, 1024)];
extern AsmOperandPattern *CTemplateNew_FindAsmOperandPattern(char *name);
extern void CTemplateNew_InitAsmOperandPatternLookup(void);

#ifdef __cplusplus
}
#endif

#endif
