#include "compiler/InterferenceGraph.h"
#include "compiler/BitVectors.h"
#include "compiler/CError.h"
#include "compiler/Coloring.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/LiveVariables.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/PCodeListing.h"
#include "compiler/PCodeUtilities.h"
#include "compiler/RegisterInfo.h"
#include "compiler/Registers.h"

IGNode** interferencegraph;
static UInt32* interferencematrix;
static SInt16* coalesced;
#if VERSION == VERSION_WII_1_0RC1
static UInt32* pinned_call_registers;
#endif

static void makeinterfere(UInt32 a, UInt32 b) {
    if (a < b) {
        bitvectorsetbit(((b * b) / 2) + a, interferencematrix);
    } else if (a > b) {
        bitvectorsetbit(((a * a) / 2) + b, interferencematrix);
    }
}

int interferes(UInt32 a, UInt32 b) {
    if (a < b) {
        return bitvectorgetbit(((b * b) / 2) + a, interferencematrix) > 0;
    } else if (a > b) {
        return bitvectorgetbit(((a * a) / 2) + b, interferencematrix) > 0;
    } else {
        return FALSE;
    }
}

static void buildinterferencematrix(UInt32* vec) {
    PCodeBlock* block;
    PCode* instr;
    PCodeArg* op;
    UInt32 i;
    UInt32 j;
    short reg;

    UInt32 regs = used_virtual_registers[coloring_class];
    UInt32 realRegs = n_real_registers[coloring_class];
    bitvectorinitialize(interferencematrix, (regs * regs) / 2, 0);
#if VERSION == VERSION_WII_1_0RC1
    bitvectorinitialize(pinned_call_registers, regs, 0);
#endif

    for (i = 0; i < realRegs; i++) {
        for (j = 0; j < realRegs; j++) {
            if (i != j) {
                makeinterfere(i, j);
            }
        }
    }

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        bitvectorcopy(vec, liveinfo[block->blockIndex].out, regs);
        for (instr = block->lastPCode; instr; instr = instr->prevPCode) {
            for (op = instr->args, i = instr->argCount; i--; op++) {
#if VERSION == VERSION_WII_1_0RC1
                if ((instr->flags & fIsCall) && PC_OP_IS_ANY_REGISTER(op, coloring_class) &&
                    (op->data.reg.effect & EffectPinned)) {
                    bitvectorsetbit(op->data.reg.reg, pinned_call_registers);
                }
#endif
                if (PC_OP_IS_WRITE_ANY_REGISTER(op, coloring_class)) {
#ifdef __MWERKS__
                    bitvectorclearbit(reg = op->data.reg.reg, vec);
#else
                    reg = op->data.reg.reg;
                    bitvectorclearbit(reg, vec);
#endif
                    for (j = 0; j < regs; j++) {
                        if (bitvectorgetbit(j, vec)) {
                            if ((instr->flags & fIsMove) &&
                                PC_OP_IS_ANY_REGISTER(&instr->args[0], coloring_class) &&
                                instr->args[1].data.reg.reg == j) {
                                continue;
                            }
                            makeinterfere(reg, j);
                        }
                    }
                }
            }

            for (op = instr->args, i = instr->argCount; i--; op++) {
                if (PC_OP_IS_READ_ANY_REGISTER(op, coloring_class)) {
#ifdef __MWERKS__
                    if (bitvectorgetbit(reg = op->data.reg.reg, vec) == 0) {
#else
                    reg = op->data.reg.reg;
                    if (bitvectorgetbit(reg, vec) == 0) {
#endif
                        op->data.reg.effect |= EffectLastUse;
                    }
                    bitvectorsetbit(reg, vec);
                }
            }

            if (coloring_class == RegClass_GPR) {
                if (PCODE_FLAG_SET_F(instr) & (fIsRead | fIsWrite | fHasBaseReg)) {
                    if (instr->args[1].data.reg.reg >= n_real_registers[coloring_class]) {
                        makeinterfere(0, instr->args[1].data.reg.reg);
                    }
                    if (PCODE_FLAG_SET_F(instr) & fUpdatesPtr) {
                        makeinterfere(instr->args[0].data.reg.reg, instr->args[1].data.reg.reg);
                    }
                } else {
                    switch (instr->op) {
                        case PC_DCBF:
                        case PC_DCBST:
                        case PC_DCBT:
                        case PC_DCBTST:
                        case PC_DCBZ:
                        case PC_DCBI:
                        case PC_ICBI:
                        case PC_DCCCI:
                        case PC_ICBT:
                        case PC_ICCCI:
                        case PC_ICREAD:
                        case PC_DCBA:
                        case PC_DST:
                        case PC_DSTT:
                        case PC_DSTST:
                        case PC_DSTSTT:
                        case PC_DCBZ_L:
                            if (instr->args[0].data.reg.reg >= n_real_registers[coloring_class]) {
                                makeinterfere(0, instr->args[0].data.reg.reg);
                            }
                            break;
                    }
                }
            }

            if (coloring_class == RegClass_GPR && (instr->flags & fIsCall)) {
                i = branch_count_volatiles();
                op = instr->args;
                CError_ASSERT(219, instr->argCount != 0);

                while (op->kind != PCOp_REGISTER || !(op->data.reg.effect & EffectWrite)) {
                    i++;
                    op++;
                    CError_ASSERT(226, i <= instr->argCount);
                }

                for (op = instr->args + i; i < instr->argCount; i++, op++) {
                    if (op->kind == PCOp_REGISTER && op->arg == RegClass_GPR) {
                        for (j = 0; j < n_scratch_registers[coloring_class]; j++) {
                            makeinterfere(op->data.reg.reg, scratch_registers[coloring_class][j]);
                        }
                    }
                }
            }
        }
    }
}

