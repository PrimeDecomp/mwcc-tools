/* The PowerPC EABI side of the link: the output sections (the default SECTIONS, those the command file names and
   those the objects' sections need, with their .rela and .mwcats sections), each input section given its output
   section, the small data sections and their bases, the memory areas and the sections' addresses, the ROM copy
   and BSS init tables, the linker-defined symbols' values, the callbacks ELF_gen.c lays out and writes the
   sections with, the objects' EABI compliance, and the relocations applied to the code and data.

   Built with -inline deferred, the compiler emits the functions in the reverse of their order here. The assertions
   name this file "ELF_PPC_EABI.c". */
#include "linker/ELF_linker.h"
#include "compiler/CompilerTools.h"

/* Part A: 0x44a210 to 0x44c440. */

#define EPEA_ASSERT(line) CompilerTools_FormatMessageAndLongjmp(0x33, 2, "ELF_PPC_EABI.c", line)
#define EPEA_CHECK(line) CompilerTools_ReportLimitedDiagnostic(0x33, "ELF_PPC_EABI.c", line)

/* LinkSym.c's, LinkerCmdFile.c's, SRecord.c's */

/* ELF_gen.c's */

/* the output sections' callbacks (o54) */

/* The other parts' functions */

/* The globals */

void ELF_PPC_EABI_FixCtorRelocations(ELOutput *out);
long ELF_PPC_EABI_LayoutLinkerSymbols(ELOutput *section);

#pragma options align = mac68k

/* A term of a linker expression (LinkSym.c's LinkExpr, 10 bytes; the header's ELExpr types its +4 word as a name). */
typedef struct EPEBExpr {
    struct EPEBExpr *next;
    void *value;                    /* kind 1: its EPEBOperator; 3: a number; 6: a section (ADDR) */
    unsigned char kind;
    unsigned char x9;
} EPEBExpr;

/* An operator of an expression (LinkSym.c's LinkOperator, 10 bytes). */
typedef struct EPEBOperator {
    int precedence;
    int depth;
    unsigned char type;             /* 0xf: + */
    unsigned char unary;
} EPEBOperator;

#pragma options align = reset

typedef char EPEBExprSizeCheck[sizeof(EPEBExpr) == 10 ? 1 : -1];
typedef char EPEBOperatorSizeCheck[sizeof(EPEBOperator) == 10 ? 1 : -1];

/* The other parts' functions. */

#define EPEB_ASSERT(line) CompilerTools_FormatMessageAndLongjmp(0x33, 2, "ELF_PPC_EABI.c", line)



/* Part D: 0x44f9a0 to 0x451570: the relocations applied to a section, the check of a small data reference and
   the linker-defined symbols' values. */

unsigned long ELF_PPC_EABI_LinkSymbolValue(long index);

extern void CompilerTools_FormatMessageAndLongjmp(int message, int status, ...);
extern unsigned char CompilerTools_ReportDiagnostic(long diagnosticCode, ...);
extern void CompilerTools_ReportLimitedDiagnostic(int message, ...);
extern int sprintf(char *s, const char *format, ...);
extern int sscanf(const char *s, const char *format, ...);
extern int strcmp(const char *a, const char *b);
extern int strncmp(const char *a, const char *b, unsigned long n);
extern void *memset(void *dst, int c, unsigned long n);
extern ELLinkSymbol *LinkSym_FindIndex(long index);
extern ELTerm *EvalExpr(ELTerm *list, ELOutput *section);
#if LINKER >= 27
extern void *LCF_CopyExpr(void *list);
#endif
extern short SRecord_InitBuffer(GList *buffer, long size);
extern void SRecord_Start(unsigned long at, unsigned char kind);
extern void SRecord_AddBytes(unsigned char *data, long size);
extern void SRecord_End(void);
extern unsigned long ELF_gen_Write(const void *data, unsigned long size);
extern long ELF_gen_WritePadding(unsigned long align, Boolean srecord);
extern long ELF_gen_SkipPadding(unsigned long align);
extern void ELF_gen_MapExtabIndex(ELOutput *section);
extern void ELF_gen_LayoutSection(ELOutput *section);          /* 0x441560 */
#if LINKER >= 27
extern void ELF_gen_LayoutNoInputs(ELOutput *section);
extern void ELF_gen_PadEmptySection(ELOutput *section);
extern unsigned long data_004a2dda; /* the output file's size so far */
#endif
extern void ELF_gen_LayoutBssSection(ELOutput *section);       /* 0x4416b0 */
extern void ELF_gen_LayoutAssignment(ELOutput *section);       /* 0x4414b0 */
extern void ELF_gen_LayoutDwarf(ELOutput *section);            /* 0x440c70 */
extern void ELF_gen_SizeRelocations(ELOutput *out);            /* 0x43cc50 */
extern void ELF_gen_LayoutRecords(ELOutput *out);              /* 0x43c2b0 */
extern void ELF_gen_SizeRecordRelocations(ELOutput *out);      /* 0x43c0e0 */
extern void ELF_gen_WriteSection(ELOutput *section);           /* 0x43f580 */
extern void ELF_gen_AlignSectionInFile(ELOutput *section);     /* 0x4410b0 */
extern void ELF_gen_WriteAssignment(ELOutput *section);        /* 0x441190 */
extern void ELF_gen_WriteDebug(ELOutput *out);                 /* 0x43b920 */
extern void ELF_gen_WriteSectionNames(void);                   /* 0x43d960 */
extern void ELF_gen_WriteStrings(void);                        /* 0x43dba0 */
extern void ELF_gen_WriteSymbols(void);                        /* 0x43d380 */
extern void ELF_gen_WriteComment(ELOutput *out);               /* 0x43b2b0 */
extern void ELF_gen_WriteRecords(ELOutput *out);               /* 0x43b530 */
extern void ELF_gen_WriteSegInfo(void);                        /* 0x43cf20 */
/* The write callbacks of .shstrtab, .strtab, .symtab and the segment information take no argument, and are called
 * with the section (the o54 callbacks' argument). WebAssembly traps on that call: there each one is called through
 * a function that takes the section. */
#ifdef NONMATCHING
static void write_section_names(ELOutput *unused) { ELF_gen_WriteSectionNames(); }
static void write_strings(ELOutput *unused) { ELF_gen_WriteStrings(); }
static void write_symbols(ELOutput *unused) { ELF_gen_WriteSymbols(); }
static void write_seg_info(ELOutput *unused) { ELF_gen_WriteSegInfo(); }
#define WRITE_CALLBACK(function, thunk) (unsigned long)thunk
#else
#define WRITE_CALLBACK(function, thunk) (unsigned long)function
#endif
extern void ELF_gen_WriteRelocations(ELOutput *out);           /* 0x43c7c0 */
extern void ELF_gen_WriteRecordRelocations(ELOutput *out);     /* 0x43bcb0 */
extern ELOutput **data_004a2a88;        /* the output sections, null-terminated (in layout order) */
extern ELOutput **data_004a2820;        /* the output sections, by file order (null-terminated) */
extern long data_004a294c;              /* the number of output sections (the arrays' size - 1) */
extern unsigned long *data_004a2dfe;    /* _rom_copy_info's table (12 bytes per section) */
extern unsigned long *data_004a2e02;    /* _bss_init_info's table (8 bytes per section) */
extern GList *data_004a2a70;            /* the output's buffer */
extern GList *data_004a28d0;            /* the S-record buffer in use */
extern unsigned long data_004a2dd2;     /* the next ROM address */
extern unsigned long data_004a2dd6;     /* the next load address */
extern char *data_004a2e0a;             /* the .comment section's header (0x2c bytes) */
extern ELOutput *data_004a2e0e;         /* .comment's output section */
extern ELOutput *data_004a2dde;         /* the section that holds the linker-defined symbols */
extern ELOutputNode *data_004a2de6;   /* the debug sections (not loaded) */
extern ELOutput *data_004a2dea;         /* the .line section */
extern ELOutput *data_004a2dee;         /* .symtab */
extern ELOutput *data_004a2df2;         /* .strtab */
extern ELOutput *data_004a2df6;         /* .shstrtab */
extern ELOutput *data_004a2dfa;         /* .PPC.EMB.seginfo */
extern ELOutput *data_004a2e16;         /* extabindex's output section */
extern ELOutput *data_004a2e1a;         /* .ctors' */
extern ELOutput *data_004a2e1e;         /* .dtors' */
extern char data_004a2e37;              /* DWARF is written */
extern Boolean data_004a2e3f;           /* a small data area (sdata) */
extern Boolean data_004a2e42;           /* a second small data area (sdata2) */
extern Boolean data_004a2e44;
extern Boolean data_004a2e45;           /* ROM images: .PPC.EMB.seginfo is written */
extern Boolean data_004a2e52;           /* a data area */
extern Boolean data_004a2e59;           /* make the segment symbols */
extern unsigned long data_004a28f8;     /* the init memory area's origin */
extern unsigned long data_004a2828;     /* the code address */
extern unsigned long data_004a2214;     /* the data address */
extern unsigned long data_004a276c;     /* the small data address */
extern unsigned long data_004a2824;     /* the second small data address */
extern short data_0049c418;
extern Boolean data_0049c41a;
extern Boolean data_0049c41b;
extern unsigned long strlen(const char *s);
extern void *galloc(long size);
extern Boolean ReadLinkerCommandFile(void);
extern void LinkSym_AddSectionSymbols(ELOutput *section);
extern Boolean data_004a2e36;           /* a linker command file is read */
extern long data_004a2a80;
extern ELOutput *data_004a2de2;         /* the section header written first */
extern char *strcpy(char *dst, const char *src);
extern char *strcat(char *dst, const char *src);
extern char *strrchr(const char *s, int c);
extern void *memcpy(void *dst, const void *src, unsigned long n);
extern void ELF_gen_ResolveSymbol(ELSymbol *symbol, long file, ELSymbol **result, long *resultFile);
extern void CompilerTools_BufferDiagnostic(int message, ...);
extern void CompilerTools_ReportBufferedDiagnostic(int message, ...);
extern void CompilerTools_Abort(void);
extern ELLinkSymbol *LinkSym_Find(char *name);
extern unsigned long ELF_gen_SymbolAddress(ELSymbol *symbol, long file);
extern unsigned long data_004a28f0;     /* (-1's value when it has no expression) */
extern unsigned long data_004a28d8;     /* (-2's: subtracted from _stack_addr's value) */
extern unsigned long data_004a284c;     /* (-3's) */
extern unsigned long data_004a2870;     /* (-4's: added to _heap_addr's value) */
extern long data_004a2218;              /* (-5's) */
extern unsigned long data_004a2e2a;     /* .ctors' terminator's address (-10's) */
extern unsigned long data_004a2e2e;     /* .dtors' terminator's address (-11's) */

/* The size of the linker's own data (the ROM copy and BSS init tables and the linker-defined data symbols), laid
   out at the end of the section that holds them. */
long data_0049aaf0;

/* The functions of this file */
unsigned long ELF_PPC_EABI_LinkSymbolValue(long index);
Boolean ELF_PPC_EABI_CheckSdaReference(ELRelocation *rel, ELPiece *global, char *name, long file);
void ELF_PPC_EABI_ApplyRelocations(long file, ELSection *section);
void ELF_PPC_EABI_RelocateSection(ELSection *section);
ELOutput *ELF_PPC_EABI_NewOutput(char *name, unsigned char type, unsigned char kind);
void ELF_PPC_EABI_AddCtorItems(void);
void ELF_PPC_EABI_AddDtorItems(void);
ELOutput *ELF_PPC_EABI_AddSmallData2And0(ELOutput *prev);
ELOutput *ELF_PPC_EABI_FindNamedOutput(char *name);
ELOutput *ELF_PPC_EABI_EntryOutput(void);
void ELF_PPC_EABI_CheckFollows(char *first, char *second);
void ELF_PPC_EABI_MakeOutputSections(void);
void ELF_PPC_EABI_DefineSmallDataBases(void);
void ELF_PPC_EABI_AddSectionSymbols(void);
unsigned char ELF_PPC_EABI_LoadedSectionKind(char *name);
unsigned char ELF_PPC_EABI_SectionKind(char *name);
void ELF_PPC_EABI_SetSectionType(ELOutput *out);
void ELF_PPC_EABI_PrepareOutputs(void);
void ELF_PPC_EABI_ReadCommandFile(void);
int ELF_PPC_EABI_MatchFileName(char *pattern, char *name);
#if LINKER >= 27
/* (2.7: BSS may go in a section of data, not data in a BSS one) */
#define EPEC_BSS_MISMATCH(bss, nobits) ((bss) != (nobits))
#else
#define EPEC_BSS_MISMATCH(bss, nobits) ((bss) != (nobits))
#endif

Boolean ELF_PPC_EABI_IsBetterFlags(unsigned long flags, ELOutput *best, unsigned long sectionFlags);
void ELF_PPC_EABI_ScoreOutput(char *sectionName, char *fileName, char *memberName, ELOutput *out, ELOutput **best, long *score, unsigned long flags, ELCmdItem **itemOut, unsigned char nobits);
ELOutput *ELF_PPC_EABI_ChooseOutput(ELObject *object, ELSection *section);
void ELF_PPC_EABI_CheckComment(ELSection *section);
void ELF_PPC_EABI_AddInput(ELOutput *out, ELSection *section);
void ELF_PPC_EABI_AssignSection(ELObject *object, ELSection *section);
void ELF_PPC_EABI_MakeMemoryAreas(void);
void ELF_PPC_EABI_CheckCompliance(void);
void ELF_PPC_EABI_PrepareLayout(void);
ELOutput *ELF_PPC_EABI_FindSectionByIndex(unsigned long index);
void ELF_PPC_EABI_AdvanceMemory(ELOutput *section);
void ELF_PPC_EABI_PlaceSection(ELOutput *section);
void ELF_PPC_EABI_SelectBuffers(ELOutput *section);
void ELF_PPC_EABI_PrepareWrite(void);
long ELF_PPC_EABI_LayoutLinkerSymbols(ELOutput *section);
void ELF_PPC_EABI_LayoutLinkerData(ELOutput *section);
void ELF_PPC_EABI_WriteLinkerData(ELOutput *section);
void ELF_PPC_EABI_MakeCommentHeader(char *header);
void ELF_PPC_EABI_FixCtorRelocations(ELOutput *out);

/* The DWARF sections' relocations are written (2.7: in a partial link only). */
#if LINKER >= 27
/* (and not with INCLUDEDWARF, whose sections are copied whole) */
#define EPEC_DEBUG_RELOCATIONS (data_004a2e49 && !data_004a28e4->includeDwarf)
#else
#define EPEC_DEBUG_RELOCATIONS 1
#endif

/* The index of VALUE's one set bit: -1 for 0, -2 for a value of more than one bit (CompilerTools.c's getbit,
   which the original has among this unit's functions). */
static short getbit(unsigned long value) {
    short bit;

    if (value == 0)
        return -1;
    for (bit = 0; bit < 32; bit++) {
        if (value == (1UL << bit))
            return bit;
    }
    return -2;
}

/* A command file section's MEMORY area: its own, or its GROUP's. */
static ELMemory *EPEA_Memory(ELOutput *section) {
    if (section->memory)
        return (ELMemory *)section->memory;
    if (section->parent)
        return (ELMemory *)section->parent->memory;
    return NULL;
}

/* A new item of an output section's contents, that names the section itself. */
static void EPEA_AddSelfItem(ELOutput *out) {
    unsigned char kind = out->kind;
    char *name = out->name;
    ELCmdItem *item = (ELCmdItem *)galloc(0x20);
    memset(item, 0, 0x20);
    item->kind = kind;
    item->section = name;
    item->file = NULL;
    out->items = item;
}

/* A section appended to the command file's sections. */
static void EPEA_AppendSection(ELOutput *out) {
    ELOutput *last = data_004a28e4->sections;
    while (last->next)
        last = last->next;
    last->next = out;
}

/* A memory area appended to the command file's MEMORY. */
static void EPEA_AppendMemory(ELMemory *memory) {
    ELMemory *last = (ELMemory *)data_004a28e4->memory;
    if (!last) {
        data_004a28e4->memory = memory;
        return;
    }
    while (last->next)
        last = last->next;
    last->next = memory;
}

/* A new memory area. */
static ELMemory *EPEA_NewMemory(char *name, unsigned long origin, long length) {
    ELMemory *memory = (ELMemory *)galloc(sizeof(ELMemory));
    memset(memory, 0, sizeof(ELMemory));
    memory->name = name;
    memory->origin = origin;
    memory->length = length;
    return memory;
}

