/* The linker's command-line parser glue: its preference panels, the options that set them (the tables below are
   written by tools/gen_options.py from the original's), and the tool they describe with its three parse steps. */

#include "compiler/common.h"
#include "driver/Help.h"
#include "driver/Option.h"
#include "driver/Parameter.h"
#include "driver/ParserErrors.h"
#include "driver/ParserFace.h"
#include "driver/ParserHelpers.h"
#include "driver/PrefPanels.h"
#include "driver/Targets.h"
#include "driver/ToolHelpers-cc.h"
#include "driver/ToolHelpers.h"
#include "driver/WarningHelpers.h"
#include "string.h"

extern int ParserHelpers_AddUnixLibraryFile(char *option, char *extensions, char *name);
extern int ParserHelpers_AddFileList(char *list, char separator, char alternateSeparator, int mode, char *name,
                                     SInt32 position);
extern int fn_0040d952(char *name, char *output, unsigned int maxLength, Boolean toPascal, Boolean addExtension,
                       int key, char *defaultName, ...);
extern char output_path;
extern int data_00587e10;

/* The project kinds of '-application', '-library' and '-partial' (pProject.type). */
enum { PROJECT_APPLICATION, PROJECT_LIBRARY, PROJECT_PARTIAL };

/* Whether '-[no]stdlib' left the system libraries in (%MWLibraries% and %MWLibraryFiles%). */
char useDefaultLibraries = 1;
/* Whether '-main' named the entry point, and '-fullpaths'. */
Boolean mainNameGiven;
Boolean useFullPaths;

/* The CodeGen panel: the linker keeps it whole without reading it (a PBackEnd and the processor's name). */
unsigned char pCodeGen[44] = {0x0b, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                              0x00, 0x00, 0x01, 0x00, 0x14};

int fn_004055c0(int unused1, char *kind, int unused2, int flags);

PCmdLine pCmdLine = {0x1002, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x2};
PCmdLineCompiler pCmdLineCompiler = {0x1005};
PCmdLineLinker pCmdLineLinker = {0x1000};
PLinker pLinker = {0x5, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x10000, 0x3000, 0x0, 0x0, 0x3dfff0, 0x2800000, 0xffe00000, 0x1a, 0x1, 0x0, "__start"};
PDisassembler pDisassembler = {0x3, 0x0, 0x0, 0x0, 0x0, 0x1, 0x1, 0x0, 0x1, 0x0, 0x1, 0x1};
PProject pProject = {0x7, 0x0, {0}, 0x400, 0x40, 0x1, 0x0, 0x8, 0x8, 0x1};
PCLTExtras pCLTExtras = {0x1};


char lbl_00470BB4[4] = {0x44, 0x73, 0x00, 0x00};
char lbl_004728FC[4] = {0x41, 0x70, 0x00, 0x00};
char lbl_00472968[4] = {0x53, 0x74, 0x00, 0x00};
char lbl_004729D0[4] = {0x50, 0x72, 0x00, 0x00};
char lbl_00473C48[12] = {0x2d, 0x43, 0x77, 0x41, 0x77, 0x7c, 0x4e, 0x77, 0x00, 0x00, 0x00, 0x00};
char lbl_00473CA8[8] = {0x43, 0x77, 0x41, 0x77, 0x00, 0x00, 0x00, 0x00};
char lbl_00473CF8[4] = {0x43, 0x77, 0x00, 0x00};
char lbl_00473D54[4] = {0x57, 0x65, 0x00, 0x00};