static short coalesced_path(short id) {
    while (id != coalesced[id]) {
        id = coalesced[id];
    }
    return id;
}

static Boolean coalescenodes(void) {
    PCodeArg* op;
    UInt32 regs;
    PCodeBlock* block;
    PCode* instr;
    UInt32 i;
    short path1;
    short path2;
    short node1;
    short node2;
    Boolean changed = FALSE;

    regs = used_virtual_registers[coloring_class];

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        for (instr = block->firstPCode; instr; instr = instr->nextPCode) {
            if ((instr->flags & fIsMove) && !(instr->flags & fSideEffects)) {
                if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                    CError_FATAL(309);
                    continue;
                }

                if (instr->argCount > 2) {
                    if (instr->argCount != 3 || instr->args[2].kind != PCOp_PLACEHOLDEROPERAND) {
                        CError_FATAL(316);
                        continue;
                    }
                }

                if (PC_OP_IS_ANY_REGISTER(&instr->args[0], coloring_class)) {
                    path1 = coalesced_path(instr->args[0].data.reg.reg);
                    path2 = coalesced_path(instr->args[1].data.reg.reg);
                    if (path1 == path2) {
                        deletepcode(instr);
                        continue;
                    }

                    if (!interferes(path1, path2)) {
#if VERSION != VERSION_WII_1_0RC1
                        if (path1 >= n_real_registers[coloring_class] &&
                            path2 >= n_real_registers[coloring_class] &&
                            !(path1 >= first_fe_temporary_register[coloring_class] &&
                              path1 <= last_temporary_register[coloring_class] &&
                              path2 >= first_fe_temporary_register[coloring_class] &&
                              path2 <= last_temporary_register[coloring_class])) {
                            continue;
                        }
#endif

                        node1 = (path2 < path1) ? path2 : path1;
                        node2 = (path2 > path1) ? path2 : path1;

#if VERSION == VERSION_WII_1_0RC1
                        /* A pinned call operand must keep its register number. */
                        if (bitvectorgetbit(node2, pinned_call_registers)) {
                            continue;
                        }
#endif
                        if (coloring_class == RegClass_GPR && node2 == _CALLER_SP_) {
                            continue;
                        }

                        coalesced[node2] = node1;
                        changed = TRUE;
                        for (i = 0; i < regs; i++) {
                            if (interferes(node2, i)) {
                                makeinterfere(node1, i);
                            }
                        }

                        deletepcode(instr);
                    }
                }
            }
        }
    }

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        for (instr = block->firstPCode; instr; instr = instr->nextPCode) {
            op = instr->args;
            i = instr->argCount;
            while (i--) {
                if (PC_OP_IS_ANY_REGISTER(op, coloring_class) &&
                    op->data.reg.reg != coalesced[op->data.reg.reg]) {
                    op->data.reg.reg = coalesced_path(op->data.reg.reg);
                }
                op++;
            }
        }
    }
    return changed;
}

