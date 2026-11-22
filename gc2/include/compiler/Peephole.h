#ifndef COMPILER_PEEPHOLE_H
#define COMPILER_PEEPHOLE_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void peepholeoptimizeforward(Object* func, Boolean removeArgumentExtensions);
#if VERSION == VERSION_WII_1_0RC1
extern void eliminatedeadpcode(Object* func, Boolean fixed_point);
extern void peepholeoptimizeearly(Object* func);
extern void peepholeoptimizeprecolor(Object* func);
#endif
extern void peepholemergeblocks(Object* func, Boolean mergePrologEpilog);
extern void peepholeoptimizepcode(Object* func);

#ifdef __cplusplus
}
#endif

#endif
