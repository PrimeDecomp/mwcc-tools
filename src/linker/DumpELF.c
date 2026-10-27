/* -dis of one ELF image (DumpELF_Dump): its header, program and section headers, string tables, symbols, segment
   information, relocations, exception tables, code (DumpCode), data in hex, the linker's records, line numbers and
   debug information (DumpDWARF), the compiler's comment, notes and the dynamic section, then whatever is left in
   hex. An image of the other byte order is converted for the dump (and, for the kinds -dis does not give, put back
   after it).

   The assertions name this file "DumpELF.c". */
#include "compiler/common.h"
#include "compiler/CompilerTools.h"
#include "compiler/ELF_Endian.h"
#include "driver/libimp-eabi-ppc.h"
#include <stdio.h>

extern int sprintf(char *s, const char *format, ...);
extern unsigned long strlen(const char *s);
extern int strcmp(const char *a, const char *b);
extern int strncmp(const char *a, const char *b, unsigned long n);
extern void *memset(void *dst, int c, unsigned long n);
extern void qsort(void *base, unsigned long count, unsigned long size, int (*compare)(const void *, const void *));
extern unsigned char __ctype_map[];
extern unsigned char copts[];

typedef void (*DumpAsserter)(const char *message, int value);
typedef int (*DumpCompare)(const void *a, const void *b);
typedef void (*DumpPrinter)(char *text, long length, long size);

extern void ELF_Endian_ConvertWords(char *data, unsigned int size);
extern void ELF_Endian_ConvertTwoHalvesTwoWords(void *data);
extern void ELF_Endian_ConvertTwoWords(unsigned int *words);
extern void DumpDWARF(char *data, unsigned long dataSize, char print, char convert, DumpPrinter output,
                      DumpAsserter assertion, char otherOrder, long name);

/* what -show asks for (the driver's options) */
extern char data_004a2d6e;  /* code */
extern char data_004a2d72;  /* data */
extern char data_004a2d73;  /* exception tables */
extern char data_004a2d74;  /* debug information */
extern char data_004a2dc2;  /* everything in hex (detail) */
extern char data_004a2dc3;  /* debug information relocated */
extern char data_004a2dc4;  /* headers */
extern char data_004a2dc5;  /* symbols and string tables */

#pragma options align = mac68k
/* An ELF file header, as DumpELF_Header reads it. */
typedef struct DumpHeader {
    unsigned char ident[16];
    unsigned short type;
    unsigned short machine;
    unsigned long version;
    unsigned long entry;
    unsigned long phoff;
    unsigned long shoff;
    unsigned long flags;
    unsigned short ehsize;
    unsigned short phentsize;
    unsigned short phnum;
    unsigned short shentsize;
    unsigned short shnum;
    unsigned short shstrndx;
} DumpHeader;

typedef struct DumpProgramHeader {
    unsigned long type;
    unsigned long offset;
    unsigned long vaddr;
    unsigned long paddr;
    unsigned long filesz;
    unsigned long memsz;
    unsigned long flags;
    unsigned long align;
} DumpProgramHeader;

/* A program header in the dump's list. */
typedef struct DumpProgram {
    struct DumpProgram *next;
    long index;
    DumpProgramHeader *header;
    long xc;
} DumpProgram;

typedef struct DumpSymbol {
    unsigned long name;
    unsigned long value;
    unsigned long size;
    unsigned char info;
    unsigned char other;
    unsigned short sectionIndex;
} DumpSymbol;

/* A section being dumped (0x1a bytes). */
typedef struct DumpSection {
    struct DumpSection *next;
    unsigned long index;
    char *name;
    Elf32Section *header;
    char *data;
    struct DumpSection *link;  /* a symbol table's strings; a relocated section's relocations */
    char done;                 /* dumped, or not to be dumped in hex at the end */
    char x19;
} DumpSection;

/* A symbol in the list sorted by address. */
typedef struct DumpSortedSymbol {
    unsigned long value;
    unsigned long size;
    unsigned long index;
} DumpSortedSymbol;

/* The image being dumped (0x30 bytes). */
typedef struct DumpFile {
    char *image;
    long firstGlobal;
    long symbolCount;
    Elf32Header *header;
    DumpSection *sections;
    DumpProgram *programs;
    DumpSection *symbols;  /* .symtab, its link .strtab */
    DumpSection *names;    /* the section names */
    DumpSortedSymbol *sorted;
    DumpSection *dynamicSymbols;
    long firstDynamicGlobal;
    long dynamicSymbolCount;
} DumpFile;
typedef struct DumpRel {
    unsigned long offset;
    unsigned long info;
} DumpRel;

typedef struct DumpRela {
    unsigned long offset;
    unsigned long info;
    long addend;
} DumpRela;

typedef struct DumpRomCopyEntry {
    unsigned short index;
    unsigned short isRomCopy;
    long name;
    long ramIndex;
} DumpRomCopyEntry;

#pragma options align = reset

static char *data_0049a6e0;        /* the name of the file dumped */
static char data_0049a6e4;         /* the dump's kind (1 to 5) */
static char data_0049a6e5;         /* a printer was given: the dump is printed */
/* what is converted to the host's byte order (bit 1) and back (bit 2), when the file's is not the host's */
static char data_0049a6e6;         /* the linker's records */
static char data_0049a6e7;         /* exception tables */
static char data_0049a6e8;         /* line numbers and debug information (2: DWARF of the other order) */
static char data_0049a6e9;         /* notes and the dynamic section */
static char data_0049a6ea;         /* relocations and segment information */
static char data_0049a6eb;         /* section headers */
static char data_0049a6ec;         /* program headers */
static char data_0049a6ed;         /* the file header */
static char data_0049a6ee;         /* the file's byte order is not the host's */
static DumpPrinter data_0049a6f0;  /* where the text goes */
static DumpAsserter data_0049a6f4;

/* Prints a line: `length = sprintf(buffer, ...); data_0049a6f0(buffer, length, size)`. */
#define DUMP_PRINT(buffer, args) data_0049a6f0(buffer, sprintf args, sizeof(buffer))
#define DUMP_ASSERT(line) data_0049a6f4("DumpELF.c", line)

extern void ELF_Endian_SwapElf32ProgramHeader(unsigned int *words);
extern void PrepDumpELF_BadFile(int problem);

struct DumpSection;
struct DumpFile;
/* DumpELFCodePPC.c's: its printer and options; a code section disassembled; a section's relocations applied to it */
extern void DumpCode_Setup(DumpPrinter output);
extern void DumpCode_Section(struct DumpFile *file, struct DumpSection *section);
extern void DumpCode_Relocate(struct DumpFile *file, struct DumpSection *section, char convert);

void DumpELF_PrintDefault(char *text, long length, long size);
int DumpELF_CompareAddressPairs(const unsigned long *a, const unsigned long *b);
int DumpELF_CompareWords(const unsigned long *a, const unsigned long *b);
void DumpELF_Dynamic(struct DumpSection *section);
void DumpELF_Records(struct DumpSection *section);

/* The name of symbol `symbol` of a symbol table: a section symbol without a name has its section's. */
/* 0x43aa10 */
char *DumpELF_SymbolName(void *file, void *symtab, unsigned long symbol) {
    DumpFile *f = file;
    DumpSection *table = symtab;
    DumpSymbol *sym;
    char *name;

    if ((long)symbol > f->symbolCount)
        return "***ERROR***";
    sym = (DumpSymbol *)table->data + symbol;
    name = table->link->data + sym->name;
    if (!*name && (sym->info & 0xf) == 3) {
        unsigned short index = sym->sectionIndex;

        if (index == 0)
            name = "UND";
        else if (index == 0xfff1)
            name = "ABS";
        else if (index == 0xfff2)
            name = "COM";
        else if (index > f->header->section_header_count)
            name = "Unknown";
        else
            name = f->names->data +
                ((Elf32Section *)(f->image + f->header->section_header_offset))[index].name;
    }
    return name;
}

/* Dumps a string table: each string on its line, with its offset. */
/* 0x43a950 */
void DumpELF_StringTable(char *name, char *data, long size) {
    char buffer[0xc00];
    long i;

    DUMP_PRINT(buffer, (buffer, "\r\n==> %s", name));
    for (i = 0; i < size - 1; i++) {
        if (data[i] == 0)
            DUMP_PRINT(buffer, (buffer, "\r\n    [% 3i]\t", i + 1));
        else
            DUMP_PRINT(buffer, (buffer, "%c", data[i]));
    }
    data_0049a6f0("\r\n", 2, 2);
}

/* Converts a symbol table's symbols to the host's byte order, and dumps them with -show tables. */
/* 0x43a330 */
void DumpELF_SymbolTable(DumpFile *file, DumpSection *symtab) {
    char buffer[0xc00];
    DumpSymbol *sym;
    unsigned long i;
    char *bindName;
    char *typeName;
    char *sectionName;
    char *name;
    int bind;
    int type;
    unsigned short index;

    sym = (DumpSymbol *)symtab->data;
    if (!(data_0049a6e5 && data_004a2dc5)) {
        for (i = 0; i < symtab->header->size >> 4; i++, sym++) {
            if (data_0049a6ee && data_0049a6ea)
                ELF_Endian_ConvertSymbolRecord(sym);
        }
        return;
    }

    DUMP_PRINT(buffer, (buffer, "\r\n==> %s\r\n", symtab->name));
    if (data_004a2dc2)
        DUMP_PRINT(buffer, (buffer, "    entry  spndx   value     size   bind       type    other    shndx             name\r\n"));
    else
        DUMP_PRINT(buffer, (buffer, "    entry  spndx   value      size   bind   type  other    shndx         name\r\n"));
    for (i = 0; i < symtab->header->size >> 4; i++, sym++) {
        if (data_0049a6ee && data_0049a6ea)
            ELF_Endian_ConvertSymbolRecord(sym);
        bind = sym->info >> 4;
        switch (bind) {
        case 0: bindName = "LOC"; break;
        case 1: bindName = "GLOB"; break;
        case 2: bindName = "WEAK"; break;
        default: bindName = "<unknown>"; break;
        }
        type = sym->info & 0xf;
        switch (type) {
        case 0: typeName = "NULL"; break;
        case 1: typeName = "OBJ"; break;
        case 2: typeName = "FUNC"; break;
        case 3: typeName = "SECT"; break;
        case 4: typeName = "FILE"; break;
        default: typeName = "<unknown>"; break;
        }
        index = sym->sectionIndex;
        if (index == 0)
            sectionName = "UND";
        else if (index == 0xfff1)
            sectionName = "ABS";
        else if (index == 0xfff2)
            sectionName = "COM";
        else if (index > file->header->section_header_count)
            sectionName = "Unknown";
        else
            sectionName = file->names->data +
                ((Elf32Section *)(file->image + file->header->section_header_offset))[index].name;
        name = DumpELF_SymbolName(file, symtab, i);
        if (data_004a2dc2)
            DUMP_PRINT(buffer, (buffer,
                "    [% 4i]  % 4i   %08x  %4i   %-4s(0%x)   %-4s(0%x)   0%x    (%04x)%-10s  %-s\r\n",
                i, sym->name, sym->value, sym->size, bindName, bind, typeName, type, sym->other, sym->sectionIndex,
                sectionName, name));
        else
            DUMP_PRINT(buffer, (buffer, "    [% 4i]  % 4i   %08x % 6i   %-4s   %-4s     %2x    %-13s %-s\r\n",
                i, sym->name, sym->value, sym->size, bindName, typeName, sym->other, sectionName, name));
    }
}

