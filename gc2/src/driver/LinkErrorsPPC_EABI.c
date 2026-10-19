// The library importer's linker-style diagnostics (Wii's LinkErrorsPPC_EABI module; no Pro7
// counterpart).
#include "driver/LinkErrorsPPC_EABI.h"
#include "compiler/Unmangle.h"
#include "driver/COSToolsCLT.h"
#include "driver/ELF_Lib_Import.h"
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>

// (Wii's publics; nothing outside this file uses them)
SInt16 maxerrors = 100;
SInt16 maxwarnings = 2000;
// TODO: invented name (Wii has this static unnamed): linkfailed's message, which linkalert shows
static char message_buffer[0x800];

// TODO: invented name (gc1's; Wii has this static unnamed)
static void format_string(char* buf, int size, char* fmt, va_list ap) {
    char c;
    char* s;
    unsigned char* p;
    int len;
    int n;
    char ch;
#if VERSION < VERSION_GC_2_5
    char tmp[256];
#else
    char tmp[2048];
#endif
    while ((c = *fmt++) != 0) {
        if (c == '%') {
            switch (c = *fmt++) {
                case 'c':
                    s = va_arg(ap, char*);
                    while ((ch = *s) != 0 && size > 1) {
                        *buf = ch;
                        s++;
                        buf++;
                        size--;
                    }
                    break;
                case 'p':
                    p = va_arg(ap, unsigned char*);
                    len = *p++;
                    while (len != 0 && size > 1) {
                        *buf++ = *p++;
                        len--;
                        size--;
                    }
                    break;
                case 's':
                    MWUnmangle(va_arg(ap, char*), tmp, sizeof(tmp));
                    s = tmp;
                    while ((ch = *s) != 0 && size > 1) {
                        *buf = ch;
                        s++;
                        buf++;
                        size--;
                    }
                    break;
                case 'n':
                    n = va_arg(ap, int);
                    if (size > 10) {
                        n = sprintf(buf, "%ld", n);
                        buf += n;
                        size -= n;
                    }
                    break;
                case 'h':
                    n = va_arg(ap, int);
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

void linkwarning(SInt32 code, ...) {
    char message[0x800];
    char format[0x100];
    va_list args;

    if (linkinfo.nowarnings == 0 && linkerwarnings < maxwarnings) {
        COS_GetString(format, 11001, code);
        va_start(args, code);
        format_string(message, sizeof(message), format, args);
        ReportLinkerWarning(message, "");
        linkerwarnings++;
    }
}

void linkerror(SInt32 code, ...) {
    va_list args;
    char message[0x800];
    char format[0x100];

    if (linkererrors < maxerrors) {
        COS_GetString(format, 11001, code);
        va_start(args, code);
        format_string(message, sizeof(message), format, args);
        ReportLinkerError(message, "");
        linkererrors++;
    } else if (linkererrors == maxerrors) {
        COS_GetString(format, 11001, 25);
        va_start(args, code);
        format_string(message, sizeof(message), format, args);
        ReportLinkerError(message, NULL);
        linkererrors++;
    }
}

void linkfailed(SInt32 code, int status, ...) {
    va_list args;
    char format[0x100];

    COS_GetString(format, 11001, code);
    va_start(args, status);
    format_string(message_buffer, sizeof(message_buffer), format, args);
    longjmp(linkerexit, status);
}

void linkalert(int mode) {
    if (mode != 1) {
        if (mode == 2) {
            MyAlert(message_buffer);
        } else {
            OSAlert(message_buffer, mode);
        }
    }
}

// (the linker dropped the function that used this literal; it stays in the unit's .data)
// TODO: invented name and body (only the literal is the original's)
static const char* LinkErrors_MemoryErrorMessage(void) {
    return "Internal Memory Error";
}
