/* -dis: the dump of an object, a linked file or each object of an archive, into a text the driver keeps or writes
   to the output file (PrepDumpELF_Dump); DumpELF.c dumps each ELF image, printing through PrepDumpELF_Print. Before
   it, the linker's file helpers: an output file made empty with its type and creator, and Pascal strings.

   The assertions name this file "PrepDumpELF.c". */
#include "compiler/common.h"
#include "compiler/CompilerTools.h"
#include "driver/COSToolsCLT.h"
#include "driver/Files.h"
#include "driver/Memory.h"
#include "driver/libimp-eabi-ppc.h"

extern void CompilerTools_FormatMessageAndLongjmp(int message, int status, ...);
extern unsigned char CompilerTools_ReportDiagnostic(SInt32 code, ...);
extern int sprintf(char *s, const char *format, ...);
extern int strcmp(const char *a, const char *b);
extern void CompilerTools_ReportLimitedDiagnostic(int message, ...);
extern void fn_00420310(void);
extern void CompilerTools_DispatchMessageBufferByMode(int status);
extern void fn_0041e000(void);
extern short __stdcall fn_004085a0(int volume, int directory, unsigned char *name, char mode, short *file);
extern char *Archive_MemberName(char **longNames, char *name);
extern void Archive_EndLongNames(char **longNames, long size);

typedef void (*PrepDumpAsserter)(const char *file, int line);
typedef void (*PrepDumpPrinter)(char *text, long length, long size);
/* DumpELF.c's: whether an image is an ELF file, and its dump */
extern Boolean ELF_linker_IsELF(char *image);
extern void DumpELF_Dump(char *image, PrepDumpAsserter assertion, PrepDumpPrinter printer, long options, char *name);
/* the hook the memory allocator calls when it runs out (ELF_linker.c's) */
extern void *data_004a2860;
extern unsigned char copts[];

#pragma options align = mac68k
/* A file to dump: an object, an archive (kind 1, its members after it) or a linked file. */
typedef struct PrepDumpFile {
    struct PrepDumpFile *next;
    unsigned char kind;            /* 1 an archive, 2 an object of it, 3 its long names, 4 and 5 a file */
    unsigned char x5;
    char **data;
    char *name;                    /* an archive member's name, as its header has it */
} PrepDumpFile;
#pragma options align = reset

/* (a flag the compiler's shared code reads, 1 in the linker; its .data is this file's) */
Boolean data_0054c3d0 = 1;

static CWFileSpec *data_0049a6f8;  /* the output file, when the text is written to it */
static GList data_0049a6fc;        /* the text */
static char *data_0049a70c;        /* the name of the file being dumped */
static long data_0049a710;
static char data_0049a714;
static long data_0049a716;
static PrepDumpAsserter data_0049a71a;
static short data_0049a71e;        /* the output file, open */
static long data_0049a720;

void PrepDumpELF_Assert(const char *file, int line);
void PrepDumpELF_DumpArchive(PrepDumpFile *file);

/* Writes the text so far to the output file (made first), and starts it again: when memory runs out. */
/* 0x43b210 */
void PrepDumpELF_Flush(void) {
    if (!data_0049a71e) {
        if (COS_FileNew(data_0049a6f8, &data_0049a71e, 'CWIE', 'TEXT'))
            CompilerTools_FormatMessageAndLongjmp(0x45, -108);
        CompilerTools_ReportDiagnostic(0x44);
    }
    if (COS_FileWrite(data_0049a71e, *data_0049a6fc.data, data_0049a6fc.size))
        CompilerTools_FormatMessageAndLongjmp(0x46, -108);
    FreeGList(&data_0049a6fc);
    if (InitGList(&data_0049a6fc, 500000))
        CompilerTools_FormatMessageAndLongjmp(0x46, -108);
}

