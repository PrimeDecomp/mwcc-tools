/*
 * The oracle build's wrappers of the original's functions (config.json "oracle": "hooks").
 *
 * tools/link.py renames a hooked function's definition in the original's split object to NAME__retail, so that what
 * calls NAME reaches the wrapper defined here, and the wrapper traces what the function is given and what it leaves, and
 * calls the original's code. The function does what it did, so what the oracle executable compiles is what the original
 * does; the trace says what happened inside, from outside: the PCode before and after, whose operands are the
 * register assignment (tools/oracle.py reads the allocation from the pair).
 */
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
#include "compiler/OracleTrace.h"

#ifdef MWCC_ORACLE_TRACE

/* The PCode of the function being generated: a block per line (its address, number, weight and the blocks it leads to),
 * an instruction per line (its address, which is what identifies it before and after, opcode, flags, operands as the
 * kind, arg and ten bytes of value each). */
static void snapshot(const char *phase, Object *function)
{
    PCodeBlock *block;
    PCodeInstruction *instruction;
    PCodeBlockLink *link;
    int operand;
    int byte;

    OracleTrace_Printf("ra.%s function=%s\n", phase, function->name->name);
    for (block = gPCodeBlocks; block != NULL; block = block->next) {
        OracleTrace_Printf("ra.block p=%08x index=%d weight=%d flags=%04x succ=", (unsigned int)block, block->index,
                           block->execution_weight, (unsigned int)block->flags);
        for (link = block->successors; link != NULL; link = link->next) {
            OracleTrace_Printf("%08x,", (unsigned int)link->payload.block);
        }
        OracleTrace_Printf("\n");
        for (instruction = block->instructions; instruction != NULL; instruction = instruction->next) {
            OracleTrace_Printf("ra.i p=%08x op=%d flags=%08x n=%d ops=", (unsigned int)instruction,
                               (int)instruction->opcode, instruction->flags, (int)instruction->operand_count);
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

static const char *callee_name(Object *function)
{
    if (function != NULL && function->datatype == DALIAS)
        function = function->u.alias.object;
    return function != NULL && function->name != NULL ? function->name->name : "?";
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
