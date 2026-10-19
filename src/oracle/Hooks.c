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
#include "compiler/CPrec.h"
#include "compiler/CTemplateNew.h"
#include "compiler/templates.h"
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
#include "compiler/IroDump.h"
#include "compiler/IroLoop.h"

#ifdef MWCC_ORACLE_TRACE

#ifdef MWCC_ORACLE_RUST_HOOKS
extern const char *OracleRust_LinkName(Object *object);
extern void OracleRust_PrintInstruction(const char *prefix, PCodeInstruction *instruction);
extern void OracleRust_Snapshot(const char *prefix, const char *phase, Object *function);
#define link_name OracleRust_LinkName
#define print_instruction OracleRust_PrintInstruction
#define snapshot_as OracleRust_Snapshot
#else
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

/* The PCode of FUNCTION, its lines prefixed with PREFIX ("ra" for the allocation's, "st" for a stage's). */
static void snapshot_as(const char *prefix, const char *phase, Object *function)
{
    PCodeBlock *block;
    PCodeInstruction *instruction;
    PCodeBlockLink *link;
    char line_prefix[8];

    OracleTrace_Printf("%s.%s function=%s link=%s\n", prefix, phase, function->name->name, link_name(function));
    for (block = gPCodeBlocks; block != NULL; block = block->next) {
        OracleTrace_Printf("%s.block p=%08x index=%d weight=%d flags=%04x succ=", prefix, (unsigned int)block,
                           block->index, block->execution_weight, (unsigned int)block->flags);
        for (link = block->successors; link != NULL; link = link->next) {
            OracleTrace_Printf("%08x,", (unsigned int)link->payload.block);
        }
        OracleTrace_Printf("\n");
        sprintf(line_prefix, "%s.i", prefix);
        for (instruction = block->instructions; instruction != NULL; instruction = instruction->next)
            print_instruction(line_prefix, instruction);
    }
    OracleTrace_Printf("%s.end\n", prefix);
}
#endif

#ifndef MWCC_ORACLE_RUST_HOOKS
static void snapshot(const char *phase, Object *function)
{
    snapshot_as("ra", phase, function);
}
#endif

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
#ifndef MWCC_ORACLE_RUST_HOOKS
extern void ALLOCATE_REGISTERS_RETAIL(Object *function);
static void origins_reset(void);
void cse_vregs(void);

/* The register allocation of a function: Coloring_AllocateRegisters in the sources. Its inlining is over by then. */
void ALLOCATE_REGISTERS(Object *function)
{
    origins_reset();
    cse_vregs();
    snapshot("before", function);
    ALLOCATE_REGISTERS_RETAIL(function);
    snapshot("after", function);
}
#endif /* !MWCC_ORACLE_RUST_HOOKS: allocation entry and origin reset live in Rust. */
#endif

#if VERSION >= VERSION_GC_1_3
#ifndef MWCC_ORACLE_RUST_HOOKS
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

#endif /* !MWCC_ORACLE_RUST_HOOKS: graph simplification/selection live in the Rust object. */

/* The stack frame once it is laid out (fn_004f7fb0, after the allocation: StackFrameEABI_FinalizeLayout and the slots
 * of the compiler's temporaries in 1.2.5): fr.local, each local in the order of the function's list, with its offset
 * in the locals area (u.var.uid; the offset from r1 adds the linkage area and the outgoing arguments), size and
 * register (0 when it is in memory). */
extern void fn_004f7fb0__retail(void);
extern ObjectList *locals;
extern ObjectList *temporary_objects; /* (a list of TemporaryObjectEntry, which has the same layout) */

#ifndef MWCC_ORACLE_RUST_HOOKS
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
#endif

/* The list scheduler, on request (MWCC_ORACLE_SCHEDULE=1): schedule_block orders the instructions of one block, cycle
 * by cycle, picking among the ready ones with select_ready_coloring_node (the sources' Scheduler.c; 1.3 to 2.7 run the
 * same code, with the machine's check one word further on and another opcode table). sc.block: the block, whether the
 * registers are still virtual (the scheduling before the allocation) and its instructions in their order before;
 * sc.node: the dependency graph once it is built (each instruction's latency, height, latest cycle, unscheduled
 * predecessors and the instructions that depend on it with the latency of each edge); sc.pick: for each pick, the
 * cycle and every instruction whose predecessors are all scheduled, with its earliest and latest cycle, height, the
 * dependents it would make ready, whether the machine can issue it this cycle and its opcode's rank; then what was
 * picked (0 when nothing could be: the cycle ends); sc.after: the order it left. */
#ifndef MWCC_ORACLE_RUST_HOOKS
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
#endif /* !MWCC_ORACLE_RUST_HOOKS: scheduler observations live in Rust. */
#endif

/* The inliner: whether a call is to be inlined (can_inline), what the body costs where the automatic inliner asks
 * (fn_0055e540, the recursion over the callees' bodies) and what happens to the call (inline_call_expression, which
 * leaves the call as it was when the body is not usable yet). The functions are named as the sources do in 1.3; in
 * 1.3.2 they changed and are not matched, and the original's own names are used (in 2.6 and 2.7, 2.7's addresses: the
 * code is 1.3.2's, but for where CInlineInfo's kind is). */
#if VERSION >= VERSION_GC_2_6
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
 * inlineLimit (0x68 in 1.3.2, 0x6c in 2.7; everything from there on moved by four). The trace's auto= is
 * inline_bottom_up (0x72, 0x76 in 2.7), as the pragma tables name it; auto_inline (0x89, 0x8d in 2.7) is not traced. CInlineInfo's kind is two bytes
 * further on in each (0xa0 in 1.3, as the sources lay it out, 0xa2 in 1.3.2, 0xa4 in 2.7: inline_call_expression's
 * read of it). 2.6 has 2.7's offsets. */
#if VERSION >= VERSION_GC_2_6
#define ALWAYS_INLINE (*((unsigned char *)&copts + 0xdc))
#define INLINE_BOTTOM_UP (*((unsigned char *)&copts + 0x76))
#define INLINE_LIMIT (*(SInt16 *)((unsigned char *)&copts + 0x6c))
#define INFO_KIND(info) (*((unsigned char *)(info) + 0xa4))
#elif VERSION >= VERSION_GC_1_3_2
#define ALWAYS_INLINE (*((unsigned char *)&copts + 0xd8))
#define INFO_KIND(info) (*((unsigned char *)(info) + 0xa2))
#else
#define ALWAYS_INLINE copts.alwaysinline
#endif
#ifndef INLINE_BOTTOM_UP
#define INLINE_BOTTOM_UP copts.inlineBottomUp
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

/* Where the calls the inliner decides on come from: the inliner goes over the function again and again (the inline
 * depth is the round), and a call met in a later round is one of an inlined body. Each body expanded has the calls in
 * the code it made entered here (their function reference node: the node can_inline is given), with the entry of the
 * call that was expanded as the parent, so a decision's chain (chain=, the inlined bodies it is in, outermost first)
 * is the parents' callees. Emptied at each function's register allocation, the inlining being over. */
