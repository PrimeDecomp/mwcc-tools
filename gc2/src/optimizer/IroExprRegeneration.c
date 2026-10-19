#include "compiler/IroExprRegeneration.h"
#include "compiler/IroCSE.h"
#include "compiler/IroDump.h"
#include "compiler/IroFlowgraph.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroSubable.h"
#include "compiler/IroTransform.h"
#include "compiler/IROUseDef.h"
#include "compiler/IroUtil.h"
#include "compiler/IroVars.h"
#include "compiler/BitVector.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/Registers.h"
#include <setjmp.h>
#include <string.h>

// forward decls
static void Regen_GetExprUses(IROLinear* linear, Boolean isEntry);
static void RebuildPossibleCondExpression(IRONode* fnode);
static void AddNodeAndSuccessorsRecursively(IRONodes* nodes, IRONode* fnode1, IRONode* fnode2);

static Boolean FindFlowgraphNodeThatStartsWithLabel(
    CLabel* label, IRONode** result1, IRONode** result2
) {
    IRONode* prev;
    IRONode* iter;

    prev = IRO_FirstNode;
    if (prev != NULL) {
        iter = prev->nextnode;
    }

    while (iter) {
        if (iter->first->type == IROLinearLabel && iter->first->u.label.label == label) {
            *result1 = iter;
            *result2 = prev;
            return 1;
        }

        prev = iter;
        iter = iter->nextnode;
    }

    return 0;
}

static IROLinear* FindLastDiadicTopLevelAssignmentInFlowgraphNode(IRONode* fnode) {
    IROLinear* scan;
    IROLinear* result;

    result = NULL;

    for (scan = fnode->first; scan != fnode->last->next; scan = scan->next) {
        if (scan->type ==
            IROLinearOp2Arg &&
            scan->nodetype ==
            EASS &&
            !(scan->flags & IROLF_Reffed)) {
            result = scan;
        }
    }

    return result;
}

static Boolean RewriteUse(IROUse* use, ENode* enode, IROLinear* nd, Boolean flag) {
    IROLinear* father;
    IROLinear* father2;

    if (use &&
        use->reachingDefCount &&
        use->linear &&
        use->linear->type ==
        IROLinearOperand &&
        use->linear->u.node->type ==
        EOBJREF &&
        use->linear->u.node->data.objref ==
        enode->data.objref &&
        (father = IRO_LocateFather(use->linear)) &&
        father->type ==
        IROLinearOp1Arg &&
        father->nodetype ==
        EINDIRECT &&
        father->rtype ==
        nd->rtype &&
        (father2 = IRO_LocateFather(father)) &&
        ((father2->type != IROLinearOp1Arg && father2->type != IROLinearOp2Arg) ||
         !IRO_IsModifyOp[father2->nodetype])) {
        if (flag) {
            IRO_LocateFather_Cut_And_Paste(father, nd);
        }
        return 1;
    }

    return 0;
}

#pragma pack(push, 2)
static struct {
    jmp_buf buf;
    UInt16 index;
    Boolean flag;
} scuai;
#pragma pack(pop)

static void StatementContainsUseAction(IROLinear* linear, Boolean isFirst) {
    if (isFirst && linear->index == scuai.index) {
        scuai.flag = 1;
        longjmp(scuai.buf, 1);
    }
}

static Boolean StatementContainsUse(IROLinear* a, IROLinear* b) {
    memset(&scuai, 0, sizeof(scuai));
    scuai.index = b->index;
    scuai.flag = 0;

    if (!setjmp(scuai.buf)) {
        IRO_WalkInts(a, a, StatementContainsUseAction);
    }

    return scuai.flag;
}

#pragma pack(push, 2)
static struct {
    jmp_buf buf;
    Boolean flag;
} scseai;
#pragma pack(pop)

static void StatementContainsSideEffectAction(IROLinear* linear, Boolean isFirst) {
    if (isFirst) {
        switch (linear->type) {
            case IROLinearOperand:
            case IROLinearOp1Arg:
            case IROLinearOp2Arg:
            case IROLinearOp3Arg:
            case IROLinearFunccall:
            case IROLinearAsm:
                if (IRO_HasSideEffect(linear)) {
                    scseai.flag = 1;
                    longjmp(scseai.buf, 1);
                }
                break;
        }
    }
}

static Boolean StatementContainsSideEffect(IROLinear* linear) {
    memset(&scseai, 0, sizeof(scseai));
    scseai.flag = 0;

    if (!setjmp(scseai.buf)) {
        IRO_WalkInts(linear, linear, StatementContainsSideEffectAction);
    }

    return scseai.flag;
}

// 2.x: whether an operand of nd evaluated unordered with skip has a side effect
static void CheckUnorderedRegionsForSideEffects(IROLinear* nd, IROLinear* skip, Boolean* result) {
    int i;

    switch (nd->type) {
        case IROLinearOp2Arg:
            if (nd->u.diadic.left != skip && IRO_HasSideEffect(nd->u.diadic.left)) {
                *result = 1;
            }
            if (nd->nodetype != ELAND && nd->nodetype != ELOR && nd->nodetype != ECOMMA) {
                if (nd->u.diadic.right != skip && IRO_HasSideEffect(nd->u.diadic.right)) {
                    *result = 1;
                }
            }
            break;
        case IROLinearOp3Arg:
            if (nd->u.args3.a != skip && IRO_HasSideEffect(nd->u.args3.a)) {
                *result = 1;
            }
            if (nd->nodetype != ECOND && nd->nodetype != ECONDASS) {
                if (nd->u.args3.b != skip && IRO_HasSideEffect(nd->u.args3.b)) {
                    *result = 1;
                }
                if (nd->u.args3.c != skip && IRO_HasSideEffect(nd->u.args3.c)) {
                    *result = 1;
                }
            }
            break;
        case IROLinearFunccall:
            if (nd->u.funccall.callee != skip && IRO_HasSideEffect(nd->u.funccall.callee)) {
                *result = 1;
            }
            for (i = 0; !*result && i < nd->u.funccall.argCount; i++) {
                if (nd->u.funccall.args[i] != skip && IRO_HasSideEffect(nd->u.funccall.args[i])) {
                    *result = 1;
                }
            }
            break;
    }
}

static Boolean HasSideEffectsBeforeUse(
    IRONode* fnode1, IRONode* fnode2, IRONode* fnode3, IROLinear* nd, Boolean flag
) {
    IRONode* fnode;
    IROLinear* scannd;
    UInt16 i;
    IROLinear* iter;
    Boolean result;

    if (fnode1 != fnode3) {
        if (flag) {
            for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
                fnode->marked = 0;
            }
        } else if (fnode1->marked) {
            return 0;
        }
        fnode1->marked = 1;
    }

    for (scannd = fnode1->first; scannd && scannd != fnode1->last->next; scannd = scannd->next) {
        if (!(scannd->flags & IROLF_Reffed)) {
            if (StatementContainsUse(scannd, nd)) {
                result = 0;
                if (scannd != nd) {
                    iter = nd;
                    nd = IRO_LocateFather(nd);
                    while (nd && !result) {
                        CheckUnorderedRegionsForSideEffects(nd, iter, &result);
                        iter = nd;
                        nd = IRO_LocateFather(nd);
                    }
                }
                return result;
            }
            if (StatementContainsSideEffect(scannd)) {
                return 1;
            }
        }
    }

    if (fnode1 != fnode3) {
        for (i = 0; i < fnode1->numsucc; i++) {
            if (HasSideEffectsBeforeUse(IRO_NodeTable[fnode1->succ[i]], fnode2, fnode3, nd, 0)) {
                return 1;
            }
        }
    }

    return 0;
}

