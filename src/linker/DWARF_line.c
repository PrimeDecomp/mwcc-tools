/* The DWARF 1 of the linked output. With -g and dead stripping, the .debug entries of each object's dead functions and
   variables become padding to their siblings and their lines are removed from .line (DWARF_line_StripDeadCode, as
   the objects are read); then the entries ("<.dwarf.tag>" pieces) and line tables (".line.<function>" pieces) of the
   dropped code are removed from the output's .debug and .line, the kept ones moved back with their relocations
   (DWARF_line_Compact).

   The assertions name this file "DWARF_line.c". */
#include "compiler/common.h"
#include "compiler/CompilerTools.h"

extern void CompilerTools_FormatMessageAndLongjmp(int message, int status, ...);
extern void *memmove(void *dst, const void *src, unsigned long n);
extern void *memcpy(void *dst, const void *src, unsigned long n);
extern void *memset(void *dst, int c, unsigned long n);
extern Boolean fn_00420350(void);
extern void CompilerTools_Abort(void);
extern void CompilerTools_ReportLimitedDiagnostic(int message, ...);
extern void CompilerTools_ReportMessage(int message, ...);
extern int strncmp(const char *a, const char *b, unsigned long n);
extern char *strncpy(char *dst, const char *src, unsigned long n);
extern int sscanf(const char *s, const char *format, ...);

typedef void (*DumpDWARFPrinter)(char *text, long length, long size);
typedef void (*DumpDWARFAsserter)(const char *file, int line);
extern void DumpDWARF(char *data, unsigned long dataSize, char print, char convert, DumpDWARFPrinter output,
                      DumpDWARFAsserter assertion, char readFirst, long errorContext);

#pragma options align = mac68k
typedef struct LineSectionHeader {
    unsigned long name;
    unsigned long type;
    unsigned long flags;
    unsigned long address;
    unsigned long offset;
    unsigned long size;
} LineSectionHeader;

/* An ELF relocation with an addend. */
typedef struct LineRelocation {
    unsigned long offset;
    unsigned long info; /* the symbol's index << 8, the type */
    long addend;
} LineRelocation;

/* A symbol of an object as the linker keeps it (16 bytes). */
typedef struct LineSymbol {
    struct LinePiece *piece;
    unsigned long value;
    unsigned long size;
    unsigned char info;
    unsigned char flags; /* 0x10: kept, 0x20: dropped */
    unsigned short section;
} LineSymbol;

/* A word to move with the data of a piece. */
typedef struct LineFixup {
    struct LineFixup *next;
    long *at;
} LineFixup;

/* A piece of a section (a function, a variable, a unit's lines). */
typedef struct LinePiece {
    long x0;
    char *name;
    long x8;
    LineSymbol *symbol;
    unsigned long offset;
    long x14;
    long x18;
    struct LinePiece *next;
    char x20[0xc];
    struct LinePiece *function; /* +0x2c, a line table's function */
    char x30[8];
    LineFixup *fixups;
    char x3c[0xe];
    struct LineChunk *chunk;
} LinePiece;

/* The pieces of a section from one compilation unit. */
typedef struct LineChunk {
    struct LineChunk *next;
    LinePiece *pieces;
    unsigned long size;
    unsigned long x0c;
    unsigned long x10;
    char x14[8];
    char x1c;
    char x1d;
    char x1e;
} LineChunk;

typedef struct LineSection {
    long x0;
    LineSectionHeader *header;
    char *data;
    struct LineSection *relocations;
    long x10;
    long file; /* its object's index in data_004a28dc */
    char x18[0x14];
    LineChunk *chunks;
    char x30[5];
    char x35;
    char x36;
} LineSection;

typedef struct LineFileName {
    long x0;
    long x4;
    long context;
} LineFileName;

/* An object of the link (0x34 bytes). */
typedef struct LineFile {
    long x0;
    long x4;
    LineFileName *x8;
    char xc[0xc];
    LineSection *symbols;
    long x1c;
    long x20;
    LineSection *debug;
    long x28;
    long x2c;
    char x30;
    char x31;
    char x32;
    char x33;
} LineFile;

typedef struct LineSectionLink {
    struct LineSectionLink *next;
    LineSection *section;
} LineSectionLink;

typedef struct LineSegment {
    struct LineSegment *next;
    LineSectionLink *sections;
    char x8[0x10];
    char kind;
} LineSegment;

typedef struct LineLinker {
    long x0;
    long x4;
    long x8;
    char xc[0x14];
    LineSegment *segments;
} LineLinker;

/* A .debug entry of a compilation unit, a function or a variable (0x3e bytes). */
typedef struct LineEntry {
    struct LineEntry *next;
    short tag;
    long dropped;             /* for a unit: the bytes of lines removed */
    unsigned long size;
    unsigned long base;       /* for a unit: the address of its lines */
    char *entry;
    LineRelocation *previous; /* the sibling of the entry before, to point past a dropped entry */
    LineRelocation *sibling;
    LineRelocation *lowPC;    /* or the variable's address */
    LineRelocation *highPC;
    LineRelocation *stmtList;
    LineRelocation *lineBase; /* the relocation of its lines' address in .line */
    char *lines;
    long *lineHeader;         /* where its lines' length is written */
    long lineSize;
    char *linesEnd;
} LineEntry;

