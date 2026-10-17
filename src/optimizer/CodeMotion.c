// CodeMotion.c: loop-invariant code motion and simple loop unswitching (Pro7's CodeMotion.c,
// deferred). 2.6 asks the use-def chains about memory through may_alias_object/must_alias_object
// (Pro7: may_alias/uniquely_aliases on two PCodes), and keeps r13 (the small-data base) loop
// invariant like r2.

#include "compiler/CodeMotion.h"
#include "compiler/Alias.h"
#include "compiler/BitVectors.h"
#include "compiler/CError.h"
#include "compiler/CompilerTools.h"
#include "compiler/LoopDetection.h"
#include "compiler/LoopOptimization.h"
#include "compiler/PCode.h"
#include "compiler/PCodeUtilities.h"
#include "compiler/RegisterInfo.h"
#include "compiler/UseDefChains.h"

int movedloopinvariantcode;
int unswitchedinvariantcode;

static int isloopinvariant(PCode* pcode, Loop* loop, UInt32* vec, int flag1, int flag2) {
    PCodeArg* op;
    RegUseOrDef* list;
    int i;

    if (pcode->flags & (fIsRead | fIsWrite | fOpFromMemory | fOpToMemory)) {
        if (pcode->alias) {
            if (pcode->alias->type ==
                AliasType_Set ||
                (pcode->flags & (fIsVolatile | fSideEffects))) {
                return FALSE;
            }

            if (pcode->flags & fIsRead) {
                for (list = findobjectusedef(pcode->alias->object)->defs; list; list = list->next) {
#if VERSION < VERSION_GC_2_5
                    if (may_alias(pcode, Defs[list->id].pcode) &&
#else
                    if (may_alias_object(pcode, Defs[list->id].v.u.object) &&
#endif
                        bitvectorgetbit(list->id, vec) &&
                        bitvectorgetbit(
                            Defs[list->id].pcode->block->blockIndex, loop->memberblocks
                        )) {
                        return FALSE;
                    }
                }
            }

            if (pcode->flags & fIsWrite) {
                for (list = findobjectusedef(pcode->alias->object)->uses; list; list = list->next) {
#if VERSION < VERSION_GC_2_5
                    if (may_alias(pcode, Uses[list->id].pcode) &&
#else
                    if (may_alias_object(pcode, Uses[list->id].v.u.object) &&
#endif
                        bitvectorgetbit(
                            Uses[list->id].pcode->block->blockIndex, loop->memberblocks
                        )) {
                        return FALSE;
                    }
                }
            }
        } else {
            return FALSE;
        }
    }

    if ((pcode->flags & fIsWrite) &&
        !bitvectorgetbit(pcode->block->blockIndex, loop->backedge_dominators)) {
        return FALSE;
    }

    op = pcode->args;
    i = pcode->argCount;
    while (i--) {
        switch (op->kind) {
            case PCOp_MEMORY:
                // (the original's precedence bug, as Pro7's: `!flags & fIsConst` is always 0, so this
                // loop never runs; 2.6 still computes it. Pro7's `(flags == 0) & 0x40` spelling: 96.6%)
                if ((pcode->flags & fIsRead) && (!pcode->flags & fIsConst)) {
                    for (list = findobjectusedef(op->data.mem.obj)->defs; list; list = list->next) {
#if VERSION < VERSION_GC_2_5
                        if (may_alias(pcode, Defs[list->id].pcode) &&
#else
                        if (may_alias_object(pcode, Defs[list->id].v.u.object) &&
#endif
                            bitvectorgetbit(list->id, vec) &&
                            bitvectorgetbit(
                                Defs[list->id].pcode->block->blockIndex, loop->memberblocks
                            )) {
                            return FALSE;
                        }
                    }
                }
                if (pcode->flags & fIsWrite) {
                    for (list = findobjectusedef(op->data.mem.obj)->uses; list; list = list->next) {
#if VERSION < VERSION_GC_2_5
                        if (may_alias(pcode, Uses[list->id].pcode) &&
#else
                        if (may_alias_object(pcode, Uses[list->id].v.u.object) &&
#endif
                            bitvectorgetbit(
                                Uses[list->id].pcode->block->blockIndex, loop->memberblocks
                            )) {
                            return FALSE;
                        }
                    }
                }
                break;
            case PCOp_REGISTER:
                if (op->data.reg.effect & (EffectRead | EffectWrite)) {
                    if (op->kind == PCOp_REGISTER && op->arg == RegClass_GPR) {
                        if (op->data.reg.reg == _FP_) {
                            break;
                        }
                        if (op->data.reg.reg == _CALLER_SP_) {
                            break;
                        }
                        if ((int)op->data.reg.reg == 2) {
                            break;
                        }
                        if ((int)op->data.reg.reg == 13) {
                            break;
                        }
                    }
                    if (op->data.reg.reg < n_real_registers[op->arg]) {
                        if (op->arg == RegClass_CRFIELD) {
                            if (!flag2 || (op->data.reg.effect & EffectRead)) {
                                return FALSE;
                            }
                        } else if (op->arg == RegClass_SPR) {
                            if (!flag1) {
                                return FALSE;
                            }
                        } else {
                            return FALSE;
                        }
                    } else if (op->data.reg.effect & EffectRead) {
                        if (flag1 && op->kind == PCOp_REGISTER && op->arg == RegClass_SPR) {
                            break;
                        }
                        if (op->kind == PCOp_REGISTER && op->arg == RegClass_GPR) {
                            if (op->data.reg.reg == _FP_) {
                                break;
                            }
                            if (op->data.reg.reg == _CALLER_SP_) {
                                break;
                            }
                            if ((int)op->data.reg.reg == 2) {
                                break;
                            }
                            if ((int)op->data.reg.reg == 13) {
                                break;
                            }
                        }

                        for (list = reg_Defs[op->arg][op->data.reg.reg]; list; list = list->next) {
                            if (bitvectorgetbit(list->id, vec) &&
                                bitvectorgetbit(
                                    Defs[list->id].pcode->block->blockIndex, loop->memberblocks
                                )) {
                                return FALSE;
                            }
                        }
                    }
                }
                break;
        }
        op++;
    }

    return TRUE;
}

static int isuniquedefinition(PCode* pcode, Loop* loop) {
    RegUseOrDef* list;
    int defID;
    UseOrDef* def;
    TinyValue* v;

    defID = pcode->defID;
    def = &Defs[defID];
    v = &def->v;
    if (defID >= number_of_Defs) {
        return FALSE;
    }
    if (def->pcode != pcode) {
        return FALSE;
    }
    if ((defID + 1) < number_of_Defs && def[1].pcode == pcode) {
        return FALSE;
    }

    if (def->v.kind == PCOp_REGISTER) {
        for (list = reg_Defs[v->arg][v->u.reg]; list; list = list->next) {
            if (bitvectorgetbit(Defs[list->id].pcode->block->blockIndex, loop->memberblocks) &&
                list->id != defID) {
                return FALSE;
            }
        }
    } else if (def->v.kind == PCOp_MEMORY) {
        for (list = findobjectusedef(v->u.object)->defs; list; list = list->next) {
#if VERSION < VERSION_GC_2_5
            if (may_alias(pcode, Defs[list->id].pcode) &&
#else
            if (may_alias_object(pcode, Defs[list->id].v.u.object) &&
#endif
                bitvectorgetbit(Defs[list->id].pcode->block->blockIndex, loop->memberblocks) &&
                list->id != defID) {
                return FALSE;
            }
        }
    } else {
        CError_FATAL(292);
    }

    return TRUE;
}

static int uniquelyreachesuse(int defID, int useID) {
    UseOrDef* def;
    TinyValue* v;
    UseOrDef* use;
    RegUseOrDef* list;
    PCode* pcode;

    def = &Defs[defID];
    use = &Uses[useID];
    v = &def->v;
    if (def->v.kind == PCOp_REGISTER) {
        for (list = reg_Defs[v->arg][v->u.reg]; list; list = list->next) {
            if (list->id !=
                defID &&
                bitvectorgetbit(list->id, usedefinfo[use->pcode->block->blockIndex].defin)) {
                break;
            }
        }
    } else if (def->v.kind == PCOp_MEMORY) {
        for (list = findobjectusedef(v->u.object)->defs; list; list = list->next) {
#if VERSION < VERSION_GC_2_5
            if (may_alias(def->pcode, Defs[list->id].pcode) &&
#else
            if (may_alias_object(def->pcode, Defs[list->id].v.u.object) &&
#endif
                list->id !=
                defID &&
                bitvectorgetbit(list->id, usedefinfo[use->pcode->block->blockIndex].defin)) {
                break;
            }
        }
    }

    if (!list) {
        return TRUE;
    }

    if (def->pcode->block == use->pcode->block) {
        for (pcode = use->pcode->prevPCode; pcode; pcode = pcode->prevPCode) {
            if (pcode == def->pcode) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

static int uniquelyreachesalluses(int defID, Loop* loop) {
    UseOrDef* def;
    TinyValue* v;
    RegUseOrDef* list;

    def = &Defs[defID];
    v = &def->v;

    if (def->v.kind == PCOp_REGISTER) {
        for (list = reg_Uses[v->arg][v->u.reg]; list; list = list->next) {
            if (bitvectorgetbit(list->id, usedefinfo[loop->preheader->blockIndex].useout) ||
                (bitvectorgetbit(Uses[list->id].pcode->block->blockIndex, loop->memberblocks) &&
                 !uniquelyreachesuse(defID, list->id))) {
                return FALSE;
            }
        }
    } else if (def->v.kind == PCOp_MEMORY) {
        for (list = findobjectusedef(v->u.object)->uses; list; list = list->next) {
#if VERSION < VERSION_GC_2_5
            if (may_alias(def->pcode, Uses[list->id].pcode)) {
#else
            if (may_alias_object(def->pcode, Uses[list->id].v.u.object)) {
#endif
                if (bitvectorgetbit(list->id, usedefinfo[loop->preheader->blockIndex].useout) ||
                    (bitvectorgetbit(Uses[list->id].pcode->block->blockIndex, loop->memberblocks) &&
                     !uniquelyreachesuse(defID, list->id))) {
                    return FALSE;
                }
            }
        }
    } else {
        CError_FATAL(382);
    }

    return TRUE;
}

static int isliveonexit(TinyValue* v, Loop* loop) {
    RegUseOrDef* list;
    UInt32* vec;

    vec = usedefinfo[loop->preheader->blockIndex].useout;

    if (v->kind == PCOp_REGISTER) {
        for (list = reg_Uses[v->arg][v->u.reg]; list; list = list->next) {
            if (bitvectorgetbit(list->id, vec) &&
                !bitvectorgetbit(Uses[list->id].pcode->block->blockIndex, loop->memberblocks)) {
                return TRUE;
            }
        }
    } else if (v->kind == PCOp_MEMORY) {
        for (list = findobjectusedef(v->u.object)->uses; list; list = list->next) {
            if (bitvectorgetbit(list->id, vec) &&
                !bitvectorgetbit(Uses[list->id].pcode->block->blockIndex, loop->memberblocks)) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

static int dominatesallexits(PCode* pcode, Loop* loop) {
    return bitvectorgetbit(pcode->block->blockIndex, loop->exit_dominators) != 0;
}

static int maymove(PCode* pcode, Loop* loop) {
    int reg;

    if (!isuniquedefinition(pcode, loop)) {
        return FALSE;
    }
    if (!uniquelyreachesalluses(pcode->defID, loop)) {
        return FALSE;
    }
    if (!dominatesallexits(pcode, loop) && isliveonexit(&Defs[pcode->defID].v, loop)) {
        return FALSE;
    }

    if (loop->bodySize > 25) {
        switch (pcode->op) {
            case PC_LI:
                if (pcode->nextPCode &&
                    pcode->nextPCode->op ==
                    PC_LVX &&
                    (pcode->nextPCode->flags & fIsConst)) {
                    reg = pcode->args[0].data.reg.reg;
                    if (pcode->nextPCode->args[1].data.reg.reg ==
                        reg ||
                        pcode->nextPCode->args[2].data.reg.reg == reg) {
                        return TRUE;
                    }
                }
#if VERSION == VERSION_GC_2_0P1
            // 2.0p1 (2.0 patched for BFBB): floating-point loads stay in a large loop, and the AltiVec
            // splats move like any other instruction (2.0p2 keeps 2.0's choice, as BFBB's code does)
            case PC_LFS:
            case PC_LFSU:
            case PC_LFSX:
            case PC_LFSUX:
            case PC_LFD:
            case PC_LFDU:
            case PC_LFDX:
            case PC_LFDUX:
#else
            case PC_VSPLTISB:
            case PC_VSPLTISH:
            case PC_VSPLTISW:
#endif
                return FALSE;
            default:
                if (!bitvectorgetbit(pcode->block->blockIndex, loop->backedge_dominators)) {
                    return FALSE;
                }
        }
    }

    return TRUE;
}

static void moveinvariantcomputation(PCode* pcode, Loop* loop) {
    ObjectUseDef* oud;
    RegUseOrDef* list;
    BlockList* blocklist;
    UseOrDef* def;
    TinyValue* v;
    int defID;

    defID = pcode->defID;
    def = &Defs[defID];
    v = &def->v;
    deletepcode(pcode);
    insertpcodebefore(loop->preheader->lastPCode, pcode);
    loop->bodySize--;
    movedloopinvariantcode = TRUE;

    if (def->v.kind == PCOp_REGISTER) {
        for (blocklist = loop->blocks; blocklist; blocklist = blocklist->next) {
            for (list = reg_Defs[v->arg][v->u.reg]; list; list = list->next) {
                bitvectorclearbit(list->id, usedefinfo[blocklist->block->blockIndex].defin);
            }
            bitvectorsetbit(defID, usedefinfo[blocklist->block->blockIndex].defin);
        }
    } else if (def->v.kind == PCOp_MEMORY) {
        oud = findobjectusedef(v->u.object);
        for (blocklist = loop->blocks; blocklist; blocklist = blocklist->next) {
            for (list = oud->defs; list; list = list->next) {
#if VERSION < VERSION_GC_2_5
                if (uniquely_aliases(pcode, Defs[list->id].pcode)) {
#else
                if (must_alias_object(pcode, Defs[list->id].v.u.object)) {
#endif
                    bitvectorclearbit(list->id, usedefinfo[blocklist->block->blockIndex].defin);
                }
            }
            bitvectorsetbit(defID, usedefinfo[blocklist->block->blockIndex].defin);
        }
    } else {
        CError_FATAL(545);
    }
}

static int srawi_addze_maymove(PCode* pcode, Loop* loop) {
    RegUseOrDef* list;
    UseOrDef* def;
    TinyValue* v;
    int defID;
    int nextDefID;

    defID = pcode->defID;
    nextDefID = pcode->nextPCode->defID;

    def = &Defs[defID];
    v = &def->v;
    if (defID >= number_of_Defs) {
        return FALSE;
    }
    if (def->pcode != pcode) {
        return FALSE;
    }
    if ((defID + 1) < number_of_Defs && def[1].pcode == pcode) {
        return FALSE;
    }

    if (def->v.kind == PCOp_REGISTER && def->v.arg == RegClass_GPR) {
        for (list = reg_Defs[RegClass_GPR][v->u.reg]; list; list = list->next) {
            if (bitvectorgetbit(Defs[list->id].pcode->block->blockIndex, loop->memberblocks) &&
                list->id !=
                defID &&
                list->id != nextDefID) {
                return FALSE;
            }
        }
    } else {
        CError_FATAL(582);
    }

    if (!uniquelyreachesalluses(pcode->defID, loop)) {
        return FALSE;
    }
    if (!dominatesallexits(pcode, loop) && isliveonexit(&Defs[pcode->defID].v, loop)) {
        return FALSE;
    }
    if (!dominatesallexits(pcode->nextPCode, loop) &&
        isliveonexit(&Defs[pcode->nextPCode->defID].v, loop)) {
        return FALSE;
    }

    return TRUE;
}

static int srawi_addze_isloopinvariant(PCode* pcode, Loop* loop, UInt32* vec) {
    static PCode* oldNextInstr;
    PCode* nextInstr;

    nextInstr = pcode->nextPCode;
    if (pcode->op == PC_ADDZE && oldNextInstr == pcode) {
        oldNextInstr = NULL;
        return TRUE;
    } else if (pcode->op ==
               PC_SRAWI &&
               nextInstr &&
               nextInstr->op ==
               PC_ADDZE &&
               pcode->args[0].data.reg.reg ==
               nextInstr->args[0].data.reg.reg &&
               nextInstr->args[0].data.reg.reg ==
               nextInstr->args[1].data.reg.reg &&
               !(pcode->flags & (fIsCall | fIsPtrOp | fIsVolatile | fSideEffects)) &&
               !(nextInstr->flags & (fIsCall | fIsPtrOp | fIsVolatile | fSideEffects)) &&
               isloopinvariant(pcode, loop, vec, TRUE, FALSE) &&
               srawi_addze_maymove(pcode, loop)) {
        oldNextInstr = nextInstr;
        return TRUE;
    } else {
        oldNextInstr = NULL;
        return FALSE;
    }
}

static void removeblockfromloop(Loop* loop, PCodeBlock* block) {
    BlockList* list;
    BlockList** ptr;

    bitvectorclearbit(block->blockIndex, loop->memberblocks);
    bitvectorclearbit(block->blockIndex, loop->exitblocks);
    bitvectorclearbit(block->blockIndex, loop->exit_dominators);
    bitvectorclearbit(block->blockIndex, loop->backedge_dominators);
    loop->bodySize -= block->pcodeCount;

    ptr = &loop->blocks;
    while ((list = *ptr)) {
        if (list->block == block) {
            *ptr = list->next;
        } else {
            ptr = &list->next;
        }
    }
}

static void changesuccessor(PCodeBlock* block, PCodeBlock* from, PCodeBlock* to) {
    PCLink** ptr;
    PCLink* link;

    for (link = block->successors; link; link = link->nextLink) {
        if (link->block == from) {
            link->block = to;
        }
    }

    ptr = &from->predecessors;
    while ((link = *ptr)) {
        if (link->block == block) {
            *ptr = link->nextLink;
            link->nextLink = to->predecessors;
            to->predecessors = link;
        } else {
            ptr = &link->nextLink;
        }
    }
}

static void movesuccessor(PCodeBlock* to, PCodeBlock* from, PCodeBlock* block) {
    PCLink** ptr;
    PCLink* link;

    for (link = block->predecessors; link; link = link->nextLink) {
        if (link->block == from) {
            link->block = to;
        }
    }

    ptr = &from->successors;
    while ((link = *ptr)) {
        if (link->block == block) {
            *ptr = link->nextLink;
            link->nextLink = to->successors;
            to->successors = link;
        } else {
            ptr = &link->nextLink;
        }
    }
}

static void movecmptopreheader(
    Loop* loop, PCodeBlock* block, PCode* pc1, PCode* pc2, PCodeArg* op
) {
    PCodeBlock* preheader;
    PCode* pc3;

    preheader = loop->preheader;
    if (PCODE_FLAG_SET_F(pc1) & fRecordBit) {
        moveinvariantcomputation(pc1, loop);
    } else {
        deletepcode(pc1);
        insertpcodebefore(loop->preheader->lastPCode, pc1);
        loop->bodySize--;
        movedloopinvariantcode = TRUE;
    }
    loop->preheader = NULL;

    insertpreheaderblock(loop);

    pc3 = preheader->lastPCode;
    CError_ASSERT(775, pc3->op == PC_B);
    deletepcode(pc3);
    deletepcode(pc2);
    appendpcode(preheader, pc2);
    movesuccessor(preheader, block, op->data.label.label->block);
}

static PCodeBlock* appendheadercopy(
    Loop* loop, PCodeBlock* block1, PCodeBlock* block2, PCodeBlock* block3
) {
    PCodeBlock* newblock1;
    PCodeBlock* newblock2;
    PCLink* link;
    PCode* scan;

    newblock1 = lalloc(sizeof(PCodeBlock));
    newblock2 = lalloc(sizeof(PCodeBlock));

    newblock1->labels = NULL;
    newblock1->predecessors = newblock1->successors = NULL;
    newblock1->firstPCode = newblock1->lastPCode = NULL;
    newblock1->pcodeCount = 0;
    newblock1->loopWeight = loop->body->loopWeight;
    newblock1->flags = 0;
    newblock1->blockIndex = pcblockcount++;

    newblock2->labels = NULL;
    newblock2->predecessors = newblock2->successors = NULL;
    newblock2->firstPCode = newblock2->lastPCode = NULL;
    newblock2->pcodeCount = 0;
    newblock2->loopWeight = loop->body->loopWeight;
    newblock2->flags = 0;
    newblock2->blockIndex = pcblockcount++;

    newblock1->nextBlock = newblock2;
    newblock2->prevBlock = newblock1;
    newblock1->prevBlock = block1;
    newblock2->nextBlock = block1->nextBlock;
    block1->nextBlock = newblock1;
    newblock2->nextBlock->prevBlock = newblock2;

    pclabel(newblock1, makepclabel());
    pclabel(newblock2, makepclabel());

    changesuccessor(block1, block1->successors->block, newblock1);

    link = lalloc(sizeof(PCLink));
    link->block = newblock2;
    link->nextLink = newblock1->successors;
    newblock1->successors = link;

    link = lalloc(sizeof(PCLink));
    link->block = newblock1;
    link->nextLink = newblock2->predecessors;
    newblock2->predecessors = link;

    appendpcode(newblock2, makepcode(PC_B, block2->nextBlock->labels));
    pcbranch(newblock2, block2->nextBlock->labels);
    pccomputepredecessors1(newblock2);

    for (scan = block2->firstPCode; scan; scan = scan->nextPCode) {
        appendpcode(newblock1, copypcode(scan));
    }

    pcbranch(newblock1, block3->labels);

    link = lalloc(sizeof(PCLink));
    link->block = newblock1;
    link->nextLink = block3->predecessors;
    block3->predecessors = link;

    addblocktoloop(loop, newblock1);
    if (bitvectorgetbit(block2->blockIndex, loop->exit_dominators)) {
        bitvectorsetbit(newblock1->blockIndex, loop->exit_dominators);
    }
    if (bitvectorgetbit(block2->blockIndex, loop->backedge_dominators)) {
        bitvectorsetbit(newblock1->blockIndex, loop->backedge_dominators);
    }

    for (loop = loop->parent; loop; loop = loop->parent) {
        addblocktoloop(loop, newblock1);
        if (bitvectorgetbit(block2->blockIndex, loop->exit_dominators)) {
            bitvectorsetbit(newblock1->blockIndex, loop->exit_dominators);
        }
        if (bitvectorgetbit(block2->blockIndex, loop->backedge_dominators)) {
            bitvectorsetbit(newblock1->blockIndex, loop->backedge_dominators);
        }

        addblocktoloop(loop, newblock2);
        if (bitvectorgetbit(block2->blockIndex, loop->exit_dominators)) {
            bitvectorsetbit(newblock2->blockIndex, loop->exit_dominators);
        }
        if (bitvectorgetbit(block2->blockIndex, loop->backedge_dominators)) {
            bitvectorsetbit(newblock2->blockIndex, loop->backedge_dominators);
        }
    }

    return newblock1;
}

static BlockList* findswitchpath(Loop* loop, PCodeBlock* block) {
    BlockList* head;
    BlockList* tail;
    BlockList* node;
    PCodeBlock* scan;

    head = NULL;
    tail = NULL;

    for (scan = block; scan && scan != loop->body; scan = scan->successors->block) {
        if (!bitvectorgetbit(scan->blockIndex, loop->memberblocks)) {
            return NULL;
        }
        if (scan->successors && scan->successors->nextLink) {
            return NULL;
        }

        node = oalloc(sizeof(BlockList));
        node->block = scan;
        node->next = NULL;
        if (head) {
            tail->next = node;
            tail = node;
        } else {
            head = node;
            tail = node;
        }
    }

    return head;
}

static void simpleunswitchloop(Loop* loop) {
    PCode* terminator;
    PCodeArg* target;
    UInt32* myvec;
    PCodeBlock* block;
    PCode* candidate;
    BlockList* branchPath;
    BlockList* common;
    BlockList* previous;
    Loop* newLoop;
    PCodeBlock* preheader;
    PCodeBlock* newBlock;
    BlockList* fallthroughPath;
    PCode* scan;
    PCode* lastpcode;
    int i;
    PCode* instr;
    PCode* firstInstr;
    BlockList* path;
    BlockList* otherPath;

    if (!(lastpcode = loop->body->lastPCode)) {
        return;
    }
    if (lastpcode->op != PC_BT && lastpcode->op != PC_BF) {
        return;
    }
    if (lastpcode->args[2].kind != PCOp_LABEL) {
        return;
    }
    if (!bitvectorgetbit(
            lastpcode->args[2].data.label.label->block->blockIndex, loop->memberblocks
        )) {
        return;
    }
    if (loop->has_memory_barrier) {
        return;
    }
    if (loop->has_call) {
        return;
    }
    if (bitvectorgetbit(loop->body->nextBlock->blockIndex, loop->memberblocks)) {
        return;
    }

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        if (bitvectorgetbit(block->blockIndex, loop->memberblocks)) {
            break;
        }
    }

    if (!block) {
        return;
    }

    myvec = oalloc(4 * ((number_of_Defs + 31) >> 5));
    bitvectorcopy(myvec, usedefinfo[block->blockIndex].defin, number_of_Defs);
    for (candidate = loop->preheader->nextBlock->firstPCode; candidate;
         candidate = candidate->nextPCode) {
        if (!(PCODE_FLAG_SET_F(candidate) &
              (fIsCall | fIsPtrOp | fIsVolatile | fSideEffects | fRecordBit))) {
            if (isloopinvariant(candidate, loop, myvec, FALSE, TRUE)) {
                break;
            }
        }
    }

    if (!candidate || candidate->argCount < 1) {
        return;
    }

    if (candidate->argCount <
        1 ||
        candidate->args[0].kind !=
        PCOp_REGISTER ||
        candidate->args[0].arg != RegClass_CRFIELD) {
        return;
    }

    terminator = candidate->block->lastPCode;
    if (!terminator ||
        !(terminator->flags & fIsBranch) ||
        terminator->args[0].kind !=
        PCOp_REGISTER ||
        terminator->args[0].arg != RegClass_CRFIELD) {
        return;
    }

    if (terminator->args[0].data.reg.reg != candidate->args[0].data.reg.reg) {
        return;
    }

    target = NULL;
    for (i = 0; i < terminator->argCount; i++) {
        if (terminator->args[i].kind == PCOp_LABEL) {
            target = &terminator->args[i];
        }
    }

    if (target) {
        preheader = loop->preheader;

        fallthroughPath = findswitchpath(loop, block->nextBlock);
        if (!fallthroughPath) {
            return;
        }

        branchPath = findswitchpath(loop, target->data.label.label->block);
        if (!branchPath) {
            return;
        }

        common = NULL;
        previous = NULL;
        for (path = fallthroughPath; path; path = path->next) {
            for (otherPath = branchPath; otherPath; otherPath = otherPath->next) {
                if (path->block == otherPath->block) {
                    common = path;
                    break;
                }
            }
            if (common) {
                break;
            }
            previous = path;
        }

        CError_ASSERT(1192, previous->block);

        if (previous->block->lastPCode && previous->block->lastPCode->op == PC_B) {
            deletepcode(previous->block->lastPCode);
        }

        for (path = common; path; path = path->next) {
            for (scan = path->block->firstPCode; scan; scan = scan->nextPCode) {
                if (scan->op != PC_B) {
                    appendpcode(previous->block, copypcode(scan));
                }
            }
        }

        newBlock = appendheadercopy(loop, previous->block, loop->body, block);
        movecmptopreheader(loop, block, candidate, terminator, target);

        if (block->pcodeCount) {
            if (branchPath->block->firstPCode) {
                firstInstr = branchPath->block->firstPCode;
                for (instr = block->firstPCode; instr; instr = instr->nextPCode) {
                    if (instr->op != PC_B) {
                        insertpcodebefore(firstInstr, copypcode(instr));
                    }
                }
            } else {
                for (instr = block->firstPCode; instr; instr = instr->nextPCode) {
                    if (instr->op != PC_B) {
                        appendpcode(branchPath->block, copypcode(instr));
                    }
                }
            }
        }

        target = NULL;
        for (i = 0; i < loop->body->lastPCode->argCount; i++) {
            if (loop->body->lastPCode->args[i].kind == PCOp_LABEL) {
                target = &loop->body->lastPCode->args[i];
            }
        }

        CError_ASSERT(1250, target != NULL);

        changesuccessor(loop->body, target->data.label.label->block, branchPath->block);
        target->data.label.label = branchPath->block->labels;

        target = NULL;
        for (i = 0; i < preheader->lastPCode->argCount; i++) {
            if (preheader->lastPCode->args[i].kind == PCOp_LABEL) {
                target = &preheader->lastPCode->args[i];
            }
        }

        CError_ASSERT(1267, target != NULL);

        changesuccessor(preheader, target->data.label.label->block, loop->body);
        target->data.label.label = loop->body->labels;

        target = NULL;
        for (i = 0; i < loop->preheader->lastPCode->argCount; i++) {
            if (loop->preheader->lastPCode->args[i].kind == PCOp_LABEL) {
                target = &loop->preheader->lastPCode->args[i];
            }
        }

        CError_ASSERT(1284, target != NULL);

        changesuccessor(loop->preheader, target->data.label.label->block, newBlock);
        target->data.label.label = newBlock->labels;

        newLoop = lalloc(sizeof(Loop));
        newLoop->parent = loop->parent;
        newLoop->children = NULL;
        newLoop->nextSibling = loop->nextSibling;
        loop->nextSibling = newLoop;
        newLoop->body = loop->body;
        newLoop->preheader = NULL;
        newLoop->blocks = NULL;
        newLoop->basicInductionVars = NULL;
        newLoop->footer = NULL;
        newLoop->inductionUpdate = NULL;
        newLoop->loopWeight = loop->loopWeight;

        bitvectorinitialize(
            newLoop->memberblocks = lalloc(4 * ((loopdetection_nblocks + 31) >> 5)),
            loopdetection_nblocks,
            0
        );
        bitvectorinitialize(
            newLoop->exitblocks = lalloc(4 * ((loopdetection_nblocks + 31) >> 5)),
            loopdetection_nblocks,
            0
        );
        bitvectorinitialize(
            newLoop->exit_dominators = lalloc(4 * ((loopdetection_nblocks + 31) >> 5)),
            loopdetection_nblocks,
            0
        );
        bitvectorinitialize(
            newLoop->backedge_dominators = lalloc(4 * ((loopdetection_nblocks + 31) >> 5)),
            loopdetection_nblocks,
            0
        );

        removeblockfromloop(loop, newLoop->body);
        addblocktoloop(newLoop, newLoop->body);

        bitvectorsetbit(newLoop->body->blockIndex, newLoop->exitblocks);
        bitvectorsetbit(newLoop->body->blockIndex, newLoop->backedge_dominators);
        bitvectorsetbit(newLoop->body->blockIndex, newLoop->exit_dominators);

        for (path = branchPath; path; path = path->next) {
            removeblockfromloop(loop, path->block);
            addblocktoloop(newLoop, path->block);
            bitvectorsetbit(path->block->blockIndex, newLoop->backedge_dominators);
        }

        newLoop->preheader = NULL;
        insertpreheaderblock(newLoop);
        analyzeloop(newLoop);

        loop->body = newBlock;

        for (path = loop->blocks; path; path = path->next) {
            bitvectorsetbit(path->block->blockIndex, loop->backedge_dominators);
        }

        bitvectorsetbit(newBlock->blockIndex, loop->exitblocks);
        analyzeloop(loop);

        unswitchedinvariantcode = TRUE;
    }
}

static void simpleunswitchloops(Loop* loop) {
    while (loop) {
        if (loop->children) {
            simpleunswitchloops(loop->children);
        } else if (!loop->is_straight_line) {
            simpleunswitchloop(loop);
        }
        loop = loop->nextSibling;
    }
}

static void moveinvariantsfromloop(Loop* loop) {
    RegUseOrDef* list;
    BlockList* blocklist;
    PCode* instr;
    PCode* nextInstr;
    UInt32* myvec;
    int defID;
    int flag;
    PCodeBlock* block;
    UseOrDef* def;

    myvec = oalloc(4 * ((number_of_Defs + 31) >> 5));
    do {
        flag = FALSE;
        for (blocklist = loop->blocks; blocklist; blocklist = blocklist->next) {
            block = blocklist->block;
            bitvectorcopy(myvec, usedefinfo[block->blockIndex].defin, number_of_Defs);
            for (instr = block->firstPCode; instr; instr = nextInstr) {
                nextInstr = instr->nextPCode;
                if (!(instr->flags & fIsBranch) && instr->argCount) {
                    if (!(instr->flags & (fIsCall | fIsPtrOp | fIsVolatile | fSideEffects)) &&
                        isloopinvariant(instr, loop, myvec, FALSE, FALSE) &&
                        maymove(instr, loop)) {
                        moveinvariantcomputation(instr, loop);
                        flag = TRUE;
                    } else if (srawi_addze_isloopinvariant(instr, loop, myvec)) {
                        moveinvariantcomputation(instr, loop);
                        flag = TRUE;
                    }

                    for (def = &Defs[defID = instr->defID];
                         defID < number_of_Defs && def->pcode == instr;
                         def++, defID++) {
                        if (def->v.kind == PCOp_REGISTER) {
                            for (list = reg_Defs[def->v.arg][def->v.u.reg]; list;
                                 list = list->next) {
                                bitvectorclearbit(list->id, myvec);
                            }
                        } else if (def->v.kind == PCOp_MEMORY) {
                            if (def->v.arg == RefType_0) {
                                for (list = findobjectusedef(def->v.u.object)->defs; list;
                                     list = list->next) {
#if VERSION < VERSION_GC_2_5
                                    if (uniquely_aliases(instr, Defs[list->id].pcode)) {
#else
                                    if (must_alias_object(instr, Defs[list->id].v.u.object)) {
#endif
                                        bitvectorclearbit(list->id, myvec);
                                    }
                                }
                            }
                        } else {
                            CError_FATAL(1434);
                        }

                        bitvectorsetbit(defID, myvec);
                    }
                }
            }
        }
    } while (flag);
}

static void moveinvariantsfromloops(Loop* loop) {
    while (loop) {
        if (loop->children) {
            moveinvariantsfromloops(loop->children);
        }
        moveinvariantsfromloop(loop);
        loop = loop->nextSibling;
    }
}

void moveloopinvariantcode(void) {
    unswitchedinvariantcode = FALSE;
    movedloopinvariantcode = FALSE;
    if (loopsinflowgraph) {
        moveinvariantsfromloops(loopsinflowgraph);
        simpleunswitchloops(loopsinflowgraph);
    }
    freeoheap();
}
