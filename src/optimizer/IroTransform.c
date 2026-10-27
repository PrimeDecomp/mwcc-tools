#define CERROR_FILE "unknown.c"
#include "compiler/common.h"
#include "compiler/IroTransform.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/types.h"
#include "compiler/CError.h"
#include "compiler/CExpr2.h"
#include "compiler/CFunc.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CParser.h"
#include "compiler/CompilerTools.h"
#include "compiler/IrOptimizer.h"
#include "compiler/IroDump.h"
#include "compiler/IroLoop.h"
#include "compiler/IroUtil.h"
#include "compiler/IroVars.h"
#ifdef __wasm__
/* The original reads a type's `integral` whatever its kind. A plain Type (void, a label, a template-dependent
   expression, the illegal type) has none: the original reads the padding after it, 0, and the wasm build's next
   object. */
static UInt8 IroTransform_UncheckedIntegral(Type *type)
{
    switch ((UInt8)type->type) {
        case TYPEVOID:
        case TYPELABEL:
        case TYPETEMPLDEPEXPR:
        case 0xFF:
            return 0;
        default:
            return TYPE_INTEGRAL(type)->integral;
    }
}
#define UNCHECKED_INTEGRAL(ty) IroTransform_UncheckedIntegral(ty)
#else
#define UNCHECKED_INTEGRAL(ty) TYPE_INTEGRAL(ty)->integral
#endif

struct ENodeList *eadd_terms;
ENodeList *eadd_terms_tail;

/* Links in the expression work list. */

#if VERSION >= VERSION_GC_1_3
/* (1.3: these two are the tables the program fills in at startup, not constants) */
extern UInt8 nodetype_map[];
extern UInt8 data_00552d8c[];
#else
/* The compound assignment of each binary operator; 0 for none. */
static UInt8 nodetype_map[68] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x00, 0x20, 0x21, 0x00, 0x00, 0x22, 0x23,
    0x24, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x26, 0x27, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* The opposite of each operator (add and subtract, less and greater, and so on); 0x4B for none. */
static UInt8 data_00552d8c[68] = {
    0x01, 0x00, 0x03, 0x02, 0x4B, 0x05, 0x06, 0x07, 0x4B, 0x0B, 0x4B, 0x09, 0x4B, 0x0E, 0x0D, 0x10, 0x0F,
    0x12, 0x11, 0x14, 0x13, 0x16, 0x15, 0x18, 0x17, 0x1B, 0x4B, 0x19, 0x1D, 0x1C, 0x4B, 0x20, 0x1F, 0x4B,
    0x23, 0x22, 0x25, 0x24, 0x28, 0x4B, 0x26, 0x4B, 0x4B, 0x2C, 0x2B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B,
    0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B,
};
#endif

/* The operator of each comparison or logical operator's negation; 0x4B for none. */
static UInt8 data_00552dd0[68] = {
    0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x07, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B,
    0x4B, 0x4B, 0x16, 0x15, 0x14, 0x13, 0x18, 0x17, 0x4B, 0x4B, 0x4B, 0x1D, 0x1C, 0x4B, 0x4B, 0x4B, 0x4B,
    0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B,
    0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B,
};
typedef enum { MutEnumByte_one = 1 } MutEnumByte;

static void fix26(IROLinear *a0)
{
    IROLinear *x = a0->u.diadic.left;
    IROLinear *y = a0->u.diadic.right;
    if (x->rtype == a0->rtype) {
        IroUtil_ReplaceNextReference(a0, x);
        x->flags = a0->flags;
        x->nodeflags = a0->nodeflags;
        a0->type = IROLinearNop;
        IroUtil_ClearZeroOperands(y);
        return;
    } else {
        a0->type = IROLinearOp1Arg;
        a0->nodetype = ETYPCON;
        a0->u.monadic = x;
        IroUtil_ClearZeroOperands(y);
    }
}

static void fix30(IROLinear *a0)
{
    IROLinear *y = a0->u.diadic.left;
    IROLinear *x = a0->u.diadic.right;
    if (x->rtype == a0->rtype) {
        IroUtil_ReplaceNextReference(a0, x);
        x->flags = a0->flags;
        x->nodeflags = a0->nodeflags;
        a0->type = IROLinearNop;
        IroUtil_ClearZeroOperands(y);
        return;
    } else {
        a0->type = IROLinearOp1Arg;
        a0->nodetype = ETYPCON;
        a0->u.monadic = x;
        IroUtil_ClearZeroOperands(y);
    }
}

static inline UInt16 isTrueConstant(IROLinear *node)
{
    return fn_0044cad0(node);
}

static inline UInt16 isFalseConstant(IROLinear *node)
{
    return IroUtil_IsZeroConstant(node);
}

static inline void recordSimplification(IROLinear *node)
{
#if VERSION < VERSION_GC_1_3
    IroDump_Print("ReplaceExprWithLeftChild: expr= %d\n", node->index);
#endif
}

static void ReplaceExprWithLeftChild(IROLinear *expr)
{
    IROLinear *left = expr->u.diadic.left;
    IROLinear *right = expr->u.diadic.right;

    if (left->rtype == expr->rtype) {
        IroUtil_ReplaceNextReference(expr, left);
        left->flags = expr->flags;
        left->nodeflags = expr->nodeflags;
        expr->type = IROLinearNop;
        IroUtil_ClearZeroOperands(right);
    } else {
        expr->type = IROLinearOp1Arg;
        expr->nodetype = ETYPCON;
        expr->u.monadic = left;
        IroUtil_ClearZeroOperands(right);
    }
#if VERSION < VERSION_GC_1_3
    IroDump_Print("ReplaceExprWithLeftChild: expr= %d\n", expr->index);
#endif
}

static void ReplaceExprWithRightChild(IROLinear *expr)
{
    IROLinear *left = expr->u.diadic.left;
    IROLinear *right = expr->u.diadic.right;

    if (right->rtype == expr->rtype) {
        IroUtil_ReplaceNextReference(expr, right);
        right->flags = expr->flags;
        right->nodeflags = expr->nodeflags;
        expr->type = IROLinearNop;
        IroUtil_ClearZeroOperands(left);
    } else {
        expr->type = IROLinearOp1Arg;
        expr->nodetype = ETYPCON;
        expr->u.monadic = right;
        IroUtil_ClearZeroOperands(left);
    }
}

static void RemoveCommonOpLeftLeft(IROLinear *expr)
{
    rotate_left_child(expr, 0);
    ReplaceExprWithRightChild(expr->u.diadic.right);
#if VERSION < VERSION_GC_1_3
    IroDump_Print("remove common op(left-left) at: %d\n", expr->index);
#endif
}

static void RemoveCommonOpRightLeft(IROLinear *expr)
{
    rotate_left_child(expr, 1);
    ReplaceExprWithRightChild(expr->u.diadic.right);
#if VERSION < VERSION_GC_1_3
    IroDump_Print("remove common op(right-left) at: %d\n", expr->index);
#endif
}

static void RemoveCommonOpLeftRight(IROLinear *expr)
{
    rotate_left_child(expr, 0);
    ReplaceExprWithLeftChild(expr->u.diadic.right);
#if VERSION < VERSION_GC_1_3
    IroDump_Print("remove common op(right-right) at: %d\n", expr->index);
#endif
}

static void RemoveCommonOpRightRight(IROLinear *expr)
{
    rotate_left_child(expr, 1);
    ReplaceExprWithLeftChild(expr->u.diadic.right);
#if VERSION < VERSION_GC_1_3
    IroDump_Print("remove common op(right-right) at: %d\n", expr->index);
#endif
}

static void TransformExpr(IROLinear *nd)
{
    simplify_diadic_nodes(nd);
    simplify_diadic_constants(nd);
    simplify_same_linears(nd);
    simplify_matching_monadic_operands(nd);
    simplify_diadic_with_monadic_operand(nd);
    simplify_diadic_matching_child(nd);
    remove_common_op(nd);
    fn_004507d0(nd);
}

#if VERSION >= VERSION_GC_1_3
/* the reference's ReplaceExprWithConst */
static void replace_const(IROLinear *node, CInt64 value)
{
    IroUtil_ClearZeroOperands(node);
    node->type = IROLinearOperand;
    node->nodetype = EINTCONST;
    node->u.node = IrOptimizer_NewENode(0x32);
    node->u.node->data.intval = value;
    node->u.node->flags = node->nodeflags;
    node->u.node->rtype = node->rtype;
    if (node->rtype->type == TYPEFLOAT) {
        node->nodetype = node->u.node->type = EFLOATCONST;
        node->u.node->data.floatval = CMach_CalcFloatConvertFromInt((Type *)&stsignedlong, value);
    }
}
#define REPLACE_CONST(node, value) replace_const(node, value)
#else
static void replace_const(IROLinear *node, SInt32 hi, UInt32 lo)
{
    CInt64 value;
    value.hi = hi;
    value.lo = lo;
    IroUtil_ClearZeroOperands(node);
    node->type = IROLinearOperand;
    node->nodetype = EINTCONST;
    node->u.node = IrOptimizer_NewENode(0x32);
    node->u.node->data.intval = value;
    node->u.node->flags = node->nodeflags;
    node->u.node->rtype = node->rtype;
    if (node->rtype->type == TYPEFLOAT) {
        node->nodetype = node->u.node->type = EFLOATCONST;
        node->u.node->data.floatval = CMach_CalcFloatConvertFromInt((Type *)&stsignedlong, value);
    }
#if VERSION < VERSION_GC_1_3
    IroDump_Print("ReplaceExprWithConst: expr= %d,\tconst= %d\n", node->index, value.lo);
#endif
}
#define REPLACE_CONST(node, value) replace_const(node, value.hi, value.lo)
#endif

static void replace_left(IROLinear *node)
{
    IROLinear *child = node->u.diadic.left;
    IROLinear *other = node->u.diadic.right;
    if (child->rtype == node->rtype) {
        IroUtil_ReplaceNextReference(node, child);
        child->flags = node->flags;
        child->nodeflags = node->nodeflags;
        node->type = IROLinearNop;
        IroUtil_ClearZeroOperands(other);
    } else {
        node->type = IROLinearOp1Arg;
        node->nodetype = ETYPCON;
        node->u.monadic = child;
        IroUtil_ClearZeroOperands(other);
    }
#if VERSION < VERSION_GC_1_3
    IroDump_Print("ReplaceExprWithLeftChild: expr= %d\n", node->index);
#endif
}

static void replace_right(IROLinear *node)
{
    IROLinear *other = node->u.diadic.left;
    IROLinear *child = node->u.diadic.right;
    if (child->rtype == node->rtype) {
        IroUtil_ReplaceNextReference(node, child);
        child->flags = node->flags;
        child->nodeflags = node->nodeflags;
        node->type = IROLinearNop;
        IroUtil_ClearZeroOperands(other);
    } else {
        node->type = IROLinearOp1Arg;
        node->nodetype = ETYPCON;
        node->u.monadic = child;
        IroUtil_ClearZeroOperands(other);
    }
}

static void swapkids(IROLinear *node)
{
    IROLinear *t = node->u.diadic.left;
    node->u.diadic.left = node->u.diadic.right;
    node->u.diadic.right = t;
}

static inline void copyExpressionMetadata(IROLinear *dest, const IROLinear *src)
{
    dest->rtype = src->rtype;
    dest->flags = src->flags;
    dest->nodeflags = src->nodeflags;
}

static void CopyTypeInfo(IROLinear *dst, IROLinear *src)
{
    dst->rtype = src->rtype;
    dst->flags = src->flags;
    dst->nodeflags = src->nodeflags;
}

#if VERSION >= VERSION_GC_1_3
void fn_0045b490(IROLinear *nd);
extern Type *get_unsigned_type(Type *type);
#endif

static inline void set_shift_constant(IROLinear *operand, int shift)
{
    CInt64_SetLong(&operand->u.node->data.intval, shift);
}

