#include "compiler/Peephole.h"
#include "compiler/Alias.h"
#include "compiler/CMachine.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/InstrSelection.h"
#include "compiler/objects.h"
#include "compiler/OracleProbes.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/PCodeUtilities.h"
#include "compiler/RegisterInfo.h"
#include "compiler/Registers.h"
#include "compiler/Scheduler.h"
#include "compiler/types.h"
#include "compiler/UseDefChains.h"

typedef int (*PeepholeFunc)(PCode* instr, UInt32* masks);

typedef struct Pattern {
    struct Pattern* next;
    PeepholeFunc func;
} Pattern;

// A block's registers of one class, as masks (TODO: invented member names, after LiveVariables'
// LiveInfo; Pro7: x0, x4, x8, xC)
typedef struct LiveRegs {
    UInt32 use; // (read before written in the block)
    UInt32 def; // (written in the block)
    UInt32 in; // (live on entry)
    UInt32 out; // (live on exit)
} LiveRegs;

/* (the original's statics; global while a lifted function of this unit still uses them) */
LiveRegs* liveregs[RegClassMax];
Pattern* peepholepatterns[OPCODE_MAX];
PCode** defininginstruction;

static void computeregisterusedefs(void) {
    PCodeBlock* block;
    PCode* instr;
    PCodeArg* op;
    int i;
    RegClass rclass;
    LiveRegs* lr;
    UInt32 array1[RegClassMax];
    UInt32 array2[RegClassMax];

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        for (rclass = 0; rclass < RegClassMax; rclass++) {
            array1[rclass] = 0;
            array2[rclass] = 0;
        }

        for (instr = block->firstPCode; instr; instr = instr->nextPCode) {
            for (op = instr->args, i = instr->argCount; i--; op++) {
                if (op->kind ==
                    PCOp_REGISTER &&
                    (op->data.reg.effect & EffectRead) &&
                    !((1 << op->data.reg.reg) & array2[op->arg])) {
                    array1[op->arg] |= 1 << op->data.reg.reg;
                }
            }

            for (op = instr->args, i = instr->argCount; i--; op++) {
                if (op->kind ==
                    PCOp_REGISTER &&
                    (op->data.reg.effect & EffectWrite) &&
                    !((1 << op->data.reg.reg) & array1[op->arg])) {
                    array2[op->arg] |= 1 << op->data.reg.reg;
                }
            }
        }

        for (rclass = 0; rclass < RegClassMax; rclass++) {
            lr = liveregs[rclass] + block->blockIndex;
            lr->use = array1[rclass];
            lr->def = array2[rclass];
            if (rclass == RegClass_GPR) {
                lr->in = 1 << 1;
                lr->out = 1 << 1;
            } else {
                lr->in = 0;
                lr->out = 0;
            }
        }
    }
}

static void computeliveness(LiveRegs* lrarray, UInt32 x) {
    PCLink* link;
    LiveRegs* lr;
    PCodeBlock* block;
    UInt32 newC;
    UInt32 new8;
    SInt32 i;
    SInt32 flag;

    flag = 1;
    while (flag) {
        flag = 0;
        i = pcblockcount;
        while (i) {
            if ((block = depthfirstordering[--i])) {
                lr = lrarray + block->blockIndex;
                newC = x;
                for (link = block->successors; link; link = link->nextLink) {
                    newC |= lrarray[link->block->blockIndex].in;
                }
                lr->out = newC;

                new8 = lr->use | (lr->out & ~lr->def);
                if (new8 != lr->in) {
                    lr->in = new8;
                    flag = 1;
                }
            }
        }
    }
}

static void computeliveregisters(Object* func) {
    Type* returntype;
    RegClass rclass;
    RegInfo* reginfo;
    PCodeBlock* block;
    int i;

    returntype = TYPE_FUNC(func->type)->functype;

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        liveregs[rclass] = lalloc(sizeof(LiveRegs) * pcblockcount);
    }

    computedepthfirstordering();
    computeregisterusedefs();

    // (2.6: the global register variables are live at the end of the last block)
    i = pcblockcount;
    do {
        block = depthfirstordering[--i];
    } while (!block);
    for (reginfo = reg_info_list; reginfo; reginfo = reginfo->next) {
        if (reginfo->object) {
            liveregs[reginfo->reginfo.rclass][block->blockIndex].use |= 1 << reginfo->reginfo.reg;
        }
    }

    if (TYPE_FITS_IN_REGISTER(returntype)) {
        liveregs[RegClass_GPR][epilogue->blockIndex].use |= 1 << 3;
        if (TYPE_IS_8BYTES(returntype)) {
            liveregs[RegClass_GPR][epilogue->blockIndex].use |= 1 << 4;
        }
    } else if (IS_TYPE_VEC2X32FLOAT(returntype)) {
        liveregs[RegClass_FPR][epilogue->blockIndex].use |= 1 << 1;
    } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(returntype)) {
        // (2.6: without an FPU a float comes back in r3, a double in r3/r4)
        if (TYPE_IS_SOFT_FLOAT(returntype)) {
            liveregs[RegClass_GPR][epilogue->blockIndex].use |= 1 << 3;
            if (returntype->size == 8) {
                liveregs[RegClass_GPR][epilogue->blockIndex].use |= 1 << 4;
            }
        } else {
            liveregs[RegClass_FPR][epilogue->blockIndex].use |= 1 << 1;
        }
    } else if (IS_TYPE_VECTOR(returntype)) {
        liveregs[RegClass_VR][epilogue->blockIndex].use |= 1 << 2;
    } else if ((IS_TYPE_STRUCT(returntype) || IS_TYPE_CLASS(returntype)) &&
               !Type_RequiresMemoryReturn(returntype)) {
        // (2.6: a small struct comes back in r3/r4)
        liveregs[RegClass_GPR][epilogue->blockIndex].use |= 1 << 3;
        if (returntype->size > 4) {
            liveregs[RegClass_GPR][epilogue->blockIndex].use |= 1 << 4;
        }
    }

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        if (rclass == RegClass_GPR) {
            computeliveness(liveregs[rclass], 2);
        } else {
            computeliveness(liveregs[rclass], 0);
        }
    }
}

static void computeinstructionpredecessors(PCodeBlock* block) {
    PCode* nop;
    PCodeArg* op;
    SInt32 defID;
    SInt32 totalOps;
    PCode* instr;
    SInt32 i;
    RegClass rclass;
    PCode* array[RegClassMax][32];

    nop = makepcode(PC_NOP);
    for (rclass = 0; rclass < RegClassMax; rclass++) {
        for (i = 0; i < 32; i++) {
            array[rclass][i] = nop;
        }
    }

    totalOps = 0;
    for (instr = block->firstPCode; instr; instr = instr->nextPCode) {
        totalOps += instr->argCount;
    }

    if (totalOps) {
        defininginstruction = oalloc(sizeof(PCode*) * totalOps);
        for (i = 0; i < totalOps; i++) {
            defininginstruction[i] = nop;
        }

        defID = 0;
        for (instr = block->firstPCode; instr; instr = instr->nextPCode) {
            instr->defID = defID;

            for (i = 0, op = instr->args; i < instr->argCount; i++, op++) {
                if (op->kind == PCOp_REGISTER && (op->data.reg.effect & EffectRead)) {
                    defininginstruction[defID + i] = array[op->arg][op->data.reg.reg];
                }
            }

            for (i = 0, op = instr->args; i < instr->argCount; i++, op++) {
                if (op->kind == PCOp_REGISTER && (op->data.reg.effect & EffectWrite)) {
                    array[op->arg][op->data.reg.reg] = instr;
                }
            }

            defID += instr->argCount;
        }
    }
}

static int dead(PCode* instr, UInt32* masks) {
    int i;
    PCodeArg* op;

    if (instr->block->flags & (fIsProlog | fIsEpilogue)) {
        return 0;
    }
    if (instr->flags & (fIsBranch | fIsWrite | fIsCall | fIsVolatile | fSideEffects)) {
        return 0;
    }
    if (!instr->block->predecessors) {
        return 1;
    }

    for (op = instr->args, i = instr->argCount; i--; op++) {
        if (op->kind ==
            PCOp_REGISTER &&
            (op->data.reg.effect & EffectWrite) &&
            ((1 << op->data.reg.reg) & masks[op->arg])) {
            return 0;
        }
    }

    return 1;
}

static int definedbetween(PCode* start, PCode* end, PCodeArg* checkOp) {
    PCode* instr;
    PCodeArg* op;
    int i;

    for (instr = start->prevPCode; instr != end; instr = instr->prevPCode) {
        for (op = instr->args, i = instr->argCount; i--; op++) {
            if (PC_OP_IS_WRITE_REGISTER(op, checkOp->arg, checkOp->data.reg.reg)) {
                return 1;
            }
        }
    }

    return 0;
}

static int usedbetween(PCode* start, PCode* end, PCodeArg* checkOp) {
    PCode* instr;
    PCodeArg* op;
    int i;

    for (instr = start->prevPCode; instr != end; instr = instr->prevPCode) {
        for (op = instr->args, i = instr->argCount; i--; op++) {
            if (PC_OP_IS_READ_REGISTER(op, checkOp->arg, checkOp->data.reg.reg)) {
                return 1;
            }
        }
    }

    return 0;
}

static int isSPRlive(PCode* instr, int reg) {
    PCode* scan;
    PCodeArg* op;
    int i;

    for (scan = instr->nextPCode; scan; scan = scan->nextPCode) {
        for (op = scan->args, i = scan->argCount; i--; op++) {
            if (PC_OP_IS_READ_REGISTER(op, RegClass_SPR, reg)) {
                return 1;
            }

            if (PC_OP_IS_WRITE_REGISTER(op, RegClass_SPR, reg)) {
                return 0;
            }
        }
    }

    return 0;
}

static SInt32 extractedbits(PCode* instr) {
    SInt32 a = instr->args[2].data.imm.value;
    SInt32 b = instr->args[3].data.imm.value;
    SInt32 c = instr->args[4].data.imm.value;
    SInt32 val;

    if (b <= c) {
        val =
            ((b > 31) ? 0 : (0xFFFFFFFFu >> b)) & ~(((c + 1) > 31) ? 0 : (0xFFFFFFFFu >> (c + 1)));
    } else {
        val =
            ((b > 31) ? 0 : (0xFFFFFFFFu >> b)) | ~(((c + 1) > 31) ? 0 : (0xFFFFFFFFu >> (c + 1)));
    }

    return ((UInt32)val >> a) | (val << (32 - a));
}

static int canmergemasks(
    int b1, int c1, SInt32 a, SInt32 b2, SInt32 c2, short* first, short* last
) {
    SInt32 val1;
    SInt32 val2;

    if (b1 <= c1) {
        val1 = ((b1 > 31) ? 0 : (0xFFFFFFFFu >> b1)) &
            ~(((c1 + 1) > 31) ? 0 : (0xFFFFFFFFu >> (c1 + 1)));
    } else {
        val1 = ((b1 > 31) ? 0 : (0xFFFFFFFFu >> b1)) |
            ~(((c1 + 1) > 31) ? 0 : (0xFFFFFFFFu >> (c1 + 1)));
    }

    if (b2 <= c2) {
        val2 = ((b2 > 31) ? 0 : (0xFFFFFFFFu >> b2)) &
            ~(((c2 + 1) > 31) ? 0 : (0xFFFFFFFFu >> (c2 + 1)));
    } else {
        val2 = ((b2 > 31) ? 0 : (0xFFFFFFFFu >> b2)) |
            ~(((c2 + 1) > 31) ? 0 : (0xFFFFFFFFu >> (c2 + 1)));
    }

    return ismaskconstant(val2 & ((val1 << a) | ((UInt32)val1 >> (32 - a))), first, last);
}

static int canuseupdatetest(
    PCodeBlock* block, PCode* instr, int count1, int count2, int count3, int count4, int count5
) {
    int i;
    PCLink* link;

    while (instr) {
        if (++count1 > 17) {
            return 1;
        }

        switch (instr->op) {
            case PC_DIVW:
            case PC_DIVWU:
            case PC_MULHW:
            case PC_MULHWU:
            case PC_MULLI:
            case PC_MULLW:
                if (count3 == 0) {
                    return 1;
                }
                return 0;
            case PC_MTXER:
            case PC_MTCTR:
            case PC_MTLR:
            case PC_MTCRF:
            case PC_MTMSR:
            case PC_MTSPR:
            case PC_MFMSR:
            case PC_MFSPR:
            case PC_MFXER:
            case PC_MFCTR:
            case PC_MFLR:
            case PC_MFCR:
            case PC_ECIWX:
            case PC_ECOWX:
            case PC_DCBI:
            case PC_ICBI:
            case PC_MCRFS:
            case PC_MCRXR:
            case PC_MFTB:
            case PC_MFSR:
            case PC_MTSR:
            case PC_MFSRIN:
            case PC_MTSRIN:
            case PC_MTFSB0:
            case PC_MTFSB1:
            case PC_MTFSFI:
            case PC_SC:
            case PC_TLBIA:
            case PC_TLBIE:
            case PC_TLBLD:
            case PC_TLBLI:
            case PC_TLBSYNC:
            case PC_TW:
            case PC_TRAP:
            case PC_TWI:
            case PC_MFROM:
            case PC_DSA:
            case PC_ESA:
                return 1;
            case PC_CRAND:
            case PC_CRANDC:
            case PC_CREQV:
            case PC_CRNAND:
            case PC_CRNOR:
            case PC_CROR:
            case PC_CRORC:
            case PC_CRXOR:
            case PC_MCRF:
                if (++count5 > 1) {
                    return 1;
                }
        }

        if (instr->flags & (fIsRead | fIsWrite)) {
            if (++count4 > 1) {
                return 1;
            }
        } else if (instr->flags & fIsBranch) {
            if (++count2 > 2) {
                return 1;
            }

            for (i = 0; i < instr->argCount; i++) {
                if (PC_OP_IS_ANY_REGISTER(&instr->args[i], RegClass_CRFIELD)) {
                    ++count3;
                    break;
                }
            }
        }

        instr = instr->nextPCode;
    }

    if (block && block->successors) {
        for (link = block->successors; link; link = link->nextLink) {
            if (link->block &&
                !canuseupdatetest(
                    link->block, link->block->firstPCode, count1, count2, count3, count4, count5
                )) {
                return 0;
            }
        }
    }

    return 1;
}