extern MASK_T optlstCmdLine_000;
extern Option optlstCmdLine_001;
extern MASK_T optlstCmdLine_002;
extern Option optlstCmdLine_003;
extern MASK_T optlstCmdLine_004;
extern Option optlstCmdLine_005;
extern MASK_T optlstCmdLine_006;
extern Option optlstCmdLine_007;
extern MASK_T optlstCmdLine_008;
extern Option optlstCmdLine_009;
extern MASK_T optlstCmdLine_010;
extern Option optlstCmdLine_011;
extern MASK_T optlstCmdLine_012;
extern Option optlstCmdLine_013;
extern MASK_T optlstCmdLine_014;
extern Option optlstCmdLine_015;
extern MASK_T optlstCmdLine_016;
extern Option optlstCmdLine_017;
extern MASK_T optlstCmdLine_018;
extern Option optlstCmdLine_019;
extern MASK_T optlstCmdLine_020;
extern STRING_T optlstCmdLine_021;
extern Option optlstCmdLine_022;
extern STRING_T optlstCmdLine_023;
extern Option optlstCmdLine_024;
extern MASK_T optlstCmdLine_025;
extern STRING_T optlstCmdLine_026;
extern Option optlstCmdLine_027;
extern Option *optlstCmdLine_028[];
extern OptionList optlstCmdLine_029;
extern MASK_T optlstCmdLine_030;
extern SET_T optlstCmdLine_031;
extern SET_T optlstCmdLine_032;
extern Option optlstCmdLine_033;
extern SET_T optlstCmdLine_034;
extern GENERIC_T optlstCmdLine_035;
extern Option optlstCmdLine_036;
extern SET_T optlstCmdLine_037;
extern Option optlstCmdLine_038;
extern GENERIC_T optlstCmdLine_039;
extern SET_T optlstCmdLine_040;
extern Option optlstCmdLine_041;
extern GENERIC_T optlstCmdLine_042;
extern GENERIC_T optlstCmdLine_043;
extern Option optlstCmdLine_044;
extern SET_T optlstCmdLine_045;
extern Option optlstCmdLine_046;
extern SET_T optlstCmdLine_047;
extern Option optlstCmdLine_048;
extern SET_T optlstCmdLine_049;
extern Option optlstCmdLine_050;
extern NUM_T optlstCmdLine_051;
extern Option optlstCmdLine_052;
extern NUM_T optlstCmdLine_053;
extern Option optlstCmdLine_054;
extern SET_T optlstCmdLine_055;
extern Option optlstCmdLine_056;
extern SET_T optlstCmdLine_057;
extern Option optlstCmdLine_058;
extern SET_T optlstCmdLine_059;
extern Option optlstCmdLine_060;
extern SET_T optlstCmdLine_061;
extern Option optlstCmdLine_062;
extern SET_T optlstCmdLine_063;
extern Option optlstCmdLine_064;
extern Option *optlstCmdLine_065[];
extern OptionList optlstCmdLine_066;
extern OptionList optlstCmdLine_067;
extern Option optlstCmdLine_068;
extern GENERIC_T optlstCmdLine_069;
extern SET_T optlstCmdLine_070;
extern Option optlstCmdLine_071;
extern GENERIC_T optlstCmdLine_072;
extern Option optlstCmdLine_073;
extern Option *optlstCmdLine_074[];
extern OptionList optlstCmdLine;
extern Option *optlstCmdLineLinker_000[];
extern OptionList optlstCmdLineLinker_001;
extern Option *optlstCmdLineLinker_002[];
extern OptionList optlstCmdLineLinker_003;
extern GENERIC_T optlstCmdLineLinker_004;
extern SET_T optlstCmdLineLinker_005;
extern SET_T optlstCmdLineLinker_006;
extern Option optlstCmdLineLinker_007;
extern GENERIC_T optlstCmdLineLinker_008;
extern Option optlstCmdLineLinker_009;
extern GENERIC_T optlstCmdLineLinker_010;
extern Option optlstCmdLineLinker_011;
extern GENERIC_T optlstCmdLineLinker_012;
extern Option optlstCmdLineLinker_013;
extern SET_T optlstCmdLineLinker_014;
extern Option optlstCmdLineLinker_015;
extern SET_T optlstCmdLineLinker_016;
extern Option optlstCmdLineLinker_017;
extern SET_T optlstCmdLineLinker_018;
extern Option optlstCmdLineLinker_019;
extern GENERIC_T optlstCmdLineLinker_020;
extern MASK_T optlstCmdLineLinker_021;
extern SET_T optlstCmdLineLinker_022;
extern SET_T optlstCmdLineLinker_023;
extern Option optlstCmdLineLinker_024;
extern Option *optlstCmdLineLinker_025[];
extern OptionList optlstCmdLineLinker;
extern STRING_T optlstLinker_000;
extern SET_T optlstLinker_001;
extern Option optlstLinker_002;
extern SETSTRING_T optlstLinker_003;
extern SET_T optlstLinker_004;
extern Option optlstLinker_005;
extern FILEPATH_T optlstLinker_006;
extern IFARG_T optlstLinker_007;
extern SET_T optlstLinker_008;
extern Option optlstLinker_009;
extern SET_T optlstLinker_010;
extern SET_T optlstLinker_011;
extern Option optlstLinker_012;
extern GENERIC_T optlstLinker_013;
extern Option optlstLinker_014;
extern Option optlstLinker_015;
extern FILEPATH_T optlstLinker_016;
extern IFARG_T optlstLinker_017;
extern SET_T optlstLinker_018;
extern Option optlstLinker_019;
extern SET_T optlstLinker_020;
extern Option optlstLinker_021;
extern SET_T optlstLinker_022;
extern Option optlstLinker_023;
extern SET_T optlstLinker_024;
extern Option optlstLinker_025;
extern Option *optlstLinker_026[];
extern OptionList optlstLinker_027;
extern OptionList optlstLinker_028;
extern SET_T optlstLinker_029;
extern Option optlstLinker_030;
extern NUM_T optlstLinker_031;
extern SET_T optlstLinker_032;
extern Option optlstLinker_033;
extern Option *optlstLinker_034[];
extern OptionList optlstLinker;
extern GENERIC_T optlstLinkerAddresses_000;
extern SET_T optlstLinkerAddresses_001;
extern Option optlstLinkerAddresses_002;
extern NUM_T optlstLinkerAddresses_003;
extern SET_T optlstLinkerAddresses_004;
extern Option optlstLinkerAddresses_005;
extern NUM_T optlstLinkerAddresses_006;
extern SET_T optlstLinkerAddresses_007;
extern Option optlstLinkerAddresses_008;
extern NUM_T optlstLinkerAddresses_009;
extern SET_T optlstLinkerAddresses_010;
extern Option optlstLinkerAddresses_011;
extern NUM_T optlstLinkerAddresses_012;
extern SET_T optlstLinkerAddresses_013;
extern Option optlstLinkerAddresses_014;
extern NUM_T optlstLinkerAddresses_015;
extern SET_T optlstLinkerAddresses_016;
extern Option optlstLinkerAddresses_017;
extern NUM_T optlstLinkerAddresses_018;
extern SET_T optlstLinkerAddresses_019;
extern Option optlstLinkerAddresses_020;
extern NUM_T optlstLinkerAddresses_021;
extern SET_T optlstLinkerAddresses_022;
extern Option optlstLinkerAddresses_023;
extern NUM_T optlstLinkerAddresses_024;
extern Option optlstLinkerAddresses_025;
extern Option *optlstLinkerAddresses_026[];
extern OptionList optlstLinkerAddresses;
extern Option *optlstDisassembler_000[];
extern OptionList optlstDisassembler_001;
extern SET_T optlstDisassembler_002;
extern Option optlstDisassembler_003;
extern Option *optlstDisassembler_004[];
extern OptionList optlstDisassembler_005;
extern OptionList optlstDisassembler_006;
extern Option optlstDisassembler_007;
extern SET_T optlstDisassembler_008;
extern SET_T optlstDisassembler_009;
extern SET_T optlstDisassembler_010;
extern SET_T optlstDisassembler_011;
extern SET_T optlstDisassembler_012;
extern SET_T optlstDisassembler_013;
extern SET_T optlstDisassembler_014;
extern SET_T optlstDisassembler_015;
extern SET_T optlstDisassembler_016;
extern Option optlstDisassembler_017;
extern SET_T optlstDisassembler_018;
extern SET_T optlstDisassembler_019;
extern SET_T optlstDisassembler_020;
extern SET_T optlstDisassembler_021;
extern SET_T optlstDisassembler_022;
extern SET_T optlstDisassembler_023;
extern SET_T optlstDisassembler_024;
extern SET_T optlstDisassembler_025;
extern SET_T optlstDisassembler_026;
extern Option optlstDisassembler_027;
extern SET_T optlstDisassembler_028;
extern Option optlstDisassembler_029;
extern SET_T optlstDisassembler_030;
extern Option optlstDisassembler_031;
extern SET_T optlstDisassembler_032;
extern Option optlstDisassembler_033;
extern SET_T optlstDisassembler_034;
extern Option optlstDisassembler_035;
extern SET_T optlstDisassembler_036;
extern Option optlstDisassembler_037;
extern SET_T optlstDisassembler_038;
extern Option optlstDisassembler_039;
extern SET_T optlstDisassembler_040;
extern Option optlstDisassembler_041;
extern SET_T optlstDisassembler_042;
extern Option optlstDisassembler_043;
extern SET_T optlstDisassembler_044;
extern Option optlstDisassembler_045;
extern SET_T optlstDisassembler_046;
extern Option optlstDisassembler_047;
extern Option *optlstDisassembler_048[];
extern OptionList optlstDisassembler_049;
extern Option optlstDisassembler_050;
extern SET_T optlstDisassembler_051;
extern Option optlstDisassembler_052;
extern ONOFF_T optlstDisassembler_053;
extern Option optlstDisassembler_054;
extern Option *optlstDisassembler_055[];
extern OptionList optlstDisassembler;
extern SET_T optlstDebugging_000;
extern SET_T optlstDebugging_001;
extern Option optlstDebugging_002;
extern SET_T optlstDebugging_003;
extern Option optlstDebugging_004;
extern SET_T optlstDebugging_005;
extern Option optlstDebugging_006;
extern SET_T optlstDebugging_007;
extern SET_T optlstDebugging_008;
extern Option optlstDebugging_009;
extern Option *optlstDebugging_010[];
extern OptionList optlstDebugging_011;
extern Option optlstDebugging_012;
extern Option *optlstDebugging_013[];
extern OptionList optlstDebugging;
extern GENERIC_T optlstProject_000;
extern Option optlstProject_001;
extern GENERIC_T optlstProject_002;
extern Option optlstProject_003;
extern GENERIC_T optlstProject_004;
extern Option optlstProject_005;
extern SET_T optlstProject_006;
extern GENERIC_T optlstProject_007;
extern Option optlstProject_008;
extern SET_T optlstProject_009;
extern SET_T optlstProject_010;
extern GENERIC_T optlstProject_011;
extern Option optlstProject_012;
extern SET_T optlstProject_013;
extern Option optlstProject_014;
extern ONOFF_T optlstProject_015;
extern Option optlstProject_016;
extern SET_T optlstProject_017;
extern Option optlstProject_018;
extern SET_T optlstProject_019;
extern Option optlstProject_020;
extern SET_T optlstProject_021;
extern Option optlstProject_022;
extern SET_T optlstProject_023;
extern Option optlstProject_024;
extern SET_T optlstProject_025;
extern Option optlstProject_026;
extern SET_T optlstProject_027;
extern Option optlstProject_028;
extern SET_T optlstProject_029;
extern Option optlstProject_030;
extern SET_T optlstProject_031;
extern Option optlstProject_032;
extern SET_T optlstProject_033;
extern Option optlstProject_034;
extern SET_T optlstProject_035;
extern Option optlstProject_036;
extern SET_T optlstProject_037;
extern Option optlstProject_038;
extern SET_T optlstProject_039;
extern Option optlstProject_040;
extern SET_T optlstProject_041;
extern Option optlstProject_042;
extern SET_T optlstProject_043;
extern Option optlstProject_044;
extern SET_T optlstProject_045;
extern Option optlstProject_046;
extern SET_T optlstProject_047;
extern Option optlstProject_048;
extern SET_T optlstProject_049;
extern Option optlstProject_050;
extern SET_T optlstProject_051;
extern Option optlstProject_052;
extern SET_T optlstProject_053;
extern Option optlstProject_054;
extern SET_T optlstProject_055;
extern Option optlstProject_056;
extern SET_T optlstProject_057;
extern Option optlstProject_058;
extern SET_T optlstProject_059;
extern Option optlstProject_060;
extern SET_T optlstProject_061;
extern Option optlstProject_062;
extern SET_T optlstProject_063;
extern Option optlstProject_064;
extern SET_T optlstProject_065;
extern Option optlstProject_066;
extern SET_T optlstProject_067;
extern Option optlstProject_068;
extern SET_T optlstProject_069;
extern Option optlstProject_070;
extern Option *optlstProject_071[];
extern OptionList optlstProject_072;
extern OptionList optlstProject_073;
extern Option optlstProject_074;
extern SET_T optlstProject_075;
extern Option optlstProject_076;
extern SET_T optlstProject_077;
extern Option optlstProject_078;
extern SET_T optlstProject_079;
extern Option optlstProject_080;
extern SET_T optlstProject_081;
extern Option optlstProject_082;
extern Option *optlstProject_083[];
extern OptionList optlstProject_084;
extern OptionList optlstProject_085;
extern Option optlstProject_086;
extern NUM_T optlstProject_087;
extern Option optlstProject_088;
extern NUM_T optlstProject_089;
extern Option optlstProject_090;
extern NUM_T optlstProject_091;
extern Option optlstProject_092;
extern NUM_T optlstProject_093;
extern Option optlstProject_094;
extern NUM_T optlstProject_095;
extern Option optlstProject_096;
extern SET_T optlstProject_097;
extern Option optlstProject_098;
extern SET_T optlstProject_099;
extern Option optlstProject_100;
extern SET_T optlstProject_101;
extern Option optlstProject_102;
extern Option *optlstProject_103[];
extern OptionList optlstProject_104;
extern OptionList optlstProject_105;
extern Option optlstProject_106;
extern PARAM_T optlstProject_107;
extern Option optlstProject_108;
extern GENERIC_T optlstProject_109;
extern Option optlstProject_110;
extern GENERIC_T optlstProject_111;
extern Option optlstProject_112;
extern GENERIC_T optlstProject_113;
extern Option optlstProject_114;
extern Option *optlstProject_115[];
extern OptionList optlstProject_116;
extern Option optlstProject_117;
extern GENERIC_T optlstProject_118;
extern Option optlstProject_119;
extern GENERIC_T optlstProject_120;
extern Option optlstProject_121;
extern GENERIC_T optlstProject_122;
extern Option optlstProject_123;
extern Option *optlstProject_124[];
extern OptionList optlstProject;
extern Option *optlstWarning_000[];
extern OptionList optlstWarning_001;
extern Option *optlstWarning_002[];
extern OptionList optlstWarning_003;
extern GENERIC_T optlstWarning_004;
extern Option optlstWarning_005;
extern GENERIC_T optlstWarning_006;
extern Option optlstWarning_007;
extern GENERIC_T optlstWarning_008;
extern Option optlstWarning_009;
extern GENERIC_T optlstWarning_010;
extern Option optlstWarning_011;
extern GENERIC_T optlstWarning_012;
extern Option optlstWarning_013;
extern Option *optlstWarning_014[];
extern OptionList optlstWarning_015;
extern Option optlstWarning_016;
extern Option *optlstWarning_017[];
extern OptionList optlstWarning;

#ifdef NONMATCHING
/* (wasm: the GENERIC_T parse functions are called with 4 arguments, and a call through another type traps: those
   that take fewer are called through these, which pass them the first ones, as x86 did) */
#define WASM_GENERIC(fn) static int wasm_##fn(const char *opt, void *var, const char *pstr, int flags)
WASM_GENERIC(fn_0040cff6) { return fn_0040cff6(); }
WASM_GENERIC(fn_0040d002) { return fn_0040d002(); }
WASM_GENERIC(fn_0040d0eb) { return fn_0040d0eb(); }
WASM_GENERIC(print_command_line_warning_options) { return print_command_line_warning_options(); }
WASM_GENERIC(fn_0040c9e7) { return fn_0040c9e7((char *)opt, (int)var, (char *)pstr); }
WASM_GENERIC(fn_0040cd55) { return fn_0040cd55((char *)opt, (char *)var, (char *)pstr); }
WASM_GENERIC(set_output_path) { return set_output_path((char *)opt, (int)var, (char *)pstr); }
WASM_GENERIC(ParserHelpers_AddUnixLibraryFile)
{
    return ParserHelpers_AddUnixLibraryFile((char *)opt, (char *)var, (char *)pstr);
}
#undef WASM_GENERIC
#define fn_0040cff6 wasm_fn_0040cff6
#define fn_0040d002 wasm_fn_0040d002
#define fn_0040d0eb wasm_fn_0040d0eb
#define print_command_line_warning_options wasm_print_command_line_warning_options
#define fn_0040c9e7 wasm_fn_0040c9e7
#define fn_0040cd55 wasm_fn_0040cd55
#define set_output_path wasm_set_output_path
#define ParserHelpers_AddUnixLibraryFile wasm_ParserHelpers_AddUnixLibraryFile
#endif

