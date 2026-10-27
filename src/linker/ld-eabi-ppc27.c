/* The 2.7 linker plugin's glue: 2.6's (ld-eabi-ppc.c) with 2.7's panels (the Project panel's 256-byte output name
   from its version 8, its ABI and DWARF settings) and what 2.7 does with them (0x4216b0, InitLinkOptionsFromPrefs;
   0x4229c0, the entry; 0x422ff0, the panels older than the linker knows).

   Written from the original's code (there is no source of it), equivalent rather than matching. */
#include "compiler/common.h"
#include "compiler/CompilerTools.h"
#include "driver/libimp-eabi-ppc.h"
#include <setjmp.h>
#include <ctype.h>
#include "driver/CWPluginsPrivate.h"
#include "driver/DropInCompilerLinkerPrivate.h"
#include "driver/Memory.h"
#include "driver/COSToolsCLT.h"
#include "driver/Files.h"
#include "driver/PrefPanels.h"
#include "driver/CWParserPluginsPrivate.h"
#include "linker/ELF_linker.h"

extern void CompilerTools_FormatMessageAndLongjmp(int message, int status, ...);
extern void CompilerTools_ReportLimitedDiagnostic(int message, ...);
extern int sscanf(const char *s, const char *format, ...);
extern unsigned long strlen(const char *s);
extern char *strcpy(char *dst, const char *src);
extern int strcmp(const char *a, const char *b);
extern int strncmp(const char *a, const char *b, unsigned long n);
extern void *memset(void *dst, int c, unsigned long n);
extern void *memcpy(void *dst, const void *src, unsigned long n);

extern __declspec(dllimport) unsigned short __stdcall GetSystemDefaultLangID(void);

/* The driver's helpers (the 2.x linker's plugin API: TextUtils.c, DropInCompilerLinkerPrivate.c, COSToolsCLT.c and
   CPrep.c). */
/* a Toolbox EqualString: two Pascal strings equal (case and marks as asked) */
extern Boolean __stdcall fn_00413620(const unsigned char *a, const unsigned char *b, Boolean caseSensitive,
                                     Boolean diacriticSensitive);
/* a project file's data, by the id fn_0042f570 gave */
extern int __stdcall fn_0041a460(CWPluginPrivateContext *context, long id, char ***data);
/* a file spec for a name in a directory */
extern int fn_0042f0d0(short vRefNum, long parID, const unsigned char *name, CWFileSpec *spec);
/* two file specs name the same file */
extern Boolean fn_0042f280(CWFileSpec *a, CWFileSpec *b);
/* the target's output, given to the IDE */
extern int __stdcall fn_0042f400(CWPluginPrivateContext *context, void *info);
/* a project file's data loaded (its id, to give back with ELF_linker_CallCallback) */
extern int __stdcall fn_0042f570(CWPluginPrivateContext *context, long index, long *id);

extern int __stdcall ELF_linker_CallCallback(CWPluginPrivateContext *context, long index, long id);
extern int ELF_linker_Link(void);
extern Boolean ELF_linker_IsELF(char *image);
extern int ELF_linker_Library(void);
extern void PrepDumpELF_PStrCat(unsigned char *dst, const unsigned char *src);
extern void PrepDumpELF_PStrCopy(unsigned char *dst, const unsigned char *src);
extern short PrepDumpELF_Dump(ELMember *file, char ***output, char *name, CWFileSpec *spec);

#pragma options align = mac68k
/* The file the IDE describes (CLDropinCallbacks_V10.h's ExportedRecord). */
typedef struct LinkerFileInfo {
    CWFileSpec file;
    long x46;
    short x4a;
    char linkWhole;                 /* +0x4c: an object linked whole (a disassembly: the IDE has its data) */
    char binary1;                   /* +0x4d, +0x4e: a file linked as its bytes */
    char binary2;
    char x4f[0x7a - 0x4f];
    long fileType;                  /* +0x7a */
    char x7e[0x88 - 0x7e];
} LinkerFileInfo;

/* The plugin's context (the linker's code reaches it through active_plugin_cu). */
typedef struct LinkerCU {
    CWPluginPrivateContext *context;
    CWFileSpec sourceFile;          /* +4 */
    CWFileSpec outputDirectory;     /* +0x4a */
    long fileCount;                 /* +0x90 */
    unsigned int fileIndex;         /* +0x94 */
    char **output;                  /* +0x98: a disassembly's text */
} LinkerCU;

/* "PPC EABI Project" (the linker's view of PrefPanels.h's PProject). */
typedef struct LinkerProject {
    short version;
    short type;                     /* 0 a program, 1 a library, 2 a partial link */
    unsigned char outfile[32];
    UInt32 heapsize;
    UInt32 stacksize;
    char bigendian;
    UInt8 x2d;
    short sdatathreshold;
    short sdata2threshold;
    short codemodel;
    UInt8 x34;
    UInt8 x35;
    UInt8 x36[2];
    UInt8 x38;
    UInt8 x39;
    UInt8 x3a;
    unsigned char outname[256];     /* +0x3b: the output's name (from version 8; before it, outfile) */
    UInt8 abi;                      /* +0x13b */
    UInt8 dwarf;                    /* +0x13c: 0 DWARF 1, 1 DWARF 2 */
    UInt8 tunerelocations;          /* +0x13d */
    UInt8 x13e[5];
    char abiconfig[257];            /* +0x143 */
} LinkerProject;

