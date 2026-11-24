#include "compiler/IroEmptyLoop.h"
#include "compiler/IroDump.h"
#include "compiler/IroCSE.h"
#include "compiler/IroEval.h"
#include "compiler/IroFlowgraph.h"
#include "compiler/IroJump.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroLoop.h"
#include "compiler/IroUtil.h"
#include "compiler/IroVars.h"
#include "compiler/BitVector.h"
#include "compiler/CError.h"
#include "compiler/CInt64.h"
#include "compiler/CParser.h"
#include "compiler/enode.h"
#include "compiler/objects.h"

static int EmptyLoop(IRONode* fnode);
static int CanRemoveRedundantLoop(IROLoop* loop);
static int CanRemoveRedundantLoop1(IROLoop* loop);
static int RedundantLoopCheck(IROLoop* loop);
static int CheckStepOverFlow1_EmptyLoop(IROLoop* loop, CInt64* iterCount, CInt64* finalValue);
static int CheckStepOverFlow2_EmptyLoop(IROLoop* loop, CInt64* iterCount, CInt64* finalValue);

static Boolean FindLoopNodes(IRONode* header) {
    IRONode* pred;
    UInt16 i;
    Boolean found = FALSE;

    for (i = 0; i < header->numpred; i++) {
        pred = IRO_NodeTable[header->pred[i]];
        if (Bv_IsBitSet(header->index, pred->dom)) {
            if (!found) {
                Bv_AllocVector(&InLoop, IRO_NumNodes + 1);
                Bv_Clear(InLoop);
                Bv_SetBit(header->index, InLoop);
            }
            found = TRUE;
            Bv_SetBit(pred->index, InLoop);
            if (pred != header) {
                AddPreds(pred);
            }
        }
    }
    return found;
}

void IRO_FindEmptyLoops(void) {
    IRONode* fnode;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        if (FindLoopNodes(fnode)) {
            IRO_Dump("IRO_FindEmptyLoops:Found loop with header %d\n", fnode->index);
            IRO_DumpBits("Loop includes: ", InLoop);
            EmptyLoop(fnode);
            IRO_UpdateFlagsOnInts();
        }
    }

    IRO_CheckForUserBreak();
}

#if VERSION == VERSION_WII_1_0RC1
typedef struct EmptyLoopTest {
    VarRecord* var;
    Type* comparisonType;
    CInt64 value;
    CInt64 bound;
    ENodeType relation;
} EmptyLoopTest;

static IROLinear* InductionLValue(IROLinear* assign, VarRecord* var) {
    IROLinear* left;

    if (assign->type == IROLinearOp2Arg) {
        left = assign->u.diadic.left;
    } else if (assign->type == IROLinearOp1Arg) {
        left = assign->u.monadic;
    } else {
        return NULL;
    }
    if (!IRO_IsAssignOp[assign->nodetype] || IRO_IsVariable(left) != var->object ||
        left->rtype->size != var->object->type->size) {
        return NULL;
    }
    return left;
}

static Boolean ReadInductionStep(IROLinear* assign, VarRecord* var, CInt64* step) {
    IROLinear* rhs;
    ENodeType op = assign->nodetype;

    if (IS_LINEAR_DIADIC(assign, EASS)) {
        rhs = assign->u.diadic.right;
        if (!IS_LINEAR_DIADIC_2(rhs, EADD, ESUB) ||
            IRO_IsVariable(rhs->u.diadic.left) != var->object ||
            !IRO_IsIntConstant(rhs->u.diadic.right)) {
            return FALSE;
        }
        op = rhs->nodetype;
        *step = rhs->u.diadic.right->u.node->data.intval;
    } else if (IS_LINEAR_DIADIC_2(assign, EADDASS, ESUBASS) &&
               IRO_IsIntConstant(assign->u.diadic.right)) {
        *step = assign->u.diadic.right->u.node->data.intval;
    } else if (IS_LINEAR_MONADIC_2(assign, EPREINC, EPOSTINC) ||
               IS_LINEAR_MONADIC_2(assign, EPREDEC, EPOSTDEC)) {
        *step = cint64_one;
    } else {
        return FALSE;
    }

    if (op == ESUB || op == ESUBASS || op == EPREDEC || op == EPOSTDEC) {
        *step = CInt64_Neg(*step);
    }
    return TRUE;
}

