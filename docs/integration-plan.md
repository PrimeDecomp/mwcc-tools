# Plan: shared compiler engine for mwcc-tools, mwdec and the Echoes kit

Status: proposed, for review. Once approved, this is the implementation goal.

## Goals

1. Integrate demwcc-echoes (mwdec) with mwcc-tools, for the best gains in functionality and performance. The
   analysis is in [mwdec-integration.md](mwdec-integration.md).
2. Move the Echoes kit's source-experiment helpers into mwcc-tools:
   - **The helpers:** `try-variants.py`, `decomp_experiment.run_variants`, `decomp_matrix.py`, `inline-sweep.py`,
     `residual-scan.py` and `sinit-profile.py`.
   - **The design:** candidates compile in process through a file overlay. The helpers become a Python package,
     `mwcc_tools`, which is the full experiment interface. Its `run_variants` and `run_matrix` keep the documented
     calls.
   - **The kit:** `echoes-agent-workflow-mcp-2026-10-18-r2.zip`, authoring tree
     `<decomp>\echoes-kit-20261018\echoes-agent-workflow\`.
   - **Not moved:** the Echoes-only helpers stay in the kit (`sldr-ctor-check.py`, `string-array-scan.py`). The
     GC/1.3.2 debugger probes (`mwcc132/`) are covered by the oracle and `colouring`.
3. Score with objdiff's Rust API (objdiff-core).
4. Spend most experiment time on compiling and comparing, not on overhead, so agents can run many more experiments.
5. Keep every documented use of these helpers in the Echoes MCP docs true.
6. Do each job once: no job is implemented twice across mwcc-tools and mwdec.
7. Keep the code small: one engine reached through one protocol. Python is the full API; the CLI is a thin generic
   client plus the few commands that people and builds need.

A built mwcc-tools is required for the Echoes kit.

## Repositories

| Repository | Work | Commits |
|---|---|---|
| mwcc (this checkout, branch `oracle`) | W1-W7 | Local commits on a new branch. Pushing needs your OK. |
| demwcc-echoes (`<demwcc-echoes>`) | W8 | Local commits on a branch. It is someone else's repository, so no push without your OK. |
| Echoes kit authoring tree | W9 | Files edited in place. A new archive is built only when you ask for a release. |
| `<decomp-kits>\KIT-IMPROVEMENTS.md` | W9 | Edited, per the global rule. |
| Echoes MCP docs (collab.decomp.dev) | W10 | Shared pages: each edit after your OK. |

---

## 1. Where the time goes

Measured on this machine against Echoes G2ME01.

| Experiment step | Productive work | Measured total | Overhead |
|---|---|---|---|
| Oracle compile of CElementGen with the default streams (as run by `colouring unit`, `oracle run-unit`, every source-search candidate, regdiff and provenance) | stock compile 0.9 s; with the hooks, 1.7 s with the trace kept in memory | **16.4-16.7 s** with the trace to a file (`run-unit --fast` 17.1 s; `colouring unit … --save` 20.2 s) | **About 15 s: the trace writer.** `oracle.c` flushes after every record. Each flush is a host `WriteFile`, which also calls `listings.forget()`, so every later header lookup lists its directories again. That is 73,412 records (11.6 MB) for this TU. Sending the trace to `NUL` still takes 8.0 s. The file and in-memory traces have the same records; only heap addresses differ (the `FILE` buffer shifts the heap) and the file has CRLF. |
| One what-if replay on a saved trace (an agent's script: one process per edit set, 8 at a time) | the replay: about 0.9 ms (annealing: 5,000 iterations add 4.6 s, 50,000 add 44.5 s) | 1.72-1.79 s per call | About 1.7 s of loading `base.trace` (22 MB, 246,166 records), `base.json` (3 MB) and the target asm, for every edit set. About 0.7 s of it is the asm. C(n,3) triples for n=30 take about 14 minutes; the replays themselves need about 4 s on one thread. |
| `mwdec match`, draft exact, warm caches | search 0.1 s | about 7 s | 4-5 s of setup (TypeDb, inline library, drafting), and about 3 s after the search. Cold: +6.4 s of Cranelift and +12 s of caches. |
| `mwdec match` search, 60 s, 6 workers | 29,483 fast compiles at 4.4-7.1 ms | 509 evals/s | 1,082 normal recompiles of failures whose diagnostics were already known (about 9% of worker time). A further 23,105 duplicate source texts were skipped before compiling. |
| A source candidate through a full compile (`try-variants`, `run_variants`, source-search) | — | 270-890 ms | Mostly re-parsing the same headers. A persistent compile of the same candidate takes 2-7 ms. |

Two conclusions follow:
- The two largest overheads have nothing to do with the compiler. One is how the trace leaves the module; the other
  is that every experiment starts a new process and reloads its inputs.
- The rest is redundant compiling: re-parsed contexts, and repeated candidates.

## 2. Architecture

### Shared crates

```
mwcc-wasm         the compiler module: registry, worker threads, overlay, trace sink, persist, PersistPool, limits
mwcc-trace        TraceOptions, every record parser, colouring, schedule replay, allocation, inlining, locals
mwcc-project      objdiff.json, report.json, build.ninja, Unit (argv, compiler, target, base), compile_unit
mwcc-compare      objects (on `object`), objdiff-core scores, strict exact, residual profile, disassembly
mwcc-experiment   plans: spaces, staged evaluator, strategies, caches, controls, timing, results.jsonl
mwcc-oracle-core  the analyses and tools on top (provenance, source-guided, inline tools, regdiff, …)
mwcc-methods      the method registry: every capability as a typed function (serde params and result)
mwcc-tools        `serve`, the generic `call`/`methods`, the build and decompilation commands
python/mwcc_tools the Python package: a client of `serve`, typed wrappers, reports, run_variants/run_matrix
```

**Dependencies:** none of the first five depends on capstone, ureq or zip, or enables serde_json's
`preserve_order`, so mwdec can depend on them freely.

### Interfaces

| Client | How it reaches the engine | What it sees |
|---|---|---|
| Agents and kit scripts | the `mwcc_tools` Python package, over `mwcc-tools serve` (JSON lines on stdin/stdout) | every method, streamed results, sessions that keep loaded state |
| People, one-off | `mwcc-tools call METHOD [@params.json \| key=value…]` | any method once, printing its JSON; `mwcc-tools methods` lists methods and parameter schemas |
| Builds | `mwcc-tools run` / `mwcc-run`, `configure`, `build-hooks` and module precompilation | hand-written commands, as today |
| The compiler decompilation | `pe`, `split`, `link`, `compare`, `download`, `source-only` and the other binary tools | hand-written commands, as today |
| mwdec | the Rust crates, linked directly | the libraries; no protocol |

**What this replaces:**
- the hand-written option loops and text printers of the oracle, colouring, provenance, source-guided and TU
  commands (most of today's 5,200 lines of `main.rs` and `*_cli.rs`);
- any per-command batch options, because every method takes a list of inputs.

**Versioning:** methods and their schemas carry a `schema_version`, as the trace JSON does. A breaking change bumps
it, and the Python package checks it at connect.

### One implementation per job

| Job | Implemented in | Replaces in mwcc-tools | Replaces in mwdec |
|---|---|---|---|
| Loading and caching the compiler module | `mwcc-wasm::Modules` | `in_process.rs`' map and `<module>.cache/` | `wasm.rs`' map and `wasm-cache/` |
| Version information for the driver | `mwcc-wasm`, from gc2's `mwcceppc.rc` | — | the PE resource parser in `wasm.rs`; the copy in `examples/persist_bench.rs` |
| Recognizing a project's compiler | `module_of_copy` (size and SHA-1 from `config/<ver>/config.json`) | — | the path-suffix rule |
| Switching in-process compiles off | `MWCC_IN_PROCESS=0` | — | `MWDEC_WASM` (kept as an alias) |
| Big-stack threads for runs | `mwcc-wasm`'s worker pool | `in_process::run`'s spawn | `wasm::execute`'s spawn |
| Units, flags, build.ninja, objdiff.json | `mwcc-project` | `ninja.rs`, `project.rs`, `kit_cli::unit` | `mwdec-project/ninja.rs`, `mwdec-oracle/flags.rs` (`Project` wraps `mwcc-project`) |
| Reading ELF objects | `object`, through `mwcc-compare` | `elf.rs` | the reader in `mwdec-oracle/asm.rs` (`mwdec-obj` stays on `object`) |
| PowerPC disassembly | objdiff-core's decoder | `ppc.rs`' helpers | `ppc750cl` |
| Comparing with the target | `mwcc-compare`: `identical`, `score`, `strict`, `profile` | `elf::differences` | `compare.rs`, `placeholder.rs`, `ExternIndex` (moved), `score.rs`' profile |
| Classifying residual rows | `mwcc-compare`'s profile; residual-scan's verdicts are a fixed projection of it | — | `DiffProfile` |
| Trace options | `mwcc-trace::TraceOptions` (the union), passed per run | `runner::TraceOptions` | `oracle_trace.rs`' variable list |
| Parsing trace records | `mwcc-trace` | `trace.rs`, `inlining.rs`, `locals` | `oracle_trace.rs` (ra, sc, inl, cp, ph, st, pc) |
| Inline decision reasons | an `inl.reason` record emitted by the hook | — | `reason()`, rebuilt from flags |
| Colouring replay and prediction | `mwcc-trace::colouring` | — | `regalloc.rs`' replay and predict (`webs.rs` and the inverse solves stay, on `colouring`'s types) |
| Scheduler replay | `mwcc-trace::schedule` | — | `sched.rs` (`schedcheck.rs` stays) |
| Persistent compiles and trust of failures | `mwcc-wasm::PersistPool` | — | `fast.rs`, `persist.rs::Instance`, `confirm_failures` |
| Compile, score and trace caches | `mwcc-experiment` | — | `MemCache` and the disk cache (the `.mch` precompile stays) |
| Time accounting | `mwcc-experiment` timing | `MWCC_RUN_TIMING` (kept as its view) | `mwdec_core::prof` (spans report into it) |
| Evaluating candidates | `mwcc-experiment`'s staged evaluator | the loops in `source_guided`, `colouring search`, `inline-sweep` | the permuter's and `regfix`'s evaluation (their operators and strategies stay) |
| Command-line parsing and output | `mwcc-methods` (typed params), generic `call` | the per-command option loops and printers | — |
| Running source experiments from Python | `mwcc_tools` (package) | — | — (in the kit: `decomp_experiment`'s tree-edit loop with its restore and locking code, the four scripts' implementations, and `_mwcc_native`'s executable lookup) |
| Per-compile memory limit | `mwcc-wasm`'s `ResourceLimiter` | — | — (`memcap` stays as the process-wide cap) |
| Benchmarks | one opt-in bench in mwcc-tools on the timing fields | `examples/persist_bench.rs` | `persist-bench`, `bench-compile` |

**What mwdec keeps:** the drafter (lift, emit, inline folding, types from DWARF), its search operators and
strategies, `webs`/`hints`/`schedcheck`, and its CLI.

---

## 3. Work

### W1. `mwcc-wasm`: the compiler module

**W1.1 One module.** `build/<ver>/wasm/mwcc.wasm` is built with persist (`CPrep.c` and `CParser.c` with
`-DMWCC_PERSIST`, plus the exported stack pointer) and with the oracle hooks linked in.
- **Tracing** is chosen per run with `Request.trace: Option<TraceOptions>`. A host import answers
  `OracleTrace_Enabled`, so with tracing off each hooked call costs one branch.
- **Persistence:** a source without `MWCC_PERSIST_MARKER` compiles normally. With the marker, it snapshots.
- **Snapshots and tracing:**
  - A snapshot taken with tracing off has no frontend records (provenance, source arenas, frontend nodes) for its
    context. Plans that need those snapshot with tracing on.
  - Backend streams (allocation, scheduling, inlining decisions, locals, CSE) can be switched on after a restore.
  - So one snapshot can compile many candidates untraced and then trace the best few.
- **Acceptance:**
  - With tracing off, the module gives byte-identical objects to the original exe over `mwcc-tools corpus
    --canaries` and a full Echoes build through `mwcc-run`, and runs within 2% of a build without hooks.
  - With tracing on, every oracle test gives the same records.
  - Persistence gives identical objects in the persist tests and in mwdec's `PERSIST_VERIFY` run.
  - The separate stock, oracle and persist builds stay available as the reference to compare against. The x86
    oracle executables for GC/1.3.x are unaffected.

**W1.2 Trace sink.**
- **Where records go:** `oracle.c` calls a host import `oracle_trace(ptr, len)` instead of `vfprintf`/`fflush` to a
  `FILE*`. The host appends to a buffer, and `Outcome` and the persist `Candidate` return it as `trace`.
  - Every record goes through `OracleTrace_Printf` (`wasm_oracle.rs`). Any hook that writes through
    `trace_output.rs`' own `fopen` path is routed to the sink as well.
- **The trace file:** `MWCC_ORACLE_TRACE_FILE` is still honoured, written once at exit or trap. A trap returns what
  was traced up to it.
- **On restore:** the buffer is cleared at each persist restore, and no file is open at a snapshot.
- **Callers:** `runner::execute_controlled` reads `Outcome.trace`, with no temporary file.

**W1.3 Directory listings.** `WriteFile` forgets the cached listings only when a write changes what a listing
answers, that is, whether a file exists. For an already-open file, the listed size is updated in place.

**W1.4 Function filter.** `TraceOptions.functions` makes the hooks record only while those functions are compiled,
with the frontend records needed for provenance scoped to them. The tools pass `--function` down and enable only the
streams a command uses. The size saved on CElementGen is measured.

**Acceptance for W1.2-W1.4:**
- An oracle compile of CElementGen with one function's streams takes at most twice as long as the stock compile.
- `colouring unit … verify` takes under 3 s (today 20.2 s).
- Every trace test gives the same records.

**W1.5 Registry and worker threads.**
- **`Modules::get(root, version)`** loads the module once per process and reloads it when the file changes. It is
  identified by `module_of_copy`, and its version information comes from gc2's resource script.
- **Compiled artifacts:** `mwcc-tools configure` emits a ninja edge that precompiles the artifact
  (`build/<ver>/wasm/mwcc.cache/<hash>.cwasm`), so no process pays for Cranelift. Writing an artifact evicts only
  older artifacts of the same module.
- **Threads:** runs execute on a pool of 64 MiB-stack worker threads owned by `mwcc-wasm`.
- **Acceptance:** a fresh process loads the module in under 50 ms when the artifact is built (today about 2 s cold).

**W1.6 File overlay.** `Request.overlay` (`mwcc_wasm::Overlay`) maps a host path to replacement bytes, or to
"absent". It applies to normal and persistent compiles.
- **Keys:** absolute, with `.` and `..` resolved lexically, compared ignoring ASCII case (as Windows compares them).
- **Reading:** `CreateFileA` on an overlaid path opens an in-memory handle. `ReadFile`, `SetFilePointer`,
  `GetFileSize`, `GetFileTime` and `CloseHandle` serve it (the module imports no other file API).
  - Its write time is the write time of the disk file it shadows; a file only the overlay has gets a fixed time.
- **Writing:** opening an overlaid path for writing, or deleting it, fails with `ERROR_ACCESS_DENIED`.
- **Queries:** `GetFileAttributesA`, `GetShortPathNameA` and `FindFirstFileA`/`FindNextFileA` report overlaid files
  with their overlay size, report absent entries as not found, and list overlay-only files in their directory.
- **Shift JIS:** `Request.transcode_sjis` does what sjiswrap does:
  - a source or header (sjiswrap's extensions, matched case-sensitively) opened with exactly `GENERIC_READ`, from the
    overlay or the disk, is read as `encoding_rs::SHIFT_JIS.encode` of its UTF-8, once per path;
  - a file that is not UTF-8 ends the run with sjiswrap's error, and an unmappable character gives its warning;
  - `IsDBCSLeadByte` answers true for any byte with its high bit set, so a second byte of 0x5C (表, ソ) is not an
    escape under `-multibyte`;
  - a persistent compile writes each candidate into its slot transcoded, so that it equals a normal compile of the
    filled slot;
  - it is set for `*_sjis` edges (843 in Echoes) and by `mwcc-tools run` when it is given `sjiswrap.exe`.
- **Tests** (`mwcc-wasm/tests/overlay.rs`):
  - an overlaid header, spelled in another case and with `..`, shadows the disk file, and its object equals one with
    that header on disk;
  - an overlay-only source and header are read;
  - an absent header gives the compiler's "not found";
  - writes to an overlaid path are refused;
  - a persistent compile reads the overlay, and its candidate equals a normal compile of the filled slot;
  - a persistent candidate with 表 equals a normal transcoded compile of the filled slot, and one that is not UTF-8
    fails;
  - a UTF-8 source with 表 and kana compiles to the Shift JIS bytes, from disk and from the overlay, and a source that
    is not UTF-8 fails;
  - with `MWCC_TEST_ECHOES`, that object equals the one sjiswrap and the executable give.

**W1.7 Persist line numbers.** `persistent_source` starts the slot with `#line N "<file>"`, so line numbers after
the marker equal a normal compile's. If GC 2.6 or 2.7 does not honour `#line` there, the line counter is reset at
the marker on the C side of persist.
- **Test:** 20 candidates with `-sym on` and with `-g` give the same objects persistently as normally.

