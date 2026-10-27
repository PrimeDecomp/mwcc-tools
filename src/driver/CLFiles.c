#include "compiler/common.h"
#include "driver/CLFiles.h"
#include "driver/AssertionFailure.h"
#include "driver/CLFileOps.h"
#include "driver/CLPrefs.h"
#include "driver/MemUtils.h"
#include "driver/Memory.h"
/* Link used by the file cleanup list. */
#include <string.h>

#include <stdlib.h>
#if VERSION >= VERSION_GC_1_3
#include "driver/MemUtils.h"
#define free xfree
#endif

struct DropinFileRecord *CLFiles_AllocDropinFileRecord(void)
{
#if defined(LINKER) && VERSION >= VERSION_GC_2_7
    /* (2.x: 0x6a0 bytes) */
    const unsigned int bufferSize = 0x6a0;
#else
    const unsigned int bufferSize = BY_VERSION(0x520, 0x6a4);
#endif
    struct DropinFileRecord *buffer;

    buffer = xmalloc(NULL, bufferSize);
    if (buffer == NULL) {
        return NULL;
    }
    memset(buffer, 0, bufferSize);
    return buffer;
}

#if VERSION >= VERSION_GC_1_3
void free_allocation_record(void *ptr)
{
#define allocation ((DropinFileRecord *)ptr)
#else
void free_allocation_record(void *ptr)
{
    DropinFileRecord *allocation = ptr;
#endif

    if (allocation != NULL) {
#if VERSION >= VERSION_GC_1_3
        if (allocation->outputStorage != NULL) {
            Memory_FreeHandle(allocation->outputStorage);
            allocation->outputStorage = NULL;
        }
#endif
        if (allocation->objectData != NULL) {
            Memory_FreeHandle(allocation->objectData);
            allocation->objectData = NULL;
        }
        if (allocation->secondaryReferenceHandle != NULL) {
            Memory_FreeHandle(allocation->secondaryReferenceHandle);
            allocation->secondaryReferenceHandle = NULL;
        }
#if VERSION >= VERSION_GC_1_3
        free(ptr);
#else
        free(ptr);
#endif
    }
}

unsigned char CLFiles_AssertNonNullIndexedListLink(struct IndexedListLink *value)
{
    if (value == NULL)
        CLIO_ReportAssertionFailure("this != NULL", "CLFiles.c", BY_VERSION(41U, 47U));
    return 1;
}

#if VERSION >= VERSION_GC_1_3
#undef allocation
#endif
unsigned char CLFiles_FreeAllocationRecords(struct IndexedListLink *list)
{
    struct IndexedListLink *next;
    struct IndexedListLink *entry;

    if (list == NULL) {
        CLIO_ReportAssertionFailure("this != NULL", "CLFiles.c", BY_VERSION(0x32, 0x38));
    }
    entry = list->next;
    while (entry != NULL) {
        next = entry->next;
        free_allocation_record(entry);
        entry = next;
    }
    return 1;
}

unsigned char CLFiles_InsertIndexedListLinkAtFirstIndex(IndexedListLink *first, IndexedListLink *second)
{
    return CLFiles_InsertIndexedListLink(first, second, first->index);
}

char CLFiles_InsertIndexedListLink(IndexedListLink *list, IndexedListLink *node, int pos)
{
    IndexedListLink *p;
    if (list == NULL) {
        CLIO_ReportAssertionFailure("this != NULL", "CLFiles.c", BY_VERSION(74, 80));
    }
    if (node == NULL) {
        CLIO_ReportAssertionFailure("file != NULL", "CLFiles.c", BY_VERSION(75, 81));
    }
    if (pos < 0) {
        pos = 0;
    } else if (pos > list->index) {
        pos = list->index;
    }
    p = list;
    while (pos-- > 0) {
        p = p->next;
    }
    node->index = list->index++;
    node->next = p->next;
    p->next = node;
    return 1;
}

DropinFileRecord *CLFiles_FindFileByIndex(IndexedListLink *head, int index)
{
    IndexedListLink *entry;
    if (!head)
        CLIO_ReportAssertionFailure("this != NULL", "CLFiles.c", BY_VERSION(98U, 104U));
    if (index < 0)
        CLIO_ReportAssertionFailure("filenum >= 0", "CLFiles.c", BY_VERSION(99U, 105U));
    entry = head->next;
    while (entry && entry->index != index)
        entry = entry->next;
    return (DropinFileRecord *)((char *)entry - offsetof(DropinFileRecord, listEntry));
}

struct DropinFileRecord *CLFiles_FindDropinFileRecord(struct IndexedListLink *files, const struct OSSpec *fileSpec)
{
    struct DropinFileRecord *record =
        (struct DropinFileRecord *)((char *)files->next - offsetof(struct DropinFileRecord, listEntry));
    while (record != NULL && OS_EqualSpec(&record->inputPath, fileSpec) == 0)
        record =
            (struct DropinFileRecord *)((char *)record->listEntry.next - offsetof(struct DropinFileRecord, listEntry));
    return record;
}

SInt32 CLFiles_GetIndex(IndexedListLink *entry)
{
    if (!entry)
        CLIO_ReportAssertionFailure("this != NULL", "CLFiles.c", BY_VERSION(121U, 127U));
    return entry->index;
}

Boolean CLFiles_InitChain(struct ChainRecord **chain)
{
    *chain = NULL;
    return 1;
}

void CLFiles_FreeChainNodes(struct ChainRecord **nodes)
{
    struct ChainRecord *next;
    StorageHandle *record;

    while (*nodes != NULL) {
        next = (*nodes)->next;
        record = (StorageHandle *)(*nodes)->object;
        Memory_FreeHandle(record);
        free(*nodes);
        nodes = &next;
    }
    *nodes = NULL;
}

struct ChainRecord *CLFiles_CreateChainRecord(const char *source, StorageHandle *argument)
{
    struct ChainRecord *record;
    StorageHandle *buffer;
    record = xmalloc(0U, 40U);
    if (record == NULL)
        return NULL;
    strncpy(record->name, source, 32U);
    record->name[31] = 0;
    record->object = (StorageHandle *)Memory_NewHandle(0U);
    if (record->object == NULL || fn_00413b50(argument, record->object) != 0) {
        free(record);
        return NULL;
    }
    buffer = record->object;
    CLPrefs_ConvertLoneLFToCR(buffer);
    record->next = NULL;
    return record;
}

Boolean CLFiles_AppendChainRecord(struct ChainRecord **link, struct ChainRecord *record)
{
    for (; *link != NULL; link = &(*link)->next) {
    }
    *link = record;
    return 1;
}

ChainRecord *CLFiles_FindChainRecord(ChainRecord *first, const char *value)
{
    ChainRecord *input;
    const char *argument;
    ChainRecord *record;
    int empty;
    input = first;
    empty = (input == NULL);
    argument = value;
    record = input;
    if (!empty) {
        do {
            if (!strcmp(record->name, argument))
                break;
            record = record->next;
        } while (record);
    }
    return record;
}