/* Gives an input section an output section from the command file or a new one (with its item), by its kind
   (relocations, procedure records, DWARF, .line, the string and symbol tables, .comment...), and adds it to the
   item's sections. */

/* An object's archive member name for the messages ("" for none). */
static char *EPEB_MemberName(long file) {
    if (file > -1 && data_004a28dc[file].memberName)
        return data_004a28dc[file].fileSymbol->name;
    return "";
}

/* An object's file name for the messages. */
static char *EPEB_FileName(long file) {
    if (file > -1)
        return data_004a28dc[file].file->name;
    return "linker generated symbol";
}

/* The first output section (not 5 or 6) of a name. */
static ELOutput *EPEB_FindOutput(char *name) {
    ELOutput *out;

    for (out = data_004a28e4->sections; out; out = out->next) {
        if (out->kind != 5 && out->kind != 6 && out->name && !strcmp(out->name, name))
            break;
    }
    return out;
}

/* A new item of a section of a kind, for an output section's contents. */
static ELCmdItem *EPEB_NewItem(unsigned char kind, char *section) {
    ELCmdItem *item;

    item = (ELCmdItem *)galloc(0x20);
    memset(item, 0, 0x20);
    item->kind = kind;
    item->section = section;
    item->file = NULL;
    return item;
}

/* A new term of an expression. */
static EPEBExpr *EPEB_NewTerm(unsigned char kind) {
    EPEBExpr *term;

    term = (EPEBExpr *)galloc(10);
    memset(term, 0, 10);
    term->kind = kind;
    return term;
}

/* A small data base symbol's value, when nothing defines it: the section's address + 0x8000. */
static void EPEB_DefineBase(long index, char *sectionName, int line) {
    ELLinkSymbol *symbol;
    ELOutput *out;
    EPEBExpr *expr;
    EPEBExpr *plus;
    EPEBOperator *op;

    symbol = LinkSym_FindIndex(index);
    if (!symbol)
        EPEB_ASSERT(line);
    if (symbol->expr) {
        CompilerTools_ReportDiagnostic(0x6c, symbol->name);
        return;
    }
    out = EPEB_FindOutput(sectionName);
    if (!out)
        return;
    expr = EPEB_NewTerm(6);
    expr->value = out;
    symbol->expr = (ELExpr *)expr;
    plus = EPEB_NewTerm(1);
    expr->next = plus;
    op = (EPEBOperator *)galloc(10);
    memset(op, 0, 10);
    op->type = 0xf;
    op->precedence = 3;
    plus->value = op;
    plus->next = EPEB_NewTerm(3);
    plus->next->value = (void *)0x8000;
}

/* An item of an output section's contents naming an input section (of the given file, or of all files). */
static ELCmdItem *EPEC_NewItem(char *section, char *file) {
    ELCmdItem *item = galloc(sizeof(ELCmdItem));
    memset(item, 0, sizeof(ELCmdItem));
    item->kind = 1;
    item->section = section;
    item->file = file;
    return item;
}

/* A new output section of SECTIONS, numbered (o2c), with nothing else set. */
static ELOutput *EPEC_AllocOutput(void) {
    ELOutput *output = galloc(sizeof(ELOutput));
    memset(output, 0, sizeof(ELOutput));
    output->o2c = data_004a294c++;
    return output;
}

/* An output section of SECTIONS by name, not a compressed one (oc9 9), or NULL. */
static ELOutput *EPEC_FindUncompressed(char *name) {
    ELOutput *output;

    for (output = data_004a28e4->sections; output; output = output->next) {
        if (output->kind != 5 && output->kind != 6 && output->name && name && strcmp(output->name, name) == 0
            && output->oc9 != 9)
            return output;
    }
    return NULL;
}

/* Applies the relocations of a (debug) section's kept pieces to its data: R_PPC_ADDR32 (1) and type 0x18
   take the target's address; any other type is an error (0x3c). */

/* Where object FILE comes from, for a message: an archive member is "MEMBER of archive 'ARCHIVE'", any other
   object its file's name (a linker-defined symbol's: "linker generated symbol"). (Not an original function: the
   original repeats it inline.) */
static void EPED_Where(long file, char **name, char **of, char **archive, char **quote) {
    char *member;
    char *path;

    member = (file > -1 && data_004a28dc[file].memberName) ? data_004a28dc[file].fileSymbol->name : "";
    path = file > -1 ? data_004a28dc[file].file->name : "linker generated symbol";
    if (*member) {
        *name = member;
        *of = " of archive '";
        *archive = path;
        *quote = "'";
    } else {
        *name = path;
        *of = "";
        *archive = "";
        *quote = "";
    }
}

/* A relocation that cannot be applied (0x43: out of range, 0x3c: an unknown type), naming its type, its symbol
   and the symbol's object. (Not an original function.) */
static void EPED_RelocError(int message, ELRelocation *rel, long file, ELSymbol *symbol) {
    char *name;
    char *of;
    char *archive;
    char *quote;

    EPED_Where(file, &name, &of, &archive, &quote);
    CompilerTools_ReportLimitedDiagnostic(message, rel->info & 0xff, LK_symbol_Name(file, symbol), name, of, archive, quote);
}

/* A small data relocation (R_PPC_EMB_SDA21) that cannot be applied (0x42), with its reason. (Not an original
   function.) */
static void EPED_SdaError(char *reason, ELRelocation *rel, long file, ELSymbol *symbol) {
    char *name;
    char *of;
    char *archive;
    char *quote;

    EPED_Where(file, &name, &of, &archive, &quote);
    CompilerTools_ReportLimitedDiagnostic(0x42, reason, rel->info & 0xff, LK_symbol_Name(file, symbol), name, of, archive, quote);
}

/* The value of the linker-defined symbol INDEX (its expression evaluated; the predefined ones, from -1 to -12,
   have their own values when they have none). */

/* 0x451410 */
unsigned long ELF_PPC_EABI_LinkSymbolValue(long index) {
    ELLinkSymbol *link;
    ELTerm *expr;

    link = LinkSym_FindIndex(index);
    if (!link) {
        CompilerTools_ReportLimitedDiagnostic(0xf, data_0049c161);
        CompilerTools_Abort();
    }
#if LINKER >= 27
    /* (2.7: evaluated each time) */
    expr = EvalExpr((ELTerm *)LCF_CopyExpr(link->expr), (ELOutput *)link->section);
#else
    link->expr = (ELExpr *)EvalExpr((ELTerm *)link->expr, (ELOutput *)link->section);
    expr = (ELTerm *)link->expr;
#endif

    if (index < 0 && index > -13) {
        switch (index) {
        case -1:
            if (expr)
                return expr->value;
            return data_004a28f0;
        case -2:
            if (expr)
                return expr->value;
            return ELF_PPC_EABI_LinkSymbolValue(LinkSym_Find("_stack_addr")->index) - data_004a28d8;
        case -3:
            if (expr)
                return expr->value;
            return data_004a284c;
        case -4:
            if (expr)
                return expr->value;
            return ELF_PPC_EABI_LinkSymbolValue(LinkSym_Find("_heap_addr")->index) + data_004a2870;
        case -5:
            return data_004a2218;
        case -6:
        case -7:
        case -12:
            return link->piece->x10;
        case -8:
        case -9:
            if (expr)
                return expr->value;
            if (link->made)
                CompilerTools_ReportDiagnostic(0x63, link->name);
            return 0;
        case -10:
            return data_004a2e2a;
        case -11:
            return data_004a2e2e;
        default:
            return 0;
        }
    }

    if (expr)
        return expr->value;
    CompilerTools_ReportDiagnostic(0x63, link->name);
    return 0;
}

/* Whether a small data relocation (R_PPC_EMB_SDA21) of FILE (NAME: the referring symbol's name) to the global
   symbol GLOBAL is allowed: its target must be in a small data section (or be _SDA_BASE_ or _SDA2_BASE_);
   otherwise it is reported (0x32, then 0x8e), counted as an error and the target marked as kept. */
/* 0x450df0 */
Boolean ELF_PPC_EABI_CheckSdaReference(ELRelocation *rel, ELPiece *global, char *name, long file) {
    ELSymbol *symbol;
    unsigned short section;
    long targetFile;
    unsigned char kind;
    Boolean ok;
    char *symbolName;
    char *sectionName;
    char *fileName;
    char *fileOf;
    char *fileArchive;
    char *fileQuote;
    char *targetName;
    char *targetOf;
    char *targetArchive;
    char *targetQuote;

    ok = 0;
    if ((rel->info & 0xff) != 0x6d)
        return 1;
    symbol = global->symbol;
    section = symbol->section;
#if LINKER >= 27 || LINKER == 13
    /* (2.7 and 1.3.2: a linker generated symbol is absolute, so checked) */
    if (section == 0 && global->file >= 0)
#else
    if (section == 0)
#endif
        return 1;

    targetFile = global->file;
    if (targetFile >= 0 && section <= data_004a28dc[targetFile].image->section_header_count) {
        kind = data_004a28dc[targetFile].sections[section].kind;
        ok = kind == 0xd || kind == 0xe || kind == 0xf || kind == 0x10 || kind == 0x11 || kind == 0x12;
    } else if ((unsigned long)(targetFile + 9) <= 1) {
        ok = 1; /* _SDA2_BASE_ (-9) or _SDA_BASE_ (-8) */
    }

    if (!ok) {
        symbolName = LK_symbol_Name(targetFile, symbol);
        global->symbol->flags |= 0x10;
        data_004a28f4++;
        targetFile = global->file;
        if (targetFile > -1)
            sectionName = data_004a28dc[targetFile].sections[symbol->section].name;
        else
            sectionName = "the equivalent of a large data section";
        EPED_Where(targetFile, &targetName, &targetOf, &targetArchive, &targetQuote);
        EPED_Where(file, &fileName, &fileOf, &fileArchive, &fileQuote);
        CompilerTools_BufferDiagnostic(0x32, rel->info & 0xff, name, fileName, fileOf, fileArchive, fileQuote, symbolName, targetName,
                    targetOf, targetArchive, targetQuote, sectionName);
        CompilerTools_ReportBufferedDiagnostic(0x8e);
    }
    return ok;
}

/* Applies the relocations of the kept pieces of SECTION (of object FILE) to its data, by PowerPC relocation
   type: a value out of range or an unknown type is reported and counted as an error. */
/* 0x44f9a0 */
void ELF_PPC_EABI_ApplyRelocations(long file, ELSection *section) {
    ELPiece *piece;
    ELRelocNode *node;
    ELRelocation *rel;
    ELSymbol *symbol;
    ELSymbol *found;
    long foundFile;
    unsigned long *where;
    unsigned long insn;
    unsigned long value;
    unsigned char type;
    unsigned char kind;

    for (piece = section->pieces; piece; piece = piece->next) {
        if (!(piece->symbol->flags & 0x10) || (piece->symbol->flags & 0x20))
            continue;
        for (node = piece->relocations; node; node = node->next) {
            rel = node->rel;
            symbol = (ELSymbol *)data_004a28dc[file].symbols->data + (rel->info >> 8);
            if (!(symbol->flags & 0x10))
                continue;
            type = rel->info & 0xff;
            /* The original's address is zeroed for the types other than 1, 3 to 6, 0x18 and 0x6d where it
               applies that test: never, as only those use it. */
            where = (unsigned long *)(section->data + (rel->offset & ~3));

            switch (type) {
            case 1:    /* R_PPC_ADDR32 */
            case 0x18: /* R_PPC_UADDR32 */
                value = CTool_EndianConvertWord32(ELF_gen_SymbolAddress(symbol, file) + rel->addend);
                memcpy(section->data + rel->offset, &value, 4);
                break;

            case 2: /* R_PPC_ADDR24 */
                value = ELF_gen_SymbolAddress(symbol, file);
                insn = CTool_EndianConvertWord32(*where);
                value += rel->addend;
                if ((long)value < -0x2000000 || (long)value > 0x1fffffc) {
                    EPED_RelocError(0x43, rel, file, symbol);
                    data_004a28f4++;
                } else {
                    *where = CTool_EndianConvertWord32((insn & 0xfc000003) | (value & 0x3fffffc));
                }
                break;

            case 3: /* R_PPC_ADDR16 */
                value = ELF_gen_SymbolAddress(symbol, file);
                insn = CTool_EndianConvertWord32(*where);
                value += rel->addend;
                if ((long)value < -0x8000 || (long)value > 0x7ffc) {
                    EPED_RelocError(0x43, rel, file, symbol);
                    data_004a28f4++;
                } else {
                    *where = CTool_EndianConvertWord32((insn & 0xffff0000) | (value & 0xffff));
                }
                break;

            case 4: /* R_PPC_ADDR16_LO */
                value = ELF_gen_SymbolAddress(symbol, file);
                insn = CTool_EndianConvertWord32(*where);
                value += rel->addend;
                *where = CTool_EndianConvertWord32((insn & 0xffff0000) | (value & 0xffff));
                break;

            case 5: /* R_PPC_ADDR16_HI */
                value = ELF_gen_SymbolAddress(symbol, file);
                insn = CTool_EndianConvertWord32(*where);
                value += rel->addend;
                *where = CTool_EndianConvertWord32((insn & 0xffff0000) | ((value >> 16) & 0xffff));
                break;

            case 6: /* R_PPC_ADDR16_HA */
                value = ELF_gen_SymbolAddress(symbol, file);
                insn = CTool_EndianConvertWord32(*where);
                value += rel->addend;
                if (value & 0x8000)
                    value += 0x10000;
                *where = CTool_EndianConvertWord32((insn & 0xffff0000) | ((value >> 16) & 0xffff));
                break;

            case 7: /* R_PPC_ADDR14 */
                value = ELF_gen_SymbolAddress(symbol, file);
                insn = CTool_EndianConvertWord32(*where);
                value += rel->addend;
                if ((long)value < -0x8000 || (long)value > 0x7ffc) {
                    EPED_RelocError(0x43, rel, file, symbol);
                    data_004a28f4++;
                } else {
                    *where = CTool_EndianConvertWord32((insn & 0xffff0003) | (value & 0xfffc));
                }
                break;

            case 10: /* R_PPC_REL24: from the piece's address in the output */
                value = ELF_gen_SymbolAddress(symbol, file);
                value -= rel->offset - piece->symbol->value + piece->x10;
                insn = CTool_EndianConvertWord32(*where);
                value += rel->addend;
                if ((long)value < -0x2000000 || (long)value > 0x1fffffc) {
                    EPED_RelocError(0x43, rel, file, symbol);
                    data_004a28f4++;
                } else {
                    *where = CTool_EndianConvertWord32((insn & 0xfc000003) | (value & 0x3fffffc));
                }
                break;

            case 11: /* R_PPC_REL14 */
                value = ELF_gen_SymbolAddress(symbol, file);
                value -= rel->offset - piece->symbol->value + piece->x10;
                insn = CTool_EndianConvertWord32(*where);
                value += rel->addend;
                if ((long)value < -0x8000 || (long)value > 0x7ffc) {
                    EPED_RelocError(0x43, rel, file, symbol);
                    data_004a28f4++;
                } else {
                    *where = CTool_EndianConvertWord32((insn & 0xffff0003) | (value & 0xfffc));
                }
                break;

            case 0x1a: /* R_PPC_REL32 */
                value = ELF_gen_SymbolAddress(symbol, file);
                value -= rel->offset - piece->symbol->value + piece->x10;
                value += rel->addend;
                *where = CTool_EndianConvertWord32(value);
                break;

            case 0x6d: /* R_PPC_EMB_SDA21: from the base of the target's small data section */
                value = ELF_gen_SymbolAddress(symbol, file);
                insn = CTool_EndianConvertWord32(*where);
                value += rel->addend;
                ELF_gen_ResolveSymbol(symbol, file, &found, &foundFile);
                if (foundFile < 0)
                    kind = 0x11;
                else
                    kind = data_004a28dc[foundFile].sections[found->section].kind;
                if (kind == 0xd || kind == 0xe) {
                    if (!(insn & 0x1f0000))
                        insn |= 0xd0000; /* r13 */
                    value -= ELF_PPC_EABI_LinkSymbolValue(-8);
                } else if (kind == 0xf || kind == 0x10) {
                    if (!(insn & 0x1f0000))
                        insn |= 0x20000; /* r2 */
                    value -= ELF_PPC_EABI_LinkSymbolValue(-9);
                } else if (kind != 0x11 && kind != 0x12) {
                    EPED_SdaError("Wrong section", rel, file, symbol);
                    data_004a28f4++;
                }
                if ((long)value < -0x8000 || (long)value > 0x7ffc) {
                    EPED_SdaError("Range Error", rel, file, symbol);
                    data_004a28f4++;
                } else {
                    *where = CTool_EndianConvertWord32((insn & 0xffff0000) | (value & 0xffff));
                }
                break;

            default:
                EPED_RelocError(0x3c, rel, file, symbol);
                data_004a28f4++;
                break;
            }
        }
    }
}

