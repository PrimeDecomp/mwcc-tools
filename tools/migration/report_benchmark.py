"""Render audited local benchmark measurements as a concise Markdown report."""
import argparse
import json
from pathlib import Path

from compare_backends import difference
from verify_archived_kits import sha


PHASES = {"parse": "Allocation/frame parse", "analyze": "Allocation analysis", "replay": "GPR/FPR replay",
          "search": "Seeded renumber search", "compile_checked": "Checked compilation", "end_to_end_checked": "Compile + full report (resident worker)"}


def load(root, cli=False):
    filename = "cli-results.json" if cli else "results.json"
    data = json.loads((root / filename).read_text())
    audit = json.loads((root / "audit.json").read_text())
    assert data["complete"] and audit["complete"] and audit["results_sha256"] == sha(root / filename)
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("windows", "linux", "windows_cli", "linux_cli", "windows_inputs", "linux_inputs", "output"):
        parser.add_argument("--" + name.replace("_", "-"), type=Path, required=True)
    args = parser.parse_args()
    windows, linux = load(args.windows), load(args.linux)
    wc, lc = load(args.windows_cli, True), load(args.linux_cli, True)
    a = json.loads((args.windows_inputs / "workloads.json").read_text())
    b = json.loads((args.linux_inputs / "workloads.json").read_text())
    assert a["reference_sources"] == b["reference_sources"]
    assert [w["label"] for w in a["workloads"]] == [w["label"] for w in b["workloads"]]
    for left, right in zip(a["workloads"], b["workloads"]):
        x = json.loads((args.windows_inputs / left["manifest"]).read_text())
        y = json.loads((args.linux_inputs / right["manifest"]).read_text())
        assert x["trace_sha256"] == y["trace_sha256"] and difference(x["search"], y["search"]) is None
        for phase in ("parse", "analyze", "replay", "search"):
            p = args.windows / f"{left['label']}-{phase}-rust.result.json"
            q = args.linux / f"{right['label']}-{phase}-rust.result.json"
            assert difference(json.loads(p.read_text()), json.loads(q.read_text())) is None
    lines = ["# Rust migration benchmarks", "",
             "These local measurements compare the frozen Python baseline with the Rust implementation. "
             "Every accepted row has matching semantic results; the core results also agree across hosts. "
             "This is behavioral evidence, not a requirement to retain legacy APIs.", "",
             "## Method", "",
             "- Rust 1.96.0 release builds; Python 3.13; psutil 7.0.0. Workers run serially on the same physical machine, with background activity uncontrolled.",
             "- Core rows use five samples after one warm-up. Batches target about 100 ms, capped at 1,000 calls; tables show per-operation medians. Raw files retain every sample and batch count.",
             "- Core input is the identical allocation/frame (`ra.*`/`fr.*`) projection on both implementations. Parsing additional event families is outside this parse measurement. Analysis is allocation analysis, not the entire oracle report pipeline.",
             "- Replay processes the last GPR/FPR round of each allocation. Search uses seed 2, 600 iterations, three restarts and at most 12 selected movable nodes; it measures renumbering only, with edge-edit kinds disabled.",
             "- Library checked-compilation rows use the same Rust producer and stock compiler for both engines, isolating host-tool overhead. Resident-worker compile/report rows exclude process startup and result serialization. Fresh CLI rows compare the complete Python/C-producer and Rust/Rust-producer stacks, including startup, checked compilation, analysis and report JSON output; one warm-up pair precedes three measured pairs, with alternating engine order. Stock compiler bytes and Linux loaders match across the stacks.",
             "- Linux measurements use verified host-native copies of the same source/header bytes and reference Python modules. The initial Windows-mounted-source profile was interrupted after identifying I/O domination; it remains retained separately and is not included in these tables.",
             "- RSS is sampled every 5 ms and summed across the worker/CLI and descendants. Peaks include setup and output, may miss short-lived peaks, and can count shared pages twice. They are process measurements, not isolated heap profiles.",
             "- Ratios are Python elapsed time divided by Rust elapsed time: greater than 1 means Rust is faster. No timing threshold is used as a correctness gate.", "", "## Workloads", "",
             "| Workload | Allocations | Projected trace lines | Search graph nodes | Selected movable nodes |", "|---|---:|---:|---:|---:|"]
    for item in a["workloads"]:
        x = json.loads((args.windows_inputs / item["manifest"]).read_text())
        lines.append(f"| {item['label']} | {x['allocations']} | {x['lines']:,} | {len(x['search']['round']['nodes'])} | {len(x['search']['options']['nodes'])} |")
    lines += ["", "The synthetic sets contain six fixture cases per compiler version. The captured Echoes input contains the real unit's allocations; its search problem is `RenderParticles__11CElementGenFv`. Game-derived inputs/results remain local and are not committed.", ""]
    for name, data, cli in (("Windows", windows, wc), ("Linux / WSL Debian", linux, lc)):
        lines += [f"## {name}", "", f"Environment: `{data['platform']}`; {data['logical_cpus']} logical CPUs. {data['rustc']}.", "",
                  "Synthetic sets, Python/Rust median elapsed ratios:", "",
                  "| Phase | GC 1.3 | GC 1.3.2 | GC 2.7 |", "|---|---:|---:|---:|"]
        for phase, title in PHASES.items():
            values = [next(r for r in data["rows"] if r["workload"] == version and r["phase"] == phase)["python_over_rust"]
                      for version in ("GC_1_3", "GC_1_3_2", "GC_2_7")]
            lines.append(f"| {title} | " + " | ".join(f"{v:.2f}x" for v in values) + " |")
        lines += ["", "Captured Echoes unit:", "",
                  "| Phase | Python ms | Rust ms | Python/Rust | Python tree RSS MiB | Rust tree RSS MiB |",
                  "|---|---:|---:|---:|---:|---:|"]
        for row in [r for r in data["rows"] if r["workload"] == "echoes-renderparticles"] + [r for r in cli["rows"] if r["workload"] == "echoes-renderparticles"]:
            p, r = row["engines"]["python"], row["engines"]["rust"]
            phase = PHASES[row["phase"]] if "phase" in row else "Fresh CLI + JSON output"
            lines.append(f"| {phase} | {p['median_ns']/1e6:.2f} | {r['median_ns']/1e6:.2f} | {row['python_over_rust']:.2f}x | {p['sampled_tree_peak_rss']/2**20:.1f} | {r['sampled_tree_peak_rss']/2**20:.1f} |")
        small = next(r for r in cli["rows"] if r["workload"] == "GC_2_7")
        lines += ["", f"The synthetic GC 2.7 fresh CLI median is {small['engines']['python']['median_ns']/1e6:.2f} ms for Python and {small['engines']['rust']['median_ns']/1e6:.2f} ms for Rust ({small['python_over_rust']:.2f}x).", ""]
    lines += ["## Interpretation", "",
              "The component gains do not imply the same gain for a complete compiler-backed command. Check the checked-compilation and fresh-CLI rows separately. Allocation-analysis RSS is also a separate tradeoff: the Rust worker uses more memory on this captured unit even where its timed allocation analysis is faster.", "",
              "The measured scope does not include source-variant search, reductions or the deferred provenance extensions. No faster allocator-search algorithm was introduced; both implementations execute the same seeded search contract.", "", "## Retained evidence", ""]
    for name, root, cli in (("Windows core", args.windows, False), ("Linux core", args.linux, False), ("Windows CLI", args.windows_cli, True), ("Linux CLI", args.linux_cli, True)):
        location = str(root)
        if location.startswith("\\\\wsl.localhost\\Debian\\"):
            location = "/" + location.removeprefix("\\\\wsl.localhost\\Debian\\").replace("\\", "/")
        lines.append(f"- {name}: `{location}`; results SHA-256 `{sha(root / ('cli-results.json' if cli else 'results.json'))}`. `audit.json` verifies inputs, results, statistics and compiler identities.")
    lines += ["", "Reproduce with `tools/migration/benchmark.py`, `benchmark_cli.py`, `audit_benchmark.py` and this report generator. Input relocation changes host paths and compiler-root selection only; trace bytes and search problems are checked unchanged.", ""]
    args.output.write_text("\n".join(lines), encoding="utf-8")
    print(args.output)


if __name__ == "__main__":
    main()
