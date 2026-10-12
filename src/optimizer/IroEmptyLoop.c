#include "compiler/common.h"
#include "compiler/CPrep.h"
#include "compiler/CInt64.h"
#include "compiler/CError.h"
#include "compiler/CompilerTools.h"
#include "compiler/CodeGen.h"
#include "compiler/IroDump.h"
#include "compiler/IroJump.h"
#include "compiler/IroRangePropagation.h"
#include "compiler/IroLoop.h"
#include "compiler/IroBitVect.h"
#include "compiler/IrOptimizer.h"
#include "compiler/IroVars.h"
#include "compiler/IroUtil.h"
#include "compiler/CParser.h"

#if VERSION >= VERSION_GC_1_3
#define FLD(p, off, T) (*(T *)((char *)(p) + (off)))
#define IEL_FILE "IroEmptyLoop.c"

extern void fn_005297b0(void *p);
extern int fn_004e6610(void *type);
extern Object *data_005875b8;

struct IelSpan {
    char *first;
    char *last;
    struct IelSpan *next;
};

struct IelResult {
    struct IelResult *next;
    Object *object;
};

extern void fn_0047c270(Object *proc, void *info, struct IelSpan **out);
extern void fn_0047a6e0(Object *obj, void *call, struct IelResult **out);

static inline Boolean Iel_IsCallNode(char *l)
{
    return l[0] == 1 && FLD(l, 0x20, UInt8 *)[0] == 0x38;
}

static inline void Iel_MarkAllUnknown(void)
{
    ERangeVar *rv;
    for (rv = range_vars; rv != NULL; rv = rv->next)
        *(UInt8 *)rv->range = 3;
}

/* mark the ranges of everything a call may modify as unknown */
void fn_0049dcf0(void *call)
{
    Boolean unknown = 0;
    void *info = FLD(call, 0x28, void *);
    struct IelSpan *spans;
    struct IelSpan *s;

    if (info != NULL && copts.ff1 && FLD(info, 0x1a, void *) != NULL && data_005875b8 != NULL) {
        spans = NULL;
        fn_0047c270(data_005875b8, info, &spans);
        if (spans != NULL) {
            for (s = spans; s != NULL; s = s->next) {
                char *l;
                Boolean found;
                if (s->first == NULL || s->last == NULL) {
                    unknown = 1;
                    break;
                }
                found = 0;
                for (l = s->first; l != FLD(s->last, 0x34, char *); l = FLD(l, 0x34, char *)) {
                    if (Iel_IsCallNode(l)) {
                        Object *obj;
                        struct IelResult *res;
                        struct IelResult *r;
                        found = 1;
                        obj = FLD(FLD(l, 0x20, void *), 0xe, Object *);
                        if (obj == NULL)
                            CError_Internal("IroRangePropagation.c", 0x181);
                        res = NULL;
                        fn_0047a6e0(obj, call, &res);
                        for (r = res; r != NULL; r = r->next) {
                            if (r->object == NULL) {
                                unknown = 1;
                                break;
                            }
                        }
                        while (res != NULL) {
                            struct IelResult *n = res->next;
                            fn_005297b0(res);
                            res = n;
                        }
                        if (unknown)
                            break;
                    }
                }
                if (!found)
                    unknown = 1;
                if (unknown)
                    break;
            }
            if (!unknown) {
                for (s = spans; s != NULL; s = s->next) {
                    char *l;
                    for (l = s->first; l != FLD(s->last, 0x34, char *); l = FLD(l, 0x34, char *)) {
                        if (Iel_IsCallNode(l)) {
                            Object *obj = FLD(FLD(l, 0x20, void *), 0xe, Object *);
                            struct IelResult *res = NULL;
                            struct IelResult *r;
                            fn_0047a6e0(obj, call, &res);
                            for (r = res; r != NULL; r = r->next) {
                                ERangeVar *rv;
                                for (rv = range_vars; rv != NULL && rv->object != r->object; rv = rv->next)
                                    ;
                                if (rv != NULL)
                                    *(UInt8 *)rv->range = 3;
                            }
                            while (res != NULL) {
                                struct IelResult *n = res->next;
                                fn_005297b0(res);
                                res = n;
                            }
                        }
                    }
                }
            }
            while (spans != NULL) {
                struct IelSpan *n = spans->next;
                fn_005297b0(spans);
                spans = n;
            }
        } else {
            unknown = 1;
        }
    } else {
        unknown = 1;
    }
    if (unknown)
        Iel_MarkAllUnknown();
}

/* record the range of a call's modified objects on linear nd */
void fn_0049df20(char *nd)
{
    Boolean unknown = 0;
    char *call;
    void *info;
    struct IelSpan *spans;
    struct IelSpan *s;

    call = FLD(nd, 0x20, char *);
    if (call != NULL && call[0] == 2 && call[1] == 4 && (info = FLD(call, 0x20, void *)) != NULL &&
        copts.ff1 && FLD(info, 0x1a, void *) != NULL && data_005875b8 != NULL) {
        spans = NULL;
        fn_0047c270(data_005875b8, info, &spans);
        if (spans != NULL) {
            for (s = spans; s != NULL; s = s->next) {
                char *l;
                Boolean found;
                if (s->first == NULL || s->last == NULL) {
                    unknown = 1;
                    break;
                }
                found = 0;
                for (l = s->first; l != FLD(s->last, 0x34, char *); l = FLD(l, 0x34, char *)) {
                    if (Iel_IsCallNode(l)) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    unknown = 1;
                    break;
                }
            }
            if (!unknown) {
                for (s = spans; s != NULL; s = s->next) {
                    char *l;
                    for (l = s->first; l != FLD(s->last, 0x34, char *); l = FLD(l, 0x34, char *)) {
                        if (Iel_IsCallNode(l)) {
                            Object *obj = FLD(FLD(l, 0x20, void *), 0xe, Object *);
                            ERange *val;
                            ERangeVar *rv;
                            if (obj == NULL)
                                CError_Internal("IroRangePropagation.c", 0x12e);
                            val = FLD(nd, 0x16, ERange *);
                            if (nd[1] == 0 || nd[1] == 1)
                                val = FLD(call, 0x16, ERange *);
                            for (rv = range_vars; rv != NULL && rv->object != obj; rv = rv->next)
                                ;
                            if (rv == NULL) {
                                rv = (ERangeVar *)oalloc(sizeof(ERangeVar));
                                rv->object = obj;
                                rv->range = val;
                                rv->next = range_vars;
                                range_vars = rv;
                                if (first_range_var == NULL)
                                    first_range_var = rv;
                            } else {
                                rv->range = val;
                            }
                        }
                    }
                }
            }
            while (spans != NULL) {
                struct IelSpan *n = spans->next;
                fn_005297b0(spans);
                spans = n;
            }
        } else {
            unknown = 1;
        }
    } else {
        unknown = 1;
    }
    if (unknown) {
        ERange *r;
        Iel_MarkAllUnknown();
        r = (ERange *)oalloc(0x12);
        r->type = 3;
        FLD(nd, 0x16, ERange *) = r;
    }
}

