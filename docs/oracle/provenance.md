# Backend birth, graph and coalescing provenance (development API)

Build the current native tools and a fresh Rust producer with `configure --oracle`.
These commands are newer than the pinned Rust migration release in the kits.

```text
mwcc-tools oracle compile-trace --oracle-root ORACLE_ROOT --version GC_2_7 --backend SOURCE.cpp -- COMPILER_FLAGS > capture.json
mwcc-tools provenance inspect capture.json --json > provenance.json
mwcc-tools provenance inspect capture.json --link RenderParticles__11CElementGenFv
```

Use `--from-ninja PROJECT/build.ninja` to recover the existing unit's ordered flags
and working directory. `provenance inspect` accepts raw traces and compile-trace
JSON. `--link` selects an exact mangled name. The default compilation still checks
object equality against the stock compiler; keep that check when investigating a
new producer. `--backend` is opt-in and is isolated from inherited environment
settings. Corpus `--all-traces` also enables it.

## Observations and explanations

The native linker checks the complete audited retail coalescer and its relocation
layout, widens and redirects its local branches, and inserts observation calls. It
preserves the original instructions and uses their existing relocations for data
addresses. A byte/layout mismatch fails linking. Each probe saves general registers,
EFLAGS, x87 state, MXCSR and XMM registers. The retail algorithm continues to decide
which instructions to coalesce.

Format-1 `pv.coalesce.*` records contain pass and candidate observation IDs, the
function link name, active class, counts, physical threshold, window bounds,
instruction identity/operands/flags, branch site/outcome and saved inputs. The
candidate filter is the retail copy flag, rather than an assumed opcode list.
Checks include instruction flags, destination kind/class, roots, interference,
four short-circuit window bounds and the GPR special-register guard. The latter
name describes the observed condition; its higher-level meaning is unconfirmed.
A merge record reads the union-parent entry **after** the retail write.

The format-2 inspection result retains raw inputs alongside typed decisions:
`rejected`, `already_same_root`, `merged` or `indeterminate`. It validates branch
sites, EFLAGS/outcomes, the ordered short-circuit path, physical-register window
bypass, candidate counts, root ordering and observed parent writes. Malformed or
contradictory records fail parsing. Incomplete captures or paths remain
indeterminate. Report-wide coverage stays conservative when `--link` filters the
display.

`decisions` describes coverage of the captured coalescing passes. A satisfied value
means those decisions were fully observed; it is not a source-search objective.
The separate aggregate `lifecycle` result remains indeterminate: semantic
categories, complete expression/inline origins and semantic lifetime binding remain
incomplete. Pass/candidate IDs are observation IDs. These records alone cannot
prove a temporary was eliminated.

## Births, counter changes and expression context

The producer also observes every audited direct counter write: 570 sites in
GC 1.3, 571 in GC 1.3.2 and 578 in GC 2.7, plus two function-context sites and two
range-marker sites per version. Most writes are inline increments, including address, call, instruction
selection, struct-move and spill paths. The remaining writes initialize counters,
rewind scratch counters, restore high-water marks or reset after allocation.

Each original memory-write instruction is replaced with an equal-sized CALL/NOP
sequence. Its call island executes that exact relocated instruction and observes
the result. Original sections, instruction offsets and branch targets stay fixed.
Writes that depend on ESP, unknown widths/address forms, missing sites and changed
bytes fail construction. The function-exit island executes the original stack
adjustment after discarding its own return slot, preserving its flag effects.

`history` in the format-2 result includes functions, births, counter changes,
expression observations, orphan writes and gaps. `history.counter_observations`
reports completeness **within the declared direct-counter scope**. Counter
discontinuities, missing function ends, orphan writes and unsupported schemas do
not become successful captures. Each function has its own coverage result.

`Birth.id` is a capture-local function/sequence pair. `generation` counts earlier
births using the same class/number within that function. `registers[].values`
retains all generations. One-based `trace_position` fields preserve the
interleaving of births, counter changes, expression observations and coalescing
checks. Restoring a high-water mark is not recorded as a birth.
Do not bind every graph use to the latest generation or compare these IDs directly
across source variants: semantic correspondence still needs anchors and ambiguity
handling. Births remain in the history even if no final instruction uses them.

Each birth records the physical threshold, original retail creation site, pipeline
phase, source line and available statement-root expression. Expression byte
spellings use hex in the trace; inspection retains bytes and optional UTF-8 text.
The bounded spellings have `complete=false`, and statement roots are not claimed
to be the exact backend leaf expression. `category=null` means the semantic
allocation category is still unknown. Existing frontend provenance can provide
additional evidence; these fields do not silently invent it.

