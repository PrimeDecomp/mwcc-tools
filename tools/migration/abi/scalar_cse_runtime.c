/* Frozen C versus Rust on identical live records. Capture raw format/argument
 * bytes, including caller EIP and forwarded floating-point/64-bit varargs. */
#include <string.h>
#include "compiler/IroVars.h"
#include "compiler/IroCSE.h"
#include "compiler/IroDump.h"
#include "compiler/enode.h"
extern IROVarPart *class_data_parts;
extern UInt8 iro_pointer_analysis;
extern Object *data_005875b8;
extern Object *create_temp_object__retail(Type *type);
extern void fn_00460780__retail(void);
extern int fn_004601f0__retail(IROLinear *, char *, SInt32, Type *);
#define fn_00454030 OracleAbi_CDumpGate
#define create_temp_object OracleAbi_CTemp
#define fn_004601f0 OracleAbi_CUse
#define fn_00460780 OracleAbi_CScalar
#define describe_expression OracleAbi_CExpression
#define IRO_CommonSubs OracleAbi_CCommon
#define move_common_sub OracleAbi_CMove
#define IroDump_Print OracleAbi_CDump
#define cse_vregs OracleAbi_CVregs
#define maybe_link OracleAbi_CMaybeLink
#define wanted OracleAbi_CWanted
#include "scalar_cse_reference.h"
#undef fn_00454030
#undef create_temp_object
#undef fn_004601f0
#undef fn_00460780
#undef describe_expression
#undef IRO_CommonSubs
#undef move_common_sub
#undef IroDump_Print
#undef cse_vregs
#undef maybe_link
#undef wanted

extern void fn_00454030(void);
extern Object *create_temp_object(Type *type);
extern int fn_004601f0(IROLinear *, char *, SInt32, Type *);
extern void fn_00460780(void);
extern void OracleRust_DescribeExpression(char *, size_t, ENode *, int);
extern void IRO_CommonSubs(void);
extern void move_common_sub(IROExpr *);
extern void cse_vregs(void);
FILE *iro_dump_output;
UInt8 INT_005882b8;
IROVarPart *class_data_parts;
UInt8 iro_pointer_analysis;
Object *data_005875b8;
IROLinear *linear_head;
static const char *scalar_locals, *scalar_cse, *scalar_path;
static int scalar_scenario, scalar_rust, scalar_open_fail;
static unsigned long scalar_cse_queries;
static ENode scalar_nodes[6];
static ENodeList scalar_arguments[2];
static IROLinear scalar_linear[4];
static IROExpr scalar_group;
static IROVarPart scalar_parts[2];
static VarRecord scalar_vars[2];
static IROElmList scalar_uses[2];
static Object scalar_objects[3];
static Type scalar_type;
static VarInfo scalar_info;
static Statement scalar_stmt;
static ObjectList scalar_list[5];
static unsigned char scalar_odd[9];
static char scalar_long_name[100];
static Object *(*scalar_temp_call)(Type *);
static int (*scalar_use_call)(IROLinear *, char *, SInt32, Type *);
static void (*scalar_dump_call)(const char *, ...);

static void scalar_zero(void *value, unsigned long size)
{
    volatile unsigned char *bytes = value;
    while (size--) *bytes++ = 0;
}

char *strcat(char *output, const char *value)
{
    char *start = output;
    while (*output) ++output;
    do { *output++ = *value; } while (*value++);
    return start;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n--) {
        unsigned char x = *a++, y = *b++;
        if (x != y) return (int)x - y;
        if (!x) return 0;
    }
    return 0;
}

static char *scalar_environment(const char *name)
{
    /* The new frontend-origin stream is separate from the legacy lc/ce ABI.
     * This harness keeps that stream disabled without changing old query events. */
    if (equal(name, "MWCC_ORACLE_BACKEND")) return 0;
    pcode_string(name);
    if (equal(name, "MWCC_ORACLE_LOCALS")) return (char *)scalar_locals;
    if (equal(name, "MWCC_ORACLE_CSE")) {
        ++scalar_cse_queries;
        if (scalar_scenario == 50) return scalar_cse_queries % 2 ? "2" : "0";
        if (scalar_scenario == 51) return scalar_cse_queries % 2 ? "1" : "2";
        return (char *)scalar_cse;
    }
    if (equal(name, "MWCC_ORACLE_IRO_DUMP")) return (char *)scalar_path;
    failures |= 128;
    return 0;
}

FILE *fopen(const char *path, const char *mode)
{
    pcode_string("fopen"); pcode_string(path); pcode_string(mode);
    if (!equal(mode, "a")) failures |= 128;
    return scalar_open_fail ? 0 : (FILE *)&scalar_info;
}