#if VERSION >= VERSION_GC_1_3
/* 1.3 copies the shift into a local first, loading it before the operand */
static inline void set_shift_constant13(IROLinear *operand, int shift)
{
    CInt64 *p;
    SInt32 n = shift;
    p = &operand->u.node->data.intval;
    p->lo = n;
    p->hi = (n < 0) ? -1 : 0;
}
#endif

void simplify_diadic_nodes(IROLinear *nd)
{
    SInt32 shift;
    IROLinear *right;
    IROLinear *left;
    IROLinear *firstConversion;
    IROLinear *logicalNot;
    IROLinear *secondConversion;
    IROLinear *branch;

    if (nd->type == IROLinearOp2Arg) {
        switch (nd->nodetype) {
            case EASS:
                right = nd->u.diadic.right;
                if (right->type == IROLinearOp2Arg && BY_VERSION(nodetype_map[right->nodetype], nodetype_map[right->nodetype] < 0x4d) &&
                    IroUtil_AreTypesEqual(nd->rtype, right->rtype)) {
                    left = nd->u.diadic.left;
                    if (IroDump_GetObjRef(left) && !(left->flags & 0x80000) &&
                        IroUtil_LinearConstantTreesSame(left, right->u.diadic.left)) {
                        nd->nodetype = nodetype_map[right->nodetype];
                        nd->u.diadic.right = right->u.diadic.right;
                        IroUtil_ClearZeroOperands(right->u.diadic.left);
                        right->type = IROLinearNop;
                        left->flags |= IROLF_Used;
                        left->u.monadic->flags |= IROLF_Used;
                    }
                }
                break;
#if VERSION >= VERSION_GC_1_3
            case EMUL:
                if (nd->rtype->type == TYPEINT && IroDump_IsPowerOfTwo(nd->u.diadic.right, &shift)) {
                    nd->nodetype = ESHL;
                    set_shift_constant13(nd->u.diadic.right, shift);
                }
                break;
            case EDIV:
                if (nd->rtype->type == TYPEINT && IroDump_IsPowerOfTwo(nd->u.diadic.right, &shift)) {
                    if (is_unsigned(nd->rtype)) {
                        nd->nodetype = ESHR;
                        set_shift_constant13(nd->u.diadic.right, shift);
                    } else if (!is_unsigned(nd->rtype) && TYPE_INTEGRAL(nd->rtype)->integral != IT_BOOL &&
                               nd->u.diadic.left->nodetype == ETYPCON &&
                               nd->u.diadic.left->u.monadic->rtype->type == TYPEINT &&
                               is_unsigned(nd->u.diadic.left->u.monadic->rtype) &&
                               nd->u.diadic.left->u.monadic->rtype->size < nd->u.diadic.left->rtype->size) {
                        Type *newtype;
                        nd->nodetype = ESHR;
                        set_shift_constant13(nd->u.diadic.right, shift);
                        if (nd->flags & IROLF_Reffed) {
                            IROLinear *copy = IrOptimizer_NewLinear(IROLinearOp1Arg);
                            memcpy(copy, nd, sizeof(IROLinear));
                            copy->type = IROLinearOp1Arg;
                            copy->nodetype = ETYPCON;
                            copy->index = linear_index_counter++;
                            copy->rtype = nd->rtype;
                            IroUtil_InsertLinearRangeAfter(copy, copy, nd);
                            IroUtil_ReplaceNextReference(nd, copy);
                            copy->u.monadic = nd;
                            nd->flags |= IROLF_Reffed;
                        }
                        nd->rtype = get_unsigned_type(nd->rtype);
                        newtype = get_unsigned_type(nd->u.diadic.right->rtype);
                        nd->u.diadic.right->rtype = nd->u.diadic.right->u.node->rtype = newtype;
                        nd->u.diadic.left->rtype = get_unsigned_type(nd->u.diadic.left->rtype);
                    }
                }
                break;
#else
            case EMUL:
                if (nd->rtype->size <= 4 && nd->rtype->type == TYPEINT &&
                    IroDump_IsPowerOfTwo(nd->u.diadic.right, &shift)) {
                    nd->nodetype = ESHL;
                    set_shift_constant(nd->u.diadic.right, shift);
                }
                break;
#endif
#if VERSION < VERSION_GC_1_3
            case EDIV:
                if (nd->rtype->type == TYPEINT && is_unsigned(nd->rtype) && nd->rtype->size <= 4 &&
                    IroDump_IsPowerOfTwo(nd->u.diadic.right, &shift)) {
                    nd->nodetype = ESHR;
                    set_shift_constant(nd->u.diadic.right, shift);
                }
                break;
#endif
            case EMODULO:
                if (nd->rtype->type == TYPEINT && is_unsigned(nd->rtype) && BY_VERSION( nd->rtype->size <= 4, 1) &&
                    IroDump_IsPowerOfTwo(nd->u.diadic.right, &shift)) {
                    nd->nodetype = EAND;
                    nd->u.diadic.right->u.node->data.intval =
                        CInt64_Sub(nd->u.diadic.right->u.node->data.intval, cint64_one);
                }
                break;
            case EEQU:
                if ((firstConversion = IroUtil_FindNextUse(nd)) && firstConversion->nodetype == ETYPCON &&
                    firstConversion->rtype->type == TYPEINT && (logicalNot = IroUtil_FindNextUse(firstConversion)) &&
                    logicalNot->nodetype == ELOGNOT && (secondConversion = IroUtil_FindNextUse(logicalNot)) &&
                    secondConversion->nodetype == ETYPCON && secondConversion->rtype->type == TYPEINT) {
                    branch = IroUtil_FindNextUse(secondConversion);
                    if ((branch && branch->type == IROLinearIf) || branch->type == IROLinearIfNot) {
                        IroUtil_ReplaceNextReference(secondConversion, nd);
                        nd->nodetype = ENOTEQU;
                        firstConversion->type = IROLinearNop;
                        logicalNot->type = IROLinearNop;
                        secondConversion->type = IROLinearNop;
                    }
                }
                break;
        }
    }
}

#if VERSION >= VERSION_GC_1_3
extern UInt8 data13_005dbfad[];
extern UInt8 data_00552d8c[];
extern UInt8 nodetype_map[];

/* initialize a table of 0x4d per-node-type entries */
void fn_0045c8b0(void)
{
    int i;

    for (i = 0; i < 0x4d; i++)
        data13_005dbfad[i] = 77;
    data13_005dbfad[0] = 77;
    data13_005dbfad[1] = 77;
    data13_005dbfad[2] = 77;
    data13_005dbfad[3] = 77;
    data13_005dbfad[4] = 77;
    data13_005dbfad[5] = 77;
    data13_005dbfad[6] = 77;
    data13_005dbfad[7] = 7;
    data13_005dbfad[8] = 77;
    data13_005dbfad[9] = 77;
    data13_005dbfad[10] = 77;
    data13_005dbfad[11] = 77;
    data13_005dbfad[12] = 77;
    data13_005dbfad[13] = 77;
    data13_005dbfad[14] = 77;
    data13_005dbfad[15] = 77;
    data13_005dbfad[16] = 77;
    data13_005dbfad[17] = 77;
    data13_005dbfad[18] = 77;
    data13_005dbfad[19] = 22;
    data13_005dbfad[20] = 21;
    data13_005dbfad[21] = 20;
    data13_005dbfad[22] = 19;
    data13_005dbfad[23] = 24;
    data13_005dbfad[24] = 23;
    data13_005dbfad[25] = 77;
    data13_005dbfad[26] = 77;
    data13_005dbfad[27] = 77;
    data13_005dbfad[28] = 29;
    data13_005dbfad[29] = 28;
    data13_005dbfad[30] = 77;
    data13_005dbfad[31] = 77;
    data13_005dbfad[32] = 77;
    data13_005dbfad[33] = 77;
    data13_005dbfad[34] = 77;
    data13_005dbfad[35] = 77;
    data13_005dbfad[36] = 77;
    data13_005dbfad[37] = 77;
    data13_005dbfad[38] = 77;
    data13_005dbfad[39] = 77;
    data13_005dbfad[40] = 77;
    data13_005dbfad[41] = 77;
    data13_005dbfad[42] = 77;
    data13_005dbfad[43] = 77;
    data13_005dbfad[44] = 77;
    data13_005dbfad[45] = 77;
    data13_005dbfad[46] = 77;
    data13_005dbfad[47] = 77;
    data13_005dbfad[48] = 77;
    data13_005dbfad[49] = 77;
    data13_005dbfad[50] = 77;
    data13_005dbfad[51] = 77;
    data13_005dbfad[52] = 77;
    data13_005dbfad[53] = 77;
    data13_005dbfad[54] = 77;
    data13_005dbfad[55] = 77;
    data13_005dbfad[56] = 77;
    data13_005dbfad[57] = 77;
    data13_005dbfad[58] = 77;
    data13_005dbfad[59] = 77;
    data13_005dbfad[60] = 77;
    data13_005dbfad[61] = 77;
    data13_005dbfad[62] = 77;
    data13_005dbfad[63] = 77;
    data13_005dbfad[64] = 77;
    data13_005dbfad[65] = 77;
    data13_005dbfad[66] = 77;
    data13_005dbfad[68] = 77;
    data13_005dbfad[69] = 77;
    data13_005dbfad[70] = 77;
    data13_005dbfad[71] = 77;
    data13_005dbfad[72] = 77;
    data13_005dbfad[73] = 77;
    data13_005dbfad[74] = 77;
    data13_005dbfad[75] = 77;
    data13_005dbfad[76] = 77;
}

/* initialize a table of 0x4d per-node-type entries */
void fn_0045cb50(void)
{
    int i;

    for (i = 0; i < 0x4d; i++)
        data_00552d8c[i] = 77;
    data_00552d8c[0] = 1;
    data_00552d8c[1] = 0;
    data_00552d8c[2] = 3;
    data_00552d8c[3] = 2;
    data_00552d8c[4] = 77;
    data_00552d8c[5] = 5;
    data_00552d8c[6] = 6;
    data_00552d8c[7] = 7;
    data_00552d8c[8] = 77;
    data_00552d8c[9] = 11;
    data_00552d8c[10] = 77;
    data_00552d8c[11] = 9;
    data_00552d8c[12] = 77;
    data_00552d8c[13] = 14;
    data_00552d8c[14] = 13;
    data_00552d8c[15] = 16;
    data_00552d8c[16] = 15;
    data_00552d8c[17] = 18;
    data_00552d8c[18] = 17;
    data_00552d8c[19] = 20;
    data_00552d8c[20] = 19;
    data_00552d8c[21] = 22;
    data_00552d8c[22] = 21;
    data_00552d8c[23] = 24;
    data_00552d8c[24] = 23;
    data_00552d8c[25] = 27;
    data_00552d8c[26] = 77;
    data_00552d8c[27] = 25;
    data_00552d8c[28] = 29;
    data_00552d8c[29] = 28;
    data_00552d8c[30] = 77;
    data_00552d8c[31] = 32;
    data_00552d8c[32] = 31;
    data_00552d8c[33] = 77;
    data_00552d8c[34] = 35;
    data_00552d8c[35] = 34;
    data_00552d8c[36] = 37;
    data_00552d8c[37] = 36;
    data_00552d8c[38] = 40;
    data_00552d8c[39] = 77;
    data_00552d8c[40] = 38;
    data_00552d8c[41] = 77;
    data_00552d8c[42] = 77;
    data_00552d8c[43] = 44;
    data_00552d8c[44] = 43;
    data_00552d8c[45] = 77;
    data_00552d8c[46] = 77;
    data_00552d8c[47] = 77;
    data_00552d8c[48] = 77;
    data_00552d8c[49] = 77;
    data_00552d8c[50] = 77;
    data_00552d8c[51] = 77;
    data_00552d8c[52] = 77;
    data_00552d8c[53] = 77;
    data_00552d8c[54] = 77;
    data_00552d8c[55] = 77;
    data_00552d8c[56] = 77;
    data_00552d8c[57] = 77;
    data_00552d8c[58] = 77;
    data_00552d8c[59] = 77;
    data_00552d8c[60] = 77;
    data_00552d8c[61] = 77;
    data_00552d8c[62] = 77;
    data_00552d8c[63] = 77;
    data_00552d8c[64] = 77;
    data_00552d8c[65] = 77;
    data_00552d8c[66] = 77;
    data_00552d8c[68] = 77;
    data_00552d8c[69] = 77;
    data_00552d8c[70] = 77;
    data_00552d8c[71] = 77;
    data_00552d8c[72] = 77;
    data_00552d8c[73] = 77;
    data_00552d8c[74] = 77;
    data_00552d8c[75] = 77;
    data_00552d8c[76] = 77;
}

