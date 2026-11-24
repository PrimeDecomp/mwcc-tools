#include "compiler/IrOptimizer.h"
#include "compiler/IroCSE.h"
#include "compiler/IroDump.h"
#include "compiler/IroEmptyLoop.h"
#include "compiler/IroEval.h"
#include "compiler/IroExprRegeneration.h"
#include "compiler/IroFlowgraph.h"
#include "compiler/IroJump.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroLoop.h"
#include "compiler/IroPointerAnalysis.h"
#include "compiler/IroPropagate.h"
#include "compiler/IroRangePropagation.h"
#include "compiler/IroSubable.h"
#include "compiler/IroTransform.h"
#include "compiler/IroUnrollLoop.h"
#include "compiler/IROUseDef.h"
#include "compiler/IroUtil.h"
#include "compiler/IroVars.h"
#include "compiler/CError.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/enode.h"
#include "compiler/InlineAsm.h"
#include "compiler/InlineAsmPPC.h"
#include "compiler/objects.h"

Boolean DoScalarize;
Boolean DoLinearize;
Boolean EarlyReturn;
Boolean IRO_CPFirstTime;
Boolean VectorPhaseCalledFromUnroll;
Boolean IRO_Log;
static Boolean stIsSetup;

static void CountRefToObject(Object* object, int depth) {
    static unsigned short LoopUsage[] = {1, 4, 16, 64};
    int index = depth;

    if (index > 3) {
        index = 3;
    }

    object->u.var.info->usage += LoopUsage[index];
    object->u.var.info->used = TRUE;
}

static void CountARef(IROLinear* node, int depth) {
    Object* object;

    object = node->u.node->data.objref;
    CError_ASSERT(78, object->datatype != DALIAS);

    if (object->datatype == DLOCAL && object->u.var.info) {
        CountRefToObject(object, depth);
        if ((node->flags & IROLF_Used) && (node->flags & IROLF_Assigned)) {
            CountRefToObject(object, depth);
        }

        if (!(node->flags & IROLF_Immind) && !object->u.var.info->noregister) {
            object->u.var.info->noregister = 2;
        }
    }
}

static void CountDoubleInd(IROLinear* node, int depth) {
    if (IRO_IsVariable(node)) {
        CountARef(node->u.monadic, depth);
    } else if (node->type == IROLinearOp2Arg) {
        if (node->nodetype == EADD) {
            CountDoubleInd(node->u.diadic.left, depth);
            CountDoubleInd(node->u.diadic.right, depth);
        } else if (IRO_IsAddressMultiply(node)) {
            if (IRO_IsVariable(node->u.diadic.left)) {
                CountARef(node->u.diadic.left->u.monadic, depth);
            }
        }
    }
}

#if VERSION == VERSION_WII_1_0RC1
static void CountScopeUsage(IROLinear* node, int depth) {
    const ENode* ref = node->u.node->data.scope.object;
    if (ref && ref->data.objref->datatype == DLOCAL && ref->data.objref->u.var.info) {
        CountRefToObject(ref->data.objref, depth);
    }
}
#endif

static void CountUsage(void) {
    IRONode* fnode = IRO_FirstNode;
    IROLinear* node;

    if (IRO_FirstNode) {
        for (; fnode; fnode = fnode->nextnode) {
            for (node = fnode->first; node; node = node->next) {
                if (IS_LINEAR_ENODE(node, EOBJREF)) {
                    CountARef(node, fnode->loopdepth);
#if VERSION == VERSION_WII_1_0RC1
                } else if (IS_LINEAR_ENODE(node, ESCOPE)) {
                    CountScopeUsage(node, fnode->loopdepth);
#endif
                } else if (IS_LINEAR_MONADIC(node, EINDIRECT)) {
                    CountDoubleInd(node->u.monadic, fnode->loopdepth);
                }

                if (node->type == IROLinearAsm) {
                    IAEffects effects;
                    int i;

                    CodeGen_GetAsmEffects(node->u.asm_stmt, &effects);
                    for (i = 0; i < effects.numoperands; i++) {
                        Object* object = effects.operands[i].object;
                        if (object->datatype == DLOCAL && object->u.var.info) {
                            CountRefToObject(object, fnode->loopdepth);
                            if (effects.operands[i].type == IAEffect_AddressOf &&
                                !object->u.var.info->noregister) {
                                object->u.var.info->noregister = 2;
                            }
                        }
                    }
                }

                if (node == fnode->last) {
                    break;
                }
            }
        }
    } else {
        for (node = IRO_FirstLinear; node; node = node->next) {
            if (IS_LINEAR_ENODE(node, EOBJREF)) {
                CountARef(node, 0);
#if VERSION == VERSION_WII_1_0RC1
            } else if (IS_LINEAR_ENODE(node, ESCOPE)) {
                CountScopeUsage(node, 0);
#endif
            } else if (IS_LINEAR_MONADIC(node, EINDIRECT)) {
                CountDoubleInd(node->u.monadic, 0);
            }
        }
    }

    IRO_CheckForUserBreak();
}

