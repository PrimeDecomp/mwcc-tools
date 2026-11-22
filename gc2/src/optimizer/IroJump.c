#include "compiler/IroJump.h"
#include "compiler/IroDump.h"
#include "compiler/IroFlowgraph.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroUtil.h"
#include "compiler/CFunc.h"
#include "compiler/Exceptions.h"
#include "compiler/Switch.h"

#if VERSION == VERSION_WII_1_0RC1
#include "compiler/CompilerTools.h"
#include "compiler/IroCSE.h"
#include "compiler/IroEval.h"
#include "compiler/IroVars.h"
#include "compiler/CParser.h"
#include <stdlib.h>

static Boolean CheckChain(CLabel** labelptr);

typedef struct IROSwitchRange {
    CLabel* label;
    CInt64 first;
    CInt64 last;
    UInt32 order;
    UInt32 sort_high;
} IROSwitchRange;

static int compare_switch_values(const void* a, const void* b) {
    const IROSwitchRange* left = a;
    const IROSwitchRange* right = b;
    if (left->sort_high != right->sort_high) {
        return left->sort_high < right->sort_high ? -1 : 1;
    }
    return (left->first.lo > right->first.lo) - (left->first.lo < right->first.lo);
}

static int compare_switch_ranges(const void* a, const void* b) {
    const IROSwitchRange* left = a;
    const IROSwitchRange* right = b;
    CInt64 lhs = CInt64_Sub(left->last, left->first);
    CInt64 rhs = CInt64_Sub(right->last, right->first);
    if (CInt64_NotEqual(lhs, rhs)) {
        return CInt64_GreaterU(lhs, rhs) ? -1 : 1;
    }
    return (left->order > right->order) - (left->order < right->order);
}

static int group_switch_cases(
    const SwitchInfo* info, IROSwitchRange* ranges, int count, CLabel* default_label
) {
    SwitchCase* entry;
    UInt32 sign_bias = is_unsigned(info->exprtype) ? 0 : 0x80000000u;
    int i = count;
    int used = 0;

    // The parser prepends cases. Keep their source order for equally wide ranges.
    for (entry = info->cases; entry; entry = entry->next) {
        IROSwitchRange* range = &ranges[--i];
        range->label = entry->label;
        CheckChain(&range->label);
        range->first = range->last = entry->min;
        range->order = i;
        range->sort_high = (UInt32)entry->min.hi ^ sign_bias;
    }
    qsort(ranges, count, sizeof(*ranges), compare_switch_values);
    for (i = 0; i < count; i++) {
        IROSwitchRange* current = &ranges[i];
        if (current->label == default_label) {
            continue;
        }
        if (used && ranges[used - 1].label == current->label &&
            CInt64_Equal(CInt64_Add(ranges[used - 1].last, cint64_one), current->first)) {
            IROSwitchRange* previous = &ranges[used - 1];
            previous->last = current->last;
            if (current->order < previous->order) {
                previous->order = current->order;
            }
        } else {
            ranges[used++] = *current;
        }
    }
    return used;
}

static void emit_switch_label(IROList* list, IROLinearType kind, CLabel* label) {
    IROLinear* linear = IRO_NewLinear(kind);
    linear->index = ++IRO_NumLinear;
    linear->u.label.label = label;
    if (kind == IROLinearLabel) {
        linear->flags |= IROLF_StartsNode;
    }
    IRO_AddToList(linear, list);
}

static void emit_switch_test(
    IROList* list, IROLinear* selector, Object* temp, ENodeType relation, CInt64 value, CLabel* label
) {
    IROLinear* lhs = temp ? IRO_TempReference(temp, list) : IRO_DuplicateExpr(selector, list);
    IROLinear* rhs = IRO_NewIntConst(value, lhs->rtype);
    IROLinear* comparison = IRO_NewLinear(IROLinearOp2Arg);
    IROLinear* branch = IRO_NewLinear(IROLinearIf);

    lhs->flags |= IROLF_Reffed;
    rhs->flags |= IROLF_Reffed;
    IRO_AddToList(rhs, list);
    comparison->nodetype = relation;
    comparison->rtype = IRO_LogicalType();
    comparison->u.diadic.left = lhs;
    comparison->u.diadic.right = rhs;
    comparison->index = ++IRO_NumLinear;
    comparison->flags |= IROLF_Reffed;
    IRO_AddToList(comparison, list);
    branch->index = ++IRO_NumLinear;
    branch->u.label.cond = comparison;
    branch->u.label.label = label;
    IRO_AddToList(branch, list);
}

