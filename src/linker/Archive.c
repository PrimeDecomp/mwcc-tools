/* The archives of -library: the linker writes its objects as a Unix archive ("!<arch>"), with a symbol table (the
   "/" member: the count, each symbol's member offset, big-endian, then the names) and the long names (the "//"
   member) of the objects whose names do not fit a member header, and reads the members' names of the archives it
   links.

   No assertion names this file: Archive.c is this repository's name for it. */
#include "compiler/common.h"
#include "compiler/CompilerTools.h"
#include "compiler/ELF_Endian.h"
#include "driver/COSToolsCLT.h"
#include "driver/MsDos.h"
#include "driver/libimp-eabi-ppc.h"

extern int sprintf(char *s, const char *format, ...);
extern int sscanf(const char *s, const char *format, ...);
extern Boolean fn_00420350(void);
extern void CompilerTools_Abort(void);
extern void fn_0041fc60(void *entry);
extern unsigned long strlen(const char *s);
#if LINKER >= 27
extern unsigned long time(unsigned long *timer);
#endif

/* ELF_gen.c's output: bytes, a byte and a string with its terminator, each returning the bytes written. */
extern unsigned long ELF_gen_Write(const void *data, unsigned long size);
extern long ELF_gen_WriteByte(char byte);
extern unsigned long ELF_gen_WriteString(const char *string);

#pragma options align = mac68k
/* An input section of an object: its header and its contents. */
typedef struct ArchiveSection {
    long x0;
    Elf32Section *header;
    char *data;
} ArchiveSection;

typedef struct ArchiveFileName {
    long x0;
    char *name;
} ArchiveFileName;

/* An object the archive is made of (0x34 bytes). */
typedef struct ArchiveFile {
    long longName;               /* its name's offset in the long names, or -1 */
    struct Elf32Header *image;   /* the object's bytes */
    long x8;
    ArchiveFileName *file;
    unsigned long size;
    long x14;
    ArchiveSection *symbols;     /* .symtab */
    long x1c;
    ArchiveSection *strings;     /* .strtab */
    long x24;
    char *sections;              /* the section list, 0x3a bytes each, the header's pointer at +4 */
    long x2c;
    long x30;
} ArchiveFile;
#pragma options align = reset

/* A member header (60 bytes). */
typedef struct ArchiveHeader {
    char name[16];
    char date[12];
    char uid[6];
    char gid[6];
    char mode[8];
    char size[10];
    char end[2];
} ArchiveHeader;

/* A global symbol of the symbol table: the next, the offset of its object's member, its name. */
typedef struct ArchiveSymbol {
    struct ArchiveSymbol *next;
    unsigned long offset;
    char *name;
} ArchiveSymbol;

extern ArchiveFile *data_004a28dc;
extern long data_004a292c;
extern char data_004a2e4e;
extern GList data_0049dbe8;
extern void **data_004a2764;

/* Whether an object's long name is written as "/offset" (else it is cut to 15 characters). */
char data_0048b7a8 = 1;

void Archive_WriteSymbolTable(unsigned long *position);
void Archive_WriteLongNames(unsigned long *position);
void Archive_CollectSymbols(void);
unsigned long Archive_WriteSymbols(unsigned long position);

static unsigned long symbolNameBytes;
static long symbolCount;
static ArchiveSymbol *lastSymbol;
static ArchiveSymbol *symbols;

/* The header's fields other than the name and the size: zero date (2.7: the time), owner, group and mode. */
static inline void FillHeader(ArchiveHeader *header) {
    unsigned int i;

#if LINKER >= 27
    for (i = sprintf(header->date, "%lu", time(NULL)); i < sizeof(header->date); i++)
        header->date[i] = ' ';
#else
    header->date[0] = '0';
    for (i = 1; i < sizeof(header->date); i++)
        header->date[i] = ' ';
#endif
    header->uid[0] = '0';
    for (i = 1; i < sizeof(header->uid); i++)
        header->uid[i] = ' ';
    header->gid[0] = '0';
    for (i = 1; i < sizeof(header->gid); i++)
        header->gid[i] = ' ';
    header->mode[0] = '0';
    for (i = 1; i < sizeof(header->mode); i++)
        header->mode[i] = ' ';
    header->end[0] = '`';
    header->end[1] = '\n';
}

