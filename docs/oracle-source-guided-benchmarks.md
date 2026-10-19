# Source-guided tooling: bounded throughput measurements

Measured 2026-10-16 with the sealed V4 producers and a frozen final-gated native
CLI. These measurements identify costs of those frozen tools on this machine; they are not a
performance guarantee for other inputs, machines, or later producer revisions.
The original frozen full-function inspection took roughly **25–35 seconds per
invocation**. The paired parser experiment below reduced Windows inspection to
about 10.3 seconds on the same retained input. New V5 state-fingerprint and
source-search costs were not benchmarked.

The retained proof root is
[`build/source-guided/throughput-audit`](../build/source-guided/throughput-audit).
Its [`summary.json`](../build/source-guided/throughput-audit/summary.json) contains
the complete Windows/Linux measurements, hashes, statistics and limitations.
These local evidence files are ignored build artifacts. The scripts, configs,
commands, captures, objects and search state must be retained together to reproduce
the run; the table alone is not a portable benchmark fixture.

## Method and scope

Each compilation case used GC 1.3, GC 1.3.2 and GC 2.7, an excluded warm-up pair,
then three measured backend-off/on pairs. Pair order alternated. Every invocation
performed the normal stock-object check. All 48 invocations per host passed, and
objects were identical across backend modes, repetitions and hosts for each
source/version pair. Source and private-header hashes were checked. No producer
was rebuilt or reconfigured, and no game checkout was written or built.

The two private compile inputs were:

- The exact [`promoted_flags.cpp`](../tests/fixtures/source-guided/promoted_flags.cpp)
  standalone fixture, with both promoted-element controls.
- A 2,672-byte CElementGen translation unit containing the retained include prefix
  and exact `SetGlobalScale` / `SetLocalScale` bodies. It used 127 restored private
  headers and the retained game flags with include roots rewritten to that private
  project. The selected trace was `SetGlobalScale`. This is a moderate extracted
  translation unit, **not a full CElementGen or RenderParticles compile**.

Backend-off still enabled ordinary allocator, stage, local and CSE tracing on the
instrumented producer. The ratio measures the incremental cost of backend
provenance capture. It does not measure all-tracing-off performance or the overhead
of a patched producer relative to pristine stock.

Timers cover the complete native `oracle compile-trace` command: startup,
instrumented compiler, stock verification, host report construction, JSON
serialization and file output. Python result validation is outside the timer.
These are wall times, not isolated compiler CPU times. Raw commands and working
directories are stored beside each capture as `*.command.json`.

Windows used NTFS and Linux used native WSL ext4 copies on the same 24-logical-CPU
machine. **Windows results are exploratory:** other agents reported Cargo/build
work that may overlap those samples. Linux measurements ran serially during a
coordinated pause in heavy builds. Neither run used OS process isolation, CPU
affinity, cache flushing or exclusive machine ownership. Small sample counts and
the reported variation limit comparisons between versions and platforms.

## Incremental backend trace cost

Values are mean seconds ± sample standard deviation; each row has three measured
pairs after warm-up. Ratio is the mean of the three paired on/off ratios, with its
sample standard deviation, rather than a ratio of aggregate means.

| Host | Source | Version | Backend off (s) | Backend on (s) | Paired on/off |
|---|---|---|---:|---:|---:|
| Windows | Standalone | 1.3 | 0.508 ± 0.012 | 0.629 ± 0.059 | 1.237 ± 0.098 |
| Windows | Standalone | 1.3.2 | 0.524 ± 0.017 | 0.619 ± 0.013 | 1.182 ± 0.017 |
| Windows | Standalone | 2.7 | 0.557 ± 0.047 | 0.639 ± 0.021 | 1.149 ± 0.059 |
| Windows | Scale methods | 1.3 | 1.473 ± 0.011 | 1.773 ± 0.016 | 1.203 ± 0.008 |
| Windows | Scale methods | 1.3.2 | 1.409 ± 0.008 | 1.753 ± 0.052 | 1.244 ± 0.033 |
| Windows | Scale methods | 2.7 | 1.909 ± 0.358 | 2.297 ± 0.255 | 1.217 ± 0.108 |
| Linux | Standalone | 1.3 | 0.382 ± 0.029 | 0.449 ± 0.029 | 1.177 ± 0.084 |
| Linux | Standalone | 1.3.2 | 0.415 ± 0.000023 | 0.466 ± 0.000279 | 1.121 ± 0.001 |
| Linux | Standalone | 2.7 | 0.415 ± 0.000204 | 0.482 ± 0.029 | 1.161 ± 0.069 |
| Linux | Scale methods | 1.3 | 1.134 ± 0.077 | 1.385 ± 0.077 | 1.227 ± 0.133 |
| Linux | Scale methods | 1.3.2 | 1.134 ± 0.029 | 1.368 ± 0.000136 | 1.207 ± 0.031 |
| Linux | Scale methods | 2.7 | 1.151 ± 0.029 | 1.318 ± 0.000304 | 1.146 ± 0.029 |