static void lower_switch(
    IROLinear* linear, CLabel* default_label, IROSwitchRange* ranges, int count,
    CInt64 min, CInt64 max
) {
    IROLinear* selector = linear->u.swtch.cond;
    Object* temp = NULL;
    IROList list;
    int i;

    CurStat = linear->stmt;
    if (IRO_HasSideEffect(selector)) {
        IROExpr expr = {0};
        expr.linear = selector;
        IRO_GetTemp(&expr);
        temp = expr.temp;
        IRO_AssignToTemp(&expr);
    }
    qsort(ranges, count, sizeof(*ranges), compare_switch_ranges);
    IRO_InitList(&list);
    for (i = 0; i < count; i++) {
        const IROSwitchRange* range = &ranges[i];
        if (CInt64_Equal(range->first, range->last)) {
            emit_switch_test(&list, selector, temp, EEQU, range->first, range->label);
        } else {
            CLabel* skip = NULL;
            if (CInt64_NotEqual(range->first, min)) {
                skip = IRO_NewLabel();
                emit_switch_test(&list, selector, temp, ELESS, range->first, skip);
            }
            if (CInt64_Equal(range->last, max)) {
                emit_switch_label(&list, IROLinearGoto, range->label);
            } else {
                emit_switch_test(&list, selector, temp, ELESSEQU, range->last, range->label);
            }
            if (skip) {
                emit_switch_label(&list, IROLinearLabel, skip);
            }
        }
    }
    emit_switch_label(&list, IROLinearGoto, default_label);
    if (!temp) {
        IRO_NopOut(selector);
    }
    linear->type = IROLinearNop;
    linear->expr = NULL;
    IRO_PasteAfter(list.head, list.tail, linear);
}

Boolean IRO_OptimizeSwitches(void) {
    IRONode* node;
    Boolean changed = FALSE;

    for (node = IRO_FirstNode; node; node = node->nextnode) {
        IROLinear* linear = node->last;
        SwitchInfo* info;
        SwitchCase* entry;
        IROSwitchRange* ranges;
        CLabel* default_label;
        CInt64 min;
        CInt64 max;
        int count = 0;
        int tests;
        int i;

        if (!linear || linear->type != IROLinearSwitch) {
            continue;
        }
        info = linear->u.swtch.info;
        default_label = info->defaultlabel;
        CheckChain(&default_label);
        for (entry = info->cases; entry; entry = entry->next) {
            count++;
        }
        ranges = oalloc(sizeof(*ranges) * count);
        count = group_switch_cases(info, ranges, count, default_label);
        min = IRO_MinValueOfIntegerType(info->exprtype);
        max = IRO_MaxValueOfIntegerType(info->exprtype);
        tests = count;
        for (i = 0; i < count && tests < 8; i++) {
            if (CInt64_NotEqual(ranges[i].first, ranges[i].last) &&
                CInt64_NotEqual(ranges[i].first, min) &&
                CInt64_NotEqual(ranges[i].last, max)) {
                tests++;
            }
        }
        if (tests < 8) {
            lower_switch(linear, default_label, ranges, count, min, max);
            changed = TRUE;
        }
    }
    if (changed) {
        CurStat = NULL;
        IRO_BuildFlowgraph(IRO_FirstLinear);
        IRO_UpdateFlagsOnInts();
    }
    return changed;
}
#endif

static Boolean CheckChain(CLabel** labelptr) {
    IRONode* node;

    for (node = IRO_FirstNode; node; node = node->nextnode) {
        if (node->first &&
            node->first->type == IROLinearLabel &&
            node->first->u.label.label == *labelptr) {
            IROLinear* linear;
            CLabel* lab;
            for (linear = node->first->next, lab = NULL;
                 linear && (linear->type == IROLinearLabel || linear->type == IROLinearNop);
                 linear = linear->next) {
                if (linear->type == IROLinearLabel) {
                    lab = linear->u.label.label;
                }
            }

            if (linear->type == IROLinearGoto &&
#if VERSION == VERSION_WII_1_0RC1
                !(linear->flags & IROLF_EndLoop) &&
#endif
                *labelptr != linear->u.label.label) {
                *labelptr = linear->u.label.label;
                IRO_Dump("Chaining goto at %d\n", linear->index);
                return TRUE;
            }

            if (lab && *labelptr != lab) {
                *labelptr = lab;
            }
            return FALSE;
        }
    }

    return FALSE;
}

