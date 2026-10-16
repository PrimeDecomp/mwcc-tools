/* Compare the frozen binding observer and Rust on identical packed records. */
extern ENode *setup_inline_locals_and_arguments__retail(Object *, CInlineInfo *, ENodeList *);
struct InlineSlot *data_0058245a;
#define setup_inline_locals_and_arguments OracleAbi_CBindings
#define wanted OracleAbi_CWanted
#define describe_expression OracleAbi_CExpression
#define maybe_link OracleAbi_CMaybeLink
#include "bindings_reference.h"
#undef setup_inline_locals_and_arguments
#undef wanted
#undef describe_expression
#undef maybe_link
extern ENode *setup_inline_locals_and_arguments(Object *, CInlineInfo *, ENodeList *);
static CInlineInfo binding_info;
static struct InlineSlot binding_slots[3], binding_alternate[3];
static CInlineVar binding_formals[3];
static FuncArg binding_declared[3];
static TypeFunc binding_function_type;
static TypePointer binding_pointer_type;
static ENode binding_nodes[10], binding_return;
static ENodeList binding_arguments[3];
static HashNameNode binding_names[3];
static Object binding_objects[3];
static unsigned char binding_odd_slots[sizeof(binding_slots) + 1];
static unsigned char binding_odd_formals[sizeof(binding_formals) + 1];
static const char *binding_setting;
static int binding_scenario;