MASK_T optlstCmdLine_000 = {9, 0x01, 0, 0, 4, 0x4000, 0x0, (void *)&data_00587ce0};
Option optlstCmdLine_001 = {"usage", 0x2041, (PARAM_T *)&optlstCmdLine_000, 0, 0, "show usage information"};
MASK_T optlstCmdLine_002 = {9, 0x01, 0, 0, 4, 0x100, 0x0, (void *)&data_00587ce0};
Option optlstCmdLine_003 = {"spaces", 0x102041, (PARAM_T *)&optlstCmdLine_002, 0, 0, "insert blank lines between options in printout"};
MASK_T optlstCmdLine_004 = {9, 0x01, 0, 0, 4, 0x6A, 0x0, (void *)&data_00587ce0};
Option optlstCmdLine_005 = {"all", 0x42041, (PARAM_T *)&optlstCmdLine_004, 0, &optlstCmdLineLinker_001, "show all standard options"};
MASK_T optlstCmdLine_006 = {9, 0x01, 0, 0, 4, 0x80, 0xE, (void *)&data_00587ce0};
Option optlstCmdLine_007 = {"normal", 0x142041, (PARAM_T *)&optlstCmdLine_006, 0, &optlstCmdLineLinker_001, "show only standard options"};
MASK_T optlstCmdLine_008 = {9, 0x01, 0, 0, 4, 0x4, 0x0, (void *)&data_00587ce0};
Option optlstCmdLine_009 = {"obsolete", 0x102041, (PARAM_T *)&optlstCmdLine_008, 0, 0, "show obsolete options"};
MASK_T optlstCmdLine_010 = {9, 0x01, 0, 0, 4, 0x2, 0x0, (void *)&data_00587ce0};
Option optlstCmdLine_011 = {"ignored", 0x102041, (PARAM_T *)&optlstCmdLine_010, 0, 0, "show ignored options"};
MASK_T optlstCmdLine_012 = {9, 0x01, 0, 0, 4, 0x8, 0x0, (void *)&data_00587ce0};
Option optlstCmdLine_013 = {"deprecated", 0x102041, (PARAM_T *)&optlstCmdLine_012, 0, 0, "show deprecated options"};
MASK_T optlstCmdLine_014 = {9, 0x01, 0, 0, 4, 0x20, 0x0, (void *)&data_00587ce0};
Option optlstCmdLine_015 = {"meaningless", 0x102041, (PARAM_T *)&optlstCmdLine_014, 0, 0, "show options meaningless for this target"};
MASK_T optlstCmdLine_016 = {9, 0x01, 0, 0, 4, 0x40, 0x0, (void *)&data_00587ce0};
Option optlstCmdLine_017 = {"compatible", 0x102041, (PARAM_T *)&optlstCmdLine_016, 0, 0, "show compatibility options"};
MASK_T optlstCmdLine_018 = {9, 0x01, 0, 0, 4, 0x10, 0x0, (void *)&data_00587ce0};
Option optlstCmdLine_019 = {"secret", 0x103041, (PARAM_T *)&optlstCmdLine_018, 0, 0, 0};
MASK_T optlstCmdLine_020 = {9, 0x01, 0, 0, 4, 0x1, 0x0, (void *)&data_00587ce0};
STRING_T optlstCmdLine_021 = {4, 0x00, "name", (PARAM_T *)&optlstCmdLine_020, 64, 0, (void *)&data_00587ca0};
Option optlstCmdLine_022 = {"opt|option", 0x42041, (PARAM_T *)&optlstCmdLine_021, 0, &optlstCmdLineLinker_003, "show help for a given option"};
STRING_T optlstCmdLine_023 = {4, 0x00, "keyword", 0, 64, 0, (void *)&data_00587ca0};
Option optlstCmdLine_024 = {"search", 0x42041, (PARAM_T *)&optlstCmdLine_023, 0, &optlstCmdLineLinker_003, "show help for an option whose name or help contains 'keyword' (case-sensitive)"};
MASK_T optlstCmdLine_025 = {9, 0x01, 0, 0, 4, 0x8000, 0x0, (void *)&data_00587ce0};
STRING_T optlstCmdLine_026 = {4, 0x00, "keyword", (PARAM_T *)&optlstCmdLine_025, 64, 0, (void *)&data_00587ca0};
Option optlstCmdLine_027 = {"group", 0x42041, (PARAM_T *)&optlstCmdLine_026, 0, &optlstCmdLineLinker_003, "show help for groups whose names contain 'keyword' (case-sensitive)"};
Option *optlstCmdLine_028[] = {
    &optlstCmdLine_001,
    &optlstCmdLine_003,
    &optlstCmdLine_005,
    &optlstCmdLine_007,
    &optlstCmdLine_009,
    &optlstCmdLine_011,
    &optlstCmdLine_013,
    &optlstCmdLine_015,
    &optlstCmdLine_017,
    &optlstCmdLine_019,
    &optlstCmdLine_022,
    &optlstCmdLine_024,
    &optlstCmdLine_027,
    0
};
OptionList optlstCmdLine_029 = {0, 0x200, optlstCmdLine_028};
MASK_T optlstCmdLine_030 = {9, 0x01, 0, 0, 4, 0x680, 0x0, (void *)&data_00587ce0};
SET_T optlstCmdLine_031 = {11, 0x01, 0, (PARAM_T *)&optlstCmdLine_030, 1, 0x1, (void *)&data_00587e1c};
SET_T optlstCmdLine_032 = {11, 0x01, 0, (PARAM_T *)&optlstCmdLine_031, 1, 0x1, (void *)&data_00587e1e};
Option optlstCmdLine_033 = {"help||-help", 0x3A041, (PARAM_T *)&optlstCmdLine_032, &optlstCmdLine_029, 0, "display help"};
SET_T optlstCmdLine_034 = {11, 0x01, 0, 0, 1, 0x1, (void *)&data_00587e1c};
GENERIC_T optlstCmdLine_035 = {13, 0x01, 0, (PARAM_T *)&optlstCmdLine_034, (void *)fn_0040cff6, 0, 0};
Option optlstCmdLine_036 = {"version", 0x41, (PARAM_T *)&optlstCmdLine_035, 0, 0, "show version, configuration, and build date"};
SET_T optlstCmdLine_037 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pCmdLine.timeWorking};
Option optlstCmdLine_038 = {"timing", 0x1C1, (PARAM_T *)&optlstCmdLine_037, 0, 0, "collect timing statistics"};
GENERIC_T optlstCmdLine_039 = {13, 0x01, 0, 0, (void *)fn_0040cff6, 0, 0};
SET_T optlstCmdLine_040 = {11, 0x01, 0, (PARAM_T *)&optlstCmdLine_039, 2, 0x1, (void *)&pCmdLine.verbose};
Option optlstCmdLine_041 = {"progress", 0x1C1, (PARAM_T *)&optlstCmdLine_040, 0, 0, "show progress and version"};
GENERIC_T optlstCmdLine_042 = {13, 0x01, 0, 0, (void *)fn_0040cff6, 0, 0};
GENERIC_T optlstCmdLine_043 = {13, 0x01, 0, (PARAM_T *)&optlstCmdLine_042, (void *)fn_0040d0eb, 0, 0};
Option optlstCmdLine_044 = {"v|verbose", 0x1C1, (PARAM_T *)&optlstCmdLine_043, 0, 0, "verbose information; cumulative; implies ~~progress"};
SET_T optlstCmdLine_045 = {11, 0x01, 0, 0, 1, 0x1, (void *)&data_00587e20};
Option optlstCmdLine_046 = {"search", 0x1C1, (PARAM_T *)&optlstCmdLine_045, 0, 0, "search access paths for source files specified on the command line; may specify object code and libraries as well; this option provides the IDE's 'access paths' functionality"};
SET_T optlstCmdLine_047 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pCmdLine.showLines};
Option optlstCmdLine_048 = {"lines", 0x11C1, (PARAM_T *)&optlstCmdLine_047, 0, 0, "show line count"};
SET_T optlstCmdLine_049 = {11, 0x01, 0, 0, 1, 0x0, (void *)&pCmdLine.noWrapOutput};
Option optlstCmdLine_050 = {"wraplines", 0x1001C1, (PARAM_T *)&optlstCmdLine_049, 0, 0, "word wrap messages"};
NUM_T optlstCmdLine_051 = {3, 0x00, "max", 0, 2, 0, 0x0, 0x0, (void *)&pCmdLine.maxErrors};
Option optlstCmdLine_052 = {"maxerrors", 0x1C0, (PARAM_T *)&optlstCmdLine_051, 0, 0, "specify maximum number of errors to print, zero means no maximum"};
NUM_T optlstCmdLine_053 = {3, 0x00, "max", 0, 2, 0, 0x0, 0x0, (void *)&pCmdLine.maxWarnings};
Option optlstCmdLine_054 = {"maxwarnings", 0x1C0, (PARAM_T *)&optlstCmdLine_053, 0, 0, "specify maximum number of warnings to print, zero means no maximum"};
SET_T optlstCmdLine_055 = {11, 0x01, 0, 0, 2, 0x2, (void *)&pCmdLine.msgStyle};
Option optlstCmdLine_056 = {"mpw", 0x401C1, (PARAM_T *)&optlstCmdLine_055, 0, &optlstCmdLine_067, "use MPW message style"};
SET_T optlstCmdLine_057 = {11, 0x01, 0, 0, 2, 0x0, (void *)&pCmdLine.msgStyle};
Option optlstCmdLine_058 = {"std", 0x401C1, (PARAM_T *)&optlstCmdLine_057, 0, &optlstCmdLine_067, "use standard message style"};
SET_T optlstCmdLine_059 = {11, 0x01, 0, 0, 2, 0x1, (void *)&pCmdLine.msgStyle};
Option optlstCmdLine_060 = {"gcc", 0x401C1, (PARAM_T *)&optlstCmdLine_059, 0, &optlstCmdLine_067, "use GCC-like message style"};
SET_T optlstCmdLine_061 = {11, 0x01, 0, 0, 2, 0x3, (void *)&pCmdLine.msgStyle};
Option optlstCmdLine_062 = {"IDE", 0x401C1, (PARAM_T *)&optlstCmdLine_061, 0, &optlstCmdLine_067, "use CW IDE-like message style"};
SET_T optlstCmdLine_063 = {11, 0x01, 0, 0, 2, 0x4, (void *)&pCmdLine.msgStyle};
Option optlstCmdLine_064 = {"parseable", 0x401C1, (PARAM_T *)&optlstCmdLine_063, 0, &optlstCmdLine_067, "use context-free machine-parseable message style"};
Option *optlstCmdLine_065[] = {
    &optlstCmdLine_056,
    &optlstCmdLine_058,
    &optlstCmdLine_060,
    &optlstCmdLine_062,
    &optlstCmdLine_064,
    0
};
OptionList optlstCmdLine_066 = {0, 0x701, optlstCmdLine_065};
OptionList optlstCmdLine_067 = {0, 0x701, optlstCmdLine_065};
Option optlstCmdLine_068 = {"msgstyle", 0x81C1, 0, &optlstCmdLine_066, 0, "set error/warning message style"};
GENERIC_T optlstCmdLine_069 = {13, 0x01, 0, 0, (void *)fn_0040d002, 0, 0};
SET_T optlstCmdLine_070 = {11, 0x01, 0, (PARAM_T *)&optlstCmdLine_069, 1, 0x0, (void *)&pCmdLine.stderr2stdout};
Option optlstCmdLine_071 = {"stderr", 0x1001C1, (PARAM_T *)&optlstCmdLine_070, 0, 0, "use separate stderr and stdout streams; if using ~~nostderr, stderr goes to stdout"};
GENERIC_T optlstCmdLine_072 = {13, 0x01, 0, 0, (void *)fn_0040cd55, 0, 0};
Option optlstCmdLine_073 = {"", 0x40, (PARAM_T *)&optlstCmdLine_072, 0, 0, 0};
Option *optlstCmdLine_074[] = {
    &optlstCmdLine_033,
    &optlstCmdLine_036,
    &optlstCmdLine_038,
    &optlstCmdLine_041,
    &optlstCmdLine_044,
    &optlstCmdLine_046,
    &optlstCmdLine_048,
    &optlstCmdLine_050,
    &optlstCmdLine_052,
    &optlstCmdLine_054,
    &optlstCmdLine_068,
    &optlstCmdLine_071,
    &optlstCmdLine_073,
    0
};
OptionList optlstCmdLine = {"General Command-Line Options\n\tPlease see '~~help usage' for details about the meaning of this help.\b", 0x700, optlstCmdLine_074};