static void buildadjacencyvectors(void) {
    IGNode* node;
    UInt32 regs;
    UInt32 i;
    UInt32 counter;
    short* array;
    short* dest;
    UInt32 j;
    short* src;

    regs = used_virtual_registers[coloring_class];
    interferencegraph = oalloc(sizeof(IGNode*) * regs);
    array = oalloc(sizeof(short) * regs);

    for (i = 0; i < regs; i++) {
        counter = 0;
        for (j = 0; j < regs; j++) {
            if (interferes(i, j)) {
                array[counter++] = j;
            }
        }

        node = interferencegraph[i] = oalloc(sizeof(IGNode) + sizeof(short) * (counter - 1));
        memclrw(node, sizeof(IGNode) + sizeof(short) * (counter - 1));

        node->reg = i;
        node->color = -1;
        node->arraySize = counter;
        node->degree = node->arraySize;

        dest = node->array;
        src = array;
        for (j = 0; j < counter; j++) {
            *(dest++) = *(src++);
        }

        if (i != coalesced[i]) {
            node->flags |= fCoalesced;
            j = coalesced_path(i);
            interferencegraph[j]->flags |= fCoalescedInto;
            node->color = j;
        }
    }
}

static void eliminatedeadcode(void) {
    UInt32 regs;
    PCodeBlock* block;
    PCode* instr;
    UInt32* vec;
    UInt32 i;
    PCodeArg* op;
    short reg;

    regs = used_virtual_registers[coloring_class];
    vec = oalloc(4 * ((regs + 31) >> 5));

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        bitvectorcopy(vec, liveinfo[block->blockIndex].out, regs);
        for (instr = block->lastPCode; instr; instr = instr->prevPCode) {
            if (dead(instr, coloring_class, vec)) {
                deletepcode(instr);
                continue;
            }

            op = instr->args;
            i = instr->argCount;
            while (i--) {
                if (PC_OP_IS_WRITE_ANY_REGISTER(op, coloring_class)) {
#ifdef __MWERKS__
                    bitvectorclearbit(reg = op->data.reg.reg, vec);
#else
                    reg = op->data.reg.reg;
                    bitvectorclearbit(reg, vec);
#endif
                }
                op++;
            }

            op = instr->args;
            i = instr->argCount;
            while (i--) {
                if (PC_OP_IS_READ_ANY_REGISTER(op, coloring_class)) {
#ifdef __MWERKS__
                    if (!bitvectorgetbit(reg = op->data.reg.reg, vec)) {
#else
                    reg = op->data.reg.reg;
                    if (!bitvectorgetbit(reg, vec)) {
#endif
                        op->data.reg.effect |= EffectLastUse;
                    }
                    bitvectorsetbit(reg, vec);
                }
                op++;
            }
        }
    }
}

static void findrematerializations(void) {
    UInt32 regs;
    UInt32 i;
    PCodeBlock* block;
    PCode* instr;
    PCodeArg* op;
    IGNode* node;

    regs = used_virtual_registers[coloring_class];

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        for (instr = block->lastPCode; instr; instr = instr->prevPCode) {
            op = instr->args;
            i = instr->argCount;
            while (i--) {
                if (PC_OP_IS_WRITE_ANY_REGISTER(op, coloring_class) &&
                    op->data.reg.reg >= n_real_registers[coloring_class] &&
                    !(interferencegraph[op->data.reg.reg]->flags & (fPairLow | fPairHigh)) &&
                    !(interferencegraph[op->data.reg.reg]->flags & fMultipleDefs)) {
                    node = interferencegraph[op->data.reg.reg];
                    if (!node->instr) {
                        node->instr = instr;
                    } else {
                        node->instr = NULL;
                        node->flags |= fMultipleDefs;
                    }
                }
                op++;
            }
        }
    }

    for (i = 0; i < regs; i++) {
        node = interferencegraph[i];
        if (node->instr && !is_location_independent(node->instr)) {
            node->instr = NULL;
        }
    }
}

void buildinterferencegraph(Object* proc) {
    UInt32 regs = used_virtual_registers[coloring_class];
    UInt32* live = oalloc(4 * ((regs + 31) >> 5));
    UInt32 i;

    computelivevariables(proc);
    eliminatedeadcode();
    coalesced = oalloc(sizeof(*coalesced) * regs);
    for (i = 0; i < regs; i++) {
        coalesced[i] = i;
    }
    interferencematrix = oalloc(4 * ((((regs * regs) / 2) + 31) >> 5));
#if VERSION == VERSION_WII_1_0RC1
    pinned_call_registers = oalloc(4 * ((regs + 31) >> 5));
#endif
    for (;;) {
#if VERSION == VERSION_WII_1_0RC1
        filter_undefined_live_registers();
#endif
        buildinterferencematrix(live);
        if (copts.debuglisting) {
            pclistinterferences(register_class_format[coloring_class], regs);
        }
        if (!coalescenodes()) {
            break;
        }
#if VERSION == VERSION_WII_1_0RC1
        /* The rewritten operands can remove interference for another move. */
        computelivevariables(proc);
#else
        break;
#endif
    }
    buildadjacencyvectors();
    findrematerializations();
}
