#include <stddef.h>
#include "compiler/PCode.h"
#include "compiler/Scheduler.h"
#include "compiler/objects.h"
#define FIELD(t, f) offsetof(t, f), sizeof(((t *)0)->f)
#define ALIGN(t, n) struct n { char tag; t value; }
ALIGN(PCodeInstruction, InstructionAlignment);
ALIGN(PCodeBlock, BlockAlignment);
ALIGN(PCodeBlockLink, LinkAlignment);
ALIGN(CColoringNode, NodeAlignment);
ALIGN(CColoringList, EdgeAlignment);
ALIGN(struct MachineInfo, MachineAlignment);
unsigned long RustAbiPCodeLayout[] = {
    sizeof(PCodeInstruction), offsetof(struct InstructionAlignment, value),
    FIELD(PCodeInstruction, next), FIELD(PCodeInstruction, opcode), FIELD(PCodeInstruction, flags),
    FIELD(PCodeInstruction, operand_count), FIELD(PCodeInstruction, f18[1]),
    offsetof(PCodeInstruction, operandData.operands), sizeof(PCodeOperand),
    sizeof(PCodeBlock), offsetof(struct BlockAlignment, value),
    FIELD(PCodeBlock, next), FIELD(PCodeBlock, instructions), FIELD(PCodeBlock, index),
    FIELD(PCodeBlock, execution_weight), FIELD(PCodeBlock, flags), FIELD(PCodeBlock, successors),
    sizeof(PCodeBlockLink), offsetof(struct LinkAlignment, value),
    FIELD(PCodeBlockLink, next), FIELD(PCodeBlockLink, payload.block),
    sizeof(CColoringNode), offsetof(struct NodeAlignment, value),
    FIELD(CColoringNode, prev), FIELD(CColoringNode, conflicts), FIELD(CColoringNode, obj),
    FIELD(CColoringNode, latency), FIELD(CColoringNode, earliestCycle), FIELD(CColoringNode, latestCycle),
    FIELD(CColoringNode, height), FIELD(CColoringNode, flag),
    sizeof(CColoringList), offsetof(struct EdgeAlignment, value),
    FIELD(CColoringList, next), FIELD(CColoringList, owner), FIELD(CColoringList, value),
    sizeof(struct MachineInfo), offsetof(struct MachineAlignment, value), FIELD(struct MachineInfo, check),
    FIELD(Object, datatype), FIELD(Object, name), FIELD(Object, u.alias.object), FIELD(Object, u.func.linkname),
    offsetof(HashNameNode, name), DFUNC, DVFUNC, DALIAS
};
