#include "compiler/IroVars.h"
#include "compiler/IroDump.h"
#include "compiler/IroFlowgraph.h"
#include "compiler/IroJump.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroMalloc.h"
#include "compiler/IroPointerAnalysis.h"
#include "compiler/IROUseDef.h"
#include "compiler/IroUtil.h"
#include "compiler/BitVector.h"
#include "compiler/CClass.h"
#include "compiler/CError.h"
#include "compiler/CException.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/enode.h"
#include "compiler/Exceptions.h"
#include "compiler/InlineAsmPPC.h"
#include "compiler/objects.h"
#include "compiler/Registers.h"

#pragma pack(push, 2) // Pro7: options align=mac68k
typedef struct IndirectRecordMatch {
    IROLinear* nd;
    struct IndirectRecordMatch* otherMatches;
} IndirectRecordMatch;

typedef struct IndirectRecord {
    IROLinear* linear;
    unsigned char flags;
    VarRecord* var;
    SInt32 addend;
    SInt32 size;
    int startbit;
    int endbit;
    Type* type;
    struct IndirectRecord* next;
    IndirectRecordMatch* matches;
    IROLinear* x26;
} IndirectRecord;
#pragma pack(pop)

static IndirectRecord* IRO_FirstIndirectRecord;
static IndirectRecord* IRO_LastIndirectRecord;
static IROLinear* TheBaseTerm;
VarRecord* IRO_FirstVar;
VarRecord* IRO_LastVar;
SInt32 IRO_NumVars;
Boolean IRO_IsBitField;
SInt32 IRO_BaseTerms;
SInt32 IRO_VarTerms;
Boolean IRO_IsModifyOp[MAXEXPR];
Boolean IRO_IsAssignOp[MAXEXPR];

// (2.6 returns int where Pro7 has Boolean: the callers' `xor eax, eax`)
static int CheckAddress(IROLinear* nd, Boolean* resultFlag);
static int CheckAddressConsistency(IROLinear* nd, VarRecord* var, SInt32 addend, Type* type);
static void DisableEntries(IndirectRecord* irec);
static UInt32 ObjectIsArg(Object* obj);

void IRO_InitializeIRO_IsModifyOpArray(void) {
    int i;

    for (i = 0; i < MAXEXPR; i++) {
        IRO_IsModifyOp[i] = FALSE;
    }

    IRO_IsModifyOp[EPOSTINC] = TRUE;
    IRO_IsModifyOp[EPOSTDEC] = TRUE;
    IRO_IsModifyOp[EPREINC] = TRUE;
    IRO_IsModifyOp[EPREDEC] = TRUE;
    IRO_IsModifyOp[EINDIRECT] = FALSE;
    IRO_IsModifyOp[EMONMIN] = FALSE;
    IRO_IsModifyOp[EBINNOT] = FALSE;
    IRO_IsModifyOp[ELOGNOT] = FALSE;
    IRO_IsModifyOp[EFORCELOAD] = FALSE;
    IRO_IsModifyOp[EMUL] = FALSE;
    IRO_IsModifyOp[EMULV] = FALSE;
    IRO_IsModifyOp[EDIV] = FALSE;
    IRO_IsModifyOp[EMODULO] = FALSE;
    IRO_IsModifyOp[EADDV] = FALSE;
    IRO_IsModifyOp[ESUBV] = FALSE;
    IRO_IsModifyOp[EADD] = FALSE;
    IRO_IsModifyOp[ESUB] = FALSE;
    IRO_IsModifyOp[ESHL] = FALSE;
    IRO_IsModifyOp[ESHR] = FALSE;
    IRO_IsModifyOp[ELESS] = FALSE;
    IRO_IsModifyOp[EGREATER] = FALSE;
    IRO_IsModifyOp[ELESSEQU] = FALSE;
    IRO_IsModifyOp[EGREATEREQU] = FALSE;
    IRO_IsModifyOp[EEQU] = FALSE;
    IRO_IsModifyOp[ENOTEQU] = FALSE;
    IRO_IsModifyOp[EAND] = FALSE;
    IRO_IsModifyOp[EXOR] = FALSE;
    IRO_IsModifyOp[EOR] = FALSE;
    IRO_IsModifyOp[ELAND] = FALSE;
    IRO_IsModifyOp[ELOR] = FALSE;
    IRO_IsModifyOp[EASS] = FALSE;
    IRO_IsModifyOp[EMULASS] = TRUE;
    IRO_IsModifyOp[EDIVASS] = TRUE;
    IRO_IsModifyOp[EMODASS] = TRUE;
    IRO_IsModifyOp[EADDASS] = TRUE;
    IRO_IsModifyOp[ESUBASS] = TRUE;
    IRO_IsModifyOp[ESHLASS] = TRUE;
    IRO_IsModifyOp[ESHRASS] = TRUE;
    IRO_IsModifyOp[EANDASS] = TRUE;
    IRO_IsModifyOp[EXORASS] = TRUE;
    IRO_IsModifyOp[EORASS] = TRUE;
    IRO_IsModifyOp[ECOMMA] = FALSE;
    IRO_IsModifyOp[EPMODULO] = TRUE;
    IRO_IsModifyOp[EROTL] = TRUE;
    IRO_IsModifyOp[EROTR] = TRUE;
    IRO_IsModifyOp[EBCLR] = TRUE;
    IRO_IsModifyOp[EBTST] = TRUE;
    IRO_IsModifyOp[EBSET] = TRUE;
    IRO_IsModifyOp[ETYPCON] = FALSE;
    IRO_IsModifyOp[EBITFIELD] = FALSE;
    IRO_IsModifyOp[EINTCONST] = FALSE;
    IRO_IsModifyOp[EFLOATCONST] = FALSE;
    IRO_IsModifyOp[ESTRINGCONST] = FALSE;
    IRO_IsModifyOp[ECOND] = FALSE;
    IRO_IsModifyOp[EFUNCCALL] = FALSE;
    IRO_IsModifyOp[EFUNCCALLP] = FALSE;
    IRO_IsModifyOp[EOBJREF] = FALSE;
    IRO_IsModifyOp[EMFPOINTER] = FALSE;
    IRO_IsModifyOp[ENULLCHECK] = FALSE;
    IRO_IsModifyOp[EPRECOMP] = FALSE;
    IRO_IsModifyOp[ETEMP] = FALSE;
    IRO_IsModifyOp[EARGOBJ] = FALSE;
    IRO_IsModifyOp[ELOCOBJ] = FALSE;
    IRO_IsModifyOp[ELABEL] = FALSE;
    IRO_IsModifyOp[ESETCONST] = FALSE;
    IRO_IsModifyOp[ENEWEXCEPTION] = FALSE;
    IRO_IsModifyOp[ENEWEXCEPTIONARRAY] = FALSE;
    IRO_IsModifyOp[EOBJLIST] = FALSE;
    IRO_IsModifyOp[EMEMBER] = FALSE;
    IRO_IsModifyOp[ETEMPLDEP] = FALSE;
    IRO_IsModifyOp[EINSTRUCTION] = FALSE;
    IRO_IsModifyOp[EDEFINE] = FALSE;
    IRO_IsModifyOp[EREUSE] = FALSE;
    IRO_IsModifyOp[EASSBLK] = FALSE;
    IRO_IsModifyOp[EVECTOR128CONST] = FALSE;
    IRO_IsModifyOp[ECONDASS] = TRUE;
}

