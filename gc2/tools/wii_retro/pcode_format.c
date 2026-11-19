/* Wii 1.0RC1 PCode operand formatting. */
#include "rc1_pcode.h"

#include <stdarg.h>

#ifndef WII_RETRO_KEEP_GQR0_SIDE_EFFECTS
#define WII_RETRO_KEEP_GQR0_SIDE_EFFECTS 0
#endif

enum {
    SystemRegister = 1,
    LabelOperand = 6,
    Placeholder = 8,
    SPR = 0,
    ConditionField = 1,
    VectorRegister = 2,
    Read = 1,
    Write = 2,
    OddRegister = 0x800,
    EvenRegister = 0x1000,
    Single = 0x2000,
    Pair0 = 0x4000,
    Pair1 = 0x8000,
    RefLocal16 = 1,
    RefAddress16 = 2,
    RefBranch = 4,
    RefLow = 6,
    RefHighAdjusted = 8,
    RefLocalHighAdjusted = 12,
    RefLocalLow = 13,
    RefPooled = 14,
    RefLocal12 = 16,
};

enum {
    IsBranch = 0x1,
    IsCall = 0x8,
    IsMove = 0x10,
    IsPointerOp = 0x20,
    IsConst = 0x40,
    IsVolatile = 0x80,
    HasSideEffects = 0x100,
    Commutative = 0x2000,
    CommonSubexpression = 0x4000,
    ReadsMemoryOperand = 0x20000,
    WritesMemoryOperand = 0x40000,
    Uses12BitOffset = 0x800,
    CanSetRecordBit = 0x20000,
};

enum {
    GekkoProcessor = 0x16,
    AddImmediate = 0x3f,
    MoveToConditionFields = 0x7a,
    FirstPairedSingleMemoryOpcode = 0x192,
    LastPairedSingleMemoryOpcode = 0x199,
    GQR0 = 0x390,
    DataObjectKind = 5,
    LocalAddress = 1,
    AbsoluteAddress = 2,
    PointerType = 0xc,
    ReferenceType = 0xd,
};

#pragma pack(push, 2)
typedef struct {
    unsigned char kind;
    unsigned char header[9];
    uint32_t target_qualifiers;
} OperandType;

struct Object {
    unsigned char kind;
    unsigned char storage;
    unsigned char address_kind;
    unsigned char header[13];
    const OperandType* type;
    uint32_t qualifiers;
    unsigned char metadata[40];
    uint32_t absolute_address;
};
#pragma pack(pop)

_Static_assert(offsetof(OperandType, target_qualifiers) == 0xa, "RC1 pointer qualifiers");
_Static_assert(offsetof(struct Object, type) == 0x10, "RC1 object type");
_Static_assert(offsetof(struct Object, absolute_address) == 0x40, "RC1 absolute address");

extern short RC1_processor;
extern unsigned char RC1_spe_register_pairs;
extern unsigned char pcode_has_vectors, pcode_has_float_single;
extern const short spr_to_sysreg[6];
extern const char RC1_PCodeInfo_source_file[];
extern void* lalloc(unsigned int);
extern void memclrw(void*, unsigned int);
extern void CError_Internal(const char*, int);
extern int pcode_const_from_format(const char*, int*);
extern int pcode_check_imm_bits(int, int, char);
extern struct Alias* make_alias(const struct Object*, int, int);
extern unsigned char is_volatile_object(const struct Object*);
extern unsigned char local_is_12bit_offset(const struct Object*);
extern unsigned char local_is_16bit_offset(const struct Object*);
extern unsigned char is_16bitdata(const struct Object*);
extern unsigned char is_pooled_object(const struct Object*);

#define FORMAT_ERROR(line) CError_Internal(RC1_PCodeInfo_source_file, (line))

void pcsetsideeffects(PCode* instruction) {
    instruction->flags = (instruction->flags & ~(IsMove | Commutative | CommonSubexpression)) | HasSideEffects;
}

static void register_operand(PCodeArg* arg, unsigned char kind, unsigned char reg_class, int reg, int effect) {
    arg->kind = kind;
    arg->reg_class = reg_class;
    arg->value.reg.number = reg;
    arg->value.reg.effect = effect;
}

