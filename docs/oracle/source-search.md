# Source search by observed graph objectives

The development Rust API is `mwcc_oracle_core::source_guided`. It provides a shared
objective evaluator and a bounded source search. Build current `mwcc-tools` and a
current Rust oracle producer; the pinned migration packages do not include this
command. Supported producers are `GC_1_3`, `GC_1_3_2` and `GC_2_7`.

```text
mwcc-tools source-search run plan.json --output search-run --candidates 100 --compilations 100 --seconds 600
mwcc-tools source-search run plan.json --output search-run --resume --compilations 100
mwcc-tools source-search replay search-run 1
mwcc-tools source-search state capture.json --object candidate.o --link RenderParticles__11CElementGenFv --class 4 --round 0
mwcc-tools source-search evaluate contract.json capture.json --source candidate.cpp --object candidate.o
```

Commands emit versioned JSON on stdout. Errors go to stderr. Search completion is
an application status in JSON: a zero process exit is not an objective success.
Read `status`, the candidate's `evaluation.overall`, its hard `constraints`, and
`independent_verification`. Every satisfied non-baseline candidate requires a fresh
compile and a fresh configured semantic check before `verified_success` is emitted.
If the baseline already satisfies the predicate, the run reports
`baseline_already_satisfied` and stops.

## Plan format 1

This complete example uses `initializer-linear.c` in the synthetic
`tests/fixtures/source-guided/` source directory. Replace the two absolute paths
for your installation. Its explicit optimization pragmas retain the named value
within the supported linear instruction scope. The initializer
edit deliberately changes behavior. Its success is a **mechanism reproducer**,
not a semantics-preserving source transformation or a RenderParticles discovery.

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

Relative manifest paths resolve against the plan's directory. `source` resolves
against the compiler working directory. To use a game unit, set `from_ninja` to
its `build.ninja`, use the source spelling from that edge, and set `flags` to `[]`.
The edge supplies the working directory, ordered flags and compiler version;
`version` can be null or must agree. The `working_directory` field is still required
and must exist. Backend, stage, local and CSE traces default on. An explicit
`traces` object uses `runner::TraceOptions`; `backend` must be true.

Selection uses an exact link name and a zero-based round within the selected
register class (`0` SPR, `1` CR, `2` VR, `3` FPR, `4` GPR). Object anchors require a
unique observed complete name, type kind/size and part (`single`, `first_word` or
`second_word`) in that function before the selected graph. Pointer addresses and
raw virtual-register numbers are capture-local, so they are not source identities.
An absent, ambiguous or incomplete anchor gives an indeterminate result.

Each transformation applies all its byte anchors atomically. Every anchor must
occur exactly once and anchors cannot overlap. Unrelated source bytes, including
non-UTF-8 bytes, are preserved. `precondition` records the author's justification;
the harness does not prove a text edit's language-level validity. A path applies
at most one transformation from each family. Breadth-first generation is bounded
by `maximum_depth` (at most 16) and `generation_limit`, including the baseline.
Provide guarded recipes for declarations, initializers, parameter types, wrappers
or promotion patterns; automatic C++ AST rewrite generation is not implemented.

## Objectives and validity

- `direct_initialization`: observe a constant LI initializing the anchored value
  directly in its final assigned register, with no surviving intermediate MR. It
  checks the first target definition and the final allocation round. Current
  recognition requires one instruction-bearing block with a verified linear,
  acyclic chain of empty scaffolding, no earlier control barrier or target use,
  and a direct LI or one local LI/MR pair for VR/FPR/GPR. A
  requested constant or source line cannot select a later assignment instead.
  Cross-block dominance and source declaration ancestry remain unsupported;
  those cases are indeterminate.
- `change_range_category`: use `value` and `target` instead of `constant` and
  `source_line`. The category must observably change to `physical`,
  `initial_object_range`, `before_coalescing_window`, `inside_coalescing_window`
  or `after_coalescing_window`, preserving normalized boundary PCode and final
  instruction/relocation structure. These are allocator number ranges, not source
  roles such as call-result or address temporaries.