void IRO_InitializeIRO_IsAssignOpArray(void) {
    int i;

    for (i = 0; i < MAXEXPR; i++) {
        IRO_IsAssignOp[i] = FALSE;
    }

    IRO_IsAssignOp[EPOSTINC] = TRUE;
    IRO_IsAssignOp[EPOSTDEC] = TRUE;
    IRO_IsAssignOp[EPREINC] = TRUE;
    IRO_IsAssignOp[EPREDEC] = TRUE;
    IRO_IsAssignOp[EINDIRECT] = FALSE;
    IRO_IsAssignOp[EMONMIN] = FALSE;
    IRO_IsAssignOp[EBINNOT] = FALSE;
    IRO_IsAssignOp[ELOGNOT] = FALSE;
    IRO_IsAssignOp[EFORCELOAD] = FALSE;
    IRO_IsAssignOp[EMUL] = FALSE;
    IRO_IsAssignOp[EMULV] = FALSE;
    IRO_IsAssignOp[EDIV] = FALSE;
    IRO_IsAssignOp[EMODULO] = FALSE;
    IRO_IsAssignOp[EADDV] = FALSE;
    IRO_IsAssignOp[ESUBV] = FALSE;
    IRO_IsAssignOp[EADD] = FALSE;
    IRO_IsAssignOp[ESUB] = FALSE;
    IRO_IsAssignOp[ESHL] = FALSE;
    IRO_IsAssignOp[ESHR] = FALSE;
    IRO_IsAssignOp[ELESS] = FALSE;
    IRO_IsAssignOp[EGREATER] = FALSE;
    IRO_IsAssignOp[ELESSEQU] = FALSE;
    IRO_IsAssignOp[EGREATEREQU] = FALSE;
    IRO_IsAssignOp[EEQU] = FALSE;
    IRO_IsAssignOp[ENOTEQU] = FALSE;
    IRO_IsAssignOp[EAND] = FALSE;
    IRO_IsAssignOp[EXOR] = FALSE;
    IRO_IsAssignOp[EOR] = FALSE;
    IRO_IsAssignOp[ELAND] = FALSE;
    IRO_IsAssignOp[ELOR] = FALSE;
    IRO_IsAssignOp[EASS] = TRUE;
    IRO_IsAssignOp[EMULASS] = TRUE;
    IRO_IsAssignOp[EDIVASS] = TRUE;
    IRO_IsAssignOp[EMODASS] = TRUE;
    IRO_IsAssignOp[EADDASS] = TRUE;
    IRO_IsAssignOp[ESUBASS] = TRUE;
    IRO_IsAssignOp[ESHLASS] = TRUE;
    IRO_IsAssignOp[ESHRASS] = TRUE;
    IRO_IsAssignOp[EANDASS] = TRUE;
    IRO_IsAssignOp[EXORASS] = TRUE;
    IRO_IsAssignOp[EORASS] = TRUE;
    IRO_IsAssignOp[ECOMMA] = FALSE;
    IRO_IsAssignOp[EPMODULO] = TRUE;
    IRO_IsAssignOp[EROTL] = TRUE;
    IRO_IsAssignOp[EROTR] = TRUE;
    IRO_IsAssignOp[EBCLR] = TRUE;
    IRO_IsAssignOp[EBTST] = TRUE;
    IRO_IsAssignOp[EBSET] = TRUE;
    IRO_IsAssignOp[ETYPCON] = FALSE;
    IRO_IsAssignOp[EBITFIELD] = FALSE;
    IRO_IsAssignOp[EINTCONST] = FALSE;
    IRO_IsAssignOp[EFLOATCONST] = FALSE;
    IRO_IsAssignOp[ESTRINGCONST] = FALSE;
    IRO_IsAssignOp[ECOND] = FALSE;
    IRO_IsAssignOp[EFUNCCALL] = FALSE;
    IRO_IsAssignOp[EFUNCCALLP] = FALSE;
    IRO_IsAssignOp[EOBJREF] = FALSE;
    IRO_IsAssignOp[EMFPOINTER] = FALSE;
    IRO_IsAssignOp[ENULLCHECK] = FALSE;
    IRO_IsAssignOp[EPRECOMP] = FALSE;
    IRO_IsAssignOp[ETEMP] = FALSE;
    IRO_IsAssignOp[EARGOBJ] = FALSE;
    IRO_IsAssignOp[ELOCOBJ] = FALSE;
    IRO_IsAssignOp[ELABEL] = FALSE;
    IRO_IsAssignOp[ESETCONST] = FALSE;
    IRO_IsAssignOp[ENEWEXCEPTION] = FALSE;
    IRO_IsAssignOp[ENEWEXCEPTIONARRAY] = FALSE;
    IRO_IsAssignOp[EOBJLIST] = FALSE;
    IRO_IsAssignOp[EMEMBER] = FALSE;
    IRO_IsAssignOp[ETEMPLDEP] = FALSE;
    IRO_IsAssignOp[EINSTRUCTION] = FALSE;
    IRO_IsAssignOp[EDEFINE] = FALSE;
    IRO_IsAssignOp[EREUSE] = FALSE;
    IRO_IsAssignOp[EASSBLK] = FALSE;
    IRO_IsAssignOp[EVECTOR128CONST] = FALSE;
    IRO_IsAssignOp[ECONDASS] = TRUE;
}

VarRecord* IRO_FindVar(Object* object, Boolean flag1, Boolean flag2) {
    VarRecord* var;
    VarInfo* vi;

    var = object->varptr;
    if (!var && flag1) {
        vi = NULL;
        if (object->datatype == DLOCAL) {
            vi = object->u.var.info;
        }
        if (vi) {
            vi->usage = 0;
        }
        var = oalloc(sizeof(VarRecord));
        var->object = object;
        var->index = ++IRO_NumVars;
        var->unused = 0;
        var->inductionState = 0;
        var->next = NULL;
        var->defs = NULL;
        var->uses = NULL;
        var->bitfieldType = NULL;
        var->bitFieldReplacement = NULL;
        var->noregister = FALSE;
        if (IRO_FirstVar) {
            IRO_LastVar->next = var;
        } else {
            IRO_FirstVar = var;
        }
        IRO_LastVar = var;
        object->varptr = var;
    }

    if (var && !flag2) {
        var->noregister = TRUE;
        if (object->datatype ==
            DLOCAL &&
            object->u.var.info &&
            object->u.var.info->noregister == 0) {
            object->u.var.info->noregister = 2;
        }
    }

    return var;
}

