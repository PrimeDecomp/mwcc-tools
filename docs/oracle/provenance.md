# Backend provenance (development API)

Backend provenance explains where virtual registers came from, which coalescing checks decided a copy, and what
graph reached simplification. The x86 oracle needs `mwcc-tools configure --oracle` (the Rust producer,
[producer.md](producer.md)); the GC 2.6/2.7 WebAssembly module has its probes in gc2's sources
([wasm.md](../wasm.md#probes)).

```sh
mwcc-tools call oracle.trace source=SOURCE.cpp version=GC_2_7 flags='["COMPILER_FLAGS"]' streams.backend=true \
    capture_path=capture.json --oracle-root ORACLE_ROOT
mwcc-tools call provenance.inspect trace_path=capture.json link=RenderParticles__11CElementGenFv text=true
mwcc-tools call provenance.compare first=baseline.json second=candidate.json link=RenderParticles__11CElementGenFv \
    class=4 first_round=0 second_round=0
```

- `provenance.inspect` reads a raw trace or a compile capture; `document` is the report, `text: true` adds the
  printed lines, `link` selects one mangled name (report-wide coverage stays conservative).
- For a project unit, give `project` and `unit` to `oracle.trace` and save the raw trace (`trace_path=unit.trace`;
  a unit has no capture).
- `streams.backend` is opt-in; `corpus --all-traces` enables it too.
- GC 2.6 and 2.7 run in process by default; there the expression, dispatch, coalescing and call-argument context
  streams are absent or partial ([wasm.md](../wasm.md#probes)). Use `MWCC_IN_PROCESS=0` for those.

Rust: `provenance::inspect` (captures or raw traces), `provenance::parse` (traces), and the `value_history`,
`graph_history` and `allocation_category` modules.

## Reading the report

The report separates coalescing decisions, counter/birth history, graphs, allocation ranges and frontend evidence.
A satisfied coverage result means the declared mechanism was observed completely. Missing or unsupported evidence
is indeterminate; malformed or contradictory records fail parsing.

The aggregate `lifecycle` result is always indeterminate: leaf/inline origins, source roles and semantic lifetimes
are not established. Names, source lines, raw pointers and graph absence do not supply those identities. Use the
[source-search objectives](source-search.md#objectives-and-validity) for predicates with explicit scopes.

## Coalescing decisions

`pv.coalesce.*` records keep pass/candidate IDs, function, class, physical threshold, window bounds, instruction
identity/operands/flags, branch outcomes and saved inputs. Candidates follow the retail copy flag; the retail
algorithm makes every decision.

Decisions are `rejected`, `already_same_root`, `merged` or `indeterminate`. Validation checks the short-circuit
path, flags, destination kind/class, roots, interference, window bounds, physical-register bypass and the GPR
special-register guard (named for an observed condition; its meaning is unconfirmed). A merge reads the union
parent **after** the retail write. Observation IDs are not lifetime identities or proof of elimination.

The installer guards complete retail bodies and COFF relocation identities and preserves the original instructions
and CPU/FPU state around its probes. Changed bytes or layouts fail linking.

## Births, generations and expression context

`history` keeps functions, births, counter changes, expression observations, orphan writes and gaps.
`counter_observations` checks the direct-counter scope per function; missing ends, discontinuities, orphan writes
and unsupported schemas prevent complete capture.

The direct-write inventory has 570 sites in GC 1.3, 571 in GC 1.3.2 and 578 in GC 2.6/2.7, plus two
function-context and two range-marker sites. It includes increments, initialization, scratch rewinds, high-water
restores and allocation resets. Restoring a high-water mark is not a birth.

- `Birth.id` is a capture-local function/sequence pair. `generation` counts earlier births of the same class/number
  in the function; `registers[].values` keeps all generations. Never bind every use to the latest generation or
  compare IDs across source variants.
- Each birth records the physical threshold, canonical retail creation site, phase, source line and available
  statement-root context. The context can be partial or stale; it is not a leaf-expression claim.
  `category=null` means the allocation role is unknown.
- `Birth.object` records guarded single-register and two-word object bindings: Object/VarInfo identities,
  datatype, storage, type kind/size, flags and byte-preserving names. Pair parts are creation order, not high/low
  word order. A missing binding does not prove an anonymous temporary.

## Graphs at the observed boundary

`graphs.graphs` pairs the instruction list immediately before retail simplification (`boundary=before_simplify`)
with every graph node, raw neighbor list, unique and stored degree, flags and colors. `graph_only` means absent
from that instruction list, not eliminated. Physical thresholds are class-specific.

`pv.graph.begin/instruction/node/end` carry function-local graph ordinals and birth cutoffs; the parser checks
instruction visibility, totals and the function end's `graphs=N`.

A node's `binding.direct` keeps the possible births before the graph in the current counter-reset epoch;
`contributors` adds origins from confirmed parent writes; `merges` keeps both roots and candidate birth sets.
Future births cannot explain an earlier graph; resets prevent cross-epoch binding. A satisfied `binding.evidence`
proves capture-local binding, not cross-variant identity. `provenance.compare` and the
[graph correspondence API](graph-correspondence.md) handle cross-variant mappings.

## Allocation ranges

`range_category` and `range_bounds` classify graph numbers as `physical`, `initial_object_range`,
`before_coalescing_window`, `inside_coalescing_window` or `after_coalescing_window`. These are numeric retail
ranges, not semantic roles; bounds are inclusive. `pv.category.initial/window` records and the function end's
`ranges=N` establish them. The window's last bound can grow after its opening marker. `-O0` can omit both markers,
so zero markers do not establish categories.

## Specific origin mechanisms

| Field | Scope | What it records | What it does not establish |
| --- | --- | --- | --- |
| `Birth.materialization` | `guarded_normalize_pair_v1` | The two Normalize high/low sites: Operand/Object/displacement, datatype, cached mangled symbol bytes | No alias writes; a missing name cache stays unknown (the mangler is not called) |
| `Birth.array_promotion` | `guarded_scalar_v1` | Array identity/name, element index/size, array size/count, flags, type, at the GPR/FPR creation paths | Vector promotion; inline/declaration ancestry |
| `Birth.call_result` | `guarded_call_result_v1` | Six result-register sites: call/callee ENodes, return type, allocated part | A stable qualified symbol; full ancestry. `output_bytes` are partial (`output_complete=false`) |

Each scope needs complete counters, matching totals and a binding at every guarded birth. Zero means only that
the guarded path was not observed.

[Scalarization](scalarization.md) (`Report.scalarization`) and [call arguments](call-arguments.md)
(`Report.call_arguments`) are separate streams in the same report.

## Audits and validation

```text
mwcc-tools provenance audit-counters --root ORACLE_ROOT --version GC_2_7
mwcc-tools provenance audit-dispatch --root ORACLE_ROOT --version GC_2_7
```

- The counter audit keeps image/metadata identities, direct references, classification, unresolved sites and
  guards. `lifecycle_coverage_complete=false` is deliberate: direct references do not exclude computed aliases.
- The dispatch audit guards all 78 initializer entries, their pointer writes and targets. Expression records keep
  parent IDs, `codegen_dispatch` context, generator, raw argument bits, tagged result and end state.
  `guarded_table_v1` establishes table-call coverage, not leaf/inline ancestry; direct helpers can bypass the table.

Reports bind the raw trace with `trace_sha256`; source objectives check that binding. State fingerprint format 4
includes backend, scalarization and call-argument evidence with normalized pointer relationships, and rejects
arena captures.

[Source arenas](source-arenas.md) supply allocation identity; they do not establish node contents or
source-write ancestry.
