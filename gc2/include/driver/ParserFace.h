#ifndef DRIVER_PARSERFACE_H
#define DRIVER_PARSERFACE_H

#include <setjmp.h>
#include "compiler/common.h"
#include "driver/cmdline.h"
#include "driver/CWPlugins.h"
#include "driver/parser.h"

#ifdef __cplusplus
extern "C" {
#endif

// A preference panel's default settings the parser stores (Pro7's parser.h)
struct PrefDataPanel {
    char* name;
    void* ptr;
    SInt32 size;
};

// The command line parser's tool (Pro7's parser.h; ParserGlue-eabi-ppc-cc.c's `parser`)
struct ParserTool {
    OSType TYPE;
    OSType LANG;
    OSType CPU;
    OSType OS;
    int numPrefPanels;
    char** prefPanels;
    char* toolInfo;
    char* copyright;
    int numOptionLists;
    struct OptionList** optionLists;
    int numPrefDataPanels;
    PrefDataPanel* prefDataPanels;
    int (*PreParse)(void);
    int (*MidParse)(void);
    int (*PostParse)(void);
};

// The parser's state (Pro7's parser.h)
typedef struct ParseOptsType {
    struct CWPluginPrivateContext* context;
    char helpKey[64];
    SInt32 helpFlags;
    UInt16 ioCols;
    UInt16 ioRows;
    CWCommandLineArgs* args;
    const ToolVersionInfo* toolVersion;
    int numPlugins;
    const CLPluginInfo* plugins;
    int numPanels;
    const char** panelNames;
    OSType cpu;
    OSType os;
    char lastoutputname[256];
    SInt32 currentSegment;
    SInt32 currentOverlayGroup;
    SInt32 currentOverlay;
    int possibleFiles;
    int userSpecifiedFiles;
    int unusedFiles;
    Boolean hadAnyOutput;
    Boolean hadErrors;
    Boolean showHelp;
    Boolean underIDE;
    Boolean alwaysUsePaths;
    Boolean noOptions;
    Boolean printedVersion;
    Boolean passingArgs;
    Boolean disToFile;
    Boolean ppToFile;
    Boolean initBefore;
    Boolean weakImport;
    Boolean mergeIntoOutput;
    Boolean success;
    Boolean ignoreUnknown;
    UInt8 unused[2];
} ParseOptsType;

extern const char* failedCallback;
extern jmp_buf exit_plugin;
extern ParseOptsType parseopts;

extern CWResult Parser_StorePanels(struct CWPluginPrivateContext* context);
extern CWPLUGIN_ENTRY(CWParser_GetDropInFlags)(const DropInFlags** flags, SInt32* flagsSize);
extern CWPLUGIN_ENTRY(CWParser_GetDropInName)(const char** dropinName);
extern CWPLUGIN_ENTRY(CWParser_GetDisplayName)(const char** displayName);
extern CWPLUGIN_ENTRY(CWParser_GetPanelList)(const CWPanelList** panelList);
extern CWPLUGIN_ENTRY(CWParser_GetVersionInfo)(const VersionInfo** versioninfo);
extern CWPLUGIN_ENTRY(Parser_SupportsPlugin)(
    const CLPluginInfo* pluginfo, CWDataType cpu, CWDataType os, Boolean* isSupported
);
extern CWPLUGIN_ENTRY(Parser_SupportsPanels)(
    int numPanels, const char** panelNames, Boolean* isSupported
);
extern CWPLUGIN_ENTRY(parser_main)(struct CWPluginPrivateContext* context);

#ifdef __cplusplus
}
#endif

#endif
