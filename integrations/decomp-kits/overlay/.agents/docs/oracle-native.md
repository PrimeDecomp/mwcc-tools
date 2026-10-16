# Native oracle workflow

Use `mwcc-tools` directly. Install the native executable on PATH and set
`MWCC_ORACLE_ROOT` to the checkout containing the built oracle compilers.
Prime game code uses `GC_1_3_2`; Echoes game code uses `GC_2_7`, with other
versions selected per library. Let the unit's Ninja edge select its compiler
and flags. Run these examples from the game checkout, or replace `--project .`
with its path. Neither candidates nor header overrides require source swaps.

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
