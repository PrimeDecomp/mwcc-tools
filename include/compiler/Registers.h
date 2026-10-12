#ifndef COMPILER_REGISTERS_H
#define COMPILER_REGISTERS_H

#include "compiler/common.h"
#include "compiler/CPrep.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma options align = mac68k
struct VarInfo {
    struct Object *func;
    SInt32 usage;
    struct TStreamElement deftoken;
    SInt16 varnumber;
    UInt8 noregister;
    UInt8 used;
#if VERSION >= VERSION_GC_1_3
    /* 1.3 (0x2a bytes): the register binding writes the register class at 0x25. The 1.2.5 names overlay these bytes only
     * so that the 1.2.5 register code still compiles; the 1.3 rows using them are not built from it. */
    union {
        struct {
            UInt8 flags;
            UInt8 rclass;
        };
        struct {
            UInt8 in_param_area;
            UInt8 is_fpr;
        };
        struct {
            UInt8 unused24;
            UInt8 is_vector;
        };
    };
    SInt16 reg;
    SInt16 regHi;
#else
    SInt16 reg;
    SInt16 regHi;
    UInt8 is_fpr;
    UInt8 in_param_area;
    UInt8 is_vector;
    UInt8 alignmentPadding;
#endif
};
#pragma options align = reset
#if VERSION >= VERSION_GC_1_3
extern void Registers_BindVR(Object *obj);
#else
extern void Registers_BindVR(Object *obj, short vr);
#endif
extern void Registers_BindFPR(Object *obj, SInt16 regnum);
#if VERSION >= VERSION_GC_1_3
/* 1.3: the reference's RegClass, a signed char-sized enum (a loop over it keeps its entry test) */
typedef enum RegClass13 { RegClass13_Invalid = -1, RegClass13_VR = 2, RegClass13_FPR = 3, RegClass13_GPR = 4, RegClass13_Max = 5 } RegClass13;
/* 1.3: Registers_BindGPR is the reference's retain_register and Registers_BindVR its assign_GPR_pair */
extern void Registers_BindGPR(Object *obj, RegClass13 rclass, short reg);
#define REGISTERS_BIND_GPR(obj, reg) Registers_BindGPR(obj, RegClass13_GPR, reg)
#define REGISTERS_BIND_VR(obj, vr) Registers_BindGPR(obj, RegClass13_VR, vr)
#else
#if VERSION < VERSION_GC_1_3
extern void Registers_BindGPR(Object *obj, SInt16 reg);
#endif
#define REGISTERS_BIND_GPR(obj, reg) Registers_BindGPR(obj, reg)
#define REGISTERS_BIND_VR(obj, vr) Registers_BindVR(obj, vr)
#endif
extern short gAvailableSavedFPRs;
extern SInt16 gAvailableSavedVRs;
extern void Coloring_ResetVRColors(void);
extern void Coloring_ResetFPRColors(void);
extern void Coloring_ResetGPRColors(void);
extern void Registers_SetupVRs(void);
extern void Registers_SetupFPRs(void);
extern void Registers_SetupGPRs(void);
extern VarInfo *Registers_GetInfo(Object *object);
extern void Registers_CloseCoalesceWindow(void);
extern void Registers_UpdateCoalesceWindow(void);
extern void Registers_CheckpointCoalesceWindow(void);
extern void Registers_SnapshotInitialObjectRange(void);
extern void Registers_BeginCoalesceWindow(void);
extern short Coloring_ClaimVRColor(void);
extern short Coloring_ClaimFPRColor(void);
extern short Coloring_ClaimGPRColor(void);
extern unsigned int Coloring_VRColorMask(void);
extern unsigned int Coloring_FPRColorMask(void);
extern unsigned int Coloring_GPRColorMask(void);
extern int Registers_AvailableVRs(void);
extern int Registers_AvailableFPRs(void);
extern int Registers_AvailableGPRs(void);
#if VERSION >= VERSION_GC_1_3
extern void Registers_BindVR(Object *obj);
#else
extern void Registers_BindVR(Object *obj, short vr);
#endif
extern void Registers_BindFPR(Object *obj, SInt16 regnum);
extern void Registers_BindGPRPair(Object *obj, SInt16 reg0, SInt16 reg1);
extern void Registers_AllocateVR(Object *obj);
extern void Registers_AllocateFPR(Object *obj);
extern void Registers_AllocateGPRPair(Object *obj);
extern void Registers_AllocateGPR(Object *obj);
extern unsigned char Registers_AreNonvolatileVRUsed(void);
#if VERSION < VERSION_GC_1_3
extern void Registers_BindGPR(Object *obj, SInt16 reg);
#endif
extern void Registers_SetupStackBaseReg(void);
extern SInt32 fn_004c15f0(void);
extern BY_VERSION( short, SInt32) gFPRCoalesceLast;
extern short gFPRCounterCheckpoint;
extern BY_VERSION(short, SInt32) gGPRCoalesceLast;
extern short gGPRCounterCheckpoint;
extern short gInitialObjectFPRLast;
extern short gInitialObjectVRLast;
extern SInt32 gUseVirtualRegisterNumbers_00587f00;
extern BY_VERSION( short, SInt32) gVRCoalesceLast;
extern short gVRCounterCheckpoint;
extern void Registers_InitRegisterState(void);
extern UInt32 Registers_GetOperandRegMask(PCodeBlock *list);

#ifdef __cplusplus
}
#endif

#endif
