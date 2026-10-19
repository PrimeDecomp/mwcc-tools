"""Capture verify.py's reports and sequential invocations with synthetic successful/failed builds."""
import argparse
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "tests/fixtures/build-verify"
VERSIONS = ["GC_1_0", "GC_1_1", "GC_1_1p1", "GC_1_2_5", "GC_1_2_5n", "GC_1_3", "GC_1_3_2", "GC_2_7"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    args = parser.parse_args()
    args.oracle_root = args.oracle_root.resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    cases = []
    for name in ["matching", "differing", "failed_build", "failed_progress"]:
        report = dict(measures=dict(matched_code_percent=99.999, complete_code_percent=84.327,
                                    matched_data_percent=12.345, complete_data_percent=0), units=[
            dict(name="a", metadata=dict(source_path="src/a.c"), functions=[
                dict(name="good", fuzzy_match_percent=100), dict(name="___patched", fuzzy_match_percent=0)]),
            dict(name="unlisted", metadata=dict(source_path="src/b.c"), functions=[dict(name="ignored", fuzzy_match_percent=0)]),
            dict(name="no_source", functions=[dict(name="ignored", fuzzy_match_percent=0)])])
        if name == "differing":
            report["units"][0]["functions"] += [dict(name="diff", fuzzy_match_percent=99.999), dict(name="missing_percent")]
        config = dict(matching=["src/a.c"])
        functions = [dict(name="patched", binary_patch=True)]
        calls = []
        active = None

        def run(command, **kwargs):
            nonlocal active
            if command[1:2] == ["configure.py"]:
                active = command[-1]
                calls.append(dict(command="configure", version=active))
                return SimpleNamespace(returncode=0, stdout="", stderr="")
            assert command[0] == "ninja"
            calls.append(dict(command="ninja", version=active, targets=command[1:]))
            fail = active == "GC_1_1" and (name == "failed_build" and command == ["ninja"]
                                                    or name == "failed_progress" and command == ["ninja", "progress"])
            return SimpleNamespace(returncode=int(fail), stdout="synthetic Ninja failure\n" if fail else "", stderr="")

        old_cwd = Path.cwd()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for version in VERSIONS:
                (root / f"config/{version}").mkdir(parents=True)
                (root / f"build/{version}").mkdir(parents=True)
                (root / f"config/{version}/config.json").write_text(json.dumps(config))
                (root / f"config/{version}/functions.json").write_text(json.dumps(functions))
                (root / f"build/{version}/report.json").write_text(json.dumps(report))
                (root / f"build/{version}/ok").write_text(f"{version} synthetic completion\n")
                if version != "GC_1_0":
                    (root / f"config/{version}/splits.txt").write_text("fixture splits\n")
            output = io.StringIO()
            try:
                os.chdir(root)
                with patch("subprocess.run", run), contextlib.redirect_stdout(output):
                    try:
                        runpy.run_path(str(args.oracle_root / "tools/verify.py"), run_name="__main__")
                    except SystemExit as error:
                        exit_code = error.code
            finally:
                os.chdir(old_cwd)
        cases.append(dict(name=name, report=report, config=config, functions=functions, calls=calls,
                          exit_code=exit_code, stdout=output.getvalue()))
    (OUT / "cases.json").write_text(json.dumps(cases, indent=2) + "\n", encoding="utf-8")
    metadata = dict(reference_commit=subprocess.check_output(["git", "-C", str(args.oracle_root), "rev-parse", "HEAD"], text=True).strip(),
                    reference_sha256=hashlib.sha256((args.oracle_root / "tools/verify.py").read_bytes()).hexdigest(),
                    files={"cases.json": hashlib.sha256((OUT / "cases.json").read_bytes()).hexdigest()})
    (OUT / "manifest.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(f"captured {len(cases)} verification workflows")


if __name__ == "__main__":
    main()