/* clamp a range to what an integral type can hold */
void fn_0049e140(ERange *range, void *type)
{
    CInt64 size;
    CInt64 three;
    CInt64 bits;
    CInt64 hi;
    CInt64 lo;
    CInt64 minusOne;

    if (range == NULL)
        return;
    if (FLD(type, 0, UInt8) != 1) {
        range->type = 3;
        return;
    }
    size.lo = FLD(type, 2, SInt32);
    size.hi = (SInt32)size.lo < 0 ? -1 : 0;
    three.hi = 0;
    three.lo = 3;
    if (fn_004e6610(type)) {
        if (FLD(type, 2, SInt32) >= 8) {
            range->type = 3;
            return;
        }
        bits = CInt64_Shl(size, three);
        hi = CInt64_Sub(CInt64_Shl(cint64_one, bits), cint64_one);
        if (CInt64_GreaterU(range->upper, hi))
            range->type = 3;
    } else {
        if (FLD(type, 2, SInt32) >= 8) {
            range->type = 3;
            return;
        }
        minusOne.hi = -1;
        minusOne.lo = -1;
        bits = CInt64_Sub(CInt64_Shl(size, three), cint64_one);
        hi = CInt64_Sub(CInt64_Shl(cint64_one, bits), cint64_one);
        lo = CInt64_Shl(minusOne, bits);
        if (CInt64_Greater(range->upper, hi) || CInt64_Less(range->lower, lo))
            range->type = 3;
    }
}

/* trip count and final-value adjustment for a counting-down loop; returns 1 if it cannot be determined */
Boolean fn_0049e310(char *loop, CInt64 *count, CInt64 *rem)
{
    char *start = FLD(FLD(loop, 0x14, void *), 0x24, char *);
    char *limit = FLD(FLD(loop, 0x18, void *), 0x24, char *);
    Boolean uns = fn_004e6610(FLD(limit, 0xe, void *));
    CInt64 v8, v10, step, m1, e;

    if (!IroDump_IsType1NodeType50((IROLinear *)limit))
        return 1;
    if (!IroDump_IsType1NodeType50((IROLinear *)start))
        return 1;
    v8 = FLD(FLD(limit, 0x20, void *), 0xe, CInt64);
    v10 = FLD(FLD(start, 0x20, void *), 0xe, CInt64);
    if (uns) {
        if (CInt64_LessEqualU(v10, v8))
            return 1;
    } else {
        if (CInt64_LessEqual(v10, v8))
            return 1;
    }
    step.lo = FLD(FLD(loop, 0x1c, void *), 0xe, SInt32);
    step.hi = (SInt32)step.lo < 0 ? -1 : 0;
    m1.hi = -1;
    m1.lo = -1;
    *count = CInt64_Sub(v10, v8);
    *count = CInt64_Add(*count, step);
    if (FLD(loop, 0x18, UInt8 *)[0] == 3 && FLD(loop, 0x18, UInt8 *)[1] == 0x14)
        *count = CInt64_Add(*count, m1);
    if (step.hi == 0 && step.lo == 0)
        CError_Internal(IEL_FILE, 0x3e3);
    if (uns)
        *count = CInt64_DivU(*count, step);
    else
        *count = CInt64_Div(*count, step);
    if (CInt64_Equal(*count, cint64_zero))
        return 1;
    if (uns) {
        if (CInt64_LessEqualU(*count, cint64_zero))
            return 0;
    } else {
        if (CInt64_LessEqual(*count, cint64_zero))
            return 0;
    }
    if (uns) {
        *rem = CInt64_MulU(*count, step);
        *rem = CInt64_Sub(v10, *rem);
    } else {
        *rem = CInt64_Mul(*count, step);
        *rem = CInt64_Sub(v10, *rem);
    }
    step.lo = FLD(FLD(loop, 0x1c, void *), 0xe, SInt32);
    step.hi = (SInt32)step.lo < 0 ? -1 : 0;
    e = CInt64_Sub(v8, step);
    if (uns) {
        if (CInt64_GreaterU(e, v8))
            return 1;
    } else {
        if (CInt64_Greater(e, v10))
            return 1;
    }
    return 0;
}

