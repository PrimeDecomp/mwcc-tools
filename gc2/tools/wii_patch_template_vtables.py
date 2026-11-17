#!/usr/bin/env python3
"""Patch Wii 1.0RC1's mwcceppc.exe to emit class template instances' vtables and virtual functions as soon as
they are needed, the way the Prime games' Wii compiler does.

RC1 emits vtables and instantiates class template members only at the end of the file, so the virtual destructor
of TObjOwnerDerivedFromIObj<T> comes out after every other function. The target compiler emits it right after the
function that first constructed the class. The patch adds a section, `.retrovt`, with a pass
(wii_retro/template_vtables.c, built for i686) that, after each top-level declaration, emits the referenced
vtables of class template instances and instantiates those instances' referenced members. Other classes'
vtables, and the members of templates without a vtable (vector::reserve), still wait for the end of the file.
See ../../docs/wii-retro-compiler-research.md, "when template instances' vtables are emitted".

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
    "RC1_ConstructVTable": 0x552E50,
    "RC1_InitObjectIterator": 0x4F6550,  # CScope_InitObjectIterator
    "RC1_NextObjectIteratorObject": 0x4F64E0,  # CScope_NextObjectIteratorObject
    "RC1_GenerateTypeInfoData": 0x600050,  # CRTTI_GenerateTypeInfoData
    "RC1_ClassInternalError": 0x45B4C0,  # CError_Internal
    "RC1_CClass_source_file": 0x6C51D4,
    "RC1_frontend_errors": 0x76C0A0,
    "RC1_GenClassInstance": 0x5C1140,  # CTempl_Instantiate's call for a class template instance
    "RC1_FreeLocalHeap": 0x47C360,  # CMid_FreeLocalHeap, the call after it
    "RC1_classlist": 0x756FF4,  # CClass_Finalize's two lists
    "RC1_classlist2": 0x751D7C,
    "RC1_templates": 0x751EFC,  # the class templates
}
CALL = 0x44F43E  # CParser_ParseDeclaration: call CMid_GlobalDeclarationActions

# wii_retro/template_vtables.c, entry retro_GlobalDeclarationActions at offset 0.
CODE = bytes.fromhex(
    "5553575683ec18e800000000884424038d7424086666662e0f1f84000000000031ff803d000000000074758b1d000000"
    "0085db7451897c240431edeb0a0f1f008b5b3085db74398b7b42eb070f1f40008b7f3085ff74e9f647230874f3807f41"
    "0075ed837f1a0074e76a0057e80000000083c4080fb6c009c5e800000000ebd08b7c240409ef85ff0f84d6010000e800"
    "000000eb8b66662e0f1f8400000000008b2d0000000031ffeb09660f1f4400008b6d0085ed0f84450100008b5d04f643"
    "230874ec807d0800740f807b2f000f85dc000000e98d000000807b2f01757dff730656e80000000083c4080f1f440000"
    "56e80000000083c40485c00f84700100008078020475e98b481080390775e18b511af6c20474d9f64014107535f6c202"
    "743a8b431a85c07433f6432340752d8b00f6401a040f85450100006a01506a006a0053e80000000083c414bf01000000"
    "eb0a83e2fb89511ac6432f00c6450801807b2f00754a8b431a85c07443f6432340753d8b0085c074370fb7481a83e106"
    "6683f902752a66c740180201804816026a01506a006a0053e80000000083c414bf0100000066662e0f1f840000000000"
    "8b431e85c00f84f5feffff0fb7481a83e1066683f9020f85e4feffff66c7401802018b431e814814000002006a0053ff"
    "731ee80000000083c40cbf01000000e9bcfeffff6666662e0f1f8400000000008b1d00000000eb0a0f1f840000000000"
    "8b1b85db0f8411feffff8b43048b4822f7c10008000074e8f6c10275e3807b080075dd8b481e85c974d60fb7511a83e2"
    "066683fa0275c9804916066a0050ff701ee80000000083c40cc6430801bf01000000ebac0fb644240383c4185e5f5b5d"
    "c368130e00006800000000e80000000068280e00006800000000e800000000"
)
RELOCS = [
    (0x8, "REL32", "RC1_GlobalDeclarationActions"),
    (0x24, "DIR32", "RC1_frontend_errors"),
    (0x2D, "DIR32", "RC1_templates"),
    (0x6D, "REL32", "RC1_GenClassInstance"),
    (0x7A, "REL32", "RC1_FreeLocalHeap"),
    (0x8F, "REL32", "RC1_GlobalDeclarationActions"),
    (0xA2, "DIR32", "RC1_classlist"),
    (0xE4, "REL32", "RC1_InitObjectIterator"),
    (0xF2, "REL32", "RC1_NextObjectIteratorObject"),
    (0x144, "REL32", "RC1_ConstructVTable"),
    (0x199, "REL32", "RC1_ConstructVTable"),
    (0x1E3, "REL32", "RC1_GenerateTypeInfoData"),
    (0x202, "DIR32", "RC1_classlist2"),
    (0x252, "REL32", "RC1_GenerateTypeInfoData"),
    (0x277, "DIR32", "RC1_CClass_source_file"),
    (0x27C, "REL32", "RC1_ClassInternalError"),
    (0x286, "DIR32", "RC1_CClass_source_file"),
    (0x28B, "REL32", "RC1_ClassInternalError"),
]


def main():
    wii_retro_pe.main(__doc__, b".retrovt", CODE, RELOCS, SYMBOLS, CALL, SYMBOLS["RC1_GlobalDeclarationActions"])


if __name__ == "__main__":
    sys.exit(main())