Option *optlstCmdLineLinker_000[] = {
    &optlstCmdLine_005,
    &optlstCmdLine_007,
    0
};
OptionList optlstCmdLineLinker_001 = {0, 0x0, optlstCmdLineLinker_000};
Option *optlstCmdLineLinker_002[] = {
    &optlstCmdLine_022,
    &optlstCmdLine_024,
    &optlstCmdLine_027,
    0
};
OptionList optlstCmdLineLinker_003 = {0, 0x0, optlstCmdLineLinker_002};
GENERIC_T optlstCmdLineLinker_004 = {13, 0x01, 0, 0, (void *)parse_stage_settings, (void *)&lbl_00470BB4, 0};
SET_T optlstCmdLineLinker_005 = {11, 0x01, 0, (PARAM_T *)&optlstCmdLineLinker_004, 1, 0x0, (void *)&useDefaultLibraries};
SET_T optlstCmdLineLinker_006 = {11, 0x01, 0, (PARAM_T *)&optlstCmdLineLinker_005, 2, 0x2, (void *)&pCmdLine.state};
Option optlstCmdLineLinker_007 = {"dis|disassemble", 0xC1, (PARAM_T *)&optlstCmdLineLinker_006, 0, 0, "disassemble object code and do not link; implies '~~nostdlib'"};
GENERIC_T optlstCmdLineLinker_008 = {13, 0x00, "path", 0, (void *)fn_0040c9e7, 0, 0};
Option optlstCmdLineLinker_009 = {"L|l", 0xC7, (PARAM_T *)&optlstCmdLineLinker_008, 0, 0, "add library search path; default is to search current working directory and then system directories (see '~~defaults'); search paths have global scope over the command line and are searched in the order given"};
GENERIC_T optlstCmdLineLinker_010 = {13, 0x00, "path", 0, (void *)fn_0040c9e7, (void *)1, 0};
Option optlstCmdLineLinker_011 = {"lr", 0xC1, (PARAM_T *)&optlstCmdLineLinker_010, 0, 0, "like '~~l', but add recursive library search path"};
GENERIC_T optlstCmdLineLinker_012 = {13, 0x00, "file", 0, (void *)ParserHelpers_AddUnixLibraryFile, 0, 0};
Option optlstCmdLineLinker_013 = {"l", 0xC6, (PARAM_T *)&optlstCmdLineLinker_012, 0, 0, "add a library by searching access paths for file named lib<file>.<ext> where <ext> is a typical library extension; added before system libraries (see '~~defaults')"};
SET_T optlstCmdLineLinker_014 = {11, 0x01, 0, 0, 1, 0x1, (void *)&useDefaultLibraries};
Option optlstCmdLineLinker_015 = {"defaults", 0x100041, (PARAM_T *)&optlstCmdLineLinker_014, 0, 0, "same as ~~[no]stdlib"};
SET_T optlstCmdLineLinker_016 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pCmdLineCompiler.noFail};
Option optlstCmdLineLinker_017 = {"nofail", 0x40, (PARAM_T *)&optlstCmdLineLinker_016, 0, 0, "continue importing or disassembling after errors in earlier files"};
SET_T optlstCmdLineLinker_018 = {11, 0x01, 0, 0, 1, 0x1, (void *)&useDefaultLibraries};
Option optlstCmdLineLinker_019 = {"stdlib", 0x100041, (PARAM_T *)&optlstCmdLineLinker_018, 0, 0, "use system library access paths (specified by %MWLibraries%) and add system libraries (specified by %MWLibraryFiles%)"};
GENERIC_T optlstCmdLineLinker_020 = {13, 0x01, 0, 0, (void *)parse_stage_settings, (void *)&lbl_00470BB4, 0};
MASK_T optlstCmdLineLinker_021 = {9, 0x01, 0, (PARAM_T *)&optlstCmdLineLinker_020, 2, 0x4, 0x0, (void *)&pCmdLine.toDisk};
SET_T optlstCmdLineLinker_022 = {11, 0x01, 0, (PARAM_T *)&optlstCmdLineLinker_021, 1, 0x0, (void *)&useDefaultLibraries};
SET_T optlstCmdLineLinker_023 = {11, 0x01, 0, (PARAM_T *)&optlstCmdLineLinker_022, 2, 0x2, (void *)&pCmdLine.state};
Option optlstCmdLineLinker_024 = {"S", 0xC5, (PARAM_T *)&optlstCmdLineLinker_023, 0, 0, "disassemble and send output to file; do not link; implies '~~nostdlib'"};
Option *optlstCmdLineLinker_025[] = {
    &optlstCmdLineLinker_007,
    &optlstCmdLineLinker_009,
    &optlstCmdLineLinker_011,
    &optlstCmdLineLinker_013,
    &optlstCmdLineLinker_015,
    &optlstCmdLineLinker_017,
    &optlstCmdLineLinker_019,
    &optlstCmdLineLinker_024,
    0
};
OptionList optlstCmdLineLinker = {"Command-Line Linker Options\n", 0x200, optlstCmdLineLinker_025};

STRING_T optlstLinker_000 = {6, 0x00, 0, 0, 64, 0, (void *)&pLinker.mainname};
SET_T optlstLinker_001 = {11, 0x01, 0, (PARAM_T *)&optlstLinker_000, 1, 0x1, (void *)&mainNameGiven};
Option optlstLinker_002 = {"m|main", 0x40040, (PARAM_T *)&optlstLinker_001, 0, &optlstDisassembler_001, "set main entry point for application or shared library"};
SETSTRING_T optlstLinker_003 = {12, 0x01, 0, 0, "", 0, (void *)&pLinker.mainname};
SET_T optlstLinker_004 = {11, 0x01, 0, (PARAM_T *)&optlstLinker_003, 1, 0x1, (void *)&mainNameGiven};
Option optlstLinker_005 = {"noentry|nomain", 0x40040, (PARAM_T *)&optlstLinker_004, 0, &optlstDisassembler_001, "do not use an entry point"};
FILEPATH_T optlstLinker_006 = {2, 0x00, "filename", 0, 1, "<outfile>.MAP", (void *)&pCLTExtras.mapfilename, 0xFF};
IFARG_T optlstLinker_007 = {14, 0x02, 0, 0, (PARAM_T *)&optlstLinker_006, "set output filename for map file", 0, 0};
SET_T optlstLinker_008 = {11, 0x01, 0, (PARAM_T *)&optlstLinker_007, 1, 0x1, (void *)&pLinker.generatemap};
Option optlstLinker_009 = {"map", 0x40, (PARAM_T *)&optlstLinker_008, 0, 0, "generate link map file"};
SET_T optlstLinker_010 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pLinker.listunused};
SET_T optlstLinker_011 = {11, 0x01, 0, (PARAM_T *)&optlstLinker_010, 1, 0x1, (void *)&pLinker.generatemap};
Option optlstLinker_012 = {"mapunused|unused", 0x100040, (PARAM_T *)&optlstLinker_011, 0, 0, "include list of unused symbols in map file; implies '~~map'"};
GENERIC_T optlstLinker_013 = {13, 0x00, "file", 0, (void *)set_output_path, 0, 0};
Option optlstLinker_014 = {"o", 0x20040, (PARAM_T *)&optlstLinker_013, 0, 0, "specify output filename"};
Option optlstLinker_015 = {"nosrec", 0x3840, 0, 0, 0, "option only has a positive form"};
FILEPATH_T optlstLinker_016 = {2, 0x00, "filename", 0, 1, "<outfile>.mot", (void *)&pCLTExtras.srecfilename, 0xFF};
IFARG_T optlstLinker_017 = {14, 0x02, 0, 0, (PARAM_T *)&optlstLinker_016, "specify filename for S-record file", 0, 0};
SET_T optlstLinker_018 = {11, 0x01, 0, (PARAM_T *)&optlstLinker_017, 1, 0x1, (void *)&pLinker.generatesrec};
Option optlstLinker_019 = {"srec", 0x40, (PARAM_T *)&optlstLinker_018, 0, 0, "generate an S-record file"};
SET_T optlstLinker_020 = {11, 0x01, 0, 0, 1, 0x0, (void *)&pLinker.sreceol};
Option optlstLinker_021 = {"mac", 0x40040, (PARAM_T *)&optlstLinker_020, 0, &optlstLinker_028, "Macintosh"};
SET_T optlstLinker_022 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pLinker.sreceol};
Option optlstLinker_023 = {"dos", 0x40040, (PARAM_T *)&optlstLinker_022, 0, &optlstLinker_028, "DOS"};
SET_T optlstLinker_024 = {11, 0x01, 0, 0, 1, 0x2, (void *)&pLinker.sreceol};
Option optlstLinker_025 = {"unix", 0x40040, (PARAM_T *)&optlstLinker_024, 0, &optlstLinker_028, "Unix"};
Option *optlstLinker_026[] = {
    &optlstLinker_021,
    &optlstLinker_023,
    &optlstLinker_025,
    0
};
OptionList optlstLinker_027 = {0, 0x201, optlstLinker_026};
OptionList optlstLinker_028 = {0, 0x201, optlstLinker_026};
SET_T optlstLinker_029 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pLinker.generatesrec};
Option optlstLinker_030 = {"sreceol", 0x8040, (PARAM_T *)&optlstLinker_029, &optlstLinker_027, 0, "set end-of-line separator for S-record file; implies '~~srec'"};
NUM_T optlstLinker_031 = {3, 0x00, "length", 0, 2, 1, 0x8, 0xFF, (void *)&pLinker.sreclength};
SET_T optlstLinker_032 = {11, 0x01, 0, (PARAM_T *)&optlstLinker_031, 1, 0x1, (void *)&pLinker.generatesrec};
Option optlstLinker_033 = {"sreclength", 0x40, (PARAM_T *)&optlstLinker_032, 0, 0, "specify length of S-records; implies '~~srec'"};
Option *optlstLinker_034[] = {
    &optlstLinker_002,
    &optlstLinker_005,
    &optlstLinker_009,
    &optlstLinker_012,
    &optlstLinker_014,
    &optlstLinker_015,
    &optlstLinker_019,
    &optlstLinker_030,
    &optlstLinker_033,
    0
};
OptionList optlstLinker = {"ELF Linker Options\n", 0x200, optlstLinker_034};

