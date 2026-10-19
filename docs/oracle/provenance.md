# Backend provenance (development API)

Use backend provenance to explain where virtual registers came from, which
coalescing checks decided a copy, and what graph actually reached simplification.
Build current native tools and a fresh Rust producer with `configure --oracle`;
these methods are newer than the pinned migration release in the kits.

```sh
mwcc-tools call oracle.trace source=SOURCE.cpp version=GC_2_7 flags='["COMPILER_FLAGS"]' streams.backend=true \
    capture_path=capture.json --oracle-root ORACLE_ROOT
mwcc-tools call provenance.inspect trace_path=capture.json > provenance.json
mwcc-tools call provenance.inspect trace_path=capture.json link=RenderParticles__11CElementGenFv text=true
```

`provenance.inspect` accepts raw traces or compile captures; its result's `document` is the report, and `text: true`
adds the printed lines. `link` selects an exact mangled name; report-wide coverage stays conservative when
filtering the display. To trace an existing unit with its ordered flags and working directory, give `project` and
`unit` and save the raw trace (`trace_path=unit.trace`; a unit has no capture) for `provenance.inspect`. Keep the
default stock-object equality check. `streams.backend` is opt-in and isolated from inherited environment
settings; corpus `--all-traces` also enables it.

## Reading the report

The format-2 report separates coalescing decisions, counter/birth history, graphs,
allocation ranges and frontend evidence. A satisfied coverage result means the
declared mechanism was observed completely; it is not a source-search objective.
Missing or unsupported evidence stays indeterminate. Malformed or contradictory
records fail parsing.

The aggregate `lifecycle` result remains indeterminate: complete leaf/inline
origins, source roles and semantic lifetimes are not established. Names, source
lines, raw pointers and graph absence do not supply those identities. Use the
[source-search contracts](source-search.md) for predicates with explicit scopes.

Rust callers can use `provenance::inspect` for captures/raw traces,
`provenance::parse` for traces, or the underlying `value_history`, `graph_history`
and `allocation_category` modules. Capture inspection skips unrelated rendered
report data instead of constructing another complete JSON analysis tree.

## Coalescing decisions

`pv.coalesce.*` records retain pass/candidate observation IDs, function, class,
physical threshold, window bounds, instruction identity/operands/flags, branch
outcomes and saved inputs. Candidates follow the retail copy flag, not an assumed
opcode list. The retail algorithm still makes every decision.

Typed decisions are `rejected`, `already_same_root`, `merged` or `indeterminate`.
Validation checks the ordered short-circuit path, instruction flags, destination
kind/class, roots, interference, window bounds, physical-register bypass and the
GPR special-register guard. That last name describes an observed condition; its
higher-level meaning is unconfirmed. A merge reads the union parent **after** the
retail write. Observation IDs and instruction many-to-one mappings are not
semantic lifetime identities or proof of elimination.

The installer guards complete retail bodies and COFF relocation identities,
then preserves original instructions and CPU/FPU state around its probes.
Changed bytes or layouts fail linking. Rebuild common hooks and producer together;
stale internal callback ABIs are rejected.

## Births, generations and expression context

`history` retains functions, births, counter changes, expression observations,
orphan writes and gaps. `counter_observations` checks the declared direct-counter
scope, independently for each function. Missing ends, discontinuities, orphan
writes and unsupported schemas cannot establish complete capture.

The direct-write inventory covers 570 sites in GC 1.3, 571 in GC 1.3.2 and 578 in
GC 2.7, plus two function-context and two range-marker sites per version. It
includes increments, initialization, scratch rewinds, high-water restores and
allocation resets. Restoring a high-water mark is not a birth.

`Birth.id` is a capture-local function/sequence pair. `generation` counts earlier
births using the same class/number in that function; `registers[].values` retains
all generations. One-based `trace_position` fields preserve event interleaving.
Births remain visible even when no final instruction uses them. Never bind every
use to the latest generation or compare IDs directly across source variants.

Each birth records the physical threshold, canonical retail creation site,
pipeline phase, source line and available statement-root context. Canonical sites
need not equal relocated runtime addresses. Context can be partial or stale outside
active generation; it is not a causal leaf-expression or declaration-initializer
claim. Spellings retain bytes and optional UTF-8, with separate completeness.
`category=null` means the semantic allocation role is unknown.

`Birth.object` records guarded single-register/two-word object bindings before
the binding writes: Object/VarInfo identities, datatype, storage, type kind/size,
flags and byte-preserving names with known/complete bits. Pair parts mean creation
order, not high/low word order. Repeated names/pointers are not unique semantic
identities; a missing binding does not prove an anonymous temporary.

## Graphs at the observed boundary

`graphs.graphs` pairs the actual instruction list immediately before retail
simplification (`boundary=before_simplify`) with every graph node, raw neighbor
list, unique adjacency degree, stored degree, flags and colors. `graph_only` means
absent from that instruction list, not eliminated. `instruction_only` and
`missing_neighbor_nodes` retain other boundaries. Physical thresholds are
class-specific, including SPR and condition registers.

`pv.graph.begin/instruction/node/end` records carry function-local graph ordinals
and birth cutoffs. The parser independently checks instruction visibility,
node/instruction totals and function-end `graphs=N`. Missing snapshots/ends,
unsupported schemas or contradictory visibility prevent complete coverage.
`graphs.observations` and per-function observations describe snapshot coverage
separately from birth binding. Old captures without these records remain usable
but cannot establish that coverage. Live physical thresholds are cross-checked
against the preceding coalescer pass when available; stale zero thresholds remain
indeterminate.

