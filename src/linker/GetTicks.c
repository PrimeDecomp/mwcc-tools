/* The ticks (60ths of a second) the linker's break checks count, in a unit of their own in 2.x (1.3's are
   COSToolsCLT.c's). */
#include "compiler/common.h"
#include "driver/MsDos.h"

unsigned int CompilerTools_GetTicks(void)
{
    return OS_GetMilliseconds() * 0x3cu / 1000;
}