static int canuseupdate(PCode* instr) {
    return canuseupdatetest(instr->block, instr->nextPCode, 0, 0, 0, 0, 0);
}

static int MR_Rx_Rx(PCode* instr, UInt32* masks) {
    if (instr->args[0].data.reg.reg ==
        instr->args[1].data.reg.reg &&
        !(PCODE_FLAG_SET_F(instr) & fRecordBit)) {
        deletepcode(instr);
        return 1;
    }

    return 0;
}

static int FMR_Fx_Fx(PCode* instr, UInt32* masks) {
    if (instr->args[0].data.reg.reg ==
        instr->args[1].data.reg.reg &&
        !(PCODE_FLAG_SET_F(instr) & fRecordBit)) {
        deletepcode(instr);
        return 1;
    }

    return 0;
}

static int VMR_Vx_Vx(PCode* instr, UInt32* masks) {
    if (instr->args[0].data.reg.reg ==
        instr->args[1].data.reg.reg &&
        !(PCODE_FLAG_SET_F(instr) & fRecordBit)) {
        deletepcode(instr);
        return 1;
    }

    return 0;
}

static int MR_MR(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID + 1];

    if (defInstr->op ==
        PC_MR &&
        instr->args[0].data.reg.reg ==
        defInstr->args[1].data.reg.reg &&
        !definedbetween(instr, defInstr, &instr->args[0]) &&
        !(PCODE_FLAG_SET_F(instr) & fRecordBit)) {
        deletepcode(instr);
        return 1;
    }

    return 0;
}

static int FMR_FMR(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID + 1];

    if (defInstr->op ==
        PC_FMR &&
        instr->args[0].data.reg.reg ==
        defInstr->args[1].data.reg.reg &&
        !(PCODE_FLAG_SET_F(instr) & fRecordBit) &&
        !definedbetween(instr, defInstr, &instr->args[0])) {
        deletepcode(instr);
        return 1;
    }

    return 0;
}

static int VMR_VMR(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID + 1];

    if (defInstr->op ==
        PC_VMR &&
        instr->args[0].data.reg.reg ==
        defInstr->args[1].data.reg.reg &&
        !(PCODE_FLAG_SET_F(instr) & fRecordBit) &&
        !definedbetween(instr, defInstr, &instr->args[0])) {
        deletepcode(instr);
        return 1;
    }

    return 0;
}

static int VMR_VMRP(PCode* instr, UInt32* masks) {
    PCode* prev = instr->prevPCode;
    PCode* next = instr->nextPCode;
    int prevFlag = 0;
    int prevPermute = 0;
    int nextFlag = 0;
    int nextPermute = 0;

    if (prev) {
        prevFlag = (prev->flags & fOpTypeMask) == fOpTypeVR;
        prevPermute = uses_vpermute_unit(prev);
    }
    if (next) {
        nextFlag = (next->flags & fOpTypeMask) == fOpTypeVR;
        nextPermute = uses_vpermute_unit(next);
    }

    if (prev) {
        if (next) {
            if (prevFlag && !prevPermute) {
                if (nextFlag) {
                    if (!nextPermute) {
                        change_opcode(instr, PC_VMRP);
                        return 1;
                    }
                } else {
                    change_opcode(instr, PC_VMRP);
                    return 1;
                }
            }
        } else {
            if (prevFlag && !prevPermute) {
                change_opcode(instr, PC_VMRP);
                return 1;
            }
        }
    } else {
        if (next && nextFlag && !nextPermute) {
            change_opcode(instr, PC_VMRP);
            return 1;
        }
    }

    return 0;
}

static int MR_CMPI(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID + 1];
    PCodeArg op;

    if (instr->args[0].data.reg.reg ==
        0 &&
        instr->args[2].data.imm.value ==
        0 &&
        (PCODE_FLAG_SET_F(defInstr) & (fSideEffects | fCanSetRecordBit | fOpTypeGPR)) ==
        (fCanSetRecordBit | fOpTypeGPR) &&
        !usedbetween(instr, defInstr, &instr->args[0]) &&
        !definedbetween(instr, defInstr, &instr->args[0])) {
        if (defInstr->op == PC_ADDI) {
            op.kind = PCOp_REGISTER;
            op.arg = RegClass_SPR;
            op.data.reg.reg = 0;
            op.data.reg.effect = EffectRead | EffectWrite;
            if (usedbetween(instr, defInstr, &op)) {
                return 0;
            }
        }

        pcsetrecordbit(defInstr);
        deletepcode(instr);
        return 1;
    }

    return 0;
}

static int EXTSB_RLWINM(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID + 1];

    if (defInstr->op ==
        PC_EXTSB &&
        (defInstr->args[0].data.reg.reg ==
         instr->args[0].data.reg.reg ||
         !(masks[RegClass_GPR] & (1 << defInstr->args[0].data.reg.reg))) &&
        !(PCODE_FLAG_SET_F(defInstr) & fRecordBit) &&
        !definedbetween(instr, defInstr, &defInstr->args[1]) &&
        !usedbetween(instr, defInstr, &defInstr->args[0]) &&
        (extractedbits(instr) & 0xFFFFFF00) == 0) {
        instr->args[1].data.reg.reg = defInstr->args[1].data.reg.reg;
        defininginstruction[instr->defID + 1] = defininginstruction[defInstr->defID + 1];
        deletepcode(defInstr);
        return 1;
    }

    return 0;
}

static int EXTSH_RLWINM(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID + 1];

    if (defInstr->op ==
        PC_EXTSH &&
        (defInstr->args[0].data.reg.reg ==
         instr->args[0].data.reg.reg ||
         !(masks[RegClass_GPR] & (1 << defInstr->args[0].data.reg.reg))) &&
        !(PCODE_FLAG_SET_F(defInstr) & fRecordBit) &&
        !definedbetween(instr, defInstr, &defInstr->args[1]) &&
        !usedbetween(instr, defInstr, &defInstr->args[0]) &&
        (extractedbits(instr) & 0xFFFF0000) == 0) {
        instr->args[1].data.reg.reg = defInstr->args[1].data.reg.reg;
        defininginstruction[instr->defID + 1] = defininginstruction[defInstr->defID + 1];
        deletepcode(defInstr);
        return 1;
    }

    return 0;
}

static int LBZ_RLWINM(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID + 1];

    if ((defInstr->op == PC_LBZ || defInstr->op == PC_LBZX) &&
        instr->args[2].data.imm.value ==
        0 &&
        instr->args[3].data.imm.value <=
        24 &&
        instr->args[4].data.imm.value ==
        31 &&
        !(PCODE_FLAG_SET_F(instr) & fRecordBit) &&
        (defInstr->args[0].data.reg.reg ==
         instr->args[0].data.reg.reg ||
         !(masks[RegClass_GPR] & (1 << defInstr->args[0].data.reg.reg))) &&
        !usedbetween(instr, defInstr, &defInstr->args[0]) &&
        !definedbetween(instr, defInstr, &instr->args[0]) &&
        !usedbetween(instr, defInstr, &instr->args[0])) {
        defInstr->args[0].data.reg.reg = instr->args[0].data.reg.reg;
        deletepcode(instr);
        return 1;
    }

    return 0;
}

static int LHZ_RLWINM(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID + 1];

    if ((defInstr->op == PC_LHZ || defInstr->op == PC_LHZX) &&
        instr->args[2].data.imm.value ==
        0 &&
        instr->args[3].data.imm.value <=
        16 &&
        instr->args[4].data.imm.value ==
        31 &&
        !(PCODE_FLAG_SET_F(instr) & fRecordBit) &&
        (defInstr->args[0].data.reg.reg ==
         instr->args[0].data.reg.reg ||
         !(masks[RegClass_GPR] & (1 << defInstr->args[0].data.reg.reg))) &&
        !usedbetween(instr, defInstr, &defInstr->args[0]) &&
        !definedbetween(instr, defInstr, &instr->args[0]) &&
        !usedbetween(instr, defInstr, &instr->args[0])) {
        defInstr->args[0].data.reg.reg = instr->args[0].data.reg.reg;
        deletepcode(instr);
        return 1;
    }

    return 0;
}

static int LHA_EXTSH(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID + 1];

    if (defInstr->op ==
        PC_LHA &&
        !(PCODE_FLAG_SET_F(instr) & fRecordBit) &&
        (defInstr->args[0].data.reg.reg ==
         instr->args[0].data.reg.reg ||
         !(masks[RegClass_GPR] & (1 << defInstr->args[0].data.reg.reg))) &&
        !usedbetween(instr, defInstr, &defInstr->args[0]) &&
        !definedbetween(instr, defInstr, &instr->args[0]) &&
        !usedbetween(instr, defInstr, &instr->args[0])) {
        defInstr->args[0].data.reg.reg = instr->args[0].data.reg.reg;
        deletepcode(instr);
        return 1;
    }

    if (defInstr->op == PC_EXTSB && !(PCODE_FLAG_SET_F(instr) & fRecordBit)) {
        if (defInstr->args[0].data.reg.reg == defInstr->args[1].data.reg.reg) {
            if (!(masks[RegClass_GPR] & (1 << defInstr->args[0].data.reg.reg)) &&
                !usedbetween(instr, defInstr, &defInstr->args[0])) {
                change_opcode(instr, PC_EXTSB);
                deletepcode(defInstr);
                return 1;
            }
        } else {
            if (!definedbetween(instr, defInstr, &defInstr->args[1])) {
                change_opcode(instr, PC_EXTSB);
                instr->args[1] = defInstr->args[1];
            } else {
                change_opcode(instr, PC_MR);
            }
            return 1;
        }
    }

    return 0;
}

// TODO: the name is made up: LI_L_S clears the operand through a helper that -inline auto inlines
// (written in the caller, the original's reloads of the caller's parameter are missing).
static void clear_base_register(PCodeArg* op) {
    op->data.reg.reg = 0;
    op->data.reg.effect &= ~(EffectRead | EffectWrite);
}

// TODO: the name is made up: a 2.6 addition (li rX, sym; load/store 0(rX) -> load/store sym(r0)),
// named like its neighbours.
static int LI_L_S(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID + 1];

    if (defInstr->op ==
        PC_LI &&
        defInstr->args[1].kind ==
        PCOp_MEMORY &&
        instr->args[2].kind ==
        PCOp_IMMEDIATE &&
        instr->args[2].data.imm.value ==
        0 &&
        defInstr->args[0].data.reg.reg ==
        instr->args[1].data.reg.reg &&
        !definedbetween(instr, defInstr, &defInstr->args[0])) {
        clear_base_register(&instr->args[1]);
        instr->args[2] = defInstr->args[1];
        defininginstruction[instr->defID + 1] = defininginstruction[defInstr->defID + 1];
        if (!usedbetween(instr, defInstr, &defInstr->args[0]) &&
            !(masks[RegClass_GPR] & (1 << defInstr->args[0].data.reg.reg))) {
            deletepcode(defInstr);
        }
        return 1;
    }

    return 0;
}

