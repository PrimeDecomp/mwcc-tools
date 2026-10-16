# Backend provenance instrumentation audit

This is an active implementation audit for the [source-guided roadmap](oracle-source-guided-search.md).
Guarded coalescing probes are installed in the development Rust producer. Complete
creation/reuse/category provenance is still open.

## Coalescing: verified retail entry points

| Version | Symbol | Address | Size |
|---|---|---:|---:|
| GC 1.3 | `_SpillCode_CoalesceCopies` | `0x005794F0` | `0x44A` |
| GC 1.3.2 | `_SpillCode_CoalesceCopies` | `0x0057A1F0` | `0x44A` |
| GC 2.7 | `_SpillCode_CoalesceCopies` | `0x0057C4B0` | `0x44A` |

`capture_backend_provenance_sites.py` checks the configured stock compiler SHA-1,
decodes the complete functions with pinned Capstone 5.0.9, and verifies identical
code after normalizing only PE HIGHLOW relocations and direct-call displacements.
It records original/normalized hashes, call offsets, relocation positions and short
instruction guards. The first capture is retained at
`build/source-guided/backend-audit/coalescing-sites.json`; readable disassembly is
beside it. Capture with:

```text
uv run --python 3.13 --no-project --with capstone==5.0.9 python tools/migration/capture_backend_provenance_sites.py --oracle-root ORACLE_ROOT --output OUTPUT.json
```

The reconstructed C in `src/backend/InterferenceGraph.c` is a useful lead, but its
prototype/predicates are not sufficient hook specifications. The retail function
reads the active class and register count from globals. Its range, root-finding
and interference predicates are inlined. Its initial filter uses instruction flags,
not the simple opcode selection described by the reconstructed implementation.
Observing only function entry/exit or hypothetical helper returns would miss the
actual deciding branches. The implementation retains the original prologue/body
and inserts callbacks; it does not assume the reconstructed function signature.

The audited sequence includes copy-flag filtering (`0x10`), rejection by another
flag (`0x100`), operand validation and class filtering, root equality, an actual
interference-bit test, four short-circuit window bounds, an additional GPR/special-
register equality guard, the union-parent write and instruction unlinking. The
special register's semantic role remains unconfirmed; record its actual value and
condition rather than inventing a name. Bounds are read from class-indexed tables.

Candidate branch/write offsets relative to each entry are `0x10F`, `0x17B`,
`0x18B`, `0x1D6`, `0x244`, `0x271`, `0x280`, `0x289`, `0x291`, `0x2B9` and
`0x2C7`. The implemented merge observation is at `0x2CB`, **after** the parent
write. Entry (`0x7`), copy-candidate (`0x107`) and exit (`0x442`) observations bring
the installed total to fourteen sites. The native linker checks the normalized
full-body hash and exact relocation layout, redirects every local branch after
widening short branches, and retains original symbol/data relocations. Callback
stubs save GPRs, flags, x87, MXCSR and XMM state. The saved PUSHAD ESP plus four
recovers retail ESP; its `+0xC` slot identifies the current instruction.

`mwcc-tools oracle compile-trace --backend` enables `pv.coalesce.*` records.
`mwcc-tools provenance inspect` validates deciding paths, EFLAGS, physical-register
window bypass and actual parent writes, and keeps absent/incomplete coverage
indeterminate. See the [development API](oracle/provenance.md).

All three versions pass stock-object and existing-trace equality with probes on
and off on Windows/Linux synthetic fixtures. The full Echoes `CElementGen.cpp`
unit passes on both hosts: 182 complete passes, 222 merges, 500 class rejections,
621 interference rejections, 18 destination-window and 11 source-window
rejections, and 3 already-equal-root decisions. These are whole-unit counts, not
RenderParticles-only counts. The particular fixtures do not exercise every rare
branch outcome; they do not establish creation or semantic-origin coverage.

## Creation and register-number reuse: direct-write observations installed

`provenance audit-counters` now audits the actual pinned images in Rust. It finds
1,188/1,191/1,200 direct counter references in GC 1.3/1.3.2/2.7, including
570/571/578 writes. All relevant functions decode completely; no direct reference
is unclassified or unaccounted for, and no address-taking reference is found in
that scope. Each version has four non-increment writes: initial reset, scratch
rewind, high-water restoration and allocation reset. The remaining writes are
increments, mostly inlined across backend/optimizer units. Computed aliases/bulk
writes outside this direct-reference scope remain an explicit audit limitation.

The retail audit corrects misleading names: `Operands_AllocateGPR` updates the
current instruction's flags; it does not create a virtual register.
`fn_004cf6d0` computes a physical-register range. The actual variable-assignment
increment is in `fn_004fcba0`, with many other increments in instruction selection,
operands, calls, struct moves and spills. Reconstructed C version branches and
symbol/container names must not be treated as authoritative semantics.