typedef struct LineRecord {
    long line;
    short column;
    long address;
} LineRecord;

/* The entries of a compile unit (or a line table's header) being moved. */
typedef struct LineBlock {
    LinePiece *piece;
    long offset;
    long removed;
    char kept;
} LineBlock;

typedef struct LineHeader {
    long length;
    long address;
} LineHeader;
#pragma options align = reset

extern LineFile *data_004a28dc;
extern char data_004a2e37;
extern char data_0049bed6[];
extern LineLinker *ELF_PPC_EABI_FindNamedOutput(const char *name);

/* the next relocation of a .line section to look at */
static int data_0049acc0;

#define ASSERT(cond, line)                                                                                             \
    if (!(cond))                                                                                                       \
    CompilerTools_FormatMessageAndLongjmp(0x33, 2, "DWARF_line.c", line)

#define FAIL(line) CompilerTools_FormatMessageAndLongjmp(0x33, 2, "DWARF_line.c", line)

#define SYMBOLS(file) ((LineSymbol *)data_004a28dc[file].symbols->data)

/* The relocation at an offset, searched from the last one found. */
#define FIND_RELOCATION(result, section, at)                                                                           \
    do {                                                                                                               \
        LineRelocation *found_;                                                                                        \
        for (;;) {                                                                                                     \
            if ((unsigned long)data_0049acc0 >= (section)->header->size / sizeof(LineRelocation)) {                    \
                CompilerTools_FormatMessageAndLongjmp(0x33, 2, "DWARF_line.c", 0x42);                                  \
                found_ = NULL;                                                                                         \
                break;                                                                                                 \
            }                                                                                                          \
            found_ = (LineRelocation *)(section)->data + data_0049acc0;                                                \
            if (found_->offset == (unsigned long)(at))                                                                 \
                break;                                                                                                 \
            data_0049acc0++;                                                                                           \
        }                                                                                                              \
        (result) = found_;                                                                                             \
    } while (0)

/* Moves the words of a chunk's pieces that point into it by the bytes it moves. */
#define MOVE_FIXUPS(c, shift)                                                                                      \
    {                                                                                                                  \
        LinePiece *piece_;                                                                                             \
        LineFixup *fixup_;                                                                                             \
        for (piece_ = (c)->pieces; piece_ && piece_->chunk == (c); piece_ = piece_->next)                          \
            for (fixup_ = piece_->fixups; fixup_; fixup_ = fixup_->next)                                               \
                *fixup_->at -= (shift);                                                                                \
    }

/* A dropped entry becomes padding to its sibling, and the sibling before it points past it. */
#define DROP_ENTRY(e)                                                                                                  \
    {                                                                                                                  \
        LineRelocation *sibling_ = (e)->sibling;                                                                       \
        *(UInt16 *)((e)->entry + 4) = CTool_EndianConvertWord16(0);                                                    \
        *(unsigned long *)(e)->entry = CTool_EndianConvertWord32(                                                      \
            debugBase + SYMBOLS(file)[sibling_->info >> 8].value + sibling_->addend - (e)->entry);                    \
        if ((e)->previous)                                                                                             \
            (e)->previous->addend = (e)->sibling->addend;                                                              \
    }

/* A unit's lines moved: its statement list, its symbol and its address relocation move with them. */
#define MOVE_UNIT(e, moved)                                                                                            \
    {                                                                                                                  \
        LineRelocation *list_ = (e)->stmtList;                                                                         \
        LineSymbol *symbol_ = &SYMBOLS(file)[list_->info >> 8];                                                        \
        if (list_->addend)                                                                                             \
            list_->addend -= (moved) - shifted;                                                                        \
        if (symbol_->value)                                                                                            \
            symbol_->value -= (moved);                                                                                 \
        (e)->lineBase->offset -= (moved);                                                                              \
    }

LineEntry *DWARF_line_ReadEntry(LineSection *relocations, char *entry, char *base, unsigned long size,
                                LineRelocation **sibling);
void DWARF_line_Assert(const char *file, int line);

/* Reads a .debug entry: its tag and the relocations of its sibling, address, high address and lines. The entries
   other than units, functions and variables are only read up to their sibling, and NULL is returned for them. */