static int ADDI_L_S(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID + 1];
    SInt32 addleft;
    SInt32 addright;

    if (defInstr->op == PC_ADDI && instr->args[2].kind == PCOp_IMMEDIATE) {
        if (!PC_OP_IS_REGISTER(&instr->args[0], RegClass_GPR, instr->args[1].data.reg.reg)) {
            if (instr->args[2].data.imm.value ==
                0 &&
                defInstr->args[0].data.reg.reg ==
                defInstr->args[1].data.reg.reg &&
                !usedbetween(instr, defInstr, &defInstr->args[0]) &&
                (!(masks[RegClass_GPR] & (1 << defInstr->args[0].data.reg.reg)) ||
                 canuseupdate(instr))) {
                if (!(masks[RegClass_GPR] & (1 << defInstr->args[0].data.reg.reg))) {
                    instr->args[2] = defInstr->args[2];
                } else {
                    // TODO: 2.6 keeps `instr` loaded across these stores (no reload after each); a
                    // block-local copy here and below gives that, Pro7's direct uses do not
                    PCode* t = instr;
                    t->op++;
                    t->args[1].data.reg.effect |= EffectWrite;
                    t->args[2] = defInstr->args[2];
                }
                defininginstruction[instr->defID + 1] = defininginstruction[defInstr->defID + 1];
                deletepcode(defInstr);
                return 1;
            }

            addleft = 0x1FFFF;
            addright = instr->args[2].data.imm.value;
            if (defInstr->args[2].kind == PCOp_IMMEDIATE) {
                addleft = defInstr->args[2].data.imm.value;
            } else if (defInstr->args[2].kind == PCOp_MEMORY) {
                if (defInstr->args[2].data.mem.obj->datatype == DLOCAL) {
                    addleft = defInstr->args[2].data.mem.offset +
                        defInstr->args[2].data.mem.obj->u.var.uid;
                } else if (addright == 0) {
                    addleft = 0;
                } else {
                    return 0;
                }
            }

            if (!FITS_IN_SHORT(addleft + addright)) {
                return 0;
            }

            if (!(masks[RegClass_GPR] & (1 << defInstr->args[0].data.reg.reg)) &&
                !usedbetween(instr, defInstr, &defInstr->args[0]) &&
                !definedbetween(instr, defInstr, &defInstr->args[1])) {
                {
                    PCode* t = instr;
                    t->args[1].data.reg.reg = defInstr->args[1].data.reg.reg;
                    defininginstruction[t->defID + 1] = defininginstruction[defInstr->defID + 1];
                }

                if (defInstr->args[2].kind == PCOp_MEMORY) {
                    instr->args[2] = defInstr->args[2];
                    instr->args[2].data.mem.offset += addright;
                    if (instr->flags & (fIsRead | fIsWrite | fOpFromMemory | fOpToMemory)) {
                        instr->alias = make_alias(
                            instr->args[2].data.mem.obj,
                            instr->args[2].data.mem.offset,
                            nbytes_loaded_or_stored_by(instr)
                        );
                    }
                } else {
                    instr->args[2].data.imm.value = addleft + addright;
                }

                deletepcode(defInstr);
                return 1;
            }

            if (instr->args[1].data.reg.reg !=
                defInstr->args[1].data.reg.reg &&
                !definedbetween(instr, defInstr, &defInstr->args[1])) {
                if (defInstr->args[2].kind ==
                    PCOp_MEMORY &&
                    defInstr->args[2].data.mem.obj->datatype != DLOCAL) {
                    return 0;
                }

                instr->args[1].data.reg.reg = defInstr->args[1].data.reg.reg;
                defininginstruction[instr->defID + 1] = defininginstruction[defInstr->defID + 1];

                if (defInstr->args[2].kind == PCOp_MEMORY) {
                    instr->args[2] = defInstr->args[2];
                    instr->args[2].data.mem.offset += addright;
                    if (instr->flags & (fIsRead | fIsWrite | fOpFromMemory | fOpToMemory)) {
                        instr->alias = make_alias(
                            instr->args[2].data.mem.obj,
                            instr->args[2].data.mem.offset,
                            nbytes_loaded_or_stored_by(instr)
                        );
                    }
                } else {
                    instr->args[2].data.imm.value = addleft + addright;
                }

                return 1;
            }
        }
    } else {
        if (defInstr->op ==
            PC_MR &&
            PC_OP_IS_ANY_REGISTER(&defInstr->args[1], RegClass_GPR) &&
            defInstr->args[1].data.reg.reg !=
            0 &&
            !definedbetween(instr, defInstr, &defInstr->args[1])) {
            instr->args[1].data.reg.reg = defInstr->args[1].data.reg.reg;
            defininginstruction[instr->defID + 1] = defininginstruction[defInstr->defID + 1];
        }
    }

    return 0;
}

static int ADDI_LU_SU(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID + 1];

    if (instr->args[2].kind ==
        PCOp_IMMEDIATE &&
        defInstr->args[2].kind ==
        PCOp_IMMEDIATE &&
        defInstr->op ==
        PC_ADDI &&
        defInstr->args[0].data.reg.reg ==
        defInstr->args[1].data.reg.reg &&
        !(instr->args[0].arg ==
          instr->args[1].arg &&
          instr->args[0].data.reg.reg == instr->args[1].data.reg.reg) &&
        !usedbetween(instr, defInstr, &defInstr->args[0]) &&
        FITS_IN_SHORT(defInstr->args[2].data.imm.value + instr->args[2].data.imm.value)) {
        if ((instr->args[2].data.imm.value + defInstr->args[2].data.imm.value) == 0) {
            instr->op--;
            instr->args[1].data.reg.effect &= ~EffectWrite;
            instr->args[2].data.imm.value = 0;
        } else {
            instr->args[2].data.imm.value =
                instr->args[2].data.imm.value + defInstr->args[2].data.imm.value;
        }

        defininginstruction[instr->defID + 1] = defininginstruction[defInstr->defID + 1];
        deletepcode(defInstr);
        return 1;
    }

    return 0;
}

static int L_S_ADDI(PCode* instr, UInt32* masks) {
    PCode* scan;
    PCodeArg* op;
    int i;
    int reg;

    if (instr->args[2].kind != PCOp_IMMEDIATE) {
        return 0;
    }

    reg = instr->args[1].data.reg.reg;
    if (!canuseupdate(instr)) {
        return 0;
    }

    for (scan = instr->nextPCode; scan; scan = scan->nextPCode) {
        for (op = scan->args, i = scan->argCount; i--; op++) {
            if (PC_OP_IS_READ_REGISTER(op, RegClass_GPR, reg)) {
                return 0;
            }

            if (PC_OP_IS_WRITE_REGISTER(op, RegClass_GPR, reg)) {
                if (scan->op != PC_ADDI) {
                    return 0;
                }
                if (scan->args[2].kind != PCOp_IMMEDIATE) {
                    return 0;
                }

                if (instr->args[2].data.imm.value ==
                    scan->args[2].data.imm.value &&
                    scan->args[0].data.reg.reg ==
                    scan->args[1].data.reg.reg &&
                    !(instr->args[0].arg ==
                      instr->args[1].arg &&
                      instr->args[0].data.reg.reg == instr->args[1].data.reg.reg)) {
                    if (!(masks[RegClass_GPR] & (1 << scan->args[0].data.reg.reg))) {
                        instr->args[2] = scan->args[2];
                    } else {
                        instr->op++;
                        instr->args[1].data.reg.effect |= EffectWrite;
                        instr->args[2] = scan->args[2];
                    }

                    change_opcode(scan, PC_NOP);
                    change_num_operands(scan, 0);
                    deletepcode(scan);
                    return 1;
                }

                return 0;
            }
        }
    }

    return 0;
}

static int LI_CMP_BC(PCode* instr, UInt32* masks) {
    PCode* defInstr;
    PCode* defInstr2;
    PCLink* link;
    PCLink** ptr;

    if (instr->args[1].data.imm.value == 2) {
        defInstr = defininginstruction[instr->defID];
        if ((defInstr->op == PC_CMPLI || defInstr->op == PC_CMPI) &&
            defInstr->args[0].data.reg.reg == 0) {
            defInstr2 = defininginstruction[defInstr->defID + 1];
            if (defInstr2->op ==
                PC_LI &&
                defInstr2->args[1].kind ==
                PCOp_IMMEDIATE &&
                (instr->op == PC_BT) ==
                (defInstr2->args[1].data.imm.value == defInstr->args[2].data.imm.value)) {
                change_opcode(instr, PC_B);
                instr->args[0] = instr->args[2];
                change_num_operands(instr, 1);

                defininginstruction[instr->defID] = defininginstruction[instr->defID + 1];

                for (ptr = &instr->block->successors; (link = *ptr); ptr = &link->nextLink) {
                    if (link->block == instr->block->nextBlock) {
                        *ptr = link->nextLink;
                        break;
                    }
                }

                for (ptr = &instr->block->nextBlock->predecessors; (link = *ptr);
                     ptr = &link->nextLink) {
                    if (link->block == instr->block) {
                        *ptr = link->nextLink;
                        break;
                    }
                }
            }
        }
    }

    return 0;
}

static int RLWINM_CMPLI_BC(PCode* instr, UInt32* masks) {
    PCode* defInstr;
    PCode* defInstr2;

    if (instr->args[1].data.imm.value == 2) {
        defInstr = defininginstruction[instr->defID];
        if (defInstr->op ==
            PC_CMPLI &&
            defInstr->args[0].data.reg.reg ==
            0 &&
            defInstr->args[2].data.imm.value == 0) {
            defInstr2 = defininginstruction[defInstr->defID + 1];
            if ((PCODE_FLAG_SET_F(defInstr2) & (fSideEffects | fCanSetRecordBit | fOpTypeGPR)) ==
                (fCanSetRecordBit | fOpTypeGPR) &&
                !usedbetween(defInstr, defInstr2, &defInstr->args[0]) &&
                !definedbetween(defInstr, defInstr2, &defInstr->args[0])) {
                pcsetrecordbit(defInstr2);
                defininginstruction[instr->defID] = defininginstruction[defInstr->defID + 1];
                deletepcode(defInstr);
                return 1;
            }
        }
    }

    return 0;
}

static int LBZ_EXTSB_CMPI_BC(PCode* instr, UInt32* masks) {
    PCode* defInstr;
    PCode* defInstr2;

    if (instr->args[1].data.imm.value == 2) {
        defInstr = defininginstruction[instr->defID];
        if (((defInstr->op == PC_CMPI || defInstr->op == PC_CMPLI) &&
             defInstr->args[2].data.imm.value >=
             0 &&
             defInstr->args[2].data.imm.value <= 127) ||
            ((defInstr->op == PC_EXTSB || defInstr->op == PC_EXTSH) &&
             (PCODE_FLAG_SET_F(defInstr) & fRecordBit))) {
            defInstr2 = defininginstruction[defInstr->defID + 1];
            if (defInstr2->op ==
                PC_EXTSB &&
                defininginstruction[defInstr2->defID + 1]->op ==
                PC_LBZ &&
                !(masks[RegClass_GPR] & (1 << defInstr2->args[0].data.reg.reg)) &&
                !usedbetween(instr, defInstr, &defInstr2->args[0]) &&
                !usedbetween(defInstr, defInstr2, &defInstr2->args[0]) &&
                !definedbetween(defInstr, defInstr2, &defInstr2->args[1])) {
                {
                    // TODO: 2.6 keeps `defInstr` loaded across the store (no reload); a block-local
                    // copy gives that (as in ADDI_L_S), Pro7's direct uses do not
                    PCode* t = defInstr;
                    t->args[1].data.reg.reg = defInstr2->args[1].data.reg.reg;
                    defininginstruction[t->defID + 1] = defininginstruction[defInstr2->defID + 1];
                }
                deletepcode(defInstr2);
                return 1;
            }
        }
    }

    return 0;
}

static int FRSP_STFS(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID];

    if (defInstr->op ==
        PC_FRSP &&
        !(masks[RegClass_FPR] & (1 << defInstr->args[0].data.reg.reg)) &&
        !(PCODE_FLAG_SET_F(defInstr) & fRecordBit) &&
        !definedbetween(instr, defInstr, &defInstr->args[1]) &&
        !usedbetween(instr, defInstr, &defInstr->args[0])) {
        instr->args[0].data.reg.reg = defInstr->args[1].data.reg.reg;
        defininginstruction[instr->defID] = defininginstruction[defInstr->defID + 1];
        deletepcode(defInstr);
        return 1;
    }

    return 0;
}

static int NOT_AND(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID + 2];

    if (defInstr->op ==
        PC_NOT &&
        (defInstr->args[0].data.reg.reg ==
         instr->args[0].data.reg.reg ||
         !(masks[RegClass_GPR] & (1 << defInstr->args[0].data.reg.reg))) &&
        !(PCODE_FLAG_SET_F(defInstr) & fRecordBit) &&
        !definedbetween(instr, defInstr, &defInstr->args[1]) &&
        !usedbetween(instr, defInstr, &defInstr->args[0])) {
        instr->args[2].data.reg.reg = defInstr->args[1].data.reg.reg;
        defininginstruction[instr->defID + 2] = defininginstruction[defInstr->defID + 1];
        change_opcode(instr, PC_ANDC);
        deletepcode(defInstr);
        return 1;
    }

    return 0;
}

static int LI_MR(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID + 1];

    if (defInstr->op ==
        PC_LI &&
        (defInstr->args[0].data.reg.reg ==
         instr->args[0].data.reg.reg ||
         !(masks[RegClass_GPR] & (1 << defInstr->args[0].data.reg.reg))) &&
        !(PCODE_FLAG_SET_F(instr) & fRecordBit) &&
        !usedbetween(instr, defInstr, &defInstr->args[0])) {
        change_opcode(instr, PC_LI);
        instr->args[1] = defInstr->args[1];
        defininginstruction[instr->defID + 1] = defininginstruction[defInstr->defID + 1];
        deletepcode(defInstr);
        return 1;
    }

    return 0;
}

