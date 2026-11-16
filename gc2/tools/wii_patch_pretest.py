#!/usr/bin/env python3
"""Patch Wii 1.0RC1's mwcceppc.exe to leave out loop pretests the way the Prime games' Wii compiler does.

The target compiler drops a loop's pretest (`cmplw rA,rB; beq exit` before the loop's `b test`) when it repeats
the loop's own test and the loop body makes a call. RC1 keeps it. The patch adds a section, `.retro`, with a pass
(wii_retro/drop_pretest.c, built for i686) that deletes such a pretest, and makes assemblefunction call it
before optimizefinalbranches. See ../../docs/wii-retro-compiler.md, "The destroy pretest".

The section also holds a copy of RC1's base relocations with the pass's added, since wrappers such as sjiswrap
load the compiler at another base. Apply it to RC1, or to the output of wii_patch_struct_copy.py.

To rebuild CODE and RELOCS after changing the C source:
    clang --target=i686-pc-windows-msvc -O2 -ffreestanding -fno-jump-tables -mno-sse \
        -fno-asynchronous-unwind-tables -c wii_retro/drop_pretest.c -o drop_pretest.obj
    python tools/wii_patch_pretest.py --from-obj drop_pretest.obj   (prints the two tables)
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import wii_retro_pe  # noqa: E402

# RC1's symbols the pass uses.
SYMBOLS = {
    "RC1_pcbasicblocks": 0x751C88,
    "RC1_deletepcode": 0x59B4C0,
    "RC1_pccomputeoffsets": 0x59B270,
    "RC1_optimizefinalbranches": 0x59CA20,
}
CALL = 0x59C364  # assemblefunction: call optimizefinalbranches

# wii_retro/drop_pretest.c, entry retro_optimizefinalbranches at offset 0.
CODE = bytes.fromhex(
    "5553575683ec148b4424288b350000000085f60f84e102000031ffeb1d8b4424288b3c246666662e0f1f840000000000"
    "8b3685f60f84b702000066837e28027cef8b561866837a280575e58b5a0466837b285575db807a3a0275d5837a3c0275"
    "cf807a2c0075c9807b2c0075c30fb64a2d884c240c3a4b2d75b60fb74a30663b4b3075ac807a480675a666894c240489"
    "3c248b7a4a8b6f0485ed748566837d2800750c8b6d0085ed75f2e972ffffff8b3e85ff0f84350200000fb74f2885c975"
    "0b8b3f85ff75f2e955ffffff897c241083f9010f8548ffffff8b4c24108b491466837928000f8536ffffff80792c060f"
    "852cffffff8b792e8b7f0485ff0f841effffff0fb74f2885c9750b8b3f85ff75f2e90bffffff897c240883f9020f85fe"
    "feffff8b4424088b781466837f28550f85e8feffff8b4424088b481866837928080f85d6feffff80793a020f85ccfeff"
    "ff83793c020f85c2feffff807f2c000f85b8feffff0fb644240c38472d0f85aafeffff0fb7442404663947300f859bfe"
    "ffff807f3a000f8591feffff807b3a000f8587feffff0fb6473b3a433b0f857afeffff0fb7473e663b433e0f856cfeff"
    "ff807f48000f8562feffff807b48000f8558feffff0fb647493a43490f854bfeffff0fb7474c663b434c0f853dfeffff"
    "80792c000f8533feffff0fb644240c38412d0f8525feffff0fb7442404663941300f8516feffff8b4424088b0085c00f"
    "8408feffff668378280074ef39e80f85f9fdffff8b4424108b008944240485c00f84e7fdffff8b4424043b4424080f84"
    "d9fdffffc744240c000000008b7c24040fbf472889c185c00f9fc48b6f1485ed0f95c020e03c0175280fb77d2883ff14"
    "7712b8020018000fa3f87308c744240c0100000083f9027c08498b6d0085ed75d88b4424048b008944240485c0740a8b"
    "4424043b44240875a3837c240c000f95c08b4c24043b4c24080f94c420c480fc018b4424288b3c240f8562fdffff52e8"
    "0000000083c40453e8000000008b44242c83c404bf01000000e942fdffff8b3c2485ff7405e80000000050e800000000"
    "83c4185e5f5b5dc3"
)
RELOCS = [
    (0xD, "DIR32", "RC1_pcbasicblocks"),
    (0x2D0, "REL32", "RC1_deletepcode"),
    (0x2D9, "REL32", "RC1_deletepcode"),
    (0x2F6, "REL32", "RC1_pccomputeoffsets"),
    (0x2FC, "REL32", "RC1_optimizefinalbranches"),
]


def main():
    wii_retro_pe.main(__doc__, b".retro", CODE, RELOCS, SYMBOLS, CALL, SYMBOLS["RC1_optimizefinalbranches"])


if __name__ == "__main__":
    sys.exit(main())
