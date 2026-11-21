#ifndef WII_RETRO_RC1_IRO_H
#define WII_RETRO_RC1_IRO_H

#include "rc1_local.h"

enum {
    Operand = 1,
    Unary = 2,
    Binary = 3,
    FunctionCall = 7,
    Indirect = 4,
    Add = 0xf,
    Assign = 0x1e,
    IntConstant = 0x34,
    ObjectRef = 0x3b,
};

typedef struct Expr {
    unsigned char opcode;
    unsigned char header[0xf];
    Object* object;
} Expr;

#pragma pack(push, 2)
typedef struct Linear {
    unsigned char kind;
    unsigned char opcode;
    unsigned char reserved[0x10];
    const Type* type;
    unsigned char attributes[0x14];
    union {
        struct {
            union {
                const Expr* expr;
                const struct Linear* left;
            } input;
            const struct Linear* right;
        };
        struct {
            unsigned char flags;
            unsigned short argument_count;
            const struct Linear* const* arguments;
            const struct Linear* callee;
            const struct IRFunctionType* type;
        } call;
    };
    unsigned char tail[4];
    const struct Linear* next;
} Linear;

typedef struct IRFunctionType {
    unsigned char prefix[0x1a];
    unsigned flags;
} IRFunctionType;
#pragma pack(pop)

_Static_assert(offsetof(Linear, input) == 0x2a, "RC1 linear IR operands");
_Static_assert(offsetof(Linear, call.arguments) == 0x2e, "RC1 linear call arguments");
_Static_assert(offsetof(Linear, call.type) == 0x36, "RC1 linear call type");
_Static_assert(offsetof(Linear, next) == 0x3e, "RC1 linear IR traversal");

static inline Object* operand(const Linear* node) {
    if (!node || node->kind != Operand || !node->input.expr || node->input.expr->opcode != ObjectRef)
        return NULL;
    return node->input.expr->object;
}

static inline Object* indirect_operand(const Linear* node) {
    return node && node->kind == Unary && node->opcode == Indirect ? operand(node->input.left) : NULL;
}

#endif
