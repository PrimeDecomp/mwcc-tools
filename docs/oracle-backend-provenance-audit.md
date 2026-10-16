# Backend provenance instrumentation audit

This is an active implementation audit for the [source-guided roadmap](oracle-source-guided-search.md).
It does not claim complete provenance or installed backend probes.

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
actual deciding branches. The exact calling context still needs validation before
installing a wrapper.

The audited sequence includes copy-flag filtering (`0x10`), rejection by another
flag (`0x100`), operand validation and class filtering, root equality, an actual
interference-bit test, four short-circuit window bounds, an additional GPR/special-
register equality guard, the union-parent write and instruction unlinking. The
special register's semantic role remains unconfirmed; record its actual value and
condition rather than inventing a name. Bounds are read from class-indexed tables.

Candidate branch/write offsets relative to each entry are `0x10F`, `0x17B`,
`0x18B`, `0x1D6`, `0x244`, `0x271`, `0x280`, `0x289`, `0x291`, `0x2B9` and
`0x2C7`. These are audit candidates, **not yet installed probes**. Branch polarity,
saved registers/flags, stack-local instruction identity, relocated code/data and
all control-flow edges must be preserved and verified before runtime use. A probe
must observe the original branch inputs/outcome; it must not replace the retail
coalescing algorithm with a reconstructed one.

## Creation and register-number reuse: incomplete audit

`Registers.c` contains object-binding allocation paths for GPRs, pairs, FPRs and
VRs, plus coalescing-window checkpoints which can rewind virtual-register counters.
`Operands_AllocateGPR` and its underlying allocator need separate inspection for
backend-generated address/call/promotion temporaries. Object binding alone cannot
establish complete creation coverage. Register numbers therefore cannot be lifetime
IDs, even within one function.

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
