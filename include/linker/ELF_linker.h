/* The linker's types (ELF_linker.c's, which LK_symbol.c, ELF_gen.c and ELF_PPC_EABI.c share), the COMMON
   block's globals and ELF_linker.c's functions. */
#ifndef LINKER_ELF_LINKER_H
#define LINKER_ELF_LINKER_H

#include "compiler/common.h"
#include "compiler/ELF_Endian.h"
#include "driver/CWPluginsPrivate.h"
#include "driver/Files.h"
#include "driver/libimp-eabi-ppc.h"

#pragma options align = mac68k

/* A symbol of an object as the linker keeps it (its .symtab entry, 16 bytes). While the object is read, the
   first word is the ELF st_name; afterwards it is the symbol's piece (for an undefined or dropped one: the
   global symbol). */
typedef struct ELSymbol {
    struct ELPiece *piece;
    unsigned long value;            /* a common symbol's: its alignment */
    unsigned long size;
    unsigned char info;             /* binding << 4 | type */
    unsigned char flags;            /* (st_other) 0x10: kept, 0x20: dropped (a duplicate), 0x40: made for an undefined one */
    unsigned short section;         /* st_shndx */
} ELSymbol;

/* An ELF relocation with an addend (Elf32_Rela). */
typedef struct ELRelocation {
    unsigned long offset;
    unsigned long info;             /* the symbol's index << 8 | the type */
    long addend;
} ELRelocation;

/* A procedure record of a piece (0x12 bytes). */
typedef struct ELRecordNode {
    struct ELRecordNode *next;
    unsigned char kind;             /* 1, 2 or 3 */
    unsigned char x5;
    unsigned long offset;           /* +6: in the record section */
    unsigned long size;             /* +0xa */
    ELSymbol *symbol;               /* +0xe: the symbol its relocation names */
} ELRecordNode;

/* A relocation of a piece, kept with the piece (0xc bytes). */
typedef struct ELRelocNode {
    struct ELRelocNode *next;
    ELRelocation *rel;
    ELRecordNode *record;
} ELRelocNode;

/* A piece of a section (a function, a variable, a unit's lines), which is also what the name hash keeps for a
   global symbol (fn_00451dc0, fn_00451820...): 0x4e bytes. */
typedef struct ELPiece {
    struct ELPiece *hashNext;       /* a global symbol: the next of its hash chain */
    char *name;
    long file;                      /* +8: its object's index in data_004a28dc, negative for a linker-defined one */
    ELSymbol *symbol;               /* +0xc */
    long x10;
    struct ELPiece *owner;          /* +0x14: a piece of no size: the sized one it is in */
    unsigned long offset;           /* +0x18: and its offset there */
    struct ELPiece *next;           /* +0x1c: the section's pieces, by address */
    struct ELPiece *prev;           /* +0x20 */
    long x24;
    struct ELPiece *nextAdded;      /* +0x28: the next symbol LK_symbol_Add made */
    struct ELPiece *related;        /* +0x2c: a piece kept with it (a .debug piece's line piece, a line piece's global) */
    long x30;
    unsigned long stName;           /* +0x34: the ELF symbol's name offset */
    ELRelocNode *relocations;       /* +0x38 */
    ELRelocNode *lastRelocation;    /* +0x3c */
    long align;                     /* +0x40: from .comment: the alignment, -1 for none */
    unsigned char commentFlags;     /* +0x44: from .comment: the flags */
    unsigned char flags;            /* +0x45: 1: no .comment entry, 8: force active, 0x10: ... */
    char x46[0x4a - 0x46];
    struct ELChunk *chunk;          /* +0x4a: the chunk it is in */
} ELPiece;

/* The pieces of a section that dead-stripping keeps or drops together (from one compilation unit; 0x20 bytes). */
typedef struct ELChunk {
    struct ELChunk *next;
    ELPiece *pieces;
    unsigned long size;
    long xc;
    long x10;
    long x14;
    ELRecordNode *records;          /* +0x18: its procedure records */
    char whole;                     /* +0x1c: kept as a whole */
    char done;                      /* +0x1d: all its pieces kept */
    char kept;                      /* +0x1e */
    char x1f;
} ELChunk;

/* An entry of the command file's SECTIONS (LinkerCmdFile.c's LCFSection): an output section, a GROUP or an
   assignment. The words not named yet are oNN, by their offset. */
