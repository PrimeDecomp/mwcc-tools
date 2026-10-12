#define CERROR_FILE "Registers.c"
#include "compiler/common.h"
#include "compiler/Registers.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/types.h"
#include "compiler/CError.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CopyPropagation.h"
#include "compiler/InstrSelection.h"
#include "compiler/InterferenceGraph.h"
#include "compiler/Intrinsics.h"
#include "compiler/PCode.h"
#include "compiler/Scheduler.h"
#include "compiler/StackFrameEABI.h"
#include <string.h>

#if VERSION >= VERSION_GC_1_3
#define REGINFO_FILE "RegisterInfo.c"
#else
#define REGINFO_FILE CERROR_FILE
#endif

SInt32 gUseVirtualRegisterNumbers_00587f00;
BY_VERSION(short, SInt32) gFPRCoalesceLast;
BY_VERSION(short, SInt32) gGPRCoalesceLast;
short gInitialObjectVRLast;
short gVRCounterCheckpoint;
short gInitialObjectFPRLast;
short gAvailableSavedFPRs;
BY_VERSION(short, SInt32) gVRCoalesceLast;
short gFPRCounterCheckpoint;
short gGPRCounterCheckpoint;
SInt16 gAvailableSavedVRs;

static UInt8 gUsedPhysicalGPR[32];
static unsigned char gUsedPhysicalFPR[32];
static UInt8 gUsedPhysicalVR[32];
static short gSaveSpan;
static unsigned char gSavedUsedPhysicalRegisters[32];
static char lbl_00581392[14];

static void Registers_RestoreClassState(unsigned char *used, short *save_span)
{
    *save_span = gSaveSpan;
    memcpy(used, gSavedUsedPhysicalRegisters, 32);
}

static void Registers_SaveClassState(const unsigned char *used, short save_span)
{
    gSaveSpan = save_span;
    memcpy(gSavedUsedPhysicalRegisters, used, 32);
}

static void Registers_Require(int condition, int line)
{
    if (!condition) {
        CError_Internal("Registers.c", line);
    }
}

static void Registers_RecordPhysicalUse(unsigned char *used, short reg, int scan_floor, int saved_floor,
                                        short *save_span, short *available_saved)
{
    int first_used;
    int free_count;
    int scan;

    if (used[reg] != 0) {
        return;
    }

    used[reg] = 1;
    first_used = 32;
    free_count = 0;
    for (scan = 31; scan >= scan_floor; scan--) {
        if (used[scan] == 1) {
            first_used = scan;
        }
        if (scan > saved_floor && used[scan] == 0) {
            free_count++;
        }
    }
    *save_span = (short)(32 - first_used);
    *available_saved = (short)free_count;
}

static int Registers_ObjectUsesFPRs(Object *object)
{
    return copts.operandsDebug && object->type->type == TYPEFLOAT;
}

static short Registers_FindFree(unsigned char *used, int first, void (*bind)(Object *, short))
{
    int reg;

    for (reg = 31; reg >= first; reg--) {
        if (used[reg] == 0) {
            bind(NULL, (short)reg);
            return (short)reg;
        }
    }
    return -1;
}

static unsigned int Registers_BuildColorMask(const unsigned char *used, int last_register)
{
    unsigned int mask;
    int reg;

    mask = 0;
    for (reg = 0; reg <= last_register; reg++) {
        if (used[reg] == 0) {
            mask |= 1U << reg;
        }
    }
    return mask;
}

static int Registers_CountFree(const unsigned char *used)
{
    int count;
    int reg;

    count = 0;
    for (reg = 0; reg < 32; reg++) {
        if (used[reg] == 0) {
            count++;
        }
    }
    return count;
}

static void Registers_MarkFPRUsed(SInt16 regnum)
{
    SInt32 avail;
    SInt32 start;
    SInt32 i;

    start = 0x20;
    avail = 0;
    gUsedPhysicalFPR[regnum] = 1;
    i = 0x1f;
    do {
        if (gUsedPhysicalFPR[i] == 1) {
            start = i;
        }
        if (i > 0x11 && gUsedPhysicalFPR[i] == 0) {
            avail++;
        }
    } while (--i >= 0xe);
    gFPRSaveSpan = 0x20 - start;
    gAvailableSavedFPRs = avail;
}

static SInt16 Registers_FindFreeGPR(void)
{
    SInt32 i = 31;
    do {
        if (gUsedPhysicalGPR[i] == 0) {
            REGISTERS_BIND_GPR(NULL, i);
            return (SInt16)i;
        }
        i--;
    } while (i >= 14);
    return -1;
}

static VarInfo *Registers_NewInfo(void)
{
    VarInfo *p = (VarInfo *)galloc(sizeof(VarInfo));
    memclrw(p, sizeof(VarInfo));
    return p;
}

void Registers_InitRegisterState(void)
{
    SInt16 i;
    gUsedVirtualRegistersGPR = 0x20;
    gUsedVirtualRegistersFPR = 0x20;
    gUsedVirtualRegistersVR = 0x20;
    gGPRSaveSpan = 0;
    gFPRSaveSpan = 0;
    data_005883ee = 0;
    gVRSaveSpan = 0;
    gAvailableSavedGPRs = 0x10;
    gAvailableSavedFPRs = 0xe;
    gAvailableSavedVRs = 0xc;
    for (i = 0; i <= 0x1f; i++) {
        gUsedPhysicalGPR[i] = 0;
        gUsedPhysicalFPR[i] = 0;
        gUsedPhysicalVR[i] = 0;
    }
    gUsedPhysicalGPR[1] = 2;
    gUsedPhysicalGPR[2] = 2;
    REGISTERS_BIND_GPR(NULL, 0xd);
    gUsedPhysicalGPR[13] = 2;
    {
        SInt32 usevrs = 0;
        if (copts.deleteDeadInstructions > 0) {
            SInt32 cond = !data_00588224;
            if (cond)
                usevrs = 1;
        }
        gUseVirtualRegisterNumbers_00587f00 = usevrs;
    }
    gVirtualRegistersActive = 1;
}

