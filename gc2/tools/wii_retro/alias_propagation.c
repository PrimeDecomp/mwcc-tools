/* Wii 1.0RC1 instruction aliases and reaching register definitions. */
#include "rc1_alias.h"
#include "rc1_local.h"
#include "rc1_pcode.h"

enum {
    IsCall = 0x8,
    IsPtrOp = 0x20,
    ReadsMemory = 0x20000,
    WritesMemory = 0x40000,
    UpdatesPointer = 0x8000,
    ConstFunction = 0x8000,
    Read = 1,
    Write = 2,
    PC_BL = 0x1,
    PC_LI = 0x89,
    PC_MR = 0x8b,
};

#pragma pack(push, 2)
typedef struct FunctionType {
    Type type;
    unsigned char attributes[0x1d];
    uint16_t flags;
} FunctionType;

typedef struct InstructionDefinition {
    PCode* instruction;
    unsigned char kind;
    signed char reg_class;
    short reg;
    unsigned char attributes[2];
} InstructionDefinition;

typedef struct DefinitionLink {
    struct DefinitionLink* next;
    int index;
} DefinitionLink;

typedef struct BlockUseDefInfo {
    unsigned char header[0x18];
    uint32_t* incoming_definitions;
    unsigned char attributes[0x14];
} BlockUseDefInfo;
#pragma pack(pop)

_Static_assert(offsetof(FunctionType, flags) == 0x1e, "RC1 function flags");
_Static_assert(offsetof(Object, type) == 0x10, "RC1 object type");
_Static_assert(sizeof(InstructionDefinition) == 10, "RC1 definition stride");
_Static_assert(offsetof(InstructionDefinition, reg) == 6, "RC1 definition register");
_Static_assert(sizeof(BlockUseDefInfo) == 48, "RC1 block use-def stride");
_Static_assert(offsetof(BlockUseDefInfo, incoming_definitions) == 0x18, "RC1 incoming definitions");

struct PropagationSettings;
extern const struct PropagationSettings RC1_alias_propagation_settings;
extern struct Object* gFunction;
extern int coloring;
extern signed char RC1_optimization_level;
extern unsigned char RC1_c99, RC1_cplusplus, RC1_indirect_aliasing;
extern unsigned char gathered_aliases_before_coloring;
extern Alias *const_func_alias, *worst_case_func;
extern BlockUseDefInfo* usedefinfo;
extern int number_of_Defs;
extern InstructionDefinition* Defs;
extern DefinitionLink** reg_Defs[];

extern void propagateinstructions(struct Object*, const struct PropagationSettings*, int, int);
extern void add_aliased_locals_to_worst_case(Alias* alias);
extern Alias* indirect_alias_set(Alias*);
extern Alias* create_alias_union(Alias*, Alias*);
extern void* oalloc(unsigned size);
extern void freeoheap(void);
extern void bitvectorcopy(uint32_t* destination, const uint32_t* source, int count);

int is_const_alias_runtime_call(const PCode* instruction) {
    if (instruction->opcode == PC_BL && instruction->arg_count > 0 &&
        instruction->args[0].kind == MemoryOperand) {
        const Object* function = instruction->args[0].value.memory.object;
        if (function && function->type && (((const FunctionType*)function->type)->flags & ConstFunction))
            return 1;
    }
    return 0;
}

static int precedes(const PCode* candidate, const PCode* use) {
    for (candidate = candidate->next; candidate; candidate = candidate->next) {
        if (candidate == use)
            return 1;
    }
    return 0;
}

int canidate_reaches_use(const PCode* definition, int definition_index, int reg,
                        const PCode* use, const uint32_t* reaching, unsigned char reg_class) {
    const PCode* instruction;
    if (definition->block == use->block && precedes(definition, use)) {
        instruction = definition->next;
    } else {
        if (!(reaching[definition_index >> 5] & (1u << (definition_index & 31))))
            return 0;
        instruction = use->block->first;
    }
    for (; instruction && instruction != use; instruction = instruction->next) {
        for (int index = 0; index < instruction->arg_count; ++index) {
            const PCodeArg* operand = &instruction->args[index];
            if (operand->kind == DirectRegister && operand->reg_class == reg_class &&
                (operand->value.reg.effect & Write) && (int16_t)operand->value.reg.number == reg)
                return 0;
        }
    }
    return 1;
}

static int indirect_aliasing_enabled(void) {
    return (RC1_c99 || RC1_cplusplus) && RC1_indirect_aliasing;
}

static int defines_word_in_gpr(const PCode* instruction) {
    return nbytes_loaded_or_stored_by(instruction) == 4 &&
           instruction->args[0].kind == DirectRegister && instruction->args[0].reg_class == GeneralRegister;
}

