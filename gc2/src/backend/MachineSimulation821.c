#include "compiler/Scheduler.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"

/* Pro7's static names, prefixed: row names are global, and every machine model has these */
#define latency m821_latency
#define initialize m821_initialize
#define can_issue m821_can_issue
#define issue m821_issue
#define advance_clock m821_advance_clock
#define serializes m821_serializes

// https://www.nxp.com/docs/en/user-guide/MPC821UM.pdf

typedef enum Stage {
    BranchUnit,
    Stage1,
    Stage2,
    LSU1,
    LSU2,
    CRUnit,
    NumStages,
    Stage7
} Stage;

static struct {
    // the instruction currently in this pipeline stage
    PCode* instr;

    // how many cycles are left for this instruction to finish
    int remaining;
} pipeline[NumStages];

enum {
    MaxEntries = 6
};

static struct {
    // how many entries remain unused in the queue
    unsigned int free;

    // how many entries are currently used in the queue
    unsigned int used;

    // the index of the next instruction that will be retired
    unsigned int nextToRetire;

    // the index of the next free slot that will be used when an instruction is dispatched
    unsigned int nextFreeSlot;

    // circular array of entries in the completion queue
    struct {
        PCode* instr;
        int completed;
    } entries[MaxEntries];
} completionbuffers;

static int latency(PCode* instr);
static void initialize(void);
static int can_issue(PCode* instr);
static void issue(PCode* instr);
static void advance_clock(void);
static int serializes(PCode* instr);

// clang-format off
MachineInfo machine821 = {
    1,
    FALSE,
    0,
    &latency,
    &initialize,
    &can_issue,
    &issue,
    &advance_clock,
    &serializes,
    &default_uses_vpermute_unit
};
// clang-format on

static struct {
    // the initial stage for this instruction
    Stage stage;

    // the total amount of cycles required by this instruction
    char latency;

    // how long it takes to finish each stage
    char cycles[3];

