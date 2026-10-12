#ifndef COMPILER_PCODEUTILITIES_H
#define COMPILER_PCODEUTILITIES_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern PCodeInstruction *PCodeUtilities_MakeInstructionWithObject(short opcode, short operand, Object *object,
                                                                  short flags, char appendToBlock);
extern void PCodeUtilities_EmitAddress(short resultReg, short baseReg, struct Object *object, short offset);
#if VERSION >= VERSION_GC_1_3
/* (1.3: the used registers of each class, indexed SPR, CR field, VR, FPR, GPR) */
extern void PCodeUtilities_EmitObjectInstructionWithPayload(Object *operand, SInt16 emitInstruction, UInt32 *masks);
static inline void PCodeUtilities_EmitObjectInstructionWithRegisterMasks(Object *operand, SInt16 emitInstruction,
                                                                         UInt32 gprMask, UInt32 fprMask, UInt32 vrMask)
{
    UInt32 masks[5];
    masks[0] = 0;
    masks[1] = 0;
    masks[2] = vrMask;
    masks[3] = fprMask;
    masks[4] = gprMask;
    PCodeUtilities_EmitObjectInstructionWithPayload(operand, emitInstruction, masks);
}
#else
extern void PCodeUtilities_EmitObjectInstructionWithPayload(Object *operand, SInt16 emitInstruction, SInt32 value1,
                                                            SInt32 value2, SInt32 value3);
#define PCodeUtilities_EmitObjectInstructionWithRegisterMasks PCodeUtilities_EmitObjectInstructionWithPayload
#endif
extern PCodeOperand *fn_004a2290(PCodeOperand *p, UInt32 mask0, UInt32 mask1, UInt32 mask2);
extern void PCodeUtilities_EmitInstructionAndCreateBlock(Object *object);
extern unsigned int PCodeUtilities_EmitConditionalBranch(unsigned int opcode, PCodeLabel *target);
extern void PCodeUtilities_EmitBranch(PCodeLabel *target);
extern PCodeInstruction *PCodeUtilities_CreateInstructionWithObject(short operand1, short operand2, Object *operand3,
                                                                    short operand4, unsigned char recordInstruction);
extern void PCodeUtilities_EmitConditionBranch(SInt16 operand, SInt16 condition, SInt16 branchIfTrue,
                                               PCodeLabel *targetBlock);
extern void PCodeUtilities_ResolveLabel(PCodeLabel *data);
extern PCodeInstruction *PCodeUtilities_EmitInstruction(short opcode, ...);
extern PCodeInstruction *PCodeUtilities_CreateInstruction(UInt16 op, ...);
extern PCodeInstruction *create_pcode_instruction(SInt16 opcode, char *args);
extern void PCodeUtilities_MakeRecordForm(PCodeInstruction *o);
extern void PCodeUtilities_EmitLoadImmediate(SInt16 destination, SInt32 value);
extern void PCodeUtilities_LoadImmediate(SInt16 target, SInt32 value);
extern void fn_004a1cb0(int first, int second, int third);
extern void emit_opcode_with_base_offset(short opcode, short dest_reg, short base_reg, Object *obj, SInt32 offset);
extern struct Statement *gCurrentStatement;
extern struct PCodeBlock *gCurrentBlock;

#ifdef __cplusplus
}
#endif

#endif
