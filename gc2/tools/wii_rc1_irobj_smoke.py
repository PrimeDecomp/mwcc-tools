#!/usr/bin/env python3
"""Compare the source IR stream codec with .irobj files emitted by stock Wii 1.0RC1."""

import argparse
import ctypes as c
import os
import tempfile
from _ctypes import FreeLibrary
from pathlib import Path
from unittest.mock import patch

from wii_rc1_smoke import FLAGS, IPA_CPP_SOURCE, ROOT, run

U32 = c.c_uint32
I32 = c.c_int32
Byte = c.c_ubyte
Pointer = c.c_void_p


class Span(c.Structure):
    _fields_ = [("bytes", Pointer), ("size", U32)]

    def read(self):
        return c.string_at(self.bytes, self.size)


class Graph(c.Structure):
    _fields_ = [
        ("type_count", U32),
        ("complex_used", Byte),
        ("main_file", Span),
        ("main_file_name", Span),
        ("file_count", U32),
        ("backend", Span),
        ("node_count", U32),
        ("nodes", Span),
    ]


class Backend(c.Structure):
    _fields_ = [("flags", U32)] + [
        (name, Span) for name in ("sections", "attributes", "interrupts", "defaults", "strings", "files")
    ]


def load_codec(work):
    target, machine = ("x86_64", "x64") if c.sizeof(Pointer) == 8 else ("i686", "x86")
    objects = []
    sources = [
        ROOT / f"src/frontend/{name}.c"
        for name in ("CIRStreamTools", "CIRStreamCallGraph", "CIRStreamType", "CIRStreamCode")
    ]
    sources.append(ROOT / "src/backend/OpcodeInfo.c")
    sources.append(ROOT / "tools/wii_rc1_irobj_probe.c")
    for source in sources:
        obj = work / source.with_suffix(".obj").name
        run(
            "clang",
            f"--target={target}-pc-windows-msvc",
            "-O2",
            "-ffreestanding",
            "-fno-builtin",
            "-fshort-enums",
            "-DVERSION=VERSION_WII_1_0RC1",
            "-I",
            ROOT / "include",
            "-isystem",
            ROOT / "include/libc",
            "-c",
            source,
            "-o",
            obj,
            cwd=work,
        )
        objects.append(obj)
    exports = {
        "CheckHeader": (Byte, Pointer, U32),
        "PackCallGraph": (Byte, c.POINTER(Span), c.POINTER(Graph)),
        "UnpackCallGraphHeader": (Byte, Pointer, U32, c.POINTER(Graph)),
        "UnpackBackendHeader": (Byte, Span, c.POINTER(Backend)),
        "PackUInt32": (U32, Pointer, U32),
        "PackInt32": (U32, Pointer, I32),
        "UnpackUInt32": (Byte, c.POINTER(Span), c.POINTER(U32)),
        "UnpackInt32": (Byte, c.POINTER(Span), c.POINTER(I32)),
    }
    dll = work / "cirstream.dll"
    run(
        "lld-link",
        "/dll",
        "/noentry",
        "/nodefaultlib",
        f"/machine:{machine}",
        f"/out:{dll}",
        "/export:CIRTest_CheckBranchRanges",
        "/export:CIRTest_CheckFunctions",
        "/export:CIRTest_RepackGraph",
        "/export:CIRTest_RepackBodies",
        "/export:CIRTest_CheckMemberLiteral",
        "/export:CIRTest_CheckTypeWriter",
        "/export:CIRTest_CheckExceptions",
        "/export:CIRTest_CheckData",
        "/export:CIRTest_CheckThunk",
        *(f"/export:CIRStream_{name}" for name in exports),
        *objects,
        cwd=work,
    )
    library = c.CDLL(str(dll))
    for name, (result, *args) in exports.items():
        function = getattr(library, f"CIRStream_{name}")
        function.restype = result
        function.argtypes = args
        setattr(library, name, function)
    library.CIRTest_CheckBranchRanges.restype = Byte
    library.CIRTest_CheckBranchRanges.argtypes = [Pointer, U32, U32]
    library.CIRTest_CheckFunctions.restype = U32
    library.CIRTest_CheckFunctions.argtypes = [Pointer, U32, Byte]
    library.CIRTest_RepackGraph.restype = U32
    library.CIRTest_RepackGraph.argtypes = [Pointer, U32, Pointer, U32, Byte]
    library.CIRTest_RepackBodies.restype = U32
    library.CIRTest_RepackBodies.argtypes = [Pointer, U32, Pointer, U32, Byte]
    library.CIRTest_CheckMemberLiteral.restype = Byte
    library.CIRTest_CheckMemberLiteral.argtypes = [Pointer, U32]
    library.CIRTest_CheckTypeWriter.restype = Byte
    library.CIRTest_CheckTypeWriter.argtypes = []
    library.CIRTest_CheckExceptions.restype = U32
    library.CIRTest_CheckExceptions.argtypes = [Pointer, U32]
    library.CIRTest_CheckData.restype = Byte
    library.CIRTest_CheckData.argtypes = [Pointer, U32]
    library.CIRTest_CheckThunk.restype = Byte
    library.CIRTest_CheckThunk.argtypes = [Pointer, U32]
    return library


