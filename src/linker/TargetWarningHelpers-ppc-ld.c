/* The linker's warning options (the 2.x linker's version of TargetWarningHelpers-ppc-cc.c): -w with 'Aw' turns
   the linker's own warnings off. */
#include "compiler/common.h"
#include "driver/PrefPanels.h"
#include "driver/StringUtils.h"

/* (the linker panel's byte that turns the linker's warnings off: ParserGlue-eabi-ppc-ld.c's) */
extern PLinker pLinker;

int set_warning_option(short option, char enabled)
{
    if (option == 0x4177) {
        pLinker.unk05 = !enabled;
        return 1;
    }
    return 0;
}

void ToolHelpers_ld_PrintLinkerWarningOptions(void *self)
{
    if (pLinker.unk05)
        HPrintF(self, "\t- no linker warnings\n");
}
