/*
 * The oracle build's wrappers of the original's functions (config.json "oracle": "hooks").
 *
 * tools/link.py renames a hooked function's definition in the original's split object to NAME__retail, so that what
 * calls NAME reaches the wrapper defined here, and the wrapper traces what the function is given and what it leaves, and
 * calls the original's code. The function does what it did, so what the oracle executable compiles is what the original
 * does; the trace says what happened inside, from outside: the PCode before and after, whose operands are the
 * register assignment (tools/oracle.py reads the allocation from the pair).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler/common.h"
#include "compiler/CInline.h"
#include "compiler/CFunc.h"
#include "compiler/CMangler.h"
#include "compiler/CPrep.h"
#include "compiler/CodeGen.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/types.h"
#include "compiler/PCode.h"
#include "compiler/InterferenceGraph.h"
#include "compiler/Registers.h"
#include "compiler/Scheduler.h"
#include "compiler/OracleTrace.h"

#ifdef MWCC_ORACLE_TRACE

/* The link (mangled) name of a function. When the compiler has not made it yet (a member function's is often made only
 * when the object is written), the mangler makes it here without caching it in the object: it only builds the name in
 * its own buffer and enters it in the name table, where the compiler finds it again later. */
static const char *link_name(Object *object)
{
    HashNameNode *name;

    while (object != NULL && object->datatype == DALIAS)
        object = object->u.alias.object;
    if (object == NULL)
        return "?";
    if (object->datatype == DFUNC || object->datatype == DVFUNC) {
        name = object->u.func.linkname;
#if VERSION >= VERSION_GC_1_3
        if (name == NULL)
            name = CMangler_GetLinkName(object);
#endif
        if (name != NULL)
            return name->name;
    }
    return object->name != NULL ? object->name->name : "?";
}

/* The PCode of the function being generated: a block per line (its address, number, weight and the blocks it leads to),
 * an instruction per line (its address, which is what identifies it before and after, opcode, flags, the source line
 * it was generated for, operands as the kind, arg and ten bytes of value each). */
static void print_instruction(const char *prefix, PCodeInstruction *instruction)
{
    int operand;
    int byte;

    /* (f18[1] is the source line of the statement it was generated for) */
    OracleTrace_Printf("%s p=%08x op=%d flags=%08x n=%d line=%d ops=", prefix, (unsigned int)instruction,
                       (int)instruction->opcode, instruction->flags, (int)instruction->operand_count,
                       instruction->f18[1]);
    for (operand = 0; operand < instruction->operand_count; operand++) {
        const unsigned char *raw = (const unsigned char *)&instruction->operandData.operands[operand];

        for (byte = 0; byte < 12; byte++) {
            OracleTrace_Printf("%02x", (unsigned int)raw[byte]);
        }
        OracleTrace_Printf(" ");
    }
    OracleTrace_Printf("\n");
}

static void snapshot(const char *phase, Object *function)
{
    PCodeBlock *block;
    PCodeInstruction *instruction;
    PCodeBlockLink *link;

    OracleTrace_Printf("ra.%s function=%s link=%s\n", phase, function->name->name, link_name(function));
    for (block = gPCodeBlocks; block != NULL; block = block->next) {
        OracleTrace_Printf("ra.block p=%08x index=%d weight=%d flags=%04x succ=", (unsigned int)block, block->index,
                           block->execution_weight, (unsigned int)block->flags);
        for (link = block->successors; link != NULL; link = link->next) {
            OracleTrace_Printf("%08x,", (unsigned int)link->payload.block);
        }
        OracleTrace_Printf("\n");
        for (instruction = block->instructions; instruction != NULL; instruction = instruction->next)
            print_instruction("ra.i", instruction);
    }
    OracleTrace_Printf("ra.end\n");
}

/* A trace that is only wanted on request (it is large): the environment variable NAME set to something else than 0. */
static int wanted(const char *name)
{
    const char *value = getenv(name);

    return value != NULL && value[0] != 0 && value[0] != '0';
}

#if VERSION >= VERSION_GC_1_3
#define ALLOCATE_REGISTERS fn_00506cd0
#define ALLOCATE_REGISTERS_RETAIL fn_00506cd0__retail
#endif