    // does this instruction serialise?
    char serializes;
    // clang-format off
} instruction_timing[OPCODE_MAX] = {
    BranchUnit,  0,   0,   0,  0,  0, // PC_B
    BranchUnit,  0,   0,   0,  0,  0, // PC_BL
    BranchUnit,  0,   0,   0,  0,  0, // PC_BC
    BranchUnit,  0,   0,   0,  0,  0, // PC_BCLR
    BranchUnit,  0,   0,   0,  0,  0, // PC_BCCTR
    BranchUnit,  0,   0,   0,  0,  0, // PC_BT
    BranchUnit,  0,   0,   0,  0,  0, // PC_BTLR
    BranchUnit,  0,   0,   0,  0,  0, // PC_BTCTR
    BranchUnit,  0,   0,   0,  0,  0, // PC_BF
    BranchUnit,  0,   0,   0,  0,  0, // PC_BFLR
    BranchUnit,  0,   0,   0,  0,  0, // PC_BFCTR
    BranchUnit,  0,   0,   0,  0,  0, // PC_BDNZ
    BranchUnit,  0,   0,   0,  0,  0, // PC_BDNZT
    BranchUnit,  0,   0,   0,  0,  0, // PC_BDNZF
    BranchUnit,  0,   0,   0,  0,  0, // PC_BDZ
    BranchUnit,  0,   0,   0,  0,  0, // PC_BDZT
    BranchUnit,  0,   0,   0,  0,  0, // PC_BDZF
    BranchUnit,  0,   0,   0,  0,  0, // PC_BLR
    BranchUnit,  0,   0,   0,  0,  0, // PC_BCTR
    BranchUnit,  0,   0,   0,  0,  0, // PC_BCTRL
    BranchUnit,  0,   0,   0,  0,  0, // PC_BLRL
    BranchUnit,  0,   0,   0,  0,  0, // PC_LBZ
    LSU1,        2,   1,   1,  0,  0, // PC_LBZU
    LSU1,        2,   1,   1,  0,  0, // PC_LBZX
    LSU1,        2,   1,   1,  0,  0, // PC_LBZUX
    LSU1,        2,   1,   1,  0,  0, // PC_LHZ
    LSU1,        2,   1,   1,  0,  0, // PC_LHZU
    LSU1,        2,   1,   1,  0,  0, // PC_LHZX
    LSU1,        2,   1,   1,  0,  0, // PC_LHZUX
    LSU1,        2,   1,   1,  0,  0, // PC_LHA
    LSU1,        2,   1,   1,  0,  0, // PC_LHAU
    LSU1,        2,   1,   1,  0,  0, // PC_LHAX
    LSU1,        2,   1,   1,  0,  0, // PC_LHAUX
    LSU1,        2,   1,   1,  0,  0, // PC_LHBRX
    LSU1,        2,   1,   1,  0,  0, // PC_LWZ
    LSU1,        2,   1,   1,  0,  0, // PC_LWZU
    LSU1,        2,   1,   1,  0,  0, // PC_LWZX
    LSU1,        2,   1,   1,  0,  0, // PC_LWZUX
    LSU1,        2,   1,   1,  0,  0, // PC_LWBRX
    LSU1,        2,   1,   1,  0,  0, // PC_LMW
    LSU1,        2,   1,   1,  0,  0, // PC_STB
    LSU1,        2,   1,   1,  0,  0, // PC_STBU
    LSU1,        2,   1,   1,  0,  0, // PC_STBX
    LSU1,        2,   1,   1,  0,  0, // PC_STBUX
    LSU1,        2,   1,   1,  0,  0, // PC_STH
    LSU1,        2,   1,   1,  0,  0, // PC_STHU
    LSU1,        2,   1,   1,  0,  0, // PC_STHX
    LSU1,        2,   1,   1,  0,  0, // PC_STHUX
    LSU1,        2,   1,   1,  0,  0, // PC_STHBRX
    LSU1,        2,   1,   1,  0,  0, // PC_STW
    LSU1,        2,   1,   1,  0,  0, // PC_STWU
    LSU1,        2,   1,   1,  0,  0, // PC_STWX
    LSU1,        2,   1,   1,  0,  0, // PC_STWUX
    LSU1,        2,   1,   1,  0,  0, // PC_STWBRX
    LSU1,        2,   1,   1,  0,  0, // PC_STMW
    LSU1,        2,   1,   1,  0,  0, // PC_DCBF
    LSU1,        2,   1,   1,  0,  0, // PC_DCBST
    LSU1,        2,   1,   1,  0,  0, // PC_DCBT
    LSU1,        2,   1,   1,  0,  0, // PC_DCBTST
    LSU1,        2,   1,   1,  0,  0, // PC_DCBZ
    LSU1,        2,   1,   1,  0,  0, // PC_ADD
    Stage1,      1,   1,   0,  0,  0, // PC_ADDC
    Stage1,      1,   1,   0,  0,  0, // PC_ADDE
    Stage1,      1,   1,   0,  0,  0, // PC_ADDI
    Stage1,      1,   1,   0,  0,  0, // PC_ADDIC
    Stage1,      1,   1,   0,  0,  0, // PC_ADDICR
    Stage1,      1,   1,   0,  0,  0, // PC_ADDIS
    Stage1,      1,   1,   0,  0,  0, // PC_ADDME
    Stage1,      1,   1,   0,  0,  0, // PC_ADDZE
    Stage1,      1,   1,   0,  0,  0, // PC_DIVW
    Stage1,      37,  37,  0,  0,  0, // PC_DIVWU
    Stage1,      37,  37,  0,  0,  0, // PC_MULHW
    Stage1,      5,   5,   0,  0,  0, // PC_MULHWU
    Stage1,      5,   5,   0,  0,  0, // PC_MULLI
    Stage1,      3,   3,   0,  0,  0, // PC_MULLW
    Stage1,      5,   5,   0,  0,  0, // PC_NEG
    Stage1,      1,   1,   0,  0,  0, // PC_SUBF
    Stage1,      1,   1,   0,  0,  0, // PC_SUBFC
    Stage1,      1,   1,   0,  0,  0, // PC_SUBFE
    Stage1,      1,   1,   0,  0,  0, // PC_SUBFIC
    Stage1,      1,   1,   0,  0,  0, // PC_SUBFME
    Stage1,      1,   1,   0,  0,  0, // PC_SUBFZE
    Stage1,      1,   1,   0,  0,  0, // PC_CMPI
    Stage1,      3,   1,   0,  0,  0, // PC_CMP
    Stage1,      3,   1,   0,  0,  0, // PC_CMPLI
    Stage1,      3,   1,   0,  0,  0, // PC_CMPL
    Stage1,      3,   1,   0,  0,  0, // PC_ANDI
    Stage1,      1,   1,   0,  0,  0, // PC_ANDIS
    Stage1,      1,   1,   0,  0,  0, // PC_ORI
    Stage1,      1,   1,   0,  0,  0, // PC_ORIS
    Stage1,      1,   1,   0,  0,  0, // PC_XORI
    Stage1,      1,   1,   0,  0,  0, // PC_XORIS
    Stage1,      1,   1,   0,  0,  0, // PC_AND
    Stage1,      1,   1,   0,  0,  0, // PC_OR
    Stage1,      1,   1,   0,  0,  0, // PC_XOR
    Stage1,      1,   1,   0,  0,  0, // PC_NAND
    Stage1,      1,   1,   0,  0,  0, // PC_NOR
    Stage1,      1,   1,   0,  0,  0, // PC_EQV
    Stage1,      1,   1,   0,  0,  0, // PC_ANDC
    Stage1,      1,   1,   0,  0,  0, // PC_ORC
    Stage1,      1,   1,   0,  0,  0, // PC_EXTSB
    Stage1,      1,   1,   0,  0,  0, // PC_EXTSH
    Stage1,      1,   1,   0,  0,  0, // PC_CNTLZW
    Stage1,      1,   1,   0,  0,  0, // PC_RLWINM
    Stage1,      1,   1,   0,  0,  0, // PC_RLWNM
    Stage1,      1,   1,   0,  0,  0, // PC_RLWIMI
    Stage1,      1,   1,   0,  0,  0, // PC_SLW
    Stage1,      1,   1,   0,  0,  0, // PC_SRW
    Stage1,      1,   1,   0,  0,  0, // PC_SRAWI
    Stage1,      1,   1,   0,  0,  0, // PC_SRAW
    Stage1,      1,   1,   0,  0,  0, // PC_CRAND
    CRUnit,      1,   1,   0,  0,  0, // PC_CRANDC
    CRUnit,      1,   1,   0,  0,  0, // PC_CREQV
    CRUnit,      1,   1,   0,  0,  0, // PC_CRNAND
    CRUnit,      1,   1,   0,  0,  0, // PC_CRNOR
    CRUnit,      1,   1,   0,  0,  0, // PC_CROR
    CRUnit,      1,   1,   0,  0,  0, // PC_CRORC
    CRUnit,      1,   1,   0,  0,  0, // PC_CRXOR
    CRUnit,      1,   1,   0,  0,  0, // PC_MCRF
    CRUnit,      1,   1,   0,  0,  0, // PC_MTXER
    Stage1,      1,   1,   0,  0,  0, // PC_MTCTR
    BranchUnit,  2,   2,   0,  0,  0, // PC_MTLR
    BranchUnit,  2,   2,   0,  0,  0, // PC_MTCRF
    Stage1,      1,   1,   0,  0,  0, // PC_MTMSR
    Stage1,      1,   1,   0,  0,  0, // PC_MTSPR
    Stage1,      1,   1,   0,  0,  0, // PC_MFMSR
    Stage1,      1,   1,   0,  0,  0, // PC_MFSPR
    Stage1,      1,   1,   0,  0,  0, // PC_MFXER
    Stage7,      3,   1,   1,  1,  0, // PC_MFCTR
    Stage7,      3,   1,   1,  1,  0, // PC_MFLR
    Stage1,      1,   1,   0,  0,  0, // PC_MFCR
    Stage1,      1,   1,   0,  0,  0, // PC_MFFS
    Stage1,      1,   1,   0,  0,  0, // PC_MTFSF
    Stage1,      1,   1,   0,  0,  0, // PC_EIEIO
    Stage1,      1,   1,   0,  0,  0, // PC_ISYNC
    Stage1,      1,   1,   0,  0,  0, // PC_SYNC
    Stage1,      1,   1,   0,  0,  0, // PC_RFI
    Stage1,      1,   1,   0,  0,  0, // PC_LI
    LSU1,        2,   1,   1,  0,  0, // PC_LIS
    LSU1,        2,   1,   1,  0,  0, // PC_MR
    LSU1,        2,   1,   1,  0,  0, // PC_NOP
    LSU1,        2,   1,   1,  0,  0, // PC_NOT
    LSU1,        2,   1,   1,  0,  0, // PC_LFS
    LSU1,        2,   1,   1,  0,  0, // PC_LFSU
    LSU1,        2,   1,   1,  0,  0, // PC_LFSX
    LSU1,        2,   1,   1,  0,  0, // PC_LFSUX
    LSU1,        2,   1,   1,  0,  0, // PC_LFD
    LSU1,        2,   1,   1,  0,  0, // PC_LFDU
    LSU1,        2,   1,   1,  0,  0, // PC_LFDX
    LSU1,        2,   1,   1,  0,  0, // PC_LFDUX
    LSU1,        2,   1,   1,  0,  0, // PC_STFS
    LSU1,        2,   1,   1,  0,  0, // PC_STFSU
    LSU1,        2,   1,   1,  0,  0, // PC_STFSX
    LSU1,        2,   1,   1,  0,  0, // PC_STFSUX
    Stage7,      3,   1,   1,  1,  0, // PC_STFD
    Stage7,      3,   1,   1,  1,  0, // PC_STFDU
    Stage7,      3,   1,   1,  1,  0, // PC_STFDX
    Stage7,      3,   1,   1,  1,  0, // PC_STFDUX
    Stage7,      3,   1,   1,  1,  0, // PC_FMR
    Stage7,      3,   1,   1,  1,  0, // PC_FABS
    Stage7,      3,   1,   1,  1,  0, // PC_FNEG
    Stage7,      3,   1,   1,  1,  0, // PC_FNABS
    Stage7,      4,   2,   1,  1,  0, // PC_FADD
    Stage7,      3,   1,   1,  1,  0, // PC_FADDS
    Stage7,      33,  33,  0,  0,  0, // PC_FSUB
    Stage7,      18,  18,  0,  0,  0, // PC_FSUBS
    Stage7,      4,   2,   1,  1,  0, // PC_FMUL
    Stage7,      3,   1,   1,  1,  0, // PC_FMULS
    Stage7,      4,   2,   1,  1,  0, // PC_FDIV
    Stage7,      3,   1,   1,  1,  0, // PC_FDIVS
    Stage7,      4,   2,   1,  1,  0, // PC_FMADD
    Stage7,      3,   1,   1,  1,  0, // PC_FMADDS
    Stage7,      4,   2,   1,  1,  0, // PC_FMSUB
    Stage7,      3,   1,   1,  1,  0, // PC_FMSUBS
    Stage7,      18,  18,  0,  0,  0, // PC_FNMADD
    Stage7,      3,   1,   1,  1,  0, // PC_FNMADDS
    Stage7,      3,   1,   1,  1,  0, // PC_FNMSUB
    Stage7,      3,   1,   1,  1,  0, // PC_FNMSUBS
    Stage7,      3,   1,   1,  1,  0, // PC_FRES
    Stage7,      3,   1,   1,  1,  0, // PC_FRSQRTE
    Stage7,      5,   1,   1,  1,  0, // PC_FSEL
    Stage7,      5,   1,   1,  1,  0, // PC_FRSP
    LSU1,        1,   0,   0,  0,  0, // PC_FCTIW
    LSU1,        1,   0,   0,  0,  0, // PC_FCTIWZ
    LSU1,        1,   0,   0,  0,  0, // PC_FCMPU
    LSU1,        1,   0,   0,  0,  0, // PC_FCMPO
    LSU1,        1,   0,   0,  0,  0, // PC_LWARX
    LSU1,        1,   0,   0,  0,  0, // PC_LSWI
    LSU1,        1,   0,   0,  0,  0, // PC_LSWX
    Stage1,      1,   0,   0,  0,  0, // PC_STFIWX
    Stage1,      1,   0,   0,  0,  0, // PC_STSWI
    Stage1,      1,   0,   0,  0,  0, // PC_STSWX
    Stage1,      1,   0,   0,  0,  0, // PC_STWCX
    Stage1,      1,   0,   0,  0,  0, // PC_ECIWX
    Stage1,      1,   0,   0,  0,  0, // PC_ECOWX
    Stage1,      1,   0,   0,  0,  0, // PC_DCBI
    Stage1,      1,   0,   0,  0,  0, // PC_ICBI
    Stage1,      1,   0,   0,  0,  0, // PC_MCRFS
    Stage1,      1,   0,   0,  0,  0, // PC_MCRXR
    Stage1,      1,   0,   0,  0,  0, // PC_MFTB
    Stage1,      1,   0,   0,  0,  0, // PC_MFSR
    Stage1,      1,   0,   0,  0,  0, // PC_MTSR
    Stage1,      1,   0,   0,  0,  0, // PC_MFSRIN
    Stage1,      1,   0,   0,  0,  0, // PC_MTSRIN
    Stage1,      1,   0,   0,  0,  0, // PC_MTFSB0
    Stage1,      1,   0,   0,  0,  0, // PC_MTFSB1
    Stage1,      1,   0,   0,  0,  0, // PC_MTFSFI
    Stage1,      1,   0,   0,  0,  1, // PC_SC
    Stage1,      1,   0,   0,  0,  1, // PC_FSQRT
    Stage1,      1,   0,   0,  0,  0, // PC_FSQRTS
    Stage1,      1,   0,   0,  0,  0, // PC_TLBIA
    Stage1,      1,   0,   0,  0,  0, // PC_TLBIE
    Stage1,      1,   0,   0,  0,  0, // PC_TLBLD
    Stage1,      1,   0,   0,  0,  0, // PC_TLBLI
    Stage1,      1,   0,   0,  0,  0, // PC_TLBSYNC
    Stage1,      1,   0,   0,  0,  0, // PC_TW
    Stage1,      1,   0,   0,  0,  1, // PC_TRAP
    Stage1,      1,   0,   0,  0,  1, // PC_TWI
    Stage1,      1,   0,   0,  0,  1, // PC_OPWORD
    Stage1,      1,   0,   0,  0,  1, // PC_MFROM
    Stage1,      1,   0,   0,  0,  0, // PC_DSA
    Stage1,      1,   0,   0,  0,  0, // PC_ESA
    Stage1,      1,   0,   0,  0,  0, // PC_DCCCI
    Stage1,      0,   0,   0,  0,  0, // PC_DCREAD
    Stage1,      0,   0,   0,  0,  0, // PC_ICBT
    Stage1,      0,   0,   0,  0,  0, // PC_ICCCI
    Stage1,      0,   0,   0,  0,  0, // PC_ICREAD
    Stage1,      0,   0,   0,  0,  0, // PC_RFCI
    Stage1,      0,   0,   0,  0,  0, // PC_TLBRE
    Stage1,      0,   0,   0,  0,  0, // PC_TLBSX
    Stage1,      0,   0,   0,  0,  0, // PC_TLBWE
    Stage1,      0,   0,   0,  0,  0, // PC_WRTEE
    Stage1,      0,   0,   0,  0,  0, // PC_WRTEEI
    Stage1,      0,   0,   0,  0,  0, // PC_MFDCR
    Stage1,      0,   0,   0,  0,  0, // PC_MTDCR
    Stage1,      0,   0,   0,  0,  0, // PC_DCBA
    Stage1,      0,   0,   0,  0,  0, // PC_ISELGT
    BranchUnit,  0,   0,   0,  0,  0, // PC_ISELEQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_ISELLT
    BranchUnit,  0,   0,   0,  0,  0, // PC_DSS
    BranchUnit,  0,   0,   0,  0,  0, // PC_DSSALL
    BranchUnit,  0,   0,   0,  0,  0, // PC_DST
    BranchUnit,  0,   0,   0,  0,  0, // PC_DSTT
    BranchUnit,  0,   0,   0,  0,  0, // PC_DSTST
    BranchUnit,  0,   0,   0,  0,  0, // PC_DSTSTT
    BranchUnit,  0,   0,   0,  0,  0, // PC_LVEBX
    BranchUnit,  0,   0,   0,  0,  0, // PC_LVEHX
    BranchUnit,  0,   0,   0,  0,  0, // PC_LVEWX
    BranchUnit,  0,   0,   0,  0,  0, // PC_LVSL
    BranchUnit,  0,   0,   0,  0,  0, // PC_LVSR
    BranchUnit,  0,   0,   0,  0,  0, // PC_LVX
    BranchUnit,  0,   0,   0,  0,  0, // PC_LVXL
    BranchUnit,  0,   0,   0,  0,  0, // PC_STVEBX
    BranchUnit,  0,   0,   0,  0,  0, // PC_STVEHX
    BranchUnit,  0,   0,   0,  0,  0, // PC_STVEWX
    BranchUnit,  0,   0,   0,  0,  0, // PC_STVX
    BranchUnit,  0,   0,   0,  0,  0, // PC_STVXL
    BranchUnit,  0,   0,   0,  0,  0, // PC_MFVSCR
    BranchUnit,  0,   0,   0,  0,  0, // PC_MTVSCR
    BranchUnit,  0,   0,   0,  0,  0, // PC_VADDCUW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VADDFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VADDSBS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VADDSHS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VADDSWS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VADDUBM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VADDUBS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VADDUHM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VADDUHS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VADDUWM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VADDUWS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VAND
    BranchUnit,  0,   0,   0,  0,  0, // PC_VANDC
    BranchUnit,  0,   0,   0,  0,  0, // PC_VAVGSB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VAVGSH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VAVGSW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VAVGUB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VAVGUH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VAVGUW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCFSX
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCFUX
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPBFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPEQFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPEQUB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPEQUH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPEQUW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPGEFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPGTFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPGTSB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPGTSH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPGTSW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPGTUB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPGTUH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPGTUW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCTSXS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCTUXS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VEXPTEFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VLOGEFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMAXFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMAXSB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMAXSH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMAXSW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMAXUB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMAXUH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMAXUW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMINFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMINSB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMINSH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMINSW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMINUB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMINUH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMINUW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMRGHB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMRGHH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMRGHW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMRGLB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMRGLH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMRGLW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMULESB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMULESH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMULEUB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMULEUH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMULOSB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMULOSH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMULOUB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMULOUH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VNOR
    BranchUnit,  0,   0,   0,  0,  0, // PC_VOR
    BranchUnit,  0,   0,   0,  0,  0, // PC_VPKPX
    BranchUnit,  0,   0,   0,  0,  0, // PC_VPKSHSS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VPKSHUS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VPKSWSS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VPKSWUS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VPKUHUM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VPKUHUS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VPKUWUM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VPKUWUS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VREFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VRFIM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VRFIN
    BranchUnit,  0,   0,   0,  0,  0, // PC_VRFIP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VRFIZ
    BranchUnit,  0,   0,   0,  0,  0, // PC_VRLB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VRLH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VRLW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VRSQRTEFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSL
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSLB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSLH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSLO
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSLW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSPLTB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSPLTH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSPLTW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSPLTISB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSPLTISH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSPLTISW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSR
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSRAB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSRAH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSRAW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSRB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSRH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSRO
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSRW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUBCUW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUBFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUBSBS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUBSHS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUBSWS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUBUBM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUBUBS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUBUHM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUBUHS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUBUWM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUBUWS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUMSWS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUM2SWS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUM4SBS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUM4SHS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUM4UBS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VUPKHPX
    BranchUnit,  0,   0,   0,  0,  0, // PC_VUPKHSB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VUPKHSH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VUPKLPX
    BranchUnit,  0,   0,   0,  0,  0, // PC_VUPKLSB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VUPKLSH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VXOR
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMADDFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMHADDSHS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMHRADDSHS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMLADDUHM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMSUMMBM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMSUMSHM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMSUMSHS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMSUMUBM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMSUMUHM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMSUMUHS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VNMSUBFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VPERM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSEL
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSLDOI
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMR
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMRP
    BranchUnit,  0,   0,   0,  0,  0, // PC_PSQ_L
    BranchUnit,  0,   0,   0,  0,  0, // PC_PSQ_LU
    BranchUnit,  0,   0,   0,  0,  0, // PC_PSQ_LX
    BranchUnit,  0,   0,   0,  0,  0, // PC_PSQ_LUX
    BranchUnit,  0,   0,   0,  0,  0, // PC_PSQ_ST
    BranchUnit,  0,   0,   0,  0,  0, // PC_PSQ_STU
    BranchUnit,  0,   0,   0,  0,  0, // PC_PSQ_STX
    BranchUnit,  0,   0,   0,  0,  0, // PC_PSQ_STUX
    BranchUnit,  0,   0,   0,  0,  0, // PC_DCBZ_L
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_ADD
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_SUB
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MUL
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_DIV
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MADD
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MSUB
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_NMADD
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_NMSUB
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_RES
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_SEL
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_ABS
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_NABS
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_NEG
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MR
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_RSQRTE
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_CMPU0
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_CMPO0
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_CMPU1
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_CMPO1
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MERGE00
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MERGE01
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MERGE10
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MERGE11
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_SUM0
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_SUM1
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MULS0
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MULS1
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MADDS0
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MADDS1
    BranchUnit,  0,   0,   0,  0,  0, // PC_SLE
    BranchUnit,  0,   0,   0,  0,  0, // PC_SLEQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_SLIQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_SLLIQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_SLLQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_SLQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_SRAIQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_SRAQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_SRE
    BranchUnit,  0,   0,   0,  0,  0, // PC_SREA
    BranchUnit,  0,   0,   0,  0,  0, // PC_SREQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_SRIQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_SRLIQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_SRLQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_SRQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_MASKG
    BranchUnit,  0,   0,   0,  0,  0, // PC_MASKIR
    BranchUnit,  0,   0,   0,  0,  0, // PC_LSCBX
    BranchUnit,  0,   0,   0,  0,  0, // PC_DIV
    BranchUnit,  0,   0,   0,  0,  0, // PC_DIVS
    BranchUnit,  0,   0,   0,  0,  0, // PC_DOZ
    BranchUnit,  0,   0,   0,  0,  0, // PC_MUL
    BranchUnit,  0,   0,   0,  0,  0, // PC_NABS
    BranchUnit,  0,   0,   0,  0,  0, // PC_ABS
    BranchUnit,  0,   0,   0,  0,  0, // PC_CLCS
    BranchUnit,  0,   0,   0,  0,  0, // PC_DOZI
    BranchUnit,  0,   0,   0,  0,  0, // PC_RLMI
    BranchUnit,  0,   0,   0,  0,  0, // PC_RRIB
};
// clang-format on

