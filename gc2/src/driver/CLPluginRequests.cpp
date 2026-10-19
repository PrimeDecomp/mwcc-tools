// Pro7's CmdLine/Src/CLPluginRequests.cpp: the driver's requests to its plugins.
// (the command line's C++ sources are built with `bool` off: comparisons are ints; the plugin
// library's, CWPluginsPrivate.cpp and CWSecretPluginCallbacks.cpp, with it on)
#pragma bool off

#include "driver/CLPluginRequests.h"
#include "driver/CLDropinCallbacks_V10.h"
#include "driver/CLErrors.h"
#include "driver/CLFileOps.h"
#include "driver/CLIO.h"
#include "driver/CLLicenses.h"
#include "driver/CLMain.h"
#include "driver/CLPlugins.h"
#include "driver/Generic.h"
#include "driver/CWPluginsPrivate.h"
#include "driver/MacSpecs.h"
#include <string.h>

// (static, as in Pro7; inside extern "C" so that they keep their C names)
extern "C" {
static CW_CALLBACK CallPlugin(Plugin* plugin) {
    return Plugin_Call(plugin, plugin->context);
}

static void SetupPluginContext(Plugin* plugin, CWResult request) {
    OSSpec spec;
    shellContextType* sc;
    OSPathSpec cwd;

    sc = static_cast<shellContextType*>(plugin->context->shellContext);
    sc->plugin = plugin;
    // (2.6 chains the assignments here and to the OS errors below, where Pro7 has two each)
    sc->userAccessPathsChanged = sc->systemAccessPathsChanged = TRUE;
    plugin->context->request = request;
    plugin->context->apiVersion = 12;
    plugin->context->pluginStorage = NULL;

    OS_MakeFileSpec("Project.mcp", &spec);
    OS_OSSpec_To_FSSpec(&spec, &plugin->context->projectFile);
    if (gTarg) {
        OS_MakeSpecWithPath(&gTarg->outputDirectory, NULL, 0, &spec);
        OS_OSSpec_To_FSSpec(&spec, &plugin->context->outputFileDirectory);
    } else {
        OS_GetCWD(&cwd);
        OS_MakeSpecWithPath(&cwd, NULL, 0, &spec);
        OS_OSSpec_To_FSSpec(&spec, &plugin->context->outputFileDirectory);
    }

    plugin->context->shellSignature = 'CWIE';
    plugin->context->pluginType = Plugin_GetDropInFlags(plugin)->dropintype;

    if (gTarg) {
        plugin->context->numFiles = Files_Count(&gTarg->files);
    } else {
        plugin->context->numFiles = 0;
    }

    if (gTarg) {
        plugin->context->numOverlayGroups = Overlays_CountGroups(&gTarg->linkage.overlays);
    } else {
        plugin->context->numOverlayGroups = 0;
    }

    plugin->context->callbackOSError = plugin->context->pluginOSError = noErr;
    plugin->context->accessPathList = NULL;
}
}

Boolean SendParserRequest(
    Plugin* plugin,
    Target* target,
    CWCommandLineArgs* args,
    UInt32 cpu,
    UInt32 os,
    int numPlugins,
    const CLPluginInfo* pluginInfo,
    int numPanels,
    const char** panelNames,
    CWCommandLineArgs* plugin_args,
    CWCommandLineArgs* panel_args,
    const char* build_date,
    const char* build_time,
    const ToolVersionInfo* build_tool
) {
    int result;
    CWParserContext* pc;

    SetupPluginContext(plugin, 1);

    pc = static_cast<CWParserContext*>(plugin->context);
    pc->build_date = build_date;
    pc->build_time = build_time;
    pc->build_tool = build_tool;
    pc->args = args;
    pc->cpu = cpu;
    pc->os = os;
    pc->numPlugins = numPlugins;
    pc->plugins = pluginInfo;
    pc->numPanels = numPanels;
    pc->panelNames = panelNames;
    pc->plugin_args = plugin_args;
    pc->panel_args = panel_args;

    if (CallPlugin(plugin) != cwNoErr) {
        return FALSE;
    }
    if (!SendTargetInfoRequest(target, target->linker, target->linkerDropinFlags)) {
        return FALSE;
    }

    target->linkmodel = (target->targetinfo->linkType == exelinkageSegmented) ? LinkModel1
        : (target->targetinfo->linkType == exelinkageOverlay1)                ? LinkModel2
                                                                              : LinkModel0;

    if (!Plugin_VerifyPanels(plugin)) {
        return FALSE;
    }

    SetupPluginContext(plugin, 2);
    pc->args = args;
    pc->cpu = cpu;
    pc->os = os;
    pc->numPlugins = numPlugins;
    pc->plugins = pluginInfo;
    pc->numPanels = numPanels;
    pc->panelNames = panelNames;
    pc->plugin_args = plugin_args;
    pc->panel_args = panel_args;

    result = CallPlugin(plugin);
    return (result == cwNoErr);
}