/* The project's output name, by the panel's version. */
#define PROJECT_NAME(project) ((project).version < 8 ? (project).outfile : (project).outname)

/* "PPC EABI CodeGen": the back end's, and (from version 12) the processor's name. */
typedef struct LinkerCodeGen {
    struct PBackEnd backEnd;
    char processorName[16];
} LinkerCodeGen;

/* The target's output (CWTargetInfo). */
typedef struct LinkerTargetInfo {
    short outputType;
    CWFileSpec outfile;
    CWFileSpec symfile;
    CWFileSpec runfile;
    short linkType;
    Boolean canRun;
    Boolean canDebug;
    UInt32 targetCPU;
    UInt32 targetOS;
    UInt32 outfileCreator;
    UInt32 outfileType;
    UInt32 debuggerCreator;
    UInt32 runHelperCreator;
    CWFileSpec linkAgainstFile;
} LinkerTargetInfo;
#pragma options align = reset

LinkerCU linker_cu;
extern LinkerCU *active_plugin_cu;
extern jmp_buf file_input_jmpbuf;
extern unsigned char copts[];

/* The linker's settings (ELF_gen.c, ELF_linker.c, LK_symbol.c...). */
extern char data_0058770c[];            /* the settings; its first short: the output kind */
extern CWFileSpec data_0049bed0;        /* the output file */
extern CWFileSpec data_0049bf16;        /* the S-record file */
extern CWFileSpec data_0049bf5c;        /* the link map file */
extern long data_0049c100;              /* the S-record file's creator */
extern long data_0049c104;              /* the link map's creator and type */
extern long data_0049c108;
extern long data_0049c10c;
extern char data_0049c115;              /* the link map shows full paths */
extern Boolean data_0049c116;           /* a link map is written */
extern char data_00587958;
extern char data_0049c120;              /* 2: the ELF file is written directly */
extern char data_0049c161[];            /* the entry point's name */
extern long data_0049c2f4;              /* the output's file type */
extern long data_0049c2f8;              /* its creator */
extern short data_0049c418;             /* the processor */
extern Boolean data_0049c41a;           /* floating point in hardware */
extern Boolean data_0049c41b;
extern char data_0049c41c;
extern unsigned long data_004a2214;     /* the data address */
extern unsigned long data_004a276c;     /* the small data address */
extern unsigned long data_004a2824;     /* the second small data address */
extern unsigned long data_004a2828;     /* the code address */
extern unsigned long data_004a284c;     /* the heap address */
extern unsigned long data_004a2870;     /* the heap's size */
extern unsigned long data_004a28d8;     /* the stack's size */
extern unsigned long data_004a28f0;     /* the stack address */
extern unsigned long data_004a28f8;     /* the ROM image's address */
extern long data_004a2a80;              /* the RAM buffer's */
extern char data_004a2d6e;              /* the disassembly's: code */
extern char data_004a2d6f;              /* extended mnemonics */
extern char data_004a2d71;              /* no binary */
extern char data_004a2d72;              /* data */
extern char data_004a2d73;              /* exception tables */
extern char data_004a2d74;              /* debug information */
extern char data_004a2dc2;              /* everything in hex (detail) */
extern char data_004a2dc3;              /* debug information relocated */
extern char data_004a2dc4;              /* headers */
extern char data_004a2dc5;              /* symbols and string tables */
extern Boolean data_004a2e36;           /* a linker command file is read */
extern char data_004a2e37;              /* DWARF is written */
extern Boolean data_004a2e39;
extern Boolean data_004a2e3a;           /* an S-record file is written */
extern Boolean data_004a2e3f;           /* a small data area (sdata) */
extern Boolean data_004a2e40;           /* the procedure records are read */
extern Boolean data_004a2e42;           /* a second small data area (sdata2) */
extern char data_004a2e43;
extern Boolean data_004a2e44;
extern Boolean data_004a2e45;           /* ROM images */
extern char data_004a2e47;              /* the link map shows the unused symbols */
extern Boolean data_004a2e49;
extern Boolean data_004a2e4f;           /* EXCLUDEFILES applies */
extern Boolean data_004a2e52;           /* a data area */
extern char data_004a2e58;              /* a code address */
extern Boolean data_004a2e59;           /* make the segment symbols */
extern Boolean data_004a2e5a;
extern short data_004897a0;             /* SRecord.c's: the S-record length */
extern char data_004897a4;              /* its end of line */

/* The Linker panel's map settings (from its version 6; before it, the closure is shown). */
Boolean ELF_linker_listClosure;
Boolean ELF_linker_listDwarf;

static char data_00482dd0 = 2;
static const unsigned char data_00482e7c[] = {4, '.', 'e', 'l', 'f'};
static const unsigned char data_00482e84[] = {4, '.', 'E', 'L', 'F'};
static const unsigned char data_00482f14[] = {12, 'D', 'u', 'm', 'p', 'E', 'L', 'F', '.', 'd', 'u', 'm', 'p'};
static const unsigned char data_00482f30[] = {4, '.', 'm', 'o', 't'};
static const unsigned char data_00482f38[] = {4, '.', 'M', 'A', 'P'};

static unsigned long data_004998e8;     /* the ticks before the next break check */
static int data_004998ec;               /* the license */
static short data_004998f0;             /* the panels' versions: CodeGen's */
static short data_004998f2;             /* Project's */
static short data_004998f4;             /* Linker's */
static short data_004998f6;             /* Disassembler's */
static struct PCLTExtras data_004998f8;
static struct PDisassembler data_00499afa;
static LinkerCodeGen data_00499b08;
static struct PLinker data_00499b34;
static LinkerProject data_00499ba8;