static Boolean ReadInductionValue(IROLinear* assign, VarRecord* var, CInt64* value, Boolean known) {
    CInt64 step;

    if (!InductionLValue(assign, var)) {
        return FALSE;
    }
    if (IS_LINEAR_DIADIC(assign, EASS) && IRO_IsIntConstant(assign->u.diadic.right)) {
        *value = assign->u.diadic.right->u.node->data.intval;
    } else if (known && ReadInductionStep(assign, var, &step)) {
        *value = CInt64_Add(*value, step);
    } else {
        return FALSE;
    }
    IRO_TruncateValueToType(value, var->object->type);
    return TRUE;
}

static Boolean AnalyzeLoopTest(IRONode* header, IRONode* test, IRONode* preheader, EmptyLoopTest* info) {
    static const ENodeType reversed[] = {EGREATER, ELESS, EGREATEREQU, ELESSEQU, EEQU, ENOTEQU};
    static const ENodeType negated[] = {EGREATEREQU, ELESSEQU, EGREATER, ELESS, ENOTEQU, EEQU};
    IROLinear* cond = test->last->u.label.cond;
    IROLinear* left;
    IROLinear* right;
    IROLinear* variable;
    IROLinear* scan;
    Object* object;
    VarRecord* var;
    IRONode* node;
    IRONode* first;
    Boolean known = FALSE;
    Boolean readValue = FALSE;
    CInt64 value;

    if (cond->type != IROLinearOp2Arg || cond->nodetype < ELESS || cond->nodetype > ENOTEQU) {
        return FALSE;
    }
    left = cond->u.diadic.left;
    right = cond->u.diadic.right;
    variable = left;
    if (!IRO_IsIntConstant(right)) {
        if (!IRO_IsIntConstant(left)) {
            return FALSE;
        }
        variable = right;
    }
    object = IRO_IsVariable(variable);
    if (!object || object->datatype != DLOCAL || !IS_TYPE_INT(object->type) ||
        object->type->size > 4 || IRO_HasSideEffect(variable) ||
        !(var = IRO_FindVar(object, FALSE, TRUE)) || var->noregister) {
        return FALSE;
    }

    // Follow the first entry to the test. A join could supply a different induction value.
    first = preheader;
    while (first->numpred == 1) {
        IRONode* pred = IRO_NodeTable[first->pred[0]];
        if (pred->numsucc != 1 || pred->nextnode != first ||
            Bv_IsBitSet(pred->index, InLoop) || !Bv_IsBitSet(pred->index, first->dom)) {
            break;
        }
        first = pred;
    }
    for (node = first;; node = IRO_NodeTable[node->succ[0]]) {
        for (scan = node->first; scan; scan = scan->next) {
            Bv_Clear(IRO_VarKills);
            IRO_GetKills(scan);
            if (Bv_IsBitSet(var->index, IRO_VarKills)) {
                if (readValue) {
                    return FALSE;
                }
                known = ReadInductionValue(scan, var, &value, known);
            }
            if (scan == variable) {
                readValue = TRUE;
            }
            if (scan == node->last) {
                break;
            }
        }
        if (node == test) {
            break;
        }
        if (node->numsucc != 1) {
            return FALSE;
        }
        if (Bv_IsBitSet(node->index, InLoop)) {
            IRONode* next = IRO_NodeTable[node->succ[0]];
            if (next->numpred != 1 || !Bv_IsBitSet(next->index, InLoop) ||
                Bv_IsBitSet(next->index, node->dom)) {
                return FALSE;
            }
        }
    }
    if (!known || !readValue) {
        return FALSE;
    }

    IRO_TruncateValueToType(&value, variable->rtype);
    info->value = value;
    info->bound = (variable == left ? right : left)->u.node->data.intval;
    IRO_TruncateValueToType(&info->bound, variable->rtype);
    info->var = var;
    info->comparisonType = variable->rtype;
    info->relation = variable == left ? cond->nodetype : reversed[cond->nodetype - ELESS];
    if (test->last->type == IROLinearIfNot) {
        info->relation = negated[info->relation - ELESS];
    }
    return TRUE;
}