/* The size field: the number, padded with spaces. */
static inline void FillSize(ArchiveHeader *header, unsigned long size) {
    unsigned int i;

    for (i = sprintf(header->size, "%ld", size); i < sizeof(header->size); i++)
        header->size[i] = ' ';
}

static inline unsigned long WriteBigEndian(unsigned long value) {
    unsigned char bytes[4];

    bytes[0] = value >> 24;
    bytes[1] = value >> 16;
    bytes[2] = value >> 8;
    bytes[3] = value;
    return ELF_gen_Write(bytes, 4);
}

/* Writes the archive: its objects, after the symbol table and the long names. */
/* 0x00453090 */
void Archive_Write(void) {
    unsigned long position;
    ArchiveHeader header;
    ArchiveFile *file;
    char *name;
    unsigned int length;
    int i;
    int j;

    position = ELF_gen_Write("!<arch>\n", 8);
    if (InitGList(&data_0049dbe8, 100))
        CompilerTools_FormatMessageAndLongjmp(0xd, -108);
    for (i = 0; i < data_004a292c; i++) {
        data_004a28dc[i].longName = -1;
        name = data_004a28dc[i].file->name;
        length = strlen(name);
        if (length > 15) {
            data_004a28dc[i].longName = data_0049dbe8.size;
            AppendGListData(&data_0049dbe8, data_004a28dc[i].file->name, length);
            AppendGListData(&data_0049dbe8, "/\n", 2);
        }
    }
    Archive_WriteSymbolTable(&position);
    if (data_0049dbe8.size)
        Archive_WriteLongNames(&position);
    for (i = 0; i < data_004a292c; i++) {
        if (position & 1)
            position += ELF_gen_WriteByte('\n');
        if (data_004a28dc[i].longName != -1 && data_0048b7a8) {
            length = sprintf(header.name, "/%ld", data_004a28dc[i].longName);
        } else {
            for (length = 0; data_004a28dc[i].file->name[length] && length < 16; length++)
                header.name[length] = data_004a28dc[i].file->name[length];
            if (length < 16)
                header.name[length++] = '/';
            else
                header.name[length - 1] = '/';
        }
        for (; length < 16; length++)
            header.name[length] = ' ';
        FillHeader(&header);
        if (fn_00420350())
            CompilerTools_Abort();
        file = &data_004a28dc[i];
        FillSize(&header, file->size);
        if (data_004a2e4e) {
            for (j = 1; j < data_004a28dc[i].image->section_header_count; j++)
                ELF_Endian_SwapElf32Section(*(Elf32Section **)(data_004a28dc[i].sections + j * 0x3a + 4));
            ELF_Endian_SwapElf32Header(file->image);
        }
        position += ELF_gen_Write(&header, sizeof(header));
        position += ELF_gen_Write(file->image, file->size);
    }
    for (name = (char *)data_004a2764; name; name = *(char **)name)
        fn_0041fc60(name);
}

/* Writes the symbol table member, when an object defines a global symbol. */
/* 0x00452f20 */
void Archive_WriteSymbolTable(unsigned long *position) {
    ArchiveHeader header;
    unsigned int i;

    Archive_CollectSymbols();
    if (!symbols)
        return;
    header.name[0] = '/';
    for (i = 1; i < 16; i++)
        header.name[i] = ' ';
    FillHeader(&header);
    if (fn_00420350())
        CompilerTools_Abort();
    FillSize(&header, symbolNameBytes + 4);
    *position += ELF_gen_Write(&header, sizeof(header));
    *position += Archive_WriteSymbols(*position);
}

/* Writes the long names member. */
/* 0x00452d80 */
void Archive_WriteLongNames(unsigned long *position) {
    ArchiveHeader header;
    unsigned int i;

    if (*position & 1)
        *position += ELF_gen_WriteByte('\n');
    header.name[0] = '/';
    header.name[1] = '/';
    for (i = 2; i < 16; i++)
        header.name[i] = ' ';
    FillHeader(&header);
    if (fn_00420350())
        CompilerTools_Abort();
    FillSize(&header, data_0049dbe8.size);
    *position += ELF_gen_Write(&header, sizeof(header));
    COS_LockHandle(data_0049dbe8.data);
    *position += ELF_gen_Write(*data_0049dbe8.data, data_0049dbe8.size);
    COS_UnlockHandle(data_0049dbe8.data);
}

