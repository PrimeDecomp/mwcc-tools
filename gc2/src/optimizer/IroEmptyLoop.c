#include "compiler/IroEmptyLoop.h"
#include "compiler/IroDump.h"
#include "compiler/IroFlowgraph.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroLoop.h"
#include "compiler/IroUtil.h"
#include "compiler/IroVars.h"
#include "compiler/BitVector.h"
#include "compiler/CInt64.h"
#include "compiler/CParser.h"

static int EmptyLoop(IRONode* fnode);
static int CanRemoveRedundantLoop(IROLoop* loop);
static int CanRemoveRedundantLoop1(IROLoop* loop);
static int RedundantLoopCheck(IROLoop* loop);
static int CheckStepOverFlow1_EmptyLoop(IROLoop* loop, CInt64* val1, CInt64* val2);
static int CheckStepOverFlow2_EmptyLoop(IROLoop* loop, CInt64* val1, CInt64* val2);

void IRO_FindEmptyLoops(void) {
    IRONode* fnode;
    IRONode* pred;
    UInt16 i;
    UInt16 x;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        x = FALSE;
        for (i = 0; i < fnode->numpred; i++) {
            pred = IRO_NodeTable[fnode->pred[i]];
            if (Bv_IsBitSet(fnode->index, pred->dom)) {
                if (!x) {
                    Bv_AllocVector(&InLoop, IRO_NumNodes + 1);
                    Bv_Clear(InLoop);
                    Bv_SetBit(fnode->index, InLoop);
                }
                x = TRUE;
                Bv_SetBit(pred->index, InLoop);
                if (pred != fnode) {
                    AddPreds(pred);
                }
            }
        }

        if (x) {
            IRO_Dump("IRO_FindEmptyLoops:Found loop with header %d\n", fnode->index);
            IRO_DumpBits("Loop includes: ", InLoop);
            EmptyLoop(fnode);
            IRO_UpdateFlagsOnInts();
        }
    }

    IRO_CheckForUserBreak();
}