A node's `binding.direct` retains possible births before the graph in the current
counter-reset epoch. `contributors` adds origins from confirmed parent writes;
`merges` retains both roots, candidate birth sets and decision positions. Future
births/merges cannot explain an earlier graph. Resets prevent cross-epoch binding;
reused numbers and unknown virtual contributors remain ambiguous. A satisfied
`binding.evidence` proves capture-local number/merge binding, not cross-variant
semantic identity.

```sh
mwcc-tools call provenance.compare first=baseline.json second=candidate.json link=RenderParticles__11CElementGenFv \
    class=4 first_round=0 second_round=0
```

The [graph correspondence API](graph-correspondence.md) combines unique observed
object/symbol anchors with instruction hypotheses. It keeps confirmed local
merges separate from conditional cross-variant mappings. Conflicts, unmatched
nodes and boundary edges remain explicit; elimination remains indeterminate.

## Observed allocation ranges

`range_category` and `range_bounds` classify graph numbers as `physical`,
`initial_object_range`, `before_coalescing_window`, `inside_coalescing_window` or
`after_coalescing_window`. These are numeric retail ranges, not semantic roles.
The initial-object upper bound and coalescing bounds are inclusive.

`pv.category.initial/window` records contain completed tables and birth cutoffs;
function-end `ranges=N` detects missing markers. The parser checks counters,
markers and graph bounds. Missing/stale/overlapping bounds remain indeterminate.
The window's last bound can grow after its opening marker. `-O0` can omit both
markers: observing zero markers does not establish categories. A definite range
category does not resolve an ambiguous value generation.

## Specific origin mechanisms

- **Symbolic addresses:** `Birth.materialization` records the two guarded Normalize
  high/low allocation sites, source Operand/Object/displacement, datatype/storage
  and cached mangled symbol bytes with known/complete flags. It does not invoke the
  mangler. A missing cache stays unknown. `matching_dispatch_output` is a scoped
  same-address/tag/Object/displacement state match, not proof of no alias writes.
  `materialization_observations` requires scope `guarded_normalize_pair_v1`,
  complete counters, matching totals and bindings at every guarded birth. Zero
  means only that this pair-allocation path was not observed.
- **Scalar array promotion:** `Birth.array_promotion` records original array
  identity/name, element index/size, array size/count, optimizer flags and type
  metadata at the two guarded GPR/FPR creation paths. Scope `guarded_scalar_v1`,
  counters, totals and every site binding must agree. Vector promotion is outside
  this scope. A complete name does not establish inline/declaration ancestry.
- **Call results:** `Birth.call_result` covers the six guarded result-register
  creation sites, retaining direct call/callee ENodes, return type and allocated
  part. It supplies neither a qualified overload-stable symbol nor full inline
  ancestry. The 24 `output_bytes` are explicitly partial/uninitialized
  (`output_complete=false`). Scope `guarded_call_result_v1`, totals, guarded births
  and adjacent pair-word bindings must agree. Intrinsic/other helper temporaries
  are not all classified by this mechanism.

[Frontend scalarization](scalarization.md) separately records source byte/bit
intervals, returned temporary identities and independent totals.
`scalarization.backend_values` joins exact Object/VarInfo identities to births
with its own evidence. [Call argument provenance](call-arguments.md), available
as `Report.call_arguments`, records lowered contexts, evaluated operands, owned
births and instruction intervals independently of call-result creation.

## Dispatch, retained inputs and validation

```text
mwcc-tools provenance audit-counters --root ORACLE_ROOT --version GC_2_7
mwcc-tools provenance audit-dispatch --root ORACLE_ROOT --version GC_2_7
```

The counter audit retains image/metadata identities, direct references,
classification, unresolved sites and guards. `lifecycle_coverage_complete=false`
remains deliberate: direct references do not exclude computed aliases/bulk writes.
A metadata container is a link location, not necessarily a semantic function.

The dispatch audit guards all 78 initializer entries (0 through 77), their pointer
writes and exact targets, including same-named local functions. Expression records
retain parent IDs, `codegen_dispatch` context, retail generator, raw argument bits,
tagged result and end/abandonment state. Results read only fields defined by their
Operand tag; unknown tags are indeterminate. `guarded_table_v1`, balanced calls
and end totals establish table-call coverage, not complete leaf/inline ancestry.
Direct helpers can bypass the table; statement lines can identify inline callers.
Raw ABI argument bits are not register identities.

Reports bind the exact raw trace with `trace_sha256`; passes retain `end_position`.
Shared source objectives check report/capture agreement and that the selected
complete pass ends before its graph. Older digest-less reports cannot establish
that retained-input gate. Mutable Rust report structures remain trusted caller data.

State fingerprint format 4 includes supported backend, scalarization and call
argument evidence with normalized pointer equality/order relationships. It excludes
partial call-output storage and incidental argument bits. Unsupported normalization
disables grouping; saved searches also bind the analyzer executable. Arena capture
normalization is not yet supported. Consult retained reports for the actual format.

`tools/migration/verify_backend_probes.py` independently checks off/on stock-object
equality, existing traces, decision paths, histories and graph observations.
`--case` selects fixtures; `--from-ninja`, `--source` and `--project-only` support
complete units. Python is a development verifier, not a runtime dependency. See
[extension status](../oracle-source-guided-status.md) for exercised paths and exact
accepted evidence, including the [EnvModulate case](../oracle-source-guided-case-envmodulate.md).
Successful comparisons make no performance or rare-branch coverage guarantee.

[Source arena observations](source-arenas.md) and the
[lifetime ledger](source-lifetimes.md) provide allocation identity prerequisites.
They do not establish initialized node contents or source-write/inline ancestry;
that bridge's native proof remains separate. The remaining scope is tracked in the
[source-guided roadmap](../oracle-source-guided-search.md).
