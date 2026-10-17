#ifndef VERSION_H
#define VERSION_H

// The GameCube compilers of the 2.x codegen line (C/C++ 2.4.x, built by the CodeWarrior Pro 6 x86
// compiler), in release order. configure.py passes -DVERSION=<index>. The sources are 2.6's; a
// version that differs tests VERSION. GC_2_0p2 is not a release: 2.0p1 with the treatment of
// floating-point literals that SpongeBob: Battle for Bikini Bottom's code shows (src/optimizer/Alias.c),
// built from the sources alone (config/GC_2_0p2).
#define VERSION_GC_2_0 0
#define VERSION_GC_2_0P1 1
#define VERSION_GC_2_0P2 2
#define VERSION_GC_2_5 3
#define VERSION_GC_2_6 4
#define VERSION_GC_2_7 5

#ifndef VERSION
#define VERSION VERSION_GC_2_6
#endif

#endif
