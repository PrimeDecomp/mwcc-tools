#include "driver/ELF_Lib_Import.h"
#include "compiler/CError.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/ELF_Endian.h"
#include "driver/COSToolsCLT.h"
#include "driver/CWPluginsPrivate.h"
#include "driver/CWSecretPluginCallbacks.h"
#include "driver/DateTimeUtils.h"
#include "driver/DropInCompilerLinker.h"
#include "driver/Files.h"
#include "driver/LinkErrorsPPC_EABI.h"
#include "driver/Memory.h"
#include "driver/oslib.h"
#include "driver/pref_structs.h"
#include "driver/StringUtils.h"
#include <ctype.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

// COMMONs. objudatasize and objidatasize (the imported uninitialized and initialized data sizes)
// fit 2.6's COMMON hash order where Wii's later Import_objudatasize/Import_objidatasize do not
jmp_buf linkerexit;
SInt32 objudatasize;
SInt32 Imported_Obj_Codesize;
SInt32 linkererrors;
LinkInfo linkinfo;
SInt32 objidatasize;
SInt32 linkerwarnings;
char Target64;

// The parameters the plugin interface gives the importer and the object data it gives back: the
// compiler's CompilerLinkerParamBlk (CPrep.h) as far as SYMinfo, then another SDK revision's
// fields (0x10c bytes where the compiler's is 0x104: the unit's .bss says so). TODO: the fields
// after caching_includes are guesses; only the ones before are used

struct LibImportCU {
    struct CWPluginPrivateContext* context;
    CWObjectData object;
    SInt32 objectdata;
    SInt32 browsedata;
    SInt32 request;
    SInt32 version;
    CWFileSpec targetfile;
    SInt32 numfiles;
    SInt32 whichfile;
    CWFileSpec sourcefile;
    UInt32 sourcetext;
    UInt32 sourcetextsize;
    UInt8 precompile;
    UInt8 autoprecompile;
    UInt8 preprocess;
    UInt8 SYMinfo;
    UInt8 caching_includes;
    UInt8 pad_e9;
    UInt8 browseOptions[16]; // (CWBrowseOptions, byte-aligned here)
    UInt8 compiling;
    UInt8 pad_fb;
    UInt16 mainFileAttributes;
    SInt32 platformCode1;
    SInt32 platformCode0;
    char* optionString;
};

// A file Link_GetFileText reads: an ELF object, or each member of an archive. TODO: invented type
// and member names
typedef struct FileInputNode {
    struct FileInputNode* next; // the archive's next member
    // what it is: 0 unknown, 1 an archive member, 2 an ELF member, 3 the long-name table, 4 an
    // ELF object (IsMWLibraryFile, Import_Archive)
    UInt8 kind;
    char** dataHandle; // its contents
    char* archiveMemberHeader; // its 60-byte ar header (archive members)
} FileInputNode;

static struct LibImportCU libimp_cu;
static UInt8 nonNativeByteOrder;

// (defined below)
FileInputNode* Link_GetFileText(CWFileSpec* name, SInt16* err);
void DeleteArchiveInfo(FileInputNode* args);
short Import_File(FileInputNode* record, char* buffer);

void out_of_memory() {
    linkfailed(13, memFullErr);
}

void ReportLinkerError(char* message, char* line2) {
    CWReportMessage(libimp_cu.context, NULL, message, line2, 2, 0);
}

void ReportLinkerWarning(char* message, char* line2) {
    CWReportMessage(libimp_cu.context, NULL, message, line2, 1, 0);
}

UInt8 IsELF(char* data) {
    char magic[4] = {0x7F, 'E', 'L', 'F'};

    if (strncmp(data, magic, sizeof(magic)) == 0) {
        return TRUE;
    }
    return FALSE;
}

