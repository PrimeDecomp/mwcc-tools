# Oracle and tools: Rust migration plan

Prepared 2026-10-16. This is the original migration design. Implementation progress and remaining gates are recorded in [oracle-rust-status.md](oracle-rust-status.md).

API policy updated by the user on 2026-10-16: matching legacy APIs is not required. Prefer coherent native commands and versioned structured results, and update kit documentation and callers to use them. Archived Python implementations remain useful independent references for compiler behavior; their imports, argument spellings and rendered paragraphs are not release requirements. This policy supersedes the original compatibility-first interface recommendations below.

Recommended order: migrate the host-side analysis and command-line tools, migrate their kit consumers, then migrate the executable construction tools. Treat converting the in-process C hooks as a separate, explicit feasibility gate. Keep the original compiler as the execution engine throughout: its decisions are what the oracle observes.

## Baseline and scope

Planning began at main commit `7556194`, which does not contain the oracle. The implementation baseline inspected is branch `oracle`, commit `b75c4e5e0b4bc97ae06c26ea9d6aad8379ac1722`, available at `<frozen-oracle>`. Implementation uses that baseline on `codex/oracle-rust-migration`.

The kit examples inspected in `<decomp-kits>` are:

| Kit archive | Oracle integration |
|---|---|
| `echoes-agent-workflow-mcp-2026-10-15-r3.zip` | Pins `b75c4e5e`; ships `oracle-run.py`, `oracle-regdiff.py`, `oracle-colouring.py`, and `inline-calibrate.py`. Game code uses GC 2.7; some libraries use other versions. |
| `prime-agent-workflow-mcp-2026-10-14-r3.zip` | Pins `f00b8a8201d0dca13c2206fce61472b02d83b01e`; ships run, regdiff and inline-calibrate, without the colouring wrapper. Its main oracle integration uses GC 1.3.2. |
| `corruption-proto-agent-workflow-mcp-2026-10-13.zip` | No oracle helper scripts in the archive. Treat it as a later integration target, not an existing compatibility consumer. |

`KIT-IMPROVEMENTS.md` provides additional integration history. Its older statement that the local checkout lacks a GC 2.7 oracle is stale: executables for GC 1.3, 1.3.2 and 2.7 are now present in `<frozen-oracle>/build`. Their existence is not a fresh correctness check.

| Layer | Existing implementation | Migration scope |
|---|---|---|
| Trace producer | `src/oracle/Hooks.c` (1,655 lines), `OracleTrace.c` (31), compiler headers, per-version hook lists | Retain during host migration; investigate Rust separately. |
| Analysis and execution | `tools/oracle.py` (3,193 lines) | Port parsing, analysis, reports, compiler invocation, checking, overrides and limit patches. |
| Colouring | `tools/colouring.py` (1,284 lines), `tests/test_colouring.py` | Port replay, target inference, graph edits, search, remapping and saved traces. |
| TU analysis and validation | `inline_diff.py`, `data_diff.py`, `inline_window.py`, `header_impact.py`, `corpus.py` | Port onto shared Rust APIs. |
| Binary investigation | `locate.py`, `nearest.py`, `fdiff.py`, `disasm.py`, `funcmatch.py`, `bindfrom.py`, `remap.py`, `derive_splits.py` | Subsequent Rust commands using common binary readers and version metadata. |
| Executable construction | `pe.py`, `split.py`, `compare.py`, `link.py`, `implib.py`, `rsrc2res.py`, `transform_dep.py`, `verify.py`, `download_tool.py`, `configure.py`, `gates.sh` | Later migration; preserve split/relink semantics and existing build outputs. |
| Kit integration | The four oracle helpers above and their use of `decomp_common.py`, objdiff and Ninja | Move oracle-specific logic to Rust; keep unrelated kit workflows outside this migration. |

The matching compiler sources under `src/frontend`, `src/backend`, etc. are not a compiler rewrite target. The original Windows compiler, historical linker, licence stub, Ninja, objdiff and wibo remain external components. A Rust host binary does not eliminate those dependencies.

## Target shape

Use a Cargo workspace in the oracle repository, initially with two crates:

- `mwcc-oracle-core`: ordinary Rust library, with modules for version/configuration data, trace parsing, compiler execution, ELF/target assembly, analysis, colouring and report models. Keep parsing and analysis callable without launching a compiler.
- `mwcc-tools`: CLI and orchestration, distributed as a native `mwcc-tools` executable. Start with `oracle`, `colouring`, `corpus`, `inline-diff`, `data-diff`, `inline-window`, and `header-impact` subcommands; add binary/build commands in later stages.

