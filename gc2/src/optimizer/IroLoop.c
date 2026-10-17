#include "compiler/IroLoop.h"
#include "compiler/IroCSE.h"
#include "compiler/IroDump.h"
#include "compiler/IroFlowgraph.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroPropagate.h"
#include "compiler/IroSubable.h"
#include "compiler/IroUtil.h"
#include "compiler/IroVars.h"
#include "compiler/BitVector.h"
#include "compiler/CError.h"
#include "compiler/CFunc.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/Switch.h"
#include "compiler/types.h"
#include "version.h"
#include <string.h>

// Pro7: in IroLoop.h; nothing else uses it
typedef enum IROLoopMemRefFlags {
    LoopMemRef_1 = 1,
    LoopMemRef_2 = 2,
    LoopMemRef_4 = 4,
    LoopMemRef_8 = 8,
    LoopMemRef_10 = 0x10
} IROLoopMemRefFlags;

// A memory reference through a loop-invariant address (MyHandleLoop_Motion's candidates for
// moving out of the loop). The flags are a byte enum, so the packing places the pointers.
#pragma pack(push, 2) // Pro7: options align=mac68k
typedef struct IROLoopMemRef {
    IROLoopMemRefFlags flags;
    IROLinear* nd;
    IROElmList* list;
    IROAddrRecord* rec;
    struct IROLoopMemRef* next;
} IROLoopMemRef;
#pragma pack(pop)

IRONode* LoopNode;
Boolean ConditionalHeaderAtBottom;
IROLoopInd* FirstInd;
BitVector* InLoop;
BitVector* InLoop_Exits;
BitVector* InLoop_Tails;
UInt32 LoopExitNumber;
UInt32 LoopTailNum;
IRONode* LoopExitSuccessor;
IRONode* LoopTail;
IROLoopMemRef* IRO_LoopMemRefFirst;
IROLoopMemRef* IRO_LoopMemRefCurrent;
static IROExpr* RisList;
static BitVector* AllKills;
static Boolean KnownBounds;
static SInt32 Times;
static IROLinear* PredInt;
static IROElmList* FirstAddendLinear;
static IROElmList* LastAddendLinear;
static int NoSubableSubs;
#if VERSION >= VERSION_GC_2_5
// 2.5 (not in Pro7): TreeContains' search state
// TODO: invented names
static Boolean iro_loop_search_found;
static IROLinear* iro_loop_search_target;
#endif

// forward decls
static void MyHandleLoop_Vector(IRONode* fnode);
static void MyHandleLoop_Motion(IRONode* fnode);
static void CheckAllLoopAddresses(IRONode* fnode, BitVector* bv, Boolean* resultFlag);
static void MakeLoopEntry(IROLinear* nd, IROAddrRecord* rec, Boolean mustreach1, Boolean flag2);
static void MoveInvarianceInAddressExpr(void);
static IROLinear* RearrangeInvarianceInAddressExpr(IROLinear* nd, IROList* list);
static UInt32 IsAssignmentReductionCandidate(IROLinear* nd);

void FindMustReach(void) {
    IRONode* fnode;
    IRONode* fnode2;
    IRONode* pred;
    int i;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        fnode->mustreach = FALSE;
        if (Bv_IsBitSet(fnode->index, InLoop)) {
            fnode->mustreach = TRUE;

            for (i = 0; i < LoopNode->numpred; i++) {
                pred = IRO_NodeTable[LoopNode->pred[i]];
                if (Bv_IsBitSet(pred->index, InLoop) && !Bv_IsBitSet(fnode->index, pred->dom)) {
                    fnode->mustreach = FALSE;
                    break;
                }
            }

            for (fnode2 = IRO_FirstNode; fnode2; fnode2 = fnode2->nextnode) {
                if (Bv_IsBitSet(fnode2->index, InLoop)) {
                    for (i = 0; i < fnode2->numsucc; i++) {
                        if (!Bv_IsBitSet(fnode2->succ[i], InLoop) &&
                            !Bv_IsBitSet(fnode->index, fnode2->dom)) {
                            fnode->mustreach = FALSE;
                            break;
                        }
                    }

                    if (!fnode->mustreach) {
                        break;
                    }
                }
            }
        }
    }
}

void FindMustReach1(IRONode* checkfnode) {
    IRONode* fnode;
    IRONode* fnode2;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        if (Bv_IsBitSet(fnode->index, InLoop)) {
            fnode->mustreach1 = TRUE;
            for (fnode2 = IRO_FirstNode; fnode2; fnode2 = fnode2->nextnode) {
                if (Bv_IsBitSet(fnode2->index, InLoop)) {
                    if (Bv_IsBitSet(fnode2->index, InLoop_Tails) &&
                        !Bv_IsBitSet(fnode->index, fnode2->dom)) {
                        fnode->mustreach1 = FALSE;
                    }

                    if (Bv_IsBitSet(fnode2->index, InLoop_Exits) &&
                        fnode2 !=
                        checkfnode &&
                        !Bv_IsBitSet(fnode->index, fnode2->dom)) {
                        fnode->mustreach1 = FALSE;
                    }

                    if (!fnode->mustreach1) {
                        break;
                    }
                }
            }
        }
    }
}

static void IRO_FindLoopTailsAndExits(IRONode* fnode) {
    IRONode* scan;
    IRONode* succ;
    int i;

    for (scan = IRO_FirstNode; scan; scan = scan->nextnode) {
        if (Bv_IsBitSet(scan->index, InLoop)) {
            for (i = 0; i < scan->numsucc; i++) {
                succ = IRO_NodeTable[scan->succ[i]];
                if (succ == fnode) {
                    Bv_SetBit(scan->index, InLoop_Tails);
                    LoopTail = scan;
                    LoopTailNum++;
                }
                if (!Bv_IsBitSet(succ->index, InLoop)) {
                    LoopExitNumber++;
                    LoopExitSuccessor = succ;
                    Bv_SetBit(scan->index, InLoop_Exits);
                }
            }
        }
    }

    IRO_Dump(
        "IRO_FindLoopTailsAndExits:For  header %d, Loop exits and loop tails are  \n", fnode->index
    );
    IRO_DumpBits("Loop Exits: ", InLoop_Exits);
    IRO_DumpBits("Loop Tails: ", InLoop_Tails);
    IRO_Dump("LoopExitNum=%d: \n", LoopExitNumber);
    if (LoopExitSuccessor) {
        IRO_Dump("LoopExitSuccessor node =%d: \n", LoopExitSuccessor->index);
    }
}

static int IsSafeTypcon(IROLinear* nd) {
    Type* srcType;
    Type* destType;
    SInt32 srcSize;
    SInt32 destSize;
    Boolean srcUnsigned;
    Boolean destUnsigned;

    srcType = nd->u.monadic->rtype;
    destType = nd->rtype;
    if (!IS_TYPE_INT(srcType) || !IS_TYPE_INT(destType)) {
        return FALSE;
    }

    srcSize = srcType->size;
    destSize = destType->size;
    srcUnsigned = is_unsigned(srcType);
    destUnsigned = is_unsigned(destType);

    if (srcUnsigned == destUnsigned && destSize >= srcSize) {
        return TRUE;
    }
    if (srcUnsigned == TRUE && destUnsigned == FALSE && destSize > srcSize) {
        return TRUE;
    }
    return FALSE;
}

#if VERSION >= VERSION_GC_2_5
// 2.5 (not in Pro7): an IRO_WalkTree visitor noting whether iro_loop_search_target occurs in the
// tree. TODO: invented name (Wii has it as a static of IroStrengthReduction, unnamed)
static void IRO_ActFindSearchTarget(IROLinear* linear, Boolean isFirst) {
    if (isFirst && linear == iro_loop_search_target) {
        iro_loop_search_found = TRUE;
    }
}

// 2.5 (not in Pro7): whether TARGET occurs in TREE
// TODO: invented name (inlined at both uses: no row)
static Boolean TreeContains(IROLinear* tree, IROLinear* target) {
    iro_loop_search_target = target;
    iro_loop_search_found = FALSE;
    IRO_WalkTree(tree, IRO_ActFindSearchTarget);
    return iro_loop_search_found;
}
#endif

