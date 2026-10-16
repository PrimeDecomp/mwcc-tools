# Rust migration benchmarks

These local measurements compare the frozen Python baseline with the Rust implementation. Every accepted row has matching semantic results; the core results also agree across hosts. This is behavioral evidence, not a requirement to retain legacy APIs.

## Method

- Rust 1.96.0 release builds; Python 3.13; psutil 7.0.0. Workers run serially on the same physical machine, with background activity uncontrolled.
- Core rows use five samples after one warm-up. Batches target about 100 ms, capped at 1,000 calls; tables show per-operation medians. Raw files retain every sample and batch count.
- Core input is the identical allocation/frame (`ra.*`/`fr.*`) projection on both implementations. Parsing additional event families is outside this parse measurement. Analysis is allocation analysis, not the entire oracle report pipeline.
- Replay processes the last GPR/FPR round of each allocation. Search uses seed 2, 600 iterations, three restarts and at most 12 selected movable nodes; it measures renumbering only, with edge-edit kinds disabled.
- Library checked-compilation rows use the same Rust producer and stock compiler for both engines, isolating host-tool overhead. Resident-worker compile/report rows exclude process startup and result serialization. Fresh CLI rows compare the complete Python/C-producer and Rust/Rust-producer stacks, including startup, checked compilation, analysis and report JSON output; one warm-up pair precedes three measured pairs, with alternating engine order. Stock compiler bytes and Linux loaders match across the stacks.
- Linux measurements use verified host-native copies of the same source/header bytes and reference Python modules. The initial Windows-mounted-source profile was interrupted after identifying I/O domination; it remains retained separately and is not included in these tables.
- RSS is sampled every 5 ms and summed across the worker/CLI and descendants. Peaks include setup and output, may miss short-lived peaks, and can count shared pages twice. They are process measurements, not isolated heap profiles.
- Ratios are Python elapsed time divided by Rust elapsed time: greater than 1 means Rust is faster. No timing threshold is used as a correctness gate.

## Workloads

| Workload | Allocations | Projected trace lines | Search graph nodes | Selected movable nodes |
|---|---:|---:|---:|---:|
| GC_1_3 | 11 | 1,132 | 109 | 12 |
| GC_1_3_2 | 11 | 1,132 | 109 | 12 |
| GC_2_7 | 11 | 1,132 | 109 | 12 |
| echoes-renderparticles | 136 | 41,184 | 555 | 12 |

The synthetic sets contain six fixture cases per compiler version. The captured Echoes input contains the real unit's allocations; its search problem is `RenderParticles__11CElementGenFv`. Game-derived inputs/results remain local and are not committed.

## Windows

Environment: `Windows-11-10.0.26300-SP0`; 24 logical CPUs. rustc 1.96.0 (ac68faa20 2026-05-25).

Synthetic sets, Python/Rust median elapsed ratios:

| Phase | GC 1.3 | GC 1.3.2 | GC 2.7 |
|---|---:|---:|---:|
| Allocation/frame parse | 1.54x | 1.48x | 1.53x |
| Allocation analysis | 0.91x | 0.91x | 0.93x |
| GPR/FPR replay | 3.75x | 3.69x | 3.98x |
| Seeded renumber search | 5.33x | 5.38x | 5.42x |
| Checked compilation | 0.86x | 0.83x | 0.82x |
| Compile + full report (resident worker) | 0.83x | 0.85x | 0.85x |

Captured Echoes unit:

| Phase | Python ms | Rust ms | Python/Rust | Python tree RSS MiB | Rust tree RSS MiB |
|---|---:|---:|---:|---:|---:|
| Allocation/frame parse | 314.05 | 162.20 | 1.94x | 80.4 | 74.7 |
| Allocation analysis | 337.49 | 233.37 | 1.45x | 109.0 | 196.3 |
| GPR/FPR replay | 23.10 | 10.18 | 2.27x | 82.7 | 81.8 |
| Seeded renumber search | 495.56 | 168.85 | 2.93x | 81.5 | 75.8 |
| Checked compilation | 20715.09 | 20816.64 | 1.00x | 64.1 | 42.8 |
| Compile + full report (resident worker) | 19458.47 | 21690.24 | 0.90x | 237.5 | 329.2 |
| Fresh CLI + JSON output | 24831.24 | 22219.80 | 1.12x | 268.6 | 327.3 |

