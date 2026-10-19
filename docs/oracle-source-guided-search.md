# Roadmap: produce useful graph edits from C++

Find C++ transformations that produce useful allocation-graph changes. The four
requested extensions are provenance, graph-directed source search, mechanism
reduction and a matched-source index. Build provenance and source search first.

The [Rust migration is complete](oracle-rust-status.md). These extensions have
usable development implementations, with requirements still open; see
[current status](oracle-source-guided-status.md). This roadmap records the full
acceptance scope, including work already delivered. Use native Rust APIs and
versioned JSON for integrations.

## Comparison requirement

Graph comparison must retain instruction-invisible nodes, ambiguous mappings,
merges, boundary edges and phase/round selection. The development
[observed-boundary comparison](oracle/graph-correspondence.md) provides these
diagnostics. Complete semantic lifetime and elimination evidence remain open:
an unmatched value alone does not establish elimination.

## 1. Reproducible cases and shared contracts

Capture baseline cases for a symbolic address argument introducing an unwanted
temporary, a flag initialization retaining an extra move, and the second promoted
array element failing to coalesce. Include the relevant full `RenderParticles`
case. Preserve source and dependencies, compiler identity, ordered flags, object,
symbolic relocations and complete trace so each case can be replayed independently.

Define versioned structures for value identity, provenance events, coalescing
decisions, graph objectives and evaluation results. Evaluations distinguish
**satisfied**, **unsatisfied** and **indeterminate**. Missing hooks, incomplete
relevant trace coverage or ambiguous value correspondence never count as success.
Record the evidence and coverage needed to justify each result.

## 2. Complete backend provenance first

Audit virtual-register creation and coalescing paths for each supported oracle
compiler version. Document covered and unsupported paths rather than interpreting
an absent event as an absent mechanism.

Record each register's function and class, creation identity, allocation phase and
round, backend creation site/reason, originating expression, source location and
inline ancestry when available. Record allocation category and category changes,
copies, merges, replacements and disappearance from visible instructions. Cover
backend addresses, call results and array promotion as well as named locals/CSE.

Identity must survive raw-address changes and reused register numbers. Preserve
multiple contributing origins and explicit unknown origins. Raw pointers and
register numbers alone are insufficient identities across source variants.

Instrument actual coalescing decisions: candidate operands, relevant instructions,
window bounds, flags, interference evidence, outcome and the condition that actually
decided it. Distinguish candidates rejected before an attempt from attempted merges.
Do not infer a definite reason from the final graph alone.

Retain nodes after their instructions disappear. Record graph membership and degree
at meaningful phases, explaining stored-degree versus adjacency differences.
Expose structured lifecycle queries and readable explanations.

Acceptance: explain the symbolic-address and promoted-array cases end to end,
account for relevant graph nodes/degrees, and verify stock-equivalent output with
tracing enabled and disabled for the affected compiler versions.

## 3. Value correspondence and complete graph comparison

Build on `remap` and `graph_difference`, combining provenance anchors with
instruction matching. Represent ambiguity, established elimination and many-to-one
merges explicitly. Include graph-only temporaries and let callers select the
intended phase and allocation round.

Report unmatched nodes and edges crossing the mapped boundary instead of silently
omitting them. Separate “not matched,” “not visible in instructions,” “merged” and
“proven eliminated.” Changed allocation rounds must not be mistaken for removal.

Acceptance fixtures cover renumbering, disappeared temporaries, merges, ambiguity
and changed allocation rounds without false elimination claims.

## 4. Graph-directed source-variant harness

Create one Rust objective evaluator shared by search, reduction and corpus queries.
The first objectives are:

- Remove a relevant temporary: lifecycle/graph evidence establishes removal while
  required symbolic relocations remain.
- Initialize a flag directly: initialization reaches the value's allocated register
  without the unwanted copy.
- Change a value's allocation category while preserving the specified instruction
  structure.

Separate hard constraints from ranking. Check compilation, required relocations,
applicable semantic checks and trace completeness before scoring graph changes.
Report final target mismatch separately; a better mismatch does not prove that the
intended mechanism changed.

Begin with bounded, reproducible transformation families: declaration placement
and forms, equivalent initialization forms, compatible parameter forms, wrappers
and array-promotion patterns. State their preconditions. Parameter changes and
wrappers are not automatically equivalent source transformations.

Save candidate source, transformations, build inputs, objective evidence and
rejection reasons. Support budgets, cancellation, resume and deterministic ordering.

Use exact build-input caching to avoid identical recompilations, including compiler
identity, dependencies, flags, trace configuration and relevant environment in the
cache contract. Separately define
and version compiler-state fingerprints to group already compiled results and avoid
redundant analysis/exploration. Preserve relevant provenance, graph boundaries,
categories, instructions and symbolic relocations while normalizing incidental
identities. Different source variants still require their initial compilation.
Keep source representatives: equivalent current compiler states do not guarantee
identical future transformations.

Acceptance: return a concrete source candidate that independently recompiles and
satisfies an objective, with inspectable evidence grouping duplicate syntax variants.

**The first usable milestone consists of stages 1–4: provenance, reliable value
correspondence and the graph-directed harness.** Build these before the reducer and
matched-source index. This preserves the requested priority of provenance and
source-variant search while making comparison correctness a prerequisite.

## 5. Mechanism-preserving reduction

Reuse the objective evaluator as the reduction predicate. Reduce declarations,
statements, expressions, helpers and dependencies while retaining the mechanism and
required build/relocation constraints. Example predicates are “a symbolic argument
creates this extra temporary” and “the second promoted array element cannot coalesce.”

Rebind values through provenance after every change and reject indeterminate
evaluations. Preserve the mechanism rather than exact physical registers, which
depend on the whole function. Provide deterministic reduction order, caching,
budgets and replayable logs. Label compiler-behavior reproducers separately from
source changes validated for transplantation.

Acceptance: a smaller standalone case reproduces the same creation/coalescing
decision. Use it to exhaustively test declaration forms, parameter types, wrappers
and promotion patterns, then retest useful transformations in `RenderParticles`.

## 6. Searchable matched-source corpus

Index traces from already matched functions with source revisions/ranges, compiler
identity, flags, match evidence and provenance coverage. Initial queries seek
symbolic address arguments without the unwanted temporary, and promoted values
whose initialization coalesces as needed.

Absence queries require complete relevant coverage. Return actual C++ constructs,
decision evidence and reproduction commands, keeping compiler-version and flag
differences visible. Feed observed examples into the harness as candidate patterns.

Acceptance: returned examples recompile and reproduce the claimed mechanism.

## 7. Validation and delivery

Add focused parser, lifecycle, correspondence, objective and fingerprint tests plus
compiler-backed cases. Run affected compiler-version checks and Windows/Linux
integration checks. Measure trace overhead and search throughput. Update native
CLI documentation, kit workflows and structured schemas together.

Finish with a reproducible `RenderParticles` investigation: explain a useful graph
change, find or generate C++ realizing it, reduce where useful, and verify in the
full function. Report graph-objective success and final code matching independently.