/* initialize a table of 0x4d per-node-type entries */
void fn_0045cdf0(void)
{
    int i;

    for (i = 0; i < 0x4d; i++)
        nodetype_map[i] = 77;
    nodetype_map[0] = 77;
    nodetype_map[1] = 77;
    nodetype_map[2] = 77;
    nodetype_map[3] = 77;
    nodetype_map[4] = 77;
    nodetype_map[5] = 77;
    nodetype_map[6] = 77;
    nodetype_map[7] = 77;
    nodetype_map[8] = 77;
    nodetype_map[9] = 31;
    nodetype_map[10] = 77;
    nodetype_map[11] = 32;
    nodetype_map[12] = 33;
    nodetype_map[13] = 77;
    nodetype_map[14] = 77;
    nodetype_map[15] = 34;
    nodetype_map[16] = 35;
    nodetype_map[17] = 36;
    nodetype_map[18] = 37;
    nodetype_map[19] = 77;
    nodetype_map[20] = 77;
    nodetype_map[21] = 77;
    nodetype_map[22] = 77;
    nodetype_map[23] = 77;
    nodetype_map[24] = 77;
    nodetype_map[25] = 38;
    nodetype_map[26] = 39;
    nodetype_map[27] = 40;
    nodetype_map[28] = 77;
    nodetype_map[29] = 77;
    nodetype_map[30] = 77;
    nodetype_map[31] = 77;
    nodetype_map[32] = 77;
    nodetype_map[33] = 77;
    nodetype_map[34] = 77;
    nodetype_map[35] = 77;
    nodetype_map[36] = 77;
    nodetype_map[37] = 77;
    nodetype_map[38] = 77;
    nodetype_map[39] = 77;
    nodetype_map[40] = 77;
    nodetype_map[41] = 77;
    nodetype_map[42] = 77;
    nodetype_map[43] = 77;
    nodetype_map[44] = 77;
    nodetype_map[45] = 77;
    nodetype_map[46] = 77;
    nodetype_map[47] = 77;
    nodetype_map[48] = 77;
    nodetype_map[49] = 77;
    nodetype_map[50] = 77;
    nodetype_map[51] = 77;
    nodetype_map[52] = 77;
    nodetype_map[53] = 77;
    nodetype_map[54] = 77;
    nodetype_map[55] = 77;
    nodetype_map[56] = 77;
    nodetype_map[57] = 77;
    nodetype_map[58] = 77;
    nodetype_map[59] = 77;
    nodetype_map[60] = 77;
    nodetype_map[61] = 77;
    nodetype_map[62] = 77;
    nodetype_map[63] = 77;
    nodetype_map[64] = 77;
    nodetype_map[65] = 77;
    nodetype_map[66] = 77;
    nodetype_map[68] = 77;
    nodetype_map[69] = 77;
    nodetype_map[70] = 77;
    nodetype_map[71] = 77;
    nodetype_map[72] = 77;
    nodetype_map[73] = 77;
    nodetype_map[74] = 77;
    nodetype_map[75] = 77;
    nodetype_map[76] = 77;
}
#endif

void rewrite_diadic_as_monadic(IROLinear *node)
{
    IROLinear *second;
    IROLinear *first;
    Type *value;
    first = node->u.diadic.left;
    second = node->u.diadic.right;
    value = second->rtype;
    if (value == node->rtype) {
        node->type = IROLinearOp1Arg;
        node->nodetype = EMONMIN;
        node->u.monadic = second;
        IroUtil_ClearZeroOperands(first);
    } else {
        IroUtil_ClearZeroOperands(first);
        first->type = IROLinearOp1Arg;
        first->nodetype = ETYPCON;
        first->expr = second->expr;
        first->rtype = node->rtype;
        first->u.monadic = second;
        node->type = IROLinearOp1Arg;
        node->nodetype = EMONMIN;
        node->u.monadic = first;
    }
}

void rewrite_diadic_as_rtype_matched_monadic(IROLinear *expression)
{
    IROLinear *function;
    IROLinear *operand;
    function = expression->u.diadic.left;
    operand = expression->u.diadic.right;
    if (function->rtype == expression->rtype) {
        expression->type = 2U;
        expression->nodetype = 5U;
        expression->u.monadic = function;
        IroUtil_ClearZeroOperands(operand);
    } else {
        IroUtil_ClearZeroOperands(operand);
        operand->type = 2U;
        operand->nodetype = 48U;
        operand->expr = function->expr;
        operand->rtype = expression->rtype;
        operand->u.monadic = function;
        expression->type = 2U;
        expression->nodetype = 5U;
        expression->u.monadic = operand;
    }
}

IROLinear *rotate_left_child(IROLinear *nd, int isleft)
{
    IROLinear *child;
    IROLinear *other;
    IROLinear *object;
    IROLinear *first;
    IROLinear *last;
    IROLinear *result;
    IROLinear *otherObject;
    IROLinear *ndObject;

    child = nd->u.diadic.left;
    IroUtil_ReplaceNextReference(nd, child);
    if (isleft) {
        nd->u.diadic.left = child->u.diadic.left;
        child->u.diadic.left = nd;
        other = child->u.diadic.right;
    } else {
        nd->u.diadic.left = child->u.diadic.right;
        child->u.diadic.right = nd;
        other = child->u.diadic.left;
    }
    child->rtype = nd->rtype;
    child->flags = nd->flags;
    child->nodeflags = nd->nodeflags;
    object = child;
    first = child;
    last = nd;
    IroUtil_MoveLinearRangeBeforeObject(object, first, last);
    otherObject = other;
    ndObject = nd;
    return IroUtil_MoveLinearRangeBeforeObject(IroUtil_GetFirstLinear(other), otherObject, ndObject);
}

