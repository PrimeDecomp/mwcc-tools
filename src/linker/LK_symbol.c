/* The link's symbols by name, and the link map file: a symbol's name and alignment for the messages and the map,
   the name hash the linker keeps every symbol of the link in (ELPiece, by name and object), and the map file's
   output (opened, written through a buffer, closed; its section layout lines).

   Built with -inline deferred, the compiler emits the functions in the reverse of their order here. No assertion
   names this file: LK_symbol.c is the survey's name for the linker's symbol unit (docs/linker/README.md). */
#include "compiler/common.h"
#include "compiler/CompilerTools.h"
#include "driver/Files.h"
#include "linker/ELF_linker.h"

#ifndef NONMATCHING /* (the wasm build has the target's stdarg.h) */
typedef char *va_list;
#define va_start(ap, last) ((ap) = (char *)&(last) + (((char *)(&(last) + 1) - (char *)&(last) + 3) / 4 * 4))
#endif

extern void CompilerTools_FormatMessageAndLongjmp(int message, int status, ...);
extern int strcmp(const char *a, const char *b);
extern unsigned long strlen(const char *s);
extern void *memset(void *dst, int c, unsigned long n);
extern int vsnprintf(char *s, unsigned long n, const char *format, va_list arguments);
extern void CompilerTools_ReportLimitedDiagnostic(int message, ...);
extern short PrepDumpELF_CreateFile(short vRefNum, long parID, unsigned char *name, long creator, long type);
extern short SRecord_InitBuffer(GList *buffer, long size);
/* ELF_gen.c's: a buffer written to a file, and data added to a buffer (written when it is full) */
extern void ELF_gen_WriteBuffer(GList *buffer, short file, int message);
extern void ELF_gen_AddToBuffer(GList *buffer, short file, char *data, long length, int message);

/* The symbols LK_symbol_Add made: the last, and the first and last of their list (after the name hash, which
   LK_symbol_ClearTable clears with them). */
extern ELPiece *data_004a21c4;
extern ELPiece *data_004a21c8;
extern ELPiece *data_004a21cc;

/* The link map file (-map): its file, its creator, and its handle when open. */
extern FSSpec data_0049bf5c;
extern long data_0049c104;

/* The link map's buffer. */
static GList map;

/* The name hash of NAME: its index in data_0049e1c0. */
static unsigned long LK_Hash(char *name) {
    unsigned long hash;
    unsigned long length;
    unsigned long i;

    length = strlen(name);
    hash = length;
    for (i = 0; i < length; i++)
        hash = ((hash >> 27) ^ (hash << 5)) ^ (long)(signed char)name[i];
    return ((hash >> 20) ^ (hash >> 10) ^ hash) & 0xfff;
}

#if LINKER >= 27
unsigned long LK_symbol_layoutFile;
Boolean LK_symbol_layoutFixed;

/* Writes a section's heading of the link map's section layout (2.7's: with the file offsets). */
void LK_symbol_PrintLayoutHeader(char *section) {
    LK_symbol_Print("\r\n\r\n%s section layout\r\n", section);
    LK_symbol_Print("  Starting        Virtual  File\r\n");
    LK_symbol_Print("  address  Size   address  offset\r\n");
    LK_symbol_Print("  ---------------------------------\r\n");
}

/* Writes a line of the link map's section layout. */
void LK_symbol_PrintLayoutLine(unsigned long offset, unsigned long size, unsigned long address, int align,
                               char *name, char *file, char *member) {
    LK_symbol_Print("  %08x %06x %08x %08x %2i %s \t%s %s\r\n", offset, size, address,
                    LK_symbol_layoutFixed ? LK_symbol_layoutFile : LK_symbol_layoutFile + offset, align, name, file,
                    member);
}

/* Writes the padding an alignment adds at OFFSET (the address and file offset shown: those after it). */
void LK_symbol_PrintLayoutFill(unsigned long offset, unsigned long size, unsigned long address, int align) {
    LK_symbol_Print("  %08x %06x %08x %08x %2i %s\r\n", offset, size, address, LK_symbol_layoutFile + offset + size,
                    align, "*fill*");
}

/* The padding between two chunks of one input section ("**fill**"). */
void LK_symbol_PrintChunkFill(unsigned long offset, unsigned long size, unsigned long address, int align) {
    LK_symbol_Print("  %08x %06x %08x %08x %2i %s\r\n", offset, size, address, LK_symbol_layoutFile + offset + size,
                    align, "**fill**");
}
#else
/* Writes a section's heading of the link map's section layout. */
/* 0x452110 */
void LK_symbol_PrintLayoutHeader(char *section) {
    LK_symbol_Print("\r\n\r\n%s section layout\r\n", section);
    LK_symbol_Print("  Starting        Virtual\r\n");
    LK_symbol_Print("  address  Size   address\r\n");
    LK_symbol_Print("  -----------------------\r\n");
}

