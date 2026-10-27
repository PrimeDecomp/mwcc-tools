/* The output: the sections laid out (their addresses, the assignments of the command file, padding, the exception
   table index, the constructors' and destructors' ends), the linker-defined symbols given their values, the ELF file
   written (its header, program headers, sections, relocations, symbols, string tables, DWARF, .comment, seginfo and
   section headers), the S-records made alongside, and the output's buffer, opened, written and closed.

   Built with -inline deferred, the compiler emits the functions in the reverse of their order here. The assertions
   name this file "ELF_gen.c". */
#include "linker/ELF_linker.h"
#include "compiler/CompilerTools.h"
#include "compiler/ELF_Endian.h"
#include "driver/COSToolsCLT.h"
#include "driver/Files.h"

#define ELG_ASSERT(line) CompilerTools_FormatMessageAndLongjmp(0x33, 2, "ELF_gen.c", line)

/* 0x43b2b0 to 0x43df30: the .comment section, the procedure records and their relocations,
   the relocation sections, .PPC.EMB.seginfo, the section numbers, .symtab (its order and its entries), .shstrtab
   and .strtab. */

/* ELF_gen_ResolveSymbol and ELF_gen_ResolveSymbolQuiet: a relocation's target (kind 2 items: not an error when missing) */
typedef void (*ELGATargetFunc)(ELSymbol *symbol, long file, ELSymbol **target, long *targetFile);

/* An entry of .PPC.EMB.seginfo (12 bytes). */
typedef struct ELGASegInfo {
    unsigned short index;
    unsigned short flag;
    unsigned long name;
    unsigned long next;
} ELGASegInfo;

#define ELGA_NEXT_OUT(piece) (*(ELPiece **)&(piece)->x24)   /* +0x24: the next symbol of .symtab's order */
#define ELGA_INDEX(piece) ((piece)->x30)                     /* +0x30: its index in .symtab */
#define ELGA_OUT_VALUE(piece) ((piece)->x10)                 /* +0x10: its address in the output */
#define ELGA_WRITTEN(piece) ((piece)->x46[2])                /* +0x48: written to .symtab */

/* 0x43df30 to 0x4403b0: the linker-defined symbols' values and the segment symbols, the writing
   of the ELF file (its header, the program headers, the sections, the section headers, the memory map) and of one
   output section's contents. */

/* The output's kind (0x49be88, data_0058770c's first word): 0 a program, 6 a partial link (the 2.6 driver
   makes no other), whose linker-defined symbols get no values. */
#define ELG_OUTPUT_KIND (*(short *)data_0058770c)

/* 0x4403b0 to 0x442e00: the exception table index (extabindex), the symbols' addresses, the
   layout of the output sections' contents (and of the DWARF sections), the fill of an assignment to ".", and the
   writing of the output file and its buffers. */

/* ELGroup's x10: the first and last of its exception table entries and their functions (set before the
   index is written). */
typedef struct ELGCGroupEnds {
    ELRoot *first;
    ELRoot *last;
    ELSymbol *firstTarget;
    ELSymbol *lastTarget;
} ELGCGroupEnds;
/* A symbol dead stripping dropped, with -mapunused (2.7: with the file offset column). */
#if LINKER >= 27
#define ELGC_UNUSED_LINE "  UNUSED   %06x ........ ........    %s %s %s\r\n"
#else
#define ELGC_UNUSED_LINE "  UNUSED   %06x ........ %s %s %s\r\n"
#endif


extern void CompilerTools_FormatMessageAndLongjmp(int message, int status, ...);
extern int sprintf(char *s, const char *format, ...);
extern unsigned long strlen(const char *s);
extern int strcmp(const char *a, const char *b);
extern void *memset(void *dst, int c, unsigned long n);
extern void CompilerTools_ReportLimitedDiagnostic(int message, ...);
extern void CompilerTools_ReportMessage(int message, ...);
extern void CompilerTools_Abort(void);
extern Boolean fn_00420350(void);
extern void ELF_Endian_ConvertTwoHalvesTwoWords(void *data);
extern ELLinkSymbol *LinkSym_FindIndex(long index);
extern void ELF_PPC_EABI_RelocateSection(ELSection *section);                         /* a section's contents prepared (records, .debug) */
extern ELOutput *ELF_PPC_EABI_FindSectionByIndex(unsigned long index);   /* the output section whose o2c is index (all 32 bits compared) */
extern char data_0049c120;              /* 2: the ELF file is written directly (otherwise into data_004a2a70) */
extern GList *data_004a2a70;            /* the output's buffer */
extern unsigned long data_004a2dda;     /* the output file's size so far (its position) */
extern char *data_004a2e0a;             /* the .comment section's header (0x2c bytes) */
extern char *data_004a2e12;             /* extabindex's relocations (0x24 bytes per table) */
extern char data_004a2e37;              /* DWARF is written */
extern Boolean data_004a2e44;
extern Boolean data_004a2e45;           /* ROM images: .PPC.EMB.seginfo is written */
extern ELOutput *data_004a2dee;         /* .symtab */
extern ELOutput *data_004a2df2;         /* .strtab */
extern ELOutput *data_004a2df6;         /* .shstrtab */
extern ELOutput *data_004a2dfa;         /* .PPC.EMB.seginfo */
extern ELOutput **data_004a2a88;        /* the output sections, null-terminated */
extern short data_004a28ce;             /* .shstrtab's section index (e_shstrndx) */
extern unsigned char CompilerTools_ReportDiagnostic(long diagnosticCode, ...);
extern int strncmp(const char *a, const char *b, unsigned long n);
extern void *memcpy(void *dst, const void *src, unsigned long n);
extern Boolean data_004a2e59;           /* make the segment symbols */
extern ELOutput **data_004a2820;        /* the output sections, by file order (NULL-terminated) */
extern ELOutput *data_004a2dde;         /* the section that holds the linker-defined symbols */
extern ELOutput *data_004a2de2;         /* the section header written first */
extern ELOutputNode *data_004a2de6;   /* the sections listed in the memory map after the loaded ones */
extern ELOutput *data_004a2dea;         /* the .line section */
extern ELOutput *data_004a2e16;         /* extabindex's output section */
extern ELOutput *data_004a2e1a;         /* .ctors' */
extern ELOutput *data_004a2e1e;         /* .dtors' */
extern unsigned long data_004a2e2a;     /* .ctors' terminator's address */
extern unsigned long data_004a2e2e;     /* .dtors' terminator's address */
#if LINKER >= 27
/* SIZEOF_HEADERS: the file offset after the header and the program headers. */
static unsigned long ELF_gen_headersSize;
#endif
/* A zero-size symbol in the map: in its owner, "(entry of ...)"; 2.7's Linker Generated Symbol File's are lines
   as any other's. */
#if LINKER >= 27
#define EL_ENTRY_LINE(file) ((file) != ELF_linker_generatedFile)
#else
#define EL_ENTRY_LINE(file) 1
#endif
/* .ctors and .dtors end with a terminator the linker adds (-10, -11); 2.7's are its Linker Generated Symbol File's. */
#if LINKER >= 27
#define EL_CTOR_SYMBOLS 0
#else
#define EL_CTOR_SYMBOLS 1
#endif
extern long data_004a2218;
extern long data_004a2a80;
extern long data_004a28f8;
extern GList data_004a21d0;             /* the S-record file's header record */
extern GList data_004a21f0;             /* its end record */
extern GList *data_004a28d0;            /* the S-record buffer in use */
extern ELLinkSymbol *LinkSym_Find(char *name);
extern ELLinkSymbol *LinkSym_AddStartOf(ELOutput *section);
extern ELLinkSymbol *LinkSym_AddSegment(ELOutput *section, long segment);
extern void SRecord_End(void);
#if LINKER >= 27
extern void SRecord_Flush(void);
#endif
extern void SRecord_Start(unsigned long at, unsigned char kind);
extern void SRecord_AddWord(long word);
extern void SRecord_AddBytes(unsigned char *data, long size);
extern void DWARF_line_Compact(void);
extern unsigned long ELF_PPC_EABI_LinkSymbolValue(long index);  /* a linker-defined symbol's value */
extern void ELF_PPC_EABI_PrepareLayout(void);
extern void ELF_PPC_EABI_PrepareWrite(void);
extern void ELF_PPC_EABI_SelectBuffers(ELOutput *section);
extern void ELF_PPC_EABI_PlaceSection(ELOutput *section);
extern void ELF_PPC_EABI_AdvanceMemory(ELOutput *section);
extern void ELF_PPC_EABI_WriteLinkerData(ELOutput *section);
extern void *galloc(long size);
extern int __stdcall fn_00412de0(long x, short vRefNum);
extern short PrepDumpELF_CreateFile(short vRefNum, long parID, unsigned char *name, long creator, long type);
extern short SRecord_InitBuffer(GList *buffer, long size);
extern void DWARF_line_StripDeadCode(ELOutput *section);
extern void ELF_PPC_EABI_ApplyRelocations(long file, ELSection *section);
extern void ELF_PPC_EABI_LayoutLinkerData(ELOutput *section);
extern ELTerm *EvalExpr(ELTerm *list, ELOutput *section);
#if LINKER >= 27
extern void *LCF_CopyExpr(void *list);
#endif
extern char data_0058770c[];            /* (0x49be88) the linker's settings; its first short: the output kind */
extern FSSpec data_0049bf5c;            /* the link map file */
extern long data_0049c2f4;              /* the output's file type */
extern long data_0049c2f8;              /* its creator */
extern GList data_004a21e0;             /* the output's buffer */
extern unsigned long data_004a2dd2;     /* (a ROM address, given) */
extern unsigned long data_004a2dd6;
extern unsigned long data_004a2e22;     /* extabindex's offset in its section */
extern unsigned long data_004a2e26;     /* its section's size with it */
extern char *data_004a2e06;             /* the exception table index */
extern long data_004a2a6c;              /* the offset of the next __tv$ entry */
extern char data_004a2e3e;
extern char data_004a2e47;              /* the link map shows the unused symbols */

/* The padding written between sections: ELF_gen_PadSize bytes at ELF_gen_PadBytes, zeros at first. A larger pad
   makes a bigger buffer, filled with '0' (as the original does). */
long ELF_gen_PadSize = 0x80;
char ELF_gen_Zeros[0x80];
char *ELF_gen_PadBytes = ELF_gen_Zeros;

/* The last local symbol of .symtab's order; the segment symbols are chained after it. */
ELPiece *data_0049a7a8;

/* The functions of this file */
void ELF_gen_WriteBuffer(GList *buffer, short file, int message);
void ELF_gen_OpenOutput(void);
void ELF_gen_CloseOutput(void);
void ELF_gen_FlushOutput(void);
long ELF_gen_WritePadding(unsigned long align, Boolean srecord);
long ELF_gen_SkipPadding(unsigned long align);
void ELF_gen_AddToBuffer(GList *buffer, short file, char *data, long length, int message);
unsigned long ELF_gen_WriteString(const char *string);
long ELF_gen_WriteByte(char byte);
unsigned long ELF_gen_Write(const void *data, unsigned long size);
long ELF_gen_EvalAssignment(char *name, ELTerm **expr, ELOutput *section);
void ELF_gen_LayoutContents(ELOutput *section);
void ELF_gen_SizeExtabIndex(ELOutput *section);
void ELF_gen_MapExtabIndex(ELOutput *section);
void ELF_gen_SortExceptionEntries(void);
void ELF_gen_LayoutBssSection(ELOutput *section);
void ELF_gen_LayoutSection(ELOutput *section);
void ELF_gen_LayoutAssignment(ELOutput *section);
long ELF_gen_WriteFill(char *name, ELTerm **expr, ELOutput *section);
void ELF_gen_WriteAssignment(ELOutput *section);
void ELF_gen_AlignSectionInFile(ELOutput *section);
void ELF_gen_LayoutDwarfSection(ELOutput *section);
void ELF_gen_LayoutDwarf(ELOutput *section);
void ELF_gen_ResolveSymbol(ELSymbol *symbol, long file, ELSymbol **result, long *resultFile);
void ELF_gen_ResolveSymbolQuiet(ELSymbol *symbol, long file, ELSymbol **result, long *resultFile);
unsigned long ELF_gen_SymbolAddress(ELSymbol *symbol, long file);
void ELF_gen_ApplyRelocations(ELOutput *section);
void ELF_gen_WriteExtabIndex(ELOutput *section);
void ELF_gen_WriteSection(ELOutput *section);
void ELF_gen_WriteProgramHeader(ELOutput *section, Boolean swapRest, Boolean swapGiven, unsigned long type, unsigned long address);
void ELF_gen_WriteFile(void);
void ELF_gen_CheckLinkSymbols(void);
void ELF_gen_SetLinkSymbols(void);
void ELF_gen_WriteStrings(void);
void ELF_gen_WriteSectionNames(void);
void ELF_gen_OrderSymbols(void);
void ELF_gen_WriteSymbols(void);
void ELF_gen_NumberSections(void);
void ELF_gen_WriteSegInfo(void);
void ELF_gen_CountRelocations(ELOutput *out);
void ELF_gen_SizeRelocations(ELOutput *out);
void ELF_gen_WriteRelocations(ELOutput *out);
void ELF_gen_WriteExtabindexRelocations(ELOutput *out);
void ELF_gen_LayoutRecords(ELOutput *out);
void ELF_gen_CountRecordRelocations(ELOutput *out);
void ELF_gen_SizeRecordRelocations(ELOutput *out);
void ELF_gen_WriteRecordRelocations(ELOutput *out);
void ELF_gen_WriteDebug(ELOutput *out);
void ELF_gen_WriteRecords(ELOutput *out);
void ELF_gen_WriteComment(ELOutput *out);

/* A write to the output failed. */
static void ELGA_Fail(short error) {
    CompilerTools_ReportLimitedDiagnostic(1, data_0049bed6);
    CompilerTools_FormatMessageAndLongjmp(0xd, error);
}

/* SIZE bytes written to the output for output section OUT. */
static void ELGA_Write(ELOutput *out, void *data, unsigned long size) {
    short error;

    if (data_0049c120 == 2) {
        error = COS_FileWrite(data_004a2dcc, data, size);
        if (error)
            ELGA_Fail(error);
    } else {
        AppendGListData(data_004a2a70, data, size);
    }
    data_004a2dda += size;
    out->o8c += size;
}

/* The padding grown to SIZE bytes. */
static void ELGA_GrowPad(long size) {
    if (size > ELF_gen_PadSize) {
        ELF_gen_PadSize = size;
        ELF_gen_PadBytes = galloc(size);
        memset(ELF_gen_PadBytes, '0', ELF_gen_PadSize);
    }
}

/* The file padded to OUT's alignment, and OUT's file offset set (when the file is written directly). */
static void ELGA_AlignFile(ELOutput *out) {
    unsigned long align;
    long pos;
    long pad;
    short error;

    align = out->align98;
    error = COS_FileGetPos(data_004a2dcc, &pos);
    if (error)
        ELGA_Fail(error);
    if (align) {
        pad = ((pos + align - 1) & ~(align - 1)) - pos;
        ELGA_GrowPad(pad);
        error = COS_FileWrite(data_004a2dcc, ELF_gen_PadBytes, pad);
        if (error)
            ELGA_Fail(error);
    }
    error = COS_FileGetPos(data_004a2dcc, &pos);
    if (error)
        ELGA_Fail(error);
    out->o88 = pos;
}

/* SYMBOL is kept and not dropped. */
static Boolean ELGA_IsKept(ELSymbol *symbol) {
    return symbol && (symbol->flags & 0x10) && !(symbol->flags & 0x20);
}

/* A file error: reported against the output file, then the link stops. */
static void ELGB_CheckIO(short err) {
    if (err) {
        CompilerTools_ReportLimitedDiagnostic(1, data_0049bed6);
        CompilerTools_FormatMessageAndLongjmp(0xd, err);
    }
}

static void ELGB_Write(void *data, long size) {
    ELGB_CheckIO(COS_FileWrite(data_004a2dcc, data, size));
}

static unsigned long ELGB_GetPos(void) {
    long pos;
    ELGB_CheckIO(COS_FileGetPos(data_004a2dcc, &pos));
    return pos;
}

static void ELGB_SetPos(unsigned long pos) {
    ELGB_CheckIO(COS_FileSetPos(data_004a2dcc, pos));
}

/* The padding up to an alignment. */
static long ELGB_PadTo(unsigned long at, unsigned long align) {
    return (long)(((at + align - 1) & ~(align - 1)) - at);
}