char** ImportObjectLibrary(
    CWFileSpec* name, SInt32* codesize, SInt32* udatasize, SInt32* idatasize
) {
    short err;
    int length;
    FileInputNode* rec;
    char** contents;
    char buf[256];

    // (big-endian target != the host's byte order: perhaps macros in the original)
    nonNativeByteOrder = (!copts.littleendian) != 0;
    linkererrors = linkerwarnings = 0;

    err = 0;
    rec = Link_GetFileText(name, &err);
    if (err != 0 || rec == NULL) {
        linkerror(0x1d, name->name);
        if (rec != NULL) {
            DeleteArchiveInfo(rec);
        }
        return NULL;
    }
    if (rec->kind == 0) {
        linkerror(0x1e, name->name);
        DeleteArchiveInfo(rec);
        return NULL;
    }
    Imported_Obj_Codesize = 0;
    objidatasize = 0;
    objudatasize = 0;
    p2cstrcpy(buf, name->name);
    err = Import_File(rec, buf);
    if (err != 0) {
        DeleteArchiveInfo(rec);
        return NULL;
    }
    // (the header's length, set where it is first used: 2.6 loads the 8 into a register for this
    // call only; a separate `length = 8;` or a plain 8 pushes the constant)
    contents = COS_NewOSHandle(length = 8);
    if (contents == NULL) {
        contents = COS_NewHandle(length);
        if (contents == NULL) {
            linkerror(0x1d, name->name);
            DeleteArchiveInfo(rec);
            return NULL;
        }
    }
    COS_LockHandle(contents);
    if (rec->kind == 1) {
        strncpy(*contents, "!<arch>\n", length);
    } else {
        COS_LockHandle(rec->dataHandle);
        memcpy(*contents, *rec->dataHandle, length);
        COS_UnlockHandle(rec->dataHandle);
    }
    COS_UnlockHandle(contents);
    DeleteArchiveInfo(rec);
    *codesize = Imported_Obj_Codesize;
    *idatasize = objidatasize;
    *udatasize = objudatasize;
    return contents;
}

// Pro7's CCompiler.c setup_param_block (the prefix keeps row names unique), for what the importer
// needs
char ELF_Lib_Import_setup_param_block(CWPluginPrivateContext* handle) {
    libimp_cu.context = handle;
    memset(&libimp_cu.object, 0, sizeof(CWObjectData));
    if (CWGetMainFileNumber(handle, &libimp_cu.whichfile) != 0) {
        return FALSE;
    }
    if (CWGetMainFileSpec(handle, &libimp_cu.sourcefile) != 0) {
        return FALSE;
    }
    if (CWIsGeneratingDebugInfo(handle, &libimp_cu.SYMinfo) != 0) {
        return FALSE;
    }
    return TRUE;
}

// (Pro7's CCompiler.c store_compile_results, for the importer's object data.) The result
// variable holds the object handle first; each call's result is returned when not cwNoErr
unsigned int store_compile_results(void) {
    unsigned int result = libimp_cu.objectdata;

    if (result) {
        result =
            CWSecretAttachHandle(libimp_cu.context, (Handle)result, &libimp_cu.object.objectdata);
        if (result) {
            return result;
        }
    }
    result = CWStoreObjectData(libimp_cu.context, libimp_cu.whichfile, &libimp_cu.object);
    if (result) {
        return result;
    }
    return result;
}

// TODO: 2.6 builds the file at -O4,p up to here and at optimization level 2 from here to its end
// (each function before matches only at -O4,p, each after only with this pragma; -O2,p for the
// whole file, or other pragmas here, match neither). It is one object, not two files: functions on
// both sides push the one "!<arch>\n" literal and use the static libimp_cu. How the original
// spelled the setting is not known
#pragma optimization_level 2
CWPLUGIN_ENTRY(ELFImport_main)(CWPluginPrivateContext* context) {
    int result;
    char** status;
    long mode;
    PProject project;
    PLinker linker;
    Handle projectHandle;
    Handle linkerHandle;

    result = 0;
    CWGetPluginRequest(context, &mode);
    switch (mode) {
        case -2:
            result = 0;
            break;
        case -1:
            result = 0;
            break;
        case 0:
            memset(&copts, 0, 264);
            memset(&linkinfo, 0, sizeof(linkinfo));
            projectHandle = NULL;
            CWSecretGetNamedPreferences(context, "PPC EABI Project", &projectHandle);
            if (projectHandle != NULL) {
                project = *((PProject*)*projectHandle);
            }
            copts.littleendian = !project.endian;
            linkerHandle = NULL;
            CWSecretGetNamedPreferences(context, "PPC EABI Linker", &linkerHandle);
            if (linkerHandle != NULL) {
                linker = *((PLinker*)*linkerHandle);
            }
            linkinfo.nowarnings = linker.suppresswarn;
            Target64 = 0;
            ELF_Lib_Import_setup_param_block(context);
            libimp_cu.browsedata = 0;
            libimp_cu.object.compiledlines = 0;
            libimp_cu.objectdata = 0;
            if (initgheap(out_of_memory) != 0) {
                out_of_memory();
                result = 2;
                break;
            }
            status = ImportObjectLibrary(
                &libimp_cu.sourcefile,
                &libimp_cu.object.codesize,
                &libimp_cu.object.udatasize,
                &libimp_cu.object.idatasize
            );
            libimp_cu.objectdata = (unsigned int)status;
            if (status != NULL) {
                result = store_compile_results();
            } else {
                result = 2;
            }
            releasegheap();
            break;
    }
    result = CWDonePluginRequest(context, (short)result);
    return result;
}

