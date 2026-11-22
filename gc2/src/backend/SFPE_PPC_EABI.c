#include "compiler/SFPE_PPC_EABI.h"
#include "compiler/CError.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/enode.h"
#include "compiler/InstrSelection.h"
#include "compiler/objects.h"
#include "compiler/Operands.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/PCodeUtilities.h"
#include "compiler/PPCError.h"
#include "compiler/RegisterInfo.h"
#include "compiler/Registers.h"
#include "compiler/types.h"

static UInt32 used_regs_r3_r4[RegClassMax] = {0, 0, 0, 0, (1 << 3) | (1 << 4)};
static UInt32 used_regs_r3_r6[RegClassMax] = {
    0, 0, 0, 0, (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6)
};
static UInt32 used_regs_r3[RegClassMax] = {0, 0, 0, 0, (1 << 3)};

static inline void lift_result(Operand* op, int kind) {
    op->optype = kind;
    op->reg = ALLOC_GPR();
    if (kind == OpndType_GPR) {
        emitpcode(PC_MR, op->reg, 3);
    } else {
        op->regHi = ALLOC_GPR();
        emitpcode(PC_MR, op->reg, low_reg);
        emitpcode(PC_MR, op->regHi, high_reg);
    }
}

#ifdef ORACLE_PROBES
#define lift_result(...) ORACLE_INLINED(lift_result(__VA_ARGS__))
#endif

static inline void LoadFloatArg(int reg) {
    union {
        float f;
        SInt32 l;
    } u;

    u.f = one_point_zero.data.value;
    load_immediate_SFPE(reg, u.l);
}

static inline void LoadFloatArgGPR(int reg) {
    LoadFloatArg(reg);
}

static inline void LoadDoubleArg(int reg1, int reg2) {
    union {
        Float f;
        CInt64 i;
    } v;

    v.f = one_point_zero;
    load_immediate_SFPE(reg2, v.i.lo);
    load_immediate_SFPE(reg1, v.i.hi);
    if (reg2 != high_reg2) {
        emitpcode(PC_MR, high_reg2, reg2);
    }
    if (reg1 != low_reg2) {
        emitpcode(PC_MR, low_reg2, reg1);
    }
}

#if VERSION == VERSION_WII_1_0RC1
static void SFPE_increment_double(short packedReg, Boolean decrement, short outputReg) {
    static const UInt32 one[] = {0x3FF00000, 0};
    short literal = ALLOC_GPR();
    load_imm_vector(literal, one);
    emitpcode(decrement ? PC_EFDSUB : PC_EFDADD, outputReg, packedReg, literal);
}

static short SFPE_spe_opcode(ENodeType operation, Boolean wide) {
    switch (operation) {
        case EADD: return wide ? PC_EFDADD : PC_EFSADD;
        case ESUB: return wide ? PC_EFDSUB : PC_EFSSUB;
        case EMUL: return wide ? PC_EFDMUL : PC_EFSMUL;
        case EDIV: return wide ? PC_EFDDIV : PC_EFSDIV;
        default:
            CError_FATAL(97);
            return PC_INVALID;
    }
}

static void SFPE_multiply_add(short opcode, ENode* mul, ENode* addend, short outputReg, Operand* output) {
    ENode* left = mul->data.diadic.left;
    ENode* right = mul->data.diadic.right;
    Operand productLeft, productRight, accumulator;
    short reg;

    memclrw(&productLeft, sizeof(productLeft));
    memclrw(&productRight, sizeof(productRight));
    memclrw(&accumulator, sizeof(accumulator));
    if (addend->hascall) {
        GEN_NODE(addend, &accumulator);
        ENSURE_GPR(&accumulator, addend->rtype, 0);
    }
    GEN_NODE(left, &productLeft);
    ENSURE_GPR(&productLeft, left->rtype, 0);
    GEN_NODE(right, &productRight);
    ENSURE_GPR(&productRight, right->rtype, 0);
    if (!addend->hascall) {
        GEN_NODE(addend, &accumulator);
        ENSURE_GPR(&accumulator, addend->rtype, 0);
    }
    reg = ALLOC_GPR();
    emitpcode(PC_MR, reg, accumulator.reg);
    emitpcode(opcode, reg, productLeft.reg, productRight.reg);
    output->optype = OpndType_GPR;
    output->reg = outputReg ? outputReg : reg;
    if (output->reg != reg) {
        emitpcode(PC_MR, output->reg, reg);
    }
}

static Boolean SFPE_try_fused(ENode* expr, Boolean negate, short outputReg, Operand* output) {
    ENode* left;
    ENode* right;

    if (!copts.spe_addl_vector || !USE_SPE_FLOAT(4) || expr->rtype->size != 4 ||
        !ENODE_IS2(expr, EADD, ESUB)) {
        return FALSE;
    }
    left = expr->data.diadic.left;
    right = expr->data.diadic.right;
    if (ENODE_IS(expr, EADD) && ENODE_IS(right, EMUL)) {
        SFPE_multiply_add(negate ? PC_EFSNMADD : PC_EFSMADD, right, left, outputReg, output);
    } else if (ENODE_IS(left, EMUL)) {
        short opcode = ENODE_IS(expr, EADD) ? (negate ? PC_EFSNMADD : PC_EFSMADD)
                                          : (negate ? PC_EFSNMSUB : PC_EFSMSUB);
        SFPE_multiply_add(opcode, left, right, outputReg, output);
    } else if (!negate && ENODE_IS(expr, ESUB) && ENODE_IS(right, EMUL)) {
        SFPE_multiply_add(PC_EFSNMSUB, right, left, outputReg, output);
    } else {
        return FALSE;
    }
    return TRUE;
}

