/* The code of -dis: each instruction of a code section, disassembled by the PowerPC disassembler, printed with its
   address and word, under a hunk line for each symbol it starts; the operands that relocations or special registers
   name are printed by their names.

   No string names this file: DumpELFCodePPC.c is the CodeWarrior linker's file for it, by its role. */
#include "compiler/common.h"
#include "compiler/CompilerTools.h"
#include "driver/libimp-eabi-ppc.h"
#include "linker/PPCDisassembler.h"

extern int sprintf(char *s, const char *format, ...);
extern char *strcpy(char *dest, const char *source);
extern char *strcat(char *dest, const char *source);
extern unsigned long strlen(const char *s);
extern unsigned char __upper_map[];
extern unsigned char copts[];
extern void CompilerTools_FormatMessageAndLongjmp(int message, int status, ...);

/* The name of a relocation's symbol. */
extern char *DumpELF_SymbolName(void *file, void *relocations, unsigned long symbol);

/* The processor (-proc) and the dump's options: the code's addresses as offsets, raw words, a compact line. */
extern short data_0049c418;
extern char data_004a2d6f;
extern char data_004a2d71;
extern char data_004a2dc2;

typedef void (*DumpCodePrinter)(char *text, long length, long size);

#pragma options align = mac68k
typedef struct RegisterName {
    unsigned long number;
    const char *name;
    unsigned long processors; /* the processors that have it */
} RegisterName;

/* An input section: its header, its contents and the section its header links. */
typedef struct CodeSectionInfo {
    long x0;
    long x4;
    long x8;
    Elf32Section *header;
    char *data;
    struct CodeSectionInfo *link;
} CodeSectionInfo;

typedef struct CodeSymbol {
    unsigned long name;
    unsigned long value;
    unsigned long size;
    unsigned char info;
    unsigned char other;
    unsigned short sectionIndex;
} CodeSymbol;

/* The object being dumped: its symbols (the list sorted by address, 12 bytes each, the index at +8). */
typedef struct CodeFile {
    long x0;
    long x4;
    long symbolCount;
    long xc;
    long x10;
    long x14;
    CodeSectionInfo *symbols;
    long x1c;
    char *sortedSymbols;
} CodeFile;

/* A section being dumped: its index, name, header, contents and relocations. */
typedef struct CodeSection {
    long x0;
    unsigned long index;
    char *name;
    Elf32Section *header;
    char *data;
    CodeSectionInfo *relocations;
} CodeSection;

typedef struct CodeRelocation {
    unsigned long offset;
    unsigned long info;
    long addend;
} CodeRelocation;
#pragma options align = reset

#if LINKER >= 27
/* 2.7's (0x49e0ac, 0x49d1b8): the e500's, 405's, 5100/5200's... registers too. */
static const RegisterName deviceControlRegisters[52] = {
    {64, "exisr", 0x40},
    {66, "exier", 0x40},
    {112, "brh0", 0x40},
    {113, "brh1", 0x40},
    {114, "brh2", 0x40},
    {115, "brh3", 0x40},
    {116, "brh4", 0x40},
    {117, "brh5", 0x40},
    {118, "brh6", 0x40},
    {119, "brh7", 0x40},
    {128, "br0", 0x40},
    {128, "brcr0", 0x20},
    {129, "br1", 0x40},
    {129, "brcr1", 0x20},
    {130, "br2", 0x40},
    {130, "brcr2", 0x20},
    {131, "br3", 0x40},
    {131, "brcr3", 0x20},
    {132, "br4", 0x40},
    {132, "brcr4", 0x20},
    {133, "br5", 0x40},
    {133, "brcr5", 0x20},
    {134, "br6", 0x40},
    {134, "brcr6", 0x20},
    {135, "br7", 0x40},
    {135, "brcr7", 0x20},
    {144, "bear", 0x1000060},
    {145, "besr", 0x40},
    {145, "besr0", 0x20},
    {160, "iocr", 0x1000060},
    {161, "pmcr0", 0x20},
    {192, "dmacr0", 0x40},
    {193, "dmact0", 0x40},
    {194, "dmada0", 0x40},
    {195, "dmasa0", 0x40},
    {196, "dmacc0", 0x40},
    {200, "dmacr1", 0x40},
    {201, "dmact1", 0x40},
    {202, "dmada1", 0x40},
    {203, "dmasa1", 0x40},
    {204, "dmacc1", 0x40},
    {208, "dmacr2", 0x40},
    {209, "dmact2", 0x40},
    {210, "dmada2", 0x40},
    {211, "dmasa2", 0x40},
    {212, "dmacc2", 0x40},
    {216, "dmacr3", 0x40},
    {217, "dmact3", 0x40},
    {218, "dmada3", 0x40},
    {219, "dmasa3", 0x40},
    {220, "dmacc3", 0x40},
    {224, "dmasr", 0x40},
};