static void CheckUnorderedRegionForUses(IROLinear* nd, BitVector* bv, Boolean* result) {
    Bv_Clear(IRO_VarKills);
    IRO_WalkInts(nd, nd, Regen_GetExprUses);
    if (Bv_BitsInCommon(bv, IRO_VarKills)) {
        *result = 1;
    }
}

// 2.x: whether an operand of nd evaluated unordered with skip uses a variable of bv
static void CheckUnorderedRegionsForUses(
    IROLinear* nd, IROLinear* skip, BitVector* bv, Boolean* result
) {
    int i;

    switch (nd->type) {
        case IROLinearOp2Arg:
            if (nd->u.diadic.left != skip) {
                CheckUnorderedRegionForUses(nd->u.diadic.left, bv, result);
            }
            if (nd->nodetype != ELAND && nd->nodetype != ELOR && nd->nodetype != ECOMMA) {
                if (nd->u.diadic.right != skip) {
                    CheckUnorderedRegionForUses(nd->u.diadic.right, bv, result);
                }
            }
            break;
        case IROLinearOp3Arg:
            if (nd->u.args3.a != skip) {
                CheckUnorderedRegionForUses(nd->u.args3.a, bv, result);
            }
            if (nd->nodetype != ECOND && nd->nodetype != ECONDASS) {
                if (nd->u.args3.b != skip) {
                    CheckUnorderedRegionForUses(nd->u.args3.b, bv, result);
                }
                if (nd->u.args3.c != skip) {
                    CheckUnorderedRegionForUses(nd->u.args3.c, bv, result);
                }
            }
            break;
        case IROLinearFunccall:
            if (nd->u.funccall.callee != skip) {
                CheckUnorderedRegionForUses(nd->u.funccall.callee, bv, result);
            }
            for (i = 0; !*result && i < nd->u.funccall.argCount; i++) {
                if (nd->u.funccall.args[i] != skip) {
                    CheckUnorderedRegionForUses(nd->u.funccall.args[i], bv, result);
                }
            }
            break;
    }
}

static Boolean UsesAKilledVarBeforeUse(
    IRONode* fnode1, IRONode* fnode2, IRONode* fnode3, IROLinear* nd, BitVector* bv, Boolean flag
) {
    IRONode* fnode;
    UInt16 i;
    IROLinear* scannd;
    IROLinear* iter;
    Boolean result;

    if (fnode1 != fnode3) {
        if (flag) {
            for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
                fnode->marked = 0;
            }
        } else if (fnode1->marked) {
            return 0;
        }
        fnode1->marked = 1;
    }

    for (scannd = fnode1->first; scannd && scannd != fnode1->last->next; scannd = scannd->next) {
        if (!(scannd->flags & IROLF_Reffed)) {
            if (StatementContainsUse(scannd, nd)) {
                result = 0;
                if (scannd != nd) {
                    iter = nd;
                    nd = IRO_LocateFather(nd);
                    while (nd && !result) {
                        CheckUnorderedRegionsForUses(nd, iter, bv, &result);
                        iter = nd;
                        nd = IRO_LocateFather(nd);
                    }
                }
                return result;
            }
            Bv_Clear(IRO_VarKills);
            IRO_WalkInts(scannd, scannd, Regen_GetExprUses);
            if (Bv_BitsInCommon(bv, IRO_VarKills)) {
                return 1;
            }
        }
    }

    if (fnode1 != fnode3) {
        for (i = 0; i < fnode1->numsucc; i++) {
            if (UsesAKilledVarBeforeUse(
                    IRO_NodeTable[fnode1->succ[i]], fnode2, fnode3, nd, bv, 0
                )) {
                return 1;
            }
        }
    }

    return 0;
}

static void Regen_GetExprUses(IROLinear* linear, Boolean isEntry) {
    Object* obj;
    VarRecord* var;

    if (isEntry) {
        if (linear->type == IROLinearOperand && linear->u.node->type == EOBJREF) {
            obj = linear->u.node->data.objref;
            if ((linear->flags & IROLF_Ind) &&
                (!(linear->flags & IROLF_Assigned) || (linear->flags & IROLF_Used))) {
                if ((var = IRO_FindVar(obj, 0, 1))) {
                    Bv_SetBit(var->index, IRO_VarKills);
                }
            }
        }
    }
}

static void Regen_GetExprKills(IROLinear* linear, Boolean isEntry) {
    if (isEntry) {
        IRO_GetKills(linear);
    }
}

static void CheckUnorderedRegionForSideEffectsAndUses(
    IROLinear* nd, BitVector* bv1, BitVector* bv2, Boolean* result
) {
    if (IRO_HasSideEffect(nd)) {
        *result = 1;
    } else {
        Bv_Clear(bv2);
        Bv_Clear(IRO_VarKills);
        IRO_WalkTree(nd, Regen_GetExprUses);
        Bv_Or(IRO_VarKills, bv2);

        if (Bv_BitsInCommon(bv1, bv2)) {
            *result = 1;
        }
    }
}

static void CheckUnorderedRegionsForSideEffectsAndUses(
    IROLinear* nd1, IROLinear* nd2, BitVector* bv1, BitVector* bv2, Boolean* result
) {
    int i;

    switch (nd1->type) {
        case IROLinearOp2Arg:
            if (nd1->nodetype != ELAND && nd1->nodetype != ELOR && nd1->nodetype != ECOMMA) {
                if (nd1->u.diadic.left != nd2) {
                    CheckUnorderedRegionForSideEffectsAndUses(nd1->u.diadic.left, bv1, bv2, result);
                }
                if (nd1->u.diadic.right != nd2) {
                    CheckUnorderedRegionForSideEffectsAndUses(
                        nd1->u.diadic.right, bv1, bv2, result
                    );
                }
            }
            break;
        case IROLinearFunccall:
            if (nd1->u.funccall.callee != nd2) {
                CheckUnorderedRegionForSideEffectsAndUses(nd1->u.funccall.callee, bv1, bv2, result);
            }
            for (i = 0; !*result && i < nd1->u.funccall.argCount; i++) {
                if (nd1->u.funccall.args[i] != nd2) {
                    CheckUnorderedRegionForSideEffectsAndUses(
                        nd1->u.funccall.args[i], bv1, bv2, result
                    );
                }
            }
            break;
    }
}

static Boolean CheckThenOrElseBranch(
    IRONode* fnode1, IRONode* fnode2, IROLinear* nd, Boolean flag
) {
    IRONode* fnode;
    IROLinear* scannd;
    IRONodes nodes;
    Boolean result;

    IRO_FNodeWorkStack_Init(&nodes);

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        fnode->marked = 0;
    }
    AddNodeAndSuccessorsRecursively(&nodes, fnode1, fnode2);

    while (IRO_FNodeWorkStack_Count(&nodes)) {
        fnode = IRO_NodeTable[IRO_FNodeWorkStack_Remove(&nodes)];
        if (fnode) {
            if (!Bv_IsBitSet(fnode1->index, fnode->dom)) {
                result = 0;
                goto done;
            }

            for (scannd = fnode->first; scannd && scannd != fnode->last->next;
                 scannd = scannd->next) {
                if (!nd || scannd != nd) {
                    if (!flag || (scannd->type != IROLinearReturn)) {
                        if (fnode->numpred == 2 && scannd->type == IROLinearLabel) {
                            RebuildPossibleCondExpression(fnode);
                        }

                        if (scannd->type !=
                            IROLinearNop &&
                            scannd->type !=
                            IROLinearLabel &&
                            scannd->type !=
                            IROLinearGoto &&
                            !(scannd->flags & IROLF_Reffed)) {
                            result = 0;
                            goto done;
                        }
                    }
                }
            }
        }
    }

    result = 1;
done:
    IRO_FNodeWorkStack_Free(&nodes);
    return result;
}

