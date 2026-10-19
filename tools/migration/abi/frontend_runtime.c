#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler/CInline.h"
#include "compiler/CPrec.h"
#include "compiler/CPrep.h"
#include "compiler/CodeGen.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/enode.h"
#include "compiler/types.h"
__declspec(dllimport) void __stdcall ExitProcess(unsigned int);
__declspec(dllimport) void *__stdcall GetStdHandle(unsigned long);
void OracleTrace_Printf(const char *, ...);
#if VERSION == VERSION_GC_1_3
#define RDecision can_inline
#define RExpansion inline_call_expression
#define RDispatch CInline_DispatchNextDeferredNode
#define DecisionRetail can_inline__retail
#define ExpansionRetail inline_call_expression__retail
#define DispatchRetail CInline_DispatchNextDeferredNode__retail
#elif VERSION == VERSION_GC_1_3_2
#define RDecision fn_0055ed70
#define RExpansion fn_0055f350
#define RDispatch fn_0055a7f0
#define DecisionRetail fn_0055ed70__retail
#define ExpansionRetail fn_0055f350__retail
#define DispatchRetail fn_0055a7f0__retail
#else
#define RDecision fn_00560c80
#define RExpansion fn_00561260
#define RDispatch fn_0055c730
#define DecisionRetail fn_00560c80__retail
#define ExpansionRetail fn_00561260__retail
#define DispatchRetail fn_0055c730__retail
#endif
struct CompilerLinkerOptions copts;
SInt16 data_00582468;
UInt8 data_005dfb66;
SInt32 data_005dfb68;
Object *data_005dfb62;
CPrecNode *pending_prec_nodes;
Boolean anyerrors;
extern Boolean RDecision(ENode *);
extern ENode *RExpansion(ENode *);
extern Boolean RDispatch(void);
extern int fn_0055e540(CInlineInfo *, int, int);
extern int fn_0055e600(ENode *, int, int);
extern Boolean DecisionRetail(ENode *);
extern ENode *ExpansionRetail(ENode *);
extern Boolean DispatchRetail(void);
extern int fn_0055e540__retail(CInlineInfo *, int, int);
extern int fn_0055e600__retail(ENode *, int, int);
const char *OracleRust_LinkName(Object *);
int OracleRust_OriginFind(ENode *);
void OracleRust_OriginWalk(ENode *, int, const char *, int);
void OracleRust_PrintChain(ENode *);
int OracleRust_InlineLine(void);
static const char *maybe_link(Object *object) { return object ? OracleRust_LinkName(object) : "-"; }
static int wanted(const char *name);
#define CAN_INLINE CDecision
#define CAN_INLINE_RETAIL DecisionRetail
#define INLINE_CALL CExpansion
#define INLINE_CALL_RETAIL ExpansionRetail
#define DISPATCH CDispatch
#define DISPATCH_RETAIL DispatchRetail
#define fn_0055e540 CBody
#define fn_0055e600 CExpression
#define link_name OracleRust_LinkName
#define origin_find OracleRust_OriginFind
#define origin_walk OracleRust_OriginWalk
#define print_chain OracleRust_PrintChain
#define inline_line OracleRust_InlineLine
#pragma dont_inline on
#include "frontend_reference.h"
#pragma dont_inline reset
#undef fn_0055e540
#undef fn_0055e600
#undef link_name
#undef origin_find
#undef origin_walk
#undef print_chain
#undef inline_line
static unsigned char expected[65536], actual[65536];
static unsigned long position, expected_size, failed;
static int phase, group, scenario, retail_calls, queries, nested, line_number, origin_parent;
static const char *full_setting, *expr_setting, *instantiate_setting;
static Object functions[3];
static TypeFunc function_types[3];
static ENode nodes[8];
static ENodeList frontend_args[2];
static NameSpace namespaces[18];
static unsigned char names[20][620];
static unsigned char bodies[2][256];
static CPrecNode work[2];
static int (*body_call)(CInlineInfo *, int, int);
static int (*expr_call)(ENode *, int, int);

