#define CERROR_FILE "CMachine.c"
#include "compiler/common.h"
#include "compiler/CMachine.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/types.h"
#include "compiler/CClass.h"
#include "compiler/CError.h"
#include "compiler/CExpr2.h"
#include "compiler/CInt64.h"
#include "compiler/CParser.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include <string.h>
#include <stdio.h>

#ifdef __wasm__
/* Constant folding's arithmetic (CMach_CalcFloatDiadic, CMach_FloatReciprocal). The original folds on the x87, which
   __dec2num leaves at 64-bit precision: from the first literal on, each result is rounded twice. wasm/src/x87.c's
   emulation follows that state. */
double __x87_double(int op, double a, double b);
#endif

#if VERSION >= VERSION_GC_1_3
#include "compiler/CInline.h"
#endif

#if VERSION >= VERSION_GC_1_3
void fn_004bdce0(struct CInlineBackendOptions *options)
{
    copts.processor = options->processor;
    copts.processorModel = options->processorModel;
    copts.instructionSchedulingMode = options->instructionSchedulingMode;
    copts.debug_listing = options->cOptimizerDumpEnabled;
    copts.emitExtraAssemblyData = options->emitExtraAssemblyData;
    copts.disable_registers = options->f08;
    copts.fp_contract = options->debugOptions;
    copts.f0a = options->f0a;
    copts.f0b = options->f0b;
    copts.unroll_speculative = options->ppcUnrollSpeculative;
    copts.unroll_instr_limit = options->ppcUnrollInstructionsLimit;
    copts.unroll_factor_limit = options->ppcUnrollFactorLimit;
    copts.altivec_model = options->altivecModel;
    copts.altivec_vrsave = options->altivecVrsave;
    copts.codeAlignment = options->codeAlignment;
    copts.catssupport = options->emitSerializedAssemblyFormat;
    copts.forcecatssupport = options->f14;
    copts.nonconstsmalldatathreshold = options->smallDataLimit;
    copts.constsmalldatathreshold = options->smallBSSLimit;
    copts.usedatapool = options->reuseSectionSymbols;
    copts.commonblocks = options->f20;
    copts.f54b = options->f54b;
    copts.use_lmw_stmw = options->useRegisterSaveHelpers;
    copts.f27 = options->f27;
    copts.f25 = options->f25;
    copts.interruptList = options->interruptList;
    copts.interruptOptions = options->interruptOptions;
    copts.incompatible_return_small_structs = options->f4e;
    copts.create_file_object = options->emitMainFileObject;
    copts.incompatible_sfpe_double_params = options->returnStructsInMemory;
    copts.f4f = options->f4f;
    copts.f50 = options->f50;
    copts.f51 = options->f51;
    copts.f52 = options->f52;
    copts.f53 = options->f53;
    copts.deleteDeadInstructions = options->deleteDeadInstructions;
    copts.optimizesize = options->uniformSpillBlockWeight;
    copts.fc3 = options->fc3;
    copts.commonsubs = options->irCommonSubexpressionElimination;
    copts.loopinvariants = options->fc5;
    copts.propagation = options->irCopyPropagation;
    copts.deadstore = options->irEliminateUnused;
    copts.strengthreduction = options->fc8;
    copts.strengthreductionstrict = options->fc9;
    copts.deadcode = options->irRemoveUnreachable;
    copts.lifetimes = options->fcb;
    copts.unrolling = options->irLoopUnrolling;
    copts.vectorizeloops = options->fce;
    copts.irSecondOptimizationPass = options->irSecondOptimizationPass;
    copts.ff1 = options->ff1;
    copts.ff2 = options->ff2;
    copts.unrollOption = options->unrollOption;
    copts.fd1 = options->fd1;
}

void fn_004bded0(struct CInlineBackendOptions *options)
{
    options->processor = copts.processor;
    options->processorModel = copts.processorModel;
    options->instructionSchedulingMode = copts.instructionSchedulingMode;
    options->cOptimizerDumpEnabled = copts.debug_listing;
    options->emitExtraAssemblyData = copts.emitExtraAssemblyData;
    options->f08 = copts.disable_registers;
    options->debugOptions = copts.fp_contract;
    options->f0a = copts.f0a;
    options->f0b = copts.f0b;
    options->ppcUnrollSpeculative = copts.unroll_speculative;
    options->ppcUnrollInstructionsLimit = copts.unroll_instr_limit;
    options->ppcUnrollFactorLimit = copts.unroll_factor_limit;
    options->altivecModel = copts.altivec_model;
    options->altivecVrsave = copts.altivec_vrsave;
    options->codeAlignment = copts.codeAlignment;
    options->emitSerializedAssemblyFormat = copts.catssupport;
    options->f14 = copts.forcecatssupport;
    options->smallDataLimit = copts.nonconstsmalldatathreshold;
    options->smallBSSLimit = copts.constsmalldatathreshold;
    options->reuseSectionSymbols = copts.usedatapool;
    options->f20 = copts.commonblocks;
    options->f54b = copts.f54b;
    options->useRegisterSaveHelpers = copts.use_lmw_stmw;
    options->f27 = copts.f27;
    options->f25 = copts.f25;
    options->interruptList = copts.interruptList;
    options->interruptOptions = copts.interruptOptions;
    options->f4e = copts.incompatible_return_small_structs;
    options->emitMainFileObject = copts.create_file_object;
    options->returnStructsInMemory = copts.incompatible_sfpe_double_params;
    options->f4f = copts.f4f;
    options->f50 = copts.f50;
    options->f51 = copts.f51;
    options->f52 = copts.f52;
    options->f53 = copts.f53;
    options->deleteDeadInstructions = copts.deleteDeadInstructions;
    options->uniformSpillBlockWeight = copts.optimizesize;
    options->fc3 = copts.fc3;
    options->irCommonSubexpressionElimination = copts.commonsubs;
    options->fc5 = copts.loopinvariants;
    options->irCopyPropagation = copts.propagation;
    options->irEliminateUnused = copts.deadstore;
    options->fc8 = copts.strengthreduction;
    options->fc9 = copts.strengthreductionstrict;
    options->irRemoveUnreachable = copts.deadcode;
    options->fcb = copts.lifetimes;
    options->irLoopUnrolling = copts.unrolling;
    options->fce = copts.vectorizeloops;
    options->irSecondOptimizationPass = copts.irSecondOptimizationPass;
    options->ff1 = copts.ff1;
    options->ff2 = copts.ff2;
    options->unrollOption = copts.unrollOption;
    options->fd1 = copts.fd1;
}
#endif

#if VERSION >= VERSION_GC_1_3
/* (1.3: also used by BE_elf.c and Registers.c) */
int maximumAlignment;
#else
static int maximumAlignment;
#endif
static int structLayoutOffset;
static short data_00580fa0;
static signed short bitfield_storage_size;
static int data_00580fa4;

TypeIntegral stbool = {TYPEINT, 1, IT_BOOL};
TypeIntegral stchar = {TYPEINT, 1, IT_CHAR};
TypeIntegral stsignedchar = {TYPEINT, 1, IT_SCHAR};
TypeIntegral stunsignedchar = {TYPEINT, 1, IT_UCHAR};
TypeIntegral stwchar = {TYPEINT, 2, IT_WCHAR_T};
TypeIntegral stsignedshort = {TYPEINT, 2, IT_SHORT};
TypeIntegral stunsignedshort = {TYPEINT, 2, IT_USHORT};
TypeIntegral stsignedint = {TYPEINT, 4, IT_INT};
TypeIntegral stunsignedint = {TYPEINT, 4, IT_UINT};
TypeIntegral stsignedlong = {TYPEINT, 4, IT_LONG};
TypeIntegral stunsignedlong = {TYPEINT, 4, IT_ULONG};
TypeIntegral stsignedlonglong = {TYPEINT, 8, IT_LONGLONG};
TypeIntegral stunsignedlonglong = {TYPEINT, 8, IT_ULONGLONG};
TypeIntegral stfloat = {TYPEFLOAT, 4, IT_FLOAT};
TypeIntegral stshortdouble = {TYPEFLOAT, 8, IT_SHORTDOUBLE};
TypeIntegral stdouble = {TYPEFLOAT, 8, IT_DOUBLE};
TypeIntegral stlongdouble = {TYPEFLOAT, 8, IT_LONGDOUBLE};

