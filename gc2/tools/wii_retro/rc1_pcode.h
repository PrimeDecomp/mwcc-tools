#ifndef WII_RETRO_RC1_PCODE_H
#define WII_RETRO_RC1_PCODE_H

#include <stddef.h>
#include <stdint.h>

struct Alias;
struct Object;

enum {
    DirectRegister = 0,
    Immediate = 2,
    MemoryOperand = 4,
    FloatRegister = 3,
    GeneralRegister = 4,
    IsLoad = 0x2,
    IsStore = 0x4,
};

enum {
    FirstPairedSingleMemoryOpcode = 0x192,
    FirstPairedSingleStoreOpcode = 0x196,
    LastPairedSingleMemoryOpcode = 0x199,
};

#pragma pack(push, 2)
typedef struct OpcodeInfo {
    const char* name;
    const char* format;
    unsigned char operand_count;
    unsigned char rank;
    uint32_t flags;
    uint32_t extended_flags;
    unsigned char encoding[4];
} OpcodeInfo;

typedef struct PCodeArg {
    unsigned char kind;
    unsigned char reg_class;
    union {
        struct {
            uint16_t effect;
            uint16_t number;
        } reg;
        struct {
            uint32_t displacement;
            const struct Object* symbol;
        } immediate;
        struct {
            int32_t offset;
            const struct Object* object;
        } memory;
        struct PCodeLabel* label;
        unsigned char storage[12];
    } value;
} PCodeArg;

typedef struct PCode {
    struct PCode* next;
    struct PCode* previous;
    struct PCodeBlock* block;
    uint32_t flags;
    uint32_t extended_flags;
    unsigned char use_state[4];
    int definition_index;
    struct Alias* alias;
    const void* source_location;
    unsigned char source_state[4];
    uint16_t opcode;
    int16_t arg_count;
    PCodeArg args[];
} PCode;

typedef struct PCodeBlock {
    struct PCodeBlock* next;
    unsigned char header[0x10];
    PCode* first;
    PCode* last;
    int index;
    unsigned char attributes[8];
    short instruction_count;
    unsigned short flags;
} PCodeBlock;
#pragma pack(pop)

_Static_assert(sizeof(OpcodeInfo) == 22, "RC1 opcode table stride");
_Static_assert(offsetof(OpcodeInfo, flags) == 10, "RC1 opcode table flags");
_Static_assert(sizeof(PCodeArg) == 14, "RC1 PCode operand stride");
_Static_assert(offsetof(PCodeArg, value.reg.number) == 4, "RC1 register operand number");
_Static_assert(offsetof(PCodeArg, value.immediate.displacement) == 2, "RC1 immediate operand value");
_Static_assert(offsetof(PCodeArg, value.memory.object) == 6, "RC1 memory operand object");
_Static_assert(offsetof(PCode, block) == 0x8, "RC1 instruction block");
_Static_assert(offsetof(PCode, extended_flags) == 0x10, "RC1 extended PCode flags");
_Static_assert(offsetof(PCode, definition_index) == 0x18, "RC1 PCode definition index");
_Static_assert(offsetof(PCode, alias) == 0x1c, "RC1 PCode alias");
_Static_assert(offsetof(PCode, opcode) == 0x28, "RC1 PCode opcode");
_Static_assert(offsetof(PCode, args) == 0x2c, "RC1 PCode operands");
_Static_assert(offsetof(PCodeBlock, first) == 0x14, "RC1 first block instruction");
_Static_assert(offsetof(PCodeBlock, index) == 0x1c, "RC1 block index");
_Static_assert(offsetof(PCodeBlock, instruction_count) == 0x28, "RC1 block instruction count");
_Static_assert(offsetof(PCodeBlock, flags) == 0x2a, "RC1 scheduler block flags");

extern PCodeBlock* pcbasicblocks;
extern const OpcodeInfo opcodeinfo[];
int nbytes_loaded_or_stored_by(const PCode* instruction);

#endif