#ifdef ALLOCATE_REGISTERS
extern void ALLOCATE_REGISTERS_RETAIL(Object *function);

/* The register allocation of a function: Coloring_AllocateRegisters in the sources. */
void ALLOCATE_REGISTERS(Object *function)
{
    snapshot("before", function);
    ALLOCATE_REGISTERS_RETAIL(function);
    snapshot("after", function);
}
#endif

#if VERSION >= VERSION_GC_1_3
/* The colouring of one register class, as the original does it (1.3 to 2.7 run the same code): fn_00507260 sets the
 * class up, fn_00507070 simplifies the interference graph and returns the stack of its nodes (the order select colours
 * them in, the top first), and fn_00506f50 colours them, the lowest free colour of the class's mask first and a
 * register claimed from the non-volatile ones when none is free. The sources have them as Coloring_SimplifyGraph and
 * Coloring_SelectColors, but with another node layout and with arguments the original takes from globals. */
typedef struct OracleNode {
    struct OracleNode *next;
    int f4;
    int f8;
    int spill_cost;      /* 0xc (set by the simplification when a node has to be spilled) */
    short virtual_register; /* 0x10 */
    short degree;        /* 0x12 */
    short color;         /* 0x14: the physical register, -1 while it has none */
    unsigned short flags; /* 0x16: 1 spilled, 2 simplified, 4 coalesced */
    short neighbor_count; /* 0x18 */
    short neighbors[1];  /* 0x1a */
} OracleNode;

extern OracleNode *fn_00507070__retail(void);
extern int fn_00506f50__retail(OracleNode *stack);
extern int fn_004fcb70(char cls);           /* the registers the class has to colour with */
extern void fn_004ceda0(char cls);          /* resets the claimed registers of the class */
extern unsigned int fn_004fcb20(char cls);  /* the class's colour mask */

/* The globals the simplification reads, from the operands of its own code (the same instructions in every version;
 * the globals have no names in common across them). */
static int coloring_globals(char **cls, int **start, int **count, OracleNode ***graph)
{
    const unsigned char *code = (const unsigned char *)fn_00507070__retail;

    if (code[7] != 0xa0 || code[0x29] != 0x8b || code[0x2a] != 0x0c || code[0x2b] != 0x85 || code[0x32] != 0x8b ||
        code[0x33] != 0x15 || code[0x8c] != 0x3b || code[0x8d] != 0x0c || code[0x8e] != 0x85)
        return 0;
    *cls = *(char **)(code + 8);
    *start = *(int **)(code + 0x2c);
    /* (the operand is the global that holds the table, which is made again for each graph) */
    *graph = **(OracleNode ****)(code + 0x34);
    *count = *(int **)(code + 0x8f);
    return 1;
}

/* ra.graph: the class's graph before the simplification (each node's degree, flags and neighbours); ra.order: the
 * stack it leaves, in the order select takes it, with each node's spill cost. */
OracleNode *fn_00507070(void)
{
    char *cls;
    int *start;
    int *count;
    OracleNode **graph;
    OracleNode *stack;
    OracleNode *node;
    int known;
    int index;
    int k;

    known = coloring_globals(&cls, &start, &count, &graph);
    if (known) {
        OracleTrace_Printf("ra.graph cls=%d k=%d start=%d count=%d\n", (int)*cls, fn_004fcb70(*cls), start[*cls],
                           count[*cls]);
        for (index = start[*cls]; index < count[*cls]; index++) {
            node = graph[index];
            OracleTrace_Printf("ra.node v=%d p=%08x deg=%d flags=%x color=%d n=", index, (unsigned int)node,
                               (int)node->degree, (unsigned int)node->flags, (int)node->color);
            for (k = 0; k < node->neighbor_count; k++)
                OracleTrace_Printf("%d,", (int)node->neighbors[k]);
            OracleTrace_Printf("\n");
        }
    }
    stack = fn_00507070__retail();
    if (known) {
        OracleTrace_Printf("ra.order cls=%d v=", (int)*cls);
        for (node = stack; node != NULL; node = node->next)
            OracleTrace_Printf("%08x:%d,", (unsigned int)node, node->spill_cost);
        OracleTrace_Printf("\n");
    }
    return stack;
}

