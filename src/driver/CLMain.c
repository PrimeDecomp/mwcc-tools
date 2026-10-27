#define CERROR_FILE "unknown.c"
#include "compiler/common.h"
#include "driver/CLMain.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "driver/CLBrowser.h"
#include "driver/CLErrors.h"
#include "driver/CLFileOps.h"
#include "driver/CLIO.h"
#include "driver/CLLicenses.h"
#include "driver/CLPluginRequests.h"
#include "driver/CLPlugins.h"
#include "driver/CLPrefs.h"
#include "driver/CLToolExec.h"
#include "driver/ClientGlue.h"
#include "driver/Generic.h"
#include "driver/MemUtils.h"
#include "driver/Resources.h"
#include "driver/StringExtras.h"
#include "driver/cc-eabi-ppc-mw.h"
#include <string.h>
#if VERSION >= VERSION_GC_1_3
#include "driver/ResourceStrings.h"
#endif

char input_name[32];
char output_name[32];
SInt32 unique_plugin_name_count;
SInt32 plugin_request_count;
char **unique_plugin_names;
struct ToolArgumentSet *file_argument_sets;
struct PluginRequest *plugin_requests;
struct ToolArgumentSet *tool_argument_sets;

Project mainProj;
Project *gProj = &mainProj;
#if defined(LINKER) && VERSION >= VERSION_GC_2_7
/* (the 2.x linker leaves them in its COMMON block: their versions are 0) */
PCmdLine optsCmdLine;
PCmdLineEnvir optsEnvir;
PCmdLineCompiler optsCompiler;
#else
PCmdLine optsCmdLine = {0x1002};
PCmdLineEnvir optsEnvir = {0x1000};
PCmdLineCompiler optsCompiler = {0x1004};
#endif
PCmdLineLinker optsLinker = {0x1002};
CLState clState;

/* The driver is itself a plugin ('cldr'): its descriptions are its getters' data. */

unsigned int __stdcall return_zero(unsigned int unused)
{
    return 0U;
}

static PluginDesc driver_flags = {2, 'cldr', 7, 0, 0, 11};

unsigned int __stdcall get_data_and_size(unsigned char **data, unsigned int *size)
{
    *data = (unsigned char *)&driver_flags;
    *size = 18U;
    return 0U;
}

#if VERSION >= VERSION_GC_1_3
/* (read from memory, not folded: named for the binding) */
SInt32 data_00541e44 = (SInt32)"Command-Line Driver";
#define driver_name data_00541e44
#else
static const char *driver_name = "Command-Line Driver";
#endif

unsigned int __stdcall copy_global_value_to_address(BY_VERSION(unsigned int, SInt32 *) destinationAddress)
{
    *(SInt32 *)destinationAddress = (SInt32)driver_name;
    return 0;
}

#if VERSION >= VERSION_GC_1_3
SInt32 data_00541e48 = (SInt32)"Command-Line Driver";
#define driver_display_name data_00541e48
#else
static const char *driver_display_name = "Command-Line Driver";
#endif

unsigned int __stdcall copy_global_value(BY_VERSION(unsigned int, SInt32 *) valueAddress)
{
    *(SInt32 *)valueAddress = (SInt32)driver_display_name;
    return 0;
}

static const char *panel_names[4];
static PluginDirectoryList panel_list = {1, 4, (char **)panel_names};

int __stdcall fn_0040a730(char **panelData)
{
    if (clState.plugintype == 'Comp') {
        panel_names[0] = "CmdLine Panel";
        panel_names[1] = "CmdLine Compiler Panel";
        panel_names[2] = "CmdLine Linker Panel";
        panel_list.count = 3;
    } else {
        panel_names[0] = "CmdLine Panel";
        panel_names[1] = "CmdLine Linker Panel";
        panel_list.count = 2;
    }
    *panelData = (char *)&panel_list;
    return 0;
}

static UInt32 target_cpu = '****';
static UInt32 target_os = '****';
static TargetInfo target_list = {1, 1, &target_cpu, 1, &target_os};

static PluginVersion driver_version = {3, 0, 0, 0};

unsigned int __stdcall fn_0040a7a0(struct ListLink *link)
{
    link->next = (struct ListLink *)&driver_version;
    return 0U;
}

static FileSignature file_signatures[2] = {{'Brws', "DubL", 4, 0}, {'MMPr', "looc", 4, 0}};
static FileSignatureList file_signature_list = {2, file_signatures};

unsigned int __stdcall set_listlink_next_to_global(struct ListLink *node)
{
    node->next = (struct ListLink *)&file_signature_list;
    return 0U;
}

