#include "compiler/common.h"
#include "driver/ParserErrors.h"
#include "compiler/objects.h"
#include "driver/CWPluginsPrivate.h"
#include "driver/ParserFace.h"
#include "driver/Projects.h"
#include "driver/TargetOptimizer-ppc-eabi.h"
#include "driver/TextUtils.h"
#include <stdio.h>

/* (wasm: the variadic arguments are not after the named ones, so the target's va_start and va_arg, from <stdarg.h>) */
#ifndef NONMATCHING
#define va_start(ap, parm) ap = (char *)&parm + ((((char *)(&parm + 1) - (char *)&parm) + 3) / 4 * 4)
#endif

static char formatted_message[1024];

#if defined(LINKER) && VERSION >= VERSION_GC_2_7
#include <string.h>

/* The output's name for the 2.x linker's parser glue: NAME (DEFAULT_NAME when empty), with the extension of the pairs
   (ID, EXTENSION) after DEFAULT_NAME, ending in -1, whose ID is KEY when ADD_EXTENSION and the name has no dot; cut to
   MAX_LENGTH - 1 characters, a Pascal string when TO_PASCAL. */
int fn_0040d952(char *name, char *output, unsigned int maxLength, Boolean toPascal, Boolean addExtension, int key,
                char *defaultName, ...)
{
    char buffer[256];
#ifdef NONMATCHING
    va_list pairs;
#else
    char *pairs;
#endif
    int id;
    char *extension;
    Boolean hasDot;

    va_start(pairs, defaultName);
    if (name == NULL || name[0] == 0) {
        strcpy(buffer, defaultName ? defaultName : "a.out");
    } else {
        strcpy(buffer, name);
    }
    hasDot = strchr(buffer, '.') != NULL;
#ifdef NONMATCHING
    while ((id = va_arg(pairs, int)) != -1 && (extension = va_arg(pairs, char *)) != NULL) {
#else
    while ((pairs += 4, id = ((int *)pairs)[-1]) != -1 && (pairs += 4, extension = ((char **)pairs)[-1]) != NULL) {
#endif
        if (key != id) {
            continue;
        }
        if (!hasDot && addExtension) {
            if (strlen(extension) + strlen(buffer) >= maxLength) {
                strcpy(buffer + maxLength - strlen(extension) - 1, extension);
            } else {
                strcat(buffer, extension);
            }
        }
        break;
    }
    if (strlen(buffer) >= maxLength) {
        Targets_ReportMessage(0x41, buffer, maxLength - 1);
        buffer[maxLength - 1] = 0;
    }
    strcpy(output, buffer);
    if (toPascal) {
        CLIO_ConvertToPascalString(output);
    }
    return 1;
}
#endif


void Targets_FormatAndDispatchMessage(char *message, char *arguments)
{
    vsprintf(formatted_message, message, (char *)(unsigned int *)arguments);
    CWPluginsPrivate_InvokeMessageCallback((struct DispatchObject_0041b830 *)pluginPrivateContext, NULL,
                                           formatted_message, NULL, 2, 0);
    data_00587e1d = 1;
}

void Targets_ReportFormattedMessage(char *format, char *arguments)
{
    vsprintf(formatted_message, format, arguments);
    CWPluginsPrivate_InvokeMessageCallback(pluginPrivateContext, NULL, formatted_message, NULL, 1, 0);
}

#undef CERROR_FILE
#define CERROR_FILE "unknown.c"

void format_and_dispatch_message(char *first, char *second)
{
    vsprintf(formatted_message, first, (char *)(unsigned int *)second);
    CWPluginsPrivate_InvokeMessageCallback((struct DispatchObject_0041b830 *)pluginPrivateContext, NULL,
                                           formatted_message, NULL, 0, 0);
}
#undef CERROR_FILE
#define CERROR_FILE __FILE__

void format_and_forward_message(char *message, unsigned int *arguments)
{
    vsprintf(formatted_message, message, (char *)arguments);
    fn_0041b8d0((CWPluginPrivateContext *)pluginPrivateContext, formatted_message, NULL);
}

void format_and_report_message(const char *text, va_list position)
{
    vsprintf(formatted_message, text, position);
    CWPluginsPrivate_CallCallback9(pluginPrivateContext, formatted_message, NULL, NULL, 0);
    data_00587e1d = 1;
}

void report_operating_system_error(char *name, DWORD value, unsigned int *result)
{
    vsprintf(formatted_message, name, (char *)result);
    CWPluginsPrivate_CallCallback9(pluginPrivateContext, formatted_message,
                                   "Operating system error:", (unsigned char *)OS_GetErrText(value), 0);
}

#undef CERROR_FILE
#define CERROR_FILE "unknown.c"

char *Targets_GetResourceCString(SInt32 argument, char *buffer)
{
    CLIO_GetResourceCString(buffer, 0x2eea, argument);
    return buffer;
}
#undef CERROR_FILE
#define CERROR_FILE __FILE__

#undef CERROR_FILE
#define CERROR_FILE "unknown.c"

void fn_0040ecb1(SInt32 messageId, ...)
{
    char message[256];
    va_list arguments;

    Targets_GetResourceCString(messageId, message);
#ifdef NONMATCHING
    va_start(arguments, messageId);
#else
    arguments = (char *)&messageId + (((char *)((SInt16 *)&messageId + 1) - (char *)&messageId + 3) / 4 * 4);
#endif
    Targets_FormatAndDispatchMessage(message, arguments);
}
#undef CERROR_FILE
#define CERROR_FILE __FILE__

#undef CERROR_FILE
#define CERROR_FILE "unknown.c"

void Targets_ReportMessage(int messageId, ...)
{
    char buffer[256];
    va_list args;

    Targets_GetResourceCString(messageId, buffer);
#ifdef NONMATCHING
    va_start(args, messageId);
#else
    args = (char *)&messageId + (((char *)((SInt16 *)&messageId + 1) - (char *)&messageId + 3) / 4 * 4);
#endif
    Targets_ReportFormattedMessage(buffer, args);
}
#undef CERROR_FILE
#define CERROR_FILE __FILE__

void Targets_DispatchVariadicMessage(int messageId, ...)
{
    char buffer[256];
    va_list arguments;
    char *argumentAddress;
    char *argumentEnd;

    Targets_GetResourceCString(messageId, buffer);
#ifdef NONMATCHING
    va_start(arguments, messageId);
#else
    argumentAddress = (va_list)&messageId;
    argumentEnd = (va_list)&messageId + sizeof(short);
    arguments = (va_list)&messageId + (argumentEnd - argumentAddress + 3) / 4 * 4;
#endif
    format_and_dispatch_message(buffer, arguments);
}

unsigned char Targets_ReportOperatingSystemError(int resourceId, short errorCode, ...)
{
#ifndef NONMATCHING /* (ParserErrors.h's, void: a call through another type traps in wasm) */
    unsigned char report_operating_system_error(char *message, int errorCode, va_list arguments);
#endif
    char message[256];
    va_list arguments;

    Targets_GetResourceCString(resourceId, message);
    va_start(arguments, errorCode);
#ifdef NONMATCHING
    report_operating_system_error(message, errorCode, (unsigned int *)arguments);
    return 0;
#else
    return report_operating_system_error(message, errorCode, arguments);
#endif
}
#undef CERROR_FILE
#define CERROR_FILE "unknown.c"

void Targets_FormatAndForwardMessage(int messageId, ...)
{
    char buffer[256];
    va_list arguments;

    Targets_GetResourceCString(messageId, buffer);
#ifdef NONMATCHING
    va_start(arguments, messageId);
#else
    arguments = (char *)&messageId + ((((char *)((short *)&messageId + 1) - (char *)&messageId) + 3) / 4 * 4);
#endif
    format_and_forward_message(buffer, (unsigned int *)arguments);
}
#undef CERROR_FILE
#define CERROR_FILE __FILE__

void Targets_ForwardVarArgsAndLongjmp(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    format_and_report_message(fmt, ap);
    longjmp(plugin_request_jmp_buf, -123);
}