`Birth.object` adds observed object bindings at the guarded single-register and
two-word assignment sites. It retains the opaque object/variable-info identities,
the object's datatype, storage class, type kind/size, and variable-info flags
**before** the binding writes. Pair parts mean first/second creation order, not a
claim about high/low word order. Names retain bytes, optional UTF-8 text and separate
known/complete flags. Repeated names and pointers are not unique semantic identities.
An absent object binding does not prove that a birth is an anonymous temporary;
other backend creation paths still need exact expression/object provenance.

Sites are canonical addresses in the pinned retail image, not necessarily the
relocated execution address. Typed phases name code generation, optimization,
allocation and their following boundaries. Rust callers can use
`provenance::inspect` for captures/raw traces, `provenance::parse` for raw traces,
and `value_history` directly. Capture inspection skips unrelated rendered-report
data rather than building a second full JSON analysis tree.

## Graphs at their actual instruction boundary

`graphs.graphs` contains snapshots taken immediately before retail graph
simplification (`boundary=before_simplify`). Each snapshot pairs the actual
instruction list at that instant with graph nodes, raw neighbor lists, unique
adjacency degrees, stored degrees, flags and colors. `graph_only` means absent
from those instructions; it does not mean eliminated. `instruction_only` and
`missing_neighbor_nodes` retain the other graph boundaries. Physical thresholds
are class-specific, including SPR and condition registers.

The format-1 `pv.graph.begin/instruction/node/end` records carry a function-local
graph ordinal and birth cutoff. A bounded bit set observes visibility in one scan
of the instruction list. The parser independently checks visibility against the
captured operands, checks node/instruction totals, and compares per-function graph
counts against `pv.function.end graphs=N`. Missing last snapshots, missing ends,
unsupported schemas and contradictory visibility cannot establish full graph
coverage. `graphs.observations` and `graphs.functions[].observations` describe
snapshot coverage separately from birth bindings. Old captures without these
records remain usable but cannot establish that coverage.
Physical thresholds are read from the live target table at the graph boundary.
The parser cross-checks them against the preceding captured coalescer pass when
available. Early development captures that cached zero before target initialization
are now indeterminate rather than misclassifying physical neighbors.

Each node's `binding.direct` retains all candidate birth IDs for its class/number
before that graph, within the current counter-reset epoch. `contributors` adds
origins from confirmed coalescer parent writes. `merges` retains both roots and
their possible birth sets, with the actual decision trace position. Child nodes
and their births remain inspectable even when their instructions disappear.
Future births and merges never explain an earlier graph; class resets prevent
bindings from leaking between allocation epochs. Reused numbers remain ambiguous,
and unknown virtual contributors remain explicit evidence gaps.

A satisfied `binding.evidence` establishes capture-local number/merge binding
within direct-counter coverage. It does not establish cross-variant semantic
identity, an exact leaf expression, or elimination. Full origin and category
evidence must still be connected to comparison and source objectives. Rust callers
can use `graph_history` directly or inspect the combined provenance report.

## Observed allocation ranges

Graph nodes have a typed `range_category`, backed by `range_bounds` and its own
evidence. The categories are `physical`, `initial_object_range`,
`before_coalescing_window`, `inside_coalescing_window` and
`after_coalescing_window`. These name the retail allocator's number ranges; they
do not guess semantic roles such as address, call result or promoted array value.
The initial-object upper bound and coalescing-window bounds are inclusive, matching
the actual retail comparisons. Birth-time `category` remains unknown until a
semantic role is justified; it is distinct from the observed range category.

Two fixed-size islands observe the last stores in the initial-range snapshot and
window-begin functions. `pv.category.initial` and `pv.category.window` retain their
completed tables and birth cutoffs. Function-end `ranges=N` totals detect missing
markers. The parser validates those snapshots against the observed counters and
checks each graph's initial/first bounds against the current function's markers.
Missing markers, stale or overlapping bounds and inconsistent snapshots remain
indeterminate. Graphs also sample the current window's last bound, which can grow
after its opening marker. With `-O0`, retail code can omit both markers; complete
observation of zero markers does not establish range categories.

Range classification and value binding are separate evidence. A reused raw number
may have a definite range category while its contributing value generation remains
ambiguous. A source objective needs both before attributing a category change to a
particular value. These observations are available in Rust through
`allocation_category` and the combined provenance report.

