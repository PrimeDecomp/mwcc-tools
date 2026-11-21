/* Wii 1.0RC1 register coalescing and interference graph simplification. */
#include "rc1_pcode.h"

#include <float.h>
#include <stddef.h>

typedef struct IGEdge {
    unsigned reserved;
    const struct IGEdge* low_next;
    const struct IGEdge* high_next;
    short low;
    short high;
} IGEdge;

typedef struct IGNode {
    struct IGNode* next;
    const void* spill_temporary;
    const void* instruction;
    int spill_cost;
    short reg;
    short color;
    unsigned short flags;
    short degree;
    unsigned edge_count;
    const IGEdge* edges;
} IGNode;

enum {
    Pushed = 0x02,
    Coalesced = 0x04,
    AvoidSpill = 0x80,
    RegisterPair = 0x200,
    RestrictedSpill = 0x400,
    IsMove = 0x10,
    RegisterPairOperand = 0x20,
};

_Static_assert(sizeof(IGNode) == 0x20, "RC1 graph node stride");
_Static_assert(offsetof(IGNode, flags) == 0x14, "RC1 graph flags");
_Static_assert(offsetof(IGNode, degree) == 0x16, "RC1 graph degree");
_Static_assert(offsetof(IGEdge, low) == 0xc, "RC1 edge endpoints");

extern IGNode* interferencegraph;
extern signed char coloring_class;
extern unsigned n_real_registers[];
extern unsigned used_virtual_registers[];
extern int available_registers(int register_class);
extern void estimatespillcosts(void);
extern IGNode* machine_specific_sort_graph(IGNode*, IGNode*);
extern short* RC1_coalesced_registers;
extern unsigned RC1_call_argument_count;
extern unsigned* RC1_call_arguments;
extern unsigned* RC1_call_argument_index;
extern const char RC1_InterferenceGraph_source_file[];
extern void CError_Internal(const char*, int);
extern int interferes(int, int);
extern void makeinterfere(int, int);
extern int machinespecific_coalesce_restriction(int, int);
extern void deletepcode(PCode*);

/* MSVC-compatible objects with floating-point code reference this CRT marker. */
int _fltused;

static short representative(short reg) {
    if ((unsigned)reg > used_virtual_registers[coloring_class])
        CError_Internal(RC1_InterferenceGraph_source_file, 412);
    while (reg != RC1_coalesced_registers[reg])
        reg = RC1_coalesced_registers[reg];
    return reg;
}

static int is_call_argument(int reg) {
    unsigned index = RC1_call_argument_index[reg];
    return index < RC1_call_argument_count && RC1_call_arguments[index] == (unsigned)reg;
}

static void merge_edges(short keep, short remove) {
    const IGNode* node = &interferencegraph[remove];
    for (const IGEdge* edge = node->edges; edge;) {
        int is_low = node->reg < edge->high;
        makeinterfere(keep, is_low ? edge->high : edge->low);
        edge = is_low ? edge->low_next : edge->high_next;
    }
}

int retro_CoalesceRegisters(void) {
    int merged = 0;
    for (PCodeBlock* block = pcbasicblocks; block; block = block->next) {
        for (PCode* op = block->first; op;) {
            PCode* next = op->next;
            if ((op->flags & IsMove) && op->args[1].reg_class == coloring_class) {
                short source = representative(op->args[1].value.reg.number);
                short destination = representative(op->args[0].value.reg.number);
                if (source == destination) {
                    deletepcode(op);
                } else if (!interferes(source, destination) &&
                           !machinespecific_coalesce_restriction(source, destination)) {
                    short keep = source < destination ? source : destination;
                    short remove = source > destination ? source : destination;
                    if (!is_call_argument(remove)) {
                        RC1_coalesced_registers[remove] = keep;
                        merge_edges(keep, remove);
                        deletepcode(op);
                        merged = 1;
                    }
                }
            }
            op = next;
        }
    }

    for (PCodeBlock* block = pcbasicblocks; block; block = block->next) {
        for (PCode* op = block->first; op; op = op->next) {
            for (int i = 0; i < op->arg_count; ++i) {
                PCodeArg* arg = &op->args[i];
                if (arg->kind != DirectRegister || arg->reg_class != coloring_class ||
                    arg->value.reg.number == RC1_coalesced_registers[arg->value.reg.number])
                    continue;
                short reg = representative(arg->value.reg.number);
                arg->value.reg.number = reg;
                if (arg->value.reg.effect & RegisterPairOperand)
                    op->args[++i].value.reg.number = reg + 1;
            }
        }
    }
    return merged;
}

static void remove_node(IGNode* node) {
    for (const IGEdge* edge = node->edges; edge;) {
        int is_low = node->reg < edge->high;
        IGNode* neighbor = &interferencegraph[is_low ? edge->high : edge->low];
        neighbor->degree -= ((node->flags | neighbor->flags) & RegisterPair) ? 2 : 1;
        edge = is_low ? edge->low_next : edge->high_next;
    }
    node->flags |= Pushed;
}

static IGNode* push_low_degree_nodes(IGNode** pushed, int available) {
    IGNode* spill;
    int changed;
    do {
        spill = NULL;
        changed = 0;
        for (unsigned reg = n_real_registers[coloring_class]; reg < used_virtual_registers[coloring_class]; ++reg) {
            IGNode* node = &interferencegraph[reg];
            if (node->flags & (Pushed | Coalesced))
                continue;
            if (node->degree < available) {
                remove_node(node);
                node->next = *pushed;
                *pushed = node;
                changed = 1;
            } else {
                node->next = spill;
                spill = node;
            }
        }
    } while (changed);
    return spill;
}

static double spill_score(const IGNode* node) {
    if (node->flags & AvoidSpill)
        return FLT_MAX;
    if (node->flags & RestrictedSpill)
        return FLT_MAX / 2;
    return (double)node->spill_cost / node->degree;
}

IGNode* retro_SimplifyGraph(void) {
    int available = available_registers(coloring_class);
    IGNode* pushed = NULL;
    IGNode* spill = push_low_degree_nodes(&pushed, available);
    pushed = machine_specific_sort_graph(pushed, NULL);
    if (spill)
        estimatespillcosts();

    while (spill) {
        IGNode* best = spill;
        double best_score = spill_score(best);
        for (IGNode* node = spill->next; node; node = node->next) {
            double score = spill_score(node);
            if (score < best_score) {
                best = node;
                best_score = score;
            }
        }

        IGNode* previous = pushed;
        remove_node(best);
        best->next = previous;
        pushed = best;
        spill = push_low_degree_nodes(&pushed, available);
        pushed = machine_specific_sort_graph(pushed, previous);
    }
    return pushed;
}
