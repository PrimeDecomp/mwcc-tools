# Search source variants by compiler behavior

The `source_search.*` methods apply guarded source edits, compile each variant and score an observed compiler
objective: graph structure, allocation categories, initialization or symbolic-address creation. They need an oracle
with the Rust producer ([producer.md](producer.md)). With GC 2.6/2.7 in process, the expression, dispatch,
coalescing and call-argument context streams are absent or partial ([wasm.md](../wasm.md#probes)); use
`MWCC_IN_PROCESS=0` for objectives that need them.

```sh
mwcc-tools call source_search.run plan=plan.json output=search-run candidates=100 compilations=100 seconds=600
mwcc-tools call source_search.run plan=plan.json output=search-run resume=true compilations=100
mwcc-tools call source_search.replay output=search-run id=1
```

`verified_success` means a non-baseline candidate passed the objective, every hard constraint and a fresh
independent compile. `baseline_already_satisfied` stops a search whose baseline meets the objective. Check `status`,
`evaluation.overall`, `constraints` and `independent_verification`: a call without an error is not success.

Rust (`source_guided`): `objective::{Contract, evaluate}`, `state::{graph, snapshot, fingerprint, inspect}`,
`inputs::capture`, `search::{load_plan, run, replay}`.

## Plan format 1

This example edits `tests/fixtures/source-guided/initializer-linear.c` (replace the absolute paths). The edit
changes behavior, so it is a mechanism reproducer:

```json
{
  "format_version": 1,
  "build": {
    "oracle_root": "F:/path/to/current/oracle",
    "working_directory": "F:/path/to/mwcc/tests/fixtures/source-guided",
    "source": "initializer-linear.c",
    "version": "GC_2_7",
    "flags": ["-nodefaults", "-proc", "gekko", "-fp", "hardware", "-O4,p"],
    "from_ninja": null,
    "timeout_seconds": 60
  },
  "contract": {
    "format_version": 1,
    "selection": {"link": "source_initializer", "class": 4, "round": 0},
    "objective": {
      "kind": "direct_initialization",
      "value": {"name": "flag", "part": "single", "type_kind": 1, "type_size": 4},
      "constant": 0,
      "source_line": null
    },
    "relocations": [{"symbol": "observe", "kind": null, "addend": null, "minimum": 1}],
    "purpose": "mechanism_reproducer"
  },
  "transformations": [{
    "id": "constant-initializer",
    "family": "initialization",
    "precondition": "Isolated mechanism experiment; the initializer edit changes behavior.",
    "edits": [{"find": "unsigned flag = input;", "replacement": "unsigned flag = 0;"}]
  }],
  "maximum_depth": 1,
  "generation_limit": 10,
  "semantic_check": null,
  "target": null
}
```

- Manifest paths are relative to the plan; `source` is relative to the working directory. For a game unit set
  `from_ninja` to `build.ninja`, use that edge's source spelling and `flags: []`: the edge supplies the directory,
  flags and version (`version` null or agreeing; `working_directory` must exist).
- Backend, stage, local and CSE traces default on; an explicit `traces` (`runner::TraceOptions`) must keep
  `backend: true`.
- A transformation applies all its anchors atomically; each must occur exactly once, without overlap, and other
  bytes are preserved. `precondition` is the author's justification, not a proof.
- Breadth-first generation uses at most one recipe per family per path, bounded by `maximum_depth` (at most 16)
  and `generation_limit` (including the baseline). There is no automatic C++ rewriting.

## Objectives and validity

`selection` names an exact link, register class (`0` SPR, `1` CR, `2` VR, `3` FPR, `4` GPR) and zero-based round.
Named-value anchors need a unique, complete name/type/part binding before that graph; `part` is `single`,
`first_word` or `second_word`. Missing or ambiguous evidence is **indeterminate**, not a failure or proof of absence.

| Objective | Fields | Requires / limits |
| --- | --- | --- |
| `direct_initialization` | `value`, `constant`, `source_line?` | The first definition is a direct LI, or one local LI/MR pair, initializing the final register without a surviving copy. VR/FPR/GPR, one instruction-bearing block with linear empty scaffolding, no earlier barrier or use. No cross-block dominance. |
| `range_category` | `value`, `target` | The allocator range (`physical`, `initial_object_range`, `before/inside/after_coalescing_window`) through births and confirmed merge roots. Not a source role. |
| `change_range_category` | same | A change from the baseline, keeping boundary PCode and final instruction/relocation structure. Search only. |
| `promoted_copy_coalescing` | `value` (`name`, `element_index`, `element_size`), `copy`, `outcome` | The incoming-copy ordinal in the coalescer pass and its outcome, `{"kind":"coalesced"}` (a merge or shared root) or e.g. `{"kind":"rejected","condition":"destination_above_window"}`. Scalar GPR/FPR promotion, not VMX. Zero copies is indeterminate; an out-of-range ordinal after a bound copy is unsatisfied. |
| `scalarized_array_initialization` | element `value`, `constant`, `source_line?` | A whole-element scalar/birth join ([scalarization](scalarization.md)) and the LI recognizer; GPR only. |
| `scalarized_array_definition` | element `value`, `definition` | An ordinal in the `ra.before` definitions whose producer survives to the final allocation with the recorded destination; MR/FMR needs a unique same-block producer and actual coalescing. FPR/GPR. |
| `symbol_address_pairs` | `symbol`, `minimum`, `maximum` | Adjacent high/low births on the guarded Normalize path before the graph (GPR). Not every address temporary. |
| `call_argument_address_pairs` | adds `callee`, `call`, `ordinal` | Both births inside that argument ([call arguments](call-arguments.md)). |
| `eliminate_temporary` | `value` | Always indeterminate: there is no semantic lifetime evidence. |

Ordinals are compiler record order, not C++ initialization, dominance or parameter order.

Every objective also needs consistent source/object hashes, a supported compiler and flags, stock-equivalent oracle
output, provenance bound to its trace, complete graph coverage for the function and the requested relocations.
Relocations count within the function; a null `kind` or `addend` accepts any value. For a zero-count address
search, require the symbol's relocations so an unrelated function cannot pass vacuously (GC 2.7 EnvModulate: kind 6
HA and kind 4 LO, addend 0, minimum 1 each).

With `purpose: "source_transformation"`, give a checker:

```json
{
  "program": "F:/checks/validate-particle-candidate.exe",
  "arguments": ["{baseline_source}", "{baseline_object}", "{source}", "{object}"],
  "inputs": ["F:/checks/particle-cases.json"],
  "timeout_seconds": 60
}
```

It runs from the working directory; baseline and candidate must both exit zero in time. Declare its scripts,
modules and data in `inputs` (existing file arguments are hashed too). Stock equivalence compares oracle and stock
compiles of the **same source**; it does not replace a semantic check.

`target: {"object":"target.o","link":"exact_target_link"}` reports the final byte and relocation match separately.
A graph objective can worsen the final match, as in the
[EnvModulate case](../oracle-source-guided-case-envmodulate.md).

## Scores and state grouping

`score.eligible` requires every hard gate. Pair-count objectives cost their distance from the interval; others 0
when satisfied, 1 when not; indeterminate is null. Eligible candidates rank first, then known costs before
unknown (`Score::compare_priority`, ties by candidate ID). Edge changes come from
[graph correspondence](graph-correspondence.md).

Fingerprint format 4 groups supported observed compiler states: graph-only nodes and degrees, births and ranges,
relevant PCode/CFG and instruction identities, coalescing decisions, scalarization and argument observations,
symbolic relocations and allocated data, with pointers and registers normalized. Unsupported forms give
`fingerprint_error` and disable grouping without dropping the candidate. Grouping is conservative and can miss
duplicates.

Inspect retained evidence without compiling:

```sh
mwcc-tools call source_search.state capture=capture.json object=candidate.o link=RenderParticles__11CElementGenFv \
    class=4 round=0
mwcc-tools call source_search.evaluate contract=contract.json capture=capture.json source=candidate.cpp \
    object=candidate.o
```

`state` needs an envelope with `compilation` and returns a fingerprint or indeterminate normalization; `evaluate`
also takes a plain `Compilation`, and all three `baseline_*` parameters for a transition. Neither rescans
dependencies or runs a checker. Fixed GQR operands are supported only for PSQ_L/PSQ_ST (403/407) with all 12 bytes
and selectors 912–919 exact.

## Cache, budgets and replay

- **Cache.** Reuse needs fresh dependency/preprocessing scans with both compilers. Keys include preprocessed hashes,
  dependencies, source, flags, working directory, trace options, runtime hashes and an environment digest. Inputs
  changing during a compile, time-dependent macros, response files and output/preprocessing modes prevent reuse.
- **Layout.** `progress.json` is the atomic checkpoint; `plan.json`, `sources/`, `candidates/`, `cache/`, `scans/`,
  `verification/` and `input-files/` hold the rest, integrity-checked on load.
- **Budgets** ([api.md](../api.md#source_searchrun)) apply per invocation, including calibration (two builds) and
  verification. `compilations` counts unit builds, each oracle plus stock. `search-run/CANCEL` (or `cancel_file`)
  stops the run and its process trees; remove it before resuming.
- **Resume** needs an unchanged plan, source, inputs and analyzer: rebuilding `mwcc-tools` needs a new run directory.
- **Replay** makes three fresh builds (original context, private baseline, candidate) and rechecks calibration,
  stock output, objective, dependencies and semantics, keeping saved outcomes. For a relocated bundle use
  [source-repro](source-repro.md).
