/* The linker proper: the project's files read, the objects loaded (their sections cut into pieces, their symbols
   resolved, the linker-defined symbols made), dead stripping from the entry point, FORCEACTIVE, the constructors,
   destructors and exception tables, the link map's tree of what was kept and why, -library's entry point and the
   end of a link.

   Built with -inline deferred, the compiler emits the functions in the reverse of their order here. The assertions
   name this file "ELF_linker.c". */
#include "compiler/common.h"
#include "compiler/CompilerTools.h"
#include "compiler/ELF_Endian.h"
#include "driver/libimp-eabi-ppc.h"

extern void CompilerTools_FormatMessageAndLongjmp(int message, int status, ...);
extern unsigned char CompilerTools_ReportDiagnostic(long diagnosticCode, ...);
extern int sprintf(char *s, const char *format, ...);
extern int sscanf(const char *s, const char *format, ...);
extern unsigned long strlen(const char *s);
extern char *strcpy(char *dst, const char *src);
extern char *strncpy(char *dst, const char *src, unsigned long n);
extern char *strcat(char *dst, const char *src);
extern int strcmp(const char *a, const char *b);
extern int strncmp(const char *a, const char *b, unsigned long n);
extern void *memcpy(void *dst, const void *src, unsigned long n);
extern void *memset(void *dst, int c, unsigned long n);
extern void CompilerTools_ReportLimitedDiagnostic(int message, ...);
#include <setjmp.h>
#include "driver/CWPluginsPrivate.h"
#include "driver/Memory.h"
#include "driver/COSToolsCLT.h"
#include "driver/Files.h"

#define EL_ASSERT(line) CompilerTools_FormatMessageAndLongjmp(0x33, 2, "ELF_linker.c", line)

#include "linker/ELF_linker.h"

/* Other units' functions. */
extern int _Setjmp(jmp_buf env);
extern Boolean fn_00420350(void);
extern void CompilerTools_Abort(void);
extern void CompilerTools_ResetDiagnosticCounts(void);
extern void CompilerTools_DispatchMessageBufferByMode(int status);
extern void CompilerTools_ReportMessage(int message, ...);
extern void CompilerTools_ReportDuplicate(char *name, char *text, char *file, char *other);
extern void CompilerTools_ReportUndefined(char *name, long x, char *referrer, char *file);
extern long fn_00420260(void);
extern void fn_00420070(long index, ELFile *file);
extern short fn_0041fcf0(ELFile *file);
extern void fn_0041fc60(ELFile *file);
extern void fn_0041e000(void);
extern int ELF_PPC_EABI_MatchFileName(char *pattern, char *name);
extern Boolean ELF_PPC_EABI_CheckSdaReference(ELRelocation *rel, ELPiece *global, char *name, long file);
extern void ELF_PPC_EABI_AssignSection(ELObject *object, ELSection *section);
extern void ELF_PPC_EABI_ReadCommandFile(void);
extern void ELF_PPC_EABI_AddSectionSymbols(void);
extern void ELF_gen_WriteFile(void);
extern void ELF_gen_FlushOutput(void);
extern void ELF_gen_CloseOutput(void);
extern void ELF_gen_OpenOutput(void);
#if LINKER >= 27
extern Boolean ELF_gen_deferOpen;
#endif
extern void SRecord_OpenFile(void);
extern void SRecord_Header(void);
extern void SRecord_CloseFile(void);
extern void Archive_Write(void);
extern char *Archive_MemberName(char **longNames, char *name);
extern void Archive_EndLongNames(char **longNames, long size);
extern ELLinkSymbol *LinkSym_FindIndex(long index);


#define EL_HandleSize(handle) COS_GetHandleSize(handle)

enum { EL_STT_SECTION = 3, EL_STT_FILE = 4 };
enum { EL_SHN_COMMON = 0xfff2 };
enum { EL_SHF_EXECINSTR = 4 };

/* A linker-generated message's hook: the output could not be written, out of memory. */
/* 0x434a50 */
void ELF_linker_OutOfMemory(void) {
    CompilerTools_FormatMessageAndLongjmp(0xe, -108, data_0049bed6);
}

/* -library: writes the project's objects as an archive. Returns 0, or the error's status. */
/* 0x434950 */
int ELF_linker_Library(void) {
    ELFile *file;
    int result;

    result = _Setjmp(file_input_jmpbuf);
    if (result == 0) {
        data_004a2764 = NULL;
        if (initheaps(ELF_linker_OutOfMemory))
            CompilerTools_FormatMessageAndLongjmp(0xe, -108, data_0049bed6);
        data_004a2860 = (void *)ELF_linker_OutOfMemory;
        InitNameHash();
        CompilerTools_ReportMessage(0x1a, data_0049bed6);
        ELF_gen_OpenOutput();
        ELF_linker_ReadFiles(1);
        ELF_linker_LoadObjects(1);
        Archive_Write();
        ELF_gen_FlushOutput();
        for (file = data_004a2764; file; file = file->next)
            fn_0041fc60(file);
        FreeGList(&data_0049dbe8);
        ELF_gen_CloseOutput();
        releasegheap();
        CompilerTools_ResetDiagnosticCounts();
        result = 0;
    } else {
        for (file = data_004a2764; file; file = file->next)
            fn_0041fc60(file);
        FreeGList(&data_0049dbe8);
        ELF_gen_CloseOutput();
        releasegheap();
        Files_DeleteFile(&data_0049bed0);
        CompilerTools_ResetDiagnosticCounts();
        CompilerTools_DispatchMessageBufferByMode(result);
    }
    return result;
}

/* The allocator's hook while the files are read: out of memory. */
/* 0x434940 */
void ELF_linker_NoMemory(void) {
    CompilerTools_FormatMessageAndLongjmp(0xd, -108);
}

/* The end of a link: the files freed, the outputs closed, and deleted when it failed. */
/* 0x4348c0 */
void ELF_linker_End(Boolean succeeded, int status) {
    ELFile *file;

    for (file = data_004a2764; file; file = file->next)
        fn_0041fc60(file);
    ELF_gen_CloseOutput();
    if (data_0049c116) {
        if (!status)
            LK_symbol_FlushMap();
        LK_symbol_CloseMap();
    }
    if (data_004a2e3a) {
        SRecord_CloseFile();
        if (!succeeded)
            Files_DeleteFile(&data_0049bf16);
    }
    releaseheaps();
    if (!succeeded)
        Files_DeleteFile(&data_0049bed0);
    CompilerTools_ResetDiagnosticCounts();
}

/* Whether an image is an ELF file. */
/* 0x434880 */
Boolean ELF_linker_IsELF(char *image) {
    return strncmp(image, "\177ELF", 4) == 0;
}

/* Reads the project's files into data_004a2764 (objects, then archives), and makes the objects' table. */
/* 0x434510 */
void ELF_linker_ReadFiles(Boolean library) {
    /* those not linked are freed; an archive's members needed are marked kind 2 */
    ELFile **last;
    ELFile *archives;
    ELFile **lastArchive;
    ELFile *file;
    ELMember *member;
    struct Elf32Header *image;
    char whole;
    long count;
    long before;
    long files;
    long i;

    count = 0;
    files = fn_00420260();
    last = &data_004a2764;
    archives = NULL;
    lastArchive = &archives;
    data_004a2e4e = !copts[0];
    for (i = 0; i < files; i++) {
        whole = 0;
        EL_CHECK_BREAK();
        file = (ELFile *)galloc(sizeof(ELFile));
        data_004a2a84 += sizeof(ELFile);
        memclrw(file, sizeof(ELFile));
        fn_00420070(i, file);
        if (file->flags & EL_FILE_DROPPED)
            continue;
        if ((file->flags & EL_FILE_BINARY) && library)
            continue;
        if (fn_0041fcf0(file)) {
            CompilerTools_ReportLimitedDiagnostic(0x3d, file->name);
            CompilerTools_FormatMessageAndLongjmp(0xd, 2);
        }
        if ((file->flags & EL_FILE_DROPPED) || ((file->flags & EL_FILE_BINARY) && library)) {
            fn_0041fc60(file);
            continue;
        }
        if (!library)
            whole = ELF_linker_MatchFileLists(file);
        if (file->flags & EL_FILE_WHOLE) {
            fn_00413a40((struct StorageHandle *)file->member->data);
            if (data_004a2e4e)
                ELF_Endian_SwapElf32Header((struct Elf32Header *)*file->member->data);
            ELF_linker_IsLinkableObject((struct Elf32Header *)*file->member->data);
            *last = file;
            last = &file->next;
            count++;
        } else if (file->flags & EL_FILE_BINARY) {
            fn_00413a40((struct StorageHandle *)file->member->data);
            *last = file;
            last = &file->next;
            count++;
        } else if (file->flags2 & EL_FILE2_OBJECT) {
            fn_00413a40((struct StorageHandle *)file->member->data);
            if (data_004a2e4e)
                ELF_Endian_SwapElf32Header((struct Elf32Header *)*file->member->data);
            image = (struct Elf32Header *)*file->member->data;
            if ((image->type == 1 || whole) && ELF_linker_IsLinkableObject(image)) {
                *last = file;
                last = &file->next;
                count++;
            } else {
                file->flags = (file->flags & ~EL_FILE_DROPPED) | EL_FILE_DROPPED;
                fn_0041fc60(file);
            }
        } else if (file->flags & EL_FILE_ARCHIVE) {
            before = count;
            member = file->member;
            if (member->kind == 6) {
                CompilerTools_ReportLimitedDiagnostic(0x3e, file->name);
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
                fn_00413a40((struct StorageHandle *)file->member->data);
                count += *(short *)(*file->member->data + 2);
            } else if (member->kind == 1) {
                for (; member; member = member->next) {
                    if (!EL_HandleSize(member->data))
                        continue;
                    fn_00413a40((struct StorageHandle *)member->data);
                    if (!ELF_linker_IsELF(*member->data))
                        continue;
                    if (data_004a2e4e)
                        ELF_Endian_SwapElf32Header((struct Elf32Header *)*member->data);
                    image = (struct Elf32Header *)*member->data;
                    if (image->type == 1 && ELF_linker_IsLinkableObject(image)) {
                        member->kind = 2;
                        count++;
                    }
                }
            } else {
                CompilerTools_ReportLimitedDiagnostic(0x3e, file->name);
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
            }
            if (before < count) {
                *lastArchive = file;
                lastArchive = &file->next;
            } else {
                file->flags = (file->flags & ~EL_FILE_DROPPED) | EL_FILE_DROPPED;
                fn_0041fc60(file);
            }
        }
    }
#if LINKER >= 27
    count++; /* (the Linker Generated Symbol File) */
#endif
    data_004a28dc = (ELObject *)galloc(count * sizeof(ELObject));
    memset(data_004a28dc, 0, count * sizeof(ELObject));
#if LINKER >= 27
    ELF_linker_memberNames = (char **)galloc(count * sizeof(char *));
    memset(ELF_linker_memberNames, 0, count * sizeof(char *));
#endif
    *last = archives;
}

/* Where object INDEX comes from, for a message: an archive member is "MEMBER of archive 'ARCHIVE'", any other
   object its file's name (the linker-defined symbols': "linker generated symbol"). (Not an original function:
   the original repeats it inline.) */
static void EL_Where(long index, char **name, char **of, char **archive, char **quote) {
    char *member;
    char *file;

    member = (index > -1 && data_004a28dc[index].memberName) ? data_004a28dc[index].fileSymbol->name : "";
    file = index > -1 ? data_004a28dc[index].file->name : "linker generated symbol";
    if (*member) {
        *name = member;
        *of = " of archive '";
        *archive = file;
        *quote = "'";
    } else {
        *name = file;
        *of = "";
        *archive = "";
        *quote = "";
    }
}

/* The file object INDEX is from, for a message. (Not an original function.) */
static char *EL_FileName(long index) {
    return index > -1 ? data_004a28dc[index].file->name : "linker generated symbol";
}

/* Makes the symbol of a linker-defined symbol NAME (LINE: the assertion's): one an object defines globally is
   reported (0x6a) and marked as overridden, a local one reported (0x6b). (Not an original function: the
   original has it twice, for the list and for the table.) */
static void EL_DefineLinkSymbol(char *name, long index, ELPiece **slot, int line) {
    ELPiece *symbol;
    ELSymbol *elf;
    char *where;
    char *of;
    char *archive;
    char *quote;
    char binding;

    symbol = LK_symbol_Find(name, -1);
    if (symbol) {
        binding = symbol->symbol->info >> 4;
        if ((unsigned char)(binding - 1) <= 1) {
            symbol->symbol->flags |= 0x20;
            EL_Where(symbol->file, &where, &of, &archive, &quote);
            CompilerTools_ReportDiagnostic(0x6a, name, where, of, archive, quote);
        }
        if (binding == 0) {
            EL_Where(symbol->file, &where, &of, &archive, &quote);
            CompilerTools_ReportDiagnostic(0x6b, name, where, of, archive, quote);
        }
    }
    symbol = LK_symbol_Add(name);
    symbol->file = index;
    *slot = symbol;
    if ((*slot)->symbol)
        EL_ASSERT(line);
    elf = (ELSymbol *)galloc(sizeof(ELSymbol));
    (*slot)->symbol = elf;
    memset(elf, 0, sizeof(ELSymbol));
    elf->info = 0x10;
    elf->piece = *slot;
}

/* A file linked as its bytes: an object of one section, .BINARY, kept whole, with a section symbol. (Not an
   original function.) */
static void EL_LoadBinary(ELFile *file, char *name, char *bytes, long size) {
    ELObject *object;
    ELSection *section;
    ELSymbol *elf;

    object = &data_004a28dc[data_004a292c];
    object->image = NULL;
    object->memberName = NULL;
    object->file = file;
    object->fileSymbol = LK_symbol_Add(name);
    section = (ELSection *)galloc(sizeof(ELSection));
    object->sections = section;
    memset(section, 0, sizeof(ELSection));
    section->name = ".BINARY";
    section->data = bytes;
    section->header = (Elf32Section *)galloc(sizeof(Elf32Section));
    memset(section->header, 0, sizeof(Elf32Section));
    section->header->alignment = 4;
    section->header->size = size;
    section->header->file_offset = 0;
    section->header->type = 1;
    section->header->flags = 2;
    section->chunks = (ELChunk *)galloc(sizeof(ELChunk));
    memset(section->chunks, 0, sizeof(ELChunk));
    section->chunks->whole = 1;
    section->whole = 1;
    section->chunks->kept = 1;
    section->kept = section->chunks->kept;
    object->file->flags = (object->file->flags & ~EL_FILE_BINARY_KEPT) | (section->kept & 1);
    object->forced = object->file->flags & EL_FILE_BINARY_KEPT;
    section->pieces = LK_symbol_Add(section->name);
    elf = (ELSymbol *)galloc(sizeof(ELSymbol));
    section->pieces->symbol = elf;
    memset(elf, 0, sizeof(ELSymbol));
    elf->info = EL_STT_SECTION;
    elf->size = size;
    elf->piece = section->pieces;
    section->file = data_004a292c;
    section->index = 0;
    ELF_PPC_EABI_AssignSection(object, section);
    ELF_linker_SplitSection(section, 1);
}

