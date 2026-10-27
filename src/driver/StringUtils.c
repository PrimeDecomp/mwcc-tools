#include "compiler/common.h"
#include "driver/StringUtils.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "driver/AssertionFailure.h"
#include "driver/Memory.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static char hprintf_buffer[256];

/* (wasm: the variadic arguments are not after the named ones, so the target's va_start, from <stdarg.h>) */
#ifndef NONMATCHING
#define va_start(ap, last)                                                                                             \
    ((ap) = (char *)&(last) + (((int)((char *)&(last) + sizeof(last)) - (int)(char *)&(last) + 3) / 4) * 4)
#endif
typedef char *(*TextFormatFunction)(char *buffer, unsigned int capacity, const char *format, va_list arguments);

UInt8 *_pstrcpy(UInt8 *destination, UInt8 *string)
{
    memcpy(destination, string, *string + 1);
    return destination;
}

#if defined(LINKER) && VERSION >= VERSION_GC_2_7
/* A Pascal string, at most MAXIMUM characters (the length byte as it is). */
void fn_00412140(UInt8 *destination, const UInt8 *source, int maximum)
{
    short count = source[0] > maximum ? maximum : source[0];

    for (; count >= 0; count--) {
        *destination++ = *source++;
    }
}
#endif

unsigned int pstrcmp(UInt8 *left, char *right)
{
    int length;
    unsigned int difference;

    if ((difference = (length = *left++) - ((UInt8)*right++)) > 0) {
        return difference;
    }
    while (length-- > 0) {
        if ((difference = (*left++ != (UInt8)*right++))) {
            return difference;
        }
    }
    return 0;
}

int pstrchr(UInt8 *characters, char character)
{
    int index;

    index = 0;
    while (index++ < *characters) {
        if (character == characters[index]) {
            return index;
        }
    }
    return 0;
}

void __stdcall c2pstrcpy(unsigned char *dst, const char *src)
{
    int length;

    length = strlen(src);
    if (0xff < length) {
        length = 0xff;
    }
    memmove(1 + dst, src, length);
    *dst = (char)length;
}

void __stdcall p2cstrcpy(char *destination, UInt8 *source)
{
    memcpy(destination, (source + 1), *source);
    destination[*source] = 0;
}

char *mvprintf(char *buffer, unsigned int size, const char *format, va_list args)
{
    int capacity;
    char *result;
    int length;

    if (buffer == NULL)
        CLIO_ReportAssertionFailure("mybuf != NULL", "StringUtils.c", BY_VERSION(135U, 134U));

    capacity = size - 1;
    result = buffer;
    length = vsnprintf(buffer, capacity, format, args);
    if (length < 0) {
        do {
            if (result != buffer)
                free(result);
            capacity <<= 1;
            result = malloc(capacity);
            if (result == NULL)
                return strncpy(buffer, "<out of memory>", size);
            length = vsnprintf(result, capacity, format, args);
        } while (length < 0);
    } else if (length > capacity) {
        capacity = length + 1;
        result = malloc(capacity);
        if (result == NULL)
            return strncpy(buffer, "<out of memory>", size);
        vsnprintf(result, capacity, format, args);
    }
    return result;
}

char *mprintf(char *buffer, int size, char *format, ...)
{
#ifdef NONMATCHING
    va_list args = __va_start(format);
#elif VERSION >= VERSION_GC_1_3
    va_list base = (va_list)&format;
    va_list args = base + (((va_list)(&format + 1) - base + 3) / 4) * 4;
#else
    va_list args = (va_list)&format + (((va_list)(&format + 1) - (va_list)&format + 3) / 4) * 4;
#endif
    char *result = mvprintf(buffer, size, format, args);
    return result;
}

int HPrintF(struct StorageHandle *output, char *format, ...)
{
    char *text;
    int length;
    va_list arguments;
#ifdef NONMATCHING
    va_start(arguments, format);
#elif VERSION >= VERSION_GC_1_3
    char *base;

    base = (char *)&format;
    arguments = base + (((int)((char *)&format + sizeof(format)) - (int)base + 3) / 4) * 4;
#else

    va_start(arguments, format);
#endif
    text = mvprintf(hprintf_buffer, 0x100, format, arguments);
    length = strlen(text);
    if (Memory_AppendStorageHandle(text, output, strlen(text)) != 0) {
        return 0;
    }
    if (text != hprintf_buffer) {
        free(text);
    }
    return length;
}

#pragma auto_inline reset