/* trip count and final-value adjustment for a counting-up loop; returns 1 if it cannot be determined */
Boolean fn_0049e660(char *loop, CInt64 *count, CInt64 *rem)
{
    char *start = FLD(FLD(loop, 0x14, void *), 0x24, char *);
    char *limit = FLD(FLD(loop, 0x18, void *), 0x24, char *);
    Boolean uns = fn_004e6610(FLD(limit, 0xe, void *));
    CInt64 v8, v10, step, m1, e;

    if (!IroDump_IsType1NodeType50((IROLinear *)start))
        return 1;
    if (!IroDump_IsType1NodeType50((IROLinear *)limit))
        return 1;
    v8 = FLD(FLD(start, 0x20, void *), 0xe, CInt64);
    v10 = FLD(FLD(limit, 0x20, void *), 0xe, CInt64);
    if (uns) {
        if (CInt64_LessEqualU(v10, v8))
            return 1;
    } else {
        if (CInt64_LessEqual(v10, v8))
            return 1;
    }
    step.lo = FLD(FLD(loop, 0x1c, void *), 0xe, SInt32);
    step.hi = (SInt32)step.lo < 0 ? -1 : 0;
    m1.hi = -1;
    m1.lo = -1;
    *count = CInt64_Sub(v10, v8);
    *count = CInt64_Add(*count, step);
    if (FLD(loop, 0x18, UInt8 *)[0] == 3 && FLD(loop, 0x18, UInt8 *)[1] == 0x13)
        *count = CInt64_Add(*count, m1);
    if (step.hi == 0 && step.lo == 0)
        CError_Internal(IEL_FILE, 0x357);
    if (uns)
        *count = CInt64_DivU(*count, step);
    else
        *count = CInt64_Div(*count, step);
    if (CInt64_Equal(*count, cint64_zero))
        return 1;
    if (uns) {
        if (CInt64_LessEqualU(*count, cint64_zero))
            CError_Internal(IEL_FILE, 0x36d);
    } else {
        if (CInt64_LessEqual(*count, cint64_zero))
            CError_Internal(IEL_FILE, 0x376);
    }
    if (uns) {
        *rem = CInt64_MulU(*count, step);
        *rem = CInt64_Add(*rem, v8);
    } else {
        *rem = CInt64_Mul(*count, step);
        *rem = CInt64_Add(*rem, v8);
    }
    step.lo = FLD(FLD(loop, 0x1c, void *), 0xe, SInt32);
    step.hi = (SInt32)step.lo < 0 ? -1 : 0;
    e = CInt64_Add(v10, step);
    if (uns) {
        if (CInt64_LessU(e, v10))
            return 1;
    } else {
        if (CInt64_Less(e, v10))
            return 1;
    }
    return 0;
}
static inline void Iel_BitSet(BitVector *bv, unsigned int bit)
{
    if ((bit >> 5) < bv->size)
        bv->bits[bit >> 5] |= 1u << bit;
    else
        CError_Internal("BitVector.h", 0x38);
}

/* check that every statement of the loop outside the induction update is a loop-invariant store */
Boolean fn_0049e9b0(char *loop)
{
    IRONode *n;
    char *l;

    for (n = iro_flowgraph_head; n != NULL; n = n->nextnode) {
        if (!((n->index >> 5) < IRO_LoopScratchVector_005880dc->size &&
              ((1 << n->index) & IRO_LoopScratchVector_005880dc->bits[n->index >> 5])))
            continue;
        if (n == FLD(loop, 4, IRONode *))
            continue;
        l = FLD(n, 0xe, char *);
        if (l == NULL)
            continue;
        for (;;) {
            int stmt = FLD(l, 8, UInt16);
            if ((stmt < FLD(loop, 0x20, int) || stmt > FLD(loop, 0x24, int)) && l[0] != 0 && l[0] != 8) {
                if (l[0] == 3 && l[1] == 0x1e) {
                    char *lhs;
                    if (FLD(l, 2, UInt32) & 2) {
                        IroDump_Print("Found EASS node that is referenced i.e embedded assignment\n");
                        return 0;
                    }
                    lhs = FLD(l, 0x20, char *);
                    if (lhs[0] != 2 || lhs[1] != 4) {
                        IroDump_Print("Found EASS nodes whose lhs root  is not a EINDIRECT node\n");
                        return 0;
                    }
                    if (FLD(lhs, 0xe, void *) != NULL &&
                        CParser_IsVolatile(FLD(lhs, 0xe, Type *), FLD(lhs, 6, UInt16) & 3)) {
                        IroDump_Print(" EASS at %d fail as store to volatile memory \n", FLD(l, 8, UInt16));
                        return 0;
                    }
                    if ((FLD(FLD(FLD(l, 0x20, char *), 0x20, char *), 2, UInt32) & 0x100) &&
                        (FLD(FLD(l, 0x24, char *), 2, UInt32) & 0x100)) {
                        IroDump_Print(" EASS at %d pass\n", FLD(l, 8, UInt16));
                    } else {
                        IroDump_Print(" EASS at %d fail,  either LHS address or RHS is variant \n", FLD(l, 8, UInt16));
                        return 0;
                    }
                } else if (!(FLD(l, 2, UInt32) & 2)) {
                    IroDump_Print("Found non EASS top level node in the loop\n");
                    return 0;
                }
            }
            if (l == FLD(n, 0x12, char *))
                break;
            l = FLD(l, 0x34, char *);
        }
    }
    return 1;
}

/* decide whether a loop of the simple counted form can be removed */
Boolean fn_0049eb40(char *loop)
{
    UInt32 flags = FLD(loop, 0, UInt32);
    char *ind;

    if (!(flags & 0x10000) || !(flags & 0x20000))
        return 0;
    if (flags & 0x8000) {
        IroDump_Print("CanRemoveRedundantLoop1:No due to LP_LOOP_HAS_ASM \n");
        return 0;
    }
    if (flags & 0x20) {
        IroDump_Print("CanRemoveRedundantLoop1:No due to LP_IFEXPR_NON_CANONICAL \n");
        return 0;
    }
    if (flags & 2) {
        IroDump_Print("CanRemoveRedundantLoop1:No due to LP_LOOP_HAS_CALLS \n");
        return 0;
    }
    if (flags & 4) {
        IroDump_Print("CanRemoveRedundantLoop1:No due to LP_LOOP_HAS_CNTRLFLOW \n");
        return 0;
    }
    if (flags & 0x10) {
        IroDump_Print("CanRemoveRedundantLoop1:No due to LP_INDUCTION_NOT_FOUND \n");
        return 0;
    }
    if (flags & 0x40) {
        IroDump_Print("CanRemoveRedundantLoop1:No due to LP_HAS_MULTIPLE_INDUCTIONS \n");
        return 0;
    }
    if (flags & 0x80) {
        IroDump_Print("CanRemoveRedundantLoop1:No due to LP_LOOP_HDR_HAS_SIDEEFFECTS \n");
        return 0;
    }
    if (!(flags & 0x200)) {
        IroDump_Print("CanRemoveRedundantLoop1:No because header does not follow induction update \n");
        return 0;
    }
    ind = FLD(FLD(loop, 0x1c, char *), 0xa, char *);
    if (ind[0] != 2 || ind[1] != 3) {
        IroDump_Print("CanRemoveRedundantLoop1:No because induction operator not a predec \n");
        return 0;
    }
    if (!fn_004e6610(FLD(ind, 0xe, void *))) {
        IroDump_Print("CanRemoveRedundantLoop1:No because induction not of the right type \n");
        return 0;
    }
    return 1;
}