The observed mean paired ratios span 1.15–1.24 on Windows and 1.12–1.23 on Linux
for these two workloads. A full renderer backend-on/off compile comparison remains
unmeasured.

## Retained analysis cost

These separate native CLI measurements read already captured inputs. They include
read/parse, provenance or objective analysis, serialization and output, with no
compiler invocation or dependency rescan. Each mode had one excluded warm-up.
The small case has three measured samples; the full case has two.

| Host | Capture | Provenance inspection (s) | Objective evaluation (s) |
|---|---|---:|---:|
| Windows | Standalone | 0.164 ± 0.017 | 0.220 ± 0.013 |
| Windows | Full RenderParticles | 28.803 ± 3.420 | 27.980 ± 2.737 |
| Linux | Standalone | 0.148 ± 0.029 | 0.165 ± 0.000133 |
| Linux | Full RenderParticles | 25.496 ± 2.840 | 35.172 ± 3.910 |

The full input is the exact 52,064,775-byte V4 JSON capture from
[`promotion-audit/real-store-after-v4`](../build/source-guided/promotion-audit/real-store-after-v4),
selecting `RenderParticles__11CElementGenFv`, with its exact source, object and
ghost contract. Those four input hashes agree across hosts. The small contract
is satisfied by the observed second-element copy rejection above the coalescing
window. The full ghost contract correctly remains **indeterminate**, because its
selected promoted value has no actual incoming copy. Thus evaluation timings also
cover a relevant negative-evidence path; they do not imply a successful source
transformation or semantic equivalence.

The large difference between retained small and full analysis motivates profiling
JSON/trace parsing and repeated provenance/evaluation work. These timings alone do
not attribute the cost to a particular parser or algorithm.

## Actual native search throughput

The native GC 2.7 search used a private single-function promoted-flag fixture and
four closed syntax recipes, including an exact parenthesization alias. Every run
produced five candidate outcomes, five stock-checked compilation requests, one
exact-input cache hit and one relevant compiler-state group. All outcomes were
unsatisfied for the requested second-element successful coalescing; there were no
fingerprint errors. Each host had one excluded warm run and three measured fresh
search directories.

| Host | Five-candidate search (s) | Mean candidates/s |
|---|---:|---:|
| Windows | 10.265 ± 0.606 | 0.488 |
| Linux | 16.667 ± 0.790 | 0.300 |

This measures the complete current negative search path, including exact-input
dependency/preprocessing scans, compilation and stock checking, trace analysis,
fingerprinting, cache/group handling, checkpointing and serialization. The cache
hit and grouped syntax variants are deliberate parts of this workload. There was
no semantic checker, target match or successful-winner independent verification;
their costs are not included. The search is explicitly a mechanism reproducer and
does not establish gameplay equivalence.

## Reproduction receipts

- [`prepare.py`](../build/source-guided/throughput-audit/prepare.py),
  [`benchmark.py`](../build/source-guided/throughput-audit/benchmark.py),
  [`prepare_linux.py`](../build/source-guided/throughput-audit/prepare_linux.py)
  and [`config-windows.json`](../build/source-guided/throughput-audit/config-windows.json)
  retain extraction, ordered flags and execution logic.
- [`windows/verification.json`](../build/source-guided/throughput-audit/windows/verification.json)
  and [`linux-verification.json`](../build/source-guided/throughput-audit/linux-verification.json)
  retain samples, means, medians, sample standard deviations, coefficients of
  variation, ranges, identities and selected environment details. The complete
  environment is recorded only as a hash.