/* Writes a line of the link map's section layout. */
/* 0x4520d0 */
void LK_symbol_PrintLayoutLine(unsigned long offset, unsigned long size, unsigned long address, int align,
                               char *name, char *file, char *member) {
    LK_symbol_Print("  %08x %06x %08x %2i %s \t%s %s\r\n", offset, size, address, align, name, file, member);
}
#endif

/* Writes to the link map, when there is one (at most 0x1400 bytes a call). */
/* 0x452040 */
void LK_symbol_Print(const char *format, ...) {
    char text[0x1400];
    va_list arguments;
    unsigned long length;

    if (data_0049c116) {
        va_start(arguments, format);
        length = vsnprintf(text, sizeof(text), format, arguments);
        if (length > sizeof(text)) {
            length = sizeof(text);
            text[sizeof(text) - 1] = 0;
        }
        ELF_gen_AddToBuffer(&map, data_004a2dce, text, length, 5);
    }
}

/* Makes and opens the link map file, and its buffer. */
/* 0x451fa0 */
void LK_symbol_OpenMap(void) {
    short error;

    error = PrepDumpELF_CreateFile(data_0049bf5c.vRefNum, data_0049bf5c.parID, data_0049bf5c.name, data_0049c104,
                                   'TEXT');
    if (error) {
        CompilerTools_ReportLimitedDiagnostic(5, data_0049bf5c.name);
        CompilerTools_FormatMessageAndLongjmp(0xd, error);
    }
    error = Files_OpenFile((CWFileSpec *)&data_0049bf5c, 3, &data_004a2dce);
    if (error) {
        CompilerTools_ReportLimitedDiagnostic(5, data_0049bf5c.name);
        CompilerTools_FormatMessageAndLongjmp(0xd, error);
    }
    if (SRecord_InitBuffer(&map, 0x7d00))
        CompilerTools_FormatMessageAndLongjmp(0xd, -108);
}

/* Closes the link map file and frees its buffer. */
/* 0x451f70 */
void LK_symbol_CloseMap(void) {
    if (data_004a2dce) {
        Files_Close(data_004a2dce);
        data_004a2dce = 0;
    }
    FreeGList(&map);
}

/* Writes what the link map's buffer holds to its file. */
/* 0x451f40 */
void LK_symbol_FlushMap(void) {
    if (data_004a2dce)
        ELF_gen_WriteBuffer(&map, data_004a2dce, 5);
}

/* Empties the name hash, and the list of the symbols made after it. */
/* 0x451f30 */
void LK_symbol_ClearTable(ELPiece **table) {
    memset(table, 0, 0x1004 * sizeof(ELPiece *));
}

/* The first symbol LK_symbol_Add made. */
/* 0x451f20 */
ELPiece *LK_symbol_First(void) {
    return data_004a21c8;
}

/* Makes a symbol of NAME (its object and ELF symbol still to set) in the name hash and the list. */
/* 0x451dc0 */
ELPiece *LK_symbol_Add(char *name) {
    unsigned long hash;
    ELPiece *piece;

    hash = LK_Hash(name);
    piece = (ELPiece *)galloc(sizeof(ELPiece));
    memset(piece, 0, sizeof(ELPiece));
    piece->name = name;
    piece->hashNext = data_0049e1c0[hash];
    data_0049e1c0[hash] = piece;
    data_004a21c4 = piece;
    if (!data_004a21c8)
        data_004a21c8 = piece;
    else
        data_004a21cc->nextAdded = piece;
    data_004a21cc = piece;
    return piece;
}

#if LINKER >= 27
/* The last symbol LK_symbol_Add made moved to just after AFTER in the list. (Not an original function.) */
void LK_symbol_MoveAfter(ELPiece *after) {
    ELPiece *piece = data_004a21cc;
    ELPiece *prev;

    if (!after || after == piece || piece == data_004a21c8)
        return;
    for (prev = data_004a21c8; prev && prev->nextAdded != piece; prev = prev->nextAdded)
        ;
    if (!prev)
        return;
    prev->nextAdded = 0;
    data_004a21cc = prev;
    piece->nextAdded = after->nextAdded;
    after->nextAdded = piece;
    if (data_004a21cc == after)
        data_004a21cc = piece;
}
#endif

#if LINKER >= 27
/* The last symbol LK_symbol_Add made. (Not an original function.) */
ELPiece *LK_symbol_Last(void) {
    return data_004a21cc;
}

/* The symbols after BEFORE (0: the first) up to LAST moved to the end of the list. (Not an original function.) */
void LK_symbol_MoveToEnd(ELPiece *before, ELPiece *last) {
    ELPiece *first = before ? before->nextAdded : data_004a21c8;

    if (!first || last == data_004a21cc)
        return;
    if (before)
        before->nextAdded = last->nextAdded;
    else
        data_004a21c8 = last->nextAdded;
    data_004a21cc->nextAdded = first;
    last->nextAdded = 0;
    data_004a21cc = last;
}
#endif

