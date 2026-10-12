#ifndef COMPILER_IROPTIMIZER_H
#define COMPILER_IROPTIMIZER_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Head and tail of the generated statement list. */
struct IrNodeList {
    Statement *last;
    Statement *first;
};
/* A temporary made for an expression (temporary_list lists them): the expression's key and the temporary. */
struct List12 {
    ENode *nullCheckExpression;
    Object *temporary;
    struct List12 *next;
};
extern void IRO_ExpressionPropagation(void);
extern void *IRO_Optimizer(Object *function, void *incomingBody);
extern void record_object_usage(void);
extern ENode *IrOptimizer_NewENode(UInt8 type);
extern void mark_referenced_linear_nodes(void);
extern void fn_004305e0(ENode *e, Object **pp, CLabel **lab);
extern void fn_00430820(ENode *e, Object **pp, CLabel **lab);
extern void insert_indirect_statement_with_label(ENode *node, Object **objp, struct CLabel **out);
extern void lower_monadic_expression_to_statement(ENode *e, Object **pp);
extern void append_cond_expr2_statement(ENode *e, Object **pp);
extern void fn_00430e60(ENode *e, Object **pp);
extern void create_temp_object_assignment(ENode *e, Object **pp);
extern void insert_intconst_assignment(ENode *expr, Object **out);
#if VERSION < VERSION_GC_1_3
extern void create_zero_initialized_temp_object(ENode *expr, Object **out);
#endif
extern void extract_right_operand_to_statement(ENode *node, BY_VERSION(int, Boolean) force);
#if VERSION >= VERSION_GC_1_3
extern Statement *convert_linear_to_statements(struct IRONode *start, struct IROLinear *head);
#else
extern Statement *convert_linear_to_statements(void);
#endif
extern void fn_0042e200(IROLinear *node, int level);
#if VERSION >= VERSION_GC_1_3
extern void trav_expr_to_update_flags(IROLinear *expression);
#define TRAV_EXPR_TO_UPDATE_FLAGS(expression, flag) trav_expr_to_update_flags(expression)
#else
extern void trav_expr_to_update_flags(IROLinear *expression, unsigned int flag);
#define TRAV_EXPR_TO_UPDATE_FLAGS(expression, flag) trav_expr_to_update_flags(expression, flag)
#endif
extern void visit_statement_expressions(struct Statement *stmt);
#if VERSION >= VERSION_GC_1_3
extern void lower_expression_to_statements(ENode *node, ENode *parent, Boolean force, Boolean flag2);
#else
extern void lower_expression_to_statements(ENode *node, int valueNeeded, int force);
#endif
extern void fn_00430a60(ENode *p, Object **objp);
extern void add_local_usage_and_set_noregister(IROLinear *node, int weightIndex);
extern struct IROLinear *linearize_expression(ENode *expression);
extern ENode *fn_0042eb40(IROLinear *e);
extern Boolean fn_0042d2c0(IROLinear *e);
extern Boolean contains_linear_index(IROLinear *a, IROLinear *b);
extern int replace_pending_reference(struct IROUse *ctx, ENode *obj, IROLinear *newval, Boolean doReplace);
extern void build_linear_from_statements(Statement *node);
extern void fn_0042c920(void);
extern void IrOptimizer_SetDeleteDeadInstructionsFlags(void);
extern void mark_node_and_operand_flags(IROLinear *node, int flag);
extern void fn_004315b0(IROLinear *node);
extern unsigned int return_zero_for_irolinear(struct IROLinear *arg1);
#if VERSION >= VERSION_GC_1_3
extern void set_monadic_addr_flags(IROLinear *p, Boolean flag);
#else
extern void set_monadic_addr_flags(IROLinear *p, int flag);
#endif
extern SInt8 monadic_addr_flags_by_nodetype[];
extern SInt32 data_00587148;
extern SInt32 data_005871b0;
extern SInt32 data_005876e4;
extern struct Statement *current_optimizer_statement;
extern SInt32 data_00587e50;
extern struct List12 *temporary_list;
extern SInt32 data_00587f08;
extern struct IROLinear *linear_head;
extern SInt32 data_0058800c;
extern SInt32 data_005880c8;
extern struct Statement *statement_insertion_point;
extern SInt32 data_00588244;
extern UInt8 data_00588518;
extern UInt8 data_00588526;
extern struct IROLinear *last_linear;
extern Boolean IrOptimizer_ConvertToVectorConstant(ENode *e, union MWVector128 *dst, TypeStruct *tstruct);
extern IROLinear *IrOptimizer_NewLinear(unsigned char kind);

#ifdef __cplusplus
}
#endif

#endif