/* The processor a name gives. */
static short fn_0041dcc0(char *name)
{
    if (!strcmp(name, "401"))
        return 0;
    if (!strcmp(name, "403"))
        return 1;
    if (!strcmp(name, "505"))
        return 2;
    if (!strcmp(name, "509"))
        return 3;
    if (!strcmp(name, "555"))
        return 4;
    if (!strcmp(name, "601"))
        return 5;
    if (!strcmp(name, "602"))
        return 6;
    if (!strcmp(name, "603"))
        return 7;
    if (!strcmp(name, "603e"))
        return 8;
    if (!strcmp(name, "604"))
        return 9;
    if (!strcmp(name, "604e"))
        return 10;
    if (!strcmp(name, "740"))
        return 11;
    if (!strcmp(name, "750"))
        return 12;
    if (!strcmp(name, "801"))
        return 13;
    if (!strcmp(name, "821"))
        return 14;
    if (!strcmp(name, "823"))
        return 15;
    if (!strcmp(name, "850"))
        return 16;
    if (!strcmp(name, "860"))
        return 17;
    if (!strcmp(name, "7400"))
        return 21;
    if (!strcmp(name, "7450"))
        return 24;
    if (!strcmp(name, "8240"))
        return 18;
    if (!strcmp(name, "8260"))
        return 19;
    return 20;
}

/* The panels older than the linker knows. */
void fn_0041e000(void)
{
    Boolean old = 0;

    if (data_004998f2 < 9) {
        CompilerTools_ReportLimitedDiagnostic(0x84, "EPPC Project");
        old = 1;
    }
    if (data_004998f4 < 5) {
        CompilerTools_ReportLimitedDiagnostic(0x84, "EPPC Linker");
        old = 1;
    }
    if (data_004998f6 < 3) {
        CompilerTools_ReportLimitedDiagnostic(0x84, "EPPC Disassembler");
        old = 1;
    }
    if (old)
        CompilerTools_FormatMessageAndLongjmp(0xd, 2);
}

static void fn_0041e050_free(ELMember *head)
{
    for (; head; head = head->next)
        if (head->data)
            Memory_FreeHandle((StorageHandle *)head->data);
}

static char **fn_0041e530(CWFileSpec *file, short *error, long extraBytes);
static unsigned char fn_004201a0(CWFileSpec *file);

/* A file's parts: an object (kind 4) or an archive's members (kind 1). */
static ELMember *fn_0041e070(CWFileSpec *name, short *error)
{
    ELMember *member;
    ELMember *head;
    ELMember *last;
    unsigned char kind;
    short handle;
    SInt32 size;
    SInt32 count;
    long length;
    long position;
    char buffer[8];

    kind = fn_004201a0(name);
    if (kind == 4) {
        member = (ELMember *)galloc(sizeof(ELMember));
        memset(member, 0, sizeof(ELMember));
        member->data = fn_0041e530(name, error, 0);
        member->kind = 4;
        return member;
    }
    if (kind != 1)
        return NULL;
    *error = Files_OpenFile(name, 1, &handle);
    if (*error)
        return NULL;
    *error = Files_GetSize(handle, &size);
    if (*error || size == 0) {
        Files_Close(handle);
        return NULL;
    }
    head = last = NULL;
    count = sizeof(buffer);
    *error = Files_Read(handle, &count, buffer);
    if (*error || count != sizeof(buffer)) {
        Files_Close(handle);
        return NULL;
    }
    position = sizeof(buffer);
    while (position < size) {
        member = (ELMember *)galloc(sizeof(ELMember));
        if (member == NULL) {
            Files_Close(handle);
            *error = -0x6c;
            fn_0041e050_free(head);
            return NULL;
        }
        if (head == NULL)
            head = member;
        memset(member, 0, sizeof(ELMember));
        member->kind = 1;
        count = 0x3c;
        member->name = galloc(0x3c);
        if (member->name == NULL) {
            Files_Close(handle);
            fn_0041e050_free(head);
            return NULL;
        }
        *error = Files_Read(handle, &count, member->name);
        if (*error || count != 0x3c) {
            Files_Close(handle);
            fn_0041e050_free(head);
            return NULL;
        }
        sscanf(member->name + 0x30, "%ld", &length);
        member->data = COS_NewOSHandle(length);
        if (member->data == NULL && (member->data = COS_NewHandle(length)) == NULL) {
            Files_Close(handle);
            *error = -0x6c;
            fn_0041e050_free(head);
            return NULL;
        }
        COS_LockHandle(member->data);
        count = length;
        *error = Files_Read(handle, &count, *member->data);
        if (*error || count != length) {
            Files_Close(handle);
            fn_0041e050_free(head);
            return NULL;
        }
        position += 0x3c + length;
        if ((position & 1) && position < size) {
            count = 1;
            *error = Files_Read(handle, &count, buffer);
            if (*error || count != 1) {
                Files_Close(handle);
                fn_0041e050_free(head);
                return NULL;
            }
            position++;
        }
        COS_UnlockHandle(member->data);
        if (last)
            last->next = member;
        last = member;
    }
    Files_Close(handle);
    return head;
}

