#include "rc1_pcode.h"
#include <stdarg.h>

__declspec(dllimport) void* __stdcall VirtualAlloc(void*, unsigned, unsigned, unsigned);
__declspec(dllimport) int __stdcall VirtualProtect(void*, unsigned, unsigned, unsigned*);
__declspec(dllimport) __declspec(noreturn) void __stdcall ExitProcess(unsigned);

extern PCode* vformatpcode(short, va_list);

static struct { uint32_t words[0x44 / 4]; } objects;
static uint32_t label, operand_type[4];

#include "pcode_format_native.h"

short RC1_processor;
unsigned char RC1_spe_register_pairs, pcode_has_vectors, pcode_has_float_single;
const short spr_to_sysreg[] = {1, 8, 9, 0x100, 0x200, 0xFD0};
const char RC1_PCodeInfo_source_file[] = "PCodeInfo.c";

#define NATIVE_PROCESSOR (*(short*)0x74F270)
#define NATIVE_PAIRS (*(unsigned char*)0x74F269)
#define NATIVE_VECTOR (*(unsigned char*)0x76C0A5)
#define NATIVE_SINGLE (*(unsigned char*)0x76C23B)

enum { ArenaSize = 512 };
static unsigned char arena[ArenaSize];
static unsigned allocated, cleared, diagnostics, alias_calls;
static unsigned object_flags;
static int alias_offset, alias_width;

static void fill(void* dst, unsigned char byte, unsigned size) {
    for (unsigned i = 0; i < size; ++i)
        ((unsigned char*)dst)[i] = byte;
}

static void copy(void* dst, const void* src, unsigned size) {
    for (unsigned i = 0; i < size; ++i)
        ((unsigned char*)dst)[i] = ((const unsigned char*)src)[i];
}

void* lalloc(unsigned size) {
    if (allocated || size > sizeof(arena))
        ExitProcess(10);
    allocated = size;
    return arena;
}

void memclrw(void* dst, unsigned size) {
    if (dst != arena || size != allocated || cleared++)
        ExitProcess(11);
    fill(dst, 0, size);
}

void CError_Internal(const char* name, int line) {
    for (unsigned i = 0; i < sizeof(RC1_PCodeInfo_source_file); ++i) {
        if (name[i] != RC1_PCodeInfo_source_file[i])
            ExitProcess(12);
    }
    diagnostics = (diagnostics * 37) ^ line;
}

int nbytes_loaded_or_stored_by(const PCode* instruction) {
    return (instruction->flags & IsStore) ? 8 : 4;
}

struct Alias* make_alias(const struct Object* object, int offset, int width) {
    if ((const void*)object != &objects)
        ExitProcess(15);
    ++alias_calls;
    alias_offset = offset;
    alias_width = width;
    return (struct Alias*)0x1234;
}

unsigned char is_volatile_object(const struct Object* object) {
    return ((const void*)object == &objects) && (object_flags & 1);
}

unsigned char local_is_12bit_offset(const struct Object* object) {
    return ((const void*)object == &objects) && (object_flags & 2);
}

unsigned char local_is_16bit_offset(const struct Object* object) {
    return ((const void*)object == &objects) && (object_flags & 4);
}

unsigned char is_16bitdata(const struct Object* object) {
    return ((const void*)object == &objects) && (object_flags & 8);
}

unsigned char is_pooled_object(const struct Object* object) {
    return ((const void*)object == &objects) && (object_flags & 16);
}

int pcode_const_from_format(const char* format, int* value) {
    int (*native)(const char*, int*) = (void*)0x5AA600;
    return native(format, value);
}

int pcode_check_imm_bits(int value, int bits, char kind) {
    int (*native)(int, int, char) = (void*)0x5AA680;
    return native(value, bits, kind);
}

static int decimal(const char* value) {
    uint32_t result = 0;
    for (; *value >= '0' && *value <= '9'; ++value)
        result = result * 10 + (*value - '0');
    return result;
}

static const char* find_char(const char* text, int character) {
    for (; *text; ++text) {
        if (*text == character)
            return text;
    }
    return character == '\0' ? text : 0;
}

static uint16_t character_classes[256];
static void* locale[3];
static void* tls[0x1c0 / 4];

static void* thread_data(int allocate) {
    if (allocate != 1)
        ExitProcess(16);
    return tls;
}

static void hook(uintptr_t address, void* function) {
    uint32_t jump = (uintptr_t)function - (address + 5);
    *(unsigned char*)address = 0xe9;
    copy((void*)(address + 1), &jump, sizeof(jump));
}

