// COptimizer.c: Pro7's file without its statement-level CSE (BasicBlockAnalyze and the rest), which
// 2.6 lacks: no 2.6 function calls it and its jump tables are not in 2.6's .data. The CSE's statics
// stay, as 2.6's .bss has them.
#include "compiler/COptimizer.h"
#include "compiler/IrOptimizer.h"
#include "compiler/CError.h"
#include "compiler/CException.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInt64.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/enode.h"
#include "compiler/Exceptions.h"
#include "compiler/InlineAsm.h"
#include "compiler/objects.h"
#include "compiler/Registers.h"
#include "compiler/Switch.h"
#include "compiler/types.h"

// The dead CSE's types (Pro7 defines them in COptimizer.h): only its statics' pointers remain
typedef struct COptBlock COptBlock;
typedef struct COptCSE COptCSE;
typedef struct COptCSEList COptCSEList;

Boolean copt_isleaffunction;
// (Pro7's, but setbytes is declared second and objrefnode and objrefnodes are gone, as the .bss
// says; the CSE's are used by no function 2.6 keeps)
static Boolean stmtchanged;
static short setbytes;
static COptBlock* currentblock;
static ENode* mexpr;
static COptCSE* csenodes[MAXEXPR];
static COptCSEList* cselist;
static short extravars;
static Boolean cse_found;
static Boolean cse_invals;
static Boolean static_for_inlines;
static short replaces;

// (the dead CSE's set-bit table: unused, but 2.6's .data starts with it)
// clang-format off
static short bitmasks[] = {
    1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80,
    0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x8000
};
// clang-format on

static CLabel* finallabel(CLabel* label, Statement* stmt) {
    Statement* scan;

    for (scan = label->stmt; scan; scan = scan->next) {
        if (scan->type > ST_LABEL) {
            if (scan == stmt) {
                return label;
            }
            if (scan->type == ST_GOTO) {
                if (scan->label != label) {
                    stmtchanged = 1;
                }
                return scan->label;
            }
            return label;
        }
    }

    return label;
}

static void optimizegoto(Statement* stmt) {
    Statement* scan;

    for (scan = stmt->next; scan; scan = scan->next) {
        if (stmt->label->stmt == scan) {
            stmt->type = ST_NOP;
            stmtchanged = 1;
            return;
        }
        if (scan->type > ST_LABEL) {
            break;
        }
    }

    stmt->label = finallabel(stmt->label, stmt);
}

static void removeif(Statement* stmt, Boolean flag) {
    if (((stmt->type == ST_IFGOTO) && flag) || ((stmt->type == ST_IFNGOTO) && !flag)) {
        Statement* copy = lalloc(sizeof(Statement));
        *copy = *stmt;
        copy->type = ST_GOTO;
        stmt->next = copy;
    }
    stmt->type = ST_EXPRESSION;
    stmtchanged = 1;
}

static void optimizeif(Statement* stmt) {
    Statement* scan;
    Statement* scan2;
    Boolean flag;

    if (iszero(stmt->expr)) {
        removeif(stmt, 0);
        return;
    }
    if (isnotzero(stmt->expr)) {
        removeif(stmt, 1);
        return;
    }

    for (scan = stmt->next, flag = 0; scan; scan = scan->next) {
        if (scan->type > ST_LABEL) {
            if (scan->type == ST_GOTO) {
                if (scan->label == stmt->label) {
                    stmt->type = ST_EXPRESSION;
                    stmtchanged = 1;
                    return;
                }

                if (!flag) {
                    // (2.6 walks on with scan and keeps the goto in scan2, where Pro7 walks with scan2;
                    // the outer loop ends after either)
                    scan2 = scan;
                    for (scan = scan->next; scan; scan = scan->next) {
                        if (scan->type > ST_LABEL) {
                            break;
                        }
                        if (stmt->label->stmt == scan) {
                            stmt->label = scan2->label;
                            scan2->type = ST_NOP;
                            if (stmt->type == ST_IFGOTO) {
                                stmt->type = ST_IFNGOTO;
                            } else {
                                stmt->type = ST_IFGOTO;
                            }
                            stmtchanged = 1;
                            stmt->label = finallabel(stmt->label, stmt);
                            return;
                        }
                    }
                }
            } else if (scan->type == ST_RETURN && !scan->expr && !static_for_inlines && !flag) {
                scan2 = scan;
                for (scan = scan->next; scan; scan = scan->next) {
                    if (scan->type > ST_LABEL) {
                        break;
                    }
                    if (stmt->label->stmt == scan) {
                        stmt->label = cleanreturnlabel;
                        needs_cleanup = 1;
                        scan2->type = ST_NOP;
                        if (stmt->type == ST_IFGOTO) {
                            stmt->type = ST_IFNGOTO;
                        } else {
                            stmt->type = ST_IFGOTO;
                        }
                        stmtchanged = 1;
                        return;
                    }
                }
            }
            break;
        }

        if (scan->type == ST_LABEL) {
            flag = 1;
        }

        if (stmt->label->stmt == scan) {
            stmt->type = ST_EXPRESSION;
            stmtchanged = 1;
            return;
        }
    }

    stmt->label = finallabel(stmt->label, stmt);
}