static StructMember stVUC_unsignedchar15 = {NULL, (Type *)&stunsignedchar, NULL, 15, 0};
static StructMember stVUC_unsignedchar14 = {&stVUC_unsignedchar15, (Type *)&stunsignedchar, NULL, 14, 0};
static StructMember stVUC_unsignedchar13 = {&stVUC_unsignedchar14, (Type *)&stunsignedchar, NULL, 13, 0};
static StructMember stVUC_unsignedchar12 = {&stVUC_unsignedchar13, (Type *)&stunsignedchar, NULL, 12, 0};
static StructMember stVUC_unsignedchar11 = {&stVUC_unsignedchar12, (Type *)&stunsignedchar, NULL, 11, 0};
static StructMember stVUC_unsignedchar10 = {&stVUC_unsignedchar11, (Type *)&stunsignedchar, NULL, 10, 0};
static StructMember stVUC_unsignedchar9 = {&stVUC_unsignedchar10, (Type *)&stunsignedchar, NULL, 9, 0};
static StructMember stVUC_unsignedchar8 = {&stVUC_unsignedchar9, (Type *)&stunsignedchar, NULL, 8, 0};
static StructMember stVUC_unsignedchar7 = {&stVUC_unsignedchar8, (Type *)&stunsignedchar, NULL, 7, 0};
static StructMember stVUC_unsignedchar6 = {&stVUC_unsignedchar7, (Type *)&stunsignedchar, NULL, 6, 0};
static StructMember stVUC_unsignedchar5 = {&stVUC_unsignedchar6, (Type *)&stunsignedchar, NULL, 5, 0};
static StructMember stVUC_unsignedchar4 = {&stVUC_unsignedchar5, (Type *)&stunsignedchar, NULL, 4, 0};
static StructMember stVUC_unsignedchar3 = {&stVUC_unsignedchar4, (Type *)&stunsignedchar, NULL, 3, 0};
static StructMember stVUC_unsignedchar2 = {&stVUC_unsignedchar3, (Type *)&stunsignedchar, NULL, 2, 0};
static StructMember stVUC_unsignedchar1 = {&stVUC_unsignedchar2, (Type *)&stunsignedchar, NULL, 1, 0};
static StructMember stVUC_unsignedchar0 = {&stVUC_unsignedchar1, (Type *)&stunsignedchar, NULL, 0, 0};
static StructMember stVSC_signedchar15 = {NULL, (Type *)&stsignedchar, NULL, 15, 0};
static StructMember stVSC_signedchar14 = {&stVSC_signedchar15, (Type *)&stsignedchar, NULL, 14, 0};
static StructMember stVSC_signedchar13 = {&stVSC_signedchar14, (Type *)&stsignedchar, NULL, 13, 0};
static StructMember stVSC_signedchar12 = {&stVSC_signedchar13, (Type *)&stsignedchar, NULL, 12, 0};
static StructMember stVSC_signedchar11 = {&stVSC_signedchar12, (Type *)&stsignedchar, NULL, 11, 0};
static StructMember stVSC_signedchar10 = {&stVSC_signedchar11, (Type *)&stsignedchar, NULL, 10, 0};
static StructMember stVSC_signedchar9 = {&stVSC_signedchar10, (Type *)&stsignedchar, NULL, 9, 0};
static StructMember stVSC_signedchar8 = {&stVSC_signedchar9, (Type *)&stsignedchar, NULL, 8, 0};
static StructMember stVSC_signedchar7 = {&stVSC_signedchar8, (Type *)&stsignedchar, NULL, 7, 0};
static StructMember stVSC_signedchar6 = {&stVSC_signedchar7, (Type *)&stsignedchar, NULL, 6, 0};
static StructMember stVSC_signedchar5 = {&stVSC_signedchar6, (Type *)&stsignedchar, NULL, 5, 0};
static StructMember stVSC_signedchar4 = {&stVSC_signedchar5, (Type *)&stsignedchar, NULL, 4, 0};
static StructMember stVSC_signedchar3 = {&stVSC_signedchar4, (Type *)&stsignedchar, NULL, 3, 0};
static StructMember stVSC_signedchar2 = {&stVSC_signedchar3, (Type *)&stsignedchar, NULL, 2, 0};
static StructMember stVSC_signedchar1 = {&stVSC_signedchar2, (Type *)&stsignedchar, NULL, 1, 0};
static StructMember stVSC_signedchar0 = {&stVSC_signedchar1, (Type *)&stsignedchar, NULL, 0, 0};
static StructMember stVUS_unsignedshort7 = {NULL, (Type *)&stunsignedshort, NULL, 14, 0};
static StructMember stVUS_unsignedshort6 = {&stVUS_unsignedshort7, (Type *)&stunsignedshort, NULL, 12, 0};
static StructMember stVUS_unsignedshort5 = {&stVUS_unsignedshort6, (Type *)&stunsignedshort, NULL, 10, 0};
static StructMember stVUS_unsignedshort4 = {&stVUS_unsignedshort5, (Type *)&stunsignedshort, NULL, 8, 0};
static StructMember stVUS_unsignedshort3 = {&stVUS_unsignedshort4, (Type *)&stunsignedshort, NULL, 6, 0};
static StructMember stVUS_unsignedshort2 = {&stVUS_unsignedshort3, (Type *)&stunsignedshort, NULL, 4, 0};
static StructMember stVUS_unsignedshort1 = {&stVUS_unsignedshort2, (Type *)&stunsignedshort, NULL, 2, 0};
static StructMember stVUS_unsignedshort0 = {&stVUS_unsignedshort1, (Type *)&stunsignedshort, NULL, 0, 0};
static StructMember stVSS_signedshort7 = {NULL, (Type *)&stsignedshort, NULL, 14, 0};
static StructMember stVSS_signedshort6 = {&stVSS_signedshort7, (Type *)&stsignedshort, NULL, 12, 0};
static StructMember stVSS_signedshort5 = {&stVSS_signedshort6, (Type *)&stsignedshort, NULL, 10, 0};
static StructMember stVSS_signedshort4 = {&stVSS_signedshort5, (Type *)&stsignedshort, NULL, 8, 0};
static StructMember stVSS_signedshort3 = {&stVSS_signedshort4, (Type *)&stsignedshort, NULL, 6, 0};
static StructMember stVSS_signedshort2 = {&stVSS_signedshort3, (Type *)&stsignedshort, NULL, 4, 0};
static StructMember stVSS_signedshort1 = {&stVSS_signedshort2, (Type *)&stsignedshort, NULL, 2, 0};
static StructMember stVSS_signedshort0 = {&stVSS_signedshort1, (Type *)&stsignedshort, NULL, 0, 0};
static StructMember stVUL_unsignedlong3 = {NULL, (Type *)&stunsignedlong, NULL, 12, 0};
static StructMember stVUL_unsignedlong2 = {&stVUL_unsignedlong3, (Type *)&stunsignedlong, NULL, 8, 0};
static StructMember stVUL_unsignedlong1 = {&stVUL_unsignedlong2, (Type *)&stunsignedlong, NULL, 4, 0};
static StructMember stVUL_unsignedlong0 = {&stVUL_unsignedlong1, (Type *)&stunsignedlong, NULL, 0, 0};
static StructMember stVSL_signedlong3 = {NULL, (Type *)&stsignedlong, NULL, 12, 0};
static StructMember stVSL_signedlong2 = {&stVSL_signedlong3, (Type *)&stsignedlong, NULL, 8, 0};
static StructMember stVSL_signedlong1 = {&stVSL_signedlong2, (Type *)&stsignedlong, NULL, 4, 0};
static StructMember stVSL_signedlong0 = {&stVSL_signedlong1, (Type *)&stsignedlong, NULL, 0, 0};
static StructMember stVF_float3 = {NULL, (Type *)&stfloat, NULL, 12, 0};
static StructMember stVF_float2 = {&stVF_float3, (Type *)&stfloat, NULL, 8, 0};
static StructMember stVF_float1 = {&stVF_float2, (Type *)&stfloat, NULL, 4, 0};
static StructMember stVF_float0 = {&stVF_float1, (Type *)&stfloat, NULL, 0, 0};

