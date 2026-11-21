/* RC1 instruction selection for scalar floating-point addition. */
#include "rc1_codegen.h"

#pragma pack(push, 2)
typedef struct CodeGenStatement {
    const struct CodeGenStatement* next;
    u8 kind;
    u8 attributes[5];
    const ENode* expression;
} CodeGenStatement;
#pragma pack(pop)

_Static_assert(offsetof(CodeGenStatement, expression) == 0x0a, "RC1 statement expression");

enum { ReturnStatement = 8, FloatResultRegister = 1, InlineData = 0x10000, PC_FADDS = 0xa3 };

extern const CodeGenStatement* current_statement;
extern void RC1_FloatBinaryOperator(short, ENode*, ENode*, short, Operand*);

static inline int is_returned_addition(const ENode* left, const ENode* right) {
    if (!current_statement || current_statement->kind != ReturnStatement)
        return 0;

    const ENode* expr = current_statement->expression;
    return expr && IS(expr, EADD) && expr->data.diadic.left == left &&
           expr->data.diadic.right == right;
}

static inline int is_literal_load(const ENode* expr) {
    return IS(expr, EINDIRECT) && IS(expr->data.monadic, EOBJREF) &&
           (expr->data.monadic->data.object->qualifiers & InlineData);
}

void retro_FloatAddition(short opcode, ENode* left, ENode* right, short result, Operand* output) {
    if (opcode != PC_FADDS || result != FloatResultRegister || left->hascall || right->hascall ||
        !is_returned_addition(left, right) || !is_literal_load(left) || is_literal_load(right)) {
        RC1_FloatBinaryOperator(opcode, left, right, result, output);
        return;
    }

    Operand lhs, rhs;
    init_operand(&lhs);
    init_operand(&rhs);
    GEN(left, &lhs);
    GEN(right, &rhs);

    // Load the nonliteral return operand first. Keep the arithmetic operands in IR order.
    ENSURE(rhs, OpFPR, Coerce_to_fp_register, right->type, -1);
    ENSURE(lhs, OpFPR, Coerce_to_fp_register, left->type, -1);
    emitpcode(opcode, result, lhs.reg, rhs.reg);
    output->kind = OpFPR;
    output->reg = result;
}
