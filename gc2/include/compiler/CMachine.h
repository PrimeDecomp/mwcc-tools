#ifndef COMPILER_CMACHINE_H
#define COMPILER_CMACHINE_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern TypeIntegral stbool;
extern TypeIntegral stchar;
extern TypeIntegral stsignedchar;
extern TypeIntegral stunsignedchar;
extern TypeIntegral stsignedshort;
extern TypeIntegral stunsignedshort;
extern TypeIntegral stsignedint;
extern TypeIntegral stunsignedint;
extern TypeIntegral stsignedlong;
extern TypeIntegral stunsignedlong;
extern TypeIntegral stsignedlonglong;
extern TypeIntegral stunsignedlonglong;
extern TypeIntegral stfloat;
extern TypeIntegral stshortdouble;
extern TypeIntegral stdouble;
extern TypeIntegral stlongdouble;
extern TypeIntegral stwchar;
extern TypeIntegral stvec2x32float; // (2.6: Gekko's paired single, __vec2x32float__)
extern TypeStruct stvectorunsignedchar;
extern TypeStruct stvectorsignedchar;
extern TypeStruct stvectorboolchar;
extern TypeStruct stvectorunsignedshort;
extern TypeStruct stvectorsignedshort;
extern TypeStruct stvectorboolshort;
extern TypeStruct stvectorunsignedlong;
extern TypeStruct stvectorsignedlong;
extern TypeStruct stvectorboollong;
extern TypeStruct stvectorfloat;
extern TypeStruct stvectorpixel;
extern TypeStruct stvector;

extern void CMach_Configure(void);
extern int CMach_GetQUALalign(Type* type, UInt32 qual);
extern SInt32 CMach_ArgumentAlignment(Type* type);
extern SInt32 CMach_AllocationAlignment(Type* type, UInt32 qual);
extern CInt64 CMach_CalcIntDiadic(Type* type, CInt64 a, SInt16 op, CInt64 b);
extern CInt64 CMach_CalcIntMonadic(Type* type, SInt16 op, CInt64 val);
extern CInt64 CMach_CalcIntConvertFromFloat(Type* type, Float fval);
extern void CMach_InitIntMem(Type* type, CInt64 val, void* mem);
extern void CMach_InitVectorMem(Type* type, MWVector128 val, void* mem, Boolean flag);
extern Float CMach_CalcFloatDiadic(Type* type, Float left, short op, Float right);
extern Float CMach_CalcFloatMonadic(Type* type, short op, Float fval);
extern Boolean CMach_CalcFloatDiadicBool(Type* type, Float left, SInt16 op, Float right);
extern char* CMach_FloatScan(char* input, Float* result, Boolean* fail);
extern Float CMach_CalcFloatConvertFromInt(Type* type, CInt64 value);
extern Float CMach_CalcFloatConvert(Type* type, Float value);
extern Boolean CMach_FloatIsZero(Float fval);
extern Boolean CMach_FloatIsOne(Float fval);
extern Boolean CMach_FloatIsNegOne(Float fval);
extern void CMach_InitFloatMem(Type* type, Float value, void* dest);
extern void CMach_PrintFloat(char* buf, Float val);
extern void CMach_PragmaParams(void);
extern void CMach_AdjustFuntionArgs(void);
extern UInt16 CMach_GetStructAlign(TypeStruct* str);
extern SInt16 CMach_GetClassAlign(TypeClass* tclass);
extern SInt16 CMach_GetTypeAlign(Type* type);
extern SInt16 CMach_MemberAlignValue(Type* type, SInt32 var);
extern void CMach_StructLayoutInitOffset(SInt32 offset);
extern SInt32 CMach_StructLayoutGetCurSize(void);
extern SInt32 CMach_StructLayoutGetOffset(Type* type, UInt32 qual);
extern SInt32 CMach_StructLayoutBitfield(TypeBitfield* tbitfield, UInt32 qual);
extern UInt8 CMach_GetFunctionResultClass(TypeFunc* functype);
extern Boolean Type_RequiresMemoryReturn(Type* type);
extern Boolean CMach_PassResultInHiddenArg(Type* type);
extern const char* CMach_GetCPU(void);
extern Boolean CMach_FloatIsPowerOf2(Float flt);
extern Boolean CMach_IsUnsignedType(Type* type);
extern Type* CMach_GetCustomType(short token, short size, short signedness);
extern CInt64 CMach_CustomIntConvert(Type* type, Type* oldtype, CInt64 value);
extern Float CMach_FloatReciprocal(Float flt);
extern SInt32 CMach_RoundedSizeOf(Object* object);
extern void CMach_ReInitRuntimeObjects(void);
// 2.6: a global register variable's asm("reg")
extern void CMach_ParseGlobalRegisterVariable(DeclInfo* declinfo);
// 2.6: the back-end options CInline keeps with an inline function
extern void CMach_GetBackEndOptions(struct CInlineBackendOptions* options);
extern void CMach_SetBackEndOptions(struct CInlineBackendOptions* options);

// Constant folding's arithmetic (CMach_CalcFloatDiadic, CMach_FloatReciprocal). The original folds on the x87, which
// __dec2num leaves at 64-bit precision: from the first literal on, each result is rounded twice. In WebAssembly,
// wasm/src/x87.c's emulation does that.
#ifdef __wasm__
double __x87_double(int op, double a, double b);
#define CMACH_FOLD(left, op, right) ((left) = __x87_double(#op[0], (left), (right)))
#define CMACH_RECIPROCAL(value) ((value) = __x87_double('/', 1.0, (value)))
#else
#define CMACH_FOLD(left, op, right) ((left) op##= (right))
#define CMACH_RECIPROCAL(value) ((value) = 1.0 / (value))
#endif

#ifdef __cplusplus
}
#endif

#endif
