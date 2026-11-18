#include "rc1_codegen.h"
#include "register_info_native.h"

__declspec(dllimport) void* __stdcall VirtualAlloc(void*, unsigned, unsigned, unsigned);
__declspec(dllimport) int __stdcall VirtualProtect(void*, unsigned, unsigned, unsigned*);
__declspec(dllimport) __declspec(noreturn) void __stdcall ExitProcess(unsigned);
extern void assign_register_by_type(Object* object);
extern void assign_register_to_variable(Object* object, signed char rclass);
extern void assign_GPR_pair(Object* object);
extern short obtain_nonvolatile_register(signed char rclass);
extern void retain_register(Object* object, signed char rclass, short reg);

const char RC1_RegisterInfo_source_file[] = "RegisterInfo.c";
const char RC1_Registers_source_file[] = "Registers.c";
u8 gHasVarargs, RC1_soft_float, RC1_use_paired_moves_for_double;
short RC1_processor;
int optimizing, assignable_registers[RegClassCount];
int used_virtual_registers[RegClassCount], used_nonvolatile_registers[RegClassCount];
int n_nonvolatile_registers[RegClassCount], nonvolatile_registers[RegClassCount * 32];
u8 reg_state[RegClassCount * 32];
static int allocations, cleared;
static unsigned diagnostics;
static VarInfo arena;

#define NativeAssignable ((int*)0x751b18)
#define NativeVirtual ((int*)0x758278)
#define NativeUsedNonvolatile ((int*)0x75804c)
#define NativeNonvolatileCount ((int*)0x750408)
#define NativeNonvolatile ((int*)0x74efd8)
#define NativeState ((u8*)0x74f588)

static void fill(void* destination, u8 value, unsigned size) {
    for (unsigned i = 0; i < size; ++i)
        ((u8*)destination)[i] = value;
}

void* galloc(u32 size) {
    if (size != sizeof(arena) || allocations++)
        ExitProcess(10);
    fill(&arena, 0xAA, size);
    return &arena;
}

void memclrw(void* destination, u32 size) {
    if (destination != &arena || size != sizeof(arena) || cleared++)
        ExitProcess(11);
    fill(destination, 0, size);
}

void CError_Internal(const char* source, int line) {
    const char* name = line == 168 ? RC1_Registers_source_file : RC1_RegisterInfo_source_file;
    for (unsigned i = 0; ; ++i) {
        if (source[i] != name[i])
            ExitProcess(12);
        if (!name[i])
            break;
    }
    diagnostics = (diagnostics << 10) | line;
}

static void reset(void) {
    allocations = cleared = diagnostics = 0;
}

static void hook(u32 address, void* function) {
    *(u8*)address = 0xe9;
    u32 delta = (u32)function - (address + 5);
    for (unsigned i = 0; i < 4; ++i)
        ((u8*)address)[i + 1] = delta >> (8 * i);
}

static void install(void) {
    if (VirtualAlloc((void*)0x400000, 0x380000, 0x3000, 0x04) != (void*)0x400000)
        ExitProcess(13);
    for (unsigned entry = 0; entry < sizeof(native_segments) / sizeof(*native_segments); ++entry) {
        const Segment* segment = &native_segments[entry];
        for (unsigned i = 0; i < segment->size; ++i)
            ((u8*)segment->address)[i] = segment->bytes[i];
    }
    hook(0x46ec30, galloc);
    hook(0x46e990, memclrw);
    hook(0x45b4c0, CError_Internal);
    unsigned previous;
    if (!VirtualProtect((void*)0x450000, 0x170000, 0x20, &previous))
        ExitProcess(14);
}

typedef struct {
    int allocations, cleared;
    unsigned diagnostics;
    VarInfo* result;
    u8 rclass, flags;
    short reg, reg_hi;
} Result;

static Result observe(VarInfo* result) {
    Result state;
    state.allocations = allocations;
    state.cleared = cleared;
    state.diagnostics = diagnostics;
    state.result = result;
    state.rclass = result ? result->rclass : 0;
    state.flags = result ? result->flags : 0;
    state.reg = result ? result->reg : 0;
    state.reg_hi = result ? result->reg_hi : 0;
    return state;
}