Boolean IRO_DoJumpChaining(void) {
    IRONode* node;
    IROLinear* linear;
    Boolean changed;
    SwitchInfo* info;
    SwitchCase* curcase;
    Boolean result;

    result = FALSE;
    do {
        changed = FALSE;
        for (node = IRO_FirstNode; node; node = node->nextnode) {
            if (node->first) {
                linear = node->last;
                switch (linear->type) {
                    case IROLinearGoto:
                        if (CheckChain(&linear->u.label.label)) {
                            changed = TRUE;
                        }
                        break;
                    case IROLinearIf:
                    case IROLinearIfNot:
                        if (CheckChain(&linear->u.label.label)) {
                            changed = TRUE;
                        }
                        break;
                    case IROLinearSwitch:
                        info = linear->u.swtch.info;
                        for (curcase = info->cases; curcase; curcase = curcase->next) {
                            if (CheckChain(&curcase->label)) {
                                changed = TRUE;
                            }
                        }
                        if (CheckChain(&info->defaultlabel)) {
                            changed = TRUE;
                        }
                        break;
                    default:
                        break;
                }
            }
        }
        result |= changed;
        IRO_CheckForUserBreak();
    } while (changed);

#if VERSION == VERSION_WII_1_0RC1
    if (result) {
        IRO_ComputeSuccPred();
        IRO_ComputeDom();
    }
#endif
    return result;
}

void IRO_MakeReachable(IRONode* node) {
    UInt16 i;
    Boolean changed;
    IRONode* scan;

    node->reachable = TRUE;
    do {
        changed = FALSE;
        for (scan = IRO_FirstNode; scan; scan = scan->nextnode) {
            if (scan->reachable && !scan->visited) {
                for (i = 0; i < scan->numsucc; i++) {
                    if (!IRO_NodeTable[scan->succ[i]]->reachable) {
                        changed = TRUE;
                        IRO_NodeTable[scan->succ[i]]->reachable = TRUE;
                    }
                }
                scan->visited = TRUE;
            }
        }
    } while (changed);
}

Boolean IRO_RemoveUnreachable(void) {
    IRONode* node;
    IRONode* prevNode;
    IROLinear* scan;
    IROLinear* linear;
    Boolean result;
    ExceptionAction** actptr;
    ExceptionAction* act;

    result = FALSE;
    IRO_ComputeSuccPred();
    IRO_MakeReachable(IRO_FirstNode);
    prevNode = IRO_FirstNode;
    for (node = IRO_FirstNode->nextnode; node; node = node->nextnode) {
        if (node->first && !node->reachable) {
            IRO_Dump("Removing unreachable code at: %d\n", node->index);
            prevNode->nextnode = node->nextnode;
            prevNode->last->next = node->last->next;
            result = TRUE;
            for (linear = node->first;
                 linear && linear->type == IROLinearLabel && linear != node->last->next;
                 linear = linear->next) {
                for (scan = IRO_FirstLinear; scan; scan = scan->next) {
                    if (scan->stmt) {
                        scan->stmt->marked = FALSE;
                    }
                }

                for (scan = IRO_FirstLinear; scan; scan = scan->next) {
                    if (scan->stmt && !scan->stmt->marked) {
                        scan->stmt->marked = TRUE;
                        for (actptr = &scan->stmt->dobjstack; (act = *actptr);
                             actptr = &act->prev) {
                            if ((act->type == EAT_CATCHBLOCK &&
                                 act->data.catch_block.catch_label == linear->u.label.label) ||
                                (act->type == EAT_SPECIFICATION &&
                                 act->data.specification.unexp_label == linear->u.label.label)) {
                                *actptr = act->prev;
                            }
                        }
                    }
                }
            }
            if (node == IRO_LastNode) {
                IRO_LastNode = prevNode;
            }
        } else {
            prevNode = node;
        }
    }

    if (result) {
        IRO_ComputeSuccPred();
        IRO_ComputeDom();
    }

    IRO_CheckForUserBreak();
    return result;
}

