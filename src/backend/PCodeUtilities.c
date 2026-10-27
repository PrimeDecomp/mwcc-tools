#define CERROR_FILE "PCodeUtilities.c"
#include "compiler/common.h"
#include "compiler/PCodeUtilities.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/types.h"
#include "compiler/CError.h"
#include "compiler/CExpr2.h"
#include "compiler/CFunc.h"
#include "compiler/CodeGen.h"
#include "compiler/Exceptions.h"
#include "compiler/InstrSelection.h"
#include "compiler/ObjGen_PPC_EABI.h"
#include "compiler/PCode.h"
#if VERSION >= VERSION_GC_1_3
#include "compiler/Registers.h"
#ifdef __wasm__ /* (the variadic arguments are not after the named ones: __va_start) */
#include <stdarg.h>
#endif
extern void expand_out_of_range_conditional_branches(void);
#endif

struct Statement *gCurrentStatement;
struct PCodeBlock *gCurrentBlock;

enum { Register2 = 2, Register13 = 13 };

static inline SInt16 lowHalf(SInt32 value)
{
    return value;
}

static inline UInt8 exception_scopes_enabled(void)
{
    return copts.exceptions;
}

static inline UInt8 PCodeUtilities_ExceptionScopesEnabled(void)
{
    return copts.exceptions;
}

static inline UInt8 PCodeUtilities_ShouldSplitObjectInstructionBlock(void)
{
    return copts.instructionSchedulingMode;
}

/* Object cv-qualifier: for pointer types the pointee qualifier lives in the
 * TypePointer record, otherwise the object's own qual field is used. */
#define PCode_GetQual(obj)                                                                                             \
    ((SInt32)(((obj)->type->type == TYPEPOINTER) ? TYPE_POINTER((obj)->type)->qual : (obj)->qual))

#define PCARG(type) (*(type *)((args += 4) - 4))