void Registers_SetupStackBaseReg(void)
{
    if (data_0058852d != 0U) {
        REGISTERS_BIND_GPR(NULL, 31);
        stack_base_reg = 31U;
    } else {
        stack_base_reg = 1U;
    }
}

void Registers_AllocateGPR(Object *obj)
{
    VarInfo *info;
    SInt16 reg;
    SInt32 i;

    switch (obj->datatype) {
        case DDATA:
            if (obj->u.data.info == NULL) {
                VarInfo *v = (VarInfo *)galloc(sizeof(VarInfo));
                memclrw(v, sizeof(VarInfo));
                obj->u.data.info = v;
            }
            info = obj->u.data.info;
            break;
        case DLOCAL:
            CError_ASSERT(745, obj->u.var.info);
            info = obj->u.var.info;
            break;
        case DABSOLUTE:
            if (obj->u.data.info == NULL) {
                info = (VarInfo *)galloc(sizeof(VarInfo));
                memclrw(info, sizeof(VarInfo));
                obj->u.data.info = info;
            }
            info = obj->u.data.info;
            break;
        default:
            CError_FATAL(758);
            info = NULL;
            break;
    }

    if (gUseVirtualRegisterNumbers_00587f00 != 0) {
        reg = gUsedVirtualRegistersGPR++;
    } else {
        i = 0x1f;
        for (;;) {
            if (gUsedPhysicalGPR[i] == 0) {
                REGISTERS_BIND_GPR(NULL, i);
                break;
            }
            if (--i < 0xe) {
                i = -1;
                break;
            }
        }
        reg = (SInt16)i;
    }

    info->is_fpr = 0;
    info->is_vector = 0;
    if (copts.operandsDebug != 0 && obj->type->type == TYPEFLOAT)
        info->is_fpr = 1;
    if (reg > 0)
        info->reg = reg;
}

#if VERSION < VERSION_GC_1_3
void Registers_AllocateGPRPair(Object *obj)
{
    SInt16 reg1;
    SInt16 reg2;
    VarInfo *info;

    switch (obj->datatype) {
        case DDATA:
            if (obj->u.data.info == NULL)
                obj->u.data.info = Registers_NewInfo();
            info = obj->u.data.info;
            break;
        case DLOCAL:
            CError_ASSERT(745, obj->u.var.info != NULL);
            info = obj->u.var.info;
            break;
        case DABSOLUTE:
            if (obj->u.data.info == NULL)
                obj->u.data.info = Registers_NewInfo();
            info = obj->u.data.info;
            break;
        default:
            CError_FATAL(758);
            info = NULL;
            break;
    }

    if (gUseVirtualRegisterNumbers_00587f00) {
        reg1 = gUsedVirtualRegistersGPR++;
        reg2 = gUsedVirtualRegistersGPR++;
    } else {
        CError_ASSERT(185, gAvailableSavedGPRs >= 2);
        reg1 = Registers_FindFreeGPR();
        reg2 = Registers_FindFreeGPR();
    }

    info->is_fpr = 0;
    info->is_vector = 0;
    if (copts.operandsDebug && obj->type->type == TYPEFLOAT)
        info->is_fpr = 1;
    if (reg1 > 0 && reg2 > 0) {
        info->reg = reg1;
        info->regHi = reg2;
    }
}
#endif

void Registers_AllocateFPR(Object *obj)
{
    SInt16 reg;
    SInt32 i;
    VarInfo *info;

    switch (obj->datatype) {
        case DDATA:
            if (obj->u.data.info == NULL) {
                VarInfo *p = (VarInfo *)galloc(sizeof(VarInfo));
                memclrw(p, sizeof(VarInfo));
                obj->u.data.info = p;
            }
            info = obj->u.data.info;
            break;

        case DLOCAL:
            CError_ASSERT(745, obj->u.var.info != NULL);
            info = obj->u.var.info;
            break;

        case DABSOLUTE:
            if (obj->u.data.info == NULL) {
                VarInfo *p = (VarInfo *)galloc(sizeof(VarInfo));
                memclrw(p, sizeof(VarInfo));
                obj->u.data.info = p;
            }
            info = obj->u.data.info;
            break;

        default:
            CError_FATAL(758);
            info = NULL;
            break;
    }

    if (gUseVirtualRegisterNumbers_00587f00) {
        reg = gUsedVirtualRegistersFPR++;
    } else {
        i = 31;
        for (;;) {
            if (gUsedPhysicalFPR[i] == 0) {
                Registers_BindFPR(NULL, i);
                reg = (SInt16)i;
                break;
            }
            if (--i < 14) {
                reg = -1;
                break;
            }
        }
    }

    info->is_fpr = 1;
    info->is_vector = 0;
    if (reg > 0)
        info->reg = reg;
}

void Registers_AllocateVR(Object *obj)
{
    SInt16 reg;
    SInt32 i;
    VarInfo *vr;

    switch (obj->datatype) {
        case DDATA:
            if (obj->u.data.info == NULL) {
                vr = (VarInfo *)galloc(sizeof(*vr));
                memclrw(vr, sizeof(*vr));
                obj->u.data.info = vr;
            }
            vr = obj->u.data.info;
            break;
        case DLOCAL:
            if (obj->u.var.info == NULL)
                CError_FATAL(745);
            vr = obj->u.var.info;
            break;
        case DABSOLUTE:
            if (obj->u.data.info == NULL) {
                vr = (VarInfo *)galloc(sizeof(*vr));
                memclrw(vr, sizeof(*vr));
                obj->u.data.info = vr;
            }
            vr = obj->u.data.info;
            break;
        default:
            CError_FATAL(758);
            vr = NULL;
    }

    if (gUseVirtualRegisterNumbers_00587f00 != 0) {
        reg = gUsedVirtualRegistersVR;
        gUsedVirtualRegistersVR++;
    } else {
        i = 31;
        for (;;) {
            if (gUsedPhysicalVR[i] == 0) {
                REGISTERS_BIND_VR(NULL, i);
                reg = (SInt16)i;
                break;
            }
            i--;
            if (i < 20) {
                reg = -1;
                break;
            }
        }
    }
    vr->is_fpr = 0;
    vr->is_vector = 1;
    if (reg > 0)
        vr->reg = reg;
}