/* Makes the padding bytes ('0's) at least size long. */
static void ELGB_GrowPad(long size) {
    if (size > ELF_gen_PadSize) {
        ELF_gen_PadSize = size;
        ELF_gen_PadBytes = (char *)galloc(size);
        memset(ELF_gen_PadBytes, 0x30, ELF_gen_PadSize);
    }
}

/* Pads the file from its position to an alignment (none for 0). */
static void ELGB_PadFile(unsigned long align) {
    unsigned long pos;
    long pad;

    pos = ELGB_GetPos();
    if (align) {
        pad = ELGB_PadTo(pos, align);
        ELGB_GrowPad(pad);
        ELGB_Write(ELF_gen_PadBytes, pad);
    }
}

/* Writes bytes of a section: to the file directly, or to the section's buffer. */
static void ELGB_Emit(void *data, long size) {
    if (data_0049c120 == 2)
        ELGB_Write(data, size);
    else
        AppendGListData(data_004a2a70, data, size);
    data_004a2dda += size;
}

/* Pads a section to an alignment (none for 0), also in the S-record file. */
static void ELGB_AlignOutput(ELOutput *section, unsigned long align) {
    Boolean srecord;
    long pad;

    srecord = data_004a2e3a;
    if (!align) {
        pad = 0;
    } else {
        pad = ELGB_PadTo(data_004a2dda, align);
        ELGB_GrowPad(pad);
        ELGB_Emit(ELF_gen_PadBytes, pad);
        if (srecord)
            SRecord_AddBytes((unsigned char *)ELF_gen_PadBytes, pad);
    }
    section->o8c += pad;
}

/* Writes the zero word that ends .ctors or .dtors. */
static void ELGB_ZeroWord(ELOutput *section) {
    long word;

    word = 0;
    ELGB_Emit(&word, 4);
    section->o8c += 4;
    if (data_004a2e3a) {
        SRecord_AddWord(word);
        SRecord_End();
    }
}

/* The padding to the next multiple of align from pos; the padding buffer grows to it. */
static unsigned long ELGC_Padding(unsigned long align, unsigned long pos) {
    unsigned long pad = ((pos + align - 1) & ~(align - 1)) - pos;

    if ((long)pad > ELF_gen_PadSize) {
        ELF_gen_PadSize = pad;
        ELF_gen_PadBytes = galloc(pad);
        memset(ELF_gen_PadBytes, 0x30, ELF_gen_PadSize);
    }
    return pad;
}

/* The names the link map shows for a symbol's object: its file and its archive member ("" for none). */
static void ELGC_FileNames(long file, char **name, char **member) {
#if LINKER >= 27
    /* (2.7 names an archive member by its name in the archive) */
    if (file > -1 && data_004a28dc[file].memberName && ELF_linker_memberNames[file])
        *member = ELF_linker_memberNames[file];
    else
#endif
    if (file > -1 && data_004a28dc[file].memberName)
        *member = data_004a28dc[file].fileSymbol->name;
    else
        *member = "";
    if (file > -1)
        *name = data_004a28dc[file].file->name;
    else
        *name = "linker generated symbol";
}

/* An I/O error writing the output file (1) or the link map (5). */
static void ELGC_WriteError(short error, int message) {
    if (message == 1) {
        CompilerTools_ReportLimitedDiagnostic(1, data_0049bed6);
        CompilerTools_FormatMessageAndLongjmp(0xd, error);
    } else {
        CompilerTools_ReportLimitedDiagnostic(5, data_0049bf5c.name);
        CompilerTools_FormatMessageAndLongjmp(0xd, error);
    }
}

/* Writes a buffer to its file and frees it (when the output is not written as it is made). */
/* 0x442d50 */
void ELF_gen_WriteBuffer(GList *buffer, short file, int message) {
    short error;

    if (data_0049c120 == 2)
        return;
    ShrinkGList(buffer);
    LockGList(buffer);
    error = COS_FileWrite(file, *buffer->data, buffer->size);
    if (error) {
        if (message == 1 || message == 5)
            ELGC_WriteError(error, message);
        else
            ELG_ASSERT(0x75);
    }
    FreeGList(buffer);
}

/* Creates and opens the output file and sets up the buffer the output goes to. */
/* 0x442c40 */
#if LINKER >= 27
/* (2.7: a link's output file is made after the layout's preparation, ELF_gen_WriteFile) */
Boolean ELF_gen_deferOpen;

static void ELGB_CreateOutput(void) {
    short error;

    error = PrepDumpELF_CreateFile(data_0049bed0.vRefNum, data_0049bed0.parID, data_0049bed0.name, data_0049c2f8,
                                   data_0049c2f4);
    if (error) {
        CompilerTools_ReportLimitedDiagnostic(1, data_0049bed6);
        CompilerTools_FormatMessageAndLongjmp(0xd, error);
    }
    error = Files_OpenFile(&data_0049bed0, 3, &data_004a2dcc);
    if (error) {
        CompilerTools_ReportLimitedDiagnostic(1, data_0049bed6);
        CompilerTools_FormatMessageAndLongjmp(0xd, error);
    }
}
#endif

void ELF_gen_OpenOutput(void) {
    short error;

#if LINKER >= 27
    if (ELF_gen_deferOpen)
        goto buffer;
#endif
    error = PrepDumpELF_CreateFile(data_0049bed0.vRefNum, data_0049bed0.parID, data_0049bed0.name, data_0049c2f8,
                                   data_0049c2f4);
    if (error) {
        CompilerTools_ReportLimitedDiagnostic(1, data_0049bed6);
        CompilerTools_FormatMessageAndLongjmp(0xd, error);
    }
    error = Files_OpenFile(&data_0049bed0, 3, &data_004a2dcc);
    if (error) {
        CompilerTools_ReportLimitedDiagnostic(1, data_0049bed6);
        CompilerTools_FormatMessageAndLongjmp(0xd, error);
    }
#if LINKER >= 27
buffer:
#endif
    data_004a2a70 = &data_004a21e0;
    if (ELG_OUTPUT_KIND == 3) {
        if (SRecord_InitBuffer(data_004a2a70, 0x7d00))
            CompilerTools_FormatMessageAndLongjmp(0xd, -108);
    } else {
        data_004a2a70->data = 0;
        data_004a2a70->hndlsize = 0;
        data_004a2a70->size = data_004a2a70->hndlsize;
    }
    data_004a28e4 = 0;
}

/* Closes the output file and frees the output's buffers (and those of each output section). */
/* 0x442bd0 */
void ELF_gen_CloseOutput(void) {
    ELOutput *section;

    if (data_004a2dcc) {
        Files_Close(data_004a2dcc);
        fn_00412de0(0, data_0049bed0.vRefNum);
        data_004a2dcc = 0;
    }
    FreeGList(&data_004a21e0);
    if (data_004a28e4 && data_004a28e4->sections) {
        for (section = data_004a28e4->sections; section; section = section->next) {
            FreeGList((GList *)&section->o34);
            FreeGList((GList *)&section->o44);
        }
    }
}

/* Writes the output's buffer to the output file. */
/* 0x442b90 */
void ELF_gen_FlushOutput(void) {
    if (data_004a21e0.size) {
        if (data_0049c120 == 2)
            ELG_ASSERT(0xae);
        ELF_gen_WriteBuffer(&data_004a21e0, data_004a2dcc, 1);
    }
}

/* Writes the padding to the next multiple of align in the output (and the S-record file); returns its size. */
/* 0x442ad0 */
long ELF_gen_WritePadding(unsigned long align, Boolean srecord) {
    unsigned long pad;
    short error;

    if (!align)
        return 0;
    pad = ELGC_Padding(align, data_004a2dda);
    if (data_0049c120 != 2) {
        AppendGListData(data_004a2a70, ELF_gen_PadBytes, pad);
    } else {
        error = COS_FileWrite(data_004a2dcc, ELF_gen_PadBytes, pad);
        if (error) {
            CompilerTools_ReportLimitedDiagnostic(1, data_0049bed6);
            CompilerTools_FormatMessageAndLongjmp(0xd, error);
        }
    }
    data_004a2dda += pad;
    if (srecord)
        SRecord_AddBytes((unsigned char *)ELF_gen_PadBytes, pad);
    return pad;
}

/* Skips the padding to the next multiple of align in the output's position; returns its size. */
/* 0x442a70 */
long ELF_gen_SkipPadding(unsigned long align) {
    unsigned long pad;

    if (!align)
        return 0;
    pad = ELGC_Padding(align, data_004a2dda);
    data_004a2dda += pad;
    return pad;
}

/* Adds data to a buffer, or writes it to its file when the output is written as it is made. */
/* 0x4429d0 */
void ELF_gen_AddToBuffer(GList *buffer, short file, char *data, long length, int message) {
    short error;

    if (data_0049c120 != 2) {
        AppendGListData(buffer, data, length);
    } else {
        error = COS_FileWrite(file, data, length);
        if (error) {
            if (message == 1 || message == 5)
                ELGC_WriteError(error, message);
            else
                ELG_ASSERT(0x151);
        }
    }
}

/* Writes a string and its terminating zero to the output; returns its size. */
/* 0x442950 */
unsigned long ELF_gen_WriteString(const char *string) {
    unsigned long length = strlen(string) + 1;
    short error;

    if (data_0049c120 != 2) {
        AppendGListData(data_004a2a70, string, length);
    } else {
        error = COS_FileWrite(data_004a2dcc, (void *)string, length);
        if (error) {
            CompilerTools_ReportLimitedDiagnostic(1, data_0049bed6);
            CompilerTools_FormatMessageAndLongjmp(0xd, error);
        }
    }
    data_004a2dda += length;
    return length;
}

/* Writes one byte to the output; returns 1. */
/* 0x4428d0 */
long ELF_gen_WriteByte(char byte) {
    short error;

    if (data_0049c120 != 2) {
        AppendGListByte(data_004a2a70, byte);
    } else {
        error = COS_FileWrite(data_004a2dcc, &byte, 1);
        if (error) {
            CompilerTools_ReportLimitedDiagnostic(1, data_0049bed6);
            CompilerTools_FormatMessageAndLongjmp(0xd, error);
        }
    }
    data_004a2dda++;
    return 1;
}

/* Writes data to the output; returns its size. */
/* 0x442860 */
unsigned long ELF_gen_Write(const void *data, unsigned long size) {
    short error;

    if (data_0049c120 != 2) {
        AppendGListData(data_004a2a70, data, size);
    } else {
        error = COS_FileWrite(data_004a2dcc, (void *)data, size);
        if (error) {
            CompilerTools_ReportLimitedDiagnostic(1, data_0049bed6);
            CompilerTools_FormatMessageAndLongjmp(0xd, error);
        }
    }
    data_004a2dda += size;
    return size;
}

/* Evaluates an assignment of an output section's contents; for one to ".", returns how far it moves the
   location past the section's end (an error in a section of type 8). */
/* 0x442770 */
long ELF_gen_EvalAssignment(char *name, ELTerm **expr, ELOutput *section) {
    unsigned long end;
    unsigned long moved = 0;
    ELTerm *result;

#if LINKER >= 27
    /* (2.7: a symbol's evaluated each time, the expression kept) */
    if (strcmp(name, ".")) {
        EvalExpr((ELTerm *)LCF_CopyExpr(*expr), section);
        return 0;
    }
#endif
    *expr = EvalExpr(*expr, section);
    result = *expr;
    if (result && !strcmp(name, ".")) {
        if (result->kind != 3)
            ELG_ASSERT(0x18b);
        if (result->next)
            ELG_ASSERT(0x18d);
        end = section->address + section->size;
        moved = result->value;
        if (moved < end)
            ELG_ASSERT(0x191);
        moved -= end;
        if (moved && section->o7c == 8) {
            CompilerTools_ReportLimitedDiagnostic(0x64, section->name);
            CompilerTools_FormatMessageAndLongjmp(0xd, 2);
        }
    }
    return moved;
}

#if LINKER >= 27
/* Whether the section symbol PIECE (of a prelinked file's part of a section, in CHUNK of object FILE) has a kept
   piece before the next section symbol. (Not an original function.) */
static Boolean ELGC_SectionSymbolUsed(long file, ELPiece *piece, ELChunk *chunk) {
    ELPiece *next;
    ELSymbol *symbol;

    for (next = piece->next; next && next->chunk == chunk; next = next->next) {
        symbol = next->symbol;
        if ((symbol->info & 0xf) == 3)
            return 0;
        if ((symbol->flags & 0x10) && !(symbol->flags & 0x20) && symbol->size)
            return 1;
    }
    return 0;
}
#endif

/* Lays out an output section's contents: its input sections' kept chunks and their symbols (shown in the link
   map), the assignments, extabindex, and the ends of .ctors and .dtors. */