Statement* IRO_Optimizer(Object* func, Statement* statements) {
    Boolean changed;
    int pass;
    int passCount;

    CError_ASSERT(234, stIsSetup);

    DisableDueToAsm = FALSE;
    FunctionName = func;
    DoScalarize = TRUE;
    DoLinearize = TRUE;
    EarlyReturn = FALSE;
    IRO_Depends = NULL;
    LoopOptimizerRun = FALSE;
    IRO_IsLeafFunction = TRUE;
    IRO_FunctionHasReturn = FALSE;

    IRO_SetupForUserBreakChecking();

    IRO_Dump("Starting function %s\n", func ? func->name->name : "Init-code");
    IRO_Dump("--------------------------------------------------------------------------------\n");

    if (DoLinearize) {
        IRO_PreLinearize(statements);
    }
    if (copts.optimizationlevel > 0) {
        IRO_TransformTree(statements);
    }

    VectorPhaseCalledFromUnroll = FALSE;

    IRO_Linearize(statements);

    CurStat = NULL;

    IRO_FirstExpr = NULL;
    IRO_LastExpr = NULL;
    IRO_FirstAssign = NULL;
    IRO_LastAssign = NULL;
    IRO_FirstVarUse = NULL;
    IRO_LastVarUse = NULL;
    IRO_FirstNode = NULL;
    IRO_LastNode = NULL;

    if (copts.optimizationlevel > 0) {
        IRO_DoTransformations();
    }

    IRO_BuildFlowgraph(IRO_FirstLinear);
    IRO_DumpAfterPhase("IRO_BuildflowGraph", FALSE);

    IRO_FindAllVars();
    if (copts.warn_uninitializedvar) {
        IRO_CheckInit();
    }

    if (!DisableDueToAsm && copts.optimizationlevel > 0 && copts.opt_pointer_analysis && func) {
        IRO_AnalyzePointers(func);
        if (copts.propagation && IRO_EvaluateDefinitePointers(func)) {
            IRO_UpdateFlagsOnInts();
            IRO_UpdateVars();
            IRO_DumpAfterPhase("IRO_EvaluateDefinitePointers", FALSE);
        }
    }

    if (copts.optimizationlevel > 0) {
        changed = IRO_EvaluateConditionals();

        if (!DisableDueToAsm) {
            changed |= IRO_RemoveUnreachable();
            IRO_DumpAfterPhase("IRO_RemoveUnreachable", FALSE);
        }

        changed |= IRO_RemoveRedundantJumps();
        IRO_DumpAfterPhase("IRO_RemoveRedundantJumps", FALSE);

#if VERSION == VERSION_WII_1_0RC1
        if (copts.opt_optimizecontrolflow) {
            changed |= IRO_DoJumpChaining();
            IRO_DumpAfterPhase("IRO_DoJumpChaining", FALSE);
        }
#endif
        if (!DisableDueToAsm) {
            changed |= IRO_RemoveLabels();
            IRO_DumpAfterPhase("IRO_RemoveLabels()", FALSE);
        }

        if (changed) {
            IRO_BuildFlowgraph(IRO_FirstLinear);
            IRO_DumpAfterPhase("IRO_BuildflowGraph--1", FALSE);
        }
    }

    if (!DisableDueToAsm &&
        (copts.optimizationlevel > 0
#if VERSION == VERSION_WII_1_0RC1
         || copts.removeemptyloops || copts.opt_optimizecontrolflow
         || copts.opt_optimizenonregaccess || copts.opt_marknonregtemps
#endif
        )) {
        passCount = copts.multiplepasses ? 2 : 1;
        IRO_CPFirstTime = TRUE;
        for (pass = 0; pass < passCount; pass++) {
            IRO_Dump("*****************\n");
            IRO_Dump("Dumps for pass=%d\n", pass);
            IRO_Dump("*****************\n");

            if (DoScalarize) {
                IRO_ScalarizeClassDataMembers();
            }
            IRO_DumpAfterPhase("IRO_ScalarizeClassDataMembers", FALSE);

#if VERSION == VERSION_WII_1_0RC1
            if (copts.opt_optimizecontrolflow) {
                if (IRO_SimplifyConditionals()) {
                    IRO_DumpAfterPhase("IRO_SimplifyConditionals", FALSE);
                }
                if (IRO_SimplifyBoundedIntervalConditionals()) {
                    IRO_DumpAfterPhase("IRO_SimplifyBoundedIntervalConditionals", FALSE);
                }
            }
#endif

            if (copts.propagation) {
                IRO_CopyAndConstantPropagation();
                IRO_CPFirstTime = FALSE;
                IRO_ExpressionPropagation();
                IRO_DumpAfterPhase("Copy and constant propagation", FALSE);

                IRO_RangePropagateInFNode();
                IRO_DumpAfterPhase("IRO_RangePropagateInFNode", FALSE);
                IRO_UpdateFlagsOnInts();
            }

            IRO_DumpAfterPhase("IRO_ExpressionPropagation", FALSE);

            if (copts.deadstore || copts.propagation) {
                IRO_UseDef(copts.deadstore, copts.propagation);
            }
            IRO_DumpAfterPhase("after IRO_UseDef", FALSE);

            IRO_UpdateVars();
            IRO_ConstantFolding();
            IRO_DumpAfterPhase("IRO_ConstantFolding", FALSE);

            IRO_EvaluateConditionals();
            IRO_RemoveUnreachable();

#if VERSION == VERSION_WII_1_0RC1
            if (copts.opt_optimizecontrolflow) {
                IRO_SimplifyConditionals();
                if (IRO_OptimizeSwitches()) {
                    IRO_DumpAfterPhase("IRO_OptimizeSwitches", FALSE);
                }
                if (IRO_SimplifyBoundedIntervalConditionals()) {
                    IRO_DumpAfterPhase("IRO_SimplifyBoundedIntervalConditionals", FALSE);
                }
            }
            if (pass == 0 && copts.loopinvariants) {
                IRO_PullOutInvariantAssignments();
            }
            while (copts.removeemptyloops && IRO_RemoveAllEmptyLoops(pass == 0)) {
                IRO_DumpAfterPhase("IRO_RemoveAllEmptyLoops", FALSE);
                if (pass != 0 || !copts.loopinvariants || !IRO_PullOutInvariantAssignments()) {
                    break;
                }
            }
#else
            IRO_SimplifyConditionals();
            if (pass == 1 && copts.optimizationlevel > 2) {
                IRO_RenumberInts();
                IRO_DumpAfterPhase("Before IRO_FindEmptyLoops", FALSE);
                IRO_FindEmptyLoops();
                IRO_DumpAfterPhase("After IRO_FindEmptyLoops", FALSE);
                IRO_RenumberInts();
            }
#endif

            if (copts.unrolling && !copts.optimizesize && pass == 0) {
                IRO_DumpAfterPhase("Before IRO_LoopUnroller", FALSE);
                IRO_LoopUnroller();
                IRO_DumpAfterPhase("After IRO_LoopUnroller", FALSE);
                IRO_RenumberInts();
            }

            VectorPhaseCalledFromUnroll = FALSE;

            if (pass == 0 &&
                (copts.loopinvariants || copts.strengthreduction
#if VERSION == VERSION_WII_1_0RC1
                 || copts.opt_optimizenonregaccess || copts.opt_marknonregtemps
#endif
                )) {
                IRO_DumpAfterPhase("Before IRO_FindLoops", FALSE);
                IRO_FindLoops();
                LoopOptimizerRun = TRUE;
                IRO_SetLoopDepth();
            }
            IRO_DumpAfterPhase("After IRO_FindLoops", FALSE);

            if (copts.propagation) {
                IRO_CopyAndConstantPropagation();
                IRO_ConstantFolding();
                IRO_EvaluateConditionals();
            }

            IRO_DumpAfterPhase(
                "Second pass:IRO_CopyAndConstantPropagation, IRO_ConstantFolding, IRO_EvaluateConditionals",
                FALSE
            );

#if VERSION != VERSION_WII_1_0RC1
            if (copts.commonsubs) {
                IRO_FindExpressions(NULL, FALSE);
            }
#endif

            if (copts.commonsubs) {
#if VERSION != VERSION_WII_1_0RC1
                IRO_ComputeAvail();
#endif
                IRO_CommonSubs();
            }
            IRO_DumpAfterPhase("IRO_CommonSubs", FALSE);

            IRO_UpdateFlagsOnInts();
            IRO_UpdateVars();
            IRO_DoTransformations();
            IRO_ConstantFolding();

            do {
                IRO_UpdateFlagsOnInts();
                changed = FALSE;

                if (copts.deadcode) {
                    IRO_RemoveUnreachable();
                }
                IRO_DumpAfterPhase("IRO_RemoveUnreachable", FALSE);

                changed |= IRO_RemoveRedundantJumps();
                IRO_DumpAfterPhase("IRO_RemoveRedundantJumps", FALSE);

                changed |= IRO_RemoveLabels();
                IRO_DumpAfterPhase("IRO_RemoveLabels", FALSE);

                changed |= IRO_DoJumpChaining();
                IRO_DumpAfterPhase("IRO_DoJumpChaining", FALSE);

                if (copts.propagation) {
                    IRO_RenumberInts();
                    IRO_DumpAfterPhase("Before IRO_CopyAndConstantPropagation", FALSE);
                    changed |= IRO_CopyAndConstantPropagation();
                    IRO_DumpAfterPhase("After IRO_CopyAndConstantPropagation", FALSE);
                    IRO_ConstantFolding();
                }

                if (copts.deadstore || copts.propagation) {
                    changed |= IRO_UseDef(copts.deadstore, copts.propagation);
                }
                IRO_DumpAfterPhase("IRO_UseDef", FALSE);

                changed |= IRO_EvaluateConditionals();
                IRO_DumpAfterPhase("IRO_EvaluateConditionals", FALSE);
#if VERSION == VERSION_WII_1_0RC1
                if (!changed && copts.removeemptyloops && IRO_RemoveAllEmptyLoops(FALSE)) {
                    IRO_DumpAfterPhase("IRO_RemoveAllEmptyLoops", FALSE);
                    changed = TRUE;
                }
#endif
            } while (changed);
        }

        if (copts.lifetimes) {
            IRO_UseDef(FALSE, FALSE);
            IRO_SplitLifetimes();
        }

#if VERSION != VERSION_WII_1_0RC1
        IRO_DoTransformations();
#endif
        IRO_DumpAfterPhase("Before RebuildCondExpressions", FALSE);
    }

    IRO_RenumberInts();
    IRO_DumpAfterPhase("before IRO_RewriteBitFieldTemps", FALSE);
    IRO_RewriteBitFieldTemps();
    IRO_DumpAfterPhase("After IRO_RewriteBitFieldTemps", FALSE);

    if (!DisableDueToAsm) {
        IRO_RegenerateExpressions();
        IRO_DumpAfterPhase("IRO_RegenerateExpressions", FALSE);
#if VERSION == VERSION_WII_1_0RC1
        if (copts.optimizationlevel > 0) {
            IRO_DoTransformations();
            IRO_DumpAfterPhase("IRO_DoTransformations", FALSE);
        }
#endif
    }

    CountUsage();

    IRO_DumpAfterPhase("After IRO_Optimizer", FALSE);

#if VERSION == VERSION_WII_1_0RC1
    IRO_SetLoopDepth();
#endif
    statements = IRO_Delinearize(IRO_FirstNode, NULL);

    IRO_ZapVarPtrs();
    ResetVars();
    freeoheap();
    return statements;
}