#ifdef MWCC_ORACLE_RUST_HOOKS
extern int OracleRust_OriginFind(ENode *funcref);
extern void OracleRust_OriginWalk(ENode *expr, int parent, const char *link, int level);
extern void OracleRust_PrintChain(ENode *funcref);
extern int OracleRust_InlineLine(void);
#define origin_find OracleRust_OriginFind
#define origin_walk OracleRust_OriginWalk
#define print_chain OracleRust_PrintChain
#define inline_line OracleRust_InlineLine
#else
#define ORIGIN_SIZE 0x10000
typedef struct Origin {
    ENode *funcref;
    int parent;
    const char *link;
} Origin;
static Origin origins[ORIGIN_SIZE];
static int origins_used;

static void origins_reset(void)
{
    memset(origins, 0, sizeof(origins));
    origins_used = 0;
}

static int origin_find(ENode *funcref)
{
    unsigned int slot = ((unsigned int)funcref >> 2) & (ORIGIN_SIZE - 1);
    int tries;

    for (tries = 0; tries < ORIGIN_SIZE && origins[slot].funcref != NULL; tries++) {
        if (origins[slot].funcref == funcref)
            return (int)slot;
        slot = (slot + 1) & (ORIGIN_SIZE - 1);
    }
    return -1;
}

static void origin_add(ENode *funcref, int parent, const char *link)
{
    unsigned int slot = ((unsigned int)funcref >> 2) & (ORIGIN_SIZE - 1);

    /* (kept below three quarters full; past that, calls go without a chain) */
    if (origins_used >= ORIGIN_SIZE / 4 * 3)
        return;
    while (origins[slot].funcref != NULL && origins[slot].funcref != funcref)
        slot = (slot + 1) & (ORIGIN_SIZE - 1);
    if (origins[slot].funcref == NULL)
        origins_used++;
    origins[slot].funcref = funcref;
    origins[slot].parent = parent;
    origins[slot].link = link;
}

/* Enter the calls of an expression made by expanding a body (LINK, under the entry PARENT). */
static void origin_walk(ENode *expr, int parent, const char *link, int level)
{
    ENodeList *arg;

    if (expr == NULL || level > 200)
        return;
    switch (expr->type) {
        case EPOSTINC: case EPOSTDEC: case EPREINC: case EPREDEC: case EINDIRECT: case EMONMIN: case EBINNOT:
        case ELOGNOT: case EFORCELOAD: case ETYPCON: case EBITFIELD:
            origin_walk(expr->data.monadic, parent, link, level + 1);
            return;
        case EMUL: case EMULV: case EDIV: case EMODULO: case EADDV: case ESUBV: case EADD: case ESUB: case ESHL:
        case ESHR: case ELESS: case EGREATER: case ELESSEQU: case EGREATEREQU: case EEQU: case ENOTEQU: case EAND:
        case EXOR: case EOR: case ELAND: case ELOR: case EASS: case EMULASS: case EDIVASS: case EMODASS: case EADDASS:
        case ESUBASS: case ESHLASS: case ESHRASS: case EANDASS: case EXORASS: case EORASS: case ECOMMA: case EPMODULO:
        case EROTL: case EROTR: case EBCLR: case EBTST: case EBSET: case ENULLCHECK: case EMFPOINTER:
            origin_walk(expr->data.diadic.left, parent, link, level + 1);
            origin_walk(expr->data.diadic.right, parent, link, level + 1);
            return;
        case ECOND:
        case ECONDASS:
            origin_walk(expr->data.cond.cond, parent, link, level + 1);
            origin_walk(expr->data.cond.expr1, parent, link, level + 1);
            origin_walk(expr->data.cond.expr2, parent, link, level + 1);
            return;
        case EFUNCCALL:
        case EFUNCCALLP:
            if (expr->data.funccall.funcref != NULL && expr->data.funccall.funcref->type == EOBJREF)
                origin_add(expr->data.funccall.funcref, parent, link);
            else
                origin_walk(expr->data.funccall.funcref, parent, link, level + 1);
            for (arg = expr->data.funccall.args; arg != NULL; arg = arg->next)
                origin_walk(arg->node, parent, link, level + 1);
            return;
        default:
            return;
    }
}

/* The chain of a call's function reference node, outermost first, separated by | (link names have < and >). */
static void print_chain(ENode *funcref)
{
    int path[32];
    int count = 0;
    int entry = origin_find(funcref);

    while (entry >= 0 && count < 32) {
        path[count++] = entry;
        entry = origins[entry].parent;
    }
    while (count-- > 0)
        OracleTrace_Printf("%s%s", origins[path[count]].link, count > 0 ? "|" : "");
}

extern Statement *generate_inline_statements__retail(Object *function, Statement *tail, CInlineInfo *args,
                                                     ENode *result, CLabel *returnLabel, Object *returnObject,
                                                     UInt8 appendStatement);

/* A body expanded as statements, after TAIL (which it reuses): the statements up to the one it returns are new. */
Statement *generate_inline_statements(Object *function, Statement *tail, CInlineInfo *args, ENode *result,
                                      CLabel *returnLabel, Object *returnObject, UInt8 appendStatement)
{
    int parent = result != NULL ? origin_find(result->data.funccall.funcref) : -1;
    const char *link = link_name(function);
    Statement *last = generate_inline_statements__retail(function, tail, args, result, returnLabel, returnObject,
                                                         appendStatement);
    Statement *statement;

    for (statement = tail; statement != NULL; statement = statement->next) {
        switch (statement->type) {
            case ST_EXPRESSION:
            case ST_SWITCH:
            case ST_IFGOTO:
            case ST_IFNGOTO:
            case ST_RETURN:
            case ST_GOTOEXPR:
                origin_walk(statement->expr, parent, link, 0);
                break;
        }
        if (statement == last)
            break;
    }
    return last;
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
#endif /* !MWCC_ORACLE_RUST_HOOKS: origin table/walking and statement context live in Rust. */

/* inl.can: the decision for a call of FUNCTION at the inline depth DEPTH, with the options and the callee's statement
 * count that the decision looks at; tools/oracle.py gives the reason. link is the callee's link name, line the source
 * line of the statement the call is in. */
#ifndef MWCC_ORACLE_RUST_FRONTEND
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
                       (int)((function->qual & Q_INLINE) != 0), (int)ALWAYS_INLINE, (int)INLINE_BOTTOM_UP,
                       (int)INLINE_LIMIT, info != NULL, info != NULL ? (int)info->nstmts : -1, inline_line());
    OracleTrace_Printf("%s chain=", link_name(function));
    print_chain(node);
    OracleTrace_Printf("\n");
    return result;
}

static int cost_nest;

/* inl.cost: the automatic inliner's estimate of a body against the threshold (data_005dfb68): the running cost, plus
 * one per statement, plus what fn_0055e600 adds for each statement's expression (it stops once over the threshold).
 * nest is the depth of the expressions being costed when the body is a callee's met inside another body's. */
