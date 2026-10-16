# Source-guided oracle tools: implementation status

Updated 2026-10-17. The active objective is to implement all four requested tools
using the [adopted roadmap](oracle-source-guided-search.md). The objective is not
complete. The completed Rust migration remains a separate release.

## Current extension: lowered call arguments and retained reproducers

V6 adds [call-argument provenance](oracle/call-arguments.md): exact lowered
contexts, cached callee links, nested dispatch/register/stack/ForceGPR episodes,
tagged operands, exclusive birth ownership and bounded PCode intervals. Independent
function-end totals detect missing streams. `event_totals` reconciliation remains
separate from complete observation of every call, so an unrelated PCode gap cannot
erase a separately complete call. Unknown earlier callees still make occurrence
selectors ambiguous. The [producer contract](oracle/producer.md) records the actual
six-word assignment ABI, guarded 48-byte context and native-only hook inventory.

The shared `call_argument_address_pairs` objective selects an exact qualified
callee, occurrence and lowered-context ordinal, then counts guarded Normalize
pairs through that argument's episode subtree, including nested calls. Search,
reduction and corpus queries share it. Fingerprint format 4 retains these
observations, coverage and pointer equality/order relationships to the existing
backend evidence. `source-index origins` inventories cached links, known/unknown
callees, bound contexts and episode counts.

Fresh full Render captures bind four void `SetTevOp` calls to address pairs and
the fifth reference argument to zero Normalize births. The first call's lowered
ordinal 1 is planned for GPR4 and owns Birth54:360/361, GPR219/220, for
`kEnvModulate`. Call512 instead materializes `sParticleModulatePass`. The matched
`Render2D` reference argument has zero such births. These are argument-creation
observations, not call results or proof of final ABI locations. Source/object
bytes agree with the retained V5 cases. Actual nested PCode block transitions
remain explicit gaps. The tiny reference-only function has no GPR graph, so its
graph-bound count objective remains indeterminate despite complete call
observations; an explicit no-allocation boundary contract remains future work.

The new [source-repro API](oracle/source-repro.md) exports retained search or
reduction cases with raw dependencies, compiler runtime, original contract,
declared native checker inputs and separate original/replay analyzer identities.
Offline verification executes nothing. Replay uses a new relocated workspace
and requires fresh stock checks, exact retained objects/raw closure, matching
compiler context and the original predicate/purpose gates. Initial support is
same-host OS/architecture with dependencies/include roots under the recorded
working directory and supported options. Complete transitive checker and OS
runtime packaging remains outside this bounded contract.

The V6 baseline workspace suites passed on both hosts: **193 passed, 11 opt-in tests ignored**.
All-target Clippy with warnings denied also passes. V6 producers pass 36 C/Rust
comparisons, 45 backend off/on pairs, nine scalarization cases and 91 constructor
corruption negatives per host. Three additional argument off/on pairs per host
have equal stock objects, common-hook COFF and typed role records. All five
fixture callees have complete cached names; its nested case preserves its actual
bounded PCode gap. Current test/lint logs and frozen analyzer identities are in
`build/source-guided/call-argument-audit/{final-validation,final-bin}/`.

The final source-search regression also passes all three compiler versions on
both V6 hosts, including grouping/cache, resume, independent replay, header drift,
semantic gates, cancellation and tampered inputs. Its copied test executables are
the run analyzers; they are recorded separately from the final CLI. Nineteen
retained objective evaluations agree in full between native hosts: seven
satisfied, three observed failures and nine deliberate indeterminate cases.
The full Render address/local/reference and matched Render2D reference objectives
are satisfied with required symbolic relocations. These evaluate existing source
cases; they do not establish a new equivalent RenderParticles transformation.

Final native GC2.7 reproducer proofs pass on each host after disconnecting both
private original project/run paths and moving the bundle. Three fresh compilations
retain exact object/raw closure and stock checks; missing blobs fail verification.
Five integrity tests per host also cover reduction export, while live relocated
replay covers search. Receipts are `build/rpw-v3/` and `build/rpl-v2/`; the V6 search
and cross-host objective receipts are under `call-argument-audit/`.

The native Windows/Linux analyzers normalize the same new Windows full Render
capture to format-4 fingerprint
`ceff10d256236a0497e6b3d53851fc6d9ff8d2cbcbe03c0bd7bfc0b9d95e4aee`.
This is cross-host retained analysis, not an independent Linux full-unit compile;
the new tiny/backend/scalarization fixtures were independently compiled on both
hosts. Earlier failed/intermediate producers and reproducer attempts remain
separate diagnostics. The [source-write audit](oracle-source-origin-audit.md)
identifies guarded transformation edges still needed for original assignment,
declaration-initializer and inline-leaf ancestry. Reused CSE indices, source lines
and an active initializer callback are insufficient joins.

## Original-source bridge work after V6

