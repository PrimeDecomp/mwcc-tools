#ifndef COMPILER_USEDEFCHAINS_H
#define COMPILER_USEDEFCHAINS_H

// UseDefChains.c: the use-def chains of registers and memory over the PCode (Pro7's UseDefChains.c,
// deferred). 2.6 counts each object's direct and indirect uses and definitions, and keeps a block's
// local sets as a range (gen) and a compressed vector (kill).

#include "compiler/common.h"
#include "compiler/BitVectors.h"
#include "compiler/PCode.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
// Pro7's TinyValue and UseOrDef (mac68k alignment: 10 bytes)
typedef struct TinyValue {
    PCOpKind kind;
    RegClass arg; // a register's class; for memory 0 (the object itself) or 1 (may alias it)
    union {
        short reg;
        Object* object;
    } u;
} TinyValue;

struct UseOrDef {
    PCode* pcode;
    TinyValue v;
};
#pragma pack(pop)

struct RegUseOrDef {
    RegUseOrDef* next;
    int id;
};

struct ObjectUseDef {
    ObjectUseDef* next;
    ObjectUseDef* leftchild;
    ObjectUseDef* rightchild;
    Object* object;
    RegUseOrDef* uses;
    RegUseOrDef* defs;
#if VERSION >= VERSION_GC_2_5
    // 2.6
    short directdefs;
    short directuses;
    short indirectdefs;
    short indirectuses;
#endif
};

typedef struct UseDefInfo {
#if VERSION >= VERSION_GC_2_5
    // 2.6: the block's uses and definitions are [usestart, usestart + usecount) and likewise
    SInt32 usestart;
    SInt32 usecount;
    SInt32 defstart;
    SInt32 defcount;
#endif
    // TODO: invented names (Pro7's defvec0, defvec4, defvec8, defvecC, usevec10, usevec14, usevec18,
    // usevec1C: their Pro7 offsets): the reaching definitions and the live uses of the block
#if VERSION >= VERSION_GC_2_5
    BitVectorRange* defgen; // 2.6: a range over the block's definitions (2.0: a full vector)
    CompressedBitVector* defkill; // 2.6: compressed (2.0: a full vector)
#else
    UInt32* defgen;
    UInt32* defkill;
#endif
    UInt32* defin;
    UInt32* defout;
#if VERSION >= VERSION_GC_2_5
    BitVectorRange* usegen; // 2.6: a range over the block's uses
    CompressedBitVector* usekill; // 2.6: compressed
#else
    UInt32* usegen;
    UInt32* usekill;
#endif
    UInt32* usein;
    UInt32* useout;
} UseDefInfo;

extern int number_of_Defs;
extern UseOrDef* Defs;
extern RegUseOrDef** reg_Defs[RegClassMax];
extern int number_of_Uses;
extern UseOrDef* Uses;
extern RegUseOrDef** reg_Uses[RegClassMax];
extern UseDefInfo* usedefinfo;

extern ObjectUseDef* findobjectusedef(Object* object);
extern void computeusedefchains(int flag);
// 2.6
extern int precedes(PCode* a, PCode* b);
extern int canidate_reaches_use(
    PCode* candidate, int defID, int reg, PCode* use, UInt32* defvec, RegClass rclass
);

#ifdef __cplusplus
}
#endif

#endif
