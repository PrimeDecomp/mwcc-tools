/* Wii 1.0RC1 memory transfer widths. */
#include "rc1_pcode.h"

#pragma pack(push, 2)
typedef struct OpcodeInfo {
    unsigned char header[10];
    uint32_t flags;
    unsigned char operands[8];
} OpcodeInfo;
#pragma pack(pop)

_Static_assert(sizeof(OpcodeInfo) == 22, "RC1 opcode table stride");
_Static_assert(offsetof(OpcodeInfo, flags) == 10, "RC1 opcode table flags");

extern OpcodeInfo opcodeinfo[];
extern short RC1_processor;
extern const char RC1_PCodeInfo_source_file[];
extern void CError_Internal(const char* source_file, int line);

enum { GekkoProcessor = 0x16, GQR0 = 0x390 };

enum {
    PC_LBZ = 0x15,
    PC_LBZU = 0x16,
    PC_LBZX = 0x17,
    PC_LBZUX = 0x18,
    PC_LHZ = 0x19,
    PC_LHZU = 0x1a,
    PC_LHZX = 0x1b,
    PC_LHZUX = 0x1c,
    PC_LHA = 0x1d,
    PC_LHAU = 0x1e,
    PC_LHAX = 0x1f,
    PC_LHAUX = 0x20,
    PC_LHBRX = 0x21,
    PC_LWZ = 0x22,
    PC_LWZU = 0x23,
    PC_LWZX = 0x24,
    PC_LWZUX = 0x25,
    PC_LWBRX = 0x26,
    PC_LMW = 0x27,
    PC_STB = 0x28,
    PC_STBU = 0x29,
    PC_STBX = 0x2a,
    PC_STBUX = 0x2b,
    PC_STH = 0x2c,
    PC_STHU = 0x2d,
    PC_STHX = 0x2e,
    PC_STHUX = 0x2f,
    PC_STHBRX = 0x30,
    PC_STW = 0x31,
    PC_STWU = 0x32,
    PC_STWX = 0x33,
    PC_STWUX = 0x34,
    PC_STWBRX = 0x35,
    PC_STMW = 0x36,
    PC_LFS = 0x8e,
    PC_LFSU = 0x8f,
    PC_LFSX = 0x90,
    PC_LFSUX = 0x91,
    PC_LFD = 0x92,
    PC_LFDU = 0x93,
    PC_LFDX = 0x94,
    PC_LFDUX = 0x95,
    PC_STFS = 0x96,
    PC_STFSU = 0x97,
    PC_STFSX = 0x98,
    PC_STFSUX = 0x99,
    PC_STFD = 0x9a,
    PC_STFDU = 0x9b,
    PC_STFDX = 0x9c,
    PC_STFDUX = 0x9d,
    PC_LWARX = 0xba,
    PC_LSWI = 0xbb,
    PC_LSWX = 0xbc,
    PC_STFIWX = 0xbd,
    PC_STSWI = 0xbe,
    PC_STSWX = 0xbf,
    PC_STWCX = 0xc0,
    PC_ECIWX = 0xc1,
    PC_ECOWX = 0xc2,
    PC_MFROM = 0xdb,
    PC_LVEBX = 0xf4,
    PC_LVEHX = 0xf5,
    PC_LVEWX = 0xf6,
    PC_LVSL = 0xf7,
    PC_LVSR = 0xf8,
    PC_LVX = 0xf9,
    PC_LVXL = 0xfa,
    PC_STVEBX = 0xfb,
    PC_STVEHX = 0xfc,
    PC_STVEWX = 0xfd,
    PC_STVX = 0xfe,
    PC_STVXL = 0xff,
    PC_PSQ_L = 0x192,
    PC_PSQ_LU = 0x193,
    PC_PSQ_LX = 0x194,
    PC_PSQ_LUX = 0x195,
    PC_PSQ_ST = 0x196,
    PC_PSQ_STU = 0x197,
    PC_PSQ_STX = 0x198,
    PC_PSQ_STUX = 0x199,
    PC_EVLDD = 0x1e0,
    PC_EVLDDX = 0x1e1,
    PC_EVLDW = 0x1e2,
    PC_EVLDWX = 0x1e3,
    PC_EVLDH = 0x1e4,
    PC_EVLDHX = 0x1e5,
    PC_EVLWHE = 0x1e6,
    PC_EVLWHEX = 0x1e7,
    PC_EVLWHOU = 0x1e8,
    PC_EVLWHOUX = 0x1e9,
    PC_EVLWHOS = 0x1ea,
    PC_EVLWHOSX = 0x1eb,
    PC_EVLWWSPLAT = 0x1ec,
    PC_EVLWWSPLATX = 0x1ed,
    PC_EVLWHSPLAT = 0x1ee,
    PC_EVLWHSPLATX = 0x1ef,
    PC_EVLHHESPLAT = 0x1f0,
    PC_EVLHHESPLATX = 0x1f1,
    PC_EVLHHOUSPLAT = 0x1f2,
    PC_EVLHHOUSPLATX = 0x1f3,
    PC_EVLHHOSSPLAT = 0x1f4,
    PC_EVLHHOSSPLATX = 0x1f5,
    PC_EVSTDD = 0x1f6,
    PC_EVSTDDX = 0x1f7,
    PC_EVSTDW = 0x1f8,
    PC_EVSTDWX = 0x1f9,
    PC_EVSTDH = 0x1fa,
    PC_EVSTDHX = 0x1fb,
    PC_EVSTWWE = 0x1fc,
    PC_EVSTWWEX = 0x1fd,
    PC_EVSTWWO = 0x1fe,
    PC_EVSTWWOX = 0x1ff,
    PC_EVSTWHE = 0x200,
    PC_EVSTWHEX = 0x201,
    PC_EVSTWHO = 0x202,
    PC_EVSTWHOX = 0x203,
    PC_E_LBZ = 0x2d9,
    PC_E_LBZU = 0x2da,
    PC_E_LHZ = 0x2db,
    PC_E_LHZU = 0x2dc,
    PC_E_LHA = 0x2dd,
    PC_E_LHAU = 0x2de,
    PC_E_LWZ = 0x2df,
    PC_E_LWZU = 0x2e0,
    PC_E_LMW = 0x2e1,
    PC_E_STB = 0x2e2,
    PC_E_STBU = 0x2e3,
    PC_E_STH = 0x2e4,
    PC_E_STHU = 0x2e5,
    PC_E_STW = 0x2e6,
    PC_E_STWU = 0x2e7,
    PC_E_STMW = 0x2e8,
    PC_SE_LBZ = 0x31e,
    PC_SE_LHZ = 0x31f,
    PC_SE_LWZ = 0x321,
    PC_SE_STB = 0x336,
    PC_SE_STH = 0x337,
    PC_SE_STW = 0x338,
    PC_LSCBX = 0x382,
};