Boolean IRO_RemoveRedundantJumps(void) {
    IRONode* node;
    IROLinear* linear;
    IROLinear* scan;
    IROLinear* scan2;
    SwitchInfo* info;
    SwitchCase* curcase;
    Boolean result;

    result = FALSE;
    for (node = IRO_FirstNode; node; node = node->nextnode) {
        if (node->first) {
            linear = node->last;
            switch (linear->type) {
                case IROLinearGoto:
                    scan = linear->next;
                    while (scan &&
                           ((scan->type == IROLinearNop) ||
                            (scan->type == IROLinearLabel &&
                             scan->u.label.label != linear->u.label.label))) {
                        scan = scan->next;
                    }
                    while (1) {
                        if (!scan) {
                            break;
                        }
                        if (scan->type != IROLinearLabel) {
                            break;
                        }
                        if (scan->u.label.label == linear->u.label.label) {
                            IRO_Dump("Removing goto next at %d\n", linear->index);
                            linear->type = IROLinearNop;
                            result = TRUE;
                            break;
                        }
                        scan = scan->next;
                    }
                    break;

                case IROLinearIf:
                case IROLinearIfNot:
                    scan = linear->next;
                    while (scan && scan->type == IROLinearNop) {
                        scan = scan->next;
                    }
                    if (scan && scan->type == IROLinearGoto) {
                        scan2 = scan->next;
                        while (scan2 &&
                               ((scan2->type == IROLinearNop) ||
                                (scan2->type == IROLinearLabel &&
                                 scan2->u.label.label != linear->u.label.label))) {
                            scan2 = scan2->next;
                        }

                        if (scan2 &&
                            scan2->type == IROLinearLabel &&
                            scan2->u.label.label == linear->u.label.label) {
                            if (linear->type == IROLinearIf) {
                                linear->type = IROLinearIfNot;
                            } else {
                                linear->type = IROLinearIf;
                            }

                            linear->u.label.label = scan->u.label.label;
                            scan->type = IROLinearNop;
                            IRO_Dump("Removing branch around goto at %d\n", linear->index);
                            result = TRUE;
                        }
                    }

                    scan2 = linear->next;
                    while (scan2 &&
                           ((scan2->type == IROLinearNop) ||
                            (scan2->type == IROLinearLabel &&
                             scan2->u.label.label != linear->u.label.label))) {
                        scan2 = scan2->next;
                    }
                    while (1) {
                        if (!scan2) {
                            break;
                        }
                        if (scan2->type != IROLinearLabel) {
                            break;
                        }
                        if (scan2->u.label.label == linear->u.label.label) {
                            IRO_Dump("Removing If/IfNot_Goto next at %d\n", linear->index);
                            linear->type = IROLinearNop;
                            IRO_CheckSideEffect(linear->u.label.cond);
                            result = TRUE;
                            break;
                        }
                        scan2 = scan2->next;
                    }
                    break;

                case IROLinearSwitch:
                    info = linear->u.swtch.info;
                    curcase = info->cases;
                    while (curcase && curcase->label == info->defaultlabel) {
                        curcase = curcase->next;
                    }
                    if (!curcase) {
                        IRO_Dump("Removing Switch next at %d\n", linear->index);
                        IRO_CheckSideEffect(linear->u.swtch.cond);
                        linear->type = IROLinearGoto;
                        linear->u.label.label = info->defaultlabel;
                        result = TRUE;
                    }
                    break;
                default:
                    break;
            }
        }
    }

    if (result) {
        IRO_ComputeSuccPred();
        IRO_ComputeDom();
    }

    IRO_CheckForUserBreak();
    return result;
}

Boolean IRO_RemoveLabels(void) {
    Boolean result;
    IRONode* node;

    result = FALSE;
    IRO_ComputeSuccPred();
    for (node = IRO_FirstNode; node; node = node->nextnode) {
        if (node->first && node->first->type == IROLinearLabel && !node->referenced) {
            node->first->type = IROLinearNop;
            node->first->flags &= ~IROLF_StartsNode;
            result = TRUE;
        }
    }

#if VERSION == VERSION_WII_1_0RC1
    if (result) {
        IRO_BuildFlowgraph(IRO_FirstLinear);
    }
#endif
    IRO_CheckForUserBreak();
    return result;
}
