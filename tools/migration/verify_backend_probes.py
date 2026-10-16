"""Audit live branch probes against stock objects and unchanged existing traces.

Development verification only; native tools and producers do not require Python.
Retains complete input/output hashes, observations and independently parsed paths.
"""
import argparse
import json
from pathlib import Path
import subprocess
import time

from verify_producer import ROOT, full_trace, sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", action="append")
    parser.add_argument("--from-ninja", type=Path)
    parser.add_argument("--source")
    parser.add_argument("--project-version", default="GC_2_7")
    parser.add_argument("--project-only", action="store_true")
    args = parser.parse_args()
    binary, oracle, output = args.binary.resolve(), args.oracle_root.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    evidence_path = output / "verification.json"
    assert not evidence_path.exists(), "retain prior evidence; choose a new output directory"
    fixtures = ROOT / "tests/fixtures/oracle"
    manifest = json.loads((fixtures / "manifest.json").read_text())
    evidence = dict(format_version=1, complete=False, binary_sha256=sha(binary), oracle_root=str(oracle), cases=[])

    def save():
        evidence_path.write_text(json.dumps(evidence, indent=2)+"\n")

    def run(name, command):
        started = time.perf_counter()
        result = subprocess.run([str(v) for v in command], capture_output=True, timeout=600)
        elapsed = time.perf_counter()-started
        stdout, stderr = output / f"{name}.json", output / f"{name}.stderr"
        stdout.write_bytes(result.stdout)
        stderr.write_bytes(result.stderr)
        assert result.returncode == 0, (name, result.returncode, result.stderr.decode(errors="replace"))
        return json.loads(result.stdout), dict(seconds=elapsed, stdout_sha256=sha(stdout), stderr_sha256=sha(stderr))

    versions = args.version or ["GC_1_3", "GC_1_3_2", "GC_2_7"]
    for version in versions:
        cases = [] if args.project_only else [v for v in manifest["cases"] if v["version"] == version]
        if args.source and args.from_ninja and version == args.project_version:
            cases.append(dict(id=args.source, source=args.source, project=True))
        for index, case in enumerate(cases):
            key = f"{version}-{index}"
            command = [binary, "oracle", "compile-trace", "--oracle-root", oracle, "--version", version]
            command += ["--schedule", "--stages", "--cse", "--locals", "--blocks"]
            if case.get("project"):
                command += ["--from-ninja", args.from_ninja.resolve(), case["source"]]
            else:
                command += [fixtures / case["source"], "--", *case["flags"]]
            disabled, disabled_meta = run(key+"-disabled", command)
            # Insert before SOURCE/-- so this cannot become a compiler option.
            enabled_command = command[:3]+["--backend"]+command[3:]
            enabled, enabled_meta = run(key+"-enabled", enabled_command)
            a, b = disabled["compilation"], enabled["compilation"]
            assert a["identical"] is True and b["identical"] is True
            for field in ["source_sha256", "producer_sha256", "original_sha256", "flags", "object_sha256", "diagnostics"]:
                assert a[field] == b[field], (case["id"], field)
            assert not any(v.startswith("pv.") for v in a["trace"].splitlines())
            old_records = "\n".join(v for v in b["trace"].splitlines() if not v.startswith("pv."))
            assert full_trace(a["trace"]) == full_trace(old_records), case["id"]
            report, report_meta = run(key+"-explanation", [binary, "provenance", "inspect", output / f"{key}-enabled.json", "--json"])
            empty = case["source"].endswith("empty.c")
            if empty:
                assert report["passes"] == [] and report["decisions"]["state"] == "indeterminate"
            else:
                assert report["passes"] and report["decisions"]["state"] == "satisfied", case["id"]
            assert report["lifecycle"]["state"] == "indeterminate"
            decisions = {}
            for p in report["passes"]:
                for candidate in p["candidates"]:
                    decision = candidate["decision"]
                    kind = decision.get("condition", decision["kind"])
                    decisions[kind] = decisions.get(kind, 0)+1
            evidence["cases"].append(dict(id=case["id"], version=version,
                source_sha256=a["source_sha256"], flags=a["flags"], producer_sha256=a["producer_sha256"],
                original_sha256=a["original_sha256"], object_sha256=a["object_sha256"],
                disabled=disabled_meta, enabled=enabled_meta, explanation=report_meta,
                passes=len(report["passes"]), decisions=decisions))
            save()
            print(f"{case['id']}: stock objects and existing trace agree, {len(report['passes'])} complete passes; {decisions}", flush=True)
    assert evidence["cases"], "no selected cases"
    evidence["complete"] = True
    save()


if __name__ == "__main__":
    main()