static void *driver_callbacks[9] = {(void *)return_zero,
                                    (void *)get_data_and_size,
                                    (void *)copy_global_value,
                                    (void *)copy_global_value_to_address,
                                    (void *)fn_0040a730,
                                    NULL,
                                    NULL,
                                    (void *)fn_0040a7a0,
                                    (void *)set_listlink_next_to_global};

unsigned int fn_0040a7c0(void)
{
#ifdef NONMATCHING /* (x86 returns the EAX ClientGlue_AddPlugin leaves, which the caller tests) */
    return ClientGlue_AddPlugin((PluginRequiredInputRecord *)driver_callbacks);
#else
    ClientGlue_AddPlugin((PluginRequiredInputRecord *)driver_callbacks);
#endif
}

/* The "Command-line strings" (12000) the driver's messages index. */
static char *command_line_strings[] = {
    "Could not get current working directory",
    "Cannot find my executable '%s'",
    "Could not initialize plugin '%s'",
    "The %s '%s' requires functionality not present in the command-line driver.",
    "The command-line parser does not support these panels:",
    "\t%s\n",
    "Compiling function: '%s'",
    "Could not write file '%s'",
    "Could not write file '%s' in directory '%s'",
    "Write error on output (errno=%ld)",
    "Current working directory is too long",
    "Unknown filetype '%c%c%c%c', defaulting to '%s'",
    "%s:\ttype %s",
    "Storing output for '%s' in '%s'",
    "Writing text file '%s'",
    "Writing %sobject file '%s'",
    "Writing browse data '%s'",
    "Could not write %s '%s' (error %ld)",
    "Deleting temporary file '%s'",
    "Could not resolve alias for '%s' (error %ld)",
    "%s:\t'%s'%s",
    "File '%s' has browse fileID %d",
    "Can't locate directory '%s'",
    "  %8.2f seconds to %s %s%s%s",
    "  %8d lines compiled",
    "  %8d %s code\n  %8d %s init'd data\n  %8d %s uninit'd data",
    "  %8d total %s code\n  %8d total %s init'd data\n  %8d total %s uninit'd data",
    "File '%s' is not compilable source, target object data, or command file; ignoring",
    "All specified files were ignored",
    "Compiling: '%s'",
    "Compiling: '%s' with '%s'",
    "Precompiling: '%s'",
    "Precompiling: '%s' with '%s'",
    "Preprocessing: '%s'",
    "Preprocessing: '%s' with '%s'",
    "Finding dependencies: '%s'",
    "Finding dependencies: '%s' with '%s'",
    "Importing: '%s'",
    "Importing: '%s' with '%s'",
    "Linking project",
    "Linking project with '%s'",
    "Pre-linking project",
    "Pre-linking project with '%s'",
    "Post-linking project",
    "Post-linking project with '%s'",
    "Disassembling: '%s'",
    "Disassembling: '%s' with '%s'",
    "Syntax checking: '%s'",
    "Syntax checking: '%s' with '%s'",
    "Getting target info from '%s'",
    "Initializing '%s'",
    "Terminating '%s'",
    "'%s' cannot preprocess, skipping '%s'",
    "'%s' cannot precompile, skipping '%s'",
    "'%s' cannot generate code, skipping '%s'",
    "'%s' has no object code to disassemble",
    "'%s' cannot disassemble, skipping '%s'",
    "Neither '%s' nor '%s' can disassemble, skipping '%s'",
    "No precompiled header name given, '%s' assumed",
    "Precompile target '%s' given on command line; source-specified name '%s' ignored",
    "Writing precompiled %s file '%s'",
    "Reading precompiled %s file '%s'",
    "Cannot %s memory for %s",
    "Files/directories must have length <= %ld characters;\n'%s' not accepted",
    "Guessed linker name '%s' from compiler name '%s'",
    "Can't find %s '%s' in path",
    "Calling %s '%s'",
    "Can't execute %s '%s' (%s)",
    "%s '%s' returned with exit code %d",
    "Too many errors printed, aborting program",
    "Too many errors printed, suppressing errors for current file",
    "Too many warnings printed, suppressing warnings for current file",
#if defined(LINKER) && VERSION >= VERSION_GC_2_7
    "No %s mapping matches '%s' (unrecognized file contents or filename extension); %s",
#else
    "No %s mapping matches '%s' (unrecognized file contents or filename extension); treating as source text",
#endif
    "No plugin or target matches the file '%s', ignoring",
    "File '%s' cannot be handled by this tool, ignoring",
    "File '%s' does not match the active target",
    "Adding%s:\t'%s'",
    "Creating new overlay '%s' in group '%s'",
    "Creating new overlay group '%s' at addr %08X:%08X",
    "File '%s' cannot be added to overlay group; define an overlay in the group first",
    "Cannot create virtual export file '%s' (from '-export name,...')",
    "Too many %s defined; at most %d%s is allowed",
    "Loading preference panel '%s'",
    "%s:",
    "\t%s%s",
    "Already defined %s search path; '%s' added after other paths",
    "License check failed: %s",
    "The plugin '%s' (version '%s') cannot be used:\n%s",
    "Plugin '%s' has already been registered",
    "Preferences for '%s' have already been registered",
    "Preferences for '%s' not found",
    "Some preferences needed by the plugin '%s' have not been registered",
    "Could not load file '%s'",
    "Could not find change current working directory to '%s'",
    "Out of memory",
    "The tool did not produce any output while %s the file '%s'",
    "The filename '%s' is invalid",
    "The %slinker for this target was not found",
    "%s\n%s (OS error %d)",
    "%s\n%s (error %d)",
    "Note:  '%s' did not generate any browse information \nfor '%s'; no browser output generated",
    "Source and specified output for the file '%s' are identical; no output will be generated",
    "More than one output filename specified for '%s'; ignoring '%s'",
    "Pref panel data for '%s' is out-of-date or invalid",
    "Changing primary user access path to '%s'",
#if defined(LINKER) && VERSION >= VERSION_GC_2_7
    "The linker does not expect duplicate files; ignoring repeated '%s'",
#endif
    "",
    NULL};