static void optimizeswitch(Statement* stmt) {
    SwitchInfo* info;
    SwitchCase* swcase;

    info = (SwitchInfo*)stmt->label;
    CError_ASSERT(1746, info && info->cases && info->defaultlabel);

    info->defaultlabel = finallabel(info->defaultlabel, stmt);
    for (swcase = info->cases; swcase; swcase = swcase->next) {
        swcase->label = finallabel(swcase->label, stmt);
    }

    if (ENODE_IS(stmt->expr, EINTCONST)) {
        for (swcase = info->cases; swcase; swcase = swcase->next) {
            // (2.6's cases are single values; Pro7's are ranges)
            if (CInt64_Equal(swcase->min, stmt->expr->data.intval)) {
                break;
            }
        }

        stmt->type = ST_GOTO;
        stmt->label = swcase ? swcase->label : info->defaultlabel;
    }
}

static void removeunusedlabels(Statement* stmt) {
    Statement* scan;
    CLabel* label;
    SwitchCase* swcase;
    ExceptionAction* action;

    for (scan = stmt; scan; scan = scan->next) {
        scan->marked = 0;
    }

    for (scan = stmt; scan; scan = scan->next) {
        switch (scan->type) {
            case ST_GOTO:
            case ST_IFGOTO:
            case ST_IFNGOTO:
            case ST_OVF:
                if (scan->label->stmt) {
                    scan->label->stmt->marked = 1;
                }
                break;
            case ST_SWITCH:
                ((SwitchInfo*)scan->label)->defaultlabel->stmt->marked = 1;
                for (swcase = ((SwitchInfo*)scan->label)->cases; swcase; swcase = swcase->next) {
                    swcase->label->stmt->marked = 1;
                }
                break;
            case ST_ASM:
                if ((label = InlineAsm_GetReferencedLabel(scan))) {
                    label->stmt->marked = 1;
                }
                if ((label = InlineAsm_GetReferencedLabel2(scan))) {
                    label->stmt->marked = 1;
                }
                break;
            case ST_BEGINLOOP:
                ((LoopInfo*)scan->expr)->stmt->marked = 1;
                break;
            case ST_ENDLOOP:
                if (scan->next->type == ST_GOTO && scan->next->next->type == ST_LABEL) {
                    scan->next->next->marked = 1;
                }
                break;
            default:
                for (action = scan->dobjstack; action; action = action->prev) {
                    if (action->type == EAT_CATCHBLOCK) {
                        action->data.catch_block.catch_label->stmt->marked = 1;
                        action->data.catch_block.catch_label->stmt->flags |= StmtFlag_1;
                    } else if (action->type == EAT_SPECIFICATION) {
                        action->data.specification.unexp_label->stmt->marked = 1;
                        action->data.specification.unexp_label->stmt->flags |= StmtFlag_1;
                    }
                }
        }
    }

    for (scan = stmt; scan; scan = scan->next) {
        if (scan->type == ST_LABEL && !scan->marked && !(scan->flags & StmtFlag_1)) {
            scan->type = ST_NOP;
            stmtchanged = 1;
        }
    }
}

static void optimizebranches(Statement* stmt) {
    Statement* scan;

    removeunusedlabels(stmt);
    for (scan = stmt; scan; scan = scan->next) {
        switch (scan->type) {
            case ST_GOTO:
                optimizegoto(scan);
                break;
            case ST_IFGOTO:
            case ST_IFNGOTO:
                optimizeif(scan);
                break;
            case ST_SWITCH:
                optimizeswitch(scan);
                break;
        }
    }
}