static int VSPLTIS_VMR(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID + 1];
    short opcode = defInstr->op;

    if ((opcode == PC_VSPLTISB || opcode == PC_VSPLTISH || opcode == PC_VSPLTISW) &&
        (defInstr->args[0].data.reg.reg ==
         instr->args[0].data.reg.reg ||
         !(masks[RegClass_VR] & (1 << defInstr->args[0].data.reg.reg))) &&
        !(PCODE_FLAG_SET_F(instr) & fRecordBit) &&
        !usedbetween(instr, defInstr, &defInstr->args[0])) {
        change_opcode(instr, opcode);
        instr->args[1] = defInstr->args[1];
        defininginstruction[instr->defID + 1] = defininginstruction[defInstr->defID + 1];
        deletepcode(defInstr);
        return 1;
    }

    return 0;
}

static int L_MR(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID + 1];

    if ((defInstr->flags & fIsRead) &&
        !(masks[RegClass_GPR] & (1 << defInstr->args[0].data.reg.reg)) &&
        !(PCODE_FLAG_SET_F(instr) & fRecordBit) &&
        !usedbetween(instr, defInstr, &defInstr->args[0]) &&
        !usedbetween(instr, defInstr, &instr->args[0]) &&
        !definedbetween(instr, defInstr, &instr->args[0])) {
        defInstr->args[0].data.reg.reg = instr->args[0].data.reg.reg;
        deletepcode(instr);
        return 1;
    }

    return 0;
}

static int L_FMR(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID + 1];

    if ((defInstr->flags & fIsRead) &&
        !(masks[RegClass_FPR] & (1 << defInstr->args[0].data.reg.reg)) &&
        !(PCODE_FLAG_SET_F(instr) & fRecordBit) &&
        !usedbetween(instr, defInstr, &defInstr->args[0]) &&
        !usedbetween(instr, defInstr, &instr->args[0]) &&
        !definedbetween(instr, defInstr, &instr->args[0])) {
        defInstr->args[0].data.reg.reg = instr->args[0].data.reg.reg;
        deletepcode(instr);
        return 1;
    }

    return 0;
}

static int L_S(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID];
    SInt32 isIndexed;
    SInt32 isFloat;
    SInt32 isVector;
    PCode* scan;
    SInt32 loadSize;
    SInt32 defInstrOffset;
    SInt32 scanOffset;
    SInt32 storeSize;

#ifdef __wasm__
    /* A store between to another object leaves both offsets unset, and the original compares its stack slots as
       earlier calls left them. The scan offset's slot holds a pointer, as computeinstructionpredecessors leaves it
       (its nop), so the offsets never overlap. (This assumes no L_S call since then has set them.) */
    defInstrOffset = 0;
    scanOffset = 0x7FFFFFFF;
#endif

    if (PCODE_FLAG_SET_F(instr) & (fIsVolatile | fSideEffects | fUpdatesPtr)) {
        return 0;
    }
    if (PCODE_FLAG_SET_F(defInstr) & (fIsVolatile | fSideEffects | fUpdatesPtr)) {
        return 0;
    }

    if ((defInstr->flags & fIsRead) &&
        PC_OP_IS_REGISTER(&defInstr->args[1], RegClass_GPR, instr->args[1].data.reg.reg) &&
        defInstr->args[2].kind ==
        instr->args[2].kind &&
        !definedbetween(instr, defInstr, &instr->args[1])) {
        if (instr->args[2].kind == PCOp_IMMEDIATE) {
            if (instr->args[2].data.imm.value != defInstr->args[2].data.imm.value) {
                return 0;
            }
        } else if (instr->args[2].kind == PCOp_MEMORY) {
            if (instr->args[2].data.mem.offset !=
                defInstr->args[2].data.mem.offset ||
                instr->args[2].data.mem.obj != defInstr->args[2].data.mem.obj) {
                return 0;
            }
        } else if (instr->args[2].kind == PCOp_REGISTER && instr->args[2].arg == RegClass_GPR) {
            if (instr->args[2].data.reg.reg !=
                defInstr->args[2].data.reg.reg ||
                definedbetween(instr, defInstr, &instr->args[2])) {
                return 0;
            }
        } else {
            return 0;
        }

        isIndexed = 0;
        isFloat = 0;
        isVector = 0;
        switch (defInstr->op) {
            case PC_LBZX:
                isIndexed = 1;
            case PC_LBZ:
                loadSize = 1;
                break;
            case PC_LHZX:
            case PC_LHAX:
                isIndexed = 1;
            case PC_LHZ:
            case PC_LHA:
                loadSize = 2;
                break;
            case PC_LWZX:
                isIndexed = 1;
            case PC_LWZ:
                loadSize = 4;
                break;
            case PC_LFSX:
                isIndexed = 1;
            case PC_LFS:
                isFloat = 1;
                loadSize = 4;
                break;
            case PC_LFDX:
                isIndexed = 1;
            case PC_LFD:
                isFloat = 1;
                loadSize = 8;
                break;
            case PC_LVX:
            case PC_LVXL:
                isIndexed = 1;
                isVector = 1;
                loadSize = 16;
                break;
            default:
                return 0;
        }

        switch (instr->op) {
            case PC_STBX:
                if (!isIndexed) {
                    return 0;
                }
            case PC_STB:
                if (isFloat) {
                    return 0;
                }
                if (loadSize != 1) {
                    return 0;
                }
                break;
            case PC_STHX:
                if (!isIndexed) {
                    return 0;
                }
            case PC_STH:
                if (isFloat) {
                    return 0;
                }
                if (loadSize != 2) {
                    return 0;
                }
                break;
            case PC_STWX:
                if (!isIndexed) {
                    return 0;
                }
            case PC_STW:
                if (isFloat) {
                    return 0;
                }
                if (loadSize != 4) {
                    return 0;
                }
                break;
            case PC_STFSX:
                if (!isIndexed) {
                    return 0;
                }
            case PC_STFS:
                if (!isFloat) {
                    return 0;
                }
                if (loadSize != 4) {
                    return 0;
                }
                break;
            case PC_STFDX:
                if (!isIndexed) {
                    return 0;
                }
            case PC_STFD:
                if (!isFloat) {
                    return 0;
                }
                if (loadSize != 8) {
                    return 0;
                }
                break;
            case PC_STVX:
            case PC_STVXL:
                if (!isIndexed) {
                    return 0;
                }
                if (!isVector) {
                    return 0;
                }
                if (loadSize != 16) {
                    return 0;
                }
                break;
            default:
                return 0;
        }

        for (scan = instr->prevPCode; scan && scan != defInstr; scan = scan->prevPCode) {
            if (scan->flags & fIsWrite) {
                if (scan->args[1].data.reg.reg != instr->args[1].data.reg.reg) {
                    return 0;
                }
                if (scan->args[2].kind != defInstr->args[2].kind) {
                    return 0;
                }

                if (scan->args[2].kind == PCOp_MEMORY) {
                    if (instr->args[2].data.mem.obj == scan->args[2].data.mem.obj) {
                        if (instr->args[2].data.mem.offset == defInstr->args[2].data.mem.offset) {
                            return 0;
                        }
                        defInstrOffset = defInstr->args[2].data.mem.offset;
                        scanOffset = scan->args[2].data.mem.offset;
                    }
                } else if (scan->args[2].kind == PCOp_IMMEDIATE) {
                    if (instr->args[1].data.reg.reg != scan->args[1].data.reg.reg) {
                        return 0;
                    }
                    if (instr->args[2].data.imm.value == scan->args[2].data.imm.value) {
                        return 0;
                    }
                    defInstrOffset = defInstr->args[2].data.imm.value;
                    scanOffset = scan->args[2].data.imm.value;
                } else {
                    return 0;
                }

                switch (scan->op) {
                    case PC_STB:
                    case PC_STBX:
                        storeSize = 1;
                        break;
                    case PC_STH:
                    case PC_STHX:
                        storeSize = 2;
                        break;
                    case PC_STW:
                    case PC_STWX:
                    case PC_STFS:
                    case PC_STFSX:
                        storeSize = 4;
                        break;
                    case PC_STFD:
                    case PC_STFDX:
                        storeSize = 8;
                        break;
                    case PC_STVX:
                    case PC_STVXL:
                        storeSize = 16;
                        break;
                    default:
                        return 0;
                }

                if (defInstrOffset > scanOffset) {
                    if ((scanOffset + storeSize) > defInstrOffset) {
                        return 0;
                    }
                } else {
                    if ((defInstrOffset + loadSize) > scanOffset) {
                        return 0;
                    }
                }
            }
        }

        deletepcode(instr);
        return 1;
    }

    return 0;
}

static int RLWINM_RLWINM(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID + 1];
    short start;
    short end;

    if (defInstr->op ==
        PC_RLWINM &&
        !(PCODE_FLAG_SET_F(defInstr) & fRecordBit) &&
        !definedbetween(instr, defInstr, &defInstr->args[1]) &&
        !(defInstr->args[0].data.reg.reg ==
          defInstr->args[1].data.reg.reg &&
          ((defInstr->args[0].data.reg.reg !=
            instr->args[0].data.reg.reg &&
            (masks[RegClass_GPR] & (1 << defInstr->args[0].data.reg.reg))) ||
           usedbetween(instr, defInstr, &defInstr->args[0]))) &&
        canmergemasks(
            defInstr->args[3].data.imm.value,
            defInstr->args[4].data.imm.value,
            instr->args[2].data.imm.value,
            instr->args[3].data.imm.value,
            instr->args[4].data.imm.value,
            &start,
            &end
        )) {
        if (instr->op == PC_RLWIMI) {
            if (instr->args[0].data.reg.reg == defInstr->args[0].data.reg.reg) {
                return 0;
            }
            if (start != instr->args[3].data.imm.value || end != instr->args[4].data.imm.value) {
                return 0;
            }
        }

        instr->args[1].data.reg.reg = defInstr->args[1].data.reg.reg;
        defininginstruction[instr->defID + 1] = defininginstruction[defInstr->defID + 1];

        instr->args[2].data.imm.value =
            (instr->args[2].data.imm.value + defInstr->args[2].data.imm.value) & 31;
        instr->args[3].data.imm.value = start;
        instr->args[4].data.imm.value = end;

        if ((defInstr->args[0].data.reg.reg ==
             instr->args[0].data.reg.reg ||
             !(masks[RegClass_GPR] & (1 << defInstr->args[0].data.reg.reg))) &&
            !usedbetween(instr, defInstr, &defInstr->args[0])) {
            deletepcode(defInstr);
        }

        return 1;
    }

    if (defInstr->op ==
        PC_MR &&
        instr->op ==
        PC_RLWINM &&
        !definedbetween(instr, defInstr, &defInstr->args[1])) {
        instr->args[1].data.reg.reg = defInstr->args[1].data.reg.reg;
        defininginstruction[instr->defID + 1] = defininginstruction[defInstr->defID + 1];

        if ((defInstr->args[0].data.reg.reg ==
             instr->args[0].data.reg.reg ||
             !(masks[RegClass_GPR] & (1 << defInstr->args[0].data.reg.reg))) &&
            !(PCODE_FLAG_SET_F(defInstr) & fRecordBit) &&
            !usedbetween(instr, defInstr, &defInstr->args[0])) {
            deletepcode(defInstr);
        }

        return 1;
    }

    return 0;
}

static int MULLI_MULLI(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID + 1];

    if (defInstr->op ==
        PC_MULLI &&
        (defInstr->args[0].data.reg.reg ==
         instr->args[0].data.reg.reg ||
         !(masks[RegClass_GPR] & (1 << defInstr->args[0].data.reg.reg))) &&
        !(PCODE_FLAG_SET_F(defInstr) & fRecordBit) &&
        !definedbetween(instr, defInstr, &defInstr->args[1]) &&
        !usedbetween(instr, defInstr, &defInstr->args[0]) &&
        FITS_IN_SHORT(instr->args[2].data.imm.value * defInstr->args[2].data.imm.value)) {
        instr->args[1].data.reg.reg = defInstr->args[1].data.reg.reg;
        defininginstruction[instr->defID + 1] = defininginstruction[defInstr->defID + 1];

        instr->args[2].data.imm.value *= defInstr->args[2].data.imm.value;
        deletepcode(defInstr);
        return 1;
    }

    return 0;
}

static int ADDI_ADDI(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID + 1];

    if (defInstr->op ==
        PC_ADDI &&
        (defInstr->args[0].data.reg.reg ==
         instr->args[0].data.reg.reg ||
         !(masks[RegClass_GPR] & (1 << defInstr->args[0].data.reg.reg))) &&
        !(PCODE_FLAG_SET_F(defInstr) & fRecordBit) &&
        !definedbetween(instr, defInstr, &defInstr->args[1]) &&
        !usedbetween(instr, defInstr, &defInstr->args[0]) &&
        instr->args[2].kind ==
        PCOp_IMMEDIATE &&
        defInstr->args[2].kind ==
        PCOp_IMMEDIATE &&
        FITS_IN_SHORT(instr->args[2].data.imm.value + defInstr->args[2].data.imm.value)) {
        instr->args[1].data.reg.reg = defInstr->args[1].data.reg.reg;
        defininginstruction[instr->defID + 1] = defininginstruction[defInstr->defID + 1];

        instr->args[2].data.imm.value += defInstr->args[2].data.imm.value;
        deletepcode(defInstr);
        return 1;
    }

    return 0;
}

