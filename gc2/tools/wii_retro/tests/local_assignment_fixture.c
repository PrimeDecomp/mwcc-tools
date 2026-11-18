#include "rc1_local.h"

__declspec(dllimport) void* __stdcall VirtualAlloc(void*, unsigned, unsigned, unsigned);
__declspec(dllimport) int __stdcall VirtualProtect(void*, unsigned, unsigned, unsigned*);
__declspec(dllimport) __declspec(noreturn) void __stdcall ExitProcess(unsigned);

enum { HasMemory = 0x80, Entries = 16, Objects = Entries * 2, Events = 128 };
int optimizing;
Local *arguments, *RC1_locals, *toclist;
static Local entries[Entries];
static Object objects[Objects];
static Identifier names[Objects];
static PointerType types[Objects];
static VarInfo info[Objects];
static unsigned char mutate[Entries];
static unsigned char name_visits[Entries];
static unsigned event_count, allocations;
static unsigned events[Events];
static int* active_optimizing;

static void record(unsigned category, unsigned index) {
    if (event_count == Events)
        ExitProcess(44);
    events[event_count++] = category | index << 8 | (unsigned)*active_optimizing << 16;
}

VarInfo* Registers_GetVarInfo(Object* object) {
    unsigned index = object - objects;
    record(1, index);
    if (index >= 10 && index < Entries && mutate[index])
        entries[index].object = objects + index + Entries;
    return info + index;
}

unsigned char IsTempName(const Identifier* name) {
    unsigned index = name - names;
    record(2, index);
    if (index < Entries && mutate[index] && name_visits[index]++ == (mutate[index] >> 1))
        entries[index].object = objects + index + Entries;
    return name->header[0];
}

void assign_register_by_type(Object* object) {
    unsigned index = object - objects;
    record(3, index);
    info[index].reg = 55;
    ++allocations;
}

void RC1_AssignLocals(void);
void RC1_ReassignLocals(void);

/* Only these two passes are under test. Fail if another relink worker runs. */
struct Linear;
const struct Linear* RC1_first_linear;
unsigned char RC1_ipa_kind;
unsigned RC1_unique_name_counter;

Object* create_temp_object(const Type* type) { (void)type; ExitProcess(45); }
void* malloc(size_t size) { (void)size; ExitProcess(45); }
void* realloc(void* memory, size_t size) { (void)memory; (void)size; ExitProcess(45); }
void free(void* memory) { (void)memory; ExitProcess(45); }
void RC1_Linearize(void* statements) { (void)statements; ExitProcess(45); }
void RC1_CopyAndConstantPropagation(void) { ExitProcess(45); }
void* RC1_Delinearize(void* statements, void* options) {
    (void)statements;
    (void)options;
    ExitProcess(45);
}
void retro_AssignInlineLocals(Local* locals, void (*assign)(void)) {
    (void)locals;
    (void)assign;
    ExitProcess(45);
}

typedef struct {
    unsigned address;
    unsigned size;
    const unsigned char* body;
} NativePass;

typedef struct {
    NativePass passes[2];
    int* optimizing;
    Local **arguments, **locals, **toclist;
    unsigned get_info, is_temp, assign;
} NativeCompiler;

#include "local_assignment_native.h"

static void jump(unsigned start, const void* target) {
    *(unsigned char*)start = 0xe9;
    *(unsigned*)(start + 1) = (unsigned)target - start - 5;
}

static unsigned mix(unsigned x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    return x ^ (x >> 16);
}

