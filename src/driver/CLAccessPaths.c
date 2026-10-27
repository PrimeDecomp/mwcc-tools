#include "compiler/common.h"
#include "driver/CLAccessPaths.h"
#include "driver/AssertionFailure.h"
#include "driver/CLIO.h"
#include "driver/CLPluginRequests.h"
#include "driver/Generic.h"
#include "driver/MacSpecs.h"
#include "driver/MemUtils.h"
#include <string.h>
#include <stdlib.h>
#if VERSION >= VERSION_GC_1_3
#include "driver/MemUtils.h"
#define free xfree
#endif

AccessPathEntry *init_access_path_entry(OSPathSpec *src, AccessPathEntry *result)
{
    result->path = xmalloc(NULL, sizeof(*src));
    *result->path = *src;
    result->children = NULL;
    result->auxiliary2 = 0;
#if VERSION >= VERSION_GC_1_3
    result->auxiliary3 = 0;
#endif
    return result;
}

AccessPathEntry *CLAccessPaths_CreateAccessPathEntry(OSPathSpec *path)
{
    AccessPathEntry *result;

    result = xmalloc(NULL, 0x10);
    return init_access_path_entry(path, result);
}

void free_access_path_entry(AccessPathEntry *blocks)
{
    if (blocks != NULL) {
        if (blocks->path != NULL) {
            free(blocks->path);
        }
        if (blocks->children != NULL) {
            CLAccessPaths_FreeItems(blocks->children);
            free(blocks->children);
        }
        free(blocks);
    }
}


unsigned char CLAccessPaths_Init(AccessPaths *paths)
{
    if (paths == NULL) {
        CLIO_ReportAssertionFailure("paths != NULL", "CLAccessPaths.c", 52U);
    }
    memset(paths, 0, sizeof(*paths));
    paths->items = NULL;
    return 1;
}


unsigned char CLAccessPaths_FreeItems(AccessPaths *paths)
{
    unsigned int index;
    if (!paths)
        CLIO_ReportAssertionFailure("paths != NULL", "CLAccessPaths.c", 63U);
    if (paths->items) {
        for (index = 0; (unsigned short)index < paths->count; ++index)
            free_access_path_entry(paths->items[(unsigned short)index]);
        free(paths->items);
    }
    paths->items = NULL;
    return 1;
}

Boolean allocate_slot(AccessPaths *table, UInt16 *slotIndex)
{
    if (table == NULL) {
        CLIO_ReportAssertionFailure("paths != NULL", "CLAccessPaths.c", 84U);
    }
    if (table->count >= table->capacity) {
        table->capacity += 20U;
        table->items = xrealloc("access paths", table->items, table->capacity << 2);
    }
    *slotIndex = table->count++;
    return 1;
}

#if VERSION >= VERSION_GC_1_3
unsigned char fn_00428b30(AccessPaths *list, UInt16 index)
{
    if (index >= list->count)
        return 0;
    free_access_path_entry(list->items[index]);
    return remove_access_path(list, index);
}
#endif

unsigned char CLAccessPaths_StoreItem(AccessPaths *ctx, void *value)
{
    UInt16 idx;
    if (allocate_slot(ctx, &idx)) {
        ctx->items[idx] = value;
        return 1;
    }
    return 0;
}

Boolean CLAccessPaths_InsertItem(AccessPaths *paths, UInt16 index, void *value)
{
    UInt16 n;
    if (allocate_slot(paths, &n)) {
        if (index > n)
#if VERSION >= VERSION_GC_1_3
            index = n ? n - 1 : 0;
#else
            index = n - 1;
#endif
        memmove(&paths->items[index + 1], &paths->items[index], (UInt32)(paths->count - index) * 4);
        paths->items[index] = value;
        return 1;
    }
    return 0;
}

unsigned char remove_access_path(AccessPaths *list, UInt16 index)
{
    if (index >= list->count)
        return 0;
#if VERSION >= VERSION_GC_1_3
    memmove(&list->items[index], &list->items[index + 1], (list->count - index - 1) * sizeof(*list->items));
#else
    free_access_path_entry(list->items[index]);
    memmove(&list->items[index], &list->items[index + 1], (list->count - index) * sizeof(*list->items));
#endif
    list->count--;
    return 1;
}

AccessPathEntry *CLAccessPaths_GetEntry(AccessPaths *table, unsigned short index)
{
    if (table == NULL)
        CLIO_ReportAssertionFailure("paths != NULL", "CLAccessPaths.c", BY_VERSION(142U, 151U));
    if ((unsigned short)index < table->count)
        return table->items[(unsigned short)index];
    return 0U;
}

unsigned short CLAccessPaths_GetCount(AccessPaths *table)
{
    if (!table) {
        CLIO_ReportAssertionFailure("paths != NULL", "CLAccessPaths.c", BY_VERSION(152U, 161U));
    }
    return table->count;
}


