# Source arena reference audit

Original source nodes and optimizer nodes can outlive backend function scopes.
Their addresses can also be reused after a heap reset. Before joining a source
write to an inline copy or reconstructed backend expression, the oracle needs
the allocation lifetime of each observation. A raw pointer, source line, or
matching bytes cannot supply that lifetime.

The native static audit inventories the relevant retail storage and its direct
relocated references:

```text
mwcc-tools provenance audit-source-arenas --root ORACLE_ROOT --version GC_2_7
```

It reads the checksum-verified original PE and symbol/split metadata without
building or changing the compiler. The JSON records their SHA-256 identities,
pool storage fragments, direct instruction references, containing function
guards and unresolved references. All five pool descriptors are 20 bytes. The
metadata symbol at each descriptor's start is only four bytes; other symbols
cover its remaining fields, sometimes together with trailing unrelated storage.
The audit range is the descriptor's exact 20 bytes, irrespective of those symbol
fragment sizes.

| Storage | GC 1.3 | GC 1.3.2 | GC 2.7 |
| --- | --- | --- | --- |
| Local lock, 2 bytes | `5db510` | `5dd510` | `5df568` |
| Failure callback, 4 bytes | `5db512` | `5dd512` | `5df56a` |
| Heap pool | `5db516` | `5dd516` | `5df56e` |
| Optimizer pool | `5db52a` | `5dd52a` | `5df582` |
| Block pool | `5db53e` | `5dd53e` | `5df596` |
| Local pool | `5db552` | `5dd552` | `5df5aa` |
| Global pool | `5db566` | `5dd566` | `5df5be` |

Coordinates are hexadecimal canonical retail addresses. Each descriptor has
`blocks` at offset 0, `allocsize` at 4, `curblock` at 8, `curfreep` at 12 and
`curfree` at 16. Heap blocks have a 16-byte header. These layouts describe
inspected retail operations; arbitrary payload bytes are not initialized typed
data merely because they lie in one of these heaps.

## Findings that affect instrumentation

All three versions have 12 direct references to the heap descriptor, 23 to the
optimizer descriptor, 21 to the block descriptor, 22 to the local descriptor,
51 to the global descriptor and six to the local lock. Every such relocated
reference falls within an instruction in a fully decoded named containing
function. This establishes the direct-reference inventory only. It does not
resolve writes through an alias passed to a helper.

* `galloc`, `lalloc`, `aalloc` and `oalloc` each round the requested size as
  `(size & ~7) + 8`. A request for 8 bytes reserves 16; padding must not become
  a typed node's readable extent. Zero, overflowing or invalid requests need
  their own runtime evidence rather than ordinary positive-allocation semantics.
* `InitNameHash`, `GetHashNameNode` and `GetHashNameNodeExport` contain direct
  global-pool allocation sequences. They bypass a `galloc` wrapper. Each of the
  latter two functions has two such sites. Capturing only calls to the four
  named allocators therefore misses real allocation spans.
* `select_or_allocate_pool_node` mutates a descriptor through its input pointer.
  It preserves a previous block's remaining space and can return to an earlier
  block. A change of current block alone is not an arena reset. Its failure path
  can invoke a callback; callback reentrancy and a failed selection must remain
  explicit runtime coverage questions.
* The retail symbol `CompilerTools_ResetPool` is the optimizer reset operation
  called `freeoheap` in reconstructed source. Its GC 2.7 entry is `440bb0`.
  `freeaheap` resets the block pool. `freelheap` resets the local pool only when
  the 16-bit lock is zero. Observing a locked `freelheap` call must preserve
  existing local allocation identities.
* `releasegheap` frees and clears the global pool. `releaseheaps` frees and clears
  all five pools. Both heap-initialization functions clear descriptors. Their
  observed invalidation scopes must be represented separately from selection.
* `CompilerTools_ResetGallocAndPoolMemory` frees and clears the **global** pool,
  then changes the optimizer allocation size and selects optimizer space with
  request zero. Its name does not justify treating that selection as optimizer
  invalidation.
* `_fn_00441220` references all five pools to collect statistics; its writes are
  to the caller's result structure. A direct reference is not automatically an
  allocation or reset site.

Future runtime instrumentation must guard the actual call/return or store sites,
including original split COFF relocation target identities. The audit's PE body
hashes and normalized fields are an inventory, not that installation contract.
The output deliberately retains `allocation_coverage_complete=false` and
`reset_coverage_complete=false`, even when `unresolved` is empty.

## Reproducing the static proof

```text
python tools/migration/verify_source_arenas.py --tools NATIVE_MWCC_TOOLS --oracle-root ORACLE_ROOT --output NEW_DIRECTORY
```

The script runs all three versions, retains complete JSON reports and retail
listings for every containing function (including the aliased selector through
its failure-callback reference), and checks the inlined allocation and locked
reset findings. It records native analyzer and script hashes, commands and exit
codes. `--compare OTHER_NATIVE_OUTPUT` requires equal complete reports and
listing bytes from another native host. It never builds a producer or compiles a
game source. A successful run proves static agreement; it supplies no runtime
source ancestry or completeness claim.

The separate host [allocation lifetime ledger](source-lifetimes.md) consumes
guarded allocation/reset observations once producer integration exists. It does
not infer runtime coverage from this static inventory.

A further retained split audit is in
`build/source-guided/source-origin-bridge/arena-split-audit/`. Its `audit.py`
checks 22 containing bodies per version against untouched V6 retail splits,
masking only actual COFF relocation words and independently resolving every
target/addend back to the retail address. The manifest
`arena-split-guards.json` has SHA-256
`0d46c8907df2af153283a505ebdcebe46c59d789a927fb61e7d94bfad1c7e2a3`.
It records five inline and four ordinary allocation commit windows per version,
with exact result/size registers, plus reset ABIs. The selector's EAX is not a
reliable success status: runtime observers must validate the actual descriptor
and returned span, including callback/failure paths. The statistics helper's
external table lives in writable `.data`, even though that helper only reads it.
These ignored local artifacts are not guaranteed present in a fresh checkout.

The broader bridge and mutation requirements remain in the
[source-origin audit](../oracle-source-origin-audit.md) and
[inline clone audit](../oracle-inline-clone-audit.md).
