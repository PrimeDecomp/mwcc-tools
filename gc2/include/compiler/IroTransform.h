#ifndef COMPILER_IROTRANSFORM_H
#define COMPILER_IROTRANSFORM_H

#include "compiler/common.h"
#if VERSION == VERSION_WII_1_0RC1
#include "compiler/enode.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern void IRO_InitializeAssignmentOpArray(void);
extern void IRO_InitializeComplementaryOpArray(void);
extern void IRO_InitializeComplementaryOpLogicalArray(void);
#if VERSION == VERSION_WII_1_0RC1
extern ENodeType IRO_ComplementaryLogicalOp(ENodeType op);
#endif
extern void IRO_DoTransformations(void);
extern Boolean IRO_TransformSelfAssignmentToAssignment(IROLinear* nd);
extern void IRO_TransformTree(Statement* statements);

#ifdef __cplusplus
}
#endif

#endif
