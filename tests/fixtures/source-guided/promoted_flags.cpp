// Isolate RenderParticles' pointer-to-flag initialization and late array promotion.
// The post-loop counter test is intentional: it makes later constant propagation
// enable the array-to-register pass. Both array functions execute identical calls.
extern unsigned observe(unsigned);

unsigned scalar_flag(const void* condition, unsigned input) {
    unsigned flag = condition ? 0u : 1u;
    observe(input + 7);
    if (observe(input)) flag = 1u;
    observe(input + 1);
    return flag;
}

#pragma opt_unroll_loops off
unsigned first_promoted_flag(const void* condition, unsigned input) {
    unsigned flag[2];
    int k;
    for (k = 0; k < 1; ++k) flag[k] = condition ? 0u : 1u;
    if (k > 0) observe(input + 7);
    if (observe(input)) flag[0] = 1u;
    observe(input + 1);
    return flag[0];
}

unsigned second_promoted_flag(const void* condition, unsigned input) {
    unsigned flag[2];
    int k;
    for (k = 1; k < 2; ++k) flag[k] = condition ? 0u : 1u;
    if (k > 0) observe(input + 7);
    if (observe(input)) flag[1] = 1u;
    observe(input + 1);
    return flag[1];
}
#pragma opt_unroll_loops reset