static void RebuildPossibleCondExpression(IRONode* fnode) {
    IRONode* pred1;
    IRONode* pred2;
    IROLinear* leftAss;
    IROLinear* rightAss;
    IROLinear* r27;
    IROLinear* r13;
    IROLinear* var_58;
    IRONode* left;
    IRONode* right;
    IRONode* r24;
    ENode* r23;
    IRODef* r22;
    IRODef* r21;
    IRODef* def;
    UInt32 r20;
    UInt32 r19;
    Boolean r18;
    Boolean r17;
    UInt32 i;
    Boolean compareType;
    Type* var_60;
    IROLinear copy1;
    IROLinear copy2;
    IROLinear* tmp;
    IROUse* use;
    Object* obj;
    IRONode* r30;

    if (!fnode || fnode->numpred != 2 || !fnode->first || fnode->first->type != IROLinearLabel) {
        return;
    }

    pred1 = IRO_NodeTable[fnode->pred[0]];
    pred2 = IRO_NodeTable[fnode->pred[1]];
    if (!pred1 || !pred2 || pred1->numsucc != 1 || pred2->numsucc != 1) {
        return;
    }

    r24 = NULL;
    for (i = 0; i < IRO_NumNodes; i++) {
        if (Bv_IsBitSet(i, fnode->dom) &&
            (r24 = IRO_NodeTable[i]) &&
            r24->last &&
            (r24->last->type == IROLinearIf || r24->last->type == IROLinearIfNot) &&
            r24->last->u.label.label &&
            r24->numsucc == 2) {
            if (Bv_IsBitSet(r24->succ[0], pred1->dom) &&
                !Bv_IsBitSet(r24->succ[0], pred2->dom) &&
                Bv_IsBitSet(r24->succ[1], pred2->dom) &&
                !Bv_IsBitSet(r24->succ[1], pred1->dom)) {
                break;
            }

            if (Bv_IsBitSet(r24->succ[0], pred2->dom) &&
                !Bv_IsBitSet(r24->succ[0], pred1->dom) &&
                Bv_IsBitSet(r24->succ[1], pred1->dom) &&
                !Bv_IsBitSet(r24->succ[1], pred2->dom)) {
                break;
            }
        }
    }

    if (i >= IRO_NumNodes) {
        return;
    }

    r30 = IRO_NodeTable[r24->succ[0]];
    if (r30 &&
        r30->numpred ==
        1 &&
        r30->first &&
        r30->first->type ==
        IROLinearLabel &&
        r30->first->u.label.label == r24->last->u.label.label) {
        (void)0;
    } else {
        r30 = IRO_NodeTable[r24->succ[1]];
        if (r30 &&
            r30->numpred ==
            1 &&
            r30->first &&
            r30->first->type ==
            IROLinearLabel &&
            r30->first->u.label.label == r24->last->u.label.label) {
        } else {
            return;
        }
    }

    if (Bv_IsBitSet(r30->index, pred1->dom)) {
        left = pred2;
        right = pred1;
    } else {
        left = pred1;
        right = pred2;
    }

    leftAss = FindLastDiadicTopLevelAssignmentInFlowgraphNode(left);
    if (!leftAss ||
        leftAss->type !=
        IROLinearOp2Arg ||
        !leftAss->u.diadic.left ||
        leftAss->u.diadic.left->type !=
        IROLinearOp1Arg ||
        leftAss->u.diadic.left->nodetype !=
        EINDIRECT ||
        !leftAss->u.diadic.left->u.monadic ||
        leftAss->u.diadic.left->u.monadic->type !=
        IROLinearOperand ||
        !(r23 = leftAss->u.diadic.left->u.monadic->u.node) ||
        !r23->rtype ||
        !IS_TYPE_POINTER_ONLY(r23->rtype) ||
        !r23->data.objref ||
        is_volatile_object(r23->data.objref) ||
        r23->data.objref->datatype != DLOCAL) {
        r23 = NULL;
    }

    if (r23) {
        rightAss = FindLastDiadicTopLevelAssignmentInFlowgraphNode(right);
        if (!rightAss ||
            rightAss->type !=
            IROLinearOp2Arg ||
            !rightAss->u.diadic.left ||
            rightAss->u.diadic.left->type !=
            IROLinearOp1Arg ||
            rightAss->u.diadic.left->nodetype !=
            EINDIRECT ||
            !rightAss->u.diadic.left->u.monadic ||
            rightAss->u.diadic.left->u.monadic->type !=
            IROLinearOperand ||
            !rightAss->u.diadic.left->u.monadic->u.node ||
            !rightAss->u.diadic.left->u.monadic->u.node->rtype ||
            !IS_TYPE_POINTER_ONLY(rightAss->u.diadic.left->u.monadic->u.node->rtype) ||
            rightAss->u.diadic.left->u.monadic->u.node->type !=
            EOBJREF ||
            rightAss->u.diadic.left->u.monadic->u.node->data.objref !=
            r23->data.objref ||
            rightAss->rtype != leftAss->rtype) {
            r23 = NULL;
        }
    }

    if (r23) {
        if (!CheckThenOrElseBranch(r24->nextnode, left, leftAss, 0) ||
            !CheckThenOrElseBranch(r30, right, rightAss, 0)) {
            return;
        }

        r20 = r19 = 0;
        r17 = r18 = 0;
        if (!r23->data.objref->varptr ||
            !r23->data.objref->varptr->uses ||
            !r23->data.objref->varptr->defs) {
            r23 = NULL;
        } else {
            r22 = r21 = NULL;
            for (def = r23->data.objref->varptr->defs; def && (!r22 || !r21); def = def->varnext) {
                if (def->useCount && def->linear == leftAss) {
                    r22 = def;
                } else if (def->useCount && def->linear == rightAss) {
                    r21 = def;
                }
            }

            if (!r22 || !r21) {
                r23 = NULL;
            } else {
                for (use = r23->data.objref->varptr->uses; r23 && use; use = use->varnext) {
                    if (use->reachingDefCount) {
                        r20++;
                        if (Bv_IsBitSet(r22->index, use->reachingDefs) &&
                            Bv_IsBitSet(r21->index, use->reachingDefs)) {
                            for (i = 0; r23 && i < use->reachingDefs->size; i++) {
                                if (i !=
                                    r22->index &&
                                    i !=
                                    r21->index &&
                                    Bv_IsBitSet(i, use->reachingDefs)) {
                                    r23 = NULL;
                                }
                            }

                            if (r23 && !Bv_IsBitSet(fnode->index, use->node->dom)) {
                                r23 = NULL;
                            }

                            if (r23) {
                                if (RewriteUse(use, r23, rightAss, 0)) {
                                    r19++;
                                } else {
                                    r23 = NULL;
                                }
                            }

                            if (r23 && !r17) {
                                r17 = HasSideEffectsBeforeUse(
                                    fnode, fnode, use->node, use->linear, 1
                                );
                            }

                            if (r23 &&
                                !r18 &&
                                (IRO_HasSideEffect(r24->last->u.label.cond) ||
                                 IRO_HasSideEffect(leftAss->u.diadic.right) ||
                                 IRO_HasSideEffect(rightAss->u.diadic.right))) {
                                r18 = 1;
                            }
                        } else if (Bv_IsBitSet(r22->index, use->reachingDefs) ||
                                   Bv_IsBitSet(r21->index, use->reachingDefs)) {
                            r23 = NULL;
                        }
                    }
                }
            }
        }
    }

    if (!r23) {
        return;
    }

    compareType = r24->last->type == IROLinearIf;
    r27 = r24->last->u.label.cond;
    r13 = leftAss->u.diadic.right;
    var_58 = rightAss->u.diadic.right;
    var_60 = rightAss->rtype;
    memcpy(&copy1, r13, sizeof(IROLinear));
    memcpy(&copy2, rightAss, sizeof(IROLinear));

    r24->last->type = IROLinearNop;
    r24->last->expr = NULL;
    r30->first->type = IROLinearNop;
    r30->first->expr = NULL;
    if (right->last->type == IROLinearGoto) {
        right->last->type = IROLinearNop;
        right->last->expr = NULL;
    }

    if (r23) {
        IRO_NopOut(leftAss->u.diadic.left);
        leftAss->type = IROLinearNop;
        leftAss->expr = NULL;
    }

    memcpy(rightAss, &copy1, sizeof(IROLinear));
    rightAss->type = IROLinearOp3Arg;
    rightAss->nodetype = ECOND;
    rightAss->index = copy2.index;
    rightAss->u.args3.a = r27;
    rightAss->rtype = var_60;
    rightAss->next = copy2.next;
    if (compareType) {
        rightAss->u.args3.b = var_58;
        rightAss->u.args3.c = r13;
    } else {
        rightAss->u.args3.b = r13;
        rightAss->u.args3.c = var_58;
    }
    rightAss->flags |= IROLF_Reffed;

    if (r19 == 1 && !r17 && !r18 && !r23->data.objref->varptr->noregister) {
        for (use = r23->data.objref->varptr->uses; use; use = use->varnext) {
            if (use->reachingDefCount &&
                Bv_IsBitSet(r22->index, use->reachingDefs) &&
                Bv_IsBitSet(r21->index, use->reachingDefs)) {
                RewriteUse(use, r23, rightAss, 1);
                if ((r18 = (use->linear->flags & IROLF_4000) && IRO_HasSideEffect(r27))) {
                    tmp = IRO_NewLinear(IROLinearOperand);
                    memcpy(tmp, copy2.u.diadic.left->u.monadic, sizeof(IROLinear));
                    tmp->index = IRO_NumLinear++;
                    tmp->rtype = r27->rtype;
                    if (r20 == 1) {
                        tmp->u.node->data.objref->type = r27->rtype;
                        tmp->u.node->rtype = CDecl_NewPointerType(r27->rtype);
                    } else {
                        obj = create_temp_object(tmp->rtype);
                        IRO_FindVar(obj, 1, 1);
                        tmp->u.node = create_objectrefnode(obj);
                    }
                    IRO_PasteAfter(tmp, tmp, r27);

                    tmp = IRO_NewLinear(IROLinearOp1Arg);
                    memcpy(tmp, copy2.u.diadic.left, sizeof(IROLinear));
                    tmp->index = IRO_NumLinear++;
                    tmp->u.monadic = r27->next;
                    tmp->rtype = r27->rtype;
                    IRO_PasteAfter(tmp, tmp, r27->next);

                    tmp = IRO_NewLinear(IROLinearOp2Arg);
                    memcpy(tmp, &copy2, sizeof(IROLinear));
                    tmp->index = IRO_NumLinear++;
                    tmp->u.diadic.right = r27;
                    tmp->u.diadic.left = r27->next->next;
                    tmp->rtype = r27->rtype;
                    IRO_PasteAfter(tmp, tmp, r27->next->next);

                    if (r20 != 1) {
                        tmp = copy2.u.diadic.left->u.monadic;
                        tmp->u.node = create_objectrefnode(obj);
                    }

                    rightAss->u.args3.a = copy2.u.diadic.left;
                    rightAss->u.args3.a->rtype = r27->rtype;

                    if (r20 == 1) {
                        r21->linear = r27->next->next;
                        if (Bv_IsBitSet(r21->index, right->out)) {
                            Bv_ClearBit(r21->index, right->out);
                            Bv_SetBit(r21->index, r24->out);
                        }
                        if (Bv_IsBitSet(r21->index, right->gen)) {
                            Bv_ClearBit(r21->index, right->gen);
                            Bv_SetBit(r21->index, r24->gen);
                        }
                        if (Bv_IsBitSet(r21->index, right->kill)) {
                            Bv_ClearBit(r21->index, right->kill);
                            Bv_SetBit(r21->index, r24->kill);
                        }
                    }
                }
            }
        }

        if (!r18) {
            if (r20 == 1 && r23->data.objref->u.var.info) {
                r23->data.objref->u.var.info->usage = 0;
                r23->data.objref->u.var.info->used = 0;
            }
            r22->useCount = 0;
            IRO_NopOut(copy2.u.diadic.left);
            r21->useCount = 0;
        }
    } else {
        tmp = IRO_NewLinear(IROLinearOp2Arg);
        memcpy(tmp, &copy2, sizeof(IROLinear));
        tmp->index = IRO_NumLinear++;
        tmp->u.diadic.right = rightAss;
        IRO_PasteAfter(tmp, tmp, fnode->first);
        r22->useCount = 0;
        r21->linear = fnode->first;
        if (Bv_IsBitSet(r21->index, right->out)) {
            Bv_ClearBit(r21->index, right->out);
            Bv_SetBit(r21->index, fnode->out);
        }
        if (Bv_IsBitSet(r21->index, right->gen)) {
            Bv_ClearBit(r21->index, right->gen);
            Bv_SetBit(r21->index, fnode->gen);
        }
        if (Bv_IsBitSet(r21->index, right->kill)) {
            Bv_ClearBit(r21->index, right->kill);
            Bv_SetBit(r21->index, fnode->kill);
        }
    }
}