GENERIC_T optlstLinkerAddresses_000 = {13, 0x00, "filename", 0, (void *)fn_0040cd55, 0, 0};
SET_T optlstLinkerAddresses_001 = {11, 0x01, 0, (PARAM_T *)&optlstLinkerAddresses_000, 1, 0x1, (void *)&pLinker.uselcf};
Option optlstLinkerAddresses_002 = {"lcf", 0x20040, (PARAM_T *)&optlstLinkerAddresses_001, 0, 0, "use the linker command file in <filename> for code and data addresses; ~~codeaddr, ~~dataaddr, ~~sdataaddr and ~~sdata2addr are ignored if ~~lcf is present; <filename> must end in '.lcf'"};
NUM_T optlstLinkerAddresses_003 = {3, 0x00, "addr", 0, 4, 0, 0x0, 0x0, (void *)&pLinker.codeaddr};
SET_T optlstLinkerAddresses_004 = {11, 0x01, 0, (PARAM_T *)&optlstLinkerAddresses_003, 1, 0x1, (void *)&pLinker.hascodeaddr};
Option optlstLinkerAddresses_005 = {"codeaddr", 0x40, (PARAM_T *)&optlstLinkerAddresses_004, 0, 0, "set address for code"};
NUM_T optlstLinkerAddresses_006 = {3, 0x00, "addr", 0, 4, 0, 0x0, 0x0, (void *)&pLinker.dataaddr};
SET_T optlstLinkerAddresses_007 = {11, 0x01, 0, (PARAM_T *)&optlstLinkerAddresses_006, 1, 0x1, (void *)&pLinker.hasdataaddr};
Option optlstLinkerAddresses_008 = {"dataaddr", 0x2040, (PARAM_T *)&optlstLinkerAddresses_007, 0, 0, "set address for data; default is to have large data sections follow the code and large const sections"};
NUM_T optlstLinkerAddresses_009 = {3, 0x00, "addr", 0, 4, 0, 0x0, 0x0, (void *)&pLinker.sdataaddr};
SET_T optlstLinkerAddresses_010 = {11, 0x01, 0, (PARAM_T *)&optlstLinkerAddresses_009, 1, 0x1, (void *)&pLinker.hassdataaddr};
Option optlstLinkerAddresses_011 = {"sdataaddr", 0x2040, (PARAM_T *)&optlstLinkerAddresses_010, 0, 0, "set address for small data; default is to have .sdata/.sbss sections follow the large data sections"};
NUM_T optlstLinkerAddresses_012 = {3, 0x00, "addr", 0, 4, 0, 0x0, 0x0, (void *)&pLinker.sdata2addr};
SET_T optlstLinkerAddresses_013 = {11, 0x01, 0, (PARAM_T *)&optlstLinkerAddresses_012, 1, 0x1, (void *)&pLinker.hassdataaddr};
Option optlstLinkerAddresses_014 = {"sdata2addr", 0x2040, (PARAM_T *)&optlstLinkerAddresses_013, 0, 0, "set address for small constant data; default is to have .sdata2/.sbss2 sections follow the .sbss section"};
NUM_T optlstLinkerAddresses_015 = {3, 0x00, "addr", 0, 4, 0, 0x0, 0x0, (void *)&pLinker.stackaddr};
SET_T optlstLinkerAddresses_016 = {11, 0x01, 0, (PARAM_T *)&optlstLinkerAddresses_015, 1, 0x1, (void *)&pLinker.hasstackaddr};
Option optlstLinkerAddresses_017 = {"stackaddr", 0x40, (PARAM_T *)&optlstLinkerAddresses_016, 0, 0, "the stack"};
NUM_T optlstLinkerAddresses_018 = {3, 0x00, "addr", 0, 4, 0, 0x0, 0x0, (void *)&pLinker.heapaddr};
SET_T optlstLinkerAddresses_019 = {11, 0x01, 0, (PARAM_T *)&optlstLinkerAddresses_018, 1, 0x1, (void *)&pLinker.hasheapaddr};
Option optlstLinkerAddresses_020 = {"heapaddr", 0x2040, (PARAM_T *)&optlstLinkerAddresses_019, 0, 0, "set address for the heap; default is computed by starting with the stack address and subtracting the heapsize and stacksize"};
NUM_T optlstLinkerAddresses_021 = {3, 0x00, "addr", 0, 4, 0, 0x0, 0x0, (void *)&pLinker.romaddr};
SET_T optlstLinkerAddresses_022 = {11, 0x01, 0, (PARAM_T *)&optlstLinkerAddresses_021, 1, 0x1, (void *)&pLinker.hasromaddr};
Option optlstLinkerAddresses_023 = {"romaddr", 0x2040, (PARAM_T *)&optlstLinkerAddresses_022, 0, 0, "set address for ROM image;default is not to generate a ROM image"};
NUM_T optlstLinkerAddresses_024 = {3, 0x00, "addr", 0, 4, 0, 0x0, 0x0, (void *)&pLinker.rambuffer};
Option optlstLinkerAddresses_025 = {"rambuffer", 0x2040, (PARAM_T *)&optlstLinkerAddresses_024, 0, 0, "set address for RAM buffer used by ROM images; default is to not generate a ROM image; '~~rambuffer' is ignored if '~~romaddr' is absent"};
Option *optlstLinkerAddresses_026[] = {
    &optlstLinkerAddresses_002,
    &optlstLinkerAddresses_005,
    &optlstLinkerAddresses_008,
    &optlstLinkerAddresses_011,
    &optlstLinkerAddresses_014,
    &optlstLinkerAddresses_017,
    &optlstLinkerAddresses_020,
    &optlstLinkerAddresses_023,
    &optlstLinkerAddresses_025,
    0
};
OptionList optlstLinkerAddresses = {"ELF Linker Address Options\n\tIf a linker command file is passed, ~~codeaddr, ~~dataaddr, ~~sdataaddr, and ~~sdata2addr are ignored.\b\tIf a linker command file is not passed, the linker will start the code sections at 0x00010000 and follow contiguously with the large const sections, the large data sections, the small data section, the small const section and then the bss sections.  If you pass one or more of ~~codeaddr, ~~dataaddr, ~~sdataaddr and ~~sdata2addr, the addresses you pass affect the starting addresses of the sections referred to.\b \tAll addresses are valid, including 0x0.\b", 0x200, optlstLinkerAddresses_026};

