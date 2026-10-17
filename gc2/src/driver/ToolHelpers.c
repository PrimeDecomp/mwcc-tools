#include "driver/ToolHelpers.h"
#include "driver/CWParserPluginsPrivate.h"
#include "driver/CWPluginsPrivate.h"
#include "driver/Generic.h"
#include "driver/IO.h"
#include "driver/MacSpecs.h"
#include "driver/MsDos.h"
#include "driver/ParserErrors.h"
#include "driver/ParserFace.h"
#include "driver/ParserHelpers.h"
#include "driver/pref_structs.h"
#include "driver/Projects.h"
#include "driver/Targets.h"
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static Boolean setStage;
// (2.6: 264 bytes, where Pro7 has 256; the COMMON's size, which the 8-byte group it is laid out in
// shows)
char linkerOutputFilename[264];
Boolean setLinkerOutputFilename;
Boolean setOutputDirectory;
SInt16 outputOrdering;

int Opt_HandleOutputName(const char* opt, void* var, const char* filename, int flags) {
    OSSpec spec;
    int err;
    SInt32 result;
    Boolean isfile;

    if (!filename) {
        filename = opt;
    }

    if ((pCmdLine.state ==
         OptsCmdLineState_CompileAndLink ||
         (pCmdLine.state == OptsCmdLineState_Unset && pTool->TYPE == CWDROPINLINKERTYPE)) &&
        pCmdLine.toDisk != 4) {
        if (setLinkerOutputFilename) {
            CLPReportError(CLPStr41, filename);
            return 0;
        }
        setLinkerOutputFilename = TRUE;
        if (pTool->TYPE == CWDROPINCOMPILERTYPE) {
            strncpy(linkerOutputFilename, filename, 256);
        } else {
            OSSpec spec;
            int err;
            Boolean isfile;
            err = OS_MakeSpec(filename, &spec, &isfile);
            if (err) {
                CLPOSAlert(CLPStr64, err, filename);
                return 0;
            }
            if (isfile) {
                OS_NameSpecToString(&spec.name, linkerOutputFilename, 260);
            }
            SetOutputFileDirectory(&spec.path);
        }
        return 1;
    } else {
        err = OS_MakeSpec(filename, &spec, &isfile);
        if (err) {
            CLPOSAlert(CLPStr64, err, filename);
            return 0;
        }

        if (!err && !isfile) {
            FSSpec ofd;
            if (setOutputDirectory) {
                CLPReportError(CLPStr59, filename);
                return 0;
            }
            setOutputDirectory = TRUE;

            OS_OSSpec_To_FSSpec(&spec, &ofd);
            result = CWParserSetOutputFileDirectory(parseopts.context, &ofd);
            if (result) {
                failedCallback = "CWParserSetOutputFileDirectory";
                longjmp(exit_plugin, result);
            }
            return 1;
        } else {
            SInt32 index;
            CWProjectFileInfo pfi;
            if (parseopts.lastoutputname[0]) {
                CLPReportError(CLPStr41, filename);
                return 0;
            }
            strncpy(parseopts.lastoutputname, filename, 256);
            if (pCmdLine.stages == CmdLineStageMask_Dp) {
                return 1;
            }

            if (outputOrdering == OutputOrdering_None || outputOrdering == OutputOrdering_After) {
                CWGetProjectFileCount(parseopts.context, &index);
                while (index-- > 0) {
                    if (!CWGetFileInfo(parseopts.context, index, 0, &pfi) &&
                        pfi.filetype == 'TEXT') {
                        outputOrdering = OutputOrdering_After;
                        break;
                    }
                }

                if (index <= 0) {
                    CWGetProjectFileCount(parseopts.context, &index);
                    if (index > 0) {
                        outputOrdering = OutputOrdering_After;
                    }
                    --index;
                }

                if (outputOrdering == OutputOrdering_None) {
                    outputOrdering = OutputOrdering_Before;
                    return 1;
                }

                SetFileOutputName(index, lastStage, parseopts.lastoutputname);
                parseopts.lastoutputname[0] = 0;
            }

            return 1;
        }
    }
}

