#define CERROR_FILE "PPCError.c"
#include "compiler/common.h"
#include "compiler/PPCError.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/types.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CPrec.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/InstrSelection.h"
#include "compiler/Operands.h"
#include "compiler/PCode.h"
#include "compiler/PCodeUtilities.h"
#include "compiler/StackFrameEABI.h"
#include "driver/COSToolsCLT.h"

struct ClassTypeUpdate *classTypeUpdates;

typedef void (*PCodeGenFn)(ENode *node, SInt32 a, SInt32 b, Operand *dst);

#ifndef __wasm__ /* (the wasm build has the target's stdarg.h) */
typedef char *va_list;
#else
#include <stdarg.h>
#endif

static inline void PPCError_CheckDiagnosticCode(SInt16 diagnosticId)
{
    if (diagnosticId < 100 || diagnosticId >= BY_VERSION(178, 216)) {
        CError_FATAL(40);
    }
}

void PPCError_EmitClassTypeUpdate(SInt16 requestedReg, ENode *node, SInt16 flags, Operand *result)
{
    SInt32 valueReg, typeReg;
    ClassTypeUpdate *update;
    Operand object;
    ClassTypeHeaderBytes header;

    update = (ClassTypeUpdate *)lalloc(sizeof(*update));
    memclrw(&object, sizeof(object));
    data_00560648[node->type](node, 0, 0, &object);
    if (object.kind)
        Operands_ForceGPR(&object, node->rtype, 0);
    valueReg = gUsedVirtualRegistersGPR;
    gUsedVirtualRegistersGPR++;
    header.bytes.lowByte = data_00588476 - 2;
    header.bytes.highByte = data_00588478;
    if (copts.littleendian != 0)
        PCodeUtilities_EmitInstruction(PC_LI, valueReg, CTool_EndianConvertWord16(header.packedWord));
    else
        PCodeUtilities_EmitInstruction(PC_LIS, valueReg, 0, CTool_EndianConvertWord16(header.packedWord));
    PCodeUtilities_EmitInstruction(PC_STW, valueReg, object.reg, 0, 0);
    valueReg = gUsedVirtualRegistersGPR;
    gUsedVirtualRegistersGPR++;
    PCodeUtilities_EmitAddress(valueReg, 1, NULL, 0x15);
    update->first = gCurrentBlock->reverse_instructions;
    PCodeUtilities_EmitInstruction(PC_STW, valueReg, object.reg, 0, 4);
    typeReg = gUsedVirtualRegistersGPR;
    gUsedVirtualRegistersGPR++;
    PCodeUtilities_EmitAddress(typeReg, 1, NULL, 0xb);
    update->second = gCurrentBlock->reverse_instructions;
    PCodeUtilities_EmitInstruction(PC_STW, typeReg, object.reg, 0, 8);
    result->kind = OpndType_GPR;
    result->reg = typeReg;
    update->next = classTypeUpdates;
    classTypeUpdates = update;
}

#if VERSION >= VERSION_GC_1_3
extern SInt32 data_005dccd6;

void fn_004f5cf0(void)
{
    data_005dccd6 = 0;
}
#endif

#if VERSION >= VERSION_GC_1_3
extern int fn_004f5f00(void *a);

int fn_004f5ee0(void *a, int b)
{
    return (SInt16)(fn_004f5f00(a) + b);
}

Boolean fn_004f5e70(void *a)
{
    int v = fn_004f5f00(a);
    return v == (SInt16)v;
}

int fn_004f5ec0(void *a, int b)
{
    int v = fn_004f5f00(a) + b;
    return (SInt16)((v >> 16) + ((v >> 15) & 1));
}
#endif

#if VERSION >= VERSION_GC_1_3
int fn_004f5e90(void *a)
{
    int r = fn_004f5f00(a);
    int v = (SInt16)r;
    if (r != v)
        CError_Internal("StackFrame.c", 0xaef);
    return v;
}
#endif

