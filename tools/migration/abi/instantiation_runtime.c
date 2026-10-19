/* Frozen C and Rust run against the same records and indirect call sites.
 * Compare raw format/argument bytes, option queries, mangling and retail order. */
#include "compiler/templates.h"
#include "compiler/CPrec.h"
CPrecNode *pendingInlineWork, *pending_prec_nodes;
InlineNode *deferredInlineNodes;
Object *cscope_currentfunc, *data_005dfb62;
extern void CInline_AddFunctionPrecNode__retail(Object *, TypeClass *, FileOffsetInfo *, TokenStream *, Boolean);
extern void CInline_AddSpecialization__retail(Object *, void *, void *);
extern Boolean CTemplateNew_InstantiateInlineTemplateObject__retail(Object *);
extern Boolean CTemplateNew_InstantiateFunction__retail(TemplateFunction *, TemplFuncInstance *, Boolean);
extern void generate_inline_code__retail(Object *, CInlineInfo *, char);
#define CInline_AddFunctionPrecNode OracleAbi_CPrec
#define CInline_AddSpecialization OracleAbi_CSpecialization
#define CTemplateNew_InstantiateInlineTemplateObject OracleAbi_CNeed
#define CTemplateNew_InstantiateFunction OracleAbi_CInstantiate
#define fn_0050f240 OracleAbi_CEmit
#define generate_inline_code OracleAbi_CGenerate
#define maybe_link OracleAbi_CMaybeLink
#define tp_context OracleAbi_TpContext
#define wanted OracleAbi_CWanted
#define link_name OracleAbi_CLinkName
#include "instantiation_reference.h"
#undef CInline_AddFunctionPrecNode
#undef CInline_AddSpecialization
#undef CTemplateNew_InstantiateInlineTemplateObject
#undef CTemplateNew_InstantiateFunction
#undef fn_0050f240
#undef generate_inline_code
#undef maybe_link
#undef tp_context
#undef wanted
#undef link_name
extern void CInline_AddFunctionPrecNode(Object *, TypeClass *, FileOffsetInfo *, TokenStream *, Boolean);
extern void CInline_AddSpecialization(Object *, void *, void *);
extern Boolean CTemplateNew_InstantiateInlineTemplateObject(Object *);
extern Boolean CTemplateNew_InstantiateFunction(TemplateFunction *, TemplFuncInstance *, Boolean);
extern void fn_0050f240(Object *);
extern void generate_inline_code(Object *, CInlineInfo *, char);
static int instantiation_case_number;
static const char *instantiation_setting;
static unsigned char instantiation_storage[sizeof(TemplFuncInstance) + 1];
static TemplFuncInstance *instantiation_specialization;
static Boolean instantiation_byte;
static Object *instantiation_object;
static void (*instantiation_emit)(Object *);