static const RegisterName specialRegisters[319] = {
    {0, "mq", 0x2},
    {1, "xer", 0x1effffe},
    {4, "rtcu", 0x2},
    {5, "rtcl", 0x2},
    {8, "lr", 0x1effffe},
    {9, "ctr", 0x1effffe},
    {18, "dsisr", 0xefff9e},
    {19, "dar", 0xefff9e},
    {22, "dec", 0xefff9e},
    {25, "sdr1", 0xeff01e},
    {26, "srr0", 0x1effffe},
    {27, "srr1", 0x1effffe},
    {48, "pid0", 0xe40000},
    {48, "pid", 0xe40000},
    {54, "decar", 0xe40000},
    {58, "csrr0", 0xf40000},
    {59, "csrr1", 0xf40000},
    {61, "dear", 0xe40000},
    {62, "esr", 0xe40000},
    {63, "ivpr", 0xe40000},
    {80, "eie", 0xf80},
    {81, "eid", 0xf80},
    {82, "nri", 0xf80},
    {144, "cmpa", 0xf80},
    {145, "cmpb", 0xf80},
    {146, "cmpc", 0xf80},
    {147, "cmpd", 0xf80},
    {148, "icr", 0x380},
    {148, "ecr", 0xc00},
    {149, "der", 0xf80},
    {150, "counta", 0xf80},
    {151, "countb", 0xf80},
    {152, "cmpe", 0xf80},
    {153, "cmpf", 0xf80},
    {154, "cmpg", 0xf80},
    {155, "cmph", 0xf80},
    {156, "lctrl1", 0xf80},
    {157, "lctrl2", 0xf80},
    {158, "ictrl", 0xf80},
    {159, "bar", 0xf80},
    {256, "vrsave", 0x90000},
    {256, "usprg0", 0x1e40000},
    {260, "sprg4.r", 0xe40000},
    {261, "sprg5.r", 0xe40000},
    {262, "sprg6.r", 0xe40000},
    {263, "sprg7.r", 0xe40000},
    {268, "tbl", 0xefff9e},
    {269, "tbu", 0xefff9e},
    {272, "sprg0", 0x1effffe},
    {273, "sprg1", 0x1effffe},
    {274, "sprg2", 0x1effffe},
    {275, "sprg3", 0x1effffe},
    {276, "sprg4", 0xf40000},
    {277, "sprg5", 0xf40000},
    {278, "sprg6", 0xf40000},
    {279, "sprg7", 0xf40000},
    {282, "ear", 0x1eff07e},
    {284, "tbl_write", 0x1effffe},
    {284, "tbl", 0x100000},
    {285, "tbu_write", 0x1effffe},
    {285, "tbu", 0x100000},
    {286, "svr", 0x100000},
    {286, "pir", 0xe40000},
    {287, "pvr", 0x1effffe},
    {304, "dbsr", 0xe40000},
    {308, "dbcr0", 0xe40000},
    {309, "dbcr1", 0xe40000},
    {310, "dbcr2", 0xe40000},
    {312, "iac1", 0xe40000},
    {313, "iac2", 0xe40000},
    {314, "iac3", 0xe00000},
    {315, "iac4", 0xe00000},
    {316, "dac1", 0xe00000},
    {317, "dac2", 0xe00000},
    {318, "dvc1", 0xe00000},
    {319, "dvc2", 0xe00000},
    {336, "tsr", 0xe40000},
    {340, "tcr", 0xe40000},
    {400, "ivor0", 0xe40000},
    {401, "ivor1", 0xe40000},
    {402, "ivor2", 0xe40000},
    {403, "ivor3", 0xe40000},
    {404, "ivor4", 0xe40000},
    {405, "ivor5", 0xe40000},
    {406, "ivor6", 0xe40000},
    {407, "ivor7", 0xe40000},
    {408, "ivor8", 0xe40000},
    {409, "ivor9", 0xe40000},
    {410, "ivor10", 0xe40000},
    {411, "ivor11", 0xe40000},
    {412, "ivor12", 0xe40000},
    {413, "ivor13", 0xe40000},
    {414, "ivor14", 0xe40000},
    {415, "ivor15", 0xe40000},
    {512, "spefscr", 0x440000},
    {512, "efscr", 0x800000},
    {513, "bbear", 0x40000},
    {514, "bbtar", 0x40000},
    {515, "l1cfg0", 0xe40000},
    {516, "l1cfg1", 0x40000},
    {517, "npidr", 0x40000},
    {518, "iarr", 0x40000},
    {528, "ibat0u", 0x1eff07e},
    {528, "mi_gra", 0x800},
    {528, "ivor32", 0xe40000},
    {529, "ivor33", 0xe40000},
    {529, "ibat0l", 0x1eff07e},
    {530, "ivor34", 0xe40000},
    {530, "ibat1u", 0x1eff07e},
    {531, "ivor35", 0x40000},
    {531, "ibat1l", 0x1eff07e},
    {532, "ibat2u", 0x1eff07e},
    {533, "ibat2l", 0x1eff07e},
    {534, "ibat3u", 0x1eff07e},
    {535, "ibat3l", 0x1eff07e},
    {536, "dbat0u", 0x1eff07c},
    {536, "l2u_gra", 0x800},
    {537, "dbat0l", 0x1eff07c},
    {538, "dbat1u", 0x1eff07c},
    {539, "dbat1l", 0x1eff07c},
    {540, "dbat2u", 0x1eff07c},
    {541, "dbat2l", 0x1eff07c},
    {542, "dbat3u", 0x1eff07c},
    {543, "dbat3l", 0x1eff07c},
    {560, "ic_cst", 0x380},
    {560, "iccst", 0x400},
    {560, "bbcmcr", 0x800},
    {560, "ibat4u", 0x100000},
    {560, "ctxcr", 0xe00000},
    {561, "dbcr3", 0xe00000},
    {561, "ic_adr", 0x380},
    {561, "icadr", 0x400},
    {561, "ibat4l", 0x100000},
    {562, "dbcnt", 0xe00000},
    {562, "ic_dat", 0x380},
    {562, "icdat", 0x400},
    {562, "ibat5u", 0x100000},
    {563, "ibat5l", 0x100000},
    {564, "ibat6u", 0x100000},
    {565, "ibat6l", 0x100000},
    {566, "ibat7u", 0x100000},
    {567, "ibat7l", 0x100000},
    {568, "dc_cst", 0x380},
    {568, "l2u_mcr", 0x800},
    {568, "dbat4u", 0x100000},
    {568, "altctxcr", 0xe00000},
    {569, "dc_adr", 0x380},
    {569, "dbat4l", 0x100000},
    {570, "dc_dat", 0x380},
    {570, "dbat5u", 0x100000},
    {570, "mcsrr0", 0x40000},
    {571, "mcsrr1", 0x40000},
    {571, "dbat5l", 0x100000},
    {572, "mcsr", 0x40000},
    {572, "dbat6u", 0x100000},
    {573, "dbat6l", 0x100000},
    {573, "mcar", 0x40000},
    {574, "dbat7u", 0x100000},
    {574, "dsrr0", 0xe00000},
    {575, "dsrr1", 0xe00000},
    {575, "dbat7l", 0x100000},
    {624, "mas0", 0xe40000},
    {625, "mas1", 0xe40000},
    {626, "mas2", 0xe40000},
    {627, "mas3", 0xe40000},
    {628, "mas4", 0xe40000},
    {629, "mas5", 0x80000},
    {630, "mas6", 0xe40000},
    {630, "dpdr", 0xf80},
    {631, "dpir", 0x380},
    {638, "immr", 0x380},
    {633, "pid1", 0x40000},
    {634, "pid2", 0x40000},
    {688, "tlbcfg0", 0xe40000},
    {689, "tlbcfg1", 0xe40000},
    {784, "mi_ctr", 0x380},
    {784, "mi_rba0", 0x800},
    {785, "mi_rba1", 0x800},
    {786, "mi_rba2", 0x800},
    {786, "mi_ap", 0x380},
    {787, "mi_epn", 0x380},
    {787, "mi_rba3", 0x800},
    {789, "mi_twc", 0x380},
    {789, "mi_l1dl2p", 0x380},
    {790, "mi_rpn", 0x380},
    {792, "md_ctr", 0x380},
    {792, "l2u_rba0", 0x800},
    {793, "l2u_rba1", 0x800},
    {793, "m_casid", 0x380},
    {794, "md_ap", 0x380},
    {794, "l2u_rba2", 0x800},
    {795, "l2u_rba3", 0x800},
    {795, "md_epn", 0x380},
    {796, "m_twb", 0x380},
    {796, "md_l1p", 0x380},
    {797, "md_twc", 0x380},
    {797, "md_l1dl2p", 0x380},
    {798, "md_rpn", 0x380},
    {799, "m_tw", 0x380},
    {799, "m_save", 0x380},
    {816, "mi_dbcam", 0x80},
    {816, "mi_cam", 0x300},
    {816, "mi_ra0", 0x800},
    {817, "mi_ra1", 0x800},
    {817, "mi_dbram0", 0x80},
    {817, "mi_ram0", 0x300},
    {818, "mi_dbram1", 0x80},
    {818, "mi_ram1", 0x300},
    {818, "mi_ra2", 0x800},
    {819, "mi_ra3", 0x800},
    {824, "md_dbcam", 0x80},
    {824, "md_cam", 0x300},
    {824, "l2u_ra0", 0x800},
    {825, "l2u_ra1", 0x800},
    {825, "md_dbram0", 0x80},
    {825, "md_ram0", 0x300},
    {826, "md_dbram1", 0x80},
    {826, "md_ram1", 0x300},
    {826, "l2u_ra2", 0x800},
    {827, "l2u_ra3", 0x800},
    {928, "ummcr2", 0x10000},
    {935, "ubmar", 0x10000},
    {936, "ummcr0", 0x11000},
    {937, "upmc1", 0x11000},
    {938, "upmc2", 0x11000},
    {939, "usia", 0x1000},
    {939, "usiar", 0x10000},
    {940, "ummcr1", 0x11000},
    {941, "upmc3", 0x11000},
    {942, "upmc4", 0x11000},
    {944, "zpr", 0x1000040},
    {944, "mmcr2", 0x10000},
    {945, "pid", 0x1000040},
    {947, "ccr0", 0x1000000},
    {950, "dvc1", 0x1000000},
    {951, "dvc2", 0x1000000},
    {951, "bamr", 0x10000},
    {952, "mmcr0", 0x11010},
    {953, "pmc1", 0x11010},
    {953, "sgr", 0x1000060},
    {954, "pmc2", 0x11010},
    {954, "dcwr", 0x1000060},
    {955, "sia", 0x1010},
    {955, "siar", 0x10000},
    {955, "sler", 0x1000020},
    {956, "mmcr1", 0x11000},
    {957, "pmc3", 0x11000},
    {957, "dbcr1", 0x1000000},
    {958, "pmc4", 0x11000},
    {959, "sda", 0x10},
    {972, "tbhu", 0x60},
    {973, "tblu", 0x60},
    {976, "dmiss", 0x10c00c},
    {977, "dcmp", 0x10c00c},
    {978, "hash1", 0x10c00c},
    {979, "hash2", 0x10c00c},
    {979, "icdbdr", 0x1000060},
    {980, "imiss", 0x10c00c},
    {980, "esr", 0x1000060},
    {981, "icmp", 0x10c00c},
    {981, "dear", 0x1000060},
    {982, "rpa", 0x10c00c},
    {982, "evpr", 0x1000060},
    {983, "cdbcr", 0x1000060},
    {984, "tsr", 0x1000060},
    {984, "tcr", 0x4},
    {986, "tcr", 0x1000060},
    {986, "ibr", 0x4},
    {987, "pit", 0x1000060},
    {987, "esasrr", 0x4},
    {988, "tbhi", 0x1000060},
    {989, "tblo", 0x1000060},
    {990, "srr2", 0x1000060},
    {990, "sebr", 0x4},
    {991, "srr3", 0x1000060},
    {991, "ser", 0x4},
    {1000, "dabr2", 0x100000},
    {1001, "dbcr", 0x100000},
    {1002, "ibcr", 0x100000},
    {1008, "hid0", 0xf5d01e},
    {1008, "dbsr", 0x1000040},
    {1009, "hid1", 0xf5d00e},
    {1010, "hid2", 0x2},
    {1010, "iabr", 0x11d01e},
    {1010, "dbcr0", 0x1000000},
    {1010, "dbcr", 0x1000060},
    {1010, "l1csr0", 0xe40000},
    {1011, "hid2", 0x10c000},
    {1011, "l1csr1", 0x40000},
    {1012, "iac1", 0x1000040},
    {1012, "iac", 0x20},
    {1012, "mmucsr0", 0xe40000},
    {1013, "dabr", 0x111012},
    {1013, "iac2", 0x1000040},
    {1013, "hid5", 0x2},
    {1013, "bucsr", 0xe40000},
    {1014, "dac1", 0x1000040},
    {1014, "dac", 0x20},
    {1014, "msscr0", 0x10000},
    {1015, "dac2", 0x1000040},
    {1015, "mmucfg", 0xe40000},
    {1016, "l1finv0", 0xe00000},
    {1017, "l2cr", 0x11000},
    {1018, "dccr", 0x1000060},
    {1018, "iabr2", 0x100000},
    {1019, "iccr", 0x1000060},
    {1019, "ictc", 0x11000},
    {1020, "pbl1", 0x40},
    {1020, "thrm1", 0x11000},
    {1021, "pbu1", 0x40},
    {1021, "thrm2", 0x11000},
    {1022, "fpecr", 0xc00},
    {1022, "pbl2", 0x40},
    {1022, "thrm3", 0x11000},
    {1023, "pir", 0x110010},
    {1023, "thrm3", 0x1000},
    {1023, "hid15", 0x2},
    {1023, "pbu2", 0x40},
    {1023, "svr", 0xe40000},
};
#else
static const RegisterName deviceControlRegisters[52] = {
    {64, "exisr", 0x40},
    {66, "exier", 0x40},
    {112, "brh0", 0x40},
    {113, "brh1", 0x40},
    {114, "brh2", 0x40},
    {115, "brh3", 0x40},
    {116, "brh4", 0x40},
    {117, "brh5", 0x40},
    {118, "brh6", 0x40},
    {119, "brh7", 0x40},
    {128, "br0", 0x40},
    {128, "brcr0", 0x20},
    {129, "br1", 0x40},
    {129, "brcr1", 0x20},
    {130, "br2", 0x40},
    {130, "brcr2", 0x20},
    {131, "br3", 0x40},
    {131, "brcr3", 0x20},
    {132, "br4", 0x40},
    {132, "brcr4", 0x20},
    {133, "br5", 0x40},
    {133, "brcr5", 0x20},
    {134, "br6", 0x40},
    {134, "brcr6", 0x20},
    {135, "br7", 0x40},
    {135, "brcr7", 0x20},
    {144, "bear", 0x60},
    {145, "besr", 0x40},
    {145, "besr0", 0x20},
    {160, "iocr", 0x60},
    {161, "pmcr0", 0x20},
    {192, "dmacr0", 0x40},
    {193, "dmact0", 0x40},
    {194, "dmada0", 0x40},
    {195, "dmasa0", 0x40},
    {196, "dmacc0", 0x40},
    {200, "dmacr1", 0x40},
    {201, "dmact1", 0x40},
    {202, "dmada1", 0x40},
    {203, "dmasa1", 0x40},
    {204, "dmacc1", 0x40},
    {208, "dmacr2", 0x40},
    {209, "dmact2", 0x40},
    {210, "dmada2", 0x40},
    {211, "dmasa2", 0x40},
    {212, "dmacc2", 0x40},
    {216, "dmacr3", 0x40},
    {217, "dmact3", 0x40},
    {218, "dmada3", 0x40},
    {219, "dmasa3", 0x40},
    {220, "dmacc3", 0x40},
    {224, "dmasr", 0x40},
};

