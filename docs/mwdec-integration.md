# Integrating demwcc-echoes (mwdec) with mwcc-tools

Investigation, 2026-10-19. mwdec: `<demwcc-echoes>` (92 k lines, 11 crates). mwcc-tools: this checkout.

## Where things stand

mwdec already depends on one crate here, `mwcc-wasm` (a path dependency on `../mwcc`). Only
`mwdec-oracle/src/wasm.rs` calls it, and it uses three modules:

- `mwcceppc.wasm`: every normal compile and every PCH;
- `persist.wasm`: the search's fast path;
- `oracle.wasm`: the tracer.

Each path still has a native fallback beside it: processes with a taskkill watchdog, a debugger-driven persistent
compiler (about 800 lines) and a debugger tracer (about 780 lines).

The wasm and native backends overlap in places. Each one has its own:

- module loader and cache: `in_process.rs` with `<module>.cache/`, against mwdec's `wasm.rs` with
  `$MWDEC_WORK_BASE/wasm-cache/`;
- VS_VERSIONINFO source: the oracle builds it from gc2's `.rc`, mwdec parses it out of the exe;
- ninja parser;
- `ra.*`/`sc.*`/`inl.*` trace parser;
- colouring model: `colouring.rs` against `regalloc.rs`;
- off switch: `MWCC_IN_PROCESS`, `MWDEC_WASM`.

mwdec has several things mwcc-tools lacks:

- a strict PPC object comparator;
- parsers for `cp.*`, `ph.*`, `st.*` and `pc.*`;
- a persistent-compile pool with routing and failure trust;
- a source-mutation search.

## Measurements (Echoes, GC 2.7, this machine)

| What | Number |
|---|---|
| Plain-context compile, CPlayerState / CStateManager | wasm 272 / 783 ms; exe 450 / 1200 ms |
| PCH compile, same units | wasm 16 / 29 ms; exe 26 / 38 ms |
| Fast path (`persist.wasm`) during `match` | 4.4-7.1 ms avg per compile; start 490-680 ms warm, 1.4-1.7 s with no module cache |
| `match` search from a perturbed start, 60 s, 6 workers | 509 evals/s: 30,566 compiles, 1,082 compile errors |
| Same run, failures | 1,082 recompiled normally, 0 of them accepted |
| `match`, draft exact, warm caches | setup 4-5 s; search 0.1 s; total about 7 s |
| Added setup with no TypeDb cache (`mwdec-ctx`) | +6.6 s |
| Added setup with no inline cache (`mwdec-inline`) | +5.5 s |
| Added setup with no PCH or work directory | +1.3 s |
| Added setup with no `.cwasm` module cache | +6.4 s (Cranelift, three modules) |
| First run on a fresh work base, OS file cache cold | 69 s |

The documented figure for persist is 1.9 ms per candidate on CStateManager + CActor (wasm.md), single threaded. Under
6 workers, mwdec's fast path averages 4.4-7.1 ms, so restore and zeroing (5-15 MB snapshots) under concurrent
memory traffic may cost more than the compile itself. mwdec drops `Candidate`'s restore, zeroed and compile timings,
so this is not yet visible.

## Recommendations, ranked by gain

### 1. Persistent compiles inside mwcc-tools' source search, from mwdec's fast path (gain: mwcc-tools)

Today `source_guided` (`source-search`, `source-reduce`, `header-impact`, `inline-calibrate`) makes one full compile
per candidate through `runner::compile_controlled`, at roughly 234 ms on a CStateManager context. A persistent
compile of the same candidate takes about 2 ms, which is 50-100x faster. Every candidate in these tools is a
context followed by a varying tail, which is exactly what persist serves.

- **Port:** mwdec's `mwdec-mwcc/src/fast.rs` logic into a `mwcc-wasm` (or oracle-core) `PersistPool`:
  - per-context routing;
  - LRU snapshots per worker;
  - start after N requests;
  - trust of failures after confirmed agreement, with periodic re-checks;
  - poisoning when a normal compile disagrees.
- **Leave out:** the thread-affinity layer, which exists only because the Win32 debugger ties a process to its
  thread. `mwcc_wasm::Persistent` already owns its thread.
- **Afterwards:** mwdec uses the shared pool and deletes its own copy, about 540 lines of `fast.rs` plus 110 lines of
  `persist.rs`.

### 2. A persist-built oracle module (gain: both, mostly mwdec's search)

`trace_source` re-parses the whole context on every call, with no PCH and no snapshot. That covers:

- mwdec-search `trace.rs`;
- mwdec-oracle `explain`, `advice` and the `ra-*` validators;
- regfix level 1;
- `oracle run-unit` candidates here.

`wasm_build.rs` links persist exports only into `persist.wasm`. Linking them into the oracle module as well
(`oracle-persist.wasm`) would cut each trace from a full context parse (hundreds of ms) to a few ms. Two details
need care:

- the trace file: it must be opened only after the checkpoint, because persist requires no open files at snapshot
  time;
- the line offset of the slot.

### 3. Persist with `-g` / `-sym on` (gain: mwdec's setup and locate)

mwdec keeps `-g` and `-sym` compiles off the fast path (`fast_spec`) because persist shifts line numbers by the two
lines before the slot.