/* 0x441e30 */
void ELF_gen_LayoutContents(ELOutput *section) {
    Boolean ctorsSeen = 0;
    Boolean dtorsSeen = 0;
    ELLinkSymbol *ctors = 0;
    ELLinkSymbol *dtors = 0;
    ELCmdItem *item;
    ELSectionRef *link;
    ELSection *input;
    ELChunk *chunk;
    ELPiece *piece;
    ELSymbol *symbol;
    ELLinkSymbol *end;
    long file;
    unsigned long value;
    unsigned long saved;
    unsigned long start;
    unsigned long align;
    char *name;
    char *fileName;
    char *member;

    CompilerTools_ReportMessage(0x29, data_0049bed6, section->name);
    EL_CHECK_BREAK();
#if LINKER >= 27
    /* (2.7's .got, its own, has no layout in the map) */
    if (strcmp(section->name, ".got"))
        LK_symbol_PrintLayoutHeader(section->name);
    LK_symbol_layoutFile = section->o5c;
    LK_symbol_layoutFixed = 0;
#else
    LK_symbol_PrintLayoutHeader(section->name);
#endif
    data_004a2a6c = 0;
    for (item = section->items; item; item = item->next) {
        /* The end of .ctors or .dtors after their last item (2.7: the Linker Generated Symbol File's). */
        if (EL_CTOR_SYMBOLS && ctors && !(item->section && !strcmp(item->section, ".ctors"))) {
            if (!data_004a2e49 || data_004a2e4f) {
                if (ctors->made) {
                    data_004a2a8c = (section->size - (data_004a2e2a - section->address)) >> 2;
                    section->size += 4;
                } else if (data_004a2a8c) {
                    CompilerTools_ReportLimitedDiagnostic(0x7b);
                    CompilerTools_Abort();
                }
            }
            ctors = 0;
        }
        if (EL_CTOR_SYMBOLS && dtors && !(item->section && !strcmp(item->section, ".dtors"))) {
            if (!data_004a2e49 || data_004a2e4f) {
                if (dtors->made) {
                    data_004a2878 = (section->size - (data_004a2e2e - section->address)) >> 2;
                    section->size += 4;
                } else if (data_004a2878) {
                    CompilerTools_ReportLimitedDiagnostic(0x7c);
                    CompilerTools_Abort();
                }
            }
            dtors = 0;
        }

        if (item->kind == 6) {
            section->size += ELF_gen_EvalAssignment(item->section, (ELTerm **)&item->expr, section);
            continue;
        }
        if (item->section && !strcmp(item->section, "extabindex")) {
            ELF_gen_SizeExtabIndex(section);
            continue;
        }
        if (EL_CTOR_SYMBOLS && item->section && !strcmp(item->section, ".ctors")) {
            if (!ctorsSeen) {
                ctorsSeen = 1;
                ctors = LinkSym_FindIndex(-10);
                if (data_004a2a8c || ctors->made) {
                    section->size = (section->size + 3) & ~3;
                    data_004a2e2a = section->address + section->size;
                } else {
                    ctors = 0;
                }
            }
        } else if (EL_CTOR_SYMBOLS && item->section && !strcmp(item->section, ".dtors")) {
            if (!dtorsSeen) {
                dtorsSeen = 1;
                dtors = LinkSym_FindIndex(-11);
                if (data_004a2878 || dtors->made) {
                    section->size = (section->size + 3) & ~3;
                    data_004a2e2e = section->address + section->size;
                } else {
                    dtors = 0;
                }
            }
        } else if (item->kind != 1) {
            continue;
        }

        for (link = item->x4; link; link = link->next) {
            input = link->section;
            file = input->file;
            if (!data_004a28dc[file].forced)
                continue;
            if (!input)
                ELG_ASSERT(0x1f9);
            if (input->output != section)
                ELG_ASSERT(0x1fb);
            if (!input->kept)
                continue;
            input->x30 = section->size;
            align = input->header->alignment - 1;
#if LINKER >= 27
            start = section->size;
            section->size = (section->size + align) & ~align;
            if (section->size != start)
                LK_symbol_PrintLayoutFill(start, section->size - start, section->address + section->size, align + 1);
#else
            section->size = (section->size + align) & ~align;
#endif
            value = 0;
            for (chunk = input->chunks; chunk; chunk = chunk->next) {
                if (!chunk->kept) {
                    value += chunk->size;
                    continue;
                }
                chunk->x14 = section->size;
                align = LK_symbol_Alignment(file, chunk->pieces->symbol) - 1;
#if LINKER >= 27
                start = section->size;
                section->size = (section->size + align) & ~align;
                if (section->size != start)
                    LK_symbol_PrintChunkFill(start, section->size - start, section->address + section->size,
                                             align + 1);
#else
                section->size = (section->size + align) & ~align;
#endif
                start = section->size;
                chunk->xc = section->address + start;
                saved = value;
                for (piece = chunk->pieces; piece && piece->chunk == chunk; piece = piece->next) {
                    symbol = piece->symbol;
                    if (symbol->flags & 0x20)
                        continue;
                    if (!(symbol->flags & 0x10)) {
                        if ((symbol->info & 0xf) != 3 && data_004a2e47) {
                            ELGC_FileNames(file, &fileName, &member);
                            LK_symbol_Print(ELGC_UNUSED_LINE, symbol->size,
                                            LK_symbol_Name(file, symbol), fileName, member);
                        }
                        continue;
                    }
                    name = LK_symbol_Name(file, symbol);
                    if (name[1] == '_' && !strncmp(LK_symbol_Name(file, symbol), "__tv$", 5)) {
                        /* An entry of the vtable list: 8 bytes each from the section's start. */
                        align = LK_symbol_Alignment(file, symbol);
                        ELGC_FileNames(file, &fileName, &member);
                        LK_symbol_PrintLayoutLine(section->size, symbol->size, data_004a2a6c + section->address,
                                                  align, LK_symbol_Name(file, symbol), fileName, member);
                        piece->x10 = data_004a2a6c + section->address;
                        data_004a2a6c += 8;
                    } else if ((symbol->info & 0xf) == 3) {
                        /* A section symbol. */
#if LINKER >= 27
                        /* (2.7: not one of a prelinked file's whose pieces are all dropped, in a program) */
                        if (!data_004a2e49 && !chunk->whole && !ELGC_SectionSymbolUsed(file, piece, chunk))
                            continue;
#endif
                        if (chunk->whole) {
                            section->size += symbol->value - value;
                            value = symbol->value;
                        }
                        ELGC_FileNames(file, &fileName, &member);
                        LK_symbol_PrintLayoutLine(section->size, symbol->size, section->size + section->address, 1,
                                                  LK_symbol_Name(file, symbol), fileName, member);
                        piece->x10 = section->size + section->address;
                    } else if (!symbol->size && EL_ENTRY_LINE(file)) {
                        /* A symbol in another: at its offset there. */
                        if (chunk->whole) {
                            section->size += symbol->value - value;
                            value = symbol->value;
                        }
                        ELGC_FileNames(file, &fileName, &member);
#if LINKER >= 27
                        LK_symbol_Print("  %08lx %06lx %08lx %08lx    %s (entry of %s) \t%s %s\r\n",
                                        piece->owner->x10 - section->address + piece->offset, symbol->size,
                                        piece->owner->x10 + piece->offset,
                                        /* (the file offset the layout is at: past the owner) */
                                        LK_symbol_layoutFile + (LK_symbol_layoutFixed ? 0 : section->size),
                                        LK_symbol_Name(file, symbol), piece->owner->name, fileName, member);
#else
                        LK_symbol_Print("  %08lx %06lx %08lx %s (entry of %s) \t%s %s\r\n",
                                        piece->owner->x10 - section->address + piece->offset, symbol->size,
                                        piece->owner->x10 + piece->offset, LK_symbol_Name(file, symbol),
                                        piece->owner->name, fileName, member);
#endif
                        piece->x10 = piece->owner->x10 + piece->offset;
                        if ((symbol->info & 0xf) == 2)
                            data_004a2218++;
                    } else {
                        align = LK_symbol_Alignment(file, symbol) - 1;
                        if (chunk->whole) {
                            section->size += symbol->value - value;
                            value = symbol->value;
                        } else {
                            section->size = (section->size + align) & ~align;
                        }
                        ELGC_FileNames(file, &fileName, &member);
                        LK_symbol_PrintLayoutLine(section->size, symbol->size, section->size + section->address,
                                                  align + 1, LK_symbol_Name(file, symbol), fileName, member);
                        piece->x10 = section->size + section->address;
                        if (!chunk->whole)
                            section->size += symbol->size;
                        if ((symbol->info & 0xf) == 2)
                            data_004a2218++;
                    }
                }
                if (chunk->whole) {
                    section->size = start;
                    section->size += chunk->size;
                }
                chunk->x10 = section->address + section->size;
                value = saved + chunk->size;
            }
        }
    }

    if (EL_CTOR_SYMBOLS && (ctors || (data_004a2e1a == section && !ctorsSeen)) && (!data_004a2e49 || data_004a2e4f)) {
        end = LinkSym_FindIndex(-10);
        if (end->made) {
            data_004a2a8c = (section->size - (data_004a2e2a - section->address)) >> 2;
            section->size += 4;
        } else if (data_004a2a8c) {
            CompilerTools_ReportLimitedDiagnostic(0x7b);
            CompilerTools_Abort();
        }
    }
    if (EL_CTOR_SYMBOLS && (dtors || (data_004a2e1e == section && !dtorsSeen)) && (!data_004a2e49 || data_004a2e4f)) {
        end = LinkSym_FindIndex(-11);
        if (end->made) {
            data_004a2878 = (section->size - (data_004a2e2e - section->address)) >> 2;
            section->size += 4;
        } else if (data_004a2878) {
            CompilerTools_ReportLimitedDiagnostic(0x7c);
            CompilerTools_Abort();
        }
    }
#if LINKER >= 27
    /* (2.7: its size rounded up to a word) */
    section->size = (section->size + 3) & ~3;
#endif
    section->o68 = section->size;
}

/* Sizes extabindex: drops the exception table entries not kept, makes the index and its section's room for it. */
/* 0x441c30 */
void ELF_gen_SizeExtabIndex(ELOutput *section) {
    long groups = 0;
    unsigned long size = (section->size + 3) & ~3;
    long total = 0;
    ELGroup *group;
    ELRoot *root;
    ELSymbol *symbol;
    ELLinkSymbol *index;
    long kept;
    long i;
    long j;
    unsigned long align;
    unsigned long bytes;

    EL_CHECK_BREAK();
    for (group = data_004a2954; group; group = group->next) {
        kept = 0;
        j = 0;
        for (i = 0; i < group->count; i++) {
            root = group->roots[i];
            symbol = root->symbol;
            if ((symbol->flags & 0x10) && !(symbol->flags & 0x20)) {
                if (i != j)
                    group->roots[j] = root;
                j++;
                kept++;
                if (!(root->target->flags & 0x10) || (root->target->flags & 0x20))
                    ELG_ASSERT(0x2c6);
                align = LK_symbol_Alignment(root->file, symbol) - 1;
                size = (size + align) & ~align;
                size += symbol->size;
            }
        }
        total += kept;
        group->count = kept;
        if (kept)
            groups++;
    }
    data_004a28d4 = total;
    if (total) {
        index = LinkSym_FindIndex(-12);
        if (!data_004a2e49 || data_004a2e4f) {
#if LINKER >= 27
            /* (2.7: exception tables kept, its _eti_init_info is not (an older __init_cpp_exceptions): an error) */
            if (!(index->piece->symbol->flags & 0x10)) {
                CompilerTools_ReportLimitedDiagnostic(0xb8, index->name, "__init_cpp_exceptions.cpp");
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
            }
#endif
            size = (size + 3) & ~3;
            groups++;
            groups <<= 4;
            index->piece->symbol->size = groups;
            data_004a2e06 = galloc(groups);
            memset(data_004a2e06, 0, groups);
            bytes = index->piece->symbol->size;
            size += bytes;
            bytes = ((bytes >> 4) - 1) * 0x24;
            data_004a2e12 = galloc(bytes);
            memset(data_004a2e12, 0, bytes);
        } else {
            index->piece->symbol->size = 0;
            index->piece->symbol->info = 0x10;
        }
    }
    data_004a2e22 = section->size;
    if (data_004a28d4)
        section->size = size;
    data_004a2e26 = section->size;
}

/* Places extabindex's entries and the index itself, shown in the link map with the entries not kept. */
/* 0x441970 */
void ELF_gen_MapExtabIndex(ELOutput *section) {
    unsigned long offset = data_004a2e22;
    ELGroup *group;
    ELRoot *root;
    ELSymbol *symbol;
    ELLinkSymbol *index;
    unsigned long align;
    long i;
    char *fileName;
    char *member;

    if (!data_004a28d4)
        return;
    ELF_gen_SortExceptionEntries();
    CompilerTools_ReportMessage(0x29, data_0049bed6, section->name);
    LK_symbol_PrintLayoutHeader(section->name);
#if LINKER >= 27
    /* (2.7 shows the section's end in the file for each of these) */
    LK_symbol_layoutFile = section->o5c + section->size;
    LK_symbol_layoutFixed = 1;
#endif
    offset = (offset + 3) & ~3;
    EL_CHECK_BREAK();
    for (group = data_004a2954; group; group = group->next) {
        for (i = 0; i < group->count; i++) {
            root = group->roots[i];
            symbol = root->symbol;
            align = LK_symbol_Alignment(root->file, symbol) - 1;
            offset = (offset + align) & ~align;
            ELGC_FileNames(root->file, &fileName, &member);
            LK_symbol_PrintLayoutLine(offset, symbol->size, offset + section->address, align + 1,
                                      LK_symbol_Name(root->file, symbol), fileName, member);
            symbol->piece->x10 = offset + section->address;
            offset += symbol->size;
        }
    }
    if (data_004a28d4 && (!data_004a2e49 || data_004a2e4f)) {
        offset = (offset + 3) & ~3;
        index = LinkSym_FindIndex(-12);
        index->piece->x10 = offset + section->address;
        index->piece->symbol->section = (unsigned short)section->o2c;
#if LINKER >= 27
        /* (2.7's index is a piece of its Linker Generated Symbol File) */
        LK_symbol_PrintLayoutLine(offset, index->piece->symbol->size, offset + section->address, 4, index->name,
                                  "Linker Generated Symbol File", "");
#endif
        offset += index->piece->symbol->size;
    }
    for (root = data_004a2948; root; root = root->next) {
        if (!data_004a28dc[root->file].forced)
            continue;
        symbol = root->symbol;
        if ((symbol->flags & 0x10) && !(symbol->flags & 0x20))
            continue;
        if (!data_004a2e47)
            continue;
        ELGC_FileNames(root->file, &fileName, &member);
        LK_symbol_Print(ELGC_UNUSED_LINE, symbol->size, LK_symbol_Name(root->file, symbol),
                        fileName, member);
    }
    if (offset != data_004a2e26)
        ELG_ASSERT(0x333);
}

/* Sorts each output section's exception table entries by their functions' addresses (a shell sort). */
/* 0x4417f0 */
void ELF_gen_SortExceptionEntries(void) {
    ELGroup *group;
    ELRoot *root;
    ELRoot *entry;
    ELSymbol *target;
    long count;
    long gap;
    long i;
    long j;

    EL_CHECK_BREAK();
    for (group = data_004a2954; group; group = group->next) {
        count = group->count;
        if (!count)
            continue;
        for (i = 0; i < count; i++) {
            root = group->roots[i];
            target = root->target;
            if (!(target->flags & 0x10) || (target->flags & 0x20))
                ELG_ASSERT(0x349);
            root->x18 = ELF_gen_SymbolAddress(target, root->file);
        }
        gap = 1;
        while (gap * 3 + 1 < count)
            gap = gap * 3 + 1;
        for (; gap >= 1; gap = (gap - 1) / 3) {
            for (i = gap; i < count; i++) {
                entry = group->roots[i];
                for (j = i; j >= gap && (unsigned long)group->roots[j - gap]->x18 > (unsigned long)entry->x18; j -= gap)
                    group->roots[j] = group->roots[j - gap];
                group->roots[j] = entry;
            }
        }
    }
}

/* Lays out an output section with no bytes in the file: its contents' size is kept apart (o8c) and checks it
   against the section it is addressed from (oac, within 64K). */
/* 0x4416b0 */
void ELF_gen_LayoutBssSection(ELOutput *section) {
    unsigned long pad;
    unsigned long distance;
    ELOutput *base;

    if (!section->align74)
        pad = 0;
    else
        pad = ELF_gen_SkipPadding(section->align74);
    section->o5c = data_004a2dda;
    section->o88 = section->o5c;
    ELF_gen_LayoutContents(section);
    section->o8c = section->o68;
    section->o68 = 0;
    base = (ELOutput *)section->oac;
    if (base && !data_004a2e3e) {
        distance = section->address;
        if (base->size) {
            if (distance < base->address) {
                CompilerTools_ReportLimitedDiagnostic(0x87, base->name, section->name);
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
            }
            distance -= base->address;
        } else {
            distance = 0;
        }
        distance += section->size;
        if (distance > 0xffff) {
            CompilerTools_ReportLimitedDiagnostic(0x27, ((ELOutput *)section->oac)->name, section->name);
            CompilerTools_FormatMessageAndLongjmp(0xd, 2);
        }
    }
    if (section->excluded) {
        section->o88 = 0;
        section->o8c = 0;
        return;
    }
    if (!section->size) {
        data_004a2dda -= pad;
        section->o88 = 0;
#if LINKER < 27
        /* (2.7's offset stays the aligned one) */
        section->o5c -= pad;
#endif
    }
}

/* Lays out an output section with bytes in the file: its file offset (o88, o5c), its contents, and the check
   against the section it is addressed from (oac, within 64K). */
/* 0x441560 */
void ELF_gen_LayoutSection(ELOutput *section) {
    unsigned long pad;
    unsigned long distance;
    unsigned long address;
    ELOutput *base;

    if (!section->align74)
        pad = 0;
    else
        pad = ELF_gen_SkipPadding(section->align74);
    section->o5c = data_004a2dda;
    section->o88 = section->o5c;
    ELF_gen_LayoutContents(section);
    base = (ELOutput *)section->oac;
    if (base && !data_004a2e3e) {
        address = section->address;
        if (base->size) {
            distance = base->address;
            if (distance < address) {
                CompilerTools_ReportLimitedDiagnostic(0x87, section->name, base->name);
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
            }
            distance -= address;
        } else {
            distance = section->size;
        }
        distance += ((ELOutput *)section->oac)->size;
        if (distance > 0xffff) {
            CompilerTools_ReportLimitedDiagnostic(0x27, section->name, ((ELOutput *)section->oac)->name);
            CompilerTools_FormatMessageAndLongjmp(0xd, 2);
        }
    }
    if (ELG_OUTPUT_KIND == 0 && section == data_004a2dde)
        ELF_PPC_EABI_LayoutLinkerData(section);
    if (section->excluded) {
        section->o88 = 0;
        return;
    }
    data_004a2dda += section->o68;
    if (!section->size) {
        data_004a2dda -= pad;
        section->o88 = 0;
#if LINKER < 27
        /* (2.7's offset stays the aligned one) */
        section->o5c -= pad;
#endif
    }
}

#if LINKER >= 27
/* 2.7: lays out a section with no inputs: only its (empty) layout in the map, at its aligned file offset. */
void ELF_gen_LayoutNoInputs(ELOutput *section) {
    section->o5c = data_004a2dda;
    if (section->align74)
        section->o5c += ELGB_PadTo(data_004a2dda, section->align74);
    section->o88 = 0;
    ELF_gen_LayoutContents(section);
}
#endif

/* Lays out an assignment of SECTIONS: its address from its MEMORY area (or its GROUP's), and the bytes an
   assignment to "." adds. */
