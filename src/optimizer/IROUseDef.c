#define CERROR_FILE "IROUseDef.c"
#include "compiler/common.h"
#include "compiler/IROUseDef.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/types.h"
#include "compiler/BitVector.h"
#include "compiler/CError.h"
#include "compiler/CExpr2.h"
#include "compiler/CFunc.h"
#include "compiler/CInt64.h"
#include "compiler/CParser.h"
#include "compiler/CompilerTools.h"
#include "compiler/IrOptimizer.h"
#include "compiler/IroBitVect.h"
#include "compiler/IroDump.h"
#include "compiler/IroLoop.h"
#include "compiler/IroUtil.h"
#include "compiler/IroVars.h"

struct BitVector *data_0058711c;
struct BitVector *data_00587174;
struct BitVector *data_00587f70;
struct BitVector *connected_defs_and_uses_bits;

static SInt32 def_count;
static struct IRODef *def_list;
static struct IRODef *global_def_tail;
static int data_00580624;
#if VERSION >= VERSION_GC_1_3
/* (1.3: IroPropagate.c uses these) */
struct IROUse *allocated_uses;
struct IROUse *global_use_tail;
#else
static struct IROUse *allocated_uses;
static struct IROUse *global_use_tail;
#endif
static struct BitVector *use_def_in;
static struct BitVector *used_defs_bitvector;
static SInt32 data_00580638;

#define BVTEST(bv, i)                                                                                                  \
    (((UInt32)((SInt32)(i) >> 5) < (bv)->size) &&                                                                      \
     (((bv)->bits[(UInt32)((SInt32)(i) >> 5)] & ((UInt32)1 << ((i) & 31)))) != 0)

static int has_side_effect(IROLinear *p);
static int is_side_effect_node(IROLinear *p);
static int is_volatile_object_ref(IROLinear *p);
static int is_object_ref(IROLinear *p);
static int has_volatile_type(IROLinear *p);
static int is_call_or_assignment(IROLinear *p);
static Boolean IRO_NodeKind(IROLinear *n);

static inline void PrintUseDefValue(char *format, BitVector *value)
{
    IroDump_PrintBitSet(format, value);
}

static inline int ReverseEqual(BitVector *out, BitVector *reaching)
{
    return IroBitVect_AreEqual(reaching, out);
}

/* Records a definition of VAR by LINEAR (null: on entry), at the end of the global list and the front of VAR's. */
static inline void AddUse(IRONode *node, IROLinear *linear, VarRecord *var)
{
    IROUse *use;
    use = (IROUse *)oalloc(sizeof(IROUse));
    use->index = data_00580624++;
    use->node = node;
    use->linear = linear;
    use->var = var;
    use->globalnext = NULL;
    use->reachingDefCount = 0;
    if (allocated_uses != NULL)
        global_use_tail->globalnext = use;
    else
        allocated_uses = use;
    global_use_tail = use;
    use->varnext = var->uses;
    var->uses = use;
}

void fn_0045ac60(IROLinear *p, int flag)
{
    if (flag) {
        if (has_side_effect(p)) {
            if (data_00580638 == 0)
                p->flags &= ~IROLF_Reffed;
            data_00580638++;
        }
    } else {
        if (has_side_effect(p))
            data_00580638--;
        else if (data_00580638 == 0)
            p->type = IROLinearNop;
    }
}

#if VERSION >= VERSION_GC_1_3
extern UInt8 data13_005e7380[];

