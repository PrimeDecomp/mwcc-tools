"""Freeze TU analysis references using redistributable objects and trace documents."""
import argparse
import importlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/fixtures/oracle"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.oracle_root / "tools"))
    diff = importlib.import_module("inline_diff")
    window = importlib.import_module("inline_window")
    manifest = json.loads((FIXTURES / "manifest.json").read_text())
    for case in manifest["cases"]:
        prefix = FIXTURES / case["id"]
        doc = json.loads(prefix.with_suffix(".json").read_text())
        ds = diff.decisions_by_link(doc)
        # Include both module policies and a weak definition held by another TU.
        emitted = diff.emission(doc)
        links = list(ds)
        target = {l: "weak" for i, l in enumerate(links) if i % 2}
        target.update({diff.MODULE: {l: Path("another.s") for i, l in enumerate(emitted) if i % 2}, diff.STRIPPED: False})
        out = {"decisions": ds, "summaries": {}, "emission": emitted, "target": {l: b for l, b in target.items() if l not in (diff.MODULE, diff.STRIPPED)},
               "module": {l: str(p) for l, p in target[diff.MODULE].items()}, "predictions": {},
               "emission_report": diff.emission_report("A", doc, target, emitted)}
        for l, decisions in ds.items():
            summary = diff.summary(decisions)
            out["summaries"][l] = {"summary": summary, "report": diff.report("A", summary),
                                     "verdict_present": diff.verdict(decisions, True), "verdict_absent": diff.verdict(decisions, False)}
        for stripped in (False, True):
            target[diff.STRIPPED] = stripped
            out["predictions"][str(stripped).lower()] = {l: diff.prediction(l, target, emitted) for l in set(ds) | set(emitted)}
        ours = window.object_calls(prefix.with_suffix(".o").read_bytes())
        # Exercise equal/fewer/more target calls using actual relocation callees.
        all_ds = {f["link"]: [d["link"] for d in f["inlining"] if d.get("kind") == "decision"] for f in doc["functions"]}
        windows = []
        for mode in ("same", "fewer", "more"):
            calls = {f["link"]: dict(ours.get(f["link"], {})) for f in doc["functions"]}
            for l, callees in all_ds.items():
                for c in callees:
                    old = calls[l].get(c, 0)
                    calls[l][c] = old if mode == "same" else max(0, old-1) if mode == "fewer" else old+1
            import collections
            rows = window.bounds(doc, ours, {l: collections.Counter(c) for l, c in calls.items()})
            low, high, _, _ = window.window(rows)
            windows.append({"calls": calls, "bounds": rows, "window": [low, high], "report": window.report(doc["source"], rows, False),
                            "verbose": window.report(doc["source"], rows, True)})
        out["object_calls"], out["windows"] = ours, windows
        prefix.with_suffix(".tu.json").write_text(json.dumps(out, indent=2, default=list) + "\n", encoding="utf-8")
    print(f"Captured inline-diff and inline-window references for {len(manifest['cases'])} cases")


if __name__ == "__main__":
    main()