static void advance(int firstStage, int oldStage, int newStage) {
    PCode* instr = pipeline[oldStage].instr;
    int cycles = instruction_timing[instr->op].cycles[newStage - firstStage];
    pipeline[newStage].instr = instr;
    pipeline[newStage].remaining = cycles;
    pipeline[oldStage].instr = NULL;
}

static void assign_completion_buffer(PCode* instr) {
    completionbuffers.used++;
    completionbuffers.free--;
    completionbuffers.entries[completionbuffers.nextFreeSlot].instr = instr;
    completionbuffers.entries[completionbuffers.nextFreeSlot].completed = FALSE;
    completionbuffers.nextFreeSlot = (completionbuffers.nextFreeSlot + 1) % MaxEntries;
}

static void complete_instruction(int stage) {
    PCode* instr = pipeline[stage].instr;
    int buf = 0;
    while (buf < MaxEntries && completionbuffers.entries[buf].instr != instr) {
        buf++;
    }

    completionbuffers.entries[buf].completed = TRUE;
    pipeline[stage].instr = NULL;
}

static void retire_instruction(void) {
    completionbuffers.entries[completionbuffers.nextToRetire].instr = NULL;
    completionbuffers.used--;
    completionbuffers.free++;
    completionbuffers.nextToRetire = (completionbuffers.nextToRetire + 1) % MaxEntries;
}