/* 0x4414b0 */
void ELF_gen_LayoutAssignment(ELOutput *section) {
    ELMemory *memory;
    long moved;

    section->o88 = data_004a2dda;
    if (section->memory)
        memory = section->memory;
    else if (section->parent)
        memory = section->parent->memory;
    else
        ELG_ASSERT(0x3ce);
    section->address = memory->origin + memory->used;
    if (data_004a2e45) {
        section->romAddress = data_004a2dd2;
        section->oa0 = data_004a2dd6;
    } else {
        section->romAddress = section->address;
        section->oa0 = section->address;
    }
    moved = ELF_gen_EvalAssignment(section->name, (ELTerm **)&section->expr, section);
    section->size += moved;
    section->o68 += moved;
    data_004a2dda += moved;
    if (!moved)
        section->o88 = 0;
}

/* Writes the fill of an assignment to "." (the section's fill value, a short, from its own "at" or its
   GROUP's); returns the bytes written. */
/* 0x441300 */
long ELF_gen_WriteFill(char *name, ELTerm **expr, ELOutput *section) {
    long written = 0;
    short fill = 0; /* (not set in the original when the section has neither a MEMORY area nor a GROUP) */
    unsigned long target;
    unsigned long at;
    unsigned long size;
    unsigned long count;
    unsigned long address;
    short *buffer;
    short *p;
    char *data;

    if (section->excluded)
        return 0;
    if (*expr && !strcmp(name, ".")) {
        if ((*expr)->kind != 3)
            ELG_ASSERT(0x3eb);
        if (section->memory)
            fill = (short)section->at;
        else if (section->parent)
            fill = (short)section->parent->at;
        target = (*expr)->value;
        at = section->address + section->o8c;
        size = target - at;
        count = (size >> 1) + 1;
        if (count) {
            buffer = galloc(count * 2);
            for (p = buffer; count; count--)
                *p++ = fill;
            data = (char *)buffer;
            if (size & 1)
                data++;
            written = size;
            ELF_gen_Write(data, size);
            section->o8c += size;
            if (data_004a2e3a) {
                address = target - section->address + section->oa0;
                SRecord_Start(address, 3);
                SRecord_AddBytes((unsigned char *)data, size);
                address = address - section->address + section->oa0;
                section->o30 = address;
                SRecord_End();
            }
        }
    }
    return written;
}

/* Writes an assignment to ".": the padding to its alignment and its fill. */
/* 0x441190 */
void ELF_gen_WriteAssignment(ELOutput *section) {
    unsigned long align;
    long position;
    long pad;
    short error;

#if LINKER >= 27
    /* (2.7: a symbol's evaluated again when written) */
    if (section->name && strcmp(section->name, "."))
        ELF_gen_EvalAssignment(section->name, (ELTerm **)&section->expr, section);
#endif
    if (!section->o88)
        return;
    data_004a2dda = section->o88;
    if (data_0049c120 == 2) {
        align = section->align74;
        error = COS_FileGetPos(data_004a2dcc, &position);
        if (error) {
            CompilerTools_ReportLimitedDiagnostic(1, data_0049bed6);
            CompilerTools_FormatMessageAndLongjmp(0xd, error);
        }
        if (align) {
            pad = ELGC_Padding(align, position);
            error = COS_FileWrite(data_004a2dcc, ELF_gen_PadBytes, pad);
            if (error) {
                CompilerTools_ReportLimitedDiagnostic(1, data_0049bed6);
                CompilerTools_FormatMessageAndLongjmp(0xd, error);
            }
        }
        error = COS_FileGetPos(data_004a2dcc, &position);
        if (error) {
            CompilerTools_ReportLimitedDiagnostic(1, data_0049bed6);
            CompilerTools_FormatMessageAndLongjmp(0xd, error);
        }
        if (section->o88 != position)
            ELG_ASSERT(0x412);
    }
    if (section->expr)
        ELF_gen_WriteFill(section->name, (ELTerm **)&section->expr, section);
    else
        ELG_ASSERT(0x417);
}

/* Writes the padding before an output section in the output file (when it is written as it is made). */
/* 0x4410b0 */
void ELF_gen_AlignSectionInFile(ELOutput *section) {
    unsigned long align;
    long position;
    long pad;
    short error;

#if LINKER >= 27
    /* (2.7: a section written) */
    if (section->size)
#endif
    CompilerTools_ReportMessage(0x23, data_0049bed6, section->name);
    if (data_0049c120 != 2 || !section->size)
        return;
    align = section->align74;
    error = COS_FileGetPos(data_004a2dcc, &position);
    if (error) {
        CompilerTools_ReportLimitedDiagnostic(1, data_0049bed6);
        CompilerTools_FormatMessageAndLongjmp(0xd, error);
    }
    if (align) {
        pad = ELGC_Padding(align, position);
        error = COS_FileWrite(data_004a2dcc, ELF_gen_PadBytes, pad);
        if (error) {
            CompilerTools_ReportLimitedDiagnostic(1, data_0049bed6);
            CompilerTools_FormatMessageAndLongjmp(0xd, error);
        }
    }
}

#if LINKER >= 27
/* A partial link's empty command file section: the file padded with zeros to its alignment (2.7's; no header). */
void ELF_gen_PadEmptySection(ELOutput *section) {
    static char zeros[0x40];
    unsigned long align;
    long position;
    long pad;
    long n;

    if (data_0049c120 != 2)
        return;
    align = section->align98;
    if (!align)
        return;
    if (COS_FileGetPos(data_004a2dcc, &position))
        return;
    pad = ((position + align - 1) & ~(align - 1)) - position;
    for (; pad > 0; pad -= n) {
        n = pad < (long)sizeof(zeros) ? pad : (long)sizeof(zeros);
        if (COS_FileWrite(data_004a2dcc, zeros, n))
            return;
    }
}
#endif

/* Lays out a DWARF section: the size of the first INCLUDEDWARF object's section of that name, then the kept
   chunks of its input sections (from objects with DWARF 2 comments, their symbols shown in the link map). */
/* 0x440ca0 */
void ELF_gen_LayoutDwarfSection(ELOutput *section) {
    unsigned long size = 0;
    ELFileName *dwarf;
    ELObject *object;
    ELSection *input;
    ELCmdItem *item;
    ELSectionRef *link;
    ELChunk *chunk;
    ELPiece *piece;
    ELSymbol *symbol;
    unsigned long offset;
    long file;
    long i;
    char *fileName;
    char *member;

    CompilerTools_ReportMessage(0x29, data_0049bed6, section->name);
    EL_CHECK_BREAK();
#if LINKER != 13
    /* (1.3.2 does not map the DWARF sections) */
    LK_symbol_PrintLayoutHeader(section->name);
#endif
#if LINKER >= 27
    LK_symbol_layoutFile = section->o5c;
    LK_symbol_layoutFixed = 0;
#endif
    if (!data_004a2e49) {
        for (dwarf = data_004a28e4->includeDwarf; dwarf; dwarf = dwarf->next) {
            if (!dwarf->index)
                continue;
            object = &data_004a28dc[dwarf->index - 1];
            for (i = 1; i < object->image->section_header_count; i++) {
                input = &object->sections[i];
                if (input->name && section->name && !strcmp(input->name, section->name)) {
                    size += input->header->size;
                    break;
                }
            }
            break;
        }
    }
    for (item = section->items; item; item = item->next) {
        if (item->kind != 2)
            continue;
        for (link = item->x4; link; link = link->next) {
            input = link->section;
            file = input->file;
            object = &data_004a28dc[file];
            if (!object->forced || !input->kept)
                continue;
            for (chunk = input->chunks; chunk; chunk = chunk->next) {
                chunk->xc = size;
                if (data_004a2e44 && chunk->pieces)
                    chunk->pieces->x10 = chunk->xc;
                chunk->x10 = chunk->xc + chunk->size;
                size = chunk->x10;
#if LINKER == 13
                continue;
#endif
                if (!object->newDwarf)
                    continue;
                offset = chunk->xc;
                for (piece = chunk->pieces; piece && piece->chunk == chunk; piece = piece->next) {
                    symbol = piece->symbol;
                    if (symbol->flags & 0x20)
                        continue;
                    if (symbol->flags & 0x10) {
                        ELGC_FileNames(file, &fileName, &member);
                        if ((symbol->info & 0xf) == 3) {
                            LK_symbol_PrintLayoutLine(offset, symbol->size, offset, 1, LK_symbol_Name(file, symbol),
                                                      fileName, member);
                            piece->x10 = offset;
                        } else {
                            LK_symbol_PrintLayoutLine(offset, symbol->size, offset,
                                                      LK_symbol_Alignment(file, symbol),
                                                      LK_symbol_Name(file, symbol), fileName, member);
                            piece->x10 = offset;
                            offset += symbol->size;
                        }
                    } else if ((symbol->info & 0xf) != 3 && data_004a2e47) {
                        ELGC_FileNames(file, &fileName, &member);
                        LK_symbol_Print(ELGC_UNUSED_LINE, symbol->size,
                                        LK_symbol_Name(file, symbol), fileName, member);
                    }
                }
            }
            /* (the original reads the first chunk even when there is none) */
            input->header->address = input->chunks->pieces->chunk->xc;
        }
    }
    section->o88 = size;
}

/* Lays out a DWARF section when -g's DWARF is linked, the dead code's lines stripped first for its section. */
/* 0x440c70 */
void ELF_gen_LayoutDwarf(ELOutput *section) {
    if (!data_004a2e37)
        return;
    if (section == data_004a2dea)
        DWARF_line_StripDeadCode(section);
#if LINKER >= 27
    /* (2.7 maps the DWARF sections with -listdwarf only) */
    {
        extern Boolean ELF_linker_listDwarf;
        Boolean map = data_0049c116;

        if (!ELF_linker_listDwarf)
            data_0049c116 = 0;
        ELF_gen_LayoutDwarfSection(section);
        data_0049c116 = map;
    }
#else
    ELF_gen_LayoutDwarfSection(section);
#endif
}

/* The symbol a reference names (for an undefined or dropped one: the global symbol of its name, an error when
   there is none unless the link is partial). */
/* 0x440b80 */
void ELF_gen_ResolveSymbol(ELSymbol *symbol, long file, ELSymbol **result, long *resultFile) {
    ELPiece *global;
    char *objectName;
    char *name;

    if (symbol->section == 0 || (symbol->flags & 0x20)) {
        global = LK_symbol_Find(LK_symbol_Name(file, symbol), file);
        if (data_004a2e49) {
            if (!global) {
                *result = 0;
                *resultFile = 0;
                return;
            }
        } else {
            if (file > -1)
                objectName = data_004a28dc[file].file->name;
            else
                objectName = "linker generated symbol";
            name = LK_symbol_Name(file, symbol);
            if (!global && !data_004a2e49) {
                CompilerTools_ReportLimitedDiagnostic(0x3a, name, objectName);
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
            }
        }
        *result = global->symbol;
        *resultFile = global->file;
    } else {
        *result = symbol;
        *resultFile = file;
    }
}

/* The symbol a reference names, or none (no error). */
/* 0x440b10 */
void ELF_gen_ResolveSymbolQuiet(ELSymbol *symbol, long file, ELSymbol **result, long *resultFile) {
    ELPiece *global;

    if (symbol->section == 0 || (symbol->flags & 0x20)) {
        global = LK_symbol_Find(LK_symbol_Name(file, symbol), file);
        if (!global) {
            *result = 0;
            *resultFile = 0;
            return;
        }
        *result = global->symbol;
        *resultFile = global->file;
    } else {
        *result = symbol;
        *resultFile = file;
    }
}

/* The address of the symbol a reference names. */
/* 0x440a90 */
unsigned long ELF_gen_SymbolAddress(ELSymbol *symbol, long file) {
    ELSymbol *found;
    long foundFile;

    ELF_gen_ResolveSymbol(symbol, file, &found, &foundFile);
    if (foundFile < 0)
        return LinkSym_FindIndex(foundFile)->piece->x10;
    if (found->section == 0xfff1)
        return found->value;
    if ((found->info & 0xf) == 3)
        return found->piece->chunk->xc;
    return found->piece->x10;
}

/* Applies the relocations of an output section's input sections. */
/* 0x440a30 */
void ELF_gen_ApplyRelocations(ELOutput *section) {
    ELCmdItem *item;
    ELSectionRef *link;
    ELSection *input;
    long file;

    for (item = section->items; item; item = item->next) {
        if (item->kind != 1)
            continue;
        for (link = item->x4; link; link = link->next) {
            input = link->section;
            file = input->file;
            if (data_004a28dc[file].forced && input->relocations)
                ELF_PPC_EABI_ApplyRelocations(file, input);
        }
    }
}

/* Writes extabindex: the exception table entries kept, then the index (per output section: its first entry,
   the end of its last, its first function and the size of its functions). */
/* 0x4403b0 */
void ELF_gen_WriteExtabIndex(ELOutput *section) {
    ELLinkSymbol *index = LinkSym_FindIndex(-12);
    ELGroup *group;
    ELGCGroupEnds *ends;
    ELRoot *root;
    ELRoot *last;
    ELSymbol *symbol;
    ELSymbol *lastTarget;
    unsigned long *table;
    unsigned long size;
    unsigned long address;
    char *data;
    long i;
    long count;

    if (!data_004a28d4)
        return;
    section->o8c += ELF_gen_WritePadding(4, data_004a2e3a);
    EL_CHECK_BREAK();
    for (group = data_004a2954; group; group = group->next) {
        for (i = 0; i < group->count; i++) {
            root = group->roots[i];
            symbol = root->symbol;
            section->o8c += ELF_gen_WritePadding(LK_symbol_Alignment(root->file, symbol), data_004a2e3a);
            size = symbol->size;
            data = data_004a28dc[root->file].sections[symbol->section].data + symbol->value;
            ELF_gen_Write(data, size);
            section->o8c += size;
            if (data_004a2e3a) {
                address = ELF_gen_SymbolAddress(symbol, root->file);
                SRecord_Start(address - section->address + section->oa0, 3);
                SRecord_AddBytes((unsigned char *)data, symbol->size);
                section->o30 = symbol->piece->x10 - section->address + section->oa0 + symbol->size;
                SRecord_End();
            }
        }
    }
    if (data_004a2e49 && !data_004a2e4f)
        return;

    for (group = data_004a2954; group; group = group->next) {
        if (!group->count)
            continue;
        last = 0;
        lastTarget = 0;
        for (i = 0; i < group->count; i++) {
            last = group->roots[i];
            lastTarget = last->target;
        }
        ends = (ELGCGroupEnds *)group->x10;
        ends->first = group->roots[0];
        ends->firstTarget = ends->first->target;
        ends->last = last;
        ends->lastTarget = lastTarget;
    }
    count = 0;
    table = (unsigned long *)data_004a2e06;
    for (group = data_004a2954; group; group = group->next) {
        if (!group->count)
            continue;
        ends = (ELGCGroupEnds *)group->x10;
        if (!data_004a2e49) {
            table[0] = CTool_EndianConvertWord32(ends->first->symbol->piece->x10);
            table[1] = CTool_EndianConvertWord32(ends->last->symbol->piece->x10 + ends->last->symbol->size);
            table[2] = CTool_EndianConvertWord32(ends->firstTarget->piece->x10);
        }
        table[3] = CTool_EndianConvertWord32(ends->lastTarget->piece->x10 - ends->firstTarget->piece->x10 +
                                             ends->lastTarget->size);
        table += 4;
        count++;
    }
    if (!data_004a28d4)
        return;
    if (count != (long)((index->piece->symbol->size >> 4) - 1))
        ELG_ASSERT(0x54e);
    section->o8c += ELF_gen_WritePadding(4, data_004a2e3a);
    size = index->piece->symbol->size;
    ELF_gen_Write(data_004a2e06, size);
    section->o8c += size;
    if (data_004a2e3a) {
        SRecord_AddBytes((unsigned char *)data_004a2e06, index->piece->symbol->size);
        SRecord_End();
    }
}