void MyAlert(char* message) {
    CWAlert(libimp_cu.context, message, NULL, NULL, 0);
}

void OSAlert(char* message, short error) {
    CWReportMessage(libimp_cu.context, NULL, message, NULL, 2, error);
}

// The tick count at which the importer next lets the IDE break in. TODO: invented name
static SInt32 next_break_check;

void Linker_UserBreakCheck(void) {
    if (TickCount() > next_break_check) {
        if (CWUserBreak(libimp_cu.context) == 1) {
            longjmp(linkerexit, 1);
        }
        next_break_check = TickCount() + 15;
    }
}

// TODO: invented name. The test it is used for returns 0 either way
static char mwmagic[] = "mw";

unsigned char IsMWLibraryFile(CWFileSpec* input) {
    Boolean ok;
    short ref;
    SInt32 count;
    SInt32 header[2];

    count = 8;
    ok = FALSE;
    if (FSpOpenDF(input, 1, &ref) == 0) {
        if (FSRead(ref, &count, header) == 0 && count == 8) {
            ok = TRUE;
        }
        FSClose(ref);
    }
    if (ok) {
        if (IsELF((char*)header)) {
            return 4;
        }
        if (strncmp((char*)header, "!<arch>\n", 8) == 0) {
            return 1;
        }
        if (header[0] == mwmagic[0] && header[1] == mwmagic[1]) {
            return 0;
        }
    }
    return 0;
}

char** Link_GetFileSpecTextHandle(CWFileSpec* file, short* error, SInt32 extraBytes) {
    char** buffer;
    short fileRef;
    SInt32 fileSize;
    SInt32 bytesRead;
    *error = FSpOpenDF(file, 1, &fileRef);
    if (*error == 0) {
        *error = GetEOF(fileRef, &fileSize);
        if (*error != 0 || fileSize == 0) {
            FSClose(fileRef);
            return NULL;
        }
        buffer = COS_NewOSHandle(fileSize + extraBytes);
        if (buffer == NULL) {
            buffer = COS_NewHandle(fileSize + extraBytes);
            if (buffer == NULL) {
                FSClose(fileRef);
                *error = memFullErr;
                return NULL;
            }
        }
        COS_LockHandle(buffer);
        bytesRead = fileSize;
        *error = FSRead(fileRef, &bytesRead, *buffer);
        if (*error != 0 || bytesRead != fileSize) {
            COS_FreeHandle(buffer);
            FSClose(fileRef);
            return NULL;
        }
        if (extraBytes != 0) {
            (*buffer)[fileSize] = 0;
        }
        COS_UnlockHandle(buffer);
        FSClose(fileRef);
        return buffer;
    }
    return NULL;
}

