#pragma optimization_level 1
#pragma opt_propagation off
#include "input.h"
extern unsigned observe;

unsigned source_initializer(unsigned input) {
    unsigned flag = input;
    observe = flag + SOURCE_GUIDED_OFFSET;
    return flag;
}
