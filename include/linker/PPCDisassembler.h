#ifndef LINKER_PPCDISASSEMBLER_H
#define LINKER_PPCDISASSEMBLER_H

/* The PowerPC disassembler (src/linker/PPCDisassembler.c), for DumpELFCodePPC.c's code dumps. */

/* The dump's callback for an operand: the text of a relocation's symbol, a special or device control register's
   name (DumpELFCodePPC.c's DumpCode_Operand), copied to OUT; NULL to print it as a number. KIND: 0 a general register
   (VALUE its number), 2, 3 an immediate, 4, 5, 8 a branch target, 6 a displacement (REG its base register), 7 a
   special register, 12 a device control register. */
typedef char *(*DisassemblerOperandProc)(long pc, long arg2, long arg3, unsigned char kind, long value, short reg,
                                         char *out);

/* Disassembles the instruction at INSTRUCTION (its address INSTRUCTION + BASE, PC for the callback) into MNEMONIC,
   OPERANDS and COMMENT, with OPTIONS (the processors in its low 20 bits; 0x80000000 little-endian, 0x10000000
   numbered registers only, 0x8000000 no extended mnemonics, 0x4000000 "0x" for hex) and PROC for the operands'
   names. Returns 0 for a word that is no instruction (dc.l), else flags: 1, 8 a relative branch target. */
extern unsigned short PPCDis_Disassemble(unsigned char *instruction, long base, unsigned long options,
                                         char *mnemonic, char *operands, char *comment, long pc, long *state,
                                         DisassemblerOperandProc proc);

#endif