/* A section of the current object, from its header. (Not an original function.) */
static void EL_MakeSection(ELObject *object, ELSection *section, char *image, char *names, long index,
                            Boolean symbols) {
    section->header = (Elf32Section *)(image + object->image->section_header_offset + index * sizeof(Elf32Section));
    section->data = image + section->header->file_offset;
    section->name = names + section->header->name;
    section->chunks = (ELChunk *)galloc(sizeof(ELChunk));
    memset(section->chunks, 0, sizeof(ELChunk));
    section->file = data_004a292c;
    section->index = index;
    if (symbols)
        ELF_PPC_EABI_AssignSection(object, section);
}

/* Whether a global symbol of the current object is a duplicate, reported or marked (other |= 0x20) as the one
   that loses. (Not an original function.) */
static void EL_CheckGlobal(ELObject *object, ELSymbol *elf, char *name) {
    ELPiece *found;
    ELSymbol *other;
    char binding;

    found = LK_symbol_Find(name, data_004a292c);
    if (!found)
        return;
    other = found->symbol;
    binding = other->info >> 4;
    if (binding == 2) {
        other->flags |= 0x20;
    } else if (binding == 1) {
        if (elf->section == EL_SHN_COMMON && other->section == EL_SHN_COMMON) {
            if (elf->size > other->size)
                other->size = elf->size;
            if (elf->value > other->value)
                other->value = elf->value;
            elf->flags |= 0x20;
        } else if (elf->section == EL_SHN_COMMON) {
            if (elf->size > other->size)
                CompilerTools_ReportDiagnostic(0x3f, found->name, EL_FileName(data_004a292c),
                                               EL_FileName(found->file));
            elf->flags |= 0x20;
        } else if (other->section == EL_SHN_COMMON) {
            if (elf->size < other->size)
                CompilerTools_ReportDiagnostic(0x3f, found->name, EL_FileName(found->file),
                                               EL_FileName(data_004a292c));
            other->flags |= 0x20;
        } else if (object->file->flags & EL_FILE_ARCHIVE) {
            elf->flags |= 0x20;
        } else {
            CompilerTools_ReportDuplicate(found->name, "", EL_FileName(data_004a292c), EL_FileName(found->file));
        }
    }
}

/* Puts a symbol in its section's list, by address (the commons in the extra section after the others, an
   STT_FILE symbol as the object's name). (Not an original function.) */
static void EL_PlaceSymbol(ELObject *object, ELPiece *symbol, ELSymbol *elf) {
    ELSection *section;
    ELPiece *at;
    unsigned short count;

    count = data_004a28dc[data_004a292c].image->section_header_count;
    if (elf->section > count) {
        if (elf->section == EL_SHN_COMMON) {
            section = &object->sections[count];
            if (!section->pieces) {
                section->pieces = symbol;
            } else {
                section->last->next = symbol;
                symbol->prev = section->last;
            }
            section->last = symbol;
        } else if ((elf->info & 0xf) == EL_STT_FILE) {
            object->fileSymbol = symbol;
        }
        return;
    }
    section = &object->sections[elf->section];
    if (!section->pieces) {
        section->pieces = symbol;
        section->last = symbol;
        return;
    }
    if (elf->value < section->last->symbol->value)
        section->last = section->pieces;
    for (at = section->last; at; at = at->next) {
#if LINKER >= 27
        /* (2.7: a global of a size before the labels of its address, after its section's symbol) */
        if (elf->value < at->symbol->value ||
            (elf->value == at->symbol->value && (elf->info >> 4) != 0 && elf->size && !at->symbol->size &&
             (at->symbol->info & 0xf) != EL_STT_SECTION)) {
#else
        if (elf->value < at->symbol->value) {
#endif
            if (!at->prev)
                section->pieces = symbol;
            else
                at->prev->next = symbol;
            symbol->prev = at->prev;
            symbol->next = at;
            at->prev = symbol;
            section->last = symbol;
            return;
        }
        if (!at->next) {
            symbol->prev = at;
            at->next = symbol;
            section->last = symbol;
            return;
        }
        section->last = at;
    }
}

/* Reads the current object's symbols: each becomes a symbol of the link, in its section's list. (Not an
   original function.) */
static void EL_ReadSymbols(ELObject *object) {
    ELSymbol *elf;
    unsigned long *comment;
    ELPiece *symbol;
    ELRoot *forced;
    char *name;
    char binding;
    unsigned long count;
    long i;

    ELF_linker_ReadObjectOptions(data_004a292c);
    if (data_004a2e4e)
        ELF_linker_SwapRelocations(data_004a292c);
    count = object->symbols->header->size >> 4;
    for (i = 1; i < (long)count; i++) {
        elf = (ELSymbol *)(data_004a28dc[data_004a292c].symbols->data) + i;
        if (data_004a2e4e)
            ELF_Endian_ConvertSymbolRecord(elf);
        if (!elf->section)
            continue;
        comment = NULL;
        if (object->comment) {
            comment = (unsigned long *)(object->comment + i * 8);
            CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)comment);
        } else {
            elf->flags = 0;
        }
        name = LK_symbol_ObjectName(data_004a292c, elf);
        if ((elf->info & 0xf) == EL_STT_SECTION && name && !strcmp(name, ".comment")) {
            elf->section = 0;
            continue;
        }
        binding = elf->info >> 4;
        if (binding == 1) {
            EL_CheckGlobal(object, elf, name);
        } else if (binding == 2) {
            symbol = LK_symbol_Find(name, data_004a292c);
            if (symbol) {
                binding = symbol->symbol->info >> 4;
                if (binding == 2 || binding == 1)
                    elf->flags |= 0x20;
            }
        }
        symbol = LK_symbol_Add(name);
        symbol->stName = (unsigned long)elf->piece;
        symbol->x10 = 0;
        symbol->file = data_004a292c;
        symbol->symbol = elf;
        if (comment) {
            memcpy(&symbol->align, comment, 8);
        } else {
            symbol->align = -1;
            symbol->flags = 1;
        }
        elf->piece = symbol;
        if (symbol->flags & 8) {
            forced = (ELRoot *)galloc(sizeof(ELRoot));
            memset(forced, 0, sizeof(ELRoot));
            forced->symbol = symbol->symbol;
            forced->file = symbol->file;
            forced->next = data_004a28e0;
            data_004a28e0 = forced;
        }
        EL_PlaceSymbol(object, symbol, elf);
    }
#if LINKER >= 27
    /* (2.7: an object's commons have a section of their own from the start, its section symbol first) */
    {
        ELSection *commons = &object->sections[object->image->section_header_count];

        if (commons->pieces) {
            elf = (ELSymbol *)galloc(sizeof(ELSymbol));
            memset(elf, 0, sizeof(ELSymbol));
            elf->info = EL_STT_SECTION;
            elf->section = EL_SHN_COMMON;
            symbol = LK_symbol_Add(".bss");
            symbol->x10 = 0;
            symbol->file = data_004a292c;
            symbol->symbol = elf;
            symbol->align = 4;
            if (!object->comment)
                symbol->flags = 1;
            elf->piece = symbol;
            symbol->next = commons->pieces;
            commons->pieces->prev = symbol;
            commons->pieces = symbol;
        }
    }
#endif
}

/* Loads the object IMAGE as the next one (data_004a292c): its sections and, linking (not LIBRARY, and not a
   file named by EXCLUDEFILES or INCLUDEDWARF: SKIP), its symbols. (Not an original function.) */
static void EL_LoadObject(ELFile *file, char *image, char *name, char *member, long size, Boolean library,
                           Boolean skip) {
    ELObject *object;
    ELSection *names;
    Elf32Section *header;
    char *strings;
    Boolean isStrtab;
    long count;
    long i;

    object = &data_004a28dc[data_004a292c];
    object->image = (struct Elf32Header *)image;
    object->memberName = member;
    object->file = file;
    if (!skip)
        object->fileSymbol = LK_symbol_Add(name);
#if LINKER >= 27
    if (member)
        ELF_linker_memberNames[data_004a292c] = name;
#endif
    object->size += size;
    count = data_004a28dc[data_004a292c].image->section_header_count;
    object->sections = (ELSection *)galloc((count + 1) * sizeof(ELSection));
    memset(object->sections, 0, (count + 1) * sizeof(ELSection));
    names = &object->sections[object->image->section_name_index];
    object->sectionNames = names;
    names->header = (Elf32Section *)(image + object->image->section_header_offset +
                                     object->image->section_name_index * sizeof(Elf32Section));
    strings = image + names->header->file_offset;
    names->data = strings;
    names->name = strings + names->header->name;
    names->chunks = (ELChunk *)galloc(sizeof(ELChunk));
    memset(names->chunks, 0, sizeof(ELChunk));
    names->index = data_004a292c;
    names->index = object->image->section_name_index;
    if (!library && !skip)
        ELF_PPC_EABI_AssignSection(object, names);
    isStrtab = 0;
    if (names->name && !strcmp(names->name, ".strtab"))
        isStrtab = 1;
    for (i = 1; i < count; i++) {
        if (i == object->image->section_name_index && !isStrtab)
            continue;
        EL_MakeSection(object, &object->sections[i], image, strings, i, !library && !skip);
        header = object->sections[i].header;
        if (header->type == 2)
            object->symbols = &object->sections[i];
        else if (header->type == 3)
            object->strings = &object->sections[i];
    }
    if (!library && !skip)
        EL_ReadSymbols(object);
    data_004a292c++;
}

#if LINKER >= 27
/* 2.7's Linker Generated Symbol File: an object the linker makes after the link's, of the symbols it defines in
   sections of their own (AddCppRuntimeRoots, 0x436b90): _ctors and _dtors, the start of .ctors and .dtors (in
   .ctors$00 and .dtors$00, first by their priority), and the lists' terminators _ctors$99 and _dtors$99 (a zero
   word each, in .ctors$99 and .dtors$99). Its sections go where their base names do, like the objects' $NN
   sections; the dead stripping keeps them (EL_KeepGenerated). Made as an ELF image and loaded as an object. */
long ELF_linker_generatedFile = -1;
char **ELF_linker_memberNames;

/* Its slots: a section and the symbol in it each. The references (a word of TARGET's address, relocated) are made
   only for the C++ runtime's (AddCppRuntimeRoots' checks, EL_CheckRuntime): one an older runtime has outside its
   $NN section, or (.dtors$10's) __destroy_global_chain's when __fini_cpp_exceptions is not defined. */
typedef struct ELGeneratedSlot {
    char *section;
    char *symbol;
    char *target; /* a reference's */
} ELGeneratedSlot;
static ELGeneratedSlot EL_generatedSlots[7] = {
    {".ctors$00", "_ctors", NULL},
    {".ctors$10", "__init_cpp_exceptions_reference", "__init_cpp_exceptions"},
    {".ctors$99", "_ctors$99", NULL},
    {".dtors$00", "_dtors", NULL},
    {".dtors$10", "__destroy_global_chain_reference", "__destroy_global_chain"},
    {".dtors$15", "__fini_cpp_exceptions_reference", "__fini_cpp_exceptions"},
    {".dtors$99", "_dtors$99", NULL},
};
/* each slot's symbol index (0: not made); whether a reference replaces an older runtime's */
static long EL_generatedIndex[7];
static char EL_generatedOld[7];

/* A word or half word of a symbol record, in the objects' byte order (EL_ReadSymbols converts them). */
static void EL_PutWord(unsigned char *at, unsigned long value, int size) {
    int i;

    for (i = 0; i < size; i++) {
        if (data_004a2e4e)
            at[i] = (unsigned char)(value >> (8 * (size - 1 - i)));
        else
            at[i] = (unsigned char)(value >> (8 * i));
    }
}

/* Adds NAME to a string table at *SIZE, giving its offset. */
static unsigned long EL_AddString(char *table, unsigned long *size, char *name) {
    unsigned long offset = *size;

    strcpy(table + offset, name);
    *size += strlen(name) + 1;
    return offset;
}

/* Whether NAME is defined by an object of the link. */
static Boolean EL_Defined(char *name) {
    ELPiece *piece = LK_symbol_FindGlobal(name, -1);

    return piece && piece->file >= 0 && piece->symbol && piece->symbol->section;
}

/* The file NAME's definition is in (-1: none). */
static long EL_DefinitionFile(char *name) {
    ELPiece *piece = LK_symbol_FindGlobal(name, -1);

    return piece && piece->file >= 0 && piece->symbol && piece->symbol->section ? piece->file : -1;
}

/* A runtime reference NAME: the global's (*FILE set to its file), else a defined local of *FILE's. */
static ELSymbol *EL_RuntimeReference(char *name, long *file) {
    ELPiece *piece = LK_symbol_FindGlobal(name, -1);
    ELObject *object;
    ELSymbol *symbol;
    unsigned long count;
    unsigned long i;

    if (piece && piece->file >= 0 && piece->symbol && piece->symbol->section) {
        *file = piece->file;
        return piece->symbol;
    }
    object = &data_004a28dc[*file];
    if (!object->symbols)
        return NULL;
    count = object->symbols->header->size >> 4;
    for (i = 1; i < count; i++) {
        symbol = &((ELSymbol *)object->symbols->data)[i];
        if ((symbol->info >> 4) == 0 && symbol->section && !strcmp(LK_symbol_Name(*file, symbol), name))
            return symbol;
    }
    return NULL;
}

/* Whether PATH names the file NAME: its end, after a directory separator (0x46b420's test). */
static Boolean EL_PathIs(char *path, char *name) {
    size_t length;
    size_t nameLength = strlen(name);
    char before;

    if (!path)
        return 0;
    length = strlen(path);
    if (length < nameLength || strcmp(path + length - nameLength, name))
        return 0;
    if (length == nameLength)
        return 1;
    before = path[length - nameLength - 1];
    return before == '\\' || before == '/' || before == ':';
}

/* Whether FILE is one of the runtime's NAME (or ALSO, when given): its path or its archive member's name. */
static Boolean EL_RuntimeFile(long file, char *name, char *also) {
    ELObject *object = &data_004a28dc[file];
    char *member = object->memberName ? object->fileSymbol->name : NULL;

    return EL_PathIs(object->file->name, name) || (member && EL_PathIs(member, name)) ||
           (also && (EL_PathIs(object->file->name, also) || (member && EL_PathIs(member, also))));
}

/* The name of the section SYMBOL of FILE is in. */
static char *EL_SectionOf(long file, ELSymbol *symbol) {
    char *name = data_004a28dc[file].sections[symbol->section].name;

    return name ? name : "";
}

