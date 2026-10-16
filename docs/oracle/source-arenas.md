# Source arena observations

Use arena observations to resolve a historical compiler pointer to its allocation
and pool epoch. They establish allocation identity, not initialized contents,
source ancestry, unchanged node revisions or semantic equivalence.

```text
mwcc-tools oracle compile-trace --oracle-root ORACLE_ROOT --version GC_2_7 --source-arenas source.cpp -- COMPILER_FLAGS > capture.json
mwcc-tools source-arenas inspect capture.json --json
mwcc-tools source-arenas resolve capture.json --position 120 --pool local --address 01000000 --extent 24
```

Use current native tools and a freshly built Rust producer. Commands accept raw
traces or compile captures. `--position` is a **one-based raw trace line**;
`--address` is hexadecimal and `--extent` is a byte count. Pools are `heap`,
`optimizer`, `block`, `local` and `global`. A successful result identifies the
allocation, historical epoch, offset, extent and covering checkpoint.

For multiple queries, put a JSON array in `queries.json`. JSON addresses are
numeric decimal values, unlike the point command's hexadecimal argument:

```json
[
  {"trace_position": 120, "pool": "local", "address": 16777216, "extent": 24},
  {"trace_position": 140, "pool": "local", "address": 16777216, "extent": 24}
]
```

```text
mwcc-tools source-arenas resolve-many capture.json --queries queries.json
```

The batch validates once and shares one chronological replay, returning results
in input order. Rust callers can retain `history.prepare()` and use
`resolve_many` for display results or `resolve_references` for private historical
receipts. See [allocation lifetimes](source-lifetimes.md) for receipt semantics.

## Coverage and query boundaries

The compilation-wide schema-1 `pv.arena.*` stream records requests, commits,
resets, gaps and independent cumulative checkpoints. The host validates raw
records rather than trusting serialized display summaries. Missing events,
counter mismatches, unsupported callbacks, stack overflow and truncated suffixes
cannot establish new coverage. An earlier reconciled prefix can remain queryable.
Older captures without the stream stay indeterminate.

A query requires a later independent checkpoint covering its position and an
observed invalidation establishing that pool's epoch. Padding, overlapping live
spans, active resets, unknown epochs and uncheckpointed suffixes remain
indeterminate. A checkpoint's own emission position is a coverage boundary, not
a queryable allocation observation, even if another checkpoint follows it.

Reset returns supply checkpoints. `--backend` also checkpoints immediately before
each matching `pv.function.end`. An arena-only capture has reset-end checkpoints;
no process-end checkpoint is inferred. Historical success does not assert current
liveness after a later reset. Graph fingerprint format 4 rejects arena captures
until arena normalization is implemented.

## What the producer observes

Nine request/commit pairs cover four ordinary allocators and five inline hash
allocation paths. Requests retain pre-alignment sizes; commits check reserved
spans against the post-store descriptor. Nineteen guarded islands preserve the
displaced instructions and machine state. Eight wrappers observe reset, release
and initialization, including the local lock. Actual failure-callback execution
emits an unsupported boundary.

The ordinary allocators reserve `(requested & ~7) + 8` bytes. Payload and padding
remain distinct: an 8-byte request reserves 16 bytes, and a zero-byte request has
an empty payload despite reserving 8. Invalid or overflowing spans do not establish
ordinary allocation coverage.

The operation, not its name, determines invalidation:

- Selecting/changing a current block is not a reset; the selector can reuse an
  earlier block. Its EAX is not a reliable success status. Observers validate the
  descriptor/span and account for failure callbacks and reentrancy.
- `CompilerTools_ResetPool` resets the optimizer pool; `freeaheap` resets the
  block pool. `freelheap` resets local only when its 16-bit lock is zero. A locked
  local reset preserves existing local allocation identities and repairs no gaps.
- `releasegheap` clears global; `releaseheaps` clears all five pools. Heap
  initialization clears descriptors, which is an identity boundary rather than
  proof of physical OS deallocation.
- `CompilerTools_ResetGallocAndPoolMemory` clears global, then changes optimizer
  allocation size and selects optimizer space with request zero. That selection
  alone is not optimizer invalidation.
- Hash initialization/string helpers allocate global spans inline, bypassing
  `galloc`. The statistics helper only reads pools and writes its caller's result.

Native Windows/Linux fixtures cover all nine pairs, independent totals,
historical reuse, payload/padding queries and malformed traces on all three
versions. Locked-reset and failure-callback paths are not claimed as exercised
native fixtures. Exact evidence and backend-coexistence limits are in
[extension status](../oracle-source-guided-status.md). These checks do not complete
the source-write/inline bridge or its native proof.

## Static reference audit

```text
mwcc-tools provenance audit-source-arenas --root ORACLE_ROOT --version GC_2_7
```

This read-only command checks the original PE checksum and symbol/split metadata.
Its JSON retains SHA-256 identities, descriptor fragments, direct instruction
references, containing-function guards and unresolved references. An empty
`unresolved` list does not resolve indirect aliases; the output deliberately keeps
`allocation_coverage_complete=false` and `reset_coverage_complete=false`.

Each pool descriptor is 20 bytes: `blocks` at 0, `allocsize` at 4, `curblock` at 8,
`curfreep` at 12 and `curfree` at 16. Heap-block headers are 16 bytes. Metadata symbols
can fragment a descriptor or include unrelated trailing storage; the audit uses
its exact 20-byte extent. Canonical addresses below need not equal relocated
runtime addresses.

| Storage | GC 1.3 | GC 1.3.2 | GC 2.7 |
| --- | --- | --- | --- |
| Local lock, 2 bytes | `5db510` | `5dd510` | `5df568` |
| Failure callback, 4 bytes | `5db512` | `5dd512` | `5df56a` |
| Heap pool | `5db516` | `5dd516` | `5df56e` |
| Optimizer pool | `5db52a` | `5dd52a` | `5df582` |
| Block pool | `5db53e` | `5dd53e` | `5df596` |
| Local pool | `5db552` | `5dd552` | `5df5aa` |
| Global pool | `5db566` | `5dd566` | `5df5be` |

Reproduce the static proof with:

```text
python tools/migration/verify_source_arenas.py --tools NATIVE_MWCC_TOOLS --oracle-root ORACLE_ROOT --output NEW_DIRECTORY
```

It retains complete reports/listings, commands, hashes and exit codes.
`--compare OTHER_NATIVE_OUTPUT` checks complete reports and listing bytes across
hosts. It does not build a producer or compile game source; static agreement
supplies no runtime ancestry claim. The runtime installer additionally guards
actual split COFF bodies and relocation target/addend identities.

Detailed private split evidence is retained in
`build/source-guided/source-origin-bridge/arena-split-audit/` when present; ignored
audit artifacts are not guaranteed in a fresh checkout. Bridge requirements and
causal gaps are described in the [source-origin audit](../oracle-source-origin-audit.md)
and [inline clone audit](../oracle-inline-clone-audit.md).
