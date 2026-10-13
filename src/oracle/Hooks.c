/*
 * The oracle build's wrappers of the original's functions (config.json "oracle": "hooks").
 *
 * tools/link.py renames a hooked function's definition in the original's split object to NAME__retail, so that what
 * calls NAME reaches the wrapper defined here, and the wrapper traces what the function is given and what it leaves, and
 * calls the original's code. The function does what it did, so what the oracle executable compiles is what the original
 * does; the trace says what happened inside, from outside: the PCode before and after, whose operands are the
 * register assignment (tools/oracle.py reads the allocation from the pair).
 */
#include <string.h>
#include "compiler/common.h"
#include "compiler/CInline.h"
#include "compiler/CPrep.h"
#include "compiler/CodeGen.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/types.h"
#include "compiler/PCode.h"
#include "compiler/InterferenceGraph.h"
#include "compiler/Registers.h"
#include "compiler/OracleTrace.h"

#ifdef MWCC_ORACLE_TRACE

/* The link (mangled) name of a function where the compiler has made it already (it has, for the function being
 * generated), else its name. */
static const char *link_name(Object *object)
{
    while (object != NULL && object->datatype == DALIAS)
        object = object->u.alias.object;
    if (object == NULL)
        return "?";
    if ((object->datatype == DFUNC || object->datatype == DVFUNC) && object->u.func.linkname != NULL)
        return object->u.func.linkname->name;
    return object->name != NULL ? object->name->name : "?";
}

/* The PCode of the function being generated: a block per line (its address, number, weight and the blocks it leads to),
 * an instruction per line (its address, which is what identifies it before and after, opcode, flags, the source line
 * it was generated for, operands as the kind, arg and ten bytes of value each). */
static void snapshot(const char *phase, Object *function)
{
    PCodeBlock *block;
    PCodeInstruction *instruction;
    PCodeBlockLink *link;
    int operand;
    int byte;

    OracleTrace_Printf("ra.%s function=%s link=%s\n", phase, function->name->name, link_name(function));
    for (block = gPCodeBlocks; block != NULL; block = block->next) {
        OracleTrace_Printf("ra.block p=%08x index=%d weight=%d flags=%04x succ=", (unsigned int)block, block->index,
                           block->execution_weight, (unsigned int)block->flags);
        for (link = block->successors; link != NULL; link = link->next) {
            OracleTrace_Printf("%08x,", (unsigned int)link->payload.block);
        }
        OracleTrace_Printf("\n");
        for (instruction = block->instructions; instruction != NULL; instruction = instruction->next) {
            /* (f18[1] is the source line of the statement it was generated for) */
            OracleTrace_Printf("ra.i p=%08x op=%d flags=%08x n=%d line=%d ops=", (unsigned int)instruction,
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
    }
    OracleTrace_Printf("ra.end\n");
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

void fn_004f7fb0(void)
{
    fn_004f7fb0__retail();
    OracleTrace_Printf("fr.begin\n");
    frame_objects("local", locals);
    frame_objects("temp", temporary_objects);
    OracleTrace_Printf("fr.end\n");
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

/* inl.can: the decision for a call of FUNCTION at the inline depth DEPTH, with the options and the callee's statement
 * count that the decision looks at; tools/oracle.py gives the reason. */
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
                       "nstmts=%d\n", callee_name(function), (int)result, (int)data_00582468,
                       (int)((function->qual & Q_INLINE) != 0), (int)ALWAYS_INLINE, (int)INLINE_AUTO,
                       (int)INLINE_LIMIT, info != NULL, info != NULL ? (int)info->nstmts : -1);
    return result;
}

/* inl.cost: the automatic inliner's estimate of a body against the threshold (data_005dfb68). */
int fn_0055e540(CInlineInfo *info, int running, int depth)
{
    int result = fn_0055e540__retail(info, running, depth);

    OracleTrace_Printf("inl.cost nstmts=%d running=%d depth=%d cost=%d threshold=%d\n",
                       info != NULL ? (int)info->nstmts : -1, running, depth, result, (int)data_005dfb68);
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

    OracleTrace_Printf("inl.begin function=%s depth=%d body=%d kind=%d\n", callee_name(function), (int)data_00582468,
                       info != NULL, info != NULL ? (int)INFO_KIND(info) : -1);
    result = INLINE_CALL_RETAIL(expr);
    OracleTrace_Printf("inl.end function=%s expanded=%d\n", callee_name(function), result != expr);
    return result;
}
#endif

#endif
