/* A fresh process per scenario gives both writers their original unopened state. */
#include <stdio.h>
#include <stdlib.h>
#pragma dont_inline on
#define OracleTrace_Enabled OracleAbi_CTraceEnabled
#define OracleTrace_Printf OracleAbi_CTracePrintf
#include "src/oracle/OracleTrace.c"
#undef OracleTrace_Enabled
#undef OracleTrace_Printf
#pragma dont_inline reset
extern int OracleTrace_Enabled(void);
extern void OracleTrace_Printf(const char *, ...);
typedef void (*Printer)(const char *, ...);
extern void OracleAbi_RustTraceEmit(Printer, const char *, int);
__declspec(dllimport) void __stdcall ExitProcess(unsigned int);
__declspec(dllimport) unsigned long __stdcall GetEnvironmentVariableA(const char *, char *, unsigned long);

FILE __files[3];
static FILE opened_files[2];
static unsigned char expected[16384];
static unsigned long position, expected_size, failures;
static int phase, scenario, queries, opens, formats, flushes, reentered;
static const char *path;
static Printer writer;
static int (*enabled)(void);
static char long_message[1200];

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void byte(unsigned char value)
{
    if (position >= sizeof(expected)) { failures |= 1; return; }
    if (phase == 0) expected[position] = value;
    else if (position >= expected_size || expected[position] != value) failures |= 2;
    ++position;
}
static void word(unsigned long value)
{
    int i;
    for (i = 0; i < 4; ++i) byte(((unsigned char *)&value)[i]);
}
static void text(const char *value)
{
    do { byte(*value); } while (*value++);
}

char *getenv(const char *name)
{
    ++queries; text("getenv"); text(name);
    if (!same(name, "MWCC_ORACLE_TRACE_FILE") || enabled() != 1) failures |= 4;
    if (scenario == 22 && !reentered) {
        reentered = 1;
        writer("nested %d", -12345);
    }
    return (char *)path;
}
FILE *fopen(const char *name, const char *mode)
{
    ++opens; text("fopen"); text(name); text(mode);
    if (!same(mode, "a")) failures |= 8;
    if (scenario == 12 && !reentered) {
        reentered = 1;
        writer("nested %d", -12345);
    }
    if (scenario == 1 || scenario == 4 || scenario == 8) return 0;
    if (scenario == 23) return stderr;
    return &opened_files[opens > 1];
}
int vfprintf(FILE *file, const char *format, char *arguments)
{
    int size = 0, i;
    ++formats; text("vfprintf"); word((unsigned long)file); text(format);
    if (same(format, "c %d %x %g %llx %s %p") || same(format, "rust %d %x %g %llx %s %p")) size = 32;
    else if (same(format, "promoted %d %d %g")) size = 16;
    else if (same(format, "nested %d")) size = 4;
    else if (same(format, "many")) size = 80;
    else if (*format && format != long_message) failures |= 16;
    word(size);
    for (i = 0; i < size; ++i) byte(arguments[i]);
    if (scenario == 13 && !reentered) {
        reentered = 1; writer("nested %d", -12345);
    }
    return scenario == 9 ? -1 : 1234567;
}
int fflush(FILE *file)
{
    ++flushes; text("fflush"); word((unsigned long)file);
    if (scenario == 11) path = "changed during flush";
    if (scenario == 14 && !reentered) {
        reentered = 1; writer("nested %d", -12345);
    }
    return scenario == 10 ? -1 : 0;
}

static void run(int rust)
{
    int i;
    queries = opens = formats = flushes = reentered = 0;
    writer = rust ? OracleTrace_Printf : OracleAbi_CTracePrintf;
    enabled = rust ? OracleTrace_Enabled : OracleAbi_CTraceEnabled;
    path = scenario == 0 || scenario == 6 ? 0 : scenario == 1 ? "" : scenario == 2 ? "0" :
        scenario == 5 ? "file with spaces and \xe9" : "file";
    if (enabled() != 1 || queries || opens || formats || flushes) failures |= 32;
    if (scenario == 18) {
        for (i = 0; i < 1199; ++i) long_message[i] = (char)(128 + i % 127);
        long_message[1199] = 0;
        writer(long_message);
    } else if (scenario == 19) {
        writer("many", -2147483000L, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 2147483000L);
    } else if (scenario == 20) writer("");
    else writer("c %d %x %g %llx %s %p", -2147483000L, 0xfedcba98UL, -1.25,
        0x8123456789abcdefLL, "data", (void *)0);
    if (scenario == 6 || scenario == 8) path = "now available";
    if (scenario == 7) path = 0;
    OracleAbi_RustTraceEmit(writer, "data", scenario == 15 || scenario == 16);
    if (scenario == 16 || scenario == 17) OracleAbi_RustTraceEmit(writer, "data", 1);
    if (enabled() != 1 || queries != (scenario == 22 ? 2 : 1) || opens != (scenario == 0 || scenario == 6 ? 0 : scenario == 22 ? 2 : 1) ||
        formats != flushes || formats < 3) failures |= 64;
    if (scenario >= 12 && scenario <= 14 && !reentered) failures |= 64;
}

void main(void)
{
    char selection[16];
    unsigned long length, i;
    length = GetEnvironmentVariableA("ORACLE_ABI_TRACE_CASE", selection, sizeof(selection));
    if (!length || length >= sizeof(selection)) ExitProcess(127);
    for (i = 0; i < length; ++i) {
        if (selection[i] < '0' || selection[i] > '9') ExitProcess(126);
        scenario = scenario * 10 + selection[i] - '0';
    }
    if (scenario > 23) ExitProcess(125);
    phase = 0; position = 0; run(0); expected_size = position;
    phase = 1; position = 0; run(1);
    if (position != expected_size) failures |= 128;
    ExitProcess(failures);
}
