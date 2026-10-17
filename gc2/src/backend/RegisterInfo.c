// RegisterInfo.c (2.6: 0x4cfde0..0x4d1040), Ninji's Pro7 file of that name.
//
// Built -inline auto,deferred, so the original holds the functions in reverse of this order: Pro7's
// order, with 2.6's global register variable functions (Registers_GetRegInfo,
// is_global_registers_variable, setup_global_registers, finish_global_registers: Wii publics of its
// RegisterInfo module) where the binary has them, and Pro7's static first_nonvolatile_reg grown
// into their per-class worker. The .data (spr_to_sysreg, init_target_registers' tables, then the
// literals in reverse function order) follows from that order.
#include "compiler/RegisterInfo.h"
#include "compiler/CError.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/objects.h"
#include "compiler/OracleProbes.h"
#include "compiler/PCode.h"
#include "compiler/PPCError.h"
#include "compiler/types.h"
#include <string.h>
#include "compiler/CPrep.h"

short last_exception_register[RegClassMax];
short first_fe_temporary_register[RegClassMax];
short last_argument_register[RegClassMax];
SInt16 _FP_;
SInt16 _CALLER_SP_;
char* special_register_names[RegClassMax][RegisterMax];
RegInfo* reg_info_list;
UInt8 ok_for_global_registers;
SInt32 num_register_variables[RegClassMax];
static short used_regs_before_coloring;
static UInt8 save_state[RegisterMax];

short spr_to_sysreg[4] = {1, 8, 9, 0x100};

// Pro7's asm_used_register
void asm_used_register(RegClass rclass, short reg) {
    long i;

    if (reg >= n_real_registers[rclass]) {
        return;
    }
    if (reg_state[rclass][reg] == RegState0) {
        if (reg == nonvolatile_registers[rclass][used_nonvolatile_registers[rclass]]) {
            if (assignable_registers[rclass] > 0) {
                assignable_registers[rclass]--;
            }
            reg_state[rclass][reg] = RegState1;
            used_nonvolatile_registers[rclass]++;
        } else {
            for (i = used_nonvolatile_registers[rclass]; i < n_nonvolatile_registers[rclass]; i++) {
                if (reg == nonvolatile_registers[rclass][i]) {
                    reg_state[rclass][reg] = RegState1;
                    if (assignable_registers[rclass] > 0) {
                        assignable_registers[rclass]--;
                    }
                }
            }
        }
    }
}

void retain_register(Object* obj, RegClass rclass, short reg) {
    VarInfo* vi;

    // the literal: 2.6 compares the short (`cmp bp, 0x20`); Pro7's `(short) reg < RegisterMax`
    // compares an int
    CError_ASSERT(95, reg < 32);

    if (reg_state[rclass][reg] == RegState0) {
        assignable_registers[rclass]--;
        reg_state[rclass][reg] = RegState1;
        if (reg == nonvolatile_registers[rclass][used_nonvolatile_registers[rclass]]) {
            used_nonvolatile_registers[rclass]++;
        }
    }

    if (obj) {
        vi = Registers_GetVarInfo(obj);
        vi->rclass = rclass;
        vi->flags |= VarInfoFlag2;
        vi->reg = reg;
    }
}

void retain_GPR_pair(Object* obj, short reg, short regHi) {
    VarInfo* vi;

    retain_register(NULL, RegClass_GPR, reg);
    retain_register(NULL, RegClass_GPR, regHi);

    if (obj) {
        vi = Registers_GetVarInfo(obj);
        vi->rclass = RegClass_GPR;
        vi->flags |= VarInfoFlag2 | VarInfoFlag4;
        vi->reg = reg;
        vi->regHi = regHi;
    }
}

// Pro7's is_register_object; 2.6 also takes a global register variable
int is_register_object(Object* obj) {
    return obj->sclass == TK_REGISTER || is_global_registers_variable(obj);
}

// 2.6 (Wii's name): a data object's global register binding
ObjRegInfo* Registers_GetRegInfo(Object* obj) {
    if (obj == NULL) {
        return NULL;
    }
    switch (obj->datatype) {
        case DDATA:
            return &obj->u.data.reginfo;
        default:
            return NULL;
    }
}

// 2.6 (Wii's name)
int is_global_registers_variable(Object* obj) {
    ObjRegInfo* info;

    info = Registers_GetRegInfo(obj);
    if (copts.ignore_global_reg_vars) {
        return FALSE;
    }
    if (info && info->reg) {
        return TRUE;
    }
    return FALSE;
}