static char *bindings_environment(const char *name)
{
    pcode_string(name);
    if (!equal(name, "MWCC_ORACLE_LOCALS")) failures |= 128;
    return (char *)binding_setting;
}
ENode *setup_inline_locals_and_arguments__retail(Object *function, CInlineInfo *info, ENodeList *arguments)
{
    int n = binding_scenario;
    ++pcode_retail_calls;
    OracleTrace_Printf("retail:bindings function=%08x info=%08x arguments=%08x\n", function, info, arguments);
    if (function != &pcode_function || info != (n < 4 ? (CInlineInfo *)1 : &binding_info) || arguments != binding_arguments) failures |= 128;
    if (n == 48) binding_setting = "1";
    if (n == 49) binding_setting = "0";
    if (n == 50) data_0058245a = binding_alternate;
    if (n == 51) binding_info.nargs = 0;
    if (n == 52) binding_formals[0].used = 0;
    if (n == 53) { binding_formals[0].type = 0; binding_formals[0].name = &binding_names[2]; }
    if (n == 54) { binding_function_type.flags = FUNC_AUTO_GENERATED; binding_function_type.args = &binding_declared[1]; }
    if (n == 55) binding_arguments[0].node = &binding_nodes[1];
    if (n == 56) binding_names[0].name[0] = 'X';
    return n == 57 ? 0 : n == 68 ? (ENode *)((char *)&binding_return + 1) : &binding_return;
}
static void binding_initialize(int n)
{
    int i;
    pcode_initialize(0);
    binding_scenario = n;
    binding_setting = n == 0 ? 0 : n == 1 ? "" : n == 2 || n == 48 ? "0" : n == 3 ? "0yes" : "2";
    scalar_zero(&binding_info, sizeof(binding_info));
    scalar_zero(binding_slots, sizeof(binding_slots)); scalar_zero(binding_alternate, sizeof(binding_alternate));
    scalar_zero(binding_formals, sizeof(binding_formals)); scalar_zero(binding_declared, sizeof(binding_declared));
    scalar_zero(&binding_function_type, sizeof(binding_function_type)); scalar_zero(&binding_pointer_type, sizeof(binding_pointer_type));
    scalar_zero(binding_nodes, sizeof(binding_nodes)); scalar_zero(binding_arguments, sizeof(binding_arguments));
    scalar_zero(binding_objects, sizeof(binding_objects)); scalar_zero(binding_names, sizeof(binding_names));
    binding_info.nargs = 1; binding_info.arginfo = binding_formals; data_0058245a = binding_slots;
    binding_function_type.type = TYPEFUNC; binding_function_type.args = binding_declared;
    binding_pointer_type.type = TYPEPOINTER; binding_pointer_type.size = -2147483000L;
    binding_pointer_type.qual = Q_REFERENCE;
    pcode_function.type = TYPE(&binding_function_type);
    for (i = 0; i < 3; ++i) {
        binding_names[i].name[0] = 'a' + i;
        binding_objects[i].name = &binding_names[i]; binding_objects[i].datatype = DLOCAL;
        binding_formals[i].name = &binding_names[i]; binding_formals[i].type = TYPE(&binding_pointer_type);
        binding_formals[i].used = n % 4 == 0 ? 0 : 255; binding_formals[i].dirty = n % 4 == 0 ? 0 : n % 4 == 1 ? 1 : n % 4 == 2 ? 128 : 255;
        binding_slots[i].var = n % 4 == 1 ? &binding_objects[i] : 0;
        binding_slots[i].arg = n % 4 == 2 ? &binding_nodes[1] : 0;
        binding_slots[i].expr = &binding_nodes[i];
        binding_alternate[i].var = &binding_objects[2]; binding_alternate[i].expr = &binding_nodes[2];
        binding_nodes[i].type = EOBJREF; binding_nodes[i].data.objref = &binding_objects[i];
        binding_declared[i].name = &binding_names[i]; binding_declared[i].type = TYPE(&binding_pointer_type);
        binding_declared[i].next = i == 2 ? 0 : &binding_declared[i + 1];
        binding_arguments[i].node = &binding_nodes[i]; binding_arguments[i].next = i == 2 ? 0 : &binding_arguments[i + 1];
    }
    if (n == 4) binding_info.nargs = 0;
    if (n == 5) binding_info.nargs = -1;
    if (n == 6) binding_info.nargs = -32768;
    if (n == 7 || n == 21 || n == 22 || n == 61) binding_info.nargs = 3;
    if (n == 12 || n == 71) binding_formals[0].name = 0;
    if (n == 13) binding_formals[0].type = 0;
    if (n == 14) binding_pointer_type.qual = 0;
    if (n == 16) binding_pointer_type.type = TYPEARRAY;
    if (n == 17) { binding_pointer_type.type = TYPECLASS; binding_pointer_type.size = 2147483000L; }
    if (n == 18) binding_objects[0].name = 0;
    if (n == 19) binding_slots[0].var = &binding_objects[1];
    if (n >= 20 && n <= 23 || n == 55) binding_slots[0].expr = 0;
    if (n == 20) binding_arguments[0].node = 0;
    if (n == 21) binding_arguments[0].next = 0;
    if (n == 22) { binding_slots[1].expr = binding_slots[2].expr = 0; binding_arguments[0].next = 0; }
    if (n == 23) binding_arguments[0].node = &binding_nodes[1];
    if (n >= 24 && n <= 35 || n == 58 || n == 64 || n == 67) {
        ENode *outer = &binding_nodes[3];
        binding_slots[0].expr = outer;
        outer->data.diadic.left = &binding_nodes[1]; outer->data.diadic.right = &binding_nodes[0];
        outer->type = n == 24 || n == 27 || n == 64 ? ECOMMA : n == 25 ? EFORCELOAD : n == 26 ? ETYPCON :
            n >= 28 && n <= 31 || n == 58 ? (n == 31 ? EFUNCCALLP : EFUNCCALL) : n == 32 || n == 33 || n == 67 ? EINDIRECT : n == 34 ? EINTCONST : 255;
        if (outer->type == EFUNCCALL || outer->type == EFUNCCALLP) {
            outer->data.funccall.funcref = &binding_nodes[2]; outer->data.funccall.args = n == 29 ? 0 : binding_arguments;
            if (n == 30) { binding_nodes[0].type = EINTCONST; binding_nodes[0].data.intval.lo = -123456789L; }
        }
        if (n == 27) { binding_nodes[4].type = ETYPCON; binding_nodes[4].data.monadic = &binding_nodes[0]; outer->data.diadic.right = &binding_nodes[4]; }
        if (n == 33) { binding_nodes[1].type = EINTCONST; binding_nodes[1].data.intval.lo = -1; }
        if (n == 34) outer->data.intval.lo = -2147483000L;
        if (n == 64) { binding_nodes[4] = *outer; outer->data.diadic.right = &binding_nodes[4]; }
        if (n == 67) {
            for (i = 3; i < 9; ++i) { binding_nodes[i].type = EINDIRECT; binding_nodes[i].data.monadic = &binding_nodes[i + 1]; }
            binding_nodes[9].type = EOBJREF; binding_nodes[9].data.objref = &binding_objects[2];
        }
    }
    if (n == 36) binding_formals[0].dirty = 255;
    if (n == 37) { binding_slots[0].expr = &binding_nodes[3]; binding_nodes[3].type = EFORCELOAD; binding_nodes[3].data.monadic = 0; }
    if (n == 38 || n == 70) binding_names[0].name[0] = 0;
    if (n == 39 || n == 63) binding_function_type.flags = n == 63 ? 0xffffffff : FUNC_AUTO_GENERATED;
    if (n == 40) binding_function_type.flags = 0x8000;
    if (n == 41 || n == 45) binding_function_type.type = TYPESTRUCT;
    if (n == 42) { binding_declared[0].name = &binding_names[1]; binding_names[1].name[0] = 'a'; }
    if (n == 43 || n == 54) binding_declared[0].name = &binding_names[1];
    if (n == 43) { scalar_type.type = TYPEINT; scalar_type.size = 2147483000L; binding_declared[1].name = &binding_names[0]; binding_declared[1].type = &scalar_type; }
    if (n == 44) binding_declared[0].type = 0;
    if (n == 46) pcode_function.u.func.linkname = 0;
    if (n == 47) pcode_function.datatype = DDATA;
    if (n == 59) binding_formals[0].used = 128;
    if (n == 60) binding_formals[0].used = 255;
    if (n == 61) { binding_slots[1].var = &binding_objects[2]; binding_formals[1].used = 0; binding_slots[2].arg = &binding_nodes[0]; }
    if (n == 62) {
        struct InlineSlot *slots = (struct InlineSlot *)(binding_odd_slots + 1);
        CInlineVar *formals = (CInlineVar *)(binding_odd_formals + 1);
        for (i = 0; i < 3; ++i) { slots[i] = binding_slots[i]; formals[i] = binding_formals[i]; }
        data_0058245a = slots; binding_info.arginfo = formals; binding_info.nargs = 3;
    }
    if (n == 65) binding_pointer_type.qual = 0xffffffe0;
    if (n == 66) binding_pointer_type.type = 255;
    if (n == 69) { binding_slots[0].var = &binding_objects[1]; binding_names[1].name[0] = 0; }
    if (n == 71) binding_declared[0].name = 0;
}
static void bindings_check(void)
{
    int n, rust;
    unsigned long calls = 0, queries = 0, mangles = 0;
    bindings_active = pcode_active = 1;
    for (n = 0; n < 72; ++n) {
        for (rust = 0; rust < 2; ++rust) {
            ENode *result;
            binding_initialize(n);
            pcode_phase = rust; pcode_position = 0;
            result = rust ? setup_inline_locals_and_arguments(&pcode_function, n < 4 ? (CInlineInfo *)1 : &binding_info, binding_arguments)
                : OracleAbi_CBindings(&pcode_function, n < 4 ? (CInlineInfo *)1 : &binding_info, binding_arguments);
            if (result != (n == 57 ? 0 : n == 68 ? (ENode *)((char *)&binding_return + 1) : &binding_return)) failures |= 128;
            if (!rust) { pcode_size = pcode_position; calls = pcode_retail_calls; queries = environment_calls; mangles = pcode_mangles; }
            else if (pcode_size != pcode_position || calls != pcode_retail_calls || queries != environment_calls || mangles != pcode_mangles) failures |= 128;
            if (pcode_retail_calls != 1 || environment_calls != 1) failures |= 128;
        }
        if (failures) {
#ifdef RUST_ABI_DIAGNOSTICS
            pcode_diagnostic();
#endif
            ExitProcess(n + 1);
        }
    }
    bindings_active = pcode_active = 0;
}