#if VERSION >= VERSION_GC_1_3
void simplify_diadic_constants(IROLinear *node)
{
    IROLinear *left;
    IROLinear *right;
    int changed;
    int compare;
    Type *type;
    SInt32 tmp1;
    SInt32 tmp2;
    CInt64 val;

    if (node->type == IROLinearOp2Arg) {
        left = node->u.diadic.left;
        right = node->u.diadic.right;
        if (node->rtype->type == TYPEFLOAT || left->rtype->type == TYPEFLOAT || right->rtype->type == TYPEFLOAT) {
            return;
        }
        changed = FALSE;
        if (fn_0044be00(left) == NULL && fn_0044be00(right) == NULL) {
            if (IroDump_IsType1NodeType50(right) != 0 || IroUtil_IsTypeOneNodeTypeFiftyOne(right) != 0) {
                if (IroUtil_IsZeroConstant(right) != 0) {
                    switch (node->nodetype) {
                        case EADDV:
                        case ESUBV:
                        case EADD:
                        case ESUB:
                        case ESHL:
                        case ESHR:
                        case EXOR:
                        case EOR:
                        case ELOR:
                        case EADDASS:
                        case ESUBASS:
                        case ESHLASS:
                        case ESHRASS:
                        case EXORASS:
                        case EORASS:
                            replace_left(node);
                            changed = TRUE;
                            break;
                        case EMUL:
                        case EAND:
                        case ELAND:
                            REPLACE_CONST(node, cint64_zero);
                            changed = TRUE;
                            break;
                        case EMULASS:
                        case EANDASS:
                            node->nodetype = EASS;
#if VERSION < VERSION_GC_1_3
                            changed = TRUE;
#endif
                            node->u.diadic.right->rtype = node->rtype;
                            node->u.diadic.right->u.node->rtype = node->rtype;
#if VERSION >= VERSION_GC_1_3
                            changed = TRUE;
#endif
                            break;
                        case EDIV:
                        case EMODULO:
                        case EDIVASS:
                        case EMODASS:
                            if (node->stmt->sourceoffset != 0) {
                                TStreamElement *errorPos = CPrep_GetLastBufferedToken();
                                errorPos->tokenoffset = node->stmt->sourceoffset;
                                CError_SetBufferedToken(errorPos);
                            }
#if VERSION >= VERSION_GC_1_3
                            CError_Warning(ERR_DIVISION_BY_0);
#else
                            CError_ReportError(ERR_DIVISION_BY_0);
#endif
                            break;
                    }
                } else {
                    if (node->nodetype == ELAND) {
                        replace_left(node);
                        changed = TRUE;
                    } else if (node->nodetype == ELOR) {
                        replace_const(node, cint64_one);
                        changed = TRUE;
                    } else if (node->nodetype == ESHL || node->nodetype == ESHR || node->nodetype == ESHLASS ||
                               node->nodetype == ESHRASS) {
                        type = node->rtype;
                        tmp1 = type->size * 8;
                        if (left->type == IROLinearOp1Arg && left->nodetype == ETYPCON) {
                            type = left->u.monadic->rtype;
                            if (left->u.monadic->type == IROLinearOp1Arg && left->u.monadic->nodetype == EINDIRECT &&
                                left->u.monadic->u.monadic->type == IROLinearOp1Arg &&
                                left->u.monadic->u.monadic->nodetype == EBITFIELD &&
                                left->u.monadic->u.monadic->rtype->type == TYPEBITFIELD)
                                tmp2 = TYPE_BITFIELD(left->u.monadic->u.monadic->rtype)->bitlength;
                            else
                                tmp2 = type->size * 8;
                        } else {
                            tmp2 = tmp1;
                        }

                        switch (node->nodetype) {
                            case ESHL:
                            case ESHLASS:
                                CInt64_SetLong(&val, tmp1);
                                if (is_unsigned(type))
                                    compare = CInt64_GreaterEqualU(right->u.node->data.intval, val);
                                else
                                    compare = CInt64_GreaterEqual(right->u.node->data.intval, val);
                                break;
                            case ESHR:
                            case ESHRASS:
                                CInt64_SetLong(&val, tmp2);
                                compare = is_unsigned(type) && CInt64_GreaterEqualU(right->u.node->data.intval, val);
                                break;
                        }

                        if (compare) {
                            switch (node->nodetype) {
                                case ESHL:
                                case ESHR:
                                    replace_const(node, cint64_zero);
                                    break;
                                case ESHLASS:
                                case ESHRASS:
                                    node->nodetype = EASS;
                                    replace_const(right, cint64_zero);
                                    break;
                            }
                            changed = TRUE;
                        }
                    } else if (fn_0044cad0(right) != 0) {
                        switch (node->nodetype) {
                            case EMUL:
                            case EMULV:
                            case EDIV:
                            case EMULASS:
                            case EDIVASS:
                                replace_left(node);
                                changed = TRUE;
                                break;
                            case EMODULO:
                                REPLACE_CONST(node, cint64_zero);
                                changed = TRUE;
                                break;
                            case EMODASS:
                                node->nodetype = EASS;
                                REPLACE_CONST(right, cint64_zero);
                                node->u.diadic.right->rtype = node->rtype;
                                node->u.diadic.right->u.node->rtype = node->rtype;
                                changed = TRUE;
                                break;
                        }
                    } else if (IroUtil_IsOne(right) != 0) {
                        switch (node->nodetype) {
                            case EMUL:
                            case EMULV:
                            case EDIV:
                                rewrite_diadic_as_rtype_matched_monadic(node);
                                changed = TRUE;
                                break;
                            case EMODULO:
                                REPLACE_CONST(node, cint64_zero);
                                changed = TRUE;
                                break;
                            case EMODASS:
                                node->nodetype = EASS;
                                REPLACE_CONST(right, cint64_zero);
                                node->u.diadic.right->rtype = node->rtype;
                                node->u.diadic.right->u.node->rtype = node->rtype;
                                changed = TRUE;
                                break;
                        }
                    }
                }
            }
            if (!changed && (IroDump_IsType1NodeType50(left) != 0 || IroUtil_IsTypeOneNodeTypeFiftyOne(left) != 0)) {
                if (IroUtil_IsZeroConstant(left) != 0) {
                    switch (node->nodetype) {
                        case EADDV:
                        case EADD:
                        case EXOR:
                        case EOR:
                        case ELOR:
                            replace_right(node);
                            break;
                        case EMUL:
                        case ESHL:
                        case ESHR:
                        case EAND:
                        case ELAND:
                            REPLACE_CONST(node, cint64_zero);
                            break;
                        case ESUBV:
                        case ESUB:
                            rewrite_diadic_as_monadic(node);
                            break;
                    }
                } else {
                    if (node->nodetype == ELAND) {
                        replace_right(node);
                    } else if (node->nodetype == ELOR) {
                        REPLACE_CONST(node, cint64_one);
                    } else if (fn_0044cad0(left) != 0) {
                        switch (node->nodetype) {
                            case EMUL:
                            case EMULV:
                                replace_right(node);
                                break;
                        }
                    } else if (IroUtil_IsOne(left) != 0) {
                        switch (node->nodetype) {
                            case EMUL:
                            case EMULV:
                                rewrite_diadic_as_monadic(node);
                                break;
                        }
                    }
                }
            }
        }
    }
}
#else
void simplify_diadic_constants(IROLinear *node)
{
    IROLinear *left;
    IROLinear *right;
    int changed;
    ENodeType op;
    union {
        CInt64 q;
        CInt64 i;
    } limit;
    int exceedsLimit;
    int operandBits;
    int resultBits;
    Type *operandType;

    if (node->type == IROLinearOp2Arg) {
        left = node->u.diadic.left;
        right = node->u.diadic.right;
        if (node->rtype->type == TYPEFLOAT || left->rtype->type == TYPEFLOAT || right->rtype->type == TYPEFLOAT) {
            return;
        }
        changed = FALSE;
        if (fn_0044be00(left) == NULL && fn_0044be00(right) == NULL) {
            if (IroDump_IsType1NodeType50(right) != 0 || IroUtil_IsTypeOneNodeTypeFiftyOne(right) != 0) {
                if (IroUtil_IsZeroConstant(right) != 0) {
                    switch (node->nodetype) {
                        case EADDV:
                        case ESUBV:
                        case EADD:
                        case ESUB:
                        case ESHL:
                        case ESHR:
                        case EXOR:
                        case EOR:
                        case ELOR:
                        case EADDASS:
                        case ESUBASS:
                        case ESHLASS:
                        case ESHRASS:
                        case EXORASS:
                        case EORASS:
                            replace_left(node);
                            changed = TRUE;
                            break;
                        case EMUL:
                        case EAND:
                        case ELAND:
                            replace_const(node, cint64_zero.hi, cint64_zero.lo);
                            changed = TRUE;
                            break;
                        case EMULASS:
                        case EANDASS:
                            node->nodetype = EASS;
#if VERSION < VERSION_GC_1_3
                            changed = TRUE;
#endif
                            node->u.diadic.right->rtype = node->rtype;
                            node->u.diadic.right->u.node->rtype = node->rtype;
#if VERSION >= VERSION_GC_1_3
                            changed = TRUE;
#endif
                            break;
                        case EDIV:
                        case EMODULO:
                        case EDIVASS:
                        case EMODASS:
                            if (node->stmt->sourceoffset != 0) {
                                TStreamElement *errorPos = CPrep_GetLastBufferedToken();
                                errorPos->tokenoffset = node->stmt->sourceoffset;
                                CError_SetBufferedToken(errorPos);
                            }
#if VERSION >= VERSION_GC_1_3
                            CError_Warning(ERR_DIVISION_BY_0);
#else
                            CError_ReportError(ERR_DIVISION_BY_0);
#endif
                            break;
                    }
                } else {
                    op = node->nodetype;
                    if (op == 0x1c) {
                        replace_left(node);
                        changed = TRUE;
                    } else if (op == 0x1d) {
                        REPLACE_CONST(node, cint64_one);
                        changed = TRUE;
                    } else if (op == 0x11 || op == 0x12 || op == 0x24 || op == 0x25) {
                        resultBits = node->rtype->size;
                        resultBits <<= 3;
                        operandType = node->rtype;
                        operandBits = resultBits;
                        if (left->type == IROLinearOp1Arg && left->nodetype == ETYPCON) {
                            operandType = left->u.monadic->rtype;
                            if (left->u.monadic->type == IROLinearOp1Arg && left->u.monadic->nodetype == EINDIRECT &&
                                left->u.monadic->u.monadic->type == IROLinearOp1Arg &&
                                left->u.monadic->u.monadic->nodetype == EBITFIELD &&
                                left->u.monadic->u.monadic->rtype->type == TYPEBITFIELD) {
                                operandBits = TYPE_BITFIELD(left->u.monadic->u.monadic->rtype)->bitlength;
                            } else {
                                operandBits = left->u.monadic->rtype->size << 3;
                            }
                        } else {
                            operandBits = resultBits;
                        }
                        switch (op) {
                            case 0x11:
                            case 0x24:
                                limit.q.lo = resultBits;
                                limit.q.hi = (resultBits < 0) ? -1 : 0;
                                if (is_unsigned(operandType) != 0) {
                                    exceedsLimit = CInt64_GreaterEqualU(right->u.node->data.intval, limit.q);
                                } else {
                                    Boolean result = CInt64_GreaterEqual(right->u.node->data.intval, limit.i);
                                    exceedsLimit = result;
                                }
                                break;
                            case 0x12:
                            case 0x25:
                                limit.q.lo = operandBits;
                                limit.q.hi = (operandBits < 0) ? -1 : 0;
                                exceedsLimit = is_unsigned(operandType) &&
                                               CInt64_GreaterEqualU(right->u.node->data.intval, limit.q);
                                break;
                        }
                        if (exceedsLimit) {
                            switch (node->nodetype) {
                                case ESHL:
                                case ESHR:
                                    replace_const(node, cint64_zero.hi, cint64_zero.lo);
                                    break;
                                case ESHLASS:
                                case ESHRASS:
                                    node->nodetype = EASS;
                                    replace_const(right, cint64_zero.hi, cint64_zero.lo);
                                    break;
                            }
                            changed = TRUE;
                        }
                    } else if (fn_0044cad0(right) != 0) {
                        switch (node->nodetype) {
                            case EMUL:
                            case EMULV:
                            case EDIV:
                            case EMULASS:
                            case EDIVASS:
                                replace_left(node);
                                changed = TRUE;
                                break;
                            case EMODULO:
                                replace_const(node, cint64_zero.hi, cint64_zero.lo);
                                changed = TRUE;
                                break;
                            case EMODASS:
                                node->nodetype = EASS;
                                replace_const(right, cint64_zero.hi, cint64_zero.lo);
                                node->u.diadic.right->rtype = node->rtype;
                                node->u.diadic.right->u.node->rtype = node->rtype;
                                changed = TRUE;
                                break;
                        }
                    } else if (IroUtil_IsOne(right) != 0) {
                        switch (node->nodetype) {
                            case EMUL:
                            case EMULV:
                            case EDIV:
                                rewrite_diadic_as_rtype_matched_monadic(node);
                                changed = TRUE;
                                break;
                            case EMODULO:
                                replace_const(node, cint64_zero.hi, cint64_zero.lo);
                                changed = TRUE;
                                break;
                            case EMODASS:
                                node->nodetype = EASS;
                                replace_const(right, cint64_zero.hi, cint64_zero.lo);
                                node->u.diadic.right->rtype = node->rtype;
                                node->u.diadic.right->u.node->rtype = node->rtype;
                                changed = TRUE;
                                break;
                        }
                    }
                }
            }
            if (!changed && (IroDump_IsType1NodeType50(left) != 0 || IroUtil_IsTypeOneNodeTypeFiftyOne(left) != 0)) {
                if (IroUtil_IsZeroConstant(left) != 0) {
                    switch (node->nodetype) {
                        case EADDV:
                        case EADD:
                        case EXOR:
                        case EOR:
                        case ELOR:
                            replace_right(node);
                            break;
                        case EMUL:
                        case ESHL:
                        case ESHR:
                        case EAND:
                        case ELAND:
                            replace_const(node, cint64_zero.hi, cint64_zero.lo);
                            break;
                        case ESUBV:
                        case ESUB:
                            rewrite_diadic_as_monadic(node);
                            break;
                    }
                } else {
                    if (node->nodetype == ELAND) {
                        replace_right(node);
                    } else if (node->nodetype == ELOR) {
                        REPLACE_CONST(node, cint64_one);
                    } else if (fn_0044cad0(left) != 0) {
                        switch (node->nodetype) {
                            case EMUL:
                            case EMULV:
                                replace_right(node);
                                break;
                        }
                    } else if (IroUtil_IsOne(left) != 0) {
                        switch (node->nodetype) {
                            case EMUL:
                            case EMULV:
                                rewrite_diadic_as_monadic(node);
                                break;
                        }
                    }
                }
            }
        }
    }
}
#endif

void simplify_same_linears(IROLinear *node)
{
    IROLinear *left;
    IROLinear *right;

    if (node->type == IROLinearOp2Arg) {
        left = node->u.diadic.left;
        right = node->u.diadic.right;
        if (node->rtype->type == TYPEFLOAT || left->rtype->type == TYPEFLOAT || right->rtype->type == TYPEFLOAT) {
            return;
        }
        if (IroUtil_LinearsSame(left, right) && fn_0044be00(left) == NULL) {
            switch (node->nodetype) {
                case ESUBV:
                case ESUB:
                case ELESS:
                case EGREATER:
                case ENOTEQU:
                case EXOR:
                    REPLACE_CONST(node, cint64_zero);
                    break;
                case ELESSEQU:
                case EGREATEREQU:
                case EEQU:
                    REPLACE_CONST(node, cint64_one);
                    break;
                case EAND:
                case EOR:
                case ELAND:
                case ELOR:
                case EASS:
                case EANDASS:
                case EORASS:
                    replace_left(node);
                    break;
                case ESUBASS:
                case EXORASS:
                    node->nodetype = EASS;
                    REPLACE_CONST(right, cint64_zero);
                    break;
            }
        }
    }
}