typedef struct ELOutput {
    struct ELOutput *next;          /* +0x00 */
    struct ELOutput *parent;        /* +0x04: its GROUP */
    char *name;                     /* +0x08 */
    unsigned long align;            /* +0x0c: ALIGN's */
    unsigned long o10;
    unsigned long o14;
    void *expr;                     /* +0x18: BIND's, or an assignment's (an LCFExpr) */
    unsigned long o1c;
    struct ELCmdItem *items;        /* +0x20: its contents */
    void *memory;                   /* +0x24: its MEMORY area (an LCFMemory) */
    unsigned long at;               /* +0x28 */
    unsigned long o2c;
    unsigned long o30;
    unsigned long o34;
    unsigned long o38;
    unsigned long o3c;
    unsigned long o40;
    unsigned long o44;
    unsigned long o48;
    unsigned long o4c;
    unsigned long o50;
    unsigned long o54;
    unsigned long o58;
    unsigned long o5c;
    unsigned long address;          /* +0x60 */
    unsigned long o64;
    unsigned long o68;
    unsigned long size;             /* +0x6c */
    unsigned long o70;
    unsigned long align74;          /* +0x74 */
    unsigned long o78;
    unsigned long o7c;
    long type;                      /* +0x80 */
    unsigned long o84;
    unsigned long o88;
    unsigned long o8c;
    unsigned long o90;
    unsigned long o94;
    unsigned long align98;          /* +0x98 */
    unsigned long o9c;
    unsigned long oa0;
    unsigned long romAddress;       /* +0xa4 */
    unsigned long oa8;
    unsigned long oac;
    unsigned long ob0;
    unsigned long ob4;
    struct ELOutput *xb8;           /* +0xb8 */
    unsigned long obc;
    unsigned long oc0;
    unsigned long oc4;
    unsigned char kind;             /* +0xc8: 1 an output section */
    unsigned char oc9;
    Boolean bss;                    /* +0xca */
    Boolean excluded;               /* +0xcb */
    Boolean occ;                    /* +0xcc */
    unsigned char ocd;
    unsigned char oce;
    unsigned char ocf;
} ELOutput;

/* An input section of an object (0x3a bytes). */
typedef struct ELSection {
    char *name;
    Elf32Section *header;
    char *data;
    struct ELSection *relocations;  /* +0xc: its .rela section */
    struct ELSection *related;      /* +0x10: the section of its procedure records */
    long file;                      /* +0x14: its object's index */
    long index;                     /* +0x18: its index in the object */
    ELOutput *output;               /* +0x1c: the output section it goes to */
    long x20;
    ELPiece *pieces;                /* +0x24: its pieces, by address (the one past the last section: the commons) */
    ELPiece *last;                  /* +0x28: (while they are read: where the last one went) */
    ELChunk *chunks;                /* +0x2c */
    long x30;
    unsigned char kind;             /* +0x34: 0x14 .debug, 0x15 .line, 0x1b .comment... */
    char kept;                      /* +0x35 */
    char whole;                     /* +0x36: a chunk is kept as soon as its section is */
    char split;                     /* +0x37: cut into pieces already */
    char x38;
    char x39;
} ELSection;

/* A part of a project file: an object, an archive, an archive member (PrepDumpELF.c's PrepDumpFile). */
typedef struct ELMember {
    struct ELMember *next;
    unsigned char kind;             /* 1 an archive, 2 an object of it needed, 3 its long names, 6 a container */
    unsigned char x5;
    char **data;                    /* +6: the bytes */
    char *name;                     /* +0xa */
} ELMember;

/* A file of the project (0xbc bytes, filled by fn_00420070). */
typedef struct ELFile {
    struct ELFile *next;
    char *name;                     /* +4: the name the link map shows */
    char *objectName;               /* +8: the name the command file's patterns match */
    char xc[0x14 - 0xc];
    ELMember *member;               /* +0x14 */
    long dataId;                    /* +0x18: the IDE's data of it (fn_0042f570's id) */
    char x1c[0xae - 0x1c];
    unsigned char flags;            /* +0xae: EL_FILE_... */
    unsigned char flags2;           /* +0xaf: EL_FILE2_... */
    long index;                     /* +0xb0: its index in the project */
    struct ELFile *original;        /* +0xb4: (EL_FILE_DUPLICATE) the file whose data it shares */
    char xb8[0xbc - 0xb8];
} ELFile;

