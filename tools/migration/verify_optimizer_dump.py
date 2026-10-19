"""Compare real optimizer dump files and trace/object output with frozen C hooks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from verify_producer import ROOT, graph_trace, sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--hybrid-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", action="append")
    args = parser.parse_args()
    reference, hybrid = args.oracle_root.resolve(), args.hybrid_root.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    fixtures = ROOT / "tests/fixtures/oracle"
    manifest = json.loads((fixtures / "manifest.json").read_text())
    versions = args.version or ["GC_1_3", "GC_1_3_2", "GC_2_7"]
    evidence = dict(versions=[], complete=False)
    for version in versions:
        case = next(c for c in manifest["cases"] if c["version"] == version and c["source"].endswith("/cse.c"))
        item = dict(version=version, source_sha256=sha(fixtures / case["source"]), flags=case["flags"], modes=[])
        for mode in ["unset", "empty", "zero", "append"]:
            records = []
            for label, root in [("C", reference), ("Rust", hybrid)]:
                directory = Path(tempfile.mkdtemp(prefix=f"{version}-{mode}-{label}-", dir=args.output.resolve()))
                executable = root / f"build/{version}/oracle/mwcceppc.exe"
                dump = directory / ("0" if mode == "zero" else "optimizer.dump")
                prefix = b"existing dump\n" if mode == "append" else b""
                if prefix:
                    dump.write_bytes(prefix)
                env = {key: value for key, value in os.environ.items() if not key.startswith("MWCC_ORACLE_")}
                env.update(MWCC_ORACLE_TRACE_FILE=str(directory / "trace.txt"), MWCC_ORACLE_CSE="2", MWCC_ORACLE_LOCALS="1")
                if mode != "unset":
                    env["MWCC_ORACLE_IRO_DUMP"] = "" if mode == "empty" else "0" if mode == "zero" else str(dump)
                launch = [str(executable)]
                if os.name != "nt":
                    # Match the primary verifier's private pinned loader on both sides.
                    loader = hybrid / "build/tools/wibo"
                    assert loader.is_file(), loader
                    launch.insert(0, str(loader))
                command = [*launch, *case["flags"], "-c", str(fixtures / case["source"]), "-o", str(directory / "result.o")]
                result = subprocess.run(command, cwd=directory, env=env, capture_output=True, timeout=120)
                for stream in ["stdout", "stderr"]:
                    (directory / stream).write_bytes(getattr(result, stream))
                assert result.returncode == 0, (version, mode, label, result.stderr[-4000:])
                trace = (directory / "trace.txt").read_text(encoding="utf-8")
                assert any(line.startswith("ce.") for line in trace.splitlines())
                content = dump.read_bytes() if dump.exists() else b""
                if mode in ["zero", "append"]:
                    assert content.startswith(prefix) and len(content) > len(prefix), (version, mode, label)
                else:
                    assert not dump.exists(), (version, mode, label)
                records.append(dict(directory=directory, content=content, object=(directory / "result.o").read_bytes(),
                    trace=graph_trace(trace), stdout=result.stdout, stderr=result.stderr, producer_sha256=sha(executable)))
            a, b = records
            for key in ["content", "object", "trace", "stdout", "stderr"]:
                assert a[key] == b[key], (version, mode, key, a["directory"], b["directory"])
            item["modes"].append(dict(mode=mode, dump_bytes=len(a["content"]),
                dump_sha256=hashlib.sha256(a["content"]).hexdigest(), object_sha256=hashlib.sha256(a["object"]).hexdigest(),
                traces=len(a["trace"]), runs=[dict(directory=str(r["directory"]), producer_sha256=r["producer_sha256"],
                    logs={p.name: sha(p) for p in r["directory"].iterdir() if p.is_file()}) for r in records]))
        evidence["versions"].append(item)
        (args.output / "optimizer-dump-verification.json").write_text(json.dumps(evidence, indent=2) + "\n")
        print(f"{version}: unset/empty/zero-path/appended optimizer dumps, traces, objects and diagnostics agree", flush=True)
    evidence["complete"] = True
    (args.output / "optimizer-dump-verification.json").write_text(json.dumps(evidence, indent=2) + "\n")


if __name__ == "__main__":
    main()