// Pro7's GetABIFirstNonVolatile
int GetABIFirstNonVolatile(RegClass rclass) {
    switch (rclass) {
        case RegClass_SPR:
            return 3;
        case RegClass_CRFIELD:
            return 2;
        case RegClass_VR:
            return 20;
        case RegClass_GPR:
            return 14;
        case RegClass_FPR:
            return 14;
        default:
            return -1;
    }
}

char GetRegisterClassName(RegClass rclass) {
    switch (rclass) {
        case RegClass_VR:
            return 'v';
        case RegClass_GPR:
            return 'r';
        case RegClass_FPR:
            return 'f';
        default:
            CError_FATAL(242);
            return '?';
    }
}

// Pro7's first_nonvolatile_reg: 2.6 also binds the class's global register variables here
static int first_nonvolatile_reg(RegClass rclass) {
    RegInfo* binding;
    Object* obj;
    VarInfo* vi;
    int first;
    int base;
    int result;
    int count;
    int i;
    int used[32];
    char name[2];

    first = GetABIFirstNonVolatile(rclass);
    result = first;
    if (copts.ignore_global_reg_vars) {
        return result;
    }
    base = result;
    // 2.6 clears with `count` as the index and indexes `used` by a copy of `first` (`i` here, or
    // `first`, moves registers)
    for (count = 0; count < 32; count++) {
        used[count] = 0;
    }

    for (binding = reg_info_list; binding; binding = binding->next) {
        if (rclass == binding->reginfo.rclass) {
            used[binding->reginfo.reg - base] = binding->reginfo.reg;
            if (binding->reginfo.reg >= result) {
                result = binding->reginfo.reg + 1;
            }
            obj = binding->object;
            if (obj && obj->otype == OT_OBJECT) {
                CError_ASSERT(279, Registers_GetVarInfo(obj)->reg == 0);
                if (reg_state[binding->reginfo.rclass][binding->reginfo.reg] == RegState0) {
                    reg_state[binding->reginfo.rclass][binding->reginfo.reg] = RegState1;
                    vi = Registers_GetVarInfo(obj);
                    vi->flags |= VarInfoFlag2;
                    vi->reg = binding->reginfo.reg;
                    vi->regHi = 0;
                    vi->rclass = binding->reginfo.rclass;
                } else {
                    char name1[2];
                    name1[0] = GetRegisterClassName(binding->reginfo.rclass);
                    name1[1] = 0;
                    PPCError_Error(PPCErrorStr202, obj, name1, binding->reginfo.reg);
                }
            } else {
                PPCError_Error(PPCErrorStr204, binding->name->name);
            }
        }
    }

    count = 0;
    for (i = 0; i < num_register_variables[rclass]; i++) {
        if (!used[i]) {
            for (i++; i < num_register_variables[rclass]; i++) {
                if (used[i]) {
                    name[0] = GetRegisterClassName(rclass);
                    name[1] = 0;
                    PPCError_Error(PPCErrorStr199, name, used[i] - 1);
                    count++;
                    break;
                }
            }
        }
    }

    if (count) {
        return first;
    }
    return result;
}

// setup_global_registers (Wii)
void setup_global_registers(void) {
    RegClass rclass;

    reg_info_list = NULL;
    ok_for_global_registers = TRUE;
    for (rclass = 0; rclass < RegClassMax; rclass++) {
        num_register_variables[rclass] = 29 - GetABIFirstNonVolatile(rclass);
    }
}

// finish_global_registers (Wii)
void finish_global_registers(void) {
    RegClass rclass;

    if (ok_for_global_registers) {
        memset(reg_state, 0, sizeof(reg_state));
        for (rclass = 0; rclass < RegClassMax; rclass++) {
            first_nonvolatile_reg(rclass);
        }
    }
}

// Pro7's setup_diagnostic_reg_strings
void setup_diagnostic_reg_strings(void) {
    register_class_name[RegClass_SPR] = "SPR";
    register_class_format[RegClass_SPR] = "spr%ld";
    register_class_name[RegClass_CRFIELD] = "CRFIELD";
    register_class_format[RegClass_CRFIELD] = "cr%ld";
    register_class_name[RegClass_VR] = "VR";
    register_class_format[RegClass_VR] = "vr%ld";
    register_class_name[RegClass_FPR] = "FPR";
    register_class_format[RegClass_FPR] = "f%ld";
    register_class_name[RegClass_GPR] = "GPR";
    register_class_format[RegClass_GPR] = "r%ld";
}

