/* The PowerPC disassembler of -dis (DumpELFCodePPC.c's code dumps): one instruction word's mnemonic, operands and
   comment, for the processors and options the dump gives (PPCDis_Disassemble). Each primary opcode has a handler
   (data_00490ec0, by data_0048e028); opcodes 30, 31, 59 and 63 dispatch again on their extended opcodes.

   The data is the original's (generated from it: PPCDis_Data, 0x0048dfd0-0x00492280, with a macro data_XXXXXXXX for
   each object at its address), so its tables keep their layout. Built with -inline deferred, the compiler emits the
   functions in the reverse of their order here. */
#include "linker/PPCDisassembler.h"

extern int sprintf(char *s, const char *format, ...);
extern char *strcpy(char *dest, const char *source);
extern char *strcat(char *dest, const char *source);
extern unsigned char __ctype_map[];   /* MSL's: & 0xdc, a printable character */

/* A primary opcode's handler (data_00490ec0, by data_0048e028's index): UNUSED 0, INSN the instruction, PC its
   address (instruction + base), MNEMONIC the mnemonic's buffer (dis_mnemonic too). */
typedef void (*DisHandler)(long unused, unsigned long insn, long pc, char *mnemonic);

/* An extended opcode's handler, in the tables the dispatchers search (6 bytes: the opcode, the handler's index in
   data_00490ec0). */
#pragma options align = mac68k
typedef struct DisExtended {
    unsigned long xo;
    unsigned char handler;
    unsigned char pad;
} DisExtended;
#pragma options align = reset

/* The state of one instruction's disassembly (the original's addresses). */
extern char dis_started;                   /* 0x0049acf8: set on the first call */
extern char data_0049acf9[0x20];           /* text buffers (32 bytes each, the original's) */
extern char data_0049ad19[0x20];
extern char data_0049ad39[0x40];
extern char data_0049ad79[0x20];
extern char data_0049ad99[0x20];
extern char data_0049adb9[0x20];
extern char data_0049add9[0x20];
extern char data_0049adf9[0x20];
extern char data_0049ae19[0x20];
extern char data_0049ae39[0x20];
extern char data_0049ae59[0x20];
extern char data_0049ae79[0x20];
extern char data_0049ae99[0x21];           /* PPCDis_Register's */
extern char *dis_hexPrefix;                /* 0x0049aeba: "0x" (options 0x4000000) or "$" */
extern char dis_operandText[0x200];        /* 0x0049aebe: the callback's buffer */
extern long data_0049b0be;                 /* 0x0049b0be: set to 0 for each instruction */
extern long dis_operandLength;             /* 0x0049b0c2: the operands' length so far (dis_operands) */
extern short dis_flags;                    /* 0x0049b0c6: returned; 0 an invalid instruction (dc.l), 1 valid; the
                                              dump takes 1 as a branch target, 8 as a relative one ($+, $-) */
extern unsigned long dis_processors;       /* 0x0049b0c8: options & 0xfffff, or 0x6fffd for none */
extern unsigned long dis_options;          /* 0x0049b0cc: 0x80000000 little-endian, 0x10000000 numbered registers
                                              only, 0x8000000 no extended mnemonics, 0x4000000 "0x" for hex */
extern DisassemblerOperandProc dis_proc;   /* 0x0049b0d0 */
extern long dis_pc;                        /* 0x0049b0d4 */
extern long dis_base;                      /* 0x0049b0d8 */
extern unsigned char *dis_instruction;     /* 0x0049b0dc */
extern char *dis_comment;                  /* 0x0049b0e0 */
extern char *dis_mnemonic;                 /* 0x0049b0e4 */
extern char *dis_operands;                 /* 0x0049b0e8 */

/* The unit's .data (generated from the original's: data_XXXXXXXX at its address; a word array's pointers are
   its relocated words). */
extern unsigned long PPCDis_Data[4268];
#define data_0048dfd0 (*(long *)((char *)PPCDis_Data + 0x0))  /* 4 bytes */
#define data_0048dfd4 (*(long *)((char *)PPCDis_Data + 0x4))  /* 4 bytes */
#define data_0048dfd8 ((char *)PPCDis_Data + 0x8)  /* 4 bytes, "LT" */
#define data_0048dfdc ((char *)PPCDis_Data + 0xc)  /* 4 bytes, "GT" */
#define data_0048dfe0 ((char *)PPCDis_Data + 0x10)  /* 4 bytes, "EQ" */
#define data_0048dfe4 ((char *)PPCDis_Data + 0x14)  /* 4 bytes, "SO" */
#define data_0048dfe8 ((unsigned long *)((char *)PPCDis_Data + 0x18))  /* 16 bytes, 4 pointers: data_0048dfd8, data_0048dfdc, data_0048dfe0, data_0048dfe4 */
#define data_0048dff8 ((unsigned long *)((char *)PPCDis_Data + 0x28))  /* 16 bytes, 4 pointers: data_0048dfd8, data_0048dfdc, data_0048dfe0, data_0048dfe4 */
#define data_0048e008 ((char *)PPCDis_Data + 0x38)  /* 4 bytes, "GE" */
#define data_0048e00c ((char *)PPCDis_Data + 0x3c)  /* 4 bytes, "LE" */
#define data_0048e010 ((char *)PPCDis_Data + 0x40)  /* 4 bytes, "NE" */
#define data_0048e014 ((char *)PPCDis_Data + 0x44)  /* 4 bytes, "NS" */
#define data_0048e018 ((unsigned long *)((char *)PPCDis_Data + 0x48))  /* 16 bytes, 4 pointers: data_0048e008, data_0048e00c, data_0048e010, data_0048e014 */
#define data_0048e028 ((char *)PPCDis_Data + 0x58)  /* 64 bytes, 64 bytes */
#define data_0048e068 ((char *)PPCDis_Data + 0x98)  /* 4 bytes, "b" */
#define data_0048e06c ((char *)PPCDis_Data + 0x9c)  /* 4 bytes, "bl" */
#define data_0048e070 ((char *)PPCDis_Data + 0xa0)  /* 4 bytes, "ba" */
#define data_0048e074 ((char *)PPCDis_Data + 0xa4)  /* 4 bytes, "bla" */
#define data_0048e078 ((unsigned long *)((char *)PPCDis_Data + 0xa8))  /* 16 bytes, 4 pointers: data_0048e068, data_0048e06c, data_0048e070, data_0048e074 */
#define data_0048e088 ((char *)PPCDis_Data + 0xb8)  /* 4 bytes, "bc" */
#define data_0048e08c ((char *)PPCDis_Data + 0xbc)  /* 4 bytes, "bcl" */
#define data_0048e090 ((char *)PPCDis_Data + 0xc0)  /* 4 bytes, "bca" */
#define data_0048e094 ((char *)PPCDis_Data + 0xc4)  /* 8 bytes, "bcla" */
#define data_0048e09c ((unsigned long *)((char *)PPCDis_Data + 0xcc))  /* 16 bytes, 4 pointers: data_0048e088, data_0048e08c, data_0048e090, data_0048e094 */
#define data_0048e0ac ((char *)PPCDis_Data + 0xdc)  /* 4 bytes, "blt" */
#define data_0048e0b0 ((char *)PPCDis_Data + 0xe0)  /* 4 bytes, "bgt" */
#define data_0048e0b4 ((char *)PPCDis_Data + 0xe4)  /* 4 bytes, "beq" */
#define data_0048e0b8 ((char *)PPCDis_Data + 0xe8)  /* 4 bytes, "bso" */
#define data_0048e0bc ((char *)PPCDis_Data + 0xec)  /* 4 bytes, "bge" */
#define data_0048e0c0 ((char *)PPCDis_Data + 0xf0)  /* 4 bytes, "ble" */
#define data_0048e0c4 ((char *)PPCDis_Data + 0xf4)  /* 4 bytes, "bne" */
#define data_0048e0c8 ((char *)PPCDis_Data + 0xf8)  /* 4 bytes, "bns" */
#define data_0048e0cc ((unsigned long *)((char *)PPCDis_Data + 0xfc))  /* 32 bytes, 8 pointers: data_0048e0ac, data_0048e0b0, data_0048e0b4, data_0048e0b8 ... */
#define data_0048e0ec ((char *)PPCDis_Data + 0x11c)  /* 64 bytes, 64 bytes */
#define data_0048e12c ((char *)PPCDis_Data + 0x15c)  /* 4 bytes, "tdi" */
#define data_0048e130 ((char *)PPCDis_Data + 0x160)  /* 4 bytes, "twi" */
#define data_0048e134 ((char *)PPCDis_Data + 0x164)  /* 8 bytes, "mulli" */
#define data_0048e13c ((char *)PPCDis_Data + 0x16c)  /* 8 bytes, "subfic" */
#define data_0048e144 ((char *)PPCDis_Data + 0x174)  /* 8 bytes, "dozi" */
#define data_0048e14c ((char *)PPCDis_Data + 0x17c)  /* 8 bytes, "cmpli" */
#define data_0048e154 ((char *)PPCDis_Data + 0x184)  /* 8 bytes, "cmpi" */
#define data_0048e15c ((char *)PPCDis_Data + 0x18c)  /* 8 bytes, "addic" */
#define data_0048e164 ((char *)PPCDis_Data + 0x194)  /* 8 bytes, "addic." */
#define data_0048e16c ((char *)PPCDis_Data + 0x19c)  /* 8 bytes, "addi" */
#define data_0048e174 ((char *)PPCDis_Data + 0x1a4)  /* 8 bytes, "addis" */
#define data_0048e17c ((char *)PPCDis_Data + 0x1ac)  /* 4 bytes, "ori" */
#define data_0048e180 ((char *)PPCDis_Data + 0x1b0)  /* 8 bytes, "oris" */
#define data_0048e188 ((char *)PPCDis_Data + 0x1b8)  /* 8 bytes, "xori" */
#define data_0048e190 ((char *)PPCDis_Data + 0x1c0)  /* 8 bytes, "xoris" */
#define data_0048e198 ((char *)PPCDis_Data + 0x1c8)  /* 8 bytes, "andi." */
#define data_0048e1a0 ((char *)PPCDis_Data + 0x1d0)  /* 8 bytes, "andis." */
#define data_0048e1a8 ((char *)PPCDis_Data + 0x1d8)  /* 4 bytes, "lwz" */
#define data_0048e1ac ((char *)PPCDis_Data + 0x1dc)  /* 8 bytes, "lwzu" */
#define data_0048e1b4 ((char *)PPCDis_Data + 0x1e4)  /* 4 bytes, "lbz" */
#define data_0048e1b8 ((char *)PPCDis_Data + 0x1e8)  /* 8 bytes, "lbzu" */
#define data_0048e1c0 ((char *)PPCDis_Data + 0x1f0)  /* 4 bytes, "stw" */
#define data_0048e1c4 ((char *)PPCDis_Data + 0x1f4)  /* 8 bytes, "stwu" */
#define data_0048e1cc ((char *)PPCDis_Data + 0x1fc)  /* 4 bytes, "stb" */
#define data_0048e1d0 ((char *)PPCDis_Data + 0x200)  /* 8 bytes, "stbu" */
#define data_0048e1d8 ((char *)PPCDis_Data + 0x208)  /* 4 bytes, "lhz" */
#define data_0048e1dc ((char *)PPCDis_Data + 0x20c)  /* 8 bytes, "lhzu" */
#define data_0048e1e4 ((char *)PPCDis_Data + 0x214)  /* 4 bytes, "lha" */
#define data_0048e1e8 ((char *)PPCDis_Data + 0x218)  /* 8 bytes, "lhau" */
#define data_0048e1f0 ((char *)PPCDis_Data + 0x220)  /* 4 bytes, "sth" */
#define data_0048e1f4 ((char *)PPCDis_Data + 0x224)  /* 8 bytes, "sthu" */
#define data_0048e1fc ((char *)PPCDis_Data + 0x22c)  /* 4 bytes, "lmw" */
#define data_0048e200 ((char *)PPCDis_Data + 0x230)  /* 8 bytes, "stmw" */
#define data_0048e208 ((char *)PPCDis_Data + 0x238)  /* 4 bytes, "lfs" */
#define data_0048e20c ((char *)PPCDis_Data + 0x23c)  /* 8 bytes, "lfsu" */
#define data_0048e214 ((char *)PPCDis_Data + 0x244)  /* 4 bytes, "lfd" */
#define data_0048e218 ((char *)PPCDis_Data + 0x248)  /* 8 bytes, "lfdu" */
#define data_0048e220 ((char *)PPCDis_Data + 0x250)  /* 8 bytes, "stfs" */
#define data_0048e228 ((char *)PPCDis_Data + 0x258)  /* 8 bytes, "stfsu" */
#define data_0048e230 ((char *)PPCDis_Data + 0x260)  /* 8 bytes, "stfd" */
#define data_0048e238 ((char *)PPCDis_Data + 0x268)  /* 8 bytes, "stfdu" */
#define data_0048e240 ((char *)PPCDis_Data + 0x270)  /* 8 bytes, "psq_l" */
#define data_0048e248 ((char *)PPCDis_Data + 0x278)  /* 8 bytes, "psq_lu" */
#define data_0048e250 ((char *)PPCDis_Data + 0x280)  /* 8 bytes, "psq_st" */
#define data_0048e258 ((char *)PPCDis_Data + 0x288)  /* 8 bytes, "psq_stu" */
#define data_0048e260 ((unsigned long *)((char *)PPCDis_Data + 0x290))  /* 248 bytes, 45 pointers: data_0048e12c, data_0048e130, data_0048e134, data_0048e13c ... */
#define data_0048e358 ((char *)PPCDis_Data + 0x388)  /* 4 bytes, "ld" */
#define data_0048e35c ((char *)PPCDis_Data + 0x38c)  /* 4 bytes, "ldu" */
#define data_0048e360 ((char *)PPCDis_Data + 0x390)  /* 4 bytes, "lwa" */
#define data_0048e364 ((unsigned long *)((char *)PPCDis_Data + 0x394))  /* 12 bytes, 3 pointers: data_0048e358, data_0048e35c, data_0048e360 */
#define data_0048e370 ((char *)PPCDis_Data + 0x3a0)  /* 8 bytes, "?????" */
#define data_0048e378 ((unsigned long *)((char *)PPCDis_Data + 0x3a8))  /* 16 bytes, 1 pointers: data_0048e370 */
#define data_0048e388 ((char *)PPCDis_Data + 0x3b8)  /* 4 bytes, "cmp" */
#define data_0048e38c ((char *)PPCDis_Data + 0x3bc)  /* 4 bytes, "tw" */
#define data_0048e390 ((char *)PPCDis_Data + 0x3c0)  /* 8 bytes, "lvsl" */
#define data_0048e398 ((char *)PPCDis_Data + 0x3c8)  /* 8 bytes, "lvebx" */
#define data_0048e3a0 ((char *)PPCDis_Data + 0x3d0)  /* 8 bytes, "mfcr" */
#define data_0048e3a8 ((char *)PPCDis_Data + 0x3d8)  /* 8 bytes, "lwarx" */
#define data_0048e3b0 ((char *)PPCDis_Data + 0x3e0)  /* 4 bytes, "ldx" */
#define data_0048e3b4 ((char *)PPCDis_Data + 0x3e4)  /* 8 bytes, "lwzx" */
#define data_0048e3bc ((char *)PPCDis_Data + 0x3ec)  /* 4 bytes, "slw" */
#define data_0048e3c0 ((char *)PPCDis_Data + 0x3f0)  /* 8 bytes, "cntlzw" */
#define data_0048e3c8 ((char *)PPCDis_Data + 0x3f8)  /* 4 bytes, "sld" */
#define data_0048e3cc ((char *)PPCDis_Data + 0x3fc)  /* 4 bytes, "and" */
#define data_0048e3d0 ((char *)PPCDis_Data + 0x400)  /* 8 bytes, "maskg" */
#define data_0048e3d8 ((char *)PPCDis_Data + 0x408)  /* 8 bytes, "cmpl" */
#define data_0048e3e0 ((char *)PPCDis_Data + 0x410)  /* 8 bytes, "lvsr" */
#define data_0048e3e8 ((char *)PPCDis_Data + 0x418)  /* 8 bytes, "lvehx" */
#define data_0048e3f0 ((char *)PPCDis_Data + 0x420)  /* 8 bytes, "ldux" */
#define data_0048e3f8 ((char *)PPCDis_Data + 0x428)  /* 8 bytes, "dcbst" */
#define data_0048e400 ((char *)PPCDis_Data + 0x430)  /* 8 bytes, "lwzux" */
#define data_0048e408 ((char *)PPCDis_Data + 0x438)  /* 8 bytes, "cntlzd" */
#define data_0048e410 ((char *)PPCDis_Data + 0x440)  /* 8 bytes, "andc" */
#define data_0048e418 ((char *)PPCDis_Data + 0x448)  /* 4 bytes, "td" */
#define data_0048e41c ((char *)PPCDis_Data + 0x44c)  /* 8 bytes, "lvewx" */
#define data_0048e424 ((char *)PPCDis_Data + 0x454)  /* 8 bytes, "mfmsr" */
#define data_0048e42c ((char *)PPCDis_Data + 0x45c)  /* 8 bytes, "ldarx" */
#define data_0048e434 ((char *)PPCDis_Data + 0x464)  /* 8 bytes, "dcbf" */
#define data_0048e43c ((char *)PPCDis_Data + 0x46c)  /* 8 bytes, "lbzx" */
#define data_0048e444 ((char *)PPCDis_Data + 0x474)  /* 4 bytes, "lvx" */
#define data_0048e448 ((char *)PPCDis_Data + 0x478)  /* 8 bytes, "lbzux" */
#define data_0048e450 ((char *)PPCDis_Data + 0x480)  /* 4 bytes, "nor" */
#define data_0048e454 ((char *)PPCDis_Data + 0x484)  /* 8 bytes, "wrtee" */
#define data_0048e45c ((char *)PPCDis_Data + 0x48c)  /* 8 bytes, "stvebx" */
#define data_0048e464 ((char *)PPCDis_Data + 0x494)  /* 8 bytes, "mtmsr" */
#define data_0048e46c ((char *)PPCDis_Data + 0x49c)  /* 8 bytes, "stdx" */
#define data_0048e474 ((char *)PPCDis_Data + 0x4a4)  /* 8 bytes, "stwcx" */
#define data_0048e47c ((char *)PPCDis_Data + 0x4ac)  /* 8 bytes, "stwx" */
#define data_0048e484 ((char *)PPCDis_Data + 0x4b4)  /* 4 bytes, "slq" */
#define data_0048e488 ((char *)PPCDis_Data + 0x4b8)  /* 4 bytes, "sle" */
#define data_0048e48c ((char *)PPCDis_Data + 0x4bc)  /* 8 bytes, "wrteei" */
#define data_0048e494 ((char *)PPCDis_Data + 0x4c4)  /* 8 bytes, "stvehx" */
#define data_0048e49c ((char *)PPCDis_Data + 0x4cc)  /* 8 bytes, "stdux" */
#define data_0048e4a4 ((char *)PPCDis_Data + 0x4d4)  /* 8 bytes, "stwux" */
#define data_0048e4ac ((char *)PPCDis_Data + 0x4dc)  /* 8 bytes, "sliq" */
#define data_0048e4b4 ((char *)PPCDis_Data + 0x4e4)  /* 8 bytes, "stvewx" */
#define data_0048e4bc ((char *)PPCDis_Data + 0x4ec)  /* 8 bytes, "mtsr" */
#define data_0048e4c4 ((char *)PPCDis_Data + 0x4f4)  /* 8 bytes, "stdcx" */
#define data_0048e4cc ((char *)PPCDis_Data + 0x4fc)  /* 8 bytes, "stbx" */
#define data_0048e4d4 ((char *)PPCDis_Data + 0x504)  /* 8 bytes, "sllq" */
#define data_0048e4dc ((char *)PPCDis_Data + 0x50c)  /* 8 bytes, "sleq" */
#define data_0048e4e4 ((char *)PPCDis_Data + 0x514)  /* 8 bytes, "stvx" */
#define data_0048e4ec ((char *)PPCDis_Data + 0x51c)  /* 8 bytes, "mtsrin" */
#define data_0048e4f4 ((char *)PPCDis_Data + 0x524)  /* 8 bytes, "dcbtst" */
#define data_0048e4fc ((char *)PPCDis_Data + 0x52c)  /* 8 bytes, "stbux" */
#define data_0048e504 ((char *)PPCDis_Data + 0x534)  /* 8 bytes, "slliq" */
#define data_0048e50c ((char *)PPCDis_Data + 0x53c)  /* 8 bytes, "icbt" */
#define data_0048e514 ((char *)PPCDis_Data + 0x544)  /* 8 bytes, "lscbx" */
#define data_0048e51c ((char *)PPCDis_Data + 0x54c)  /* 8 bytes, "dcbt" */
#define data_0048e524 ((char *)PPCDis_Data + 0x554)  /* 8 bytes, "lhzx" */
#define data_0048e52c ((char *)PPCDis_Data + 0x55c)  /* 4 bytes, "eqv" */
#define data_0048e530 ((char *)PPCDis_Data + 0x560)  /* 8 bytes, "tlbie" */
#define data_0048e538 ((char *)PPCDis_Data + 0x568)  /* 8 bytes, "eciwx" */
#define data_0048e540 ((char *)PPCDis_Data + 0x570)  /* 8 bytes, "lhzux" */
#define data_0048e548 ((char *)PPCDis_Data + 0x578)  /* 4 bytes, "xor" */
#define data_0048e54c ((char *)PPCDis_Data + 0x57c)  /* 8 bytes, "lwax" */
#define data_0048e554 ((char *)PPCDis_Data + 0x584)  /* 4 bytes, "dst" */
#define data_0048e558 ((char *)PPCDis_Data + 0x588)  /* 8 bytes, "lhax" */
#define data_0048e560 ((char *)PPCDis_Data + 0x590)  /* 8 bytes, "lvxl" */
#define data_0048e568 ((char *)PPCDis_Data + 0x598)  /* 8 bytes, "tlbia" */
#define data_0048e570 ((char *)PPCDis_Data + 0x5a0)  /* 8 bytes, "lwaux" */
#define data_0048e578 ((char *)PPCDis_Data + 0x5a8)  /* 8 bytes, "dstst" */
#define data_0048e580 ((char *)PPCDis_Data + 0x5b0)  /* 8 bytes, "lhaux" */
#define data_0048e588 ((char *)PPCDis_Data + 0x5b8)  /* 8 bytes, "sthx" */
#define data_0048e590 ((char *)PPCDis_Data + 0x5c0)  /* 4 bytes, "orc" */
#define data_0048e594 ((char *)PPCDis_Data + 0x5c4)  /* 8 bytes, "slbie" */
#define data_0048e59c ((char *)PPCDis_Data + 0x5cc)  /* 8 bytes, "ecowx" */
#define data_0048e5a4 ((char *)PPCDis_Data + 0x5d4)  /* 8 bytes, "sthux" */
#define data_0048e5ac ((char *)PPCDis_Data + 0x5dc)  /* 4 bytes, "or" */
#define data_0048e5b0 ((char *)PPCDis_Data + 0x5e0)  /* 8 bytes, "dccci" */
#define data_0048e5b8 ((char *)PPCDis_Data + 0x5e8)  /* 8 bytes, "dcbi" */
#define data_0048e5c0 ((char *)PPCDis_Data + 0x5f0)  /* 8 bytes, "nand" */
#define data_0048e5c8 ((char *)PPCDis_Data + 0x5f8)  /* 8 bytes, "dcread" */
#define data_0048e5d0 ((char *)PPCDis_Data + 0x600)  /* 8 bytes, "stvxl" */
#define data_0048e5d8 ((char *)PPCDis_Data + 0x608)  /* 8 bytes, "slbia" */
#define data_0048e5e0 ((char *)PPCDis_Data + 0x610)  /* 8 bytes, "mcrxr" */
#define data_0048e5e8 ((char *)PPCDis_Data + 0x618)  /* 8 bytes, "lswx" */
#define data_0048e5f0 ((char *)PPCDis_Data + 0x620)  /* 8 bytes, "lwbrx" */
#define data_0048e5f8 ((char *)PPCDis_Data + 0x628)  /* 8 bytes, "lfsx" */
#define data_0048e600 ((char *)PPCDis_Data + 0x630)  /* 4 bytes, "srw" */
#define data_0048e604 ((char *)PPCDis_Data + 0x634)  /* 8 bytes, "rrib" */
#define data_0048e60c ((char *)PPCDis_Data + 0x63c)  /* 4 bytes, "srd" */
#define data_0048e610 ((char *)PPCDis_Data + 0x640)  /* 8 bytes, "maskir" */
#define data_0048e618 ((char *)PPCDis_Data + 0x648)  /* 8 bytes, "tlbsync" */
#define data_0048e620 ((char *)PPCDis_Data + 0x650)  /* 8 bytes, "lfsux" */
#define data_0048e628 ((char *)PPCDis_Data + 0x658)  /* 8 bytes, "mfsr" */
#define data_0048e630 ((char *)PPCDis_Data + 0x660)  /* 4 bytes, "esa" */
#define data_0048e634 ((char *)PPCDis_Data + 0x664)  /* 8 bytes, "lswi" */
#define data_0048e63c ((char *)PPCDis_Data + 0x66c)  /* 8 bytes, "sync" */
#define data_0048e644 ((char *)PPCDis_Data + 0x674)  /* 8 bytes, "lfdx" */
#define data_0048e64c ((char *)PPCDis_Data + 0x67c)  /* 4 bytes, "dsa" */
#define data_0048e650 ((char *)PPCDis_Data + 0x680)  /* 8 bytes, "lfdux" */
#define data_0048e658 ((char *)PPCDis_Data + 0x688)  /* 8 bytes, "mfsrin" */
#define data_0048e660 ((char *)PPCDis_Data + 0x690)  /* 8 bytes, "stswx" */
#define data_0048e668 ((char *)PPCDis_Data + 0x698)  /* 8 bytes, "stwbrx" */
#define data_0048e670 ((char *)PPCDis_Data + 0x6a0)  /* 8 bytes, "stfsx" */
#define data_0048e678 ((char *)PPCDis_Data + 0x6a8)  /* 4 bytes, "srq" */
#define data_0048e67c ((char *)PPCDis_Data + 0x6ac)  /* 4 bytes, "sre" */
#define data_0048e680 ((char *)PPCDis_Data + 0x6b0)  /* 8 bytes, "stfsux" */
#define data_0048e688 ((char *)PPCDis_Data + 0x6b8)  /* 8 bytes, "sriq" */
#define data_0048e690 ((char *)PPCDis_Data + 0x6c0)  /* 8 bytes, "stswi" */
#define data_0048e698 ((char *)PPCDis_Data + 0x6c8)  /* 8 bytes, "stfdx" */
#define data_0048e6a0 ((char *)PPCDis_Data + 0x6d0)  /* 8 bytes, "srlq" */
#define data_0048e6a8 ((char *)PPCDis_Data + 0x6d8)  /* 8 bytes, "sreq" */
#define data_0048e6b0 ((char *)PPCDis_Data + 0x6e0)  /* 8 bytes, "stfdux" */
#define data_0048e6b8 ((char *)PPCDis_Data + 0x6e8)  /* 8 bytes, "srliq" */
#define data_0048e6c0 ((char *)PPCDis_Data + 0x6f0)  /* 8 bytes, "lhbrx" */
#define data_0048e6c8 ((char *)PPCDis_Data + 0x6f8)  /* 8 bytes, "sraw" */
#define data_0048e6d0 ((char *)PPCDis_Data + 0x700)  /* 8 bytes, "srad" */
#define data_0048e6d8 ((char *)PPCDis_Data + 0x708)  /* 4 bytes, "dss" */
#define data_0048e6dc ((char *)PPCDis_Data + 0x70c)  /* 8 bytes, "srawi" */
#define data_0048e6e4 ((char *)PPCDis_Data + 0x714)  /* 8 bytes, "eieio" */
#define data_0048e6ec ((char *)PPCDis_Data + 0x71c)  /* 8 bytes, "tlbsx" */
#define data_0048e6f4 ((char *)PPCDis_Data + 0x724)  /* 8 bytes, "sthbrx" */
#define data_0048e6fc ((char *)PPCDis_Data + 0x72c)  /* 8 bytes, "sraq" */
#define data_0048e704 ((char *)PPCDis_Data + 0x734)  /* 8 bytes, "srea" */
#define data_0048e70c ((char *)PPCDis_Data + 0x73c)  /* 8 bytes, "extsh" */
#define data_0048e714 ((char *)PPCDis_Data + 0x744)  /* 8 bytes, "tlbre" */
#define data_0048e71c ((char *)PPCDis_Data + 0x74c)  /* 8 bytes, "sraiq" */
#define data_0048e724 ((char *)PPCDis_Data + 0x754)  /* 8 bytes, "extsb" */
#define data_0048e72c ((char *)PPCDis_Data + 0x75c)  /* 8 bytes, "iccci" */
#define data_0048e734 ((char *)PPCDis_Data + 0x764)  /* 8 bytes, "tlbld" */
#define data_0048e73c ((char *)PPCDis_Data + 0x76c)  /* 8 bytes, "icbi" */
#define data_0048e744 ((char *)PPCDis_Data + 0x774)  /* 8 bytes, "stfiwx" */
#define data_0048e74c ((char *)PPCDis_Data + 0x77c)  /* 8 bytes, "extsw" */
#define data_0048e754 ((char *)PPCDis_Data + 0x784)  /* 8 bytes, "icread" */
#define data_0048e75c ((char *)PPCDis_Data + 0x78c)  /* 8 bytes, "tlbli" */
#define data_0048e764 ((char *)PPCDis_Data + 0x794)  /* 8 bytes, "dcbz" */
#define data_0048e76c ((unsigned long *)((char *)PPCDis_Data + 0x79c))  /* 2176 bytes, 136 pointers: data_0048e388, data_0048e38c, data_0048e390, data_0048e398 ... */
#define data_0048efec ((char *)PPCDis_Data + 0x101c)  /* 8 bytes, "fcmpu" */
#define data_0048eff4 ((char *)PPCDis_Data + 0x1024)  /* 8 bytes, "frsp" */
#define data_0048effc ((char *)PPCDis_Data + 0x102c)  /* 8 bytes, "fctiw" */
#define data_0048f004 ((char *)PPCDis_Data + 0x1034)  /* 8 bytes, "fctiwz" */
#define data_0048f00c ((char *)PPCDis_Data + 0x103c)  /* 8 bytes, "fcmpo" */
#define data_0048f014 ((char *)PPCDis_Data + 0x1044)  /* 8 bytes, "mtfsb1" */
#define data_0048f01c ((char *)PPCDis_Data + 0x104c)  /* 8 bytes, "fneg" */
#define data_0048f024 ((char *)PPCDis_Data + 0x1054)  /* 8 bytes, "mcrfs" */
#define data_0048f02c ((char *)PPCDis_Data + 0x105c)  /* 8 bytes, "mtfsb0" */
#define data_0048f034 ((char *)PPCDis_Data + 0x1064)  /* 4 bytes, "fmr" */
#define data_0048f038 ((char *)PPCDis_Data + 0x1068)  /* 8 bytes, "mtfsfi" */
#define data_0048f040 ((char *)PPCDis_Data + 0x1070)  /* 8 bytes, "fnabs" */
#define data_0048f048 ((char *)PPCDis_Data + 0x1078)  /* 8 bytes, "fabs" */
#define data_0048f050 ((char *)PPCDis_Data + 0x1080)  /* 8 bytes, "mffs" */
#define data_0048f058 ((char *)PPCDis_Data + 0x1088)  /* 8 bytes, "fctid" */
#define data_0048f060 ((char *)PPCDis_Data + 0x1090)  /* 8 bytes, "fctidz" */
#define data_0048f068 ((char *)PPCDis_Data + 0x1098)  /* 8 bytes, "fcfid" */
#define data_0048f070 ((unsigned long *)((char *)PPCDis_Data + 0x10a0))  /* 272 bytes, 17 pointers: data_0048efec, data_0048eff4, data_0048effc, data_0048f004 ... */
#define data_0048f180 ((char *)PPCDis_Data + 0x11b0)  /* 8 bytes, "vaddubm" */
#define data_0048f188 ((char *)PPCDis_Data + 0x11b8)  /* 8 bytes, "vmaxub" */
#define data_0048f190 ((char *)PPCDis_Data + 0x11c0)  /* 8 bytes, "vrlb" */
#define data_0048f198 ((char *)PPCDis_Data + 0x11c8)  /* 8 bytes, "vmuloub" */
#define data_0048f1a0 ((char *)PPCDis_Data + 0x11d0)  /* 8 bytes, "vaddfp" */
#define data_0048f1a8 ((char *)PPCDis_Data + 0x11d8)  /* 8 bytes, "vmrghb" */
#define data_0048f1b0 ((char *)PPCDis_Data + 0x11e0)  /* 8 bytes, "vpkuhum" */
#define data_0048f1b8 ((char *)PPCDis_Data + 0x11e8)  /* 8 bytes, "vadduhm" */
#define data_0048f1c0 ((char *)PPCDis_Data + 0x11f0)  /* 8 bytes, "vmaxuh" */
#define data_0048f1c8 ((char *)PPCDis_Data + 0x11f8)  /* 8 bytes, "vrlh" */
#define data_0048f1d0 ((char *)PPCDis_Data + 0x1200)  /* 8 bytes, "vmulouh" */
#define data_0048f1d8 ((char *)PPCDis_Data + 0x1208)  /* 8 bytes, "vsubfp" */
#define data_0048f1e0 ((char *)PPCDis_Data + 0x1210)  /* 8 bytes, "vmrghh" */
#define data_0048f1e8 ((char *)PPCDis_Data + 0x1218)  /* 8 bytes, "vpkuwum" */
#define data_0048f1f0 ((char *)PPCDis_Data + 0x1220)  /* 8 bytes, "vadduwm" */
#define data_0048f1f8 ((char *)PPCDis_Data + 0x1228)  /* 8 bytes, "vmaxuw" */
#define data_0048f200 ((char *)PPCDis_Data + 0x1230)  /* 8 bytes, "vrlw" */
#define data_0048f208 ((char *)PPCDis_Data + 0x1238)  /* 8 bytes, "vmrghw" */
#define data_0048f210 ((char *)PPCDis_Data + 0x1240)  /* 8 bytes, "vpkuhus" */
#define data_0048f218 ((char *)PPCDis_Data + 0x1248)  /* 8 bytes, "vpkuwus" */
#define data_0048f220 ((char *)PPCDis_Data + 0x1250)  /* 8 bytes, "vmaxsb" */
#define data_0048f228 ((char *)PPCDis_Data + 0x1258)  /* 8 bytes, "vslb" */
#define data_0048f230 ((char *)PPCDis_Data + 0x1260)  /* 8 bytes, "vmulosb" */
#define data_0048f238 ((char *)PPCDis_Data + 0x1268)  /* 8 bytes, "vrefp" */
#define data_0048f240 ((char *)PPCDis_Data + 0x1270)  /* 8 bytes, "vmrglb" */
#define data_0048f248 ((char *)PPCDis_Data + 0x1278)  /* 8 bytes, "vpkshus" */
#define data_0048f250 ((char *)PPCDis_Data + 0x1280)  /* 8 bytes, "vmaxsh" */
#define data_0048f258 ((char *)PPCDis_Data + 0x1288)  /* 8 bytes, "vslh" */
#define data_0048f260 ((char *)PPCDis_Data + 0x1290)  /* 8 bytes, "vmulosh" */
#define data_0048f268 ((char *)PPCDis_Data + 0x1298)  /* 12 bytes, "vrsqrtefp" */
#define data_0048f274 ((char *)PPCDis_Data + 0x12a4)  /* 8 bytes, "vmrglh" */
#define data_0048f27c ((char *)PPCDis_Data + 0x12ac)  /* 8 bytes, "vpkswus" */
#define data_0048f284 ((char *)PPCDis_Data + 0x12b4)  /* 8 bytes, "vaddcuw" */
#define data_0048f28c ((char *)PPCDis_Data + 0x12bc)  /* 8 bytes, "vmaxsw" */
#define data_0048f294 ((char *)PPCDis_Data + 0x12c4)  /* 8 bytes, "vslw" */
#define data_0048f29c ((char *)PPCDis_Data + 0x12cc)  /* 12 bytes, "vexptefp" */
#define data_0048f2a8 ((char *)PPCDis_Data + 0x12d8)  /* 8 bytes, "vmrglw" */
#define data_0048f2b0 ((char *)PPCDis_Data + 0x12e0)  /* 8 bytes, "vpkshss" */
#define data_0048f2b8 ((char *)PPCDis_Data + 0x12e8)  /* 4 bytes, "vsl" */
#define data_0048f2bc ((char *)PPCDis_Data + 0x12ec)  /* 8 bytes, "vlogefp" */
#define data_0048f2c4 ((char *)PPCDis_Data + 0x12f4)  /* 8 bytes, "vpkswss" */
#define data_0048f2cc ((char *)PPCDis_Data + 0x12fc)  /* 8 bytes, "vaddubs" */
#define data_0048f2d4 ((char *)PPCDis_Data + 0x1304)  /* 8 bytes, "vminub" */
#define data_0048f2dc ((char *)PPCDis_Data + 0x130c)  /* 8 bytes, "vsrb" */
#define data_0048f2e4 ((char *)PPCDis_Data + 0x1314)  /* 8 bytes, "vmuleub" */
#define data_0048f2ec ((char *)PPCDis_Data + 0x131c)  /* 8 bytes, "vrfin" */
#define data_0048f2f4 ((char *)PPCDis_Data + 0x1324)  /* 8 bytes, "vspltb" */
#define data_0048f2fc ((char *)PPCDis_Data + 0x132c)  /* 8 bytes, "vupkhsb" */
#define data_0048f304 ((char *)PPCDis_Data + 0x1334)  /* 8 bytes, "vadduhs" */
#define data_0048f30c ((char *)PPCDis_Data + 0x133c)  /* 8 bytes, "vminuh" */
#define data_0048f314 ((char *)PPCDis_Data + 0x1344)  /* 8 bytes, "vsrh" */
#define data_0048f31c ((char *)PPCDis_Data + 0x134c)  /* 8 bytes, "vmuleuh" */
#define data_0048f324 ((char *)PPCDis_Data + 0x1354)  /* 8 bytes, "vrfiz" */
#define data_0048f32c ((char *)PPCDis_Data + 0x135c)  /* 8 bytes, "vsplth" */
#define data_0048f334 ((char *)PPCDis_Data + 0x1364)  /* 8 bytes, "vupkhsh" */
#define data_0048f33c ((char *)PPCDis_Data + 0x136c)  /* 8 bytes, "vadduws" */
#define data_0048f344 ((char *)PPCDis_Data + 0x1374)  /* 8 bytes, "vminuw" */
#define data_0048f34c ((char *)PPCDis_Data + 0x137c)  /* 8 bytes, "vsrw" */
#define data_0048f354 ((char *)PPCDis_Data + 0x1384)  /* 8 bytes, "vrfip" */
#define data_0048f35c ((char *)PPCDis_Data + 0x138c)  /* 8 bytes, "vspltw" */
#define data_0048f364 ((char *)PPCDis_Data + 0x1394)  /* 8 bytes, "vupklsb" */
#define data_0048f36c ((char *)PPCDis_Data + 0x139c)  /* 4 bytes, "vsr" */
#define data_0048f370 ((char *)PPCDis_Data + 0x13a0)  /* 8 bytes, "vrfim" */
#define data_0048f378 ((char *)PPCDis_Data + 0x13a8)  /* 8 bytes, "vupklsh" */
#define data_0048f380 ((char *)PPCDis_Data + 0x13b0)  /* 8 bytes, "vaddsbs" */
#define data_0048f388 ((char *)PPCDis_Data + 0x13b8)  /* 8 bytes, "vminsb" */
#define data_0048f390 ((char *)PPCDis_Data + 0x13c0)  /* 8 bytes, "vsrab" */
#define data_0048f398 ((char *)PPCDis_Data + 0x13c8)  /* 8 bytes, "vmulesb" */
#define data_0048f3a0 ((char *)PPCDis_Data + 0x13d0)  /* 8 bytes, "vcfux" */
#define data_0048f3a8 ((char *)PPCDis_Data + 0x13d8)  /* 12 bytes, "vspltisb" */
#define data_0048f3b4 ((char *)PPCDis_Data + 0x13e4)  /* 8 bytes, "vpkpx" */
#define data_0048f3bc ((char *)PPCDis_Data + 0x13ec)  /* 8 bytes, "vaddshs" */
#define data_0048f3c4 ((char *)PPCDis_Data + 0x13f4)  /* 8 bytes, "vminsh" */
#define data_0048f3cc ((char *)PPCDis_Data + 0x13fc)  /* 8 bytes, "vsrah" */
#define data_0048f3d4 ((char *)PPCDis_Data + 0x1404)  /* 8 bytes, "vmulesh" */
#define data_0048f3dc ((char *)PPCDis_Data + 0x140c)  /* 8 bytes, "vcfsx" */
#define data_0048f3e4 ((char *)PPCDis_Data + 0x1414)  /* 12 bytes, "vspltish" */
#define data_0048f3f0 ((char *)PPCDis_Data + 0x1420)  /* 8 bytes, "vupkhpx" */
#define data_0048f3f8 ((char *)PPCDis_Data + 0x1428)  /* 8 bytes, "vaddsws" */
#define data_0048f400 ((char *)PPCDis_Data + 0x1430)  /* 8 bytes, "vminsw" */
#define data_0048f408 ((char *)PPCDis_Data + 0x1438)  /* 8 bytes, "vsraw" */
#define data_0048f410 ((char *)PPCDis_Data + 0x1440)  /* 8 bytes, "vctuxs" */
#define data_0048f418 ((char *)PPCDis_Data + 0x1448)  /* 12 bytes, "vspltisw" */
#define data_0048f424 ((char *)PPCDis_Data + 0x1454)  /* 8 bytes, "vctsxs" */
#define data_0048f42c ((char *)PPCDis_Data + 0x145c)  /* 8 bytes, "vupklpx" */
#define data_0048f434 ((char *)PPCDis_Data + 0x1464)  /* 8 bytes, "vsububm" */
#define data_0048f43c ((char *)PPCDis_Data + 0x146c)  /* 8 bytes, "vavgub" */
#define data_0048f444 ((char *)PPCDis_Data + 0x1474)  /* 8 bytes, "vand" */
#define data_0048f44c ((char *)PPCDis_Data + 0x147c)  /* 8 bytes, "vmaxfp" */
#define data_0048f454 ((char *)PPCDis_Data + 0x1484)  /* 8 bytes, "vslo" */
#define data_0048f45c ((char *)PPCDis_Data + 0x148c)  /* 8 bytes, "vsubuhm" */
#define data_0048f464 ((char *)PPCDis_Data + 0x1494)  /* 8 bytes, "vavguh" */
#define data_0048f46c ((char *)PPCDis_Data + 0x149c)  /* 8 bytes, "vandc" */
#define data_0048f474 ((char *)PPCDis_Data + 0x14a4)  /* 8 bytes, "vminfp" */
#define data_0048f47c ((char *)PPCDis_Data + 0x14ac)  /* 8 bytes, "vsro" */
#define data_0048f484 ((char *)PPCDis_Data + 0x14b4)  /* 8 bytes, "vsubuwm" */
#define data_0048f48c ((char *)PPCDis_Data + 0x14bc)  /* 8 bytes, "vavguw" */
#define data_0048f494 ((char *)PPCDis_Data + 0x14c4)  /* 4 bytes, "vor" */
#define data_0048f498 ((char *)PPCDis_Data + 0x14c8)  /* 8 bytes, "vxor" */
#define data_0048f4a0 ((char *)PPCDis_Data + 0x14d0)  /* 8 bytes, "vavgsb" */
#define data_0048f4a8 ((char *)PPCDis_Data + 0x14d8)  /* 8 bytes, "vnor" */
#define data_0048f4b0 ((char *)PPCDis_Data + 0x14e0)  /* 8 bytes, "vavgsh" */
#define data_0048f4b8 ((char *)PPCDis_Data + 0x14e8)  /* 8 bytes, "vsubcuw" */
#define data_0048f4c0 ((char *)PPCDis_Data + 0x14f0)  /* 8 bytes, "vavgsw" */
#define data_0048f4c8 ((char *)PPCDis_Data + 0x14f8)  /* 8 bytes, "vsububs" */
#define data_0048f4d0 ((char *)PPCDis_Data + 0x1500)  /* 8 bytes, "mfvscr" */
#define data_0048f4d8 ((char *)PPCDis_Data + 0x1508)  /* 12 bytes, "vsum4ubs" */
#define data_0048f4e4 ((char *)PPCDis_Data + 0x1514)  /* 8 bytes, "vsubuhs" */
#define data_0048f4ec ((char *)PPCDis_Data + 0x151c)  /* 8 bytes, "mtvscr" */
#define data_0048f4f4 ((char *)PPCDis_Data + 0x1524)  /* 12 bytes, "vsum4shs" */
#define data_0048f500 ((char *)PPCDis_Data + 0x1530)  /* 8 bytes, "vsubuws" */
#define data_0048f508 ((char *)PPCDis_Data + 0x1538)  /* 12 bytes, "vsum2sws" */
#define data_0048f514 ((char *)PPCDis_Data + 0x1544)  /* 8 bytes, "vsubsbs" */
#define data_0048f51c ((char *)PPCDis_Data + 0x154c)  /* 12 bytes, "vsum4sbs" */
#define data_0048f528 ((char *)PPCDis_Data + 0x1558)  /* 8 bytes, "vsubshs" */
#define data_0048f530 ((char *)PPCDis_Data + 0x1560)  /* 8 bytes, "vsubsws" */
#define data_0048f538 ((unsigned long *)((char *)PPCDis_Data + 0x1568))  /* 1880 bytes, 117 pointers: data_0048f180, data_0048f188, data_0048f190, data_0048f198 ... */
#define data_0048fc90 ((char *)PPCDis_Data + 0x1cc0)  /* 12 bytes, "vcmpequb" */
#define data_0048fc9c ((char *)PPCDis_Data + 0x1ccc)  /* 12 bytes, "vcmpequh" */
#define data_0048fca8 ((char *)PPCDis_Data + 0x1cd8)  /* 12 bytes, "vcmpequw" */
#define data_0048fcb4 ((char *)PPCDis_Data + 0x1ce4)  /* 12 bytes, "vcmpeqfp" */
#define data_0048fcc0 ((char *)PPCDis_Data + 0x1cf0)  /* 12 bytes, "vcmpgefp" */
#define data_0048fccc ((char *)PPCDis_Data + 0x1cfc)  /* 12 bytes, "vcmpgtub" */
#define data_0048fcd8 ((char *)PPCDis_Data + 0x1d08)  /* 12 bytes, "vcmpgtuh" */
#define data_0048fce4 ((char *)PPCDis_Data + 0x1d14)  /* 12 bytes, "vcmpgtuw" */
#define data_0048fcf0 ((char *)PPCDis_Data + 0x1d20)  /* 12 bytes, "vcmpgtfp" */
#define data_0048fcfc ((char *)PPCDis_Data + 0x1d2c)  /* 12 bytes, "vcmpgtsb" */
#define data_0048fd08 ((char *)PPCDis_Data + 0x1d38)  /* 12 bytes, "vcmpgtsh" */
#define data_0048fd14 ((char *)PPCDis_Data + 0x1d44)  /* 12 bytes, "vcmpgtsw" */
#define data_0048fd20 ((unsigned long *)((char *)PPCDis_Data + 0x1d50))  /* 216 bytes, 13 pointers: data_0048fc90, data_0048fc9c, data_0048fca8, data_0048fcb4 ... */
#define data_0048fdf8 ((char *)PPCDis_Data + 0x1e28)  /* 12 bytes, "vmhaddshs" */
#define data_0048fe04 ((char *)PPCDis_Data + 0x1e34)  /* 12 bytes, "vmhraddshs" */
#define data_0048fe10 ((char *)PPCDis_Data + 0x1e40)  /* 12 bytes, "vmladduhm" */
#define data_0048fe1c ((char *)PPCDis_Data + 0x1e4c)  /* 12 bytes, "vmsumubm" */
#define data_0048fe28 ((char *)PPCDis_Data + 0x1e58)  /* 12 bytes, "vmsummbm" */
#define data_0048fe34 ((char *)PPCDis_Data + 0x1e64)  /* 12 bytes, "vmsumuhm" */
#define data_0048fe40 ((char *)PPCDis_Data + 0x1e70)  /* 12 bytes, "vmsumuhs" */
#define data_0048fe4c ((char *)PPCDis_Data + 0x1e7c)  /* 12 bytes, "vmsumshm" */
#define data_0048fe58 ((char *)PPCDis_Data + 0x1e88)  /* 12 bytes, "vmsumshs" */
#define data_0048fe64 ((char *)PPCDis_Data + 0x1e94)  /* 8 bytes, "vsel" */
#define data_0048fe6c ((char *)PPCDis_Data + 0x1e9c)  /* 8 bytes, "vperm" */
#define data_0048fe74 ((char *)PPCDis_Data + 0x1ea4)  /* 8 bytes, "vsldoi" */
#define data_0048fe7c ((char *)PPCDis_Data + 0x1eac)  /* 8 bytes, "vmaddfp" */
#define data_0048fe84 ((unsigned long *)((char *)PPCDis_Data + 0x1eb4))  /* 236 bytes, 14 pointers: data_0048fdf8, data_0048fe04, data_0048fe10, data_0048fe1c ... */
#define data_0048ff70 ((char *)PPCDis_Data + 0x1fa0)  /* 8 bytes, "ps_neg" */
#define data_0048ff78 ((char *)PPCDis_Data + 0x1fa8)  /* 8 bytes, "ps_mr" */
#define data_0048ff80 ((char *)PPCDis_Data + 0x1fb0)  /* 8 bytes, "ps_nabs" */
#define data_0048ff88 ((char *)PPCDis_Data + 0x1fb8)  /* 8 bytes, "ps_abs" */
#define data_0048ff90 ((char *)PPCDis_Data + 0x1fc0)  /* 12 bytes, "ps_merge00" */
#define data_0048ff9c ((char *)PPCDis_Data + 0x1fcc)  /* 12 bytes, "ps_merge01" */
#define data_0048ffa8 ((char *)PPCDis_Data + 0x1fd8)  /* 12 bytes, "ps_merge10" */
#define data_0048ffb4 ((char *)PPCDis_Data + 0x1fe4)  /* 12 bytes, "ps_merge11" */
#define data_0048ffc0 ((char *)PPCDis_Data + 0x1ff0)  /* 8 bytes, "dbcz_l" */
#define data_0048ffc8 ((unsigned long *)((char *)PPCDis_Data + 0x1ff8))  /* 144 bytes, 9 pointers: data_0048ff70, data_0048ff78, data_0048ff80, data_0048ff88 ... */
#define data_00490058 ((char *)PPCDis_Data + 0x2088)  /* 8 bytes, "ps_sum0" */
#define data_00490060 ((char *)PPCDis_Data + 0x2090)  /* 8 bytes, "ps_sum1" */
#define data_00490068 ((char *)PPCDis_Data + 0x2098)  /* 12 bytes, "ps_muls0" */
#define data_00490074 ((char *)PPCDis_Data + 0x20a4)  /* 12 bytes, "ps_muls1" */
#define data_00490080 ((char *)PPCDis_Data + 0x20b0)  /* 12 bytes, "ps_madds0" */
#define data_0049008c ((char *)PPCDis_Data + 0x20bc)  /* 12 bytes, "ps_madds1" */
#define data_00490098 ((char *)PPCDis_Data + 0x20c8)  /* 8 bytes, "ps_div" */
#define data_004900a0 ((char *)PPCDis_Data + 0x20d0)  /* 8 bytes, "ps_sub" */
#define data_004900a8 ((char *)PPCDis_Data + 0x20d8)  /* 8 bytes, "ps_add" */
#define data_004900b0 ((char *)PPCDis_Data + 0x20e0)  /* 8 bytes, "ps_sel" */
#define data_004900b8 ((char *)PPCDis_Data + 0x20e8)  /* 8 bytes, "ps_res" */
#define data_004900c0 ((char *)PPCDis_Data + 0x20f0)  /* 8 bytes, "ps_mul" */
#define data_004900c8 ((char *)PPCDis_Data + 0x20f8)  /* 12 bytes, "ps_rsqrte" */
#define data_004900d4 ((char *)PPCDis_Data + 0x2104)  /* 8 bytes, "ps_msub" */
#define data_004900dc ((char *)PPCDis_Data + 0x210c)  /* 8 bytes, "ps_madd" */
#define data_004900e4 ((char *)PPCDis_Data + 0x2114)  /* 12 bytes, "ps_nmsub" */
#define data_004900f0 ((char *)PPCDis_Data + 0x2120)  /* 12 bytes, "ps_nmadd" */
#define data_004900fc ((unsigned long *)((char *)PPCDis_Data + 0x212c))  /* 272 bytes, 17 pointers: data_00490058, data_00490060, data_00490068, data_00490074 ... */
#define data_0049020c ((char *)PPCDis_Data + 0x223c)  /* 8 bytes, "psq_lx" */
#define data_00490214 ((char *)PPCDis_Data + 0x2244)  /* 8 bytes, "psq_stx" */
#define data_0049021c ((char *)PPCDis_Data + 0x224c)  /* 8 bytes, "psq_lux" */
#define data_00490224 ((char *)PPCDis_Data + 0x2254)  /* 12 bytes, "psq_stux" */
#define data_00490230 ((unsigned long *)((char *)PPCDis_Data + 0x2260))  /* 64 bytes, 4 pointers: data_0049020c, data_00490214, data_0049021c, data_00490224 */
#define data_00490270 ((char *)PPCDis_Data + 0x22a0)  /* 12 bytes, "ps_cmpu0" */
#define data_0049027c ((char *)PPCDis_Data + 0x22ac)  /* 12 bytes, "ps_cmpo0" */
#define data_00490288 ((char *)PPCDis_Data + 0x22b8)  /* 12 bytes, "ps_cmpu1" */
#define data_00490294 ((char *)PPCDis_Data + 0x22c4)  /* 12 bytes, "ps_cmpo1" */
#define data_004902a0 ((unsigned long *)((char *)PPCDis_Data + 0x22d0))  /* 64 bytes, 4 pointers: data_00490270, data_0049027c, data_00490288, data_00490294 */
#define data_004902e0 ((char *)PPCDis_Data + 0x2310)  /* 8 bytes, "mcrf" */
#define data_004902e8 ((char *)PPCDis_Data + 0x2318)  /* 8 bytes, "bclr" */
#define data_004902f0 ((char *)PPCDis_Data + 0x2320)  /* 8 bytes, "crnor" */
#define data_004902f8 ((char *)PPCDis_Data + 0x2328)  /* 4 bytes, "rfi" */
#define data_004902fc ((char *)PPCDis_Data + 0x232c)  /* 8 bytes, "rfci" */
#define data_00490304 ((char *)PPCDis_Data + 0x2334)  /* 8 bytes, "crandc" */
#define data_0049030c ((char *)PPCDis_Data + 0x233c)  /* 8 bytes, "isync" */
#define data_00490314 ((char *)PPCDis_Data + 0x2344)  /* 8 bytes, "crxor" */
#define data_0049031c ((char *)PPCDis_Data + 0x234c)  /* 8 bytes, "crnand" */
#define data_00490324 ((char *)PPCDis_Data + 0x2354)  /* 8 bytes, "crand" */
#define data_0049032c ((char *)PPCDis_Data + 0x235c)  /* 8 bytes, "creqv" */
#define data_00490334 ((char *)PPCDis_Data + 0x2364)  /* 8 bytes, "crorc" */
#define data_0049033c ((char *)PPCDis_Data + 0x236c)  /* 8 bytes, "cror" */
#define data_00490344 ((char *)PPCDis_Data + 0x2374)  /* 8 bytes, "bcctr" */
#define data_0049034c ((unsigned long *)((char *)PPCDis_Data + 0x237c))  /* 224 bytes, 14 pointers: data_004902e0, data_004902e8, data_004902f0, data_004902f8 ... */
#define data_0049042c ((char *)PPCDis_Data + 0x245c)  /* 8 bytes, "bltlr" */
#define data_00490434 ((char *)PPCDis_Data + 0x2464)  /* 8 bytes, "bgtlr" */
#define data_0049043c ((char *)PPCDis_Data + 0x246c)  /* 8 bytes, "beqlr" */
#define data_00490444 ((char *)PPCDis_Data + 0x2474)  /* 8 bytes, "bsolr" */
#define data_0049044c ((char *)PPCDis_Data + 0x247c)  /* 8 bytes, "bgelr" */
#define data_00490454 ((char *)PPCDis_Data + 0x2484)  /* 8 bytes, "blelr" */
#define data_0049045c ((char *)PPCDis_Data + 0x248c)  /* 8 bytes, "bnelr" */
#define data_00490464 ((char *)PPCDis_Data + 0x2494)  /* 8 bytes, "bnslr" */
#define data_0049046c ((unsigned long *)((char *)PPCDis_Data + 0x249c))  /* 32 bytes, 8 pointers: data_0049042c, data_00490434, data_0049043c, data_00490444 ... */
#define data_0049048c ((char *)PPCDis_Data + 0x24bc)  /* 8 bytes, "bltctr" */
#define data_00490494 ((char *)PPCDis_Data + 0x24c4)  /* 8 bytes, "bgtctr" */
#define data_0049049c ((char *)PPCDis_Data + 0x24cc)  /* 8 bytes, "beqctr" */
#define data_004904a4 ((char *)PPCDis_Data + 0x24d4)  /* 8 bytes, "bsoctr" */
#define data_004904ac ((char *)PPCDis_Data + 0x24dc)  /* 8 bytes, "bgectr" */
#define data_004904b4 ((char *)PPCDis_Data + 0x24e4)  /* 8 bytes, "blectr" */
#define data_004904bc ((char *)PPCDis_Data + 0x24ec)  /* 8 bytes, "bnectr" */
#define data_004904c4 ((char *)PPCDis_Data + 0x24f4)  /* 8 bytes, "bnsctr" */
#define data_004904cc ((unsigned long *)((char *)PPCDis_Data + 0x24fc))  /* 32 bytes, 8 pointers: data_0049048c, data_00490494, data_0049049c, data_004904a4 ... */
#define data_004904ec ((char *)PPCDis_Data + 0x251c)  /* 8 bytes, "subfc" */
#define data_004904f4 ((char *)PPCDis_Data + 0x2524)  /* 8 bytes, "mulhdu" */
#define data_004904fc ((char *)PPCDis_Data + 0x252c)  /* 8 bytes, "addc" */
#define data_00490504 ((char *)PPCDis_Data + 0x2534)  /* 8 bytes, "mulhwu" */
#define data_0049050c ((char *)PPCDis_Data + 0x253c)  /* 8 bytes, "subf" */
#define data_00490514 ((char *)PPCDis_Data + 0x2544)  /* 8 bytes, "mulhd" */
#define data_0049051c ((char *)PPCDis_Data + 0x254c)  /* 8 bytes, "mulhw" */
#define data_00490524 ((char *)PPCDis_Data + 0x2554)  /* 4 bytes, "neg" */
#define data_00490528 ((char *)PPCDis_Data + 0x2558)  /* 4 bytes, "mul" */
#define data_0049052c ((char *)PPCDis_Data + 0x255c)  /* 8 bytes, "subfe" */
#define data_00490534 ((char *)PPCDis_Data + 0x2564)  /* 8 bytes, "adde" */
#define data_0049053c ((char *)PPCDis_Data + 0x256c)  /* 8 bytes, "subfze" */
#define data_00490544 ((char *)PPCDis_Data + 0x2574)  /* 8 bytes, "addze" */
#define data_0049054c ((char *)PPCDis_Data + 0x257c)  /* 8 bytes, "subfme" */
#define data_00490554 ((char *)PPCDis_Data + 0x2584)  /* 8 bytes, "mulld" */
#define data_0049055c ((char *)PPCDis_Data + 0x258c)  /* 8 bytes, "addme" */
#define data_00490564 ((char *)PPCDis_Data + 0x2594)  /* 8 bytes, "mullw" */
#define data_0049056c ((char *)PPCDis_Data + 0x259c)  /* 4 bytes, "doz" */
#define data_00490570 ((char *)PPCDis_Data + 0x25a0)  /* 4 bytes, "add" */
#define data_00490574 ((char *)PPCDis_Data + 0x25a4)  /* 4 bytes, "div" */
#define data_00490578 ((char *)PPCDis_Data + 0x25a8)  /* 4 bytes, "abs" */
#define data_0049057c ((char *)PPCDis_Data + 0x25ac)  /* 8 bytes, "divs" */
#define data_00490584 ((char *)PPCDis_Data + 0x25b4)  /* 8 bytes, "divdu" */
#define data_0049058c ((char *)PPCDis_Data + 0x25bc)  /* 8 bytes, "divwu" */
#define data_00490594 ((char *)PPCDis_Data + 0x25c4)  /* 8 bytes, "nabs" */
#define data_0049059c ((char *)PPCDis_Data + 0x25cc)  /* 8 bytes, "divd" */
#define data_004905a4 ((char *)PPCDis_Data + 0x25d4)  /* 8 bytes, "divw" */
#define data_004905ac ((unsigned long *)((char *)PPCDis_Data + 0x25dc))  /* 432 bytes, 27 pointers: data_004904ec, data_004904f4, data_004904fc, data_00490504 ... */
#define data_0049075c ((char *)PPCDis_Data + 0x278c)  /* 4 bytes, 4 bytes */
#define data_00490760 ((char *)PPCDis_Data + 0x2790)  /* 4 bytes, "." */
#define data_00490764 ((char *)PPCDis_Data + 0x2794)  /* 4 bytes, "o" */
#define data_00490768 ((unsigned long *)((char *)PPCDis_Data + 0x2798))  /* 20 bytes, 4 pointers: data_0049075c, data_00490760, data_00490764, data_00490768 */
#define data_0049077c ((char *)PPCDis_Data + 0x27ac)  /* 8 bytes, "iselgt" */
#define data_00490784 ((char *)PPCDis_Data + 0x27b4)  /* 8 bytes, "iseleq" */
#define data_0049078c ((char *)PPCDis_Data + 0x27bc)  /* 8 bytes, "isellt" */
#define data_00490794 ((char *)PPCDis_Data + 0x27c4)  /* 16 bytes, "isel<illegal>" */
#define data_004907a4 ((unsigned long *)((char *)PPCDis_Data + 0x27d4))  /* 16 bytes, 4 pointers: data_0049077c, data_00490784, data_0049078c, data_00490794 */
#define data_004907b4 ((char *)PPCDis_Data + 0x27e4)  /* 8 bytes, "fdivs" */
#define data_004907bc ((char *)PPCDis_Data + 0x27ec)  /* 8 bytes, "fsubs" */
#define data_004907c4 ((char *)PPCDis_Data + 0x27f4)  /* 8 bytes, "fadds" */
#define data_004907cc ((char *)PPCDis_Data + 0x27fc)  /* 8 bytes, "fsqrts" */
#define data_004907d4 ((char *)PPCDis_Data + 0x2804)  /* 8 bytes, "fres" */
#define data_004907dc ((char *)PPCDis_Data + 0x280c)  /* 8 bytes, "fmuls" */
#define data_004907e4 ((char *)PPCDis_Data + 0x2814)  /* 8 bytes, "fmsubs" */
#define data_004907ec ((char *)PPCDis_Data + 0x281c)  /* 8 bytes, "fmadds" */
#define data_004907f4 ((char *)PPCDis_Data + 0x2824)  /* 8 bytes, "fnmsubs" */
#define data_004907fc ((char *)PPCDis_Data + 0x282c)  /* 8 bytes, "fnmadds" */
#define data_00490804 ((unsigned long *)((char *)PPCDis_Data + 0x2834))  /* 160 bytes, 10 pointers: data_004907b4, data_004907bc, data_004907c4, data_004907cc ... */
#define data_004908a4 ((char *)PPCDis_Data + 0x28d4)  /* 8 bytes, "fdiv" */
#define data_004908ac ((char *)PPCDis_Data + 0x28dc)  /* 8 bytes, "fsub" */
#define data_004908b4 ((char *)PPCDis_Data + 0x28e4)  /* 8 bytes, "fadd" */
#define data_004908bc ((char *)PPCDis_Data + 0x28ec)  /* 8 bytes, "fsqrt" */
#define data_004908c4 ((char *)PPCDis_Data + 0x28f4)  /* 8 bytes, "fsel" */
#define data_004908cc ((char *)PPCDis_Data + 0x28fc)  /* 8 bytes, "fmul" */
#define data_004908d4 ((char *)PPCDis_Data + 0x2904)  /* 8 bytes, "frsqrte" */
#define data_004908dc ((char *)PPCDis_Data + 0x290c)  /* 8 bytes, "fmsub" */
#define data_004908e4 ((char *)PPCDis_Data + 0x2914)  /* 8 bytes, "fmadd" */
#define data_004908ec ((char *)PPCDis_Data + 0x291c)  /* 8 bytes, "fnmsub" */
#define data_004908f4 ((char *)PPCDis_Data + 0x2924)  /* 8 bytes, "fnmadd" */
#define data_004908fc ((unsigned long *)((char *)PPCDis_Data + 0x292c))  /* 176 bytes, 11 pointers: data_004908a4, data_004908ac, data_004908b4, data_004908bc ... */
#define data_004909ac ((char *)PPCDis_Data + 0x29dc)  /* 8 bytes, "rlwimi" */
#define data_004909b4 ((char *)PPCDis_Data + 0x29e4)  /* 8 bytes, "rlwinm" */
#define data_004909bc ((char *)PPCDis_Data + 0x29ec)  /* 8 bytes, "rlmi" */
#define data_004909c4 ((char *)PPCDis_Data + 0x29f4)  /* 8 bytes, "rlwnm" */
#define data_004909cc ((unsigned long *)((char *)PPCDis_Data + 0x29fc))  /* 16 bytes, 4 pointers: data_004909ac, data_004909b4, data_004909bc, data_004909c4 */
#define data_004909dc ((char *)PPCDis_Data + 0x2a0c)  /* 8 bytes, "rldicl" */
#define data_004909e4 ((char *)PPCDis_Data + 0x2a14)  /* 8 bytes, "rldicr" */
#define data_004909ec ((char *)PPCDis_Data + 0x2a1c)  /* 8 bytes, "rldic" */
#define data_004909f4 ((char *)PPCDis_Data + 0x2a24)  /* 8 bytes, "rldimi" */
#define data_004909fc ((unsigned long *)((char *)PPCDis_Data + 0x2a2c))  /* 16 bytes, 4 pointers: data_004909dc, data_004909e4, data_004909ec, data_004909f4 */
#define data_00490a0c ((char *)PPCDis_Data + 0x2a3c)  /* 8 bytes, "rldcl" */
#define data_00490a14 ((char *)PPCDis_Data + 0x2a44)  /* 8 bytes, "rldcr" */
#define data_00490a1c ((unsigned long *)((char *)PPCDis_Data + 0x2a4c))  /* 8 bytes, 2 pointers: data_00490a0c, data_00490a14 */
#define data_00490a24 ((char *)PPCDis_Data + 0x2a54)  /* 4 bytes, 4 bytes */
#define data_00490a28 ((char *)PPCDis_Data + 0x2a58)  /* 1000 bytes, 1000 bytes */
#define data_00490e10 ((char *)PPCDis_Data + 0x2e40)  /* 4 bytes, 4 bytes */
#define data_00490e14 ((char *)PPCDis_Data + 0x2e44)  /* 64 bytes, 64 bytes */
#define data_00490e54 ((char *)PPCDis_Data + 0x2e84)  /* 4 bytes, 4 bytes */
#define data_00490e58 ((char *)PPCDis_Data + 0x2e88)  /* 104 bytes, 104 bytes */
#define data_00490ec0 ((unsigned long *)((char *)PPCDis_Data + 0x2ef0))  /* 80 bytes, 20 pointers: PPCDis_Branch, PPCDis_BranchConditional, PPCDis_SystemCall, PPCDis_DForm ... */
#define data_00490f10 ((char *)PPCDis_Data + 0x2f40)  /* 8 bytes, "dcr%d" */
#define data_00490f18 ((char *)PPCDis_Data + 0x2f48)  /* 12 bytes, "; '%c%c'" */
#define data_00490f24 ((char *)PPCDis_Data + 0x2f54)  /* 8 bytes, "0x%08X" */
#define data_00490f2c ((char *)PPCDis_Data + 0x2f5c)  /* 8 bytes, "$%+d" */
#define data_00490f34 ((char *)PPCDis_Data + 0x2f64)  /* 8 bytes, "%d(%s)" */
#define data_00490f3c ((char *)PPCDis_Data + 0x2f6c)  /* 8 bytes, "%s%04x" */
#define data_00490f44 ((char *)PPCDis_Data + 0x2f74)  /* 8 bytes, "%s%08x" */
#define data_00490f4c ((char *)PPCDis_Data + 0x2f7c)  /* 4 bytes, "%d" */
#define data_00490f50 ((char *)PPCDis_Data + 0x2f80)  /* 8 bytes, "cr%d" */
#define data_00490f58 ((char *)PPCDis_Data + 0x2f88)  /* 8 bytes, "sr%d" */
#define data_00490f60 ((char *)PPCDis_Data + 0x2f90)  /* 8 bytes, "spr%d" */
#define data_00490f68 ((char *)PPCDis_Data + 0x2f98)  /* 8 bytes, "????" */
#define data_00490f70 ((char *)PPCDis_Data + 0x2fa0)  /* 8 bytes, "vr%d" */
#define data_00490f78 ((char *)PPCDis_Data + 0x2fa8)  /* 8 bytes, "fp%d" */
#define data_00490f80 ((char *)PPCDis_Data + 0x2fb0)  /* 4 bytes, "rsp" */
#define data_00490f84 ((char *)PPCDis_Data + 0x2fb4)  /* 8 bytes, "rtoc" */
#define data_00490f8c ((char *)PPCDis_Data + 0x2fbc)  /* 32 bytes, strings "r%d" "'%s'" "%02x" "<No name>" */
#define data_00490fac ((char *)PPCDis_Data + 0x2fdc)  /* 4 bytes, "0x" */
#define data_00490fb0 ((char *)PPCDis_Data + 0x2fe0)  /* 4 bytes, "$" */
#define data_00490fb4 ((char *)PPCDis_Data + 0x2fe4)  /* 8 bytes, "dc.l" */
#define data_00490fbc ((char *)PPCDis_Data + 0x2fec)  /* 8 bytes, "0x%08x" */
#define data_00490fc4 ((char *)PPCDis_Data + 0x2ff4)  /* 28 bytes, "; Invalid opcode '%c%c%c%c'" */
#define data_00490fe0 ((char *)PPCDis_Data + 0x3010)  /* 4 bytes, "%s." */
#define data_00490fe4 ((char *)PPCDis_Data + 0x3014)  /* 4 bytes, "%s" */
#define data_00490fe8 ((char *)PPCDis_Data + 0x3018)  /* 8 bytes, "insrwi" */
#define data_00490ff0 ((char *)PPCDis_Data + 0x3020)  /* 8 bytes, "inslwi" */
#define data_00490ff8 ((char *)PPCDis_Data + 0x3028)  /* 8 bytes, "clrrwi" */
#define data_00491000 ((char *)PPCDis_Data + 0x3030)  /* 8 bytes, "clrlwi" */
#define data_00491008 ((char *)PPCDis_Data + 0x3038)  /* 8 bytes, "rotlwi" */
#define data_00491010 ((char *)PPCDis_Data + 0x3040)  /* 8 bytes, "rotrwi" */
#define data_00491018 ((char *)PPCDis_Data + 0x3048)  /* 8 bytes, "slwi" */
#define data_00491020 ((char *)PPCDis_Data + 0x3050)  /* 8 bytes, "srwi" */
#define data_00491028 ((char *)PPCDis_Data + 0x3058)  /* 8 bytes, "extlwi" */
#define data_00491030 ((char *)PPCDis_Data + 0x3060)  /* 8 bytes, "extrwi" */
#define data_00491038 ((char *)PPCDis_Data + 0x3068)  /* 12 bytes, "clrlslwi" */
#define data_00491044 ((char *)PPCDis_Data + 0x3074)  /* 8 bytes, "rotlw" */
#define data_0049104c ((char *)PPCDis_Data + 0x307c)  /* 8 bytes, "sradi." */
#define data_00491054 ((char *)PPCDis_Data + 0x3084)  /* 8 bytes, "sradi" */
#define data_0049105c ((char *)PPCDis_Data + 0x308c)  /* 8 bytes, "mtfsf." */
#define data_00491064 ((char *)PPCDis_Data + 0x3094)  /* 8 bytes, "mtfsf" */
#define data_0049106c ((char *)PPCDis_Data + 0x309c)  /* 8 bytes, "mfxer" */
#define data_00491074 ((char *)PPCDis_Data + 0x30a4)  /* 8 bytes, "mflr" */
#define data_0049107c ((char *)PPCDis_Data + 0x30ac)  /* 8 bytes, "mfctr" */
#define data_00491084 ((char *)PPCDis_Data + 0x30b4)  /* 8 bytes, "mfdsisr" */
#define data_0049108c ((char *)PPCDis_Data + 0x30bc)  /* 8 bytes, "mfdar" */
#define data_00491094 ((char *)PPCDis_Data + 0x30c4)  /* 8 bytes, "mfdec" */
#define data_0049109c ((char *)PPCDis_Data + 0x30cc)  /* 8 bytes, "mfsdr1" */
#define data_004910a4 ((char *)PPCDis_Data + 0x30d4)  /* 8 bytes, "mfsrr0" */
#define data_004910ac ((char *)PPCDis_Data + 0x30dc)  /* 8 bytes, "mfsrr1" */
#define data_004910b4 ((char *)PPCDis_Data + 0x30e4)  /* 12 bytes, "mfvrsave" */
#define data_004910c0 ((char *)PPCDis_Data + 0x30f0)  /* 8 bytes, "mfasr" */
#define data_004910c8 ((char *)PPCDis_Data + 0x30f8)  /* 8 bytes, "mfear" */
#define data_004910d0 ((char *)PPCDis_Data + 0x3100)  /* 8 bytes, "mfpvr" */
#define data_004910d8 ((char *)PPCDis_Data + 0x3108)  /* 8 bytes, "mfcdbcr" */
#define data_004910e0 ((char *)PPCDis_Data + 0x3110)  /* 8 bytes, "mfdac1" */
#define data_004910e8 ((char *)PPCDis_Data + 0x3118)  /* 8 bytes, "mfdac2" */
#define data_004910f0 ((char *)PPCDis_Data + 0x3120)  /* 8 bytes, "mfdbcr" */
#define data_004910f8 ((char *)PPCDis_Data + 0x3128)  /* 8 bytes, "mfdbsr" */
#define data_00491100 ((char *)PPCDis_Data + 0x3130)  /* 8 bytes, "mfdccr" */
#define data_00491108 ((char *)PPCDis_Data + 0x3138)  /* 8 bytes, "mfdcwr" */
#define data_00491110 ((char *)PPCDis_Data + 0x3140)  /* 8 bytes, "mfdear" */
#define data_00491118 ((char *)PPCDis_Data + 0x3148)  /* 8 bytes, "mfesr" */
#define data_00491120 ((char *)PPCDis_Data + 0x3150)  /* 8 bytes, "mfevpr" */
#define data_00491128 ((char *)PPCDis_Data + 0x3158)  /* 8 bytes, "mfiac1" */
#define data_00491130 ((char *)PPCDis_Data + 0x3160)  /* 8 bytes, "mfiac2" */
#define data_00491138 ((char *)PPCDis_Data + 0x3168)  /* 8 bytes, "mficcr" */
#define data_00491140 ((char *)PPCDis_Data + 0x3170)  /* 12 bytes, "mficdbdr" */
#define data_0049114c ((char *)PPCDis_Data + 0x317c)  /* 8 bytes, "mfpid" */
#define data_00491154 ((char *)PPCDis_Data + 0x3184)  /* 8 bytes, "mfpit" */
#define data_0049115c ((char *)PPCDis_Data + 0x318c)  /* 8 bytes, "mfpbl1" */
#define data_00491164 ((char *)PPCDis_Data + 0x3194)  /* 8 bytes, "mfpbl2" */
#define data_0049116c ((char *)PPCDis_Data + 0x319c)  /* 12 bytes, "INST_403" */
#define data_00491178 ((char *)PPCDis_Data + 0x31a8)  /* 8 bytes, "mfpbu2" */
#define data_00491180 ((char *)PPCDis_Data + 0x31b0)  /* 8 bytes, "mfsgr" */
#define data_00491188 ((char *)PPCDis_Data + 0x31b8)  /* 8 bytes, "mfsler" */
#define data_00491190 ((char *)PPCDis_Data + 0x31c0)  /* 8 bytes, "mfsprg0" */
#define data_00491198 ((char *)PPCDis_Data + 0x31c8)  /* 8 bytes, "mfsprg1" */
#define data_004911a0 ((char *)PPCDis_Data + 0x31d0)  /* 8 bytes, "mfsprg2" */
#define data_004911a8 ((char *)PPCDis_Data + 0x31d8)  /* 8 bytes, "mfsprg3" */
#define data_004911b0 ((char *)PPCDis_Data + 0x31e0)  /* 8 bytes, "mfsrr2" */
#define data_004911b8 ((char *)PPCDis_Data + 0x31e8)  /* 8 bytes, "mfsrr3" */
#define data_004911c0 ((char *)PPCDis_Data + 0x31f0)  /* 8 bytes, "mftbhi" */
#define data_004911c8 ((char *)PPCDis_Data + 0x31f8)  /* 8 bytes, "mftbhu" */
#define data_004911d0 ((char *)PPCDis_Data + 0x3200)  /* 8 bytes, "mftblo" */
#define data_004911d8 ((char *)PPCDis_Data + 0x3208)  /* 8 bytes, "mftblu" */
#define data_004911e0 ((char *)PPCDis_Data + 0x3210)  /* 8 bytes, "mftcr" */
#define data_004911e8 ((char *)PPCDis_Data + 0x3218)  /* 8 bytes, "mftsr" */
#define data_004911f0 ((char *)PPCDis_Data + 0x3220)  /* 8 bytes, "mfzpr" */
#define data_004911f8 ((char *)PPCDis_Data + 0x3228)  /* 8 bytes, "mfspr" */
#define data_00491200 ((char *)PPCDis_Data + 0x3230)  /* 8 bytes, "mtxer" */
#define data_00491208 ((char *)PPCDis_Data + 0x3238)  /* 8 bytes, "mtlr" */
#define data_00491210 ((char *)PPCDis_Data + 0x3240)  /* 8 bytes, "mtctr" */
#define data_00491218 ((char *)PPCDis_Data + 0x3248)  /* 8 bytes, "mtdsisr" */
#define data_00491220 ((char *)PPCDis_Data + 0x3250)  /* 8 bytes, "mtdar" */
#define data_00491228 ((char *)PPCDis_Data + 0x3258)  /* 8 bytes, "mtdec" */
#define data_00491230 ((char *)PPCDis_Data + 0x3260)  /* 8 bytes, "mtsdr1" */
#define data_00491238 ((char *)PPCDis_Data + 0x3268)  /* 8 bytes, "mtsrr0" */
#define data_00491240 ((char *)PPCDis_Data + 0x3270)  /* 8 bytes, "mtsrr1" */
#define data_00491248 ((char *)PPCDis_Data + 0x3278)  /* 12 bytes, "mtvrsave" */
#define data_00491254 ((char *)PPCDis_Data + 0x3284)  /* 8 bytes, "mtasr" */
#define data_0049125c ((char *)PPCDis_Data + 0x328c)  /* 8 bytes, "mtear" */
#define data_00491264 ((char *)PPCDis_Data + 0x3294)  /* 8 bytes, "mttbl" */
#define data_0049126c ((char *)PPCDis_Data + 0x329c)  /* 8 bytes, "mttbu" */
#define data_00491274 ((char *)PPCDis_Data + 0x32a4)  /* 8 bytes, "mtcdbcr" */
#define data_0049127c ((char *)PPCDis_Data + 0x32ac)  /* 8 bytes, "mtdac1" */
#define data_00491284 ((char *)PPCDis_Data + 0x32b4)  /* 8 bytes, "mtdac2" */
#define data_0049128c ((char *)PPCDis_Data + 0x32bc)  /* 8 bytes, "mtdbcr" */
#define data_00491294 ((char *)PPCDis_Data + 0x32c4)  /* 8 bytes, "mtdbsr" */
#define data_0049129c ((char *)PPCDis_Data + 0x32cc)  /* 8 bytes, "mtdccr" */
#define data_004912a4 ((char *)PPCDis_Data + 0x32d4)  /* 8 bytes, "mtdcwr" */
#define data_004912ac ((char *)PPCDis_Data + 0x32dc)  /* 8 bytes, "mtdear" */
#define data_004912b4 ((char *)PPCDis_Data + 0x32e4)  /* 8 bytes, "mtesr" */
#define data_004912bc ((char *)PPCDis_Data + 0x32ec)  /* 8 bytes, "mtevpr" */
#define data_004912c4 ((char *)PPCDis_Data + 0x32f4)  /* 8 bytes, "mtiac1" */
#define data_004912cc ((char *)PPCDis_Data + 0x32fc)  /* 8 bytes, "mtiac2" */
#define data_004912d4 ((char *)PPCDis_Data + 0x3304)  /* 8 bytes, "mticcr" */
#define data_004912dc ((char *)PPCDis_Data + 0x330c)  /* 12 bytes, "mticdbdr" */
#define data_004912e8 ((char *)PPCDis_Data + 0x3318)  /* 8 bytes, "mtpid" */
#define data_004912f0 ((char *)PPCDis_Data + 0x3320)  /* 8 bytes, "mtpit" */
#define data_004912f8 ((char *)PPCDis_Data + 0x3328)  /* 8 bytes, "mtpbl1" */
#define data_00491300 ((char *)PPCDis_Data + 0x3330)  /* 8 bytes, "mtpbl2" */
#define data_00491308 ((char *)PPCDis_Data + 0x3338)  /* 8 bytes, "mtpbu1" */
#define data_00491310 ((char *)PPCDis_Data + 0x3340)  /* 8 bytes, "mtpbu2" */
#define data_00491318 ((char *)PPCDis_Data + 0x3348)  /* 8 bytes, "mtsgr" */
#define data_00491320 ((char *)PPCDis_Data + 0x3350)  /* 8 bytes, "mtsler" */
#define data_00491328 ((char *)PPCDis_Data + 0x3358)  /* 8 bytes, "mtsprg0" */
#define data_00491330 ((char *)PPCDis_Data + 0x3360)  /* 8 bytes, "mtsprg1" */
#define data_00491338 ((char *)PPCDis_Data + 0x3368)  /* 8 bytes, "mtsprg2" */
#define data_00491340 ((char *)PPCDis_Data + 0x3370)  /* 8 bytes, "mtsprg3" */
#define data_00491348 ((char *)PPCDis_Data + 0x3378)  /* 8 bytes, "mtsrr2" */
#define data_00491350 ((char *)PPCDis_Data + 0x3380)  /* 8 bytes, "mtsrr3" */
#define data_00491358 ((char *)PPCDis_Data + 0x3388)  /* 8 bytes, "mttbhi" */
#define data_00491360 ((char *)PPCDis_Data + 0x3390)  /* 8 bytes, "mttblo" */
#define data_00491368 ((char *)PPCDis_Data + 0x3398)  /* 8 bytes, "mttcr" */
#define data_00491370 ((char *)PPCDis_Data + 0x33a0)  /* 8 bytes, "mttsr" */
#define data_00491378 ((char *)PPCDis_Data + 0x33a8)  /* 8 bytes, "mtzpr" */
#define data_00491380 ((char *)PPCDis_Data + 0x33b0)  /* 8 bytes, "mtspr" */
#define data_00491388 ((char *)PPCDis_Data + 0x33b8)  /* 8 bytes, "mtcrf" */
#define data_00491390 ((char *)PPCDis_Data + 0x33c0)  /* 8 bytes, "0x%x" */
#define data_00491398 ((char *)PPCDis_Data + 0x33c8)  /* 8 bytes, "mftb" */
#define data_004913a0 ((char *)PPCDis_Data + 0x33d0)  /* 8 bytes, "mftbu" */
#define data_004913a8 ((char *)PPCDis_Data + 0x33d8)  /* 8 bytes, "mtbr" */
#define data_004913b0 ((char *)PPCDis_Data + 0x33e0)  /* 8 bytes, "mtbru" */
#define data_004913b8 ((char *)PPCDis_Data + 0x33e8)  /* 8 bytes, "mfbear" */
#define data_004913c0 ((char *)PPCDis_Data + 0x33f0)  /* 8 bytes, "mfbesr" */
#define data_004913c8 ((char *)PPCDis_Data + 0x33f8)  /* 8 bytes, "mfbesr0" */
#define data_004913d0 ((char *)PPCDis_Data + 0x3400)  /* 8 bytes, "mfbr0" */
#define data_004913d8 ((char *)PPCDis_Data + 0x3408)  /* 8 bytes, "mfbrcr0" */
#define data_004913e0 ((char *)PPCDis_Data + 0x3410)  /* 8 bytes, "mfbr1" */
#define data_004913e8 ((char *)PPCDis_Data + 0x3418)  /* 8 bytes, "mfbrcr1" */
#define data_004913f0 ((char *)PPCDis_Data + 0x3420)  /* 8 bytes, "mfbr2" */
#define data_004913f8 ((char *)PPCDis_Data + 0x3428)  /* 8 bytes, "mfbrcr2" */
#define data_00491400 ((char *)PPCDis_Data + 0x3430)  /* 8 bytes, "mfbr3" */
#define data_00491408 ((char *)PPCDis_Data + 0x3438)  /* 8 bytes, "mfbrcr3" */
#define data_00491410 ((char *)PPCDis_Data + 0x3440)  /* 8 bytes, "mfbr4" */
#define data_00491418 ((char *)PPCDis_Data + 0x3448)  /* 8 bytes, "mfbrcr4" */
#define data_00491420 ((char *)PPCDis_Data + 0x3450)  /* 8 bytes, "mfbr5" */
#define data_00491428 ((char *)PPCDis_Data + 0x3458)  /* 8 bytes, "mfbrcr5" */
#define data_00491430 ((char *)PPCDis_Data + 0x3460)  /* 8 bytes, "mfbr6" */
#define data_00491438 ((char *)PPCDis_Data + 0x3468)  /* 8 bytes, "mfbrcr6" */
#define data_00491440 ((char *)PPCDis_Data + 0x3470)  /* 8 bytes, "mfbr7" */
#define data_00491448 ((char *)PPCDis_Data + 0x3478)  /* 8 bytes, "mfbrcr7" */
#define data_00491450 ((char *)PPCDis_Data + 0x3480)  /* 12 bytes, "mfdmacc0" */
#define data_0049145c ((char *)PPCDis_Data + 0x348c)  /* 12 bytes, "mfdmacc1" */
#define data_00491468 ((char *)PPCDis_Data + 0x3498)  /* 12 bytes, "mfdmacc2" */
#define data_00491474 ((char *)PPCDis_Data + 0x34a4)  /* 12 bytes, "mfdmacc3" */
#define data_00491480 ((char *)PPCDis_Data + 0x34b0)  /* 12 bytes, "mfdmacr0" */
#define data_0049148c ((char *)PPCDis_Data + 0x34bc)  /* 12 bytes, "mfdmacr1" */
#define data_00491498 ((char *)PPCDis_Data + 0x34c8)  /* 12 bytes, "mfdmacr2" */
#define data_004914a4 ((char *)PPCDis_Data + 0x34d4)  /* 12 bytes, "mfdmacr3" */
#define data_004914b0 ((char *)PPCDis_Data + 0x34e0)  /* 12 bytes, "mfdmact0" */
#define data_004914bc ((char *)PPCDis_Data + 0x34ec)  /* 12 bytes, "mfdmact1" */
#define data_004914c8 ((char *)PPCDis_Data + 0x34f8)  /* 12 bytes, "mfdmact2" */
#define data_004914d4 ((char *)PPCDis_Data + 0x3504)  /* 12 bytes, "mfdmact3" */
#define data_004914e0 ((char *)PPCDis_Data + 0x3510)  /* 12 bytes, "mfdmada0" */
#define data_004914ec ((char *)PPCDis_Data + 0x351c)  /* 12 bytes, "mfdmada1" */
#define data_004914f8 ((char *)PPCDis_Data + 0x3528)  /* 12 bytes, "mfdmada2" */
#define data_00491504 ((char *)PPCDis_Data + 0x3534)  /* 12 bytes, "mfdmada3" */
#define data_00491510 ((char *)PPCDis_Data + 0x3540)  /* 12 bytes, "mfdmasa0" */
#define data_0049151c ((char *)PPCDis_Data + 0x354c)  /* 12 bytes, "mfdmasa1" */
#define data_00491528 ((char *)PPCDis_Data + 0x3558)  /* 12 bytes, "mfdmasa2" */
#define data_00491534 ((char *)PPCDis_Data + 0x3564)  /* 12 bytes, "mfdmasa3" */
#define data_00491540 ((char *)PPCDis_Data + 0x3570)  /* 8 bytes, "mfdmasr" */
#define data_00491548 ((char *)PPCDis_Data + 0x3578)  /* 8 bytes, "mfexisr" */
#define data_00491550 ((char *)PPCDis_Data + 0x3580)  /* 8 bytes, "mfexier" */
#define data_00491558 ((char *)PPCDis_Data + 0x3588)  /* 8 bytes, "mfiocr" */
#define data_00491560 ((char *)PPCDis_Data + 0x3590)  /* 8 bytes, "mfpmcr0" */
#define data_00491568 ((char *)PPCDis_Data + 0x3598)  /* 8 bytes, "mfdcr" */
#define data_00491570 ((char *)PPCDis_Data + 0x35a0)  /* 8 bytes, "mtbear" */
#define data_00491578 ((char *)PPCDis_Data + 0x35a8)  /* 8 bytes, "mtbesr" */
#define data_00491580 ((char *)PPCDis_Data + 0x35b0)  /* 8 bytes, "mtbesr0" */
#define data_00491588 ((char *)PPCDis_Data + 0x35b8)  /* 8 bytes, "mtbr0" */
#define data_00491590 ((char *)PPCDis_Data + 0x35c0)  /* 8 bytes, "mtbrcr0" */
#define data_00491598 ((char *)PPCDis_Data + 0x35c8)  /* 8 bytes, "mtbr1" */
#define data_004915a0 ((char *)PPCDis_Data + 0x35d0)  /* 8 bytes, "mtbrcr1" */
#define data_004915a8 ((char *)PPCDis_Data + 0x35d8)  /* 8 bytes, "mtbr2" */
#define data_004915b0 ((char *)PPCDis_Data + 0x35e0)  /* 8 bytes, "mtbrcr2" */
#define data_004915b8 ((char *)PPCDis_Data + 0x35e8)  /* 8 bytes, "mtbr3" */
#define data_004915c0 ((char *)PPCDis_Data + 0x35f0)  /* 8 bytes, "mtbrcr3" */
#define data_004915c8 ((char *)PPCDis_Data + 0x35f8)  /* 8 bytes, "mtbr4" */
#define data_004915d0 ((char *)PPCDis_Data + 0x3600)  /* 8 bytes, "mtbrcr4" */
#define data_004915d8 ((char *)PPCDis_Data + 0x3608)  /* 8 bytes, "mtbr5" */
#define data_004915e0 ((char *)PPCDis_Data + 0x3610)  /* 8 bytes, "mtbrcr5" */
#define data_004915e8 ((char *)PPCDis_Data + 0x3618)  /* 8 bytes, "mtbr6" */
#define data_004915f0 ((char *)PPCDis_Data + 0x3620)  /* 8 bytes, "mtbrcr6" */
#define data_004915f8 ((char *)PPCDis_Data + 0x3628)  /* 8 bytes, "mtbr7" */
#define data_00491600 ((char *)PPCDis_Data + 0x3630)  /* 8 bytes, "mtbrcr7" */
#define data_00491608 ((char *)PPCDis_Data + 0x3638)  /* 12 bytes, "mtdmacc0" */
#define data_00491614 ((char *)PPCDis_Data + 0x3644)  /* 12 bytes, "mtdmacc1" */
#define data_00491620 ((char *)PPCDis_Data + 0x3650)  /* 12 bytes, "mtdmacc2" */
#define data_0049162c ((char *)PPCDis_Data + 0x365c)  /* 12 bytes, "mtdmacc3" */
#define data_00491638 ((char *)PPCDis_Data + 0x3668)  /* 12 bytes, "mtdmacr0" */
#define data_00491644 ((char *)PPCDis_Data + 0x3674)  /* 12 bytes, "mtdmacr1" */
#define data_00491650 ((char *)PPCDis_Data + 0x3680)  /* 12 bytes, "mtdmacr2" */
#define data_0049165c ((char *)PPCDis_Data + 0x368c)  /* 12 bytes, "mtdmacr3" */
#define data_00491668 ((char *)PPCDis_Data + 0x3698)  /* 12 bytes, "mtdmact0" */
#define data_00491674 ((char *)PPCDis_Data + 0x36a4)  /* 12 bytes, "mtdmact1" */
#define data_00491680 ((char *)PPCDis_Data + 0x36b0)  /* 12 bytes, "mtdmact2" */
#define data_0049168c ((char *)PPCDis_Data + 0x36bc)  /* 12 bytes, "mtdmact3" */
#define data_00491698 ((char *)PPCDis_Data + 0x36c8)  /* 12 bytes, "mtdmada0" */
#define data_004916a4 ((char *)PPCDis_Data + 0x36d4)  /* 12 bytes, "mtdmada1" */
#define data_004916b0 ((char *)PPCDis_Data + 0x36e0)  /* 12 bytes, "mtdmada2" */
#define data_004916bc ((char *)PPCDis_Data + 0x36ec)  /* 12 bytes, "mtdmada3" */
#define data_004916c8 ((char *)PPCDis_Data + 0x36f8)  /* 12 bytes, "mtdmasa0" */
#define data_004916d4 ((char *)PPCDis_Data + 0x3704)  /* 12 bytes, "mtdmasa1" */
#define data_004916e0 ((char *)PPCDis_Data + 0x3710)  /* 12 bytes, "mtdmasa2" */
#define data_004916ec ((char *)PPCDis_Data + 0x371c)  /* 12 bytes, "mtdmasa3" */
#define data_004916f8 ((char *)PPCDis_Data + 0x3728)  /* 8 bytes, "mtdmasr" */
#define data_00491700 ((char *)PPCDis_Data + 0x3730)  /* 8 bytes, "mtexisr" */
#define data_00491708 ((char *)PPCDis_Data + 0x3738)  /* 8 bytes, "mtexier" */
#define data_00491710 ((char *)PPCDis_Data + 0x3740)  /* 8 bytes, "mtiocr" */
#define data_00491718 ((char *)PPCDis_Data + 0x3748)  /* 8 bytes, "mtpmcr0" */
#define data_00491720 ((char *)PPCDis_Data + 0x3750)  /* 8 bytes, "mtdcr" */
#define data_00491728 ((char *)PPCDis_Data + 0x3758)  /* 8 bytes, "bdnzflr" */
#define data_00491730 ((char *)PPCDis_Data + 0x3760)  /* 8 bytes, "bdzflr" */
#define data_00491738 ((char *)PPCDis_Data + 0x3768)  /* 8 bytes, "bdnztlr" */
#define data_00491740 ((char *)PPCDis_Data + 0x3770)  /* 8 bytes, "bdztlr" */
#define data_00491748 ((char *)PPCDis_Data + 0x3778)  /* 8 bytes, "bdnzlr" */
#define data_00491750 ((char *)PPCDis_Data + 0x3780)  /* 8 bytes, "bdzlr" */
#define data_00491758 ((char *)PPCDis_Data + 0x3788)  /* 4 bytes, "blr" */
#define data_0049175c ((char *)PPCDis_Data + 0x378c)  /* 4 bytes, "+" */
#define data_00491760 ((char *)PPCDis_Data + 0x3790)  /* 4 bytes, "l" */
#define data_00491764 ((char *)PPCDis_Data + 0x3794)  /* 8 bytes, "bctr" */
#define data_0049176c ((char *)PPCDis_Data + 0x379c)  /* 24 bytes, "dCTR_NZERO_AND,%s_%s" */
#define data_00491784 ((char *)PPCDis_Data + 0x37b4)  /* 20 bytes, "dCTR_ZERO_AND,%s_%s" */
#define data_00491798 ((char *)PPCDis_Data + 0x37c8)  /* 16 bytes, "IF_NOT,%s_%s" */
#define data_004917a8 ((char *)PPCDis_Data + 0x37d8)  /* 12 bytes, "IF,%s_%s" */
#define data_004917b4 ((char *)PPCDis_Data + 0x37e4)  /* 16 bytes, "dCTR_NZERO,%d" */
#define data_004917c4 ((char *)PPCDis_Data + 0x37f4)  /* 16 bytes, "dCTR_ZERO,%d" */
#define data_004917d4 ((char *)PPCDis_Data + 0x3804)  /* 12 bytes, "ALWAYS,%d" */
#define data_004917e0 ((char *)PPCDis_Data + 0x3810)  /* 8 bytes, "%d,%d" */
#define data_004917e8 ((char *)PPCDis_Data + 0x3818)  /* 12 bytes, "%d,%d,%d" */
#define data_004917f4 ((char *)PPCDis_Data + 0x3824)  /* 12 bytes, "cr%d,cr%d" */
#define data_00491800 ((char *)PPCDis_Data + 0x3830)  /* 8 bytes, "cr%d_%s" */
#define data_00491808 ((char *)PPCDis_Data + 0x3838)  /* 8 bytes, "twlgt" */
#define data_00491810 ((char *)PPCDis_Data + 0x3840)  /* 8 bytes, "twllt" */
#define data_00491818 ((char *)PPCDis_Data + 0x3848)  /* 8 bytes, "tweq" */
#define data_00491820 ((char *)PPCDis_Data + 0x3850)  /* 8 bytes, "twlge" */
#define data_00491828 ((char *)PPCDis_Data + 0x3858)  /* 8 bytes, "twlle" */
#define data_00491830 ((char *)PPCDis_Data + 0x3860)  /* 8 bytes, "twgt" */
#define data_00491838 ((char *)PPCDis_Data + 0x3868)  /* 8 bytes, "twge" */
#define data_00491840 ((char *)PPCDis_Data + 0x3870)  /* 8 bytes, "twlt" */
#define data_00491848 ((char *)PPCDis_Data + 0x3878)  /* 8 bytes, "twle" */
#define data_00491850 ((char *)PPCDis_Data + 0x3880)  /* 8 bytes, "twne" */
#define data_00491858 ((char *)PPCDis_Data + 0x3888)  /* 4 bytes, "not" */
#define data_0049185c ((char *)PPCDis_Data + 0x388c)  /* 4 bytes, "mr" */
#define data_00491860 ((char *)PPCDis_Data + 0x3890)  /* 4 bytes, "w" */
#define data_00491864 ((char *)PPCDis_Data + 0x3894)  /* 4 bytes, "lw" */
#define data_00491868 ((char *)PPCDis_Data + 0x3898)  /* 8 bytes, "cr%d,%d" */
#define data_00491870 ((char *)PPCDis_Data + 0x38a0)  /* 12 bytes, "cr%d,0x%x" */
#define data_0049187c ((char *)PPCDis_Data + 0x38ac)  /* 8 bytes, "tlbwehi" */
#define data_00491884 ((char *)PPCDis_Data + 0x38b4)  /* 8 bytes, "tlbwelo" */
#define data_0049188c ((char *)PPCDis_Data + 0x38bc)  /* 8 bytes, "tlbwe" */
#define data_00491894 ((char *)PPCDis_Data + 0x38c4)  /* 8 bytes, "tlbrehi" */
#define data_0049189c ((char *)PPCDis_Data + 0x38cc)  /* 8 bytes, "tlbrelo" */
#define data_004918a4 ((char *)PPCDis_Data + 0x38d4)  /* 8 bytes, "dstt" */
#define data_004918ac ((char *)PPCDis_Data + 0x38dc)  /* 8 bytes, "dststt" */
#define data_004918b4 ((char *)PPCDis_Data + 0x38e4)  /* 8 bytes, "dssall" */
#define data_004918bc ((char *)PPCDis_Data + 0x38ec)  /* 8 bytes, "twlgti" */
#define data_004918c4 ((char *)PPCDis_Data + 0x38f4)  /* 8 bytes, "twllti" */
#define data_004918cc ((char *)PPCDis_Data + 0x38fc)  /* 8 bytes, "tweqi" */
#define data_004918d4 ((char *)PPCDis_Data + 0x3904)  /* 8 bytes, "twlgei" */
#define data_004918dc ((char *)PPCDis_Data + 0x390c)  /* 8 bytes, "twllei" */
#define data_004918e4 ((char *)PPCDis_Data + 0x3914)  /* 8 bytes, "twgti" */
#define data_004918ec ((char *)PPCDis_Data + 0x391c)  /* 8 bytes, "twgei" */
#define data_004918f4 ((char *)PPCDis_Data + 0x3924)  /* 8 bytes, "twlti" */
#define data_004918fc ((char *)PPCDis_Data + 0x392c)  /* 8 bytes, "twlei" */
#define data_00491904 ((char *)PPCDis_Data + 0x3934)  /* 8 bytes, "twnei" */
#define data_0049190c ((char *)PPCDis_Data + 0x393c)  /* 8 bytes, "cmpwi" */
#define data_00491914 ((char *)PPCDis_Data + 0x3944)  /* 8 bytes, "cmplwi" */
#define data_0049191c ((char *)PPCDis_Data + 0x394c)  /* 4 bytes, "li" */
#define data_00491920 ((char *)PPCDis_Data + 0x3950)  /* 4 bytes, "lis" */
#define data_00491924 ((char *)PPCDis_Data + 0x3954)  /* 8 bytes, "subi" */
#define data_0049192c ((char *)PPCDis_Data + 0x395c)  /* 4 bytes, "nop" */
#define data_00491930 ((char *)PPCDis_Data + 0x3960)  /* 20 bytes, "; POWER instruction" */
#define data_00491944 ((char *)PPCDis_Data + 0x3974)  /* 4 bytes, "sc" */
#define data_00491948 ((char *)PPCDis_Data + 0x3978)  /* 8 bytes, "bdnzf" */
#define data_00491950 ((char *)PPCDis_Data + 0x3980)  /* 8 bytes, "bdzf" */
#define data_00491958 ((char *)PPCDis_Data + 0x3988)  /* 8 bytes, "bdnzt" */
#define data_00491960 ((char *)PPCDis_Data + 0x3990)  /* 8 bytes, "bdzt" */
#define data_00491968 ((char *)PPCDis_Data + 0x3998)  /* 8 bytes, "bdnz" */
#define data_00491970 ((char *)PPCDis_Data + 0x39a0)  /* 4 bytes, "bdz" */
#define data_00491974 ((char *)PPCDis_Data + 0x39a4)  /* 4 bytes, "a" */
#define data_00491978 ((char *)PPCDis_Data + 0x39a8)  /* 4 bytes, "la" */
#define data_0049197c ((char *)PPCDis_Data + 0x39ac)  /* 4 bytes, "-" */
#define data_00491980 ((char *)PPCDis_Data + 0x39b0)  /* 12 bytes, "; %s%08X" */
#define data_0049198c ((char *)PPCDis_Data + 0x39bc)  /* 16 bytes, a switch table (4 cases; zeros: the compiler makes its own) */
#define data_0049199c ((char *)PPCDis_Data + 0x39cc)  /* 16 bytes, a switch table (4 cases; zeros: the compiler makes its own) */
#define data_004919ac ((char *)PPCDis_Data + 0x39dc)  /* 16 bytes, a switch table (4 cases; zeros: the compiler makes its own) */
#define data_004919bc ((char *)PPCDis_Data + 0x39ec)  /* 12 bytes, a switch table (3 cases; zeros: the compiler makes its own) */
#define data_004919c8 ((char *)PPCDis_Data + 0x39f8)  /* 644 bytes, a switch table (161 cases; zeros: the compiler makes its own) */
#define data_00491c4c ((char *)PPCDis_Data + 0x3c7c)  /* 644 bytes, a switch table (161 cases; zeros: the compiler makes its own) */
#define data_00491ed0 ((char *)PPCDis_Data + 0x3f00)  /* 52 bytes, a switch table (13 cases; zeros: the compiler makes its own) */
#define data_00491f04 ((char *)PPCDis_Data + 0x3f34)  /* 84 bytes, a switch table (21 cases; zeros: the compiler makes its own) */
#define data_00491f58 ((char *)PPCDis_Data + 0x3f88)  /* 24 bytes, a switch table (6 cases; zeros: the compiler makes its own) */
#define data_00491f70 ((char *)PPCDis_Data + 0x3fa0)  /* 16 bytes, a switch table (4 cases; zeros: the compiler makes its own) */
#define data_00491f80 ((char *)PPCDis_Data + 0x3fb0)  /* 36 bytes, a switch table (9 cases; zeros: the compiler makes its own) */
#define data_00491fa4 ((char *)PPCDis_Data + 0x3fd4)  /* 16 bytes, a switch table (4 cases; zeros: the compiler makes its own) */
#define data_00491fb4 ((char *)PPCDis_Data + 0x3fe4)  /* 76 bytes, a switch table (19 cases; zeros: the compiler makes its own) */
#define data_00492000 ((char *)PPCDis_Data + 0x4030)  /* 28 bytes, a switch table (7 cases; zeros: the compiler makes its own) */
#define data_0049201c ((char *)PPCDis_Data + 0x404c)  /* 132 bytes, a switch table (33 cases; zeros: the compiler makes its own) */
#define data_004920a0 ((char *)PPCDis_Data + 0x40d0)  /* 96 bytes, a switch table (24 cases; zeros: the compiler makes its own) */
#define data_00492100 ((char *)PPCDis_Data + 0x4130)  /* 44 bytes, a switch table (11 cases; zeros: the compiler makes its own) */
#define data_0049212c ((char *)PPCDis_Data + 0x415c)  /* 96 bytes, a switch table (24 cases; zeros: the compiler makes its own) */
#define data_0049218c ((char *)PPCDis_Data + 0x41bc)  /* 84 bytes, a switch table (21 cases; zeros: the compiler makes its own) */
#define data_004921e0 ((char *)PPCDis_Data + 0x4210)  /* 52 bytes, a switch table (13 cases; zeros: the compiler makes its own) */
#define data_00492214 ((char *)PPCDis_Data + 0x4244)  /* 16 bytes, a switch table (4 cases; zeros: the compiler makes its own) */
#define data_00492224 ((char *)PPCDis_Data + 0x4254)  /* 16 bytes, a switch table (4 cases; zeros: the compiler makes its own) */
#define data_00492234 ((char *)PPCDis_Data + 0x4264)  /* 76 bytes, a switch table (19 cases; zeros: the compiler makes its own) */

/* 0x0048dfd0: 1, set to 0 on the first call; 0x0048dfd4: 1 when extended mnemonics are on (no 0x8000000). */
#define dis_extended data_0048dfd4

/* The functions (the original's addresses). */
extern unsigned short PPCDis_Disassemble(unsigned char *instruction, long base, unsigned long options,
                                         char *mnemonic, char *operands, char *comment, long pc, long *state,
                                         DisassemblerOperandProc proc);   /* 0x004586a0 */
extern char *PPCDis_Register(unsigned long reg);                         /* 0x004585d0: rN's name */
extern void PPCDis_AddOperand(char *text);                               /* 0x004588f0: ",TEXT" to the operands */
extern void PPCDis_Invalid(long unused, unsigned long insn, long pc, char *mnemonic);       /* 0x004589b0 */
extern void PPCDis_Opcode63(long unused, unsigned long insn, long pc, char *mnemonic);      /* 0x004589c0 */
extern void PPCDis_Opcode31(long unused, unsigned long insn, long pc, char *mnemonic);      /* 0x00458ad0 */
extern void PPCDis_Opcode30(long unused, unsigned long insn, long pc, char *mnemonic);      /* 0x00458b90 */
extern void PPCDis_RotateMDS(long unused, unsigned long insn, long pc, char *mnemonic);
extern void PPCDis_RotateMD(long unused, unsigned long insn, long pc, char *mnemonic);
extern void PPCDis_Rotate(long unused, unsigned long insn, long pc, char *mnemonic);
extern void PPCDis_FloatAForm(long unused, unsigned long insn, long pc, char *mnemonic);
extern void PPCDis_Isel(long unused, unsigned long insn, long pc, char *mnemonic);
extern void PPCDis_IntegerArithmetic(long unused, unsigned long insn, long pc, char *mnemonic);
extern void PPCDis_Sradi(long unused, unsigned long insn, long pc, char *mnemonic);
extern void PPCDis_Mtfsf(long unused, unsigned long insn, long pc, char *mnemonic);
extern void PPCDis_MoveSpecialRegister(long unused, unsigned long insn, long pc, char *mnemonic);
extern void PPCDis_Opcode19(long unused, unsigned long insn, long pc, char *mnemonic);
extern void PPCDis_Opcode4(long unused, unsigned long insn, long pc, char *mnemonic);
extern void PPCDis_XForm(long unused, unsigned long insn, long pc, char *mnemonic);
extern void PPCDis_LoadDoubleword(long unused, unsigned long insn, long pc, char *mnemonic);
extern void PPCDis_DForm(long unused, unsigned long insn, long pc, char *mnemonic);
extern void PPCDis_SystemCall(long unused, unsigned long insn, long pc, char *mnemonic);
extern void PPCDis_BranchConditional(long unused, unsigned long insn, long pc, char *mnemonic);
extern void PPCDis_Branch(long unused, unsigned long insn, long pc, char *mnemonic);

char dis_started;
char data_0049acf9[0x20];
char data_0049ad19[0x20];
char data_0049ad39[0x40];
char data_0049ad79[0x20];
char data_0049ad99[0x20];
char data_0049adb9[0x20];
char data_0049add9[0x20];
char data_0049adf9[0x20];
char data_0049ae19[0x20];
char data_0049ae39[0x20];
char data_0049ae59[0x20];
char data_0049ae79[0x20];
char data_0049ae99[0x21];
char *dis_hexPrefix;
char dis_operandText[0x200];
long data_0049b0be;
long dis_operandLength;
short dis_flags;
unsigned long dis_processors;
unsigned long dis_options;
DisassemblerOperandProc dis_proc;
long dis_pc;
long dis_base;
unsigned char *dis_instruction;
char *dis_comment;
char *dis_mnemonic;
char *dis_operands;

unsigned long PPCDis_Data[4268] = {
    /* 0x0048dfd0 4 bytes */
    0x00000001,
    /* 0x0048dfd4 4 bytes */
    0x00000001,
    /* 0x0048dfd8 4 bytes, "LT" */
    0x0000544c,
    /* 0x0048dfdc 4 bytes, "GT" */
    0x00005447,
    /* 0x0048dfe0 4 bytes, "EQ" */
    0x00005145,
    /* 0x0048dfe4 4 bytes, "SO" */
    0x00004f53,
    /* 0x0048dfe8 16 bytes, 4 pointers: data_0048dfd8, data_0048dfdc, data_0048dfe0, data_0048dfe4 */
    (unsigned long)((char *)PPCDis_Data + 0x8),
    (unsigned long)((char *)PPCDis_Data + 0xc),
    (unsigned long)((char *)PPCDis_Data + 0x10),
    (unsigned long)((char *)PPCDis_Data + 0x14),
    /* 0x0048dff8 16 bytes, 4 pointers: data_0048dfd8, data_0048dfdc, data_0048dfe0, data_0048dfe4 */
    (unsigned long)((char *)PPCDis_Data + 0x8),
    (unsigned long)((char *)PPCDis_Data + 0xc),
    (unsigned long)((char *)PPCDis_Data + 0x10),
    (unsigned long)((char *)PPCDis_Data + 0x14),
    /* 0x0048e008 4 bytes, "GE" */
    0x00004547,
    /* 0x0048e00c 4 bytes, "LE" */
    0x0000454c,
    /* 0x0048e010 4 bytes, "NE" */
    0x0000454e,
    /* 0x0048e014 4 bytes, "NS" */
    0x0000534e,
    /* 0x0048e018 16 bytes, 4 pointers: data_0048e008, data_0048e00c, data_0048e010, data_0048e014 */
    (unsigned long)((char *)PPCDis_Data + 0x38),
    (unsigned long)((char *)PPCDis_Data + 0x3c),
    (unsigned long)((char *)PPCDis_Data + 0x40),
    (unsigned long)((char *)PPCDis_Data + 0x44),
    /* 0x0048e028 64 bytes, 64 bytes */
    0x03031313,
    0x03131312,
    0x03031303,
    0x03030303,
    0x06000201,
    0x0c0c0c0c,
    0x03030303,
    0x100f0303,
    0x03030303,
    0x03030303,
    0x03030303,
    0x03030303,
    0x03030303,
    0x03030303,
    0x0b040303,
    0x11040303,
    /* 0x0048e068 4 bytes, "b" */
    0x00000062,
    /* 0x0048e06c 4 bytes, "bl" */
    0x00006c62,
    /* 0x0048e070 4 bytes, "ba" */
    0x00006162,
    /* 0x0048e074 4 bytes, "bla" */
    0x00616c62,
    /* 0x0048e078 16 bytes, 4 pointers: data_0048e068, data_0048e06c, data_0048e070, data_0048e074 */
    (unsigned long)((char *)PPCDis_Data + 0x98),
    (unsigned long)((char *)PPCDis_Data + 0x9c),
    (unsigned long)((char *)PPCDis_Data + 0xa0),
    (unsigned long)((char *)PPCDis_Data + 0xa4),
    /* 0x0048e088 4 bytes, "bc" */
    0x00006362,
    /* 0x0048e08c 4 bytes, "bcl" */
    0x006c6362,
    /* 0x0048e090 4 bytes, "bca" */
    0x00616362,
    /* 0x0048e094 8 bytes, "bcla" */
    0x616c6362,
    0x00000000,
    /* 0x0048e09c 16 bytes, 4 pointers: data_0048e088, data_0048e08c, data_0048e090, data_0048e094 */
    (unsigned long)((char *)PPCDis_Data + 0xb8),
    (unsigned long)((char *)PPCDis_Data + 0xbc),
    (unsigned long)((char *)PPCDis_Data + 0xc0),
    (unsigned long)((char *)PPCDis_Data + 0xc4),
    /* 0x0048e0ac 4 bytes, "blt" */
    0x00746c62,
    /* 0x0048e0b0 4 bytes, "bgt" */
    0x00746762,
    /* 0x0048e0b4 4 bytes, "beq" */
    0x00716562,
    /* 0x0048e0b8 4 bytes, "bso" */
    0x006f7362,
    /* 0x0048e0bc 4 bytes, "bge" */
    0x00656762,
    /* 0x0048e0c0 4 bytes, "ble" */
    0x00656c62,
    /* 0x0048e0c4 4 bytes, "bne" */
    0x00656e62,
    /* 0x0048e0c8 4 bytes, "bns" */
    0x00736e62,
    /* 0x0048e0cc 32 bytes, 8 pointers: data_0048e0ac, data_0048e0b0, data_0048e0b4, data_0048e0b8 ... */
    (unsigned long)((char *)PPCDis_Data + 0xdc),
    (unsigned long)((char *)PPCDis_Data + 0xe0),
    (unsigned long)((char *)PPCDis_Data + 0xe4),
    (unsigned long)((char *)PPCDis_Data + 0xe8),
    (unsigned long)((char *)PPCDis_Data + 0xec),
    (unsigned long)((char *)PPCDis_Data + 0xf0),
    (unsigned long)((char *)PPCDis_Data + 0xf4),
    (unsigned long)((char *)PPCDis_Data + 0xf8),
    /* 0x0048e0ec 64 bytes, 64 bytes */
    0x07070000,
    0x02000000,
    0x05060202,
    0x02020202,
    0x00000000,
    0x00000000,
    0x04040404,
    0x00000404,
    0x01010101,
    0x03030303,
    0x01010101,
    0x03010303,
    0x09080908,
    0x09080908,
    0x00000a0a,
    0x00000a0a,
    /* 0x0048e12c 4 bytes, "tdi" */
    0x00696474,
    /* 0x0048e130 4 bytes, "twi" */
    0x00697774,
    /* 0x0048e134 8 bytes, "mulli" */
    0x6c6c756d,
    0x00000069,
    /* 0x0048e13c 8 bytes, "subfic" */
    0x66627573,
    0x00006369,
    /* 0x0048e144 8 bytes, "dozi" */
    0x697a6f64,
    0x00000000,
    /* 0x0048e14c 8 bytes, "cmpli" */
    0x6c706d63,
    0x00000069,
    /* 0x0048e154 8 bytes, "cmpi" */
    0x69706d63,
    0x00000000,
    /* 0x0048e15c 8 bytes, "addic" */
    0x69646461,
    0x00000063,
    /* 0x0048e164 8 bytes, "addic." */
    0x69646461,
    0x00002e63,
    /* 0x0048e16c 8 bytes, "addi" */
    0x69646461,
    0x00000000,
    /* 0x0048e174 8 bytes, "addis" */
    0x69646461,
    0x00000073,
    /* 0x0048e17c 4 bytes, "ori" */
    0x0069726f,
    /* 0x0048e180 8 bytes, "oris" */
    0x7369726f,
    0x00000000,
    /* 0x0048e188 8 bytes, "xori" */
    0x69726f78,
    0x00000000,
    /* 0x0048e190 8 bytes, "xoris" */
    0x69726f78,
    0x00000073,
    /* 0x0048e198 8 bytes, "andi." */
    0x69646e61,
    0x0000002e,
    /* 0x0048e1a0 8 bytes, "andis." */
    0x69646e61,
    0x00002e73,
    /* 0x0048e1a8 4 bytes, "lwz" */
    0x007a776c,
    /* 0x0048e1ac 8 bytes, "lwzu" */
    0x757a776c,
    0x00000000,
    /* 0x0048e1b4 4 bytes, "lbz" */
    0x007a626c,
    /* 0x0048e1b8 8 bytes, "lbzu" */
    0x757a626c,
    0x00000000,
    /* 0x0048e1c0 4 bytes, "stw" */
    0x00777473,
    /* 0x0048e1c4 8 bytes, "stwu" */
    0x75777473,
    0x00000000,
    /* 0x0048e1cc 4 bytes, "stb" */
    0x00627473,
    /* 0x0048e1d0 8 bytes, "stbu" */
    0x75627473,
    0x00000000,
    /* 0x0048e1d8 4 bytes, "lhz" */
    0x007a686c,
    /* 0x0048e1dc 8 bytes, "lhzu" */
    0x757a686c,
    0x00000000,
    /* 0x0048e1e4 4 bytes, "lha" */
    0x0061686c,
    /* 0x0048e1e8 8 bytes, "lhau" */
    0x7561686c,
    0x00000000,
    /* 0x0048e1f0 4 bytes, "sth" */
    0x00687473,
    /* 0x0048e1f4 8 bytes, "sthu" */
    0x75687473,
    0x00000000,
    /* 0x0048e1fc 4 bytes, "lmw" */
    0x00776d6c,
    /* 0x0048e200 8 bytes, "stmw" */
    0x776d7473,
    0x00000000,
    /* 0x0048e208 4 bytes, "lfs" */
    0x0073666c,
    /* 0x0048e20c 8 bytes, "lfsu" */
    0x7573666c,
    0x00000000,
    /* 0x0048e214 4 bytes, "lfd" */
    0x0064666c,
    /* 0x0048e218 8 bytes, "lfdu" */
    0x7564666c,
    0x00000000,
    /* 0x0048e220 8 bytes, "stfs" */
    0x73667473,
    0x00000000,
    /* 0x0048e228 8 bytes, "stfsu" */
    0x73667473,
    0x00000075,
    /* 0x0048e230 8 bytes, "stfd" */
    0x64667473,
    0x00000000,
    /* 0x0048e238 8 bytes, "stfdu" */
    0x64667473,
    0x00000075,
    /* 0x0048e240 8 bytes, "psq_l" */
    0x5f717370,
    0x0000006c,
    /* 0x0048e248 8 bytes, "psq_lu" */
    0x5f717370,
    0x0000756c,
    /* 0x0048e250 8 bytes, "psq_st" */
    0x5f717370,
    0x00007473,
    /* 0x0048e258 8 bytes, "psq_stu" */
    0x5f717370,
    0x00757473,
    /* 0x0048e260 248 bytes, 45 pointers: data_0048e12c, data_0048e130, data_0048e134, data_0048e13c ... */
    0x00000000,
    0x00000000,
    (unsigned long)((char *)PPCDis_Data + 0x15c),
    (unsigned long)((char *)PPCDis_Data + 0x160),
    0x00000000,
    0x00000000,
    0x00000000,
    (unsigned long)((char *)PPCDis_Data + 0x164),
    (unsigned long)((char *)PPCDis_Data + 0x16c),
    (unsigned long)((char *)PPCDis_Data + 0x174),
    (unsigned long)((char *)PPCDis_Data + 0x17c),
    (unsigned long)((char *)PPCDis_Data + 0x184),
    (unsigned long)((char *)PPCDis_Data + 0x18c),
    (unsigned long)((char *)PPCDis_Data + 0x194),
    (unsigned long)((char *)PPCDis_Data + 0x19c),
    (unsigned long)((char *)PPCDis_Data + 0x1a4),
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    (unsigned long)((char *)PPCDis_Data + 0x1ac),
    (unsigned long)((char *)PPCDis_Data + 0x1b0),
    (unsigned long)((char *)PPCDis_Data + 0x1b8),
    (unsigned long)((char *)PPCDis_Data + 0x1c0),
    (unsigned long)((char *)PPCDis_Data + 0x1c8),
    (unsigned long)((char *)PPCDis_Data + 0x1d0),
    0x00000000,
    0x00000000,
    (unsigned long)((char *)PPCDis_Data + 0x1d8),
    (unsigned long)((char *)PPCDis_Data + 0x1dc),
    (unsigned long)((char *)PPCDis_Data + 0x1e4),
    (unsigned long)((char *)PPCDis_Data + 0x1e8),
    (unsigned long)((char *)PPCDis_Data + 0x1f0),
    (unsigned long)((char *)PPCDis_Data + 0x1f4),
    (unsigned long)((char *)PPCDis_Data + 0x1fc),
    (unsigned long)((char *)PPCDis_Data + 0x200),
    (unsigned long)((char *)PPCDis_Data + 0x208),
    (unsigned long)((char *)PPCDis_Data + 0x20c),
    (unsigned long)((char *)PPCDis_Data + 0x214),
    (unsigned long)((char *)PPCDis_Data + 0x218),
    (unsigned long)((char *)PPCDis_Data + 0x220),
    (unsigned long)((char *)PPCDis_Data + 0x224),
    (unsigned long)((char *)PPCDis_Data + 0x22c),
    (unsigned long)((char *)PPCDis_Data + 0x230),
    (unsigned long)((char *)PPCDis_Data + 0x238),
    (unsigned long)((char *)PPCDis_Data + 0x23c),
    (unsigned long)((char *)PPCDis_Data + 0x244),
    (unsigned long)((char *)PPCDis_Data + 0x248),
    (unsigned long)((char *)PPCDis_Data + 0x250),
    (unsigned long)((char *)PPCDis_Data + 0x258),
    (unsigned long)((char *)PPCDis_Data + 0x260),
    (unsigned long)((char *)PPCDis_Data + 0x268),
    (unsigned long)((char *)PPCDis_Data + 0x270),
    (unsigned long)((char *)PPCDis_Data + 0x278),
    0x00000000,
    0x00000000,
    (unsigned long)((char *)PPCDis_Data + 0x280),
    (unsigned long)((char *)PPCDis_Data + 0x288),
    /* 0x0048e358 4 bytes, "ld" */
    0x0000646c,
    /* 0x0048e35c 4 bytes, "ldu" */
    0x0075646c,
    /* 0x0048e360 4 bytes, "lwa" */
    0x0061776c,
    /* 0x0048e364 12 bytes, 3 pointers: data_0048e358, data_0048e35c, data_0048e360 */
    (unsigned long)((char *)PPCDis_Data + 0x388),
    (unsigned long)((char *)PPCDis_Data + 0x38c),
    (unsigned long)((char *)PPCDis_Data + 0x390),
    /* 0x0048e370 8 bytes, "?????" */
    0x3f3f3f3f,
    0x0000003f,
    /* 0x0048e378 16 bytes, 1 pointers: data_0048e370 */
    0x00170000,
    (unsigned long)((char *)PPCDis_Data + 0x3a0),
    0x00000000,
    0x00000000,
    /* 0x0048e388 4 bytes, "cmp" */
    0x00706d63,
    /* 0x0048e38c 4 bytes, "tw" */
    0x00007774,
    /* 0x0048e390 8 bytes, "lvsl" */
    0x6c73766c,
    0x00000000,
    /* 0x0048e398 8 bytes, "lvebx" */
    0x6265766c,
    0x00000078,
    /* 0x0048e3a0 8 bytes, "mfcr" */
    0x7263666d,
    0x00000000,
    /* 0x0048e3a8 8 bytes, "lwarx" */
    0x7261776c,
    0x00000078,
    /* 0x0048e3b0 4 bytes, "ldx" */
    0x0078646c,
    /* 0x0048e3b4 8 bytes, "lwzx" */
    0x787a776c,
    0x00000000,
    /* 0x0048e3bc 4 bytes, "slw" */
    0x00776c73,
    /* 0x0048e3c0 8 bytes, "cntlzw" */
    0x6c746e63,
    0x0000777a,
    /* 0x0048e3c8 4 bytes, "sld" */
    0x00646c73,
    /* 0x0048e3cc 4 bytes, "and" */
    0x00646e61,
    /* 0x0048e3d0 8 bytes, "maskg" */
    0x6b73616d,
    0x00000067,
    /* 0x0048e3d8 8 bytes, "cmpl" */
    0x6c706d63,
    0x00000000,
    /* 0x0048e3e0 8 bytes, "lvsr" */
    0x7273766c,
    0x00000000,
    /* 0x0048e3e8 8 bytes, "lvehx" */
    0x6865766c,
    0x00000078,
    /* 0x0048e3f0 8 bytes, "ldux" */
    0x7875646c,
    0x00000000,
    /* 0x0048e3f8 8 bytes, "dcbst" */
    0x73626364,
    0x00000074,
    /* 0x0048e400 8 bytes, "lwzux" */
    0x757a776c,
    0x00000078,
    /* 0x0048e408 8 bytes, "cntlzd" */
    0x6c746e63,
    0x0000647a,
    /* 0x0048e410 8 bytes, "andc" */
    0x63646e61,
    0x00000000,
    /* 0x0048e418 4 bytes, "td" */
    0x00006474,
    /* 0x0048e41c 8 bytes, "lvewx" */
    0x7765766c,
    0x00000078,
    /* 0x0048e424 8 bytes, "mfmsr" */
    0x736d666d,
    0x00000072,
    /* 0x0048e42c 8 bytes, "ldarx" */
    0x7261646c,
    0x00000078,
    /* 0x0048e434 8 bytes, "dcbf" */
    0x66626364,
    0x00000000,
    /* 0x0048e43c 8 bytes, "lbzx" */
    0x787a626c,
    0x00000000,
    /* 0x0048e444 4 bytes, "lvx" */
    0x0078766c,
    /* 0x0048e448 8 bytes, "lbzux" */
    0x757a626c,
    0x00000078,
    /* 0x0048e450 4 bytes, "nor" */
    0x00726f6e,
    /* 0x0048e454 8 bytes, "wrtee" */
    0x65747277,
    0x00000065,
    /* 0x0048e45c 8 bytes, "stvebx" */
    0x65767473,
    0x00007862,
    /* 0x0048e464 8 bytes, "mtmsr" */
    0x736d746d,
    0x00000072,
    /* 0x0048e46c 8 bytes, "stdx" */
    0x78647473,
    0x00000000,
    /* 0x0048e474 8 bytes, "stwcx" */
    0x63777473,
    0x00000078,
    /* 0x0048e47c 8 bytes, "stwx" */
    0x78777473,
    0x00000000,
    /* 0x0048e484 4 bytes, "slq" */
    0x00716c73,
    /* 0x0048e488 4 bytes, "sle" */
    0x00656c73,
    /* 0x0048e48c 8 bytes, "wrteei" */
    0x65747277,
    0x00006965,
    /* 0x0048e494 8 bytes, "stvehx" */
    0x65767473,
    0x00007868,
    /* 0x0048e49c 8 bytes, "stdux" */
    0x75647473,
    0x00000078,
    /* 0x0048e4a4 8 bytes, "stwux" */
    0x75777473,
    0x00000078,
    /* 0x0048e4ac 8 bytes, "sliq" */
    0x71696c73,
    0x00000000,
    /* 0x0048e4b4 8 bytes, "stvewx" */
    0x65767473,
    0x00007877,
    /* 0x0048e4bc 8 bytes, "mtsr" */
    0x7273746d,
    0x00000000,
    /* 0x0048e4c4 8 bytes, "stdcx" */
    0x63647473,
    0x00000078,
    /* 0x0048e4cc 8 bytes, "stbx" */
    0x78627473,
    0x00000000,
    /* 0x0048e4d4 8 bytes, "sllq" */
    0x716c6c73,
    0x00000000,
    /* 0x0048e4dc 8 bytes, "sleq" */
    0x71656c73,
    0x00000000,
    /* 0x0048e4e4 8 bytes, "stvx" */
    0x78767473,
    0x00000000,
    /* 0x0048e4ec 8 bytes, "mtsrin" */
    0x7273746d,
    0x00006e69,
    /* 0x0048e4f4 8 bytes, "dcbtst" */
    0x74626364,
    0x00007473,
    /* 0x0048e4fc 8 bytes, "stbux" */
    0x75627473,
    0x00000078,
    /* 0x0048e504 8 bytes, "slliq" */
    0x696c6c73,
    0x00000071,
    /* 0x0048e50c 8 bytes, "icbt" */
    0x74626369,
    0x00000000,
    /* 0x0048e514 8 bytes, "lscbx" */
    0x6263736c,
    0x00000078,
    /* 0x0048e51c 8 bytes, "dcbt" */
    0x74626364,
    0x00000000,
    /* 0x0048e524 8 bytes, "lhzx" */
    0x787a686c,
    0x00000000,
    /* 0x0048e52c 4 bytes, "eqv" */
    0x00767165,
    /* 0x0048e530 8 bytes, "tlbie" */
    0x69626c74,
    0x00000065,
    /* 0x0048e538 8 bytes, "eciwx" */
    0x77696365,
    0x00000078,
    /* 0x0048e540 8 bytes, "lhzux" */
    0x757a686c,
    0x00000078,
    /* 0x0048e548 4 bytes, "xor" */
    0x00726f78,
    /* 0x0048e54c 8 bytes, "lwax" */
    0x7861776c,
    0x00000000,
    /* 0x0048e554 4 bytes, "dst" */
    0x00747364,
    /* 0x0048e558 8 bytes, "lhax" */
    0x7861686c,
    0x00000000,
    /* 0x0048e560 8 bytes, "lvxl" */
    0x6c78766c,
    0x00000000,
    /* 0x0048e568 8 bytes, "tlbia" */
    0x69626c74,
    0x00000061,
    /* 0x0048e570 8 bytes, "lwaux" */
    0x7561776c,
    0x00000078,
    /* 0x0048e578 8 bytes, "dstst" */
    0x73747364,
    0x00000074,
    /* 0x0048e580 8 bytes, "lhaux" */
    0x7561686c,
    0x00000078,
    /* 0x0048e588 8 bytes, "sthx" */
    0x78687473,
    0x00000000,
    /* 0x0048e590 4 bytes, "orc" */
    0x0063726f,
    /* 0x0048e594 8 bytes, "slbie" */
    0x69626c73,
    0x00000065,
    /* 0x0048e59c 8 bytes, "ecowx" */
    0x776f6365,
    0x00000078,
    /* 0x0048e5a4 8 bytes, "sthux" */
    0x75687473,
    0x00000078,
    /* 0x0048e5ac 4 bytes, "or" */
    0x0000726f,
    /* 0x0048e5b0 8 bytes, "dccci" */
    0x63636364,
    0x00000069,
    /* 0x0048e5b8 8 bytes, "dcbi" */
    0x69626364,
    0x00000000,
    /* 0x0048e5c0 8 bytes, "nand" */
    0x646e616e,
    0x00000000,
    /* 0x0048e5c8 8 bytes, "dcread" */
    0x65726364,
    0x00006461,
    /* 0x0048e5d0 8 bytes, "stvxl" */
    0x78767473,
    0x0000006c,
    /* 0x0048e5d8 8 bytes, "slbia" */
    0x69626c73,
    0x00000061,
    /* 0x0048e5e0 8 bytes, "mcrxr" */
    0x7872636d,
    0x00000072,
    /* 0x0048e5e8 8 bytes, "lswx" */
    0x7877736c,
    0x00000000,
    /* 0x0048e5f0 8 bytes, "lwbrx" */
    0x7262776c,
    0x00000078,
    /* 0x0048e5f8 8 bytes, "lfsx" */
    0x7873666c,
    0x00000000,
    /* 0x0048e600 4 bytes, "srw" */
    0x00777273,
    /* 0x0048e604 8 bytes, "rrib" */
    0x62697272,
    0x00000000,
    /* 0x0048e60c 4 bytes, "srd" */
    0x00647273,
    /* 0x0048e610 8 bytes, "maskir" */
    0x6b73616d,
    0x00007269,
    /* 0x0048e618 8 bytes, "tlbsync" */
    0x73626c74,
    0x00636e79,
    /* 0x0048e620 8 bytes, "lfsux" */
    0x7573666c,
    0x00000078,
    /* 0x0048e628 8 bytes, "mfsr" */
    0x7273666d,
    0x00000000,
    /* 0x0048e630 4 bytes, "esa" */
    0x00617365,
    /* 0x0048e634 8 bytes, "lswi" */
    0x6977736c,
    0x00000000,
    /* 0x0048e63c 8 bytes, "sync" */
    0x636e7973,
    0x00000000,
    /* 0x0048e644 8 bytes, "lfdx" */
    0x7864666c,
    0x00000000,
    /* 0x0048e64c 4 bytes, "dsa" */
    0x00617364,
    /* 0x0048e650 8 bytes, "lfdux" */
    0x7564666c,
    0x00000078,
    /* 0x0048e658 8 bytes, "mfsrin" */
    0x7273666d,
    0x00006e69,
    /* 0x0048e660 8 bytes, "stswx" */
    0x77737473,
    0x00000078,
    /* 0x0048e668 8 bytes, "stwbrx" */
    0x62777473,
    0x00007872,
    /* 0x0048e670 8 bytes, "stfsx" */
    0x73667473,
    0x00000078,
    /* 0x0048e678 4 bytes, "srq" */
    0x00717273,
    /* 0x0048e67c 4 bytes, "sre" */
    0x00657273,
    /* 0x0048e680 8 bytes, "stfsux" */
    0x73667473,
    0x00007875,
    /* 0x0048e688 8 bytes, "sriq" */
    0x71697273,
    0x00000000,
    /* 0x0048e690 8 bytes, "stswi" */
    0x77737473,
    0x00000069,
    /* 0x0048e698 8 bytes, "stfdx" */
    0x64667473,
    0x00000078,
    /* 0x0048e6a0 8 bytes, "srlq" */
    0x716c7273,
    0x00000000,
    /* 0x0048e6a8 8 bytes, "sreq" */
    0x71657273,
    0x00000000,
    /* 0x0048e6b0 8 bytes, "stfdux" */
    0x64667473,
    0x00007875,
    /* 0x0048e6b8 8 bytes, "srliq" */
    0x696c7273,
    0x00000071,
    /* 0x0048e6c0 8 bytes, "lhbrx" */
    0x7262686c,
    0x00000078,
    /* 0x0048e6c8 8 bytes, "sraw" */
    0x77617273,
    0x00000000,
    /* 0x0048e6d0 8 bytes, "srad" */
    0x64617273,
    0x00000000,
    /* 0x0048e6d8 4 bytes, "dss" */
    0x00737364,
    /* 0x0048e6dc 8 bytes, "srawi" */
    0x77617273,
    0x00000069,
    /* 0x0048e6e4 8 bytes, "eieio" */
    0x69656965,
    0x0000006f,
    /* 0x0048e6ec 8 bytes, "tlbsx" */
    0x73626c74,
    0x00000078,
    /* 0x0048e6f4 8 bytes, "sthbrx" */
    0x62687473,
    0x00007872,
    /* 0x0048e6fc 8 bytes, "sraq" */
    0x71617273,
    0x00000000,
    /* 0x0048e704 8 bytes, "srea" */
    0x61657273,
    0x00000000,
    /* 0x0048e70c 8 bytes, "extsh" */
    0x73747865,
    0x00000068,
    /* 0x0048e714 8 bytes, "tlbre" */
    0x72626c74,
    0x00000065,
    /* 0x0048e71c 8 bytes, "sraiq" */
    0x69617273,
    0x00000071,
    /* 0x0048e724 8 bytes, "extsb" */
    0x73747865,
    0x00000062,
    /* 0x0048e72c 8 bytes, "iccci" */
    0x63636369,
    0x00000069,
    /* 0x0048e734 8 bytes, "tlbld" */
    0x6c626c74,
    0x00000064,
    /* 0x0048e73c 8 bytes, "icbi" */
    0x69626369,
    0x00000000,
    /* 0x0048e744 8 bytes, "stfiwx" */
    0x69667473,
    0x00007877,
    /* 0x0048e74c 8 bytes, "extsw" */
    0x73747865,
    0x00000077,
    /* 0x0048e754 8 bytes, "icread" */
    0x65726369,
    0x00006461,
    /* 0x0048e75c 8 bytes, "tlbli" */
    0x6c626c74,
    0x00000069,
    /* 0x0048e764 8 bytes, "dcbz" */
    0x7a626364,
    0x00000000,
    /* 0x0048e76c 2176 bytes, 136 pointers: data_0048e388, data_0048e38c, data_0048e390, data_0048e398 ... */
    0x00100000,
    (unsigned long)((char *)PPCDis_Data + 0x3b8),
    0x000fffff,
    0x00000000,
    0x00150004,
    (unsigned long)((char *)PPCDis_Data + 0x3bc),
    0x000fffff,
    0x00000000,
    0x001f0006,
    (unsigned long)((char *)PPCDis_Data + 0x3c0),
    0x00090000,
    0x00000000,
    0x001f0007,
    (unsigned long)((char *)PPCDis_Data + 0x3c8),
    0x00090000,
    0x00000000,
    0x00050013,
    (unsigned long)((char *)PPCDis_Data + 0x3d0),
    0x000fffff,
    0x00000000,
    0x00010014,
    (unsigned long)((char *)PPCDis_Data + 0x3d8),
    0x000fffff,
    0x00000000,
    0x00010015,
    (unsigned long)((char *)PPCDis_Data + 0x3e0),
    0x00002000,
    0x40000000,
    0x00010017,
    (unsigned long)((char *)PPCDis_Data + 0x3e4),
    0x000fffff,
    0x00000000,
    0x00080018,
    (unsigned long)((char *)PPCDis_Data + 0x3ec),
    0x000fffff,
    0x00000000,
    0x000b001a,
    (unsigned long)((char *)PPCDis_Data + 0x3f0),
    0x000fffff,
    0x00000000,
    0x0008001b,
    (unsigned long)((char *)PPCDis_Data + 0x3f8),
    0x00002000,
    0x40000000,
    0x0008001c,
    (unsigned long)((char *)PPCDis_Data + 0x3fc),
    0x000fffff,
    0x00000000,
    0x0008001d,
    (unsigned long)((char *)PPCDis_Data + 0x400),
    0x00000002,
    0x00000000,
    0x00100020,
    (unsigned long)((char *)PPCDis_Data + 0x408),
    0x000fffff,
    0x00000000,
    0x001f0026,
    (unsigned long)((char *)PPCDis_Data + 0x410),
    0x00090000,
    0x00000000,
    0x001f0027,
    (unsigned long)((char *)PPCDis_Data + 0x418),
    0x00090000,
    0x00000000,
    0x00010035,
    (unsigned long)((char *)PPCDis_Data + 0x420),
    0x00002000,
    0x40000000,
    0x00160036,
    (unsigned long)((char *)PPCDis_Data + 0x428),
    0x000ff3ff,
    0x00000000,
    0x00010037,
    (unsigned long)((char *)PPCDis_Data + 0x430),
    0x000fffff,
    0x00000000,
    0x000b003a,
    (unsigned long)((char *)PPCDis_Data + 0x438),
    0x00002000,
    0x40000000,
    0x0008003c,
    (unsigned long)((char *)PPCDis_Data + 0x440),
    0x000fffff,
    0x00000000,
    0x00150044,
    (unsigned long)((char *)PPCDis_Data + 0x448),
    0x00002000,
    0x40000000,
    0x001f0047,
    (unsigned long)((char *)PPCDis_Data + 0x44c),
    0x00090000,
    0x00000000,
    0x00050053,
    (unsigned long)((char *)PPCDis_Data + 0x454),
    0x000fffff,
    0x00000002,
    0x00010054,
    (unsigned long)((char *)PPCDis_Data + 0x45c),
    0x00002000,
    0x40000000,
    0x00160056,
    (unsigned long)((char *)PPCDis_Data + 0x464),
    0x000ff3ff,
    0x00000000,
    0x00010057,
    (unsigned long)((char *)PPCDis_Data + 0x46c),
    0x000fffff,
    0x00000000,
    0x001f0067,
    (unsigned long)((char *)PPCDis_Data + 0x474),
    0x00090000,
    0x00000000,
    0x00010077,
    (unsigned long)((char *)PPCDis_Data + 0x478),
    0x000fffff,
    0x00000000,
    0x0008007c,
    (unsigned long)((char *)PPCDis_Data + 0x480),
    0x000fffff,
    0x00000000,
    0x000d0083,
    (unsigned long)((char *)PPCDis_Data + 0x484),
    0x00000040,
    0x00000000,
    0x001f0087,
    (unsigned long)((char *)PPCDis_Data + 0x48c),
    0x00090000,
    0x00000000,
    0x000d0092,
    (unsigned long)((char *)PPCDis_Data + 0x494),
    0x000fffff,
    0x00000002,
    0x000a0095,
    (unsigned long)((char *)PPCDis_Data + 0x49c),
    0x00002000,
    0x40000000,
    0x00090096,
    (unsigned long)((char *)PPCDis_Data + 0x4a4),
    0x000fffff,
    0x00000000,
    0x000a0097,
    (unsigned long)((char *)PPCDis_Data + 0x4ac),
    0x000fffff,
    0x00000000,
    0x00080098,
    (unsigned long)((char *)PPCDis_Data + 0x4b4),
    0x00000002,
    0x00000000,
    0x00080099,
    (unsigned long)((char *)PPCDis_Data + 0x4b8),
    0x00000002,
    0x00000000,
    0x001c00a3,
    (unsigned long)((char *)PPCDis_Data + 0x4bc),
    0x00000040,
    0x00000000,
    0x001f00a7,
    (unsigned long)((char *)PPCDis_Data + 0x4c4),
    0x00090000,
    0x00000000,
    0x000a00b5,
    (unsigned long)((char *)PPCDis_Data + 0x4cc),
    0x000fffff,
    0x40000000,
    0x000a00b7,
    (unsigned long)((char *)PPCDis_Data + 0x4d4),
    0x000fffff,
    0x00000000,
    0x000f00b8,
    (unsigned long)((char *)PPCDis_Data + 0x4dc),
    0x00000002,
    0x00000000,
    0x001f00c7,
    (unsigned long)((char *)PPCDis_Data + 0x4e4),
    0x00090000,
    0x00000000,
    0x000e00d2,
    (unsigned long)((char *)PPCDis_Data + 0x4ec),
    0x000ff01f,
    0x00000002,
    0x000900d6,
    (unsigned long)((char *)PPCDis_Data + 0x4f4),
    0x00002000,
    0x40000000,
    0x000a00d7,
    (unsigned long)((char *)PPCDis_Data + 0x4fc),
    0x000fffff,
    0x00000000,
    0x000800d8,
    (unsigned long)((char *)PPCDis_Data + 0x504),
    0x00000002,
    0x00000000,
    0x000800d9,
    (unsigned long)((char *)PPCDis_Data + 0x50c),
    0x00000002,
    0x00000000,
    0x001f00e7,
    (unsigned long)((char *)PPCDis_Data + 0x514),
    0x00090000,
    0x00000000,
    0x000c00f2,
    (unsigned long)((char *)PPCDis_Data + 0x51c),
    0x000ff01f,
    0x00000002,
    0x001600f6,
    (unsigned long)((char *)PPCDis_Data + 0x524),
    0x000ff3ff,
    0x00000000,
    0x000a00f7,
    (unsigned long)((char *)PPCDis_Data + 0x52c),
    0x000fffff,
    0x00000000,
    0x000f00f8,
    (unsigned long)((char *)PPCDis_Data + 0x534),
    0x00000002,
    0x00000000,
    0x00160106,
    (unsigned long)((char *)PPCDis_Data + 0x53c),
    0x00000040,
    0x00000002,
    0x00020115,
    (unsigned long)((char *)PPCDis_Data + 0x544),
    0x00000002,
    0x00000000,
    0x00160116,
    (unsigned long)((char *)PPCDis_Data + 0x54c),
    0x000ff3ff,
    0x00000000,
    0x00010117,
    (unsigned long)((char *)PPCDis_Data + 0x554),
    0x000fffff,
    0x00000000,
    0x0008011c,
    (unsigned long)((char *)PPCDis_Data + 0x55c),
    0x000fffff,
    0x00000000,
    0x00190132,
    (unsigned long)((char *)PPCDis_Data + 0x560),
    0x000ff3df,
    0x00000006,
    0x00010136,
    (unsigned long)((char *)PPCDis_Data + 0x568),
    0x000ff19b,
    0x00000004,
    0x00010137,
    (unsigned long)((char *)PPCDis_Data + 0x570),
    0x000fffff,
    0x00000000,
    0x0008013c,
    (unsigned long)((char *)PPCDis_Data + 0x578),
    0x000fffff,
    0x00000000,
    0x00010155,
    (unsigned long)((char *)PPCDis_Data + 0x57c),
    0x00002000,
    0x40000000,
    0x001c0156,
    (unsigned long)((char *)PPCDis_Data + 0x584),
    0x00090000,
    0x00000000,
    0x00010157,
    (unsigned long)((char *)PPCDis_Data + 0x588),
    0x000fffff,
    0x00000000,
    0x001f0167,
    (unsigned long)((char *)PPCDis_Data + 0x590),
    0x00090000,
    0x00000000,
    0x00170172,
    (unsigned long)((char *)PPCDis_Data + 0x598),
    0x000003c0,
    0x00000006,
    0x00010175,
    (unsigned long)((char *)PPCDis_Data + 0x5a0),
    0x00002000,
    0x40000000,
    0x001d0176,
    (unsigned long)((char *)PPCDis_Data + 0x5a8),
    0x00090000,
    0x00000000,
    0x00010177,
    (unsigned long)((char *)PPCDis_Data + 0x5b0),
    0x000fffff,
    0x00000000,
    0x000a0197,
    (unsigned long)((char *)PPCDis_Data + 0x5b8),
    0x000fffff,
    0x00000000,
    0x0008019c,
    (unsigned long)((char *)PPCDis_Data + 0x5c0),
    0x000fffff,
    0x00000000,
    0x001901b2,
    (unsigned long)((char *)PPCDis_Data + 0x5c4),
    0x00002000,
    0x40000006,
    0x000a01b6,
    (unsigned long)((char *)PPCDis_Data + 0x5cc),
    0x000ff19b,
    0x00000004,
    0x000a01b7,
    (unsigned long)((char *)PPCDis_Data + 0x5d4),
    0x000fffff,
    0x00000000,
    0x000801bc,
    (unsigned long)((char *)PPCDis_Data + 0x5dc),
    0x000fffff,
    0x00000000,
    0x001601c6,
    (unsigned long)((char *)PPCDis_Data + 0x5e0),
    0x00000040,
    0x00000002,
    0x001601d6,
    (unsigned long)((char *)PPCDis_Data + 0x5e8),
    0x000ff3ff,
    0x00000002,
    0x000801dc,
    (unsigned long)((char *)PPCDis_Data + 0x5f0),
    0x000fffff,
    0x00000000,
    0x000101e6,
    (unsigned long)((char *)PPCDis_Data + 0x5f8),
    0x00000040,
    0x00000002,
    0x001f01e7,
    (unsigned long)((char *)PPCDis_Data + 0x600),
    0x00090000,
    0x00000000,
    0x001701f2,
    (unsigned long)((char *)PPCDis_Data + 0x608),
    0x00002000,
    0x40000006,
    0x00130200,
    (unsigned long)((char *)PPCDis_Data + 0x610),
    0x000fffff,
    0x00000000,
    0x00010215,
    (unsigned long)((char *)PPCDis_Data + 0x618),
    0x000fffff,
    0x00000000,
    0x00010216,
    (unsigned long)((char *)PPCDis_Data + 0x620),
    0x000fffff,
    0x00000000,
    0x001a0217,
    (unsigned long)((char *)PPCDis_Data + 0x628),
    0x000ffc1f,
    0x00000000,
    0x00080218,
    (unsigned long)((char *)PPCDis_Data + 0x630),
    0x000fffff,
    0x00000000,
    0x00080219,
    (unsigned long)((char *)PPCDis_Data + 0x634),
    0x00000002,
    0x00000000,
    0x0008021b,
    (unsigned long)((char *)PPCDis_Data + 0x63c),
    0x00002000,
    0x40000000,
    0x0008021d,
    (unsigned long)((char *)PPCDis_Data + 0x640),
    0x00000002,
    0x00000000,
    0x00170236,
    (unsigned long)((char *)PPCDis_Data + 0x648),
    0x000ff3dd,
    0x00000002,
    0x001a0237,
    (unsigned long)((char *)PPCDis_Data + 0x650),
    0x000ffc1f,
    0x00000000,
    0x00070253,
    (unsigned long)((char *)PPCDis_Data + 0x658),
    0x000ff01f,
    0x00000002,
    0x00200254,
    (unsigned long)((char *)PPCDis_Data + 0x660),
    0x00000004,
    0x00000000,
    0x001b0255,
    (unsigned long)((char *)PPCDis_Data + 0x664),
    0x000fffff,
    0x00000000,
    0x00170256,
    (unsigned long)((char *)PPCDis_Data + 0x66c),
    0x000fffff,
    0x00000000,
    0x001a0257,
    (unsigned long)((char *)PPCDis_Data + 0x674),
    0x000ffc1b,
    0x00000000,
    0x00200274,
    (unsigned long)((char *)PPCDis_Data + 0x67c),
    0x00000004,
    0x00000000,
    0x001a0277,
    (unsigned long)((char *)PPCDis_Data + 0x680),
    0x000ffc1b,
    0x00000000,
    0x00030293,
    (unsigned long)((char *)PPCDis_Data + 0x688),
    0x000ff01f,
    0x00000002,
    0x000a0295,
    (unsigned long)((char *)PPCDis_Data + 0x690),
    0x000fffff,
    0x00000000,
    0x000a0296,
    (unsigned long)((char *)PPCDis_Data + 0x698),
    0x000fffff,
    0x00000000,
    0x001a0297,
    (unsigned long)((char *)PPCDis_Data + 0x6a0),
    0x000ffc1f,
    0x00000000,
    0x00080298,
    (unsigned long)((char *)PPCDis_Data + 0x6a8),
    0x00000002,
    0x00000000,
    0x00080299,
    (unsigned long)((char *)PPCDis_Data + 0x6ac),
    0x00000002,
    0x00000000,
    0x001a02b7,
    (unsigned long)((char *)PPCDis_Data + 0x6b0),
    0x000ffc1f,
    0x00000000,
    0x000f02b8,
    (unsigned long)((char *)PPCDis_Data + 0x6b8),
    0x00000002,
    0x00000000,
    0x001b02d5,
    (unsigned long)((char *)PPCDis_Data + 0x6c0),
    0x000fffff,
    0x00000000,
    0x001a02d7,
    (unsigned long)((char *)PPCDis_Data + 0x6c8),
    0x000ffc1b,
    0x00000000,
    0x000802d8,
    (unsigned long)((char *)PPCDis_Data + 0x6d0),
    0x00000002,
    0x00000000,
    0x000802d9,
    (unsigned long)((char *)PPCDis_Data + 0x6d8),
    0x00000002,
    0x00000000,
    0x001a02f7,
    (unsigned long)((char *)PPCDis_Data + 0x6e0),
    0x000ffc1b,
    0x00000000,
    0x000f02f8,
    (unsigned long)((char *)PPCDis_Data + 0x6e8),
    0x00000002,
    0x00000000,
    0x00010316,
    (unsigned long)((char *)PPCDis_Data + 0x6f0),
    0x000fffff,
    0x00000000,
    0x00080318,
    (unsigned long)((char *)PPCDis_Data + 0x6f8),
    0x000fffff,
    0x00000000,
    0x0008031a,
    (unsigned long)((char *)PPCDis_Data + 0x700),
    0x000fffff,
    0x40000000,
    0x001e0336,
    (unsigned long)((char *)PPCDis_Data + 0x708),
    0x00090000,
    0x00000000,
    0x000f0338,
    (unsigned long)((char *)PPCDis_Data + 0x70c),
    0x000fffff,
    0x00000000,
    0x00170356,
    (unsigned long)((char *)PPCDis_Data + 0x714),
    0x000fffff,
    0x00000000,
    0x00010392,
    (unsigned long)((char *)PPCDis_Data + 0x71c),
    0x00000040,
    0x00000000,
    0x000a0396,
    (unsigned long)((char *)PPCDis_Data + 0x724),
    0x000fffff,
    0x00000000,
    0x00080398,
    (unsigned long)((char *)PPCDis_Data + 0x72c),
    0x00000002,
    0x00000000,
    0x00080399,
    (unsigned long)((char *)PPCDis_Data + 0x734),
    0x00000002,
    0x00000000,
    0x000b039a,
    (unsigned long)((char *)PPCDis_Data + 0x73c),
    0x000fffff,
    0x00000000,
    0x001b03b2,
    (unsigned long)((char *)PPCDis_Data + 0x744),
    0x00000040,
    0x00000000,
    0x000f03b8,
    (unsigned long)((char *)PPCDis_Data + 0x74c),
    0x00000002,
    0x00000000,
    0x000b03ba,
    (unsigned long)((char *)PPCDis_Data + 0x754),
    0x000fffff,
    0x00000000,
    0x001603c6,
    (unsigned long)((char *)PPCDis_Data + 0x75c),
    0x00000040,
    0x00000002,
    0x001903d2,
    (unsigned long)((char *)PPCDis_Data + 0x764),
    0x0000004c,
    0x00000000,
    0x001603d6,
    (unsigned long)((char *)PPCDis_Data + 0x76c),
    0x000fffff,
    0x00000000,
    0x001a03d7,
    (unsigned long)((char *)PPCDis_Data + 0x774),
    0x000ffc19,
    0x00000004,
    0x000b03da,
    (unsigned long)((char *)PPCDis_Data + 0x77c),
    0x00002000,
    0x40000000,
    0x001603e6,
    (unsigned long)((char *)PPCDis_Data + 0x784),
    0x00000040,
    0x00000002,
    0x001903f2,
    (unsigned long)((char *)PPCDis_Data + 0x78c),
    0x0000000c,
    0x00000000,
    0x001603f6,
    (unsigned long)((char *)PPCDis_Data + 0x794),
    0x000ff3ff,
    0x00000000,
    /* 0x0048efec 8 bytes, "fcmpu" */
    0x706d6366,
    0x00000075,
    /* 0x0048eff4 8 bytes, "frsp" */
    0x70737266,
    0x00000000,
    /* 0x0048effc 8 bytes, "fctiw" */
    0x69746366,
    0x00000077,
    /* 0x0048f004 8 bytes, "fctiwz" */
    0x69746366,
    0x00007a77,
    /* 0x0048f00c 8 bytes, "fcmpo" */
    0x706d6366,
    0x0000006f,
    /* 0x0048f014 8 bytes, "mtfsb1" */
    0x7366746d,
    0x00003162,
    /* 0x0048f01c 8 bytes, "fneg" */
    0x67656e66,
    0x00000000,
    /* 0x0048f024 8 bytes, "mcrfs" */
    0x6672636d,
    0x00000073,
    /* 0x0048f02c 8 bytes, "mtfsb0" */
    0x7366746d,
    0x00003062,
    /* 0x0048f034 4 bytes, "fmr" */
    0x00726d66,
    /* 0x0048f038 8 bytes, "mtfsfi" */
    0x7366746d,
    0x00006966,
    /* 0x0048f040 8 bytes, "fnabs" */
    0x62616e66,
    0x00000073,
    /* 0x0048f048 8 bytes, "fabs" */
    0x73626166,
    0x00000000,
    /* 0x0048f050 8 bytes, "mffs" */
    0x7366666d,
    0x00000000,
    /* 0x0048f058 8 bytes, "fctid" */
    0x69746366,
    0x00000064,
    /* 0x0048f060 8 bytes, "fctidz" */
    0x69746366,
    0x00007a64,
    /* 0x0048f068 8 bytes, "fcfid" */
    0x69666366,
    0x00000064,
    /* 0x0048f070 272 bytes, 17 pointers: data_0048efec, data_0048eff4, data_0048effc, data_0048f004 ... */
    0x00110000,
    (unsigned long)((char *)PPCDis_Data + 0x101c),
    0x000ffc1f,
    0x00000000,
    0x0004000c,
    (unsigned long)((char *)PPCDis_Data + 0x1024),
    0x000ffc1f,
    0x00000000,
    0x0004000e,
    (unsigned long)((char *)PPCDis_Data + 0x102c),
    0x000ffc1f,
    0x00000000,
    0x0004000f,
    (unsigned long)((char *)PPCDis_Data + 0x1034),
    0x000ffc1f,
    0x00000000,
    0x00110020,
    (unsigned long)((char *)PPCDis_Data + 0x103c),
    0x000ffc1f,
    0x00000000,
    0x00180026,
    (unsigned long)((char *)PPCDis_Data + 0x1044),
    0x000ffc1f,
    0x00000000,
    0x00040028,
    (unsigned long)((char *)PPCDis_Data + 0x104c),
    0x000ffc1f,
    0x00000000,
    0x00120040,
    (unsigned long)((char *)PPCDis_Data + 0x1054),
    0x000ffc1f,
    0x00000000,
    0x00180046,
    (unsigned long)((char *)PPCDis_Data + 0x105c),
    0x000ffc1f,
    0x00000000,
    0x00040048,
    (unsigned long)((char *)PPCDis_Data + 0x1064),
    0x000ffc1f,
    0x00000000,
    0x00140086,
    (unsigned long)((char *)PPCDis_Data + 0x1068),
    0x000ffc1f,
    0x00000000,
    0x00040088,
    (unsigned long)((char *)PPCDis_Data + 0x1070),
    0x000ffc1f,
    0x00000000,
    0x00040108,
    (unsigned long)((char *)PPCDis_Data + 0x1078),
    0x000ffc1f,
    0x00000000,
    0x00060247,
    (unsigned long)((char *)PPCDis_Data + 0x1080),
    0x000ffc1f,
    0x00000000,
    0x0004032e,
    (unsigned long)((char *)PPCDis_Data + 0x1088),
    0x00002000,
    0x40000000,
    0x0004032f,
    (unsigned long)((char *)PPCDis_Data + 0x1090),
    0x00002000,
    0x40000000,
    0x0004034e,
    (unsigned long)((char *)PPCDis_Data + 0x1098),
    0x00002000,
    0x40000000,
    /* 0x0048f180 8 bytes, "vaddubm" */
    0x64646176,
    0x006d6275,
    /* 0x0048f188 8 bytes, "vmaxub" */
    0x78616d76,
    0x00006275,
    /* 0x0048f190 8 bytes, "vrlb" */
    0x626c7276,
    0x00000000,
    /* 0x0048f198 8 bytes, "vmuloub" */
    0x6c756d76,
    0x0062756f,
    /* 0x0048f1a0 8 bytes, "vaddfp" */
    0x64646176,
    0x00007066,
    /* 0x0048f1a8 8 bytes, "vmrghb" */
    0x67726d76,
    0x00006268,
    /* 0x0048f1b0 8 bytes, "vpkuhum" */
    0x756b7076,
    0x006d7568,
    /* 0x0048f1b8 8 bytes, "vadduhm" */
    0x64646176,
    0x006d6875,
    /* 0x0048f1c0 8 bytes, "vmaxuh" */
    0x78616d76,
    0x00006875,
    /* 0x0048f1c8 8 bytes, "vrlh" */
    0x686c7276,
    0x00000000,
    /* 0x0048f1d0 8 bytes, "vmulouh" */
    0x6c756d76,
    0x0068756f,
    /* 0x0048f1d8 8 bytes, "vsubfp" */
    0x62757376,
    0x00007066,
    /* 0x0048f1e0 8 bytes, "vmrghh" */
    0x67726d76,
    0x00006868,
    /* 0x0048f1e8 8 bytes, "vpkuwum" */
    0x756b7076,
    0x006d7577,
    /* 0x0048f1f0 8 bytes, "vadduwm" */
    0x64646176,
    0x006d7775,
    /* 0x0048f1f8 8 bytes, "vmaxuw" */
    0x78616d76,
    0x00007775,
    /* 0x0048f200 8 bytes, "vrlw" */
    0x776c7276,
    0x00000000,
    /* 0x0048f208 8 bytes, "vmrghw" */
    0x67726d76,
    0x00007768,
    /* 0x0048f210 8 bytes, "vpkuhus" */
    0x756b7076,
    0x00737568,
    /* 0x0048f218 8 bytes, "vpkuwus" */
    0x756b7076,
    0x00737577,
    /* 0x0048f220 8 bytes, "vmaxsb" */
    0x78616d76,
    0x00006273,
    /* 0x0048f228 8 bytes, "vslb" */
    0x626c7376,
    0x00000000,
    /* 0x0048f230 8 bytes, "vmulosb" */
    0x6c756d76,
    0x0062736f,
    /* 0x0048f238 8 bytes, "vrefp" */
    0x66657276,
    0x00000070,
    /* 0x0048f240 8 bytes, "vmrglb" */
    0x67726d76,
    0x0000626c,
    /* 0x0048f248 8 bytes, "vpkshus" */
    0x736b7076,
    0x00737568,
    /* 0x0048f250 8 bytes, "vmaxsh" */
    0x78616d76,
    0x00006873,
    /* 0x0048f258 8 bytes, "vslh" */
    0x686c7376,
    0x00000000,
    /* 0x0048f260 8 bytes, "vmulosh" */
    0x6c756d76,
    0x0068736f,
    /* 0x0048f268 12 bytes, "vrsqrtefp" */
    0x71737276,
    0x66657472,
    0x00000070,
    /* 0x0048f274 8 bytes, "vmrglh" */
    0x67726d76,
    0x0000686c,
    /* 0x0048f27c 8 bytes, "vpkswus" */
    0x736b7076,
    0x00737577,
    /* 0x0048f284 8 bytes, "vaddcuw" */
    0x64646176,
    0x00777563,
    /* 0x0048f28c 8 bytes, "vmaxsw" */
    0x78616d76,
    0x00007773,
    /* 0x0048f294 8 bytes, "vslw" */
    0x776c7376,
    0x00000000,
    /* 0x0048f29c 12 bytes, "vexptefp" */
    0x70786576,
    0x70666574,
    0x00000000,
    /* 0x0048f2a8 8 bytes, "vmrglw" */
    0x67726d76,
    0x0000776c,
    /* 0x0048f2b0 8 bytes, "vpkshss" */
    0x736b7076,
    0x00737368,
    /* 0x0048f2b8 4 bytes, "vsl" */
    0x006c7376,
    /* 0x0048f2bc 8 bytes, "vlogefp" */
    0x676f6c76,
    0x00706665,
    /* 0x0048f2c4 8 bytes, "vpkswss" */
    0x736b7076,
    0x00737377,
    /* 0x0048f2cc 8 bytes, "vaddubs" */
    0x64646176,
    0x00736275,
    /* 0x0048f2d4 8 bytes, "vminub" */
    0x6e696d76,
    0x00006275,
    /* 0x0048f2dc 8 bytes, "vsrb" */
    0x62727376,
    0x00000000,
    /* 0x0048f2e4 8 bytes, "vmuleub" */
    0x6c756d76,
    0x00627565,
    /* 0x0048f2ec 8 bytes, "vrfin" */
    0x69667276,
    0x0000006e,
    /* 0x0048f2f4 8 bytes, "vspltb" */
    0x6c707376,
    0x00006274,
    /* 0x0048f2fc 8 bytes, "vupkhsb" */
    0x6b707576,
    0x00627368,
    /* 0x0048f304 8 bytes, "vadduhs" */
    0x64646176,
    0x00736875,
    /* 0x0048f30c 8 bytes, "vminuh" */
    0x6e696d76,
    0x00006875,
    /* 0x0048f314 8 bytes, "vsrh" */
    0x68727376,
    0x00000000,
    /* 0x0048f31c 8 bytes, "vmuleuh" */
    0x6c756d76,
    0x00687565,
    /* 0x0048f324 8 bytes, "vrfiz" */
    0x69667276,
    0x0000007a,
    /* 0x0048f32c 8 bytes, "vsplth" */
    0x6c707376,
    0x00006874,
    /* 0x0048f334 8 bytes, "vupkhsh" */
    0x6b707576,
    0x00687368,
    /* 0x0048f33c 8 bytes, "vadduws" */
    0x64646176,
    0x00737775,
    /* 0x0048f344 8 bytes, "vminuw" */
    0x6e696d76,
    0x00007775,
    /* 0x0048f34c 8 bytes, "vsrw" */
    0x77727376,
    0x00000000,
    /* 0x0048f354 8 bytes, "vrfip" */
    0x69667276,
    0x00000070,
    /* 0x0048f35c 8 bytes, "vspltw" */
    0x6c707376,
    0x00007774,
    /* 0x0048f364 8 bytes, "vupklsb" */
    0x6b707576,
    0x0062736c,
    /* 0x0048f36c 4 bytes, "vsr" */
    0x00727376,
    /* 0x0048f370 8 bytes, "vrfim" */
    0x69667276,
    0x0000006d,
    /* 0x0048f378 8 bytes, "vupklsh" */
    0x6b707576,
    0x0068736c,
    /* 0x0048f380 8 bytes, "vaddsbs" */
    0x64646176,
    0x00736273,
    /* 0x0048f388 8 bytes, "vminsb" */
    0x6e696d76,
    0x00006273,
    /* 0x0048f390 8 bytes, "vsrab" */
    0x61727376,
    0x00000062,
    /* 0x0048f398 8 bytes, "vmulesb" */
    0x6c756d76,
    0x00627365,
    /* 0x0048f3a0 8 bytes, "vcfux" */
    0x75666376,
    0x00000078,
    /* 0x0048f3a8 12 bytes, "vspltisb" */
    0x6c707376,
    0x62736974,
    0x00000000,
    /* 0x0048f3b4 8 bytes, "vpkpx" */
    0x706b7076,
    0x00000078,
    /* 0x0048f3bc 8 bytes, "vaddshs" */
    0x64646176,
    0x00736873,
    /* 0x0048f3c4 8 bytes, "vminsh" */
    0x6e696d76,
    0x00006873,
    /* 0x0048f3cc 8 bytes, "vsrah" */
    0x61727376,
    0x00000068,
    /* 0x0048f3d4 8 bytes, "vmulesh" */
    0x6c756d76,
    0x00687365,
    /* 0x0048f3dc 8 bytes, "vcfsx" */
    0x73666376,
    0x00000078,
    /* 0x0048f3e4 12 bytes, "vspltish" */
    0x6c707376,
    0x68736974,
    0x00000000,
    /* 0x0048f3f0 8 bytes, "vupkhpx" */
    0x6b707576,
    0x00787068,
    /* 0x0048f3f8 8 bytes, "vaddsws" */
    0x64646176,
    0x00737773,
    /* 0x0048f400 8 bytes, "vminsw" */
    0x6e696d76,
    0x00007773,
    /* 0x0048f408 8 bytes, "vsraw" */
    0x61727376,
    0x00000077,
    /* 0x0048f410 8 bytes, "vctuxs" */
    0x75746376,
    0x00007378,
    /* 0x0048f418 12 bytes, "vspltisw" */
    0x6c707376,
    0x77736974,
    0x00000000,
    /* 0x0048f424 8 bytes, "vctsxs" */
    0x73746376,
    0x00007378,
    /* 0x0048f42c 8 bytes, "vupklpx" */
    0x6b707576,
    0x0078706c,
    /* 0x0048f434 8 bytes, "vsububm" */
    0x62757376,
    0x006d6275,
    /* 0x0048f43c 8 bytes, "vavgub" */
    0x67766176,
    0x00006275,
    /* 0x0048f444 8 bytes, "vand" */
    0x646e6176,
    0x00000000,
    /* 0x0048f44c 8 bytes, "vmaxfp" */
    0x78616d76,
    0x00007066,
    /* 0x0048f454 8 bytes, "vslo" */
    0x6f6c7376,
    0x00000000,
    /* 0x0048f45c 8 bytes, "vsubuhm" */
    0x62757376,
    0x006d6875,
    /* 0x0048f464 8 bytes, "vavguh" */
    0x67766176,
    0x00006875,
    /* 0x0048f46c 8 bytes, "vandc" */
    0x646e6176,
    0x00000063,
    /* 0x0048f474 8 bytes, "vminfp" */
    0x6e696d76,
    0x00007066,
    /* 0x0048f47c 8 bytes, "vsro" */
    0x6f727376,
    0x00000000,
    /* 0x0048f484 8 bytes, "vsubuwm" */
    0x62757376,
    0x006d7775,
    /* 0x0048f48c 8 bytes, "vavguw" */
    0x67766176,
    0x00007775,
    /* 0x0048f494 4 bytes, "vor" */
    0x00726f76,
    /* 0x0048f498 8 bytes, "vxor" */
    0x726f7876,
    0x00000000,
    /* 0x0048f4a0 8 bytes, "vavgsb" */
    0x67766176,
    0x00006273,
    /* 0x0048f4a8 8 bytes, "vnor" */
    0x726f6e76,
    0x00000000,
    /* 0x0048f4b0 8 bytes, "vavgsh" */
    0x67766176,
    0x00006873,
    /* 0x0048f4b8 8 bytes, "vsubcuw" */
    0x62757376,
    0x00777563,
    /* 0x0048f4c0 8 bytes, "vavgsw" */
    0x67766176,
    0x00007773,
    /* 0x0048f4c8 8 bytes, "vsububs" */
    0x62757376,
    0x00736275,
    /* 0x0048f4d0 8 bytes, "mfvscr" */
    0x7376666d,
    0x00007263,
    /* 0x0048f4d8 12 bytes, "vsum4ubs" */
    0x6d757376,
    0x73627534,
    0x00000000,
    /* 0x0048f4e4 8 bytes, "vsubuhs" */
    0x62757376,
    0x00736875,
    /* 0x0048f4ec 8 bytes, "mtvscr" */
    0x7376746d,
    0x00007263,
    /* 0x0048f4f4 12 bytes, "vsum4shs" */
    0x6d757376,
    0x73687334,
    0x00000000,
    /* 0x0048f500 8 bytes, "vsubuws" */
    0x62757376,
    0x00737775,
    /* 0x0048f508 12 bytes, "vsum2sws" */
    0x6d757376,
    0x73777332,
    0x00000000,
    /* 0x0048f514 8 bytes, "vsubsbs" */
    0x62757376,
    0x00736273,
    /* 0x0048f51c 12 bytes, "vsum4sbs" */
    0x6d757376,
    0x73627334,
    0x00000000,
    /* 0x0048f528 8 bytes, "vsubshs" */
    0x62757376,
    0x00736873,
    /* 0x0048f530 8 bytes, "vsubsws" */
    0x62757376,
    0x00737773,
    /* 0x0048f538 1880 bytes, 117 pointers: data_0048f180, data_0048f188, data_0048f190, data_0048f198 ... */
    0x6d757376,
    0x00737773,
    0x00000000,
    (unsigned long)((char *)PPCDis_Data + 0x11b0),
    0x00090000,
    0x00000000,
    0x00000002,
    (unsigned long)((char *)PPCDis_Data + 0x11b8),
    0x00090000,
    0x00000000,
    0x00000004,
    (unsigned long)((char *)PPCDis_Data + 0x11c0),
    0x00090000,
    0x00000000,
    0x00000008,
    (unsigned long)((char *)PPCDis_Data + 0x11c8),
    0x00090000,
    0x00000000,
    0x0000000a,
    (unsigned long)((char *)PPCDis_Data + 0x11d0),
    0x00090000,
    0x00000000,
    0x0000000c,
    (unsigned long)((char *)PPCDis_Data + 0x11d8),
    0x00090000,
    0x00000000,
    0x0000000e,
    (unsigned long)((char *)PPCDis_Data + 0x11e0),
    0x00090000,
    0x00000000,
    0x00000040,
    (unsigned long)((char *)PPCDis_Data + 0x11e8),
    0x00090000,
    0x00000000,
    0x00000042,
    (unsigned long)((char *)PPCDis_Data + 0x11f0),
    0x00090000,
    0x00000000,
    0x00000044,
    (unsigned long)((char *)PPCDis_Data + 0x11f8),
    0x00090000,
    0x00000000,
    0x00000048,
    (unsigned long)((char *)PPCDis_Data + 0x1200),
    0x00090000,
    0x00000000,
    0x0000004a,
    (unsigned long)((char *)PPCDis_Data + 0x1208),
    0x00090000,
    0x00000000,
    0x0000004c,
    (unsigned long)((char *)PPCDis_Data + 0x1210),
    0x00090000,
    0x00000000,
    0x0000004e,
    (unsigned long)((char *)PPCDis_Data + 0x1218),
    0x00090000,
    0x00000000,
    0x00000080,
    (unsigned long)((char *)PPCDis_Data + 0x1220),
    0x00090000,
    0x00000000,
    0x00000082,
    (unsigned long)((char *)PPCDis_Data + 0x1228),
    0x00090000,
    0x00000000,
    0x00000084,
    (unsigned long)((char *)PPCDis_Data + 0x1230),
    0x00090000,
    0x00000000,
    0x0000008c,
    (unsigned long)((char *)PPCDis_Data + 0x1238),
    0x00090000,
    0x00000000,
    0x0000008e,
    (unsigned long)((char *)PPCDis_Data + 0x1240),
    0x00090000,
    0x00000000,
    0x000000ce,
    (unsigned long)((char *)PPCDis_Data + 0x1248),
    0x00090000,
    0x00000000,
    0x00000102,
    (unsigned long)((char *)PPCDis_Data + 0x1250),
    0x00090000,
    0x00000000,
    0x00000104,
    (unsigned long)((char *)PPCDis_Data + 0x1258),
    0x00090000,
    0x00000000,
    0x00000108,
    (unsigned long)((char *)PPCDis_Data + 0x1260),
    0x00090000,
    0x00000000,
    0x0001010a,
    (unsigned long)((char *)PPCDis_Data + 0x1268),
    0x00090000,
    0x00000000,
    0x0000010c,
    (unsigned long)((char *)PPCDis_Data + 0x1270),
    0x00090000,
    0x00000000,
    0x0000010e,
    (unsigned long)((char *)PPCDis_Data + 0x1278),
    0x00090000,
    0x00000000,
    0x00000142,
    (unsigned long)((char *)PPCDis_Data + 0x1280),
    0x00090000,
    0x00000000,
    0x00000144,
    (unsigned long)((char *)PPCDis_Data + 0x1288),
    0x00090000,
    0x00000000,
    0x00000148,
    (unsigned long)((char *)PPCDis_Data + 0x1290),
    0x00090000,
    0x00000000,
    0x0001014a,
    (unsigned long)((char *)PPCDis_Data + 0x1298),
    0x00090000,
    0x00000000,
    0x0000014c,
    (unsigned long)((char *)PPCDis_Data + 0x12a4),
    0x00090000,
    0x00000000,
    0x0000014e,
    (unsigned long)((char *)PPCDis_Data + 0x12ac),
    0x00090000,
    0x00000000,
    0x00000180,
    (unsigned long)((char *)PPCDis_Data + 0x12b4),
    0x00090000,
    0x00000000,
    0x00000182,
    (unsigned long)((char *)PPCDis_Data + 0x12bc),
    0x00090000,
    0x00000000,
    0x00000184,
    (unsigned long)((char *)PPCDis_Data + 0x12c4),
    0x00090000,
    0x00000000,
    0x0001018a,
    (unsigned long)((char *)PPCDis_Data + 0x12cc),
    0x00090000,
    0x00000000,
    0x0000018c,
    (unsigned long)((char *)PPCDis_Data + 0x12d8),
    0x00090000,
    0x00000000,
    0x0000018e,
    (unsigned long)((char *)PPCDis_Data + 0x12e0),
    0x00090000,
    0x00000000,
    0x000001c4,
    (unsigned long)((char *)PPCDis_Data + 0x12e8),
    0x00090000,
    0x00000000,
    0x000101ca,
    (unsigned long)((char *)PPCDis_Data + 0x12ec),
    0x00090000,
    0x00000000,
    0x000001ce,
    (unsigned long)((char *)PPCDis_Data + 0x12f4),
    0x00090000,
    0x00000000,
    0x00000200,
    (unsigned long)((char *)PPCDis_Data + 0x12fc),
    0x00090000,
    0x00000000,
    0x00000202,
    (unsigned long)((char *)PPCDis_Data + 0x1304),
    0x00090000,
    0x00000000,
    0x00000204,
    (unsigned long)((char *)PPCDis_Data + 0x130c),
    0x00090000,
    0x00000000,
    0x00000208,
    (unsigned long)((char *)PPCDis_Data + 0x1314),
    0x00090000,
    0x00000000,
    0x0001020a,
    (unsigned long)((char *)PPCDis_Data + 0x131c),
    0x00090000,
    0x00000000,
    0x0002020c,
    (unsigned long)((char *)PPCDis_Data + 0x1324),
    0x00090000,
    0x00000000,
    0x0001020e,
    (unsigned long)((char *)PPCDis_Data + 0x132c),
    0x00090000,
    0x00000000,
    0x00000240,
    (unsigned long)((char *)PPCDis_Data + 0x1334),
    0x00090000,
    0x00000000,
    0x00000242,
    (unsigned long)((char *)PPCDis_Data + 0x133c),
    0x00090000,
    0x00000000,
    0x00000244,
    (unsigned long)((char *)PPCDis_Data + 0x1344),
    0x00090000,
    0x00000000,
    0x00000248,
    (unsigned long)((char *)PPCDis_Data + 0x134c),
    0x00090000,
    0x00000000,
    0x0001024a,
    (unsigned long)((char *)PPCDis_Data + 0x1354),
    0x00090000,
    0x00000000,
    0x0002024c,
    (unsigned long)((char *)PPCDis_Data + 0x135c),
    0x00090000,
    0x00000000,
    0x0001024e,
    (unsigned long)((char *)PPCDis_Data + 0x1364),
    0x00090000,
    0x00000000,
    0x00000280,
    (unsigned long)((char *)PPCDis_Data + 0x136c),
    0x00090000,
    0x00000000,
    0x00000282,
    (unsigned long)((char *)PPCDis_Data + 0x1374),
    0x00090000,
    0x00000000,
    0x00000284,
    (unsigned long)((char *)PPCDis_Data + 0x137c),
    0x00090000,
    0x00000000,
    0x0001028a,
    (unsigned long)((char *)PPCDis_Data + 0x1384),
    0x00090000,
    0x00000000,
    0x0002028c,
    (unsigned long)((char *)PPCDis_Data + 0x138c),
    0x00090000,
    0x00000000,
    0x0001028e,
    (unsigned long)((char *)PPCDis_Data + 0x1394),
    0x00090000,
    0x00000000,
    0x000002c4,
    (unsigned long)((char *)PPCDis_Data + 0x139c),
    0x00090000,
    0x00000000,
    0x000102ca,
    (unsigned long)((char *)PPCDis_Data + 0x13a0),
    0x00090000,
    0x00000000,
    0x000102ce,
    (unsigned long)((char *)PPCDis_Data + 0x13a8),
    0x00090000,
    0x00000000,
    0x00000300,
    (unsigned long)((char *)PPCDis_Data + 0x13b0),
    0x00090000,
    0x00000000,
    0x00000302,
    (unsigned long)((char *)PPCDis_Data + 0x13b8),
    0x00090000,
    0x00000000,
    0x00000304,
    (unsigned long)((char *)PPCDis_Data + 0x13c0),
    0x00090000,
    0x00000000,
    0x00000308,
    (unsigned long)((char *)PPCDis_Data + 0x13c8),
    0x00090000,
    0x00000000,
    0x0002030a,
    (unsigned long)((char *)PPCDis_Data + 0x13d0),
    0x00090000,
    0x00000000,
    0x0003030c,
    (unsigned long)((char *)PPCDis_Data + 0x13d8),
    0x00090000,
    0x00000000,
    0x0000030e,
    (unsigned long)((char *)PPCDis_Data + 0x13e4),
    0x00090000,
    0x00000000,
    0x00000340,
    (unsigned long)((char *)PPCDis_Data + 0x13ec),
    0x00090000,
    0x00000000,
    0x00000342,
    (unsigned long)((char *)PPCDis_Data + 0x13f4),
    0x00090000,
    0x00000000,
    0x00000344,
    (unsigned long)((char *)PPCDis_Data + 0x13fc),
    0x00090000,
    0x00000000,
    0x00000348,
    (unsigned long)((char *)PPCDis_Data + 0x1404),
    0x00090000,
    0x00000000,
    0x0002034a,
    (unsigned long)((char *)PPCDis_Data + 0x140c),
    0x00090000,
    0x00000000,
    0x0003034c,
    (unsigned long)((char *)PPCDis_Data + 0x1414),
    0x00090000,
    0x00000000,
    0x0001034e,
    (unsigned long)((char *)PPCDis_Data + 0x1420),
    0x00090000,
    0x00000000,
    0x00000380,
    (unsigned long)((char *)PPCDis_Data + 0x1428),
    0x00090000,
    0x00000000,
    0x00000382,
    (unsigned long)((char *)PPCDis_Data + 0x1430),
    0x00090000,
    0x00000000,
    0x00000384,
    (unsigned long)((char *)PPCDis_Data + 0x1438),
    0x00090000,
    0x00000000,
    0x0002038a,
    (unsigned long)((char *)PPCDis_Data + 0x1440),
    0x00090000,
    0x00000000,
    0x0009038c,
    (unsigned long)((char *)PPCDis_Data + 0x1448),
    0x00090000,
    0x00000000,
    0x000203ca,
    (unsigned long)((char *)PPCDis_Data + 0x1454),
    0x00090000,
    0x00000000,
    0x000103ce,
    (unsigned long)((char *)PPCDis_Data + 0x145c),
    0x00090000,
    0x00000000,
    0x00000400,
    (unsigned long)((char *)PPCDis_Data + 0x1464),
    0x00090000,
    0x00000000,
    0x00000402,
    (unsigned long)((char *)PPCDis_Data + 0x146c),
    0x00090000,
    0x00000000,
    0x00000404,
    (unsigned long)((char *)PPCDis_Data + 0x1474),
    0x00090000,
    0x00000000,
    0x0000040a,
    (unsigned long)((char *)PPCDis_Data + 0x147c),
    0x00090000,
    0x00000000,
    0x0000040c,
    (unsigned long)((char *)PPCDis_Data + 0x1484),
    0x00090000,
    0x00000000,
    0x00000440,
    (unsigned long)((char *)PPCDis_Data + 0x148c),
    0x00090000,
    0x00000000,
    0x00000442,
    (unsigned long)((char *)PPCDis_Data + 0x1494),
    0x00090000,
    0x00000000,
    0x00000444,
    (unsigned long)((char *)PPCDis_Data + 0x149c),
    0x00090000,
    0x00000000,
    0x0000044a,
    (unsigned long)((char *)PPCDis_Data + 0x14a4),
    0x00090000,
    0x00000000,
    0x0000044c,
    (unsigned long)((char *)PPCDis_Data + 0x14ac),
    0x00090000,
    0x00000000,
    0x00000480,
    (unsigned long)((char *)PPCDis_Data + 0x14b4),
    0x00090000,
    0x00000000,
    0x00000482,
    (unsigned long)((char *)PPCDis_Data + 0x14bc),
    0x00090000,
    0x00000000,
    0x000a0484,
    (unsigned long)((char *)PPCDis_Data + 0x14c4),
    0x00090000,
    0x00000000,
    0x000b04c4,
    (unsigned long)((char *)PPCDis_Data + 0x14c8),
    0x00090000,
    0x00000000,
    0x00000502,
    (unsigned long)((char *)PPCDis_Data + 0x14d0),
    0x00090000,
    0x00000000,
    0x000d0504,
    (unsigned long)((char *)PPCDis_Data + 0x14d8),
    0x00090000,
    0x00000000,
    0x00000542,
    (unsigned long)((char *)PPCDis_Data + 0x14e0),
    0x00090000,
    0x00000000,
    0x00000580,
    (unsigned long)((char *)PPCDis_Data + 0x14e8),
    0x00090000,
    0x00000000,
    0x00000582,
    (unsigned long)((char *)PPCDis_Data + 0x14f0),
    0x00090000,
    0x00000000,
    0x00000600,
    (unsigned long)((char *)PPCDis_Data + 0x14f8),
    0x00090000,
    0x00000000,
    0x00040604,
    (unsigned long)((char *)PPCDis_Data + 0x1500),
    0x00090000,
    0x00000000,
    0x00000608,
    (unsigned long)((char *)PPCDis_Data + 0x1508),
    0x00090000,
    0x00000000,
    0x00000640,
    (unsigned long)((char *)PPCDis_Data + 0x1514),
    0x00090000,
    0x00000000,
    0x00050644,
    (unsigned long)((char *)PPCDis_Data + 0x151c),
    0x00090000,
    0x00000000,
    0x00000648,
    (unsigned long)((char *)PPCDis_Data + 0x1524),
    0x00090000,
    0x00000000,
    0x00000680,
    (unsigned long)((char *)PPCDis_Data + 0x1530),
    0x00090000,
    0x00000000,
    0x00000688,
    (unsigned long)((char *)PPCDis_Data + 0x1538),
    0x00090000,
    0x00000000,
    0x00000700,
    (unsigned long)((char *)PPCDis_Data + 0x1544),
    0x00090000,
    0x00000000,
    0x00000708,
    (unsigned long)((char *)PPCDis_Data + 0x154c),
    0x00090000,
    0x00000000,
    0x00000740,
    (unsigned long)((char *)PPCDis_Data + 0x1558),
    0x00090000,
    0x00000000,
    0x00000780,
    (unsigned long)((char *)PPCDis_Data + 0x1560),
    0x00090000,
    0x00000000,
    0x00000788,
    (unsigned long)((char *)PPCDis_Data + 0x1568),
    0x00090000,
    0x00000000,
    /* 0x0048fc90 12 bytes, "vcmpequb" */
    0x706d6376,
    0x62757165,
    0x00000000,
    /* 0x0048fc9c 12 bytes, "vcmpequh" */
    0x706d6376,
    0x68757165,
    0x00000000,
    /* 0x0048fca8 12 bytes, "vcmpequw" */
    0x706d6376,
    0x77757165,
    0x00000000,
    /* 0x0048fcb4 12 bytes, "vcmpeqfp" */
    0x706d6376,
    0x70667165,
    0x00000000,
    /* 0x0048fcc0 12 bytes, "vcmpgefp" */
    0x706d6376,
    0x70666567,
    0x00000000,
    /* 0x0048fccc 12 bytes, "vcmpgtub" */
    0x706d6376,
    0x62757467,
    0x00000000,
    /* 0x0048fcd8 12 bytes, "vcmpgtuh" */
    0x706d6376,
    0x68757467,
    0x00000000,
    /* 0x0048fce4 12 bytes, "vcmpgtuw" */
    0x706d6376,
    0x77757467,
    0x00000000,
    /* 0x0048fcf0 12 bytes, "vcmpgtfp" */
    0x706d6376,
    0x70667467,
    0x00000000,
    /* 0x0048fcfc 12 bytes, "vcmpgtsb" */
    0x706d6376,
    0x62737467,
    0x00000000,
    /* 0x0048fd08 12 bytes, "vcmpgtsh" */
    0x706d6376,
    0x68737467,
    0x00000000,
    /* 0x0048fd14 12 bytes, "vcmpgtsw" */
    0x706d6376,
    0x77737467,
    0x00000000,
    /* 0x0048fd20 216 bytes, 13 pointers: data_0048fc90, data_0048fc9c, data_0048fca8, data_0048fcb4 ... */
    0x706d6376,
    0x00706662,
    0x00000006,
    (unsigned long)((char *)PPCDis_Data + 0x1cc0),
    0x00090000,
    0x00000000,
    0x00000046,
    (unsigned long)((char *)PPCDis_Data + 0x1ccc),
    0x00090000,
    0x00000000,
    0x000c0086,
    (unsigned long)((char *)PPCDis_Data + 0x1cd8),
    0x00090000,
    0x00000000,
    0x000000c6,
    (unsigned long)((char *)PPCDis_Data + 0x1ce4),
    0x00090000,
    0x00000000,
    0x000001c6,
    (unsigned long)((char *)PPCDis_Data + 0x1cf0),
    0x00090000,
    0x00000000,
    0x00000206,
    (unsigned long)((char *)PPCDis_Data + 0x1cfc),
    0x00090000,
    0x00000000,
    0x00000246,
    (unsigned long)((char *)PPCDis_Data + 0x1d08),
    0x00090000,
    0x00000000,
    0x00000286,
    (unsigned long)((char *)PPCDis_Data + 0x1d14),
    0x00090000,
    0x00000000,
    0x000002c6,
    (unsigned long)((char *)PPCDis_Data + 0x1d20),
    0x00090000,
    0x00000000,
    0x00000306,
    (unsigned long)((char *)PPCDis_Data + 0x1d2c),
    0x00090000,
    0x00000000,
    0x00000346,
    (unsigned long)((char *)PPCDis_Data + 0x1d38),
    0x00090000,
    0x00000000,
    0x00000386,
    (unsigned long)((char *)PPCDis_Data + 0x1d44),
    0x00090000,
    0x00000000,
    0x000003c6,
    (unsigned long)((char *)PPCDis_Data + 0x1d50),
    0x00090000,
    0x00000000,
    /* 0x0048fdf8 12 bytes, "vmhaddshs" */
    0x61686d76,
    0x68736464,
    0x00000073,
    /* 0x0048fe04 12 bytes, "vmhraddshs" */
    0x72686d76,
    0x73646461,
    0x00007368,
    /* 0x0048fe10 12 bytes, "vmladduhm" */
    0x616c6d76,
    0x68756464,
    0x0000006d,
    /* 0x0048fe1c 12 bytes, "vmsumubm" */
    0x75736d76,
    0x6d62756d,
    0x00000000,
    /* 0x0048fe28 12 bytes, "vmsummbm" */
    0x75736d76,
    0x6d626d6d,
    0x00000000,
    /* 0x0048fe34 12 bytes, "vmsumuhm" */
    0x75736d76,
    0x6d68756d,
    0x00000000,
    /* 0x0048fe40 12 bytes, "vmsumuhs" */
    0x75736d76,
    0x7368756d,
    0x00000000,
    /* 0x0048fe4c 12 bytes, "vmsumshm" */
    0x75736d76,
    0x6d68736d,
    0x00000000,
    /* 0x0048fe58 12 bytes, "vmsumshs" */
    0x75736d76,
    0x7368736d,
    0x00000000,
    /* 0x0048fe64 8 bytes, "vsel" */
    0x6c657376,
    0x00000000,
    /* 0x0048fe6c 8 bytes, "vperm" */
    0x72657076,
    0x0000006d,
    /* 0x0048fe74 8 bytes, "vsldoi" */
    0x646c7376,
    0x0000696f,
    /* 0x0048fe7c 8 bytes, "vmaddfp" */
    0x64616d76,
    0x00706664,
    /* 0x0048fe84 236 bytes, 14 pointers: data_0048fdf8, data_0048fe04, data_0048fe10, data_0048fe1c ... */
    0x736d6e76,
    0x70666275,
    0x00000000,
    0x00060020,
    (unsigned long)((char *)PPCDis_Data + 0x1e28),
    0x00090000,
    0x00000000,
    0x00060021,
    (unsigned long)((char *)PPCDis_Data + 0x1e34),
    0x00090000,
    0x00000000,
    0x00060022,
    (unsigned long)((char *)PPCDis_Data + 0x1e40),
    0x00090000,
    0x00000000,
    0x00060024,
    (unsigned long)((char *)PPCDis_Data + 0x1e4c),
    0x00090000,
    0x00000000,
    0x00060025,
    (unsigned long)((char *)PPCDis_Data + 0x1e58),
    0x00090000,
    0x00000000,
    0x00060026,
    (unsigned long)((char *)PPCDis_Data + 0x1e64),
    0x00090000,
    0x00000000,
    0x00060027,
    (unsigned long)((char *)PPCDis_Data + 0x1e70),
    0x00090000,
    0x00000000,
    0x00060028,
    (unsigned long)((char *)PPCDis_Data + 0x1e7c),
    0x00090000,
    0x00000000,
    0x00060029,
    (unsigned long)((char *)PPCDis_Data + 0x1e88),
    0x00090000,
    0x00000000,
    0x0006002a,
    (unsigned long)((char *)PPCDis_Data + 0x1e94),
    0x00090000,
    0x00000000,
    0x0006002b,
    (unsigned long)((char *)PPCDis_Data + 0x1e9c),
    0x00090000,
    0x00000000,
    0x0007002c,
    (unsigned long)((char *)PPCDis_Data + 0x1ea4),
    0x00090000,
    0x00000000,
    0x0008002e,
    (unsigned long)((char *)PPCDis_Data + 0x1eac),
    0x00090000,
    0x00000000,
    0x0008002f,
    (unsigned long)((char *)PPCDis_Data + 0x1eb4),
    0x00090000,
    0x00000000,
    /* 0x0048ff70 8 bytes, "ps_neg" */
    0x6e5f7370,
    0x00006765,
    /* 0x0048ff78 8 bytes, "ps_mr" */
    0x6d5f7370,
    0x00000072,
    /* 0x0048ff80 8 bytes, "ps_nabs" */
    0x6e5f7370,
    0x00736261,
    /* 0x0048ff88 8 bytes, "ps_abs" */
    0x615f7370,
    0x00007362,
    /* 0x0048ff90 12 bytes, "ps_merge00" */
    0x6d5f7370,
    0x65677265,
    0x00003030,
    /* 0x0048ff9c 12 bytes, "ps_merge01" */
    0x6d5f7370,
    0x65677265,
    0x00003130,
    /* 0x0048ffa8 12 bytes, "ps_merge10" */
    0x6d5f7370,
    0x65677265,
    0x00003031,
    /* 0x0048ffb4 12 bytes, "ps_merge11" */
    0x6d5f7370,
    0x65677265,
    0x00003131,
    /* 0x0048ffc0 8 bytes, "dbcz_l" */
    0x7a636264,
    0x00006c5f,
    /* 0x0048ffc8 144 bytes, 9 pointers: data_0048ff70, data_0048ff78, data_0048ff80, data_0048ff88 ... */
    0x00000028,
    (unsigned long)((char *)PPCDis_Data + 0x1fa0),
    0x00020000,
    0x00000000,
    0x00000048,
    (unsigned long)((char *)PPCDis_Data + 0x1fa8),
    0x00020000,
    0x00000000,
    0x00000088,
    (unsigned long)((char *)PPCDis_Data + 0x1fb0),
    0x00020000,
    0x00000000,
    0x00000108,
    (unsigned long)((char *)PPCDis_Data + 0x1fb8),
    0x00020000,
    0x00000000,
    0x00010210,
    (unsigned long)((char *)PPCDis_Data + 0x1fc0),
    0x00020000,
    0x00000000,
    0x00010230,
    (unsigned long)((char *)PPCDis_Data + 0x1fcc),
    0x00020000,
    0x00000000,
    0x00010250,
    (unsigned long)((char *)PPCDis_Data + 0x1fd8),
    0x00020000,
    0x00000000,
    0x00010270,
    (unsigned long)((char *)PPCDis_Data + 0x1fe4),
    0x00020000,
    0x00000000,
    0x000203f6,
    (unsigned long)((char *)PPCDis_Data + 0x1ff0),
    0x00020000,
    0x00000000,
    /* 0x00490058 8 bytes, "ps_sum0" */
    0x735f7370,
    0x00306d75,
    /* 0x00490060 8 bytes, "ps_sum1" */
    0x735f7370,
    0x00316d75,
    /* 0x00490068 12 bytes, "ps_muls0" */
    0x6d5f7370,
    0x30736c75,
    0x00000000,
    /* 0x00490074 12 bytes, "ps_muls1" */
    0x6d5f7370,
    0x31736c75,
    0x00000000,
    /* 0x00490080 12 bytes, "ps_madds0" */
    0x6d5f7370,
    0x73646461,
    0x00000030,
    /* 0x0049008c 12 bytes, "ps_madds1" */
    0x6d5f7370,
    0x73646461,
    0x00000031,
    /* 0x00490098 8 bytes, "ps_div" */
    0x645f7370,
    0x00007669,
    /* 0x004900a0 8 bytes, "ps_sub" */
    0x735f7370,
    0x00006275,
    /* 0x004900a8 8 bytes, "ps_add" */
    0x615f7370,
    0x00006464,
    /* 0x004900b0 8 bytes, "ps_sel" */
    0x735f7370,
    0x00006c65,
    /* 0x004900b8 8 bytes, "ps_res" */
    0x725f7370,
    0x00007365,
    /* 0x004900c0 8 bytes, "ps_mul" */
    0x6d5f7370,
    0x00006c75,
    /* 0x004900c8 12 bytes, "ps_rsqrte" */
    0x725f7370,
    0x74727173,
    0x00000065,
    /* 0x004900d4 8 bytes, "ps_msub" */
    0x6d5f7370,
    0x00627573,
    /* 0x004900dc 8 bytes, "ps_madd" */
    0x6d5f7370,
    0x00646461,
    /* 0x004900e4 12 bytes, "ps_nmsub" */
    0x6e5f7370,
    0x6275736d,
    0x00000000,
    /* 0x004900f0 12 bytes, "ps_nmadd" */
    0x6e5f7370,
    0x6464616d,
    0x00000000,
    /* 0x004900fc 272 bytes, 17 pointers: data_00490058, data_00490060, data_00490068, data_00490074 ... */
    0x0003000a,
    (unsigned long)((char *)PPCDis_Data + 0x2088),
    0x00020000,
    0x00000000,
    0x0003000b,
    (unsigned long)((char *)PPCDis_Data + 0x2090),
    0x00020000,
    0x00000000,
    0x0004000c,
    (unsigned long)((char *)PPCDis_Data + 0x2098),
    0x00020000,
    0x00000000,
    0x0004000d,
    (unsigned long)((char *)PPCDis_Data + 0x20a4),
    0x00020000,
    0x00000000,
    0x0003000e,
    (unsigned long)((char *)PPCDis_Data + 0x20b0),
    0x00020000,
    0x00000000,
    0x0003000f,
    (unsigned long)((char *)PPCDis_Data + 0x20bc),
    0x00020000,
    0x00000000,
    0x00010012,
    (unsigned long)((char *)PPCDis_Data + 0x20c8),
    0x00020000,
    0x00000000,
    0x00010014,
    (unsigned long)((char *)PPCDis_Data + 0x20d0),
    0x00020000,
    0x00000000,
    0x00010015,
    (unsigned long)((char *)PPCDis_Data + 0x20d8),
    0x00020000,
    0x00000000,
    0x00030017,
    (unsigned long)((char *)PPCDis_Data + 0x20e0),
    0x00020000,
    0x00000000,
    0x00000018,
    (unsigned long)((char *)PPCDis_Data + 0x20e8),
    0x00020000,
    0x00000000,
    0x00040019,
    (unsigned long)((char *)PPCDis_Data + 0x20f0),
    0x00020000,
    0x00000000,
    0x0000001a,
    (unsigned long)((char *)PPCDis_Data + 0x20f8),
    0x00020000,
    0x00000000,
    0x0003001c,
    (unsigned long)((char *)PPCDis_Data + 0x2104),
    0x00020000,
    0x00000000,
    0x0003001d,
    (unsigned long)((char *)PPCDis_Data + 0x210c),
    0x00020000,
    0x00000000,
    0x0003001e,
    (unsigned long)((char *)PPCDis_Data + 0x2114),
    0x00020000,
    0x00000000,
    0x0003001f,
    (unsigned long)((char *)PPCDis_Data + 0x2120),
    0x00020000,
    0x00000000,
    /* 0x0049020c 8 bytes, "psq_lx" */
    0x5f717370,
    0x0000786c,
    /* 0x00490214 8 bytes, "psq_stx" */
    0x5f717370,
    0x00787473,
    /* 0x0049021c 8 bytes, "psq_lux" */
    0x5f717370,
    0x0078756c,
    /* 0x00490224 12 bytes, "psq_stux" */
    0x5f717370,
    0x78757473,
    0x00000000,
    /* 0x00490230 64 bytes, 4 pointers: data_0049020c, data_00490214, data_0049021c, data_00490224 */
    0x00050006,
    (unsigned long)((char *)PPCDis_Data + 0x223c),
    0x00020000,
    0x00000000,
    0x00050007,
    (unsigned long)((char *)PPCDis_Data + 0x2244),
    0x00020000,
    0x00000000,
    0x00050026,
    (unsigned long)((char *)PPCDis_Data + 0x224c),
    0x00020000,
    0x00000000,
    0x00050027,
    (unsigned long)((char *)PPCDis_Data + 0x2254),
    0x00020000,
    0x00000000,
    /* 0x00490270 12 bytes, "ps_cmpu0" */
    0x635f7370,
    0x3075706d,
    0x00000000,
    /* 0x0049027c 12 bytes, "ps_cmpo0" */
    0x635f7370,
    0x306f706d,
    0x00000000,
    /* 0x00490288 12 bytes, "ps_cmpu1" */
    0x635f7370,
    0x3175706d,
    0x00000000,
    /* 0x00490294 12 bytes, "ps_cmpo1" */
    0x635f7370,
    0x316f706d,
    0x00000000,
    /* 0x004902a0 64 bytes, 4 pointers: data_00490270, data_0049027c, data_00490288, data_00490294 */
    0x00060000,
    (unsigned long)((char *)PPCDis_Data + 0x22a0),
    0x00020000,
    0x00000000,
    0x00060020,
    (unsigned long)((char *)PPCDis_Data + 0x22ac),
    0x00020000,
    0x00000000,
    0x00060040,
    (unsigned long)((char *)PPCDis_Data + 0x22b8),
    0x00020000,
    0x00000000,
    0x00060060,
    (unsigned long)((char *)PPCDis_Data + 0x22c4),
    0x00020000,
    0x00000000,
    /* 0x004902e0 8 bytes, "mcrf" */
    0x6672636d,
    0x00000000,
    /* 0x004902e8 8 bytes, "bclr" */
    0x726c6362,
    0x00000000,
    /* 0x004902f0 8 bytes, "crnor" */
    0x6f6e7263,
    0x00000072,
    /* 0x004902f8 4 bytes, "rfi" */
    0x00696672,
    /* 0x004902fc 8 bytes, "rfci" */
    0x69636672,
    0x00000000,
    /* 0x00490304 8 bytes, "crandc" */
    0x6e617263,
    0x00006364,
    /* 0x0049030c 8 bytes, "isync" */
    0x6e797369,
    0x00000063,
    /* 0x00490314 8 bytes, "crxor" */
    0x6f787263,
    0x00000072,
    /* 0x0049031c 8 bytes, "crnand" */
    0x616e7263,
    0x0000646e,
    /* 0x00490324 8 bytes, "crand" */
    0x6e617263,
    0x00000064,
    /* 0x0049032c 8 bytes, "creqv" */
    0x71657263,
    0x00000076,
    /* 0x00490334 8 bytes, "crorc" */
    0x726f7263,
    0x00000063,
    /* 0x0049033c 8 bytes, "cror" */
    0x726f7263,
    0x00000000,
    /* 0x00490344 8 bytes, "bcctr" */
    0x74636362,
    0x00000072,
    /* 0x0049034c 224 bytes, 14 pointers: data_004902e0, data_004902e8, data_004902f0, data_004902f8 ... */
    0x00030000,
    (unsigned long)((char *)PPCDis_Data + 0x2310),
    0x000fffff,
    0x00000000,
    0x00010010,
    (unsigned long)((char *)PPCDis_Data + 0x2318),
    0x000fffff,
    0x00000008,
    0x00020021,
    (unsigned long)((char *)PPCDis_Data + 0x2320),
    0x000fffff,
    0x00000000,
    0x00040032,
    (unsigned long)((char *)PPCDis_Data + 0x2328),
    0x000fffff,
    0x00000000,
    0x00040033,
    (unsigned long)((char *)PPCDis_Data + 0x232c),
    0x00000040,
    0x00000000,
    0x00020081,
    (unsigned long)((char *)PPCDis_Data + 0x2334),
    0x000fffff,
    0x00000000,
    0x00040096,
    (unsigned long)((char *)PPCDis_Data + 0x233c),
    0x000fffff,
    0x00000000,
    0x000200c1,
    (unsigned long)((char *)PPCDis_Data + 0x2344),
    0x000fffff,
    0x00000000,
    0x000200e1,
    (unsigned long)((char *)PPCDis_Data + 0x234c),
    0x000fffff,
    0x00000000,
    0x00020101,
    (unsigned long)((char *)PPCDis_Data + 0x2354),
    0x000fffff,
    0x00000000,
    0x00020121,
    (unsigned long)((char *)PPCDis_Data + 0x235c),
    0x000fffff,
    0x00000000,
    0x000201a1,
    (unsigned long)((char *)PPCDis_Data + 0x2364),
    0x000fffff,
    0x00000000,
    0x000201c1,
    (unsigned long)((char *)PPCDis_Data + 0x236c),
    0x000fffff,
    0x00000000,
    0x00010210,
    (unsigned long)((char *)PPCDis_Data + 0x2374),
    0x000fffff,
    0x00000008,
    /* 0x0049042c 8 bytes, "bltlr" */
    0x6c746c62,
    0x00000072,
    /* 0x00490434 8 bytes, "bgtlr" */
    0x6c746762,
    0x00000072,
    /* 0x0049043c 8 bytes, "beqlr" */
    0x6c716562,
    0x00000072,
    /* 0x00490444 8 bytes, "bsolr" */
    0x6c6f7362,
    0x00000072,
    /* 0x0049044c 8 bytes, "bgelr" */
    0x6c656762,
    0x00000072,
    /* 0x00490454 8 bytes, "blelr" */
    0x6c656c62,
    0x00000072,
    /* 0x0049045c 8 bytes, "bnelr" */
    0x6c656e62,
    0x00000072,
    /* 0x00490464 8 bytes, "bnslr" */
    0x6c736e62,
    0x00000072,
    /* 0x0049046c 32 bytes, 8 pointers: data_0049042c, data_00490434, data_0049043c, data_00490444 ... */
    (unsigned long)((char *)PPCDis_Data + 0x245c),
    (unsigned long)((char *)PPCDis_Data + 0x2464),
    (unsigned long)((char *)PPCDis_Data + 0x246c),
    (unsigned long)((char *)PPCDis_Data + 0x2474),
    (unsigned long)((char *)PPCDis_Data + 0x247c),
    (unsigned long)((char *)PPCDis_Data + 0x2484),
    (unsigned long)((char *)PPCDis_Data + 0x248c),
    (unsigned long)((char *)PPCDis_Data + 0x2494),
    /* 0x0049048c 8 bytes, "bltctr" */
    0x63746c62,
    0x00007274,
    /* 0x00490494 8 bytes, "bgtctr" */
    0x63746762,
    0x00007274,
    /* 0x0049049c 8 bytes, "beqctr" */
    0x63716562,
    0x00007274,
    /* 0x004904a4 8 bytes, "bsoctr" */
    0x636f7362,
    0x00007274,
    /* 0x004904ac 8 bytes, "bgectr" */
    0x63656762,
    0x00007274,
    /* 0x004904b4 8 bytes, "blectr" */
    0x63656c62,
    0x00007274,
    /* 0x004904bc 8 bytes, "bnectr" */
    0x63656e62,
    0x00007274,
    /* 0x004904c4 8 bytes, "bnsctr" */
    0x63736e62,
    0x00007274,
    /* 0x004904cc 32 bytes, 8 pointers: data_0049048c, data_00490494, data_0049049c, data_004904a4 ... */
    (unsigned long)((char *)PPCDis_Data + 0x24bc),
    (unsigned long)((char *)PPCDis_Data + 0x24c4),
    (unsigned long)((char *)PPCDis_Data + 0x24cc),
    (unsigned long)((char *)PPCDis_Data + 0x24d4),
    (unsigned long)((char *)PPCDis_Data + 0x24dc),
    (unsigned long)((char *)PPCDis_Data + 0x24e4),
    (unsigned long)((char *)PPCDis_Data + 0x24ec),
    (unsigned long)((char *)PPCDis_Data + 0x24f4),
    /* 0x004904ec 8 bytes, "subfc" */
    0x66627573,
    0x00000063,
    /* 0x004904f4 8 bytes, "mulhdu" */
    0x686c756d,
    0x00007564,
    /* 0x004904fc 8 bytes, "addc" */
    0x63646461,
    0x00000000,
    /* 0x00490504 8 bytes, "mulhwu" */
    0x686c756d,
    0x00007577,
    /* 0x0049050c 8 bytes, "subf" */
    0x66627573,
    0x00000000,
    /* 0x00490514 8 bytes, "mulhd" */
    0x686c756d,
    0x00000064,
    /* 0x0049051c 8 bytes, "mulhw" */
    0x686c756d,
    0x00000077,
    /* 0x00490524 4 bytes, "neg" */
    0x0067656e,
    /* 0x00490528 4 bytes, "mul" */
    0x006c756d,
    /* 0x0049052c 8 bytes, "subfe" */
    0x66627573,
    0x00000065,
    /* 0x00490534 8 bytes, "adde" */
    0x65646461,
    0x00000000,
    /* 0x0049053c 8 bytes, "subfze" */
    0x66627573,
    0x0000657a,
    /* 0x00490544 8 bytes, "addze" */
    0x7a646461,
    0x00000065,
    /* 0x0049054c 8 bytes, "subfme" */
    0x66627573,
    0x0000656d,
    /* 0x00490554 8 bytes, "mulld" */
    0x6c6c756d,
    0x00000064,
    /* 0x0049055c 8 bytes, "addme" */
    0x6d646461,
    0x00000065,
    /* 0x00490564 8 bytes, "mullw" */
    0x6c6c756d,
    0x00000077,
    /* 0x0049056c 4 bytes, "doz" */
    0x007a6f64,
    /* 0x00490570 4 bytes, "add" */
    0x00646461,
    /* 0x00490574 4 bytes, "div" */
    0x00766964,
    /* 0x00490578 4 bytes, "abs" */
    0x00736261,
    /* 0x0049057c 8 bytes, "divs" */
    0x73766964,
    0x00000000,
    /* 0x00490584 8 bytes, "divdu" */
    0x64766964,
    0x00000075,
    /* 0x0049058c 8 bytes, "divwu" */
    0x77766964,
    0x00000075,
    /* 0x00490594 8 bytes, "nabs" */
    0x7362616e,
    0x00000000,
    /* 0x0049059c 8 bytes, "divd" */
    0x64766964,
    0x00000000,
    /* 0x004905a4 8 bytes, "divw" */
    0x77766964,
    0x00000000,
    /* 0x004905ac 432 bytes, 27 pointers: data_004904ec, data_004904f4, data_004904fc, data_00490504 ... */
    0x00010008,
    (unsigned long)((char *)PPCDis_Data + 0x251c),
    0x000fffff,
    0x00000000,
    0x00020009,
    (unsigned long)((char *)PPCDis_Data + 0x2524),
    0x00002000,
    0x40000000,
    0x0001000a,
    (unsigned long)((char *)PPCDis_Data + 0x252c),
    0x000fffff,
    0x00000000,
    0x0002000b,
    (unsigned long)((char *)PPCDis_Data + 0x2534),
    0x000fffff,
    0x00000000,
    0x00010028,
    (unsigned long)((char *)PPCDis_Data + 0x253c),
    0x000fffff,
    0x00000000,
    0x00020049,
    (unsigned long)((char *)PPCDis_Data + 0x2544),
    0x00002000,
    0x40000000,
    0x0002004b,
    (unsigned long)((char *)PPCDis_Data + 0x254c),
    0x000fffff,
    0x00000000,
    0x00030068,
    (unsigned long)((char *)PPCDis_Data + 0x2554),
    0x000fffff,
    0x00000000,
    0x0001006b,
    (unsigned long)((char *)PPCDis_Data + 0x2558),
    0x00000002,
    0x00000000,
    0x00010088,
    (unsigned long)((char *)PPCDis_Data + 0x255c),
    0x000fffff,
    0x00000000,
    0x0001008a,
    (unsigned long)((char *)PPCDis_Data + 0x2564),
    0x000fffff,
    0x00000000,
    0x000300c8,
    (unsigned long)((char *)PPCDis_Data + 0x256c),
    0x000fffff,
    0x00000000,
    0x000300ca,
    (unsigned long)((char *)PPCDis_Data + 0x2574),
    0x000fffff,
    0x00000000,
    0x000300e8,
    (unsigned long)((char *)PPCDis_Data + 0x257c),
    0x000fffff,
    0x00000000,
    0x000100e9,
    (unsigned long)((char *)PPCDis_Data + 0x2584),
    0x00002000,
    0x40000000,
    0x000300ea,
    (unsigned long)((char *)PPCDis_Data + 0x258c),
    0x000fffff,
    0x00000000,
    0x000100eb,
    (unsigned long)((char *)PPCDis_Data + 0x2594),
    0x000fffff,
    0x00000000,
    0x00010108,
    (unsigned long)((char *)PPCDis_Data + 0x259c),
    0x00000002,
    0x00000000,
    0x0001010a,
    (unsigned long)((char *)PPCDis_Data + 0x25a0),
    0x000fffff,
    0x00000000,
    0x0001014b,
    (unsigned long)((char *)PPCDis_Data + 0x25a4),
    0x00000002,
    0x00000000,
    0x00030168,
    (unsigned long)((char *)PPCDis_Data + 0x25a8),
    0x00000002,
    0x00000000,
    0x0001016b,
    (unsigned long)((char *)PPCDis_Data + 0x25ac),
    0x00000002,
    0x00000000,
    0x000101c9,
    (unsigned long)((char *)PPCDis_Data + 0x25b4),
    0x00002000,
    0x40000000,
    0x000101cb,
    (unsigned long)((char *)PPCDis_Data + 0x25bc),
    0x000fffff,
    0x00000000,
    0x000301e8,
    (unsigned long)((char *)PPCDis_Data + 0x25c4),
    0x00000002,
    0x00000000,
    0x000101e9,
    (unsigned long)((char *)PPCDis_Data + 0x25cc),
    0x00002000,
    0x40000000,
    0x000101eb,
    (unsigned long)((char *)PPCDis_Data + 0x25d4),
    0x000fffff,
    0x00000000,
    /* 0x0049075c 4 bytes, 4 bytes */
    0x00000000,
    /* 0x00490760 4 bytes, "." */
    0x0000002e,
    /* 0x00490764 4 bytes, "o" */
    0x0000006f,
    /* 0x00490768 20 bytes, 4 pointers: data_0049075c, data_00490760, data_00490764, data_00490768 */
    0x00002e6f,
    (unsigned long)((char *)PPCDis_Data + 0x278c),
    (unsigned long)((char *)PPCDis_Data + 0x2790),
    (unsigned long)((char *)PPCDis_Data + 0x2794),
    (unsigned long)((char *)PPCDis_Data + 0x2798),
    /* 0x0049077c 8 bytes, "iselgt" */
    0x6c657369,
    0x00007467,
    /* 0x00490784 8 bytes, "iseleq" */
    0x6c657369,
    0x00007165,
    /* 0x0049078c 8 bytes, "isellt" */
    0x6c657369,
    0x0000746c,
    /* 0x00490794 16 bytes, "isel<illegal>" */
    0x6c657369,
    0x6c6c693c,
    0x6c616765,
    0x0000003e,
    /* 0x004907a4 16 bytes, 4 pointers: data_0049077c, data_00490784, data_0049078c, data_00490794 */
    (unsigned long)((char *)PPCDis_Data + 0x27ac),
    (unsigned long)((char *)PPCDis_Data + 0x27b4),
    (unsigned long)((char *)PPCDis_Data + 0x27bc),
    (unsigned long)((char *)PPCDis_Data + 0x27c4),
    /* 0x004907b4 8 bytes, "fdivs" */
    0x76696466,
    0x00000073,
    /* 0x004907bc 8 bytes, "fsubs" */
    0x62757366,
    0x00000073,
    /* 0x004907c4 8 bytes, "fadds" */
    0x64646166,
    0x00000073,
    /* 0x004907cc 8 bytes, "fsqrts" */
    0x72717366,
    0x00007374,
    /* 0x004907d4 8 bytes, "fres" */
    0x73657266,
    0x00000000,
    /* 0x004907dc 8 bytes, "fmuls" */
    0x6c756d66,
    0x00000073,
    /* 0x004907e4 8 bytes, "fmsubs" */
    0x75736d66,
    0x00007362,
    /* 0x004907ec 8 bytes, "fmadds" */
    0x64616d66,
    0x00007364,
    /* 0x004907f4 8 bytes, "fnmsubs" */
    0x736d6e66,
    0x00736275,
    /* 0x004907fc 8 bytes, "fnmadds" */
    0x616d6e66,
    0x00736464,
    /* 0x00490804 160 bytes, 10 pointers: data_004907b4, data_004907bc, data_004907c4, data_004907cc ... */
    0x00010012,
    (unsigned long)((char *)PPCDis_Data + 0x27e4),
    0x000ffc1f,
    0x00000000,
    0x00010014,
    (unsigned long)((char *)PPCDis_Data + 0x27ec),
    0x000ffc1f,
    0x00000000,
    0x00010015,
    (unsigned long)((char *)PPCDis_Data + 0x27f4),
    0x000ffc1f,
    0x00000000,
    0x00040016,
    (unsigned long)((char *)PPCDis_Data + 0x27fc),
    0x000ffc19,
    0x00000004,
    0x00040018,
    (unsigned long)((char *)PPCDis_Data + 0x2804),
    0x000ffc1d,
    0x00000004,
    0x00030019,
    (unsigned long)((char *)PPCDis_Data + 0x280c),
    0x000ffc1f,
    0x00000000,
    0x0002001c,
    (unsigned long)((char *)PPCDis_Data + 0x2814),
    0x000ffc1f,
    0x00000000,
    0x0002001d,
    (unsigned long)((char *)PPCDis_Data + 0x281c),
    0x000ffc1f,
    0x00000000,
    0x0002001e,
    (unsigned long)((char *)PPCDis_Data + 0x2824),
    0x000ffc1f,
    0x00000000,
    0x0002001f,
    (unsigned long)((char *)PPCDis_Data + 0x282c),
    0x000ffc1f,
    0x00000000,
    /* 0x004908a4 8 bytes, "fdiv" */
    0x76696466,
    0x00000000,
    /* 0x004908ac 8 bytes, "fsub" */
    0x62757366,
    0x00000000,
    /* 0x004908b4 8 bytes, "fadd" */
    0x64646166,
    0x00000000,
    /* 0x004908bc 8 bytes, "fsqrt" */
    0x72717366,
    0x00000074,
    /* 0x004908c4 8 bytes, "fsel" */
    0x6c657366,
    0x00000000,
    /* 0x004908cc 8 bytes, "fmul" */
    0x6c756d66,
    0x00000000,
    /* 0x004908d4 8 bytes, "frsqrte" */
    0x71737266,
    0x00657472,
    /* 0x004908dc 8 bytes, "fmsub" */
    0x75736d66,
    0x00000062,
    /* 0x004908e4 8 bytes, "fmadd" */
    0x64616d66,
    0x00000064,
    /* 0x004908ec 8 bytes, "fnmsub" */
    0x736d6e66,
    0x00006275,
    /* 0x004908f4 8 bytes, "fnmadd" */
    0x616d6e66,
    0x00006464,
    /* 0x004908fc 176 bytes, 11 pointers: data_004908a4, data_004908ac, data_004908b4, data_004908bc ... */
    0x00010012,
    (unsigned long)((char *)PPCDis_Data + 0x28d4),
    0x000ffc1f,
    0x00000000,
    0x00010014,
    (unsigned long)((char *)PPCDis_Data + 0x28dc),
    0x000ffc1f,
    0x00000000,
    0x00010015,
    (unsigned long)((char *)PPCDis_Data + 0x28e4),
    0x000ffc1f,
    0x00000000,
    0x00040016,
    (unsigned long)((char *)PPCDis_Data + 0x28ec),
    0x000ffc19,
    0x00000004,
    0x00020017,
    (unsigned long)((char *)PPCDis_Data + 0x28f4),
    0x000ffc1d,
    0x00000004,
    0x00030019,
    (unsigned long)((char *)PPCDis_Data + 0x28fc),
    0x000ffc1f,
    0x00000000,
    0x0004001a,
    (unsigned long)((char *)PPCDis_Data + 0x2904),
    0x000ffc19,
    0x00000004,
    0x0002001c,
    (unsigned long)((char *)PPCDis_Data + 0x290c),
    0x000ffc1f,
    0x00000000,
    0x0002001d,
    (unsigned long)((char *)PPCDis_Data + 0x2914),
    0x000ffc1f,
    0x00000000,
    0x0002001e,
    (unsigned long)((char *)PPCDis_Data + 0x291c),
    0x000ffc1f,
    0x00000000,
    0x0002001f,
    (unsigned long)((char *)PPCDis_Data + 0x2924),
    0x000ffc1f,
    0x00000000,
    /* 0x004909ac 8 bytes, "rlwimi" */
    0x69776c72,
    0x0000696d,
    /* 0x004909b4 8 bytes, "rlwinm" */
    0x69776c72,
    0x00006d6e,
    /* 0x004909bc 8 bytes, "rlmi" */
    0x696d6c72,
    0x00000000,
    /* 0x004909c4 8 bytes, "rlwnm" */
    0x6e776c72,
    0x0000006d,
    /* 0x004909cc 16 bytes, 4 pointers: data_004909ac, data_004909b4, data_004909bc, data_004909c4 */
    (unsigned long)((char *)PPCDis_Data + 0x29dc),
    (unsigned long)((char *)PPCDis_Data + 0x29e4),
    (unsigned long)((char *)PPCDis_Data + 0x29ec),
    (unsigned long)((char *)PPCDis_Data + 0x29f4),
    /* 0x004909dc 8 bytes, "rldicl" */
    0x69646c72,
    0x00006c63,
    /* 0x004909e4 8 bytes, "rldicr" */
    0x69646c72,
    0x00007263,
    /* 0x004909ec 8 bytes, "rldic" */
    0x69646c72,
    0x00000063,
    /* 0x004909f4 8 bytes, "rldimi" */
    0x69646c72,
    0x0000696d,
    /* 0x004909fc 16 bytes, 4 pointers: data_004909dc, data_004909e4, data_004909ec, data_004909f4 */
    (unsigned long)((char *)PPCDis_Data + 0x2a0c),
    (unsigned long)((char *)PPCDis_Data + 0x2a14),
    (unsigned long)((char *)PPCDis_Data + 0x2a1c),
    (unsigned long)((char *)PPCDis_Data + 0x2a24),
    /* 0x00490a0c 8 bytes, "rldcl" */
    0x63646c72,
    0x0000006c,
    /* 0x00490a14 8 bytes, "rldcr" */
    0x63646c72,
    0x00000072,
    /* 0x00490a1c 8 bytes, 2 pointers: data_00490a0c, data_00490a14 */
    (unsigned long)((char *)PPCDis_Data + 0x2a3c),
    (unsigned long)((char *)PPCDis_Data + 0x2a44),
    /* 0x00490a24 4 bytes, 4 bytes */
    0x00000000,
    /* 0x00490a28 1000 bytes, 1000 bytes */
    0x00040005,
    0x00050000,
    0x00000006,
    0x00070005,
    0x00050000,
    0x00000008,
    0x0009000a,
    0x000a0000,
    0x0000000a,
    0x000b000a,
    0x000a0000,
    0x00000013,
    0x00140005,
    0x00050000,
    0x00000015,
    0x00170005,
    0x00050000,
    0x00000018,
    0x001a0005,
    0x00050000,
    0x0000001b,
    0x001c0005,
    0x00050000,
    0x00000020,
    0x00260005,
    0x00050000,
    0x00000027,
    0x00280005,
    0x000a0000,
    0x00000035,
    0x00360005,
    0x00050000,
    0x00000037,
    0x003a0005,
    0x00050000,
    0x0000003c,
    0x00440005,
    0x00050000,
    0x00000047,
    0x00490005,
    0x000a0000,
    0x0000004b,
    0x0053000a,
    0x00050000,
    0x00000054,
    0x00560005,
    0x00050000,
    0x00000057,
    0x00670005,
    0x00050000,
    0x00000068,
    0x0077000a,
    0x00050000,
    0x0000007c,
    0x00830005,
    0x00050000,
    0x00000087,
    0x00880005,
    0x000a0000,
    0x0000008a,
    0x0090000a,
    0x00070000,
    0x00000092,
    0x00950005,
    0x00050000,
    0x00000096,
    0x00970005,
    0x00050000,
    0x000000a3,
    0x00a70005,
    0x00050000,
    0x000000b5,
    0x00b70005,
    0x00050000,
    0x000000c7,
    0x00c80005,
    0x000a0000,
    0x000000ca,
    0x00d2000a,
    0x00050000,
    0x000000d6,
    0x00d70005,
    0x00050000,
    0x000000e7,
    0x00e80005,
    0x000a0000,
    0x000000e9,
    0x00ea000a,
    0x000a0000,
    0x000000eb,
    0x00f2000a,
    0x00050000,
    0x000000f6,
    0x00f70005,
    0x00050000,
    0x00000106,
    0x010a0005,
    0x000a0000,
    0x00000116,
    0x01170005,
    0x00050000,
    0x0000011c,
    0x01320005,
    0x00050000,
    0x00000136,
    0x01370005,
    0x00050000,
    0x0000013c,
    0x01430005,
    0x00070000,
    0x00000153,
    0x01550007,
    0x00050000,
    0x00000156,
    0x01570005,
    0x00050000,
    0x00000167,
    0x01680005,
    0x000a0000,
    0x00000172,
    0x01730005,
    0x00070000,
    0x00000175,
    0x01760005,
    0x00050000,
    0x00000177,
    0x01970005,
    0x00050000,
    0x0000019c,
    0x019d0005,
    0x00090000,
    0x000001b2,
    0x01b60005,
    0x00050000,
    0x000001b7,
    0x01bc0005,
    0x00050000,
    0x000001c3,
    0x01c60007,
    0x00050000,
    0x000001c9,
    0x01cb000a,
    0x000a0000,
    0x000001d3,
    0x01d60007,
    0x00050000,
    0x000001dc,
    0x01e60005,
    0x00050000,
    0x000001e7,
    0x01e90005,
    0x000a0000,
    0x000001eb,
    0x01f2000a,
    0x00050000,
    0x00000200,
    0x02080005,
    0x000a0000,
    0x0000020a,
    0x0215000a,
    0x00050000,
    0x00000216,
    0x02170005,
    0x00050000,
    0x00000218,
    0x021b0005,
    0x00050000,
    0x00000228,
    0x0236000a,
    0x00050000,
    0x00000237,
    0x02530005,
    0x00050000,
    0x00000255,
    0x02560005,
    0x00050000,
    0x00000257,
    0x02680005,
    0x000a0000,
    0x0000026b,
    0x0277000a,
    0x00050000,
    0x00000288,
    0x028a000a,
    0x000a0000,
    0x00000293,
    0x02950005,
    0x00050000,
    0x00000296,
    0x02970005,
    0x00050000,
    0x000002b7,
    0x02c80005,
    0x000a0000,
    0x000002ca,
    0x02d5000a,
    0x00050000,
    0x000002d7,
    0x02e80005,
    0x000a0000,
    0x000002e9,
    0x02ea000a,
    0x000a0000,
    0x000002eb,
    0x02f7000a,
    0x00050000,
    0x00000308,
    0x030a000a,
    0x000a0000,
    0x00000316,
    0x03180005,
    0x00050000,
    0x0000031a,
    0x03360005,
    0x00050000,
    0x00000338,
    0x034b0005,
    0x000a0000,
    0x00000356,
    0x03680005,
    0x000a0000,
    0x0000036b,
    0x0392000a,
    0x00050000,
    0x00000396,
    0x039a0005,
    0x00050000,
    0x000003b2,
    0x03ba0005,
    0x00050000,
    0x000003c6,
    0x03c90005,
    0x000a0000,
    0x000003cb,
    0x03d2000a,
    0x00050000,
    0x000003d6,
    0x03d70005,
    0x00050000,
    0x000003da,
    0x03e60005,
    0x00050000,
    0x000003e8,
    0x03e9000a,
    0x000a0000,
    0x000003eb,
    0x03f2000a,
    0x00050000,
    0x000003f6,
    0x00000005,
    /* 0x00490e10 4 bytes, 4 bytes */
    0x00000012,
    /* 0x00490e14 64 bytes, 64 bytes */
    0x0014000b,
    0x000b0000,
    0x00000015,
    0x0016000b,
    0x000b0000,
    0x00000017,
    0x0019000b,
    0x000b0000,
    0x0000001a,
    0x001c000b,
    0x000b0000,
    0x0000001d,
    0x001e000b,
    0x000b0000,
    0x0000001f,
    0x0000000b,
    /* 0x00490e54 4 bytes, 4 bytes */
    0x00000000,
    /* 0x00490e58 104 bytes, 104 bytes */
    0x000c0005,
    0x00050000,
    0x0000000e,
    0x000f0005,
    0x00050000,
    0x00000020,
    0x00260005,
    0x00050000,
    0x00000028,
    0x00400005,
    0x00050000,
    0x00000046,
    0x00480005,
    0x00050000,
    0x00000086,
    0x00880005,
    0x00050000,
    0x00000108,
    0x02470005,
    0x00050000,
    0x000002c7,
    0x032e0008,
    0x00050000,
    0x0000032f,
    0x034e0005,
    0x00050000,
    /* 0x00490ec0 80 bytes, 20 pointers: PPCDis_Branch, PPCDis_BranchConditional, PPCDis_SystemCall, PPCDis_DForm ... */
    (unsigned long)PPCDis_Branch,
    (unsigned long)PPCDis_BranchConditional,
    (unsigned long)PPCDis_SystemCall,
    (unsigned long)PPCDis_DForm,
    (unsigned long)PPCDis_LoadDoubleword,
    (unsigned long)PPCDis_XForm,
    (unsigned long)PPCDis_Opcode19,
    (unsigned long)PPCDis_MoveSpecialRegister,
    (unsigned long)PPCDis_Mtfsf,
    (unsigned long)PPCDis_Sradi,
    (unsigned long)PPCDis_IntegerArithmetic,
    (unsigned long)PPCDis_FloatAForm,
    (unsigned long)PPCDis_Rotate,
    (unsigned long)PPCDis_RotateMD,
    (unsigned long)PPCDis_RotateMDS,
    (unsigned long)PPCDis_Opcode30,
    (unsigned long)PPCDis_Opcode31,
    (unsigned long)PPCDis_Opcode63,
    (unsigned long)PPCDis_Opcode4,
    (unsigned long)PPCDis_Invalid,
    /* 0x00490f10 8 bytes, "dcr%d" */
    0x25726364,
    0x00000064,
    /* 0x00490f18 12 bytes, "; '%c%c'" */
    0x2527203b,
    0x27632563,
    0x00000000,
    /* 0x00490f24 8 bytes, "0x%08X" */
    0x30257830,
    0x00005838,
    /* 0x00490f2c 8 bytes, "$%+d" */
    0x642b2524,
    0x00000000,
    /* 0x00490f34 8 bytes, "%d(%s)" */
    0x25286425,
    0x00002973,
    /* 0x00490f3c 8 bytes, "%s%04x" */
    0x30257325,
    0x00007834,
    /* 0x00490f44 8 bytes, "%s%08x" */
    0x30257325,
    0x00007838,
    /* 0x00490f4c 4 bytes, "%d" */
    0x00006425,
    /* 0x00490f50 8 bytes, "cr%d" */
    0x64257263,
    0x00000000,
    /* 0x00490f58 8 bytes, "sr%d" */
    0x64257273,
    0x00000000,
    /* 0x00490f60 8 bytes, "spr%d" */
    0x25727073,
    0x00000064,
    /* 0x00490f68 8 bytes, "????" */
    0x3f3f3f3f,
    0x00000000,
    /* 0x00490f70 8 bytes, "vr%d" */
    0x64257276,
    0x00000000,
    /* 0x00490f78 8 bytes, "fp%d" */
    0x64257066,
    0x00000000,
    /* 0x00490f80 4 bytes, "rsp" */
    0x00707372,
    /* 0x00490f84 8 bytes, "rtoc" */
    0x636f7472,
    0x00000000,
    /* 0x00490f8c 32 bytes, strings "r%d" "'%s'" "%02x" "<No name>" */
    0x00642572,
    0x27732527,
    0x00000000,
    0x78323025,
    0x00000000,
    0x206f4e3c,
    0x656d616e,
    0x0000003e,
    /* 0x00490fac 4 bytes, "0x" */
    0x00007830,
    /* 0x00490fb0 4 bytes, "$" */
    0x00000024,
    /* 0x00490fb4 8 bytes, "dc.l" */
    0x6c2e6364,
    0x00000000,
    /* 0x00490fbc 8 bytes, "0x%08x" */
    0x30257830,
    0x00007838,
    /* 0x00490fc4 28 bytes, "; Invalid opcode '%c%c%c%c'" */
    0x6e49203b,
    0x696c6176,
    0x706f2064,
    0x65646f63,
    0x63252720,
    0x63256325,
    0x00276325,
    /* 0x00490fe0 4 bytes, "%s." */
    0x002e7325,
    /* 0x00490fe4 4 bytes, "%s" */
    0x00007325,
    /* 0x00490fe8 8 bytes, "insrwi" */
    0x72736e69,
    0x00006977,
    /* 0x00490ff0 8 bytes, "inslwi" */
    0x6c736e69,
    0x00006977,
    /* 0x00490ff8 8 bytes, "clrrwi" */
    0x72726c63,
    0x00006977,
    /* 0x00491000 8 bytes, "clrlwi" */
    0x6c726c63,
    0x00006977,
    /* 0x00491008 8 bytes, "rotlwi" */
    0x6c746f72,
    0x00006977,
    /* 0x00491010 8 bytes, "rotrwi" */
    0x72746f72,
    0x00006977,
    /* 0x00491018 8 bytes, "slwi" */
    0x69776c73,
    0x00000000,
    /* 0x00491020 8 bytes, "srwi" */
    0x69777273,
    0x00000000,
    /* 0x00491028 8 bytes, "extlwi" */
    0x6c747865,
    0x00006977,
    /* 0x00491030 8 bytes, "extrwi" */
    0x72747865,
    0x00006977,
    /* 0x00491038 12 bytes, "clrlslwi" */
    0x6c726c63,
    0x69776c73,
    0x00000000,
    /* 0x00491044 8 bytes, "rotlw" */
    0x6c746f72,
    0x00000077,
    /* 0x0049104c 8 bytes, "sradi." */
    0x64617273,
    0x00002e69,
    /* 0x00491054 8 bytes, "sradi" */
    0x64617273,
    0x00000069,
    /* 0x0049105c 8 bytes, "mtfsf." */
    0x7366746d,
    0x00002e66,
    /* 0x00491064 8 bytes, "mtfsf" */
    0x7366746d,
    0x00000066,
    /* 0x0049106c 8 bytes, "mfxer" */
    0x6578666d,
    0x00000072,
    /* 0x00491074 8 bytes, "mflr" */
    0x726c666d,
    0x00000000,
    /* 0x0049107c 8 bytes, "mfctr" */
    0x7463666d,
    0x00000072,
    /* 0x00491084 8 bytes, "mfdsisr" */
    0x7364666d,
    0x00727369,
    /* 0x0049108c 8 bytes, "mfdar" */
    0x6164666d,
    0x00000072,
    /* 0x00491094 8 bytes, "mfdec" */
    0x6564666d,
    0x00000063,
    /* 0x0049109c 8 bytes, "mfsdr1" */
    0x6473666d,
    0x00003172,
    /* 0x004910a4 8 bytes, "mfsrr0" */
    0x7273666d,
    0x00003072,
    /* 0x004910ac 8 bytes, "mfsrr1" */
    0x7273666d,
    0x00003172,
    /* 0x004910b4 12 bytes, "mfvrsave" */
    0x7276666d,
    0x65766173,
    0x00000000,
    /* 0x004910c0 8 bytes, "mfasr" */
    0x7361666d,
    0x00000072,
    /* 0x004910c8 8 bytes, "mfear" */
    0x6165666d,
    0x00000072,
    /* 0x004910d0 8 bytes, "mfpvr" */
    0x7670666d,
    0x00000072,
    /* 0x004910d8 8 bytes, "mfcdbcr" */
    0x6463666d,
    0x00726362,
    /* 0x004910e0 8 bytes, "mfdac1" */
    0x6164666d,
    0x00003163,
    /* 0x004910e8 8 bytes, "mfdac2" */
    0x6164666d,
    0x00003263,
    /* 0x004910f0 8 bytes, "mfdbcr" */
    0x6264666d,
    0x00007263,
    /* 0x004910f8 8 bytes, "mfdbsr" */
    0x6264666d,
    0x00007273,
    /* 0x00491100 8 bytes, "mfdccr" */
    0x6364666d,
    0x00007263,
    /* 0x00491108 8 bytes, "mfdcwr" */
    0x6364666d,
    0x00007277,
    /* 0x00491110 8 bytes, "mfdear" */
    0x6564666d,
    0x00007261,
    /* 0x00491118 8 bytes, "mfesr" */
    0x7365666d,
    0x00000072,
    /* 0x00491120 8 bytes, "mfevpr" */
    0x7665666d,
    0x00007270,
    /* 0x00491128 8 bytes, "mfiac1" */
    0x6169666d,
    0x00003163,
    /* 0x00491130 8 bytes, "mfiac2" */
    0x6169666d,
    0x00003263,
    /* 0x00491138 8 bytes, "mficcr" */
    0x6369666d,
    0x00007263,
    /* 0x00491140 12 bytes, "mficdbdr" */
    0x6369666d,
    0x72646264,
    0x00000000,
    /* 0x0049114c 8 bytes, "mfpid" */
    0x6970666d,
    0x00000064,
    /* 0x00491154 8 bytes, "mfpit" */
    0x6970666d,
    0x00000074,
    /* 0x0049115c 8 bytes, "mfpbl1" */
    0x6270666d,
    0x0000316c,
    /* 0x00491164 8 bytes, "mfpbl2" */
    0x6270666d,
    0x0000326c,
    /* 0x0049116c 12 bytes, "INST_403" */
    0x54534e49,
    0x3330345f,
    0x00000000,
    /* 0x00491178 8 bytes, "mfpbu2" */
    0x6270666d,
    0x00003275,
    /* 0x00491180 8 bytes, "mfsgr" */
    0x6773666d,
    0x00000072,
    /* 0x00491188 8 bytes, "mfsler" */
    0x6c73666d,
    0x00007265,
    /* 0x00491190 8 bytes, "mfsprg0" */
    0x7073666d,
    0x00306772,
    /* 0x00491198 8 bytes, "mfsprg1" */
    0x7073666d,
    0x00316772,
    /* 0x004911a0 8 bytes, "mfsprg2" */
    0x7073666d,
    0x00326772,
    /* 0x004911a8 8 bytes, "mfsprg3" */
    0x7073666d,
    0x00336772,
    /* 0x004911b0 8 bytes, "mfsrr2" */
    0x7273666d,
    0x00003272,
    /* 0x004911b8 8 bytes, "mfsrr3" */
    0x7273666d,
    0x00003372,
    /* 0x004911c0 8 bytes, "mftbhi" */
    0x6274666d,
    0x00006968,
    /* 0x004911c8 8 bytes, "mftbhu" */
    0x6274666d,
    0x00007568,
    /* 0x004911d0 8 bytes, "mftblo" */
    0x6274666d,
    0x00006f6c,
    /* 0x004911d8 8 bytes, "mftblu" */
    0x6274666d,
    0x0000756c,
    /* 0x004911e0 8 bytes, "mftcr" */
    0x6374666d,
    0x00000072,
    /* 0x004911e8 8 bytes, "mftsr" */
    0x7374666d,
    0x00000072,
    /* 0x004911f0 8 bytes, "mfzpr" */
    0x707a666d,
    0x00000072,
    /* 0x004911f8 8 bytes, "mfspr" */
    0x7073666d,
    0x00000072,
    /* 0x00491200 8 bytes, "mtxer" */
    0x6578746d,
    0x00000072,
    /* 0x00491208 8 bytes, "mtlr" */
    0x726c746d,
    0x00000000,
    /* 0x00491210 8 bytes, "mtctr" */
    0x7463746d,
    0x00000072,
    /* 0x00491218 8 bytes, "mtdsisr" */
    0x7364746d,
    0x00727369,
    /* 0x00491220 8 bytes, "mtdar" */
    0x6164746d,
    0x00000072,
    /* 0x00491228 8 bytes, "mtdec" */
    0x6564746d,
    0x00000063,
    /* 0x00491230 8 bytes, "mtsdr1" */
    0x6473746d,
    0x00003172,
    /* 0x00491238 8 bytes, "mtsrr0" */
    0x7273746d,
    0x00003072,
    /* 0x00491240 8 bytes, "mtsrr1" */
    0x7273746d,
    0x00003172,
    /* 0x00491248 12 bytes, "mtvrsave" */
    0x7276746d,
    0x65766173,
    0x00000000,
    /* 0x00491254 8 bytes, "mtasr" */
    0x7361746d,
    0x00000072,
    /* 0x0049125c 8 bytes, "mtear" */
    0x6165746d,
    0x00000072,
    /* 0x00491264 8 bytes, "mttbl" */
    0x6274746d,
    0x0000006c,
    /* 0x0049126c 8 bytes, "mttbu" */
    0x6274746d,
    0x00000075,
    /* 0x00491274 8 bytes, "mtcdbcr" */
    0x6463746d,
    0x00726362,
    /* 0x0049127c 8 bytes, "mtdac1" */
    0x6164746d,
    0x00003163,
    /* 0x00491284 8 bytes, "mtdac2" */
    0x6164746d,
    0x00003263,
    /* 0x0049128c 8 bytes, "mtdbcr" */
    0x6264746d,
    0x00007263,
    /* 0x00491294 8 bytes, "mtdbsr" */
    0x6264746d,
    0x00007273,
    /* 0x0049129c 8 bytes, "mtdccr" */
    0x6364746d,
    0x00007263,
    /* 0x004912a4 8 bytes, "mtdcwr" */
    0x6364746d,
    0x00007277,
    /* 0x004912ac 8 bytes, "mtdear" */
    0x6564746d,
    0x00007261,
    /* 0x004912b4 8 bytes, "mtesr" */
    0x7365746d,
    0x00000072,
    /* 0x004912bc 8 bytes, "mtevpr" */
    0x7665746d,
    0x00007270,
    /* 0x004912c4 8 bytes, "mtiac1" */
    0x6169746d,
    0x00003163,
    /* 0x004912cc 8 bytes, "mtiac2" */
    0x6169746d,
    0x00003263,
    /* 0x004912d4 8 bytes, "mticcr" */
    0x6369746d,
    0x00007263,
    /* 0x004912dc 12 bytes, "mticdbdr" */
    0x6369746d,
    0x72646264,
    0x00000000,
    /* 0x004912e8 8 bytes, "mtpid" */
    0x6970746d,
    0x00000064,
    /* 0x004912f0 8 bytes, "mtpit" */
    0x6970746d,
    0x00000074,
    /* 0x004912f8 8 bytes, "mtpbl1" */
    0x6270746d,
    0x0000316c,
    /* 0x00491300 8 bytes, "mtpbl2" */
    0x6270746d,
    0x0000326c,
    /* 0x00491308 8 bytes, "mtpbu1" */
    0x6270746d,
    0x00003175,
    /* 0x00491310 8 bytes, "mtpbu2" */
    0x6270746d,
    0x00003275,
    /* 0x00491318 8 bytes, "mtsgr" */
    0x6773746d,
    0x00000072,
    /* 0x00491320 8 bytes, "mtsler" */
    0x6c73746d,
    0x00007265,
    /* 0x00491328 8 bytes, "mtsprg0" */
    0x7073746d,
    0x00306772,
    /* 0x00491330 8 bytes, "mtsprg1" */
    0x7073746d,
    0x00316772,
    /* 0x00491338 8 bytes, "mtsprg2" */
    0x7073746d,
    0x00326772,
    /* 0x00491340 8 bytes, "mtsprg3" */
    0x7073746d,
    0x00336772,
    /* 0x00491348 8 bytes, "mtsrr2" */
    0x7273746d,
    0x00003272,
    /* 0x00491350 8 bytes, "mtsrr3" */
    0x7273746d,
    0x00003372,
    /* 0x00491358 8 bytes, "mttbhi" */
    0x6274746d,
    0x00006968,
    /* 0x00491360 8 bytes, "mttblo" */
    0x6274746d,
    0x00006f6c,
    /* 0x00491368 8 bytes, "mttcr" */
    0x6374746d,
    0x00000072,
    /* 0x00491370 8 bytes, "mttsr" */
    0x7374746d,
    0x00000072,
    /* 0x00491378 8 bytes, "mtzpr" */
    0x707a746d,
    0x00000072,
    /* 0x00491380 8 bytes, "mtspr" */
    0x7073746d,
    0x00000072,
    /* 0x00491388 8 bytes, "mtcrf" */
    0x7263746d,
    0x00000066,
    /* 0x00491390 8 bytes, "0x%x" */
    0x78257830,
    0x00000000,
    /* 0x00491398 8 bytes, "mftb" */
    0x6274666d,
    0x00000000,
    /* 0x004913a0 8 bytes, "mftbu" */
    0x6274666d,
    0x00000075,
    /* 0x004913a8 8 bytes, "mtbr" */
    0x7262746d,
    0x00000000,
    /* 0x004913b0 8 bytes, "mtbru" */
    0x7262746d,
    0x00000075,
    /* 0x004913b8 8 bytes, "mfbear" */
    0x6562666d,
    0x00007261,
    /* 0x004913c0 8 bytes, "mfbesr" */
    0x6562666d,
    0x00007273,
    /* 0x004913c8 8 bytes, "mfbesr0" */
    0x6562666d,
    0x00307273,
    /* 0x004913d0 8 bytes, "mfbr0" */
    0x7262666d,
    0x00000030,
    /* 0x004913d8 8 bytes, "mfbrcr0" */
    0x7262666d,
    0x00307263,
    /* 0x004913e0 8 bytes, "mfbr1" */
    0x7262666d,
    0x00000031,
    /* 0x004913e8 8 bytes, "mfbrcr1" */
    0x7262666d,
    0x00317263,
    /* 0x004913f0 8 bytes, "mfbr2" */
    0x7262666d,
    0x00000032,
    /* 0x004913f8 8 bytes, "mfbrcr2" */
    0x7262666d,
    0x00327263,
    /* 0x00491400 8 bytes, "mfbr3" */
    0x7262666d,
    0x00000033,
    /* 0x00491408 8 bytes, "mfbrcr3" */
    0x7262666d,
    0x00337263,
    /* 0x00491410 8 bytes, "mfbr4" */
    0x7262666d,
    0x00000034,
    /* 0x00491418 8 bytes, "mfbrcr4" */
    0x7262666d,
    0x00347263,
    /* 0x00491420 8 bytes, "mfbr5" */
    0x7262666d,
    0x00000035,
    /* 0x00491428 8 bytes, "mfbrcr5" */
    0x7262666d,
    0x00357263,
    /* 0x00491430 8 bytes, "mfbr6" */
    0x7262666d,
    0x00000036,
    /* 0x00491438 8 bytes, "mfbrcr6" */
    0x7262666d,
    0x00367263,
    /* 0x00491440 8 bytes, "mfbr7" */
    0x7262666d,
    0x00000037,
    /* 0x00491448 8 bytes, "mfbrcr7" */
    0x7262666d,
    0x00377263,
    /* 0x00491450 12 bytes, "mfdmacc0" */
    0x6d64666d,
    0x30636361,
    0x00000000,
    /* 0x0049145c 12 bytes, "mfdmacc1" */
    0x6d64666d,
    0x31636361,
    0x00000000,
    /* 0x00491468 12 bytes, "mfdmacc2" */
    0x6d64666d,
    0x32636361,
    0x00000000,
    /* 0x00491474 12 bytes, "mfdmacc3" */
    0x6d64666d,
    0x33636361,
    0x00000000,
    /* 0x00491480 12 bytes, "mfdmacr0" */
    0x6d64666d,
    0x30726361,
    0x00000000,
    /* 0x0049148c 12 bytes, "mfdmacr1" */
    0x6d64666d,
    0x31726361,
    0x00000000,
    /* 0x00491498 12 bytes, "mfdmacr2" */
    0x6d64666d,
    0x32726361,
    0x00000000,
    /* 0x004914a4 12 bytes, "mfdmacr3" */
    0x6d64666d,
    0x33726361,
    0x00000000,
    /* 0x004914b0 12 bytes, "mfdmact0" */
    0x6d64666d,
    0x30746361,
    0x00000000,
    /* 0x004914bc 12 bytes, "mfdmact1" */
    0x6d64666d,
    0x31746361,
    0x00000000,
    /* 0x004914c8 12 bytes, "mfdmact2" */
    0x6d64666d,
    0x32746361,
    0x00000000,
    /* 0x004914d4 12 bytes, "mfdmact3" */
    0x6d64666d,
    0x33746361,
    0x00000000,
    /* 0x004914e0 12 bytes, "mfdmada0" */
    0x6d64666d,
    0x30616461,
    0x00000000,
    /* 0x004914ec 12 bytes, "mfdmada1" */
    0x6d64666d,
    0x31616461,
    0x00000000,
    /* 0x004914f8 12 bytes, "mfdmada2" */
    0x6d64666d,
    0x32616461,
    0x00000000,
    /* 0x00491504 12 bytes, "mfdmada3" */
    0x6d64666d,
    0x33616461,
    0x00000000,
    /* 0x00491510 12 bytes, "mfdmasa0" */
    0x6d64666d,
    0x30617361,
    0x00000000,
    /* 0x0049151c 12 bytes, "mfdmasa1" */
    0x6d64666d,
    0x31617361,
    0x00000000,
    /* 0x00491528 12 bytes, "mfdmasa2" */
    0x6d64666d,
    0x32617361,
    0x00000000,
    /* 0x00491534 12 bytes, "mfdmasa3" */
    0x6d64666d,
    0x33617361,
    0x00000000,
    /* 0x00491540 8 bytes, "mfdmasr" */
    0x6d64666d,
    0x00727361,
    /* 0x00491548 8 bytes, "mfexisr" */
    0x7865666d,
    0x00727369,
    /* 0x00491550 8 bytes, "mfexier" */
    0x7865666d,
    0x00726569,
    /* 0x00491558 8 bytes, "mfiocr" */
    0x6f69666d,
    0x00007263,
    /* 0x00491560 8 bytes, "mfpmcr0" */
    0x6d70666d,
    0x00307263,
    /* 0x00491568 8 bytes, "mfdcr" */
    0x6364666d,
    0x00000072,
    /* 0x00491570 8 bytes, "mtbear" */
    0x6562746d,
    0x00007261,
    /* 0x00491578 8 bytes, "mtbesr" */
    0x6562746d,
    0x00007273,
    /* 0x00491580 8 bytes, "mtbesr0" */
    0x6562746d,
    0x00307273,
    /* 0x00491588 8 bytes, "mtbr0" */
    0x7262746d,
    0x00000030,
    /* 0x00491590 8 bytes, "mtbrcr0" */
    0x7262746d,
    0x00307263,
    /* 0x00491598 8 bytes, "mtbr1" */
    0x7262746d,
    0x00000031,
    /* 0x004915a0 8 bytes, "mtbrcr1" */
    0x7262746d,
    0x00317263,
    /* 0x004915a8 8 bytes, "mtbr2" */
    0x7262746d,
    0x00000032,
    /* 0x004915b0 8 bytes, "mtbrcr2" */
    0x7262746d,
    0x00327263,
    /* 0x004915b8 8 bytes, "mtbr3" */
    0x7262746d,
    0x00000033,
    /* 0x004915c0 8 bytes, "mtbrcr3" */
    0x7262746d,
    0x00337263,
    /* 0x004915c8 8 bytes, "mtbr4" */
    0x7262746d,
    0x00000034,
    /* 0x004915d0 8 bytes, "mtbrcr4" */
    0x7262746d,
    0x00347263,
    /* 0x004915d8 8 bytes, "mtbr5" */
    0x7262746d,
    0x00000035,
    /* 0x004915e0 8 bytes, "mtbrcr5" */
    0x7262746d,
    0x00357263,
    /* 0x004915e8 8 bytes, "mtbr6" */
    0x7262746d,
    0x00000036,
    /* 0x004915f0 8 bytes, "mtbrcr6" */
    0x7262746d,
    0x00367263,
    /* 0x004915f8 8 bytes, "mtbr7" */
    0x7262746d,
    0x00000037,
    /* 0x00491600 8 bytes, "mtbrcr7" */
    0x7262746d,
    0x00377263,
    /* 0x00491608 12 bytes, "mtdmacc0" */
    0x6d64746d,
    0x30636361,
    0x00000000,
    /* 0x00491614 12 bytes, "mtdmacc1" */
    0x6d64746d,
    0x31636361,
    0x00000000,
    /* 0x00491620 12 bytes, "mtdmacc2" */
    0x6d64746d,
    0x32636361,
    0x00000000,
    /* 0x0049162c 12 bytes, "mtdmacc3" */
    0x6d64746d,
    0x33636361,
    0x00000000,
    /* 0x00491638 12 bytes, "mtdmacr0" */
    0x6d64746d,
    0x30726361,
    0x00000000,
    /* 0x00491644 12 bytes, "mtdmacr1" */
    0x6d64746d,
    0x31726361,
    0x00000000,
    /* 0x00491650 12 bytes, "mtdmacr2" */
    0x6d64746d,
    0x32726361,
    0x00000000,
    /* 0x0049165c 12 bytes, "mtdmacr3" */
    0x6d64746d,
    0x33726361,
    0x00000000,
    /* 0x00491668 12 bytes, "mtdmact0" */
    0x6d64746d,
    0x30746361,
    0x00000000,
    /* 0x00491674 12 bytes, "mtdmact1" */
    0x6d64746d,
    0x31746361,
    0x00000000,
    /* 0x00491680 12 bytes, "mtdmact2" */
    0x6d64746d,
    0x32746361,
    0x00000000,
    /* 0x0049168c 12 bytes, "mtdmact3" */
    0x6d64746d,
    0x33746361,
    0x00000000,
    /* 0x00491698 12 bytes, "mtdmada0" */
    0x6d64746d,
    0x30616461,
    0x00000000,
    /* 0x004916a4 12 bytes, "mtdmada1" */
    0x6d64746d,
    0x31616461,
    0x00000000,
    /* 0x004916b0 12 bytes, "mtdmada2" */
    0x6d64746d,
    0x32616461,
    0x00000000,
    /* 0x004916bc 12 bytes, "mtdmada3" */
    0x6d64746d,
    0x33616461,
    0x00000000,
    /* 0x004916c8 12 bytes, "mtdmasa0" */
    0x6d64746d,
    0x30617361,
    0x00000000,
    /* 0x004916d4 12 bytes, "mtdmasa1" */
    0x6d64746d,
    0x31617361,
    0x00000000,
    /* 0x004916e0 12 bytes, "mtdmasa2" */
    0x6d64746d,
    0x32617361,
    0x00000000,
    /* 0x004916ec 12 bytes, "mtdmasa3" */
    0x6d64746d,
    0x33617361,
    0x00000000,
    /* 0x004916f8 8 bytes, "mtdmasr" */
    0x6d64746d,
    0x00727361,
    /* 0x00491700 8 bytes, "mtexisr" */
    0x7865746d,
    0x00727369,
    /* 0x00491708 8 bytes, "mtexier" */
    0x7865746d,
    0x00726569,
    /* 0x00491710 8 bytes, "mtiocr" */
    0x6f69746d,
    0x00007263,
    /* 0x00491718 8 bytes, "mtpmcr0" */
    0x6d70746d,
    0x00307263,
    /* 0x00491720 8 bytes, "mtdcr" */
    0x6364746d,
    0x00000072,
    /* 0x00491728 8 bytes, "bdnzflr" */
    0x7a6e6462,
    0x00726c66,
    /* 0x00491730 8 bytes, "bdzflr" */
    0x667a6462,
    0x0000726c,
    /* 0x00491738 8 bytes, "bdnztlr" */
    0x7a6e6462,
    0x00726c74,
    /* 0x00491740 8 bytes, "bdztlr" */
    0x747a6462,
    0x0000726c,
    /* 0x00491748 8 bytes, "bdnzlr" */
    0x7a6e6462,
    0x0000726c,
    /* 0x00491750 8 bytes, "bdzlr" */
    0x6c7a6462,
    0x00000072,
    /* 0x00491758 4 bytes, "blr" */
    0x00726c62,
    /* 0x0049175c 4 bytes, "+" */
    0x0000002b,
    /* 0x00491760 4 bytes, "l" */
    0x0000006c,
    /* 0x00491764 8 bytes, "bctr" */
    0x72746362,
    0x00000000,
    /* 0x0049176c 24 bytes, "dCTR_NZERO_AND,%s_%s" */
    0x52544364,
    0x455a4e5f,
    0x415f4f52,
    0x252c444e,
    0x73255f73,
    0x00000000,
    /* 0x00491784 20 bytes, "dCTR_ZERO_AND,%s_%s" */
    0x52544364,
    0x52455a5f,
    0x4e415f4f,
    0x73252c44,
    0x0073255f,
    /* 0x00491798 16 bytes, "IF_NOT,%s_%s" */
    0x4e5f4649,
    0x252c544f,
    0x73255f73,
    0x00000000,
    /* 0x004917a8 12 bytes, "IF,%s_%s" */
    0x252c4649,
    0x73255f73,
    0x00000000,
    /* 0x004917b4 16 bytes, "dCTR_NZERO,%d" */
    0x52544364,
    0x455a4e5f,
    0x252c4f52,
    0x00000064,
    /* 0x004917c4 16 bytes, "dCTR_ZERO,%d" */
    0x52544364,
    0x52455a5f,
    0x64252c4f,
    0x00000000,
    /* 0x004917d4 12 bytes, "ALWAYS,%d" */
    0x41574c41,
    0x252c5359,
    0x00000064,
    /* 0x004917e0 8 bytes, "%d,%d" */
    0x252c6425,
    0x00000064,
    /* 0x004917e8 12 bytes, "%d,%d,%d" */
    0x252c6425,
    0x64252c64,
    0x00000000,
    /* 0x004917f4 12 bytes, "cr%d,cr%d" */
    0x64257263,
    0x2572632c,
    0x00000064,
    /* 0x00491800 8 bytes, "cr%d_%s" */
    0x64257263,
    0x0073255f,
    /* 0x00491808 8 bytes, "twlgt" */
    0x676c7774,
    0x00000074,
    /* 0x00491810 8 bytes, "twllt" */
    0x6c6c7774,
    0x00000074,
    /* 0x00491818 8 bytes, "tweq" */
    0x71657774,
    0x00000000,
    /* 0x00491820 8 bytes, "twlge" */
    0x676c7774,
    0x00000065,
    /* 0x00491828 8 bytes, "twlle" */
    0x6c6c7774,
    0x00000065,
    /* 0x00491830 8 bytes, "twgt" */
    0x74677774,
    0x00000000,
    /* 0x00491838 8 bytes, "twge" */
    0x65677774,
    0x00000000,
    /* 0x00491840 8 bytes, "twlt" */
    0x746c7774,
    0x00000000,
    /* 0x00491848 8 bytes, "twle" */
    0x656c7774,
    0x00000000,
    /* 0x00491850 8 bytes, "twne" */
    0x656e7774,
    0x00000000,
    /* 0x00491858 4 bytes, "not" */
    0x00746f6e,
    /* 0x0049185c 4 bytes, "mr" */
    0x0000726d,
    /* 0x00491860 4 bytes, "w" */
    0x00000077,
    /* 0x00491864 4 bytes, "lw" */
    0x0000776c,
    /* 0x00491868 8 bytes, "cr%d,%d" */
    0x64257263,
    0x0064252c,
    /* 0x00491870 12 bytes, "cr%d,0x%x" */
    0x64257263,
    0x2578302c,
    0x00000078,
    /* 0x0049187c 8 bytes, "tlbwehi" */
    0x77626c74,
    0x00696865,
    /* 0x00491884 8 bytes, "tlbwelo" */
    0x77626c74,
    0x006f6c65,
    /* 0x0049188c 8 bytes, "tlbwe" */
    0x77626c74,
    0x00000065,
    /* 0x00491894 8 bytes, "tlbrehi" */
    0x72626c74,
    0x00696865,
    /* 0x0049189c 8 bytes, "tlbrelo" */
    0x72626c74,
    0x006f6c65,
    /* 0x004918a4 8 bytes, "dstt" */
    0x74747364,
    0x00000000,
    /* 0x004918ac 8 bytes, "dststt" */
    0x73747364,
    0x00007474,
    /* 0x004918b4 8 bytes, "dssall" */
    0x61737364,
    0x00006c6c,
    /* 0x004918bc 8 bytes, "twlgti" */
    0x676c7774,
    0x00006974,
    /* 0x004918c4 8 bytes, "twllti" */
    0x6c6c7774,
    0x00006974,
    /* 0x004918cc 8 bytes, "tweqi" */
    0x71657774,
    0x00000069,
    /* 0x004918d4 8 bytes, "twlgei" */
    0x676c7774,
    0x00006965,
    /* 0x004918dc 8 bytes, "twllei" */
    0x6c6c7774,
    0x00006965,
    /* 0x004918e4 8 bytes, "twgti" */
    0x74677774,
    0x00000069,
    /* 0x004918ec 8 bytes, "twgei" */
    0x65677774,
    0x00000069,
    /* 0x004918f4 8 bytes, "twlti" */
    0x746c7774,
    0x00000069,
    /* 0x004918fc 8 bytes, "twlei" */
    0x656c7774,
    0x00000069,
    /* 0x00491904 8 bytes, "twnei" */
    0x656e7774,
    0x00000069,
    /* 0x0049190c 8 bytes, "cmpwi" */
    0x77706d63,
    0x00000069,
    /* 0x00491914 8 bytes, "cmplwi" */
    0x6c706d63,
    0x00006977,
    /* 0x0049191c 4 bytes, "li" */
    0x0000696c,
    /* 0x00491920 4 bytes, "lis" */
    0x0073696c,
    /* 0x00491924 8 bytes, "subi" */
    0x69627573,
    0x00000000,
    /* 0x0049192c 4 bytes, "nop" */
    0x00706f6e,
    /* 0x00491930 20 bytes, "; POWER instruction" */
    0x4f50203b,
    0x20524557,
    0x74736e69,
    0x74637572,
    0x006e6f69,
    /* 0x00491944 4 bytes, "sc" */
    0x00006373,
    /* 0x00491948 8 bytes, "bdnzf" */
    0x7a6e6462,
    0x00000066,
    /* 0x00491950 8 bytes, "bdzf" */
    0x667a6462,
    0x00000000,
    /* 0x00491958 8 bytes, "bdnzt" */
    0x7a6e6462,
    0x00000074,
    /* 0x00491960 8 bytes, "bdzt" */
    0x747a6462,
    0x00000000,
    /* 0x00491968 8 bytes, "bdnz" */
    0x7a6e6462,
    0x00000000,
    /* 0x00491970 4 bytes, "bdz" */
    0x007a6462,
    /* 0x00491974 4 bytes, "a" */
    0x00000061,
    /* 0x00491978 4 bytes, "la" */
    0x0000616c,
    /* 0x0049197c 4 bytes, "-" */
    0x0000002d,
    /* 0x00491980 12 bytes, "; %s%08X" */
    0x7325203b,
    0x58383025,
    0x00000000,
    /* 0x0049198c 16 bytes, a switch table (4 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    0,
    /* 0x0049199c 16 bytes, a switch table (4 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    0,
    /* 0x004919ac 16 bytes, a switch table (4 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    0,
    /* 0x004919bc 12 bytes, a switch table (3 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    /* 0x004919c8 644 bytes, a switch table (161 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    /* 0x00491c4c 644 bytes, a switch table (161 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    /* 0x00491ed0 52 bytes, a switch table (13 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    /* 0x00491f04 84 bytes, a switch table (21 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    /* 0x00491f58 24 bytes, a switch table (6 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    0,
    0,
    0,
    /* 0x00491f70 16 bytes, a switch table (4 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    0,
    /* 0x00491f80 36 bytes, a switch table (9 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    /* 0x00491fa4 16 bytes, a switch table (4 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    0,
    /* 0x00491fb4 76 bytes, a switch table (19 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    /* 0x00492000 28 bytes, a switch table (7 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    /* 0x0049201c 132 bytes, a switch table (33 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    /* 0x004920a0 96 bytes, a switch table (24 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    /* 0x00492100 44 bytes, a switch table (11 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    /* 0x0049212c 96 bytes, a switch table (24 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    /* 0x0049218c 84 bytes, a switch table (21 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    /* 0x004921e0 52 bytes, a switch table (13 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    /* 0x00492214 16 bytes, a switch table (4 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    0,
    /* 0x00492224 16 bytes, a switch table (4 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    0,
    /* 0x00492234 76 bytes, a switch table (19 cases; zeros: the compiler makes its own) */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
};

/* The handler of extended opcode XO in TABLE (COUNT entries, sorted), by a binary search; none: invalid. */
static void PPCDis_Dispatch(DisExtended *table, long count, unsigned long xo, long unused, unsigned long insn,
                            long pc, char *mnemonic) {
    long low = 0;
    long high = count - 1;
    long middle;
    unsigned char handler;

    do {
        middle = (low + high) / 2;
        if (xo == table[middle].xo) {
            handler = table[middle].handler;
            goto found;
        }
        if (table[middle].xo < xo)
            low = middle + 1;
        else
            high = middle - 1;
    } while (low <= high);
    dis_flags = 0;
    handler = 19;
found:
    ((DisHandler)data_00490ec0[handler])(unused, insn, pc, mnemonic);
}

/* The callback's text for an operand of KIND with VALUE (REG 0: the original passes an uninitialised word for the
   kinds below except 6), else NULL. */
static char *A_Callback(unsigned char kind, long value, short reg) {
    if (!dis_proc)
        return 0;
    return dis_proc(dis_pc, (long)dis_instruction, dis_base, kind, value, reg, dis_operandText);
}

/* An immediate operand (kind 3): the callback's text, else VALUE in decimal. */
static void A_AddImmediate(unsigned long value) {
    char *text = A_Callback(3, (long)value, 0);

    if (!text) {
        sprintf(data_0049add9, data_00490f4c, value);
        text = data_0049add9;
    }
    PPCDis_AddOperand(text);
}

/* A branch's target operand, DISPLACEMENT from the branch at PC (absolute when ABSOLUTE): the callback's (kind 4,
   the address; kind 8, the target), else 0x%08X or $%+d; the comment gets the target. */
static void A_AddTarget(long displacement, long pc, int absolute) {
    char *text;
    long target;

    if (absolute) {
        text = A_Callback(4, displacement, 0);
        if (!text) {
            sprintf(data_0049ad39, data_00490f24, displacement);
            text = data_0049ad39;
        }
        PPCDis_AddOperand(text);
        target = displacement;
    } else {
        text = A_Callback(8, displacement + dis_base + (long)dis_instruction, 0);
        if (!text) {
            sprintf(data_0049ad79, data_00490f2c, displacement);
            text = data_0049ad79;
        }
        PPCDis_AddOperand(text);
        target = displacement + pc;
    }
    sprintf(dis_comment, data_00491980, dis_hexPrefix, target);
}

/* An XO-form instruction's entry (data_004905ac). */
typedef struct A_XOForm {
    unsigned short xo;        /* bits 1-9 */
    short form;               /* 1, 2: rD,rA,rB; 3: rD,rA */
    char *name;
    unsigned long processors; /* none of dis_processors: invalid */
    unsigned long flags;      /* 0x40000000: 64-bit (options 0x40000000); & 6 to dis_flags */
} A_XOForm;

/* The mnemonic NAME, with "." when the instruction's Rc bit is set. */
static void B_Mnemonic(char *mnemonic, unsigned long insn, char *name) {
    sprintf(mnemonic, (insn & 1) ? data_00490fe0 : data_00490fe4, name);
}

/* An immediate operand: the callback's text (kind 3), else VALUE in decimal. */
static void B_Number(long value) {
    char *text;

    if (!dis_proc || !(text = dis_proc(dis_pc, (long)dis_instruction, dis_base, 3, value, 0, dis_operandText))) {
        sprintf(data_0049add9, data_00490f4c, value);
        text = data_0049add9;
    }
    PPCDis_AddOperand(text);
}

/* "NAME ra,rs" followed by the immediates N and B (two operands) or N alone (COUNT 1). */
static void B_Extended(unsigned long insn, char *mnemonic, char *name, unsigned long ra, unsigned long rs, int count,
                       long n, long b) {
    B_Mnemonic(mnemonic, insn, name);
    PPCDis_AddOperand(PPCDis_Register(ra));
    PPCDis_AddOperand(PPCDis_Register(rs));
    B_Number(n);
    if (count > 1)
        B_Number(b);
}

/* An A-form floating point instruction (data_00490804 for opcode 59, data_004908fc for opcode 63; data_0048e378
   when none): its extended opcode (bits 1-5), its operands' form, its name, the processors it is valid on and its
   flags (0x40000000: valid only with options 0x40000000; & 6 added to dis_flags). */
typedef struct C_AForm {
    unsigned short xo;
    short form;
    char *name;
    unsigned long processors;
    unsigned long flags;
} C_AForm;

/* Appends floating point register REG: the callback's text (kind 1), else fpN; "????" for a number over 31. */
static void C_AddFPRegister(unsigned long reg) {
    char *text;

    if (reg > 31) {
        sprintf(data_0049ae79, "????");
        text = data_0049ae79;
    } else {
        text = 0;
        if (dis_proc)
            text = dis_proc(dis_pc, (long)dis_instruction, dis_base, 1, (long)reg, 0, dis_operandText);
        if (!text) {
            sprintf(data_0049ae79, "fp%d", reg);
            text = data_0049ae79;
        }
    }
    PPCDis_AddOperand(text);
}

/* A special or device control register with a mnemonic of its own (mfxer, mtdmacr0, ...): NUMBER, valid when
   TEST holds of dis_processors & MASK, with general register rD/rS as operand when OPERAND is set. The first
   entry of the number whose test holds is taken. */
typedef struct C_Register {
    short number;
    char test;      /* C_ALWAYS, C_SOME (& MASK nonzero), C_NONE (& MASK zero) */
    char operand;
    unsigned long mask;
    char *name;
} C_Register;

#define C_ALWAYS 0
#define C_SOME 1
#define C_NONE 2

static C_Register C_mfspr[] = {
    {0x001, C_ALWAYS, 1, 0, "mfxer"},
    {0x008, C_ALWAYS, 1, 0, "mflr"},
    {0x009, C_ALWAYS, 1, 0, "mfctr"},
    {0x012, C_NONE, 1, 0x60, "mfdsisr"},
    {0x013, C_NONE, 1, 0x60, "mfdar"},
    {0x016, C_NONE, 1, 0x60, "mfdec"},
    {0x019, C_NONE, 1, 0x3e0, "mfsdr1"},
    {0x01a, C_ALWAYS, 1, 0, "mfsrr0"},
    {0x01b, C_ALWAYS, 1, 0, "mfsrr1"},
    {0x100, C_NONE, 1, 0xfff6ffff, "mfvrsave"},
    {0x110, C_SOME, 1, 0x60, "mfsprg0"},
    {0x111, C_SOME, 1, 0x60, "mfsprg1"},
    {0x112, C_SOME, 1, 0x60, "mfsprg2"},
    {0x113, C_SOME, 1, 0x60, "mfsprg3"},
    {0x118, C_NONE, 1, 0xffffdfff, "mfasr"},
    {0x11a, C_NONE, 1, 0x380, "mfear"},
    {0x11f, C_ALWAYS, 1, 0, "mfpvr"},
    {0x3b0, C_SOME, 1, 0x40, "mfzpr"},
    {0x3b1, C_SOME, 1, 0x40, "mfpid"},
    {0x3b9, C_SOME, 1, 0x60, "mfsgr"},
    {0x3ba, C_SOME, 1, 0x60, "mfdcwr"},
    {0x3bb, C_SOME, 1, 0x20, "mfsler"},
    {0x3cc, C_SOME, 1, 0x60, "mftbhu"},
    {0x3cd, C_SOME, 1, 0x60, "mftblu"},
    {0x3d3, C_SOME, 1, 0x60, "mficdbdr"},
    {0x3d4, C_SOME, 1, 0x60, "mfesr"},
    {0x3d5, C_SOME, 1, 0x60, "mfdear"},
    {0x3d6, C_SOME, 1, 0x60, "mfevpr"},
    {0x3d7, C_SOME, 1, 0x60, "mfcdbcr"},
    {0x3d8, C_SOME, 1, 0x60, "mftsr"},
    {0x3da, C_SOME, 1, 0x60, "mftcr"},
    {0x3db, C_SOME, 1, 0x60, "mfpit"},
    {0x3dc, C_SOME, 1, 0x60, "mftbhi"},
    {0x3dd, C_SOME, 1, 0x60, "mftblo"},
    {0x3de, C_SOME, 1, 0x60, "mfsrr2"},
    {0x3df, C_SOME, 1, 0x60, "mfsrr3"},
    {0x3f0, C_SOME, 1, 0x60, "mfdbsr"},
    {0x3f2, C_SOME, 1, 0x60, "mfdbcr"},
    {0x3f4, C_SOME, 1, 0x60, "mfiac1"},
    {0x3f5, C_SOME, 1, 0x40, "mfiac2"},
    {0x3f6, C_SOME, 1, 0x60, "mfdac1"},
    {0x3f7, C_SOME, 1, 0x40, "mfdac2"},
    {0x3fa, C_SOME, 1, 0x60, "mfdccr"},
    {0x3fb, C_SOME, 1, 0x60, "mficcr"},
    {0x3fc, C_SOME, 1, 0x40, "mfpbl1"},
    {0x3fd, C_SOME, 1, 0x60, "INST_403"},
    {0x3fe, C_SOME, 1, 0x40, "mfpbl2"},
    {0x3ff, C_SOME, 1, 0x40, "mfpbu2"},
};

static C_Register C_mtspr[] = {
    {0x001, C_ALWAYS, 1, 0, "mtxer"},
    {0x008, C_ALWAYS, 1, 0, "mtlr"},
    {0x009, C_ALWAYS, 1, 0, "mtctr"},
    {0x012, C_NONE, 1, 0x60, "mtdsisr"},
    {0x013, C_NONE, 1, 0x60, "mtdar"},
    {0x016, C_NONE, 1, 0x60, "mtdec"},
    {0x019, C_NONE, 1, 0x3e0, "mtsdr1"},
    {0x01a, C_ALWAYS, 1, 0, "mtsrr0"},
    {0x01b, C_ALWAYS, 1, 0, "mtsrr1"},
    {0x100, C_NONE, 1, 0xfff6ffff, "mtvrsave"},
    {0x110, C_SOME, 1, 0x60, "mtsprg0"},
    {0x111, C_SOME, 1, 0x60, "mtsprg1"},
    {0x112, C_SOME, 1, 0x60, "mtsprg2"},
    {0x113, C_SOME, 1, 0x60, "mtsprg3"},
    {0x118, C_NONE, 1, 0xffffdfff, "mtasr"},
    {0x11a, C_NONE, 1, 0x380, "mtear"},
    {0x11c, C_NONE, 1, 0xfff00062, "mttbl"},
    {0x11d, C_NONE, 1, 0xfff00062, "mttbu"},
    {0x3b0, C_SOME, 1, 0x40, "mtzpr"},
    {0x3b1, C_SOME, 1, 0x40, "mtpid"},
    {0x3b9, C_SOME, 1, 0x60, "mtsgr"},
    {0x3ba, C_SOME, 1, 0x60, "mtdcwr"},
    {0x3bb, C_SOME, 1, 0x20, "mtsler"},
    {0x3d3, C_SOME, 1, 0x60, "mticdbdr"},
    {0x3d4, C_SOME, 1, 0x60, "mtesr"},
    {0x3d5, C_SOME, 1, 0x60, "mtdear"},
    {0x3d6, C_SOME, 1, 0x60, "mtevpr"},
    {0x3d7, C_SOME, 1, 0x60, "mtcdbcr"},
    {0x3d8, C_SOME, 1, 0x60, "mttsr"},
    {0x3da, C_SOME, 1, 0x60, "mttcr"},
    {0x3db, C_SOME, 1, 0x60, "mtpit"},
    {0x3dc, C_SOME, 1, 0x60, "mttbhi"},
    {0x3dd, C_SOME, 1, 0x60, "mttblo"},
    {0x3de, C_SOME, 1, 0x60, "mtsrr2"},
    {0x3df, C_SOME, 1, 0x60, "mtsrr3"},
    {0x3f0, C_SOME, 1, 0x60, "mtdbsr"},
    {0x3f2, C_SOME, 1, 0x60, "mtdbcr"},
    {0x3f4, C_SOME, 1, 0x60, "mtiac1"},
    {0x3f5, C_SOME, 1, 0x40, "mtiac2"},
    {0x3f6, C_SOME, 1, 0x60, "mtdac1"},
    {0x3f7, C_SOME, 1, 0x40, "mtdac2"},
    {0x3fa, C_SOME, 1, 0x60, "mtdccr"},
    {0x3fb, C_SOME, 1, 0x60, "mticcr"},
    {0x3fc, C_SOME, 1, 0x40, "mtpbl1"},
    {0x3fd, C_SOME, 1, 0x40, "mtpbu1"},
    {0x3fe, C_SOME, 1, 0x40, "mtpbl2"},
    {0x3ff, C_SOME, 1, 0x40, "mtpbu2"},
};

/* The 403/405's device control registers (the bank registers br0-br7, brcr0-brcr7, besr and besr0 without an
   operand, as the original). */
static C_Register C_mfdcr[] = {
    {0x40, C_SOME, 1, 0x40, "mfexisr"},
    {0x42, C_SOME, 1, 0x40, "mfexier"},
    {0x80, C_SOME, 0, 0x40, "mfbr0"},
    {0x80, C_SOME, 0, 0x20, "mfbrcr0"},
    {0x81, C_SOME, 0, 0x40, "mfbr1"},
    {0x81, C_SOME, 0, 0x20, "mfbrcr1"},
    {0x82, C_SOME, 0, 0x40, "mfbr2"},
    {0x82, C_SOME, 0, 0x20, "mfbrcr2"},
    {0x83, C_SOME, 0, 0x40, "mfbr3"},
    {0x83, C_SOME, 0, 0x20, "mfbrcr3"},
    {0x84, C_SOME, 0, 0x40, "mfbr4"},
    {0x84, C_SOME, 0, 0x20, "mfbrcr4"},
    {0x85, C_SOME, 0, 0x40, "mfbr5"},
    {0x85, C_SOME, 0, 0x20, "mfbrcr5"},
    {0x86, C_SOME, 0, 0x40, "mfbr6"},
    {0x86, C_SOME, 0, 0x20, "mfbrcr6"},
    {0x87, C_SOME, 0, 0x40, "mfbr7"},
    {0x87, C_SOME, 0, 0x20, "mfbrcr7"},
    {0x90, C_SOME, 1, 0x60, "mfbear"},
    {0x91, C_SOME, 0, 0x40, "mfbesr"},
    {0x91, C_SOME, 0, 0x20, "mfbesr0"},
    {0xa0, C_SOME, 1, 0x60, "mfiocr"},
    {0xa1, C_SOME, 1, 0x20, "mfpmcr0"},
    {0xc0, C_SOME, 1, 0x40, "mfdmacr0"},
    {0xc1, C_SOME, 1, 0x40, "mfdmact0"},
    {0xc2, C_SOME, 1, 0x40, "mfdmada0"},
    {0xc3, C_SOME, 1, 0x40, "mfdmasa0"},
    {0xc4, C_SOME, 1, 0x40, "mfdmacc0"},
    {0xc8, C_SOME, 1, 0x40, "mfdmacr1"},
    {0xc9, C_SOME, 1, 0x40, "mfdmact1"},
    {0xca, C_SOME, 1, 0x40, "mfdmada1"},
    {0xcb, C_SOME, 1, 0x40, "mfdmasa1"},
    {0xcc, C_SOME, 1, 0x40, "mfdmacc1"},
    {0xd0, C_SOME, 1, 0x40, "mfdmacr2"},
    {0xd1, C_SOME, 1, 0x40, "mfdmact2"},
    {0xd2, C_SOME, 1, 0x40, "mfdmada2"},
    {0xd3, C_SOME, 1, 0x40, "mfdmasa2"},
    {0xd4, C_SOME, 1, 0x40, "mfdmacc2"},
    {0xd8, C_SOME, 1, 0x40, "mfdmacr3"},
    {0xd9, C_SOME, 1, 0x40, "mfdmact3"},
    {0xda, C_SOME, 1, 0x40, "mfdmada3"},
    {0xdb, C_SOME, 1, 0x40, "mfdmasa3"},
    {0xdc, C_SOME, 1, 0x40, "mfdmacc3"},
    {0xe0, C_SOME, 1, 0x40, "mfdmasr"},
};

static C_Register C_mtdcr[] = {
    {0x40, C_SOME, 1, 0x40, "mtexisr"},
    {0x42, C_SOME, 1, 0x40, "mtexier"},
    {0x80, C_SOME, 0, 0x40, "mtbr0"},
    {0x80, C_SOME, 0, 0x20, "mtbrcr0"},
    {0x81, C_SOME, 0, 0x40, "mtbr1"},
    {0x81, C_SOME, 0, 0x20, "mtbrcr1"},
    {0x82, C_SOME, 0, 0x40, "mtbr2"},
    {0x82, C_SOME, 0, 0x20, "mtbrcr2"},
    {0x83, C_SOME, 0, 0x40, "mtbr3"},
    {0x83, C_SOME, 0, 0x20, "mtbrcr3"},
    {0x84, C_SOME, 0, 0x40, "mtbr4"},
    {0x84, C_SOME, 0, 0x20, "mtbrcr4"},
    {0x85, C_SOME, 0, 0x40, "mtbr5"},
    {0x85, C_SOME, 0, 0x20, "mtbrcr5"},
    {0x86, C_SOME, 0, 0x40, "mtbr6"},
    {0x86, C_SOME, 0, 0x20, "mtbrcr6"},
    {0x87, C_SOME, 0, 0x40, "mtbr7"},
    {0x87, C_SOME, 0, 0x20, "mtbrcr7"},
    {0x90, C_SOME, 1, 0x60, "mtbear"},
    {0x91, C_SOME, 0, 0x40, "mtbesr"},
    {0x91, C_SOME, 0, 0x20, "mtbesr0"},
    {0xa0, C_SOME, 1, 0x60, "mtiocr"},
    {0xa1, C_SOME, 1, 0x20, "mtpmcr0"},
    {0xc0, C_SOME, 1, 0x40, "mtdmacr0"},
    {0xc1, C_SOME, 1, 0x40, "mtdmact0"},
    {0xc2, C_SOME, 1, 0x40, "mtdmada0"},
    {0xc3, C_SOME, 1, 0x40, "mtdmasa0"},
    {0xc4, C_SOME, 1, 0x40, "mtdmacc0"},
    {0xc8, C_SOME, 1, 0x40, "mtdmacr1"},
    {0xc9, C_SOME, 1, 0x40, "mtdmact1"},
    {0xca, C_SOME, 1, 0x40, "mtdmada1"},
    {0xcb, C_SOME, 1, 0x40, "mtdmasa1"},
    {0xcc, C_SOME, 1, 0x40, "mtdmacc1"},
    {0xd0, C_SOME, 1, 0x40, "mtdmacr2"},
    {0xd1, C_SOME, 1, 0x40, "mtdmact2"},
    {0xd2, C_SOME, 1, 0x40, "mtdmada2"},
    {0xd3, C_SOME, 1, 0x40, "mtdmasa2"},
    {0xd4, C_SOME, 1, 0x40, "mtdmacc2"},
    {0xd8, C_SOME, 1, 0x40, "mtdmacr3"},
    {0xd9, C_SOME, 1, 0x40, "mtdmact3"},
    {0xda, C_SOME, 1, 0x40, "mtdmada3"},
    {0xdb, C_SOME, 1, 0x40, "mtdmasa3"},
    {0xdc, C_SOME, 1, 0x40, "mtdmacc3"},
    {0xe0, C_SOME, 1, 0x40, "mtdmasr"},
};

#define C_COUNT(table) (sizeof(table) / sizeof((table)[0]))

/* With extended mnemonics, the mnemonic of register NUMBER in TABLE (COUNT entries) valid for dis_processors, and
   general register REG as its operand when it has one; 0 when there is none. */
static int C_NamedRegister(C_Register *table, unsigned long count, long number, unsigned long reg, char *mnemonic) {
    unsigned long i;
    unsigned long bits;

    if (!dis_extended)
        return 0;
    for (i = 0; i < count; i++) {
        if (table[i].number != number)
            continue;
        bits = dis_processors & table[i].mask;
        if (table[i].test == C_SOME && !bits)
            continue;
        if (table[i].test == C_NONE && bits)
            continue;
        sprintf(mnemonic, "%s", table[i].name);
        if (table[i].operand)
            PPCDis_AddOperand(PPCDis_Register(reg));
        return 1;
    }
    return 0;
}

/* Special register NUMBER's text: the callback's (kind 7, with extended mnemonics only), else sprN. */
static char *C_SpecialRegister(long number) {
    char *text = 0;

    if (dis_extended && dis_proc)
        text = dis_proc(dis_pc, (long)dis_instruction, dis_base, 7, number, 0, dis_operandText);
    if (!text) {
        sprintf(data_0049ae39, "spr%d", number);
        text = data_0049ae39;
    }
    return text;
}

/* Device control register NUMBER's text: the callback's (kind 12, with extended mnemonics only), else dcrN. */
static char *C_DeviceControlRegister(long number) {
    char *text = 0;

    if (dis_extended && dis_proc)
        text = dis_proc(dis_pc, (long)dis_instruction, dis_base, 12, number, 0, dis_operandText);
    if (!text) {
        sprintf(data_0049acf9, "dcr%d", number);
        text = data_0049acf9;
    }
    return text;
}

/* An entry of the opcode-19 and opcode-4 tables (16 bytes): the extended opcode, the operands' format, the
   mnemonic, the processors it is valid for and its flags (0x40000000: only with options 0x40000000; & 0xe, or
   & 6, to dis_flags). */
typedef struct D_Opcode {
    unsigned short xo;
    unsigned short format;
    unsigned long name;         /* char * */
    unsigned long processors;
    unsigned long flags;
} D_Opcode;

/* The entry of XO in TABLE (entries 0 to HIGH, sorted; the original's bounds, whose last entry lies past the
   table, on bytes greater than any XO), by a binary search; none: 0. */
static D_Opcode *D_Find(D_Opcode *table, long high, unsigned long xo) {
    long low = 0;
    long middle;

    do {
        middle = (low + high) / 2;
        if (xo == table[middle].xo)
            return &table[middle];
        if (table[middle].xo < xo)
            low = middle + 1;
        else
            high = middle - 1;
    } while (low <= high);
    return 0;
}

/* Floating-point register REG's text: the callback's (kind 1), else fpN ("????" over 31). */
static char *D_FPRegister(unsigned long reg) {
    char *text;

    if (reg > 31) {
        sprintf(data_0049ae79, data_00490f68);
        return data_0049ae79;
    }
    if (dis_proc) {
        text = dis_proc(dis_pc, (long)dis_instruction, dis_base, 1, (long)reg, 0, dis_operandText);
        if (text)
            return text;
    }
    sprintf(data_0049ae79, data_00490f78, reg);
    return data_0049ae79;
}

/* An immediate's text: the callback's (kind 3), else %d. */
static char *D_Immediate(unsigned long value) {
    char *text;

    if (dis_proc) {
        text = dis_proc(dis_pc, (long)dis_instruction, dis_base, 3, (long)value, 0, dis_operandText);
        if (text)
            return text;
    }
    sprintf(data_0049add9, data_00490f4c, value);
    return data_0049add9;
}

/* An X-form instruction's description (16 bytes): data_0048e76c (opcode 31, 137 searched), data_0048f070 (opcode
   63, 18 searched), data_0048e378 (none found: no processors, so invalid). */
typedef struct E_XForm {
    unsigned short xo;          /* bits 1-10 */
    short form;                 /* the operands' layout (1-33) */
    char *name;
    unsigned long processors;   /* valid when it has one of dis_processors */
    unsigned long flags;        /* 0x40000000 64-bit (options 0x40000000), & 6 to dis_flags */
} E_XForm;

/* The entry of XO in TABLE by a binary search of COUNT entries (the original searches one past each table's end:
   the strings there never match an opcode); none: data_0048e378, with dis_flags 0. */
static E_XForm *E_Find(E_XForm *table, long count, unsigned long xo) {
    long low = 0;
    long high = count - 1;
    long middle;

    do {
        middle = (low + high) / 2;
        if (xo == table[middle].xo)
            return &table[middle];
        if (table[middle].xo < xo)
            low = middle + 1;
        else
            high = middle - 1;
    } while (low <= high);
    dis_flags = 0;
    return (E_XForm *)data_0048e378;
}

/* Appends general register REG. */
static void E_AddRegister(unsigned long reg) {
    PPCDis_AddOperand(PPCDis_Register(reg));
}

/* Appends floating-point register REG: the callback's text (kind 1), else fpN; "????" over 31. */
static void E_AddFloatRegister(unsigned long reg) {
    char *text;

    if (reg < 32) {
        if (dis_proc) {
            text = dis_proc(dis_pc, (long)dis_instruction, dis_base, 1, (long)reg, 0, dis_operandText);
            if (text) {
                PPCDis_AddOperand(text);
                return;
            }
        }
        sprintf(data_0049ae79, data_00490f78, reg);   /* "fp%d" */
    } else {
        sprintf(data_0049ae79, data_00490f68);        /* "????" */
    }
    PPCDis_AddOperand(data_0049ae79);
}

/* Appends vector register REG: the callback's text (kind 15), else vrN; "????" over 31. */
static void E_AddVectorRegister(unsigned long reg) {
    char *text;

    if (reg < 32) {
        if (dis_proc) {
            text = dis_proc(dis_pc, (long)dis_instruction, dis_base, 0xf, (long)reg, 0, dis_operandText);
            if (text) {
                PPCDis_AddOperand(text);
                return;
            }
        }
        sprintf(data_0049ae59, data_00490f70, reg);   /* "vr%d" */
    } else {
        sprintf(data_0049ae59, data_00490f68);        /* "????" */
    }
    PPCDis_AddOperand(data_0049ae59);
}

/* Appends VALUE in decimal: the callback's text (kind 3), else "%d". */
static void E_AddDecimal(unsigned long value) {
    char *text = 0;

    if (dis_proc)
        text = dis_proc(dis_pc, (long)dis_instruction, dis_base, 3, (long)value, 0, dis_operandText);
    if (!text) {
        sprintf(data_0049add9, data_00490f4c, value);  /* "%d" */
        text = data_0049add9;
    }
    PPCDis_AddOperand(text);
}

/* Appends VALUE in hex: the callback's text (kind 2), else the prefix and 4 digits (8 over 0xffff). */
static void E_AddHex(unsigned long value) {
    char *text = 0;

    if (dis_proc)
        text = dis_proc(dis_pc, (long)dis_instruction, dis_base, 2, (long)value, 0, dis_operandText);
    if (!text) {
        if (value & 0xffff0000)
            sprintf(data_0049adb9, data_00490f44, dis_hexPrefix, value);   /* "%s%08x" */
        else
            sprintf(data_0049adb9, data_00490f3c, dis_hexPrefix, value);   /* "%s%04x" */
        text = data_0049adb9;
    }
    PPCDis_AddOperand(text);
}

/* Appends the text "%d" of VALUE (no callback). */
static void E_AddNumber(unsigned long value) {
    char text[32];

    sprintf(text, data_00490f4c, value);   /* "%d" */
    PPCDis_AddOperand(text);
}

/* The comment of VALUE's two low bytes as characters ("; 'xy'", in the instruction's byte order) when both are
   printable, else none. */
static void E_Comment(unsigned long value) {
    unsigned char first;
    unsigned char second;

    if (dis_options & 0x80000000) {
        first = (unsigned char)value;
        second = (unsigned char)(value >> 8);
    } else {
        first = (unsigned char)(value >> 8);
        second = (unsigned char)value;
    }
    if ((__ctype_map[first] & 0xdc) && (__ctype_map[second] & 0xdc))
        sprintf(data_0049ad19, data_00490f18, (int)(signed char)first, (int)(signed char)second);   /* "; '%c%c'" */
    else
        data_0049ad19[0] = 0;
    strcpy(dis_comment, data_0049ad19);
}

/* The mnemonic NAME, with "." when the instruction's Rc bit is set. */
static void E_Mnemonic(char *mnemonic, unsigned long insn, char *name) {
    sprintf(mnemonic, (insn & 1) ? data_00490fe0 : data_00490fe4, name);   /* "%s." : "%s" */
}

/* The trap's extended mnemonic for TO (tw), or 0. */
static char *E_TrapName(unsigned long to) {
    switch (to) {
    case 1:
        return data_00491808;   /* "twlgt" */
    case 2:
        return data_00491810;   /* "twllt" */
    case 4:
        return data_00491818;   /* "tweq" */
    case 5:
        return data_00491820;   /* "twlge" */
    case 6:
        return data_00491828;   /* "twlle" */
    case 8:
        return data_00491830;   /* "twgt" */
    case 12:
        return data_00491838;   /* "twge" */
    case 16:
        return data_00491840;   /* "twlt" */
    case 20:
        return data_00491848;   /* "twle" */
    case 24:
        return data_00491850;   /* "twne" */
    }
    return 0;
}

/* data_0048e0ec, the forms of the primary opcodes (through a pointer: indexed directly in the switch, the compiler
   loses the offset into PPCDis_Data). */
static signed char *F_forms = (signed char *)data_0048e0ec;

/* An immediate in decimal (the callback's kind 3). */
static char *F_Decimal(long value) {
    char *text;

    if (dis_proc) {
        text = dis_proc(dis_pc, (long)dis_instruction, dis_base, 3, value, 0, dis_operandText);
        if (text)
            return text;
    }
    sprintf(data_0049add9, "%d", value);
    return data_0049add9;
}

/* An unsigned 16-bit immediate in hex (the callback's kind 2). */
static char *F_Hex(unsigned long value) {
    char *text;

    if (dis_proc) {
        text = dis_proc(dis_pc, (long)dis_instruction, dis_base, 2, (long)value, 0, dis_operandText);
        if (text)
            return text;
    }
    if (value & 0xffff0000)
        sprintf(data_0049adb9, "%s%08x", dis_hexPrefix, value);
    else
        sprintf(data_0049adb9, "%s%04x", dis_hexPrefix, value);
    return data_0049adb9;
}

/* An absolute address (a displacement from r0, the callback's kind 4). */
static char *F_Address(long value) {
    char *text;

    if (dis_proc) {
        text = dis_proc(dis_pc, (long)dis_instruction, dis_base, 4, value, 0, dis_operandText);
        if (text)
            return text;
    }
    sprintf(data_0049ad39, "0x%08X", value);
    return data_0049ad39;
}

/* A displacement DISPLACEMENT from general register BASE, d(rA) (the callback's kind 6). */
static char *F_Displacement(long displacement, unsigned long base) {
    char *text;

    if (dis_proc) {
        text = dis_proc(dis_pc, (long)dis_instruction, dis_base, 6, displacement, (short)base, dis_operandText);
        if (text)
            return text;
    }
    text = PPCDis_Register(base);
    sprintf(data_0049ad99, "%d(%s)", displacement, text);
    return data_0049ad99;
}

/* Floating-point register REG (the callback's kind 1), fpN. */
static char *F_Float(unsigned long reg) {
    char *text;

    if (reg > 31) {
        sprintf(data_0049ae79, "????");
        return data_0049ae79;
    }
    if (dis_proc) {
        text = dis_proc(dis_pc, (long)dis_instruction, dis_base, 1, (long)reg, 0, dis_operandText);
        if (text)
            return text;
    }
    sprintf(data_0049ae79, "fp%d", reg);
    return data_0049ae79;
}

/* The comment "; 'cc'" with the two bytes of a 16-bit immediate VALUE as characters (in memory order) when both are
   printable, else none. */
static void F_Comment(unsigned long value) {
    unsigned char first;
    unsigned char second;

    if (dis_options & 0x80000000) {
        first = (unsigned char)value;
        second = (unsigned char)(value >> 8);
    } else {
        first = (unsigned char)(value >> 8);
        second = (unsigned char)value;
    }
    if ((__ctype_map[first] & 0xdc) && (__ctype_map[second] & 0xdc))
        sprintf(data_0049ad19, "; '%c%c'", (int)(char)first, (int)(char)second);
    else
        data_0049ad19[0] = 0;
    strcpy(dis_comment, data_0049ad19);
}

static void PPCDis_Dispatch(DisExtended *table, long count, unsigned long xo, long unused, unsigned long insn,
                            long pc, char *mnemonic);
static char *A_Callback(unsigned char kind, long value, short reg);
static void A_AddImmediate(unsigned long value);
static void A_AddTarget(long displacement, long pc, int absolute);
static void B_Mnemonic(char *mnemonic, unsigned long insn, char *name);
static void B_Number(long value);
static void B_Extended(unsigned long insn, char *mnemonic, char *name, unsigned long ra, unsigned long rs, int count,
                       long n, long b);
static void C_AddFPRegister(unsigned long reg);
static int C_NamedRegister(C_Register *table, unsigned long count, long number, unsigned long reg, char *mnemonic);
static char *C_SpecialRegister(long number);
static char *C_DeviceControlRegister(long number);
static D_Opcode *D_Find(D_Opcode *table, long high, unsigned long xo);
static char *D_FPRegister(unsigned long reg);
static char *D_Immediate(unsigned long value);
static E_XForm *E_Find(E_XForm *table, long count, unsigned long xo);
static void E_AddRegister(unsigned long reg);
static void E_AddFloatRegister(unsigned long reg);
static void E_AddVectorRegister(unsigned long reg);
static void E_AddDecimal(unsigned long value);
static void E_AddHex(unsigned long value);
static void E_AddNumber(unsigned long value);
static void E_Comment(unsigned long value);
static void E_Mnemonic(char *mnemonic, unsigned long insn, char *name);
static char *E_TrapName(unsigned long to);
static char *F_Decimal(long value);
static char *F_Hex(unsigned long value);
static char *F_Address(long value);
static char *F_Displacement(long displacement, unsigned long base);
static char *F_Float(unsigned long reg);
static void F_Comment(unsigned long value);

/* Opcode 18: b, bl, ba, bla. */
/* 0x0046b0c0 */
void PPCDis_Branch(long unused, unsigned long insn, long pc, char *mnemonic) {
    long displacement = ((long)(insn << 6) >> 8) * 4;

    sprintf(mnemonic, data_00490fe4, (char *)data_0048e078[insn & 3]);
    dis_flags |= 8;
    A_AddTarget(displacement, pc, (int)(insn >> 1 & 1));
}

/* Opcode 16: bc, bcl, bca, bcla, or (extended mnemonics on) bdnzf, bdzf, blt..., bdnzt, bdzt, bdnz, bdz with
   l/a/la and +/- for the hint. */
/* 0x0046a7c0 */
void PPCDis_BranchConditional(long unused, unsigned long insn, long pc, char *mnemonic) {
    char buf[48];
    int extended = 0;
    long displacement = ((short)insn >> 2) * 4;
    unsigned long bo = insn >> 21 & 0x1f;
    unsigned long bi = insn >> 16 & 0x1f;
    unsigned long kind = bo & 0x1e;
    char *format;

    if (dis_extended) {
        extended = 1;
        switch (kind) {
        case 0:
            strcpy(buf, data_00491948);
            break;
        case 2:
            strcpy(buf, data_00491950);
            break;
        case 4:
        case 0xc:
            /* blt, bgt, beq, bso (true) or bge, ble, bne, bns (false) */
            strcpy(buf, (char *)data_0048e0cc[(kind == 0xc ? 0 : 4) + (bi & 3)]);
            break;
        case 8:
            strcpy(buf, data_00491958);
            break;
        case 10:
            strcpy(buf, data_00491960);
            break;
        case 0x10:
            strcpy(buf, data_00491968);
            break;
        case 0x12:
            strcpy(buf, data_00491970);
            break;
        default:
            extended = 0;
            break;
        }
    }
    if (extended) {
        switch (insn & 3) {
        case 1:
            strcat(buf, data_00491760);
            break;
        case 2:
            strcat(buf, data_00491974);
            break;
        case 3:
            strcat(buf, data_00491978);
            break;
        }
        if (bo & 1)
            strcat(buf, displacement < 0 ? data_0049197c : data_0049175c);
        sprintf(mnemonic, data_00490fe4, buf);
        switch (kind) {
        case 0:
        case 2:
        case 8:
        case 10:
            if (bi < 4)
                sprintf(buf, data_00490fe4, (char *)data_0048dff8[bi & 3]);
            else
                sprintf(buf, data_00491800, bi >> 2, (char *)data_0048dff8[bi & 3]);
            PPCDis_AddOperand(buf);
            break;
        case 4:
        case 0xc:
            if (bi > 3) {
                sprintf(data_0049adf9, data_00490f50, bi >> 2);
                PPCDis_AddOperand(data_0049adf9);
            }
            break;
        }
    } else {
        sprintf(data_0049adf9, data_00490f50, bi >> 2);
        sprintf(mnemonic, data_00490fe4, (char *)data_0048e09c[insn & 3]);
        switch (kind) {
        case 0:
            sprintf(buf, data_0049176c, data_0049adf9, (char *)data_0048e018[bi & 3]);
            break;
        case 2:
            sprintf(buf, data_00491784, data_0049adf9, (char *)data_0048e018[bi & 3]);
            break;
        case 4:
            sprintf(buf, data_00491798, data_0049adf9, (char *)data_0048dfe8[bi & 3]);
            break;
        case 8:
            sprintf(buf, data_0049176c, data_0049adf9, (char *)data_0048dff8[bi & 3]);
            break;
        case 10:
            sprintf(buf, data_00491784, data_0049adf9, (char *)data_0048dff8[bi & 3]);
            break;
        case 0xc:
            sprintf(buf, data_004917a8, data_0049adf9, (char *)data_0048dfe8[bi & 3]);
            break;
        case 0x10:
        case 0x12:
            /* The original passes its "cr%d" buffer to the %d: the number printed is that buffer's address in
               the original (0x0049adf9), not the condition register. */
            format = kind == 0x10 ? data_004917b4 : data_004917c4;
            sprintf(buf, format, 0x0049adf9L, bi);
            break;
        case 0x14:
            sprintf(buf, data_004917d4, bi);
            break;
        default:
            sprintf(buf, data_004917e0, bo, bi);
            break;
        }
        PPCDis_AddOperand(buf);
    }
    dis_flags |= 8;
    A_AddTarget(displacement, pc, (int)(insn >> 1 & 1));
}

/* Opcode 17: sc (no operands). */
/* 0x0046a7a0 */
void PPCDis_SystemCall(long unused, unsigned long insn, long pc, char *mnemonic) {
    sprintf(mnemonic, data_00490fe4, data_00491944);
}

/* The D-form primary opcodes (by data_0048e260's mnemonics and data_0048e0ec's forms): tdi/twi (and twi's extended
   mnemonics), mulli, subfic, dozi (options 2), cmpli/cmpi (cmplwi/cmpwi), addic, addic., addi, addis (li, lis, subi...),
   ori, oris, xori, xoris, andi., andis. (nop), the integer loads and stores, lmw, stmw, the floating loads and stores
   (processors 0xffc1f) and psq_l, psq_lu, psq_st, psq_stu (Gekko, processors 0x20000). */
/* 0x00467950 */
void PPCDis_DForm(long unused, unsigned long insn, long pc, char *mnemonic) {
    unsigned long opcode = insn >> 26;
    unsigned long rD = insn >> 21 & 0x1f;      /* rD, rS, frD, TO, crfD|L */
    unsigned long rA = insn >> 16 & 0x1f;
    long simm = (short)insn;
    unsigned long uimm = insn & 0xffff;
    unsigned long w = insn >> 15 & 1;          /* psq: W, I and the 12-bit displacement */
    unsigned long i = insn >> 12 & 7;
    long d12 = (short)((short)insn << 4) >> 4;
    char *name;
    char text[32];
    signed char form;

    if (opcode > 0x3d)
        opcode = 0;
    if (!data_0048e260[opcode]) {
        dis_flags = 0;
        return;
    }
    if (opcode == 9 && !(dis_options & 2)) {
        dis_flags = 0;
        return;
    }
    if (opcode >= 0x30 && !(dis_processors & 0xffc1f)) {
        dis_flags = 0;
        return;
    }
    if (opcode >= 0x38 && !(dis_processors & 0x20000)) {
        dis_flags = 0;
        return;
    }

    if (dis_extended) {
        if (opcode == 3) {
            /* twi TO,rA,SIMM: tw<cond>i rA,SIMM */
            switch (rD) {
            case 1: name = data_004918bc; break;    /* twlgti */
            case 2: name = data_004918c4; break;    /* twllti */
            case 4: name = data_004918cc; break;    /* tweqi */
            case 5: name = data_004918d4; break;    /* twlgei */
            case 6: name = data_004918dc; break;    /* twllei */
            case 8: name = data_004918e4; break;    /* twgti */
            case 12: name = data_004918ec; break;   /* twgei */
            case 16: name = data_004918f4; break;   /* twlti */
            case 20: name = data_004918fc; break;   /* twlei */
            case 24: name = data_00491904; break;   /* twnei */
            default: name = 0; break;
            }
            if (name) {
                sprintf(mnemonic, "%s", name);
                PPCDis_AddOperand(PPCDis_Register(rA));
                PPCDis_AddOperand(F_Decimal(simm));
                F_Comment((unsigned long)simm);
                return;
            }
        } else if ((opcode == 11 || opcode == 10) && !(insn >> 21 & 1)) {
            /* cmpi/cmpli with L 0: cmpwi/cmplwi [crN,]rA,UIMM (in hex) */
            sprintf(mnemonic, "%s", opcode == 11 ? data_0049190c : data_00491914);
            if (insn >> 23 & 7) {
                sprintf(text, "cr%d", insn >> 23 & 7);
                PPCDis_AddOperand(text);
            }
            PPCDis_AddOperand(PPCDis_Register(rA));
            PPCDis_AddOperand(F_Hex(uimm));
            return;
        }
    }

    sprintf(mnemonic, "%s", (char *)data_0048e260[opcode]);
    form = F_forms[opcode];
    switch (form) {
    case 0:
        dis_flags = 0;
        return;

    case 1:   /* integer loads, lmw: rD,d(rA), or rD,address from r0 */
    case 3:   /* integer stores, stmw */
        PPCDis_AddOperand(PPCDis_Register(rD));
        if (rA == 0)
            PPCDis_AddOperand(F_Address(simm));
        else
            PPCDis_AddOperand(F_Displacement(simm, rA));
        break;

    case 2:   /* mulli, subfic, dozi, addic, addic., addi, addis: rD,rA,SIMM */
        if (dis_extended && rA == 0 && (opcode == 14 || opcode == 15)) {
            /* li, lis rD,SIMM */
            sprintf(mnemonic, "%s", opcode == 14 ? data_0049191c : data_00491920);
            PPCDis_AddOperand(PPCDis_Register(rD));
            PPCDis_AddOperand(F_Decimal(simm));
            break;
        }
        if (dis_extended && simm < 0 && opcode >= 12 && opcode <= 15) {
            /* a negative immediate: subic, subic., subi, subis with -SIMM ("subi" over the first 4 characters) */
            strcpy(text, (char *)data_0048e260[opcode]);
            text[0] = data_00491924[0];
            text[1] = data_00491924[1];
            text[2] = data_00491924[2];
            text[3] = data_00491924[3];
            sprintf(mnemonic, "%s", text);
            simm = -simm;
        }
        PPCDis_AddOperand(PPCDis_Register(rD));
        PPCDis_AddOperand(PPCDis_Register(rA));
        PPCDis_AddOperand(F_Decimal(simm));
        F_Comment((unsigned long)simm);
        break;

    case 4:   /* ori, oris, xori, xoris, andi., andis.: rA,rS,UIMM (in hex); ori 0,0,0: nop */
        if (dis_extended && opcode == 24 && rD == 0 && rA == 0 && uimm == 0) {
            sprintf(mnemonic, "%s", data_0049192c);
        } else {
            PPCDis_AddOperand(PPCDis_Register(rA));
            PPCDis_AddOperand(PPCDis_Register(rD));
            PPCDis_AddOperand(F_Hex(uimm));
        }
        F_Comment(uimm);
        break;

    case 5:   /* cmpi crfD,L,rA,SIMM */
        sprintf(text, "cr%d,%d", insn >> 23 & 7, insn >> 21 & 1);
        PPCDis_AddOperand(text);
        PPCDis_AddOperand(PPCDis_Register(rA));
        PPCDis_AddOperand(F_Decimal(simm));
        break;

    case 6:   /* cmpli crfD,L,rA,UIMM (in hex) */
        sprintf(text, "cr%d,%d", insn >> 23 & 7, insn >> 21 & 1);
        PPCDis_AddOperand(text);
        PPCDis_AddOperand(PPCDis_Register(rA));
        PPCDis_AddOperand(F_Hex(uimm));
        break;

    case 7:   /* tdi, twi TO,rA,SIMM */
        PPCDis_AddOperand(F_Decimal((long)rD));
        PPCDis_AddOperand(PPCDis_Register(rA));
        PPCDis_AddOperand(F_Decimal(simm));
        F_Comment((unsigned long)simm);
        break;

    case 8:   /* lfs, lfd, stfs, stfd: frD,d(rA), or frD,address from r0 */
        PPCDis_AddOperand(F_Float(rD));
        if (rA == 0)
            PPCDis_AddOperand(F_Address(simm));
        else
            PPCDis_AddOperand(F_Displacement(simm, rA));
        break;

    case 9:   /* lfsu, lfdu, stfsu, stfdu: frD,d(rA) */
        PPCDis_AddOperand(F_Float(rD));
        PPCDis_AddOperand(F_Displacement(simm, rA));
        strcat(dis_comment, data_00491930);   /* "; POWER instruction" */
        break;

    case 10:  /* psq_l, psq_lu, psq_st, psq_stu frD,d(rA),W,I (12-bit d), or frD,address from r0,W,I */
        PPCDis_AddOperand(F_Float(rD));
        if (rA == 0)
            PPCDis_AddOperand(F_Address(d12));
        else
            PPCDis_AddOperand(F_Displacement(d12, rA));
        PPCDis_AddOperand(F_Decimal((long)w));
        PPCDis_AddOperand(F_Decimal((long)i));
        break;
    }
}

/* Opcodes 58 and 62 (64-bit, options 0x40000000 and processor 0x2000): ld, ldu, lwa by bits 0-1 (opcode 62's std,
   stdu are shown as these too). ld/lwa with rA 0: rD,0x%08X (an address, kind 4), else rD,d(rA) (kind 6). */
/* 0x004674f0 */
void PPCDis_LoadDoubleword(long unused, unsigned long insn, long pc, char *mnemonic) {
    unsigned long form = insn & 3;
    unsigned long rd;
    unsigned long ra;
    long displacement;
    char *text;

    if (!(dis_options & 0x40000000) || !(dis_processors & 0x2000) || form > 2) {
        dis_flags = 0;
        return;
    }
    rd = insn >> 21 & 0x1f;
    ra = insn >> 16 & 0x1f;
    displacement = ((short)insn >> 2) * 4;
    sprintf(mnemonic, data_00490fe4, (char *)data_0048e364[form]);
    PPCDis_AddOperand(PPCDis_Register(rd));
    if (form != 1 && ra == 0) {
        text = A_Callback(4, displacement, 0);
        if (!text) {
            sprintf(data_0049ad39, data_00490f24, displacement);
            text = data_0049ad39;
        }
    } else {
        text = A_Callback(6, (short)displacement, (short)ra);
        if (!text) {
            sprintf(data_0049ad99, data_00490f34, displacement, PPCDis_Register(ra));
            text = data_0049ad99;
        }
    }
    PPCDis_AddOperand(text);
}

/* X-form instructions of opcodes 31 and 63 by their description (data_0048e76c, data_0048f070): indexed loads and
   stores (integer, floating point, vector), logical and shift operations, compares, traps, segment registers,
   cache and TLB operations, data streams, FPSCR moves; with the extended mnemonics tw<cond>, not, mr, cmpw, cmplw,
   tlbwehi/lo and tlbrehi/lo (403), dstt, dststt, dssall. */
/* 0x00462f50 */
void PPCDis_XForm(long unused, unsigned long insn, long pc, char *mnemonic) {
    unsigned long xo = insn >> 1 & 0x3ff;
    unsigned long rd = insn >> 21 & 0x1f;   /* rD, rS, frD, TO, crbD */
    unsigned long ra = insn >> 16 & 0x1f;
    unsigned long rb = insn >> 11 & 0x1f;   /* rB, NB, SH, WS */
    unsigned long crfd = insn >> 23 & 7;
    E_XForm *entry;
    char *name;
    char text[32];

    if ((insn >> 26) == 31)
        entry = E_Find((E_XForm *)data_0048e76c, 0x89, xo);
    else
        entry = E_Find((E_XForm *)data_0048f070, 0x12, xo);
    if (!(entry->processors & dis_processors)) {
        dis_flags = 0;
        return;
    }
    if (!(dis_options & 0x40000000) && (entry->flags & 0x40000000)) {
        dis_flags = 0;
        return;
    }
    dis_flags |= (short)(entry->flags & 6);

    if (dis_extended) {
        if (xo == 4) {
            name = E_TrapName(rd);
            if (name) {
                sprintf(mnemonic, data_00490fe4, name);
                E_AddRegister(ra);
                E_AddRegister(rb);
                return;
            }
        } else if (xo == 0x7c && rd == rb) {
            E_Mnemonic(mnemonic, insn, data_00491858);   /* "not" */
            E_AddRegister(ra);
            E_AddRegister(rd);
            return;
        }
    }

    E_Mnemonic(mnemonic, insn, entry->name);
    if (entry->form == 9 && !(insn & 1)) {   /* stwcx., stdcx. only with Rc */
        dis_flags = 0;
        return;
    }

    switch (entry->form) {
    case 1:    /* rD,rA,rB: loads */
    case 2:
    case 9:
    case 10:   /* rS,rA,rB: stores */
        E_AddRegister(rd);
        E_AddRegister(ra);
        E_AddRegister(rb);
        break;
    case 3:    /* rD,rB: mfsrin, mtsrin */
    case 12:
        E_AddRegister(rd);
        E_AddRegister(rb);
        break;
    case 4:    /* frD,frB */
        E_AddFloatRegister(rd);
        E_AddFloatRegister(rb);
        break;
    case 5:    /* rD: mfcr, mfmsr, wrtee, mtmsr */
    case 13:
        E_AddRegister(rd);
        break;
    case 6:    /* frD: mffs */
        E_AddFloatRegister(rd);
        break;
    case 7:    /* rD,srN: mfsr */
        E_AddRegister(rd);
        sprintf(data_0049ae19, data_00490f58, insn >> 16 & 0xf);   /* "sr%d" */
        PPCDis_AddOperand(data_0049ae19);
        break;
    case 8:    /* rA,rS,rB; mr for or with rS = rB */
        if (dis_extended && entry->xo == 0x1bc && rd == rb) {
            E_Mnemonic(mnemonic, insn, data_0049185c);   /* "mr" */
            E_AddRegister(ra);
            E_AddRegister(rd);
        } else {
            E_AddRegister(ra);
            E_AddRegister(rd);
            E_AddRegister(rb);
        }
        break;
    case 11:   /* rA,rS */
        E_AddRegister(ra);
        E_AddRegister(rd);
        break;
    case 14:   /* srN,rS: mtsr */
        sprintf(data_0049ae19, data_00490f58, insn >> 16 & 0xf);   /* "sr%d" */
        PPCDis_AddOperand(data_0049ae19);
        E_AddRegister(rd);
        break;
    case 15:   /* rA,rS,SH */
        E_AddRegister(ra);
        E_AddRegister(rd);
        E_AddDecimal(rb);
        E_Comment(rb);
        break;
    case 16:   /* crfD,L,rA,rB: cmp, cmpl; cmpw, cmplw for cr0 and L 0 */
        if (dis_extended && xo == 0 && crfd == 0 && !(insn >> 21 & 1)) {
            strcpy(text, entry->name);
            strcpy(text + 3, data_00491860);   /* "w" */
            sprintf(mnemonic, data_00490fe4, text);
        } else if (dis_extended && xo == 0x20 && crfd == 0 && !(insn >> 21 & 1)) {
            strcpy(text, entry->name);
            strcpy(text + 3, data_00491864);   /* "lw" */
            sprintf(mnemonic, data_00490fe4, text);
        } else {
            sprintf(text, data_00491868, crfd, insn >> 21 & 1);   /* "cr%d,%d" */
            PPCDis_AddOperand(text);
        }
        E_AddRegister(ra);
        E_AddRegister(rb);
        break;
    case 17:   /* crfD,frA,frB: fcmpu, fcmpo */
        sprintf(text, data_00490f50, crfd);   /* "cr%d" */
        PPCDis_AddOperand(text);
        E_AddFloatRegister(ra);
        E_AddFloatRegister(rb);
        break;
    case 18:   /* mcrfs: the original takes crfS from bits 11-13 */
        sprintf(dis_operands, data_004917f4, crfd, insn >> 11 & 7);   /* "cr%d,cr%d" */
        break;
    case 19:   /* mcrxr */
        sprintf(dis_operands, data_00490f50, crfd);   /* "cr%d" */
        break;
    case 20:   /* mtfsfi */
        sprintf(dis_operands, data_00491870, crfd, insn >> 12 & 0xf);   /* "cr%d,0x%x" */
        break;
    case 21:   /* TO,rA,rB: tw, td */
        E_AddNumber(rd);
        E_AddRegister(ra);
        E_AddRegister(rb);
        break;
    case 22:   /* rA,rB: cache operations */
        E_AddRegister(ra);
        E_AddRegister(rb);
        break;
    case 24:   /* crbD: mtfsb0, mtfsb1 */
        E_AddNumber(rd);
        break;
    case 25:   /* rB: tlbie, slbie, tlbli; tlbld on the 403 alone is tlbwe */
        if (entry->xo == 0x3d2 && dis_processors == 0x40) {
            if (!dis_extended) {
                sprintf(mnemonic, data_00490fe4, data_0049188c);   /* "tlbwe" */
                E_AddRegister(rd);
                E_AddRegister(ra);
                E_AddNumber(rb);
                break;
            }
            if (rb == 0) {
                sprintf(mnemonic, data_00490fe4, data_0049187c);   /* "tlbwehi" */
            } else if (rb == 1) {
                sprintf(mnemonic, data_00490fe4, data_00491884);   /* "tlbwelo" */
            } else {
                dis_flags = 0;
                return;
            }
            E_AddRegister(rd);
            E_AddRegister(ra);
            E_Comment(rb);
            break;
        }
        E_AddRegister(rb);
        break;
    case 26:   /* frS,rA,rB: floating-point loads and stores */
        E_AddFloatRegister(rd);
        E_AddRegister(ra);
        E_AddRegister(rb);
        break;
    case 27:   /* rD,rA,NB: lswi, stswi; tlbre */
        if (entry->xo == 0x3b2) {
            if (!dis_extended) {
                E_AddRegister(rd);
                E_AddRegister(ra);
                E_AddNumber(rb);
                break;
            }
            if (rb == 0) {
                sprintf(mnemonic, data_00490fe4, data_00491894);   /* "tlbrehi" */
            } else if (rb == 1) {
                sprintf(mnemonic, data_00490fe4, data_0049189c);   /* "tlbrelo" */
            } else {
                dis_flags = 0;
                return;
            }
            E_AddRegister(rd);
            E_AddRegister(ra);
            E_Comment(rb);
            break;
        }
        E_AddRegister(rd);
        E_AddRegister(ra);
        E_AddDecimal(rb);
        E_Comment(rb);
        break;
    case 28:   /* rA,rB,STRM[,T]: dst (dstt), wrteei */
    case 29:   /* dstst (dststt) */
        if (dis_extended && (insn >> 25 & 1))
            sprintf(mnemonic, data_00490fe4, entry->form == 28 ? data_004918a4 : data_004918ac);
        E_AddRegister(ra);
        E_AddRegister(rb);
        E_AddHex(rd & 3);
        if (!dis_extended)
            E_AddHex(rd >> 4);
        break;
    case 30:   /* STRM[,A]: dss (dssall) */
        if (dis_extended && (insn >> 25 & 1)) {
            sprintf(mnemonic, data_00490fe4, data_004918b4);   /* "dssall" */
            return;
        }
        E_AddHex(rd & 3);
        if (!dis_extended)
            E_AddHex(rd >> 4);
        break;
    case 31:   /* vD,rA,rB: vector loads and stores */
        E_AddVectorRegister(rd);
        E_AddRegister(ra);
        E_AddRegister(rb);
        break;
    case 33:   /* rD,rA,rB (unused) */
        E_AddRegister(rd);
        E_AddRegister(ra);
        E_AddRegister(rb);
        break;
    }
    /* 23 (sync, eieio, tlbia, tlbsync, slbia) and 32 (esa, dsa): no operands */
}

/* Primary opcode 4: the Gekko's paired singles and dcbz_l (data_0048ffc8 and data_004902a0 by bits 1-10, then
   data_00490230 by bits 1-6, then data_004900fc by bits 1-5; valid for the processors in each entry). */
/* 0x004614d0 */
void PPCDis_Opcode4(long unused, unsigned long insn, long pc, char *mnemonic) {
    D_Opcode *op;
    unsigned long d = insn >> 21 & 0x1f;
    unsigned long a = insn >> 16 & 0x1f;
    unsigned long b = insn >> 11 & 0x1f;
    unsigned long c = insn >> 6 & 0x1f;
    unsigned long w = insn >> 10 & 1;
    unsigned long i = insn >> 7 & 7;
    char text[32];

    op = D_Find((D_Opcode *)data_0048ffc8, 9, insn >> 1 & 0x3ff);
    if (!op)
        op = D_Find((D_Opcode *)data_004902a0, 4, insn >> 1 & 0x3ff);
    if (!op)
        op = D_Find((D_Opcode *)data_00490230, 4, insn >> 1 & 0x3f);
    if (!op)
        op = D_Find((D_Opcode *)data_004900fc, 0x11, insn >> 1 & 0x1f);
    if (!op || !(op->processors & dis_processors)) {
        dis_flags = 0;
        return;
    }
    if (!(dis_options & 0x40000000) && (op->flags & 0x40000000)) {
        dis_flags = 0;
        return;
    }
    dis_flags |= (short)(op->flags & 6);
    sprintf(mnemonic, (insn & 1) ? data_00490fe0 : data_00490fe4, (char *)op->name);

    switch (op->format) {
    case 0:   /* frD,frB */
        PPCDis_AddOperand(D_FPRegister(d));
        PPCDis_AddOperand(D_FPRegister(b));
        break;
    case 1:   /* frD,frA,frB */
        PPCDis_AddOperand(D_FPRegister(d));
        PPCDis_AddOperand(D_FPRegister(a));
        PPCDis_AddOperand(D_FPRegister(b));
        break;
    case 2:   /* dcbz_l rA,rB */
        PPCDis_AddOperand(PPCDis_Register(a));
        PPCDis_AddOperand(PPCDis_Register(b));
        break;
    case 3:   /* frD,frA,frC,frB */
        PPCDis_AddOperand(D_FPRegister(d));
        PPCDis_AddOperand(D_FPRegister(a));
        PPCDis_AddOperand(D_FPRegister(c));
        PPCDis_AddOperand(D_FPRegister(b));
        break;
    case 4:   /* frD,frA,frC */
        PPCDis_AddOperand(D_FPRegister(d));
        PPCDis_AddOperand(D_FPRegister(a));
        PPCDis_AddOperand(D_FPRegister(c));
        break;
    case 5:   /* psq_lx and co.: frD,rA,rB,W,I */
        PPCDis_AddOperand(D_FPRegister(d));
        PPCDis_AddOperand(PPCDis_Register(a));
        PPCDis_AddOperand(PPCDis_Register(b));
        PPCDis_AddOperand(D_Immediate(w));
        PPCDis_AddOperand(D_Immediate(i));
        break;
    case 6:   /* ps_cmpu0 and co.: crD,frA,frB */
        sprintf(text, data_00490f50, insn >> 23 & 7);
        PPCDis_AddOperand(text);
        PPCDis_AddOperand(D_FPRegister(a));
        PPCDis_AddOperand(D_FPRegister(b));
        break;
    }
}

/* Primary opcode 19 (data_0049034c by bits 1-10): mcrf, bclr and bcctr (with their extended mnemonics: bdnzflr,
   bltlr, blr, bnectr, bctr, ..., + for the hint and l for LK), the CR logical instructions, rfi, rfci, isync. */
/* 0x004609a0 */
void PPCDis_Opcode19(long unused, unsigned long insn, long pc, char *mnemonic) {
    D_Opcode *op = D_Find((D_Opcode *)data_0049034c, 14, insn >> 1 & 0x3ff);
    unsigned long bo = insn >> 21 & 0x1f;
    unsigned long bi = insn >> 16 & 0x1f;
    unsigned short format;
    char *name;
    char text[12];
    char operand[32];

    if (!op || !(op->processors & dis_processors)) {
        dis_flags = 0;
        return;
    }
    if (!(dis_options & 0x40000000) && (op->flags & 0x40000000)) {
        dis_flags = 0;
        return;
    }
    dis_flags |= (short)(op->flags & 0xe);
    format = op->format;
    name = (char *)op->name;
    if (dis_extended) {
        if (op->xo == 0x10 || op->xo == 0x210) {
            if (op->xo == 0x10) {
                format = 6;
                switch (bo & 0x1e) {
                case 0: name = data_00491728; break;   /* bdnzflr */
                case 2: name = data_00491730; break;   /* bdzflr */
                case 4: name = (char *)data_0049046c[4 + (bi & 3)]; break;   /* bgelr ... */
                case 0xc: name = (char *)data_0049046c[bi & 3]; break;      /* bltlr ... */
                case 8: name = data_00491738; break;   /* bdnztlr */
                case 10: name = data_00491740; break;  /* bdztlr */
                case 0x10: name = data_00491748; break; /* bdnzlr */
                case 0x12: name = data_00491750; break; /* bdzlr */
                default: name = data_00491758; break;  /* blr */
                }
            } else {
                format = 5;
                switch (bo & 0x1e) {
                case 4: name = (char *)data_004904cc[4 + (bi & 3)]; break;  /* bgectr ... */
                case 0xc: name = (char *)data_004904cc[bi & 3]; break;     /* bltctr ... */
                default: name = data_00491764; break;  /* bctr */
                }
            }
            if (bo & 1) {
                strcpy(text, name);
                strcat(text, data_0049175c);   /* + */
                name = text;
            }
            if (insn & 1) {
                if (name != text)
                    strcpy(text, name);
                strcat(text, data_00491760);   /* l */
                name = text;
            }
        }
    } else if (insn & 1) {
        strcpy(text, name);
        strcat(text, data_00491760);   /* l */
        name = text;
    }
    sprintf(mnemonic, data_00490fe4, name);

    switch (format) {
    case 1:   /* bclr, bcctr: BO and BI by name */
        sprintf(data_0049adf9, data_00490f50, bi >> 2);
        switch (bo & 0x1e) {
        case 0:
            sprintf(operand, data_0049176c, data_0049adf9, (char *)data_0048e018[bi & 3]);
            break;
        case 2:
            sprintf(operand, data_00491784, data_0049adf9, (char *)data_0048e018[bi & 3]);
            break;
        case 4:
            sprintf(operand, data_00491798, data_0049adf9, (char *)data_0048dfe8[bi & 3]);
            break;
        case 8:
            sprintf(operand, data_0049176c, data_0049adf9, (char *)data_0048dff8[bi & 3]);
            break;
        case 10:
            sprintf(operand, data_00491784, data_0049adf9, (char *)data_0048dff8[bi & 3]);
            break;
        case 0xc:
            sprintf(operand, data_004917a8, data_0049adf9, (char *)data_0048dfe8[bi & 3]);
            break;
        case 0x10:
            /* The original passes the cr buffer's address (0x0049adf9) to "%d" (and BI after it). */
            sprintf(operand, data_004917b4, 0x0049adf9L);
            break;
        case 0x12:
            sprintf(operand, data_004917c4, 0x0049adf9L);
            break;
        case 0x14:
            sprintf(operand, data_004917d4, bi);
            break;
        default:
            sprintf(operand, data_004917e0, bo, bi);
            break;
        }
        PPCDis_AddOperand(operand);
        break;
    case 2:   /* CR logical: crbD,crbA,crbB */
        sprintf(operand, data_004917e8, bo, bi, insn >> 11 & 0x1f);
        PPCDis_AddOperand(operand);
        break;
    case 3:   /* mcrf: crD,crS */
        sprintf(operand, data_004917f4, insn >> 23 & 7, insn >> 18 & 7);
        PPCDis_AddOperand(operand);
        break;
    case 5:   /* bcctr's extended mnemonics: the cr field, unless cr0 */
        if (bi > 3) {
            sprintf(data_0049adf9, data_00490f50, bi >> 2);
            PPCDis_AddOperand(data_0049adf9);
        }
        break;
    case 6:   /* bclr's extended mnemonics */
        switch (bo & 0x1e) {
        case 0:
        case 2:
        case 8:
        case 10:
            if (bi < 4)
                sprintf(operand, data_00490fe4, (char *)data_0048dff8[bi & 3]);
            else
                sprintf(operand, data_00491800, bi >> 2, (char *)data_0048dff8[bi & 3]);
            PPCDis_AddOperand(operand);
            break;
        case 4:
        case 0xc:
            if (bi > 3) {
                sprintf(data_0049adf9, data_00490f50, bi >> 2);
                PPCDis_AddOperand(data_0049adf9);
            }
            break;
        }
        break;
    }
}

/* The moves to and from the condition, special, time base and device control registers (opcode 31: mtcrf,
   mfspr/mtspr with their extended mnemonics, mftb, mfdcr/mtdcr with the 403/405's). */
/* 0x0045de40 */
void PPCDis_MoveSpecialRegister(long unused, unsigned long insn, long pc, char *mnemonic) {
    unsigned long reg = insn >> 21 & 0x1f;
    unsigned long field = insn >> 11 & 0x3ff;
    long number = (long)((field >> 5) + (field & 0x1f) * 32);
    char text[32];
    char *name;

    switch (insn >> 1 & 0x3ff) {
    case 0x90:
        sprintf(mnemonic, "%s", "mtcrf");
        sprintf(text, "0x%x", insn >> 12 & 0xff);
        PPCDis_AddOperand(text);
        PPCDis_AddOperand(PPCDis_Register(reg));
        break;
    case 0x143:
        if (C_NamedRegister(C_mfdcr, C_COUNT(C_mfdcr), number, reg, mnemonic))
            return;
        sprintf(mnemonic, "%s", "mfdcr");
        PPCDis_AddOperand(PPCDis_Register(reg));
        PPCDis_AddOperand(C_DeviceControlRegister(number));
        break;
    case 0x153:
        if (C_NamedRegister(C_mfspr, C_COUNT(C_mfspr), number, reg, mnemonic))
            return;
        sprintf(mnemonic, "%s", "mfspr");
        PPCDis_AddOperand(PPCDis_Register(reg));
        PPCDis_AddOperand(C_SpecialRegister(number));
        break;
    case 0x173:
        /* mftb: 268 mftb, 269 mftbu; else by the field as encoded (not swapped) */
        if (number == 0x10c)
            name = "mftb";
        else if (number == 0x10d)
            name = "mftbu";
        else if (field == 0x188)
            name = "mtbr";
        else
            name = "mtbru";
        sprintf(mnemonic, "%s", name);
        PPCDis_AddOperand(PPCDis_Register(reg));
        break;
    case 0x1c3:
        if (C_NamedRegister(C_mtdcr, C_COUNT(C_mtdcr), number, reg, mnemonic))
            return;
        sprintf(mnemonic, "%s", "mtdcr");
        PPCDis_AddOperand(C_DeviceControlRegister(number));
        PPCDis_AddOperand(PPCDis_Register(reg));
        break;
    case 0x1d3:
        if (C_NamedRegister(C_mtspr, C_COUNT(C_mtspr), number, reg, mnemonic))
            return;
        sprintf(mnemonic, "%s", "mtspr");
        PPCDis_AddOperand(C_SpecialRegister(number));
        PPCDis_AddOperand(PPCDis_Register(reg));
        break;
    default:
        dis_flags = 0;
        break;
    }
}

/* Opcode 63 mtfsf: FM,frB. */
/* 0x0045dbb0 */
void PPCDis_Mtfsf(long unused, unsigned long insn, long pc, char *mnemonic) {
    unsigned long frb;
    char *text;

    sprintf(mnemonic, data_00490fe4, (insn & 1) ? data_0049105c : data_00491064);
    A_AddImmediate(insn >> 17 & 0xff);
    frb = insn >> 11 & 0x1f;
    text = A_Callback(1, (long)frb, 0);
    if (!text) {
        sprintf(data_0049ae79, data_00490f78, frb);
        text = data_0049ae79;
    }
    PPCDis_AddOperand(text);
}

/* Opcode 31 sradi (XS-form): rA,rS,SH (6 bits). */
/* 0x0045d8d0 */
void PPCDis_Sradi(long unused, unsigned long insn, long pc, char *mnemonic) {
    sprintf(mnemonic, data_00490fe4, (insn & 1) ? data_0049104c : data_00491054);
    PPCDis_AddOperand(PPCDis_Register(insn >> 16 & 0x1f));
    PPCDis_AddOperand(PPCDis_Register(insn >> 21 & 0x1f));
    A_AddImmediate((insn >> 1 & 1) << 5 | (insn >> 11 & 0x1f));
}

/* Opcode 31's XO-form arithmetic (add, subf, mullw, divw, neg, ...; data_004905ac by bits 1-9): "o" for OE, "." for
   Rc. */
/* 0x0045d380 */
void PPCDis_IntegerArithmetic(long unused, unsigned long insn, long pc, char *mnemonic) {
    A_XOForm *table = (A_XOForm *)data_004905ac;
    A_XOForm entry;
    unsigned long xo = insn >> 1 & 0x1ff;
    long low = 0;
    long high = 0x1b;   /* the original's, one past its 27 entries */
    long middle;
    char buf[12];
    char *name;

    do {
        middle = (low + high) / 2;
        if (xo == table[middle].xo) {
            entry = table[middle];
            goto found;
        }
        if (table[middle].xo < xo)
            low = middle + 1;
        else
            high = middle - 1;
    } while (low <= high);
    dis_flags = 0;
    entry = *(A_XOForm *)data_0048e378;
found:
    if (!(entry.processors & dis_processors)) {
        dis_flags = 0;
        return;
    }
    if (!(dis_options & 0x40000000) && (entry.flags & 0x40000000)) {
        dis_flags = 0;
        return;
    }
    dis_flags |= (short)(entry.flags & 6);
    name = entry.name;
    if (insn >> 10 & 1) {
        strcpy(buf, name);
        strcat(buf, data_00490764);
        name = buf;
    }
    sprintf(mnemonic, (insn & 1) ? data_00490fe0 : data_00490fe4, name);
    switch (entry.form) {
    case 1:
    case 2:
        PPCDis_AddOperand(PPCDis_Register(insn >> 21 & 0x1f));
        PPCDis_AddOperand(PPCDis_Register(insn >> 16 & 0x1f));
        PPCDis_AddOperand(PPCDis_Register(insn >> 11 & 0x1f));
        break;
    case 3:
        PPCDis_AddOperand(PPCDis_Register(insn >> 21 & 0x1f));
        PPCDis_AddOperand(PPCDis_Register(insn >> 16 & 0x1f));
        break;
    }
}

/* Opcode 31 isel (processor 0x80000): isellt, iselgt, iseleq, iselso (by BC & 3) rD,rA,rB,crN. */
/* 0x0045cfb0 */
void PPCDis_Isel(long unused, unsigned long insn, long pc, char *mnemonic) {
    if (!(dis_processors & 0x80000)) {
        dis_flags = 0;
        return;
    }
    sprintf(mnemonic, (insn & 1) ? data_00490fe0 : data_00490fe4, (char *)data_004907a4[insn >> 6 & 3]);
    PPCDis_AddOperand(PPCDis_Register(insn >> 21 & 0x1f));
    PPCDis_AddOperand(PPCDis_Register(insn >> 16 & 0x1f));
    PPCDis_AddOperand(PPCDis_Register(insn >> 11 & 0x1f));
    sprintf(data_0049adf9, data_00490f50, insn >> 8 & 7);
    PPCDis_AddOperand(data_0049adf9);
}

/* The A-form floating point instructions (opcodes 59 and 63, by bits 1-5: fadd, fsub, fmul, fdiv, fsqrt, fres,
   frsqrte, fsel, fmadd, fmsub, fnmadd, fnmsub and their single forms), "." for Rc. Forms: 1 frD,frA,frB;
   2 frD,frA,frC,frB; 3 frD,frA,frC; 4 frD,frB. */
/* 0x0045bfa0 */
void PPCDis_FloatAForm(long unused, unsigned long insn, long pc, char *mnemonic) {
    C_AForm *table;
    C_AForm *entry;
    long low;
    long high;
    long middle;
    unsigned short xo;
    unsigned long d, a, b, c;

    if ((insn >> 26) == 59) {
        table = (C_AForm *)data_00490804;
        high = 10;
    } else {
        table = (C_AForm *)data_004908fc;
        high = 11;
    }
    xo = (unsigned short)(insn >> 1 & 0x1f);
    low = 0;
    entry = 0;
    do {
        middle = (low + high) / 2;
        if (xo == table[middle].xo) {
            entry = &table[middle];
            break;
        }
        if (table[middle].xo < xo)
            low = middle + 1;
        else
            high = middle - 1;
    } while (low <= high);
    if (!entry) {
        dis_flags = 0;
        entry = (C_AForm *)data_0048e378;
    }

    if (!(entry->processors & dis_processors)) {
        dis_flags = 0;
        return;
    }
    if (!(dis_options & 0x40000000) && (entry->flags & 0x40000000)) {
        dis_flags = 0;
        return;
    }
    dis_flags |= (short)(entry->flags & 6);

    d = insn >> 21 & 0x1f;
    a = insn >> 16 & 0x1f;
    b = insn >> 11 & 0x1f;
    c = insn >> 6 & 0x1f;
    sprintf(mnemonic, (insn & 1) ? "%s." : "%s", entry->name);
    switch (entry->form) {
    case 1:
        C_AddFPRegister(d);
        C_AddFPRegister(a);
        C_AddFPRegister(b);
        break;
    case 2:
        C_AddFPRegister(d);
        C_AddFPRegister(a);
        C_AddFPRegister(c);
        C_AddFPRegister(b);
        break;
    case 3:
        C_AddFPRegister(d);
        C_AddFPRegister(a);
        C_AddFPRegister(c);
        break;
    case 4:
        C_AddFPRegister(d);
        C_AddFPRegister(b);
        break;
    }
}

/* Primary opcodes 20-23: rlwimi, rlwinm, rlmi (options 2), rlwnm, and with extended mnemonics insrwi, inslwi,
   clrrwi, clrlwi, rotlwi, rotrwi, slwi, srwi, extlwi, extrwi, clrlslwi and rotlw. */
/* 0x004593e0 */
void PPCDis_Rotate(long unused, unsigned long insn, long pc, char *mnemonic) {
    unsigned long rs = insn >> 21 & 0x1f;
    unsigned long ra = insn >> 16 & 0x1f;
    unsigned long sh = insn >> 11 & 0x1f;   /* rb for rlwnm */
    unsigned long mb = insn >> 6 & 0x1f;
    unsigned long me = insn >> 1 & 0x1f;
    unsigned long opcode = insn >> 26;

    if (dis_extended) {
        switch (opcode) {
        case 20:
            /* insrwi ra,rs,n,b: n = me + 1 - mb, b = mb */
            if (sh == 31 - me) {
                B_Extended(insn, mnemonic, data_00490fe8, ra, rs, 2, (long)(me + 1 - mb), (long)mb);
                return;
            }
            /* inslwi ra,rs,n,b */
            if (sh == 32 - mb) {
                B_Extended(insn, mnemonic, data_00490ff0, ra, rs, 2, (long)(me + 1 - mb), (long)mb);
                return;
            }
            break;
        case 21:
            if (sh == 0) {
                if (mb == 0) {
                    B_Extended(insn, mnemonic, data_00490ff8, ra, rs, 1, (long)(31 - me), 0);   /* clrrwi */
                    return;
                }
                if (me == 31) {
                    B_Extended(insn, mnemonic, data_00491000, ra, rs, 1, (long)mb, 0);          /* clrlwi */
                    return;
                }
            }
            if (mb == 0 && me == 31) {
                /* rotlwi up to 17, else rotrwi (with SH itself, not 32 - SH) */
                B_Extended(insn, mnemonic, sh <= 17 ? data_00491008 : data_00491010, ra, rs, 1, (long)sh, 0);
                return;
            }
            if (mb == 0 && sh == 31 - me) {
                B_Extended(insn, mnemonic, data_00491018, ra, rs, 1, (long)sh, 0);              /* slwi */
                return;
            }
            if (me == 31 && mb == 32 - sh) {
                B_Extended(insn, mnemonic, data_00491020, ra, rs, 1, (long)(32 - sh), 0);       /* srwi */
                return;
            }
            if (mb == 0 && sh != 0) {
                B_Extended(insn, mnemonic, data_00491028, ra, rs, 2, (long)(me + 1), (long)sh); /* extlwi */
                return;
            }
            if (me == 31) {
                /* extrwi ra,rs,n,b: n = 32 - mb, b = sh - n (the original's test n == sh - (sh - n) always
                   holds) */
                B_Extended(insn, mnemonic, data_00491030, ra, rs, 2, (long)(32 - mb), (long)(sh - (32 - mb)));
                return;
            }
            /* clrlslwi ra,rs,b,n: b = sh + mb, n = sh */
            if (sh == 31 - me && (long)(sh + mb) < 32) {
                B_Extended(insn, mnemonic, data_00491038, ra, rs, 2, (long)(sh + mb), (long)sh);
                return;
            }
            break;
        case 23:
            if (mb == 0 && me == 31) {
                B_Mnemonic(mnemonic, insn, data_00491044);                                    /* rotlw */
                PPCDis_AddOperand(PPCDis_Register(ra));
                PPCDis_AddOperand(PPCDis_Register(rs));
                PPCDis_AddOperand(PPCDis_Register(sh));
                return;
            }
            break;
        }
    }

    B_Mnemonic(mnemonic, insn, (char *)data_004909cc[opcode - 20]);
    PPCDis_AddOperand(PPCDis_Register(ra));
    PPCDis_AddOperand(PPCDis_Register(rs));
    switch (opcode) {
    case 20:
    case 21:
        B_Number((long)sh);
        break;
    case 22:
        if (!(dis_options & 2)) {
            dis_flags = 0;
            return;
        }
        /* fall through */
    case 23:
        PPCDis_AddOperand(PPCDis_Register(sh));
        break;
    }
    B_Number((long)mb);
    B_Number((long)me);
}

/* Opcode 30 MD-form: rldicl, rldicr, rldic, rldimi (bits 2-4 < 4) rA,rS,SH,MB (6 bits each). */
/* 0x00458fb0 */
void PPCDis_RotateMD(long unused, unsigned long insn, long pc, char *mnemonic) {
    unsigned long xo = insn >> 2 & 7;

    if (xo > 3) {
        dis_flags = 0;
        return;
    }
    sprintf(mnemonic, (insn & 1) ? data_00490fe0 : data_00490fe4, (char *)data_004909fc[xo]);
    PPCDis_AddOperand(PPCDis_Register(insn >> 16 & 0x1f));
    PPCDis_AddOperand(PPCDis_Register(insn >> 21 & 0x1f));
    A_AddImmediate((insn >> 1 & 1) << 5 | (insn >> 11 & 0x1f));
    A_AddImmediate((insn >> 5 & 1) << 5 | (insn >> 6 & 0x1f));
}

/* Opcode 30 MDS-form: rldcl, rldcr (bits 1-4 8, 9) rA,rS,rB,MB. */
/* 0x00458bd0 */
void PPCDis_RotateMDS(long unused, unsigned long insn, long pc, char *mnemonic) {
    long xo = (long)(insn >> 1 & 0xf) - 8;

    if (xo != 0 && xo != 1) {
        dis_flags = 0;
        return;
    }
    sprintf(mnemonic, (insn & 1) ? data_00490fe0 : data_00490fe4, (char *)data_00490a1c[xo]);
    PPCDis_AddOperand(PPCDis_Register(insn >> 16 & 0x1f));
    PPCDis_AddOperand(PPCDis_Register(insn >> 21 & 0x1f));
    PPCDis_AddOperand(PPCDis_Register(insn >> 11 & 0x1f));
    A_AddImmediate((insn >> 5 & 1) << 5 | (insn >> 6 & 0x1f));
}

/* Opcode 30: MD-form (bits 1-3 < 8 of bits 1-4) or MDS-form. */
/* 0x00458b90 */
void PPCDis_Opcode30(long unused, unsigned long insn, long pc, char *mnemonic) {
    if (((unsigned char)insn >> 1 & 0xf) < 8)
        PPCDis_RotateMD(unused, insn, pc, mnemonic);
    else
        PPCDis_RotateMDS(unused, insn, pc, mnemonic);
}

/* Opcode 31: isel (xo & 0x1f == 0xf, not 0x6f) by itself, the rest by bits 1-10 (data_00490a24, 168 entries). */
/* 0x00458ad0 */
void PPCDis_Opcode31(long unused, unsigned long insn, long pc, char *mnemonic) {
    unsigned short xo = (unsigned short)insn >> 1;

    if ((xo & 0x1f) == 0xf && (xo & 0x7f) != 0x6f)
        PPCDis_Isel(unused, insn, pc, mnemonic);
    else
        PPCDis_Dispatch((DisExtended *)(data_00490a24), 0xa8, xo & 0x3ff, unused, insn, pc, mnemonic);
}

/* Opcodes 59 and 63 (floating point): A-form by bits 1-5 (data_00490e10, 12 entries) when bit 5 is set, else X-form
   by bits 1-10 (data_00490e54, 19 entries). */
/* 0x004589c0 */
void PPCDis_Opcode63(long unused, unsigned long insn, long pc, char *mnemonic) {
    if (!(insn >> 1 & 0x10))
        PPCDis_Dispatch((DisExtended *)(data_00490e54), 0x13, insn >> 1 & 0x3ff, unused, insn, pc, mnemonic);
    else
        PPCDis_Dispatch((DisExtended *)(data_00490e10), 0xc, insn >> 1 & 0x1f, unused, insn, pc, mnemonic);
}

/* Not an instruction. */
/* 0x004589b0 */
void PPCDis_Invalid(long unused, unsigned long insn, long pc, char *mnemonic) {
    dis_flags = 0;
}

/* Appends TEXT to the operands, after a comma when there are some; at 0x1ff characters the operands end with
   "..." instead. */
/* 0x004588f0 */
void PPCDis_AddOperand(char *text) {
    if (dis_operandLength != 0 && dis_operandLength <= 0x1fe) {
        dis_operands[dis_operandLength] = ',';
        dis_operandLength++;
    }
    for (; *text; text++) {
        if (dis_operandLength > 0x1fe) {
            dis_operands[dis_operandLength - 1] = '.';
            dis_operands[dis_operandLength - 2] = dis_operands[dis_operandLength - 1];
            dis_operands[dis_operandLength - 3] = dis_operands[dis_operandLength - 2];
            dis_operands[dis_operandLength] = 0;
            return;
        }
        dis_operands[dis_operandLength] = *text;
        dis_operandLength++;
    }
    dis_operands[dis_operandLength] = 0;
}

/* Disassembles the instruction at INSTRUCTION (its address INSTRUCTION + BASE, PC for the callback) into MNEMONIC,
   OPERANDS and COMMENT, with OPTIONS (the processors in its low 20 bits, see dis_options) and PROC for the
   operands' names. *STATE is set to 4 (the instruction's length). Returns dis_flags (0: not an instruction, shown as
   dc.l with its bytes). */
/* 0x004586a0 */
unsigned short PPCDis_Disassemble(unsigned char *instruction, long base, unsigned long options, char *mnemonic,
                                  char *operands, char *comment, long pc, long *state, DisassemblerOperandProc proc) {
    unsigned long insn;
    long value;
    char text[4];
    int i;

    dis_processors = options & 0xfffff;
    if (!dis_processors)
        dis_processors = 0x6fffd;
    if (options & 0x4000000)
        dis_hexPrefix = "0x";
    else
        dis_hexPrefix = "$";
    if (!dis_started) {
        data_0048dfd0 = 0;
        dis_started = 1;
    }
    *state = 4;
    dis_flags = 1;
    dis_extended = (options & 0x8000000) == 0;
    *comment = 0;
    *operands = *comment;
    *mnemonic = *operands;
    data_0049b0be = 0;
    dis_operandLength = 0;
    dis_mnemonic = mnemonic;
    dis_operands = operands;
    dis_comment = comment;
    dis_proc = proc;
    dis_pc = pc;
    dis_instruction = instruction;
    dis_base = base;
    dis_options = options;
    if (options & 0x80000000)
        insn = (unsigned long)instruction[3] << 24 | (unsigned long)instruction[2] << 16 |
               (unsigned long)instruction[1] << 8 | instruction[0];
    else
        insn = (unsigned long)instruction[0] << 24 | (unsigned long)instruction[1] << 16 |
               (unsigned long)instruction[2] << 8 | instruction[3];
    ((DisHandler)data_00490ec0[(unsigned char)data_0048e028[insn >> 26]])(0, insn, (long)(instruction + base),
                                                                          mnemonic);
    if (!dis_flags) {
        strcpy(mnemonic, "dc.l");
        sprintf(operands, "0x%08x", insn);
        value = insn;
        for (i = 0; i < 4; i++) {
            if (__ctype_map[value & 0xff] & 0xdc)
                text[i] = (char)value;
            else
                text[i] = '.';
            value >>= 8;
        }
        if (dis_options & 0x80000000)
            sprintf(comment, "; Invalid opcode '%c%c%c%c'", text[0], text[1], text[2], text[3]);
        else
            sprintf(comment, "; Invalid opcode '%c%c%c%c'", text[3], text[2], text[1], text[0]);
    }
    return dis_flags;
}

/* The text of general register REG: the callback's, else rsp and rtoc for r1 and r2 (only rsp with "0x" for hex),
   else rN; "????" for a number over 31. Options 0x10000000: rN only. */
/* 0x004585d0 */
char *PPCDis_Register(unsigned long reg) {
    char *text;

    if (reg > 31) {
        sprintf(data_0049ae99, "????");
        return data_0049ae99;
    }
    if (dis_proc) {
        text = dis_proc(dis_pc, (long)dis_instruction, dis_base, 0, reg, 0, dis_operandText);
        if (text)
            return text;
    }
    if (!(dis_options & 0x10000000)) {
        if (!(dis_options & 0x4000000)) {
            if (reg == 1)
                return "rsp";
            if (reg == 2)
                return "rtoc";
        } else if (reg == 1) {
            return "rsp";
        }
    }
    sprintf(data_0049ae99, "r%d", reg);
    return data_0049ae99;
}