void SetVarUsage(Object* obj, Boolean noregister) {
    VarInfo* vi;

    CError_ASSERT(1875, obj->datatype != DALIAS);

    // (Pro7 also counts DNONLAZYPTR objects, which are Mach-O's)
    if (obj->datatype != DLOCAL) {
        return;
    }

    vi = obj->u.var.info;
    vi->used = 1;
    if (!copts.globaloptimizer) {
        if (copts.optimizesize) {
            vi->usage++;
        } else {
            vi->usage += curstmtvalue;
        }
    }

    if (noregister) {
        vi->noregister = 1;
    }
}

static void checkexpression(ENode* expr) {
    ENodeList* list;

    while (1) {
        switch (expr->type) {
            case EOBJREF:
                SetVarUsage(expr->data.objref, 1);
                return;
            case EINDIRECT:
                if (ENODE_IS(expr->data.monadic, EOBJREF)) {
                    SetVarUsage(expr->data.monadic->data.objref, 0);
                    return;
                }
                expr = expr->data.monadic;
                break;
            case EFUNCCALL:
            case EFUNCCALLP:
                copt_isleaffunction = 0;
                checkexpression(expr->data.funccall.funcref);
                for (list = expr->data.funccall.args; list; list = list->next) {
                    checkexpression(list->node);
                }
                return;
            case ECOND:
            case ECONDASS:
                checkexpression(expr->data.cond.cond);
                checkexpression(expr->data.cond.expr1);
                checkexpression(expr->data.cond.expr2);
                return;
            case EMUL:
            case EMULV:
            case EDIV:
            case EMODULO:
            case EADDV:
            case ESUBV:
            case EADD:
            case ESUB:
            case ESHL:
            case ESHR:
            case ELESS:
            case EGREATER:
            case ELESSEQU:
            case EGREATEREQU:
            case EEQU:
            case ENOTEQU:
            case EAND:
            case EXOR:
            case EOR:
            case ELAND:
            case ELOR:
            case EASS:
            case EMULASS:
            case EDIVASS:
            case EMODASS:
            case EADDASS:
            case ESUBASS:
            case ESHLASS:
            case ESHRASS:
            case EANDASS:
            case EXORASS:
            case EORASS:
            case ECOMMA:
            case EPMODULO:
            case EROTL:
            case EROTR:
            case EBCLR:
            case EBTST:
            case EBSET:
                checkexpression(expr->data.diadic.left);
                expr = expr->data.diadic.right;
                break;
            case EPOSTINC:
            case EPOSTDEC:
            case EPREINC:
            case EPREDEC:
            case EMONMIN:
            case EBINNOT:
            case ELOGNOT:
            case EFORCELOAD:
            case ETYPCON:
            case EBITFIELD:
                expr = expr->data.monadic;
                break;
            case ENULLCHECK:
                checkexpression(expr->data.nullcheck.nullcheckexpr);
                expr = expr->data.nullcheck.condexpr;
                break;
            case EINTCONST:
            case EFLOATCONST:
            case EPRECOMP:
            case ELABEL:
            case EVECTOR128CONST:
                return;
            case ESTRINGCONST:
                return;
            default:
                CError_FATAL(1998);
        }
    }
}

static void checklocalusage(Statement* stmt) {
    Statement* scan;

    for (scan = stmt; scan; scan = scan->next) {
        if (scan->type >= ST_EXPRESSION && scan->type <= ST_GOTOEXPR && scan->expr) {
            curstmtvalue = scan->value;
            checkexpression(scan->expr);
        } else if (scan->type == ST_ASM) {
            curstmtvalue = scan->value;
            InlineAsm_CheckLocalUsage(scan);
        }
    }
}