// TODO: 2.6's register allocation needs this declaration order, the separate counter `k`, the
// block-local variables below and the counters incremented before the assignments beside them (Pro7
// shares `i`, `type20`, `constnd` and `save` and re-indexes IRO_NodeTable; its body is 56.7%). Found
// by search: plausible as the original's, not proven.
static int EmptyLoop(IRONode* fnode) {
    ENode* enode;
    int counter2;
    int counter;
    IROLoop* loop;
    IRONode* succ;
    IRONode* pred;
    int j;
    IRONode* bestpred;
    IROLinear* constnd;
    Boolean flag;
    UInt16 i;
    IRONode* r24;
    UInt32 counter3;
    IRONode* r20;
    int flag2;
    IRONode* bestsucc;
    IRONode* r21;
    VarRecord* var;
    Type* type20;
    IROLinear* save;
    UInt16 k;

    flag = FALSE;
    counter = 0;
    LoopNode = fnode;
    FindMustReach();

    for (var = IRO_FirstVar; var; var = var->next) {
        var->inductionState = 1;
    }

    ComputeLoopKills();
    ComputeLoopInvariance();
    ComputeLoopInduction();

    LoopNode = fnode;
    ConditionalHeaderAtBottom = FALSE;

    bestpred = NULL;
    flag2 = FALSE;
    for (i = 0; i < LoopNode->numpred; i++) {
        pred = IRO_NodeTable[LoopNode->pred[i]];
        if (!Bv_IsBitSet(pred->index, InLoop)) {
            flag2 = TRUE;
            if (pred->nextnode == fnode) {
                CError_ASSERT(173, !bestpred || pred == bestpred);
                bestpred = pred;
            }
        }
    }

    if (!flag2) {
        IRO_Dump("No predecessor outside the loop\n");
        return FALSE;
    }

    bestsucc = NULL;
    for (k = 0; k < LoopNode->numsucc; k++) {
        succ = IRO_NodeTable[LoopNode->succ[k]];
        if (Bv_IsBitSet(succ->index, InLoop)) {
            counter++;
            bestsucc = succ;
        }
    }

    if (LoopNode == bestsucc && counter == 1) {
        flag = TRUE;
    }

    if (LoopNode->last->type != IROLinearIf || LoopNode->last->type != IROLinearIfNot || flag) {
        counter2 = 0;
        for (j = 0; j < LoopNode->numpred; j++) {
            IRONode* node = IRO_NodeTable[LoopNode->pred[j]];
            if (Bv_IsBitSet(node->index, InLoop)) {
                counter2++;
                r21 = node;
            }
        }

        r24 = NULL;
        counter3 = 0;
        for (j = 0; j < LoopNode->numpred; j++) {
            if (!Bv_IsBitSet(IRO_NodeTable[LoopNode->pred[j]]->index, InLoop)) {
                counter3++;
                r24 = IRO_NodeTable[LoopNode->pred[j]];
            }
        }

        if (counter2 == 1 && counter3 == 1) {
            if (r21->last->type == IROLinearIf) {
                if ((Bv_IsBitSet(LoopNode->nextnode->index, InLoop) &&
                     !Bv_IsBitSet(r21->nextnode->index, InLoop)) ||
                    flag) {
                    IRO_Dump("Standard while loop layout\n");
                    loop = ExtractLoopInfo(r21);
                    if (flag) {
                        loop->flags |= LoopFlags_20000;
                    }
                    FindAssignmenttoInductionVar(loop, r24);
                    r20 = r24;
                    while (r20 &&
                           !loop->init &&
                           r20->numpred ==
                           1 &&
                           IRO_NodeTable[r20->pred[0]]->numsucc == 1) {
                        FindAssignmenttoInductionVar(loop, IRO_NodeTable[r20->pred[0]]);
                        r20 = IRO_NodeTable[r20->pred[0]];
                    }

                    if (CanRemoveRedundantLoop(loop)) {
                        IRO_Dump(
                            "EmptyLoop: # of iterations =%ld, FinalStoreVal=%ld\n",
                            CInt64_GetULong(&loop->iterCount),
                            CInt64_GetULong(&loop->finalStoreVal)
                        );
                        IRO_NopOut(r21->last->u.label.cond);
                        r21->last->type = IROLinearNop;
                        type20 = loop->induction->nd->rtype;
                        constnd = IRO_NewLinear(IROLinearOperand);
                        constnd->index = ++IRO_NumLinear;
                        enode = IRO_NewENode(EINTCONST);
                        enode->rtype = type20;
                        enode->data.intval = loop->finalStoreVal;
                        constnd->u.node = enode;
                        constnd->rtype = type20;

                        if (loop->induction->nd->type == IROLinearOp1Arg) {
                            save = loop->induction->nd->u.monadic;
                            loop->induction->nd->type = IROLinearOp2Arg;
                            loop->induction->nd->nodetype = EASS;
                            loop->induction->nd->u.diadic.left = save;
                            loop->induction->nd->u.diadic.right = constnd;
                            IRO_Paste(constnd, constnd, loop->induction->nd);
                        } else if (loop->induction->nd->type == IROLinearOp2Arg) {
                            loop->induction->nd->nodetype = EASS;
                            IRO_NopOut(loop->induction->nd->u.diadic.right);
                            loop->induction->nd->u.diadic.right = constnd;
                            IRO_Paste(constnd, constnd, loop->induction->nd);
                        }
                    } else if (CanRemoveRedundantLoop1(loop)) {
                        Type* type2;
                        IROLinear* const2;
                        IROLinear* save2;

                        IRO_Dump("EmptyLoop: self recursive dowhile(--n ) loop\n");

                        r21->last->type = IROLinearNop;
                        type2 = loop->induction->nd->rtype;
                        const2 = IRO_NewLinear(IROLinearOperand);
                        const2->index = ++IRO_NumLinear;
                        enode = IRO_NewENode(EINTCONST);
                        enode->rtype = type2;
                        enode->data.intval = cint64_zero;
                        const2->u.node = enode;
                        const2->rtype = type2;

                        save2 = loop->induction->nd->u.monadic;
                        loop->induction->nd->type = IROLinearOp2Arg;
                        loop->induction->nd->nodetype = EASS;
                        loop->induction->nd->u.diadic.left = save2;
                        loop->induction->nd->u.diadic.right = const2;
                        IRO_Paste(const2, const2, loop->induction->nd);
                    }
                } else {
                    IRO_Dump("NonStandard while loop layout\n");
                }
            } else {
                IRO_Dump("NonStandard while loop layout\n");
            }
        } else {
            IRO_Dump("Cannot handle Do While Loop with multiple tails\n");
        }
    }

    return FALSE;
}

