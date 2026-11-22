#ifndef COMPILER_IROEVAL_H
#define COMPILER_IROEVAL_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void IRO_InitializeIsAssociativeENodeTypeArray(void);
#if VERSION == VERSION_WII_1_0RC1
extern CInt64 IRO_MaxValueOfIntegerType(Type* type);
extern CInt64 IRO_MinValueOfIntegerType(Type* type);
#endif
extern void IRO_TruncateValueToType(CInt64* val, Type* type);
extern void IRO_TruncateBitfieldValueToType(CInt64* val, Type* type, TypeBitfield* bitfieldType);
extern void IRO_ConstantFolding(void);
extern Boolean IRO_EvaluateConditionals(void);
extern int IRO_SimplifyConditionals(void);
extern Boolean IRO_EvaluateDefinitePointers(Object* func);

#ifdef __cplusplus
}
#endif

#endif
