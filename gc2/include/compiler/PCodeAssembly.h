#ifndef COMPILER_PCODEASSEMBLY_H
#define COMPILER_PCODEASSEMBLY_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// The relocation assemblepcode asks for (Pro7's): its kind in Pro7's MWReloc numbering, which
// ObjGen's emit_relocation maps to an ELF relocation (-1 for none), its object and addend.
// TODO: invented member names (Pro7's are the placeholders x2 and x6)
// (2-byte packing: object at offset 2 and addend at 6, Pro7's x2 and x6)
#pragma pack(push, 2)
struct WeirdOperand {
    SInt16 type;
    struct Object* object;
    UInt32 addend;
};
#pragma pack(pop)

struct EntryPoint {
    struct EntryPoint* next;
    struct Object* object;
    struct PCodeBlock* block;
};

extern UInt32 assemblepcode(PCode* instr, UInt32 offset, WeirdOperand* wop);
extern SInt32 optimizefinalbranches(SInt32 codesize);
extern SInt32 assemblefunction(Object* func, EntryPoint* entrypoints);

#ifdef __cplusplus
}
#endif

#endif