extern Boolean fn_0049eef0(IRONode *header);

void fn_0049f400(void)
{
    IRONode *header;
    IRONode *pred;
    UInt16 i;
    UInt16 found;

    for (header = iro_flowgraph_head; header != NULL; header = header->nextnode) {
        found = 0;
        for (i = 0; i < header->numpred; i++) {
            pred = iroNodesByIndex[header->pred[i]];
            if ((header->index >> 5) < pred->dom->size && ((1 << header->index) & pred->dom->bits[header->index >> 5])) {
                if (!found) {
                    IroBitVect_AllocateBitVector(&IRO_LoopScratchVector_005880dc, iro_node_count + 1);
                    IroBitVect_ClearBitVector(IRO_LoopScratchVector_005880dc);
                    Iel_BitSet(IRO_LoopScratchVector_005880dc, header->index);
                }
                found = 1;
                Iel_BitSet(IRO_LoopScratchVector_005880dc, pred->index);
                if (pred != header)
                    fn_004614f0(pred);
            }
        }
        if (found) {
            IroDump_Print("IRO_FindEmptyLoops:Found loop with header %d\n", header->index);
            IroDump_PrintBitSet("Loop includes: ", IRO_LoopScratchVector_005880dc);
            fn_0049eef0(header);
            IRO_ExpressionPropagation();
        }
    }
    IroVars_CheckTimedLongjmp();
}
/* decide whether the loop described by `loop` can be removed after computing its trip count */
Boolean fn_0049ec80(char *loop)
{
    UInt32 flags = FLD(loop, 0, UInt32);
    char *limit;
    char *init;

    if (flags & 0x10000) {
        IroDump_Print("CanRemoveRedundantLoop:No because detection of dowhile(n--) loop not supported\n");
        return 0;
    }
    if (flags & 0x8000) {
        IroDump_Print("CanRemoveRedundantLoop:No due to LP_LOOP_HAS_ASM \n");
        return 0;
    }
    if (flags & 0x20) {
        IroDump_Print("CanRemoveRedundantLoop:No due to LP_IFEXPR_NON_CANONICAL \n");
        return 0;
    }
    if (flags & 2) {
        IroDump_Print("CanRemoveRedundantLoop:No due to LP_LOOP_HAS_CALLS \n");
        return 0;
    }
    if (flags & 4) {
        IroDump_Print("CanRemoveRedundantLoop:No due to LP_LOOP_HAS_CNTRLFLOW \n");
        return 0;
    }
    if (flags & 0x10) {
        IroDump_Print("CanRemoveRedundantLoop:No due to LP_INDUCTION_NOT_FOUND \n");
        return 0;
    }
    if (flags & 0x40) {
        IroDump_Print("CanRemoveRedundantLoop:No due to LP_HAS_MULTIPLE_INDUCTIONS \n");
        return 0;
    }
    if (flags & 0x80) {
        IroDump_Print("CanRemoveRedundantLoop:No due to LP_LOOP_HDR_HAS_SIDEEFFECTS \n");
        return 0;
    }
    if (!(flags & 0x200)) {
        IroDump_Print("CanRemoveRedundantLoop:No because header does not follow induction update \n");
        return 0;
    }
    limit = FLD(FLD(loop, 0x18, char *), 0x24, char *);
    if (!IroDump_IsType1NodeType50((IROLinear *)limit) && !(FLD(limit, 2, UInt32) & 0x100)) {
        IroDump_Print("CanRemoveRedundantLoop:No because Loop Upper Bound is Variant in the loop\n");
        return 0;
    }
    init = FLD(loop, 0x14, char *);
    if (init == NULL) {
        IroDump_Print("CanRemoveRedundantLoop:No because there is no initialization of loop index in PreHeader\n");
        return 0;
    }
    if (IroDump_GetObjRef(FLD(init, 0x20, IROLinear *)) == NULL) {
        IroDump_Print("CanRemoveRedundantLoop:No because initial value of induction stored thru pointer\n");
        return 0;
    }
    if (!fn_004e6610(FLD(FLD(loop, 0x14, char *), 0xe, void *))) {
        char *initVal = FLD(FLD(loop, 0x14, char *), 0x24, char *);
        if (IroDump_IsType1NodeType50((IROLinear *)initVal)) {
            if (!CInt64_GreaterEqual(FLD(FLD(initVal, 0x20, void *), 0xe, CInt64), cint64_zero)) {
                IroDump_Print("CanRemoveRedundantLoop:No because initial value of induction is signed but < 0\n");
                return 0;
            }
        } else {
            IroDump_Print("CanRemoveRedundantLoop:No because initial value of induction is signed and not constant\n");
            return 0;
        }
    }
    flags = FLD(loop, 0, UInt32);
    if (!(flags & 0x400) && !(flags & 0x4000)) {
        IroDump_Print("CanRemoveRedundantLoop:No because LP_LOOP_STEP_ISPOS/LP_LOOP_STEP_ISNEG   is not set\n");
        return 0;
    }
    if (flags & 0x400) {
        if (fn_0049e660(loop, (CInt64 *)(loop + 0x28), (CInt64 *)(loop + 0x30))) {
            IroDump_Print("CanRemoveRedundantLoop:No because Final Value of indution will overflow\n");
            return 0;
        }
    }
    if (FLD(loop, 0, UInt32) & 0x4000) {
        if (fn_0049e310(loop, (CInt64 *)(loop + 0x28), (CInt64 *)(loop + 0x30))) {
            IroDump_Print("CanRemoveRedundantLoop:No because Final Value of indution will overflow\n");
            return 0;
        }
    }
    return fn_0049e9b0(loop) ? 1 : 0;
}

