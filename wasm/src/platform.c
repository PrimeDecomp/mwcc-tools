/* What the wasm build links in place of the x86 build's assembly and of the DLLs besides kernel32 and version. */
#include <stdlib.h>
#include <string.h>
#include "driver/LicenseImports.h"

double fabs(double x)
{
    return __builtin_fabs(x);
}

/* CLPlugins.c's two uses: a plug-in's information and its panel list, which its caller frees with the frame. The
 * heap does as well (they are not freed). */
void* alloca(unsigned int size)
{
    return malloc(size);
}

/* LMGR326B.dll: gc2's stand-in (tools/lmgr_stub), whose checkout always succeeds */
int lp_checkout(LPCODE_HANDLE* code, int policy, const char* feature, const char* version, int nlic,
    const char* licensePath, LP_HANDLE** handle)
{
    *handle = (LP_HANDLE*)1234;
    return 0;
}

void lp_checkin(LP_HANDLE* handle)
{
}

char* lp_errstring(LP_HANDLE* handle)
{
    return "";
}

/* MSL extras.c's (which the wasm build leaves out: its _alloca is assembly) */
char* _itoa(int value, char* str, int radix)
{
    char digits[34];
    int i = 0, negative = radix == 10 && value < 0;
    unsigned int v = negative ? 0u - (unsigned int)value : (unsigned int)value;
    do {
        digits[i++] = "0123456789abcdefghijklmnopqrstuvwxyz"[v % (unsigned int)radix];
        v /= (unsigned int)radix;
    } while (v);
    if (negative) {
        digits[i++] = '-';
    }
    {
        char* p = str;
        while (i) {
            *p++ = digits[--i];
        }
        *p = 0;
    }
    return str;
}
