#include "compiler/CIRTransform.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInit.h"
#include "compiler/CMachine.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CScope.h"
#include "compiler/objects.h"
#include "compiler/types.h"
#include <string.h>

#pragma pack(push, 2)
typedef struct CIRTransTemp {
    struct CIRTransTemp* next;
    Object* object;
    Boolean flag;
} CIRTransTemp;

typedef struct MultiAccessOperand {
    Object* object;
    Object* tempobj;
    ENode* ass;
    Type* type;
    Type* bitfieldType;
} MultiAccessOperand;

// TODO: invented names (Pro7's decompilation: "no idea what this is for..."); only the dead
// CIRTrans_TransExpr's helpers take one, and nothing reads `unk`
typedef struct StrangeRuntimeFunction {
    Object* object;
    short unk;
    char name[1];
} StrangeRuntimeFunction;
#pragma pack(pop)

// 2.6's .data has them in this order (definition order); Pro7 declares rtfunc8 first
// clang-format off
static TypeFunc cirtrans_rtfunc2 = {
    TYPEFUNC, 0, NULL, NULL, TYPE(&stsignedshort), 0, 0
};
static TypeFunc cirtrans_rtfunc4 = {
    TYPEFUNC, 0, NULL, NULL, TYPE(&stunsignedlong), 0, 0
};
static TypeFunc cirtrans_rtfunc8 = {
    TYPEFUNC, 0, NULL, NULL, TYPE(&void_ptr), 0, 0
};
// clang-format on

static CIRTransTemp* cirtrans_temps;
// Pro7 defines modulo_generated, bigswitch_generated and alloca_called here. 2.6 has none of these
// COMMONs (defining them moves the COMMON tail), but its dead CIRTrans_TransExpr still sets
// alloca_called (its "__alloca" literal is in the unit's .data), so it was declared, not defined.
extern Boolean alloca_called;

// forward decls
static ENode* CIRTrans_TransExpr(ENode* expr, Boolean flag);

void CIRTrans_Setup(void) {}

void CIRTrans_Cleanup(void) {}

static Object* CIRTrans_GetRuntimeFunction(StrangeRuntimeFunction* rtfunc, Type* type) {
    Object* object;

    object = rtfunc->object;
    if (!object) {
        object = CParser_NewFunctionObject(NULL);
        rtfunc->object = object;

        object->nspace = cscope_root;
        object->name = GetHashNameNodeExport(rtfunc->name);
        object->flags = OBJECT_INTERNAL;

        if (type) {
            switch (type->size) {
                case 2:
                    object->type = TYPE(&cirtrans_rtfunc2);
                    break;
                case 4:
                    object->type = TYPE(&cirtrans_rtfunc4);
                    break;
                case 8:
                    object->type = TYPE(&cirtrans_rtfunc8);
                    break;
                default:
                    CError_FATAL(427);
            }
        } else {
            object->type = TYPE(&cirtrans_rtfunc8);
        }
    }

    return object;
}

static Object* CIRTrans_GetTemporary(Type* type) {
    CIRTransTemp* temp;

    for (temp = cirtrans_temps; temp; temp = temp->next) {
        if (temp->object->type->size == type->size && !temp->flag) {
            temp->flag = TRUE;
            return temp->object;
        }
    }

    temp = oalloc(sizeof(CIRTransTemp));
    temp->next = cirtrans_temps;
    cirtrans_temps = temp;

    temp->object = create_temp_object(type);
    temp->flag = TRUE;
    return temp->object;
}

static ENode* CIRTrans_CheckRuntimeAssign(ENode* expr) {
    ENode* inner;

    if (ENODE_IS(expr->data.diadic.right, EINDIRECT)) {
        inner = expr->data.diadic.right->data.monadic;
        if (ENODE_IS(inner, EFUNCCALL) &&
            (inner->flags & ENODE_FLAG_80) &&
            inner->data.funccall.args &&
            ENODE_IS(inner->data.funccall.args->node, EOBJREF)) {
            CError_ASSERT(502, ENODE_IS(expr->data.diadic.left, EINDIRECT));
            inner->data.funccall.args->node = expr->data.diadic.left->data.monadic;
            inner->flags &= ~ENODE_FLAG_80;
            return expr->data.diadic.right;
        }
    }

    return expr;
}

