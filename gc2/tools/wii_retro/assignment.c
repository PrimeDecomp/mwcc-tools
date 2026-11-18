/* RC1 instruction selection for assignment expressions. */
#include "rc1_codegen.h"

typedef struct PCodeBlock PCodeBlock;
struct PCode {
    PCode* next;
};

#pragma pack(push, 2)
typedef struct {
    u8 kind;
    u8 rclass;
    u16 effect;
    short reg;
    u8 reserved[10];
} PCodeArg;
#pragma pack(pop)

_Static_assert(offsetof(PCodeArg, reg) == 0x04, "RC1 PCodeArg register offset");
_Static_assert(sizeof(PCodeArg) == 0x10, "RC1 PCodeArg size");

enum { InRegister = 2 };
enum { PC_ADDI = 0x3f, PC_ADDIS = 0x42, EffectRead = 1, EffectWrite = 2, EffectPair = 0xc000 };
extern u8 RC1_gprfloatcopy;
extern short RC1_processor;
extern Object* RC1_postincrement_object;
extern PCode *RC1_postincrement_end, *RC1_postincrement_start;
extern Type stunsignedlong, stunsignedlonglong;
extern PCodeBlock* pclastblock;

extern void SFPE_gen_ASS(ENode*, short, short, Operand*);
extern void I8_gen_ASS(ENode*, short, short, Operand*);
extern int ispostincrementopportunity(ENode*, Operand*, int*);
extern void store(short, Operand*, Type*);
extern void store_pair(short, short, Operand*, Type*);
extern void store_fp(short, Operand*, Type*);
extern void store_v(short, Operand*);
extern void vectorstore(short, Operand*, Type*);
extern void insert_bitfield(short, Operand*, Type*);
extern void extract_bitfield(Operand*, Type*, short, Operand*);
extern PCode* makecopyinstruction(PCodeArg*, PCodeArg*);
extern void appendpcode(PCodeBlock*, PCode*);
extern void deletepcode(PCode*);
extern void insertpcodebefore(PCode*, PCode*);
extern int RC1_CanMoveAssignmentBeforePostincrement(ENode*, Object*);

static inline void assignment_increment(short reg, int amount) {
    if (amount != (short)amount) {
        emitpcode(PC_ADDIS, reg, reg, 0, (short)((amount >> 16) + ((amount >> 15) & 1)));
        if ((short)amount)
            emitpcode(PC_ADDI, reg, reg, 0, (short)amount);
    } else {
        emitpcode(PC_ADDI, reg, reg, 0, amount);
    }
}

static inline int gpr_float_copy(const Operand* op, const ENode* left) {
    if (op->kind >= OpIndirect &&
        ((IS(left, EINDIRECT) && IS(left->data.monadic, EOBJREF)) ||
         (IS(left, EADD) && IS(left->data.diadic.right, EINTCONST) &&
          IS(left->data.diadic.left, EINDIRECT) && IS(left->data.diadic.left->data.monadic, EOBJREF))))
        return 1;
    return 0;
}

