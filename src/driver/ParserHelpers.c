#define CERROR_FILE "unknown.c"
#include "compiler/common.h"
#include "driver/ParserHelpers.h"
#include "driver/CWPluginsPrivate.h"
#include "driver/Generic.h"
#include "driver/MacSpecs.h"
#include "driver/Memory.h"
#include "driver/MsDos.h"
#include "driver/ParserErrors.h"
#include "driver/ParserFace.h"
#include "driver/Projects.h"
#include "driver/ToolHelpers-cc.h"
#include "driver/Utils.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "driver/CLDropinCallbacks_V10.h"

int data_00587e14;
int data_00587e18;
Boolean data_00587e20;
#if VERSION >= VERSION_GC_1_3
/* (1.3: the option table's storage in the raw data) */
extern char at_005e73e1[];
#define data_0058851b (*(unsigned char *)at_005e73e1)
#else
char data_0058851b;
#endif

short data_0054a0b8 = 0;

#ifndef NONMATCHING /* (wasm: the variadic arguments are not after the named ones; the target's, from <stdarg.h>) */
#define va_start(ap, parm) ap = (char *)&parm + ((((long)((char *)&parm + 2) - (long)&parm) + 3) / 4 * 4)
#endif
int get_spec_from_signature_callback(const char *path, OSSpec *spec)
{
    DropinFileCallback l;
    l.enableDependencyLookup = 1;
    l.searchOption = 0;
    l.suppressFileReferenceLookup = 1;
    l.fileKey = -1;
    if (CWPluginsPrivate_CallSignatureCallback(pluginPrivateContext, path, &l) == 0) {
        MacSpecs_MakeOSSpec(&l.output, spec);
        return 1;
    }
    return 0;
}

char *ParserHelpers_GetFirstEnvironmentVariable(char *list, char verbose, char **out)
{
    char *p = list;
    char *last = list;
    char *r;
    while (*p) {
        if ((r = getenv(p)) != NULL) {
            if (out)
                *out = p;
            return r;
        }
        last = p;
        p += strlen(p) + 1;
    }
    if (verbose)
        Targets_ReportMessage(0x34, last);
    *out = NULL;
    return NULL;
}

#if LINKER >= 27
#include "driver/MemUtils.h"
#include "driver/CWParserPluginsPrivate.h"

/* (2.7) The options of an environment variable (%MWLinkEABIPPCOptions%, the parser glue's) put into the command line
   after the tool's name and any leading '--' arguments: split at white space, with "..." quoting and \" for a
   quote, and '#' standing for '='. */
void ParserHelpers_AddEnvironmentOptions(char *options)
{
    static const char spaces[] = " \t\r\n\v";
    char **list;
    int count;
    int capacity;
    char *text;
    Boolean quoted;
    int at;
    char **argv;

    count = 0;
    capacity = 1;
    list = xmalloc(NULL, 4);
    text = xmalloc(NULL, strlen(options) + 1);
    while (*options && strchr(spaces, *options))
        options++;
    while (*options) {
        if (count + 1 >= capacity) {
            capacity += 16;
            list = xrealloc("environment options", list, capacity * 4);
        }
        list[count++] = text;
        quoted = 0;
        while (*options) {
            if (!quoted && strchr(spaces, *options)) {
                while (strchr(spaces, *options)) {
                    options++;
                    if (!*options)
                        break;
                }
                break;
            }
            if (*options == '"') {
                options++;
                quoted = !quoted;
            } else if (*options == '\\' && options[1] == '"') {
                *text++ = '"';
                options += 2;
            } else if (*options == '#') {
                *text++ = '=';
                options++;
            } else {
                *text++ = *options++;
            }
        }
        *text++ = 0;
    }
    *text = 0;
    list[count] = NULL;
    at = 0;
    do {
        at++;
    } while (at < cmdline_environment->argc && strncmp(cmdline_environment->argv[at], "--", 2) == 0);
    argv = xmalloc("environment options", (cmdline_environment->argc + count + 1) * 4);
    memcpy(argv, cmdline_environment->argv, at * 4);
    memmove(argv + at + count, cmdline_environment->argv + at, (cmdline_environment->argc - at + 1) * 4);
    memcpy(argv + at, list, count * 4);
    cmdline_environment->argv = argv;
    cmdline_environment->argc += count;
}
#endif

Boolean match_extension_pattern(char *pattern, char *name)
{
    char *base = OS_GetFileNamePtr(name);
    char *ext = strrchr(base, '.');
    char *p;
    if (!ext)
        return 0;
    if (!pattern)
        return 1;
    p = ext;
    while (*pattern) {
        if (*pattern == '|' && *p == 0)
            return 1;
        if (TO_LOWERCASE_CHAR(*pattern) == TO_LOWERCASE_CHAR(*p)) {
            pattern++;
            p++;
        } else {
            while (*pattern && *pattern != '|')
                pattern++;
            if (*pattern)
                pattern++;
            p = ext;
        }
    }
    return *pattern == 0 && *p == 0;
}

