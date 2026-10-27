#define CERROR_FILE "unknown.c"
#include "compiler/common.h"
#include "driver/CLPrefs.h"
#include "driver/AssertionFailure.h"
#include "driver/CLErrors.h"
#include "driver/CLIO.h"
#include "driver/CLMain.h"
#if VERSION >= VERSION_GC_2_7
#include "driver/MemUtils.h"
#endif
#include <string.h>

/* Source and destination handles for a preference data copy. */
#pragma auto_inline off

static NameTableEntry *name_table_entries;

#pragma auto_inline reset

#if VERSION >= VERSION_GC_2_7
/* (2.x: here, not in CLIO.c) */
NameTableEntry *create_data_block(char *name, const void *source, unsigned int size)
{
    NameTableEntry *block;
    char *end;
    block = xmalloc(NULL, 16U);
    if (!block)
        return block;
    block->name = xstrdup(name);
    block->handles.storage.destination = (StorageHandle *)Memory_NewHandle(size);
    if (!block->handles.storage.destination) {
#if VERSION >= VERSION_GC_1_3
        CLIO_OutOfMemory();
#else
        CLIO_FormatAndDispatchText("\nOut of memory\n");
        longjmp(driver_jmp_buf, 1U);
#endif
    }
    block->handles.storage.temporary = NULL;
    fn_00413a00(block->handles.storage.destination);
    if (source) {
        memcpy(block->handles.storage.destination->data, source, size);
    } else {
        end = block->handles.storage.destination->data;
        memset(end, 0, size);
        end += size;
    }
    fn_00413a50(block->handles.preference.source);
    block->next = NULL;
    return block;
}
#endif

StorageHandle *CLPrefs_CopyDestinationToTemporary(NameTableEntry *preferences)
{
    NameTableEntry *copy;
    unsigned int size;
    copy = preferences;
    size = Memory_GetHandleSize(copy->handles.storage.destination);
#if VERSION >= VERSION_GC_1_3
    if (copy->handles.storage.temporary == NULL || copy->handles.storage.temporary->data == NULL) {
#else
    if (copy->handles.storage.temporary == NULL) {
#endif
        copy->handles.storage.temporary = (StorageHandle *)Memory_NewHandle(size);
    } else {
        Memory_ResizeStorageHandle(copy->handles.storage.temporary, size);
    }
    if (Memory_GetError() != 0) {
        CLIO_ReportAssertionFailure("MemError()==noErr", "CLPrefs.c", 60);
    }
    fn_00413a00(copy->handles.storage.destination);
    fn_00413a00(copy->handles.storage.temporary);
    memcpy(copy->handles.storage.temporary->data, copy->handles.storage.destination->data, size);
    fn_00413a50(copy->handles.storage.temporary);
    fn_00413a50(copy->handles.storage.destination);
    return copy->handles.storage.temporary;
}

int CLPrefs_CopyStorage(NameTableEntry *storage, StorageHandle *source)
{
    unsigned int size = Memory_GetHandleSize(source);
    if (source != storage->handles.storage.temporary) {
        if (storage->handles.storage.temporary == NULL) {
            storage->handles.storage.temporary = (StorageHandle *)Memory_NewHandle(size);
        } else {
            Memory_ResizeStorageHandle(storage->handles.storage.temporary, size);
        }
        if (Memory_GetError() != 0) {
            return 0;
        }
        fn_00413a00(storage->handles.storage.temporary);
        fn_00413a00(source);
        memcpy(storage->handles.storage.temporary->data, source->data, size);
        fn_00413a50(source);
        fn_00413a50(storage->handles.storage.temporary);
    }
    fn_00413a00(storage->handles.storage.destination);
    fn_00413a00(storage->handles.storage.temporary);
    memcpy(storage->handles.storage.destination->data, storage->handles.storage.temporary->data, size);
    fn_00413a50(storage->handles.storage.temporary);
    fn_00413a50(storage->handles.storage.destination);
    return 1;
}

Boolean CLPrefs_AddPrefPanel(NameTableEntry *entry)
{
    NameTableEntry **link;

    link = &name_table_entries;
    while (*link != NULL) {
        if (strcmp((*link)->name, entry->name) == 0) {
            CLErrors_EmitDiagnostic(0x5a, entry->name);
            return 0;
        }
        link = &(*link)->next;
    }
    if (clState.pluginDebug != '\0') {
        CLIO_FormatAndDispatchText("Defining/adding pref panel '%s'\n", entry->name);
    }
    *link = entry;
    return 1;
}

NameTableEntry *CLPrefs_FindNameTableEntry(char *name)
{
    NameTableEntry *entry = name_table_entries;
    while (entry != NULL) {
        if (CLIO_CompareStringsIgnoreCase(entry->name, name) == 0) {
            break;
        }
        entry = entry->next;
    }
    return entry;
}

#if VERSION < VERSION_GC_2_7
unsigned int CLPrefs_ConvertLoneLFToCR(StorageHandle *buffer)
{
    char *cursor;
    char *end;
    int length;
    fn_00413a00(buffer);
    end = buffer->data;
    length = Memory_GetHandleSize(buffer);
    cursor = end;
    while (cursor < end + length) {
        if (*cursor == '\r') {
            if (cursor[1] == '\n')
                cursor += 2;
            else
                cursor++;
        } else {
            if (*cursor == '\n')
                *cursor = '\r';
            cursor++;
        }
    }
    fn_00413a50(buffer);
    return 0;
}
#endif

#pragma auto_inline off

#pragma auto_inline reset

#pragma auto_inline off

#pragma auto_inline reset
