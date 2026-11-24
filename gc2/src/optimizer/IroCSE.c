#include "compiler/IroCSE.h"
#include "compiler/IroDump.h"
#include "compiler/IroFlowgraph.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroMalloc.h"
#include "compiler/IroPointerAnalysis.h"
#include "compiler/IroSubable.h"
#include "compiler/IROUseDef.h"
#include "compiler/IroUtil.h"
#include "compiler/IroVars.h"
#include "compiler/BitVector.h"
#include "compiler/CError.h"
#include "compiler/CExpr.h"
#include "compiler/CInt64.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include <string.h>

BitVector* IRO_Depends;
Boolean IRO_NotSubable;
Boolean IRO_IsVolatile;
Boolean IRO_CouldError;
IROExpr* IRO_FirstExpr;
IROExpr* IRO_LastExpr;
SInt32 IRO_NumExprs;
static Boolean HasVectorOperand;

static void IRO_DependsOnForDataFlow(IROLinear* linear, Boolean isIndirect);

static void GetDependsOfIndirect(IROLinear* nd) {
    IROListNode* resultList;
    IROListNode* next;
    IROListNode* scan;
    IROLinear* scannd;
    Object* obj;
    VarRecord* var;
    int index;
    Boolean result;
    Boolean foundObjRef;

    result = FALSE;

    if (nd && copts.opt_pointer_analysis && nd->pointsToFunction && FunctionName) {
        resultList = NULL;
        PointerAnalysis_LookupLinearNodePointerExpr(FunctionName, nd, &resultList);
        if (resultList) {
            for (scan = resultList; scan; scan = scan->nextList) {
                if (!scan->list.head || !scan->list.tail) {
                    result = TRUE;
                    break;
                }

                foundObjRef = FALSE;
                for (scannd = scan->list.head; scannd != scan->list.tail->next;
                     scannd = scannd->next) {
                    if (scannd->type == IROLinearOperand && scannd->u.node->type == EOBJREF) {
                        foundObjRef = TRUE;
                        break;
                    }
                }

                if (!foundObjRef) {
                    result = TRUE;
                    break;
                }
            }

            if (!result) {
                for (scan = resultList; scan; scan = scan->nextList) {
                    for (scannd = scan->list.head; scannd != scan->list.tail->next;
                         scannd = scannd->next) {
                        if (scannd->type == IROLinearOperand && scannd->u.node->type == EOBJREF) {
                            obj = scannd->u.node->data.objref;
                            CError_ASSERT(119, obj != NULL);
                            var = IRO_FindVar(obj, TRUE, TRUE);
                            CError_ASSERT(121, var != NULL);
                            index = var->index;
                            CError_ASSERT(123, index != 0);

                            if (is_volatile_object(obj)) {
                                IRO_IsVolatile = TRUE;
                                IRO_NotSubable = TRUE;
                            }
                            Bv_SetBit(index, IRO_Depends);
                        }
                    }
                }
            }

            while (resultList) {
                next = resultList->nextList;
                IRO_free(resultList);
                resultList = next;
            }
        } else {
            result = TRUE;
        }
    } else {
        result = TRUE;
    }

    if (result) {
        nd = nd->u.monadic;
        if (nd->type == IROLinearOp1Arg && nd->nodetype == EBITFIELD) {
            nd = nd->u.monadic;
        }

        if (nd->type == IROLinearOp2Arg && nd->nodetype == EADD) {
            IRO_BaseTerms = 0;
            IRO_VarTerms = 0;
            IRO_DecomposeAddressExpression_Cheap(nd);
            if (IRO_BaseTerms != 1) {
                IRO_CouldError = TRUE;
                Bv_SetBit(0, IRO_Depends);
                Bv_Or(IRO_FuncKills, IRO_Depends);
            }
            if (IRO_VarTerms) {
                IRO_CouldError = TRUE;
            }
        } else {
            IRO_CouldError = TRUE;
            Bv_SetBit(0, IRO_Depends);
            Bv_Or(IRO_FuncKills, IRO_Depends);
        }
    }
}