#if VERSION < VERSION_GC_1_3
void Registers_BindGPR(Object *obj, SInt16 reg)
{
    SInt32 i;
    SInt32 maxreg;
    SInt32 avail;
    VarInfo *info;

    if (gUsedPhysicalGPR[reg] == 0) {
        maxreg = 32;
        avail = 0;
        gUsedPhysicalGPR[reg] = 1;
        i = 31;
        do {
            if (gUsedPhysicalGPR[i] == 1)
                maxreg = i;
            if (i > 15 && gUsedPhysicalGPR[i] == 0)
                avail++;
        } while (--i >= 14);
        gGPRSaveSpan = (SInt16)(32 - maxreg);
        gAvailableSavedGPRs = (SInt16)avail;
    }

    if (obj != NULL) {
        switch (obj->datatype) {
            case DDATA:
                if (obj->u.data.info == NULL) {
                    info = (VarInfo *)galloc(sizeof(VarInfo));
                    memclrw(info, sizeof(VarInfo));
                    obj->u.data.info = info;
                }
                info = obj->u.data.info;
                break;
            case DLOCAL:
                CError_ASSERT(745, obj->u.var.info != NULL);
                info = obj->u.var.info;
                break;
            case DABSOLUTE:
                if (obj->u.data.info == NULL) {
                    info = (VarInfo *)galloc(sizeof(VarInfo));
                    memclrw(info, sizeof(VarInfo));
                    obj->u.data.info = info;
                }
                info = obj->u.data.info;
                break;
            default:
                CError_FATAL(758);
                info = NULL;
        }

        info->is_fpr = 0;
        info->is_vector = 0;
        info->reg = reg;
        if (copts.operandsDebug && obj->type->type == TYPEFLOAT)
            info->is_fpr = 1;
    }
}
#endif

#if VERSION < VERSION_GC_1_3
void Registers_BindGPRPair(Object *obj, SInt16 reg0, SInt16 reg1)
{
    VarInfo *info;

    Registers_BindGPR(NULL, reg0);
    Registers_BindGPR(NULL, reg1);
    if (obj != NULL) {
        switch (obj->datatype) {
            case DDATA:
                if (obj->u.data.info == NULL) {
                    VarInfo *vi = (VarInfo *)galloc(sizeof(VarInfo));
                    memclrw(vi, sizeof(VarInfo));
                    obj->u.data.info = vi;
                }
                info = obj->u.data.info;
                break;
            case DLOCAL:
                if (obj->u.var.info == NULL)
                    CError_FATAL(745);
                info = obj->u.var.info;
                break;
            case DABSOLUTE:
                if (obj->u.data.info == NULL) {
                    VarInfo *vi = (VarInfo *)galloc(sizeof(VarInfo));
                    memclrw(vi, sizeof(VarInfo));
                    obj->u.data.info = vi;
                }
                info = obj->u.data.info;
                break;
            default:
                CError_FATAL(758);
                info = NULL;
                break;
        }
        info->is_fpr = 0;
        info->is_vector = 0;
        info->reg = reg0;
        info->regHi = reg1;
        if (copts.operandsDebug && obj->type->type == TYPEFLOAT)
            info->is_fpr = 1;
    }
}
#endif

void Registers_BindFPR(Object *obj, SInt16 regnum)
{
    if (gUsedPhysicalFPR[regnum] == 0) {
        Registers_MarkFPRUsed(regnum);
    }
    if (obj != NULL) {
        VarInfo *info;

        switch (obj->datatype) {
            case DDATA:
                if (obj->u.data.info == NULL) {
                    VarInfo *p;
                    p = galloc(sizeof(VarInfo));
                    memclrw(p, sizeof(VarInfo));
                    obj->u.data.info = p;
                }
                info = obj->u.data.info;
                break;
            case DLOCAL:
                if (obj->u.var.info == NULL) {
                    CError_FATAL(745);
                }
                info = obj->u.var.info;
                break;
            case DABSOLUTE:
                if (obj->u.data.info == NULL) {
                    VarInfo *p;
                    p = galloc(sizeof(VarInfo));
                    memclrw(p, sizeof(VarInfo));
                    obj->u.data.info = p;
                }
                info = obj->u.data.info;
                break;
            default:
                CError_FATAL(758);
                info = NULL;
        }

        info->is_fpr = 1;
        info->is_vector = 0;
        info->reg = regnum;
    }
}

#if VERSION < VERSION_GC_1_3
void Registers_BindVR(Object *obj, short vr)
{
    if (gUsedPhysicalVR[vr] == 0) {
        SInt32 i;
        SInt32 span = 0x20;
        SInt32 avail = 0;

        gUsedPhysicalVR[vr] = 1;
        i = 0x1f;
        do {
            if (gUsedPhysicalVR[i] == 1)
                span = i;
            if (i > 0x13 && gUsedPhysicalVR[i] == 0)
                avail++;
            i--;
        } while (i >= 0x14);
        gVRSaveSpan = 0x20 - span;
        gAvailableSavedVRs = avail;
    }
    if (obj != NULL) {
        VarInfo *regs;

        switch (obj->datatype) {
            case DDATA:
                if (obj->u.data.info == NULL) {
                    VarInfo *p = (VarInfo *)galloc(sizeof(VarInfo));
                    memclrw(p, sizeof(VarInfo));
                    obj->u.data.info = p;
                }
                regs = obj->u.data.info;
                break;
            case DLOCAL:
                CError_ASSERT(745, obj->u.var.info != NULL);
                regs = obj->u.var.info;
                break;
            case DABSOLUTE:
                if (obj->u.data.info == NULL) {
                    VarInfo *p = (VarInfo *)galloc(sizeof(VarInfo));
                    memclrw(p, sizeof(VarInfo));
                    obj->u.data.info = p;
                }
                regs = obj->u.data.info;
                break;
            default:
                CError_FATAL(758);
                regs = NULL;
                break;
        }

        regs->is_fpr = 0;
        regs->is_vector = 1;
        regs->reg = vr;
    }
}
#endif

