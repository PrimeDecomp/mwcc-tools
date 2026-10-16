# Native oracle workflow

Use `mwcc-tools` directly to inspect compiler decisions, compare registers and
test allocation graphs. New automation should consume JSON results rather than
parse report prose or import Python helpers.

## Install

Run the kit's `install-native.py PROJECT --apply` and add the installed executable's
directory to PATH. Set `MWCC_ORACLE_ROOT` to the checkout containing the built
oracle compilers. `native-manifest.json` records the kit's source pin and local
source bundle; `EXTERNALS.md` covers the build requirements.

To build an oracle compiler in that checkout:

```text
mwcc-tools configure --root ORACLE_ROOT --version VERSION --oracle
ninja -C ORACLE_ROOT
```

Run unit commands from the game checkout, or pass its path through `--project`.
The unit's Ninja edge supplies the compiler version and flags. Prime game code
usually uses `GC_1_3_2`; Echoes uses `GC_2_7`, with versions varying by library.

## Inspect a unit

```text
mwcc-tools oracle run-unit --project . --unit main/Kyoto/Particles/CElementGen -- --locals --schedule
mwcc-tools oracle regdiff --project . --unit main/Kyoto/Particles/CElementGen --function RenderParticles --pcode
mwcc-tools oracle regdiff --project . --unit main/Kyoto/Particles/CElementGen --function RenderParticles --json > regdiff.json
```

For Prime, try unit `main/GuiSys/CGuiFrame` and function `Draw`. Add
`--candidate candidate.cpp` before the `run-unit` separator, or alongside the
`regdiff` unit options, to compile a private candidate. The original source
directory remains on the include path; project files are not swapped.

`run-unit` checks the complete object against the stock compiler by default.
`--fast` requests an unchecked run. Regdiff compares against the saved project
object and can report that it is stale; it does not rebuild the game checkout.
`OBJDIFF_CLI` selects the objdiff executable.

## Capture structured results

```text
mwcc-tools oracle compile-trace --from-ninja build.ninja --locals --schedule src/Kyoto/Particles/CElementGen.cpp > compilation.json
```

Read `format_version`, `compilation` and `report`. Require
`compilation.check_status.status == "verified"` when stock-object equality is
needed. `mismatch`, `skipped`, `original_unavailable` and `original_failed` do not
establish equality; a successful process exit alone is insufficient.
`compilation.trace` retains the raw trace. Validate supported format/schema
versions before consuming either JSON API.

For source or header experiments, add `--override PATH=REPLACEMENT`. Use the
source spelling from the Ninja edge for a source override. Replacements compile
privately while preserving include order.

Regdiff JSON exposes `differences`, `explanations`, `analysis` and `compilation`.
Its `compilation.project_object_identical` describes the saved project object,
separately from stock-compiler equality. The relocation comparison policy is
objdiff's `functionRelocDiffs=data_value`.

## Replay and search allocation graphs

```text
mwcc-tools colouring unit --project . --unit main/Kyoto/Particles/CElementGen verify --function RenderParticles__11CElementGenFv --save particle-baseline --json
mwcc-tools colouring unit --project . --unit main/Kyoto/Particles/CElementGen search --function RenderParticles__11CElementGenFv --load particle-baseline --seed 2 --json
```

Verify replay first, and use exact link names to distinguish overloads. Other
commands are `target`, `force`, `replay` and `remap`; repeated `--load` options
let `remap` compare saved states. Sessions retain traces, objects and provenance.
Legacy `.trace`/`.o` pairs can also be loaded with the unit's compiler version.
The what-if model does not support spills. A useful graph edit still needs a C++
transformation that produces it.

## Calibrate inline decisions

```text
mwcc-tools inline-calibrate --project . --callee destroy_impl --unit main/Kyoto/Particles/CElementGen --breakdown --json calibration.json
```

Versions are inferred from the unit/build unless overridden. `--breakdown`
explains individual decision costs; `--binary` compares against whole-binary
symbols. Repeated units and header overrides support experiments across affected
units. Missing cost data is an error.

## Development workflows

The commands below require current tools and a matching freshly built Rust
producer. They are newer than the pinned migration-kit executable; consult
`native-manifest.json` before assuming a shipped kit includes them. Reference
paths below are relative to the **oracle source checkout**; they are not bundled
with the installed kit guide.

| Task | Native command | Source-checkout reference |
| --- | --- | --- |
| Explain observed backend births and coalescing | `provenance inspect CAPTURE --link NAME --json` | `docs/oracle/provenance.md` |
| Find C++ edits that meet a graph objective | `source-search run plan.json --output search` | `docs/oracle/source-search.md` |
| Shrink a case while retaining a mechanism | `source-reduce run reduction.json --output reduced` | `docs/oracle/source-reduce.md` |
| Export and freshly replay retained inputs | `source-repro export search RUN ID --output repro --run-analyzer ORIGINAL_ANALYZER` | `docs/oracle/source-repro.md` |
| Find matching mechanisms in existing source | `source-index query query.json --index corpus` | `docs/oracle/source-index.md` |
| Inspect guarded historical allocations | `source-arenas inspect CAPTURE --json` | `docs/oracle/source-arenas.md` |

Capture backend evidence with `oracle compile-trace --backend`; add
`--source-arenas` for guarded allocation/reset observations. Specialized field
contracts and coverage limits are documented in
`docs/oracle/scalarization.md`, `docs/oracle/call-arguments.md` and
`docs/oracle/source-lifetimes.md`.

Search and reduction plans specify the objective, required symbolic relocations
and semantic checker. Stock equality validates instrumentation; it does not
establish equivalent behavior between source variants. A `mechanism_reproducer`
may deliberately change behavior. Indeterminate evidence cannot establish a
successful transformation; read the evaluation and independent verification.
Retained-input inspection does not replace fresh replay.

Missing observations do not prove elimination, and pointer or register equality
does not establish source ancestry. Start with the documented coverage for the
mechanism being investigated, then test useful discoveries in the full function.