/* initialize a table of 0x4d per-node-type entries */
void fn_0045f8c0(void)
{
    int i;

    for (i = 0; i < 0x4d; i++)
        data13_005e7380[i] = 77;
    data13_005e7380[0] = 77;
    data13_005e7380[1] = 77;
    data13_005e7380[2] = 77;
    data13_005e7380[3] = 77;
    data13_005e7380[4] = 77;
    data13_005e7380[5] = 77;
    data13_005e7380[6] = 77;
    data13_005e7380[7] = 77;
    data13_005e7380[8] = 77;
    data13_005e7380[9] = 77;
    data13_005e7380[10] = 77;
    data13_005e7380[11] = 77;
    data13_005e7380[12] = 77;
    data13_005e7380[13] = 77;
    data13_005e7380[14] = 77;
    data13_005e7380[15] = 77;
    data13_005e7380[16] = 77;
    data13_005e7380[17] = 77;
    data13_005e7380[18] = 77;
    data13_005e7380[19] = 77;
    data13_005e7380[20] = 77;
    data13_005e7380[21] = 77;
    data13_005e7380[22] = 77;
    data13_005e7380[23] = 77;
    data13_005e7380[24] = 77;
    data13_005e7380[25] = 77;
    data13_005e7380[26] = 77;
    data13_005e7380[27] = 77;
    data13_005e7380[28] = 77;
    data13_005e7380[29] = 77;
    data13_005e7380[30] = 77;
    data13_005e7380[31] = 9;
    data13_005e7380[32] = 11;
    data13_005e7380[33] = 12;
    data13_005e7380[34] = 15;
    data13_005e7380[35] = 16;
    data13_005e7380[36] = 17;
    data13_005e7380[37] = 18;
    data13_005e7380[38] = 25;
    data13_005e7380[39] = 26;
    data13_005e7380[40] = 27;
    data13_005e7380[41] = 77;
    data13_005e7380[42] = 77;
    data13_005e7380[43] = 77;
    data13_005e7380[44] = 77;
    data13_005e7380[45] = 77;
    data13_005e7380[46] = 77;
    data13_005e7380[47] = 77;
    data13_005e7380[48] = 77;
    data13_005e7380[49] = 77;
    data13_005e7380[50] = 77;
    data13_005e7380[51] = 77;
    data13_005e7380[52] = 77;
    data13_005e7380[53] = 77;
    data13_005e7380[54] = 77;
    data13_005e7380[55] = 77;
    data13_005e7380[56] = 77;
    data13_005e7380[57] = 77;
    data13_005e7380[58] = 77;
    data13_005e7380[59] = 77;
    data13_005e7380[60] = 77;
    data13_005e7380[61] = 77;
    data13_005e7380[62] = 77;
    data13_005e7380[63] = 77;
    data13_005e7380[64] = 77;
    data13_005e7380[65] = 77;
    data13_005e7380[66] = 77;
    data13_005e7380[68] = 77;
    data13_005e7380[69] = 77;
    data13_005e7380[70] = 77;
    data13_005e7380[71] = 77;
    data13_005e7380[72] = 77;
    data13_005e7380[73] = 77;
    data13_005e7380[74] = 77;
    data13_005e7380[75] = 77;
    data13_005e7380[76] = 77;
}
extern Boolean fn_0045e110(IRODef *def);

/* can the use be rewritten without crossing a reference to its variable */
Boolean fn_0045e1a0(IROUse *use)
{
    VarRecord *vr = use->var;
    IROLinear *l;
    IROLinear *right;

    if (vr != NULL) {
        l = use->linear;
        if (l != NULL && (l->flags & 0x4000))
            return 0;
        while (l != NULL && (l->flags & 2))
            l = l->next;
        if (l != NULL && (UInt8)fn_0044d460(l) && IroVars_GetOperandVarRecord(l) == vr && !fn_0044be00(l->u.diadic.left)) {
            right = (l->type == IROLinearOp1Arg) ? NULL : l->u.diadic.right;
            if (right == NULL || !fn_0044be00(right))
                return 1;
        }
    }
    return 0;
}

/* every use reached by a loop definition of the variable must also be inside a loop and pass fn_0045e1a0 */
Boolean fn_0045e110(IRODef *def)
{
    VarRecord *vr = def->var;
    IRODef *d;
    IROUse *u;

    if (vr != NULL) {
        for (d = vr->defs; d != NULL; d = d->varnext) {
            if (d->f04 != 0 && ((IRONode *)d->f04)->loopdepth != 0) {
                for (u = vr->uses; u != NULL; u = u->varnext) {
                    if ((d->index >> 5) < u->reachingDefs->size && ((1 << d->index) & u->reachingDefs->bits[d->index >> 5])) {
                        if (u->node == NULL || u->node->loopdepth == 0 || !fn_0045e1a0(u))
                            return 0;
                    }
                }
            }
        }
        return 1;
    }
    return 0;
}

/* flag the other definitions of variables whose loop definitions can all be forwarded */
void fn_0045e090(void)
{
    IRODef *d;
    IRODef *x;

    for (d = def_list; d != NULL; d = d->globalnext) {
        if (!d->global && !d->noregister && d->f04 != 0 && ((IRONode *)d->f04)->loopdepth != 0 && !d->f1d &&
            fn_0045e110(d) && d->var != NULL) {
            for (x = d->var->defs; x != NULL; x = x->varnext) {
                if (x->f04 != 0 && ((IRONode *)x->f04)->loopdepth != 0)
                    x->f1d = 1;
            }
        }
    }
}
#endif