/* An object of the link (0x34 bytes, Archive.c's ArchiveFile). */
typedef struct ELObject {
    long longName;
    struct Elf32Header *image;      /* +4 */
    ELFile *file;                   /* +8 */
    ELPiece *fileSymbol;            /* +0xc: its name (later its STT_FILE symbol), whose name the map shows */
    unsigned long size;             /* +0x10 */
    char *memberName;               /* +0x14: an archive member's name, as its header has it (nonzero: a member) */
    ELSection *symbols;             /* +0x18: .symtab */
    ELSection *sectionNames;        /* +0x1c: the section name strings */
    ELSection *strings;             /* +0x20: .strtab */
    long x24;
    ELSection *sections;            /* +0x28: one per section header, and one more for the commons */
    char *comment;                  /* +0x2c: the .comment entries, 8 bytes per symbol */
    char forced;                    /* +0x30: FORCEFILES (used) */
    char commentVersion;            /* +0x31: 0, 2 or 3 */
    char hasComment;                /* +0x32 */
    char newDwarf;                  /* +0x33: a comment version from 11 */
} ELObject;

/* The procedure records (see DumpELF.c). */
typedef struct ELRecord1 {
    unsigned char type;
    unsigned char x1;
    unsigned short x2;
    unsigned long size;
    unsigned long address;
    unsigned long exitCount;
    unsigned long nonInstrumentableCount;
} ELRecord1;

typedef struct ELRecord2 {
    unsigned char type;
    unsigned char exitCount;
    unsigned short size;
    unsigned long address;
} ELRecord2;

typedef struct ELRecord3 {
    unsigned char type;
    unsigned char x1;
    unsigned short exitCount;
    unsigned long size;
    unsigned long address;
} ELRecord3;

/* A root of the dead stripping (0x2c bytes): a force-active symbol, an exception table entry, a constructor... */
typedef struct ELRoot {
    struct ELRoot *next;
    ELSymbol *symbol;
    ELSymbol *target;               /* +8: an exception table entry: its function */
    long file;                      /* +0xc */
    ELRelocation *rel0;             /* +0x10: an exception table entry: the relocation of its function */
    ELRelocation *rel8;             /* +0x14: an exception table entry: the relocation at +8 */
    long x18;
    char *entry;                    /* +0x1c: an exception table entry's bytes */
    ELChunk *chunk;                 /* +0x20: a constructor's or destructor's chunk */
    ELSection *section;             /* +0x24: its section */
    void *key;                      /* +0x28: an exception table entry: its function's output section */
} ELRoot;

/* The exception table entries of one output section (0x20 bytes). */
typedef struct ELGroup {
    struct ELGroup *next;
    long count;
    void *key;
    ELRoot **roots;
    char x10[0x10];
} ELGroup;

/* A linker expression term (LinkSym.c's LinkExpr). */
typedef struct ELExpr {
    struct ELExpr *next;
    char *name;                     /* kind 4: a symbol's name */
    unsigned char kind;
} ELExpr;

/* A linker-defined symbol (LinkSym.c's LinkSymbol, 0x1a bytes). */
typedef struct ELLinkSymbol {
    char *name;
    long index;
    Boolean x8;
    Boolean made;                   /* +9: (here: already kept) */
    struct ELLinkSymbol *next;
    ELExpr *expr;                   /* +0xe */
    void *section;
    ELPiece *piece;                 /* +0x16: its symbol of the link */
} ELLinkSymbol;

/* A term of a command file expression as EvalExpr leaves it (LinkerCmdFile.c's LCFExpr): kind 3 a number. */
typedef struct ELTerm {
    struct ELTerm *next;
    unsigned long value;
    unsigned char kind;
} ELTerm;

#pragma options align = reset

/* A MEMORY area (LinkerCmdFile.c's LCFMemory, 0x18 bytes). */
typedef struct ELMemory {
    struct ELMemory *next;
    char *name;
    unsigned long origin;
    long length;                    /* -1: no limit */
    unsigned long used;             /* +0x10: what the sections placed so far take */
    unsigned long mark;             /* +0x14: used, as it was after the last section of some size */
} ELMemory;