/* Dumps a relocation with an addend. */
/* 0x43a220 */
void DumpELF_RelaEntry(DumpFile *file, DumpSection *symtab, DumpRela *r, int index) {
    char buffer[0xc00];
    unsigned long symbol = r->info >> 8;
    char *name = DumpELF_SymbolName(file, symtab, symbol);

    DUMP_PRINT(buffer, (buffer, "    [% 4i] %08x %08x %4i %-s(%i)\r\n", index, r->offset, r->addend, r->info & 0xff,
        name, symbol));
}

/* Dumps a relocation without an addend. */
/* 0x43a110 */
void DumpELF_RelEntry(DumpFile *file, DumpSection *symtab, DumpRel *r, int index) {
    char buffer[0xc00];
    unsigned long symbol = r->info >> 8;
    char *name = DumpELF_SymbolName(file, symtab, symbol);

    DUMP_PRINT(buffer, (buffer, "    [% 4i] %08x %4i %-s(%i)\r\n", index, r->offset, r->info & 0xff, name, symbol));
}

/* Dumps a .rel section. */
/* 0x43a070 */
void DumpELF_RelSection(DumpFile *file, DumpSection *s) {
    char buffer[0xc00];
    unsigned long i;

    DUMP_PRINT(buffer, (buffer, "\r\n==> %s\r\n", s->name));
    DUMP_PRINT(buffer, (buffer, "    entry  offset   type name(symbol id)\r\n"));
    for (i = 0; i < s->header->size / sizeof(DumpRel); i++)
        DumpELF_RelEntry(file, s->link, (DumpRel *)s->data + i, i);
}

/* Dumps a .rela section. */
/* 0x439fd0 */
void DumpELF_RelaSection(DumpFile *file, DumpSection *s) {
    char buffer[0xc00];
    unsigned long i;

    DUMP_PRINT(buffer, (buffer, "\r\n==> %s\r\n", s->name));
    DUMP_PRINT(buffer, (buffer, "    entry  offset   addend   type name(symbol id)\r\n"));
    for (i = 0; i < s->header->size / sizeof(DumpRela); i++)
        DumpELF_RelaEntry(file, s->link, (DumpRela *)s->data + i, i);
}

/* Dumps a ROM copy table entry, with its name when the table has strings. */
/* 0x439f50 */
void DumpELF_RomCopyEntry(DumpFile *file, char *strings, DumpRomCopyEntry *e) {
    char buffer[0xc00];

    if (strings)
        DUMP_PRINT(buffer, (buffer, "    [% 4i]  %i            %-13s %4i \r\n", e->index, e->isRomCopy,
            strings + e->name, e->ramIndex));
    else
        DUMP_PRINT(buffer, (buffer, "    [% 4i]  %i  \t       %4i \r\n", e->index, e->isRomCopy, e->ramIndex));
}

/* Dumps a ROM copy table (12-byte entries). */
/* 0x439e80 */
void DumpELF_RomCopyTable(DumpFile *file, DumpSection *s) {
    char buffer[0xc00];
    char *strings = 0;
    unsigned long i;

    DUMP_PRINT(buffer, (buffer, "\r\n==> %s\r\n", s->name));
    if (s->link) {
        strings = s->link->data;
        DUMP_PRINT(buffer, (buffer,
            "    entry   is_rom_copy  name             ram index (valid only if is_rom_copy is 1)\r\n"));
    } else {
        DUMP_PRINT(buffer, (buffer, "    entry   is_rom_copy   ram index (valid only if is_rom_copy is 1)\r\n"));
    }
    for (i = 0; i < s->header->size / 12; i++)
        DumpELF_RomCopyEntry(file, strings, (DumpRomCopyEntry *)s->data + i);
}

/* Dumps a .line section (DWARF 1 lines): each unit's length and address, then its lines. */
/* 0x439d20 */
void DumpELF_LineSection(DumpSection *s) {
    char buffer[0x200];
    unsigned long offset = 0;
    unsigned long length;
    unsigned long i;
    unsigned long *unit;
    char *line;
    long address;

    DUMP_PRINT(buffer, (buffer, "\r\n==> %s\r\n", s->name));
    while (offset < s->header->size) {
        unit = (unsigned long *)(s->data + offset);
        length = unit[0];
        DUMP_PRINT(buffer, (buffer, "          length           address\r\n"));
        DUMP_PRINT(buffer, (buffer, "%08x  %08x         %08x\r\n", offset, length, unit[1]));
        offset += 8;
        DUMP_PRINT(buffer, (buffer, "          source  offset   char offset   code offset\r\n"));
        line = s->data + offset;
        for (i = 0; i < (length - 8) / 10; i++) {
            memcpy(&address, line + 6, 4);
            DUMP_PRINT(buffer, (buffer, "%08x  %08x         %ld            %08x\r\n", offset, *(long *)line,
                (long)*(short *)(line + 4), address));
            offset += 10;
            line += 10;
        }
    }
}

/* The index in the processor names of a comment section's processor, which each version numbers its own way. */
/* 0x439a10 */
int DumpELF_ProcessorIndex(short cpu, int version) {
    static const signed char v1[] = {0, 1, 2, 3, 5, 6, 7, 9, 0xb, 0xc, 0xd, 0xe, 0x11, 0x14};
    static const signed char v3[] = {0, 1, 2, 3, 5, 6, 7, 9, 0xb, 0xc, 0xd, 0xe, 0xf, 0x11, 0x14};
    static const signed char v4[] = {0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 0xb, 0xc, 0xd, 0xe, 0xf, 0x11, 0x14};
    static const signed char v5[] = {0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11, 0x14};
    const signed char *table;
    int count;

    switch (version) {
    case 1:
    case 2:
        table = v1;
        count = sizeof(v1);
        break;
    case 3:
        table = v3;
        count = sizeof(v3);
        break;
    case 4:
        table = v4;
        count = sizeof(v4);
        break;
    case 5:
    case 6:
        table = v5;
        count = sizeof(v5);
        break;
    case 7:
    case 8:
        if (cpu >= 0 && cpu <= 17)
            return cpu;
        if (cpu == 18)
            return 0x13;
        if (cpu == 19)
            return 0x14;
        return -1;
    case 9:
        if (cpu >= 0 && cpu <= 21)
            return cpu;
        return -1;
    default:
        return cpu;
    }
    if (cpu >= 0 && cpu < count)
        return table[cpu];
    return -1;
}

/* Dumps a .comment section: the compiler or linker that made the object, and the flags of each symbol. */
/* 0x439520 */
void DumpELF_Comment(DumpSection *section) {
    static char *const abiNames[] = {
        "PPC EABI compliant",
        "Non-PPC EABI compliant: used #pragma incompatible_return_small_structs",
        "Non-PPC EABI compliant: used #pragma incompatible_sfpe_double_params",
        "Non-PPC EABI compliant: used #pragma incompatible_return_small_structs\n\t\t\t\t\t\tand #pragma "
        "incompatible_sfpe_double_params",
        "Non-PPC EABI compliant: used #pragma unsafe_global_reg_vars",
        "Non-PPC EABI compliant: used #pragma incompatible_return_small_structs\n\t\t\t\t\t\tand #pragma "
        "unsafe_global_reg_vars",
        "Non-PPC EABI compliant: used #pragma incompatible_sfpe_double_params\n\t\t\t\t\t\tand #pragma "
        "unsafe_global_reg_vars",
        "Non-PPC EABI compliant: used #pragma incompatible_return_small_structs,\n\t\t\t\t\t\t#pragma "
        "incompatible_sfpe_double_params and #pragma unsafe_global_reg_vars",
    };
    static char *const processorNames[] = {
        "401", "403", "505", "509", "555", "601", "602", "603", "603e", "604", "604e", "740", "750",
        "801", "821", "823", "850", "860", "8240", "8260", "Generic", "7400", "Unknown CPU", "Unknown CPU", "7450",
    };
    char buffer[0x200];
    char *p = section->data;
    signed char version;
    char *text;
    char *fp;
    char *processor;
    short cpu;
    int index;
    char *entry;
    unsigned long count;
    unsigned long i;
    char *kind;
    unsigned char flags;

    section->done = 1;
    DUMP_PRINT(buffer, (buffer, "\r\n==> %s", section->name));
    version = p[0xb];
    /* (the original leaves the text unset for another kind; here it is the compiler's) */
    text = "CW Compiler generated";
    if (version >= 8) {
        switch ((signed char)p[0xf]) {
        case 1: text = "CW Compiler generated"; break;
        case 2: text = "CW Linker generated unoptimized partial link"; break;
        case 3: text = "CW Linker generated optimized partial link"; break;
        }
    }
    DUMP_PRINT(buffer, (buffer,
        "\r\nMagicWord: %.*s\r\nComment section version: %d\r\nCompiler version: %d.%d.%d\r\n%s\r\nPooled data: %s\r\n",
        0xb, p, (int)version, (int)(signed char)p[0xc], (int)(signed char)p[0xd], (int)(signed char)p[0xe], text,
        p[0x10] ? "true" : "false"));
    if (version >= 8) {
        if ((unsigned long)(long)(signed char)p[0x15] <= 7)
            text = abiNames[(signed char)p[0x15]];
        DUMP_PRINT(buffer, (buffer, "%s\r\n", text));
    }
    if (version > 1) {
        if (p[0x11] == 0)
            fp = "None";
        else if (p[0x11] == 1)
            fp = "Software";
        else
            fp = "Hardware";
        DUMP_PRINT(buffer, (buffer, "FP Support: %s\r\n", fp));
        memcpy(&cpu, p + 0x12, 2);
        cpu = CTool_EndianConvertWord16(cpu);
        index = (short)DumpELF_ProcessorIndex(cpu, version);
        if ((unsigned long)index <= 0x18)
            processor = processorNames[index];
        else
            processor = "Unknown CPU";
        DUMP_PRINT(buffer, (buffer, "Processor: %s\r\n", processor));
    }
    if (version >= 6) {
        entry = p + (signed char)p[0x14];
        count = (section->header->size - (signed char)p[0x14]) >> 3;
        if (count == 0)
            return;
        if (data_004a2dc2)
            DUMP_PRINT(buffer, (buffer, "    entry  align  flags\r\n"));
        for (i = 0; (long)i < (long)count; i++, entry += 8) {
            CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)entry);
            if (data_004a2dc2) {
                switch ((signed char)entry[4]) {
                case 0xd: kind = "MULTIDEF"; break;
                case 0xe: kind = "OVERLOAD"; break;
                default: kind = ""; break;
                }
                DUMP_PRINT(buffer, (buffer, "    [% 4i]  % 4i", i, *(long *)entry));
                flags = entry[5];
                if (flags & 1)
                    DUMP_PRINT(buffer, (buffer, "  %s", "Not_CW_Built"));
                if (flags & 2)
                    DUMP_PRINT(buffer, (buffer, "  %s", "export"));
                if (flags & 4)
                    DUMP_PRINT(buffer, (buffer, "  %s", "internal"));
                if (flags & 8)
                    DUMP_PRINT(buffer, (buffer, "  %s", "force_active"));
                if (flags & 0x10)
                    DUMP_PRINT(buffer, (buffer, "  %s", "pooled_anchor"));
                DUMP_PRINT(buffer, (buffer, "   %s\r\n", kind));
            }
        }
    }
}