static void colorcode(Statement* stmt) {
    CLabel* label;
    SwitchCase* swcase;

    while (stmt && !stmt->marked) {
        stmt->marked = 1;
        switch (stmt->type) {
            case ST_GOTOEXPR:
                return;
            case ST_IFGOTO:
            case ST_IFNGOTO:
            case ST_OVF:
                colorcode(stmt->label->stmt);
                break;
            case ST_GOTO:
            case ST_EXIT:
                stmt = stmt->label->stmt;
                continue;
            case ST_RETURN:
                return;
            case ST_SWITCH:
                for (swcase = ((SwitchInfo*)stmt->label)->cases; swcase; swcase = swcase->next) {
                    colorcode(swcase->label->stmt);
                }
                stmt = ((SwitchInfo*)stmt->label)->defaultlabel->stmt;
                continue;
            case ST_ASM:
                if ((label = InlineAsm_GetReferencedLabel(stmt))) {
                    colorcode(label->stmt);
                }
                if ((label = InlineAsm_GetReferencedLabel2(stmt))) {
                    colorcode(label->stmt);
                }
                break;
            case ST_ENDLOOP:
                if (stmt->next && stmt->next->type == ST_GOTO) {
                    stmt = stmt->next;
                    stmt->marked = 1;
                }
                break;
            case ST_NOP:
            case ST_LABEL:
            case ST_EXPRESSION:
            case ST_ENTRY:
            case ST_BEGINCATCH:
            case ST_ENDCATCH:
            case ST_ENDCATCHDTOR:
            case ST_BEGINLOOP:
                break;
            default:
                CError_FATAL(2096);
        }
        stmt = stmt->next;
    }
}

static void removeunusedcode(Statement* stmt) {
    Statement* scan;

    for (scan = stmt; scan; scan = scan->next) {
        scan->marked = 0;
    }

    colorcode(stmt);
    for (scan = stmt; scan; scan = scan->next) {
        if (!scan->marked && (scan->flags & StmtFlag_1)) {
            colorcode(scan);
        }
    }

    for (scan = stmt; scan; scan = scan->next) {
        if (!scan->marked && scan->type != ST_NOP) {
            scan->type = ST_NOP;
            stmtchanged = 1;
        }
    }
}

static void COpt_ReturnCheck(Object* obj, Statement* stmt) {
    Statement* scan;

    if ((copts.pedantic || copts.cplusplus) && obj && TYPE_FUNC(obj->type)->functype != &stvoid) {
        // (a copy of the parameter where Pro7 walks stmt itself: walking the parameter changes the
        // registers)
        scan = stmt;
        while (scan) {
            if (!scan->next && scan->type != ST_GOTO && scan->type != ST_RETURN) {
                CError_Warning(ERR_RETURN_VALUE_EXPECTED);
                break;
            }

            if (scan->type == ST_RETURN && !scan->expr && !(scan->flags & StmtFlag_8)) {
                CError_Warning(ERR_RETURN_VALUE_EXPECTED);
                break;
            }

            scan = scan->next;
        }
    }
}

static void COpt_Optimize(Object* obj, Statement* stmt) {
    Statement* scan;
    Statement** ptr;

    do {
        stmtchanged = 0;
        optimizebranches(stmt->next);
        removeunusedcode(stmt->next);
    } while (stmtchanged);

    ptr = &stmt->next;
    while ((scan = *ptr)) {
        if (scan->type == ST_NOP) {
            *ptr = scan->next;
        } else {
            ptr = &scan->next;
        }
    }
}

static void COpt_ELABELCallBack(ENode* expr) {
    CError_ASSERT(2195, expr->data.label->stmt && expr->data.label->stmt->type == ST_LABEL);
    expr->data.label->stmt->flags |= StmtFlag_1;
}

void COpt_SimpleOptimizer(Object* obj, Statement* stmt) {
    Statement* scan;

    for (scan = stmt; scan; scan = scan->next) {
        if ((scan->type >= ST_EXPRESSION && scan->type <= ST_GOTOEXPR) && scan->expr) {
            CExpr_SearchExprTree(scan->expr, COpt_ELABELCallBack, 1, ELABEL);
        }
    }

    static_for_inlines = 1;
    cleanreturnlabel = NULL;
    COpt_Optimize(obj, stmt);
    COpt_ReturnCheck(obj, stmt);
}

Statement* COpt_Optimizer(Object* obj, Statement* stmt) {
    copt_isleaffunction = 1;
    if (copts.globaloptimizer) {
        stmt = IRO_Optimizer(obj, stmt);
    }

    static_for_inlines = 0;
    COpt_Optimize(obj, stmt);

    if (obj && !(obj->qual & Q_INLINE)) {
        COpt_ReturnCheck(obj, stmt);
    }

    checklocalusage(stmt->next);
    return stmt;
}