def check_integers(codec):
    for signed, marker, values in (
        (False, 0xFF, (0, 254, 255, 256, 0xFFFFFFFF)),
        (True, 0x80, (-(2**31), -129, -128, -127, -1, 0, 127, 128, 2**31 - 1)),
    ):
        pack, unpack, ty = (
            (codec.PackInt32, codec.UnpackInt32, I32) if signed else (codec.PackUInt32, codec.UnpackUInt32, U32)
        )
        for value in values:
            short = -127 <= value <= 127 if signed else value < 255
            expected = bytes([value & 255]) if short else bytes([marker]) + value.to_bytes(4, "little", signed=signed)
            data = c.create_string_buffer(5)
            size = pack(data, value)
            assert data.raw[:size] == expected
            result = ty()
            for length in range(size):
                span = Span(c.addressof(data), length)
                assert not unpack(c.byref(span), c.byref(result)) and span.size == length
            span = Span(c.addressof(data), size)
            assert unpack(c.byref(span), c.byref(result)) and not span.size and result.value == value


def check_graph(codec, path, node_count, section=b".text\0"):
    data = path.read_bytes()
    blob = c.create_string_buffer(data)
    base = c.addressof(blob)
    graph = Graph()
    for length in range(16):
        assert not codec.CheckHeader(base, length)
    assert codec.CheckHeader(base, len(data))
    assert codec.UnpackCallGraphHeader(base, len(data), c.byref(graph))
    source_name = graph.main_file_name.read().replace(b"\\", b"/").rsplit(b"/", 1)[-1]
    assert source_name.rsplit(b".", 1)[0].lower() == path.stem.lower().encode("ascii")
    if node_count is not None:
        assert graph.node_count == node_count
    for offset in (0, 4, len(data) - 1):
        blob[offset] = bytes([data[offset] ^ 0x80])
        malformed = Graph()
        assert not codec.UnpackCallGraphHeader(base, len(data), c.byref(malformed))
        blob[offset] = data[offset : offset + 1]

    backend = Backend()
    assert codec.UnpackBackendHeader(graph.backend, c.byref(backend))
    assert backend.sections.size >= 124 and backend.files.size >= 16
    assert section in backend.strings.read()
    assert not codec.UnpackBackendHeader(Span(graph.backend.bytes, graph.backend.size - 1), c.byref(Backend()))
    invalid_offset = graph.backend.bytes - base + 12
    original = blob[invalid_offset : invalid_offset + 4]
    blob[invalid_offset : invalid_offset + 4] = b"\xff" * 4
    assert not codec.UnpackBackendHeader(graph.backend, c.byref(Backend()))
    blob[invalid_offset : invalid_offset + 4] = original

    buffer = c.create_string_buffer(len(data))
    writer = Span(c.addressof(buffer), len(data) - 1)
    assert not codec.PackCallGraph(c.byref(writer), c.byref(graph)) and writer.size == len(data) - 1
    assert buffer.raw == bytes(len(data))
    writer = Span(c.addressof(buffer), len(data))
    assert codec.PackCallGraph(c.byref(writer), c.byref(graph)) and not writer.size
    assert buffer.raw == data

    size = codec.CIRTest_RepackGraph(data, len(data), buffer, len(data), False)
    assert size == len(data), f"{path.name}: typed graph export produced {size} bytes, expected {len(data)}"
    if buffer.raw != data:
        offset = next(i for i, pair in enumerate(zip(buffer.raw, data, strict=True)) if pair[0] != pair[1])
        raise AssertionError(f"{path.name}: typed graph export differs at byte {offset:#x}")
    return data


