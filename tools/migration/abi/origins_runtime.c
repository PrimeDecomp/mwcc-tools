/* Execute the frozen C ancestry/entry bodies and Rust on the same live records.
 * Include capacity, collision and nesting paths absent from ordinary fixtures. */
#include "compiler/CInline.h"
static void origins_snapshot(const char *phase, Object *function)
{
    OracleAbi_CSnapshot("ra", phase, function);
}
extern void fn_00506cd0__retail(Object *function);
#define origin_find OracleAbi_COriginFind
#define origin_walk OracleAbi_COriginWalk
#define print_chain OracleAbi_CPrintChain
#define origins_reset OracleAbi_COriginsReset
#define inline_line OracleAbi_CInlineLine
#define generate_inline_statements OracleAbi_CStatements
#define inline_statement OracleAbi_CInlineStatement
#define link_name OracleAbi_CLinkName
#define cse_vregs OracleAbi_CVregs
#define snapshot origins_snapshot
#define ALLOCATE_REGISTERS OracleAbi_CAllocate
#define ALLOCATE_REGISTERS_RETAIL fn_00506cd0__retail
#include "origins_reference.h"
#undef origin_find
#undef origin_walk
#undef print_chain
#undef origins_reset
#undef inline_line
#undef generate_inline_statements
#undef inline_statement
#undef link_name
#undef cse_vregs
#undef snapshot
#undef ALLOCATE_REGISTERS
#undef ALLOCATE_REGISTERS_RETAIL

extern int OracleRust_OriginFind(ENode *);
extern void OracleRust_OriginWalk(ENode *, int, const char *, int);
extern void OracleRust_PrintChain(ENode *);
extern int OracleRust_InlineLine(void);
extern void fn_00506cd0(Object *);
extern Statement *generate_inline_statements(Object *, Statement *, CInlineInfo *, ENode *, CLabel *, Object *, UInt8);
extern Statement *inline_statement(Statement *);
static int (*origins_find)(ENode *);
static void (*origins_walk)(ENode *, int, const char *, int);
static void (*origins_chain)(ENode *);
static int (*origins_line)(void);
static Statement *(*origins_inline)(Statement *);
static void (*origins_allocate)(Object *);
static int origins_scenario, origins_initializing, origins_nest;
static const char *origins_cse;
static unsigned long origins_resets;
static ENode origins_refs[49154], origins_calls[3], origins_tree, origins_result;
static ENodeList origins_args[3];
static Statement origins_statements[3], origins_unlisted;
static unsigned char origins_collisions[0xc0000 + 32];
static HashNameNode origins_new_link;
static ObjectList origins_local;
static ENode *origins_queries[3];

void *memset(void *value, int byte, size_t size)
{
    volatile unsigned char *cursor = value;
    unsigned long count = size;
    if (origins_active) {
        if (size != 786432 || byte != 0) failures |= 128;
        ++origins_resets;
        OracleTrace_Printf("reset bytes=%d value=%d\n", (int)size, byte);
    }
    while (count--) *cursor++ = byte;
    return value;
}

static char *origins_environment(const char *name)
{
    pcode_string(name);
    if (!equal(name, "MWCC_ORACLE_CSE")) failures |= 128;
    return (char *)origins_cse;
}

void fn_00506cd0__retail(Object *function)
{
    ++pcode_retail_calls;
    pcode_string("retail:allocate");
    if (function != &pcode_function || origins_find(origins_refs) != -1 || !origins_resets) failures |= 128;
    if (!origins_initializing && origins_scenario == 76) {
        /* AFTER must inspect the state changed by retail. */
        pcode_blocks[0].instructions = &pcode_instructions[1];
        pcode_instructions[1].next = 0;
        gPCodeBlocks = pcode_blocks;
        locals = 0;
    }
    if (!origins_initializing && origins_scenario == 117) gPCodeBlocks = pcode_blocks;
}