/* A runtime reference missing: the error, and the link ends. */
static void EL_RuntimeMissing(void) {
    CompilerTools_ReportLimitedDiagnostic(0xb7);
    CompilerTools_FormatMessageAndLongjmp(0xd, 2);
}

/* 2.7's checks of the C++ runtime (AddCppRuntimeRoots, its .ctors$10, .dtors$10 and .dtors$15 slots): a reference
   missing is an error, one of an older runtime (not in its $NN section) a warning (message 183 both), and the
   generated file then has its own. Gives which references to make. */
static void EL_CheckRuntime(Boolean make[7]) {
    ELSymbol *reference;
    long file;
    long fini;
    Boolean count;

    memset(make, 0, 7);
    if (data_004a2e49 && !data_004a2e4f)
        return;
    /* .ctors$10: __init_cpp_exceptions' */
    if ((file = EL_DefinitionFile("__init_cpp_exceptions")) < 0) {
        make[1] = 1;
    } else {
        reference = EL_RuntimeReference("__init_cpp_exceptions_reference", &file);
        if (!reference) {
            EL_RuntimeMissing();
        } else if (strcmp(EL_SectionOf(file, reference), ".ctors$10")) {
            if (EL_RuntimeFile(file, "__init_cpp_exceptions.o", NULL))
                CompilerTools_ReportDiagnostic(0xb7);
            make[1] = EL_generatedOld[1] = 1;
        }
    }
    /* .dtors$10: __destroy_global_chain's, when __fini_cpp_exceptions is not defined or has an older reference */
    count = 0;
    if ((fini = EL_DefinitionFile("__fini_cpp_exceptions")) < 0) {
        count = 1;
    } else {
        file = fini;
        reference = EL_RuntimeReference("__destroy_global_chain_reference", &file);
        if (reference && file != fini)
            EL_RuntimeMissing();
        if (!reference) {
            EL_RuntimeMissing();
        } else if (!strcmp(EL_SectionOf(file, reference), ".dtors")) {
            if (EL_RuntimeFile(file, "__init_cpp_exceptions.o", "global_destructor_chain.o"))
                CompilerTools_ReportDiagnostic(0xb7);
            count = 1;
            EL_generatedOld[4] = 1;
        }
    }
    if ((file = EL_DefinitionFile("__destroy_global_chain")) < 0) {
        count = 1;
    } else {
        reference = EL_RuntimeReference("__destroy_global_chain_reference", &file);
        if (!reference) {
            EL_RuntimeMissing();
        } else if (strcmp(EL_SectionOf(file, reference), ".dtors$10")) {
            if (EL_RuntimeFile(file, "__init_cpp_exceptions.o", "global_destructor_chain.o"))
                CompilerTools_ReportDiagnostic(0xb7);
            EL_generatedOld[4] = 1;
        }
    }
    make[4] = count;
    if (!make[4])
        EL_generatedOld[4] = 0;
    /* .dtors$15: __fini_cpp_exceptions' */
    if (fini < 0) {
        make[5] = 1;
    } else {
        file = fini;
        reference = EL_RuntimeReference("__fini_cpp_exceptions_reference", &file);
        if (!reference) {
            EL_RuntimeMissing();
        } else if (strcmp(EL_SectionOf(file, reference), ".dtors$15")) {
            if (EL_RuntimeFile(file, "__init_cpp_exceptions.o", NULL))
                CompilerTools_ReportDiagnostic(0xb7);
            make[5] = EL_generatedOld[5] = 1;
        }
    }
}

static void EL_LoadGenerated(void) {
    /* sections: 0, .shstrtab, .strtab, .symtab, the slots', then the references' relocations; symbols: 0, the
       slots' sections', the slots', then the references' targets (undefined) */
    struct Elf32Header *header;
    Elf32Section *sections;
    ELFile *file;
    unsigned char *image;
    unsigned char *symbol;
    char *names;
    char *strings;
    unsigned long nameSize;
    unsigned long stringSize;
    unsigned long offset;
    unsigned long size;
    Boolean make[7];
    long slots[7];
    long count;
    long references;
    long symbols;
    long sectionCount;
    long target;
    long rela;
    long i;
    long j;
    char relaName[32];

    memset(EL_generatedIndex, 0, sizeof(EL_generatedIndex));
    memset(EL_generatedOld, 0, sizeof(EL_generatedOld));
    EL_CheckRuntime(make);
    count = 0;
    references = 0;
    for (i = 0; i < 7; i++) {
        if (EL_generatedSlots[i].target && !make[i])
            continue;
        if (EL_generatedSlots[i].target)
            references++;
        slots[count++] = i;
    }
    symbols = 1 + 2 * count + references;
    sectionCount = 4 + count + references;
    size = 0x34 + sectionCount * sizeof(Elf32Section) + 0x100 + 0x200 + symbols * 0x10 + count * 4 + references * 12;
    image = galloc(size);
    memset(image, 0, size);
    header = (struct Elf32Header *)image;
    header->magic[0] = 0x7f;
    header->magic[1] = 'E';
    header->magic[2] = 'L';
    header->magic[3] = 'F';
    header->elfClass = 1;
    header->dataEncoding = data_004a2e4e ? 2 : 1;
    header->identificationRest[0] = 1;
    header->type = 1;
    header->machine = 0x14;
    header->version = 1;
    header->section_header_offset = 0x34;
    header->header_size = 0x34;
    header->section_header_size = sizeof(Elf32Section);
    header->section_header_count = (unsigned short)sectionCount;
    header->section_name_index = 1;
    sections = (Elf32Section *)(image + 0x34);
    offset = 0x34 + sectionCount * sizeof(Elf32Section);

    names = (char *)image + offset;
    nameSize = 1;
    sections[1].name = EL_AddString(names, &nameSize, ".shstrtab");
    sections[2].name = EL_AddString(names, &nameSize, ".strtab");
    sections[3].name = EL_AddString(names, &nameSize, ".symtab");
    for (j = 0; j < count; j++)
        sections[4 + j].name = EL_AddString(names, &nameSize, EL_generatedSlots[slots[j]].section);
    rela = 4 + count;
    for (j = 0; j < count; j++) {
        if (!EL_generatedSlots[slots[j]].target)
            continue;
        strcpy(relaName, ".rela");
        strcat(relaName, EL_generatedSlots[slots[j]].section);
        sections[rela++].name = EL_AddString(names, &nameSize, relaName);
    }
    sections[1].type = 3;
    sections[1].file_offset = offset;
    sections[1].size = nameSize;
    sections[1].alignment = 1;
    offset += 0x100;

    strings = (char *)image + offset;
    stringSize = 1;
    sections[2].type = 3;
    sections[2].file_offset = offset;
    sections[2].alignment = 1;
    offset += 0x200;

    symbol = image + offset;
    sections[3].type = 2;
    sections[3].file_offset = offset;
    sections[3].size = symbols * 0x10;
    sections[3].link = 2;
    sections[3].info = 1 + count;
    sections[3].alignment = 4;
    sections[3].entry_size = 0x10;
    for (j = 0; j < count; j++) {
        i = slots[j];
        /* the section's symbol */
        EL_PutWord(symbol + (1 + j) * 0x10 + 0xc, 3, 1);
        EL_PutWord(symbol + (1 + j) * 0x10 + 0xe, 4 + j, 2);
        /* _ctors and _dtors: no type, no size; the terminators and the references: an object of a word (a reference
           local while the link reads it, beside the objects' own, global once kept) */
        EL_PutWord(symbol + (1 + count + j) * 0x10,
                   EL_AddString(strings, &stringSize, EL_generatedSlots[i].symbol), 4);
        EL_PutWord(symbol + (1 + count + j) * 0x10 + 8, i == 0 || i == 3 ? 0 : 4, 4);
        EL_PutWord(symbol + (1 + count + j) * 0x10 + 0xc,
                   EL_generatedSlots[i].target ? 0x01 : i == 0 || i == 3 ? 0x10 : 0x11, 1);
        EL_PutWord(symbol + (1 + count + j) * 0x10 + 0xe, 4 + j, 2);
        EL_generatedIndex[i] = 1 + count + j;
    }
    target = 1 + 2 * count;
    for (j = 0; j < count; j++) {
        if (!EL_generatedSlots[slots[j]].target)
            continue;
        EL_PutWord(symbol + target * 0x10, EL_AddString(strings, &stringSize, EL_generatedSlots[slots[j]].target), 4);
        EL_PutWord(symbol + target * 0x10 + 0xc, 0x10, 1);
        target++;
    }
    sections[2].size = stringSize;
    offset += symbols * 0x10;

    for (j = 0; j < count; j++) {
        i = slots[j];
        sections[4 + j].type = 1;
        sections[4 + j].flags = 2;
        sections[4 + j].alignment = 4;
        sections[4 + j].file_offset = offset;
        if (i != 0 && i != 3) {
            sections[4 + j].size = 4;
            offset += 4;
        }
    }
    /* the references' relocations: R_PPC_ADDR32 of their targets */
    rela = 4 + count;
    target = 1 + 2 * count;
    for (j = 0; j < count; j++) {
        if (!EL_generatedSlots[slots[j]].target)
            continue;
        sections[rela].type = 4;
        sections[rela].file_offset = offset;
        sections[rela].size = 12;
        sections[rela].link = 3;
        sections[rela].info = 4 + j;
        sections[rela].alignment = 4;
        sections[rela].entry_size = 12;
        EL_PutWord(image + offset + 4, (unsigned long)target << 8 | 1, 4);
        offset += 12;
        rela++;
        target++;
    }

    file = (ELFile *)galloc(sizeof(ELFile));
    memset(file, 0, sizeof(ELFile));
    file->name = "Linker Generated Symbol File";
    file->objectName = file->name;
    ELF_linker_generatedFile = data_004a292c;
    EL_LoadObject(file, (char *)image, file->name, NULL, size, 0, 0);
}

/* 2.7's Linker Generated Symbol File has an extabindex section too (its _eti_init_info): it is placed like an
   object's, so that the command file's lack of one is reported for it when no object's was. (Not an original
   function: the section itself is not made.) */
extern ELOutput *ELF_PPC_EABI_ChooseOutput(ELObject *object, ELSection *section);

static void EL_PlaceGeneratedIndex(void) {
    ELObject *object;
    ELSection section;
    Elf32Section header;

    if (ELF_linker_generatedFile < 0)
        return;
    object = &data_004a28dc[ELF_linker_generatedFile];
    if (!object->fileSymbol)
        return;
    memset(&section, 0, sizeof(section));
    memset(&header, 0, sizeof(header));
    header.type = 1;
    header.flags = 2;
    header.alignment = 4;
    section.name = "extabindex";
    section.header = &header;
    section.file = ELF_linker_generatedFile;
    ELF_PPC_EABI_ChooseOutput(object, &section);
}

#endif

/* Marks the command file's entry naming FILE with its object's number (index + 1). (Not an original
   function.) */
static void EL_MarkFileName(ELFileName *names, ELFile *file) {
    for (; names; names = names->next) {
        if (file == names->match) {
            names->index = data_004a292c + 1;
            break;
        }
    }
}

/* Reports the command file's files that no object of the link is (0x89, 0x8a with a member). (Not an
   original function.) */
static void EL_ReportMissingFiles(void) {
    ELFileName *name;
    ELCmdSection *section;
    ELCmdItem *item;

    for (name = data_004a28e4->forceFiles; name; name = name->next) {
        if (!name->found) {
            if (name->member)
                CompilerTools_ReportDiagnostic(0x8a, name->name, name->member);
            else
                CompilerTools_ReportDiagnostic(0x89, name->name);
        }
    }
    for (section = data_004a28e4->sections; section; section = section->next) {
        if (section->kind != 1)
            continue;
        for (item = section->items; item; item = item->next) {
            if (item->kind == 1 && !item->found) {
                if (item->member)
                    CompilerTools_ReportDiagnostic(0x8a, item->file, item->member);
                else if (item->file)
                    CompilerTools_ReportDiagnostic(0x89, item->file);
            }
        }
    }
}

/* Loads the objects of the files read (members, containers, .BINARY files) and the linker-defined symbols. */
/* 0x432d70 */
void ELF_linker_LoadObjects(Boolean library) {
    /* each object, each needed member of an archive, each entry of a kind 6 container; then, linking (not
       LIBRARY), the symbols of the linker-defined ones */
    ELFile *file;
    ELMember *member;
    char **longNames;
    short *container;
    long *entry;
    char *image;
    char *name;
    char *memberName;
    long size;
    Boolean skip;
#if LINKER >= 27
    ELPiece *generatedBefore;
    ELPiece *generatedLast;
#endif
    long i;

    data_004a292c = 0;
    for (file = data_004a2764; file; file = file->next) {
        skip = 0;
        EL_CHECK_BREAK();
        image = *file->member->data;
        size = EL_HandleSize(file->member->data);
        name = file->objectName;
        memberName = NULL;
        member = NULL;
        container = NULL;
        entry = NULL;
        longNames = NULL;
        if ((file->flags2 & EL_FILE2_EXCLUDED) || (file->flags2 & EL_FILE2_DWARF)) {
            skip = 1;
            if (file->flags2 & EL_FILE2_EXCLUDED)
                EL_MarkFileName(data_004a28e4->excludeFiles, file);
            if (file->flags2 & EL_FILE2_DWARF)
                EL_MarkFileName(data_004a28e4->includeDwarf, file);
        } else if (file->flags & EL_FILE_ARCHIVE) {
            container = (short *)image;
            member = file->member;
            if (member->kind == 6) {
                entry = (long *)(image + 0xc);
                image += 0x10;
                container[1]--;
            } else if ((unsigned char)(member->kind - 1) <= 1) {
                container = NULL;
                longNames = NULL;
                memberName = member->name;
                name = Archive_MemberName(NULL, memberName);
                if (!strcmp(name, "//")) {
                    longNames = member->data;
                    Archive_EndLongNames(longNames, size);
                    member->kind = 3;
                }
                image = *member->data;
                if (member->kind != 2)
                    goto next;
            } else {
                CompilerTools_ReportLimitedDiagnostic(0x3e, file->name);
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
            }
        } else if ((file->flags & EL_FILE_BINARY) && !library) {
            EL_LoadBinary(file, name, image, size);
            continue;
        }
        for (;;) {
            EL_LoadObject(file, image, name, memberName, size, library, skip);
        next:
            if (!(file->flags & EL_FILE_ARCHIVE))
                break;
            if (container) {
                if (!container[1])
                    break;
                container[1]--;
                entry = (long *)(image + *entry);
                image = (char *)(entry + 1);
                continue;
            }
            if (file->member->kind != 1)
                break;
            for (;;) {
                member = member->next;
                if (!member)
                    break;
                memberName = member->name;
                name = Archive_MemberName(longNames, memberName);
                size = EL_HandleSize(member->data);
                if (!strcmp(name, "//")) {
                    longNames = member->data;
                    Archive_EndLongNames(longNames, size);
                    member->kind = 3;
                }
                image = *member->data;
                if (member->kind == 2)
                    break;
            }
            if (!member)
                break;
        }
    }
#if LINKER >= 27
    ELF_linker_generatedFile = -1;
    generatedBefore = LK_symbol_Last();
    if (!library && (!data_004a2e49 || data_004a2e4f)) {
        EL_LoadGenerated();
        EL_PlaceGeneratedIndex();
    }
    generatedLast = LK_symbol_Last();
#endif
    if (library)
        return;
    ELF_PPC_EABI_AddSectionSymbols();
    {
        ELLinkSymbol *symbol;

        for (symbol = data_004a2934; symbol; symbol = symbol->next)
            EL_DefineLinkSymbol(symbol->name, symbol->index, &symbol->piece, 0x35f);
    }
    for (i = 0; LinkSym_Table[i].name; i++) {
#if LINKER >= 27
        /* (2.7's generated symbols come after the command file's and the linker's own, before _eti_init_info) */
        if (LinkSym_Table[i].index == -12 && generatedLast != generatedBefore)
            LK_symbol_MoveToEnd(generatedBefore, generatedLast);
#endif
        EL_DefineLinkSymbol(LinkSym_Table[i].name, LinkSym_Table[i].index, &LinkSym_Table[i].piece, 0x380);
    }
    if (data_004a2e32)
        EL_ReportMissingFiles();
}