/* 0x458090 */
LineEntry *DWARF_line_ReadEntry(LineSection *relocations, char *entry, char *base, unsigned long size,
                                LineRelocation **sibling) {
    LineRelocation *location = NULL;
    LineRelocation *highPC = NULL;
    LineRelocation *stmtList = NULL;
    char *p;
    char *end;
    unsigned short tag;
    unsigned short attribute;
    char skip;
    short length;
    unsigned char flags;
    LineEntry *result;

    *sibling = NULL;
    end = entry + size;
    tag = CTool_EndianConvertWord16(*(UInt16 *)(entry + 4));
    p = entry + 6;
    switch (tag) {
    case 6:
    case 7:
    case 0xc:
    case 0x11:
    case 0x14:
        skip = 0;
        break;
    default:
        skip = 1;
        break;
    }

    while (p < end) {
        attribute = CTool_EndianConvertWord16(*(UInt16 *)p);
        p += 2;
        switch (attribute) {
        case 0x12: /* sibling */
            FIND_RELOCATION(*sibling, relocations, p - base);
            p += 4;
            if (skip)
                return NULL;
            break;
        case 0x23: /* location */
            if (tag == 7 || tag == 0xc) {
                if (p[2] != 3)
                    return NULL;
                FIND_RELOCATION(location, relocations, p + 3 - base);
            }
            p += CTool_EndianConvertWord16(*(UInt16 *)p) + 2;
            break;
        case 0x38:
        case 0x2008:
            while (*p++)
                ;
            break;
        case 0x72:
            p += 4;
            break;
        case 0x83:
            length = CTool_EndianConvertWord16(*(UInt16 *)p) + 2;
            if ((unsigned long)(long)length > 4)
                length -= 4;
            p += length;
            p += 4;
            break;
        case 0x106: /* statement list */
            FIND_RELOCATION(stmtList, relocations, p - base);
            p += 4;
            break;
        case 0x111: /* low pc */
            FIND_RELOCATION(location, relocations, p - base);
            p += 4;
            break;
        case 0x121: /* high pc */
            FIND_RELOCATION(highPC, relocations, p - base);
            p += 4;
            break;
        default:
            switch (attribute & 0xf) {
            case 1:
            case 2:
                p += 4;
                break;
            case 3:
                p += CTool_EndianConvertWord16(*(UInt16 *)p) + 2;
                break;
            case 4:
                p += CTool_EndianConvertWord32(*(unsigned int *)p) + 4;
                break;
            case 5:
                p += 2;
                break;
            case 6:
                p += 4;
                break;
            case 7:
                p += 8;
                break;
            case 8:
                while (*p++)
                    ;
                break;
            }
            break;
        }
    }

    ASSERT(*sibling, 0xeb);
    if (tag == 0x11)
        *sibling = NULL;
    result = aalloc(sizeof(LineEntry));
    memset(result, 0, sizeof(LineEntry));
    result->size = size;
    result->tag = tag;
    result->entry = entry;
    result->stmtList = stmtList;
    result->sibling = *sibling;
    result->lowPC = location;
    if (!location) {
        CompilerTools_FormatMessageAndLongjmp(0x33, 2, "DWARF_line.c", 0x101);
    } else if (tag != 0x11) {
        flags = SYMBOLS(relocations->file)[location->info >> 8].flags;
        if (!(flags & 0x10) || (flags & 0x20)) {
            location->info &= 0xffffff00;
            result->dropped = 1;
        } else {
            result->dropped = 0;
        }
    }
    result->highPC = highPC;
    return result;
}

/* DumpDWARF's assertions while it converts an object's .debug. */
/* 0x458070 */
void DWARF_line_Assert(const char *file, int line) {
    CompilerTools_ReportLimitedDiagnostic(0x33, file, line);
    CompilerTools_FormatMessageAndLongjmp(0xd, 2);
}

/* For each object of a code segment linked with -g: converts its .debug if it is not converted yet, or, when some
   of its relocations' symbols are dead, drops their entries in .debug and their lines in .line. */