void IRO_Setup(void) {
    static Boolean ENodeArraysHaveBeenInitialized;

    if (!stIsSetup) {
        IRO_Log = FALSE;
        IRO_SetupDump();
        if (!ENodeArraysHaveBeenInitialized) {
            IRO_InitializeNodeNamesArray();
            IRO_InitializeIsAssociativeENodeTypeArray();
            IRO_InitializeIsSubableOpArray();
            IRO_InitializeAssignmentOpArray();
            IRO_InitializeComplementaryOpArray();
            IRO_InitializeComplementaryOpLogicalArray();
            IRO_InitializeNonAssignmentOpArray();
            IRO_InitializeAssignmentFoldingFunctionArray();
            IRO_InitializeIRO_IsModifyOpArray();
            IRO_InitializeIRO_IsAssignOpArray();
            ENodeArraysHaveBeenInitialized = TRUE;
        }
        stIsSetup = TRUE;
    }
}

void IRO_Cleanup(void) {
    if (stIsSetup) {
        IRO_CleanupDump();
        stIsSetup = FALSE;
    }
}

void CodeGen_UpdateOptimizerOptions(void) {
    copts.deadcode = copts.optimizationlevel >= 1;
    copts.commonsubs = copts.propagation = copts.optimizationlevel >= 2;
#if VERSION == VERSION_WII_1_0RC1
    copts.deadstore = copts.optimizationlevel >= 2;
#else
    copts.deadstore = copts.optimizationlevel >= 3;
#endif
    copts.loopinvariants = copts.strengthreduction = copts.lifetimes =
        copts.unrolling = copts.vectorizeloops = copts.optimizationlevel >= 3;
    copts.multiplepasses = copts.optimizationlevel >= 4;
#if VERSION == VERSION_WII_1_0RC1
    copts.removeemptyloops = copts.optimizationlevel >= 3;
    copts.opt_optimizenonregaccess = copts.removeemptyloops;
    copts.opt_marknonregtemps = FALSE;
    copts.opt_optimizecontrolflow = copts.optimizationlevel >= 1;
#endif
}
