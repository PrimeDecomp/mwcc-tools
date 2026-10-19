/* The oracle's C side in the WebAssembly module (crates/mwcc-oracle-core/src/wasm_oracle.rs): what the x86 oracle's
 * Rust trace shim and the probes' islands give the hooks. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* The host's trace sink (crates/mwcc-wasm): whether this run traces, and a record's bytes, appended to the run's
 * trace. */
__attribute__((import_module("env"), import_name("oracle_tracing"))) int oracle_tracing(void);
__attribute__((import_module("env"), import_name("oracle_trace"))) void oracle_trace(const char* data, size_t size);
__attribute__((import_module("env"), import_name("oracle_scope"))) void oracle_scope(int entering);

/* A function's scope (include/compiler/OracleProbes.h's ORACLE_SCOPE): the host keeps its records or not. */
void OracleTrace_Scope(int entering)
{
    oracle_scope(entering);
}

/* Whether the run traces (include/compiler/OracleProbes.h): asked of the host once, before main. A run that does not
 * skips the probes and the hooks (wasm_oracle.rs's dispatchers), and formats nothing. */
int OracleTrace_On;

__attribute__((constructor)) static void trace_on(void)
{
    OracleTrace_On = oracle_tracing();
}

/* The trace writer: each record formatted here and given to the host, which keeps the run's trace (and writes
 * MWCC_ORACLE_TRACE_FILE once, at the end). */
void OracleTrace_Printf(const char* format, ...)
{
    char buffer[1024];
    char* text = buffer;
    va_list arguments;
    int size;
    if (!OracleTrace_On) {
        return;
    }
    va_start(arguments, format);
    size = vsnprintf(buffer, sizeof buffer, format, arguments);
    va_end(arguments);
    if (size < 0) {
        return;
    }
    if ((size_t)size >= sizeof buffer) {
        text = malloc((size_t)size + 1);
        if (!text) {
            return;
        }
        va_start(arguments, format);
        vsnprintf(text, (size_t)size + 1, format, arguments);
        va_end(arguments);
    }
    oracle_trace(text, (size_t)size);
    if (text != buffer) {
        free(text);
    }
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

/* The stand-in for a probe's island, as in the source-built oracle: the forced-GPR entry, which only an installed probe
 * calls, traps. (gc2 defines the arena globals, under ORACLE_PROBES.) */

unsigned long OracleRust_ArgumentForceGprRetail(unsigned long operand, unsigned long type, unsigned long requested)
{
    abort();
    return 0;
}
