#include "compiler/CFunc.h"
#define link_name OracleAbi_CLinkName
#define maybe_link OracleAbi_CMaybeLink
#define print_instruction OracleAbi_CInstruction
#define snapshot_as OracleAbi_CSnapshot
#define wanted OracleAbi_CWanted
#define fn_00436390 OracleAbi_CStatement
#define COptimizer_Optimize OracleAbi_COptimize
#define Peephole_MergeAdjacentBlocks OracleAbi_CMerge
#include "blocks_reference.h"
#undef link_name
#undef maybe_link
#undef print_instruction
#undef snapshot_as
#undef wanted
#undef fn_00436390
#undef COptimizer_Optimize
#undef Peephole_MergeAdjacentBlocks

extern void fn_00436390(ENode *expression);
extern void COptimizer_Optimize(Object *function);
extern void Peephole_MergeAdjacentBlocks(Object *object, Boolean prologue);
Statement *gCurrentStatement;
PCodeBlock *gCurrentBlock;
static Statement blocks_statement;
static PCodeBlockLink blocks_pred, blocks_extra;
static const char *blocks_setting, *stages_setting;
static int blocks_scenario;
static Boolean blocks_prologue;
static unsigned int blocks_why_mask;

static char *blocks_environment(const char *name)
{
    pcode_string(name); /* Observe option-query ordering as well as its count. */
    if (equal(name, "MWCC_ORACLE_BLOCKS")) return (char *)blocks_setting;
    if (equal(name, "MWCC_ORACLE_STAGES")) return (char *)stages_setting;
    failures |= 128;
    return 0;
}

static const char *blocks_reason(void)
{
    int n = blocks_scenario;
    if (n == 16) return "prologue";
    if (n == 17 || n == 31) return "empty";
    if (n == 18 || n == 19) return "call";
    if (n == 20) return "branch";
    if (n >= 21 && n <= 23) return "successors";
    if (n >= 24 && n <= 26) return "predecessors";
    if (n == 27) return "epilogue";
    if (n == 28 || n == 34) return "flag";
    if (n == 29) return "over100";
    return "-";
}

static void blocks_note(const char *format, char *arguments)
{
    static const char *reasons[] = {"prologue", "empty", "call", "branch", "successors",
                                   "predecessors", "epilogue", "flag", "over100", "-"};
    int i;
    if (equal(format, "bk.pair a=%08x b=%08x counts=%d+%d why=%s\n")) {
        const char *why = ((char **)arguments)[4];
        if (!equal(why, blocks_reason())) failures |= 128;
        if (pcode_phase == 0)
            for (i = 0; i < 10; ++i) if (equal(why, reasons[i])) blocks_why_mask |= 1U << i;
    }
}

void fn_00436390__retail(ENode *expression)
{
    if (expression != (ENode *)&pcode_function) failures |= 128;
    ++pcode_retail_calls;
    pcode_string("retail:statement");
    gCurrentBlock = blocks_scenario == 4 ? 0 : &pcode_blocks[1];
    gCurrentStatement = blocks_scenario == 4 ? 0 : &blocks_statement;
    if (blocks_scenario == 40) blocks_setting = "2";
    if (blocks_scenario == 41) blocks_setting = "0";
}

void COptimizer_Optimize__retail(Object *function)
{
    if (function != (blocks_scenario == 11 ? 0 : &pcode_function)) failures |= 128;
    ++pcode_retail_calls;
    pcode_string("retail:optimize");
    gPCodeBlocks = &pcode_blocks[1];
    if (blocks_scenario == 9) { blocks_setting = "2"; stages_setting = "1"; }
    if (blocks_scenario == 10) { blocks_setting = stages_setting = "0"; }
}

void Peephole_MergeAdjacentBlocks__retail(Object *object, Boolean prologue)
{
    if (object != (blocks_scenario == 13 ? 0 : &pcode_function) || prologue != blocks_prologue) failures |= 128;
    ++pcode_retail_calls;
    pcode_string("retail:merge"); pcode_byte(prologue);
    /* Check that merged lists reflect retail's changes, not the before state. */
    if (gPCodeBlocks) { gPCodeBlocks->next = 0; gPCodeBlocks->instruction_count = -1234; }
    if (blocks_scenario == 37) blocks_setting = "0";
    if (blocks_scenario == 39) blocks_setting = "2";
}

