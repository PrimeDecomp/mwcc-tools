#include "driver/ParserHelpers-cc.h"
#include "driver/ParserErrors.h"
#include "driver/ParserFace.h"
#include "driver/ParserHelpers.h"
#include "driver/Targets.h"
#include <stdio.h>
#include <string.h>

Handle definesHandle;

int Opt_AddStringToDefines(const char* opt, void* str, const char* param, int flags) {
    AddStringToHandle(&definesHandle, (const char*)str);
    if (param) {
        AddStringToHandle(&definesHandle, param);
    }
    return 1;
}

int Opt_DefineSymbol(const char* var, const char* value) {
    char tmp[1024];

    if (pTool->LANG == Lang_C_CPP || pTool->LANG == 'Asm ' || pTool->LANG == Lang_Rez) {
        sprintf(tmp, "#define %s %s\n", var, value ? value : "1");
    } else if (pTool->LANG == Lang_Pascal) {
        sprintf(tmp, "{$definec %s %s}\n", var, value ? value : "1");
    } else {
        sprintf(tmp, "Option '-D|d' is not supported with this plugin");
        CLPReportError(CLPStr28_WarningText, tmp);
        return 0;
    }

    AddStringToHandle(&definesHandle, tmp);
    return 1;
}

int Opt_UndefineSymbol(const char* opt, void* var, const char* arg, int flags) {
    char tmp[300];

    if (pTool->LANG == Lang_C_CPP || pTool->LANG == 'Asm ') {
        sprintf(tmp, "#undef %s\n", arg);
    } else if (pTool->LANG == Lang_Pascal) {
        sprintf(tmp, "{$undefc %s}\n", arg);
    } else {
        sprintf(tmp, "Option -%s is not supported with this plugin", opt);
        CLPReportError(CLPStr28_WarningText, tmp);
        return 0;
    }

    AddStringToHandle(&definesHandle, tmp);
    return 1;
}

int Opt_AddPrefixFile(const char* opt, void* handle, const char* filename, int flags) {
    char tmp[300];
    // (2.6 picks the handle into a local of its own, where Pro7 reassigns the parameter)
    Handle* storage;

    if (!handle) {
        storage = &definesHandle;
    } else {
        storage = (Handle*)handle;
    }

    if (!filename[0]) {
        return 1;
    }

    if (pTool->LANG == Lang_C_CPP || pTool->LANG == 'Asm ') {
        if (filename[0] == '<' && filename[strlen(filename) - 1] == '>') {
            snprintf(tmp, sizeof(tmp), "#include %s\n", filename);
        } else {
            snprintf(tmp, sizeof(tmp), "#include \"%s\"\n", filename);
        }
    } else if (pTool->LANG == Lang_Pascal) {
        snprintf(tmp, sizeof(tmp), "{$I+}\n{$I %s}\n{$I-}\n", filename);
    } else {
        sprintf(tmp, "Option -%s is not supported with this plugin", opt);
        CLPReportError(CLPStr28_WarningText, tmp);
        return 0;
    }

    AddStringToHandle(storage, tmp);
    return 1;
}

int SetupPragmas(const Pragma* pragmas) {
    const char* set;
    char on;
    char off;
    Boolean reverse;
    char tmp[300];

    while (pragmas->pragma) {
        if (pragmas->flags == PRAGMA_FLAGS_0 || pragmas->flags == PRAGMA_FLAGS_1) {
            set = 0;
            // (2.6 tests the flag once into a Boolean, where Pro7 compares it in each line)
            reverse = pragmas->flags == PRAGMA_FLAGS_1;
            on = !reverse ? PR_ON : PR_OFF;
            off = !reverse ? PR_OFF : PR_ON;
            if (*((char*)pragmas->value) == on) {
                set = "on";
            } else if (*((char*)pragmas->value) == off) {
                set = "off";
            } else if (*((char*)pragmas->value) == PR_AUTO) {
                set = "auto";
            } else if (*((char*)pragmas->value) == PR_RESET) {
                set = "reset";
            } else {
                OS_ASSERT(186, *((char *)pragmas->value) == PR_UNSET);
            }

            if (set) {
                snprintf(tmp, sizeof(tmp), "#pragma %s %s\n", pragmas->pragma, set);
                AddStringToHandle(&definesHandle, tmp);
            }
        } else {
            OS_ASSERT(195, !"Can't handle pragma");
        }
        pragmas++;
    }

    return 1;
}