extern UInt8 data_005dfb66;
extern Object *data_005dfb62;
static int cost_quiet;

/* With MWCC_ORACLE_FULL_COST=1, a body's estimate is made a second time, traced, past the threshold (raised out of
 * reach), for its whole cost (full=); the first, untraced, is the compiler's own and is what is returned. The estimate
 * changes nothing but data_005dfb66 (whether the function being compiled was met: it is only ever set), which is put
 * back. The compiler makes the estimate the threshold plus one when it meets a call of an inline function at the inline
 * depth limit (unless the function being compiled was met: data_005dfb66), so the body is never inlined; an untraced
 * estimate with the raised threshold says whether that happened (limit=1), and the traced one is made with
 * data_005dfb66 set, where such a call adds nothing (its arguments are counted). */
#define FULL_COST_LIMIT 0x3fffffff

int fn_0055e540(CInlineInfo *info, int running, int depth)
{
    int result;
    int full;
    SInt32 threshold;
    UInt8 met;
    UInt8 after;
    int limited;

    if (cost_nest == 0 && !cost_quiet && wanted("MWCC_ORACLE_FULL_COST")) {
        met = data_005dfb66;
        cost_quiet = 1;
        result = fn_0055e540__retail(info, running, depth);
        cost_quiet = 0;
        after = data_005dfb66;
        threshold = data_005dfb68;
        /* (the traced pass starts from the state the compiler's started from, and leaves the state it left) */
        data_005dfb66 = met;
        data_005dfb68 = FULL_COST_LIMIT;
        cost_quiet = 1;
        limited = fn_0055e540__retail(info, running, depth) > FULL_COST_LIMIT;
        cost_quiet = 0;
        data_005dfb66 = 1;
        full = fn_0055e540__retail(info, running, depth);
        data_005dfb68 = threshold;
        data_005dfb66 = after;
        OracleTrace_Printf("inl.cost nstmts=%d running=%d depth=%d cost=%d threshold=%d nest=%d full=%d limit=%d\n",
                           info != NULL ? (int)info->nstmts : -1, running, depth, result, (int)threshold, cost_nest,
                           full, limited);
        return result;
    }
    result = fn_0055e540__retail(info, running, depth);
    if (!cost_quiet)
        OracleTrace_Printf("inl.cost nstmts=%d running=%d depth=%d cost=%d threshold=%d nest=%d\n",
                           info != NULL ? (int)info->nstmts : -1, running, depth, result, (int)data_005dfb68,
                           cost_nest);
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

/* The retail fn_0055e600 costs an expression's operands itself (its calls to itself are internal, not hooked), so only
 * the expressions a body's statements are made of are traced. body_calls walks one as it does, listing the calls of
 * inline functions in the order it costs them (their bodies are the inl.cost lines one deeper, in that
 * order), with ! before one at the inline depth limit (depth 0: charged, or nothing, not costed as its body) and ? before
 * an inline function with no body yet. */
static void body_calls(ENode *node, int depth, char *buffer, int *length)
{
    ENodeList *arg;
    Object *callee;
    const char *link;
    int size;

    if (node == NULL)
        return;
    switch (node->type) {
        case EPOSTINC: case EPOSTDEC: case EPREINC: case EPREDEC: case EINDIRECT: case EMONMIN: case EBINNOT:
        case ELOGNOT: case EFORCELOAD: case ETYPCON: case EBITFIELD:
            body_calls(node->data.monadic, depth, buffer, length);
            break;
        case EMUL: case EMULV: case EDIV: case EMODULO: case EADDV: case ESUBV: case EADD: case ESUB: case ESHL:
        case ESHR: case ELESS: case EGREATER: case ELESSEQU: case EGREATEREQU: case EEQU: case ENOTEQU: case EAND:
        case EXOR: case EOR: case ELAND: case ELOR: case EASS: case EMULASS: case EDIVASS: case EMODASS: case EADDASS:
        case ESUBASS: case ESHLASS: case ESHRASS: case EANDASS: case EXORASS: case EORASS: case ECOMMA: case EPMODULO:
        case EROTL: case EROTR: case EBCLR: case EBTST: case EBSET: case ENULLCHECK: case EMFPOINTER:
            body_calls(node->data.diadic.left, depth, buffer, length);
            body_calls(node->data.diadic.right, depth, buffer, length);
            break;
        case EFUNCCALL:
        case EFUNCCALLP:
            if (node->data.funccall.funcref->type == EOBJREF) {
                callee = node->data.funccall.funcref->data.objref;
                if (callee->type != NULL && callee->type->type == TYPEFUNC &&
                    ((callee->qual & Q_INLINE) || (TYPE_METHOD(callee->type)->flags & 0x800)) &&
                    (callee->datatype == DFUNC ||
                     (callee->datatype == DVFUNC && (node->data.funccall.funcref->flags & ENODE_FLAG_80)))) {
                    /* (? before a candidate with no body yet: costed as a call, one) */
                    int mark = callee->u.func.u == NULL ? '?' : depth == 0 ? '!' : 0;
                    while (callee->datatype == DALIAS && callee->u.alias.object != NULL)
                        callee = callee->u.alias.object;
                    link = link_name(callee);
                    size = strlen(link);
                    if (*length + size + 3 < 4000) {
                        if (*length)
                            buffer[(*length)++] = '|';
                        if (mark)
                            buffer[(*length)++] = (char)mark;
                        memcpy(buffer + *length, link, size);
                        *length += size;
                        buffer[*length] = 0;
                    }
                }
            }
            for (arg = node->data.funccall.args; arg != NULL; arg = arg->next)
                body_calls(arg->node, depth, buffer, length);
            break;
        case ECOND:
            body_calls(node->data.cond.cond, depth, buffer, length);
            body_calls(node->data.cond.expr1, depth, buffer, length);
            body_calls(node->data.cond.expr2, depth, buffer, length);
            break;
        case EMEMBER:
            if (((ENodeList *)node->data.monadic)->node != NULL)
                body_calls(((ENodeList *)node->data.monadic)->node, depth, buffer, length);
            break;
        default:
            break;
    }
}

int fn_0055e600(ENode *expr, int running, int depth)
{
    static char calls[4096];
    int calls_length;
    char what[256];
    int length;
    int result;

    /* (MWCC_ORACLE_FULL_COST needs this too: it is what keeps the nested bodies' estimates apart from the top one) */
    if (cost_quiet || !(wanted("MWCC_ORACLE_INLINE_COST") || wanted("MWCC_ORACLE_FULL_COST")))
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

        /* limit=1: an inline function with a body met at the inline depth limit, the call the compiler charges the
         * threshold plus one for (the test fn_0055e600 makes, on the object as it is referenced, before the alias) */
        /* cand=1: an inline candidate (inline, or a method defined in its class, and called directly); body=1: its
         * body is saved, so it is costed as its body (or charged at the depth limit), else as a call (one) */
        int candidate = callee->type != NULL && callee->type->type == TYPEFUNC &&
                        ((callee->qual & Q_INLINE) || (TYPE_METHOD(callee->type)->flags & 0x800)) &&
                        (callee->datatype == DFUNC ||
                         (callee->datatype == DVFUNC && (expr->data.funccall.funcref->flags & ENODE_FLAG_80)));
        int body = candidate && callee->u.func.u != NULL;
        int at_limit = depth == 0 && body && callee != data_005dfb62;

        while (callee->datatype == DALIAS && callee->u.alias.object != NULL)
            callee = callee->u.alias.object;
        OracleTrace_Printf("link=%s flags=%x cand=%d body=%d %s", link_name(callee),
                           callee->type != NULL && callee->type->type == TYPEFUNC
                               ? (unsigned int)TYPE_FUNC(callee->type)->flags
                               : 0u,
                           candidate, body, at_limit ? "limit=1 " : "");
    }
    calls[0] = 0;
    calls_length = 0;
    body_calls(expr, depth, calls, &calls_length);
    if (calls_length)
        OracleTrace_Printf("calls=%s ", calls);
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
    int parent;

    if (function != NULL && function->datatype == DALIAS)
        function = function->u.alias.object;
    if (function != NULL && (function->datatype == DFUNC || function->datatype == DVFUNC) &&
        (unsigned long)function->u.func.u >= 0x10000)
        info = (CInlineInfo *)function->u.func.u;

    OracleTrace_Printf("inl.begin function=%s depth=%d body=%d kind=%d line=%d link=", callee_name(function),
                       (int)data_00582468, info != NULL, info != NULL ? (int)INFO_KIND(info) : -1, inline_line());
    OracleTrace_Printf("%s\n", function != NULL ? link_name(function) : "?");
    parent = origin_find(expr->data.funccall.funcref);
    result = INLINE_CALL_RETAIL(expr);
    OracleTrace_Printf("inl.end function=%s expanded=%d\n", callee_name(function), result != expr);
    /* (a body expanded in place, as an expression: the calls in it are the body's) */
    if (result != expr && function != NULL)
        origin_walk(result, parent, link_name(function), 0);
    return result;
}
#endif /* !MWCC_ORACLE_RUST_FRONTEND */
#endif

