# Allocation lifetimes for source provenance

The native `source_lifetimes` Rust module is an incremental host ledger for
guarded compiler allocation observations. It is a foundation for the original
source bridge. **No producer hooks or trace parser feed it yet.** Existing V6
captures cannot acquire proven node lifetimes by passing their pointers to it.

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
cannot establish it. The future parser must independently reconcile stream totals
and report missing/reset/alias coverage. `begin_pool` cannot erase an earlier gap
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
operations and the allocator bypasses that future hooks must capture.

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