static void GetDependsOfFunctionCallForDataFlow(IROLinear* nd) {
    IROLinear* innernd;
    IROListNode* resultList;
    IROListNode* scan;
    IROLinear* scannd;
    Object* obj;
    VarRecord* var;
    int index;
    int i;
    IROListNode* next;
    Boolean result;
    Boolean foundObjRef;
    ObjectList* olist;
    ObjectList* depsList;

    result = FALSE;
    innernd = nd->u.funccall.callee;

    if (innernd && copts.opt_pointer_analysis && innernd->pointsToFunction && FunctionName) {
        resultList = NULL;
        PointerAnalysis_LookupLinearNodePointerExpr(FunctionName, innernd, &resultList);
        if (resultList) {
            for (scan = resultList; scan; scan = scan->nextList) {
                if (!scan->list.head || !scan->list.tail) {
                    result = TRUE;
                    break;
                }

                foundObjRef = FALSE;
                for (scannd = scan->list.head; scannd != scan->list.tail->next;
                     scannd = scannd->next) {
                    if (scannd->type == IROLinearOperand && scannd->u.node->type == EOBJREF) {
                        foundObjRef = TRUE;
                        obj = scannd->u.node->data.objref;
                        CError_ASSERT(234, obj != NULL);

                        depsList = NULL;
                        PointerAnalysis_GetFunctionDependencies(obj, nd, &depsList);

                        for (olist = depsList; olist; olist = olist->next) {
                            if (!olist->object) {
                                result = TRUE;
                                break;
                            }
                        }

                        while (depsList) {
                            olist = depsList->next;
                            IRO_free(depsList);
                            depsList = olist;
                        }

                        if (result) {
                            break;
                        }
                    }
                }

                if (!foundObjRef) {
                    result = TRUE;
                }
                if (result) {
                    break;
                }
            }

            if (!result) {
                for (scan = resultList; scan; scan = scan->nextList) {
                    for (scannd = scan->list.head; scannd != scan->list.tail->next;
                         scannd = scannd->next) {
                        if (scannd->type == IROLinearOperand && scannd->u.node->type == EOBJREF) {
                            obj = scannd->u.node->data.objref;

                            depsList = NULL;
                            PointerAnalysis_GetFunctionDependencies(obj, nd, &depsList);

                            for (olist = depsList; olist; olist = olist->next) {
                                var = IRO_FindVar(olist->object, TRUE, TRUE);
                                CError_ASSERT(285, var != NULL);
                                index = var->index;
                                CError_ASSERT(287, index != 0);

                                if (is_volatile_object(olist->object)) {
                                    IRO_IsVolatile = TRUE;
                                    IRO_NotSubable = TRUE;
                                }
                                Bv_SetBit(index, IRO_Depends);
                            }

                            while (depsList) {
                                olist = depsList->next;
                                IRO_free(depsList);
                                depsList = olist;
                            }
                        }
                    }
                }
            }

            while (resultList) {
                next = resultList->nextList;
                IRO_free(resultList);
                resultList = next;
            }
        } else {
            result = TRUE;
        }
    } else {
        result = TRUE;
    }

    if (result) {
        IRO_DependsOnForDataFlow(nd->u.funccall.callee, FALSE);
        Bv_Or(IRO_FuncKills, IRO_Depends);

        for (i = nd->u.funccall.argCount - 1; i >= 0; i--) {
            IRO_DependsOnForDataFlow(nd->u.funccall.args[i], FALSE);
        }
    }
}

static void IRO_DependsOn(IROLinear* linear, Boolean isIndirect) {
    VarRecord* var;
    IROLinear* inner;

    if (linear->rtype && CParser_IsVolatile(linear->rtype, linear->nodeflags & ENODE_FLAG_QUALS)) {
        IRO_IsVolatile = TRUE;
        IRO_NotSubable = TRUE;
    }

    if (IRO_NotSubable) {
        return;
    }

    switch (linear->type) {
        case IROLinearOperand:
            if (isIndirect && linear->u.node->type == EOBJREF) {
                if ((var = IRO_FindVar(linear->u.node->data.objref, FALSE, TRUE))) {
                    if (is_volatile_object(var->object)) {
                        IRO_IsVolatile = TRUE;
                        IRO_NotSubable = TRUE;
                    }
                    Bv_SetBit(var->index, IRO_Depends);
                } else {
                    IRO_NotSubable = TRUE;
                }
            }
            break;
        case IROLinearOp1Arg:
            if (IRO_IsAssignOp[linear->nodetype]) {
                IRO_NotSubable = TRUE;
                return;
            }
            inner = linear->u.monadic;
            if (linear->nodetype == EINDIRECT) {
                if (inner->type == IROLinearOp1Arg && inner->nodetype == EBITFIELD) {
                    inner = inner->u.monadic;
                }
                if (inner->type != IROLinearOperand || inner->u.node->type != EOBJREF) {
                    GetDependsOfIndirect(linear);
                }
            }

            IRO_DependsOn(inner, linear->nodetype == EINDIRECT);
            break;
        case IROLinearOp2Arg:
            if (IRO_IsAssignOp[linear->nodetype]) {
                IRO_NotSubable = TRUE;
                return;
            }
            if (linear->nodetype == EDIV || linear->nodetype == EMODULO) {
                if (IRO_IsIntConstant(linear->u.diadic.right)) {
                    if (CInt64_Equal(linear->u.diadic.right->u.node->data.intval, cint64_zero)) {
                        IRO_CouldError = TRUE;
                    }
                } else {
                    IRO_CouldError = TRUE;
                }
            }

            IRO_DependsOn(linear->u.diadic.left, isIndirect);
            IRO_DependsOn(linear->u.diadic.right, isIndirect);
            break;
        case IROLinearOp3Arg:
            if (IRO_IsAssignOp[linear->nodetype]) {
                IRO_NotSubable = TRUE;
                return;
            }
            IRO_DependsOn(linear->u.args3.a, isIndirect);
            IRO_DependsOn(linear->u.args3.b, isIndirect);
            IRO_DependsOn(linear->u.args3.c, isIndirect);
            break;
        case IROLinearFunccall:
            IRO_NotSubable = TRUE;
            break;
        default:
            CError_FATAL(479);
    }
}