/* ra.select: the class's colour mask before and after (a register claimed when no colour was free is added to it),
 * then each node of the stack with the colour it took (-1 and flags 1 when it is to be spilled). */
int fn_00506f50(OracleNode *stack)
{
    char *cls;
    int *start;
    int *count;
    OracleNode **graph;
    OracleNode *node;
    unsigned int before;
    int result;
    int known;

    known = coloring_globals(&cls, &start, &count, &graph);
    before = 0;
    if (known) {
        /* (what select does first, so it is the same mask select starts from) */
        fn_004ceda0(*cls);
        before = fn_004fcb20(*cls);
    }
    result = fn_00506f50__retail(stack);
    if (known) {
        OracleTrace_Printf("ra.select cls=%d mask=%08x after=%08x ok=%d v=", (int)*cls, before, fn_004fcb20(*cls),
                           result);
        for (node = stack; node != NULL; node = node->next)
            OracleTrace_Printf("%08x:%d:%x,", (unsigned int)node, (int)node->color, (unsigned int)node->flags);
        OracleTrace_Printf("\n");
    }
    return result;
}

/* The stack frame once it is laid out (fn_004f7fb0, after the allocation: StackFrameEABI_FinalizeLayout and the slots
 * of the compiler's temporaries in 1.2.5): fr.local, each local in the order of the function's list, with its offset
 * in the locals area (u.var.uid; the offset from r1 adds the linkage area and the outgoing arguments), size and
 * register (0 when it is in memory). */
extern void fn_004f7fb0__retail(void);
extern ObjectList *locals;
extern ObjectList *temporary_objects; /* (a list of TemporaryObjectEntry, which has the same layout) */

static void frame_objects(const char *kind, ObjectList *list)
{
    Object *object;
    VarInfo *info;

    for (; list != NULL; list = list->next) {
        object = list->object;
        if (object == NULL || object->datatype != DLOCAL)
            continue;
        info = object->u.var.info;
        OracleTrace_Printf("fr.%s name=%s offset=%d size=%d reg=%d used=%d\n", kind,
                           object->name != NULL ? object->name->name : "?", object->u.var.uid,
                           object->type != NULL ? (int)object->type->size : -1, info != NULL ? (int)info->reg : -1,
                           info != NULL ? (int)info->used : -1);
    }
}

/* fr.layout: the sizes the layout computed, read through the operands of its own code (the same in 1.3, 1.3.2 and
 * 2.7; checked byte by byte): the linkage area (8, or 0 when the function has no frame), the outgoing arguments, the
 * locals area, the area above it and the frame before the register saves. A local's offset from r1 is
 * linkage + outgoing + its offset. */
static void frame_layout(void)
{
    const unsigned char *code = (const unsigned char *)fn_004f7fb0__retail;

    if (code[0x2da] != 0xc7 || code[0x2db] != 0x05 || code[0x2e0] != 8 || code[0x2e4] != 0x8b ||
        code[0x2e5] != 0x15 || code[0x2ed] != 0x03 || code[0x2ee] != 0x15 || code[0x2f3] != 0xa1 ||
        code[0x2fa] != 0xa3)
        return;
    OracleTrace_Printf("fr.layout linkage=%d outgoing=%d locals=%d above=%d size=%d\n", **(int **)(code + 0x2dc),
                       **(int **)(code + 0x2e6), **(int **)(code + 0x2ef), **(int **)(code + 0x2f4),
                       **(int **)(code + 0x2fb));
}

void fn_004f7fb0(void)
{
    fn_004f7fb0__retail();
    OracleTrace_Printf("fr.begin\n");
    frame_layout();
    frame_objects("local", locals);
    frame_objects("temp", temporary_objects);
    OracleTrace_Printf("fr.end\n");
}

/* The list scheduler, on request (MWCC_ORACLE_SCHEDULE=1): schedule_block orders the instructions of one block, cycle
 * by cycle, picking among the ready ones with select_ready_coloring_node (the sources' Scheduler.c; 1.3 to 2.7 run the
 * same code, with the machine's check one word further on and another opcode table). sc.block: the block, whether the
 * registers are still virtual (the scheduling before the allocation) and its instructions in their order before;
 * sc.node: the dependency graph once it is built (each instruction's latency, height, latest cycle, unscheduled
 * predecessors and the instructions that depend on it with the latency of each edge); sc.pick: for each pick, the
 * cycle and every instruction whose predecessors are all scheduled, with its earliest and latest cycle, height, the
 * dependents it would make ready, whether the machine can issue it this cycle and its opcode's rank; then what was
 * picked (0 when nothing could be: the cycle ends); sc.after: the order it left. */