Option *optlstDisassembler_000[] = {
    &optlstLinker_002,
    &optlstLinker_005,
    0
};
OptionList optlstDisassembler_001 = {0, 0x0, optlstDisassembler_000};
SET_T optlstDisassembler_002 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pDisassembler.extended};
Option optlstDisassembler_003 = {"x", 0x1400C0, (PARAM_T *)&optlstDisassembler_002, 0, &optlstDisassembler_006, "[don't] show extended mnemonics"};
Option *optlstDisassembler_004[] = {
    &optlstDisassembler_003,
    0
};
OptionList optlstDisassembler_005 = {0, 0x601, optlstDisassembler_004};
OptionList optlstDisassembler_006 = {0, 0x601, optlstDisassembler_004};
Option optlstDisassembler_007 = {"fmt|format", 0xC0C0, 0, &optlstDisassembler_005, 0, "specify formatting options"};
SET_T optlstDisassembler_008 = {11, 0x01, 0, 0, 1, 0x0, (void *)&pDisassembler.showheaders};
SET_T optlstDisassembler_009 = {11, 0x01, 0, (PARAM_T *)&optlstDisassembler_008, 1, 0x0, (void *)&pDisassembler.showdebug};
SET_T optlstDisassembler_010 = {11, 0x01, 0, (PARAM_T *)&optlstDisassembler_009, 1, 0x0, (void *)&pDisassembler.showtables};
SET_T optlstDisassembler_011 = {11, 0x01, 0, (PARAM_T *)&optlstDisassembler_010, 1, 0x0, (void *)&pDisassembler.showdetail};
SET_T optlstDisassembler_012 = {11, 0x01, 0, (PARAM_T *)&optlstDisassembler_011, 1, 0x0, (void *)&pDisassembler.extended};
SET_T optlstDisassembler_013 = {11, 0x01, 0, (PARAM_T *)&optlstDisassembler_012, 1, 0x1, (void *)&pDisassembler.nobinary};
SET_T optlstDisassembler_014 = {11, 0x01, 0, (PARAM_T *)&optlstDisassembler_013, 1, 0x0, (void *)&pDisassembler.showxtables};
SET_T optlstDisassembler_015 = {11, 0x01, 0, (PARAM_T *)&optlstDisassembler_014, 1, 0x0, (void *)&pDisassembler.showcode};
SET_T optlstDisassembler_016 = {11, 0x01, 0, (PARAM_T *)&optlstDisassembler_015, 1, 0x0, (void *)&pDisassembler.showdata};
Option optlstDisassembler_017 = {"only|none", 0xC0, (PARAM_T *)&optlstDisassembler_016, 0, 0, "as in '~~show none' or, e.g.,\015'~~show only,code,data'"};
SET_T optlstDisassembler_018 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pDisassembler.showheaders};
SET_T optlstDisassembler_019 = {11, 0x01, 0, (PARAM_T *)&optlstDisassembler_018, 1, 0x1, (void *)&pDisassembler.showdebug};
SET_T optlstDisassembler_020 = {11, 0x01, 0, (PARAM_T *)&optlstDisassembler_019, 1, 0x1, (void *)&pDisassembler.showtables};
SET_T optlstDisassembler_021 = {11, 0x01, 0, (PARAM_T *)&optlstDisassembler_020, 1, 0x1, (void *)&pDisassembler.showdetail};
SET_T optlstDisassembler_022 = {11, 0x01, 0, (PARAM_T *)&optlstDisassembler_021, 1, 0x1, (void *)&pDisassembler.extended};
SET_T optlstDisassembler_023 = {11, 0x01, 0, (PARAM_T *)&optlstDisassembler_022, 1, 0x0, (void *)&pDisassembler.nobinary};
SET_T optlstDisassembler_024 = {11, 0x01, 0, (PARAM_T *)&optlstDisassembler_023, 1, 0x1, (void *)&pDisassembler.showxtables};
SET_T optlstDisassembler_025 = {11, 0x01, 0, (PARAM_T *)&optlstDisassembler_024, 1, 0x1, (void *)&pDisassembler.showcode};
SET_T optlstDisassembler_026 = {11, 0x01, 0, (PARAM_T *)&optlstDisassembler_025, 1, 0x1, (void *)&pDisassembler.showdata};
Option optlstDisassembler_027 = {"all", 0xC0, (PARAM_T *)&optlstDisassembler_026, 0, 0, "show everything"};
SET_T optlstDisassembler_028 = {11, 0x01, 0, 0, 1, 0x0, (void *)&pDisassembler.nobinary};
Option optlstDisassembler_029 = {"binary", 0x1000C0, (PARAM_T *)&optlstDisassembler_028, 0, 0, "show binary information, such as address and opcodes, for object code"};
SET_T optlstDisassembler_030 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pDisassembler.showcode};
Option optlstDisassembler_031 = {"code|text", 0x1000C0, (PARAM_T *)&optlstDisassembler_030, 0, 0, "show .text sections"};
SET_T optlstDisassembler_032 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pDisassembler.showdata};
Option optlstDisassembler_033 = {"data", 0x1000C0, (PARAM_T *)&optlstDisassembler_032, 0, 0, "show data"};
SET_T optlstDisassembler_034 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pDisassembler.showdetail};
Option optlstDisassembler_035 = {"detail", 0x1000C0, (PARAM_T *)&optlstDisassembler_034, 0, 0, "show detailed dump information"};
SET_T optlstDisassembler_036 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pDisassembler.extended};
Option optlstDisassembler_037 = {"extended", 0x1000C0, (PARAM_T *)&optlstDisassembler_036, 0, 0, "show extended mnemonics"};
SET_T optlstDisassembler_038 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pDisassembler.showxtables};
Option optlstDisassembler_039 = {"exceptions|xtab|xtables", 0x1000C0, (PARAM_T *)&optlstDisassembler_038, 0, 0, "show exception tables; implies '~~show data'"};
SET_T optlstDisassembler_040 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pDisassembler.showheaders};
Option optlstDisassembler_041 = {"headers", 0x1000C0, (PARAM_T *)&optlstDisassembler_040, 0, 0, "show object headers"};
SET_T optlstDisassembler_042 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pDisassembler.showdebug};
Option optlstDisassembler_043 = {"debug|dwarf", 0x1000C0, (PARAM_T *)&optlstDisassembler_042, 0, 0, "show DWARF information"};
SET_T optlstDisassembler_044 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pDisassembler.showtables};
Option optlstDisassembler_045 = {"tables", 0x1000C0, (PARAM_T *)&optlstDisassembler_044, 0, 0, "show string and symbol tables"};
SET_T optlstDisassembler_046 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pDisassembler.showxtables};
Option optlstDisassembler_047 = {"xtables", 0x1000C0, (PARAM_T *)&optlstDisassembler_046, 0, 0, "show exception tables"};
Option *optlstDisassembler_048[] = {
    &optlstDisassembler_017,
    &optlstDisassembler_027,
    &optlstDisassembler_029,
    &optlstDisassembler_031,
    &optlstDisassembler_033,
    &optlstDisassembler_035,
    &optlstDisassembler_037,
    &optlstDisassembler_039,
    &optlstDisassembler_041,
    &optlstDisassembler_043,
    &optlstDisassembler_045,
    &optlstDisassembler_047,
    0
};
OptionList optlstDisassembler_049 = {0, 0x600, optlstDisassembler_048};
Option optlstDisassembler_050 = {"show", 0x80C0, 0, &optlstDisassembler_049, 0, "specify display options"};
SET_T optlstDisassembler_051 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pDisassembler.relocate};
Option optlstDisassembler_052 = {"relocate", 0x1000C0, (PARAM_T *)&optlstDisassembler_051, 0, 0, "for DWARF information, relocate addends in .rela.text and .rela.debug"};
ONOFF_T optlstDisassembler_053 = {7, 0x00, 0, 0, (void *)&pDisassembler.showxtables};
Option optlstDisassembler_054 = {"xtables", 0x40C0, (PARAM_T *)&optlstDisassembler_053, 0, 0, "show exception tables"};
Option *optlstDisassembler_055[] = {
    &optlstDisassembler_007,
    &optlstDisassembler_050,
    &optlstDisassembler_052,
    &optlstDisassembler_054,
    0
};
OptionList optlstDisassembler = {"Embedded PowerPC Disassembler Options\n", 0x600, optlstDisassembler_055};

SET_T optlstDebugging_000 = {11, 0x01, 0, 0, 1, 0x1, (void *)&useFullPaths};
SET_T optlstDebugging_001 = {11, 0x01, 0, (PARAM_T *)&optlstDebugging_000, 1, 0x1, (void *)&pCmdLine.debugInfo};
Option optlstDebugging_002 = {"g", 0x145, (PARAM_T *)&optlstDebugging_001, 0, 0, "generate debugging information; same as '~~sym full'"};
SET_T optlstDebugging_003 = {11, 0x01, 0, 0, 1, 0x0, (void *)&pCmdLine.debugInfo};
Option optlstDebugging_004 = {"off", 0x141, (PARAM_T *)&optlstDebugging_003, 0, 0, "do not generate debugging information"};
SET_T optlstDebugging_005 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pCmdLine.debugInfo};
Option optlstDebugging_006 = {"on", 0x2141, (PARAM_T *)&optlstDebugging_005, 0, 0, "turn on debugging information"};
SET_T optlstDebugging_007 = {11, 0x01, 0, 0, 1, 0x1, (void *)&useFullPaths};
SET_T optlstDebugging_008 = {11, 0x01, 0, (PARAM_T *)&optlstDebugging_007, 1, 0x1, (void *)&pCmdLine.debugInfo};
Option optlstDebugging_009 = {"full|fullpath", 0x3141, (PARAM_T *)&optlstDebugging_008, 0, 0, "store full paths to source files"};
Option *optlstDebugging_010[] = {
    &optlstDebugging_004,
    &optlstDebugging_006,
    &optlstDebugging_009,
    0
};
OptionList optlstDebugging_011 = {0, 0x302, optlstDebugging_010};
Option optlstDebugging_012 = {"sym", 0xA141, 0, &optlstDebugging_011, 0, "specify debugging options"};
Option *optlstDebugging_013[] = {
    &optlstDebugging_002,
    &optlstDebugging_012,
    0
};
OptionList optlstDebugging = {"Debugging Control Options\n", 0x300, optlstDebugging_013};

