# Source-guided oracle tools: implementation status

Updated 2026-10-16. The active objective is to implement all four requested tools
using the [adopted roadmap](oracle-source-guided-search.md). The objective is not
complete. The completed Rust migration remains a separate release.

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

Elimination remains **indeterminate**. The comparison API does not yet bind rich
birth histories to graph lifetimes or consume them as semantic correspondence
anchors. Capturing births does not turn unmatched nodes into elimination proof.
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

## Validation

121 workspace tests pass on Windows and Linux, with nine compiler-backed tests
left opt-in. The new live source-search test was also run on both hosts across all
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
from the later successful graph-objective experiment. Timings are retained; a controlled probe
overhead/throughput benchmark remains open.

## Required work still open

| Roadmap stage | Remaining work |
|---|---|
| Reproducible cases/contracts | Capture the flag/second-promoted-element mechanisms and finish identity/provenance/objective/build-input contracts; the symbolic-address case now has a useful checked graph edit |
| Backend provenance | Complete source-role/object and leaf/inline provenance beyond guarded object bindings and allocator ranges, justify semantic lifetime bindings for ambiguous generations, and close remaining alias/coverage gaps and rare coalescing cases |
| Correspondence | Add lifecycle/provenance anchors, generations, observed merges/replacements and justified elimination; extend phase selection to newly captured backend phases |
| Source search | Capture the remaining requested mechanisms, extend precise provenance/objective recognition, add language-aware recipes and finish the full RenderParticles investigation; the bounded harness and one useful checked full-function graph edit are implemented |
| Reduction | Reuse the evaluator for deterministic mechanism-preserving reduction, rebind identities, reject indeterminate results and retest discoveries in RenderParticles |
| Matched-source index | Coverage-aware trace/source index and reproducible queries/examples feeding the harness |
| Delivery | Compiler-version/platform checks, trace overhead/search throughput measurements, native documentation/kit/schema updates and the complete RenderParticles investigation |

The qualified-symbol objective now has a concrete independently checked C++ graph
edit. Implement reduction and the matched-source index using the shared evaluator,
while closing source-role coverage beyond this Normalize pair path and the remaining
grouping gaps. Complete exact backend expression/inline evidence and semantic
lifetime binding for ambiguous generations. Capture and label the three
requested real mechanisms and complete the RenderParticles loop. Keep the full
roadmap scope: the implemented provenance components do not complete any of the
four requested tools.
