# After the Rust migration: realize useful graph edits in C++

Added 2026-10-16. Implement this work **after the Rust migration is finished and its completion gates have passed**, as recorded in [the migration plan](oracle-rust-migration.md) and [implementation status](oracle-rust-status.md). This is a follow-on roadmap, not an expansion of the migration's parity requirements.

The objective is to connect successful allocation-graph edits to C++ constructs that cause the compiler to produce those edits. Allocation replay, graph search, graph comparison and existing frontend provenance provide the foundation. The missing evidence is which source transformations realize a useful graph change.

## Priority 1: complete backend provenance and coalescing explanations

Extend the oracle's producer and structured reports to describe every virtual register's creation site, originating expression, allocation category and subsequent merges. Cover backend-generated address temporaries, call-result temporaries and array-promotion temporaries, in addition to the existing named-local and CSE provenance.

Record each attempted coalescing and the condition that decides it, including the coalescing window, interference and instruction flags. Retain temporaries that disappear from the visible instructions but still contribute nodes or graph degrees. Preserve enough identity across creation, transformations, merge attempts and allocation rounds to follow a value through the backend.

Deliver a queryable lifecycle and coalescing history alongside the human explanation. Include the compiler version, phase and deciding inputs so that an absent event is distinguishable from an unsupported hook or an incomplete trace.

Acceptance examples:

- Explain the extra temporary introduced by a symbolic address argument, from its creation through its allocation effects.
- Explain why the second promoted array element does or does not coalesce, identifying the actual deciding condition.
- Account for relevant graph nodes and degrees even when their original temporaries no longer occur in the instruction list.
- Confirm that the added observation hooks preserve compiler output with tracing disabled and enabled.

## Priority 2: source-variant search scored by graph changes

Build a harness that accepts a baseline, a relevant set of values or graph nodes, and an explicit compiler-state objective. Compile C++ variants and use the existing `remap` machinery to assess whether each variant achieved the requested change.

Initial objectives:

- Eliminate a relevant coalesced temporary while preserving symbolic relocations.
- Keep a flag's initialization directly in its final register, removing the extra move.
- Change a particular value's allocation category while preserving its instructions.

Keep semantic and relocation constraints explicit. A better final mismatch alone does not establish that a variant realized the intended mechanism. Report the graph, provenance, category, move and relocation evidence used to score or reject it, as appropriate to the objective.

Deduplicate variants by the relevant compiler state after remapping. Source forms that copy propagation reduces to the same state should share a result rather than consume repeated search effort. Define and version this state fingerprint; include the fields needed by the objective and avoid accidentally merging variants with different symbolic relocations or provenance mechanisms.

Start with bounded, reproducible variant families such as declaration forms, parameter types, wrappers and promotion patterns. Save each candidate, its build flags and compiler identity, the objective result and its state fingerprint. A successful search must return a concrete, reviewable source variant and a recompilation that demonstrates the graph change.

**Build priorities 1 and 2 first.** Provenance should explain why candidates succeed or collapse to the same state, and the graph-directed harness should turn that information into testable source hypotheses.

## Priority 3: reduce examples while preserving the mechanism

Add a reducer whose predicate describes the compiler behavior under investigation. Examples are “a symbolic argument creates this extra temporary” and “the second promoted array element cannot coalesce.” Preserve the mechanism rather than exact final physical registers, which depend on the rest of the function.

Use provenance and remapping to follow the relevant values as unrelated code is removed. Keep compilation validity and the objective's required semantics and symbolic relocations as constraints. Record the reduction steps and reproduce the final predicate from the saved small example.

Use reduced examples to exhaustively test declaration forms, parameter types, wrappers and promotion patterns. Verify useful discoveries again in `RenderParticles`; success on the reduced case is a hypothesis until it survives transplantation into the full function.

## Priority 4: find existing source examples of the desired behavior

Index oracle traces from already matched functions together with their source locations, compiler versions and build flags. Query for concrete mechanisms, beginning with symbolic address arguments without the unwanted temporary and promoted values whose initialization coalesces as needed.

Return the corresponding C++ construct and its provenance/coalescing evidence. Distinguish examples obtained under different compiler versions or flags, and make each result reproducible. Use these observed source patterns to seed the variant harness and reducer experiments.

## Completion evidence

Demonstrate the complete loop on a representative `RenderParticles` problem: identify a useful graph edit, explain its backend mechanism, find or search for a C++ construct that realizes it, reduce the behavior where useful, and verify the source change in the full function. Keep the graph objective and the final code-match result separate so the evidence shows what was learned.