Boolean SendCompilerRequest(Plugin* plugin, File* file, SInt16 stage) {
    CWResult result;
    CWCompilerLinkerContext* cc;
    Boolean precompiling;
    SInt16 type;
    OSType mactype;
    const char* pn;
    int v;

    if (stage & CmdLineStageMask_Cg) {
        if (optsCompiler.forcePrecompile == 1) {
            precompiling = TRUE;
        } else if (optsCompiler.forcePrecompile == 2) {
            precompiling = FALSE;
        } else {
            precompiling = (file->mappingflags & kPrecompile) != 0;
        }
    } else {
        precompiling = FALSE;
    }

    if (optsCmdLine.verbose) {
        pn = Plugin_GetDropInName(plugin);
        v = optsCmdLine.verbose > 1;
        if (stage & CmdLineStageMask_Pp) {
            CLReport(CLStr34 + v, file->srcfilename, pn);
        } else if (stage & CmdLineStageMask_Cg) {
            if (precompiling) {
                CLReport(CLStr32 + v, file->srcfilename, pn);
            } else {
                CLReport(CLStr30 + v, file->srcfilename, pn);
            }
        } else if (stage & CmdLineStageMask_Dp) {
            CLReport(CLStr36 + v, file->srcfilename, pn);
        } else if (stage & CmdLineStageMask_Ds) {
            CLReport(CLStr46 + v, file->srcfilename, pn);
        } else if (!stage) {
            CLReport(CLStr48 + v, file->srcfilename, pn);
        }
    }

    if (!Plugin_VerifyPanels(plugin)) {
        return FALSE;
    }

    SetupPluginContext(plugin, reqCompile);
    cc = static_cast<CWCompilerLinkerContext*>(plugin->context);
    *cc->targetinfo = *gTarg->targetinfo;

    OS_OSSpec_To_FSSpec(&file->srcfss, &cc->sourcefile);
    if (!OS_GetMacFileType(&file->srcfss, &mactype) && mactype == 'TEXT') {
        if (UCBGetFileText(cc, &cc->sourcefile, &cc->sourcetext, &cc->sourcetextsize, &type) !=
            cwNoErr) {
            return FALSE;
        }
    } else {
        cc->sourcetext = NULL;
        cc->sourcetextsize = 0;
    }

    cc->whichfile = file->filenum;
    cc->debuginfo = optsCmdLine.debugInfo;
    cc->recordbrowseinfo = file->recordbrowseinfo;
    cc->fileID = file->browseFileID;
    memcpy(&cc->browseoptions, &file->browseoptions, sizeof(cc->browseoptions));

    if (stage & (CmdLineStageMask_Pp | CmdLineStageMask_Dp)) {
        if (stage & CmdLineStageMask_Pp) {
            cc->preprocess = 1;
        } else {
            cc->preprocess = 2;
        }
    } else {
        cc->preprocess = 0;
    }

    if ((stage & CmdLineStageMask_Cg) && precompiling) {
        cc->precompile = 1;
        cc->autoprecompile = 1;
    } else {
        cc->precompile = 0;
        cc->autoprecompile = 0;
    }

    if (!(stage & (CmdLineStageMask_Pp | CmdLineStageMask_Cg | CmdLineStageMask_Dp))) {
        cc->preprocess = 2;
        cc->precompile = 2;
    }

    result = CallPlugin(plugin);

    if (cc->sourcetext) {
        UCBReleaseFileText(cc, cc->sourcetext);
        cc->sourcetext = NULL;
        cc->sourcetextsize = 0;
    }

    return (result == cwNoErr);
}