#if VERSION < VERSION_GC_2_5
// 2.0's file has 26 lines fewer above here
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT -26
#endif
static int Reducable(
    IROLinear* nd, IROLinear** resultNd1, IROLinear** resultNd2, VarRecord** resultVar
) {
    IROLinear* indirect;
    IROLinear* left;
    IROLinear* right;
    Boolean leftInvariant;
    Boolean rightInvariant;
    Boolean leftTypcon;
    Boolean rightTypcon;
    Object* obj;
    ENode* enode;
    IROLoopInd* ind;
    CInt64 val64;
    SInt32 val;
    SInt32 div;

    leftInvariant = FALSE;
    rightInvariant = FALSE;
    leftTypcon = FALSE;
    rightTypcon = FALSE;

    if (nd->type ==
        IROLinearOp2Arg &&
        nd->nodetype ==
        EADD &&
        (IS_TYPE_INT(nd->rtype) || IS_TYPE_POINTER_ONLY(nd->rtype))) {
        left = nd->u.diadic.left;
        right = nd->u.diadic.right;
        if (left->type == IROLinearOp1Arg && left->nodetype == ETYPCON) {
            leftTypcon = TRUE;
            leftInvariant = (left->flags & IROLF_LoopInvariant) != 0;
            left = left->u.monadic;
        }
        if (right->type == IROLinearOp1Arg && right->nodetype == ETYPCON) {
            rightTypcon = TRUE;
            rightInvariant = (right->flags & IROLF_LoopInvariant) != 0;
            right = right->u.monadic;
        }

        if (((left->flags & IROLF_LoopInvariant) || leftInvariant) && IRO_IsVariable(right)) {
            if (leftInvariant ||
                ((!(obj = IRO_IsVariable(left)) || !IRO_IsRegable(obj)) && !IRO_IsConstant(left))) {
                if (left->type ==
                    IROLinearOp2Arg &&
                    left->nodetype ==
                    EADD &&
                    (obj = IRO_IsVariable(left->u.diadic.left)) &&
                    IRO_IsRegable(obj) &&
                    IRO_IsConstant(left->u.diadic.right)) {
                    return FALSE;
                }

                if (rightTypcon) {
                    if (!IsSafeTypcon(nd->u.diadic.right)) {
                        return FALSE;
                    }
                    *resultNd2 = nd->u.diadic.right;
                } else {
                    *resultNd2 = right;
                }

                indirect = right;
                *resultNd1 = IRO_NewLinear(IROLinearOperand);

                enode = IRO_NewENode(EINTCONST);
                enode->data.intval = cint64_one;
                enode->rtype = nd->u.diadic.right->rtype;
                (*resultNd1)->rtype = nd->u.diadic.right->rtype;
                (*resultNd1)->u.node = enode;
            } else {
                return FALSE;
            }
        } else if (((left->flags & IROLF_LoopInvariant) || leftInvariant) &&
                   right->type ==
                   IROLinearOp2Arg &&
                   !rightTypcon &&
                   (right->nodetype == EMUL || right->nodetype == ESHL)) {
            if (IRO_IsConstant(right->u.diadic.right)) {
                if (right->nodetype == ESHL) {
                    right->nodetype = EMUL;
                    right->u.diadic.right->u.node->data.intval =
                        CInt64_Shl(cint64_one, right->u.diadic.right->u.node->data.intval);
                }
                if (right->u.diadic.left->type == IROLinearOp1Arg) {
                    if (IRO_IsVariable(right->u.diadic.left)) {
                        *resultNd2 = right->u.diadic.left;
                        indirect = right->u.diadic.left;
                        *resultNd1 = right->u.diadic.right;
                    } else if (right->u.diadic.left->nodetype ==
                               ETYPCON &&
                               IRO_IsVariable(right->u.diadic.left->u.monadic)) {
                        if (!IsSafeTypcon(right->u.diadic.left)) {
                            return FALSE;
                        }
                        *resultNd2 = right->u.diadic.left;
                        indirect = right->u.diadic.left->u.monadic;
                        *resultNd1 = right->u.diadic.right;
                    } else {
                        return FALSE;
                    }
                } else {
                    return FALSE;
                }
            } else {
                return FALSE;
            }
        } else if (((right->flags & IROLF_LoopInvariant) || rightInvariant) &&
                   left->type ==
                   IROLinearOp2Arg &&
                   !leftTypcon &&
                   (left->nodetype == EMUL || left->nodetype == ESHL)) {
            if (IRO_IsConstant(left->u.diadic.right)) {
                if (left->nodetype == ESHL) {
                    left->nodetype = EMUL;
                    left->u.diadic.right->u.node->data.intval =
                        CInt64_Shl(cint64_one, left->u.diadic.right->u.node->data.intval);
                }
                if (left->u.diadic.left->type == IROLinearOp1Arg) {
                    if (IRO_IsVariable(left->u.diadic.left)) {
                        *resultNd2 = left->u.diadic.left;
                        indirect = left->u.diadic.left;
                        *resultNd1 = left->u.diadic.right;
                    } else if (left->u.diadic.left->nodetype ==
                               ETYPCON &&
                               IRO_IsVariable(left->u.diadic.left->u.monadic)) {
                        if (!IsSafeTypcon(left->u.diadic.left)) {
                            return FALSE;
                        }
                        *resultNd2 = left->u.diadic.left;
                        indirect = left->u.diadic.left->u.monadic;
                        *resultNd1 = left->u.diadic.right;
                    } else {
                        return FALSE;
                    }
                } else {
                    return FALSE;
                }
            } else {
                return FALSE;
            }
        } else if (((right->flags & IROLF_LoopInvariant) || rightInvariant) &&
                   IRO_IsVariable(left)) {
            if (rightInvariant ||
                ((!(obj = IRO_IsVariable(right)) || !IRO_IsRegable(obj)) &&
                 !IRO_IsConstant(right))) {
                if (right->type ==
                    IROLinearOp2Arg &&
                    right->nodetype ==
                    EADD &&
                    (obj = IRO_IsVariable(right->u.diadic.left)) &&
                    IRO_IsRegable(obj) &&
                    IRO_IsConstant(right->u.diadic.right)) {
                    return FALSE;
                }

                if (leftTypcon) {
                    if (!IsSafeTypcon(nd->u.diadic.left)) {
                        return FALSE;
                    }
                    *resultNd2 = nd->u.diadic.left;
                } else {
                    *resultNd2 = left;
                }

                indirect = left;
                *resultNd1 = IRO_NewLinear(IROLinearOperand);

                enode = IRO_NewENode(EINTCONST);
                enode->data.intval = cint64_one;
                enode->rtype = nd->u.diadic.left->rtype;
                (*resultNd1)->rtype = nd->u.diadic.left->rtype;
                (*resultNd1)->u.node = enode;
            } else {
                return FALSE;
            }
        } else {
            return FALSE;
        }
    } else if (nd->type ==
               IROLinearOp2Arg &&
               (nd->nodetype == EMUL || nd->nodetype == ESHL) &&
               nd->rtype->size <=
               4 &&
               (IS_TYPE_INT(nd->rtype) || IS_TYPE_POINTER_ONLY(nd->rtype))) {
        left = nd->u.diadic.left;
        right = nd->u.diadic.right;

        if (IRO_IsConstant(nd->u.diadic.right) && IRO_IsVariable(left)) {
            *resultNd2 = left;
            indirect = left;
            *resultNd1 = right;
        } else if (IRO_IsConstant(nd->u.diadic.right) &&
                   left->type ==
                   IROLinearOp1Arg &&
                   left->nodetype ==
                   ETYPCON &&
                   IRO_IsVariable(left->u.monadic)) {
            if (!IsSafeTypcon(left)) {
                return FALSE;
            }
            *resultNd2 = left;
            indirect = left->u.monadic;
            *resultNd1 = right;
        } else {
            if (nd->type == IROLinearOp2Arg && nd->nodetype == ESHL) {
                return FALSE;
            }

            if (nd->u.diadic.right->flags & IROLF_LoopInvariant) {
                if (IRO_IsVariable(left)) {
                    *resultNd2 = left;
                    indirect = left;
                    *resultNd1 = right;
                } else if (left->type ==
                           IROLinearOp1Arg &&
                           left->nodetype ==
                           ETYPCON &&
                           IRO_IsVariable(left->u.monadic)) {
                    if (!IsSafeTypcon(left)) {
                        return FALSE;
                    }
                    *resultNd2 = left;
                    indirect = left->u.monadic;
                    *resultNd1 = right;
                } else {
                    return FALSE;
                }
            } else if (nd->u.diadic.left->flags & IROLF_LoopInvariant) {
                if (IRO_IsVariable(right)) {
                    *resultNd2 = right;
                    indirect = right;
                    *resultNd1 = left;
                } else if (right->type ==
                           IROLinearOp1Arg &&
                           right->nodetype ==
                           ETYPCON &&
                           IRO_IsVariable(right->u.monadic) &&
                           nd->type ==
                           IROLinearOp2Arg &&
                           nd->nodetype == EMUL) {
                    if (!IsSafeTypcon(right)) {
                        return FALSE;
                    }
                    *resultNd2 = right;
                    indirect = right->u.monadic;
                    *resultNd1 = left;
                } else {
                    return FALSE;
                }
            } else {
                return FALSE;
            }
        }
    } else if (nd->type ==
               IROLinearOp2Arg &&
               (nd->nodetype == EDIV || nd->nodetype == ESHR) &&
               nd->rtype->size <=
               4 &&
               IS_TYPE_INT(nd->rtype)) {
        if (IRO_IsVariable(nd->u.diadic.left) && IRO_IsConstant(nd->u.diadic.right)) {
            val64 = nd->u.diadic.right->u.node->data.intval;
            if (nd->type == IROLinearOp2Arg && nd->nodetype == ESHR) {
                if (CInt64_GetULong(&val64) > 32 || val64.hi) {
                    return FALSE;
                }
                val64 = CInt64_Shl(cint64_one, val64);
            }
            *resultNd2 = nd->u.diadic.left;
            indirect = nd->u.diadic.left;
        } else {
            return FALSE;
        }
    } else {
        return FALSE;
    }

    // TODO: fakematch. 2.6 loads the constant's high word into a register before testing it; the
    // natural `...intval.hi` (and `!= 0`, casts, Pro7's CTool_EndianReadWord32, a copy of the ENode
    // in `enode`) compiles to a memory compare. The dead store to `div` stands in.
    if (nd->type ==
        IROLinearOp2Arg &&
        nd->nodetype ==
        ESHL &&
        (!IRO_IsConstant(*resultNd1) ||
         (SInt32)CInt64_GetULong(&(*resultNd1)->u.node->data.intval) <
         0 ||
         (SInt32)CInt64_GetULong(&(*resultNd1)->u.node->data.intval) >
         32 ||
         (div = (*resultNd1)->u.node->data.intval.hi))) {
        return FALSE;
    }

    CError_ASSERT(834, indirect->u.monadic->u.node != NULL);
    *resultVar = IRO_FindVar(indirect->u.monadic->u.node->data.objref, FALSE, TRUE);
    if (!*resultVar || (*resultVar)->inductionState != 2) {
        return FALSE;
    }

    if (copts.ANSIstrict || copts.strengthreductionstrict) {
        Type* type = (*resultVar)->object->type;
        if (is_unsigned(type) && type->size < stunsignedlong.size) {
            return FALSE;
        }
    }

#if VERSION < VERSION_GC_2_5
    if (nd->type == IROLinearOp2Arg && (nd->nodetype == ESHR || nd->nodetype == EDIV)) {
        ind = FirstInd;
        while (ind && ind->var != *resultVar) {
            ind = ind->next;
        }

#if VERSION < VERSION_GC_2_5
// 2.0's file has 22 lines fewer above here
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT -22
#endif
        CError_ASSERT(873, ind != NULL);

        if (ind->addNode == NULL) {
            SInt32 addConst = ind->addConst;
            if (addConst < (val = CInt64_GetULong(&val64))) {
                return FALSE;
            }
            if ((div = addConst / val) <= 0) {
                return FALSE;
            }

            *resultNd1 = IRO_NewLinear(IROLinearOperand);
            enode = IRO_NewENode(EINTCONST);
            CInt64_SetULong(&enode->data.intval, div);
            enode->rtype = nd->u.diadic.left->rtype;
            (*resultNd1)->rtype = nd->u.diadic.left->rtype;
            (*resultNd1)->u.node = enode;
        } else {
            return FALSE;
        }
    }
#else
    // 2.5 looks the induction record up for every candidate (Pro7: only for ESHR and EDIV), for
    // the check at the end
    ind = FirstInd;
    while (ind && ind->var != *resultVar) {
        ind = ind->next;
    }

    CError_ASSERT(873, ind != NULL);

    if (nd->type == IROLinearOp2Arg && (nd->nodetype == ESHR || nd->nodetype == EDIV)) {
        if (ind->addNode == NULL) {
            SInt32 addConst = ind->addConst;
            if (addConst < (val = CInt64_GetULong(&val64))) {
                return FALSE;
            }
            if ((div = addConst / val) <= 0) {
                return FALSE;
            }
            // 2.5: the step must also be a multiple of the divisor
            if (addConst % val) {
                return FALSE;
            }

            *resultNd1 = IRO_NewLinear(IROLinearOperand);
            enode = IRO_NewENode(EINTCONST);
            CInt64_SetULong(&enode->data.intval, div);
            enode->rtype = nd->u.diadic.left->rtype;
            (*resultNd1)->rtype = nd->u.diadic.left->rtype;
            (*resultNd1)->u.node = enode;
        } else {
            return FALSE;
        }
    }

    // 2.5: an add that the induction variable's update uses as a value is not reduced
    if (nd->type ==
        IROLinearOp2Arg &&
        nd->nodetype ==
        EADD &&
        (IS_TYPE_INT(nd->rtype) || IS_TYPE_POINTER_ONLY(nd->rtype))) {
        if (TreeContains(ind->nd, nd) && (ind->nd->flags & IROLF_Reffed)) {
            return FALSE;
        }
    }
#endif

    return TRUE;
}

static void IRO_RemoveExpr_Action(IROLinear* linear, Boolean isFirst) {
    if (isFirst && linear->expr) {
        IRO_RemoveExpr(linear->expr);
    }
}

static void IRO_ActUnmarkRISCandidate(IROLinear* linear, Boolean isFirst) {
    if (isFirst && linear->expr) {
        linear->flags &= ~IROLF_Ris;
    }
}