int Registers_AvailableGPRs(void)
{
    return Registers_CountFree(gUsedPhysicalGPR);
}

int Registers_AvailableFPRs(void)
{
    return Registers_CountFree(gUsedPhysicalFPR);
}

int Registers_AvailableVRs(void)
{
    return Registers_CountFree(gUsedPhysicalVR);
}

unsigned int Coloring_GPRColorMask(void)
{
    return Registers_BuildColorMask(gUsedPhysicalGPR, 12);
}

unsigned int Coloring_FPRColorMask(void)
{
    return Registers_BuildColorMask(gUsedPhysicalFPR, 13);
}

unsigned int Coloring_VRColorMask(void)
{
    return Registers_BuildColorMask(gUsedPhysicalVR, 19);
}

short Coloring_ClaimGPRColor(void)
{
    int reg;
    for (reg = 31; reg >= 14; --reg) {
        if (gUsedPhysicalGPR[reg] == 0) {
            REGISTERS_BIND_GPR(NULL, reg);
            return reg;
        }
    }
    return -1;
}

short Coloring_ClaimFPRColor(void)
{
    int color;

    for (color = 31; color >= 14; --color) {
        if (gUsedPhysicalFPR[color] == 0) {
            Registers_BindFPR(NULL, color);
            return color;
        }
    }
    return -1;
}

short Coloring_ClaimVRColor(void)
{
    int color;

    for (color = 31; color >= 20; --color) {
        if (gUsedPhysicalVR[color] == 0) {
            REGISTERS_BIND_VR(NULL, color);
            return color;
        }
    }
    return -1;
}

unsigned char Registers_AreNonvolatileVRUsed(void)
{
    int reg;

    for (reg = 31; reg >= 20; --reg) {
        if (gUsedPhysicalVR[reg] != 0) {
            return 1;
        }
    }
    return 0;
}

void Registers_BeginCoalesceWindow(void)
{
    gGPRCoalesceFirst = gGPRCoalesceLast = gUsedVirtualRegistersGPR;
    gFPRCoalesceFirst = gFPRCoalesceLast = gUsedVirtualRegistersFPR;
    gVRCoalesceFirst = gVRCoalesceLast = gUsedVirtualRegistersVR;
}

void Registers_SnapshotInitialObjectRange(void)
{
    gInitialObjectGPRLast = gUsedVirtualRegistersGPR - 1;
    gInitialObjectFPRLast = gUsedVirtualRegistersFPR - 1;
    gInitialObjectVRLast = gUsedVirtualRegistersVR - 1;
}

void Registers_CheckpointCoalesceWindow(void)
{
    gGPRCounterCheckpoint = gGPRCoalesceLast = gUsedVirtualRegistersGPR;
    gFPRCounterCheckpoint = gFPRCoalesceLast = gUsedVirtualRegistersFPR;
    gVRCounterCheckpoint = gVRCoalesceLast = gUsedVirtualRegistersVR;
}

void Registers_UpdateCoalesceWindow(void)
{
    if (!gUseVirtualRegisterNumbers_00587f00) {
        if (gUsedVirtualRegistersGPR > gGPRCoalesceLast) {
            gGPRCoalesceLast = gUsedVirtualRegistersGPR;
        }
        if (gUsedVirtualRegistersFPR > gFPRCoalesceLast) {
            gFPRCoalesceLast = gUsedVirtualRegistersFPR;
        }
        if (gUsedVirtualRegistersVR > gVRCoalesceLast) {
            gVRCoalesceLast = gUsedVirtualRegistersVR;
        }
        if (gUsedVirtualRegistersGPR > 0x100) {
            gUsedVirtualRegistersGPR = gGPRCounterCheckpoint;
        }
        if (gUsedVirtualRegistersFPR > 0x100) {
            gUsedVirtualRegistersFPR = gFPRCounterCheckpoint;
        }
        if (gUsedVirtualRegistersVR > 0x100) {
            gUsedVirtualRegistersVR = gVRCounterCheckpoint;
        }
    }
}

void Registers_CloseCoalesceWindow(void)
{
    if (gUsedVirtualRegistersGPR < gGPRCoalesceLast) {
        gUsedVirtualRegistersGPR = gGPRCoalesceLast;
    } else {
        gGPRCoalesceLast = gUsedVirtualRegistersGPR;
    }
    if (gUsedVirtualRegistersFPR < gFPRCoalesceLast) {
        gUsedVirtualRegistersFPR = gFPRCoalesceLast;
    } else {
        gFPRCoalesceLast = gUsedVirtualRegistersFPR;
    }
    if (gUsedVirtualRegistersVR < gVRCoalesceLast) {
        gUsedVirtualRegistersVR = gVRCoalesceLast;
    } else {
        gVRCoalesceLast = gUsedVirtualRegistersVR;
    }
}

VarInfo *Registers_GetInfo(Object *object)
{
    VarInfo *info;

    switch (object->datatype) {
        case DDATA:
            if (!object->u.data.info) {
                info = (VarInfo *)galloc(BY_VERSION(44U, 0x2a));
                memclrw(info, BY_VERSION(44U, 0x2a));
                object->u.data.info = info;
            }
            return object->u.data.info;
        case DLOCAL:
            if (!object->u.var.info)
#if VERSION >= VERSION_GC_1_3
                CError_Internal(REGINFO_FILE, 647);
#else
                CError_FATAL(745);
#endif
            return object->u.var.info;
        case DABSOLUTE:
            if (!object->u.data.info) {
                info = (VarInfo *)galloc(BY_VERSION(44U, 0x2a));
                memclrw(info, BY_VERSION(44U, 0x2a));
                object->u.data.info = info;
            }
            return object->u.data.info;
        default:
#if VERSION >= VERSION_GC_1_3
            CError_Internal(REGINFO_FILE, 660);
#else
            CError_FATAL(758);
#endif
            return NULL;
    }
}