Boolean SendTargetInfoRequest(Target* targ, Plugin* linker, SInt32 dropinflags) {
    CWResult result;
    CWCompilerLinkerContext* cc;
    OSSpec spec;
    CWFileSpec fss;

    if (linker && !(dropinflags & isExecutableTool)) {
        if (optsCmdLine.verbose > 1) {
            CLReport(CLStr50, Plugin_GetDropInName(linker));
        }
        OS_ASSERT(298, Plugin_GetPluginType(linker) == CWDROPINLINKERTYPE);

        if (!Plugin_VerifyPanels(linker)) {
            return FALSE;
        }

        SetupPluginContext(linker, reqTargetInfo);
        cc = static_cast<CWCompilerLinkerContext*>(linker->context);
        *cc->targetinfo = *targ->targetinfo;
        result = CallPlugin(linker);
        *targ->targetinfo = *cc->targetinfo;
    } else {
        OS_MakeFileSpec("(unknown file)", &spec);
        OS_OSSpec_To_FSSpec(&spec, &fss);
        targ->targetinfo->outputType = linkOutputFile;
        targ->targetinfo->outfile = fss;
        targ->targetinfo->symfile = fss;
        targ->targetinfo->runfile = fss;
        targ->targetinfo->linkType = exelinkageFlat;
        targ->targetinfo->canRun = FALSE;
        targ->targetinfo->canDebug = FALSE;
        targ->targetinfo->targetCPU = targ->cpu;
        targ->targetinfo->targetOS = targ->os;
        targ->targetinfo->outfileCreator = 'CWIE';
        targ->targetinfo->outfileType = 'APPL';
        targ->targetinfo->debuggerCreator = 'MWDB';
        targ->targetinfo->runHelperCreator = '?\?\?\?'; // ('????', escaped from the trigraphs)
        result = cwNoErr;
    }

    return (result == cwNoErr);
}

Boolean SendLinkerRequest(Plugin* plugin, SInt32 dropinflags, CWTargetInfo* targetInfo) {
    CWResult result;
    CWCompilerLinkerContext* cc;
    const char* nm;
    int v;

    if (optsCmdLine.verbose) {
        nm = Plugin_GetDropInName(plugin);
        v = optsCmdLine.verbose > 1;
        if (dropinflags & isPostLinker) {
            CLReport(CLStr44 + v, nm);
        } else if (dropinflags & isPreLinker) {
            CLReport(CLStr42 + v, nm);
        } else {
            CLReport(CLStr40 + v, nm);
        }
    }

    if (!Plugin_VerifyPanels(plugin)) {
        return FALSE;
    }

    SetupPluginContext(plugin, reqLink);
    cc = static_cast<CWCompilerLinkerContext*>(plugin->context);
    *cc->targetinfo = *targetInfo;
    result = CallPlugin(plugin);
    *targetInfo = *cc->targetinfo;

    return (result == cwNoErr);
}

Boolean SendDisassemblerRequest(Plugin* linker, File* file) {
    CWResult result;
    Plugin* plugin;

    if (linker == NULL) {
        plugin = file->compiler;
    } else {
        plugin = linker;
    }

    if (optsCmdLine.verbose) {
        CLReport(
            (optsCmdLine.verbose > 1) + CLStr46, file->srcfilename, Plugin_GetDropInName(linker)
        );
    }

    if (!Plugin_VerifyPanels(plugin)) {
        return FALSE;
    }

    if (!linker) {
        SetupPluginContext(plugin, reqCompDisassemble);
        static_cast<CWCompilerLinkerContext*>(plugin->context)->whichfile = file->filenum;
        result = CallPlugin(plugin);
    } else {
        SetupPluginContext(plugin, reqDisassemble);
        static_cast<CWCompilerLinkerContext*>(plugin->context)->whichfile = file->filenum;
        result = CallPlugin(plugin);
    }

    return (result == cwNoErr);
}

Boolean SendInitOrTermRequest(Plugin* plugin, Boolean reqIsInitialize) {
    CWResult result;
    OSType ty;

    result = cwErrRequestFailed;

    if (plugin) {
        if (clState.pluginDebug) {
            CLReport(reqIsInitialize ? CLStr51 : CLStr52, Plugin_GetDropInName(plugin));
        }

        if (reqIsInitialize) {
            ty = Plugin_GetDropInFlags(plugin)->dropintype;
            if (ty == CWDROPINDRIVERTYPE) {
                plugin->context = new CWPluginPrivateContext(ty);
            } else if (ty == CWDROPINPARSERTYPE) {
                plugin->context = new CWParserContext;
            } else if (ty == CWDROPINCOMPILERTYPE || ty == CWDROPINLINKERTYPE) {
                plugin->context = new CWCompilerLinkerContext;
            } else {
                plugin->context = NULL;
            }

            plugin->context->shellContext = new shellContextType;
        }

        if (plugin->context) {
            SetupPluginContext(plugin, reqIsInitialize ? reqInitialize : reqTerminate);
            result = CallPlugin(plugin);
        } else {
            result = cwNoErr;
        }

        if (!reqIsInitialize) {
            License_AutoCheckin();
        }
    }

    return (result == cwNoErr);
}

void AddFileTypeMappingList(OSFileTypeMappings** list, const OSFileTypeMappingList* entry) {
    OS_AddFileTypeMappingList(list, entry);
}