Alias* candidatealias(PCode* instruction) {
    if (instruction->opcode == PC_LI) {
        if (instruction->args[1].kind != MemoryOperand)
            return NULL;
        if (!instruction->alias)
            instruction->alias = make_alias(instruction->args[1].value.memory.object,
                                            instruction->args[1].value.memory.offset, 1);
        return instruction->alias;
    }
    if (instruction->arg_count < 3 && instruction->opcode != PC_MR)
        return NULL;

    uint32_t flags = instruction->flags;
    if (instruction->arg_count > 2 && instruction->args[2].kind == MemoryOperand) {
        const Object* object = instruction->args[2].value.memory.object;
        int32_t offset = instruction->args[2].value.memory.offset;
        if (flags & (IsLoad | IsStore)) {
            if (instruction->extended_flags & UpdatesPointer) {
                instruction->alias = make_alias(object, offset, nbytes_loaded_or_stored_by(instruction));
                return make_alias(object, offset, 1);
            }
            if (indirect_aliasing_enabled() && nbytes_loaded_or_stored_by(instruction) == 4 &&
                object->type->kind == TypePointer &&
                instruction->args[0].kind == DirectRegister && instruction->args[0].reg_class == GeneralRegister)
                return indirect_alias_set(make_alias(object, 0, 0));
            return NULL;
        }
        if (!instruction->alias)
            instruction->alias = make_alias(object, offset, 1);
        return instruction->alias;
    }
    if (flags & (IsLoad | IsStore)) {
        if (instruction->extended_flags & UpdatesPointer) {
            Alias* alias = instruction->alias;
            if (alias && alias->kind == AliasObjectPart)
                instruction->alias = make_alias(alias->object, 0, 0);
        } else if (indirect_aliasing_enabled() && (flags & IsLoad) && defines_word_in_gpr(instruction)) {
            return indirect_alias_set(instruction->alias);
        }
        return NULL;
    }
    return instruction->alias;
}

Alias* RC1_ReachingOperandAlias(const PCode* instruction, int index, const uint32_t* reaching) {
    const PCodeArg* operand = &instruction->args[index];
    Alias* current = NULL;
    int definition_count = 0;
    for (const DefinitionLink* link = reg_Defs[(signed char)operand->reg_class][(short)operand->value.reg.number];
         link; link = link->next) {
        PCode* definition = Defs[link->index].instruction;
        if (!canidate_reaches_use(definition, link->index, (short)operand->value.reg.number,
                                instruction, reaching, operand->reg_class))
            continue;
        Alias* alias = candidatealias(definition);
        ++definition_count;
        if (alias) {
            if (definition_count == 1)
                current = alias;
            else
                current = create_alias_union(current ? current : worst_case_ptr, alias);
        } else if (current && current != worst_case_ptr) {
            current = create_alias_union(worst_case_ptr, current);
        }
    }
    return current;
}

void RC1_GatherInstructionAlias(PCode* instruction, const uint32_t* reaching) {
    if (!instruction->alias) {
        instruction->alias = worst_case_ptr;
    } else if (instruction->alias != worst_case_ptr) {
        if (instruction->arg_count < 3 || instruction->args[2].kind != MemoryOperand) {
            Alias* base_alias = NULL;
            for (int index = 1; index < instruction->arg_count; ++index) {
                const PCodeArg* operand = &instruction->args[index];
                if (operand->kind == DirectRegister && operand->reg_class == GeneralRegister &&
                    (operand->value.reg.effect & Read)) {
                    Alias* alias = RC1_ReachingOperandAlias(instruction, index, reaching);
                    if (alias)
                        base_alias = base_alias ? create_alias_union(base_alias, alias) : alias;
                }
            }
            if (base_alias == worst_case_ptr) {
                instruction->flags |= IsPtrOp;
                instruction->alias = worst_case_ptr;
                add_alias_member(worst_case_ptr, instruction->alias);
            }
        }
        if ((instruction->flags & (IsLoad | IsStore)) && (instruction->extended_flags & UpdatesPointer) &&
            instruction->alias && instruction->alias->kind == AliasObjectPart)
            instruction->alias = make_alias(instruction->alias->object, 0, 0);
    }

    if (instruction->alias->kind <= AliasObjectPart) {
        int width = nbytes_loaded_or_stored_by(instruction);
        if (instruction->alias->size == width) {
            instruction->flags &= ~IsPtrOp;
            return;
        }
    }
    instruction->flags |= IsPtrOp;
}

void propagatealiasinfo(void) {
    if (coloring && RC1_optimization_level > 0) {
        propagateinstructions(gFunction, &RC1_alias_propagation_settings, 4, 1);
        uint32_t* reaching = oalloc(sizeof(uint32_t) * ((number_of_Defs + 31) >> 5));
        for (PCodeBlock* block = pcbasicblocks; block; block = block->next) {
            bitvectorcopy(reaching, usedefinfo[block->index].incoming_definitions, number_of_Defs);
            for (PCode* instruction = block->first; instruction; instruction = instruction->next) {
                if (instruction->flags & (IsLoad | IsStore))
                    RC1_GatherInstructionAlias(instruction, reaching);
                else if ((instruction->flags & IsCall) && !instruction->alias)
                    instruction->alias = is_const_alias_runtime_call(instruction) ? const_func_alias : worst_case_func;

                int index = instruction->definition_index;
                const InstructionDefinition* definition = &Defs[index];
                for (; index < number_of_Defs && definition->instruction == instruction; ++index, ++definition) {
                    if (definition->kind == DirectRegister && definition->reg_class == GeneralRegister) {
                        for (const DefinitionLink* link = reg_Defs[definition->reg_class][definition->reg];
                             link; link = link->next)
                            reaching[link->index >> 5] &= ~(1u << (link->index & 31));
                    }
                    reaching[index >> 5] |= 1u << (index & 31);
                }
            }
        }
        freeoheap();
        gathered_aliases_before_coloring = 1;
    } else {
        for (PCodeBlock* block = pcbasicblocks; block; block = block->next) {
            for (PCode* instruction = block->first; instruction; instruction = instruction->next) {
                if (instruction->alias) {
                    if (!gathered_aliases_before_coloring)
                        add_aliased_locals_to_worst_case(instruction->alias);
                } else if (instruction->flags & (IsLoad | IsStore | ReadsMemory | WritesMemory)) {
                    instruction->alias = worst_case_ptr;
                } else if (instruction->flags & IsCall) {
                    instruction->alias = is_const_alias_runtime_call(instruction) ? const_func_alias : worst_case_func;
                }
            }
        }
    }
}
