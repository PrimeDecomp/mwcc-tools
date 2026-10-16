"""Compare independent and combined frontend trace switches with frozen C."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from verify_producer import ROOT, sha, full_trace, trace_line_endings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--native-root", type=Path, required=True)
    args = parser.parse_args()
    binary, reference, native = args.binary.resolve(), args.oracle_root.resolve(), args.native_root.resolve()
    stage = Path(tempfile.mkdtemp(prefix="frontend-switches-", dir=native))
    print(f"retained frontend switch tree: {stage}", flush=True)
    fixtures = ROOT / "tests/fixtures/oracle"
    cases = json.loads((fixtures / "manifest.json").read_text())["cases"]
    evidence = dict(complete=False, binary_sha256=sha(binary), cases=[])
    env = os.environ.copy()
    if os.name != "nt":
        env["MWCC_WIBO"] = str(native / "build/tools/wibo")
        evidence["wibo_sha256"] = sha(Path(env["MWCC_WIBO"]))
    def save():
        (stage / "frontend-switches.json").write_text(json.dumps(evidence, indent=2) + "\n")
    for version in ["GC_1_3", "GC_1_3_2", "GC_2_7"]:
        fixture = next(c for c in cases if c["id"] == version + "/pipeline")
        for mode, flags in [("default", []), ("expression", ["--inline-cost"]), ("full", ["--full-cost"]),
                ("both", ["--inline-cost", "--full-cost"]), ("instantiation", ["--instantiation"])]:
            item = dict(version=version, mode=mode, runs=[])
            evidence["cases"].append(item)
            documents = []
            for label, root in [("C", reference), ("Rust", native)]:
                command = [str(binary), "oracle", "compile-trace", "--oracle-root", str(root), "--version", version,
                    "--working-directory", str(fixtures), *flags, str(fixtures / fixture["source"]), "--", *fixture["flags"]]
                result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, timeout=180)
                prefix = f"{version}-{mode}-{label}"
                for stream in ["stdout", "stderr"]: (stage / f"{prefix}.{stream}").write_bytes(getattr(result, stream))
                item["runs"].append(dict(label=label, exit_code=result.returncode,
                    stdout_sha256=sha(stage / f"{prefix}.stdout"), stderr_sha256=sha(stage / f"{prefix}.stderr")))
                save()
                assert result.returncode == 0, (prefix, result.stdout[-2000:], result.stderr[-2000:])
                documents.append(json.loads(result.stdout)["compilation"])
            left, right = documents
            assert left["identical"] is True and right["identical"] is True
            for field in ["flags", "trace_options", "source_sha256", "object_sha256", "diagnostics"]: assert left[field] == right[field], (version, mode, field)
            assert right["producer_sha256"] == sha(native / f"build/{version}/oracle/mwcceppc.exe")
            assert full_trace(left["trace"]) == full_trace(right["trace"]), (version, mode)
            assert trace_line_endings(left["trace"]) == trace_line_endings(right["trace"]), (version, mode)
            assert left["trace_options"]["inline_cost"] == (mode in ["expression", "both"])
            assert left["trace_options"]["full_cost"] == (mode in ["full", "both"])
            assert left["trace_options"]["instantiation"] == (mode == "instantiation")
            if mode in ["expression", "full", "both"]: assert "inl.expr " in left["trace"]
            else: assert "inl.expr " not in left["trace"]
            if mode in ["full", "both"]: assert " full=" in left["trace"]
            else: assert not any(" full=" in line for line in left["trace"].splitlines() if line.startswith("inl.cost "))
            if mode == "instantiation": assert "tp.parse " in left["trace"]
            item.update(producer_sha256=right["producer_sha256"], object_sha256=right["object_sha256"], records=len(left["trace"].splitlines()))
            save()
        print(f"{version}: five frontend trace-switch modes agree", flush=True)
    evidence["complete"] = True
    save()


if __name__ == "__main__":
    main()