def check_functions(codec, path, node_count, fixture=0):
    data = check_graph(codec, path, node_count)
    status = codec.CIRTest_CheckFunctions(data, len(data), fixture)
    assert status == 0, f"{path.name}: function graph reconstruction failed at check {status}"
    truncated = data[:-6] + data[-5:]
    assert codec.CIRTest_CheckFunctions(truncated, len(truncated), fixture) == 8
    rebuilt = repack_bodies(codec, data, path.name)
    status = codec.CIRTest_CheckFunctions(rebuilt, len(rebuilt), fixture)
    assert status == 0, f"{path.name}: rebuilt function bodies fail check {status}"


def repack_bodies(codec, data, label, edit=False):
    buffer = c.create_string_buffer(len(data) * 2)
    size = codec.CIRTest_RepackBodies(data, len(data), buffer, len(buffer), edit)
    assert size, f"{label}: could not write function bodies"
    return buffer.raw[:size]


def linked_program_text(reference, work, name, inputs=None, flags=(), *, ipa="program"):
    sidecars = [f"{name}.irobj"] if inputs is None else inputs
    run(reference, *FLAGS, "-O4,p", *flags, "-ipa", ipa, *sidecars, "-o", f"{name}.elf", cwd=work)
    run("llvm-objcopy", "-O", "binary", "--only-section=.text", f"{name}.elf", f"{name}.text", cwd=work)
    return (work / f"{name}.text").read_bytes()


def check_spe_program(compiler, reference, work):
    for fixture, cpu, body, words, options in (
        ("literal", "e500", "__ev64_u32__ __start(void) { return (__ev64_u32__){3,4}; }\n",
         "38600003 38000004 1063022d 4e800020", ()),
        ("assembly", "e500", "void __start(void) { asm {\n    evaddiw r3,r5,11\n    evsel r3,r3,r7,cr2\n"
         "    evmhosmia r3,r3,r7\n    evmra r6,r3\n    evstdd r6,0(r4)\n} }\n",
         "106b2a02 10633a7a 10633c2d 10c304c4 10c40321 4e800020", ()),
        ("spfp", "e500", "#pragma e500_floatingpoint on\n"
         "float __start(float a, float b) { return a + b; }\n"
         "#pragma e500_floatingpoint off\n",
         "106322c0 4e800020", ("-fp", "soft", "-spe_vector")),
        ("dpfp", "e500v2", "#pragma e500v2_floatingpoint on\n"
         "double __start(double a, double b) { return a + b; }\n"
         "#pragma e500v2_floatingpoint off\n",
         "1063222d 1005322d 108302e0 1064222f 4e800020", ("-fp", "soft", "-spe_vector")),
        ("aligned_local", "e500",
         "int __start(void) { __declspec(align(32)) volatile int slot = 7; return slot; }\n",
         "542b06fe 216bffc0 7c2c0b78 7c21596e 38000007 90010020 80610020 81410000 7d415378 4e800020",
         ("-fp", "soft", "-spe_vector", "-Cpp_exceptions", "off")),
        ("interrupt", "e500", "#pragma interrupt save_spe critical SRR on\n"
         "void __start(void) { asm {\n addi r0,r3,1\n stw r0,0(r4)\n} }\n"
         "#pragma interrupt off\n",
         "9421ffe0 90010018 7c0000a6 64000200 7c000124 1000022f 9001001c 7c1a0aa6 90010010 "
         "7c1b0aa6 90010014 38030001 90040000 80010010 7c1a0ba6 80010014 7c1b0ba6 10011b01 "
         "38210020 4c000066", ("-fp", "soft", "-spe_vector", "-Cpp_exceptions", "off")),
    ):
        flags = ("-nodefaults", "-proc", cpu, "-O2", *options)
        source = work / f"spe_{fixture}.c"
        source.write_text(body, encoding="ascii")
        expected = bytes.fromhex(words)
        for prefix, producer in (("stock", reference), ("source", compiler)):
            stem = f"{prefix}_spe_{fixture}"
            sidecar = f"{stem}.irobj"
            run(producer, *flags, "-ipa", "program", "-c", source, "-o", f"{stem}.o", cwd=work)
            run(compiler, *flags, "-ipa", "file", "-c", sidecar, "-o", f"{stem}_replay.o", cwd=work)
            replay = work / f"{stem}_replay.text"
            run("llvm-objcopy", f"--dump-section=.text={replay}", f"{stem}_replay.o", cwd=work)
            assert replay.read_bytes() == expected, f"{sidecar}: unexpected source replay of SPE code"
            run(reference, *flags, "-nostdlib", "-ipa", "program", sidecar, "-o", f"{stem}.elf", cwd=work)
            run("llvm-objcopy", "-O", "binary", "--only-section=.text", f"{stem}.elf", f"{stem}.text", cwd=work)
            assert (work / f"{stem}.text").read_bytes() == expected, f"{sidecar}: unexpected stock link of SPE code"


