/* Included by runtime.c. Compare the frozen C helpers and Rust on the SAME
 * packed records, so every format/argument byte can be checked without masking. */
#include <stdio.h>
#include "compiler/CMangler.h"
#include "compiler/InterferenceGraph.h"
#define link_name OracleAbi_CLinkName
#define print_instruction OracleAbi_CInstruction
#define snapshot_as OracleAbi_CSnapshot
#define wanted OracleAbi_CWanted
#define schedule_block OracleAbi_CSchedule
#define select_ready_coloring_node OracleAbi_CSelect
#include "pcode_reference.h"
#undef link_name
#undef print_instruction
#undef snapshot_as
#undef wanted
#undef schedule_block
#undef select_ready_coloring_node

extern const char *OracleRust_LinkName(Object *object);
extern void OracleRust_PrintInstruction(const char *prefix, PCodeInstruction *instruction);
extern void OracleRust_Snapshot(const char *prefix, const char *phase, Object *function);
PCodeBlock *gPCodeBlocks;
SInt32 gVirtualRegistersActive;
static unsigned char pcode_expected[65536];
static unsigned long pcode_position, pcode_size;
static int pcode_phase;
#ifdef RUST_ABI_DIAGNOSTICS
static unsigned char pcode_actual[65536];
static void pcode_diagnostic(void)
{
    static const char hex[] = "0123456789abcdef";
    char text[700], *out = text;
    unsigned long i, n, start = 0, written;
    while (start < pcode_size && start < pcode_position && pcode_expected[start] == pcode_actual[start]) ++start;
    sprintf(out, "first difference at %ld; expected %ld bytes, actual %ld\n", start, pcode_size, pcode_position);
    while (*out) ++out;
    start = start > 24 ? start - 24 : 0;
    for (n = 0; n < 2; ++n) {
        unsigned long limit = n ? pcode_position : pcode_size;
        unsigned char *bytes = n ? pcode_actual : pcode_expected;
        for (i = start; i < limit && i < start + 128; ++i) {
            *out++ = hex[bytes[i] >> 4]; *out++ = hex[bytes[i] & 15];
        }
        *out++ = '\n';
    }
    WriteFile(GetStdHandle(-11UL), text, out - text, &written, 0);
}
#endif
static unsigned long pcode_mangles, pcode_retail_calls;
static HashNameNode pcode_name, pcode_link;
static Object pcode_function, pcode_alias, pcode_alias2;
static PCodeInstruction pcode_instructions[2];
static unsigned char pcode_instruction_storage[36 + 24];
static PCodeBlock pcode_blocks[2];
static PCodeBlockLink pcode_successor;
static CColoringNode pcode_nodes[2];
static CColoringList pcode_edges[2];
static HashNameNode *pcode_mangle_result;
static CColoringNode *(*pcode_select)(CColoringNode *, UInt16);
static int pcode_schedule_mode;

static void pcode_byte(unsigned char value)
{
    if (pcode_position >= sizeof(pcode_expected)) {
        failures |= 128;
        return;
    }
    if (pcode_phase == 0)
        pcode_expected[pcode_position] = value;
    else if (pcode_position >= pcode_size || pcode_expected[pcode_position] != value)
        failures |= 128;
#ifdef RUST_ABI_DIAGNOSTICS
    if (pcode_phase != 0) pcode_actual[pcode_position] = value;
#endif
    ++pcode_position;
}

static void pcode_string(const char *text)
{
    do { pcode_byte(*text); } while (*text++);
}

static void pcode_trace(const char *format, char *arguments)
{
    const char *cursor = format;
    int i, width;
#ifdef RUST_ABI_BLOCKS
    if (blocks_active) blocks_note(format, arguments);
#endif
    pcode_string(format);
    while (*cursor) {
        if (*cursor++ != '%') continue;
        while (*cursor >= '0' && *cursor <= '9') ++cursor;
        width = 4;
        if (*cursor == 'l') {
            ++cursor; /* historical x86 long is four bytes */
            if (*cursor == 'l') { ++cursor; width = 8; }
        }
        if (*cursor == 'g' || *cursor == 'f') width = 8;
        if (*cursor == 's') pcode_string(*(char **)arguments);
        else if (*cursor == 'd' || *cursor == 'x' || *cursor == 'g' || *cursor == 'f') {
            for (i = 0; i < width; ++i) pcode_byte(arguments[i]);
        } else failures |= 128;
        arguments += width;
        ++cursor;
    }
}

