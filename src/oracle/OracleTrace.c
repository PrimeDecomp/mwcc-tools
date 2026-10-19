/*
 * The oracle build's trace output. Only built by configure.py --oracle; see include/compiler/OracleTrace.h.
 */
#include <stdio.h>
#include <stdlib.h>

static FILE *trace_file;
static int trace_opened;

int OracleTrace_Enabled(void)
{
    return 1;
}

void OracleTrace_Printf(const char *format, ...)
{
    /* (the arguments follow FORMAT on the stack, as in the sources that read their own) */
    char *arguments = (char *)(&format + 1);

    if (!trace_opened) {
        const char *path = getenv("MWCC_ORACLE_TRACE_FILE");

        trace_opened = 1;
        trace_file = path ? fopen(path, "a") : NULL;
        if (!trace_file) {
            trace_file = stderr;
        }
    }
    vfprintf(trace_file, format, arguments);
    fflush(trace_file);
}