static int SRAWI_SRAWI(PCode* instr, UInt32* masks) {
    PCode* defInstr = defininginstruction[instr->defID + 1];

    if (defInstr->op ==
        PC_SRAWI &&
        (defInstr->args[0].data.reg.reg ==
         instr->args[0].data.reg.reg ||
         !(masks[RegClass_GPR] & (1 << defInstr->args[0].data.reg.reg))) &&
        !(PCODE_FLAG_SET_F(defInstr) & fRecordBit) &&
        !definedbetween(instr, defInstr, &defInstr->args[1]) &&
        !usedbetween(instr, defInstr, &defInstr->args[0]) &&
        instr->args[2].kind ==
        PCOp_IMMEDIATE &&
        defInstr->args[2].kind ==
        PCOp_IMMEDIATE &&
        (instr->args[2].data.imm.value + defInstr->args[2].data.imm.value) <
        32 &&
        (instr->args[2].data.imm.value + defInstr->args[2].data.imm.value) > 0) {
        instr->args[1].data.reg.reg = defInstr->args[1].data.reg.reg;
        defininginstruction[instr->defID + 1] = defininginstruction[defInstr->defID + 1];

        instr->args[2].data.imm.value += defInstr->args[2].data.imm.value;
        deletepcode(defInstr);
        return 1;
    }

    return 0;
}

static int MR_ADDI(PCode* instr, UInt32* masks) {
    PCode* prev = instr->prevPCode;
    PCode* next = instr->nextPCode;
    int prevFlag = 0;
    int nextFlag = 0;

    if (copts.processor == CPU_PPC603e) {
        if (prev) {
            prevFlag = (prev->flags & fOpTypeMask) == fOpTypeGPR;
        }
        if (next) {
            nextFlag = (next->flags & fOpTypeMask) == fOpTypeGPR;
        }

        if (!(PCODE_FLAG_SET_F(instr) & fRecordBit) &&
            instr->argCount >=
            2 &&
            instr->args[1].data.reg.reg !=
            0 &&
            (prevFlag || nextFlag)) {
            change_opcode(instr, PC_ADDI);
            instr->args[2].kind = PCOp_IMMEDIATE;
            instr->args[2].data.imm.value = 0;
            instr->args[2].data.imm.obj = NULL;
            change_num_operands(instr, 3);
        }
    }

    return 0;
}

static int rotatedefinedusedtest(UInt32* masks, PCode* instr, PCode* a, PCode* b, PCode* subfic) {
    PCode* scan;
    PCodeArg* op;
    int i;
    // (Pro7: int; 2.6 keeps the two register numbers as shorts)
    SInt16 reg1;
    SInt16 reg2;

    if ((masks[RegClass_GPR] & (1 << subfic->args[0].data.reg.reg)) &&
        subfic->args[0].data.reg.reg !=
        instr->args[0].data.reg.reg &&
        subfic->args[0].data.reg.reg != a->args[2].data.reg.reg) {
        return 1;
    }

    for (scan = instr->block->firstPCode; scan != instr->block->lastPCode; scan = scan->nextPCode) {
        if (scan == a) {
            break;
        }
        if (scan == b) {
            break;
        }
        if (scan == subfic) {
            break;
        }
    }

    reg1 = a->args[1].data.reg.reg;
    reg2 = subfic->args[1].data.reg.reg;
    while (scan != instr) {
        for (op = instr->args, i = instr->argCount; i--; op++) {
            if (op->kind ==
                PCOp_REGISTER &&
                op->arg ==
                RegClass_GPR &&
                (((op->data.reg.reg == reg1 || op->data.reg.reg == reg2) &&
                  (op->data.reg.effect & EffectWrite)) ||
                 (op->data.reg.reg ==
                  subfic->args[0].data.reg.reg &&
                  (op->data.reg.effect & EffectRead))) &&
                scan !=
                a &&
                scan !=
                b &&
                scan != subfic) {
                return 1;
            }
        }

        scan = scan->nextPCode;
    }

    return 0;
}

static int SRW_SUBFIC_RLW_OR(PCode* instr, UInt32* masks) {
    PCode* subfic;
    PCode* defInstr1 = defininginstruction[instr->defID + 1];
    PCode* defInstr2 = defininginstruction[instr->defID + 2];

    if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
        return 0;
    }

    if ((masks[RegClass_GPR] & (1 << instr->args[1].data.reg.reg)) &&
        instr->args[1].data.reg.reg != instr->args[0].data.reg.reg) {
        return 0;
    }

    if ((masks[RegClass_GPR] & (1 << instr->args[2].data.reg.reg)) &&
        instr->args[1].data.reg.reg != instr->args[0].data.reg.reg) {
        return 0;
    }

    if (defInstr1->op != PC_SRW && defInstr1->op != PC_SLW) {
        return 0;
    }
    if (defInstr2->op != PC_SRW && defInstr2->op != PC_SLW) {
        return 0;
    }

    if (usedbetween(instr, defInstr1, &defInstr1->args[0])) {
        return 0;
    }
    if (usedbetween(instr, defInstr2, &defInstr2->args[0])) {
        return 0;
    }

    if (defInstr1->op ==
        PC_SRW &&
        defInstr2->op ==
        PC_SLW &&
        defInstr1->args[1].data.reg.reg == defInstr2->args[1].data.reg.reg) {
        subfic = defininginstruction[defInstr1->defID + 2];
        if (subfic->op ==
            PC_SUBFIC &&
            subfic->args[1].data.reg.reg ==
            defInstr2->args[2].data.reg.reg &&
            subfic->args[2].data.imm.value == 32) {
            if (rotatedefinedusedtest(masks, instr, defInstr2, defInstr1, subfic)) {
                return 0;
            }

            change_opcode(instr, PC_RLWNM);

            instr->args[1] = defInstr1->args[1];
            defininginstruction[instr->defID + 1] = defininginstruction[defInstr1->defID + 1];

            instr->args[2] = defInstr2->args[2];
            defininginstruction[instr->defID + 2] = defininginstruction[defInstr2->defID + 2];

            instr->args[3].kind = PCOp_IMMEDIATE;
            instr->args[3].data.imm.value = 0;
            instr->args[3].data.imm.obj = NULL;

            instr->args[4].kind = PCOp_IMMEDIATE;
            instr->args[4].data.imm.value = 31;
            instr->args[4].data.imm.obj = NULL;

            deletepcode(defInstr1);
            deletepcode(defInstr2);
            deletepcode(subfic);
            return 1;
        }

        subfic = defininginstruction[defInstr2->defID + 2];
        if (subfic->op ==
            PC_SUBFIC &&
            subfic->args[1].data.reg.reg ==
            defInstr1->args[2].data.reg.reg &&
            subfic->args[2].data.imm.value == 32) {
            if (rotatedefinedusedtest(masks, instr, defInstr2, defInstr1, subfic)) {
                return 0;
            }

            change_opcode(instr, PC_RLWNM);

            instr->args[1] = defInstr1->args[1];
            defininginstruction[instr->defID + 1] = defininginstruction[defInstr1->defID + 1];

            instr->args[2] = defInstr2->args[2];
            defininginstruction[instr->defID + 2] = defininginstruction[defInstr2->defID + 2];

            instr->args[3].kind = PCOp_IMMEDIATE;
            instr->args[3].data.imm.value = 0;
            instr->args[3].data.imm.obj = NULL;

            instr->args[4].kind = PCOp_IMMEDIATE;
            instr->args[4].data.imm.value = 31;
            instr->args[4].data.imm.obj = NULL;

            deletepcode(defInstr1);
            deletepcode(defInstr2);
            return 1;
        }
    } else if (defInstr1->op ==
               PC_SLW &&
               defInstr2->op ==
               PC_SRW &&
               defInstr1->args[1].data.reg.reg == defInstr2->args[1].data.reg.reg) {
        subfic = defininginstruction[defInstr1->defID + 2];
        if (subfic->op ==
            PC_SUBFIC &&
            subfic->args[1].data.reg.reg ==
            defInstr2->args[2].data.reg.reg &&
            subfic->args[2].data.imm.value == 32) {
            if (rotatedefinedusedtest(masks, instr, defInstr1, defInstr2, subfic)) {
                return 0;
            }

            change_opcode(instr, PC_RLWNM);

            instr->args[1] = defInstr1->args[1];
            defininginstruction[instr->defID + 1] = defininginstruction[defInstr1->defID + 1];

            instr->args[2] = defInstr1->args[2];
            defininginstruction[instr->defID + 2] = defininginstruction[defInstr1->defID + 2];

            instr->args[3].kind = PCOp_IMMEDIATE;
            instr->args[3].data.imm.value = 0;
            instr->args[3].data.imm.obj = NULL;

            instr->args[4].kind = PCOp_IMMEDIATE;
            instr->args[4].data.imm.value = 31;
            instr->args[4].data.imm.obj = NULL;

            deletepcode(defInstr1);
            deletepcode(defInstr2);
            return 1;
        }

        subfic = defininginstruction[defInstr2->defID + 2];
        if (subfic->op ==
            PC_SUBFIC &&
            subfic->args[1].data.reg.reg ==
            defInstr1->args[2].data.reg.reg &&
            subfic->args[2].data.imm.value == 32) {
            if (rotatedefinedusedtest(masks, instr, defInstr1, defInstr2, subfic)) {
                return 0;
            }

            change_opcode(instr, PC_RLWNM);

            instr->args[1] = defInstr1->args[1];
            defininginstruction[instr->defID + 1] = defininginstruction[defInstr1->defID + 1];

            instr->args[2] = defInstr1->args[2];
            defininginstruction[instr->defID + 2] = defininginstruction[defInstr1->defID + 2];

            instr->args[3].kind = PCOp_IMMEDIATE;
            instr->args[3].data.imm.value = 0;
            instr->args[3].data.imm.obj = NULL;

            instr->args[4].kind = PCOp_IMMEDIATE;
            instr->args[4].data.imm.value = 31;
            instr->args[4].data.imm.obj = NULL;

            deletepcode(defInstr1);
            deletepcode(defInstr2);
            deletepcode(subfic);
            return 1;
        }
    }

    return 0;
}

static int RLWINM_RLWIMI_STW(PCode* instr, UInt32* masks) {
    PCode* newInstr;
    Boolean isZeroOffset;
    PCode* scan;
    int i;
    int flags;
    PCode* array[4];
    PCode* p;

    flags = 0;
    isZeroOffset = 0;
    if (instr->op ==
        PC_STW &&
        instr->args[2].kind ==
        PCOp_IMMEDIATE &&
        instr->args[2].data.imm.value == 0) {
        isZeroOffset = 1;
    }

    scan = instr;
    for (i = 0; i < 4; i++) {
        if (scan->op == PC_RLWINM) {
            array[i] = defininginstruction[scan->defID + 1];
        } else {
            array[i] = defininginstruction[scan->defID];
        }

        p = array[i];
        scan = p;
        if (array[0]->args[1].data.reg.reg != p->args[1].data.reg.reg) {
            return 0;
        }

        if (i < 3) {
            if (scan->op != PC_RLWIMI) {
                return 0;
            }
        } else {
            if (scan->op != PC_RLWINM) {
                return 0;
            }
        }

        if (scan->args[2].data.imm.value == 8) {
            if (scan->args[3].data.imm.value == 24 && scan->args[4].data.imm.value == 31) {
                if (flags & 1) {
                    return 0;
                }
                flags |= 1;
            } else if (scan->args[3].data.imm.value == 8 && scan->args[4].data.imm.value == 15) {
                if (flags & 4) {
                    return 0;
                }
                flags |= 4;
            } else {
                return 0;
            }
        } else if (scan->args[2].data.imm.value == 24) {
            if (scan->args[3].data.imm.value == 0 && scan->args[4].data.imm.value == 7) {
                if (flags & 8) {
                    return 0;
                }
                flags |= 8;
            } else if (scan->args[3].data.imm.value == 16 && scan->args[4].data.imm.value == 23) {
                if (flags & 2) {
                    return 0;
                }
                flags |= 2;
            } else {
                return 0;
            }
        } else {
            return 0;
        }
    }

    if (definedbetween(instr, array[3], &array[0]->args[1])) {
        return 0;
    }

    if (instr->op == PC_STWX) {
        change_opcode(instr, PC_STWBRX);
        instr->args[0] = array[0]->args[1];
        defininginstruction[instr->defID] = defininginstruction[array[3]->defID + 1];
        return 1;
    }

    if (instr->op == PC_STW) {
        if (!isZeroOffset) {
            if (masks[RegClass_GPR] & (1 << array[0]->args[0].data.reg.reg)) {
                return 0;
            }

            if (usedbetween(array[2], array[3], &array[0]->args[0])) {
                return 0;
            }
            if (usedbetween(array[1], array[2], &array[0]->args[0])) {
                return 0;
            }
            if (usedbetween(array[0], array[1], &array[0]->args[0])) {
                return 0;
            }
            if (usedbetween(instr, array[0], &array[0]->args[0])) {
                return 0;
            }
        }

        defininginstruction[instr->defID] = defininginstruction[array[3]->defID + 1];

        if (!isZeroOffset) {
            newInstr = makepcode(
                PC_STWBRX, array[3]->args[1].data.reg.reg, 0, instr->args[0].data.reg.reg
            );
            newInstr->alias = instr->alias;
            change_opcode(instr, PC_ADDI);
            insertpcodeafter(instr, newInstr);

            masks[RegClass_GPR] |= 1 << newInstr->args[0].data.reg.reg;
            masks[RegClass_GPR] |= 1 << newInstr->args[2].data.reg.reg;
            instr->args[0].data.reg.effect &= ~EffectRead;
            instr->args[0].data.reg.effect |= EffectWrite;
            defininginstruction[instr->defID] = instr;

            deletepcode(array[0]);
            deletepcode(array[1]);
            deletepcode(array[2]);
            deletepcode(array[3]);
        } else {
            change_opcode(instr, PC_STWBRX);
            instr->args[0] = array[0]->args[1];
            instr->args[2] = instr->args[1];
            defininginstruction[instr->defID + 2] = defininginstruction[instr->defID + 1];

            {
                // TODO: a copy of `instr` (2.6 does not reload it after each store; Pro7's direct
                // uses do)
                PCode* t = instr;
                t->args[1].kind = PCOp_REGISTER;
                t->args[1].arg = RegClass_GPR;
                t->args[1].data.reg.reg = 0;
                t->args[1].data.reg.effect = 0;
            }
        }

        return 1;
    }

    return 0;
}

