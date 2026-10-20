# Open work

What the engine work and the source-guided tools leave open. The tools themselves are described in
[oracle.md](oracle.md), [api.md](api.md) and the guides under [oracle/](oracle/).

## Engine and mwdec

| Item | State |
|---|---|
| Persistent source-search candidates | The target is under 10 ms per candidate on a CStateManager context (a full compile is about 234 ms). Not reached. |
| `-g` / `-sym on` on the persistent path | The target is mwdec's cold setup without its TypeDb and inline caches falling by at least 50% (+6.6 s and +5.5 s before). Not reached. |
| mwdec's inverse solvers | mwdec's colouring, unit flags and strict comparator are now mwcc-tools' crates (on its local `mwcc-engine` branch). What stays in `mwdec-oracle/src/regalloc.rs` is the vreg numbering, `spill_cost`, `infer_classes` and the inverse solvers (`solve_order`, `solve_order_constrained`, `forced_pairs`): `mwcc-trace` replays traced rounds and has nothing that infers an order or classes from observed registers. |
| Placeholder proofs | `mwcc_compare::strict::PlaceholderProver` has no implementation in mwcc-tools, so the strict level here cannot prove a target `fn_<address>` against one of our functions. mwdec's `UnitProver` is the only implementation. |

Kit releases (MCP doc edits, bundles) happen outside this repository.

### What mwdec uses

demwcc-echoes (mwdec) depends on four crates by path (`../mwcc/crates/...`). Changing these APIs breaks it:

- `mwcc-wasm`: `load` / `Loaded`, `Request`, `Overlay`, `Outcome` (with `trace`), and `PersistPool` with
  `PoolOptions`, `pool::Context` and `pool::Compiled` for its fast path;
- `mwcc-trace`: `records::parse` and `records::Selection` (its tracer), `sched` (its scheduler model), and
  `trace::{Node, Round}`, `allocation::simplify` and `colouring::select` (its colouring prediction);
- `mwcc-compare`: `strict::compare`, `strict::Index`, `ExternIndex`, `DiffClass`, `masked_words`, `reloc_mask`,
  `is_generated_name` and `PlaceholderProver` (its comparator), `strict::elf`'s object model and `load` (its
  object files), and `score::Target` with `ScoreOptions::from_objdiff_unit` (the objdiff % its search reports);
- `mwcc-project`: `Project::open`, `module_of` and `module_objects`, and `ninja::parse` / `CompileRule` (its units
  and flag profiles).

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