static int latency(PCode* instr) {
    int cycles = instruction_timing[instr->op].latency;
    if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
        cycles += 2;
    }
    if (instr->op == PC_LMW || instr->op == PC_STMW) {
        cycles += instr->argCount - 2;
    }
    return cycles;
}

static void initialize(void) {
    int stage;
    int i;

    for (stage = 0; stage < NumStages; stage++) {
        pipeline[stage].instr = NULL;
    }

    completionbuffers.free = MaxEntries;
    completionbuffers.used = 0;
    completionbuffers.nextToRetire = 0;
    completionbuffers.nextFreeSlot = 0;
    for (i = 0; i < MaxEntries; i++) {
        completionbuffers.entries[i].instr = NULL;
    }
}

static int can_issue(PCode* instr) {
    int stage;

    if (completionbuffers.free == 0) {
        return FALSE;
    }

    stage = instruction_timing[instr->op].stage;
    if (pipeline[stage].instr) {
        return FALSE;
    }

    if ((instr->flags & fIsWrite) &&
        pipeline[LSU2].instr &&
        (pipeline[LSU2].instr->flags & fIsWrite)) {
        return FALSE;
    }

    return TRUE;
}

static void issue(PCode* instr) {
    int stage = instruction_timing[instr->op].stage;
    int cycles = instruction_timing[instr->op].cycles[0];
    assign_completion_buffer(instr);
    pipeline[stage].instr = instr;
    pipeline[stage].remaining = cycles;
}

static void advance_clock(void) {
    int stage;

    for (stage = 0; stage < NumStages; stage++) {
        if (pipeline[stage].instr && pipeline[stage].remaining) {
            --pipeline[stage].remaining;
        }
    }

    if (completionbuffers.used &&
        completionbuffers.entries[completionbuffers.nextToRetire].completed) {
        retire_instruction();
        if (completionbuffers.used &&
            completionbuffers.entries[completionbuffers.nextToRetire].completed) {
            retire_instruction();
        }
    }

    if (pipeline[Stage1].instr && pipeline[Stage1].remaining == 0) {
        complete_instruction(Stage1);
    }
    if (pipeline[LSU2].instr && pipeline[LSU2].remaining == 0) {
        complete_instruction(LSU2);
    }
    if (pipeline[BranchUnit].instr && pipeline[BranchUnit].remaining == 0) {
        complete_instruction(BranchUnit);
    }

    if (pipeline[LSU1].instr && pipeline[LSU1].remaining == 0 && !pipeline[LSU2].instr) {
        advance(LSU1, LSU1, LSU2);
    }
}

static int serializes(PCode* instr) {
    return instruction_timing[instr->op].serializes;
}