The native `provenance audit-source-arenas` inventories all five retail pools,
local lock and failure callback, with input hashes, containing-function guards
and unresolved direct references. Both native hosts agree on complete reports
and 66 retail listings across the three compiler versions. The retained proof is
`build/source-guided/source-origin-bridge/arena-audit/{windows-final,linux-final}/`.
This is static evidence: both runtime coverage flags remain false. Inlined global
allocation in name interning bypasses `galloc`; locked `freelheap` does not reset
local allocations; selecting optimizer space after freeing global space is not
an optimizer reset. See [source arenas](oracle/source-arenas.md).

The [inline audit](oracle-inline-clone-audit.md) identifies exact frontend owners,
ordinary source-statement emission, deferred initializer callbacks, clone mode
substitutions and external switch-table guards. The
[IRO audit](oracle-iro-origin-audit.md) verifies 66 retail/split bodies and 45
scalar-use replacement sites. Later compiler versions contain private ordinary
functions inside coarse split symbols. Constructor-only hooks miss direct
allocations; lifetime identity alone cannot cover destructive in-place rewrites.
These audits resolve implementation sites without claiming a runtime original
source-write path.

The host `source_lifetimes` API now tracks exact requested/reserved allocation
spans, pool epochs, guarded invalidations and capture-bound private receipts.
Unknown end totals invalidate affected earlier receipts retrospectively;
precisely located loss is a separate operation. Cross-pool overlaps and malformed
spans cannot leave conflicting current identities proven. An active interval
index avoids historical scans in normal operation. This is a tested foundation,
not a V7 producer or a parser for V6 pointers. See
[allocation lifetimes](oracle/source-lifetimes.md).

The [new bounded source family](oracle-source-discovery-envmodulate.md) retains
three checked full-unit variants with unchanged noRota source and headers. Late
pointer/reference reuse removes one graph node; call factoring removes two.
All reduce three EnvModulate pairs to two, so the stronger maximum-one objective
remains unsatisfied. One selected late-reference argument has zero pairs while
another argument still owns the remaining materialization. Those are scoped
mechanism facts, not a final retail match or original-source lifetime proof.

Count objectives now score distance to the requested interval rather than giving
all failures cost one. A three-pair baseline with maximum one costs two; a
two-pair candidate costs one. Unknown evidence stays unscored, hard constraints
still determine eligibility, and only actual satisfaction can trigger independent
winner verification. The retained source experiments use their original frozen
V6 analyzer and keep its original scoring results.

Fresh native analyzers agree on all four complete score reports from those
retained Windows captures: costs are `[2,1,1,1]`, placing the three partial
improvements before their baseline. The separate mechanism-only score audit
reruns no compiler or semantic checker. Exact reports, analyzer hashes and the
cross-host receipt are under
`build/source-guided/source-origin-bridge/score-audit/`. It changes neither the
old source-transformation run nor its success status.

A separate [caller-proven helper specialization](oracle-source-discovery-helper-specialization.md)
now reaches the stronger objective: two EnvModulate pairs become one, graph
nodes fall from 553 to 551, and the named noRota degree falls from 157 to 155.
The native harness independently recompiles the candidate, passes all nine hard
gates and the closed-source checker, and retains equal candidate/verification
objects. The recipe uses the original public setters in the helper's proven else
branch, without changing headers, noRota or the ABI. This is a Windows GC2.7
full-unit source-transformation proof; the diagnostic target is the clean-source
baseline object, not retail. It does not establish semantic lifetime elimination,
direct initialization or a category transition.

Current workspace suites pass on both hosts: **208 passed, 12 opt-in tests
ignored**, with all-target Clippy warnings denied. The opt-in retail arena test
also passes on each host across all three compiler versions. Logs are in
`build/source-guided/source-origin-bridge/validation-v1/`. The first Windows
preflight exposed an inherited nonblocking accepted socket in the HTTP fixture;
the fixture now explicitly uses blocking request reads with its existing timeout.
Initial diagnostics remain separate from the passing current logs.

The next source-provenance implementation step is guarded runtime allocation and
invalidation capture with independent totals feeding the lifetime ledger, then
immutable source-node observations and actual inline/IRO replacement edges.
Neither pointer equality nor the new successful source recipe closes those
remaining original-source and semantic-lifetime requirements.

## Previous extension: frontend scalarization and real source examples

V5 observes actual aggregate-part temporary returns, original byte/bit intervals,
Object/VarInfo/type identities and independent function-end totals. The native
`scalarization` API joins these to backend births only within the exact observed
function and complete temporal scope. In the full outside-loop-store
`RenderParticles` case, `noRota[1]` creates the scalar that becomes GPR 162; the
older promoted nodes remain unused. Nine small cases cover all three supported
versions, including six positive joins and three controls. The real computation
is not a constant LI, so the narrow scalarized-initialization objective remains
indeterminate. See [scalarization provenance](oracle/scalarization.md).