static Boolean EvaluateLoopTest(const EmptyLoopTest* test) {
    int order;

    if (CInt64_Equal(test->value, test->bound)) {
        order = 0;
    } else if (is_unsigned(test->comparisonType) ? CInt64_LessU(test->value, test->bound)
                                                 : CInt64_Less(test->value, test->bound)) {
        order = -1;
    } else {
        order = 1;
    }
    switch (test->relation) {
        case ELESS: return order < 0;
        case EGREATER: return order > 0;
        case ELESSEQU: return order <= 0;
        case EGREATEREQU: return order >= 0;
        case EEQU: return order == 0;
        case ENOTEQU: return order != 0;
        default: return FALSE;
    }
}

static IROLinear* FindEmptyLoopUpdate(IRONode* test, VarRecord* var) {
    IRONode* node;
    IROLinear* scan;
    IROLinear* update = NULL;

    for (node = IRO_FirstNode; node; node = node->nextnode) {
        if (!Bv_IsBitSet(node->index, InLoop)) {
            continue;
        }
        if (node != test && (node->numsucc != 1 || !Bv_IsBitSet(node->succ[0], InLoop))) {
            return NULL;
        }
        for (scan = node->first; scan; scan = scan->next) {
            if (node == test && scan == node->last) {
                break;
            }
            if (scan->type != IROLinearNop && scan->type != IROLinearLabel &&
                scan->type != IROLinearGoto &&
                (IRO_HasSideEffect(scan) || IRO_ExprHasFloatingPointEffects(scan))) {
                if (update || !InductionLValue(scan, var) || (scan->flags & IROLF_Reffed)) {
                    return NULL;
                }
                update = scan;
            }
            if (scan == node->last) {
                break;
            }
        }
    }
    return update;
}

static Boolean GetEmptyLoopFinalValue(const EmptyLoopTest* test, CInt64 step, CInt64* final) {
    Type* type = test->var->object->type;
    CInt64 stride;
    CInt64 distance;
    CInt64 count;
    Boolean descending = CInt64_IsNegative(&step);

    if (type->size != test->comparisonType->size ||
        is_unsigned(type) != is_unsigned(test->comparisonType) ||
        CInt64_IsZero(&step)) {
        return FALSE;
    }

    stride = descending ? CInt64_Neg(step) : step;
    if (CInt64_IsNegative(&stride) || CInt64_Greater(stride, IRO_MakeULong(0xffffffffu))) {
        return FALSE;
    }
    distance = descending ? CInt64_Sub(test->value, test->bound)
                          : CInt64_Sub(test->bound, test->value);
    switch (test->relation) {
        case ELESS:
        case ELESSEQU:
            if (descending) {
                return FALSE;
            }
            break;
        case EGREATER:
        case EGREATEREQU:
            if (!descending) {
                return FALSE;
            }
            break;
        case ENOTEQU: {
            CInt64 remainder = CInt64_ModU(distance, stride);
            if (CInt64_IsNegative(&distance) || !CInt64_IsZero(&remainder)) {
                return FALSE;
            }
            break;
        }
        case EEQU:
            distance = stride;
            break;
        default:
            return FALSE;
    }

    if (test->relation == ELESSEQU || test->relation == EGREATEREQU) {
        distance = CInt64_Add(distance, cint64_one);
    }
    count = CInt64_DivU(CInt64_Add(distance, CInt64_Sub(stride, cint64_one)), stride);
    *final = CInt64_Add(test->value, CInt64_Mul(count, step));
    return CInt64_GreaterEqual(*final, IRO_MinValueOfIntegerType(type)) &&
           CInt64_LessEqual(*final, IRO_MaxValueOfIntegerType(type));
}