Statement *generate_inline_statements__retail(Object *function, Statement *tail, CInlineInfo *args, ENode *result,
    CLabel *label, Object *object, UInt8 append)
{
    int n = origins_scenario;
    ++pcode_retail_calls;
    OracleTrace_Printf("retail:statements function=%08x tail=%08x args=%08x result=%08x label=%08x object=%08x append=%d\n",
        function, tail, args, result, label, object, (int)append);
    if (function != &pcode_function || tail != (n == 98 ? 0 : origins_statements) ||
        args != (n == 116 ? 0 : (CInlineInfo *)&scalar_group) || result != (n == 116 ? 0 : &origins_result) ||
        label != (n == 116 ? 0 : (CLabel *)&origins_unlisted) || object != (n == 116 ? 0 : &scalar_objects[0]) ||
        append != (n == 104 ? 128 : n == 105 ? 255 : 1)) failures |= 128;
    if (n == 103) origins_statements[0].expr = &origins_calls[1];
    if (n == 106) pcode_function.u.func.linkname = &origins_new_link;
    if (n == 107) result->data.funccall.funcref = &origins_refs[2]; /* parent was captured before retail */
    if (n == 99) return 0;
    if (n == 100) return origins_statements;
    if (n == 101) return &origins_statements[1];
    if (n == 102) return &origins_unlisted;
    return &origins_statements[2];
}

Statement *inline_statement__retail(Statement *statement)
{
    Statement *result;
    ++pcode_retail_calls;
    OracleTrace_Printf("retail:inline input=%08x line=%d\n", statement, origins_line());
    if (origins_scenario == 110 && origins_nest < 2) {
        ++origins_nest;
        result = origins_inline(&origins_statements[origins_nest]);
        if (result != &origins_statements[origins_nest]) failures |= 128;
        --origins_nest;
        OracleTrace_Printf("retail:restored line=%d\n", origins_line());
    }
    if (origins_scenario == 113 && origins_nest == 0) {
        origins_allocate(&pcode_function);
        OracleTrace_Printf("retail:allocated line=%d\n", origins_line());
    }
    return origins_scenario == 111 ? 0 : origins_scenario == 112 ? &origins_unlisted : statement;
}

static void origins_initialize(int n, int rust)
{
    int i;
    origins_scenario = n; origins_nest = 0; origins_resets = 0;
    origins_find = rust ? OracleRust_OriginFind : OracleAbi_COriginFind;
    origins_walk = rust ? OracleRust_OriginWalk : OracleAbi_COriginWalk;
    origins_chain = rust ? OracleRust_PrintChain : OracleAbi_CPrintChain;
    origins_line = rust ? OracleRust_InlineLine : OracleAbi_CInlineLine;
    origins_inline = rust ? inline_statement : OracleAbi_CInlineStatement;
    origins_allocate = rust ? fn_00506cd0 : OracleAbi_CAllocate;
    pcode_initialize(0);
    gPCodeBlocks = 0; locals = temporary_objects = 0;
    origins_cse = "0";
    for (i = 0; i < 3; ++i) {
        scalar_zero(&origins_calls[i], sizeof(ENode));
        scalar_zero(&origins_refs[i], sizeof(ENode));
        scalar_zero(&origins_statements[i], sizeof(Statement));
        origins_refs[i].type = EOBJREF;
        origins_calls[i].type = EFUNCCALL; origins_calls[i].data.funccall.funcref = &origins_refs[i];
        origins_statements[i].next = i == 2 ? 0 : &origins_statements[i + 1];
        origins_statements[i].type = ST_EXPRESSION;
        origins_statements[i].expr = &origins_calls[i];
        origins_statements[i].sourceoffset = i == 0 ? -2147483000L : i == 1 ? 2147483000L : -1;
        origins_queries[i] = &origins_refs[i];
        origins_args[i].next = i == 2 ? 0 : &origins_args[i + 1];
        origins_args[i].node = &origins_calls[i];
    }
    scalar_zero(&origins_tree, sizeof(origins_tree));
    scalar_zero(&origins_result, sizeof(origins_result));
    origins_result.type = EFUNCCALL; origins_result.data.funccall.funcref = origins_refs;
    origins_new_link.name[0] = 'N'; origins_new_link.name[1] = 0;
    origins_initializing = 1;
    origins_allocate(&pcode_function);
    origins_initializing = 0;
    if (origins_resets != 1 || origins_line() != -1) failures |= 128;
}

static void origins_inspect(void)
{
    int i;
    for (i = 0; i < 3; ++i) {
        OracleTrace_Printf("find=%d\n", origins_find(origins_queries[i]));
        origins_chain(origins_queries[i]);
        pcode_string("chain:end");
    }
}