TypeStruct stvectorunsignedchar = {TYPESTRUCT, 16, NULL, &stVUC_unsignedchar0, 4, 16};
TypeStruct stvectorsignedchar = {TYPESTRUCT, 16, NULL, &stVSC_signedchar0, 5, 16};
TypeStruct stvectorboolchar = {TYPESTRUCT, 16, NULL, &stVSC_signedchar0, 6, 16};
TypeStruct stvectorunsignedshort = {TYPESTRUCT, 16, NULL, &stVUS_unsignedshort0, 7, 16};
TypeStruct stvectorsignedshort = {TYPESTRUCT, 16, NULL, &stVSS_signedshort0, 8, 16};
TypeStruct stvectorboolshort = {TYPESTRUCT, 16, NULL, &stVSS_signedshort0, 9, 16};
TypeStruct stvectorunsignedlong = {TYPESTRUCT, 16, NULL, &stVUL_unsignedlong0, 10, 16};
TypeStruct stvectorsignedlong = {TYPESTRUCT, 16, NULL, &stVSL_signedlong0, 11, 16};
TypeStruct stvectorboollong = {TYPESTRUCT, 16, NULL, &stVSL_signedlong0, 12, 16};
TypeStruct stvectorfloat = {TYPESTRUCT, 16, NULL, &stVF_float0, 13, 16};
TypeStruct stvectorpixel = {TYPESTRUCT, 16, NULL, &stVUS_unsignedshort0, 14, 16};
TypeStruct stvector = {TYPESTRUCT, 16, NULL, NULL, 10, 16};

static SInt16 loadalign_table[5] = {1, 2, 4, 8, 16};

static inline SInt16 cm_min(SInt32 index, SInt16 b)
{
    SInt32 a;
    SInt16 r;
    SInt32 n;
    SInt32 limit;
    n = index - 3;
    a = loadalign_table[n];
    r = a;
    limit = b;
    if (a >= limit)
        return b;
    return r;
}

static inline SInt32 cm_loadalign(UInt32 index)
{
    return loadalign_table[index];
}

static inline UInt16 cm_classmin(SInt16 b, SInt32 index)
{
    SInt32 a;
    int r;
    SInt32 limit;
    a = index - 3;
    a = cm_loadalign(a);
    r = a;
    limit = b;
    if (a >= limit)
        return b;
    return r;
}

static inline SInt16 cm_structalign(SInt32 index, TypeStruct *type)
{
    return cm_min(index, type->align);
}

static inline SInt16 cm_classalign(UInt32 index, SInt16 b)
{
    return cm_classmin(b, index);
}

static inline SInt32 cm_sizealign(Type *type, SInt32 alignment)
{
    SInt32 index = alignment - 3;
    SInt32 limit = cm_loadalign(index);
    if (type->size >= limit)
        return limit;
    return *(volatile SInt32 *)&type->size;
}

static inline SInt32 cm_alignment(void)
{
    return copts.structalignment;
}

static inline Float CMach_CalcFloatConvert(Type *type, Float value)
{
    switch (type->size) {
        case 4:
            value.data.value = (float)value.data.value;
            break;
        case 8:
            value.data.value = (double)value.data.value;
            break;
        case 10:
        case 12:
            break;
        default:
            CError_FATAL(BY_VERSION(714, 801));
    }
    return value;
}

int CMach_GetQUALalign(int qualifiers)
{
    int alignment = 0;

    if ((qualifiers &= Q_ALIGNED_MASK) != 0) {
        if (qualifiers == 0x02000000) {
            alignment = 1;
        } else if (qualifiers == 0x04000000) {
            alignment = 2;
        } else if (qualifiers == 0x06000000) {
            alignment = 4;
        } else if (qualifiers == 0x08000000) {
            alignment = 8;
        } else if (qualifiers == 0x0A000000) {
            alignment = 16;
        } else if (qualifiers == 0x0C000000) {
            alignment = 32;
        } else if (qualifiers == 0x10000000) {
            alignment = 64;
        } else if (qualifiers == 0x12000000) {
            alignment = 128;
        } else if (qualifiers == 0x14000000) {
            alignment = 256;
        } else if (qualifiers == 0x16000000) {
            alignment = 512;
        } else if (qualifiers == 0x18000000) {
            alignment = 1024;
        } else if (qualifiers == 0x1A000000) {
            alignment = 2048;
        } else if (qualifiers == 0x1C000000) {
            alignment = 4096;
        } else if (qualifiers == Q_ALIGNED_MASK) {
            alignment = 8192;
        } else {
            CError_Internal("CMachine.c", BY_VERSION(202, 226));
        }
    }
    return alignment;
}

#if VERSION >= VERSION_GC_1_3
void fn_004c17a0(void)
{
}
#endif

#define ISZERO64(v) ((Boolean)(((v).hi == 0) && ((v).lo == 0)))

CInt64 CMach_CalcIntDiadic(Type *type, CInt64 a, SInt16 op, CInt64 b)
{
    if (is_unsigned(type)) {
        switch (type->size) {
            case 1:
                CInt64_ConvertUInt8(&a);
                CInt64_ConvertUInt8(&b);
                break;
            case 2:
                CInt64_ConvertUInt16(&a);
                CInt64_ConvertUInt16(&b);
                break;
            case 4:
                CInt64_ConvertUInt32(&a);
                CInt64_ConvertUInt32(&b);
                break;
            case 8:
                break;
            default:
                CError_FATAL(BY_VERSION(243, 327));
        }
        switch (op) {
            case '*':
                a = CInt64_MulU(a, b);
                break;
            case '/': {
                Boolean z = (b.hi == 0) && (b.lo == 0);
                if (z) {
                    CError_Warning(ERR_DIVISION_BY_0);
                } else {
                    a = CInt64_DivU(a, b);
                }
            } break;
            case '%': {
                Boolean z = (b.hi == 0) && (b.lo == 0);
                if (z) {
                    CError_Warning(ERR_DIVISION_BY_0);
                } else {
                    a = CInt64_ModU(a, b);
                }
            } break;
            case '+':
                a = CInt64_Add(a, b);
                break;
            case '-':
                a = CInt64_Sub(a, b);
                break;
            case 0x16c:
                a = CInt64_Shl(a, b);
                break;
            case 0x16d:
                a = CInt64_ShrU(a, b);
                break;
            case '<': {
                SInt32 t = CInt64_LessU(a, b);
                a.lo = t;
                a.hi = (t < 0) ? -1 : 0;
            } break;
            case '>': {
                SInt32 t = CInt64_GreaterU(a, b);
                a.lo = t;
                a.hi = (t < 0) ? -1 : 0;
            } break;
            case 0x16a: {
                Boolean t = CInt64_LessEqualU(a, b);
                SInt32 result = t;
                a.lo = result;
                a.hi = (result < 0) ? -1 : 0;
            } break;
            case 0x16b: {
                Boolean t = CInt64_GreaterEqualU(a, b);
                SInt32 result = t;
                a.lo = result;
                a.hi = (result < 0) ? -1 : 0;
            } break;
            case 0x168: {
                SInt32 t = CInt64_Equal(a, b);
                a.lo = t;
                a.hi = (t < 0) ? -1 : 0;
            } break;
            case 0x169: {
                SInt32 t = CInt64_NotEqual(a, b);
                a.lo = t;
                a.hi = (t < 0) ? -1 : 0;
            } break;
            case '&':
                a = CInt64_And(a, b);
                break;
            case '^':
                a = CInt64_Xor(a, b);
                break;
            case '|':
                a = CInt64_Or(a, b);
                break;
            case 0x167: {
                SInt32 t = !ISZERO64(a) && !ISZERO64(b);
                a.lo = t;
                a.hi = (t < 0) ? -1 : 0;
            } break;
            case 0x166: {
                SInt32 t = !ISZERO64(a) || !ISZERO64(b);
                a.lo = t;
                a.hi = (t < 0) ? -1 : 0;
            } break;
            default:
                CError_ReportError(ERR_UNEXPECTED_TOKEN);
        }
        switch (type->size) {
            case 1:
                CInt64_ConvertUInt8(&a);
                break;
            case 2:
                CInt64_ConvertUInt16(&a);
                break;
            case 4:
                CInt64_ConvertUInt32(&a);
                break;
            case 8:
                break;
        }
    } else {
        switch (type->size) {
            case 1:
                CInt64_ConvertInt8(&a);
                CInt64_ConvertInt8(&b);
                break;
            case 2:
                CInt64_ConvertInt16(&a);
                CInt64_ConvertInt16(&b);
                break;
            case 4:
                CInt64_ConvertInt32(&a);
                CInt64_ConvertInt32(&b);
                break;
            case 8:
                break;
            default:
                CError_FATAL(BY_VERSION(305, 389));
        }
        switch (op) {
            case '*':
                a = CInt64_Mul(a, b);
                break;
            case '/': {
                Boolean z = (b.hi == 0) && (b.lo == 0);
                if (z) {
                    CError_Warning(ERR_DIVISION_BY_0);
                } else {
                    a = CInt64_Div(a, b);
                }
            } break;
            case '%': {
                Boolean z = (b.hi == 0) && (b.lo == 0);
                if (z) {
                    CError_Warning(ERR_DIVISION_BY_0);
                } else {
                    a = CInt64_Mod(a, b);
                }
            } break;
            case '+':
                a = CInt64_Add(a, b);
                break;
            case '-':
                a = CInt64_Sub(a, b);
                break;
            case 0x16c:
                a = CInt64_Shl(a, b);
                break;
            case 0x16d:
                a = CInt64_Shr(a, b);
                break;
            case '<': {
                SInt32 t = CInt64_Less(a, b);
                a.lo = t;
                a.hi = (t < 0) ? -1 : 0;
            } break;
            case '>': {
                SInt32 t = CInt64_Greater(a, b);
                a.lo = t;
                a.hi = (t < 0) ? -1 : 0;
            } break;
            case 0x16a: {
                SInt32 t = CInt64_LessEqual(a, b);
                a.lo = t;
                a.hi = (t < 0) ? -1 : 0;
            } break;
            case 0x16b: {
                Boolean t = CInt64_GreaterEqual(a, b);
                SInt32 result = t;
                a.lo = result;
                a.hi = (result < 0) ? -1 : 0;
            } break;
            case 0x168: {
                SInt32 t = CInt64_Equal(a, b);
                a.lo = t;
                a.hi = (t < 0) ? -1 : 0;
            } break;
            case 0x169: {
                SInt32 t = CInt64_NotEqual(a, b);
                a.lo = t;
                a.hi = (t < 0) ? -1 : 0;
            } break;
            case '&':
                a = CInt64_And(a, b);
                break;
            case '^':
                a = CInt64_Xor(a, b);
                break;
            case '|':
                a = CInt64_Or(a, b);
                break;
            case 0x167: {
                SInt32 t = !ISZERO64(a) && !ISZERO64(b);
                a.lo = t;
                a.hi = (t < 0) ? -1 : 0;
            } break;
            case 0x166: {
                SInt32 t = !ISZERO64(a) || !ISZERO64(b);
                a.lo = t;
                a.hi = (t < 0) ? -1 : 0;
            } break;
            default:
                CError_ReportError(ERR_UNEXPECTED_TOKEN);
        }
        switch (type->size) {
            case 1:
                CInt64_ConvertInt8(&a);
                break;
            case 2:
                CInt64_ConvertInt16(&a);
                break;
            case 4:
                CInt64_ConvertInt32(&a);
                break;
            case 8:
                break;
        }
    }
    return a;
}

