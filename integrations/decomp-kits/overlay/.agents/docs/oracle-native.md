# Native oracle workflow

Use `mwcc-tools` directly. Install the native executable on PATH and set
`MWCC_ORACLE_ROOT` to the checkout containing the built oracle compilers.
Prime game code uses `GC_1_3_2`; Echoes game code uses `GC_2_7`, with other
versions selected per library. Let the unit's Ninja edge select its compiler
and flags. Run these examples from the game checkout, or replace `--project .`
with its path. Neither candidates nor header overrides require source swaps.

The kit's `install-native.py PROJECT --apply` installs its checked host executable
and notices; use the printed `MWCC_TOOLS` path or add its `bin` directory to PATH.
The source bundle and pin are in `native-manifest.json`. Build the oracle with
`mwcc-tools configure --root ORACLE_ROOT --version VERSION --oracle`, then Ninja
in that checkout. `--oracle` selects linking and Rust observation hooks by default;
`--producer c` selects the explicit rollback and regeneration retains that choice.
Building hooks needs Rust 1.96.0 with `i686-pc-windows-msvc`; offline analysis uses
only the host executable. See the kit's `EXTERNALS.md` for complete setup.

## Inspect a unit

```text
mwcc-tools oracle run-unit --project . --unit main/Kyoto/Particles/CElementGen -- --locals --schedule
mwcc-tools oracle regdiff --project . --unit main/Kyoto/Particles/CElementGen --function RenderParticles --pcode
```

For Prime, use a unit such as `main/GuiSys/CGuiFrame` and function `Draw`.
`run-unit` checks the complete object against the stock compiler by default.
Use `--fast` only for an intentionally unchecked run. A successful process
exit alone does not establish object equality; inspect the check result.

To try a candidate, add `--candidate candidate.cpp` before the `--` separator
for `run-unit`, or alongside the unit options for `regdiff`. The unit's original
source directory stays on the include path. Regdiff may report a stale project
object; it does not rebuild the game checkout to hide that state.

## Automate with structured results

Use the format-1 compilation envelope when a harness needs trace data,
compiler/source/object hashes, exact flags and an explicit check result:

```text
mwcc-tools oracle compile-trace --from-ninja build.ninja --locals --schedule src/Kyoto/Particles/CElementGen.cpp > compilation.json
```

Read `format_version`, `compilation` and `report`. Require
`compilation.check_status.status == "verified"` when verified equality is a
precondition. Other states include `mismatch`, `skipped`, `original_unavailable`
and `original_failed`; they are not success. `compilation.trace` retains the
trace, while `report` contains the analysis. Validate supported format/schema
versions and keep diagnostics separate from JSON.

For source/header experiments, add `--override PATH=REPLACEMENT` to
`compile-trace`. Use the source spelling from the Ninja edge for a source
override. The tool compiles replacements privately and preserves include order.

For structured register differences, use:

```text
mwcc-tools oracle regdiff --project . --unit main/Kyoto/Particles/CElementGen --function RenderParticles --json > regdiff.json
```

Read its format-1 `symbol`, `target`, `selection`, `analysis`, `differences`,
`explanations` and `compilation` fields. In particular,
`compilation.project_object_identical` describes the saved project object;
it is distinct from a stock-compiler equality check. Register explanations
are data, so tools should not extract them from human report paragraphs.
Regdiff uses objdiff's `functionRelocDiffs=data_value` policy; `OBJDIFF_CLI`
can select its executable explicitly.

## Replay and compare graph changes

```text
mwcc-tools colouring unit --project . --unit main/Kyoto/Particles/CElementGen verify --function RenderParticles__11CElementGenFv --save particle-baseline --json
mwcc-tools colouring unit --project . --unit main/Kyoto/Particles/CElementGen target --function RenderParticles__11CElementGenFv --load particle-baseline --json
mwcc-tools colouring unit --project . --unit main/Kyoto/Particles/CElementGen search --function RenderParticles__11CElementGenFv --load particle-baseline --seed 2 --json
```

