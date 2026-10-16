#ifndef ORACLE_TRACE_H
#define ORACLE_TRACE_H

/*
 * Trace points for the oracle build: the register allocator and the inliner say what they decide, and why.
 *
 * Nothing is compiled unless MWCC_ORACLE_TRACE is defined (the oracle build defines it), so a matching build
 * is byte for byte what it was without the trace points. Use double parentheses, as the macro has no variadic form in
 * the compiler this builds with:
 *
 *     ORACLE_TRACE(("ra.color %s r%d\n", function_name, color));
 *
 * Lines are "<pass>.<event> key=value ..." text, one event per line, written to the file named by the environment
 * variable MWCC_ORACLE_TRACE_FILE (else to standard error). mwcc-tools reads them.
 */
#ifdef MWCC_ORACLE_TRACE
extern void OracleTrace_Printf(const char *format, ...);
extern int OracleTrace_Enabled(void);
#define ORACLE_TRACE(args) OracleTrace_Printf args
#define ORACLE_TRACE_IF(condition, args) do { if (condition) OracleTrace_Printf args; } while (0)
#else
#define ORACLE_TRACE(args) ((void) 0)
#define ORACLE_TRACE_IF(condition, args) ((void) 0)
#endif

#endif
