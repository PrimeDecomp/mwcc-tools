/* RC1 instruction selection for conditional expressions. */
#include "rc1_codegen.h"

typedef struct PCodeLabel PCodeLabel;
enum {
    PC_ADD = 0x3c, PC_SUBF = 0x4c, PC_AND = 0x5c, PC_XOR = 0x5e, PC_ANDC = 0x62,
    PC_SRAWI = 0x6c, PC_MR = 0x8b, PC_FMR = 0x9e, PC_FNEG = 0xa0, PC_FSUB = 0xa4,
    PC_FSUBS = 0xa5, PC_FSEL = 0xb4, PC_ISEL = 0xed, PC_VMR = 0x190, PC_EVMR = 0x2c7,
};
extern int RC1_next_gpr, RC1_next_fpr, RC1_next_vr;
extern PCodeLabel* makepclabel(void);
extern ENode* evaluate_and_skip_comma(ENode*);
extern u8 TOC_use_fsel(ENode*);
extern u8 TOC_use_isel(ENode*, u8);
extern u8 CMach_FloatIsZero(FloatBits);
extern void RC1_FloatBinaryOperator(short, ENode*, ENode*, short, Operand*);
extern void gen_condition(ENode*, Operand*);
extern void SFPE_gen_condition(ENode*, Operand*, int);
extern void I8_gen_condition(ENode*, Operand*, int);
extern void gen_condition_gpr(ENode*, Operand*, short);
extern void gen_negated_condition_gpr(ENode*, Operand*, short);
extern void RC1_LogicalExpressionNoBranch(ENode*, u8, Operand*);
extern void logical_expression(ENode*, PCodeLabel*, PCodeLabel*, PCodeLabel*);
extern void branch_conditional(short, short, int, PCodeLabel*);
extern void coerce_to_register_pair(Operand*, Type*, short, short);
extern void combine(Operand*, Operand*, short, Operand*);
extern u8 RC1_TryConditionalCompare(ENode*, ENode*, ENode*, short, Operand*);
extern ENode* RC1_MatchConditionalAbsolute(ENode*, ENode*, ENode*);
extern int RC1_ConditionalConstantKind(ENode*, ENode*);
extern Type stunsignedint;

#define ALLOC_GPR() (RC1_next_gpr++)
#define ALLOC_FPR() (RC1_next_fpr++)
#define ALLOC_VR() (RC1_next_vr++)
#define CALL_GEN(n, r, h, o) cgdispatch[(n)->kind]((n), (r), (h), (o))
#define ENSURE_GPR(o, t, r) ENSURE(*(o), OpGPR, Coerce_to_register, t, r)
#define ENSURE_FPR(o, t, r) ENSURE(*(o), OpFPR, Coerce_to_fp_register, t, r)
#define ENSURE_VR(o, t, r) ENSURE(*(o), OpVR, Coerce_to_v_register, t, r)
#define SOFT_FLOAT(t) (RC1_soft_float && SCALAR_FLOAT(t))
#define PAIRED_DOUBLE(t) (RC1_use_paired_moves_for_double && SCALAR_FLOAT(t) && (t)->size != 4)
extern Object* maketemporary(Type*);
extern void symbol_operand(Operand*, Object*);
extern void coerce_to_addressable_before(PCode*, Operand*, short);
extern void branch_always(PCodeLabel*);
extern void branch_label(PCodeLabel*);

static inline void store_aggregate(Operand* output, Operand* source, Type* type, short align) {
    if ((type->kind == StructureType || type->kind == ClassType) && !CMach_PassResultInHiddenArg(type)) {
        if (source->kind == OpGPRPair)
            store_small_block_from_reg_pair(source->reg, source->reg_hi, output, type, align);
        else if (source->kind == OpGPR)
            store_small_block_from_reg(source->reg, output, type, align);
        else
            move_block(output, source, type->size, align);
    } else {
        move_block(output, source, type->size, align);
    }
}

