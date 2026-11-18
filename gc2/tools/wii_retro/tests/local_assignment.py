#!/usr/bin/env python3
"""Compare the relink's local register passes with known stock Wii and GC images."""

import argparse
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from native_fixture import (  # noqa: E402
    GC_SHA1, RC1_STOCK, byte_array, fixture_directory, fixture_options, run_fixture, stock_image,
)

SHIFT = 0x17000000
RC1 = {
    "passes": ((0x46D690, 536), (0x46D8B0, 475)),
    "globals": (0x756FF8, 0x751CE4, 0x758120, 0x7502BC),
    "callbacks": (0x590780, 0x456CC0, 0x590A60),
}
GC = {
    "passes": ((0x46B280, 536), (0x46B4A0, 475)),
    "globals": (0x715C34, 0x710924, 0x716D50, 0x710074),
    "callbacks": (0x579790, 0x455A50, 0x579A70),
}


def write_native_data(output, compilers):
    lines = []
    entries = []
    for image, layout in compilers:
        passes = []
        for address, size in layout["passes"]:
            data = bytearray(image.read(address, size))
            for reloc in image.base_relocations:
                if address <= reloc < address + size:
                    struct.pack_into("<I", data, reloc - address, image.word(reloc) + SHIFT)
            name = f"body_{address:x}"
            lines.extend(byte_array(name, data))
            passes.append(f"{{{address + SHIFT:#x}, sizeof({name}), {name}}}")
        globals_ = ", ".join(f"(void*){address + SHIFT:#x}" for address in layout["globals"])
        callbacks = ", ".join(f"{address + SHIFT:#x}" for address in layout["callbacks"])
        entries.extend(("    {", "        {" + ", ".join(passes) + "},",
                        f"        {globals_},", f"        {callbacks},", "    },"))
    lines.extend(("static const NativeCompiler native_compilers[] = {", *entries, "};"))
    output.write_text("\n".join(lines) + "\n", encoding="ascii")


def compare(args, compilers):
    work = fixture_directory(args.out_dir)
    write_native_data(work / "local_assignment_native.h", compilers)
    run_fixture(args, work, "local_assignment", HERE / "local_assignment_fixture.c", HERE.parent / "reference_order.c")
    print(f"Matched {8192 * len(compilers):,} stock local assignment and reassignment cases")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    fixture_options(parser, "local-assignment")
    parser.add_argument("--gc", type=Path, help="stock GC 3.0a5.2 executable for a second control")
    args = parser.parse_args()
    try:
        compilers = [(stock_image(args.stock, RC1_STOCK["sha1"]), RC1)]
        if args.gc:
            compilers.append((stock_image(args.gc, GC_SHA1), GC))
        compare(args, compilers)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"local assignment: {error}\n")


if __name__ == "__main__":
    main()
