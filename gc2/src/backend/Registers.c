// Registers.c (2.6: 0x4fe310..0x4fe6a0): Pro7's file, built -inline auto,deferred (the binary holds
// it in reverse). 2.6 lacks Pro7's retain_register_for_argument, count_scratch_registers and the
// *_modified_registers functions (no rows; no .bss for mod_regs_table). Its globals are COMMONs.
#include "compiler/Registers.h"
#include "compiler/OracleProbes.h"
#include "compiler/PPCError.h"
#include "compiler/RegisterInfo.h"
#include "compiler/objects.h"

SInt32 used_virtual_registers[RegClassMax];
SInt32 used_nonvolatile_registers[RegClassMax];
SInt32 assignable_registers[RegClassMax];
SInt32 n_real_registers[RegClassMax];
SInt32 n_scratch_registers[RegClassMax];
SInt32 scratch_registers[RegClassMax][RegisterMax];
SInt32 n_nonvolatile_registers[RegClassMax];
SInt32 nonvolatile_registers[RegClassMax][RegisterMax];
UInt8 reg_state[RegClassMax][RegisterMax];
SInt32 first_temporary_register[RegClassMax];
SInt32 last_temporary_register[RegClassMax];
char* register_class_name[RegClassMax];
char* register_class_format[RegClassMax];
int coloring;
int optimizing;

#ifdef ORACLE_PROBES
// (the oracle's counter tables: the counters, the physical registers' counts, and the last
// exception registers, the first front-end temporaries and the last temporaries from the VRs on)
struct {
    SInt32* counts;
    SInt32* physical;
    short* initial;
    short* first;
    SInt32* last;
} OracleRust_CounterGlobals = {
    used_virtual_registers, n_real_registers, &last_exception_register[RegClass_VR],
    &first_fe_temporary_register[RegClass_VR], &last_temporary_register[RegClass_VR]
};
#endif

void init_registers(void) {
    RegClass rclass;
    int j;

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        for (j = 0; j < RegisterMax; j++) {
            reg_state[rclass][j] = RegState0;
        }
    }

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        used_nonvolatile_registers[rclass] = 0;
    }

    optimizing = TRUE;

    init_target_registers();
    for (rclass = 0; rclass < RegClassMax; rclass++) {
        used_virtual_registers[rclass] = n_real_registers[rclass];
        ORACLE_PROBE(OracleProbe_Counter(4, rclass, NULL));
    }

    coloring = TRUE;
}

void assign_register_to_variable(Object* obj, RegClass rclass) {
    VarInfo* vi;
    short reg;

    vi = Registers_GetVarInfo(obj);
    if (optimizing) {
        reg = used_virtual_registers[rclass]++;
    } else {
        reg = obtain_nonvolatile_register(rclass);
        retain_register(obj, rclass, reg);
    }

    vi->flags |= VarInfoFlag2;
    vi->rclass = rclass;
    vi->reg = reg;
}

int available_registers(RegClass rclass) {
    int i;
    int count = 0;

    for (i = 0; i < n_real_registers[rclass]; i++) {
        if (reg_state[rclass][i] == RegState0) {
            count++;
        }
    }

    return count;
}

UInt32 volatile_registers(RegClass rclass) {
    UInt32 mask = 0;
    int i;
    int reg;

    for (i = 0; i < n_scratch_registers[rclass]; i++) {
        reg = scratch_registers[rclass][i];
        if (reg_state[rclass][reg] == RegState0) {
            mask |= 1 << reg;
        }
    }

    return mask;
}

short obtain_nonvolatile_register(RegClass rclass) {
    int best = -1;

    while (used_nonvolatile_registers[rclass] < n_nonvolatile_registers[rclass]) {
        int tmp = nonvolatile_registers[rclass][used_nonvolatile_registers[rclass]++];
        if (reg_state[rclass][tmp] == RegState0) {
            best = tmp;
            break;
        }
    }

    return best;
}

void open_temp_registers(void) {
    RegClass rclass;

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        int r = used_virtual_registers[rclass];
        first_temporary_register[rclass] = last_temporary_register[rclass] = r;
    }
}

void check_temp_registers(void) {
    RegClass rclass;

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        if (used_virtual_registers[rclass] > 0x7FFF) {
            PPCError_ErrorTerm(PPCErrorStr219);
        }
    }

    // 2.6: an early return and a local copy of the counter where Pro7 has `if (!optimizing) {...}`
    // reading the array each time (Pro7's spelling is 91.2%, either alone 94-97%)
    if (optimizing) {
        return;
    }
    for (rclass = 0; rclass < RegClassMax; rclass++) {
        int r = used_virtual_registers[rclass];
        if (r > last_temporary_register[rclass]) {
            last_temporary_register[rclass] = r;
        }
        if (r > 256) {
            used_virtual_registers[rclass] = first_temporary_register[rclass];
            ORACLE_PROBE(OracleProbe_Counter(3, rclass, NULL));
        }
    }
}

void close_temp_registers(void) {
    RegClass rclass;

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        if (used_virtual_registers[rclass] < last_temporary_register[rclass]) {
            used_virtual_registers[rclass] = last_temporary_register[rclass];
            ORACLE_PROBE(OracleProbe_Counter(2, rclass, NULL));
        } else {
            last_temporary_register[rclass] = used_virtual_registers[rclass];
        }
    }
}