#if VERSION >= VERSION_GC_1_3
extern char *data_005dc070[0x4d];
void fn_0045f520(void)
{
    int i;

    for (i = 0; i < 0x4d; i++)
        data_005dc070[i] = 0;
    data_005dc070[0] = 0;
    data_005dc070[1] = 0;
    data_005dc070[2] = 0;
    data_005dc070[3] = 0;
    data_005dc070[4] = 0;
    data_005dc070[5] = 0;
    data_005dc070[6] = 0;
    data_005dc070[7] = 0;
    data_005dc070[8] = 0;
    data_005dc070[9] = 0;
    data_005dc070[10] = 0;
    data_005dc070[11] = 0;
    data_005dc070[12] = 0;
    data_005dc070[13] = 0;
    data_005dc070[14] = 0;
    data_005dc070[15] = 0;
    data_005dc070[16] = 0;
    data_005dc070[17] = 0;
    data_005dc070[18] = 0;
    data_005dc070[19] = 0;
    data_005dc070[20] = 0;
    data_005dc070[21] = 0;
    data_005dc070[22] = 0;
    data_005dc070[23] = 0;
    data_005dc070[24] = 0;
    data_005dc070[25] = 0;
    data_005dc070[26] = 0;
    data_005dc070[27] = 0;
    data_005dc070[28] = 0;
    data_005dc070[29] = 0;
    data_005dc070[30] = 0;
    data_005dc070[31] = (char *)CInt64_Mul;
    data_005dc070[32] = (char *)CInt64_Mul;
    data_005dc070[33] = 0;
    data_005dc070[34] = (char *)CInt64_Add;
    data_005dc070[35] = (char *)CInt64_Add;
    data_005dc070[36] = (char *)CInt64_Add;
    data_005dc070[37] = (char *)CInt64_Add;
    data_005dc070[38] = (char *)CInt64_And;
    data_005dc070[39] = (char *)CInt64_Xor;
    data_005dc070[40] = (char *)CInt64_Or;
    data_005dc070[41] = 0;
    data_005dc070[42] = 0;
    data_005dc070[43] = 0;
    data_005dc070[44] = 0;
    data_005dc070[45] = 0;
    data_005dc070[46] = 0;
    data_005dc070[47] = 0;
    data_005dc070[48] = 0;
    data_005dc070[49] = 0;
    data_005dc070[50] = 0;
    data_005dc070[51] = 0;
    data_005dc070[52] = 0;
    data_005dc070[53] = 0;
    data_005dc070[54] = 0;
    data_005dc070[55] = 0;
    data_005dc070[56] = 0;
    data_005dc070[57] = 0;
    data_005dc070[58] = 0;
    data_005dc070[59] = 0;
    data_005dc070[60] = 0;
    data_005dc070[61] = 0;
    data_005dc070[62] = 0;
    data_005dc070[63] = 0;
    data_005dc070[64] = 0;
    data_005dc070[65] = 0;
    data_005dc070[66] = 0;
    data_005dc070[68] = 0;
    data_005dc070[69] = 0;
    data_005dc070[70] = 0;
    data_005dc070[71] = 0;
    data_005dc070[72] = 0;
    data_005dc070[73] = 0;
    data_005dc070[74] = 0;
    data_005dc070[75] = 0;
    data_005dc070[76] = 0;
}
#endif

static int has_side_effect(IROLinear *p)
{
    return is_side_effect_node(p) || is_volatile_object_ref(p);
}

static int is_side_effect_node(IROLinear *p)
{
    return is_call_or_assignment(p) || has_volatile_type(p);
}

static int is_volatile_object_ref(IROLinear *p)
{
    return is_object_ref(p) && is_volatile_object(p->u.node->data.objref);
}

static int is_object_ref(IROLinear *p)
{
    return p->type == IROLinearOperand && p->u.node->type == EOBJREF;
}

static int has_volatile_type(IROLinear *p)
{
    return p->rtype && CParser_IsVolatile(p->rtype, p->nodeflags & 3);
}

static int is_call_or_assignment(IROLinear *p)
{
    return p->type == IROLinearFunccall || fn_0044d460(p);
}

#if VERSION >= VERSION_GC_1_3
void create_def_record(VarRecord *var, struct IROLinear *linear, struct IRONode *node, unsigned char definite)
#else
void create_def_record(VarRecord *var, struct IROLinear *linear, unsigned char definite)
#endif
{
    IRODef *def;

    def = oalloc(sizeof(IRODef));
    def->index = def_count;
    def_count++;
#if VERSION >= VERSION_GC_1_3
    def->f04 = (UInt32)node;
#endif
    def->linear = linear;
    def->var = var;
    def->globalnext = NULL;
    def->useCount = 0;
    def->global = (var->object->datatype == DDATA);
    def->noregister = var->noregister;
    def->definite = definite;
#if VERSION >= VERSION_GC_1_3
    def->f1d = 0;
#endif
    if (def_list != NULL) {
        global_def_tail->globalnext = def;
    } else {
        def_list = def;
    }
    global_def_tail = def;
    def->varnext = var->defs;
    var->defs = def;
}