static void SFPE_compare_spe(ENodeType operation, Boolean wide, short left, short right, Operand* output, int branch) {
    short opcode;
    Boolean invert;

    switch (operation) {
        case EEQU:
        case ENOTEQU:
            opcode = wide ? PC_EFDTSTEQ : PC_EFSTSTEQ;
            break;
        case ELESS:
        case EGREATEREQU:
            opcode = wide ? PC_EFDTSTLT : PC_EFSTSTLT;
            break;
        case EGREATER:
        case ELESSEQU:
            opcode = wide ? PC_EFDTSTGT : PC_EFSTSTGT;
            break;
        default:
            CError_FATAL(120);
            return;
    }
    invert = operation == ENOTEQU || operation == ELESSEQU || operation == EGREATEREQU;
    emitpcode(opcode, 0, left, right);
    if (branch) {
        short flags;
        output->optype = OpndType_GPR;
        output->reg = ALLOC_GPR();
        flags = ALLOC_GPR();
        emitpcode(PC_MFCR, flags);
        emitpcode(PC_RLWINM, invert ? flags : output->reg, flags, 2, 30, 31);
        if (invert) {
            emitpcode(PC_XORI, output->reg, flags, 1);
        }
    } else {
        if (invert) {
            emitpcode(PC_CRNOR, 0, 1, 0, 1, 0, 1);
        }
        output->optype = OpndType_CRField;
        output->reg = 0;
        output->regOffset = EGREATER;
    }
}
#endif

void SFPE_gen_OP(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* left;
    Operand leftOperand;
    Operand rightOperand;
    ENode* right;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;

#if VERSION == VERSION_WII_1_0RC1
    if (SFPE_try_fused(expr, FALSE, outputReg, output)) {
        return;
    }
#endif

    memclrw(&leftOperand, sizeof(leftOperand));
    memclrw(&rightOperand, sizeof(rightOperand));

#if VERSION == VERSION_WII_1_0RC1
    if (TYPE_IS_DPFP(expr->rtype)) {
        GEN_NODE(right, &rightOperand);
        Coerce_to_spe_register(&rightOperand, right->rtype, 0);
        GEN_NODE(left, &leftOperand);
        Coerce_to_spe_register(&leftOperand, left->rtype, 0);
        output->optype = OpndType_GPR;
        output->reg = outputReg ? outputReg : ALLOC_GPR();
        emitpcode(SFPE_spe_opcode(expr->type, TRUE), output->reg, leftOperand.reg, rightOperand.reg);
        return;
    }
#endif

    if (expr->rtype->size == 4) {
        GEN_NODE(right, &rightOperand);
        if (rightOperand.optype) {
            Coerce_to_register(&rightOperand, right->rtype, 0);
        }
        CError_ASSERT(154, rightOperand.optype == OpndType_GPR);

        GEN_NODE(left, &leftOperand);
        if (leftOperand.optype) {
            Coerce_to_register(&leftOperand, left->rtype, 0);
        }
        CError_ASSERT(161, leftOperand.optype == OpndType_GPR);

#if VERSION == VERSION_WII_1_0RC1
        if (USE_SPE_FLOAT(4)) {
            output->optype = OpndType_GPR;
            output->reg = outputReg ? outputReg : ALLOC_GPR();
            emitpcode(SFPE_spe_opcode(expr->type, FALSE), output->reg, leftOperand.reg, rightOperand.reg);
            return;
        }
#endif

        {
            SInt32 reg = leftOperand.reg;
            if (reg != 3) {
                emitpcode(PC_MR, 3, reg);
            }
        }
        {
            SInt32 reg = rightOperand.reg;
            if (reg != 4) {
                emitpcode(PC_MR, 4, reg);
            }
        }

        if (expr->type == EADD) {
            branch_subroutine(rt_sfpe_f_add, 0, used_regs_r3_r4);
        } else if (expr->type == ESUB) {
            branch_subroutine(rt_sfpe_f_sub, 0, used_regs_r3_r4);
        } else if (expr->type == EMUL) {
            branch_subroutine(rt_sfpe_f_mul, 0, used_regs_r3_r4);
        } else if (expr->type == EDIV) {
            branch_subroutine(rt_sfpe_f_div, 0, used_regs_r3_r4);
        } else {
            CError_FATAL(218);
        }

        output->optype = OpndType_GPR;
        output->reg = ALLOC_GPR();
        emitpcode(PC_MR, output->reg, 3);
    } else {
        GEN_NODE(right, &rightOperand);
        coerce_to_register_pair(&rightOperand, right->rtype, 0, 0);
        CError_ASSERT(237, rightOperand.optype == OpndType_GPRPair);

        GEN_NODE(left, &leftOperand);
        coerce_to_register_pair(&leftOperand, left->rtype, 0, 0);
        CError_ASSERT(243, leftOperand.optype == OpndType_GPRPair);

        if (leftOperand.regHi != high_reg) {
            emitpcode(PC_MR, high_reg, leftOperand.regHi);
        }
        if (leftOperand.reg != low_reg) {
            emitpcode(PC_MR, low_reg, leftOperand.reg);
        }
        if (rightOperand.regHi != high_reg2) {
            emitpcode(PC_MR, high_reg2, rightOperand.regHi);
        }
        if (rightOperand.reg != low_reg2) {
            emitpcode(PC_MR, low_reg2, rightOperand.reg);
        }

        if (expr->type == EADD) {
            branch_subroutine(rt_sfpe_d_add, 0, used_regs_r3_r6);
        } else if (expr->type == ESUB) {
            branch_subroutine(rt_sfpe_d_sub, 0, used_regs_r3_r6);
        } else if (expr->type == EMUL) {
            branch_subroutine(rt_sfpe_d_mul, 0, used_regs_r3_r6);
        } else if (expr->type == EDIV) {
            if (((left->rtype->type == TYPEINT || left->rtype->type == TYPEENUM) &&
                 left->rtype->size == 8 &&
                 is_unsigned(left->rtype)) ||
                ((right->rtype->type == TYPEINT || right->rtype->type == TYPEENUM) &&
                 right->rtype->size == 8 &&
                 is_unsigned(right->rtype))) {
                PPCError_Error(PPCERR_FLOAT_CHECKED_AT, __FILE__, 269);
                branch_subroutine(rt_sfpe_d_div, 0, used_regs_r3_r6);
            } else {
                branch_subroutine(rt_sfpe_d_div, 0, used_regs_r3_r6);
            }
        } else {
            CError_FATAL(276);
        }

        output->optype = OpndType_GPRPair;
        output->reg = ALLOC_GPR();
        output->regHi = ALLOC_GPR();
        emitpcode(PC_MR, output->reg, low_reg);
        emitpcode(PC_MR, output->regHi, high_reg);
    }
}