/* The symbol NAME of object FILE, dropped or not. */
/* 0x451c60 */
ELPiece *LK_symbol_FindInFile(char *name, long file) {
    ELPiece *piece;

    for (piece = data_0049e1c0[LK_Hash(name)]; piece; piece = piece->hashNext) {
        if (piece->file == file && strcmp(name, piece->name) == 0)
            break;
    }
    return piece;
}

/* The global or weak symbol NAME (or a linker-defined one) that is not dropped (FILE is not read). */
/* 0x451b00 */
ELPiece *LK_symbol_FindGlobal(char *name, long file) {
    ELPiece *piece;
    unsigned char binding;

    for (piece = data_0049e1c0[LK_Hash(name)]; piece; piece = piece->hashNext) {
        binding = piece->symbol ? piece->symbol->info >> 4 : 0xff;
        if ((unsigned char)(binding - 1) > 1 && piece->file >= 0)
            continue;
        if (strcmp(name, piece->name) == 0 && !(piece->symbol->flags & 0x20))
            break;
    }
    return piece;
}

/* The first symbol NAME of the hash, of any object. */
/* 0x4519d0 */
ELPiece *LK_symbol_FindAny(char *name) {
    ELPiece *piece;

    for (piece = data_0049e1c0[LK_Hash(name)]; piece; piece = piece->hashNext) {
        if (strcmp(name, piece->name) == 0)
            break;
    }
    return piece;
}

/* The symbol NAME as object FILE sees it: its own that is not dropped, else the global one. */
/* 0x451820 */
ELPiece *LK_symbol_Find(char *name, long file) {
    ELPiece *piece;

    piece = NULL;
    if (file >= 0) {
        for (piece = data_0049e1c0[LK_Hash(name)]; piece; piece = piece->hashNext) {
            if (piece->file == file && strcmp(name, piece->name) == 0 &&
                (!piece->symbol || !(piece->symbol->flags & 0x20)))
                break;
        }
    }
    if (piece)
        return piece;
    return LK_symbol_FindGlobal(name, file);
}

/* The alignment of a symbol of object FILE: from its .comment entry (by the comment's version), else from its
   kind and its size and value. */
/* 0x4516e0 */
unsigned long LK_symbol_Alignment(long file, ELSymbol *symbol) {
    ELObject *object;
    ELSection *section;
    unsigned long value;
    unsigned long size;
    unsigned char type;

    value = symbol->value;
    size = symbol->size;
    object = &data_004a28dc[file];
#if LINKER >= 27
    /* (the Linker Generated Symbol File's symbols: words) */
    if (file == ELF_linker_generatedFile)
        return 4;
#endif
    if (object->commentVersion == 1)
        return (symbol->flags & 0xf) + 1;
    if (object->commentVersion == 2)
        return 1 << (symbol->flags & 0xf);
    if (object->commentVersion == 3 && symbol->piece->align != -1)
        return symbol->piece->align ? symbol->piece->align : 1;
    section = &object->sections[symbol->section];
    type = symbol->info & 0xf;
    if (type == 3)
        return section->header->alignment;
    if (type == 2)
        return 4;
    if ((unsigned char)(section->kind - 6) <= 1)
        return 4;
    if (!size)
        return 1;
    if ((value & 1) || size == 1)
        return 1;
    if ((value & 2) || size == 2)
        return 2;
    if ((value & 4) || size == 4)
        return 4;
    if ((value & 8) || size == 8)
        return 8;
    if ((value & 0x10) || size == 0x10)
        return 8;
    return 4;
}

/* A symbol's name as its object's string table has it: a section symbol of no name is its section's (or
   "_Undefined_Symbol_", "_ABS_Symbol_", "_Common_Symbol_", "_Unknown_Symbol_"). While the object is read, the
   symbol's first word is its name's offset. */
/* 0x451640 */
char *LK_symbol_ObjectName(long file, ELSymbol *symbol) {
    ELObject *object;
    char *name;
    unsigned short section;

    object = &data_004a28dc[file];
    name = object->strings->data + (unsigned long)symbol->piece;
    if (*name || (symbol->info & 0xf) != 3)
        return name;
    section = symbol->section;
    if (section == 0)
        return "_Undefined_Symbol_";
    if (section == 0xfff1)
        return "_ABS_Symbol_";
    if (section == 0xfff2)
        return "_Common_Symbol_";
    if (section > object->image->section_header_count)
        return "_Unknown_Symbol_";
    return object->sections[section].name;
}

/* A symbol's name: its piece's, once it has one (a defined symbol, one made for an undefined one, a
   linker-defined one), else its object's string table's. */
/* 0x451570 */
char *LK_symbol_Name(long file, ELSymbol *symbol) {
    if (symbol->section || (symbol->flags & 0x40) || file < 0)
        return symbol->piece->name;
    return LK_symbol_ObjectName(file, symbol);
}