int vfprintf(FILE *file, const char *format, va_list arguments)
{
    int i;
    pcode_string("vfprintf");
    for (i = 0; i < 4; ++i) pcode_byte(((unsigned char *)&file)[i]);
    pcode_trace(format, arguments);
    return -12345;
}

Object *create_temp_object__retail(Type *type)
{
    ++pcode_retail_calls;
    OracleTrace_Printf("retail:temp type=%08x\n", type);
    if (type != &scalar_type && type != 0) failures |= 128;
    return scalar_scenario % 4 == 3 ? 0 : &scalar_objects[0];
}

int fn_004601f0__retail(IROLinear *linear, char *var, SInt32 offset, Type *type)
{
    ++pcode_retail_calls;
    OracleTrace_Printf("retail:use node=%08x var=%08x offset=%ld type=%08x\n", linear, var, offset, type);
    if (linear != scalar_linear || var != (char *)scalar_vars || offset != -2147483000L ||
        (type != &scalar_type && type != 0)) failures |= 128;
    return -2147483001L;
}

#pragma dont_inline on
/* Both wrappers must reach the same call instruction. Inlining this synthetic
 * retail function into the C wrapper changes caller EIP before either hook runs. */
void fn_00460780__retail(void)
{
    ++pcode_retail_calls; pcode_string("retail:scalar:before");
    if (scalar_use_call(scalar_linear, (char *)scalar_vars, -2147483000L,
            scalar_scenario % 4 == 2 ? 0 : &scalar_type) != -2147483001L) failures |= 128;
    if (scalar_temp_call(&scalar_type) != (scalar_scenario % 4 == 3 ? 0 : &scalar_objects[0])) failures |= 128;
    if (scalar_scenario == 34) scalar_locals = "2";
    if (scalar_scenario == 35) scalar_locals = "0";
    class_data_parts = scalar_scenario == 33 ? 0 : scalar_parts;
    pcode_string("retail:scalar:after");
}
#pragma dont_inline reset

void fn_00454030__retail(void)
{
    ++pcode_retail_calls; pcode_string("retail:dump-gate");
    INT_005882b8 = 128;
}

void IRO_CommonSubs__retail(void)
{
    ++pcode_retail_calls; pcode_string("retail:common");
}

void move_common_sub__retail(IROExpr *group)
{
    ++pcode_retail_calls; pcode_string("retail:move");
    if (group != &scalar_group) failures |= 128;
    if (scalar_scenario % 3 == 0) scalar_dump_call("Moving common sub %d", 65530);
    group->temp = scalar_scenario % 4 == 0 ? 0 : &scalar_objects[0];
}