void gen_ASS(ENode* expr, short output_reg, short output_reg_hi, Operand* output) {
    Type* type;
    ENode* left;
    ENode* right;
    Operand dest;
    Operand source;
    Operand word;
    int increment;
    u8 rclass;
    VarInfo* info;

    type = expr->type;
    RC1_postincrement_object = 0;
    rclass = Registers_ClassForType(type);
    if (IS(expr, ECONDASS)) {
        left = expr->data.conditional.left;
        if (IS(left, EINDIRECT))
            left = left->data.monadic;
        else
            CError_Internal(RC1_InstrSelection_source_file, 2767);
        right = expr->data.conditional.right;
    } else {
        left = expr->data.diadic.left;
        right = expr->data.diadic.right;
    }

    init_operand(&dest);
    init_operand(&source);
    init_operand(&word);

    if (RC1_soft_float && SCALAR_FLOAT(type)) {
        SFPE_gen_ASS(expr, output_reg, output_reg_hi, output);
        return;
    }
    if (IS_64BIT_INT(type)) {
        I8_gen_ASS(expr, output_reg, output_reg_hi, output);
        return;
    }

    if (IS(left, EOBJREF) && (info = Registers_GetVarInfo(left->data.object)) &&
        (info->flags & InRegister)) {
        info = Registers_GetVarInfo(left->data.object);
        cgdispatch[right->kind](right, info->reg, -1, &source);
        switch (info->rclass) {
        case RegGPR:
            ENSURE(source, OpGPR, Coerce_to_register, type, info->reg);
            output->kind = OpGPR;
            break;
        case RegFPR:
            ENSURE(source, OpFPR, Coerce_to_fp_register, type, info->reg);
            output->kind = OpFPR;
            break;
        case RegVR:
            ENSURE(source, OpVR, Coerce_to_v_register, type, info->reg);
            output->kind = OpVR;
            break;
        default:
            CError_Internal(RC1_InstrSelection_source_file, 2818);
        }
        if (source.reg != info->reg) {
            PCodeArg dst, src;
            int processor = RC1_processor;
            int effect = 0;
            if ((SMALL_VECTOR_CPU(processor) && VECTOR(type) && type->size == 8) ||
                (RC1_use_paired_moves_for_double && SCALAR_FLOAT(type) && type->size != 4) ||
                (processor == GekkoProcessor && type->kind == FloatType && INTEGRAL(type) == PairedSingle))
                effect = EffectPair;
            dst.kind = 0;
            dst.rclass = info->rclass;
            dst.reg = info->reg;
            dst.effect = effect | EffectWrite;
            src.kind = 0;
            src.rclass = info->rclass;
            src.reg = source.reg;
            src.effect = effect | EffectRead;
            appendpcode(pclastblock, makecopyinstruction(&src, &dst));
        }
        output->reg = info->reg;
    } else if (rclass == RegFPR) {
        int processor;
        GEN(right, &source);
        processor = RC1_processor;
        if (processor == GekkoProcessor && type->kind == FloatType && INTEGRAL(type) == PairedSingle) {
            ENSURE(source, OpFPR, Coerce_to_fp_register, right->type, -1);
            GEN(left, &dest);
            indirect(&dest, expr);
            vectorstore(source.reg, &dest, type);
            output->kind = source.kind;
            output->reg = source.reg;
        } else if (processor == GekkoProcessor && RC1_gprfloatcopy && expr->ignored && gpr_float_copy(&source, left)) {
            Type* integer_type = type->size == 4 ? &stunsignedlong : &stunsignedlonglong;
            ENSURE(source, OpGPR, Coerce_to_register, integer_type, -1);
            if (!IS_64BIT_INT(integer_type) && ispostincrementopportunity(left, &dest, &increment)) {
                indirect(&dest, expr);
                store(source.reg, &dest, integer_type);
                assignment_increment(dest.reg, increment);
            } else {
                GEN(left, &dest);
                indirect(&dest, expr);
                if (IS_64BIT_INT(integer_type))
                    store_pair(source.reg, source.reg_hi, &dest, integer_type);
                else
                    store(source.reg, &dest, integer_type);
            }
            if (IS_64BIT_INT(integer_type)) {
                output->kind = OpGPRPair;
                output->reg = source.reg;
                output->reg_hi = source.reg_hi;
            } else {
                output->kind = OpGPR;
                output->reg = source.reg;
            }
        } else {
            ENSURE(source, OpFPR, Coerce_to_fp_register, right->type, -1);
            if (ispostincrementopportunity(left, &dest, &increment)) {
                indirect(&dest, expr);
                store_fp(source.reg, &dest, type);
                assignment_increment(dest.reg, increment);
            } else {
                GEN(left, &dest);
                indirect(&dest, expr);
                store_fp(source.reg, &dest, type);
            }
            output->kind = OpFPR;
            output->reg = source.reg;
        }
    } else if (rclass == RegVR) {
        GEN(right, &source);
        if (source.kind == OpAbsolute) {
            ENSURE(source, OpVR, Coerce_to_v_register, type, -1);
        } else if (source.kind != OpVR) {
            Coerce_to_v_register(&source, right->type, -1);
        }
        if (ispostincrementopportunity(left, &dest, &increment)) {
            indirect(&dest, expr);
            store_v(source.reg, &dest);
            assignment_increment(dest.reg, increment);
        } else {
            GEN(left, &dest);
            indirect(&dest, expr);
            store_v(source.reg, &dest);
        }
        output->kind = OpVR;
        output->reg = source.reg;
    } else if (SMALL_VECTOR_CPU((int)RC1_processor) && VECTOR(type) && type->size == 8) {
        GEN(right, &source);
        ENSURE(source, OpGPR, Coerce_to_register, right->type, -1);
        GEN(left, &dest);
        indirect(&dest, expr);
        vectorstore(source.reg, &dest, type);
        output->kind = source.kind;
        output->reg = source.reg;
    } else if (SCALAR_TYPE(type)) {
        GEN(right, &source);
        ENSURE(source, OpGPR, Coerce_to_register, right->type, -1);
        if (IS(left, EBITFIELD)) {
            GEN(left->data.monadic, &dest);
            indirect(&dest, expr);
            word = dest;
            ENSURE(word, OpGPR, Coerce_to_register, type, -1);
            insert_bitfield(source.reg, &word, left->type);
            store(word.reg, &dest, type);
            if (!expr->ignored)
                extract_bitfield(&word, left->type, source.reg, &dest);
        } else if (ispostincrementopportunity(left, &dest, &increment)) {
            indirect(&dest, expr);
            store(source.reg, &dest, type);
            assignment_increment(dest.reg, increment);
        } else {
            GEN(left, &dest);
            indirect(&dest, expr);
            store(source.reg, &dest, type);
        }
        /* Move the assignment ahead of a postincrement when their operands allow it. */
        if (RC1_postincrement_object &&
            RC1_CanMoveAssignmentBeforePostincrement(expr, RC1_postincrement_object)) {
            PCode* code = RC1_postincrement_end->next;
            while (code) {
                PCode* next = code->next;
                deletepcode(code);
                insertpcodebefore(RC1_postincrement_start, code);
                code = next;
            }
        }
        output->kind = OpGPR;
        output->reg = source.reg;
    } else {
        short align, source_align;
        GEN(right, &source);
        GEN(left, output);
        indirect(output, expr);
        align = operand_alignment(output->object, type);
        source_align = operand_alignment(source.object, type);
        align = source_align < align ? source_align : align;

        if ((type->kind == StructureType || type->kind == ClassType) && !CMach_PassResultInHiddenArg(type)) {
            if (source.kind == OpGPRPair)
                store_small_block_from_reg_pair(source.reg, source.reg_hi, output, type, align);
            else if (source.kind == OpGPR)
                store_small_block_from_reg(source.reg, output, type, align);
            else
                move_block(output, &source, type->size, align);
        } else {
            move_block(output, &source, type->size, align);
        }
    }
}