- **Fix:** reset the line counter at the marker (a `#line` directive in `persistent_source`, or in the C side of
  persist).
- **What it unlocks:**
  - TypeDb builds: up to 8 `-g` compile rounds of one context with different forcing tails, part of the +6.6 s;
  - inline probes: part of the +5.5 s;
  - locate's `-sym on` compiles during the search.

### 4. Precompiled `.cwasm` next to the modules (gain: both, 6.4 s per cold start)

Today the module cache has two schemes. Its artifact eviction also deletes every other `.cwasm` in the directory.
The fix has two parts:

- Have `configure`/ninja emit the `<module>.cache/<hash>.cwasm` artifacts as build outputs.
- Have mwdec look there first, before its own `wasm-cache` (or instead of it).

A fresh mwdec work base or CI runner then pays about 3 ms per module, not about 2 s.

### 5. Keep `Candidate.console` in mwdec's fast path (gain: mwdec, about 9% of search worker time)

mwdec's `FastStats` drops the messages, so every fast compile error becomes `UNCONFIRMED_FAILURE` and gets a normal
recompile, roughly 30 ms on a PCH. In the 60 s run that was 1,082 recompiles and about 32 s of the 360 worker-seconds,
and every one of them agreed.

- Return the console from the fast path.
- Trust a fast error whose messages are a real diagnostic, rather than a trap or timeout.
- Since wasm persist is verified identical to normal compiles, also sample exact confirmations instead of confirming
  every one.

### 6. The strict comparator and the missing trace parsers move here (gain: mwcc-tools' correctness)

mwcc-tools' `elf::differences` returns strings per section and symbol. mwdec's `mwdec-mwcc/src/compare.rs` (771 lines)
plus `placeholder.rs` defines a match properly:

- masked words, with relocations matched per word;
- generated names (`@N`, `@stringBase0`, `lbl_`, `fn_`) compared by referenced bytes, with a one-to-one mapping;
- `ExternIndex` for literals in other splits;
- a `DiffClass` and a score.

It belongs in a shared crate, for example `mwcc-objcmp` with mwdec-obj's ELF loader, which depends only on `object`.
mwcc-tools' checked compiles and the source-search `objective` would use it.

`oracle_trace.rs`'s parsers for `cp.*`, `ph.*`, `st.*` and `pc.*` go into `mwcc-oracle-core::trace` next to the
existing `ra/sc/inl` parsing. mwdec then reads one parser.

### 7. A lean crate split before mwdec takes more from oracle-core (enabler)

Depending on `mwcc-oracle-core` as it stands is not free:

- **Always-on dependencies:** capstone (a git dependency with a C build), ureq with rustls, and zip, plus exact
  pins (`ureq =3.2.0`, `zip =6.0.0`, `wasmtime =49.0.3`).
- **serde_json features:** oracle-core enables `preserve_order` and `float_roundtrip`. Cargo feature unification
  would switch these on for all of mwdec, changing `Value` key order. That risks the determinism of `draft_hash` and
  the merge gate.

**Proposal:** a `mwcc-trace` crate (or default-off features in oracle-core) holding:

- `trace`, `version`, `elf`, `ppc` and `ninja`;
- `colouring`, `allocation`, `schedule`, `inlining` and `locals`;

with serde_json at its default features. mwdec then drops its `ra/sc/inl` parsing, and later its colouring
duplicate in `regalloc.rs`, wherever the two agree.

### 8. Smaller mwcc-wasm items

- **Pooling allocator and CoW memory init for `run`:** every normal compile instantiates afresh and starts its own
  64 MB-stack thread. A pooling allocator plus copy-on-write memory init, and a reused worker thread, would trim the
  per-compile overhead. Measure it on the 16 ms PCH compiles, where overhead is proportionally largest.
- **Cancellation:** mwdec never passes `Request.cancel`. Wiring it to the search budget would stop in-flight
  candidates at the deadline.

## mwdec cleanups once wasm is required for GC 2.6/2.7

About 1,600 lines can go:

- the debugger tracer (`tracer.rs`, about 780 lines; `trace-parity` already shows the two tracers agree);
- the native `PersistentCompiler` (`persist.rs:1-806`);
- the process path with its taskkill watchdog and resource-status retries;
- `compile.rs`'s exe fallback.

Two pieces stay:

- **`split.rs` and `Scorer.pch_broken` stay.** The PCH crash still happens under wasm: today's wasm-only work
  directory holds a real repair (`pch/cc8d…ffd.split` = `0,13`). The crash is in the compiler, so finding its cause
  in gc2's sources (the oracle can trace it) is a better fix than the delta-debugging.
- **The memory cap stays, but needs work:** wasm guest memory now counts against mwdec's own 3 GB job, so a runaway compile fails the main
  process, not a child. A per-store `ResourceLimiter` in mwcc-wasm would turn that into a compile error.

## Should mwdec live inside mwcc-tools?

No: integrate at the crate boundary. mwdec is Echoes-specific:

- Echoes header paths and flag profiles from Echoes' build.ninja;
- GC 2.7 addresses in its native paths;
- train/test splits over Echoes' report.json.

It is also three times the size of everything here. mwcc-tools is multi-version and project-agnostic. The shared
pieces (pool, comparator, trace parsers, lean crate) should move here as libraries, and mwdec should keep its
drafter and searcher.