CInt64 CMach_CalcIntMonadic(Type *type, SInt16 op, CInt64 val)
{
    if (is_unsigned(type)) {
        switch (type->size) {
            case 1:
                CInt64_ConvertUInt8(&val);
                break;
            case 2:
                CInt64_ConvertUInt16(&val);
                break;
            case 4:
                CInt64_ConvertUInt32(&val);
                break;
            case 8:
                break;
            default:
                CError_Internal("CMachine.c", BY_VERSION(364, 448));
        }
        switch (op) {
            case '-':
                val = CInt64_Neg(val);
                break;
            case '~':
                val = CInt64_Inv(val);
                break;
            case '!':
                val = CInt64_Not(val);
                break;
            default:
                CError_ReportError(ERR_UNEXPECTED_TOKEN);
        }
        switch (type->size) {
            case 1:
                CInt64_ConvertUInt8(&val);
                break;
            case 2:
                CInt64_ConvertUInt16(&val);
                break;
            case 4:
                CInt64_ConvertUInt32(&val);
                break;
            case 8:
                break;
        }
    } else {
        switch (type->size) {
            case 1:
                CInt64_ConvertInt8(&val);
                break;
            case 2:
                CInt64_ConvertInt16(&val);
                break;
            case 4:
                CInt64_ConvertInt32(&val);
                break;
            case 8:
                break;
            default:
                CError_Internal("CMachine.c", BY_VERSION(394, 478));
        }
        switch (op) {
            case '-':
                val = CInt64_Neg(val);
                break;
            case '~':
                val = CInt64_Inv(val);
                break;
            case '!':
                val = CInt64_Not(val);
                break;
            default:
                CError_ReportError(ERR_UNEXPECTED_TOKEN);
        }
        switch (type->size) {
            case 1:
                CInt64_ConvertInt8(&val);
                break;
            case 2:
                CInt64_ConvertInt16(&val);
                break;
            case 4:
                CInt64_ConvertInt32(&val);
                break;
            case 8:
                break;
        }
    }
    return val;
}

CInt64 CMach_CalcIntConvertFromFloat(Type *type, double value)
{
    CInt64 result;
    if ((type->type == TYPEINT || type->type == TYPEENUM) && type->size == 8) {
        if (is_unsigned(type))
            CInt64_ConvertUFromLongDouble(&result, value);
        else
            CInt64_ConvertFromLongDouble(&result, value);
    } else if (is_unsigned(type)) {
#if VERSION >= VERSION_GC_1_3
        result.lo = value;
        result.hi = 0;
#else
        result.hi = 0;
        result.lo = value;
#endif
    } else {
        SInt32 converted;
        result.lo = (SInt32)value;
        converted = value;
        result.hi = (converted < 0) ? -1 : 0;
    }
    return result;
}

void CMach_InitIntMem(Type *type, CInt64 val, void *mem)
{
    UInt8 ch;
    UInt16 sh;
    UInt32 lg;

    switch ((char)type->type) {
        case TYPEINT:
            switch (type->size) {
                case 1:
                    ch = (UInt8)val.lo;
                    memcpy(mem, &ch, 1);
                    break;
                case 2:
                    sh = val.lo;
                    sh = CTool_EndianConvertWord16(sh);
                    memcpy(mem, &sh, 2);
                    break;
                case 4:
                    lg = CTool_EndianConvertWord32(val.lo);
                    memcpy(mem, &lg, 4);
                    break;
                case 8:
                    CTool_EndianConvertWord64(val, mem);
                    break;
                default:
                    CError_FATAL(BY_VERSION(482, 566));
            }
            break;
        default:
            CError_FATAL(BY_VERSION(486, 570));
    }
}

void CMach_InitVectorMem(Type *type, MWVector128 val, void *mem)
{
    UInt8 uc[16];
    UInt16 us[8];
    UInt32 ul[4];
    float f[4];
    int i;

    switch ((char)type->type) {
        case TYPESTRUCT:
            switch (TYPE_STRUCT(type)->stype) {
                case STRUCT_VECTOR_UCHAR:
                case STRUCT_VECTOR_SCHAR:
                case STRUCT_VECTOR_BCHAR:
                    for (i = 0; i < 16; i++) {
                        if (!copts.littleendian)
                            uc[i] = val.uc[i];
                        else
                            uc[i] = val.uc[15 - i];
                    }
                    memcpy(mem, uc, 16);
                    break;
                case STRUCT_VECTOR_USHORT:
                case STRUCT_VECTOR_SSHORT:
                case STRUCT_VECTOR_BSHORT:
                case STRUCT_VECTOR_PIXEL:
                    for (i = 0; i < 8; i++) {
                        if (!copts.littleendian)
                            us[i] = CTool_EndianConvertWord16(val.us[i]);
                        else
                            us[i] = CTool_EndianConvertWord16(val.us[7 - i]);
                    }
                    memcpy(mem, us, 16);
                    break;
                case STRUCT_VECTOR_UINT:
                case STRUCT_VECTOR_SINT:
                case STRUCT_VECTOR_BINT:
                    for (i = 0; i < 4; i++) {
                        if (!copts.littleendian)
                            ul[i] = CTool_EndianConvertWord32(val.ul[i]);
                        else
                            ul[i] = CTool_EndianConvertWord32(val.ul[3 - i]);
                    }
                    memcpy(mem, ul, 16);
                    break;
                case STRUCT_VECTOR_FLOAT:
                    for (i = 0; i < 4; i++) {
                        if (!copts.littleendian)
                            f[i] = val.f[i];
                        else
                            f[i] = val.f[3 - i];
                        CTool_EndianConvertMem(&f[i], 4);
                    }
                    memcpy(mem, f, 16);
                    break;
                default:
                    CError_FATAL(568);
            }
            break;
        default:
            CError_FATAL(572);
    }
}