// (2.6: external, IRO_Optimizer calls it after delinearizing; Pro7's is static)
void ResetVars(void) {
    VarRecord* var;

    for (var = IRO_FirstVar; var; var = var->next) {
        var->noregister = FALSE;
        if (var->object && var->object->datatype == DLOCAL && var->object->u.var.info) {
            if (var->object->u.var.info->noregister == 2) {
                var->object->u.var.info->noregister = 0;
            }
        }
    }
}

static void IRO_HandleExceptionActions(IROLinear* linear) {
    ExceptionAction* action;

    for (action = linear->stmt->dobjstack; action; action = action->prev) {
        switch (action->type) {
            case EAT_DESTROYLOCAL:
                IRO_FindVar(action->data.destroy_local.local, TRUE, FALSE);
                break;
            case EAT_DESTROYLOCALCOND:
                IRO_FindVar(action->data.destroy_local_cond.local, TRUE, FALSE);
                break;
            case EAT_DESTROYLOCALOFFSET:
                IRO_FindVar(action->data.destroy_local_offset.local, TRUE, FALSE);
                break;
            case EAT_DESTROYLOCALARRAY:
                IRO_FindVar(action->data.destroy_local_array.localarray, TRUE, FALSE);
                break;
            case EAT_DESTROYMEMBER:
            case EAT_DESTROYBASE:
                IRO_FindVar(action->data.destroy_member.objectptr, TRUE, FALSE);
                break;
            case EAT_DESTROYMEMBERCOND:
                IRO_FindVar(action->data.destroy_member_cond.objectptr, TRUE, FALSE);
                break;
            case EAT_DESTROYMEMBERARRAY:
                IRO_FindVar(action->data.destroy_member_array.objectptr, TRUE, FALSE);
                break;
            case EAT_CATCHBLOCK:
                if (action->data.catch_block.catch_object) {
                    IRO_FindVar(action->data.catch_block.catch_object, TRUE, FALSE);
                }
                IRO_FindVar(action->data.catch_block.catch_info_object, TRUE, FALSE);
                break;
            case EAT_ACTIVECATCHBLOCK:
                IRO_FindVar(action->data.active_catch_block.catch_info_object, TRUE, FALSE);
                break;
        }
    }
}

void IRO_FindAllVars(void) {
    IROLinear* linear;
    VarRecord* var;
    Object* obj; // (2.6, as 1.x: the object in a local; Pro7 rereads linear->u.node->data.objref)

    linear = IRO_FirstLinear;
    IRO_NumVars = 0;
    IRO_FirstVar = IRO_LastVar = NULL;

    while (linear) {
        if (IS_LINEAR_ENODE(linear, EOBJREF)) {
            obj = linear->u.node->data.objref;
            if (Inline_IsObjectData(obj) || obj->datatype == DLOCAL) {
                IRO_FindVar(obj, TRUE, (linear->flags & IROLF_Ind) != 0);
            } else {
                obj->varptr = NULL;
            }
        } else if (linear->type == IROLinearFunccall) {
            if (linear->stmt && IRO_FunctionCallMightThrowException(linear)) {
                IRO_HandleExceptionActions(linear);
            }
        } else if (linear->type == IROLinearAsm) {
            IAEffects effects;
            int i;
            CodeGen_GetAsmEffects(linear->u.asm_stmt, &effects);
            for (i = 0; i < effects.numoperands; i++) {
                IRO_FindVar(
                    effects.operands[i].object, TRUE, effects.operands[i].type != IAEffect_AddressOf
                );
            }
        }
        linear = linear->next;
    }

    var = IRO_FirstVar;
    Bv_AllocVector(&IRO_FuncKills, IRO_NumVars + 1);
    Bv_SetBit(0, IRO_FuncKills);
    while (var) {
        if (Inline_IsObjectData(var->object) || var->noregister) {
            Bv_SetBit(var->index, IRO_FuncKills);
        }
        var = var->next;
    }

    IRO_CheckForUserBreak();
}

void IRO_ZapVarPtrs(void) {
    VarRecord* var;

    for (var = IRO_FirstVar; var; var = var->next) {
        var->object->varptr = NULL;
    }
}

void IRO_UpdateVars(void) {
    IROLinear* linear;
    VarRecord* var;
    Object* obj; // (as in IRO_FindAllVars)

    ResetVars();

    for (linear = IRO_FirstLinear; linear; linear = linear->next) {
        if (IS_LINEAR_ENODE(linear, EOBJREF)) {
            obj = linear->u.node->data.objref;
            if (Inline_IsObjectData(obj) || obj->datatype == DLOCAL) {
                IRO_FindVar(
                    obj, FALSE, (linear->flags & IROLF_Ind) || !(linear->flags & IROLF_Reffed)
                );
            }
        } else if (linear->type == IROLinearFunccall) {
            if (linear->stmt && IRO_FunctionCallMightThrowException(linear)) {
                IRO_HandleExceptionActions(linear);
            }
        } else if (linear->type == IROLinearAsm) {
            IAEffects effects;
            int i;
            CodeGen_GetAsmEffects(linear->u.asm_stmt, &effects);
            for (i = 0; i < effects.numoperands; i++) {
                IRO_FindVar(
                    effects.operands[i].object,
                    FALSE,
                    effects.operands[i].type != IAEffect_AddressOf
                );
            }
        }
    }

    var = IRO_FirstVar;
    Bv_AllocVector(&IRO_FuncKills, IRO_NumVars + 1);
    Bv_SetBit(0, IRO_FuncKills);
    while (var) {
        if (Inline_IsObjectData(var->object) || var->noregister) {
            Bv_SetBit(var->index, IRO_FuncKills);
        }
        var = var->next;
    }
}

void IRO_AddElmToList(IROLinear* linear, IROElmList** list) {
    IROElmList* l = oalloc(sizeof(IROElmList));
    l->element = linear;
    l->next = NULL;
    if (!*list) {
        *list = l;
    } else {
        l->next = *list;
        *list = l;
    }
}

void IRO_DecomposeAddressExpression(IROLinear* linear, IROAddrRecord* rec) {
    if (IS_LINEAR_DIADIC(linear->u.diadic.left, EADD)) {
        IRO_DecomposeAddressExpression(linear->u.diadic.left, rec);
    } else if (IS_LINEAR_ENODE(linear->u.diadic.left, EINTCONST)) {
        rec->numInts++;
        IRO_AddElmToList(linear->u.diadic.left, &rec->ints);
    } else if (IS_LINEAR_ENODE(linear->u.diadic.left, EOBJREF)) {
        rec->numObjRefs++;
        IRO_AddElmToList(linear->u.diadic.left, &rec->objRefs);
    } else {
        rec->numMisc++;
        IRO_AddElmToList(linear->u.diadic.left, &rec->misc);
    }

    if (IS_LINEAR_DIADIC(linear->u.diadic.right, EADD)) {
        IRO_DecomposeAddressExpression(linear->u.diadic.right, rec);
    } else if (IS_LINEAR_ENODE(linear->u.diadic.right, EINTCONST)) {
        rec->numInts++;
        IRO_AddElmToList(linear->u.diadic.right, &rec->ints);
    } else if (IS_LINEAR_ENODE(linear->u.diadic.right, EOBJREF)) {
        rec->numObjRefs++;
        IRO_AddElmToList(linear->u.diadic.right, &rec->objRefs);
    } else {
        rec->numMisc++;
        IRO_AddElmToList(linear->u.diadic.right, &rec->misc);
    }
}

