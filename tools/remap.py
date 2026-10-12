#!/usr/bin/env python3
"""python tools/remap.py FROM TO: carry a version's function list and bindings over to the next version of the compiler.

A function of FROM is found again in TO by its bytes with the relocated operands wildcarded (the absolute fixups of the
base relocation table and the rel32 displacements of calls and jumps), searched near where the previous functions
landed, since a version's code mostly moves as a block. A function that is not found is a function the two versions
differ in (or one that is not listed). The data FROM binds is carried over by the operands the matched functions
relocate (the same operand holds the address in FROM and in TO), and the rest by the distance between the nearest
known pairs when it is the same on both sides.

config/TO/functions.json and bindings.json are written, and the differing functions listed on standard output."""
import json
import re
import struct
import sys
from bisect import bisect_left, bisect_right
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from pe import PEFile
from split import base_relocations, branches

COMPILERS = "build/compilers/GC"
WINDOW = 0x1800


def folder(version):
    return "GC_" + version.replace(".", "_")


def wildcards(pe, fixups, address, size):
    """(the function's bytes, the offsets of its relocated operands)."""
    code = bytearray(pe.read(address, size))
    wild = set()
    for at in fixups[bisect_left(fixups, address):bisect_right(fixups, address + size - 1)]:
        wild.update(i for i in range(at - address, at - address + 4) if i < size)
    try:
        for field, _ in branches(bytes(code), address):
            wild.update(i for i in range(field, field + 4) if i < size)
    except (KeyError, IndexError):
        pass
    return bytes(code), wild


def pattern(code, wild):
    parts, run = [], bytearray()
    for i, byte in enumerate(code):
        if i in wild:
            if run:
                parts.append(re.escape(bytes(run)))
                run.clear()
            parts.append(b".")
        else:
            run.append(byte)
    if run:
        parts.append(re.escape(bytes(run)))
    return re.compile(b"".join(parts), re.S)


def match_functions(functions, pe_a, fix_a, pe_b, fix_b):
    """{address in A: address in B} and the functions with no counterpart."""
    text_b = next(s for s in pe_b.sections if s.name == ".text")
    start_b = text_b.virtual_address
    blob = pe_b.read(start_b, text_b.virtual_size)
    # (the shift where the code starts)
    delta = start_b - next(s for s in pe_a.sections if s.name == ".text").virtual_address
    result, missing = {}, []
    taken = set()
    for f in sorted(functions, key=lambda f: int(f["address"], 16)):
        address, size = int(f["address"], 16), f["size"]
        code, wild = wildcards(pe_a, fix_a, address, size)
        compiled = pattern(code, wild)
        guess = address + delta - start_b
        low = max(0, guess - WINDOW)
        high = min(len(blob), guess + size + WINDOW)
        found = []
        position = low
        while True:
            m = compiled.search(blob, position, high)
            if not m:
                break
            found.append(m.start())
            position = m.start() + 1
        found = [p for p in found if p not in taken]
        if not found:
            missing.append(f)
            continue
        best = min(found, key=lambda p: abs(p - guess))
        if len(found) > 1 and size - len(wild) < 8:
            missing.append(f)
            continue
        taken.add(best)
        result[address] = start_b + best
        delta = start_b + best - address
    return result, missing


def data_pairs(functions, found, pe_a, fix_a, pe_b):
    """{address in A: address in B} of what the matched functions relocate."""
    pairs, conflicts = {}, set()
    for f in functions:
        address, size = int(f["address"], 16), f["size"]
        if address not in found:
            continue
        other = found[address]
        for at in fix_a[bisect_left(fix_a, address):bisect_right(fix_a, address + size - 1)]:
            if at + 4 > address + size:
                continue
            value_a = struct.unpack("<I", pe_a.read(at, 4))[0]
            value_b = struct.unpack("<I", pe_b.read(other + at - address, 4))[0]
            if pairs.setdefault(value_a, value_b) != value_b:
                conflicts.add(value_a)
    return {k: v for k, v in pairs.items() if k not in conflicts}


def main():
    from_version, to_version = sys.argv[1:3]
    pe_a = PEFile(Path(f"{COMPILERS}/{from_version}/mwcceppc.exe"))
    pe_b = PEFile(Path(f"{COMPILERS}/{to_version}/mwcceppc.exe"))
    fix_a, fix_b = base_relocations(pe_a), base_relocations(pe_b)
    source = Path(f"config/{folder(from_version)}")
    functions = json.loads((source / "functions.json").read_text())
    bindings = json.loads((source / "bindings.json").read_text())
    found, missing = match_functions(functions, pe_a, fix_a, pe_b, fix_b)
    pairs = data_pairs(functions, found, pe_a, fix_a, pe_b)
    pairs.update(found)
    known = sorted(pairs)

    def carry(address):
        if address in pairs:
            return pairs[address]
        k = bisect_right(known, address) - 1
        if k < 0 or k + 1 >= len(known):
            return None
        low, high = known[k], known[k + 1]
        if pairs[low] - low == pairs[high] - high and pe_a.section_for_address(address) == pe_a.section_for_address(low) \
                == pe_a.section_for_address(high):
            return address + pairs[low] - low
        return None

    out = []
    for f in functions:
        address = int(f["address"], 16)
        if address in found:
            out.append({**f, "address": f"0x{found[address]:08x}"})
    out.sort(key=lambda f: int(f["address"], 16))
    target = Path(f"config/{folder(to_version)}")
    target.mkdir(parents=True, exist_ok=True)
    (target / "functions.json").write_text(json.dumps(out, indent=2) + "\n")
    carried, lost = {}, []
    for name, text in bindings.items():
        moved = carry(int(text, 16))
        if moved is None:
            lost.append(name)
        else:
            carried[name] = f"0x{moved:08x}"
    (target / "bindings.json").write_text(json.dumps(carried, indent=2) + "\n")
    print(f"{len(found)} of {len(functions)} functions found in {to_version}; {len(missing)} differ or are not found")
    print(f"{len(carried)} of {len(bindings)} bindings carried; {len(lost)} lost")
    for f in missing:
        print(f"  {f['address']} {f['name']} ({f['size']} bytes) {f.get('source', '')}")
    for name in lost:
        print(f"  binding {name} = {bindings[name]}")


if __name__ == "__main__":
    main()
