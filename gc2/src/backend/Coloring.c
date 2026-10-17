#include "compiler/Coloring.h"
#include "compiler/CError.h"
#include "compiler/CFunc.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/InterferenceGraph.h"
#include "compiler/objects.h"
#include "compiler/OracleProbes.h"
#include "compiler/PCode.h"
#include "compiler/PPCError.h"
#include "compiler/RegisterInfo.h"
#include "compiler/SpillCode.h"
#include "compiler/StackFrame.h"
#include <float.h>

RegClass coloring_class;
static short used_regs_before_coloring;

static void markspecialregisters(RegClass rclass) {
    ObjectList* list;
    Object* object;
    VarInfo* vi;
    UInt32 i;
    RegInfo* binding;

    if (copts.soft_float && rclass == RegClass_FPR) {
        CError_FATAL(58);
    }

    for (i = 0; i < n_real_registers[rclass]; i++) {
        interferencegraph[i]->color = i;
    }

    for (list = arguments; list; list = list->next) {
        object = list->object;
        vi = Registers_GetVarInfo(object);
        if ((vi->flags & VarInfoFlag2) && vi->rclass == rclass) {
            interferencegraph[vi->reg]->spillTemporary = object;
            if (vi->flags & VarInfoFlag4) {
                interferencegraph[vi->reg]->flags |= fPairLow;
                interferencegraph[vi->regHi]->flags |= fPairHigh;
                interferencegraph[vi->regHi]->spillTemporary = object;
            }
        }
    }

    for (list = locals; list; list = list->next) {
        object = list->object;
        vi = Registers_GetVarInfo(object);
        if ((vi->flags & VarInfoFlag2) && vi->rclass == rclass) {
            interferencegraph[vi->reg]->spillTemporary = object;
            if (vi->flags & VarInfoFlag4) {
                interferencegraph[vi->reg]->flags |= fPairLow;
                interferencegraph[vi->regHi]->flags |= fPairHigh;
                interferencegraph[vi->regHi]->spillTemporary = object;
            }
        }
    }

    for (binding = reg_info_list; binding; binding = binding->next) {
        if (rclass == binding->reginfo.rclass) {
            vi = Registers_GetVarInfo(binding->object);
            if (vi->flags & VarInfoFlag2) {
                interferencegraph[vi->reg]->spillTemporary = binding->object;
            }
        }
    }
}

static IGNode* simplifygraph(void) {
    int availableRegs;
    IGNode* spilledNodes;
    IGNode* pushedNodes;
    IGNode* best;
    IGNode* node;
    UInt32 j;
    float bestScore;
    float score;
    UInt32 i;
    int flag;

    availableRegs = available_registers(coloring_class);
    pushedNodes = NULL;

    do {
        spilledNodes = NULL;
        flag = FALSE;
        for (i = n_real_registers[coloring_class]; i < used_virtual_registers[coloring_class];
             i++) {
            node = interferencegraph[i];
            if (!(node->flags & (fPushed | fCoalesced))) {
                if (node->degree < availableRegs) {
                    for (j = 0; j < node->arraySize; j++) {
                        interferencegraph[node->array[j]]->degree--;
                    }
                    node->flags |= fPushed;
                    node->next = pushedNodes;
                    pushedNodes = node;
                    flag = TRUE;
                } else {
                    node->next = spilledNodes;
                    spilledNodes = node;
                }
            }
        }
    } while (flag);

    if (spilledNodes) {
        estimatespillcosts();
    }

    while (spilledNodes) {
        best = spilledNodes;
        bestScore = (spilledNodes->reg >= used_regs_before_coloring)
            ? FLT_MAX
            : ((float)spilledNodes->spillCost / (float)spilledNodes->degree);

        for (node = spilledNodes->next; node; node = node->next) {
            score = (node->reg >= used_regs_before_coloring)
                ? FLT_MAX
                : ((float)node->spillCost / (float)node->degree);
            if (score < bestScore) {
                best = node;
                bestScore = score;
            }
        }

        for (i = 0; i < best->arraySize; i++) {
            interferencegraph[best->array[i]]->degree--;
        }

        best->flags |= fPushed;
        best->next = pushedNodes;
        pushedNodes = best;

        do {
            spilledNodes = NULL;
            flag = FALSE;
            for (i = n_real_registers[coloring_class]; i < used_virtual_registers[coloring_class];
                 i++) {
                node = interferencegraph[i];
                if (!(node->flags & (fPushed | fCoalesced))) {
                    if (node->degree < availableRegs) {
                        for (j = 0; j < node->arraySize; j++) {
                            interferencegraph[node->array[j]]->degree--;
                        }
                        node->flags |= fPushed;
                        node->next = pushedNodes;
                        pushedNodes = node;
                        flag = TRUE;
                    } else {
                        node->next = spilledNodes;
                        spilledNodes = node;
                    }
                }
            }
        } while (flag);
    }

    return pushedNodes;
}