/* A project file's text, when it is a linker command file (.lcf), ending with a zero. */
char **fn_0041e410(long index)
{
    LinkerFileInfo info;
    unsigned char *name;
    unsigned int length;
    char **data;
    short error;

    CWPluginsPrivate_InvokeExportedRecordCallback(active_plugin_cu->context, index, 1, (struct ExportedRecord *)&info);
    name = info.file.name;
    length = name[0];
    if (length > 4 && name[length - 3] == '.' && tolower(name[length - 2]) == 'l' && tolower(name[length - 1]) == 'c'
        && tolower(name[length]) == 'f') {
        data = fn_0041e530(&info.file, &error, 1);
        if (data == NULL || error) {
            CompilerTools_ReportLimitedDiagnostic(3, info.file.name);
            CompilerTools_FormatMessageAndLongjmp(0xd, error);
        }
        return data;
    }
    return NULL;
}

/* A file's bytes in a handle (and extraBytes more, the first one zero). */
static char **fn_0041e530(CWFileSpec *file, short *error, long extraBytes)
{
    char **buffer;
    short handle;
    SInt32 size;
    SInt32 count;

    *error = Files_OpenFile(file, 1, &handle);
    if (*error)
        return NULL;
    *error = Files_GetSize(handle, &size);
    if (*error || size == 0) {
        Files_Close(handle);
        return NULL;
    }
    buffer = COS_NewOSHandle(size + extraBytes);
    if (buffer == NULL && (buffer = COS_NewHandle(size + extraBytes)) == NULL) {
        Files_Close(handle);
        *error = -0x6c;
        return NULL;
    }
    COS_LockHandle(buffer);
    count = size;
    *error = Files_Read(handle, &count, *buffer);
    if (*error == 0 && count == size) {
        if (extraBytes)
            (*buffer)[size] = 0;
        COS_UnlockHandle(buffer);
        Files_Close(handle);
        return buffer;
    }
    Memory_FreeHandle((StorageHandle *)buffer);
    Files_Close(handle);
    return NULL;
}

static void fn_0041e660(void)
{
    CompilerTools_FormatMessageAndLongjmp(0xd, -0x6c);
}

static short fn_0041ed20(void);
static int fn_0041f1c0(void);

/* A panel's data, when the IDE has it. */
#define READ_PANEL(context, name, panel)                                                                               \
    do {                                                                                                               \
        void **reference = NULL;                                                                                       \
        DropInCompilerLinkerPrivate_CallArgumentValue(context, name, &reference);                                      \
        if (reference)                                                                                                 \
            memcpy(&(panel), *reference, sizeof(panel));                                                               \
    } while (0)

static void fn_0041e670_start(CWPluginPrivateContext *context)
{
    active_plugin_cu = &linker_cu;
    linker_cu.context = context;
    CWPluginsPrivate_GetNumFiles(context, &linker_cu.fileCount);
    CWPluginsPrivate_GetSourceFile(context, &linker_cu.sourceFile);
    CWPluginsPrivate_GetOutputFileDirectory(context, &linker_cu.outputDirectory);
    CPrep_GetFileIndex(context, &linker_cu.fileIndex);
    linker_cu.output = NULL;
}

/* The plugin's entry: 0 link, 1 disassemble, 2 the target's output; -1 and -2 the license given back. */
int __stdcall fn_0041e670(CWPluginPrivateContext *context)
{
    short result;
    long request;
    LinkerProject project;
    struct PLinker linker;
    void *codeGen;
    LinkerTargetInfo info;
    CWFileSpec directory;
    void **reference;

    result = 0;
    CWPluginsPrivate_GetRequest(context, &request);
    switch (request) {
        case 0:
            if (data_004998ec == 0)
                CWPluginsPrivate_InvokeCallback40(context, "Win32_Plugins_PPC_Nintendo",
                                                  GetSystemDefaultLangID() == 0x411 ? "2" : "1", 0, 0,
                                                  &data_004998ec);
            if (data_004998ec == 0) {
                result = 10;
                break;
            }
            memset(copts, 0, 0x106);
            memset(data_0058770c, 0, 0x596);
            fn_0041e670_start(context);
            READ_PANEL(context, "PPC EABI Project", data_00499ba8);
            READ_PANEL(context, "PPC EABI Linker", data_00499b34);
            READ_PANEL(context, "PPC EABI CodeGen", data_00499b08);
            READ_PANEL(context, "PPC EABI Disassembler", data_00499afa);
            {
                /* (2.7: the compiler's panel asked for too, unused here) */
                void **reference = NULL;

                DropInCompilerLinkerPrivate_CallArgumentValue(context, "C/C++ Compiler", &reference);
            }
            READ_PANEL(context, "CmdLine Extras EPPC", data_004998f8);
            copts[0] = data_00499ba8.bigendian == 0;
            data_004a2e37 = data_00499b34.generatesyminfo;
            ELF_linker_listClosure = data_00499b34.version > 5 ? data_00499b34.listclosure : 1;
            ELF_linker_listDwarf = data_00499b34.version > 5 ? data_00499b34.listdwarf : 1;
            if (data_00499ba8.type == 1) {
                *(short *)data_0058770c = 3;
                data_0049c120 = data_00482dd0;
                fn_0042f0d0(linker_cu.outputDirectory.vRefNum, linker_cu.outputDirectory.parID, PROJECT_NAME(data_00499ba8),
                            &data_0049bed0);
                data_0049c2f8 = 'CWIE';
                data_0049c2f4 = 'MPLF';
                result = ELF_linker_Library();
            } else {
                result = fn_0041f1c0();
            }
            break;
        case 1:
            memset(copts, 0, 0x106);
            memset(data_0058770c, 0, 0x596);
            memset(&data_004a2d6e, 0, 0x5a);
            fn_0041e670_start(context);
            READ_PANEL(context, "PPC EABI Disassembler", data_00499afa);
            READ_PANEL(context, "PPC EABI Project", data_00499ba8);
            READ_PANEL(context, "PPC EABI CodeGen", data_00499b08);
            READ_PANEL(context, "PPC EABI Linker", data_00499b34);
            READ_PANEL(context, "CmdLine Extras EPPC", data_004998f8);
            copts[0] = data_00499ba8.bigendian == 0;
            result = fn_0041ed20();
            break;
        case 2:
            READ_PANEL(context, "PPC EABI Project", project);
            READ_PANEL(context, "PPC EABI Linker", linker);
            reference = NULL;
            DropInCompilerLinkerPrivate_CallArgumentValue(context, "PPC EABI CodeGen", &reference);
            memset(&info, 0, sizeof(info));
            CWPluginsPrivate_GetOutputFileDirectory(context, &directory);
            info.outfile.vRefNum = directory.vRefNum;
            info.outfile.parID = directory.parID;
            PrepDumpELF_PStrCopy(info.outfile.name, PROJECT_NAME(project));
            info.symfile = info.outfile;
            info.linkAgainstFile = info.outfile;
            info.debuggerCreator = 'MWDB';
            info.canRun = 0;
            info.canDebug = linker.generatesyminfo && project.type != 1;
            info.linkType = 0;
            info.outputType = 1;
            info.runfile = info.outfile;
            info.targetCPU = 'ePPC';
            info.targetOS = 'EABI';
            info.outfileCreator = 'CWIE';
            info.outfileType = project.type == 1 ? 'MPLF' : 'ELF ';
            fn_0042f400(context, &info);
            break;
        case -2:
            data_004998ec = 0;
            break;
        case -1:
            if (data_004998ec) {
                fn_0041bc50(context, data_004998ec);
                data_004998ec = 0;
            }
            break;
    }
    return CWPluginsPrivate_ReturnArgument(context, result);
}

