#!/usr/bin/env python3
"""Patch Wii 1.0RC1's mwcceppc.exe to emit class template instances' vtables and virtual functions as soon as
they are needed, the way the Prime games' Wii compiler does.

RC1 emits vtables and instantiates class template members only at the end of the file, so the virtual destructor
of TObjOwnerDerivedFromIObj<T> comes out after every other function. The target compiler emits it right after the
function that first constructed the class. The patch adds a section, `.retrovt`, with a pass
(wii_retro/template_vtables.c, built for i686) that, after each top-level declaration, emits the referenced
vtables of class template instances and instantiates those instances' referenced members. Other classes'
vtables, and the members of templates without a vtable (vector::reserve), still wait for the end of the file.
See ../../docs/wii-retro-compiler.md, "Template instances' vtables".

Apply it to RC1, or to the output of wii_patch_struct_copy.py or wii_patch_pretest.py.

To rebuild CODE and RELOCS after changing the C source:
    clang --target=i686-pc-windows-msvc -O2 -ffreestanding -fno-jump-tables -mno-sse \
        -fno-asynchronous-unwind-tables -mstack-probe-size=65536 -c wii_retro/template_vtables.c -o tv.obj
    python tools/wii_patch_template_vtables.py --from-obj tv.obj   (prints the two tables)
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import wii_retro_pe  # noqa: E402

# RC1's symbols the pass uses.
SYMBOLS = {
    "RC1_GlobalDeclarationActions": 0x47C120,  # CMid_GlobalDeclarationActions
    "RC1_Finalize": 0x552C20,  # CClass_Finalize
    "RC1_GenClassInstance": 0x5C1140,  # CTempl_Instantiate's call for a class template instance
    "RC1_InstanceFlush": 0x47C360,  # and the call after it
    "RC1_classlist": 0x756FF4,  # CClass_Finalize's two lists
    "RC1_classlist2": 0x751D7C,
    "RC1_templates": 0x751EFC,  # the class templates
}
CALL = 0x44F43E  # CParser_ParseDeclaration: call CMid_GlobalDeclarationActions

# wii_retro/template_vtables.c, entry retro_GlobalDeclarationActions at offset 0.
CODE = bytes.fromhex(
    "5553575681ec14400000e80000000089442408666666662e0f1f8400000000008b0d00000000a1000000008944240c85"
    "c9894c2410745931c00f1f8000000000898c84142000008b0985c97454898c84182000008b0985c9744c898c841c2000"
    "008b0985c97447898c84202000008b0985c9744483c0043d0008000075c231ffe9dc02000066662e0f1f840000000000"
    "b900000000c7042400000000e9af0000008d4801eb1a89c14083c102eb1289c183c00283c903eb0889c183c00383c104"
    "890c2485c0745f8d680183e5feb90000000031d2eb11662e0f1f84000000000083c20239d574398bb494142000008b5e"
    "04f6432308740989318b8c94142000008bb494182000008b5e04f643230874d089318b8c9418200000ebc50f1f440000"
    "a8017409eb2ab90000000031d28b8494142000008b7004f6462308741389018b8c9414200000662e0f1f840000000000"
    "c701000000008b0d0000000085c9746031c066666666662e0f1f840000000000894c84148b0985c97458894c84188b09"
    "85c97453894c841c8b0985c97451894c84208b0985c9745183c0043d0008000075ce31ff8b1c2485db0f84b2010000c7"
    "442404ffffffff31c0e9b90000006690c744240400000000b900000000e98e0000008d4801eb1a89c14083c102eb1289"
    "c183c00283c903eb0889c183c00383c104894c240485c0744d8d680183e5feb90000000031d2eb0f0f1f840000000000"
    "83c20239d574298b7494148b5e04f6432308740689318b4c94148b7494188b5e04f643230874d989318b4c9418ebd190"
    "a8017409eb1ab90000000031d28b4494148b7004f6462308740689018b4c9414c70100000000e8000000000fb6f8b001"
    "8b1c2485db747c31c9ba0000000083fb01745583e3fe31edeb16660f1f4400008bacac1820000089750089d539d37430"
    "8d7501ba000000003b342473078b94ac182000008bb4ac1420000089168d550231f63b142473c98bb4ac1c200000ebc0"
    "8b1c24f6c30174178d720139de73078b8c94182000008b949414200000890a84c0746e8b54240485d2746631c0b90000"
    "000083fa01744783e2fe31dbeb0e66908b5c9c18893389cb39ca74298d7301b9000000003b74240473048b4c9c188b74"
    "9c14890e8d4b0231f63b4c240473d18b749c1cebcb8b542404f6c201741389d68d510139f273048b448c188b4c8c1489"
    "018b442410a3000000008b44240ca3000000008b350000000085f67458893c2431dbbf01000000eb0e0f1f8000000000"
    "8b763085f674398b6e42eb070f1f40008b6d3085ed74e9f645230874f3807d410075ed837d1a0074e76a0055e8000000"
    "0083c40884c00f45dfe800000000ebd08b3c2409df85ff740ae800000000e93dfcffff8b44240881c4144000005e5f5b"
    "5dc3"
)
RELOCS = [
    (0xB, "REL32", "RC1_GlobalDeclarationActions"),
    (0x22, "DIR32", "RC1_classlist"),
    (0x27, "DIR32", "RC1_classlist2"),
    (0x91, "DIR32", "RC1_classlist"),
    (0xCE, "DIR32", "RC1_classlist"),
    (0x127, "DIR32", "RC1_classlist"),
    (0x158, "DIR32", "RC1_classlist2"),
    (0x1C9, "DIR32", "RC1_classlist2"),
    (0x200, "DIR32", "RC1_classlist2"),
    (0x247, "DIR32", "RC1_classlist2"),
    (0x267, "REL32", "RC1_Finalize"),
    (0x366, "DIR32", "RC1_classlist"),
    (0x36F, "DIR32", "RC1_classlist2"),
    (0x375, "DIR32", "RC1_templates"),
    (0x3BD, "REL32", "RC1_GenClassInstance"),
    (0x3CA, "REL32", "RC1_InstanceFlush"),
    (0x3DA, "REL32", "RC1_GlobalDeclarationActions"),
]


def main():
    wii_retro_pe.main(__doc__, b".retrovt", CODE, RELOCS, SYMBOLS, CALL, SYMBOLS["RC1_GlobalDeclarationActions"])


if __name__ == "__main__":
    sys.exit(main())
