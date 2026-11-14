#!/usr/bin/env python3
"""research/wii-rc1-differences.md: Wii 1.0RC1 against GC 3.0a5.2, per RC1 module (wii_remap's mapping)."""

import collections
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import wii_remap as w

RC1 = "research/Wii_1_0RC1"
A52 = "build/compilers/GC/3.0a5.2/mwcceppc.exe"
FLAG = re.compile(r"SWCH|LocalCSE|Registers|Coloring|Spill|Schedul|Struct|Move|Copy|StackFrame|IroLoop|IroJump|IrOptimizer", re.I)


def main():
    A, B, mapped, how, by, order = w.run(RC1, "build/compilers/Wii/1.0RC1/mwcceppc.exe", A52, lambda *a: None)
    out = w.fill_modules(w.build_layout(A, B, mapped, how, by))
    unit = {}
    rows = collections.defaultdict(lambda: collections.Counter())
    for a in order:
        f = by[a]
        m = f["module"] or "(none)"
        unit[m] = f["unit"]
        r = rows[m]
        if a not in mapped:
            r["only"] += 1
            r["only_b"] += f["size"]
        elif how[a] == "exact":
            r["same"] += 1
            r["same_b"] += f["size"]
        else:
            r["diff"] += 1
            r["diff_b"] += f["size"]
            r["diff_b52"] += next((e["size"] for e in out if e["address"] == f"0x{mapped[a]:08x}"), 0)
            r[how[a]] += 1
    tot = collections.Counter()
    for r in rows.values():
        tot.update(r)
    n = sum(tot[k] for k in ("same", "diff", "only"))
    lines = [
        "# Wii 1.0RC1 against GC 3.0a5.2",
        "",
        f"The Wii 1.0RC1 `mwcceppc.exe` ({n} functions, laid out from its CodeView in `research/Wii_1_0RC1`) was mapped onto the GC 3.0a5.2 one",
        "(no symbols) by relocation-masked code matching (`tools/wii_remap.py`; layout in `research/GC_3_0a5_2`).",
        f"{tot['same']} functions ({tot['same_b']} bytes) are identical once relocations and call targets are masked, {tot['diff']} are found in 3.0a5.2",
        f"but differ ({tot['diff_b']} bytes in RC1, {tot['diff_b52']} in 3.0a5.2), {tot['only']} ({tot['only_b']} bytes) have no counterpart in 3.0a5.2.",
        f"A further {sum(1 for e in out if e['how'].startswith('new'))} functions of 3.0a5.2 ({sum(e['size'] for e in out if e['how'].startswith('new'))} bytes)",
        "have no RC1 counterpart.",
        "",
        "How to read the numbers: \"identical\" means the bytes are the same except for relocated addresses and call/jmp operands, so a changed callee",
        "does not make the caller differ. \"differs\" means the function was found by call-graph propagation (`propagated`), by its position between two",
        "found functions (`order`) or by byte similarity (`fuzzy`), so the pairing of the `order` and `fuzzy` ones is a guess; the last two columns say how many.",
        "A function whose size changed by only a few bytes is typically a compiler-wide change (a new register or an inlined helper), a function",
        "that doubled is a rewrite. Functions that moved between modules are counted in the RC1 module they have there.",
        "",
        "## RC1 modules, by differing bytes",
        "",
        "| module | unit | identical | differs | RC1-only | differing bytes (RC1 / 3.0a5.2) | RC1-only bytes | order+fuzzy |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for m, r in sorted(rows.items(), key=lambda kv: (-kv[1]["diff_b"], -kv[1]["only_b"], kv[0])):
        lines.append(
            f"| {m} | {unit[m]} | {r['same']} | {r['diff']} | {r['only']} | {r['diff_b']} / {r['diff_b52']} | {r['only_b']} | {r['order'] + r['fuzzy']} |"
        )
    lines.append(
        f"| total | | {tot['same']} | {tot['diff']} | {tot['only']} | {tot['diff_b']} / {tot['diff_b52']} | {tot['only_b']} | {tot['order'] + tot['fuzzy']} |"
    )
    lines += ["", "## Modules to watch", ""]
    lines += ["| module | identical | differs | RC1-only | verdict |", "| --- | ---: | ---: | ---: | --- |"]
    for m, r in sorted(rows.items()):
        if FLAG.search(m):
            total = r["same"] + r["diff"] + r["only"]
            verdict = "unchanged" if r["diff"] + r["only"] == 0 else f"changed ({r['diff'] + r['only']} of {total} functions)"
            lines.append(f"| {m} | {r['same']} | {r['diff']} | {r['only']} | {verdict} |")
    lines += [
        "",
        "`SWCH_*` (switch lowering) and `VLE_*` (VLE instruction sets) are new in the Wii compiler: neither the module names nor any of their",
        "assert strings are in the 3.0a5.2 executable, so their functions are RC1-only for real, not unmatched. `Coloring` shares its file name",
        "with 3.0a5.2 but only one of its four functions was found (changed); the other three may be new helpers or rewritten ones.",
        "`LocalCSE` has no assert string of that name in 3.0a5.2, yet 7 of its 18 functions match, so it existed under another file name.",
        "",
    ]
    new = [e for e in out if e["how"].startswith("new")]
    lines += ["", "## 3.0a5.2 functions with no RC1 counterpart", ""]
    lines += [
        f"{len(new)} functions, {sum(e['size'] for e in new)} bytes, the {min(len(new), 40)} largest below. The module is the neighbours' module where both agree (`+between`).",
        "",
        "| address | size | in module |",
        "| --- | ---: | --- |",
    ]
    for e in sorted(new, key=lambda e: -e["size"])[:40]:
        lines.append(f"| {e['address']} | {e['size']} | {e['module'] or '?'} |")
    bym = collections.Counter()
    for e in new:
        bym[e["module"] or "(between modules / unknown)"] += e["size"]
    lines += ["", "By module (bytes):", "", "| module | bytes |", "| --- | ---: |"]
    lines += [f"| {m} | {b} |" for m, b in bym.most_common(25)]
    Path(w.resolve("research/wii-rc1-differences.md")).write_text("\n".join(lines) + "\n")
    print("\n".join(lines[:60]))


if __name__ == "__main__":
    main()