void simplify_matching_monadic_operands(IROLinear *node)
{
    IROLinear *left, *right, *wrapper;
    Boolean changed;
    ENodeType op;

    if (node->type == IROLinearOp2Arg) {
        left = node->u.diadic.left;
        right = node->u.diadic.right;
        if (left->type == IROLinearOp1Arg) {
            if (right->type == IROLinearOp1Arg) {
                if (left->nodetype == right->nodetype) {
                    switch (left->nodetype) {
                        case EMONMIN:
                        case EBINNOT:
                        case ELOGNOT:
                            changed = 0;
                            op = node->nodetype;
                            do {
                                switch (op) {
                                    case EXOR:
                                        if (left->nodetype == EBINNOT)
                                            goto simple;
                                        continue;
                                    case ELESS:
                                    case EGREATER:
                                    case ELESSEQU:
                                    case EGREATEREQU:
                                        if (left->nodetype != EMONMIN)
                                            continue;
                                        node->nodetype = data_00552d8c[op];
                                        goto simple;
                                        continue;
                                    case EMUL:
                                    case EDIV:
                                        if (left->nodetype == EMONMIN)
                                            goto simple;
                                        continue;
                                    case EEQU:
                                    case ENOTEQU:
                                        if (left->nodetype == ELOGNOT)
                                            continue;
                                    simple:
                                        node->u.diadic.left = left->u.monadic;
                                        node->u.diadic.right = right->u.monadic;
                                        right->type = IROLinearNop;
                                        left->type = right->type;
                                        changed = 1;
                                        continue;
                                    case ELAND:
                                    case ELOR:
                                        if (left->nodetype != ELOGNOT)
                                            continue;
                                        node->nodetype = data_00552d8c[op];
                                        break;
                                        continue;
                                    case EAND:
                                    case EOR:
                                        if (node->rtype->type == TYPEFLOAT)
                                            continue;
                                        if (left->rtype->type == TYPEFLOAT)
                                            continue;
                                        if (right->rtype->type == TYPEFLOAT)
                                            continue;
                                        if (left->nodetype == EMONMIN)
                                            continue;
                                        node->nodetype = data_00552d8c[op];
                                        break;
                                        continue;
                                    case EADD:
                                    case ESUB:
                                        if (node->rtype->type == TYPEFLOAT)
                                            continue;
                                        if (left->rtype->type == TYPEFLOAT)
                                            continue;
                                        if (right->rtype->type == TYPEFLOAT)
                                            continue;
                                        if (left->nodetype != EMONMIN)
                                            continue;
                                        break;
                                    default:
                                        continue;
                                }
                                wrapper = node->u.diadic.left;
                                IroUtil_ReplaceNextReference(node, wrapper);
                                node->u.diadic.left = wrapper->u.monadic;
                                wrapper->u.monadic = node;
                                CopyTypeInfo(wrapper, node);
                                IroUtil_MoveLinearRangeBeforeObject(wrapper, wrapper, node);
                                node->u.diadic.right = right->u.monadic;
                                right->type = IROLinearNop;
                                changed = 1;
                            } while (0);
                            if (changed) {
                                simplify_diadic_nodes(node);
                                simplify_diadic_constants(node);
                                simplify_same_linears(node);
#if VERSION >= VERSION_GC_1_3
                                fn_0045b490(node);
#endif
                            }
                            break;
                    }
                }
            }
        }
    }
}

#if VERSION >= VERSION_GC_1_3
typedef struct IROListNode13 {
    IROList list;
    struct IROListNode13 *nextList;
} IROListNode13;

extern Object *data_005875b8;
extern Boolean fn_0047d430(Object *proc, IROLinear *node);
extern void fn_0047c270(Object *proc, IROLinear *node, IROListNode13 **list);
extern void fn_005297b0(void *p);

/* the reference's DoTransformations13: an equality of two pointers pointer analysis resolves to one place each */
void fn_0045b490(IROLinear *nd)
{
    IROLinear *left;
    IROLinear *right;
    IROLinear *left2;
    IROLinear *right2;
    IROListNode13 *tmp;
    IROListNode13 *leftlist;
    IROListNode13 *rightlist;

    if (nd->type == IROLinearOp2Arg) {
        left = nd->u.diadic.left;
        right = nd->u.diadic.right;

        if (!fn_0044be00(left) && !fn_0044be00(right) && (nd->nodetype == EEQU || nd->nodetype == ENOTEQU) &&
            fn_0047d430(data_005875b8, left) && fn_0047d430(data_005875b8, right)) {
            leftlist = NULL;
            fn_0047c270(data_005875b8, left, &leftlist);
            if (leftlist) {
                if (leftlist->list.head && leftlist->list.tail && !leftlist->nextList) {
                    rightlist = NULL;
                    fn_0047c270(data_005875b8, right, &rightlist);
                    if (rightlist) {
                        if (rightlist->list.head && rightlist->list.tail && !rightlist->nextList) {
                            left2 = leftlist->list.tail;
                            right2 = rightlist->list.tail;
                            if (IroUtil_LinearsSame(left2, right2)) {
                                replace_const(nd, (nd->nodetype == EEQU) ? cint64_one : cint64_zero);
                            } else if (left2->type == right2->type &&
                                       IroUtil_AreTypesEqual(left2->rtype, right2->rtype)) {
                                replace_const(nd, (nd->nodetype == EEQU) ? cint64_zero : cint64_one);
                            }
                        }

                        while (rightlist) {
                            tmp = rightlist->nextList;
                            fn_005297b0(rightlist);
                            rightlist = tmp;
                        }
                    }
                }

                while (leftlist) {
                    tmp = leftlist->nextList;
                    fn_005297b0(leftlist);
                    leftlist = tmp;
                }
            }
        }
    }
}
#endif

void simplify_diadic_with_monadic_operand(IROLinear *expr)
{
    IROLinear *left;
    IROLinear *right;
    IROLinear *matchingOperand;

    if (expr->type == IROLinearOp2Arg) {
        left = expr->u.diadic.left;
        right = expr->u.diadic.right;
        if (expr->rtype->type == TYPEFLOAT || left->rtype->type == TYPEFLOAT || right->rtype->type == TYPEFLOAT)
            return;
        if (fn_0044be00(left) == NULL && fn_0044be00(right) == NULL) {
            if (left->type == IROLinearOp1Arg && left->nodetype == EMONMIN) {
                switch (expr->nodetype) {
                    case ESUBV:
                    case ESUB:
                        expr->nodetype = data_00552d8c[expr->nodetype];
                        left = expr->u.diadic.left;
                        IroUtil_ReplaceNextReference(expr, left);
                        expr->u.diadic.left = left->u.monadic;
                        left->u.monadic = expr;
                        copyExpressionMetadata(left, expr);
                        IroUtil_MoveLinearRangeBeforeObject(left, left, expr);
                        break;
                    case EADDV:
                    case EADD:
                        expr->nodetype = data_00552d8c[expr->nodetype];
                        expr->u.diadic.left = right;
                        expr->u.diadic.right = left->u.monadic;
                        left->type = IROLinearNop;
                        simplify_diadic_nodes(expr);
                        simplify_same_linears(expr);
                        simplify_matching_monadic_operands(expr);
                        break;
                    case EDIV:
                        if (IroUtil_LinearsSame(left->u.monadic, right) != 0)
                            REPLACE_CONST(expr, cint64_negone);
                        break;
                }
            } else {
                matchingOperand = NULL;
                if (left->type == IROLinearOp1Arg && IroUtil_LinearsSame(left->u.monadic, right) != 0)
                    matchingOperand = left;
                else if (right->type == IROLinearOp1Arg && IroUtil_LinearsSame(left, right->u.monadic) != 0)
                    matchingOperand = right;
                if (matchingOperand != NULL) {
                    switch (expr->nodetype) {
                        case EAND:
                            if (matchingOperand->nodetype == EBINNOT || matchingOperand->nodetype == ELOGNOT)
                                REPLACE_CONST(expr, cint64_zero);
                            break;
                        case EANDASS:
                            if (matchingOperand->nodetype == EBINNOT || matchingOperand->nodetype == ELOGNOT) {
                                expr->nodetype = EASS;
                                REPLACE_CONST(right, cint64_zero);
                                expr->u.diadic.right->rtype = expr->rtype;
                                expr->u.diadic.right->u.node->rtype = expr->rtype;
                            }
                            break;
                        case EXOR:
                        case EOR:
                            if (matchingOperand->nodetype == EBINNOT) {
                                REPLACE_CONST(expr, cint64_zero);
                                expr->u.node->data.intval = CInt64_Inv(expr->u.node->data.intval);
                            }
                            break;
                        case EXORASS:
                        case EORASS:
                            if (matchingOperand->nodetype == EBINNOT) {
                                expr->nodetype = EASS;
                                REPLACE_CONST(right, cint64_zero);
                                right->u.node->data.intval = CInt64_Inv(right->u.node->data.intval);
                                expr->u.diadic.right->rtype = expr->rtype;
                                expr->u.diadic.right->u.node->rtype = expr->rtype;
                            }
                            break;
                        case ELOR:
                            if (matchingOperand->nodetype == EBINNOT || matchingOperand->nodetype == ELOGNOT)
                                REPLACE_CONST(expr, cint64_one);
                            break;
                        case ELAND:
                            if (matchingOperand->nodetype == ELOGNOT)
                                REPLACE_CONST(expr, cint64_zero);
                            break;
                        case EDIV:
                            if (matchingOperand->nodetype == EMONMIN)
                                REPLACE_CONST(expr, cint64_negone);
                            break;
                        case EDIVASS:
                            if (matchingOperand->nodetype == EMONMIN) {
                                expr->nodetype = EASS;
                                REPLACE_CONST(right, cint64_negone);
                                expr->u.diadic.right->rtype = expr->rtype;
                                expr->u.diadic.right->u.node->rtype = expr->rtype;
                            }
                            break;
                    }
                }
            }
        }
    }
}

void simplify_diadic_matching_child(IROLinear *node)
{
    IROLinear *left;
    IROLinear *right;
    unsigned char reverseOp;
    unsigned char matchingChild;
    unsigned char changed;

    if (node->type == IROLinearOp2Arg && fn_0044be00(node) == NULL) {
        left = node->u.diadic.left;
        right = node->u.diadic.right;
        if (node->rtype->type == TYPEFLOAT || left->rtype->type == TYPEFLOAT || right->rtype->type == TYPEFLOAT)
            return;

        changed = reverseOp = FALSE;
        matchingChild = 0;

        do {
            switch (node->nodetype) {
                case ELESS:
                case EGREATER:
                case ELESSEQU:
                case EGREATEREQU:
                    reverseOp = TRUE;
                    /* fallthrough */
                case EADDV:
                case EADD:
                case EEQU:
                case ENOTEQU:
                case EAND:
                case EOR:
                    if (left->type == IROLinearOp2Arg) {
                        if (IroUtil_LinearsSame(right, left->u.diadic.left) != 0) {
                            matchingChild = 1;
                        } else if (IroUtil_LinearsSame(right, left->u.diadic.right) != 0) {
                            matchingChild = 2;
                        }
                        if (matchingChild != 0) {
                            if (reverseOp)
                                node->nodetype = data_00552d8c[node->nodetype];
                            swapkids(node);
                            left = node->u.diadic.left;
                            right = node->u.diadic.right;
                            break;
                        }
                    }
                    /* fallthrough */
                case ESUBV:
                case ESUB:
                case EADDASS:
                case ESUBASS:
                case EANDASS:
                case EORASS:
                    if (right->type == IROLinearOp2Arg) {
                        if (IroUtil_LinearsSame(left, right->u.diadic.left) != 0) {
                            matchingChild = 1;
                        } else if (IroUtil_LinearsSame(left, right->u.diadic.right) != 0) {
                            matchingChild = 2;
                        }
                    }
                    break;
                default:
                    continue;
            }

            if (matchingChild != 0) {
                switch (right->nodetype) {
                    case EAND:
                    case EOR:
                        if (matchingChild == 2) {
                            swapkids(right);
                        }
                        if (node->nodetype == right->nodetype ||
                            (node->nodetype == EANDASS && right->nodetype == EAND) ||
                            (node->nodetype == EORASS && right->nodetype == EOR)) {
                            replace_right(right);
                            changed = 1;
                            break;
                        }
                        if (node->nodetype == data_00552d8c[right->nodetype] ||
                            (node->nodetype == EANDASS && right->nodetype == EOR) ||
                            (node->nodetype == EORASS && right->nodetype == EAND)) {
                            replace_left(node);
                        }
                        break;
                    case EADD:
                        if (matchingChild == 2) {
                            swapkids(right);
                        }
                        switch (node->nodetype) {
                            case ELESS:
                            case EGREATER:
                            case ELESSEQU:
                            case EGREATEREQU:
                            case EEQU:
                            case ENOTEQU:
                                REPLACE_CONST(left, cint64_zero);
                                replace_right(right);
                                swapkids(node);
                                if (reverseOp)
                                    node->nodetype = data_00552d8c[node->nodetype];
                                changed = 1;
                                break;
                            case ESUBV:
                            case ESUB:
                                replace_right(right);
                                rewrite_diadic_as_monadic(node);
                                changed = 1;
                                break;
                            case ESUBASS:
                                replace_right(right);
                                changed = 1;
                                break;
                            default:
                                break;
                        }
                        break;
                    case ESUB:
                        switch (node->nodetype) {
                            case ELESS:
                            case EGREATER:
                            case ELESSEQU:
                            case EGREATEREQU:
                            case EEQU:
                            case ENOTEQU:
                                if (matchingChild == 1) {
                                    REPLACE_CONST(left, cint64_zero);
                                    replace_right(right);
                                    swapkids(node);
                                }
                                break;
                            case EADDV:
                            case EADD:
                                if (matchingChild == 2) {
                                    replace_left(right);
                                    replace_right(node);
                                }
                                break;
                            case ESUBV:
                            case ESUB:
                                if (matchingChild == 1) {
                                    replace_right(right);
                                    replace_right(node);
                                }
                                break;
                            case EADDASS:
                                if (matchingChild == 2) {
                                    node->nodetype = EASS;
                                    replace_left(right);
                                    changed = 1;
                                }
                                break;
                            case ESUBASS:
                                if (matchingChild == 1) {
                                    node->nodetype = EASS;
                                    replace_right(right);
                                    changed = 1;
                                }
                                break;
                            default:
                                break;
                        }
                        break;
                    default:
                        break;
                }
            }

        } while (FALSE);

        if (!changed) {
            switch (node->nodetype) {
                case ESUBV:
                case ESUB:
                    matchingChild = 0;
                    if (left->type == IROLinearOp2Arg) {
                        if (IroUtil_LinearsSame(right, left->u.diadic.left) != 0) {
                            matchingChild = 1;
                        } else if (IroUtil_LinearsSame(right, left->u.diadic.right) != 0) {
                            matchingChild = 2;
                        }
                    }
                    if (matchingChild == 1) {
                        if (left->nodetype == ESUB) {
                            rewrite_diadic_as_monadic(left);
                            replace_left(node);
                        } else if (left->nodetype == EADD) {
                            replace_right(left);
                            replace_left(node);
                        }
                    } else if (matchingChild == 2 && left->nodetype == EADD) {
                        replace_left(left);
                        replace_left(node);
                    }
                    break;
            }
        }
        if (changed) {
            simplify_diadic_nodes(node);
            simplify_diadic_constants(node);
            simplify_same_linears(node);
            simplify_matching_monadic_operands(node);
            simplify_diadic_with_monadic_operand(node);
            simplify_diadic_matching_child(node);
        }
    }
}