void gen_COND(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* expr1;
    ENode* expr2;
    Type* type;
    ENode* cond;
    PCodeLabel* label1;
    PCodeLabel* label2;
    PCodeLabel* label3;
    Operand op1;
    Operand op2;
    int has_const;
    int reg1;
    int reg2;
    int reg3;
    short align;
    short max_align;

    expr1 = expr->data.conditional.left;
    expr2 = expr->data.conditional.right;
    type = expr->type;

    label1 = makepclabel();
    label2 = makepclabel();
    label3 = makepclabel();

    init_operand(&op1);
    init_operand(&op2);

    cond = evaluate_and_skip_comma(expr->data.conditional.condition);

    if (TOC_use_fsel(expr)) {
        ENode* left;
        ENode* right;
        ENode* swapNode;
        u8 nt;
        u8 leftIsZero;
        Operand op;
        int fneg_reg;
        int fneg_reg2;
        int fneg_reg3;
        int fsel_reg;
        int final_reg;

        left = cond->data.diadic.left;
        right = cond->data.diadic.right;
        nt = cond->kind;
        leftIsZero = 0;
        init_operand(&op);

        switch (nt) {
            case EGREATEREQU:
            case EEQU:
                break;
            case EGREATER:
                swapNode = left;
                left = right;
                right = swapNode;
                /* Fall through to swap the result arms. */
            case ELESS:
            case ENOTEQU:
                swapNode = expr1;
                expr1 = expr2;
                expr2 = swapNode;
                break;
            case ELESSEQU:
                swapNode = left;
                left = right;
                right = swapNode;
                break;
            default:
                CError_Internal(RC1_InstrSelection_source_file, 4022);
        }

        if (IS(left, EFLOATCONST) && CMach_FloatIsZero(left->data.floating)) {
            GEN(right, &op);
            ENSURE_FPR(&op, right->type, -1);
            leftIsZero = 1;
        } else if (IS(right, EFLOATCONST) && CMach_FloatIsZero(right->data.floating)) {
            GEN(left, &op);
            ENSURE_FPR(&op, left->type, -1);
        } else {
            RC1_FloatBinaryOperator((type->size == 4) ? PC_FSUBS : PC_FSUB, left, right, -1, &op);
        }

        switch (cond->kind) {
            case EEQU:
            case ENOTEQU:
                if (leftIsZero) {
                    GEN(expr1, &op1);
                    ENSURE_FPR(&op1, expr1->type, -1);
                    GEN(expr2, &op2);
                    ENSURE_FPR(&op2, expr2->type, -1);

                    fneg_reg = ALLOC_FPR();
                    emitpcode(PC_FNEG, fneg_reg, op.reg);
                    fsel_reg = ALLOC_FPR();
                    emitpcode(PC_FSEL, fsel_reg, op.reg, op1.reg, op2.reg);
                    final_reg = outputReg != -1 ? outputReg : ALLOC_FPR();
                    emitpcode(PC_FSEL, final_reg, fneg_reg, fsel_reg, op2.reg);
                } else {
                    GEN(expr1, &op1);
                    ENSURE_FPR(&op1, expr1->type, -1);
                    GEN(expr2, &op2);
                    ENSURE_FPR(&op2, expr2->type, -1);

                    fneg_reg2 = ALLOC_FPR();
                    emitpcode(PC_FNEG, fneg_reg2, op.reg);
                    fsel_reg = ALLOC_FPR();
                    emitpcode(PC_FSEL, fsel_reg, op.reg, op1.reg, op2.reg);
                    final_reg = outputReg != -1 ? outputReg : ALLOC_FPR();
                    emitpcode(PC_FSEL, final_reg, fneg_reg2, fsel_reg, op2.reg);
                }
                break;
            case ELESS:
            case EGREATER:
            case ELESSEQU:
            case EGREATEREQU:
                GEN(expr1, &op1);
                ENSURE_FPR(&op1, expr1->type, -1);
                GEN(expr2, &op2);
                ENSURE_FPR(&op2, expr2->type, -1);

                fneg_reg3 = op.reg;
                if (leftIsZero) {
                    fneg_reg3 = ALLOC_FPR();
                    emitpcode(PC_FNEG, fneg_reg3, op.reg);
                }

                final_reg = outputReg != -1 ? outputReg : ALLOC_FPR();
                emitpcode(PC_FSEL, final_reg, fneg_reg3, op1.reg, op2.reg);
                break;
            default:
                CError_Internal(RC1_InstrSelection_source_file, 4104);
        }

        output->kind = OpFPR;
        output->reg = final_reg;
        return;
    }

    if (TOC_use_isel(expr, 0)) {
        Operand condOp;
        Operand swapOp;
        int bit;
        int swap;

        init_operand(&condOp);
        if (SOFT_FLOAT(cond->data.diadic.right->type) || SOFT_FLOAT(cond->data.diadic.left->type))
            SFPE_gen_condition(cond, &condOp, 0);
        else if (IS_64BIT_INT(cond->data.diadic.right->type) || IS_64BIT_INT(cond->data.diadic.left->type))
            I8_gen_condition(cond, &condOp, 0);
        else
            gen_condition(cond, &condOp);
        GEN(expr1, &op1);
        ENSURE_GPR(&op1, expr1->type, -1);
        GEN(expr2, &op2);
        ENSURE_GPR(&op2, expr2->type, -1);

        reg1 = outputReg != -1 ? outputReg : ALLOC_GPR();
        bit = 0;
        swap = 0;
        switch (condOp.reg_offset) {
            case ENOTEQU:
                swap = 1;
                /* Fall through to select the inverted condition bit. */
            case EEQU:
                bit = 2;
                break;
            case EGREATEREQU:
                swap = 1;
                /* Fall through to select the inverted condition bit. */
            case ELESS:
                bit = 0;
                break;
            case ELESSEQU:
                swap = 1;
                /* Fall through to select the inverted condition bit. */
            case EGREATER:
                bit = 1;
                break;
        }
        if (swap) {
            swapOp = op1;
            op1 = op2;
            op2 = swapOp;
        }
        emitpcode(PC_ISEL, reg1, op1.reg, op2.reg, condOp.reg, bit);
        output->kind = OpGPR;
        output->reg = reg1;
        return;
    }

    if (TOC_use_isel(expr, 1)) {
        Operand isel_op1;
        Operand isel_op2;
        ENode* x;
        ENode* y;
        ENode* abs_expr;

        init_operand(&isel_op1);
        init_operand(&isel_op2);

        if (RC1_TryConditionalCompare(cond, expr1, expr2, outputReg, output)) {
            return;
        }

        if ((abs_expr = RC1_MatchConditionalAbsolute(cond, expr1, expr2))) {
            if (IS(expr1, EADD) && IS(expr2, ESUB)) {
                x = expr1->data.diadic.left;
                y = expr2->data.diadic.right;
                if (y->hascall) {
                    GEN(y, &op2);
                    ENSURE_GPR(&op2, y->type, -1);

                    GEN(x, &op1);
                    if (op1.kind >= OpIndirect) {
                        ENSURE_GPR(&op1, x->type, -1);
                    }
                } else {
                    GEN(x, &op1);
                    if (op1.kind >= OpIndirect) {
                        ENSURE_GPR(&op1, x->type, -1);
                    }

                    GEN(y, &op2);
                    ENSURE_GPR(&op2, y->type, -1);
                }

                reg1 = ALLOC_GPR();
                emitpcode(PC_SRAWI, reg1, op2.reg, 31);
                reg2 = ALLOC_GPR();
                emitpcode(PC_XOR, reg2, reg1, op2.reg);
                reg3 = ALLOC_GPR();
                emitpcode(PC_SUBF, reg3, reg1, reg2);
                op2.kind = OpGPR;
                op2.reg = reg3;
                combine(&op1, &op2, outputReg, output);
            } else {
                GEN(abs_expr, output);
                ENSURE_GPR(output, abs_expr->type, -1);

                reg1 = ALLOC_GPR();
                emitpcode(PC_SRAWI, reg1, output->reg, 31);
                reg2 = ALLOC_GPR();
                emitpcode(PC_XOR, reg2, reg1, output->reg);
                reg3 = outputReg != -1 ? outputReg : ALLOC_GPR();
                emitpcode(PC_SUBF, reg3, reg1, reg2);
                output->kind = OpGPR;
                output->reg = reg3;
            }
            return;
        }

        if ((has_const = RC1_ConditionalConstantKind(expr1, expr2))) {
            switch (RC1_ConditionalConstantKind(expr1, expr2)) {
                case 0:
                case 2:
                    break;
                case 3:
                case 4:
                    if (has_const == 4) {
                        if (expr1->data.integer.low < expr2->data.integer.low) {
                            gen_negated_condition_gpr(cond, &isel_op1, -1);
                        } else {
                            gen_condition_gpr(cond, &isel_op1, -1);
                        }

                        GEN(expr1, &op1);
                        GEN(expr2, &op2);
                        reg1 = ALLOC_GPR();
                        ENSURE_GPR(&op2, expr2->type, reg1);
                        emitpcode(PC_ADD, reg1, isel_op1.reg, op2.reg);
                        if (outputReg != -1) {
                            emitpcode(PC_MR, reg2 = outputReg, reg1);
                            reg1 = reg2;
                        }
                        output->kind = OpGPR;
                        output->reg = reg1;
                        return;
                    }
                    break;
                case 5:
                case 6:
                    gen_negated_condition_gpr(cond, &isel_op1, -1);
                    ENSURE_GPR(&isel_op1, (Type*)&stunsignedint, -1);
                    GEN(expr1, &op1);
                    GEN(expr2, &op2);

                    reg1 = outputReg != -1 ? outputReg : ALLOC_GPR();
                    if (op1.kind == OpAbsolute && op1.immediate == 0) {
                        ENSURE_GPR(&op2, expr2->type, -1);
                        emitpcode(PC_ANDC, reg1, op2.reg, isel_op1.reg);
                    } else if (op2.kind == OpAbsolute && op2.immediate == 0) {
                        ENSURE_GPR(&op1, expr1->type, -1);
                        emitpcode(PC_AND, reg1, op1.reg, isel_op1.reg);
                    } else {
                        CError_Internal(RC1_InstrSelection_source_file, 4363);
                    }

                    output->kind = OpGPR;
                    output->reg = reg1;
                    return;
                case 1:
                    reg2 = ALLOC_GPR();
                    reg1 = reg2;
                    RC1_LogicalExpressionNoBranch(cond, 0, &isel_op2);

                    CALL_GEN(expr1, reg1, -1, &op1);
                    ENSURE_GPR(&op1, expr1->type, reg1);
                    if (op1.reg != reg1) {
                        emitpcode(PC_MR, reg1, op1.reg);
                    }

                    branch_conditional(isel_op2.reg, isel_op2.reg_offset, 1, label2);
                    branch_label(label1);

                    CALL_GEN(expr2, reg1, -1, &op2);
                    ENSURE_GPR(&op2, expr2->type, reg1);
                    if (op2.reg != reg1) {
                        emitpcode(PC_MR, reg1, op2.reg);
                    }

                    branch_label(label2);

                    if (outputReg != -1) {
                        emitpcode(PC_MR, reg2 = outputReg, reg1);
                        reg1 = reg2;
                    }

                    output->kind = OpGPR;
                    output->reg = reg1;
                    return;

                default:
                    CError_Internal(RC1_InstrSelection_source_file, 4412);
            }
        }

        reg1 = ALLOC_GPR();
        RC1_LogicalExpressionNoBranch(cond, 0, &isel_op2);

        CALL_GEN(expr2, reg1, -1, &op2);
        ENSURE_GPR(&op2, expr2->type, reg1);
        if (op2.reg != reg1) {
            emitpcode(PC_MR, reg1, op2.reg);
        }

        branch_conditional(isel_op2.reg, isel_op2.reg_offset, 0, label2);
        branch_label(label1);

        CALL_GEN(expr1, reg1, -1, &op1);
        ENSURE_GPR(&op1, expr1->type, reg1);
        if (op1.reg != reg1) {
            emitpcode(PC_MR, reg1, op1.reg);
        }

        branch_label(label2);

        if (outputReg != -1) {
            emitpcode(PC_MR, reg2 = outputReg, reg1);
            reg1 = reg2;
        }

        output->kind = OpGPR;
        output->reg = reg1;
        return;
    }

    logical_expression(cond, label1, label2, label1);
    branch_label(label1);

    if (type->kind == VoidType || expr->ignored) {
        GEN(expr1, &op1);
        branch_always(label3);
        branch_label(label2);
        GEN(expr2, &op2);
    } else if (SCALAR_FLOAT(type) && !SOFT_FLOAT(type)) {
        if (expr1->hascall || expr2->hascall) {
            reg1 = ALLOC_FPR();
        } else {
            reg1 = outputReg != -1 ? outputReg : ALLOC_FPR();
        }

        CALL_GEN(expr1, reg1, -1, &op1);
        ENSURE_FPR(&op1, expr1->type, reg1);
        if (op1.reg != reg1) {
            emitpcode(PC_FMR, reg1, op1.reg);
        }

        branch_always(label3);
        branch_label(label2);

        CALL_GEN(expr2, reg1, -1, &op2);
        ENSURE_FPR(&op2, expr2->type, reg1);
        if (op2.reg != reg1) {
            emitpcode(PC_FMR, reg1, op2.reg);
        }

        output->kind = OpFPR;
        output->reg = reg1;
    } else if (IS_64BIT_INT(type) || (SOFT_FLOAT(type) && type->size != 4 && !PAIRED_DOUBLE(type))) {
        if (expr1->hascall || expr2->hascall) {
            reg1 = ALLOC_GPR();
            reg3 = ALLOC_GPR();
            reg2 = reg3;
        } else {
            reg1 = outputReg != -1 ? outputReg : ALLOC_GPR();
            reg3 = outputRegHi != -1 ? outputRegHi : ALLOC_GPR();
            reg2 = reg3;
        }

        CALL_GEN(expr1, reg1, reg2, &op1);
        coerce_to_register_pair(&op1, expr1->type, reg1, reg2);

        branch_always(label3);
        branch_label(label2);

        CALL_GEN(expr2, reg1, reg2, &op2);
        coerce_to_register_pair(&op2, expr2->type, reg1, reg2);

        output->kind = OpGPRPair;
        output->reg = reg1;
        output->reg_hi = reg2;
    } else if (SCALAR_TYPE(type) || (SOFT_FLOAT(type) && type->size == 4) || PAIRED_DOUBLE(type)) {
        if (expr1->hascall || expr2->hascall) {
            reg1 = ALLOC_GPR();
        } else {
            reg1 = outputReg != -1 ? outputReg : ALLOC_GPR();
        }

        CALL_GEN(expr1, reg1, -1, &op1);
        ENSURE_GPR(&op1, expr1->type, reg1);
        if (op1.reg != reg1) {
            if (PAIRED_DOUBLE(type))
                emitpcode(PC_EVMR, reg1, op1.reg);
            else
                emitpcode(PC_MR, reg1, op1.reg);
        }

        branch_always(label3);
        branch_label(label2);

        CALL_GEN(expr2, reg1, -1, &op2);
        ENSURE_GPR(&op2, expr2->type, reg1);
        if (op2.reg != reg1) {
            emitpcode(PAIRED_DOUBLE(type) ? PC_EVMR : PC_MR, reg1, op2.reg);
        }

        output->kind = OpGPR;
        output->reg = reg1;
    } else if ((VECTOR(type) && type->size == 16)) {
        if (expr1->hascall || expr2->hascall) {
            reg1 = ALLOC_VR();
        } else {
            reg1 = outputReg != -1 ? outputReg : ALLOC_VR();
        }

        CALL_GEN(expr1, reg1, -1, &op1);
        ENSURE_VR(&op1, expr1->type, reg1);
        if (op1.reg != reg1) {
            emitpcode(PC_VMR, reg1, op1.reg);
        }

        branch_always(label3);
        branch_label(label2);

        CALL_GEN(expr2, reg1, -1, &op2);
        ENSURE_VR(&op2, expr2->type, reg1);
        if (op2.reg != reg1) {
            emitpcode(PC_VMR, reg1, op2.reg);
        }

        output->kind = OpVR;
        output->reg = reg1;
    } else {
        symbol_operand(output, maketemporary(type));
        indirect(output, 0);
        coerce_to_addressable_before(0, output, -1);

        GEN(expr1, &op1);
        align = operand_alignment(op1.object, type);
        max_align = type_operand_alignment(type);
        align = align > max_align ? max_align : align;
        store_aggregate(output, &op1, type, align);

        branch_always(label3);
        branch_label(label2);

        GEN(expr2, &op2);
        align = operand_alignment(op2.object, type);
        align = align > max_align ? max_align : align;
        store_aggregate(output, &op2, type, align);
    }

    branch_label(label3);
}