/* Writes one output section's contents (kept chunks and pieces, aligned; .ctors/.dtors terminators; S-records). */
/* 0x43f580 */
void ELF_gen_WriteSection(ELOutput *section) {
    Boolean ctorsSeen;
    Boolean dtorsSeen;
    ELLinkSymbol *ctorsEnd;
    ELLinkSymbol *dtorsEnd;
    ELCmdItem *item;
    ELSectionRef *node;
    ELSection *input;
    ELChunk *chunk;
    ELPiece *piece;
    ELSymbol *symbol;
    ELSymbol *target;
    long file;
    long index;
    unsigned long offset;
    unsigned long pos;
    unsigned long size;
    unsigned long value;
    char *data;

    ctorsSeen = 0;
    dtorsSeen = 0;
    ctorsEnd = NULL;
    dtorsEnd = NULL;
    if (!section->o88 || section->excluded)
        return;

    data_004a2dda = section->o88;
    CompilerTools_ReportMessage(0x23, data_0049bed6, section->name);
    if (data_0049c120 == 2) {
        ELGB_PadFile(section->align74);
        if (section->o88 != ELGB_GetPos())
            ELG_ASSERT(0x573);
    }
    if (!data_004a2e49)
        ELF_gen_ApplyRelocations(section);

    for (item = section->items; item; item = item->next) {
        /* The terminator of .ctors (.dtors) comes after the last of the items that name it. */
        if (ctorsEnd && !(item->section && !strcmp(item->section, ".ctors"))) {
            if (!data_004a2e49 || data_004a2e4f)
                ELGB_ZeroWord(section);
            ctorsEnd = NULL;
        }
        if (dtorsEnd && !(item->section && !strcmp(item->section, ".dtors"))) {
            if (!data_004a2e49 || data_004a2e4f)
                ELGB_ZeroWord(section);
            dtorsEnd = NULL;
        }

        if (item->kind == 6) {
            ELF_gen_WriteFill(item->section, (ELTerm **)&item->expr, section);
            continue;
        }
        if (item->section && !strcmp(item->section, "extabindex")) {
            ELF_gen_WriteExtabIndex(section);
            continue;
        }
        if (EL_CTOR_SYMBOLS && item->section && !strcmp(item->section, ".ctors")) {
            if (!ctorsSeen) {
                ctorsSeen = 1;
                ctorsEnd = LinkSym_FindIndex(-10);
                if (!data_004a2a8c && !ctorsEnd->made)
                    ctorsEnd = NULL;
                else
                    ELGB_AlignOutput(section, 4);
            }
        } else if (EL_CTOR_SYMBOLS && item->section && !strcmp(item->section, ".dtors")) {
            if (!dtorsSeen) {
                dtorsSeen = 1;
                dtorsEnd = LinkSym_FindIndex(-11);
                if (!data_004a2878 && !dtorsEnd->made)
                    dtorsEnd = NULL;
                else
                    ELGB_AlignOutput(section, 4);
            }
        } else if (item->kind != 1) {
            continue;
        }

        for (node = (ELSectionRef *)item->x4; node; node = node->next) {
            input = node->section;
            file = input->file;
            if (!data_004a28dc[file].forced)
                continue;
            if (!input)
                ELG_ASSERT(0x5b5);
            if (input->output != section)
                ELG_ASSERT(0x5b7);
            if (!input->kept)
                continue;

            ELGB_AlignOutput(section, input->header->alignment);
            offset = 0;
            for (chunk = input->chunks; chunk; chunk = chunk->next) {
                if (chunk->kept) {
                    ELGB_AlignOutput(section, LK_symbol_Alignment(file, chunk->pieces->symbol));
                    if (chunk->whole) {
                        /* The chunk as it is in the input section. */
                        size = chunk->size;
                        data = input->data + offset;
                        pos = section->o8c;
                        ELGB_Emit(data, size);
                        section->o8c += size;
                        if (data_004a2e3a) {
                            SRecord_Start(pos + section->oa0, 3);
                            SRecord_AddBytes((unsigned char *)data, size);
                            section->o30 = pos + section->oa0 + size;
                            SRecord_End();
                        }
                    } else {
                        /* Its pieces that are kept, each aligned. */
                        for (piece = chunk->pieces; piece && piece->chunk == chunk; piece = piece->next) {
                            symbol = piece->symbol;
                            if (!symbol->size || (symbol->flags & 0x20) || (symbol->info & 0xf) == 3 ||
                                !(symbol->flags & 0x10))
                                continue;
                            if (data_004a2904 && LK_symbol_Name(file, symbol)[1] == '_' &&
                                !strncmp(LK_symbol_Name(file, symbol), "__tv$", 5))
                                continue;

                            ELGB_AlignOutput(section, LK_symbol_Alignment(file, symbol));
                            size = symbol->size;
                            data = data_004a28dc[file].sections[symbol->section].data + symbol->value;
                            ELGB_Emit(data, size);
                            section->o8c += size;
                            if (data_004a2e3a) {
                                ELF_gen_ResolveSymbol(symbol, file, &target, &index);
                                if (index < 0)
                                    value = LinkSym_FindIndex(index)->piece->x10;
                                else if (target->section == 0xfff1)
                                    value = target->value;
                                else if ((target->info & 0xf) == 3)
                                    value = target->piece->chunk->xc;
                                else
                                    value = target->piece->x10;
                                SRecord_Start(value - section->address + section->oa0, 3);
                                SRecord_AddBytes((unsigned char *)(data_004a28dc[file]
                                                                       .sections[symbol->section]
                                                                       .data +
                                                                   symbol->value),
                                                 symbol->size);
                                section->o30 = symbol->piece->x10 - section->address + section->oa0 + symbol->size;
                                SRecord_End();
                            }
                        }
                    }
                }
                offset += chunk->size;
            }
        }
    }

    if (EL_CTOR_SYMBOLS && (ctorsEnd || (data_004a2e1a == section && !ctorsSeen)) && (!data_004a2e49 || data_004a2e4f) &&
        LinkSym_FindIndex(-10)->made)
        ELGB_ZeroWord(section);
    if (EL_CTOR_SYMBOLS && (dtorsEnd || (data_004a2e1e == section && !dtorsSeen)) && (!data_004a2e49 || data_004a2e4f) &&
        LinkSym_FindIndex(-11)->made)
        ELGB_ZeroWord(section);
#if LINKER >= 27
    ELGB_AlignOutput(section, 4);
#endif

    if (!ELG_OUTPUT_KIND && section == data_004a2dde)
        ELF_PPC_EABI_WriteLinkerData(section);
}

/* Writes a section's program header (0x58..0x77) with the type and address given, swapping as asked. */
/* 0x43f490 */
void ELF_gen_WriteProgramHeader(ELOutput *section, Boolean swapRest, Boolean swapGiven, unsigned long type,
                                unsigned long address) {
    unsigned long oldAddress;
    unsigned long oldType;

    oldAddress = section->address;
    oldType = section->o58;
    if (swapGiven) {
        address = CTool_EndianConvertWord32(address);
        type = CTool_EndianConvertWord32(type);
    }
    section->address = address;
    section->o58 = type;
    if (swapRest) {
        section->o5c = CTool_EndianConvertWord32(section->o5c);
        section->o64 = CTool_EndianConvertWord32(section->o64);
        if (!type)
            section->o68 = 0;
        section->o68 = CTool_EndianConvertWord32(section->o68);
        section->size = CTool_EndianConvertWord32(section->size);
        section->o70 = CTool_EndianConvertWord32(section->o70);
        section->align74 = CTool_EndianConvertWord32(section->align74);
    }
    ELGB_Write(&section->o58, 0x20);
    section->address = oldAddress;
    section->o58 = oldType;
}

/* The memory map's check of one loaded section against the others, the stack and the heap. */
static void ELGB_CheckOverlaps(ELOutput *current, long *romSlot, long stackAddr, long stackEnd, long heapAddr,
                               long heapEnd) {
    ELOutput *other;
    long start;
    long end;
    long j;

    start = current->address;
    end = start + (long)current->size;
    if (data_004a2e45 && data_004a2a80 != data_004a28f8)
        *romSlot = current->oa0;
    if (data_004a2e45 && current->romAddress != (unsigned long)start && start >= (long)current->romAddress &&
        start < (long)(current->size + current->romAddress))
        CompilerTools_ReportDiagnostic(0x86, current->name, current->name);

    for (j = 0; (other = data_004a2a88[j]); j++) {
        if (other == current || !other->type || !other->size)
            continue;
        if (start >= (long)other->address && start < (long)(other->size + other->address))
            CompilerTools_ReportDiagnostic(0x2a, current->name, other->name);
        if (data_004a2e45 && data_004a2a80 != data_004a28f8 && *romSlot >= (long)other->romAddress &&
            *romSlot < (long)(other->romAddress + other->size))
            CompilerTools_ReportDiagnostic(0x85, current->name, other->name);
        if (data_004a2e45 && start >= (long)other->romAddress && start < (long)(other->romAddress + other->size))
            CompilerTools_ReportDiagnostic(0x86, other->name, current->name);
    }

    if ((start >= stackEnd && start < stackAddr) || (end > stackEnd && end <= stackAddr))
        CompilerTools_ReportDiagnostic(0x2a, current->name, "stack");
    if ((start >= heapAddr && start < heapEnd) || (end > heapAddr && end <= heapEnd))
        CompilerTools_ReportDiagnostic(0x2a, current->name, "heap");
}

/* Writes a section header (an Elf32Section copy, swapped) and counts it. */
static void ELGB_WriteSectionHeader(ELOutput *section) {
    Elf32Section header;

    memcpy(&header, &section->o78, sizeof(header));
#if LINKER >= 27
    /* (2.7: an empty section's offset is where it would be) */
    if (section->kind == 1 && !section->size && !header.file_offset)
        header.file_offset = section->o5c;
    /* (2.7: a ROM image's loaded section is at its ROM address) */
    if (data_004a2e45 && section->kind == 1 && header.type != 8 && section->type)
        header.address = section->romAddress;
#endif
    ELF_Endian_SwapElf32Section(&header);
    ELGB_Write(&header, sizeof(header));
    data_004a289c.section_header_count++;
}

