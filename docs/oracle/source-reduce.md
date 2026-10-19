# Reduce a compiler-behavior reproducer

The `source_reduce.*` methods remove source while preserving a selected compiler objective: a large case becomes a
smaller example of a graph change, allocation category or coalescing decision. They share the evaluator, stock
checks, relocation constraints and exact-input cache with [source search](source-search.md).

```sh
mwcc-tools call source_reduce.run plan=reduction.json output=scratch/reduction candidates=100 compilations=100 \
    seconds=600
mwcc-tools call source_reduce.run plan=reduction.json output=scratch/reduction resume=true
mwcc-tools call source_reduce.replay output=scratch/reduction id=1
```

The baseline must satisfy the predicate. Every accepted reduction is smaller, passes all gates and reproduces in a
fresh compile. The original source is never edited. A `mechanism_reproducer` may change behavior; use
`purpose: "source_transformation"` with a semantic checker when behavior must be preserved.

Rust: `source_guided::reduction::{load_plan, run, replay}` with `search::Budget`.

## Plan format 1

`build`, `contract`, `semantic_check` and `target` are as in the [search plan](source-search.md#plan-format-1);
`reductions` and `delete_lines` replace the search's generation fields:

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
    "objective": {"kind": "symbol_address_pairs", "symbol": "symbol__8Graphics", "minimum": 1, "maximum": 10},
    "relocations": [{"symbol": "symbol__8Graphics", "kind": null, "addend": 0, "minimum": 1}],
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

Unknown fields and duplicate recipe IDs are rejected. Candidates are stored at
`OUTPUT/sources/HASH/ORIGINAL_FILENAME`.

## What is preserved

The function/class/round must still exist, and each candidate rebinds the objective through its own provenance.
Raw and final physical register numbers are not criteria. Missing graphs, ambiguous bindings, incomplete coverage
and indeterminate predicates are rejected. A final target mismatch is reported separately.

All static [objectives](source-search.md#objectives-and-validity) apply; `change_range_category` (a baseline
transition) is rejected. For the initializer objectives prefer `source_line: null`: deleting lines renumbers them,
and a given line is never rewritten. For example, `promoted_copy_coalescing` with
`value: {"name":"flag","element_index":1,"element_size":4}`, `copy: 0` and
`outcome: {"kind":"rejected","condition":"destination_above_window"}` keeps that rejection.

## Order and results

Each round tries the recipes in plan order (every anchor once, no overlapping edits, fewer bytes). With
`delete_lines: true`, contiguous line deletion then goes from coarse blocks to single lines. Invalid C++ is a normal
rejection. An accepted reduction restarts the passes. The result is a fixed point of the configured passes, not a
minimal program; there is no C++ parser.

A satisfied candidate must reproduce its input snapshot, object hash, predicate and semantic checks in a fresh
build before it replaces the best case. `mechanism_reproducer` results report `transplantation_validated: false`.

- `progress.json` holds the best case, pass cursor, pending candidate and counts; `candidates/ID.json` each parent,
  recipe, hash, evaluation and verification; `result.json` points to the final source and artifact.
- Statuses: `verified_reduction`, `no_reduction`, `baseline_not_satisfied`; resumable `candidate_budget`,
  `compilation_budget`, `time_budget`, `cancelled`; `failed` for infrastructure errors (candidate build errors are
  rejections).
- Budgets include baseline setup and verification. `OUTPUT/CANCEL` stops the run and its process trees.
- Resume needs an unchanged plan, source, dependencies, analyzer, checker inputs and target. Replay makes three
  fresh builds (original context, private baseline, candidate), recalibrates, reruns the predicate and saves a new
  outcome. For relocation away from the checkout, use [source-repro](source-repro.md).
