"""Prepare fixed workloads or benchmark Python/Rust workers serially with sampled tree RSS.

Run with Python 3.13 and psutil==7.0.0. No timings are accepted until semantic
results agree. Private game traces/results stay beneath the selected output.
"""
import argparse
import hashlib
import importlib
import json
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import tempfile
import time

from compare_backends import difference, normalize
from verify_archived_kits import ROOT, sha, write_json


def serialize_round(r):
    return {"class": r.cls, "k": r.k, "start": r.start, "count": r.count,
        "nodes": {v: {"pointer": n["p"], **{k: n[k] for k in ("degree", "flags", "color", "neighbors")}} for v, n in r.nodes.items()},
        "order": r.order, "mask": r.mask, "mask_after": r.mask_after, "ok": r.ok,
        "colors": r.colors, "color_order": list(r.colors)}


def prepare(args):
    sys.path.insert(0, str(args.reference.resolve() / "tools"))
    oracle, colouring = importlib.import_module("oracle"), importlib.import_module("colouring")
    output = Path(tempfile.mkdtemp(prefix="benchmark-inputs-", dir=args.output.resolve()))
    reference_sources = {n: sha(args.reference / n) for n in ("tools/oracle.py", "tools/colouring.py", "include/compiler/PCode.h")}
    if args.inputs:
        # Reuse identical traces/problems across hosts, changing host filesystem
        # paths and the explicitly chosen compiler root only.
        previous = args.inputs.resolve()
        result = json.loads((previous / "workloads.json").read_text())
        assert result["reference_sources"] == reference_sources
        result["relocated_from_sha256"] = sha(previous / "workloads.json")
        def relocate(value):
            if sys.platform != "win32" and len(value) > 2 and value[1] == ":":
                return subprocess.check_output(["wslpath", "-u", value], text=True).strip()
            return value
        for item in result["workloads"]:
            source = previous / item["manifest"]
            assert sha(source) == item["sha256"]
            data = json.loads(source.read_text())
            trace = output / (item["label"] + ".trace")
            trace.write_bytes((previous / (item["label"] + ".trace")).read_bytes())
            assert sha(trace) == item["trace_sha256"]
            data["trace"] = str(trace)
            data["compile"]["root"] = str(args.compiler_root.resolve())
            for key in ("working_directory", "source"):
                data["compile"][key] = relocate(data["compile"][key])
            if sys.platform != "win32":
                if item["label"] == "echoes-renderparticles":
                    if not args.compile_proof:
                        raise RuntimeError("Linux game compilation requires --compile-proof for an identical host-native project")
                    local = json.loads((args.compile_proof / "verification.json").read_text())
                    current = next(c for c in local["cases"] if c["kit"] == "echoes")
                    old_project = Path(data["compile"]["working_directory"])
                    old = json.loads((old_project.parent / "verification.json").read_text())
                    expected = next(c for c in old["cases"] if c["kit"] == "echoes")
                    assert current["input_hashes"] == expected["input_hashes"]
                    project = args.compile_proof.resolve() / "echoes-project"
                    verified = {n: h for n, h in current["input_hashes"].items() if n != "build.ninja"}
                    for name, digest in verified.items():
                        assert sha(project / name) == digest, name
                    data["compile"]["working_directory"] = str(project)
                    data["compile"]["project_inputs_sha256"] = hashlib.sha256(json.dumps(verified, sort_keys=True).encode()).hexdigest()
                else:
                    source = Path(data["compile"]["source"])
                    destination = output / "sources" / source.name
                    destination.parent.mkdir(exist_ok=True)
                    destination.write_bytes(source.read_bytes())
                    data["compile"]["source_sha256"] = sha(destination)
                    data["compile"]["source"] = "sources/" + source.name
                    data["compile"]["working_directory"] = str(output)
            path = output / item["manifest"]
            write_json(path, data)
            item["sha256"] = sha(path)
        result["reference"] = str(args.reference.resolve())
        result["compiler_filesystem"] = "host-native copies; game source/header bytes verified against the original sealed project; Ninja path adaptation excluded from that byte comparison because flags are supplied explicitly"
        write_json(output / "workloads.json", result)
        print(output)
        return
    fixtures = ROOT / "tests/fixtures/oracle"
    original = json.loads((fixtures / "manifest.json").read_text())
    workloads = []
    for version in ("GC_1_3", "GC_1_3_2", "GC_2_7"):
        cases = [c for c in original["cases"] if c["version"] == version]
        raw = "".join((fixtures / (c["id"] + ".trace")).read_text() for c in cases)
        source = next(c for c in cases if c["id"].endswith("/pipeline"))
        workloads.append((version, version, raw, dict(source=str((fixtures / source["source"]).resolve()),
                          flags=source["flags"], working_directory=str(fixtures)), None))
    if args.kit_proof:
        proof = args.kit_proof.resolve()
        sealed = json.loads((proof / "verification.json").read_text())
        assert sealed["complete"]
        # The legacy saved session is already compiled and has no private trace-file dependency.
        raw = (proof / "echoes-project/saved.trace").read_text()
        c = next(c for c in sealed["cases"] if c["kit"] == "echoes")
        project = proof / "echoes-project"
        # Keep the exact original edge flags, including quoted pragma arguments.
        unit = next(u for u in json.loads((project / "objdiff.json").read_text())["units"] if u["name"] == c["unit"])
        from verify_archived_kits import snapshot
        snapshot(project, c["unit"])
        import re, shlex
        ninja = re.sub(r"\$\r?\n\s*", "", (project / "build.ninja").read_text())
        block = next(m for m in re.finditer(r"^build (.*?): mwcc\w* .*\n((?:  [^\n]*\n)+)", ninja, re.M)
                     if m.group(1).replace("\\", "/") == unit["base_path"])
        flags = shlex.split(re.search(r"^  cflags = (.*)$", block.group(2), re.M).group(1))
        flags = [f for f in flags if f not in ("-MD", "-MMD", "-c", "--")]
        workloads.append(("echoes-renderparticles", "GC_2_7", raw,
                          dict(source=unit["metadata"]["source_path"], flags=flags, working_directory=str(project)),
                          "RenderParticles__11CElementGenFv"))
    result = dict(format_version=1, reference=str(args.reference.resolve()),
                  reference_sources=reference_sources,
                  reference_commit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=args.reference, text=True).strip(),
                  workloads=[], parse_scope="allocation/frame records only; same ra./fr. input for both implementations")
    for label, version, raw, compile_input, link in workloads:
        # Project only common allocation/frame grammar; do not time Rust parsing
        # additional event families which the reference parse_trace ignores.
        text = "\n".join(line for line in raw.splitlines() if line.startswith(("ra.", "fr."))) + "\n"
        trace = output / (label + ".trace")
        trace.write_text(text, encoding="utf-8", newline="\n")
        allocations = oracle.parse_trace(text.splitlines())[0]
        choices = []
        for name, before, after, _ in allocations:
            a = colouring.Allocation(name, before, after, {})
            r = a.last_round("GPR")
            if r is None or (link and before.link != link):
                continue
            replay = colouring.replay(r, "GPR", a.register_count("GPR"))
            colours = sorted({c for v, c in replay["colour"].items() if v >= 32 and c >= 14})
            if len(colours) >= 2 and not replay["spilled"]:
                choices.append((len(r.nodes), a, r, replay, colours[-2:]))
        assert choices, label
        _, a, r, replay, swap = max(choices, key=lambda c: c[0])
        targets = {v: (swap[1] if c == swap[0] else swap[0] if c == swap[1] else c)
                   for v, c in replay["colour"].items() if v >= 32 and c >= 0}
        movers = [v for v in replay["order"] if v >= 32 and r.nodes[v]["flags"] & 4 == 0 and replay["colour"].get(v, -1) >= 14]
        nodes = list(dict.fromkeys([v for v in movers if targets.get(v) != replay["colour"].get(v)] + movers))[:12]
        data = dict(format_version=1, label=label, trace=str(trace), trace_sha256=sha(trace), version=version,
                    allocations=len(allocations), lines=len(text.splitlines()),
                    search=dict(link=a.link, **{"class": "GPR"}, round=serialize_round(r), count=a.register_count("GPR"), targets=targets,
                                options=dict(kinds=[], iterations=600, seed="2", restarts=3, nodes=nodes)),
                    compile=dict(root=str(args.compiler_root.resolve()), **compile_input))
        path = output / (label + ".json")
        write_json(path, data)
        result["workloads"].append(dict(label=label, manifest=path.name, sha256=sha(path), trace_sha256=sha(trace)))
    write_json(output / "workloads.json", result)
    print(output)