static inline Boolean Iel_InLoop(IRONode *n)
{
    return (n->index >> 5) < IRO_LoopScratchVector_005880dc->size &&
           ((1 << n->index) & IRO_LoopScratchVector_005880dc->bits[n->index >> 5]);
}

extern char *fn_004a4360(IRONode *tail);
extern void fn_004a4bc0(char *info, IRONode *node);

/* try to remove the loop headed by `header` when its trip count is a compile-time constant */
Boolean fn_0049eef0(IRONode *header)
{
    Boolean dowhile = 0;
    int tailCount = 0;
    IRONode *preheader = NULL;
    IRONode *tail = NULL;
    IRONode *lastIn = NULL;
    IRONode *p;
    IRONode *q;
    UInt16 i;
    int hasOutside = 0;
    int inLoopPreds;
    int outsidePreds;
    VarRecord *vr;
    char *info;
    UInt16 t;

    loop_header = header;
    compute_mustreach();
    for (vr = var_records; vr != NULL; vr = vr->next)
        vr->inductionState = 1;
    fn_00460f70();
    mark_nonintersecting_linears();
    find_induction_variables();
    loop_header = header;
    data_0058851c = 0;
    for (i = 0; i < loop_header->numpred; i++) {
        p = iroNodesByIndex[loop_header->pred[i]];
        if (!Iel_InLoop(p)) {
            hasOutside = 1;
            if (p->nextnode == header) {
                if (preheader != NULL && p != preheader)
                    CError_Internal(IEL_FILE, 0xad);
                preheader = p;
            }
        }
    }
    if (!hasOutside) {
        IroDump_Print("No predecessor outside the loop\n");
        return 0;
    }
    for (i = 0; i < loop_header->numsucc; i++) {
        p = iroNodesByIndex[loop_header->succ[i]];
        if (Iel_InLoop(p)) {
            tailCount++;
            tail = p;
        }
    }
    if (loop_header == tail && tailCount == 1)
        dowhile = 1;
    t = FLD(loop_header->last, 0, UInt8);
    if (t == 5 && t == 6 && !dowhile)
        return 0;
    inLoopPreds = 0;
    for (i = 0; i < header->numpred; i++) {
        p = iroNodesByIndex[loop_header->pred[i]];
        if (Iel_InLoop(p)) {
            inLoopPreds++;
            lastIn = p;
        }
    }
    outsidePreds = 0;
    q = NULL;
    for (i = 0; i < header->numpred; i++) {
        p = iroNodesByIndex[loop_header->pred[i]];
        if (!Iel_InLoop(p)) {
            outsidePreds++;
            q = p;
        }
    }
    if (inLoopPreds != 1 || outsidePreds != 1) {
        IroDump_Print("Cannot handle Do While Loop with multiple tails\n");
        return 0;
    }
    if (FLD(lastIn->last, 0, UInt8) != 5) {
        IroDump_Print("NonStandard while loop layout\n");
        return 0;
    }
    if (!(Iel_InLoop(loop_header->nextnode) && !Iel_InLoop(lastIn->nextnode)) && !dowhile) {
        IroDump_Print("NonStandard while loop layout\n");
        return 0;
    }
    IroDump_Print("Standard while loop layout\n");
    info = fn_004a4360(lastIn);
    if (dowhile)
        FLD(info, 0, UInt32) |= 0x20000;
    fn_004a4bc0(info, q);
    while (q != NULL && FLD(info, 0x14, void *) == NULL && q->numpred == 1 && iroNodesByIndex[q->pred[0]]->numsucc == 1) {
        fn_004a4bc0(info, iroNodesByIndex[q->pred[0]]);
        q = iroNodesByIndex[q->pred[0]];
    }
    if (fn_0049ec80(info)) {
        char *upd;
        char *lin;
        char *en;
        void *ty;
        IroDump_Print("EmptyLoop: # of iterations =%ld, FinalStoreVal=%ld\n", FLD(info, 0x2c, SInt32), FLD(info, 0x34, SInt32));
        IroUtil_ClearZeroOperands(FLD(lastIn->last, 0x24, IROLinear *));
        FLD(lastIn->last, 0, UInt8) = 0;
        upd = FLD(FLD(info, 0x1c, char *), 0xa, char *);
        ty = FLD(upd, 0xe, void *);
        lin = (char *)IrOptimizer_NewLinear(1);
        linear_index_counter++;
        FLD(lin, 8, UInt16) = linear_index_counter;
        en = (char *)IrOptimizer_NewENode(0x32);
        FLD(en, 6, void *) = ty;
        FLD(en, 0xe, SInt32) = FLD(info, 0x30, SInt32);
        FLD(en, 0x12, SInt32) = FLD(info, 0x34, SInt32);
        FLD(lin, 0x20, char *) = en;
        FLD(lin, 0xe, void *) = ty;
        upd = FLD(FLD(info, 0x1c, char *), 0xa, char *);
        if (upd[0] == 2) {
            void *operand = FLD(upd, 0x20, void *);
            upd[0] = 3;
            FLD(FLD(info, 0x1c, char *), 0xa, char *)[1] = 0x1e;
            FLD(FLD(FLD(info, 0x1c, char *), 0xa, char *), 0x20, void *) = operand;
            FLD(FLD(FLD(info, 0x1c, char *), 0xa, char *), 0x24, char *) = lin;
            IroUtil_InsertLinearBefore((IROLinear *)lin, (IROLinear *)lin, (IROLinear *)FLD(FLD(info, 0x1c, char *), 0xa, char *));
        } else if (upd[0] == 3) {
            upd[1] = 0x1e;
            IroUtil_ClearZeroOperands(FLD(FLD(FLD(info, 0x1c, char *), 0xa, char *), 0x24, IROLinear *));
            FLD(FLD(FLD(info, 0x1c, char *), 0xa, char *), 0x24, char *) = lin;
            IroUtil_InsertLinearBefore((IROLinear *)lin, (IROLinear *)lin, (IROLinear *)FLD(FLD(info, 0x1c, char *), 0xa, char *));
        }
    } else if (fn_0049eb40(info)) {
        char *upd;
        char *lin;
        char *en;
        void *ty;
        void *operand;
        IroDump_Print("EmptyLoop: self recursive dowhile(--n ) loop\n");
        FLD(lastIn->last, 0, UInt8) = 0;
        ty = FLD(FLD(FLD(info, 0x1c, char *), 0xa, char *), 0xe, void *);
        lin = (char *)IrOptimizer_NewLinear(1);
        linear_index_counter++;
        FLD(lin, 8, UInt16) = linear_index_counter;
        en = (char *)IrOptimizer_NewENode(0x32);
        FLD(en, 6, void *) = ty;
        FLD(en, 0xe, CInt64) = cint64_zero;
        FLD(lin, 0x20, char *) = en;
        FLD(lin, 0xe, void *) = ty;
        upd = FLD(FLD(info, 0x1c, char *), 0xa, char *);
        operand = FLD(upd, 0x20, void *);
        upd[0] = 3;
        FLD(FLD(FLD(info, 0x1c, char *), 0xa, char *), 1, UInt8) = 0x1e;
        FLD(FLD(FLD(info, 0x1c, char *), 0xa, char *), 0x20, void *) = operand;
        FLD(FLD(FLD(info, 0x1c, char *), 0xa, char *), 0x24, char *) = lin;
        IroUtil_InsertLinearBefore((IROLinear *)lin, (IROLinear *)lin, (IROLinear *)FLD(FLD(info, 0x1c, char *), 0xa, char *));
    }
    return 0;
}