def check_exported_program(codec, reference, work, compiler_flags):
    (work / "rooted.c").write_text(
        "#pragma force_active on\n"
        "volatile int global[2] = {10, 4};\n"
        "volatile int* ptr = &global[1];\n"
        "int __start(void) { return global[0] + *ptr; }\n",
        "ascii",
    )
    run(reference, *compiler_flags, "rooted.c", "-o", "rooted.o", cwd=work)
    data = check_graph(codec, work / "rooted.irobj", 3)
    buffer = c.create_string_buffer(len(data) * 2)
    size = codec.CIRTest_RepackGraph(data, len(data), buffer, len(buffer), True)
    assert size, "could not export the edited graph"
    (work / "rewritten.irobj").write_bytes(buffer.raw[:size])
    run(reference, *FLAGS, "-O4,p", "-ipa", "program", "rewritten.irobj", "-o", "linked.elf", cwd=work)

    symbols = {}
    for line in run("llvm-nm", "--defined-only", "linked.elf", cwd=work).splitlines():
        address, _, name = line.split()
        symbols[name] = int(address, 16)
    assert "__start" in symbols and symbols["ptr"] == symbols["global"] + 8
    run("llvm-objcopy", "--dump-section", ".sdata=linked.dat", "linked.elf", "copied.elf", cwd=work)
    assert (work / "linked.dat").read_bytes() == b"\0\0\0\x1b\0\0\0\x04" + symbols["global"].to_bytes(4, "big")

    amended = repack_bodies(codec, data, "rooted.irobj", edit=True)
    (work / "rebuilt-body.irobj").write_bytes(amended)
    root_source = (work / "rooted.c").read_text("ascii")
    (work / "expected.c").write_text(root_source.replace("+ *ptr;", "+ *ptr + 37;"), "ascii")
    run(reference, *compiler_flags, "expected.c", "-o", "expected.o", cwd=work)
    assert linked_program_text(reference, work, "rebuilt-body") == linked_program_text(reference, work, "expected")

    (work / "asm_body.c").write_text(
        "#pragma force_active on\n"
        "volatile int global = 27;\n"
        "int __start(void) {\n"
        "    register int result;\n"
        "    asm {\n"
        "        li result, 1\n"
        "        b done\n"
        "        li result, 2\n"
        "    done:\n"
        "        addi result, result, 5\n"
        "    }\n"
        "    return result + global;\n"
        "}\n",
        "ascii",
    )
    run(reference, *compiler_flags, "asm_body.c", "-o", "asm_body.o", cwd=work)
    data = check_graph(codec, work / "asm_body.irobj", 2)
    (work / "rebuilt_asm.irobj").write_bytes(repack_bodies(codec, data, "asm_body.irobj"))
    assert linked_program_text(reference, work, "rebuilt_asm") == linked_program_text(reference, work, "asm_body")

    (work / "member_literal.cpp").write_text(
        "#pragma force_active on\n"
        "struct Cell { int value; int read(); };\n"
        "int Cell::read() { return value; }\n"
        "typedef int (Cell::*Method)();\n"
        "Method selected;\n"
        'extern "C" int __start(void) { selected = &Cell::read; return 0; }\n',
        "ascii",
    )
    run(reference, *compiler_flags, "member_literal.cpp", "-o", "member_literal.o", cwd=work)
    data = check_graph(codec, work / "member_literal.irobj", 3)
    assert codec.CIRTest_CheckMemberLiteral(data, len(data))
    rebuilt = repack_bodies(codec, data, "member_literal.irobj")
    assert codec.CIRTest_CheckMemberLiteral(rebuilt, len(rebuilt))
    (work / "rebuilt_member.irobj").write_bytes(rebuilt)
    assert linked_program_text(reference, work, "rebuilt_member") == linked_program_text(
        reference, work, "member_literal"
    )


