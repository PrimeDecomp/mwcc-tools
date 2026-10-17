/* The string, memory and character functions, the character tables and qsort, as MSL's: comparisons return the
 * difference of the first differing (unsigned) characters, memcpy copies forward, and qsort is MSL's heapsort (which
 * orders equal elements as the original does). qsort follows the Echoes decompilation's qsort.c. */
#include "libc.h"

size_t strlen(const char* str)
{
    size_t len = -1;

    do {
        len++;
    } while (*str++);
    return len;
}

char* strcpy(char* dst, const char* src)
{
    char* q = dst;

    while ((*q++ = *src++) != 0) {
    }
    return dst;
}

char* strncpy(char* dst, const char* src, size_t n)
{
    char* q = dst;

    n++;
    while (--n) {
        if (!(*q++ = *src++)) {
            while (--n) {
                *q++ = 0;
            }
            break;
        }
    }
    return dst;
}

char* strcat(char* dst, const char* src)
{
    char* q = dst;

    while (*q) {
        q++;
    }
    while ((*q++ = *src++) != 0) {
    }
    return dst;
}

char* strncat(char* dst, const char* src, size_t n)
{
    char* q = dst;

    while (*q) {
        q++;
    }
    n++;
    while (--n) {
        if (!(*q++ = *src++)) {
            q--;
            break;
        }
    }
    *q = 0;
    return dst;
}

int strcmp(const char* str1, const char* str2)
{
    const unsigned char* p1 = (const unsigned char*)str1;
    const unsigned char* p2 = (const unsigned char*)str2;
    unsigned char c1, c2;

    while ((c1 = *p1++) == (c2 = *p2++)) {
        if (!c1) {
            return 0;
        }
    }
    return c1 - c2;
}

int strncmp(const char* str1, const char* str2, size_t n)
{
    const unsigned char* p1 = (const unsigned char*)str1;
    const unsigned char* p2 = (const unsigned char*)str2;
    unsigned char c1, c2;

    n++;
    while (--n) {
        if ((c1 = *p1++) != (c2 = *p2++)) {
            return c1 - c2;
        } else if (!c1) {
            break;
        }
    }
    return 0;
}

char* strchr(const char* str, int chr)
{
    char c = chr;
    char ch;

    while ((ch = *str++) != 0) {
        if (ch == c) {
            return (char*)(str - 1);
        }
    }
    return c ? 0 : (char*)(str - 1);
}

char* strrchr(const char* str, int chr)
{
    const char* found = 0;
    char c = chr;
    char ch;

    while ((ch = *str++) != 0) {
        if (ch == c) {
            found = str - 1;
        }
    }
    if (found) {
        return (char*)found;
    }
    return c ? 0 : (char*)(str - 1);
}

char* strpbrk(const char* str, const char* set)
{
    unsigned char map[32] = {0};
    const unsigned char* p = (const unsigned char*)set;
    int c;

    while ((c = *p++) != 0) {
        map[c >> 3] |= 1 << (c & 7);
    }
    p = (const unsigned char*)str;
    while ((c = *p++) != 0) {
        if (map[c >> 3] & (1 << (c & 7))) {
            return (char*)(p - 1);
        }
    }
    return 0;
}

char* strstr(const char* str, const char* pat)
{
    const unsigned char* s = (const unsigned char*)str;
    const unsigned char* p = (const unsigned char*)pat;
    unsigned char first, c1, c2;

    if (pat == 0 || !(first = *p++)) {
        return (char*)str;
    }
    while ((c1 = *s++) != 0) {
        if (c1 == first) {
            const unsigned char* s2 = s;
            const unsigned char* p2 = p;
            while ((c1 = *s2++) == (c2 = *p2++) && c1) {
            }
            if (!c2) {
                return (char*)s - 1;
            }
        }
    }
    return 0;
}