static int CanRemoveRedundantLoop(IROLoop* loop) {
    IROLinear* inner;

    if (loop->flags & LoopFlags_10000) {
        IRO_Dump(
            "CanRemoveRedundantLoop:No because detection of dowhile(n--) loop not supported\n"
        );
        return FALSE;
    }

    if (loop->flags & LP_LOOP_HAS_ASM) {
        IRO_Dump("CanRemoveRedundantLoop:No due to LP_LOOP_HAS_ASM \n");
        return FALSE;
    }

    if (loop->flags & LP_IFEXPR_NON_CANONICAL) {
        IRO_Dump("CanRemoveRedundantLoop:No due to LP_IFEXPR_NON_CANONICAL \n");
        return FALSE;
    }

    if (loop->flags & LP_LOOP_HAS_CALLS) {
        IRO_Dump("CanRemoveRedundantLoop:No due to LP_LOOP_HAS_CALLS \n");
        return FALSE;
    }

    if (loop->flags & LP_LOOP_HAS_CNTRLFLOW) {
        IRO_Dump("CanRemoveRedundantLoop:No due to LP_LOOP_HAS_CNTRLFLOW \n");
        return FALSE;
    }

    if (loop->flags & LP_INDUCTION_NOT_FOUND) {
        IRO_Dump("CanRemoveRedundantLoop:No due to LP_INDUCTION_NOT_FOUND \n");
        return FALSE;
    }

    if (loop->flags & LP_HAS_MULTIPLE_INDUCTIONS) {
        IRO_Dump("CanRemoveRedundantLoop:No due to LP_HAS_MULTIPLE_INDUCTIONS \n");
        return FALSE;
    }

    if (loop->flags & LP_LOOP_HDR_HAS_SIDEEFFECTS) {
        IRO_Dump("CanRemoveRedundantLoop:No due to LP_LOOP_HDR_HAS_SIDEEFFECTS \n");
        return FALSE;
    }

    if (!(loop->flags & LoopFlags_200)) {
        IRO_Dump("CanRemoveRedundantLoop:No because header does not follow induction update \n");
        return FALSE;
    }

    if (!(loop->flags & LoopFlags_10000)) {
        inner = loop->cond->u.diadic.right;
        if (!IRO_IsIntConstant(inner) && !(inner->flags & IROLF_LoopInvariant)) {
            IRO_Dump("CanRemoveRedundantLoop:No because Loop Upper Bound is Variant in the loop\n");
            return FALSE;
        }

        if (!loop->init) {
            IRO_Dump(
                "CanRemoveRedundantLoop:No because there is no initialization of loop index in PreHeader\n"
            );
            return FALSE;
        }

        if (!IRO_IsVariable(loop->init->u.diadic.left)) {
            IRO_Dump(
                "CanRemoveRedundantLoop:No because initial value of induction stored thru pointer\n"
            );
            return FALSE;
        }

        if (!is_unsigned(loop->init->rtype)) {
            if (IRO_IsIntConstant(loop->init->u.diadic.right)) {
                if (!CInt64_GreaterEqual(
                        loop->init->u.diadic.right->u.node->data.intval, cint64_zero
                    )) {
                    IRO_Dump(
                        "CanRemoveRedundantLoop:No because initial value of induction is signed but < 0\n"
                    );
                    return FALSE;
                }
            } else {
                IRO_Dump(
                    "CanRemoveRedundantLoop:No because initial value of induction is signed and not constant\n"
                );
                return FALSE;
            }
        }

        if (!(loop->flags & LP_LOOP_STEP_ISPOS) && !(loop->flags & LP_LOOP_STEP_ISNEG)) {
            IRO_Dump(
                "CanRemoveRedundantLoop:No because LP_LOOP_STEP_ISPOS/LP_LOOP_STEP_ISNEG   is not set\n"
            );
            return FALSE;
        }

        if ((loop->flags & LP_LOOP_STEP_ISPOS) &&
            CheckStepOverFlow1_EmptyLoop(loop, &loop->iterCount, &loop->finalStoreVal)) {
            IRO_Dump("CanRemoveRedundantLoop:No because Final Value of indution will overflow\n");
            return FALSE;
        }

        if ((loop->flags & LP_LOOP_STEP_ISNEG) &&
            CheckStepOverFlow2_EmptyLoop(loop, &loop->iterCount, &loop->finalStoreVal)) {
            IRO_Dump("CanRemoveRedundantLoop:No because Final Value of indution will overflow\n");
            return FALSE;
        }
    }

    if (RedundantLoopCheck(loop)) {
        return TRUE;
    }
    return FALSE;
}

