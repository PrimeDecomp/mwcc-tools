/* Preserve direct class reference bindings when file IPA inlines a float select. */
#include "rc1_local.h"

typedef struct InlineFormal {
    const Object* object;
    const void* substitute;
    unsigned cost;
    unsigned char used;
    unsigned char unmodified;
    unsigned char force_temporary;
    unsigned char reserved;
} InlineFormal;

typedef struct InlineExpression {
    unsigned char kind;
    unsigned char header[15];
    const Object* object;
} InlineExpression;

typedef struct InlineArgument {
    const struct InlineArgument* next;
    const InlineExpression* expression;
} InlineArgument;

#pragma pack(push, 2)
typedef struct InlineAsmOperand {
    unsigned char kind;
    unsigned char flags;
    int reg;
    const Object* object;
    short effect;
    short register_class;
} InlineAsmOperand;

typedef struct InlineAsm {
    unsigned opcode;
    unsigned char flags;
    unsigned char flags2;
    short count;
    InlineAsmOperand args[];
} InlineAsm;

typedef struct InlineStatement {
    struct InlineStatement* next;
    unsigned char kind;
    unsigned char attributes[5];
    const InlineAsm* assembly;
} InlineStatement;
#pragma pack(pop)

enum { FileIpa = 1, NopStatement = 1, AsmStatement = 16, NamedRegister = 2, FloatSelect = 0xb4 };
enum { LocalObject = 1, ObjectReferenceExpression = 59 };

_Static_assert(sizeof(InlineFormal) == 16, "RC1 inline formal stride");
_Static_assert(offsetof(InlineExpression, object) == 0x10, "RC1 expression object operand");
_Static_assert(offsetof(InlineArgument, expression) == 4, "RC1 inline argument expression");
_Static_assert(offsetof(InlineStatement, assembly) == 0xa, "RC1 inline assembly statement");
_Static_assert(offsetof(InlineAsm, args) == 8, "RC1 inline assembly operands");
_Static_assert(sizeof(InlineAsmOperand) == 14, "RC1 inline assembly operand stride");

extern unsigned char RC1_ipa_kind;
extern unsigned char RC1_inlining_cost_mode;
extern unsigned RC1_inline_formal_count;
extern InlineFormal* RC1_inline_formals;
extern void RC1_InitializeInlineFormals(InlineStatement*, const Local*);

const InlineArgument* retro_inline_arguments;

static int analyzable_select(const InlineAsm* instruction) {
    if (!instruction || instruction->opcode != FloatSelect || instruction->count != 4)
        return 0;
    for (unsigned i = 0; i < 4; ++i) {
        const InlineAsmOperand* arg = &instruction->args[i];
        if (arg->kind != NamedRegister || !arg->object || !arg->object->type ||
            arg->object->type->kind != TypeFloat)
            return 0;
    }
    return 1;
}

static int direct_class_binding(const Type* type, const InlineArgument* argument) {
    const InlineExpression* expression = argument->expression;
    if (!type || type->kind != TypePointer || !expression || expression->kind != ObjectReferenceExpression)
        return 0;
    const PointerType* pointer = (const PointerType*)type;
    const Object* object = expression->object;
    return (pointer->qualifiers & QualReference) && pointer->target && pointer->target->kind == TypeClass &&
           object && object->address_kind == LocalObject && object->type == pointer->target;
}

void retro_InitializeInlineFormals(InlineStatement* statements, const Local* formals) {
    RC1_InitializeInlineFormals(statements, formals);
    if (RC1_ipa_kind != FileIpa || RC1_inlining_cost_mode || !retro_inline_arguments)
        return;

    const InlineArgument* argument = retro_inline_arguments;
    const Local* formal = formals;
    for (; formal && argument; formal = formal->next, argument = argument->next) {
        if (direct_class_binding(formal->object->type, argument))
            break;
    }
    if (!formal || !argument)
        return;

    size_t count = 0;
    for (const InlineStatement* stmt = statements; stmt; stmt = stmt->next) {
        if (stmt->kind != AsmStatement)
            continue;
        if (!analyzable_select(stmt->assembly))
            return;
        ++count;
    }
    if (!count || count > SIZE_MAX / sizeof(InlineStatement*))
        return;

    InlineStatement** selects = malloc(count * sizeof(*selects));
    if (!selects)
        return;
    size_t next = 0;
    for (InlineStatement* stmt = statements; stmt; stmt = stmt->next) {
        if (stmt->kind == AsmStatement) {
            selects[next++] = stmt;
            stmt->kind = NopStatement;
        }
    }

    /* RC1 stops the scan at inline assembly and forces every formal to a local.
     * Scan the C statements separately for references bound to caller-owned class objects.
     * Forwarded references and computed addresses retain RC1's temporary. */
    InlineFormal* original = RC1_inline_formals;
    RC1_InitializeInlineFormals(statements, formals);
    const InlineFormal* scanned = RC1_inline_formals;
    RC1_inline_formals = original;

    for (size_t i = 0; i < count; ++i)
        selects[i]->kind = AsmStatement;
    free(selects);

    argument = retro_inline_arguments;
    for (unsigned i = 0; i < RC1_inline_formal_count && argument; ++i, argument = argument->next) {
        if (direct_class_binding(original[i].object->type, argument) && scanned[i].used &&
            scanned[i].unmodified && !scanned[i].force_temporary) {
            original[i].unmodified = 1;
            original[i].force_temporary = 0;
        }
    }
}