Float CMach_CalcFloatDiadic(Type *type, Float left, short op, Float right)
{
    switch (op) {
        case '+':
#ifdef __wasm__
            left.data.value = __x87_double('+', left.data.value, right.data.value);
#else
            left.data.value += right.data.value;
#endif
            break;
        case '-':
#ifdef __wasm__
            left.data.value = __x87_double('-', left.data.value, right.data.value);
#else
            left.data.value -= right.data.value;
#endif
            break;
        case '*':
#ifdef __wasm__
            left.data.value = __x87_double('*', left.data.value, right.data.value);
#else
            left.data.value *= right.data.value;
#endif
            break;
        case '/':
#ifdef __wasm__
            left.data.value = __x87_double('/', left.data.value, right.data.value);
#else
            left.data.value /= right.data.value;
#endif
            break;
        default:
            CError_FATAL(BY_VERSION(592, 679));
    }
    return CMach_CalcFloatConvert(type, left);
}

Float CMach_CalcFloatMonadic(Type *type, short op, double value)
{
    Float result;
    if (op != 0x2d)
        CError_FATAL(BY_VERSION(605, 692));
    value = -value;
    result.data.value = value;
    switch (type->size) {
        case 4:
            result.data.value = (float)result.data.value;
            break;
        case 8:
            result.data.value = result.data.value;
            break;
        case 10:
        case 12:
            break;
        default:
            CError_FATAL(BY_VERSION(714, 801));
            break;
    }
    return result;
}

Boolean CMach_CalcFloatDiadicBool(Type *self, volatile double a, SInt16 op, volatile double b)
{
    int dead_1;
    dead_1 = 0;
    switch (op) {
        case 0x168:
            return a == b;
        case 0x169:
            return a != b;
        case 0x16a:
            return a <= b;
        case 0x16b:
            return a >= b;
        case 0x3e:
            return a > b;
        case 0x3c:
            return a < b;
        default:
            CError_Internal("CMachine.c", BY_VERSION(0x273, 0x2ca));
            return 0;
    }
}

unsigned char CMach_CalcVectorDiadicBool(unsigned int context, const union MWVector128 *left, unsigned int operation,
                                         const union MWVector128 *right)
{
    switch ((short)operation) {
        case 360:
            return left->ul[0] == right->ul[0] && left->ul[1] == right->ul[1] && left->ul[2] == right->ul[2] &&
                   left->ul[3] == right->ul[3];
        case 361:
            return left->ul[0] != right->ul[0] && left->ul[1] != right->ul[1] && left->ul[2] != right->ul[2] &&
                   left->ul[3] != right->ul[3];
        default:
            CError_Internal("CMachine.c", 653);
            return 0;
    }
}

void *CMach_FloatScan(char *text, Float *result, char *flag)
{
    double value;
    char *end;

    if (!(end = CTemplateClass_ParseDouble(text, &value, flag)))
        CError_FatalError(ERR_NUMBER_OUT_RANGE);
    if (*flag)
        result->data.value = 0.0;
    else
        result->data.value = value;
    return end;
}

Float CMach_CalcFloatConvertFromInt(Type *type, CInt64 value)
{
    Float f;
    if ((type->type == TYPEINT || type->type == TYPEENUM) && type->size == 8) {
        if (is_unsigned(type))
            f.data.value = CInt64_ConvertUToLongDouble(&value);
        else
            f.data.value = CInt64_ConvertToLongDouble(&value);
    } else {
        if (is_unsigned(type))
            f.data.value = value.lo;
        else
            f.data.value = (SInt32)value.lo;
    }
    return f;
}

Float CMachine_RoundFloatToType(Type *type, Float value)
{
    switch (type->size) {
        case 4:
            value.data.value = (float)value.data.value;
            break;
        case 8:
            value.data.value = value.data.value;
            break;
        case 10:
            break;
        case 12:
            break;
        default:
            CError_Internal("CMachine.c", BY_VERSION(714, 801));
    }
    return value;
}

unsigned char CMach_FloatIsZero(double value)
{
    return BY_VERSION(value == 0.0L, 0.0L == value);
}

unsigned char CMach_FloatIsOne(double value)
{
    return BY_VERSION(value == 1.0L, 1.0L == value);
}

UInt8 CMach_FloatIsNegOne(double value)
{
    return BY_VERSION(value == -1.0L, -1.0L == value);
}

void CMach_InitFloatMem(Type *type, Float value, unsigned char *dest)
{
    if (type->type == TYPEFLOAT) {
        switch (type->size) {
            case 4: {
                float f = value.data.value;
                memcpy(dest, &f, 4);
                CTool_EndianConvertMem(dest, 4);
                return;
            }
            case 8: {
                double d = value.data.value;
                memcpy(dest, &d, 8);
                CTool_EndianConvertMem(dest, 8);
                return;
            }
        }
    }
    CError_FATAL(BY_VERSION(779, 866));
}

void CMach_PrintFloat(char *output, Float value)
{
    FloatFormatBuffer buffer;
    Float doubleValue;
    float singleValue;

    do {
        if (stshortdouble.type == 2) {
            switch (stshortdouble.size) {
                case 4:
                    singleValue = (float)value.data.value;
                    memcpy(&buffer.value, &singleValue, sizeof(singleValue));
                    CTool_EndianConvertMem(&buffer.value, sizeof(singleValue));
                    continue;
                case 8:
                    doubleValue = value;
                    memcpy(&buffer.value, &doubleValue, sizeof(doubleValue));
                    CTool_EndianConvertMem(&buffer.value, sizeof(doubleValue));
                    continue;
                default:
                    break;
            }
        }
        CError_Internal("CMachine.c", 779);
    } while (0);
    CTool_EndianConvertMem(&buffer.value, sizeof(buffer.value));
    sprintf(output, "%g", (long double)buffer.value);
}

void CMach_PragmaParams(void)
{
    if (copts.warn_illpragma != 0) {
        CError_Warning(ERR_ILLEGAL_PRAGMA, 0);
    }
    while (CPrep_ScanMacroExpandedChar() != 0) {
        CPrepTokenizer_GetNextToken();
    }
}

#if VERSION >= VERSION_GC_1_3
int fn_004c0080(void *a, int b, int c);

/* The alignment of a struct: the largest of its members', as the pack mode (copts.structalignment) has it. */
int fn_004bfe90(TypeStruct *tstruct, UInt8 flag)
{
    StructMember *member;
    SInt8 mode = *((SInt8 *)&copts + 0xcc);
    int stype = tstruct->stype;
    int align, value;
    Boolean first;

    if (stype >= 4 && stype <= 14)
        return 16;
    switch (mode) {
    case 0:
        return 2;
    case 1:
        return tstruct->size > 2 ? 4 : 2;
    case 3:
    case 8:
        return 1;
    }
    if (tstruct->size <= 1)
        return 1;
    align = 1;
    switch (mode) {
    case 2:
        if (*((UInt8 *)&copts + 0x86)) {
            for (member = tstruct->members; member; member = member->next) {
                value = CMachine_GetTypeAlignmentImpl(member->type, flag);
                if (value > align)
                    align = value;
                if (flag && (value = CMach_GetQUALalign(member->qual)) > align)
                    align = value;
            }
        } else if (stype == 1) {
            for (member = tstruct->members; member; member = member->next) {
                value = fn_004c0080(member->type, 1, flag);
                if (value > align)
                    align = value;
                if (flag && (value = CMach_GetQUALalign(member->qual)) > align)
                    align = value;
            }
        } else {
            first = 1;
            for (member = tstruct->members; member; member = member->next) {
                value = fn_004c0080(member->type, first || align >= 8, flag);
                if (value > align)
                    align = value;
                if (flag && (value = CMach_GetQUALalign(member->qual)) > align)
                    align = value;
                first = 0;
            }
        }
        return align;
    default:
        CError_FATAL(1028);
    case 4:
    case 5:
    case 6:
    case 7:
        break;
    }
    for (member = tstruct->members; member; member = member->next) {
        value = CMachine_GetTypeAlignmentImpl(member->type, flag);
        if (value > align)
            align = value;
        if (flag && (value = CMach_GetQUALalign(member->qual)) > align)
            align = value;
    }
    return align;
}

UInt16 CMach_GetStructAlign(TypeStruct *str)
{
    return fn_004bfe90(str, 1);
}
#else
UInt16 CMach_GetStructAlign(TypeStruct *str)
{
    unsigned int lift_value_0;
    lift_value_0 = maximumAlignment;
    maximumAlignment = 0;
    return lift_value_0;
}
#endif
#if VERSION >= VERSION_GC_1_3
SInt16 fn_004bf830(Type *type, int minAlign, UInt8 flag);
#endif

