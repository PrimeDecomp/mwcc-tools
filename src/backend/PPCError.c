#include "compiler/PPCError.h"
#include "compiler/CError.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/InlineAsm.h"
#include "driver/COSToolsCLT.h"
#include <stdarg.h>

static void PPCError_GetErrorString(char* str, short code) {
    // ((int) casts: 2.6 compares the id as an int, movsx then cmp; Pro7 tests a short copy, which
    // compiles otherwise in ErrorTerm)
    CError_ASSERT(40, (int)code >= 100 && (int)code < PPCErrorStrMAX);

    COS_GetString(str, 10001, code - 99);
}

static void PPCError_VAErrorMessage(int code, va_list list, Boolean flag1, Boolean flag2) {
    char format[256];

    PPCError_GetErrorString(format, code);
    CError_ErrorMessageVA(code + 10001, format, list, flag1, flag2);
}

void PPCError_Error(int code, ...) {
    va_list list;

    if (trychain) {
        longjmp(trychain->jmpbuf, 1);
    }

    va_start(list, code);
    PPCError_VAErrorMessage(code, list, FALSE, FALSE);
    va_end(list);

    if (in_assembler) {
        AssemblerError();
    }
}

void PPCError_Warning(int code, ...) {
    va_list list;

    if (trychain) {
        return;
    }

    va_start(list, code);
    PPCError_VAErrorMessage(code, list, FALSE, TRUE);
    va_end(list);
}

void PPCError_Message(char* format, ...) {
    va_list list;

    if (trychain || copts.supress_warnings) {
        return;
    }

    va_start(list, format);
    CError_ErrorMessageVA(10221, format, list, FALSE, TRUE);
    va_end(list);
}

void PPCError_ErrorTerm(short code, ...) {
    va_list list;

    if (trychain) {
        longjmp(trychain->jmpbuf, 1);
    }

    va_start(list, code);
    PPCError_VAErrorMessage(code, list, TRUE, FALSE);
    va_end(list);

    if (in_assembler) {
        AssemblerError();
    }

    longjmp(errorreturn, 1);
}