extern UInt8 data13_005e72c7[];
#include "compiler/IroPropagate.h"

/* mark the variable of a referenced address-taken store in data_00588018 */
void fn_004ab480(IROLinear *l, Boolean flag)
{
    Object *obj;
    VarRecord *vr;
    UInt32 f;

    if (flag && l->type == 1 && FLD(l, 0x20, char *)[0] == 0x38) {
        obj = FLD(FLD(l, 0x20, char *), 0xe, Object *);
        f = FLD(l, 2, UInt32);
        if ((f & 0x20) && (!(f & 4) || (f & 0x10))) {
            vr = fn_0044ba70(obj, 0, 1);
            if (vr != NULL)
                Iel_BitSet(data_00588018, vr->index);
        }
    }
}

/* check a use chain pattern: object use -> indirect through ptr -> consumer */
Boolean fn_004ab820(char *p, char *a, char *b, Boolean replace)
{
    char *s;
    char *n;
    char *m;

    if (p != NULL && FLD(p, 0x1c, UInt16) != 0 && (s = FLD(p, 8, char *)) != NULL && s[0] == 1 &&
        FLD(s, 0x20, char *)[0] == 0x38 && FLD(FLD(s, 0x20, char *), 0xe, void *) == FLD(a, 0xe, void *)) {
        n = (char *)IroUtil_FindNextUse((IROLinear *)s);
        if (n != NULL && n[0] == 2 && n[1] == 4 && FLD(n, 0xe, void *) == FLD(b, 0xe, void *)) {
            m = (char *)IroUtil_FindNextUse((IROLinear *)n);
            if (m != NULL) {
                if ((m[0] == 2 || m[0] == 3) && data13_005e72c7[(UInt8)m[1]] != 0)
                    return 0;
                if (replace)
                    IroUtil_ReplaceFirstReference((IROLinear *)n, (IROLinear *)b);
                return 1;
            }
        }
    }
    return 0;
}
extern Boolean fn_00457a30(IROLinear *node);

/* delete an assignment whose result is never used apart from a single reference, folding the reference away */
Boolean fn_0045df90(char *use)
{
    IROLinear *l = FLD(use, 8, IROLinear *);
    Boolean postfix = 0;
    Boolean removable;
    Boolean result;

    if (l->type == 2 && (l->nodetype == 0 || l->nodetype == 1))
        postfix = 1;
    removable = IroUtil_FindNextUse(l) != NULL;
    if (removable && fn_0044d460(l) && data13_005e72c7[l->nodetype])
        removable = fn_00457a30(l);
    result = 0;
    if (removable && l->type == 3 && l->nodetype == 0x1e)
        result = 1;
    if (result) {
        IROLinear *keep;

        FLD(use, 0x18, UInt16) = 0;
        IroDump_Print("Removing referenced dead assignment %d\n", l->index);
        if (postfix) {
            IroUtil_IsTypeSame((Type *)l->u.diadic.right, 0);
            l->type = 0;
            keep = l->u.diadic.left;
        } else {
            IroUtil_IsTypeSame((Type *)l->u.diadic.left, 0);
            l->type = 0;
            keep = l->u.diadic.right;
        }
        IroUtil_ReplaceNextReference(l, keep);
    }
    return result;
}

/* find the operand of an add/sub chain that matches `base` */
IROLinear *fn_004a3ca0(IROLinear *base, IROLinear *l, Boolean *flag)
{
    IROLinear *left = l->u.diadic.left;
    IROLinear *right = l->u.diadic.right;
    IROLinear *s;
    IROLinear *r;
    Boolean tmp;

    if (l->nodetype == 0xf || l->nodetype == 0x10) {
        s = left;
        while (s->type == 2 && s->nodetype == 0x30)
            s = s->u.diadic.left;
        if (s->type == 2 && s->nodetype == 4 && (s->u.diadic.left->flags & 0x100) &&
            IroUtil_LinearConstantTreesSame(base, s)) {
            *flag = 1;
            return left;
        }
        if (s->type == 3 && (s->nodetype == 0xf || s->nodetype == 0x10)) {
            r = fn_004a3ca0(base, s, &tmp);
            if (r != NULL) {
                *flag = 1;
                return r;
            }
        }
    }
    *flag = 0;
    if (l->nodetype == 0xf) {
        s = right;
        while (s->type == 2 && s->nodetype == 0x30)
            s = s->u.diadic.left;
        if (s->type == 2 && s->nodetype == 4 && (s->u.diadic.left->flags & 0x100) &&
            IroUtil_LinearConstantTreesSame(base, s))
            return right;
        if (s->type == 3 && (s->nodetype == 0xf || s->nodetype == 0x10)) {
            r = fn_004a3ca0(base, s, &tmp);
            if (r != NULL)
                return r;
        }
    }
    return NULL;
}
extern void fn_004aa360(IRONode *n);
extern void fn_004a9f40(IRONode *n);

