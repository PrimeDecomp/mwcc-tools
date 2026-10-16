# Matched-source examples

`mwcc-tools source-index` stores source examples whose selected function has exact
bytes **and** identical symbolic relocation records in a supplied reference
object. It queries these examples with the same evidence-aware objectives used
by source search. A missing hook or ambiguous correspondence remains
`indeterminate`; it cannot satisfy an absence query.

```
mwcc-tools source-index ingest ingest.json --index corpus
mwcc-tools source-index query query.json --index corpus
mwcc-tools source-index verify query.json ENTRY_ID --index corpus --output fresh-example
mwcc-tools source-index origins --index corpus --version GC_2_7
```

The ingestion specification is format 1:

```json
{
  "format_version": 1,
  "capture": "retained/capture.json",
  "object": "retained/object.o",
  "target": "retail.o",
  "source": "retained/source.cpp",
  "snapshot": null,
  "build": {
    "oracle_root": "verified-producer",
    "working_directory": "project",
    "source": "absolute/path/from/snapshot/source.cpp",
    "version": "GC_2_7",
    "flags": ["-O4,p"],
    "from_ninja": null,
    "traces": {"backend": true, "locals": true, "cse": true, "stages": true},
    "timeout_seconds": 600
  },
  "repository": "project-name",
  "revision": "source-revision",
  "functions": [
    {"link": "example__Fv", "source_range": {"first_line": 10, "last_line": 25}}
  ]
}
```

`capture` accepts the native search artifact envelope or a plain `Compilation`.
When `snapshot` is null the envelope must contain `inputs`; otherwise it names a
native exact-input `Snapshot`. The source, dependency and runtime files must
still match their captured hashes at ingestion. The reproduction build must
resolve to the snapshot's actual source, working directory, compiler and ordered
flags. For search artifacts this is usually the retained source path, rather
than the original project path that the search overrode.

The corpus retains content-addressed source, object, reference object, capture,
dependencies and runtime blobs. Entries bind all hashes, input identity,
compiler version, ordered flags, declared revision/range and provenance coverage.
Revision and range association are supplied metadata, not a Git attestation or
an automatically proven mapping from backend line numbers. Queries return the
exact source text from the declared range and relevant coalescing observations.

For a function defined in a captured header, its function record can additionally
contain `"source_file":{"path":"include/example.hpp","sha256":"CAPTURED_HASH"}`.
The path resolves relative to the ingestion specification. Its exact path/hash
must occur in the closed snapshot's dependencies; the corpus retains and checks
that blob and validates the range against it. Omission keeps the original
translation-unit source behavior and existing entry identities. This changes
the returned declaration excerpt, never the reproduction build's source:
verification still recompiles the original translation unit and checks its
whole input closure.

`origins` provides discovery metadata before writing a query. It accepts optional
`--link NAME` and `--version GC_2_7` filters and returns JSON. The typed API is
`index::origins(root, &OriginFilter)`; its filter also supports exact ordered
flags. Each result includes its declared source file/range and exact excerpt,
separate declaration-file and translation-unit hashes, reproduction build,
retail match evidence, compiler identities, graph class/round selectors, and
scoped provenance coverage/counts. Scalar-array records expose the observed
name/index/size; Normalize records expose symbol/part/displacement. Call-result
records retain explicit partial output/tree flags and capture-local references;
they do not supply a qualified callee or prove a call-argument relationship.

Inventory derives coverage and origins from the retained capture, including old
entries whose stored coverage has no backend-origin fields. It rechecks retained
blob integrity and retail function bytes/relocations. A zero count describes only
its stated complete guarded scope; missing scope remains indeterminate. Whole
function call counts do not show whether a particular symbolic argument avoided
a temporary, and an array-promotion observation alone does not bind its source
initializer or semantic lifetime.

Every function is checked independently. Unmatched functions are listed as
rejected without admitting their traces. Section-symbol and anonymous relocation
references, ambiguous symbols and unknown relocation representations are rejected
conservatively; this initial implementation does not resolve equivalent constant
pools or relocated section offsets.

An example query for a complete, scoped zero count is:

```json
{
  "format_version": 1,
  "link": null,
  "version": "GC_2_7",
  "flags": null,
  "class": 4,
  "round": 0,
  "objective": {
    "kind": "symbol_address_pairs",
    "symbol": "desired_symbol",
    "minimum": 0,
    "maximum": 0
  },
  "relocations": [
    {"symbol": "desired_symbol", "kind": null, "addend": null, "minimum": 1}
  ]
}
```

