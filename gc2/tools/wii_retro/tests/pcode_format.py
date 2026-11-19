#!/usr/bin/env python3
"""Compare the recovered operand formatter with stock Wii 1.0RC1."""

import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from native_fixture import (  # noqa: E402
    RC1_STOCK, byte_array, fixture_directory, fixture_options, run_fixture, stock_image,
)

SEGMENTS = (
    (0x5A6C10, 0x8D),    # is_sci8_form, used by the immediate validator
    (0x5A8680, 0x212B),  # vformatpcode, pcode_const_from_format and pcode_check_imm_bits
    (0x5B2FC0, 0x13),
    (0x67D1CC, 0x158),
    (0x6DB7FA, 12),
    (0x6DBD96, 12),
    (0x6DBE13, 7),
)

OBJECT = "(uintptr_t)&objects"
LABEL = "(uintptr_t)&label"
REAL = [
    (0, (LABEL,)), (0, (0, OBJECT)), (1, (2, OBJECT, 16)),
    (2, (7, 2, 9, LABEL)), (0x11, ()), (0x12, (OBJECT, 24)),
    (0x15, (4, 0, OBJECT, 12)), (0x16, (7, 9, OBJECT, -4)),
    (0x27, (2, 3, 5, OBJECT, 8)), (0x36, (3, 9, 3, OBJECT, 8)),
    (0x39, (3, 0, 4)), (0x3B, (0, 4)), (0x3F, (7, 9, OBJECT, 36)),
    (0x42, (7, 9, OBJECT, 36)), (0x52, (2, 5, -32768)),
    (0x59, (3, 4, 0xffff)), (0x6E, (2, 1, 3, 2, 4, 3)),
    (0x7A, (0xA1, 9)), (0x7C, (0x100, 4)), (0x7E, (4, 0x391)),
    (0x80, (7,)), (0x82, (7,)), (0x8E, (4, 0, 0, 12)),
    (0xBB, (3, 4, 31)), (0xC7, (8, 0x10C)), (0xC7, (9, 0x10D)),
    (0xC7, (9, 0x100)), (0xD7, (17, 4, 5)), (0xEA, (3, 21)),
    (0xF4, (3, 0, 4)), (0x1B0, (4, 5, 6)),
]
for op in range(0x192, 0x19A):
    for gqr in (0x390, 0x391):
        REAL.append((op, (4, 5, 6, 1, gqr) if (op - 0x192) & 2 else (4, 5, OBJECT, 12, 1, gqr)))

SYNTHETIC = (
    ("$r,=$c,+$b", 3, 0, 0, ()),
    ("=-?!r", 1, 0, 0, (4,)),
    ("$s", 1, 0, 0, ()),
    ("S912,=S256", 2, 0, 0, ()),
    ("k,K,U16,o,a24", 5, 0, 0, (0x87654321, 0x12345678, 147, 0xffff, 8)),
    ("u2", 1, 0, 0, (4,)),
    ("D,R,N,t,B,q", 7, 0, 0, (0x102, 0x123, 0x3f, 0x1f, 3, 4, 3)),
    ("r@2,c/2,v>p,^f<4,:r*1,z|4", 6, 0, 0x20000, (3, 2, 1, 0, 5, 7)),
    ("e,j", 2, 0, 0, (6, 9)),
    ("e,j", 2, 0, 0, (8, 5)),
    ("r,m,M", 3, 0x20000, 0, (4, OBJECT, 8, OBJECT, 12)),
    ("r,m,M", 3, 0x6, 0, (4, 0, 8, 0, 12)),
    ("%0b,%4", 3, 0, 0, (8,)),
    ("%z0(+b)", 3, 0, 0, (8, 9)),
    ("p", 4, 0, 0x20000, ()),
)


def contents(image):
    lines = ["typedef struct { uintptr_t address; unsigned size; const unsigned char* bytes; } Segment;"]
    for address, size in SEGMENTS:
        lines.extend(byte_array(f"bytes_{address:x}", image.read(address, size)))
    lines.append("static const Segment native_segments[] = {")
    for address, _ in SEGMENTS:
        lines.append(f"    {{{address:#x}, sizeof(bytes_{address:x}), bytes_{address:x}}},")
    lines.append("};")

    synthetic = 0x380
    rows = {}
    for opcode, _ in REAL:
        address = 0x6E3D10 + 22 * opcode
        _, text, count, rank, flags, extended = struct.unpack("<IIBBII", image.read(address, 18))
        fmt = image.read(text, 32).split(b"\0", 1)[0].decode("ascii")
        rows[opcode] = fmt, count, rank, flags, extended
    for i, (fmt, count, flags, extended, _) in enumerate(SYNTHETIC):
        rows[synthetic + i] = fmt, count, 0, flags, extended
    lines.append("const OpcodeInfo opcodeinfo[] = {")
    for opcode, (fmt, count, rank, flags, extended) in rows.items():
        lines.append(f"    [{opcode:#x}] = {{0, {json.dumps(fmt)}, {count}, {rank}, {flags:#x}, {extended:#x}, {{0}}}},")
    lines.append("};")
    cases = [*REAL, *((synthetic + i, args) for i, (_, _, _, _, args) in enumerate(SYNTHETIC))]
    lines.append("static const struct { short opcode; uintptr_t args[12]; } cases[] = {")
    for opcode, args in cases:
        lines.append(f"    {{{opcode:#x}, {{{', '.join(map(str, args)) or '0'}}}}},")
    lines.append("};")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    fixture_options(parser, "pcode-format")
    args = parser.parse_args()
    try:
        work = fixture_directory(args.out_dir)
        (work / "pcode_format_native.h").write_text(contents(stock_image(args.stock, RC1_STOCK["sha1"])), encoding="ascii")
        run_fixture(args, work, "pcode_format", HERE / "pcode_format_fixture.c", HERE.parent / "pcode_format.c")
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"PCode format: {error}\n")
    print(f"Matched stock RC1 for {(len(REAL) + len(SYNTHETIC)) * 3 * 6} operand formatting cases")


if __name__ == "__main__":
    main()