UInt32 Registers_GetOperandRegMask(PCodeBlock *list)
{
    UInt32 mask = 0;
    PCodeBlock *cur = list;

    while (cur != NULL) {
        PCodeInstruction *node;
        for (node = cur->instructions; node != NULL; node = node->next) {
            if (node->flags & 3) {
                SInt32 i;
                for (i = 0; i < node->operand_count; i++) {
                    if (node->operandData.operands[i].kind == PCOp_VR)
                        mask |= 1 << (31 - node->operandData.operands[i].value.reg);
                }
            }
        }
        cur = cur->next;
    }
    return mask;
}

SInt32 fn_004c15f0(void)
{
    SInt16 i;
    SInt16 n;

    if (data_00588521 == 0)
        return 1;

    n = 0x20;
    for (i = 0; i <= 0xd; i++) {
        if (gUsedPhysicalFPR[i] != 0)
            n++;
    }
    if (gUsedVirtualRegistersFPR > n)
        return 1;

    n = 0x20;
    for (i = 0; i <= 0x13; i++) {
        if (gUsedPhysicalVR[i] != 0)
            n++;
    }
    if (gUsedVirtualRegistersVR > n)
        return 1;

    for (i = 0xe; i <= 0x1f; i++) {
        if (gUsedPhysicalFPR[i] != 0)
            return 1;
    }

    for (i = 0x14; i <= 0x1f; i++) {
        if (gUsedPhysicalVR[i] != 0)
            return 1;
    }

    return 0;
}

void Registers_SetupGPRs(void)
{
    gSaveSpan = gGPRSaveSpan;
    memcpy(gSavedUsedPhysicalRegisters, gUsedPhysicalGPR, 32);
}

void Registers_SetupFPRs(void)
{
    gSaveSpan = gFPRSaveSpan;
    memcpy(gSavedUsedPhysicalRegisters, gUsedPhysicalFPR, 32);
}

void Registers_SetupVRs(void)
{
    gSaveSpan = gVRSaveSpan;
    memcpy(gSavedUsedPhysicalRegisters, gUsedPhysicalVR, 32);
}

void Coloring_ResetGPRColors(void)
{
    gGPRSaveSpan = gSaveSpan;
    memcpy(gUsedPhysicalGPR, gSavedUsedPhysicalRegisters, 32);
}

void Coloring_ResetFPRColors(void)
{
    gFPRSaveSpan = gSaveSpan;
    memcpy(gUsedPhysicalFPR, gSavedUsedPhysicalRegisters, 32);
}

void Coloring_ResetVRColors(void)
{
    gVRSaveSpan = gSaveSpan;
    memcpy(gUsedPhysicalVR, gSavedUsedPhysicalRegisters, 32);
}

#if VERSION >= VERSION_GC_1_3
struct Fn004ceda0 {
    SInt32 w[8];
};
extern UInt8 data_005e2bf0[5][32];
extern SInt32 data_005e674c[];
extern SInt16 data_005dca90;
extern struct Fn004ceda0 data_005dca70;

void fn_004ceda0(RegClass13 rclass)
{
    data_005e674c[rclass] = data_005dca90;
    memcpy(data_005e2bf0[rclass], &data_005dca70, sizeof(data_005dca70));
}

void fn_004cedd0(RegClass13 rclass)
{
    data_005dca90 = data_005e674c[rclass];
    memcpy(&data_005dca70, data_005e2bf0[rclass], sizeof(data_005dca70));
}
#endif

#if VERSION >= VERSION_GC_1_3
extern UInt8 data_005e7375;
extern int fn_004cf6d0(RegClass13 rclass);


void fn_004cf620(void)
{
    RegClass13 i;
    if (data_005e7375) {
        memset(data_005e2bf0, 0, 0xa0);
        for (i = 0; i < RegClass13_Max; i++)
            fn_004cf6d0(i);
    }
}
#endif

#if VERSION >= VERSION_GC_1_3
extern SInt32 data_005e6794[];
extern SInt32 data_005e0ef0[5][32];

int fn_004ced60(RegClass13 rclass, int reg)
{
    int i;

    for (i = 0; i < data_005e6794[rclass]; i++) {
        if (reg == data_005e0ef0[rclass][i])
            return 1;
    }

    return 0;
}
#endif

#if VERSION >= VERSION_GC_1_3
char fn_004cfa90(char cls)
{
    switch (cls) {
    case 2:
        return 'v';
    case 4:
        return 'r';
    case 3:
        return 'f';
    default:
        CError_Internal("RegisterInfo.c", 0xf2);
        return '?';
    }
}

int fn_004cfad0(char cls)
{
    switch (cls) {
    case 0:
        return 3;
    case 1:
        return 2;
    case 2:
        return 0x14;
    case 4:
        return 0xe;
    case 3:
        return 0xe;
    default:
        return -1;
    }
}

/* a register a variable is bound to (1.3), and its name for diagnostics */
typedef struct RegisterBinding13 {
    struct RegisterBinding13 *next;
    Object *object;
    HashNameNode *name;
    char rclass;
    char reg;
} RegisterBinding13;
extern RegisterBinding13 *data_005e686c;
extern SInt32 data_005e6cc0[5];

/* RegisterInfo.c (1.3), with GetABIFirstNonVolatile inlined */
void CMachine_ResetMaximumAlignment(void)
{
    RegClass13 rclass;

    data_005e686c = NULL;
    data_005e7375 = 1;
    for (rclass = 0; rclass < RegClass13_Max; rclass++)
        data_005e6cc0[rclass] = 29 - fn_004cfad0(rclass);
}

#if VERSION >= VERSION_GC_1_3
#define data_005e713d (*(UInt8 *)((char *)&copts + 0x5b))