**W1.8 `PersistPool`** (`mwcc-wasm/src/pool.rs`, from mwdec's `fast.rs`, CC0).
- **Behaviour:**
  - routing by context key;
  - LRU snapshots per worker, with a start after N requests;
  - trusting a fast failure whose console holds a compiler diagnostic (not a trap, timeout or empty console);
  - re-checking every 8th trusted failure with a normal compile;
  - poisoning a worker when a normal compile disagrees.
- **Per-call confirmation:** `compile(key, context, candidate, timeout, confirm_failures)` returns
  `{object, console, fast}`. Callers that must never act on an unconfirmed failure pass `confirm_failures = true`.
- **Acceptance:** source-search candidates on a CStateManager context take under 10 ms each (today about 234 ms),
  with identical objects in its tests.

**W1.9 Limits and instances.**
- **Memory:** a `ResourceLimiter` per store (default 1 GiB, `MWCC_WASM_MEMORY_MB`) makes a runaway compile an
  error.
- **Instances:** pooling allocation with copy-on-write memory initialization for normal runs, kept only if it saves
  at least 10% on 16 ms PCH compiles.

### W2. `mwcc-trace`

- **Contents:** `trace`, `version`, `colouring`, `allocation`, `schedule`, `inlining` and `locals`, moved out of
  mwcc-oracle-core, which re-exports them so its callers are unchanged.
- **`TraceOptions`:** the union of both projects' streams. Today mwcc-tools has backend, source arenas, source
  nodes, schedule, inline cost, full cost, instantiation, locals, blocks, CSE, stages and IRO dump; mwdec has names,
  copy propagation, peephole and IRO phases. It also carries the function filter (W1.4).
- **Parsers** for every record family, including `cp.*`, `ph.*`, `st.*` and `pc.*` from mwdec's `oracle_trace.rs`.
- **`inl.reason`:** a hook record naming the branch of the inline check that decided.
- **Schedule:** mwdec's pick-rule replay from `sched.rs`, next to `schedule.rs`.
- **Colouring:** a "neighbour degree" edit (every non-coalesced neighbour of a node gets +1 degree), which models a
  longer live range of that node. It is used by `colouring replay`, `colouring search --kinds neighbours` and plans.
- **Dependencies:** regex, serde, serde_json at its default features, and indexmap.
- **Tests:** the existing trace and colouring tests, plus mwdec's record samples as fixtures.

### W3. `mwcc-project`

- **Contents:** `ninja` and `paths` (oracle-core's `ninja.rs` and `project.rs`, which it re-exports as `ninja` and
  `project`), `compilers` (the module that runs a project's compiler, `module_of_copy`, and the wibo launcher, which
  `in_process` and `runner` re-export) and `unit`. It depends on mwcc-wasm and mwcc-trace.
- **`Project`:** objdiff.json and build.ninja's compile edges, read once; `unit(name)`, `unit_names()`, and
  report.json (the `report` edge's output) with `report_unit(name)`. `kit_cli` uses it; mwdec's `Project` will.
- **`Unit`:** `name`, `source`, `flags`, `mw_version` and `version`, the compiler executable, the `sjiswrap` that
  runs it, `precompile`, `target_path`, `base_path`, the edge's working directory and its objdiff.json entry.
- **The compiler's argv**, from the edge:
  - the rule's command gives the compiler, after `cmd /c`, sjiswrap, wibo or wine; what follows `&&` is not the
    compile's;
  - the flags are `cflags` as the shell gives them (a `-pragma` value unquoted), without `-MMD`/`-MD`, then
    `-c SOURCE -o OUTPUT`;
  - an edge run by sjiswrap compiles in process with `transcode_sjis`.
- **Precompiled headers:** when a unit's flags use `-prefix X.mch`, or its edge precompiles, `compile_unit` refuses
  an overlaid header ("header H is overlaid, but U uses the precompiled header X, which may have it"). Echoes
  defines `mwcc_pch` rules but no edge uses them.
- **`compile_unit(&Unit, &Overlay, &CompileOptions) -> Compiled`** (`object`, `console`, `exit_code`, `timed_out`,
  `cancelled`, `trace`, `in_process`, `time`):
  - runs the project's compiler in process (`module_of_copy` of the options' mwcc checkout), with a private `-o`;
  - `inline_max_size` replaces the last `inline_max_size(N)` of the flags, or adds one;
  - `trace` requests a traced compile, with its streams.
- **Without a module** (Prime's GC/1.3.2, other compilers):
  - the executable runs through `project_launcher` (natively on Windows, wibo elsewhere), through sjiswrap where the
    edge runs it so;
  - a source-only overlay becomes a scratch copy, with `-i <source's directory>` before `-c`;
  - a header overlay, and a traced compile, are errors.
- **Tests (opt-in, `MWCC_TEST_ECHOES`, `mwcc-project/tests/echoes.rs`):**
  - the unmodified control is byte-identical to the build's object for a DOL unit and a REL unit (both
    `mwcc_sjis`, as every Echoes edge is), in process;
  - a candidate with 表 and kana gives the same object in process as through sjiswrap and the executable (the
    module-less path, on a scratch copy);
  - `inline_max_size` replaces the flags' last one.

### W4. `mwcc-compare`

- **Objects:** objdiff-core parses them for scores; `words` reads each function's words and relocations with
  `object` (0.39, objdiff-core's) for the profile.
- **Scores with objdiff-core:**
  - **Dependency:** crates.io `objdiff-core =3.8.2`, with `default-features = false` and
    `features = ["std", "ppc", "demangler"]`. It builds with the 1.96 toolchain next to `wasmtime =49.0.3`; the
    two bring `object` 0.39 and 0.40. objdiff.json is read by `mwcc-project`, so objdiff-core's `config` feature
    (which needs its `serde` feature too) is not used.
  - **Policy:** `ScoreOptions.config` (objdiff's option ids and values, as `-c` takes them: objdiff.json's, which
    Echoes has none of), then `functionRelocDiffs=data_value`; `ScoreOptions.mappings` is the unit's
    `symbol_mappings`.
  - **Output:** a `UnitScore`: `fuzzy` (the size-weighted match of the target's functions, an unpaired one 0),
    `functions` in the target's order and `extra` (ours the target lacks). Each function has `name`, `demangled`,
    `size`, `match_percent`, `paired`, `stats`, `residual`, `profile`, and with `ScoreOptions.rows` its rows: per
    side `{diff_kind, instruction: {address, mnemonic, args, formatted}}`, as objdiff-cli's JSON has them.
  - **`stats`:** `original`, `emitted`, `differing` and `exact_instructions`, defined exactly as decomp_common's
    `instruction_stats` and decomp_experiment's `score_symbols` define them.
  - **A prepared `Target`** holds the parsed target and its words, to score many candidates;
    `ScoreOptions.symbols` limits the functions scored (and `fuzzy` to them). Instructions are shown only for
    differing rows unless the rows are asked for. objdiff's parse of our object (its pool-relocation pass) and
    `diff_objs` dominate what is left, both in proportion to the unit's size.
  - **report.json's matches** are objdiff's report configuration's (`report_config`: relocations compared by
    nothing, sections combined, no pool relocations), which is less strict than the kit's policy.
- **Levels:**
  - `identical`: the bytes are equal;
  - `score`: as above;
  - `strict` (§4 step 9): mwdec's exact rules: masked words, relocations per word, generated names compared by
    referenced bytes, a one-to-one mapping, `ExternIndex` and placeholder proofs.
- **Residual:** residual-scan's register/stack/other rows and its verdicts (`register-only`, `stack-only`,
  `register+stack`, `structural`, `missing`, `no-row-diff`; none for a function that matches), computed from the
  rows as residual-scan computes them, so that they equal its own.
- **Profile:** mwdec's classes (reg, stack, branch, other, reorder, substituted, inserted, deleted, reloc), residue
  and penalty, from its opcode alignment of the masked words (mwdec-search's `score.rs`). Until the strict level,
  `reloc` is 1 when the masked words are equal and the function does not match (mwdec counts the strict
  comparator's difference classes there).
- **Disassembly:** objdiff-core's decoder (the rows' instructions).
- **Parity test (opt-in, `MWCC_TEST_ECHOES`, `mwcc-compare/tests/parity.rs`):** for 20 Echoes units (five matched
  and five nonmatching DOL units, as many REL units, from report.json), every target function's `match_percent`,
  row count and both sides' `diff_kind` sequences equal `objdiff-cli 3.8.2 diff -c functionRelocDiffs=data_value
  -u … --format json`, and the residual rows and verdicts equal `residual-scan.py --json` run with that
  objdiff-cli; each unit's `fuzzy`, scored with `report_config`, equals report.json's `fuzzy_match_percent`.
  Echoes' build fetches 3.8.2, so there is no 3.7.0 difference to list.
- **Offline builds:** the kit's bundled source vendors the dependency (`cargo vendor`), or EXTERNALS documents the
  GitHub fetch.

### W5. `mwcc-experiment`

**Plans.** A plan names a unit (or a saved trace), a space, an evaluator, a strategy and a budget. It is the
parameter of the `experiment.run` method.

- **Spaces:** an explicit list, Cartesian axes, combinations of k from a set, text-edit operators, or model edits
  (colouring's move, degree, drop, link, unlink and neighbour degree). Each can carry constraints: `must`/`must_not`
  sets, a maximum size, and predicates such as "touches 55 and 32, not 53".
- **Candidate files:** complete files, or exact replacements in the `### name / @@@ old / @@@ new` format:
  - an old block occurs exactly once;
  - CRLF is normalized for matching and the source's newline style restored;
  - an empty old block is an error;
  - a no-op edit gives a warning.

  Any declared file may be replaced, including headers and files that are absent at entry.
- **Evaluator:** a pipeline of stages, cheapest first, each able to stop a candidate (`prune_if`):
  - **model:** a colouring replay on a loaded trace, about 1 ms;
  - **compile:** a persistent compile, at a few ms, or a normal one;
  - **score:** `mwcc-compare`;
  - **trace:** an oracle trace of the candidate, from the same snapshot;
  - **link check:** optional, a full source link and hash, for finalists only.
- **Strategies:** `enumerate`, `beam`, `anneal` and `singles-then-pairs`.
- **Budget:** candidates, compiles, seconds, and a cancel file.
- **Controls:**
  - the unmodified control runs first and is compared with the build's object byte for byte
    (`control identical to build`);
  - persistent results are confirmed against normal compiles on a sample;
  - the control is repeated at the end to check for nondeterminism.
- **Output directory:**
  - `manifest.json`: files, `absent_original`, root, unit, symbols and the compiler argv;
  - `original/` and `baseline/`;
  - one directory per candidate with its files, `variant.json`, `build.log` (the compiler console), and on request
    the object and `diff.json`;
  - `results.jsonl`: one flushed line per finished candidate,
    `{name, directory, status: ok|compile_error|evaluation_error, scores, timing}`, where `scores` maps each symbol
    to `{percent, original, emitted, differing, exact_instructions}`;
  - `coverage.json`.

  A run resumes from its directory, and a Ctrl-C keeps the rows already written.

**Caches.**
- **Compile cache, in direct mode:**
  - the key is the module hash, the argv and the overlay's file hashes;
  - an entry holds the object, the console, and the files the compile read with their hashes (the host records each
    file `CreateFileA` opens);
  - a hit counts only if those files are unchanged;
  - it lives in memory, bounded, and optionally on disk under `build/mwcc-tools/cache`.
- **Dedup:** candidates with the same rendered files are evaluated once (`duplicate_of`). A candidate whose object
  equals an earlier one's reuses its **score**, never its trace: the same code can come from different graphs.
- **Score cache:** (target, object hash, policy) → `UnitScore`.
- **Trace cache:** the compile key plus the streams and functions → the parsed trace.

**Timing.**
- Every result row and response carries `timing: {load, compile, score, trace, model, queue, other}` in
  milliseconds, and each run reports `productive = (compile + score + model) / (wall × jobs)`, the share of the
  workers' time spent working.
- **Target:** a plan of at least 100 candidates on a warm server spends at least 80% of its wall time in compile,
  score or model stages.
- The opt-in bench records this for three plans: source variants on CEntity, a colouring triple search on
  CElementGen, and an inline sweep on five units.

**The method registry (`mwcc-methods`).**
- **Methods:** each capability is a function `fn(Session, Params) -> stream of Result`, with serde parameter and
  result types and a JSON schema. Every method takes a list of inputs and streams one result per input as it
  finishes.
- **Sessions** keep loaded projects, units, targets, the module, pool snapshots, parsed traces and caches between
  calls.
- **Handles:** a loaded trace or unit is returned once and named in later calls, so nothing is reloaded.

**`mwcc-tools serve [--project DIR]`.**
- **Protocol:** JSON lines over stdin/stdout. A request is `{id, method, params}`; the server answers with
  `{id, result}` lines, then `{id, done, timing}`, or `{id, error}`. `cancel` takes an id.
- **Lifetime:** one server per Python process by default. A local socket for sharing one server between agents can
  come later, on the same protocol.

**`mwcc-tools call METHOD [@params.json | key=value…]` and `mwcc-tools methods`.** These run one method in a fresh
session and print its JSON, or list the methods and their schemas.

**The Python package `mwcc_tools`** (standard library only, in `python/` and shipped in the kit) starts or reuses
the server and checks `schema_version`.
- **Generic:** `engine.call(name, **params)` iterates over results.
- **Typed wrappers** for the commonly used methods:
  - `Unit`: `compile(overlay)`, `score()`, `residuals()`, `inline_sweep(size)`;
  - `Trace`: `replay`, `replay_many`, `search`;
  - `experiment(space, stages, strategy, budget)`, which yields rows;
  - `run_variants` and `run_matrix`.
- **Callbacks:** render functions, `skip` and `prune` predicates, and `evaluate(candidate)` run in Python per
  candidate. The engine sends the candidate's object and diff paths, so a custom metric still works.
- **Reports:** human-readable output (try-variants' and inline-sweep's text, the residual table) is formatted here,
  in one place.

**Acceptance:** an agent's triple search over 30 candidates (single stage plus about 4,000 triples on a saved
CElementGen trace), written against `Trace.replay_many`, runs in under 5 s, against about 14 minutes today.

**From a model edit to source.** A `realize` stage turns a winning model edit into bounded source variants for the
vreg's definition and uses (from `--cse`, `--locals` and provenance):
- move a named local's declaration;
- name a temporary;
- extend a use;
- split an expression.

The variants are labelled "suggested" and evaluated like any others.

### W6. Methods, the CLI and docs

**Methods** (in `mwcc-methods`):

| Method | Does | Replaces |
|---|---|---|
| `unit.load` | a `Unit` handle (W3) | `kit_cli::unit` |
| `unit.compile` | compile with an overlay and options; object, console, timing | the ninja and scratch-copy compiles |
| `unit.score` | `mwcc-compare` levels and stats against the target | `objdiff-cli diff` in the helpers |
| `unit.residuals` | residual verdicts with residual-scan's row shape. Scores the current source compiled in memory, or the built object with `built: true`; `below` reads `build/<version>/report.json` | `residual-scan.py` |
| `unit.inline_sweep` | a flag-axis plan over `inline_max_size`. The configured object is never touched; units whose source sets its own `inline_max_size` are reported; `around_window: CALLEE` sweeps only the sizes where `inline-window` says that callee flips | `inline-sweep.py` |
| `unit.sinit_profile` | per object: the instruction count, the `stmw` register, the frame size, the `lwz`/`mr` counts and the long store runs | `sinit-profile.py` |
| `source.variants` | a source plan: the `@@@` variants format, complete candidate files, or `{files, baseline, candidates}`. `apply` writes the winner into the working source in its newline style, refused when the file has changed since the run started; it is the only write to the tree | `try-variants.py`, `decomp_experiment`'s loop |
| `experiment.run` | any plan (W5) | the loops listed in section 2 |
| `trace.load`, `trace.replay`, `trace.search`, `trace.verify`, `trace.target`, `trace.force`, `trace.remap` | colouring on a loaded trace, with the neighbour-degree edit | `colouring …` |
| `oracle.trace` | a traced compile of a unit or candidate, with `TraceOptions` | `oracle`, `oracle compile-trace`, `oracle run-unit` |
| `oracle.regdiff` | register differences against the target | `oracle regdiff` |
| `oracle.analyze` | the schema-6 report of a saved trace | `oracle analyze-trace` |
| `inline.calibrate`, `inline.diff`, `inline.window`, `tu.data_diff`, `tu.header_impact` | the TU tools | their commands |
| `provenance.*`, `source_search.*`, `source_reduce.*`, `source_index.*`, `source_repro.*`, `source_arenas.*`, `source_nodes.*` | the source-guided tools, with their current JSON documents as results | their commands |

The source-guided tools compile candidates through `PersistPool`. Moving source-search and source-reduce onto
`mwcc-experiment` comes only once their tests pass on it.

**The CLI** (`mwcc-tools`):
- **Generic:** `serve`, `call`, `methods`.
- **Hand-written:**
  - the build commands: `run`/`mwcc-run`, `configure`, `build-hooks`, module precompilation;
  - the compiler-decompilation tools: `pe`, `split`, `link`, `compare`, `download`, `source-only`, `corpus`, …
- **No aliases:** the updated mwcc is used only with new kits, so old command lines are not kept. The new kit's
  launchers and docs, and the MCP docs, call the methods (through the package or `call`); the package's report
  functions print the text the old commands printed, where a doc or a script reads it.
- **Removed:** every other oracle, colouring, provenance, source-guided and TU command with its option parser and
  printer. `call` covers them.

**Docs:**
- `docs/api.md`: the protocol, sessions and handles, `schema_version`, and every method's parameters and results.
  It is generated from the registry's schemas, with hand-written introductions.
- `docs/oracle/experiments.md`: plans, the variants format, stages, caches, timing, the overlay and its limits,
  behaviour without a module, and score parity.
- `python/README.md`: the package, with the examples the kit's docs use.
- `docs/oracle.md` and the README: their tool tables point at the methods and the package. Command
  examples elsewhere in `docs/` become `call` or package examples.
- The personal paths in the tracked docs (9 files) are replaced with placeholders.

### W7. The precompiled-header crash

Some precompiled contexts crash the compiler. mwdec's `split.rs` repairs them, and one repair exists under wasm
(`pch/cc8d…ffd.split` = `0,13`).
- **Find it:** reproduce that context with the oracle and the host's trap report, and find the faulting routine in
  gc2's sources.
- **If gc2 differs from the original:** fix gc2 and add a canary; `split.rs`, `Scorer.pch_broken` and the
  PCH-crash retries then go.
- **If the original compiler also crashes:** record the trigger in `split.rs`, which stays.

### W8. demwcc-echoes

- **Adopt the shared crates:**
  - the module registry, version information, compiler identification, switch and worker threads (`mwcc-wasm`);
  - `PersistPool` for `fast.rs` and `persist.rs::Instance`;
  - `mwcc-trace` for trace options and parsing;
  - `colouring` and `schedule` under `regalloc.rs` and `sched.rs`;
  - `mwcc-project` under `Project` and in place of `flags.rs`;
  - `mwcc-compare` in place of `compare.rs`, `placeholder.rs`, `ExternIndex`, the ELF reader in `asm.rs` and the
    profile;
  - `mwcc-experiment`'s caches and timing.
- **Fast-path failures:**
  - The search trusts fast failures with a diagnostic console.
  - Draft-time decisions (`choose_between`, `repair_or_variant`) pass `confirm_failures = true`, as commit `58a0436`
    requires: "draft-time decisions never act on an unconfirmed persistent-compiler failure".
  - **Acceptance:** in a 60 s perturbed `match`, normal recompiles of fast failures fall from 1,082 to under 50, and
    a fixed-sample `mwdec eval` (same seed and list, three runs before and after) shows no lower exact count and no
    lower evals/s. `tools/gate.sh` does not see the search stage, so it cannot check this.
- **`-g` and `-sym` on the persistent path** (after W1.7): `fast_spec` allows them, so `build_typedb`'s
  forced-declaration rounds, the inline probes and `locate`'s `-sym on` compiles use it.
  - **Acceptance:** cold setup without the TypeDb and inline caches falls by at least 50% (today +6.6 s and +5.5 s).
- **Tracer:** traced candidates come from the module's snapshots, not a full context parse per call.
- **Native paths removed** for GC 2.6 and 2.7:
  - the debugger tracer (about 780 lines);
  - the native `PersistentCompiler` (`persist.rs:1-806`);
  - the process path with its taskkill watchdog and resource-status retries;
  - `compile.rs`' exe fallback;
  - `persist-bench` and `trace-parity`.

  A missing module is an error that says how to build it. Compilers without a module keep their native path.
  `split.rs` follows W7.
- **Cancellation:** `Request.cancel` is wired to the search budget.
- **Docs:** `ARCHITECTURE.md` gets the fast-worker count (6) and the memory-cap note; the README lists the required
  module.

### W9. The Echoes kit

**Where the package comes from.** The kit imports `mwcc_tools` from the mwcc checkout it requires
(`$MWCC_ORACLE_ROOT/python`), so the package always matches the executable. `scripts/_mwcc_tools_path.py` puts it
on `sys.path` and replaces `_mwcc_native.py`'s executable lookup.

**The four experiment scripts** keep their names, options and printed output, because the docs, the routine-loop
skill and `selftest.py` name them:
- `try-variants.py`, `inline-sweep.py`, `residual-scan.py` and `sinit-profile.py` become short `argparse` scripts
  calling `Unit`/`source.variants`, `inline_sweep`, `residuals` and `sinit_profile`;
- each prints the package's report for its method.

**The oracle launchers** (`oracle-run.py`, `oracle-regdiff.py`, `oracle-colouring.py`, `inline-calibrate.py`) become
short scripts on the package (`oracle.trace`, `oracle.regdiff`, `trace.*`, `inline.calibrate`), printing its reports;
their options may change where the methods' parameters read better, with the kit's docs updated to match.

**`decomp_experiment.py` and `decomp_matrix.py`** become re-exports of the package (`run_variants`,
`score_symbols`; `run_matrix`, `checked_replace`, `matrix_cases`, `refresh_coverage`). So
`sys.path.insert(0, "scripts"); from decomp_experiment import run_variants` keeps working.
- **What stays the same:** the function signatures except `evaluate`'s, the row schema and the artifacts. `build`
  is still the ninja target; it names the unit's edge.
- **Edges that are not mwcc compiles:** when `build` names one (a fixture root, a link or a custom rule), the call
  fails before compiling, with an error naming the edge's rule.
- **What runs:** candidates render in Python and compile in process through the engine. The working tree is never
  written, so there is no restore, rebuild or lock.
- **`evaluate`:** now `evaluate(candidate)`, receiving the candidate's files, object and diff paths. A zero-argument
  callback gets an error that names the change.
- **Streaming:** rows stream as candidates finish; `run_matrix` refreshes `coverage.json` after each row. Ctrl-C
  cancels the request and keeps the rows written so far.

**`decomp_common.py`** is unchanged. It is shared across the kits, and `ensure_source_object` stays for the
objdiff-based helpers.

**mwcc-tools is required.**
- The scripts and `run_variants` fail with the build instructions when the executable, the package or `mwcc.wasm`
  is missing.
- `install.py` checks for the executable (`MWCC_TOOLS`, `MWCC_ORACLE_ROOT/target/release`, or the path), for
  `$MWCC_ORACLE_ROOT/python/mwcc_tools` and for `build/GC_2_7/wasm/mwcc.wasm`, and stops with the build commands.
  `--skip-tool-check` exists for the kit's packaging tests only.
- `selftest.py` checks the same, plus the server's `schema_version`, and that the version is at least the pinned
  commit.

**Tests.**
- **Rewritten against a fake server:** `test_decomp_experiment*.py` and `test_decomp_matrix.py`. The fake is a
  Python script that speaks the protocol and writes rows. They cover rendering, header and absent files, generation
  errors, duplicates, coverage, an interrupted run, recovery from a partial run, `checked_replace` and axis
  validation. The Windows replace-retry and lock tests go, along with that code.
- **Missing-tool tests:** the four scripts, `run_variants` and `install.py` fail with the instructions.
- **Updated:** `scripts/tests/test_agent_tools.py` (the scripts' `--help`) and `selftest-expected.json` (the
  `inline-sweep` line on CSpatialPrimitive).

**Docs and metadata.**
- **`overlay/scripts/README.md`:** the package, the four scripts as clients of it, and the requirement.
- **`routine-loop/SKILL.md`:** one line on the requirement. The symptom table keeps its script names.
- **`.agents/docs/oracle.md`:** a "Python API" section, with examples for variants, matrices, residuals, sweeps and
  `Trace.replay_many`, and the build steps.
- **README, AGENTS.md and EXTERNALS.md:**
  - mwcc-tools, its package and `mwcc.wasm` are required;
  - the mwcc pin and `references/mwcc-oracle.bundle` are updated;
  - demwcc-echoes is recorded as CC0 (LICENSE since `858707d`), with its bundle updated when W8 is committed.
- **`MCP-GUIDES.json`:** the revisions after W10, with RELEASE.md corrected. `bundle-files.json` and
  `bundle-manifest.json` are regenerated by `build-bundle.py`. An archive is built only when you ask for a release.

**`KIT-IMPROVEMENTS.md`.**
- **Echoes items applied:**
  - the `MCP-GUIDES.json` refresh;
  - the personal paths in the mwcc bundle's docs (W6);
  - the mwdec license (now CC0).
- **Still open:** mwdec's unpublished commits (the owner's call). The "deliberately not imported" table is
  unchanged.
