#include "compiler/ScanFloat.h"
#include <errno.h>
#include <stdlib.h>

// Wii's ScanFloatX86 module (Pro7's ScanFloat.c, whose Mac version reports no failure): 2.6 reports
// an out-of-range value through errno (which, as built, is not the errno the linked strtod sets:
// <errno.h>)
char* ScanFloat(const char* str, double* result, Boolean* failed) {
    char* end;

    errno = 0;
    *result = strtod(str, &end);
    *failed = errno != 0;
    return end;
}
