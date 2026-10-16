"""Capture relocation-aware data layouts and reports without invoking Rust."""
import argparse
import copy
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
    data = importlib.import_module("data_diff")
    asm = ROOT / "tests/fixtures/data/target/asm/sample.s"
    target = data.asm_layout(asm)
    (ROOT / "tests/fixtures/data/assembly.json").write_text(json.dumps(target, indent=2) + "\n", encoding="utf-8")
    manifest = json.loads((FIXTURES / "manifest.json").read_text())
    for case in manifest["cases"]:
        prefix = FIXTURES / case["id"]
        mine = data.object_layout(prefix.with_suffix(".o").read_bytes())
        theirs = copy.deepcopy(target)
        for name in mine.keys() & theirs.keys():
            data.absorb(mine[name]["items"], theirs[name]["items"])
        data.annotate(mine, theirs)
        # Check both function-order and missing-function candidate annotations.
        data.made_early(theirs, ["use literals"], ["first", "absent", "use literals"])
        comparisons = {}
        for name in dict.fromkeys([*mine, *theirs]):
            a, b = mine.get(name), theirs.get(name)
            comparisons[name] = {"report": data.report(name, a, b, True),
                                 "default": data.report(name, a, b, False),
                                 "differences": data.compare(data.linked(a["items"]), data.linked(b["items"])) if a and b else None}
        out = {"object": data.object_layout(prefix.with_suffix(".o").read_bytes()),
               "mine": mine, "theirs": theirs, "comparisons": comparisons}
        prefix.with_suffix(".data.json").write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    print(f"Captured data layouts/reports for {len(manifest['cases'])} cases")


if __name__ == "__main__":
    main()