static void IRO_DependsOnForDataFlow(IROLinear* linear, Boolean isIndirect) {
    VarRecord* var;
    IROLinear* inner;

    if (linear->rtype && CParser_IsVolatile(linear->rtype, linear->nodeflags & ENODE_FLAG_QUALS)) {
        IRO_IsVolatile = TRUE;
        IRO_NotSubable = TRUE;
    }

    switch (linear->type) {
        case IROLinearOperand:
            if (isIndirect && linear->u.node->type == EOBJREF) {
                if ((var = IRO_FindVar(linear->u.node->data.objref, FALSE, TRUE))) {
                    if (is_volatile_object(var->object)) {
                        IRO_IsVolatile = TRUE;
                        IRO_NotSubable = TRUE;
                    }
                    Bv_SetBit(var->index, IRO_Depends);
                } else {
                    IRO_NotSubable = TRUE;
                }
            }
            break;
        case IROLinearOp1Arg:
            if (IRO_IsAssignOp[linear->nodetype]) {
                IRO_NotSubable = TRUE;
            }

            inner = linear->u.monadic;
            if (linear->nodetype == EINDIRECT) {
                if (inner->type == IROLinearOp1Arg && inner->nodetype == EBITFIELD) {
                    inner = inner->u.monadic;
                }
                if (inner->type != IROLinearOperand || inner->u.node->type != EOBJREF) {
                    GetDependsOfIndirect(linear);
                }
            }

            IRO_DependsOnForDataFlow(inner, linear->nodetype == EINDIRECT);
            break;
        case IROLinearOp2Arg:
            if (IRO_IsAssignOp[linear->nodetype]) {
                IRO_NotSubable = TRUE;
            }

            if (linear->nodetype == EDIV || linear->nodetype == EMODULO) {
                if (IRO_IsIntConstant(linear->u.diadic.right)) {
                    if (CInt64_Equal(linear->u.diadic.right->u.node->data.intval, cint64_zero)) {
                        IRO_CouldError = TRUE;
                    }
                } else {
                    IRO_CouldError = TRUE;
                }
            }

            IRO_DependsOnForDataFlow(linear->u.diadic.left, isIndirect);
            IRO_DependsOnForDataFlow(linear->u.diadic.right, isIndirect);
            break;
        case IROLinearOp3Arg:
            if (IRO_IsAssignOp[linear->nodetype]) {
                IRO_NotSubable = TRUE;
            }

            IRO_DependsOnForDataFlow(linear->u.args3.a, isIndirect);
            IRO_DependsOnForDataFlow(linear->u.args3.b, isIndirect);
            IRO_DependsOnForDataFlow(linear->u.args3.c, isIndirect);
            break;
        case IROLinearFunccall:
            IRO_NotSubable = TRUE;
            GetDependsOfFunctionCallForDataFlow(linear);
            break;
        default:
            CError_FATAL(650);
    }
}

void IRO_FindDepends_NoAlloc(IROLinear* linear) {
    Bv_Clear(IRO_Depends);
    IRO_CouldError = FALSE;
    IRO_NotSubable = FALSE;
    IRO_IsVolatile = FALSE;
    IRO_DependsOnForDataFlow(linear, FALSE);
}

void IRO_FindDepends(IROLinear* linear) {
    Bv_AllocVector(&IRO_Depends, IRO_NumVars + 1);
    IRO_CouldError = FALSE;
    IRO_NotSubable = FALSE;
    IRO_DependsOn(linear, FALSE);
}

static void VecAct(IROLinear* linear, Boolean isFirst) {
    if (!isFirst && (linear->flags & IROLF_VecOpBase)) {
        HasVectorOperand = TRUE;
    }
}

static Boolean IRO_DoesNotHaveVectorOperand(IROLinear* linear) {
    HasVectorOperand = FALSE;
    IRO_WalkTree(linear, VecAct);
    return !HasVectorOperand;
}

static void IRO_AddExpression(IROLinear* linear, IRONode* node, Boolean unused) {
    IROExpr* expr;

    if ((linear->flags & IROLF_Reffed) &&
        IRO_IsSubableExpression(linear) &&
        IRO_DoesNotHaveVectorOperand(linear)) {
        expr = oalloc(sizeof(IROExpr));
        expr->flags = FALSE;
        expr->index = ++IRO_NumExprs;
        expr->linear = linear;
        expr->temp = NULL;
        expr->node = node;
        IRO_FindDepends(linear);
        expr->depends = IRO_Depends;
        expr->notSubable = IRO_NotSubable;
        expr->couldError = IRO_CouldError;
        expr->next = NULL;
        expr->use = NULL;
        if (IRO_FirstExpr) {
            IRO_LastExpr->next = expr;
        } else {
            IRO_FirstExpr = expr;
        }
        IRO_LastExpr = expr;
        linear->expr = expr;
    }
}

void IRO_FindExpressions(BitVector* bv, Boolean unused) {
    IROLinear* nd;
    IRONode* fnode;

    IRO_FirstExpr = IRO_LastExpr = NULL;
    IRO_NumExprs = 0;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        if (!bv || Bv_IsBitSet(fnode->index, bv)) {
            for (nd = fnode->first; nd; nd = nd->next) {
                nd->expr = NULL;
                IRO_AddExpression(nd, fnode, unused);
                if (nd == fnode->last) {
                    break;
                }
            }
        } else {
            for (nd = fnode->first; nd; nd = nd->next) {
                nd->expr = NULL;
                if (nd == fnode->last) {
                    break;
                }
            }
        }
    }

    IRO_CheckForUserBreak();
}

void IRO_RemoveExpr(IROExpr* expr) {
    IROExpr* prev;
    IROExpr* scan;

    scan = IRO_FirstExpr;
    prev = NULL;
    while (scan != expr) {
        prev = scan;
        scan = scan->next;
        CError_ASSERT(809, scan);
    }

    expr->linear->expr = NULL;
    if (prev) {
        prev->next = expr->next;
    } else {
        IRO_FirstExpr = expr->next;
    }
}