/* Dumps a .note section: each note's sizes, type and name, and its description in hex and text. */
/* 0x439260 */
void DumpELF_Notes(DumpSection *section) {
    char buffer[0x200];
    char hex[0x10];
    unsigned char c;
    unsigned long offset;
    long *note;
    unsigned long skip;
    unsigned char *desc;
    long size;
    long at;
    int i;

    section->done = 1;
    DUMP_PRINT(buffer, (buffer, "\r\n==> %s", section->name));
    offset = 0;
    while (offset < section->header->size) {
        note = (long *)(section->data + offset);
        if (offset != 0)
            DUMP_PRINT(buffer, (buffer, "\r\n"));
        DUMP_PRINT(buffer, (buffer, "\r\nnamesz:   %ld\r\ndescsz:   %ld\r\ntype:     %ld\r\nname:     %s\r\n", note[0],
            note[1], note[2], (char *)note + 12));
        skip = (note[0] + 15) & ~3;
        offset += skip;
        if (note[1] != 0) {
            DUMP_PRINT(buffer, (buffer, "desc:"));
            desc = (unsigned char *)note + skip;
            for (at = 0; at < note[1]; at += 16) {
                if (at == 0)
                    DUMP_PRINT(buffer, (buffer, "     "));
                else
                    DUMP_PRINT(buffer, (buffer, "\r\n          "));
                size = note[1];
                for (i = 0; i < 16; i++) {
                    if (i == 8)
                        data_0049a6f0("          ", 2, 0x10);
                    if (i + at < size) {
                        sprintf(hex, "%.2X ", desc[at + i]);
                        data_0049a6f0(hex, 3, 0x10);
                    } else {
                        data_0049a6f0("          ", 3, 0x10);
                    }
                }
                size = note[1];
                data_0049a6f0("'", 1, 1);
                for (i = 0; i < 16; i++) {
                    if (i + at < size) {
                        c = desc[at + i];
                        if (!(__ctype_map[c] & 0xdc))
                            c = '.';
                        data_0049a6f0((char *)&c, 1, 1);
                    }
                }
                data_0049a6f0("'", 1, 1);
            }
            offset = (offset + note[1] + 3) & ~3;
        }
    }
}

/* Converts the exception table of a symbol (its size long) to the host's byte order: its header and ranges, then
   its actions. */
/* 0x438f30 */
void DumpELF_SwapExceptionTable(DumpSymbol *symbol, unsigned short *data, char readFirst) {
    char *p;
    char *end;
    char *next;
    unsigned short count;
    int i;

    data[0] = CTool_EndianConvertWord16(data[0]);
    if (data[0] >> 3 & 1) {
        for (p = (char *)(data + 2); *(long *)p != 0; p += 8) {
            *(unsigned long *)p = CTool_EndianConvertWord32(*(unsigned long *)p);
            *(unsigned short *)(p + 4) = CTool_EndianConvertWord16(*(unsigned short *)(p + 4));
            *(unsigned short *)(p + 6) = CTool_EndianConvertWord16(*(unsigned short *)(p + 6));
        }
        p += 4;
    } else {
        for (p = (char *)(data + 1); *(unsigned short *)p != 0; p += 6) {
            *(unsigned short *)p = CTool_EndianConvertWord16(*(unsigned short *)p);
            *(unsigned short *)(p + 2) = CTool_EndianConvertWord16(*(unsigned short *)(p + 2));
            *(unsigned short *)(p + 4) = CTool_EndianConvertWord16(*(unsigned short *)(p + 4));
        }
        p += 2;
    }
    end = (char *)data + symbol->size;
    while (p < end) {
        next = end;
        switch (*(unsigned char *)p & 0x7f) {
        case 1:
            CTool_EndianConvertInPlaceWord16Ptr((short *)(p + 2));
            next = p + 4;
            break;
        case 2:
        case 4:
        case 10:
            CTool_EndianConvertInPlaceWord16Ptr((short *)(p + 2));
            CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)(p + 4));
            next = p + 8;
            break;
        case 3:
        case 11:
            CTool_EndianConvertInPlaceWord16Ptr((short *)(p + 2));
            CTool_EndianConvertInPlaceWord16Ptr((short *)(p + 4));
            CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)(p + 8));
            next = p + 12;
            break;
        case 5:
            CTool_EndianConvertInPlaceWord16Ptr((short *)(p + 2));
            CTool_EndianConvertInPlaceWord16Ptr((short *)(p + 4));
            CTool_EndianConvertInPlaceWord16Ptr((short *)(p + 6));
            CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)(p + 8));
            next = p + 12;
            break;
        case 6:
        case 7:
            CTool_EndianConvertInPlaceWord16Ptr((short *)(p + 2));
            CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)(p + 4));
            CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)(p + 8));
            next = p + 12;
            break;
        case 8:
            CTool_EndianConvertInPlaceWord16Ptr((short *)(p + 2));
            CTool_EndianConvertInPlaceWord16Ptr((short *)(p + 4));
            CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)(p + 8));
            CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)(p + 12));
            next = p + 16;
            break;
        case 9:
            CTool_EndianConvertInPlaceWord16Ptr((short *)(p + 2));
            CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)(p + 4));
            CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)(p + 8));
            CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)(p + 12));
            CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)(p + 16));
            next = p + 20;
            break;
        case 12:
            CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)(p + 4));
            CTool_EndianConvertInPlaceWord16Ptr((short *)(p + 8));
            CTool_EndianConvertInPlaceWord16Ptr((short *)(p + 10));
            next = p + 12;
            break;
        case 13:
            CTool_EndianConvertInPlaceWord16Ptr((short *)(p + 2));
            next = p + 4;
            break;
        case 14:
            next = p + 2;
            break;
        case 15:
            /* the count of the words that follow: read before converting it when the file is the host's order */
            if (readFirst) {
                count = *(unsigned short *)(p + 2);
                CTool_EndianConvertInPlaceWord16Ptr((short *)(p + 2));
            } else {
                count = CTool_EndianConvertInPlaceWord16Ptr((short *)(p + 2));
            }
            CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)(p + 4));
            CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)(p + 8));
            next = p + 12;
            for (i = 0; i < count; i++, next += 4)
                CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)next);
            next = p + 12 + *(unsigned short *)(p + 2) * 4;
            break;
        case 16:
            CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)(p + 4));
            CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)(p + 8));
            CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)(p + 12));
            next = p + 16;
            break;
        }
        p = next;
    }
}

#define DUMP_SYMBOL(file, k) \
    ((DumpSymbol *)(file)->symbols->data + (file)->sorted[k].index)
#define DUMP_SYMBOL_NAME(file, symbol) ((file)->symbols->link->data + (symbol)->name)

/* The bytes of an exception table action that are a stack or a frame pointer's offset. */
#define DUMP_BASE(framePointer) ((framePointer) ? "FP" : "SP")
#define DUMP_S16(p, at) ((long)*(short *)((p) + (at)))
#define DUMP_U16(p, at) ((unsigned long)*(unsigned short *)((p) + (at)))
#define DUMP_S32(p, at) (*(long *)((p) + (at)))

/* Prints a location of an action: a register (`registerFlag`) or an offset from the frame. */
#define DUMP_LOCATION(buffer, registerFlag, at)                                                                     \
    if (registerFlag)                                                                                                 \
        DUMP_PRINT(buffer, (buffer, "r%ld", DUMP_S16(p, at)));                                                      \
    else                                                                                                              \
        DUMP_PRINT(buffer, (buffer, "%ld(%s)", DUMP_S16(p, at), DUMP_BASE(framePointer)))

/* Prints a member of an action: an offset from a register, or from the frame plus an offset. */
#define DUMP_MEMBER(buffer, registerFlag, at, offsetAt)                                                             \
    if (registerFlag)                                                                                                 \
        DUMP_PRINT(buffer, (buffer, "%ld(r%ld)", DUMP_S32(p, offsetAt), DUMP_S16(p, at)));                         \
    else                                                                                                              \
        DUMP_PRINT(buffer, (buffer, "%ld(%s)+%ld", DUMP_S16(p, at), DUMP_BASE(framePointer), DUMP_S32(p, offsetAt)))