AccessPathEntry *CLAccessPaths_FindPath(AccessPaths *table, OSPathSpec *value)
{
    unsigned int index;
    AccessPathEntry *entry;

    if (!table)
        CLIO_ReportAssertionFailure("paths != NULL", "CLAccessPaths.c", BY_VERSION( 173U, 182U));
    for (index = 0; (unsigned short)index < table->count; ++index) {
        entry = table->items[(unsigned short)index];
        if (OS_EqualPathSpec(entry->path, value))
            return entry;
    }
    return NULL;
}

Boolean add_subdirectory_access_paths(AccessPaths *ctx, AccessPathEntry *param2)
{
    struct {
        DirectorySearch state;
    } bufA;
    OSSpec bufB;
#if defined(LINKER) && VERSION >= VERSION_GC_2_7
    /* (2.x: names up to 255 characters) */
    char bufC[256];
    char *last;
#else
    char bufC[67];
#endif
    Boolean flag;
    DWORD status;
    int count = 0;
    char *name;
    unsigned short s;
    AccessPathEntry *ptr;

    if (fn_004151f0())
        return 1;
    status = OS_OpenDir(param2->path, &bufA.state);
    while (status == 0) {
        status = OS_ReadDir(&bufA.state, &bufB, bufC, &flag);
        if (status == 0) {
            if (flag == 0) {
                ptr = CLAccessPaths_CreateAccessPathEntry(&bufB.path);
                name = OS_GetFileNamePtr(bufC);
#if defined(LINKER) && VERSION >= VERSION_GC_2_7
                /* (2.x: a directory named "(...)", its trailing separator aside, is skipped; on failure the entry is
                   freed as well) */
                last = name + strlen(name) - 1;
                if (*last == '\\')
                    last--;
                if (!(last > name && *name == '(' && *last == ')')) {
                    if (!CLAccessPaths_StoreItem(ctx, ptr))
                        break;
                    s = CLAccessPaths_GetCount(ctx) + 0xFFFF;
                    if (!add_subdirectory_access_paths(ctx, ptr))
                        fn_00428b30(ctx, s);
                }
#else
                if (*name != '(' && name[strlen(name) - 2] != ')') {
                    if (!CLAccessPaths_StoreItem(ctx, ptr))
                        break;
                    s = CLAccessPaths_GetCount(ctx) + 0xFFFF;
                    if (!add_subdirectory_access_paths(ctx, ptr))
                        remove_access_path(ctx, s);
                }
#endif
            } else {
                count++;
                if (count % 50 == 0 && fn_004151f0())
                    return 0;
            }
        }
    }
    OS_CloseDir(&bufA.state);
    return count > 0;
}


unsigned char CLAccessPaths_InitializeChildren(AccessPathEntry *path)
{
    path->children = xcalloc(0U, 8U);
    add_subdirectory_access_paths(path->children, path);
    return (unsigned char)(CLAccessPaths_GetCount(path->children) != 0);
}

int count_access_paths_recursive(AccessPaths *accessPaths)
{
    unsigned short index;
    int count;
    AccessPathEntry *entry;

    count = CLAccessPaths_GetCount(accessPaths);
    for (index = 0; index < count; index++) {
        entry = CLAccessPaths_GetEntry(accessPaths, index);
        if (entry == NULL)
            CLIO_ReportAssertionFailure("path", "CLAccessPaths.c", BY_VERSION(0x146, 0x150));
        if (entry->children)
            count += count_access_paths_recursive(entry->children);
    }
    return count;
}

/* An access-path entry and its nested list. */
void copy_access_paths_to_file_specs(CWFileSpec **destination, AccessPaths *paths, short *remaining)
{
    int validEntry;
    AccessPathEntry *entry;
    UInt16 index;
    OSSpec spec;

    for (index = 0; index < CLAccessPaths_GetCount(paths); index = index + 1) {
        entry = CLAccessPaths_GetEntry(paths, index);
        validEntry = 0;
        if ((entry != NULL) && (*remaining != 0)) {
            validEntry = 1;
        }
        if (!validEntry) {
            CLIO_ReportAssertionFailure("path && *count > 0", "CLAccessPaths.c", BY_VERSION(0x157, 0x161));
        }
        OS_MakeSpecWithPath(entry->path, NULL, '\0', &spec);
        if (OS_OSSpec_To_FSSpec(&spec, *destination) == 0) {
            CLIO_ReportAssertionFailure("OS_OSSpec_To_FSSpec(&spec, *list)", "CLAccessPaths.c", BY_VERSION(0x159, 0x163));
        }
        *destination += 1;
        *remaining -= 1;
        if (entry->children != NULL) {
            copy_access_paths_to_file_specs(destination, entry->children, remaining);
        }
    }
    return;
}

void copy_access_paths_to_file_specs_checked(CWFileSpec *path, AccessPaths *value, short result)
{
    copy_access_paths_to_file_specs(&path, value, &result);
    if ((unsigned short)result != 0)
        CLIO_ReportAssertionFailure("count == 0", "CLAccessPaths.c", BY_VERSION(357U, 367U));
}