/* Swaps two symbols of a section's list. */
/* 0x432ce0 */
void ELF_linker_SwapSymbols(ELPiece *a, ELPiece *b) {
    ELPiece *bNext;
    ELPiece *aPrev;

    if (!a->prev)
        EL_ASSERT(0x3cc);
#if LINKER >= 27
    /* (neighbours: B moved before A; 2.7's lists reach this) */
    if (a->next == b) {
        a->prev->next = b;
        b->prev = a->prev;
        a->next = b->next;
        if (b->next)
            b->next->prev = a;
        b->next = a;
        a->prev = b;
        return;
    }
#endif
    bNext = b->next;
    aPrev = a->prev;
    if (a->next == b->prev) {
        b->next = a;
        a->prev = b;
    } else {
        b->next = a->next;
        a->prev = b->prev;
        b->next->prev = b;
        a->prev->next = a;
    }
    b->prev = aPrev;
    a->next = bNext;
    if (b->prev)
        b->prev->next = b;
    if (a->next)
        a->next->prev = a;
}

/* Moves SYMBOL out of its list to just before AT. (Not an original function.) */
static void EL_MoveBefore(ELPiece *symbol, ELPiece *at) {
    symbol->prev->next = symbol->next;
    if (symbol->next)
        symbol->next->prev = symbol->prev;
    symbol->next = at;
    if (at->prev)
        at->prev->next = symbol;
    symbol->prev = at->prev;
    at->prev = symbol;
}

/* Cuts an input section into atoms (one per section symbol) and gives out its labels and relocations. */
/* 0x432710 */
#if LINKER >= 27
/* The section ELF_linker_MakeCommonSections made for FILE's commons (NULL: none yet). (Not an original function.) */
static ELSection *EL_CommonSection(long file) {
    ELObject *object = &data_004a28dc[file];
    ELSection *section = &object->sections[object->image->section_header_count - 1];

    return section->name && !strcmp(section->name, ".bss") && section->index == object->image->section_header_count - 1 &&
                   section->header && section->header->type == 8 && section->header->name == 0
               ? section
               : NULL;
}
#endif

void ELF_linker_SplitSection(ELSection *section, Boolean unused) {
    /* each symbol of no size gets the sized one it is in, each relocation the symbol whose bytes it is in;
       UNUSED is not read (the callers pass 1 or a flag) */
    ELSection *relocations;
    ELPiece *first;
    ELPiece *symbol;
    ELPiece *prev;
    ELPiece *owner;
    ELPiece *last;
    ELPiece *sectionSymbol;
#if LINKER >= 27
    ELPiece *next;
    ELPiece *moved;
#endif
    ELChunk *atom;
    ELRelocNode *relocation;
    ELSymbol *elf;
    ELRelocation *rela;
    unsigned long i;

    relocations = section->relocations;
    section->split = 1;
    first = section->pieces;
    if (!first)
        EL_ASSERT(0x415);
    elf = first->symbol;
    if ((elf->info & 0xf) != EL_STT_SECTION || elf->value != 0) {
        /* the section symbol at 0 goes first */
        for (symbol = first->next; symbol; symbol = symbol->next) {
            if ((symbol->symbol->info & 0xf) == EL_STT_SECTION && symbol->symbol->value == 0) {
                EL_MoveBefore(symbol, first);
                section->pieces = symbol;
                break;
            }
        }
        first = section->pieces;
        elf = first->symbol;
    }
    section->chunks->pieces = first;
    atom = section->chunks;
    first->chunk = atom;
    atom->whole = section->whole || (first->flags & 1);
    if (section->header->size)
        elf->size = section->header->size;
    else
        section->header->size = elf->size;
    if (section->chunks->pieces->align < (long)section->header->alignment)
        section->chunks->pieces->align = section->header->alignment;

    /* an atom from each section symbol */
    prev = NULL;
    for (symbol = first->next; symbol; prev = symbol, symbol = symbol->next) {
        elf = symbol->symbol;
        if ((elf->info & 0xf) == EL_STT_SECTION) {
            if (!section->x38 && prev && elf->value == prev->symbol->value &&
                (prev->symbol->info & 0xf) != EL_STT_SECTION)
                EL_MoveBefore(symbol, prev);
            atom->next = (ELChunk *)galloc(sizeof(ELChunk));
            memset(atom->next, 0, sizeof(ELChunk));
            atom->next->pieces = symbol;
            atom->size = elf->value - atom->pieces->symbol->value;
            atom->pieces->symbol->size = atom->size;
            atom = atom->next;
            atom->whole = section->whole || (symbol->flags & 1);
        }
        symbol->chunk = atom;
    }
    atom->size = section->header->size - atom->pieces->symbol->value;
    atom->pieces->symbol->size = atom->size;

    /* the symbols of no size */
    for (atom = section->chunks; atom; atom = atom->next) {
        first = atom->pieces;
        if (first->flags == 1) {
            for (symbol = first->next; symbol && symbol->chunk == atom; symbol = symbol->next) {
                if (!symbol->symbol->size) {
                    symbol->owner = symbol->chunk->pieces;
                    symbol->offset = symbol->symbol->value - symbol->owner->symbol->value;
                }
            }
        } else if (!(section->header->flags & EL_SHF_EXECINSTR)) {
            if (section->x38)
                continue;
            for (symbol = first->next; symbol && symbol->chunk == atom; symbol = symbol->next) {
                if (symbol->symbol->size)
                    continue;
                symbol->owner = symbol->chunk->pieces;
                symbol->offset = symbol->symbol->value - symbol->owner->symbol->value;
#if LINKER >= 27
                /* (2.7: in the commons' section, after the one of a size at its address) */
                if (section == EL_CommonSection(section->file)) {
                    next = symbol->next;
                    if (next && next->chunk == atom && next->symbol->size &&
                        next->symbol->value == symbol->symbol->value) {
                        ELF_linker_SwapSymbols(symbol, next);
                        symbol = next;
                    }
                } else if (symbol->flags != 1 && symbol->chunk->pieces->next != symbol)
                    EL_MoveBefore(symbol, symbol->chunk->pieces->next);
#else
                if (symbol->flags != 1 && symbol->chunk->pieces->next != symbol)
                    EL_MoveBefore(symbol, symbol->chunk->pieces->next);
#endif
            }
        } else {
            prev = first;
            for (symbol = first->next; symbol && symbol->chunk == atom; symbol = symbol->next) {
                if (symbol->symbol->size) {
                    prev = symbol;
                    continue;
                }
                if (symbol->flags != 1 && (symbol->flags & 0x10)) {
                    symbol->owner = symbol->chunk->pieces;
                    symbol->offset = symbol->symbol->value - symbol->owner->symbol->value;
                    if (symbol->flags != 1 && symbol->chunk->pieces->next != symbol)
                        EL_MoveBefore(symbol, symbol->chunk->pieces->next);
                    prev = symbol;
                    continue;
                }
                if (prev == atom->pieces) {
                    /* a label before the first function: it goes after the function it is in */
                    for (prev = symbol->next; prev; prev = prev->next) {
                        if (prev->symbol->size) {
                            if (prev->symbol->value < symbol->symbol->value ||
                                symbol->symbol->value >= prev->symbol->value + prev->symbol->size)
                                EL_ASSERT(0x468);
                            ELF_linker_SwapSymbols(symbol, prev);
                            break;
                        }
                    }
                }
#if LINKER >= 27
                /* (2.7: a label past the function before it, at the start of the next, goes with the next) */
                else if (symbol->symbol->value >= prev->symbol->value + prev->symbol->size) {
                    for (next = symbol->next; next && next->chunk == atom && !next->symbol->size; next = next->next)
                        ;
                    if (next && next->chunk == atom && next->symbol->value <= symbol->symbol->value &&
                        symbol->symbol->value < next->symbol->value + next->symbol->size) {
                        /* (moved to just after it; ELF_linker_SwapSymbols can't swap neighbours) */
                        moved = symbol;
                        symbol = symbol->prev;
                        moved->prev->next = moved->next;
                        moved->next->prev = moved->prev;
                        moved->next = next->next;
                        moved->prev = next;
                        if (next->next)
                            next->next->prev = moved;
                        next->next = moved;
                        moved->owner = next;
                        moved->offset = moved->symbol->value - next->symbol->value;
                        continue;
                    }
                }
#endif
                symbol->owner = prev;
                symbol->offset = symbol->symbol->value - symbol->owner->symbol->value;
            }
        }
    }

    /* the relocations */
    if (!relocations)
        return;
    last = NULL;
    for (i = 0; i < relocations->header->size / 12; i++) {
        rela = (ELRelocation *)(relocations->data + i * 12);
        relocation = (ELRelocNode *)galloc(12);
        relocation->rel = rela;
        relocation->next = NULL;
        for (atom = section->chunks; atom; atom = atom->next) {
            if (rela->offset >= atom->pieces->symbol->value && rela->offset < atom->pieces->symbol->value + atom->size)
                break;
        }
        if (!atom)
            EL_ASSERT(0x4a1);
        if (last && last->chunk == atom && rela->offset >= last->symbol->value)
            symbol = last;
        else
            symbol = atom->pieces;
        sectionSymbol = NULL;
        for (; symbol && symbol->chunk == atom; symbol = symbol->next) {
            if (!symbol->symbol->size) {
                if (section->x38)
                    continue;
                owner = symbol->owner;
            } else {
                owner = symbol;
            }
            if (rela->offset < owner->symbol->value || rela->offset >= owner->symbol->value + owner->symbol->size)
                continue;
            last = symbol;
            if ((owner->symbol->info & 0xf) == EL_STT_SECTION) {
                sectionSymbol = owner;
                continue;
            }
#if LINKER >= 27
            /* (2.7: a symbol's relocations by their offsets, those of one offset in the file's order) */
            if (owner->relocations && rela->offset < owner->lastRelocation->rel->offset) {
                ELRelocNode **at;

                for (at = &owner->relocations; (*at)->rel->offset <= rela->offset; at = &(*at)->next)
                    ;
                relocation->next = *at;
                *at = relocation;
                relocation = NULL;
                break;
            }
#endif
            if (!owner->relocations)
                owner->relocations = relocation;
            else
                owner->lastRelocation->next = relocation;
            owner->lastRelocation = relocation;
            relocation = NULL;
            break;
        }
        if (relocation && sectionSymbol) {
            if (!sectionSymbol->relocations)
                sectionSymbol->relocations = relocation;
            else
                sectionSymbol->lastRelocation->next = relocation;
            sectionSymbol->lastRelocation = relocation;
        }
    }
}

#define EL_SYMBOLS(f) ((ELSymbol *)data_004a28dc[f].symbols->data)

#define EL_FILENAME(f) ((f) > -1 ? data_004a28dc[f].file->name : "linker generated symbol")

#define EL_MEMBERNAME(f) ((f) > -1 && data_004a28dc[f].memberName ? data_004a28dc[f].fileSymbol->name : "")

#define EL_TYPENAME(info)                                                                                             \
    (((info) & 0xf) == 0   ? "notype"                                                                                  \
     : ((info) & 0xf) == 1 ? "object"                                                                                  \
     : ((info) & 0xf) == 2 ? "func"                                                                                    \
     : ((info) & 0xf) == 3 ? "section"                                                                                 \
     : ((info) & 0xf) == 4 ? "file"                                                                                    \
                           : "unknown")

#define EL_BINDNAME(info)                                                                                             \
    (((info) >> 4) == 0     ? "local"                                                                                  \
     : ((info) >> 4) == 1   ? "global"                                                                                 \
     : ((info) >> 4) == 2   ? "weak"                                                                                   \
     : ((info) >> 4) == 0xe ? "overload"                                                                               \
     : ((info) >> 4) == 0xd ? "multidef"                                                                               \
                            : "unknown")

/* The symbol closure in the map: 2.7 shows it only with -listclosure (the Linker panel's listclosure). */
#if LINKER >= 27
extern Boolean ELF_linker_listClosure;
#define EL_LIST_CLOSURE ELF_linker_listClosure
#else
#define EL_LIST_CLOSURE 1
#endif

/* The map's indentation of a depth and the depth. */
#define EL_INDENT(depth)                                                                                              \
    {                                                                                                                  \
        int i_;                                                                                                        \
        for (i_ = 0; i_ <= (depth); i_++)                                                                              \
            LK_symbol_Print(" ");                                                                                          \
        LK_symbol_Print("%i] ", (depth));                                                                                  \
    }

/* A needed dropped duplicate: swaps with the kept definition if that file allows it and it is unused, else warns. */
/* 0x4325a0 */
void ELF_linker_ResolveDuplicate(ELPiece *piece) {
    ELObject *object;
    ELObject *other;
    ELPiece *global;

    object = &data_004a28dc[piece->file];
    if (object->file->flags2 & 8)
        return;
    if (object->hasComment)
        return;
    global = LK_symbol_Find(piece->name, piece->file);
    if (!global)
        return;
    other = &data_004a28dc[global->file];
    if (other == object)
        EL_ASSERT(0x4e5);
    if ((other->file->flags & 4) && !(global->symbol->flags & 0x10)) {
        piece->symbol->flags &= ~0x20;
        global->symbol->flags |= 0x20;
        return;
    }
    CompilerTools_ReportDuplicate(piece->name, "", EL_FILENAME(piece->file), EL_FILENAME(global->file));
}