int ValidateToolState(Boolean mustHaveFiles) {
    if (parseopts.noOptions) {
        pCmdLine.state = OptsCmdLineState_NoAction;
        ShowVersion(1);
        return 1;
    }

    if (parseopts.success) {
        if (!parseopts.userSpecifiedFiles) {
            if (!parseopts.hadAnyOutput) {
                if (mustHaveFiles) {
                    CLPReportError(CLPStr70);
                    return 0;
                }
            } else {
                if (mustHaveFiles) {
                    pCmdLine.state = OptsCmdLineState_NoAction;
                }
                return 1;
            }
        } else if (parseopts.unusedFiles > 0) {
            CLPReportError(CLPStr69);
            return 0;
        }
    }

    if (pCmdLine.state ==
        OptsCmdLineState_Unset ||
        (parseopts.userSpecifiedFiles > 0 && pCmdLine.state == OptsCmdLineState_NoAction)) {
        pCmdLine.state = OptsCmdLineState_CompileAndLink;
    }

    if (!setStage) {
        pCmdLine.stages = CmdLineStageMask_Cg;
        if (pCmdLine.state == OptsCmdLineState_Compile) {
            pCmdLine.toDisk |= 2;
        }
    }

    if (pCmdLine.state ==
        OptsCmdLineState_CompileAndLink &&
        !(pCmdLine.stages & CmdLineStageMask_Cg)) {
        pCmdLine.state = OptsCmdLineState_Compile;
    }

    return 1;
}

int Opt_IncreaseVerbosity(const char* opt, void* var, const char* arg, int flags) {
    if (pCmdLine.verbose) {
        pCmdLine.verbose++;
    } else {
        pCmdLine.verbose = 2;
    }

    Parser_StorePanels(parseopts.context);
    return 1;
}

int Opt_SetStage(const char* opt, void* str, const char* arg, int flags) {
    unsigned char* ptr;
    Boolean set;
    Boolean no;
    UInt16 flag;

    ptr = (unsigned char*)str;
    // (flag 8: the option was given with its "no" prefix; 2.6 tests it, where Pro7 shifts it down)
    if (flags & 8) {
        no = TRUE;
    } else {
        no = FALSE;
    }
    set = (Boolean)(no ^ 1);

    while (*ptr) {
        if (*ptr == '+') {
            set = !no;
        } else if (*ptr == '-') {
            set = no;
        } else if (*ptr == '|') {
            set = (Boolean)(no ^ 1);
        } else {
            flag = (ptr[0] << 8) | ptr[1];
            setStage = TRUE;

            switch (flag) {
                case 'Cg':
                    if (set) {
                        pCmdLine.stages |= CmdLineStageMask_Cg;
                    } else {
                        pCmdLine.stages &= ~CmdLineStageMask_Cg;
                    }
                    lastStage = CmdLineStage_Cg;
                    break;
                case 'Ds':
                    if (set) {
                        pCmdLine.stages |= CmdLineStageMask_Ds;
                    } else {
                        pCmdLine.stages &= ~CmdLineStageMask_Ds;
                    }
                    lastStage = CmdLineStage_Ds;
                    break;
                case 'Pp':
                    if (set) {
                        pCmdLine.stages |= CmdLineStageMask_Pp;
                    } else {
                        pCmdLine.stages &= ~CmdLineStageMask_Pp;
                    }
                    lastStage = CmdLineStage_Pp;
                    break;
                case 'Dp':
                    if (set) {
                        pCmdLine.stages |= CmdLineStageMask_Dp;
                    } else {
                        pCmdLine.stages &= ~CmdLineStageMask_Dp;
                    }
                    break;
                default:
                    CLPFatalError("Bad stage settings in %s (%c%c)\n", str, ptr[0], ptr[1]);
            }

            ++ptr;
        }

        ++ptr;
    }

    return 1;
}

// (no caller: the linker dropped it, but its "wt" stays in the unit's .data)
int Opt_RedirectStream(const char* opt, void* file, const char* filename) {
    OSSpec spec;
    int err;
    FILE* nw;

    err = OS_MakeFileSpec(filename, &spec);
    if (err) {
        CLPReportError(CLPStr78, filename, OS_GetErrText(err));
        return 0;
    }

    nw = freopen(OS_SpecToString(&spec, STSbuf, sizeof(STSbuf)), "wt", (FILE*)file);
    if (!nw) {
        CLPReportError(CLPStr78, filename, strerror(errno));
        return 0;
    }

    return 1;
}
