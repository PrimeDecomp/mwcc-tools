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
The internal descriptor is 48 bytes and uses `OracleRust_CounterProbeV5`; native
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

## Guarded scalar-array and call-result origins

The V4 descriptor selects eight existing counter islands for additional read-only
origins. The ordinary birth remains the function/sequence identity. These records
describe specific retail mechanisms; they do not establish complete semantic
lifetimes, optimizer replacement history or inline ancestry.

`COpt_ArrayToRegister` has size `0x5c8` and the same normalized full-body hash on
all three versions:
`4492771ad6e0733be32e5c687da872cfbe999cf0be8b49b75930c3b0645a5ff8`.
Its FPR/GPR increments are at offsets `+0x332` and `+0x345`:

| Version | FPR site | GPR site |
|---|---|---|
| GC 1.3 | `0x571072` | `0x571085` |
| GC 1.3.2 | `0x571d32` | `0x571d45` |
| GC 2.7 | `0x573ff2` | `0x574005` |

At both sites ESI holds the promotion record, ECX the element index and EAX the
new register. Retail stores and consumers establish the record's original array
Object at `+4`, flags at `+8`, element size at `+0xc`, array size at `+0x10`,
element count at `+0x14` and register slots at `+0x1c`. The constructor
`build_array_loopvars`, size `0x1e3`, is separately guarded by exact pinned bytes:

| Version | Constructor address | SHA-256 |
|---|---|---|
| GC 1.3 | `0x571310` | `084ecf59c2d0aaf301f2581df94d468013b1df8a3fff018327f6314e91b4cd37` |
| GC 1.3.2 | `0x571fd0` | `b011ab9c2d7652b78e6f00abab6468fccf52f501a72beb39a3601cae6730e217` |
| GC 2.7 | `0x574290` | `aa78233ab4f31a3906968dad68aacb7470763b4f386871873e83c6a3083ae00b` |

`pv.value.array` binds the birth directly to this Object and element, retaining
bounded name bytes, type/storage fields and raw flags. Runtime checks require
one to eight elements, a valid index, positive element size, consistent sizes,
array/element type sizes, the observed class flag and the actual newly allocated
register. This does not cover the separate vector-array pass: its VR increment
at `fn_0052cf10+0x14b` is still only an ordinary birth. It also does not recover
an array's declaration token or its full inline ancestry.

`FunctionCalls_GenerateCall`, size `0x6a6`, has normalized full-body hash
`0aefa5c61a1dd82488fcce8b2480816f051613daa95f64ae7b543084307112bc`.
The entry addresses are `0x5267c0`, `0x527320` and `0x5281b0` respectively.
Its six result increments have offsets `+0x4c7` (FPR), `+0x514` (VR), `+0x55d`
(another FPR branch), `+0x602` and `+0x612` (GPR pair creation order), and
`+0x66d` (GPR). The reconstructed C omits the second FPR branch; it is not an
authoritative exhaustive list of return cases.

The guarded frame retains the actual call ENode at retail ESP `+0x64`, its
return Type at `+0xc` and output Operand argument at `+0x68`. EAX is the same
output Operand at each increment. The observer checks those identities, the
call-node kind, return-type relation and output tag. `pv.value.call_result`
retains the actual node/callee/type/output pointers, class-independent pair role,
bounded expression text and a raw 24-byte output snapshot. The register field
means the observed birth, not a final result read from that partial Operand.
The following retail instruction has not yet stored the new result register.
Consequently `output_complete=0` and `tree_complete=0`; inactive raw output bytes
can contain incidental pointers or uninitialized data and are not semantic
fingerprint inputs.

The normal dispatcher reaches this helper through
`generate_intrinsic_or_function_call` (table kinds 54/55). Two additional direct
calls originate in intrinsic fallback paths. Observing the actual helper frame
covers these routes without assuming that the active outer dispatch node is the
helper's call node. Source inline ancestry remains partial: the existing origin
table records certain call references, not arbitrary array objects or generated
nodes. Function headers declare `guarded_scalar_v1` and
`guarded_call_result_v1`; function ends count their successful observations.
Missing records, runtime guard failures and missing scope declarations cannot
establish absence. V4 producer runtime proofs must be built separately from the
sealed V3 trees.