static void system_register(PCode* instruction, PCodeArg* arg, int spr, int effect, int from_format) {
    for (int i = 0; i < 6; ++i) {
        if (spr == spr_to_sysreg[i]) {
            register_operand(arg, DirectRegister, SPR, i, effect);
            if ((effect & Write) && spr == 0x100)
                pcsetsideeffects(instruction);
            return;
        }
    }
    if (from_format || WII_RETRO_KEEP_GQR0_SIDE_EFFECTS || RC1_processor != GekkoProcessor ||
        instruction->opcode < FirstPairedSingleMemoryOpcode || instruction->opcode > LastPairedSingleMemoryOpcode ||
        spr != GQR0)
        pcsetsideeffects(instruction);
    register_operand(arg, SystemRegister, SPR, spr, effect);
}

static uint32_t target_qualifiers(const struct Object* object) {
    const OperandType* type = object->type;
    if (type->kind == PointerType || type->kind == ReferenceType)
        return type->target_qualifiers;
    return object->qualifiers;
}

static unsigned char low_reference(const PCode* instruction, const struct Object* object) {
    if (instruction->flags & (IsBranch | IsCall))
        return RefBranch;
    if (object->address_kind == LocalAddress) {
        if (!(instruction->extended_flags & Uses12BitOffset))
            return local_is_16bit_offset(object) ? RefLocal16 : RefLocalLow;
        if (RC1_processor == GekkoProcessor &&
            ((instruction->opcode >= 0x192 && instruction->opcode <= 0x193) ||
             (instruction->opcode >= 0x196 && instruction->opcode <= 0x197)) &&
            local_is_12bit_offset(object))
            return RefLocal12;
        FORMAT_ERROR(153);
        return 0;
    }
    if (is_16bitdata(object))
        return RefAddress16;
    if (is_pooled_object(object))
        return RefPooled;
    return RefLow;
}

static void memory_operand(PCode* instruction, PCodeArg* arg, const struct Object* object, int offset, int high) {
    if (!object) {
        arg->kind = Immediate;
        arg->value.immediate.displacement = offset;
        if (instruction->flags & (IsLoad | IsStore))
            instruction->flags |= IsPointerOp;
        return;
    }
    if (object->kind != DataObjectKind)
        FORMAT_ERROR(high ? 861 : 786);
    if (object->address_kind == AbsoluteAddress) {
        uint32_t absolute = object->absolute_address + offset;
        arg->kind = Immediate;
        arg->value.immediate.displacement = high ? absolute : (int16_t)absolute;
        return;
    }

    arg->kind = MemoryOperand;
    arg->value.memory.object = object;
    arg->value.memory.offset = offset;
    if (!high) {
        if (instruction->flags & (IsLoad | IsStore | ReadsMemoryOperand | WritesMemoryOperand)) {
            instruction->alias = make_alias(object, offset, nbytes_loaded_or_stored_by(instruction));
            if (is_volatile_object(object))
                instruction->flags |= IsVolatile;
            if (target_qualifiers(object) & 1)
                instruction->flags |= IsConst;
        } else if (instruction->opcode == AddImmediate) {
            instruction->alias = make_alias(object, offset, 1);
        }
    }
    if (instruction->flags & (IsLoad | IsStore)) {
        uint32_t qualifiers = target_qualifiers(object);
        if (qualifiers & 1)
            instruction->flags |= IsConst;
        if (qualifiers & 2)
            instruction->flags |= IsVolatile;
    }
    if (high)
        arg->reg_class = object->address_kind == LocalAddress ? RefLocalHighAdjusted : RefHighAdjusted;
    else
        arg->reg_class = low_reference(instruction, object);
}

static int decimal_digit(char c) {
    return c >= '0' && c <= '9';
}

static int format_number(const char** format) {
    int value;
    *format += pcode_const_from_format(*format + 1, &value);
    return value;
}

static int register_effect(const char** format) {
    if (**format == '=') {
        ++*format;
        return Write;
    }
    if (**format == '+') {
        ++*format;
        return Read | Write;
    }
    return Read;
}

static void checked_immediate(PCodeArg* arg, int value, int bits, char kind, int line) {
    arg->kind = Immediate;
    arg->value.immediate.displacement = value;
    if (pcode_check_imm_bits(value, bits, kind))
        FORMAT_ERROR(line);
}

