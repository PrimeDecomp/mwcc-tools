# Source-guided tools: current status

Updated 2026-10-17. The Rust migration is complete. The follow-on tools are usable
in development, but the [full roadmap](oracle-source-guided-search.md) remains open.
Build current tools and hooks for these features; the pinned migration kits do
not include them.

## What each tool does

| Tool | Purpose | Guide |
|---|---|---|
| `provenance inspect` | Explain actual coalescing decisions, value creation, graph membership and available frontend origins. | [Provenance](oracle/provenance.md) |
| `provenance compare` | Compare captured graphs, including ambiguous mappings, merges and boundary edges. | [Graph comparison](oracle/graph-correspondence.md) |
| `source-search` | Compile C++ variants, score a graph objective and group equivalent observed states. | [Source search](oracle/source-search.md) |
| `source-reduce` | Shrink a case while preserving a compiler mechanism. | [Reducer](oracle/source-reduce.md) |
| `source-index` | Find C++ examples in strictly matched functions and recompile them to verify a query. | [Matched-source index](oracle/source-index.md) |
| `source-repro` | Export a retained case, check its files and replay it in a new directory. | [Reproducers](oracle/source-repro.md) |
| `source-arenas` | Check which allocation owned a byte range at a particular trace position. | [Arena queries](oracle/source-arenas.md) |

Search, reduction and indexing share one objective evaluator. It supports scoped
symbol/address-pair counts, guarded call-argument counts, allocation-range
predicates, promoted-copy coalescing and a narrow direct-initialization predicate.
Unknown evidence stays indeterminate. Missing graph nodes alone cannot establish
elimination.

## What has been demonstrated

- A checked full-unit helper specialization reduces EnvModulate address pairs
  from two to one, graph nodes from 553 to 551 and noRota's degree from 157 to 155.
  It passes independent compilation and the retained semantic checker on Windows
  GC2.7. The comparison target is the clean-source baseline, not retail.
  [Case study](oracle-source-discovery-helper-specialization.md).
- Search, reduction, matched-source queries and relocated reproducer replay have
  native Windows/Linux evidence. Their supported scopes and limits are in the
  guides above.
- Guarded arena capture covers all nine allocation paths in the retained fixtures.
  Historical queries distinguish payload, padding, resets and reused addresses.
  Allocation identity alone does not establish node contents or source ancestry.

The last completed cross-host validation, at `ae28169`, passed 229 workspace tests
per host, with 13 opt-in tests ignored, plus all-target and i686 Clippy.
Fresh producers passed 36 C/Rust baseline compilations and 18 arena fixture
compilations per host. Runtime checks included 54 malformed captures and nine
padding negatives per host. This baseline predates the experimental work below.

## Experimental source-node work

The working tree adds `--source-nodes`, immutable snapshots of selected frontend
fields, observed assignment emission, actual recursive clone/substitution edges
and batched arena replay. Eleven focused host parser tests pass on Windows and Linux.
Native producer/runtime validation is pending; these features are not yet an
accepted cross-host milestone. See the [experimental API](oracle/source-nodes.md).

Snapshots identify separate reads. Allocation receipts identify storage lifetimes.
Neither supplies a complete path from an original C++ expression to a backend
value. Declaration-initializer attribution, lowering, IRO reconstruction and
scalar-use replacement still need observed edges.

## Remaining work

| Area | Next requirement |
|---|---|
| Source provenance | Validate the experimental capture, then connect source writes through inline/lowering/IRO/scalarization to backend definitions. |
| Value identity | Resolve ambiguous generations and unobserved replacements; establish elimination only with complete relevant coverage. |
| Objectives | Broaden direct-initialization and category-transition support; define explicit no-allocation boundaries. |
| Source discovery | Test additional C++ recipes and useful matched-source transplants in the complete `RenderParticles` function. |
| Delivery | Finish compiler-version/platform validation, measure new overhead and update development packages. |

A useful graph change and a better retail match are separate results. The complete
`RenderParticles` matching investigation remains open.

Detailed counts, artifact paths and earlier milestones are preserved in the
[validation history](oracle-source-guided-history.md). Research details remain in
the [backend](oracle-backend-provenance-audit.md),
[source-write](oracle-source-origin-audit.md),
[inline](oracle-inline-clone-audit.md) and [IRO](oracle-iro-origin-audit.md) audits.