The internal callback is now `OracleRust_CounterProbeV2`, with a guarded 36-byte
descriptor. Rebuild the common hooks and producer together; the linker rejects an
older callback ABI instead of interpreting stale descriptors.

Audit a pinned retail image independently with the native command:

```text
mwcc-tools provenance audit-counters --root ORACLE_ROOT --version GC_2_7 > counter-audit.json
```

The format-1 audit records image/metadata hashes, direct PE references, access
classification, indexed accesses, undecoded/unresolved sites and function guards.
Its `lifecycle_coverage_complete` remains false: the direct-reference audit does
not prove the absence of all computed aliases or bulk writes. A metadata container
or unit name is a link location, not necessarily the semantic function's name.

## Expression-generation dispatch context

```text
mwcc-tools provenance audit-dispatch --root ORACLE_ROOT --version GC_2_7
```

The native audit guards the complete retail initializer and its pointer writes.
It proves 78 initialized entries (0 through 77), rather than trusting reconstructed
array sizes. Function guards retain addresses as well as names: GC 2.7 has two
local functions named `_fn_00453e10`. The constructor distinguishes their bodies
and rejects substituting same-named COFF targets.

The constructor changes only initializer target relocations, adding entry/return
islands that forward the original argument bytes and preserve return registers,
flags, x87, MXCSR and XMM state. Existing sections keep their sizes and instruction
offsets; retail generator bodies remain intact. This uses the actual retail dispatch, not a reconstructed
C prototype. The internal expression descriptor has its own versioned observer;
stale common hooks fail linking with a rebuild diagnostic.

`history.functions[].expressions[]` adds parent IDs, active `codegen_dispatch`
context, original-retail generator target, raw requested argument bits, a tagged
result snapshot, and end/abandonment observations. `births[].expression` names the
most specific observed active scope. Requested argument bits can include unused
upper-half pointer bits; they are ABI evidence, not virtual-register identities.
The relevant-state fingerprint retains the generator target and completed graph,
not those incidental argument bits. Result records read only the fields associated
with the observed Operand tag; unknown tags retain indeterminate result evidence.

`dispatch_scope == "guarded_table_v1"`, end totals and
`dispatch_observations` describe balanced capture of those table calls. Result
evidence, bounded expression spelling and semantic lifetime completeness are
separate. Statement lines can identify an inline caller rather than the original
leaf. Direct helper calls can bypass the table. Dispatch context therefore does
not claim complete exact leaf, inline or source-role provenance, and
`lifecycle` remains indeterminate. Missing result/end records, unknown declarations,
unwinds and depth overflow cannot establish complete relevant capture.

The [real EnvModulate case](../oracle-source-guided-case-envmodulate.md) connects
symbol-returning leaves, later address-register births, symbolic PCode, confirmed
r4 merges and invisible graph nodes that remain adjacent to `noRota`. It also
records three unsuccessful, behavior-justified wrapper experiments.

## Validation

`tools/migration/verify_backend_probes.py` runs each selected input with probes
disabled/enabled, checks stock-object equality and unchanged existing traces, then
independently invokes the native explanation command to validate every candidate
path, direct-counter history and graph-phase capture. It compares the new node
records against the independently emitted existing graph records, preserving raw
neighbors and degrees. Full outputs, identities, flags, hashes and elapsed times are retained in a
private output directory. `--case` selects fixture IDs. It also accepts `--from-ninja`, `--source` and
`--project-only` for complete game units. This is a development audit; Python is
not a product runtime dependency. A successful trace comparison is not a measured
overhead guarantee or a claim that every rare rejection branch has a live fixture.

The scratch-reuse fixture preserves a compiler mechanism rather than physical
register choices: with `-O0`, it produces 239 births, one rewind and twelve reused
numbers on all supported versions/hosts. The complete Echoes particle unit records
6,356 births and 4,325 partial expression observations with stock-equivalent output
on both hosts.

The current graph checks retain 182 snapshots from that unit, with 2,627 node
observations absent from the instructions at their respective graph boundaries.
These are retained observations, not a count of eliminated values.
The same unit now records 1,750 observed object bindings, including eight pair
words, and exercises initial-object, before/inside/after-window range categories
on both hosts. The separate 64-bit object-binding fixture retains eight bindings
and two range categories on every supported version/host.

Continue with semantic lifetime correspondence, categories and complete origins before
using absence predicates. The complete implementation scope remains in the
[source-guided roadmap](../oracle-source-guided-search.md).