short CMach_GetClassAlign(TypeClass *list)
{
    int maximum;
    ClassList *node;

    maximum = maximumAlignment;
    maximumAlignment = 0;

    node = list->bases;
    while (node != NULL) {
        int value = node->base->align;
        if (value > maximum)
            maximum = value;
        node = node->next;
    }
    return maximum;
}

#if VERSION >= VERSION_GC_1_3
SInt16 CMachine_GetTypeAlignmentImpl(Type *type, SInt32 flag)
#else
SInt16 CMachine_GetTypeAlignment(Type *type)
#endif
{
    UInt8 arrayAlignment;
    Boolean isPowerAlignment;

    if (type->type == TYPESTRUCT) {
        SInt32 structType = ((TypeStruct *)type)->stype;
        if (structType >= 4 && structType <= 14)
            return 16;
    }

    switch (cm_alignment()) {
        case 3:
        case 8:
            return 1;
        case 4:
        case 5:
        case 6:
        case 7:
            isPowerAlignment = 1;
            break;
        default:
            isPowerAlignment = 0;
            break;
    }

    arrayAlignment = copts.alignarraymembers;

    for (;;) {
        switch ((SInt8)type->type) {
            case TYPEVOID:
                return 0;
            case TYPEFUNC:
                return 0;
            case TYPEENUM:
                type = ((TypeEnum *)type)->enumtype;
                /* fall through */
            case TYPEINT:
                if (isPowerAlignment) {
                    return cm_sizealign(type, cm_alignment());
                }
                if (type->size == 1)
                    return 1;
                if (cm_alignment() != 0 && type->size >= 8)
                    return 8;
                if (cm_alignment() != 0 && type->size >= 4)
                    return 4;
                return 2;
            case TYPEFLOAT:
                if (isPowerAlignment) {
                    return cm_sizealign(type, cm_alignment());
                }
                switch (cm_alignment()) {
                    case 0:
                        return 2;
                    case 1:
                        return 4;
                    case 2:
                        if (type->size > 4)
                            return 8;
                        return 4;
                    default:
                        CError_FATAL(BY_VERSION(1173, 1348));
                }
                /* fall through */
            case TYPEMEMBERPOINTER:
            case TYPEPOINTER:
                if (isPowerAlignment) {
                    return cm_sizealign(type, cm_alignment());
                }
                if (!copts.structalignment)
                    return 2;
                return 4;
            case TYPEARRAY:
                if (arrayAlignment != 0) {
                    if (isPowerAlignment) {
                        return cm_sizealign(type, cm_alignment());
                    }
                    if (type->size == 1)
                        return 1;
                    if (cm_alignment() == 0 || type->size <= 2)
                        return 2;
                    if (cm_alignment() == 1 || type->size < 8)
                        return 4;
                    {
                        SInt32 elementAlignment = CMachine_GetTypeAlignment(((TypePointer *)type)->target);
                        if (elementAlignment > 4)
                            return elementAlignment;
                        return 4;
                    }
                }
                type = ((TypePointer *)type)->target;
                continue;
            case TYPESTRUCT:
                if (isPowerAlignment)
                    return cm_structalign(cm_alignment(), (TypeStruct *)type);
                return ((TypeStruct *)type)->align;
            case TYPECLASS:
                if (isPowerAlignment)
                    return cm_classalign(cm_alignment(), ((TypeClass *)type)->align);
                return ((TypeClass *)type)->align;
            case TYPEBITFIELD:
                type = ((TypeBitfield *)type)->bitfieldtype;
                continue;
            case TYPETEMPLATE:
                return 1;
            case TYPELABEL:
            default:
                CError_FATAL(1218);
                return 0;
        }
    }
}

#if VERSION >= VERSION_GC_1_3
extern int fn_004bfc30(void *a, int b);

SInt16 CMachine_GetTypeAlignment(Type *type)
{
    return CMachine_GetTypeAlignmentImpl(type, 1);
}

int fn_004bfc20(void *a)
{
    return fn_004bfc30(a, 1);
}
#endif

#if VERSION >= VERSION_GC_1_3
int fn_004c0080(void *a, int b, int c)
{
    return (SInt16)CMachine_GetTypeAlignmentImpl(a, c);
}

#pragma options align = mac68k
struct Fn004bec90 {
    char pad[0xe];
    struct Fn004bec90b *inner;
};
struct Fn004bec90b {
    char pad[2];
    int size;
};
#pragma options align = reset

int fn_004bec90(struct Fn004bec90 *a)
{
    struct Fn004bec90b *inner = a->inner;
    int size = inner->size;
    int align = (SInt16)CMachine_GetTypeAlignmentImpl((Type *)inner, 1) - 1;
    return (size + align) & ~align;
}
#endif

#if VERSION >= VERSION_GC_1_3
#if VERSION >= VERSION_GC_1_3
#define data_005e7169 (*(UInt8 *)((char *)&copts + 0x87))
#else
extern UInt8 data_005e7169;
#endif

Boolean fn_004bf100(Type *type)
{
    switch ((SInt8)type->type) {
    case TYPEINT:
        switch (TYPE_INTEGRAL(type)->integral) {
        case IT_BOOL:
        case IT_UCHAR:
        case IT_WCHAR_T:
        case IT_USHORT:
        case IT_UINT:
        case IT_ULONG:
        case IT_ULONGLONG:
            return 1;
        case IT_CHAR:
            return data_005e7169;
        default:
            return 0;
        }
    case TYPEPOINTER:
        return 1;
    default:
        return 0;
    }
}
#endif

#if VERSION >= VERSION_GC_1_3
extern TypeIntegral data_005adcf0;
extern TypeIntegral data_005adcf8;
extern TypeIntegral data_005add00;

Type *fn_004bf0a0(short token, short a, short b)
{
    Type *type;

    switch (token) {
    case 0x113:
        type = (Type *)&data_005adcf0;
        break;
    case 0x114:
        type = (Type *)&data_005adcf8;
        break;
    case 0x115:
        type = (Type *)&data_005add00;
        break;
    default:
        type = NULL;
        break;
    }
    if (type == NULL || a != 0 || b != 0) {
        CError_ReportError(ERR_ILLEGAL_TYPE);
        return (Type *)&stsignedint;
    }
    return type;
}

#if VERSION >= VERSION_GC_1_3
#define data_005e71ae (*(UInt8 *)((char *)&copts + 0xcc))
#else
extern UInt8 data_005e71ae;
#endif
#if VERSION >= VERSION_GC_1_3
#define data_005e7168 (*(UInt8 *)((char *)&copts + 0x86))
#else
extern UInt8 data_005e7168;
#endif

SInt16 fn_004c1620(Type *type)
{
    UInt8 savedA = data_005e71ae;
    UInt8 savedB = data_005e7168;
    SInt32 alignment;

    data_005e71ae = 2;
    data_005e7168 = 0;
    alignment = CMachine_GetTypeAlignmentImpl(type, 0);
    data_005e71ae = savedA;
    data_005e7168 = savedB;
    if (type->type == TYPESTRUCT && *(SInt32 *)((char *)type + 0xa) == 0) {
        if (*(SInt16 *)((char *)type + 0x10) > alignment)
            return *(SInt16 *)((char *)type + 0x10);
    }
    return alignment;
}
#endif

#if VERSION >= VERSION_GC_1_3
extern UInt8 data_005dc468;


/* alignment of a struct member, honouring the pack mode, and the running maximum alignment of the struct */
SInt16 fn_004bf830(Type *type, int minAlign, UInt8 flag)
{
    int align;

    align = CMachine_GetTypeAlignmentImpl(type, flag);
    if (align < 1)
        align = 1;
    if (*(UInt8 *)type == 4 && *((SInt8 *)type + 0xe) >= 4 && *((SInt8 *)type + 0xe) <= 14 && align < 16)
        align = 16;
    switch (data_005e71ae) {
    case 0:
        if (align > 2)
            align = 2;
        break;
    case 1:
        if (align > 4)
            align = 4;
        break;
    case 2:
        if (!data_005e7168) {
            align = 1;
            if (structLayoutOffset != 0 && data_005dc468 < 8)
                align = 0;
            align = fn_004c0080(type, align, flag);
        }
        if (minAlign > align)
            align = minAlign;
        break;
    case 8:
        align = 1;
        break;
    }
    if (align > data_005dc468)
        data_005dc468 = align;
    return align;
}
#endif