- Windows commands used sealed producer
  `birth-origin-audit/windows-producer/rust-producer-57ijuxk6`; Linux commands used
  `<linux-cache>/mwcc-birth-origin-audit/producer/rust-producer-gmo66ilq`.
  Linux raw outputs and config remain under
  `<linux-cache>/mwcc-throughput-audit/`.
- Frozen CLI SHA-256: Windows
  `3c3d223e961775f1f5b3d29d722045f8851922e41499e5c0b122d9eecd7d13ce`;
  Linux
  `e3e0a0f6d8d35886f7bae5fdc969dc2d6017b13e9d11f6f5165033b6746de013`.
  Both sealed producers share hooks SHA-256
  `539d259896c03824763ae47b9aca3d7e5ef7092378cbe8cbe2679e9df32b8f3b`.

The complete runs took 267.46 seconds on Windows and 287.80 seconds on Linux,
serially. A later optimization should rerun the same retained full inputs and
negative search before claiming an improvement, and add a successful search case
when measuring independent verification overhead.

## Retained parser follow-up

The initial measurements motivated an early record classification change:
coalescing, value-history and graph-history parsers now avoid allocating field
maps for unrelated `pv.*` record families. A frozen current CLI was compared with
the frozen baseline above using the **same retained V4 inputs**, without invoking
a compiler or producer. This is a before/after CLI measurement, not an isolated
microbenchmark of one allocation or a new source-search throughput measurement.

The local proof is
[`parser-throughput-audit/summary.json`](../build/source-guided/parser-throughput-audit/summary.json),
with executable
[`compare.py`](../build/source-guided/parser-throughput-audit/compare.py),
[`config-windows.json`](../build/source-guided/parser-throughput-audit/config-windows.json)
and every raw command/output. The new Windows CLI SHA-256 is
`4a4bbf02675f35ade4556cbcc547404429122766dbd71233d2bdd2a2a042940c`.
It also contains additive scalarization support. For parity, comparison removed
only top-level `Report.scalarization` and each
`history.functions[].function_object` / `.scalarization` field. Every other JSON
field, including trace digests, positions, graphs, births, coalescing decisions and
evidence gaps, had to compare exactly. This establishes parity of existing report
fields; it does not validate the removed feature fields.

All **11 parity cases passed**: full ghost and full second-element renderer
captures, the untouched small capture, unknown coalescer/graph schemas, an explicit
gap and unrelated record family inserted inside a graph, a truncated coalescer,
and malformed coalescing/history/JSON inputs. The mutations are synthetic parser
probes derived from the retained small trace, not new compiler observations.
Unknown schemas, gaps and truncation retained the same indeterminate evidence;
the three malformed cases returned identical exit statuses and stdout/stderr,
including their error text.

Only the 52 MB full ghost capture was timed: one excluded warm-up pair, then three
measured old/current pairs with alternating order, using the same selected
`RenderParticles` link and end-to-end inspection scope as above.

| Windows full retained inspection | Mean seconds ± sample SD | Range (s) |
|---|---:|---:|
| Frozen baseline | 22.851 ± 3.235 | 19.115–24.761 |
| Frozen current parser | 10.270 ± 1.279 | 8.804–11.158 |

The three paired current/baseline ratios were 0.440, 0.461 and 0.451: mean
**0.450 ± 0.010** sample SD, or about 55% lower elapsed time for this retained
inspection workload. Absolute times still varied (baseline CV 14.2%, current CV
12.5%). The baseline mean in this paired run also differs from the earlier
exploratory Windows measurement; use the contemporaneous pairs for this
comparison, rather than comparing independent table means.

Root's final Cargo work overlapped only the excluded warm-up. Measured pairs ran
during a coordinated heavy-build/capture pause; secondary full-capture parity ran
after that pause and its times are not reported as a benchmark. There was still no
OS isolation, CPU pinning or exclusive machine guarantee. Linux before/after
timings, V5 trace timings, objective evaluation and native search throughput were
not remeasured. Retained full inspection still costs roughly ten seconds here, so
this improvement does not remove the need to profile remaining analysis costs.