#if VERSION >= VERSION_GC_1_3
void fn_004b49e0(const char *format, ...)
{
    char *base;

    if (trychain != NULL)
        return;
#ifdef __wasm__
    (void)base;
    CError_FormatAndReportDiagnostic(0x27e9, format, __va_start(format), 0, 1);
#else
    CError_FormatAndReportDiagnostic(0x27e9, format,
                                     (((char *)(&format + 1) - (base = (char *)&format)) + 3) / 4 * 4 + base, 0, 1);
#endif
}
#endif

void PPCError_UpdateClassTypeOperands(void)
{
    ClassTypeUpdate *update;
    update = classTypeUpdates;
    if (classTypeUpdates != NULL) {
        do {
            update->first->operandData.operands[2].value.signed_value = fn_004a9f70(0) + data_00588274;
            update->second->operandData.operands[2].value.signed_value = fn_004a9f90();
            update = update->next;
        } while (update != NULL);
    }
}

void PPCError_ReportError(SInt32 error, ...)
{
    char buffer[256];
    char *args;
#if VERSION >= VERSION_GC_1_3
    char *base;
#endif
    SInt32 diagnosticCode;
    SInt16 errorCode;

    if (trychain != NULL)
        longjmp(trychain->jmpbuf, 1);

#if defined(__wasm__)
    args = __va_start(error);
#elif VERSION >= VERSION_GC_1_3
    base = (char *)&error;
    args = base + (((char *)(&error + 1) - base + 3) / 4 * 4);
#else
    args = (char *)&error + (((char *)(&error + 1) - (char *)&error + 3) / 4 * 4);
#endif
    diagnosticCode = error;
    CError_ASSERT(40, (SInt16)diagnosticCode >= 100 && (SInt16)diagnosticCode < BY_VERSION(178, 216));

    errorCode = diagnosticCode;
    COS_GetString(buffer, 10001, errorCode - 99);
    CError_FormatAndReportDiagnostic(diagnosticCode + 10001, buffer, args, 0, 0);

    if (data_005884fd != 0)
        InlineAsm_LongJump();
}

void PPCError_ReportDiagnostic(SInt32 diagnosticId, ...)
{
    char buf[256];
    va_list args;
#if VERSION >= VERSION_GC_1_3
    char *base;
#endif
    SInt32 diagnosticCode;
    SInt16 errorCode;

    if (trychain != NULL) {
        return;
    }
#if defined(__wasm__)
    va_start(args, diagnosticId);
#elif VERSION >= VERSION_GC_1_3
    base = (char *)&diagnosticId;
    args = base + (((va_list)(&diagnosticId + 1) - base) + 3) / 4 * 4;
#else
    args = (va_list)&diagnosticId + (((va_list)(&diagnosticId + 1) - (va_list)&diagnosticId) + 3) / 4 * 4;
#endif
    diagnosticCode = diagnosticId;
    PPCError_CheckDiagnosticCode(diagnosticCode);
    errorCode = diagnosticCode;
    COS_GetString(buf, 10001, errorCode - 99);
    CError_FormatAndReportDiagnostic(diagnosticCode + 10001, buf, args, 0, 1);
}

void PPCError_FatalError(short diagnostic, ...)
{
    char buffer[256];
    va_list args;
#if VERSION >= VERSION_GC_1_3
    char *base;
#endif
    SInt32 diagnosticCode;
    SInt16 errorCode;

    if (trychain != NULL)
        longjmp(trychain->jmpbuf, 1);

#if defined(__wasm__)
    va_start(args, diagnostic);
#elif VERSION >= VERSION_GC_1_3
    base = (char *)&diagnostic;
    args = base + (((va_list)(&diagnostic + 1) - base + 3) / 4 * 4);
#else
    args = (va_list)&diagnostic + (((va_list)(&diagnostic + 1) - (va_list)&diagnostic + 3) / 4 * 4);
#endif

    diagnosticCode = diagnostic;
    CError_ASSERT(40, (SInt16)diagnosticCode >= 100 && (SInt16)diagnosticCode < BY_VERSION(178, 216));

    errorCode = diagnosticCode;

    COS_GetString(buffer, 10001, errorCode - 99);
    CError_FormatAndReportDiagnostic(diagnosticCode + 10001, buffer, args, 1, 0);

    if (data_005884fd != 0)
        InlineAsm_LongJump();
    longjmp(error_jmp_buf, 1);
}