static void RebuildPossibleReturnCondExpression(IRONode* fnode) {
    IRONode* succ2;
    IROLinear* node1;
    IROLinear* node2;
    IROLinear* tmp1;
    IRONode* succ1;
    IROLinear* node3;
    IRONode* r27;
    Boolean isIf;
    Type* type;
    IROList list;
    IROLinear* last;
    IROLinear* tmp2;
    IROLinear* tmp;

    if (!fnode ||
        fnode->numsucc !=
        2 ||
        !(last = fnode->last) ||
        (last->type != IROLinearIf && last->type != IROLinearIfNot)) {
        return;
    }

    succ1 = IRO_NodeTable[fnode->succ[0]];
    succ2 = IRO_NodeTable[fnode->succ[1]];
    if (!succ1 ||
        !succ2 ||
        succ1->numsucc !=
        0 ||
        succ1->numpred !=
        1 ||
        succ2->numsucc !=
        0 ||
        succ2->numpred != 1) {
        return;
    }

    if (succ1->first &&
        succ1->first->type ==
        IROLinearLabel &&
        succ1->first->u.label.label == fnode->last->u.label.label) {
        r27 = succ1;
    } else if (succ2->first &&
               succ2->first->type ==
               IROLinearLabel &&
               succ2->first->u.label.label == fnode->last->u.label.label) {
        r27 = succ2;
    } else {
        return;
    }

    if (r27->numpred != 1) {
        return;
    }

    if (!CheckThenOrElseBranch(fnode->nextnode, fnode->nextnode, NULL, 1) ||
        !CheckThenOrElseBranch(r27, r27, NULL, 1)) {
        return;
    }

    if (!fnode->nextnode->last ||
        fnode->nextnode->last->type !=
        IROLinearReturn ||
        !fnode->nextnode->last->u.monadic ||
        !r27->last ||
        r27->last->type !=
        IROLinearReturn ||
        !r27->last->u.monadic ||
        !IRO_TypesEqual(fnode->nextnode->last->u.monadic->rtype, r27->last->u.monadic->rtype)) {
        return;
    }

    isIf = fnode->last->type == IROLinearIf;
    node1 = fnode->last->u.label.cond;
    node2 = fnode->nextnode->last->u.monadic;
    node3 = r27->last->u.monadic;
    type = node2->rtype;

    fnode->last->type = IROLinearNop;
    fnode->last->expr = NULL;
    r27->last->type = IROLinearNop;
    r27->last->expr = NULL;
    r27->first->type = IROLinearNop;
    r27->first->expr = NULL;

    if (IRO_IsIntConstant(node2) &&
        IRO_IsIntConstant(node3) &&
        ((IRO_IsConstantOne(node2) && IRO_IsConstantZero(node3)) ||
         (IRO_IsConstantZero(node2) && IRO_IsConstantOne(node3)))) {
        if (!(
                node1->type == IROLinearOp1Arg && node1->nodetype == ELOGNOT && node1->rtype == type
            )) {
            tmp1 = IRO_NewLinear(IROLinearOp1Arg);
            memcpy(tmp1, node2, sizeof(IROLinear));
            tmp1->type = IROLinearOp1Arg;
            tmp1->nodetype = ELOGNOT;
            tmp1->index = IRO_NumLinear++;
            tmp1->u.monadic = node1;

            tmp1->next = tmp2 = IRO_NewLinear(IROLinearOp1Arg);
            memcpy(tmp2, node2, sizeof(IROLinear));
            tmp2->type = IROLinearOp1Arg;
            tmp2->nodetype = ELOGNOT;
            tmp2->index = IRO_NumLinear++;
            tmp2->u.monadic = tmp1;

            IRO_PasteAfter(tmp1, tmp2, node1);
            node1 = tmp2;
        }

        if ((IRO_IsConstantZero(node2) && !isIf) || (IRO_IsConstantOne(node2) && isIf)) {
            if (node1->type ==
                IROLinearOp1Arg &&
                node1->nodetype ==
                ELOGNOT &&
                node1->rtype == node1->u.monadic->rtype) {
                tmp = node1;
                node1 = node1->u.monadic;
                tmp->type = IROLinearNop;
                tmp->expr = NULL;
            } else {
                tmp = IRO_NewLinear(IROLinearOp1Arg);
                memcpy(tmp, node2, sizeof(IROLinear));
                tmp->type = IROLinearOp1Arg;
                tmp->nodetype = ELOGNOT;
                tmp->index = IRO_NumLinear++;
                tmp->u.monadic = node1;
                IRO_PasteAfter(tmp, tmp, node1);
                node1 = tmp;
            }
        }

        node2->type = IROLinearNop;
        node2->expr = NULL;
        node3->type = IROLinearNop;
        node3->expr = NULL;
        fnode->nextnode->last->u.monadic = node1;
    } else {
        IRO_InitList(&list);
        tmp1 = IRO_DuplicateExpr(node3, &list);
        IRO_NopOut(node3);
        IRO_Paste(list.head, list.tail, fnode->nextnode->last);

        tmp2 = IRO_NewLinear(IROLinearOp3Arg);
        memcpy(tmp2, node2, sizeof(IROLinear));
        tmp2->type = IROLinearOp3Arg;
        tmp2->nodetype = ECOND;
        tmp2->index = IRO_NumLinear++;
        tmp2->u.args3.a = node1;
        tmp2->rtype = type;
        IRO_Paste(tmp2, tmp2, fnode->nextnode->last);

        if (isIf) {
            tmp2->u.args3.b = tmp1;
            tmp2->u.args3.c = node2;
        } else {
            tmp2->u.args3.b = node2;
            tmp2->u.args3.c = tmp1;
        }

        fnode->nextnode->last->u.monadic = tmp2;
    }
}

