# Evidence-aware source reduction

`mwcc-tools source-reduce` shrinks a translation unit while retaining a selected
compiler mechanism. It uses the same Rust objective evaluator, stock-equivalence
checks, symbolic-relocation constraints and exact-input compilation cache as
[`source-search`](source-search.md). The output is a compiler-behavior reproducer;
it is not automatically a source change suitable for transplantation into a game.

```text
mwcc-tools source-reduce run reduction.json --output scratch/reduction --candidates 100 --compilations 100 --seconds 600
mwcc-tools source-reduce run reduction.json --output scratch/reduction --resume
mwcc-tools source-reduce replay scratch/reduction 1
```

The native API is `source_guided::reduction::{load_plan, run, replay}`. `run` uses
`search::Budget`; cancellation is a file, by default `OUTPUT/CANCEL`. Budgets apply
per invocation, including baseline setup and independent verification. Cancellation
terminates compiler/checker process trees. A candidate awaiting its independent
compile remains pending across resume and is not recorded as accepted.

## Plan format 1

The build, contract, semantic checker and optional target have the same structure
as source search. Replace search generation bounds and transformations with
`reductions` and `delete_lines`:

```json
{
  "format_version": 1,
  "build": {
    "oracle_root": "/absolute/oracle",
    "working_directory": "/absolute/reproducer",
    "source": "case.cpp",
    "version": "GC_2_7",
    "flags": ["-nodefaults", "-proc", "gekko", "-fp", "hardware", "-O4,p"],
    "from_ninja": null,
    "timeout_seconds": 60
  },
  "contract": {
    "format_version": 1,
    "selection": { "link": "symbolic_address__FUi", "class": 4, "round": 0 },
    "objective": {
      "kind": "symbol_address_pairs",
      "symbol": "symbol__8Graphics",
      "minimum": 1,
      "maximum": 10
    },
    "relocations": [
      { "symbol": "symbol__8Graphics", "kind": null, "addend": 0, "minimum": 1 }
    ],
    "purpose": "mechanism_reproducer"
  },
  "reductions": [
    {
      "id": "remove-unused-helper",
      "family": "helper",
      "precondition": "Exact helper anchor; the retained mechanism is checked after deletion.",
      "edits": [
        { "find": "static int unused() { return 1; }\n", "replacement": "" }
      ]
    }
  ],
  "delete_lines": true,
  "semantic_check": null,
  "target": null
}
```

Backend/stage/local/CSE traces default to enabled. Unknown fields and duplicate
recipe IDs are rejected. The original source is read, never edited. All proposed
sources are written under `OUTPUT/sources/HASH/ORIGINAL_FILENAME`.

## Reduction order and evidence

Each round tries guarded recipes in plan order. Anchors must occur exactly once,
edits cannot overlap, and the resulting byte count must decrease. Recipes can
remove declarations, helpers or included dependencies, or simplify expressions and
statements. There is no C++ parser or automatic semantic rewrite generator.

When `delete_lines` is enabled, the reducer next tries contiguous line deletion
from coarse blocks down to individual lines. It retains original bytes outside
the deleted span, including source encodings and line endings. Invalid C++ is a
normal rejection. After accepting a reduction it restarts all passes on the new
case. The final result is a fixed point of these configured passes, not a proof
of a globally minimal program. Disabling line deletion gives a bounded sequence
of explicitly supplied structural reductions.

The baseline must satisfy the predicate before any shrinking starts. Every
candidate rebinds the selected named object or symbolic materialization through
its newly captured provenance; raw register numbers and final physical registers
are not supplied as preservation criteria. The configured function/class/round
must continue to exist. Ambiguous bindings, absent graphs, incomplete relevant
coverage and indeterminate results are rejected. `symbol_address_pairs` preserves
the guarded Normalize creation mechanism within the requested count bounds. It
does not establish absence of all address temporaries or semantic elimination.

Direct-initialization objectives are also usable. Prefer `source_line: null` when
the complete named-object/type/part binding identifies the initializer: deleting
earlier lines changes numeric source locations. A specified line remains a hard
selector and is never silently rewritten. `change_range_category` is a
transition between two cases and is currently rejected by reduction plans: a
separate reference capture is needed to preserve that transition while shrinking
the candidate. `eliminate_temporary` remains indeterminate and cannot produce an
accepted reduction. Promoted-array/coalescing predicates and stronger lifetime
bindings remain shared-evaluator work, not capabilities implied by this reducer.