static void CIRTrans_SetupMultiAccessOperand(MultiAccessOperand* mop, ENode* expr) {
    memclrw(mop, sizeof(MultiAccessOperand));

    mop->type = expr->rtype;

    CError_ASSERT(527, ENODE_IS(expr, EINDIRECT));
    expr = expr->data.monadic;

    if (ENODE_IS(expr, EOBJREF)) {
        mop->object = expr->data.objref;
    } else {
        if (ENODE_IS(expr, EBITFIELD)) {
            mop->bitfieldType = expr->rtype;
            expr = expr->data.monadic;
        }
        expr->rtype = CDecl_NewPointerType(mop->type);
        mop->tempobj = create_temp_object(expr->rtype);
        mop->ass = makediadicnode(create_objectnode(mop->tempobj), expr, EASS);
    }
}

static ENode* CIRTrans_GetMultiAccessOperand(MultiAccessOperand* mop) {
    ENode* expr;

    if (mop->object == NULL) {
        expr = create_objectnode(mop->tempobj);
        if (mop->bitfieldType) {
            expr = makemonadicnode(expr, EBITFIELD);
            expr->rtype = mop->bitfieldType;
        }
        expr = makemonadicnode(expr, EINDIRECT);
    } else {
        expr = create_objectnode(mop->object);
    }

    expr->rtype = mop->type;
    return expr;
}

static ENode* CIRTrans_InitMultiAccessExpression(MultiAccessOperand* mop, ENode* expr) {
    if (mop->ass) {
        expr = makediadicnode(mop->ass, expr, ECOMMA);
        expr->rtype = expr->data.diadic.right->rtype;
    }
    return expr;
}

ENode* CIRTrans_TransformOpAss(ENode* expr) {
    ENodeType nt;
    ENode* expr2;
    MultiAccessOperand mop;

    if (!ENODE_IS(expr->data.diadic.left, EINDIRECT)) {
        CError_Error(ERR_NOT_LVALUE);
        return nullnode();
    }

    CIRTrans_SetupMultiAccessOperand(&mop, expr->data.diadic.left);

    switch (expr->type) {
        case EMULASS:
            nt = EMUL;
            break;
        case EDIVASS:
            nt = EDIV;
            break;
        case EMODASS:
            nt = EMODULO;
            break;
        case EADDASS:
            nt = EADD;
            break;
        case ESUBASS:
            nt = ESUB;
            break;
        case ESHLASS:
            nt = ESHL;
            break;
        case ESHRASS:
            nt = ESHR;
            break;
        case EANDASS:
            nt = EAND;
            break;
        case EXORASS:
            nt = EXOR;
            break;
        case EORASS:
            nt = EOR;
            break;
        default:
            CError_FATAL(627);
    }

    expr2 = CIRTrans_GetMultiAccessOperand(&mop);

    if (!IS_TYPE_POINTER_ONLY(expr2->rtype)) {
        expr2 = CExpr_NewDyadicNode(expr2, nt, expr->data.diadic.right);
        if (expr2->rtype != expr->data.diadic.left->rtype) {
            expr2 = makemonadicnode(expr2, ETYPCON);
            expr2->rtype = expr->data.diadic.left->rtype;
        }
    } else {
        expr2 = makediadicnode(expr2, expr->data.diadic.right, nt);
    }

    if (IS_TYPE_FLOAT(expr2->rtype)) {
        expr2 = CExpr_BinaryFloatExpression(expr2);
    }

    expr2 = makediadicnode(CIRTrans_GetMultiAccessOperand(&mop), expr2, EASS);
    return CIRTrans_InitMultiAccessExpression(&mop, expr2);
}

static void CIRTrans_TransIncDec() {
    // empty, never called
}

static ENode* CIRTrans_TransIntConst(ENode* expr) {
    Object* obj;
    UInt8 data[16];

    CMach_InitIntMem(expr->rtype, expr->data.intval, data);

    obj = CParser_NewGlobalDataObject(NULL);
    obj->name = CParser_GetUniqueName();
    obj->type = expr->rtype;
    obj->sclass = TK_STATIC;
    obj->datatype = DDATA;
    CScope_AddGlobalObject(obj);
    CInit_DeclareData(obj, data, NULL, obj->type->size);
    return create_objectnode(obj);
}

