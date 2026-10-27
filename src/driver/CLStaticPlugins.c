#define CERROR_FILE "unknown.c"
#include "version.h"
#include "compiler/common.h"
#include "driver/CLStaticPlugins.h"
#include "driver/libimp-eabi-ppc-mw.h"

unsigned int CLStaticPlugins_SetIdentifiers(SInt32 *architectureIdentifier, SInt32 *abiIdentifier)
{
#if VERSION >= VERSION_GC_1_3
    *architectureIdentifier = 0x65505043U;
#else
    *(unsigned int *)architectureIdentifier = 0x65505043U;
#endif
    *abiIdentifier = 0x45414249U;
    return (unsigned int)abiIdentifier;
}

unsigned int fn_004053a0(SInt32 *pluginType, SInt32 *pluginSubtype)
{
#if VERSION >= VERSION_GC_1_3
    *pluginType = '****';
#else
    *(unsigned int *)pluginType = '****';
#endif
#if defined(LINKER) && VERSION >= VERSION_GC_2_7
    *pluginSubtype = 'Link';
#else
    *pluginSubtype = 'Comp';
#endif
    return (unsigned int)pluginSubtype;
}

void fn_004053c0(SInt32 *pluginID)
{
    *pluginID = 'Seep';
}

unsigned int fn_004053d0(void)
{
    int success;

    success = 0;
    if (fn_0040bed0() != 0 && fn_0040c060() != 0) {
        success = 1;
    }
    return success;
}

int fn_004053f0(void)
{
    return fn_0040bf10() && fn_0040c070();
}
