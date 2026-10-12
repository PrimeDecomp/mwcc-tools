#!/usr/bin/env python3
"""python tools/funcmatch.py FROM TO: match the functions of one version's executable with another's by their bytes.

Two functions are the same when their lengths are equal and their bytes are once the operands that are relocated in the
image (the absolute fixups of the base relocation table and the rel32 displacements of calls and jumps) are masked.
config/FROM/functions.json lists the functions; a match is written as {"from address": "to address"} (config of TO is
only needed for the report).
"""
import json
import struct
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from pe import PEFile
from split import base_relocations, branches

COMPILERS = "build/compilers/GC"


def config_functions(version):
    folder = "GC_" + version.replace(".", "_")
    return json.loads(Path(f"config/{folder}/functions.json").read_text())


def masked(pe, fixups, address, size):
    """The bytes of a function with the relocated operands zeroed."""
    from bisect import bisect_left
    code = bytearray(pe.read(address, size))
    for at in fixups[bisect_left(fixups, address):]:
        if at >= address + size:
            break
        for i in range(4):
            if at - address + i < size:
                code[at - address + i] = 0
    try:
        for field, _ in branches(bytes(code), address):
            for i in range(4):
                code[field + i] = 0
    except (KeyError, IndexError):
        pass
    return bytes(code)


def match(version_a, version_b):
    """{address in A: address in B} of the functions identical by masked bytes (one candidate in each only)."""
    pe_a, fix_a, pe_b, fix_b = None, None, None, None
    pe_a = PEFile(Path(f"{COMPILERS}/{version_a}/mwcceppc.exe"))
    pe_b = PEFile(Path(f"{COMPILERS}/{version_b}/mwcceppc.exe"))
    fix_a, fix_b = base_relocations(pe_a), base_relocations(pe_b)
    fa, fb = config_functions(version_a), config_functions(version_b)
    by_bytes = defaultdict(list)
    for f in fb:
        address = int(f["address"], 16)
        by_bytes[masked(pe_b, fix_b, address, f["size"])].append(address)
    result, ambiguous = {}, {}
    for f in fa:
        address = int(f["address"], 16)
        candidates = by_bytes.get(masked(pe_a, fix_a, address, f["size"]), [])
        if len(candidates) == 1:
            result[address] = candidates[0]
        elif candidates:
            ambiguous[address] = candidates
    return result, ambiguous


if __name__ == "__main__":
    a, b = sys.argv[1:3]
    result, ambiguous = match(a, b)
    print(f"{len(result)} unique, {len(ambiguous)} ambiguous of {len(config_functions(a))}")