- **Done:** the Echoes import, with its date and archive.
- **For Prime:** GC/1.3.2 has no module. The engine runs its executable as a process, with source-only variants
  and no header variants. Requiring mwcc-tools there is that kit's decision.
- **For Corruption:** its game code is GC/2.7, so the package, the four scripts and the re-exports apply as they
  are.
- **Open ideas:**
  - a GC/1.3.2 module, for Prime's header variants and persistence;
  - `decomp_common.run_objdiff` on the package's `Unit.score`, giving the kits one scorer.

### W10. Echoes MCP docs (each edit after your OK)

| Page | Section | Change |
|---|---|---|
| `experiments.md` | intro, "Runner API" | `run_variants` and `run_matrix` come from the `mwcc_tools` package; the kit's `decomp_experiment` and `decomp_matrix` re-export them, so the imports stay. Candidates render in Python and compile in process; the working tree is never edited or rebuilt. `build` stays the ninja target. `evaluate` is now `evaluate(candidate)`, with the candidate's files, object and diff. Header and absent-file replacements work. Link the kit's `.agents/docs/oracle.md` for the rest of the API (`Unit`, `Trace`, `experiment`). |
| `experiments.md` | "Check capabilities…" | mwcc-tools and its package are required (`MWCC_ORACLE_ROOT`). The locking and platform notes go: nothing edits the tree. |
| `experiments.md` | "Combinations without an additional helper" | `run_matrix` is installed and writes `matrix.json` and `coverage.json`. Keep the `itertools` example as the minimal form. |
| `experiments.md` | "Results and artifacts" | `manifest.json` records the compiler argv. Candidates can keep their object and `diff.json`. The control reports `control identical to build`. There is no `restore-build.log`. |
| `experiments.md` | "Restore, recover and select" | Becomes "Resume and select": an interrupted run keeps its rows and resumes from its directory, and nothing needs restoring. Selection is unchanged: the runner never applies a winner, and applying one is a deliberate edit followed by a real build. |
| `setup.md` | "Checkout and capabilities" | The matrix helper is supplied. New row: mwcc-tools, its `mwcc_tools` package and `mwcc.wasm` are required, built as EXTERNALS.md says, and the installer checks them. |
| `tooling-evidence.md` | "Kit helpers for variants…" table | try-variants, residual-scan, inline-sweep and sinit-profile are scripts on the `mwcc_tools` package, with the same options and output. try-variants: the unmodified control is compared with the build object byte for byte. inline-sweep: never overwrites the object, so it need not be serialized with builds. residual-scan: scores the current source compiled in memory (`--built` for the built object). |
| `tooling-evidence.md` | "MWCC oracle on Echoes" | The Python package as the interface for experiments, `mwcc-tools call`/`methods` for one-off calls, and the one-module build line. |
| `tooling-evidence.md` | "Symptom to helper", and the `run_objdiff` paragraph | Rebuilding a stale object (`DECOMP_NO_REBUILD`) concerns the objdiff-based helpers (`decomp-diff`, `decomp-status`, `decomp-context`, `report-changes`). residual-scan and inline-sweep compile in memory and never read a stale object. |
| `execute.md` | §1, the `&& transform_dep.py` paragraph | Keep the evidence. mwcc-tools takes only the compiler argv from the edge, so the tail never reaches the compiler. The control rule stays. |
| `research/Tooling-setup-history-G2ME01.md` | new entry | The move to mwcc-tools, the parity results and the archive. |

