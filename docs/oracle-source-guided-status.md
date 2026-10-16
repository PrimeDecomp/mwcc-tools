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

Elimination is deliberately **indeterminate** with existing snapshot-only traces.
This is a missing-evidence result, not an implemented elimination objective or
complete backend provenance. Snapshot names are existing trace boundaries; they do
not add instrumentation for new backend phases.

## Retail instrumentation

The [backend audit](oracle-backend-provenance-audit.md) identifies the coalescer and
eleven candidate branch/write probe sites in all three supported retail versions.
Complete decoded function bytes match after masking only PE HIGHLOW relocations
and direct-call displacements. The actual predicates are inlined and include an
additional GPR/special-register guard; using reconstructed C helper signatures
would not faithfully observe them. The native Rust linker now installs fourteen
guarded observations while preserving the retail function body and control flow.
The common Rust observer records actual branch inputs/outcomes, pass/candidate
context and confirmed union-parent writes. Register creation/reuse/category
coverage is still incomplete.

`oracle compile-trace --backend` enables these records; `provenance inspect`
returns typed decisions, full saved inputs and explicit decision/lifecycle coverage.
It validates sites, EFLAGS, short-circuit paths, physical-register window bypass,
counts and roots. Unsupported schemas, incomplete paths and absent captures remain
indeterminate. The [native API](oracle/provenance.md), trace reference and kit guide
are updated. Existing pinned migration releases are unchanged.

Capture/disassembly evidence is retained under
`build/source-guided/backend-audit/`. The capture helper records compiler/function
hashes and instruction guards and is explicitly a development audit, not a product
runtime dependency. Runtime proofs are separate under `build/source-guided/probes/`.

## Validation

92 workspace tests pass on Windows and Linux, with six compiler-backed tests
left opt-in. The new retail rewrite test was also run explicitly on both hosts;
it checks all three versions, exact current rewrites against retained runtime
objects, unchanged neighboring sections/relocations, foreign call targets and
rejection of changed code/layout. All-target Clippy passes on both hosts.
Three core provenance tests and one CLI test cover complete/missing paths,
unsupported schemas, observed-write consistency, malformed outcomes and filtering.
Five comparison tests
cover graph-only nodes, boundary edges, degree discrepancies, majority-vote
ambiguity, colliding targets, physical projections, changed rounds and absent
snapshots. A new native CLI test verifies selectors, version-2 results and error
output. Existing allocation/search/decoder fixtures continue to pass; legacy remap
prose/JSON parity is not required.

Fresh producers pass the existing 36-compilation reference comparison on each
host. Another 36 compilations per host compare probes off/on across all three
versions: stock objects and existing trace records agree, and all coalescing paths
are explained. The full Echoes `CElementGen.cpp` unit passes off/on on both hosts
with 182 complete coalescing passes and identical stock objects/existing traces.
The capture includes RenderParticles, but this is not a source-variant success or
the final RenderParticles investigation. Timings are retained; a controlled probe
overhead/throughput benchmark remains open.

## Required work still open

| Roadmap stage | Remaining work |
|---|---|
| Reproducible cases/contracts | Capture and label the three actual mechanisms, retain the full RenderParticles case, and finish identity/provenance/objective/build-input contracts |
| Backend provenance | Audit/instrument actual creation, counter reuse, categories, origins and graph-only lifetimes; add stable generations and phase/context coverage; extend live cases to rare coalescing outcomes |
| Correspondence | Add lifecycle/provenance anchors, generations, observed merges/replacements and justified elimination; extend phase selection to newly captured backend phases |
| Source search | Shared objective evaluator, hard constraints, bounded source transformations, exact input cache, compiler-state fingerprints, budgets/cancellation/resume and independently verified successful candidates |
| Reduction | Reuse the evaluator for deterministic mechanism-preserving reduction, rebind identities, reject indeterminate results and retest discoveries in RenderParticles |
| Matched-source index | Coverage-aware trace/source index and reproducible queries/examples feeding the harness |
| Delivery | Compiler-version/platform checks, trace overhead/search throughput measurements, native documentation/kit/schema updates and the complete RenderParticles investigation |

The next implementation step is creation/reuse/category provenance. Enumerate
actual counter writes and class-indexed allocation paths, including inline
increments in address/call/promotion code, and establish function/expression
context before assigning stable lifetime IDs. Then connect those identities to
comparison and the graph-directed harness. Keep the full roadmap scope; the
implemented coalescing observations do not complete any of the four requested tools.