/* DumpELF.c's printer: adds a line to the text. */
/* 0x43b1d0 */
void PrepDumpELF_Print(char *text, long length, long size) {
    if (length > size) {
        data_0049a71a("PrepDumpELF.c", 0x6d);
        length = size;
    }
    fn_00420310();
    AppendGListData(&data_0049a6fc, text, length);
}

/* Adds a line of at most `size` bytes to the text. */
#define PRINT(buffer, size, format, argument)                                                                          \
    {                                                                                                                  \
        long length_ = sprintf(buffer, format, argument);                                                              \
        if (length_ > (size)) {                                                                                        \
            data_0049a71a("PrepDumpELF.c", 0x6d);                                                                      \
            length_ = (size);                                                                                          \
        }                                                                                                              \
        fn_00420310();                                                                                                 \
        AppendGListData(&data_0049a6fc, buffer, length_);                                                              \
    }

/* Dumps a file: the text is the output's (*output) or, when it does not fit in memory, written to `spec`. */
/* 0x43aec0 */
short PrepDumpELF_Dump(PrepDumpFile *file, char ***output, char *name, CWFileSpec *spec) {
    char text[0x200];
    char **data;
    short result;

    data_0049a71e = 0;
    data_0049a6f8 = spec;
    data_0049a71a = PrepDumpELF_Assert;
    data_0049a710 = 5;
    data_0049a716 = 0x34;
    data_0049a714 = 1;
    if (InitGList(&data_0049a6fc, 500000))
        return -108;
    data_0049a720 = 0x7fffffff;
    data_004a2860 = (void *)PrepDumpELF_Flush;
    data_0049a70c = name;
    PRINT(text, 0x200, "Analyzing: %s\r\n", name);
    result = _Setjmp(file_input_jmpbuf);
    if (result == 0) {
        fn_0041e000();
        if (file->kind == 1) {
            PrepDumpELF_DumpArchive(file);
        } else if ((unsigned char)(file->kind - 4) <= 1) {
            data = file->data;
            COS_LockHandleHi(data);
            DumpELF_Dump(*file->data, PrepDumpELF_Assert, PrepDumpELF_Print, data_0049a710, name);
            COS_UnlockHandle(file->data);
        } else {
            CompilerTools_ReportLimitedDiagnostic(0x39, name);
            CompilerTools_FormatMessageAndLongjmp(0xd, 2);
        }
        if ((*data_0049a6fc.data)[data_0049a6fc.size - 1] != "\r\n"[1])
            AppendGListData(&data_0049a6fc, "\r\n", 2);
        ShrinkGList(&data_0049a6fc);
        if (!data_0049a71e) {
            *output = (char **)COS_NewHandle(data_0049a6fc.size);
            if (!*output)
                PrepDumpELF_Flush(); /* (no memory for the text: it goes to the file) */
        }
        if (data_0049a71e) {
            if (COS_FileWrite(data_0049a71e, *data_0049a6fc.data, data_0049a6fc.size))
                CompilerTools_FormatMessageAndLongjmp(0x46, -108);
        } else {
            COS_FreeHandle(*output);
            *output = data_0049a6fc.data;
            data_0049a6fc.data = NULL;
        }
        FreeGList(&data_0049a6fc);
    } else {
        FreeGList(&data_0049a6fc);
        CompilerTools_DispatchMessageBufferByMode(result);
    }
    if (data_0049a71e) {
        COS_FileClose(data_0049a71e);
        data_0049a71e = 0;
    }
    return result;
}