/* Writes to the map the dropped duplicates of a kept symbol's name. */
/* 0x4323a0 */
void ELF_linker_MapDuplicates(char *name, long file, ELSymbol *symbol, long depth) {
    Boolean first;
    ELPiece *self;
    ELPiece *global;

    first = 1;
    self = symbol->piece;
    for (global = LK_symbol_FindAny(name); global; global = global->hashNext) {
        if (strcmp(name, global->name) || global == self || !(global->symbol->flags & 0x20))
            continue;
        if (first) {
            first = 0;
            EL_INDENT(depth);
            LK_symbol_Print(">>> UNREFERENCED DUPLICATE %s\r\n", name);
        }
        EL_INDENT(depth);
        LK_symbol_Print(">>> (%s,%s) found in %s %s\r\n", EL_TYPENAME(global->symbol->info),
                    EL_BINDNAME(global->symbol->info), EL_FILENAME(global->file), EL_MEMBERNAME(global->file));
    }
}

/* Keeps a symbol (file < 0: a linker-defined one) and, recursively, what it refers to, writing each to the map. */
/* 0x431d20 */
void ELF_linker_KeepSymbol(long file, ELSymbol *symbol, long depth) {
    ELObject *object;
    ELLinkSymbol *linkSymbol;
    ELSection *section;
    ELPiece *piece;
    ELPiece *global;
    ELPiece *other;
    ELPiece *related;
    ELChunk *chunk;
    ELRelocNode *node;
    ELRelocation *rel;
    ELSymbol *target;
    char *name;
    long targetFile;
    unsigned char kind;

    if (file < 0) {
        if (!symbol || (symbol->flags & 0x20))
            EL_ASSERT(0x525);
        linkSymbol = LinkSym_FindIndex(file);
        if (!linkSymbol->made) {
            linkSymbol->made = 1;
            symbol->flags |= 0x10;
            ELF_linker_KeepLinkSymbol(file, "<entry point>");
        }
        if (EL_LIST_CLOSURE) {
            EL_INDENT(depth);
#if LINKER >= 27
            /* (2.7: _eti_init_info is its Linker Generated Symbol File's) */
            if (file == -12 && ELF_linker_generatedFile >= 0)
                LK_symbol_Print("%s (object,global) found in %s %s\r\n", linkSymbol->name,
                                EL_FILENAME(ELF_linker_generatedFile), EL_MEMBERNAME(ELF_linker_generatedFile));
            else
#endif
            LK_symbol_Print("%s found as linker generated symbol\r\n", linkSymbol->name);
        }
        related = linkSymbol->piece->related;
        if (related && !(related->symbol->flags & 0x10))
            ELF_linker_KeepSymbol(related->file, related->symbol, depth + 1);
        return;
    }

    if (!symbol) {
        EL_ASSERT(0x53a);
        return;
    }
    if (symbol->flags & 0x20)
        return;
    if (symbol->section == 0) {
        LK_symbol_Name(file, symbol);
        EL_ASSERT(0x544);
        return;
    }

    object = &data_004a28dc[file];
    name = LK_symbol_Name(file, symbol);
    symbol->flags |= 0x10;
    if (EL_LIST_CLOSURE) {
        EL_INDENT(depth);
        LK_symbol_Print("%s (%s,%s) found in %s %s\r\n", name, EL_TYPENAME(symbol->info), EL_BINDNAME(symbol->info),
                    EL_FILENAME(file), EL_MEMBERNAME(file));
        if ((symbol->info & 0xf) != 3)
            ELF_linker_MapDuplicates(name, file, symbol, depth);
    }
    object->forced = 1;
    object->file->flags = (object->file->flags & ~1) | (object->forced & 1);
    if (symbol->section >= 0xff00)
        return;

    section = &object->sections[symbol->section];
    if (!section->split)
        ELF_linker_SplitSection(section, !object->hasComment);
    piece = symbol->piece;
    if (symbol->section != 0xfff2) {
        piece->chunk->kept = 1;
        section->kept = piece->chunk->kept;
    }

    for (node = piece->relocations; node; node = node->next) {
        rel = node->rel;
        target = &EL_SYMBOLS(file)[rel->info >> 8];
        if (target->flags & 0x10)
            continue;
        global = NULL;
        if (target->section != 0 && !(target->flags & 0x20)) {
            global = target->piece;
        } else if ((target->info >> 4) == 1 || (target->info >> 4) == 2) {
            global = LK_symbol_Find(LK_symbol_Name(file, target), file);
#if LINKER >= 27
            /* (2.7: no _eti_init_info in a partial link without -r1) */
            if (global && global->file == -12 && data_004a2e49 && !data_004a2e4f)
                global = NULL;
#endif
        }

        if (!global) {
            LK_symbol_Print(">>> SYMBOL NOT FOUND: %s\r\n", LK_symbol_Name(file, target));
            if (!data_004a2e5a) {
                if (!object->hasComment)
                    target->flags = 0;
#if LINKER >= 27
                /* (2.7: its own definition, which an excluded file's overrode, kept, as a global of no type and no
                   .comment flags) */
                if (target->section != 0) {
                    target->flags &= ~0x20;
                    target->info = 0x10;
                    if (target->piece)
                        target->piece->commentFlags = 0;
                    ELF_linker_KeepSymbol(file, target, depth + 1);
                    continue;
                }
#endif
                global = LK_symbol_Add(LK_symbol_Name(file, target));
                global->stName = (unsigned long)target->piece; /* the undefined symbol's first word */
                global->x10 = 0;
                global->file = file;
                if (target->section != 0) {
                    if (!(target->flags & 0x20))
                        EL_ASSERT(0x592);
                    target = (ELSymbol *)galloc(sizeof(ELSymbol));
                    memset(target, 0, sizeof(ELSymbol));
                    target->info = 0x10;
                }
                target->flags |= 0x10;
                target->flags |= 0x40;
                global->symbol = target;
                target->piece = global;
#if LINKER >= 27
                /* (2.7: among its object's symbols, after the one before it) */
                {
                    ELSymbol *before;

                    for (before = &EL_SYMBOLS(file)[(rel->info >> 8) - 1]; before > EL_SYMBOLS(file); before--) {
                        if (before->section != 0 || (before->flags & 0x40)) {
                            LK_symbol_MoveAfter(before->piece);
                            break;
                        }
                    }
                }
#endif
            } else {
                data_004a28f4++;
                CompilerTools_ReportUndefined(LK_symbol_Name(file, target), 0, name, EL_FILENAME(file));
            }
            continue;
        }

        if (global->symbol && !ELF_PPC_EABI_CheckSdaReference(rel, global, name, file))
            continue;
        if (target != global->symbol) {
            target->flags |= 0x10;
            target = global->symbol;
        }
        targetFile = global->file;
        if (target && !(target->flags & 0x10)) {
            target->flags |= 0x10;
            ELF_linker_KeepSymbol(targetFile, target, depth + 1);
        }
    }

    if (piece->owner == piece->chunk->pieces)
        piece->chunk->whole = 1;
    chunk = piece->chunk;
    kind = section->kind;
    if ((chunk->whole || !data_004a2e39) && kind != 7 && kind != 6 && kind != 0x1c && kind != 0x1d && !chunk->done) {
        chunk->done = 1;
        other = chunk->pieces;
        if (other->symbol->flags & 0x20)
            ELF_linker_ResolveDuplicate(other);
        chunk->pieces->symbol->flags |= 0x10;
        for (other = chunk->pieces->next; other && other->chunk == chunk; other = other->next) {
            if (other->symbol->flags & 0x10)
                continue;
            if (other->symbol->flags & 0x20)
                ELF_linker_ResolveDuplicate(other);
            ELF_linker_KeepSymbol(file, other->symbol, depth + 1);
        }
        ELF_linker_KeepSymbol(file, chunk->pieces->symbol, depth + 1);
    }

#if LINKER >= 27
    /* (2.7: its Linker Generated Symbol File's labels' sections kept, not listed) */
    if (piece->owner && file == ELF_linker_generatedFile && (piece->owner->symbol->info & 0xf) == 3)
        piece->owner->symbol->flags |= 0x10;
#endif
    if (piece->owner && !(piece->owner->symbol->flags & 0x10))
        ELF_linker_KeepSymbol(file, piece->owner->symbol, depth + 1);
    related = piece->related;
    if (related && !(related->symbol->flags & 0x10))
        ELF_linker_KeepSymbol(related->file, related->symbol, depth + 1);
}

/* The exception table entries (kind 7 sections) as roots, also grouped by output section into data_004a2954. */
/* 0x431960 */
ELRoot *ELF_linker_ExceptionRoots(void) {
    ELGroup *groups;
    ELGroup *lastGroup;
    ELGroup *group;
    ELRoot **tail;
    ELRoot *root;
    ELObject *object;
    ELSection *section;
    ELChunk *chunk;
    ELPiece *piece;
    ELSymbol *symbol;
    ELRelocNode *node;
    ELRelocation *rel;
    long file;
    unsigned long i;
    long n;

    groups = NULL;
    lastGroup = NULL;
    data_004a2954 = NULL;
    data_004a2948 = NULL;
    data_004a28d4 = 0;
    tail = &data_004a2948;
    for (file = 0; file < data_004a292c; file++) {
        EL_CHECK_BREAK();
        object = &data_004a28dc[file];
        if (object->file->flags2 & 8)
            continue;
        for (i = 1; (long)i < data_004a28dc[file].image->section_header_count; i++) {
            section = &object->sections[i];
            if (section->kind != 7 || !section->pieces)
                continue;
            if (!section->split)
                ELF_linker_SplitSection(section, 0);
            for (chunk = section->chunks; chunk; chunk = chunk->next) {
                for (piece = chunk->pieces; piece && piece->chunk == chunk; piece = piece->next) {
                    symbol = piece->symbol;
                    if (symbol->section != i || (symbol->info & 0xf) == 3 || !symbol->size || (symbol->flags & 0x20))
                        continue;
                    root = (ELRoot *)galloc(sizeof(ELRoot));
                    memset(root, 0, sizeof(ELRoot));
                    root->symbol = symbol;
                    root->file = file;
                    root->entry = section->data + symbol->value;
                    for (node = piece->relocations; node; node = node->next) {
                        rel = node->rel;
                        if (root->symbol->value == rel->offset) {
                            if (root->rel0)
                                EL_ASSERT(0x608);
                            root->rel0 = rel;
                            root->target = &EL_SYMBOLS(root->file)[rel->info >> 8];
                        } else if (root->symbol->value + 8 == rel->offset) {
                            if (root->rel8)
                                EL_ASSERT(0x60d);
                            root->rel8 = rel;
                        } else {
                            EL_ASSERT(0x611);
                        }
                    }
                    if (!root->target) {
                        CompilerTools_ReportLimitedDiagnostic(0x41, "exception function", EL_FILENAME(root->file));
                        data_004a28f4++;
                        continue;
                    }
                    root->key = object->sections[root->target->section].output;
                    for (group = groups; group && root->key != group->key; group = group->next)
                        ;
                    if (!group) {
                        group = (ELGroup *)galloc(sizeof(ELGroup));
                        memset(group, 0, sizeof(ELGroup));
                        group->key = root->key;
                        if (!groups) {
                            lastGroup = group;
                            groups = group;
                        } else {
                            lastGroup->next = group;
                            lastGroup = group;
                        }
                    }
                    group->count++;
                    data_004a28d4++;
                    *tail = root;
                    tail = &root->next;
                }
            }
        }
    }
    if (data_004a28d4) {
        for (group = groups; group; group = group->next) {
            group->roots = (ELRoot **)galloc(group->count * 4);
            n = 0;
            for (root = data_004a2948; root; root = root->next) {
                if (root->key == group->key)
                    group->roots[n++] = root;
            }
        }
    }
    data_004a2954 = groups;
    return data_004a2948;
}

#if LINKER >= 27
/* Whether a piece is its chunk's section symbol and the chunk has other pieces: 2.7 roots a .ctors or .dtors
   chunk by its named symbols then (a losing weak duplicate keeps nothing). (Not an original function.) */
static int EL_ReplacedReference(long file, ELPiece *piece);

static int EL_SectionSymbolWithOthers(long file, ELChunk *chunk, ELPiece *piece) {
    ELPiece *other;

    if ((piece->symbol->info & 0xf) != EL_STT_SECTION)
        return 0;
    for (other = chunk->pieces; other && other->chunk == chunk; other = other->next) {
        /* (an older runtime's local reference the generated file replaces is not one, in an object without
           .comment) */
        if (other != piece && !(EL_ReplacedReference(file, other) && (other->symbol->info >> 4) == 0 &&
                                !data_004a28dc[file].hasComment))
            return 1;
    }
    return 0;
}
#endif

#if LINKER >= 27
/* Whether PIECE of FILE is an older runtime's reference the Linker Generated Symbol File replaces (not a root). (Not
   an original function.) */
static int EL_ReplacedReference(long file, ELPiece *piece) {
    char *name;
    long i;

    if ((piece->symbol->info & 0xf) == EL_STT_SECTION)
        return 0;
    name = LK_symbol_Name(file, piece->symbol);
    for (i = 0; i < 7; i++) {
        if (EL_generatedOld[i] && EL_generatedIndex[i] && !strcmp(name, EL_generatedSlots[i].symbol))
            return 1;
    }
    return 0;
}
#endif

#if LINKER >= 27
/* Takes PIECE out of its chunk's list, its relocations given to the chunk's section symbol (whose root keeps the
   section). (Not an original function.) */
static void EL_DropReplacedReference(ELChunk *chunk, ELPiece *piece) {
    ELPiece *first = chunk->pieces;
    ELRelocNode *node;

    if (piece == first || !piece->prev)
        return;
    for (node = piece->relocations; node; node = node->next) {
        if (!first->relocations)
            first->relocations = node;
        else
            first->lastRelocation->next = node;
        first->lastRelocation = node;
    }
    piece->relocations = NULL;
    piece->prev->next = piece->next;
    if (piece->next)
        piece->next->prev = piece->prev;
}
#endif