/* A Pascal name's extension is .elf (or .ELF). */
static Boolean fn_elf_extension(const unsigned char *name)
{
    unsigned char extension[256];
    int i;
    int j;

    extension[0] = 0;
    for (i = name[0]; i > 0 && name[i] != '.'; i--)
        ;
    if (i > 1) {
        extension[0] = name[0] - i + 1;
        for (j = 0; i + j <= name[0]; j++)
            extension[1 + j] = name[i + j];
    }
    return fn_00413620(extension, data_00482e7c, 0, 1) || fn_00413620(extension, data_00482e84, 0, 1);
}

static void fn_free_members(ELMember *member)
{
    for (; member; member = member->next) {
        if (member->data)
            Memory_FreeHandle((StorageHandle *)member->data);
        member->data = NULL;
    }
}

/* -dis: the project's file dumped (from the disk, or the IDE's data of it). */
static short fn_0041ed20(void)
{
    LinkerFileInfo info;
    char path[256];
    CWFileSpec dump;
    ELMember *members;
    char **data;
    long id;
    short error;
    struct ValuePairState pair;

    data_004998f0 = data_00499b08.backEnd.version;
    data_004998f2 = data_00499ba8.version;
    data_004998f4 = data_00499b34.version;
    data_004998f6 = data_00499afa.version;
    linker_cu.output = NULL;
    fn_0042f0d0(linker_cu.sourceFile.vRefNum, linker_cu.sourceFile.parID, data_00482f14, &dump);
    CWPluginsPrivate_InvokeExportedRecordCallback(linker_cu.context, linker_cu.fileIndex, 1,
                                                  (struct ExportedRecord *)&info);
    COS_FileGetPathName(path, (CPrepFileInfo *)&info, NULL);
    data_0049c418 = data_00499b08.backEnd.processor;
    data_0049c120 = data_00482dd0;
    data_004a2dc2 = data_00499afa.showdetail;
    data_004a2d74 = data_00499afa.showdebug;
    data_004a2dc3 = data_00499afa.relocate;
    data_004a2d6e = data_00499afa.showcode;
    data_004a2d6f = data_00499afa.extended;
    data_004a2d72 = data_00499afa.showdata;
    data_004a2d73 = data_00499afa.showxtables;
    data_004a2dc4 = data_00499afa.showheaders;
    data_004a2dc5 = data_00499afa.showtables;
    data_004a2d71 = data_00499afa.nobinary;
    if (initheaps(fn_0041e660))
        CompilerTools_FormatMessageAndLongjmp(0xd, -0x6c);
    error = 0;
    if (info.fileType != 'MPLF' && info.fileType != 'ELF ' && !fn_elf_extension(info.file.name)) {
        if (info.linkWhole) {
            error = fn_0042f570(linker_cu.context, linker_cu.fileIndex, &id);
            if (error == 0) {
                fn_0041a460(linker_cu.context, id, &data);
                if (COS_GetHandleSize(data) == 8) {
                    if (ELF_linker_IsELF(*data) || strncmp(*data, "!<arch>\n", 8) == 0) {
                        ELF_linker_CallCallback(linker_cu.context, linker_cu.fileIndex, id);
                        members = fn_0041e070(&info.file, &error);
                        if (error == 0 && members) {
                            error = PrepDumpELF_Dump(members, &linker_cu.output, path, &dump);
                            fn_free_members(members);
                        }
                    }
                } else {
                    members = (ELMember *)galloc(sizeof(ELMember));
                    memset(members, 0, sizeof(ELMember));
                    members->data = data;
                    members->kind = 5;
                    error = PrepDumpELF_Dump(members, &linker_cu.output, path, &dump);
                    ELF_linker_CallCallback(linker_cu.context, linker_cu.fileIndex, id);
                }
            }
        }
    } else {
        members = fn_0041e070(&info.file, &error);
        if (error == 0 && members) {
            error = PrepDumpELF_Dump(members, &linker_cu.output, path, &dump);
            fn_free_members(members);
        }
    }
    releasegheap();
    if (linker_cu.output) {
        pair.firstValue = NULL;
        fn_0041bcb0(linker_cu.context, (struct StorageHandle *)linker_cu.output, (long *)&pair.secondValue);
        pair.flag = 0;
        return CWPluginsPrivate_CallValuePairCallback(linker_cu.context, &pair);
    }
    return error;
}