FileInputNode* Link_GetFileText(CWFileSpec* name, SInt16* err) {
    FileInputNode* member;
    unsigned char type;
    FileInputNode* head;
    FileInputNode* last;
    char** data;
    char* header;
    SInt32 size;
    SInt16 handle;
    SInt32 count;
    SInt32 length;
    char buffer[8];
    SInt32 position;

    type = IsMWLibraryFile(name);
    if (type == 4) {
        member = (FileInputNode*)galloc(sizeof(FileInputNode));
        memset(member, 0, sizeof(FileInputNode));
        member->dataHandle = Link_GetFileSpecTextHandle(name, err, 0);
        member->kind = type;
        return member;
    }
    if (type == 1) {
        *err = FSpOpenDF(name, 1, &handle);
        if (*err != 0) {
            return NULL;
        }
        *err = GetEOF(handle, &size);
        if (*err != 0 || size == 0) {
            FSClose(handle);
            return NULL;
        }
        head = last = NULL;
        count = sizeof(buffer);
        *err = FSRead(handle, &count, buffer);
        if (*err != 0 || count != sizeof(buffer)) {
            FSClose(handle);
            return NULL;
        }
        position = sizeof(buffer);
        while (position < size) {
            member = (FileInputNode*)galloc(sizeof(FileInputNode));
            if (member == NULL) {
                FSClose(handle);
                *err = memFullErr;
                for (member = head; member; member = member->next) {
                    if (member->dataHandle) {
                        COS_FreeHandle(member->dataHandle);
                    }
                }
                return NULL;
            }
            if (head == NULL) {
                head = member;
            }
            memset(member, 0, sizeof(FileInputNode));
            member->kind = 1;
            count = 60; // an ar member header
            member->archiveMemberHeader = galloc(60);
            if (member->archiveMemberHeader == NULL) {
                FSClose(handle);
                for (member = head; member; member = member->next) {
                    if (member->dataHandle) {
                        COS_FreeHandle(member->dataHandle);
                    }
                }
                return NULL;
            }
            *err = FSRead(handle, &count, member->archiveMemberHeader);
            if (*err != 0 || count != 60) {
                FSClose(handle);
                for (member = head; member; member = member->next) {
                    if (member->dataHandle) {
                        COS_FreeHandle(member->dataHandle);
                    }
                }
                return NULL;
            }
            header = member->archiveMemberHeader;
            position += 60;
            sscanf(header + 48, "%ld", &length); // its ar_size
            data = COS_NewOSHandle(length);
            if (data == NULL) {
                data = COS_NewHandle(length);
                if (data == NULL) {
                    FSClose(handle);
                    *err = memFullErr;
                    for (member = head; member; member = member->next) {
                        if (member->dataHandle) {
                            COS_FreeHandle(member->dataHandle);
                        }
                    }
                    return NULL;
                }
            }
            member->dataHandle = data;
            COS_LockHandle(member->dataHandle);
            count = length;
            *err = FSRead(handle, &count, *member->dataHandle);
            if (*err != 0 || count != length) {
                FSClose(handle);
                for (member = head; member; member = member->next) {
                    if (member->dataHandle) {
                        COS_FreeHandle(member->dataHandle);
                    }
                }
                return NULL;
            }
            position += length;
            if ((position & 1) != 0 && position < size) {
                count = 1;
                *err = FSRead(handle, &count, buffer);
                if (*err != 0 || count != 1) {
                    FSClose(handle);
                    for (member = head; member; member = member->next) {
                        if (member->dataHandle) {
                            COS_FreeHandle(member->dataHandle);
                        }
                    }
                    return NULL;
                }
                position++;
            }
            COS_UnlockHandle(member->dataHandle);
            if (last != NULL) {
                last->next = member;
            }
            last = member;
        }
        FSClose(handle);
        return head;
    }
    return NULL;
}

void DeleteArchiveInfo(FileInputNode* args) {
    while (args != NULL) {
        if (args->dataHandle != NULL) {
            COS_FreeHandle(args->dataHandle);
        }
        args->dataHandle = NULL;
        args = args->next;
    }
}

unsigned char Inspect_ELF_Header(Elf32_Ehdr* record, char* location, char* name) {
    // EI_DATA: ELFDATA2LSB, ELFDATA2MSB; EI_CLASS: ELFCLASS32, ELFCLASS64; e_machine: EM_PPC, and 17
    if (record->e_ident[5] == 1) {
        if (!copts.littleendian) {
            linkwarning(48, location, name);
            return FALSE;
        }
    } else if (record->e_ident[5] == 2) {
        if (copts.littleendian) {
            linkwarning(48, location, name);
            return FALSE;
        }
    } else {
        linkwarning(46, "EI_DATA", location, name);
        return FALSE;
    }

    if (record->e_ident[4] == 1) {
        if (Target64) {
            linkwarning(47, location, name);
            return FALSE;
        }
    } else if (record->e_ident[4] == 2) {
        if (!Target64) {
            linkwarning(47, location, name);
            return FALSE;
        }
    } else {
        linkwarning(46, "EI_CLASS", location, name);
        return FALSE;
    }

    if (record->e_machine != 20 && record->e_machine != 17) {
        linkwarning(
            54,
            *name ? name : location,
            *name ? " of archive '" : "",
            *name ? location : "",
            *name ? "'" : "",
            "PowerPC"
        );
        return FALSE;
    }
    return TRUE;
}

