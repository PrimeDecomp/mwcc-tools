#include "driver/StringExtras.h"
#include <ctype.h>
#include <string.h>

char* __stdcall strcatn(char* d, const char* s, SInt32 max) {
    char* p;

    p = d + strlen(d);
    while (*s && (p - d) + 1 < max) {
        *(p++) = *(s++);
    }

    *p = 0;
    return d;
}

char* __stdcall strcpyn(char* d, const char* s, SInt32 len, SInt32 max) {
    char* p;

    p = d;
    while (len-- && *s && (p - d) + 1 < max) {
        *(p++) = *(s++);
    }

    *p = 0;
    return d;
}

int __stdcall ustrcmp(const char* src, const char* dst) {
    int x;

    do {
        x = tolower(*src) - tolower(*(dst++));
        if (x) {
            return x;
        }
    } while (*(src++));

    return 0;
}

int __stdcall ustrncmp(const char* src, const char* dst, UInt32 len) {
    int x;

    // (Pro7's `while (len--)` compiles to `sub 1; jb`; 2.6 copies and tests the count)
    while (len-- != 0) {
        x = tolower(*src) - tolower(*(dst++));
        if (x) {
            return x;
        }
        if (!*(src++)) {
            return 0;
        }
    }

    return 0;
}