PCodeInstruction *create_pcode_instruction(SInt16 opcode, char *args)
{
    PCodeOpcodeDescriptor *desc;
    PCodeInstruction *inst;
    SInt32 extra;
    const char *fmt;
    PCodeOperand *opnd;
    SInt32 mode;
    SInt32 count;
    SInt32 variableCount;

    desc = &gPCodeOpcodeDescriptors[opcode];
    variableCount = 0;
    fmt = desc->operand_format;
    count = desc->operand_count;
    extra = 0;
    if (*fmt == '#') {
        variableCount += PCARG(SInt32);
        count += variableCount;
        fmt++;
    }
    if (desc->flags & 0x200)
        extra++;
    inst = (PCodeInstruction *)lalloc(sizeof(PCodeInstruction) + (count + extra) * sizeof(PCodeOperand));
    inst->opcode = opcode;
    inst->operand_count = count;
    inst->flags = desc->flags;
    opnd = inst->operandData.operands;

    while (*fmt != 0) {
        if (*fmt == ',')
            fmt++;
        mode = 1;
        if (*fmt == '=') {
            mode++;
            fmt++;
        } else if (*fmt == '+') {
            mode = 3;
            fmt++;
        }
        switch (*fmt) {
            case 'b': {
                SInt32 value = PCARG(SInt32);
                if (value == 0)
                    mode = 0;
                opnd->kind = PCOp_GPR;
                opnd->value.reg = value;
                opnd->flags = mode;
                break;
            }
            case 'c':
                opnd->kind = PCOp_CRFIELD;
                opnd->value.reg = PCARG(SInt16);
                opnd->flags = mode;
                break;
            case 'f':
                opnd->kind = PCOp_FPR;
                opnd->value.reg = PCARG(SInt16);
                opnd->flags = mode;
                break;
            case 'v':
                opnd->kind = PCOp_VR;
                opnd->value.reg = PCARG(SInt16);
                opnd->flags = mode;
                break;
            case 'i':
                opnd->kind = PCOp_IMMEDIATE;
                opnd->value.signed_value = PCARG(SInt32);
                opnd->object = NULL;
                break;
            case 'l': {
                SInt32 label = PCARG(SInt32);
                if (label != 0) {
                    opnd->kind = PCOp_LABEL;
                    opnd->value.signed_value = label;
                } else {
                    Object *object;
                    opnd->kind = PCOp_MEMORY;
                    object = PCARG(Object *);
                    CError_ASSERT(151, object->otype == OT_OBJECT);
                    opnd->object = object;
                    opnd->value.signed_value = 0;
                    opnd->flags = 4;
                }
                break;
            }
            case 'm': {
                Object *object = PCARG(Object *);
                if (object != NULL) {
                    CError_ASSERT(162, object->otype == OT_OBJECT);
                    if (object->datatype == DABSOLUTE) {
                        opnd->kind = PCOp_IMMEDIATE;
                        opnd->object = object;
                        opnd->value.signed_value = PCARG(SInt32);
                    } else {
                        SInt32 quals;
                        opnd->kind = PCOp_MEMORY;
                        opnd->object = object;
                        opnd->value.signed_value = PCARG(SInt32);
                        if (inst->flags & (fIsRead | fIsWrite)) {
                            quals = PCode_GetQual(object) & 2;
                            if (quals != 0)
                                inst->flags |= fIsVolatile;
                            quals = PCode_GetQual(object) & 1;
                            if (quals != 0)
                                inst->flags |= fIsConst;
                        }
                        if (inst->flags & 0x24)
                            opnd->flags = 4;
                        else if (object->datatype == DLOCAL)
                            opnd->flags = 1;
                        else if (PCodeUtilities_Require(object))
                            opnd->flags = 2;
                        else
                            opnd->flags = 6;
                    }
                } else {
                    opnd->kind = PCOp_IMMEDIATE;
                    opnd->value.signed_value = PCARG(SInt32);
                    opnd->object = NULL;
                    if (inst->flags & (fIsRead | fIsWrite))
                        inst->flags |= fIsPtrOp;
                }
                break;
            }
            case 'M': {
                Object *object = PCARG(Object *);
                if (object != NULL) {
                    CError_ASSERT(212, object->otype == OT_OBJECT);
                    if (object->datatype == DABSOLUTE) {
                        opnd->kind = PCOp_IMMEDIATE;
                        opnd->object = object;
                        opnd->value.signed_value = PCARG(SInt32);
                    } else {
                        SInt32 quals;
                        opnd->kind = PCOp_MEMORY;
                        opnd->object = object;
                        opnd->value.signed_value = PCARG(SInt32);
                        if (inst->flags & (fIsRead | fIsWrite)) {
                            quals = PCode_GetQual(object) & 2;
                            if (quals != 0)
                                inst->flags |= fIsVolatile;
                            quals = PCode_GetQual(object) & 1;
                            if (quals != 0)
                                inst->flags |= fIsConst;
                        }
                        if (object->datatype == DLOCAL) {
                            CError_FATAL(234);
                        } else {
                            opnd->flags = 8;
                        }
                    }
                } else {
                    opnd->kind = PCOp_IMMEDIATE;
                    opnd->value.signed_value = PCARG(SInt32);
                    opnd->object = NULL;
                    if (inst->flags & (fIsRead | fIsWrite))
                        inst->flags |= fIsPtrOp;
                }
                break;
            }
            case 'r':
                opnd->kind = PCOp_GPR;
                opnd->value.reg = PCARG(SInt16);
                opnd->flags = mode;
                break;
            case 'C':
                opnd->kind = PCOp_SPR;
                opnd->value.reg = 1;
                opnd->flags = mode;
                break;
            case 'L':
                opnd->kind = PCOp_SPR;
                opnd->value.reg = 2;
                opnd->flags = mode;
                break;
            case 'V': {
                SInt32 index = variableCount;
                while (index--) {
                    opnd->kind = PCOp_GPR;
                    opnd->value.reg = 0x1f - index;
                    opnd->flags = mode;
                    opnd++;
                }
                opnd--;
                break;
            }
            case 'X':
                opnd->kind = PCOp_SPR;
                opnd->value.reg = 0;
                opnd->flags = mode;
                break;
            case 'Y': {
                SInt32 index = 0;
                do {
                    opnd->kind = PCOp_CRFIELD;
                    opnd->value.reg = index;
                    opnd->flags = mode;
                    opnd++;
                } while (++index <= 7);
                opnd--;
                break;
            }
            case 'Z':
                opnd->kind = PCOp_CRFIELD;
                opnd->value.reg = 0;
                opnd->flags = mode;
                break;
            case 'S':
                opnd->kind = PCOp_SPR;
                opnd->value.reg = PCARG(SInt16);
                opnd->flags = mode;
                break;
            case 'p':
                opnd->kind = TYPEMEMBERPOINTER;
                break;
            default:
            case '?':
                CError_FATAL(313);
                break;
        }
        fmt++;
        opnd++;
    }
    return inst;
}