static int CanRemoveRedundantLoop1(IROLoop* loop) {
    if ((loop->flags & LoopFlags_10000) && (loop->flags & LoopFlags_20000)) {
        if (loop->flags & LP_LOOP_HAS_ASM) {
            IRO_Dump("CanRemoveRedundantLoop1:No due to LP_LOOP_HAS_ASM \n");
            return FALSE;
        }

        if (loop->flags & LP_IFEXPR_NON_CANONICAL) {
            IRO_Dump("CanRemoveRedundantLoop1:No due to LP_IFEXPR_NON_CANONICAL \n");
            return FALSE;
        }

        if (loop->flags & LP_LOOP_HAS_CALLS) {
            IRO_Dump("CanRemoveRedundantLoop1:No due to LP_LOOP_HAS_CALLS \n");
            return FALSE;
        }

        if (loop->flags & LP_LOOP_HAS_CNTRLFLOW) {
            IRO_Dump("CanRemoveRedundantLoop1:No due to LP_LOOP_HAS_CNTRLFLOW \n");
            return FALSE;
        }

        if (loop->flags & LP_INDUCTION_NOT_FOUND) {
            IRO_Dump("CanRemoveRedundantLoop1:No due to LP_INDUCTION_NOT_FOUND \n");
            return FALSE;
        }

        if (loop->flags & LP_HAS_MULTIPLE_INDUCTIONS) {
            IRO_Dump("CanRemoveRedundantLoop1:No due to LP_HAS_MULTIPLE_INDUCTIONS \n");
            return FALSE;
        }

        if (loop->flags & LP_LOOP_HDR_HAS_SIDEEFFECTS) {
            IRO_Dump("CanRemoveRedundantLoop1:No due to LP_LOOP_HDR_HAS_SIDEEFFECTS \n");
            return FALSE;
        }

        if (!(loop->flags & LoopFlags_200)) {
            IRO_Dump(
                "CanRemoveRedundantLoop1:No because header does not follow induction update \n"
            );
            return FALSE;
        }

        if (loop->induction->nd->type ==
            IROLinearOp1Arg &&
            loop->induction->nd->nodetype == EPREDEC) {
            if (is_unsigned(loop->induction->nd->rtype)) {
                return TRUE;
            }
            IRO_Dump("CanRemoveRedundantLoop1:No because induction not of the right type \n");
            return FALSE;
        }

        IRO_Dump("CanRemoveRedundantLoop1:No because induction operator not a predec \n");
        return FALSE;
    } else {
        return FALSE;
    }
}