/* An input section an item of an output section's contents matched (ELCmdItem's x4 list). */
typedef struct ELSectionRef {
    struct ELSectionRef *next;
    ELSection *section;
} ELSectionRef;

/* A node of a list of output sections (data_004a2de6: the unloaded ones the memory map lists). */
typedef struct ELOutputNode {
    struct ELOutputNode *next;
    ELOutput *output;
} ELOutputNode;

#pragma pack(push, 2)

/* A file pattern of the command file's FORCEFILES, EXCLUDEFILES or INCLUDEDWARF (LCFFileName, 0x16 bytes). */
typedef struct ELFileName {
    struct ELFileName *next;
    char *name;
    char *member;
    ELFile *match;                  /* +0xc: the file it names */
    long index;                     /* +0x10: its object's index + 1 */
    unsigned char found;            /* +0x14 */
    char x15;
} ELFileName;

/* An item of an output section's contents (LCFItem). */
typedef struct ELCmdItem {
    struct ELCmdItem *next;
    void *x4;
    char *file;
    char *member;
    char *section;
    void *expr;
    unsigned char kind;             /* 1: an input file */
    long type;
    unsigned char bss;
    unsigned char found;            /* +0x1f */
} ELCmdItem;

/* A FORCEACTIVE name. */
typedef struct ELName {
    struct ELName *next;
    char *name;
} ELName;

/* The command file (LinkerCmdFile.c's LCFFile). */
typedef struct ELCmdFile {
    void *assignments;
    ELOutput *sections;
    void *memory;
    ELName *forceActive;            /* +0xc */
    ELFileName *forceFiles;         /* +0x10 */
    ELFileName *excludeFiles;       /* +0x14 */
    ELFileName *includeDwarf;       /* +0x18 */
    Boolean exclude;
} ELCmdFile;

#pragma pack(pop)

/* An entry of SECTIONS, as ELF_linker.c names it. */
typedef ELOutput ELCmdSection;

/* An overlay's entry point. */
typedef struct ELOverlay {
    long x0;
    struct ELOverlay *next;
    ELPiece *entry;
} ELOverlay;

typedef char ELSymbolSizeCheck[sizeof(ELSymbol) == 0x10 ? 1 : -1];
typedef char ELOutputSizeCheck[sizeof(ELOutput) == 0xd0 ? 1 : -1];
typedef char ELPieceSizeCheck[sizeof(ELPiece) == 0x4e ? 1 : -1];
typedef char ELChunkSizeCheck[sizeof(ELChunk) == 0x20 ? 1 : -1];
typedef char ELSectionSizeCheck[sizeof(ELSection) == 0x3a ? 1 : -1];
typedef char ELObjectSizeCheck[sizeof(ELObject) == 0x34 ? 1 : -1];
typedef char ELFileSizeCheck[sizeof(ELFile) == 0xbc ? 1 : -1];
typedef char ELRootSizeCheck[sizeof(ELRoot) == 0x2c ? 1 : -1];
typedef char ELLinkSymbolSizeCheck[sizeof(ELLinkSymbol) == 0x1a ? 1 : -1];
typedef char ELFileNameSizeCheck[sizeof(ELFileName) == 0x16 ? 1 : -1];
typedef char ELRecordNodeSizeCheck[sizeof(ELRecordNode) == 0x12 ? 1 : -1];

enum {
    EL_FILE_BINARY_KEPT = 0x01,     /* (a .BINARY file's section is kept whole; also: used) */
    EL_FILE_WHOLE = 0x02,           /* an object linked whole */
    EL_FILE_ARCHIVE = 0x04,         /* (also: a member of an archive; a duplicate is replaced) */
    EL_FILE_DUPLICATE = 0x08,       /* the data is another file's (original) */
    EL_FILE_BINARY = 0x20,          /* a file linked as its bytes (.BINARY) */
    EL_FILE_DROPPED = 0x40
};
enum {
    EL_FILE2_OBJECT = 0x04,         /* an object linked when it is needed */
    EL_FILE2_EXCLUDED = 0x08,       /* named by EXCLUDEFILES: not an object to link */
    EL_FILE2_DWARF = 0x10           /* named by INCLUDEDWARF */
};