PCodeInstruction *PCodeUtilities_CreateInstruction(UInt16 op, ...)
{
    va_list arguments;
    PCodeInstruction *instruction;
#if defined(__wasm__)
    arguments = __va_start(op);
#elif VERSION >= VERSION_GC_1_3
    {
        va_list base = (va_list)&op;
        arguments = base + (((va_list)(&op + 1) - base + 3) / 4) * 4;
    }
#else
    arguments = (va_list)&op + (((va_list)(&op + 1) - (va_list)&op + 3) / 4) * 4;
#endif
    instruction = create_pcode_instruction(op, arguments);
#if VERSION >= VERSION_GC_1_3
    instruction->f18[1] = gCurrentStatement != NULL ? gCurrentStatement->sourceoffset : -1;
#endif
    return instruction;
}

#if VERSION >= VERSION_GC_1_3
struct Fn004e95f0 {
    char pad[0x14];
    UInt32 flags;
};

void fn_004e95f0(struct Fn004e95f0 *a)
{
    a->flags &= ~0x6010;
    a->flags |= 0x100;
}
#endif

#if VERSION >= VERSION_GC_1_3
extern SInt32 data_005e0bf0[];
extern SInt32 data_005e6574[];

/* fill `op` with one use-operand for every register in the per-class bit masks; returns the next free operand */
PCodeOperand *fn_004e92d0(PCodeOperand *op, UInt32 *masks)
{
    RegClass13 cls;
    int i;

    for (cls = 4; cls >= 0; cls--) {
        for (i = 0; i < data_005e6574[cls]; i++) {
            op->kind = 0;
            op->arg = cls;
            op->value.reg = ((SInt32 (*)[32])data_005e0bf0)[cls][i];
            op->flags = 2;
            if (masks[cls] & (1 << ((SInt32 (*)[32])data_005e0bf0)[cls][i]))
                op->flags |= 1;
            if (cls == 1 && op->value.reg == 1)
                op->flags |= 1;
            op++;
        }
    }
    return op;
}
#endif

#if VERSION >= VERSION_GC_1_3
/* turn the instruction's label operand into an implicit use of r1 and normalise its opcode */
void fn_004e9530(PCodeInstruction *instr)
{
    PCodeOperand *op;
    int count;

    switch (instr->opcode) {
    case 0:
        instr->opcode = 1;
        break;
    case 0x12:
        instr->opcode = 0x13;
        break;
    case 0x11:
        instr->opcode = 0x14;
        break;
    }
    op = instr->operandData.operands;
    for (count = instr->operand_count; op->kind != 6 && count != 0; op++, count--) {
        if (op->kind == 0 && op->arg == 0 && op->value.reg == 1) {
            op->flags |= 2;
            instr->flags |= 0x1000000;
            return;
        }
    }
    if (op->kind != 6)
        CError_Internal("PCodeUtilities.c", 0xa9);
    op->kind = 0;
    op->arg = 0;
    op->value.reg = 1;
    op->flags = 2;
    if (gPCodeOpcodeDescriptors[instr->opcode].flags & 8) {
        instr->flags &= ~1;
        instr->flags |= 8;
    }
    instr->flags |= 0x1000000;
}
#endif

#if VERSION >= VERSION_GC_1_3
extern SInt32 data_005e6574[];
#if VERSION >= VERSION_GC_1_3
#define data_005e71b4 (*(UInt8 *)((char *)&copts + 0xd2))
#else
extern UInt8 data_005e71b4;
#endif
#if VERSION >= VERSION_GC_1_3
#define data_005e713e (*(UInt32 *)((char *)&copts + 0x5c))
#else
extern UInt32 data_005e713e;
#endif
extern struct PCodeBlock *gPCodeBlocks;
extern PCodeLabel *fn_004dbc20(void);
extern void fn_0049f4b0(void);
extern HashNameNode *COptimizer_GetFunctionObject(Object *obj);
extern void fn_004c4bc0(const char *name, const char *message);

