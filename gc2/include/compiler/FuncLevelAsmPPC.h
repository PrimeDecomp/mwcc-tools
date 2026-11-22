#ifndef COMPILER_FUNCLEVELASMPPC_H
#define COMPILER_FUNCLEVELASMPPC_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void setup_assembly_argument(Object* obj, short reg);
extern void assign_local_addresses(void);
extern void Assembler(Object* func);
#if VERSION == VERSION_WII_1_0RC1
extern void Assembler_GenerateIR(Object* func);
#endif
extern void SetupAssembler(void);
extern void CleanupAssembler(void);

#ifdef __cplusplus
}
#endif

#endif