static char *instantiation_environment(const char *name)
{
    pcode_string(name);
    if (!equal(name, "MWCC_ORACLE_INSTANTIATION")) failures |= 128;
    return (char *)instantiation_setting;
}
static void instantiation_mutate(void)
{
    int n = instantiation_case_number;
    ++pcode_retail_calls;
    if (n == 8) instantiation_setting = "1";
    if (n == 9) instantiation_setting = "0";
    if (n == 10) { cscope_currentfunc = 0; data_005dfb62 = &pcode_function; }
    if (n == 11) pcode_link.name[0] = 'X';
    if (n & 1) {
        pendingInlineWork = (CPrecNode *)&scalar_parts[0];
        pending_prec_nodes = (CPrecNode *)&scalar_parts[1];
        deferredInlineNodes = (InlineNode *)&scalar_vars[0];
    }
    if (n == 12) {
        /* Change only the other list: flag zero must inspect pending_prec_nodes. */
        pendingInlineWork = (CPrecNode *)&scalar_parts[0];
    }
}
void CInline_AddFunctionPrecNode__retail(Object *function, TypeClass *value, FileOffsetInfo *key, TokenStream *pair, Boolean flag)
{
    OracleTrace_Printf("retail:prec function=%08x value=%08x key=%08x pair=%08x flag=%d\n", function, value, key, pair, (int)flag);
    if (function != &pcode_function || value != (TypeClass *)&scalar_type || key != (FileOffsetInfo *)&scalar_stmt ||
        pair != (TokenStream *)&scalar_group || flag != instantiation_byte) failures |= 128;
    instantiation_mutate();
}
void CInline_AddSpecialization__retail(Object *function, void *definition, void *specialization)
{
    OracleTrace_Printf("retail:specialization function=%08x definition=%08x specialization=%08x\n", function, definition, specialization);
    if (function != &pcode_function || definition != &scalar_type || specialization != instantiation_specialization) failures |= 128;
    instantiation_mutate();
}
Boolean CTemplateNew_InstantiateInlineTemplateObject__retail(Object *object)
{
    OracleTrace_Printf("retail:need object=%08x\n", object);
    if (object != &pcode_function) failures |= 128;
    instantiation_mutate();
    return instantiation_byte;
}
Boolean CTemplateNew_InstantiateFunction__retail(TemplateFunction *definition, TemplFuncInstance *specialization, Boolean report)
{
    OracleTrace_Printf("retail:instantiate definition=%08x specialization=%08x report=%d\n", definition, specialization, (int)report);
    if (definition != (TemplateFunction *)&scalar_type || specialization != instantiation_specialization || report != instantiation_byte) failures |= 128;
    instantiation_mutate();
    return instantiation_byte;
}
void fn_0050f240__retail(Object *object)
{
    OracleTrace_Printf("retail:emit object=%08x\n", object);
    if (object != instantiation_object) failures |= 128;
    instantiation_mutate();
}
void generate_inline_code__retail(Object *object, CInlineInfo *input, char mode)
{
    OracleTrace_Printf("retail:generate object=%08x input=%08x mode=%d\n", object, input, (int)mode);
    if (object != instantiation_object || input != (CInlineInfo *)&scalar_group || (unsigned char)mode != instantiation_byte) failures |= 128;
    instantiation_mutate();
}
#pragma dont_inline on
static void instantiation_emit_call(void)
{
    /* A single instruction gives both implementations the identical caller EIP. */
    instantiation_emit(instantiation_object);
}
#pragma dont_inline reset
static void instantiation_case(int hook, int n, int rust)
{
    Boolean result;
    pcode_initialize(0);
    instantiation_case_number = n;
    instantiation_setting = n == 0 ? 0 : n == 1 ? "" : n == 2 ? "0" : n == 3 ? "0enabled" : n == 8 ? "0" : "2";
    instantiation_byte = n % 4 == 0 ? 0 : n % 4 == 1 ? 1 : n % 4 == 2 ? 128 : 255;
    pendingInlineWork = pending_prec_nodes = 0; deferredInlineNodes = 0;
    cscope_currentfunc = n == 4 ? 0 : &pcode_function;
    data_005dfb62 = n == 5 ? &pcode_function : 0;
    instantiation_object = n == 6 ? 0 : &pcode_function;
    instantiation_specialization = n == 6 ? 0 : (TemplFuncInstance *)(instantiation_storage + (n == 15 ? 1 : 0));
    if (instantiation_specialization) {
        scalar_zero(instantiation_specialization, sizeof(TemplFuncInstance));
        instantiation_specialization->object = n == 7 ? 0 : &pcode_function;
    }
    if (n == 13) pcode_function.u.func.linkname = 0;
    if (n == 14) { instantiation_object = &pcode_alias; cscope_currentfunc = &pcode_alias; instantiation_specialization->object = &pcode_alias; }
    if (hook == 0) {
        if (rust) CInline_AddFunctionPrecNode(&pcode_function, (TypeClass *)&scalar_type, (FileOffsetInfo *)&scalar_stmt, (TokenStream *)&scalar_group, instantiation_byte);
        else OracleAbi_CPrec(&pcode_function, (TypeClass *)&scalar_type, (FileOffsetInfo *)&scalar_stmt, (TokenStream *)&scalar_group, instantiation_byte);
    } else if (hook == 1) {
        if (rust) CInline_AddSpecialization(&pcode_function, &scalar_type, instantiation_specialization);
        else OracleAbi_CSpecialization(&pcode_function, &scalar_type, instantiation_specialization);
    } else if (hook == 2) {
        result = rust ? CTemplateNew_InstantiateInlineTemplateObject(&pcode_function) : OracleAbi_CNeed(&pcode_function);
        if (result != instantiation_byte) failures |= 128;
    } else if (hook == 3) {
        result = rust ? CTemplateNew_InstantiateFunction((TemplateFunction *)&scalar_type, instantiation_specialization, instantiation_byte)
            : OracleAbi_CInstantiate((TemplateFunction *)&scalar_type, instantiation_specialization, instantiation_byte);
        if (result != instantiation_byte) failures |= 128;
    } else if (hook == 4) {
        instantiation_emit = rust ? fn_0050f240 : OracleAbi_CEmit;
        instantiation_emit_call();
    } else {
        if (rust) generate_inline_code(instantiation_object, (CInlineInfo *)&scalar_group, (char)instantiation_byte);
        else OracleAbi_CGenerate(instantiation_object, (CInlineInfo *)&scalar_group, (char)instantiation_byte);
    }
}
static void instantiation_check(void)
{
    int hook, n;
    unsigned long calls, queries, mangles;
    instantiation_active = pcode_active = 1;
    for (hook = 0; hook < 6; ++hook) for (n = 0; n < 16; ++n) {
        pcode_phase = 0; pcode_position = 0;
        instantiation_case(hook, n, 0);
        pcode_size = pcode_position; calls = pcode_retail_calls; queries = environment_calls; mangles = pcode_mangles;
        pcode_phase = 1; pcode_position = 0;
        instantiation_case(hook, n, 1);
        if (pcode_position != pcode_size || calls != pcode_retail_calls || queries != environment_calls || mangles != pcode_mangles || calls != 1 || queries != 1) failures |= 128;
        if (failures) {
#ifdef RUST_ABI_DIAGNOSTICS
            pcode_diagnostic();
#endif
            ExitProcess(hook * 16 + n + 1);
        }
    }
    instantiation_active = pcode_active = 0;
}