/* 0x4574f0 */
void DWARF_line_StripDeadCode(LineLinker *linker) {
    LineSegment *segment;
    LineSectionLink *link;
    LineSection *line;
    LineSection *debug;
    LineSection *debugRelocations;
    LineSection *lineRelocations;
    LineFile *object;
    long file;
    unsigned long i;
    unsigned char flags;
    Boolean kept;
    char *debugBase;
    char *debugAt;
    char *debugEnd;
    char *lineRead;
    char *lineWrite;
    char *linesEnd;
    char *cursor;
    long shift;
    long shifted;
    long moved;
    long n;
    unsigned long entrySize;
    unsigned long address;
    unsigned long offset;
    unsigned long unitSize;
    LineChunk *debugChunk;
    LineChunk *lineChunk;
    LineEntry *entries;
    LineEntry *last;
    LineEntry *entry;
    LineEntry *unit;
    LineRelocation *sibling;
    LineRelocation *previous;
    LineSymbol *symbol;
    LinePiece *piece;
    long *at;

    if (!data_004a2e37)
        return;
    CompilerTools_ReportMessage(0x29, data_0049bed6, linker->x8);
    if (fn_00420350())
        CompilerTools_Abort();

    for (segment = linker->segments; segment; segment = segment->next) {
        if (segment->kind != 2)
            continue;
        for (link = segment->sections; link; link = link->next) {
            line = link->section;
            file = line->file;
            object = &data_004a28dc[file];
            if (!object->x30)
                continue;
            if (!object->x32) {
                debug = object->debug;
                if (debug) {
                    DumpDWARF(debug->data, debug->header->size, 0, 1, NULL, DWARF_line_Assert, 0,
                              object->x8->context);
                    DumpDWARF(debug->data, debug->header->size, 0, 1, NULL, DWARF_line_Assert, 1,
                              object->x8->context);
                }
                continue;
            }
            if (object->x33 || !line || !line->header->size || !line->x35 || !(debug = object->debug))
                continue;

            /* nothing to do when all the symbols .debug refers to are kept */
            debugRelocations = debug->relocations;
            kept = 1;
            for (i = 0; i < debugRelocations->header->size / sizeof(LineRelocation); i++) {
                flags = SYMBOLS(debugRelocations->file)[((LineRelocation *)debugRelocations->data)[i].info >> 8].flags;
                if (!(flags & 0x10) || (flags & 0x20)) {
                    kept = 0;
                    break;
                }
            }
            if (kept)
                continue;

            lineRelocations = line->relocations;
            debugBase = debugAt = debug->data;
            lineRead = lineWrite = line->data;
            linesEnd = NULL;
            unit = NULL;
            debugChunk = debug->chunks;
            lineChunk = line->chunks;
            while (debugChunk || lineChunk) {
                shift = lineRead - lineWrite;
                if (!lineChunk) {
                    debugAt += debugChunk->size;
                    debugChunk = debugChunk->next;
                } else if (!debugChunk) {
                    if (shift) {
                        MOVE_FIXUPS(lineChunk, shift);
                        memmove(lineWrite, lineRead, lineChunk->size);
                    }
                    lineWrite += lineChunk->size;
                    lineRead += lineChunk->size;
                    lineChunk = lineChunk->next;
                } else {
                    ASSERT(lineChunk->x1e == debugChunk->x1e, 0x191);
                    if (!lineChunk->x1e || !lineChunk->size) {
                        debugAt += debugChunk->size;
                        lineRead += lineChunk->size;
                        debugChunk = debugChunk->next;
                        lineChunk = lineChunk->next;
                    } else if (lineChunk->x1c) {
                        if (shift) {
                            MOVE_FIXUPS(lineChunk, shift);
                            memmove(lineWrite, lineRead, lineChunk->size);
                        }
                        debugAt += debugChunk->size;
                        lineWrite += lineChunk->size;
                        lineRead += lineChunk->size;
                        debugChunk = debugChunk->next;
                        lineChunk = lineChunk->next;
                    } else {
                        /* the unit's entries */
                        debugEnd = debugAt + debugChunk->size;
                        entries = NULL;
                        last = NULL;
                        sibling = NULL;
                        previous = NULL;
                        data_0049acc0 = 0;
                        while (debugAt < debugEnd) {
                            entrySize = CTool_EndianConvertWord32(*(unsigned int *)debugAt);
                            if (entrySize >= 8) {
                                entry = DWARF_line_ReadEntry(debugRelocations, debugAt, debugBase, entrySize, &sibling);
                                if (entry) {
                                    if (last)
                                        last->next = entry;
                                    else
                                        entries = entry;
                                    if (entry->dropped)
                                        entry->previous = previous;
                                    else
                                        previous = sibling;
                                    last = entry;
                                } else {
                                    previous = sibling;
                                    ASSERT(last, 0x1cf);
                                }
                                if (sibling) {
                                    debugAt += SYMBOLS(file)[sibling->info >> 8].value + sibling->addend -
                                               (debugAt - debugBase);
                                    continue;
                                }
                            }
                            debugAt += entrySize;
                        }

                        /* the lines of each unit and function */
                        cursor = lineRead;
                        data_0049acc0 = 0;
                        for (entry = entries; entry; entry = entry->next) {
                            switch (entry->tag) {
                            case 0x11:
                                if (entry != entries) {
                                    ASSERT(cursor == linesEnd, 0x1ee);
                                    cursor += sizeof(LineRecord);
                                    ASSERT(cursor == unit->lines + unit->lineSize, 0x1f3);
                                }
                                unitSize = CTool_EndianConvertWord32(*(unsigned int *)cursor);
                                linesEnd = cursor + unitSize - sizeof(LineRecord);
                                entry->lines = cursor;
                                entry->lineSize = unitSize;
                                entry->linesEnd = linesEnd;
                                symbol = &SYMBOLS(file)[entry->stmtList->info >> 8];
                                FIND_RELOCATION(entry->lineBase, lineRelocations,
                                                symbol->value + entry->stmtList->addend + 4);
                                ASSERT(entry->lineBase, 0x200);
                                entry->base = SYMBOLS(file)[entry->lineBase->info >> 8].value;
                                cursor += sizeof(LineHeader);
                                unit = entry;
                                break;
                            case 6:
                            case 0x14:
                                symbol = &SYMBOLS(file)[entry->lowPC->info >> 8];
                                if (symbol->size && cursor != linesEnd) {
                                    while (cursor < linesEnd) {
                                        address = CTool_EndianConvertWord32(*(unsigned int *)(cursor + 6)) + unit->base;
                                        if (address < symbol->value || address >= symbol->value + symbol->size)
                                            break;
                                        if (!entry->lineSize)
                                            entry->lines = cursor;
                                        entry->lineSize += sizeof(LineRecord);
                                        cursor += sizeof(LineRecord);
                                    }
                                    ASSERT(cursor <= linesEnd, 0x227);
                                }
                                break;
                            }
                        }
                        ASSERT(cursor == linesEnd, 0x22d);
                        ASSERT(cursor + sizeof(LineRecord) == unit->lines + unit->lineSize, 0x232);

                        /* drop the dead entries and their lines, move the others */
                        shifted = shift;
                        for (entry = entries; entry; entry = entry->next) {
                            switch (entry->tag) {
                            case 0x11:
                                if (entry != entries) {
                                    ASSERT(lineRead == linesEnd, 0x23c);
                                    piece = SYMBOLS(file)[unit->highPC->info >> 8].piece;
                                    *(unsigned int *)(lineRead + 6) =
                                        CTool_EndianConvertWord32(piece->chunk->x10 - piece->chunk->x0c);
                                    unit->highPC->addend = piece->chunk->x10 - piece->chunk->x0c;
                                    if (lineRead != lineWrite) {
                                        moved = lineRead - lineWrite;
                                        *(LineRecord *)lineWrite = *(LineRecord *)lineRead;
                                        lineWrite += sizeof(LineRecord);
                                        lineRead += sizeof(LineRecord);
                                        *unit->lineHeader = CTool_EndianConvertWord32(unit->lineSize - unit->dropped);
                                        MOVE_UNIT(entry, moved);
                                        *(LineHeader *)lineWrite = *(LineHeader *)lineRead;
                                    } else {
                                        lineWrite += sizeof(LineRecord);
                                        lineRead += sizeof(LineRecord);
                                    }
                                } else if (lineRead != lineWrite) {
                                    moved = lineRead - lineWrite;
                                    MOVE_UNIT(entry, moved);
                                    *(LineHeader *)lineWrite = *(LineHeader *)lineRead;
                                }
                                ASSERT(lineRead == entry->lines, 0x27d);
                                entry->lineHeader = (long *)lineWrite;
                                linesEnd = entry->linesEnd;
                                lineWrite += sizeof(LineHeader);
                                lineRead += sizeof(LineHeader);
                                unit = entry;
                                break;
                            case 6:
                            case 0x14:
                                if (entry->dropped) {
                                    unit->dropped += entry->lineSize;
                                    DROP_ENTRY(entry);
                                    line->header->size -= entry->lineSize;
                                    lineChunk->size -= entry->lineSize;
                                    lineRead += entry->lineSize;
                                } else {
                                    symbol = &SYMBOLS(file)[entry->lowPC->info >> 8];
                                    offset = symbol->piece->offset - symbol->piece->chunk->x0c;
                                    entry->highPC->addend = offset + symbol->size;
                                    moved = symbol->value - offset;
                                    if (moved) {
                                        for (n = 0; n < entry->lineSize; n += sizeof(LineRecord)) {
                                            at = (long *)(entry->lines + n + 6);
                                            *at = CTool_EndianConvertWord32(CTool_EndianConvertWord32(*at) +
                                                                            unit->base - moved);
                                        }
                                    }
                                    if (lineWrite != lineRead)
                                        memcpy(lineWrite, entry->lines, entry->lineSize);
                                    lineWrite += entry->lineSize;
                                    lineRead += entry->lineSize;
                                }
                                break;
                            case 7:
                            case 0xc:
                                if (entry->dropped)
                                    DROP_ENTRY(entry);
                                break;
                            }
                        }
                        ASSERT(lineRead == linesEnd, 0x2db);
                        piece = SYMBOLS(file)[unit->highPC->info >> 8].piece;
                        *(unsigned int *)(lineRead + 6) = CTool_EndianConvertWord32(piece->chunk->x10 - piece->chunk->x0c);
                        unit->highPC->addend = piece->chunk->x10 - piece->chunk->x0c;
                        if (lineRead != lineWrite) {
                            *(LineRecord *)lineWrite = *(LineRecord *)lineRead;
                            lineWrite += sizeof(LineRecord);
                            lineRead += sizeof(LineRecord);
                            *unit->lineHeader = CTool_EndianConvertWord32(unit->lineSize - unit->dropped);
                        } else {
                            lineWrite += sizeof(LineRecord);
                            lineRead += sizeof(LineRecord);
                        }
                        cursor += sizeof(LineRecord);
                        ASSERT(cursor == lineRead, 0x2f6);
                        debugChunk = debugChunk->next;
                        lineChunk = lineChunk->next;
                        freeaheap();
                    }
                }
            }
        }
    }
}

