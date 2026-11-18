/* Wii 1.0RC1 register classes and allocation. */
#include "rc1_codegen.h"

enum { PhysicalRegisterCount = 32, RegisterFree = 0, RegisterRetained = 1 };
enum { InRegister = 2, RegisterPair = 4 };

extern const char RC1_RegisterInfo_source_file[];
extern const char RC1_Registers_source_file[];
extern void* galloc(u32 size);
extern u8 gHasVarargs;
extern int optimizing;
extern int assignable_registers[];
extern int used_virtual_registers[];
extern int used_nonvolatile_registers[];
extern int n_nonvolatile_registers[];
/* RC1 stores the register-class rows consecutively. */
extern int nonvolatile_registers[];
extern u8 reg_state[];
extern short RC1_processor;
extern u8 RC1_soft_float;
extern u8 RC1_use_paired_moves_for_double;

static inline VarInfo* new_var_info(void) {
    VarInfo* info = galloc(sizeof(*info));
    memclrw(info, sizeof(*info));
    return info;
}

VarInfo* Registers_GetVarInfo(Object* object) {
    switch (object->address_kind) {
        case DataObject:
            if (!object->data_info)
                object->data_info = new_var_info();
            return object->data_info;
        case LocalObject:
            if (!object->local_info)
                CError_Internal(RC1_RegisterInfo_source_file, 649);
            return object->local_info;
        case FunctionObject:
            return 0;
        case AbsoluteObject:
            if (!object->data_info)
                object->data_info = new_var_info();
            return object->data_info;
        default:
            CError_Internal(RC1_RegisterInfo_source_file, 664);
            return 0;
    }
}

u8 Registers_ClassForType(Type* type) {
    switch (type->kind) {
        case MemberPointerType:
            if (((TypeMemberPointer*)type)->target->kind != FunctionType)
                return RegGPR;
            break;
        case EnumType:
        case PointerType:
            return RegGPR;
        case IntegerType:
            if (INTEGRAL(type) < FirstNonScalarIntegral)
                return RegGPR;
            break;
        case FloatType:
            if (INTEGRAL(type) < FirstNonScalarIntegral)
                return RC1_soft_float ? RegGPR : RegFPR;
            if (RC1_processor == GekkoProcessor && INTEGRAL(type) == PairedSingle)
                return RegFPR;
            break;
        case StructureType:
            if (VECTOR(type)) {
                if (type->size == 8)
                    return RegGPR;
                if (type->size == 16)
                    return RegVR;
            }
            break;
    }
    return NoRegister;
}

short obtain_nonvolatile_register(signed char rclass) {
    int base = rclass * PhysicalRegisterCount;
    while (used_nonvolatile_registers[rclass] < n_nonvolatile_registers[rclass]) {
        int reg = nonvolatile_registers[base + used_nonvolatile_registers[rclass]++];
        if (reg_state[base + reg] == RegisterFree)
            return reg;
    }
    return -1;
}

void retain_register(Object* object, signed char rclass, short reg) {
    if (reg >= PhysicalRegisterCount)
        CError_Internal(RC1_Registers_source_file, 168);

    int base = rclass * PhysicalRegisterCount;
    if (reg_state[base + reg] == RegisterFree) {
        const int available = assignable_registers[rclass];
        if (available > 0)
            assignable_registers[rclass] = available - 1;
        reg_state[base + reg] = RegisterRetained;
        if (reg == nonvolatile_registers[base + used_nonvolatile_registers[rclass]])
            ++used_nonvolatile_registers[rclass];
    }
    if (object) {
        VarInfo* info = Registers_GetVarInfo(object);
        info->rclass = rclass;
        info->flags |= InRegister;
        info->reg = reg;
    }
}

void assign_register_to_variable(Object* object, signed char rclass) {
    VarInfo* info = Registers_GetVarInfo(object);
    short reg;
    if (optimizing) {
        reg = used_virtual_registers[rclass]++;
    } else {
        reg = obtain_nonvolatile_register(rclass);
        retain_register(object, rclass, reg);
    }
    info->flags |= InRegister;
    info->rclass = rclass;
    info->reg = reg;
}

static inline void bind_gpr_pair(VarInfo* info, short reg, short reg_hi) {
    info->flags |= InRegister | RegisterPair;
    info->reg = reg;
    info->reg_hi = reg_hi;
}

void assign_GPR_pair(Object* object) {
    VarInfo* info = Registers_GetVarInfo(object);
    short reg, reg_hi;
    if (optimizing) {
        reg = used_virtual_registers[RegGPR]++;
        reg_hi = used_virtual_registers[RegGPR]++;
    } else {
        if (assignable_registers[RegGPR] < 2)
            CError_Internal(RC1_RegisterInfo_source_file, 573);
        reg = obtain_nonvolatile_register(RegGPR);
        reg_hi = obtain_nonvolatile_register(RegGPR);
        retain_register(NULL, RegGPR, reg);
        retain_register(NULL, RegGPR, reg_hi);
        if (object) {
            VarInfo* retained = Registers_GetVarInfo(object);
            retained->rclass = RegGPR;
            bind_gpr_pair(retained, reg, reg_hi);
        }
    }
    info->rclass = RegGPR;
    if (reg > 0 && reg_hi > 0)
        bind_gpr_pair(info, reg, reg_hi);
    else
        CError_Internal(RC1_RegisterInfo_source_file, 586);
}

static inline int scalar_float_type(Type* type) {
    return SCALAR_FLOAT(type);
}

void assign_register_by_type(Object* object) {
    int initial_class;
    int class_check;
    Type* type = object->type;
    VarInfo* info = Registers_GetVarInfo(object);
    u8 needs_pair = 0;
    u8 rclass;
    info->reg = 0;
    info->reg_hi = 0;
    info->rclass = Registers_ClassForType(type);
    initial_class = info->rclass;

    if (initial_class == RegGPR) {
        if (IS_64BIT_INT(type) ||
            (!(RC1_use_paired_moves_for_double && scalar_float_type(type) && type->size != 4) &&
             RC1_soft_float && scalar_float_type(type) && type->size != 4)) {
            goto mark_pair;
        } else {
            const VarInfo* parameter = Registers_GetVarInfo(object);
            int cpu;
            /* Reuse the integer comparison temporary for the parameter-area check. */
            if (parameter && (parameter->flags & InParameterArea))
                class_check = 1;
            else
                class_check = 0;
            if (class_check) {
                cpu = RC1_processor;
                if (SMALL_VECTOR_CPU(cpu) && VECTOR(type) && type->size == 8 && gHasVarargs)
                    goto mark_pair;
            }
        }
    }
    goto dispatch;

mark_pair:
    needs_pair = 1;
dispatch:
    rclass = info->rclass;
    class_check = rclass;

    if (class_check < RegClassCount) {
        if (needs_pair) {
            if (class_check != RegGPR)
                CError_Internal(RC1_RegisterInfo_source_file, 539);
            if (optimizing || assignable_registers[info->rclass] > 1)
                assign_GPR_pair(object);
        } else {
            if (optimizing || assignable_registers[rclass] > 0)
                assign_register_to_variable(object, rclass);
        }
    }
}