static void RebuildCondExpressions(void) {
    IRONode* fnode;
    IROLinear* nd;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        if (fnode->numpred == 2) {
            if ((nd = fnode->first) && nd->type == IROLinearLabel) {
                RebuildPossibleCondExpression(fnode);
            }
        } else if (fnode->numsucc == 2) {
            if ((nd = fnode->last) && (nd->type == IROLinearIf || nd->type == IROLinearIfNot)) {
                RebuildPossibleReturnCondExpression(fnode);
            }
        }
    }

    IRO_CheckForUserBreak();
}

static Boolean IsLogicalExpressionAssign(IROLinear* nd) {
    IROLinear* right;
    IROLinear* left;

    return (
        nd->type ==
        IROLinearOp2Arg &&
        nd->nodetype ==
        EASS &&
        (left = nd->u.diadic.left) &&
        (right = nd->u.diadic.right) &&
        right->type ==
        IROLinearOperand &&
        right->u.node &&
        right->u.node->type ==
        EINTCONST &&
        (CInt64_Equal(right->u.node->data.intval, cint64_one) ||
         CInt64_Equal(right->u.node->data.intval, cint64_zero)) &&
        left->type ==
        IROLinearOp1Arg &&
        left->nodetype ==
        EINDIRECT &&
        left->u.monadic &&
        IS_TYPE_POINTER_ONLY(left->u.monadic->rtype) &&
        left->u.monadic->type ==
        IROLinearOperand &&
        left->u.monadic->u.node &&
        left->u.monadic->u.node->type ==
        EOBJREF &&
        left->u.monadic->u.node->data.objref &&
        left->u.monadic->u.node->data.objref->datatype == DLOCAL
    );
}

static Boolean IsPossibleLogicalExpressionStart(IRONode* fnode, IROLinear* nd) {
    IROLinear* last;

    return fnode->numsucc ==
        2 &&
        (last = fnode->last) &&
        (last->type == IROLinearIf || last->type == IROLinearIfNot) &&
        IsLogicalExpressionAssign(nd);
}

static Boolean CheckForTopLevelExpressions(
    IRONode* fnode1, IRONode* fnode2, IRONode* fnode3, IROLinear* nd1, IROLinear* nd2, Boolean flag
) {
    IRONode* fnode;
    IROLinear* nd;
    UInt16 i;

    if (fnode1 != fnode3) {
        if (!Bv_IsBitSet(fnode2->index, fnode1->dom)) {
            return 0;
        }

        if (flag) {
            for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
                fnode->marked = 0;
            }
        } else if (fnode1->marked) {
            return 1;
        }
        fnode1->marked = 1;

        for (i = 0; i < fnode1->numsucc; i++) {
            if (!CheckForTopLevelExpressions(
                    IRO_NodeTable[fnode1->succ[i]],
                    fnode2,
                    fnode3,
                    IRO_NodeTable[fnode1->succ[i]]->first,
                    nd2,
                    0
                )) {
                return 0;
            }
        }
    }

    for (nd = nd1; nd && nd != fnode1->last->next; nd = nd->next) {
        if (nd == nd2) {
            return 1;
        }

        if (nd->type != IROLinearNop && nd->type != IROLinearLabel && !(nd->flags & IROLF_Reffed)) {
            return 0;
        }
    }

    return 1;
}

static void AddNodeAndSuccessorsRecursively(IRONodes* nodes, IRONode* fnode1, IRONode* fnode2) {
    UInt16 i;

    if (fnode1 && !fnode1->marked) {
        fnode1->marked = 1;
        IRO_FNodeWorkStack_Add(nodes, fnode1->index);
        if (fnode1 != fnode2) {
            for (i = 0; i < fnode1->numsucc; i++) {
                AddNodeAndSuccessorsRecursively(nodes, IRO_NodeTable[fnode1->succ[i]], fnode2);
            }
        }
    }
}

