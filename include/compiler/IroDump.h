#ifndef COMPILER_IRODUMP_H
#define COMPILER_IRODUMP_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// (IRO_DumpIntTree, IRO_DumpFlowgraph, IRO_DumpNode and IRO_SpellType: no other unit calls them, but
// Pro7 declares them here and Wii has them public)
extern void IRO_InitializeNodeNamesArray(void);
extern void IRO_DumpIntTree(IROLinear* linear);
extern void IRO_DumpBits(char* name, BitVector* bv);
extern void IRO_DumpAfterPhase(char* str, Boolean flag);
extern void IRO_DumpFlowgraph(void);
extern void IRO_DumpNode(IRONode* node);
extern void IRO_DumpExprs(void);
extern void IRO_SetupDump(void);
extern void IRO_CleanupDump(void);
extern void IRO_Dump(char* format, ...);
extern void IRO_SpellType(Type* type, char* buf);

#ifdef __cplusplus
}
#endif

#endif