static int RedundantLoopCheck(IROLoop* loop) {
    IRONode* fnode;
    IROLinear* nd;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        if (Bv_IsBitSet(fnode->index, InLoop) && fnode != loop->fnode && (nd = fnode->first)) {
            while (1) {
                if ((nd->index < loop->lo || nd->index > loop->hi) &&
                    nd->type !=
                    IROLinearNop &&
                    nd->type != IROLinearLabel) {
                    if (IS_LINEAR_DIADIC(nd, EASS)) {
                        if (!(nd->flags & IROLF_Reffed)) {
                            if (IS_LINEAR_MONADIC(nd->u.diadic.left, EINDIRECT)) {
                                if (nd->u.diadic.left->rtype &&
                                    CParser_IsVolatile(
                                        nd->u.diadic.left->rtype,
                                        nd->u.diadic.left->nodeflags & ENODE_FLAG_QUALS
                                    )) {
                                    IRO_Dump(
                                        " EASS at %d fail as store to volatile memory \n", nd->index
                                    );
                                    return FALSE;
                                }

                                if ((nd->u.diadic.left->u.monadic->flags & IROLF_LoopInvariant) &&
                                    (nd->u.diadic.right->flags & IROLF_LoopInvariant)) {
                                    IRO_Dump(" EASS at %d pass\n", nd->index);
                                } else {
                                    IRO_Dump(
                                        " EASS at %d fail,  either LHS address or RHS is variant \n",
                                        nd->index
                                    );
                                    return FALSE;
                                }
                            } else {
                                IRO_Dump(
                                    "Found EASS nodes whose lhs root  is not a EINDIRECT node\n"
                                );
                                return FALSE;
                            }
                        } else {
                            IRO_Dump(
                                "Found EASS node that is referenced i.e embedded assignment\n"
                            );
                            return FALSE;
                        }
                    } else {
                        if (!(nd->flags & IROLF_Reffed)) {
                            IRO_Dump("Found non EASS top level node in the loop\n");
                            return FALSE;
                        }
                    }
                }

                if (nd == fnode->last) {
                    break;
                }
                nd = nd->next;
            }
        }
    }

    return TRUE;
}

static int CheckStepOverFlow1_EmptyLoop(IROLoop* loop, CInt64* val1, CInt64* val2) {
    Boolean isUnsigned;
    IROLinear* nd2;
    IROLinear* nd1;
    CInt64 nd2value;
    CInt64 nd1value;
    CInt64 addConst;
    CInt64 work;
    CInt64 neg1;

    nd2 = loop->init->u.diadic.right;
    nd1 = loop->cond->u.diadic.right;
    isUnsigned = is_unsigned(loop->cond->u.diadic.right->rtype);

    if (IRO_IsIntConstant(nd2) && IRO_IsIntConstant(nd1)) {
        nd2value = nd2->u.node->data.intval;
        nd1value = nd1->u.node->data.intval;
        if (isUnsigned) {
            if (CInt64_LessEqualU(nd1value, nd2value)) {
                return TRUE;
            }
        } else {
            if (CInt64_LessEqual(nd1value, nd2value)) {
                return TRUE;
            }
        }

        CInt64_SetLong(&addConst, loop->induction->addConst);
        CInt64_SetLong(&neg1, -1);
        *val1 = CInt64_Sub(nd1value, nd2value);
        *val1 = CInt64_Add(*val1, addConst);
        if (IS_LINEAR_DIADIC(loop->cond, ELESS)) {
            *val1 = CInt64_Add(*val1, neg1);
        }

        CError_ASSERT(855, !CInt64_IsZero(&addConst));

        if (isUnsigned) {
            *val1 = CInt64_DivU(*val1, addConst);
        } else {
            *val1 = CInt64_Div(*val1, addConst);
        }

        if (CInt64_Equal(*val1, cint64_zero)) {
            return TRUE;
        }

        if (isUnsigned) {
            if (CInt64_LessEqualU(*val1, cint64_zero)) {
                CError_FATAL(877);
            }
        } else {
            if (CInt64_LessEqual(*val1, cint64_zero)) {
                CError_FATAL(886);
            }
        }

        if (isUnsigned) {
            *val2 = CInt64_MulU(*val1, addConst);
            *val2 = CInt64_Add(*val2, nd2value);
        } else {
            *val2 = CInt64_Mul(*val1, addConst);
            *val2 = CInt64_Add(*val2, nd2value);
        }
    } else {
        return TRUE;
    }

    CInt64_SetLong(&addConst, loop->induction->addConst);
    work = CInt64_Add(nd1value, addConst);

    if (isUnsigned) {
        if (CInt64_LessU(work, nd1value)) {
            return TRUE;
        }
    } else {
        if (CInt64_Less(work, nd1value)) {
            return TRUE;
        }
    }

    return FALSE;
}

