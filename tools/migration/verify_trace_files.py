"""Check real trace append/fallback behavior with the compiler's original stdio."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from verify_producer import ROOT, full_trace, sha, trace_line_endings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--hybrid-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    reference, hybrid = args.oracle_root.resolve(), args.hybrid_root.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix="trace-files-", dir=args.output.resolve()))
    print(f"retained real trace files: {stage}", flush=True)
    fixtures = ROOT / "tests/fixtures/oracle"
    manifest = json.loads((fixtures / "manifest.json").read_text())
    evidence = dict(versions=[])
    for version in ["GC_1_3", "GC_1_3_2", "GC_2_7"]:
        case = next(c for c in manifest["cases"] if c["version"] == version and c["source"].endswith("/cse.c"))
        item = dict(version=version, modes=[], flags=case["flags"], source_sha256=sha(fixtures / case["source"]))
        for mode in ["unset", "empty", "zero", "missing-parent", "directory", "append"]:
            runs = []
            for label, root in [("C", reference), ("Rust", hybrid)]:
                directory = stage / f"{version}-{mode}-{label}"
                directory.mkdir()
                trace = directory / ("0" if mode == "zero" else "trace.txt")
                prefix = b"preserved prefix\n" if mode == "append" else b""
                if prefix:
                    trace.write_bytes(prefix)
                env = {k: v for k, v in os.environ.items() if not k.startswith("MWCC_ORACLE_")}
                env.update(MWCC_ORACLE_CSE="2", MWCC_ORACLE_LOCALS="1", MWCC_ORACLE_BLOCKS="2", MWCC_ORACLE_SCHEDULE="1",
                           MWCC_ORACLE_STAGES="1", MWCC_ORACLE_INSTANTIATION="1")
                if mode != "unset":
                    env["MWCC_ORACLE_TRACE_FILE"] = "" if mode == "empty" else "0" if mode == "zero" else (
                        str(directory / "missing/trace.txt") if mode == "missing-parent" else str(directory) if mode == "directory" else str(trace))
                executable = root / f"build/{version}/oracle/mwcceppc.exe"
                launch = [str(executable)]
                if os.name != "nt":
                    loader = hybrid / "build/tools/wibo"
                    assert loader.is_file(), loader
                    launch.insert(0, str(loader))
                result = subprocess.run([*launch, *case["flags"], "-c", str(fixtures / case["source"]), "-o", str(directory / "result.o")],
                    cwd=directory, env=env, capture_output=True, timeout=120)
                for stream in ["stdout", "stderr"]:
                    (directory / stream).write_bytes(getattr(result, stream))
                assert result.returncode == 0, (version, mode, label, result.stderr[-2000:])
                file_output = trace.read_bytes() if trace.exists() else b""
                if mode in ["zero", "append"]:
                    assert file_output.startswith(prefix) and len(file_output) > len(prefix)
                    observed = file_output[len(prefix):]
                    assert not result.stderr, (version, mode, label)
                else:
                    assert not trace.exists(), (version, mode, label)
                    observed = result.stderr
                text = observed.decode("latin-1")
                assert any(line.startswith("ra.before ") for line in text.splitlines())
                runs.append(dict(directory=directory, trace=full_trace(text), endings=trace_line_endings(text),
                    object=(directory / "result.o").read_bytes(), stdout=result.stdout, producer_sha256=sha(executable),
                    raw_trace_sha256=hashlib.sha256(observed).hexdigest()))
            a, b = runs
            for field in ["trace", "endings", "object", "stdout"]:
                assert a[field] == b[field], (version, mode, field, a["directory"], b["directory"])
            item["modes"].append(dict(mode=mode, records=len(a["trace"]), object_sha256=hashlib.sha256(a["object"]).hexdigest(),
                runs=[dict(directory=str(r["directory"]), producer_sha256=r["producer_sha256"], raw_trace_sha256=r["raw_trace_sha256"],
                           files={p.name: sha(p) for p in r["directory"].iterdir() if p.is_file()}) for r in runs]))
        evidence["versions"].append(item)
        (stage / "trace-files-verification.json").write_text(json.dumps(evidence, indent=2) + "\n")
        print(f"{version}: real trace append, literal zero, absent/empty/failing paths and stderr output agree", flush=True)
    evidence["complete"] = True
    (stage / "trace-files-verification.json").write_text(json.dumps(evidence, indent=2) + "\n")


if __name__ == "__main__":
    main()
