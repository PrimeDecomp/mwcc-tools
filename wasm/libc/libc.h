/* The C library the WebAssembly build links in place of MSL's (gc2's src/msl, which compiles the library's own
 * sources from the downloaded gc2/lib): what the compiler calls, behaving as Pro 5.3's Win32 MSL does wherever the
 * compiler can see the difference (its allocator's addresses and reuse, its number formatting and scanning, its
 * buffering and text translation, its errno slot). Parts are adapted from the MSL of the Metroid Prime 2: Echoes
 * decompilation (github.com/PrimeDecomp/echoes, src/Runtime, CC0). The declarations the compiler's units see are gc2's include/libc; these are the library's
 * own. */
#ifndef WASM_LIBC_H
#define WASM_LIBC_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* (tolower out of line: string.c defines it) */
#define _MSL_USE_INLINE 0
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include "msl/ansifp_x86.h"
#include "msl/crtl.h"
#include "windows.h"

/* MSL's thread data, as the library has it (Pro 5.3's ThreadLocalData.h with the thread handle 2.x's adds): the
 * library's errno is the first word. The compiler's own units read the second (gc2's include/libc/errno.h), which
 * nothing here writes after it is set to 1. 596 bytes, the size MSL allocates. */
typedef struct {
    int error;
    unsigned long random_next;
    unsigned char* strtok_n;
    unsigned char* strtok_s;
    void* thread_handle;
    struct tm gmtime_tm;
    char asctime_result[26];
    char tmpnam_temp_name[512];
} ThreadData;

ThreadData* __thread_data(void);
#define LIBC_ERRNO (__thread_data()->error)
#define LIBC_ERANGE 34
#define LIBC_ESIGPARM 36

/* alloc.c: the allocator's operating-system blocks */
void* __sys_alloc(size_t size);
void __sys_free(void* ptr);

/* scan.c: MSL's reading procedures (`ReadProc`) and its number scanners */
enum { READ_GET, READ_UNGET, READ_TEST };
typedef int (*ReadProc)(void* argument, int c, int action);
typedef struct {
    char* next;
    int null_seen;
} StringInput;
int __StringRead(void* input, int c, int action);
unsigned long __strtoul(int base, int max_width, ReadProc read, void* argument, int* chars_scanned, int* negative,
    int* overflow);
double __strtold(int max_width, ReadProc read, void* argument, int* chars_scanned, int* overflow);

/* x87.c */
double __dec2num(const decimal* d);
void __num2dec(const decform* f, double x, decimal* d);

/* stdio.c */
void __stdio_atexit(void);
extern void (*__stdio_exit)(void);

/* extras.c; the handle table's next entry to try (stdio.c) */
extern int _HandPtr;
int GetHandle(void);
int _strnicmp(const char* s1, const char* s2, int n);
extern int _doserrno;
void __fill_mem(void* dst, int val, unsigned long n);
void __copy_longs_aligned(void* dst, const void* src, unsigned long n);
void __copy_longs_unaligned(void* dst, const void* src, unsigned long n);
void __copy_longs_rev_aligned(void* dst, const void* src, unsigned long n);
void __copy_longs_rev_unaligned(void* dst, const void* src, unsigned long n);

/* string.c */
void* __memrchr(const void* src, int val, size_t n);

/* runtime.c */
int atexit(void (*func)(void));
extern char** _Environ;

#endif