void remove_common_op(struct IROLinear *expr)
{
    IROLinear *left;
    IROLinear *right;
    IROLinear *tmp;
    char flag;

    if (expr->type == IROLinearOp2Arg) {
        left = expr->u.diadic.left;
        right = expr->u.diadic.right;
        if (expr->rtype->type == TYPEFLOAT || left->rtype->type == TYPEFLOAT || right->rtype->type == TYPEFLOAT)
            return;
        if (left->type == IROLinearOp2Arg && right->type == IROLinearOp2Arg && !fn_0044be00(expr)) {
            flag = 0;
            if (IroUtil_LinearsSame(left->u.diadic.left, right->u.diadic.left)) {
                if (left->nodetype == right->nodetype) {
                    switch (left->nodetype) {
                        case EADD:
                            if (expr->nodetype == data_00552d8c[left->nodetype]) {
                                ReplaceExprWithRightChild(left);
                                ReplaceExprWithRightChild(right);
                                flag = 1;
                            }
                            break;
                        case ESUB:
                            if (expr->nodetype == ESUB || expr->nodetype == ESUBV) {
                                ReplaceExprWithRightChild(left);
                                ReplaceExprWithRightChild(right);
                                tmp = expr->u.diadic.left;
                                expr->u.diadic.left = expr->u.diadic.right;
                                expr->u.diadic.right = tmp;
                                flag = 1;
                            }
                            break;
                        case EMUL:
                            switch (expr->nodetype) {
                                case EADD:
                                case ESUB:
                                    RemoveCommonOpLeftLeft(expr);
                                    flag = 3;
                            }
                            break;
                        case EAND:
                            if (expr->nodetype == EXOR) {
                                RemoveCommonOpLeftLeft(expr);
                                flag = 3;
                                break;
                            }
                        case EOR:
                            if (expr->nodetype == left->nodetype) {
                                ReplaceExprWithRightChild(left);
                                flag = 1;
                            } else if (expr->nodetype == data_00552d8c[left->nodetype]) {
                                RemoveCommonOpLeftLeft(expr);
                                flag = 3;
                            }
                            break;
                    }
                } else if (left->nodetype == data_00552d8c[right->nodetype]) {
                    switch (expr->nodetype) {
                        case ESUBV:
                        case ESUB:
                            switch (left->nodetype) {
                                case EADD:
                                    expr->nodetype = data_00552d8c[expr->nodetype];
                                    ReplaceExprWithRightChild(left);
                                    ReplaceExprWithRightChild(right);
                                    flag = 1;
                                    break;
                                case ESUB:
                                    rewrite_diadic_as_monadic(left);
                                    ReplaceExprWithRightChild(right);
                                    flag = 1;
                                    break;
                            }
                            break;
                        case EAND:
                        case EOR:
                            if (left->nodetype == expr->nodetype)
                                ReplaceExprWithLeftChild(expr);
                            else if (right->nodetype == expr->nodetype)
                                ReplaceExprWithRightChild(expr);
                            break;
                    }
                }
            } else if (IroUtil_LinearsSame(left->u.diadic.right, right->u.diadic.left)) {
                if (left->nodetype == right->nodetype) {
                    switch (left->nodetype) {
                        case EADD:
                            if (expr->nodetype == data_00552d8c[left->nodetype]) {
                                ReplaceExprWithLeftChild(left);
                                ReplaceExprWithRightChild(right);
                                flag = 1;
                            }
                            break;
                        case EMUL:
                            switch (expr->nodetype) {
                                case EADD:
                                case ESUB:
                                    RemoveCommonOpRightLeft(expr);
                                    flag = 3;
                            }
                            break;
                        case EAND:
                            if (expr->nodetype == EXOR) {
                                RemoveCommonOpRightLeft(expr);
                                flag = 3;
                                break;
                            }
                        case EOR:
                            if (expr->nodetype == left->nodetype) {
                                ReplaceExprWithLeftChild(left);
                                flag = 1;
                            } else if (expr->nodetype == data_00552d8c[left->nodetype]) {
                                RemoveCommonOpRightLeft(expr);
                                flag = 3;
                            }
                            break;
                    }
                } else if (left->nodetype == data_00552d8c[right->nodetype]) {
                    switch (expr->nodetype) {
                        case ESUBV:
                        case ESUB:
                            if (left->nodetype == EADD) {
                                expr->nodetype = data_00552d8c[expr->nodetype];
                                ReplaceExprWithLeftChild(left);
                                ReplaceExprWithRightChild(right);
                                flag = 1;
                            }
                            break;
                        case EADDV:
                        case EADD:
                            if (left->nodetype == ESUB) {
                                ReplaceExprWithLeftChild(left);
                                ReplaceExprWithRightChild(right);
                                flag = 1;
                            }
                            break;
                        case EAND:
                        case EOR:
                            if (left->nodetype == expr->nodetype)
                                ReplaceExprWithLeftChild(expr);
                            else if (right->nodetype == expr->nodetype)
                                ReplaceExprWithRightChild(expr);
                            break;
                    }
                }
            } else if (IroUtil_LinearsSame(left->u.diadic.left, right->u.diadic.right)) {
                if (left->nodetype == right->nodetype) {
                    switch (left->nodetype) {
                        case EADD:
                            if (expr->nodetype == data_00552d8c[left->nodetype]) {
                                ReplaceExprWithRightChild(left);
                                ReplaceExprWithLeftChild(right);
                                flag = 1;
                            }
                            break;
                        case ESUB:
                            switch (expr->nodetype) {
                                case EADDV:
                                case EADD:
                                    expr->nodetype = data_00552d8c[expr->nodetype];
                                    ReplaceExprWithRightChild(left);
                                    ReplaceExprWithLeftChild(right);
                                    tmp = expr->u.diadic.left;
                                    expr->u.diadic.left = expr->u.diadic.right;
                                    expr->u.diadic.right = tmp;
                                    flag = 1;
                            }
                            break;
                        case EMUL:
                            switch (expr->nodetype) {
                                case EADD:
                                case ESUB:
                                    RemoveCommonOpLeftRight(expr);
                                    flag = 2;
                            }
                            break;
                        case EAND:
                            if (expr->nodetype == EXOR) {
                                RemoveCommonOpLeftRight(expr);
                                flag = 2;
                                break;
                            }
                        case EOR:
                            if (expr->nodetype == left->nodetype) {
                                ReplaceExprWithRightChild(left);
                                flag = 1;
                            } else if (expr->nodetype == data_00552d8c[left->nodetype]) {
                                RemoveCommonOpLeftRight(expr);
                                flag = 2;
                            }
                            break;
                    }
                } else if (left->nodetype == data_00552d8c[right->nodetype]) {
                    switch (expr->nodetype) {
                        case EADDV:
                        case EADD:
                            if (left->nodetype == EADD) {
                                ReplaceExprWithRightChild(left);
                                ReplaceExprWithLeftChild(right);
                                flag = 1;
                            }
                            break;
                        case ESUBV:
                        case ESUB:
                            if (left->nodetype == ESUB) {
                                rewrite_diadic_as_monadic(left);
                                ReplaceExprWithRightChild(right);
                                flag = 1;
                            }
                            break;
                        case EAND:
                        case EOR:
                            if (left->nodetype == expr->nodetype)
                                ReplaceExprWithLeftChild(expr);
                            else if (right->nodetype == expr->nodetype)
                                ReplaceExprWithRightChild(expr);
                            break;
                    }
                }
            } else if (IroUtil_LinearsSame(left->u.diadic.right, right->u.diadic.right)) {
                if (left->nodetype == right->nodetype) {
                    switch (expr->nodetype) {
                        case ESUB:
                            switch (left->nodetype) {
                                case EADD:
                                case ESUB:
                                    ReplaceExprWithLeftChild(left);
                                    ReplaceExprWithLeftChild(right);
                                    flag = 1;
                            }
                        case EADD:
                            switch (left->nodetype) {
                                case EMUL:
                                case ESHL:
                                    RemoveCommonOpRightRight(expr);
                                    flag = 2;
                            }
                            break;
                        case EXOR:
                            switch (left->nodetype) {
                                case ESHL:
                                case ESHR:
                                case EAND:
                                    RemoveCommonOpRightRight(expr);
                                    flag = 2;
                            }
                            break;
                        case EAND:
                        case EOR:
                            if (left->nodetype == expr->nodetype) {
                                ReplaceExprWithLeftChild(right);
                                flag = 1;
                            } else if (left->nodetype == data_00552d8c[expr->nodetype] || left->nodetype == ESHL ||
                                       left->nodetype == ESHR) {
                                RemoveCommonOpRightRight(expr);
                                flag = 2;
                            }
                            break;
                    }
                } else if (left->nodetype == data_00552d8c[right->nodetype]) {
                    switch (expr->nodetype) {
                        case EADDV:
                        case EADD:
                            switch (left->nodetype) {
                                case EADD:
                                case ESUB:
                                    ReplaceExprWithLeftChild(left);
                                    ReplaceExprWithLeftChild(right);
                                    flag = 1;
                            }
                            break;
                        case EAND:
                        case EOR:
                            if (left->nodetype == expr->nodetype)
                                ReplaceExprWithLeftChild(expr);
                            else if (right->nodetype == expr->nodetype)
                                ReplaceExprWithRightChild(expr);
                            break;
                    }
                }
            }
            if (flag) {
                TransformExpr(expr);
                if (flag == 2)
                    TransformExpr(expr->u.diadic.left);
                else if (flag == 3)
                    TransformExpr(expr->u.diadic.right);
                IroDump_Print("remove common op at: %d\n", expr->index);
            }
        }
    }
}

