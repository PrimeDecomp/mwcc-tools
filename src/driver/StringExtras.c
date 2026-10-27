#include "compiler/common.h"
#include "driver/StringExtras.h"
#include <string.h>

char *__stdcall CLProj_AppendString(char *dest, char *src, int size)
{
    char *end;
    char ch;

    end = dest + strlen(dest);
    while ((ch = *src) != '\0' && (int)(end - dest + 1) < size) {
        *end = ch;
        src++;
        end++;
    }
    *end = '\0';
    return dest;
}

char *__stdcall CLProj_CopyStringBounded(char *destination, const char *source, unsigned int count, int capacity)
{
    char *output;

    for (output = destination; (count-- && *source != '\0' && (int)(output - destination + 1) < capacity);
         output = 1 + output) {
        *output = *source;
        ++source;
    }
    *output = '\0';
    return destination;
}

#if VERSION >= VERSION_GC_2_7
#include <ctype.h>

/* (2.x: here, mweppc's ustrcmp and ustrncmp; 1.3's are CLIO.c's and ClientGlue.c's) */
int __stdcall CLIO_CompareStringsIgnoreCase(char *left, char *right)
{
    int difference;

    do {
        difference = tolower(*left) - tolower(*(right++));
        if (difference)
            return difference;
    } while (*(left++));
    return 0;
}

int __stdcall fn_004050e0(char *left, char *right, int count)
{
    int difference;

    while (count-- != 0) {
        difference = tolower(*left) - tolower(*(right++));
        if (difference)
            return difference;
        if (!*(left++))
            return 0;
    }
    return 0;
}
#endif