/* canonicalize an add/sub whose operands are themselves add/sub of the same type */
void fn_004a3bf0(IROLinear *base, IROLinear *l)
{
    IROLinear *left = l->u.diadic.left;
    IROLinear *right = l->u.diadic.right;
    IROLinear *p;
    Boolean swapped;

    if (l->nodetype == 0xf) {
        p = fn_004a3ca0(base, l, &swapped);
        if (p != NULL && p != left) {
            if (swapped && left->type == 3 && (left->nodetype == 0xf || left->nodetype == 0x10) &&
                p->rtype == right->rtype) {
                IROLinear *t = left;
                l->u.diadic.left = right;
                left = l->u.diadic.left;
                l->u.diadic.right = t;
                right = l->u.diadic.right;
                swapped = 0;
            }
            if (!swapped && right->type == 3 && (right->nodetype == 0xf || right->nodetype == 0x10) &&
                p->rtype == left->rtype) {
                if (IroUtil_ReplaceNextReference(p, left) != NULL)
                    l->u.diadic.left = p;
            }
        }
    }
}

/* find the last statement in `node` that references the loop variable */
void fn_004a4bc0(char *info, IRONode *node)
{
    char *var = FLD(info, 0x1c, char *);
    IROLinear *l;
    UInt16 idx;

    if (var == NULL) {
        FLD(info, 0x14, void *) = NULL;
        return;
    }
    idx = *FLD(var, 2, UInt16 *);
    l = node->first;
    if (l != NULL) {
        do {
            IroBitVect_ClearBitVector(data_00588018);
            fn_0044b2d0(l);
            if ((idx >> 5) < data_00588018->size && ((1 << idx) & data_00588018->bits[idx >> 5]))
                FLD(info, 0x14, IROLinear *) = l;
            if (l == node->last)
                break;
            l = l->next;
        } while (l != NULL);
    }
    if (FLD(info, 0x14, IROLinear *) != NULL && FLD(info, 0x14, IROLinear *)->type != 3)
        FLD(info, 0x14, void *) = NULL;
}

void fn_004a9ee0(void)
{
    IRONode *n;

    for (n = iro_flowgraph_head; n != NULL; n = n->nextnode) {
        if (n->numpred == 2) {
            if (n->first != NULL && n->first->type == 8)
                fn_004aa360(n);
        } else if (n->numsucc == 2) {
            if (n->last != NULL && (n->last->type == 5 || n->last->type == 6))
                fn_004a9f40(n);
        }
    }
    IroVars_CheckTimedLongjmp();
}
/* move the statements of the blocks start..end (exclusive) that are not part of the loop control to `at`,
 * optionally rebasing induction-variable references by `*offset` + the induction step */
void fn_004a1fe0(IRONode *start, IRONode *end, char *info, IROLinear *at, int unused, CInt64 *offset, Boolean apply)
{
    IRONode *node;
    IROLinear *head = NULL;
    IROLinear *tail = NULL;

    for (node = start; node != NULL && node != end; node = node->nextnode) {
        IROList list;
        IROLinear *last;
        IROLinear *l;

        IroUtil_InitList(&list);
        last = node->last;
        for (l = node->first;; l = l->next) {
            if (l->stmt != NULL)
                ((UInt8 *)l->stmt)[6] |= 0x10;
            if ((l->index < ((int *)info)[8] || l->index > ((int *)info)[9]) && l->type != 8 &&
                !(l->flags & 2)) {
                IroUtil_CopyLinearToList(l, &list);
                if (head == NULL)
                    head = list.head;
                tail = list.tail;
            }
            if (l == last)
                break;
        }
        if (list.head != NULL && list.tail != NULL)
            IroUtil_InsertLinearBefore(list.head, list.tail, at);
    }
    if (apply) {
        SInt32 step = *(SInt32 *)(((char **)info)[7] + 0xe);
        CInt64 stepv;
        stepv.hi = step < 0 ? -1 : 0;
        stepv.lo = step;
        *offset = CInt64_Add(*offset, stepv);
        ((void (*)(IROLinear *, IROLinear *, CInt64))add_const_to_induction_var_references)(head, tail, *offset);
    }
}
extern char *data13_005e67e8;
extern char *data13_005e6a48;

/* append a record describing an invariant address use to the loop's use list */
void fn_004a4c60(IROLinear *l, IROAddrRecord *rec, Boolean flag, Boolean ok)
{
    char *r;
    char *item;
    char *use = FLD(FLD(rec, 6, char *), 0, char *);

    if (!IroPropagate_IsRegisterEligible(FLD(FLD(use, 0x20, char *), 0xe, Object *)) &&
        (l->u.diadic.left->flags & 0x100) && !fn_0044be00(l)) {
        if (data13_005e67e8 == NULL) {
            r = (char *)oalloc(0x12);
            data13_005e67e8 = r;
        } else {
            r = (char *)oalloc(0x12);
            FLD(data13_005e6a48, 0xe, char *) = r;
        }
        data13_005e6a48 = r;
        FLD(r, 0, UInt8) = 0x10;
        FLD(r, 2, IROLinear *) = l;
        FLD(r, 0xa, IROAddrRecord *) = rec;
        FLD(r, 0xe, void *) = NULL;
        FLD(r, 6, void *) = NULL;
        item = (char *)oalloc(8);
        FLD(item, 0, IROLinear *) = l;
        FLD(item, 4, void *) = NULL;
        if (FLD(r, 6, char *) == NULL) {
            FLD(r, 6, char *) = item;
        } else {
            FLD(item, 4, char *) = FLD(r, 6, char *);
            FLD(r, 6, char *) = item;
        }
        if (FLD(use, 2, UInt32) & 4) {
            FLD(r, 0, UInt8) |= 2;
            if (FLD(use, 2, UInt32) & 0x10)
                FLD(r, 0, UInt8) |= 1;
        } else {
            FLD(r, 0, UInt8) |= 1;
        }
        if (!flag && ok)
            FLD(r, 0, UInt8) |= 8;
        if (flag || ok)
            FLD(r, 0, UInt8) &= ~0x10;
    }
}