void build_use_def_records(void)
{
    VarRecord *entry;
    IRONode *owner;
    IROLinear *member;
    SInt32 index;
    AsmOut list;
    ENode *expression;
    Object *object;

    def_count = 0;
    data_00580624 = 0;
    allocated_uses = NULL;
    def_list = NULL;

    for (entry = var_records; entry; entry = entry->next) {
        entry->defs = NULL;
        entry->uses = NULL;
        entry->usedAtCall = 0;
    }

    for (owner = iro_flowgraph_head; owner; owner = owner->nextnode) {
        for (member = owner->first; member; member = member->next) {
            if (member->type == IROLinearOperand && (expression = member->u.node)->type == EOBJREF) {
                object = expression->data.objref;
                if ((member->flags & IROLF_Ind) != 0 &&
                    ((member->flags & IROLF_Assigned) == 0 || (member->flags & IROLF_Used) != 0)) {
                    entry = fn_0044ba70(object, 0, 1);
                    if (entry)
                        AddUse(owner, member, entry);
                }
            }
            if (fn_0044d460(member)) {
                entry = IroVars_GetOperandVarRecord(member);
                if (entry)
                    CREATE_DEF_RECORD(entry, member, owner, member->rtype->size == (UInt32)entry->object->type->size);
            }
            if (member->type == IROLinearAsm) {
                fn_00462d70(member->u.asm_stmt, &list);
                for (index = 0; index < list.numoperands; index++) {
                    entry = fn_0044ba70(list.operands[index].object, 0, 1);
                    switch (list.operands[index].type) {
                        case 0:
                            AddUse(owner, member, entry);
                            break;
                        case 1:
                            CREATE_DEF_RECORD(entry, member, owner,
                                              list.operands[index].offset == 0 &&
                                                  list.operands[index].size == (UInt32)entry->object->type->size);
                            break;
                        case 2:
                            CREATE_DEF_RECORD(entry, member, owner, 0);
                            break;
                    }
                }
            }
            if (member == owner->last)
                break;
        }
    }

    for (entry = var_records; entry; entry = entry->next)
        CREATE_DEF_RECORD(entry, NULL, NULL, 1);
}

/* (IroVars_VisitExceptionOperands visitor at a call) the object's reaching definitions are used there. */
void mark_var_used_at_call(Object *obj)
{
    IRODef *u;
    VarRecord *si;

    if ((si = fn_0044ba70(obj, 0, 1)) != NULL) {
        for (u = si->defs; u != NULL; u = u->varnext) {
            if ((u->index >> 5) < use_def_in->size && (use_def_in->bits[u->index >> 5] & (1u << (u->index & 31)))) {
                IroBitVect_SetBit(u->index, used_defs_bitvector);
                u->useCount++;
            }
        }
        si->usedAtCall = 1;
    }
}

/* The amount a propagated definition adds: the step of an increment (scaled for a pointer) or the constant of
   an add or subtract. */
CInt64 get_update_delta(IROLinear *node)
{
    CInt64 r;
    CInt64 s;
    SInt32 size;

    if (node->type == IROLinearOp1Arg) {
        switch (node->nodetype) {
            case EPOSTINC:
            case EPREINC:
                r.lo = 1;
                r.hi = 0;
                break;
            case EPOSTDEC:
            case EPREDEC:
                r.lo = -1;
                r.hi = -1;
                break;
            default:
                CError_FATAL(BY_VERSION(399, 445));
        }
        if (node->rtype->type == TYPEPOINTER) {
            size = ((TypePointer *)node->rtype)->target->size;
            s.lo = size;
            s.hi = (size < 0) ? -1 : 0;
            r = CInt64_Mul(r, s);
        }
    } else if (node->type == IROLinearOp2Arg) {
        switch (node->nodetype) {
            case EADDASS:
                r = node->u.diadic.right->u.node->data.intval;
                break;
            case ESUBASS:
                r = CInt64_Neg(node->u.diadic.right->u.node->data.intval);
                break;
            default:
                CError_FATAL(424);
        }
    } else {
        CError_FATAL(BY_VERSION(429, 496));
    }
    return r;
}

/* Adds VALUE (of TYPE) to the use EXPRESSION: folds it into the constant of an enclosing increment or constant
   add or subtract, or wraps EXPRESSION in a new add. */
void add_constant_to_next_use(IROLinear *expression, CInt64 value, Type *type)
{
    IROLinear *node = IroUtil_FindNextUse(expression);

    if (node != NULL) {
        switch (node->type) {
            case IROLinearOp1Arg:
                switch (node->nodetype) {
                    case EPOSTINC:
                    case EPOSTDEC:
                    case EPREINC:
                    case EPREDEC:
                        value = CInt64_Add(value, get_update_delta(node));
                        node->nodetype = EADDASS;
                        node->type = IROLinearOp2Arg;
                        {
                            IROLinear *constant = IroVars_CreateIntConstant(value, type);
                            node->u.diadic.right = constant;
                            IroUtil_InsertLinearRangeAfter(constant, constant, expression);
                        }
                        return;
                }
                break;
            case IROLinearOp2Arg:
                if (IroDump_IsType1NodeType50(node->u.diadic.right) != 0) {
                    switch (node->nodetype) {
                        case EADD:
                        case EADDASS: {
                            IROLinear *constant = node->u.diadic.right;
                            ENode *constantExpression = constant->u.node;
                            node->u.diadic.right->u.node->data.intval =
                                CInt64_Add(constantExpression->data.intval, value);
                            return;
                        }
                        case ESUB:
                        case ESUBASS: {
                            IROLinear *constant = node->u.diadic.right;
                            ENode *constantExpression = constant->u.node;
                            node->u.diadic.right->u.node->data.intval =
                                CInt64_Sub(constantExpression->data.intval, value);
                            return;
                        }
                    }
                }
                break;
        }
    }

    {
        IROLinear *operation;
        IROLinear *constant;
        IROLinear *constantNode;
        constant = IroVars_CreateIntConstant(value, type);
        constantNode = constant;
        operation = IrOptimizer_NewLinear(IROLinearOp2Arg);
#if VERSION >= VERSION_GC_1_3
        operation->index = ++linear_index_counter;
#endif
        operation->nodetype = EADD;
        operation->u.diadic.left = expression;
        operation->u.diadic.right = constant;
        operation->rtype = expression->rtype;
        constantNode->next = operation;
        IroUtil_ReplaceNextReference(expression, operation);
        IroUtil_InsertLinearRangeAfter(constant, operation, expression);
    }
}