/* The globals (all in the COMMON block or other units' data: ELF_linker.c has none of its own). */
extern unsigned char copts[];
extern ELFile *data_004a2764;           /* the project's files to link */
extern ELObject *data_004a28dc;         /* the objects */
extern long data_004a292c;              /* their count */
extern ELRoot *data_004a28e0;           /* the force-active symbols */
extern ELCmdFile *data_004a28e4;        /* the command file */
extern ELLinkSymbol *data_004a2934;     /* the linker-defined symbols after the predefined ones */
extern ELLinkSymbol LinkSym_Table[];    /* the predefined ones */
extern ELOverlay *data_004a2200;        /* the overlays */
extern ELRoot *data_004a2948;           /* the exception table entries */
extern ELGroup *data_004a2954;          /* the exception table entries by output section */
extern long data_004a28d4;              /* the number of exception table entries */
extern long data_004a28f4;              /* the errors */
extern long data_004a2904;
extern long data_004a2a8c;              /* the constructors kept */
extern long data_004a2878;              /* the destructors kept */
extern long data_004a2944;              /* the exception table entries kept */
extern Boolean data_004a2e32;           /* a command file was read: its missing files are reported, and no default
                                           sections or memory areas are made */
extern Boolean data_004a2e39;
extern Boolean data_004a2e3a;           /* an S-record file is written */
extern Boolean data_004a2e40;           /* the procedure records are read */
extern Boolean data_004a2e49;
extern Boolean data_004a2e4e;           /* the objects' byte order is swapped */
extern Boolean data_004a2e4f;           /* EXCLUDEFILES applies */
extern Boolean data_004a2e53;
extern Boolean data_004a2e5a;
extern Boolean data_0049c116;           /* a link map is written */
extern char data_0049c161[];            /* the entry point's name */
extern CWFileSpec data_0049bed0;        /* the output file */
extern char data_0049bed6[];            /* its name */
extern CWFileSpec data_0049bf16;        /* the S-record file */
extern ELPiece *data_0049e1c0[0x1000]; /* the symbols' name hash (LK_symbol.c's) */
extern GList data_0049dbe8;             /* -library's long names */
extern struct Elf32Header data_004a289c; /* the output's ELF header */
extern ELPiece *data_004a2a68;          /* the entry point's symbol */
extern short data_004a2dce;
extern short data_004a2dcc;
extern short data_004a2e34;
extern long data_00588228;              /* 0x4a2a64 */
extern long data_004a2a74;
extern long data_004a2868;
extern long data_004a2204;
extern unsigned long data_004a2a84;     /* the memory the files take */
extern long data_004a2840;
extern void *data_004a2860;             /* the allocator's hook when memory runs out */
extern SInt32 limited_diagnostic_count;

/* The user's break checked (the IDE asked to stop). */
#define EL_CHECK_BREAK() \
    if (fn_00420350()) \
    CompilerTools_Abort()

/* ELF_linker.c's functions, by address. */
int __stdcall ELF_linker_CallCallback(CWPluginPrivateContext *context, long a1, long a2); /* 0x42f600 */
void ELF_linker_ReadProcedureRecords(void);                       /* 0x42f640 */
Boolean ELF_linker_MatchFileLists(ELFile *file);                  /* 0x42f940 */
void ELF_linker_CheckIncludeDwarf(void);                          /* 0x42f9f0 */
void ELF_linker_ExcludeFiles(void);                               /* 0x42fad0 */
Boolean ELF_linker_IsLinkableObject(struct Elf32Header *header);  /* 0x42fda0 */
void ELF_linker_SwapRelocations(long index);                      /* 0x42fe70 */
void ELF_linker_ReadObjectOptions(long index);                    /* 0x42ff10 */
int ELF_linker_Link(void);                                        /* 0x430130 */
void ELF_linker_MakeCommonSections(void);                         /* 0x430450 */
void ELF_linker_LinkDebugPieces(void);                            /* 0x4305c0 */
void ELF_linker_KeepDebugSections(ELSection *section, long file); /* 0x4309e0 */
void ELF_linker_DeadStrip(ELPiece *entry);                        /* 0x430c60 */
void ELF_linker_KeepLinkSymbol(long index, char *referrer);       /* 0x431200 */
void ELF_linker_KeepForceActive(void);                            /* 0x431340 */
ELRoot *ELF_linker_FileRoots(void);                               /* 0x431430 */
ELRoot *ELF_linker_DtorRoots(void);                               /* 0x431630 */
ELRoot *ELF_linker_CtorRoots(void);                               /* 0x4317b0 */
ELRoot *ELF_linker_ExceptionRoots(void);                          /* 0x431960 */
void ELF_linker_KeepSymbol(long file, ELSymbol *symbol, long depth); /* 0x431d20 */
void ELF_linker_MapDuplicates(char *name, long file, ELSymbol *symbol, long depth); /* 0x4323a0 */
void ELF_linker_ResolveDuplicate(ELPiece *piece);                 /* 0x4325a0 */
void ELF_linker_SplitSection(ELSection *section, Boolean unused); /* 0x432710 */
void ELF_linker_SwapSymbols(ELPiece *a, ELPiece *b);              /* 0x432ce0 */
void ELF_linker_LoadObjects(Boolean library);                     /* 0x432d70 */
void ELF_linker_ReadFiles(Boolean library);                       /* 0x434510 */
Boolean ELF_linker_IsELF(char *image);                            /* 0x434880 */
void ELF_linker_End(Boolean succeeded, int status);               /* 0x4348c0 */
void ELF_linker_NoMemory(void);                                   /* 0x434940 */
int ELF_linker_Library(void);                                     /* 0x434950 */
void ELF_linker_OutOfMemory(void);                                /* 0x434a50 */