/* Writes a padding entry of `size` + 4 bytes at `p` (its length, then zeros), returns the bytes written. */
/* 0x457430 */
int DWARF_line_WritePadding(int size, char *p) {
    unsigned long word;
    int written;

    written = 4;
    word = CTool_EndianConvertWord32(size + 4);
    memmove(p, &word, 4);
    p += 4;
    word = 0;
    if (size > 1) {
        written = 6;
        memmove(p, &word, 2);
        size -= 2;
        p += 2;
    }
    if (size == 1) {
        written++;
        memmove(p, &word, 1);
    }
    return written;
}

/* Sets a kept compile unit's relocations: its first (the unit's own symbol) gets the unit's size, its third (the
   code's section) the size of the code kept after it. */
/* 0x4572b0 */
void DWARF_line_FixCompileUnit(long file, long size, LineBlock *blocks) {
    LineFixup *node;
    LineRelocation *rela;
    LineSymbol *target;
    LineChunk *unit;
    LinePiece *piece;
    LineSymbol *symbol;
    long total;
    unsigned char index;

    if (!blocks[0].piece)
        FAIL(0x34b);
    index = 0;
    for (node = blocks[0].piece->fixups; node; node = node->next, index++) {
        rela = (LineRelocation *)node->at;
        switch (index) {
        case 0:
            if ((LineSymbol *)data_004a28dc[file].symbols->data + (rela->info >> 8) != blocks[0].piece->symbol)
                FAIL(0x352);
            rela->addend = size;
            break;
        case 1:
        case 3:
            break;
        case 2:
            target = (LineSymbol *)data_004a28dc[file].symbols->data + (rela->info >> 8);
            if ((target->info & 0xf) != 3)
                FAIL(0x35a);
            total = 0;
            for (unit = target->piece->chunk; unit; unit = unit->next) {
                for (piece = unit->pieces; piece && piece->chunk == unit; piece = piece->next) {
                    symbol = piece->symbol;
                    if ((symbol->info & 0xf) != 3 && (symbol->flags & 0x10) && !(symbol->flags & 0x20))
                        total += symbol->size;
                }
#if LINKER >= 27
                /* (2.7: the unit's own code, that of its chunk) */
                break;
#endif
            }
            rela->addend = total;
            break;
        default:
            FAIL(0x370);
        }
    }
}

