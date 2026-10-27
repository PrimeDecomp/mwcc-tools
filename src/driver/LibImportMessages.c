#define CERROR_FILE "unknown.c"
#include "compiler/common.h"
#include "driver/LibImportMessages.h"
#include "compiler/objects.h"
#include "compiler/Unmangle.h"
#include "driver/COSToolsCLT.h"
#include "driver/libimp-eabi-ppc.h"
#include <stdio.h>

/* The linker's version (LINKER, built from the 2.x driver's) has more: a message kept in the buffer to be printed
   with the next, one printed without being counted, the undefined and duplicate symbol errors (two lines each),
   the counts reset and the jump back with status 1. Its messages are printed by the linker's own copies of the
   driver's functions, and its %s names are unmangled into a buffer of 0x800. */
#ifdef LINKER
extern void fn_00420270(char *message, void *argument);   /* a message, not counted */
extern void fn_00420290(char *message, void *argument);   /* a diagnostic */
extern void fn_004202b0(char *message, void *argument);   /* an error (its second line, or NULL) */
extern void fn_004202d0(void *argument, short request);
extern void fn_004202f0(char *value);
#define PRINT_DIAGNOSTIC fn_00420290
#define PRINT_LIMITED fn_004202b0
#define DISPATCH_TWO fn_004202f0
#define DISPATCH_OTHER fn_004202d0
#define UNMANGLE Unmangle_UnmangleSymbolName
#define UNMANGLED_SIZE 0x800
#else
#define PRINT_DIAGNOSTIC fn_0041e990
#define PRINT_LIMITED fn_0041e970
#define DISPATCH_TWO fn_0041ede0
#define DISPATCH_OTHER fn_0041ee00
#define UNMANGLE Unmangle_UnmangleSymbolName
#define UNMANGLED_SIZE 256
#endif

SInt32 limited_diagnostic_count;
char data_00587958;
SInt32 data_00588228;

#ifdef NONMATCHING /* (the target's walk of the variadic arguments: AP is a va_list from va_start) */
#define VA_ARG(ap, T) __builtin_va_arg(*(va_list *)&(ap), T)
#else
#define VA_ARG(ap, T) (*(T *)((ap += 4) - 4))
#endif

SInt16 limited_diagnostic_limit = 100;
SInt16 data_005511b4 = 2000;
static char message_buffer[0x800];

void format_string(char *buf, int size, char *fmt, char *ap)
{
    char c;
    char *s;
    unsigned char *p;
    int len;
    int n;
    char ch;
    char tmp[UNMANGLED_SIZE];
    while ((c = *fmt++) != 0) {
        if (c == '%') {
            switch (c = *fmt++) {
                case 'c':
                    s = VA_ARG(ap, char *);
                    while ((ch = *s) != 0 && size > 1) {
                        *buf = ch;
                        s++;
                        buf++;
                        size--;
                    }
                    break;
                case 'p':
                    p = VA_ARG(ap, unsigned char *);
                    len = *p++;
                    while (len != 0 && size > 1) {
                        *buf++ = *p++;
                        len--;
                        size--;
                    }
                    break;
                case 's':
                    UNMANGLE(VA_ARG(ap, char *), tmp, sizeof(tmp));
                    s = tmp;
                    while ((ch = *s) != 0 && size > 1) {
                        *buf = ch;
                        s++;
                        buf++;
                        size--;
                    }
                    break;
                case 'n':
                    n = VA_ARG(ap, int);
                    if (size > 10) {
                        n = sprintf(buf, "%ld", n);
                        buf += n;
                        size -= n;
                    }
                    break;
                case 'h':
                    n = VA_ARG(ap, int);
                    if (size > 11) {
                        n = sprintf(buf, "0x%.8X", n);
                        buf += n;
                        size -= n;
                    }
                    break;
                default:
                    if (size > 1) {
                        *buf++ = c;
                        size--;
                    }
                    break;
            }
        } else if (size > 1) {
            *buf++ = c;
            size--;
        }
    }
    *buf = 0;
}

#ifdef LINKER
/* format_string with its arguments after FMT. */
void format_message(char *buf, int size, char *fmt, ...)
{
    char *args;

#ifdef NONMATCHING
    format_string(buf, size, fmt, (char *)__va_start(fmt));
#else
    format_string(buf, size, fmt, ((char *)(&fmt + 1) - (args = (char *)&fmt) + 3) / 4 * 4 + args);
#endif
}

