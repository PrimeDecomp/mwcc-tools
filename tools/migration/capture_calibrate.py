"""Freeze the kit's inline-calibration reports independently of Rust."""
import argparse
from contextlib import redirect_stdout
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/fixtures/oracle"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-script", type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("calibrate_reference", args.reference_script)
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    manifest = json.loads((FIXTURES / "manifest.json").read_text())
    for case in manifest["cases"]:
        prefix = FIXTURES / case["id"]
        doc = json.loads(prefix.with_suffix(".json").read_text())
        costed = [d for f in doc["functions"] for d in f["inlining"] if "cost" in d]
        callees = sorted({d["callee"] for d in costed})
        out = []
        for callee in callees:
            links = {d["link"] for d in costed if callee in d["callee"]}
            target = {"name": "synthetic", "metadata": {"source_path": doc["source"]},
                      "functions": [{"name": l} for l in sorted(links) if l.startswith(callee+"<")]
                                   + [{"name": f"{callee}<P5Value>__4rstlFPC5Value"}]}
            with tempfile.TemporaryDirectory() as temporary:
                output = Path(temporary) / "rows.json"
                def run(binary, symbols):
                    stream = io.StringIO()
                    arguments = ["calibrate", "--callee", callee, "--unit", "synthetic", "--near", "3", "--breakdown", "--json", str(output)]
                    if binary:
                        arguments += ["--binary"]
                    with patch.object(helper, "load_units", return_value={"synthetic": target}), \
                         patch.object(helper, "oracle_json", return_value=(doc, None)), \
                         patch.object(helper, "binary_symbols", return_value=symbols), \
                         patch.dict(os.environ, {"MWCC_ORACLE_ROOT": "reference"}), \
                         patch.object(sys, "argv", arguments), redirect_stdout(stream):
                        helper.main()
                    return json.loads(output.read_text()), stream.getvalue().splitlines()
                try:
                    rows, report = run(False, set())
                except KeyError as error:
                    out.append({"callee": callee, "target": target, "missing_field": str(error)})
                    continue
                binaries = []
                for symbols in (set(), links):
                    table, text = run(True, symbols)
                    binaries.append({"symbols": sorted(symbols), "table": table, "report": text})
            out.append({"callee": callee, "target": target, "rows": rows, "report": report, "binaries": binaries})
        prefix.with_suffix(".calibrate.json").write_text(json.dumps(out, indent=2)+"\n", encoding="utf-8")
    print(f"Captured inline calibration for {len(manifest['cases'])} fixture documents")


if __name__ == "__main__":
    main()