/* Drops a compile unit: its symbol, and its fourth relocation's symbol. */
/* 0x457210 */
void DWARF_line_DropCompileUnit(long file, LineBlock *blocks) {
    LineFixup *node;
    unsigned char index;

    if (!blocks[0].piece)
        FAIL(0x37f);
    blocks[0].piece->symbol->flags |= 0x20;
    index = 0;
    for (node = blocks[0].piece->fixups; node; node = node->next, index++) {
        switch (index) {
        case 0:
        case 1:
        case 2:
            break;
        case 3:
            ((LineSymbol *)data_004a28dc[file].symbols->data)[((LineRelocation *)node->at)->info >> 8].flags |= 0x20;
            break;
        default:
            FAIL(0x38d);
        }
    }
}

/* Moves the relocations of a symbol whose bytes moved `distance` bytes back. */
#define MOVE_RELOCATIONS(piece, distance)                                   \
    {                                                                                  \
        LineFixup *node;                                                      \
        for (node = (piece)->fixups;   node; node = node->next)                    \
            ((LineRelocation *)node->at)->offset -= (distance);                                          \
    }

/* Removes from the .debug sections the entries of the dead-stripped code: the entries (".dwarf.<tag>" symbols) of
   dropped symbols, and the compile units left without any. */
/* 0x456af0 */
void DWARF_line_CompactDebug(LineLinker *output) {
    LineSegment *group;
    LineSectionLink *link;
    LineSection *section;
    LineChunk *unit;
    LinePiece *piece;
    LinePiece *sectionPiece;
    LineSymbol *symbol;
    LineBlock blocks[3];
    LineFixup *node;
    char *data;
    char *dst;
    char text[5];
    long file;
    long delta;
    long removed;
    long out;
    long blockSize;
    long unitSize;
    long size;
    long rest;
    long pad;
    int written;
    int tag;
    unsigned char state;

    if (!data_004a2e37)
        return;
    if (fn_00420350())
        CompilerTools_Abort();
    for (group = output->segments; group; group = group->next) {
        if (group->kind != 2)
            continue;
        for (link = group->sections; link; link = link->next) {
            section = link->section;
            file = section->file;
            if (!data_004a28dc[file].x30 || !data_004a28dc[file].x33 || !section || !section->header->size ||
                !section->x35 || section->x36)
                continue;
            data = dst = section->data;
            delta = 0;
            removed = out = blockSize = 0;
            state = 0;
            sectionPiece = NULL;
            memset(blocks, 0, sizeof(blocks));
            for (unit = section->chunks; unit; unit = unit->next) {
                unitSize = 0;
                for (piece = unit->pieces; piece && piece->chunk == unit; piece = piece->next) {
                    symbol = piece->symbol;
                    if (piece->name && !strncmp(piece->name, ".dwarf.", 7)) {
                        strncpy(text, piece->name + 7, 4);
                        text[4] = 0;
                        sscanf(text, "%x", &tag);
                        if (tag == 0) {
                            switch (state) {
                            case 0:
                                FAIL(0x3f3);
                                break;
                            case 1:
                                size = symbol->size;
                                if (blocks[state].kept) {
                                    symbol->flags |= 0x10;
                                    blocks[state].piece = piece;
                                    blockSize += size;
                                    memmove(dst, data + out + removed, size);
                                    symbol->value = out + delta;
                                    out += size;
                                    unitSize += size;
                                    dst += size;
                                } else {
                                    symbol->flags |= 0x20;
                                    removed += size;
                                    if (blockSize) {
                                        size = blocks[0].piece->symbol->size;
                                        removed += size;
                                        if (blockSize - size)
                                            FAIL(0x407);
                                        out -= size;
                                        if (out != blocks[0].offset)
                                            FAIL(0x40a);
                                        blockSize -= size;
                                        unitSize -= size;
                                        dst -= size;
                                        DWARF_line_DropCompileUnit(file, blocks);
                                    }
                                }
                                state++;
                                break;
                            case 2:
                                size = symbol->size;
                                if (blocks[state].kept) {
                                    symbol->flags |= 0x10;
                                    blocks[state].piece = piece;
                                    rest = size - 4;
                                    if (out % 4) {
                                        pad = 4 - out % 4;
                                        if (pad < 4) {
                                            if (pad + 4 == rest) {
                                                blockSize += size;
                                                memmove(dst, data + out + removed, size);
                                                symbol->value = out + delta;
                                                out += size;
                                                unitSize += size;
                                                dst += size;
                                            } else {
                                                memmove(dst, data + out + removed, 4);
                                                symbol->value = out + delta;
                                                dst += 4;
                                                written = DWARF_line_WritePadding(pad, dst);
                                                if (written > rest) {
                                                    out += rest + 4;
                                                    delta += written - rest;
                                                } else if (rest > written) {
                                                    out += written + 4;
                                                    rest -= written;
                                                    removed += rest;
                                                } else {
                                                    FAIL(0x43f);
                                                }
                                                blockSize += written + 4;
                                                unitSize += written + 4;
                                                dst += written;
                                                symbol->size = written + 4;
                                            }
                                        } else {
                                            FAIL(0x44a);
                                        }
                                    } else {
                                        blockSize += 4;
                                        memmove(dst, data + out + removed, 4);
                                        if (removed) {
                                            for (node = piece->fixups; node; node = node->next) {
                                                FAIL(0x452);
                                                ((LineRelocation *)node->at)->offset -= removed;
                                            }
                                        }
                                        removed += rest;
                                        symbol->size = 4;
                                        symbol->value = out + delta;
                                        out += 4;
                                        unitSize += 4;
                                        dst += 4;
                                    }
                                    DWARF_line_FixCompileUnit(file, blockSize, blocks);
                                } else {
                                    symbol->flags |= 0x20;
                                    removed += size;
                                }
                                state++;
                                break;
                            }
                        } else if (tag == 0x11) {
                            /* a compile unit */
                            memset(blocks, 0, sizeof(blocks));
                            state = 0;
                            blocks[state].piece = piece;
                            blocks[state].offset = out;
                            size = symbol->size;
                            blockSize = size;
                            memmove(dst, data + out + removed, size);
                            if (removed) {
                                if (delta > removed)
                                    FAIL(0x3e4);
                                MOVE_RELOCATIONS(piece, removed - delta);
                            }
                            symbol->value = out + delta;
                            out += size;
                            unitSize += size;
                            dst += size;
                            state++;
                        } else {
                            size = symbol->size;
                            if (!(symbol->flags & 0x10) || (symbol->flags & 0x20)) {
                                removed += size;
                            } else {
                                unit->x1e = 1;
                                blocks[0].piece->symbol->flags |= 0x10;
                                blocks[0].kept = blocks[1].kept = blocks[2].kept = 1;
                                blockSize += size;
                                memmove(dst, data + out + removed, size);
                                if (removed) {
                                    if (delta > removed)
                                        FAIL(0x475);
                                    MOVE_RELOCATIONS(piece, removed - delta);
                                }
                                symbol->value = out + delta;
                                out += size;
                                unitSize += size;
                                dst += size;
                            }
                        }
                    } else if ((symbol->info & 0xf) == 3) {
                        if (sectionPiece)
                            FAIL(0x484);
                        sectionPiece = piece;
                    } else {
                        FAIL(0x488);
                    }
                }
                unit->size = unitSize;
                if (!sectionPiece)
                    FAIL(0x48e);
                sectionPiece->symbol->size = unitSize;
                sectionPiece = NULL;
            }
            section->header->size = out;
        }
    }
}