static Boolean RemoveEmptyCountedLoop(IRONode* test, const EmptyLoopTest* info) {
    IROLinear* update = FindEmptyLoopUpdate(test, info->var);
    IROLinear* assign;
    IROLinear* left;
    IROLinear* value;
    IROList list;
    CInt64 step;
    CInt64 final;

    if (!update || !ReadInductionStep(update, info->var, &step) ||
        !GetEmptyLoopFinalValue(info, step, &final)) {
        return FALSE;
    }

    left = InductionLValue(update, info->var);
    IRO_InitList(&list);
    left = IRO_DuplicateExpr(left, &list);
    value = IRO_NewIntConst(final, left->rtype);
    IRO_AddToList(value, &list);
    assign = IRO_NewLinear(IROLinearOp2Arg);
    assign->nodetype = EASS;
    assign->rtype = left->rtype;
    assign->stmt = update->stmt;
    assign->u.diadic.left = left;
    assign->u.diadic.right = value;
    IRO_AddToList(assign, &list);
    IRO_Paste(list.head, list.tail, IRO_FindStart(test->last->u.label.cond));
    return TRUE;
}


static Boolean InductionIsLiveAfterLoop(IRONode* exit, VarRecord* var) {
    IRONodes work;
    BitVector* visited;
    IRO_FNodeWorkStack_Init(&work);
    Bv_AllocVector(&visited, IRO_NumNodes + 1);
    IRO_FNodeWorkStack_Add(&work, exit->index);
    Bv_SetBit(exit->index, visited);

    while (IRO_FNodeWorkStack_Count(&work)) {
        IRONode* block = IRO_NodeTable[IRO_FNodeWorkStack_Remove(&work)];
        IROLinear* scan;
        Boolean overwritten = FALSE;
        UInt16 i;

        for (scan = block->first; scan; scan = scan->next) {
            if (IRO_IsVariable(scan) == var->object &&
                (!(scan->flags & IROLF_Assigned) || (scan->flags & IROLF_Used))) {
                return TRUE;
            }
            if (IRO_IsAssignment(scan) && IRO_FindAssigned(scan) == var) {
                overwritten = TRUE;
                break;
            }
            if (scan == block->last) {
                break;
            }
        }
        if (!overwritten) {
            for (i = 0; i < block->numsucc; ++i) {
                UInt16 successor = block->succ[i];
                if (!Bv_IsBitSet(successor, visited)) {
                    Bv_SetBit(successor, visited);
                    IRO_FNodeWorkStack_Add(&work, successor);
                }
            }
        }
    }
    return FALSE;
}



static Boolean FirstInductionUpdateIsRepresentable(
    IRONode* preheader, VarRecord* var, CInt64 step
) {
    IROLinear* write = IRO_FindInitialLoopAssignment(preheader, var);
    Type* type = var->object->type;
    CInt64 value;

    if (!write || !ReadInductionValue(write, var, &value, FALSE)) {
        return FALSE;
    }
    value = CInt64_Add(value, step);
    return CInt64_GreaterEqual(value, IRO_MinValueOfIntegerType(type)) &&
        CInt64_LessEqual(value, IRO_MaxValueOfIntegerType(type));
}

