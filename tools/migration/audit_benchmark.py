"""Independently verify benchmark inputs/results/statistics and retain measured sources."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import shutil
import statistics

from compare_backends import difference, normalize
from verify_archived_kits import ROOT, sha, write_json


def retain(root, source, name, digest):
    target = root / name
    if not target.exists():
        assert sha(source) == digest, source
        shutil.copyfile(source, target)
    assert sha(target) == digest, target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--rust-worker", type=Path)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--cli", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    path = root / ("cli-results.json" if args.cli else "results.json")
    data = json.loads(path.read_text())
    workloads = json.loads((args.inputs / "workloads.json").read_text())
    assert data["complete"] and data["format_version"] == 1
    input_key = "inputs_sha256" if args.cli else "workloads_manifest_sha256"
    assert data[input_key] == sha(args.inputs / "workloads.json")
    for rel, digest in workloads["reference_sources"].items():
        assert sha(Path(workloads["reference"]) / rel) == digest
    for item in workloads["workloads"]:
        manifest = args.inputs / item["manifest"]
        assert sha(manifest) == item["sha256"]
        w = json.loads(manifest.read_text())
        assert sha(w["trace"]) == w["trace_sha256"] == item["trace_sha256"]
    if args.cli:
        assert args.binary
        retain(root, args.binary, "measured-mwcc-tools" + args.binary.suffix, data["binary_sha256"])
        retain(root, ROOT / "tools/migration/benchmark_cli.py", "harness.py", data["harness_sha256"])
        assert {r["workload"] for r in data["rows"]} == {"GC_2_7", "echoes-renderparticles"}
        assert len(data["rows"]) == 2
        for row in data["rows"]:
            values = {}
            assert row["semantic_results_equal"]
            version = json.loads((args.inputs / (row["workload"] + ".json")).read_text())["version"]
            for engine, field in (("python", "python_compiler_root"), ("rust", "rust_compiler_root")):
                compiler = Path(row[field])
                assert sha(compiler / f"build/{version}/oracle/mwcceppc.exe") == row["producer_sha256"][engine]
                config = json.loads((compiler / f"config/{version}/config.json").read_text())
                assert sha(compiler / config["original"]) == row["original_sha256"]
            for engine, metrics in row["engines"].items():
                assert [r["sample"] for r in metrics["runs"]] == [-1, 0, 1, 2]
                times = [r["elapsed_ns"] for r in metrics["runs"] if r["sample"] >= 0]
                assert metrics["median_ns"] == statistics.median(times)
                assert metrics["min_ns"] == min(times) and metrics["max_ns"] == max(times)
                assert metrics["sampled_tree_peak_rss"] == max(r["sampled_tree_peak_rss"] for r in metrics["runs"] if r["sample"] >= 0)
                for r in metrics["runs"]:
                    prefix = root / f"{row['workload']}-{r['sample']}-{engine}"
                    assert r["observations"] > 0 and r["sampled_tree_peak_rss"] > 0 and r["elapsed_ns"] > 0
                    for stream in ("stdout", "stderr"):
                        assert sha(prefix.with_suffix("." + stream)) == r[stream + "_sha256"]
                    result = json.loads(prefix.with_suffix(".stdout").read_text())
                    assert result["identical"] is True
                    values[(r["sample"], engine)] = normalize(result)
            for sample in (-1, 0, 1, 2):
                assert difference(values[(sample, "python")], values[(sample, "rust")]) is None
    else:
        assert args.rust_worker
        retain(root, args.rust_worker, "worker" + args.rust_worker.suffix, data["rust_worker_sha256"])
        for source, target, field in (("tools/migration/benchmark.py", "harness.py", "harness_sha256"),
                                      ("tools/migration/benchmark_worker.py", "python_worker.py", "python_worker_sha256"),
                                      ("crates/mwcc-tools/examples/migration_bench.rs", "rust_worker.rs", "rust_worker_source_sha256")):
            retain(root, ROOT / source, target, data[field])
        expected = {(w["label"], phase) for w in workloads["workloads"] for phase in
                    ("parse", "analyze", "replay", "search", "compile_checked", "end_to_end_checked")}
        assert {(r["workload"], r["phase"]) for r in data["rows"]} == expected
        assert len(data["rows"]) == len(expected)
        for row in data["rows"]:
            assert row["semantic_results_equal"] and set(row["engines"]) == {"python", "rust"}
            results = []
            for engine, metrics in row["engines"].items():
                prefix = root / f"{row['workload']}-{row['phase']}-{engine}"
                assert sha(prefix.with_suffix(".result.json")) == metrics["result_sha256"]
                for stream in ("stdout", "stderr"):
                    assert sha(prefix.with_suffix("." + stream)) == metrics[stream + "_sha256"]
                raw = json.loads(prefix.with_suffix(".stdout").read_text())
                assert raw["samples_ns"] == metrics["samples_ns"] and raw["batch"] == metrics["batch"]
                assert raw["phase"] == row["phase"] and raw["warmups"] == 1
                assert len(raw["samples_ns"]) == 5 and raw["batch"] > 0
                times = [n / raw["batch"] for n in raw["samples_ns"]]
                assert min(times) > 0 and metrics["median_ns"] == statistics.median(times)
                assert metrics["min_ns"] == min(times) and metrics["max_ns"] == max(times)
                assert metrics["observations"] > 0 and metrics["sampled_tree_peak_rss"] >= metrics["sampled_worker_peak_rss"] > 0
                results.append(json.loads(prefix.with_suffix(".result.json").read_text()))
            if row["phase"] == "end_to_end_checked":
                results = [normalize(value) for value in results]
            assert difference(*results) is None, (row["workload"], row["phase"], difference(*results))
    for row in data["rows"]:
        assert row["python_over_rust"] == row["engines"]["python"]["median_ns"] / row["engines"]["rust"]["median_ns"]
    # Producer identities come from the already sealed runtime proofs, not a
    # claim that the benchmark's source checkout is a clean release commit.
    compilers = {}
    for item in workloads["workloads"]:
        w = json.loads((args.inputs / item["manifest"]).read_text())
        compiler_root = Path(w["compile"]["root"])
        version = w["version"]
        sealed = json.loads((compiler_root / "producer-verification.json").read_text())
        expected = next(v["producer_sha256"] for v in sealed["versions"] if v["version"] == version)
        producer = compiler_root / f"build/{version}/oracle/mwcceppc.exe"
        assert sha(producer) == expected
        config = json.loads((compiler_root / f"config/{version}/config.json").read_text())
        compilers[version] = dict(producer_sha256=expected, original_sha256=sha(compiler_root / config["original"]))
        if (compiler_root / "build/tools/wibo").is_file():
            compilers[version]["wibo_sha256"] = sha(compiler_root / "build/tools/wibo")
    write_json(root / "audit.json", dict(complete=True, rows=len(data["rows"]), compilers=compilers,
               audited_utc=datetime.now(timezone.utc).isoformat(), results_sha256=sha(path), auditor_sha256=sha(__file__)))
    print(f"benchmark audit: {len(data['rows'])} rows; result, input, timing, memory and compiler identities verified")


if __name__ == "__main__":
    main()