Verify replay before trusting a search, and use exact link names to distinguish
overloads. Supported commands are `verify`,
`target`, `force`, `search`, `replay` and `remap`. Use repeated `--load` options
with `remap` to compare saved states. Saved sessions contain trace/object data
and a provenance manifest. Legacy `.trace`/`.o` pairs can also be loaded;
unit dispatch supplies the compiler version, or specify it explicitly.
Spills remain unsupported by the what-if model. A useful graph edit is a
source-transformation hypothesis, not evidence that particular C++ produces it.

The development `remap --json` interface uses format 2: all captured graph nodes,
unmatched nodes, ambiguous/competing instruction votes and unmapped boundary edges
remain explicit. Instruction-invisible nodes are not automatically eliminated.
Elimination is indeterminate until backend lifecycle coverage supplies evidence.
`--first-round N`/`--second-round N` select zero-based occurrences within the class;
`--first-snapshot before|after`/`--second-snapshot before|after` select existing trace
snapshots. These additions are in development after the pinned migration release.

## Observe actual coalescing decisions (development)

The current Rust source adds backend branch provenance after the pinned migration
release. Build fresh native tools and a fresh Rust producer to use it:

```text
mwcc-tools oracle compile-trace --oracle-root ORACLE_ROOT --from-ninja build.ninja --backend src/Kyoto/Particles/CElementGen.cpp > particle-capture.json
mwcc-tools provenance inspect particle-capture.json --link RenderParticles__11CElementGenFv --json > particle-coalescing.json
```

The format-2 result records actual deciding branches and confirmed union-parent
writes. It explains interference, window bounds, flags, kind/class filtering,
already-equal roots and the GPR special-register guard. `decisions` reports capture
coverage. Its `history` includes direct counter births, resets, rewinds,
capture-local value IDs/generations and available partial statement-root origins.
`history.counter_observations` covers that direct-counter scope; aggregate
`lifecycle` remains indeterminate while semantic categories, complete origins and
semantic lifetime correspondence are unfinished. `graphs.graphs` captures actual
instructions and graph membership immediately before simplification, including
`graph_only`, stored/raw adjacency evidence, candidate birth bindings and confirmed
merge contributors. Read `graphs.observations` and each node's `binding.evidence`
separately: snapshot coverage does not prove a unique lifetime when register
numbers were reused. Function-end graph counts detect missing snapshots.
Observation IDs are not cross-variant
semantic identities. Missing records do not
prove elimination. Consume the structured decisions and coverage rather than
extracting explanations from report text.

`history.functions[].births[].object` now supplies observed single/pair object
bindings, type/storage information and byte-preserving names at the audited sites.
Require known, complete names and a unique relevant binding before using one as a
source anchor. `graphs.graphs[].nodes[].range_category` describes the actual retail
allocation range, with evidence backed by `range_bounds` and per-function range
markers. Range categories distinguish initial objects, values before/inside/after
the coalescing window and physical registers; they do not identify an address or
call-result temporary by themselves. Some paths, including the `-O0` reuse fixture,
omit range markers and remain indeterminate. Check range and value-binding evidence
separately. Rebuild both tools and common hooks for the current internal callback ABI.

`mwcc-tools provenance audit-counters --root ORACLE_ROOT --version GC_2_7` emits a
native audit of the pinned image's actual counter references and remaining gaps.

Current development producers also observe expression-generation dispatch. Use
`provenance audit-dispatch --root ORACLE_ROOT --version GC_2_7` for the native
initializer/target audit. Rebuild common hooks and the producer for its versioned
observer. Consume `expressions[].parent`, `dispatch`, `result`, `end_position` and
`abandoned` with per-function `dispatch_scope`, totals and `dispatch_observations`.
Births can now point to nested `codegen_dispatch` contexts. Raw requested argument
bits can contain unused upper-half pointer bits; they are not register identities.
Tagged result evidence is separate from dispatch capture and complete lifetime
evidence. Caller statement lines do not supply complete inline ancestry, and direct
helper calls can bypass the table.

The full RenderParticles EnvModulate case now ties symbol returns to later address
births, symbolic PCode and confirmed r4 merges. The merged temporaries remain
invisible graph nodes adjacent to noRota and the zero-vector address. Three tested
reference/pointer forwarders produce the same complete graph and retain the
99.94901% mismatch. See `docs/oracle-source-guided-case-envmodulate.md` in the current
source checkout for the source pin, evidence and remaining materializer-binding gap.

## Search source variants against a graph objective (development)