static void scalar_initialize(int n, int rust)
{
    int i;
    static const char *options[] = {0, "", "0", "1", "2", "enabled"};
    scalar_scenario = n; scalar_rust = rust;
    scalar_temp_call = rust ? create_temp_object : OracleAbi_CTemp;
    scalar_use_call = rust ? fn_004601f0 : OracleAbi_CUse;
    scalar_dump_call = rust ? IroDump_Print : OracleAbi_CDump;
    scalar_locals = n >= 24 && n < 36 ? options[(n - 24) % 6] : "2";
    scalar_cse = n >= 36 && n < 52 ? options[(n - 36) % 6] : n % 8 == 7 ? "0" : "2";
    scalar_path = 0; scalar_open_fail = 0;
    iro_dump_output = 0; INT_005882b8 = 0;
    pcode_retail_calls = pcode_mangles = environment_calls = 0;
    scalar_cse_queries = 0;
    scalar_zero(scalar_nodes, sizeof(scalar_nodes));
    scalar_zero(scalar_arguments, sizeof(scalar_arguments));
    scalar_zero(scalar_linear, sizeof(scalar_linear));
    scalar_zero(&scalar_group, sizeof(scalar_group));
    scalar_zero(scalar_parts, sizeof(scalar_parts));
    scalar_zero(scalar_vars, sizeof(scalar_vars));
    scalar_zero(scalar_objects, sizeof(scalar_objects));
    scalar_zero(scalar_list, sizeof(scalar_list));
    pcode_name.name[0] = 's'; pcode_name.name[1] = 0;
    pcode_link.name[0] = 'L'; pcode_link.name[1] = 0;
    pcode_function.datatype = DFUNC; pcode_function.name = &pcode_name;
    pcode_function.u.func.linkname = &pcode_link;
    data_005875b8 = n % 4 == 3 ? 0 : &pcode_function;
    scalar_type.type = 255; scalar_type.size = -2147483000L;
    scalar_objects[0].name = n % 4 == 2 ? 0 : &pcode_name;
    scalar_objects[0].type = &scalar_type; scalar_objects[0].datatype = DLOCAL;
    scalar_objects[0].u.var.info = &scalar_info;
    scalar_info.reg = -32760; scalar_info.rclass = 255; scalar_info.noregister = 128;
    scalar_objects[1].datatype = DDATA;
    scalar_objects[2].datatype = DLOCAL;
    for (i = 0; i < 2; ++i) {
        scalar_vars[i].object = i ? 0 : &scalar_objects[0];
        scalar_vars[i].noregister = i ? 255 : 128;
        scalar_parts[i].var = &scalar_vars[i]; scalar_parts[i].flags = i;
        *(SInt32 *)scalar_parts[i].x12 = -2147483000L;
        *(SInt32 *)(scalar_parts[i].x12 + 4) = 2147483000L;
        scalar_parts[i].type = i ? 0 : &scalar_type;
        scalar_parts[i].next = i ? 0 : &scalar_parts[1];
        scalar_uses[i].next = i ? 0 : &scalar_uses[1];
    }
    scalar_parts[0].uses = scalar_uses;
    class_data_parts = 0; iro_pointer_analysis = 255;
    scalar_stmt.sourceoffset = -2147483000L;
    scalar_nodes[0].type = EOBJREF; scalar_nodes[0].data.objref = &scalar_objects[0];
    scalar_nodes[1].type = EINTCONST; scalar_nodes[1].data.intval.lo = 0x80000000;
    for (i = 0; i < 4; ++i) {
        scalar_linear[i].type = i == 0 ? IROLinearOperand : i == 1 ? IROLinearOp1Arg : i == 2 ? IROLinearOp2Arg : 255;
        scalar_linear[i].index = 65530 + i; scalar_linear[i].nodetype = 255;
        scalar_linear[i].stmt = i % 2 ? 0 : &scalar_stmt;
        scalar_linear[i].expr = i % 2 ? 0 : &scalar_group;
        scalar_linear[i].next = i == 3 ? 0 : &scalar_linear[i + 1];
        scalar_linear[i].u.monadic = i ? &scalar_linear[i - 1] : (IROLinear *)&scalar_nodes[0];
        if (i == 2) scalar_linear[i].u.diadic.right = 0;
    }
    scalar_group.index = 65535;
    scalar_group.linear = n % 4 == 3 ? 0 : &scalar_linear[n % 4];
    linear_head = scalar_linear;
    locals = temporary_objects = 0;
}

static void scalar_expression(int n)
{
    char buffer[640];
    ENode *expression = &scalar_nodes[2];
    int depth = 4, size = sizeof(buffer), i;
    buffer[0] = 0;
    expression->data.diadic.left = &scalar_nodes[0];
    expression->data.diadic.right = &scalar_nodes[1];
    switch (n) {
    case 0: expression = 0; break;
    case 1: depth = 0; break;
    case 2: depth = -1; break;
    case 3: expression = &scalar_nodes[0]; break;
    case 4: expression = &scalar_nodes[0]; scalar_objects[0].name = 0; break;
    case 5:
        for (i = 0; i < 80; ++i) scalar_long_name[10 + i] = 'a' + i % 26;
        scalar_long_name[90] = 0;
        scalar_objects[0].name = (HashNameNode *)scalar_long_name;
        expression = &scalar_nodes[0]; break;
    case 6: expression->type = EINDIRECT; break;
    case 7: expression->type = EFORCELOAD; break;
    case 8: expression->type = ETYPCON; break;
    case 9: expression->type = EADD; break;
    case 10: expression->type = ECOMMA; break;
    case 11: expression->type = EMUL; break;
    case 12: expression = &scalar_nodes[1]; break;
    case 13: expression->type = 255; break;
    case 14: expression->type = EFUNCCALL; expression->data.funccall.args = 0; break;
    case 15: case 16: case 17:
        expression->type = EFUNCCALL;
        if (n == 17) expression->data.funccall.funcref = 0;
        scalar_arguments[0].node = &scalar_nodes[1];
        scalar_arguments[0].next = n == 16 ? &scalar_arguments[1] : 0;
        expression->data.funccall.args = scalar_arguments; break;
    case 18: size = 8; break;
    case 19: buffer[0] = 'p'; buffer[1] = 0; size = 9; break;
    case 20: expression->type = EINDIRECT; expression->data.monadic = 0; break;
    case 21: expression->type = EINDIRECT; expression->data.monadic = expression; break;
    case 22: expression->type = EADD; depth = 1; break;
    case 23: expression->type = ECOMMA; buffer[0] = 'p'; buffer[1] = 0; break;
    }
    if (scalar_rust) OracleRust_DescribeExpression(buffer, size, expression, depth);
    else OracleAbi_CExpression(buffer, size, expression, depth);
    pcode_string(buffer);
}