Separate the binary-format code into another crate only if the later build tools justify it. Avoid one crate per existing script. Select and pin concrete dependencies during implementation after checking their APIs and legacy format support; this plan does not depend on particular crate versions.

Proposed usage:

```text
mwcc-tools oracle --from-ninja ../prime/build.ninja src/GuiSys/CGuiFrame.cpp --function Draw
mwcc-tools colouring search --from-ninja ../echoes/build.ninja src/Kyoto/Particles/CElementGen.cpp --function RenderParticles__
mwcc-tools oracle analyze-trace saved.trace --version GC_1_3_2 --json
mwcc-tools oracle regdiff --project ../prime --unit UNIT --function FUNCTION
```

The last two are new interfaces to design, not existing commands. The offline interface must also expose PCode, inlining and register explanations needed by regdiff, so it does not need to reconstruct Python objects.

Keep three boundaries clear:

1. A compile request resolves a project, compiler version, arguments, overrides and trace options, then produces object bytes, diagnostics, trace data and check status.
2. Analysis consumes a trace and optional object/target data. It has no working-directory changes or compiler side effects.
3. Reports render structured analysis as legacy JSON or human-readable text. Machine consumers should obtain structured explanations instead of parsing paragraphs.

## Behavior and native protocol contracts

- **JSON:** report schema 6 is the captured behavioral reference. Prefer the native format-1 compilation envelope and structured regdiff results for new callers. Version protocol changes explicitly, including absent fields versus null, identifiers and ordering; do not silently change data meanings. Keeping a legacy-shaped output is optional.
- **CLI:** document a coherent native interface, with compiler arguments clearly separated from tool options. Preserve required behavior such as exact function selection and non-mutating candidates; legacy argument spellings and Python entry points need not be reproduced. Update kit callers and tests together with interface changes.
- **Checks:** use explicit verified/mismatch/skipped/unavailable/failed states in native results. A nullable `identical` projection may remain for reports; null is not success. The ordinary oracle compares full object bytes; the corpus compares non-debug sections, `.bss` sizes, normalized messages and exit status. Keep that distinction.
- **Trace:** keep existing `ra`, `fr`, `inl`, `sc`, `tp`, `lc`, `bk`, `ce`, `st` and `pp` records. Pointer-shaped values are identities within a trace, not host addresses to dereference. Preserve record order and nesting. Report malformed/truncated known records; retain unknown records for forward compatibility without inventing conclusions.
- **Saved colouring inputs:** read existing `PREFIX.trace` and optional `PREFIX.o`. Old files do not carry complete provenance: require or retain an explicit version choice. A new optional manifest can record compiler hash, schema, options and source identity while leaving old pairs readable.
- **Project resolution:** maintain `MWCC_ORACLE_ROOT`, explicit root overrides and existing `build/<version>/oracle/mwcceppc.exe` paths. The executable must work outside the source checkout. Package or locate version tables, opcode/ENode names and function metadata explicitly; today some are read from repository headers/config files.
- **Ninja and paths:** test continuation lines, escaped colons/spaces, edge/global `mw_version`, quoted arguments, Windows drive letters, slash variants and case handling. Parse the supported generated-Ninja subset deliberately and reject unsupported constructs clearly; do not claim a complete Ninja interpreter. Preserve the current flag tokenization with fixtures before extending it.
- **Subprocesses:** Windows launches the PE executable directly; other hosts need the supported runner. Use argument arrays, per-child environments, private temporary directories and isolated trace files. Add cancellation/timeouts without mixing diagnostics into JSON. Keep source/header overrides non-mutating and preserve include order. Document the existing limitation for includes relative to an including header.
- **Patching:** block-limit changes patch copies of both compilers. Preserve expected-byte checks and add identity checks tied to version metadata; never patch installed originals. A patch of only one side invalidates the comparison.

## Migration stages and acceptance gates

### 0. Capture the reference and consumer contracts

Create small fixture sources covering allocation/coalescing/spills, inlining and depth limits, templates, stack/scalarization, scheduling, block splitting, CSE and propagation. Capture trace, object, JSON, text, diagnostics and invocation metadata from the Python baseline for GC 1.3, 1.3.2 and 2.7. Include no-function output, failed compilation and missing-original cases.