/* emit the register-usage marker instruction and the label that follows it */
void fn_004e9070(UInt32 *masks)
{
    int n = 0;
    RegClass13 cls;
    int i;
    PCodeInstruction *instr;
    PCodeLabel *label;
    PCodeOperand *payload;

    for (cls = 0; cls < RegClass13_Max; cls++) {
        for (i = 0; data_005e6574[cls] > i; i++)
            n++;
    }
    if (PCodeUtilities_ExceptionScopesEnabled() && gCurrentStatement)
        n += Exceptions_CountBoundObjectFields(gCurrentStatement->dobjstack);
    instr = PCodeUtilities_CreateInstruction(0x13, n);
    payload = fn_004e92d0(instr->operandData.operands + 1, masks);
    if (PCodeUtilities_ExceptionScopesEnabled() && gCurrentStatement)
        Exceptions_CollectRegisterOperands(gCurrentStatement->dobjstack, payload);
    PCode_AppendInstruction(gCurrentBlock, instr);
    label = fn_004dbc20();
    if (gCurrentBlock->instruction_count != 0) {
        PCode_AddSuccessor(gCurrentBlock, label);
        fn_0049f4b0();
    }
    PCode_ResolveLabel(gCurrentBlock, label);
    if (PCodeUtilities_ExceptionScopesEnabled() && gCurrentStatement)
        Exceptions_AppendScopeEntry(instr, gCurrentStatement->dobjstack);
}

/* insert alignment no-ops before short aligned blocks, re-laying out the code until nothing changes */
UInt32 fn_004e9a30(Object *func, UInt32 size)
{
    PCodeBlock *block;
    PCodeBlock *prev;
    PCodeInstruction *last;
    PCodeInstruction *nop;
    int changed;
    UInt32 half;

    do {
        changed = 0;
        for (block = gPCodeBlocks; block != NULL; block = block->next) {
            if ((block->flags & 0x6000) == 0x2000 && (block->code_offset & 7) != 0 && block->instruction_count < 8) {
                prev = block->prev;
                if (prev != NULL && !(prev->flags & 0x2000)) {
                    last = prev->reverse_instructions;
                    if (last != NULL && last->opcode == 0x8c && !(last->flags & 0x100)) {
                        PCode_UnlinkInstruction(last);
                    } else {
                        nop = PCodeUtilities_CreateInstruction(0x8c);
                        nop->flags &= ~0x100;
                        PCode_AppendInstruction(prev, nop);
                    }
                    size = PCode_SetCodeOffsets();
                    changed = 1;
                }
            }
        }
        if (changed) {
            fn_004c4bc0(COptimizer_GetFunctionObject(func)->name + 0, "AFTER INSERT ALIGN NOPs");
            half = data_005e713e >> 1;
            if (size >= half) {
                ((void (*)(int, int))expand_out_of_range_conditional_branches)(half - 1, -half);
                size = PCode_SetCodeOffsets();
            }
        }
    } while (changed);
    return size;
}
#endif

#if VERSION >= VERSION_GC_1_3
/* (1.3 inlines PCode_AppendInstruction here) */
static inline void append_instruction(PCodeBlock *block, PCodeInstruction *instruction)
{
    PCodeInstruction *tail;
    if (block->instructions) {
        instruction->next = NULL;
        instruction->previous = block->reverse_instructions;
        block->reverse_instructions->next = instruction;
        block->reverse_instructions = instruction;
    } else {
        block->reverse_instructions = instruction;
        block->instructions = block->reverse_instructions;
        instruction->previous = NULL;
        instruction->next = instruction->previous;
    }
    instruction->block = block;
    block->instruction_count += 1U;
}

PCodeInstruction *PCodeUtilities_EmitInstruction(short opcode, ...)
{
#ifdef __wasm__
    va_list arguments = __va_start(opcode);
#else
    va_list base = (va_list)&opcode;
    va_list arguments = base + (((va_list)(&opcode + 1) - base + 3) / 4) * 4;
#endif
    PCodeInstruction *instruction = create_pcode_instruction(opcode, arguments);
    instruction->f18[1] = gCurrentStatement != NULL ? gCurrentStatement->sourceoffset : -1;
    append_instruction(gCurrentBlock, instruction);
    return instruction;
}
#else
PCodeInstruction *PCodeUtilities_EmitInstruction(short opcode, ...)
{
#ifdef __wasm__
    va_list arguments = __va_start(opcode);
#else
    va_list arguments = (va_list)&opcode + (((va_list)(&opcode + 1) - (va_list)&opcode + 3) / 4) * 4;
#endif
    return PCode_AppendInstruction(gCurrentBlock, create_pcode_instruction(opcode, arguments));
}
#endif