int fn_0040c9e7(char *name, int recursive, char *override)
{
    OSPathSpec spec;
    UInt32 err;
    char *path = override ? override : name;
    if (!path)
        return 0;
    if (!*path)
        return 1;
    if (strlen(path) >= 0x104) {
        fn_0040ecb1(0xd, path + strlen(path) - 32, 0x104);
        return 0;
    }
    err = OS_MakePathSpec(NULL, path, &spec);
    if (err == 2 || err == 3) {
        Targets_ReportMessage(0x2d, path);
        return 1;
    }
    if (err) {
        Targets_ReportOperatingSystemError(0x2d, err, path);
        return 1;
    }
    if (!ToolHelpers_cc_AddAccessPath(&spec, data_0058851b, 0, recursive ? 1 : 0))
        return 0;
    return 1;
}

void report_environment_variable_message(void (*report)(char *msg, va_list ap), char *where, int id, ...)
{
    char buf[0x400];
    va_list ap;
    if (where && *where)
        sprintf(buf, "In environment variable '%s':\n", where);
    else
        buf[0] = 0;
    va_start(ap, id);
    Targets_GetResourceCString(id, buf + strlen(buf));
    report(buf, ap);
}

int ParserHelpers_ParsePathList(char *path, char separator, char alternateSeparator, int mode, char *name, char option,
                                int flags, unsigned char toggle)
{
    int result;
    char *end;
    unsigned char plus;
    char component[260];
    OSPathSpec resolved;
#if defined(LINKER) && VERSION >= VERSION_GC_2_7
    /* (the 2.x linker passes a kind of path for the option: 1 for option 1, and 2 more when mode is not 0) */
    int kind = (option == 1 ? 1 : 0) | (mode != 0 ? 2 : 0);
#define PATH_KIND kind
#else
#define PATH_KIND option
#endif
    plus = *path == '+';
    if (plus) {
        ++path;
    }
    if ((separator == ':' || alternateSeparator == ':') && Utils_IsAlpha(*path) != 0 && path[1] == ':' &&
        OS_MakePathSpec(NULL, path, &resolved) == 0) {
        result = ToolHelpers_cc_AddAccessPath(&resolved, PATH_KIND, flags, plus ^ toggle);
    } else {
        if (strchr(path, separator) == NULL) {
            separator = alternateSeparator;
        }
        while (*path != 0) {
            end = component;
            for (; *path != 0 && *path != separator && end + 1 < component + 260; ++end) {
                *end = *path;
                ++path;
            }
            *end = 0;
            if (end + 1 >= component + 260) {
                report_environment_variable_message(Targets_FormatAndDispatchMessage, mode == 1 ? name : NULL, 9,
                                                    component, component + strlen(component) - 16, 260);
                return 0;
            }
            if (OS_MakePathSpec(NULL, component, &resolved) != 0) {
#if defined(LINKER) && LINKER >= 27
                /* (the 2.7 linker: an environment variable's missing directory is not reported) */
                if (mode != 1)
#endif
                report_environment_variable_message(Targets_ReportFormattedMessage, mode == 1 ? name : NULL, 45,
                                                    component);
            } else {
                ToolHelpers_cc_AddAccessPath(&resolved, PATH_KIND, flags, plus ^ toggle);
            }
            if (*path != 0) {
                ++path;
            }
            plus = *path == '+';
            if (plus) {
                ++path;
            }
        }
        result = 1;
    }
    return result;
}
#undef PATH_KIND

static char match_path_buffer[0x104];

int fn_0040cd55(char *name, char *filter, char *override)
{
    OSSpec spec;
    Boolean isdir;
    char *path = override ? override : name;
    int err;
    OSSpec *match;
    if (!path)
        return 0;
    if (!*path)
        return 1;
    data_00587e14++;
    err = OS_MakeSpec(path, &spec, &isdir);
    if (!err)
        err = OS_Status(&spec);
    if (!err && !isdir) {
        fn_0040ecb1(0x2f, path);
        data_00587e18++;
        return 0;
    }
    if (err && err != 2 && !strpbrk(path, "*?")) {
        Targets_ReportOperatingSystemError(0x2c, err, path);
        data_00587e18++;
        return 0;
    }
    if (err && (match = OS_MatchPath(path))) {
        do {
            if (filter && !match_extension_pattern(filter, OS_NameSpecToString(&match->name, match_path_buffer, 0x104)))
                Targets_ReportMessage(0x4c, OS_SpecToStringRelative(match, NULL, match_path_buffer, 0x104), filter);
            if (!ToolHelpers_cc_AddProjectEntry(match, data_0054a0b8, data_00587d04, 1, -1)) {
                data_00587e18++;
                return 0;
            }
            data_00587d04[0] = 0;
        } while ((match = OS_MatchPath(NULL)));
        return 1;
    }
    if (err && data_00587e20) {
        if (get_spec_from_signature_callback(path, &spec))
            err = 0;
        else
            err = 2;
    }
    if (err) {
        fn_0040ecb1(0x2c, path);
        data_00587e18++;
        return 0;
    }
    if (filter && !match_extension_pattern(filter, path))
        Targets_ReportMessage(0x4c, path, filter);
    if (!ToolHelpers_cc_AddProjectEntry(&spec, data_0054a0b8, data_00587d04, 1, -1)) {
        data_00587e18++;
        return 0;
    }
    data_00587d04[0] = 0;
    return 1;
}