void SFPE_gen_FLOATCONST(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
#if VERSION == VERSION_WII_1_0RC1
    if (TYPE_IS_DPFP(expr->rtype)) {
        const CInt64 bits = expr->data.intval;
        const UInt32 words[] = {bits.lo, bits.hi};
        output->optype = OpndType_GPR;
        output->reg = outputReg ? outputReg : ALLOC_GPR();
        load_imm_vector(output->reg, words);
        return;
    }
#endif
    if (expr->rtype->size == 4) {
        union {
            float f;
            SInt32 l;
        } u;
        int reg;
        reg = outputReg ? outputReg : ALLOC_GPR();
        u.f = expr->data.floatval.data.value;
        load_immediate_SFPE(reg, u.l);
        output->optype = OpndType_GPR;
        output->reg = reg;
    } else {
        int regHi;
        int reg;
        CInt64 v;
        reg = outputReg ? outputReg : ALLOC_GPR();
        regHi = outputRegHi ? outputRegHi : ALLOC_GPR();
        v = expr->data.intval;
        load_immediate_SFPE(regHi, v.lo);
        load_immediate_SFPE(reg, v.hi);
        output->optype = OpndType_GPRPair;
        output->reg = reg;
        output->regHi = regHi;
    }
}

void SFPE_gen_MONMIN(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    Operand operand;
    ENode* subexpr;

    subexpr = expr->data.monadic;
#if VERSION == VERSION_WII_1_0RC1
    if (SFPE_try_fused(subexpr, TRUE, outputReg, output)) {
        return;
    }
#endif
    memclrw(&operand, sizeof(operand));
    if (expr->rtype->size == 4) {
        SInt32 resultReg;
        GEN_NODE(subexpr, &operand);
        if (operand.optype) {
            Coerce_to_register(&operand, subexpr->rtype, 0);
        }
        resultReg = outputReg ? outputReg : ALLOC_GPR();
        emitpcode(PC_XORIS, resultReg, operand.reg, 0x8000);
        output->optype = OpndType_GPR;
        output->reg = resultReg;
    } else {
        SInt32 resultLowReg, resultHighReg;
        GEN_NODE(subexpr, &operand);
#if VERSION == VERSION_WII_1_0RC1
        if (TYPE_IS_DPFP(expr->rtype)) {
            Coerce_to_spe_register(&operand, subexpr->rtype, 0);
            output->optype = OpndType_GPR;
            output->reg = outputReg ? outputReg : ALLOC_GPR();
            emitpcode(PC_EFDNEG, output->reg, operand.reg);
            return;
        }
#endif
        coerce_to_register_pair(&operand, subexpr->rtype, 0, 0);
        resultHighReg = outputRegHi ? outputRegHi : ALLOC_GPR();
        emitpcode(PC_XORIS, resultHighReg, operand.regHi, 0x8000);
        resultLowReg = outputReg ? outputReg : ALLOC_GPR();
        if (resultLowReg != operand.reg) {
            emitpcode(PC_MR, resultLowReg, operand.reg);
        }
        output->optype = OpndType_GPRPair;
        output->reg = resultLowReg;
        output->regHi = resultHighReg;
    }
}