PCode* vformatpcode(short opcode, va_list args) {
    const OpcodeInfo* info = &opcodeinfo[opcode];
    const char* format = info->format;
    int count = info->operand_count;
    int registers = 0;
    if (*format == '#') {
        registers = va_arg(args, int);
        count += registers;
        ++format;
    }
    int slots = count + ((info->extended_flags & CanSetRecordBit) != 0);
    if (slots < 6)
        slots = 6;
    unsigned int size = offsetof(PCode, args) + slots * sizeof(PCodeArg);
    PCode* instruction = lalloc(size);
    memclrw(instruction, size);
    instruction->opcode = opcode;
    instruction->arg_count = count;
    instruction->flags = info->flags;
    instruction->extended_flags = info->extended_flags;

    int index = 0;
    while (*format) {
        if (index >= count)
            FORMAT_ERROR(323);
        if (*format == ',' || *format == ';')
            ++format;
        if (*format == '[' || *format == '(')
            ++format;

        int effect = register_effect(&format);
        switch (*format) {
        case '^':
            effect |= Pair1;
            pcode_has_vectors = 1;
            ++format;
            break;
        case ':':
            effect |= Pair0 | Pair1;
            pcode_has_vectors = 1;
            ++format;
            break;
        case '.':
            effect |= Pair0;
            ++format;
            break;
        case '`':
            if (RC1_processor == GekkoProcessor) {
                effect |= Single;
                pcode_has_float_single = 1;
            }
            ++format;
            break;
        }
        if (*format == '-')
            ++format;
        if (*format == '?')
            ++format;
        if (*format == '!')
            ++format;

        int value = -1;
        if (*format == '$') {
            ++format;
            value = format_number(&format);
        }
        PCodeArg* arg = &instruction->args[index];
        char kind = *format;
        switch (kind) {
        case 'b':
        case 'r':
        case 'f':
        case 'v':
        case 'z':
        case 'c':
        case 'e':
        case 'j': {
            if (kind == 'z' && (int16_t)arg->value.reg.number >= 4 && (int16_t)arg->value.reg.number <= 7)
                FORMAT_ERROR(430);
            if (value == -1)
                value = va_arg(args, int);
            unsigned char reg_class = GeneralRegister;
            if (kind == 'f')
                reg_class = FloatRegister;
            else if (kind == 'v')
                reg_class = VectorRegister;
            else if (kind == 'c' || kind == 'z')
                reg_class = ConditionField;
            if (kind == 'b' && value == 0)
                effect = 0;
            if (kind == 'e')
                effect += EvenRegister;
            else if (kind == 'j')
                effect += OddRegister;
            register_operand(arg, DirectRegister, reg_class, value, effect);
            int16_t number = arg->value.reg.number;
            if (kind == 'e' && number >= 8 && number <= 23)
                FORMAT_ERROR(662);
            if (kind == 'j' && (number < 8 || (number > 23 && number < 32)))
                FORMAT_ERROR(672);
            break;
        }
        case 'X':
        case 'L':
        case 'C':
            register_operand(arg, DirectRegister, SPR, kind == 'X' ? 0 : kind == 'L' ? 1 : 2, effect);
            break;
        case 'Z':
            register_operand(arg, DirectRegister, ConditionField, 0, effect);
            break;
        case 'V':
            for (int i = 32 - registers; i < 32; ++i)
                register_operand(&instruction->args[index++], DirectRegister, GeneralRegister, i, effect);
            --index;
            break;
        case 'Y': {
            int mask = opcode == MoveToConditionFields ? instruction->args[0].value.immediate.displacement : 0xff;
            for (int i = 0; i < 8; ++i) {
                if (mask & (0x80 >> i))
                    register_operand(&instruction->args[index++], DirectRegister, ConditionField, i,
                                     opcode == MoveToConditionFields ? Write : Read);
            }
            --index;
            break;
        }
        case 'P':
            if (decimal_digit(format[1]))
                format_number(&format);
            else
                FORMAT_ERROR(486);
            /* The bit count is encoding information. The operand is a system register. */
            /* fall through */
        case 's':
            if (value == -1)
                value = va_arg(args, int);
            system_register(instruction, arg, value, effect, 0);
            break;
        case 'S':
            value = 0;
            if (decimal_digit(format[1]))
                value = format_number(&format);
            else
                FORMAT_ERROR(543);
            system_register(instruction, arg, value, effect, 1);
            break;
        case 'T':
            if (value == -1)
                value = va_arg(args, int);
            if (value == 0x10c || value == 0x10d)
                register_operand(arg, DirectRegister, SPR, value + 0x10, effect);
            else
                FORMAT_ERROR(529);
            break;
        case '%': {
            ++format;
            int immediate = format_number(&format);
            if (format[1] == '(') {
                ++format;
                const char* base = format + 1;
                int access = register_effect(&base);
                format = base - 1;
                int reg = -1;
                if (format[1] == '$') {
                    ++format;
                    reg = format_number(&format);
                }
                if (format[1] == 'b') {
                    if (reg == -1)
                        reg = va_arg(args, int);
                    if (reg == 0)
                        access = 0;
                } else if (RC1_spe_register_pairs && format[1] == 'e') {
                    if (reg == -1)
                        reg = va_arg(args, int);
                    int16_t previous = arg->value.reg.number;
                    if (previous >= 8 && previous <= 23)
                        FORMAT_ERROR(601);
                    access |= EvenRegister;
                } else {
                    FORMAT_ERROR(607);
                }
                register_operand(arg, DirectRegister, GeneralRegister, reg, access);
                arg = &instruction->args[++index];
            }
            arg->kind = Immediate;
            arg->value.immediate.displacement = immediate;
            break;
        }
        case 'a':
        case 'i':
        case 'n':
        case 'o':
        case 'u':
        case 'w':
        case 'x':
        case 'k':
        case 'K':
        case 'U': {
            int bits = kind == 'k' || kind == 'K' ? 32 : 16;
            if (kind == 'o')
                kind = 'x';
            if (kind == 'a') {
                if (decimal_digit(format[1]))
                    kind = *++format;
                else
                    FORMAT_ERROR(637);
            }
            if (decimal_digit(format[1]))
                bits = format_number(&format);
            checked_immediate(arg, va_arg(args, int), bits, kind, 650);
            break;
        }
        case 'N':
            checked_immediate(arg, va_arg(args, int), 6, 'u', 681);
            break;
        case 'D':
        case 'R':
            checked_immediate(arg, va_arg(args, int), 10, 'u', 690);
            break;
        case 't':
            checked_immediate(arg, va_arg(args, int), 5, 'u', 698);
            break;
        case 'Q':
        case 'q':
            if (value == -1)
                value = va_arg(args, int);
            register_operand(arg, DirectRegister, ConditionField, value, effect);
            arg = &instruction->args[++index];
            /* fall through */
        case 'B':
            checked_immediate(arg, va_arg(args, int), 4, 'u', 716);
            break;
        case 'l': {
            struct PCodeLabel* label = va_arg(args, struct PCodeLabel*);
            if (label) {
                arg->kind = LabelOperand;
                arg->value.label = label;
            } else {
                const struct Object* object = va_arg(args, const struct Object*);
                if (object->kind != DataObjectKind)
                    FORMAT_ERROR(729);
                arg->kind = MemoryOperand;
                arg->reg_class = RefBranch;
                arg->value.memory.object = object;
                arg->value.memory.offset = 0;
            }
            break;
        }
        case 'd':
        case 'h': {
            if (format[1] != '(')
                FORMAT_ERROR(742);
            format += 2;
            int access = register_effect(&format);
            if (value == -1)
                value = va_arg(args, int);
            if (RC1_spe_register_pairs && *format == 'e') {
                access |= EvenRegister;
                int16_t number = value;
                if (number >= 8 && number <= 23)
                    FORMAT_ERROR(761);
            } else {
                if (*format != 'b')
                    FORMAT_ERROR(768);
                if (value == 0)
                    access = 0;
            }
            register_operand(arg, DirectRegister, GeneralRegister, value, access);
            arg = &instruction->args[++index];
            /* Displacement addressing emits the base before the low memory operand. */
            const struct Object* object = va_arg(args, const struct Object*);
            int offset = va_arg(args, int);
            memory_operand(instruction, arg, object, offset, 0);
            break;
        }
        case 'm':
        case 'M': {
            const struct Object* object = va_arg(args, const struct Object*);
            int offset = va_arg(args, int);
            memory_operand(instruction, arg, object, offset, kind == 'M');
            break;
        }
        case 'p':
            arg->kind = Placeholder;
            break;
        case 'O':
            --index;
            break;
        default:
            FORMAT_ERROR(910);
            break;
        }

        for (;;) {
            char suffix = format[1];
            if (suffix == '/') {
                ++format;
                if (format[1] == '2')
                    ++format;
            } else if (suffix == '@' || suffix == '<' || suffix == '>' || suffix == '|' || suffix == '*') {
                ++format;
                if (format[1] == 'p')
                    ++format;
                else if (decimal_digit(format[1]))
                    format_number(&format);
                else
                    FORMAT_ERROR(941);
            } else {
                break;
            }
        }
        ++format;
        if (*format == ']' || *format == ')')
            ++format;
        ++index;
    }
    while (index < slots)
        instruction->args[index++].kind = Placeholder;
    return instruction;
}