static IROExpr* CreateRIS(
    IROExpr* expr, IROLinear* nd1, IROLinear* nd2, IROLoopInd* induction, int unk
) {
    Object* tempObj;
    IROLinear* thirdnode;
    Object* tempObj2;
    Boolean flag23;
    IROLinear* firstnode;
    IROLinear* secondnode;
    IROLinear* fifthnode;
    IROLinear* fourthnode;
    Type* type;
    IROLinear* tmp;
    ENode* enode;
    IROList list1;
    IROList list2;

    flag23 = FALSE;
    type = expr->linear->rtype;
    tempObj = create_temp_object(type);
    IRO_FindVar(tempObj, TRUE, TRUE);
    IRO_InitList(&list1);

    if (IS_LINEAR_DIADIC(expr->linear, EADD)) {
        firstnode = IRO_DuplicateExpr(expr->linear, &list1);
    } else if (IS_LINEAR_DIADIC_2(expr->linear, EDIV, ESHR)) {
        firstnode = IRO_DuplicateExpr(expr->linear, &list1);
    } else {
        firstnode = IRO_NewLinear(IROLinearOp2Arg);
        firstnode->index = ++IRO_NumLinear;
        firstnode->rtype = type;
        firstnode->nodetype = EMUL;
        firstnode->u.diadic.left = IRO_DuplicateExpr(nd1, &list1);
        if (unk) {
            firstnode->u.diadic.right = IRO_DuplicateExpr(nd1, &list1);
        } else {
            firstnode->u.diadic.right = IRO_DuplicateExpr(nd2, &list1);
        }
        IRO_AddToList(firstnode, &list1);
    }

    secondnode = IRO_NewLinear(IROLinearOp2Arg);
    secondnode->index = ++IRO_NumLinear;
    secondnode->rtype = type;
    secondnode->nodetype = EASS;
    secondnode->u.diadic.left = IRO_TempReference(tempObj, &list1);
    secondnode->u.diadic.left->flags |= IROLF_Ind | IROLF_Assigned;
    secondnode->u.diadic.left->u.monadic->flags |= IROLF_Ind | IROLF_Assigned;
    secondnode->u.diadic.right = firstnode;
    IRO_AddToList(secondnode, &list1);
    IRO_Paste(list1.head, list1.tail, PredInt);
    if (!IS_LINEAR_DIADIC_2(expr->linear, EDIV, ESHR) &&
        induction->addConst !=
        1 &&
        (induction->addNode || !IRO_IsConstant(nd2))) {
        flag23 = TRUE;
        IRO_InitList(&list1);
        thirdnode = IRO_NewLinear(IROLinearOp2Arg);
        thirdnode->index = ++IRO_NumLinear;
        thirdnode->rtype = nd2->rtype;
        thirdnode->nodetype = EMUL;

        if (!induction->addNode) {
            thirdnode->u.diadic.left = IRO_DuplicateExpr(nd2, &list1);
            thirdnode->u.diadic.right = IRO_NewLinear(IROLinearOperand);
            thirdnode->u.diadic.right->index = ++IRO_NumLinear;
            enode = IRO_NewENode(EINTCONST);
            enode->rtype = nd2->rtype;
            thirdnode->u.diadic.right->rtype = nd2->rtype;
            CInt64_SetLong(&enode->data.intval, induction->addConst);
            thirdnode->u.diadic.right->u.node = enode;
            IRO_AddToList(thirdnode->u.diadic.right, &list1);
        } else {
            thirdnode->u.diadic.left = IRO_DuplicateExpr(nd2, &list1);
            thirdnode->u.diadic.right = IRO_DuplicateExpr(induction->addNode, &list1);
            if (nd2->rtype != induction->addNode->rtype) {
                tmp = IRO_NewLinear(IROLinearOp1Arg);
                tmp->nodetype = ETYPCON;
                tmp->index = ++IRO_NumLinear;
                tmp->rtype = nd2->rtype;
                tmp->u.monadic = thirdnode->u.diadic.right;
                IRO_AddToList(tmp, &list1);
                thirdnode->u.diadic.right = tmp;
            }
        }
        IRO_AddToList(thirdnode, &list1);

        tempObj2 = create_temp_object(nd2->rtype);

        fourthnode = IRO_NewLinear(IROLinearOp2Arg);
        fourthnode->index = ++IRO_NumLinear;
        fourthnode->rtype = nd2->rtype;
        fourthnode->nodetype = EASS;
        fourthnode->u.diadic.left = IRO_TempReference(tempObj2, &list1);
        fourthnode->u.diadic.left->flags |= IROLF_Ind | IROLF_Assigned;
        fourthnode->u.diadic.left->u.monadic->flags |= IROLF_Ind | IROLF_Assigned;
        fourthnode->u.diadic.right = thirdnode;
        IRO_AddToList(fourthnode, &list1);
        IRO_Paste(list1.head, list1.tail, PredInt);
    }

    IRO_InitList(&list2);
    fifthnode = IRO_NewLinear(IROLinearOp2Arg);
    fifthnode->index = ++IRO_NumLinear;
    fifthnode->rtype = type;
    if (induction->nd->type == IROLinearOp2Arg) {
        if (induction->nd->nodetype ==
            EASS &&
            IS_LINEAR_DIADIC(induction->nd->u.diadic.right, EADD)) {
            fifthnode->nodetype = EADDASS;
        } else if (induction->nd->nodetype ==
                   EASS &&
                   IS_LINEAR_DIADIC(induction->nd->u.diadic.right, ESUB)) {
            fifthnode->nodetype = ESUBASS;
        } else {
            fifthnode->nodetype = induction->nd->nodetype;
        }
    } else {
        if (induction->nd->nodetype == EPREINC || induction->nd->nodetype == EPOSTINC) {
            fifthnode->nodetype = EADDASS;
        } else {
            fifthnode->nodetype = ESUBASS;
        }
    }

    fifthnode->u.diadic.left = IRO_TempReference(tempObj, &list2);
    fifthnode->u.diadic.left->flags |= IROLF_Ind | IROLF_Used | IROLF_Assigned;
    fifthnode->u.diadic.left->u.monadic->flags |= IROLF_Ind | IROLF_Used | IROLF_Assigned;
    if (!unk) {
        if (!flag23) {
            if (induction->addConst == 1 || IS_LINEAR_DIADIC_2(expr->linear, EDIV, ESHR)) {
                fifthnode->u.diadic.right = IRO_DuplicateExpr(nd2, &list2);
            } else {
                fifthnode->u.diadic.right = IRO_NewLinear(IROLinearOperand);
                fifthnode->u.diadic.right->index = ++IRO_NumLinear;
                enode = IRO_NewENode(EINTCONST);
                enode->rtype = nd2->rtype;
                fifthnode->u.diadic.right->rtype = nd2->rtype;
                CInt64_SetLong(
                    &enode->data.intval,
                    induction->addConst * CInt64_GetULong(&nd2->u.node->data.intval)
                );
                fifthnode->u.diadic.right->u.node = enode;
                IRO_AddToList(fifthnode->u.diadic.right, &list2);
            }
        } else {
            fifthnode->u.diadic.right = IRO_TempReference(tempObj2, &list2);
            fifthnode->u.diadic.right->flags |= IROLF_Used;
        }
    }

    fifthnode->index = ++IRO_NumLinear;
    IRO_AddToList(fifthnode, &list2);
#if VERSION < VERSION_GC_2_5
    IRO_Paste(list2.head, list2.tail, IRO_FindStart(induction->nd));
#else
    if (IS_LINEAR_DIADIC(expr->linear, EADD) &&
        (IS_TYPE_INT(expr->linear->rtype) || IS_TYPE_POINTER_ONLY(expr->linear->rtype)) &&
        TreeContains(induction->nd, expr->linear) &&
        !(induction->nd->flags & IROLF_Reffed)) {
        IRO_PasteAfter(list2.head, list2.tail, induction->nd);
    } else {
        IRO_Paste(list2.head, list2.tail, IRO_FindStart(induction->nd));
    }
#endif
    IRO_WalkTree(expr->linear, IRO_RemoveExpr_Action);
    expr->temp = tempObj;
    expr->next = RisList;
    RisList = expr;
    return expr;
}

#if VERSION < VERSION_GC_2_5
// 2.0's file has 53 lines fewer above here
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT -53
#endif
static IRONode* CreatePreHeader(IRONode* fnode1, IRONode* fnode2) {
    IROLinear* labelnode;
    IRONode* newfnode;
    IRONode* iter;
    CLabel* oldlabel;
    CLabel* newlabel;
    SwitchInfo* swinfo;
    SwitchCase* swcase;

    newfnode = oalloc(sizeof(IRONode));
    memset(newfnode, 0, sizeof(IRONode));
    newfnode->index = IRO_NumNodes;
    IRO_NumNodes++;

    labelnode = IRO_NewLinear(IROLinearLabel);
    labelnode->index = IRO_NumLinear++;
    labelnode->next = NULL;
    labelnode->u.label.label = IRO_NewLabel();
    labelnode->flags |= IROLF_1;
    labelnode->u.label.label->stmt = (Statement*)newfnode;
    newfnode->first = labelnode;
    newfnode->last = labelnode;

    if (fnode2) {
        fnode2->last->next = labelnode;
        labelnode->next = IRO_NewLinear(IROLinearNop);
        labelnode->next->next = fnode1->first;
        fnode2->nextnode = newfnode;
        newfnode->nextnode = fnode1;
    } else {
        CError_ASSERT(1313, fnode1->first->type == IROLinearLabel);
        labelnode->next = IRO_NewLinear(IROLinearGoto);
        labelnode->next->u.label.label = fnode1->first->u.label.label;
        IRO_LastNode->last->next = labelnode;
        IRO_LastNode->nextnode = newfnode;
        IRO_LastNode = newfnode;
        IRO_LastLinear = labelnode->next;
    }

    newfnode->last = labelnode->next;

    IRO_NodeTable = oalloc(sizeof(IRONode*) * IRO_NumNodes);
    memset(IRO_NodeTable, 0, sizeof(IRONode*) * IRO_NumNodes);
    for (iter = IRO_FirstNode; iter; iter = iter->nextnode) {
        IRO_NodeTable[iter->index] = iter;
    }

    if (fnode1->first->type == IROLinearLabel) {
        oldlabel = fnode1->first->u.label.label;
        newlabel = newfnode->first->u.label.label;
        for (iter = IRO_FirstNode; iter; iter = iter->nextnode) {
            if (!Bv_IsBitSet(iter->index, InLoop) && iter != newfnode) {
                switch (iter->last->type) {
                    case IROLinearGoto:
                        if (iter->last->u.label.label == oldlabel) {
                            iter->last->u.label.label = newlabel;
                        }
                        break;
                    case IROLinearIf:
                    case IROLinearIfNot:
                        if (iter->last->u.label.label == oldlabel) {
                            iter->last->u.label.label = newlabel;
                        }
                        break;
                    case IROLinearSwitch:
                        swinfo = iter->last->u.swtch.info;
                        for (swcase = swinfo->cases; swcase; swcase = swcase->next) {
                            if (swcase->label == oldlabel) {
                                swcase->label = newlabel;
                            }
                        }
                        if (swinfo->defaultlabel == oldlabel) {
                            swinfo->defaultlabel = newlabel;
                        }
                        break;
                }
            }
        }
    }

    IRO_ComputeSuccPred();
    IRO_ComputeDom();
    return newfnode;
}

static IRONode* CreateNewLoopExitSuccessor(IRONode* fnode1) {
    IROLinear* labelnode;
    IRONode* fnode2;
    CLabel* oldlabel;
    SwitchInfo* swinfo;
    IRONode* iter;
    UInt16 i;
    IRONode* newfnode;
    Boolean flag;
    CLabel* newlabel;
    SwitchCase* swcase;

    CError_ASSERT(1414, fnode1 != NULL && LoopExitNumber == 1);

    fnode2 = NULL;
    flag = FALSE;

    for (i = 0; i < fnode1->numpred; i++) {
        iter = IRO_NodeTable[fnode1->pred[i]];
        if (Bv_IsBitSet(iter->index, InLoop_Exits)) {
            CError_ASSERT(1425, fnode2 == NULL);
            fnode2 = iter;
            if (!flag) {
                if (!iter->last ||
                    !fnode1->first ||
                    fnode1->first->type !=
                    IROLinearLabel ||
                    ((iter->last->type == IROLinearIf || iter->last->type == IROLinearIfNot) &&
                     iter->last->u.label.label != fnode1->first->u.label.label)) {
                    flag = TRUE;
                }
            }
        }
    }

    CError_ASSERT(1441, fnode2 != NULL);

    newfnode = oalloc(sizeof(IRONode));
    memset(newfnode, 0, sizeof(IRONode));
    newfnode->index = IRO_NumNodes;
    IRO_NumNodes++;

    labelnode = IRO_NewLinear(IROLinearLabel);
    labelnode->index = IRO_NumLinear++;
    labelnode->next = NULL;
    labelnode->u.label.label = IRO_NewLabel();
    labelnode->flags |= IROLF_1;
    labelnode->u.label.label->stmt = (Statement*)newfnode;
    newfnode->first = labelnode;
    newfnode->last = labelnode;

    if (flag) {
        fnode2->last->next = labelnode;
        labelnode->next = IRO_NewLinear(IROLinearNop);
        labelnode->next->next = fnode1->first;
        fnode2->nextnode = newfnode;
        newfnode->nextnode = fnode1;
    } else {
        CError_ASSERT(1481, fnode1->first->type == IROLinearLabel);
        labelnode->next = IRO_NewLinear(IROLinearGoto);
        labelnode->next->u.label.label = fnode1->first->u.label.label;
        IRO_LastNode->last->next = labelnode;
        IRO_LastNode->nextnode = newfnode;
        IRO_LastNode = newfnode;
        IRO_LastLinear = labelnode->next;
    }

    newfnode->last = labelnode->next;

    IRO_NodeTable = oalloc(sizeof(IRONode*) * IRO_NumNodes);
    memset(IRO_NodeTable, 0, sizeof(IRONode*) * IRO_NumNodes);
    for (iter = IRO_FirstNode; iter; iter = iter->nextnode) {
        IRO_NodeTable[iter->index] = iter;
    }

    if (fnode1->first->type == IROLinearLabel) {
        oldlabel = fnode1->first->u.label.label;
        newlabel = newfnode->first->u.label.label;
        switch (fnode2->last->type) {
            case IROLinearGoto:
                if (fnode2->last->u.label.label == oldlabel) {
                    fnode2->last->u.label.label = newlabel;
                }
                break;
            case IROLinearIf:
            case IROLinearIfNot:
                if (fnode2->last->u.label.label == oldlabel) {
                    fnode2->last->u.label.label = newlabel;
                }
                break;
            case IROLinearSwitch:
                swinfo = fnode2->last->u.swtch.info;
                for (swcase = swinfo->cases; swcase; swcase = swcase->next) {
                    if (swcase->label == oldlabel) {
                        swcase->label = newlabel;
                    }
                }
                if (swinfo->defaultlabel == oldlabel) {
                    swinfo->defaultlabel = newlabel;
                }
                break;
        }
    }

    IRO_ComputeSuccPred();
    IRO_ComputeDom();
    return newfnode;
}