void fn_004507d0(IROLinear *node)
{
    IROLinear *left;
    IROLinear *right;
    int simplifiedLeft = 0;

    if (node->type == IROLinearOp2Arg) {
        left = node->u.diadic.left;
        right = node->u.diadic.right;
        if ((left->type == IROLinearOp2Arg &&
             (left->u.diadic.left->rtype->type == TYPEFLOAT || left->u.diadic.right->rtype->type == TYPEFLOAT)) ||
            (right->type == IROLinearOp2Arg &&
             (right->u.diadic.left->rtype->type == TYPEFLOAT || right->u.diadic.right->rtype->type == TYPEFLOAT)))
            return;

        switch (left->nodetype) {
            case ELESS:
            case EGREATER:
            case ELESSEQU:
            case EGREATEREQU:
            case EEQU:
            case ENOTEQU:
                switch (node->nodetype) {
                    case EEQU:
                        if (isTrueConstant(right)) {
                            fix26(node);
                            recordSimplification(node);
                            simplifiedLeft = 1;
                        } else if (isFalseConstant(right)) {
                            left->nodetype = data_00552dd0[left->nodetype];
                            fix26(node);
                            recordSimplification(node);
                            simplifiedLeft = 1;
                        }
                        break;
                    case ENOTEQU:
                        if (isTrueConstant(right)) {
                            left->nodetype = data_00552dd0[left->nodetype];
                            fix26(node);
                            recordSimplification(node);
                            simplifiedLeft = 1;
                        } else if (isFalseConstant(right)) {
                            fix26(node);
                            recordSimplification(node);
                            simplifiedLeft = 1;
                        }
                        break;
                }
                break;
            case ELAND:
            case ELOR:
                switch (node->nodetype) {
                    case EEQU:
                        if (isTrueConstant(right)) {
                            fix26(node);
                            recordSimplification(node);
                            simplifiedLeft = 1;
                        }
                        break;
                    case ENOTEQU:
                        if (isFalseConstant(right)) {
                            fix26(node);
                            recordSimplification(node);
                            simplifiedLeft = 1;
                        }
                        break;
                }
                break;
        }

        if (!simplifiedLeft) {
            switch (right->nodetype) {
                case ELESS:
                case EGREATER:
                case ELESSEQU:
                case EGREATEREQU:
                case EEQU:
                case ENOTEQU:
                    switch (node->nodetype) {
                        case EEQU:
                            if (isTrueConstant(left)) {
                                fix30(node);
                            } else if (isFalseConstant(left)) {
                                right->nodetype = data_00552dd0[right->nodetype];
                                fix30(node);
                            }
                            break;
                        case ENOTEQU:
                            if (isTrueConstant(left)) {
                                right->nodetype = data_00552dd0[right->nodetype];
                                fix30(node);
                            } else if (isFalseConstant(left)) {
                                fix30(node);
                            }
                            break;
                    }
                    break;
                case ELAND:
                case ELOR:
                    switch (node->nodetype) {
                        case EEQU:
                            if (isTrueConstant(left)) {
                                fix30(node);
                            }
                            break;
                        case ENOTEQU:
                            if (isFalseConstant(left)) {
                                fix30(node);
                            }
                            break;
                    }
                    break;
            }
        }
    }
}

void fn_00450720(IROLinear *o)
{
    IROLinear *p;
    Object *x;
    Type *e;
    UInt32 q;
    IROLinear *record;
    Object *object;

    if (o->type == IROLinearOp1Arg && (o->flags & IROLF_Reffed) == 0) {
        p = o;
        while (p->nodetype == EINDIRECT && p->u.monadic->nodetype == EINDIRECT && (e = p->rtype) != NULL) {
            switch ((SInt8)e->type) {
                case TYPEARRAY:
                    q = TYPE_POINTER(e)->qual;
                    break;
                case TYPEPOINTER:
                    q = TYPE_POINTER(e)->qual;
                    break;
                case TYPEMEMBERPOINTER:
                    q = TYPE_MEMBER_POINTER(e)->qual;
                    break;
                default:
                    q = 0;
                    break;
            }
            if (q & 2)
                return;
            p = p->u.monadic;
        }

        record = p;
        x = IroDump_GetObjRef(record);
        if (x != NULL) {
            object = x;
            if (!is_volatile_object(object) && (x->qual & Q_VOLATILE) == 0)
                IroUtil_ClearZeroOperands(o);
        }
    }
}

void remove_redundant_monadic_ops(IROLinear *expression)
{
    short changed;
    IROLinear *previous;
    IROLinear *value;
    if (expression->type == IROLinearOp1Arg) {
        previous = IroUtil_FindNextUse(expression);
        if (previous != NULL && previous->nodetype == expression->nodetype) {
            changed = 0;
            switch (expression->nodetype) {
                case ELOGNOT:
                    value = IroUtil_FindNextUse(previous);
                    do {
                        if (value != NULL) {
                            if (value->rtype != NULL && UNCHECKED_INTEGRAL(value->rtype) == IT_BOOL &&
                                expression->u.monadic->rtype != NULL &&
                                UNCHECKED_INTEGRAL(expression->u.monadic->rtype) == IT_BOOL)
                                break;
                            if (value->type == IROLinearIf)
                                break;
                            if (value->type == IROLinearIfNot)
                                break;
#if VERSION >= VERSION_GC_1_3
                            if (value->type == IROLinearCond && expression == value->u.args3.a)
                                break;
#endif
                            switch (value->nodetype) {
                                case ELOGNOT:
                                case ELAND:
                                case ELOR:
                                    continue;
                            }
                        }
                        if (expression->u.monadic->type == IROLinearOp1Arg ||
                            expression->u.monadic->type == IROLinearOp2Arg) {
                            switch (expression->u.monadic->nodetype) {
                                case ELOGNOT:
                                case ELESS:
                                case EGREATER:
                                case ELESSEQU:
                                case EGREATEREQU:
                                case EEQU:
                                case ENOTEQU:
                                case ELAND:
                                case ELOR:
                                    continue;
                            }
                        }
                        goto done;
                    } while (0);
                case EMONMIN:
                case EBINNOT:
                    IroUtil_ReplaceNextReference(previous, expression->u.monadic);
                    expression->type = previous->type = IROLinearNop;
                    changed = 1;
                    break;
                case ETYPCON:
                    if (TYPE_INTEGRAL(expression->rtype)->integral == IT_FLOAT) {
                        switch (TYPE_INTEGRAL(previous->rtype)->integral) {
                            case IT_DOUBLE:
                            case IT_LONGDOUBLE:
                                switch (TYPE_INTEGRAL(expression->u.monadic->rtype)->integral) {
                                    case IT_BOOL:
                                    case IT_CHAR:
                                    case IT_SCHAR:
                                    case IT_UCHAR:
                                    case IT_WCHAR_T:
                                    case IT_SHORT:
                                    case IT_USHORT:
                                        expression->type = IROLinearNop;
                                        previous->u.monadic = expression->u.monadic;
                                        changed = 1;
                                }
                        }
                    }
            }
        done:
#if VERSION < VERSION_GC_1_3
            if (changed != 0)
                IroDump_Print("remove redundant Monadic op at: %d, %d\n", expression->index, previous->index);
#else
            ;
#endif
        }
    }
}

void IroTransform_SimplifyLinear(void)
{
    IROLinear *node;

    for (node = linear_head; node != NULL; node = node->next) {
        switch (node->type) {
            case IROLinearOp2Arg:
                simplify_diadic_nodes(node);
                simplify_diadic_constants(node);
                simplify_same_linears(node);
                simplify_matching_monadic_operands(node);
                simplify_diadic_with_monadic_operand(node);
                simplify_diadic_matching_child(node);
                remove_common_op(node);
                fn_004507d0(node);
                break;
            case IROLinearOp1Arg: {
                IROLinear *mon;
                UInt8 cost;

                fn_00450720(node);
                remove_redundant_monadic_ops(node);
                if (node->type == IROLinearOp1Arg && node->nodetype == EMONMIN) {
                    mon = IroUtil_FindNextUse(node);
                    if (mon != NULL && mon->type == IROLinearOp2Arg && mon->u.diadic.right == node) {
                        switch (cost = mon->nodetype) {
                            case EADDV:
                            case ESUBV:
                            case EADD:
                            case ESUB:
                            case EADDASS:
                            case ESUBASS:
                                mon->nodetype = data_00552d8c[cost];
                                node->type = IROLinearNop;
                                mon->u.diadic.right = node->u.monadic;
                                IroDump_Print("ReverseOp For Monmin at: %d\n", node->index);
                                break;
                        }
                    }
                }
                break;
            }
            case IROLinearOperand:
                if (!(node->flags & IROLF_Reffed) && node->u.node->type != EINSTRUCTION) {
                    node->type = IROLinearNop;
                }
                break;
        }
    }
    IroVars_CheckTimedLongjmp();
}

/* the opcode reversal table just before it */
void collect_eadd_terms(ENode *n)
{
    if (n->type == EADD) {
        collect_eadd_terms(n->data.diadic.left);
        collect_eadd_terms(n->data.diadic.right);
    } else {
        ENodeList *s = (ENodeList *)oalloc(8);
        ENodeList *tail;
        s->node = n;
        s->next = NULL;
        if (eadd_terms != NULL) {
            tail = eadd_terms_tail;
            tail->next = s;
        } else {
            eadd_terms = s;
        }
        eadd_terms_tail = s;
    }
}