V5 introduced format-3 fingerprints retaining whole-function frontend
bindings, including creations without graph nodes. Unsupported normalization
disables grouping. They also retain preallocation definitions, CFG and actual
coalescing evidence: equal final graphs can have different definition predicates.
The fingerprint API now requires the full compiler capture. A regression audit
also corrected the initializer recognizer's ability to select a later assignment;
supported initialization uses one instruction-bearing block with verified linear
empty scaffolding. The separate `scalarized_array_definition` objective takes an
explicit observed ordinal and supports non-LI computations without claiming C++
initialization or dominance. Promoted-element provenance anchors also associate invisible
nodes across variants, while keeping lifetime/elimination indeterminate.

The matched-source index now inventories retained origin coverage and supports
source ranges in captured headers. Fresh exact-retail examples include the inline
`CParticle` constructor's symbolic member initialization and both `Render2D`
overloads. The pointer overload passes a global reference through an SDA load
directly to `SetTevOp`, with zero Normalize materialization pairs for that symbol.
It is a useful source construct; the current trace does not prove the absence of
all call-argument temporaries or establish an equivalent RenderParticles rewrite.
See [the corpus guide](oracle/source-index.md).

[Measured overhead and search throughput](oracle-source-guided-benchmarks.md)
now cover both hosts/all compiler versions with explicit workload and concurrency
limits. A separate paired Windows retained-trace experiment reduced full inspection
time from 22.9 to 10.3 seconds by classifying record families before allocating
unrelated fields. This is a retained-analysis measurement, not a V5 compilation
or source-search speed claim.

The V5 baseline validation passed on Windows and Linux: **172 workspace tests**, ten
opt-in integration tests skipped by default, all-target Clippy with warnings
denied, and formatting checks. Separate V5 producer checks pass 36 legacy
comparisons, 45 backend probe off/on pairs, nine scalarization cases and 63
constructor corruption negatives per host. The native live search test was run
separately on all three versions on both hosts, covering grouping/cache, resume,
fresh verification/replay, dependency changes, semantic gates and cancellation.
Its final linear initializer fixture replaces the earlier unsupported branching
positive while retaining the branching fixture for grouping/cache tests.

Final definition replay gives six satisfied live-scalar cases and three
indeterminate original-promotion controls on both hosts. The full GC 2.7
`RenderParticles` capture satisfies observed definitions zero and one; definition
two is unsatisfied, and automatic constant initialization remains indeterminate.
The final Windows and Linux analyzers normalize the same full retained capture
to identical fingerprint
`65b2ea359dd6b82a6c78049274d924da58166c9e5f14a22e2e1d043f700d817c`.
This is cross-host analysis of the Windows full capture; the nine small compiler
cases and broad backend fixture captures were independently produced on both hosts.
Current evidence is under `build/source-guided/scalar-origin-audit/`,
`scalar-definition-audit/` and `build/ssv5-b58363d2/`. The short Windows live-run
root avoids an observed retail compiler path-length failure; its earlier failed
long-path run remains retained. The live initializer test binaries precede the
final MR/FMR-only guard fix in the separate observed-definition predicate; current
unit/CLI suites, lint checks and definition replays include that fix.

## Implemented foundation

- Shared Rust `evidence` types distinguish satisfied, unsatisfied and indeterminate
  results, with machine-readable evidence-gap codes.
- `colouring::target::remap_snapshots` exposes all operand votes and can align
  either existing instruction snapshot. It preserves losing candidates for audit.
- `colouring::correspondence::compare` compares all captured graph nodes, including
  instruction-invisible nodes. It retains raw neighbor records, unique adjacency,
  stored degrees, absent neighbor nodes and instruction-only register indices.
- Correspondence is explicitly inferred, ambiguous, colliding, projected to a
  physical register, missing a target node or unmatched. Many-to-one instruction
  votes are not reported as observed merges. Boundary edges remain unclassified;
  they are not silently treated as removed/added.
- Native `colouring remap --json` uses format 2 and the new comparison. Explicit
  class-round and before/after snapshot selectors are supported. Missing snapshots
  remain explicit and bad requested round indices fail before emitting JSON.
- The old mapped-subgraph projection remains for historical reference fixtures;
  production remap and future source objectives use the evidence-aware interface.

The new native [provenance comparison](oracle/graph-correspondence.md) compares
actual `before_simplify` graphs. It combines unique complete named-object and
qualified-symbol anchors with instruction votes, reports only injective conditional
hypotheses, and retains conflicts, complete nodes, multiplicities and unclassified
boundary edges. Actual capture-local merge chains remain separate from hypotheses.
The shared objective's graph diagnostics now use this comparison.

Elimination remains **indeterminate**. These anchors do not establish complete
semantic graph lifetimes. Capturing births does not turn unmatched nodes into elimination proof.
Snapshot names remain existing boundaries, not new backend-phase instrumentation.

## Retail instrumentation

