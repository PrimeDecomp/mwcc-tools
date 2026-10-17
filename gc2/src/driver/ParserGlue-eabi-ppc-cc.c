#include "driver/ParserGlue-eabi-ppc-cc.h"
#include "driver/Arguments.h"
#include "driver/IO.h"
#include "driver/OptimizerHelpers.h"
#include "driver/option_system.h"
#include "driver/ParserErrors.h"
#include "driver/ParserFace.h"
#include "driver/ParserHelpers.h"
#include "driver/ParserHelpers-cc.h"
#include "driver/pref_structs.h"
#include "driver/Projects.h"
#include "driver/StdTargetWarningHelpers-cc.h"
#include "driver/StringUtils.h"
#include "driver/Targets.h"
#include "driver/ToolHelpers.h"
#include "driver/ToolHelpers-cc.h"
#include "driver/WarningHelpers.h"

// The command line compiler's preference panels, option tables and parse hooks (Pro7's
// ParserGlue-mach-ppc-cc.c and ParserGlue-mach-ppc-common.h; the tables are generated from the
// original's .data by tools/gen_options.py)

PCmdLine pCmdLine = {0x1002, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
PCmdLineCompiler pCmdLineCompiler = {0x1005};
PCmdLineLinker pCmdLineLinker = {0x1000};
PBackEnd pBackEnd = {0xb, 2, 0, 1, 0, 0, 0, 0, {0}, 0, 0, 0, 1, 0, 0x14};
// clang-format off
PLinker pLinker = {5,          0,         0,          0,          0,          0,      0,     0, 0, 0, 0, 0, 0, 0, 0,
                   0x10000,    0x3000,    0,          0,          0x3dfff0,   0x2800000, 0xffe00000, 0x1a, 1, 0,
                   "__start"};
// clang-format on
PDisassembler pDisassembler = {3, 0, 0, 0, 0, 1, 1, 0, 1, 0, 1, 1};
PProject pProject = {7, {0}, 0x400, 0x40, 1, 8, 8, 1};
PCLTExtras pCLTExtras = {1};

static Boolean useDefaultIncludes = TRUE;
static Boolean useDefaultLibraries = TRUE;
// (a COMMON Pro7's Mach-O tool lacks, as it lacks the option; the debugging options set it)
Boolean symFullPath;

#include "Opts-eabi-ppc-cc/OptsCmdLine.opt"
#include "Opts-eabi-ppc-cc/OptsCmdLineCompiler.opt"
#include "Opts-eabi-ppc-cc/OptsCmdLineLinker.opt"
#include "Opts-eabi-ppc-cc/OptsFrontEndC.opt"
#include "Opts-eabi-ppc-cc/OptsDebugging.opt"
#include "Opts-eabi-ppc-cc/OptsOptimizer.opt"
#include "Opts-eabi-ppc-cc/OptsWarningC.opt"
#include "Opts-eabi-ppc-cc/OptsBackEnd.opt"
#include "Opts-eabi-ppc-cc/OptsProject.opt"
#include "Opts-eabi-ppc-cc/OptsLinker.opt"
#include "Opts-eabi-ppc-cc/OptsLinkerAddresses.opt"
#include "Opts-eabi-ppc-cc/OptsDisassembler.opt"

// clang-format off
char* prefs[] = {"PPC EABI CodeGen",       "PPC EABI Linker", "PPC EABI Disassembler",  "PPC EABI Project",
                 "EPPC Global Optimizer",  "C/C++ Compiler",  "C/C++ Warnings",         "CmdLine Panel",
                 "CmdLine Compiler Panel", "CmdLine Linker Panel", "CmdLine Extras EPPC"};
// clang-format on

// clang-format off
OptionList* optLists[] = {&optlstCmdLine,  &optlstCmdLineCompiler, &optlstFrontEndC, &optlstOptimizer,
                          &optlstBackEnd,  &optlstDebugging,       &optlstWarningC,  &optlstProject,
                          &optlstCmdLineLinker, &optlstLinker,     &optlstLinkerAddresses, &optlstDisassembler};
// clang-format on

// clang-format off
static PrefDataPanel stPrefPanels[] = {
    {"CmdLine Panel", &pCmdLine, sizeof(pCmdLine)},
    {"CmdLine Compiler Panel", &pCmdLineCompiler, sizeof(pCmdLineCompiler)},
    {"CmdLine Linker Panel", &pCmdLineLinker, sizeof(pCmdLineLinker)},
    {"C/C++ Compiler", &pFrontEndC, sizeof(pFrontEndC)},
    {"C/C++ Warnings", &pWarningC, sizeof(pWarningC)},
    {"EPPC Global Optimizer", &pGlobalOptimizer, sizeof(pGlobalOptimizer)},
    {"PPC EABI CodeGen", &pBackEnd, sizeof(pBackEnd)},
    {"PPC EABI Linker", &pLinker, sizeof(pLinker)},
    {"PPC EABI Disassembler", &pDisassembler, sizeof(pDisassembler)},
    {"PPC EABI Project", &pProject, sizeof(pProject)},
    {"CmdLine Extras EPPC", &pCLTExtras, sizeof(pCLTExtras)}
};
// clang-format on

PFrontEndC pFrontEndC = {0xc, 0, 0, 0, 0, 0, 0, 0, "", 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1};
PWarningC pWarningC = {4};
PGlobalOptimizer pGlobalOptimizer = {1, 0, 1};

static int PreParse(void) {
    // (2.6: the linker's -sreceol default, where Pro7 resets schedule_ppc_default)
    pLinker.sreceol = TRUE;
    setLinkerOutputFilename = FALSE;
    linkerOutputFilename[0] = 0;
    definesHandle = NULL;
    useDefaultIncludes = TRUE;
    return 1;
}

static int MidParse(void) {
    const char* match;
    const char* env;
    // (2.6 compiles and links by default; Pro7's Mach-O tool only compiles)
    if (!pCmdLine.state) {
        pCmdLine.state = OptsCmdLineState_CompileAndLink;
    }
    if (parseopts.possibleFiles > 0 && useDefaultIncludes) {
        if ((env = GetEnvVar("MWCEABIPPCIncludes\0MWCIncludes\0", 1, &match))) {
            if (!AddAccessPathList(env, ';', ':', 1, (char*)match, 1, -1, 0)) {
                return 0;
            }
        }
    }
    return 1;
}

static int PostParse(void) {
    if (!SetupPragmas(irPragmas) || !SetupPragmas(warningPragmas)) {
        return 0;
    }
    if (definesHandle) {
        static const char* name = "(command-line defines)";
        if (pCmdLine.verbose) {
            ShowTextHandle(name, definesHandle);
        }
        AddVirtualFile(name, &definesHandle);
        c2pstrcpy(pFrontEndC.oldprefixname, name);
    } else {
        pFrontEndC.oldprefixname[0] = 0;
    }
    FinishCompilerTool();
    if (!ValidateToolState(1)) {
        return 0;
    }
    // (2.6)
    if (pBackEnd.use_lmw_stmw && !pProject.endian) {
        CLPReport(
            CLPStr28_WarningText,
            "'-use_lmw_stmw on' or '-opt functions' only applies to big-endian machines"
        );
    }
    if (linkerOutputFilename[0]) {
        Arg_AddToToolArgs(&linkargs, ATK_ARG_END, NULL);
        Arg_AddToToolArgs(&linkargs, ATK_ARG, "-o");
        Arg_AddToToolArgs(&linkargs, ATK_ARG_END, NULL);
        Arg_AddToToolArgs(&linkargs, ATK_ARG, linkerOutputFilename);
        Arg_AddToToolArgs(&linkargs, ATK_ARG_END, NULL);
    }
    // (2.6)
    pLinker.debugInfo = pCmdLine.debugInfo;
    pLinker.symfullpath = symFullPath;
    return 1;
}

// clang-format off
static ParserTool parser = {'Comp', 'c++ ', 'ePPC', 'EABI', 11, prefs, "Metrowerks C/C++ Compiler for Embedded PowerPC",
                            "1998-2000", 12, optLists, 11, stPrefPanels, PreParse, MidParse, PostParse};
// clang-format on

int RegisterStaticParserToolInfo(void) {
    return SetParserToolInfo(&parser);
}