static int matches(Result a, Result b) {
    return a.allocations == b.allocations && a.cleared == b.cleared && a.diagnostics == b.diagnostics &&
           a.result == b.result && a.rclass == b.rclass &&
           a.flags == b.flags && a.reg == b.reg && a.reg_hi == b.reg_hi;
}

static void setup_bank(int available, int first_virtual, unsigned cursor, unsigned limit, unsigned mask) {
    for (int c = 0; c < RegClassCount; ++c) {
        assignable_registers[c] = NativeAssignable[c] = available;
        used_virtual_registers[c] = NativeVirtual[c] = first_virtual;
        used_nonvolatile_registers[c] = NativeUsedNonvolatile[c] = cursor;
        n_nonvolatile_registers[c] = NativeNonvolatileCount[c] = limit;
        for (int r = 0; r < 32; ++r) {
            int offset = 32 * c + r;
            nonvolatile_registers[offset] = NativeNonvolatile[offset] = 31 - r;
            reg_state[offset] = NativeState[offset] = (mask >> (31 - r)) & 1 ? 2 : 0;
        }
    }
}

static int matches_bank(void) {
    for (int c = 0; c < RegClassCount; ++c) {
        if (assignable_registers[c] != NativeAssignable[c] || used_virtual_registers[c] != NativeVirtual[c] ||
            used_nonvolatile_registers[c] != NativeUsedNonvolatile[c] ||
            n_nonvolatile_registers[c] != NativeNonvolatileCount[c])
            return 0;
    }
    for (unsigned i = 0; i < sizeof(reg_state); ++i)
        if (reg_state[i] != NativeState[i] || nonvolatile_registers[i] != NativeNonvolatile[i])
            return 0;
    return 1;
}

static void init_object(Object* object, u8 kind, Type* type, VarInfo* info, int cached, u8 flags) {
    fill(object, 0, sizeof(*object));
    fill(info, 0xAB, sizeof(*info));
    info->flags = flags;
    object->address_kind = kind;
    object->type = type;
    if (cached)
        object->local_info = object->data_info = info;
    reset();
}

static void check_getter(void) {
    VarInfo* (*native_get)(Object*) = (void*)0x590780;
    Object object;
    VarInfo info;
    for (unsigned kind = 0; kind < 256; ++kind) {
        for (int cached = 0; cached <= 1; ++cached) {
            init_object(&object, kind, 0, &info, cached, 1);
            Result expected = observe(native_get(&object));
            init_object(&object, kind, 0, &info, cached, 1);
            VarInfo* first = Registers_GetVarInfo(&object);
            if (!matches(expected, observe(first)))
                ExitProcess(20);
            if (first && (first != Registers_GetVarInfo(&object) || allocations != expected.allocations))
                ExitProcess(21);
        }
    }
}

static void check_alloc(void) {
    static const short processors[] = {0, 22, 23, 28, 38, 39};
    static const int sizes[] = {0, 1, 4, 8, 16};
    static const u8 subtypes[] = {0, 3, 4, 14, 15, 21, 22, 23, 24, 25, 26, 128, 255};
    u8 (*native_class)(Type*) = (void*)0x590530;
    void (*native_assign)(Object*) = (void*)0x590a60;
    Object object;
    VarInfo info;
    Type target;
    union { Type type; TypeIntegral integr; TypeStruct struc; TypeMemberPointer member; } input;
    /* Stock may read the vector subtype byte when classifying a float. */
    fill(&input, 0, sizeof(input));

    for (unsigned kind = 0; kind < 256; ++kind)
        for (unsigned t = 0; t < sizeof(subtypes); ++t)
            for (unsigned s = 0; s < sizeof(sizes) / sizeof(*sizes); ++s)
                for (unsigned p = 0; p < sizeof(processors) / sizeof(*processors); ++p)
                    for (unsigned options = 0; options < 128; ++options) {
                        RC1_processor = *(short*)0x74f270 = processors[p];
                        RC1_soft_float = *(u8*)0x74f28f = options & 1;
                        RC1_use_paired_moves_for_double = *(u8*)0x74f2c0 = (options >> 1) & 1;
                        gHasVarargs = *(u8*)0x76c02d = (options >> 2) & 1;
                        optimizing = *(int*)0x756ff8 = (options >> 3) & 1;
                        u8 flags = (options >> 6) & 1;
                        input.type.kind = kind;
                        input.type.size = sizes[s];
                        if (kind == StructureType)
                            input.struc.subtype = subtypes[t];
                        else if (kind == MemberPointerType) {
                            target.kind = t & 1 ? PointerType : FunctionType;
                            input.member.target = &target;
                        } else
                            input.integr.integral = subtypes[t];
                        if (Registers_ClassForType(&input.type) != native_class(&input.type))
                            ExitProcess(30);
                        if (kind > 15)
                            continue;
                        setup_bank((options >> 4) & 3, 32 + (options & 7), 0, 5, options & 7);
                        u8 object_kind = (t + s + p) % 3;
                        int cached = object_kind == LocalObject || (options & 2);
                        init_object(&object, object_kind, &input.type, &info, cached, flags);
                        native_assign(&object);
                        Result expected = observe(cached ? &info : object.data_info);
                        init_object(&object, object_kind, &input.type, &info, cached, flags);
                        assign_register_by_type(&object);
                        if (!matches(expected, observe(cached ? &info : object.data_info)) || !matches_bank())
                            ExitProcess(31);
                    }
}