/* 0x44f630 */
void ELF_PPC_EABI_RelocateSection(ELSection *section) {
    long file;
    ELPiece *piece;
    ELRelocNode *node;
    ELRelocation *rel;
    ELSymbol *symbol;
    ELSymbol *target;
    long targetFile;
    unsigned long value;
    unsigned long addend;
    unsigned long word;
    unsigned long type;
    ELObject *object;

    if (!section->relocations)
        return;
    file = section->file;
#if LINKER == 13
    /* (1.3.2: every relocation of the section, its pieces' and targets' kept or not) */
    {
        unsigned long count = section->relocations->header->size / 12;
        unsigned long k;

        for (k = 0; k < count; k++) {
            rel = (ELRelocation *)(section->relocations->data + k * 12);
            object = &data_004a28dc[file];
            symbol = (ELSymbol *)object->symbols->data + (rel->info >> 8);
#else
    for (piece = section->pieces; piece; piece = piece->next) {
        if (!(piece->symbol->flags & 0x10) || (piece->symbol->flags & 0x20))
            continue;
        for (node = piece->relocations; node; node = node->next) {
            rel = node->rel;
            object = &data_004a28dc[file];
            symbol = (ELSymbol *)object->symbols->data + (rel->info >> 8);
            if (!(symbol->flags & 0x10))
                continue;
#endif
            type = rel->info & 0xff;
            if (type == 0)
                continue;
            if (type == 1 || type == 0x18) {
                addend = rel->addend;
                ELF_gen_ResolveSymbol(symbol, file, &target, &targetFile);
                if (targetFile < 0) {
                    value = LinkSym_FindIndex(targetFile)->piece->x10;
                } else if (target->section == 0xfff1) {
                    value = target->value;
                } else if ((target->info & 0xf) == 3) {
                    ELChunk *chunk = target->piece->chunk;
                    if (chunk->xc + addend > (unsigned long)chunk->x10)
                        value = chunk->x10;
                    else
                        value = chunk->xc;
                } else {
                    value = target->piece->x10;
                }
                if (rel->offset & 3) {
                    word = CTool_EndianConvertWord32(value + rel->addend);
                    memcpy(section->data + rel->offset, &word, 4);
                } else {
                    unsigned int *p = (unsigned int *)(section->data + (rel->offset & ~3));
                    *p = value + rel->addend;
                    CTool_EndianConvertInPlaceWord32Ptr(p);
                }
            } else {
                char *member;
                char *objectName;
                char *ofArchive;
                char *archive;
                char *quote;

                member = (file > -1 && object->memberName) ? object->fileSymbol->name : "";
                if (*member) {
                    objectName = member;
                    ofArchive = " of archive '";
                    archive = file > -1 ? object->file->name : "linker generated symbol";
                    quote = "'";
                } else {
                    objectName = file > -1 ? object->file->name : "linker generated symbol";
                    ofArchive = "";
                    archive = "";
                    quote = "";
                }
                CompilerTools_ReportLimitedDiagnostic(0x3c, type, LK_symbol_Name(file, symbol), objectName, ofArchive, archive, quote);
                data_004a28f4++;
            }
        }
    }
}

/* Makes an output section of SECTIONS (kind 1 a section, 2 a debug one, 3 another, 5 a GROUP, 6 an assignment)
   with its type (oc9); a section (1 or 2) gets its .rela section, a section of kind 1 its .mwcats section when
   the procedure records are read (and that its .rela section with data_004a2e49). */
/* 0x44f430 */
ELOutput *ELF_PPC_EABI_NewOutput(char *name, unsigned char type, unsigned char kind) {
    ELOutput *output;
    ELOutput *rela;
    ELOutput *records;
    ELOutput *recordsRela;

    output = EPEC_AllocOutput();
    output->name = name;
    output->oc9 = type;
    output->kind = kind;
    if ((unsigned char)(kind - 1) <= 1) {
        rela = EPEC_AllocOutput();
        rela->name = galloc(strlen(name) + 5 + 1);
        strcpy(rela->name, ".rela");
        strcat(rela->name, name);
        rela->oc9 = 0x1a;
        rela->kind = 3;
        output->ob0 = (unsigned long)rela;
        rela->ob4 = (unsigned long)output;
    }
    if (data_004a2e40 && kind == 1) {
        records = EPEC_AllocOutput();
        records->name = galloc(strlen(name) + 7 + 1);
        strcpy(records->name, ".mwcats");
        strcat(records->name, name);
        records->oc9 = 0x13;
        records->kind = 4;
        output->xb8 = records;
        records->ob4 = (unsigned long)output;
        if (data_004a2e49) {
            recordsRela = EPEC_AllocOutput();
            recordsRela->name = galloc(strlen(records->name) + 5 + 1);
            strcpy(recordsRela->name, ".rela");
            strcat(recordsRela->name, records->name);
            recordsRela->oc9 = 0x1a;
            recordsRela->kind = 3;
            records->ob0 = (unsigned long)recordsRela;
            recordsRela->ob4 = (unsigned long)records;
        }
    }
    return output;
}

/* The item before .ctors' (.dtors') first ".ctors" item, or NULL (none, or it is the first). */
static ELCmdItem *EPEC_ItemBefore(ELOutput *output, char *name) {
    ELCmdItem *prev = NULL;
    ELCmdItem *item;

    for (item = output->items; item; item = item->next) {
        if (item->section && strcmp(item->section, name) == 0)
            return prev;
        prev = item;
    }
    return NULL;
}

/* Puts __init_cpp_exceptions.o's .ctors first in .ctors and the "sinit" sections after the .ctors item. */
/* 0x44f250 */
void ELF_PPC_EABI_AddCtorItems(void) {
    ELOutput *ctors = data_004a2e1a;
    ELCmdItem *prev;
    ELCmdItem *item;
    ELCmdItem *match;
    ELCmdItem *after;

    if (!ctors)
        return;
    prev = EPEC_ItemBefore(ctors, ".ctors");
    if (prev) {
        match = prev->next;
        item = EPEC_NewItem(".ctors", "__init_cpp_exceptions.o");
        prev->next = item;
        item->found = 1;
        item->next = match;
        after = match->next;
        item = EPEC_NewItem("sinit", 0);
        match->next = item;
        item->next = after;
    } else if (!ctors->items) {
        ctors->items = EPEC_NewItem(".ctors", "__init_cpp_exceptions.o");
        ctors->items->found = 1;
        ctors->items->next = EPEC_NewItem(".ctors", 0);
        ctors->items->next->next = EPEC_NewItem("sinit", 0);
    } else {
        match = ctors->items;
        item = EPEC_NewItem(".ctors", "__init_cpp_exceptions.o");
        ctors->items = item;
        item->found = 1;
        item->next = match;
        after = match->next;
        item = EPEC_NewItem("sinit", 0);
        match->next = item;
        item->next = after;
    }
}

/* Puts __init_cpp_exceptions.o's .dtors first in .dtors and global_destructor_chain.o's after the .dtors item. */
/* 0x44f050 */
void ELF_PPC_EABI_AddDtorItems(void) {
    ELOutput *dtors = data_004a2e1e;
    ELCmdItem *prev;
    ELCmdItem *item;
    ELCmdItem *match;
    ELCmdItem *after;

    if (!dtors)
        return;
    prev = EPEC_ItemBefore(dtors, ".dtors");
    if (prev) {
        match = prev->next;
        item = EPEC_NewItem(".dtors", "__init_cpp_exceptions.o");
        prev->next = item;
        item->found = 1;
        item->next = match;
        after = match->next;
        item = EPEC_NewItem(".dtors", "global_destructor_chain.o");
        match->next = item;
        item->found = 1;
        item->next = after;
    } else if (!dtors->items) {
        dtors->items = EPEC_NewItem(".dtors", "__init_cpp_exceptions.o");
        dtors->items->found = 1;
        item = dtors->items;
        item->next = EPEC_NewItem(".dtors", "global_destructor_chain.o");
        item->next->found = 1;
        item->next->next = EPEC_NewItem(".dtors", 0);
    } else {
        match = dtors->items;
        item = EPEC_NewItem(".dtors", "__init_cpp_exceptions.o");
        dtors->items = item;
        item->found = 1;
        item->next = match;
        after = match->next;
        item = EPEC_NewItem(".dtors", "global_destructor_chain.o");
        match->next = item;
        item->found = 1;
        item->next = after;
    }
}

/* Makes .sdata2/.sbss2 and .PPC.EMB.sdata0/.PPC.EMB.sbss0 after prev, each pair linked by oac; returns the last. */
/* 0x44efd0 */
ELOutput *ELF_PPC_EABI_AddSmallData2And0(ELOutput *prev) {
    ELOutput *data;
    ELOutput *bss;

    data = ELF_PPC_EABI_NewOutput(".sdata2", 0xf, 1);
    prev->next = data;
    bss = ELF_PPC_EABI_NewOutput(".sbss2", 0x10, 1);
    data->next = bss;
    data->oac = (unsigned long)bss;
    bss->oac = (unsigned long)data;
    data = ELF_PPC_EABI_NewOutput(".PPC.EMB.sdata0", 0x11, 1);
    bss->next = data;
    bss = ELF_PPC_EABI_NewOutput(".PPC.EMB.sbss0", 0x12, 1);
    data->next = bss;
    data->oac = (unsigned long)bss;
    bss->oac = (unsigned long)data;
    return bss;
}

/* The command file's output section of that name (not a GROUP or an assignment), or NULL. */
/* 0x44ef80 */
ELOutput *ELF_PPC_EABI_FindNamedOutput(char *name) {
    ELOutput *output;

    for (output = data_004a28e4->sections; output; output = output->next) {
        if (output->kind != 5 && output->kind != 6 && output->name && name && strcmp(output->name, name) == 0)
            break;
    }
    return output;
}

/* The output section of the entry point (__start, or the -main symbol): for a section of SHF_... 0x40000000
   the output section of its name, otherwise its own; when there is none, .init; neither is an error (0x8c, or
   0x8d with ROM images). */
/* 0x44edb0 */
ELOutput *ELF_PPC_EABI_EntryOutput(void) {
    ELPiece *entry;
    ELSection *section;
    ELOutput *output;

    entry = LK_symbol_Find("__start", -1);
    if (!entry)
        entry = LK_symbol_Find(data_0049c161, -1);
    if (entry) {
        section = &data_004a28dc[entry->file].sections[entry->symbol->section];
        if (!(section->header->flags & 0x40000000))
            return section->output;
        if ((output = EPEC_FindUncompressed(section->name)) != NULL)
            return output;
        if ((output = EPEC_FindUncompressed(".init")) != NULL)
            return output;
        CompilerTools_ReportLimitedDiagnostic(data_004a2e45 ? 0x8d : 0x8c);
        CompilerTools_FormatMessageAndLongjmp(0xd, 2);
    }
    if ((output = EPEC_FindUncompressed(".init")) != NULL)
        return output;
    CompilerTools_ReportLimitedDiagnostic(data_004a2e45 ? 0x8d : 0x8c);
    CompilerTools_FormatMessageAndLongjmp(0xd, 2);
    return NULL;
}

/* With the command file's two output sections both there, the second must be the first's next output
   section (error 0x71). */
/* 0x44ece0 */
void ELF_PPC_EABI_CheckFollows(char *first, char *second) {
    ELOutput *a;
    ELOutput *b;
    ELOutput *output;

    a = ELF_PPC_EABI_FindNamedOutput(first);
    b = ELF_PPC_EABI_FindNamedOutput(second);
    if (!b || !a)
        return;
    for (output = a->next; output; output = output->next) {
        if (output->kind == 1) {
            if (output != b) {
                CompilerTools_ReportLimitedDiagnostic(0x71, first, second);
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
            }
            return;
        }
    }
}

/* A compressed section (.compress.init...): one item, the input sections of that name, type 0x40000006. */
static ELOutput *EPEC_NewCompressed(char *name, char *section) {
    ELOutput *output = ELF_PPC_EABI_NewOutput(name, 9, 1);
    output->items = EPEC_NewItem(section, 0);
    output->items->type = 0x40000006;
    return output;
}

/* Adds an output section to the list the memory map shows after the loaded ones. */
static void EPEC_ListUnloaded(ELOutput *output) {
    ELOutputNode *node = galloc(sizeof(ELOutputNode));
    node->next = NULL;
    node->output = NULL;
    node->next = data_004a2de6;
    data_004a2de6 = node;
    node->output = output;
}

/* Makes the .comment header and, with a command file, checks .sdata/.sbss and .sdata2/.sbss2 follow each other;
   without one, makes the default output sections. */
/* 0x44e930 */
void ELF_PPC_EABI_MakeOutputSections(void) {
    ELOutput *o;
    ELOutput *last;

    data_004a2e0a = galloc(0x2c);
    memset(data_004a2e0a, 0, 0x2c);
    ELF_PPC_EABI_MakeCommentHeader(data_004a2e0a);
    if (data_004a2e32) {
        ELF_PPC_EABI_CheckFollows(".sdata", ".sbss");
        ELF_PPC_EABI_CheckFollows(".sdata2", ".sbss2");
        return;
    }
    if (!data_004a28e4) {
        data_004a28e4 = galloc(0x1e);
        memset(data_004a28e4, 0, 0x1e);
    }
    o = EPEC_NewCompressed(".compress.init", ".init");
    data_004a28e4->sections = o;
    o = o->next = ELF_PPC_EABI_NewOutput(".init", 2, 1);
    o = o->next = EPEC_NewCompressed(".compress.text", ".text");
    o = o->next = ELF_PPC_EABI_NewOutput(".text", 2, 1);
    o = o->next = EPEC_NewCompressed(".compress.fini", ".fini");
    o = o->next = ELF_PPC_EABI_NewOutput(".fini", 2, 1);
    o = o->next = ELF_PPC_EABI_NewOutput(".rodata", 8, 1);
    o = o->next = data_004a2e1a = ELF_PPC_EABI_NewOutput(".ctors", 0x1c, 1);
    o = o->next = data_004a2e1e = ELF_PPC_EABI_NewOutput(".dtors", 0x1d, 1);
    o = o->next = ELF_PPC_EABI_NewOutput("extab", 6, 1);
    o = o->next = data_004a2e16 = ELF_PPC_EABI_NewOutput("extabindex", 7, 1);
    o = o->next = ELF_PPC_EABI_NewOutput(".BINARY", 0x1e, 1);
    o = o->next = ELF_PPC_EABI_NewOutput(".data", 10, 1);
#if LINKER >= 27
    o = o->next = ELF_PPC_EABI_NewOutput(".got", 10, 1);
#endif
    o = o->next = ELF_PPC_EABI_NewOutput(".sdata", 0xd, 1);
    o = o->next = ELF_PPC_EABI_NewOutput(".sbss", 0xe, 1);
    last = ELF_PPC_EABI_AddSmallData2And0(o);
    o = last->next = ELF_PPC_EABI_NewOutput(".bss", 0xc, 1);
    o = o->next = ELF_PPC_EABI_NewOutput(".line", 0x15, 2);
    o = o->next = ELF_PPC_EABI_NewOutput(".debug", 0x14, 2);
    EPEC_ListUnloaded(o);
    o = o->next = ELF_PPC_EABI_NewOutput(".debug_sfnames", 0x14, 2);
    EPEC_ListUnloaded(o);
    o = o->next = ELF_PPC_EABI_NewOutput(".debug_srcinfo", 0x14, 2);
    EPEC_ListUnloaded(o);
    o = o->next = ELF_PPC_EABI_NewOutput(".shstrtab", 0x18, 3);
    o = o->next = ELF_PPC_EABI_NewOutput(".strtab", 0x17, 3);
    o = o->next = ELF_PPC_EABI_NewOutput(".symtab", 0x16, 3);
    if (data_004a2e45) {
        o->next = ELF_PPC_EABI_NewOutput(".PPC.EMB.seginfo", 0x19, 3);
    }
}

/* _SDA_BASE_ (-8) and _SDA2_BASE_ (-9): .sdata's and .sdata2's address + 0x8000, unless defined already. */
/* 0x44e720 */
void ELF_PPC_EABI_DefineSmallDataBases(void) {
    EPEB_DefineBase(-8, ".sdata", 0x46f);
    EPEB_DefineBase(-9, ".sdata2", 0x48e);
}

/* The output sections' symbols (_f_, _f_..._rom, _e_), then the small data bases. */
/* 0x44e6f0 */
void ELF_PPC_EABI_AddSectionSymbols(void) {
    ELOutput *out;

    for (out = data_004a28e4->sections; out; out = out->next) {
        if (out->kind == 1)
            LinkSym_AddSectionSymbols(out);
    }
    ELF_PPC_EABI_DefineSmallDataBases();
}

/* The kind of a loaded section by its name (sinit, extab, extabindex, the small data, .ctors, .dtors), 0 for another. */
/* 0x44e520 */
unsigned char ELF_PPC_EABI_LoadedSectionKind(char *name) {
    if (name && !strcmp(name, "sinit"))
        return 3;
    if (name && !strcmp(name, "extab"))
        return 6;
    if (name && !strcmp(name, "extabindex"))
        return 7;
    if (name && !strcmp(name, ".sdata"))
        return 0xd;
    if (name && !strcmp(name, ".sbss"))
        return 0xe;
    if (name && !strcmp(name, ".sdata2"))
        return 0xf;
    if (name && !strcmp(name, ".sbss2"))
        return 0x10;
    if (name && !strcmp(name, ".PPC.EMB.sdata0"))
        return 0x11;
    if (name && !strcmp(name, ".PPC.EMB.sbss0"))
        return 0x12;
    if (name && !strcmp(name, ".ctors"))
        return 0x1c;
    if (name && !strcmp(name, ".dtors"))
        return 0x1d;
    return 0;
}

/* The kind of a section by its name (.symtab, .strtab, .shstrtab, .debug..., .line, .comment, .BINARY, or a loaded one). */
/* 0x44e3c0 */
unsigned char ELF_PPC_EABI_SectionKind(char *name) {
    if (!name || !*name)
        return 0;
    if (!strcmp(name, ".symtab"))
        return 0x16;
    if (!strcmp(name, ".strtab"))
        return 0x17;
    if (!strcmp(name, ".shstrtab"))
        return 0x18;
    if (!strcmp(name, ".debug"))
        return 0x14;
    if (!strcmp(name, ".line"))
        return 0x15;
    if (!strncmp(name, ".debug", 6))
        return 0x14;
    if (!strcmp(name, ".comment"))
        return 0x1b;
    if (!strcmp(name, ".BINARY"))
        return 0x1e;
    return ELF_PPC_EABI_LoadedSectionKind(name);
}

/* An output section's ELF types and flags by its kind (oc9); its relocation and record sections done too. */
/* 0x44dcc0 */
void ELF_PPC_EABI_SetSectionType(ELOutput *out) {
    switch (out->oc9) {
    case 1:
        if (data_004a2de2)
            EPEB_ASSERT(0x501);
        data_004a2de2 = out;
        out->o7c = 0;
        out->o90 = 0;
        out->oce = 1;
        break;
    case 0x16:
        if (data_004a2dee)
            EPEB_ASSERT(0x509);
        data_004a2dee = out;
        out->o7c = 2;
        out->type = 0;
        out->o9c = 0x10;
        out->align98 = 4;
        out->oce = 1;
        break;
    case 0x17:
        if (data_004a2df2)
            EPEB_ASSERT(0x513);
        data_004a2df2 = out;
        out->o7c = 3;
        out->type = 0;
        out->o9c = 1;
        out->align98 = 1;
        out->oce = 1;
        break;
    case 0x18:
        if (data_004a2df6)
            EPEB_ASSERT(0x51d);
        data_004a2df6 = out;
        out->o7c = 3;
        out->type = 0;
        out->o9c = 1;
        out->align98 = 1;
        out->oce = 1;
        break;
    case 0x14:
        out->o7c = 1;
        out->type = 0;
        out->align98 = 4;
        out->o9c = 1;
        out->oce = 1;
        break;
    case 0x15:
        if (data_004a2dea)
            EPEB_ASSERT(0x534);
        data_004a2dea = out;
        out->o7c = 1;
        out->type = 0;
        out->align98 = 4;
        out->o9c = 1;
        out->oce = 1;
        break;
    case 2:
        out->o7c = 1;
        out->type = 6;
        out->align98 = 4;
        out->o58 = 1;
        out->o70 = 5;
        out->align74 = out->align98;
        out->oce = 0;
        break;
    case 9:
        out->o7c = 1;
        out->type = 0x40000006;
        out->align98 = 4;
        out->o58 = 1;
        out->o70 = 5;
        out->align74 = out->align98;
        out->oce = 0;
        break;
    case 0xb:
        out->o7c = 1;
        out->type = 7;
        out->align98 = 4;
        out->o58 = 1;
        out->o70 = 7;
        out->align74 = out->align98;
        out->oce = 0;
        break;
    case 0xa:
        out->o7c = 1;
        out->type = 3;
        out->align98 = 4;
        out->o58 = 1;
        out->o70 = 6;
        out->align74 = out->align98;
        out->oce = 0;
        break;
    case 3:
    case 0x1c:
    case 0x1d:
        out->o7c = 1;
        out->type = 2;
        out->o9c = 1;
        out->align98 = 4;
        out->o58 = 1;
        out->o70 = 4;
        out->align74 = out->align98;
        out->oce = 0;
        break;
    case 0xc:
        out->o7c = 8;
        out->type = 3;
        out->align98 = 4;
        out->o58 = 1;
        out->o70 = 6;
        out->align74 = out->align98;
        out->oce = 0;
        break;
    case 6:
    case 7:
    case 8:
    case 0x1e:
        out->o7c = 1;
        out->type = 2;
        out->align98 = 4;
        out->o58 = 1;
        out->o70 = 4;
        out->align74 = out->align98;
        out->oce = 0;
        break;
    case 0xd:
    case 0x11:
        out->o7c = 1;
        out->type = 3;
        out->align98 = 4;
        out->o58 = 1;
        out->o70 = 6;
        out->align74 = out->align98;
        out->oce = 1;
        break;
    case 0xe:
    case 0x10:
        out->o7c = 8;
        out->type = 3;
        out->align98 = 4;
        out->o58 = 1;
        out->o70 = 6;
        out->align74 = out->align98;
        out->oce = 1;
        break;
    case 0xf:
        out->o7c = 1;
        out->type = 2;
        out->align98 = 4;
        out->o58 = 1;
        out->o70 = 4;
        out->align74 = out->align98;
        out->oce = 1;
        break;
    case 0x12:
        out->o7c = 8;
        out->type = 3;
        out->align98 = 4;
        out->o58 = 1;
        out->o70 = 6;
        out->align74 = out->align98;
        out->oce = 1;
        break;
    case 0x1a:
        out->o7c = 4;
        out->o9c = 0xc;
        out->type = 0;
        out->align98 = 4;
        out->oce = 1;
        break;
    case 0x13:
        out->o7c = 0xca2a82c2;
        out->o9c = 1;
        out->type = 0;
        out->align98 = 4;
        out->oce = 1;
        break;
    case 0x19:
        if (data_004a2dfa)
            EPEB_ASSERT(0x5e7);
        data_004a2dfa = out;
        out->o7c = 1;
        out->type = 0;
        out->o84 = 0;
        out->o94 = 0;
        out->o9c = 0xc;
#if LINKER >= 27
        out->align98 = 1; /* (2.7: byte aligned) */
#else
        out->align98 = 0;
#endif
        out->oce = 1;
        break;
    case 0x1b:
        data_004a2e0e = out;
        out->o7c = 1;
        out->type = 0;
        out->align98 = 4;
        out->o9c = 1;
        out->oce = 1;
        break;
    }
    if (out->ob0)
        ELF_PPC_EABI_SetSectionType((ELOutput *)out->ob0);
    if (data_004a2e40 && out->xb8) {
        ELF_PPC_EABI_SetSectionType(out->xb8);
        if (out->xb8->ob0)
            ELF_PPC_EABI_SetSectionType((ELOutput *)out->xb8->ob0);
    }
}

/* Pairs a small data section with its BSS (.sdata/.sbss, .sdata2/.sbss2, sdata0/sbss0) through oac. */
static void EPEB_PairSmallData(char *data, char *bss) {
    ELOutput *a;
    ELOutput *b;

    a = EPEB_FindOutput(data);
    if (a) {
        b = EPEB_FindOutput(bss);
        if (b) {
            a->oac = (unsigned long)b;
            b->oac = (unsigned long)a;
        }
    }
}

/* The command file's output sections given their kinds, types and (sections and GROUPs) their own name's item; the
   small data sections paired. */
/* 0x44da50 */
void ELF_PPC_EABI_PrepareOutputs(void) {
    ELOutput *out;

    for (out = data_004a28e4->sections; out; out = out->next) {
        if (!out->oc9)
            out->oc9 = ELF_PPC_EABI_SectionKind(out->name);
        ELF_PPC_EABI_SetSectionType(out);
        if ((out->kind == 1 || out->kind == 2 || out->oc9 == 0x1b) && !out->items)
            out->items = EPEB_NewItem(out->kind, out->name);
    }
    EPEB_PairSmallData(".sdata", ".sbss");
    EPEB_PairSmallData(".sdata2", ".sbss2");
    EPEB_PairSmallData(".PPC.EMB.sdata0", ".PPC.EMB.sbss0");
}

/* The start of the link's output: the globals cleared, the command file read, its sections prepared and .comment added. */
/* 0x44d950 */
void ELF_PPC_EABI_ReadCommandFile(void) {
    ELOutput *out;
    ELOutput *last;

    data_004a294c = 0;
    data_004a2934 = NULL;
    /* 0x4a2dd2 to 0x4a2e34: the output's globals (data_004a2dd2 to data_004a2e32) */
    memset(&data_004a2dd2, 0, 0x62);
    if (data_004a2e45) {
        data_004a2dd2 = data_004a28f8;
        data_004a2dd6 = data_004a2a80;
    }
    ELF_PPC_EABI_SetSectionType(ELF_PPC_EABI_NewOutput("", 1, 3));
    data_004a2e32 = 0;
    if (data_004a2e36)
        data_004a2e32 = ReadLinkerCommandFile();
    ELF_PPC_EABI_MakeOutputSections();
#if LINKER < 27
    ELF_PPC_EABI_AddCtorItems();
    ELF_PPC_EABI_AddDtorItems();
#endif
    ELF_PPC_EABI_PrepareOutputs();
    out = ELF_PPC_EABI_NewOutput(".comment", 0x1b, 3);
    for (last = data_004a28e4->sections; last->next; last = last->next)
        ;
    last->next = out;
    ELF_PPC_EABI_SetSectionType(out);
    out->items = EPEB_NewItem(out->kind, out->name);
}

/* Whether a file name matches a command file's pattern: one ends with the other at a path separator (0 if so). */
/* 0x44d870 */
int ELF_PPC_EABI_MatchFileName(char *pattern, char *name) {
    long patternLength;
    long nameLength;
    long n;
    char *p;
    char *q;

    patternLength = strlen(pattern);
    nameLength = strlen(name);
    p = pattern + patternLength;
    q = name + nameLength;
    n = patternLength > nameLength ? nameLength : patternLength;
    while (n) {
        q--;
        p--;
        if (*p != *q)
            return 1;
        n--;
    }
    if (patternLength == nameLength)
        return 0;
    if (patternLength > nameLength) {
        if (*p == '\\' || *p == ':' || *p == '/')
            return 0;
        p--;
        if (*p == '\\' || *p == ':' || *p == '/')
            return 0;
    } else {
        if (*q == '\\' || *q == ':' || *q == '/')
            return 0;
        q--;
        if (*q == '\\' || *q == ':' || *q == '/')
            return 0;
    }
    return 1;
}

/* For a read-only section (flags 2): whether a candidate's flags suit it better than the best output's so far. */
/* 0x44d7e0 */
#if LINKER >= 27
/* 2.7's input section priorities: a section NAME$NN (two digits) is an input of NAME's, placed by NN in its
   item's sections (0 first; the same priority in the order read); any other is 50. */
static char *EPEC_PriorityMark(char *name) {
    char *mark;

    if (!name)
        return NULL;
    mark = strrchr(name, '$');
    if (!mark || mark[1] < '0' || mark[1] > '9' || mark[2] < '0' || mark[2] > '9' || mark[3])
        return NULL;
    return mark;
}

int ELF_PPC_EABI_SectionPriority(char *name) {
    char *mark = EPEC_PriorityMark(name);

    return mark ? (mark[1] - '0') * 10 + (mark[2] - '0') : 50;
}

/* NAME without its $NN (made once per name read), or NULL when it has none. */
char *ELF_PPC_EABI_BaseName(char *name) {
    char *mark = EPEC_PriorityMark(name);
    char *base;

    if (!mark)
        return NULL;
    base = galloc(mark - name + 1);
    memcpy(base, name, mark - name);
    base[mark - name] = 0;
    return base;
}
#endif

Boolean ELF_PPC_EABI_IsBetterFlags(unsigned long flags, ELOutput *best, unsigned long sectionFlags) {
    unsigned long bestFlags;

    if (!best)
        return 1;
    bestFlags = best->type;
    if (bestFlags == flags)
        return 0;
    if (sectionFlags != 2)
        return 0;
    if (bestFlags == 6)
        return 0;
    if (flags == 6)
        return 1;
    if (bestFlags == 0x40000006)
        return 0;
    if (flags == 0x40000006)
        return 0;
    if (bestFlags == 7)
        return 0;
    if (flags == 7)
        return 1;
    if (bestFlags == 3)
        return 0;
    if (flags == 3)
        return 1;
    return 0;
}

/* Scores an output section (its items, then itself) for an input section: the best so far kept in BEST, SCORE, ITEM. */
/* 0x44d250 */
void ELF_PPC_EABI_ScoreOutput(char *sectionName, char *fileName, char *memberName, ELOutput *out, ELOutput **best,
                              long *score, unsigned long flags, ELCmdItem **itemOut, unsigned char nobits) {
    ELCmdItem *item;
    long value;
    Boolean subset;
    Boolean exact;
    Boolean named;

    for (item = out->items; item; item = item->next) {
        if (item->kind != 1)
            continue;
        if (!item->section) {
            /* a file's (or member's) sections */
            if (!item->file) {
                CompilerTools_ReportLimitedDiagnostic(0x62, out->name);
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
            }
            if (ELF_PPC_EABI_MatchFileName(item->file, fileName)) {
                if (!memberName || item->member || ELF_PPC_EABI_MatchFileName(item->file, memberName))
                    continue;
                if (item->type) {
                    if (EPEC_BSS_MISMATCH(item->bss, nobits) || item->type != flags)
                        continue;
                    *score = 0x11;
                    *itemOut = item;
                    *best = out;
                }
                if (*score >= 0x10)
                    continue;
                *score = 0x10;
            } else if (item->member) {
                if (!memberName) {
                    CompilerTools_ReportLimitedDiagnostic(0x61, item->file, item->member);
                    CompilerTools_FormatMessageAndLongjmp(0xd, 2);
                }
                if (ELF_PPC_EABI_MatchFileName(item->member, memberName))
                    continue;
                if (item->type) {
                    if (EPEC_BSS_MISMATCH(item->bss, nobits) || item->type != flags || *score >= 0x15)
                        continue;
                    *score = 0x15;
                } else {
                    if (*score >= 0x14)
                        continue;
                    *score = 0x14;
                }
            } else if (item->type) {
                if (EPEC_BSS_MISMATCH(item->bss, nobits) || item->type != flags || *score >= 0xd)
                    continue;
                *score = 0xd;
            } else {
                if (*score >= 0xc)
                    continue;
                *score = 0xc;
            }
        } else {
            /* a section by its name (of a file or member) */
            if (!sectionName || strcmp(sectionName, item->section))
                continue;
            if (item->file) {
                if (ELF_PPC_EABI_MatchFileName(item->file, fileName)) {
#if LINKER >= 27
                    /* (2.7: another file's item of the section's name takes it, after that file's) */
                    if (!memberName || item->member || ELF_PPC_EABI_MatchFileName(item->file, memberName)) {
                        if (*score < 8 && !(item->type && (EPEC_BSS_MISMATCH(item->bss, nobits) || item->type != flags))) {
                            *score = 8;
                            *itemOut = item;
                            *best = out;
                        }
                        continue;
                    }
#else
                    if (!memberName || item->member || ELF_PPC_EABI_MatchFileName(item->file, memberName))
                        continue;
#endif
                    if (item->type) {
                        if (EPEC_BSS_MISMATCH(item->bss, nobits) || item->type != flags)
                            continue;
                        *score = 0x13;
                        *itemOut = item;
                        *best = out;
                    }
                    if (*score >= 0x12)
                        continue;
                    *score = 0x12;
                } else if (item->member) {
                    if (!memberName) {
                        CompilerTools_ReportLimitedDiagnostic(0x61, item->file, item->member);
                        CompilerTools_FormatMessageAndLongjmp(0xd, 2);
                    }
                    if (ELF_PPC_EABI_MatchFileName(item->member, memberName))
                        continue;
                    if (item->type) {
                        if (EPEC_BSS_MISMATCH(item->bss, nobits) || item->type != flags)
                            continue;
                        *score = 0x17;
                        *itemOut = item;
                        *best = out;
                        return;
                    }
                    if (*score >= 0x16)
                        continue;
                    *score = 0x16;
                } else if (item->type) {
                    if (EPEC_BSS_MISMATCH(item->bss, nobits) || item->type != flags || *score >= 0xf)
                        continue;
                    *score = 0xf;
                } else {
                    if (*score >= 0xe)
                        continue;
                    *score = 0xe;
                }
            } else if (item->type) {
                if (EPEC_BSS_MISMATCH(item->bss, nobits))
                    continue;
                if (item->type == flags) {
                    if (*score >= 0xb)
                        continue;
                    *score = 0xb;
                } else if ((flags & item->type) == flags) {
                    if (!ELF_PPC_EABI_IsBetterFlags(item->type, *best, flags) || *score >= 7)
                        continue;
                    *score = 7;
                } else {
#if LINKER >= 27
                    /* (2.7: not an item of another kind) */
                    continue;
#endif
                    if (*score >= 6)
                        continue;
                    *score = 6;
                }
            } else {
#if LINKER >= 27
                /* (2.7: the output section of the section's name takes it from another's item) */
                if (*best && *score >= 8 && *score <= 0xa) {
                    Boolean bestOwn = (*best)->name && !strcmp((*best)->name, sectionName);
                    Boolean own = out->name && !strcmp(out->name, sectionName);

                    if (bestOwn && !own)
                        continue;
                    if (own && !bestOwn)
                        *score = 7;
                }
#endif
                if (*score < 8) {
                    *score = 8;
                    *itemOut = item;
                    *best = out;
                }
                value = 0;
                if (out->type == flags)
                    value = 0xa;
                else if ((out->type & flags) == flags &&
                         ELF_PPC_EABI_IsBetterFlags(out->type, *best, flags))
                    value = 9;
                if (*score >= value)
                    continue;
                *score = value;
            }
        }
        *itemOut = item;
        *best = out;
    }

    /* the output section itself, by its name and flags */
    if (*score >= 8)
        return;
#if LINKER >= 27
    /* (2.7: BSS may go to a data section) */
    if (out->type && out->bss != nobits && !nobits)
        return;
#else
    if (out->type && out->bss != nobits)
        return;
#endif
    subset = (out->type & flags) == flags;
    exact = out->type == flags;
    named = out->name && sectionName && !strcmp(out->name, sectionName);
    value = 0;
    if (named) {
        if (exact)
            value = 5;
        else if (subset && ELF_PPC_EABI_IsBetterFlags(out->type, *best, flags))
            value = 4;
#if LINKER >= 27
        /* (2.7: not one of another kind) */
        else if (out->type && !subset)
            value = 0;
#endif
        else
            value = 3;
    } else {
        if (exact)
            value = 2;
        else if (subset && ELF_PPC_EABI_IsBetterFlags(out->type, *best, flags))
            value = 1;
    }
    if (*score < value) {
        *score = value;
        *best = out;
    }
}

/* The output section an object's input section goes to (an item made for it in that section's contents), or NULL. */
/* 0x44ceb0 */
ELOutput *ELF_PPC_EABI_ChooseOutput(ELObject *object, ELSection *section) {
    char *memberName;
    unsigned long flags;
    long score;
    ELOutput *best;
    ELCmdItem *item;
    char *fileName;
    Boolean nobits;
    ELOutput *out;
    ELCmdItem *last;
    ELCmdItem *node;
    char *kind;
#if LINKER >= 27
    char *base;
#endif

    memberName = NULL;
    flags = section->header->flags;
    score = 0;
    best = NULL;
    item = NULL;
    fileName = object->file->objectName;
    if (object->file->flags & EL_FILE_ARCHIVE)
        memberName = object->fileSymbol->name;
    nobits = section->header->type == 8;
    for (out = data_004a28e4->sections; out; out = out->next) {
        if (out->kind == 1)
            ELF_PPC_EABI_ScoreOutput(section->name, fileName, memberName, out, &best, &score, flags, &item, nobits);
    }
#if LINKER >= 27
    /* (2.7: a NAME$NN not named itself goes where NAME would) */
    if (score < 8 && (base = ELF_PPC_EABI_BaseName(section->name)) != NULL) {
        score = 0;
        best = NULL;
        item = NULL;
        for (out = data_004a28e4->sections; out; out = out->next) {
            if (out->kind == 1)
                ELF_PPC_EABI_ScoreOutput(base, fileName, memberName, out, &best, &score, flags, &item, nobits);
        }
    }
#endif
    if (best) {
        switch (section->kind) {
        case 0xd:
        case 0xe:
        case 0xf:
        case 0x10:
        case 0x11:
        case 0x12:
            if (best->oc9 != section->kind) {
                if (score >= 8) {
                    CompilerTools_ReportLimitedDiagnostic(0x67, section->name, object->fileSymbol->name, best->name);
                    CompilerTools_FormatMessageAndLongjmp(0xd, 2);
                }
                return NULL;
            }
            break;
        }
    }
    if (!best || score >= 8) {
        section->x20 = (long)item;
        return best;
    }
    if (score < 3 && section->name && !strncmp(section->name, ".abs.", 5))
        return NULL;

    /* not named by the command file: an item of its own appended to the output section's contents (2.7: NAME's
       for a NAME$NN, shared by them, so that their priorities order them) */
    last = NULL;
#if LINKER >= 27
    base = ELF_PPC_EABI_BaseName(section->name);
    item = NULL;
    for (node = best->items; node; node = node->next) {
        if (base && node->kind == 1 && !node->file && node->section && !strcmp(node->section, base))
            item = node;
        last = node;
    }
    if (!item) {
        item = (ELCmdItem *)galloc(0x20);
        memset(item, 0, 0x20);
        item->kind = 1;
        item->section = base ? base : section->name;
        item->file = NULL;
        if (!last)
            best->items = item;
        else
            last->next = item;
        item->type = flags;
    }
#else
    for (node = best->items; node; node = node->next)
        last = node;
    item = (ELCmdItem *)galloc(0x20);
    memset(item, 0, 0x20);
    item->kind = 1;
    item->section = section->name;
    item->file = NULL;
    if (!last)
        best->items = item;
    else
        last->next = item;
    item->type = flags;
#endif

    if (score < 3) {
        if (section->name && !strcmp(section->name, "extabindex")) {
            if (data_004a2e16)
                CompilerTools_ReportLimitedDiagnostic(0x33, "ELF_PPC_EABI.c", 0x7e3);
            data_004a2e16 = best;
        } else if (section->name && !strcmp(section->name, ".ctors")) {
            if (data_004a2e1a)
                CompilerTools_ReportLimitedDiagnostic(0x33, "ELF_PPC_EABI.c", 0x7e7);
            data_004a2e1a = best;
#if LINKER < 27
            ELF_PPC_EABI_AddCtorItems();
#endif
        } else if (section->name && !strcmp(section->name, ".dtors")) {
            if (data_004a2e1e)
                CompilerTools_ReportLimitedDiagnostic(0x33, "ELF_PPC_EABI.c", 0x7ec);
            data_004a2e1e = best;
#if LINKER < 27
            ELF_PPC_EABI_AddDtorItems();
#endif
        } else if (section->name && !strcmp(section->name, "...BINARY")) {
            section->x20 = (long)item;
            return best;
        }
    }

    switch (flags) {
    case 0:
        kind = "NO PERMISSION";
        break;
    case 2:
        kind = "CONST";
        break;
    case 3:
#if LINKER >= 27
        kind = nobits ? "BSS" : "DATA";
#else
        kind = best->bss ? "BSS" : "DATA";
#endif
        break;
    case 6:
        kind = "CODE";
        break;
    case 0x40000006:
        kind = "ZCODE";
        break;
    case 7:
        kind = "MIXED";
        break;
    default:
        kind = "UNKNOWN";
        break;
    }
    CompilerTools_ReportDiagnostic(0x66, section->name, kind, object->fileSymbol->name, section->name, best->name);
    section->x20 = (long)item;
    return best;
}

#if LINKER >= 27
/* 2.7's warnings of the objects' .comment, kept until the layout: one per message and file, an archive's members
   counted (its first named). */
typedef struct EPECWarning {
    struct EPECWarning *next;
    long message;
    long count;
    char *file;
    char *member;
} EPECWarning;

static EPECWarning *EPEC_Warnings;

/* Keeps a warning of FILE (MEMBER: its archive member, "" for none), unless it is one kept already. */
static void EPEC_KeepWarning(long message, char *file, char *member) {
    EPECWarning *warning;
    EPECWarning *last;

    last = NULL;
    for (warning = EPEC_Warnings; warning; warning = warning->next) {
        if (warning->message == message && !strcmp(file, warning->file))
            break;
        last = warning;
    }
    if (!warning) {
        warning = (EPECWarning *)galloc(sizeof(EPECWarning));
        memset(warning, 0, sizeof(EPECWarning));
        warning->message = message;
        warning->count = 1;
        warning->file = file;
        warning->member = member;
        if (last)
            last->next = warning;
        else
            EPEC_Warnings = warning;
        return;
    }
    if (!*member || !*warning->member) {
        if (*member == *warning->member)
            return;
        if (!*warning->member)
            warning->member = member;
    } else if (!strcmp(member, warning->member)) {
        return;
    }
    warning->count++;
}

/* Reports the warnings kept: one of an archive's members as its own, more as the archive's. */
static void EPEC_ReportWarnings(void) {
    EPECWarning *warning;

    for (warning = EPEC_Warnings; warning; warning = warning->next) {
        if (warning->count == 1) {
            CompilerTools_ReportDiagnostic(warning->message, warning->file, *warning->member ? " " : "",
                                           warning->member);
        } else {
            switch (warning->message) {
            case 0x7a:
                CompilerTools_ReportDiagnostic(0xad, warning->file, warning->count, warning->member);
                break;
            case 0x7f:
                CompilerTools_ReportDiagnostic(0xaf, warning->file, warning->count, warning->member);
                break;
            case 0xac:
                CompilerTools_ReportDiagnostic(0xae, warning->file, warning->count, warning->member);
                break;
            }
        }
    }
    EPEC_Warnings = NULL;
}
#define EPEC_WARN(message, file, member) EPEC_KeepWarning(message, file, member)
#else
#define EPEC_WARN(message, file, member) \
    CompilerTools_ReportDiagnostic(message, file, *(member) ? " " : "", member)
#endif

/* Checks an object's .comment against the output's: the EABI compliance (0x7a) and the version 8 flag (0x7f); 2.7's
   also the char signedness (0xac), all kept until the layout. */
/* 0x44ccc0 */
void ELF_PPC_EABI_CheckComment(ELSection *section) {
    char *comment;
    char *member;
    char value;

    comment = section->data;
    if (comment[0xb] < 3)
        return;
    value = comment[0x11];
    member = EPEB_MemberName(section->file);
    if (data_004a2e0a[0x11] != value) {
        if (!data_0049c41a || (data_0049c41b && (value == 2 || value == 3)) || (!data_0049c41b && value == 1))
            EPEC_WARN(0x7a, EPEB_FileName(section->file), member);
    }
#if LINKER >= 27
    if (comment[0xb] > 0xc && data_004a2e0a[0x16] != comment[0x16])
        EPEC_WARN(0xac, EPEB_FileName(section->file), member);
#endif
    if (comment[0xb] < 8)
        return;
    if (comment[0xf] == 3)
        EPEC_WARN(0x7f, EPEB_FileName(section->file), member);
}

#if LINKER >= 27
/* A BSS section put in a section of data: written as zeros. */
static void EPEC_BssAsData(ELSection *section) {
    unsigned long size = section->header->size;

    section->data = galloc(size ? size : 1);
    memset(section->data, 0, size ? size : 1);
}

/* SECTION, a standard one (.data, .bss...), is named by the command file item that put it in its output section. */
static Boolean EPEC_NamedStandard(ELSection *section) {
    ELCmdItem *item = (ELCmdItem *)section->x20;

    if (!item || item->kind != 1 || !item->section || !section->name || strcmp(item->section, section->name))
        return 0;
    return !strcmp(section->name, ".data") || !strcmp(section->name, ".bss") || !strcmp(section->name, ".text") ||
           !strcmp(section->name, ".rodata");
}
#endif

/* Adds an input section to its output section: the kinds checked against each other, the alignment and the flags. */
/* 0x44c440 */
void ELF_PPC_EABI_AddInput(ELOutput *out, ELSection *section) {
    unsigned long flags;
    unsigned long align;
    char *member;
    char *file;

    flags = section->header->flags;
    if (!out->oc9) {
        out->oc9 = section->kind;
        ELF_PPC_EABI_SetSectionType(out);
    }

    align = section->header->alignment;
    {
        short bit = getbit(align);
        if (bit <= 0 || bit >= 0x1f)
            align = 0;
    }
    if (section->header->alignment != align) {
        member = EPEB_MemberName(section->file);
        file = EPEB_FileName(section->file);
        if (*member)
            CompilerTools_ReportDiagnostic(0x8f, section->name, member, " of archive '", file, "'",
                                           section->header->alignment);
        else
            CompilerTools_ReportDiagnostic(0x8f, section->name, file, "", "", "", section->header->alignment);
        align = 4;
        section->header->alignment = 4;
    }
    if (out->align98 < align) {
        out->align98 = align;
        out->align74 = align;
    }
    if (!out->o7c)
        out->o7c = section->header->type;

    if (!out->oce) {
        switch (section->kind) {
        case 0xd:
        case 0xe:
        case 0xf:
        case 0x10:
        case 0x11:
        case 0x12:
            if (out->oc9 != section->kind) {
                CompilerTools_ReportLimitedDiagnostic(0x5d, out->name, section->name);
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
            }
            break;
        }
        switch (out->oc9) {
        case 0:
            EPEB_ASSERT(0x87d);
            break;
        case 2:
            if (section->header->flags & 0x40000000)
                CompilerTools_ReportLimitedDiagnostic(0x8b, section->name, out->name);
            if (section->header->flags & 1)
                out->oc9 = 0xb;
            if (section->kind == 0xc) {
#if LINKER >= 27
                EPEC_BssAsData(section);
#else
                CompilerTools_ReportLimitedDiagnostic(0x5c, section->name, out->name);
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
#endif
            }
            break;
        case 8:
            switch (section->kind) {
            case 0xb:
                out->oc9 = 0xb;
                break;
            case 2:
                out->oc9 = 2;
                break;
            case 0xa:
                out->oc9 = 0xa;
                break;
            case 0xc:
#if LINKER >= 27
                EPEC_BssAsData(section);
#else
                CompilerTools_ReportLimitedDiagnostic(0x5c, section->name, out->name);
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
#endif
                break;
            case 9:
                CompilerTools_ReportLimitedDiagnostic(0x8b, section->name, out->name);
                break;
            }
            break;
        case 9:
            if (!(section->header->flags & 0x40000000) || (section->header->flags & 1))
                CompilerTools_ReportLimitedDiagnostic(0x8b, section->name, out->name);
            if (section->kind == 0xc) {
#if LINKER >= 27
                EPEC_BssAsData(section);
#else
                CompilerTools_ReportLimitedDiagnostic(0x5c, section->name, out->name);
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
#endif
            }
            break;
        case 0xa:
            if (section->header->flags & 0x40000000)
                CompilerTools_ReportLimitedDiagnostic(0x8b, section->name, out->name);
            if ((section->header->flags & 4) && !(section->header->flags & 0x40000000))
                out->oc9 = 0xb;
            if (section->kind == 0xc) {
#if LINKER >= 27
                EPEC_BssAsData(section);
#else
                CompilerTools_ReportLimitedDiagnostic(0x5c, section->name, out->name);
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
#endif
            }
            break;
        case 0xc:
            if (section->kind != 0xc) {
#if LINKER >= 27
                CompilerTools_ReportLimitedDiagnostic(0x5c, EPEB_FileName(section->file), out->name, section->name);
#else
                CompilerTools_ReportLimitedDiagnostic(0x5c, out->name, section->name);
#endif
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
            }
            break;
        }
    } else {
        /* a section of the linker's own kinds (oce): one of its name only */
        if ((section->header->flags & 0x40000000) && out->oc9 != 9)
            CompilerTools_ReportLimitedDiagnostic(0x8b, section->name, out->name);
#if LINKER >= 27
        /* (2.7: BSS is written as zeros in a section of data; data in a BSS one is warned about, and a section of
           another name allowed when the command file names it, a standard one) */
        {
        Boolean mixed = 0;

        if (out->o7c == 1 && section->header->type == 8)
            EPEC_BssAsData(section);
        if (out->o7c == 8 && section->header->type == 1) {
            CompilerTools_ReportDiagnostic(0x5c, EPEB_FileName(section->file), out->name, section->name);
            mixed = 1;
        }
        if ((!out->name || !section->name || strcmp(out->name, section->name)) &&
            (mixed || !EPEC_NamedStandard(section))) {
            CompilerTools_ReportLimitedDiagnostic(0x5d, out->name, section->name);
            CompilerTools_FormatMessageAndLongjmp(0xd, 2);
        }
        }
#else
        if (out->o7c == 1 && section->header->type == 8) {
            CompilerTools_ReportLimitedDiagnostic(0x5c, section->name, out->name);
            CompilerTools_FormatMessageAndLongjmp(0xd, 2);
        }
        if (out->o7c == 8 && section->header->type == 1) {
            CompilerTools_ReportLimitedDiagnostic(0x5c, out->name, section->name);
            CompilerTools_FormatMessageAndLongjmp(0xd, 2);
        }
        if (!out->name || !section->name || strcmp(out->name, section->name)) {
            CompilerTools_ReportLimitedDiagnostic(0x5d, out->name, section->name);
            CompilerTools_FormatMessageAndLongjmp(0xd, 2);
        }
#endif
    }

    if (out->type != flags) {
        unsigned long pflags;

        out->type |= flags;
#if LINKER >= 27
        flags = out->type; /* (2.7: the segment's of all the inputs' flags) */
#endif
        pflags = 0;
        if (flags & 2)
            pflags |= 4;
        if (flags & 1)
            pflags |= 2;
        if (flags & 4)
            pflags |= 1;
        out->o70 = pflags;
    }
    out->o58 = 1;
}

/* 0x44bd40 */
void ELF_PPC_EABI_AssignSection(ELObject *object, ELSection *section) {
    ELOutput *out;
    ELOutput *other;
    ELOutputNode *node;
    ELSectionRef *ref;
    ELSectionRef *last;
    ELMemory *memory;
    unsigned long flags;
    unsigned long origin;
    char *kindName;
    char *name;

    if (!section->header->alignment)
        section->header->alignment = 1;
    section->output = NULL;

    if (section->header->type == 4) {
        object->sections[section->header->info].relocations = section;
        if (data_004a2e49)
            section->kind = 0x1a;
        return;
    }
    if (section->header->type == 0xca2a82c2) {
        object->sections[section->header->link].related = section;
        section->kind = 0x13;
        data_004a2e44 = 1;
        return;
    }

    section->kind = ELF_PPC_EABI_SectionKind(section->name);
#if LINKER >= 27
    if (!section->kind && (name = ELF_PPC_EABI_BaseName(section->name)) != NULL)
        section->kind = ELF_PPC_EABI_SectionKind(name);
#endif
    if (!section->kind) {
        flags = section->header->flags;
        if (flags & 0x40000000) {
            section->kind = 9;
            data_004a2e44 = 1;
            data_004a2e59 = 1;
        } else if (flags == 7) {
            section->kind = 0xb;
        } else if (flags & 4) {
            section->kind = 2;
        } else if (flags & 1) {
            section->kind = 0xa;
            if (section->header->type == 8)
                section->kind = 0xc;
        } else if ((flags & 2) && section->header->type == 1) {
            section->kind = 8;
        }
    }

    switch (section->kind) {
    case 0:
        CompilerTools_ReportDiagnostic(0x5e, section->name);
        return;

    case 0x1b:
        if (strncmp(section->data, "CodeWarrior", 0xb))
            return;
        section->output = data_004a2e0e;
        out = section->output;
        if (data_004a2e49)
            out->oa8 = 1;
        section->x20 = (long)out->items;
        ELF_PPC_EABI_CheckComment(section);
        break;

    case 0x16:
        section->output = data_004a2dee;
        return;
    case 0x17:
        section->output = data_004a2df2;
        return;
    case 0x18:
        section->output = data_004a2df6;
        return;

    case 0x14:
        if (section->name && !strcmp(section->name, ".debug"))
            object->x24 = (long)section;
        name = section->name;
        out = NULL;
        for (node = data_004a2de6; node; node = node->next) {
            if (node->output->name && name && !strcmp(node->output->name, name)) {
                out = node->output;
                break;
            }
        }
        section->output = out;
        if (!section->output) {
            node = (ELOutputNode *)galloc(sizeof(ELOutputNode));
            memset(node, 0, sizeof(ELOutputNode));
            node->next = data_004a2de6;
            data_004a2de6 = node;
            out = ELF_PPC_EABI_NewOutput(section->name, 0x14, 2);
            EPEA_AppendSection(out);
            data_004a2de6->output = out;
            section->output = out;
            ELF_PPC_EABI_SetSectionType(out);
            EPEA_AddSelfItem(out);
        }
        if (data_004a2e37)
            out->oa8 = 1;
        section->x20 = (long)out->items;
        break;

    case 0x15:
        section->output = data_004a2dea;
        out = section->output;
        if (data_004a2e37)
            out->oa8 = 1;
        section->x20 = (long)out->items;
        break;

    default:
        out = ELF_PPC_EABI_ChooseOutput(object, section);
        if (!out) {
            for (other = data_004a28e4->sections; other; other = other->next) {
                if (other->name && section->name && !strcmp(other->name, section->name)) {
                    switch (other->type) {
                    case 0:
                        kindName = "NO PERMISSION";
                        break;
                    case 2:
                        kindName = "CONST";
                        break;
                    case 3:
                        kindName = other->bss ? "BSS" : "DATA";
                        break;
                    case 6:
                        kindName = "CODE";
                        break;
                    case 0x40000006:
                        kindName = "ZCODE";
                        break;
                    case 7:
                        kindName = "MIXED";
                        break;
                    default:
                        kindName = "UNKNOWN";
                        break;
                    }
                    CompilerTools_ReportLimitedDiagnostic(0x73, section->name, kindName, section->name, object->fileSymbol->name);
                    CompilerTools_FormatMessageAndLongjmp(0xd, 2);
                }
            }

            if (section->name && !strncmp(section->name, ".abs.", 5)) {
                out = ELF_PPC_EABI_NewOutput(section->name, section->kind, 1);
                ELF_PPC_EABI_SetSectionType(out);
                EPEA_AddSelfItem(out);
                section->x20 = (long)out->items;
                EPEA_AppendSection(out);
                sscanf(section->name + 5, "%x", &origin);
                out->memory = galloc(sizeof(ELMemory));
                memset(out->memory, 0, sizeof(ELMemory));
                memory = (ELMemory *)out->memory;
                memory->name = section->name;
                memory->origin = data_004a2e49 ? 0 : origin;
                memory->length = -1;
                EPEA_AppendMemory(memory);
            } else {
                switch (section->kind) {
                case 0xd:
                case 0xe:
                case 0xf:
                case 0x10:
                case 0x11:
                case 0x12:
                    CompilerTools_ReportLimitedDiagnostic(0x68, section->name);
                    CompilerTools_FormatMessageAndLongjmp(0xd, 2);
                }
                out = ELF_PPC_EABI_NewOutput(section->name, section->kind, 1);
                ELF_PPC_EABI_SetSectionType(out);
                EPEA_AddSelfItem(out);
                section->x20 = (long)out->items;
                EPEA_AppendSection(out);
                if (data_004a2e32) {
                    out->memory = data_004a28e4->memory;
                    memory = (ELMemory *)out->memory;
                    if (!memory || memory->name)
                        EPEA_ASSERT(0x970);
                    CompilerTools_ReportDiagnostic(0x69, section->name, object->fileSymbol->name, section->name);
                    if (section->name && !strcmp(section->name, ".ctors")) {
                        if (data_004a2e1a)
                            EPEA_CHECK(0x974);
                        data_004a2e1a = out;
#if LINKER < 27
                        ELF_PPC_EABI_AddCtorItems();
#endif
                    } else if (section->name && !strcmp(section->name, ".dtors")) {
                        if (data_004a2e1e)
                            EPEA_CHECK(0x979);
                        data_004a2e1e = out;
#if LINKER < 27
                        ELF_PPC_EABI_AddDtorItems();
#endif
                    }
                }
            }
        }
        ELF_PPC_EABI_AddInput(out, section);
        section->output = out;
        if (section->kind == 0x1e)
            out->oa8 = 1;
        break;
    }

    /* The section added to its item's sections (2.7: after those of its priority or a lower one). */
    last = NULL;
#if LINKER >= 27
    for (ref = (ELSectionRef *)((ELCmdItem *)section->x20)->x4; ref; ref = ref->next) {
        if (ELF_PPC_EABI_SectionPriority(section->name) < ELF_PPC_EABI_SectionPriority(ref->section->name))
            break;
        last = ref;
    }
#else
    for (ref = (ELSectionRef *)((ELCmdItem *)section->x20)->x4; ref; ref = ref->next)
        last = ref;
#endif
    ref = (ELSectionRef *)galloc(sizeof(ELSectionRef));
    memset(ref, 0, sizeof(ELSectionRef));
    ref->section = section;
#if LINKER >= 27
    if (!last) {
        ref->next = (ELSectionRef *)((ELCmdItem *)section->x20)->x4;
        ((ELCmdItem *)section->x20)->x4 = ref;
    } else {
        ref->next = last->next;
        last->next = ref;
    }
#else
    if (!last)
        ((ELCmdItem *)section->x20)->x4 = ref;
    else
        last->next = ref;
#endif
    ((ELCmdItem *)section->x20)->found = 1;
}

/* Gives the command file's sections that have none a MEMORY area made from the options (init, code, data, sdata,
   sdata2, sdata0), by their kinds; unless the command file has its own (data_004a2e32). */
/* 0x44b8d0 */
void ELF_PPC_EABI_MakeMemoryAreas(void) {
    ELMemory *init;
    ELMemory *code;
    ELMemory *data;
    ELMemory *small;
    ELMemory *memory;
    ELMemory *last;
    ELOutput *out;
    ELOutput *s;

    if (data_004a2e32)
        return;

    last = NULL;
    if (data_004a2e45 && data_004a28f8 != data_004a2828) {
        out = data_004a28dc[data_004a2a68->file].sections[data_004a2a68->symbol->section].output;
        init = EPEA_NewMemory("init", data_004a28f8, -1);
        if (!data_004a28e4->memory) {
            data_004a28e4->memory = init;
        } else {
            for (last = (ELMemory *)data_004a28e4->memory; last->next; last = last->next)
                ;
            last->next = init;
        }
        last = init;
        out->memory = init;
    }

    code = EPEA_NewMemory("code", data_004a2e49 ? 0 : data_004a2828, -1);
    if (!data_004a28e4->memory) {
        data_004a28e4->memory = code;
    } else {
        for (last = (ELMemory *)data_004a28e4->memory; last->next; last = last->next)
            ;
        last->next = code;
    }
    last = code;

    for (s = data_004a28e4->sections; s; s = s->next)
        if (s->oc9 == 9 && !s->memory)
            s->memory = code;
    for (s = data_004a28e4->sections; s; s = s->next)
        if (s->oc9 == 2 && !s->memory)
            s->memory = code;
    for (s = data_004a28e4->sections; s; s = s->next)
        if (s->oc9 == 8 && !s->memory)
            s->memory = code;
    for (s = data_004a28e4->sections; s; s = s->next)
        if (s->oc9 == 0x1c && !s->memory)
            s->memory = code;
    for (s = data_004a28e4->sections; s; s = s->next)
        if (s->oc9 == 0x1d && !s->memory)
            s->memory = code;
    for (s = data_004a28e4->sections; s; s = s->next)
        if (s->oc9 == 6 && !s->memory)
            s->memory = code;
    for (s = data_004a28e4->sections; s; s = s->next)
        if (s->oc9 == 7 && !s->memory)
            s->memory = code;
    for (s = data_004a28e4->sections; s; s = s->next)
        if (s->oc9 == 0x1e && !s->memory)
            s->memory = code;

    data = code;
    if (data_004a2e52) {
        data = EPEA_NewMemory("data", data_004a2e49 ? 0 : data_004a2214, -1);
        last->next = data;
        last = data;
    }
    for (s = data_004a28e4->sections; s; s = s->next)
        if (s->oc9 == 0xa && !s->memory)
            s->memory = data;
    for (s = data_004a28e4->sections; s; s = s->next)
        if (s->oc9 == 0xb && !s->memory)
            s->memory = data;

    memory = data;
    if (data_004a2e3f) {
        memory = EPEA_NewMemory("sdata", data_004a2e49 ? 0 : data_004a276c, 0xffff);
        last->next = memory;
        last = memory;
    }
    for (s = data_004a28e4->sections; s; s = s->next) {
        if (s->oc9 == 0xd) {
            s->memory = memory;
            ((ELOutput *)s->oac)->memory = memory;
            break;
        }
    }

    if (data_004a2e42) {
        memory = EPEA_NewMemory("sdata2", data_004a2e49 ? 0 : data_004a2824, 0xffff);
        last->next = memory;
        last = memory;
    }
#if LINKER >= 27
    else {
        /* (2.7: with the code, being read only) */
        memory = code;
    }
#endif
    for (s = data_004a28e4->sections; s; s = s->next) {
        if (s->oc9 == 0xf) {
            s->memory = memory;
            ((ELOutput *)s->oac)->memory = memory;
            break;
        }
    }

    if (data_004a2e3f || data_004a2e42)
        memory = data;
#if LINKER >= 27
    memory = data;
#endif
    for (s = data_004a28e4->sections; s; s = s->next)
        if (s->oc9 == 0xc && !s->memory)
            s->memory = memory;

    small = EPEA_NewMemory("sdata0", data_004a2e49 ? 0 : 0xffff8000, 0xffff);
    last->next = small;
    for (s = data_004a28e4->sections; s; s = s->next) {
        if (s->oc9 == 0x11) {
            s->memory = small;
            ((ELOutput *)s->oac)->memory = small;
            break;
        }
    }
}

/* The text of a level of EABI compliance (a .comment's byte 0x15); another level keeps OLD. */
static char *EPEA_ComplianceText(signed char level, char *old) {
    switch (level) {
    case 0:
        return "PPC EABI compliant";
    case 1:
        return "Non-PPC EABI compliant (#pragma incompatible_return_small_structs)";
    case 2:
        return "Non-PPC EABI compliant (#pragma incompatible_sfpe_double_params)";
    case 3:
        return "Non-PPC EABI compliant (#pragma incompatible_return_small_structs and #pragma "
               "incompatible_sfpe_double_params)";
    case 4:
        return "Non-PPC EABI compliant (#pragma unsafe_global_reg_vars)";
    case 5:
        return "Non-PPC EABI compliant (#pragma incompatible_return_small_structs and #pragma "
               "unsafe_global_reg_vars)";
    case 6:
        return "Non-PPC EABI compliant (#pragma incompatible_sfpe_double_params and #pragma "
               "unsafe_global_reg_vars)";
    case 7:
        return "Non-PPC EABI compliant (#pragma incompatible_return_small_structs, #pragma "
               "incompatible_sfpe_double_params and #pragma unsafe_global_reg_vars)";
    default:
        return old;
    }
}

/* Checks the objects' .comment sections for their EABI compliance (each file once), reports the files that differ
   from the first of the least compliant level (0x80), and writes that level into the output's .comment header. */
/* 0x44b540 */
void ELF_PPC_EABI_CheckCompliance(void) {
    char *header;
    ELCmdItem *item;
    ELSectionRef *ref;
    ELSection *section;
    ELFile *file;
    char *fileName;
    char *member;
    char *desc;
    char *firstDesc;
    char *firstFile;
    char *firstMember;
    signed char level;
    signed char previous;
    signed char current;

    header = data_004a2e0a;
    level = 0;
    previous = -1;
    desc = NULL;
    firstDesc = NULL;
    firstFile = NULL;
    firstMember = NULL;

    for (item = data_004a2e0e->items; item; item = item->next) {
        if (!item->section || strcmp(item->section, ".comment"))
            continue;
        for (ref = (ELSectionRef *)item->x4; ref; ref = ref->next) {
            section = ref->section;
            file = data_004a28dc[section->file].file;
            if (file->flags2 & 0x20)
                continue;
            file->flags2 = (file->flags2 & ~0x20) | 0x20;

            if (section->file > -1)
                fileName = data_004a28dc[section->file].file->name;
            else
                fileName = "linker generated symbol";
            if (section->file > -1 && data_004a28dc[section->file].memberName)
                member = data_004a28dc[section->file].fileSymbol->name;
            else
                member = "";

            if ((signed char)section->data[0xb] < 8) {
                desc = "PPC EABI compliant";
                if (level) {
                    CompilerTools_ReportDiagnostic(0x80, firstFile, *firstMember ? " " : "", firstMember, fileName,
                                                   *member ? " " : "", member, firstDesc, desc);
                } else if (previous == -1) {
                    level = 0;
                    previous = level;
                    firstDesc = "PPC EABI compliant";
                    firstFile = fileName;
                    firstMember = member;
                }
            } else {
                current = section->data[0x15];
                if (previous == -1) {
                    level = current;
                    firstDesc = EPEA_ComplianceText(current, firstDesc);
                    firstFile = fileName;
                    firstMember = member;
                }
                previous = current;
                if (level != current) {
                    desc = EPEA_ComplianceText(current, desc);
                    CompilerTools_ReportDiagnostic(0x80, firstFile, *firstMember ? " " : "", firstMember, fileName,
                                                   *member ? " " : "", member, firstDesc, desc);
                }
                if (level < current) {
                    level = current;
                    firstFile = fileName;
                    firstMember = member;
                    firstDesc = EPEA_ComplianceText(current, firstDesc);
                }
            }
        }
    }
    header[0x15] = level;
}
#if LINKER >= 27
#define EPEC_HAS_RELA(s)     ((((s)->kind == 1 || (s)->kind == 2) && ((data_004a2e37 && EPEC_DEBUG_RELOCATIONS) || (s)->kind != 2) && (s)->ob0))

/* Output section S's .rela section next in the headers' list (at COUNT), laid out by its size. */
static long EPEC_LayoutRela(ELOutput *s, long count) {
    ((ELOutput *)s->ob0)->items = s->items;
    ((ELOutput *)s->ob0)->excluded = s->excluded;
    data_004a2a88[count++] = (ELOutput *)s->ob0;
    ((ELOutput *)s->ob0)->o54 = (unsigned long)ELF_gen_SizeRelocations;
    return count;
}
#endif


#if LINKER >= 27
/* Whether 2.7's tables (_rom_copy_info, _bss_init_info) are in .init: they are made and there is one. (Not an
   original function.) */
Boolean ELF_PPC_EABI_TablesInInit(void) {
    return (LinkSym_FindIndex(-6)->made || LinkSym_FindIndex(-7)->made) && EPEC_FindUncompressed(".init");
}
#endif

/* Prepares the output sections for the layout: the compliance check, _ctors and _dtors, the MEMORY areas, the
   buffers of each section (contents, S-records, relocations, records), the linker-defined data's section, the
   .ctors and .dtors tables, and the list data_004a2a88 in layout order with each one's layout callback (o54). */
/* 0x44b000 */
void ELF_PPC_EABI_PrepareLayout(void) {
    ELLinkSymbol *link;
    ELOutput *s;
    ELOutputNode *node;
    long size;
    long count;

    ELF_PPC_EABI_CheckCompliance();
#if LINKER >= 27
    /* (2.7: after the compliance's) */
    EPEC_ReportWarnings();
#endif

#if LINKER < 27
    link = LinkSym_FindIndex(-10);
    if (data_004a2e4f) {
        link->made = 1;
        link->piece->symbol->flags |= 0x10;
    }
    if (link->made) {
        if (!data_004a2e1a)
            EPEA_CHECK(0xad2);
        data_004a2e1a->oa8 = 1;
    }
    link = LinkSym_FindIndex(-11);
    if (data_004a2e4f) {
        link->made = 1;
        link->piece->symbol->flags |= 0x10;
    }
    if (link->made) {
        if (!data_004a2e1e)
            EPEA_CHECK(0xadc);
        data_004a2e1e->oa8 = 1;
    }
#else
    /* (2.7's _eti_init_info is its Linker Generated Symbol File's extabindex piece: that section is written, empty
       or not, when it is kept) */
    if (data_004a2e16 && LinkSym_FindIndex(-12)->made)
        data_004a2e16->oa8 = 1;
    /* (and its .init piece, of _rom_copy_info and _bss_init_info, makes .init code) */
    for (s = data_004a28e4->sections; s; s = s->next) {
        if (s->kind == 1 && !s->oc9 && s->name && !strcmp(s->name, ".init")) {
            s->oc9 = 2;
            ELF_PPC_EABI_SetSectionType(s);
        }
    }
#endif

    ELF_PPC_EABI_MakeMemoryAreas();

    data_004a2a88 = (ELOutput **)galloc((data_004a294c + 1) * 4);
    for (s = data_004a28e4->sections; s; s = s->next) {
        s->o54 = 0;
        if (s->kind == 5)
            continue;
        size = 1000;
        if (s->kind == 6)
            size = 0x14;
        if (s->kind == 1 || s->kind == 6) {
            switch (s->oc9) {
            case 2:
            case 9:
            case 10:
            case 11:
                size = 4000;
                break;
            }
            if (data_004a2e3a && SRecord_InitBuffer((GList *)&s->o44, size))
                CompilerTools_FormatMessageAndLongjmp(0xd, -0x6c);
        }
        if (SRecord_InitBuffer((GList *)&s->o34, size))
            CompilerTools_FormatMessageAndLongjmp(0xd, -0x6c);
        if (data_004a2e44 && s->ob0 && SRecord_InitBuffer((GList *)&((ELOutput *)s->ob0)->o34, 1000))
            CompilerTools_FormatMessageAndLongjmp(0xd, -0x6c);
        if (data_004a2e40 && s->xb8) {
            if (SRecord_InitBuffer((GList *)&s->xb8->o34, 1000))
                CompilerTools_FormatMessageAndLongjmp(0xd, -0x6c);
            if (s->xb8->ob0 && data_004a2e44 && SRecord_InitBuffer((GList *)&((ELOutput *)s->xb8->ob0)->o34, 1000))
                CompilerTools_FormatMessageAndLongjmp(0xd, -0x6c);
        }
    }

    data_004a2dde = ELF_PPC_EABI_EntryOutput();
    if (!data_004a2e1a) {
        data_004a2e1a = data_004a2dde;
#if LINKER < 27
        ELF_PPC_EABI_AddCtorItems();
#endif
    }
    if (!data_004a2e1e) {
        data_004a2e1e = data_004a2dde;
#if LINKER < 27
        ELF_PPC_EABI_AddDtorItems();
#endif
    }
#if LINKER >= 27
    /* (2.7: _rom_copy_info and _bss_init_info are its Linker Generated Symbol File's .init piece) */
    if (ELF_PPC_EABI_TablesInInit())
        data_004a2dde = EPEC_FindUncompressed(".init");
#endif
    ELF_PPC_EABI_FixCtorRelocations(data_004a2e1a);
    ELF_PPC_EABI_FixCtorRelocations(data_004a2e1e);

    count = 0;
    for (s = data_004a28e4->sections; s; s = s->next) {
        if (s->kind == 3 || s->kind == 2 || s->kind == 4)
            continue;
        data_004a2a88[count++] = s;
        switch (s->oc9) {
        case 2:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 0xb:
        case 0xd:
        case 0xf:
        case 0x11:
        case 0x1c:
        case 0x1d:
        case 0x1e:
            s->o54 = (unsigned long)ELF_gen_LayoutSection;
            break;
        case 3:
            EPEA_ASSERT(0xb2b);
            break;
        case 0xc:
        case 0xe:
        case 0x10:
        case 0x12:
            s->o54 = (unsigned long)ELF_gen_LayoutBssSection;
            break;
        default:
            if (s->kind == 6)
                s->o54 = (unsigned long)ELF_gen_LayoutAssignment;
#if LINKER >= 27
            /* (2.7 lays out, and maps, a section with no inputs too) */
            if (s->kind == 1)
                s->o54 = (unsigned long)ELF_gen_LayoutNoInputs;
#endif
            break;
        }
    }
    for (node = data_004a2de6; node; node = node->next) {
        node->output->o54 = (unsigned long)ELF_gen_LayoutDwarf;
        data_004a2a88[count++] = node->output;
    }
    data_004a2dea->o54 = (unsigned long)ELF_gen_LayoutDwarf;
    data_004a2a88[count++] = data_004a2dea;

    if (data_004a2e44) {
#if LINKER >= 27
        /* (2.7: the DWARF sections' after the others', in their headers' order) */
        long n = count;
        long i;

        for (s = data_004a28e4->sections; s; s = s->next) {
            if (s->kind == 1 && EPEC_HAS_RELA(s))
                count = EPEC_LayoutRela(s, count);
        }
        for (i = 0; i < n; i++) {
            if (data_004a2a88[i]->kind == 2 && EPEC_HAS_RELA(data_004a2a88[i]))
                count = EPEC_LayoutRela(data_004a2a88[i], count);
        }
#else
        for (s = data_004a28e4->sections; s; s = s->next) {
            if ((s->kind == 1 || s->kind == 2) && ((data_004a2e37 && EPEC_DEBUG_RELOCATIONS) || s->kind != 2) && s->ob0) {
                ((ELOutput *)s->ob0)->items = s->items;
                ((ELOutput *)s->ob0)->excluded = s->excluded;
                data_004a2a88[count++] = (ELOutput *)s->ob0;
                ((ELOutput *)s->ob0)->o54 = (unsigned long)ELF_gen_SizeRelocations;
            }
        }
#endif
    }
    if (data_004a2e40) {
        for (s = data_004a28e4->sections; s; s = s->next) {
            if (s->kind == 1 && s->xb8) {
                s->xb8->items = s->items;
                s->xb8->excluded = s->excluded;
                data_004a2a88[count++] = s->xb8;
                s->xb8->o54 = (unsigned long)ELF_gen_LayoutRecords;
                if (s->xb8->ob0 && data_004a2e44) {
                    ((ELOutput *)s->xb8->ob0)->items = s->xb8->items;
                    ((ELOutput *)s->xb8->ob0)->excluded = s->xb8->excluded;
                    data_004a2a88[count++] = (ELOutput *)s->xb8->ob0;
                    ((ELOutput *)s->xb8->ob0)->o54 = (unsigned long)ELF_gen_SizeRecordRelocations;
                }
            }
        }
    }

    data_004a2dee->o54 = 0;
    data_004a2a88[count] = data_004a2dee;
    data_004a2df6->o54 = 0;
    data_004a2a88[count + 1] = data_004a2df6;
    data_004a2df2->o54 = 0;
    data_004a2a88[count + 2] = data_004a2df2;
    count += 3;
    if (data_004a2e49) {
        data_004a2e0e->o54 = 0;
        data_004a2a88[count++] = data_004a2e0e;
    }
    if (data_004a2e45) {
        data_004a2dfa->o54 = 0;
        data_004a2a88[count++] = data_004a2dfa;
    }
    if (count > data_004a294c + 1)
        EPEA_CHECK(0xb81);
    data_004a2a88[count] = NULL;
}

/* The output section (not a GROUP or an assignment) whose section index (o2c) is INDEX, all 32 bits compared. */
/* 0x44afc0 */
ELOutput *ELF_PPC_EABI_FindSectionByIndex(unsigned long index) {
    ELOutput *s;
    long i;

    for (i = 0; (s = data_004a2a88[i]); i++) {
        if (s->kind != 5 && s->kind != 6 && s->o2c == index)
            break;
    }
    return s;
}

/* After a section's layout: its MEMORY area's use advanced past it (and the next ROM addresses), or for an empty
   one, its address taken back to where the area was. */
/* 0x44af20 */
void ELF_PPC_EABI_AdvanceMemory(ELOutput *section) {
    ELMemory *memory = EPEA_Memory(section);

    if (memory && section->size) {
        if (!data_004a2e49) {
            memory->used += section->size;
            memory->mark = memory->used;
        }
        if (data_004a2e45) {
            data_004a2dd2 = section->romAddress + section->o68;
            data_004a2dd6 = section->oa0 + section->o68;
        }
    } else if (memory && !section->size && !data_004a2e49) {
        section->address -= memory->used - memory->mark;
        if (!data_004a2e45) {
#if LINKER >= 27
            /* (2.7's ROM address is an aligned one, past the padding its file offset takes) */
            unsigned long align = section->align74 ? section->align74 : 1;

            unsigned long pad = ((data_004a2dda + align - 1) & ~(align - 1)) - data_004a2dda;

            section->romAddress = (section->address + pad + align - 1) & ~(align - 1);
#else
            section->romAddress = section->address;
#endif
            section->oa0 = section->address;
        }
#if LINKER >= 27
        /* (2.7: a ROM image's empty section is at the ROM's next address, not aligned) */
        else
            section->romAddress = data_004a2dd2;
#endif
        memory->used = memory->mark;
    }
}

/* Before a section's layout: its address in its MEMORY area (BIND's, checked against the area, or the area's next
   address), aligned, and its ROM and load addresses. */
/* 0x44ace0 */
void ELF_PPC_EABI_PlaceSection(ELOutput *section) {
    ELMemory *memory;
    unsigned long align;
    unsigned long address;
    char addressText[20];
    char originText[20];
    char lengthText[20];

    if (section->kind != 1 && section->kind != 5)
        return;

    if (section->memory)
        memory = (ELMemory *)section->memory;
    else if (section->parent)
        memory = (ELMemory *)section->parent->memory;
    else
        EPEA_ASSERT(0xbcb);

    if (data_004a2e49) {
        if (!memory)
            EPEA_ASSERT(0xbd3);
        if (memory->origin)
            EPEA_ASSERT(0xbd9);
        if (memory->used)
            EPEA_ASSERT(0xbdb);
    }

    section->address = memory->origin + memory->used;
    section->size = 0;
    section->expr = EvalExpr((ELTerm *)section->expr, section);

    if (section->align > section->align74)
        section->align74 = section->align;
#if LINKER >= 27
    /* (2.7: its header's alignment too) */
    if (section->align > section->align98)
        section->align98 = section->align;
#endif
    align = 1;
    if (section->align74)
        align = section->align74;

    if (section->expr) {
        address = (((ELTerm *)section->expr)->value + align - 1) & ~(align - 1);
        if (section->occ) {
            memory->origin = address;
#if LINKER >= 27
        /* (2.7: nor before the area's next address, an overlap) */
        } else if (address < memory->origin + memory->used ||
#else
        } else if (address < memory->origin ||
#endif
                   (memory->length != -1 && address > memory->origin + memory->length)) {
            sprintf(addressText, "0x%.8x", address);
            sprintf(originText, "0x%.8x", memory->origin);
            sprintf(lengthText, "0x%.8x", memory->length);
            CompilerTools_ReportLimitedDiagnostic(0x60, section->kind == 5 ? "GROUP" : section->name, addressText, originText, lengthText);
            CompilerTools_FormatMessageAndLongjmp(0xd, 2);
        }
        if (data_004a2e49) {
            address = 0;
            memory->origin = 0;
        }
    } else {
        address = memory->origin + memory->used;
#if LINKER >= 27
        /* (2.7: a GROUP's, past the padding its file offset takes first) */
        if (section->kind == 1 && section->parent && !data_004a2e49 && !data_004a2e45)
            address += ((data_004a2dda + align - 1) & ~(align - 1)) - data_004a2dda;
#endif
        address = (address + align - 1) & ~(align - 1);
    }
    section->address = address;

    if (data_004a2e45) {
        section->romAddress = data_004a2dd2;
        section->romAddress = (section->romAddress + align - 1) & ~(align - 1);
        section->oa0 = data_004a2dd6;
        section->oa0 = (section->oa0 + align - 1) & ~(align - 1);
    } else {
        section->romAddress = section->address;
        section->oa0 = section->address;
    }
    memory->used += section->address - (memory->origin + memory->used);
}

/* Makes a section's buffers (its contents', its S-records') the ones written to. */
/* 0x44acc0 */
void ELF_PPC_EABI_SelectBuffers(ELOutput *section) {
    data_004a2a70 = (GList *)&section->o34;
    data_004a28d0 = (GList *)&section->o44;
}

/* Makes the list data_004a2820 of the output sections in file order, with each one's writing callback (o54), and
   maps the exception table index. */
/* 0x44a9d0 */
void ELF_PPC_EABI_PrepareWrite(void) {
    ELOutput *s;
    ELOutputNode *node;
    long count;

    ELF_gen_MapExtabIndex(data_004a2e16);
    data_004a2820 = (ELOutput **)galloc((data_004a294c + 1) * 4);
    for (s = data_004a28e4->sections; s; s = s->next) {
        s->o54 = 0;
        if (s->ob0)
            ((ELOutput *)s->ob0)->o54 = 0;
    }

    count = 0;
    for (s = data_004a28e4->sections; s; s = s->next) {
        if (s->kind == 3 || s->kind == 2 || s->kind == 4)
            continue;
        data_004a2820[count++] = s;
        switch (s->oc9) {
        case 2:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 0xb:
        case 0xd:
        case 0xf:
        case 0x11:
        case 0x1c:
        case 0x1d:
        case 0x1e:
            s->o54 = (unsigned long)ELF_gen_WriteSection;
            break;
        case 3:
            EPEA_ASSERT(0xc39);
            break;
        case 0xc:
        case 0xe:
        case 0x10:
        case 0x12:
            s->o54 = (unsigned long)ELF_gen_AlignSectionInFile;
            break;
        default:
            if (s->kind == 6)
                s->o54 = (unsigned long)ELF_gen_WriteAssignment;
#if LINKER >= 27
            else if (s->kind == 1 && s->oc9 == 0 && data_004a2e49)
                s->o54 = (unsigned long)ELF_gen_PadEmptySection;
#endif
            break;
        }
    }

    data_004a2dea->o54 = (unsigned long)ELF_gen_WriteDebug;
    data_004a2820[count++] = data_004a2dea;
    for (node = data_004a2de6; node; node = node->next) {
        node->output->o54 = (unsigned long)ELF_gen_WriteDebug;
        data_004a2820[count++] = node->output;
    }
    data_004a2df6->o54 = WRITE_CALLBACK(ELF_gen_WriteSectionNames, write_section_names);
    data_004a2820[count] = data_004a2df6;
    data_004a2df2->o54 = WRITE_CALLBACK(ELF_gen_WriteStrings, write_strings);
    data_004a2820[count + 1] = data_004a2df2;
    data_004a2dee->o54 = WRITE_CALLBACK(ELF_gen_WriteSymbols, write_symbols);
    data_004a2820[count + 2] = data_004a2dee;
    count += 3;
#if LINKER < 27
    if (data_004a2e49) {
        data_004a2e0e->o54 = (unsigned long)ELF_gen_WriteComment;
        data_004a2820[count++] = data_004a2e0e;
    }
#endif
#if LINKER < 27
    if (data_004a2e40) {
        for (s = data_004a28e4->sections; s; s = s->next) {
            if (s->kind == 1 && s->xb8) {
                data_004a2820[count++] = s->xb8;
                s->xb8->o54 = (unsigned long)ELF_gen_WriteRecords;
            }
        }
    }
#endif
#if LINKER < 27
    if (data_004a2e45) {
        data_004a2dfa->o54 = WRITE_CALLBACK(ELF_gen_WriteSegInfo, write_seg_info);
        data_004a2820[count++] = data_004a2dfa;
    }
#endif
    if (data_004a2e44) {
#if LINKER >= 27
        /* (2.7: in the headers' order) */
        long i;

        for (i = 0; data_004a2a88[i]; i++) {
            if (data_004a2a88[i]->kind == 3 && data_004a2a88[i]->o54 == 0 && data_004a2a88[i]->oc9 == 0x1a &&
                data_004a2a88[i]->ob4 && ((ELOutput *)data_004a2a88[i]->ob4)->kind != 4) {
                data_004a2820[count++] = data_004a2a88[i];
                data_004a2a88[i]->o54 = (unsigned long)ELF_gen_WriteRelocations;
            }
        }
#else
        for (s = data_004a28e4->sections; s; s = s->next) {
            if ((s->kind == 1 || s->kind == 2) && ((data_004a2e37 && EPEC_DEBUG_RELOCATIONS) || s->kind != 2) && s->ob0) {
                data_004a2820[count++] = (ELOutput *)s->ob0;
                ((ELOutput *)s->ob0)->o54 = (unsigned long)ELF_gen_WriteRelocations;
            }
        }
#endif
    }
#if LINKER >= 27
    /* (2.7: the comments, then the procedure records, after the relocations) */
    if (data_004a2e49) {
        data_004a2e0e->o54 = (unsigned long)ELF_gen_WriteComment;
        data_004a2820[count++] = data_004a2e0e;
    }
    if (data_004a2e40) {
        for (s = data_004a28e4->sections; s; s = s->next) {
            if (s->kind == 1 && s->xb8) {
                data_004a2820[count++] = s->xb8;
                s->xb8->o54 = (unsigned long)ELF_gen_WriteRecords;
            }
        }
    }
#endif
    if (data_004a2e40) {
        for (s = data_004a28e4->sections; s; s = s->next) {
            if (s->kind == 1 && s->xb8 && s->xb8->ob0 && data_004a2e44) {
                data_004a2820[count++] = (ELOutput *)s->xb8->ob0;
                ((ELOutput *)s->xb8->ob0)->o54 = (unsigned long)ELF_gen_WriteRecordRelocations;
            }
        }
    }
#if LINKER >= 27
    /* (2.7: last) */
    if (data_004a2e45) {
        data_004a2dfa->o54 = WRITE_CALLBACK(ELF_gen_WriteSegInfo, write_seg_info);
        data_004a2820[count++] = data_004a2dfa;
    }
#endif
    if (count > data_004a294c + 1)
        EPEA_CHECK(0xc9c);
    data_004a2820[count] = NULL;
}

/* Lays out the linker-defined data symbols (_rom_copy_info and _bss_init_info, sized by the sections they list,
   and the assigned ones of the kind) at the end of SECTION, and makes their tables; returns their total size. */
#if LINKER >= 27
extern ELPiece *ELF_gen_tablesSymbol;
extern void LK_symbol_PrintLayoutLine(unsigned long offset, unsigned long size, unsigned long address, int align,
                                      char *name, char *file, char *member);
#endif
/* 0x44a830 */
long ELF_PPC_EABI_LayoutLinkerSymbols(ELOutput *section) {
    long total;
    long bssCount;
    long romCount;
    long i;
    unsigned long romSize;
    unsigned long bssSize;
    ELOutput *s;
    ELLinkSymbol *link;
    ELSymbol *symbol;

    total = 0;
    bssCount = 0;
    romCount = 0;
    for (i = 0; (s = data_004a2a88[i]); i++) {
        if (!s->oa8 || s->kind != 1)
            continue;
        if (s->o7c == 8) {
            if (!(s->type & 1))
                EPEA_CHECK(0xcb1);
            bssCount++;
        } else if (s->o7c == 1) {
            romCount++;
        }
    }
    romSize = (romCount + 1) * 12;
    data_004a2dfe = (unsigned long *)galloc(romSize);
    memset(data_004a2dfe, 0, romSize);
    bssSize = (bssCount + 1) * 8;
    data_004a2e02 = (unsigned long *)galloc(bssSize);
    memset(data_004a2e02, 0, bssSize);

#if LINKER >= 27
    if (ELF_gen_tablesSymbol)
        ELF_gen_tablesSymbol->x10 = section->size + section->address;
#endif
    for (i = 0; LinkSym_Table[i].name; i++) {
        link = &LinkSym_Table[i];
        if (!link->made)
            continue;
        symbol = link->piece->symbol;
        if ((symbol->info & 0xf) != 1 || !link->x8)
            continue;
        switch (link->index) {
        case -12:
        case -11:
        case -10:
            continue;
        case -7:
            symbol->size = bssSize;
            break;
        case -6:
            symbol->size = romSize;
            break;
        }
        link->piece->x10 = section->size + section->address;
        section->size += symbol->size;
        total += symbol->size;
    }
#if LINKER >= 27
    /* (2.7's map: its Linker Generated Symbol File's .init piece and the tables) */
    if (total && ELF_PPC_EABI_TablesInInit()) {
        LK_symbol_PrintLayoutLine(section->size - total, total, section->size - total + section->address, 1, ".init",
                                  "Linker Generated Symbol File", "");
        for (i = 0; LinkSym_Table[i].name; i++) {
            link = &LinkSym_Table[i];
            if (!link->made || (link->index != -6 && link->index != -7))
                continue;
            LK_symbol_PrintLayoutLine(link->piece->x10 - section->address, link->piece->symbol->size, link->piece->x10, 4,
                                      link->name, "Linker Generated Symbol File", "");
        }
    }
#endif
    for (link = data_004a2934; link; link = link->next) {
        if (!link->made)
            continue;
        symbol = link->piece->symbol;
        if (!link->x8)
            continue;
        link->piece->x10 = section->size + section->address;
        section->size += symbol->size;
        total += symbol->size;
    }
    section->o68 = section->size;
    return total;
}

/* The layout of the section of the linker-defined data: padded to 4, then its symbols. */
/* 0x44a810 */
void ELF_PPC_EABI_LayoutLinkerData(ELOutput *section) {
    section->size += ELF_gen_SkipPadding(4);
    data_0049aaf0 = ELF_PPC_EABI_LayoutLinkerSymbols(section);
}

/* Writes the linker-defined data at the end of its section (and its S-records): _rom_copy_info's table (each
   loaded section's ROM address, address and size), _bss_init_info's (each .bss's address and size), the other
   symbols' values. */
/* 0x44a490 */
void ELF_PPC_EABI_WriteLinkerData(ELOutput *section) {
    long i;
    long j;
    long n;
    ELOutput *s;
    ELLinkSymbol *link;
    ELSymbol *symbol;
    unsigned long first;
    unsigned long value;
    void *data;

    if (!data_0049aaf0)
        return;
    if (section->excluded)
        return;

    section->o8c += ELF_gen_WritePadding(4, data_004a2e3a);
    if (data_004a2e3a)
        SRecord_Start(section->o8c + section->oa0, 3);

    for (i = 0; LinkSym_Table[i].name; i++) {
        link = &LinkSym_Table[i];
        if (!link->made)
            continue;
        symbol = link->piece->symbol;
        if (!link->x8 || (symbol->info & 0xf) != 1)
            continue;
        switch (link->index) {
        case -12:
        case -11:
        case -10:
            continue;
        case -6:
            n = 0;
            for (j = 0; (s = data_004a2a88[j]); j++) {
                if (!s->oa8 || s->kind != 1 || s->o7c != 1)
                    continue;
                first = ELF_PPC_EABI_LinkSymbolValue(((ELLinkSymbol *)s->obc)->index);
                value = ELF_PPC_EABI_LinkSymbolValue(((ELLinkSymbol *)s->oc4)->index) - first;
                if (!value)
                    continue;
                data_004a2dfe[n * 3 + 2] = CTool_EndianConvertWord32(value);
                value = ELF_PPC_EABI_LinkSymbolValue(((ELLinkSymbol *)s->oc0)->index);
                data_004a2dfe[n * 3 + 0] = CTool_EndianConvertWord32(value);
                value = ELF_PPC_EABI_LinkSymbolValue(((ELLinkSymbol *)s->obc)->index);
                data_004a2dfe[n * 3 + 1] = CTool_EndianConvertWord32(value);
                n++;
            }
            data = data_004a2dfe;
            break;
        case -7:
            n = 0;
            for (j = 0; (s = data_004a2a88[j]); j++) {
                if (!s->oa8 || s->kind != 1 || !(s->type & 1) || s->o7c != 8)
                    continue;
                first = ELF_PPC_EABI_LinkSymbolValue(((ELLinkSymbol *)s->obc)->index);
                value = ELF_PPC_EABI_LinkSymbolValue(((ELLinkSymbol *)s->oc4)->index) - first;
                if (!value)
                    continue;
                data_004a2e02[n * 2 + 1] = CTool_EndianConvertWord32(value);
                value = ELF_PPC_EABI_LinkSymbolValue(((ELLinkSymbol *)s->obc)->index);
                data_004a2e02[n * 2 + 0] = CTool_EndianConvertWord32(value);
                n++;
            }
            data = data_004a2e02;
            break;
        default:
            value = ELF_PPC_EABI_LinkSymbolValue(link->index);
            value = CTool_EndianConvertWord32(value);
            data = &value;
            break;
        }
        section->o8c += ELF_gen_Write(data, symbol->size);
        if (data_004a2e3a)
            SRecord_AddBytes((unsigned char *)data, symbol->size);
    }

    for (link = data_004a2934; link; link = link->next) {
        if (!link->made)
            continue;
        symbol = link->piece->symbol;
        if (!link->x8)
            continue;
        value = ELF_PPC_EABI_LinkSymbolValue(link->index);
        value = CTool_EndianConvertWord32(value);
        section->o8c += ELF_gen_Write(&value, symbol->size);
        if (data_004a2e3a)
            SRecord_AddBytes((unsigned char *)&value, symbol->size);
    }
    if (data_004a2e3a)
        SRecord_End();
}

/* Fills the output's .comment section header (0x2c bytes): "CodeWarrior", its version, the EABI and processor
   bytes from the options. */
/* 0x44a3a0 */
void ELF_PPC_EABI_MakeCommentHeader(char *header) {
    unsigned char mode;
    unsigned char fp;
    UInt16 word;

    mode = 1;
    header[0] = 'C';
    header[1] = 'o';
    header[2] = 'd';
    header[3] = 'e';
    header[4] = 'W';
    header[5] = 'a';
    header[6] = 'r';
    header[7] = 'r';
    header[8] = 'i';
    header[9] = 'o';
    header[0xa] = 'r';
#if LINKER >= 27
    /* (2.7's: version 13; +0x16 is the char signedness) */
    header[0xb] = 0xd;
    header[0xc] = 3;
    header[0xd] = 0;
    header[0xe] = 0;
#else
    header[0xb] = 0xa;
    header[0xc] = 2;
    header[0xd] = 4;
#if LINKER == 13
    /* (1.3.2's: version 2.4.0) */
    header[0xe] = 0;
#else
    header[0xe] = 7;
#endif
#endif
    if (data_004a2e4f)
        mode = 3;
    else if (data_004a2e49)
        mode = 2;
    header[0xf] = mode;
    header[0x10] = 0;
    if (!data_0049c41a)
        fp = 0;
    else if (data_0049c41b)
        fp = 1;
    else
        fp = 2;
    header[0x11] = fp;
    word = CTool_EndianConvertWord16(data_0049c418);
    header[0x12] = ((unsigned char *)&word)[0];
    header[0x13] = ((unsigned char *)&word)[1];
    header[0x14] = 0x2c;
    header[0x15] = 0;
    header[0x16] = 0;
    header[0x17] = 0;
    *(long *)(header + 0x18) = 0;
    *(long *)(header + 0x1c) = 0;
    *(long *)(header + 0x20) = 0;
    *(long *)(header + 0x24) = 0;
    *(long *)(header + 0x28) = 0;
}

/* In the .ctors or .dtors output section: the input sections of the .ctors, .dtors and sinit items from objects
   linked whole (FORCEFILES), of kind 3: their relocations made R_PPC_ADDR32 and the words they patch cleared. */
/* 0x44a210 */
void ELF_PPC_EABI_FixCtorRelocations(ELOutput *out) {
    ELCmdItem *item;
    ELSectionRef *ref;
    ELSection *section;
    ELSection *rela;
    ELRelocation *rel;
    unsigned long i;

    for (item = out->items; item; item = item->next) {
        if (item->kind != 1)
            continue;
        if (!(item->section && !strcmp(item->section, ".ctors")) &&
            !(item->section && !strcmp(item->section, ".dtors")) &&
            !(item->section && !strcmp(item->section, "sinit")))
            continue;
        for (ref = (ELSectionRef *)item->x4; ref; ref = ref->next) {
            section = ref->section;
            if (!data_004a28dc[section->file].forced)
                continue;
            if (!section)
                EPEA_ASSERT(0xdaa);
            if (section->output != out)
                EPEA_ASSERT(0xdac);
            if (section->kind != 3)
                continue;
            rela = section->relocations;
            for (i = 0; i < rela->header->size / 12; i++) {
                rel = (ELRelocation *)rela->data + i;
                rel->info = (rel->info & ~0xff) + 1;
                *(long *)(section->data + (rel->offset & ~3)) = 0;
            }
        }
    }
}
