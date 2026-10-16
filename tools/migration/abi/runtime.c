/* Direct ABI checks: unsigned-byte returns, packed reads, C varargs and gating. */
#include "compiler/IroLoop.h"
#ifdef RUST_ABI_PCODE
#include "compiler/PCode.h"
#include "compiler/Scheduler.h"
static int pcode_active;
static void pcode_trace(const char *format, char *arguments);
static void pcode_check(void);
#endif
#ifdef RUST_ABI_BLOCKS
static int blocks_active;
static char *blocks_environment(const char *name);
static void blocks_note(const char *format, char *arguments);
static void blocks_check(void);
#endif
#ifdef RUST_ABI_SCALAR_CSE
static int scalar_active;
static char *scalar_environment(const char *name);
static void scalar_check(void);
#endif
#ifdef RUST_ABI_ORIGINS
static int origins_active;
static char *origins_environment(const char *name);
static void origins_check(void);
#endif
#ifdef RUST_ABI_INSTANTIATION
static int instantiation_active;
static char *instantiation_environment(const char *name);
static void instantiation_check(void);
#endif
#ifdef RUST_ABI_BINDINGS
static int bindings_active;
static char *bindings_environment(const char *name);
static void bindings_check(void);
#endif
#ifdef RUST_ABI_FRAME_HOOK
#include "compiler/objects.h"
#include "compiler/types.h"
#include "compiler/Registers.h"
#include "compiler/CodeGen.h"
extern void fn_004f7fb0(void);
ObjectList *locals;
ObjectList *temporary_objects;
static Object named_object, anonymous_object, other_object;
static HashNameNode frame_name;
static Type frame_type;
static VarInfo frame_info;
static ObjectList frame_list[4];
static unsigned char temporary_storage[9];
static int frame_scenario = -1;
static unsigned long frame_retail_calls;
#endif

__declspec(dllimport) void __stdcall ExitProcess(unsigned int status);
extern Boolean fn_00460c90(IROLinear *node);
static IROLinear node;
static Boolean retail_result;
static const char *setting;
static unsigned long failures;
static unsigned long trace_calls;
static unsigned long environment_calls;

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

Boolean fn_00460c90__retail(IROLinear *input)
{
    if (input != &node)
        failures |= 1;
    return retail_result;
}

char *getenv(const char *name)
{
    ++environment_calls;
#ifdef RUST_ABI_BINDINGS
    if (bindings_active) return bindings_environment(name);
#endif
#ifdef RUST_ABI_INSTANTIATION
    if (instantiation_active) return instantiation_environment(name);
#endif
#ifdef RUST_ABI_ORIGINS
    if (origins_active) return origins_environment(name);
#endif
#ifdef RUST_ABI_SCALAR_CSE
    if (scalar_active) return scalar_environment(name);
#endif
#ifdef RUST_ABI_BLOCKS
    if (blocks_active) return blocks_environment(name);
#endif
#ifdef RUST_ABI_PCODE
    if (pcode_active) {
        if (!equal(name, "MWCC_ORACLE_SCHEDULE")) failures |= 128;
        return (char *)setting;
    }
#endif
    if (!equal(name, "MWCC_ORACLE_LOCALS"))
        failures |= 2;
    return (char *)setting;
}

void OracleTrace_Printf(const char *format, ...)
{
#ifdef RUST_ABI_PCODE
    if (pcode_active) { pcode_trace(format, (char *)(&format + 1)); return; }
#endif
#ifdef RUST_ABI_FRAME_HOOK
    if (frame_scenario >= 0) {
        unsigned long index = trace_calls++;
        if (frame_retail_calls != 1)
            failures |= 128;
        if (index == 0) {
            if (!equal(format, "fr.begin\n"))
                failures |= 128;
        } else if (index == (frame_scenario == 1 ? 4 : 1)) {
            if (!equal(format, "fr.end\n"))
                failures |= 128;
        } else if (frame_scenario == 1 && index <= 3) {
            int *args = (int *)(&format + 1);
            int anonymous = index == 2;
            if (!equal(format, "fr.%s name=%s offset=%d size=%d reg=%d used=%d\n") ||
                !equal((char *)args[0], index == 3 ? "temp" : "local") ||
                !equal((char *)args[1], anonymous ? "?" : "x") ||
                args[2] != (anonymous ? 2147483000L : -123456789L) ||
                args[3] != (anonymous ? -1 : -2147483000L) ||
                args[4] != (anonymous ? -1 : -32760) || args[5] != (anonymous ? -1 : 255))
                failures |= 128;
        } else {
            failures |= 128;
        }
        return;
    }
#endif
    ++trace_calls;
    if (!equal(format, "lc.call node=%d\n") || *(int *)(&format + 1) != 65530)
        failures |= 4;
}

