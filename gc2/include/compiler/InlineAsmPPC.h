#ifndef COMPILER_INLINEASMPPC_H
#define COMPILER_INLINEASMPPC_H

#include "compiler/common.h"
#include "compiler/InlineAsm.h"

#ifdef __cplusplus
extern "C" {
#endif

// Pro7's assembler kinds: asm statements in a C function (InlineAsm_ScanStatements), or a whole asm
// function (InlineAsm_ScanFunction) (TODO: invented names for Pro7's AssemblerType_0 and _1)
typedef enum AssemblerType {
    AssemblerType_Statements,
    AssemblerType_Function
} AssemblerType;

enum {
    IADirective_Null,
    IADirective_Entry,
    IADirective_FrAlloc,
    IADirective_NoFrAlloc,
    IADirective_FrFree,
    IADirective_Machine,
    // 2.6: `warn_def_reserved_reg on|off`, `volatile`, `nonvolatile`, `scheduling on|off`
    IADirective_WarnDefReservedReg,
    IADirective_Volatile,
    IADirective_NonVolatile,
    IADirective_Scheduling,
    IADirective_SmClass
};

// What a processor has (`cpu`, IAMnemonic.cpu, the registers' cpus): the low bits one per processor
// family, the high bits the optional instruction groups, named after the mnemonics that need each
// (TODO: invented names for Pro7's CPUFLAG_800000 to CPUFLAG_80000000; 0x100000-0x400000 are unused)
enum {
    CPUFLAG_LOW_MASK = 0xFFFFF,
    CPUFLAG_100000 = 0x100000,
    CPUFLAG_200000 = 0x200000,
    CPUFLAG_400000 = 0x400000,
    CPUFLAG_GRAPHICS = 0x800000, // fres, frsqrte, fsel
    CPUFLAG_SEGMENTS = 0x1000000, // mfsr, mtsr, mfsrin, mtsrin
    CPUFLAG_EXTERNAL_CONTROL = 0x2000000, // eciwx, ecowx
    CPUFLAG_TLB = 0x4000000, // tlbie, tlbsync
    CPUFLAG_CACHE = 0x8000000, // dcbf, dcbst, dcbt, dcbz, dcbi ...
    CPUFLAG_TIME_BASE = 0x10000000, // mftb, mttbl ...
    CPUFLAG_PAIRED_SINGLE = 0x20000000, // 2.6: Gekko's psq_l, ps_add ...
    CPUFLAG_ALTIVEC = 0x40000000, // (set when -altivec_model is on)
    CPUFLAG_FPU = 0x80000000 // (needs a hardware FPU: supports_hardware_fpu)
};

#define OPCODE_PART_1(opcode) ((opcode) >> 26)
#define OPCODE_PART_2(opcode) (((opcode) >> 21) & 0x1F)
#define OPCODE_PART_3(opcode) (((opcode) >> 1) & 0x3FF)

// clang-format off
enum {
    CPUMask_401 = CPUFLAG_CACHE | 0x40,
    CPUMask_403 = CPUFLAG_CACHE | 0x80,
    CPUMask_50x = CPUFLAG_TIME_BASE | CPUFLAG_FPU | 0x800,
    CPUMask_55x_56x = CPUFLAG_TIME_BASE | CPUFLAG_FPU | 0x1000,
    CPUMask_601 = CPUFLAG_SEGMENTS | CPUFLAG_EXTERNAL_CONTROL | CPUFLAG_TLB | CPUFLAG_CACHE | CPUFLAG_FPU | 1,
    CPUMask_602 = CPUFLAG_GRAPHICS | CPUFLAG_SEGMENTS | CPUFLAG_TLB | CPUFLAG_CACHE | CPUFLAG_TIME_BASE | CPUFLAG_FPU | 2,
    CPUMask_8240 = CPUFLAG_GRAPHICS | CPUFLAG_SEGMENTS | CPUFLAG_EXTERNAL_CONTROL | CPUFLAG_TLB | CPUFLAG_CACHE | CPUFLAG_TIME_BASE | CPUFLAG_FPU | 0x10000,
    CPUMask_8260 = CPUFLAG_SEGMENTS | CPUFLAG_EXTERNAL_CONTROL | CPUFLAG_TLB | CPUFLAG_CACHE | CPUFLAG_TIME_BASE | 0x10000,
    CPUMask_603 = CPUFLAG_GRAPHICS | CPUFLAG_SEGMENTS | CPUFLAG_EXTERNAL_CONTROL | CPUFLAG_TLB | CPUFLAG_CACHE | CPUFLAG_TIME_BASE | CPUFLAG_FPU | 4,
    CPUMask_604 = CPUFLAG_GRAPHICS | CPUFLAG_SEGMENTS | CPUFLAG_EXTERNAL_CONTROL | CPUFLAG_TLB | CPUFLAG_CACHE | CPUFLAG_TIME_BASE | CPUFLAG_FPU | 8,
    CPUMask_740_750 = CPUFLAG_GRAPHICS | CPUFLAG_SEGMENTS | CPUFLAG_EXTERNAL_CONTROL | CPUFLAG_TLB | CPUFLAG_CACHE | CPUFLAG_TIME_BASE | CPUFLAG_FPU | 0x2000,
    CPUMask_801_821_860 = CPUFLAG_TLB | CPUFLAG_CACHE | CPUFLAG_TIME_BASE | 0x10,
    CPUMask_823_850 = CPUFLAG_TLB | CPUFLAG_CACHE | CPUFLAG_TIME_BASE | 0x20,
    CPUMask_74xx = CPUFLAG_GRAPHICS | CPUFLAG_SEGMENTS | CPUFLAG_EXTERNAL_CONTROL | CPUFLAG_TLB | CPUFLAG_CACHE | CPUFLAG_TIME_BASE | CPUFLAG_ALTIVEC | CPUFLAG_FPU | 0x6000,
    // 2.6: Gekko (TODO: invented name)
    CPUMask_Gekko = CPUFLAG_GRAPHICS | CPUFLAG_SEGMENTS | CPUFLAG_EXTERNAL_CONTROL | CPUFLAG_TLB | CPUFLAG_CACHE | CPUFLAG_TIME_BASE | CPUFLAG_PAIRED_SINGLE | CPUFLAG_FPU | 0x2000,
    CPUMask_All = CPUFLAG_GRAPHICS | CPUFLAG_SEGMENTS | CPUFLAG_EXTERNAL_CONTROL | CPUFLAG_TLB | CPUFLAG_CACHE | CPUFLAG_TIME_BASE | CPUFLAG_PAIRED_SINGLE | CPUFLAG_ALTIVEC | CPUFLAG_FPU | 0xFFFFF,
    CPUMask_Generic = CPUFLAG_LOW_MASK
};
// clang-format on

extern char asm_alloc_flags[10];
extern UInt16 sm_section; // 2.6: the ELF section index (Pro7's Section)
extern UInt32 cpu;
extern SInt32 fralloc_parameter_area_size;
extern Boolean user_responsible_for_frame;
extern Boolean supports_hardware_fpu;
extern UInt32 assembledinstructions;
extern AssemblerType assembler_type;
extern char volatileasm;
extern Boolean InlineAsm_gccmode;
extern Boolean InlineAsm_labelref;

extern const char* InlineAsm_GetMnemonic(InlineAsm* ia);
extern void InlineAsm_InitializePPC(void);
extern void InlineAsm_Initialize(AssemblerType assemblertype);
extern SInt32 InlineAsm_IsDirective(AssemblerType assemblertype);
extern void InlineAsm_ProcessDirective(SInt32 directive);
extern void InlineAsm_ScanAssemblyDirective(void);
extern void InlineAsm_ScanAssemblyInstruction(void);
extern void InlineAsm_TranslateIRtoPCode(Statement* stmt);
extern void CodeGen_GetAsmEffects(Statement* stmt, IAEffects* effects);
extern void CodeGen_PropagateIntoAsm(Statement* stmt, Object* obj, ENode* expr);
extern Statement* CodeGen_CopyAsmStat(Statement* stmt);

#ifdef __cplusplus
}
#endif

#endif