- `range_category`: uses the same `value` and `target`, but tests the candidate's
  observed category without requiring a change or preserving its instructions.
  This static predicate is useful for reduction and corpus queries.
- `promoted_copy_coalescing`: selects a guarded scalar-array element by complete
  source name, `element_index` and `element_size`. For example:
  `{"kind":"promoted_copy_coalescing","value":{"name":"flag","element_index":1,"element_size":4},"copy":0,"outcome":{"kind":"rejected","condition":"destination_above_window"}}`.
  `copy` is the zero-based ordinal of observed incoming register-copy candidates
  in the complete selected coalescing pass. Set `outcome` to
  `{"kind":"coalesced"}` to require a confirmed merge or already-shared root.
  It checks actual decisions, array/counter coverage and unique birth/graph binding;
  it does not identify a source initializer automatically. A surviving promotion
  birth with no incoming copies remains indeterminate: it may be an unused node
  while another value carries the flag. See the [promoted-flag case](../oracle-source-guided-case-promoted-flags.md).
- `scalarized_array_initialization`: selects a whole array element using the same
  `name`, `element_index` and `element_size` fields, then requires an exact
  frontend-created scalar/backend birth join and applies the narrow constant-LI
  recognizer using `constant` and optional `source_line`. GPR only. Unknown or
  duplicate possible source bindings remain indeterminate, including creations
  without graph nodes. The real pointer-zero flag computation is not an LI and
  remains unsupported by this predicate. See [scalarization provenance](scalarization.md).
- `scalarized_array_definition`: selects the same exact frontend-created element
  and takes `definition`, a zero-based ordinal among definitions of its backend
  register in the captured `ra.before` block/instruction stream. For example:
  `{"kind":"scalarized_array_definition","value":{"name":"noRota","element_index":1,"element_size":4},"definition":0}`.
  It requires the selected producer to remain visible at the actual graph and
  after allocation, defining the anchored root and its recorded final physical
  register with compatible opcode and operand shape. A selected MR/FMR must have
  a unique preceding same-block producer, a complete actual coalescing decision,
  and no surviving copy. It supports FPR/GPR computations beyond LI constants.
  The ordinal describes observed compiler records; it does not infer C++
  initialization, dominance or execution order. Reports retain every candidate
  definition, the chosen instruction, producer and actual copy decision.
- `eliminate_temporary`: takes `value`. It deliberately remains indeterminate
  until complete semantic lifetime evidence can justify elimination. Missing nodes
  and inferred edge changes do not prove that a temporary disappeared.
- `symbol_address_pairs`: takes a qualified mangled `symbol` and inclusive
  `minimum`/`maximum` pair counts, with GPR selection. For example,
  `{"kind":"symbol_address_pairs","symbol":"kEnvModulate__9CGraphics","minimum":0,"maximum":2}`
  asks for at most two pairs from the guarded Normalize creation path before the
  selected graph. It requires complete materializer/counter capture, complete
  symbol identities, consistent adjacent high/low births and unique graph bindings
  for the selected results. Reports retain both counts, birth IDs, allocation roots,
  graph-only nodes and confirmed physical merges. Unknown identities or reused
  numbers remain indeterminate. This bounded path count does not prove the absence
  of all address temporaries, including values created later by optimization.

For the symbolic-address objective, require at least one relocation of each needed
type rather than preserving the incidental number of repeated materializations.
For GC 2.7 EnvModulate, kind 6 (high-adjusted) and kind 4 (low) each with addend 0
and `minimum: 1` retain the symbolic address while allowing fewer repeated pairs.

All objectives require stock-equivalent oracle objects with consistent retained
object hashes, matching supported compiler identities/flags/options, captured
source hashes, provenance bound to its exact trace, complete selected function graph
coverage, and all requested symbolic relocations. Relocation requirements count
within the selected function; null kind/addend accepts any kind/addend for that
symbol. Missing evidence is distinct from an observed failed predicate.

