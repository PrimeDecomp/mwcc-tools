#include "compiler/LoadDeletion.h"
#include "compiler/CopyPropagation.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/PCode.h"
#include "compiler/Registers.h"

int deletedloads;

static int is_load(PCode* instr) {
    // 2.6 computes the register test to a value (xor; cmp; jl; mov 1) before testing it: the
    // negated `<` does that, Pro7's `>=` (tried) compiles to a plain branch
    return (instr->op ==
            PC_LI ||
            instr->op ==
            PC_VSPLTISB ||
            instr->op ==
            PC_VSPLTISH ||
            instr->op == PC_VSPLTISW) &&
        !(instr->args[0].data.reg.reg < n_real_registers[instr->args[0].arg]);
}

static int loadpropagatestouse(int candidateID, int useID) {
    return FALSE;
}

static void deleteload(int id) {
    Candidate* candidate;

    candidate = Candidates + id;
    if (candidate->list || (candidate->pcode->flags & fSideEffects)) {
        return;
    }
    if (candidate->pcode->args[0].data.reg.reg < n_real_registers[candidate->pcode->args[0].arg]) {
        return;
    }

    deletepcode(candidate->pcode);
    deletedloads = 1;
}

// clang-format off
static Propagation load_immediate_prop = {
    &is_load,
    &loadpropagatestouse,
    &deleteload,
    "LOAD IMMEDIATE",
    "LOAD_IMMEDIATES",
    "l%ld",
    fSideEffects | fRecordBit, // 2.6: a candidate with one of these flags is not propagated
    FALSE
};
// clang-format on

void deletedeadloads(Object* proc) {
    propagateinstructions(
        proc, &load_immediate_prop, (copts.optimizationlevel >= 4) ? 4 : 1, FALSE
    );
    deletedloads = propagated_instructions;
}