def check_live_program(codec, compiler, reference, work, compiler_flags):
    (work / "live_root.c").write_text(
        '#pragma push\n#pragma section data_type ".more" ".morebss"\n'
        "#pragma force_active on\n"
        "volatile int table[8] = {7, 4};\n"
        "volatile int* selected = &table[1];\n"
        "extern int branch(int);\n"
        "int __start(void) { return branch(4) + 5; }\n"
        "#pragma pop\n",
        "ascii",
    )
    (work / "live_branch.cpp").write_text(
        'extern "C" int branch(int value) { int product = value * 7; return product + 1; }\n',
        "ascii",
    )
    flags = ("-sdata", "0", "-sdata2", "0", "-func_align", "4")
    text = {}
    for name, producer in (("live", compiler), ("baseline", reference)):
        for stem, suffix in (("root", "c"), ("branch", "cpp")):
            output = work / f"{name}_{stem}.o"
            run(producer, *compiler_flags, *flags, f"live_{stem}.{suffix}", "-o", output, cwd=work)
            assert "__start" not in run("llvm-nm", "--defined-only", output, cwd=work)
            data = output.with_suffix(".irobj").read_bytes()
            graph = Graph()
            assert codec.UnpackCallGraphHeader(data, len(data), c.byref(graph)) and graph.node_count
        text[name] = linked_program_text(reference, work, name, [f"{name}_root.irobj", f"{name}_branch.irobj"], flags)
        run("llvm-objcopy", "--dump-section", f".more={name}.dat", f"{name}.elf", f"{name}_copy.elf", cwd=work)
    assert text["live"] == text["baseline"]
    assert (work / "live.dat").read_bytes() == (work / "baseline.dat").read_bytes()
    run(compiler, *compiler_flags, "asm_body.c", "-o", "live_asm.o", cwd=work)
    assert linked_program_text(reference, work, "live_asm") == linked_program_text(reference, work, "asm_body")

    (work / "live_initializer.cpp").write_text(
        "#pragma force_active on\n"
        "volatile int seed = 21;\n"
        "int value = seed + 2;\n"
        'extern "C" int __start(void) { return value; }\n',
        "ascii",
    )
    for prefix, producer in (("live", compiler), ("baseline", reference)):
        run(producer, *compiler_flags, "live_initializer.cpp", "-o", f"{prefix}_init.o", cwd=work)
    assert linked_program_text(reference, work, "live_init") == linked_program_text(reference, work, "baseline_init")

    for replay, baseline in (
        ("live_asm", "asm_body"),
        ("asm_body", "asm_body"),
        ("live_init", "baseline_init"),
        ("baseline_init", "baseline_init"),
    ):
        name = f"{replay}_replay"
        run(compiler, *compiler_flags, f"{replay}.irobj", "-o", f"{name}.o", cwd=work)
        assert linked_program_text(reference, work, name, [f"{name}.o"], ipa="off") == linked_program_text(
            reference, work, baseline
        )

    run(compiler, *compiler_flags, "member_literal.irobj", "-o", "member_replay.o", cwd=work)
    linked_program_text(reference, work, "member_replay", ["member_replay.o"], ipa="off")
    for name in ("member_literal", "member_replay"):
        run("llvm-objcopy", "--dump-section", f".data={name}.dat", f"{name}.elf", f"{name}_copy.elf", cwd=work)
    assert (work / "member_replay.dat").read_bytes() == (work / "member_literal.dat").read_bytes()

    (work / "assembly_function.c").write_text(
        "#pragma force_active on\n"
        "asm int helper(int value) {\n"
        "    nofralloc\n"
        "    addi r3, r3, 7\n"
        "    blr\n"
        "}\n"
        "int __start(void) { return helper(4); }\n",
        "ascii",
    )
    assembly_flags = (*compiler_flags, "-Cpp_exceptions", "off")
    run(reference, *assembly_flags, "assembly_function.c", "-o", "assembly_function.o", cwd=work)
    run(compiler, *compiler_flags, "assembly_function.irobj", "-o", "assembly_replay.o", cwd=work)
    asm_symbols = run("llvm-nm", "--defined-only", "assembly_replay.o", cwd=work)
    assert " helper" in asm_symbols and " __start" in asm_symbols

    linker_path = os.pathsep.join((str(reference.parent), os.environ.get("PATH", "")))
    with patch.dict(os.environ, PATH=linker_path):
        for name, inputs in (
            ("source_project", ["live_root.c", "live_branch.cpp"]),
            ("stock_sidecars", ["baseline_root.irobj", "baseline_branch.irobj"]),
            ("source_sidecars", ["live_root.irobj", "live_branch.irobj"]),
            ("mixed_project", ["live_root.c", "baseline_branch.irobj"]),
        ):
            actual = linked_program_text(compiler, work, name, inputs, (*flags, "-Cpp_exceptions", "off"))
            assert actual == text["baseline"], name
            symbols = run("llvm-nm", "--defined-only", f"{name}.elf", cwd=work)
            assert "__start" in symbols and " branch" not in symbols, name
            run("llvm-objcopy", "--dump-section", f".more={name}.dat", f"{name}.elf", f"{name}_copy.elf", cwd=work)
            assert (work / f"{name}.dat").read_bytes() == (work / "baseline.dat").read_bytes(), name

        no_exceptions = ("-func_align", "4", "-Cpp_exceptions", "off")
        assert linked_program_text(
            compiler, work, "assembly_project", ["assembly_function.c"], no_exceptions
        ) == linked_program_text(reference, work, "assembly_replay", ["assembly_replay.o"], no_exceptions, ipa="off")