The first V4 Windows proof is retained under
`build/source-guided/birth-origin-audit/windows-producer/rust-producer-57ijuxk6`:
all three versions pass the 36 legacy C/Rust comparisons, with common hook-object
SHA-256 `539d259896c03824763ae47b9aca3d7e5ef7092378cbe8cbe2679e9df32b8f3b`.
Separate stock-verified backend captures exercise all eight selected origin sites
on every version, without provenance gaps. Ordinary word/pair/float/double calls
cover four call sites; `__vector float` with `-proc 7400 -vector on` covers the VR
site; `__vec2x32float__` with `-proc gekko` covers the extra FPR/PS_MR site. Small
integer and double array cases exercise the GPR and FPR promotion loops. Their
raw captures, partial output records and source inputs are retained under the
same audit directory's `call-paths/`, `windows-promoted/` and
`windows-promoted-fpr/` directories. These enabled captures are separate from
full probe off/on and full-function acceptance checks.

The completed V4 fixture verification now covers 45 off/on pairs (90 captures)
on each host, across all three versions, with stock-equivalent objects and
unchanged preexisting trace records. Every version records six scalar-array
elements and 34 call-result births across the fixtures, including all six guarded
call sites and both scalar-array classes. Windows and Linux stock-object hashes,
typed evidence summaries and counter-island COFF bytes agree. The shared hook
hash above also agrees across hosts; the linked producer files retain their own
host-specific hashes.

Windows evidence is at
`build/source-guided/birth-origin-audit/backend-windows-full/verification.json`.
The separately built Linux producer and 36 legacy comparisons are retained at
`<linux-cache>/mwcc-birth-origin-audit/producer/rust-producer-gmo66ilq`,
with off/on evidence at
`<linux-cache>/mwcc-birth-origin-audit/backend-full/verification.json`.
`build/source-guided/birth-origin-audit/cross-host-verification.json` records the
cross-host checks. Per-invocation timings are retained; concurrent verification
does not establish controlled trace overhead or search throughput.

## Frontend aggregate scalarization

Backend array promotion is separate from the frontend rewrite of an aggregate
part into a new scalar Object. Retained V4 traces show that the live ghost-case
values `@17` (empty-loop/store-after), `@21` (inline helper), and `@5986`
(`RenderParticles`) are created at the scalarizer's temp-object return,
`0x461748` in GC 2.7. They are not CSE-created Objects. Existing `lc.use` records
identify byte offset 4 of `flag`/`noRota`; the old name-only temp/part ordering
does not establish the exact Object association.

V5 saves original EBX at the naked `create_temp_object` entry, forwards the
original caller address, and observes the returned Object. Only the audited
scalarizer return is recognized. The scalarizer base/return addresses are:

| Version | Scalarizer | Temp-object return |
| --- | --- | --- |
| GC 1.3 | `0x460780` | `0x4608a8` |
| GC 1.3.2 | `0x460a00` | `0x460b28` |
| GC 2.7 | `0x461620` | `0x461748` |

All three scalarizer bodies are `0x3a2` bytes with normalized SHA-256
`7ff5c374f005e7bf1c782113ee92c9c311518e26bf27c3f88a0bab8072d46b3a`.
The `record_var_part_use` constructor is `0x3fc` bytes, normalized SHA-256
`d528fa438432ca999f55a60133384fc4b116313a9569f601ba5ef6779e2f65cd`;
`create_temp_object` is `0x76` bytes, normalized SHA-256
`36d59c0f578dc022a12a7f3c9f6096214f2353f93a9823f02ab3250df1322cb1`.
Normalization zeros PE base fixups and direct-call operands. Installation also
checks each version's exact pinned PE body and the supplied split COFF bodies,
relocation positions, target-symbol bindings and raw addends. The identity guard
includes local symbol section/value and auxiliary records; changing a call target
cannot hide inside a normalized operand. An older common object lacks the V5 support export
and is rejected before instrumentation.

At the audited return, EBX is the actual `IROVarPart` and EAX the newly created
Object. Retail constructor stores establish part fields `+0` source linear,
`+4` flags, `+6` VarRecord, `+0xa` byte offset, `+0xe` byte size, `+0x12` first
bit, `+0x16` last bit, `+0x1a` Type, and `+0x22` use list. VarRecord `+2` is the
source Object. The bit fields are initialized by retail arithmetic, including
the separate bitfield offset/width path; they are not inferred from the
reconstructed declaration's unnamed bytes. The returned Object's Type and
VarInfo are initialized by the guarded temp constructor.

`pv.scalar.begin/binding/end` records carry a capture-local pass ID, original
function Object, binding ordinal, actual return site, source/temporary Objects
and VarInfo pointers, types, complete/bounded source-name bytes, and byte/bit
intervals. Array element type is read from the original array Type, independently
of the temporary's size. Nulls, unsupported scalar/aggregate kinds, inconsistent
types and invalid bounds emit scoped gaps. These records observe creation of the
replacement Object, not complete source declaration or inline ancestry.