Also capture representative Prime/Echoes unit inputs, Ninja edges, objdiff unit metadata and output, plus saved colouring pairs. Keep redistributable synthetic fixtures in the repository; retain game-derived fixtures locally or in appropriate private CI storage. Record exact source/compiler hashes and tool versions.

Inventory the imported Python API and kit text dependencies. Add a differential harness that can run the Python and Rust implementations on the same captured input. Normalize only specified differences such as temporary paths; never normalize register order, cost, target selection or check results.

**Gate:** reference fixtures reproduce on the pinned Python implementation, with an explicit manifest of available versus missing integration inputs. Existing documentation's corpus counts are historical evidence, not results of this planning session.

### 1. Establish the Rust core and execution layer

Implement typed IDs for functions, blocks, instructions, virtual registers and classes; version metadata; trace event parsing; ELF32 big-endian objects; target assembly parsing; compile requests; checks and override handling. Keep x86 PE/COFF and PowerPC ELF representations distinct.

Port `corpus.py` early as an independent validation command. Its current `compile_one` launches executables directly, unlike `oracle.launcher`; share the new runner so corpus validation also works with wibo. Use deterministic report ordering even when jobs run concurrently.

Implement the offline trace command and enough analysis to compare allocation snapshots and frame data. Keep the Python CLI as default during this stage.

**Gate:** parsed events, object comparisons and compiler invocations agree with fixtures; failed/unchecked compilations cannot appear verified; Windows and Linux/wibo smoke tests pass. Malformed binary lengths and truncated traces return errors without panics or unbounded allocation.

### 2. Port oracle analysis and reports

Port in cohesive increments:

1. Liveness, definitions/uses, allocation rounds, simplify/select explanation, coalesced/spilled/split states and frame reports.
2. Inlining decisions, full costs, nested breakdowns, sensitivity, near-threshold views and instantiation events.
3. Scalarized locals and stack comparisons, scheduling, basic-block reports, CSE/IR, propagation and exchange origins.

Use explicit ordered collections where order influences decisions or structured results. Preserve 32-bit masks, signed values, tie-breaks and binary endianness. Provide structured register explanations and migrate kit consumers to those fields; human report text is not a machine API.

**Gate:** structured analysis matches the frozen behavioral fixtures, with versioned native results and documented human views. Exact legacy text is not required. Raw trace selection and supported views are covered. Re-run real compilations with the unchanged C producer and confirm object/diagnostic equivalence.

### 3. Port colouring, then optimize it

Port all six commands: `verify`, `target`, `force`, `search`, `replay`, `remap`. Preserve graph edits, physical-register exclusions, register claims, class-specific behavior, target voting, instruction-field decoding and the distinction between traced and predicted claims. Port the existing 12 tests, then add multi-round and unsupported-spill fixtures from real traces.

Port deterministic replay and edit enumeration first. Search currently uses Python's seeded random generator, simulated annealing and floating-point acceptance. Matching `--seed` numerically with a different Rust RNG is insufficient. Initially reproduce the reference random operations and draw order, verified by fixtures. Any later faster search mode must identify its algorithm/version and have a separate reproducibility contract.

Spills are not modelled by the current what-if tool; retain that limitation and make unsupported results explicit. An exact graph replay result remains a source-change hypothesis, not proof that a C++ edit will generate it.

**Gate:** replay agrees on all supported fixture allocations; deterministic commands agree with Python; compatibility search reproduces fixed-seed fixtures. Benchmark parsing, replay and search separately from compiler runtime before claiming a speedup. Parallel restarts are a later optimization with deterministic result ordering.

### 4. Port TU tools and migrate the kit boundary

Port `inline_diff`, `data_diff`, `inline_window`, then `header_impact`, reusing the shared compile/target APIs. Preserve weak-symbol/module ownership, string pooling, relocation-aware comparisons, BSS, anonymous-name normalization and original-compiler fallback for versions without hooks. Preserve the existing instruction-similarity semantics; substituting a different diff algorithm changes reports.

Integrate the kits through a stable executable/JSON boundary:

| Consumer | Required transition |
|---|---|
| `oracle-run.py` | Document native `oracle run-unit` for interactive views and `oracle compile-trace` for structured compile/check/provenance results. Preserve unit flags and candidate includes; check by default and make unchecked execution explicit. |
| `oracle-colouring.py` | Use native `colouring unit`, with all six graph commands and saved sessions. Source experiments remain private overrides. |
| `inline-calibrate.py` | Use native `inline-calibrate` and its JSON output. Preserve per-decision thresholds and infer compiler versions per unit. |
| `oracle-regdiff.py` | Use native `oracle regdiff --json`, with structured selection, differences and explanations. Retain the objdiff relocation policy without importing Python analysis objects or parsing report paragraphs. |