The [backend audit](oracle-backend-provenance-audit.md) identifies the coalescer and
eleven candidate branch/write probe sites in all three supported retail versions.
Complete decoded function bytes match after masking only PE HIGHLOW relocations
and direct-call displacements. The actual predicates are inlined and include an
additional GPR/special-register guard; using reconstructed C helper signatures
would not faithfully observe them. The native Rust linker now installs fourteen
guarded observations while preserving the retail function body and control flow.
The common Rust observer records actual branch inputs/outcomes, pass/candidate
context and confirmed union-parent writes.

Native `provenance audit-counters` now identifies every direct PE counter reference
in the three pinned images. The constructor observes 570/571/578 actual writes,
plus two function-context and two range-marker sites per version, using fixed-size call islands. It
checks complete retail container bytes/relocation layouts and write targets;
original instruction offsets, section sizes, symbols and jump tables stay fixed.
The islands execute the original writes, not a reimplementation of allocation.

The Rust observer records capture-local function/birth IDs, counter changes,
pipeline phases and partial statement-root expression contexts. The host preserves
all generations when numbers recur and distinguishes increments from resets,
rewinds and high-water restores. Orphan writes, discontinuities, missing function
ends and unsupported schemas cannot establish complete direct-counter capture.
Semantic categories, precise leaf/inline origins and semantic lifetime binding remain
incomplete; direct PE-reference coverage is not a complete alias-analysis proof.

Guarded single-register/pair creation sites now attach observed object and
variable-info identities, type/storage information, flags before binding, and
byte-preserving known/complete names. Completed initial-range/window markers and
current graph bounds supply typed `range_category` evidence. These are actual
allocator number ranges, separate from source roles such as address/call-result
temporaries. Range marker totals, counter consistency and stale/overlapping bounds
are checked; paths such as `-O0` without markers remain indeterminate.
The internal counter descriptor now uses a versioned callback ABI; stale common
hooks fail linking with a rebuild diagnostic.

The graph hook additionally observes current instructions and graph membership
immediately before retail simplification. Snapshots retain actual visibility,
graph-only nodes, stored degrees, ordered/duplicate adjacency, missing neighbors,
class-specific physical thresholds and a birth cutoff. Function-end graph totals
detect missing snapshots. Capture-local bindings retain all possible generations,
confirmed merge contributors and explicit unknown/ambiguous origins, using reset
epochs and actual decision positions. Future births/merges cannot bind earlier
graphs. Semantic lifetime correspondence remains unfinished.

`oracle compile-trace --backend` enables these records; `provenance inspect`
returns format-2 typed decisions, birth/reuse histories, saved inputs and separate
decision, direct-counter, graph-snapshot and aggregate lifecycle coverage.
It validates sites, EFLAGS, short-circuit paths, physical-register window bypass,
counts and roots. Unsupported schemas, incomplete paths and absent captures remain
indeterminate. The [native API](oracle/provenance.md), trace reference and kit guide
are updated. Existing pinned migration releases are unchanged.

Capture/disassembly evidence is retained under
`build/source-guided/backend-audit/`. The capture helper records compiler/function
hashes and instruction guards and is explicitly a development audit, not a product
runtime dependency. Runtime proofs are retained under `build/source-guided/probes/`
and the newer `build/source-guided/counters/`, `build/source-guided/graphs/`
and `build/source-guided/categories/`
(Windows), with corresponding Linux proofs under
`/home/henrique/.cache/mwcc-counter-probes/` and
`/home/henrique/.cache/mwcc-graph-probes/` and
`/home/henrique/.cache/mwcc-category-probes/`.

## Dispatch provenance and a real symbolic-address case

The native expression-table audit and constructor now guard all 78 retail entries
and 43 initializer writes across the three versions. Machine-state islands preserve
original argument bits and return state. They add nested dispatch context, tagged
operand results and per-function capture totals. Counter births attach to the
active observed dispatch, rather than only a whole statement. Duplicate GC 2.7
local metadata names are retained and disambiguated by address/body guards.

The [real EnvModulate investigation](oracle-source-guided-case-envmodulate.md)
independently compiles the clean full particle source on both hosts, preserving its
stock object. RenderParticles has 4,080 balanced dispatches and 1,104 births: 760
have dispatch context, 42 only statement context and 302 neither. Three symbolic
leaves return a symbol without allocating; their parent calls materialize address
pairs. The three low halves merge into r4 and disappear from instructions while
remaining degree-7 graph nodes adjacent to `noRota` and the zero-vector address.

Three private reference/pointer forwarding variants retain the complete numbered
graph and instruction body. Required symbolic relocations remain; independent
objdiff reports 100% against baseline and 99.94901% against target. Those variants
produce no useful graph edit. A fourth, target-ABI-specific pointer/integer
round trip at the first call also retains that graph and body. Their exact source/header/runtime inputs,
objects, traces, remaps and comparison evidence are retained. This is direct native
case evidence, not a claimed source-search winner.