unsigned int fn_0040a7d0(void)
{
    return ClientGlue_AddResourceStrings("Command-line strings", 12000, command_line_strings);
}

Boolean invoke_if_requested(Boolean pre, char *argument)
{
    if (pre) {
        CLIO_ExchangeClearGlobal();
        return 1;
    }
    return 0;
}

Boolean latch_flag(Boolean pre, char *argument)
{
    if (pre) {
        clState.pluginDebug = 1;
        return 1;
    }
    return clState.pluginDebug;
}

#if VERSION >= VERSION_GC_1_3
/* (1.3: the option's own string setting (--stdout), not the license path) */
char *data_005e656c;
#endif

Boolean is_enabled_or_global_nonzero(Boolean pre, char *argument)
{
    if (pre) {
        return 1;
    }
#if VERSION >= VERSION_GC_1_3
    return data_005e656c != NULL;
#else
    return license_path != NULL;
#endif
}

/* The options the driver takes before its parser sees the command line; the first is named at run time. */
#if VERSION >= VERSION_GC_1_3
Boolean fn_0040aaf0(Boolean pre, char *argument);
#endif
static struct DriverCommandLineOption driver_options[BY_VERSION(3, 4)] = {
    {"", NULL, invoke_if_requested},
#if VERSION >= VERSION_GC_1_3
    {"--use-license-file", &license_path, fn_0040aaf0},
    {"--plugin-debug", NULL, latch_flag},
    {"--stdout", &data_005e656c, is_enabled_or_global_nonzero},
#else
    {"--use-license-file", &license_path, is_enabled_or_global_nonzero},
    {"--plugin-debug", NULL, latch_flag},
#endif
};

void consume_driver_command_line_options(int *argc, char ***argv)
{
    int optionIndex;
    struct DriverCommandLineOption *option;

    if (*argc <= 1) {
        return;
    }
    do {
        optionIndex = 0;
        option = driver_options;
        do {
            if (strcmp(option->name, (*argv)[1]) == 0) {
                if (option->value != NULL) {
                    *option->value = (*argv)[2];
                    option->handler(1, *option->value);
                    (*argv)[1] = (*argv)[0];
                    --*argc;
                    (*argv)++;
                    (*argv)[1] = (*argv)[0];
                    --*argc;
                    (*argv)++;
                } else {
                    option->handler(1, NULL);
                    (*argv)[1] = (*argv)[0];
                    --*argc;
                    (*argv)++;
                }
                break;
            }
            ++optionIndex;
            ++option;
        } while (optionIndex < BY_VERSION(3, 4));
    } while ((*argc > 1) && (optionIndex < BY_VERSION(3, 4)));
}

void CLMain_AppendEnabledCommandLineOptions(int *first, char ***second)
{
    char enabled;
    int index;
    struct DriverCommandLineOption *entry;

    index = 0;
    entry = driver_options;
    do {
        enabled = entry->handler(0, NULL);
        if (enabled != '\0') {
            CLToolExec_AppendArgument(first, second, entry->name);
            if (entry->value != NULL) {
#if VERSION >= VERSION_GC_1_3
                CLToolExec_AppendArgument(first, second, *entry->value ? *entry->value : "");
#else
                CLToolExec_AppendArgument(first, second, *entry->value);
#endif
            }
        }
        index = 1 + index;
        entry = entry + 1;
    } while (index < BY_VERSION(3, 4));
}