#if VERSION >= VERSION_GC_1_3
/* With MWCC_ORACLE_INSTANTIATION=1, when each function's body comes to be (tp.*): from= is the function being parsed
 * then (cscope_currentfunc), expanding= the one whose calls are being inlined (data_005dfb62), - for none.
 *   tp.queue kind= link= new=   an inline definition (kind 0: a class's member, a template class's included) or a
 *                               template function instance (kind 2) put on the queue of bodies to parse
 *   tp.need link= result=       the inliner asks for a template class member's body (result=1: queued)
 *   tp.parse kind= link= body=  a queued definition parsed (kind 3: a compiler-made method, 1: a template member);
 *                               body=1 when the inliner has its body after
 *   tp.func link=               a template function instance compiled
 *   tp.gen link=                a function's code generated from its saved body (inlining deferred) */
#if VERSION >= VERSION_GC_2_6
#define DISPATCH fn_0055c730
#define DISPATCH_RETAIL fn_0055c730__retail
#elif VERSION >= VERSION_GC_1_3_2
#define DISPATCH fn_0055a7f0
#define DISPATCH_RETAIL fn_0055a7f0__retail
#else
#define DISPATCH CInline_DispatchNextDeferredNode
#define DISPATCH_RETAIL CInline_DispatchNextDeferredNode__retail
#endif

extern CPrecNode *pending_prec_nodes;
extern InlineNode *deferredInlineNodes;
extern Boolean DISPATCH_RETAIL(void);
extern void CInline_AddFunctionPrecNode__retail(Object *func, TypeClass *value, FileOffsetInfo *key, TokenStream *pair,
                                                Boolean flag);
extern void CInline_AddSpecialization__retail(Object *func, void *definition, void *specialization);
extern Boolean CTemplateNew_InstantiateInlineTemplateObject__retail(Object *obj);
extern Boolean CTemplateNew_InstantiateFunction__retail(TemplateFunction *definition, TemplFuncInstance *specialization,
                                                        Boolean report);
extern void generate_inline_code__retail(Object *object, CInlineInfo *input, char mode);

static const char *maybe_link(Object *object)
{
    return object != NULL ? link_name(object) : "-";
}

static void tp_context(void)
{
    OracleTrace_Printf("from=%s ", maybe_link(cscope_currentfunc));
    OracleTrace_Printf("expanding=%s ", maybe_link(data_005dfb62));
}

static int has_body(Object *object)
{
    return object != NULL && (unsigned long)object->u.func.u >= 0x10000;
}

#ifndef MWCC_ORACLE_RUST_HOOKS
void CInline_AddFunctionPrecNode(Object *func, TypeClass *value, FileOffsetInfo *key, TokenStream *pair, Boolean flag)
{
    CPrecNode *before = flag ? pendingInlineWork : pending_prec_nodes;

    CInline_AddFunctionPrecNode__retail(func, value, key, pair, flag);
    if (!wanted("MWCC_ORACLE_INSTANTIATION"))
        return;
    OracleTrace_Printf("tp.queue kind=0 pending=%d new=%d ", (int)flag,
                       (flag ? pendingInlineWork : pending_prec_nodes) != before);
    tp_context();
    OracleTrace_Printf("link=%s\n", link_name(func));
}

void CInline_AddSpecialization(Object *func, void *definition, void *specialization)
{
    CPrecNode *before = pendingInlineWork;

    CInline_AddSpecialization__retail(func, definition, specialization);
    if (!wanted("MWCC_ORACLE_INSTANTIATION"))
        return;
    OracleTrace_Printf("tp.queue kind=2 pending=1 new=%d ", pendingInlineWork != before);
    tp_context();
    OracleTrace_Printf("link=%s\n", link_name(func));
}

Boolean CTemplateNew_InstantiateInlineTemplateObject(Object *obj)
{
    Boolean result = CTemplateNew_InstantiateInlineTemplateObject__retail(obj);

    if (wanted("MWCC_ORACLE_INSTANTIATION")) {
        OracleTrace_Printf("tp.need result=%d ", (int)result);
        tp_context();
        OracleTrace_Printf("link=%s\n", link_name(obj));
    }
    return result;
}

Boolean CTemplateNew_InstantiateFunction(TemplateFunction *definition, TemplFuncInstance *specialization, Boolean report)
{
    Boolean result;

    if (wanted("MWCC_ORACLE_INSTANTIATION")) {
        OracleTrace_Printf("tp.func ");
        tp_context();
        OracleTrace_Printf("link=%s\n", maybe_link(specialization != NULL ? specialization->object : NULL));
    }
    result = CTemplateNew_InstantiateFunction__retail(definition, specialization, report);
    return result;
}

#endif /* !MWCC_ORACLE_RUST_HOOKS: common instantiation observers live in Rust. */