void AddPreds(IRONode* fnode) {
    IRONode* pred;
    int i;

    for (i = 0; i < fnode->numpred; i++) {
        pred = IRO_NodeTable[fnode->pred[i]];
        if (!Bv_IsBitSet(pred->index, InLoop)) {
            Bv_SetBit(pred->index, InLoop);
            AddPreds(pred);
        }
    }
}

static void RemoveNoopsFromExprList(void) {
    IROExpr* expr;
    IROExpr* prev;

    expr = IRO_FirstExpr;
    prev = NULL;
    while (expr) {
        if (expr->linear->type == IROLinearNop) {
            if (prev) {
                prev->next = expr->next;
            } else {
                IRO_FirstExpr = expr->next;
            }
            expr = expr->next;
        } else {
            prev = expr;
            expr = expr->next;
        }
    }
}

void IncLoopDepth(void) {
    IRONode* fnode;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        if (Bv_IsBitSet(fnode->index, InLoop)) {
            fnode->loopdepth++;
        }
    }
}

void IRO_SetLoopDepth(void) {
    IRONode* fnode;
    IRONode* pred;
    UInt16 i;
    UInt16 flag;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        fnode->loopdepth = 0;
    }

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        flag = FALSE;

        for (i = 0; i < fnode->numpred; i++) {
            pred = IRO_NodeTable[fnode->pred[i]];
            if (Bv_IsBitSet(fnode->index, pred->dom)) {
                if (!flag) {
                    Bv_AllocVector(&InLoop, IRO_NumNodes + 1);
                    Bv_Clear(InLoop);
                    Bv_SetBit(fnode->index, InLoop);
                }
                flag = TRUE;
                Bv_SetBit(pred->index, InLoop);
                if (pred != fnode) {
                    AddPreds(pred);
                }
            }
        }

        if (flag) {
            IncLoopDepth();
        }
    }

    IRO_CheckForUserBreak();
}

static void InsertBranchAroundLoopPreheaders(void) {
    IRONode* fnode;
    IROLinear* labelnode;
    IRONode* iter;
    IROLinear* endnode;
    CLabel* label;

    if (IRO_EndNode && IRO_EndNode->last && IRO_EndNode->last->type == IROLinearEnd) {
        label = IRO_NewLabel();
        fnode = IRO_NewFlowGraphNode();
        IRO_LastNode->nextnode = fnode;
        label->stmt = (Statement*)fnode;

        IRO_NodeTable = oalloc(sizeof(IRONode*) * IRO_NumNodes);
        memset(IRO_NodeTable, 0, sizeof(IRONode*) * IRO_NumNodes);
        for (iter = IRO_FirstNode; iter; iter = iter->nextnode) {
            IRO_NodeTable[iter->index] = iter;
        }

        labelnode = IRO_NewLinear(IROLinearLabel);
        labelnode->index = ++IRO_NumLinear;
        labelnode->u.label.label = label;
        labelnode->flags |= IROLF_1;
        fnode->first = labelnode;
        IRO_LastLinear->next = labelnode;

        endnode = IRO_NewLinear(IROLinearEnd);
        memcpy(endnode, IRO_EndNode->last, sizeof(IROLinear));
        endnode->index = ++IRO_NumLinear;
        endnode->next = NULL;
        fnode->last = endnode;
        labelnode->next = endnode;
        IRO_LastLinear = endnode;

        IRO_EndNode->last->type = IROLinearGoto;
        IRO_EndNode->last->u.label.label = label;
        IRO_EndNode = IRO_LastNode = fnode;

        IRO_ComputeSuccPred();
        IRO_ComputeDom();
    }
}

void IRO_FindLoops(void) {
    IRONode* fnode;
    IRONode* pred;
    UInt16 i;
    UInt16 flag;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        flag = FALSE;

        for (i = 0; i < fnode->numpred; i++) {
            pred = IRO_NodeTable[fnode->pred[i]];
            if (Bv_IsBitSet(fnode->index, pred->dom)) {
                if (!flag) {
                    Bv_AllocVector(&InLoop, IRO_NumNodes + 1);
                    Bv_Clear(InLoop);
                    Bv_SetBit(fnode->index, InLoop);
                }
                flag = TRUE;
                Bv_SetBit(pred->index, InLoop);
                if (pred != fnode) {
                    AddPreds(pred);
                }
            }
        }

        if (flag) {
            IncLoopDepth();
            IRO_Dump("IRO_FindLoops:Found loop with header %d\n", fnode->index);
            IRO_DumpBits("Loop includes: ", InLoop);
            MyHandleLoop_Motion(fnode);
            IRO_UpdateFlagsOnInts();
            MyHandleLoop_Vector(fnode);
            RemoveNoopsFromExprList();
            IRO_UpdateFlagsOnInts();
            IRO_UpdateVars();
        }
    }

    if (!IRO_FunctionHasReturn && IRO_EndNode != IRO_LastNode) {
        InsertBranchAroundLoopPreheaders();
    }
}

static void CheckSubableSub(IROLinear* linear, Boolean isFirst) {
    if (isFirst && IRO_IsSubableExpression(linear)) {
        IRO_Dump("Subable Expression is %d\n", linear->index);
        NoSubableSubs = FALSE;
    }
}

static int NoSubableSubExprs(IROLinear* nd) {
    NoSubableSubs = TRUE;
    IRO_WalkTree(nd, CheckSubableSub);
    return NoSubableSubs;
}

void ComputeLoopKills(void) {
    IRONode* fnode;
    IROLinear* nd;

    Bv_AllocVector(&AllKills, IRO_NumVars + 1);
    Bv_AllocVector(&IRO_VarKills, IRO_NumVars + 1);

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        if (Bv_IsBitSet(fnode->index, InLoop) && (nd = fnode->first)) {
            Bv_AllocVector(&fnode->in, IRO_NumVars + 1);
            Bv_AllocVector(&fnode->gen, IRO_NumVars + 1);
            while (1) {
                Bv_Clear(IRO_VarKills);
                IRO_GetKills(nd);
                Bv_Or(IRO_VarKills, AllKills);
                if (nd == fnode->last) {
                    break;
                }
                nd = nd->next;
            }
        }
    }
}

void ComputeLoopInvariance(void) {
    IRONode* fnode;
    IROLinear* nd;

    IRO_Depends = IRO_VarKills;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        if (Bv_IsBitSet(fnode->index, InLoop) && (nd = fnode->first)) {
            nd->flags &= ~IROLF_LoopInvariant;
            while (1) {
                if ((nd->flags & IROLF_Reffed) && nd->type != IROLinearNop) {
                    IRO_FindDepends_NoAlloc(nd);
                    if (!IRO_IsVolatile && !Bv_BitsInCommon(IRO_Depends, AllKills)) {
                        nd->flags |= IROLF_LoopInvariant;
                    }

                    if (IRO_CouldError) {
                        nd->flags |= IROLF_CouldError;
                    } else {
                        nd->flags &= ~IROLF_CouldError;
                    }
                }

                if (nd == fnode->last) {
                    break;
                }
                nd = nd->next;
            }
        }
    }
}