/* Prints the condition of an action: a register, or an offset from the frame. */
#define DUMP_CONDITION(buffer, registerFlag)                                                                         \
    if (registerFlag)                                                                                                 \
        DUMP_PRINT(buffer, (buffer, "  Cond=r%ld", DUMP_S16(p, 2)));                                                \
    else                                                                                                              \
        DUMP_PRINT(buffer, (buffer, "  Cond=%ld(%s)", DUMP_S16(p, 2), DUMP_BASE(framePointer)))

/* Prints an exception table (a function's, in extab): its saved registers, its PC ranges and their actions. */
/* 0x4382d0 */
void DumpELF_ExceptionTable(DumpFile *file, DumpSymbol *symbol, unsigned char *table) {
    char buffer[0xc00];
    unsigned short word;
    unsigned short gprs;
    unsigned short fprs;
    unsigned short cr;
    unsigned short framePointer;
    unsigned char large;
    unsigned char *p;
    unsigned char *end;
    unsigned char kind;
    unsigned long pc;
    unsigned short length;

    DUMP_PRINT(buffer, (buffer, "\r\nException Table Name=\"%s\"  Size=%ld", DUMP_SYMBOL_NAME(file, symbol),
                        symbol->size));
    word = *(unsigned short *)table;
    gprs = word >> 11;
    fprs = (word >> 6) & 0x1f;
    cr = (word >> 5) & 1;
    framePointer = (word >> 4) & 1;
    large = (word >> 3) & 1;
#if LINKER >= 27
    /* (2.7 prints the whole label: 2.6's length is one short of it) */
    data_0049a6f0("\r\n\t\tSaved Registers:         ", 0x1d, 0x1d);
#else
    data_0049a6f0("\r\n\t\tSaved Registers:         ", 0x1c, 0x1c);
#endif
    if (!gprs && !fprs && !cr && !large) {
        data_0049a6f0("none", 4, 4);
    } else {
        if (gprs) {
            if (gprs > 1)
                DUMP_PRINT(buffer, (buffer, "r%d-r31", 32 - gprs));
            else
                data_0049a6f0("r31", 3, 3);
        }
        if (fprs) {
            if (gprs)
                data_0049a6f0("/", 1, 1);
            if (fprs > 1)
                DUMP_PRINT(buffer, (buffer, "fp%d-fp31", 32 - fprs));
            else
                data_0049a6f0("fp31", 4, 4);
        }
        if (cr) {
            if (gprs || fprs)
                data_0049a6f0("/", 1, 1);
            data_0049a6f0("CR", 2, 2);
        }
        if (large) {
            if (gprs || fprs || cr)
                data_0049a6f0("/", 1, 1);
            data_0049a6f0("Large Frame", 0xb, 0xb);
        }
    }
    DUMP_PRINT(buffer, (buffer, "\r\n\t\tHas Frame Pointer:       %s", framePointer ? "Yes" : "No"));

    /* the PC ranges: 32-bit PCs and lengths in a large table, 16-bit start and end PCs in a small one */
    if (large) {
        for (p = table + 4; (pc = *(unsigned long *)p) != 0; p += 8) {
            length = *(unsigned short *)(p + 4);
            if (!length)
                DUMP_PRINT(buffer, (buffer, "\r\n\t\tPC=%.8X              Action=%.6X", pc, DUMP_U16(p, 6)));
            else
                DUMP_PRINT(buffer, (buffer, "\r\n\t\tPC=%.8X:%.8X     Action=%.6X", pc, pc + length, DUMP_U16(p, 6)));
        }
        p += 4;
    } else {
        for (p = table + 2; (pc = *(unsigned short *)p) != 0; p += 6) {
            if (pc == DUMP_U16(p, 2))
                DUMP_PRINT(buffer, (buffer, "\r\n\t\tPC=%.8X              Action=%.6X", pc, DUMP_U16(p, 4)));
            else
                DUMP_PRINT(buffer, (buffer, "\r\n\t\tPC=%.8X:%.8X     Action=%.6X", pc, DUMP_U16(p, 2),
                                    DUMP_U16(p, 4)));
        }
        p += 2;
    }

    /* the actions, to the end of the table */
    end = table + symbol->size;
    while (p < end) {
        kind = p[0];
        if (kind == 0) {
            DUMP_PRINT(buffer, (buffer, "\r\n%.6lX: NULL %.4lX", (long)(p - table), (unsigned long)kind));
            p += 2;
        } else {
            switch (kind & 0x7f) {
            case 1:
                DUMP_PRINT(buffer, (buffer, "\r\n%.6lX: BRANCH                   Action=%.6lX", (long)(p - table),
                                    DUMP_U16(p, 2)));
                p += 4;
                break;
            case 2:
                DUMP_PRINT(buffer, (buffer, "\r\n%.6lX: DESTROYLOCAL             Local=%ld(%s)", (long)(p - table),
                                    DUMP_S16(p, 2), DUMP_BASE(framePointer)));
                p += 8;
                break;
            case 3:
                DUMP_PRINT(buffer, (buffer, "\r\n%.6lX: DESTROYLOCALCOND         Local=%ld(%s)", (long)(p - table),
                                    DUMP_S16(p, 4), DUMP_BASE(framePointer)));
                DUMP_CONDITION(buffer, (p[1] << 7) != 0);
                p += 0xc;
                break;
            case 4:
                DUMP_PRINT(buffer, (buffer, "\r\n%.6lX: DESTROYLOCALPOINTER      Pointer=", (long)(p - table)));
                DUMP_LOCATION(buffer, p[1] >> 7, 2);
                p += 8;
                break;
            case 5:
                DUMP_PRINT(buffer, (buffer, "\r\n%.6lX: DESTROYLOCALARRAY        Array=%ld(%s)  Elements=%ld  Size=%ld",
                                    (long)(p - table), DUMP_S16(p, 2), DUMP_BASE(framePointer), DUMP_U16(p, 4),
                                    DUMP_U16(p, 6)));
                p += 0xc;
                break;
            case 6:
                DUMP_PRINT(buffer, (buffer, "\r\n%.6lX: DESTROYBASE              Member=", (long)(p - table)));
                DUMP_MEMBER(buffer, p[1] >> 7, 2, 4);
                p += 0xc;
                break;
            case 7:
                DUMP_PRINT(buffer, (buffer, "\r\n%.6lX: DESTROYMEMBER            Member=", (long)(p - table)));
                DUMP_MEMBER(buffer, p[1] >> 7, 2, 4);
                p += 0xc;
                break;
            case 8:
                DUMP_PRINT(buffer, (buffer, "\r\n%.6lX: DESTROYMEMBERCOND        Member=", (long)(p - table)));
                DUMP_MEMBER(buffer, (p[1] >> 6) & 1, 4, 8);
                DUMP_CONDITION(buffer, p[1] >> 7);
                p += 0x10;
                break;
            case 9:
                /* (a member array: the original prints it as DESTROYMEMBER) */
                DUMP_PRINT(buffer, (buffer, "\r\n%.6lX: DESTROYMEMBER            Member=", (long)(p - table)));
                DUMP_MEMBER(buffer, p[1] >> 7, 2, 4);
                DUMP_PRINT(buffer, (buffer, "Elements=%ld  Size=%ld", DUMP_S32(p, 8), DUMP_S32(p, 0xc)));
                p += 0x14;
                break;
            case 10:
                DUMP_PRINT(buffer, (buffer, "\r\n%.6lX: DELETEPOINTER            Pointer=", (long)(p - table)));
                DUMP_LOCATION(buffer, p[1] >> 7, 2);
                p += 8;
                break;
            case 11:
                DUMP_PRINT(buffer, (buffer, "\r\n%.6lX: DELETEPOINTERCOND        Pointer=", (long)(p - table)));
                DUMP_LOCATION(buffer, (p[1] >> 6) & 1, 4);
                DUMP_CONDITION(buffer, p[1] >> 7);
                p += 0xc;
                break;
            case 12:
                DUMP_PRINT(buffer, (buffer,
                                    "\r\n%.6lX: CATCHBLOCK (Small)       Local=%ld(%s)  PC=%.8lX  catch_type_addr=%.8lX",
                                    (long)(p - table), DUMP_S16(p, 0xa), DUMP_BASE(framePointer), DUMP_U16(p, 8),
                                    DUMP_S32(p, 4)));
                p += 0xc;
                break;
            case 13:
                DUMP_PRINT(buffer, (buffer, "\r\n%.6lX: ACTIVECATCHBLOCK         Local=%ld(%s)", (long)(p - table),
                                    DUMP_S16(p, 2), DUMP_BASE(framePointer)));
                p += 4;
                break;
            case 14:
                DUMP_PRINT(buffer, (buffer, "\r\n%.6lX: TERMINATE", (long)(p - table)));
                p += 2;
                break;
            case 15:
                DUMP_PRINT(buffer, (buffer, "\r\n%.6lX: SPECIFICATION            Local=%ld(%s)  PC=%.8lX  Types=%ld",
                                    (long)(p - table), DUMP_S32(p, 8), DUMP_BASE(framePointer), DUMP_S32(p, 4),
                                    DUMP_U16(p, 2)));
                p += DUMP_U16(p, 2) * 4 + 0xc;
                break;
            case 16:
                DUMP_PRINT(buffer, (buffer,
                                    "\r\n%.6lX: CATCHBLOCK (Large)       Local=%ld(%s)  PC=%.8lX  catch_type_addr=%.8lX",
                                    (long)(p - table), DUMP_S32(p, 0xc), DUMP_BASE(framePointer), DUMP_S32(p, 8),
                                    DUMP_S32(p, 4)));
                p += 0x10;
                break;
            default:
                p = end;
                break;
            }
        }
        if (kind & 0x80)
            data_0049a6f0(".", 1, 1);
    }
    data_0049a6f0("\r\n", 2, 2);
}

/* Prints an extab section: the exception table of each of its symbols. */
/* 0x438210 */
void DumpELF_ExceptionTables(DumpFile *file, DumpSection *section) {
    char buffer[0xc00];
    char *data;
    DumpSymbol *symbol;
    long k;

    DUMP_PRINT(buffer, (buffer, "\r\n==> %s\r\n", section->name));
    if (!section->header->size)
        return;
    data = section->data;
    for (k = 0; k < file->symbolCount; k++) {
        symbol = DUMP_SYMBOL(file, k);
        if (symbol->sectionIndex == section->index && (symbol->info & 0xf) != 3)
            DumpELF_ExceptionTable(file, symbol,
                                   (unsigned char *)data + (symbol->value - section->header->address));
    }
}