void SFPE_gen_ASS(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    VarInfo* vi;
    Type* type;
    ENode* left;
    ENode* right;
    Operand opleft;
    Operand opright;
    Operand op2;
    SInt32 incval;

    type = expr->rtype;
    if (ENODE_IS(expr, ECONDASS)) {
        left = expr->data.cond.expr1;
        if (ENODE_IS(left, EINDIRECT)) {
            left = left->data.monadic;
        } else {
            CError_FATAL(413);
        }
        right = expr->data.cond.expr2;
    } else {
        left = expr->data.diadic.left;
        right = expr->data.diadic.right;
    }

    memclrw(&opleft, sizeof(Operand));
    memclrw(&opright, sizeof(Operand));
    memclrw(&op2, sizeof(Operand));

    if (ENODE_IS(left, EOBJREF) && OBJECT_REG(left->data.objref)) {
        vi = Registers_GetVarInfo(left->data.objref);
        GEN_NODE_TO_REG(right, vi->reg, vi->regHi, &opright);
#if VERSION == VERSION_WII_1_0RC1
        if (TYPE_IS_DPFP(type)) {
            Coerce_to_spe_register(&opright, type, vi->reg);
            if (opright.reg != vi->reg) {
                emitpcode(PC_EVMR, vi->reg, opright.reg);
            }
            output->optype = OpndType_GPR;
            output->reg = vi->reg;
            return;
        }
#endif
        if (type->size == 4) {
            ENSURE_GPR(&opright, type, vi->reg);
            if (opright.reg != vi->reg) {
                emitpcode(PC_MR, vi->reg, opright.reg);
            }
            output->optype = OpndType_GPR;
            output->reg = vi->reg;
        } else {
            coerce_to_register_pair(&opright, type, vi->reg, vi->regHi);
            if (opright.reg != vi->reg) {
                emitpcode(PC_MR, vi->reg, opright.reg);
            }
            if (opright.regHi != vi->regHi) {
                emitpcode(PC_MR, vi->regHi, opright.regHi);
            }
            output->optype = OpndType_GPRPair;
            output->reg = vi->reg;
            output->regHi = vi->regHi;
        }
    } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
        GEN_NODE(right, &opright);
#if VERSION == VERSION_WII_1_0RC1
        if (TYPE_IS_DPFP(type)) {
            Boolean postincrement;
            Coerce_to_spe_register(&opright, type, 0);
            CError_ASSERT(438, !ENODE_IS(left, EBITFIELD));
            postincrement = ispostincrementopportunity(left, &opleft, &incval);
            if (!postincrement) {
                GEN_NODE(left, &opleft);
            }
            indirect(&opleft, left);
            store_spe(opright.reg, &opleft, type);
            if (postincrement) {
                add_register_immediate(opleft.reg, opleft.reg, incval);
            }
            output->optype = OpndType_GPR;
            output->reg = opright.reg;
            return;
        }
#endif
        if (type->size == 4) {
            ENSURE_GPR(&opright, right->rtype, 0);
            if (ispostincrementopportunity(left, &opleft, &incval)) {
                indirect(&opleft, left);
                store(opright.reg, &opleft, type);
                add_register_immediate(opleft.reg, opleft.reg, incval);
            } else {
                GEN_NODE(left, &opleft);
                indirect(&opleft, left);
                store(opright.reg, &opleft, type);
            }
            output->optype = OpndType_GPR;
            output->reg = opright.reg;
        } else {
            coerce_to_register_pair(&opright, right->rtype, 0, 0);
            if (ENODE_IS(left, EBITFIELD)) {
                CError_FATAL(489);
            }
            if (ispostincrementopportunity(left, &opleft, &incval)) {
                indirect(&opleft, left);
                store_pair(opright.reg, opright.regHi, &opleft, type);
                add_register_immediate(opleft.reg, opleft.reg, incval);
            } else {
                GEN_NODE(left, &opleft);
                indirect(&opleft, left);
                store_pair(opright.reg, opright.regHi, &opleft, type);
            }
            output->optype = OpndType_GPRPair;
            output->reg = opright.reg;
            output->regHi = opright.regHi;
        }
    } else if (TYPE_FITS_IN_REGISTER(type)) {
        CError_FATAL(511);
    }
}