int fn_004cfb10(UInt8 *p)
{
    UInt8 *r;

    if (p != NULL) {
        switch (p[2]) {
        case 0:
            r = p + 0x36;
            break;
        default:
            r = NULL;
            break;
        }
    } else {
        r = NULL;
    }
    if (data_005e713d != 0)
        return 0;
    if (r != NULL && r[1] != 0)
        return 1;
    return 0;
}

/* the reference's is_register_object; 1.3 also takes an object fn_004cfb10 accepts */
Boolean fn_004cfb90(Object *obj)
{
    return obj->sclass == 0x101 || fn_004cfb10((UInt8 *)obj);
}
#endif
#endif

#if VERSION >= VERSION_GC_1_3
extern SInt32 data_005e6574[];
extern SInt32 data_005e0bf0[5][32];

UInt32 fn_004fcb20(RegClass13 rclass)
{
    UInt32 mask;
    int i;
    int reg;

    mask = 0;
    for (i = 0; i < data_005e6574[rclass]; i++) {
        if (!data_005e2bf0[rclass][reg = data_005e0bf0[rclass][i]])
            mask |= 1 << reg;
    }

    return mask;
}
#endif

#if VERSION >= VERSION_GC_1_3
int fn_004e9360(void)
{
    int i;
    RegClass13 rclass;
    int count;

    count = 0;
    for (rclass = 0; rclass < RegClass13_Max; rclass++) {
        for (i = 0; data_005e6574[rclass] > i; i++)
            count++;
    }
    return count;
}
#endif

#if VERSION >= VERSION_GC_1_3
extern SInt32 data_005e6878;
extern SInt32 data_005e6a7c[];
extern SInt32 data_005e66a8[];
extern SInt32 data_005e5fc8[];

void fn_004fca40(void)
{
    RegClass13 rclass;

    if (data_005e6878)
        return;
    for (rclass = 0; rclass < RegClass13_Max; rclass++) {
        int r = data_005e6a7c[rclass];
        if (r > data_005e66a8[rclass])
            data_005e66a8[rclass] = r;
        if (r > 256)
            data_005e6a7c[rclass] = data_005e5fc8[rclass];
    }
}
#endif

#if VERSION >= VERSION_GC_1_3
/* RegisterInfo.c (1.3): the reference's retain_register, retain_GPR_pair and assign_GPR_pair, with
 * Registers_GetVarInfo (Registers_GetInfo, out of line) inlined. */
extern SInt32 data_005e6878;
extern SInt32 data_005e6a7c[];
extern SInt32 data_005e6c00[];

inline VarInfo *Registers_GetNewVarInfo(void)
{
    VarInfo *vi = (VarInfo *)galloc(sizeof(VarInfo));
    memclrw(vi, sizeof(VarInfo));
    return vi;
}

inline VarInfo *Registers_GetVarInfo(Object *obj)
{
    switch (obj->datatype) {
        case DDATA:
            if (!obj->u.data.info)
                obj->u.data.info = Registers_GetNewVarInfo();
            return obj->u.data.info;
        case DLOCAL:
            if (!obj->u.var.info)
                CError_Internal(REGINFO_FILE, 647);
            return obj->u.var.info;
        case DABSOLUTE:
            if (!obj->u.data.info)
                obj->u.data.info = Registers_GetNewVarInfo();
            return obj->u.data.info;
        default:
            CError_Internal(REGINFO_FILE, 660);
            return NULL;
    }
}

extern void PPCError_ReportError(SInt32 code, ...);

/* the first nonvolatile register of a class, after binding the variables the source bound to its registers */
int fn_004cf6d0(RegClass13 rclass)
{
    RegisterBinding13 *binding;
    Object *obj;
    VarInfo *vi;
    int first;
    int base;
    int result;
    int count;
    int i;
    int used[32];

    first = fn_004cfad0(rclass);
    result = first;
    if (data_005e713d)
        return result;

    base = first;
    for (i = 0; i < 32; i++)
        used[i] = 0;

    for (binding = data_005e686c; binding; binding = binding->next) {
        if (rclass == binding->rclass) {
            used[binding->reg - base] = binding->reg;
            if (binding->reg >= result)
                result = binding->reg + 1;
            obj = binding->object;
            if (obj && obj->otype == OT_OBJECT) {
                if (!(Registers_GetVarInfo(obj)->reg == 0))
                    CError_Internal(REGINFO_FILE, 279);
                if (data_005e2bf0[binding->rclass][binding->reg] == 0) {
                    data_005e2bf0[binding->rclass][binding->reg] = 1;
                    vi = Registers_GetVarInfo(obj);
                    vi->flags |= 2;
                    vi->reg = binding->reg;
                    vi->regHi = 0;
                    vi->rclass = binding->rclass;
                } else {
                    char name[2];
                    name[0] = fn_004cfa90(binding->rclass);
                    name[1] = 0;
                    PPCError_ReportError(0xca, obj, name, binding->reg);
                }
            } else {
                PPCError_ReportError(0xcc, binding->name->name);
            }
        }
    }

    count = 0;
    for (i = 0; i < data_005e6cc0[rclass]; i++) {
        if (!used[i]) {
            for (i++; i < data_005e6cc0[rclass]; i++) {
                if (used[i]) {
                    char name[2];
                    name[0] = fn_004cfa90(rclass);
                    name[1] = 0;
                    PPCError_ReportError(0xc7, name, used[i] - 1);
                    count++;
                    break;
                }
            }
        }
    }

    if (count)
        return first;
    return result;
}

/* 1.3 floating-point types: a long double (integral 0x13) and any other one */
#define IS_TYPE_FLOAT_LONGDOUBLE(ty) ((ty)->type == TYPEFLOAT && TYPE_INTEGRAL(ty)->integral == 0x13)
#define IS_TYPE_FLOAT_NOT_LONGDOUBLE(ty) ((ty)->type == TYPEFLOAT && !IS_TYPE_FLOAT_LONGDOUBLE(ty))

extern void fn_004fcba0(Object *obj, RegClass13 rclass);

