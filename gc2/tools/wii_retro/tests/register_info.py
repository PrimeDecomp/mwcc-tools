#!/usr/bin/env python3
"""Compare the relink's register classifier and allocator against stock Wii 1.0RC1."""

import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from native_fixture import (  # noqa: E402
    RC1_STOCK, byte_array, fixture_directory, fixture_options, run_fixture, stock_image,
)

# Functions, their jump tables, and the diagnostic strings read by the fixture.
SEGMENTS = (
    (0x590530, 185), (0x67B878, 13 * 4),  # Registers_ClassForType
    (0x590780, 156), (0x67B8AC, 4 * 4),   # Registers_GetVarInfo
    (0x590820, 568), (0x67B8BC, 8 * 4),   # assign_GPR_pair
    (0x590A60, 788), (0x67B8DC, 8 * 4),   # assign_register_by_type
    (0x5B1380, 92),                       # obtain_nonvolatile_register
    (0x5B15B0, 154),                      # retain_register
    (0x5B1650, 180),                      # assign_register_to_variable
    (0x6DB82E, len(b"RegisterInfo.c\0")),
    (0x6DC02C, len(b"Registers.c\0")),
)


def write_native_data(output, image):
    lines = ["typedef struct { u32 address; u32 size; const u8* bytes; } Segment;"]
    for address, size in SEGMENTS:
        lines.extend(byte_array(f"bytes_{address:x}", image.read(address, size)))
    lines.append("static const Segment native_segments[] = {")
    for address, _ in SEGMENTS:
        name = f"bytes_{address:x}"
        lines.append(f"    {{{address:#x}, sizeof({name}), {name}}},")
    lines.append("};")
    output.write_text("\n".join(lines) + "\n", encoding="ascii")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    fixture_options(parser, "register-info")
    args = parser.parse_args()
    try:
        image = stock_image(args.stock, RC1_STOCK["sha1"])
        work = fixture_directory(args.out_dir)
        write_native_data(work / "register_info_native.h", image)
        run_fixture(args, work, "register_info", HERE / "register_info_fixture.c", HERE.parent / "register_info.c")
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"register info: {error}\n")
    print("Matched 512 variable-info cases, 12,779,520 register-class cases, and 798,720 allocation dispatches")
    print("Matched 38,400 scans, reservations and single assignments and 2,560 pair assignments")


if __name__ == "__main__":
    main()