void IRO_DecomposeAddressExpression_Cheap(IROLinear* linear) {
    if (IS_LINEAR_DIADIC(linear->u.diadic.left, EADD)) {
        IRO_DecomposeAddressExpression_Cheap(linear->u.diadic.left);
    } else if (!IS_LINEAR_ENODE(linear->u.diadic.left, EINTCONST)) {
        if (IS_LINEAR_ENODE(linear->u.diadic.left, EOBJREF)) {
            IRO_BaseTerms++;
            TheBaseTerm = linear->u.diadic.left;
        } else {
            IRO_VarTerms++;
        }
    }

    if (IS_LINEAR_DIADIC(linear->u.diadic.right, EADD)) {
        IRO_DecomposeAddressExpression_Cheap(linear->u.diadic.right);
    } else if (!IS_LINEAR_ENODE(linear->u.diadic.right, EINTCONST)) {
        if (IS_LINEAR_ENODE(linear->u.diadic.right, EOBJREF)) {
            IRO_BaseTerms++;
            TheBaseTerm = linear->u.diadic.right;
        } else {
            IRO_VarTerms++;
        }
    }
}

VarRecord* IRO_FindAssigned(IROLinear* linear) {
    VarRecord* rec;

    IRO_IsBitField = FALSE;
    if (linear->type == IROLinearOp2Arg) {
        linear = linear->u.diadic.left;
    } else if (linear->type == IROLinearOp1Arg) {
        linear = linear->u.monadic;
    } else {
        CError_FATAL(817);
    }

    if (IS_LINEAR_MONADIC(linear, EINDIRECT)) {
        linear = linear->u.monadic;
    }

    if (IS_LINEAR_MONADIC(linear, EBITFIELD)) {
        IRO_IsBitField = TRUE;
        linear = linear->u.monadic;
    }

    if (IS_LINEAR_DIADIC(linear, EADD)) {
        if (IS_LINEAR_ENODE(linear->u.diadic.left, EOBJREF) &&
            IS_LINEAR_ENODE(linear->u.diadic.right, EINTCONST)) {
            linear = linear->u.diadic.left;
        } else {
            IRO_BaseTerms = 0;
            IRO_DecomposeAddressExpression_Cheap(linear);
            if (IRO_BaseTerms == 1) {
                linear = TheBaseTerm;
            }
        }
    }

    if (!IS_LINEAR_ENODE(linear, EOBJREF)) {
        return NULL;
    }

    if ((rec = IRO_FindVar(linear->u.node->data.objref, FALSE, TRUE))) {
        return rec;
    } else {
        return NULL;
    }
}

// (2.6 walks the result list twice with scan, where Pro7 keeps a second walker `list`; Pro7's
// declaration order gives other registers)
static void GetKillsByIndirectAssignment(IROLinear* linear) {
    IROListNode* resultList;
    IROListNode* scan;
    IROLinear* nd;
    IROLinear* inner;
    Boolean result;
    Boolean foundLinear;

    result = FALSE;

    if (linear->type == IROLinearOp2Arg) {
        linear = linear->u.diadic.left;
    } else {
        linear = linear->u.monadic;
    }

    if (linear &&
        linear->type ==
        IROLinearOp1Arg &&
        linear->nodetype ==
        EINDIRECT &&
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

                foundLinear = FALSE;
                for (nd = scan->list.head; nd != scan->list.tail->next; nd = nd->next) {
                    if (nd->type == IROLinearOperand && nd->u.node->type == EOBJREF) {
                        foundLinear = TRUE;
                        break;
                    }
                }
                if (!foundLinear) {
                    result = TRUE;
                    break;
                }
            }

            if (!result) {
                for (scan = resultList; scan; scan = scan->nextList) {
                    for (nd = scan->list.head; nd != scan->list.tail->next; nd = nd->next) {
                        if (nd->type == IROLinearOperand && nd->u.node->type == EOBJREF) {
                            Object* obj;
                            VarRecord* var;
                            int index;

                            obj = nd->u.node->data.objref;
                            CError_ASSERT(951, obj != NULL);

                            var = IRO_FindVar(obj, TRUE, TRUE);
                            CError_ASSERT(953, var != NULL);

                            index = var->index;
                            CError_ASSERT(955, index != 0);

                            Bv_SetBit(index, IRO_VarKills);
                        }
                    }
                }
            }

            while (resultList) {
                IROListNode* next = resultList->nextList;
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
        Bv_Or(IRO_FuncKills, IRO_VarKills);
    }
}

static void GetKillsByFunctionCall(IROLinear* linear) {
    IROListNode* resultList;
    IROListNode* list; // (2.6 frees the result list through it; Pro7 declares it unused)
    IROListNode* scan;
    IROLinear* nd;
    IROLinear* inner;
    Boolean result;
    Boolean foundLinear;
    Object* obj;
    ObjectList* killList;
    ObjectList* olist;

    result = FALSE;

    if ((inner = linear->u.funccall.callee) &&
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

                foundLinear = FALSE;
                for (nd = scan->list.head; nd != scan->list.tail->next; nd = nd->next) {
                    if (nd->type == IROLinearOperand && nd->u.node->type == EOBJREF) {
                        foundLinear = TRUE;
                        obj = nd->u.node->data.objref;
                        CError_ASSERT(1027, obj != NULL);

                        killList = NULL;
                        PointerAnalysis_GetFunctionKills(obj, linear, &killList);

                        for (olist = killList; olist; olist = olist->next) {
                            if (!olist->object) {
                                result = TRUE;
                                break;
                            }
                        }

                        while (killList) {
                            ObjectList* next = killList->next;
                            IRO_free(killList);
                            killList = next;
                        }

                        if (result) {
                            break;
                        }
                    }
                }

                if (!foundLinear) {
                    result = TRUE;
                }
                if (result) {
                    break;
                }
            }

            if (!result) {
                for (scan = resultList; scan; scan = scan->nextList) {
                    for (nd = scan->list.head; nd != scan->list.tail->next; nd = nd->next) {
                        if (nd->type == IROLinearOperand && nd->u.node->type == EOBJREF) {
                            obj = nd->u.node->data.objref;
                            killList = NULL;
                            PointerAnalysis_GetFunctionKills(obj, linear, &killList);

                            for (olist = killList; olist; olist = olist->next) {
                                VarRecord* var;
                                int index;

                                var = IRO_FindVar(olist->object, TRUE, TRUE);
                                CError_ASSERT(1078, var != NULL);

                                index = var->index;
                                CError_ASSERT(1080, index != 0);

                                Bv_SetBit(index, IRO_VarKills);
                            }

                            while (killList) {
                                ObjectList* next = killList->next;
                                IRO_free(killList);
                                killList = next;
                            }
                        }
                    }
                }
            }

            while (resultList) {
                list = resultList->nextList;
                IRO_free(resultList);
                resultList = list;
            }
        } else {
            result = TRUE;
        }
    } else {
        result = TRUE;
    }

    if (result) {
        Bv_Or(IRO_FuncKills, IRO_VarKills);
    }
}