extern void schedule_block__retail(PCodeBlock *block);
extern CColoringNode *select_ready_coloring_node__retail(CColoringNode *list, UInt16 cycle);
extern SInt32 gVirtualRegistersActive;
static int schedule_dag_pending;

static int schedule_globals(struct MachineInfo **machine, const unsigned char **rank)
{
    const unsigned char *code = (const unsigned char *)select_ready_coloring_node__retail;

    if (code[0x26] != 0x8b || code[0x27] != 0x2d || code[0x100] != 0x8a || code[0x101] != 0x04 || code[0x102] != 0x55)
        return 0;
    *machine = **(struct MachineInfo ***)(code + 0x28);
    *rank = *(const unsigned char **)(code + 0x103);
    return 1;
}

void schedule_block(PCodeBlock *block)
{
    PCodeInstruction *instruction;

    if (!wanted("MWCC_ORACLE_SCHEDULE")) {
        schedule_block__retail(block);
        return;
    }
    OracleTrace_Printf("sc.block p=%08x index=%d virtual=%d\n", (unsigned int)block, block->index,
                       (int)gVirtualRegistersActive);
    for (instruction = block->instructions; instruction != NULL; instruction = instruction->next)
        print_instruction("sc.i", instruction);
    schedule_dag_pending = 1;
    schedule_block__retail(block);
    schedule_dag_pending = 0;
    OracleTrace_Printf("sc.after order=");
    for (instruction = block->instructions; instruction != NULL; instruction = instruction->next)
        OracleTrace_Printf("%08x,", (unsigned int)instruction);
    OracleTrace_Printf("\n");
}

CColoringNode *select_ready_coloring_node(CColoringNode *list, UInt16 cycle)
{
    struct MachineInfo *machine;
    const unsigned char *rank;
    CColoringNode *node;
    CColoringNode *picked;
    CColoringList *edge;
    int count;
    int known;

    if (!wanted("MWCC_ORACLE_SCHEDULE"))
        return select_ready_coloring_node__retail(list, cycle);
    known = schedule_globals(&machine, &rank);
    if (schedule_dag_pending) {
        schedule_dag_pending = 0;
        for (node = list; node != NULL; node = node->prev) {
            OracleTrace_Printf("sc.node i=%08x latency=%d height=%d latest=%d preds=%d succ=", (unsigned int)node->obj,
                               (int)node->latency, (int)node->height, (int)node->latestCycle, (int)node->flag);
            for (edge = node->conflicts; edge != NULL; edge = edge->next)
                OracleTrace_Printf("%08x:%d,", (unsigned int)edge->owner->obj, (int)edge->value);
            OracleTrace_Printf("\n");
        }
    }
    OracleTrace_Printf("sc.pick cycle=%d ready=", (int)cycle);
    for (node = list; node != NULL; node = node->prev) {
        if (node->flag != 0)
            continue;
        /* (what select counts: the dependents this one is the last unscheduled predecessor of) */
        count = 0;
        for (edge = node->conflicts; edge != NULL; edge = edge->next)
            if (edge->owner->flag == 1)
                count++;
        /* (the machine's check only reads its pipeline state) */
        OracleTrace_Printf("%08x:%d:%d:%d:%d:%d:%d,", (unsigned int)node->obj, (int)node->earliestCycle,
                           (int)node->latestCycle, (int)node->height, count,
                           known ? (int)(machine->check(node->obj) != 0) : -1,
                           known ? (int)rank[node->obj->opcode * 18] : -1);
    }
    picked = select_ready_coloring_node__retail(list, cycle);
    OracleTrace_Printf(" pick=%08x\n", picked != NULL ? (unsigned int)picked->obj : 0);
    return picked;
}
#endif