The subsequent V3 counter observer adds direct Operand/Object/symbol bindings at
the two guarded Normalize address-pair birth sites. It guards the retail cache
layout difference (56 in GC 1.3/1.3.2, 54 in GC 2.7); it does not invoke the mangler.
Both hosts' corrected full-case captures preserve the stock object and retain 32
address-half bindings with complete names, including all six EnvModulate halves.
Each pair matches an observed expression output, with alias/dataflow limits explicit.
The new `symbol_address_pairs` evaluator counts the selected qualified-symbol
mechanism with complete capture and unique graph-binding requirements. A real
native harness run passes calibration and all hard gates, but the cast variant
still has three pairs against a maximum of two: `bounded_search_exhausted`, with
no winner. That initial analyzer reports unsupported PCode kind 1 separately in
`fingerprint_error`.
Exact argument/inline origins, other source roles and semantic lifetimes remain
incomplete. Pair-path counts do not prove complete temporary elimination.

A subsequent native search finds a checked shared-const-pointer rewrite. It
reports `verified_success` after private/original baseline calibration, candidate
compilation and fresh independent verification. The two direct call sites reuse
one local const pointer to the same global object; the third inlined use stays
unchanged. Observed EnvModulate pairs fall from three to two, adjacent address-half
nodes from six to two, and uniquely anchored `noRota` degree from 159 to 156.
All hard constraints and the closed source-rewrite check pass. An independent
Linux compile has the same complete stock object, numbered graphs and stable
materialization bindings. Final matching worsens to 99.491905% against retail;
this useful graph experiment is not a matching fix and is not applied to the game.
The two additional generated array recipes were not compiled or tested.

Retained-state normalization now supports the observed fixed GQR read operands
at PSQ_L/PSQ_ST, preserving all bytes and all eight hardware selectors. Other
kind-1 forms still fail explicitly. `source-search state` validates a saved
capture/object and selected graph, then reports a fingerprint or an indeterminate
normalization evaluation. It performs no compilation, dependency rescan or semantic
check. The candidate, fresh Windows verification and independent Linux capture
share one fingerprint; baseline has another. Generated local-symbol suffixes
remain a conservative grouping limitation. Proofs are under
`build/source-guided/address-reuse-audit/` and
`/home/henrique/.cache/mwcc-address-reuse-audit/`.

## Scalar-array and call-result origins

The V4 producer guards two scalar `COpt_ArrayToRegister` increment sites and six
`FunctionCalls_GenerateCall` result sites per compiler version. New typed records
bind each scalar promotion to its original array Object, complete name/type,
element index/size and checked bounds. Call records bind the original call ENode,
return type and single/paired result role, with a bounded partial expression.
Output bytes are explicitly incomplete: only the Operand tag is initialized at
these birth sites. Separate scoped totals detect missing bindings. Vector-array
promotion and full expression/inline/lifetime ancestry remain open.

The [promoted-flag case](oracle-source-guided-case-promoted-flags.md) captures both
historical real RenderParticles array candidates and a small cross-version
reproducer. The second element is created above the completed coalescing window;
its incoming copy passes interference and is rejected by
`destination_above_window`. A new `promoted_copy_coalescing` objective binds the
source element to its unique birth/graph node and tests an actual incoming-copy
decision. It does not infer source initializer identity. All three versions have
positive first-element coalescing and second-element rejection proofs on both
hosts. Two store/helper variants retain unused promotion births but move the live
value elsewhere; the evaluator correctly leaves those cases indeterminate.
Twenty-three bounded standalone variants and a full-function outside-loop store
experiment are documented. The full rewrite removes the move and worsens final
matching; no matching fix or game edit is claimed.

Fingerprint format 2 includes initialized scalar-array origin and partial call
shape/type evidence, excludes capture pointers/inactive bytes and masks unused
array flag bits. Native `source-search evaluate` applies contracts to retained
captures/source/object files without compilation or semantic checking. Static
`range_category` is available to search, reduction and matched-source queries;
the instruction-preserving `change_range_category` transition remains distinct.
Shared evaluation validates object/source hashes, exact report/trace binding,
supported compiler context and explicit verified-identical stock status.

## Native source-search harness

The new [`source_guided` API and `source-search` command](oracle/source-search.md)
connect observed object/range evidence to a shared objective evaluator. Strict
versioned plans select an exact function/class/round, unique named object/type/part,
required symbolic relocations and mechanism-only or checked source-transformation
purpose. Predicates currently recognize direct constant initialization through
final allocation and observed range-category changes with instruction/relocation
preservation. They require complete selected-function graph counts, stock object
equality and matching capture context. Elimination remains indeterminate.

The harness applies bounded atomic source recipes, scores eligible objectives,
records inferred graph changes separately from final target mismatch, groups
relevant compiler state and retains every source representative for further
expansion. Exact compilation caching is separate: oracle and stock dependency/
preprocessing scans, raw headers, compiler/DLL/loader files, flags, options, working
directory and environment hashes bind artifacts. Input bytes are retained and
rescanned after compilation. Analyzer identity is checked once per invocation.
Private-baseline compilation is calibrated against the original context.