/* The global symbols the objects define (their binding global or weak), each with the offset of its object's member
   from the first object's, and the size of the table's offsets and names. */
/* 0x00452bc0 */
void Archive_CollectSymbols(void) {
    unsigned long member;
    unsigned long count;
    unsigned long j;
    int i;
    struct ElfSymbolRecord symbol;
    ArchiveSymbol *entry;
    char *name;

    symbols = 0;
    symbolNameBytes = 0;
    symbolCount = 0;
    member = 0;
    for (i = 0; i < data_004a292c; i++) {
        if (member & 1)
            member++;
        if (fn_00420350())
            CompilerTools_Abort();
        count = data_004a28dc[i].symbols->header->size >> 4;
        for (j = 1; j < count; j++) {
            symbol = ((struct ElfSymbolRecord *)data_004a28dc[i].symbols->data)[j];
            if (data_004a2e4e)
                ELF_Endian_ConvertSymbolRecord(&symbol);
            if (symbol.sectionIndex != 0 && (unsigned char)((symbol.info >> 4) - 1) < 2) {
                name = data_004a28dc[i].strings->data + symbol.name;
                entry = galloc(sizeof(ArchiveSymbol));
                entry->next = 0;
                entry->offset = 0;
                entry->name = 0;
                entry->name = name;
                entry->offset = member;
                if (symbols)
                    lastSymbol->next = entry;
                else
                    symbols = entry;
                lastSymbol = entry;
                symbolCount++;
                symbolNameBytes += strlen(name) + 5;
            }
        }
        member += 0x3c + data_004a28dc[i].size;
    }
}

/* The symbol table's contents, at POSITION: the count, the members' offsets (made absolute: the objects follow the
   table and the long names) and the names. */
/* 0x00452ad0 */
unsigned long Archive_WriteSymbols(unsigned long position) {
    unsigned long base;
    unsigned long written;
    ArchiveSymbol *entry;

    base = position + 4 + symbolNameBytes;
    if (symbolNameBytes & 1)
        base++;
    if (data_0049dbe8.size)
        base += data_0049dbe8.size + 0x3c;
    written = WriteBigEndian(symbolCount);
    for (entry = symbols; entry; entry = entry->next) {
        entry->offset += base;
        written += WriteBigEndian(entry->offset);
    }
    for (entry = symbols; entry; entry = entry->next)
        written += ELF_gen_WriteString(entry->name);
    return written;
}

/* A member's name, from its header's NAME: "/" and "//" (with no long names yet) are themselves, "/offset" is a long
   name, and other names end at their last '/' in the first 16 characters (cut to 15 without one). */
/* 0x00452a00 */
char *Archive_MemberName(char **longNames, char *name) {
    long offset = 0;
    char *table = 0;

    if (longNames)
        table = *longNames;
    if (!table && name[0] == '/') {
        offset = 1;
        if (name[1] == '/')
            offset = 2;
        if (name[offset] == ' ' || name[offset] == '\0') {
            name[offset] = '\0';
            return name;
        }
    }
    if (name[0] == '/' && table && (__ctype_map[(unsigned char)name[1]] & 0x10)) {
        sscanf(name + 1, "%ld", &offset);
        return table + offset;
    }
    offset = 15;
    while (name[offset] != '/' && name[offset] != '\0') {
        offset--;
        if (offset == 0) {
            name[15] = '\0';
            return name;
        }
    }
    name[offset] = '\0';
    return name;
}

/* Ends each of the long names ("name/\n") at its '/'. */
/* 0x004529d0 */
void Archive_EndLongNames(char **longNames, long size) {
    char *table = *longNames;

    while (--size > 1) {
        if (table[size] == '\n' && table[size - 1] == '/')
            table[size - 1] = '\0';
    }
}