void init_target_registers(void) {
    RegClass rclass;
    int reg;
    int end;

    static int last_nonvolatile_reg[] = {3, 5, 31, 31, 31};
    static int nonvol_reserve[] = {0, 0, 0, 4, 3};

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        for (reg = 0; reg < RegisterMax; reg++) {
            special_register_names[rclass][reg] = NULL;
        }
    }

    special_register_names[RegClass_SPR][0] = "XER";
    special_register_names[RegClass_SPR][1] = "LR";
    special_register_names[RegClass_SPR][2] = "CTR";
    special_register_names[RegClass_SPR][3] = "VRSAVE";
    special_register_names[RegClass_GPR][1] = "SP";
    special_register_names[RegClass_GPR][13] = "SDA";

    setup_diagnostic_reg_strings();
    n_real_registers[RegClass_SPR] = 4;
    n_real_registers[RegClass_CRFIELD] = 8;
    n_real_registers[RegClass_VR] = 32;
    n_real_registers[RegClass_FPR] = 32;
    n_real_registers[RegClass_GPR] = 32;
    reg_state[RegClass_GPR][1] = RegState2;
    reg_state[RegClass_GPR][2] = RegState2;
    reg_state[RegClass_GPR][13] = RegState2;
    reg_state[RegClass_CRFIELD][5] = RegState2;

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        n_nonvolatile_registers[rclass] = 0;
        if (last_nonvolatile_reg[rclass] >= 0) {
            end = first_nonvolatile_reg(rclass);
            for (reg = last_nonvolatile_reg[rclass]; reg >= end; reg--) {
                if (reg_state[rclass][reg] == RegState0) {
                    nonvolatile_registers[rclass][n_nonvolatile_registers[rclass]++] = reg;
                }
            }
        }

        assignable_registers[rclass] = n_nonvolatile_registers[rclass] - nonvol_reserve[rclass];
        if (assignable_registers[rclass] < 0) {
            assignable_registers[rclass] = 0;
        }

        n_scratch_registers[rclass] = 0;
        for (reg = 0; reg < n_real_registers[rclass]; reg++) {
            if (reg < GetABIFirstNonVolatile(rclass) || reg > last_nonvolatile_reg[rclass]) {
                if (reg_state[rclass][reg] == RegState0) {
                    scratch_registers[rclass][n_scratch_registers[rclass]++] = reg;
                }
            }
        }
    }

    _FP_ = -1;
    _CALLER_SP_ = -1;
    optimizing = (copts.optimizationlevel > 0) && !disable_optimizer;
}

// Pro7's; 2.6 puts a soft-float value in a GPR (a double in a GPR pair) and a paired single in an
// FPR
void assign_register_by_type(Object* obj) {
    VarInfo* vi;
    Type* ty;
    Boolean flag;

    ty = obj->type;
    vi = Registers_GetVarInfo(obj);
    flag = FALSE;
    vi->rclass = RegClassMax;
    vi->reg = 0;
    vi->regHi = 0;

    if ((ty->type == TYPEINT) ||
        (ty->type == TYPEENUM) ||
        ((ty->type == TYPEPOINTER || ty->type == TYPEARRAY) && (ty->type != TYPEARRAY)) ||
        ((ty->type == TYPEMEMBERPOINTER) && (ty->size == 4U)) ||
        (copts.soft_float && IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(ty))) {
        if ((((ty->type == TYPEINT) || (ty->type == TYPEENUM)) && (ty->size == 8)) ||
            (copts.soft_float && IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(ty) && ty->size != 4)) {
            flag = TRUE;
        }
        vi->rclass = RegClass_GPR;
    } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(ty) || IS_TYPE_VEC2X32FLOAT(ty)) {
        vi->rclass = RegClass_FPR;
    } else if ((ty->type == TYPESTRUCT) &&
               (TYPE_STRUCT(ty)->stype >= STRUCT_VECTOR_UCHAR) &&
               (TYPE_STRUCT(ty)->stype <= STRUCT_VECTOR_PIXEL)) {
        vi->rclass = RegClass_VR;
    } else {
        return;
    }

    if (vi->rclass < RegClassMax) {
        if (flag) {
            CError_ASSERT(520, vi->rclass == RegClass_GPR);
            if (assignable_registers[vi->rclass] > 1) {
                assign_GPR_pair(obj);
            }
        } else {
            if (assignable_registers[vi->rclass] > 0) {
                assign_register_to_variable(obj, vi->rclass);
            }
        }
    }
}

void assign_GPR_pair(Object* obj) {
    VarInfo* vi;
    short reg;
    short regHi;

    vi = Registers_GetVarInfo(obj);
    if (optimizing) {
        reg = ALLOC_GPR();
        regHi = ALLOC_GPR();
    } else {
        CError_ASSERT(554, assignable_registers[RegClass_GPR] >= 2);
        reg = obtain_nonvolatile_register(RegClass_GPR);
        regHi = obtain_nonvolatile_register(RegClass_GPR);
        retain_GPR_pair(obj, reg, regHi);
    }

    vi->rclass = RegClass_GPR;
    if (reg > 0 && regHi > 0) {
        vi->flags |= VarInfoFlag2 | VarInfoFlag4;
        vi->reg = reg;
        vi->regHi = regHi;
    } else {
        CError_FATAL(567);
    }
}

