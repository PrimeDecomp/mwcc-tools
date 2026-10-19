/* What the compiler uses of MSL Win32's extras (_strnicmp, and the handle table's next free entry) and of its block
 * copies and fills. */
#include "libc.h"

int _doserrno = 0;

int _strnicmp(const char* s1, const char* s2, int n)
{
    int i;
    char c1, c2;
    for (i = 0; i < n; i++) {
        c1 = tolower(*s1++);
        c2 = tolower(*s2++);
        if (c1 < c2) {
            return -1;
        }
        if (c1 > c2) {
            return 1;
        }
        if (!c1) {
            return 0;
        }
    }
    return 0;
}

int GetHandle()
{
    int i;
    for (i = 0; i < 256; i++) {
        if (_HandPtr == NUM_HANDLES) {
            _HandPtr = 3;
        }
        if (_HandleTable[_HandPtr] == 0) {
            return _HandPtr++;
        }
        _HandPtr++;
    }
    return -1;
}

/* MSL's block copies and fills (mem_funcs.c's, which clang cannot take: it assigns to casts). Each copies N bytes
 * from SRC to DST, the rev ones from the end down (for a move to a higher, overlapping address); byte loops, which
 * cannot become calls back into memmove and memset (-fno-builtin) that call these. */
void __copy_longs_aligned(void* dst, const void* src, unsigned long n)
{
    unsigned char* d = dst;
    const unsigned char* s = src;
    while (n--) {
        *d++ = *s++;
    }
}

void __copy_longs_unaligned(void* dst, const void* src, unsigned long n)
{
    __copy_longs_aligned(dst, src, n);
}

void __copy_longs_rev_aligned(void* dst, const void* src, unsigned long n)
{
    unsigned char* d = (unsigned char*)dst + n;
    const unsigned char* s = (const unsigned char*)src + n;
    while (n--) {
        *--d = *--s;
    }
}

void __copy_longs_rev_unaligned(void* dst, const void* src, unsigned long n)
{
    __copy_longs_rev_aligned(dst, src, n);
}

void __fill_mem(void* dst, int val, unsigned long n)
{
    unsigned char* d = dst;
    while (n--) {
        *d++ = (unsigned char)val;
    }
}