static const RegisterName specialRegisters[202] = {
    {0, "mq", 0x2},
    {1, "xer", 0xffffe},
    {4, "rtcu", 0x2},
    {5, "rtcl", 0x2},
    {8, "lr", 0xffffe},
    {9, "ctr", 0xffffe},
    {18, "dsisr", 0xfff9e},
    {19, "dar", 0xfff9e},
    {22, "dec", 0xfff9e},
    {25, "sdr1", 0xff01e},
    {26, "srr0", 0xffffe},
    {27, "srr1", 0xffffe},
    {80, "eie", 0xf80},
    {81, "eid", 0xf80},
    {82, "nri", 0xf80},
    {144, "cmpa", 0xf80},
    {145, "cmpb", 0xf80},
    {146, "cmpc", 0xf80},
    {147, "cmpd", 0xf80},
    {148, "icr", 0x380},
    {148, "ecr", 0xc00},
    {149, "der", 0xf80},
    {150, "counta", 0xf80},
    {151, "countb", 0xf80},
    {152, "cmpe", 0xf80},
    {153, "cmpf", 0xf80},
    {154, "cmpg", 0xf80},
    {155, "cmph", 0xf80},
    {156, "lctrl1", 0xf80},
    {157, "lctrl2", 0xf80},
    {158, "ictrl", 0xf80},
    {159, "bar", 0xf80},
    {268, "tbl", 0xfff9e},
    {269, "tbu", 0xfff9e},
    {272, "sprg0", 0xffffe},
    {273, "sprg1", 0xffffe},
    {274, "sprg2", 0xffffe},
    {275, "sprg3", 0xffffe},
    {282, "ear", 0xff07e},
    {284, "tbl_write", 0xfff9e},
    {285, "tbu_write", 0xfff9e},
    {287, "pvr", 0xffffe},
    {528, "ibat0u", 0xff07e},
    {528, "mi_gra", 0x800},
    {529, "ibat0l", 0xff07e},
    {530, "ibat1u", 0xff07e},
    {531, "ibat1l", 0xff07e},
    {532, "ibat2u", 0xff07e},
    {533, "ibat2l", 0xff07e},
    {534, "ibat3u", 0xff07e},
    {535, "ibat3l", 0xff07e},
    {536, "dbat0u", 0xff07c},
    {536, "l2u_gra", 0x800},
    {537, "dbat0l", 0xff07c},
    {538, "dbat1u", 0xff07c},
    {539, "dbat1l", 0xff07c},
    {540, "dbat2u", 0xff07c},
    {541, "dbat2l", 0xff07c},
    {542, "dbat3u", 0xff07c},
    {543, "dbat3l", 0xff07c},
    {560, "ic_cst", 0x380},
    {560, "iccst", 0x400},
    {560, "bbcmcr", 0x800},
    {561, "ic_adr", 0x380},
    {561, "icadr", 0x400},
    {562, "ic_dat", 0x380},
    {562, "icdat", 0x400},
    {568, "dc_cst", 0x380},
    {568, "l2u_mcr", 0x800},
    {569, "dc_adr", 0x380},
    {570, "dc_dat", 0x380},
    {630, "dpdr", 0xf80},
    {631, "dpir", 0x380},
    {638, "immr", 0x380},
    {784, "mi_ctr", 0x380},
    {784, "mi_rba0", 0x800},
    {785, "mi_rba1", 0x800},
    {786, "mi_rba2", 0x800},
    {786, "mi_ap", 0x380},
    {787, "mi_epn", 0x380},
    {787, "mi_rba3", 0x800},
    {789, "mi_twc", 0x380},
    {789, "mi_l1dl2p", 0x380},
    {790, "mi_rpn", 0x380},
    {792, "md_ctr", 0x380},
    {792, "l2u_rba0", 0x800},
    {793, "l2u_rba1", 0x800},
    {793, "m_casid", 0x380},
    {794, "md_ap", 0x380},
    {794, "l2u_rba2", 0x800},
    {795, "l2u_rba3", 0x800},
    {795, "md_epn", 0x380},
    {796, "m_twb", 0x380},
    {796, "md_l1p", 0x380},
    {797, "md_twc", 0x380},
    {797, "md_l1dl2p", 0x380},
    {798, "md_rpn", 0x380},
    {799, "m_tw", 0x380},
    {799, "m_save", 0x380},
    {816, "mi_dbcam", 0x80},
    {816, "mi_cam", 0x300},
    {816, "mi_ra0", 0x800},
    {817, "mi_ra1", 0x800},
    {817, "mi_dbram0", 0x80},
    {817, "mi_ram0", 0x300},
    {818, "mi_dbram1", 0x80},
    {818, "mi_ram1", 0x300},
    {818, "mi_ra2", 0x800},
    {819, "mi_ra3", 0x800},
    {824, "md_dbcam", 0x80},
    {824, "md_cam", 0x300},
    {824, "l2u_ra0", 0x800},
    {825, "l2u_ra1", 0x800},
    {825, "md_dbram0", 0x80},
    {825, "md_ram0", 0x300},
    {826, "md_dbram1", 0x80},
    {826, "md_ram1", 0x300},
    {826, "l2u_ra2", 0x800},
    {827, "l2u_ra3", 0x800},
    {928, "ummcr2", 0x10000},
    {935, "ubmar", 0x10000},
    {936, "ummcr0", 0x11000},
    {937, "upmc1", 0x11000},
    {938, "upmc2", 0x11000},
    {939, "usia", 0x1000},
    {939, "usiar", 0x10000},
    {940, "ummcr1", 0x11000},
    {941, "upmc3", 0x11000},
    {942, "upmc4", 0x11000},
    {944, "zpr", 0x40},
    {944, "mmcr2", 0x10000},
    {945, "pid", 0x40},
    {951, "bamr", 0x10000},
    {952, "mmcr0", 0x11010},
    {953, "pmc1", 0x11010},
    {953, "sgr", 0x60},
    {954, "pmc2", 0x11010},
    {954, "dcwr", 0x60},
    {955, "sia", 0x1010},
    {955, "siar", 0x10000},
    {955, "sler", 0x20},
    {956, "mmcr1", 0x11000},
    {957, "pmc3", 0x11000},
    {958, "pmc4", 0x11000},
    {959, "sda", 0x10},
    {972, "tbhu", 0x60},
    {973, "tblu", 0x60},
    {976, "dmiss", 0xc00c},
    {977, "dcmp", 0xc00c},
    {978, "hash1", 0xc00c},
    {979, "hash2", 0xc00c},
    {979, "icdbdr", 0x60},
    {980, "imiss", 0xc00c},
    {980, "esr", 0x60},
    {981, "icmp", 0xc00c},
    {981, "dear", 0x60},
    {982, "rpa", 0xc00c},
    {982, "evpr", 0x60},
    {983, "cdbcr", 0x60},
    {984, "tsr", 0x60},
    {984, "tcr", 0x4},
    {986, "tcr", 0x60},
    {986, "ibr", 0x4},
    {987, "pit", 0x60},
    {987, "esasrr", 0x4},
    {988, "tbhi", 0x60},
    {989, "tblo", 0x60},
    {990, "srr2", 0x60},
    {990, "sebr", 0x4},
    {991, "srr3", 0x60},
    {991, "ser", 0x4},
    {1008, "hid0", 0x1d01e},
    {1008, "dbsr", 0x40},
    {1009, "hid1", 0x1d00e},
    {1010, "hid2", 0x2},
    {1010, "iabr", 0x1d01e},
    {1010, "dbcr", 0x60},
    {1011, "hid2", 0xc000},
    {1012, "iac1", 0x40},
    {1012, "iac", 0x20},
    {1013, "dabr", 0x11012},
    {1013, "iac2", 0x40},
    {1013, "hid5", 0x2},
    {1014, "dac1", 0x40},
    {1014, "dac", 0x20},
    {1014, "msscr0", 0x10000},
    {1015, "dac2", 0x40},
    {1017, "l2cr", 0x11000},
    {1018, "dccr", 0x60},
    {1019, "iccr", 0x60},
    {1019, "ictc", 0x11000},
    {1020, "pbl1", 0x40},
    {1020, "thrm1", 0x11000},
    {1021, "pbu1", 0x40},
    {1021, "thrm2", 0x11000},
    {1022, "fpecr", 0xc00},
    {1022, "pbl2", 0x40},
    {1022, "thrm3", 0x11000},
    {1023, "pir", 0x10010},
    {1023, "thrm3", 0x1000},
    {1023, "hid15", 0x2},
    {1023, "pbu2", 0x40},
};
#endif