void IRO_GetKills(IROLinear* linear) {
    VarRecord* var;

    switch (linear->type) {
        case IROLinearOp1Arg:
        case IROLinearOp2Arg:
            if (IRO_IsAssignOp[linear->nodetype]) {
                IROLinear* assigned;
                int index;

                var = IRO_FindAssigned(linear);
                index = 0;
                if (var) {
                    index = var->index;
                }
                Bv_SetBit(index, IRO_VarKills);

                if (!index) {
                    GetKillsByIndirectAssignment(linear);
                }
            }
            break;
        case IROLinearFunccall:
            GetKillsByFunctionCall(linear);
            break;
        case IROLinearAsm: {
            IAEffects effects;
            int i;

            CodeGen_GetAsmEffects(linear->u.asm_stmt, &effects);
            for (i = 0; i < effects.numoperands; i++) {
                switch (effects.operands[i].type) {
                    case IAEffect_Write:
                    case IAEffect_Clobber:
                    case IAEffect_ReadWrite:
                        if ((var = IRO_FindVar(effects.operands[i].object, FALSE, TRUE))) {
                            Bv_SetBit(var->index, IRO_VarKills);
                        }
                        break;
                }
            }

            if (effects.writesMemory || effects.branchWithLink) {
                Bv_Or(IRO_FuncKills, IRO_VarKills);
            }
            break;
        }
    }
}

void IRO_CheckInit(void) {
    IRONode* fnode;
    IROLinear* nd;
    VarRecord* var;
    ObjectList* olist;
    int flag;
    int i;
    int j; // (2.6: the propagation loop has a counter of its own; Pro7 reuses i)
    BitVector* bv;

    IRO_MakeReachable(IRO_FirstNode);
    fnode = IRO_FirstNode;
    Bv_AllocVector(&IRO_VarKills, IRO_NumVars + 1);

    while (fnode) {
        Bv_AllocVector(&fnode->in, IRO_NumVars + 1);
        Bv_AllocVector(&fnode->gen, IRO_NumVars + 1);

        for (nd = fnode->first; nd; nd = nd->next) {
            if (nd->type == IROLinearOperand && nd->u.node->type == EOBJREF) {
                if (nd->flags & IROLF_Ind) {
                    if (!(nd->flags & IROLF_Assigned) || (nd->flags & IROLF_Used)) {
                        if ((var = IRO_FindVar(nd->u.node->data.objref, FALSE, TRUE))) {
                            if (!Bv_IsBitSet(var->index, fnode->gen)) {
                                Bv_SetBit(var->index, fnode->in);
                            }
                        }
                    }
                } else {
                    if ((var = IRO_FindVar(nd->u.node->data.objref, FALSE, FALSE))) {
                        Bv_SetBit(var->index, fnode->gen);
                    }
                }
            } else if (nd->type == IROLinearOp2Arg && nd->nodetype == EASS) {
                if ((var = IRO_FindAssigned(nd))) {
                    Bv_SetBit(var->index, fnode->gen);
                }
            } else if (nd->type == IROLinearAsm) {
                IAEffects effects;

                CodeGen_GetAsmEffects(nd->u.asm_stmt, &effects);
                for (i = 0; i < effects.numoperands; i++) {
                    var = IRO_FindVar(effects.operands[i].object, FALSE, TRUE);
                    switch (effects.operands[i].type) {
                        case IAEffect_Write:
                        case IAEffect_Clobber:
                        case IAEffect_ReadWrite:
                            Bv_SetBit(var->index, fnode->gen);
                            break;
                    }
                }
            }

            if (nd == fnode->last) {
                break;
            }
        }

        fnode = fnode->nextnode;
    }

    Bv_AllocVector(&bv, IRO_NumVars + 1);

    do {
        flag = FALSE;
        for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
            Bv_Copy(fnode->gen, bv);
            for (j = 0; j < fnode->numpred; j++) {
                Bv_Or(IRO_NodeTable[fnode->pred[j]]->gen, bv);
            }
            if (!Bv_Compare(bv, fnode->gen)) {
                Bv_Copy(bv, fnode->gen);
                flag = TRUE;
            }
        }
    } while (flag);

    Bv_Clear(IRO_VarKills);

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        if (fnode->reachable) {
            Bv_Copy(fnode->in, bv);
            for (i = 0; i < fnode->numpred; i++) {
                Bv_Minus(IRO_NodeTable[fnode->pred[i]]->gen, bv);
            }
            Bv_Or(bv, IRO_VarKills);
        }
    }

    for (olist = locals; olist; olist = olist->next) {
        // (2.6 nests the two tests Pro7 joins with &&)
        if ((var = IRO_FindVar(olist->object, FALSE, TRUE))) {
            if (Bv_IsBitSet(var->index, IRO_VarKills)) {
                VarInfo* vi = olist->object->u.var.info;
                if (!IsTempName(olist->object->name) && !is_volatile_object(olist->object)) {
                    if (!IS_TYPE_CLASS(olist->object->type) ||
                        !CClass_IsEmpty(TYPE_CLASS(olist->object->type))) {
                        CError_SetErrorToken(&vi->deftoken);
                        CError_Warning(
                            ERR_VARIABLE_NOT_INITIALIZED_BEFORE_BEING_USED,
                            olist->object->name->name
                        );
                    }
                }
            }
        }
    }

    IRO_CheckForUserBreak();
}