static void scalar_case(int n)
{
    int i;
    if (n < 24) scalar_expression(n);
    else if (n < 36) {
        if (scalar_rust) fn_00460780(); else OracleAbi_CScalar();
        /* The private scalarization flag must have been cleared on return. */
        scalar_use_call(scalar_linear, (char *)scalar_vars, -2147483000L, 0);
    } else if (n < 44) {
        Object *object = scalar_temp_call(n % 2 ? 0 : &scalar_type);
        if (object != (n % 4 == 3 ? 0 : &scalar_objects[0])) failures |= 128;
    } else if (n < 52) {
        for (i = 0; i < 2; ++i) {
            if (scalar_rust) IRO_CommonSubs(); else OracleAbi_CCommon();
        }
    } else if (n < 60) {
        if (n == 58) scalar_cse = "0";
        if (scalar_rust) move_common_sub(&scalar_group); else OracleAbi_CMove(&scalar_group);
    } else if (n < 72) {
        int k = n - 60;
        if (k == 0) scalar_dump_call("Moving common sub %d", 65530);
        else if (k == 1) scalar_dump_call("Replacing common sub at %d with %d", -2147483000L, 65535);
        else if (k >= 2 && k <= 7) scalar_dump_call(k % 3 == 0 ? "Found propagatable assignment %d" :
                k % 3 == 1 ? "Found propagatable expression %d" : "Removing dead assignment %d",
                k < 5 ? 65530 + k - 2 : k == 5 ? -1 : k == 6 ? 65533 : 100);
        else {
            INT_005882b8 = k == 8 ? 0 : k == 9 ? 1 : k == 10 ? 128 : 255;
            iro_dump_output = (FILE *)&scalar_info;
            scalar_dump_call("other %d %g %lld %s %08lx", -2147483000L, -1.25,
                (long long)0x8123456789abcdefLL, "text", 0xfedcba98UL);
        }
    } else if (n < 80) {
        i = n - 72;
        scalar_path = i == 0 ? 0 : i == 1 ? "" : i == 2 ? "0" : "test.dump";
        scalar_open_fail = i == 4;
        iro_dump_output = i >= 5 ? (FILE *)&scalar_info : 0;
        INT_005882b8 = 255;
        if (scalar_rust) fn_00454030(); else OracleAbi_CDumpGate();
        OracleTrace_Printf("gate result=%d file=%08x\n", INT_005882b8, iro_dump_output);
    } else {
        struct TemporaryObjectEntry *odd = (struct TemporaryObjectEntry *)((unsigned long)scalar_odd | 1);
        for (i = 0; i < 5; ++i) scalar_list[i].next = i == 4 ? 0 : &scalar_list[i + 1];
        scalar_list[1].object = &scalar_objects[1]; scalar_list[2].object = &scalar_objects[2];
        scalar_list[3].object = &scalar_objects[0]; scalar_list[4].object = &scalar_objects[0];
        odd->next = 0; odd->object = &scalar_objects[0];
        locals = n == 80 ? 0 : scalar_list;
        temporary_objects = n == 81 ? 0 : (ObjectList *)odd;
        if (n == 82) scalar_objects[0].name = 0;
        if (n == 83) scalar_objects[0].u.var.info = 0;
        if (n == 84) scalar_cse = 0;
        if (n == 85) scalar_cse = "";
        if (n == 86) scalar_cse = "0";
        if (scalar_rust) cse_vregs(); else OracleAbi_CVregs();
    }
}

static void scalar_check(void)
{
    int n;
    unsigned long calls, queries;
    scalar_active = pcode_active = 1;
    for (n = 0; n < 88; ++n) {
        pcode_phase = 0; pcode_position = 0;
        scalar_initialize(n, 0); scalar_case(n);
        pcode_size = pcode_position; calls = pcode_retail_calls; queries = environment_calls;
        pcode_phase = 1; pcode_position = 0;
        scalar_initialize(n, 1); scalar_case(n);
        if (pcode_position != pcode_size || calls != pcode_retail_calls || queries != environment_calls) failures |= 128;
        /* A useful failing scenario remains visible even under Unix's 8-bit exit status. */
        if (failures) {
#ifdef RUST_ABI_DIAGNOSTICS
            pcode_diagnostic();
#endif
            ExitProcess(n + 1);
        }
    }
    scalar_active = pcode_active = 0;
}
