#!/usr/bin/env python3
"""python tools/disasm.py VERSION ADDRESS [SIZE]: disassemble a function of a version's executable.

Call targets and absolute addresses are annotated with the names config/VERSION/symbols.txt gives them. SIZE defaults to
the function's size in symbols.txt (or 0x100)."""
import re
import sys
from bisect import bisect_right
from pathlib import Path

import capstone

sys.path.insert(0, str(Path(__file__).parent))
from locate import folder
from pe import PEFile
import json


def symbols_of(version):
    names, sizes = {}, {}
    for line in Path(f"config/{folder(version)}/symbols.txt").read_text().splitlines():
        m = re.match(r"(\S+) = \.(\w+):0x([0-9A-Fa-f]+);(?:.*size:0x([0-9A-Fa-f]+))?", line)
        if m:
            address = int(m.group(3), 16)
            names[address] = m.group(1)
            sizes[address] = int(m.group(4), 16) if m.group(4) else 0
    return names, sizes


def main():
    version, address, *rest = sys.argv[1:]
    address = int(address, 16)
    config = json.loads(Path(f"config/{folder(version)}/config.json").read_text())
    pe = PEFile(Path(config["original"]))
    names, sizes = symbols_of(version)
    starts = sorted(names)

    def label(value):
        k = bisect_right(starts, value) - 1
        if k >= 0 and value - starts[k] <= max(sizes[starts[k]], 0) + (0 if sizes[starts[k]] else 0x10):
            offset = value - starts[k]
            return names[starts[k]] + (f"+{offset:#x}" if offset else "")
        return None

    size = int(rest[0], 16) if rest else (sizes.get(address) or 0x100)
    code = pe.read(address, size)
    disassembler = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for i in disassembler.disasm(bytes(code), address):
        text = f"{i.mnemonic} {i.op_str}".strip()

        def annotate(m):
            value = int(m.group(0), 16)
            name = label(value) if value >= 0x400000 else None
            return f"{m.group(0)} <{name}>" if name else m.group(0)

        print(f"{i.address:08x}  {re.sub(r'0x[0-9a-f]{5,8}', annotate, text)}")


if __name__ == "__main__":
    main()
