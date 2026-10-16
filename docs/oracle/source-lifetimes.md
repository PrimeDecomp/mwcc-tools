# Allocation lifetimes for source provenance

The native `source_lifetimes` Rust module is an incremental host ledger for
guarded compiler allocation observations. It is a foundation for the original
source bridge. The development `source_arenas` parser now connects the guarded
arena protocol to this ledger. Native fixture validation passes on Windows and
Linux across all three versions; see the [status](../oracle-source-guided-status.md)
for exercised paths and limits. The parser alone is not a coverage claim. Existing
V6 captures have no arena stream and remain indeterminate.

The ledger accepts exact observed allocation spans with globally increasing
capture-local IDs, a pool domain and epoch. Requested payload bytes are separate
from the allocator's reserved span. A 30-byte request reserving 32 bytes cannot
authorize a read into its two padding bytes. Neither extent proves initialized
data, a node type, or a source role.

`Ledger::resolve(pool, epoch, address, extent)` issues a private
`AllocationReference` only for one covered live allocation containing the requested
payload. It records allocation ID, offset, extent and observation event. Receipts
belong to one ledger/capture, cannot be deserialized into trusted references, and
cannot be fabricated through their public API. A missing allocation, wrong epoch,
overlapping reserved spans, lost coverage or exhausted capacity returns explicit
indeterminate evidence. Cross-domain overlaps taint both affected allocations;
the ledger never silently replaces an old identity at the same address.

`same_allocation` and `same_span` compare historical receipts, including receipts
from before an observed reset. `is_live` separately asks whether a receipt's
allocation is still live now. Two receipts for the same allocation/span do not
prove unchanged contents, equal node revisions, transformation equivalence or a
semantic value lifetime. The original-source bridge still needs immutable tagged
node observations and actual clone/lowering/replacement edges.

## Coverage and invalidation

Coverage is supplied by an audited producer/parser; successful allocations alone
cannot establish it. The development parser independently reconciles cumulative
request, allocation, reset, gap and checkpoint totals, with exact event order,
request/commit pairing and reset nesting. `begin_pool` cannot erase an earlier gap
or assign identities to pre-existing nodes.

* `lose_coverage` retrospectively invalidates affected receipts throughout the
  capture, including retired epochs. Use it when an end total reveals missing
  records whose positions are unknown.
* `lose_coverage_since` requires a justified earliest affected ledger event.
  Earlier receipts can remain valid.
* `lose_coverage_from_now` is only for a precisely located loss beginning now.
  It must not stand in for an unexplained end-total mismatch.
* `observe_invalidation` requires an actually observed reset/release boundary and
  a new epoch. It can establish fresh coverage for that pool's future epoch;
  it does not repair old receipts or another pool's history. An absent old epoch
  explicitly represents an observed initial invalidation before any pool scope.
* `observe_locked_local_reset` represents a verified nonzero-lock `freelheap`
  observation. It preserves the local epoch and allocations, and repairs no gaps.

These are observed arena identity boundaries, not automatic claims of physical
deallocation. Selecting another block, or clearing some other pool's descriptor,
is insufficient. The [retail arena audit](source-arenas.md) distinguishes those
operations and the allocator bypasses covered by the new observation contract.

## Historical arena queries

`source_arenas::History::resolve_at(position, pool, address, extent)` validates the
retained raw records and rebuilds the ledger through a one-based raw trace
position. A later independent checkpoint must reconcile that prefix. Epoch zero
remains unknown until an observed invalidation establishes coverage for the
requested pool. Active resets, uncheckpointed suffixes, missing records or totals,
unsupported paths, allocator padding and overlapping live spans remain
indeterminate. A known earlier checkpoint can still cover an earlier historical
observation after a later suffix is truncated.

The checkpoint's counters are sampled before its record is formatted. Its own
record position is therefore a coverage boundary, not a queryable allocation
observation, including when a later checkpoint covers that position. There is no
inferred compilation-end checkpoint. Backend tracing supplies function-end
checkpoints; an arena-only capture has reset-end checkpoints alone.

The returned allocation ID, epoch, offset and extent describe the requested
historical position. A later reset can retire that allocation without making the
historical observation indeterminate. The result does not assert current liveness,
an original source role or unchanged node contents. Serialized query results are
display data, not private `AllocationReference` receipts; queries rebuild from raw
evidence rather than trusting deserialized coverage summaries.

Each query currently clones and revalidates the retained records, then replays
allocations into a new ledger. Normal work is proportional to retained record
bytes plus allocation replay and active-index operations; reporting overlaps adds
the matching spans. Event-count and stack limits bound retained entries and
nesting; memory still depends on input record sizes. A large batch of node
queries repeats this work. This API currently serves bounded CLI
point queries. An original-source bridge resolving thousands of observations
will need shared validated replay or batch queries before making throughput
claims. No such optimization or native performance result is implied here.

The history has an explicit allocation capacity and never evicts records that
could disambiguate old receipts. An augmented interval index covers active
reserved spans, including nested overlaps; each pool also retains its active IDs.
Ordinary allocation/lookup avoids scanning historical records. Coverage failures
can intentionally scan history to invalidate earlier evidence.

Focused tests cover address reuse, payload/padding boundaries, overlapping pools,
locked reset behavior, stale and foreign receipts, capacity, event/ID errors,
initial and retrospective gaps, and recovery only through an observed barrier.
A deterministic 20,000-step test compares the interval index with an independent
brute-force active-span oracle during nested/cross-domain insertion and retirement.
