#define CERROR_FILE "unknown.c"
#include "compiler/common.h"
#include "driver/MemUtils.h"
#include "driver/CLBrowser.h"
#include "driver/CLErrors.h"
#include "driver/CLPlugins.h"
#include <string.h>

/* Source and destination handles for a preference data copy. */
#include <stdio.h>
#include <stdlib.h>



#if VERSION >= VERSION_GC_1_3
void *XMEMCALL xmalloc(const char *text, unsigned int size)
{
    char *memory;

    memory = malloc(size ? size : 1);
    if (memory == NULL) {
        fprintf(stderr, "*** Out of memory when allocating %d bytes%s%s", size, text ? " for " : "", text ? text : "");
        exit(-23);
        return NULL;
    }
    return memory;
}
#else
void *XMEMCALL xmalloc(const char *text, unsigned int size)
{
    char message[80];
    char *memory;

    memory = malloc(size ? size : 1);
    if (memory == NULL) {
        sprintf(message, "Out of memory when allocating %d bytes%s%s", size, text ? " for " : "", text ? text : "");
        CLErrors_FatalError(message);
        longjmp(driver_jmp_buf, 1);
        return NULL;
    }
    return memory;
}

#endif

void *XMEMCALL xcalloc(const char *pool, unsigned int size)
{
    unsigned char *memory;
    memory = xmalloc(pool, size);
    memset(memory, 0, size);
    return memory;
}

#if VERSION >= VERSION_GC_1_3
void *XMEMCALL xrealloc(const char *detail, void *block, unsigned int size)
{
    char *resizedBlock;

    resizedBlock = (char *)realloc(block, size ? size : 1U);
    if (!resizedBlock) {
        fprintf(stderr, "*** Out of memory when resizing buffer to %d bytes%s%s", size, detail ? " for " : "",
                detail ? detail : "");
        exit(-23);
        return NULL;
    }
    return resizedBlock;
}
#else
void *XMEMCALL xrealloc(const char *detail, void *block, unsigned int size)
{
    char message[80];
    char *resizedBlock;

    resizedBlock = (char *)realloc(block, size ? size : 1U);
    if (!resizedBlock) {
        sprintf(message, "Out of memory when resizing buffer to %d bytes%s%s", size, detail ? " for " : "",
                detail ? detail : "");
        CLErrors_FatalError(message);
        longjmp(driver_jmp_buf, 1U);
        return NULL;
    }
    return resizedBlock;
}

#endif

char *XMEMCALL xstrdup(const char *s)
{
    return strcpy(xmalloc(NULL, strlen(s) + 1), s);
}

#if VERSION >= VERSION_GC_1_3
void __stdcall xfree(void *block)
{
    if (block != NULL)
        free(block);
}
#endif

/* (2.x has it in CLPluginRequests.cpp) */
#if VERSION < VERSION_GC_2_7
int __stdcall MemUtils_CallPluginEntry(Plugin *entry)
{
    short result;
    result = CLPlugins_CallEntry(entry, entry->object);
    return result;
}
#endif

#pragma auto_inline reset