static Boolean RemoveEmptyDynamicLoop(IRONode* test, IRONode* preheader) {
    IROLinear* cond = test->last->u.label.cond;
    IROLinear* variable;
    IROLinear* bound;
    IROLinear* update;
    Object* object;
    VarRecord* var;
    Type* type;
    Boolean increasing;
    Boolean testedFirst;
    CInt64 step;
    int side;

    if (cond->type != IROLinearOp2Arg ||
        (cond->nodetype != ELESS && cond->nodetype != EGREATER &&
         cond->nodetype != ELESSEQU && cond->nodetype != EGREATEREQU) ||
        IRO_HasSideEffect(cond) || IRO_ExprHasFloatingPointEffects(cond) ||
        IRO_IsIntConstant(cond->u.diadic.left) || IRO_IsIntConstant(cond->u.diadic.right)) {
        return FALSE;
    }

    for (side = 0; side < 2; ++side) {
        variable = side ? cond->u.diadic.right : cond->u.diadic.left;
        bound = side ? cond->u.diadic.left : cond->u.diadic.right;
        object = IRO_IsVariable(variable);
        if (!object || object->datatype != DLOCAL || !(var = IRO_FindVar(object, FALSE, TRUE)) ||
            var->noregister || !(type = object->type) || !IS_TYPE_INT(type) || type->size > 4 ||
            !IRO_TypesEqual(variable->rtype, type) || !IRO_TypesEqual(bound->rtype, type)) {
            continue;
        }

        increasing = cond->nodetype == ELESS || cond->nodetype == EGREATEREQU;
        if (side) {
            increasing = !increasing;
        }

        // Only a strict comparison guarantees that a unit step cannot step past the type's limit.
        if ((test->last->type == IROLinearIf) !=
            (cond->nodetype == ELESS || cond->nodetype == EGREATER)) {
            continue;
        }
        update = FindEmptyLoopUpdate(test, var);
        if (!update || !ReadInductionStep(update, var, &step) ||
            !CInt64_Equal(step, increasing ? cint64_one : CInt64_Neg(cint64_one))) {
            continue;
        }

        testedFirst = TRUE;
        for (IROLinear* nd = test->first; nd && nd != test->last; nd = nd->next) {
            if (nd == update) {
                testedFirst = FALSE;
                break;
            }
        }
        if ((!testedFirst && !FirstInductionUpdateIsRepresentable(preheader, var, step)) ||
            InductionIsLiveAfterLoop(test->nextnode, var)) {
            continue;
        }
        IRO_FindDepends(bound);
        if (!Bv_IsBitSet(var->index, IRO_Depends)) {
            return TRUE;
        }
    }
    return FALSE;
}

Boolean IRO_RemoveAllEmptyLoops(Boolean removeDynamic) {
    BitVector* savedKills = IRO_VarKills;
    Boolean changed = FALSE;
    IRONode* header;
    IRONode* node;
    IRONode* preheader;
    EmptyLoopTest info;
    UInt16 i;

    Bv_AllocVector(&IRO_VarKills, IRO_NumVars + 1);
    for (header = IRO_FirstNode; header; header = header->nextnode) {
        if (!FindLoopNodes(header)) {
            continue;
        }
        preheader = NULL;
        for (i = 0; i < header->numpred; i++) {
            node = IRO_NodeTable[header->pred[i]];
            if (!Bv_IsBitSet(node->index, InLoop)) {
                if (preheader) {
                    preheader = NULL;
                    break;
                }
                preheader = node;
            }
        }
        if (!preheader) {
            continue;
        }
        for (node = IRO_FirstNode; node; node = node->nextnode) {
            if (Bv_IsBitSet(node->index, InLoop) && node->numsucc == 2 && node->nextnode &&
                (node == header || (header->numpred == 2 &&
                                    (header->pred[0] == node->index || header->pred[1] == node->index))) &&
                !Bv_IsBitSet(node->nextnode->index, InLoop) && node->last &&
                (node->last->type == IROLinearIf || node->last->type == IROLinearIfNot) &&
                ((AnalyzeLoopTest(header, node, preheader, &info) &&
                  (!EvaluateLoopTest(&info) || RemoveEmptyCountedLoop(node, &info))) ||
                 (removeDynamic && RemoveEmptyDynamicLoop(node, preheader)))) {
                IROLinear* cond = node->last->u.label.cond;
                node->last->type = IROLinearNop;
                cond->flags &= ~IROLF_Reffed;
                IRO_CheckSideEffect(cond);
                changed = TRUE;
                break;
            }
        }
    }
    IRO_VarKills = savedKills;
    if (changed) {
        IRO_UpdateFlagsOnInts();
        if (copts.deadcode) {
            IRO_RemoveUnreachable();
        } else {
            IRO_ComputeSuccPred();
            IRO_ComputeDom();
        }
    }
    IRO_CheckForUserBreak();
    return changed;
}
#endif