/* The inliner: whether a call is to be inlined (can_inline), what the body costs where the automatic inliner asks
 * (fn_0055e540, the recursion over the callees' bodies) and what happens to the call (inline_call_expression, which
 * leaves the call as it was when the body is not usable yet). The functions are named as the sources do in 1.3; in
 * 1.3.2 they changed and are not matched, and the original's own names are used (in 2.7, those of its addresses: the
 * code is 1.3.2's, but for where CInlineInfo's kind is). */
#if VERSION >= VERSION_GC_2_7
#define CAN_INLINE fn_00560c80
#define CAN_INLINE_RETAIL fn_00560c80__retail
#define INLINE_CALL fn_00561260
#define INLINE_CALL_RETAIL fn_00561260__retail
#elif VERSION >= VERSION_GC_1_3_2
#define CAN_INLINE fn_0055ed70
#define CAN_INLINE_RETAIL fn_0055ed70__retail
#define INLINE_CALL fn_0055f350
#define INLINE_CALL_RETAIL fn_0055f350__retail
#elif VERSION >= VERSION_GC_1_3
#define CAN_INLINE can_inline
#define CAN_INLINE_RETAIL can_inline__retail
#define INLINE_CALL inline_call_expression
#define INLINE_CALL_RETAIL inline_call_expression__retail
#endif

/* 1.3.2 put three bytes into the options before alwaysinline (0xd5 in 1.3, 0xd8 in 1.3.2), and 2.7 four more before
 * inlineLimit (0x68 in 1.3.2, 0x6c in 2.7; everything from there on moved by four). CInlineInfo's kind is two bytes
 * further on in each (0xa0 in 1.3, as the sources lay it out, 0xa2 in 1.3.2, 0xa4 in 2.7: inline_call_expression's
 * read of it). */
#if VERSION >= VERSION_GC_2_7
#define ALWAYS_INLINE (*((unsigned char *)&copts + 0xdc))
#define INLINE_AUTO (*((unsigned char *)&copts + 0x76))
#define INLINE_LIMIT (*(SInt16 *)((unsigned char *)&copts + 0x6c))
#define INFO_KIND(info) (*((unsigned char *)(info) + 0xa4))
#elif VERSION >= VERSION_GC_1_3_2
#define ALWAYS_INLINE (*((unsigned char *)&copts + 0xd8))
#define INFO_KIND(info) (*((unsigned char *)(info) + 0xa2))
#else
#define ALWAYS_INLINE copts.alwaysinline
#endif
#ifndef INLINE_AUTO
#define INLINE_AUTO copts.inlineAuto
#define INLINE_LIMIT copts.inlineLimit
#endif
#ifndef INFO_KIND
#define INFO_KIND(info) ((info)->kind)
#endif

#ifdef CAN_INLINE
extern SInt16 data_00582468;
extern SInt32 data_005dfb68;
extern Boolean CAN_INLINE_RETAIL(ENode *node);
extern int fn_0055e540__retail(CInlineInfo *info, int running, int depth);
extern ENode *INLINE_CALL_RETAIL(ENode *expr);

/* The callee's name qualified with its classes and namespaces (CActor::GetHealthInfo); its link name is often not made
 * yet when the inliner looks at it. */
static const char *callee_name(Object *function)
{
    static char buffer[512];
    const char *parts[16];
    NameSpace *nspace;
    int count;
    int length;
    int size;

    if (function != NULL && function->datatype == DALIAS)
        function = function->u.alias.object;
    if (function == NULL || function->name == NULL)
        return "?";
    count = 0;
    for (nspace = function->nspace; nspace != NULL && count < 16; nspace = nspace->parent)
        if (nspace->name != NULL)
            parts[count++] = nspace->name->name;
    length = 0;
    while (count-- > 0) {
        size = strlen(parts[count]);
        if (length + size + 2 >= (int)sizeof(buffer) - 1)
            break;
        memcpy(buffer + length, parts[count], size);
        buffer[length + size] = ':';
        buffer[length + size + 1] = ':';
        length += size + 2;
    }
    size = strlen(function->name->name);
    if (length + size >= (int)sizeof(buffer))
        size = sizeof(buffer) - 1 - length;
    memcpy(buffer + length, function->name->name, size);
    buffer[length + size] = 0;
    return buffer;
}