/* Dumps the objects of an archive (the "//" member has the long names). */
/* 0x43ad30 */
void PrepDumpELF_DumpArchive(PrepDumpFile *file) {
    char text[400];
    char **longNames;
    char **data;
    char *name;

    longNames = NULL;
    for (; file; file = file->next) {
        data = file->data;
        name = Archive_MemberName(longNames, file->name);
        if (!Memory_GetHandleSize((struct StorageHandle *)data)) {
            PRINT(text, 400, "\r\nEmpty ar member: %s\r\n", name);
            continue;
        }
        if (!strcmp(name, "//")) {
            longNames = data;
            COS_LockHandleHi(data);
            Archive_EndLongNames(data, Memory_GetHandleSize((struct StorageHandle *)data));
            file->kind = 3;
        }
        if (ELF_linker_IsELF(*data)) {
            file->kind = 2;
            PRINT(text, 400, "\r\nAnalyzing ar member: %s\r\n", name);
            data_0049a70c = name;
            COS_LockHandleHi(file->data);
            DumpELF_Dump(*file->data, PrepDumpELF_Assert, PrepDumpELF_Print, data_0049a710, name);
            COS_UnlockHandle(file->data);
        }
    }
    if (longNames)
        COS_UnlockHandle(longNames);
}

/* DumpELF.c's assertions. */
/* 0x43ad00 */
void PrepDumpELF_Assert(const char *file, int line) {
    CompilerTools_ReportLimitedDiagnostic(data_0049a716, file, line);
    CompilerTools_FormatMessageAndLongjmp(0xd, 2);
}

/* Stops at a file DumpELF.c cannot dump: of the other byte order (3), not for the PowerPC (1), of another ELF
   version (2). */
/* 0x43ac90 */
void PrepDumpELF_BadFile(int problem) {
    if (problem == 3) {
        CompilerTools_ReportLimitedDiagnostic(0x38, data_0049a70c, copts[0] ? "big" : "little");
    } else if (problem == 1) {
        CompilerTools_ReportLimitedDiagnostic(0x36, "PowerPC");
    } else if (problem == 2) {
        CompilerTools_ReportLimitedDiagnostic(0x37);
    } else {
        return;
    }
    CompilerTools_FormatMessageAndLongjmp(0xd, 2);
}

/* Copies a Pascal string. */
/* 0x43ac60 */
void PrepDumpELF_PStrCopy(unsigned char *dst, const unsigned char *src) {
    short count;

    count = src[0];
    do {
        *dst++ = *src++;
    } while (count-- > 0);
}

/* Appends a Pascal string to another, up to 255 characters. */
/* 0x43ac10 */
void PrepDumpELF_PStrCat(unsigned char *dst, const unsigned char *src) {
    short length;
    short count;
    unsigned char *at;

    count = *src++;
    length = dst[0];
    if (count + length > 255)
        count = 255 - length;
    dst[0] += count;
    at = dst + length + 1;
    while (count-- > 0)
        *at++ = *src++;
}

/* Makes an output file empty, both its forks, with its type and creator; a file that does not exist is created. */
/* 0x43aab0 */
short PrepDumpELF_CreateFile(short volume, long directory, unsigned char *name, long creator, long type) {
    RecordQuery query;
    short file;
    short error;

    error = Files_OpenFileFromPath(volume, directory, name, 3, &file);
    if (error) {
        if (error == -43) {
            Files_DeleteFileFromPath(volume, directory, name);
            return Files_CallWithFileSpecFromPath(volume, directory, name, creator, type);
        }
        return error;
    }
    error = Files_SetSize(file, 0);
    Files_Close(file);
    if (error)
        return error;
    if (!fn_004085a0(volume, directory, name, 3, &file)) {
        error = Files_SetSize(file, 0);
        Files_Close(file);
        if (error)
            return error;
    }
    query.kind = volume;
    query.value = directory;
    query.name = name;
    query.flags = 0;
    *(UInt8 *)&query.options = 0;
    error = Files_UpdateRecordQuery(&query);
    if (error)
        return error;
    if (query.identifier.words[1] != creator || query.identifier.words[0] != type) {
        query.kind = volume;
        query.value = directory;
        query.name = name;
        query.flags = 0;
        *(UInt8 *)&query.options = 0;
        query.identifier.words[1] = creator;
        query.identifier.words[0] = type;
        error = fn_00414710(&query);
        if (error)
            return error;
    }
    return 0;
}