/* The column of characters of a hex dump's line: its bytes from `first` to `count` - 1, printable or '.'. */
#define DUMP_CHARACTERS()                                                                                            \
    {                                                                                                                 \
        data_0049a6f0("'", 1, 1);                                                                                     \
        for (position = 0; position < 16; position++) {                                                               \
            if (position < first || position > count - 1) {                                                           \
                data_0049a6f0("          ", 1, 0x10);                                                                 \
            } else {                                                                                                  \
                character = data[lineStart + position];                                                               \
                if (!(__ctype_map[character] & 0xdc))                                                                 \
                    character = '.';                                                                                  \
                data_0049a6f0((char *)&character, 1, 1);                                                              \
            }                                                                                                         \
        }                                                                                                             \
        data_0049a6f0("'", 1, 1);                                                                                     \
    }

/* Ends a hex dump's line: the column of characters (`withData`), when it is printed. */
#define DUMP_END_LINE()                                                                                              \
    if (!eliding) {                                                                                                   \
        data_0049a6f0("        ", 1, 0xc00);                                                                          \
        if (withData)                                                                                                 \
            DUMP_CHARACTERS();                                                                                       \
    }

/* Prints a section's bytes, 16 to a line with their characters, each symbol's name before its bytes; past 256 bytes
   from a symbol, " ... " stands for the rest unless -show ...,detail. A section without data (`withData` 0) shows
   zeros. */
/* 0x437a40 */
void DumpELF_HexDump(DumpFile *file, DumpSection *section, char withData) {
    char buffer[0xc00];
    unsigned long base;
    unsigned long size;
    unsigned long offset;
    unsigned long address;
    unsigned char *data;
    unsigned char character;
    DumpSymbol *symbol;
    long k;
    long column;
    long lineStart;
    long first;
    long count;
    long sinceSymbol;
    long padded;
    long position;
    char eliding;

    base = section->header->address;
    padded = 0;
    column = 0;
    lineStart = 0;
    first = 0;
    count = 0;
    sinceSymbol = 0;
    eliding = 0;
    DUMP_PRINT(buffer, (buffer, "\r\n==> %s", section->name));
    size = section->header->size;
    if (!size) {
        /* (a common section: the size of its section symbol) */
        for (k = 0; k < file->symbolCount; k++) {
            symbol = DUMP_SYMBOL(file, k);
            if (symbol->sectionIndex == section->index && (symbol->info & 0xf) == 3)
                size = symbol->size;
        }
    }
    if (withData)
        data = (unsigned char *)section->data;
    for (offset = 0; offset < size; offset++) {
        address = offset + base;
        for (k = 0; k < file->symbolCount; k++) {
            symbol = DUMP_SYMBOL(file, k);
            if (symbol->sectionIndex != section->index || (symbol->info & 0xf) == 3)
                continue;
            if (symbol->value == address) {
                /* a symbol starts here: its name on a line of its own */
                sinceSymbol = 0;
                if (column == 0x13) {
                    DUMP_END_LINE();
                    first = 0;
                    column = 0;
                    count = 0;
                }
                if (column) {
                    for (padded = column; padded < 0x13; padded++)
                        if (!eliding)
                            data_0049a6f0("        ", 2, 0xc00);
                    DUMP_END_LINE();
                }
                eliding = 0;
                DUMP_PRINT(buffer, (buffer, "\r\n%s:", DUMP_SYMBOL_NAME(file, symbol)));
                first = count;
            } else if (symbol->value > address) {
                break;
            }
        }
        if (sinceSymbol > 0x100 && !data_004a2dc2) {
            if (!eliding)
                DUMP_PRINT(buffer, (buffer, " ... "));
            eliding = 1;
        }
        if (column == 0x13) {
            DUMP_END_LINE();
            first = 0;
            column = 0;
            count = 0;
        }
        if (column == 0)
            lineStart = offset;
        if (column == 0 || padded == 0x13) {
            if (!eliding) {
                DUMP_PRINT(buffer, (buffer, "\r\n%.8lX: ", address));
                for (k = 0; k < column; k++)
                    data_0049a6f0("        ", 2, 0xc00);
            }
            padded = 0;
        }
        if (column == 4 || column == 9 || column == 14) {
            if (!eliding)
                data_0049a6f0("        ", 2, 0xc00);
            column++;
        }
        if (!eliding) {
            sprintf(buffer, "%.2X", withData ? data[offset] : 0);
            data_0049a6f0(buffer, 2, 0xc00);
        }
        column++;
        count++;
        sinceSymbol++;
    }
    if (!eliding && column) {
        for (k = column; k < 0x13; k++)
            data_0049a6f0("        ", 2, 0xc00);
        data_0049a6f0("        ", 1, 0xc00);
        if (withData)
            DUMP_CHARACTERS();
    }
    DUMP_PRINT(buffer, (buffer, "\r\n"));
}

/* Checks the ELF header (its byte order, and with kind 3 its machine and version), converts it when the file's
   byte order is not the host's, and prints it (-show headers). */
/* 0x437800 */
void DumpELF_Header(DumpHeader *header) {
    char buffer[0x200];
    char *order;
    char encoding;

    order = NULL;
    encoding = 0;
    if (header->ident[5] == 2) {
        order = "MSB";
        encoding = 2;
    } else if (header->ident[5] == 1) {
        order = "LSB";
        encoding = 1;
    } else {
        DUMP_ASSERT(0x6c1);
    }
    if ((copts[0] == 0) != (encoding == 2))
        PrepDumpELF_BadFile(3);
    data_0049a6ee = copts[0] == 0;
    if (data_0049a6ee && (data_0049a6ed & 1))
        ELF_Endian_SwapElf32Header((struct Elf32Header *)header);
    if (data_0049a6e4 == 3) {
        if (header->machine != 0x14)
            PrepDumpELF_BadFile(1);
        if (header->version != 1)
            PrepDumpELF_BadFile(2);
    }
    if (data_0049a6e5 && data_004a2dc4) {
        DUMP_PRINT(buffer, (buffer, "Header idenfication: 0x%x, %c%c%c, %s %s v%i\r\n", header->ident[0],
                            header->ident[1], header->ident[2], header->ident[3],
                            header->ident[4] == 1 ? "32 bit" : "Error 100", order, header->ident[6]));
        DUMP_PRINT(buffer, (buffer, "    type:    %8i, machine:   %8i\r\n", header->type, header->machine));
        DUMP_PRINT(buffer, (buffer, "    version: %08x, entry:     %08x, phoff:  %08x\r\n", header->version,
                            header->entry, header->phoff));
        DUMP_PRINT(buffer, (buffer, "    shoff:   %08x, flags:     %08x, ehsize: %8i, phentsize:  %i\r\n",
                            header->shoff, header->flags, header->ehsize, header->phentsize));
        DUMP_PRINT(buffer, (buffer, "    phnum:   %8i, shentsize: %8i, shnum:  %8i, shstrndx:  %i\r\n",
                            header->phnum, header->shentsize, header->shnum, header->shstrndx));
        data_0049a6f0("\r\n", 2, 2);
    }
    if (data_0049a6e5 && data_004a2d6e)
        DumpCode_Setup(data_0049a6f0);
}

/* Makes the list of the program headers (converted when the byte order is not the host's) and prints them. */
/* 0x437690 */
void DumpELF_ProgramHeaders(DumpFile *file) {
    char buffer[0x200];
    DumpProgram *program;
    DumpProgramHeader *header;
    long i;

    for (i = 0; i < ((DumpHeader *)file->header)->phnum; i++) {
        if (i == 0) {
            program = galloc(sizeof(DumpProgram));
            file->programs = program;
        } else {
            program->next = galloc(sizeof(DumpProgram));
            program = program->next;
        }
        header = (DumpProgramHeader *)(file->image + ((DumpHeader *)file->header)->phoff + i * 0x20);
        program->header = header;
        program->xc = 0;
        program->index = i;
        program->next = NULL;
        if (data_0049a6ee && data_0049a6ec)
            ELF_Endian_SwapElf32ProgramHeader((unsigned int *)header);
        if (data_0049a6e5 && data_004a2dc4) {
            DUMP_PRINT(buffer, (buffer, "\r\nProgram Header [%i]\r\n", i));
            DUMP_PRINT(buffer, (buffer, "    p_type:   %08x, p_offset: %08x\r\n", header->type, header->offset));
            DUMP_PRINT(buffer, (buffer, "    p_vaddr:  %08x, p_paddr:  %08x\r\n", header->vaddr, header->paddr));
            DUMP_PRINT(buffer, (buffer, "    p_filesz: %08x, p_memsz:  %08x, p_flags: %08x, p_align: %08x\r\n",
                                header->filesz, header->memsz, header->flags, header->align));
        }
    }
}

/* Helpers of DumpELF_Dump; the original has each of them inline. */

/* The section with this index; asserts when there is none. */
static DumpSection *DumpELF_FindSection(DumpSection *sections, unsigned long index) {
    DumpSection *section;

    for (section = sections; section; section = section->next) {
        if (section->index == index)
            return section;
    }
    data_0049a6f4("unable to find section", index);
    return NULL;
}

/* The section with this name. */
static DumpSection *DumpELF_FindNamed(DumpSection *sections, const char *name) {
    DumpSection *section;

    for (section = sections; section; section = section->next) {
        if (section->name && !strcmp(section->name, name))
            return section;
    }
    return NULL;
}

/* Dumps a section's relocations: REL or RELA. */
static void DumpELF_DumpRelocations(DumpFile *file, DumpSection *relocations) {
    if (relocations->header->type == 9)
        DumpELF_RelSection(file, relocations);
    else if (relocations->header->type == 4)
        DumpELF_RelaSection(file, relocations);
}

/* Converts the byte order of each exception table: `back` is 1 to put them in the file's order again. */
static void DumpELF_ConvertExceptionTables(DumpFile *file, DumpSection *extab, int back) {
    DumpSymbol *symbol;
    long i;

    for (i = 0; i < file->symbolCount; i++) {
        symbol = (DumpSymbol *)file->symbols->data + file->sorted[i].index;
        if (symbol->sectionIndex == extab->index && (symbol->info & 0xf) != 3)
            DumpELF_SwapExceptionTable(symbol, (unsigned short *)(extab->data + symbol->value - extab->header->address),
                                          back);
    }
}