#ifndef MWCC_ORACLE_RUST_FRONTEND
Boolean DISPATCH(void)
{
    CPrecNode *work = pending_prec_nodes;
    Boolean result;

    if (!wanted("MWCC_ORACLE_INSTANTIATION"))
        return DISPATCH_RETAIL();
    if (work != NULL && !anyerrors)
        OracleTrace_Printf("tp.parse.begin kind=%d link=%s\n", (int)work->kind, maybe_link(work->obj));
    result = DISPATCH_RETAIL();
    if (work != NULL && result && pending_prec_nodes != work)
        OracleTrace_Printf("tp.parse kind=%d body=%d link=%s\n", (int)work->kind, has_body(work->obj),
                           maybe_link(work->obj));
    return result;
}

#endif /* !MWCC_ORACLE_RUST_FRONTEND */

/* tp.emit: fn_0050f240 marks a function referenced; an inline one with a body not generated yet is then queued for
 * code generation (emitted), whatever becomes of the references. caller= is the return address (the compiler
 * function that referenced it: an exception action, a vtable, an initializer, the calls left in a function...). */
extern void fn_0050f240__retail(Object *object);

#ifndef MWCC_ORACLE_RUST_HOOKS
void fn_0050f240(Object *object)
{
    unsigned long caller;
    InlineNode *before = deferredInlineNodes;

    asm {
        mov eax, [ebp + 4]
        mov caller, eax
    }
    fn_0050f240__retail(object);
    if (wanted("MWCC_ORACLE_INSTANTIATION") && deferredInlineNodes != before) {
        OracleTrace_Printf("tp.emit caller=%lx ", caller);
        tp_context();
        OracleTrace_Printf("link=%s\n", maybe_link(object));
    }
}

void generate_inline_code(Object *object, CInlineInfo *input, char mode)
{
    if (wanted("MWCC_ORACLE_INSTANTIATION"))
        OracleTrace_Printf("tp.gen link=%s\n", maybe_link(object));
    generate_inline_code__retail(object, input, mode);
}

#endif /* !MWCC_ORACLE_RUST_HOOKS: deferred emission observers live in Rust. */

/* The optimizer's own dump (IroDump: the IR after its passes, and what some of them did, such as the parts of local
 * aggregates IRO_ScalarizeClassDataMembers, fn_00460780, records), which the compiler turns off before it opens the
 * dump file (fn_00454030, once a translation unit): MWCC_ORACLE_IRO_DUMP=PATH turns it on, into PATH. */
extern FILE *iro_dump_output;
extern void fn_00454030__retail(void);

#ifndef MWCC_ORACLE_RUST_HOOKS
void fn_00454030(void)
{
    const char *path = getenv("MWCC_ORACLE_IRO_DUMP");

    if (path != NULL && path[0] != 0) {
        if (iro_dump_output == NULL)
            iro_dump_output = fopen(path, "a");
        INT_005882b8 = iro_dump_output != NULL;
        return;
    }
    fn_00454030__retail();
}
#endif

/* lc.*: which local aggregates IRO_ScalarizeClassDataMembers (fn_00460780, once or twice a function) turns into
 * scalars a part at a time (and which it leaves in memory), with MWCC_ORACLE_LOCALS=1. Its parts list is
 * class_data_parts (named as in 1.2.5); with the pointer analysis flag (iro_pointer_analysis) an address-taken local is
 * recorded too, and left in memory only if a call may reach it (fn_00460c90). A part (IROVarPart, packed): flags (1:
 * the variable is left in memory) at 4, the VarRecord at 6, the first and last bit at 0x12 and 0x16, the type at
 * 0x1a, the next part at 0x1e, the uses at 0x22; a VarRecord: the object at 2, noregister (its address taken) at 0xb.
 * The relink moves .bss, so these are read by name, not at their addresses. */
extern struct IROVarPart *class_data_parts;
extern UInt8 iro_pointer_analysis;
extern Object *data_005875b8;
extern void fn_00460780__retail(void);
extern Boolean fn_00460c90__retail(IROLinear *node);
extern Object *create_temp_object__retail(Type *type);
#ifndef MWCC_ORACLE_RUST_HOOKS
static Boolean in_scalarize;

/* lc.temp: the scalar the pass makes for a part (one for each part not blocked, in the parts' order; before the
 * pass's lc.pass line). */
Object *create_temp_object(Type *type)
{
    unsigned long caller;
    Object *object;

    asm {
        mov eax, [ebp + 4]
        mov caller, eax
    }
    object = create_temp_object__retail(type);
    if (wanted("MWCC_ORACLE_CSE"))
        OracleTrace_Printf("ce.object name=%s caller=%lx\n", object && object->name ? object->name->name : "?", caller);
    if (in_scalarize && wanted("MWCC_ORACLE_LOCALS"))
        OracleTrace_Printf("lc.temp name=%s\n", object && object->name ? object->name->name : "?");
    return object;
}

/* lc.use: each use of a local the pass records (record_var_part_use), before it is recorded: the node (kind 17: a
 * call, whose argument it is), the offset (-1: the whole, or an offset not known) and the type used there. A use that
 * overlaps another differently leaves the local in memory, and makes no part of its own. */
extern int fn_004601f0__retail(IROLinear *node, char *var, SInt32 offset, Type *type);
int fn_004601f0(IROLinear *node, char *var, SInt32 offset, Type *type)
{
    Object *object;

    if (in_scalarize && wanted("MWCC_ORACLE_LOCALS")) {
        object = *(Object **)(var + 2);
        OracleTrace_Printf("lc.use node=%d kind=%d offset=%ld type=%d size=%ld var=%s\n", node->index, node->type,
                           offset, type ? type->type : -1, type ? type->size : -1,
                           object && object->name ? object->name->name : "?");
    }
    return fn_004601f0__retail(node, var, offset, type);
}

void fn_00460780(void)
{
    char *part;
    char *var;
    char *use;
    Object *object;
    Type *type;
    int uses;

    in_scalarize = 1;
    fn_00460780__retail();
    in_scalarize = 0;
    if (!wanted("MWCC_ORACLE_LOCALS"))
        return;
    OracleTrace_Printf("lc.pass pointers=%d link=%s\n", iro_pointer_analysis, maybe_link(data_005875b8));
    for (part = (char *)class_data_parts; part != NULL; part = *(char **)(part + 0x1e)) {
        var = *(char **)(part + 6);
        object = *(Object **)(var + 2);
        type = *(Type **)(part + 0x1a);
        uses = 0;
        for (use = *(char **)(part + 0x22); use != NULL; use = *(char **)(use + 4))
            uses++;
        OracleTrace_Printf("lc.part blocked=%d noregister=%d bits=%ld..%ld type=%d size=%ld uses=%d var_size=%ld "
                           "var=%s\n",
                           part[4] & 1, var[0xb], *(SInt32 *)(part + 0x12), *(SInt32 *)(part + 0x16),
                           type ? type->type : -1, type ? type->size : -1, uses,
                           object && object->type ? object->type->size : -1,
                           object && object->name ? object->name->name : "?");
    }
}
#endif /* !MWCC_ORACLE_RUST_HOOKS: scalarization and temporary observation live in Rust. */

