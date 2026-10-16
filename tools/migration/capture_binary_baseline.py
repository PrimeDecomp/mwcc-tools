"""Capture PE mapping, COFF/import-library and resource contracts on synthetic data."""
import argparse
from dataclasses import asdict
import importlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/fixtures/binary"


def synthetic_pe():
    image = bytearray(0x800)
    image[:2] = b"MZ"
    struct.pack_into("<I", image, 0x3c, 0x80)
    image[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", image, 0x84, 0x14c, 3, 0, 0, 0, 224, 0)
    struct.pack_into("<H", image, 0x98, 0x10b)
    struct.pack_into("<I", image, 0xb4, 0x400000)
    for i, (name, rva, vsize, offset, flags) in enumerate(((b".text", 0x1000, 16, 0x200, 0x60000020),
                                                         (b".data", 0x2000, 0x300, 0x400, 0xc0000040),
                                                         (b".rsrc", 0x3000, 0x200, 0x600, 0x40000040))):
        struct.pack_into("<8sIIIIIIHHI", image, 0x178+40*i, name, vsize, rva, 0x200, offset, 0, 0, 0, 0, flags)
    image[0x200:0x206] = b"alpha\0"
    image[0x400:0x413] = b"alpha\0alphabet\0alpha"
    resources = bytearray(0x200)
    next_offset = 0
    def reserve(size):
        nonlocal next_offset
        at = next_offset
        next_offset += size
        return at
    # Distinct directory order and payload order; two leaves deliberately share a payload.
    tree = {"TYPE": {"first": {1033: (0x190, 65001, b"abc")}},
            10: {20: {0: (0x180, 0, b"12345"), 1033: (0x180, 1252, b"12345")}}}
    def label(name):
        if isinstance(name, int):
            return name
        raw = name.encode("utf-16-le")
        at = reserve(2+len(raw))
        struct.pack_into("<H", resources, at, len(raw)//2)
        resources[at+2:at+2+len(raw)] = raw
        return 0x80000000 | at
    def directory(tree):
        entries = sorted(tree.items(), key=lambda e: (isinstance(e[0], int), e[0]))
        at = reserve(16+8*len(entries))
        named = sum(isinstance(n, str) for n in tree)
        struct.pack_into("<HH", resources, at+12, named, len(tree)-named)
        for i, (name, value) in enumerate(entries):
            key = label(name)
            if isinstance(value, dict):
                target = 0x80000000 | directory(value)
            else:
                payload, page, raw = value
                target = reserve(16)
                struct.pack_into("<IIII", resources, target, 0x3000+payload, len(raw), page, 0)
                resources[payload:payload+len(raw)] = raw
            struct.pack_into("<II", resources, at+16+8*i, key, target)
        return at
    assert directory(tree) == 0
    assert next_offset < 0x180
    image[0x600:] = resources
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.oracle_root / "tools"))
    pe_module = importlib.import_module("pe")
    implib = importlib.import_module("implib")
    resource = importlib.import_module("rsrc2res")
    FIXTURES.mkdir(parents=True, exist_ok=True)
    path = FIXTURES / "synthetic.pe"
    path.write_bytes(synthetic_pe())
    pe = pe_module.PEFile(path)
    reads = [{"address": address, "size": size, "bytes": list(pe.read(address, size))}
             for address, size in ((0x401000, 6), (0x402000, 19), (0x4021ff, 1), (0x403180, 5))]
    finds = [{"bytes": list(needle), "limit": limit, "addresses": pe.find(needle, limit)}
             for needle in (b"alpha", b"aba", b"", b"\0\0", b"not there") for limit in (None, 0, 1, 3)]
    entries = resource.resources(pe, next(s for s in pe.sections if s.name == ".rsrc"))
    path.with_suffix(".res").write_bytes(resource.res_file(entries))
    expected = {"image_base": pe.image_base, "sections": [asdict(s) for s in pe.sections], "reads": reads, "finds": finds,
                "resources": [{"kind": kind, "name": name, "language": language, "codepage": page, "data": list(data)}
                              for kind, name, language, page, data in entries]}
    (FIXTURES / "pe.json").write_text(json.dumps(expected, indent=2)+"\n")
    sections = [(".text", 0x60500020, bytes([0xe8,0,0,0,0,0xc3]), [(1,1,0x14)]), (".data", 0xc0300040, b"hello\0", [])]
    symbols = [("_entry", 0, 1, 2), ("_external_with_a_long_name", 0, 0, 2)]
    (FIXTURES / "coff-input.json").write_text(json.dumps({"sections": [{"name": n, "flags": f, "data": list(d),
        "relocations": [{"offset": o, "symbol": s, "kind": k} for o,s,k in rs]} for n,f,d,rs in sections],
        "symbols": [{"name": n, "value": v, "section": s, "storage": c} for n,v,s,c in symbols]}, indent=2)+"\n")
    (FIXTURES / "synthetic.obj").write_bytes(implib.coff(sections, symbols))
    libraries = []
    for name, text in (("license", (ROOT / "config/lmgr326b.def").read_text()),
                       ("named", "LIBRARY sample\nEXPORTS\n    foo@8 @3\n    by_ordinal @7 NONAME\n")):
        import re
        dll = re.search(r"^LIBRARY\s+(\S+)", text, re.M)[1]
        if "." not in dll:
            dll += ".dll"
        exports = [(m[1], int(m[2]), not m[3]) for m in re.finditer(r"^\s+([\w@]+)\s+@(\d+)(\s+NONAME)?\s*$", text, re.M)]
        (FIXTURES / f"{name}.def").write_text(text)
        (FIXTURES / f"{name}.lib").write_bytes(implib.library(dll, exports))
        libraries.append({"name": name, "dll": dll, "exports": [{"symbol": s, "ordinal": o, "named": n} for s,o,n in exports]})
    (FIXTURES / "libraries.json").write_text(json.dumps(libraries, indent=2)+"\n")
    print("Captured synthetic PE, COFF, resource and import-library references")


if __name__ == "__main__":
    main()
