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

import argparse
import hashlib
import shutil
import struct
import sys
import textwrap
from pathlib import Path

# RC1's symbols the pass uses.
SYMBOLS = {
    "RC1_pcbasicblocks": 0x751C88,
    "RC1_deletepcode": 0x59B4C0,
    "RC1_pccomputeoffsets": 0x59B270,
    "RC1_optimizefinalbranches": 0x59CA20,
}
CALL = 0x59C364  # assemblefunction: call optimizefinalbranches
TEXT_VA, TEXT_RAW = 0x401000, 0x400

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


def from_obj(path: Path) -> None:
    """Print CODE and RELOCS from a COFF object of drop_pretest.c."""
    o = path.read_bytes()
    nsec = struct.unpack_from("<H", o, 2)[0]
    symptr, nsym = struct.unpack_from("<II", o, 8)
    strtab = symptr + nsym * 18

    def symname(i):
        e = symptr + i * 18
        if o[e : e + 4] == bytes(4):
            off = struct.unpack_from("<I", o, e + 4)[0]
            s = o[strtab + off :]
            return s[: s.index(b"\0")].decode()
        return o[e : e + 8].rstrip(b"\0").decode()

    for i in range(nsec):
        h = 20 + i * 40
        if o[h : h + 8].rstrip(b"\0") == b".text":
            size, ptr, rptr = struct.unpack_from("<III", o, h + 16)
            nrel = struct.unpack_from("<H", o, h + 32)[0]
            code = o[ptr : ptr + size]
            rel = [struct.unpack_from("<IIH", o, rptr + k * 10) for k in range(nrel)]
    print("CODE = bytes.fromhex(\n" + "\n".join(f'    "{x}"' for x in textwrap.wrap(code.hex(), 96)) + "\n)")
    print("RELOCS = [")
    for off, si, typ in rel:
        kind = {6: "DIR32", 20: "REL32"}[typ]
        print(f'    (0x{off:X}, "{kind}", "{symname(si).lstrip("_")}"),')
    print("]")


def patch(d: bytearray) -> None:
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    n = struct.unpack_from("<H", d, pe + 6)[0]
    optsize = struct.unpack_from("<H", d, pe + 20)[0]
    oh = pe + 24
    base = struct.unpack_from("<I", d, oh + 28)[0]
    salign, falign = struct.unpack_from("<II", d, oh + 32)
    sizeofimage = struct.unpack_from("<I", d, oh + 56)[0]
    sizeofheaders = struct.unpack_from("<I", d, oh + 60)[0]
    sh = oh + optsize + n * 40
    if any(d[oh + optsize + k * 40 : oh + optsize + k * 40 + 8].rstrip(b"\0") == b".retro" for k in range(n)):
        raise SystemExit("already patched")
    if d[sh : sh + 40] != bytes(40) or sh + 40 > sizeofheaders:
        raise SystemExit("no room for another section header")
    at = CALL - TEXT_VA + TEXT_RAW
    if d[at] != 0xE8 or struct.unpack_from("<i", d, at + 1)[0] + CALL + 5 != SYMBOLS["RC1_optimizefinalbranches"]:
        raise SystemExit(f"0x{CALL:x}: not `call optimizefinalbranches` (not Wii 1.0RC1?)")

    rva = sizeofimage
    code = bytearray(CODE)
    new_relocs = []
    for off, kind, sym in RELOCS:
        target = SYMBOLS[sym]
        if kind == "DIR32":
            struct.pack_into("<I", code, off, target)
            new_relocs.append(rva + off)
        else:
            struct.pack_into("<i", code, off, target - (base + rva + off + 4))
    code += bytes(-len(code) % 4)

    # Base relocations: RC1's, then a block per page for the pass's.
    dd = oh + 96 + 5 * 8
    reloc_rva, reloc_size = struct.unpack_from("<II", d, dd)
    reloc_off = None
    for k in range(n):
        h = oh + optsize + k * 40
        vsize, va, rsize, raw = struct.unpack_from("<IIII", d, h + 8)
        if va <= reloc_rva < va + vsize:
            reloc_off = raw + reloc_rva - va
    table = bytearray(d[reloc_off : reloc_off + reloc_size])
    pages = {}
    for r in new_relocs:
        pages.setdefault(r & ~0xFFF, []).append(r & 0xFFF)
    for page, offs in sorted(pages.items()):
        entries = [0x3000 | o for o in offs]
        if len(entries) % 2:
            entries.append(0)
        table += struct.pack("<II", page, 8 + 2 * len(entries)) + struct.pack(f"<{len(entries)}H", *entries)

    body = bytes(code) + bytes(table)
    raw = (len(d) + falign - 1) // falign * falign
    rawsize = (len(body) + falign - 1) // falign * falign
    d += bytes(raw - len(d)) + body + bytes(rawsize - len(body))
    d[sh : sh + 40] = struct.pack("<8sIIIIIIHHI", b".retro", len(body), rva, rawsize, raw, 0, 0, 0, 0, 0x60000020)
    struct.pack_into("<H", d, pe + 6, n + 1)
    struct.pack_into("<I", d, oh + 56, (rva + len(body) + salign - 1) // salign * salign)
    struct.pack_into("<II", d, dd, rva + len(code), len(table))
    struct.pack_into("<i", d, at + 1, base + rva - (CALL + 5))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rc1_dir", nargs="?", help="a Wii 1.0RC1 directory (mwcceppc.exe and its DLLs)")
    parser.add_argument("out_dir", nargs="?", help="where to write the patched copy (may be rc1_dir itself)")
    parser.add_argument("--from-obj", type=Path, help="print CODE and RELOCS from a compiled drop_pretest.c")
    args = parser.parse_args()
    if args.from_obj:
        from_obj(args.from_obj)
        return
    if not args.rc1_dir or not args.out_dir:
        parser.error("rc1_dir and out_dir are required")
    src, out = Path(args.rc1_dir), Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    if src.resolve() != out.resolve():
        for f in src.iterdir():
            if f.is_file():
                shutil.copy(f, out / f.name)
    data = bytearray((src / "mwcceppc.exe").read_bytes())
    patch(data)
    (out / "mwcceppc.exe").write_bytes(data)
    print(f"{out / 'mwcceppc.exe'}: SHA-1 {hashlib.sha1(data).hexdigest()}")


if __name__ == "__main__":
    sys.exit(main())