static ENode* CIRTrans_TransFloatConst(ENode* expr) {
    Object* obj;
    UInt8 data[16];

    CMach_InitFloatMem(expr->rtype, expr->data.floatval, data);

    obj = CParser_NewGlobalDataObject(NULL);
    obj->name = CParser_GetUniqueName();
    obj->type = expr->rtype;
    obj->sclass = TK_STATIC;
    obj->datatype = DDATA;
    CScope_AddGlobalObject(obj);
    CInit_DeclareData(obj, data, NULL, obj->type->size);
    return create_objectnode(obj);
}

static ENode* CIRTrans_TransUnary(ENode* expr, Type* type, StrangeRuntimeFunction* rtfunc) {
    if (type->size > 4) {
        expr = funccallexpr(
            CIRTrans_GetRuntimeFunction(rtfunc, type),
            create_objectrefnode(CIRTrans_GetTemporary(type)),
            expr,
            NULL,
            NULL
        );
        expr->flags |= ENODE_FLAG_80;
        expr = makemonadicnode(expr, EINDIRECT);
        expr->rtype = type;
        return expr;
    } else {
        expr = funccallexpr(CIRTrans_GetRuntimeFunction(rtfunc, type), expr, NULL, NULL, NULL);
        expr->rtype = type;
        return expr;
    }
}

static ENode* CIRTrans_TransBinary(ENode* expr, StrangeRuntimeFunction* rtfunc) {
    ENode* expr2;

    if (expr->rtype->size > 4) {
        expr2 = funccallexpr(
            CIRTrans_GetRuntimeFunction(rtfunc, expr->rtype),
            create_objectrefnode(CIRTrans_GetTemporary(expr->rtype)),
            expr->data.diadic.left,
            expr->data.diadic.right,
            NULL
        );
        expr2->flags |= ENODE_FLAG_80;
        expr2 = makemonadicnode(expr2, EINDIRECT);
        expr2->rtype = expr->rtype;
        return expr2;
    } else {
        expr2 = funccallexpr(
            CIRTrans_GetRuntimeFunction(rtfunc, expr->rtype),
            expr->data.diadic.left,
            expr->data.diadic.right,
            NULL,
            NULL
        );
        expr2->rtype = expr->rtype;
        return expr2;
    }
}

static ENodeList* CIRTrans_TransExprList(ENodeList* list) {
    ENodeList* scan;

    for (scan = list; scan; scan = scan->next) {
        scan->node = CIRTrans_TransExpr(scan->node, TRUE);
    }

    return list;
}

