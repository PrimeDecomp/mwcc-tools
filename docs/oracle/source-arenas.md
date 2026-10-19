# Source arenas and allocation lifetimes

Arena observations resolve a historical compiler pointer to its allocation and pool epoch. They establish
allocation identity, not initialized contents, source ancestry, unchanged node revisions or semantic equivalence.

```sh
mwcc-tools call oracle.trace source=source.cpp version=GC_2_7 flags='["COMPILER_FLAGS"]' \
    streams.source_arenas=true capture_path=capture.json --oracle-root ORACLE_ROOT
mwcc-tools call source_arenas.inspect capture=capture.json
mwcc-tools call source_arenas.resolve capture=capture.json position=120 pool=local address='"01000000"' extent=24
mwcc-tools call source_arenas.resolve_many capture=capture.json queries_path=queries.json
```

- `position` is a **one-based raw trace line**; `address` is hexadecimal text; `extent` is a byte count. Pools are
  `heap`, `optimizer`, `block`, `local` and `global`.
- `queries.json` is an array such as
  `[{"trace_position": 120, "pool": "local", "address": 16777216, "extent": 24}]`, with **decimal** addresses. The
  batch validates once, shares one replay and returns results in input order.
- A result gives the allocation, epoch, offset, extent and covering checkpoint.

## Coverage

The `pv.arena.*` stream (schema 1) records requests, commits, resets, gaps and cumulative checkpoints; the parser
validates the raw records. Missing events, counter mismatches, unsupported callbacks, stack overflow and truncated
suffixes add no coverage; an earlier reconciled prefix stays queryable.

A query needs a later checkpoint covering its position and an observed invalidation establishing the pool's epoch.
Padding, overlapping live spans, active resets, unknown epochs and uncheckpointed suffixes are indeterminate. A
checkpoint's own position is a coverage boundary, not an allocation observation.

Reset returns supply checkpoints; `streams.backend` adds one before each `pv.function.end`. No process-end
checkpoint is inferred. Success is historical: it does not assert liveness after a later reset. Fingerprint
format 4 rejects arena captures.

## What the producer observes

- Nine request/commit pairs: four ordinary allocators and five inline hash allocation paths. Requests keep
  pre-alignment sizes; commits check reserved spans against the post-store descriptor. Eight wrappers observe
  reset, release and initialization, including the local lock. A failure callback emits an unsupported boundary.
- Ordinary allocators reserve `(requested & ~7) + 8` bytes. Payload and padding stay distinct: an 8-byte request
  reserves 16, and a zero-byte request has an empty payload.

The operation, not its name, decides invalidation:

- Selecting a current block is not a reset (it can reuse an earlier block; its EAX is not a success status).
- `CompilerTools_ResetPool` resets optimizer; `freeaheap` resets block; `freelheap` resets local only when its
  16-bit lock is zero (a locked reset keeps local identities and repairs no gaps).
- `releasegheap` clears global; `releaseheaps` clears all five. Heap initialization clears descriptors (an identity
  boundary, not proof of OS deallocation).
- `CompilerTools_ResetGallocAndPoolMemory` clears global, then selects optimizer space with request zero; that
  selection is not an optimizer invalidation.
- Hash initialization/string helpers allocate global spans inline, bypassing `galloc`.

## Static audit

```text
mwcc-tools provenance audit-source-arenas --root ORACLE_ROOT --version GC_2_7
```

Read-only: checks the original PE checksum and symbol/split metadata, and gives SHA-256 identities, descriptor
fragments, direct references, containing-function guards and unresolved references. It keeps
`allocation_coverage_complete=false` and `reset_coverage_complete=false`: direct references do not resolve indirect
aliases. A pool descriptor is 20 bytes (`blocks` 0, `allocsize` 4, `curblock` 8, `curfreep` 12, `curfree` 16); a
heap-block header is 16. The storage addresses per version are in `source_arena_audit.rs`.

- `python tools/migration/verify_source_arenas.py --tools MWCC_TOOLS --oracle-root ROOT --output NEW_DIR` keeps the
  reports, commands and hashes; `--compare OTHER_OUTPUT` checks them across hosts.
- `python tools/migration/verify_source_arena_runtime.py` exercises the runtime fixtures.

## Rust API: histories and ledgers

```rust
let history = source_arenas::parse(&trace)?;
let prepared = history.prepare();
let displays = prepared.resolve_many(&queries);      // display data
let receipts = prepared.resolve_references(&queries); // HistoricalReference receipts
let one = history.resolve_at(position, pool, address, extent);
```

`prepare()` revalidates the raw records once into an immutable capture; clones share its identity, preparing again
makes a new one. Each batch replays one ledger forward in position order.

A `HistoricalReference` records allocation, pool, epoch, offset, extent, query position and checkpoint.
`same_allocation` and `same_span` need the same prepared capture (else indeterminate). Fields are private and
nothing deserializes into a receipt: serialized results are display data.

`source_lifetimes::Ledger` takes exact spans with increasing capture-local allocation IDs, pool domains and epochs.
Payload is separate from the reserved span (a 30-byte payload reserving 32 does not authorize reading the padding).
`Ledger::resolve(pool, epoch, address, extent)` issues a private `AllocationReference` only for one covered live
allocation containing the extent. Missing allocations, wrong epochs, overlaps, lost coverage and capacity exhaustion
are indeterminate; cross-domain overlaps taint both allocations; malformed spans taint all pools until each sees a
fresh invalidation. `is_live` checks current liveness separately.

| Operation | Contract |
| --- | --- |
| `lose_coverage` | Invalidates affected receipts retrospectively, including retired epochs (missing records at unknown positions). |
| `lose_coverage_since` | Keeps earlier evidence only when the earliest affected event is justified. |
| `lose_coverage_from_now` | A precisely located loss beginning now; not for an unknown end-total mismatch. |
| `observe_invalidation` | An observed reset/release and a new epoch: future coverage for that pool, no repair of old receipts. |
| `observe_locked_local_reset` | A verified nonzero-lock `freelheap`: keeps the local epoch and repairs no gaps. |

Coverage comes only from an audited producer and parser: `begin_pool` cannot erase a gap or assign identities to
existing nodes. The ledger has a fixed allocation capacity and never evicts history; an interval index tracks
active spans.