Set `purpose` to `source_transformation` for a candidate intended to preserve
behavior. Configure a checker, for example:

```json
{
  "program": "F:/checks/validate-particle-candidate.exe",
  "arguments": ["{baseline_source}", "{baseline_object}", "{source}", "{object}"],
  "inputs": ["F:/checks/particle-cases.json"],
  "timeout_seconds": 60
}
```

The executable runs directly, without a shell, from the compiler working directory.
Both baseline and candidate must return exit code zero within the timeout. Design
the checker to establish the required behavior; stock equivalence only compares
oracle and stock compilation of the same source. Declare checker dependencies
(scripts, imported modules, test data) in `inputs`; existing file arguments are
also hashed. A missing checker prevents source-transformation success. Results
record whether semantics were unchecked mechanism-only or a configured check
passed; a configured check is not a formal equivalence proof.

An optional `target: {"object": "target.o", "link": "exact_target_link"}` records
final byte mismatch and relocation equality separately. That mismatch is not the
graph objective. `score.eligible` requires all hard constraints; objective cost is
0 for observed satisfaction, 1 for observed failure, and null for indeterminate.
`ranking` orders eligible candidates first and known objective costs before unknown
ones. Edge-change counts are conditional diagnostics from the
[provenance-aware comparison](graph-correspondence.md), with multiplicity changes,
complete nodes and explicit unclassified boundary edges.
`fingerprint_error` explains unavailable state grouping separately from compilation
and objective results. An unsupported operand is not silently normalized; that
candidate remains available without joining a compiler-state group.

## Cache, grouping and retained results

`progress.json` is the atomic checkpoint. `plan.json` stores normalized paths,
`sources/` stores every candidate's exact bytes, `candidates/` stores evaluations,
and `groups` preserves every source representative of a relevant-state fingerprint.
Grouping never discards source representatives or their future transformations.
Fingerprints normalize known pointer/register identities but retain graph-only
nodes, degrees, origin/range evidence, selected instructions, symbolic relocations
and allocated data. Unsupported operand forms disable grouping. Conservative
graph-only numbering can miss equivalent states.
The current fingerprint payload is format 3. It includes normalized scalar-array
origin and partial call-result shape/type evidence, plus whole-function frontend
scalarization bindings, independent totals, unbound creations and pointer equality
relationships. Preallocation definitions, CFG structure, instruction identity
relationships through allocation, and actual coalescing decisions also participate:
two equal final graphs can give different definition predicates. It excludes capture-local pointer spellings and inactive output
bytes. Unsupported scalarization schemas or normalization disable grouping.
Reports expose `fingerprint_format_version`;
older frozen proofs retain their original analyzer and fingerprints.
Generated local relocation-name suffixes can also split equivalent states, as the
[real EnvModulate experiment](../oracle-source-guided-case-envmodulate.md) demonstrates.
That case used complete graph checks and independent objdiff evidence; the generic
fingerprint has not yet incorporated a checked local-symbol normalization.

`source-search state` inspects a saved JSON envelope containing `compilation` and
the supplied object, without compiling. It validates verified stock-check status
and explicit identical output,
the object hash and complete selected-function graph coverage. The format-1 report
contains `link`, `class`, `round`, source/object hashes, an optional `fingerprint`
and a `normalization` evaluation. Unsupported representations give an indeterminate
evaluation (`compiler_state_coverage`) with no fingerprint. Invalid captures,
objects or selectors fail with diagnostics on stderr. This does not establish
current dependency integrity or rerun a semantic checker; use search replay for
fresh compilation and checks.

`source-search evaluate` applies a contract to retained source/object/capture files
without compiling. It accepts a compile-trace envelope or a plain `Compilation`.
Add all three `--baseline-capture`, `--baseline-source` and `--baseline-object`
options when testing a transition; otherwise the candidate is its own baseline
for static predicates. It verifies the retained hashes and capture evidence, but
does not rescan dependencies or execute a semantic checker. Consequently a
`source_transformation` contract remains indeterminate here. Use search/replay for
fresh compilation and configured semantic validation.