void SFPE_gen_POSTINCDEC(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* inner;
    Type* type;
    int sourceReg;
    int sourceReg2;
    int argumentReg;
    int argumentReg2;
    Operand operand;
    Operand value;

    inner = expr->data.monadic->data.monadic;
    type = expr->rtype;
    memclrw(&operand, sizeof(operand));
    memclrw(&value, sizeof(value));

#if VERSION == VERSION_WII_1_0RC1
    if (TYPE_IS_DPFP(type)) {
        output->optype = OpndType_GPR;
        if (ENODE_IS(inner, EOBJREF) && (sourceReg = OBJECT_REG(inner->data.objref)) != 0) {
            output->reg = ALLOC_GPR();
            emitpcode(PC_EVMR, output->reg, sourceReg);
            SFPE_increment_double(sourceReg, expr->type == EPOSTDEC, sourceReg);
        } else {
            short updated;
            CError_ASSERT(543, !ENODE_IS(inner, EBITFIELD));
            GEN_NODE(inner, &operand);
            indirect(&operand, inner);
            value = operand;
            Coerce_to_spe_register(&value, type, 0);
            output->reg = value.reg;
            updated = ALLOC_GPR();
            SFPE_increment_double(value.reg, expr->type == EPOSTDEC, updated);
            store_spe(updated, &operand, type);
        }
        return;
    }
#endif

    if (type->size == 4) {
        if (inner->type == EOBJREF && (sourceReg = OBJECT_REG(inner->data.objref)) != 0) {
            Registers_GetVarInfo(inner->data.objref);
            output->optype = OpndType_GPR;
            output->reg = (outputReg != 0 && outputReg != sourceReg)
                ? outputReg
                : ALLOC_GPR();
            CError_ASSERT(546, sourceReg != output->reg);
            emitpcode(PC_MR, output->reg, sourceReg);
            argumentReg = ALLOC_GPR();
            LoadFloatArg(argumentReg);
#if VERSION == VERSION_WII_1_0RC1
            if (USE_SPE_FLOAT(4)) {
                emitpcode(expr->type == EPOSTINC ? PC_EFSADD : PC_EFSSUB, sourceReg, sourceReg, argumentReg);
                return;
            }
#endif
            if (sourceReg != 3) {
                emitpcode(PC_MR, 3, sourceReg);
            }
            if (argumentReg != 4) {
                emitpcode(PC_MR, 4, argumentReg);
            }
            if (expr->type == EPOSTINC) {
                branch_subroutine(rt_sfpe_f_add, 0, used_regs_r3_r4);
            } else {
                branch_subroutine(rt_sfpe_f_sub, 0, used_regs_r3_r4);
            }
            emitpcode(PC_MR, sourceReg, 3);
        } else {
            if (inner->type == EBITFIELD) {
                CError_FATAL(590);
            }
            GEN_NODE(inner, &operand);
            indirect(&operand, inner);
            value = operand;
            if (value.optype != OpndType_GPR) {
                Coerce_to_register(&value, type, 0);
            }
            output->optype = OpndType_GPR;
            output->reg = ALLOC_GPR();
            argumentReg = ALLOC_GPR();
            LoadFloatArgGPR(argumentReg);
            emitpcode(PC_MR, output->reg, value.reg);
#if VERSION == VERSION_WII_1_0RC1
            if (USE_SPE_FLOAT(4)) {
                short reg = ALLOC_GPR();
                emitpcode(expr->type == EPOSTINC ? PC_EFSADD : PC_EFSSUB, reg, value.reg, argumentReg);
                store(reg, &operand, type);
                return;
            }
#endif
            {
                SInt32 reg = value.reg;
                if (reg != 3) {
                    emitpcode(PC_MR, 3, reg);
                }
            }
            if (argumentReg != 4) {
                emitpcode(PC_MR, 4, argumentReg);
            }
            if (expr->type == EPOSTINC) {
                branch_subroutine(rt_sfpe_f_add, 0, used_regs_r3_r4);
            } else {
                branch_subroutine(rt_sfpe_f_sub, 0, used_regs_r3_r4);
            }
            store(3, &operand, type);
        }
    } else {
        if (inner->type == EOBJREF && (sourceReg = OBJECT_REG(inner->data.objref)) != 0) {
            sourceReg2 = Registers_GetVarInfo(inner->data.objref)->regHi;
            output->optype = OpndType_GPRPair;
            output->regHi =
                (outputRegHi != 0 && (outputRegHi != sourceReg2 && outputRegHi != sourceReg))
                ? outputRegHi
                : ALLOC_GPR();
            output->reg = (outputReg != 0 && (outputReg != sourceReg && outputReg != sourceReg2))
                ? outputReg
                : ALLOC_GPR();
            CError_ASSERT(654, sourceReg != output->reg && sourceReg2 != output->reg);
            CError_ASSERT(655, sourceReg != output->regHi && sourceReg2 != output->regHi);
            emitpcode(PC_MR, output->regHi, sourceReg2);
            emitpcode(PC_MR, output->reg, sourceReg);
            if (sourceReg2 != high_reg) {
                emitpcode(PC_MR, high_reg, sourceReg2);
            }
            if (sourceReg != low_reg) {
                emitpcode(PC_MR, low_reg, sourceReg);
            }
            argumentReg = ALLOC_GPR();
            argumentReg2 = ALLOC_GPR();
            LoadDoubleArg(argumentReg, argumentReg2);
            if (expr->type == EPOSTINC) {
                branch_subroutine(rt_sfpe_d_add, 0, used_regs_r3_r6);
            } else {
                branch_subroutine(rt_sfpe_d_sub, 0, used_regs_r3_r6);
            }
            emitpcode(PC_MR, sourceReg2, high_reg);
            emitpcode(PC_MR, sourceReg, low_reg);
        } else {
            if (inner->type == EBITFIELD) {
                CError_FATAL(690);
            }
            GEN_NODE(inner, &operand);
            indirect(&operand, inner);
            value = operand;
            coerce_to_register_pair(&value, type, 0, 0);
            output->optype = OpndType_GPRPair;
            output->reg = ALLOC_GPR();
            output->regHi = ALLOC_GPR();
            emitpcode(PC_MR, output->reg, value.reg);
            emitpcode(PC_MR, output->regHi, value.regHi);
            if (value.regHi != high_reg) {
                emitpcode(PC_MR, high_reg, value.regHi);
            }
            if (value.reg != low_reg) {
                emitpcode(PC_MR, low_reg, value.reg);
            }
            argumentReg = ALLOC_GPR();
            argumentReg2 = ALLOC_GPR();
            LoadDoubleArg(argumentReg, argumentReg2);
            if (expr->type == EPOSTINC) {
                branch_subroutine(rt_sfpe_d_add, 0, used_regs_r3_r6);
            } else {
                branch_subroutine(rt_sfpe_d_sub, 0, used_regs_r3_r6);
            }
            store_pair(low_reg, high_reg, &operand, type);
        }
    }
}