// Pro7's open_fe_temp_registers
void open_fe_temp_registers(void) {
    int r;

    r = used_virtual_registers[RegClass_GPR];
    first_fe_temporary_register[RegClass_GPR] = last_temporary_register[RegClass_GPR] = r;
    r = used_virtual_registers[RegClass_FPR];
    first_fe_temporary_register[RegClass_FPR] = last_temporary_register[RegClass_FPR] = r;
    r = used_virtual_registers[RegClass_VR];
    first_fe_temporary_register[RegClass_VR] = last_temporary_register[RegClass_VR] = r;
    ORACLE_PROBE(OracleProbe_Counter(9, 0, NULL));
}

// Pro7's set_last_exception_registers
void set_last_exception_registers(void) {
    last_exception_register[RegClass_GPR] = used_virtual_registers[RegClass_GPR] - 1;
    last_exception_register[RegClass_FPR] = used_virtual_registers[RegClass_FPR] - 1;
    last_exception_register[RegClass_VR] = used_virtual_registers[RegClass_VR] - 1;
    ORACLE_PROBE(OracleProbe_Counter(8, 0, NULL));
}

static VarInfo* Registers_GetNewVarInfo(void) {
    VarInfo* vi = galloc(sizeof(VarInfo));
    memclrw(vi, sizeof(VarInfo));
    return vi;
}

VarInfo* Registers_GetVarInfo(Object* obj) {
    switch (obj->datatype) {
        case DDATA:
            if (!obj->u.data.info) {
                obj->u.data.info = Registers_GetNewVarInfo();
            }
            return obj->u.data.info;
        case DLOCAL:
            if (!obj->u.var.info) {
                CError_FATAL(647);
            }
            return obj->u.var.info;
        case DABSOLUTE:
            if (!obj->u.data.info) {
                obj->u.data.info = Registers_GetNewVarInfo();
            }
            return obj->u.data.info;
        default:
            CError_FATAL(660);
            return NULL;
    }
}

UInt32 colored_vrs_as_vrsave(PCodeBlock* block) {
    PCodeBlock* b;
    PCode* pc;
    UInt32 mask;
    int i;

    mask = 0;
    if (copts.altivec_vrsave == 2) {
        return 0xFFFFFFFF;
    }
    if (copts.altivec_vrsave == 0) {
        return 0;
    }

    for (b = block; b; b = b->nextBlock) {
        for (pc = b->firstPCode; pc; pc = pc->nextPCode) {
            if (pc->flags & fOpTypeVR) {
                for (i = 0; i < pc->argCount; i++) {
                    if (pc->args[i].kind == PCOp_REGISTER && pc->args[i].arg == RegClass_VR) {
                        mask |= 1 << (31 - pc->args[i].data.reg.reg);
                    }
                }
            }
        }
    }

    return mask;
}

void save_before_coloring_nonvolatile_registers(RegClass rclass) {
    used_regs_before_coloring = used_nonvolatile_registers[rclass];
    memcpy(save_state, reg_state[rclass], sizeof(save_state));
}

void reset_nonvolatile_registers(RegClass rclass) {
    used_nonvolatile_registers[rclass] = used_regs_before_coloring;
    memcpy(reg_state[rclass], save_state, sizeof(save_state));
}

#if VERSION < VERSION_GC_2_5
int is_nonvolatile_register(RegClass rclass, int reg) {
    int i;

    for (i = 0; i < n_nonvolatile_registers[rclass]; i++) {
        if (reg == nonvolatile_registers[rclass][i]) {
            return TRUE;
        }
    }

    return FALSE;
}
#endif

void init_endian(void) {
    if (copts.littleendian) {
        high_offset = 4;
        low_offset = 0;
        high_reg = 4;
        low_reg = 3;
        high_reg2 = 6;
        low_reg2 = 5;
    } else {
        high_offset = 0;
        low_offset = 4;
        high_reg = 3;
        low_reg = 4;
        high_reg2 = 5;
        low_reg2 = 6;
    }
}

void update_asm_nonvolatile_registers(void) {
    RegClass rclass;
    int i;
    int reg;

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        reg = n_nonvolatile_registers[rclass];
        for (i = n_nonvolatile_registers[rclass] - 1; i >= 0; i--) {
            if (reg_state[rclass][nonvolatile_registers[rclass][i]] == RegState1) {
                break;
            }
            reg--;
        }
        if (reg > used_nonvolatile_registers[rclass]) {
            used_nonvolatile_registers[rclass] = reg;
        }
    }
}
