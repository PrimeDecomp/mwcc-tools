/* Preserve file-IPA loop-invariant receiver addresses. */
#include "rc1_iro.h"

#pragma pack(push, 2)
typedef struct FlowNode {
    unsigned index;
    unsigned char attributes[0x14];
    const Linear* first;
    const Linear* last;
    unsigned char tail[0x18];
    const struct FlowNode* next;
} FlowNode;

typedef struct LoopMembers {
    unsigned word_count;
    unsigned words[];
} LoopMembers;

typedef struct NaturalLoop {
    unsigned char prefix[8];
    const LoopMembers* members;
    unsigned char tail[0x62];
    struct NaturalLoop* next;
} NaturalLoop;
#pragma pack(pop)

_Static_assert(offsetof(FlowNode, first) == 0x18, "RC1 IR block start");
_Static_assert(offsetof(FlowNode, next) == 0x38, "RC1 IR block traversal");
_Static_assert(offsetof(NaturalLoop, next) == 0x6e, "RC1 natural loop traversal");

enum { Method = 0x10, ConstMethod = 0x8000 };
enum { FileIpa = 1 };

extern unsigned char RC1_ipa_kind;
extern unsigned char RC1_loop_invariants_option;
extern const FlowNode* IRO_FirstNode;
extern Local* arguments;
extern unsigned char IsTempName(const Identifier* name);
extern unsigned char RC1_IsPropagatableExpression(const Linear* assignment);
extern void IRO_ExpressionPropagation(void);
extern NaturalLoop* IRO_FindLoops(void);
extern void IRO_FindLoopNodes(NaturalLoop* loop);
extern void IRO_FreeLoopList(NaturalLoop* loops);

static NaturalLoop* expression_loops;
static int expression_loops_collected;

static int is_parameter(const Object* object) {
    for (const Local* parameter = arguments; parameter; parameter = parameter->next)
        if (parameter->object == object)
            return 1;
    return 0;
}

static const FlowNode* containing_block(const Linear* assignment) {
    for (const FlowNode* block = IRO_FirstNode; block; block = block->next) {
        if (!block->last)
            continue;
        for (const Linear* node = block->first; node; node = node->next) {
            if (node == assignment)
                return block;
            if (node == block->last)
                break;
        }
    }
    return NULL;
}

static int feeds_mutable_receiver(const Linear* assignment, const Linear* end, const Object* receiver) {
    for (const Linear* node = assignment->next; node; node = node->next) {
        if (node->kind == FunctionCall) {
            const IRFunctionType* type = node->call.type;
            return type && (type->flags & (Method | ConstMethod)) == Method &&
                   node->call.argument_count && indirect_operand(node->call.arguments[0]) == receiver;
        }
        if (node == end)
            break;
    }
    return 0;
}

static int in_natural_loop(const FlowNode* block) {
    if (!expression_loops_collected) {
        expression_loops = IRO_FindLoops();
        for (NaturalLoop* loop = expression_loops; loop; loop = loop->next)
            IRO_FindLoopNodes(loop);
        expression_loops_collected = 1;
    }

    unsigned word = block->index / 32;
    unsigned mask = 1u << (block->index % 32);
    for (const NaturalLoop* loop = expression_loops; loop; loop = loop->next) {
        const LoopMembers* members = loop->members;
        if (members && word < members->word_count && (members->words[word] & mask))
            return 1;
    }
    return 0;
}

void retro_PropagateExpressions(void) {
    expression_loops = NULL;
    expression_loops_collected = 0;
    IRO_ExpressionPropagation();
    if (expression_loops)
        IRO_FreeLoopList(expression_loops);
    expression_loops = NULL;
}

/* Leave mutable member receivers available to RC1's invariant-assignment pass.
 * Expression propagation runs first and would otherwise substitute the address
 * directly into the call, leaving no assignment for the loop pass to hoist. */
unsigned char retro_IsPropagatableExpression(const Linear* assignment) {
    if (!RC1_IsPropagatableExpression(assignment))
        return 0;
    if (RC1_ipa_kind != FileIpa || !RC1_loop_invariants_option)
        return 1;

    const Linear* value = assignment->right;
    if (!value->type || value->type->kind != TypePointer || value->kind != Binary || value->opcode != Add ||
        value->right->kind != Operand || value->right->opcode != IntConstant)
        return 1;

    const Object* receiver = indirect_operand(assignment->input.left);
    const Object* base = indirect_operand(value->input.left);
    if (!receiver || !IsTempName(receiver->name) || !base || !is_parameter(base))
        return 1;

    const FlowNode* block = containing_block(assignment);
    return !block || block->last == assignment ||
           !feeds_mutable_receiver(assignment, block->last, receiver) || !in_natural_loop(block);
}