static int EmptyLoop(IRONode* fnode) {
    ENode* enode;
    int loopPredCount;
    int loopSuccCount;
    IROLoop* loop;
    IRONode* succ;
    IRONode* pred;
    int j;
    IRONode* bestpred;
    IROLinear* constnd;
    Boolean isSelfLoop;
    UInt16 i;
    IRONode* preheader;
    UInt32 outsidePredCount;
    IRONode* walk;
    int hasOutsidePred;
    IRONode* bestsucc;
    IRONode* tail;
    VarRecord* var;
    Type* type;
    IROLinear* save;
    UInt16 k;

    isSelfLoop = FALSE;
    loopSuccCount = 0;
    LoopNode = fnode;
    FindMustReach();

    for (var = IRO_FirstVar; var; var = var->next) {
        var->inductionState = TRUE;
    }

    ComputeLoopKills();
    ComputeLoopInvariance();
    ComputeLoopInduction();

    LoopNode = fnode;
    ConditionalHeaderAtBottom = FALSE;

    bestpred = NULL;
    hasOutsidePred = FALSE;
    for (i = 0; i < LoopNode->numpred; i++) {
        pred = IRO_NodeTable[LoopNode->pred[i]];
        if (!Bv_IsBitSet(pred->index, InLoop)) {
            hasOutsidePred = TRUE;
            if (pred->nextnode == fnode) {
                CError_ASSERT(173, !bestpred || pred == bestpred);
                bestpred = pred;
            }
        }
    }

    if (!hasOutsidePred) {
        IRO_Dump("No predecessor outside the loop\n");
        return FALSE;
    }

    bestsucc = NULL;
    for (k = 0; k < LoopNode->numsucc; k++) {
        succ = IRO_NodeTable[LoopNode->succ[k]];
        if (Bv_IsBitSet(succ->index, InLoop)) {
            loopSuccCount++;
            bestsucc = succ;
        }
    }

    if (LoopNode == bestsucc && loopSuccCount == 1) {
        isSelfLoop = TRUE;
    }

    if (LoopNode->last->type != IROLinearIf ||
        LoopNode->last->type != IROLinearIfNot ||
        isSelfLoop) {
        loopPredCount = 0;
        for (j = 0; j < LoopNode->numpred; j++) {
            IRONode* node = IRO_NodeTable[LoopNode->pred[j]];
            if (Bv_IsBitSet(node->index, InLoop)) {
                loopPredCount++;
                tail = node;
            }
        }

        preheader = NULL;
        outsidePredCount = 0;
        for (j = 0; j < LoopNode->numpred; j++) {
            if (!Bv_IsBitSet(IRO_NodeTable[LoopNode->pred[j]]->index, InLoop)) {
                outsidePredCount++;
                preheader = IRO_NodeTable[LoopNode->pred[j]];
            }
        }

        if (loopPredCount == 1 && outsidePredCount == 1) {
            if (tail->last->type == IROLinearIf) {
                if ((Bv_IsBitSet(LoopNode->nextnode->index, InLoop) &&
                     !Bv_IsBitSet(tail->nextnode->index, InLoop)) ||
                    isSelfLoop) {
                    IRO_Dump("Standard while loop layout\n");
                    loop = ExtractLoopInfo(tail);
                    if (isSelfLoop) {
                        loop->flags |= LP_RECURSIVE_LOOP;
                    }
                    FindAssignmenttoInductionVar(loop, preheader);
                    walk = preheader;
                    while (walk &&
                           !loop->init &&
                           walk->numpred == 1 &&
                           IRO_NodeTable[walk->pred[0]]->numsucc == 1) {
                        FindAssignmenttoInductionVar(loop, IRO_NodeTable[walk->pred[0]]);
                        walk = IRO_NodeTable[walk->pred[0]];
                    }

                    if (CanRemoveRedundantLoop(loop)) {
                        IRO_Dump(
                            "EmptyLoop: # of iterations =%ld, FinalStoreVal=%ld\n",
                            CInt64_GetULong(&loop->iterCount),
                            CInt64_GetULong(&loop->finalStoreVal)
                        );
                        IRO_NopOut(tail->last->u.label.cond);
                        tail->last->type = IROLinearNop;
                        type = loop->induction->nd->rtype;
                        constnd = IRO_NewLinear(IROLinearOperand);
                        constnd->index = ++IRO_NumLinear;
                        enode = IRO_NewENode(EINTCONST);
                        enode->rtype = type;
                        enode->data.intval = loop->finalStoreVal;
                        constnd->u.node = enode;
                        constnd->rtype = type;

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

                        tail->last->type = IROLinearNop;
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

    if (loop->flags & LP_WHILE_LOOP) {
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

    if (!(loop->flags & LP_HEADER_FOLLOWS_UPDATE)) {
        IRO_Dump("CanRemoveRedundantLoop:No because header does not follow induction update \n");
        return FALSE;
    }

    if (!(loop->flags & LP_WHILE_LOOP)) {
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
    if ((loop->flags & LP_WHILE_LOOP) && (loop->flags & LP_RECURSIVE_LOOP)) {
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

        if (!(loop->flags & LP_HEADER_FOLLOWS_UPDATE)) {
            IRO_Dump(
                "CanRemoveRedundantLoop1:No because header does not follow induction update \n"
            );
            return FALSE;
        }

        if (loop->induction->nd->type == IROLinearOp1Arg &&
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
                    nd->type != IROLinearNop &&
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

static int CheckStepOverFlow1_EmptyLoop(IROLoop* loop, CInt64* iterCount, CInt64* finalValue) {
    Boolean isUnsigned;
    IROLinear* initNd;
    IROLinear* limitNd;
    CInt64 initValue;
    CInt64 limitValue;
    CInt64 addConst;
    CInt64 work;
    CInt64 neg1;

    initNd = loop->init->u.diadic.right;
    limitNd = loop->cond->u.diadic.right;
    isUnsigned = is_unsigned(loop->cond->u.diadic.right->rtype);

    if (IRO_IsIntConstant(initNd) && IRO_IsIntConstant(limitNd)) {
        initValue = initNd->u.node->data.intval;
        limitValue = limitNd->u.node->data.intval;
        if (isUnsigned) {
            if (CInt64_LessEqualU(limitValue, initValue)) {
                return TRUE;
            }
        } else {
            if (CInt64_LessEqual(limitValue, initValue)) {
                return TRUE;
            }
        }

        CInt64_SetLong(&addConst, loop->induction->addConst);
        CInt64_SetLong(&neg1, -1);
        *iterCount = CInt64_Sub(limitValue, initValue);
        *iterCount = CInt64_Add(*iterCount, addConst);
        if (IS_LINEAR_DIADIC(loop->cond, ELESS)) {
            *iterCount = CInt64_Add(*iterCount, neg1);
        }

        CError_ASSERT(855, !CInt64_IsZero(&addConst));

        if (isUnsigned) {
            *iterCount = CInt64_DivU(*iterCount, addConst);
        } else {
            *iterCount = CInt64_Div(*iterCount, addConst);
        }

        if (CInt64_Equal(*iterCount, cint64_zero)) {
            return TRUE;
        }

        if (isUnsigned) {
            if (CInt64_LessEqualU(*iterCount, cint64_zero)) {
                CError_FATAL(877);
            }
        } else {
            if (CInt64_LessEqual(*iterCount, cint64_zero)) {
                CError_FATAL(886);
            }
        }

        if (isUnsigned) {
            *finalValue = CInt64_MulU(*iterCount, addConst);
            *finalValue = CInt64_Add(*finalValue, initValue);
        } else {
            *finalValue = CInt64_Mul(*iterCount, addConst);
            *finalValue = CInt64_Add(*finalValue, initValue);
        }
    } else {
        return TRUE;
    }

    CInt64_SetLong(&addConst, loop->induction->addConst);
    work = CInt64_Add(limitValue, addConst);

    if (isUnsigned) {
        if (CInt64_LessU(work, limitValue)) {
            return TRUE;
        }
    } else {
        if (CInt64_Less(work, limitValue)) {
            return TRUE;
        }
    }

    return FALSE;
}

static int CheckStepOverFlow2_EmptyLoop(IROLoop* loop, CInt64* iterCount, CInt64* finalValue) {
    Boolean isUnsigned;
    IROLinear* limitNd;
    IROLinear* initNd;
    CInt64 limitValue;
    CInt64 initValue;
    CInt64 addConst;
    CInt64 work;
    CInt64 neg1;

    initNd = loop->init->u.diadic.right;
    limitNd = loop->cond->u.diadic.right;
    isUnsigned = is_unsigned(loop->cond->u.diadic.right->rtype);

    if (IRO_IsIntConstant(limitNd) && IRO_IsIntConstant(initNd)) {
        limitValue = limitNd->u.node->data.intval;
        initValue = initNd->u.node->data.intval;
        if (isUnsigned) {
            if (CInt64_LessEqualU(initValue, limitValue)) {
                return TRUE;
            }
        } else {
            if (CInt64_LessEqual(initValue, limitValue)) {
                return TRUE;
            }
        }

        CInt64_SetLong(&addConst, loop->induction->addConst);
        CInt64_SetLong(&neg1, -1);
        *iterCount = CInt64_Sub(initValue, limitValue);
        *iterCount = CInt64_Add(*iterCount, addConst);
        if (IS_LINEAR_DIADIC(loop->cond, EGREATER)) {
            *iterCount = CInt64_Add(*iterCount, neg1);
        }

        CError_ASSERT(995, !CInt64_IsZero(&addConst));

        if (isUnsigned) {
            *iterCount = CInt64_DivU(*iterCount, addConst);
        } else {
            *iterCount = CInt64_Div(*iterCount, addConst);
        }

        if (CInt64_Equal(*iterCount, cint64_zero)) {
            return TRUE;
        }

        if (isUnsigned) {
            if (CInt64_LessEqualU(*iterCount, cint64_zero)) {
                return FALSE;
            }
        } else {
            if (CInt64_LessEqual(*iterCount, cint64_zero)) {
                return FALSE;
            }
        }

        if (isUnsigned) {
            *finalValue = CInt64_MulU(*iterCount, addConst);
            *finalValue = CInt64_Sub(initValue, *finalValue);
        } else {
            *finalValue = CInt64_Mul(*iterCount, addConst);
            *finalValue = CInt64_Sub(initValue, *finalValue);
        }
    } else {
        return TRUE;
    }

    CInt64_SetLong(&addConst, loop->induction->addConst);
    work = CInt64_Sub(limitValue, addConst);

    if (isUnsigned) {
        if (CInt64_GreaterU(work, limitValue)) {
            return TRUE;
        }
    } else {
        if (CInt64_Greater(work, initValue)) {
            return TRUE;
        }
    }

    return FALSE;
}