static char lbl_0054a0e0[] = ".lib|.a";
static char lbl_0054a0e8[] = "lib%s%*.*s";

#if defined(LINKER) && VERSION >= VERSION_GC_2_7
/* (the linker's -l and its file lists: the compilers have them too, but nothing calls them there, so their links drop
   them) */

/* -lNAME: the first of libNAME followed by one of EXTENSIONS ('|' between them, ".lib|.a" by default) found in the
   access paths, or else NAME itself, added to the project. */
int ParserHelpers_AddUnixLibraryFile(char *option, char *extensions, char *name)
{
    OSSpec spec;
    Boolean failed;
    char *start;
    char *end;
    char filename[256];

    if (strlen(name) >= 248) {
        fn_0040ecb1(13, name + strlen(name) - 32, 256);
        return 0;
    }
    failed = 1;
    if (!extensions)
        extensions = lbl_0054a0e0;
    start = extensions;
    while (start && *start) {
        end = start;
        while (*end && *end != '|')
            ++end;
        sprintf(filename, lbl_0054a0e8, name, end - start, end - start, start);
        if (get_spec_from_signature_callback(filename, &spec)) {
            failed = 0;
            break;
        }
        if (*end)
            ++end;
        start = end;
    }
    if (failed) {
        failed = !get_spec_from_signature_callback(name, &spec);
        if (failed)
            fn_0040ecb1(0x31, name, extensions, name);
    }
    if (!failed && !ToolHelpers_cc_AddProjectEntry(&spec, 0, NULL, 1, -1)) {
        data_00587e18++;
        failed = 1;
    }
    return !failed;
}

/* Each file of LIST (SEPARATOR between them, or ALTERNATESEPARATOR if LIST has no SEPARATOR) found in the access
   paths added to the project at POSITION; NAME is the environment variable LIST came from when MODE is 1. */
int ParserHelpers_AddFileList(char *list, char separator, char alternateSeparator, int mode, char *name,
                              SInt32 position)
{
    char *end;
    char component[260];
    OSSpec spec;

    if (strchr(list, separator) == NULL)
        separator = alternateSeparator;
    while (*list != 0) {
        end = component;
        for (; *list != 0 && *list != separator && end + 1 < component + 260; ++end) {
            *end = *list;
            ++list;
        }
        *end = 0;
        if (end + 1 >= component + 260) {
            report_environment_variable_message(Targets_FormatAndDispatchMessage, mode == 1 ? name : NULL, 9,
                                                component, component + strlen(component) - 16, 260);
            return 0;
        }
        if (!get_spec_from_signature_callback(component, &spec)) {
#ifdef NONMATCHING
            /* (the original asks OS_IsDir about the spec the failed lookup left, what its stack held, which is no
               directory: a file not found, even a directory, is reported as not found) */
            if (0)
#else
            if (OS_IsDir(&spec))
#endif
                report_environment_variable_message(Targets_FormatAndDispatchMessage, mode == 1 ? name : NULL, 16,
                                                    component);
            else
                report_environment_variable_message(Targets_ReportFormattedMessage, mode == 1 ? name : NULL, 44,
                                                    component);
        } else {
            ToolHelpers_cc_AddProjectEntry(&spec, 0, NULL, 1, position);
        }
        if (*list != 0)
            ++list;
    }
    return 1;
}
#endif

void ParserHelpers_AppendText(struct StorageHandle **hp, char *text)
{
    unsigned int len;
    if (!*hp) {
        *hp = (struct StorageHandle *)Memory_NewHandle(strlen(text) + 1);
        if (*hp) {
            fn_00413a00(*hp);
            memcpy((*hp)->data, text, strlen(text) + 1);
            fn_00413a50(*hp);
        } else
            longjmp(plugin_request_jmp_buf, 7);
    } else {
        len = Memory_GetHandleSize(*hp) - 1;
        Memory_ResizeStorageHandle(*hp, len + strlen(text) + 1);
        if (!Memory_GetError()) {
            fn_00413a00(*hp);
            memcpy((*hp)->data + len, text, strlen(text) + 1);
            fn_00413a50(*hp);
        } else
            longjmp(plugin_request_jmp_buf, 7);
    }
}

int fn_0040cff6(void)
{
    ToolHelpers_cc_PrintVersion(0);
    return 1;
}

int fn_0040d002(void)
{
    fn_0040ba99(pluginPrivateContext);
    return 1;
}
