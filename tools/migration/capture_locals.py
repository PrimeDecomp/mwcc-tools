"""Capture locals/frame references from existing redistributable traces and synthetic PPC code."""
import argparse
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/fixtures/oracle"


def d(op, reg, offset, base=1):
    return op << 26 | reg << 21 | base << 16 | (offset & (0xfff if op in (56, 57, 60, 61) else 0xffff))


def assembly(words, link):
    lines = [f'.fn "{link}", global']
    for word in words:
        op = word >> 26
        reg = (word >> 21) & 31
        offset = word & (0xfff if op in (56, 57, 60, 61) else 0xffff)
        if offset & (0x800 if op in (56, 57, 60, 61) else 0x8000):
            offset -= 0x1000 if op in (56, 57, 60, 61) else 0x10000
        mnemonic = ORACLE.MNEMONICS.get(op)
        ins = (f"{mnemonic} {'f' if op >= 48 else 'r'}{reg}, {offset}(r{(word >> 16) & 31})"
               if mnemonic else f".long 0x{word:08x}")
        if op == 14:
            ins = f"addi r{reg}, r{(word >> 16) & 31}, {offset}"
        raw = " ".join(f"{b:02X}" for b in word.to_bytes(4, "big"))
        lines.append(f"/* 00000000 {raw} */ {ins}")
    return "\n".join([*lines, f'.endfn "{link}"'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("oracle_reference", args.oracle_root / "tools/oracle.py")
    global ORACLE
    ORACLE = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ORACLE)
    manifest = json.loads((FIXTURES / "manifest.json").read_text())
    for case in manifest["cases"]:
        prefix = FIXTURES / case["id"]
        lines = prefix.with_suffix(".trace").read_text().splitlines()
        analyses = ORACLE.analyze_file(lines, version=case["version"])
        passes, bindings = ORACLE.scalarize_passes(lines), ORACLE.inline_bindings(lines)
        out = []
        for a in analyses:
            p, b = passes.get(a["link"], []), bindings.get(a["link"], [])
            words = ORACLE.function_words(prefix.with_suffix(".o").read_bytes(), a["link"]) or []
            # Synthetic target: same instructions, plus copies in otherwise unpaired stack slots.
            target_words = [d(37, 1, -512), d(47, 30, 480), *words]
            target_words += [d(36, r, base + 4*i) for base in (320, 332, 344) for i, r in enumerate((3, 4, 5))]
            target_words += [d(32, 6, 320), d(14, 7, 332)]
            asm = assembly(target_words, a["link"])
            path = prefix.with_suffix(".locals-target.tmp.s")
            try:
                path.write_text(asm)
                target = ORACLE.target_frame(path, a["link"])
                frame = a["frame"] or []
                layout = a.get("layout")
                limit = sum(layout[k] for k in ("linkage", "outgoing", "locals")) if layout else 512
                shifts = ORACLE.slot_shifts(words, target_words, frame, limit, target[2])
                runs, mine = ORACLE.unpaired_objects(words, target_words, frame, limit, target[2])
                out.append({"link": a["link"], "plain": ORACLE.locals_report(a, p, bindings=b),
                            "summary": ORACLE.locals_summary(a, p, b), "asm": asm,
                            "target": {"size": target[0], "uses": {o: sorted(u) for o, u in target[1].items()}, "limit": target[2]},
                            "words": words, "target_words": target_words, "limit": limit,
                            "target_report": ORACLE.locals_report(a, p, target, b),
                            "shifts": shifts, "shift_report": ORACLE.shift_report(shifts, frame),
                            "runs": runs, "mine": mine, "unpaired_report": ORACLE.unpaired_report(runs, mine),
                            "target_summary": ORACLE.locals_summary(a, p, b, runs)})
            finally:
                path.unlink(missing_ok=True)
        prefix.with_suffix(".locals.json").write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    print(f"Captured locals and target-frame views for {len(manifest['cases'])} cases")


if __name__ == "__main__":
    main()