/* lc.call: a call through which a local recorded may be reached (pointer analysis): those whose address is taken are
 * then left in memory. */
#ifndef MWCC_ORACLE_RUST_HOOKS
Boolean fn_00460c90(IROLinear *node)
{
    Boolean result = fn_00460c90__retail(node);

    if (result && wanted("MWCC_ORACLE_LOCALS"))
        OracleTrace_Printf("lc.call node=%d\n", node->index);
    return result;
}
#endif

/* lc.arg: how the inliner binds each formal of a call it expands (setup_inline_locals_and_arguments): unused, the
 * argument substituted for it (no side effects, and dirty set), a reference bound to the object the argument makes
 * (a constructor's or a by-value return's temporary: how a by-value class argument comes, its copy made by the
 * caller), or a fresh local the argument is copied to (var=, as the frame names it; for a reference, ref=1, the local
 * holds the address). object= is the object an aliased formal stands for, when it is one (EOBJREF). declared= and
 * declared_size= are the kind and size of the formal's type as the function declares it (the inline variable of a
 * by-value class formal is already a reference: declared=5 there, with ref=1), -1 when no argument has its name. */
/* A short form of EXPR (an argument) appended to BUFFER (of SIZE, kept terminated), without spaces: objects by
 * name, *x, (a+b), f(first argument,...), (a,b) for a comma, #N for any other node kind; DEPTH levels deep. */
#ifdef MWCC_ORACLE_RUST_HOOKS
extern void OracleRust_DescribeExpression(char *buffer, size_t size, ENode *expr, int depth);
#define describe_expression OracleRust_DescribeExpression
#else
static void describe_expression(char *buffer, size_t size, ENode *expr, int depth)
{
    char piece[64];
    size_t used = strlen(buffer);

    if (used + 8 >= size)
        return;
    if (expr == NULL) {
        strcat(buffer, "?");
        return;
    }
    if (depth <= 0) {
        strcat(buffer, "..");
        return;
    }
    switch (expr->type) {
    case EOBJREF:
        sprintf(piece, "%.48s", expr->data.objref->name ? expr->data.objref->name->name : "?");
        strcat(buffer, piece);
        return;
    case EINDIRECT:
        strcat(buffer, "*");
        describe_expression(buffer, size, expr->data.monadic, depth - 1);
        return;
    case EFORCELOAD:
    case ETYPCON:
        describe_expression(buffer, size, expr->data.monadic, depth);
        return;
    case EADD:
    case ECOMMA:
        strcat(buffer, "(");
        describe_expression(buffer, size, expr->data.diadic.left, depth - 1);
        strcat(buffer, expr->type == EADD ? "+" : ",");
        describe_expression(buffer, size, expr->data.diadic.right, depth - 1);
        strcat(buffer, ")");
        return;
    case EFUNCCALL:
        if (expr->data.funccall.funcref != NULL && expr->data.funccall.funcref->type == EOBJREF)
            describe_expression(buffer, size, expr->data.funccall.funcref, 1);
        else
            strcat(buffer, "call");
        strcat(buffer, "(");
        if (expr->data.funccall.args != NULL) {
            describe_expression(buffer, size, expr->data.funccall.args->node, depth - 1);
            if (expr->data.funccall.args->next != NULL)
                strcat(buffer, ",..");
        }
        strcat(buffer, ")");
        return;
    case EINTCONST:
        sprintf(piece, "%ld", (long)expr->data.intval.lo);
        strcat(buffer, piece);
        return;
    case EMUL:
        strcat(buffer, "(");
        describe_expression(buffer, size, expr->data.diadic.left, depth - 1);
        strcat(buffer, "*");
        describe_expression(buffer, size, expr->data.diadic.right, depth - 1);
        strcat(buffer, ")");
        return;
    default:
        sprintf(piece, "#%d", expr->type);
        strcat(buffer, piece);
        return;
    }
}

#endif /* !MWCC_ORACLE_RUST_HOOKS: expression spelling lives in Rust. */
extern ENode *setup_inline_locals_and_arguments__retail(Object *function, CInlineInfo *info, ENodeList *arguments);
extern struct InlineSlot *data_0058245a;
#ifndef MWCC_ORACLE_RUST_HOOKS
ENode *setup_inline_locals_and_arguments(Object *function, CInlineInfo *info, ENodeList *arguments)
{
    ENode *result = setup_inline_locals_and_arguments__retail(function, info, arguments);
    struct InlineSlot *slot;
    CInlineVar *formal;
    const char *mode;
    Object *object;
    FuncArg *declared;
    ENode *argument;
    char expression[256];
    int i;

    if (!wanted("MWCC_ORACLE_LOCALS"))
        return result;
    for (i = 0; i < info->nargs; i++) {
        slot = &data_0058245a[i];
        formal = &info->arginfo[i];
        if (!formal->used)
            mode = "unused";
        else if (slot->var != NULL)
            mode = "local";
        else if (slot->arg != NULL)
            mode = "reference";
        else
            mode = "substituted";
        declared = IS_TYPE_FUNC(function->type) ? TYPE_FUNC(function->type)->args : NULL;
        while (declared != NULL && declared->name != formal->name)
            declared = declared->next;
        /* the object the argument is or points at (a local formal: the object whose address it holds), through the
           right side of commas (a temporary constructed, then its address) */
        argument = slot->expr;
        if (argument == NULL) { /* a local formal: the call's own argument */
            ENodeList *list = arguments;
            int n;
            for (n = 0; list != NULL && n < i; n++)
                list = list->next;
            argument = list != NULL ? list->node : NULL;
        }
        expression[0] = 0;
        describe_expression(expression, sizeof(expression) - 8, argument, 4);
        for (;;) {
            if (argument != NULL && argument->type == ECOMMA)
                argument = argument->data.diadic.right;
            else if (argument != NULL && (argument->type == EFORCELOAD || argument->type == ETYPCON))
                argument = argument->data.monadic;
            else if (argument != NULL && argument->type == EFUNCCALL && argument->data.funccall.args != NULL &&
                     argument->data.funccall.args->node->type == EOBJREF)
                argument = argument->data.funccall.args->node; /* a temporary a call returns into */
            else
                break;
        }
        object = NULL;
        if (argument != NULL && argument->type == EOBJREF)
            object = argument->data.objref;
        else if (argument != NULL && argument->type == EINDIRECT && argument->data.monadic->type == EOBJREF)
            object = argument->data.monadic->data.objref; /* a pointer local's value: what it points at */
        OracleTrace_Printf("lc.arg index=%d name=%s type=%d size=%ld ref=%d dirty=%d mode=%s var=%s object=%s "
                           "declared=%d declared_size=%ld generated=%d argument=%d expression=%s link=%s\n", i,
                           formal->name ? formal->name->name : "?", formal->type ? formal->type->type : -1,
                           formal->type ? formal->type->size : -1,
                           formal->type && formal->type->type == TYPEPOINTER &&
                               (TYPE_POINTER(formal->type)->qual & Q_REFERENCE) != 0,
                           formal->dirty, mode,
                           slot->var && slot->var->name ? slot->var->name->name : "-",
                           object && object->name ? object->name->name : "-",
                           declared && declared->type ? declared->type->type : -1,
                           declared && declared->type ? declared->type->size : -1,
                           IS_TYPE_FUNC(function->type) && (TYPE_FUNC(function->type)->flags & FUNC_AUTO_GENERATED),
                           argument != NULL ? argument->type : -1, expression[0] ? expression : "-",
                           maybe_link(function));
    }
    return result;
}