static int did_init = 0;

#if VERSION >= VERSION_GC_1_3
int CLMain_Initialize(int argc, char **argv)
{
    static char secret[8];
    char buf[260];

    fn_00411980(&argc, &argv);
    memset(&clState, 0, 0x544);
    driver_options[0].name = secret;
    secret[7] = 0;
    secret[4] = 'b';
    secret[1] = '-';
    secret[5] = 'u';
    secret[2] = 'd';
    secret[0] = '-';
    secret[3] = 'e';
    secret[6] = 'g';
    consume_driver_command_line_options(&argc, &argv);
    clState.argc = argc;
    clState.argv = argv;

    CLProj_CopyStringBounded(buf, *argv, strlen(*argv), sizeof(buf));
    buf[sizeof(buf) - 1] = 0;
    if (strlen(buf) < 4 || CLIO_CompareStringsIgnoreCase(buf + strlen(buf) - 4, ".exe") != 0)
        CLProj_AppendString(buf, ".exe", sizeof(buf));

    CLIO_InitializeStreamBuffering();
    if (fn_0040a7d0() == 0)
        CLErrors_FatalError("Could not initialize resource strings");
    if (OS_IsFullPath(*argv) == 0)
        clState.programName = *argv;
    else
        clState.programName = OS_GetFileNamePtr(*argv);

    if (OS_FindProgram(buf, clState.programSpec.path.s) != 0)
        CLErrors_EmitDiagnostic(2, buf);
    fn_004098d0();
    if (fn_0040a7c0() == 0)
        CLErrors_FatalError("Could not initialize built-in plugins");
    CLProj_InitializeCWD(gProj);
    Resources_OpenResourceFile(&clState.programSpec);
    fn_00417750();
    did_init = 1;
    return 0;
}
#else
int CLMain_Initialize(int argc, char **argv)
{
    static char secret[8];
    char buf[260];

    OS_InitProgram(&argc, &argv);
    memset(&clState, 0, sizeof(clState));
    driver_options[0].name = secret;
    secret[7] = 0;
    secret[4] = 'b';
    secret[1] = '-';
    secret[5] = 'u';
    secret[2] = 'd';
    secret[0] = '-';
    secret[3] = 'e';
    secret[6] = 'g';
    consume_driver_command_line_options(&argc, &argv);
    clState.argc = argc;
    clState.argv = argv;

    CLProj_CopyStringBounded(buf, *argv, strlen(*argv), sizeof(buf));
    buf[sizeof(buf) - 1] = 0;
    if (strlen(buf) < 4 || CLIO_CompareStringsIgnoreCase(buf + strlen(buf) - 4, ".exe") != 0)
        CLProj_AppendString(buf, ".exe", sizeof(buf));

    CLIO_InitializeStreamBuffering();
    fn_004098d0();
    CLProj_InitializeCWD(gProj);
    if (fn_0040a7d0() == 0)
        CLErrors_FatalError("Could not initialize resource strings");
    if (fn_0040a7c0() == 0)
        CLErrors_FatalError("Could not initialize built-in plugins");
    if (OS_IsFullPath(*argv) == 0)
        clState.programName = *argv;
    else
        clState.programName = OS_GetFileNamePtr(*argv);

    if (OS_FindProgram(buf, clState.programSpec.path.s) != 0)
        CLErrors_EmitDiagnostic(2, buf);
    Resources_OpenResourceFile(&clState.programSpec);
    fn_00417750();
    did_init = 1;
    return 0;
}

#endif

unsigned int CLMain_FreePlugins(unsigned int result)
{
    if (did_init != 0) {
        CLPlugins_FreePlugins();
        CLLicenses_ReleaseLicenses();
        CLProj_FreeTargets(gProj);
        fn_00414e20();
        did_init = 0;
    }
    return result;
}