void PCodeUtilities_MakeRecordForm(PCodeInstruction *o)
{
    int num = o->operand_count;

    if (o->opcode == PC_ANDI || o->opcode == PC_ANDIS) {
        o->flags |= fRecordBit;
        return;
    }

    if ((unsigned short)(o->opcode - 0x3f) <= 1) {
        PCodeOperand *p;

        o->flags |= fSetsCarry;
        o->flags |= fRecordBit;
        o->operand_count = 5;
        o->opcode = PC_ADDICR;

        p = &o->operandData.operands[3];
        p->kind = PCOp_SPR;
        p->value.reg = 0;
        p->flags = 2;

        p = &o->operandData.operands[4];
        p->kind = PCOp_CRFIELD;
        p->value.reg = 0;
        p->flags = 2;
        return;
    }

    {
        int idx = num;
        PCodeOperand *it = &o->operandData.operands[idx];

        it->kind = PCOp_CRFIELD;
        it->flags = 2;
        o->operand_count = num + 1;

        if ((o->flags & 3) == 2) {
            it->value.reg = 1;
        } else if ((o->flags & 3) == 3) {
            it->value.reg = 6;
        } else {
            it->value.reg = 0;
        }

        if (o->opcode != PC_ADDICR) {
            o->flags |= fRecordBit;
        }
    }
}

void PCodeUtilities_ResolveLabel(PCodeLabel *data)
{
    if (gCurrentBlock->instruction_count != 0) {
        PCode_AddSuccessor(gCurrentBlock, data);
        PCode_CreateBlock();
    }
    PCode_ResolveLabel((gCurrentBlock), data);
}

void PCodeUtilities_EmitConditionBranch(SInt16 operand, SInt16 condition, SInt16 branchIfTrue, PCodeLabel *targetBlock)
{
    PCodeLabel *fallthroughBlock;
    SInt32 kind;

    fallthroughBlock = PCode_NewLabel();
    switch (condition) {
        case 0x18:
            branchIfTrue = !branchIfTrue;
        case 0x17:
            kind = 2;
            break;
        case 0x16:
            branchIfTrue = !branchIfTrue;
        case 0x13:
            kind = 0;
            break;
        case 0x15:
            branchIfTrue = !branchIfTrue;
        case 0x14:
            kind = 1;
            break;
    }
    PCodeUtilities_EmitInstruction(branchIfTrue ? 5 : 8, operand, kind, targetBlock);
    PCode_AddSuccessor(gCurrentBlock, targetBlock);
    PCode_AddSuccessor(gCurrentBlock, fallthroughBlock);
    PCode_CreateBlock();
    PCode_ResolveLabel((gCurrentBlock), (fallthroughBlock));
}

void PCodeUtilities_EmitBranch(PCodeLabel *target)
{
    PCodeUtilities_EmitInstruction(0U, target);
    PCode_AddSuccessor(gCurrentBlock, target);
    PCode_CreateBlock();
}

unsigned int PCodeUtilities_EmitConditionalBranch(unsigned int opcode, PCodeLabel *target)
{
    PCodeLabel *fallthrough = PCode_NewLabel();
    PCodeUtilities_EmitInstruction(opcode, target);
    PCode_AddSuccessor(gCurrentBlock, target);
    PCode_AddSuccessor(gCurrentBlock, fallthrough);
    PCode_CreateBlock();
    PCode_ResolveLabel(gCurrentBlock, fallthrough);
}

void PCodeUtilities_EmitInstructionAndCreateBlock(Object *object)
{
    PCodeUtilities_EmitInstruction(18, object, 0);
    PCode_CreateBlock();
}

PCodeOperand *fn_004a2290(PCodeOperand *operand, UInt32 gprMask, UInt32 fprMask, UInt32 vrMask)
{
    SInt32 reg;

    operand->kind = PCOp_GPR;
    operand->value.reg = 0;
    operand->flags = 2;
    if (gprMask & 1) {
        operand->flags |= 1;
    }
    operand++;

    reg = 3;
    do {
        operand->kind = PCOp_GPR;
        operand->value.reg = reg;
        operand->flags = 2;
        if (gprMask & (1L << reg)) {
            operand->flags |= 1;
        }
        reg++;
        operand++;
    } while (reg <= 12);

    reg = 0;
    do {
        operand->kind = PCOp_FPR;
        operand->value.reg = reg;
        operand->flags = 2;
        if (fprMask & (1L << reg)) {
            operand->flags |= 1;
        }
        reg++;
        operand++;
    } while (reg <= 13);

    reg = 0;
    do {
        operand->kind = PCOp_VR;
        operand->value.reg = reg;
        operand->flags = 2;
        if (vrMask & (1L << reg)) {
            operand->flags |= 1;
        }
        reg++;
        operand++;
    } while (reg <= 19);

    operand->kind = PCOp_CRFIELD;
    operand->value.reg = 0;
    operand->flags = 2;
    operand++;
    operand->kind = PCOp_CRFIELD;
    operand->value.reg = 1;
    operand->flags = 3;
    operand++;
    operand->kind = PCOp_CRFIELD;
    operand->value.reg = 6;
    operand->flags = 2;
    operand++;
    operand->kind = PCOp_CRFIELD;
    operand->value.reg = 7;
    operand->flags = 2;
    operand++;
    return operand;
}