GENERIC_T optlstProject_000 = {13, 0x01, 0, 0, (void *)fn_004055c0, (void *)&lbl_004728FC, 0};
Option optlstProject_001 = {"application", 0x40041, (PARAM_T *)&optlstProject_000, 0, &optlstWarning_001, "generate an application; same as '~~xm a'"};
GENERIC_T optlstProject_002 = {13, 0x01, 0, 0, (void *)fn_004055c0, (void *)&lbl_00472968, 0};
Option optlstProject_003 = {"library", 0x40041, (PARAM_T *)&optlstProject_002, 0, &optlstWarning_001, "generate a static library; same as '~~xm l'"};
GENERIC_T optlstProject_004 = {13, 0x01, 0, 0, (void *)fn_004055c0, (void *)&lbl_004729D0, 0};
Option optlstProject_005 = {"partial|r", 0x40, (PARAM_T *)&optlstProject_004, 0, 0, "perform a partial link of objects; unresolved symbols are not errors"};
SET_T optlstProject_006 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pProject.optpartial};
GENERIC_T optlstProject_007 = {13, 0x01, 0, (PARAM_T *)&optlstProject_006, (void *)fn_004055c0, (void *)&lbl_004729D0, 0};
Option optlstProject_008 = {"opt_partial|r1", 0x40, (PARAM_T *)&optlstProject_007, 0, 0, "perform final work on a partial link; allows use of LCF file and creates tables for static constructors / destructors and C++ exception initialization; implies '~~r'"};
SET_T optlstProject_009 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pProject.resolvedpartial};
SET_T optlstProject_010 = {11, 0x01, 0, (PARAM_T *)&optlstProject_009, 1, 0x1, (void *)&pProject.optpartial};
GENERIC_T optlstProject_011 = {13, 0x01, 0, (PARAM_T *)&optlstProject_010, (void *)fn_004055c0, (void *)&lbl_004729D0, 0};
Option optlstProject_012 = {"resolved_partial|r2", 0x40, (PARAM_T *)&optlstProject_011, 0, 0, "perform resolved final partial link; unresolved symbols are errors; implies '~~r1'"};
SET_T optlstProject_013 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pProject.strip};
Option optlstProject_014 = {"strip_partial", 0x40, (PARAM_T *)&optlstProject_013, 0, 0, "perform dead-stripping on partial link; requires '~~r1' or '~~r2'"};
ONOFF_T optlstProject_015 = {7, 0x00, 0, 0, (void *)&pProject.strip};
Option optlstProject_016 = {"strip", 0x1040, (PARAM_T *)&optlstProject_015, 0, 0, "perform dead-stripping on partial link; requires '~~r1' or '~~r2'"};
SET_T optlstProject_017 = {11, 0x01, 0, 0, 1, 0x1, (void *)&pProject.bigendian};
Option optlstProject_018 = {"big", 0x401C0, (PARAM_T *)&optlstProject_017, 0, &optlstWarning_003, "generate code and link for a big-endian target"};
SET_T optlstProject_019 = {11, 0x01, 0, 0, 1, 0x0, (void *)&pProject.bigendian};
Option optlstProject_020 = {"little", 0x401C0, (PARAM_T *)&optlstProject_019, 0, &optlstWarning_003, "generate code and link for a little-endian target"};
SET_T optlstProject_021 = {11, 0x01, 0, 0, 2, 0x0, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_022 = {"401", 0x401C0, (PARAM_T *)&optlstProject_021, 0, &optlstProject_073, ""};
SET_T optlstProject_023 = {11, 0x01, 0, 0, 2, 0x1, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_024 = {"403", 0x401C0, (PARAM_T *)&optlstProject_023, 0, &optlstProject_073, ""};
SET_T optlstProject_025 = {11, 0x01, 0, 0, 2, 0x2, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_026 = {"505", 0x401C0, (PARAM_T *)&optlstProject_025, 0, &optlstProject_073, ""};
SET_T optlstProject_027 = {11, 0x01, 0, 0, 2, 0x3, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_028 = {"509", 0x401C0, (PARAM_T *)&optlstProject_027, 0, &optlstProject_073, ""};
SET_T optlstProject_029 = {11, 0x01, 0, 0, 2, 0x4, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_030 = {"555", 0x401C0, (PARAM_T *)&optlstProject_029, 0, &optlstProject_073, ""};
SET_T optlstProject_031 = {11, 0x01, 0, 0, 2, 0x5, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_032 = {"601", 0x401C0, (PARAM_T *)&optlstProject_031, 0, &optlstProject_073, ""};
SET_T optlstProject_033 = {11, 0x01, 0, 0, 2, 0x6, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_034 = {"602", 0x401C0, (PARAM_T *)&optlstProject_033, 0, &optlstProject_073, ""};
SET_T optlstProject_035 = {11, 0x01, 0, 0, 2, 0x7, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_036 = {"603", 0x401C0, (PARAM_T *)&optlstProject_035, 0, &optlstProject_073, ""};
SET_T optlstProject_037 = {11, 0x01, 0, 0, 2, 0x8, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_038 = {"603e", 0x401C0, (PARAM_T *)&optlstProject_037, 0, &optlstProject_073, ""};
SET_T optlstProject_039 = {11, 0x01, 0, 0, 2, 0x9, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_040 = {"604", 0x401C0, (PARAM_T *)&optlstProject_039, 0, &optlstProject_073, ""};
SET_T optlstProject_041 = {11, 0x01, 0, 0, 2, 0xA, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_042 = {"604e", 0x401C0, (PARAM_T *)&optlstProject_041, 0, &optlstProject_073, ""};
SET_T optlstProject_043 = {11, 0x01, 0, 0, 2, 0xB, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_044 = {"740", 0x401C0, (PARAM_T *)&optlstProject_043, 0, &optlstProject_073, ""};
SET_T optlstProject_045 = {11, 0x01, 0, 0, 2, 0xC, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_046 = {"750", 0x401C0, (PARAM_T *)&optlstProject_045, 0, &optlstProject_073, ""};
SET_T optlstProject_047 = {11, 0x01, 0, 0, 2, 0xD, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_048 = {"801", 0x401C0, (PARAM_T *)&optlstProject_047, 0, &optlstProject_073, ""};
SET_T optlstProject_049 = {11, 0x01, 0, 0, 2, 0xE, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_050 = {"821", 0x401C0, (PARAM_T *)&optlstProject_049, 0, &optlstProject_073, ""};
SET_T optlstProject_051 = {11, 0x01, 0, 0, 2, 0xF, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_052 = {"823", 0x401C0, (PARAM_T *)&optlstProject_051, 0, &optlstProject_073, ""};
SET_T optlstProject_053 = {11, 0x01, 0, 0, 2, 0x10, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_054 = {"850", 0x401C0, (PARAM_T *)&optlstProject_053, 0, &optlstProject_073, ""};
SET_T optlstProject_055 = {11, 0x01, 0, 0, 2, 0x11, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_056 = {"860", 0x401C0, (PARAM_T *)&optlstProject_055, 0, &optlstProject_073, ""};
SET_T optlstProject_057 = {11, 0x01, 0, 0, 2, 0x15, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_058 = {"7400", 0x401C0, (PARAM_T *)&optlstProject_057, 0, &optlstProject_073, ""};
SET_T optlstProject_059 = {11, 0x01, 0, 0, 2, 0x18, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_060 = {"7450", 0x401C0, (PARAM_T *)&optlstProject_059, 0, &optlstProject_073, ""};
SET_T optlstProject_061 = {11, 0x01, 0, 0, 2, 0x12, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_062 = {"8240", 0x401C0, (PARAM_T *)&optlstProject_061, 0, &optlstProject_073, ""};
SET_T optlstProject_063 = {11, 0x01, 0, 0, 2, 0x13, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_064 = {"8260", 0x401C0, (PARAM_T *)&optlstProject_063, 0, &optlstProject_073, ""};
SET_T optlstProject_065 = {11, 0x01, 0, 0, 2, 0x17, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_066 = {"e500", 0x401C0, (PARAM_T *)&optlstProject_065, 0, &optlstProject_073, ""};
SET_T optlstProject_067 = {11, 0x01, 0, 0, 2, 0x16, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_068 = {"gekko||gecko", 0x401C0, (PARAM_T *)&optlstProject_067, 0, &optlstProject_073, ""};
SET_T optlstProject_069 = {11, 0x01, 0, 0, 2, 0x14, (void *)((char *)&pCodeGen + 16)};
Option optlstProject_070 = {"generic", 0x401C0, (PARAM_T *)&optlstProject_069, 0, &optlstProject_073, ""};
Option *optlstProject_071[] = {
    &optlstProject_022,
    &optlstProject_024,
    &optlstProject_026,
    &optlstProject_028,
    &optlstProject_030,
    &optlstProject_032,
    &optlstProject_034,
    &optlstProject_036,
    &optlstProject_038,
    &optlstProject_040,
    &optlstProject_042,
    &optlstProject_044,
    &optlstProject_046,
    &optlstProject_048,
    &optlstProject_050,
    &optlstProject_052,
    &optlstProject_054,
    &optlstProject_056,
    &optlstProject_058,
    &optlstProject_060,
    &optlstProject_062,
    &optlstProject_064,
    &optlstProject_066,
    &optlstProject_068,
    &optlstProject_070,
    0
};
OptionList optlstProject_072 = {0, 0x701, optlstProject_071};
OptionList optlstProject_073 = {0, 0x701, optlstProject_071};
Option optlstProject_074 = {"proc|processor", 0x81C0, 0, &optlstProject_072, 0, "specify processor for scheduling and inline assembler"};
SET_T optlstProject_075 = {11, 0x01, 0, 0, 1, 0x0, (void *)((char *)&pCodeGen + 14)};
Option optlstProject_076 = {"none|off", 0x401C0, (PARAM_T *)&optlstProject_075, 0, &optlstProject_085, "no floating point"};
SET_T optlstProject_077 = {11, 0x01, 0, 0, 1, 0x1, (void *)((char *)&pCodeGen + 14)};
Option optlstProject_078 = {"soft|software", 0x401C0, (PARAM_T *)&optlstProject_077, 0, &optlstProject_085, "software FP emulation"};
SET_T optlstProject_079 = {11, 0x01, 0, 0, 1, 0x2, (void *)((char *)&pCodeGen + 14)};
Option optlstProject_080 = {"hard|hardware", 0x401C0, (PARAM_T *)&optlstProject_079, 0, &optlstProject_085, "hardware FP codegen"};
SET_T optlstProject_081 = {11, 0x01, 0, 0, 1, 0x3, (void *)((char *)&pCodeGen + 14)};
Option optlstProject_082 = {"fmadd", 0x401C0, (PARAM_T *)&optlstProject_081, 0, &optlstProject_085, "same as '~~fp hard' and '~~fp_contract'"};
Option *optlstProject_083[] = {
    &optlstProject_076,
    &optlstProject_078,
    &optlstProject_080,
    &optlstProject_082,
    0
};
OptionList optlstProject_084 = {0, 0x701, optlstProject_083};
OptionList optlstProject_085 = {0, 0x701, optlstProject_083};
Option optlstProject_086 = {"fp", 0x81C0, 0, &optlstProject_084, 0, "specify floating-point code generation options"};
NUM_T optlstProject_087 = {3, 0x00, 0, 0, 2, 0, 0x0, 0x0, (void *)&pProject.sdatathreshold};
Option optlstProject_088 = {"sdatathreshold", 0x1C0, (PARAM_T *)&optlstProject_087, 0, 0, "set maximum size in bytes of mutable data objects before they are moved from small data sections"};
NUM_T optlstProject_089 = {3, 0x00, 0, 0, 2, 0, 0x0, 0x0, (void *)&pProject.sdatathreshold};
Option optlstProject_090 = {"sdata|sdatathreshold", 0x1C0, (PARAM_T *)&optlstProject_089, 0, 0, "set maximum size in bytes for mutable data objects before being spilled from small data section into data section"};
NUM_T optlstProject_091 = {3, 0x00, 0, 0, 2, 0, 0x0, 0x0, (void *)&pProject.sdata2threshold};
Option optlstProject_092 = {"sdata2|sdata2threshold", 0x1C0, (PARAM_T *)&optlstProject_091, 0, 0, "set maximum size in bytes for constant data objects before being spilled from constant section into data section"};
NUM_T optlstProject_093 = {3, 0x00, 0, 0, 4, 0, 0x0, 0x0, (void *)&pProject.heapsize};
Option optlstProject_094 = {"heapsize", 0x40, (PARAM_T *)&optlstProject_093, 0, 0, "set size of data heap in kilobytes"};
NUM_T optlstProject_095 = {3, 0x00, 0, 0, 4, 0, 0x0, 0x0, (void *)&pProject.stacksize};
Option optlstProject_096 = {"stacksize", 0x40, (PARAM_T *)&optlstProject_095, 0, 0, "set size of stack in kilobytes"};
SET_T optlstProject_097 = {11, 0x01, 0, 0, 2, 0x1, (void *)&pProject.codemodel};
Option optlstProject_098 = {"absolute", 0x401C0, (PARAM_T *)&optlstProject_097, 0, &optlstProject_105, "absolute code and data addressing"};
SET_T optlstProject_099 = {11, 0x01, 0, 0, 2, 0x2, (void *)&pProject.codemodel};
Option optlstProject_100 = {"pic", 0x411C0, (PARAM_T *)&optlstProject_099, 0, &optlstProject_105, "position-independent code"};
SET_T optlstProject_101 = {11, 0x01, 0, 0, 2, 0x3, (void *)&pProject.codemodel};
Option optlstProject_102 = {"other", 0x401C0, (PARAM_T *)&optlstProject_101, 0, &optlstProject_105, "other code model"};
Option *optlstProject_103[] = {
    &optlstProject_098,
    &optlstProject_100,
    &optlstProject_102,
    0
};
OptionList optlstProject_104 = {0, 0x701, optlstProject_103};
OptionList optlstProject_105 = {0, 0x701, optlstProject_103};
Option optlstProject_106 = {"model", 0x81C0, 0, &optlstProject_104, 0, "specify code model"};
PARAM_T optlstProject_107 = {0, 0x00, 0, 0};
Option optlstProject_108 = {"gprel|gprelative", 0x29C0, (PARAM_T *)&optlstProject_107, 0, 0, 0};
GENERIC_T optlstProject_109 = {13, 0x01, 0, 0, (void *)fn_004055c0, (void *)&lbl_004728FC, 0};
Option optlstProject_110 = {"a|application|e|executable", 0x1041, (PARAM_T *)&optlstProject_109, 0, 0, "application"};
GENERIC_T optlstProject_111 = {13, 0x01, 0, 0, (void *)fn_004055c0, (void *)&lbl_00472968, 0};
Option optlstProject_112 = {"l|library", 0x1041, (PARAM_T *)&optlstProject_111, 0, 0, "static library"};
GENERIC_T optlstProject_113 = {13, 0x01, 0, 0, (void *)fn_004055c0, (void *)&lbl_004729D0, 0};
Option optlstProject_114 = {"p|partial|partiallink", 0x1041, (PARAM_T *)&optlstProject_113, 0, 0, "partial link"};
Option *optlstProject_115[] = {
    &optlstProject_110,
    &optlstProject_112,
    &optlstProject_114,
    0
};
OptionList optlstProject_116 = {0, 0x200, optlstProject_115};
Option optlstProject_117 = {"xm", 0x49041, 0, &optlstProject_116, &optlstWarning_001, "specify project type"};
GENERIC_T optlstProject_118 = {13, 0x01, 0, 0, (void *)fn_004055c0, (void *)&lbl_004728FC, 0};
Option optlstProject_119 = {"xma", 0x41041, (PARAM_T *)&optlstProject_118, 0, &optlstWarning_001, "same as '~~xm a'"};
GENERIC_T optlstProject_120 = {13, 0x01, 0, 0, (void *)fn_004055c0, (void *)&lbl_00472968, 0};
Option optlstProject_121 = {"xml", 0x41041, (PARAM_T *)&optlstProject_120, 0, &optlstWarning_001, "same as '~~xm l'"};
GENERIC_T optlstProject_122 = {13, 0x01, 0, 0, (void *)fn_004055c0, (void *)&lbl_004729D0, 0};
Option optlstProject_123 = {"xmp", 0x41041, (PARAM_T *)&optlstProject_122, 0, &optlstWarning_001, "same as '~~xm p'"};
Option *optlstProject_124[] = {
    &optlstProject_001,
    &optlstProject_003,
    &optlstProject_005,
    &optlstProject_008,
    &optlstProject_012,
    &optlstProject_014,
    &optlstProject_016,
    &optlstProject_018,
    &optlstProject_020,
    &optlstProject_074,
    &optlstProject_086,
    &optlstProject_088,
    &optlstProject_090,
    &optlstProject_092,
    &optlstProject_094,
    &optlstProject_096,
    &optlstProject_106,
    &optlstProject_108,
    &optlstProject_117,
    &optlstProject_119,
    &optlstProject_121,
    &optlstProject_123,
    0
};
OptionList optlstProject = {"Embedded PPC Project Options\n", 0x700, optlstProject_124};

Option *optlstWarning_000[] = {
    &optlstProject_001,
    &optlstProject_003,
    &optlstProject_117,
    &optlstProject_119,
    &optlstProject_121,
    &optlstProject_123,
    0
};
OptionList optlstWarning_001 = {0, 0x0, optlstWarning_000};
Option *optlstWarning_002[] = {
    &optlstProject_018,
    &optlstProject_020,
    0
};
OptionList optlstWarning_003 = {0, 0x0, optlstWarning_002};
GENERIC_T optlstWarning_004 = {13, 0x01, 0, 0, (void *)parse_warning_settings, (void *)&lbl_00473C48, 0};
Option optlstWarning_005 = {"off||quiet", 0x1C1, (PARAM_T *)&optlstWarning_004, 0, 0, "turn off all warnings"};
GENERIC_T optlstWarning_006 = {13, 0x01, 0, 0, (void *)parse_warning_settings, (void *)&lbl_00473CA8, 0};
Option optlstWarning_007 = {"on", 0x1C1, (PARAM_T *)&optlstWarning_006, 0, 0, "turn on all warnings"};
GENERIC_T optlstWarning_008 = {13, 0x01, 0, 0, (void *)parse_warning_settings, (void *)&lbl_00473CF8, 0};
Option optlstWarning_009 = {"cmdline", 0x100041, (PARAM_T *)&optlstWarning_008, 0, 0, "command-line parser warnings"};
GENERIC_T optlstWarning_010 = {13, 0x01, 0, 0, (void *)parse_warning_settings, (void *)&lbl_00473D54, 0};
Option optlstWarning_011 = {"err|error|iserr|iserror", 0x1001C1, (PARAM_T *)&optlstWarning_010, 0, 0, "treat warnings as errors"};
GENERIC_T optlstWarning_012 = {13, 0x01, 0, 0, (void *)print_command_line_warning_options, 0, 0};
Option optlstWarning_013 = {"display|dump", 0x41, (PARAM_T *)&optlstWarning_012, 0, 0, "display list of active warnings"};
Option *optlstWarning_014[] = {
    &optlstWarning_005,
    &optlstWarning_007,
    &optlstWarning_009,
    &optlstWarning_011,
    &optlstWarning_013,
    0
};
OptionList optlstWarning_015 = {0, 0x202, optlstWarning_014};
Option optlstWarning_016 = {"w|warn|warnings", 0x8041, 0, &optlstWarning_015, 0, "warning options"};
Option *optlstWarning_017[] = {
    &optlstWarning_016,
    0
};
OptionList optlstWarning = {"Warning Options\n", 0x200, optlstWarning_017};

/* '-application', '-library' and '-partial': the project's kind, from its four-character code. */
int fn_004055c0(int unused1, char *kind, int unused2, int flags)
{
    switch ((unsigned short)((((unsigned char *)kind)[0] << 8) + ((unsigned char *)kind)[1])) {
    case 'Ap':
        pProject.type = PROJECT_APPLICATION;
        break;
    case 'St':
        pProject.type = PROJECT_LIBRARY;
        break;
    case 'Pr':
        pProject.type = PROJECT_PARTIAL;
        break;
    default:
        return 0;
    }
    return 1;
}

static char *prefPanels[] = {"PPC EABI CodeGen", "PPC EABI Linker", "PPC EABI Disassembler", "PPC EABI Project",
                             "CmdLine Panel",    "CmdLine Linker Panel", "CmdLine Extras EPPC"};

static OptionList *optLists[] = {&optlstCmdLine,          &optlstCmdLineLinker, &optlstLinker,  &optlstLinkerAddresses,
                                 &optlstProject,          &optlstDebugging,     &optlstWarning, &optlstDisassembler};

static PrefDataPanel stPrefPanels[] = {
    {"PPC EABI CodeGen", (unsigned char *)&pCodeGen, sizeof(pCodeGen)},
    {"PPC EABI Linker", (unsigned char *)&pLinker, sizeof(PLinker)},
    {"PPC EABI Project", (unsigned char *)&pProject, sizeof(PProject)},
    {"PPC EABI Disassembler", (unsigned char *)&pDisassembler, sizeof(PDisassembler)},
    {"CmdLine Panel", (unsigned char *)&pCmdLine, sizeof(PCmdLine)},
    {"CmdLine Linker Panel", (unsigned char *)&pCmdLineLinker, sizeof(PCmdLineLinker)},
    {"CmdLine Extras EPPC", (unsigned char *)&pCLTExtras, sizeof(PCLTExtras)},
};

/* Before the options: no output name yet, and the system libraries in. */
static Boolean pre_parse(void)
{
    output_path = 0;
    useDefaultLibraries = 1;
    return 1;
}

/* After the options and before the files: the system library paths (%MWEABIPPCLibraries% or %MWLibraries%). */
static Boolean mid_parse(void)
{
    char *paths;
    char *name;

    if (data_00587e10 > 0 && useDefaultLibraries) {
        paths = ParserHelpers_GetFirstEnvironmentVariable("MWEABIPPCLibraries\0MWLibraries" ENVIRONMENT_LIST_END, 1, &name);
        if (paths != NULL && ParserHelpers_ParsePathList(paths, ';', ':', 1, name, 1, -1, 0) == 0) {
            return 0;
        }
    }
    return 1;
}

/* After the files: the output's name (by the project's kind), the settings checked, the system libraries
   (%MWEABIPPCLibraryFiles% or %MWLibraryFiles%) added when linking, and the entry point. */
static Boolean post_parse(void)
{
    char *files;
    char *name;

    fn_0040d952(&output_path, pProject.outfile, sizeof(pProject.outfile), 1, 0, pProject.type, "a.out",
                PROJECT_APPLICATION, "", PROJECT_LIBRARY, ".a", PROJECT_PARTIAL, ".o", -1, 0);
    if (fn_0040d012(0) == 0) {
        return 0;
    }
    if (pProject.strip && !(pProject.type == PROJECT_PARTIAL && pProject.optpartial)) {
        Targets_DispatchVariadicMessage(
            0x1c, "'-strip_partial on' only has an effect when used with '-opt_partial' or '-resolved_partial'");
        return 0;
    }
    if (pCmdLine.state == 3 && pProject.type != PROJECT_LIBRARY && useDefaultLibraries) {
        files = ParserHelpers_GetFirstEnvironmentVariable("MWEABIPPCLibraryFiles\0MWLibraryFiles" ENVIRONMENT_LIST_END, 1, &name);
        if (files != NULL && ParserHelpers_AddFileList(files, ';', ':', 1, name, -1) == 0) {
            return 0;
        }
    }
    if (!mainNameGiven) {
        strcpy(pLinker.mainname, "__start");
    }
    pLinker.generatesyminfo = pCmdLine.debugInfo;
    pLinker.fullpaths = useFullPaths;
    return 1;
}

static ParserTool parser_tool = {'Link',
                                 '****',
                                 'ePPC',
                                 'EABI',
                                 7,
                                 prefPanels,
                                 "Metrowerks Linker for Embedded PowerPC",
                                 "1998-2000",
                                 8,
                                 optLists,
                                 7,
                                 stPrefPanels,
                                 pre_parse,
                                 mid_parse,
                                 post_parse};

int fn_00405840(void)
{
    return Targets_SetTool(&parser_tool);
}