ENode *IroTransform_CombineEAddTerms(ENode *expression)
{
    ENode *root = expression;
    ENode *found;
    ENode *result;
    CInt64 sum;
    char isZero;
    ENode *combined;
    ENode *constant;
    ENode *current;
    Type *bestType;
    ENodeList *node;
    ENodeList *previous;
    ENode *left;

    eadd_terms = eadd_terms_tail = NULL;
    collect_eadd_terms(root->data.diadic.left);
    collect_eadd_terms(root->data.diadic.right);

#if VERSION >= VERSION_GC_1_3
    for (node = eadd_terms, previous = NULL, found = NULL; node != NULL; node = node->next) {
#else
    found = NULL;
    for (node = eadd_terms, previous = NULL; node != NULL; node = node->next) {
#endif
        current = node->node;
        if (current->type == EOBJREF) {
            found = current;
            if (previous != NULL)
                previous->next = node->next;
            else
                eadd_terms = node->next;
            break;
        }
        previous = node;
    }
    if (found == NULL) {
        for (node = eadd_terms, previous = NULL; node != NULL; node = node->next) {
            current = node->node;
            if (current->type == EINDIRECT) {
                found = current;
                if (previous != NULL)
                    previous->next = node->next;
                else
                    eadd_terms = node->next;
                break;
            }
            previous = node;
        }
    }
    previous = NULL;
#if VERSION >= VERSION_GC_1_3
    sum.lo = 0;
    sum.hi = 0;
    bestType = NULL;
#else
    bestType = NULL;
    sum.lo = 0;
    sum.hi = 0;
#endif
    for (node = eadd_terms; node != NULL; node = node->next) {
        current = node->node;
        if (current->type == EINTCONST && current->rtype != NULL) {
            if (bestType == NULL || bestType->size < current->rtype->size)
                bestType = current->rtype;
            sum = CInt64_Add(sum, current->data.intval);
            if (previous != NULL)
                previous->next = node->next;
            else
                eadd_terms = node->next;
        } else {
            if (current->type == EMUL && current->data.diadic.right->type == EINTCONST && current->rtype != NULL) {
                if (bestType == NULL || bestType->size < current->rtype->size)
                    bestType = current->rtype;
                left = current->data.diadic.left;
                if (left->type == EADD && left->data.diadic.right->type == EINTCONST) {
                    sum = CInt64_Add(sum, CInt64_MulU(left->data.diadic.right->data.intval,
                                                      current->data.diadic.right->data.intval));
                    current->data.diadic.left = left->data.diadic.left;
                }
            }
            previous = node;
        }
    }
    result = NULL;
    if (found != NULL) {
        result = found;
        isZero = (sum.hi == 0 && sum.lo == 0);
        if (!isZero) {
            combined = IrOptimizer_NewENode(15);
            combined->data.diadic.left = found;
            combined->data.diadic.right = IrOptimizer_NewENode(50);
            constant = combined->data.diadic.right;
            constant->data.intval = sum;
            combined->data.diadic.right->rtype = bestType;
            combined->rtype = found->rtype;
            combined->cost = 1;
            sum.lo = 0;
            sum.hi = 0;
            result = combined;
        }
    }
    for (node = eadd_terms; node != NULL; node = node->next) {
        current = node->node;
        if (result != NULL) {
            combined = IrOptimizer_NewENode(15);
            combined->data.diadic.left = result;
            combined->data.diadic.right = current;
            combined->cost = result->cost + 1;
            combined->rtype = result->rtype;
            result = combined;
        } else {
            result = current;
        }
    }
    isZero = (sum.hi == 0 && sum.lo == 0);
    if (!isZero) {
        combined = IrOptimizer_NewENode(15);
        combined->data.diadic.left = result;
        combined->data.diadic.right = IrOptimizer_NewENode(50);
        constant = combined->data.diadic.right;
        constant->data.intval = sum;
        combined->data.diadic.right->rtype = bestType;
        combined->cost = result->cost + 1;
        combined->rtype = result->rtype;
        result = combined;
    }
    return result;
}

ENode *canonicalize_diadic_expression(ENode *expression)
{
    switch (expression->type) {
        case EINDIRECT:
            if (expression->data.diadic.left->type == EADD)
                expression->data.diadic.left = IroTransform_CombineEAddTerms(expression->data.diadic.left);
            break;
        case EMUL:
        case EADD:
        case EAND:
        case EXOR:
        case EOR:
            if (expression->rtype->type == TYPEINT && expression->data.diadic.right->type != EINTCONST &&
                expression->data.diadic.left->type == EINTCONST) {
                ENode *left = expression->data.diadic.left;
                expression->data.diadic.left = expression->data.diadic.right;
                expression->data.diadic.right = left;
            }
            break;
        case EEQU:
        case ENOTEQU:
            if (expression->rtype->type == TYPEINT && expression->data.diadic.right->type != EINTCONST &&
                expression->data.diadic.left->type == EINTCONST) {
                ENode *left = expression->data.diadic.left;
                expression->data.diadic.left = expression->data.diadic.right;
                expression->data.diadic.right = left;
            }
            if (expression->data.diadic.right->type == EINTCONST && expression->data.diadic.left->type == EBINNOT) {
                ENode *operand = expression->data.diadic.left;
                expression->data.diadic.left = operand->data.diadic.left;
                operand->data.diadic.left = expression->data.diadic.right;
                expression->data.diadic.right = operand;
            }
            break;
    }
    return expression;
}


#if VERSION >= VERSION_GC_1_3
extern void CLIO_ReportAssertionFailure(char *a, char *b, unsigned int c);

struct Fn00482e40 {
    char pad[8];
    UInt8 value;
};

#endif

#if VERSION >= VERSION_GC_1_3
extern void *fn_00529870(int size);

struct Fn00480380 {
    SInt32 a;
    SInt32 b;
};

struct Fn004737c0 {
    SInt32 a;
    SInt32 b;
    UInt8 c;
};

struct Fn0047b330 {
    SInt32 a;
    SInt32 b;
    SInt32 c;
};

#endif

#if VERSION >= VERSION_GC_1_3
extern void *data_005a9534;
extern void *data_005a951c;

struct Fn004719b0 {
    void *first;
    void *rtype;
};

struct Fn00473800 {
    SInt32 a;
    SInt32 b;
    UInt8 c;
};

struct Fn0047e400 {
    SInt32 a;
    SInt32 b;
    SInt32 c;
    SInt32 d;
};

#endif

#if VERSION >= VERSION_GC_1_3
extern void fn_005297b0(void *);
extern void fn_00529700(void);

#endif

#if VERSION >= VERSION_GC_1_3
struct Fn004832f0 {
    SInt32 a;
    SInt32 b;
};

struct Fn0048ecf0 {
    SInt32 a;
    SInt32 b;
    SInt32 c;
};

#endif

#if VERSION >= VERSION_GC_1_3
extern void fn_00474400(void *, void *);
extern void fn_00476320(void *, void *);

struct Fn004762b0 {
    SInt32 *pte;
    SInt32 otherPtes;
};

struct Fn00477bc0 {
    void *first;
    void *rtype;
    SInt32 third;
    SInt32 fourth;
};

struct Fn0047ba30 {
    SInt32 a;
    SInt32 b;
    SInt32 c;
    SInt32 d;
};

#endif

#if VERSION >= VERSION_GC_1_3
extern void fn_0047afa0(void *, void *);

#endif

#if VERSION >= VERSION_GC_1_3
extern char data_00565458[];

/* initialize a table of 0x4d per-node-type entries */
void fn_00456bd0(void)
{
    int i;

    for (i = 0; i < 0x4d; i++)
        data_00565458[i] = 0;
    data_00565458[0] = 0;
    data_00565458[1] = 0;
    data_00565458[2] = 0;
    data_00565458[3] = 0;
    data_00565458[4] = 0;
    data_00565458[5] = 1;
    data_00565458[6] = 1;
    data_00565458[7] = 1;
    data_00565458[8] = 0;
    data_00565458[9] = 1;
    data_00565458[10] = 1;
    data_00565458[11] = 1;
    data_00565458[12] = 1;
    data_00565458[13] = 1;
    data_00565458[14] = 1;
    data_00565458[15] = 1;
    data_00565458[16] = 1;
    data_00565458[17] = 1;
    data_00565458[18] = 1;
    data_00565458[19] = 0;
    data_00565458[20] = 0;
    data_00565458[21] = 0;
    data_00565458[22] = 0;
    data_00565458[23] = 0;
    data_00565458[24] = 0;
    data_00565458[25] = 1;
    data_00565458[26] = 1;
    data_00565458[27] = 1;
    data_00565458[28] = 0;
    data_00565458[29] = 0;
    data_00565458[30] = 0;
    data_00565458[31] = 0;
    data_00565458[32] = 0;
    data_00565458[33] = 0;
    data_00565458[34] = 0;
    data_00565458[35] = 0;
    data_00565458[36] = 0;
    data_00565458[37] = 0;
    data_00565458[38] = 0;
    data_00565458[39] = 0;
    data_00565458[40] = 0;
    data_00565458[41] = 0;
    data_00565458[42] = 0;
    data_00565458[43] = 0;
    data_00565458[44] = 0;
    data_00565458[45] = 0;
    data_00565458[46] = 0;
    data_00565458[47] = 0;
    data_00565458[48] = 0;
    data_00565458[49] = 0;
    data_00565458[50] = 0;
    data_00565458[51] = 0;
    data_00565458[52] = 0;
    data_00565458[53] = 0;
    data_00565458[54] = 0;
    data_00565458[55] = 0;
    data_00565458[56] = 0;
    data_00565458[57] = 0;
    data_00565458[58] = 0;
    data_00565458[59] = 0;
    data_00565458[60] = 0;
    data_00565458[61] = 0;
    data_00565458[62] = 0;
    data_00565458[63] = 0;
    data_00565458[64] = 0;
    data_00565458[65] = 0;
    data_00565458[66] = 0;
    data_00565458[68] = 0;
    data_00565458[69] = 0;
    data_00565458[70] = 0;
    data_00565458[71] = 0;
    data_00565458[72] = 0;
    data_00565458[73] = 0;
    data_00565458[74] = 0;
    data_00565458[75] = 0;
    data_00565458[76] = 0;
}
#endif

ENode *walk_expr_postorder(ENode *expr)
{
    ENodeList *arg;
    switch (expr->type) {
        case EPOSTINC:
        case EPOSTDEC:
        case EPREINC:
        case EPREDEC:
        case EINDIRECT:
        case EMONMIN:
        case EBINNOT:
        case ELOGNOT:
        case EFORCELOAD:
        case ETYPCON:
        case EBITFIELD:
            expr->data.monadic = walk_expr_postorder(expr->data.monadic);
            break;
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
            expr->data.diadic.left = walk_expr_postorder(expr->data.diadic.left);
            expr->data.diadic.right = walk_expr_postorder(expr->data.diadic.right);
            break;
        case EFUNCCALL:
        case EFUNCCALLP:
            walk_expr_postorder(expr->data.funccall.funcref);
            arg = expr->data.funccall.args;
            while (arg) {
                walk_expr_postorder(arg->node);
                arg = arg->next;
            }
            break;
        case ECOND:
            walk_expr_postorder(expr->data.cond.cond);
            walk_expr_postorder(expr->data.cond.expr1);
            walk_expr_postorder(expr->data.cond.expr2);
            break;
        case ENULLCHECK:
            walk_expr_postorder(expr->data.diadic.left);
            walk_expr_postorder(expr->data.diadic.right);
            break;
    }
    return canonicalize_diadic_expression(expr);
}

void fold_nested_diadic_intval(ENode *node)
{
    int operation;
    short nestedKind;
    ENodeType kind, leftKind;
    unsigned char changed;
    CInt64 rightValue, leftValue;
    if ((node->type == EADD || node->type == EMUL || (node->type == EAND || node->type == EXOR || node->type == EOR) ||
         (node->type == ESHL || node->type == ESHR)) &&
        node->rtype->type == TYPEINT && node->data.diadic.right->type == EINTCONST) {
        do {
            changed = 0;
            kind = node->type;
            leftKind = node->data.diadic.left->type;
            if (leftKind == kind && node->data.diadic.left->data.diadic.right->type == EINTCONST) {
                rightValue = node->data.diadic.right->data.intval;
                leftValue = node->data.diadic.left->data.diadic.right->data.intval;
                switch ((unsigned char)kind) {
                    case EADD:
                    case ESHL:
                    case ESHR:
                        operation = '+';
                        break;
                    case EMUL:
                        operation = '*';
                        break;
                    case EAND:
                        operation = '&';
                        break;
                    case EOR:
                        operation = '|';
                        break;
                    case EXOR:
                        operation = '^';
                        break;
                    default:
                        return;
                }
                node->data.diadic.right->data.intval =
                    CMach_CalcIntDiadic(node->rtype, rightValue, operation, leftValue);
                node->data.diadic.left = node->data.diadic.left->data.diadic.left;
                changed = 1;
            } else {
                if ((short)kind != 25 && (short)kind != 27)
                    continue;
                if (((short)leftKind != 25 && (short)leftKind != 27) ||
                    ((nestedKind = node->data.diadic.left->data.diadic.left->type) != 25 && nestedKind != 27) ||
                    node->data.diadic.left->data.diadic.left->data.diadic.right->type != EINTCONST ||
                    !CInt64_Equal(node->data.diadic.right->data.intval,
                                  node->data.diadic.left->data.diadic.left->data.diadic.right->data.intval))
                    continue;
                if ((short)kind == nestedKind) {
                    node->data.diadic.left->data.diadic.left =
                        node->data.diadic.left->data.diadic.left->data.diadic.left;
                    changed = 1;
                } else if ((short)leftKind == nestedKind) {
                    *node = *node->data.diadic.right;
                    changed = 1;
                } else {
                    node->data.diadic.left = node->data.diadic.left->data.diadic.right;
                    changed = 1;
                }
            }
        } while (changed != 0);
    }
}
