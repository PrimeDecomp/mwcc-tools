#include <stddef.h>
#include "compiler/PCode.h"
#include "compiler/CFunc.h"
struct StatementAlignment { char tag; Statement value; };
unsigned long RustAbiBlocksLayout[] = {
    sizeof(Statement), offsetof(struct StatementAlignment, value),
    offsetof(Statement, sourceoffset), sizeof(((Statement *)0)->sourceoffset),
    offsetof(PCodeBlock, instruction_count), sizeof(((PCodeBlock *)0)->instruction_count),
    offsetof(PCodeBlock, reverse_instructions), offsetof(PCodeBlock, predecessors),
    offsetof(PCodeBlock, flags), sizeof(((PCodeBlock *)0)->flags),
    offsetof(PCodeInstruction, flags), sizeof(((PCodeInstruction *)0)->flags),
    offsetof(PCodeInstruction, opcode), sizeof(((PCodeInstruction *)0)->opcode),
    sizeof(Boolean), BY_VERSION(0x420, 0x108), fIsBranch, PC_B
};