#endif /* !MWCC_ORACLE_RUST_HOOKS: inline argument bindings live in Rust. */

/* bk.*: how code generation cuts the function into blocks and how they are merged (MWCC_ORACLE_BLOCKS=1).
 * bk.stmt per expression statement (fn_00436390): its source line and the block and instruction count before and
 * after it is generated; a statement whose block before is not the last one's after was cut from it (a block is cut
 * once it holds more than 100 instructions, at the start of a statement). bk.list (raw: before COptimizer_Optimize;
 * optimized: after it; merged: after Peephole_MergeAdjacentBlocks) lists the blocks, each bk.block with its count,
 * and with MWCC_ORACLE_BLOCKS=2 its instructions (bk.i, as ra.i). bk.pair, before each merge pass, says for each block
 * and the next why the pass leaves them apart, as the pass starts (it then merges a run of blocks into the first
 * while the sum stays at 100 or below). */
extern struct Statement *gCurrentStatement;
extern struct PCodeBlock *gCurrentBlock;
#ifndef MWCC_ORACLE_RUST_HOOKS
extern void fn_00436390__retail(ENode *expression);
extern void COptimizer_Optimize__retail(Object *function);
extern void Peephole_MergeAdjacentBlocks__retail(Object *object, Boolean mergePrologue);

static void block_list(const char *phase, Object *function)
{
    PCodeBlock *block;
    PCodeInstruction *instruction;
    PCodeBlockLink *link;
    int successors, predecessors;
    const char *level = getenv("MWCC_ORACLE_BLOCKS");

    OracleTrace_Printf("bk.list phase=%s link=%s\n", phase, maybe_link(function));
    for (block = gPCodeBlocks; block != NULL; block = block->next) {
        for (successors = 0, link = block->successors; link != NULL; link = link->next)
            successors++;
        for (predecessors = 0, link = block->predecessors; link != NULL; link = link->next)
            predecessors++;
        OracleTrace_Printf("bk.block p=%08x index=%d count=%d flags=%04x succ=%d pred=%d\n",
                           (unsigned int)block, block->index, (int)block->instruction_count,
                           (unsigned int)block->flags, successors, predecessors);
        if (level != NULL && level[0] == '2')
            for (instruction = block->instructions; instruction != NULL; instruction = instruction->next)
                print_instruction("bk.i", instruction);
    }
}

void fn_00436390(ENode *expression)
{
    PCodeBlock *block = gCurrentBlock;
    int count = block != NULL ? block->instruction_count : -1;

    fn_00436390__retail(expression);
    if (wanted("MWCC_ORACLE_BLOCKS"))
        OracleTrace_Printf("bk.stmt line=%ld before=%08x:%d after=%08x:%d\n",
                           gCurrentStatement != NULL ? (long)gCurrentStatement->sourceoffset : -1L,
                           (unsigned int)block, count, (unsigned int)gCurrentBlock,
                           gCurrentBlock != NULL ? (int)gCurrentBlock->instruction_count : -1);
}

void COptimizer_Optimize(Object *function)
{
    if (wanted("MWCC_ORACLE_BLOCKS"))
        block_list("raw", function);
    /* (MWCC_ORACLE_STAGES: the PCode code generation made, and what the global optimizer left of it; the peephole
     * forward pass then gives the allocator's, ra.before) */
    if (wanted("MWCC_ORACLE_STAGES"))
        snapshot_as("st", "codegen", function);
    COptimizer_Optimize__retail(function);
    if (wanted("MWCC_ORACLE_STAGES"))
        snapshot_as("st", "optimized", function);
    if (wanted("MWCC_ORACLE_BLOCKS"))
        block_list("optimized", function);
}

void Peephole_MergeAdjacentBlocks(Object *object, Boolean mergePrologue)
{
    PCodeBlock *block;
    PCodeBlock *next;
    PCodeInstruction *instruction;
    const char *why;

    if (!wanted("MWCC_ORACLE_BLOCKS")) {
        Peephole_MergeAdjacentBlocks__retail(object, mergePrologue);
        return;
    }
    OracleTrace_Printf("bk.merge prologue=%d link=%s\n", (int)mergePrologue, maybe_link(object));
    for (block = gPCodeBlocks; block != NULL && block->next != NULL; block = block->next) {
        next = block->next;
        why = NULL;
        if (!mergePrologue && (block->flags & 1))
            why = "prologue";
        else if (block->instruction_count <= 0)
            why = "empty";
        for (instruction = block->instructions; why == NULL && instruction != NULL; instruction = instruction->next)
            if (instruction->flags & BY_VERSION(0x420, 0x108))
                why = "call";
        if (why == NULL && block->reverse_instructions != NULL &&
            (block->reverse_instructions->flags & fIsBranch) != 0 && block->reverse_instructions->opcode != PC_B)
            why = "branch";
        if (why == NULL && (block->successors == NULL || block->successors->payload.block != next ||
                            block->successors->next != NULL))
            why = "successors";
        if (why == NULL && (next->predecessors == NULL || next->predecessors->payload.block != block ||
                            next->predecessors->next != NULL))
            why = "predecessors";
        if (why == NULL && !mergePrologue && (next->flags & 2))
            why = "epilogue";
        if (why == NULL && (next->flags & BY_VERSION(0x20, 0x8000)))
            why = "flag";
        if (why == NULL && block->instruction_count + next->instruction_count > 100)
            why = "over100";
        OracleTrace_Printf("bk.pair a=%08x b=%08x counts=%d+%d why=%s\n", (unsigned int)block, (unsigned int)next,
                           (int)block->instruction_count, (int)next->instruction_count, why != NULL ? why : "-");
    }
    Peephole_MergeAdjacentBlocks__retail(object, mergePrologue);
    block_list("merged", object);
}
#endif /* !MWCC_ORACLE_RUST_HOOKS: block/stage observers live in Rust. */

/* ce.*: the global optimizer's common subexpressions, with MWCC_ORACLE_CSE=1 (2: also the linear IR as each pass
 * starts). IRO_CommonSubs first marks each expression that repeats an available one, then walks the IR in order: at
 * each repeat, the expression it repeats gets its temporary (move_common_sub) if it has none yet, so the temporaries
 * are made in the order of their first repeats. ce.pass starts a pass (the function's n-th) and ce.l lists the IR
 * (index, kind, ENode type, operands' indices, line); ce.move is a temporary about to be made, for the expression at
 * expr= (its tree: `(TYPE#index operands)`, operands by name), ce.temp the temporary made, by path (replace: assigned
 * where the expression stands; comma: assigned where all its uses meet), and ce.replace each repeat replaced (from the
 * optimizer's own dump message, which IroDump_Print prints only when the dump is on). ce.object is each temporary
 * object made (create_temp_object), with the caller's return address, and ce.vreg each local's register (a virtual
 * register number, in the class rclass) as the allocation starts. */
