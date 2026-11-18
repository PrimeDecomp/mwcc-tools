#!/usr/bin/env python3
"""Check the x86 local register passes with a CodeWarrior Windows 9.x host."""

import argparse
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from local_assignment import GC, RC1  # noqa: E402
from mw import coff, verdict  # noqa: E402
from native_fixture import GC_SHA1, RC1_STOCK, ROOT, fixture_directory, stock_image  # noqa: E402

# The host probe uses the fields that these functions access. The runtime check
# compiles the full relink structs in rc1_local.h and links the production file.
C90_ABI = """\
typedef unsigned char u8;
typedef struct Identifier Identifier;
#pragma pack(push, 2)
typedef struct { u8 kind; } Type;
typedef struct {
    Type type;
    unsigned size;
    const Type* target;
    unsigned qualifiers;
} PointerType;
typedef struct {
    u8 prefix[12];
    const Identifier* name;
    const Type* type;
    unsigned qualifiers;
} Object;
typedef struct {
    u8 reserved[4];
    int usage;
    u8 no_register;
    u8 used;
    u8 reserved2[4];
    u8 flags;
    u8 register_class;
    short reg;
} VarInfo;
typedef struct Local Local;
struct Local {
    Local* next;
    Object* object;
};
#pragma pack(pop)

enum { TypePointer = 12, TypeArray = 13, QualVolatile = 2, HasMemory = 128 };
extern int optimizing;
extern Local *arguments, *RC1_locals, *toclist;
extern VarInfo* Registers_GetVarInfo(Object*);
extern void assign_register_by_type(Object*);
extern u8 IsTempName(const Identifier*);

"""


def compile_passes(args):
    work = fixture_directory(args.out_dir)
    source = (HERE.parent / "reference_order.c").read_text(encoding="utf-8")
    start = source.index("static inline int pointer_or_array(")
    end = source.index("void retro_AssignLocals(", start)
    adapted = work / "local_assignment_c90.c"
    adapted.write_text(C90_ABI + source[start:end].replace("static inline", "static __inline"), encoding="ascii")
    obj = work / "local_assignment_c90.obj"
    env = os.environ.copy()
    env.setdefault("MWCIncludes", str(ROOT / "gc2/include/libc"))
    if args.runtime_dir:
        env["PATH"] = str(args.runtime_dir.resolve()) + os.pathsep + env.get("PATH", "")
    if args.license_file:
        env["LM_LICENSE_FILE"] = str(args.license_file.resolve())
    subprocess.run(
        [str(args.compiler.resolve()), "-c", "-lang", "c", "-O3", "-inline", "auto,deferred",
         "-proc", "x86", "-sym", "on", "-Cpp_exceptions", "off", str(adapted), "-o", str(obj)],
        check=True,
        env=env,
    )
    return coff.Object.read(obj)


def check(obj, label, image, layout):
    names = ("_optimizing", "_arguments", "_RC1_locals", "_toclist", "_Registers_GetVarInfo", "_IsTempName",
             "_assign_register_by_type")
    bindings = dict(zip(names, (*layout["globals"], *layout["callbacks"]), strict=True))
    passes = ("RC1_AssignLocals", "RC1_ReassignLocals")
    rows = [verdict.Row(name, address, size, "local_assignment_c90.c", name)
            for name, (address, size) in zip(passes, layout["passes"], strict=True)]
    results = verdict.compare_object(image, obj, rows, bindings, image.base_relocations, score=False)
    for row, result in zip(rows, results, strict=True):
        if result.body is None:
            raise ValueError(f"{label} {row.name}: {result.reason}")
        stock = image.read(row.address, row.size)
        matched = sum(a == b for a, b in zip(stock, result.body))
        print(f"{label} {row.name}: {matched}/{row.size} bytes, emitted {len(result.body)} bytes")
    if not results[0].exact:
        raise ValueError(f"{label}: the initial assignment pass did not match")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True, help="CodeWarrior Windows 9.2 or 9.4 mwcc.exe")
    parser.add_argument("--runtime-dir", type=Path, help="directory containing LMGR8C.dll")
    parser.add_argument("--license-file", type=Path, help="CodeWarrior license.dat")
    parser.add_argument("--stock", type=Path, default=ROOT / RC1_STOCK["original"])
    parser.add_argument("--gc", type=Path, help="stock GC 3.0a5.2 executable for a second control")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "build/wii-retro-tests/local-assignment-host")
    args = parser.parse_args()
    try:
        obj = compile_passes(args)
        check(obj, "Wii 1.0RC1", stock_image(args.stock, RC1_STOCK["sha1"]), RC1)
        if args.gc:
            check(obj, "GC 3.0a5.2", stock_image(args.gc, GC_SHA1), GC)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"local assignment host match: {error}\n")


if __name__ == "__main__":
    main()