HashNameNode *CMangler_GetLinkName(Object *object)
{
    if (object != &pcode_function) failures |= 128;
    ++pcode_mangles;
    return pcode_mangle_result;
}

int sprintf(char *output, const char *format, ...)
{
    char *arguments = (char *)(&format + 1);
    char *start = output;
    while (*format) {
        int limit = 0x7fffffff;
        if (*format != '%') { *output++ = *format++; continue; }
        ++format;
        if (*format == '.') {
            limit = 0; ++format;
            while (*format >= '0' && *format <= '9') limit = limit * 10 + *format++ - '0';
        }
        if (*format == 'l') ++format;
        if (*format == 's') {
            const char *text = *(char **)arguments;
            while (*text && limit-- > 0) *output++ = *text++;
        } else if (*format == 'd') {
            long value = *(long *)arguments;
            unsigned long magnitude = value < 0 ? 0UL - (unsigned long)value : (unsigned long)value;
            char digits[16]; int n = 0;
            if (value < 0) *output++ = '-';
            do { digits[n++] = '0' + magnitude % 10; magnitude /= 10; } while (magnitude);
            while (n) *output++ = digits[--n];
        } else failures |= 128;
        arguments += 4; ++format;
    }
    *output = 0;
    return output - start;
}

CColoringNode *select_ready_coloring_node__retail(CColoringNode *list, UInt16 cycle)
{
    ++pcode_retail_calls;
    if (list != pcode_nodes || (cycle != 65530 && cycle != 65531)) failures |= 128;
    return cycle == 65530 ? pcode_nodes : 0;
}

void schedule_block__retail(PCodeBlock *block)
{
    ++pcode_retail_calls;
    if (block != pcode_blocks) failures |= 128;
    if (pcode_schedule_mode) {
        if (pcode_select(pcode_nodes, 65530) != pcode_nodes || pcode_select(pcode_nodes, 65531) != 0)
            failures |= 128;
        /* The after list must be read after retail has changed its order. */
        pcode_instructions[1].next = &pcode_instructions[0];
        pcode_instructions[0].next = 0;
        block->instructions = &pcode_instructions[1];
    }
}