/* The pieces of the .ctors and sinit sections, as roots (each marks its chunk whole). */
/* 0x4317b0 */
ELRoot *ELF_linker_CtorRoots(void) {
#if LINKER >= 27
    char *name;
#endif
    ELRoot *roots;
    ELRoot **tail;
    ELRoot *root;
    ELObject *object;
    ELSection *section;
    ELChunk *chunk;
    ELPiece *piece;
    long file;
    int i;

    roots = NULL;
    tail = &roots;
    for (file = 0; file < data_004a292c; file++) {
        EL_CHECK_BREAK();
        object = &data_004a28dc[file];
        if (object->file->flags2 & 8)
            continue;
#if LINKER >= 27
        if (file == ELF_linker_generatedFile)
            continue;
#endif
        for (i = 1; i < data_004a28dc[file].image->section_header_count; i++) {
            section = &object->sections[i];
#if LINKER >= 27
            name = ELF_PPC_EABI_BaseName(section->name);
            if (!name)
                name = section->name;
            if (!(name && !strcmp(name, ".ctors")) && !(name && !strcmp(name, "sinit")))
                continue;
#else
            if (!(section->name && !strcmp(section->name, ".ctors")) &&
                !(section->name && !strcmp(section->name, "sinit")))
                continue;
#endif
            if (!section->pieces)
                continue;
            if (!section->split)
                ELF_linker_SplitSection(section, 1);
            for (chunk = section->chunks; chunk; chunk = chunk->next) {
                for (piece = chunk->pieces; piece && piece->chunk == chunk; piece = piece->next) {
                    if (piece->symbol->flags & 0x20)
                        continue;
#if LINKER >= 27
                    /* (an older runtime's reference the generated file replaces: not a root; a local one of an
                       object without .comment is not listed or written either, its section kept) */
                    if (EL_ReplacedReference(file, piece)) {
                        if ((piece->symbol->info >> 4) == 0 && !object->hasComment)
                            EL_DropReplacedReference(chunk, piece);
                        continue;
                    }
                    if (EL_SectionSymbolWithOthers(file, chunk, piece))
                        continue;
#endif
                    chunk->whole = 1;
                    root = (ELRoot *)galloc(sizeof(ELRoot));
                    memset(root, 0, sizeof(ELRoot));
                    root->symbol = piece->symbol;
                    root->file = file;
                    root->chunk = chunk;
                    root->section = section;
                    *tail = root;
                    tail = &root->next;
                }
            }
        }
    }
    return roots;
}

/* The pieces of the .dtors sections, as roots (each marks its chunk whole). */
/* 0x431630 */
ELRoot *ELF_linker_DtorRoots(void) {
#if LINKER >= 27
    char *name;
#endif
    ELRoot *roots;
    ELRoot **tail;
    ELRoot *root;
    ELObject *object;
    ELSection *section;
    ELChunk *chunk;
    ELPiece *piece;
    long file;
    int i;

    roots = NULL;
    tail = &roots;
    for (file = 0; file < data_004a292c; file++) {
        EL_CHECK_BREAK();
        object = &data_004a28dc[file];
        if (object->file->flags2 & 8)
            continue;
#if LINKER >= 27
        if (file == ELF_linker_generatedFile)
            continue;
#endif
        for (i = 1; i < data_004a28dc[file].image->section_header_count; i++) {
            section = &object->sections[i];
#if LINKER >= 27
            name = ELF_PPC_EABI_BaseName(section->name);
            if (!name)
                name = section->name;
            if (!name || strcmp(name, ".dtors") || !section->pieces)
                continue;
#else
            if (!section->name || strcmp(section->name, ".dtors") || !section->pieces)
                continue;
#endif
            if (!section->split)
                ELF_linker_SplitSection(section, 1);
            for (chunk = section->chunks; chunk; chunk = chunk->next) {
                for (piece = chunk->pieces; piece && piece->chunk == chunk; piece = piece->next) {
                    if (piece->symbol->flags & 0x20)
                        continue;
#if LINKER >= 27
                    /* (an older runtime's reference the generated file replaces: not a root; a local one of an
                       object without .comment is not listed or written either, its section kept) */
                    if (EL_ReplacedReference(file, piece)) {
                        if ((piece->symbol->info >> 4) == 0 && !object->hasComment)
                            EL_DropReplacedReference(chunk, piece);
                        continue;
                    }
                    if (EL_SectionSymbolWithOthers(file, chunk, piece))
                        continue;
#endif
                    chunk->whole = 1;
                    root = (ELRoot *)galloc(sizeof(ELRoot));
                    memset(root, 0, sizeof(ELRoot));
                    root->symbol = piece->symbol;
                    root->file = file;
                    root->chunk = chunk;
                    root->section = section;
                    *tail = root;
                    tail = &root->next;
                }
            }
        }
    }
    return roots;
}

/* The roots kept when their object is used: each chunk's first live piece (and the common pieces). */
/* 0x431430 */
ELRoot *ELF_linker_FileRoots(void) {
    ELRoot *roots;
    ELRoot **tail;
    ELRoot *root;
    ELObject *object;
    ELSection *section;
    ELChunk *chunk;
    ELPiece *piece;
    long file;
    int i;
    unsigned char kind;

    roots = NULL;
    tail = &roots;
    for (file = 0; file < data_004a292c; file++) {
        EL_CHECK_BREAK();
        object = &data_004a28dc[file];
        if (object->file->flags2 & 8)
            continue;
        if (object->hasComment && data_004a2e39)
            continue;
        for (i = 1; i < data_004a28dc[file].image->section_header_count; i++) {
            section = &object->sections[i];
            kind = section->kind;
            if (kind == 0x14 || kind == 0x15 || kind == 6 || kind == 7 || kind == 0x1c || kind == 0x1d ||
                !section->pieces)
                continue;
            if (!section->split)
                ELF_linker_SplitSection(section, 1);
            for (chunk = section->chunks; chunk; chunk = chunk->next) {
                for (piece = chunk->pieces; piece && piece->chunk == chunk; piece = piece->next) {
                    if (piece->symbol->flags & 0x20)
                        continue;
                    root = (ELRoot *)galloc(sizeof(ELRoot));
                    memset(root, 0, sizeof(ELRoot));
                    root->symbol = piece->symbol;
                    root->file = file;
                    *tail = root;
                    tail = &root->next;
                    break;
                }
            }
        }
        if (data_004a2e49 && !data_004a2e4f) {
            section = &object->sections[data_004a28dc[file].image->section_header_count];
            for (piece = section->pieces; piece; piece = piece->next) {
                if (piece->symbol->flags & 0x20)
                    continue;
                root = (ELRoot *)galloc(sizeof(ELRoot));
                memset(root, 0, sizeof(ELRoot));
                root->symbol = piece->symbol;
                root->file = file;
                *tail = root;
                tail = &root->next;
            }
        }
    }
    return roots;
}

/* Keeps the symbols the command file's FORCEACTIVE names. */
/* 0x431340 */
void ELF_linker_KeepForceActive(void) {
    ELName *force;
    ELPiece *global;
    ELLinkSymbol *linkSymbol;
    ELSymbol *symbol;

    for (force = data_004a28e4->forceActive; force; force = force->next) {
        global = LK_symbol_Find(force->name, -1);
        if (!global) {
#if LINKER < 27
            LK_symbol_Print(">>> SYMBOL NOT FOUND: %s\r\n", force->name);
            CompilerTools_ReportDiagnostic(0x74, force->name);
#endif
        } else if (global->file < 0) {
            linkSymbol = LinkSym_FindIndex(global->file);
            if (!linkSymbol->made) {
                linkSymbol->made = 1;
                if (!linkSymbol->piece->symbol)
                    EL_ASSERT(0x6fd);
                linkSymbol->piece->symbol->flags |= 0x10;
                ELF_linker_KeepLinkSymbol(global->file, force->name);
            }
        } else {
            symbol = global->symbol;
            if (!(symbol->flags & 0x10) && !(symbol->flags & 0x20) &&
                ((symbol->info & 0xf) == 2 || (symbol->info & 0xf) <= 1))
                ELF_linker_KeepSymbol(global->file, symbol, 1);
        }
    }
}

/* Keeps the symbols a linker-defined symbol's expression names; referrer names who needs it, for the errors. */
/* 0x431200 */
void ELF_linker_KeepLinkSymbol(long index, char *referrer) {
    ELExpr *expr;
    ELPiece *global;
    ELLinkSymbol *linkSymbol;
    ELSymbol *symbol;

    for (expr = LinkSym_FindIndex(index)->expr; expr; expr = expr->next) {
        if (expr->kind != 4)
            continue;
        global = LK_symbol_Find(expr->name, -1);
        if (!global) {
            LK_symbol_Print(">>> SYMBOL NOT FOUND: %s\r\n", expr->name);
            data_004a28f4++;
            CompilerTools_ReportUndefined(expr->name, 0, referrer, "<Linker Command File>");
        } else if (global->file < 0) {
            linkSymbol = LinkSym_FindIndex(global->file);
            if (!linkSymbol->made) {
                linkSymbol->made = 1;
                if (!linkSymbol->piece->symbol)
                    EL_ASSERT(0x721);
                linkSymbol->piece->symbol->flags |= 0x10;
                ELF_linker_KeepLinkSymbol(global->file, referrer);
            }
        } else {
            symbol = global->symbol;
            if (!(symbol->flags & 0x10) && !(symbol->flags & 0x20)) {
                if ((symbol->info & 0xf) == 2 || (symbol->info & 0xf) == 1) {
                    ELF_linker_KeepSymbol(global->file, symbol, 1);
                } else {
                    CompilerTools_ReportLimitedDiagnostic(0x40, global->name, EL_FILENAME(global->file));
                    data_004a28f4++;
                }
            }
        }
    }
}

#if LINKER >= 27
/* Keeps a symbol of the Linker Generated Symbol File (INDEX: its EL_generatedSymbols') when WANTED: _ctors and
   _dtors when the dead stripping kept a .ctors (.dtors) section's or an exception table's entry for them, each
   terminator with its list's start. */
static Boolean EL_KeepGenerated(long index, Boolean wanted) {
    static long slot[4] = {0, 2, 3, 6};
    ELSymbol *symbol;
    ELSymbol *start;

    if (ELF_linker_generatedFile < 0)
        return 0;
    symbol = &EL_SYMBOLS(ELF_linker_generatedFile)[EL_generatedIndex[slot[index]]];
    if (symbol->flags & 0x30)
        return 0;
    if (index & 1) {
        start = &EL_SYMBOLS(ELF_linker_generatedFile)[EL_generatedIndex[slot[index - 1]]];
        wanted = (start->flags & 0x10) && !(start->flags & 0x20);
    }
    if (!wanted)
        return 0;
    ELF_linker_KeepSymbol(ELF_linker_generatedFile, symbol, 1);
    return 1;
}
#endif

#if LINKER >= 27
/* Keeps the Linker Generated Symbol File's references, as AddCppRuntimeRoots' root records say (0x43e320 walks
   them): those of .ctors$10 and .dtors$15 with the exception tables' kept entries, the
   __destroy_global_chain_reference of .dtors$10 with __register_global_object when it is defined, else with .dtors'
   kept entries. (Not an original function.) */
static Boolean EL_KeepGeneratedReferences(void) {
    static long slots[3] = {1, 4, 5};
    ELSymbol *symbol;
    ELPiece *other;
    Boolean changed = 0;
    long i;

    if (ELF_linker_generatedFile < 0)
        return 0;
    for (i = 0; i < 3; i++) {
        if (!EL_generatedIndex[slots[i]])
            continue;
        symbol = &EL_SYMBOLS(ELF_linker_generatedFile)[EL_generatedIndex[slots[i]]];
        if (symbol->flags & 0x30)
            continue;
        if (slots[i] == 4) {
            /* (with __register_global_object, when there is one, else with .dtors' kept entries) */
            other = LK_symbol_FindGlobal("__register_global_object", -1);
            if (other && other->file >= 0 && other->symbol && other->symbol->section) {
                if (!(other->symbol->flags & 0x10) || (other->symbol->flags & 0x20))
                    continue;
            } else if (!data_004a2878) {
                continue;
            }
        } else if (!data_004a2944) {
            /* (with the exception tables' kept entries) */
            continue;
        }
        ELF_linker_KeepSymbol(ELF_linker_generatedFile, symbol, 1);
        changed = 1;
    }
    return changed;
}

/* Whether an entry of the list ROOTS is kept (a partial link's, kept already, too). (Not an original
   function.) */
static Boolean EL_AnyKept(ELRoot *roots) {
    for (; roots; roots = roots->next) {
        if (data_004a28dc[roots->file].forced && (roots->symbol->flags & 0x10) && !(roots->symbol->flags & 0x20))
            return 1;
    }
    return 0;
}
#endif