void Import_Object_File(FileInputNode* input, char* location, char* name) {
    Elf32_Ehdr* header;
    Elf32_Shdr* section;
    int section_index;
    char* image;
    COS_LockHandleHi(input->dataHandle);
    image = (char*)*input->dataHandle;
    Linker_UserBreakCheck();
    header = (Elf32_Ehdr*)image;
    if (nonNativeByteOrder != 0) {
        Convert_Elf32_Ehdr(header);
    }
    if (Inspect_ELF_Header(header, location, name)) {
        for (section_index = 0; section_index < header->e_shnum; ++section_index) {
            section = (Elf32_Shdr*)(image + header->e_shoff + section_index * sizeof(Elf32_Shdr));
            if (nonNativeByteOrder != 0) {
                Convert_Elf32_Shdr(section);
            }
            if ((section->sh_flags & SHF_EXECINSTR) != 0) {
                Imported_Obj_Codesize += section->sh_size;
            } else if ((section->sh_flags & SHF_ALLOC) != 0) {
                if (section->sh_type == SHT_PROGBITS) {
                    objidatasize += section->sh_size;
                } else if (section->sh_type == SHT_NOBITS) {
                    objudatasize += section->sh_size;
                }
            }
        }
    }
    COS_UnlockHandle(input->dataHandle);
}

char* Get_AR_Name(char** baseOffset, char* text) {
    char* base;
    int index;
    index = 0;
    base = NULL;

    if (baseOffset != NULL) {
        base = *baseOffset;
    }

    if (base == NULL && text[0] == '/') {
        ++index;
        if (text[index] == '/') {
            ++index;
        }
        if (text[index] == ' ' || text[index] == 0) {
            text[index] = 0;
            return text;
        }
    }

    if (text[0] == '/' && base != NULL && isdigit(text[1])) {
        sscanf(text + 1, "%ld", &index);
        return base + index;
    }

    for (index = 15; index != 0; --index) {
        if (text[index] == '/' || text[index] == 0) {
            text[index] = 0;
            return text;
        }
    }
    text[15] = 0;
    return text;
}

void term_ar_string_table(char** buffer, int length) {
    char* text = *buffer;
    int index;
    for (index = length - 1; index > 1; --index) {
        if (text[index] == '\n' && text[index - 1] == '/') {
            text[index - 1] = 0;
        }
    }
}

void Import_Archive(FileInputNode* list, char* arg) {
    char** longNames;
    char* name;
    char** data;
    FileInputNode* node;
    char* header;

    longNames = NULL;
    for (node = list; node; node = node->next) {
        header = node->archiveMemberHeader;
        data = node->dataHandle;
        name = Get_AR_Name(longNames, header);

        if (!GetHandleSize(data)) {
            linkwarning(0x7e, arg, name);
            continue;
        }
        if (!memcmp(name, "//", 3)) {
            longNames = data;
            COS_LockHandleHi(data);
            term_ar_string_table(data, GetHandleSize(data));
            node->kind = 3;
        }
        if (!IsELF(*data)) {
            continue;
        }
        node->kind = 2;
        Import_Object_File(node, arg, name);
    }
    if (longNames) {
        COS_UnlockHandle(longNames);
    }
}

short Import_File(FileInputNode* record, char* buffer) {
    short result = setjmp(linkerexit);

    if (result == 0) {
        if (record->kind == 1) {
            Import_Archive(record, buffer);
        } else if (record->kind == 4) {
            Import_Object_File(record, buffer, "");
        } else {
            linkerror(0x39, buffer);
            linkfailed(0xd, 2);
        }
    } else {
        linkalert(result);
    }
    return result;
}