Build current native tools and the current Rust oracle producer. This command is
newer than the pinned migration kit executable:

```text
mwcc-tools source-search run plan.json --output particle-search --candidates 100 --compilations 100 --seconds 600
mwcc-tools source-search run plan.json --output particle-search --resume
mwcc-tools source-search replay particle-search 1
```

Use the strict format-1 plan from `docs/oracle/source-search.md` in the oracle
source checkout. Its `build` names the oracle root, working directory, source,
compiler version, timeout and either explicit ordered flags or a Ninja file.
For a game edge, use `from_ninja`, its source spelling and `flags: []`; the edge
supplies the real version/flags. `contract` selects an exact link, register class
and zero-based class round, a unique observed object name/type/part, an objective,
required symbolic relocations and a purpose. `transformations` records identified
families, preconditions and atomic exact-anchor edits. Depth and generation limits
bound exploration. Use these structured inputs rather than wrapping report text.

Available predicates are `direct_initialization`, `change_range_category` and
`eliminate_temporary`, plus the scoped `symbol_address_pairs` count. Direct initialization checks a constant LI and removal of
the intermediate copy through final allocation. Category changes must preserve
normalized instructions and relocations. Elimination currently stays indeterminate:
an unmatched graph node is insufficient evidence. Range categories describe
allocator number ranges, not semantic source roles.

`symbol_address_pairs` selects a qualified mangled `symbol`, `minimum` and
`maximum`, with class 4 (GPR). It counts observed high/low pairs from the guarded
Normalize path before the selected graph. It requires complete relevant capture,
symbol identities, adjacent pair births and unique graph bindings. Its report
includes the source Operand, selected births, surviving graph nodes and confirmed
merges. Other materialization paths and optimizer-created values remain outside
this count, so it is not complete temporary-elimination proof. Retain required
symbolic relocation kinds with minimum counts of one; do not accidentally require
all repeated address pairs to remain. Current producers use the guarded V3 counter
descriptor and must be rebuilt with current common hooks.

Require `purpose: "source_transformation"` and configure a semantic-check executable
with arguments using `{source}`, `{object}`, `{baseline_source}` and
`{baseline_object}`. Declare its script/test-data dependencies in `inputs` and a
positive `timeout_seconds`. It must actually check the required behavior and exit
nonzero on failure. Both baseline and candidate are checked. Stock compiler object
equality checks the instrumentation, not semantic equivalence between variants.
`mechanism_reproducer` permits unchecked examples, explicitly labeled as such.

Read `status`, `evaluation.overall`, `constraints`, `score`, `graph_changes` and
`independent_verification`. A zero command exit or improved final mismatch alone
does not establish success. Known objective costs rank eligible candidates;
indeterminate evidence never wins. A successful candidate is freshly compiled and
rechecked before `verified_success`. Optional target-object mismatch is separate.
An unavailable fingerprint has `fingerprint_error`; keep that source candidate
and its evaluation, without claiming that it joined an equivalent-state group.

`progress.json` saves the queue, evaluations and every source representative of
each relevant compiler state. Exact compilation reuse separately hashes both
compiler input scans, raw dependencies, runtime files, flags and environment.
Header edits invalidate it even if preprocessing produces identical code. Sources,
objects, traces, dependency/runtime bytes and independent verification artifacts
remain in the run directory. Do not modify these saved inputs. Remove the default
`particle-search/CANCEL` file before resuming a cancelled run. Changed source/build
context requires a new output directory; replay requires matching dependencies.

These APIs are under development. The reducer, matched-source index and a useful
full RenderParticles source transformation are not complete. A synthetic successful
initializer experiment deliberately changes behavior and is only a mechanism test.

## Calibrate inline decisions

```text
mwcc-tools inline-calibrate --project . --callee destroy_impl --unit main/Kyoto/Particles/CElementGen --breakdown --json calibration.json
```

Compiler/game versions are inferred from the unit/build unless explicitly
overridden. Thresholds come from individual decisions. `--binary` compares
decisions against whole-binary symbols; repeated units and header overrides
support experiments across affected units. Missing cost data is an error.

Python helper imports are not an API. Optional old-name launchers may remain
for interactive convenience, but new kit automation and tests should invoke
the native commands or Rust library and consume structured results.