static char registerName[0x20];
static char *symbolName;
static char operandText[0x12c];
static unsigned long processorMask;
static DumpCodePrinter printer;
static long branchTarget;
static CodeFile *dumpFile;
static CodeSection *dumpSection;
static unsigned long sectionAddress;
static long relocationIndex;
static long relocationCount;
static CodeRelocation *relocation;
static Boolean converting;
static unsigned long disassemblerOptions;

char *DumpCode_RelocationOperand(long pc, long addend);
char *DumpCode_SpecialRegister(unsigned long number);
char *DumpCode_DeviceControlRegister(unsigned long number);

/* A register's name, upper case, from TABLE: the entry of the processor, else the first entry of its number. */
static inline char *RegisterNameOf(const RegisterName *table, unsigned int count, unsigned long number) {
    const RegisterName *found = 0;
    const unsigned char *p;
    char *q;
    unsigned int i;

    for (i = 0; i < count; i++) {
        if (number == table[i].number) {
            found = &table[i];
            if (processorMask & table[i].processors)
                break;
        } else if (number < table[i].number) {
            break;
        }
    }
    if (!found)
        return 0;
    q = registerName;
    for (p = (const unsigned char *)found->name; *p; p++)
        *q++ = *p == 0xff ? 0xff : __upper_map[*p];
    *q = '\0';
    return registerName;
}