SInt16 CMach_GetTypeAlign(Type *type)
{
    SInt32 alignment;
    alignment = CMachine_GetTypeAlignment(type);
    if (alignment <= 1) {
        return 1;
    }
    if (type->type == TYPESTRUCT) {
        TypeStruct *structType = (TypeStruct *)type;
        SInt32 structKind = structType->stype;
        if (structKind >= 4 && structKind <= 14) {
            return 16;
        }
    }
    switch (copts.structalignment) {
        case 8:
            alignment = 1;
            break;
        case 0:
            if (alignment > 2) {
                alignment = 2;
            }
            break;
        case 1:
            if (alignment > 4) {
                alignment = 4;
            }
            break;
    }
    return alignment;
}

SInt16 CMach_MemberAlignValue(Type *type, SInt32 offset)
{
#if VERSION >= VERSION_GC_1_3
    /* (1.3 aligns as a struct member: the pack mode applies) */
    SInt16 alignment = fn_004bf830(type, 0, 1);
#else
    SInt16 alignment = CMach_GetTypeAlign(type);
#endif
    if ((SInt32)alignment <= 1)
        return 0;
    return (alignment - (offset & (alignment - 1U))) & (alignment - 1U);
}

/* (1.3: RegisterInfo.c's, in Registers.c) */
#if VERSION < VERSION_GC_1_3
void CMachine_ResetMaximumAlignment(void)
{
    maximumAlignment = 0;
    return;
}
#endif

void CMach_StructLayoutInitOffset(unsigned int offset)
{
    structLayoutOffset = offset;
#if VERSION >= VERSION_GC_1_3
    data_005dc468 = 4;
#endif
    data_00580fa0 = 0;
    bitfield_storage_size = 0;
    data_00580fa4 = 0;
}

int CMach_StructLayoutGetCurSize(void)
{
    int alignment = 0;
    int padding;

    if (data_00580fa0 != 0) {
        switch (copts.structalignment) {
            case 3:
            case 8:
                alignment = 8;
                break;
            case 0:
            case 4:
                alignment = 16;
                break;
        }
        if (alignment != 0) {
            padding = bitfield_storage_size * 8 - data_00580fa0;
            if (padding > 0) {
                structLayoutOffset -= padding / alignment;
            }
        }
        data_00580fa0 = 0;
    }
    return structLayoutOffset;
}

long CMach_StructLayoutGetOffset(Type *type, int flags)
{
    int unusedBits;
    unsigned int typeCode;
    int requiredAlignment;
    int currentOffset;
    int alignment;
    int mask;
    int offset;
    if (data_00580fa0 != 0) {
        unusedBits = (bitfield_storage_size << 3) - data_00580fa0;
        if (unusedBits >= 0) {
            structLayoutOffset -= unusedBits / 8;
        }
    }
    typeCode = CParser_GetTypeQualifiers(type, flags);
    data_00580fa0 = 0;
    requiredAlignment = CMach_GetQUALalign(typeCode);
    currentOffset = structLayoutOffset;
    alignment = CMach_GetTypeAlign(type);
    if (requiredAlignment > alignment) {
        alignment = requiredAlignment;
    }
    maximumAlignment = maximumAlignment < alignment ? alignment : maximumAlignment;
    mask = alignment - 1;
    offset = (short)(mask & (alignment - (currentOffset & mask))) + structLayoutOffset;
    structLayoutOffset = offset + type->size;
    return offset;
}

#if VERSION >= VERSION_GC_1_3
extern UInt8 data_005dc468;
#endif

long CMach_StructLayoutBitfield(TypeBitfield *field, int alignmentKind)
{
    short requestedAlignment;
    short naturalAlignment;
    short storageSize;
    short padding;
    long size;
    short alignment;
    short storageBits;

    padding = 0;
    naturalAlignment = 0;
    requestedAlignment = CMach_GetQUALalign(alignmentKind);
    if (requestedAlignment <= field->bitfieldtype->size)
        requestedAlignment = 0;
    size = field->bitfieldtype->size;
    switch (size) {
        case 1:
            storageSize = 1;
            storageBits = 8;
            naturalAlignment = 0;
            break;
        case 2:
            storageSize = 2;
            storageBits = 16;
            naturalAlignment = storageSize;
            break;
        case 4:
            size = copts.structalignment;
            if (size != 0 && size != 4)
                naturalAlignment = 4;
            else
                naturalAlignment = 2;
            storageSize = 4;
            storageBits = 32;
            break;
        default:
            CError_Internal("CMachine.c", 1437);
    }
    switch (copts.structalignment) {
        case 3:
        case 8:
            naturalAlignment = 0;
    }
    alignment = naturalAlignment;
    if (requestedAlignment > naturalAlignment)
        alignment = requestedAlignment;
    if (alignment != 0 && ((alignment - 1) & structLayoutOffset) != 0)
        padding = alignment - ((alignment - 1) & structLayoutOffset);
    if (data_00580fa0 == 0 && structLayoutOffset != 0 && requestedAlignment == 0 && padding != 0) {
        bitfield_storage_size = storageSize - padding;
        if ((short)(storageBits - (bitfield_storage_size << 3) - field->bitlength) >= 0)
            data_00580fa0 = bitfield_storage_size << 3;
    }
    if (data_00580fa0 == 0) {
        structLayoutOffset += padding;
        if (field->bitlength == 0)
            return structLayoutOffset;
        if (field->suppressAlignment == 0)
            maximumAlignment = maximumAlignment >= alignment ? maximumAlignment : alignment;
        data_00580fa0 = field->bitlength;
        bitfield_storage_size = storageSize;
        data_00580fa4 = structLayoutOffset;
        structLayoutOffset += storageSize;
        field->offset = 0;
        return data_00580fa4;
    }
    if (field->bitlength == 0 || data_00580fa0 + (signed char)field->bitlength > storageBits ||
        bitfield_storage_size > storageSize) {
        structLayoutOffset += padding;
        data_00580fa0 = 0;
        bitfield_storage_size = storageSize;
        if (field->bitlength == 0)
            return structLayoutOffset;
        if (field->suppressAlignment == 0)
            maximumAlignment = maximumAlignment >= alignment ? maximumAlignment : alignment;
        data_00580fa4 = structLayoutOffset;
        structLayoutOffset += storageSize;
    } else if (bitfield_storage_size < storageSize) {
        structLayoutOffset -= bitfield_storage_size;
        bitfield_storage_size = storageSize;
        if (field->bitlength == 0)
            return structLayoutOffset;
        if (field->suppressAlignment == 0)
            maximumAlignment = maximumAlignment >= naturalAlignment ? maximumAlignment : naturalAlignment;
        data_00580fa4 = structLayoutOffset;
        structLayoutOffset += storageSize;
    }
    field->offset = data_00580fa0;
    data_00580fa0 += field->bitlength;
    return data_00580fa4;
}

#if VERSION >= VERSION_GC_1_3
#pragma sym off
#endif
Boolean CMach_GetFunctionResultClass(TypeFunc *functype)
{
    SInt8 type = functype->functype->type;
    switch (type) {
        case TYPESTRUCT: {
            if (functype->functype->type == TYPESTRUCT) {
                SInt32 structKind = TYPE_STRUCT(functype->functype)->stype;
                if (structKind >= 4 && structKind <= 14)
                    return 0;
            }
        }
        case TYPECLASS:
        case TYPEMEMBERPOINTER:
            if (Type_RequiresMemoryReturn(functype->functype))
                return 1;
            return 0;
        default:
            return 0;
    }
}
#if VERSION >= VERSION_GC_1_3
#pragma sym reset
#endif

Boolean Type_RequiresMemoryReturn(Type *type)
{
    SInt32 structKind;
    switch ((char)type->type) {
        case TYPESTRUCT: {
            structKind = TYPE_STRUCT(type)->stype;
            if (structKind >= 4 && structKind <= 14)
                return 0;
            if (type->size <= 8 && !copts.incompatible_sfpe_double_params)
                return 0;
            return 1;
        }
        case TYPECLASS:
            if (type->size <= 8 && (Boolean)(CClass_Constructor(TYPE_CLASS(type)) == NULL) &&
                !copts.incompatible_sfpe_double_params)
                return 0;
            return 1;
        case TYPEMEMBERPOINTER:
            if (type->size == 4)
                return 0;
            return 1;
        default:
            return 0;
    }
}

Boolean CMach_PassResultInHiddenArg(Type *type)
{
    switch ((char)type->type) {
        case 4:
            if ((char)type->type == 4 && TYPE_STRUCT(type)->stype >= TYPESTRUCT &&
                TYPE_STRUCT(type)->stype <= TYPETEMPLDEPEXPR)
                return 0;
            /* fall through */
        case 5:
            return 1;
        case 10:
            if (type->size == 4)
                return 0;
            return 1;
        default:
            return 0;
    }
}

