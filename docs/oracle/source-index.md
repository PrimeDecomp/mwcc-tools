# Find matched-source examples

The `source_index.*` methods store source examples whose selected function matches a reference object's bytes
**and symbolic relocations**. Query the corpus for an observed compiler behavior, then verify an example freshly
before using it as a starting point for a rewrite.

```sh
mwcc-tools call source_index.ingest spec=ingest.json index=corpus
mwcc-tools call source_index.origins index=corpus version=GC_2_7
mwcc-tools call source_index.query query=query.json index=corpus
mwcc-tools call source_index.verify query=query.json id=ENTRY_ID index=corpus output=fresh-example
```

A matched example establishes observed compiler behavior, not semantic equivalence elsewhere. Missing hooks or
ambiguous correspondence are `indeterminate` and cannot satisfy an absence query.

Rust: `source_guided::index::{ingest, origins, query, verify, exact_match}`.

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

- `capture` is a search artifact envelope or a plain `Compilation`. With `snapshot: null` the envelope must contain
  `inputs`; otherwise give an exact-input `Snapshot`. Source, dependencies and runtime files must match their
  captured hashes, and the build must resolve to the snapshot's source, working directory, compiler and flags
  (for search captures, usually the retained private source).
- Each function is checked on its own; unmatched ones are rejected. Ambiguous or anonymous symbols, section-symbol
  relocations and unknown relocation forms are rejected.
- The corpus keeps content-addressed sources, objects, captures, dependencies and runtime blobs. Revision and
  source range are supplied metadata, not verified; queries return the declared excerpt.
- For a header-defined function, add `"source_file": {"path": "include/example.hpp", "sha256": "CAPTURED_HASH"}`.
  The path is relative to the spec and must be a snapshot dependency; it selects the excerpt, while verification
  still recompiles the whole translation unit.

## Discover and query

`origins` lists excerpts, declaration/TU hashes, builds, match evidence, compiler identity, graph selectors and
provenance coverage, filtered by `link` and `version` (Rust `OriginFilter` also by flags). It rechecks blob
integrity and reference bytes/relocations. Use the observed scalar-array name/index/size, Normalize
symbol/part/displacement and argument `callee_links`/`bound_arguments`/`episodes` to choose selectors.

A query for a zero guarded Normalize-pair count that still requires a reference to the symbol:

```json
{
  "format_version": 1,
  "link": null,
  "version": "GC_2_7",
  "flags": null,
  "class": 4,
  "round": 0,
  "objective": {"kind": "symbol_address_pairs", "symbol": "desired_symbol", "minimum": 0, "maximum": 0},
  "relocations": [{"symbol": "desired_symbol", "kind": null, "addend": null, "minimum": 1}]
}
```

`link`, `version` and `flags` are optional exact filters. Each entry is evaluated against itself with the
[search objectives](source-search.md#objectives-and-validity), except `change_range_category` (a baseline
transition, rejected here). A zero Normalize count proves absence only on that guarded path; the required
relocations stop unrelated functions from satisfying it vacuously.

## Fresh verification

`verify` applies the same filters and objective and recompiles without cache reuse. It checks the snapshot before
and after, stock equivalence, reference bytes/relocations and the predicate, and writes scans, captures, the object
and `verified.json` to its new output directory. `seconds` and `cancel_file` bound it. The environment and path
identity must match; on another host, ingest a separate entry.