static int RLWINM_RLWIMI_STH(PCode* instr, UInt32* masks) {
    PCode* newInstr;
    Boolean isZeroOffset;
    PCode* scan;
    int i;
    int flags;
    PCode* array[2];
    PCode* p;

    flags = 0;
    isZeroOffset = 0;
    if (instr->op ==
        PC_STH &&
        instr->args[2].kind ==
        PCOp_IMMEDIATE &&
        instr->args[2].data.imm.value == 0) {
        isZeroOffset = 1;
    }

    scan = instr;
    for (i = 0; i < 2; i++) {
        if (scan->op == PC_RLWINM) {
            array[i] = defininginstruction[scan->defID + 1];
        } else {
            array[i] = defininginstruction[scan->defID];
        }

        p = array[i];
        scan = p;
        if (array[0]->args[1].data.reg.reg != p->args[1].data.reg.reg) {
            return 0;
        }

        if (i < 1) {
            if (scan->op != PC_RLWIMI) {
                return 0;
            }
        } else {
            if (scan->op != PC_RLWINM) {
                return 0;
            }
        }

        if (scan->args[2].data.imm.value == 8) {
            if (scan->args[3].data.imm.value == 16 && scan->args[4].data.imm.value == 23) {
                if (flags & 2) {
                    return 0;
                }
                flags |= 2;
            } else {
                return 0;
            }
        } else if (scan->args[2].data.imm.value == 24) {
            if (scan->args[3].data.imm.value == 24 && scan->args[4].data.imm.value == 31) {
                if (flags & 1) {
                    return 0;
                }
                flags |= 1;
            } else {
                return 0;
            }
        } else {
            return 0;
        }
    }

    if (definedbetween(instr, array[1], &array[0]->args[1])) {
        return 0;
    }

    if (instr->op == PC_STHX) {
        change_opcode(instr, PC_STHBRX);
        instr->args[0] = array[0]->args[1];
        defininginstruction[instr->defID] = defininginstruction[array[1]->defID + 1];
        return 1;
    }

    if (instr->op == PC_STH) {
        if (!isZeroOffset) {
            if (masks[RegClass_GPR] & (1 << array[0]->args[0].data.reg.reg)) {
                return 0;
            }

            if (usedbetween(array[0], array[1], &array[0]->args[0])) {
                return 0;
            }
            if (usedbetween(instr, array[0], &array[0]->args[0])) {
                return 0;
            }
        }

        defininginstruction[instr->defID] = defininginstruction[array[1]->defID + 1];

        if (!isZeroOffset) {
            newInstr = makepcode(
                PC_STHBRX, array[1]->args[1].data.reg.reg, 0, instr->args[0].data.reg.reg
            );
            newInstr->alias = instr->alias;
            change_opcode(instr, PC_ADDI);

            instr->args[0].data.reg.effect &= ~EffectRead;
            instr->args[0].data.reg.effect |= EffectWrite;
            defininginstruction[instr->defID] = instr;

            insertpcodeafter(instr, newInstr);

            masks[RegClass_GPR] |= 1 << newInstr->args[0].data.reg.reg;
            masks[RegClass_GPR] |= 1 << newInstr->args[2].data.reg.reg;

            deletepcode(array[0]);
            deletepcode(array[1]);
        } else {
            change_opcode(instr, PC_STHBRX);
            instr->args[0] = array[0]->args[1];
            instr->args[2] = instr->args[1];
            defininginstruction[instr->defID + 2] = defininginstruction[instr->defID + 1];

            instr->args[1].kind = PCOp_REGISTER;
            instr->args[1].arg = RegClass_GPR;
            instr->args[1].data.reg.reg = 0;
            instr->args[1].data.reg.effect = 0;
        }

        return 1;
    }

    return 0;
}

static void peepholeoptimizeblock(PCodeBlock* block) {
    RegClass rclass;
    PCode* instr;
    PCodeArg* op;
    int i;
    Pattern* pattern;
    UInt32 masks[RegClassMax];

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        masks[rclass] = liveregs[rclass][block->blockIndex].out;
    }

    for (instr = block->lastPCode; instr; instr = instr->prevPCode) {
        if (dead(instr, masks)) {
            ORACLE_PROBE(OracleProbe_PeepholeDead(instr));
            deletepcode(instr);
        } else {
            pattern = peepholepatterns[instr->op];

            while (pattern) {
                ORACLE_PROBE(OracleProbe_PeepholeTry(instr, (void*)pattern->func));
                if (pattern->func(instr, masks)) {
                    ORACLE_PROBE(OracleProbe_PeepholeHit(instr));
                    if (!instr->block) {
                        break;
                    }
                    pattern = peepholepatterns[instr->op];
                } else {
                    pattern = pattern->next;
                }
            }

            if (instr->block) {
                for (op = instr->args, i = instr->argCount; i--; op++) {
                    if (op->kind == PCOp_REGISTER && (op->data.reg.effect & EffectWrite)) {
                        masks[op->arg] &= ~(1 << op->data.reg.reg);
                    }
                }

                for (op = instr->args, i = instr->argCount; i--; op++) {
                    if (op->kind == PCOp_REGISTER && (op->data.reg.effect & EffectRead)) {
                        masks[op->arg] |= 1 << op->data.reg.reg;
                    }
                }
            }
        }
    }
}

static SInt32 computepossiblemask(PCode* instr, short reg) {
    SInt32 mask;
    SInt32 val;
    PCodeArg* op;
    int i;

    mask = 0xFFFFFFFF;
    while (instr) {
        for (op = instr->args, i = instr->argCount; i--; op++) {
            if (PC_OP_IS_WRITE_REGISTER(op, RegClass_GPR, reg)) {
                switch (instr->op) {
                    case PC_LBZ:
                    case PC_LBZU:
                    case PC_LBZX:
                    case PC_LBZUX:
                        mask = 0xFF;
                        break;

                    case PC_LHZ:
                    case PC_LHZU:
                    case PC_LHZX:
                    case PC_LHZUX:
                        mask = 0xFFFF;
                        break;

                    case PC_LI:
                        mask = instr->args[1].data.imm.value;
                        break;

                    case PC_SRAWI:
                        mask = computepossiblemask(instr->prevPCode, instr->args[1].data.reg.reg) >>
                            instr->args[2].data.imm.value;
                        break;

                    case PC_RLWINM:
                        val = computepossiblemask(instr->prevPCode, instr->args[1].data.reg.reg);
                        mask = (val << instr->args[2].data.imm.value) |
                            ((UInt32)val >> (32 - instr->args[2].data.imm.value));

                        if (instr->args[3].data.imm.value <= instr->args[4].data.imm.value) {
                            val = ((instr->args[3].data.imm.value > 31)
                                       ? 0
                                       : (0xFFFFFFFFu >> instr->args[3].data.imm.value)) &
                                ~(((instr->args[4].data.imm.value + 1) > 31)
                                      ? 0
                                      : (0xFFFFFFFFu >> (instr->args[4].data.imm.value + 1)));
                        } else {
                            val = ((instr->args[3].data.imm.value > 31)
                                       ? 0
                                       : (0xFFFFFFFFu >> instr->args[3].data.imm.value)) |
                                ~(((instr->args[4].data.imm.value + 1) > 31)
                                      ? 0
                                      : (0xFFFFFFFFu >> (instr->args[4].data.imm.value + 1)));
                        }

                        mask &= val;
                        break;

                    case PC_RLWIMI: {
                        // TODO: 2.6 spells this case unlike Pro7's (whose text, the same as the
                        // RLWINM case's, gives other registers for the rotation); this spelling
                        // matches, the original's is not known
                        UInt32 value =
                            computepossiblemask(instr->prevPCode, instr->args[1].data.reg.reg);
                        UInt32 afterEnd, fromStart, m;
                        SInt32 rotateCount = instr->args[2].data.imm.value;
                        mask = value >> (32 - rotateCount);
                        mask |= value << rotateCount;
                        if (instr->args[3].data.imm.value <= instr->args[4].data.imm.value) {
                            if (instr->args[4].data.imm.value + 1 > 31) {
                                afterEnd = 0;
                            } else {
                                afterEnd = 0xffffffffu >> (instr->args[4].data.imm.value + 1);
                            }
                            if (instr->args[3].data.imm.value > 31) {
                                fromStart = 0;
                            } else {
                                fromStart = 0xffffffffu >> instr->args[3].data.imm.value;
                            }
                            m = ~afterEnd & fromStart;
                        } else {
                            if (instr->args[4].data.imm.value + 1 > 31) {
                                afterEnd = 0;
                            } else {
                                afterEnd = 0xffffffffu >> (instr->args[4].data.imm.value + 1);
                            }
                            if (instr->args[3].data.imm.value > 31) {
                                fromStart = 0;
                            } else {
                                fromStart = 0xffffffffu >> instr->args[3].data.imm.value;
                            }
                            m = ~afterEnd | fromStart;
                        }
                        mask &= m;
                        mask |= computepossiblemask(instr->prevPCode, instr->args[0].data.reg.reg);
                        break;
                    }

                    case PC_OR:
                        mask = computepossiblemask(instr->prevPCode, instr->args[1].data.reg.reg) |
                            computepossiblemask(instr->prevPCode, instr->args[2].data.reg.reg);
                        break;

                    case PC_ORI:
                        mask = instr->args[2].data.imm.value |
                            computepossiblemask(instr->prevPCode, instr->args[1].data.reg.reg);
                        break;

                    case PC_AND:
                        mask = computepossiblemask(instr->prevPCode, instr->args[1].data.reg.reg) &
                            computepossiblemask(instr->prevPCode, instr->args[2].data.reg.reg);
                        break;

                    case PC_ANDI:
                        mask = instr->args[2].data.imm.value &
                            computepossiblemask(instr->prevPCode, instr->args[1].data.reg.reg);
                        break;

                    case PC_MR:
                        mask = computepossiblemask(instr->prevPCode, instr->args[1].data.reg.reg);
                        break;
                }

                return mask;
            }
        }
        instr = instr->prevPCode;
    }

    return mask;
}

static UInt32 fillmaskholes(UInt32 mask) {
    UInt32 oneBit;
    UInt32 allBits;
    UInt32 result;

    oneBit = 1;
    allBits = 0xFFFFFFFF;
    result = 0;

    if ((mask & 1) && (mask & 0x80000000)) {
        result = 0xFFFFFFFF;
        while ((mask & oneBit) == 1) {
            oneBit <<= 1;
        }

        while ((mask & oneBit) == 0) {
            result &= ~oneBit;
            oneBit <<= 1;
        }

        return result;
    } else {
        while ((mask & oneBit) == 0 && (mask & allBits) != 0) {
            oneBit <<= 1;
            allBits <<= 1;
        }
        while ((mask & allBits) != 0) {
            result |= oneBit;
            oneBit <<= 1;
            allBits <<= 1;
        }
        return result;
    }
}

static int canuseinsert(PCode* instr1, PCode* instr2, short reg) {
    return (computepossiblemask(instr2, reg) &
            fillmaskholes(computepossiblemask(instr1, instr1->args[0].data.reg.reg))) == 0;
}

static PCode* find_def_backwords(PCode* instr, short reg) {
    int i;

    while (instr) {
        for (i = 0; i < instr->argCount; i++) {
            if (PC_OP_IS_WRITE_REGISTER(&instr->args[i], RegClass_GPR, reg)) {
                return instr;
            }
        }
        instr = instr->prevPCode;
    }

    return NULL;
}