int nbytes_loaded_or_stored_by(const PCode* instruction) {
    int16_t opcode = (int16_t)instruction->opcode;
    if (opcodeinfo[opcode].flags & (IsLoad | IsStore)) {
        switch (opcode) {
        case PC_LBZ:
        case PC_LBZU:
        case PC_LBZX:
        case PC_LBZUX:
        case PC_STB:
        case PC_STBU:
        case PC_STBX:
        case PC_STBUX:
        case PC_E_LBZ:
        case PC_E_LBZU:
        case PC_E_STB:
        case PC_E_STBU:
        case PC_SE_LBZ:
        case PC_SE_STB:
            return 1;
        case PC_LHZ:
        case PC_LHZU:
        case PC_LHZX:
        case PC_LHZUX:
        case PC_LHA:
        case PC_LHAU:
        case PC_LHAX:
        case PC_LHAUX:
        case PC_LHBRX:
        case PC_STH:
        case PC_STHU:
        case PC_STHX:
        case PC_STHUX:
        case PC_STHBRX:
        case PC_E_LHZ:
        case PC_E_LHZU:
        case PC_E_LHA:
        case PC_E_LHAU:
        case PC_E_STH:
        case PC_E_STHU:
        case PC_SE_LHZ:
        case PC_SE_STH:
            return 2;
        case PC_LWZ:
        case PC_LWZU:
        case PC_LWZX:
        case PC_LWZUX:
        case PC_LWBRX:
        case PC_STW:
        case PC_STWU:
        case PC_STWX:
        case PC_STWUX:
        case PC_STWBRX:
        case PC_LFS:
        case PC_LFSU:
        case PC_LFSX:
        case PC_LFSUX:
        case PC_STFS:
        case PC_STFSU:
        case PC_STFSX:
        case PC_STFSUX:
        case PC_LWARX:
        case PC_STFIWX:
        case PC_STWCX:
        case PC_ECIWX:
        case PC_ECOWX:
        case PC_MFROM:
        case PC_E_LWZ:
        case PC_E_LWZU:
        case PC_E_STW:
        case PC_E_STWU:
        case PC_SE_LWZ:
        case PC_SE_STW:
        case PC_LSCBX:
            return 4;
        case PC_LMW:
        case PC_STMW:
        case PC_E_LMW:
        case PC_E_STMW:
            if (instruction->args[0].kind == DirectRegister &&
                instruction->args[0].reg_class == GeneralRegister)
                return (32 - (short)instruction->args[0].value.reg.number) * 4;
            return 128;
        case PC_LFD:
        case PC_LFDU:
        case PC_LFDX:
        case PC_LFDUX:
        case PC_STFD:
        case PC_STFDU:
        case PC_STFDX:
        case PC_STFDUX:
            return 8;
        case PC_LSWI:
        case PC_STSWI:
            return (int)instruction->args[2].value.immediate.displacement;
        case PC_LSWX:
        case PC_STSWX:
            return 128;
        case PC_LVEBX:
        case PC_STVEBX:
            return 1;
        case PC_LVEHX:
        case PC_STVEHX:
            return 2;
        case PC_LVEWX:
        case PC_STVEWX:
            return 4;
        case PC_LVSL:
        case PC_LVSR:
        case PC_LVX:
        case PC_LVXL:
        case PC_STVX:
        case PC_STVXL:
            return 16;
        case PC_EVLDD:
        case PC_EVLDDX:
        case PC_EVLDW:
        case PC_EVLDWX:
        case PC_EVLDH:
        case PC_EVLDHX:
        case PC_EVSTDD:
        case PC_EVSTDDX:
        case PC_EVSTDW:
        case PC_EVSTDWX:
        case PC_EVSTDH:
        case PC_EVSTDHX:
            if (RC1_processor == GekkoProcessor)
                CError_Internal(RC1_PCodeInfo_source_file, 3261);
            else
                return 8;
            /* Fall through after the diagnostic. */
        case PC_EVLWHE:
        case PC_EVLWHEX:
        case PC_EVLWHOU:
        case PC_EVLWHOUX:
        case PC_EVLWHOS:
        case PC_EVLWHOSX:
        case PC_EVLWWSPLAT:
        case PC_EVLWWSPLATX:
        case PC_EVLWHSPLAT:
        case PC_EVLWHSPLATX:
        case PC_EVSTWWE:
        case PC_EVSTWWEX:
        case PC_EVSTWWO:
        case PC_EVSTWWOX:
        case PC_EVSTWHE:
        case PC_EVSTWHEX:
        case PC_EVSTWHO:
        case PC_EVSTWHOX:
            if (RC1_processor == GekkoProcessor)
                CError_Internal(RC1_PCodeInfo_source_file, 3283);
            else
                return 4;
            /* Fall through after the diagnostic. */
        case PC_EVLHHESPLAT:
        case PC_EVLHHESPLATX:
        case PC_EVLHHOUSPLAT:
        case PC_EVLHHOUSPLATX:
        case PC_EVLHHOSSPLAT:
        case PC_EVLHHOSSPLATX:
            if (RC1_processor == GekkoProcessor)
                CError_Internal(RC1_PCodeInfo_source_file, 3293);
            else
                return 2;
            /* Fall through after the diagnostic. */
        case PC_PSQ_L:
        case PC_PSQ_LU:
        case PC_PSQ_LX:
        case PC_PSQ_LUX:
        case PC_PSQ_ST:
        case PC_PSQ_STU:
        case PC_PSQ_STX:
        case PC_PSQ_STUX:
            if (RC1_processor == GekkoProcessor) {
                if (instruction->args[4].value.reg.number == GQR0 &&
                    instruction->args[3].value.immediate.displacement == 1)
                    return 4;
                return 8;
            }
            CError_Internal(RC1_PCodeInfo_source_file, 3323);
            break;
        default:
            CError_Internal(RC1_PCodeInfo_source_file, 3327);
            break;
        }
    }
    CError_Internal(RC1_PCodeInfo_source_file, 3330);
    return 0;
}