/* Sets the options of the dumps to come from the processor, and their output. */
/* 0x00453e30 */
void DumpCode_Setup(DumpCodePrinter output) {
    converting = copts[0] == 0;
    processorMask = 0;
    switch (data_0049c418) {
    case 0: processorMask = 0x20; break;
    case 1: processorMask = 0x40; break;
    case 2: case 3: case 4: processorMask = 0xc00; break;
    case 5: processorMask = 2; break;
    case 6: processorMask = 4; break;
    case 7: case 8: processorMask = 8; break;
    case 9: case 10: processorMask = 0x10; break;
    case 11: case 12: processorMask = 0x1000; break;
    case 13: case 14: case 17: processorMask = 0x80; break;
    case 15: processorMask = 0x100; break;
    case 16: processorMask = 0x200; break;
    case 18: processorMask = 0x4000; break;
    case 19: processorMask = 0x8000; break;
    case 20: processorMask = 0; break;
    case 21: processorMask = 0x90000; break;
    case 22: processorMask = 0x20000; break;
#if LINKER >= 27
    /* 2.7's processors (the plugin's numbers for e500, 7450, 56X, 5100, 405, Zen, 5200, 827X, 8280, 87X, 88X) */
    case 23: processorMask = 0x40000; break;
    case 24: processorMask = 0x90000; break;
    case 25: processorMask = 0xc00; break;
    case 26: processorMask = 0x80000; break;
    case 27: case 30: processorMask = 0x100000; break;
    case 28: processorMask = 0xe00000; break;
    case 29: processorMask = 0x1000000; break;
    case 31: case 32: processorMask = 0x4000; break;
    case 33: case 34: processorMask = 0x100; break;
    default: CompilerTools_FormatMessageAndLongjmp(0x33, 2); break;
#endif
    }
    disassemblerOptions = processorMask | (copts[0] ? 0x84000000 : 0x4000000);
    if (!data_004a2d6f)
        disassemblerOptions |= 0x8000000;
    printer = output;
}

