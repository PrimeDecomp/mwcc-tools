# Search source variants by compiler behavior

`mwcc-tools source-search` tries guarded source edits, compiles each variant and
scores an observed compiler objective. Use it to find edits that change graph
structure, allocation categories, initialization or symbolic-address creation.
It supports GC 1.3, 1.3.2 and 2.7 with a current development tool and Rust producer.

```text
mwcc-tools source-search run plan.json --output search-run --candidates 100 --compilations 100 --seconds 600
mwcc-tools source-search run plan.json --output search-run --resume --compilations 100
mwcc-tools source-search replay search-run 1
```

Commands return JSON. `verified_success` means a non-baseline candidate passed
the objective, all hard constraints and a fresh independent compile/check.
`baseline_already_satisfied` stops a search whose baseline already meets the
objective. Check `status`, `evaluation.overall`, `constraints` and
`independent_verification`; a zero process exit alone is not success.

## Plan format 1

This example edits `tests/fixtures/source-guided/initializer-linear.c`. Replace
the absolute paths. The edit changes behavior, so it is explicitly a mechanism
reproducer rather than a semantics-preserving transformation.

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

Manifest paths resolve relative to the plan; `source` resolves relative to the
compiler working directory. For a game unit, set `from_ninja` to `build.ninja`,
use that edge's source spelling and set `flags` to `[]`. The edge supplies the
working directory, flags and version. `version` may be null or must agree;
the required `working_directory` must still exist. Backend, stage, local and
CSE traces default on. An explicit `traces` uses `runner::TraceOptions` and must
keep `backend: true`.

Each transformation applies all byte anchors atomically. Anchors must occur
exactly once and cannot overlap. Other bytes, encodings and line endings are
preserved. `precondition` is the author's justification, not a language-level
proof. Breadth-first generation uses at most one recipe from each family per
path, bounded by `maximum_depth` (at most 16) and `generation_limit` (including
the baseline). Supply recipes for the source changes you want to explore;
automatic C++ AST rewriting is not implemented.

## Objectives and validity

`selection` names an exact link, register class and zero-based round. Classes are
`0` SPR, `1` CR, `2` VR, `3` FPR and `4` GPR. Named-value anchors require a unique,
complete observed name/type/part binding before that graph; `part` is `single`,
`first_word` or `second_word`. Raw pointers and register numbers are not source
identities. Missing or ambiguous evidence is **indeterminate**, not a failed
predicate or proof of absence.

Supported objectives:

- `direct_initialization`: `value`, `constant`, optional `source_line`. Requires
  the first target definition to be a direct LI or one local LI/MR pair that
  initializes the final assigned register without a surviving copy. Recognition
  is limited to VR/FPR/GPR, one instruction-bearing block and verified linear,
  acyclic empty scaffolding, with no earlier barrier or target use. A constant
  or line cannot select a later assignment. Cross-block dominance and C++
  declaration ancestry remain unsupported.
- `range_category`: `value`, `target`. Tests `physical`, `initial_object_range`,
  `before_coalescing_window`, `inside_coalescing_window` or
  `after_coalescing_window` through observed births and confirmed merge roots.
  These are allocator ranges, not semantic source roles.
- `change_range_category`: the same fields, but requires a change from the
  baseline while preserving normalized boundary PCode and final instruction/
  relocation structure. Use `range_category` for static reduction/index queries.
- `promoted_copy_coalescing`: selects `value` by `name`, `element_index` and
  `element_size`; `copy` is the zero-based incoming-copy ordinal in the complete
  matching coalescer pass. `outcome` is `{"kind":"coalesced"}` or, for example,
  `{"kind":"rejected","condition":"destination_above_window"}`. Requires
  guarded scalar-array/counter coverage and unique birth/graph binding. A
  coalesced result needs an actual merge or already-shared root. Zero incoming
  copies remain indeterminate: the promotion node may be unused while another
  value carries initialization. An out-of-range ordinal after at least one bound
  copy is unsatisfied. Covers scalar GPR/FPR promotion, not VMX; the ordinal does
  not identify a source initializer.
- `scalarized_array_initialization`: the same element selector, `constant` and
  optional `source_line`. Requires an exact whole-element frontend scalar/
  backend birth join and the narrow LI recognizer; GPR only. Unknown or duplicate
  bindings, including creations without graph nodes, remain indeterminate.
  Pointer-zero computations that are not LI are unsupported.
- `scalarized_array_definition`: the same element selector and `definition`, a
  zero-based ordinal in the captured `ra.before` definitions. Requires the
  producer to survive through the graph and final allocation with compatible
  opcode/operands and the recorded physical destination. MR/FMR needs a unique
  preceding same-block producer, actual coalescing evidence and no surviving
  copy. Supports FPR/GPR computations beyond constants; the ordinal is compiler
  record order, not C++ initialization, dominance or execution order.
- `symbol_address_pairs`: qualified `symbol` and inclusive `minimum`/`maximum`,
  with GPR selection. Counts adjacent high/low births on the guarded Normalize
  path before the graph. Requires complete materializer/counter/symbol evidence
  and unique result bindings. It does not count every possible address temporary.
- `call_argument_address_pairs`: adds exact qualified `callee`, zero-based
  `call` occurrence and lowered-context `ordinal` to the symbol/count fields.
  Both births must belong to that argument, including void-call arguments.
  Unknown earlier callees, missing contexts or incomplete independent totals
  remain indeterminate. Ordinals are not automatically C++ parameter indices.
- `eliminate_temporary`: takes `value`; remains indeterminate until semantic
  lifetime evidence can prove elimination. A missing graph node is insufficient.

