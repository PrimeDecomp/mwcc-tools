#define CERROR_FILE "unknown.c"
#include "compiler/common.h"
#include "driver/ResourceStrings.h"
#include <stdio.h>

#if VERSION >= VERSION_GC_1_3
#include <string.h>
#include "compiler/win32.h"
#include "driver/CLMain.h"
#include "driver/MemUtils.h"
#include "driver/MsDos.h"
#endif

static ResourceRegistration resourceRegistrations[16];
static char resource_string_buffer[64];
#if defined(LINKER) && VERSION >= VERSION_GC_2_7
CompilerVersionInfo13 data_005d89be;  /* (the 2.x linker has it in this unit's data) */
#endif

int ResourceStrings_AddResource(char *resourceData, short resourceId, char **resourceValue)
{
    int i;

    for (i = 0; i < 16 && resourceRegistrations[i].resourceId != 0; ++i) {
        if (resourceId == resourceRegistrations[i].resourceId) {
            fprintf(stderr, "Resource %d is already added!\n", resourceId);
            return 0;
        }
    }
    if (i >= 16) {
        return 0;
    }
    resourceRegistrations[i].data = resourceData;
    resourceRegistrations[i].resourceId = resourceId;
    resourceRegistrations[i].value = resourceValue;
    return 1;
}

char *ResourceStrings_GetString(short id, short index)
{
    int i;
    int j;
    index--;
    for (i = 0; i < 16; i++) {
        if (id == resourceRegistrations[i].resourceId) {
            j = 0;
            if (index < 0) {
#if VERSION >= VERSION_GC_1_3
                snprintf(resource_string_buffer, 0x100, "[Illegal string index #%d in list '%s' (%d)]", index,
                        resourceRegistrations[i].data, id);
#else
                sprintf(resource_string_buffer, "[Illegal string index #%d in list '%s' (%d)]", index,
                        resourceRegistrations[i].data, id);
#endif
                return resource_string_buffer;
            }
            while (j <= index) {
                if (resourceRegistrations[i].value[j] == NULL) {
#if VERSION >= VERSION_GC_1_3
                    snprintf(resource_string_buffer, 0x100, "[String #%d not found in resource '%s' (%d)]", index + 1,
                            resourceRegistrations[i].data, id);
#else
                    sprintf(resource_string_buffer, "[String #%d not found in resource '%s' (%d)]", index + 1,
                            resourceRegistrations[i].data, id);
#endif
                    return resource_string_buffer;
                }
                if (j == index)
                    return resourceRegistrations[i].value[j];
                j++;
            }
        }
    }
    return NULL;
}

#if VERSION >= VERSION_GC_1_3

char *fn_00408f30(void)
{
    char *r;
    if (data_005d89be.valid)
        r = (char *)&data_005d89be;
    else
        r = 0;
    return r;
}
#endif

#if VERSION >= VERSION_GC_1_3
extern void *alloca(unsigned int size);
extern char *format_plugin_version(Plugin *plugin);

/* read the compiler's own version resource: its version numbers and company, product, description and copyright */
void fn_00408cc0(void)
{
    DWORD handle;
    char path[0x104];
    UINT length;
    DWORD *fixed;
    char *value;
    char text[0x50];
    DWORD size;
    void *info;
    char *source;
    char *copy;
    char *cursor;
    int i;

    data_005d89be.valid = 0;
    OS_SpecToString(&clState.programSpec, path, 0x104);
    size = GetFileVersionInfoSizeA(path, &handle);
    if (size == 0)
        return;
    info = alloca(size);
    if (!GetFileVersionInfoA(path, handle, size, info))
        return;
    if (!VerQueryValueA(info, "\\", (LPVOID *)&fixed, &length))
        return;
    data_005d89be.valid = 1;
    data_005d89be.numbers[0] = fixed[4] >> 16;
    data_005d89be.numbers[1] = fixed[4];
    data_005d89be.numbers[2] = fixed[5] >> 16;
    data_005d89be.numbers[3] = fixed[5];
    if (VerQueryValueA(info, "\\StringFileInfo\\040904B0\\CompanyName", (LPVOID *)&value, &length))
        data_005d89be.company = xstrdup(value);
    else
        data_005d89be.company = NULL;
    if (VerQueryValueA(info, "\\StringFileInfo\\040904B0\\ProductName", (LPVOID *)&value, &length))
        data_005d89be.product = xstrdup(value);
    else
        data_005d89be.product = NULL;
    if (VerQueryValueA(info, "\\StringFileInfo\\040904B0\\FileDescription", (LPVOID *)&value, &length))
        data_005d89be.description = xstrdup(value);
    else
        data_005d89be.description = NULL;
    if (VerQueryValueA(info, "\\StringFileInfo\\040904B0\\LegalCopyright", (LPVOID *)&value, &length)) {
        copy = (char *)xmalloc(0, strlen(value) + 2);
        data_005d89be.copyright = copy;
        for (i = 0; value[i] != 0; i++) {
            if ((signed char)value[i] == (signed char)0xA9) {
                *copy++ = '(';
                *copy++ = 'c';
                *copy++ = ')';
            } else {
                *copy++ = value[i];
            }
        }
        *copy = 0;
    } else {
        data_005d89be.copyright = NULL;
    }
    if (VerQueryValueA(info, "\\StringFileInfo\\040904B0\\ProductVersion", (LPVOID *)&value, &length)) {
        source = value;
        for (;;) {
            char c = *source;
            if (c >= '0' && c <= '9') {
                source++;
                continue;
            }
            if (c == '.' || c == ' ') {
                source++;
                continue;
            }
            break;
        }
        if (source <= value + 2 || *source == 0)
            snprintf(text, 0x50, "%s", format_plugin_version(NULL));
        else
            snprintf(text, 0x50, "%s (%s)", format_plugin_version(NULL), source);
        data_005d89be.version = xstrdup(text);
    } else {
        data_005d89be.version = xstrdup("Version %%s");
    }
}
#endif