static void origins_case(int n, int rust)
{
    static unsigned char unary[] = {EPOSTINC,EPOSTDEC,EPREINC,EPREDEC,EINDIRECT,EMONMIN,EBINNOT,ELOGNOT,EFORCELOAD,ETYPCON,EBITFIELD};
    static unsigned char binary[] = {EMUL,EMULV,EDIV,EMODULO,EADDV,ESUBV,EADD,ESUB,ESHL,ESHR,ELESS,EGREATER,ELESSEQU,
        EGREATEREQU,EEQU,ENOTEQU,EAND,EXOR,EOR,ELAND,ELOR,EASS,EMULASS,EDIVASS,EMODASS,EADDASS,ESUBASS,ESHLASS,
        ESHRASS,EANDASS,EXORASS,EORASS,ECOMMA,EPMODULO,EROTL,EROTR,EBCLR,EBTST,EBSET,ENULLCHECK,EMFPOINTER};
    int i, parent;
    if (n < 8) {
        if (n == 0) origins_queries[0] = 0;
        origins_tree.type = n == 1 ? EOBJREF : n == 2 ? 255 : EINDIRECT;
        origins_tree.data.monadic = n == 7 ? &origins_tree : origins_calls;
        if (n == 0) origins_walk(0, -1, "root", 0);
        else if (n == 3 || n == 4) origins_walk(origins_calls, -1, "root", n == 3 ? 201 : 200);
        else origins_walk(&origins_tree, -1, "root", n == 5 ? 199 : n == 6 ? 200 : 0);
    } else if (n < 19) {
        origins_tree.type = unary[n - 8]; origins_tree.data.monadic = origins_calls;
        origins_walk(&origins_tree, -1, "unary", 0);
    } else if (n < 60) {
        origins_tree.type = binary[n - 19]; origins_tree.data.diadic.left = origins_calls;
        origins_tree.data.diadic.right = &origins_calls[1];
        origins_walk(&origins_tree, -1, "binary", 0);
    } else if (n < 62) {
        origins_tree.type = n == 60 ? ECOND : ECONDASS;
        origins_tree.data.cond.cond = origins_calls; origins_tree.data.cond.expr1 = &origins_calls[1];
        origins_tree.data.cond.expr2 = &origins_calls[2]; origins_walk(&origins_tree, -1, "conditional", 0);
    } else if (n < 66) {
        origins_tree.type = n == 63 ? EFUNCCALLP : EFUNCCALL;
        origins_tree.data.funccall.funcref = n == 64 ? &origins_calls[0] : n == 65 ? 0 : &origins_refs[0];
        origins_tree.data.funccall.args = &origins_args[1];
        if (n == 65) origins_args[1].node = 0;
        origins_walk(&origins_tree, -1, "call", 0);
    } else if (n == 66) {
        origins_walk(origins_calls, -1, "old", 0); origins_walk(origins_calls, -1, "new", 0);
    } else if (n < 70) {
        unsigned long offset = n == 67 ? 0 : ((0x3fffcUL - ((unsigned long)origins_collisions & 0x3ffffUL)) & 0x3ffffUL);
        for (i = 0; i < 3; ++i) {
            origins_queries[i] = (ENode *)(origins_collisions + offset + i * 0x40000UL);
            origins_queries[i]->type = EOBJREF;
            origins_tree.type = EFUNCCALL; origins_tree.data.funccall.funcref = origins_queries[i];
            origins_walk(&origins_tree, -1, "collision", 0);
        }
        if (n >= 68 && origins_find(origins_queries[0]) != 65535) failures |= 128;
        if (n == 69) { origins_tree.data.funccall.funcref = origins_queries[1]; origins_walk(&origins_tree, -1, "updated", 0); }
    } else if (n < 72) {
        parent = -1;
        for (i = 0; i < 34; ++i) {
            origins_refs[i].type = EOBJREF;
            origins_tree.type = EFUNCCALL; origins_tree.data.funccall.funcref = &origins_refs[i];
            origins_walk(&origins_tree, parent, i % 2 ? "odd" : "even", 0);
            parent = origins_find(&origins_refs[i]);
        }
        origins_queries[0] = &origins_refs[33];
        if (n == 71) origins_walk(&origins_tree, parent, "cycle", 0);
    } else if (n < 75) {
        origins_tree.type = EFUNCCALL;
        for (i = 0; i < 49153; ++i) {
            origins_refs[i].type = EOBJREF;
            origins_tree.data.funccall.funcref = &origins_refs[i];
            origins_walk(&origins_tree, -1, "capacity", 0);
            if (n == 74 && i < 100) origins_walk(&origins_tree, -1, "duplicate", 0);
        }
        origins_queries[1] = &origins_refs[49151]; origins_queries[2] = &origins_refs[49152];
        if (origins_find(origins_queries[1]) < 0 || origins_find(origins_queries[2]) != -1) failures |= 128;
        if (n == 73) { origins_tree.data.funccall.funcref = origins_refs; origins_walk(&origins_tree, -1, "must-not-update", 0); }
    } else if (n < 80 || n == 117) {
        origins_walk(origins_calls, -1, "before-reset", 0);
        origins_cse = n == 77 ? "0" : "2";
        if (n == 76) {
            origins_local.next = 0; origins_local.object = &scalar_objects[0];
            scalar_objects[0].datatype = DLOCAL; scalar_objects[0].name = &pcode_name; scalar_objects[0].u.var.info = &scalar_info;
            locals = &origins_local; gPCodeBlocks = pcode_blocks;
        }
        origins_allocate(&pcode_function);
        if (origins_resets != 2) failures |= 128;
        if (n == 79) origins_allocate(&pcode_function);
    } else if (n < 108 || n == 116) {
        Statement *result;
        if (n < 98) origins_statements[0].type = n - 79;
        if (n < 98) origins_statements[0].next = 0;
        if (n == 107) origins_walk(origins_calls, -1, "parent", 0);
        result = rust ? generate_inline_statements(&pcode_function, n == 98 ? 0 : origins_statements,
            n == 116 ? 0 : (CInlineInfo *)&scalar_group, n == 116 ? 0 : &origins_result,
            n == 116 ? 0 : (CLabel *)&origins_unlisted, n == 116 ? 0 : &scalar_objects[0], n == 104 ? 128 : n == 105 ? 255 : 1)
            : OracleAbi_CStatements(&pcode_function, n == 98 ? 0 : origins_statements,
            n == 116 ? 0 : (CInlineInfo *)&scalar_group, n == 116 ? 0 : &origins_result,
            n == 116 ? 0 : (CLabel *)&origins_unlisted, n == 116 ? 0 : &scalar_objects[0], n == 104 ? 128 : n == 105 ? 255 : 1);
        if (result != (n == 99 ? 0 : n == 100 ? origins_statements : n == 101 ? &origins_statements[1] :
            n == 102 ? &origins_unlisted : &origins_statements[2])) failures |= 128;
    } else if (n < 114) {
        Statement *result = origins_inline(n == 108 ? 0 : origins_statements);
        if (result != (n == 108 || n == 111 ? 0 : n == 112 ? &origins_unlisted : origins_statements)) failures |= 128;
        if (origins_line() != -1) failures |= 128;
    } else if (n == 114) origins_walk(origins_calls, -1, "negative-level", -1);
    else if (n == 115) origins_walk((ENode *)1, -1, "guarded-invalid-pointer", 201);
    else if (n == 118) {
        origins_tree.type = ECOMMA; origins_tree.data.diadic.left = origins_calls;
        origins_tree.data.diadic.right = &origins_calls[1];
        origins_calls[2].data.funccall.funcref = &origins_tree;
        origins_walk(&origins_calls[2], -1, "indirect", 0);
    } else {
        origins_walk(origins_calls, -1, "outer", 0);
        parent = origins_find(origins_refs);
        origins_walk(&origins_calls[1], parent, "inner", 0);
        origins_walk(&origins_calls[2], origins_find(&origins_refs[1]), "leaf", 0);
    }
    origins_inspect();
}

static void origins_check(void)
{
    int n;
    unsigned long retail, queries, resets;
    origins_active = pcode_active = 1;
    for (n = 0; n < 120; ++n) {
        pcode_phase = 0; pcode_position = 0;
        origins_initialize(n, 0); origins_case(n, 0);
        pcode_size = pcode_position; retail = pcode_retail_calls; queries = environment_calls; resets = origins_resets;
        pcode_phase = 1; pcode_position = 0;
        origins_initialize(n, 1); origins_case(n, 1);
        if (pcode_position != pcode_size || retail != pcode_retail_calls || queries != environment_calls || resets != origins_resets) failures |= 128;
        if (failures) {
#ifdef RUST_ABI_DIAGNOSTICS
            pcode_diagnostic();
#endif
            ExitProcess(n + 1);
        }
    }
    origins_active = pcode_active = 0;
}
