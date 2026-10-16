# Find matched-source examples

`mwcc-tools source-index` stores source examples whose selected function matches
a reference object's bytes **and symbolic relocations**. Query the corpus for
an observed compiler behavior, then freshly verify a promising example before
using it as a starting point for a rewrite.

```text
mwcc-tools source-index ingest ingest.json --index corpus
mwcc-tools source-index origins --index corpus --version GC_2_7
mwcc-tools source-index query query.json --index corpus
mwcc-tools source-index verify query.json ENTRY_ID --index corpus --output fresh-example
```

Commands return JSON. Missing hooks or ambiguous correspondence remain
`indeterminate` and cannot satisfy an absence query. A matched example establishes
observed compiler behavior, not semantic equivalence when transplanted elsewhere.

## Ingest format 1

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

`capture` accepts a search artifact envelope or plain `Compilation`. With
`snapshot: null`, the envelope must contain `inputs`; otherwise provide a native
exact-input `Snapshot`. Source, dependencies and runtime files must still match
their captured hashes. The reproduction build must resolve to that snapshot's
actual source, working directory, compiler and ordered flags. For search captures,
this is usually the retained private source rather than its original project path.

Each function is checked independently. Unmatched functions are rejected without
admitting their traces. Ambiguous/anonymous symbols, section-symbol relocations
and unknown relocation forms are rejected; equivalent constant pools or relocated
section offsets are not inferred.

The corpus retains content-addressed sources, objects, captures, dependencies and
runtime blobs. Entries bind those hashes, input/compiler identity, flags, declared
revision/range and provenance coverage. Revision and source-range association are
supplied metadata, not a Git attestation or inferred backend-to-source mapping.
Queries return the exact declared excerpt.

For a header-defined function, add this to its function record:

```json
{"source_file": {"path": "include/example.hpp", "sha256": "CAPTURED_HASH"}}
```

The path resolves relative to the ingestion specification and must match a closed
snapshot dependency. Its retained blob and line range are checked. This selects
the returned declaration excerpt; fresh verification still recompiles the original
translation unit with its full input closure.

## Discover and query behavior

`origins` lists source excerpts, declaration/translation-unit hashes, reproduction
builds, reference-match evidence, compiler identity, graph selectors and observed
provenance coverage. Optional `--link NAME` and `--version GC_2_7` filter it; the
Rust `OriginFilter` also supports exact ordered flags. Inventory rechecks retained
blob integrity and reference bytes/relocations, deriving coverage from the capture
even for older stored entries.

Use observed scalar-array name/index/size, Normalize symbol/part/displacement and
V6 argument `callee_links`/`bound_arguments`/`episodes` to choose selectors.
Call-result fields remain partial and do not identify an argument. Older captures
without argument coverage stay indeterminate. An aggregate zero describes only
its complete guarded scope, not a particular argument or semantic lifetime.

This query asks for a zero guarded Normalize-pair count while requiring an actual
reference to the symbol:

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
entry against itself as a mechanism reproducer. They use the shared
[objective definitions and limits](source-search.md#objectives-and-validity):
guarded symbol/argument address counts, supported direct or scalarized
initialization, explicit scalarized definition ordinals, promoted-copy outcomes
and current-state `range_category`.

For example, `range_category` takes
`value: {"name":"flag","part":"single","type_kind":1,"type_size":1}` and
`target: "inside_coalescing_window"`. It requires a unique birth and confirmed
merge root; a final physical color alone does not make the category `physical`.
`promoted_copy_coalescing` uses an element selector such as
`{"name":"flag","element_index":1,"element_size":4}`, an incoming-copy ordinal
and an actual coalesced/rejected outcome. It does not infer a source initializer;
zero incoming copies remain indeterminate even when a promotion birth exists.

`change_range_category` is a baseline transition and is rejected by static
queries. General `eliminate_temporary` remains indeterminate. A zero Normalize
count proves only absence on that guarded path before the graph; required
relocations prevent unrelated functions from satisfying it vacuously. Missing
initializer/dominance, vector-promotion or lifetime evidence is not filled by
an exact source match.

## Fresh verification

`verify` applies the same filters/objective and recompiles without compilation
cache reuse. It checks the complete snapshot before and after, stock equivalence,
reference bytes/relocations and the predicate. Its new output directory retains
scans, captures, the fresh object and `verified.json`.

Use `--seconds N` and `--cancel-file FILE` to bound verification. Cancellation is
checked around synchronous parse/hash work and while waiting on subprocesses.
The exact environment/path identity must still match. For another host, capture
and verify a separate entry rather than silently reusing the old input identity.

Rust APIs: `source_guided::index::{ingest,origins,query,verify,exact_match}`.
The [implementation status](../oracle-source-guided-status.md) records native
corpus validation. Retained examples include 35 matched `CElementGen` functions,
a header-defined particle constructor and both `Render2D` overloads. Their scoped
symbol/reference behaviors are examples to investigate, not automatic transplant
or complete argument-lifetime proofs.