int parse_command_line(void)
{
    SInt32 recordIndex;
    PluginRequest *requests;
    CommandParseInfo info;
    Plugin *parser;
    struct CLTarget *env;
    SInt32 argumentIndex, fileIndex;

    if (CLPlugins_BuildPluginRequests(NULL, &plugin_request_count, &plugin_requests) == 0 ||
        CLPlugins_GetUniquePluginNames(NULL, &unique_plugin_name_count, &unique_plugin_names) == 0 ||
        (file_argument_sets = xcalloc(NULL, unique_plugin_name_count * 12)) == NULL ||
        (tool_argument_sets = xcalloc(NULL, plugin_request_count * 12)) == NULL)
        CLErrors_FatalError("Out of memory during init\n");

    env = default_target;
    requests = plugin_requests;
    parser =
        CLPlugins_SelectPluginByRequestsAndValues(NULL, clState.parserplugin, plugin_request_count, requests, env->cpu,
                                                  env->os, unique_plugin_name_count, unique_plugin_names);
    if (parser == NULL)
        CLErrors_FatalError("Could not find a command-line parser!");

    info.argc = clState.argc;
    info.argv = clState.argv;
    info.value2 = 0;
    env = default_target;
    if (!CLPluginRequests_ParseCommandLine(parser, env, &info, env->cpu, env->os, plugin_request_count, plugin_requests,
                                           unique_plugin_name_count, unique_plugin_names, tool_argument_sets,
                                           file_argument_sets, &input_name, &output_name
#if VERSION >= VERSION_GC_1_3
                                           , fn_00408f30()
#endif
                                           )) {
        UInt8 status;
        if (fn_004151f0())
            status = 2;
        else
            status = 1;
        return status;
    }

    if (clState.pluginDebug != 0) {
        for (fileIndex = 0; fileIndex < unique_plugin_name_count; fileIndex++) {
            CLIO_WriteFormattedText("Outgoing args for '%s':  ", unique_plugin_names[fileIndex]);
            for (argumentIndex = 0; argumentIndex < file_argument_sets[fileIndex].count; argumentIndex++)
                CLIO_WriteFormattedText("%s ", file_argument_sets[fileIndex].arguments[argumentIndex]);
            CLIO_WriteFormattedText("\r\n");
        }
        recordIndex = 0;
        for (; recordIndex < plugin_request_count; recordIndex++) {
            SInt32 secondCode = plugin_requests[recordIndex].secondCode;
            if (secondCode == 0)
                secondCode = '----';
            CLIO_WriteFormattedText(
                "Outgoing args for '%c%c%c%c/%c%c%c%c':  ", (plugin_requests[recordIndex].tag.kind & 0xFF000000) >> 24,
                (plugin_requests[recordIndex].tag.kind & 0xFF0000) >> 16,
                (plugin_requests[recordIndex].tag.kind & 0xFF00) >> 8, plugin_requests[recordIndex].tag.kind & 0xFF,
                (secondCode & 0xFF000000) >> 24, (secondCode & 0xFF0000) >> 16, (secondCode & 0xFF00) >> 8,
                secondCode & 0xFF);
            for (argumentIndex = 1; argumentIndex < tool_argument_sets[recordIndex].count; argumentIndex++)
                CLIO_WriteFormattedText("%s ", tool_argument_sets[recordIndex].arguments[argumentIndex]);
            CLIO_WriteFormattedText("\r\n");
        }
    }
    return 0;
}