static int colorgraph(IGNode* node) {
    UInt32 volatileRegs;
    int result;
    IGNode* otherNode;
    int reg;
    UInt32 workingMask;
    int i;
    short* array;

    result = TRUE;

    reset_nonvolatile_registers(coloring_class);
    volatileRegs = volatile_registers(coloring_class);

    while (node) {
        workingMask = volatileRegs;
        for (array = node->array, i = 0; i < node->arraySize; i++) {
            otherNode = interferencegraph[*(array++)];
            if (otherNode->color != -1 && otherNode->color < n_real_registers[coloring_class]) {
                workingMask &= ~(1 << otherNode->color);
            }
        }

        if (workingMask) {
            for (i = 0; i < n_real_registers[coloring_class]; i++) {
                if (workingMask & (1 << i)) {
                    node->color = i;
                    break;
                }
            }
        } else {
            reg = obtain_nonvolatile_register(coloring_class);
            if (reg != -1) {
                volatileRegs |= 1 << (node->color = reg);
            } else {
                if (is_global_registers_variable(node->spillTemporary)) {
                    CError_FATAL(278);
                }
                node->flags |= fSpilled;
                result = FALSE;
            }
        }

        node = node->next;
    }

    return result;
}

static void rewritepcode(void) {
    PCodeBlock* block;
    PCode* instr;
    PCodeArg* op;
    UInt32 i;
    IGNode* node;
    int reg;

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        for (instr = block->firstPCode; instr; instr = instr->nextPCode) {
            op = instr->args;
            i = instr->argCount;
            while (i--) {
                if (PC_OP_IS_ANY_REGISTER(op, coloring_class)) {
                    op->data.reg.reg = interferencegraph[op->data.reg.reg]->color;
                }
                op++;
            }

            if ((instr->flags & fIsMove) &&
                (instr->args[1].arg == coloring_class) &&
                instr->args[1].data.reg.reg == instr->args[0].data.reg.reg) {
                deletepcode(instr);
            }
        }
    }

    for (i = n_real_registers[coloring_class]; i < used_virtual_registers[coloring_class]; i++) {
        node = interferencegraph[i];
        if (node->spillTemporary && !(node->flags & fSpilled)) {
            if (node->flags & fCoalesced) {
                reg = node->color;
                while (reg >= n_real_registers[coloring_class]) {
                    reg = interferencegraph[reg]->color;
                    if (reg < 0) {
                        break;
                    }
                }
                node->color = reg;
            }

            if (node->flags & fPairHigh) {
                Registers_GetVarInfo(node->spillTemporary)->regHi = node->color;
            } else {
                Registers_GetVarInfo(node->spillTemporary)->reg = node->color;
            }
        }
    }
}

void colorinstructions(Object* proc) {
    int flag;
    RegClass rclass;

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        coloring_class = rclass;

        if (rclass == RegClass_GPR) {
            check_dynamic_aligned_frame();
        } else if (rclass ==
                   RegClass_FPR &&
                   copts.soft_float &&
                   used_virtual_registers[RegClass_FPR] > 32) {
            CError_FATAL(375);
        }

        if (used_virtual_registers[rclass] > n_real_registers[rclass]) {
            save_before_coloring_nonvolatile_registers(rclass);
            used_regs_before_coloring = used_virtual_registers[rclass];
            if (!available_registers(rclass)) {
                PPCError_Error(PPCErrorStr102, register_class_name[rclass]);
                return;
            }

            flag = TRUE;
            while (flag && used_virtual_registers[rclass] > n_real_registers[rclass]) {
                buildinterferencegraph(proc);
                markspecialregisters(rclass);
                flag = !colorgraph(simplifygraph());

                if (flag) {
                    insertspillcode();
                } else {
                    rewritepcode();
                }
                freeoheap();
            }
        }

        used_virtual_registers[rclass] = n_real_registers[rclass];
        ORACLE_PROBE(OracleProbe_Counter(5, rclass, NULL));
    }

    coloring = FALSE;
}