/* record one invariant address use of `l` in the loop's use list, merging it with an existing record when
 * the same object and address are already present */
Boolean fn_004a4e30(IROLinear *l, Boolean flag, Boolean unused)
{
    IROLinear *addr = l->u.diadic.left;
    IROAddrRecord *rec;
    Boolean ok;
    char *r;

    rec = IroVars_CreateAddrRecord(addr);
    if (addr->type == 1 && addr->u.diadic.left->type == 0x38) {
        rec->numObjRefs++;
        IroVars_PrependElmList(addr, &rec->objRefs);
    } else if (addr->type == 3 && addr->nodetype == 0xf) {
        IroVars_CollectAddrRecordElements(addr, rec);
    } else {
        return 0;
    }
    if (rec->numObjRefs != 1)
        return 0;
    ok = 1;
    if (!(l->flags & 0x100000) && (l->flags & 2))
        ok = 0;
    for (r = data13_005e67e8; r != NULL; r = FLD(r, 0xe, char *)) {
        char *use = FLD(r, 0xa, char *);
        if (FLD(FLD(FLD(FLD(rec, 6, char *), 0, char *), 0x20, char *), 0xe, void *) ==
            FLD(FLD(FLD(FLD(use, 6, char *), 0, char *), 0x20, char *), 0xe, void *)) {
            if (IroUtil_LinearConstantTreesSame(addr, FLD(use, 0, IROLinear *))) {
                char *item = (char *)oalloc(8);
                FLD(item, 0, IROLinear *) = l;
                FLD(item, 4, void *) = NULL;
                if (FLD(r, 6, char *) == NULL) {
                    FLD(r, 6, char *) = item;
                } else {
                    FLD(item, 4, char *) = FLD(r, 6, char *);
                    FLD(r, 6, char *) = item;
                }
            } else {
                FLD(r, 0, UInt8) |= 8;
            }
            if (!flag && ok)
                FLD(r, 0, UInt8) |= 8;
            if (flag || ok)
                FLD(r, 0, UInt8) &= ~0x10;
            break;
        }
    }
    if (r == NULL)
        fn_004a4c60(l, rec, flag, ok);
    return 1;
}
extern BitVector *data13_005e5f3c;
extern BitVector *data13_005e5fe4;
#define IEL_BIT(bv, i) (((i) >> 5) < (bv)->size && ((1 << (i)) & (bv)->bits[(i) >> 5]))

/* decide for each loop block whether it dominates every block with a recorded effect (except `skip`) */
void fn_004a7ec0(IRONode *skip)
{
    IRONode *n;
    IRONode *m;

    for (n = iro_flowgraph_head; n != NULL; n = n->nextnode) {
        if (!IEL_BIT(IRO_LoopScratchVector_005880dc, n->index))
            continue;
        FLD(n, 0x42, UInt8) = 1;
        for (m = iro_flowgraph_head; m != NULL; m = m->nextnode) {
            if (IEL_BIT(IRO_LoopScratchVector_005880dc, m->index)) {
                if (IEL_BIT(data13_005e5f3c, m->index)) {
                    if (!IEL_BIT(m->dom, n->index))
                        FLD(n, 0x42, UInt8) = 0;
                }
                if (IEL_BIT(data13_005e5fe4, m->index) && m != skip) {
                    if (!IEL_BIT(m->dom, n->index))
                        FLD(n, 0x42, UInt8) = 0;
                }
                if (FLD(n, 0x42, UInt8) == 0)
                    break;
            }
        }
    }
}
extern void fn_004a8b70(void);
extern Boolean fn_004a8250(IRONode *n);

/* rebuild logical and conditional expressions from the flow graph */
void fn_004a8160(void)
{
    IRONode *n;

    IRO_ExpressionPropagation();
    IroFlowgraph_RebuildSuccPred();
    IroFlowgraph_ComputeDom();
    IRO_UseDef(0, 0);
    if (copts.deleteDeadInstructions > 0) {
        IroDump_Print("Rebuilding ELORs and ELANDs\n");
        fn_004a8b70();
        IroDump_DumpFunction("RebuildLogicalExpressions", 0);
    }
    if (copts.deleteDeadInstructions > 0) {
        IroDump_Print("Rebuilding ECONDs\n");
        fn_004a9ee0();
        IroDump_DumpFunction("RebuildCondExpressions", 0);
    }
    if (copts.deleteDeadInstructions > 0) {
        IroDump_Print("Generating ECONDASSes\n");
        n = iro_flowgraph_head;
        while (n != NULL && n->last != NULL) {
            Boolean cond = 0;
            if (n->numsucc == 2 && n->last != NULL) {
                if (n->last->type == 5 || n->last->type == 6)
                    cond = 1;
            }
            if (cond && fn_004a8250(n))
                continue;
            n = n->nextnode;
        }
        IroVars_CheckTimedLongjmp();
        IroDump_DumpFunction("GenerateCondAssignments", 0);
    }
}
/* check that every path from `n` to `stop` only passes through blocks dominated by `target`, and that the linears
 * from `start` up to `limit` have no side effects */
Boolean fn_004a9dd0(IRONode *n, IRONode *target, IRONode *stop, IROLinear *start, IROLinear *limit, Boolean reset)
{
    UInt16 i;
    IROLinear *l;
    IRONode *p;

    if (n != stop) {
        if (!IEL_BIT(n->dom, target->index))
            return 0;
        if (reset) {
            for (p = iro_flowgraph_head; p != NULL; p = p->nextnode)
                FLD(p, 0x3c, UInt8) = 0;
        } else if (FLD(n, 0x3c, UInt8)) {
            return 1;
        }
        FLD(n, 0x3c, UInt8) = 1;
        for (i = 0; i < n->numsucc; i++) {
            IRONode *s = iroNodesByIndex[n->succ[i]];
            if (!fn_004a9dd0(s, target, stop, s->first, limit, 0))
                return 0;
        }
    }
    for (l = start; l != NULL && l != n->last->next; l = l->next) {
        if (l == limit)
            return 1;
        if (l->type != 0 && l->type != 8 && !(l->flags & 2))
            return 0;
    }
    return 1;
}
#endif