static void check_workers(void) {
    short (*native_obtain)(signed char) = (void*)0x5b1380;
    void (*native_retain)(Object*, signed char, short) = (void*)0x5b15b0;
    void (*native_single)(Object*, signed char) = (void*)0x5b1650;
    void (*native_pair)(Object*) = (void*)0x590820;
    Object object;
    VarInfo info;

    for (int c = 0; c < RegClassCount; ++c)
        for (unsigned mask = 0; mask < 32; ++mask)
            for (unsigned cursor = 0; cursor < 5; ++cursor)
                for (unsigned options = 0; options < 16; ++options) {
                    unsigned limit = (options & 2) ? 5 : cursor;
                    int first = (options & 8) ? 32767 : 32;
                    int capacity = options & 3;
                    optimizing = *(int*)0x756ff8 = options & 1;
                    setup_bank(capacity, first, cursor, limit, mask);
                    if (obtain_nonvolatile_register(c) != native_obtain(c) || !matches_bank())
                        ExitProcess(32);

                    short reserve = (options & 8) ? 31 - cursor : 0;
                    if (c > 0 && options == 14)
                        reserve = -1;
                    if (c < RegClassCount - 1 && options == 15)
                        reserve = 32;
                    int bound = options & 4;
                    setup_bank(capacity, first, cursor, limit, mask);
                    init_object(&object, LocalObject, 0, &info, 1, 0x81);
                    native_retain(bound ? &object : 0, c, reserve);
                    Result original = observe(&info);
                    init_object(&object, LocalObject, 0, &info, 1, 0x81);
                    retain_register(bound ? &object : 0, c, reserve);
                    if (!matches(original, observe(&info)) || !matches_bank())
                        ExitProcess(33);

                    setup_bank(capacity, first, cursor, limit, mask);
                    if (c == 0 && !optimizing) {
                        n_nonvolatile_registers[c] = NativeNonvolatileCount[c] = 5;
                        reg_state[27] = NativeState[27] = 0;
                    }
                    init_object(&object, DataObject, 0, &info, bound, 0x81);
                    native_single(&object, c);
                    original = observe(object.data_info);
                    init_object(&object, DataObject, 0, &info, bound, 0x81);
                    assign_register_to_variable(&object, c);
                    if (!matches(original, observe(object.data_info)) || !matches_bank())
                        ExitProcess(34);

                    if (c == RegGPR) {
                        setup_bank(capacity, options & 8 ? 32767 : options & 4 ? 0 : 32, cursor, limit, mask);
                        init_object(&object, AbsoluteObject, 0, &info, bound, 0x81);
                        native_pair(&object);
                        original = observe(object.data_info);
                        init_object(&object, AbsoluteObject, 0, &info, bound, 0x81);
                        assign_GPR_pair(&object);
                        if (!matches(original, observe(object.data_info)) || !matches_bank())
                            ExitProcess(35);
                    }
                }
}

void start(void) {
    install();
    check_getter();
    check_alloc();
    check_workers();
    ExitProcess(0);
}