Move imported Python consumers to the native executable/JSON boundary and update their documentation/tests. Do not reproduce `parse_trace`'s Python objects or introduce Python extension wheels to preserve imports. Keep the frozen Python implementation as an independent regression reference and, where useful, an explicitly selected rollback implementation.

Resolve the native executable and compiler root predictably; do not silently switch backend after a Rust runtime failure. `MWCC_TOOLS` supports optional old-name launchers; native commands are the documented primary API. Use archived Prime and Echoes wrappers as regression references before updating kit source, manifests, pins, documentation, tests and new archive hashes. Old API/prose parity is not a gate. Leave old archives immutable.

**Gate:** the named Prime/Echoes workflows pass using original sources and candidate files; neither checkout is modified; old saved traces remain usable. Test both archived pin baselines where feasible. Native consumers no longer import Python oracle classes or parse prose. Corruption adoption is a separate integration change.

### 5. Migrate executable construction and investigation tools

Extend shared binary support to legacy PE32 and x86 COFF, including relocations, auxiliary symbols, imports and resources. Port in dependency order:

1. PE/config readers, symbol/split readers, compare helpers and investigation commands.
2. Split and COFF writers, import libraries and resource conversion.
3. Link orchestration, including `NAME__retail` hook renaming, supplements, patches, section ordering, timestamps and hash checks. Continue invoking the historical linker.
4. Configuration/Ninja generation, dependency conversion, downloads/extraction, verification and the sequential gate driver.

Validate output from any general-purpose object writer against this repository's exact layout requirements before using it. A valid COFF file is not necessarily interchangeable with one the old linker expects. Keep existing build paths and flags compatible while switching Ninja commands incrementally.

Move `gates.sh` into an exit-status-aware Rust command. Configure each version sequentially because `configure.py` currently overwrites the shared `build.ninja`; alternatively introduce isolated build directories as a separately tested change. Preserve archive checksum verification and platform-specific extraction behavior.

**Gate:** the existing version matrix's comparison results do not regress, and all currently SHA-exact relinks remain exact. The oracle can be built using Rust host tooling plus the retained compiler/linker/hook sources. Only then retire the Python build implementation.

### 6. Decide and, if feasible, port the in-process hooks

This is the high-risk part of a literal all-Rust oracle. `Hooks.c` accesses version-dependent compiler structures directly and calls renamed retail functions. `OracleTrace_Printf` manually accesses stack varargs and calls the compiler's C runtime. Host-side Rust success says nothing about compatibility here.

Run a bounded spike with one simple hook before translating the file:

- Produce a 32-bit x86 Rust object with the required symbol decoration and calling convention, then link it with the existing split objects using the historical linker.
- Inspect relocations, section names, stack alignment, runtime imports and helper symbols. Test a minimal `no_std` implementation with no unwinding or Rust allocator crossing the boundary; resolve any required helpers explicitly.
- Prove struct sizes, offsets, unions, bitfields and alignment against the existing C definitions for all three oracle versions. Do not automatically treat compiler pointers as valid Rust references; isolate raw-pointer access in a small ABI layer.
- Preserve the existing runtime's file/varargs conventions through a minimal C shim if necessary, then assess whether replacing that shim is feasible and useful. Rust formatting must preserve trace syntax and avoid changing compiler behavior.
- Relink and run the equivalence corpus with tracing disabled, baseline tracing and every optional trace family enabled.

**Gate:** the simple hook passes ABI/layout checks and object/diagnostic equivalence on all three versions. If it does, port hook groups incrementally with the same gates. If it does not, record the exact incompatibility and retain the C producer; that completes the host-tools migration, but must not be described as a fully Rust trace producer. Budget full hook conversion only after the spike.

## Validation and release criteria

Use three independent levels of evidence:

1. **Offline parity:** fixed traces, ELF/COFF inputs, target assembly, JSON and reports. These are fast and do not require original executables or game trees.
2. **Live equivalence:** original versus instrumented compiler, and Python versus Rust orchestration. Include failed compiles, missing traces, stale/mismatched versions, overrides, patched limits and unsupported versions. Test trace switches individually and together.
3. **Consumer workflows:** Prime GC 1.3.2, Echoes GC 2.7 and its 1.3.2 library use, plus GC 1.3 coverage. Exercise unit selection, objdiff matching, candidates, header changes and saved colouring sessions.