def check_program_inline_strings(compiler, reference, work):
    fixtures = {
        "alpha": (
            "const char* volatile sink;\n"
            "extern void branch();\n"
            'inline void alpha() { sink = "alpha1"; sink = "alpha2"; }\n'
            'inline void unused() { sink = "unused"; }\n'
            "void (*volatile anchorA)() = alpha;\n"
            'extern "C" int __start() { anchorA(); branch(); return *sink; }\n'
        ),
        "bravo": (
            "extern const char* volatile sink;\n"
            'inline void bravo() { sizeof("gone"); sink = "bravo1"; sink = "bravo2"; }\n'
            "void (*volatile anchorB)() = bravo;\n"
            "void branch() { anchorB(); }\n"
        ),
    }
    flags = ("-inline", "noauto", "-func_align", "4", "-Cpp_exceptions", "off")
    for name, source in fixtures.items():
        (work / f"{name}.cpp").write_text(source, "ascii")

    sidecars = {}
    for prefix, producer in (("stock", reference), ("source", compiler)):
        sidecars[prefix] = []
        for name in fixtures:
            stem = f"{prefix}_{name}"
            run(producer, *FLAGS, "-O4,p", *flags, "-ipa", "program", "-c", f"{name}.cpp", "-o", f"{stem}.o", cwd=work)
            sidecars[prefix].append(f"{stem}.irobj")

    linker_path = os.pathsep.join((str(reference.parent), os.environ.get("PATH", "")))
    with patch.dict(os.environ, PATH=linker_path):
        for name, tool, inputs in (
            ("stock_inline_strings", reference, sidecars["stock"]),
            ("source_inline_strings", reference, sidecars["source"]),
            ("import_inline_strings", compiler, sidecars["stock"]),
        ):
            linked_program_text(tool, work, name, inputs, (*flags, "-nostdlib"))
            run("llvm-objcopy", "-O", "binary", "--only-section=.sdata", f"{name}.elf", f"{name}.data", cwd=work)
            data = (work / f"{name}.data").read_bytes()
            # Two function address relocations surround the first unit's strings.
            assert data[4:20] == b"alpha2\0\0alpha1\0\0", name
            assert data[24:] == b"bravo2\0\0bravo1\0\0", name


def check_program_branch_ranges(codec, compiler, reference, work):
    flags = ("-inline", "noauto", "-func_align", "4", "-Cpp_exceptions", "off")
    for pragma, expected in (("default", 65536), ("0", 0), ("8", 16)):
        (work / "branch_ranges.cpp").write_text(
            "volatile int counter;\n"
            "static int next_value() { return ++counter; }\n"
            f"#pragma bc_range {pragma}\n"
            'extern "C" int __start() { static int value = next_value(); return value; }\n',
            "ascii",
        )
        for prefix, producer in (("stock", reference), ("source", compiler)):
            name = f"{prefix}_range_{pragma}"
            run(producer, *FLAGS, "-O4,p", *flags, "-ipa", "program", "-c",
                "branch_ranges.cpp", "-o", f"{name}.o", cwd=work)
            data = (work / f"{name}.irobj").read_bytes()
            assert codec.CIRTest_CheckBranchRanges(data, len(data), expected), name
            assert linked_program_text(reference, work, name, flags=(*flags, "-nostdlib")), name