Atomic checkpoints support budgets, cancellation and resume without advancing past
a pending candidate. Compiler/checker process trees are terminated on cancellation.
Satisfied candidates require a fresh independent compile and semantic check when
configured. Replay makes three fresh builds and rechecks the predicate. Source
transformations without semantic checks cannot succeed; mechanism-only outcomes
are explicitly labeled. Checker script/data dependencies must be declared.

Synthetic live tests cover syntax variants reducing to one relevant compiler state,
actual exact-source cache hits, changed-header invalidation, pending verification
across resume, independent replay, missing semantic checks, cancellation and saved
input corruption. The successful constant-initializer fixture deliberately changes
behavior. It proves orchestration and evidence gates, not a semantics-preserving
transformation or a useful RenderParticles discovery. Recipes are guarded text
edits; automatic language-aware rewrite generation and portable repro export are
not implemented. Native help and the kit source guide use the new structured API.
Pinned migration packages remain unchanged.

## Native reduction and matched-source index

[`source-reduce`](oracle/source-reduce.md) now reuses the search execution helpers
and shared evaluator. It tries guarded shrinking recipes and deterministic
coarse-to-fine line deletion. The original must satisfy the mechanism; every
accepted smaller case must reproduce its exact input snapshot and stock object
in a fresh independent compile and satisfy all configured gates again. Pending
verification survives budgets/resume, accepted candidate-only dependencies are
rescanned, and replay performs three fresh builds. Results distinguish a fixed
point of configured passes from global minimality and leave transplantation
unvalidated. Static range-category and promoted-copy predicates are supported;
category-transition reduction and stronger lifetime elimination remain open.

A real bounded reduction removes an unrelated 222-line constructor from the
winning full CElementGen source: 117,085 to 110,801 bytes, with exactly two
EnvModulate pairs and required HA/LO relocations still observed. Four builds
include private/original calibration and independent verification; the fresh
object reproduces. This is explicitly mechanism-only and not a game change.
Evidence is retained under `build/source-guided/reduction-probes/particle-windows/`.

The new promoted-copy predicate also passes a native GC 2.7 reducer run: 1,117 to
255 bytes, 59 candidates and six independently verified acceptances. Fresh replay
uses three new builds and reproduces the exact final stock object. The selected
register/window changes from 44/43 to 41/40 while the ordinal-zero
`destination_above_window` rejection and required `observe` REL24 relocation
remain. A fresh ghost baseline is rejected as indeterminate with no bound incoming
copies. This is a mechanism reproducer, not a semantic or transplant claim.
Proof: `build/source-guided/promoted-reducer-audit/verification.compact.json`.

[`source-index`](oracle/source-index.md) admits only exact function bytes and
symbolic relocations against a supplied target. Entries retain complete source,
dependency/runtime blobs, compiler flags/identity, declared revision/range and
separate provenance coverage. Queries reuse the shared static mechanism evaluator;
incomplete absence evidence cannot win. Fresh verification applies the same
filters, requires the exact environment/input snapshot, recompiles stock/oracle,
rechecks the reference match and reevaluates the query. Unsupported relocation
representations and ambiguous/section-symbol references fail conservatively.
Revision/range association is declared metadata, not automatically proven ancestry.

The real clean CElementGen corpus admits 35 strictly matched functions. A query
requiring an actual `sStaticListInitialized` relocation finds Initialize with zero
pairs on the guarded Normalize path. An EnvModulate query requiring its relocation
finds no qualifying example. These results exercise a real matched-source corpus;
they do not discover the desired symbolic-address argument behavior. Input drift
is rejected, including an attempted fresh verification whose environment hash
differs from the older retained capture.

An independent review caught and corrected verification filter bypass, unsupported
REL relocations, effective Ninja-flag handling and mutable checker/proof inputs.
Checker inputs are checked around execution; mutable analysis inputs are checked
before completed search/reduction outcomes are saved. Analyzer identity is still
hashed once per invocation.

## Validation

V4 producers pass 36 C/Rust migration comparisons per host and 45 backend
off/on pairs per host (90 captures), covering all three compiler versions,
scalar GPR/FPR promotions and all six call-result paths including vector and
paired-single results. Stock objects and all existing non-provenance trace
records agree. Cross-host object hashes and typed summaries agree for all 45
cases; common-hook and counter-island COFF bytes also agree. Proof roots are
`build/source-guided/birth-origin-audit/` and
`/home/henrique/.cache/mwcc-birth-origin-audit/`.
New native comparison reports for the real baseline/shared-pointer source pair
are identical across hosts: 555 to 554 nodes, 292 conditional injective pairs,
no conflicts, and the anchored `noRota` degree changes from 159 to 156. Neighbor
changes outside the mapped subgraph remain unclassified. Proof:
`build/source-guided/graph-correspondence-audit/verification.json`.
The final gated evaluator's six positive, two standalone ghost-negative and one
full RenderParticles ghost-negative retained evaluations agree in full across
hosts under `build/source-guided/promoted-copy-final-gated-audit/`. All retained
source/object/report-trace gates pass; ghost objectives remain indeterminate.
The current full workspace suites pass on both hosts: 148 tests, ten ignored
compiler-backed tests in the default suite, and all-target Clippy without warnings.
Exact V4 constructor preservation/tamper tests pass separately on both hosts/all
three versions. Relative SDK input-scan destinations also work with a distinct
compiler working directory on both hosts. Logs and summaries are retained under
`build/source-guided/birth-origin-audit/`.