Fixed GQR operands are supported only in the observed PSQ_L/PSQ_ST read form
(opcodes 403/407): all twelve operand bytes, including selectors 912 through 919,
remain exact. They are hardware identities, not renamed virtual registers. Other
kind-1 forms remain unsupported. The real shared-pointer EnvModulate experiment
now has reproducible distinct baseline/candidate fingerprints on both hosts.

Compilation caching is separate. Every lookup runs dependency and preprocessing
scans with **both** oracle and stock compilers. Keys include both preprocessed
hashes, raw dependency bytes, source identity, ordered flags, working directory,
trace options, compiler/DLL/loader hashes and a digest of the inherited environment.
Environment values are not written into results. Source variants use stable
content-addressed paths; private-baseline output is calibrated against an original
context compile. A second scan rejects inputs changing during compilation.
Time-dependent macros disable reuse. Response files and output/preprocessing modes
are rejected. Failed compiles are not reusable artifacts.

`cache/` contains captures and objects, `scans/` retains each input scan,
`verification/` contains independent compiles, and `input-files/` retains dependency
and runtime bytes by hash. Artifact integrity is checked on load. Analyzer, checker
inputs and target identities also bind resumable analysis. The analyzer executable
is hashed once per invocation rather than copied into every compiler cache entry.
Checker/proof files and the target are rechecked before completed outcomes are
saved; checker inputs are also checked before and after execution. Changes during
a run invalidate its evidence instead of being recorded under the old identity.

Budgets apply per invocation, including baseline calibration and independent
verification. `--compilations` counts attempted source-unit builds (each includes
oracle and stock); preprocessing/checker subprocesses do not consume that count
but do consume time. Baseline calibration needs two builds; a new successful
candidate normally needs one build and one verification build. Time limits are
checked between hashing/setup operations and while waiting for subprocesses.
Cancellation terminates active compiler/checker process trees. Create the cancel
file (default `search-run/CANCEL`) to stop; remove it before resuming. Pending
candidates are retried. A successful run is terminal; use independent replay or
a new output directory for further work.

Resume and replay require unchanged source, plan, relevant build inputs and analysis identity.
Keep the analyzer executable with the case; rebuilding it changes that identity.
Use a fresh directory after those change. Replay retains saved outcomes and makes
three fresh builds: original context, private baseline and candidate. It rechecks
stock equality, calibration, objectives and configured semantics. Replay requires
both baseline and candidate-only dependencies to match retained hashes; restore them in an isolated
checkout if needed. Retained bytes are available, but automatic portable repro
export/restoration is not implemented.

## Rust entry points

Use `objective::{Contract, Input, evaluate}` for the shared evidence-aware predicate,
`state::{graph, snapshot, fingerprint, inspect}` for observed state, `inputs::capture` for
compiler-input identity, and `search::{load_plan, run, replay}` for orchestration.
`state::fingerprint` takes a graph, its provenance report and the complete
`runner::Compilation`; it verifies report/trace binding before normalizing the
allocation records needed by the predicates. An object and graph alone do not
contain enough evidence for state grouping.
These native typed APIs and strict versioned JSON are the supported development
interface. Legacy Python API compatibility is not required.

The real [EnvModulate experiment](../oracle-source-guided-case-envmodulate.md)
finds a shared-const-pointer rewrite with fewer observed address pairs and lower
`noRota` degree, verified by fresh builds on both hosts. The final match worsens;
this is a graph-objective result, not a matching fix. The mechanism-preserving
[reducer](source-reduce.md) and [matched-source index](source-index.md) now have
native implementations using the shared evaluator. Guarded scalar-array and
call-result origins and a promoted-copy predicate are implemented. Stronger
expression/inline/lifetime bindings and the complete RenderParticles
investigation remain open. See the [implementation status](../oracle-source-guided-status.md).