static void adjustforward(PCodeBlock* block) {
    PCode* instr;
    PCode* tmp;
    PCode* scan;
    PCodeArg* op;
    PCodeArg* a;
    int i;
    short opcode;
    short reg0;
    int reg1;
    SInt32 valA;
    SInt32 valB;

    instr = block->firstPCode;
    while (instr) {
        if (instr->op == PC_RLWINM) {
            SInt32 val2;
            SInt32 val3;
            SInt32 val4;
            short start;
            short end;

            short flag1 = 0;
            short flag2 = 0;
            reg0 = instr->args[0].data.reg.reg;
            reg1 = instr->args[1].data.reg.reg;
            val2 = instr->args[2].data.imm.value;
            val3 = instr->args[3].data.imm.value;
            val4 = instr->args[4].data.imm.value;

            for (scan = instr->nextPCode; scan; scan = scan->nextPCode) {
                // 2.6 reads the operands its tests look at through one pointer computed at the top
                // of each step (`lea esi, [scan + 0x24]`); the stores go through `scan`
                a = scan->args;
                opcode = scan->op;
                if (opcode == PC_RLWINM && a[1].data.reg.reg == reg0) {
                    if (a[3].data.imm.value ==
                        val3 &&
                        a[4].data.imm.value ==
                        val4 &&
                        a[2].data.imm.value == 0) {
                        if (PCODE_FLAG_SET_F(scan) & fRecordBit) {
                            if (!flag1) {
                                pcsetrecordbit(instr);
                                change_opcode(scan, PC_MR);
                                scan->flags &= ~fRecordBit;
                                scan->flags |= fIsMove;
                                change_num_operands(scan, 2);
                            } else {
                                change_opcode(scan, PC_MR);
                                scan->args[2] = scan->args[5];
                                change_num_operands(scan, 3);
                            }
                        } else {
                            change_opcode(scan, PC_MR);
                            change_num_operands(scan, 2);
                        }
                    } else if (reg0 !=
                               reg1 &&
                               !flag2 &&
                               canmergemasks(
                                   val3,
                                   val4,
                                   a[2].data.imm.value,
                                   a[3].data.imm.value,
                                   a[4].data.imm.value,
                                   &start,
                                   &end
                               )) {
                        scan->args[1].data.reg.reg = reg1;
                        scan->args[2].data.imm.value =
                            (scan->args[2].data.imm.value + instr->args[2].data.imm.value) & 31;
                        scan->args[3].data.imm.value = start;
                        scan->args[4].data.imm.value = end;
                    }
                } else if (opcode ==
                           PC_SRAWI &&
                           a[1].data.reg.reg ==
                           reg0 &&
                           reg0 !=
                           reg1 &&
                           instr->args[2].data.imm.value ==
                           0 &&
                           !(computepossiblemask(instr, reg0) & 0x80000000) &&
                           !flag2 &&
                           canmergemasks(
                               val3,
                               val4,
                               32 - a[2].data.imm.value,
                               a[2].data.imm.value,
                               31,
                               &start,
                               &end
                           ) &&
                           !isSPRlive(scan, 0)) {
                    insertpcodeafter(
                        scan,
                        makepcode(
                            PC_RLWINM, a[0].data.reg.reg, reg1, 32 - a[2].data.imm.value, start, end
                        )
                    );
                    if (PCODE_FLAG_SET_F(scan) & fRecordBit) {
                        pcsetrecordbit(scan->nextPCode);
                    }
                    deletepcode(scan);
                } else if (opcode ==
                           PC_OR &&
                           !flag2 &&
                           reg0 !=
                           reg1 &&
                           !(PCODE_FLAG_SET_F(scan) & fRecordBit) &&
                           !(PCODE_FLAG_SET_F(instr) & fRecordBit) &&
                           scan->args[0].data.reg.reg != instr->args[1].data.reg.reg) {
                    if (a[1].data.reg.reg == reg0 && canuseinsert(instr, scan, a[2].data.reg.reg)) {
                        op = &scan->args[2];
                        tmp = find_def_backwords(scan->prevPCode, op->data.reg.reg);
                        if (tmp->op == PC_RLWINM && tmp->args[2].data.imm.value == 0) {
                            if (instr->args[3].data.imm.value <= instr->args[4].data.imm.value) {
                                valA = ((instr->args[3].data.imm.value > 31)
                                            ? 0
                                            : (0xFFFFFFFFu >> instr->args[3].data.imm.value)) &
                                    ~(((instr->args[4].data.imm.value + 1) > 31)
                                          ? 0
                                          : (0xFFFFFFFFu >> (instr->args[4].data.imm.value + 1)));
                            } else {
                                valA = ((instr->args[3].data.imm.value > 31)
                                            ? 0
                                            : (0xFFFFFFFFu >> instr->args[3].data.imm.value)) |
                                    ~(((instr->args[4].data.imm.value + 1) > 31)
                                          ? 0
                                          : (0xFFFFFFFFu >> (instr->args[4].data.imm.value + 1)));
                            }

                            if (tmp->args[3].data.imm.value <= tmp->args[4].data.imm.value) {
                                valB = ((tmp->args[3].data.imm.value > 31)
                                            ? 0
                                            : (0xFFFFFFFFu >> tmp->args[3].data.imm.value)) &
                                    ~(((tmp->args[4].data.imm.value + 1) > 31)
                                          ? 0
                                          : (0xFFFFFFFFu >> (tmp->args[4].data.imm.value + 1)));
                            } else {
                                valB = ((tmp->args[3].data.imm.value > 31)
                                            ? 0
                                            : (0xFFFFFFFFu >> tmp->args[3].data.imm.value)) |
                                    ~(((tmp->args[4].data.imm.value + 1) > 31)
                                          ? 0
                                          : (0xFFFFFFFFu >> (tmp->args[4].data.imm.value + 1)));
                            }

                            if (valA == ~valB) {
                                op = &tmp->args[1];
                            }
                        }

                        change_opcode(scan, PC_MR);
                        scan->args[1] = *op;
                        change_num_operands(scan, 2);

                        tmp = copypcode(instr);
                        change_opcode(tmp, PC_RLWIMI);
                        tmp->args[0] = scan->args[0];
                        tmp->args[0].data.reg.effect |= EffectRead;

                        if (ismaskconstant(
                                fillmaskholes(
                                    computepossiblemask(instr, instr->args[0].data.reg.reg)
                                ),
                                &start,
                                &end
                            )) {
                            tmp->args[3].data.imm.value = start;
                            tmp->args[4].data.imm.value = end;
                        }

                        insertpcodeafter(scan, tmp);
                        break;
                    }

                    if (a[2].data.reg.reg == reg0 && canuseinsert(instr, scan, a[1].data.reg.reg)) {
                        op = &scan->args[1];
                        tmp = find_def_backwords(scan->prevPCode, op->data.reg.reg);
                        if (tmp->op == PC_RLWINM && tmp->args[2].data.imm.value == 0) {
                            if (instr->args[3].data.imm.value <= instr->args[4].data.imm.value) {
                                valA = ((instr->args[3].data.imm.value > 31)
                                            ? 0
                                            : (0xFFFFFFFFu >> instr->args[3].data.imm.value)) &
                                    ~(((instr->args[4].data.imm.value + 1) > 31)
                                          ? 0
                                          : (0xFFFFFFFFu >> (instr->args[4].data.imm.value + 1)));
                            } else {
                                valA = ((instr->args[3].data.imm.value > 31)
                                            ? 0
                                            : (0xFFFFFFFFu >> instr->args[3].data.imm.value)) |
                                    ~(((instr->args[4].data.imm.value + 1) > 31)
                                          ? 0
                                          : (0xFFFFFFFFu >> (instr->args[4].data.imm.value + 1)));
                            }

                            if (tmp->args[3].data.imm.value <= tmp->args[4].data.imm.value) {
                                valB = ((tmp->args[3].data.imm.value > 31)
                                            ? 0
                                            : (0xFFFFFFFFu >> tmp->args[3].data.imm.value)) &
                                    ~(((tmp->args[4].data.imm.value + 1) > 31)
                                          ? 0
                                          : (0xFFFFFFFFu >> (tmp->args[4].data.imm.value + 1)));
                            } else {
                                valB = ((tmp->args[3].data.imm.value > 31)
                                            ? 0
                                            : (0xFFFFFFFFu >> tmp->args[3].data.imm.value)) |
                                    ~(((tmp->args[4].data.imm.value + 1) > 31)
                                          ? 0
                                          : (0xFFFFFFFFu >> (tmp->args[4].data.imm.value + 1)));
                            }

                            if (valA == ~valB) {
                                op = &tmp->args[1];
                            }
                        }

                        change_opcode(scan, PC_MR);
                        scan->args[1] = *op;
                        change_num_operands(scan, 2);

                        tmp = copypcode(instr);
                        change_opcode(tmp, PC_RLWIMI);
                        tmp->args[0] = scan->args[0];
                        tmp->args[0].data.reg.effect |= EffectRead;

                        if (ismaskconstant(
                                fillmaskholes(
                                    computepossiblemask(instr, instr->args[0].data.reg.reg)
                                ),
                                &start,
                                &end
                            )) {
                            tmp->args[3].data.imm.value = start;
                            tmp->args[4].data.imm.value = end;
                        }

                        insertpcodeafter(scan, tmp);
                        break;
                    }
                } else if (!flag2 &&
                           reg0 !=
                           reg1 &&
                           val4 ==
                           31 &&
                           instr->args[2].data.imm.value ==
                           0 &&
                           (((opcode == PC_STB || opcode == PC_STBX) && val3 <= 24) ||
                            ((opcode == PC_STH || opcode == PC_STHX) && val3 <= 16)) &&
                           scan->args[0].data.reg.reg == reg0) {
                    scan->args[0].data.reg.reg = reg1;
                } else if (opcode ==
                           PC_EXTSH &&
                           a[1].data.reg.reg ==
                           reg0 &&
                           val2 ==
                           0 &&
                           val3 >
                           16 &&
                           val3 < val4) {
                    change_opcode(scan, PC_MR);
                    if ((PCODE_FLAG_SET_F(scan) & fRecordBit) && !flag1) {
                        pcsetrecordbit(instr);
                        scan->flags &= ~fRecordBit;
                        scan->flags |= fIsMove;
                        change_num_operands(scan, 2);
                    }
                } else if (opcode ==
                           PC_EXTSB &&
                           a[1].data.reg.reg ==
                           reg0 &&
                           val2 ==
                           0 &&
                           val3 >
                           24 &&
                           val3 < val4) {
                    change_opcode(scan, PC_MR);
                    if ((PCODE_FLAG_SET_F(scan) & fRecordBit) && !flag1) {
                        pcsetrecordbit(instr);
                        scan->flags &= ~fRecordBit;
                        scan->flags |= fIsMove;
                        change_num_operands(scan, 2);
                    }
                }

                for (op = scan->args, i = scan->argCount; i--; op++) {
                    if (PC_OP_IS_WRITE_REGISTER(op, RegClass_GPR, reg0)) {
                        scan = block->lastPCode;
                        break;
                    }

                    if (PC_OP_IS_WRITE_REGISTER(op, RegClass_GPR, reg1)) {
                        flag2 = 1;
                    }

                    if (PC_OP_IS_REGISTER(op, RegClass_CRFIELD, 0)) {
                        flag1 = 1;
                    }
                }
            }

        } else if (instr->op ==
                   PC_EXTSB &&
                   instr->args[0].data.reg.reg != instr->args[1].data.reg.reg) {
            short flag = 0;
            reg0 = instr->args[0].data.reg.reg;
            reg1 = instr->args[1].data.reg.reg;
            for (scan = instr->nextPCode; scan; scan = scan->nextPCode) {
                if ((scan->op >= PC_STB && scan->op <= PC_STBUX) &&
                    scan->args[0].data.reg.reg == reg0) {
                    scan->args[0].data.reg.reg = reg1;
                } else if ((scan->op == PC_EXTSH || scan->op == PC_EXTSB) &&
                           scan->args[1].data.reg.reg == reg0) {
                    change_opcode(scan, PC_MR);
                    if ((PCODE_FLAG_SET_F(scan) & fRecordBit) && !flag) {
                        pcsetrecordbit(instr);
                        scan->flags &= ~fRecordBit;
                        scan->flags |= fIsMove;
                        change_num_operands(scan, 2);
                    }
                }

                for (op = scan->args, i = scan->argCount; i--; op++) {
                    if (PC_OP_IS_WRITE_ANY_REGISTER(op, RegClass_GPR) &&
                        (op->data.reg.reg == reg0 || op->data.reg.reg == reg1)) {
                        scan = block->lastPCode;
                        break;
                    }

                    if (PC_OP_IS_REGISTER(op, RegClass_CRFIELD, 0)) {
                        flag = 1;
                    }
                }
            }
        } else if (instr->op ==
                   PC_EXTSH &&
                   instr->args[0].data.reg.reg != instr->args[1].data.reg.reg) {
            short flag = 0;
            reg0 = instr->args[0].data.reg.reg;
            reg1 = instr->args[1].data.reg.reg;
            for (scan = instr->nextPCode; scan; scan = scan->nextPCode) {
                if (((scan->op >= PC_STB && scan->op <= PC_STBUX) ||
                     (scan->op >= PC_STH && scan->op <= PC_STHUX)) &&
                    scan->args[0].data.reg.reg == reg0) {
                    scan->args[0].data.reg.reg = reg1;
                } else if (scan->op == PC_EXTSH && scan->args[1].data.reg.reg == reg0) {
                    change_opcode(scan, PC_MR);
                    if ((PCODE_FLAG_SET_F(scan) & fRecordBit) && !flag) {
                        pcsetrecordbit(instr);
                        scan->flags &= ~fRecordBit;
                        scan->flags |= fIsMove;
                        change_num_operands(scan, 2);
                    }
                }

                for (op = scan->args, i = scan->argCount; i--; op++) {
                    if (PC_OP_IS_WRITE_ANY_REGISTER(op, RegClass_GPR) &&
                        (op->data.reg.reg == reg0 || op->data.reg.reg == reg1)) {
                        scan = block->lastPCode;
                        break;
                    }

                    if (PC_OP_IS_REGISTER(op, RegClass_CRFIELD, 0)) {
                        flag = 1;
                    }
                }
            }
        } else if (instr->op ==
                   PC_ADDI &&
                   (reg0 = instr->args[0].data.reg.reg) ==
                   instr->args[1].data.reg.reg &&
                   instr->args[2].kind == PCOp_IMMEDIATE) {
            Boolean flag1 = 0;
            Boolean flag2 = 0;
            SInt32 val2 = instr->args[2].data.imm.value;

            for (scan = instr->nextPCode; scan; scan = scan->nextPCode) {
                if ((scan->flags & fIsWrite) && scan->args[0].data.reg.reg == reg0) {
                    break;
                }

                if ((scan->flags & (fIsRead | fIsWrite)) &&
                    scan->args[1].data.reg.reg ==
                    reg0 &&
                    scan->args[2].kind ==
                    PCOp_IMMEDIATE &&
                    FITS_IN_SHORT(val2 + scan->args[2].data.imm.value)) {
                    scan->args[2].data.imm.value += val2;
                    tmp = instr->prevPCode;
                    if ((scan->flags & fIsRead) &&
                        scan->args[0].data.reg.reg ==
                        reg0 &&
                        scan->args[0].kind ==
                        PCOp_REGISTER &&
                        scan->args[0].arg == RegClass_GPR) {
                        deletepcode(instr);
                    } else {
                        deletepcode(instr);
                        insertpcodeafter(scan, instr);
                    }
                    instr = tmp;
                    break;
                }

                if (scan->op ==
                    PC_ADDI &&
                    scan->args[1].data.reg.reg ==
                    reg0 &&
                    scan->args[2].kind ==
                    PCOp_IMMEDIATE &&
                    FITS_IN_SHORT(val2 + scan->args[2].data.imm.value)) {
                    scan->args[2].data.imm.value += val2;
                    tmp = instr->prevPCode;
                    if (scan->args[0].data.reg.reg == reg0) {
                        deletepcode(instr);
                    } else {
                        deletepcode(instr);
                        insertpcodeafter(scan, instr);
                    }
                    instr = tmp;
                    break;
                }

                if (scan->flags & (fIsBranch | fIsCall)) {
                    if (flag1 && scan->prevPCode != instr) {
                        tmp = instr->prevPCode;
                        deletepcode(instr);
                        insertpcodebefore(scan, instr);
                        instr = tmp;
                    }
                    break;
                }

                for (op = scan->args, i = scan->argCount; i--; op++) {
                    if (PC_OP_IS_R_OR_W_REGISTER(op, RegClass_GPR, reg0)) {
                        if (flag1 && scan->prevPCode != instr) {
                            tmp = instr->prevPCode;
                            deletepcode(instr);
                            insertpcodebefore(scan, instr);
                            instr = tmp;
                        }
                        flag2 = 1;
                        break;
                    }
                }

                if (flag2) {
                    break;
                }

                if (scan->op != PC_ADDI) {
                    flag1 = 1;
                }

                if (flag1 && !scan->nextPCode) {
                    tmp = instr->prevPCode;
                    deletepcode(instr);
                    appendpcode(block, instr);
                    instr = tmp;
                    break;
                }
            }
        }

        if (instr) {
            instr = instr->nextPCode;
        } else {
            instr = block->firstPCode;
        }
    }
}