/* The error of an undefined symbol NAME, referred to by REFERRER in FILE (two lines: 0xf, 0x10), counted with the
   limited diagnostics. */
void CompilerTools_ReportUndefined(char *name, long unused, char *referrer, char *file)
{
    char first[0x800];
    char second[0x800];
    char format[0x100];

    if (limited_diagnostic_count < limited_diagnostic_limit) {
        COS_GetString(format, 0x2af9, 0xf);
        format_message(first, 0x800, format, name);
        COS_GetString(format, 0x2af9, 0x10);
        format_message(second, 0x800, format, referrer, file);
        PRINT_LIMITED(first, second);
        limited_diagnostic_count++;
    } else if (limited_diagnostic_count == limited_diagnostic_limit) {
        CompilerTools_ReportLimitedDiagnostic(25);
    }
}

/* The error of a symbol NAME defined in FILE and in OTHER (two lines: 0x11, 0x14), counted with the limited
   diagnostics. */
void CompilerTools_ReportDuplicate(char *name, char *unused, char *file, char *other)
{
    char first[0x800];
    char second[0x800];
    char format[0x100];

    if (limited_diagnostic_count < limited_diagnostic_limit) {
        COS_GetString(format, 0x2af9, 0x11);
        format_message(first, 0x800, format, name, "", file);
        COS_GetString(format, 0x2af9, 0x14);
        format_message(second, 0x800, format, other);
        PRINT_LIMITED(first, second);
        limited_diagnostic_count++;
    } else if (limited_diagnostic_count == limited_diagnostic_limit) {
        CompilerTools_ReportLimitedDiagnostic(25);
    }
}

/* A message, printed whatever the counts are and not counted. */
void CompilerTools_ReportMessage(SInt32 diagnosticCode, ...)
{
    char message[0x800];
    char format[0x100];
    char *args;

    COS_GetString(format, 0x2af9, diagnosticCode);
#ifdef NONMATCHING
    format_string(message, sizeof(message), format, (char *)__va_start(diagnosticCode));
#else
    format_string(message, sizeof(message), format,
                  (((char *)(&diagnosticCode + 1) - (args = (char *)&diagnosticCode)) + 3) / 4 * 4 + args);
#endif
    fn_00420270(message, "");
}
#endif

unsigned char CompilerTools_ReportDiagnostic(SInt32 diagnosticCode, ...)
{
    char message[0x800];
    char format[0x100];
    va_list args;
#if VERSION >= VERSION_GC_1_3
    char *base;
#endif

    if (data_00587958 == 0 && data_00588228 < data_005511b4) {
        COS_GetString(format, 0x2af9, diagnosticCode);
#ifdef NONMATCHING
        va_start(args, diagnosticCode);
#elif VERSION >= VERSION_GC_1_3
        base = (char *)&diagnosticCode;
        args = base + ((va_list)(&diagnosticCode + 1) - base + 3) / 4 * 4;
#else
        args = (va_list)&diagnosticCode + ((va_list)(&diagnosticCode + 1) - (va_list)&diagnosticCode + 3) / 4 * 4;
#endif
        format_string(message, sizeof(message), format, args);
        PRINT_DIAGNOSTIC(message, "");
        data_00588228++;
    }
}

void CompilerTools_ReportLimitedDiagnostic(SInt32 diagnosticCode, ...)
{
    va_list args;
    char message[2048];
    char format[256];

    if (limited_diagnostic_count < limited_diagnostic_limit) {
        COS_GetString(format, 0x2af9, diagnosticCode);
#if VERSION >= VERSION_GC_1_3
#ifdef NONMATCHING
        format_string(message, sizeof(message), format, (char *)__va_start(diagnosticCode));
#else
        format_string(message, sizeof(message), format,
                      (((char *)(&diagnosticCode + 1) - (args = (char *)&diagnosticCode)) + 3) / 4 * 4 + args);
#endif
#else
        args = (va_list)&diagnosticCode + (((va_list)(&diagnosticCode + 1) - (va_list)&diagnosticCode + 3) / 4) * 4;
        format_string(message, sizeof(message), format, args);
#endif
        PRINT_LIMITED(message, "");
        limited_diagnostic_count++;
    } else if (limited_diagnostic_count == limited_diagnostic_limit) {
        COS_GetString(format, 0x2af9, 25);
#if VERSION >= VERSION_GC_1_3
#ifdef NONMATCHING
        format_string(message, sizeof(message), format, (char *)__va_start(diagnosticCode));
#else
        format_string(message, sizeof(message), format,
                      (((char *)(&diagnosticCode + 1) - (args = (char *)&diagnosticCode)) + 3) / 4 * 4 + args);
#endif
#else
        args = (va_list)&diagnosticCode + (((va_list)(&diagnosticCode + 1) - (va_list)&diagnosticCode + 3) / 4) * 4;
        format_string(message, sizeof(message), format, args);
#endif
        PRINT_LIMITED(message, NULL);
        limited_diagnostic_count++;
    }
}