/* MSL's messages for its own error numbers; the rest are unknown */
char* strerror(int errnum)
{
    static char message[32];

    switch (errnum) {
    case 0:
        strcpy(message, "No Error");
        break;
    case 33:
        strcpy(message, "Domain Error");
        break;
    case 34:
        strcpy(message, "Range Error");
        break;
    case 35:
        strcpy(message, "File Position Error");
        break;
    case 36:
        strcpy(message, "Signal Error");
        break;
    default:
        sprintf(message, "Unknown Error (%d)", errnum);
        break;
    }
    return message;
}

/* 32 bytes or more go through the word copies (extras.c), as MSL's do */
void* memcpy(void* dst, const void* src, size_t n)
{
    const char* p = src;
    char* q = dst;

    if (n >= 32) {
        if (((unsigned long)dst ^ (unsigned long)src) & 3) {
            __copy_longs_unaligned(dst, src, n);
        } else {
            __copy_longs_aligned(dst, src, n);
        }
        return dst;
    }
    for (n++; --n;) {
        *q++ = *p++;
    }
    return dst;
}

void* memmove(void* dst, const void* src, size_t n)
{
    const char* p;
    char* q;
    int rev = (unsigned long)src < (unsigned long)dst;

    if (n >= 32) {
        if (((unsigned long)dst ^ (unsigned long)src) & 3) {
            if (!rev) {
                __copy_longs_unaligned(dst, src, n);
            } else {
                __copy_longs_rev_unaligned(dst, src, n);
            }
        } else if (!rev) {
            __copy_longs_aligned(dst, src, n);
        } else {
            __copy_longs_rev_aligned(dst, src, n);
        }
        return dst;
    }
    if (!rev) {
        for (p = src, q = dst, n++; --n;) {
            *q++ = *p++;
        }
    } else {
        for (p = (const char*)src + n, q = (char*)dst + n, n++; --n;) {
            *--q = *--p;
        }
    }
    return dst;
}

void* memset(void* dst, int val, size_t n)
{
    __fill_mem(dst, val, n);
    return dst;
}

void* memchr(const void* src, int val, size_t n)
{
    const unsigned char* p = src;
    unsigned char v = val & 0xFF;

    for (n++; --n;) {
        if (*p++ == v) {
            return (void*)(p - 1);
        }
    }
    return 0;
}

void* __memrchr(const void* src, int val, size_t n)
{
    const unsigned char* p = (const unsigned char*)src + n;
    unsigned char v = val & 0xFF;

    for (n++; --n;) {
        if (*--p == v) {
            return (void*)p;
        }
    }
    return 0;
}

int memcmp(const void* src1, const void* src2, size_t n)
{
    const unsigned char* p1 = src1;
    const unsigned char* p2 = src2;

    for (n++; --n;) {
        if (*p1++ != *p2++) {
            return *--p1 < *--p2 ? -1 : 1;
        }
    }
    return 0;
}

/* The character classes of the first 128 characters (none above): 1 control, 2 a control that is space, 4 the space,
 * 8 punctuation, 0x10 a digit, 0x20 a hexadecimal digit, 0x40 lower case, 0x80 upper case. */
#define CC 0x01
#define CS 0x02
#define SP 0x04
#define PU 0x08
#define DI (0x10 | 0x20)
#define LX (0x40 | 0x20)
#define UX (0x80 | 0x20)
#define LO 0x40
#define UP 0x80

