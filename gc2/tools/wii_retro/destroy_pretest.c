/* Fold the duplicate entry test of a fixed-stride calling loop after RC1 colors registers. */
#include <stddef.h>
#include <stdint.h>

typedef struct Block Block;
typedef struct Edge Edge;
typedef struct PCode PCode;

#pragma pack(push, 2)
typedef struct Label {
    struct Label* next;
    Block* block;
} Label;

typedef struct Operand {
    uint8_t kind;
    uint8_t reg_class;
    union {
        struct {
            uint16_t effect;
            uint16_t number;
        } reg;
        int32_t immediate;
        const Label* label;
        uint8_t storage[12];
    } value;
} Operand;

struct PCode {
    PCode* next;
    PCode* prev;
    Block* block;
    uint32_t flags;
    uint32_t branch_flags;
    uint8_t state[0x14];
    uint16_t op;
    uint16_t arg_count;
    Operand args[];
};

struct Edge {
    Edge* next_hash;
    Edge* next_successor;
    Edge* next_predecessor;
    Block* from;
    Block* to;
};

struct Block {
    Block* next;
    Block* prev;
    Label* labels;
    Edge* predecessors;
    Edge* successors;
    PCode* first;
    PCode* last;
    uint8_t state[0xc];
    uint16_t count;
};
#pragma pack(pop)

_Static_assert(sizeof(Operand) == 14, "RC1 operand stride");
_Static_assert(offsetof(PCode, args) == 0x2c, "RC1 PCode operands");
_Static_assert(offsetof(Edge, to) == 0x10, "RC1 edge destination");
_Static_assert(offsetof(Block, count) == 0x28, "RC1 block instruction count");

enum {
    Branch = 0,
    BranchTrue = 5,
    BranchFalse = 8,
    AddImmediate = 63,
    CompareLogical = 85,
    RegisterOperand = 0,
    ImmediateOperand = 2,
    LabelOperand = 6,
    ConditionRegister = 1,
    GeneralRegister = 4,
    EqualBit = 2,
    IsCall = 0x8,
    SideEffects = 0x100,
    Links = 0x800000,
};

extern Block* pcbasicblocks;
extern void pcode_expand_pseudo_ops(uint8_t late);
extern void deleteedge(Block* from, Block* to);
extern void deletepcode(PCode* instruction);

static Block* next_nonempty(Block* block) {
    while (block && !block->count)
        block = block->next;
    return block;
}

static Block* branch_target(const PCode* branch, unsigned arg) {
    if (branch->arg_count <= arg || branch->args[arg].kind != LabelOperand ||
        (branch->flags & (IsCall | SideEffects)) || (branch->branch_flags & Links))
        return NULL;
    const Label* label = branch->args[arg].value.label;
    return label ? label->block : NULL;
}

static int is_register(const Operand* operand, unsigned reg_class) {
    return operand->kind == RegisterOperand && operand->reg_class == reg_class;
}

static int is_compare(const PCode* instruction) {
    return instruction && instruction->op == CompareLogical && instruction->arg_count == 3 &&
           is_register(&instruction->args[0], ConditionRegister) &&
           is_register(&instruction->args[1], GeneralRegister) &&
           is_register(&instruction->args[2], GeneralRegister);
}

static Block* equality_target(const PCode* branch, unsigned op, const PCode* compare) {
    if (branch->op != op || branch->arg_count < 3 ||
        !is_register(&branch->args[0], ConditionRegister) ||
        branch->args[0].value.reg.number != compare->args[0].value.reg.number ||
        branch->args[1].kind != ImmediateOperand || branch->args[1].value.immediate != EqualBit)
        return NULL;
    return branch_target(branch, 2);
}

static int has_successor(const Block* from, const Block* to) {
    for (const Edge* edge = from->successors; edge; edge = edge->next_successor)
        if (edge->from == from && edge->to == to)
            return 1;
    return 0;
}

static int is_fixed_stride(const PCode* update, unsigned cursor) {
    return update && update->op == AddImmediate && update->arg_count >= 3 &&
           is_register(&update->args[0], GeneralRegister) &&
           update->args[0].value.reg.number == cursor &&
           is_register(&update->args[1], GeneralRegister) &&
           update->args[1].value.reg.number == cursor && update->args[2].kind == ImmediateOperand &&
           update->args[2].value.immediate > 0;
}

static int loop_has_call(const Block* body, const Block* test) {
    int found = 0;
    while (body && body != test) {
        for (const PCode* pc = body->first; pc; pc = pc->next)
            found |= (pc->flags & IsCall) != 0;
        body = body->next;
    }
    return body && found;
}

static void fold_destroy_pretests(void) {
    for (Block* guard = pcbasicblocks; guard; guard = guard->next) {
        PCode* branch = guard->last;
        PCode* compare = branch ? branch->prev : NULL;
        if (!is_compare(compare))
            continue;
        Block* exit = equality_target(branch, BranchTrue, compare);
        Block* entry = next_nonempty(guard->next);
        if (!exit || !entry || entry->count != 1 || entry->first->op != Branch)
            continue;
        Block* test = next_nonempty(branch_target(entry->first, 0));
        if (!test || test->count != 2 || !is_compare(test->first) ||
            next_nonempty(test->next) != next_nonempty(exit))
            continue;
        Block* body = next_nonempty(equality_target(test->last, BranchFalse, test->first));
        if (!body || body != next_nonempty(entry->next))
            continue;

        const Operand* first = compare->args;
        const Operand* second = test->first->args;
        if (first[0].value.reg.number != second[0].value.reg.number ||
            first[1].value.reg.number != second[1].value.reg.number ||
            first[2].value.reg.number != second[2].value.reg.number)
            continue;

        const Block* latch = test->prev;
        while (latch && latch != entry && !latch->count)
            latch = latch->prev;
        if (!latch || latch == entry || !is_fixed_stride(latch->last, second[1].value.reg.number) ||
            !loop_has_call(body, test) || !has_successor(guard, exit))
            continue;

        /* The entry jump still checks the range before the first iteration. */
        deleteedge(guard, exit);
        deletepcode(branch);
        deletepcode(compare);
    }
}

void retro_expand_pseudo_ops(uint8_t late) {
    pcode_expand_pseudo_ops(late);
    fold_destroy_pretests();
}
