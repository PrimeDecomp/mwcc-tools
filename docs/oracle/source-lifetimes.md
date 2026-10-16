# Allocation lifetimes for source provenance

Use `source_lifetimes` to bind an observed pointer extent to one compiler
allocation and pool epoch. The `source_arenas` parser supplies validated runtime
observations and historical queries; start with its [capture and CLI usage](source-arenas.md).
Neither module establishes initialized node contents, source roles or semantic
value lifetimes.

For repeated historical queries, prepare the raw arena history once:

```rust
let prepared = history.prepare();
let displays = prepared.resolve_many(&queries);
let receipts = prepared.resolve_references(&queries);
```

`queries` contains `source_arenas::Query { trace_position, pool, address, extent }`.
Positions are one-based raw trace lines. `resolve_many` returns display data;
`resolve_references` returns private `HistoricalReference` receipts with evidence.
The point-query convenience API remains
`History::resolve_at(position, pool, address, extent)`.

## Historical queries and prepared captures

`prepare()` revalidates authoritative raw records once and creates an immutable
capture. Later edits to the public display model do not change it. Cloned
`PreparedHistory` values share capture identity; preparing the same history again
creates a different identity.

Each batch orders queries by position, replays one ledger forward, then restores
input order. Validation cost is shared within the prepared capture, and replay
is shared within a batch; separate batches start separate replays. Event/stack
limits bound retained entries and nesting, while memory also depends on record
sizes. No native throughput guarantee is implied.

A historical query needs a later independent checkpoint reconciling its prefix
and an observed invalidation establishing the requested pool epoch. Unknown
initial epochs, padding, active resets, overlapping live spans, missing records,
unsupported paths and uncheckpointed suffixes remain indeterminate. A valid
earlier prefix can survive truncation of a later suffix.

Checkpoint counters are sampled before formatting their record. A checkpoint's
own position is a coverage boundary, not an allocation observation, even if a
later checkpoint covers it. Reset returns and backend function ends supply
checkpoints; no compilation-end checkpoint is inferred. Arena-only captures have
reset-end checkpoints alone.

A `HistoricalReference` records allocation, pool, epoch, offset, extent, query
position and covering checkpoint. It establishes identity at that historical
position, not current liveness. `same_allocation` and `same_span` require the same
prepared capture; foreign receipts return indeterminate evidence. Fields are
private and no `Deserialize` implementation can import display data as a trusted
receipt. Serialized query results are display data, not transferable capabilities.

## Direct ledger API

`Ledger` accepts exact observed spans with globally increasing capture-local
allocation IDs, pool domains and epochs. Requested payload is separate from the
reserved span: a 30-byte payload reserving 32 bytes cannot authorize reading the
two padding bytes. Extent alone proves neither initialization nor node type.

`Ledger::resolve(pool, epoch, address, extent)` issues a private
`AllocationReference` only for one covered live allocation containing the payload
extent. It records allocation ID, offset, extent and ledger event. Receipts belong
to one ledger/capture, cannot be fabricated through public fields and cannot be
deserialized as trusted references.

Missing allocations, wrong epochs, overlapping reserved spans, lost coverage or
capacity exhaustion return explicit indeterminate evidence. Cross-domain
overlaps taint both allocations; the ledger never silently replaces an old
identity at the same address. Malformed spans whose footprint cannot be trusted
taint all pool coverage until each pool observes a fresh invalidation boundary.

`same_allocation` and `same_span` compare historical receipts, including retired
epochs. `is_live` separately checks current liveness. Equal allocation/span does
not prove unchanged contents, equal node revisions or transformation equivalence.
The source bridge also needs immutable tagged snapshots and actual
clone/lowering/replacement edges.

## Coverage and invalidation

An audited producer/parser must supply coverage. Successful allocations alone
cannot establish it, and `begin_pool` cannot erase a gap or assign identities to
pre-existing nodes. The runtime parser independently reconciles cumulative event
counts, exact order, request/commit pairing and reset nesting.

| Operation | Contract |
| --- | --- |
| `lose_coverage` | Retrospectively invalidates affected receipts, including retired epochs; use for missing records with unknown positions. |
| `lose_coverage_since` | Retains earlier evidence only when the earliest affected ledger event is justified. |
| `lose_coverage_from_now` | Applies only to a precisely located loss beginning now; it cannot explain an unknown end-total mismatch. |
| `observe_invalidation` | Requires an observed reset/release and a new epoch. It can establish future coverage for that pool, without repairing old receipts or other pools. An absent old epoch represents initial observed invalidation. |
| `observe_locked_local_reset` | Requires verified nonzero-lock `freelheap`; preserves the local epoch/allocations and repairs no gaps. |

These are arena identity boundaries, not automatic claims of physical
deallocation. Block selection or clearing another pool's descriptor is
insufficient. The [retail audit](source-arenas.md) distinguishes those operations
and the inline allocator bypasses.

The ledger has an explicit allocation capacity and never evicts history needed
to disambiguate receipts. An augmented interval index tracks active reserved
spans, including nested overlaps, with per-pool active IDs. Ordinary lookup avoids
scanning historical records; coverage invalidation may intentionally scan them.

Tests cover reuse, payload/padding, cross-pool overlaps, locked resets,
stale/foreign receipts, capacity, event/ID failures and recovery only at observed
barriers. A deterministic 20,000-step test compares the active interval index with
an independent brute-force oracle. Native arena fixtures pass on Windows/Linux
for all three versions; see [status](../oracle-source-guided-status.md) for the
exercised paths. Parser availability and allocation receipts alone do not prove
the source-node bridge; older captures without arena records remain indeterminate.
