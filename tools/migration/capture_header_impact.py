"""Capture header-impact analysis from synthetic traces and independently changed objects."""
import argparse
import copy
import importlib
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/fixtures/oracle"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.oracle_root / "tools"))
    oracle = importlib.import_module("oracle")
    impact = importlib.import_module("header_impact")
    manifest = json.loads((FIXTURES / "manifest.json").read_text())
    for case in manifest["cases"]:
        prefix = FIXTURES / case["id"]
        doc = json.loads(prefix.with_suffix(".json").read_text())
        object_bytes = prefix.with_suffix(".o").read_bytes()
        # Recompile from a source override: a real compiler transformation, not byte patching.
        source = FIXTURES / case["source"]
        with tempfile.TemporaryDirectory() as temporary:
            candidate = Path(temporary) / source.name
            candidate.write_bytes(source.read_bytes().replace(b"+ 1", b"+ 17").replace(b"+3", b"+9")
                                 .replace(b"return x + y + z", b"return (x*3) + y + z")
                                 .replace(b"int total = 0;", b"volatile int scratch[8] = {0}; int total = scratch[input&7];")
                                 + b"\nint added_by_header_change(int x) { return x * 7; }\n")
            compiled = oracle.compile_trace(str(source.resolve()), case["flags"], version=case["version"],
                                            overrides=[f"{source.resolve()}={candidate}"], full_cost=True, check=False)
            after = oracle.document_for(compiled)[0]
            after["source"] = case["source"]
            modified = prefix.with_suffix(".impact.o")
            modified.write_bytes(compiled["object"])
            prefix.with_suffix(".impact-after.json").write_text(json.dumps(after, indent=2) + "\n", encoding="utf-8")
            before = ({"object": object_bytes}, doc)
            result = impact.compare(before, (compiled, after), None)
            target_asm = "\n".join(e["asm"] for e in json.loads(prefix.with_suffix(".locals.json").read_text()))
            asm = Path(temporary) / "target.s"
            asm.write_text(target_asm)
            with_target = impact.compare(before, (compiled, after), asm)
        out = {"functions": impact.object_functions(object_bytes), "decisions": {f["link"]: impact.decisions(f) for f in doc["functions"]},
               "frames": {f["link"]: impact.frame(f) for f in doc["functions"]},
               "comparison": result, "report": impact.report("src/unit.cpp", ["include/header.hpp"], result),
               "asm": target_asm, "with_target": with_target,
               "target_report": impact.report("src/unit.cpp", ["include/header.hpp"], with_target)}
        prefix.with_suffix(".impact.json").write_text(json.dumps(out, indent=2, default=sorted) + "\n", encoding="utf-8")
    names = ["__arraydtor$89", "__arraydtor$93", "@12", "@14", "normal", "__arraydtor$89"]
    path = ROOT / "tests/fixtures/header-impact"
    path.mkdir(exist_ok=True)
    (path / "pairing.json").write_text(json.dumps({"names": names, "paired": impact.paired(names)}, indent=2) + "\n")
    print(f"Captured header-impact references for {len(manifest['cases'])} cases")


if __name__ == "__main__":
    main()
