#ifndef VERSION_H
#define VERSION_H

#define VERSION_GC_1_0 0
#define VERSION_GC_1_1 1
#define VERSION_GC_1_1P1 2
#define VERSION_GC_1_2_5 3
#define VERSION_GC_1_2_5N 4
#define VERSION_GC_1_3 5
#define VERSION_GC_1_3_2 6

/* configure.py passes -DVERSION=<index> */
#ifndef VERSION
#define VERSION VERSION_GC_1_2_5
#endif

/* a value that differs between the versions: BY_VERSION(v1.2.5, v1.3) */
#if VERSION >= VERSION_GC_1_3
#define BY_VERSION(v125, v13) v13
#else
#define BY_VERSION(v125, v13) v125
#endif

/* const only in 1.3 (the optimizer's CInt64 constants, which 1.3 reads where an inline uses them) */
#define CONST_13 BY_VERSION(, const)

/* a token value above 283: shifted by one in 1.3 */
#define TKN(n) BY_VERSION(n, (n) + 1)

#endif