`workflow.md`, `matching-tips.md` and `implement.md` stay as they are. The stuck-residual rule relies on
residual-scan's verdicts, which the W4 parity test keeps identical.

---

## 4. Order of work

1. **W1.1-W1.4:** the one module, the trace sink, the listings and the function filter. Then the timing fields
   (W5).
2. **W1.5, W2, W3, W4:** the registry, artifacts and threads; `mwcc-trace`; `mwcc-project`; and `mwcc-compare` with
   its parity test, which must pass before anything uses scores. W1.6 (the overlay) goes in here.
3. **W5's core:** plans, the staged evaluator, `results.jsonl` and timing. Then the method registry, `serve`, and the
   Python package with its fake-server tests.
4. **W5's caches,** then W6's methods, `call`/`methods`, the removal of the old commands, and the
   docs (`docs/api.md`, `docs/oracle/experiments.md`, `python/README.md`).
5. **W9:** the kit's four scripts, the re-exports, `install.py`/`selftest.py` and the tests, against the local
   Echoes checkout.
6. **W10:** the exact edits drafted and shown to you before writing.
7. **W1.7 and W1.8:** persist line numbers and the pool, as the evaluator's persistent stage. Then W8's adoption of
   the shared crates, its fast-path change and its `-g`/`-sym` path.
8. **W7, then W8's removals and cancellation, then W1.9.**
9. **Later:** the `realize` stage and the strict level in result rows. Each is measured.
10. **Throughout:** `KIT-IMPROVEMENTS.md`, with the kit's pins updated once the mwcc changes are committed.

## 5. Open decisions

- **What residual-scan scores by default:** the current source compiled in memory (as planned), or the built object.
- **Upstreaming W8:** changes to demwcc-echoes stay local unless you decide to propose them upstream.