/* An operand's text: a relocation's symbol, a special or device control register's name. */
/* 0x00453be0 */
char *DumpCode_Operand(long pc, long arg2, long arg3, unsigned char kind, long value, short reg, char *out) {
    char text[16];
    char *result = 0;

    switch (kind) {
    case 12:
        result = DumpCode_DeviceControlRegister(value);
        break;
    case 7:
        result = DumpCode_SpecialRegister(value);
        break;
    case 4:
        result = DumpCode_RelocationOperand(pc, 0);
        break;
    case 5:
    case 8:
        result = DumpCode_RelocationOperand(pc, value - pc);
        if (result)
            branchTarget = 1;
        break;
    case 3:
    case 2:
        result = DumpCode_RelocationOperand(pc, value);
        break;
    case 6:
        result = DumpCode_RelocationOperand(pc, value);
        if (result && reg) {
            sprintf(text, "(r%hu)", (unsigned short)reg);
            strcat(result, text);
        }
        break;
    }
    if (result)
        strcpy(out, result);
    return result;
}

/* The symbol of the relocation at PC, the next one in the section's, with ADDEND and its @l/@h/@ha. */
/* 0x00453cd0 */
char *DumpCode_RelocationOperand(long pc, long addend) {
    CodeRelocation *r = relocation;
    unsigned long address = pc + sectionAddress;
    long length;

    if (!r || relocationIndex >= relocationCount)
        return 0;
    if (address != r->offset - (long)r->offset % 4)
        return 0;
    symbolName = DumpELF_SymbolName(dumpFile, dumpSection->relocations->link, r->info >> 8);
    length = strlen(symbolName);
    if (length > 0x122)
        length = 0x122;
    if (addend > 0)
        sprintf(operandText, "%.*s+%ld", length, symbolName, addend);
    else if (addend < 0)
        sprintf(operandText, "%.*s-%ld", length, symbolName, -addend);
    else
        sprintf(operandText, "%.*s", length, symbolName);
    switch (r->info & 0xff) {
    case 4:
        strcat(operandText, "@l");
        break;
    case 5:
        strcat(operandText, "@h");
        break;
    case 6:
        strcat(operandText, "@ha");
        break;
    }
    if (++relocationIndex == relocationCount)
        relocation = 0;
    else
        relocation++;
    return operandText;
}

