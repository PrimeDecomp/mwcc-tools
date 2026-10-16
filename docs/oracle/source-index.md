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
`symbol_address_pairs` counts and `direct_initialization`. Category **changes**
require a baseline transition and are rejected as static queries. General
`eliminate_temporary` remains indeterminate until lifecycle coverage can prove
it. A zero Normalize-pair count only establishes absence on that guarded path
before the selected allocation graph; it does not prove absence of every
address temporary. To find useful symbolic-address examples, add required
relocations so functions that never reference the symbol cannot satisfy the
query vacuously. Array-promotion role predicates remain future work.

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

The Rust entry points are `source_guided::index::{ingest,query,verify,exact_match}`.
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