/* the reference's assign_register_by_type; a float goes to GPRs under copts.operandsDebug */
void Registers_AllocateGPRPair(Object *obj)
{
    VarInfo *vi;
    Type *ty;
    Boolean flag;

    ty = obj->type;
    vi = Registers_GetVarInfo(obj);
    flag = 0;
    vi->rclass = RegClass13_Max;
    vi->reg = 0;
    vi->regHi = 0;

    if ((ty->type == TYPEINT) || (ty->type == TYPEENUM) ||
        ((ty->type == TYPEPOINTER || ty->type == TYPEARRAY) && (ty->type != TYPEARRAY)) ||
        ((ty->type == TYPEMEMBERPOINTER) && (ty->size == 4U)) ||
        (copts.operandsDebug && IS_TYPE_FLOAT_NOT_LONGDOUBLE(ty))) {
        if ((((ty->type == TYPEINT) || (ty->type == TYPEENUM)) && (ty->size == 8)) ||
            (copts.operandsDebug && IS_TYPE_FLOAT_NOT_LONGDOUBLE(ty) && ty->size != 4))
            flag = 1;
        vi->rclass = RegClass13_GPR;
    } else if (IS_TYPE_FLOAT_NOT_LONGDOUBLE(ty) || IS_TYPE_FLOAT_LONGDOUBLE(ty)) {
        vi->rclass = RegClass13_FPR;
    } else if ((ty->type == TYPESTRUCT) && ((int)TYPE_STRUCT(ty)->stype >= 4) && ((int)TYPE_STRUCT(ty)->stype <= 14)) {
        vi->rclass = RegClass13_VR;
    } else {
        return;
    }

    if (vi->rclass < RegClass13_Max) {
        if (flag) {
            if (!(vi->rclass == RegClass13_GPR))
                CError_Internal(REGINFO_FILE, 520);
            if (data_005e6c00[vi->rclass] > 1)
                Registers_BindVR(obj);
        } else {
            if (data_005e6c00[vi->rclass] > 0)
                fn_004fcba0(obj, vi->rclass);
        }
    }
}

extern int fn_004fcac0(RegClass13 rclass);

#pragma defer_codegen off
void Registers_BindVR(Object *obj)
{
    short reg;
    short regHi;
    VarInfo *vi;

    vi = Registers_GetVarInfo(obj);
    if (data_005e6878) {
        reg = data_005e6a7c[RegClass13_GPR]++;
        regHi = data_005e6a7c[RegClass13_GPR]++;
    } else {
        if (!(data_005e6c00[RegClass13_GPR] >= 2))
            CError_Internal(REGINFO_FILE, 554);
        reg = fn_004fcac0(RegClass13_GPR);
        regHi = fn_004fcac0(RegClass13_GPR);
        Registers_BindGPRPair(obj, reg, regHi);
    }

    vi->rclass = RegClass13_GPR;
    if (reg > 0 && regHi > 0) {
        vi->flags |= 2 | 4;
        vi->reg = reg;
        vi->regHi = regHi;
    } else {
        CError_Internal(REGINFO_FILE, 567);
    }
}
#pragma defer_codegen reset
#endif

#if VERSION >= VERSION_GC_1_3
int fn_004fcac0(RegClass13 rclass)
{
    int best;
    int tmp;

    best = -1;
    while (data_005e674c[rclass] < data_005e6794[rclass]) {
        if (data_005e2bf0[rclass][tmp = data_005e0ef0[rclass][data_005e674c[rclass]++]] == 0) {
            best = tmp;
            break;
        }
    }

    return best;
}
#endif

#if VERSION >= VERSION_GC_1_3
extern char *data_005e6884[5];
extern char *data_005e6c5c[5];

void fn_004cf5b0(void)
{
    data_005e6884[0] = "SPR";
    data_005e6c5c[0] = "spr%ld";
    data_005e6884[1] = "CRFIELD";
    data_005e6c5c[1] = "cr%ld";
    data_005e6884[2] = "VR";
    data_005e6c5c[2] = "vr%ld";
    data_005e6884[3] = "FPR";
    data_005e6c5c[3] = "f%ld";
    data_005e6884[4] = "GPR";
    data_005e6c5c[4] = "r%ld";
}

extern char *data_005e5988[5][32];
extern SInt32 data_005e6574[];
extern SInt32 data_005e6778[];
extern SInt32 data_005e6878;
extern SInt32 data_005e6c00[];
extern SInt16 data_005e70e0;
extern SInt16 data_005e6cfa;
extern SInt32 data_00588224;

/* the reference's init_target_registers */
void fn_004cf2c0(void)
{
    RegClass13 rclass;
    int reg;
    int end;

    static int last_nonvolatile_reg[] = {3, 5, 31, 31, 31};
    static int nonvol_reserve[] = {0, 0, 0, 4, 3};

    for (rclass = 0; rclass < RegClass13_Max; rclass++) {
        for (reg = 0; reg < 32; reg++)
            data_005e5988[rclass][reg] = NULL;
    }

    data_005e5988[0][0] = "XER";
    data_005e5988[0][1] = "LR";
    data_005e5988[0][2] = "CTR";
    data_005e5988[0][3] = "VRSAVE";
    data_005e5988[RegClass13_GPR][1] = "SP";
    data_005e5988[RegClass13_GPR][13] = "SDA";

    fn_004cf5b0();
    data_005e6778[0] = 4;
    data_005e6778[1] = 8;
    data_005e6778[2] = 32;
    data_005e6778[3] = 32;
    data_005e6778[4] = 32;
    data_005e2bf0[RegClass13_GPR][1] = 2;
    data_005e2bf0[RegClass13_GPR][2] = 2;
    data_005e2bf0[RegClass13_GPR][13] = 2;
    data_005e2bf0[1][5] = 2;

    for (rclass = 0; rclass < RegClass13_Max; rclass++) {
        data_005e6794[rclass] = 0;
        if (last_nonvolatile_reg[rclass] >= 0) {
            end = fn_004cf6d0(rclass);
            for (reg = last_nonvolatile_reg[rclass]; reg >= end; reg--) {
                if (data_005e2bf0[rclass][reg] == 0)
                    data_005e0ef0[rclass][data_005e6794[rclass]++] = reg;
            }
        }

        data_005e6c00[rclass] = data_005e6794[rclass] - nonvol_reserve[rclass];
        if (data_005e6c00[rclass] < 0)
            data_005e6c00[rclass] = 0;

        data_005e6574[rclass] = 0;
        for (reg = 0; reg < data_005e6778[rclass]; reg++) {
            if (reg < fn_004cfad0(rclass) || reg > last_nonvolatile_reg[rclass]) {
                if (data_005e2bf0[rclass][reg] == 0)
                    data_005e0bf0[rclass][data_005e6574[rclass]++] = reg;
            }
        }
    }

    data_005e70e0 = -1;
    data_005e6cfa = -1;
    data_005e6878 = (copts.deleteDeadInstructions > 0) && !data_00588224;
}
#endif