The baseline documentation reports 2,313 canaries for each of the three oracle versions, Prime 604/604 on 1.3.2 and 64/64 on 1.3, and Echoes 729/729 on 2.7 and 31/31 on 1.3.2. Recover those corpus manifests and rerun them where available. Record omissions; a smaller available corpus does not reproduce those historical claims.

The existing CI builds older compiler versions through GC 1.3 and does not itself establish full oracle parity. Add Windows and Linux host tests, offline fixtures on every change, and live oracle checks where inputs are available. Do not promise macOS compiler execution until its runner path is demonstrated; offline analysis can be validated separately.

Ship versioned native artifacts with checksums, the required metadata, and a compatibility manifest identifying supported trace/report versions and compiler hashes. Pin the Rust toolchain and dependencies. Keep the C producer's provenance separate from the Rust analyzer version. Measure end-to-end time, trace parse time, replay/search time and peak memory on a fixed workload; compiler-bound commands may see little wall-clock improvement.

Switch callers to the documented native API after behavior and saved-artifact checks pass. Keep the pinned Python reference separately available for regression/rollback work; it need not retain the production API. Release notes must explain intentional interface and behavior changes.

## Recommended first implementation milestone

Deliver stages 0–2 as the first usable release: pinned fixtures, Rust trace parsing and analysis, compiler execution, corpus checking and versioned structured output. Then deliver colouring and the kit migration as the next release, followed by build tools. The hook ABI spike can run once fixtures exist, but should not block useful host-side releases. Legacy API matching is not a milestone or release gate.

Completion means every in-scope tool has a named replacement, verified contracts and a migrated caller. Keep separate completion statuses for the Rust host tools, Python-free oracle kit workflows, Rust executable-construction tools and Rust trace producer so a remaining legacy dependency is visible.

## Follow-on work after migration completion

After this migration is finished and its completion gates pass, implement [source-guided graph search](oracle-source-guided-search.md). The purpose is to connect successful graph edits to C++ constructs that actually produce them, using allocation replay, graph search, graph comparison and existing frontend provenance as the foundation.

| Priority | Extension | Required result |
|---|---|---|
| 1 | Complete backend provenance and coalescing explanations | Record every virtual register's creation site, originating expression, allocation category and merges, including backend address, call-result and array-promotion temporaries. Explain each coalescing attempt's deciding condition, including window limits, interference and instruction flags. Retain disappearing temporaries that still affect graph degrees. |
| 2 | Source-variant search scored by graph changes | Use `remap` to score explicit objectives: eliminate a relevant coalesced temporary while preserving symbolic relocations; initialize a flag directly in its final register without the extra move; or change a value's allocation category without changing its instructions. Deduplicate variants that produce the same relevant compiler state. |
| 3 | Mechanism-preserving reducer | Shrink examples while preserving predicates such as a symbolic argument creating an extra temporary or a second promoted array element failing to coalesce. Avoid predicates tied to exact final physical registers. Exhaustively test small declaration, parameter, wrapper and promotion variants, then verify discoveries in `RenderParticles`. |
| 4 | Searchable examples from matched code | Index oracle traces with source and build context. Find symbolic address arguments without the unwanted temporary and promoted values whose initialization coalesces as needed; use their actual C++ constructs to seed experiments. |

**Build provenance and the graph-directed harness first.** The missing information is which source transformations realize useful graph changes. This work is deferred until after migration completion and is not part of the migration parity gates; implementation details and acceptance examples are in the linked roadmap.

## Inspected source references

- Oracle checkout: `<frozen-oracle>/docs/oracle.md` and `docs/oracle/{trace,colouring,tu-tools}.md`.
- Host implementation: `<frozen-oracle>/tools/oracle.py`, `colouring.py`, `corpus.py`, TU helpers and binary/build utilities.
- Producer/build boundary: `<frozen-oracle>/src/oracle/{Hooks,OracleTrace}.c`, `include/compiler/OracleTrace.h`, `configure.py`, `tools/link.py` and per-version config files.
- Tests/CI: `<frozen-oracle>/tests/test_colouring.py`, `.github/workflows/build.yml` and `tools/gates.sh`.
- Kit archives listed above: `overlay/scripts/oracle-*.py`, `inline-calibrate.py`, `decomp_common.py` and `EXTERNALS.md`; accompanying `KIT-IMPROVEMENTS.md`.

This plan is based on static inspection. No full corpus, live kit workflow, Rust ABI experiment or performance benchmark was run during planning.