Boolean propagate_inc_dec(void)
{
    Boolean removed = 0;
    CInt64 value;
    IRODef *def;

    for (def = def_list; def != NULL; def = def->globalnext) {
        IROUse *use;
        IROUse *matchingUse;
        IROLinear *parent;
        IROLinear *ancestor;
        UInt32 live;

        if (def->linear == NULL || def->definite == 0)
            continue;
        if (!IRO_NodeKind(def->linear))
            continue;
        if (def->linear->flags & IROLF_Reffed)
            continue;

        if ((def->linear->rtype->type == TYPEINT || def->linear->rtype->type == TYPEENUM ||
             def->linear->rtype->type == TYPEPOINTER) &&
            IroUtil_AreTypesEqual(def->linear->rtype, def->var->object->type) &&
            !is_volatile_object(def->var->object)) {
            live = 0;
            for (use = def->var->uses; use != NULL; use = use->varnext) {
                if ((UInt32)(def->index >> 5) < use->reachingDefs->size &&
                    ((1u << (def->index & 31)) & use->reachingDefs->bits[def->index >> 5])) {
                    if (use->reachingDefCount != 1 || use->linear->type != IROLinearOperand ||
                        (parent = IroUtil_FindNextUse(use->linear)) == NULL || !IroDump_GetObjRef(parent) ||
                        !IroUtil_AreTypesEqual(def->linear->rtype, parent->rtype) ||
                        ((use->linear->flags & IROLF_Assigned) &&
                         ((ancestor = IroUtil_FindNextUse(parent)) == NULL || !IRO_NodeKind(ancestor) ||
                          (ancestor->flags & IROLF_Reffed))))
                        goto skipDefinition;
                    live++;
                }
            }
            if (live == def->useCount) {
                for (use = def->var->uses; use != NULL; use = use->varnext) {
                    if ((UInt32)(def->index >> 5) < use->reachingDefs->size &&
                        ((1u << (def->index & 31)) & use->reachingDefs->bits[def->index >> 5])) {
                        Type *type;
                        IroDump_Print("Propagating inc/dec from %d to %d\n", def->linear->index, use->linear->index);
                        parent = IroUtil_FindNextUse(use->linear);
                        value = get_update_delta(def->linear);
                        type = def->linear->rtype;
                        if (type->type == TYPEPOINTER)
                            type = (Type *)&stunsignedlong;
                        add_constant_to_next_use(parent, value, type);
                        removed = 1;
                        for (matchingUse = def->var->uses; matchingUse != NULL; matchingUse = matchingUse->varnext) {
                            if (matchingUse->linear == def->linear->u.monadic->u.monadic) {
                                use->reachingDefCount = matchingUse->reachingDefCount;
                                IroBitVect_CopyBitVector(matchingUse->reachingDefs, use->reachingDefs);
                                break;
                            }
                        }
                    }
                }
                def->useCount = 0;
                data_00580638 = -1;
                IroUtil_VisitLinearTree(def->linear, fn_0045ac60);
                def->linear->type = IROLinearNop;
                IroDump_Print("Removing deadddd assignment %d\n", def->linear->index);
            }
        }
    skipDefinition:;
    }
    return removed;
}

/* a definition worth propagating: an increment (0..2) or a constant add or subtract (0x22, 0x23) of a variable
   that is not a bit field */
static Boolean IRO_NodeKind(IROLinear *n)
{
    switch (n->type) {
        case IROLinearOp1Arg:
            if (n->nodetype != EPOSTINC && n->nodetype > 2)
                return 0;
            if (n->u.monadic->flags & 0x80000)
                return 0;
            return 1;
        case IROLinearOp2Arg:
            if ((n->nodetype == EADDASS || n->nodetype == ESUBASS) && IroDump_IsType1NodeType50(n->u.diadic.right) &&
                !(n->u.diadic.left->flags & 0x80000))
                return 1;
            return 0;
    }
    return 0;
}