static void blocks_initialize(int scenario)
{
    pcode_initialize(0);
    blocks_scenario = scenario;
    blocks_setting = "2"; stages_setting = "0"; blocks_prologue = 0;
    gCurrentBlock = pcode_blocks; gCurrentStatement = &blocks_statement;
    blocks_statement.sourceoffset = -2147483000L;
    pcode_blocks[0].flags = pcode_blocks[1].flags = 0;
    pcode_blocks[0].instruction_count = 10; pcode_blocks[1].instruction_count = 20;
    pcode_blocks[0].predecessors = 0; pcode_blocks[1].successors = 0;
    pcode_blocks[1].predecessors = &blocks_pred;
    blocks_pred.next = 0; blocks_pred.payload.block = &pcode_blocks[0];
    blocks_extra.next = 0; blocks_extra.payload.block = 0;
    pcode_blocks[0].reverse_instructions = &pcode_instructions[0];
    pcode_blocks[1].reverse_instructions = &pcode_instructions[1];
    pcode_instructions[0].flags = pcode_instructions[1].flags = 0;
    pcode_instructions[0].opcode = pcode_instructions[1].opcode = PC_B;
    pcode_instructions[0].next = 0;
    pcode_blocks[1].instructions = &pcode_instructions[1];
    if (scenario == 0 || scenario == 12 || scenario == 39 || scenario == 40) blocks_setting = "0";
    if (scenario == 1) { gCurrentBlock = 0; pcode_blocks[1].instruction_count = -32760; }
    if (scenario == 2) blocks_statement.sourceoffset = 2147483000L;
    if (scenario == 3) blocks_setting = "";
    if (scenario == 5) { blocks_setting = 0; stages_setting = ""; }
    if (scenario == 6 || scenario == 11 || scenario == 36) blocks_setting = "1";
    if (scenario == 7 || scenario == 10) stages_setting = "1";
    if (scenario == 8) { blocks_setting = "0"; stages_setting = "1"; }
    if (scenario == 9) blocks_setting = stages_setting = "0";
    if (scenario == 13) gPCodeBlocks = 0;
    if (scenario == 14) pcode_blocks[0].next = 0;
    if (scenario == 16) { pcode_blocks[0].flags = 1; pcode_blocks[0].instruction_count = 0; pcode_instructions[0].flags = 0x108; }
    if (scenario == 17) { pcode_blocks[0].instruction_count = 0; pcode_instructions[0].flags = 0x108; }
    if (scenario == 18) pcode_instructions[0].flags = 0x100;
    if (scenario == 19) pcode_instructions[0].flags = 8;
    if (scenario == 20) { pcode_instructions[0].flags = 1; pcode_instructions[0].opcode = 1; }
    if (scenario == 21) pcode_blocks[0].successors = 0;
    if (scenario == 22) pcode_successor.payload.block = &pcode_blocks[0];
    if (scenario == 23) pcode_successor.next = &blocks_extra;
    if (scenario == 24) pcode_blocks[1].predecessors = 0;
    if (scenario == 25) blocks_pred.payload.block = &pcode_blocks[1];
    if (scenario == 26) blocks_pred.next = &blocks_extra;
    if (scenario == 27) pcode_blocks[1].flags = 2;
    if (scenario == 28 || scenario == 34) pcode_blocks[1].flags = 0x8000;
    if (scenario == 29 || scenario == 30) { pcode_blocks[0].instruction_count = 50; pcode_blocks[1].instruction_count = scenario == 29 ? 51 : 50; }
    if (scenario == 31) pcode_blocks[0].instruction_count = -32768;
    if (scenario == 32) { pcode_blocks[0].instruction_count = 32767; pcode_blocks[1].instruction_count = -32768; }
    if (scenario == 33) { blocks_prologue = 128; pcode_blocks[0].flags = 1; pcode_blocks[1].flags = 2; }
    if (scenario == 34) blocks_prologue = 255;
    if (scenario == 35) pcode_instructions[0].flags = 1; /* Unconditional B may merge. */
    if (scenario == 38) pcode_blocks[1].instruction_count = 0;
}

static void blocks_case(int scenario, int rust)
{
    Object *object = &pcode_function;
    if (scenario <= 4 || scenario >= 40) {
        if (rust) fn_00436390((ENode *)object); else OracleAbi_CStatement((ENode *)object);
    } else if (scenario <= 11) {
        if (scenario == 11) object = 0;
        if (rust) COptimizer_Optimize(object); else OracleAbi_COptimize(object);
    } else {
        if (scenario == 13) object = 0;
        if (rust) Peephole_MergeAdjacentBlocks(object, blocks_prologue); else OracleAbi_CMerge(object, blocks_prologue);
    }
}

static void blocks_check(void)
{
    int scenario;
    unsigned long expected_mangles, expected_retail, expected_environment;
    blocks_active = pcode_active = 1;
    blocks_why_mask = 0;
    for (scenario = 0; scenario < 42; ++scenario) {
        pcode_phase = 0; pcode_position = 0;
        blocks_initialize(scenario); blocks_case(scenario, 0);
        pcode_size = pcode_position;
        expected_mangles = pcode_mangles; expected_retail = pcode_retail_calls; expected_environment = environment_calls;
        pcode_phase = 1; pcode_position = 0;
        blocks_initialize(scenario); blocks_case(scenario, 1);
        if (pcode_position != pcode_size || pcode_mangles != expected_mangles || pcode_retail_calls != expected_retail ||
            environment_calls != expected_environment) failures |= 128;
    }
    if (blocks_why_mask != 0x3ff) failures |= 128;
    blocks_active = pcode_active = 0;
}