Function begin records carry the same function Object; scalar passes run inside
that enclosing function context, before the backend Object births. Function end
records carry an independent pending summary of scalar pass IDs and binding
totals. A bounded 256-entry cache is consumed once per function end. Missing,
overflowed, open or inconsistent
summaries are `unknown`, never an invented zero-pass observation. Real scalar
passes with zero generated bindings can be complete. This permits the host to
detect a wholly missing scalar stream as well as a truncated binding list.

Exact backend joins require the matching function Object, capture interval,
temporary Object and VarInfo, with ambiguous/reused identities retained as gaps.
A whole-array-element role additionally needs matching source element type/size,
in-bounds byte interval, agreeing full-width bit interval and no bitfield flag.
Raw pointers are capture identities, not cross-capture anchors or fingerprint
inputs. An invisible original-array promotion still does not prove coalescing or
elimination of the separately scalarized live value.

Fresh V5 producers on Windows and Linux each pass all 36 legacy C/Rust
comparisons. Their common hook COFF SHA-256 agrees:
`24a0e4e95a3e0b6f54d3246ab7c8161d396a1f34cfc0c72a1c338331f88ca839`.
The sealed producers are
`build/source-guided/scalar-origin-audit/windows-corrected-producer/rust-producer-zt4qxn8n`
and `<linux-cache>/mwcc-scalar-origin-audit/producer/rust-producer-fshdnbn5`.
This does not assert that their linked PE files have identical hashes.
Independent current native constructor tests pass on both hosts against these
sealed producers. They reproduce the retained counter islands, preserve all
unpatched bytes/symbols/sections, and reject 63 deliberate corruptions per host.
These include call retargets, changed raw addends and changed target-symbol
values in each of the three scalar helpers on every version. Evidence is at
`build/source-guided/scalar-origin-audit/constructor-verification.json`;
`scalar-helper-cross-host.json` records the matching helper relocation identities.

Both hosts' raw evidence covers nine cases across the three versions, with 18
fresh probe off/on captures per host. Both ghost variants bind the original `flag[1]` to the
live scalarized GPR35; the late-promotion control has no such frontend binding.
Objects and preexisting trace records agree off/on. Typed results are retained
at `build/source-guided/scalar-origin-audit/windows-typed/verification.json`.
Native Linux raw/typed results are retained under
`<linux-cache>/mwcc-scalar-origin-audit/linux-fixtures/` and
`linux-typed/`. All nine source/object hashes and derived SDK roles agree across
hosts. The 15 small function contexts on each host complete every same-function
scalar pass before the first backend birth. The cross-host checks are at
`build/source-guided/scalar-origin-audit/cross-host-tiny-verification.json`.
The separate broad backend suite passes 45 off/on pairs per host, including all
eight previously guarded backend birth-origin paths. Windows evidence is at
`build/source-guided/scalar-origin-audit/backend-windows-full/verification.json`.
Linux evidence is at
`<linux-cache>/mwcc-scalar-origin-audit/backend-full/verification.json`.
All 45 source/object hashes and retained typed backend summaries agree across
hosts, as recorded in
`build/source-guided/scalar-origin-audit/cross-host-backend-verification.json`.
These concurrent correctness runs do not measure controlled overhead.

A fresh full `RenderParticles` capture connects the original `noRota[1]`, byte
interval 4..7 and bit interval 32..63, through scalarizer pass 107/binding 1 to
backend birth 54:209, GPR162. Its stock object also matches the retained V4
capture. All 132 full-unit contexts containing both scalar passes and backend
births finish every scalar pass before the first birth. The retained proof is
`build/source-guided/scalar-origin-audit/windows-render/verification.json`, with
the full-unit ordering audit alongside it. The earlier frontend-before-function-
begin assumption was disproven by these captures; summaries are emitted at
function end.

These joins establish the source element's live value, not a complete
initialization or elimination argument. With the retained analysis binary,
the LI-only scalarized initialization predicate remains indeterminate on these
pointer-to-flag lowerings. The original late-promoted ghost values still have
no observed incoming copy, so their coalescing predicate also remains
indeterminate. Functions with no observed scalar pass retain unknown frontend
coverage.

## Comparison foundation

Native `colouring remap --json` now exposes format 2: complete captured graph node
domains, graph-only and instruction-only values, stored and observed adjacency
degrees, all instruction votes, competing/ambiguous targets and unmapped boundary
edges. Round/snapshot choices are explicit. Many-to-one instruction votes are not
reported as observed merges. Elimination remains `indeterminate` until backend
lifecycle coverage and value identity provide evidence; no backend hook coverage
is claimed by this comparison change.