/* The statement the inliner is looking at, for the source line of a call it decides on (1.3 and later keep the line in
 * the statement's sourceoffset, as the PCode does). */
extern Statement *inline_statement__retail(Statement *statement);
static Statement *inline_current;

Statement *inline_statement(Statement *statement)
{
    Statement *saved = inline_current;
    Statement *result;

    inline_current = statement;
    result = inline_statement__retail(statement);
    inline_current = saved;
    return result;
}

static int inline_line(void)
{
    return inline_current != NULL ? (int)inline_current->sourceoffset : -1;
}

/* inl.can: the decision for a call of FUNCTION at the inline depth DEPTH, with the options and the callee's statement
 * count that the decision looks at; tools/oracle.py gives the reason. link is the callee's link name, line the source
 * line of the statement the call is in. */
Boolean CAN_INLINE(ENode *node)
{
    Boolean result;
    Object *function = node->data.objref;
    CInlineInfo *info;

    if (function->datatype == DALIAS)
        function = function->u.alias.object;
    /* u.func.u only holds a body where the decision looks at it (it is something else for other functions) */
    info = NULL;
    if (function->type != NULL && function->type->type == TYPEFUNC &&
        ((function->qual & Q_INLINE) || (TYPE_METHOD(function->type)->flags & 0x800)) &&
        (function->datatype == DFUNC || (function->datatype == DVFUNC && (node->flags & ENODE_FLAG_80))))
        info = (CInlineInfo *)function->u.func.u;
    result = CAN_INLINE_RETAIL(node);
    OracleTrace_Printf("inl.can function=%s result=%d depth=%d inline_bit=%d always=%d auto=%d limit=%d body=%d "
                       "nstmts=%d line=%d link=", callee_name(function), (int)result, (int)data_00582468,
                       (int)((function->qual & Q_INLINE) != 0), (int)ALWAYS_INLINE, (int)INLINE_AUTO,
                       (int)INLINE_LIMIT, info != NULL, info != NULL ? (int)info->nstmts : -1, inline_line());
    OracleTrace_Printf("%s\n", link_name(function));
    return result;
}

static int cost_nest;

/* inl.cost: the automatic inliner's estimate of a body against the threshold (data_005dfb68): the running cost, plus
 * one per statement, plus what fn_0055e600 adds for each statement's expression (it stops once over the threshold).
 * nest is the depth of the expressions being costed when the body is a callee's met inside another body's. */
int fn_0055e540(CInlineInfo *info, int running, int depth)
{
    int result = fn_0055e540__retail(info, running, depth);

    OracleTrace_Printf("inl.cost nstmts=%d running=%d depth=%d cost=%d threshold=%d nest=%d\n",
                       info != NULL ? (int)info->nstmts : -1, running, depth, result, (int)data_005dfb68, cost_nest);
    return result;
}

/* inl.expr, on request (MWCC_ORACLE_INLINE_COST=1): what each expression of a body adds to its cost (fn_0055e600 walks
 * an expression and its operands, and a call's callee body through fn_0055e540), after its operands (theirs have
 * nest one deeper): its ENode type, the object it names or the constant it is, and the cost it added, its operands'
 * included. */
extern int fn_0055e600__retail(ENode *expr, int running, int depth);

static void describe_append(char *buffer, int *length, const char *text)
{
    int size = strlen(text);

    if (*length + size >= 255)
        size = 255 - *length - 1;
    if (size <= 0)
        return;
    memcpy(buffer + *length, text, size);
    *length += size;
    buffer[*length] = 0;
}

/* A short description of an expression, a few levels deep, without spaces: objects by name, constants by value, `*`
 * for an indirection, `+` and `=` for an addition and an assignment, `name()` for a call and `eN` for the other ENode
 * types (N is the type number), `..` where it stops. */