SInt32 IRO_UseDef(UInt8 eliminateUnused, UInt8 simplifyUses)
{
    IROLinear *node;
    IROLinear *currentNode;
    IRONode *block;
    IRODef *addressTakenVar;
    IROUse *definition;
    IRODef *variable;
    IRODef *callVar;
    IRODef *reference;
    VarRecord *entry;
    SInt32 changed;
    SInt32 i;
    SInt32 result;
    IROUse *allocatedDef;
    BitVector *reachingDefs;
    BitVector *usedDefs;
    BitVector *callDefs;
    AsmOut buffer;

    build_use_def_records();
    IroVars_CheckTimedLongjmp();
    for (allocatedDef = allocated_uses; allocatedDef != NULL; allocatedDef = allocatedDef->globalnext)
        IroBitVect_AllocateBitVector(&allocatedDef->reachingDefs, def_count);
    IroBitVect_AllocateBitVector(&used_defs_bitvector, def_count);
    IroBitVect_AllocateBitVector(&callDefs, def_count);
    IroVars_CheckTimedLongjmp();
    for (variable = def_list; variable != NULL; variable = variable->globalnext)
        if (variable->global || variable->noregister)
            IroBitVect_SetBit(variable->index, callDefs);
    IroBitVect_AllocateBitVector(&usedDefs, def_count);
    variable = def_list;
    for (block = iro_flowgraph_head; block != NULL; block = block->nextnode) {
        IroBitVect_AllocateBitVector(&block->in, def_count);
        IroBitVect_AllocateBitVector(&block->gen, def_count);
        IroBitVect_AllocateBitVector(&block->kill, def_count);
        IroBitVect_AllocateBitVector(&block->out, def_count);
        for (node = block->first; node != NULL; node = node->next) {
            while (variable != NULL && variable->linear == node) {
                IroBitVect_SetBit(variable->index, block->gen);
                if (variable->definite) {
                    for (reference = variable->var->defs; reference != NULL; reference = reference->varnext) {
                        if (reference != variable) {
                            IroBitVect_SetBit(reference->index, block->kill);
                            IroBitVect_ClearBit(reference->index, block->gen);
                        }
                    }
                }
                variable = variable->globalnext;
            }
            if (node == block->last)
                break;
        }
        if (block->numpred != 0)
            IroBitVect_SetAllBits(block->in);
        IroBitVect_CopyBitVector(block->gen, block->out);
        IroVars_CheckTimedLongjmp();
    }
    IroBitVect_AllocateBitVector(&reachingDefs, def_count);
    do {
        changed = 0;
        for (block = iro_flowgraph_head; block != NULL; block = block->nextnode) {
            IroBitVect_ClearBitVector(block->in);
            if (block->numpred != 0) {
                for (i = 0; i < block->numpred; i++) {
                    IroBitVect_Or(iroNodesByIndex[block->pred[i]]->out, block->in);
                }
            } else if (block == iro_flowgraph_head) {
                for (entry = var_records; entry != NULL; entry = entry->next) {
                    IroBitVect_SetBit(entry->defs->index, block->in);
                }
            }
#ifdef __wasm__ /* (void functions, called as SInt32 ones; WebAssembly's calls must match) */
            IroBitVect_CopyBitVector(block->in, reachingDefs);
            IroBitVect_Subtract(block->kill, reachingDefs);
#else
            (void)((SInt32 (*)(BitVector *, BitVector *))IroBitVect_CopyBitVector)(block->in, reachingDefs);
            (void)((SInt32 (*)(BitVector *, BitVector *))IroBitVect_Subtract)(block->kill, reachingDefs);
#endif
            IroBitVect_Or(block->gen, reachingDefs);
            if (ReverseEqual(block->out, reachingDefs))
                continue;
            IroBitVect_CopyBitVector(reachingDefs, block->out);
            changed = 1;
        }
        IroVars_CheckTimedLongjmp();
    } while (changed);
    variable = def_list;
    definition = allocated_uses;
    IroBitVect_AllocateBitVector(&use_def_in, def_count);
    for (block = iro_flowgraph_head; block != NULL; block = block->nextnode) {
        IroBitVect_CopyBitVector(block->in, use_def_in);
        for (currentNode = block->first;; currentNode = currentNode->next) {
            for (; definition != NULL && definition->linear == currentNode; definition = definition->globalnext) {
                for (reference = definition->var->defs; reference != NULL; reference = reference->varnext) {
                    if (BVTEST(use_def_in, reference->index)) {
                        IroBitVect_SetBit(reference->index, definition->reachingDefs);
                        IroBitVect_SetBit(reference->index, used_defs_bitvector);
                        reference->useCount++;
                        definition->reachingDefCount++;
                    }
                }
            }
            if (currentNode->type == IROLinearFunccall ||
                (currentNode->type == IROLinearOp1Arg && currentNode->nodetype == EINDIRECT &&
                 IroDump_GetObjRef(currentNode) == NULL)) {
                IroBitVect_CopyBitVector(use_def_in, usedDefs);
                IroBitVect_Intersect(callDefs, usedDefs);
                IroBitVect_Or(usedDefs, used_defs_bitvector);
                for (callVar = def_list; callVar != NULL; callVar = callVar->globalnext)
                    if (callVar->global || (callVar->noregister && BVTEST(use_def_in, callVar->index)))
                        callVar->useCount++;
                if (currentNode->type == IROLinearFunccall && currentNode->stmt != NULL)
                    IroVars_VisitExceptionOperands(currentNode->stmt->dobjstack, mark_var_used_at_call);
            }
            if (currentNode->type == IROLinearAsm) {
                fn_00462d70(currentNode->u.asm_stmt, &buffer);
                if (buffer.readsMemory != 0) {
                    IroBitVect_CopyBitVector(use_def_in, usedDefs);
                    IroBitVect_Intersect(callDefs, usedDefs);
                    IroBitVect_Or(usedDefs, used_defs_bitvector);
                }
            }
            if (currentNode->type == IROLinearReturn || currentNode->type == IROLinearEnd) {
                for (addressTakenVar = def_list; addressTakenVar != NULL; addressTakenVar = addressTakenVar->globalnext)
                    if (addressTakenVar->global && BVTEST(use_def_in, addressTakenVar->index)) {
                        IroBitVect_SetBit(addressTakenVar->index, used_defs_bitvector);
                        addressTakenVar->useCount++;
                    }
            }
            while (variable != NULL && variable->linear == currentNode) {
                if (variable->definite) {
                    for (reference = variable->var->defs; reference != NULL; reference = reference->varnext)
                        IroBitVect_ClearBit(reference->index, use_def_in);
                }
                IroBitVect_SetBit(variable->index, use_def_in);
                variable = variable->globalnext;
            }
            if (currentNode == block->last)
                break;
        }
    }
    IroVars_CheckTimedLongjmp();
    result = 0;
    if (eliminateUnused != 0) {
        for (variable = def_list; variable != NULL; variable = variable->globalnext) {
            if (BVTEST(used_defs_bitvector, variable->index))
                continue;
            if (variable->noregister)
                continue;
            if (variable->linear == NULL)
                continue;
            if (variable->linear->type == IROLinearAsm)
                continue;
            if (variable->linear->flags & IROLF_Reffed)
                continue;
            if (is_volatile_object(variable->var->object))
                continue;
            variable->useCount = 0;
            data_00580638 = -1;
            IroUtil_VisitLinearTree(variable->linear, fn_0045ac60);
            variable->linear->type = IROLinearNop;
            IroDump_Print("Removing dead assignment %d\n", variable->linear->index);
            result = 1;
        }
    }
    IroVars_CheckTimedLongjmp();
    if (simplifyUses != 0) {
        for (;;) {
            if (!propagate_inc_dec())
                break;
            result = 1;
        }
    }
    IroVars_CheckTimedLongjmp();
    return result;
}

