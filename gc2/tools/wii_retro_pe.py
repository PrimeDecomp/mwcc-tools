"""Add a pass (the .text of a COFF object built for i686) to Wii 1.0RC1's mwcceppc.exe as a new section, and point
one of RC1's calls at it. Used by wii_patch_pretest.py and wii_patch_template_vtables.py; the passes' C sources
are in wii_retro/.

A pass's code refers to RC1 by symbols named RC1_*: a call is resolved to a relative displacement, a data address
to an absolute one. The new section also holds a copy of the executable's base relocations with the pass's
absolute addresses added, since wrappers such as sjiswrap load the compiler at another base.
"""

import argparse
import hashlib
import shutil
import struct
import textwrap
from pathlib import Path

TEXT_VA, TEXT_RAW = 0x401000, 0x400
BUILD = (
    "clang --target=i686-pc-windows-msvc -O2 -ffreestanding -fno-jump-tables -mno-sse "
    "-fno-asynchronous-unwind-tables -mstack-probe-size=65536 -c"
)


def read_obj(path: Path):
    """The .text of a COFF object and its relocations, [(offset, "DIR32" or "REL32", symbol)]."""
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

    code, rel = None, []
    for i in range(nsec):
        h = 20 + i * 40
        name = o[h : h + 8].rstrip(b"\0")
        size, ptr, rptr = struct.unpack_from("<III", o, h + 16)
        if name in (b".data", b".bss", b".rdata") and size:
            raise SystemExit(f"{path}: {name.decode()} is not empty; a pass may only have code")
        if name == b".text":
            nrel = struct.unpack_from("<H", o, h + 32)[0]
            code = o[ptr : ptr + size]
            for k in range(nrel):
                off, si, typ = struct.unpack_from("<IIH", o, rptr + k * 10)
                rel.append((off, {6: "DIR32", 20: "REL32"}[typ], symname(si).lstrip("_")))
    return code, rel


def print_tables(path: Path) -> None:
    """Print CODE and RELOCS, to embed in a patcher, from a compiled pass."""
    code, rel = read_obj(path)
    print("CODE = bytes.fromhex(\n" + "\n".join(f'    "{x}"' for x in textwrap.wrap(code.hex(), 96)) + "\n)")
    print("RELOCS = [")
    for off, kind, sym in rel:
        print(f'    (0x{off:X}, "{kind}", "{sym}"),')
    print("]")


def add_section(d: bytearray, name: bytes, code, relocs, symbols, call: int, callee: int) -> None:
    """Add CODE (entry at offset 0) as section NAME, and make the call at CALL, now to CALLEE, call it."""
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    n = struct.unpack_from("<H", d, pe + 6)[0]
    optsize = struct.unpack_from("<H", d, pe + 20)[0]
    oh = pe + 24
    base = struct.unpack_from("<I", d, oh + 28)[0]
    salign, falign = struct.unpack_from("<II", d, oh + 32)
    sizeofimage = struct.unpack_from("<I", d, oh + 56)[0]
    sizeofheaders = struct.unpack_from("<I", d, oh + 60)[0]
    sh = oh + optsize + n * 40
    if any(d[oh + optsize + k * 40 : oh + optsize + k * 40 + 8].rstrip(b"\0") == name for k in range(n)):
        raise SystemExit(f"already patched ({name.decode()})")
    if d[sh : sh + 40] != bytes(40) or sh + 40 > sizeofheaders:
        raise SystemExit("no room for another section header")
    at = call - TEXT_VA + TEXT_RAW
    if d[at] != 0xE8 or struct.unpack_from("<i", d, at + 1)[0] + call + 5 != callee:
        raise SystemExit(f"0x{call:x}: not a call to 0x{callee:x} (not Wii 1.0RC1?)")

    rva = sizeofimage
    code = bytearray(code)
    new_relocs = []
    for off, kind, sym in relocs:
        target = symbols[sym]
        if kind == "DIR32":
            struct.pack_into("<I", code, off, target + struct.unpack_from("<I", code, off)[0])
            new_relocs.append(rva + off)
        else:
            struct.pack_into("<i", code, off, target - (base + rva + off + 4))
    code += bytes(-len(code) % 4)

    # Base relocations: the executable's, then a block per page for the pass's.
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
    d[sh : sh + 40] = struct.pack("<8sIIIIIIHHI", name, len(body), rva, rawsize, raw, 0, 0, 0, 0, 0x60000020)
    struct.pack_into("<H", d, pe + 6, n + 1)
    struct.pack_into("<I", d, oh + 56, (rva + len(body) + salign - 1) // salign * salign)
    struct.pack_into("<II", d, dd, rva + len(code), len(table))
    struct.pack_into("<i", d, at + 1, base + rva - (call + 5))


def main(doc: str, name: bytes, code, relocs, symbols, call: int, callee: int) -> None:
    """A patcher's command line: copy an RC1 directory and add the pass to its mwcceppc.exe."""
    parser = argparse.ArgumentParser(description=doc, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rc1_dir", nargs="?", help="a Wii 1.0RC1 directory (mwcceppc.exe and its DLLs)")
    parser.add_argument("out_dir", nargs="?", help="where to write the patched copy (may be rc1_dir itself)")
    parser.add_argument("--from-obj", type=Path, help="print CODE and RELOCS from the compiled pass")
    args = parser.parse_args()
    if args.from_obj:
        print_tables(args.from_obj)
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
    add_section(data, name, code, relocs, symbols, call, callee)
    (out / "mwcceppc.exe").write_bytes(data)
    print(f"{out / 'mwcceppc.exe'}: SHA-1 {hashlib.sha1(data).hexdigest()}")
