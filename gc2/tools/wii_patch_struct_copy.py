#!/usr/bin/env python3
"""Patch Wii 1.0RC1's mwcceppc.exe to copy structs the way the Prime games' Wii compiler does.

RC1's `#pragma legacy_struct_alignment` (copts+0xA1, at 0x74F2F9) switches two things at once:
- the alignment a struct copy or compare assumes for its operands (gen_ASS, gen_COND, two Intrinsics
  functions, and CMach_GetObjectAlignment's path for objects that are not locals). Legacy: the declared
  alignment (CMach_GetQualifiedTypeAlign at 0x4F2730), so a 4-byte struct of alignment 1 is copied byte by
  byte. Otherwise: CMach_AllocationAlignment, which raises a small aggregate's alignment to its size.
- the alignment of what is allocated (CMach_AllocationAlignment): the stack layout.

The patch takes the legacy branch at the copy sites only, so CMach_AllocationAlignment (and the stack layout)
stays as without the pragma. See ../../docs/wii-retro-compiler-research.md.
"""

import argparse
import hashlib
import shutil
import struct
import sys
from pathlib import Path

FLAG = 0x74F2F9
TEXT_VA, TEXT_RAW = 0x401000, 0x400

# CMach_GetObjectAlignment: `cmp [FLAG], 0; je not_legacy` -> fall through to the legacy path.
NOP_JE = (0x4F4AAF,)
# `cmp [FLAG], 0; jne legacy` -> always jump to the legacy path.
JMP_JNE = (
    0x52346E,  # gen_COND
    0x523627,  # gen_COND
    0x5236B1,  # gen_COND
    0x525DC8,  # gen_ASS
    0x525DF3,  # gen_ASS
    0x57FA97,  # Intrinsics fn_0057f9d0
    0x57FBC7,  # Intrinsics fn_0057fad0
)


def patch(data: bytearray) -> None:
    flag = struct.pack("<I", FLAG)

    def compare(va: int) -> int:
        at = va - TEXT_VA + TEXT_RAW
        if data[at : at + 2] != b"\x80\x3d" or data[at + 2 : at + 6] != flag or data[at + 6] != 0:
            raise SystemExit(f"0x{va:x}: not `cmp byte ptr [0x{FLAG:x}], 0` (not Wii 1.0RC1?)")
        return at + 7

    for va in NOP_JE:
        j = compare(va)
        if data[j : j + 2] != b"\x0f\x84":
            raise SystemExit(f"0x{va:x}: no je after the compare")
        data[j : j + 6] = b"\x90" * 6
    for va in JMP_JNE:
        j = compare(va)
        if data[j] == 0x75:
            data[j] = 0xEB
        elif data[j : j + 2] == b"\x0f\x85":
            data[j : j + 6] = b"\x90\xe9" + struct.pack("<i", struct.unpack_from("<i", data, j + 2)[0] + 1)
        else:
            raise SystemExit(f"0x{va:x}: no jne after the compare")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rc1_dir", help="the Wii 1.0RC1 directory (mwcceppc.exe and its DLLs)")
    parser.add_argument("out_dir", help="where to write the patched copy (the other files are copied as they are)")
    args = parser.parse_args()
    src, out = Path(args.rc1_dir), Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    for f in src.iterdir():
        if f.is_file():
            shutil.copy(f, out / f.name)
    data = bytearray((src / "mwcceppc.exe").read_bytes())
    patch(data)
    (out / "mwcceppc.exe").write_bytes(data)
    print(f"{out / 'mwcceppc.exe'}: {len(NOP_JE) + len(JMP_JNE)} sites, SHA-1 {hashlib.sha1(data).hexdigest()}")


if __name__ == "__main__":
    sys.exit(main())
