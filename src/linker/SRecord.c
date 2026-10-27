/* The linker's S-record output (-srec, -sreclength, -sreceol): the records are built one at a time in a line buffer
   and written to the output file with each record's count and checksum.

   No assertion names this file: SRecord.c is this repository's name for it. */
#include "compiler/CompilerTools.h"
#include "driver/Files.h"

extern int sprintf(char *s, const char *format, ...);
extern short PrepDumpELF_CreateFile(short vRefNum, long parID, unsigned char *name, long creator, long type);
extern void CompilerTools_ReportLimitedDiagnostic(int message, ...);
extern void ELF_gen_AddToBuffer(GList *buffer, short file, char *data, long length, int message);

/* The output file and the buffers its records are gathered in, when the output is not written as it is made. */
extern char data_0049c120;
extern FSSpec data_0049bf16;
extern long data_0049c10c;
extern short data_004a2e34;
extern GList data_004a21d0;
extern GList data_004a21f0;
extern GList *data_004a28d0;

/* The options: the bytes of data in a record (-sreclength, less the address and checksum bytes) and the end of a line
   (-sreceol: 1 is CR, 2 CR LF, 3 LF). */
short data_004897a0 = 254;
char data_004897a4 = 1;

/* The record being made: its bytes so far (the address and data), its length in the line, its address, its type, its
   checksum and the line itself, whether a record is open, and the end of its line. */
static long count;
static long length;
static unsigned long address;
static unsigned char type;
static unsigned char checksum;
static char line[0x20c];
static Boolean open;
static char *eol;
#if LINKER >= 27
/* 2.7 fills its records: an open one (ended, or filled by padding) takes a next one at its next address (in the
   same buffer) until SRecord_Flush. */
static Boolean ended;
static GList *buffer;
#endif

/* 0x00442e00 */
short SRecord_InitBuffer(GList *buffer, long size) {
    buffer->data = 0;
    buffer->size = 0;
    if (data_0049c120 != 2) {
        if (InitGList(buffer, size))
            return -1;
    }
    return 0;
}

/* 0x00442e40 */
void SRecord_CloseFile(void) {
    if (data_004a2e34) {
        Files_Close(data_004a2e34);
        data_004a2e34 = 0;
    }
    FreeGList(&data_004a21d0);
    FreeGList(&data_004a21f0);
}

/* 0x00442e80 */
void SRecord_Nothing(void) {
}

/* 0x00442e90 */
void SRecord_OpenFile(void) {
    short error;

    error = PrepDumpELF_CreateFile(data_0049bf16.vRefNum, data_0049bf16.parID, data_0049bf16.name, data_0049c10c, 'TEXT');
    if (error) {
        CompilerTools_ReportLimitedDiagnostic(1, data_0049bf16.name);
        CompilerTools_FormatMessageAndLongjmp(0xd, error);
    }
    error = Files_OpenFile(&data_0049bf16, 3, &data_004a2e34);
    if (error) {
        CompilerTools_ReportLimitedDiagnostic(1, data_0049bf16.name);
        CompilerTools_FormatMessageAndLongjmp(0xd, error);
    }
    if (SRecord_InitBuffer(&data_004a21d0, 0x20))
        CompilerTools_FormatMessageAndLongjmp(0xd, -108);
    if (SRecord_InitBuffer(&data_004a21f0, 0x100))
        CompilerTools_FormatMessageAndLongjmp(0xd, -108);
    data_004a28d0 = &data_004a21d0;
}

/* Ends the open record: its count (the bytes after the count, the checksum's among them) goes in the place the record
   left for it, and the line ends with the checksum, the one's complement of the bytes' sum. */
static inline void EndRecord(void) {
    char saved;

    if (open) {
        saved = line[4];
        sprintf(line + 2, "%02X", count + 1);
        line[4] = saved;
        checksum += count + 1;
        checksum = ~checksum;
        length += sprintf(line + length, "%02X%s", checksum, eol);
#if LINKER >= 27
        ELF_gen_AddToBuffer(buffer, data_004a2e34, line, length, 1);
        ended = 0;
#else
        ELF_gen_AddToBuffer(data_004a28d0, data_004a2e34, line, length, 1);
#endif
        count = 0;
        open = 0;
    }
}

/* 0x00442f50 */
void SRecord_End(void) {
#if LINKER >= 27
    /* (a data record stays open) */
    if (open && type == 3)
        ended = 1;
    else
        EndRecord();
#else
    EndRecord();
#endif
}

#if LINKER >= 27
/* Ends the record left open (at the end of a section's records). */
void SRecord_Flush(void) {
    EndRecord();
}
#endif

/* Starts a record of a type at an address: S0, S1 and S9 have 2 address bytes, S2 and S8 3, S3 and S7 4
   (the linker writes S0, S3 and S7 records). */
/* 0x004432a0 */
void SRecord_Start(unsigned long at, unsigned char kind) {
    unsigned char *bytes = (unsigned char *)&at;

#if LINKER >= 27
    if (open) {
        if (kind == type && at == address && buffer == data_004a28d0 && kind == 3) {
            ended = 0;
            return;
        }
        EndRecord();
    }
    buffer = data_004a28d0;
#endif
    length = sprintf(line, "S%1d", kind);
    type = kind;
    address = at;
    length += sprintf(line + length, "00");
    switch (kind) {
    case 0:
    case 1:
    case 9:
        length += sprintf(line + length, "%04X", at);
        checksum = bytes[0] + bytes[1];
        count = 2;
        break;
    case 2:
    case 8:
        count = 3;
        length += sprintf(line + length, "%06X", at);
        checksum = bytes[0] + bytes[1] + bytes[2];
        break;
    case 3:
    case 7:
        count = 4;
        length += sprintf(line + length, "%08X", at);
        checksum = bytes[0] + bytes[1] + bytes[2] + bytes[3];
        break;
    }
    open = 1;
}

/* Adds a byte to the record, starting one at the next address after a full record. */
static inline void AddByte(unsigned char byte) {
    if (!open)
        SRecord_Start(address, type);
    length += sprintf(line + length, "%02X", byte);
    checksum += byte;
    count++;
    address++;
    if (count == data_004897a0)
        EndRecord();
}

/* Adds the four bytes of a word, in the order they are in memory. */
/* 0x00443000 */
void SRecord_AddWord(long word) {
    unsigned char *p = (unsigned char *)&word;
    int i;

    for (i = 4; i > 0; i--)
        AddByte(*p++);
}

/* 0x00443150 */
void SRecord_AddBytes(unsigned char *data, long size) {
    for (; size > 0; size--)
        AddByte(*data++);
}

/* Writes the S0 header record, after choosing the lines' end. */
/* 0x004433f0 */
void SRecord_Header(void) {
    switch (data_004897a4) {
    case 1:
        eol = "\r";
        break;
    case 2:
        eol = "\r\n";
        break;
    case 3:
        eol = "\n";
        break;
    }
    SRecord_Start(0, 0);
    EndRecord();
}
