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

## Retail instrumentation audit

The [backend audit](oracle-backend-provenance-audit.md) identifies the coalescer and
eleven candidate branch/write probe sites in all three supported retail versions.
Complete decoded function bytes match after masking only PE HIGHLOW relocations
and direct-call displacements. The actual predicates are inlined and include an
additional GPR/special-register guard; using reconstructed C helper signatures
would not faithfully observe them. Register creation/reuse/category coverage is
still incomplete. **No new runtime probes are installed yet.**

Capture/disassembly evidence is retained under
`build/source-guided/backend-audit/`. The capture helper records compiler/function
hashes and instruction guards and is explicitly a development audit, not a product
runtime dependency or a coverage claim.

## Validation

87 workspace tests pass on Windows and Linux, with five compiler-backed tests
left opt-in. All-target Clippy passes on both hosts. Five new comparison tests
cover graph-only nodes, boundary edges, degree discrepancies, majority-vote
ambiguity, colliding targets, physical projections, changed rounds and absent
snapshots. A new native CLI test verifies selectors, version-2 results and error
output. Existing allocation/search/decoder fixtures continue to pass; legacy remap
prose/JSON parity is not required.

## Required work still open

| Roadmap stage | Remaining work |
|---|---|
| Reproducible cases/contracts | Capture and label the three actual mechanisms, retain the full RenderParticles case, and finish identity/provenance/objective/build-input contracts |
| Backend provenance | Install byte-guarded observations of actual creation, reuse, categories and coalescing decisions; preserve retail execution and registers/flags; establish context/coverage and verify enabled/disabled stock equality |
| Correspondence | Add lifecycle/provenance anchors, generations, observed merges/replacements and justified elimination; extend phase selection to newly captured backend phases |
| Source search | Shared objective evaluator, hard constraints, bounded source transformations, exact input cache, compiler-state fingerprints, budgets/cancellation/resume and independently verified successful candidates |
| Reduction | Reuse the evaluator for deterministic mechanism-preserving reduction, rebind identities, reject indeterminate results and retest discoveries in RenderParticles |
| Matched-source index | Coverage-aware trace/source index and reproducible queries/examples feeding the harness |
| Delivery | Compiler-version/platform checks, trace overhead/search throughput measurements, native documentation/kit/schema updates and the complete RenderParticles investigation |

The next implementation step is runtime provenance: validate the retail creation
paths/calling context, then add guarded coalescing decision observations at the
audited branches without substituting a reconstructed allocator. Keep the full
roadmap scope; passing the current comparison tests does not complete any of the
four requested tools.