// TODO: an unproven copy of the parameter, kept for AddNodeAndSuccessorsRecursively (compiled next).
static Boolean RebuildPossibleLogicalExpression(IRONode* start, IROLinear* nd) {
    IRONode* fnode = start;
    IRONode* var_6C;
    IROLinear* newlin;
    IROUse* use;
    IROLinear* nd2;
    IROLinear* left;
    IROLinear* right;
    IRONode* r25;
    IRONode* r24;
    UInt8 compareType;
    ENode* objnode;
    IRODef* def1;
    IRODef* def2;
    UInt32 useCount;
    UInt32 rewrittenUseCount;
    Boolean sideEffectFlag;
    CLabel* somelab;
    IRONode* var_68;
    CInt64 compareVal;
    Boolean killedFlag;
    BitVector* bv1;
    BitVector* bv2;
    IROLinear* father;
    IROLinear* iter;
    IRODef* def;
    UInt32 i;

    objnode = nd->u.diadic.left->u.monadic->u.node;
    if (!objnode->data.objref->varptr) {
        return 0;
    }

    if (CInt64_Equal(nd->u.diadic.right->u.node->data.intval, cint64_zero)) {
        compareType = IROLinearIfNot;
        compareVal = cint64_one;
    } else {
        compareType = IROLinearIf;
        compareVal = cint64_zero;
    }

    var_68 = NULL;
    def1 = NULL;
    for (def = objnode->data.objref->varptr->defs; def && !def1; def = def->varnext) {
        if (def->useCount && def->linear == nd) {
            def1 = def;
        }
    }

    if (def1) {
        for (use = objnode->data.objref->varptr->uses; use; use = use->varnext) {
            if (use->reachingDefCount == 2 && Bv_IsBitSet(def1->index, use->reachingDefs)) {
                if (!var_68) {
                    var_68 = use->node;
                } else if (Bv_IsBitSet(use->node->index, var_68->dom)) {
                    var_68 = use->node;
                }
            }
        }

        if (var_68) {
            for (use = objnode->data.objref->varptr->uses; use; use = use->varnext) {
                if (use->node !=
                    var_68 &&
                    use->reachingDefCount ==
                    2 &&
                    Bv_IsBitSet(def1->index, use->reachingDefs) &&
                    !Bv_IsBitSet(var_68->index, use->node->dom)) {
                    var_68 = NULL;
                    break;
                }
            }
        }
    }

    if (!var_68 ||
        !var_68->first ||
        var_68->first->type !=
        IROLinearLabel ||
        !(somelab = var_68->first->u.label.label) ||
        var_68->numpred != 3) {
        return 0;
    }

    r25 = IRO_NodeTable[var_68->pred[0]];
    if (Bv_IsBitSet(IRO_NodeTable[var_68->pred[1]]->index, r25->dom)) {
        r25 = IRO_NodeTable[var_68->pred[1]];
    } else if (!Bv_IsBitSet(r25->index, IRO_NodeTable[var_68->pred[1]]->dom)) {
        return 0;
    }

    if (Bv_IsBitSet(IRO_NodeTable[var_68->pred[2]]->index, r25->dom)) {
        r25 = IRO_NodeTable[var_68->pred[2]];
    } else if (!Bv_IsBitSet(r25->index, IRO_NodeTable[var_68->pred[2]]->dom)) {
        return 0;
    }

    if (!r25 ||
        !(left = r25->last) ||
        left->type !=
        compareType ||
        left->u.label.label != somelab) {
        return 0;
    }

    if (!FindFlowgraphNodeThatStartsWithLabel(left->u.label.label, &var_68, &var_6C) ||
        !var_6C ||
        var_6C->numpred !=
        1 ||
        !(r24 = IRO_NodeTable[var_6C->pred[0]]) ||
        !(right = r24->last) ||
        right->type !=
        compareType ||
        right->u.label.label !=
        left->u.label.label ||
        !(nd2 = FindLastDiadicTopLevelAssignmentInFlowgraphNode(var_6C)) ||
        !IsLogicalExpressionAssign(nd2) ||
        nd2->u.diadic.left->u.monadic->u.node->data.objref !=
        objnode->data.objref ||
        !CInt64_Equal(compareVal, nd2->u.diadic.right->u.node->data.intval) ||
        nd2->rtype != nd->rtype) {
        return 0;
    }

    if (!CheckForTopLevelExpressions(fnode, fnode, r25, nd->next, left, 1) ||
        !CheckForTopLevelExpressions(
            r25->nextnode, r25->nextnode, r24, r25->nextnode->first, right, 1
        ) ||
        !CheckForTopLevelExpressions(r24->nextnode, r24->nextnode, var_6C, right->next, nd2, 1) ||
        !CheckForTopLevelExpressions(var_6C, var_6C, var_6C, nd2->next, var_68->first, 1)) {
        return 0;
    }

    useCount = rewrittenUseCount = 0;
    sideEffectFlag = killedFlag = 0;
    if (!objnode->data.objref->varptr ||
        !(objnode->data.objref->varptr->uses) ||
        !(objnode->data.objref->varptr->defs)) {
        objnode = NULL;
    } else {
        def1 = def2 = NULL;
        for (def = objnode->data.objref->varptr->defs; def && (!def1 || !def2);
             def = def->varnext) {
            if (def->useCount && def->linear == nd) {
                def1 = def;
            } else if (def->useCount && def->linear == nd2) {
                def2 = def;
            }
        }

        if (!def1 || !def2) {
            objnode = NULL;
        } else {
            for (use = objnode->data.objref->varptr->uses; objnode && use; use = use->varnext) {
                if (use->reachingDefCount) {
                    useCount++;
                    if (Bv_IsBitSet(def1->index, use->reachingDefs) &&
                        Bv_IsBitSet(def2->index, use->reachingDefs)) {
                        for (i = 0; objnode && i < use->reachingDefs->size; i++) {
                            if (i !=
                                def1->index &&
                                i !=
                                def2->index &&
                                Bv_IsBitSet(i, use->reachingDefs)) {
                                objnode = NULL;
                            }
                        }

                        if (objnode && !Bv_IsBitSet(var_68->index, use->node->dom)) {
                            objnode = NULL;
                        }

                        if (objnode) {
                            if (RewriteUse(use, objnode, nd2, 0)) {
                                rewrittenUseCount++;
                            } else {
                                objnode = NULL;
                            }
                        }

                        if (objnode) {
                            if (!sideEffectFlag) {
                                sideEffectFlag = HasSideEffectsBeforeUse(
                                    var_68, var_68, use->node, use->linear, 1
                                );
                            }

                            if (!killedFlag &&
                                (IRO_HasSideEffect(left->u.label.cond) ||
                                 IRO_HasSideEffect(right->u.label.cond))) {
                                iter = use->linear;
                                if ((father = IRO_LocateFather(use->linear))) {
                                    Bv_AllocVector(&bv1, IRO_NumVars + 1);
                                    Bv_AllocVector(&bv2, IRO_NumVars + 1);
                                    Bv_AllocVector(&IRO_VarKills, IRO_NumVars + 1);
                                    IRO_WalkTree(left->u.label.cond, Regen_GetExprKills);
                                    IRO_WalkTree(right->u.label.cond, Regen_GetExprKills);
                                    Bv_Or(IRO_VarKills, bv1);
                                    while (father && !killedFlag) {
                                        CheckUnorderedRegionsForSideEffectsAndUses(
                                            father, iter, bv1, bv2, &killedFlag
                                        );
                                        iter = father;
                                        father = IRO_LocateFather(father);
                                    }
                                }

                                if (!killedFlag) {
                                    killedFlag = UsesAKilledVarBeforeUse(
                                        var_68, var_68, use->node, use->linear, bv1, 1
                                    );
                                }
                            }
                        }
                    } else if (Bv_IsBitSet(def1->index, use->reachingDefs) ||
                               Bv_IsBitSet(def2->index, use->reachingDefs)) {
                        objnode = NULL;
                    }
                }
            }
        }
    }

    if (!objnode) {
        return 0;
    }

    left->type = right->type = IROLinearNop;
    left->expr = right->expr = NULL;

    IRO_NopOut(nd);

    newlin = IRO_NewLinear(IROLinearOp2Arg);
    memcpy(newlin, nd2, sizeof(IROLinear));
    newlin->nodetype = (compareType == IROLinearIf) ? ELOR : ELAND;
    newlin->index = IRO_NumLinear++;
    newlin->u.diadic.left = left->u.label.cond;
    newlin->u.diadic.right = right->u.label.cond;
    newlin->flags |= IROLF_Reffed;
    IRO_Paste(newlin, newlin, nd2);

    if (rewrittenUseCount ==
        1 &&
        !sideEffectFlag &&
        !killedFlag &&
        !objnode->data.objref->varptr->noregister) {
        for (use = objnode->data.objref->varptr->uses; use; use = use->varnext) {
            if (use->reachingDefCount &&
                Bv_IsBitSet(def1->index, use->reachingDefs) &&
                Bv_IsBitSet(def2->index, use->reachingDefs)) {
                RewriteUse(use, objnode, newlin, 1);
            }
        }

        if (useCount == 1 && objnode->data.objref->u.var.info) {
            objnode->data.objref->u.var.info->usage = 0;
            objnode->data.objref->u.var.info->used = 0;
        }
        IRO_NopOut(nd2);
        def1->useCount = 0;
        def2->useCount = 0;
    } else {
        IRO_NopOut(nd2->u.diadic.right);
        nd2->u.diadic.right = newlin;
        def1->useCount = 0;
    }

    if (r24->numpred ==
        1 &&
        IRO_NodeTable[r24->pred[0]] ==
        r25 &&
        IRO_MergeFlowGraphNodes(r25, r24)) {
        r24 = r25;
    }
    if (var_6C->numpred ==
        1 &&
        IRO_NodeTable[var_6C->pred[0]] ==
        r24 &&
        IRO_MergeFlowGraphNodes(r24, var_6C)) {
        var_6C = r24;
    }
    if (var_68->numpred == 1 && r24 == r25 && var_6C == r24) {
        IRO_MergeFlowGraphNodes(var_6C, var_68);
    }
    return 1;
}

