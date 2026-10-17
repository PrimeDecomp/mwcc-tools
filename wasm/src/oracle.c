/* The oracle's C side in the WebAssembly module (crates/mwcc-oracle-core/src/wasm_oracle.rs): what the x86 oracle's
 * Rust trace shim and the probes' islands give the hooks. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static FILE* trace_file;
static int trace_opened;

/* The trace writer: MWCC_ORACLE_TRACE_FILE, appended to, or else stderr; flushed after each record. */
void OracleTrace_Printf(const char* format, ...)
{
    va_list arguments;
    if (!trace_opened) {
        const char* path = getenv("MWCC_ORACLE_TRACE_FILE");
        trace_opened = 1;
        trace_file = path ? fopen(path, "a") : NULL;
        if (!trace_file) {
            trace_file = stderr;
        }
    }
    va_start(arguments, format);
    vfprintf(trace_file, format, arguments);
    va_end(arguments);
    fflush(trace_file);
}

/* IroDump_Print's hook takes its arguments as a va_list, a pointer to them; this is its variadic entry, under gc2's
 * name for the function. */
void ORACLE_IRO_DUMP_ARGUMENTS(const char* message, va_list arguments);

void ORACLE_IRO_DUMP(const char* message, ...)
{
    va_list arguments;
    va_start(arguments, message);
    ORACLE_IRO_DUMP_ARGUMENTS(message, arguments);
    va_end(arguments);
}

/* The stand-ins for the probes' islands, as in the source-built oracle: the arena globals are zero, and the forced-GPR
 * entry, which only an installed probe calls, traps. */
unsigned char OracleRust_ArenaGlobals[12];

unsigned long OracleRust_ArgumentForceGprRetail(unsigned long operand, unsigned long type, unsigned long requested)
{
    abort();
    return 0;
}