static void describe(ENode *expr, char *buffer, int *length, int level)
{
    char number[16];

    if (expr == NULL) {
        describe_append(buffer, length, "?");
        return;
    }
    if (level > 5) {
        describe_append(buffer, length, "..");
        return;
    }
    switch (expr->type) {
        case EOBJREF:
            describe_append(buffer, length, expr->data.objref != NULL && expr->data.objref->name != NULL
                                                ? expr->data.objref->name->name
                                                : "?");
            return;
        case EINTCONST:
            sprintf(number, "%d", (int)expr->data.intval.lo);
            describe_append(buffer, length, number);
            return;
        case EARGOBJ:
        case ELOCOBJ:
            /* (an inline body's argument or local, by its index) */
            sprintf(number, "%s%d", expr->type == EARGOBJ ? "arg" : "loc", (int)expr->data.longval);
            describe_append(buffer, length, number);
            return;
        case EINDIRECT:
            describe_append(buffer, length, "*(");
            describe(expr->data.monadic, buffer, length, level + 1);
            describe_append(buffer, length, ")");
            return;
        case EADD:
        case EASS:
            describe(expr->data.diadic.left, buffer, length, level + 1);
            describe_append(buffer, length, expr->type == EADD ? "+" : "=");
            describe(expr->data.diadic.right, buffer, length, level + 1);
            return;
        case EFUNCCALL:
        case EFUNCCALLP:
            if (expr->data.funccall.funcref != NULL && expr->data.funccall.funcref->type == EOBJREF)
                describe_append(buffer, length, callee_name(expr->data.funccall.funcref->data.objref));
            else
                describe_append(buffer, length, "(*)");
            describe_append(buffer, length, "()");
            return;
        default:
            sprintf(number, "e%d", (int)expr->type);
            describe_append(buffer, length, number);
            if (expr->type <= ELOGNOT || expr->type == ETYPCON) {
                describe_append(buffer, length, "(");
                describe(expr->data.monadic, buffer, length, level + 1);
                describe_append(buffer, length, ")");
            }
            return;
    }
}

int fn_0055e600(ENode *expr, int running, int depth)
{
    char what[256];
    int length;
    int result;

    if (!wanted("MWCC_ORACLE_INLINE_COST"))
        return fn_0055e600__retail(expr, running, depth);
    cost_nest++;
    result = fn_0055e600__retail(expr, running, depth);
    cost_nest--;
    what[0] = 0;
    length = 0;
    describe(expr, what, &length, 0);
    OracleTrace_Printf("inl.expr nest=%d type=%d cost=%d ", cost_nest, expr != NULL ? (int)expr->type : -1,
                       result - running);
    /* a call of a named function: its link name and its function type's flags (0x100 compiler-generated, 0x1000 a
     * constructor, 0x2000 a destructor) */
    if (expr != NULL && (expr->type == EFUNCCALL || expr->type == EFUNCCALLP) && expr->data.funccall.funcref != NULL &&
        expr->data.funccall.funcref->type == EOBJREF && expr->data.funccall.funcref->data.objref != NULL) {
        Object *callee = expr->data.funccall.funcref->data.objref;

        while (callee->datatype == DALIAS && callee->u.alias.object != NULL)
            callee = callee->u.alias.object;
        OracleTrace_Printf("link=%s flags=%x ", link_name(callee),
                           callee->type != NULL && callee->type->type == TYPEFUNC
                               ? (unsigned int)TYPE_FUNC(callee->type)->flags
                               : 0u);
    }
    OracleTrace_Printf("what=%s\n", what);
    return result;
}

/* inl.call: the expansion of a call, nested as the bodies are (inl.begin and inl.end); expanded=0 when the call is
 * left as it was. */
ENode *INLINE_CALL(ENode *expr)
{
    Object *function = expr->data.monadic->data.objref;
    CInlineInfo *info = NULL;
    ENode *result;

    if (function != NULL && function->datatype == DALIAS)
        function = function->u.alias.object;
    if (function != NULL && (function->datatype == DFUNC || function->datatype == DVFUNC) &&
        (unsigned long)function->u.func.u >= 0x10000)
        info = (CInlineInfo *)function->u.func.u;

    OracleTrace_Printf("inl.begin function=%s depth=%d body=%d kind=%d line=%d link=", callee_name(function),
                       (int)data_00582468, info != NULL, info != NULL ? (int)INFO_KIND(info) : -1, inline_line());
    OracleTrace_Printf("%s\n", function != NULL ? link_name(function) : "?");
    result = INLINE_CALL_RETAIL(expr);
    OracleTrace_Printf("inl.end function=%s expanded=%d\n", callee_name(function), result != expr);
    return result;
}
#endif

#endif
