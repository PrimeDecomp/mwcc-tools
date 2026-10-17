// The C++ allocation the driver's C++ sources call (MSL's runtime has it as newop.cpp, in the MWCC ABI)
#include <stdlib.h>

void* operator new(unsigned long size)
{
    return malloc(size ? size : 1);
}

void operator delete(void* p)
{
    free(p);
}