void ComputeLoopInduction(void) {
    IRONode* fnode;
    IROLinear* nd;
    Boolean flag;
    Object* obj;
    Object* obj2;
    Object* obj3;
    VarRecord* var;
    IROLinear* tmpnd;
    IROLoopInd* ind;
    Boolean isUnsigned;

    Bv_AllocVector(&AllKills, IRO_NumVars + 1);
    Bv_AllocVector(&IRO_VarKills, IRO_NumVars + 1);

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        if (Bv_IsBitSet(fnode->index, InLoop) && (nd = fnode->first)) {
            while (1) {
                Bv_Clear(IRO_VarKills);
                IRO_GetKills(nd);
                Bv_Or(IRO_VarKills, AllKills);
                flag = FALSE;
                if ((nd->type ==
                     IROLinearOp2Arg &&
                     nd->rtype->size <=
                     4 &&
                     (nd->nodetype == EADDASS || nd->nodetype == ESUBASS) &&
                     (obj = IRO_IsVariable(nd->u.diadic.left)) &&
                     obj->type->size ==
                     nd->rtype->size &&
                     IS_TYPE_INT(obj->type) &&
                     ((IRO_IsIntConstant(nd->u.diadic.right) &&
                       CInt64_GetULong(&nd->u.diadic.right->u.node->data.intval)) ||
                      (nd->u.diadic.right->flags & IROLF_LoopInvariant))) ||
                    (nd->type ==
                     IROLinearOp1Arg &&
                     nd->rtype->size <=
                     4 &&
                     (nd->nodetype ==
                      EPOSTINC ||
                      nd->nodetype ==
                      EPOSTDEC ||
                      nd->nodetype ==
                      EPREINC ||
                      nd->nodetype == EPREDEC) &&
                     (obj = IRO_IsVariable(nd->u.monadic)) &&
                     obj->type->size ==
                     nd->rtype->size &&
                     IS_TYPE_INT(obj->type))) {
                    flag = TRUE;
                } else if (nd->type ==
                           IROLinearOp2Arg &&
                           nd->rtype->size <=
                           4 &&
                           nd->nodetype ==
                           EASS &&
                           (obj = IRO_IsVariable(nd->u.diadic.left)) &&
                           obj->type->size ==
                           nd->rtype->size &&
                           IS_TYPE_INT(obj->type) &&
                           nd->u.diadic.right->type ==
                           IROLinearOp2Arg &&
                           (nd->u.diadic.right->nodetype ==
                            EADD ||
                            nd->u.diadic.right->nodetype == ESUB)) {
                    if (nd->u.diadic.right->nodetype == EADD) {
                        obj2 = IRO_IsVariable(nd->u.diadic.right->u.diadic.left);
                        obj3 = IRO_IsVariable(nd->u.diadic.right->u.diadic.right);
                        if (obj2 ==
                            obj &&
                            obj3 !=
                            obj &&
                            (nd->u.diadic.right->u.diadic.right->flags & IROLF_LoopInvariant)) {
                            flag = TRUE;
                        }

                        if (obj3 ==
                            obj &&
                            obj2 !=
                            obj &&
                            (nd->u.diadic.right->u.diadic.left->flags & IROLF_LoopInvariant)) {
                            flag = TRUE;
                            tmpnd = nd->u.diadic.right->u.diadic.left;
                            nd->u.diadic.right->u.diadic.left = nd->u.diadic.right->u.diadic.right;
                            nd->u.diadic.right->u.diadic.right = tmpnd;
                        }
                    } else {
                        obj2 = IRO_IsVariable(nd->u.diadic.right->u.diadic.left);
                        obj3 = IRO_IsVariable(nd->u.diadic.right->u.diadic.right);
                        if (obj2 ==
                            obj &&
                            obj3 !=
                            obj &&
                            (nd->u.diadic.right->u.diadic.right->flags & IROLF_LoopInvariant)) {
                            flag = TRUE;
                        }
                    }
                }

                if (flag) {
                    if ((var = IRO_FindAssigned(nd))) {
                        if (var->inductionState == 2) {
                            var->inductionState = 0;
                        } else if (var->inductionState == 1) {
                            var->inductionState = 2;
                        }
                    }
                } else {
                    for (var = IRO_FirstVar; var; var = var->next) {
                        if (Bv_IsBitSet(var->index, IRO_VarKills)) {
                            var->inductionState = 0;
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

    IRO_DumpBits("Killed in loop: ", AllKills);

    for (fnode = IRO_FirstNode, FirstInd = NULL; fnode; fnode = fnode->nextnode) {
        if (Bv_IsBitSet(fnode->index, InLoop) && (nd = fnode->first)) {
            while (1) {
                if ((
                        nd->type ==
                        IROLinearOp2Arg &&
                        (nd->nodetype == EADDASS || nd->nodetype == ESUBASS || nd->nodetype == EASS)
                    ) ||
                    (nd->type ==
                     IROLinearOp1Arg &&
                     (nd->nodetype ==
                      EPOSTINC ||
                      nd->nodetype ==
                      EPOSTDEC ||
                      nd->nodetype ==
                      EPREINC ||
                      nd->nodetype == EPREDEC))) {
                    if ((var = IRO_FindAssigned(nd)) &&
                        var->inductionState ==
                        2 &&
                        var->object->type->size <= 4) {
                        ind = oalloc(sizeof(IROLoopInd));
                        ind->fnode = fnode;
                        ind->var = var;
                        ind->nd = nd;
                        ind->next = FirstInd;
                        ind->addNode = NULL;
                        ind->addConst = 0;
                        ind->flags = 0;

                        if (nd->type == IROLinearOp2Arg) {
                            isUnsigned = is_unsigned(nd->rtype);
                            if (IRO_IsIntConstant(nd->u.diadic.right)) {
                                CInt64 val = nd->u.diadic.right->u.node->data.intval;
                                if (IS_LINEAR_DIADIC(nd, EADDASS) &&
                                    CInt64_Less(val, cint64_zero)) {
                                    nd->nodetype = ESUBASS;
                                    nd->u.diadic.right->u.node->data.intval = CInt64_Neg(val);
                                }
                                if (isUnsigned) {
                                    CInt64_ConvertUInt32(&nd->u.diadic.right->u.node->data.intval);
                                    ind->addConst =
                                        CInt64_GetULong(&nd->u.diadic.right->u.node->data.intval);
                                } else {
                                    CInt64_ConvertInt32(&nd->u.diadic.right->u.node->data.intval);
                                    ind->addConst =
                                        CInt64_GetULong(&nd->u.diadic.right->u.node->data.intval);
                                }
                                ind->addNode = NULL;
                            } else if (nd->nodetype == EADDASS || nd->nodetype == ESUBASS) {
                                ind->addNode = nd->u.diadic.right;
                            } else if (nd->nodetype == EASS) {
                                ind->addNode = nd->u.diadic.right->u.diadic.right;
                            }
                        } else {
                            if (IS_TYPE_POINTER_ONLY(nd->rtype)) {
                                ind->addConst = TPTR_TARGET(nd->rtype)->size;
                            } else {
                                ind->addConst = 1;
                            }
                            ind->addNode = NULL;
                        }

                        FirstInd = ind;

                        if (IS_LINEAR_DIADIC_2(nd, EADDASS, ESUBASS)) {
                            if (nd->u.diadic.right->flags & IROLF_LoopInvariant) {
                                IRO_Dump(
                                    "Found induction variable the new way: %s\n",
                                    var->object->name->name
                                );
                            } else {
                                IRO_Dump(
                                    "Found induction variable the old way: %s\n",
                                    var->object->name->name
                                );
                            }
                        } else if (nd->type ==
                                   IROLinearOp2Arg &&
                                   (nd->u.diadic.right->nodetype ==
                                    EADD ||
                                    nd->u.diadic.right->nodetype == ESUB)) {
                            IRO_Dump(
                                "Found induction variable the new way: %s\n",
                                var->object->name->name
                            );
                        } else {
                            IRO_Dump(
                                "Found induction variable the old way: %s\n",
                                var->object->name->name
                            );
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
}

static void IRO_ActUnmarkLoopInvariance(IROLinear* linear, Boolean isFirst) {
    if (isFirst) {
        linear->flags &= ~IROLF_LoopInvariant;
    }
}

static void UnmarkSubexpressionsOfInvariantExpressions(void) {
    IROExpr* expr;

    for (expr = IRO_FirstExpr; expr; expr = expr->next) {
        if (Bv_IsBitSet(expr->node->index, InLoop) &&
            !expr->notSubable &&
            (!expr->couldError || expr->node->mustreach) &&
            (expr->linear->flags & IROLF_LoopInvariant)) {
            IRO_WalkTree(expr->linear, IRO_ActUnmarkLoopInvariance);
            expr->linear->flags |= IROLF_LoopInvariant;
        }
    }
}

static void MyHandleLoop_Vector(IRONode* fnode) {
    IRONode* pred;
    UInt16 i;
    IROExpr* expr;
    IROExpr* removedExprs;
    IROExpr* exprnext;
    IROLoopInd* induction;
    IROExpr* exprinner;
    Boolean flag24;
    IRONode* v2;
    IRONode* node22;
    int flag21;
    IRONode* iter;
    IROExpr* searchris;
    IROLinear* reduceNd1;
    VarRecord* var;
    IROLinear* reduceNd2;

    IRO_FirstExpr = NULL;
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

    v2 = NULL;
    flag21 = FALSE;
    node22 = NULL;
    for (i = 0; i < LoopNode->numpred; i++) {
        pred = IRO_NodeTable[LoopNode->pred[i]];
        if (!Bv_IsBitSet(pred->index, InLoop)) {
            if (!flag21) {
                node22 = pred;
            } else {
                node22 = NULL;
            }
            flag21 = TRUE;
            if (pred->nextnode == fnode) {
                CError_ASSERT(2377, v2 == NULL || pred == v2);
                v2 = pred;
            }
        }
    }

    if (!flag21) {
        IRO_Dump("No predecessor outside the loop\n");
        return;
    }

    if (!node22 || node22->last->type != IROLinearGoto) {
        node22 = CreatePreHeader(fnode, v2);
    }
    PredInt = node22->last;

    if (PredInt->type ==
        IROLinearGoto &&
        PredInt->u.label.label != LoopNode->first->u.label.label) {
        if (!KnownBounds || !Times) {
            for (iter = IRO_FirstNode; iter; iter = iter->nextnode) {
                iter->mustreach = FALSE;
            }
        } else {
            PredInt->u.label.label = LoopNode->first->u.label.label;
            IRO_ComputeSuccPred();
        }
    }

    if (copts.loopinvariants || copts.strengthreduction) {
        MoveInvarianceInAddressExpr();
        IRO_DumpAfterPhase("MoveInvarianceInAddressExpr", 0);
        IRO_FindExpressions(InLoop, 1);
        IRO_DumpExprs();
    }

    if (copts.loopinvariants) {
        UnmarkSubexpressionsOfInvariantExpressions();
    }

    if (!copts.optimizesize && copts.strengthreduction) {
        for (expr = IRO_FirstExpr; expr; expr = expr->next) {
            if (!(expr->state & 4) &&
                Bv_IsBitSet(expr->node->index, InLoop) &&
                !expr->notSubable &&
                (!expr->couldError || expr->node->mustreach) &&
                Reducable(expr->linear, &reduceNd1, &reduceNd2, &var)) {
                IRO_WalkTree(expr->linear, IRO_ActUnmarkRISCandidate);
                expr->linear->flags |= IROLF_Ris;
                expr->factor = reduceNd1;
                expr->var = var;
                expr->expr = reduceNd2;
            }
        }
    }

    if (!copts.optimizesize && copts.strengthreduction) {
        RisList = NULL;
        for (expr = IRO_FirstExpr; expr; expr = exprnext) {
            exprnext = expr->next;
            if (!(expr->state & 4) && (expr->linear->flags & IROLF_Ris)) {
                reduceNd1 = expr->factor;
                var = expr->var;
                reduceNd2 = expr->expr;

                induction = FirstInd;
                while (induction && induction->var != var) {
                    induction = induction->next;
                }
                CError_ASSERT(3035, induction != NULL);

                IRO_FindDepends(reduceNd1);
                if (!Bv_BitsInCommon(IRO_Depends, AllKills)) {
                    IRO_Dump("Found reduction in strength: %d\n", expr->linear->index);
                    if (IS_LINEAR_DIADIC(expr->linear, ESHL)) {
                        expr->linear->nodetype = EMUL;
                        CInt64_SetULong(
                            &reduceNd1->u.node->data.intval,
                            1 << CInt64_GetULong(&reduceNd1->u.node->data.intval)
                        );
                        reduceNd1->u.node->rtype = expr->linear->rtype;
                    }

                    searchris = RisList;
                    while (1) {
                        if (!searchris) {
                            break;
                        }
                        if (IRO_ExprsSame(expr->linear, searchris->linear)) {
                            break;
                        }
                        searchris = searchris->next;
                    }

                    if (searchris) {
                        IRO_Dump("Using existing RIS: %d\n", searchris->linear->index);
                        IRO_WalkTree(expr->linear, IRO_RemoveExpr_Action);
                    } else {
                        searchris = CreateRIS(expr, reduceNd2, reduceNd1, induction, 0);
                    }
                    IRO_ReplaceReference(expr->linear, searchris->temp, expr->linear);
                    IRO_ClipExpr(expr);
                }
            }
        }
    }

    RisList = NULL;

    expr = IRO_FirstExpr;
    removedExprs = NULL;
    if (copts.loopinvariants) {
        while (expr) {
            exprnext = expr->next;
            flag24 = FALSE;

            if (Bv_IsBitSet(expr->node->index, InLoop) &&
                !expr->notSubable &&
                (!expr->couldError || expr->node->mustreach) &&
                (expr->linear->flags & IROLF_LoopInvariant) &&
                !(expr->state & 4)) {
                IRO_Dump("Found loop invariant: %d\n", expr->linear->index);

                for (exprinner = removedExprs; exprinner; exprinner = exprinner->next) {
                    if (IRO_ExprsSame(exprinner->linear, expr->linear)) {
                        IRO_ReplaceReference(expr->linear, exprinner->temp, expr->linear);
                        IRO_NopOut(expr->linear);
                        IRO_Dump("Using already removed expr: %d\n", exprinner->linear->index);
                        IRO_RemoveExpr(expr);
                        flag24 = TRUE;
                        break;
                    }
                }

                if (!flag24 && !expr->temp) {
                    IRO_GetTemp(expr);
                    IRO_ReplaceReference(expr->linear, expr->temp, expr->linear);
                    IRO_MoveExpression(expr, PredInt);
                    IRO_AssignToTemp(expr);
                    IRO_RemoveExpr(expr);
                    expr->next = removedExprs;
                    removedExprs = expr;
                }
            }

            expr = exprnext;
        }
    }
}

static void MyHandleLoop_Motion(IRONode* fnode) {
    IROLoopMemRef* memref;
    IRONode* pred;
    UInt16 i;
    IRONode* v2;
    IRONode* node21;
    int flag20;
    IRONode* iter;
    IROLinear* ass;
    Object* tempobj;
    VarRecord* var;
    Boolean checkflag;
    IROLoop* loop;
    IROList list;
    IROElmList* refiter;

    IRO_FirstExpr = NULL;
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

    v2 = NULL;
    flag20 = FALSE;
    node21 = NULL;
    for (i = 0; i < LoopNode->numpred; i++) {
        pred = IRO_NodeTable[LoopNode->pred[i]];
        if (!Bv_IsBitSet(pred->index, InLoop)) {
            if (!flag20) {
                node21 = pred;
            } else {
                node21 = NULL;
            }
            flag20 = TRUE;
            if (pred->nextnode == fnode) {
                CError_ASSERT(3388, v2 == NULL || pred == v2);
                v2 = pred;
            }
        }
    }

    if (!flag20) {
        IRO_Dump("No predecessor outside the loop\n");
        return;
    }

    if (!node21 || node21->last->type != IROLinearGoto) {
        node21 = CreatePreHeader(fnode, v2);
    }
    PredInt = node21->last;

    if (PredInt->type ==
        IROLinearGoto &&
        PredInt->u.label.label != LoopNode->first->u.label.label) {
        if (!KnownBounds || !Times) {
            for (iter = IRO_FirstNode; iter; iter = iter->nextnode) {
                iter->mustreach = FALSE;
            }
        } else {
            PredInt->u.label.label = LoopNode->first->u.label.label;
            IRO_ComputeSuccPred();
        }
    }

    if (copts.loopinvariants || copts.strengthreduction) {
        MoveInvarianceInAddressExpr();
        IRO_DumpAfterPhase("MoveInvarianceInAddressExpr", 0);
        IRO_FindExpressions(InLoop, 0);
        IRO_DumpExprs();
    }

    if (copts.loopinvariants) {
        UnmarkSubexpressionsOfInvariantExpressions();
    }

    checkflag = TRUE;
    Bv_AllocVector(&InLoop_Exits, IRO_NumNodes + 1);
    Bv_AllocVector(&InLoop_Tails, IRO_NumNodes + 1);
    LoopExitNumber = 0;
    LoopExitSuccessor = NULL;
    LoopTailNum = 0;
    LoopTail = NULL;
    IRO_FindLoopTailsAndExits(fnode);
    FindMustReach1(fnode);

    loop = NULL;
    if (LoopTailNum == 1) {
        if (fnode->last->type == IROLinearIf || fnode->last->type == IROLinearIfNot) {
            loop = ExtractLoopInfo(fnode);
        } else if (LoopTail->last->type == IROLinearIf || LoopTail->last->type == IROLinearIfNot) {
            loop = ExtractLoopInfo(LoopTail);
        }
    }

    if (loop && !(loop->flags & LP_IFEXPR_NON_CANONICAL) && LoopExitNumber == 1) {
        IRO_LoopMemRefFirst = NULL;
        IRO_LoopMemRefCurrent = NULL;
        CheckAllLoopAddresses(IRO_FirstNode, InLoop, &checkflag);
        CheckAllLoopAddresses(IRO_FirstNode, InLoop, &checkflag);
        if (checkflag) {
            for (memref = IRO_LoopMemRefFirst; memref; memref = memref->next) {
                if (memref->flags & LoopMemRef_10) {
                    memref->flags |= LoopMemRef_8;
                }
            }

            for (memref = IRO_LoopMemRefFirst; memref; memref = memref->next) {
                IRO_Dump("Loop Motion Candidate:: Int= %d", memref->nd->index);
                if (memref->flags & LoopMemRef_8) {
                    IRO_Dump("<invalid>");
                }
                if (memref->flags & LoopMemRef_1) {
                    IRO_Dump("<load>");
                }
                if (memref->flags & LoopMemRef_2) {
                    IRO_Dump("<store>");
                }
                IRO_Dump("\n");

                if (!(memref->flags & LoopMemRef_8)) {
                    tempobj = create_temp_object(memref->nd->rtype);
                    IRO_FindVar(tempobj, TRUE, TRUE);
                    if (IRO_FindVar(
                            ((IROLinear*)memref->rec->objRefs->element)->u.node->data.objref, 0, 1
                        )) {
                        IRO_InitList(&list);
                        ass = IRO_NewLinear(IROLinearOp2Arg);
                        ass->index = ++IRO_NumLinear;
                        ass->rtype = memref->nd->rtype;
                        ass->nodetype = EASS;
                        ass->u.diadic.left = IRO_TempReference(tempobj, &list);
                        ass->u.diadic.left->flags |= IROLF_Ind | IROLF_Assigned;
                        ass->u.diadic.left->u.monadic->flags |= IROLF_Ind | IROLF_Assigned;
                        ass->u.diadic.right = IRO_DuplicateExpr(memref->nd, &list);
                        IRO_AddToList(ass, &list);
                        IRO_Paste(list.head, list.tail, PredInt);
                    } else {
                        CError_FATAL(3631);
                    }

                    if (LoopExitSuccessor->numpred != 1) {
                        LoopExitSuccessor = CreateNewLoopExitSuccessor(LoopExitSuccessor);
                    }

                    IRO_InitList(&list);
                    ass = IRO_NewLinear(IROLinearOp2Arg);
                    ass->index = ++IRO_NumLinear;
                    ass->rtype = memref->nd->rtype;
                    ass->nodetype = EASS;
                    ass->u.diadic.left = IRO_DuplicateExpr(memref->nd, &list);
                    ass->u.diadic.left->flags |= IROLF_Ind | IROLF_Assigned;
                    ass->u.diadic.left->u.monadic->flags |= IROLF_Ind | IROLF_Assigned;
                    ass->u.diadic.right = IRO_TempReference(tempobj, &list);
                    IRO_AddToList(ass, &list);

                    if (LoopExitSuccessor->first->type == IROLinearLabel) {
                        IRO_PasteAfter(list.head, list.tail, LoopExitSuccessor->first);
                    } else {
                        IRO_Paste(list.head, list.tail, LoopExitSuccessor->first);
                    }

                    for (refiter = memref->list; refiter; refiter = refiter->next) {
                        IRO_Dump(
                            "Loop Motion Candidate Reference at Int= %d\n",
                            ((IROLinear*)refiter->element)->index
                        );
                        IRO_InitList(&list);
                        IRO_TempReference(tempobj, &list);
                        IRO_Paste(list.head, list.tail, refiter->element);
                        IRO_LocateFather_Cut_And_Paste(refiter->element, list.tail);
                    }
                }
            }
        }
    }

    IRO_DumpAfterPhase("After Motion", 0);
}

static Boolean CheckLoopAddress(IROLinear* nd, Boolean mustreach1) {
    IROLoopMemRef* memref;
    IROAddrRecord* rec;
    IROAddrRecord* otherrec;
    IROLinear* inner;
    IROElmList* list;
    Boolean flag;

    inner = nd->u.monadic;
    rec = IRO_InitAddrRecordPointer(inner);
    if (IS_LINEAR_ENODE(inner, EOBJREF)) {
        rec->numObjRefs++;
        IRO_AddElmToList(inner, &rec->objRefs);
    } else if (IS_LINEAR_DIADIC(inner, EADD)) {
        IRO_DecomposeAddressExpression(inner, rec);
    } else {
        return FALSE;
    }

    if (rec->numObjRefs != 1) {
        return FALSE;
    }

    flag = (nd->flags & IROLF_CouldError) || ((nd->flags & IROLF_Reffed) == 0);
    if (!IRO_LoopMemRefFirst) {
        MakeLoopEntry(nd, rec, mustreach1, flag);
    } else {
        for (memref = IRO_LoopMemRefFirst; memref; memref = memref->next) {
            otherrec = memref->rec;
            if (((IROLinear*)rec->objRefs->element)->u.node->data.objref ==
                ((IROLinear*)otherrec->objRefs->element)->u.node->data.objref) {
                if (IRO_ExprsSame(inner, otherrec->linear)) {
                    list = oalloc(sizeof(IROElmList));
                    list->element = nd;
                    list->next = NULL;
                    if (!memref->list) {
                        memref->list = list;
                    } else {
                        list->next = memref->list;
                        memref->list = list;
                    }
                } else {
                    memref->flags |= LoopMemRef_8;
                }

                if (!mustreach1 && flag) {
                    memref->flags |= LoopMemRef_8;
                }
                if (mustreach1 || flag) {
                    memref->flags &= ~LoopMemRef_10;
                }
                break;
            }
        }

        if (!memref) {
            MakeLoopEntry(nd, rec, mustreach1, flag);
        }
    }

    return TRUE;
}

static void CheckAllLoopAddresses(IRONode* fnode, BitVector* bv, Boolean* resultFlag) {
    IROLinear* nd;
    Boolean flag;

    flag = *resultFlag;
    while (fnode) {
        if (Bv_IsBitSet(fnode->index, bv) && (nd = fnode->first)) {
            while (flag) {
                if (nd->type == IROLinearOp1Arg && nd->nodetype == EINDIRECT) {
                    flag = CheckLoopAddress(nd, fnode->mustreach1);
                    // 2.6: a call of an intrinsic no longer clears the flag, an asm statement does
                    // (Pro7: any call). 2.6's code tests for a call twice, as if a macro testing the
                    // kind again were used; one test does not match.
                } else if ((nd->type ==
                            IROLinearFunccall &&
                            (nd->type ==
                             IROLinearFunccall &&
                             !(nd->u.funccall.functype->flags & FUNC_INTRINSIC))) ||
                           nd->type == IROLinearAsm) {
                    flag = FALSE;
                }

                if (nd == fnode->last) {
                    break;
                }
                nd = nd->next;
            }

            if (!flag) {
                break;
            }
        }
        fnode = fnode->nextnode;
    }
    *resultFlag = flag;
}

static void MakeLoopEntry(IROLinear* nd, IROAddrRecord* rec, Boolean mustreach1, Boolean flag2) {
    IROElmList* list;

    if (!IRO_IsRegable(((IROLinear*)rec->objRefs->element)->u.node->data.objref) &&
        (nd->u.monadic->flags & IROLF_LoopInvariant) &&
        !IRO_HasSideEffect(nd)) {
        if (!IRO_LoopMemRefFirst) {
            IRO_LoopMemRefCurrent = IRO_LoopMemRefFirst = oalloc(sizeof(IROLoopMemRef));
        } else {
            IRO_LoopMemRefCurrent->next = oalloc(sizeof(IROLoopMemRef));
            IRO_LoopMemRefCurrent = IRO_LoopMemRefCurrent->next;
        }
        IRO_LoopMemRefCurrent->flags = LoopMemRef_10;
        IRO_LoopMemRefCurrent->nd = nd;
        IRO_LoopMemRefCurrent->rec = rec;
        IRO_LoopMemRefCurrent->next = NULL;
        IRO_LoopMemRefCurrent->list = NULL;

        list = oalloc(sizeof(IROElmList));
        list->element = nd;
        list->next = NULL;
        if (!IRO_LoopMemRefCurrent->list) {
            IRO_LoopMemRefCurrent->list = list;
        } else {
            list->next = IRO_LoopMemRefCurrent->list;
            IRO_LoopMemRefCurrent->list = list;
        }

        if (((IROLinear*)rec->objRefs->element)->flags & IROLF_Assigned) {
            IRO_LoopMemRefCurrent->flags |= LoopMemRef_2;
            if (((IROLinear*)rec->objRefs->element)->flags & IROLF_Used) {
                IRO_LoopMemRefCurrent->flags |= LoopMemRef_1;
            }
        } else {
            IRO_LoopMemRefCurrent->flags |= LoopMemRef_1;
        }

        if (!mustreach1 && flag2) {
            IRO_LoopMemRefCurrent->flags |= LoopMemRef_8;
        }

        if (mustreach1 || flag2) {
            IRO_LoopMemRefCurrent->flags &= ~LoopMemRef_10;
        }
    }
}

void FindAssignmenttoInductionVar(IROLoop* loop, IRONode* fnode) {
    IROLinear* nd;
    UInt32 index;

    if (!loop->induction) {
        loop->init = NULL;
        return;
    }

    index = loop->induction->var->index;
    for (nd = fnode->first; nd; nd = nd->next) {
        Bv_Clear(IRO_VarKills);
        IRO_GetKills(nd);
        if (Bv_IsBitSet(index, IRO_VarKills)) {
            loop->init = nd;
        }

        if (nd == fnode->last) {
            break;
        }
    }

    if (loop->init && loop->init->type != IROLinearOp2Arg) {
        loop->init = NULL;
    }
}

static void ComputeIntWeight(IROLoop* loop, IROLinear* Int) {
    loop->count++;
}

static IROLinear* IsTypconVar(IROLinear* nd) {
    if (IS_LINEAR_MONADIC(nd, ETYPCON) && IRO_IsVariable(nd->u.monadic)) {
        return nd->u.monadic;
    } else {
        return NULL;
    }
}

// TODO: not exact (93.1: esi/edi swapped in the induction-step and loop-body parts, one byte
// counter's stack slot). Pro7's text with 2.6's changes reads 65.6; the extra variables (`ind`,
// `right2`, `scannd2`, `right18` reused for the swapped compare) are an earlier pass's search
// toward 2.6's registers, not evidence of 2.6's source.
IROLoop* ExtractLoopInfo(IRONode* fnode) {
    Boolean flag30;
    Boolean flag29;
    Boolean flag28;
    UInt32 counter27;
    IROLoopInd* ind23;
    Object* obj;
    IROLinear* nd21;
    IRONode* scanfnode;
    IROLinear* left19;
    IROLinear* tmp18;
    IROLinear* scannd;
    IROLinear* scannd2;
    IROLinear* tmp;
    VarRecord* var;
    IROLoopInd* scanind;
    IROLoopInd* ind;
    IROLinear* right18;
    IROLinear* left;
    IROLinear* right;
    IROLinear* right2;
    UInt32 counter2;
    IROLoop* loop;
    UInt32 i;
    SInt32 hi;

    flag30 = FALSE;
    flag29 = FALSE;
    flag28 = 0;
    counter27 = 0;
    LoopNode = fnode;

    loop = oalloc(sizeof(IROLoop));
    loop->fnode = fnode;
    nd21 = LoopNode->last->u.label.cond;
    loop->cond = nd21;
    loop->flags = 0;
    loop->unused = 0;
    loop->init = NULL;
    loop->induction = NULL;
    loop->lo = -1;
    loop->hi = -1;
    loop->count = 0;
    // 2.6: renumbers the nodes first (lo and hi below compare their indices)
    IRO_RenumberInts();

    if (nd21->type == IROLinearOp2Arg && IS_TYPE_INT(nd21->rtype)) {
        ind23 = NULL;
        left19 = nd21->u.diadic.left;
        right18 = nd21->u.diadic.right;
        if (IRO_IsVariable(left19) || (left19 = IsTypconVar(left19))) {
            if ((var = IRO_FindVar(left19->u.monadic->u.node->data.objref, FALSE, TRUE))) {
                scanind = FirstInd;
                while (scanind && scanind->var != var) {
                    scanind = scanind->next;
                }
                if (scanind) {
                    ind23 = scanind;
                    loop->flags |= LoopFlags_1;
                    loop->induction = scanind;
                }
            }
        }

        if (IRO_IsVariable(right18) || (right18 = IsTypconVar(right18))) {
            if ((var = IRO_FindVar(right18->u.monadic->u.node->data.objref, FALSE, TRUE))) {
                scanind = FirstInd;
                while (scanind && scanind->var != var) {
                    scanind = scanind->next;
                }
                if (scanind) {
                    ind23 = scanind;
                    loop->flags &= ~LoopFlags_1;
                    loop->induction = scanind;
                }
            }
        }

        if (ind23 && ind23->addConst > 0) {
            if (loop->flags & LoopFlags_1) {
                if (loop->cond->type !=
                    IROLinearOp2Arg ||
                    !(loop->cond->nodetype ==
                      ELESS ||
                      loop->cond->nodetype ==
                      EGREATER ||
                      loop->cond->nodetype ==
                      ELESSEQU ||
                      loop->cond->nodetype == EGREATEREQU) ||
                    !loop->cond->u.diadic.left ||
                    !loop->cond->u.diadic.left->rtype ||
                    !IS_TYPE_INT(loop->cond->u.diadic.left->rtype) ||
                    !loop->cond->u.diadic.right ||
                    !loop->cond->u.diadic.right->rtype ||
                    !IS_TYPE_INT(loop->cond->u.diadic.right->rtype)) {
                    loop->flags |= LP_IFEXPR_NON_CANONICAL;
                    return loop;
                }
            } else {
                if (loop->cond->type ==
                    IROLinearOp2Arg &&
                    (loop->cond->nodetype == EGREATER || loop->cond->nodetype == EGREATEREQU) &&
                    (right18 = loop->cond->u.diadic.left) &&
                    right18->rtype &&
                    IS_TYPE_INT(right18->rtype) &&
                    (right = loop->cond->u.diadic.right) &&
                    right->rtype &&
                    IS_TYPE_INT(right->rtype)) {
                    loop->cond->u.diadic.left = right;
                    loop->cond->u.diadic.right = right18;
                    if (loop->cond->nodetype == EGREATER) {
                        loop->cond->nodetype = ELESS;
                    } else if (loop->cond->nodetype == EGREATEREQU) {
                        loop->cond->nodetype = ELESSEQU;
                    }
                    loop->flags |= LoopFlags_1;
                } else if (loop->cond->type ==
                           IROLinearOp2Arg &&
                           (loop->cond->nodetype == ELESS || loop->cond->nodetype == ELESSEQU) &&
                           (left = loop->cond->u.diadic.left) &&
                           left->rtype &&
                           IS_TYPE_INT(left->rtype) &&
                           (right2 = loop->cond->u.diadic.right) &&
                           right2->rtype &&
                           IS_TYPE_INT(right2->rtype)) {
                    loop->cond->u.diadic.left = right2;
                    loop->cond->u.diadic.right = left;
                    if (loop->cond->nodetype == ELESS) {
                        loop->cond->nodetype = EGREATER;
                    } else if (loop->cond->nodetype == ELESSEQU) {
                        loop->cond->nodetype = EGREATEREQU;
                    }
                    loop->flags |= LoopFlags_1;
                } else {
                    loop->flags |= LP_IFEXPR_NON_CANONICAL;
                    return loop;
                }
            }
        } else {
            loop->flags |= LP_INDUCTION_NOT_FOUND;
            return loop;
        }
    } else if (nd21->type == IROLinearOp1Arg && IS_TYPE_INT(nd21->rtype)) {
        if (nd21->nodetype ==
            EPREINC ||
            nd21->nodetype ==
            EPOSTINC ||
            nd21->nodetype ==
            EPREDEC ||
            nd21->nodetype == EPOSTDEC) {
            if (IRO_IsVariable(nd21->u.monadic)) {
                if ((var = IRO_FindVar(
                         nd21->u.monadic->u.monadic->u.node->data.objref, FALSE, TRUE
                     ))) {
                    scanind = FirstInd;
                    while (scanind && scanind->var != var) {
                        scanind = scanind->next;
                    }
                    if (scanind) {
                        ind23 = scanind;
                        loop->flags |= LoopFlags_10000;
                        loop->induction = scanind;
                    } else {
                        loop->flags |= LP_INDUCTION_NOT_FOUND;
                        return loop;
                    }
                } else {
                    loop->flags |= LP_INDUCTION_NOT_FOUND;
                    return loop;
                }
            } else {
                loop->flags |= LP_INDUCTION_NOT_FOUND;
                return loop;
            }
        } else {
            loop->flags |= LP_INDUCTION_NOT_FOUND;
            return loop;
        }
    } else {
        loop->flags |= LP_IFEXPR_NON_CANONICAL;
        return loop;
    }

    counter2 = 0;
    scanind = FirstInd;
    while (scanind) {
        counter2++;
        scanind = scanind->next;
    }

    if (counter2 > 1) {
        loop->flags |= LP_HAS_MULTIPLE_INDUCTIONS;
    }

    if ((ind = loop->induction)) {
        nd21 = ind->nd;
        if (nd21->type == IROLinearOp2Arg) {
            if (IS_LINEAR_DIADIC_2(loop->cond, ELESS, ELESSEQU)) {
                if (nd21->nodetype == EADDASS) {
                    if (ind->addConst == 1) {
                        loop->flags |= LP_LOOP_STEP_ISADD;
                    }
                    if (ind->addConst > 0) {
                        loop->flags |= LP_LOOP_STEP_ISPOS;
                    }
                } else if (nd21->nodetype ==
                           EASS &&
                           IS_LINEAR_DIADIC(nd21->u.diadic.right, EADD) &&
                           IRO_IsIntConstant(tmp18 = nd21->u.diadic.right->u.diadic.right) &&
                           // TODO: as in Reducable, 2.6 tests the high word in a register;
                           // the dead store to `hi` stands in (the plain field reads 92.7)
                           (hi = tmp18->u.node->data.intval.hi) == 0) {
                    if (CInt64_GetULong(&tmp18->u.node->data.intval) == 1) {
                        loop->flags |= LP_LOOP_STEP_ISADD;
                    }
                    if (CInt64_GetULong(&tmp18->u.node->data.intval) > 0) {
                        loop->flags |= LP_LOOP_STEP_ISPOS;
                    }
                }
            }
        } else if (nd21->type == IROLinearOp1Arg) {
            if (nd21->nodetype == EPREINC || nd21->nodetype == EPOSTINC) {
                if (ind->addConst == 1) {
                    loop->flags |= LP_LOOP_STEP_ISADD;
                }
                if (ind->addConst > 0) {
                    loop->flags |= LP_LOOP_STEP_ISPOS;
                }
            }
            if (nd21->nodetype == EPREDEC || nd21->nodetype == EPOSTDEC) {
                if (ind->addConst == 1) {
                    loop->flags |= LoopFlags_2000;
                }
                if (ind->addConst > 0) {
                    loop->flags |= LP_LOOP_STEP_ISNEG;
                }
            }
        }
        loop->hi = nd21->index;
        loop->lo = IRO_FindStart(nd21)->index;
    }

    if (ind23) {
        tmp = IRO_FindStart(fnode->last->u.label.cond);
        loop->flags |= LoopFlags_200;
        if (loop->flags & LoopFlags_10000) {
            for (scannd = loop->fnode->first; scannd && scannd != tmp; scannd = scannd->next) {
                if (scannd->type != IROLinearLabel && scannd->type != IROLinearNop) {
                    loop->flags &= ~LoopFlags_200;
                }
            }
        } else {
            for (scannd = ind23->nd->next; scannd && scannd != tmp; scannd = scannd->next) {
                if (scannd->type != IROLinearLabel && scannd->type != IROLinearNop) {
                    loop->flags &= ~LoopFlags_200;
                }
            }
            for (scannd2 = loop->fnode->first; scannd2 && scannd2 != tmp; scannd2 = scannd2->next) {
                if ((scannd2->index < loop->lo || scannd2->index > loop->hi) &&
                    scannd2->type !=
                    IROLinearLabel &&
                    scannd2->type != IROLinearNop) {
                    loop->flags &= ~LoopFlags_200;
                }
            }
        }
    }

    for (scanfnode = IRO_FirstNode; scanfnode; scanfnode = scanfnode->nextnode) {
        if (Bv_IsBitSet(scanfnode->index, InLoop) &&
            scanfnode !=
            fnode &&
            (scannd2 = scanfnode->first)) {
            while (1) {
                if (scannd2->type == IROLinearFunccall) {
                    flag30 = TRUE;
                }
                if (scannd2->type == IROLinearGoto) {
                    flag28++;
                }
                if (flag28 >= 1) {
                    flag29 = TRUE;
                }
                if (scannd2->type ==
                    IROLinearIf ||
                    scannd2->type ==
                    IROLinearIfNot ||
                    scannd2->type ==
                    IROLinearSwitch ||
                    scannd2->type == IROLinearReturn) {
                    flag29 = TRUE;
                }

                if (scannd2->type == IROLinearAsm) {
                    loop->flags |= LP_LOOP_HAS_ASM;
                }

                if (!(scannd2->flags & IROLF_Reffed) &&
                    scannd2->type !=
                    IROLinearNop &&
                    scannd2->type != IROLinearLabel) {
                    if (!IRO_IsAssignOp[scannd2->nodetype]) {
                        loop->flags |= LoopFlags_8;
                    } else if (scannd2->index < loop->lo || scannd2->index > loop->hi) {
                        counter27++;
                        if (IsAssignmentReductionCandidate(scannd2) && counter27 == 1) {
                            loop->flags |= LoopFlags_40000;
                        }
                        if (counter27 > 1) {
                            loop->flags &= ~LoopFlags_40000;
                        }
                    }
                }

                if (scannd2->index < loop->lo || scannd2->index > loop->hi) {
                    if (IS_LINEAR_ENODE(scannd2, EOBJREF)) {
                        obj = scannd2->u.node->data.objref;
                        if ((scannd2->flags & IROLF_Ind) &&
                            obj &&
                            obj == loop->induction->var->object) {
                            // 2.6: `||` (Pro7 `&&`)
                            if (!(scannd2->flags & IROLF_Assigned) ||
                                (scannd2->flags & IROLF_Used)) {
                                IRO_Dump("Induction Used in loop\n");
                                loop->flags |= LoopFlags_800;
                            }
                        }
                    }

                    if (IS_LINEAR_DIADIC(scannd2, ESHR) &&
                        (obj = IRO_IsVariable(scannd2->u.diadic.left)) &&
                        IRO_IsIntConstant(scannd2->u.diadic.right)) {
                        for (scanind = FirstInd; scanind; scanind = scanind->next) {
                            if (scanind->var->object == obj) {
                                IRO_Dump("Induction has DIV: %s\n", obj->name->name);
                                scanind->flags |= LoopInd_HasDiv;
                            }
                        }
                    }

                    if (IS_LINEAR_DIADIC(scannd2, EAND) &&
                        (obj = IRO_IsVariable(scannd2->u.diadic.left)) &&
                        IRO_IsIntConstant(scannd2->u.diadic.right)) {
                        for (scanind = FirstInd; scanind && obj; scanind = scanind->next) {
                            if (scanind->var->object == obj) {
                                IRO_Dump("Induction has MOD: %s\n", obj->name->name);
                                scanind->flags |= LoopInd_HasMod;
                            }
                        }
                    }
                }

                ComputeIntWeight(loop, scannd2);
                if (scannd2 == scanfnode->last) {
                    break;
                }
                scannd2 = scannd2->next;
            }
        }

        if (flag30) {
            loop->flags |= LP_LOOP_HAS_CALLS;
        }
        if (flag29) {
            loop->flags |= LP_LOOP_HAS_CNTRLFLOW;
        }

        for (i = 0; i < scanfnode->numsucc && scanfnode != fnode; i++) {
            if (Bv_IsBitSet(scanfnode->index, InLoop) && !Bv_IsBitSet(scanfnode->succ[i], InLoop)) {
                IRO_Dump(
                    "Node %d has an out of loop successor %d\n",
                    scanfnode->index,
                    scanfnode->succ[i]
                );
                IRO_DumpBits("Loop includes: ", InLoop);
                IRO_Dump("loop has multiple exits\n");
                loop->flags |= LoopFlags_1000;
            }
        }
    }

    return loop;
}

CLabel* BuildLabel(IROList* list) {
    IROLinear* nd;

    nd = IRO_NewLinear(IROLinearLabel);
    nd->index = IRO_NumLinear++;
    nd->u.label.label = IRO_NewLabel();
    nd->flags |= IROLF_1;
    IRO_AddToList(nd, list);
    return nd->u.label.label;
}

static void MoveInvarianceInAddressExpr(void) {
    IROLinear* nd;
    IRONode* fnode;
    IROLinear* start;
    IROList list;

    IRO_FirstExpr = IRO_LastExpr = NULL;
    IRO_NumExprs = 0;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        if (Bv_IsBitSet(fnode->index, InLoop)) {
            for (nd = fnode->first; nd; nd = nd->next) {
                if (nd->type ==
                    IROLinearOp1Arg &&
                    (nd->nodetype == EINDIRECT || nd->nodetype == EINDIRECT) &&
                    IS_LINEAR_DIADIC(nd->u.monadic, EADD)) {
                    RearrangeInvarianceInAddressExpr(nd->u.monadic, &list);
                    start = IRO_FindStart(nd->u.monadic);
                    IRO_LocateFather_Cut_And_Paste(nd->u.monadic, list.tail);
                    IRO_Paste(list.head, list.tail, start);
                }

                if (nd == fnode->last) {
                    break;
                }
            }
        }
    }

    IRO_UpdateFlagsOnInts();
    ComputeLoopInvariance();
}

static void AddAddendLinear(IROLinear* nd) {
    IROElmList* list;

    if (IS_LINEAR_DIADIC(nd, EADD)) {
        AddAddendLinear(nd->u.diadic.left);
        AddAddendLinear(nd->u.diadic.right);
    } else {
        list = oalloc(sizeof(IROElmList));
        list->element = nd;
        list->next = NULL;
        if (FirstAddendLinear) {
            LastAddendLinear->next = list;
        } else {
            FirstAddendLinear = list;
        }
        LastAddendLinear = list;
    }
}

// TODO: not exact (99.5). 2.6 stores `elist = NULL` before `result` and the first loop's
// load, Pro7's order (`elist = NULL; result = NULL; for (scanlist = ...)`); written so, the
// second loop's `scanlist` and `elist` swap registers (94.2, with every declaration order and
// loop-header spelling tried, and split variables for the second loop).
static IROLinear* RearrangeInvarianceInAddressExpr(IROLinear* nd, IROList* list) {
    IROLinear* scannd;
    IROLinear* result;
    IROElmList* scanlist;
    IROElmList* elist;

    IRO_InitList(list);
    FirstAddendLinear = LastAddendLinear = NULL;
    AddAddendLinear(nd->u.diadic.left);
    AddAddendLinear(nd->u.diadic.right);

    result = NULL;
    for (scanlist = FirstAddendLinear, elist = NULL; scanlist; scanlist = scanlist->next) {
        scannd = scanlist->element;
        if (!IRO_IsIntConstant(scannd) && (scannd->flags & IROLF_LoopInvariant)) {
            if (result) {
                IROLinear* tmp = IRO_NewLinear(IROLinearOp2Arg);
                tmp->index = ++IRO_NumLinear;
                tmp->nodetype = EADD;
                tmp->u.diadic.left = result;
                tmp->u.diadic.right = IRO_DuplicateExpr(scannd, list);
                IRO_AddToList(tmp, list);
                tmp->flags |= IROLF_LoopInvariant;
                tmp->flags |= IROLF_Reffed;
                tmp->rtype = result->rtype;
                result = tmp;
            } else {
                result = IRO_DuplicateExpr(scannd, list);
            }

            if (elist) {
                elist->next = scanlist->next;
            } else {
                FirstAddendLinear = scanlist->next;
            }
        } else {
            elist = scanlist;
        }
    }

    for (scanlist = FirstAddendLinear, elist = NULL; scanlist; scanlist = scanlist->next) {
        scannd = scanlist->element;
        if (!IRO_IsIntConstant(scannd)) {
            if (result) {
                IROLinear* tmp = IRO_NewLinear(IROLinearOp2Arg);
                tmp->index = ++IRO_NumLinear;
                tmp->nodetype = EADD;
                tmp->u.diadic.left = result;
                tmp->u.diadic.right = IRO_DuplicateExpr(scannd, list);
                IRO_AddToList(tmp, list);
                tmp->flags |= IROLF_Reffed;
                tmp->rtype = result->rtype;
                result = tmp;
            } else {
                result = IRO_DuplicateExpr(scannd, list);
            }

            if (elist) {
                elist->next = scanlist->next;
            } else {
                FirstAddendLinear = scanlist->next;
            }
        } else {
            elist = scanlist;
        }
    }

    for (scanlist = FirstAddendLinear; scanlist; scanlist = scanlist->next) {
        scannd = scanlist->element;
        if (result) {
            IROLinear* tmp = IRO_NewLinear(IROLinearOp2Arg);
            tmp->index = ++IRO_NumLinear;
            tmp->nodetype = EADD;
            tmp->u.diadic.left = result;
            tmp->u.diadic.right = scannd;
            tmp->u.diadic.right = IRO_DuplicateExpr(scannd, list);
            IRO_AddToList(tmp, list);
            tmp->flags |= IROLF_Reffed;
            tmp->rtype = result->rtype;
            result = tmp;
        } else {
            result = IRO_DuplicateExpr(scannd, list);
        }
    }

    return result;
}

static IROLinear* FindAddendForReductionPattern(IROLinear* a, IROLinear* b, Boolean* resultFlag) {
    IROLinear* left;
    IROLinear* right;
    IROLinear* node28;
    Boolean subflag;

    left = b->u.diadic.left;
    right = b->u.diadic.right;
    if (b->nodetype == EADD || b->nodetype == ESUB) {
        node28 = left;
        while (IS_LINEAR_MONADIC(node28, ETYPCON)) {
            node28 = node28->u.monadic;
        }

        if (IS_LINEAR_MONADIC(node28, EINDIRECT) &&
            (node28->u.monadic->flags & IROLF_LoopInvariant) &&
            IRO_ExprsSame(a, node28)) {
            *resultFlag = TRUE;
            return left;
        }

        if (IS_LINEAR_DIADIC_2(node28, EADD, ESUB)) {
            if ((node28 = FindAddendForReductionPattern(a, node28, &subflag))) {
                *resultFlag = TRUE;
                return node28;
            }
        }
    }

    *resultFlag = FALSE;
    if (b->nodetype == EADD) {
        node28 = right;
        while (IS_LINEAR_MONADIC(node28, ETYPCON)) {
            node28 = node28->u.monadic;
        }

        if (IS_LINEAR_MONADIC(node28, EINDIRECT) &&
            (node28->u.monadic->flags & IROLF_LoopInvariant) &&
            IRO_ExprsSame(a, node28)) {
            return right;
        }

        if (IS_LINEAR_DIADIC_2(node28, EADD, ESUB)) {
            if ((node28 = FindAddendForReductionPattern(a, node28, &subflag))) {
                return node28;
            }
        }
    }

    return NULL;
}

static void ReorderOperandsForReductionPattern(IROLinear* a, IROLinear* b) {
    IROLinear* left;
    IROLinear* right;
    IROLinear* addend;
    IROLinear* tmp;
    Boolean flag;

    left = b->u.diadic.left;
    right = b->u.diadic.right;

    if (b->nodetype ==
        EADD &&
        (addend = FindAddendForReductionPattern(a, b, &flag)) &&
        addend != left) {
        if (flag && IS_LINEAR_DIADIC_2(left, EADD, ESUB) && addend->rtype == right->rtype) {
            tmp = left;
            b->u.diadic.left = right;
            left = b->u.diadic.left;
            b->u.diadic.right = tmp;
            right = b->u.diadic.right;
            flag = FALSE;
        }

        if (!flag && IS_LINEAR_DIADIC_2(right, EADD, ESUB) && addend->rtype == left->rtype) {
#if VERSION < VERSION_GC_2_5
            if (IRO_LocateFather_Cut_And_Paste_Without_Nopping(addend, left)) {
                b->u.diadic.left = addend;
            }
#else
            // 2.5: the operands are moved as copies
            IROList list;
            IRO_InitList(&list);
            IRO_DuplicateExpr(left, &list);
            IRO_NopOut(left);
            IRO_PasteAfter(list.head, list.tail, addend);
            if (IRO_LocateFather_Cut_And_Paste_Without_Nopping(addend, list.tail)) {
                IRO_InitList(&list);
                IRO_DuplicateExpr(addend, &list);
                IRO_NopOut(addend);
                IRO_Paste(list.head, list.tail, b);
                b->u.diadic.left = list.tail;
            }
#endif
        }
    }
}

static UInt32 IsAssignmentReductionCandidate(IROLinear* nd) {
    IROLinear* left;
    IROLinear* right;
    IROLinear* tmp;

    if (nd->type == IROLinearOp2Arg) {
        if (nd->nodetype == EASS) {
            left = nd->u.diadic.left;
            right = nd->u.diadic.right;
            if (IS_LINEAR_MONADIC(left, EINDIRECT) &&
                (left->u.monadic->flags & IROLF_LoopInvariant)) {
                if (IS_LINEAR_MONADIC(right, ETYPCON) &&
                    right->rtype->type ==
                    right->u.monadic->rtype->type &&
                    IS_TYPE_INT(right->rtype) &&
                    right->rtype->size < right->u.monadic->rtype->size) {
                    right = right->u.monadic;
                }

                if (IS_LINEAR_DIADIC_2(right, EADD, ESUB)) {
                    ReorderOperandsForReductionPattern(left, right);
                    tmp = right->u.diadic.left;
                    if (IS_LINEAR_MONADIC(tmp, ETYPCON)) {
                        tmp = tmp->u.monadic;
                    }
                    if (IS_LINEAR_MONADIC(tmp, EINDIRECT) &&
                        (tmp->u.monadic->flags & IROLF_LoopInvariant) &&
                        IRO_ExprsSame(left, tmp) &&
                        right->u.diadic.right->type == IROLinearOp2Arg) {
                        if (right->u.diadic.right->nodetype == EADD) {
                            return TRUE;
                        }
                        if (right->u.diadic.right->nodetype == ESUB) {
                            return TRUE;
                        }
                        if (right->u.diadic.right->nodetype == EMUL) {
                            return TRUE;
                        }
                        if (right->u.diadic.right->nodetype == EDIV) {
                            return TRUE;
                        }
                    }
                }
            }
        } else if (nd->nodetype == EADDASS || nd->nodetype == ESUBASS) {
            left = nd->u.diadic.left;
            right = nd->u.diadic.right;

            if (IS_LINEAR_MONADIC(right, ETYPCON) &&
                right->rtype->type ==
                right->u.monadic->rtype->type &&
                IS_TYPE_INT(right->rtype) &&
                right->rtype->size < right->u.monadic->rtype->size) {
                right = right->u.monadic;
            }

            if (IS_LINEAR_MONADIC(left, EINDIRECT) &&
                (left->u.monadic->flags & IROLF_LoopInvariant) &&
                right->type == IROLinearOp2Arg) {
                if (right->nodetype == EADD) {
                    return TRUE;
                }
                if (right->nodetype == ESUB) {
                    return TRUE;
                }
                if (right->nodetype == EMUL) {
                    return TRUE;
                }
                if (right->nodetype == EDIV) {
                    return TRUE;
                }
            }
        }
    }

    return FALSE;
}

// TODO: 2.6 lays out this unit's statics at 2-byte packing (`iro_loop_search_found` 2 bytes,
// `NoSubableSubs` at offset 6), and MWCC lays out .bss with the packing in force at the end of the
// file: Pro7's `options align=mac68k` (IroLoop.h, IroCSE.h ...) was evidently left on, as in
// CABI, CError and CTemplateFunc.
#pragma pack(2)