static int equal(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static void zero(void *base, unsigned long n) { volatile unsigned char *p = base; while (n--) *p++ = 0; }
void *memcpy(void *to, const void *from, size_t n) { volatile unsigned char *d = to; const volatile unsigned char *s = from; while(n--) *d++ = *s++; return to; }
size_t strlen(const char *text) { const volatile char *p = text; size_t n = 0; while (*p++) ++n; return n; }
static void byte(unsigned char b) {
    if (position >= sizeof(expected)) { failed = 1; return; }
    if (phase == 0) expected[position] = b;
    else { actual[position] = b; if (position >= expected_size || expected[position] != b) failed = 1; }
    ++position;
}
static void string(const char *s) { do { byte(*s); } while (*s++); }
void OracleTrace_Printf(const char *format, ...) {
    const char *p = format;
    char *args = (char *)(&format + 1);
    int i;
    string(format);
    while (*p) {
        if (*p++ != '%') continue;
        while (*p >= '0' && *p <= '9') ++p;
        if (*p == 'l') ++p;
        if (*p == 's') string(*(char **)args);
        else if (*p == 'd' || *p == 'x') for (i = 0; i < 4; ++i) byte(args[i]);
        else failed = 1;
        args += 4; ++p;
    }
}
int sprintf(char *output, const char *format, ...) {
    char *args = (char *)(&format + 1), *start = output;
    while (*format) {
        if (*format != '%') { *output++ = *format++; continue; }
        ++format;
        if (*format == 's') { const char *s = *(char **)args; while (*s) *output++ = *s++; }
        else if (*format == 'd') {
            long v = *(long *)args; unsigned long magnitude = v < 0 ? 0UL - (unsigned long)v : (unsigned long)v;
            char digits[16]; int n = 0; if (v < 0) *output++ = '-';
            do { digits[n++] = '0' + magnitude % 10; magnitude /= 10; } while (magnitude);
            while (n) *output++ = digits[--n];
        } else failed = 1;
        args += 4; ++format;
    }
    *output = 0; return output - start;
}
char *getenv(const char *name) {
    ++queries; string(name);
    if (equal(name, "MWCC_ORACLE_FULL_COST")) return (char *)full_setting;
    if (equal(name, "MWCC_ORACLE_INLINE_COST")) return (char *)expr_setting;
    if (equal(name, "MWCC_ORACLE_INSTANTIATION")) return (char *)instantiate_setting;
    failed = 1; return 0;
}
static int wanted(const char *name) { const char *v = getenv(name); return v && v[0] && v[0] != '0'; }
const char *OracleRust_LinkName(Object *object) {
    OracleTrace_Printf("helper:link object=%08x\n", object);
    while (object && object->datatype == DALIAS) object = object->u.alias.object;
    if (!object) return "?";
    if ((object->datatype == DFUNC || object->datatype == DVFUNC) && object->u.func.linkname) return object->u.func.linkname->name;
    return object->name ? object->name->name : "?";
}
int OracleRust_InlineLine(void) { string("helper:line"); return line_number; }
void OracleRust_PrintChain(ENode *n) { OracleTrace_Printf("helper:chain node=%08x\n", n); }
int OracleRust_OriginFind(ENode *n) { OracleTrace_Printf("helper:find node=%08x\n", n); return origin_parent; }
void OracleRust_OriginWalk(ENode *n, int parent, const char *link, int level) {
    OracleTrace_Printf("helper:walk node=%08x parent=%d link=%s level=%d\n", n, parent, link, level);
}
#pragma dont_inline on
Boolean DecisionRetail(ENode *node) {
    ++retail_calls; OracleTrace_Printf("retail:decision node=%08x\n", node);
    if (scenario == 24) { functions[0].qual = 0; functions[0].u.func.u = (CInlineInfo *)bodies[1]; ((CInlineInfo *)bodies[0])->nstmts = 43210; }
    if (scenario == 25) { data_00582468 = -32760; line_number = 2147483000L; ((unsigned char *)&copts)[0xd5] = 255; }
    return scenario % 4 == 0 ? 0 : scenario % 4 == 1 ? 1 : scenario % 4 == 2 ? 128 : 255;
}
ENode *ExpansionRetail(ENode *node) {
    ++retail_calls; OracleTrace_Printf("retail:expansion node=%08x\n", node);
    if (scenario == 24) names[0][10] = 'Z';
    if (scenario == 25) origin_parent = 999;
    return scenario % 4 == 0 ? node : scenario % 4 == 1 ? 0 : &nodes[7];
}
Boolean DispatchRetail(void) {
    ++retail_calls; string("retail:dispatch");
    if (scenario & 1) pending_prec_nodes = &work[1];
    if (scenario == 25) { work[0].kind = 255; work[0].obj = &functions[1]; }
    if (scenario == 25) functions[0].u.func.u = (CInlineInfo *)0x10000;
    return scenario % 4 == 0 ? 0 : scenario % 4 == 1 ? 1 : scenario % 4 == 2 ? 128 : 255;
}
int fn_0055e540__retail(CInlineInfo *info, int running, int depth) {
    ++retail_calls;
    OracleTrace_Printf("retail:body info=%08x running=%d depth=%d met=%d threshold=%d\n", info, running, depth, (int)data_005dfb66, (int)data_005dfb68);
    data_005dfb66 = scenario % 4 == 3 ? 255 : 1;
    if (scenario == 24 && !nested) { ++nested; expr_call(&nodes[0], running, depth); --nested; }
    if (scenario == 25 && data_005dfb68 == FULL_COST_LIMIT && data_005dfb66 != 255) return FULL_COST_LIMIT + 1;
    return running + (int)(info ? info->nstmts : 7) + depth;
}
int fn_0055e600__retail(ENode *expr, int running, int depth) {
    ++retail_calls; OracleTrace_Printf("retail:expression expr=%08x running=%d depth=%d\n", expr, running, depth);
    if (scenario == 24 && !nested) { ++nested; body_call((CInlineInfo *)bodies[0], running, depth); --nested; }
    if (scenario == 25 && !nested) { ++nested; expr_call(&nodes[1], running + 1, depth); --nested; }
    if (scenario == 26) expr_setting = "0";
    if (scenario == 27) data_005dfb62 = &functions[1];
    return running + depth + 123;
}
#pragma dont_inline reset
static void initialize(int n, int rust) {
    int i, j;
    scenario = n; retail_calls = queries = nested = 0; line_number = -2147483000L; origin_parent = -123456789;
    zero(functions, sizeof(functions)); zero(function_types, sizeof(function_types)); zero(nodes, sizeof(nodes)); zero(frontend_args, sizeof(frontend_args));
    zero(names, sizeof(names)); zero(namespaces, sizeof(namespaces)); zero(bodies, sizeof(bodies)); zero(work, sizeof(work)); zero(&copts, sizeof(copts));
    data_00582468 = 32760; data_005dfb66 = n % 4 == 0 ? 0 : 128; data_005dfb68 = -1234567;
    data_005dfb62 = &functions[0]; anyerrors = n % 4 == 0 ? 255 : 0; pending_prec_nodes = n == 6 ? 0 : work;
    expr_setting = n % 4 == 0 ? 0 : n % 4 == 1 ? "" : n % 4 == 2 ? "0" : "2";
    full_setting = n % 4 == 0 ? "0" : n % 4 == 1 ? "" : n % 4 == 2 ? "1" : "2";
    instantiate_setting = n < 4 ? expr_setting : "2";
    if (n == 24 || n == 25) { expr_setting = "2"; full_setting = "1"; }
    body_call = rust ? fn_0055e540 : CBody; expr_call = rust ? fn_0055e600 : CExpression;
    for (i = 0; i < 3; ++i) {
        names[i][10] = 'a' + i;
        functions[i].datatype = DFUNC; functions[i].name = (HashNameNode *)names[i]; functions[i].qual = Q_INLINE;
        functions[i].type = TYPE(&function_types[i]); functions[i].u.func.u = (CInlineInfo *)bodies[0]; functions[i].u.func.linkname = (HashNameNode *)names[i];
        function_types[i].type = TYPEFUNC;
        nodes[i].type = EOBJREF; nodes[i].data.objref = &functions[i];
    }
    ((CInlineInfo *)bodies[0])->nstmts = 65530; ((CInlineInfo *)bodies[1])->nstmts = 32769;
    bodies[0][0xa0] = 11; bodies[0][0xa2] = 22; bodies[0][0xa4] = 33;
    ((unsigned char *)&copts)[0xd5] = 128; ((unsigned char *)&copts)[0xd8] = 255; ((unsigned char *)&copts)[0xdc] = 7;
    ((unsigned char *)&copts)[0x72] = 254; ((unsigned char *)&copts)[0x76] = 129;
    *(SInt16 *)((unsigned char *)&copts + 0x68) = -12345; *(SInt16 *)((unsigned char *)&copts + 0x6c) = 23456;
    work[0].obj = &functions[0]; work[0].kind = 128;
    nodes[3].type = EFUNCCALL; nodes[3].data.funccall.funcref = &nodes[0]; nodes[3].data.funccall.args = frontend_args;
    frontend_args[0].node = &nodes[1];
    if (n == 4) functions[0].name = 0;
    if (n == 5) functions[0].u.func.u = 0;
    if (n == 7) functions[0].type = 0;
    if (n == 8) functions[0].qual = 0;
    if (n == 9) { functions[0].qual = 0; function_types[0].flags = 0x800; }
    if (n == 10 || n == 11) { functions[0].datatype = DVFUNC; nodes[0].flags = n == 11 ? ENODE_FLAG_80 : 0; }
    if (n == 12) { functions[0].datatype = DALIAS; functions[0].u.alias.object = &functions[1]; }
    if (n == 13 && group != 0) { functions[0].datatype = DALIAS; functions[0].u.alias.object = 0; }
    if (n == 14) functions[0].datatype = DDATA;
    if (n == 15) function_types[0].type = TYPECLASS;
    if (n >= 16 && n <= 19) {
        for (i = 0; i < 18; ++i) {
            namespaces[i].parent = i == 17 ? 0 : &namespaces[i + 1]; namespaces[i].name = (HashNameNode *)names[i + 1];
            names[i + 1][10] = 'A' + i;
        }
        functions[0].nspace = namespaces;
        if (n == 17) namespaces[1].name = 0;
        if (n == 18) for (j = 10; j < 590; ++j) names[1][j] = 'x';
        if (n == 19) for (j = 10; j < 590; ++j) names[0][j] = 'y';
    }
    if (n == 20) { functions[0].u.func.u = (CInlineInfo *)65535; if (group == 0) functions[0].qual = 0; }
    if (n == 21) work[0].obj = 0;
    if (n == 22) functions[0].u.func.u = (CInlineInfo *)65536;
    if (n == 23) anyerrors = 1;
    if (n == 28) { nodes[0].type = EARGOBJ; nodes[0].data.longval = -123456789; }
    if (n == 29) { nodes[0].type = ELOCOBJ; nodes[0].data.longval = 2147483000L; }
    if (n == 30) { nodes[0].type = EINTCONST; nodes[0].data.intval.lo = -2147483000L; }
    if (n == 31) { nodes[0].type = EASS; nodes[0].data.diadic.left = &nodes[1]; nodes[0].data.diadic.right = &nodes[2]; }
    if (group == 0 || group == 3) {
        /* These wrappers require an object reference, irrespective of spelling cases. */
        nodes[0].type = EOBJREF; nodes[0].data.objref = &functions[0];
        if (n == 20 || n == 22) { if (group == 0) functions[0].qual = 0; else if (n == 22) functions[0].u.func.u = (CInlineInfo *)bodies[0]; }
    }
    if (group == 2 && n >= 4 && n < 24) {
        /* Exercise expression descriptions and body-call walks independently. */
        expr_setting = "2";
        if (n == 4) nodes[0].data.objref = 0;
        if (n == 5) nodes[0].data.objref = &functions[1];
        if (n >= 6 && n <= 9) { nodes[0].type = n == 6 ? EINDIRECT : n == 7 ? EADD : n == 8 ? EFORCELOAD : ETYPCON; nodes[0].data.diadic.left = &nodes[1]; nodes[0].data.diadic.right = &nodes[2]; }
    }
}
static void execute(int rust) {
    int result;
    if (group == 0) { result = rust ? RDecision(nodes) : CDecision(nodes); OracleTrace_Printf("result=%d\n", result); }
    if (group == 1) { result = body_call(scenario == 7 ? 0 : (CInlineInfo *)bodies[0], -12345, scenario % 3 - 1); OracleTrace_Printf("result=%d met=%d threshold=%d\n", result, (int)data_005dfb66, (int)data_005dfb68); }
    if (group == 2) { result = expr_call(scenario == 7 ? 0 : scenario == 23 ? &nodes[3] : nodes, -12345, scenario % 3 - 1); OracleTrace_Printf("result=%d\n", result); }
    if (group == 3) { ENode *result = rust ? RExpansion(&nodes[3]) : CExpansion(&nodes[3]); OracleTrace_Printf("result=%08x\n", result); }
    if (group == 4) { result = rust ? RDispatch() : CDispatch(); OracleTrace_Printf("result=%d\n", result); }
}
static ENode *extra_expression(int n) {
    int i;
    ENode *root = &nodes[3];
    scenario = 32 + n; expr_setting = "2"; full_setting = "0";
    for (i = 0; i < 3; ++i) { nodes[i].type = EOBJREF; nodes[i].data.objref = &functions[i]; }
    for (i = 3; i < 7; ++i) { nodes[i].type = EFUNCCALL; nodes[i].data.funccall.funcref = &nodes[(i - 3) % 3]; nodes[i].data.funccall.args = 0; }
    if (n < 11) { root->type = n < 9 ? n : n == 9 ? ETYPCON : EBITFIELD; root->data.monadic = &nodes[4]; }
    else if (n < 52) { root->type = n < 50 ? n - 2 : n == 50 ? EMFPOINTER : ENULLCHECK; root->data.diadic.left = &nodes[4]; root->data.diadic.right = &nodes[5]; }
    else if (n == 52) { root->type = ECOND; root->data.cond.cond = &nodes[4]; root->data.cond.expr1 = &nodes[5]; root->data.cond.expr2 = &nodes[6]; }
    else if (n == 53 || n == 54) { root->type = EMEMBER; root->data.monadic = (ENode *)frontend_args; frontend_args[0].node = n == 53 ? &nodes[4] : 0; }
    else if (n == 55) { root->type = ECONDASS; root->data.cond.cond = &nodes[4]; root->data.cond.expr1 = &nodes[5]; root->data.cond.expr2 = &nodes[6]; }
    if (n == 56 || n == 69) functions[0].u.func.u = 0;
    if (n == 59) functions[0].qual = 0;
    if (n == 60) { functions[0].qual = 0; function_types[0].flags = 0x800; }
    if (n == 61 || n == 62) { functions[0].datatype = DVFUNC; nodes[0].flags = n == 61 ? ENODE_FLAG_80 : 0; }
    if (n == 63) data_005dfb62 = &functions[1];
    if (n == 64) { functions[0].datatype = DALIAS; functions[0].u.alias.object = &functions[1]; }
    if (n == 65) { nodes[0].type = EINTCONST; nodes[0].data.intval.lo = -1; root->data.funccall.args = frontend_args; frontend_args[0].node = &nodes[4]; }
    if (n == 66) functions[0].type = 0;
    if (n == 67) function_types[0].type = TYPECLASS;
    if (n == 68) { root->type = EFUNCCALLP; function_types[0].flags = 0x800; }
    if (n == 70 || n == 85) { for (i = 10; i < 610; ++i) names[0][i] = 'x'; }
    if (n == 71) { root->data.funccall.args = frontend_args; frontend_args[0].node = &nodes[4]; frontend_args[0].next = &frontend_args[1]; frontend_args[1].node = &nodes[5]; }
    if (n == 72) functions[0].name = 0;
    if (n == 73) {
        for (i = 0; i < 18; ++i) { namespaces[i].parent = i == 17 ? 0 : &namespaces[i + 1]; namespaces[i].name = (HashNameNode *)names[i + 1]; names[i + 1][10] = 'A' + i; }
        functions[0].nspace = namespaces;
    }
    if (n >= 74 && n <= 84) {
        root = &nodes[7]; root->type = n == 74 ? EARGOBJ : n == 75 ? ELOCOBJ : n == 76 ? EINTCONST : n == 77 ? EOBJREF : n == 78 ? 255 : n == 79 ? EFLOATCONST : EINDIRECT;
        root->data.longval = -2147483000L;
        if (n == 77) root->data.objref = &functions[0];
        if (n >= 80) {
            root = &nodes[0];
            for (i = 0; i < 7; ++i) { nodes[i].type = EINDIRECT; nodes[i].data.monadic = &nodes[i + 1]; }
            nodes[7].type = EINTCONST; nodes[7].data.intval.lo = -1;
        }
    }
    if (n == 85) { root->type = EADD; root->data.diadic.left = &nodes[0]; root->data.diadic.right = &nodes[1]; }
    if (n == 86) function_types[0].flags = 0x3100;
    if (n == 87) functions[0].u.func.linkname = 0;
    if (n >= 88) { functions[0].datatype = n - 88; if (functions[0].datatype == DALIAS) functions[0].u.alias.object = &functions[1]; }
    return root;
}
static void diagnose(int test) {
    static const char hex[] = "0123456789abcdef";
    unsigned long i, n, start = 0, written; char text[800], *out = text;
    while (start < expected_size && start < position && expected[start] == actual[start]) ++start;
    out += sprintf(out, "case %d first difference %d expected %d actual %d\n", test, start, expected_size, position);
    start = start > 24 ? start - 24 : 0;
    for (n = 0; n < 2; ++n) {
        unsigned long limit = n ? position : expected_size; unsigned char *bytes = n ? actual : expected;
        for (i = start; i < limit && i < start + 128; ++i) { *out++ = hex[bytes[i] >> 4]; *out++ = hex[bytes[i] & 15]; }
        *out++ = '\n';
    }
    WriteFile(GetStdHandle(-11UL), text, out - text, &written, 0);
}
void main(void) {
    int n, rust, calls = 0, envs = 0;
    for (group = 0; group < 5; ++group) for (n = 0; n < 32; ++n) {
        failed = 0;
        for (rust = 0; rust < 2; ++rust) {
            phase = rust; position = 0;
            initialize(n, rust); execute(rust);
            if (!rust) { expected_size = position; calls = retail_calls; envs = queries; }
            else if (expected_size != position || calls != retail_calls || envs != queries) failed = 1;
        }
        if (failed) { diagnose(group * 32 + n); ExitProcess(group * 32 + n + 1); }
    }
    group = 2;
    for (n = 0; n < 96; ++n) {
        failed = 0;
        for (rust = 0; rust < 2; ++rust) {
            ENode *expr; int result;
            phase = rust; position = 0; initialize(0, rust); expr = extra_expression(n);
            result = expr_call(expr, -12345, n == 57 || n == 63 ? 0 : n == 58 ? -1 : 2);
            OracleTrace_Printf("result=%d\n", result);
            if (!rust) { expected_size = position; calls = retail_calls; envs = queries; }
            else if (expected_size != position || calls != retail_calls || envs != queries) failed = 1;
        }
        if (failed) { diagnose(160 + n); ExitProcess(n + 1); }
    }
    ExitProcess(0);
}
