#!/usr/bin/env python3
"""python tools/bindfrom.py VERSION: add to config/VERSION/bindings.json the symbols the exact functions of the compiled
sources name (tools/locate.py bindings_from), which is how a global or function a source defines or uses gets the name
the splits give it. A symbol bound at another address already is reported and left."""
import json
import sys
from glob import glob
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import locate


def main():
    version = sys.argv[1]
    folder = locate.folder(version)
    path = Path(f"config/{folder}/bindings.json")
    bindings = json.loads(path.read_text())
    root = f"build/{folder}/compiled/"
    added, conflicts = 0, []
    by_address = {int(a, 16): n for n, a in bindings.items()}
    for obj in sorted(glob(root + "**/*.obj", recursive=True)):
        source = obj.replace("\\", "/").removeprefix(root).removesuffix(".obj")
        for symbol, address in locate.bindings_from(version, source).items():
            if symbol.startswith(("_@", "@", ".", "$")) or symbol == "":
                continue
            if symbol in bindings:
                if int(bindings[symbol], 16) != address:
                    conflicts.append((symbol, bindings[symbol], hex(address), source))
                continue
            bindings[symbol] = f"0x{address:08x}"
            added += 1
    path.write_text(json.dumps(dict(sorted(bindings.items())), indent=2) + "\n")
    print(f"{added} symbols bound; {len(conflicts)} at another address")
    for c in conflicts[:40]:
        print("  conflict", *c)


if __name__ == "__main__":
    main()