/* Writes the ELF file: header, program headers, sections, section headers, overlap checks and memory map. */
/* 0x43e410 */
void ELF_gen_WriteFile(void) {
    long headers;
    long i;
    ELOutput *section;
    unsigned long type;
    long pad;
    unsigned long end;
    long stackAddr;
    long stackEnd;
    long heapAddr;
    long heapEnd;
    long rom;
    ELOutputNode *node;
    ELLinkSymbol *symbol;
    Boolean same;

    headers = 0;
    ELF_PPC_EABI_PrepareLayout();
#if LINKER >= 27
    if (ELF_gen_deferOpen) {
        ELF_gen_deferOpen = 0;
        ELGB_CreateOutput();
    }
#endif
    DWARF_line_Compact();
    ELF_gen_CheckLinkSymbols();
    ELF_gen_OrderSymbols();

    /* The header, then the program headers (their first version), then the padding to 16. */
    ELGB_Write(&data_004a289c, 0x34);
    data_004a2dda += 0x34;
    data_004a289c.program_header_offset = data_004a2dda;
    if (!data_004a2e49) {
        for (i = 0; (section = data_004a2a88[i]); i++) {
            if (!section->oa8 || section->kind != 1)
                continue;
            type = section->o58;
            if (section->excluded)
                type = 0;
#if LINKER >= 27
            /* (2.7: a ROM image's BSS has one, at its address) */
            if (data_004a2e45 && section->o7c != 8) {
#else
            if (data_004a2e45) {
#endif
                ELF_gen_WriteProgramHeader(section, 0, 0, type, section->oa0);
                data_004a2dda += 0x20;
                ELF_gen_WriteProgramHeader(section, 0, 0, 0, section->address);
                headers += 2;
            } else {
                ELF_gen_WriteProgramHeader(section, 0, 0, type, section->address);
                headers++;
            }
            data_004a2dda += 0x20;
        }
    }
    pad = ELGB_PadTo(data_004a2dda, 0x10);
    ELGB_GrowPad(pad);
    data_004a2dda += pad;
    ELGB_PadFile(0x10);
#if LINKER >= 27
    ELF_gen_headersSize = data_004a2dda;
#endif

    data_004a2218 = 0;
    for (i = 0; (section = data_004a2a88[i]); i++) {
        ELF_PPC_EABI_PlaceSection(section);
        if (section->o54)
            ((void (*)(ELOutput *))section->o54)(section);
        ELF_PPC_EABI_AdvanceMemory(section);
    }
    ELF_gen_NumberSections();
    ELF_gen_SetLinkSymbols();
    if (!ELG_OUTPUT_KIND)
        data_004a289c.entry = data_004a2a68->x10;
    ELF_PPC_EABI_PrepareWrite();
    for (i = 0; (section = data_004a2820[i]); i++) {
        ELF_PPC_EABI_SelectBuffers(section);
        if (section->o54)
            ((void (*)(ELOutput *))section->o54)(section);
#if LINKER >= 27
        SRecord_Flush();
#endif
        if (section->oa8 && section->kind == 1)
            section->o84 = section->address;
    }

    if (data_004a2e3a) {
        if (data_004a21d0.size)
            ELG_ASSERT(0x6d6);
        ELF_gen_WriteBuffer(&data_004a21d0, data_004a2e34, 1);
    }

    /* The loaded sections' contents. */
    for (i = 0; (section = data_004a2a88[i]); i++) {
        if (data_0049c120 == 2 && (section->o38 || section->o48))
            ELG_ASSERT(0x6dd);
        if (data_0049c120 == 2 || section->kind == 3 || section->kind == 2 || section->kind == 4)
            continue;
        if (section->o38 && !section->excluded) {
            ELGB_PadFile(section->align74);
            if (section->o88 != ELGB_GetPos())
                ELG_ASSERT(0x6ea);
            ELF_gen_WriteBuffer((GList *)&section->o34, data_004a2dcc, 1);
        } else if (section->size) {
            ELGB_PadFile(section->align74);
        }
        if (section->o48 && !section->excluded)
            ELF_gen_WriteBuffer((GList *)&section->o44, data_004a2e34, 1);
    }

    if (data_004a2e3a) {
        data_004a28d0 = &data_004a21f0;
        SRecord_Start(data_004a289c.entry, 7);
        SRecord_End();
        ELF_gen_WriteBuffer(&data_004a21f0, data_004a2e34, 1);
    }

    /* The other sections (kinds 0x13 to 0x1b: the debugging, symbol and string sections...). */
    for (i = 0; (section = data_004a2820[i]); i++) {
        if (section->oc9 < 0x13 || section->oc9 > 0x1b || !section->o38)
            continue;
        if (data_0049c120 == 2)
            ELG_ASSERT(0x70b);
        ELGB_PadFile(section->align98);
        section->o88 = ELGB_GetPos();
        ELF_gen_WriteBuffer((GList *)&section->o34, data_004a2dcc, 1);
    }

    /* The section headers. */
    ELGB_PadFile(0x10);
    data_004a289c.section_header_offset = ELGB_GetPos();
    ELGB_WriteSectionHeader(data_004a2de2);
    for (i = 0; (section = data_004a2a88[i]); i++) {
        if (section->oa8)
            ELGB_WriteSectionHeader(section);
    }

    /* The loaded sections against each other, the stack and the heap. */
    if (!ELG_OUTPUT_KIND) {
        stackAddr = ELF_PPC_EABI_LinkSymbolValue(LinkSym_Find("_stack_addr")->index);
        stackEnd = ELF_PPC_EABI_LinkSymbolValue(LinkSym_Find("_stack_end")->index);
        heapAddr = ELF_PPC_EABI_LinkSymbolValue(LinkSym_Find("_heap_addr")->index);
        heapEnd = ELF_PPC_EABI_LinkSymbolValue(LinkSym_Find("_heap_end")->index);
        for (i = 0; (section = data_004a2a88[i]); i++) {
            if (section->type && section->size)
                ELGB_CheckOverlaps(section, &rom, stackAddr, stackEnd, heapAddr, heapEnd);
        }
        if ((stackAddr <= heapEnd && stackAddr > heapAddr) || (stackEnd >= heapAddr && stackEnd < heapEnd)) {
            CompilerTools_ReportDiagnostic(0x2a, "stack", "heap");
        } else {
            if (stackAddr < stackEnd)
                CompilerTools_ReportDiagnostic(0x78, "_stack_addr", "_stack_end");
            if (heapAddr > heapEnd)
                CompilerTools_ReportDiagnostic(0x78, "heap_addr", "heap_end");
        }
    }

    /* The link map's memory map. */
    if (data_004a2e45) {
        LK_symbol_Print("\r\n\r\nMemory map:\r\n");
        LK_symbol_Print("                   Starting Size     File     ROM      RAM Buffer\r\n");
        LK_symbol_Print("                   address           Offset   Address  Address\r\n");
        for (i = 0; (section = data_004a2a88[i]); i++) {
            if (section->type)
                LK_symbol_Print("  %15s  %08x %08x %08x %08x %08x\r\n", section->name, section->address,
                                section->size, section->o5c, section->romAddress, section->oa0);
        }
    } else {
        LK_symbol_Print("\r\n\r\nMemory map:\r\n");
        LK_symbol_Print("                   Starting Size     File\r\n");
        LK_symbol_Print("                   address           Offset\r\n");
        for (i = 0; (section = data_004a2a88[i]); i++) {
            if (section->type)
                LK_symbol_Print("  %15s  %08x %08x %08x\r\n", section->name, section->address, section->size,
                                section->o5c);
        }
    }
#if LINKER >= 27
    /* (2.7: those with contents) */
    for (node = data_004a2de6; node; node = node->next) {
        if (node->output->o8c)
            LK_symbol_Print("  %15s           %08x %08x\r\n", node->output->name, node->output->o8c,
                            node->output->o88);
    }
    if (data_004a2dea->o8c)
        LK_symbol_Print("  %15s           %08x %08x\r\n", ".line", data_004a2dea->o8c, data_004a2dea->o88);
#else
    for (node = data_004a2de6; node; node = node->next)
        LK_symbol_Print("  %15s           %06x %08x\r\n", node->output->name, node->output->o8c, node->output->o88);
    LK_symbol_Print("  %15s           %06x %08x\r\n", ".line", data_004a2dea->o8c, data_004a2dea->o88);
#endif

    if (!ELG_OUTPUT_KIND) {
        LK_symbol_Print("\r\n\r\nLinker generated symbols:\r\n");
        for (symbol = data_004a2934; symbol; symbol = symbol->next)
            LK_symbol_Print("%25s %08x\r\n", symbol->name, ELF_PPC_EABI_LinkSymbolValue(symbol->index));
#if LINKER >= 27
        /* (2.7's table: its tables are the Linker Generated Symbol File's symbols; SIZEOF_HEADERS, the file
           offset after the headers, and _ABS_SDA_BASE_ and _ABS_SDA2_BASE_, the bases again) */
        for (i = 0; LinkSym_Table[i].name; i++) {
            if (LinkSym_Table[i].x8)
                continue;
            if (LinkSym_Table[i].index == -8)
                LK_symbol_Print("%25s %08x\r\n", "SIZEOF_HEADERS", ELF_gen_headersSize);
            LK_symbol_Print("%25s %08x\r\n", LinkSym_Table[i].name, ELF_PPC_EABI_LinkSymbolValue(LinkSym_Table[i].index));
        }
        LK_symbol_Print("%25s %08x\r\n", "_ABS_SDA_BASE_", ELF_PPC_EABI_LinkSymbolValue(-8));
        LK_symbol_Print("%25s %08x\r\n", "_ABS_SDA2_BASE_", ELF_PPC_EABI_LinkSymbolValue(-9));
#else
        for (i = 0; LinkSym_Table[i].name; i++)
            LK_symbol_Print("%25s %08x\r\n", LinkSym_Table[i].name, ELF_PPC_EABI_LinkSymbolValue(LinkSym_Table[i].index));
#endif
    }

    /* The header's final values, swapped to the output's byte order. */
    data_004a289c.program_header_count = 0;
    if (!data_004a2e49) {
        for (i = 0; (section = data_004a2a88[i]); i++) {
            if (section->oa8 && section->kind == 1) {
#if LINKER >= 27
                if (section->oa0 != section->address && !(data_004a2e45 && section->o7c == 8))
#else
                if (section->oa0 != section->address)
#endif
                    data_004a289c.program_header_count++;
                data_004a289c.program_header_count++;
            }
        }
    }
    if (headers < (long)data_004a289c.program_header_count)
        ELG_ASSERT(0x7b3);
    if (data_004a2e49) {
        data_004a289c.type = 1;
        data_004a289c.entry = 0;
        data_004a289c.program_header_offset = 0;
        data_004a289c.program_header_size = 0;
        data_004a289c.program_header_count = 0;
    }
    data_004a289c.type = CTool_EndianConvertWord16(data_004a289c.type);
    data_004a289c.machine = CTool_EndianConvertWord16(data_004a289c.machine);
    data_004a289c.version = CTool_EndianConvertWord32(data_004a289c.version);
    data_004a289c.entry = CTool_EndianConvertWord32(data_004a289c.entry);
    data_004a289c.program_header_offset = CTool_EndianConvertWord32(data_004a289c.program_header_offset);
    data_004a289c.section_header_offset = CTool_EndianConvertWord32(data_004a289c.section_header_offset);
    data_004a289c.flags = CTool_EndianConvertWord32(data_004a289c.flags);
    data_004a289c.header_size = CTool_EndianConvertWord16(data_004a289c.header_size);
    data_004a289c.program_header_size = CTool_EndianConvertWord16(data_004a289c.program_header_size);
    data_004a289c.program_header_count = CTool_EndianConvertWord16(data_004a289c.program_header_count);
    data_004a289c.section_header_size = CTool_EndianConvertWord16(data_004a289c.section_header_size);
    data_004a289c.section_header_count = CTool_EndianConvertWord16(data_004a289c.section_header_count);
    data_004a289c.section_name_index = CTool_EndianConvertWord16(data_004a289c.section_name_index);

    /* The header and the program headers again, at the start of the file. */
    end = ELGB_GetPos();
    ELGB_SetPos(0);
    ELGB_Write(&data_004a289c, 0x34);
    if (!data_004a2e49) {
        for (i = 0; (section = data_004a2a88[i]); i++) {
            if (!section->oa8 || section->kind != 1)
                continue;
            type = section->o58;
            if (section->excluded)
                type = 0;
            same = section->oa0 == section->address;
#if LINKER >= 27
            if (data_004a2e45 && section->o7c == 8)
                same = 1;
#endif
            if (!same) {
                ELF_gen_WriteProgramHeader(section, 1, 1, type, section->oa0);
#if LINKER >= 27
                /* (2.7: the RAM one has no file contents) */
                {
                    unsigned long size = section->o68;

                    section->o68 = 0;
                    ELF_gen_WriteProgramHeader(section, 0, 1, 0, section->address);
                    section->o68 = size;
                }
#else
                ELF_gen_WriteProgramHeader(section, 0, 1, 0, section->address);
#endif
            } else {
#if LINKER >= 27
                /* (2.7: an empty one's at its aligned address, ELF_PPC_EABI_AdvanceMemory's) */
                if (!section->size && !data_004a2e45) {
                    ELF_gen_WriteProgramHeader(section, 1, 1, type, section->romAddress);
                    continue;
                }
#endif
                ELF_gen_WriteProgramHeader(section, 1, 1, type, section->address);
            }
        }
    }
    ELGB_SetPos(end);
}

/* Marks a linker-defined symbol global (an object) and its section as having symbols. */
static void ELGB_MakeGlobal(ELSymbol *symbol) {
    symbol->info = 0x11;
    data_004a2dde->oa8 = 1;
}

/* Checks the kept linker-defined symbols have a sizeless symbol, and makes the command file's ones global. */
/* 0x43e2f0 */
void ELF_gen_CheckLinkSymbols(void) {
    ELLinkSymbol *link;
    ELSymbol *symbol;
    long i;

    if (data_004a2e49 && !data_004a2e4f)
        return;

    for (i = 0; LinkSym_Table[i].name; i++) {
        link = &LinkSym_Table[i];
        if (!link->made)
            continue;
        symbol = link->piece->symbol;
        if (!symbol || !(symbol->flags & 0x10) || symbol->size)
            ELG_ASSERT(0x7f3);
        if (data_004a2e4f) {
            if (link->index >= -12 && link->index <= -10)
                ELGB_MakeGlobal(symbol);
        } else if (link->x8) {
            ELGB_MakeGlobal(symbol);
        }
    }
    for (link = data_004a2934; link; link = link->next) {
        if (!link->made)
            continue;
        symbol = link->piece->symbol;
        if (!symbol || !(symbol->flags & 0x10) || symbol->size)
            ELG_ASSERT(0x80a);
        if (link->x8 && !data_004a2e4f)
            ELGB_MakeGlobal(symbol);
    }
}

/* Makes the symbol of a segment symbol (.startof., .phdr_): a new global of its section, with its value. */
static ELPiece *ELGB_MakeSegmentPiece(ELLinkSymbol *link, ELOutput *section) {
    ELPiece *piece;

    piece = LK_symbol_Add(link->name);
    link->piece = piece;
    piece->symbol = (ELSymbol *)galloc(sizeof(ELSymbol));
    memset(piece->symbol, 0, sizeof(ELSymbol));
    piece->file = link->index;
    piece->symbol->info = 5;
    piece->symbol->section = (unsigned short)section->o2c;
    link->piece->x10 = ELF_PPC_EABI_LinkSymbolValue(link->index);
    piece->symbol->value = link->piece->x10;
    return piece;
}

/* Gives the linker-defined symbols their values and sections, and makes the segment symbols. */
/* 0x43df30 */
void ELF_gen_SetLinkSymbols(void) {
    ELLinkSymbol *link;
    ELSymbol *symbol;
    ELOutput *section;
    ELPiece *prev;
    ELPiece *saved;
    ELPiece *start;
    ELPiece *first;
    ELPiece *second;
    long segment;
    long i;

    if (data_004a2e49 && !data_004a2e4f)
        return;

    for (i = 0; LinkSym_Table[i].name; i++) {
        link = &LinkSym_Table[i];
        if (!link->made)
            continue;
        symbol = link->piece->symbol;
        if (!link->x8) {
            if (!ELG_OUTPUT_KIND) {
                link->piece->x10 = ELF_PPC_EABI_LinkSymbolValue(link->index);
                symbol->value = link->piece->x10;
                symbol->section = 0xfff1;
            }
        } else {
            switch (link->index) {
            case -12:
                symbol->section = (unsigned short)data_004a2e16->o2c;
                break;
            case -11:
                symbol->size = data_004a2878 * 4 + 4;
                symbol->section = (unsigned short)data_004a2e1e->o2c;
                link->piece->x10 = data_004a2e2e;
                break;
            case -10:
                symbol->size = data_004a2a8c * 4 + 4;
                symbol->section = (unsigned short)data_004a2e1a->o2c;
                link->piece->x10 = data_004a2e2a;
                break;
            default:
                if (!data_004a2e49)
                    symbol->section = (unsigned short)data_004a2dde->o2c;
                break;
            }
        }
    }

    if (!ELG_OUTPUT_KIND) {
        for (link = data_004a2934; link; link = link->next) {
            if (!link->made)
                continue;
            symbol = link->piece->symbol;
            if (!link->x8) {
                link->piece->x10 = ELF_PPC_EABI_LinkSymbolValue(link->index);
                symbol->value = link->piece->x10;
                symbol->section = 0xfff1;
            } else {
                symbol->section = (unsigned short)data_004a2dde->o2c;
            }
        }
    }

    if (!data_004a2e49 && data_004a2e59) {
        prev = data_0049a7a8;
        saved = (ELPiece *)prev->x24;
        segment = 0;
        for (i = 0; (section = data_004a2a88[i]); i++) {
            if (!section->oa8 || section->kind != 1)
                continue;
            start = ELGB_MakeSegmentPiece(LinkSym_AddStartOf(section), section);
            prev->x24 = (long)start;
            if (section->oa0 != section->address) {
                first = ELGB_MakeSegmentPiece(LinkSym_AddSegment(section, segment), section);
                start->x24 = (long)first;
                second = ELGB_MakeSegmentPiece(LinkSym_AddSegment(section, segment + 1), section);
                segment += 2;
                first->x24 = (long)second;
                prev = second;
            } else {
                first = ELGB_MakeSegmentPiece(LinkSym_AddSegment(section, segment), section);
                segment++;
                start->x24 = (long)first;
                prev = first;
            }
        }
        prev->x24 = (long)saved;
    }
}

/* .strtab: the symbols' names (with data_004a2e53, a name over 1000 characters is reported). */
/* 0x43dba0 */
void ELF_gen_WriteStrings(void) {
    char buffer[0x100];
    char zero;
    ELPiece *piece;
    long length;
#if LINKER >= 27
    /* (2.7: said too) */
    CompilerTools_ReportMessage(0x23, data_0049bed6, data_004a2df2->name);
#endif

    if (data_0049c120 == 2)
        ELGA_AlignFile(data_004a2df2);
    zero = 0;
    if (data_0049c120 == 2) {
        short error = COS_FileWrite(data_004a2dcc, &zero, 1);
        if (error)
            ELGA_Fail(error);
    } else {
        AppendGListByte(data_004a2a70, 0);
    }
    data_004a2dda++;
    data_004a2df2->o8c++;

    for (piece = LK_symbol_First(); piece; piece = ELGA_NEXT_OUT(piece)) {
        if (!piece->symbol || (piece->symbol->info & 0xf) == 3)
            continue;
        piece->stName = data_004a2df2->o8c;
        length = strlen(piece->name) + 1;
        if (data_004a2e53 && length > 1000) {
            if (piece->symbol->section == 0) {
                ELGA_Write(data_004a2df2, piece->name, length);
                LK_symbol_Print(">>> NONMODULE SYMBOL NAME TOO LARGE: %s\r\n", piece->name);
            } else {
                length = sprintf(buffer, "***Symbol_name_too_large_at_offset_%lx", ELGA_OUT_VALUE(piece));
                ELGA_Write(data_004a2df2, buffer, length + 1);
                LK_symbol_Print(">>> MODULE SYMBOL NAME TOO LARGE: %s\r\n", piece->name);
            }
        } else {
            ELGA_Write(data_004a2df2, piece->name, length);
        }
    }
}

/* .shstrtab: the output sections' names (and each one's offset there). */
/* 0x43d960 */
void ELF_gen_WriteSectionNames(void) {
    char zero;
    ELOutput *out;
    long i;
#if LINKER >= 27
    /* (2.7: said too) */
    CompilerTools_ReportMessage(0x23, data_0049bed6, data_004a2df6->name);
#endif

    if (data_0049c120 == 2)
        ELGA_AlignFile(data_004a2df6);
    zero = 0;
    if (data_0049c120 == 2) {
        short error = COS_FileWrite(data_004a2dcc, &zero, 1);
        if (error)
            ELGA_Fail(error);
    } else {
        AppendGListByte(data_004a2a70, 0);
    }
    data_004a2dda++;
    data_004a2df6->o8c++;

    for (i = 0; (out = data_004a2a88[i]) != 0; i++) {
        if (out->oa8) {
            out->o78 = data_004a2df6->o8c;
            ELGA_Write(data_004a2df6, out->name, strlen(out->name) + 1);
        }
    }
}

/* .symtab's order (linked by +0x24): the kept locals, the section symbols, the globals. */
/* 0x43d730 */
#if LINKER >= 27
/* Whether a symbol is of a DWARF section's: 2.7's executables have none of them. (Not an original function.) */
static Boolean ELGA_InDwarf(ELPiece *piece) {
    ELObject *object;
    unsigned short shndx;

    /* (a partial link's are kept, with -g) */
    if ((data_004a2e49 && !data_004a28e4->includeDwarf && data_004a2e37) || !piece->symbol || piece->file < 0)
        return 0;
    object = &data_004a28dc[piece->file];
    shndx = piece->symbol->section;
    return object->image && shndx && shndx < object->image->section_header_count &&
           object->sections[shndx].output && object->sections[shndx].output->kind == 2;
}
#define ELGA_IN_DWARF(piece) ELGA_InDwarf(piece)
#else
#define ELGA_IN_DWARF(piece) 0
#endif

#if LINKER >= 27
extern Boolean ELF_PPC_EABI_TablesInInit(void);
extern ELOutput *data_004a2dde;
ELPiece *ELF_gen_tablesSymbol; /* (2.7: the .init piece's section symbol, its address set by the layout) */
#endif

void ELF_gen_OrderSymbols(void) {
    ELPiece *first;
    ELPiece *tail;
    ELPiece *piece;
    ELPiece *after;
    ELPiece *rest;
    ELSymbol *symbol;
    ELSection *section;
    ELObject *object;
    ELOutput *out;
    unsigned short shndx;
    long i;
#if LINKER >= 27
    ELPiece *tables = NULL;
#endif

    first = LK_symbol_First();
    tail = first;
#if LINKER >= 27
    /* (2.7: the section symbol of its Linker Generated Symbol File's .init piece, the tables: before the file's
       locals, else last of them) */
    ELF_gen_tablesSymbol = NULL;
    if (data_004a2e44 && ELF_PPC_EABI_TablesInInit()) {
        tables = (ELPiece *)galloc(sizeof(ELPiece));
        memset(tables, 0, sizeof(ELPiece));
        tables->file = -2;
        tables->symbol = (ELSymbol *)galloc(sizeof(ELSymbol));
        memset(tables->symbol, 0, sizeof(ELSymbol));
        tables->symbol->info = 3;
        tables->symbol->section = (unsigned short)data_004a2dde->o2c;
        ELF_gen_tablesSymbol = tables;
    }
#endif
    for (piece = first; piece; piece = piece->nextAdded) {
        symbol = piece->symbol;
        if (!symbol || piece->file < 0)
            continue;
        object = &data_004a28dc[piece->file];
#if LINKER >= 27
        if (ELGA_InDwarf(piece))
            continue;
#endif
        if ((symbol->info & 0xf) == 3) {
            section = &object->sections[symbol->section];
            if (!section->kept || !section->output)
                continue;
#if LINKER >= 27
            /* (2.7: not a loaded section's the layout does not place (ELF_gen_LayoutContents), in a program
               without -g) */
            if (!data_004a2e49 && !data_004a2e37 && piece->file != ELF_linker_generatedFile && piece->chunk &&
                section->header && (section->header->flags & 2) &&
                (!piece->chunk->kept ||
                 (!piece->chunk->whole && !ELGC_SectionSymbolUsed(piece->file, piece, piece->chunk))))
                continue;
#endif
            if (!data_004a2e44 && section->kind == 3)
                continue;
            if (!data_004a2e44 && section->output->kind == 2)
                continue;
            symbol->flags |= 0x10;
            if (!data_004a2e44)
                section->output->ocd = 1;
        }
        symbol = piece->symbol;
        if (!((symbol->flags & 0x10) && !(symbol->flags & 0x20))) {
#if LINKER >= 27
            /* (2.7: a partial link without -strip_partial has every object's file symbol, one with nothing kept
               too) */
            if ((symbol->info & 0xf) != 4 ||
                !(object->forced || (data_004a2e49 && !data_004a2e39 && piece->file != ELF_linker_generatedFile)))
                continue;
#else
            if ((symbol->info & 0xf) != 4 || !object->forced)
                continue;
#endif
        }
        shndx = symbol->section;
        if (shndx && shndx <= object->image->section_header_count)
            object->sections[shndx].output->oa8 = 1;
        if ((piece->symbol->info >> 4) == 0) {
#if LINKER >= 27
            /* (2.7: not an object's local past its symbol table's first global, as its header says) */
            if (object->symbols && piece->symbol >= (ELSymbol *)object->symbols->data &&
                piece->symbol < (ELSymbol *)object->symbols->data + (object->symbols->header->size >> 4) &&
                (unsigned long)(piece->symbol - (ELSymbol *)object->symbols->data) >= object->symbols->header->info)
                continue;
            if (tables && piece->file == ELF_linker_generatedFile) {
                ELGA_NEXT_OUT(tail) = tables;
                tail = tables;
                tables = NULL;
            }
#endif
            ELGA_NEXT_OUT(tail) = piece;
            tail = piece;
        }
    }
#if LINKER >= 27
    if (tables) {
        ELGA_NEXT_OUT(tail) = tables;
        tail = tables;
    }
#endif
    data_0049a7a8 = tail;

    for (piece = LK_symbol_First(); piece; piece = piece->nextAdded) {
        symbol = piece->symbol;
        if (ELGA_IsKept(symbol) && (symbol->info >> 4) > 0 && !ELGA_IN_DWARF(piece)) {
            ELGA_NEXT_OUT(tail) = piece;
            tail = piece;
        }
    }

    if (!data_004a2e44) {
        after = first;
        rest = ELGA_NEXT_OUT(first);
        for (i = 0; (out = data_004a2a88[i]) != 0; i++) {
            if (out->oa8 && (out->kind == 1 || out->oc9 == 0x13)) {
                piece = LK_symbol_Add(out->name);
                piece->symbol = galloc(sizeof(ELSymbol));
                memset(piece->symbol, 0, sizeof(ELSymbol));
                piece->file = -1;
                piece->symbol->info = 3;
                piece->symbol->section = (unsigned short)out->o2c;
                ELGA_NEXT_OUT(after) = piece;
                after = piece;
                out->ocd = 0;
            }
        }
        ELGA_NEXT_OUT(after) = rest;
    }
}

/* .symtab: its entries, in the order of ELF_gen_OrderSymbols (each symbol's index set), and its sh_info. */
/* 0x43d380 */
void ELF_gen_WriteSymbols(void) {
    struct ElfSymbolRecord sym;
    ELPiece *piece;
    ELOutput *out;
    ELSection *section;
    ELObject *object;
    unsigned short shndx;
    long index;
    long locals;
    Boolean written;
#if LINKER >= 27
    /* (2.7: said too) */
    CompilerTools_ReportMessage(0x23, data_0049bed6, data_004a2dee->name);
#endif

    if (data_0049c120 == 2)
        ELGA_AlignFile(data_004a2dee);
    locals = 0;
    memset(&sym, 0, sizeof(sym));
    ELGA_Write(data_004a2dee, &sym, sizeof(sym));
    data_004a2dee->o8c = sizeof(sym);

    index = 0;
    for (piece = LK_symbol_First(); piece; piece = ELGA_NEXT_OUT(piece)) {
        if (!piece->symbol)
            continue;
        sym = *(struct ElfSymbolRecord *)piece->symbol;
        sym.name = piece->stName;
        out = 0;
        shndx = sym.sectionIndex;
        if (shndx && piece->file >= 0 &&
            shndx <= (object = &data_004a28dc[piece->file])->image->section_header_count) {
            section = &object->sections[shndx];
            out = section->output;
            if (!out) {
                if (section->kind == 3)
                    continue;
                ELG_ASSERT(0x946);
            }
        } else if (piece->file < 0) {
            out = ELF_PPC_EABI_FindSectionByIndex((unsigned long)shndx);
        }

        if ((sym.info & 0xf) == 3) {
            if (!out)
                ELG_ASSERT(0x94d);
            if (piece->file == -1) {
                if (out->ocd)
                    continue;
                out->ocd = 1;
                sym.size = out->o8c;
                ELGA_OUT_VALUE(piece) = out->o84;
            } else {
                written = out->ocd;
                if (written && sym.value == 0)
                    continue;
                if (!data_004a2e44) {
                    if (written)
                        continue;
                    out->ocd = 1;
                }
                sym.size = 0;
            }
            sym.name = 0;
        }
        if ((sym.info >> 4) == 0)
            locals++;
        sym.other = 0;
        sym.sectionIndex = shndx;
        if (out) {
            sym.sectionIndex = (unsigned short)out->oa8;
            sym.value = ELGA_OUT_VALUE(piece);
        }
        ELF_Endian_ConvertSymbolRecord(&sym);
        ELGA_Write(data_004a2dee, &sym, sizeof(sym));
        ELGA_INDEX(piece) = ++index;
        ELGA_WRITTEN(piece) = 1;
    }
    data_004a2dee->o94 = locals + 1;
}

/* The section numbers (+0xa8) of the output sections written, and their sh_link and sh_info. */
/* 0x43d1d0 */
void ELF_gen_NumberSections(void) {
    ELOutput *out;
    ELOutput *rela;
    ELOutput *other;
    long index;
    long i;

    data_004a2dee->oa8 = 1;
    data_004a2df2->oa8 = 1;
    data_004a2df6->oa8 = 1;
    if (data_004a2e45)
        data_004a2dfa->oa8 = 1;

    index = 0;
    for (i = 0; (out = data_004a2a88[i]) != 0; i++) {
        if (out->oa8) {
            out->oa8 = ++index;
            if (out->oc9 == 0x18)
                data_004a28ce = (short)index;
        }
    }
    data_004a2dee->o90 = data_004a2df2->oa8;
    if (data_004a2e45)
        data_004a2dfa->o90 = data_004a2df6->oa8;

    if (data_004a2e49) {
        for (i = 0; (out = data_004a2a88[i]) != 0; i++) {
            rela = (ELOutput *)out->ob0;
            if (out->oa8 && rela && rela->oa8) {
                rela->o90 = data_004a2dee->oa8;
                ((ELOutput *)out->ob0)->o94 = out->oa8;
            }
        }
    }
    if (data_004a2e40) {
        for (i = 0; (out = data_004a2a88[i]) != 0; i++) {
            if (!out->oa8)
                continue;
            rela = (ELOutput *)out->ob0;
            if (rela && rela->oa8) {
                rela->o90 = data_004a2dee->oa8;
                ((ELOutput *)out->ob0)->o94 = out->oa8;
            }
            other = out->xb8;
            if (other && other->oa8)
                other->o90 = out->oa8;
        }
    }
}

/* .PPC.EMB.seginfo: an entry per output section (two when its load address differs from its address). */
/* 0x43cf20 */
void ELF_gen_WriteSegInfo(void) {
    ELGASegInfo entry;
    ELOutput *out;
    long count;
    long i;

    if (!data_004a2e45)
        return;
    if (data_0049c120 == 2)
        ELGA_AlignFile(data_004a2dfa);
    count = 0;
    for (i = 0; (out = data_004a2a88[i]) != 0; i++) {
        if (!out->oa8 || out->kind != 1)
            continue;
        if (out->oa0 != out->address) {
            entry.index = (unsigned short)count;
            entry.name = out->o78;
            entry.flag = 1;
            entry.next = count + 1;
            ELF_Endian_ConvertTwoHalvesTwoWords(&entry);
            ELGA_Write(data_004a2dfa, &entry, sizeof(entry));
            entry.index = (unsigned short)(count + 1);
            count += 2;
        } else {
            entry.index = (unsigned short)count;
            count++;
        }
        entry.name = out->o78;
        entry.flag = 0;
        entry.next = 0;
        ELF_Endian_ConvertTwoHalvesTwoWords(&entry);
        ELGA_Write(data_004a2dfa, &entry, sizeof(entry));
    }
}

#if LINKER >= 27
/* Whether object FILE's relocations go in an executable: it has a section of the procedure records' type (a
   .mwcats section, or another of that type, such as the .note.split of decomp-toolkit's objects); a partial
   link's have all. (Not an original function.) */
static Boolean ELGA_HasRecords(long file) {
    int i;

    if (data_004a2e49)
        return 1;

    for (i = 1; i < data_004a28dc[file].image->section_header_count; i++) {
        if (data_004a28dc[file].sections[i].header && data_004a28dc[file].sections[i].header->type == 0xca2a82c2)
            return 1;
    }
    return 0;
}
#endif

/* The size (+0x6c) of the relocations of output section OUT's pieces (and extabindex's). */
/* 0x43cc80 */
void ELF_gen_CountRelocations(ELOutput *out) {
    ELGATargetFunc find;
    ELCmdItem *item;
    ELSectionRef *ref;
    ELSection *section;
    ELChunk *chunk;
    ELPiece *piece;
    ELRelocNode *node;
    ELSymbol *target;
    ELSymbol *symbols;
    ELLinkSymbol *link;
    long file;
    long targetFile;
    Boolean kept;

    find = ELF_gen_ResolveSymbol;
    if (!data_004a2e49 && !data_004a2e40)
        ELG_ASSERT(0x9ec);
    for (item = out->items; item; item = item->next) {
        if (item->kind != 1 && item->kind != 2 && item->kind != 4)
            continue;
        if (item->kind == 2)
            find = ELF_gen_ResolveSymbolQuiet;
        for (ref = (ELSectionRef *)item->x4; ref; ref = ref->next) {
            section = ref->section;
            file = section->file;
            if (!data_004a28dc[file].forced || !section->kept)
                continue;
#if LINKER >= 27
            /* (2.7: only those of objects with procedure records) */
            if (!ELGA_HasRecords(file))
                continue;
#endif
            for (chunk = section->chunks; chunk; chunk = chunk->next) {
                if (!chunk->kept)
                    continue;
                for (piece = chunk->pieces; piece && piece->chunk == chunk; piece = piece->next) {
                    if (!ELGA_IsKept(piece->symbol))
                        continue;
                    for (node = piece->relocations; node; node = node->next) {
                        symbols = (ELSymbol *)data_004a28dc[file].symbols->data;
                        target = &symbols[node->rel->info >> 8];
                        find(&symbols[node->rel->info >> 8], file, &target, &targetFile);
                        if (targetFile < 0)
                            kept = LinkSym_FindIndex(targetFile)->made;
                        else
                            kept = ELGA_IsKept(target);
                        if (kept)
                            out->size += 0xc;
                    }
                }
            }
        }
        if (data_004a2e4f && item->section && strcmp(item->section, "extabindex") == 0) {
            link = LinkSym_FindIndex(-0xc);
            if (!link)
                ELG_ASSERT(0xa90);
            if (link->made && link->piece->symbol->size > 0x10)
                out->size += ((link->piece->symbol->size >> 4) - 1) * 0x24;
        }
    }
}

/* The relocation section of output section OUT: its size, and whether it is written (+0xa8). */
/* 0x43cc50 */
void ELF_gen_SizeRelocations(ELOutput *out) {
    out->size = 0;
    if (out->excluded)
        return;
    ELF_gen_CountRelocations(out);
    if (out->size)
        out->oa8 = 1;
}

#if LINKER >= 27
/* The relocation of LIST after PREV (0: the first) by offset, then by list order. (Not an original function.) */
static ELRelocNode *ELGA_NextByOffset(ELRelocNode *list, ELRelocNode *prev) {
    ELRelocNode *node;
    ELRelocNode *best = 0;
    unsigned long offset;
    unsigned long prevOffset = prev ? ((ELRelocation *)prev->rel)->offset : 0;
    long i;
    long prevIndex = -1;
    long bestIndex = 0;

    if (prev) {
        for (i = 0, node = list; node != prev; node = node->next)
            i++;
        prevIndex = i;
    }
    for (i = 0, node = list; node; node = node->next, i++) {
        offset = ((ELRelocation *)node->rel)->offset;
        if (prev && (offset < prevOffset || (offset == prevOffset && i <= prevIndex)))
            continue;
        if (!best || offset < ((ELRelocation *)best->rel)->offset) {
            best = node;
            bestIndex = i;
        }
    }
    (void)bestIndex;
    return best;
}
#endif

/* The relocations of output section OUT's pieces written (its .rela section), and extabindex's. */
/* 0x43c7c0 */
void ELF_gen_WriteRelocations(ELOutput *out) {
    ELGATargetFunc find;
    ELCmdItem *item;
    ELSectionRef *ref;
    ELSection *section;
    ELChunk *chunk;
    ELPiece *piece;
    ELPiece *targetPiece;
    ELRelocNode *node;
    ELSymbol *symbols;
    ELSymbol *target;
    ELLinkSymbol *link;
    ELRelocation rela;
    long file;
    long targetFile;
    Boolean kept;

    find = ELF_gen_ResolveSymbol;
    if (!data_004a2e49 && !data_004a2e40)
        ELG_ASSERT(0xa44);
    if (out->excluded || out->size == 0)
        return;
    CompilerTools_ReportMessage(0x23, data_0049bed6, out->name);
    if (data_0049c120 == 2)
        ELGA_AlignFile(out);
    targetPiece = 0;
    for (item = out->items; item; item = item->next) {
        if (item->kind != 1 && item->kind != 2 && item->kind != 4)
            continue;
        if (item->kind == 2)
            find = ELF_gen_ResolveSymbolQuiet;
        for (ref = (ELSectionRef *)item->x4; ref; ref = ref->next) {
            section = ref->section;
            file = section->file;
            if (!data_004a28dc[file].forced || !section->kept)
                continue;
#if LINKER >= 27
            /* (2.7: only those of objects with procedure records) */
            if (!ELGA_HasRecords(file))
                continue;
#endif
            for (chunk = section->chunks; chunk; chunk = chunk->next) {
                if (!chunk->kept)
                    continue;
                for (piece = chunk->pieces; piece && piece->chunk == chunk; piece = piece->next) {
                    if (!ELGA_IsKept(piece->symbol))
                        continue;
#if LINKER >= 27
                    /* (2.7: by offset) */
                    for (node = ELGA_NextByOffset(piece->relocations, 0); node;
                         node = ELGA_NextByOffset(piece->relocations, node)) {
#else
                    for (node = piece->relocations; node; node = node->next) {
#endif
                        rela = *(ELRelocation *)node->rel;
                        symbols = (ELSymbol *)data_004a28dc[file].symbols->data;
                        target = &symbols[rela.info >> 8];
                        find(&symbols[rela.info >> 8], file, &target, &targetFile);
                        if (targetFile < 0) {
                            link = LinkSym_FindIndex(targetFile);
                            kept = link->made;
                            targetPiece = link->piece;
                        } else {
                            kept = ELGA_IsKept(target);
                            if (kept)
                                targetPiece = target->piece;
                        }
                        if (!kept)
                            continue;
                        rela.info = (ELGA_INDEX(targetPiece) << 8) + (rela.info & 0xff);
                        rela.offset = rela.offset - piece->symbol->value + ELGA_OUT_VALUE(piece);
                        ELF_Endian_ConvertThreeWords((unsigned int *)&rela);
                        ELGA_Write(out, &rela, sizeof(rela));
                    }
                }
            }
        }
        if (data_004a2e4f && item->section && strcmp(item->section, "extabindex") == 0)
            ELF_gen_WriteExtabindexRelocations(out);
    }
}

/* extabindex's relocations, three per exception table group, into output section OUT's .rela section. */
/* 0x43c500 */
void ELF_gen_WriteExtabindexRelocations(ELOutput *out) {
    ELLinkSymbol *link;
    ELGroup *group;
    ELRelocation *rela;
    ELPiece *piece;
    unsigned long size;
    unsigned long offset;
    long n;

    link = LinkSym_FindIndex(-0xc);
    if (!link)
        ELG_ASSERT(0xaa3);
    if (!link->made || !data_004a28d4)
        return;
#if LINKER >= 27
    /* (2.7: after the inputs' relocations, already aligned) */
    if (data_0049c120 == 2 && out->o8c == 0)
#else
    if (data_0049c120 == 2)
#endif
        ELGA_AlignFile(out);
    size = ((link->piece->symbol->size >> 4) - 1) * 0x24;
    n = 0;
    offset = 0;
    for (group = data_004a2954; group; group = group->next) {
        if (!group->count)
            continue;
        rela = (ELRelocation *)(data_004a2e12 + offset);
        /* (the original adds the group's number, not its offset, to each relocation's offset) */
        piece = (*(ELRoot **)&group->x10[0])->symbol->piece;
        rela[0].offset = ELGA_OUT_VALUE(link->piece) + n;
        rela[0].info = (ELGA_INDEX(piece) << 8) + 1;
        rela[0].addend = 0;
        ELF_Endian_ConvertThreeWords((unsigned int *)&rela[0]);
        piece = (*(ELRoot **)&group->x10[4])->symbol->piece;
        rela[1].offset = ELGA_OUT_VALUE(link->piece) + n + 4;
        rela[1].info = (ELGA_INDEX(piece) << 8) + 1;
        rela[1].addend = piece->symbol->size;
        ELF_Endian_ConvertThreeWords((unsigned int *)&rela[1]);
        piece = (*(ELSymbol **)&group->x10[8])->piece;
        rela[2].offset = ELGA_OUT_VALUE(link->piece) + n + 8;
        rela[2].info = (ELGA_INDEX(piece) << 8) + 1;
        rela[2].addend = 0;
        ELF_Endian_ConvertThreeWords((unsigned int *)&rela[2]);
        offset += 0x24;
        n++;
    }
    ELGA_Write(out, data_004a2e12, size);
}

/* The layout of output section OUT's procedure records: chunk offsets, kept records marked and counted. */
/* 0x43c2b0 */
void ELF_gen_LayoutRecords(ELOutput *out) {
    ELCmdItem *item;
    ELSectionRef *ref;
    ELSection *section;
    ELSection *records;
    ELChunk *chunk;
    ELRecordNode *record;
    unsigned long align;
    unsigned long start;
    long size;
    long pad;

    start = 0;
    if (!data_004a2e40)
        return;
    CompilerTools_ReportMessage(0x29, data_0049bed6, out->name);
    EL_CHECK_BREAK();
    align = out->align98;
    if (align) {
        pad = ((data_004a2dda + align - 1) & ~(align - 1)) - data_004a2dda;
        ELGA_GrowPad(pad);
        data_004a2dda += pad;
    }
    out->o88 = data_004a2dda;
    for (item = out->items; item; item = item->next) {
        if (item->kind != 1)
            continue;
        for (ref = (ELSectionRef *)item->x4; ref; ref = ref->next) {
            section = ref->section;
            if (!data_004a28dc[section->file].forced || !section->kept)
                continue;
            records = section->related;
            if (!records)
                continue;
            if (records->header->type != 0xca2a82c2)
                ELG_ASSERT(0xaf0);
            for (chunk = records->chunks; chunk; chunk = chunk->next) {
                chunk->xc = start;
                if (data_004a2e49 && chunk->pieces)
                    chunk->pieces->x10 = chunk->xc;
                size = 0;
                for (record = chunk->records; record; record = record->next) {
                    if (!ELGA_IsKept(record->symbol))
                        continue;
                    size += record->size;
                    data_004a2dda += record->size;
                    pad = ((data_004a2dda + 3) & ~3) - data_004a2dda;
                    ELGA_GrowPad(pad);
                    data_004a2dda += pad;
                    size += pad;
                    record->x5 = 1;
                }
                chunk->x10 = chunk->xc + size;
                start = chunk->x10;
            }
            records->header->address = records->chunks->pieces->chunk->xc;
        }
    }
}

/* The size (+0x6c) of the relocations of output section OUT's procedure records. */
/* 0x43c110 */
void ELF_gen_CountRecordRelocations(ELOutput *out) {
    ELCmdItem *item;
    ELSectionRef *ref;
    ELSection *section;
    ELSection *records;
    ELChunk *chunk;
    ELPiece *piece;
    ELRelocNode *node;
    ELSymbol *symbols;
    long file;

    if (!data_004a2e49 || !data_004a2e40)
        ELG_ASSERT(0xb1e);
    for (item = out->items; item; item = item->next) {
        if (item->kind != 1)
            continue;
        for (ref = (ELSectionRef *)item->x4; ref; ref = ref->next) {
            section = ref->section;
            file = section->file;
            if (!data_004a28dc[file].forced || !section->kept)
                continue;
            records = section->related;
            if (!records || !records->kept)
                continue;
            for (chunk = records->chunks; chunk; chunk = chunk->next) {
                if (!chunk->kept)
                    continue;
                for (piece = chunk->pieces; piece && piece->chunk == chunk; piece = piece->next) {
                    if (!ELGA_IsKept(piece->symbol))
                        continue;
                    for (node = piece->relocations; node; node = node->next) {
                        symbols = (ELSymbol *)data_004a28dc[file].symbols->data;
                        if (ELGA_IsKept(&symbols[node->rel->info >> 8]))
                            out->size += 0xc;
                    }
                }
            }
        }
    }
}

/* The relocation section of output section OUT's procedure records: its size, and whether it is written. */
/* 0x43c0e0 */
void ELF_gen_SizeRecordRelocations(ELOutput *out) {
    out->size = 0;
    if (out->excluded)
        return;
    ELF_gen_CountRecordRelocations(out);
    if (out->size)
        out->oa8 = 1;
}

/* The relocations of output section OUT's procedure records written (a dropped record's relocations skip it). */
/* 0x43bcb0 */
void ELF_gen_WriteRecordRelocations(ELOutput *out) {
    ELCmdItem *item;
    ELSectionRef *ref;
    ELSection *section;
    ELSection *records;
    ELChunk *chunk;
    ELPiece *piece;
    ELRelocNode *node;
    ELSymbol *symbols;
    ELSymbol *target;
    ELRelocation rela;
    unsigned long skipped;
    long file;

    if (!data_004a2e49 || !data_004a2e40)
        ELG_ASSERT(0xb60);
    if (out->excluded || out->size == 0)
        return;
    CompilerTools_ReportMessage(0x23, data_0049bed6, out->name);
    if (data_0049c120 == 2)
        ELGA_AlignFile(out);
    for (item = out->items; item; item = item->next) {
        if (item->kind != 1 && item->kind != 2 && item->kind != 4)
            continue;
        for (ref = (ELSectionRef *)item->x4; ref; ref = ref->next) {
            section = ref->section;
            file = section->file;
            if (!data_004a28dc[file].forced || !section->kept)
                continue;
            records = section->related;
            if (!records || !records->kept)
                continue;
            for (chunk = records->chunks; chunk; chunk = chunk->next) {
                if (!chunk->kept)
                    continue;
                for (piece = chunk->pieces; piece && piece->chunk == chunk; piece = piece->next) {
                    skipped = 0;
                    if (!ELGA_IsKept(piece->symbol))
                        continue;
                    for (node = piece->relocations; node; node = node->next) {
                        rela = *(ELRelocation *)node->rel;
                        symbols = (ELSymbol *)data_004a28dc[file].symbols->data;
                        target = &symbols[rela.info >> 8];
                        if (ELGA_IsKept(target)) {
                            rela.info = (ELGA_INDEX(target->piece) << 8) + (rela.info & 0xff);
                            rela.offset = rela.offset - piece->symbol->value - skipped + ELGA_OUT_VALUE(piece);
                            ELF_Endian_ConvertThreeWords((unsigned int *)&rela);
                            ELGA_Write(out, &rela, sizeof(rela));
                        } else {
#if LINKER != 13
                            /* (1.3.2's offsets do not skip the dropped records) */
                            if (!node->record)
                                ELG_ASSERT(0xb8a);
                            skipped = (skipped + node->record->size + 3) & ~3;
#endif
                        }
                    }
                }
            }
        }
    }
}

/* The DWARF sections: output section OUT's from INCLUDEDWARF's first object, or its kept pieces' bytes. */
/* 0x43b920 */
void ELF_gen_WriteDebug(ELOutput *out) {
    ELFileName *name;
    ELObject *object;
    ELSection *section;
    ELCmdItem *item;
    ELSectionRef *ref;
    ELChunk *chunk;
    unsigned long offset;
    long i;

    if (!data_004a2e37)
        return;
    CompilerTools_ReportMessage(0x23, data_0049bed6, out->name);
    if (data_0049c120 == 2 && out->o88)
        ELGA_AlignFile(out);
    if (!data_004a2e49) {
        for (name = data_004a28e4->includeDwarf; name; name = name->next) {
            if (!name->index)
                continue;
            object = &data_004a28dc[name->index - 1];
            for (i = 1; i < object->image->section_header_count; i++) {
                section = &object->sections[i];
                if (section->name && out->name && strcmp(section->name, out->name) == 0) {
                    ELGA_Write(out, section->data, section->header->size);
                    break;
                }
            }
            break;
        }
    }
    for (item = out->items; item; item = item->next) {
        if (item->kind != 2)
            continue;
        for (ref = (ELSectionRef *)item->x4; ref; ref = ref->next) {
            section = ref->section;
            if (!data_004a28dc[section->file].forced || !section->kept)
                continue;
            ELF_PPC_EABI_RelocateSection(section);
            offset = 0;
            for (chunk = section->chunks; chunk; chunk = chunk->next) {
                if (chunk->kept)
                    ELGA_Write(out, section->data + offset, chunk->size);
                offset += chunk->size;
            }
        }
    }
}

/* The procedure records of output section OUT (their fields swapped in place), each padded to 4. */
#if LINKER >= 27
/* Whether output section OUT has procedure records to write. (Not an original function.) */
static Boolean ELGC_HasRecords(ELOutput *out) {
    ELCmdItem *item;
    ELSectionRef *ref;
    ELSection *records;
    ELChunk *chunk;
    ELRecordNode *record;

    if (!out->o88 || out->excluded)
        return 0;
    for (item = out->items; item; item = item->next) {
        if (item->kind != 1)
            continue;
        for (ref = (ELSectionRef *)item->x4; ref; ref = ref->next) {
            if (!data_004a28dc[ref->section->file].forced || !ref->section->kept || !(records = ref->section->related))
                continue;
            for (chunk = records->chunks; chunk; chunk = chunk->next) {
                if (!chunk->kept)
                    continue;
                for (record = chunk->records; record; record = record->next) {
                    if (record->x5)
                        return 1;
                }
            }
        }
    }
    return 0;
}
#endif

/* 0x43b530 */
void ELF_gen_WriteRecords(ELOutput *out) {
    ELCmdItem *item;
    ELSectionRef *ref;
    ELSection *section;
    ELSection *records;
    ELChunk *chunk;
    ELRecordNode *record;
    char *data;
    long pad;

    if (!data_004a2e40)
        return;
#if LINKER >= 27
    /* (2.7: records written) */
    if (ELGC_HasRecords(out))
#endif
    CompilerTools_ReportMessage(0x23, data_0049bed6, out->name);
    if (!out->o88 || out->excluded)
        return;
    data_004a2dda = out->o88;
    if (data_0049c120 == 2)
        ELGA_AlignFile(out);
    for (item = out->items; item; item = item->next) {
        if (item->kind != 1)
            continue;
        for (ref = (ELSectionRef *)item->x4; ref; ref = ref->next) {
            section = ref->section;
            if (!data_004a28dc[section->file].forced || !section->kept)
                continue;
            records = section->related;
            if (!records)
                continue;
            ELF_PPC_EABI_RelocateSection(records);
            for (chunk = records->chunks; chunk; chunk = chunk->next) {
                if (!chunk->kept)
                    continue;
                for (record = chunk->records; record; record = record->next) {
                    if (!record->x5)
                        continue;
                    data = records->data + record->offset;
                    switch (record->kind) {
                    case 1:
                        *(unsigned long *)(data + 4) = CTool_EndianConvertWord32(*(unsigned long *)(data + 4));
                        *(unsigned long *)(data + 0xc) = CTool_EndianConvertWord32(*(unsigned long *)(data + 0xc));
                        *(unsigned long *)(data + 0x10) = CTool_EndianConvertWord32(*(unsigned long *)(data + 0x10));
                        break;
                    case 2:
                        *(unsigned short *)(data + 2) = CTool_EndianConvertWord16(*(unsigned short *)(data + 2));
                        break;
                    case 3:
                        *(unsigned long *)(data + 4) = CTool_EndianConvertWord32(*(unsigned long *)(data + 4));
                        *(unsigned short *)(data + 2) = CTool_EndianConvertWord16(*(unsigned short *)(data + 2));
                        break;
                    }
                    ELGA_Write(out, records->data + record->offset, record->size);
                    pad = ((data_004a2dda + 3) & ~3) - data_004a2dda;
                    ELGA_GrowPad(pad);
                    ELGA_Write(out, ELF_gen_PadBytes, pad);
                }
            }
        }
    }
}

/* .comment: its header, an empty entry, and the alignment and flags of each symbol written to .symtab. */
/* 0x43b2b0 */
void ELF_gen_WriteComment(ELOutput *out) {
    unsigned long zeros[2];
    ELPiece *piece;

    if (data_0049c120 == 2)
        ELGA_AlignFile(out);
    ELGA_Write(out, data_004a2e0a, 0x2c);
    zeros[0] = 0;
    zeros[1] = 0;
    ELGA_Write(out, zeros, 8);
    for (piece = LK_symbol_First(); piece; piece = ELGA_NEXT_OUT(piece)) {
        if (ELGA_WRITTEN(piece)) {
#if LINKER >= 27
            /* (2.7: the Linker Generated Symbol File's symbols, its _eti_init_info among them, are word aligned) */
            if (piece->file < 0 || piece->file == ELF_linker_generatedFile) {
                zeros[0] = CTool_EndianConvertWord32(4);
                zeros[1] = 0;
                ELGA_Write(out, zeros, 8);
                continue;
            }
            /* (2.7: an object without its comment, one FORCEFILES names, has no alignment, flags 0xff) */
            if (piece->file >= 0 && !data_004a28dc[piece->file].hasComment) {
                static unsigned char none[8] = {0xff, 0xff, 0xff, 0xff, 0, 0xff, 0, 0};

                ELGA_Write(out, none, 8);
                continue;
            }
            /* (2.7: a weak symbol's .comment flags cleared, its force active kept) */
            if (piece->symbol && piece->symbol->section != 0 && (piece->symbol->info >> 4) == 2) {
                piece->commentFlags = 0;
                piece->align = CTool_EndianConvertWord32(piece->align);
                ELGA_Write(out, &piece->align, 8);
                continue;
            }
            /* (2.7: an undefined symbol byte aligned, its flags cleared) */
            if (piece->symbol && piece->symbol->section == 0) {
                zeros[0] = CTool_EndianConvertWord32(piece->symbol->section == 0 && !piece->align ? 1 : piece->align);
                zeros[1] = 0;
                ELGA_Write(out, zeros, 8);
                continue;
            }
#endif
            piece->align = CTool_EndianConvertWord32(piece->align);
            ELGA_Write(out, &piece->align, 8);
        }
    }
}