`link`, `version` and `flags` are optional exact filters. Queries evaluate each
example against itself as a mechanism reproducer. They support guarded
`symbol_address_pairs` counts, supported `direct_initialization` and
`scalarized_array_initialization` cases, explicit `scalarized_array_definition`
ordinals, promoted-copy outcomes, and the
current-state `range_category` predicate. See the [objective scopes](source-search.md#objectives-and-validity):
an exact source match does not fill missing compiler provenance. For example:

```json
{
  "kind": "range_category",
  "value": {"name": "flag", "part": "single", "type_kind": 1, "type_size": 1},
  "target": "inside_coalescing_window"
}
```

The named object/type/part must uniquely bind to a complete observed birth. The
predicate follows confirmed coalescer parent writes and tests the resulting
root's category at the selected graph. A final allocated register/color alone
does not make the category `physical`. This predicate requires no category
transition or preserved instruction structure; it can query an already desired
category. It says nothing about a value's source role or array-promotion status.
Unknown range or ambiguous merge/birth evidence remains indeterminate.
`promoted_copy_coalescing` can query the observed incoming-copy behavior of a
scalar promoted array element:

```json
{
  "kind": "promoted_copy_coalescing",
  "value": {"name": "flag", "element_index": 1, "element_size": 4},
  "copy": 0,
  "outcome": {"kind": "rejected", "condition": "destination_above_window"}
}
```

This requires complete guarded scalar-promotion provenance, a unique whole-function
array-name/index/size birth without raw-register reuse, and a complete original
graph-node binding. The latest matching coalescer pass before the selected graph
must have complete decisions and matching counts/physical/window bounds. `copy`
is the zero-based ordinal among observed incoming register-copy candidates for
that element. It is **not** an automatic source-initializer or dominance binding.
`outcome: {"kind":"coalesced"}` accepts an actual confirmed merge or an observed
already-same-root copy removal. Rejection conditions come from the deciding
retail branch. Missing or ambiguous provenance is indeterminate. Zero bound incoming copies
remains indeterminate: an invisible promotion node may be a ghost while live
initialization uses a different value. An out-of-range ordinal after at least
one bound copy in a complete pass is unsatisfied. The current guarded scope supports scalar
GPR/FPR promotion, not VMX promotion. Actual source examples still require exact
function matching and fresh revalidation before being treated as corpus evidence.
Category **changes**
require a baseline transition and are rejected as static queries. General
`eliminate_temporary` remains indeterminate until lifecycle coverage can prove
it. A zero Normalize-pair count only establishes absence on that guarded path
before the selected allocation graph; it does not prove absence of every
address temporary. To find useful symbolic-address examples, add required
relocations so functions that never reference the symbol cannot satisfy the
query vacuously. General initializer/dominance and vector-promotion bindings
remain open.

`verify` applies the same filters and objective, recompiles without a compilation
cache, checks the complete snapshot before and after, checks stock equivalence,
rechecks reference bytes/relocations and reevaluates the predicate. It requires
a new output directory and retains captures, input scans, the fresh object and
`verified.json`. `--seconds` starts its budget at API entry and `--cancel-file`
supplies a cooperative cancellation marker. Large synchronous parse/hash steps
check cancellation at their boundaries rather than interrupting mid-step.
Exact environment/path identity is intentional:
moving a corpus to another host requires a separately captured, verified entry
instead of silently declaring the old input identity reproducible.

The Rust entry points are `source_guided::index::{ingest,query,verify,origins,exact_match}`.
This is a corpus of observed compiler mechanisms, not a claim that every example
is a semantics-preserving transformation suitable for transplantation.

The first compiler-backed corpus indexed 35 exact function matches from the clean
`CElementGen.cpp` at Echoes revision `7968f74ae1c3268a9cb03104a60635673d31deb5`.
Windows and Linux independently recompiled `Initialize__11CElementGenFv`, checked
its 36 bytes and four symbolic relocations against retail, and reproduced a zero
guarded Normalize-pair count for `sStaticListInitialized` while requiring its
actual kind-109 relocations. This is a concrete static-list load/store example;
it is not an example of the desired symbolic address argument. The query requiring
an `EnvModulate` relocation returned zero hits among those 35 functions. Both
hosts reproduced the clean full object hash
`8c1304a643c8e8ab49fa2dd2331073d5d2c0ec0341bfb58270417becac835161`.

Local evidence is retained under
`build/source-guided/matched-index-audit/verification.json` and
`build/source-guided/matched-index-linux/verification.json`, with captures, input
snapshots, source excerpts and fresh verification artifacts. An old Windows
snapshot was correctly refused when its environment hash no longer matched;
a separate new capture, snapshot and entry were created before successful
revalidation. The old evidence was preserved.

The V4 inventory of those 35 matched functions found 47 call-result births across
13 functions, with no scalar-array or Normalize observations in their complete
guarded scopes. It does not identify the desired symbolic-call behavior. A further
real retail match, the header-defined `CElementGen::CParticle()` constructor,
provides a positive symbolic initialization example: its 104 bytes and two
`sZeroVector__9CVector3f` HA/LO relocations match retail, and its trace contains
three guarded Normalize pairs. Native query and fresh verification return its
exact header excerpt while recompiling `CElementGen.cpp` with the full unchanged
input closure. This is member/reference initialization, not proof that an external
symbolic call argument avoids an unwanted temporary. Inventory and revalidation
artifacts are under `build/source-guided/matched-origin-audit/`, including
`header-fresh/verification.json`.

A second bounded corpus admits both retail `CGraphics::Render2D` overloads from
`DolphinCGraphics.cpp`. The 748-byte pointer overload passes the reference global
`kEnvPassthru` to `SetTevOp`; native query and fresh verification satisfy a zero
guarded Normalize-pair count for that symbol while requiring its actual
relocation. The object loads the reference into argument register `r4` immediately
before the call. This is useful observed source behavior, but the function-wide
predicate does not bind a particular argument's source lifetime or prove absence
of backend temporaries. Other symbols still produce eight Normalize halves in
that function. All 105 dependencies were private copies, and fresh stock-checked
compilation preserved the exact input snapshot and retail function match. Evidence
is retained in `build/source-guided/matched-symbolic-call-audit/verification.json`.
The source's newer `BeginRender2D` helper uses `kEnvModulate`, but has no function
symbol in this retail target and was not admitted as a retail example.
