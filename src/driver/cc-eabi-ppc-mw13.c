#include "compiler/common.h"
#include "driver/cc-eabi-ppc-mw.h"

#if VERSION >= VERSION_GC_1_3
extern unsigned int data_0058ca10;
extern unsigned int data_0058ca28;
extern unsigned int data_00594934;
extern unsigned int data_00594938;
#pragma scheduling off
unsigned int __stdcall get_global_value_0058ca10(unsigned int *value)
{
    *value = data_0058ca10;
    return 0U;
}

unsigned int __stdcall get_global_value_0058ca28(unsigned int *value)
{
    *value = data_0058ca28;
    return 0U;
}

unsigned int __stdcall get_global_value_00594934(unsigned int *value)
{
    *value = data_00594934;
    return 0U;
}

unsigned int __stdcall get_global_value_00594938(unsigned int *value)
{
    *value = data_00594938;
    return 0U;
}
#pragma scheduling reset
#endif