static void check(Boolean result, const char *enabled, unsigned long wanted)
{
    retail_result = result;
    setting = enabled;
    trace_calls = 0;
    environment_calls = 0;
    if (fn_00460c90(&node) != result)
        failures |= 8;
    if (trace_calls != wanted || environment_calls != (result != 0))
        failures |= 16;
}

void main(void)
{
    node.index = 65530;
    check(0, "1", 0);
    check(1, 0, 0);
    check(1, "", 0);
    check(1, "0", 0);
    check(1, "1", 1);
    check(128, "2", 1);
    check(255, "enabled", 1);
#ifdef RUST_ABI_GRAPH_HOOKS
    {
        extern void *fn_00507070(void);
        extern int fn_00506f50(void *stack);
        if (fn_00507070() != 0 || fn_00506f50(0) != 7)
            failures |= 32;
    }
#endif
#ifdef RUST_ABI_FRAME_HOOK
    {
        int i;
        named_object.datatype = DLOCAL;
        named_object.name = &frame_name;
        frame_name.name[0] = 'x';
        frame_name.name[1] = 0;
        named_object.type = &frame_type;
        frame_type.size = -2147483000L;
        named_object.u.var.info = &frame_info;
        named_object.u.var.uid = -123456789L;
        frame_info.reg = -32760;
        frame_info.used = 255;
        anonymous_object.datatype = DLOCAL;
        anonymous_object.u.var.uid = 2147483000L;
        other_object.datatype = DDATA;
        frame_list[0].next = &frame_list[1];
        frame_list[1].object = &other_object;
        frame_list[1].next = &frame_list[2];
        frame_list[2].object = &named_object;
        frame_list[2].next = &frame_list[3];
        frame_list[3].object = &anonymous_object;
        for (i = 0; i < 3; ++i) {
            frame_scenario = i;
            frame_retail_calls = trace_calls = environment_calls = 0;
            locals = temporary_objects = 0;
            fn_004f7fb0();
            if (trace_calls != (i == 1 ? 5 : 2) || frame_retail_calls != 1 || environment_calls != 0)
                failures |= 128;
        }
    }
#endif
#ifdef RUST_ABI_PCODE
    pcode_check();
#endif
#ifdef RUST_ABI_BLOCKS
    blocks_check();
#endif
#ifdef RUST_ABI_SCALAR_CSE
    scalar_check();
#endif
#ifdef RUST_ABI_ORIGINS
    origins_check();
#endif
#ifdef RUST_ABI_INSTANTIATION
    instantiation_check();
#endif
#ifdef RUST_ABI_BINDINGS
    bindings_check();
#endif
    ExitProcess(failures);
}

#ifdef RUST_ABI_GRAPH_HOOKS
/* The synthetic retail prologue intentionally fails the production byte guards. */
void *fn_00507070__retail(void) { return 0; }
int fn_00506f50__retail(void *stack) { return stack == 0 ? 7 : 8; }
int fn_004fcb70(char cls) { failures |= 64; return cls; }
void fn_004ceda0(char cls) { failures |= 64; }
unsigned int fn_004fcb20(char cls) { failures |= 64; return 0; }
#endif

#ifdef RUST_ABI_PCODE
#include "pcode_runtime.c"
#endif
#ifdef RUST_ABI_BLOCKS
#include "blocks_runtime.c"
#endif
#ifdef RUST_ABI_SCALAR_CSE
#include "scalar_cse_runtime.c"
#endif
#ifdef RUST_ABI_ORIGINS
#include "origins_runtime.c"
#endif
#ifdef RUST_ABI_INSTANTIATION
#include "instantiation_runtime.c"
#endif
#ifdef RUST_ABI_BINDINGS
#include "bindings_runtime.c"
#endif

#ifdef RUST_ABI_FRAME_HOOK
/* Retail writes the lists: the wrapper must inspect them after this call. */
void fn_004f7fb0__retail(void)
{
    struct TemporaryObjectEntry *entry;
    ++frame_retail_calls;
    if (frame_scenario == 1) {
        /* Deliberately odd address, valid for the historical packed(1) list. */
        entry = (struct TemporaryObjectEntry *)((unsigned long)temporary_storage | 1);
        entry->next = 0;
        entry->object = &named_object;
        locals = frame_list;
        temporary_objects = (ObjectList *)entry;
    } else if (frame_scenario == 2) {
        frame_list[1].next = 0;
        locals = &frame_list[1];
    }
}
#endif