#if VERSION >= VERSION_GC_1_3
extern SInt32 data_005e6574[];
PCodeOperand *fn_004e92d0(PCodeOperand *op, UInt32 *masks);

/* (1.3: one register-use operand for every register of every class; `masks` holds the used ones per class) */
void PCodeUtilities_EmitObjectInstructionWithPayload(Object *operand, SInt16 emitInstruction, UInt32 *masks)
{
    SInt32 operandCount = 0;
    RegClass13 cls;
    int i;
    PCodeInstruction *instruction;
    PCodeLabel *label;
    PCodeOperand *payload;
    char operandKind;

    for (cls = 0; cls < RegClass13_Max; cls++) {
        for (i = 0; data_005e6574[cls] > i; i++)
            operandCount++;
    }
    if (PCodeUtilities_ExceptionScopesEnabled() && gCurrentStatement)
        operandCount += Exceptions_CountBoundObjectFields(gCurrentStatement->dobjstack);

    operandKind = PCodeUtilities_Require(operand);
    if (operandKind == 10) {
        instruction = PCodeUtilities_CreateInstruction(1, operandCount, operand, 0);
    } else if (operandKind == 11) {
        instruction = PCodeUtilities_CreateInstruction(1, operandCount, operand, 0);
        instruction->flags |= 0x10000000;
    } else {
        instruction = PCodeUtilities_CreateInstruction(1, operandCount, operand, 0);
    }

    payload = fn_004e92d0(instruction->operandData.operands + 1, masks);

    if (PCodeUtilities_ExceptionScopesEnabled() && gCurrentStatement)
        Exceptions_CollectRegisterOperands(gCurrentStatement->dobjstack, payload);

    PCode_AppendInstruction(gCurrentBlock, instruction);

    if (emitInstruction != 0)
        PCodeUtilities_EmitInstruction(PC_NOP);

    label = PCode_NewLabel();
    if (gCurrentBlock->instruction_count != 0) {
        PCode_AddSuccessor(gCurrentBlock, label);
        PCode_CreateBlock();
    }
    PCode_ResolveLabel(gCurrentBlock, label);

    if (PCodeUtilities_ExceptionScopesEnabled() && gCurrentStatement)
        Exceptions_AppendScopeEntry(instruction, gCurrentStatement->dobjstack);
}
#else
void PCodeUtilities_EmitObjectInstructionWithPayload(Object *operand, SInt16 emitInstruction, SInt32 firstRegisterMask,
                                                     SInt32 secondRegisterMask, SInt32 thirdRegisterMask)
{
    SInt32 operandCount = 49;
    PCodeInstruction *instruction;
    PCodeLabel *label;
    PCodeOperand *payload;
    char operandKind;

    if (PCodeUtilities_ExceptionScopesEnabled() && gCurrentStatement)
        operandCount += Exceptions_CountBoundObjectFields(gCurrentStatement->dobjstack);

    operandKind = ObjGen_PPC_EABI_GetSectionAlignmentOrKind(operand);
    if (operandKind == 10) {
        instruction = PCodeUtilities_CreateInstruction(1, operandCount, operand, 0);
    } else if (operandKind == 11) {
        instruction = PCodeUtilities_CreateInstruction(1, operandCount, operand, 0);
        instruction->flags |= fAbsolute;
    } else {
        instruction = PCodeUtilities_CreateInstruction(1, operandCount, operand, 0);
    }

    payload =
        fn_004a2290(instruction->operandData.operands + 1, firstRegisterMask, secondRegisterMask, thirdRegisterMask);

    if (PCodeUtilities_ExceptionScopesEnabled() && gCurrentStatement)
        Exceptions_CollectRegisterOperands(gCurrentStatement->dobjstack, payload);

    if (PCodeUtilities_ShouldSplitObjectInstructionBlock()) {
        label = PCode_NewLabel();
        if (gCurrentBlock->instruction_count != 0) {
            PCode_AddSuccessor(gCurrentBlock, label);
            PCode_CreateBlock();
        }
        PCode_ResolveLabel(gCurrentBlock, label);
    }

    PCode_AppendInstruction(gCurrentBlock, instruction);

    if (emitInstruction != 0)
        PCodeUtilities_EmitInstruction(PC_NOP);

    label = PCode_NewLabel();
    if (gCurrentBlock->instruction_count != 0) {
        PCode_AddSuccessor(gCurrentBlock, label);
        PCode_CreateBlock();
    }
    PCode_ResolveLabel(gCurrentBlock, label);

    if (PCodeUtilities_ExceptionScopesEnabled() && gCurrentStatement)
        Exceptions_AppendScopeEntry(instruction, gCurrentStatement->dobjstack);
}
#endif