Fixed-size call islands now execute and observe every audited write, with two
additional context sites. The generator's actual entries are `0x433590`,
`0x433310`, `0x4333F0`; later metadata folds it into a larger container. All three
share the audited object store at `+0x9D` and stack epilogue at `+0xDB4`.
Original section sizes and unpatched bytes/symbol values stay unchanged; no
instruction branch or jump table needs to be moved for these counter probes.

`pv.function.*`, `pv.value.birth`, `pv.counter.*` and `pv.expression.*` record
function/creation observations, counter reset/rewind/restore events, phases and
available partial statement-root origins. The host preserves separate generations
when a number recurs. Direct-counter continuity is checked rather than assumed.
The new reuse fixture demonstrates 239 births, one rewind and twelve reused numbers
with stock-equivalent output on all three versions and both hosts. The full particle
unit records 6,356 births and 4,325 partial expression observations, without gaps or
orphan writes in these captures.

The graph hook now captures the current instructions and graph immediately before
retail simplification, with class-specific physical thresholds and a birth cutoff.
The new `pv.graph.*` records preserve actual visibility, graph-only nodes, stored
degrees and ordered adjacency. Function-end graph totals detect missing snapshots.
The host keeps candidate generations and confirmed merge contributors within a
counter-reset epoch; it does not substitute the latest birth for a reused number.

Complete semantic categories, full expression/inline ancestry and semantic
lifetime correspondence remain open. Birth IDs are local to a capture; no
cross-variant identity or elimination proof is claimed yet. The old `remap`
before/after instruction boundaries are distinct from these new graph snapshots.

## Object bindings and allocator range categories

The single-register creator `_fn_004fcba0` has the same normalized full-body hash
on all three versions:
`c7b3553183e2a39829ae89e562ce3bc1801d954270d1ba1e67e5017ecf4da38b`.
At its increment (`+0x28`), EBX is the variable-info pointer and the object argument
is at retail ESP+0x1C. The counter island adds its own CALL return slot, so its
saved PUSHAD ESP+8 recovers retail ESP. `_Registers_BindVR` is actually the paired
GPR assignment path; its normalized hash is
`bdde62ffa0f7872d0c8afa6518ddbf92b4b0270356083e1048ba87a2590886c5`.
At `+0x9A` and `+0xA6`, EBX is the object and EDI is variable info. These guarded
sites now attach object/type/storage/name evidence to the respective births.
They observe flags before the later binding stores; pair parts mean creation order.

Two additional fixed-size islands observe the completed stores at
`Registers_SnapshotInitialObjectRange+0x21` and
`Registers_BeginCoalesceWindow+0x42`. Their exact 16-bit store targets, RETs,
complete retail containers and table layouts are checked. The short initial/first
tables and 32-bit last table use VR/FPR/GPR order. Graph snapshots read the actual
current bounds, and the parser checks opening markers against observed counters,
function totals, and graph bounds. Initial-object, before-window, inside-window
and after-window categories refer to these actual ranges, not guessed source roles.
The `-O0` reuse path has no range markers, so its range categories remain unknown.

The graph hook now reads live physical limits: target initialization may occur
after generator entry. The host also cross-checks the preceding coalescer header,
making stale early development snapshots with a zero boundary indeterminate.
The internal descriptor is 44 bytes and uses `OracleRust_CounterProbeV3`; native
linking rejects older common-hook ABIs before installing these islands.

The descriptor also selects the two guarded Normalize symbol-address births and
the audited data link-name cache offset (56 in GC 1.3/1.3.2, 54 in GC 2.7). These
observations bind the actual Operand/Object to each high/low address half without
calling name-generation helpers. The reconstructed Object header retains the
older suffix and cannot prove the later retail layout. The initial full-case
failure and corrected cache-access byte evidence are retained under
`build/source-guided/materializer-audit/`; the failed producer is not a runtime proof.

The standalone object-binding fixture retains eight bindings (six paired words)
and both before/inside-window categories with stock-equivalent output on all
three versions and both hosts. This does not establish exact origins for all
backend address, call-result or array-promotion temporaries.

## Comparison foundation

Native `colouring remap --json` now exposes format 2: complete captured graph node
domains, graph-only and instruction-only values, stored and observed adjacency
degrees, all instruction votes, competing/ambiguous targets and unmapped boundary
edges. Round/snapshot choices are explicit. Many-to-one instruction votes are not
reported as observed merges. Elimination remains `indeterminate` until backend
lifecycle coverage and value identity provide evidence; no backend hook coverage
is claimed by this comparison change.