static void RewriteIndDec(void) {
    // (2.6's declaration order: Pro7's, add/indirect/Int/nd, gives other registers)
    IROLinear* nd;
    IROLinear* add;
    IROLinear* Int;
    IROLinear* indirect;
    CInt64 value;
    IROList list;

    for (nd = IRO_FirstLinear; nd; nd = nd->next) {
        if (nd->type ==
            IROLinearOp1Arg &&
            (nd->nodetype == EPOSTINC || nd->nodetype == EPOSTDEC) &&
            !(nd->flags & IROLF_Reffed)) {
            indirect = nd->u.monadic;
            if (indirect->type ==
                IROLinearOp1Arg &&
                indirect->nodetype ==
                EINDIRECT &&
                indirect->u.monadic->type ==
                IROLinearOp1Arg &&
                indirect->u.monadic->nodetype == EBITFIELD) {
                value = IRO_GetSelfAssignmentVal(nd);
                IRO_InitList(&list);

                nd->type = IROLinearOp2Arg;
                nd->nodetype = EASS;
                nd->u.diadic.left = indirect;
                IRO_DuplicateExpr(indirect, &list);

                Int = IRO_NewIntConst(value, indirect->rtype);
                Int->flags |= IROLF_Used | IROLF_Reffed;

                add = IRO_NewLinear(IROLinearOp2Arg);
                add->nodetype = EADD;
                add->u.diadic.left = list.tail;
                add->u.diadic.left->flags = 0;
                add->u.diadic.left->flags |= IROLF_Used | IROLF_Reffed;
                add->u.diadic.right = Int;
                add->rtype = indirect->rtype;
                add->flags = 0;
                add->flags |= IROLF_Used | IROLF_Reffed;

                nd->u.diadic.right = add;

                IRO_AddToList(Int, &list);
                IRO_AddToList(add, &list);
                IRO_PasteAfter(list.head, list.tail, indirect);
            }
        }
    }
}

void IRO_RewriteBitFieldTemps(void) {
    IROLinear* nd;
    IROLinear* expr2;
    Object* obj;
    VarRecord* var;
    IROList list;

    for (nd = IRO_FirstLinear; nd; nd = nd->next) {
        if ((obj = IRO_IsVariable(nd)) && (nd->flags & IROLF_BitfieldIndirect)) {
            var = IRO_FindVar(obj, FALSE, TRUE);
            CError_ASSERT(1525, var != NULL);

            expr2 = var->bitFieldReplacement;
            CError_ASSERT(1531, expr2 != NULL);

            IRO_InitList(&list);
            IRO_DuplicateExpr(expr2, &list);
            IRO_NopOut(nd->u.monadic);
            nd->u.monadic = list.tail;
            IRO_Paste(list.head, list.tail, nd);
        }
    }
}