void SFPE_gen_INDIRECT(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    Type* type;
    ENode* inner;
    VarInfo* vi;
    Operand op;
    SInt32 postincvalue;

    type = expr->rtype;
    inner = expr->data.monadic;

    memclrw(&op, sizeof(Operand));
    if (ENODE_IS(inner, EOBJREF) && OBJECT_REG(inner->data.objref)) {
        vi = Registers_GetVarInfo(inner->data.objref);
        if (type->size == 4 || TYPE_IS_DPFP(type)) {
            output->optype = OpndType_GPR;
            output->reg = vi->reg;
            output->object = NULL;
        } else {
            output->optype = OpndType_GPRPair;
            output->reg = vi->reg;
            output->regHi = vi->regHi;
            output->object = NULL;
        }
    } else if (ENODE_IS(inner, EBITFIELD)) {
        CError_FATAL(763);
    } else if (ispostincrementopportunity(inner, &op, &postincvalue) &&
               (TYPE_FITS_IN_REGISTER(type) || IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type))) {
        indirect(&op, inner);
        *output = op;
        if (type->size == 8) {
            coerce_to_register_pair(output, type, outputReg, outputRegHi);
            add_register_immediate(op.reg, op.reg, postincvalue);
        } else {
            ENSURE_GPR(output, type, outputReg);
            add_register_immediate(op.reg, op.reg, postincvalue);
        }
    } else {
        GEN_NODE(inner, output);
        indirect(output, inner);
    }
}

void SFPE_gen_condition(ENode* expr, Operand* output, int branch) {
    ENode* left;
    ENode* right;
    Operand leftOperand, rightOperand, tempOperand;
    SInt32 size;
    UInt32* masks;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;

    memclrw(&leftOperand, sizeof(leftOperand));
    memclrw(&rightOperand, sizeof(rightOperand));
    memclrw(&tempOperand, sizeof(tempOperand));

    size = right->rtype->size;
    if (left->rtype->size != right->rtype->size) {
        CError_FATAL(820);
    }
    if (size < 4 || size > 8) {
        CError_FATAL(823);
    }

#if VERSION == VERSION_WII_1_0RC1
    if (TYPE_IS_DPFP(right->rtype)) {
        Boolean rightFirst = right->hascall;
        ENode* first = rightFirst ? right : left;
        ENode* second = rightFirst ? left : right;
        Operand* firstOperand = rightFirst ? &rightOperand : &leftOperand;
        Operand* secondOperand = rightFirst ? &leftOperand : &rightOperand;
        GEN_NODE(first, firstOperand);
        Coerce_to_spe_register(firstOperand, first->rtype, 0);
        GEN_NODE(second, secondOperand);
        Coerce_to_spe_register(secondOperand, second->rtype, 0);
        SFPE_compare_spe(expr->type, TRUE, leftOperand.reg, rightOperand.reg, output, branch);
        return;
    }
#endif

    if (right->hascall) {
        GEN_NODE(right, &rightOperand);
        ENSURE_GPR(&rightOperand, right->rtype, 0);
        if (right->rtype->size < size) {
            CError_FATAL(832);
        }

        GEN_NODE(left, &leftOperand);
        ENSURE_GPR(&leftOperand, left->rtype, 0);
        if (left->rtype->size < size) {
            CError_FATAL(839);
        }
    } else {
        GEN_NODE(left, &leftOperand);
        ENSURE_GPR(&leftOperand, left->rtype, 0);
        if (left->rtype->size < size) {
            CError_FATAL(847);
        }

        GEN_NODE(right, &rightOperand);
        ENSURE_GPR(&rightOperand, right->rtype, 0);
        if (right->rtype->size < size) {
            CError_FATAL(854);
        }
    }

    if (size == 8) {
        CError_ASSERT(
            859, leftOperand.optype == OpndType_GPRPair && rightOperand.optype == OpndType_GPRPair
        );
    }
    if (size == 4) {
        CError_ASSERT(
            861, leftOperand.optype == OpndType_GPR && rightOperand.optype == OpndType_GPR
        );
    }

#if VERSION == VERSION_WII_1_0RC1
    if (size == 4 && USE_SPE_FLOAT(4)) {
        SFPE_compare_spe(expr->type, FALSE, leftOperand.reg, rightOperand.reg, output, branch);
        return;
    }
#endif

    if (size == 8) {
        masks = used_regs_r3_r6;
        if (leftOperand.regHi != high_reg) {
            emitpcode(PC_MR, high_reg, leftOperand.regHi);
        }
        if (leftOperand.reg != low_reg) {
            emitpcode(PC_MR, low_reg, leftOperand.reg);
        }
        if (rightOperand.regHi != high_reg2) {
            emitpcode(PC_MR, high_reg2, rightOperand.regHi);
        }
        if (rightOperand.reg != low_reg2) {
            emitpcode(PC_MR, low_reg2, rightOperand.reg);
        }
    } else {
        masks = used_regs_r3_r4;
        if ((SInt32)leftOperand.reg != 3) {
            emitpcode(PC_MR, 3, leftOperand.reg);
        }
        if ((SInt32)rightOperand.reg != 4) {
            emitpcode(PC_MR, 4, rightOperand.reg);
        }
    }

    switch (expr->type) {
        case ENOTEQU:
            branch_subroutine(size == 8 ? rt_sfpe_d_fne : rt_sfpe_f_fne, 0, masks);
            break;
        case EEQU:
            branch_subroutine(size == 8 ? rt_sfpe_d_feq : rt_sfpe_f_feq, 0, masks);
            break;
        case EGREATER:
            branch_subroutine(size == 8 ? rt_sfpe_d_fgt : rt_sfpe_f_fgt, 0, masks);
            break;
        case ELESS:
            branch_subroutine(size == 8 ? rt_sfpe_d_flt : rt_sfpe_f_flt, 0, masks);
            break;
        case ELESSEQU:
            branch_subroutine(size == 8 ? rt_sfpe_d_fle : rt_sfpe_f_fle, 0, masks);
            break;
        case EGREATEREQU:
            branch_subroutine(size == 8 ? rt_sfpe_d_fge : rt_sfpe_f_fge, 0, masks);
            break;
        default:
            CError_FATAL(963);
    }

    if (branch) {
        output->optype = OpndType_GPR;
        output->reg = ALLOC_GPR();
        emitpcode(PC_MR, output->reg, 3);
    } else {
        emitpcode(PC_CMPI, 0, 3, 1);
        output->optype = OpndType_CRField;
        output->reg = 0;
        output->regOffset = EEQU;
    }
}