unsigned char __ctype_map[256] = {
    CC, CC, CC, CC, CC, CC, CC, CC, CC, CS, CS, CS, CS, CS, CC, CC,
    CC, CC, CC, CC, CC, CC, CC, CC, CC, CC, CC, CC, CC, CC, CC, CC,
    SP, PU, PU, PU, PU, PU, PU, PU, PU, PU, PU, PU, PU, PU, PU, PU,
    DI, DI, DI, DI, DI, DI, DI, DI, DI, DI, PU, PU, PU, PU, PU, PU,
    PU, UX, UX, UX, UX, UX, UX, UP, UP, UP, UP, UP, UP, UP, UP, UP,
    UP, UP, UP, UP, UP, UP, UP, UP, UP, UP, UP, PU, PU, PU, PU, PU,
    PU, LX, LX, LX, LX, LX, LX, LO, LO, LO, LO, LO, LO, LO, LO, LO,
    LO, LO, LO, LO, LO, LO, LO, LO, LO, LO, LO, PU, PU, PU, PU, CC,
};

/* the case maps: every character itself but the letters */
#define R4(b) (b), (b) + 1, (b) + 2, (b) + 3
#define R16(b) R4(b), R4((b) + 4), R4((b) + 8), R4((b) + 12)
#define R128(b) R16(b), R16((b) + 16), R16((b) + 32), R16((b) + 48), R16((b) + 64), R16((b) + 80), R16((b) + 96), \
    R16((b) + 112)
/* @, the 26 letters from L, and [ \ ] ^ _ */
#define LETTERS(L) 0x40, R16(L), R4((L) + 16), R4((L) + 20), (L) + 24, (L) + 25, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F

unsigned char __lower_map[256] = {
    R16(0x00), R16(0x10), R16(0x20), R16(0x30), LETTERS('a'), 0x60, R16('a'), R4('a' + 16), R4('a' + 20), 'y', 'z',
    0x7B, 0x7C, 0x7D, 0x7E, 0x7F, R128(0x80),
};

unsigned char __upper_map[256] = {
    R16(0x00), R16(0x10), R16(0x20), R16(0x30), LETTERS('A'), 0x60, R16('A'), R4('A' + 16), R4('A' + 20), 'Y', 'Z',
    0x7B, 0x7C, 0x7D, 0x7E, 0x7F, R128(0x80),
};

int isalpha(int c)
{
    return __ctype_map[(unsigned char)c] & (LO | UP);
}

int isprint(int c)
{
    return __ctype_map[(unsigned char)c] & (LO | UP | 0x10 | PU | SP);
}

int tolower(int c)
{
    return c == -1 ? -1 : __lower_map[(unsigned char)c];
}

int toupper(int c)
{
    return c == -1 ? -1 : __upper_map[(unsigned char)c];
}

int abs(int n)
{
    return n < 0 ? -n : n;
}

/* MSL's heapsort. The table is 1-based in its index arithmetic. */
#define table_ptr(i) (((char*)table_base) + (member_size * ((i) - 1)))

#define swap(dst, src, cnt) \
    do { \
        char* p; \
        char* q; \
        size_t n = cnt; \
        unsigned char tmp; \
        for (p = src, q = dst, n++; --n;) { \
            tmp = *q; \
            *q++ = *p; \
            *p++ = tmp; \
        } \
    } while (0)

void qsort(void* table_base, size_t num_members, size_t member_size, _compare_function compare_members)
{
    size_t l, r, j;
    char* lp;
    char* rp;
    char* ip;
    char* jp;
    char* kp;

    if (num_members < 2) {
        return;
    }
    r = num_members;
    l = (r / 2) + 1;
    lp = table_ptr(l);
    rp = table_ptr(r);
    for (;;) {
        if (l > 1) {
            l--;
            lp -= member_size;
        } else {
            swap(lp, rp, member_size);
            if (--r == 1) {
                return;
            }
            rp -= member_size;
        }
        j = l;
        jp = table_ptr(j);
        while (j * 2 <= r) {
            j *= 2;
            ip = jp;
            jp = table_ptr(j);
            if (j < r) {
                kp = jp + member_size;
                if (compare_members(jp, kp) < 0) {
                    j++;
                    jp = kp;
                }
            }
            if (compare_members(ip, jp) < 0) {
                swap(ip, jp, member_size);
            } else {
                break;
            }
        }
    }
}