/* Dumps a section's bytes, 16 to a line, in hex and as characters. */
static void DumpELF_RawBytes(DumpSection *section) {
    char text[0x200];
    char line[0x10];
    char c;
    unsigned long size;
    unsigned char *data;
    unsigned long offset;
    int i;

    DUMP_PRINT(text, (text, "\r\n==> %s", section->name));
    size = section->header->size;
    data = (unsigned char *)section->data;
    for (offset = 0; offset < size; offset += 16) {
        DUMP_PRINT(text, (text, "\r\n%.8lX: ", offset));
        for (i = 0; i < 16; i++) {
            if (i == 8)
                data_0049a6f0("          ", 2, 0x10);
            if (offset + i < size)
                data_0049a6f0(line, sprintf(line, "%.2X ", data[offset + i]), 0x10);
            else
                data_0049a6f0("          ", 3, 0x10);
        }
        data_0049a6f0("'", 1, 1);
        for (i = 0; i < 16; i++) {
            if (offset + i < size) {
                c = data[offset + i];
                if (!(__ctype_map[(unsigned char)c] & 0xdc))
                    c = '.';
                data_0049a6f0(&c, 1, 1);
            }
        }
        data_0049a6f0("'", 1, 1);
    }
    data_0049a6f0("\r\n", 2, 2);
}

/* Dumps an ELF image: the file header, each section header, then the sections by kind (symbols, relocations,
   code, exception tables, debug information, comments, notes, dynamic sections, the rest in hex). `kind` (1 to 5)
   says what is converted (5 for -dis); without a printer the text goes to DumpELF_PrintDefault, which reports that it
   should not be called. */
/* 0x435360 */
void DumpELF_Dump(char *image, DumpAsserter assertion, DumpPrinter printer, long kind, char *name) {
    char text[0x200];
    DumpFile file;
    DumpSection *section;
    DumpSection *last;
    DumpSection *found;
    DumpSection *extab;
    Elf32Section *header;
    DumpSortedSymbol *sorted;
    unsigned long count;
    unsigned long i;
    long index;

    data_0049a6f4 = assertion;
    data_0049a6e5 = 0;
    data_0049a6e4 = kind;
    data_0049a6e0 = name;
    if (printer) {
        data_0049a6f0 = printer;
        data_0049a6e5 = 1;
    } else {
        data_0049a6f0 = DumpELF_PrintDefault;
    }
    if (data_0049a6e4 != 5)
        data_0049a6f4("Task should be DISASSEMBLE_DISK_FILE but is", data_0049a6e4);
    data_0049a6ed = 0;
    data_0049a6ec = 0;
    data_0049a6eb = 0;
    data_0049a6ea = 0;
    data_0049a6e8 = 0;
    data_0049a6e9 = 0;
    data_0049a6e7 = 0;
    if (kind == 1) {
        memset(&data_004a2d6e, 0, 0x5a);
        data_004a2d74 = 1;
        data_0049a6ed = data_0049a6ec = data_0049a6eb = data_0049a6ea = data_0049a6e8 = data_0049a6e9 = 2;
    } else if (kind == 2) {
        memset(&data_004a2d6e, 0, 0x5a);
        data_004a2d74 = 1;
        data_0049a6ed = 3;
        data_0049a6eb = data_0049a6ea = data_0049a6e8 = data_0049a6e9 = 2;
    } else if (kind == 3) {
        memset(&data_004a2d6e, 0, 0x5a);
        data_004a2d74 = 1;
        data_0049a6ed = data_0049a6ec = data_0049a6eb = data_0049a6ea = data_0049a6e8 = data_0049a6e9 = 1;
    } else if (kind == 5) {
        data_0049a6ed = data_0049a6ec = data_0049a6eb = data_0049a6ea = data_0049a6e8 = data_0049a6e9 = 1;
        data_0049a6e7 = 1;
        data_0049a6e6 = 1;
    } else if (kind == 4) {
        data_0049a6e7 = 1;
    }

    memset(&file, 0, sizeof(file));
    file.image = image;
    file.header = (Elf32Header *)image;
    DumpELF_Header((DumpHeader *)image);
    DumpELF_ProgramHeaders(&file);

    /* the section names' section */
    file.names = (DumpSection *)galloc(sizeof(DumpSection));
    file.names->header = (Elf32Section *)(image + file.header->section_header_offset) + file.header->section_name_index;
    if (data_0049a6ee && (data_0049a6eb & 1))
        ELF_Endian_SwapElf32Section(file.names->header);
    file.names->data = image + file.names->header->file_offset;
    if (data_0049a6e5 && data_004a2dc4)
        DumpELF_StringTable(file.names->data + file.names->header->name, file.names->data, file.names->header->size);

    /* the sections */
    last = NULL;
    for (index = 0; index < file.header->section_header_count; index++) {
        if (index == 0) {
            section = (DumpSection *)galloc(sizeof(DumpSection));
            file.sections = section;
            section->done = 1;
        } else {
            last->next = (DumpSection *)galloc(sizeof(DumpSection));
            section = last->next;
            section->done = 0;
        }
        header = (Elf32Section *)(image + file.header->section_header_offset) + index;
        section->header = header;
        if (data_0049a6ee && (data_0049a6eb & 1) && index != file.header->section_name_index)
            ELF_Endian_SwapElf32Section(header);
        if (data_0049a6e4 == 3) {
            if (header->flags & 4)
                accumulated_section_size += header->size;
            else if (header->flags & 2) {
                if (header->type == 1)
                    accumulated_section_sizes += header->size;
                else if (header->type == 8)
                    section_size_total += header->size;
            }
        }
        section->data = image + header->file_offset;
        section->index = index;
        section->link = NULL;
        section->next = NULL;
        section->name = file.names->data + header->name;
        if (data_0049a6e5 && index != 0 && data_004a2dc4) {
            DUMP_PRINT(text, (text, "\r\n==> %s [%i]: Section Header\r\n", section->name, index));
            DUMP_PRINT(text, (text, "    type:    %08x, flags:  %08x\r\n", header->type, header->flags));
            DUMP_PRINT(text, (text, "    address: %08x, offset: %08x, size: %08x\r\n", header->address, header->file_offset,
                              header->size));
            DUMP_PRINT(text, (text, "    link:    %08x, info:   %08x, alignment: %i, entry size: %i\r\n", header->link,
                              header->info, header->alignment, header->entry_size));
            data_0049a6f0("\r\n", 2, 2);
        }
        last = section;
        if (index == file.header->section_name_index)
            section->done = 1;
    }

    /* the string tables */
    if (data_0049a6e5) {
        for (section = file.sections; section; section = section->next) {
            if (section->header->type == 3 && section->header != file.names->header) {
                if (data_004a2dc5)
                    DumpELF_StringTable(section->name, section->data, section->header->size);
                section->done = 1;
            }
        }
    }

    /* the symbol table, and its symbols sorted by address */
    for (section = file.sections; section; section = section->next) {
        if (section->header->type != 2)
            continue;
        section->link = DumpELF_FindSection(file.sections, section->header->link);
        if (!file.symbols) {
            file.symbols = (DumpSection *)galloc(sizeof(DumpSection));
            *file.symbols = *section;
            file.symbols->next = NULL;
        } else {
            data_0049a6f4("one too many .symtab sections", section->index);
        }
        section->done = 1;
        file.firstGlobal = section->header->info + 1;
        file.symbolCount = file.symbols->header->size >> 4;
        DumpELF_SymbolTable(&file, file.symbols);
        count = file.symbols->header->size >> 4;
        sorted = (DumpSortedSymbol *)galloc(count * sizeof(DumpSortedSymbol));
        file.sorted = sorted;
        for (i = 0; i < count; i++) {
            sorted[i].value = ((DumpSymbol *)file.symbols->data)[i].value;
            sorted[i].size = ((DumpSymbol *)file.symbols->data)[i].size;
            sorted[i].index = i;
        }
        qsort(file.sorted + 1, count - 1, sizeof(DumpSortedSymbol), (DumpCompare)DumpELF_CompareAddressPairs);
    }

    /* the segment information */
    for (section = file.sections; section; section = section->next) {
        if (section->name && !strcmp(section->name, ".PPC.EMB.seginfo"))
            break;
    }
    if (section) {
        if (section->header->link)
            section->link = DumpELF_FindSection(file.sections, section->header->link);
        else
            section->link = NULL;
        if (data_0049a6ee && (data_0049a6ea & 1)) {
            for (i = 0; i < section->header->size / 12; i++)
                ELF_Endian_ConvertTwoHalvesTwoWords((char *)section->data + i * 12);
        }
        if (data_0049a6e5 && data_004a2dc4)
            DumpELF_RomCopyTable(&file, section);
        section->done = 1;
    }

    /* the dynamic symbols */
    for (section = file.sections; section; section = section->next) {
        if (section->header->type != 11)
            continue;
        section->link = DumpELF_FindSection(file.sections, section->header->link);
        if (!file.dynamicSymbols) {
            file.dynamicSymbols = (DumpSection *)galloc(sizeof(DumpSection));
            *file.dynamicSymbols = *section;
            file.dynamicSymbols->next = NULL;
        } else {
            data_0049a6f4("one too many .dynsym sections", section->index);
        }
        section->done = 1;
        file.firstDynamicGlobal = section->header->info + 1;
        file.dynamicSymbolCount = file.dynamicSymbols->header->size >> 4;
        DumpELF_SymbolTable(&file, file.dynamicSymbols);
    }

    /* the relocations: each section relocated has its relocations as its link */
    for (section = file.sections; section; section = section->next) {
        if (section->header->type == 9) {
            if (data_0049a6ee && (data_0049a6ea & 1)) {
                for (i = 0; i < section->header->size >> 3; i++)
                    ELF_Endian_ConvertTwoWords((unsigned int *)section->data + i * 2);
            }
            section->link = DumpELF_FindSection(file.sections, section->header->link);
            DumpELF_FindSection(file.sections, section->header->info)->link = section;
        }
        if (section->header->type == 4) {
            if (data_0049a6ee && (data_0049a6ea & 1)) {
                for (i = 0; i < section->header->size / 12; i++)
                    ELF_Endian_ConvertThreeWords((unsigned int *)section->data + i * 3);
            }
            section->link = DumpELF_FindSection(file.sections, section->header->link);
            DumpELF_FindSection(file.sections, section->header->info)->link = section;
        }
    }

    /* the exception tables */
    for (extab = file.sections; extab; extab = extab->next) {
        if (extab->name && !strcmp(extab->name, "extab"))
            break;
    }
    if (extab) {
        if (data_0049a6ee && (data_0049a6e7 & 1) && extab->header->size)
            DumpELF_ConvertExceptionTables(&file, extab, 0);
        if (data_0049a6e5) {
            if (extab->link) {
                if (data_004a2d72 && data_004a2d73)
                    DumpELF_DumpRelocations(&file, extab->link);
                extab->link->done = 1;
            }
            if (data_004a2d72 && data_004a2d73) {
                DumpELF_ExceptionTables(&file, extab);
                if (data_0049a6e5 && data_004a2dc2)
                    DumpELF_RawBytes(extab);
            }
        }
        extab->done = 1;
    }

    /* the exception table index */
    found = DumpELF_FindNamed(file.sections, "extabindex");
    if (found) {
        if (data_0049a6ee && (data_0049a6e7 & 1))
            ELF_Endian_ConvertWords(found->data, found->header->size);
        if (data_0049a6e5) {
            if (found->link) {
                if (data_004a2d72 && data_004a2d73)
                    DumpELF_DumpRelocations(&file, found->link);
                found->link->done = 1;
            }
            if (data_004a2d72 && data_004a2d73)
                DumpELF_HexDump(&file, found, found->header->type == 1);
        }
        found->done = 1;
    }

    /* code, data and the other sections the linker makes */
    if (data_0049a6e5) {
        for (section = file.sections; section; section = section->next) {
            if (!section->name || !section->name[0] || !strcmp(section->name, ".line") ||
                !strncmp(section->name, ".debug", 6))
                continue;
            header = section->header;
            if (header->type != 8 && (header->flags & 4)) {
                if (section->link) {
                    if (data_004a2d6e)
                        DumpELF_DumpRelocations(&file, section->link);
                    section->link->done = 1;
                    qsort(section->link->data, section->link->header->size / 12, 12, (DumpCompare)DumpELF_CompareWords);
                }
                if (data_004a2d6e)
                    DumpCode_Section(&file, section);
            } else if (header->flags & 2) {
                if (section->link) {
                    if (data_004a2d72)
                        DumpELF_DumpRelocations(&file, section->link);
                    section->link->done = 1;
                }
                if (section->header->type != 1 && section->header->type != 8)
                    continue;
                if (data_004a2d72)
                    DumpELF_HexDump(&file, section, section->header->type == 1);
            } else if (header->type == 0xca2a82c2) {
                if (section->link) {
                    if (data_004a2d6e)
                        DumpELF_DumpRelocations(&file, section->link);
                    section->link->done = 1;
                }
                if (data_004a2d6e)
                    DumpELF_Records(section);
            } else {
                continue;
            }
            section->done = 1;
        }
    }

    /* the line numbers */
    found = DumpELF_FindNamed(file.sections, ".line");
    if (found) {
        if (data_004a2d74) {
            if (data_0049a6e5) {
                if (found->link)
                    DumpELF_DumpRelocations(&file, found->link);
                if (data_004a2dc2)
                    DumpELF_RawBytes(found);
                if (data_0049a6ee && (data_0049a6e8 & 1))
                    swap_conversion_blocks(found->data, found->header->size, (void *)data_0049a6f4, 0);
                DumpELF_LineSection(found);
            } else if (data_0049a6ee && (data_0049a6e8 & 1)) {
                swap_conversion_blocks(found->data, found->header->size, (void *)data_0049a6f4, 0);
            }
        }
        found->done = 1;
        if (found->link)
            found->link->done = 1;
    }

    /* the debug information */
    found = DumpELF_FindNamed(file.sections, ".debug");
    if (found) {
        if (data_004a2d74) {
            if (data_0049a6e5) {
                if (found->link)
                    DumpELF_DumpRelocations(&file, found->link);
                if (data_004a2dc2)
                    DumpELF_RawBytes(found);
                if (data_004a2dc3 && found->link)
                    DumpCode_Relocate(&file, found, data_0049a6ee && data_0049a6e8);
                DumpDWARF(found->data, found->header->size, data_0049a6e5, data_0049a6ee && data_0049a6e8,
                          data_0049a6f0, data_0049a6f4, data_0049a6e8 == 2, (long)data_0049a6e0);
            } else if (data_0049a6ee && data_0049a6e8) {
                if (data_004a2dc3 && found->link)
                    DumpCode_Relocate(&file, found, data_0049a6ee && data_0049a6e8);
                DumpDWARF(found->data, found->header->size, data_0049a6e5, data_0049a6ee && data_0049a6e8,
                          data_0049a6f0, data_0049a6f4, data_0049a6e8 == 2, (long)data_0049a6e0);
            }
        }
        found->done = 1;
        if (found->link)
            found->link->done = 1;
    }

    /* the other debug sections, in hex */
    for (section = file.sections; section; section = section->next) {
        if (!section->name || strncmp(section->name, ".debug", 6) || section->done)
            continue;
        if (data_004a2d74) {
            if (data_0049a6e5) {
                if (section->link)
                    DumpELF_DumpRelocations(&file, section->link);
                if (data_004a2dc3 && section->link)
                    DumpCode_Relocate(&file, section, data_0049a6ee && data_0049a6e8);
                DumpELF_RawBytes(section);
            } else if (data_0049a6ee && data_0049a6e8 && data_004a2dc3 && section->link) {
                DumpCode_Relocate(&file, section, data_0049a6ee && data_0049a6e8);
            }
        }
        section->done = 1;
        if (section->link)
            section->link->done = 1;
    }

    /* the comment the compiler leaves */
    found = DumpELF_FindNamed(file.sections, ".comment");
    if (found && !strncmp(found->data, "CodeWarrior", 11) && data_0049a6e5)
        DumpELF_Comment(found);

    /* the notes */
    found = DumpELF_FindNamed(file.sections, ".note");
    if (found) {
        if (data_0049a6ee && (data_0049a6e9 & 1)) {
            for (i = 0; i < found->header->size; i += 4)
                CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)(found->data + i));
        }
        if (data_0049a6e5)
            DumpELF_Notes(found);
    }

    /* the dynamic section */
    found = DumpELF_FindNamed(file.sections, ".dynamic");
    if (found) {
        if (data_0049a6ee && (data_0049a6e9 & 1)) {
            for (i = 0; i < found->header->size; i += 4)
                *(unsigned int *)(found->data + i) = CTool_EndianConvertWord32(*(unsigned int *)(found->data + i));
        }
        if (data_0049a6e5)
            DumpELF_Dynamic(found);
    }

    /* the rest, in hex */
    if (data_0049a6e5 && data_004a2dc2) {
        for (section = file.sections; section; section = section->next) {
            if (!section->done)
                DumpELF_RawBytes(section);
        }
    }

    /* the image back in its own byte order */
    if (data_0049a6ee) {
        found = DumpELF_FindNamed(file.sections, ".PPC.EMB.seginfo");
        if (found && (data_0049a6ea & 2)) {
            for (i = 0; i < found->header->size / 12; i++)
                ELF_Endian_ConvertTwoHalvesTwoWords(found->data + i * 12);
        }
        if (data_0049a6ea & 2) {
            for (section = file.sections; section; section = section->next) {
                if (section->header->type == 9) {
                    for (i = 0; i < section->header->size >> 3; i++)
                        ELF_Endian_ConvertTwoWords((unsigned int *)section->data + i * 2);
                }
                if (section->header->type == 4) {
                    for (i = 0; i < section->header->size / 12; i++)
                        ELF_Endian_ConvertThreeWords((unsigned int *)section->data + i * 3);
                }
            }
        }
        found = DumpELF_FindNamed(file.sections, "extabindex");
        if (found && (data_0049a6e7 & 2))
            ELF_Endian_ConvertWords(found->data, found->header->size);
        found = DumpELF_FindNamed(file.sections, "extab");
        if (found && (data_0049a6e7 & 2) && found->header->size)
            DumpELF_ConvertExceptionTables(&file, found, 1);
        found = DumpELF_FindNamed(file.sections, ".line");
        if (found && data_004a2d74 && (data_0049a6e8 & 2))
            swap_conversion_blocks(found->data, found->header->size, (void *)data_0049a6f4, 1);
        found = DumpELF_FindNamed(file.sections, ".note");
        if (found && (data_0049a6e9 & 2)) {
            for (i = 0; i < found->header->size; i += 4)
                CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)(found->data + i));
        }
        if (data_0049a6eb & 2) {
            for (index = 1; index < file.header->section_header_count; index++)
                ELF_Endian_SwapElf32Section((Elf32Section *)(image + file.header->section_header_offset) + index);
        }
        if (data_0049a6ed & 2)
            ELF_Endian_SwapElf32Header(file.header);
    }
}

