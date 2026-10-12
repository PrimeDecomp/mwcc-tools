#define CERROR_FILE "IroPointerAnalysis.c"
#include "compiler/common.h"
#include "driver/CLIO.h"
#if VERSION >= VERSION_GC_1_3
#define IRO_POINTER_ANALYSIS_TU
#include "IroPointerAnalysisADTs.c"
#endif

#if VERSION >= VERSION_GC_1_3
#include "compiler/CError.h"
#include "compiler/CodeGen.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/CompilerTools.h"
#include "compiler/Registers.h"
#include "compiler/IrOptimizer.h"
#include "compiler/IroUtil.h"
#include "compiler/IroVars.h"
#include "compiler/CExpr2.h"
#include "compiler/enode.h"
#include "compiler/CABI.h"
#include "compiler/CParser.h"
#include <setjmp.h>

#undef IRO_ASSERT
#undef IRO_ASSERT_T
#define IRO_ASSERT(line, expr)                                                                                         \
    if (!(expr))                                                                                                       \
    CLIO_ReportAssertionFailure(#expr, "IroPointerAnalysis.c", line)
#define IRO_ASSERT_T(line, expr)                                                                                       \
    do {                                                                                                               \
        int ok_ = (expr);                                                                                              \
        if (!ok_)                                                                                                      \
            CLIO_ReportAssertionFailure(#expr, "IroPointerAnalysis.c", line);                                          \
    } while (0)

/* an assertion whose text in the original differs from our spelling of the expression */
#define IRO_ASSERT_MSG(line, expr, text)                                                                               \
    if (!(expr))                                                                                                       \
    CLIO_ReportAssertionFailure(text, "IroPointerAnalysis.c", line)

#define GetLocalObject fn_0049bf00
#define LocationIsVolatile fn_0049c040
#define EvalExprAction2 fn_0049b010
#define EvalExprAction3 fn_0049afa0
#define ExpandLocationSetSetToActualsAction fn_00492700
#define AddToInitialPointsToFunc fn_00491870
#define IsAddressableLocation fn_0048e5c0
#define FillInAppropriateMappingsWithExtParam fn_00497200
#define MatchPTFAction1 fn_004965a0
#define MatchPTFAction2 fn_00496460
#define FindCallTargetsAction2 fn_004952f0
#define FindCallTargetsAction fn_00495120
#define FindCallTargets fn_00494e90
#define CreateExtendedParamAction fn_0048fa90
#define RecordActuals fn_0048e940
#define FindAliasingAction2 fn_0048e0c0
#define KillAllAddressableLocationsAction fn_0048dd90
#define KillLocationsAction fn_0048cf90
#define FindAliasingAction fn_0048dfe0
#define AllocatePTF fn_0048a910
#define EvalCall fn_00489b30
#define EvalReturn fn_00486a80
#define ApplySummaryAction fn_00486060
#define GetPTFAction2 fn_00485a30
#define GetPTFAction fn_004857d0
#define FunctionArguments fn_0048a680
#define AddLocalVarsAddressedByExceptionUses fn_00481b10
#define CleanseLocationSet fn_0047fc60
#define CleansePointsToEntry fn_0047f940
#define PointerAnalysis_TermAction4 fn_0047f800
#define PointerAnalysis_TermAction2 fn_0047f480
#define PointerAnalysis_TermAction1 fn_0047eae0
#define InvalidatePointsToFunctions fn_0047d900
#define PointerAnalysis_HeapErrorProc fn_0047d8f0
#define IRO_AnalyzePointers fn_0047d720
#define FindGlobalObjectAction fn_0047d3c0
#define GetDefiniteObjectOfExtendedParamLoc fn_0047cb70
#define CreateExpressionForLocationSet fn_0047c6e0
#define LookupLinearExprAction fn_0047c5c0
#define PointerAnalysis_LookupLinearNodePointerExpr fn_0047c270
#define GetFunctionDepsOrKillsAction fn_0047bdd0
#define GetFunctionDepsOrKillsAction2 fn_0047ba80
#define CreateFalseContext fn_0047b370
#define DestroyFalseContext fn_0047a8f0
#define PointerAnalysis_GetFunctionKills fn_0047a6e0
#define PointerAnalysis_GetFunctionDependencies fn_0047a490
#define PointerAnalysis_PragmaMode fn_0047a3a0

#define PointerAnalysis_Setup fn_0047d8c0
#define PointerAnalysis_Cleanup fn_0047d880

/* the analysis' state (statics of the original, at their addresses) */
extern IRONode *data_005dc1fc;
#define stExceptionFNode data_005dc1fc
extern PTFList *data_005dc21e;
#define stPTFList data_005dc21e
extern ExtendedParamSet *data_005dc222;
#define stExtParamSet data_005dc222
extern Object *g_ipaFunction;
#define stCurrentProc g_ipaFunction
extern Object g_ipaUnknownObj;
#define stUnknown g_ipaUnknownObj
extern jmp_buf g_ipaJmpBuf;
#define stAbortPointerAnalysis g_ipaJmpBuf
extern Stack *data_005dc29a;
#define stCallingContextStack data_005dc29a
extern PartialTransferFunction *g_ipaPtf;
#define stUnknownPTF g_ipaPtf
extern ObjectList *data_005dc296;
#define stParamObjs data_005dc296
extern UInt32 data_005dc21a;
#define stMaxPassCount data_005dc21a
extern void fn_00529710(void);
#define IRO_InitializeAllocator fn_00529710
extern void fn_00529700(void);
#define IRO_TerminateAllocator fn_00529700

extern ObjectList *arguments;

/* analysis functions still taken from the original */
extern ObjectList *FunctionArguments(Object *proc);
#define EvalExpr fn_00497f50
extern Boolean EvalExpr(LocationSetSet *set, Object *proc, IROLinear *Int, Stack **stackPtr, ParamMappingFunction *map,
                        PartialTransferFunction *ptf);
#define ExpandLocationSetSetToActuals fn_004920a0
extern Boolean ExpandLocationSetSetToActuals(Stack **stackPtr, ParamMappingFunction *map, LocationSetSet *thingsPointedTo);
#define EvalCallAction fn_00489de0
extern void EvalCallAction(Object *proc, void *refcon);
#define PointerAnalysis_Init fn_0047fdb0
#define PointerAnalysis_Term fn_0047dda0
extern Object *data_005875b8;
#define FunctionName data_005875b8
extern void RecordActuals(IROLinear *nd, Object *proc, ParamMappingFunction *map);
#define GetPTF fn_00485000
extern PartialTransferFunction *GetPTF(ParamMappingFunction *map, Object *proc, IROLinear *nd, PartialTransferFunction *ptf,
                                       Boolean *needVisit);
#define GetActualLocsOfExtendedParam fn_00492ae0
extern Boolean GetActualLocsOfExtendedParam(LocationSetSet *set, LocationSet *ls, Object *var, Stack **stackPtr,
                                            ParamMappingFunction *map, Boolean flag);
#define LocationSetRepresentsSingleLocation fn_0049b7c0
extern Boolean LocationSetRepresentsSingleLocation(LocationSet *ls, Object *proc, PointsToFunction *pointsToFunc);
extern Boolean IsAddressableLocation(LocationSet *loc, Object *proc, IRONode *fnode, IROLinear *nd);
#define EvalAssignAction fn_004886b0
extern void EvalAssignAction(LocationSet *dst, void *refcon);
#define ApplySummaryAction2 fn_00486300
extern void ApplySummaryAction2(ParamMapping *mapping, void *refcon);
#define FillInAppropriateMappingsWithExtParamAction fn_00497440
extern void FillInAppropriateMappingsWithExtParamAction(Object *obj, void *refcon);
#define MatchPTF fn_004954c0
extern Boolean MatchPTF(PartialTransferFunction *tgtPTF, Object *proc, ParamMappingFunction *map, IROLinear *nd,
                        PartialTransferFunction *ptf);
#define Assign fn_0048b6d0
extern Boolean Assign(PartialTransferFunction *ptf, LocationSet *dst, LocationSetSet *srcs, Object *proc, IROLinear *nd,
                      IRONode *fnode);
#define Lookup fn_0048fc70
extern Boolean Lookup(LocationSetSet *set, Stack **stackPtr, Object *proc, ParamMappingFunction *map, PartialTransferFunction *ptf,
                      LocationSet *dst, PointsToFunction *pointsToFunc, Boolean unk, Type *indRtype);
#define MatchPTFHelper fn_00496700
extern void MatchPTFHelper(LocationSet *loc, LocationSetSet *locs, Object *proc, ParamMappingFunction *map,
                           PartialTransferFunction *ptf);
#define LocationSetsAlias fn_0048e0f0
extern Boolean LocationSetsAlias(LocationSet *ls1, LocationSet *ls2);

inline Boolean Inline_IsObjectData(Object *obj)
{
    return obj->datatype == DDATA;
}

Object *GetLocalObject(PALocalVar *local, Object *proc, Boolean flag)
{
    ObjectList *list;
    Object *arg;
    Object *obj;
    char *name;

    IRO_ASSERT(999, local != NULL);
    IRO_ASSERT(1000, proc != NULL);

    obj = PALocalVar_obj(local);
    name = PALocalVar_name(local);

    if (proc != &stUnknown && !obj && name && name[0] && flag) {
        for (list = FunctionArguments(proc); list && !obj; list = list->next) {
            arg = list->object;
            if (arg && arg != &stUnknown && arg->name && arg->name->name) {
                if (!strcmp(arg->name->name, name))
                    obj = arg;
            }
        }
    }

    if (obj)
        PALocalVar_SetObject(local, obj);

    return obj;
}

inline Boolean ObjectIsAnExtendedParamCandidate(Object *obj)
{
    IRO_ASSERT(1042, obj != NULL);

    return Inline_IsObjectData(obj) || obj->datatype == DABSOLUTE || obj->datatype == DFUNC || obj->datatype == DVFUNC ||
           obj->datatype == DINLINEFUNC;
}

inline Boolean ObjectIsAFunction(Object *obj)
{
    IRO_ASSERT(1054, obj != NULL);

    return obj->datatype == DFUNC || obj->datatype == DVFUNC || obj->datatype == DINLINEFUNC;
}

inline void FindGlobalObjectAction(Object *object, void *refcon)
{
    if (ObjectIsAnExtendedParamCandidate(object)) {
        Object **ptr = refcon;
        *ptr = object;
    }
}

Boolean LocationIsVolatile(LocationSet *loc, Object *proc)
{
    Boolean result;
    Type *rtype;

    IRO_ASSERT(932, loc != NULL);
    IRO_ASSERT(933, proc != NULL);

    result = 0;

    rtype = LocationSet_rtype(loc);
    if (rtype) {
        UInt32 qual;
        switch ((char) rtype->type) {
        case TYPEARRAY:
            qual = TYPE_POINTER(rtype)->qual;
            break;
        case TYPEPOINTER:
            qual = TYPE_POINTER(rtype)->qual;
            break;
        case TYPEMEMBERPOINTER:
            qual = TYPE_MEMBER_POINTER(rtype)->qual;
            break;
        default:
            qual = 0;
        }
        if (qual & Q_VOLATILE)
            result = 1;
    }

    if (!result && !LocationSet_IsUnknown(loc)) {
        Object *obj = NULL;
        PAMemoryBlock *block = LocationSet_block(loc);
        PAMemoryBlockKind kind;

        switch ((kind = PAMemoryBlock_kind(block))) {
        case PAMEMORYBLOCKKIND_LOCALVAR: {
            PALocalVar *local = PAMemoryBlock_thing(block);
            if (local)
                obj = GetLocalObject(local, proc, 0);
            break;
        }
        case PAMEMORYBLOCKKIND_EXTENDEDPARAM: {
            ExtendedParam *ep = PAMemoryBlock_thing(block);
            if (ep) {
                ObjectSet *objSet;
                if ((objSet = ExtendedParam_objectSet(ep))) {
                    if (ObjectSet_Count(objSet) == 1)
                        obj = ObjectSet_FindFirst(objSet);
                    else
                        ObjectSet_ForEach(objSet, FindGlobalObjectAction, &obj);
                }
            }
            break;
        }
        }

        if (obj && is_volatile_object(obj))
            result = 1;
    }

    return result;
}

#pragma options align = mac68k
typedef struct EvalExprAction3Params {
    LocationSetSet *set;
    Stack **stackPtr;
    Object *proc;
    ParamMappingFunction *map;
    PartialTransferFunction *ptf;
    PointsToFunction *pointsToFunc;
    Type *indRtype;
    Boolean x1C;
} EvalExprAction3Params;
#pragma options align = reset

void EvalExprAction2(LocationSet *ls, void *refcon)
{
    UInt32 value;

    IRO_ASSERT(1188, ls != NULL);
    IRO_ASSERT_T(1189, !LocationSet_IsUnknown(ls));
    IRO_ASSERT(1190, refcon != NULL);

    value = CInt64_GetULong((CInt64 *) refcon);
    if (value) {
        LocationSet_SetField(ls, CInt64_Mod(LocationSet_field(ls), *((CInt64 *) refcon)));
    }
    LocationSet_SetStride(ls, value);
}

void EvalExprAction3(LocationSet *ls, void *refcon)
{
    EvalExprAction3Params *params;

    IRO_ASSERT(1219, ls != NULL);
    IRO_ASSERT(1220, refcon != NULL);

    params = refcon;

    params->x1C |= Lookup(params->set, params->stackPtr, params->proc, params->map, params->ptf, ls, params->pointsToFunc, 1,
                          params->indRtype);
}

#pragma options align = mac68k
typedef struct MatchPTFActionParams {
    Object *proc;
    PartialTransferFunction *ptfCopy;
    ParamMappingFunction *mapCopy;
    Stack **stackPtr;
} MatchPTFActionParams;
#pragma options align = reset

Boolean AddToInitialPointsToFunc(Boolean flag, PartialTransferFunction *ptf, LocationSet *dst, LocationSetSet *set)
{
    Boolean result;
    LocationSet *ls;
    PointsToFunction *initial;
    PointsToEntry *pte;

    IRO_ASSERT_T(3192, ptf == NULL || ptf != NULL);
    IRO_ASSERT(3193, dst != NULL);
    IRO_ASSERT(3194, set != NULL);

    result = 0;

    if (flag && ptf) {
        if (!(ls = LocationSetSet_FindUnknown(set)) || LocationSet_rtype(ls)) {
            if ((initial = PartialTransferFunction_initialPointsToFn(ptf)) && !PointsToFunction_FindByLookupCompatibleLocationSet(initial, dst)) {
                IRO_ASSERT(3208, dst != NULL);

                pte = PointsToEntry_New();
                PointsToEntry_Init(pte, dst, set);
                result = PointsToFunction_AddWithoutChecking(initial, pte);
                PointsToEntry_Term(pte);
                PointsToEntry_Delete(pte);
            }
        }
    }

    return result;
}

#pragma options align = mac68k
typedef struct ExpandLocationSetSetActionParams {
    LocationSetSet *toBeRemoved;
    LocationSetSet *toBeAdded;
    Stack **stackPtr;
    ParamMappingFunction *map;
    Boolean x10;
} ExpandLocationSetSetActionParams;
#pragma options align = reset

void ExpandLocationSetSetToActualsAction(LocationSet *ls, void *refcon)
{
    ExpandLocationSetSetActionParams *params;

    IRO_ASSERT(3021, ls != NULL);
    IRO_ASSERT(3022, refcon != NULL);

    params = refcon;

    IRO_ASSERT(3026, params->toBeRemoved != NULL);
    IRO_ASSERT(3027, params->toBeAdded != NULL);
    IRO_ASSERT_T(3028, params->stackPtr == NULL || *params->stackPtr == NULL || *params->stackPtr != NULL);
    IRO_ASSERT_T(3029, params->map == NULL || params->map != NULL);

    if (!LocationSet_IsUnknown(ls)) {
        PAMemoryBlock *block;
        PAMemoryBlockKind kind;
        if ((block = LocationSet_block(ls)) && (kind = PAMemoryBlock_kind(block)) == PAMEMORYBLOCKKIND_EXTENDEDPARAM) {
            ExtendedParam *ep = PAMemoryBlock_thing(block);
            if (ep) {
                ObjectSet *objSet = ExtendedParam_objectSet(ep);
                Object *obj = NULL;
                ObjectSet_ForEach(objSet, FindGlobalObjectAction, &obj);
                if (!obj) {
                    LocationSetSet_Add(params->toBeRemoved, ls);
                    params->x10 |=
                        GetActualLocsOfExtendedParam(params->toBeAdded, ls, NULL, params->stackPtr, params->map, 0);
                }
            }
        }
    }
}

inline Boolean ObjectIsAFunctionArgument(Object *proc, Object *obj)
{
    ObjectList *list;

    IRO_ASSERT(5643, proc != NULL);
    IRO_ASSERT(5644, proc != &stUnknown);
    IRO_ASSERT(5645, obj != NULL);

    if (obj->datatype == DLOCAL) {
        for (list = FunctionArguments(proc); list; list = list->next) {
            if (obj == list->object)
                return 1;
        }
    }

    return 0;
}

inline Boolean ObjectIsRestrictQualified(Object *obj)
{
    return (CParser_GetTypeQualifiers(obj->type, obj->qual) & Q_RESTRICT) != 0;
}

Boolean IsAddressableLocation(LocationSet *loc, Object *proc, IRONode *fnode, IROLinear *nd)
{
    IRO_ASSERT(3676, loc != NULL);
    IRO_ASSERT(3677, fnode != NULL);
    IRO_ASSERT(3678, nd != NULL);

    if (!LocationSet_IsUnknown(loc)) {
        PAMemoryBlock *block;
        PALocalVar *local;
        Object *obj;
        ExtendedParam *ep;
        ObjectSet *objSet;
        PAMemoryBlockKind kind;

        block = LocationSet_block(loc);
        if ((kind = PAMemoryBlock_kind(block)) == PAMEMORYBLOCKKIND_LOCALVAR && (local = PAMemoryBlock_thing(block)) &&
            (obj = GetLocalObject(local, proc, 0)) && fnode->addressed && !ObjectSet_Find(fnode->addressed, obj)) {
            return 0;
        }

        if ((kind = PAMemoryBlock_kind(block)) == PAMEMORYBLOCKKIND_EXTENDEDPARAM && (ep = PAMemoryBlock_thing(block)) &&
            (objSet = ExtendedParam_objectSet(ep)) && ObjectSet_Count(objSet) == 1 &&
            (obj = ObjectSet_FindFirst(objSet)) && ObjectIsAFunctionArgument(proc, obj) &&
            ObjectIsRestrictQualified(obj)) {
            return 0;
        }
    }

    return 1;
}

#pragma options align = mac68k
typedef struct EPParams {
    ParamMappingFunction *map;
    ExtendedParam *ep;
    Object *proc;
    Object *var;
    unsigned char x10;
    unsigned char x11;
} EPParams;
#pragma options align = reset

Boolean FillInAppropriateMappingsWithExtParam(ParamMappingFunction *map, Object *var, ExtendedParam *ep, Object *proc)
{
    EPParams params;
    ObjectSet *objSet;

    IRO_ASSERT(2398, map != NULL);
    IRO_ASSERT_T(2399, var == NULL || var != NULL);
    IRO_ASSERT(2400, var != &stUnknown);
    IRO_ASSERT(2401, ep != NULL);
    IRO_ASSERT_T(2402, proc == NULL || proc != NULL);
    IRO_ASSERT(2403, proc != &stUnknown);

    memset(&params, 0, sizeof(params));
    params.map = map;
    params.ep = ep;
    params.proc = proc;
    params.var = var;
    params.x10 = 0;
    params.x11 = 0;

    if ((objSet = ExtendedParam_objectSet(ep)))
        ObjectSet_ForEach(objSet, FillInAppropriateMappingsWithExtParamAction, &params);

    if (var && !params.x10) {
        ExtendedParam_AddObject(ep, var);
        FillInAppropriateMappingsWithExtParamAction(var, &params);
    }

    return params.x11;
}

void MatchPTFAction1(PointsToEntry *tgtPTE, void *refcon)
{
    MatchPTFActionParams *params;
    LocationSet *loc;
    LocationSetSet *locs;
    PAMemoryBlock *block;
    PAMemoryBlockKind kind;

    IRO_ASSERT(2525, tgtPTE != NULL);
    IRO_ASSERT(2526, refcon != NULL);

    params = refcon;

    IRO_ASSERT(2530, params->proc != NULL);
    IRO_ASSERT(2531, params->ptfCopy != NULL);
    IRO_ASSERT(2532, params->mapCopy != NULL);

    if ((loc = PointsToEntry_loc(tgtPTE)) && (locs = PointsToEntry_locs(tgtPTE))) {
        if ((block = LocationSet_block(loc))) {
            kind = PAMemoryBlock_kind(block);
            if (kind == PAMEMORYBLOCKKIND_LOCALVAR) {
                MatchPTFHelper(loc, locs, params->proc, params->mapCopy, params->ptfCopy);
            } else if (kind != PAMEMORYBLOCKKIND_EXTENDEDPARAM) {
                CError_FATAL(2547);
            }
        }
    }
}

void MatchPTFAction2(PointsToEntry *tgtPTE, void *refcon)
{
    MatchPTFActionParams *params;
    LocationSet *loc;
    LocationSetSet *locs;
    PAMemoryBlock *block;
    PAMemoryBlockKind kind;

    IRO_ASSERT(2561, tgtPTE != NULL);
    IRO_ASSERT(2562, refcon != NULL);

    params = refcon;

    IRO_ASSERT(2566, params->proc != NULL);
    IRO_ASSERT(2567, params->ptfCopy != NULL);
    IRO_ASSERT(2568, params->mapCopy != NULL);

    if ((loc = PointsToEntry_loc(tgtPTE)) && (locs = PointsToEntry_locs(tgtPTE))) {
        if ((block = LocationSet_block(loc))) {
            kind = PAMemoryBlock_kind(block);
            if (kind == PAMEMORYBLOCKKIND_EXTENDEDPARAM) {
                MatchPTFHelper(loc, locs, params->proc, params->mapCopy, params->ptfCopy);
            }
        }
    }
}

void FindCallTargetsAction2(Object *obj, void *refcon)
{
    ObjectSet *procList;

    IRO_ASSERT(2650, obj != NULL);
    IRO_ASSERT(2651, refcon != NULL);

    procList = refcon;

    if (obj == &stUnknown || ObjectIsAFunction(obj))
        ObjectSet_AddUnique(procList, obj);
}

void FindCallTargetsAction(LocationSet *ls, void *refcon)
{
    ObjectSet *procList;

    IRO_ASSERT(2669, ls != NULL);
    IRO_ASSERT(2670, refcon != NULL);

    procList = refcon;

    if (!LocationSet_IsUnknown(ls)) {
        PAMemoryBlock *mb = LocationSet_block(ls);
        PAMemoryBlockKind kind;
        if ((kind = PAMemoryBlock_kind(mb)) == PAMEMORYBLOCKKIND_EXTENDEDPARAM) {
            ExtendedParam *ep = PAMemoryBlock_thing(mb);
            ObjectSet *objSet = ExtendedParam_objectSet(ep);
            ObjectSet_ForEach(objSet, FindCallTargetsAction2, procList);
        }
    } else {
        FindCallTargetsAction2(&stUnknown, procList);
    }
}

Boolean FindCallTargets(ObjectSet *procList, Object *proc, IROLinear *nd, ParamMappingFunction *map, PartialTransferFunction *ptf)
{
    LocationSetSet *set;
    Boolean result;

    IRO_ASSERT(2696, procList != NULL);
    IRO_ASSERT(2697, proc != NULL);
    IRO_ASSERT(2698, nd != NULL);
    IRO_ASSERT_MSG(2699, nd->type == IROLinearFunccall, "nd->lt == Lt_Funccall");
    IRO_ASSERT(2700, map != NULL);
    IRO_ASSERT(2701, ptf != NULL);

    set = LocationSetSet_New();
    LocationSetSet_Init(set);
    result = EvalExpr(set, proc, nd->u.funccall.callee, &stCallingContextStack, map, ptf);
    result |= ExpandLocationSetSetToActuals(&stCallingContextStack, map, set);
    LocationSetSet_ForEach(set, FindCallTargetsAction, procList);
    LocationSetSet_Term(set);
    LocationSetSet_Delete(set);

    return result;
}

#pragma options align = mac68k
typedef struct FindAliasingParams {
    LocationSetSet *x0;
    LocationSet *x4;
    Boolean x8;
} FindAliasingParams;
#pragma options align = reset

inline Boolean InputsHaveNewPointerValues(PartialTransferFunction *tgtPTF, PartialTransferFunction *ptf)
{
    IRO_ASSERT(3393, tgtPTF != NULL);
    IRO_ASSERT(3394, ptf != NULL);

    return 0;
}

#pragma options align = mac68k
typedef struct CreateEPActionParams {
    ParamMapping *last;
    ParamMapping *lowest;
} CreateEPActionParams;
#pragma options align = reset

void CreateExtendedParamAction(ParamMapping *mapping, void *refcon)
{
    CreateEPActionParams *params;
    ExtendedParam *ep;
    UInt32 value;
    ExtendedParam *lowestEP;
    UInt32 lowestValue;
    ExtendedParam *lastEP;
    UInt32 lastValue;

    IRO_ASSERT(3417, mapping != NULL);
    IRO_ASSERT(3418, refcon != NULL);

    params = refcon;

    IRO_ASSERT_T(3422, params->last == NULL || params->last != NULL);
    IRO_ASSERT_T(3423, params->lowest == NULL || params->lowest != NULL);

    ep = ParamMapping_extended(mapping);
    if (ep) {
        value = ExtendedParam_num(ep);

        if (params->lowest) {
            lowestEP = ParamMapping_extended(params->lowest);
            lowestValue = ExtendedParam_num(lowestEP);
            if (params->last) {
                lastEP = ParamMapping_extended(params->last);
                lastValue = ExtendedParam_num(lastEP);
                if (value > lastValue && value < lowestValue)
                    params->lowest = mapping;
            } else if (value < lowestValue) {
                params->lowest = mapping;
            }
        } else if (params->last) {
            lastEP = ParamMapping_extended(params->last);
            lastValue = ExtendedParam_num(lastEP);
            if (value > lastValue)
                params->lowest = mapping;
        } else {
            params->lowest = mapping;
        }
    }
}

void RecordActuals(IROLinear *nd, Object *proc, ParamMappingFunction *map)
{
    IRO_ASSERT(3628, nd != NULL);
    IRO_ASSERT_MSG(3629, nd->type == IROLinearFunccall, "nd->lt == Lt_Funccall");
    IRO_ASSERT(3630, proc != NULL);
    IRO_ASSERT(3631, map != NULL);

    if (proc != &stUnknown) {
        int i;
        ObjectList *args;
        Object *arg;

        args = FunctionArguments(proc);
        if (args)
            arg = args->object;
        else
            arg = &stUnknown;

        for (i = 0; i < nd->u.funccall.argCount; i++) {
            IRO_ASSERT(3643, arg != NULL);

            if (arg != &stUnknown) {
                ParamMapping *mapping = ParamMapping_New();
                ParamMapping_Init(mapping, nd->u.funccall.args[i], arg, NULL);
                ParamMappingFunction_Add(map, mapping);
                ParamMapping_Term(mapping);
                ParamMapping_Delete(mapping);
            }

            if (args) {
                args = args->next;
                if (args)
                    arg = args->object;
                else
                    arg = &stUnknown;
            }
        }
    }
}

void FindAliasingAction2(LocationSet *ls, void *refcon)
{
    FindAliasingParams *params;

    params = refcon;

    if (!params->x8) {
        if (LocationSetsAlias(params->x4, ls))
            params->x8 = 1;
    }
}

void FindAliasingAction(LocationSet *loc, FindAliasingParams *refcon)
{
    IRO_ASSERT(3751, loc != NULL);
    IRO_ASSERT(3752, refcon != NULL);

    if (!refcon->x8) {
        refcon->x4 = loc;
        LocationSetSet_ForEach(refcon->x0, FindAliasingAction2, refcon);
    }
}

#pragma options align = mac68k
typedef struct KillLocationParams {
    Object *proc;
    IRONode *fnode;
    IROLinear *nd;
    PartialTransferFunction *ptf;
    PointsToFunction *toBeKilled;
    LocationSet *dst;
    Boolean x18;
} KillLocationParams;
#pragma options align = reset

void KillAllAddressableLocationsAction(PointsToEntry *pte, void *refcon)
{
    KillLocationParams *params;
    LocationSet *loc;

    IRO_ASSERT(3779, pte != NULL);
    IRO_ASSERT(3780, refcon != NULL);

    params = refcon;

    IRO_ASSERT(3784, params->proc != NULL);
    IRO_ASSERT(3785, params->fnode != NULL);
    IRO_ASSERT(3786, params->nd != NULL);
    IRO_ASSERT(3787, params->ptf != NULL);
    IRO_ASSERT(3788, params->toBeKilled != NULL);
    IRO_ASSERT(3789, params->dst == NULL);

    loc = PointsToEntry_loc(pte);
    IRO_ASSERT(3793, loc != NULL);
    IRO_ASSERT_T(3794, !LocationSet_IsUnknown(loc));

    if (IsAddressableLocation(loc, params->proc, params->fnode, params->nd))
        PointsToFunction_Add(params->toBeKilled, pte);
}

void KillLocationsAction(PointsToEntry *pte, void *refcon)
{
    KillLocationParams *params;
    LocationSet *loc;
    LocationSetSet *lss;

    IRO_ASSERT(3886, pte != NULL);
    IRO_ASSERT(3887, refcon != NULL);

    params = refcon;

    IRO_ASSERT(3891, params->proc != NULL);
    IRO_ASSERT_T(3892, params->fnode == NULL || params->fnode != NULL);
    IRO_ASSERT_T(3893, params->nd == NULL || params->nd != NULL);
    IRO_ASSERT(3894, params->ptf != NULL);
    IRO_ASSERT(3895, params->toBeKilled != NULL);
    IRO_ASSERT_T(3896, params->dst == NULL || params->dst != NULL);

    loc = PointsToEntry_loc(pte);
    IRO_ASSERT(3900, loc != NULL);
    IRO_ASSERT_T(3901, !LocationSet_IsUnknown(loc));

    lss = LocationSetSet_New();
    LocationSetSet_Init(lss);
    LocationSetSet_AddUnknown(lss, NULL, NULL, NULL);

    params->x18 |= Assign(params->ptf, loc, lss, params->proc, params->nd, params->fnode);

    LocationSetSet_Term(lss);
    LocationSetSet_Delete(lss);
}

PartialTransferFunction *AllocatePTF(Object *proc, IROLinear *nd, PartialTransferFunction *ptf)
{
    PartialTransferFunction *newPTF;

    IRO_ASSERT(4210, proc != NULL);
    IRO_ASSERT(4211, proc->u.func.ptfList != NULL);
    IRO_ASSERT_T(4212, nd == NULL || nd != NULL);
    IRO_ASSERT_T(4213, ptf == NULL || ptf != NULL);

    newPTF = PartialTransferFunction_New();
    PartialTransferFunction_Init(newPTF, nd, ptf);
    PTFList_Add(proc->u.func.ptfList, newPTF);
    return newPTF;
}

#pragma options align = mac68k
typedef struct EvalCallActionParams {
    Object *proc;
    IRONode *fnode;
    IROLinear *nd;
    PartialTransferFunction *ptf;
    ParamMappingFunction *map;
    int x14;
    Boolean x18;
    Boolean x19;
    Boolean x1A;
} EvalCallActionParams;
#pragma options align = reset

Boolean EvalCall(Object *proc, IRONode *fnode, IROLinear *nd, ParamMappingFunction *map, PartialTransferFunction *ptf)
{
    EvalCallActionParams params;
    ObjectSet *objSet;

    IRO_ASSERT(4548, proc != NULL);
    IRO_ASSERT(4549, fnode != NULL);
    IRO_ASSERT(4550, nd != NULL);
    IRO_ASSERT(4551, map != NULL);
    IRO_ASSERT(4552, ptf != NULL);

    memset(&params, 0, sizeof(params));
    params.proc = proc;
    params.fnode = fnode;
    params.nd = nd;
    params.ptf = ptf;
    params.map = map;
    params.x18 = 0;
    params.x19 = 0;
    params.x1A = 0;

    objSet = ObjectSet_New();
    ObjectSet_Init(objSet);

    params.x19 |= FindCallTargets(objSet, proc, nd, map, ptf);
    params.x14 = ObjectSet_Count(objSet);
    ObjectSet_ForEach(objSet, EvalCallAction, &params);

    ObjectSet_Term(objSet);
    ObjectSet_Delete(objSet);

    return params.x19;
}

#pragma options align = mac68k
typedef struct EvalAssignActionParams {
    Object *proc;
    PartialTransferFunction *ptf;
    IROLinear *nd;
    IRONode *fnode;
    LocationSetSet *srcs;
    Boolean x14;
    Boolean x15;
    Boolean x16;
} EvalAssignActionParams;
#pragma options align = reset

Boolean EvalReturn(Object *proc, IROLinear *nd, IRONode *fnode, ParamMappingFunction *map, PartialTransferFunction *ptf)
{
    EvalAssignActionParams params;
    LocationSet *loc;

    IRO_ASSERT(5046, proc != NULL);
    IRO_ASSERT(5047, nd != NULL);
    IRO_ASSERT_MSG(5048, nd->type == IROLinearReturn, "nd->lt == Lt_Return");
    IRO_ASSERT(5049, fnode != NULL);
    IRO_ASSERT(5050, map != NULL);
    IRO_ASSERT(5051, ptf != NULL);

    memset(&params, 0, sizeof(params));
    params.proc = proc;
    params.ptf = ptf;
    params.nd = nd;
    params.fnode = fnode;
    params.x15 = 0;
    params.x16 = 0;

    if (nd->u.monadic) {
        params.srcs = LocationSetSet_New();
        LocationSetSet_Init(params.srcs);

        loc = PartialTransferFunction_returnLocation(ptf);
        params.x15 |= EvalExpr(params.srcs, proc, nd->u.monadic, &stCallingContextStack, map, ptf);

        if (LocationSetSet_Count(params.srcs) != 0) {
            params.x14 = 1;
            EvalAssignAction(loc, &params);
            params.x15 |= params.x16;
        }

        LocationSetSet_Term(params.srcs);
        LocationSetSet_Delete(params.srcs);
    }

    return params.x15;
}

#pragma options align = mac68k
typedef struct ApplySummaryActionParams {
    ParamMappingFunction *tgtMap;
    Object *proc;
    IRONode *fnode;
    IROLinear *nd;
    PartialTransferFunction *ptf;
    ParamMappingFunction *map;
    LocationSet *loc;
    LocationSetSet *locs;
    Boolean x20;
    Boolean x21;
    Boolean x22;
} ApplySummaryActionParams;
#pragma options align = reset

void ApplySummaryAction(PointsToEntry *pte, void *refcon)
{
    ApplySummaryActionParams *params;
    LocationSet *loc;
    PAMemoryBlock *block;
    PAMemoryBlockKind kind;

    IRO_ASSERT(5175, pte != NULL);
    IRO_ASSERT(5176, refcon != NULL);

    params = refcon;

    IRO_ASSERT(5180, params->tgtMap != NULL);
    IRO_ASSERT(5181, params->proc != NULL);
    IRO_ASSERT(5182, params->fnode != NULL);
    IRO_ASSERT(5183, params->nd != NULL);
    IRO_ASSERT_MSG(5184, params->nd->type == IROLinearFunccall, "params->nd->lt == Lt_Funccall");
    IRO_ASSERT(5185, params->ptf != NULL);
    IRO_ASSERT(5186, params->map != NULL);

    loc = PointsToEntry_loc(pte);

    IRO_ASSERT(5189, loc != NULL);
    IRO_ASSERT_T(5190, !LocationSet_IsUnknown(loc));

    if ((block = LocationSet_block(loc)) && (kind = PAMemoryBlock_kind(block)) == PAMEMORYBLOCKKIND_EXTENDEDPARAM) {
        params->loc = loc;
        params->locs = PointsToEntry_locs(pte);
        ParamMappingFunction_ForEach(params->tgtMap, ApplySummaryAction2, params);
    }
}

inline void GetPTFAction2(ParamMapping *mapping, void *refcon)
{
    IRO_ASSERT(5331, mapping != NULL);

    if (ParamMapping_extended(mapping))
        ParamMapping_SetExtended(mapping, NULL);
}

#pragma options align = mac68k
typedef struct GetPTFActionParams {
    ParamMappingFunction *map;
    Object *proc;
    IROLinear *nd;
    PartialTransferFunction *ptf;
    Boolean *needVisit;
    PartialTransferFunction *x14;
    PartialTransferFunction *x18;
} GetPTFActionParams;
#pragma options align = reset

void GetPTFAction(PartialTransferFunction *tgtPTF, void *refcon)
{
    GetPTFActionParams *params;

    IRO_ASSERT(5359, tgtPTF != NULL);
    IRO_ASSERT(5360, refcon != NULL);

    params = refcon;

    IRO_ASSERT(5364, params->map != NULL);
    IRO_ASSERT(5365, params->proc != NULL);
    IRO_ASSERT(5366, params->proc != &stUnknown);
    IRO_ASSERT(5367, params->nd != NULL);
    IRO_ASSERT(5368, params->ptf != NULL);
    IRO_ASSERT(5369, params->needVisit != NULL);

    if (!params->x18) {
        if (MatchPTF(tgtPTF, params->proc, params->map, params->nd, params->ptf)) {
            if (InputsHaveNewPointerValues(tgtPTF, params->ptf))
                *params->needVisit = 1;
            params->x18 = tgtPTF;
            return;
        } else {
            ParamMappingFunction_ForEach(params->map, GetPTFAction2, NULL);
            if (PartialTransferFunction_contextNd(tgtPTF) == params->nd &&
                PartialTransferFunction_contextPTF(tgtPTF) == params->ptf)
                params->x14 = tgtPTF;
        }
    }
}

inline Object *FindMainEntryPoint(Object *function)
{
    IRO_ASSERT(4229, function != NULL);

    return function;
}

ObjectList *FunctionArguments(Object *proc)
{
    ObjectList *prev;
    Object *search;
    ObjectList *scan;
    ObjectList *list;
    ExtendedParam *ep;
    char *name;
    VarInfo *vi;
    Object *obj;
    FuncArg *args;
    Boolean notFound;

    IRO_ASSERT(4252, proc != NULL);

    if (proc == stCurrentProc) {
        for (list = arguments; list; list = list->next) {
            if ((obj = list->object) && obj->name && (name = obj->name->name) && name[0]) {
                prev = NULL;
                for (scan = proc->u.func.argList; scan; scan = scan->next) {
                    prev = scan;
                    if ((search = scan->object) && search->name && search->name->name) {
                        if (!strcmp(name, search->name->name))
                            break;
                    }
                }

                if (!scan)
                    search = NULL;

                notFound = search == NULL;

                if (!search) {
                    search = IRO_malloc(sizeof(Object));
                    ep = NULL;
                    search->u.var.info = IRO_malloc(sizeof(VarInfo));
                    memset(search->u.var.info, 0, sizeof(VarInfo));
                    search->u.var.info->func = proc;
                } else {
                    ep = search->extParam;
                }

                vi = search->u.var.info;
                memcpy(search, obj, sizeof(Object));
                search->extParam = ep;
                search->u.var.info = vi;
                search->u.var.realObj = obj;

                if (notFound) {
                    scan = IRO_malloc(sizeof(ObjectList));
                    scan->next = NULL;
                    scan->object = search;
                    if (!prev)
                        proc->u.func.argList = scan;
                    else
                        prev->next = scan;
                }
            }
        }
    } else if (proc->type) {
        for (args = TYPE_FUNC(proc->type)->args; args; args = args->next) {
            if (args->name && (name = args->name->name) && name[0]) {
                prev = NULL;
                for (scan = proc->u.func.argList; scan; scan = scan->next) {
                    prev = scan;
                    if ((search = scan->object) && search->name && search->name->name) {
                        if (!strcmp(name, search->name->name))
                            break;
                    }
                }

                if (!scan)
                    search = NULL;

                if (!search) {
                    search = IRO_malloc(sizeof(Object));
                    memset(search, 0, sizeof(Object));
                    search->datatype = DLOCAL;
                    search->extParam = NULL;
                    search->name = GetHashNameNode(name);
                    search->type = args->type;
                    search->qual = args->qual;
                    search->u.var.info = IRO_malloc(sizeof(VarInfo));
                    memset(search->u.var.info, 0, sizeof(VarInfo));
                    search->u.var.info->func = proc;

                    scan = IRO_malloc(sizeof(ObjectList));
                    scan->next = NULL;
                    scan->object = search;
                    if (!prev)
                        proc->u.func.argList = scan;
                    else
                        prev->next = scan;
                }
            }
        }
    }

    return proc->u.func.argList;
}

inline IRONode **FunctionNodeTable(Object *proc)
{
    IRO_ASSERT(4383, proc != NULL);

    IRO_ASSERT(4391, proc == stCurrentProc);

    return iroNodesByIndex;
}

inline IRONode *FunctionFirstNode(Object *proc)
{
    IRO_ASSERT(4401, proc != NULL);

    IRO_ASSERT(4409, proc == stCurrentProc);

    return iro_flowgraph_head;
}

void AddLocalVarsAddressedByExceptionUses(Object *var)
{
    IRO_ASSERT(5699, var != NULL);
    IRO_ASSERT(5700, stExceptionFNode != NULL);
    IRO_ASSERT(5701, stExceptionFNode->addressed != NULL);

    if (var->datatype == DLOCAL)
        ObjectSet_AddUnique(stExceptionFNode->addressed, var);
}

void PointerAnalysis_Init(void)
{
    stCallingContextStack = Stack_New();
    Stack_Init(stCallingContextStack);

    stUnknownPTF = PartialTransferFunction_New();
    PartialTransferFunction_Init(stUnknownPTF, NULL, NULL);

    stExtParamSet = ExtendedParamSet_New();
    ExtendedParamSet_Init(stExtParamSet);

    stPTFList = PTFList_New();
    PTFList_Init(stPTFList);
}

inline void CleanseLocationSet(LocationSet *loc, void *refcon)
{
    PAMemoryBlock *block;
    PALocalVar *local;
    PAMemoryBlockKind kind;

    IRO_ASSERT(6161, loc != NULL);
    IRO_ASSERT(6162, refcon == NULL);

    if (!LocationSet_IsUnknown(loc) && (block = LocationSet_block(loc)) &&
        (kind = PAMemoryBlock_kind(block)) == PAMEMORYBLOCKKIND_LOCALVAR && (local = PAMemoryBlock_thing(block))) {
        PALocalVar_SetObject(local, NULL);
    }
}

void CleansePointsToEntry(PointsToEntry *pte, void *refcon)
{
    IRO_ASSERT(6177, pte != NULL);
    IRO_ASSERT(6178, refcon == NULL);

    CleanseLocationSet(PointsToEntry_loc(pte), NULL);
    LocationSetSet_ForEach(PointsToEntry_locs(pte), CleanseLocationSet, NULL);
}

inline void PointerAnalysis_TermAction4(PartialTransferFunction *ptf, void *refcon)
{
    IRO_ASSERT(6187, ptf != NULL);
    IRO_ASSERT(6188, refcon == NULL);

    if (ptf != stUnknownPTF) {
        PointsToFunction_ForEach(PartialTransferFunction_initialPointsToFn(ptf), CleansePointsToEntry, NULL);
        PointsToFunction_ForEach(PartialTransferFunction_finalPointsToFn(ptf), CleansePointsToEntry, NULL);
    }
}

void PointerAnalysis_TermAction2(Object *obj, void *refcon)
{
    ObjectSet *objSet;
    Object *proc;
    ObjectList *list;

    objSet = refcon;

    if (!ObjectIsAnExtendedParamCandidate(obj)) {
        if (obj->datatype == DLOCAL && obj->u.var.info && (proc = obj->u.var.info->func) && ObjectIsAFunction(proc) &&
            proc->u.func.argList) {
            for (list = proc->u.func.argList; list; list = list->next) {
                if (obj == list->object)
                    break;
            }

            if (!list)
                ObjectSet_AddUnique(objSet, obj);
        } else {
            ObjectSet_AddUnique(objSet, obj);
        }
    }
}

void PointerAnalysis_TermAction1(ExtendedParam *ep, void *refcon)
{
    ObjectSet *objSet;
    ObjectSet *epObjSet;
    Object *obj;

    objSet = ObjectSet_New();
    ObjectSet_Init(objSet);

    epObjSet = ExtendedParam_objectSet(ep);
    obj = NULL;
    ObjectSet_ForEach(epObjSet, FindGlobalObjectAction, &obj);
    if (obj) {
        ObjectSet_ForEach(epObjSet, PointerAnalysis_TermAction2, objSet);
        if (ObjectSet_FindFirst(objSet))
            ExtendedParam_RemoveObjects(ep, objSet);
    }

    ObjectSet_Term(objSet);
    ObjectSet_Delete(objSet);
}

void PointerAnalysis_Term(void)
{
    if (stExtParamSet) {
        ExtendedParamSet_ForEach(stExtParamSet, PointerAnalysis_TermAction1, NULL);
        ExtendedParamSet_Term(stExtParamSet);
        ExtendedParamSet_Delete(stExtParamSet);
        stExtParamSet = NULL;
    }

    if (stPTFList) {
        PTFList_ForEach(stPTFList, PointerAnalysis_TermAction4, NULL);
        PTFList_Term(stPTFList);
        PTFList_Delete(stPTFList);
        stPTFList = NULL;
    }

    PartialTransferFunction_Term(stUnknownPTF);
    PartialTransferFunction_Delete(stUnknownPTF);
    stUnknownPTF = NULL;

    Stack_Term(&stCallingContextStack);
    Stack_Delete(stCallingContextStack);
    stCallingContextStack = NULL;
}

void InvalidatePointsToFunctions(Object *proc)
{
    IRONode *fnode;
    IROLinear *nd;

    IRO_ASSERT(6302, proc != NULL);

    for (fnode = FunctionFirstNode(proc); fnode; fnode = fnode->nextnode) {
        if (fnode->last) {
            if (fnode->addressed) {
                ObjectSet_Term(fnode->addressed);
                ObjectSet_Delete(fnode->addressed);
                fnode->addressed = NULL;
            }

            for (nd = fnode->first; nd && nd != fnode->last->next; nd = nd->next) {
                if (((PointsToFunction *) nd->f1a)) {
                    PointsToFunction_Term(((PointsToFunction *) nd->f1a));
                    PointsToFunction_Delete(((PointsToFunction *) nd->f1a));
                    nd->f1a = 0;
                }
            }
        }
    }
}

inline void InitialSetup(void)
{
}

void PointerAnalysis_HeapErrorProc(void)
{
    longjmp(stAbortPointerAnalysis, 2);
}

void PointerAnalysis_Setup(void)
{
    IRO_InitializeAllocator();
    stExtendedParamNum = 0;
    stParamObjs = NULL;
    stMaxPassCount = 0;
}

void PointerAnalysis_Cleanup(void)
{
    ObjectList *list;
    ObjectList *next;

    for (list = stParamObjs; list; list = next) {
        IRO_free(list->object);
        next = list->next;
        IRO_free(list);
    }
    stParamObjs = NULL;
    IRO_TerminateAllocator();
}

void IRO_AnalyzePointers(Object *function)
{
    EvalCallActionParams params;
    IROLinear nd;
    IRONode fnode;
    int code;
    void (*volatile saveheaperror)();

    IRO_ASSERT(6393, function != NULL);

    PointerAnalysis_Init();

    memset(&params, 0, sizeof(params));
    memset(&nd, 0, sizeof(nd));
    nd.type = IROLinearFunccall;
    memset(&fnode, 0, sizeof(fnode));
    params.proc = &stUnknown;
    params.fnode = &fnode;
    params.nd = &nd;
    params.ptf = stUnknownPTF;
    params.map = NULL;
    params.x18 = 0;
    params.x19 = 0;
    params.x1A = 1;

    stCurrentProc = FindMainEntryPoint(function);

    if ((code = setjmp(stAbortPointerAnalysis)) == 0) {
        saveheaperror = CompilerTools_GetHeapCallback();
        CompilerTools_SetHeapCallback(PointerAnalysis_HeapErrorProc);
        InitialSetup();
        EvalCallAction(stCurrentProc, &params);
        PointerAnalysis_Term();
        stCurrentProc = NULL;
        CompilerTools_SetHeapCallback(saveheaperror);
    } else {
        CompilerTools_SetHeapCallback(saveheaperror);
        InvalidatePointsToFunctions(stCurrentProc);
        PointerAnalysis_Term();
        stCurrentProc = NULL;
        if (code == 2 && saveheaperror)
            saveheaperror();
    }
}

void GetDefiniteObjectOfExtendedParamLoc(LocationSet *loc, Object **resultObj, CInt64 *resultField)
{
    Object *obj;
    CInt64 field;
    PAMemoryBlock *block;
    ExtendedParam *ep;
    ObjectSet *objSet;
    LocationSetSet *locs;
    LocationSet *tmp;
    PALocalVar *local;
    PAMemoryBlockKind kind;

    IRO_ASSERT(6763, loc != NULL);
    IRO_ASSERT(6764, resultObj != NULL);
    IRO_ASSERT(6765, resultField != NULL);

    obj = NULL;
    field = LocationSet_field(loc);
    block = LocationSet_block(loc);

    if (block && (kind = PAMemoryBlock_kind(block)) == PAMEMORYBLOCKKIND_EXTENDEDPARAM) {
        if ((ep = PAMemoryBlock_thing(block)) && (objSet = ExtendedParam_objectSet(ep))) {
            IRO_ASSERT(6777, obj == NULL);
            ObjectSet_ForEach(objSet, FindGlobalObjectAction, &obj);
        }
    }

    if (!obj) {
        locs = LocationSetSet_New();
        LocationSetSet_Init(locs);
        GetActualLocsOfExtendedParam(locs, loc, NULL, &stCallingContextStack, NULL, 0);

        if (LocationSetSet_Count(locs) == 1 && (tmp = LocationSetSet_FindFirst(locs)) && !LocationSet_IsUnknown(tmp)) {
            field = CInt64_Add(field, LocationSet_field(tmp));
            if ((block = LocationSet_block(tmp)) && (kind = PAMemoryBlock_kind(block)) == PAMEMORYBLOCKKIND_EXTENDEDPARAM &&
                (ep = PAMemoryBlock_thing(block)) && (objSet = ExtendedParam_objectSet(ep))) {
                IRO_ASSERT(6801, obj == NULL);
                ObjectSet_ForEach(objSet, FindGlobalObjectAction, &obj);
            } else if ((block = LocationSet_block(tmp)) && (kind = PAMemoryBlock_kind(block)) == PAMEMORYBLOCKKIND_LOCALVAR &&
                       (local = PAMemoryBlock_thing(block))) {
                obj = GetLocalObject(local, &stUnknown, 0);
            }
        }

        LocationSetSet_Term(locs);
        LocationSetSet_Delete(locs);
    }

    *resultObj = obj;
    *resultField = field;
}

void CreateExpressionForLocationSet(LocationSet *loc, IROList *list, Type *rtype, Object *proc)
{
    CInt64 field;
    PAMemoryBlock *block;
    PAMemoryBlockKind kind;
    void *thing;
    IROLinear *nd;
    Object *obj;

    IRO_ASSERT(6833, loc != NULL);
    IRO_ASSERT_T(6834, !LocationSet_IsUnknown(loc));
    IRO_ASSERT(6835, LocationSet_stride(loc) == 0);
    IRO_ASSERT(6836, list != NULL);
    IRO_ASSERT(6837, rtype != NULL);
    IRO_ASSERT(6838, proc != NULL);

    field = LocationSet_field(loc);
    block = LocationSet_block(loc);
    kind = PAMemoryBlock_kind(block);
    thing = PAMemoryBlock_thing(block);
    nd = NULL;

    switch (kind) {
    case PAMEMORYBLOCKKIND_EXTENDEDPARAM:
        GetDefiniteObjectOfExtendedParamLoc(loc, &obj, &field);
        if (obj) {
            nd = IrOptimizer_NewLinear(IROLinearOperand);
            nd->u.node = create_objectrefnode(obj);
            nd->rtype = rtype;
            nd->index = ++linear_index_counter;
            IroUtil_AppendLinear(nd, list);
        }
        break;
    case PAMEMORYBLOCKKIND_LOCALVAR:
        obj = GetLocalObject(thing, proc, 0);
        if (obj) {
            if (ObjectIsAFunctionArgument(proc, obj) && obj->u.var.realObj)
                obj = obj->u.var.realObj;
            nd = IrOptimizer_NewLinear(IROLinearOperand);
            nd->u.node = create_objectrefnode(obj);
            nd->rtype = rtype;
            nd->index = ++linear_index_counter;
            IroUtil_AppendLinear(nd, list);
        }
        break;
    case PAMEMORYBLOCKKIND_INT:
        if (IS_TYPE_INT(rtype)) {
            nd = IroVars_CreateIntConstant(*((CInt64 *) thing), rtype);
            IroUtil_AppendLinear(nd, list);
        }
        break;
    case PAMEMORYBLOCKKIND_OTHER:
        break;
    default:
        CError_FATAL(6894);
    }

    if (nd && !CInt64_IsZero(&field)) {
        IROLinear *nd2;
        IROLinear *nd3;

        nd2 = IroVars_CreateIntConstant(field, (Type *) &stunsignedlong);
        IroUtil_AppendLinear(nd2, list);

        nd3 = IrOptimizer_NewLinear(IROLinearOp2Arg);
        nd3->nodetype = EADD;
        nd3->index = ++linear_index_counter;
        nd3->rtype = rtype;
        nd3->u.diadic.left = nd;
        nd3->u.diadic.right = nd2;
        IroUtil_AppendLinear(nd3, list);
    }
}

typedef struct IROListNode {
    IROList list;
    struct IROListNode *nextList;
} IROListNode;

#pragma options align = mac68k
typedef struct LookupLinearExprActionParams {
    Object *proc;
    Type *indirectType;
    IROListNode **list;
} LookupLinearExprActionParams;
#pragma options align = reset

inline void LookupLinearExprAction(LocationSet *loc, void *refcon)
{
    LookupLinearExprActionParams *params;
    IROListNode *list;

    IRO_ASSERT(6926, loc != NULL);
    IRO_ASSERT(6927, refcon != NULL);

    params = refcon;

    IRO_ASSERT(6931, params->proc != NULL);
    IRO_ASSERT(6932, params->indirectType != NULL);
    IRO_ASSERT(6933, params->list != NULL);

    list = *params->list = IRO_malloc(sizeof(IROListNode));
    IroUtil_InitList(&list->list);
    list->nextList = NULL;

    if (!LocationSet_IsUnknown(loc) && LocationSetRepresentsSingleLocation(loc, NULL, NULL))
        CreateExpressionForLocationSet(loc, &list->list, params->indirectType, params->proc);

    params->list = &list->nextList;
}

void PointerAnalysis_LookupLinearNodePointerExpr(Object *proc, IROLinear *indirect, IROListNode **list)
{
    LocationSetSet *set;
    LookupLinearExprActionParams params;

    IRO_ASSERT(6957, indirect != NULL);

    if (!indirect->f1a)
        return;

    set = LocationSetSet_New();
    LocationSetSet_Init(set);
    EvalExpr(set, proc, indirect, NULL, NULL, NULL);

    memset(&params, 0, sizeof(params));
    params.proc = proc;
    params.indirectType = indirect->rtype;
    params.list = list;

    LocationSetSet_ForEach(set, LookupLinearExprAction, &params);
    LocationSetSet_Term(set);
    LocationSetSet_Delete(set);
}

#pragma options align = mac68k
typedef struct GetFunctionDepsOrKillsActionParams {
    Object *proc;
    PartialTransferFunction *ptf;
    IROLinear *funccall;
    ParamMappingFunction *map;
    ObjectList **list;
} GetFunctionDepsOrKillsActionParams;
#pragma options align = reset

void GetFunctionDepsOrKillsAction(LocationSet *ls, void *refcon)
{
    GetFunctionDepsOrKillsActionParams *params;
    ObjectList *list;
    PAMemoryBlock *block;
    ExtendedParam *ep;
    ObjectSet *objSet;
    PALocalVar *local;
    Object *obj;
    PAMemoryBlockKind kind;

    IRO_ASSERT(7204, ls != NULL);
    IRO_ASSERT(7205, refcon != NULL);

    params = refcon;

    IRO_ASSERT(7209, params->proc != NULL);
    IRO_ASSERT(7210, params->ptf != NULL);
    IRO_ASSERT(7211, params->funccall != NULL);
    IRO_ASSERT_T(7212, params->map == NULL || params->map != NULL);
    IRO_ASSERT(7213, params->list != NULL);

    list = *params->list = IRO_malloc(sizeof(ObjectList));
    list->object = NULL;
    list->next = NULL;

    if (!LocationSet_IsUnknown(ls) && (block = LocationSet_block(ls))) {
        obj = NULL;
        switch ((kind = PAMemoryBlock_kind(block))) {
        case PAMEMORYBLOCKKIND_EXTENDEDPARAM:
            if ((ep = PAMemoryBlock_thing(block)) && (objSet = ExtendedParam_objectSet(ep))) {
                IRO_ASSERT(7232, obj == NULL);
                ObjectSet_ForEach(objSet, FindGlobalObjectAction, &obj);
            }
            break;
        case PAMEMORYBLOCKKIND_LOCALVAR:
            if ((local = PAMemoryBlock_thing(block))) {
                obj = GetLocalObject(local, &stUnknown, 0);
                if (ObjectIsAFunctionArgument(FunctionName, obj) && obj->u.var.realObj)
                    obj = obj->u.var.realObj;
            }
            break;
        }

        list->object = obj;
    }

    params->list = &list->next;
}

void GetFunctionDepsOrKillsAction2(PointsToEntry *pte, void *refcon)
{
    GetFunctionDepsOrKillsActionParams *params;
    LocationSet *loc;
    PAMemoryBlock *block;
    LocationSetSet *set;
    PAMemoryBlockKind kind;

    IRO_ASSERT(7264, pte != NULL);
    IRO_ASSERT(7265, refcon != NULL);

    params = refcon;

    IRO_ASSERT(7269, params->proc != NULL);
    IRO_ASSERT(7270, params->ptf != NULL);
    IRO_ASSERT(7271, params->funccall != NULL);
    IRO_ASSERT(7272, params->map != NULL);
    IRO_ASSERT(7273, params->list != NULL);

    loc = PointsToEntry_loc(pte);

    IRO_ASSERT_T(7277, !LocationSet_IsUnknown(loc));

    block = LocationSet_block(loc);
    if (block && (kind = PAMemoryBlock_kind(block)) == PAMEMORYBLOCKKIND_EXTENDEDPARAM) {
        set = LocationSetSet_New();
        LocationSetSet_Init(set);
        GetActualLocsOfExtendedParam(set, loc, NULL, &stCallingContextStack, params->map, 0);
        LocationSetSet_ForEach(set, GetFunctionDepsOrKillsAction, params);
        LocationSetSet_Term(set);
        LocationSetSet_Delete(set);
    }
}

void CreateFalseContext(Object *proc, IROLinear *funccall, PartialTransferFunction **resultPTF,
                        ParamMappingFunction **resultMap)
{
    IROLinear myFunccall;
    Boolean needVisit;
    ParamMappingFunction *map;
    PartialTransferFunction *ptf;
    StackElement *stackElement;
    ParamMappingFunction *map2;
    PartialTransferFunction *ptf2;
    StackElement *stackElement2;

    PointerAnalysis_Init();
    memset(&myFunccall, 0, sizeof(myFunccall));
    myFunccall.type = IROLinearFunccall;

    map = ParamMappingFunction_New();
    ParamMappingFunction_Init(map);
    RecordActuals(&myFunccall, FunctionName, map);

    needVisit = 0;
    ptf = GetPTF(map, FunctionName, &myFunccall, stUnknownPTF, &needVisit);

    stackElement = StackElement_New();
    StackElement_Init(stackElement, FunctionName, ptf, map, &myFunccall);
    Stack_Push(&stCallingContextStack, stackElement);
    StackElement_Term(stackElement);
    StackElement_Delete(stackElement);

    map2 = ParamMappingFunction_New();
    ParamMappingFunction_Init(map2);
    RecordActuals(funccall, proc, map2);

    if (FunctionName != proc) {
        needVisit = 0;
        ptf2 = GetPTF(map2, proc, funccall, ptf, &needVisit);

        stackElement2 = StackElement_New();
        StackElement_Init(stackElement2, proc, ptf2, map2, funccall);
        Stack_Push(&stCallingContextStack, stackElement2);
        StackElement_Term(stackElement2);
        StackElement_Delete(stackElement2);
    } else {
        ptf2 = stUnknownPTF;
    }

    *resultPTF = ptf2;
    *resultMap = map2;
}

void DestroyFalseContext(Object *proc, PartialTransferFunction *ptf, ParamMappingFunction *map)
{
    StackElement *element;

    if (FunctionName != proc) {
        element = Stack_Pop(&stCallingContextStack);
        StackElement_Term(element);
        StackElement_Delete(element);
    }

    ParamMappingFunction_Term(map);
    ParamMappingFunction_Delete(map);

    element = Stack_Pop(&stCallingContextStack);
    map = StackElement_map(element);
    ParamMappingFunction_Term(map);
    ParamMappingFunction_Delete(map);
    StackElement_Term(element);
    StackElement_Delete(element);

    PointerAnalysis_Term();
}

void PointerAnalysis_GetFunctionKills(Object *proc, IROLinear *funccall, ObjectList **list)
{
    Boolean fail;
    PartialTransferFunction *ptf;
    LocationSetSet *set;
    GetFunctionDepsOrKillsActionParams params;

    IRO_ASSERT(7398, proc != NULL);
    IRO_ASSERT(7399, funccall != NULL);
    IRO_ASSERT_MSG(7400, funccall->type == IROLinearFunccall, "funccall->lt == Lt_Funccall");

    if (!ObjectIsAFunction(proc))
        return;

    fail = proc->u.func.ptfList == NULL || (ptf = PTFList_FindFirst(proc->u.func.ptfList)) == NULL;
    if (!fail) {
        set = PartialTransferFunction_funcModifies(ptf);
        fail = set == NULL;
        if (!fail) {
            memset(&params, 0, sizeof(params));
            params.proc = proc;
            params.ptf = ptf;
            params.funccall = funccall;
            params.map = NULL;
            params.list = list;
            LocationSetSet_ForEach(set, GetFunctionDepsOrKillsAction, &params);
        }
    }

    if (fail) {
        *list = IRO_malloc(sizeof(ObjectList));
        (*list)->object = NULL;
        (*list)->next = NULL;
    }
}

void PointerAnalysis_GetFunctionDependencies(Object *proc, IROLinear *funccall, ObjectList **list)
{
    Boolean fail;
    PartialTransferFunction *ptf;
    ParamMappingFunction *map;
    PointsToFunction *finalPointsTo;
    GetFunctionDepsOrKillsActionParams params;

    IRO_ASSERT(7446, proc != NULL);
    IRO_ASSERT(7447, funccall != NULL);
    IRO_ASSERT_MSG(7448, funccall->type == IROLinearFunccall, "funccall->lt == Lt_Funccall");

    if (!ObjectIsAFunction(proc))
        return;

    fail = !proc->u.func.ptfList || !PTFList_FindFirst(proc->u.func.ptfList);
    if (!fail) {
        CreateFalseContext(proc, funccall, &ptf, &map);
        fail = !ptf || !map || !(finalPointsTo = PartialTransferFunction_finalPointsToFn(ptf));
        if (!fail) {
            memset(&params, 0, sizeof(params));
            params.proc = proc;
            params.ptf = ptf;
            params.funccall = funccall;
            params.map = map;
            params.list = list;
            PointsToFunction_ForEach(finalPointsTo, GetFunctionDepsOrKillsAction2, &params);
        }
        DestroyFalseContext(proc, ptf, map);
    }

    if (fail) {
        *list = IRO_malloc(sizeof(ObjectList));
        (*list)->object = NULL;
        (*list)->next = NULL;
    }
}

void PointerAnalysis_PragmaMode(void)
{
    if (((UInt8 *)cprep_cu)[0xe2] != 0) {
        CPrepTokenizer_SkipToEndOfLine();
        return;
    }
    if (CPrep_ScanMacroExpandedChar() != 0) {
        if (CPrepTokenizer_ScanToken() == -3) {
            if (!strcmp(((char *)data_00587fa0) + 10, "addr")) {
                copts.ff2 = 0;
                return;
            }
            if (!strcmp(((char *)data_00587fa0) + 10, "ansi")) {
                copts.ff2 = 1;
                return;
            }
            if (!strcmp(((char *)data_00587fa0) + 10, "type")) {
                copts.ff2 = 2;
                return;
            }
        }
        fn_0043f3b0(0x69);
        CPrepTokenizer_SkipToEndOfLine();
        return;
    }
    if (copts.warn_illpragma)
        fn_0043f3b0(0xba);
    CPrepTokenizer_SkipToEndOfLine();
}

/* The helper rows are inline functions: while the analysis functions that call them are lifted, this table makes the
 * compiler emit their out-of-line copies. */
void *IroPointerAnalysis_HelperRows[] = {
    (void *)FindGlobalObjectAction, (void *)PointerAnalysis_TermAction4, (void *)LookupLinearExprAction,
    (void *)GetPTFAction2,
    (void *)StackElement_New, (void *)StackElement_Delete, (void *)StackElement_Copy, (void *)StackElement_Term,
    (void *)Stack_New, (void *)Stack_Pop, (void *)ObjectSet_New, (void *)ObjectSet_Delete, (void *)ObjectSet_Init,
    (void *)ObjectSet_Term, (void *)ObjectSet_AddUnique, (void *)ObjectSet_Remove, (void *)ObjectSet_RemoveAll,
    (void *)ObjectSet_AddSetAction, (void *)ObjectSet_SimpleAddSetAction, (void *)ObjectSet_RemoveSetAction,
    (void *)ObjectSets_Equal, (void *)ExtendedParams_Equal, (void *)ExtendedParamSet_Delete,
    (void *)ExtendedParamSet_Term, (void *)ExtendedParamSet_RemoveAll, (void *)PAHeapBlocks_Equal,
    (void *)PALocalVars_Equal, (void *)PAMemoryBlock_New, (void *)PAMemoryBlock_Delete, (void *)PAMemoryBlock_Init,
    (void *)PAMemoryBlock_Term, (void *)MemoryBlocks_Equal, (void *)LocationSet_New, (void *)LocationSet_Delete,
    (void *)LocationSet_InitKnown, (void *)LocationSet_InitUnknown, (void *)LocationSet_Copy, (void *)LocationSet_Term,
    (void *)LocationSets_Overlap, (void *)LocationSets_Equal, (void *)LocationSets_LookupCompatible,
    (void *)LocationSet_Contains, (void *)LocationSet_IsUnknown, (void *)LocationSet_SetRtype,
    (void *)LocationSet_stride, (void *)LocationSet_restriction, (void *)LocationSet_bitfieldOf,
    (void *)LocationSetSet_New, (void *)LocationSetSet_Delete, (void *)LocationSetSet_Init, (void *)LocationSetSet_Copy,
    (void *)LocationSetSet_Term, (void *)LocationSetSet_Find, (void *)LocationSetSet_FindFirst,
    (void *)LocationSetSet_Count, (void *)LocationSetSet_RemoveAllWithMemoryBlock, (void *)LocationSetSet_SimpleAdd,
    (void *)LocationSetSet_Add, (void *)LocationSetSet_AddUnknown, (void *)LocationSetSet_Remove,
    (void *)LocationSetSet_RemoveAll, (void *)LocationSetSet_AddSetAction, (void *)LocationSetSet_SimpleAddSetAction,
    (void *)LocationSetSet_RemoveSetAction, (void *)ParamMapping_New, (void *)ParamMapping_Delete,
    (void *)ParamMapping_Copy, (void *)ParamMapping_Term, (void *)ParamMappingFunction_New,
    (void *)ParamMappingFunction_Delete, (void *)ParamMappingFunction_Init, (void *)ParamMappingFunction_Term,
    (void *)ParamMappingFunction_FindMappingByFormal, (void *)ParamMappingFunction_Add,
    (void *)ParamMappingFunction_Remove, (void *)ParamMappingFunction_RemoveAll,
    (void *)ParamMappingFunction_AddFunctionAction, (void *)PointsToEntry_New, (void *)PointsToEntry_Delete,
    (void *)PointsToEntry_Init, (void *)PointsToEntry_Copy, (void *)PointsToEntry_Term, (void *)PointsToEntries_Equal,
    (void *)PointsToFunction_New, (void *)PointsToFunction_Delete, (void *)PointsToFunction_Init,
    (void *)PointsToFunction_Copy, (void *)PointsToFunction_Term, (void *)PointsToFunction_FindByLocationSet,
    (void *)PointsToFunction_FindByLookupCompatibleLocationSet, (void *)PointsToFunction_RemoveOverlappingLocations,
    (void *)ShouldAddNewPointsToEntryToFunction, (void *)PointsToFunction_SimpleAdd, (void *)PointsToFunction_Add,
    (void *)PointsToFunction_RemoveByLocationSet, (void *)PointsToFunction_RemoveAll,
    (void *)PointsToFunction_AddFunctionAction, (void *)PointsToFunction_SimpleAddFunctionAction,
    (void *)PartialTransferFunction_returnLocation, (void *)PTFList_Delete, (void *)PTFList_Term,
    (void *)PTFList_RemoveAll, (void *)CInt64_IsNegative, (void *)CInt64_SetLong, (void *)CInt64_SetULong, (void *)gcd,
    (void *)ExtendedParam_TermAction, (void *)CleanseLocationSet,
};
#endif