int delete_file_and_make_path_spec(void)
{
    DWORD error;
#if VERSION >= VERSION_GC_1_3
    char *firstName;
    OSSpec *resolvedName;
    char *secondName;
#endif

    data_0054bf48(NULL);
#if VERSION >= VERSION_GC_1_3
    if (optsCompiler.outMakefile[0] != '\0') {
        error = OS_MakeFileSpec(firstName = optsCompiler.outMakefile, resolvedName = &clState.makefileSpec);
        if (error > 0) {
            CLErrors_ReportOSError(8, error, firstName);
            return 1;
        }
        OS_Delete(resolvedName);
    }
    if (optsCompiler.sbmPath[0] != '\0') {
        error = OS_MakePathSpec(NULL, secondName = optsCompiler.sbmPath, &clState.sbmPathSpec);
        if (error > 0) {
            CLErrors_ReportOSError(0x17, error, secondName);
            return 1;
        }
#else
    if (optsCompiler.outMakefile[0] != '\0') {
        error = OS_MakeFileSpec(optsCompiler.outMakefile, &clState.makefileSpec);
        if (error > 0) {
            CLErrors_ReportOSError(8, error, optsCompiler.outMakefile);
            return 1;
        }
        OS_Delete(&clState.makefileSpec);
    }
    if (optsCompiler.sbmPath[0] != '\0') {
        error = OS_MakePathSpec(NULL, optsCompiler.sbmPath, &clState.sbmPathSpec);
        if (error > 0) {
            CLErrors_ReportOSError(0x17, error, optsCompiler.sbmPath);
            return 1;
        }
#endif
    } else {
        OS_GetCWD(&clState.sbmPathSpec);
    }
    return 0;
}

SInt32 fn_0040aed0(void)
{
    SInt32 start;
    SInt32 end;
    SInt32 r;

    if (+optsCmdLine.state == 0 || +optsCmdLine.state == 1)
        return 0;
    start = CLFileOps_GetScaledTicks();
    r = CLFileOps_CompileProject();
    if (r != 0)
        return r;
    if (optsCmdLine.state == 3) {
        r = CLFileOps_LinkProject();
        if (r != 0)
            return r;
    }
#if VERSION >= VERSION_GC_1_3
    if (!CLToolExec_DeleteTemporaryOutputs())
        return 1;
#endif
    end = CLFileOps_GetScaledTicks();
    if (optsCmdLine.timeWorking != 0)
        CLErrors_ForwardMessage(0x18, (end - start) / 60.0, "resolve", "", "project", "");
    return 0;
}

#if defined(LINKER) && VERSION >= VERSION_GC_2_7
int fn_0040af70(PCmdLine *message, PCmdLine *result)
{
    /* 2.x: the old versions are upgraded one step at a time, and warned about once and only with -plugindebug */
    static Boolean warned;
    short version = message->version;

    *result = *message;
    if (clState.pluginDebug && version < 0x1002 && !warned) {
        CLErrors_ForwardMessageArguments(0x68, "CmdLine Panel");
        warned = 1;
    }
    if (version == 0x1000) {
        result->stderr2stdout = 1;
        version++;
    }
    if (version == 0x1001) {
        result->noCmdLineWarnings = 0;
        version++;
    }
    if (version < 0x1000 || version > 0x1002) {
        CLErrors_EmitDiagnostic(0x68, "CmdLine Panel");
        return 0;
    }
    result->version = 0x1002;
    return 1;
}
#else
int fn_0040af70(PCmdLine *message, PCmdLine *result)
{
    if (message->version == 0x1002) {
        *result = *message;
        return 1;
    }
    if (message->version == 0x1001) {
        CLErrors_ForwardMessageArguments(0x68, "CmdLine Panel");
        *result = *message;
        result->noCmdLineWarnings = 0;
        return 1;
    }
    if (message->version == 0x1000) {
        CLErrors_ForwardMessageArguments(0x68, "CmdLine Panel");
        *result = *message;
        result->stderr2stdout = 1;
        return 1;
    }
    CLErrors_EmitDiagnostic(0x68, "CmdLine Panel");
    return 0;
}
#endif

unsigned int copy_environment_code_record(PCmdLineEnvir *record, PCmdLineEnvir *result)
{
#if VERSION >= VERSION_GC_1_3
    short version = record->version;

    *result = *record;
    if ((clState.pluginDebug && version < 0x1000) || version > 0x1000) {
        CLErrors_EmitDiagnostic(104U, "CmdLine Environment");
        return 0U;
    }
    result->version = 0x1000;
    return 1U;
#else
    if (record->version == 4096U) {
        *result = *record;
        return 1U;
    }
    CLErrors_EmitDiagnostic(104U, "CmdLine Environment");
    return 0U;
#endif
}

#if VERSION >= VERSION_GC_1_3
#if defined(LINKER) && VERSION >= VERSION_GC_2_7
/* (the 2.x linker has them in this unit's data) */
UInt8 data_005d89e5;
UInt8 data_005d89e6;
#else
extern UInt8 data_005d89e5;
extern UInt8 data_005d89e6;
#endif

/* bring the compiler panel settings of an older version up to date, warning once about it */
int convert_command_line_panel_settings(PCmdLineCompiler *source, PCmdLineCompiler *destination)
{
    short version = source->version;

    *destination = *source;
    if (clState.pluginDebug && version < 0x1005 && !data_005d89e5) {
        CLErrors_ForwardMessageArguments(0x68, "CmdLine Compiler Panel");
        data_005d89e5 = 1;
    }
    if (version == 0x1001) {
        destination->ignoreMissingFiles = 0;
        destination->printHeaderNames = 0;
        version++;
    }
    if (version == 0x1002) {
        destination->sbmState = 0;
        destination->sbmPath[0] = 0;
        version++;
    }
    if (version == 0x1003) {
        destination->canonicalIncludes = 0;
        version++;
    }
    if (version == 0x1004) {
        destination->keepObjects = 0;
        version++;
    }
    if (version <= 0x1000 || version > 0x1005) {
        CLErrors_EmitDiagnostic(0x68, "CmdLine Compiler Panel");
        return 0;
    }
    destination->version = 0x1005;
    return 1;
}

/* bring the linker panel settings of an older version up to date, warning once about it */
int convert_linker_panel_settings(PCmdLineLinker *input, PCmdLineLinker *output)
{
    short version = input->version;

    *output = *input;
    if (clState.pluginDebug && version < 0x1002 && !data_005d89e6) {
        CLErrors_ForwardMessageArguments(0x68, "CmdLine Linker Panel");
        data_005d89e6 = 1;
    }
    if (version == 0x1000) {
        output->callPreLinker = 1;
        output->callPostLinker = 1;
        output->keepLinkerOutput = 1;
        version++;
    }
    if (version == 0x1001) {
        output->callLinker = 1;
        version++;
    }
    if (version < 0x1000 || version > 0x1002) {
        CLErrors_EmitDiagnostic(0x68, "CmdLine Linker Panel");
        return 0;
    }
    output->version = 0x1002;
    return 1;
}
#else
int convert_command_line_panel_settings(PCmdLineCompiler *source, PCmdLineCompiler *destination)
{
    if (source->version == 0x1004) {
        *destination = *source;
        return 1;
    }
    if (source->version == 0x1003) {
        if (clState.pluginDebug)
            CLErrors_ForwardMessageArguments(0x68, "CmdLine Panel");
        *destination = *source;
        destination->canonicalIncludes = 1;
        return 1;
    }
    if (source->version == 0x1002) {
        if (clState.pluginDebug)
            CLErrors_ForwardMessageArguments(0x68, "CmdLine Panel");
        *destination = *source;
        destination->sbmState = 0;
        destination->sbmPath[0] = 0;
        return 1;
    }
    if (source->version == 0x1001) {
        if (clState.pluginDebug)
            CLErrors_ForwardMessageArguments(0x68, "CmdLine Panel");
        *destination = *source;
        destination->ignoreMissingFiles = 0;
        destination->printHeaderNames = 0;
        destination->sbmState = 0;
        destination->sbmPath[0] = 0;
        return 1;
    }
    if (source->version == 0x1000) {
        CLErrors_ForwardMessageArguments(0x68, "CmdLine Panel");
        *destination = *source;
        destination->forcePrecompile = 0;
        destination->ignoreMissingFiles = 0;
        destination->printHeaderNames = 0;
        destination->sbmState = 0;
        destination->sbmPath[0] = 0;
        return 1;
    }
    CLErrors_EmitDiagnostic(0x68, "CmdLine Compiler Panel");
    return 0;
}

int convert_linker_panel_settings(PCmdLineLinker *input, PCmdLineLinker *output)
{
    if (input->version == 0x1002) {
        *output = *input;
        return 1;
    }
    if (input->version == 0x1001) {
        *output = *input;
        output->callLinker = 1;
        return 1;
    }
    if (input->version == 0x1000) {
        *output = *input;
        output->callPreLinker = 1;
        output->callPostLinker = 1;
        output->keepLinkerOutput = 1;
        output->callLinker = 1;
        return 1;
    }
    CLErrors_EmitDiagnostic(0x68, "CmdLine Linker Panel");
    return 0;
}

#endif

unsigned int check_cmdline_entries(char *context)
{
    NameTableEntry *entry;
    StorageHandle *handle;
    if (!context || BY_VERSION(!ClientGlue_CompareLowercaseStrings(context, "CmdLine Panel"), CLIO_STRINGS_EQUAL(context, "CmdLine Panel"))) {
        entry = CLPrefs_FindNameTableEntry("CmdLine Panel");
        if (entry && (handle = CLPrefs_CopyDestinationToTemporary(entry)) != NULL) {
            if (context && !((int (*)(void *, void *))fn_0040af70)((PCmdLine *)handle->data, &optsCmdLine))
                return 0;
        } else {
            CLErrors_EmitDiagnostic(91, "CmdLine Panel");
            return 0;
        }
    }
    if (!context || BY_VERSION(!ClientGlue_CompareLowercaseStrings(context, "CmdLine Environment"), CLIO_STRINGS_EQUAL(context, "CmdLine Environment"))) {
        entry = CLPrefs_FindNameTableEntry("CmdLine Environment");
        if (entry && (handle = CLPrefs_CopyDestinationToTemporary(entry)) != NULL) {
            if (context &&
                !((int (*)(void *, void *))copy_environment_code_record)((PCmdLineEnvir *)handle->data, &optsEnvir))
                return 0;
        } else {
            CLErrors_EmitDiagnostic(91, "CmdLine Environment");
            return 0;
        }
    }
    if (!context || BY_VERSION(!ClientGlue_CompareLowercaseStrings(context, "CmdLine Compiler Panel"), CLIO_STRINGS_EQUAL(context, "CmdLine Compiler Panel"))) {
        entry = CLPrefs_FindNameTableEntry("CmdLine Compiler Panel");
        if (entry && (handle = CLPrefs_CopyDestinationToTemporary(entry)) != NULL) {
            if (context && !((int (*)(void *, void *))convert_command_line_panel_settings)(
                               (PCmdLineCompiler *)handle->data, &optsCompiler))
                return 0;
        } else {
            CLErrors_EmitDiagnostic(91, "CmdLine Compiler Panel");
            return 0;
        }
    }
    if (!context || BY_VERSION(!ClientGlue_CompareLowercaseStrings(context, "CmdLine Linker Panel"), CLIO_STRINGS_EQUAL(context, "CmdLine Linker Panel"))) {
        entry = CLPrefs_FindNameTableEntry("CmdLine Linker Panel");
        if (entry && (handle = CLPrefs_CopyDestinationToTemporary(entry)) != NULL) {
            if (context &&
                !((int (*)(void *, void *))convert_linker_panel_settings)((PCmdLineLinker *)handle->data, &optsLinker))
                return 0;
        } else {
            CLErrors_EmitDiagnostic(91, "CmdLine Linker Panel");
            return 0;
        }
    }
    return 1;
}

unsigned int create_cmdline_data_blocks(void)
{
    Boolean result;
    unsigned int combined;
    unsigned int enabled;
    unsigned int prerequisites;
    NameTableEntry *entry;

    data_0054bf48 = check_cmdline_entries;
    entry = create_data_block("CmdLine Environment", &optsEnvir, 8U);
    result = CLPrefs_AddPrefPanel(entry);
    combined = result;
    enabled = 0U;
    prerequisites = 0U;
    entry = create_data_block("CmdLine Panel", NULL, 28U);
    if (CLPrefs_AddPrefPanel(entry) != 0) {
        entry = create_data_block("CmdLine Compiler Panel", NULL, 722U);
        if (CLPrefs_AddPrefPanel(entry) != 0)
            prerequisites = 1U;
    }
    if (prerequisites != 0) {
        entry = create_data_block("CmdLine Linker Panel", 0U, 6U);
        if (CLPrefs_AddPrefPanel(entry) != 0)
            enabled = 1U;
    }
    combined |= enabled;
    return combined;
}

int create_default_target(void)
{
    default_target = CLTarg_CreateTarget("default", clState.cpu, clState.os, clState.language);
    CLTarg_AppendEntry(&gProj->targets, default_target);

    CLPlugins_DispatchArgumentToPlugins(NULL, 0, default_target->cpu, default_target->os);

    default_target->linker = CLPlugins_FindLinkerPlugin(NULL, default_target->cpu, default_target->os);
    if (default_target->linker) {
        default_target->linkerFlags = CLPlugins_GetPluginDesc(default_target->linker)->flags;
    } else {
        if (CLPlugins_FindLinkerPlugin(NULL, 0x2a2a2a2a, 0x2a2a2a2a))
            CLErrors_FatalError("A linker is compiled in, but does not match the static target!\n");
        default_target->linkerFlags = 0;
    }

    default_target->preLinker = CLPlugins_FindLinkPluginForTarget(NULL, default_target->cpu, default_target->os);
    if (default_target->preLinker) {
        default_target->preLinkerFlags = CLPlugins_GetPluginDesc(default_target->preLinker)->flags;
    } else {
        if (CLPlugins_FindLinkPluginForTarget(NULL, 0x2a2a2a2a, 0x2a2a2a2a))
            CLErrors_FatalError("A pre-linker is compiled in, but does not match the static target!\n");
        default_target->preLinkerFlags = 0;
    }

    default_target->postLinker = CLPlugins_FindMatchingLinkPlugin(NULL, default_target->cpu, default_target->os);
    if (default_target->postLinker) {
        default_target->postLinkerFlags = CLPlugins_GetPluginDesc(default_target->postLinker)->flags;
    } else {
        if (CLPlugins_FindMatchingLinkPlugin(NULL, 0x2a2a2a2a, 0x2a2a2a2a))
            CLErrors_FatalError("A post-linker is compiled in, but does not match the static target!\n");
        default_target->postLinkerFlags = 0;
    }

    if (clState.plugintype == 0x4c696e6b && default_target->preLinker == NULL && default_target->linker == NULL &&
        default_target->postLinker == NULL)
        CLErrors_FatalError("The linker plugin was not found!");

    default_target->linkage = 0;
    return 1;
}

unsigned int CLMain_InitializeAndParseCommandLine(void)
{
    unsigned int result;

    result = setjmp(driver_jmp_buf);
    if (!result) {
        if (!create_cmdline_data_blocks())
            CLErrors_FatalError("Could not initialize preferences");
        create_default_target();

        (result = parse_command_line()) || ((result = delete_file_and_make_path_spec()) || (result = fn_0040aed0()));
    } else {
        result = 1U;
    }
    return result;
}

#pragma auto_inline reset

#if VERSION >= VERSION_GC_1_3
/* (1.3: the --use-license-file option's check, of the license path) */
Boolean fn_0040aaf0(Boolean pre, char *argument)
{
    if (pre)
        return 1;
    return license_path != NULL;
}
#endif