/* Dead stripping: keeps the entry points, FORCEACTIVE and the roots until stable, then the kept code's debug info. */
/* 0x430c60 */
void ELF_linker_DeadStrip(ELPiece *entry) {
    Boolean once;
    Boolean changed;
    ELOverlay *overlay;
    ELRoot *ctors;
    ELRoot *dtors;
    ELRoot *roots;
    ELRoot *exceptions;
    ELRoot *root;
    ELPiece *global;
    ELObject *object;
    ELSection *section;
    ELSection *debug;
    ELSection *line;
    long file;
    int i;

    once = 0;
    if (!entry && data_004a2e39) {
        CompilerTools_ReportLimitedDiagnostic(0xf, data_0049c161);
        CompilerTools_Abort();
    }
    CompilerTools_ReportMessage(0x28, data_0049bed6);
    data_004a2904 = 0;
    data_004a2a8c = 0;
    data_004a2878 = 0;
    data_004a2944 = 0;
    EL_CHECK_BREAK();
    EL_CHECK_BREAK();
#if LINKER >= 27
    {
        /* (2.7: the FORCEACTIVE names nothing defines reported first, in the map too) */
        ELName *force;

        for (force = data_004a28e4->forceActive; force; force = force->next) {
            if (!LK_symbol_Find(force->name, -1)) {
                LK_symbol_Print(">>> SYMBOL NOT FOUND: %s\r\n", force->name);
                CompilerTools_ReportDiagnostic(0x74, force->name);
            }
        }
    }
#endif
    if (entry)
        ELF_linker_KeepSymbol(entry->file, entry->symbol, 1);
    for (overlay = data_004a2200; overlay; overlay = overlay->next) {
        EL_CHECK_BREAK();
        LK_symbol_Print("\r\n\r\nOverlay\r\n");
        ELF_linker_KeepSymbol(overlay->entry->file, overlay->entry->symbol, 1);
    }
    EL_CHECK_BREAK();
    EL_CHECK_BREAK();
    ELF_linker_KeepForceActive();
    ctors = ELF_linker_CtorRoots();
    dtors = ELF_linker_DtorRoots();
    EL_CHECK_BREAK();
    roots = ELF_linker_FileRoots();
    EL_CHECK_BREAK();
    exceptions = ELF_linker_ExceptionRoots();

    do {
        changed = 0;
        EL_CHECK_BREAK();
        for (root = data_004a28e0; root; root = root->next) {
#if LINKER >= 27
            /* (2.7: not an older runtime's reference the generated file replaces, forced by its .comment) */
            if (root->symbol->piece && EL_ReplacedReference(root->file, root->symbol->piece))
                continue;
#endif
            if (!(root->symbol->flags & 0x10) && !(root->symbol->flags & 0x20)) {
                ELF_linker_KeepSymbol(root->file, root->symbol, 1);
                changed = 1;
            }
        }
#if LINKER < 27
        EL_CHECK_BREAK();
        for (root = roots; root; root = root->next) {
            if (root->symbol->flags & 0x10)
                continue;
            if (!data_004a2e39 && !(data_004a28dc[root->file].file->flags & 4))
                data_004a28dc[root->file].forced = 1;
            if (data_004a28dc[root->file].forced) {
                ELF_linker_KeepSymbol(root->file, root->symbol, 1);
                changed = 1;
            }
        }
#endif
        EL_CHECK_BREAK();
        for (root = ctors; root; root = root->next) {
            if (data_004a28dc[root->file].forced && !(root->symbol->flags & 0x10) && !(root->symbol->flags & 0x20)) {
                root->chunk->kept = 1;
                root->section->kept = root->chunk->kept;
                data_004a2a8c++;
                ELF_linker_KeepSymbol(root->file, root->symbol, 1);
                changed = 1;
            }
        }
        EL_CHECK_BREAK();
        for (root = dtors; root; root = root->next) {
            if (data_004a28dc[root->file].forced && !(root->symbol->flags & 0x10) && !(root->symbol->flags & 0x20)) {
                root->chunk->kept = 1;
                root->section->kept = root->chunk->kept;
                data_004a2878++;
                ELF_linker_KeepSymbol(root->file, root->symbol, 1);
                changed = 1;
            }
        }
#if LINKER >= 27
        EL_CHECK_BREAK();
        for (root = roots; root; root = root->next) {
            if (root->symbol->flags & 0x10)
                continue;
            if (!data_004a2e39 && !(data_004a28dc[root->file].file->flags & 4))
                data_004a28dc[root->file].forced = 1;
            if (data_004a28dc[root->file].forced) {
                ELF_linker_KeepSymbol(root->file, root->symbol, 1);
                changed = 1;
            }
        }
#endif
        EL_CHECK_BREAK();
        for (root = exceptions; root; root = root->next) {
            if ((root->symbol->flags & 0x10) && (!(root->target->flags & 0x10) || (root->target->flags & 0x20)))
                EL_ASSERT(0x7d9);
            if ((root->symbol->flags & 0x10) || (root->symbol->flags & 0x20) || !data_004a28dc[root->file].forced ||
                !(root->target->flags & 0x10) || (root->target->flags & 0x20))
                continue;
            data_004a2944++;
            ELF_linker_KeepSymbol(root->file, root->symbol, 1);
            changed = 1;
            if (!once) {
                once = 1;
                global = LK_symbol_Find("__init_cpp_exceptions", -1);
                if (!data_004a2e49 || data_004a2e4f) {
#if LINKER >= 27
                    /* (2.7: the Linker Generated Symbol File's reference says it is undefined instead) */
                    if (global && (global->file < 0 || !global->symbol || !global->symbol->section))
                        global = NULL;
                    if (!global && ELF_linker_generatedFile < 0)
                        CompilerTools_ReportLimitedDiagnostic(0x7d);
                    else if (global)
                        data_004a28dc[global->file].forced = 1;
#else
                    if (!global)
                        CompilerTools_ReportLimitedDiagnostic(0x7d);
                    else
                        data_004a28dc[global->file].forced = 1;
#endif
                }
            }
        }
#if LINKER >= 27
        changed |= EL_KeepGenerated(0, data_004a2944 || data_004a2a8c || data_004a2e49);
        changed |= EL_KeepGenerated(1, 0);
        changed |= EL_KeepGenerated(2, data_004a2944 || data_004a2878 || data_004a2e49);
        changed |= EL_KeepGenerated(3, 0);
        changed |= EL_KeepGeneratedReferences();
#endif
    } while (changed);

    for (file = 0; file < data_004a292c; file++) {
        EL_CHECK_BREAK();
        object = &data_004a28dc[file];
        if ((object->file->flags2 & 8) || !object->forced)
            continue;
        line = NULL;
        debug = NULL;
        for (i = 1; i < data_004a28dc[file].image->section_header_count; i++) {
            section = &object->sections[i];
            if (section->kind != 0x14 && section->kind != 0x15)
                continue;
            if (section->pieces && !section->split)
                ELF_linker_SplitSection(section, 1);
            if (section->name && !strcmp(section->name, ".debug"))
                debug = section;
            else if (section->name && !strcmp(section->name, ".line"))
                line = section;
        }
        if (debug && debug->pieces)
            ELF_linker_KeepDebugSections(debug, file);
        /* (the original tests .debug's contents here too, so a .line without a .debug reads through NULL) */
        if (line && debug->pieces)
            ELF_linker_KeepDebugSections(line, file);
        if (debug && debug->pieces)
            ELF_linker_KeepDebugSections(debug, file);
    }
    ELF_linker_ReadProcedureRecords();
}

/* Keeps a .debug/.line section's chunks of kept code and the sections they refer to (others' addends zeroed). */
/* 0x4309e0 */
void ELF_linker_KeepDebugSections(ELSection *section, long file) {
    ELObject *object;
    ELChunk *chunk;
    ELPiece *piece;
    ELRelocNode *node;
    ELRelocation *rel;
    ELSymbol *target;
    ELSection *into;

    object = &data_004a28dc[file];
    for (chunk = section->chunks; chunk; chunk = chunk->next) {
        for (piece = chunk->pieces; piece && piece->chunk == chunk; piece = piece->next) {
            if (piece->symbol->flags & 0x20)
                continue;
            for (node = piece->relocations; node; node = node->next) {
                target = &EL_SYMBOLS(file)[node->rel->info >> 8];
                if (target->section != 0 && target->section != 0xfff2 && object->sections[target->section].kept) {
                    chunk->pieces->symbol->flags |= 0x10;
                    piece->symbol->flags |= 0x10;
                    section->kept = 1;
                    chunk->kept = 1;
                    break;
                }
            }
            if (chunk->kept)
                break;
        }

        if (!chunk->kept)
            continue;
        for (piece = chunk->pieces; piece && piece->chunk == chunk; piece = piece->next) {
            if (!(piece->symbol->flags & 0x10) || (piece->symbol->flags & 0x20))
                continue;
            for (node = piece->relocations; node; node = node->next) {
                rel = node->rel;
                target = &EL_SYMBOLS(file)[rel->info >> 8];
                if (target->section == 0 || target->section == 0xfff2)
                    continue;
                into = &object->sections[target->section];
                if (!into->split)
                    ELF_linker_SplitSection(into, !object->hasComment);
                if (!into->kept) {
                    rel->addend = 0;
                    if (!(into->header->flags & 4) && !(into->name && !strncmp(into->name, ".debug", 6)) &&
                        !(into->name && !strcmp(into->name, ".line")))
                        continue;
                }
                into->kept = 1;
                if (!(target->flags & 0x20))
                    target->piece->chunk->kept = 1;
            }
        }
    }
}

extern void SRecord_Nothing(void); /* 0x442e80: not in eltypes.h */
extern void ELF_Endian_ConvertRecord1(void *record);
extern void ELF_Endian_ConvertRecord2(void *record);
extern void ELF_Endian_ConvertRecord3(void *record);

/* Whether an object's bytes are an ELF image that is not relocatable (what EXCLUDEFILES and INCLUDEDWARF need). */
static inline Boolean EL_IsLinkedImage(ELObject *object) {
    char *image = *object->file->member->data;

    return strncmp(image, "\177ELF", 4) == 0 && ((struct Elf32Header *)image)->type != 1;
}

/* Ties the new-DWARF objects' .debug pieces to their globals, then their .line pieces to those .debug pieces. */
/* 0x4305c0 */
void ELF_linker_LinkDebugPieces(void) {
    long i, j;
    ELObject *file;
    ELSection *section;
    ELChunk *chunk;
    ELPiece *piece;
    ELSymbol *symbol;
    ELPiece *global;
    ELPiece *previous;
    char *name;
    char tagText[5];
    unsigned int tag;

    for (i = 0; i < data_004a292c; i++) {
        EL_CHECK_BREAK();
        file = &data_004a28dc[i];
        if (file->file->flags2 & EL_FILE2_EXCLUDED || !file->newDwarf)
            continue;
        for (j = 1; j < file->image->section_header_count; j++) {
            section = &file->sections[j];
            if (section->kind != 0x14 || !section->name || strcmp(section->name, ".debug"))
                continue;
            section->x38 = 1;
            if (section->pieces && !section->split)
                ELF_linker_SplitSection(section, 1);
            for (chunk = section->chunks; chunk; chunk = chunk->next) {
                for (piece = chunk->pieces; piece && piece->chunk == chunk; piece = piece->next) {
                    symbol = piece->symbol;
                    if (!piece->name || strncmp(piece->name, ".dwarf.", 7))
                        continue;
                    name = piece->name + 7;
                    strncpy(tagText, name, 4);
                    tagText[4] = 0;
                    sscanf(tagText, "%x", &tag);
                    if (name[5] == '.')
                        continue;
                    switch (tag) {
                    case 6:
                    case 0x14:
                        global = LK_symbol_FindInFile(name + 5, i);
                        if (!global)
                            EL_ASSERT(0x8aa);
                        global->related = piece;
                        break;
                    case 7:
                    case 0xc:
                        if (!(symbol->flags & 0x20) && (global = LK_symbol_Find(name + 5, -1)))
                            global->related = piece;
                        break;
                    }
                }
            }
        }
    }

    for (i = 0; i < data_004a292c; i++) {
        EL_CHECK_BREAK();
        file = &data_004a28dc[i];
        if (file->file->flags2 & EL_FILE2_EXCLUDED || !file->newDwarf)
            continue;
        for (j = 1; j < file->image->section_header_count; j++) {
            section = &file->sections[j];
            if (section->kind != 0x15 || !section->name || strcmp(section->name, ".line"))
                continue;
            section->x38 = 1;
            if (section->pieces && !section->split)
                ELF_linker_SplitSection(section, 1);
            for (chunk = section->chunks; chunk; chunk = chunk->next) {
                for (piece = chunk->pieces; piece && piece->chunk == chunk; piece = piece->next) {
                    symbol = piece->symbol;
                    if (!piece->name || strncmp(piece->name, ".line.", 6))
                        continue;
                    name = piece->name + 6;
                    if (*name == '.')
                        continue;
                    global = LK_symbol_FindInFile(name, i);
                    if (!global)
                        EL_ASSERT(0x8da);
                    previous = global->related;
                    if (symbol->size) {
                        if (!previous)
                            EL_ASSERT(0x8e1);
                        previous->related = piece;
                    } else {
                        global->related = piece;
                    }
                    piece->related = global;
                }
            }
        }
    }
}

/* Gives each object's common symbols a .bss section of their own, after its sections, and places them in it. */
/* 0x430450 */
void ELF_linker_MakeCommonSections(void) {
    long i;
    ELObject *file;
    ELSection *section;
    Elf32Section *header;
    ELPiece *piece;
    ELSymbol *symbol;
    unsigned long align;
    unsigned short index;

    for (i = 0; i < data_004a292c; i++) {
        EL_CHECK_BREAK();
        file = &data_004a28dc[i];
        if (file->file->flags2 & EL_FILE2_EXCLUDED)
            continue;
        index = file->image->section_header_count;
        if (!file->sections[index].pieces)
            continue;
        section = &file->sections[index];
        if (!section->chunks) {
            section->chunks = galloc(sizeof(ELChunk));
            memset(section->chunks, 0, sizeof(ELChunk));
        }
        section->name = ".bss";
        file->image->section_header_count++;
        section->header = header = galloc(sizeof(Elf32Section));
        memset(header, 0, sizeof(Elf32Section));
        header->type = 8;
        header->flags = 3;
        header->alignment = 4;
        section->file = i;
        section->index = index;
        ELF_PPC_EABI_AssignSection(file, section);
        for (piece = section->pieces; piece; piece = piece->next) {
            symbol = piece->symbol;
#if LINKER >= 27
            if ((symbol->info & 0xf) == EL_STT_SECTION) {
                symbol->section = index;
                continue;
            }
#endif
            align = symbol->value;
            if (align > header->alignment) {
                header->alignment = align;
                if (section->output->align98 < header->alignment) {
                    section->output->align98 = header->alignment;
                    section->output->align74 = header->alignment;
                }
            }
            header->size = (header->size + align - 1) & ~(align - 1);
            symbol->value = header->size;
            header->size += symbol->size;
            symbol->section = index;
        }
    }
}

/* The link: the output's ELF header, the state, reading, resolving and writing; the status, 0 when it linked. */
/* 0x430130 */
int ELF_linker_Link(void) {
    int status;

    data_004a2200 = 0;
    data_004a28f4 = 0;
    data_004a2a68 = NULL;
    LK_symbol_ClearTable(data_0049e1c0);
    data_004a289c.magic[0] = 0x7f;
    data_004a289c.magic[1] = 'E';
    data_004a289c.magic[2] = 'L';
    data_004a289c.magic[3] = 'F';
    data_004a289c.elfClass = 1;
    data_004a289c.dataEncoding = copts[0] == 0 ? 2 : 1;
    data_004a289c.identificationRest[0] = 1;
    data_004a289c.type = 2;
    data_004a289c.machine = 0x14;
    data_004a289c.flags = 0x80000000;
    data_004a289c.version = 1;
    data_004a289c.program_header_offset = 0;
    data_004a289c.section_header_offset = 0;
    data_004a289c.header_size = 0x34;
    data_004a289c.program_header_size = 0x20;
    data_004a289c.program_header_count = 0;
    data_004a289c.section_header_size = 0x28;
    data_004a289c.section_header_count = 0;
    data_004a289c.section_name_index = 2;

    status = _Setjmp(file_input_jmpbuf);
    if (status == 0) {
        fn_0041e000();
        if (!data_0049c161[0] && data_004a2e39) {
            CompilerTools_ReportLimitedDiagnostic(0x1f);
            CompilerTools_FormatMessageAndLongjmp(0xd, 2);
        }
        data_004a2dce = 0;
        if (data_0049c116)
            LK_symbol_OpenMap();
        data_004a2e34 = 0;
        if (data_004a2e3a) {
            SRecord_OpenFile();
            SRecord_Header();
        }
        data_004a2dcc = 0;
#if LINKER >= 27
        ELF_gen_deferOpen = 1;
#endif
        ELF_gen_OpenOutput();
        LK_symbol_Print("Link map of %s\r\n", data_0049c161);
        data_004a2764 = 0;
        data_00588228 = 0;
        limited_diagnostic_count = 0;
        data_004a2a74 = 0;
        data_004a2868 = 0;
        data_004a2204 = 0;
        data_004a2a84 = 0;
        data_004a2840 = 0;
        data_004a28e0 = 0;
        if (CompilerTools_InitHeaps(ELF_linker_NoMemory))
            CompilerTools_FormatMessageAndLongjmp(0xd, -108);
        data_004a2860 = (void *)ELF_linker_NoMemory;
        data_004a2e53 = 0;
        CompilerTools_ReportMessage(0x1a, data_0049bed6);
        ELF_PPC_EABI_ReadCommandFile();
        ELF_linker_ReadFiles(0);
        ELF_linker_LoadObjects(0);
        EL_CHECK_BREAK();
        ELF_linker_ExcludeFiles();
        ELF_linker_CheckIncludeDwarf();
#if LINKER >= 27
        /* (2.7: a partial link's commons too go in .bss) */
        ELF_linker_MakeCommonSections();
#else
        if (!data_004a2e49 || data_004a2e4f)
            ELF_linker_MakeCommonSections();
#endif
        ELF_linker_LinkDebugPieces();
        data_004a2a68 = data_004a2e39 ? LK_symbol_Find(data_0049c161, -1) : NULL;
        ELF_linker_DeadStrip(data_004a2a68);
#if LINKER >= 27
        /* (the generated reference is global in the output) */
        if (ELF_linker_generatedFile >= 0) {
            long slot;

            for (slot = 0; slot < 7; slot++) {
                if (EL_generatedSlots[slot].target && EL_generatedIndex[slot])
                    EL_SYMBOLS(ELF_linker_generatedFile)[EL_generatedIndex[slot]].info = 0x11;
            }
        }
#endif
        EL_CHECK_BREAK();
        if (limited_diagnostic_count) {
            if (data_0049c116) {
                LK_symbol_FlushMap();
                LK_symbol_CloseMap();
            }
            CompilerTools_FormatMessageAndLongjmp(0xd, 2);
        }
        CompilerTools_ReportMessage(0x1c, data_0049bed6);
        ELF_gen_WriteFile();
        if (data_004a2e3a)
            SRecord_Nothing();
        ELF_gen_FlushOutput();
        if (data_004a28f4) {
            if (data_0049c116) {
                LK_symbol_FlushMap();
                LK_symbol_CloseMap();
            }
            CompilerTools_FormatMessageAndLongjmp(0xd, 2);
        }
        ELF_linker_End(1, 0);
        status = 0;
    } else {
        ELF_linker_End(0, status);
        CompilerTools_DispatchMessageBufferByMode(status);
    }
    return status;
}