/* A name made from the output file's: its .elf replaced with an extension (or a name of its own). */
static void fn_side_file(unsigned char *name, const unsigned char *own, const unsigned char *extension,
                         CWFileSpec *spec)
{
    if (own[0] == 0) {
        PrepDumpELF_PStrCopy(name, PROJECT_NAME(data_00499ba8));
        if (fn_elf_extension(name))
            name[0] -= 4;
        if (name[0] > 0x3b)
            name[0] = 0x3b;
        PrepDumpELF_PStrCat(name, extension);
    } else {
        PrepDumpELF_PStrCopy(name, own);
    }
    fn_0042f0d0(linker_cu.outputDirectory.vRefNum, linker_cu.outputDirectory.parID, name, spec);
}

/* The processor the back end's number names, by the panel's version. Each version's switch falls into the older
   version's after it, so from version 3 to 9 the 1-2 table decides (a number past it: 20). */
static void fn_processor(void)
{
    static const short v1[14] = {0, 1, 2, 3, 5, 6, 7, 9, 11, 12, 13, 14, 17, 20};
    static const short v3[15] = {0, 1, 2, 3, 5, 6, 7, 9, 11, 12, 13, 14, 15, 17, 20};
    static const short v4[17] = {0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17, 16};
    static const short v5[18] = {0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 20};
    unsigned long processor = data_00499b08.backEnd.processor;

    switch (data_00499b08.backEnd.version) {
        case 6:
        case 7:
        case 8:
        case 9:
            if (processor < 18)
                data_0049c418 = processor;
            else if (processor == 18)
                data_0049c418 = 19;
            else
                data_0049c418 = 20;
        case 5:
            data_0049c418 = processor < 18 ? v5[processor] : 20;
        case 4:
            data_0049c418 = processor < 17 ? v4[processor] : 16;
        case 3:
            data_0049c418 = processor < 15 ? v3[processor] : 20;
        case 1:
        case 2:
            if (processor < 14)
                data_0049c418 = v1[processor];
            break;
        default:
            data_0049c418 = processor;
            break;
    }
}