static void installpattern(short opcode, PeepholeFunc func) {
    Pattern* pattern = lalloc(sizeof(Pattern));
    pattern->func = func;
    pattern->next = peepholepatterns[opcode];
    peepholepatterns[opcode] = pattern;
}

#ifdef ORACLE_PROBES
// (the oracle names the patterns it reports)
#define installpattern(opcode, func) (OracleProbe_PeepholePattern((void*)func, #func), installpattern(opcode, func))
#endif

static void installpeepholepatterns(void) {
    int i;

    for (i = 0; i < OPCODE_MAX; i++) {
        peepholepatterns[i] = NULL;
    }

    installpattern(PC_AND, NOT_AND);
    installpattern(PC_MR, LI_MR);
    installpattern(PC_MR, L_MR);
    installpattern(PC_FMR, L_FMR);
    installpattern(PC_MR, MR_MR);
    installpattern(PC_MR, MR_Rx_Rx);
    installpattern(PC_FMR, FMR_FMR);
    installpattern(PC_FMR, FMR_Fx_Fx);
    installpattern(PC_VMR, VMR_VMRP);
    installpattern(PC_VMR, VMR_VMR);
    installpattern(PC_VMR, VMR_Vx_Vx);
    installpattern(PC_VMR, VSPLTIS_VMR);
    installpattern(PC_CMPI, MR_CMPI);
    installpattern(PC_RLWIMI, RLWINM_RLWINM);
    installpattern(PC_RLWINM, RLWINM_RLWINM);
    installpattern(PC_RLWINM, EXTSB_RLWINM);
    installpattern(PC_RLWINM, EXTSH_RLWINM);
    installpattern(PC_RLWINM, LBZ_RLWINM);
    installpattern(PC_RLWINM, LHZ_RLWINM);
    installpattern(PC_EXTSH, LHA_EXTSH);
    installpattern(PC_STW, RLWINM_RLWIMI_STW);
    installpattern(PC_STWX, RLWINM_RLWIMI_STW);
    installpattern(PC_STH, RLWINM_RLWIMI_STH);
    installpattern(PC_STHX, RLWINM_RLWIMI_STH);
    installpattern(PC_LBZ, LI_L_S);
    installpattern(PC_LHZ, LI_L_S);
    installpattern(PC_LHA, LI_L_S);
    installpattern(PC_LWZ, LI_L_S);
    installpattern(PC_STB, LI_L_S);
    installpattern(PC_STH, LI_L_S);
    installpattern(PC_STW, LI_L_S);
    installpattern(PC_LFS, LI_L_S);
    installpattern(PC_LFD, LI_L_S);
    installpattern(PC_STFS, LI_L_S);
    installpattern(PC_STFD, LI_L_S);
    installpattern(PC_LBZ, ADDI_L_S);
    installpattern(PC_LHZ, ADDI_L_S);
    installpattern(PC_LHA, ADDI_L_S);
    installpattern(PC_LWZ, ADDI_L_S);
    installpattern(PC_STB, ADDI_L_S);
    installpattern(PC_STH, ADDI_L_S);
    installpattern(PC_STW, ADDI_L_S);
    installpattern(PC_LFS, ADDI_L_S);
    installpattern(PC_LFD, ADDI_L_S);
    installpattern(PC_STFS, ADDI_L_S);
    installpattern(PC_STFD, ADDI_L_S);
    installpattern(PC_LBZU, ADDI_LU_SU);
    installpattern(PC_LHZU, ADDI_LU_SU);
    installpattern(PC_LHAU, ADDI_LU_SU);
    installpattern(PC_LWZU, ADDI_LU_SU);
    installpattern(PC_STBU, ADDI_LU_SU);
    installpattern(PC_STHU, ADDI_LU_SU);
    installpattern(PC_STWU, ADDI_LU_SU);
    installpattern(PC_LFSU, ADDI_LU_SU);
    installpattern(PC_LFDU, ADDI_LU_SU);
    installpattern(PC_STFSU, ADDI_LU_SU);
    installpattern(PC_STFDU, ADDI_LU_SU);
    installpattern(PC_LBZ, L_S_ADDI);
    installpattern(PC_LHZ, L_S_ADDI);
    installpattern(PC_LHA, L_S_ADDI);
    installpattern(PC_LWZ, L_S_ADDI);
    installpattern(PC_STB, L_S_ADDI);
    installpattern(PC_STH, L_S_ADDI);
    installpattern(PC_STW, L_S_ADDI);
    installpattern(PC_LFS, L_S_ADDI);
    installpattern(PC_LFD, L_S_ADDI);
    installpattern(PC_STFS, L_S_ADDI);
    installpattern(PC_STFD, L_S_ADDI);
    installpattern(PC_STB, L_S);
    installpattern(PC_STH, L_S);
    installpattern(PC_STW, L_S);
    installpattern(PC_STFS, L_S);
    installpattern(PC_STFD, L_S);
    installpattern(PC_STBX, L_S);
    installpattern(PC_STHX, L_S);
    installpattern(PC_STWX, L_S);
    installpattern(PC_STFSX, L_S);
    installpattern(PC_STFDX, L_S);
    installpattern(PC_BT, LBZ_EXTSB_CMPI_BC);
    installpattern(PC_BF, LBZ_EXTSB_CMPI_BC);
    installpattern(PC_BT, RLWINM_CMPLI_BC);
    installpattern(PC_BF, RLWINM_CMPLI_BC);
    installpattern(PC_BT, LI_CMP_BC);
    installpattern(PC_BF, LI_CMP_BC);
    installpattern(PC_RLWINM, RLWINM_RLWINM);
    installpattern(PC_MULLI, MULLI_MULLI);
    installpattern(PC_ADDI, ADDI_ADDI);
    installpattern(PC_SRAWI, SRAWI_SRAWI);
    installpattern(PC_MR, MR_ADDI);
    installpattern(PC_OR, SRW_SUBFIC_RLW_OR);
}

void peepholeoptimizeforward(Object* func) {
    PCodeBlock* block;

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        if (block->pcodeCount >= 2) {
            adjustforward(block);
        }
    }
}

void peepholemergeblocks(Object* func, Boolean flag) {
    PCodeBlock* block;
    PCodeBlock* next;
    Boolean flag2;
    PCode* instr;
    PCode* nextinstr;
    PCLink* link;
    PCLink* link2;

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        flag2 = 0;
        next = block->nextBlock;

        if (!flag) {
            flag2 = next && (next->flags & fIsEpilogue);
            if (block->flags & fIsProlog) {
                continue;
            }
        }

        if (block->pcodeCount > 0) {
            for (instr = block->firstPCode; instr; instr = instr->nextPCode) {
                if (instr->flags & (fIsCall | fSideEffects)) {
                    break;
                }
            }

            if (instr) {
                continue;
            }

            if (block->lastPCode && block->lastPCode->op == PC_B) {
                if (block->lastPCode->args[0].kind ==
                    PCOp_LABEL &&
                    block->lastPCode->args[0].data.label.label->block == next) {
                    deletepcode(block->lastPCode);
                } else {
                    continue;
                }
            }

            instr = block->lastPCode;
            if (instr && (instr->flags & fIsBranch) && instr->op != PC_B) {
                continue;
            }

            while (block->successors->block ==
                   next &&
                   block->successors->nextLink ==
                   NULL &&
                   next->predecessors->block ==
                   block &&
                   next->predecessors->nextLink ==
                   NULL &&
                   !flag2 &&
                   !(next->flags & fEntryPoint) &&
                   (block->pcodeCount + next->pcodeCount) <= 100) {
                if (next->pcodeCount > 0) {
                    for (instr = next->firstPCode; instr; instr = nextinstr) {
                        nextinstr = instr->nextPCode;
                        if (instr->flags & (fIsCall | fSideEffects)) {
                            break;
                        }

                        deletepcode(instr);
                        if (instr->op != PC_B) {
                            appendpcode(block, instr);
                        } else if (instr->args[0].kind == PCOp_LABEL) {
                            if (instr->args[0].data.label.label->block != next->nextBlock) {
                                appendpcode(block, instr);
                            }
                        } else {
                            appendpcode(block, instr);
                        }
                    }
                }

                if (next->pcodeCount != 0) {
                    break;
                }
                if (next == epilogue) {
                    break;
                }

                block->successors = next->successors;

                for (link = block->successors; link; link = link->nextLink) {
                    for (link2 = link->block->predecessors; link2; link2 = link2->nextLink) {
                        if (link2->block == next) {
                            link2->block = block;
                            break;
                        }
                    }
                }

                block->nextBlock = next->nextBlock;
                if (block->nextBlock) {
                    block->nextBlock->prevBlock = block;
                }

                next->flags |= fDeleted;
                next = block->nextBlock;

                if (!flag) {
                    flag2 = next && (next->flags & fIsEpilogue);
                }
            }
        }
    }
}

void peepholeoptimizepcode(Object* func) {
    PCodeBlock* block;

    installpeepholepatterns();
    computeliveregisters(func);

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        if (block->pcodeCount >= 1) {
            computeinstructionpredecessors(block);
            peepholeoptimizeblock(block);
            freeoheap();
        }
    }
}
