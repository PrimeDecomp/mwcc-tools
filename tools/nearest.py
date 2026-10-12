#!/usr/bin/env python3
"""python tools/nearest.py VERSION SOURCE...: for each function a compiled source has that is not found exactly in the
executable, the functions of the executable it resembles most.

A function is reduced to its instruction mnemonics (operands and relocated fields ignored) and compared with every
function symbols.txt lists, by difflib's ratio; the report gives the three nearest with their address and size. It is the
map from a source function to the one it is a changed version of, the starting point of matching it."""
import re
import sys
from difflib import SequenceMatcher
from pathlib import Path

import capstone

sys.path.insert(0, str(Path(__file__).parent))
from locate import folder, functions_of, find
from pe import PEFile
import json

DISASSEMBLER = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


def mnemonics(code):
    return [i.mnemonic for i in DISASSEMBLER.disasm(bytes(code), 0)]


def main():
    version, *sources = sys.argv[1:]
    config = json.loads(Path(f"config/{folder(version)}/config.json").read_text())
    pe = PEFile(Path(config["original"]))
    text = next(s for s in pe.sections if s.name == ".text")
    blob = pe.read(text.virtual_address, text.virtual_size)
    candidates = []
    for line in Path(f"config/{folder(version)}/symbols.txt").read_text().splitlines():
        m = re.match(r"(\S+) = \.text:0x([0-9A-Fa-f]+);.*type:function size:0x([0-9A-Fa-f]+)", line)
        if m:
            address, size = int(m.group(2), 16), int(m.group(3), 16)
            candidates.append((m.group(1), address, size,
                               mnemonics(blob[address - text.virtual_address:address - text.virtual_address + size])))
    for source in sources:
        print(f"{source}:")
        path = Path(f"build/{folder(version)}/compiled/{source}.obj")
        for name, code, wild in functions_of(path):
            if find(blob, code, wild):
                continue
            mine = mnemonics(code)
            scored = []
            for other, address, size, theirs in candidates:
                if not size or size > 3 * len(code) + 64 or 3 * size + 64 < len(code):
                    continue
                matcher = SequenceMatcher(None, mine, theirs, autojunk=False)
                if matcher.real_quick_ratio() < 0.5 or matcher.quick_ratio() < 0.5:
                    continue
                scored.append((matcher.ratio(), other, address, size))
            scored.sort(reverse=True)
            best = ", ".join(f"{r:.2f} {o}@{a:#x}/{s}" for r, o, a, s in scored[:3])
            print(f"  {name:44} {len(code):5}  {best}")


if __name__ == "__main__":
    main()