static void RebuildLogicalExpressions(void) {
    IRONode* fnode;
    IROLinear* nd;
    Boolean flag;

    do {
        flag = 0;
        for (fnode = IRO_FirstNode; fnode && fnode->last; fnode = fnode->nextnode) {
            for (nd = fnode->first; nd && nd != fnode->last->next; nd = nd->next) {
                if (IsPossibleLogicalExpressionStart(fnode, nd) &&
                    RebuildPossibleLogicalExpression(fnode, nd)) {
                    flag = 1;
                }
            }
        }
    } while (flag);

    IRO_CheckForUserBreak();
}

static Boolean IsPossibleCondAssStart(IRONode* fnode) {
    IROLinear* last;

    return (fnode->numsucc == 2) &&
        (last = fnode->last) &&
        (last->type == IROLinearIf || last->type == IROLinearIfNot);
}

static Type* logicalType(void) {
    if (copts.cplusplus && copts.booltruefalse) {
        return TYPE(&stbool);
    } else {
        return TYPE(&stsignedint);
    }
}

static Boolean GeneratePossibleCondAss(IRONode* fnode) {
    IROLinear* r24;
    IROLinear* r16;
    IRONode* node1;
    IROLinear* ass;
    IRONode* node2;
    Object* obj;
    IROLinear* r19;
    IROLinear* right2;
    IROLinear* r20;
    Boolean r19flag;
    IROLinear* r18nd;
    IRONode* r29;
    IROLinear* r31;
    IROLinear* left2;
    IROLinear* cond;
    Boolean r28;
    BitVector* saveVarKills;
    IROList list;
    Boolean r23;
    IROLinear* r14;
    IROLinear* last;
    IRONode* succ;
    IROLinear* node2last;
    UInt16 succ0;
    IRONode* succnode;
    Boolean r18;
    IROLinear* r15;
    IROLinear* left;

    if (!fnode ||
        fnode->numsucc !=
        2 ||
        !(last = fnode->last) ||
        (last->type != IROLinearIf && last->type != IROLinearIfNot)) {
        return 0;
    }

    node1 = node2 = IRO_NodeTable[fnode->succ[0]];
    if (node1 &&
        node1->first &&
        node1->first->type ==
        IROLinearLabel &&
        node1->first->u.label.label == fnode->last->u.label.label) {
        node2 = IRO_NodeTable[fnode->succ[1]];
    } else {
        succ = node1 = IRO_NodeTable[fnode->succ[1]];
        if (!succ ||
            !succ->first ||
            succ->first->type !=
            IROLinearLabel ||
            succ->first->u.label.label != fnode->last->u.label.label) {
            return 0;
        }
    }

    if (!node2 ||
        node2->numpred !=
        1 ||
        node2->numsucc !=
        1 ||
        (node2last = node2->last)->next != node1->first) {
        return 0;
    }

    succ0 = node2->succ[0];
    if ((succnode = IRO_NodeTable[succ0]) == node1 && Bv_IsBitSet(fnode->index, node1->dom)) {
        r28 = 0;
    } else if (node1->numpred ==
               1 &&
               node1->numsucc ==
               1 &&
               succ0 ==
               node1->succ[0] &&
               (r29 = IRO_NodeTable[succ0]) &&
               Bv_IsBitSet(fnode->index, r29->dom) &&
               node1->last->next == r29->first) {
        r28 = 1;
    } else {
        return 0;
    }

    r24 = NULL;
    if (node2last) {
        for (r19 = node2->first; r19 && r19 != node2->last->next; r19 = r19->next) {
            if (IRO_IsAssignment(r19) && !(r19->flags & IROLF_Reffed) && !r24) {
                r24 = r19;
            } else if (r19->type !=
                       IROLinearNop &&
                       r19->type !=
                       IROLinearLabel &&
                       r19->type !=
                       IROLinearGoto &&
                       !(r19->flags & IROLF_Reffed)) {
                return 0;
            }
        }
    }

    if (!r24 ||
        (r24->nodetype != EASS && !IRO_TransformSelfAssignmentToAssignment(r24)) ||
        !r24->u.diadic.left ||
        r24->u.diadic.left->type !=
        IROLinearOp1Arg ||
        r24->u.diadic.left->nodetype !=
        EINDIRECT ||
        !r24->u.diadic.left->u.monadic ||
        r24->u.diadic.left->u.monadic->type !=
        IROLinearOperand ||
        !r24->u.diadic.left->u.monadic->u.node ||
        !r24->u.diadic.left->u.monadic->u.node->rtype ||
        !IS_TYPE_POINTER_ONLY(r24->u.diadic.left->u.monadic->u.node->rtype) ||
        !r24->u.diadic.left->u.monadic->u.node->data.objref) {
        return 0;
    }

    if (r28) {
        r31 = NULL;
        if (node1->last) {
            for (r19 = node1->first; r19 && r19 != node1->last->next; r19 = r19->next) {
                if (IRO_IsAssignment(r19) && !(r19->flags & IROLF_Reffed) && !r31) {
                    r31 = r19;
                } else if (r19->type !=
                           IROLinearNop &&
                           r19->type !=
                           IROLinearLabel &&
                           r19->type !=
                           IROLinearGoto &&
                           !(r19->flags & IROLF_Reffed)) {
                    return 0;
                }
            }
        }

        if (!r31 ||
            (r31->nodetype != EASS && !IRO_TransformSelfAssignmentToAssignment(r31)) ||
            !(left = r31->u.diadic.left) ||
            left->type !=
            IROLinearOp1Arg ||
            left->nodetype !=
            EINDIRECT ||
            !left->u.monadic ||
            left->u.monadic->type !=
            IROLinearOperand ||
            !left->u.monadic->u.node ||
            !left->u.monadic->u.node->rtype ||
            !IS_TYPE_POINTER_ONLY(left->u.monadic->u.node->rtype) ||
            !left->u.monadic->u.node->data.objref) {
            return 0;
        }

        r18 = IRO_ExprsSame(r24->u.diadic.left, r31->u.diadic.left);
    }

    r23 = fnode->last->type == IROLinearIf;
    r20 = fnode->last->u.label.cond;
    r19flag = r28 && copts.commonsubs && IRO_IsSubableExpression(r20);

    if (r19flag) {
        IRO_FindDepends(r20);
        r19flag = !IRO_NotSubable;
    }

    if (r28) {
        if (r18) {
            r19flag = 0;
        } else if (IRO_HasSideEffect(r20)) {
            r19flag = 1;
        } else {
            Bv_AllocVector(&IRO_VarKills, IRO_NumVars + 1);
            IRO_WalkTree(r20, Regen_GetExprUses);
            saveVarKills = IRO_VarKills;
            Bv_AllocVector(&IRO_VarKills, IRO_NumVars + 1);
            IRO_WalkTree(r24, Regen_GetExprKills);
            if (Bv_BitsInCommon(IRO_VarKills, saveVarKills)) {
                r19flag = 1;
            }
        }

        if (r19flag && !copts.commonsubs) {
            return 0;
        }
    }

    if (r19flag) {
        IRO_InitList(&list);
        obj = create_temp_object(r20->rtype);
        IRO_FindVar(obj, 1, 1);
        ass = IRO_NewLinear(IROLinearOp2Arg);
        ass->u.diadic.left = IRO_TempReference(obj, &list);
        ass->rtype = r20->rtype;
        ass->nodetype = EASS;
        ass->index = ++IRO_NumLinear;
        ass->u.diadic.left->flags |= IROLF_Ind | IROLF_Assigned;
        ass->u.diadic.left->u.monadic->flags |= IROLF_Ind | IROLF_Assigned;
        ass->u.diadic.right = r20;
        IRO_AddToList(ass, &list);
        IRO_PasteAfter(list.head, list.tail, r20);
    }

    r16 = r24->u.diadic.left;
    fnode->last->type = IROLinearNop;
    r15 = r24->u.diadic.right;
    fnode->last->expr = NULL;
    if (node2->last->type == IROLinearGoto) {
        node2->last->type = IROLinearNop;
        node2->last->expr = NULL;
    }
    if (r28 || node1->numpred == 2) {
        node1->first->type = IROLinearNop;
        node1->first->expr = NULL;
    }

    if (r28 && r18) {
        if (node1->last->type == IROLinearGoto) {
            node1->last->type = IROLinearNop;
            node1->last->expr = NULL;
        }
        if (r29->numpred == 2) {
            r29->first->type = IROLinearNop;
            r29->first->expr = NULL;
        }

        cond = IRO_NewLinear(IROLinearOp3Arg);
        memcpy(cond, r24, sizeof(IROLinear));
        cond->type = IROLinearOp3Arg;
        cond->nodetype = ECOND;
        cond->index = IRO_NumLinear++;
        cond->rtype = r16->rtype;
        cond->flags |= IROLF_Reffed;
        if (r19flag) {
            IRO_InitList(&list);
            r20 = IRO_TempReference(obj, &list);
            IRO_Paste(list.head, list.tail, r31);
        }
        cond->u.args3.a = r20;

        if (r23) {
            cond->u.args3.b = r31->u.diadic.right;
            cond->u.args3.c = r15;
        } else {
            cond->u.args3.b = r15;
            cond->u.args3.c = r31->u.diadic.right;
        }

        r24->type = IROLinearNop;
        r24->expr = NULL;
        IRO_NopOut(r24->u.diadic.left);
        IRO_Paste(cond, cond, r31);
        r31->u.diadic.right = cond;
    } else {
        r24->type = IROLinearOp3Arg;
        r24->nodetype = ECONDASS;
        if (r19flag) {
            IRO_InitList(&list);
            r14 = IRO_TempReference(obj, &list);
            IRO_Paste(list.head, list.tail, r24);
        } else {
            r14 = r20;
        }
        cond = r14;
        if (r23) {
            r18nd = IRO_NewLinear(IROLinearOp1Arg);
            memcpy(r18nd, r14, sizeof(IROLinear));
            r18nd->type = IROLinearOp1Arg;
            r18nd->nodetype = ELOGNOT;
            r18nd->index = IRO_NumLinear++;
            r18nd->rtype = logicalType();
            r18nd->u.monadic = r14;
            r18nd->flags |= IROLF_Reffed;
            IRO_Paste(r18nd, r18nd, r24);
            r14 = r18nd;
        }
        r24->u.args3.a = r14;
        r24->u.args3.b = r16;
        r24->u.args3.c = r15;
        if (r28) {
            left2 = r31->u.diadic.left;
            right2 = r31->u.diadic.right;
            if (node1->last->type == IROLinearGoto) {
                node1->last->type = IROLinearNop;
                node1->last->expr = NULL;
            }
            if (r29->numpred == 2) {
                r29->first->type = IROLinearNop;
                r29->first->expr = NULL;
            }
            r31->type = IROLinearOp3Arg;
            r31->nodetype = ECONDASS;
            if (r19flag) {
                IRO_InitList(&list);
                cond = IRO_TempReference(obj, &list);
                IRO_Paste(list.head, list.tail, r31);
            } else {
                IRO_InitList(&list);
                cond = IRO_DuplicateExpr(cond, &list);
                IRO_Paste(list.head, list.tail, r31);
            }
            if (!r23) {
                r14 = IRO_NewLinear(IROLinearOp1Arg);
                memcpy(r14, cond, sizeof(IROLinear));
                r14->type = IROLinearOp1Arg;
                r14->nodetype = ELOGNOT;
                r14->index = IRO_NumLinear++;
                r14->rtype = logicalType();
                r14->u.monadic = cond;
                r14->flags |= IROLF_Reffed;
                IRO_Paste(r14, r14, r31);
                cond = r14;
            }
            r31->u.args3.a = cond;
            r31->u.args3.b = left2;
            r31->u.args3.c = right2;
        }
    }

    if (IRO_MergeFlowGraphNodes(fnode, node2)) {
        node2 = fnode;
    }
    if ((r28 || node1->numpred == 1) && IRO_MergeFlowGraphNodes(node2, node1)) {
        node1 = node2;
    }
    if (r28 && r29->numpred == 1) {
        IRO_MergeFlowGraphNodes(node1, r29);
    }
    return 1;
}

