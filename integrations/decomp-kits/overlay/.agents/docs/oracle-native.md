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

The format-1 result records actual deciding branches and confirmed union-parent
writes. It explains interference, window bounds, flags, kind/class filtering,
already-equal roots and the GPR special-register guard. `decisions` reports capture
coverage; `lifecycle` remains indeterminate until creation/reuse/category coverage
is implemented. Observation IDs are not stable value IDs. Missing records do not
prove elimination. Consume the structured decisions and coverage rather than
extracting explanations from report text.

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