// (2.6: an early break where Pro7 has `if (!result) { dependencies }`, and the list walkers' next
// pointers at function scope; Pro7's block-local ones give other registers)
static Boolean FunctionCallMightUseOrKillAnyAddressedVar(IROLinear* funccall) {
    IROListNode* nextList;
    IROLinear* funcnd;
    IROLinear* nd;
    IROListNode* scan;
    IROListNode* resultList;
    ObjectList* olist;
    ObjectList* next;
    ObjectList* killList;
    VarRecord* var;
    Boolean result;
    Boolean foundObjRef;
    Object* obj;

    result = FALSE;

    funcnd = funccall->u.funccall.callee;
    if (funcnd && copts.opt_pointer_analysis && funcnd->pointsToFunction && FunctionName) {
        resultList = NULL;
        PointerAnalysis_LookupLinearNodePointerExpr(FunctionName, funcnd, &resultList);

        if (resultList) {
            for (scan = resultList; scan; scan = scan->nextList) {
                if (!scan->list.head || !scan->list.tail) {
                    result = TRUE;
                    break;
                }

                foundObjRef = FALSE;
                for (nd = scan->list.head; nd != scan->list.tail->next; nd = nd->next) {
                    if (nd->type == IROLinearOperand && nd->u.node->type == EOBJREF) {
                        foundObjRef = TRUE;
                        obj = nd->u.node->data.objref;
                        CError_ASSERT(1591, obj != NULL);

                        killList = NULL;
                        PointerAnalysis_GetFunctionKills(obj, funccall, &killList);
                        for (olist = killList; olist; olist = olist->next) {
                            if (!olist->object) {
                                result = TRUE;
                            } else {
                                var = IRO_FindVar(olist->object, TRUE, TRUE);
                                CError_ASSERT(1603, var != NULL);
                                if (var->noregister) {
                                    result = TRUE;
                                }
                            }

                            if (result) {
                                break;
                            }
                        }

                        while (killList) {
                            next = killList->next;
                            IRO_free(killList);
                            killList = next;
                        }

                        if (result) {
                            break;
                        }

                        killList = NULL;
                        PointerAnalysis_GetFunctionDependencies(obj, funccall, &killList);
                        for (olist = killList; olist; olist = olist->next) {
                            if (!olist->object) {
                                result = TRUE;
                            } else {
                                var = IRO_FindVar(olist->object, TRUE, TRUE);
                                CError_ASSERT(1631, var != NULL);
                                if (var->noregister) {
                                    result = TRUE;
                                }
                            }

                            if (result) {
                                break;
                            }
                        }

                        while (killList) {
                            next = killList->next;
                            IRO_free(killList);
                            killList = next;
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

            while (resultList) {
                nextList = resultList->nextList;
                IRO_free(resultList);
                resultList = nextList;
            }
        } else {
            result = TRUE;
        }
    } else {
        result = TRUE;
    }

    return result;
}

// (2.6 reuses the parameter for Pro7's `inner` and declares next at function scope, in this order)
static Boolean IndirectMightUseOrKillAnyAddressedVar(IROLinear* indirect) {
    IROListNode* next;
    IROLinear* nd;
    Object* obj;
    IROListNode* resultList;
    Boolean result;
    IROListNode* scan;
    Boolean foundObjRef;
    VarRecord* var;

    result = FALSE;

    if (indirect &&
        indirect->type ==
        IROLinearOp1Arg &&
        indirect->nodetype ==
        EINDIRECT &&
        (indirect = indirect->u.monadic) &&
        copts.opt_pointer_analysis &&
        indirect->pointsToFunction &&
        FunctionName) {
        resultList = NULL;
        PointerAnalysis_LookupLinearNodePointerExpr(FunctionName, indirect, &resultList);
        if (resultList) {
            for (scan = resultList; scan; scan = scan->nextList) {
                if (!scan->list.head || !scan->list.tail) {
                    result = TRUE;
                    break;
                }

                foundObjRef = FALSE;
                for (nd = scan->list.head; nd != scan->list.tail->next; nd = nd->next) {
                    if (nd->type == IROLinearOperand && nd->u.node->type == EOBJREF) {
                        foundObjRef = TRUE;
                        obj = nd->u.node->data.objref;
                        CError_ASSERT(1722, obj != NULL);
                        var = IRO_FindVar(obj, TRUE, TRUE);
                        CError_ASSERT(1724, var != NULL);

                        if (var->noregister) {
                            result = TRUE;
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

    return result;
}

void IRO_ScalarizeClassDataMembers(void) {
    IROLinear* nd;
    Object* obj;
    IndirectRecord* irec;
    IndirectRecordMatch* match;
    IROLinear* tmp;
    // (2.6 declares the lists at function scope, in this order; Pro7's block-local ones change the
    // frame)
    IROList list1;
    VarRecord* var;
    IROList list2;
    Boolean flag;
    // (2.6 copies the object before the inlined ObjectIsArg; passing irec->var->object, as Pro7
    // does, gives other registers)
    Object* argobj;

    nd = IRO_FirstLinear;
    IRO_FirstIndirectRecord = NULL;
    IRO_LastIndirectRecord = NULL;
    RewriteIndDec();
    IRO_DumpAfterPhase("RewriteIndDec", 0);
    flag = FALSE;

    while (nd) {
        if (nd->type == IROLinearOp1Arg && nd->nodetype == EINDIRECT) {
            CheckAddress(nd, &flag);
        }
        if (nd->type == IROLinearAsm) {
            IAEffects effects;
            SInt32 i;
            CodeGen_GetAsmEffects(nd->u.asm_stmt, &effects);
            for (i = 0; i < effects.numoperands; i++) {
                if ((var = IRO_FindVar(effects.operands[i].object, FALSE, TRUE))) {
                    CheckAddressConsistency(nd, var, -1, &stvoid);
                } else {
                    CError_FATAL(1822);
                }
            }
        }
        if (nd->type == IROLinearFunccall && !flag) {
            flag = FunctionCallMightUseOrKillAnyAddressedVar(nd);
        }

        nd = nd->next;
    }

    if (flag) {
        // (2.6 writes Pro7's DisableAddressedEntries out here, walking with irec; the call,
        // inlined, gives other registers)
        for (irec = IRO_FirstIndirectRecord; irec; irec = irec->next) {
            if (irec->var->noregister) {
                irec->flags |= 1;
            }
        }
    }

    for (irec = IRO_FirstIndirectRecord; irec; irec = irec->next) {
        if (!(irec->flags & 1)) {
            obj = create_temp_object(irec->type);
            var = IRO_FindVar(obj, TRUE, TRUE);
            for (match = irec->matches; match; match = match->otherMatches) {
                IRO_InitList(&list1);
                IRO_InitList(&list2);
                IRO_DuplicateExpr(match->nd, &list1);
                IRO_DuplicateExpr(match->nd, &list2);
                if (match->nd->type == IROLinearOperand && match->nd->u.node->type == EOBJREF) {
                    tmp = IRO_LocateFather(match->nd);
                    if (irec->flags & 2) {
                        tmp->flags |= IROLF_BitfieldIndirect;
                        var->bitfieldType = TYPE_BITFIELD(match->nd->rtype);
                        var->bitFieldReplacement = list2.tail;
                    }
                    match->nd->u.node = create_objectrefnode(obj);
                    irec->x26 = list2.tail;
                } else {
                    tmp = IRO_LocateFather(match->nd);
                    if (irec->flags & 2) {
                        tmp->flags |= IROLF_BitfieldIndirect;
                        var->bitfieldType = TYPE_BITFIELD(match->nd->rtype);
                        var->bitFieldReplacement = list2.tail;
                    }
                    irec->x26 = list2.tail;

                    IRO_NopOut(tmp->u.monadic);
                    tmp->u.monadic = IRO_NewLinear(IROLinearOperand);
                    tmp->u.monadic->u.node = create_objectrefnode(obj);
                    tmp->u.monadic->rtype = match->nd->rtype;
                    tmp->u.monadic->index = ++IRO_NumLinear;
                    tmp->u.monadic->flags |= IROLF_Ind | IROLF_Reffed;
                    IRO_NopOut(match->nd);
                    IRO_PasteAfter(tmp->u.monadic, tmp->u.monadic, match->nd);
                }
            }

            argobj = irec->var->object;
            if (ObjectIsArg(argobj)) {
                IROLinear* op1;
                IROLinear* op2;
                IROLinear* op3;
                IROLinear* op4;
                IROLinear* op5;

                op1 = IRO_NewLinear(IROLinearOperand);
                op1->u.node = create_objectrefnode(obj);
                op1->rtype = op1->u.node->data.objref->type;
                op1->index = ++IRO_NumLinear;
                op1->flags |= IROLF_Ind | IROLF_Assigned;

                op2 = list1.tail;

                op3 = IRO_NewLinear(IROLinearOp1Arg);
                op3->nodetype = EINDIRECT;
                op3->rtype = irec->type;
                op3->u.monadic = op1;
                op3->index = ++IRO_NumLinear;
                op3->flags |= IROLF_Assigned;

                op4 = IRO_NewLinear(IROLinearOp1Arg);
                op4->nodetype = EINDIRECT;
                op4->rtype = irec->type;
                op4->u.monadic = op2;
                op4->index = ++IRO_NumLinear;
                op4->flags |= IROLF_Reffed;

                op5 = IRO_NewLinear(IROLinearOp2Arg);
                op5->nodetype = EASS;
                op5->u.diadic.left = op3;
                op5->u.diadic.right = op4;
                op5->rtype = irec->type;
                op5->index = ++IRO_NumLinear;

                op2->next = op4;
                op4->next = op1;
                op1->next = op3;
                op3->next = op5;

                IRO_PasteAfter(list1.head, op5, IRO_FirstLinear);
            }
        }
    }

    IRO_CheckForUserBreak();
}

static int CheckAddress(IROLinear* nd, Boolean* resultFlag) {
    IROLinear* inner;
    Object* obj; // (2.6, as 1.x's: the object in a local)
    VarRecord* var;
    Boolean result;
    // TODO: 2.6 loads intval.hi into a register before the test (Pro7 compares it in memory); a
    // local assigned in the condition gives that, nothing reads it. Tried: Pro7's `.hi == 0`,
    // `!...hi`, int/long/unsigned casts, `0 == ...`, CTool_EndianReadWord32, a CInt64 or ENode*
    // local. An inline `SInt32 f(CInt64* n) { return n->hi; }` also matches (a UInt32 return
    // does not): 2.6 may test through such a helper, which neither Pro7 nor Wii names.
    SInt32 hi;

    inner = nd->u.monadic;
    if (inner->type == IROLinearOp1Arg && inner->nodetype == EBITFIELD) {
        inner = inner->u.monadic;
    }

    result = FALSE;

    if (inner->type == IROLinearOperand && inner->u.node->type == EOBJREF) {
        obj = inner->u.node->data.objref;
        if (IS_TYPE_CLASS(obj->type) || IS_TYPE_STRUCT(obj->type) || IS_TYPE_ARRAY(obj->type)) {
            if (obj->datatype == DLOCAL) {
                var = IRO_FindVar(obj, FALSE, TRUE);
                CError_ASSERT(2239, var != NULL);
                CheckAddressConsistency(nd, var, 0, nd->rtype);
                result = TRUE;
            }
        }
    } else if (inner->type ==
               IROLinearOp2Arg &&
               inner->nodetype ==
               EADD &&
               inner->u.diadic.left->type ==
               IROLinearOperand &&
               inner->u.diadic.left->u.node->type == EOBJREF) {
        if (inner->u.diadic.right->type ==
            IROLinearOperand &&
            inner->u.diadic.right->u.node->type ==
            EINTCONST &&
            (hi = inner->u.diadic.right->u.node->data.intval.hi) == 0) {
            obj = inner->u.diadic.left->u.node->data.objref;
            if (IS_TYPE_CLASS(obj->type) || IS_TYPE_STRUCT(obj->type) || IS_TYPE_ARRAY(obj->type)) {
                if (obj->datatype == DLOCAL) {
                    var = IRO_FindVar(obj, FALSE, TRUE);
                    CError_ASSERT(2266, var != NULL);
                    CheckAddressConsistency(
                        nd, var, inner->u.diadic.right->u.node->data.intval.lo, nd->rtype
                    );
                    result = TRUE;
                }
            }
        } else {
            obj = inner->u.diadic.left->u.node->data.objref;
            if (IS_TYPE_CLASS(obj->type) || IS_TYPE_STRUCT(obj->type) || IS_TYPE_ARRAY(obj->type)) {
                if (obj->datatype == DLOCAL) {
                    var = IRO_FindVar(obj, FALSE, TRUE);
                    CheckAddressConsistency(nd, var, -1, nd->rtype);
                    result = TRUE;
                }
            }
        }
    }

    if (!result && !*resultFlag) {
        *resultFlag = IndirectMightUseOrKillAnyAddressedVar(nd);
    }

    return 0;
}

static int CheckAddressConsistency(IROLinear* nd, VarRecord* var, SInt32 addend, Type* type) {
    IndirectRecord* rp;
    IndirectRecordMatch* match;
    UInt32 flag;
    IROLinear* inner;
    UInt32 start_bit;
    UInt32 end_bit;
    TypeBitfield* tbitfield;

    flag = FALSE;
    inner = nd->u.monadic;
    if (inner->type == IROLinearOp1Arg && inner->nodetype == EBITFIELD) {
        // (2.6 reads the bit-field type once; Pro7's TYPE_BITFIELD(inner->rtype) twice loads it
        // late)
        tbitfield = TYPE_BITFIELD(inner->rtype);
        start_bit = tbitfield->offset + 8 * addend;
        end_bit = start_bit + tbitfield->bitlength - 1;
    } else {
        start_bit = 8 * addend;
        end_bit = start_bit + 8 * type->size - 1;
    }

    if (var->noregister && !copts.opt_pointer_analysis) {
        return FALSE;
    }

    if (nd->rtype && CParser_IsVolatile(nd->rtype, nd->nodeflags & ENODE_FLAG_QUALS)) {
        addend = -1;
    }
    if (is_volatile_object(var->object)) {
        addend = -1;
    }

    for (rp = IRO_FirstIndirectRecord; rp; rp = rp->next) {
        if (rp->var->index == var->index) {
            if (rp->flags & 1) {
                return FALSE;
            }

            if (addend == -1) {
                flag = TRUE;
                break;
            }

            if (IRO_TypesEqual(rp->type, type) &&
                rp->startbit ==
                start_bit &&
                rp->endbit == end_bit) {
                match = oalloc(sizeof(IndirectRecordMatch));
                match->nd = inner;
                match->otherMatches = NULL;
                if (!rp->matches) {
                    rp->matches = match;
                } else {
                    match->otherMatches = rp->matches;
                    rp->matches = match;
                }

                IRO_Dump(
                    "Exact Match of Type and bits at %d and %d\n", rp->linear->index, nd->index
                );
                IRO_Dump(
                    "Rp->startbit=%d,Rp->endbit=%d,start_bit=%d,end_bit=%d\n",
                    rp->startbit,
                    rp->endbit,
                    start_bit,
                    end_bit
                );
                return TRUE;
            }

            if (rp->startbit ==
                start_bit &&
                rp->endbit ==
                end_bit &&
                !IRO_TypesEqual(rp->type, type)) {
                IRO_Dump(
                    "match on bits but mismatch on type %d and %d\n", rp->linear->index, nd->index
                );
                IRO_Dump(
                    "Rp->startbit=%d,Rp->endbit=%d,start_bit=%d,end_bit=%d\n",
                    rp->startbit,
                    rp->endbit,
                    start_bit,
                    end_bit
                );
                flag = TRUE;
                break;
            }

            if (rp->startbit >= start_bit && end_bit >= rp->startbit) {
                IRO_Dump("Overlap detected --1 %d and %d\n", rp->linear->index, nd->index);
                IRO_Dump(
                    "Rp->startbit=%d,Rp->endbit=%d,start_bit=%d,end_bit=%d\n",
                    rp->startbit,
                    rp->endbit,
                    start_bit,
                    end_bit
                );
                flag = TRUE;
                break;
            }

            if (rp->startbit < start_bit && rp->endbit >= start_bit) {
                IRO_Dump("Overlap detected --2 %d and %d\n", rp->linear->index, nd->index);
                IRO_Dump(
                    "Rp->startbit=%d,Rp->endbit=%d,start_bit=%d,end_bit=%d\n",
                    rp->startbit,
                    rp->endbit,
                    start_bit,
                    end_bit
                );
                flag = TRUE;
                break;
            }
        }
    }

    if (flag) {
        DisableEntries(rp);
        if (addend >= 0) {
            return FALSE;
        }
    }

    IRO_Dump("Create Entry at %d\n", nd->index);
    IRO_Dump("start_bit=%d,end_bit=%d\n", start_bit, end_bit);

    rp = oalloc(sizeof(IndirectRecord));
    rp->linear = nd;
    rp->flags = 0;
    rp->var = var;
    rp->addend = addend;
    rp->size = type->size;
    rp->type = type;
    rp->next = NULL;
    rp->matches = oalloc(sizeof(IndirectRecordMatch));
    rp->matches->nd = inner;
    rp->matches->otherMatches = NULL;
    rp->startbit = start_bit;
    rp->endbit = end_bit;

    if (nd->type ==
        IROLinearOp1Arg &&
        nd->nodetype ==
        EINDIRECT &&
        nd->u.monadic->type ==
        IROLinearOp1Arg &&
        nd->u.monadic->nodetype == EBITFIELD) {
        rp->flags |= 2;
    }

    if (!(IS_TYPE_FLOAT(type) || IS_TYPE_INT(type) || IS_TYPE_POINTER_ONLY(type)) ||
        addend ==
        -1 ||
        ((inner->flags & IROLF_4000) && ObjectIsArg(var->object))) {
        rp->flags |= 1;
    }

    if (IRO_FirstIndirectRecord) {
        IRO_LastIndirectRecord->next = rp;
    } else {
        IRO_FirstIndirectRecord = rp;
    }
    IRO_LastIndirectRecord = rp;
    return TRUE;
}

static void DisableEntries(IndirectRecord* irec) {
    IndirectRecord* scan;

    for (scan = IRO_FirstIndirectRecord; scan; scan = scan->next) {
        if (irec->var->index == scan->var->index) {
            scan->flags |= 1;
        }
    }
}

static UInt32 ObjectIsArg(Object* obj) {
    ObjectList* scan;

    for (scan = arguments; scan; scan = scan->next) {
        if (scan->object == obj) {
            return TRUE;
        }
    }

    return FALSE;
}