IROLinear *find_type_one_linear(IROLinear *e)
{
    IROLinear *r;

    if (e->type == IROLinearOperand) {
        return e;
    }
    if (e->type == IROLinearOp2Arg && e->nodetype == EADD) {
        r = find_type_one_linear(e->u.diadic.left);
        if (r == NULL) {
            r = find_type_one_linear(e->u.diadic.right);
        }
        return r;
    }
    return NULL;
}

/* The variable operand a use or definition (an assignment or an increment through an address) refers to. */
IROLinear *fn_00459940(IROLinear *node)
{
    if (node == NULL)
        return NULL;
    if (node->type == IROLinearOp2Arg) {
        node = node->u.diadic.left;
    } else if (node->type == IROLinearOp1Arg) {
        node = node->u.monadic;
    } else {
        CError_Internal("IROUseDef.c", BY_VERSION(1153u, 2338u));
    }
    if (node->type != IROLinearOp1Arg || node->nodetype != EINDIRECT) {
        CError_Internal("IROUseDef.c", BY_VERSION(1160u, 2351u));
    }
    node = node->u.monadic;
    if (node->type == IROLinearOp2Arg && node->nodetype == EADD) {
        struct IROLinear *result = find_type_one_linear(node);
        node = result;
    }
    return node;
}

static void visit_connected_use(IROUse *n)
{
    IRODef *m;

    IroBitVect_SetBit(n->index, connected_defs_and_uses_bits);
    IroBitVect_ClearBit(n->index, data_00587174);
    for (m = n->var->defs; m != NULL; m = m->varnext) {
        if ((m->index >> 5) < data_0058711c->size && (data_0058711c->bits[m->index >> 5] & (1u << (m->index & 31)))) {
            if (((m->index >> 5) < n->reachingDefs->size &&
                 (n->reachingDefs->bits[m->index >> 5] & (1u << (m->index & 31)))) ||
                fn_00459940(m->linear) == n->linear) {
                visit_connected_defs_and_uses(m);
            }
        }
    }
}

