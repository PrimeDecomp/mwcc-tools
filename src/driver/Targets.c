#include "compiler/common.h"
#include "driver/Targets.h"
#include "compiler/objects.h"
#include "driver/AssertionFailure.h"
#include "driver/ClientGlue.h"
#include "driver/Option.h"
#include "driver/ParserErrors.h"
#include "driver/ParserFace.h"

#if VERSION >= VERSION_GC_1_3
extern UInt32 parser_panel_extra; /* (parseopts.toolVersion) */
#endif
int Targets_SetTool(ParserTool *tool)
{
    pTool = tool;
#if VERSION >= VERSION_GC_1_3
    ((pTool->toolInfo && (parser_panel_extra || pTool->copyright))
         ? (void)0
         : CLIO_ReportAssertionFailure("pTool->toolInfo && (parseopts.toolVersion || pTool->copyright)", "Targets.c",
                                       16));
#else
    ((pTool->toolInfo && pTool->copyright)
         ? (void)0
         : CLIO_ReportAssertionFailure("pTool->toolInfo && pTool->copyright", "Targets.c", 16));
#endif
    return 1;
}

#define MATCH(x, y) ((x) == 0x2a2a2a2a || (y) == 0x2a2a2a2a || (y) == (x))
Boolean Targets_MatchTool(UInt32 type, UInt32 lang, UInt32 cpu, UInt32 os)
{
    if (!pTool) {
        Targets_ForwardVarArgsAndLongjmp("No options loaded for command line\n");
        return 0;
    }
    if (MATCH(type, pTool->tool) && MATCH(lang, pTool->lang) && MATCH(cpu, pTool->cpu) && MATCH(os, pTool->os))
        return 1;
    return 0;
}

#undef CERROR_FILE
#define CERROR_FILE "options.c"

Boolean Targets_MatchCommandLineOptions(int argc, char **argv)
{
    int i, j;
    Boolean ok;

    if (pTool == NULL)
        Targets_ForwardVarArgsAndLongjmp("No options loaded for command line\n");

    for (i = 0; i < argc; i++) {
        for (j = 0; j < pTool->numPrefPanels; j++) {
            if (BY_VERSION(ClientGlue_CompareLowercaseStrings(pTool->prefPanels[j], argv[i]) == 0, CLIO_STRINGS_EQUAL(pTool->prefPanels[j], argv[i])))
                break;
        }
        if (j >= pTool->numPrefPanels)
            break;
    }

    if (i >= argc)
        ok = 1;
    else
        ok = 0;
    return ok;
}

#undef CERROR_FILE
#define CERROR_FILE __FILE__

BY_VERSION(int, char) Targets_RegisterOptionLists(void)
{
    int index;
    Option_ResetOptionLists();
    for (index = 0; index < pTool->numOptionLists; index++)
        Option_RegisterOptionList (pTool->optionLists[index]);
#if VERSION >= VERSION_GC_1_3
    Option_FinalizeOptionLists();
#endif
    return 1;
}