/* Removes from the .line sections the line tables (".line.<function>" symbols) of the dead-stripped code, and the
   units left without any; the kept entries' addresses are moved by the removed code's size. */
/* 0x456590 */
void DWARF_line_CompactLine(LineLinker *output) {
    LineSegment *group;
    LineSectionLink *link;
    LineSection *section;
    LineSection *relocations;
    LineChunk *unit;
    LinePiece *piece;
    LinePiece *sectionPiece;
    LineSymbol *symbol;
    LineBlock blocks[3];
    LineRelocation *rela;
    unsigned long *at;
    char *data;
    char *dst;
    long removed;
    long codeKept;
    long codeRemoved;
    long out;
    long blockSize;
    long unitSize;
    unsigned long size;
    unsigned long i;
    unsigned long target;
    unsigned char state;
    char dropped;

    if (!data_004a2e37)
        return;
    if (fn_00420350())
        CompilerTools_Abort();
    for (group = output->segments; group; group = group->next) {
        if (group->kind != 2)
            continue;
        for (link = group->sections; link; link = link->next) {
            section = link->section;
            if (!data_004a28dc[section->file].x30 || !data_004a28dc[section->file].x33 || !section ||
                !section->header->size || !section->x35 || section->x36)
                continue;
            relocations = section->relocations;
            data = dst = section->data;
            removed = codeKept = codeRemoved = out = blockSize = 0;
            data_0049acc0 = 0;
            sectionPiece = NULL;
            state = 0;
            dropped = 0;
            memset(blocks, 0, sizeof(blocks));
            for (unit = section->chunks; unit; unit = unit->next) {
                unitSize = 0;
                for (piece = unit->pieces; piece && piece->chunk == unit; piece = piece->next) {
                    symbol = piece->symbol;
                    if (piece->name && !strncmp(piece->name, ".line.", 6)) {
                        if (piece->function) {
                            /* a function's line entries, 10 bytes each: the line, the position, the address */
                            size = symbol->size;
                            if (!(symbol->flags & 0x10) || (symbol->flags & 0x20)) {
                                codeRemoved += piece->function->symbol->size;
                                removed += size;
                                continue;
                            }
                            if (dropped && size)
                                FAIL(0x4e3);
                            codeKept += piece->function->symbol->size;
                            unit->x1e = 1;
                            blockSize += size;
                            if (codeRemoved) {
                                for (i = 0; i < size; i += 10) {
                                    at = (unsigned long *)(data + out + removed + i + 6);
                                    *at = CTool_EndianConvertWord32(CTool_EndianConvertWord32(*at) - codeRemoved);
                                }
                            }
                            memmove(dst, data + out + removed, size);
                            symbol->value = out;
                            out += size;
                            unitSize += size;
                            dst += size;
                            continue;
                        }
                        size = symbol->size;
                        switch (state) {
                        case 0:
                            /* a unit's header: its size and its code's address */
                            memset(blocks, 0, sizeof(blocks));
                            codeKept = codeRemoved = 0;
                            blockSize = 0;
                            dropped = 0;
                            if (symbol->flags & 0x20) {
                                dropped = 1;
                                symbol->flags |= 0x20;
                                removed += size;
                                state++;
                                continue;
                            }
                            symbol->flags |= 0x10;
                            blocks[state].piece = piece;
                            blocks[state].kept = 1;
                            blocks[state].offset = out;
                            blocks[state].removed = removed;
                            state++;
                            break;
                        case 1:
                            /* the unit's last entry: the end of its code */
                            if ((symbol->flags & 0x20) || dropped) {
                                symbol->flags |= 0x20;
                                removed += size;
                                state = 0;
                                continue;
                            }
                            symbol->flags |= 0x10;
                            *(unsigned long *)(data + out + removed + 6) = CTool_EndianConvertWord32(codeKept);
                            *(unsigned long *)(data + blocks[0].offset) = CTool_EndianConvertWord32(blockSize + size);
                            if (blocks[0].removed) {
                                target = blocks[0].offset + blocks[0].removed + 4;
                                FIND_RELOCATION(rela, relocations, target);
                                rela->offset = blocks[0].offset + 4;
                            }
                            state = 0;
                            break;
                        }
                        blockSize += size;
                        memmove(dst, data + out + removed, size);
                        symbol->value = out;
                        out += size;
                        unitSize += size;
                        dst += size;
                    } else if ((symbol->info & 0xf) == 3) {
                        if (sectionPiece)
                            FAIL(0x52d);
                        sectionPiece = piece;
                    } else {
                        FAIL(0x531);
                    }
                }
                unit->size = unitSize;
                if (!sectionPiece)
                    FAIL(0x537);
                sectionPiece->symbol->size = unitSize;
                sectionPiece = NULL;
            }
            section->header->size = out;
        }
    }
}

/* Removes the debugging information of the dead-stripped code from the output's .debug and .line. */
/* 0x456550 */
void DWARF_line_Compact(void) {
    if (!data_004a2e37)
        return;
    DWARF_line_CompactDebug(ELF_PPC_EABI_FindNamedOutput(".debug"));
    DWARF_line_CompactLine(ELF_PPC_EABI_FindNamedOutput(".line"));
}