def check_program_peephole(compiler, reference, work):
    (work / "peephole.cpp").write_text(
        "volatile int counter;\n"
        "static int next_value() { return ++counter; }\n"
        'extern "C" int __start() { static int value = next_value(); return value; }\n',
        "ascii",
    )
    flags = ("-inline", "noauto", "-func_align", "4", "-Cpp_exceptions", "off")
    linker_path = os.pathsep.join((str(reference.parent), os.environ.get("PATH", "")))
    stock_text = {}
    with patch.dict(os.environ, PATH=linker_path):
        for opt, opposite in (("peep", "nopeep"), ("nopeep", "peep")):
            stock = f"stock_{opt}"
            source = f"source_{opt}"
            for name, producer in ((stock, reference), (source, compiler)):
                run(producer, *FLAGS, "-O4,p", *flags, "-opt", opt, "-ipa", "program",
                    "-c", "peephole.cpp", "-o", f"{name}.o", cwd=work)
            link_flags = (*flags, "-nostdlib", "-opt", opposite)
            stock_text[opt] = linked_program_text(reference, work, stock, flags=link_flags)
            assert linked_program_text(reference, work, source, flags=link_flags) == stock_text[opt], opt
            assert linked_program_text(
                compiler, work, f"replay_{opt}", [f"{stock}.irobj"], link_flags
            ) == stock_text[opt], opt
    assert stock_text["peep"] != stock_text["nopeep"]


