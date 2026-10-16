"""Freeze COFF auxiliary records, hook rewrites, metadata and x86 branch scanning."""
import argparse
import importlib
import json
from pathlib import Path
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "tests/fixtures/binary"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.oracle_root / "tools"))
    split = importlib.import_module("split")
    compare = importlib.import_module("compare")
    link = importlib.import_module("link")
    coff = split.Coff()
    # Section symbols carry auxiliary entries; relocation indices count those slots.
    coff.section(".text", 0x60500020, b"\xe8\0\0\0\0\xc3", 6, [])
    entry = coff.add_symbol("_entry", 0, 1, 0x20, 2)
    external = coff.external("_external_with_a_long_name")
    coff.sections[0] = (".text", 0x60500020, b"\xe8\0\0\0\0\xc3", 6, [(1, entry, 20)])
    coff.section(".bss", 0xc0300080, b"", 16, [])
    coff.section(".data", 0xc0300040, b"\x04\0\0\0\0\0\0\0", 8, [(0, entry, 6), (4, entry, 6)])
    coff.section(".debug$long_section", 0x42000040, b"debug", 5, [])
    weak_aux = struct.pack("<II10x", entry, 3)
    weak = coff.add_symbol("_weak", 0, 0, 0, 105, weak_aux)
    coff.write(OUT / "auxiliary.obj")
    link.undefine_symbols(OUT / "auxiliary.obj", OUT / "undefined.obj", {"_entry"})
    link.hook_symbols(OUT / "auxiliary.obj", OUT / "hooked.obj", {"_entry"})
    symbols, sections = compare.read_object(OUT / "auxiliary.obj")
    expected = {"indices": {"entry": entry, "external": external, "weak": weak},
                "symbols": symbols,
                "sections": [dict(s, data=list(s["data"])) for s in sections],
                "defined": sorted(link.defined_symbols(OUT / "auxiliary.obj")),
                "sizes": link.section_sizes(OUT / "auxiliary.obj")}
    (OUT / "auxiliary.json").write_text(json.dumps(expected, indent=2)+"\n")
    cases = []
    for two, table in ((False, split.ONE_BYTE), (True, split.TWO_BYTE)):
        for op, (modrm, immediate) in sorted(table.items()):
            if not two and (op in split.PREFIXES or op == 0x0f):
                continue
            for prefix in (b"", b"\x66", b"\x67", b"\x66\x67"):
                for mode in ((0xc0, 0xd0, 0x04, 0x05, 0x06, 0x44, 0x85) if modrm else (None,)):
                    code = bytearray(prefix + (b"\x0f" if two else b"") + bytes([op]))
                    size = immediate
                    if modrm:
                        code.append(mode)
                        if immediate == "grp3":
                            size = (1 if op == 0xf6 else "z") if (mode >> 3) & 7 in (0, 1) else 0
                        mod, rm = mode >> 6, mode & 7
                        if b"\x67" in prefix:
                            code += bytes({0: 2 if rm == 6 else 0, 1: 1, 2: 2, 3: 0}[mod])
                        else:
                            if mod != 3 and rm == 4:
                                code += b"\x25" + bytes(4 if mod == 0 else 0)
                            code += bytes({0: 4 if rm == 5 else 0, 1: 1, 2: 4, 3: 0}[mod])
                    relative = not two and op in (0xe8, 0xe9) or two and 0x80 <= op <= 0x8f
                    amount = (2 if b"\x66" in prefix and not relative else 4) if size == "z" else (2 if b"\x67" in prefix else 4) if size == "a" else 6 if size == "p" else size
                    code += bytes(amount)
                    code += b"\xe8\x04\0\0\0\xc3"
                    cases.append({"code": list(code), "base": 0x401000, "branches": split.branches(code, 0x401000)})
    (OUT / "branches.json").write_text(json.dumps(cases, indent=2)+"\n")
    # Small metadata fixture covers option order, booleans, local labels and COMMON ranges.
    symbol_text = "_entry = .text:0x00401000; // type:function size:0x6\nlocal_label = .text:0x00401002; // type:label scope:local\n_data = .data:0x00402000; // type:object size:0x4 scope:local force_active\n"
    split_text = "Sections:\n\t.text type:code align:16\n\t.data type:data align:4\n\t.bss type:bss align:8\nsrc/unit.c:\n\t.text start:0x00401000 end:0x00401006\n\t.data start:0x00402000 end:0x00402004 rename:.data$A\ncommon:\n\t.bss start:0x00403000 end:0x00403008 common:true\n"
    (OUT / "symbols.txt").write_text(symbol_text)
    (OUT / "splits.txt").write_text(split_text)
    with tempfile.TemporaryDirectory() as temporary:
        p = Path(temporary)
        (p / "symbols.txt").write_text(symbol_text)
        (p / "splits.txt").write_text(split_text)
        symbols = split.read_symbols(p / "symbols.txt")
        sections, units = split.read_splits(p / "splits.txt")
    expected = {"symbols": [{"name": n, "section": sec, "address": at, "options": opts} for n,sec,at,opts in symbols],
                "splits": {"sections": sections, "units": [{"name": n, "ranges": [{"section": s, "start": a, "end": b, "options": o} for s,a,b,o in rs]} for n,rs in units]},
                "common_names": split.common_names(2),
                "common_layout": split.common_layout([("_one", 1), ("_word", 4), ("_double", 8), ("_odd", 3)], 0x403001)}
    (OUT / "metadata.json").write_text(json.dumps(expected, indent=2)+"\n")
    suffix_cases = []
    for seed in (0, 1, 255, 1023, 3454, 4095):
        for threshold in (None, 0, 1000, 3455, 4090):
            for suffix in range(10000):
                h = seed
                for c in str(suffix).encode():
                    h = (h << 1) ^ c
                    h = (h & 4095) ^ (h >> 12)
                if threshold is None or h > threshold:
                    suffix_cases.append({"seed": seed, "threshold": threshold, "suffix": str(suffix), "hash": h})
                    break
    (OUT / "common-suffixes.json").write_text(json.dumps(suffix_cases, indent=2)+"\n")
    print(f"Captured {len(cases)} x86 scanner cases and COFF construction/patch contracts")


if __name__ == "__main__":
    main()