static void GetExprKillsByIndirectAssignment(IROLinear* linear) {
    int index;
    IROListNode* scan;
    IROLinear* inner;
    VarRecord* var;
    Boolean foundObjRef;
    IROListNode* resultList;
    IROLinear* scannd;
    Boolean result;
    IROListNode* next;
    Object* obj;
    IROExpr* expr;
    IROListNode* list;

    result = FALSE;
    if (linear->type == IROLinearOp2Arg) {
        linear = linear->u.diadic.left;
    } else {
        linear = linear->u.monadic;
    }

    if (linear &&
        linear->type == IROLinearOp1Arg &&
        linear->nodetype == EINDIRECT &&
        (inner = linear->u.monadic) &&
        copts.opt_pointer_analysis &&
        inner->pointsToFunction &&
        FunctionName) {
        resultList = NULL;
        PointerAnalysis_LookupLinearNodePointerExpr(FunctionName, inner, &resultList);
        if (resultList) {
            for (scan = resultList; scan; scan = scan->nextList) {
                if (!scan->list.head || !scan->list.tail) {
                    result = TRUE;
                    break;
                }

                foundObjRef = FALSE;
                for (scannd = scan->list.head; scannd != scan->list.tail->next;
                     scannd = scannd->next) {
                    if (scannd->type == IROLinearOperand && scannd->u.node->type == EOBJREF) {
                        foundObjRef = TRUE;
                        break;
                    }
                }

                if (!foundObjRef) {
                    result = TRUE;
                    break;
                }
            }

            if (!result) {
                for (list = resultList; list; list = list->nextList) {
                    for (scannd = list->list.head; scannd != list->list.tail->next;
                         scannd = scannd->next) {
                        if (scannd->type == IROLinearOperand && scannd->u.node->type == EOBJREF) {
                            obj = scannd->u.node->data.objref;
                            CError_ASSERT(893, obj != NULL);
                            var = IRO_FindVar(obj, TRUE, TRUE);
                            CError_ASSERT(895, var != NULL);
                            index = var->index;

                            for (expr = IRO_FirstExpr; expr; expr = expr->next) {
                                if (Bv_IsBitSet(index, expr->depends)) {
                                    Bv_SetBit(expr->index, IRO_ExprKills);
                                }
                            }
                        }
                    }
                }
            }

            while (resultList) {
                next = resultList->nextList;
                IRO_free(resultList);
                resultList = next;
            }
        } else {
            result = TRUE;
        }
    } else {
        result = TRUE;
    }

    if (result) {
        for (expr = IRO_FirstExpr; expr; expr = expr->next) {
            if (Bv_BitsInCommon(expr->depends, IRO_IndirectKills)) {
                Bv_SetBit(expr->index, IRO_ExprKills);
            }
        }
    }
}

static void GetExprKillsByFunctionCall(IROLinear* funccall) {
    IROListNode* list;
    VarRecord* var;
    IROListNode* resultList;
    IROLinear* innernd;
    ObjectList* depsList;
    IROListNode* scan;
    ObjectList* olist;
    IROExpr* expr;
    Boolean foundObjRef;
    Boolean result;
    IROLinear* scannd;
    int index;
    Object* obj;
    IROListNode* next;

    result = FALSE;
    innernd = funccall->u.funccall.callee;

    if (innernd && copts.opt_pointer_analysis && innernd->pointsToFunction && FunctionName) {
        resultList = NULL;
        PointerAnalysis_LookupLinearNodePointerExpr(FunctionName, innernd, &resultList);
        if (resultList) {
            for (scan = resultList; scan; scan = scan->nextList) {
                if (!scan->list.head || !scan->list.tail) {
                    result = TRUE;
                    break;
                }

                foundObjRef = FALSE;
                for (scannd = scan->list.head; scannd != scan->list.tail->next;
                     scannd = scannd->next) {
                    if (scannd->type == IROLinearOperand && scannd->u.node->type == EOBJREF) {
                        foundObjRef = TRUE;
                        obj = scannd->u.node->data.objref;
                        CError_ASSERT(991, obj != NULL);

                        depsList = NULL;
                        PointerAnalysis_GetFunctionKills(obj, funccall, &depsList);

                        for (olist = depsList; olist; olist = olist->next) {
                            if (!olist->object) {
                                result = TRUE;
                                break;
                            }
                        }

                        while (depsList) {
                            olist = depsList->next;
                            IRO_free(depsList);
                            depsList = olist;
                        }

                        if (result) {
                            break;
                        }
                    }
                }

                if (!foundObjRef) {
                    result = TRUE;
                }
                if (result) {
                    break;
                }
            }

            if (!result) {
                for (list = resultList; list; list = list->nextList) {
                    for (scannd = list->list.head; scannd != list->list.tail->next;
                         scannd = scannd->next) {
                        if (scannd->type == IROLinearOperand && scannd->u.node->type == EOBJREF) {
                            obj = scannd->u.node->data.objref;

                            depsList = NULL;
                            PointerAnalysis_GetFunctionKills(obj, funccall, &depsList);

                            for (olist = depsList; olist; olist = olist->next) {
                                var = IRO_FindVar(olist->object, TRUE, TRUE);
                                CError_ASSERT(1042, var != NULL);
                                index = var->index;
                                CError_ASSERT(1044, index != 0);

                                for (expr = IRO_FirstExpr; expr; expr = expr->next) {
                                    if (Bv_IsBitSet(index, expr->depends)) {
                                        Bv_SetBit(expr->index, IRO_ExprKills);
                                    }
                                }
                            }

                            while (depsList) {
                                olist = depsList->next;
                                IRO_free(depsList);
                                depsList = olist;
                            }
                        }
                    }
                }
            }

            while (resultList) {
                next = resultList->nextList;
                IRO_free(resultList);
                resultList = next;
            }
        } else {
            result = TRUE;
        }
    } else {
        result = TRUE;
    }

    if (result) {
        for (expr = IRO_FirstExpr; expr; expr = expr->next) {
            if (Bv_BitsInCommon(expr->depends, IRO_IndirectKills)) {
                Bv_SetBit(expr->index, IRO_ExprKills);
            }
        }
    }
}