/* The link: the panels into the linker's settings. */
static int fn_0041f1c0(void)
{
    unsigned char name[256];
    Boolean commandFile;

    data_004998f0 = data_00499b08.backEnd.version;
    data_004998f2 = data_00499ba8.version;
    data_004998f4 = data_00499b34.version;
    data_004998f6 = data_00499afa.version;
    data_004a2e49 = 0;
    data_004a2e4f = 0;
    data_004a2e39 = 0;
    data_004a2e5a = 0;
    data_004a2e44 = 0;
    data_004a2e59 = 0;
    if (data_00499ba8.type == 2) {
        *(short *)data_0058770c = 6;
        data_0049c2f4 = 'MPLF';
        data_004a2e49 = 1;
        if (data_00499ba8.version > 6) {
            data_004a2e4f = data_00499ba8.x39;
            data_004a2e44 = 1;
            if (data_00499ba8.x39) {
                data_004a2e39 = data_00499ba8.x38;
                data_004a2e5a = data_00499ba8.x3a;
            }
        }
    } else if (data_00499ba8.type == 0) {
        *(short *)data_0058770c = 0;
        data_0049c2f4 = 'ELF ';
        data_004a2e39 = 1;
        data_004a2e44 = 0;
        data_004a2e5a = 1;
    }
    data_0049c120 = data_00482dd0;
    commandFile = 1;
    if (data_004a2e49 && !data_004a2e4f)
        commandFile = 0;
    data_004a2e36 = data_00499b34.uselcf;
    if (!commandFile)
        data_004a2e36 = 0;
    data_004a2828 = 0;
    data_004a2214 = 0;
    data_004a276c = 0;
    data_004a2824 = 0;
    data_004a2e42 = 0;
    data_004a2e3f = 0;
    data_004a2e52 = 0;
    data_004a2e58 = 0;
    if (!data_004a2e36) {
        data_004a2828 = 0x10000;
        if ((data_004a2e58 = data_00499b34.hascodeaddr) != 0)
            data_004a2828 = data_00499b34.codeaddr;
        if ((data_004a2e52 = data_00499b34.hasdataaddr) != 0)
            data_004a2214 = data_00499b34.dataaddr;
        if ((data_004a2e3f = data_00499b34.hassdataaddr) != 0)
            data_004a276c = data_00499b34.sdataaddr;
        if ((data_004a2e42 = data_00499b34.unk0c) != 0)
            data_004a2824 = data_00499b34.sdata2addr;
    }
    data_004a2e45 = data_00499b34.hasromaddr;
    if (*(short *)data_0058770c != 0)
        data_004a2e45 = 0;
    data_004a2a80 = 0;
    data_004a28f8 = 0;
    if (data_004a2e45) {
        data_004a2a80 = data_00499b34.rambuffer;
        data_004a28f8 = data_00499b34.romaddr;
    }
    data_004a2e3a = data_00499b34.generatesrec;
    if (data_004a2e49)
        data_004a2e3a = 0;
    data_004897a0 = data_00499b34.sreclength - 2;
    data_004897a4 = data_00499b34.sreceol + 1;
    data_004a2e47 = data_00499b34.listunused;
    data_004a2e43 = data_00499ba8.x35;
    data_004a28d8 = data_00499ba8.stacksize * 0x400;
    data_004a28f0 = 0x3dfff0;
    if (data_00499b34.hasstackaddr)
        data_004a28f0 = data_00499b34.stackaddr;
    data_004a28f0 = (data_004a28f0 + 15) & ~15;
    data_004a2870 = data_00499ba8.heapsize * 0x400;
    data_004a284c = data_004a28f0 - data_00499ba8.stacksize * 0x400 - data_00499ba8.heapsize * 0x400;
    if (data_00499b34.hasheapaddr)
        data_004a284c = data_00499b34.heapaddr;
    data_004a284c = (data_004a284c + 15) & ~15;
    fn_0042f0d0(linker_cu.outputDirectory.vRefNum, linker_cu.outputDirectory.parID, PROJECT_NAME(data_00499ba8),
                &data_0049bed0);
    fn_side_file(name, (unsigned char *)data_004998f8.srecfilename, data_00482f30, &data_0049bf16);
    fn_side_file(name, (unsigned char *)data_004998f8.mapfilename, data_00482f38, &data_0049bf5c);
    data_0049c100 = 'CWIE';
    data_0049c104 = 'CWIE';
    data_0049c10c = 'CWIE';
    data_0049c108 = 'SREC';
    data_0049c115 = data_00499b34.fullpaths;
    data_0049c116 = data_00499b34.generatemap;
    data_00587958 = data_00499b34.unk05;
    strcpy(data_0049c161, data_00499b34.mainname);
    data_0049c2f8 = 'CWIE';
    if (data_00499b08.backEnd.version > 11 && data_00499b08.processorName[0])
        data_00499b08.backEnd.processor = fn_0041dcc0(data_00499b08.processorName);
    fn_processor();
    data_0049c41a = 0;
    data_0049c41b = 0;
    if (data_00499b08.backEnd.fpmode) {
        data_0049c41a = 1;
        if (data_00499b08.backEnd.fpmode == 1)
            data_0049c41b = 1;
    }
    data_0049c41c = data_00499b08.backEnd.fp_contract;
    data_004a2e40 = 1;
    return ELF_linker_Link();
}

/* A file's data freed (its handles, or the IDE's data given back). */
void fn_0041fc60(ELFile *file)
{
    ELMember *member = file->member;

    if (member == NULL || (file->flags & EL_FILE_DUPLICATE))
        return;
    if (!(file->flags & EL_FILE_ARCHIVE) && !(file->flags2 & EL_FILE2_OBJECT)) {
        if (file->dataId) {
            ELF_linker_CallCallback(active_plugin_cu->context, file->index, file->dataId);
            file->dataId = 0;
        }
    } else {
        fn_free_members(member);
    }
    file->member = NULL;
}

static ELMember *fn_0041ff80(ELFile *file, short *error, CWFileSpec *spec);

/* A project file's data for the link. */
short fn_0041fcf0(ELFile *file)
{
    LinkerFileInfo info;
    ELMember *member;
    char **data;
    short error;
    int i;

    if (file->flags & EL_FILE_DUPLICATE) {
        error = fn_0041fcf0(file->original);
        file->member = file->original->member;
        return error;
    }
    member = NULL;
    if (!(file->flags & EL_FILE_ARCHIVE)) {
        CWPluginsPrivate_InvokeExportedRecordCallback(active_plugin_cu->context, file->index, 1,
                                                      (struct ExportedRecord *)&info);
        error = fn_0042f570(active_plugin_cu->context, file->index, &file->dataId);
        if (error == 0)
            fn_0041a460(active_plugin_cu->context, file->dataId, &data);
        if (!(file->flags & EL_FILE_BINARY) && !(file->flags & EL_FILE_DROPPED)) {
            if (error == 0 && COS_GetHandleSize(data) == 8) {
                ELF_linker_CallCallback(active_plugin_cu->context, file->index, file->dataId);
                member = fn_0041ff80(file, &error, &info.file);
            } else {
                member = (ELMember *)galloc(sizeof(ELMember));
                memset(member, 0, sizeof(ELMember));
                member->data = data;
                member->kind = 5;
                /* (the IDE's data is the object: its names end .o) */
                for (i = strlen(file->name) - 1; i != 0; i--) {
                    if (file->name[i] == '.') {
                        file->name[i + 1] = 'o';
                        file->name[i + 2] = 0;
                        break;
                    }
                }
                for (i = strlen(file->objectName) - 1; i != 0; i--) {
                    if (file->objectName[i] == '.') {
                        file->objectName[i + 1] = 'o';
                        file->objectName[i + 2] = 0;
                        break;
                    }
                }
            }
        } else if (file->flags & EL_FILE_BINARY) {
            data = fn_0041e530(&info.file, &error, 0);
            if (data == NULL || error) {
                CompilerTools_ReportLimitedDiagnostic(3, info.file.name);
                CompilerTools_FormatMessageAndLongjmp(0xd, error);
            }
            member = (ELMember *)galloc(sizeof(ELMember));
            memset(member, 0, sizeof(ELMember));
            member->data = data;
            member->kind = 7;
        }
    } else {
        CWPluginsPrivate_InvokeExportedRecordCallback(active_plugin_cu->context, file->index, 1,
                                                      (struct ExportedRecord *)&info);
        member = fn_0041ff80(file, &error, &info.file);
    }
    file->member = member;
    return error;
}