void PCodeUtilities_EmitAddress(short resultReg, short baseReg, struct Object *object, short offset)
{
    short addressReg;
    PCodeBlock *block;
    PCodeInstruction *instruction;

    addressReg = baseReg;
    if (object != NULL && ((int)baseReg == 2 || (int)baseReg == 13)) {
#if VERSION >= VERSION_GC_1_3
        CError_ASSERT(534, object->datatype != DLOCAL);
#endif
        addressReg = 0;
    }
    if (object == NULL && offset == 0) {
        PCodeUtilities_EmitInstruction(PC_MR, resultReg, addressReg);
        return;
    }
    PCodeUtilities_EmitInstruction(PC_ADDI, resultReg, addressReg, object, offset);
    if (object != NULL) {
        if ((int)addressReg == 0) {
            block = gCurrentBlock;
            instruction = block->reverse_instructions;
            instruction->flags = ~gPCodeOpcodeDescriptors[instruction->opcode].flags & instruction->flags |
                                 gPCodeOpcodeDescriptors[PC_LI].flags;
            instruction->opcode = PC_LI;
#if VERSION >= VERSION_GC_1_3
            instruction->operandData.operands[1] = instruction->operandData.operands[2];
            PC_SETCOUNT(instruction, 2);
#else
            instruction->operand_count = 2;
            instruction->operandData.operands[1] = instruction->operandData.operands[2];
#endif
        }
    } else
        CError_ASSERT(BY_VERSION(848, 557), baseReg != 0);
}

PCodeInstruction *PCodeUtilities_CreateInstructionWithObject(short operand1, short operand2, Object *operand3,
                                                             short operand4, unsigned char recordInstruction)
{
    PCodeInstruction *instruction;
    if (operand3 == NULL) {
        CError_FATAL(BY_VERSION(869, 577));
    }
    {
        short first = operand1;
        short second = operand2;
        short fourth = operand4;
        instruction = PCodeUtilities_CreateInstruction(63, first, second, operand3, fourth);
    }
    {
        unsigned char record = recordInstruction;
        if (record) {
            PCode_AppendInstruction(gCurrentBlock, instruction);
        }
    }
    return instruction;
}

PCodeInstruction *PCodeUtilities_MakeInstructionWithObject(short opcode, short operand, Object *object, short flags,
                                                           char appendToBlock)
{
    PCodeInstruction *result;

    if (copts.f25 != 0) {
        if ((int)operand == 0) {
            CError_FATAL(BY_VERSION(889, 601));
        }
        result = PCodeUtilities_CreateInstruction(0x42, opcode, operand, object, flags);
    } else {
        if (operand != 0) {
            CError_FATAL(BY_VERSION(894, 606));
        }
        result = PCodeUtilities_CreateInstruction(0x8a, opcode, object, flags);
    }
    if (appendToBlock != 0) {
        PCode_AppendInstruction(gCurrentBlock, result);
    }
    return result;
}

