# Reduce a compiler-behavior reproducer

The `source_reduce.*` methods remove source while preserving a selected compiler
objective. Use it to turn a large case into a smaller example of a graph change,
allocation category or coalescing decision. It shares the evaluator, stock checks,
relocation constraints and exact-input cache with [source-search](source-search.md).

```sh
mwcc-tools call source_reduce.run plan=reduction.json output=scratch/reduction candidates=100 compilations=100 \
    seconds=600
mwcc-tools call source_reduce.run plan=reduction.json output=scratch/reduction resume=true
mwcc-tools call source_reduce.replay output=scratch/reduction id=1
```

The baseline must satisfy the predicate. Every accepted reduction must be smaller,
pass all evidence gates and reproduce in a fresh independent compile. The tool
never edits the original source. A mechanism reproducer may change behavior;
use `purpose: "source_transformation"` and a declared semantic checker when
behavior must be preserved.

## Plan format 1

The `build`, `contract`, checker and optional target use the
[search plan contract](source-search.md#plan-format-1). Replace search generation
with `reductions` and `delete_lines`:

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
    "selection": {"link": "symbolic_address__FUi", "class": 4, "round": 0},
    "objective": {
      "kind": "symbol_address_pairs",
      "symbol": "symbol__8Graphics",
      "minimum": 1,
      "maximum": 10
    },
    "relocations": [
      {"symbol": "symbol__8Graphics", "kind": null, "addend": 0, "minimum": 1}
    ],
    "purpose": "mechanism_reproducer"
  },
  "reductions": [{
    "id": "remove-unused-helper",
    "family": "helper",
    "precondition": "Exact helper anchor; the retained mechanism is checked after deletion.",
    "edits": [{"find": "static int unused() { return 1; }\n", "replacement": ""}]
  }],
  "delete_lines": true,
  "semantic_check": null,
  "target": null
}
```

Backend/stage/local/CSE traces default on. Unknown fields and duplicate recipe IDs
are rejected. Candidate bytes are stored at
`OUTPUT/sources/HASH/ORIGINAL_FILENAME`, preserving bytes outside each edit.

## What is preserved

The function/class/round must still exist, and each candidate must rebind the
objective through its new provenance. Raw register numbers and final physical
registers are not preservation criteria. Missing graphs, ambiguous bindings,
incomplete coverage and indeterminate predicates are rejected. Final target
mismatch is reported separately from reduction acceptance.

Use static [search objectives](source-search.md#objectives-and-validity):

- `symbol_address_pairs` or `call_argument_address_pairs` retains the requested
  guarded Normalize count and required relocations. It does not prove absence
  of all address temporaries.
- `range_category` preserves an observed category through a unique named/type/
  part anchor and confirmed merge roots. It does not require unchanged
  instructions or physical registers, and does not identify a source role.
- `promoted_copy_coalescing` retains an actual copy outcome for an element. For
  example, use `value: {"name":"flag","element_index":1,"element_size":4}`,
  `copy: 0` and
  `outcome: {"kind":"rejected","condition":"destination_above_window"}`.
  `{"kind":"coalesced"}` retains a merge or already-shared-root removal.
  Complete scalar GPR/FPR promotion, birth/node/generation and matching-pass
  evidence are required. Zero bound copies remain indeterminate; the ordinal
  does not prove source initialization or dominance.
- `direct_initialization`, `scalarized_array_initialization` and
  `scalarized_array_definition` retain their supported observed definitions.
  Initializer recognition requires one instruction-bearing block with verified
  linear scaffolding. Prefer `source_line: null`: deleting earlier lines changes
  line numbers, and a specified line is never silently rewritten.

`change_range_category` needs a separate baseline transition and is rejected by
reduction plans. General `eliminate_temporary` remains indeterminate. Allocation
categories and promotion observations cannot supply missing semantic lifetime
or VMX/source-initializer evidence.

## Reduction order and results

Each round tries recipes in plan order. Every anchor must occur once, edits must
not overlap and byte count must decrease. With `delete_lines: true`, contiguous
line deletion then proceeds from coarse blocks to single lines. Invalid C++ is a
normal rejection. An accepted reduction restarts the passes on the smaller case.
The result is a fixed point of the configured passes, not a globally minimal
program. There is no C++ parser or automatic semantic rewrite generator.

A satisfied candidate must reproduce its input snapshot, object hash, predicate
and configured semantic checks in a fresh build before replacing the best case.
For `mechanism_reproducer`, results report `transplantation_validated: false`.
For `source_transformation`, baseline and candidates need successful checker
results; the checker establishes only its declared coverage.

`progress.json` stores the best case, pass cursor, pending candidate and counts.
`candidates/ID.json` retains each parent, recipe, hash, evaluation and verification;
`result.json` points to the final source/artifact. Input blobs, scans, captures and
objects use the search layout. Cache reuse requires exact compiler inputs.

Terminal statuses are `verified_reduction`, `no_reduction` and
`baseline_not_satisfied`. `candidate_budget`, `compilation_budget`, `time_budget`
and `cancelled` can resume. Infrastructure errors are `failed`; candidate
build/evaluation errors are rejected outcomes.
Budgets include baseline setup and verification. Create `OUTPUT/CANCEL` to stop;
active compiler/checker process trees are terminated. A pending independent build
is retried on resume rather than accepted early.

Resume requires unchanged plan, original source, baseline/candidate dependencies,
analyzer, checker inputs and target. Replay checks retained integrity and current
inputs, then makes three fresh builds for original context, private baseline and
candidate, recalibrates and reruns the predicate. It checks the retained candidate
object hash and saves a new outcome without changing the old result. For bounded
relocation away from the original checkout, use [source-repro](source-repro.md).

Rust APIs: `source_guided::reduction::{load_plan,run,replay}`, with
`search::Budget`. The [implementation status](../oracle-source-guided-status.md)
retains real RenderParticles and standalone all-version/all-host reduction
evidence, including promoted-copy rejection cases.