static void GenerateCondAssignments(void) {
    IRONode* fnode;

    fnode = IRO_FirstNode;
    while (fnode && fnode->last) {
        if (IsPossibleCondAssStart(fnode)) {
            if (!GeneratePossibleCondAss(fnode)) {
                fnode = fnode->nextnode;
            }
        } else {
            fnode = fnode->nextnode;
        }
    }

    IRO_CheckForUserBreak();
}

void IRO_RegenerateExpressions(void) {
    IRO_UpdateFlagsOnInts();
    IRO_ComputeSuccPred();
    IRO_ComputeDom();
    IRO_UseDef(0, 0);

    if (copts.optimizationlevel > 0) {
        IRO_Dump("Rebuilding ELORs and ELANDs\n");
        RebuildLogicalExpressions();
        IRO_DumpAfterPhase("RebuildLogicalExpressions", 0);
    }

    if (copts.optimizationlevel > 0) {
        IRO_Dump("Rebuilding ECONDs\n");
        RebuildCondExpressions();
        IRO_DumpAfterPhase("RebuildCondExpressions", 0);
    }

    if (copts.optimizationlevel > 0) {
        IRO_Dump("Generating ECONDASSes\n");
        GenerateCondAssignments();
        IRO_DumpAfterPhase("GenerateCondAssignments", 0);
    }
}