void SFPE_gen_TYPCON(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* inner;
    Type* srctype;
    Type* dsttype;

    inner = expr->data.monadic;
    srctype = inner->rtype;
    dsttype = expr->rtype;

#if VERSION == VERSION_WII_1_0RC1
    if (IS_TYPE_INT_OR_ENUM(srctype) && srctype->size <= 4 &&
        TYPE_IS_SOFT_FLOAT(dsttype) && USE_SPE_FLOAT(dsttype->size)) {
        Boolean wide = dsttype->size != 4;
        Boolean isUnsigned = is_unsigned(srctype);
        short reg = outputReg ? outputReg : ALLOC_GPR();
        short opcode = wide ? (isUnsigned ? PC_EFDCFUI : PC_EFDCFSI)
                            : (isUnsigned ? PC_EFSCFUI : PC_EFSCFSI);
        GEN_NODE(inner, output);
        if (srctype->size < 4) {
            extend32(output, srctype, 0);
        }
        ENSURE_GPR(output, srctype, 0);
        emitpcode(opcode, reg, output->reg);
        output->optype = OpndType_GPR;
        output->reg = reg;
        return;
    }
    if (IS_TYPE_INT_OR_ENUM(dsttype) && dsttype->size <= 4 &&
        TYPE_IS_SOFT_FLOAT(srctype) && USE_SPE_FLOAT(srctype->size)) {
        Boolean wide = srctype->size != 4;
        Boolean isUnsigned = is_unsigned(dsttype);
        short reg = outputReg ? outputReg : ALLOC_GPR();
        short opcode = wide ? (isUnsigned ? PC_EFDCTUIZ : PC_EFDCTSIZ)
                            : (isUnsigned ? PC_EFSCTUIZ : PC_EFSCTSIZ);
        GEN_NODE(inner, output);
        if (wide) {
            Coerce_to_spe_register(output, srctype, 0);
        } else {
            ENSURE_GPR(output, srctype, 0);
        }
        emitpcode(opcode, reg, output->reg);
        output->optype = OpndType_GPR;
        output->reg = reg;
        return;
    }
#endif

    if (IS_TYPE_VOID(dsttype)) {
        PPCError_Error(PPCERR_FLOAT_CHECKED_AT, __FILE__, 1001);
        GEN_NODE(inner, output);
        output->optype = OpndType_Absolute;
        output->immediate = 0;
    } else if (IS_TYPE_INT_OR_ENUM(srctype)) {
        if (TYPE_IS_SOFT_FLOAT(dsttype) && TYPE_IS_8BYTES(srctype)) {
            GEN_NODE(inner, output);
            coerce_to_register_pair(output, srctype, 0, 0);
            if (output->optype != OpndType_GPRPair) {
                CError_FATAL(1020);
            }
            if (output->regHi != high_reg) {
                emitpcode(PC_MR, high_reg, output->regHi);
            }
            if (output->reg != low_reg) {
                emitpcode(PC_MR, low_reg, output->reg);
            }
            branch_subroutine(
                is_unsigned(srctype) ? (dsttype->size == 4 ? rt_sfpe_f_ulltof : rt_sfpe_d_ulltod)
                                     : (dsttype->size == 4 ? rt_sfpe_f_lltof : rt_sfpe_d_lltod),
                0,
                used_regs_r3_r4
            );
            if (dsttype->size == 4) {
                lift_result(output, OpndType_GPR);
            } else {
                lift_result(output, OpndType_GPRPair);
            }
        } else if (TYPE_IS_SOFT_FLOAT(dsttype)) {
            GEN_NODE(inner, output);
            if (srctype->size < 4) {
                extend32(output, srctype, 0);
            }
            ENSURE_GPR(output, srctype, 0);
            {
                SInt32 reg = output->reg;
                if (reg != 3) {
                    emitpcode(PC_MR, 3, reg);
                }
            }
            branch_subroutine(
                is_unsigned(srctype) ? (dsttype->size == 4 ? rt_sfpe_f_utof : rt_sfpe_d_utod)
                                     : (dsttype->size == 4 ? rt_sfpe_f_itof : rt_sfpe_d_itod),
                0,
                used_regs_r3
            );
            if (dsttype->size == 4) {
                lift_result(output, OpndType_GPR);
            } else {
                lift_result(output, OpndType_GPRPair);
            }
        } else {
            CError_FATAL(1089);
        }
    } else if (IS_TYPE_POINTER(srctype)) {
        CError_FATAL(1098);
    } else if (TYPE_IS_SOFT_FLOAT(dsttype)) {
        if (dsttype->size == 4 && srctype->size != 4) {
            GEN_NODE(inner, output);
            coerce_to_register_pair(output, srctype, 0, 0);
            if (output->optype != OpndType_GPRPair) {
                CError_FATAL(1115);
            }
            if (output->regHi != high_reg) {
                emitpcode(PC_MR, high_reg, output->regHi);
            }
            if (output->reg != low_reg) {
                emitpcode(PC_MR, low_reg, output->reg);
            }
            branch_subroutine(rt_sfpe_d_dtof, 0, used_regs_r3_r4);
            lift_result(output, OpndType_GPR);
        } else if (srctype->size == 4 && dsttype->size != 4) {
            GEN_NODE(inner, output);
            ENSURE_GPR(output, srctype, 0);
            {
                SInt32 reg = output->reg;
                if (reg != 3) {
                    emitpcode(PC_MR, 3, reg);
                }
            }
            branch_subroutine(rt_sfpe_f_ftod, 0, used_regs_r3);
#if VERSION == VERSION_WII_1_0RC1
            if (TYPE_IS_DPFP(dsttype)) {
                output->optype = OpndType_GPR;
                output->reg = ALLOC_GPR();
                emitpcode(PC_EVMERGELO, output->reg, high_reg, low_reg);
                return;
            }
#endif
            lift_result(output, OpndType_GPRPair);
        } else {
            if (dsttype->size != srctype->size) {
                CError_FATAL(1152);
            }
            GEN_NODE_TO_REG(inner, outputReg, outputRegHi, output);
            if (srctype->size == 4 && output->optype != OpndType_GPR) {
                ENSURE_GPR(output, srctype, outputReg);
                return;
            }
            if (srctype->size != 4 && output->optype != OpndType_GPRPair) {
                coerce_to_register_pair(output, srctype, outputReg, outputRegHi);
            }
        }
    } else if (TYPE_IS_8BYTES(dsttype) && TYPE_IS_SOFT_FLOAT(srctype) && srctype->size != 4) {
        GEN_NODE(inner, output);
        coerce_to_register_pair(output, srctype, 0, 0);
        if (output->optype != OpndType_GPRPair) {
            CError_FATAL(1175);
        }
        if (output->regHi != high_reg) {
            emitpcode(PC_MR, high_reg, output->regHi);
        }
        if (output->reg != low_reg) {
            emitpcode(PC_MR, low_reg, output->reg);
        }
        if (is_unsigned(expr->rtype)) {
            branch_subroutine(rt_sfpe_d_dtoull, 0, used_regs_r3_r4);
        } else {
            branch_subroutine(rt_sfpe_d_dtoll, 0, used_regs_r3_r4);
        }
        lift_result(output, OpndType_GPRPair);
    } else if (TYPE_IS_8BYTES(dsttype) && TYPE_IS_SOFT_FLOAT(srctype) && srctype->size == 4) {
        GEN_NODE(inner, output);
        ENSURE_GPR(output, srctype, 0);
        {
            SInt32 reg = output->reg;
            if (reg != 3) {
                emitpcode(PC_MR, 3, reg);
            }
        }
        if (is_unsigned(expr->rtype)) {
            branch_subroutine(rt_sfpe_f_ftoull, 0, used_regs_r3);
        } else {
            branch_subroutine(rt_sfpe_f_ftoll, 0, used_regs_r3);
        }
        lift_result(output, OpndType_GPRPair);
    } else if (IS_TYPE_INT_OR_ENUM(dsttype) && TYPE_IS_SOFT_FLOAT(srctype) && srctype->size != 4) {
        GEN_NODE(inner, output);
        coerce_to_register_pair(output, srctype, 0, 0);
        if (output->optype != OpndType_GPRPair) {
            CError_FATAL(1231);
        }
        if (output->regHi != high_reg) {
            emitpcode(PC_MR, high_reg, output->regHi);
        }
        if (output->reg != low_reg) {
            emitpcode(PC_MR, low_reg, output->reg);
        }
        if (is_unsigned(expr->rtype)) {
            branch_subroutine(rt_sfpe_d_dtou, 0, used_regs_r3_r4);
        } else {
            branch_subroutine(rt_sfpe_d_dtoi, 0, used_regs_r3_r4);
        }
        lift_result(output, OpndType_GPR);
    } else if (IS_TYPE_INT_OR_ENUM(dsttype) && TYPE_IS_SOFT_FLOAT(srctype) && srctype->size == 4) {
        GEN_NODE(inner, output);
        ENSURE_GPR(output, srctype, 0);
        {
            SInt32 reg = output->reg;
            if (reg != 3) {
                emitpcode(PC_MR, 3, reg);
            }
        }
        if (is_unsigned(expr->rtype)) {
            branch_subroutine(rt_sfpe_f_ftou, 0, used_regs_r3);
        } else {
            branch_subroutine(rt_sfpe_f_ftoi, 0, used_regs_r3);
        }
        lift_result(output, OpndType_GPR);
    } else {
        CError_FATAL(1280);
    }
}