void emit_opcode_with_base_offset(short opcode, short dest_reg, short base_reg, Object *obj, SInt32 offset)
{
    short address_reg;
    short offset_reg;
    short temp_reg;

    address_reg = base_reg;
    if (obj && obj->datatype != DLOCAL && PCodeUtilities_Require(obj) &&
        (base_reg == Register2 || base_reg == Register13))
        address_reg = 0;

    if (offset != (short)offset) {
        short high_offset;
        if (opcode == 0x22 && dest_reg == 12)
            offset_reg = 12;
        else if (opcode == 0x22 && dest_reg == 11)
            offset_reg = 11;
        else
            offset_reg = gUsedVirtualRegistersGPR++;
        high_offset = (offset >> 16) + ((offset >> 15) & 1);
        PCodeUtilities_EmitInstruction(PC_ADDIS, offset_reg, address_reg, 0, high_offset);
        offset = (short)offset;
        address_reg = offset_reg;
    }

    if (opcode == 0xfc || opcode == 0xf7) {
        offset_reg = 0;
        if (obj) {
            temp_reg = gUsedVirtualRegistersGPR++;
            PCodeUtilities_EmitInstruction(PC_ADDI, temp_reg, address_reg, obj, offset);
            address_reg = temp_reg;
        } else if (offset) {
            offset_reg = gUsedVirtualRegistersGPR++;
            PCodeUtilities_EmitInstruction(PC_LI, offset_reg, offset);
        }
        if (!offset_reg)
            PCodeUtilities_EmitInstruction(opcode, dest_reg, 0, address_reg);
        else
            PCodeUtilities_EmitInstruction(opcode, dest_reg, address_reg, offset_reg);
    } else {
        PCodeUtilities_EmitInstruction(opcode, dest_reg, address_reg, obj, offset);
    }
}

void fn_004a1cb0(int integerRegisterMask, int floatingRegisterMask, int vectorRegisterMask)
{
    int operandCount;
    PCodeInstruction *instruction;
    PCodeLabel *label;
    PCodeOperand *nextOperand;

    operandCount = 49;
    if (exception_scopes_enabled() && gCurrentStatement)
        operandCount += Exceptions_CountBoundObjectFields(gCurrentStatement->dobjstack);
    instruction = PCodeUtilities_CreateInstruction(PC_BLRL, operandCount, 0);
    nextOperand = fn_004a2290(instruction->operandData.operands + 1, integerRegisterMask, floatingRegisterMask,
                              vectorRegisterMask);
    if (exception_scopes_enabled() && gCurrentStatement)
        Exceptions_CollectRegisterOperands(gCurrentStatement->dobjstack, nextOperand);
    if (copts.instructionSchedulingMode) {
        label = PCode_NewLabel();
        if (gCurrentBlock->instruction_count != 0) {
            PCode_AddSuccessor(gCurrentBlock, label);
            PCode_CreateBlock();
        }
        PCode_ResolveLabel(gCurrentBlock, label);
    }
    PCodeUtilities_EmitInstruction(PC_MTLR, 12);
    PCode_AppendInstruction(gCurrentBlock, instruction);
    label = PCode_NewLabel();
    if (gCurrentBlock->instruction_count != 0) {
        PCode_AddSuccessor(gCurrentBlock, label);
        PCode_CreateBlock();
    }
    PCode_ResolveLabel(gCurrentBlock, label);
    if (exception_scopes_enabled() && gCurrentStatement)
        Exceptions_AppendScopeEntry(instruction, gCurrentStatement->dobjstack);
}

void PCodeUtilities_LoadImmediate(SInt16 target, SInt32 value)
{
    SInt16 intermediate = target;

    if (value != (SInt16)value) {
        if (copts.deleteDeadInstructions > 1 && (SInt16)value != 0) {
#if VERSION >= VERSION_GC_1_3
            SInt32 fresh = gUsedVirtualRegistersGPR++;
            intermediate = fresh;
#else
            intermediate = gUsedVirtualRegistersGPR++;
#endif
        }
        PCodeUtilities_EmitInstruction(PC_LIS, intermediate, 0, (SInt16)((value >> 16) + (value >> 15 & 1)));
        if ((SInt16)value != 0) {
            PCodeUtilities_EmitInstruction(PC_ADDI, target, intermediate, 0, (SInt16)value);
        }
    } else {
        PCodeUtilities_EmitInstruction(PC_LI, target, value);
    }
}

void PCodeUtilities_EmitLoadImmediate(SInt16 destination, SInt32 value)
{
    SInt16 temporary = destination;
    SInt16 high;

    if (value != lowHalf(value)) {
        if (copts.deleteDeadInstructions > 1 && lowHalf(value) != 0) {
#if VERSION >= VERSION_GC_1_3
            SInt32 reg = gUsedVirtualRegistersGPR++;
            temporary = reg;
#else
            temporary = gUsedVirtualRegistersGPR++;
#endif
        }
        high = value >> 16;
        PCodeUtilities_EmitInstruction(PC_LIS, temporary, 0, high);
        if (lowHalf(value) != 0) {
            PCodeUtilities_EmitInstruction(PC_ORI, destination, temporary, lowHalf(value));
        }
    } else {
        PCodeUtilities_EmitInstruction(PC_LI, destination, value);
    }
}