#include "compiler/IroCSE.h"

#ifndef MWCC_ORACLE_RUST_HOOKS
extern struct IROLinear *linear_head;
extern void IRO_CommonSubs__retail(void);
extern void move_common_sub__retail(IROExpr *group);
static Object *cse_function;
static int cse_pass;
static int cse_moved;

static void describe_linear(char *buffer, size_t size, IROLinear *linear, int depth)
{
    char piece[32];
    size_t used = strlen(buffer);

    if (used + 12 >= size)
        return;
    if (linear == NULL) {
        strcat(buffer, "?");
        return;
    }
    if (depth <= 0) {
        strcat(buffer, "..");
        return;
    }
    switch (linear->type) {
    case IROLinearOperand:
        describe_expression(buffer, size, linear->u.node, 3);
        return;
    case IROLinearOp1Arg:
        sprintf(piece, "(%d#%d ", (int)linear->nodetype, (int)linear->index);
        strcat(buffer, piece);
        describe_linear(buffer, size, linear->u.monadic, depth - 1);
        strcat(buffer, ")");
        return;
    case IROLinearOp2Arg:
        sprintf(piece, "(%d#%d ", (int)linear->nodetype, (int)linear->index);
        strcat(buffer, piece);
        describe_linear(buffer, size, linear->u.diadic.left, depth - 1);
        strcat(buffer, " ");
        describe_linear(buffer, size, linear->u.diadic.right, depth - 1);
        strcat(buffer, ")");
        return;
    default:
        sprintf(piece, "[kind%d#%d]", (int)linear->type, (int)linear->index);
        strcat(buffer, piece);
    }
}

static long linear_line(IROLinear *linear)
{
    return linear != NULL && linear->stmt != NULL ? (long)linear->stmt->sourceoffset : -1L;
}

static void linear_list(void)
{
    IROLinear *linear;
    char text[160];

    for (linear = linear_head; linear != NULL; linear = linear->next) {
        text[0] = 0;
        if (linear->type == IROLinearOperand)
            describe_expression(text, sizeof(text), linear->u.node, 3);
        OracleTrace_Printf("ce.l i=%d kind=%d op=%d a=%d b=%d line=%ld expr=%d text=%s\n", (int)linear->index,
                           (int)linear->type, (int)linear->nodetype,
                           (linear->type == IROLinearOp1Arg || linear->type == IROLinearOp2Arg) &&
                                   linear->u.monadic != NULL
                               ? (int)linear->u.monadic->index
                               : -1,
                           linear->type == IROLinearOp2Arg && linear->u.diadic.right != NULL
                               ? (int)linear->u.diadic.right->index
                               : -1,
                           linear_line(linear), linear->expr != NULL ? (int)linear->expr->index : -1, text);
    }
}

void IRO_CommonSubs(void)
{
    const char *level = getenv("MWCC_ORACLE_CSE");

    if (!wanted("MWCC_ORACLE_CSE")) {
        IRO_CommonSubs__retail();
        return;
    }
    if (cse_function != data_005875b8) {
        cse_function = data_005875b8;
        cse_pass = 0;
    }
    cse_pass++;
    OracleTrace_Printf("ce.pass n=%d link=%s\n", cse_pass, maybe_link(data_005875b8));
    if (level[0] == '2')
        linear_list();
    IRO_CommonSubs__retail();
    OracleTrace_Printf("ce.end\n");
}

void move_common_sub(IROExpr *group)
{
    char tree[512];

    if (!wanted("MWCC_ORACLE_CSE")) {
        move_common_sub__retail(group);
        return;
    }
    tree[0] = 0;
    describe_linear(tree, sizeof(tree), group->linear, 8);
    OracleTrace_Printf("ce.move expr=%d line=%ld tree=%s\n", group->linear != NULL ? (int)group->linear->index : -1,
                       linear_line(group->linear), tree);
    cse_moved = 0;
    move_common_sub__retail(group);
    OracleTrace_Printf("ce.temp name=%s path=%s\n",
                       group->temp != NULL && group->temp->name != NULL ? group->temp->name->name : "-",
                       group->temp == NULL ? "none" : cse_moved ? "comma" : "replace");
}

/* A propagation or dead-assignment message's node, by index: "pp.KIND at=N line=L tree=T". */
static void propagation_note(const char *kind, int index)
{
    IROLinear *linear;
    char tree[512];

    for (linear = linear_head; linear != NULL; linear = linear->next)
        if (linear->index == index)
            break;
    tree[0] = 0;
    if (linear != NULL)
        describe_linear(tree, sizeof(tree), linear, 6);
    OracleTrace_Printf("pp.%s at=%d line=%ld tree=%s\n", kind, index, linear != NULL ? linear_line(linear) : 0L,
                       tree);
}

/* (the original's, which prints only when the dump is on; the CSE and propagation messages are read here first) */
void IroDump_Print(const char *message, ...)
{
    va_list arguments;
    char *base = (char *)&message;

    arguments = base + ((va_list)(&message + 1) - base + 3) / 4 * 4;
    if (strncmp(message, "Moving common sub", 17) == 0)
        cse_moved = 1;
    if (strncmp(message, "Replacing common sub at", 23) == 0 && wanted("MWCC_ORACLE_CSE"))
        OracleTrace_Printf("ce.replace at=%d with=%d\n", ((int *)arguments)[0], ((int *)arguments)[1]);
    if (wanted("MWCC_ORACLE_CSE")) {
        if (strncmp(message, "Found propagatable assignment", 29) == 0)
            propagation_note("copy", ((int *)arguments)[0]);
        else if (strncmp(message, "Found propagatable expression", 29) == 0)
            propagation_note("expression", ((int *)arguments)[0]);
        else if (strncmp(message, "Removing dead assignment", 24) == 0)
            propagation_note("dead", ((int *)arguments)[0]);
    }
    if (INT_005882b8 != 0)
        vfprintf(iro_dump_output, message, arguments);
}

static void vreg_objects(ObjectList *list)
{
    Object *object;
    VarInfo *info;

    for (; list != NULL; list = list->next) {
        object = list->object;
        if (object == NULL || object->datatype != DLOCAL || (info = object->u.var.info) == NULL)
            continue;
        OracleTrace_Printf("ce.vreg name=%s reg=%d class=%d noregister=%d\n",
                           object->name != NULL ? object->name->name : "?", (int)info->reg, (int)info->rclass,
                           (int)info->noregister);
    }
}

void cse_vregs(void)
{
    if (!wanted("MWCC_ORACLE_CSE"))
        return;
    vreg_objects(locals);
    vreg_objects(temporary_objects);
}
#endif /* !MWCC_ORACLE_RUST_HOOKS: CSE/propagation observers live in Rust. */
#endif

#endif