static ENode* CIRTrans_TransExpr(ENode* expr, Boolean flag) {
    switch (expr->type) {
        case EINDIRECT:
        case EFORCELOAD:
        case EBITFIELD:
            expr->data.monadic = CIRTrans_TransExpr(expr->data.monadic, flag);
            break;
        case EPOSTINC:
            expr->data.monadic = CIRTrans_TransExpr(expr->data.monadic, TRUE);
            break;
        case EPOSTDEC:
            expr->data.monadic = CIRTrans_TransExpr(expr->data.monadic, TRUE);
            break;
        case EPREINC:
            expr->data.monadic = CIRTrans_TransExpr(expr->data.monadic, TRUE);
            break;
        case EPREDEC:
            expr->data.monadic = CIRTrans_TransExpr(expr->data.monadic, TRUE);
            break;
        case ETYPCON:
            expr->data.monadic = CIRTrans_TransExpr(expr->data.monadic, flag);
            if (!flag) {
                return expr->data.monadic;
            }
            break;
        case EBINNOT:
            expr->data.monadic = CIRTrans_TransExpr(expr->data.monadic, flag);
            break;
        case ELOGNOT:
            expr->data.monadic = CIRTrans_TransExpr(expr->data.monadic, flag);
            break;
        case EMONMIN:
            expr->data.monadic = CIRTrans_TransExpr(expr->data.monadic, flag);
            break;
        case EADD:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, flag);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, flag);
            break;
        case ESUB:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, flag);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, flag);
            break;
        case EMUL:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, flag);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, flag);
        case EDIV:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, flag);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, flag);
            break;
        case EMODULO:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, flag);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, flag);
            break;
        case ESHL:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, flag);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, flag);
            break;
        case ESHR:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, flag);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, flag);
            break;
        case EROTL:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, flag);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, flag);
            break;
        case EROTR:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, flag);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, flag);
            break;
        case EAND:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, flag);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, flag);
            break;
        case EXOR:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, flag);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, flag);
            break;
        case EOR:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, flag);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, flag);
            break;
        case ELESS:
        case EGREATER:
        case ELESSEQU:
        case EGREATEREQU:
        case EEQU:
        case ENOTEQU:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, flag);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, flag);
            if (!flag) {
                expr->type = ECOMMA;
                expr->rtype = expr->data.diadic.right->rtype;
                return expr;
            }
            break;
        case ELAND:
        case ELOR:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, TRUE);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, flag);
            break;
        case EMULV:
        case EADDV:
        case ESUBV:
        case EPMODULO:
        case EBCLR:
        case EBTST:
        case EBSET:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, flag);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, flag);
            break;
        case EASS:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, TRUE);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, TRUE);
            break;
        case EMULASS:
        case EDIVASS:
        case EADDASS:
        case ESUBASS:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, TRUE);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, TRUE);
            break;
        case EMODASS:
        case ESHLASS:
        case ESHRASS:
        case EANDASS:
        case EXORASS:
        case EORASS:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, TRUE);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, TRUE);
            break;
        case ECOMMA:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, FALSE);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, flag);
            break;
        case ECOND:
            expr->data.cond.cond = CIRTrans_TransExpr(expr->data.cond.cond, TRUE);
            expr->data.cond.expr1 = CIRTrans_TransExpr(expr->data.cond.expr1, TRUE);
            expr->data.cond.expr2 = CIRTrans_TransExpr(expr->data.cond.expr2, TRUE);
            break;
        case EMFPOINTER:
            expr->data.mfpointer.accessnode =
                CIRTrans_TransExpr(expr->data.mfpointer.accessnode, flag);
            expr->data.mfpointer.mfpointer =
                CIRTrans_TransExpr(expr->data.mfpointer.mfpointer, flag);
            break;
        case EFUNCCALL:
        case EFUNCCALLP:
            if (ENODE_IS(expr->data.funccall.funcref, EOBJREF) &&
                !strcmp(expr->data.funccall.funcref->data.objref->name->name, "__alloca")) {
                alloca_called = TRUE;
            }
            expr->data.funccall.funcref = CIRTrans_TransExpr(expr->data.funccall.funcref, TRUE);
            expr->data.funccall.args = CIRTrans_TransExprList(expr->data.funccall.args);
            break;
        case ENULLCHECK:
            expr->data.nullcheck.nullcheckexpr =
                CIRTrans_TransExpr(expr->data.nullcheck.nullcheckexpr, TRUE);
            expr->data.nullcheck.condexpr = CIRTrans_TransExpr(expr->data.nullcheck.condexpr, TRUE);
            break;
        case ENEWEXCEPTION:
        case ENEWEXCEPTIONARRAY:
            expr->data.newexception.initexpr =
                CIRTrans_TransExpr(expr->data.newexception.initexpr, TRUE);
            expr->data.newexception.tryexpr =
                CIRTrans_TransExpr(expr->data.newexception.tryexpr, TRUE);
            break;
        case EINITTRYCATCH:
            expr->data.itc.initexpr = CIRTrans_TransExpr(expr->data.itc.initexpr, TRUE);
            expr->data.itc.tryexpr = CIRTrans_TransExpr(expr->data.itc.tryexpr, TRUE);
            expr->data.itc.catchexpr = CIRTrans_TransExpr(expr->data.itc.catchexpr, TRUE);
            expr->data.itc.result = CIRTrans_TransExpr(expr->data.itc.result, TRUE);
            break;
        case EINTCONST:
        case EFLOATCONST:
        case ESTRINGCONST:
        case EOBJREF:
        case EPRECOMP:
        case ETEMP:
        case ELABEL:
        case EMEMBER:
        case EINSTRUCTION:
        case EVECTOR128CONST:
            break;
        default:
            CError_FATAL(1947);
    }

    return expr;
}

void CIRTrans_Transform(void) {
    cirtrans_temps = NULL;
}
