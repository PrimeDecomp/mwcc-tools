# Open work

What the engine work and the source-guided tools leave open. The tools themselves are described in
[oracle.md](oracle.md), [api.md](api.md) and the guides under [oracle/](oracle/).

## Engine and mwdec

| Item | State |
|---|---|
| Persistent source-search candidates | The target is under 10 ms per candidate on a CStateManager context (a full compile is about 234 ms). Not reached. |
| `-g` / `-sym on` on the persistent path | The target is mwdec's cold setup without its TypeDb and inline caches falling by at least 50% (+6.6 s and +5.5 s before). Not reached. |
| mwdec's own copies | mwdec still has `mwdec-oracle/src/regalloc.rs` (colouring replay and prediction), `mwdec-oracle/src/flags.rs` (unit flags) and `mwdec-mwcc/src/compare.rs` / `placeholder.rs` (the strict comparator), beside `mwcc-trace::colouring`, `mwcc-project` and `mwcc-compare`'s strict level. |
| Placeholder proofs | `mwcc_compare::strict::PlaceholderProver` has no implementation in mwcc-tools, so the strict level here cannot prove a target `fn_<address>` against one of our functions. mwdec's `UnitProver` is the only implementation. |

Kit releases (MCP doc edits, bundles) happen outside this repository.

### What mwdec uses

demwcc-echoes (mwdec) depends on two crates by path (`../mwcc/crates/...`). Changing these APIs breaks it:

- `mwcc-wasm`: `load` / `Loaded`, `Request`, `Overlay`, `Outcome` (with `trace`), and `PersistPool` with
  `PoolOptions`, `pool::Context` and `pool::Compiled` for its fast path;
- `mwcc-trace`: `records::parse` and `records::Selection` (its tracer) and `sched` (its scheduler model).

mwdec's README and ARCHITECTURE.md describe its side.

## Source-guided tools

The provenance, source search, reduction, matched-source index, reproducer and arena tools work within the scopes
their guides state. The goal they serve is still open: find C++ that produces a useful allocation-graph change, and
show it in the complete `RenderParticles` function. A useful graph change and a better retail match are separate
results.

| Area | Open |
|---|---|
| Source provenance | Validate the `source_nodes` capture ([source-nodes](oracle/source-nodes.md)) on native producers, then connect source writes through inline, lowering, IRO and scalarization to backend definitions. Declaration-initializer attribution, lowering, IRO reconstruction and scalar-use replacement still need observed edges. |
| Backend provenance | Source-role and leaf/inline provenance beyond guarded object bindings and allocator ranges; remaining alias and coverage gaps and rare coalescing cases. |
| Value identity | Resolve ambiguous generations and unobserved replacements. Establish elimination only with complete relevant coverage: a missing graph node alone does not establish it. |
| Objectives | Broaden direct-initialization and category-transition support; define explicit no-allocation boundaries. |
| Reduction | Category-transition support; retest reduced discoveries in `RenderParticles`. |
| Matched-source index | Wider argument and promotion role coverage; test useful transplants. |
| Source discovery | More C++ recipes, and the complete `RenderParticles` investigation: explain a useful graph change, find or generate C++ for it, reduce where useful, verify in the full function. |
| Delivery | Compiler-version and platform validation of the newer streams; measure their trace overhead and search throughput. |

One full-unit result exists: a helper specialization in EnvModulate cuts its address pairs from two to one
([case study](oracle-source-guided-case-envmodulate.md)), measured against the clean-source baseline, not
retail. The research behind these items is in the [backend](oracle-backend-provenance-audit.md) and
[source-origin](oracle-source-origin-audit.md) audits (the latter covers inline clones and the IRO optimizer).