127 workspace tests pass on Windows and Linux, with ten compiler-backed tests
left opt-in. All-target Clippy and final focused index checks pass on both hosts;
logs are under `build/source-guided/tool-integration/`. The new checker test
rejects a proof file mutated by the checker and separately detects target drift.
Three reducer tests cover deterministic byte-preserving shrink order, guarded
recipes and saved pass cursors. Two index tests cover exact bytes/relocations,
unsupported relocation forms, complete input closure, indeterminate absence,
query/verification filters, cancellation and entry/blob integrity.
Real native index ingestion/query/fresh verification passes on both hosts;
`Initialize` reproduces the complete clean stock object and the same matched
function/source excerpt. Summaries are
`build/source-guided/matched-index-audit/verification.json` and
`build/source-guided/matched-index-linux/verification.json`.
The expanded reducer live proof passes all three compiler versions on both hosts:
each automatic pass reduces 534 to 281 bytes over 59 candidates and 60 builds,
with identical reduced source bytes across hosts/versions. Guarded shrinking,
cache/resume, fresh verification, three-build replay, candidate-only header drift,
missing semantic checks and cancellation all pass. Windows proof root is
`build/source-guided/tool-integration/source-reduction-8oyCev`; the final Linux
summary is `build/source-guided/reduction-probes/linux-final-verification.json`.
An earlier Linux attempt lost its running test executable to a concurrent Cargo
rebuild and is retained as a failed harness diagnostic. The final proof uses a
frozen copied executable and does not rebuild or replace it while running.

The live source-search test was also run on both hosts across all
three supported versions. It exercises relevant-state grouping, real cache hits,
header invalidation, verification across resume, independent replay, semantic
evidence gates, cancellation and retained-input integrity. Native CLI run/replay
also passes on both hosts using the guide's mechanism example. Six offline tests
cover objective gates and uncertainty, original-initializer/final-color tracking,
ambiguous/missing origins, data-sensitive state grouping, atomic byte-preserving
edits and actual dependency path identities.
Final source-search proofs are retained at
`build/source-guided/search-probes/windows-final-harness/source-search-70XrAk`
and `/home/henrique/.cache/mwcc-source-search-proofs-final-harness/source-search-gu1EGK`.
Both include candidate-only header drift rejection during replay. Full-suite and
live logs are under `build/source-guided/search-probes/*-final-harness-*.log`.
Retail coalescer/counter rewrite tests were also run on both hosts;
they check all three versions, exact current rewrites against retained runtime
objects, unchanged neighboring sections/relocations, foreign call targets and
rejection of changed code/layout, off-site function changes and relocation targets.
All-target Clippy passes on both hosts.
Dispatch constructor tests were run on both hosts/all three versions, including
same-named target substitution, changed loop bounds/pointers, and unchanged original
sections/relocations. Three new history tests cover nested dispatch/result records,
all observed Operand tags, missing coverage and separate output uncertainty. A
fingerprint check preserves generator identity while ignoring unused argument bits.
Fresh dispatch producers pass 36 C/Rust fixture comparisons and 48 probe off/on
compiles on each host. The source-search live test also passes on these producers.
Proofs are under `build/source-guided/dispatch-audit/` on Windows and
`/home/henrique/.cache/mwcc-dispatch-{producer,enabled,source-search}/` on Linux.
The newer corrected materializer producers pass 36 C/Rust fixture comparisons and
54 probe off/on compiles per host (including the new symbolic-address fixture).
Constructor tests pass on both hosts/all versions against those exact retained
islands. Two history tests cover source operands, pair roles, incomplete symbol
identities, stale/changed outputs and missing materializer coverage; one objective
test checks scoped counts, hard uncertainty gates and fingerprint identity fields.
Proofs are under `build/source-guided/materializer-audit/` and
`/home/henrique/.cache/mwcc-materializer-audit/`. Full-case native search, the actual
stock object and independent Linux symbolic bindings are retained there. The
initial wrong-offset producer is retained only as failed diagnostic evidence.
The new fixed-quantizer fixture adds six stock-verified probe off/on compiles on
each host, covering all eight GQR selectors at both observed opcodes on all three
compiler versions. Two core tests check exact fixed-operand normalization,
unsupported forms and retained-capture validity; a CLI test checks structured
positive/indeterminate results and rejection of invalid objects/selectors.
Both full suites and all-target Clippy pass after these changes. The full-unit
shared-pointer candidate and its independent verification are stock-checked;
the Linux object, graphs, materialization bindings and fingerprint agree.
Three core provenance tests and one CLI test cover complete/missing paths,
unsupported schemas, observed-write consistency, malformed outcomes and filtering.
Four value-history tests cover actual identity semantics: multiple generations,
rewinds/restores, nested function namespaces, partial origins, missing data,
unsupported schemas, gaps and malformed expression bytes.
Five graph-history tests cover actual-phase visibility, reused/future generations,
reset epochs, merge chains, missing contributor births, class-specific boundaries,
missing snapshots and inconsistent or partial captures. A CLI test checks that
link-name filtering keeps graph and function-coverage views aligned.
Four allocation-category tests cover inclusive boundaries, missing/stale range
evidence, object pair parts and byte names, plus graph/coalescer physical-boundary
agreement. This caught and fixed an early graph-hook bug: physical limits can
initialize after generator entry and must be read live. Older captures whose
cached zero disagrees with their coalescer header now remain indeterminate.
Five comparison tests
cover graph-only nodes, boundary edges, degree discrepancies, majority-vote
ambiguity, colliding targets, physical projections, changed rounds and absent
snapshots. A new native CLI test verifies selectors, version-2 results and error
output. Existing allocation/search/decoder fixtures continue to pass; legacy remap
prose/JSON parity is not required.