/* ELF_Endian.c's: the procedure records' byte order */
extern void ELF_Endian_ConvertRecord1(void *record);
extern void ELF_Endian_ConvertRecord2(void *record);
extern void ELF_Endian_ConvertRecord3(void *record);

#pragma options align = mac68k

/* The procedure records of a section (type 1, 2 or 3), each word aligned after the one before. */
typedef struct DumpRecord1 {
    unsigned char type;
    unsigned char x1;
    unsigned short x2;
    unsigned long size;
    unsigned long address;
    unsigned long exitCount;
    unsigned long nonInstrumentableCount;
    unsigned long exits[1];        /* then the non-instrumentable procedures' offsets */
} DumpRecord1;

typedef struct DumpRecord2 {
    unsigned char type;
    unsigned char exitCount;
    unsigned short size;
    unsigned long address;
    unsigned short exits[1];
} DumpRecord2;

typedef struct DumpRecord3 {
    unsigned char type;
    unsigned char x1;
    unsigned short exitCount;
    unsigned long size;
    unsigned long address;
    unsigned long exits[1];
} DumpRecord3;

/* A .dynamic entry. */
typedef struct DumpDynamic {
    unsigned long tag;
    unsigned long value;
} DumpDynamic;
#pragma options align = reset

/* Whether the records' values are converted to the host's byte order. */
#define DUMP_SWAP() (data_0049a6ee && (data_0049a6e6 & 1))

/* The default printer, when DumpELF has none: to stderr. */
/* 0x435300 */
void DumpELF_PrintDefault(char *text, long length, long size) {
    if (length > size) {
        DUMP_ASSERT(0x9b3);
        length = size;
    }
    fprintf(stderr, "In AppendText2: shouldn't be here!\r\n");
    fprintf(stderr, "%.*s", length, text);
    fflush(stderr);
}