/* LK_symbol.c's: the symbols' names and their name hash, and the link map file. */
char *LK_symbol_Name(long file, ELSymbol *symbol);                /* 0x451570 */
char *LK_symbol_ObjectName(long file, ELSymbol *symbol);          /* 0x451640 */
unsigned long LK_symbol_Alignment(long file, ELSymbol *symbol);   /* 0x4516e0 */
ELPiece *LK_symbol_Find(char *name, long file);                   /* 0x451820 */
ELPiece *LK_symbol_FindAny(char *name);                           /* 0x4519d0 */
ELPiece *LK_symbol_FindGlobal(char *name, long file);             /* 0x451b00 */
ELPiece *LK_symbol_FindInFile(char *name, long file);             /* 0x451c60 */
ELPiece *LK_symbol_Add(char *name);                               /* 0x451dc0 */
#if LINKER >= 27
void LK_symbol_MoveAfter(ELPiece *after);
ELPiece *LK_symbol_Last(void);
void LK_symbol_MoveToEnd(ELPiece *before, ELPiece *last);
#endif
ELPiece *LK_symbol_First(void);                                   /* 0x451f20 */
void LK_symbol_ClearTable(ELPiece **table);                       /* 0x451f30 */
void LK_symbol_FlushMap(void);                                    /* 0x451f40 */
void LK_symbol_CloseMap(void);                                    /* 0x451f70 */
void LK_symbol_OpenMap(void);                                     /* 0x451fa0 */
void LK_symbol_Print(const char *format, ...);                    /* 0x452040 */
void LK_symbol_PrintLayoutLine(unsigned long offset, unsigned long size, unsigned long address, int align,
                               char *name, char *file, char *member); /* 0x4520d0 */
void LK_symbol_PrintLayoutHeader(char *section);                  /* 0x452110 */
#if LINKER >= 27
/* 2.7's link map: a layout line's file offset (the section's, LK_symbol_layoutFile, plus the line's offset; or
   LK_symbol_layoutFile itself while LK_symbol_layoutFixed), and the padding an alignment adds (*fill*). */
extern unsigned long LK_symbol_layoutFile;
extern Boolean LK_symbol_layoutFixed;
void LK_symbol_PrintLayoutFill(unsigned long offset, unsigned long size, unsigned long address, int align);
void LK_symbol_PrintChunkFill(unsigned long offset, unsigned long size, unsigned long address, int align);

/* 2.7's input section priorities (NAME$NN), and its "Linker Generated Symbol File" (ELF_linker.c: the object of
   _ctors, _dtors and their terminators; its index, -1 when there is none). */
int ELF_PPC_EABI_SectionPriority(char *name);
char *ELF_PPC_EABI_BaseName(char *name);
extern long ELF_linker_generatedFile;
/* 2.7: each object's archive member name as read (NULL for none), which its map lines show */
extern char **ELF_linker_memberNames;
#endif

#endif
