"""Measure fresh CLI processes, including startup, checked compilation and JSON output.

Uses the same compiler image/flags for both engines. One warm-up pair precedes
three measured pairs; sampled RSS includes the CLI and compiler descendants.
"""
import argparse
import json
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import tempfile
import threading
import time

from compare_backends import difference, normalize
from verify_archived_kits import ROOT, sha, write_json


def main():
    import psutil
    assert psutil.__version__ == "7.0.0"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--python-compiler-root", type=Path, required=True,
                        help="frozen C producer for the complete legacy stack")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="benchmark-cli-", dir=args.output.resolve()))
    inputs = json.loads((args.inputs / "workloads.json").read_text())
    evidence = dict(complete=False, format_version=1, platform=platform.platform(), binary_sha256=sha(args.binary),
                    inputs_sha256=sha(args.inputs / "workloads.json"), harness_sha256=sha(__file__), psutil=psutil.__version__,
                    timing_scope="fresh process startup through exit; Python/C producer versus Rust/Rust producer; includes checked compilation, trace analysis and report JSON serialization/output; warm filesystem cache",
                    memory_scope="sampled sum of CLI/descendant RSS at 5ms; shared pages may be counted twice", rows=[])
    def save():
        write_json(root / "cli-results.json", evidence)
    bootstrap = '''import sys,pathlib
reference,root,*arguments=sys.argv[1:]
sys.path.insert(0,str(pathlib.Path(reference)/"tools"))
import oracle
oracle.ROOT=pathlib.Path(root)
sys.argv=["oracle",*arguments]
oracle.main()
'''
    print(f"retained CLI benchmark: {root}", flush=True)
    save()
    for item in inputs["workloads"]:
        if item["label"] not in ("GC_2_7", "echoes-renderparticles"):
            continue
        path = args.inputs / item["manifest"]
        assert sha(path) == item["sha256"]
        data = json.loads(path.read_text())
        c = data["compile"]
        python_root = args.python_compiler_root.resolve()
        rust_root = Path(c["root"])
        configs = [json.loads((r / f"config/{data['version']}/config.json").read_text()) for r in (python_root, rust_root)]
        stock = [sha(r / config["original"]) for r, config in zip((python_root, rust_root), configs)]
        assert stock[0] == stock[1]
        producers = {name: sha(r / f"build/{data['version']}/oracle/mwcceppc.exe") for name, r in (("python", python_root), ("rust", rust_root))}
        if sys.platform != "win32":
            assert sha(python_root / "build/tools/wibo") == sha(rust_root / "build/tools/wibo")
        common = ["--version", data["version"], "--json", c["source"], "--", *c["flags"]]
        measurements = {"python": [], "rust": []}
        for sample in range(-1, 3):
            outputs = []
            for engine in (("python", "rust") if sample % 2 else ("rust", "python")):
                prefix = root / f"{item['label']}-{sample}-{engine}"
                command = ([sys.executable, "-c", bootstrap, str(args.reference.resolve()), str(python_root), *common]
                           if engine == "python" else [str(args.binary.resolve()), "oracle", "--oracle-root", c["root"], *common])
                stop = threading.Event()
                observed = dict(peak=0, observations=0, timed_out=False)
                began = time.perf_counter_ns()
                with prefix.with_suffix(".stdout").open("wb") as stdout, prefix.with_suffix(".stderr").open("wb") as stderr:
                    process = subprocess.Popen(command, cwd=c["working_directory"], stdout=stdout, stderr=stderr)
                    parent = psutil.Process(process.pid)
                    def monitor():
                        while not stop.is_set():
                            try:
                                children = parent.children(recursive=True)
                                rss = parent.memory_info().rss
                                for child in children:
                                    try:
                                        rss += child.memory_info().rss
                                    except (psutil.NoSuchProcess, psutil.AccessDenied):
                                        pass
                                observed["peak"] = max(observed["peak"], rss)
                                observed["observations"] += 1
                                if time.perf_counter_ns()-began > 900*10**9:
                                    observed["timed_out"] = True
                                    for child in children:
                                        try:
                                            child.kill()
                                        except psutil.NoSuchProcess:
                                            pass
                                    process.kill()
                            except (psutil.NoSuchProcess, psutil.AccessDenied):
                                pass
                            stop.wait(.005)
                    sampler = threading.Thread(target=monitor, daemon=True)
                    sampler.start()
                    code = process.wait()
                    elapsed = time.perf_counter_ns()-began
                    stop.set()
                    sampler.join()
                assert code == 0 and not observed["timed_out"], prefix.with_suffix(".stderr").read_text()
                assert observed["observations"] > 0 and observed["peak"] > 0
                result = json.loads(prefix.with_suffix(".stdout").read_text())
                assert result["identical"] is True
                outputs.append(normalize(result))
                metrics = dict(sample=sample, elapsed_ns=elapsed, sampled_tree_peak_rss=observed["peak"],
                               observations=observed["observations"], stdout_sha256=sha(prefix.with_suffix(".stdout")),
                               stderr_sha256=sha(prefix.with_suffix(".stderr")), command=command)
                measurements[engine].append(metrics)
            assert difference(*outputs) is None, difference(*outputs)
        row = dict(workload=item["label"], semantic_results_equal=True, engines={}, producer_sha256=producers,
                   original_sha256=stock[0], python_compiler_root=str(python_root), rust_compiler_root=str(rust_root))
        for engine, values in measurements.items():
            times = [v["elapsed_ns"] for v in values if v["sample"] >= 0]
            row["engines"][engine] = dict(runs=values, median_ns=statistics.median(times), min_ns=min(times), max_ns=max(times),
                                         sampled_tree_peak_rss=max(v["sampled_tree_peak_rss"] for v in values if v["sample"] >= 0))
        row["python_over_rust"] = row["engines"]["python"]["median_ns"] / row["engines"]["rust"]["median_ns"]
        evidence["rows"].append(row)
        save()
        print(f"{item['label']} fresh CLI: checked results agree; Python/Rust {row['python_over_rust']:.2f}x", flush=True)
    evidence["complete"] = True
    save()


if __name__ == "__main__":
    main()
