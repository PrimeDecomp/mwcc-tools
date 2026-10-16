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
plus two function-context sites per version, using fixed-size call islands. It
checks complete retail container bytes/relocation layouts and write targets;
original instruction offsets, section sizes, symbols and jump tables stay fixed.
The islands execute the original writes, not a reimplementation of allocation.

The Rust observer records capture-local function/birth IDs, counter changes,
pipeline phases and partial statement-root expression contexts. The host preserves
all generations when numbers recur and distinguishes increments from resets,
rewinds and high-water restores. Orphan writes, discontinuities, missing function
ends and unsupported schemas cannot establish complete direct-counter capture.
Semantic categories, precise leaf/inline origins and graph/lifetime binding remain
incomplete; direct PE-reference coverage is not a complete alias-analysis proof.

`oracle compile-trace --backend` enables these records; `provenance inspect`
returns format-2 typed decisions, birth/reuse histories, saved inputs and separate
decision, direct-counter and aggregate lifecycle coverage.
It validates sites, EFLAGS, short-circuit paths, physical-register window bypass,
counts and roots. Unsupported schemas, incomplete paths and absent captures remain
indeterminate. The [native API](oracle/provenance.md), trace reference and kit guide
are updated. Existing pinned migration releases are unchanged.

Capture/disassembly evidence is retained under
`build/source-guided/backend-audit/`. The capture helper records compiler/function
hashes and instruction guards and is explicitly a development audit, not a product
runtime dependency. Runtime proofs are retained under `build/source-guided/probes/`
and the newer `build/source-guided/counters/` (Windows), with corresponding Linux
proofs under `/home/henrique/.cache/mwcc-counter-probes/`.

## Validation

96 workspace tests pass on Windows and Linux, with seven compiler-backed tests
left opt-in. Retail coalescer/counter rewrite tests were also run on both hosts;
they check all three versions, exact current rewrites against retained runtime
objects, unchanged neighboring sections/relocations, foreign call targets and
rejection of changed code/layout, off-site function changes and relocation targets.
All-target Clippy passes on both hosts.
Three core provenance tests and one CLI test cover complete/missing paths,
unsupported schemas, observed-write consistency, malformed outcomes and filtering.
Four value-history tests cover actual identity semantics: multiple generations,
rewinds/restores, nested function namespaces, partial origins, missing data,
unsupported schemas, gaps and malformed expression bytes.
Five comparison tests
cover graph-only nodes, boundary edges, degree discrepancies, majority-vote
ambiguity, colliding targets, physical projections, changed rounds and absent
snapshots. A new native CLI test verifies selectors, version-2 results and error
output. Existing allocation/search/decoder fixtures continue to pass; legacy remap
prose/JSON parity is not required.

Fresh producers pass 36 C/Rust fixture comparisons on each host. Another 42
fixture compilations per host compare probes off/on across all three
versions: stock objects and existing trace records agree, and all coalescing paths
are explained. The full Echoes `CElementGen.cpp` unit passes off/on on both hosts
with 182 complete coalescing passes, 6,356 births, 4,325 partial expression
observations, no counter gaps/orphans, and identical stock objects/existing traces.
The new scratch-reuse fixture gives 239 births, one rewind and twelve reused
numbers on all three versions/hosts, without asserting physical register choices.
The capture includes RenderParticles, but this is not a source-variant success or
the final RenderParticles investigation. Timings are retained; a controlled probe
overhead/throughput benchmark remains open.

## Required work still open

| Roadmap stage | Remaining work |
|---|---|
| Reproducible cases/contracts | Capture and label the three actual mechanisms, retain the full RenderParticles case, and finish identity/provenance/objective/build-input contracts |
| Backend provenance | Bind observed births/generations to graph phases and instruction visibility; complete object/category and leaf/inline provenance; close remaining alias/coverage gaps and rare coalescing cases |
| Correspondence | Add lifecycle/provenance anchors, generations, observed merges/replacements and justified elimination; extend phase selection to newly captured backend phases |
| Source search | Shared objective evaluator, hard constraints, bounded source transformations, exact input cache, compiler-state fingerprints, budgets/cancellation/resume and independently verified successful candidates |
| Reduction | Reuse the evaluator for deterministic mechanism-preserving reduction, rebind identities, reject indeterminate results and retest discoveries in RenderParticles |
| Matched-source index | Coverage-aware trace/source index and reproducible queries/examples feeding the harness |
| Delivery | Compiler-version/platform checks, trace overhead/search throughput measurements, native documentation/kit/schema updates and the complete RenderParticles investigation |

The next implementation step is graph-phase visibility and lifetime binding,
followed by object/category and complete origin evidence. Connect these observations
to correspondence and the graph-directed harness; then implement reduction and the
matched-source index using the shared evaluator. Capture and label the three
requested real mechanisms and complete the RenderParticles loop. Keep the full
roadmap scope: the implemented provenance components do not complete any of the
four requested tools.