static void pcode_initialize(int scenario)
{
    int i;
    pcode_name.name[0] = 'n'; pcode_name.name[1] = 0;
    pcode_link.name[0] = 'L'; pcode_link.name[1] = 0;
    pcode_function.datatype = DFUNC;
    pcode_function.name = &pcode_name;
    pcode_function.u.func.linkname = &pcode_link;
    pcode_alias.datatype = pcode_alias2.datatype = DALIAS;
    pcode_alias.u.alias.object = &pcode_alias2;
    pcode_alias2.u.alias.object = &pcode_function;
    pcode_mangle_result = &pcode_link;
    pcode_nodes[0].prev = &pcode_nodes[1]; pcode_nodes[1].prev = 0;
    pcode_nodes[0].obj = &pcode_instructions[0]; pcode_nodes[1].obj = &pcode_instructions[1];
    pcode_nodes[0].conflicts = pcode_edges; pcode_nodes[1].conflicts = 0;
    pcode_nodes[0].flag = 0; pcode_nodes[1].flag = 1;
    pcode_nodes[0].latency = 65535; pcode_nodes[0].height = 65534;
    pcode_nodes[0].latestCycle = 65533; pcode_nodes[0].earliestCycle = 65532;
    pcode_edges[0].next = &pcode_edges[1]; pcode_edges[1].next = 0;
    pcode_edges[0].owner = &pcode_nodes[1]; pcode_edges[1].owner = &pcode_nodes[0];
    pcode_edges[0].value = 65535; pcode_edges[1].value = 32768;
    pcode_blocks[0].next = &pcode_blocks[1]; pcode_blocks[1].next = 0;
    pcode_blocks[0].index = -12345; pcode_blocks[0].execution_weight = -2147483000L;
    pcode_blocks[0].flags = 65535; pcode_blocks[0].successors = &pcode_successor;
    pcode_successor.next = 0; pcode_successor.payload.block = &pcode_blocks[1];
    pcode_blocks[0].instructions = &pcode_instructions[0]; pcode_blocks[1].instructions = 0;
    pcode_instructions[0].next = &pcode_instructions[1]; pcode_instructions[1].next = 0;
    for (i = 0; i < 2; ++i) {
        pcode_instructions[i].operand_count = 1;
        pcode_instructions[i].opcode = -32760;
        pcode_instructions[i].flags = 0xfedcba98;
        pcode_instructions[i].f18[1] = -2147483000L;
    }
    for (i = 0; i < sizeof(pcode_instruction_storage); ++i) pcode_instruction_storage[i] = (unsigned char)(i * 7);
    ((PCodeInstruction *)pcode_instruction_storage)->operand_count = scenario == 8 ? 0 : scenario == 9 ? -1 : 2;
    pcode_schedule_mode = scenario == 17;
    if (scenario == 16) pcode_blocks[0].instructions = 0;
    gPCodeBlocks = scenario == 11 ? 0 : pcode_blocks;
    gVirtualRegistersActive = -2147483000L;
    setting = scenario == 13 ? 0 : scenario == 14 ? "" : scenario == 15 ? "0" : "2";
    pcode_retail_calls = pcode_mangles = environment_calls = 0;
}

static void pcode_case(int scenario, int rust)
{
    Object *object = &pcode_function;
    const char *name;
    pcode_select = rust ? select_ready_coloring_node : OracleAbi_CSelect;
    if (scenario < 8) {
        if (scenario == 0) object = 0;
        if (scenario == 1) object = &pcode_alias;
        if (scenario >= 2 && scenario <= 4) pcode_function.u.func.linkname = 0;
        if (scenario == 3 || scenario == 4) pcode_mangle_result = 0;
        if (scenario == 4 || scenario == 6) pcode_function.name = 0;
        if (scenario == 5 || scenario == 6) pcode_function.datatype = DLOCAL;
        if (scenario == 7) pcode_function.datatype = DVFUNC;
        name = rust ? OracleRust_LinkName(object) : OracleAbi_CLinkName(object);
        OracleTrace_Printf("%s", name);
    } else if (scenario <= 10) {
        if (rust) OracleRust_PrintInstruction("qx.i", (PCodeInstruction *)pcode_instruction_storage);
        else OracleAbi_CInstruction("qx.i", (PCodeInstruction *)pcode_instruction_storage);
    } else if (scenario <= 12) {
        if (rust) OracleRust_Snapshot("st", "codegen", &pcode_function);
        else OracleAbi_CSnapshot("st", "codegen", &pcode_function);
    } else {
        if (rust) schedule_block(pcode_blocks);
        else OracleAbi_CSchedule(pcode_blocks);
    }
}

static void pcode_check(void)
{
    int scenario;
    unsigned long expected_mangles, expected_retail, expected_environment;
    pcode_active = 1;
    for (scenario = 0; scenario < 18; ++scenario) {
        pcode_phase = 0; pcode_position = 0;
        pcode_initialize(scenario); pcode_case(scenario, 0);
        pcode_size = pcode_position;
        expected_mangles = pcode_mangles; expected_retail = pcode_retail_calls; expected_environment = environment_calls;
        pcode_phase = 1; pcode_position = 0;
        pcode_initialize(scenario); pcode_case(scenario, 1);
        if (pcode_position != pcode_size || pcode_mangles != expected_mangles ||
            pcode_retail_calls != expected_retail || environment_calls != expected_environment) failures |= 128;
    }
    pcode_active = 0;
}