static int CheckStepOverFlow2_EmptyLoop(IROLoop* loop, CInt64* val1, CInt64* val2) {
    Boolean isUnsigned;
    IROLinear* nd2;
    IROLinear* nd1;
    CInt64 nd2value;
    CInt64 nd1value;
    CInt64 addConst;
    CInt64 work;
    CInt64 neg1;

    nd1 = loop->init->u.diadic.right;
    nd2 = loop->cond->u.diadic.right;
    isUnsigned = is_unsigned(loop->cond->u.diadic.right->rtype);

    if (IRO_IsIntConstant(nd2) && IRO_IsIntConstant(nd1)) {
        nd2value = nd2->u.node->data.intval;
        nd1value = nd1->u.node->data.intval;
        if (isUnsigned) {
            if (CInt64_LessEqualU(nd1value, nd2value)) {
                return TRUE;
            }
        } else {
            if (CInt64_LessEqual(nd1value, nd2value)) {
                return TRUE;
            }
        }

        CInt64_SetLong(&addConst, loop->induction->addConst);
        CInt64_SetLong(&neg1, -1);
        *val1 = CInt64_Sub(nd1value, nd2value);
        *val1 = CInt64_Add(*val1, addConst);
        if (IS_LINEAR_DIADIC(loop->cond, EGREATER)) {
            *val1 = CInt64_Add(*val1, neg1);
        }

        CError_ASSERT(995, !CInt64_IsZero(&addConst));

        if (isUnsigned) {
            *val1 = CInt64_DivU(*val1, addConst);
        } else {
            *val1 = CInt64_Div(*val1, addConst);
        }

        if (CInt64_Equal(*val1, cint64_zero)) {
            return TRUE;
        }

        if (isUnsigned) {
            if (CInt64_LessEqualU(*val1, cint64_zero)) {
                return FALSE;
            }
        } else {
            if (CInt64_LessEqual(*val1, cint64_zero)) {
                return FALSE;
            }
        }

        if (isUnsigned) {
            *val2 = CInt64_MulU(*val1, addConst);
            *val2 = CInt64_Sub(nd1value, *val2);
        } else {
            *val2 = CInt64_Mul(*val1, addConst);
            *val2 = CInt64_Sub(nd1value, *val2);
        }
    } else {
        return TRUE;
    }

    CInt64_SetLong(&addConst, loop->induction->addConst);
    work = CInt64_Sub(nd2value, addConst);

    if (isUnsigned) {
        if (CInt64_GreaterU(work, nd2value)) {
            return TRUE;
        }
    } else {
        if (CInt64_Greater(work, nd1value)) {
            return TRUE;
        }
    }

    return FALSE;
}
