#!/usr/bin/env python3
"""python tools/fdiff.py VERSION SOURCE FUNCTION ADDRESS: the instructions of a compiled function against the function of
the original executable it is a version of, as a diff.

The source is the one ninja compiled to build/VERSION/compiled/SOURCE.obj (run `ninja` on that object first); FUNCTION is
its symbol (with the leading underscore) and ADDRESS the function in the original. Relocated operands are shown as
`<reloc>` on the object side and by their values on the original's, since an object has no addresses yet; instructions are
compared by their text with every number masked to `N`, so the diff lists the instruction changes, not the displacement
changes. `=` marks a line that agrees exactly (also in its operands), `~` a line that agrees once numbers are masked."""
import difflib
import json
import re
import struct
import sys
from pathlib import Path

import capstone

sys.path.insert(0, str(Path(__file__).parent))
from locate import folder, functions_of
from pe import PEFile

DISASSEMBLER = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


def listing(code, wild):
    out = []
    for i in DISASSEMBLER.disasm(bytes(code), 0):
        field = any(j in wild for j in range(i.address, i.address + i.size))
        text = f"{i.mnemonic} {i.op_str}".strip()
        out.append((i.address, text, field))
    return out


def shape(text, field):
    text = re.sub(r"0x[0-9a-f]+|\b\d+\b", "N", text)
    return text + (" R" if field else "")


def main():
    version, source, name, address = sys.argv[1:5]
    config = json.loads(Path(f"config/{folder(version)}/config.json").read_text())
    pe = PEFile(Path(config["original"]))
    path = Path(f"build/{folder(version)}/compiled/{source}.obj")
    ours = next((code, wild) for n, code, wild in functions_of(path) if n == name)
    # (the original's function is as long as the next function's start says; show a window of the same length class)
    address = int(address, 16)
    text = next(s for s in pe.sections if s.name == ".text")
    blob = pe.read(address, min(len(ours[0]) * 2 + 64, text.virtual_address + text.virtual_size - address))
    theirs = DISASSEMBLER.disasm(bytes(blob), 0)
    lines, size = [], 0
    for i in theirs:
        lines.append((i.address, f"{i.mnemonic} {i.op_str}".strip(), False))
        if i.mnemonic == "ret" and i.address + i.size >= len(ours[0]) - 64 and (
                i.address + i.size + 1 > 0):
            size = i.address + i.size
            # (a function ends at a ret followed by the next function's alignment padding or prologue)
            nxt = bytes(blob[size:size + 3])
            if nxt[:1] in (b"\x55", b"\xcc", b"\x90") or nxt == b"\x00\x00\x00":
                break
    mine = listing(*ours)
    a = [shape(t, f) for _, t, f in mine]
    b = [shape(t, False) for _, t, _ in lines]
    # (theirs has relocated operands as plain numbers: compare mnemonic and operand shape alike)
    matcher = difflib.SequenceMatcher(None, [re.sub(r" R$", "", x) for x in a], b, autojunk=False)
    print(f"{name}: {len(mine)} instructions, original at {address:#x}: {len(lines)} ({size} bytes); "
          f"ratio {matcher.ratio():.3f}")
    for tag, i1, i2, j1, j2 in matcher.get_opcodes():
        if tag == "equal":
            for k in range(i1, i2):
                mark = "=" if mine[k][1] == lines[j1 + k - i1][1] else "~"
                print(f"  {mark} {mine[k][0]:4x}  {mine[k][1]}")
            continue
        for k in range(i1, i2):
            print(f"  - {mine[k][0]:4x}  {mine[k][1]}")
        for k in range(j1, j2):
            print(f"  + {lines[k][0]:4x}  {lines[k][1]}")


if __name__ == "__main__":
    main()
