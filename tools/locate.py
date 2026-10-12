#!/usr/bin/env python3
"""python tools/locate.py VERSION SOURCE...: where the functions a source compiles to are in the version's executable.

Each function of build/VERSION/compiled/SOURCE.obj is searched for in the executable's code by its bytes, the operands
the object relocates wildcarded (so a function that is exact is found at its address, whatever the symbols it names). The
report lists, for each function, its address (or that it is not found, or ambiguous) and the unit config/VERSION/splits.txt
puts that address in, and says whether the source's functions lie in the range of the unit named like it."""
import re
import struct
import sys
from bisect import bisect_right
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from pe import PEFile
from split import read_splits


def folder(version):
    return version if version.startswith("GC_") else "GC_" + version.replace(".", "_")


def functions_of(path):
    """[(name, bytes, set of relocated byte offsets)] of an object's code, a section per function."""
    data = Path(path).read_bytes()
    count = struct.unpack_from("<H", data, 2)[0]
    symptr, symcount = struct.unpack_from("<II", data, 8)
    strings = symptr + 18 * symcount
    symbols = []
    i = 0
    while i < symcount:
        at = symptr + 18 * i
        raw = data[at:at + 8]
        value, section, kind, storage, aux = struct.unpack_from("<IhHBB", data, at + 8)
        if raw[:4] == bytes(4):
            offset = struct.unpack_from("<I", raw, 4)[0]
            name = data[strings + offset:data.index(bytes(1), strings + offset)].decode("latin-1")
        else:
            name = raw.rstrip(bytes(1)).decode("latin-1")
        symbols.append((name, value, section, kind, storage))
        i += 1 + aux
    out = []
    for k in range(count):
        header = 20 + 40 * k
        flags = struct.unpack_from("<I", data, header + 36)[0]
        if not flags & 0x20:
            continue
        size, offset, reloc, _, nreloc = struct.unpack_from("<IIIIH", data, header + 16)
        if not size:
            continue
        wild = set()
        for r in range(nreloc):
            at = struct.unpack_from("<I", data, reloc + 10 * r)[0]
            wild.update(range(at, at + 4))
        names = [n for n, v, s, kind, st in symbols if s == k + 1 and v == 0 and st in (2, 3) and kind & 0xF0 == 0x20]
        out.append((names[0] if names else f"section{k + 1}", data[offset:offset + size], wild))
    return out


def relocations_of(path):
    """{section name of a function: [(offset, symbol name, type)]} keyed like functions_of's order: a list parallel to
    functions_of(path), each entry the relocations (offset in the function, target symbol, COFF type) of a function."""
    data = Path(path).read_bytes()
    count = struct.unpack_from("<H", data, 2)[0]
    symptr, symcount = struct.unpack_from("<II", data, 8)
    strings = symptr + 18 * symcount
    names = {}
    i = 0
    while i < symcount:
        at = symptr + 18 * i
        raw = data[at:at + 8]
        aux = data[at + 17]
        if raw[:4] == bytes(4):
            offset = struct.unpack_from("<I", raw, 4)[0]
            names[i] = data[strings + offset:data.index(bytes(1), strings + offset)].decode("latin-1")
        else:
            names[i] = raw.rstrip(bytes(1)).decode("latin-1")
        i += 1 + aux
    out = []
    for k in range(count):
        header = 20 + 40 * k
        flags = struct.unpack_from("<I", data, header + 36)[0]
        size, offset, reloc, _, nreloc = struct.unpack_from("<IIIIH", data, header + 16)
        if not flags & 0x20 or not size:
            continue
        entries = []
        for r in range(nreloc):
            at, index, kind = struct.unpack_from("<IIH", data, reloc + 10 * r)
            addend = struct.unpack_from("<i", data, offset + at)[0]
            entries.append((at, names.get(index, "?"), kind, addend))
        out.append(entries)
    return out


def bindings_from(version, source):
    """{symbol: address} the exact functions of a compiled source say: where a function is found in the executable, each
    relocated operand holds the address of the symbol its object names there."""
    import json
    config = json.loads(Path(f"config/{folder(version)}/config.json").read_text())
    pe = PEFile(Path(config["original"]))
    text = next(s for s in pe.sections if s.name == ".text")
    blob = pe.read(text.virtual_address, text.virtual_size)
    path = Path(f"build/{folder(version)}/compiled/{source}.obj")
    found = {}
    conflicts = set()
    for (name, code, wild), relocs in zip(functions_of(path), relocations_of(path)):
        places = find(blob, code, wild, limit=2)
        if len(places) != 1:
            continue
        base = text.virtual_address + places[0]
        for at, symbol, kind, addend in relocs:
            value = struct.unpack_from("<I", blob, places[0] + at)[0]
            if kind == 6:
                address = value - addend
            elif kind == 20:
                address = (value + base + at + 4 - addend) & 0xFFFFFFFF
            else:
                continue
            if found.setdefault(symbol, address) != address:
                conflicts.add(symbol)
    return {s: a for s, a in found.items() if s not in conflicts}


def find(blob, code, wild, limit=4):
    runs, run = [], None
    for i in range(len(code)):
        if i in wild:
            run = None
        elif run is None:
            run = [i, i + 1]
            runs.append(run)
        else:
            run[1] = i + 1
    if not runs:
        return []
    anchor = max(runs, key=lambda r: r[1] - r[0])
    literal = code[anchor[0]:anchor[1]]
    if len(literal) < 4:
        return []
    out, position = [], 0
    while len(out) < limit:
        at = blob.find(literal, position)
        if at < 0:
            break
        position = at + 1
        start = at - anchor[0]
        if start < 0 or start + len(code) > len(blob):
            continue
        window = blob[start:start + len(code)]
        if all(i in wild or window[i] == code[i] for i in range(len(code))):
            out.append(start)
    return out


def main():
    version, *sources = sys.argv[1:]
    import json
    config = json.loads(Path(f"config/{folder(version)}/config.json").read_text())
    pe = PEFile(Path(config["original"]))
    text = next(s for s in pe.sections if s.name == ".text")
    blob = pe.read(text.virtual_address, text.virtual_size)
    _, units = read_splits(f"config/{folder(version)}/splits.txt")
    ranges = sorted((start, end, unit) for unit, sections in units for section, start, end, _ in sections
                    if section == ".text")
    starts = [r[0] for r in ranges]

    def unit_at(address):
        k = bisect_right(starts, address) - 1
        return ranges[k][2] if k >= 0 and address < ranges[k][1] else None

    for source in sources:
        path = Path(f"build/{folder(version)}/compiled/{source}.obj")
        print(f"{source}:")
        own = [r for r in ranges if r[2] == source]
        inside = outside = missing = 0
        for name, code, wild in functions_of(path):
            found = find(blob, code, wild)
            if not found:
                missing += 1
                print(f"  {name:48} {len(code):6}  not found")
                continue
            address = text.virtual_address + found[0]
            unit = unit_at(address)
            mark = "" if unit == source else f"  <- {unit}"
            inside += unit == source
            outside += unit != source
            print(f"  {name:48} {len(code):6}  0x{address:08x}{'  (ambiguous)' if len(found) > 1 else ''}{mark}")
        print(f"  {inside} in its unit, {outside} in others, {missing} not found; unit range "
              + ", ".join(f"0x{s:08x}-0x{e:08x}" for s, e, _ in own))


if __name__ == "__main__":
    main()
