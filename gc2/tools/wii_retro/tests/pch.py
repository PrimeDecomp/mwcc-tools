#!/usr/bin/env python3
"""Compare PowerPC objects across four Wii compiler PCH producer/reader pairs."""

import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
HEADER = """\
#pragma parse_func_templ on
extern int external(int);
struct Item {
    int tag;
    Item() : tag(external(7)) {}
    ~Item() { external(tag); }
};
template<class T, class Size> int packed_class_vla(Size size) {
    T values[size];
    return values[0].tag;
}
"""
CONSUMER = """\
#include "consumer.pch"
int use_class_vla(int n) { return packed_class_vla<Item>(1 + (n & 7)); }
"""
FLAGS = ("-nodefaults", "-proc", "gekko", "-lang=c++", "-gccinc", "-Cpp_exceptions", "on",
         "-O4,p", "-inline", "auto", "-ipa", "file")


def compare(args):
    compilers = {label: path.resolve(strict=True) for label, path in (
        ("baseline", args.baseline), ("candidate", args.candidate),
    )}
    args.out_dir.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="pairs-", dir=args.out_dir.resolve()))
    header = work / "Guarded.hpp"
    header.write_text(HEADER, encoding="ascii")
    consumer = work / "UsePch.cpp"
    consumer.write_text(CONSUMER, encoding="ascii")
    objects = {}
    for producer, compiler in compilers.items():
        pch = work / f"{producer}.pch"
        subprocess.run([str(compiler), *FLAGS, "-precompile", str(pch), str(header)], cwd=work, check=True)
        shutil.copyfile(pch, work / "consumer.pch")
        for reader, reader_compiler in compilers.items():
            obj = work / f"{producer}-{reader}.o"
            subprocess.run([str(reader_compiler), *FLAGS, "-c", str(consumer), "-o", str(obj)], cwd=work, check=True)
            objects[producer, reader] = obj.read_bytes()
    reference = objects["baseline", "baseline"]
    changes = [f"{producer}-{reader}" for (producer, reader), data in objects.items() if data != reference]
    if changes:
        raise ValueError(f"different producer/reader pairs: {', '.join(changes)}; see {work}")
    print(f"Matched guarded template initialization across all four PCH pairs in {work}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True, help="baseline mwcceppc.exe")
    parser.add_argument("--candidate", type=Path, required=True, help="candidate mwcceppc.exe")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "build/wii-retro-tests/pch")
    args = parser.parse_args()
    try:
        compare(args)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Wii PCH comparison: {error}\n")


if __name__ == "__main__":
    main()