/* The instructions of CODE (SIZE bytes). */
/* 0x004538c0 */
void DumpCode_Instructions(unsigned char *code, long size, CodeFile *file, CodeSection *section) {
    char line[0x1000];
    char mnemonic[0x200];
    char operands[0x200];
    char comment[0x200];
    long state = 0;
    long symbol = 0;
    long pos = 0;
    long hunkOffset = 0;
    long length;
    unsigned short flags;
    unsigned char *word;
    CodeSymbol *s;
    char *p;
    const char *kindName;

    for (; pos < size; pos += 4, hunkOffset += 4) {
        for (; symbol < file->symbolCount; symbol++) {
            s = &((CodeSymbol *)file->symbols->data)[*(long *)(file->sortedSymbols + symbol * 12 + 8)];
            if (s->sectionIndex != section->index || (s->info & 0xf) == 3)
                continue;
            if (s->value == pos + sectionAddress) {
                kindName = (s->info >> 4) == 0 ? "HUNK_LOCAL_CODE" : "HUNK_GLOBAL_CODE";
                length = sprintf(line, "\r\n\r\nHunk:\tKind=%s     Name=\"%s\"  Size=%ld", kindName,
                                 file->symbols->link->data + s->name, s->size);
                if ((s->info & 0xf) == 2)
                    hunkOffset = 0;
                printer(line, length, sizeof(line));
            } else if (s->value > pos + sectionAddress) {
                break;
            }
        }
        branchTarget = 0;
        word = code + pos;
        if (*(long *)word == 0 && !data_004a2dc2)
            continue;
        flags = PPCDis_Disassemble(word, pos - (long)word, disassemblerOptions, mnemonic, operands, comment, pos,
                                   &state, DumpCode_Operand);
        if (converting)
            *(unsigned long *)word = CTool_EndianConvertWord32(*(unsigned long *)word);
        if (flags & 8) {
            for (p = operands; *p; p++) {
                if (*p == '$' && (p[1] == '+' || p[1] == '-')) {
                    *p = '*';
                    p++;
                }
            }
        } else if (flags & 1) {
            branchTarget = 1;
        }
        if (data_004a2d71)
            length = sprintf(line, "\r\n%-8s %-24s%s", mnemonic, operands, branchTarget ? "" : comment);
        else if (data_004a2dc2)
            length = sprintf(line, "\r\n%.8lX: %.8lX: %.8lX  %-8s %-24s%s", pos + sectionAddress, hunkOffset,
                             *(unsigned long *)word, mnemonic, operands, branchTarget ? "" : comment);
        else
            length = sprintf(line, "\r\n%.8lX: %.8lX  %-8s %-24s%s", pos + sectionAddress, *(unsigned long *)word,
                             mnemonic, operands, branchTarget ? "" : comment);
        while (line[--length] == ' ')
            ;
        printer(line, length + 1, sizeof(line));
    }
    printer("\r\n", 2, 2);
}