#if VERSION >= VERSION_GC_1_3
extern short high_word_offset;
extern short low_word_offset;
extern short returnRegHi;
extern short return_gpr_first;
extern short sfpe_right_operand_reg_hi;
extern short sfpe_right_operand_reg;

void fn_004cece0(void)
{
    if (copts.littleendian != 0) {
        high_word_offset = 4;
        low_word_offset = 0;
        returnRegHi = 4;
        return_gpr_first = 3;
        sfpe_right_operand_reg_hi = 6;
        sfpe_right_operand_reg = 5;
    } else {
        high_word_offset = 0;
        low_word_offset = 4;
        returnRegHi = 3;
        return_gpr_first = 4;
        sfpe_right_operand_reg_hi = 5;
        sfpe_right_operand_reg = 6;
    }
}
#endif

#if VERSION >= VERSION_GC_1_3
extern SInt32 data_005e6878;
extern SInt32 data_005e6a7c[];
extern SInt32 data_005e6794[];
extern SInt32 data_005e674c[];
extern SInt32 data_005e0ef0[5][32];

extern SInt32 gVirtualRegistersActive;
extern SInt32 data_005e6778[];
extern void fn_004cf2c0(void);

/* the reference's init_registers */
void fn_004fcc60(void)
{
    RegClass13 rclass;
    int j;

    for (rclass = 0; rclass < RegClass13_Max; rclass++) {
        for (j = 0; j < 32; j++)
            data_005e2bf0[rclass][j] = 0;
    }

    for (rclass = 0; rclass < RegClass13_Max; rclass++)
        data_005e674c[rclass] = 0;

    data_005e6878 = 1;

    fn_004cf2c0();
    for (rclass = 0; rclass < RegClass13_Max; rclass++)
        data_005e6a7c[rclass] = data_005e6778[rclass];

    gVirtualRegistersActive = 1;
}

/* the reference's assign_register_to_variable */
#pragma defer_codegen off
void fn_004fcba0(Object *obj, RegClass13 rclass)
{
    VarInfo *vi;
    int reg;

    vi = Registers_GetInfo(obj);
    if (data_005e6878) {
        reg = data_005e6a7c[rclass]++;
    } else {
        reg = fn_004fcac0(rclass);
        Registers_BindGPR(obj, rclass, reg);
    }

    vi->flags |= 2;
    vi->rclass = rclass;
    vi->reg = reg;
}
#pragma defer_codegen reset

/* RegisterInfo.c (1.3), defined after fn_004fcba0, which calls Registers_BindGPR out of line */
void Registers_BindGPR(Object *obj, RegClass13 rclass, short reg)
{
    VarInfo *vi;

    if (!(reg < 32))
        CError_Internal(REGINFO_FILE, 95);

    if (data_005e2bf0[rclass][reg] == 0) {
        data_005e6c00[rclass]--;
        data_005e2bf0[rclass][reg] = 1;
        if (reg == data_005e0ef0[rclass][data_005e674c[rclass]])
            data_005e674c[rclass]++;
    }

    if (obj) {
        vi = Registers_GetVarInfo(obj);
        vi->rclass = rclass;
        vi->flags |= 2;
        vi->reg = reg;
    }
}

void Registers_BindGPRPair(Object *obj, short reg, short regHi)
{
    VarInfo *vi;

    Registers_BindGPR(NULL, RegClass13_GPR, reg);
    Registers_BindGPR(NULL, RegClass13_GPR, regHi);

    if (obj) {
        vi = Registers_GetVarInfo(obj);
        vi->rclass = RegClass13_GPR;
        vi->flags |= 2 | 4;
        vi->reg = reg;
        vi->regHi = regHi;
    }
}

extern SInt32 data_005e6778[];

/* the reference's update_asm_nonvolatile_registers */
void fn_004cec70(void)
{
    RegClass13 rclass;
    int i;
    int reg;

    for (rclass = 0; rclass < RegClass13_Max; rclass++) {
        reg = data_005e6794[rclass];
        for (i = data_005e6794[rclass] - 1; i >= 0; i--) {
            if (data_005e2bf0[rclass][data_005e0ef0[rclass][i]] == 1)
                break;
            reg--;
        }
        if (reg > data_005e674c[rclass])
            data_005e674c[rclass] = reg;
    }
}

/* the reference's asm_used_register */
void fn_004cfe50(RegClass13 rclass, short reg)
{
    long i;

    if (reg >= data_005e6778[rclass])
        return;
    if (data_005e2bf0[rclass][reg] == 0) {
        if (reg == data_005e0ef0[rclass][data_005e674c[rclass]]) {
            if (data_005e6c00[rclass] > 0)
                data_005e6c00[rclass]--;
            data_005e2bf0[rclass][reg] = 1;
            data_005e674c[rclass]++;
        } else {
            for (i = data_005e674c[rclass]; i < data_005e6794[rclass]; i++) {
                if (reg == data_005e0ef0[rclass][i]) {
                    data_005e2bf0[rclass][reg] = 1;
                    if (data_005e6c00[rclass] > 0)
                        data_005e6c00[rclass]--;
                }
            }
        }
    }
}
#endif