#ifdef LINKER
/* A limited diagnostic's first part, kept in the message buffer for CompilerTools_ReportBufferedDiagnostic: neither
   printed nor counted. */
void CompilerTools_BufferDiagnostic(SInt32 diagnosticCode, ...)
{
    char format[0x100];
    char *args;

    if (limited_diagnostic_count < limited_diagnostic_limit) {
        COS_GetString(format, 0x2af9, diagnosticCode);
#ifdef NONMATCHING
        format_string(message_buffer, 0x800, format, (char *)__va_start(diagnosticCode));
#else
        format_string(message_buffer, 0x800, format,
                      (((char *)(&diagnosticCode + 1) - (args = (char *)&diagnosticCode)) + 3) / 4 * 4 + args);
#endif
    }
}

/* A limited diagnostic printed after the message buffer's (its second line). */
void CompilerTools_ReportBufferedDiagnostic(SInt32 diagnosticCode, ...)
{
    char message[0x800];
    char format[0x100];
    char *args;

    if (limited_diagnostic_count < limited_diagnostic_limit) {
        COS_GetString(format, 0x2af9, diagnosticCode);
#ifdef NONMATCHING
        format_string(message, sizeof(message), format, (char *)__va_start(diagnosticCode));
#else
        format_string(message, sizeof(message), format,
                      (((char *)(&diagnosticCode + 1) - (args = (char *)&diagnosticCode)) + 3) / 4 * 4 + args);
#endif
        PRINT_LIMITED(message_buffer, message);
    } else if (limited_diagnostic_count == limited_diagnostic_limit) {
        COS_GetString(format, 0x2af9, 25);
#ifdef NONMATCHING
        format_string(message, sizeof(message), format, (char *)__va_start(diagnosticCode));
#else
        format_string(message, sizeof(message), format,
                      (((char *)(&diagnosticCode + 1) - (args = (char *)&diagnosticCode)) + 3) / 4 * 4 + args);
#endif
        PRINT_LIMITED(message, NULL);
    } else {
        return;
    }
    limited_diagnostic_count++;
}
#endif

/* (called with arguments for the message: wasm's call of a variadic function has one more parameter) */
#ifdef NONMATCHING
void CompilerTools_FormatMessageAndLongjmp(int message, int status, ...)
#else
void CompilerTools_FormatMessageAndLongjmp(int message, int status)
#endif
{
    va_list args;
    char buf[256];

    COS_GetString(buf, 0x2af9, message);
#if VERSION >= VERSION_GC_1_3
#ifdef NONMATCHING
    format_string(message_buffer, 0x800, buf, (char *)__va_start(status));
#else
    format_string(message_buffer, 0x800, buf, (((char *)(&status + 1) - (args = (char *)&status)) + 3) / 4 * 4 + args);
#endif
#else
    args = (va_list)&status + (((va_list)(&status + 1) - (va_list)&status + 3) / 4) * 4;
    format_string(message_buffer, 0x800, buf, args);
#endif
    longjmp(file_input_jmpbuf, status);
}

void CompilerTools_DispatchMessageBufferByMode(int mode)
{
    if (mode != 1) {
        if (mode == 2) {
            DISPATCH_TWO(message_buffer);
        } else {
            DISPATCH_OTHER(message_buffer, mode);
        }
    }
}

#ifdef LINKER
/* Back to the link's start, with status 1 (a fatal error, reported already). */
void CompilerTools_Abort(void)
{
    longjmp(file_input_jmpbuf, 1);
}

/* The diagnostics' counts set back to none. */
void CompilerTools_ResetDiagnosticCounts(void)
{
    limited_diagnostic_count = data_00588228 = 0;
}
#endif

/* The linker stripped the function that used this literal; it stays in the unit's .data. */
static const char *CompilerTools_MemoryErrorMessage(void)
{
    return "Internal Memory Error";
}