/* A project file read from the disk: an object or an archive (the output file itself is dropped). */
static ELMember *fn_0041ff80(ELFile *file, short *error, CWFileSpec *spec)
{
    ELMember *members;

    members = fn_0041e070(spec, error);
    if (*error) {
        CompilerTools_ReportLimitedDiagnostic(0x1e, spec->name);
        CompilerTools_FormatMessageAndLongjmp(0xd, 2);
    }
    if (members == NULL) {
        if (fn_0042f280(spec, &data_0049bed0)) {
            file->flags = (file->flags & ~EL_FILE_DROPPED) | EL_FILE_DROPPED;
            return NULL;
        }
        CompilerTools_ReportLimitedDiagnostic(0x1e, spec->name);
        CompilerTools_FormatMessageAndLongjmp(0xd, 2);
    }
    if (members->kind == 4) {
        file->flags2 = (file->flags2 & ~EL_FILE2_OBJECT) | EL_FILE2_OBJECT;
        file->flags &= ~EL_FILE_WHOLE;
    } else if (members->kind == 1) {
        file->flags = (file->flags & ~EL_FILE_ARCHIVE) | EL_FILE_ARCHIVE;
        file->flags &= ~EL_FILE_WHOLE;
    } else {
        CompilerTools_ReportLimitedDiagnostic(0x1e, spec->name);
        CompilerTools_FormatMessageAndLongjmp(0xd, 2);
    }
    return members;
}

/* A project file's names (the map's, the patterns') and how it is linked. */
void fn_00420070(long index, ELFile *file)
{
    LinkerFileInfo info;
    char text[1024];
    char path[1028];

    file->index = index;
    CWPluginsPrivate_InvokeExportedRecordCallback(active_plugin_cu->context, index, 1, (struct ExportedRecord *)&info);
    memcpy(text, info.file.name + 1, info.file.name[0]);
    text[info.file.name[0]] = 0;
    file->name = galloc(info.file.name[0] + 2);
    strcpy(file->name, text);
    if (!data_00499b34.fullpaths) {
        file->objectName = file->name;
    } else {
        COS_FileGetPathName(path, (CPrepFileInfo *)&info, NULL);
        file->objectName = galloc(strlen(path) + 1);
        strcpy(file->objectName, path);
    }
    if (info.linkWhole)
        file->flags = (file->flags & ~EL_FILE_WHOLE) | EL_FILE_WHOLE;
    else if (info.binary1 || info.binary2)
        file->flags = (file->flags & ~EL_FILE_BINARY) | EL_FILE_BINARY;
    else
        file->flags = (file->flags & ~EL_FILE_DROPPED) | EL_FILE_DROPPED;
}

/* A file's kind by its first bytes: 4 an ELF file, 1 an archive, 0 another. */
static unsigned char fn_004201a0(CWFileSpec *file)
{
    Boolean read;
    short handle;
    SInt32 count;
    char bytes[8];

    count = 8;
    read = 0;
    if (Files_OpenFile(file, 1, &handle) == 0) {
        if (Files_Read(handle, &count, bytes) == 0 && count == 8)
            read = 1;
        Files_Close(handle);
    }
    if (read) {
        if (ELF_linker_IsELF(bytes))
            return 4;
        if (strncmp(bytes, "!<arch>\n", 8) == 0)
            return 1;
    }
    return 0;
}

long fn_00420260(void)
{
    return active_plugin_cu->fileCount;
}

/* The messages the linker's code reports. */
void fn_00420270(char *message, void *argument)
{
    fn_0041b8d0(active_plugin_cu->context, message, argument);
}

void fn_00420290(char *message, void *argument)
{
    CWPluginsPrivate_InvokeMessageCallback(active_plugin_cu->context, NULL, message, argument, 1, 0);
}

void fn_004202b0(char *message, void *argument)
{
    CWPluginsPrivate_InvokeMessageCallback(active_plugin_cu->context, NULL, message, argument, 2, 0);
}

void fn_004202d0(void *argument, short request)
{
    CWPluginsPrivate_InvokeMessageCallback(active_plugin_cu->context, NULL, argument, NULL, 2, request);
}

void fn_004202f0(char *value)
{
    CWPluginsPrivate_CallCallback9(active_plugin_cu->context, value, NULL, NULL, 0);
}

/* The IDE asked to stop: checked every 15 ticks, the link left by its jump buffer. */
void fn_00420310(void)
{
    if (CompilerTools_GetTicks() > data_004998e8) {
        if (fn_0041b910(active_plugin_cu->context) == 1)
            longjmp(file_input_jmpbuf, 1);
        data_004998e8 = CompilerTools_GetTicks() + 15;
    }
}

Boolean fn_00420350(void)
{
    return fn_0041b910(active_plugin_cu->context) == 1;
}