/* Orders two {address, x} pairs by address; at the same address, one whose second word is 0 goes first when the
   other's is not, as the original's comparison has it. */
/* 0x435290 */
int DumpELF_CompareAddressPairs(const unsigned long *a, const unsigned long *b) {
    if (a[0] == b[0]) {
        if (a[1] == 0)
            return 0;
        if (b[1] == 0)
            return 1;
    }
    if (a[0] < b[0])
        return -1;
    if (a[0] > b[0])
        return 1;
    return 0;
}

/* Orders two words. */
/* 0x435260 */
int DumpELF_CompareWords(const unsigned long *a, const unsigned long *b) {
    if (*a < *b)
        return -1;
    if (*a > *b)
        return 1;
    return 0;
}

/* A .dynamic tag's name. */
/* 0x4351b0 */
char *DumpELF_DynamicTagName(unsigned long tag) {
    switch (tag) {
    case 0: return "DT_NULL";
    case 1: return "DT_NEEDED";
    case 2: return "DT_PLTRELSZ";
    case 3: return "DT_PLTGOT";
    case 4: return "DT_HASH";
    case 5: return "DT_STRTAB";
    case 6: return "DT_SYMTAB";
    case 7: return "DT_RELA";
    case 8: return "DT_RELASZ";
    case 9: return "DT_RELAENT";
    case 10: return "DT_STRSZ";
    case 11: return "DT_SYMENT";
    case 12: return "DT_INIT";
    case 13: return "DT_FINI";
    case 14: return "DT_SONAME";
    case 15: return "DT_RPATH";
    case 16: return "DT_SYMBOLIC";
    case 17: return "DT_REL";
    case 18: return "DT_RELSZ";
    case 19: return "DT_RELENT";
    case 20: return "DT_PLTREL";
    case 21: return "DT_DEBUG";
    case 22: return "DT_TEXTREL";
    case 23: return "DT_JMPREL";
    default: return "<unknown>";
    }
}

/* How a .dynamic entry's value is used: 0 ignored, 1 a value, 2 an address. */
/* 0x435140 */
unsigned char DumpELF_DynamicValueKind(DumpDynamic *entry) {
    switch (entry->tag) {
    case 0: return 0;
    case 1: case 2: return 1;
    case 3: case 4: case 5: case 6: case 7: return 2;
    case 8: case 9: case 10: case 11: return 1;
    case 12: case 13: return 2;
    case 14: case 15: return 1;
    case 16: return 0;
    case 17: return 2;
    case 18: case 19: case 20: return 1;
    case 21: return 2;
    case 22: return 0;
    case 23: return 2;
    default: return 0;
    }
}

/* Prints a .dynamic entry. */
/* 0x4350b0 */
void DumpELF_DynamicEntry(DumpDynamic *entry, int index) {
    char text[0xc00];

    if (DumpELF_DynamicValueKind(entry))
        DUMP_PRINT(text, (text, "    [% 4i] %-12s %08x\r\n", index, DumpELF_DynamicTagName(entry->tag), entry->value));
    else
        DUMP_PRINT(text, (text, "    [% 4i] %-12s <ignored>\r\n", index, DumpELF_DynamicTagName(entry->tag)));
}

/* Prints a .dynamic section. */
/* 0x435020 */
void DumpELF_Dynamic(DumpSection *section) {
    char text[0xc00];
    DumpDynamic *entry;
    unsigned long i;

    DUMP_PRINT(text, (text, "\r\n==> %s\r\n", section->name));
    DUMP_PRINT(text, (text, "    entry  d_tag        d_un\r\n"));
    entry = (DumpDynamic *)section->data;
    for (i = 0; i < section->header->size >> 3; i++) {
        DumpELF_DynamicEntry(entry, i);
        entry++;
    }
}

/* Prints a type 1 procedure record, its exits and its non-instrumentable procedures; whether it printed any. */
/* 0x434e60 */
char DumpELF_Record1(DumpRecord1 *record, char header, int index, unsigned long offset) {
    char text[0xc00];
    unsigned long *at;
    unsigned long i;
    char swap;

    swap = 0;
    if (DUMP_SWAP())
        swap = 1;
    if (swap)
        ELF_Endian_ConvertRecord1(record);
#if LINKER == 13
    /* (1.3.2: no entry and offset columns) */
    if (header)
        DUMP_PRINT(text, (text, "    type size     addr     procexits noninstrumentables\r\n"));
    DUMP_PRINT(text, (text, "    %4i %08x %08x %9i %18i\r\n", record->type, record->size, record->address,
                      record->exitCount, record->nonInstrumentableCount));
#else
    if (header)
        DUMP_PRINT(text, (text, "    entry   offset    type size     addr     procexits noninstrumentables\r\n"));
    DUMP_PRINT(text, (text, "    [% 4i]  %08x  %4i %08x %08x %9i %18i\r\n", index, offset, record->type, record->size,
                      record->address, record->exitCount, record->nonInstrumentableCount));
#endif
    at = record->exits;
    if (record->exitCount) {
        DUMP_PRINT(text, (text, "    non-standand exit offsets\r\n"));
        for (i = 0; i < record->exitCount; i++) {
            if (swap)
                at[i] = CTool_EndianConvertWord32(at[i]);
            DUMP_PRINT(text, (text, "    %08x\r\n", at[i]));
        }
    }
    /* (the original moves past the exits by 16 bytes for each) */
    at += record->exitCount * 4;
    if (record->nonInstrumentableCount) {
        DUMP_PRINT(text, (text, "    non-instrumentable procedure offsets\r\n"));
        for (i = 0; i < record->nonInstrumentableCount; i++) {
            if (swap)
                at[i] = CTool_EndianConvertWord32(at[i]);
            DUMP_PRINT(text, (text, "    %08x\r\n", at[i]));
        }
    }
    return record->exitCount || record->nonInstrumentableCount;
}

/* Prints a type 2 procedure record and its exits; whether it has any. */
/* 0x434d10 */
char DumpELF_Record2(DumpRecord2 *record, char header, int index, unsigned long offset) {
    char text[0xc00];
    int i;
    char swap;

    swap = 0;
    if (DUMP_SWAP())
        swap = 1;
    if (swap)
        ELF_Endian_ConvertRecord2(record);
#if LINKER == 13
    if (header)
        DUMP_PRINT(text, (text, "    type size     addr     procexits\r\n"));
    DUMP_PRINT(text, (text, "    %4i %04x     %08x %9i\r\n", record->type, record->size, record->address,
                      record->exitCount));
#else
    if (header)
        DUMP_PRINT(text, (text, "    entry   offset    type size     addr     procexits\r\n"));
    DUMP_PRINT(text, (text, "    [% 4i]  %08x  %4i %04x     %08x %9i\r\n", index, offset, record->type, record->size,
                      record->address, record->exitCount));
#endif
    if (record->exitCount) {
        DUMP_PRINT(text, (text, "    non-standand exit offsets\r\n"));
        for (i = 0; i < record->exitCount; i++) {
            if (swap)
                record->exits[i] = CTool_EndianConvertWord16(record->exits[i]);
            DUMP_PRINT(text, (text, "    %04x\r\n", record->exits[i]));
        }
    }
    return record->exitCount != 0;
}

/* Prints a type 3 procedure record and its exits; whether it has any. */
/* 0x434bd0 */
char DumpELF_Record3(DumpRecord3 *record, char header, int index, unsigned long offset) {
    char text[0xc00];
    int i;
    char swap;

    swap = 0;
    if (DUMP_SWAP())
        swap = 1;
    if (swap)
        ELF_Endian_ConvertRecord3(record);
#if LINKER == 13
    if (header)
        DUMP_PRINT(text, (text, "    type size     addr     procexits\r\n"));
    DUMP_PRINT(text, (text, "    %4i %08x %08x %9i\r\n", record->type, record->size, record->address,
                      record->exitCount));
#else
    if (header)
        DUMP_PRINT(text, (text, "    entry   offset    type size     addr     procexits\r\n"));
    DUMP_PRINT(text, (text, "    [% 4i]  %08x  %4i %08x %08x %9i\r\n", index, offset, record->type, record->size,
                      record->address, record->exitCount));
#endif
    if (record->exitCount) {
        DUMP_PRINT(text, (text, "    non-standand exit offsets\r\n"));
        for (i = 0; i < record->exitCount; i++) {
            if (swap)
                record->exits[i] = CTool_EndianConvertWord32(record->exits[i]);
            DUMP_PRINT(text, (text, "    %08x\r\n", record->exits[i]));
        }
    }
    return record->exitCount != 0;
}

/* Prints a section of procedure records: the column titles again after a record with exits or after a record of
   the other layout (1 against 2 and 3). A record of another type is not passed (the original does not move on). */
/* 0x434a70 */
void DumpELF_Records(DumpSection *section) {
    char text[0xc00];
    char afterLong;   /* the record before was a type 1 */
    char afterShort;  /* the record before was a type 2 or 3 */
    char header;
    unsigned char *record;
    unsigned long offset;
    int index;

    header = 1;
    afterLong = 0;
    afterShort = 0;
    index = 0;
    DUMP_PRINT(text, (text, "\r\n==> %s\r\n", section->name));
    offset = 0;
    record = (unsigned char *)section->data;
    while (offset < section->header->size) {
        switch (*record) {
        case 1:
            header = DumpELF_Record1((DumpRecord1 *)(section->data + offset), (char)(afterShort + header), index,
                                     offset);
            offset += 0x14 + ((DumpRecord1 *)record)->exitCount * 4 +
                      ((DumpRecord1 *)record)->nonInstrumentableCount * 4;
            afterLong = 1;
            afterShort = 0;
            break;
        case 2:
            header = DumpELF_Record2((DumpRecord2 *)(section->data + offset), (char)(afterLong + header), index,
                                     offset);
            offset += 8 + record[1] * 2;
            afterLong = 0;
            afterShort = 1;
            break;
        case 3:
            header = DumpELF_Record3((DumpRecord3 *)(section->data + offset), (char)(afterLong + header), index,
                                     offset);
            offset += 0xc + ((DumpRecord3 *)record)->exitCount * 4;
            afterLong = 0;
            afterShort = 1;
            break;
        }
        offset = (offset + 3) & ~3;
        index++;
        record = (unsigned char *)section->data + offset;
    }
}
