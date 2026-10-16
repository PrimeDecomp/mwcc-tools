#include "input.h"
extern unsigned observe(unsigned);

unsigned source_initializer(unsigned input) {
    unsigned flag = observe(input);
    if (observe(input + 1)) {
        flag = input + 2 + SOURCE_GUIDED_OFFSET;
    }
    return flag;
}