static void IRO_GetExprKills(IROLinear* linear) {
    VarRecord* var;
    int index;
    IROExpr* expr;

    Bv_Clear(IRO_ExprKills);
    switch (linear->type) {
        case IROLinearOp1Arg:
        case IROLinearOp2Arg:
            if (IRO_IsAssignOp[linear->nodetype]) {
                var = IRO_FindAssigned(linear);
                index = 0;
                if (var) {
                    index = var->index;
                }
                if (!index) {
                    GetExprKillsByIndirectAssignment(linear);
                } else {
                    for (expr = IRO_FirstExpr; expr; expr = expr->next) {
                        if (Bv_IsBitSet(index, expr->depends)) {
                            Bv_SetBit(expr->index, IRO_ExprKills);
                        }
                    }
                }
            }
            break;
        case IROLinearAsm:
#if VERSION == VERSION_WII_1_0RC1
            Bv_Clear(IRO_VarKills);
#endif
            IRO_GetKills(linear);
            for (expr = IRO_FirstExpr; expr; expr = expr->next) {
                if (Bv_BitsInCommon(expr->depends, IRO_VarKills)) {
                    Bv_SetBit(expr->index, IRO_ExprKills);
                }
            }
            break;
        case IROLinearFunccall:
            GetExprKillsByFunctionCall(linear);
            break;
        default:
            break;
    }
}

#if VERSION == VERSION_WII_1_0RC1
static UInt32 GroupCommonSubexpressions(void) {
    IROExpr** representatives = oalloc((IRO_NumExprs + 1) * sizeof(IROExpr*));
    IROExpr* expr;
    UInt32 count = 0;
    UInt32 group;

    for (expr = IRO_FirstExpr; expr; expr = expr->next) {
        expr->cseGroup = 0;
        expr->cseDefinitions = NULL;
        expr->cseFromPredecessors = FALSE;
        if (expr->notSubable || IRO_HasSideEffect(expr->linear) ||
            IRO_ExprHasFloatingPointEffects(expr->linear)) {
            continue;
        }
        for (group = 1; group <= count; group++) {
            if (IRO_ExprsSameSemantically(expr->linear, representatives[group]->linear)) {
                break;
            }
        }
        if (group > count) {
            count = group;
        }
        representatives[group] = expr;
        expr->cseGroup = group;
    }
    return count;
}

static void IRO_GetUExprKills(IROLinear* linear, BitVector* kills) {
    IROExpr* expr;

    Bv_Clear(kills);
    IRO_GetExprKills(linear);
    if (Bv_IsEmpty(IRO_ExprKills)) {
        return;
    }
    for (expr = IRO_FirstExpr; expr; expr = expr->next) {
        if (expr->cseGroup && Bv_IsBitSet(expr->index, IRO_ExprKills)) {
            Bv_SetBit(expr->cseGroup, kills);
        }
    }
}