/* Dumps a code section of FILE. */
/* 0x00453800 */
void DumpCode_Section(CodeFile *file, CodeSection *section) {
    char line[0x200];
    long length;

    length = sprintf(line, "\r\n==> %s", section->name);
    printer(line, length, sizeof(line));
    relocation = 0;
    relocationCount = 0;
    relocationIndex = 0;
    dumpFile = file;
    dumpSection = section;
    if (section->relocations) {
        relocation = (CodeRelocation *)section->relocations->data;
        relocationCount = section->relocations->header->size / 12;
    }
    sectionAddress = section->header->address;
    DumpCode_Instructions((unsigned char *)section->data, section->header->size, file, section);
}

/* Applies SECTION's 32-bit relocations (R_PPC_ADDR32, R_PPC_UADDR32) to its contents, from FILE's symbols, the words
   converted when CONVERT is set. */
/* 0x00453720 */
void DumpCode_Relocate(CodeFile *file, CodeSection *section, char convert) {
    char *data = section->data;
    CodeRelocation *r;
    unsigned long i;
    long value;
    long *at;

    for (i = 0; i < section->relocations->header->size / 12; i++) {
        r = &((CodeRelocation *)section->relocations->data)[i];
        if ((r->info & 0xff) == 1 || (r->info & 0xff) == 0x18) {
            value = ((CodeSymbol *)file->symbols->data)[r->info >> 8].value;
            if ((r->offset & 3) == 0) {
                at = (long *)(data + r->offset);
                *at = value + r->addend;
                if (convert)
                    *at = CTool_EndianConvertWord32(*at);
            } else {
                value += r->addend;
                if (convert)
                    value = CTool_EndianConvertWord32(value);
                *(long *)(data + r->offset) = value;
            }
        }
    }
}

/* A special register's name (mfspr, mtspr). */
/* 0x00453630 */
char *DumpCode_SpecialRegister(unsigned long number) {
    return RegisterNameOf(specialRegisters, sizeof(specialRegisters) / sizeof(specialRegisters[0]), number);
}

/* A device control register's name (mfdcr, mtdcr). */
/* 0x00453540 */
char *DumpCode_DeviceControlRegister(unsigned long number) {
    return RegisterNameOf(deviceControlRegisters, 0x34, number);
}