static void prepare(unsigned seed) {
    const unsigned char kinds[] = {TypeInteger, TypePointer, TypeArray, TypeFloat};
    event_count = allocations = 0;
    for (unsigned i = 0; i != Objects; ++i) {
        unsigned random = mix(seed * Objects + i);
        if (i < Entries) {
            entries[i].next = i == 1 || i == 9 || i == 15 ? 0 : entries + i + 1;
            entries[i].object = objects + i;
            mutate[i] = (seed & 1) && (random & 8) ? 1 | ((random >> 5) & 2) : 0;
            name_visits[i] = 0;
        }
        objects[i].name = names + i;
        objects[i].type = &types[i].type;
        objects[i].qualifiers = (random >> 8) & QualVolatile;
        names[i].header[0] = (random >> 10) & 1;
        types[i].type.kind = kinds[(random >> 11) & 3];
        types[i].qualifiers = (random >> 12) & QualVolatile;
        info[i].no_register = (random >> 14) & 1;
        info[i].used = (random >> 16) & 1;
        info[i].usage = ((random >> 17) & 7) - 2;
        info[i].flags = (random >> 17) & HasMemory;
        info[i].reg = ((random >> 26) & 3) == 1 ? 9 : 0;
    }
    arguments = seed & 2 ? entries : 0;
    RC1_locals = seed & 4 ? entries + 2 : 0;
    toclist = seed & 8 ? entries + 10 : 0;
    optimizing = (seed >> 4) & 127;
}

void start(void) {
    void* const shadow = (void*)0x17400000;
    if (VirtualAlloc(shadow, 0x380000, 0x3000, 4) != shadow)
        ExitProcess(13);
    const unsigned versions = sizeof(native_compilers) / sizeof(native_compilers[0]);
    for (unsigned version = 0; version < versions; ++version) {
        const NativeCompiler* stock = &native_compilers[version];
        for (unsigned routine = 0; routine < 2; ++routine) {
            const NativePass* pass = &stock->passes[routine];
            unsigned char* dest = (void*)pass->address;
            for (unsigned i = 0; i < pass->size; ++i)
                dest[i] = pass->body[i];
        }
        jump(stock->get_info, Registers_GetVarInfo);
        jump(stock->is_temp, IsTempName);
        jump(stock->assign, assign_register_by_type);
    }
    unsigned old;
    if (!VirtualProtect((void*)0x17450000, 0x150000, 0x20, &old))
        ExitProcess(14);

    unsigned matches = 0;
    for (unsigned version = 0; version < versions; ++version) {
        const NativeCompiler* stock = &native_compilers[version];
        for (unsigned routine = 0; routine < 2; ++routine) {
            for (unsigned seed = 0; seed < 4096; ++seed) {
                unsigned expected[Events];
                short expected_registers[Objects];
                Object* expected_objects[Entries];
                prepare(seed);
                *stock->optimizing = optimizing;
                *stock->arguments = arguments;
                *stock->locals = RC1_locals;
                *stock->toclist = toclist;
                active_optimizing = stock->optimizing;
                ((void (*)(void))stock->passes[routine].address)();
                if (*active_optimizing != optimizing)
                    ExitProcess(15);
                unsigned count = event_count;
                matches += allocations;
                for (unsigned i = 0; i < count; ++i)
                    expected[i] = events[i];
                for (unsigned i = 0; i < Objects; ++i)
                    expected_registers[i] = info[i].reg;
                for (unsigned i = 0; i < Entries; ++i)
                    expected_objects[i] = entries[i].object;

                prepare(seed);
                active_optimizing = &optimizing;
                if (routine)
                    RC1_ReassignLocals();
                else
                    RC1_AssignLocals();
                if (*active_optimizing != *stock->optimizing)
                    ExitProcess(16);
                if (count != event_count)
                    ExitProcess(20 + version * 2 + routine);
                for (unsigned i = 0; i < count; ++i)
                    if (expected[i] != events[i])
                        ExitProcess(24 + version * 2 + routine);
                for (unsigned i = 0; i < Objects; ++i)
                    if (expected_registers[i] != info[i].reg)
                        ExitProcess(28 + version * 2 + routine);
                for (unsigned i = 0; i < Entries; ++i)
                    if (expected_objects[i] != entries[i].object)
                        ExitProcess(32 + version * 2 + routine);
            }
        }
    }
    ExitProcess(matches > 1000 * versions ? 0 : 40);
}