static void ComputeAvailability(UInt32 expression_count, BitVector* kills) {
#else
void IRO_ComputeAvail(void) {
    UInt32 expression_count = IRO_NumExprs;
    BitVector* kills;
#endif
    IRONode* node;
    IROLinear* linear;
    SInt32 counter;
    BitVector* bv;
    Boolean changed;

    counter = 0;
    node = IRO_FirstNode;
    Bv_AllocVector(&IRO_VarKills, IRO_NumVars + 1);
    Bv_AllocVector(&IRO_ExprKills, IRO_NumExprs + 1);
#if VERSION != VERSION_WII_1_0RC1
    kills = IRO_ExprKills;
#endif

    while (node) {
        Bv_AllocVector(&node->in, expression_count);
        if (node->numpred) {
            Bv_Set(node->in);
        }
        Bv_AllocVector(&node->kill, expression_count);
        Bv_AllocVector(&node->gen, expression_count);
        Bv_AllocVector(&node->out, expression_count);

        for (linear = node->first; linear; linear = linear->next) {
#if VERSION == VERSION_WII_1_0RC1
            if (linear->expr && linear->expr->cseGroup) {
                Bv_SetBit(linear->expr->cseGroup, node->gen);
            }
            IRO_GetUExprKills(linear, kills);
#else
            if (linear->expr) {
                Bv_SetBit(linear->expr->index, node->gen);
            }
            IRO_GetExprKills(linear);
#endif
            Bv_Or(kills, node->kill);
            Bv_Minus(kills, node->gen);
            if (linear == node->last) {
                break;
            }

            if (counter > 250) {
                IRO_CheckForUserBreak();
                counter = 0;
            } else {
                counter++;
            }
        }

        Bv_Copy(node->in, node->out);
        Bv_Minus(node->kill, node->out);
        Bv_Or(node->gen, node->out);
        node = node->nextnode;
    }

    IRO_CheckForUserBreak();

    Bv_AllocVector(&bv, expression_count);
    do {
        IRONode* scan;
        changed = FALSE;
        for (scan = IRO_FirstNode; scan; scan = scan->nextnode) {
            if (!scan->numpred) {
                Bv_Clear(bv);
            } else {
                UInt16 i;
                Bv_Set(bv);
                for (i = 0; i < scan->numpred; i++) {
                    Bv_And(IRO_NodeTable[scan->pred[i]]->out, bv);
                }
            }

            if (!Bv_Compare(bv, scan->in)) {
                changed = TRUE;
                Bv_Copy(bv, scan->in);
            }

            Bv_Copy(scan->in, scan->out);
            Bv_Minus(scan->kill, scan->out);
            Bv_Or(scan->gen, scan->out);
        }
        IRO_CheckForUserBreak();
    } while (changed);
}

#if VERSION == VERSION_WII_1_0RC1
static IROLinear* IRO_MakeReplacementEmbedded(IROExpr* expr) {
#else
static void IRO_MakeReplacementEmbedded(IROExpr* expr) {
#endif
    IROLinear* opnd;
    IROLinear* ind;
    IROLinear* ass;

#if VERSION != VERSION_WII_1_0RC1
    IRO_GetTemp(expr);
#endif

    opnd = IRO_NewLinear(IROLinearOperand);
    opnd->u.node = create_objectrefnode(expr->temp);
    opnd->rtype = opnd->u.node->data.objref->type;
    opnd->index = ++IRO_NumLinear;
    opnd->flags |= IROLF_Reffed | IROLF_Assigned | IROLF_Ind;

    ind = IRO_NewLinear(IROLinearOp1Arg);
    ind->nodetype = EINDIRECT;
    ind->rtype = expr->linear->rtype;
    ind->u.monadic = opnd;
    ind->index = ++IRO_NumLinear;
    ind->flags |= IROLF_Reffed | IROLF_Assigned;

    ass = IRO_NewLinear(IROLinearOp2Arg);
    ass->nodetype = EASS;
    ass->u.diadic.left = ind;
    ass->u.diadic.right = expr->linear;
    ass->rtype = expr->linear->rtype;
    ass->index = ++IRO_NumLinear;

    opnd->next = ind;
    ind->next = ass;
    IRO_ReplaceReferenceWithNode(expr->linear, ass);
    IRO_PasteAfter(opnd, ass, expr->linear);
#if VERSION == VERSION_WII_1_0RC1
    return ass;
#endif
}

#if VERSION != VERSION_WII_1_0RC1
static void IRO_ActUnmarkSubExpressions(IROLinear* linear, Boolean isFirst) {
    if (isFirst) {
        linear->flags &= ~IROLF_ReplaceCSE;
    }
}

static void CheckCommonSub(IROLinear* linear) {
    IROExpr* expr;

    for (expr = IRO_FirstExpr; expr; expr = expr->next) {
        if (expr->linear != linear && !expr->use) {
            if (Bv_IsBitSet(expr->index, IRO_Avail) &&
                !expr->notSubable &&
                IRO_ExprsSame(linear, expr->linear)) {
                IRO_WalkTree(linear, IRO_ActUnmarkSubExpressions);
                linear->flags |= IROLF_ReplaceCSE;
                linear->expr->use = expr;
                break;
            }
        }
    }
}
#endif

#if VERSION == VERSION_WII_1_0RC1
static void MoveCommonSub(IROExpr* expr) {
    IROLinear* assignment = IRO_MakeReplacementEmbedded(expr);
    IROList copy;
    IROList reference;

    if (!(assignment->flags & IROLF_Reffed)) {
        return;
    }

    IRO_Dump("Moving common sub from node %d before statement starting at %d\n",
             expr->linear->index, expr->cseStatementStart->index);
    /* Preserve the definition if the enclosing expression is replaced. */
    IRO_InitList(&copy);
    IRO_DuplicateExpr(assignment, &copy);
    copy.tail->flags &= ~IROLF_Reffed;
    IRO_Paste(copy.head, copy.tail, expr->cseStatementStart);

    IRO_InitList(&reference);
    IRO_DuplicateExpr(assignment->u.diadic.left, &reference);
    IRO_PasteAfter(reference.head, reference.tail, assignment);
    IRO_LocateFather_Cut_And_Paste(assignment, reference.tail);
}
#else
static void MoveCommonSub(IROExpr* expr) {
    IROExpr* scanexpr;
    SInt32 sz1;
    SInt32 sz2;
    SInt32 i1;
    SInt32 i2;
    IROLinear* scan;
    IROLinear* array1[64];
    IROLinear* array2[64];
    SInt32 best;

    sz1 = 0;
    scan = expr->linear;
    do {
        scan = IRO_LocateFather(scan);
        if (scan) {
            if (sz1 == 64) {
                return;
            }
            array1[sz1++] = scan;
        }
    } while (scan);

    best = -1;
    for (scanexpr = IRO_FirstExpr; scanexpr; scanexpr = scanexpr->next) {
        if (scanexpr->use == expr) {
            scan = scanexpr->linear;
            sz2 = 0;
            do {
                scan = IRO_LocateFather(scan);
                if (scan) {
                    if (sz2 == 64) {
                        return;
                    }
                    array2[sz2++] = scan;
                }
            } while (scan);

            i1 = sz1;
            i2 = sz2;
            while (i1 && i2 && array1[i1 - 1] == array2[i2 - 1]) {
                i1--;
                i2--;
            }

            if (i1 != sz1 && i1 > best) {
                best = i1;
            }
        }
    }

    if (best < 0) {
        IRO_MakeReplacementEmbedded(expr);
    } else {
        IROLinear* start;
        IROLinear* comma;
        IRO_Dump(
            "Moving common sub from node %d to %d\n", expr->linear->index, array1[best]->index
        );
        start = IRO_FindStart(array1[best]);
        IRO_GetTemp(expr);
        IRO_ReplaceReference(expr->linear, expr->temp, expr->linear);
        IRO_MoveExpression(expr, start);

        comma = IRO_NewLinear(IROLinearOp2Arg);
        comma->nodetype = ECOMMA;
        comma->rtype = array1[best]->rtype;
        comma->u.diadic.left = IRO_AssignToTemp(expr);
        comma->u.diadic.right = array1[best];
        comma->stmt = array1[best]->stmt;
        IRO_ReplaceReferenceWithNode(array1[best], comma);
        IRO_PasteAfter(comma, comma, array1[best]);
    }
}

static void ReplaceCommonSub(IROLinear* linear) {
    IROExpr* expr = linear->expr->use;
    if (!expr->temp) {
        MoveCommonSub(expr);
        if (!expr->temp) {
            return;
        }
    }

    IRO_Dump("Replacing common sub at %d with %d\n", linear->index, expr->linear->index);
    IRO_ReplaceReference(linear, expr->temp, linear);
    IRO_RemoveExpr(linear->expr);
    IRO_NopOut(linear);
}

void IRO_CommonSubs(void) {
    IRONode* node;
    IROLinear* linear;
    SInt32 counter;

    counter = 0;
    for (node = IRO_FirstNode; node; node = node->nextnode) {
        IRO_Avail = node->in;
        linear = node->first;
        while (1) {
            if (!linear) {
                break;
            }
            if (linear->expr && !linear->expr->notSubable) {
                CheckCommonSub(linear);
            }
            if (linear->expr) {
                Bv_SetBit(linear->expr->index, IRO_Avail);
            }
            IRO_GetExprKills(linear);
            Bv_Minus(IRO_ExprKills, IRO_Avail);
            if (linear == node->last) {
                break;
            }
            if (counter > 250) {
                IRO_CheckForUserBreak();
                counter = 0;
            } else {
                counter++;
            }
            linear = linear->next;
        }
    }

    for (node = IRO_FirstNode; node; node = node->nextnode) {
        for (linear = node->first; linear; linear = linear->next) {
            if (linear->expr && (linear->flags & IROLF_ReplaceCSE) && !IRO_HasSideEffect(linear)) {
                ReplaceCommonSub(linear);
            }
            if (linear == node->last) {
                break;
            }
            if (counter > 250) {
                IRO_CheckForUserBreak();
                counter = 0;
            } else {
                counter++;
            }
        }
    }

    IRO_CheckForUserBreak();
}
#endif

#if VERSION == VERSION_WII_1_0RC1
typedef struct IROCSEDefinition {
    IROExpr* expr;
    struct IROCSEDefinition* next;
} IROCSEDefinition;

static IROCSEDefinition* NewCommonSubDefinition(IROExpr* expr) {
    IROCSEDefinition* result = oalloc(sizeof(IROCSEDefinition));
    result->expr = expr;
    result->next = NULL;
    return result;
}

static void LinkLocalCommonSubs(UInt32 count, BitVector* kills) {
    IRONode* node;
    IROLinear* linear;
    IROExpr** definitions = oalloc((count + 1) * sizeof(IROExpr*));
    BitVector* local;
    BitVector* seen;
    BitVector* incoming;
    UInt32 group;

    Bv_AllocVector(&local, count);
    Bv_AllocVector(&seen, count);
    Bv_AllocVector(&incoming, count);
    for (node = IRO_FirstNode; node; node = node->nextnode) {
        Bv_Clear(local);
        Bv_Clear(seen);
        Bv_Copy(node->in, incoming);
        for (linear = node->first; linear; linear = linear->next) {
            IROExpr* expr = linear->expr;
            if (expr && (group = expr->cseGroup)) {
                if (Bv_IsBitSet(group, local)) {
                    expr->cseDefinitions = NewCommonSubDefinition(definitions[group]);
                } else {
                    if (!Bv_IsBitSet(group, seen) && Bv_IsBitSet(group, incoming)) {
                        expr->cseFromPredecessors = TRUE;
                    }
                    definitions[group] = expr;
                }
                Bv_SetBit(group, local);
                Bv_SetBit(group, seen);
                Bv_SetBit(group, incoming);
            }
            IRO_GetUExprKills(linear, kills);
            Bv_Minus(kills, local);
            Bv_Minus(kills, incoming);
            if (linear == node->last) {
                break;
            }
        }
        IRO_CheckForUserBreak();
    }
}

static IROCSEDefinition** FindCommonSubDefinitions(
    IRONode* node, UInt32 group, BitVector* visited, IROCSEDefinition** tail
) {
    IROExpr* reaching = NULL;
    IROLinear* linear;
    UInt16 i;

    if (Bv_IsBitSet(node->index, visited) || !Bv_IsBitSet(group, node->out)) {
        return tail;
    }
    Bv_SetBit(node->index, visited);
    for (linear = node->first; linear; linear = linear->next) {
        IROExpr* expr = linear->expr;
        if (expr && expr->cseGroup == group && !expr->cseDefinitions) {
            reaching = expr;
        }
        if (linear == node->last) {
            break;
        }
    }
    if (reaching && !reaching->cseFromPredecessors) {
        *tail = NewCommonSubDefinition(reaching);
        return &(*tail)->next;
    }
    for (i = 0; i < node->numpred; i++) {
        tail = FindCommonSubDefinitions(IRO_NodeTable[node->pred[i]], group, visited, tail);
    }
    return tail;
}

static void LinkIncomingCommonSubs(void) {
    IROExpr* expr;
    BitVector* visited;

    Bv_AllocVector(&visited, IRO_NumNodes);
    for (expr = IRO_FirstExpr; expr; expr = expr->next) {
        if (expr->cseFromPredecessors) {
            IRONode* node = expr->node;
            IROCSEDefinition** tail = &expr->cseDefinitions;
            UInt16 i;

            Bv_Clear(visited);
            for (i = 0; i < node->numpred; i++) {
                tail = FindCommonSubDefinitions(IRO_NodeTable[node->pred[i]], expr->cseGroup, visited, tail);
            }
        }
    }
}

static void AllocateCommonSubTemps(void) {
    IROExpr* expr;
    IROCSEDefinition* def;
    Boolean changed;

    do {
        changed = FALSE;
        for (expr = IRO_FirstExpr; expr; expr = expr->next) {
            Object* temp = NULL;
            if (!expr->cseDefinitions) {
                continue;
            }
            for (def = expr->cseDefinitions; def; def = def->next) {
                if (def->expr->temp && (!temp || (size_t)def->expr->temp < (size_t)temp)) {
                    temp = def->expr->temp;
                }
            }
            if (!temp) {
                IRO_GetTemp(expr->cseDefinitions->expr);
                temp = expr->cseDefinitions->expr->temp;
            }
            for (def = expr->cseDefinitions; def; def = def->next) {
                if (def->expr->temp && def->expr->temp != temp) {
                    changed = TRUE;
                }
                def->expr->temp = temp;
            }
        }
        IRO_CheckForUserBreak();
    } while (changed);
}

static void RewriteCommonSubs(void) {
    IRONode* node;

    for (node = IRO_FirstNode; node; node = node->nextnode) {
        IROLinear* start = node->first;
        IROLinear* linear;
        for (linear = start; linear; linear = linear->next) {
            IROExpr* expr = linear->expr;
            if (expr && expr->temp) {
                /* MoveCommonSub keeps the oracle hook's one-argument signature. */
                expr->cseStatementStart = start;
                MoveCommonSub(expr);
            } else if (expr && expr->cseDefinitions) {
                IROExpr* def = expr->cseDefinitions->expr;
                IRO_Dump("Replacing common sub at %d with %d\n", linear->index, def->linear->index);
                IRO_ReplaceReference(linear, def->temp, linear);
                IRO_RemoveExpr(expr);
                IRO_NopOut(linear);
            }
            if (!(linear->flags & IROLF_Reffed) &&
                (linear->type != IROLinearNop || linear == start)) {
                start = linear->next;
            }
            if (linear == node->last) {
                break;
            }
        }
        IRO_CheckForUserBreak();
    }
}

void IRO_CommonSubs(void) {
    UInt32 count;
    BitVector* kills;

    IRO_FindExpressions(NULL, FALSE);
    count = GroupCommonSubexpressions();
    Bv_AllocVector(&kills, count);
    ComputeAvailability(count, kills);
    LinkLocalCommonSubs(count, kills);
    LinkIncomingCommonSubs();
    AllocateCommonSubTemps();
    RewriteCommonSubs();
    IRO_UpdateFlagsOnInts();
}
#endif

static Boolean CountThisSubableOperandUse(IROUse* use) {
    return use->reachingDefCount != 0;
}

static int GetSubableOperandUseCount(VarRecord* var) {
    int count = 0;
    IROUse* use;

    if (var->uses) {
        for (use = var->uses; use; use = use->varnext) {
            if (CountThisSubableOperandUse(use)) {
                count++;
            }
        }
    }

    return count;
}

static void IRO_MakeTopLevelExprForSubableOperand(IROLinear* linear) {
    IROLinear* copy = IRO_NewLinear(IROLinearOperand);
    memcpy(copy, linear, sizeof(IROLinear));
    copy->index = ++IRO_NumLinear;

    if (IRO_FirstLinear && IRO_FirstLinear->type == IROLinearNop) {
        IRO_PasteAfter(copy, copy, IRO_FirstLinear);
    } else {
        IRO_Paste(copy, copy, IRO_FirstLinear);
    }
}

void IRO_GenerateTopLevelExprsForSubableOperands(void) {
    IROLinear* nd;
    IRONode* fnode;
    VarRecord* var;
    BitVector* bv;

    Bv_AllocVector(&bv, IRO_NumVars + 1);

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        for (nd = fnode->first; nd; nd = nd->next) {
            nd->expr = NULL;
            if ((nd->flags & IROLF_Reffed) &&
                IRO_IsSubableExpression(nd) &&
                IRO_DoesNotHaveVectorOperand(nd)) {
                if (nd->type == IROLinearOperand && nd->u.node && nd->u.node->type == EOBJREF) {
                    if ((var = IRO_FindVar(nd->u.node->data.objref, FALSE, TRUE))) {
                        if (!Bv_IsBitSet(var->index, bv)) {
                            IRO_MakeTopLevelExprForSubableOperand(nd);
                            Bv_SetBit(var->index, bv);
                        }
                    }
                }
            }

            if (nd == fnode->last) {
                break;
            }
        }
    }
}