/* An object's CodeWarrior comment, FORCEFILES and the SECTIONS items naming it (all sections 0x36 if no comment). */
/* 0x42ff10 */
void ELF_linker_ReadObjectOptions(long index) {
    ELObject *file;
    ELSection *section;
    signed char *comment;
    char *name;
    char *member;
    ELFileName *pattern;
    ELCmdSection *output;
    ELCmdItem *item;
    long j;

    member = NULL;
    file = &data_004a28dc[index];
    file->hasComment = 0;
    file->commentVersion = 0;
    file->newDwarf = 0;
    for (j = 1; j < file->image->section_header_count; j++) {
        section = &file->sections[j];
        if (section->kind == 0x1b) {
            comment = (signed char *)section->data;
            if (strncmp((char *)comment, "CodeWarrior", 11) == 0) {
                if (comment[11] >= 6) {
                    file->commentVersion = 3;
                    file->comment = (char *)comment + comment[0x14];
                } else if (comment[11] >= 3) {
                    file->commentVersion = 2;
                }
                file->hasComment = 1;
#if LINKER != 13
                /* (1.3.2 strips no DWARF entries piece by piece: it has no 3.0 objects' symbols of them) */
                if (comment[11] >= 11)
                    file->newDwarf = 1;
#endif
            }
            break;
        }
    }

    name = file->file->objectName;
    if (file->file->flags & EL_FILE_ARCHIVE)
        member = file->fileSymbol->name;

    for (pattern = data_004a28e4->forceFiles; pattern; pattern = pattern->next) {
        if (ELF_PPC_EABI_MatchFileName(pattern->name, name) == 0) {
            if (pattern->member) {
                if (!member) {
                    CompilerTools_ReportLimitedDiagnostic(0x61, pattern->name, pattern->member);
                    CompilerTools_FormatMessageAndLongjmp(0xd, 2);
                }
                if (ELF_PPC_EABI_MatchFileName(pattern->member, member) != 0)
                    continue;
            }
        } else if (!member || ELF_PPC_EABI_MatchFileName(pattern->name, member) != 0) {
            continue;
        }
        file->forced = 1;
        file->commentVersion = 0;
        file->hasComment = 0;
        pattern->found = 1;
        break;
    }

    for (output = data_004a28e4->sections; output; output = output->next) {
        if (output->kind != 1)
            continue;
        for (item = output->items; item; item = item->next) {
            if (item->kind != 1 || !item->file)
                continue;
            if (ELF_PPC_EABI_MatchFileName(item->file, name) == 0) {
                if (item->member && (!member || ELF_PPC_EABI_MatchFileName(item->member, member) != 0))
                    continue;
            } else if (!member || ELF_PPC_EABI_MatchFileName(item->file, member) != 0) {
                continue;
            }
            item->found = 1;
        }
    }

    if (!file->hasComment) {
        for (j = 1; j < data_004a28dc[index].image->section_header_count; j++)
            file->sections[j].whole = 1;
    }
}

/* Converts the relocations (SHT_RELA sections) of an object to the host's byte order. */
/* 0x42fe70 */
void ELF_linker_SwapRelocations(long index) {
    ELObject *file;
    ELSection *section;
    unsigned long k;
    long j;

    file = &data_004a28dc[index];
    for (j = 1; j < data_004a28dc[index].image->section_header_count; j++) {
        section = &file->sections[j];
        if (section->header->type != 4)
            continue;
        EL_CHECK_BREAK();
        for (k = 0; k < section->header->size / 12; k++)
            ELF_Endian_ConvertThreeWords((unsigned int *)(section->data + k * 12));
    }
}

/* Whether an ELF header suits the link: class, byte order, machine and a symbol table (swaps its section headers). */
/* 0x42fda0 */
Boolean ELF_linker_IsLinkableObject(struct Elf32Header *header) {
    Boolean result;
    Elf32Section *section;
    int i;

    result = 0;
    if (header->dataEncoding == 1) {
        if (!copts[0])
            return 0;
    } else if (header->dataEncoding == 2) {
        if (copts[0])
            return 0;
    } else {
        return 0;
    }
    if (header->elfClass == 1) {
        if (data_00588517)
            return 0;
    } else if (header->elfClass == 2) {
        if (!data_00588517)
            return 0;
    } else {
        return 0;
    }
    if (header->machine != 0x14 && header->machine != 0x11)
        return 0;
    for (i = 1; i < header->section_header_count; i++) {
        section = (Elf32Section *)((char *)header + header->section_header_offset + i * sizeof(Elf32Section));
        if (data_004a2e4e)
            ELF_Endian_SwapElf32Section(section);
        if (section->type == 2)
            result = 1;
    }
    return result;
}

/* EXCLUDEFILES: drops the weak globals the excluded files' global/weak symbols name, listing them in the map. */
/* 0x42fad0 */
void ELF_linker_ExcludeFiles(void) {
    Boolean failed;
    ELFileName *pattern;
    long index;
    unsigned long count;
    long j;
    ELSymbol *symbol;
    char *name;
    ELPiece *global;
    char *fileName;
    char *memberName;
    char *type;
    char *binding;

    failed = 0;
    if (!data_004a2e4f)
        return;
    if (!data_004a28e4->excludeFiles)
        return;
    for (pattern = data_004a28e4->excludeFiles; pattern; pattern = pattern->next) {
        if (!pattern->index) {
            CompilerTools_ReportDiagnostic(0x81, "EXCLUDEFILES", pattern->name);
            continue;
        }
        index = pattern->index - 1;
        if (!EL_IsLinkedImage(&data_004a28dc[index])) {
            CompilerTools_ReportLimitedDiagnostic(0x82, "EXCLUDEFILES", pattern->name);
            failed = 1;
            continue;
        }
        count = data_004a28dc[index].symbols->header->size >> 4;
        for (j = 1; j < (long)count; j++) {
            symbol = (ELSymbol *)(data_004a28dc[index].symbols->data + j * 16);
            if (data_004a2e4e)
                ELF_Endian_ConvertSymbolRecord(symbol);
            if (!symbol->section)
                continue;
            symbol->flags = 0;
            name = LK_symbol_ObjectName(index, symbol);
            if ((unsigned char)((symbol->info >> 4) - 1) > 1)
                continue;
            global = LK_symbol_Find(name, index);
            if (!global || (global->symbol->info >> 4) != 2)
                continue;
            global->symbol->flags |= 0x20;
            fileName = index > -1 ? data_004a28dc[index].file->name : "linker generated symbol";
            memberName = index > -1 && data_004a28dc[index].memberName ? data_004a28dc[index].fileSymbol->name : "";
            switch (symbol->info & 0xf) {
            case 0: type = "notype"; break;
            case 1: type = "object"; break;
            case 2: type = "func"; break;
            case 3: type = "section"; break;
            case 4: type = "file"; break;
            default: type = "unknown"; break;
            }
            switch (symbol->info >> 4) {
            case 0: binding = "local"; break;
            case 1: binding = "global"; break;
            case 2: binding = "weak"; break;
            case 0xd: binding = "multidef"; break;
            case 0xe: binding = "overload"; break;
            default: binding = "unknown"; break;
            }
            if (EL_LIST_CLOSURE)
                LK_symbol_Print(">>> EXCLUDED SYMBOL %s (%s,%s) found in %s %s\r\n", name, type, binding, fileName,
                            memberName);
        }
    }
    if (failed)
        CompilerTools_FormatMessageAndLongjmp(0xd, 2);
}

/* INCLUDEDWARF: checks each file it names was found and is a linked image. */
/* 0x42f9f0 */
void ELF_linker_CheckIncludeDwarf(void) {
    Boolean failed;
    ELFileName *pattern;

    failed = 0;
    if (data_004a2e49)
        return;
    if (!data_004a28e4->includeDwarf)
        return;
    for (pattern = data_004a28e4->includeDwarf; pattern; pattern = pattern->next) {
        if (!pattern->index) {
            CompilerTools_ReportDiagnostic(0x81, "INCLUDEDWARF", pattern->name);
        } else if (!EL_IsLinkedImage(&data_004a28dc[pattern->index - 1])) {
            CompilerTools_ReportLimitedDiagnostic(0x82, "INCLUDEDWARF", pattern->name);
            failed = 1;
        }
    }
    if (failed)
        CompilerTools_FormatMessageAndLongjmp(0xd, 2);
}

/* Whether EXCLUDEFILES or INCLUDEDWARF names an object, which it marks (0x08 excluded, 0x10 its DWARF included). */
/* 0x42f940 */
Boolean ELF_linker_MatchFileLists(ELFile *file) {
    Boolean found;
    ELFileName *pattern;

    found = 0;
    if (data_004a2e4f) {
        for (pattern = data_004a28e4->excludeFiles; pattern; pattern = pattern->next) {
            if (ELF_PPC_EABI_MatchFileName(pattern->name, file->objectName) == 0) {
                file->flags2 |= EL_FILE2_EXCLUDED;
                pattern->match = file;
                found = 1;
                break;
            }
        }
    }
    if (!data_004a2e49) {
        for (pattern = data_004a28e4->includeDwarf; pattern; pattern = pattern->next) {
            if (ELF_PPC_EABI_MatchFileName(pattern->name, file->objectName) == 0) {
                file->flags2 |= EL_FILE2_DWARF;
                pattern->match = file;
                return 1;
            }
        }
    }
    return found;
}

/* Reads the forced objects' procedure records: a node per kept piece's relocation, listed in the piece's chunk. */
/* 0x42f640 */
void ELF_linker_ReadProcedureRecords(void) {
    long i, j;
    ELObject *file;
    ELSection *section;
    ELSection *target;
    ELChunk *chunk;
    ELPiece *piece;
    ELRelocNode *fixup;
    ELSymbol *symbol;
    ELRecordNode *node;
    ELRecordNode *head;
    ELRecordNode **tail;
    unsigned char *record;
    unsigned long offset;
    unsigned long size;

    if (!data_004a2e40)
        return;
    for (i = 0; i < data_004a292c; i++) {
        EL_CHECK_BREAK();
        file = &data_004a28dc[i];
#if LINKER == 13
        /* (1.3.2: every object's, its sections' kept or not) */
        if (file->file->flags2 & EL_FILE2_EXCLUDED)
            continue;
#else
        if (file->file->flags2 & EL_FILE2_EXCLUDED || !file->forced)
            continue;
#endif
        for (j = 1; j < data_004a28dc[i].image->section_header_count; j++) {
            section = &file->sections[j];
#if LINKER == 13
            if (!(target = section->related) || !section->output)
                continue;
#else
            if (!section->kept || !(target = section->related))
                continue;
#endif
            target->output = section->output->xb8;
            target->kept = 1;
            if (!target->pieces) {
                EL_ASSERT(0xb81);
                continue;
            }
            if (!target->split)
                ELF_linker_SplitSection(target, 1);
            offset = 0;
            for (chunk = target->chunks; chunk; chunk = chunk->next) {
                head = NULL;
                tail = &head;
                for (piece = chunk->pieces; piece && piece->chunk == chunk; piece = piece->next) {
                    if (piece->symbol->flags & 0x20)
                        continue;
                    for (fixup = piece->relocations; fixup; fixup = fixup->next) {
                        symbol = (ELSymbol *)data_004a28dc[i].symbols->data + (fixup->rel->info >> 8);
                        node = galloc(sizeof(ELRecordNode));
                        memset(node, 0, sizeof(ELRecordNode));
                        fixup->record = node;
                        node->symbol = symbol;
                        node->offset = offset;
                        size = 0;
                        record = (unsigned char *)target->data + offset;
                        node->kind = record[0];
                        switch (record[0]) {
                        case 1:
                            if (data_004a2e4e)
                                ELF_Endian_ConvertRecord1(record);
                            size = 0x14 + ((ELRecord1 *)record)->exitCount * 4 +
                                   ((ELRecord1 *)record)->nonInstrumentableCount * 4;
                            break;
                        case 2:
                            if (data_004a2e4e)
                                ELF_Endian_ConvertRecord2(record);
                            size = 8 + ((ELRecord2 *)record)->exitCount * 2;
                            break;
                        case 3:
                            if (data_004a2e4e)
                                ELF_Endian_ConvertRecord3(record);
                            size = 0xc + ((ELRecord3 *)record)->exitCount * 4;
                            break;
                        }
                        node->size = size;
                        offset += (size + 3) & ~3;
                        *tail = node;
                        tail = &node->next;
                        chunk->kept = 1;
                    }
                }
                chunk->records = head;
            }
        }
    }
}

/* The plugin interface's call of the compiler's callback 3, for a context. */
/* 0x42f600 */
int __stdcall ELF_linker_CallCallback(CWPluginPrivateContext *context, long argument1, long argument2) {
    CWPluginPrivateContext *object;

    object = validate_context_signature(context);
    if (!object)
        return 4;
    return ((int(__stdcall **)(CWPluginPrivateContext *, long, long))object->compilerCallbacks)[3](object, argument1,
                                                                                                   argument2);
}