Fresh producers pass 36 C/Rust fixture comparisons on each host. Another 48
fixture compilations per host compare probes off/on across all three
versions: stock objects and existing trace records agree, and all coalescing paths
are explained. The full Echoes `CElementGen.cpp` unit passes off/on on both hosts
with 182 complete coalescing passes, 6,356 births, 4,325 partial expression
observations, no counter gaps/orphans, and identical stock objects/existing traces.
Both hosts also pass the new graph checks: the fixtures contain 48 graph snapshots
with 204 nodes absent from their current instructions, 273 object bindings and
eighteen paired words. The new object fixture has eight object bindings, six paired
words and before/inside-window categories on every version/host. The full particle unit has
182 snapshots, 2,627 such node observations and 6,662 capture-local number/merge
bindings. Those are snapshot observations, not distinct eliminated values.
The current full-unit captures on both hosts retain 1,750 object bindings,
including eight paired words, and exercise initial-object, before-window,
inside-window and after-window categories with complete relevant range evidence.
The reuse fixture's twelve reused numbers each retain both birth candidates and
remain indeterminate. Fresh source-equivalence builds on both hosts match all
three retained producer objects exactly.
The new scratch-reuse fixture gives 239 births, one rewind and twelve reused
numbers on all three versions/hosts, without asserting physical register choices.
The capture includes RenderParticles; these earlier off/on checks are separate
from the later successful graph-objective experiment. Controlled workload measurements
are now documented in the [benchmark report](oracle-source-guided-benchmarks.md).

## Required work still open

| Roadmap stage | Remaining work |
|---|---|
| Reproducible cases/contracts | Symbolic-address, flag and second-promoted-element mechanisms have retained real/standalone cases; bounded same-host portable reproduction is implemented; broaden supported path/checker closures and strengthen semantic lifetime identity |
| Backend provenance | Complete source-role/object and leaf/inline provenance beyond guarded object bindings and allocator ranges, justify semantic lifetime bindings for ambiguous generations, and close remaining alias/coverage gaps and rare coalescing cases |
| Correspondence | Native provenance anchors, conditional injective hypotheses, actual local merge chains and boundary diagnostics are implemented; finish semantic lifetime/replacement/elimination evidence and additional backend phases |
| Source search | Extend precise provenance/objective recognition, including explicit no-allocation boundaries, add language-aware recipes and finish the full RenderParticles investigation; bounded harness, argument-qualified Normalize counts and one useful checked full-function graph edit are implemented |
| Reduction | Native deterministic reduction/rebinding/uncertainty gates, static category/promoted-copy predicates and replay are implemented; add category-transition reference support and retest discoveries in RenderParticles |
| Matched-source index | Native exact-match corpus/query/revalidation and real symbolic member/reference-argument examples are implemented; expand argument/promotion role coverage and test useful transplants |
| Delivery | Finish original-source/semantic lifetime coverage and the complete RenderParticles investigation; V6 compiler-version/platform checks and native/kit development documentation are implemented; V6 throughput has not been benchmarked |

The qualified-symbol objective now has a concrete independently checked C++ graph
edit. Native reduction and matched-source indexing now use the shared evaluator.
Close source-role coverage beyond this Normalize pair path and the remaining
grouping gaps. Complete exact backend expression/inline evidence and semantic
lifetime binding for ambiguous generations. Complete the remaining RenderParticles
source-discovery loop using the three retained mechanisms. Keep the full
roadmap active: the delivered native implementations do not close the remaining
provenance, mechanism and end-to-end acceptance requirements.