def check_program_conditional_returns(compiler, reference, work):
    stock_text = {}
    flags = ("-inline", "noauto", "-func_align", "4", "-Cpp_exceptions", "off", "-opt", "nopeep")
    for setting in ("on", "off"):
        (work / "conditional_return.cpp").write_text(
            f"#pragma ppc_opt_bclr_bcctr {setting}\n"
            "extern volatile signed char gate;\n"
            'extern "C" int __start() { return gate ? 15 : 27; }\n'
            "volatile signed char gate = 10;\n",
            "ascii",
        )
        stock = f"stock_return_{setting}"
        source = f"source_return_{setting}"
        for name, producer in ((stock, reference), (source, compiler)):
            run(producer, *FLAGS, "-O4,p", *flags, "-ipa", "program",
                "-c", "conditional_return.cpp", "-o", f"{name}.o", cwd=work)
        stock_text[setting] = linked_program_text(reference, work, stock, flags=(*flags, "-nostdlib"))
        assert linked_program_text(reference, work, source, flags=(*flags, "-nostdlib")) == stock_text[setting], setting
    assert stock_text["on"] != stock_text["off"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path, help="path to stock Wii 1.0RC1")
    parser.add_argument("--compiler", type=Path, help="also exercise program IPA in a source-built compiler")
    args = parser.parse_args()
    reference = args.reference.resolve(strict=True)
    if args.compiler and not reference.with_name("mwldeppc.exe").is_file():
        parser.error("--compiler requires mwldeppc.exe beside the stock compiler")
    with tempfile.TemporaryDirectory(prefix="rc1-irobj-") as directory:
        work = Path(directory)
        compiler_flags = (*FLAGS, "-O4,p", "-inline", "auto", "-func_align", "4", "-ipa", "program", "-c")
        (work / "program.c").write_text("extern int leaf(int); int entry(int n) { return leaf(n) + 1; }\n", "ascii")
        (work / "sections.c").write_text(
            '#pragma section data_type ".more" ".morebss"\nint global[1] = {10};\nint* ptr = &global[1];\n', "ascii"
        )
        (work / "nodes.c").write_text("".join(f"int f{i}(void) {{ return {i}; }}\n" for i in range(300)), "ascii")
        (work / "aggregates.c").write_text(
            "enum Shade { Red = -3, Green = 257 };\n"
            "struct Cell { struct Cell* next; unsigned bits:3; const int values[3]; };\n"
            "int describe(const struct Cell* cell, enum Shade shade, ...) {\n"
            "    return cell->bits + cell->values[0] + shade;\n}\n",
            "ascii",
        )
        (work / "classes.cpp").write_text(
            "namespace Outer { namespace Inner {\n"
            "struct Base { int base; };\n"
            "struct Cell : Base { int value; int read() const; };\n"
            "int Cell::read() const { return base + value; }\n"
            "int use(const Cell* cell) { return cell->read(); }\n"
            "}}\n",
            "ascii",
        )
        (work / "nested_enum.cpp").write_text(
            "struct Outer {\n"
            "    struct Inner { enum Kind { First, Second }; Kind kind; };\n"
            "    Inner::Kind state;\n"
            "};\n"
            "int read_kind(const Outer::Inner* value) { return value->kind; }\n",
            "ascii",
        )
        (work / "thunks.cpp").write_text(IPA_CPP_SOURCE, "ascii")
        (work / "flow.c").write_text(
            "int flow(int n) {\n"
            "    int sum = 0;\n"
            "    while (n > 0) { if (n & 1) sum += n; --n; }\n"
            "    switch (sum) { case 2: return 4; case 7: return 9; default: return sum; }\n"
            "}\n",
            "ascii",
        )
        (work / "literals.c").write_text(
            'extern int sink(const char*, double); int call_sink(void) { return sink("Wii", 3.5); }\n', "ascii"
        )
        (work / "source_locations.h").write_text("int included(void) { return 8; }\n", "ascii")
        (work / "include.c").write_text(
            '#include "source_locations.h"\nint include_entry(void) { return included(); }\n', "ascii"
        )
        (work / "exceptions.cpp").write_text(
            'extern "C" int may_throw(int);\n'
            "struct Guard { Guard(); ~Guard(); };\n"
            'extern "C" int run_guarded(int x) {\n'
            "    try { Guard guard; return may_throw(x); } catch (...) { return -1; }\n"
            "}\n",
            "ascii",
        )
        run(reference, *compiler_flags, "-Cpp_exceptions", "on", "exceptions.cpp", "-o", "exceptions.o", cwd=work)
        (work / "options.c").write_text(
            "#pragma optimization_level 1\n"
            "#pragma opt_removeemptyloops on\n"
            "#pragma optimize_tail_call on\n"
            "#pragma optimize_for_size on\n"
            "#pragma function_align 64\n"
            "#pragma fp_contract on\n"
            "#pragma ppc_unroll_instructions_limit 333\n"
            "#pragma ppc_unroll_factor_limit 123\n"
            "#pragma legacy_struct_alignment on\n"
            "#pragma ppc_dont_balance_ir on\n"
            "#pragma ppc_dont_balance_exp on\n"
            "#pragma force_cats on\n"
            "int options(int x) { return x + 17; }\n",
            "ascii",
        )
        run(
            reference,
            *compiler_flags,
            "-proc",
            "750",
            "-sdata",
            "79",
            "-sdata2",
            "113",
            "options.c",
            "-o",
            "options.o",
            cwd=work,
        )
        for name in (
            "program.c",
            "sections.c",
            "nodes.c",
            "aggregates.c",
            "classes.cpp",
            "nested_enum.cpp",
            "thunks.cpp",
            "flow.c",
            "literals.c",
            "include.c",
        ):
            run(reference, *compiler_flags, name, "-o", Path(name).with_suffix(".o"), cwd=work)
        codec = load_codec(work)
        try:
            check_integers(codec)
            assert codec.CIRTest_CheckTypeWriter(), "new type records did not preserve identity or patch indices"
            check_functions(codec, work / "program.irobj", 2, fixture=1)
            data = check_graph(codec, work / "sections.irobj", 2, section=b".more\0")
            assert codec.CIRTest_CheckData(data, len(data)), "sections.irobj: data relocation reconstruction failed"
            check_functions(codec, work / "nodes.irobj", 300)
            check_functions(codec, work / "aggregates.irobj", 1, fixture=2)
            check_functions(codec, work / "classes.irobj", 2, fixture=3)
            check_graph(codec, work / "nested_enum.irobj", 1)
            check_functions(codec, work / "flow.irobj", 1, fixture=4)
            check_functions(codec, work / "literals.irobj", 2, fixture=5)
            check_functions(codec, work / "include.irobj", 2, fixture=6)
            check_functions(codec, work / "options.irobj", 1, fixture=7)
            data = check_graph(codec, work / "exceptions.irobj", None)
            status = codec.CIRTest_CheckExceptions(data, len(data))
            assert status == 0, f"exceptions.irobj: exception action reconstruction failed at check {status}"
            rebuilt = repack_bodies(codec, data, "exceptions.irobj")
            status = codec.CIRTest_CheckExceptions(rebuilt, len(rebuilt))
            assert status == 0, f"exceptions.irobj: rebuilt exception actions fail check {status}"
            data = check_graph(codec, work / "thunks.irobj", None)
            assert codec.CIRTest_CheckThunk(data, len(data)), "thunks.irobj: C++ thunk reconstruction failed"
            if reference.with_name("mwldeppc.exe").is_file():
                check_exported_program(codec, reference, work, compiler_flags)
                if args.compiler:
                    compiler = args.compiler.resolve(strict=True)
                    check_live_program(codec, compiler, reference, work, compiler_flags)
                    check_program_inline_strings(compiler, reference, work)
                    check_program_branch_ranges(codec, compiler, reference, work)
                    check_program_peephole(compiler, reference, work)
                    check_program_conditional_returns(compiler, reference, work)
                    check_spe_program(compiler, reference, work)
        finally:
            # Windows keeps a loaded DLL open until its last reference is released.
            FreeLibrary(codec._handle)
    print("Wii 1.0RC1 program IPA stream smoke checks passed")


if __name__ == "__main__":
    main()
