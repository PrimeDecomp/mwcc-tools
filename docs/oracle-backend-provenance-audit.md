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

## Creation and register-number reuse: incomplete audit

`Registers.c` contains object-binding allocation paths for GPRs, pairs, FPRs and
VRs, plus coalescing-window checkpoints which can rewind virtual-register counters.
`Operands_AllocateGPR` and its underlying allocator need separate inspection for
backend-generated address/call/promotion temporaries. Object binding alone cannot
establish complete creation coverage. Register numbers therefore cannot be lifetime
IDs, even within one function.

Inspection also finds many direct/inlined counter increments in `Operands.c`
(address materialization and several `Operands_NewReg` paths). Wrapping only the
named allocation functions would miss these births. The next audit must enumerate
actual retail counter writes, including class-indexed accesses and checkpoint
rewinds, across code-generation units.

Next audit every counter-creation/reuse/category path against all three retail
images, establish function/expression/inline context, and build identity/coverage
events before using absence predicates. Entry-point names and C signatures are
leads until checked against the executable. Unsupported paths remain explicit.

## Comparison foundation

Native `colouring remap --json` now exposes format 2: complete captured graph node
domains, graph-only and instruction-only values, stored and observed adjacency
degrees, all instruction votes, competing/ambiguous targets and unmapped boundary
edges. Round/snapshot choices are explicit. Many-to-one instruction votes are not
reported as observed merges. Elimination remains `indeterminate` until backend
lifecycle coverage and value identity provide evidence; no backend hook coverage
is claimed by this comparison change.