The synthetic GC 2.7 fresh CLI median is 225.10 ms for Python and 115.92 ms for Rust (1.94x).

## Linux / WSL Debian

Environment: `Linux-5.15.167.4-microsoft-standard-WSL2-x86_64-with-glibc2.41`; 8 logical CPUs. rustc 1.96.0 (ac68faa20 2026-05-25).

Synthetic sets, Python/Rust median elapsed ratios:

| Phase | GC 1.3 | GC 1.3.2 | GC 2.7 |
|---|---:|---:|---:|
| Allocation/frame parse | 1.63x | 1.54x | 1.67x |
| Allocation analysis | 1.00x | 1.10x | 1.02x |
| GPR/FPR replay | 3.42x | 3.63x | 3.67x |
| Seeded renumber search | 4.34x | 4.59x | 4.55x |
| Checked compilation | 0.66x | 0.73x | 0.75x |
| Compile + full report (resident worker) | 0.70x | 0.74x | 0.79x |

Captured Echoes unit:

| Phase | Python ms | Rust ms | Python/Rust | Python tree RSS MiB | Rust tree RSS MiB |
|---|---:|---:|---:|---:|---:|
| Allocation/frame parse | 270.38 | 115.91 | 2.33x | 74.6 | 82.8 |
| Allocation analysis | 284.32 | 205.75 | 1.38x | 107.9 | 211.6 |
| GPR/FPR replay | 18.31 | 8.39 | 2.18x | 77.3 | 93.8 |
| Seeded renumber search | 409.76 | 172.10 | 2.38x | 75.1 | 82.8 |
| Checked compilation | 9892.74 | 9693.00 | 1.02x | 99.1 | 60.9 |
| Compile + full report (resident worker) | 11305.25 | 10234.00 | 1.10x | 237.5 | 360.7 |
| Fresh CLI + JSON output | 11363.34 | 10380.67 | 1.09x | 256.1 | 372.3 |

The synthetic GC 2.7 fresh CLI median is 66.29 ms for Python and 50.16 ms for Rust (1.32x).

## Interpretation

The component gains do not imply the same gain for a complete compiler-backed command. Check the checked-compilation and fresh-CLI rows separately. Allocation-analysis RSS is also a separate tradeoff: the Rust worker uses more memory on this captured unit even where its timed allocation analysis is faster.

The measured scope does not include source-variant search, reductions or the deferred provenance extensions. No faster allocator-search algorithm was introduced; both implementations execute the same seeded search contract.

## Retained evidence

- Windows core: `build\rust-migration\benchmark-run-6vm7834e`; results SHA-256 `09c04c3b48edafb940508a26b48306f2d66239a99163f910fe1fbc194a3c2646`. `audit.json` verifies inputs, results, statistics and compiler identities.
- Linux core: `/home/henrique/.cache/mwcc-rust-benchmarks/benchmark-run-lovjoef_`; results SHA-256 `3f059d9d894265ad11702384f53f1f20c9af6a4057fc5ff8b3837d90eb06eede`. `audit.json` verifies inputs, results, statistics and compiler identities.
- Windows CLI: `build\rust-migration\benchmark-cli-c9mzai7w`; results SHA-256 `b58afd0b4d716bce3f05e34d7339b7bd87563a25be6ad6dc94181e3e128da169`. `audit.json` verifies inputs, results, statistics and compiler identities.
- Linux CLI: `/home/henrique/.cache/mwcc-rust-benchmarks/benchmark-cli-mvp_ek77`; results SHA-256 `0b5dc2108f1cd1f8e68798176e06e44ef46a289ae9091d7d6905f5d818e16887`. `audit.json` verifies inputs, results, statistics and compiler identities.

Reproduce with `tools/migration/benchmark.py`, `benchmark_cli.py`, `audit_benchmark.py` and this report generator. Input relocation changes host paths and compiler-root selection only; trace bytes and search problems are checked unchanged.