void visit_connected_defs_and_uses(IRODef *p)
{
    IROUse *n;
    IROLinear *key;

    IroBitVect_SetBit(p->index, data_00587f70);
    IroBitVect_ClearBit(p->index, data_0058711c);
    for (n = p->var->uses, key = fn_00459940(p->linear); n != NULL; n = n->varnext) {
        if ((n->index >> 5) < data_00587174->size && (data_00587174->bits[n->index >> 5] & (1u << (n->index & 31)))) {
            if (((p->index >> 5) < n->reachingDefs->size &&
                 (n->reachingDefs->bits[p->index >> 5] & (1u << (p->index & 31)))) ||
                key == n->linear) {
                visit_connected_use(n);
            }
        }
    }
}

void split_variable_range(VarRecord *entry)
{
    IROLinear *definitionNode;
    IRODef *use;
    IROUse *definition;
    IROLinear *useNode;
    Object *useReference;
    Object *definitionReference;
    Object *object;
    IroDump_Print("Splitting range for variable: %d\n", entry->index);
    PrintUseDefValue("Def set: ", data_00587f70);
    PrintUseDefValue("Use set: ", connected_defs_and_uses_bits);
    PrintUseDefValue("All defs: ", data_0058711c);
    PrintUseDefValue("All uses: ", data_00587174);
    object = create_temp_object(entry->object->type);
    for (use = entry->defs; use != NULL; use = use->varnext) {
        UInt32 wordIndex = use->index >> 5;
        if (wordIndex < data_00587f70->size && (1 << use->index & data_00587f70->bits[use->index >> 5]) != 0 &&
            use->linear != NULL) {
            useReference = entry->object;
            useNode = fn_00459940(use->linear);
            CError_ASSERT(BY_VERSION(1277, 2459), !(useNode == 0 || useNode->type != IROLinearOperand ||
                                  useNode->u.node->type != EOBJREF || useNode->u.node->data.objref != useReference));
            useNode->u.node->data.objref = object;
        }
    }
    for (definition = entry->uses; definition != NULL; definition = definition->varnext) {
        unsigned int wordIndex = definition->index >> 5;
        unsigned int size = connected_defs_and_uses_bits->size;
        if (wordIndex < size &&
            (1 << definition->index & connected_defs_and_uses_bits->bits[definition->index >> 5]) != 0) {
            definitionReference = entry->object;
            definitionNode = definition->linear;
            CError_ASSERT(BY_VERSION(1300, 2482), !(definitionNode->type != IROLinearOperand || definitionNode->u.node->type != EOBJREF));
            if (definitionNode->u.node->data.objref == definitionReference) {
                definitionNode->u.node->data.objref = object;
            } else
                CError_ASSERT(BY_VERSION(1312, 2494), definitionNode->u.node->data.objref == object);
        }
    }
}

void fn_00459420(void)
{
    VarRecord *record;
    int limit;
    IRODef *reference;
    IROUse *secondary;
    char first;
    BitVector *pending_bits;
    IroBitVect_AllocateBitVector(&data_0058711c, def_count);
    IroBitVect_AllocateBitVector(&data_00587174, data_00580624);
    IroBitVect_AllocateBitVector(&data_00587f70, def_count);
    IroBitVect_AllocateBitVector(&connected_defs_and_uses_bits, data_00580624);
    record = var_records;
    limit = iroVarCount;
    for (; record != NULL && record->index <= limit; record = record->next) {
        if (record->object->datatype == DLOCAL && is_volatile_object(record->object) == 0 && record->usedAtCall == 0 &&
            record->noregister == 0 && record->bitFieldReplacement == NULL) {
            IroBitVect_ClearBitVector(data_0058711c);
            IroBitVect_ClearBitVector(data_00587174);
            for (reference = record->defs; reference != NULL; reference = reference->varnext) {
                if (reference->linear != NULL && reference->linear->type == IROLinearAsm)
                    goto next;
                IroBitVect_SetBit(reference->index, data_0058711c);
            }
            for (secondary = record->uses; secondary != NULL; secondary = secondary->varnext) {
                if (secondary->linear != NULL && secondary->linear->type == IROLinearAsm)
                    goto next;
                IroBitVect_SetBit(secondary->index, data_00587174);
            }
            first = 1;
            for (;;) {
                IroBitVect_ClearBitVector(data_00587f70);
                IroBitVect_ClearBitVector(connected_defs_and_uses_bits);
                reference = record->defs;
                pending_bits = data_0058711c;
                while (reference != NULL &&
                       ((unsigned int)(reference->index >> 5) >= pending_bits->size ||
                        (1 << reference->index & data_0058711c->bits[reference->index >> 5]) == 0)) {
                    reference = reference->varnext;
                }
                if (reference == NULL) {
                    break;
                }
                visit_connected_defs_and_uses(reference);
                if (is_bitvector_empty(data_0058711c) != 0) {
                    break;
                }
                if (first == 0) {
                    split_variable_range(record);
                }
                first = 0;
            }
        }
    next:;
    }
    IroVars_CheckTimedLongjmp();
}
