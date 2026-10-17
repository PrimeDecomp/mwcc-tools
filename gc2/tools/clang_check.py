#!/usr/bin/env python3
"""python tools/clang_check.py [save] [FILTER]: compile every source (whose path contains FILTER) with clang
-fsyntax-only (tools/mw/clang.py: the primary version's command), in parallel, and print its errors -- the ones MWCC
lets through matter: a call to an undeclared function is an implicit int declaration to MWCC too, and changes the code
around it.

  save  keep the errors as the baseline (build/clang_errors.txt); without it, print only errors the baseline lacks
        (line numbers ignored), so a change can be checked for what it newly breaks. Exits 1 when there are such.
"""

import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import build, clang, isolated, project

BASELINE = project.ROOT / "build/clang_errors.txt"


def main():
    if sys.argv[1:2] in (["-h"], ["--help"]):
        print(__doc__)
        return
    args = sys.argv[1:]
    save = args[:1] == ["save"]
    args = args[1:] if save else args
    try:
        compilations = [c for c in build.compilations() if not args or args[0] in c.source]
        found = [line for lines in isolated.parallel(clang.errors, compilations) for line in lines]
    except project.Error as e:
        sys.exit(f"error: {e}")
    if save:
        project.write_atomic(BASELINE, "\n".join(found) + "\n")
        print(f"{len(found)} errors saved to {project.relative(BASELINE)}")
        return
    if not BASELINE.exists():
        print("\n".join(found))
        print(f"{len(found)} errors (no baseline: python tools/clang_check.py save)")
        return

    def strip(line):
        return re.sub(r":\d+:\d+:", ":", line)

    base = Counter(map(strip, BASELINE.read_text().splitlines()))
    new = []
    for line in found:
        if base[strip(line)]:
            base[strip(line)] -= 1
        else:
            new.append(line)
    print("\n".join(new))
    print(f"{len(found)} errors, {len(new)} not in the baseline")
    sys.exit(1 if new else 0)


if __name__ == "__main__":
    main()