A satisfied candidate receives a fresh independent compilation. Its build-input
snapshot and object hash must reproduce, and the shared predicate and configured
semantic checks must pass again. Only then does it replace the best case. Final
target mismatch is reported separately and does not decide acceptance.

For `mechanism_reproducer`, behavior changes are allowed and the result explicitly
reports `transplantation_validated: false`. For `source_transformation`, a successful
declared semantic checker is required for both baseline and candidates. A checker
only establishes its declared coverage; it does not turn arbitrary statement
deletion into a general equivalence proof.

## Retained evidence and replay

`progress.json` is an atomic checkpoint of the best case, pass cursor, pending
candidate, completed outcomes and cumulative compilation/cache counts. Each
`candidates/ID.json` includes its parent, recipe, source hash, evaluation and
independent verification. `result.json` points to the final source and artifact.
Raw input/runtime blobs, preprocessing/dependency scans, object files and traces
are retained in the same layout as source search. Cache reuse requires the exact
compiler inputs, not merely an equal graph. Candidate-only dependencies of the
accepted best case are rescanned on resume.

Terminal statuses are `verified_reduction`, `no_reduction` and
`baseline_not_satisfied`. `candidate_budget`, `compilation_budget`, `time_budget`
and `cancelled` can be resumed. Infrastructure errors are recorded as `failed`;
candidate compilation/evaluation errors are retained as rejected outcomes.

Resume requires the same normalized plan, original source, baseline inputs,
analyzer, checker dependencies and target. Replay validates the retained blobs and
current dependencies, performs three fresh builds (private baseline, original
context and selected candidate), recalibrates the original context and reruns the
predicate. It checks the candidate object against the retained hash and writes a
new replay outcome without modifying the earlier result. Replay needs the original
build environment; portable project export is not implemented.

## Retained real-case proof

The Windows native CLI reduced the checked address-reuse variant from the
[RenderParticles investigation](../oracle-source-guided-case-envmodulate.md).
A guarded recipe removed the unrelated 222-line constructor, reducing the
translation unit from 117,085 to 110,801 bytes. The scoped predicate retained
exactly two `kEnvModulate__9CGraphics` Normalize pairs in `RenderParticles` and
required both symbolic HA/LO relocation kinds. All shared hard gates passed;
four compilations included original-context calibration and a fresh independent
candidate build. The candidate and independent build produced the same complete
stock object, SHA-256
`6859b26d4200a5a4962ff120f1a1214bb9d3434193f9e74d3bd40a62d944b2f6`.

Inputs, the frozen analyzer, native checkpoint/outcomes and verification summary
are retained under `build/source-guided/reduction-probes/particle-windows/`.
This is a smaller compiler-behavior case, not a globally minimal unit or a
behavior-preserving constructor deletion. Its selected function remains 6,672
bytes against the 6,668-byte retail function; graph-predicate success is separate
from final code matching. No game source was changed.

The opt-in `source_reduction_live` test additionally starts from the standalone
symbolic const-reference fixture. It exercises an explicit helper deletion,
automatic line shrinking, exact cache reuse, pending independent verification
across resume, three fresh replay builds, candidate-only header drift rejection,
missing semantic evidence and cancellation. The automatic pass can reduce the
534-byte fixture to a 281-byte single-call reproducer while retaining an observed
symbolic address pair and its relocation. It does not preserve a particular
physical register.

This live test passes on Windows and Linux for GC 1.3, 1.3.2 and 2.7. Each
automatic run evaluates 59 candidates with 60 compilations, reaching the same
281-byte source (SHA-256
`a05c9cc59061ab4b3995f17761a206ea38bf773e164709b2d002e973686f6924`).
Final Windows evidence is under
`build/source-guided/tool-integration/source-reduction-8oyCev/`; final Linux
evidence is under
`/home/henrique/.cache/mwcc-source-reduction-proofs/final/source-reduction-atpXSV/`.
The Linux aggregate is also retained at
`build/source-guided/reduction-probes/linux-final-verification.json`.