See [scalarization](scalarization.md), [call arguments](call-arguments.md) and
the [promoted-flag case](../oracle-source-guided-case-promoted-flags.md) for the
underlying observations.

All objectives require consistent retained source/object hashes, a supported
compiler and matching flags/options, stock-equivalent oracle output, provenance
bound to its trace, complete selected-function graph coverage and requested
relocations. Relocations are counted within the selected function; null kind or
addend accepts any value for that symbol. For a zero-count address search, require
the symbol's relocations so an unrelated function cannot pass vacuously. GC 2.7
EnvModulate commonly needs kind 6 (HA) and kind 4 (LO), addend 0, each minimum 1.

Set `purpose: "source_transformation"` when behavior must be preserved and
provide a checker:

```json
{
  "program": "F:/checks/validate-particle-candidate.exe",
  "arguments": ["{baseline_source}", "{baseline_object}", "{source}", "{object}"],
  "inputs": ["F:/checks/particle-cases.json"],
  "timeout_seconds": 60
}
```

The checker runs directly from the compiler working directory. Baseline and
candidate must both exit zero within the timeout. Declare its scripts, modules
and data in `inputs`; existing file arguments are also hashed. Stock equivalence
compares oracle and stock compilation of the **same source**. It does not replace
a semantic check, and a configured check proves only its declared coverage.

An optional `target: {"object":"target.o","link":"exact_target_link"}` reports
final byte mismatch/relocation equality separately. Graph-objective success can
worsen the final match, as in the [EnvModulate case](../oracle-source-guided-case-envmodulate.md).

## Scores and compiler-state grouping

`score.eligible` requires every hard gate. Pair-count objectives cost their
distance from the inclusive requested interval; other objectives cost 0 when
satisfied and 1 when unsatisfied. Indeterminate cost is null. Ranking puts
eligible candidates first, then known costs before unknown ones. Partial progress
cannot trigger success. Rust callers can use `Score::compare_priority` plus a
stable candidate ID for ties. Edge changes are conditional diagnostics from
[graph correspondence](graph-correspondence.md).

Fingerprint format 4 groups only supported observed compiler states. It retains
graph-only nodes/degrees, births/ranges, relevant PCode/CFG and instruction
identities, actual coalescing decisions, scalarization and guarded argument
observations, symbolic relocations and allocated data. Known pointer/register
identities are normalized; inactive output bytes are excluded. Unsupported forms
produce `fingerprint_error` and disable grouping without discarding the candidate.
Every source representative and its future transformations are retained.
Conservative numbering and generated local-symbol suffixes can miss duplicates;
they are not silently treated as equivalent.

Inspect retained evidence without compiling:

```text
mwcc-tools source-search state capture.json --object candidate.o --link RenderParticles__11CElementGenFv --class 4 --round 0
mwcc-tools source-search evaluate contract.json capture.json --source candidate.cpp --object candidate.o
```

`state` accepts a JSON envelope containing `compilation`, validates the object,
stock-check status and selected graph, and returns a fingerprint or indeterminate
normalization. `evaluate` also accepts plain `Compilation`; for a transition add
all three `--baseline-capture`, `--baseline-source` and `--baseline-object` options.
Neither command rescans dependencies or runs a semantic checker, so retained-only
evaluation cannot validate a `source_transformation`. Use replay for fresh checks.
Fixed GQR operands are supported only for observed PSQ_L/PSQ_ST reads (403/407),
with all 12 bytes and hardware selectors 912–919 kept exact; other kind-1 forms
remain unsupported.

## Cache, budgets and replay

Compilation reuse requires fresh dependency/preprocessing scans with both oracle
and stock compilers. Keys include preprocessed hashes, raw dependencies, source,
ordered flags, working directory, trace options, runtime hashes and an inherited
environment digest. Environment values are not written to results. A second scan
rejects inputs changing during compilation. Time-dependent macros disable reuse;
response files and output/preprocessing modes are rejected. Failed builds are
not reusable artifacts. Equal graphs alone never authorize compilation reuse.

`progress.json` is the atomic checkpoint; `plan.json`, `sources/`, `candidates/`,
`cache/`, `scans/`, `verification/` and `input-files/` retain the plan, exact source
bytes, evaluations, captures, objects and input/runtime blobs. Integrity is
checked on load. The analyzer, checker/proof inputs and target bind resume;
checker inputs are checked before/after execution and before outcomes are saved.

Budgets apply per invocation, including calibration and independent verification.
`--compilations` counts attempted source-unit builds, each including oracle and
stock; scans/checkers consume time but not that count. Baseline calibration needs
two builds; a new successful candidate normally needs a build and a verification
build. Cancellation terminates active process trees. Create `search-run/CANCEL`
(or the configured cancel file) to stop and remove it before resume. Pending
candidates are retried; a successful run is terminal.

Resume requires unchanged plan, source, relevant inputs and analyzer identity.
Keep the executable with the case; rebuilding it requires a new run directory.
Replay preserves saved outcomes and makes three fresh builds: original context,
private baseline and candidate. It rechecks calibration, stock output, objective,
dependencies and configured semantics. Restore both baseline and candidate-only
dependencies for ordinary replay, or use [source-repro](source-repro.md) for a
bounded relocated bundle.

Rust APIs live in `source_guided`: `objective::{Contract,Input,evaluate}`,
`state::{graph,snapshot,fingerprint,inspect}`, `inputs::capture` and
`search::{load_plan,run,replay}`. Fingerprinting takes the graph, provenance report
and complete `runner::Compilation`; an object/graph alone is insufficient.
See the [implementation status](../oracle-source-guided-status.md) for retained
validation and remaining work.