static void install(void) {
    if (VirtualAlloc((void*)0x400000, 0x380000, 0x3000, 0x04) != (void*)0x400000)
        ExitProcess(13);
    for (unsigned i = 0; i < sizeof(native_segments) / sizeof(*native_segments); ++i)
        copy((void*)native_segments[i].address, native_segments[i].bytes, native_segments[i].size);
    for (unsigned i = '0'; i <= '9'; ++i)
        character_classes[i] = 8;
    locale[2] = character_classes;
    tls[0x1bc / 4] = locale;
    hook(0x403FF0, thread_data);
    hook(0x4041A0, find_char);
    hook(0x42DF70, decimal);
    hook(0x454350, is_volatile_object);
    hook(0x45B4C0, CError_Internal);
    hook(0x46E990, memclrw);
    hook(0x46EBF0, lalloc);
    hook(0x4C6C80, is_16bitdata);
    hook(0x59A560, is_pooled_object);
    hook(0x5A2560, local_is_12bit_offset);
    hook(0x5A2610, local_is_16bit_offset);
    hook(0x5A7620, nbytes_loaded_or_stored_by);
    hook(0x5BF250, make_alias);
    unsigned previous;
    if (!VirtualProtect((void*)0x400000, 0x1C0000, 0x20, &previous))
        ExitProcess(14);
}

static void reset(void) {
    fill(arena, 0xaa, sizeof(arena));
    allocated = cleared = diagnostics = alias_calls = 0;
    alias_offset = alias_width = 0;
    pcode_has_vectors = NATIVE_VECTOR = 0;
    pcode_has_float_single = NATIVE_SINGLE = 0;
}

static void setup_object(unsigned variant) {
    static const unsigned char kinds[] = {0, 1, 1, 2, 0, 0};
    static const unsigned char flags[] = {8, 2, 4, 0, 16, 1};
    fill(&objects, 0, sizeof(objects));
    fill(operand_type, 0, sizeof(operand_type));
    unsigned char* bytes = (unsigned char*)&objects;
    bytes[0] = 5;
    bytes[2] = kinds[variant];
    objects.words[4] = (uintptr_t)operand_type;
    objects.words[5] = variant & 3;
    objects.words[16] = 0x1234FF80;
    object_flags = flags[variant];
    ((unsigned char*)operand_type)[0] = variant >= 4 ? 0xc : 1;
    ((unsigned char*)operand_type)[0xa] = variant & 3;
}

void start(void) {
    install();
    PCode* (*native)(short, va_list) = (void*)0x5A8680;
    const short processors[] = {0x16, 4, 0x18};
    for (unsigned i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        short opcode = cases[i].opcode;
        copy((void*)(0x6E3D10 + opcode * sizeof(OpcodeInfo)), &opcodeinfo[opcode], sizeof(OpcodeInfo));
        for (unsigned cpu = 0; cpu < 3; ++cpu) {
            RC1_processor = NATIVE_PROCESSOR = processors[cpu];
            RC1_spe_register_pairs = NATIVE_PAIRS = (cpu == 2);
            for (unsigned object = 0; object < 6; ++object) {
                setup_object(object);
                reset();
                if ((void*)native(opcode, (va_list)cases[i].args) != arena)
                    ExitProcess(17);
                unsigned char expected[ArenaSize];
                copy(expected, arena, sizeof(expected));
                unsigned size = allocated, clear = cleared, issues = diagnostics, aliases = alias_calls;
                int offset = alias_offset, width = alias_width;
                unsigned char vector = NATIVE_VECTOR, single = NATIVE_SINGLE;
                reset();
                unsigned error = 0x100000 + (i << 8) + (cpu << 6) + (object << 3);
                if ((void*)vformatpcode(opcode, (va_list)cases[i].args) != arena)
                    ExitProcess(error + 1);
                if (size != allocated || clear != cleared || issues != diagnostics)
                    ExitProcess(error + 2);
                if (aliases != alias_calls || offset != alias_offset || width != alias_width)
                    ExitProcess(error + 3);
                if (vector != pcode_has_vectors || single != pcode_has_float_single)
                    ExitProcess(error + 4);
                for (unsigned j = 0; j < sizeof(expected); ++j) {
                    if (expected[j] != arena[j])
                        ExitProcess(error + 5);
                }
            }
        }
    }
    ExitProcess(0);
}