const char *CMach_GetCPU(void)
{
    switch (copts.processor) {
        case 0:
            return "__PPC401__";
        case 1:
            return "__PPC403__";
        case 2:
            return "__PPC505__";
        case 3:
            return "__PPC509__";
        case 4:
            return "__PPC555__";
#if VERSION >= VERSION_GC_1_3
        case 25:
            return "__PPC556__";
        case 26:
            return "__PPC565__";
#endif
        case 5:
            return "__PPC601__";
        case 6:
            return "__PPC602__";
        case 7:
            return "__PPC603__";
        case 8:
            return "__PPC603e__";
        case 9:
            return "__PPC604__";
        case 10:
            return "__PPC604e__";
        case 11:
            return "__PPC740__";
        case 12:
            return "__PPC750__";
        case 13:
            return "__PPC801__";
        case 14:
            return "__PPC821__";
        case 15:
            return "__PPC823__";
        case 16:
            return "__PPC850__";
        case 17:
            return "__PPC860__";
        case 21:
            return "__PPC7400__";
#if VERSION >= VERSION_GC_1_3
        case 24:
            return "__PPC7450__";
#endif
        case 18:
            return "__PPC8240__";
        case 19:
            return "__PPC8260__";
        case 22:
            return "__PPCGEKKO__";
#if VERSION >= VERSION_GC_1_3
        case 23:
            return "__PPCe500__";
#endif
        default:
            return NULL;
    }
}

Boolean CMach_FloatIsPowerOf2(Float f)
{
    return f.data.value == 2.0L || f.data.value == 4.0L || f.data.value == 8.0L || f.data.value == 16.0L ||
           f.data.value == 32.0L || f.data.value == 64.0L || f.data.value == 128.0L || f.data.value == 256.0L ||
           f.data.value == 512.0L || f.data.value == 1024.0L;
}

Float CMach_FloatReciprocal(Float value)
{
#ifdef __wasm__
    value.data.value = __x87_double('/', 1.0, value.data.value);
#else
    value.data.value = 1.0L / value.data.value;
#endif
    return value;
}

void CMach_ReInitRuntimeObjects(void)
{
    HashNameNode *nameHash1;
    HashNameNode *nameHash2;
    HashNameNode *nameHash3;
    HashNameNode *nameHash4;
    HashNameNode *nameHash5;
    HashNameNode *nameHash6;
    HashNameNode *nameHash7;
    HashNameNode *nameHash8;
    HashNameNode *nameHash9;
    HashNameNode *nameHash10;
    HashNameNode *nameHash11;
    HashNameNode *nameHash12;
    HashNameNode *nameHash13;
    HashNameNode *nameHash14;
    HashNameNode *nameHash15;
    HashNameNode *nameHash16;
    HashNameNode *nameHash17;
    HashNameNode *nameHash18;
    HashNameNode *nameHash19;
    HashNameNode *nameHash20;
    HashNameNode *nameHash21;
    HashNameNode *nameHash22;
    HashNameNode *nameHash23;
    HashNameNode *nameHash24;
    HashNameNode *nameHash25;
    HashNameNode *nameHash26;
    HashNameNode *nameHash27;
    nameHash1 = GetHashNameNode("[0]");
    nameHash2 = GetHashNameNode("[1]");
    nameHash3 = GetHashNameNode("[2]");
    nameHash4 = GetHashNameNode("[3]");
    nameHash5 = GetHashNameNode("[4]");
    nameHash6 = GetHashNameNode("[5]");
    nameHash7 = GetHashNameNode("[6]");
    nameHash8 = GetHashNameNode("[7]");
    nameHash9 = GetHashNameNode("[8]");
    nameHash10 = GetHashNameNode("[9]");
    nameHash11 = GetHashNameNode("[10]");
    nameHash12 = GetHashNameNode("[11]");
    nameHash13 = GetHashNameNode("[12]");
    nameHash14 = GetHashNameNode("[13]");
    nameHash15 = GetHashNameNode("[14]");
    nameHash16 = GetHashNameNode("[15]");
    nameHash17 = GetHashNameNode("vector unsigned char");
    nameHash18 = GetHashNameNode("vector unsigned short");
    nameHash19 = GetHashNameNode("vector unsigned int");
    nameHash20 = GetHashNameNode("vector signed char");
    nameHash21 = GetHashNameNode("vector signed short");
    nameHash22 = GetHashNameNode("vector signed int");
    nameHash23 = GetHashNameNode("vector bool char");
    nameHash24 = GetHashNameNode("vector bool short");
    nameHash25 = GetHashNameNode("vector bool int");
    nameHash26 = GetHashNameNode("vector float");
    nameHash27 = GetHashNameNode("vector pixel");
    stvectorunsignedchar.name = nameHash17;
    stvectorunsignedshort.name = nameHash18;
    stvectorunsignedlong.name = nameHash19;
    stvectorsignedchar.name = nameHash20;
    stvectorsignedshort.name = nameHash21;
    stvectorsignedlong.name = nameHash22;
    stvectorboolchar.name = nameHash23;
    stvectorboolshort.name = nameHash24;
    stvectorboollong.name = nameHash25;
    stvectorfloat.name = nameHash26;
    stvectorpixel.name = nameHash27;
    stVUC_unsignedchar0.name = nameHash1;
    stVUC_unsignedchar1.name = nameHash2;
    stVUC_unsignedchar2.name = nameHash3;
    stVUC_unsignedchar3.name = nameHash4;
    stVUC_unsignedchar4.name = nameHash5;
    stVUC_unsignedchar5.name = nameHash6;
    stVUC_unsignedchar6.name = nameHash7;
    stVUC_unsignedchar7.name = nameHash8;
    stVUC_unsignedchar8.name = nameHash9;
    stVUC_unsignedchar9.name = nameHash10;
    stVUC_unsignedchar10.name = nameHash11;
    stVUC_unsignedchar11.name = nameHash12;
    stVUC_unsignedchar12.name = nameHash13;
    stVUC_unsignedchar13.name = nameHash14;
    stVUC_unsignedchar14.name = nameHash15;
    stVUC_unsignedchar15.name = nameHash16;
    stVSC_signedchar0.name = nameHash1;
    stVSC_signedchar1.name = nameHash2;
    stVSC_signedchar2.name = nameHash3;
    stVSC_signedchar3.name = nameHash4;
    stVSC_signedchar4.name = nameHash5;
    stVSC_signedchar5.name = nameHash6;
    stVSC_signedchar6.name = nameHash7;
    stVSC_signedchar7.name = nameHash8;
    stVSC_signedchar8.name = nameHash9;
    stVSC_signedchar9.name = nameHash10;
    stVSC_signedchar10.name = nameHash11;
    stVSC_signedchar11.name = nameHash12;
    stVSC_signedchar12.name = nameHash13;
    stVSC_signedchar13.name = nameHash14;
    stVSC_signedchar14.name = nameHash15;
    stVSC_signedchar15.name = nameHash16;
    stVUS_unsignedshort0.name = nameHash1;
    stVUS_unsignedshort1.name = nameHash2;
    stVUS_unsignedshort2.name = nameHash3;
    stVUS_unsignedshort3.name = nameHash4;
    stVUS_unsignedshort4.name = nameHash5;
    stVUS_unsignedshort5.name = nameHash6;
    stVUS_unsignedshort6.name = nameHash7;
    stVUS_unsignedshort7.name = nameHash8;
    stVSS_signedshort0.name = nameHash1;
    stVSS_signedshort1.name = nameHash2;
    stVSS_signedshort2.name = nameHash3;
    stVSS_signedshort3.name = nameHash4;
    stVSS_signedshort4.name = nameHash5;
    stVSS_signedshort5.name = nameHash6;
    stVSS_signedshort6.name = nameHash7;
    stVSS_signedshort7.name = nameHash8;
    stVUL_unsignedlong0.name = nameHash1;
    stVUL_unsignedlong1.name = nameHash2;
    stVUL_unsignedlong2.name = nameHash3;
    stVUL_unsignedlong3.name = nameHash4;
    stVSL_signedlong0.name = nameHash1;
    stVSL_signedlong1.name = nameHash2;
    stVSL_signedlong2.name = nameHash3;
    stVSL_signedlong3.name = nameHash4;
    stVF_float0.name = nameHash1;
    stVF_float1.name = nameHash2;
    stVF_float2.name = nameHash3;
    stVF_float3.name = nameHash4;
}