def benchmark(args):
    import psutil
    assert psutil.__version__ == "7.0.0"
    inputs = args.inputs.resolve()
    workloads = json.loads((inputs / "workloads.json").read_text())
    output = Path(tempfile.mkdtemp(prefix="benchmark-run-", dir=args.output.resolve()))
    evidence = dict(format_version=1, complete=False, platform=platform.platform(), machine=platform.machine(),
                    cpu=platform.processor(), logical_cpus=psutil.cpu_count(), physical_cpus=psutil.cpu_count(logical=False),
                    python=sys.version, psutil=psutil.__version__, rust_worker_sha256=sha(args.rust_worker),
                    rust_worker_source_sha256=sha(ROOT / "crates/mwcc-tools/examples/migration_bench.rs"),
                    rustc=subprocess.check_output(["rustc", "--version"], cwd=ROOT, text=True).strip(),
                    python_worker_sha256=sha(ROOT / "tools/migration/benchmark_worker.py"),
                    harness_sha256=sha(__file__), workloads_manifest_sha256=sha(inputs / "workloads.json"),
                    sampling_interval_ms=5, memory_scope="sampled sum of resident sets for worker and descendants; shared pages may be counted twice; includes setup/output",
                    timing_scope="inside worker; setup/read/serialization excluded; one warm-up and one untimed result check; destruction included; Python GC collection before samples excluded; core batches independently calibrated to ~100ms, capped at 1000 calls",
                    rows=[])
    def save():
        write_json(output / "results.json", evidence)
    print(f"retained benchmark run: {output}", flush=True)
    save()
    for item in workloads["workloads"]:
        path = inputs / item["manifest"]
        assert sha(path) == item["sha256"]
        data = json.loads(path.read_text())
        assert sha(data["trace"]) == item["trace_sha256"]
        for phase in ("parse", "analyze", "replay", "search", "compile_checked", "end_to_end_checked"):
            if args.core_only and phase in ("compile_checked", "end_to_end_checked"):
                continue
            if args.only and item["label"] not in args.only:
                continue
            samples = args.samples
            batch = 1 if phase in ("compile_checked", "end_to_end_checked") else 0
            row = dict(workload=item["label"], phase=phase, inputs_sha256=sha(path), engines={})
            results = []
            # Serial execution, alternating order avoids always giving the same
            # implementation the cold filesystem cache. Timed core inputs are resident.
            engines = ("python", "rust") if len(evidence["rows"]) % 2 == 0 else ("rust", "python")
            for engine in engines:
                prefix = output / f"{item['label']}-{phase}-{engine}"
                result = prefix.with_suffix(".result.json")
                command = ([sys.executable, str(ROOT / "tools/migration/benchmark_worker.py")] if engine == "python" else [str(args.rust_worker.resolve())])
                command += [str(path), phase, str(samples), str(batch), str(result)]
                if engine == "python":
                    command.append(str(args.reference.resolve()))
                began = time.perf_counter_ns()
                with prefix.with_suffix(".stdout").open("wb") as stdout, prefix.with_suffix(".stderr").open("wb") as stderr:
                    process = subprocess.Popen(command, stdout=stdout, stderr=stderr)
                    tracked = psutil.Process(process.pid)
                    peak = worker_peak = observations = 0
                    while process.poll() is None:
                        rss = 0
                        try:
                            worker_rss = tracked.memory_info().rss
                            worker_peak = max(worker_peak, worker_rss)
                            rss += worker_rss
                            for child in tracked.children(recursive=True):
                                try:
                                    rss += child.memory_info().rss
                                except (psutil.NoSuchProcess, psutil.AccessDenied):
                                    pass
                            observations += 1
                        except (psutil.NoSuchProcess, psutil.AccessDenied):
                            pass
                        peak = max(peak, rss)
                        if time.perf_counter_ns() - began > 1800 * 10**9:
                            for child in tracked.children(recursive=True):
                                child.kill()
                            process.kill()
                            process.wait()
                            raise RuntimeError(f"worker timed out: {prefix}")
                        time.sleep(.005)
                assert process.returncode == 0, prefix.with_suffix(".stderr").read_text()
                metrics = json.loads(prefix.with_suffix(".stdout").read_text())
                assert metrics["batch"] > 0 and len(metrics["samples_ns"]) == samples
                if batch:
                    assert metrics["batch"] == batch
                assert all(v > 0 for v in metrics["samples_ns"])
                times = [v / metrics["batch"] for v in metrics["samples_ns"]]
                row["engines"][engine] = dict(**metrics, median_ns=statistics.median(times), min_ns=min(times), max_ns=max(times),
                    sampled_worker_peak_rss=worker_peak, sampled_tree_peak_rss=peak, observations=observations,
                    worker_wall_ns=time.perf_counter_ns()-began, result_sha256=sha(result),
                    stdout_sha256=sha(prefix.with_suffix(".stdout")), stderr_sha256=sha(prefix.with_suffix(".stderr")))
                results.append(json.loads(result.read_text()))
            if phase == "end_to_end_checked":
                results = [normalize(d) for d in results]
            found = difference(*results)
            if found:
                raise RuntimeError(f"{item['label']} {phase}: {found}")
            row["semantic_results_equal"] = True
            row["python_over_rust"] = row["engines"]["python"]["median_ns"] / row["engines"]["rust"]["median_ns"]
            evidence["rows"].append(row)
            save()
            print(f"{item['label']} {phase}: results agree; Python/Rust {row['python_over_rust']:.2f}x", flush=True)
    evidence["complete"] = True
    save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("prepare", "run"))
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler-root", type=Path)
    parser.add_argument("--kit-proof", type=Path)
    parser.add_argument("--compile-proof", type=Path)
    parser.add_argument("--inputs", type=Path)
    parser.add_argument("--rust-worker", type=Path)
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--core-only", action="store_true")
    parser.add_argument("--only", action="append")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.mode == "prepare":
        if not args.compiler_root:
            parser.error("prepare requires --compiler-root")
        prepare(args)
    else:
        if not args.inputs or not args.rust_worker or args.samples < 3:
            parser.error("run requires --inputs, --rust-worker and at least 3 samples")
        benchmark(args)


if __name__ == "__main__":
    main()
