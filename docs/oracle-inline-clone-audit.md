# Frontend write emission and inline clone audit

This is a read-only retail audit after `8f90c3f`, not an implemented source-origin
contract. It refines the [source-origin bridge proposal](oracle-source-origin-audit.md).
No producer or game input was changed. Allocation lifetime coverage is a separate
prerequisite; an immutable node observation is also needed whenever a tracked
allocation is read again after a possible mutation.

The retained audit is `build/source-guided/source-origin-bridge/inline-audit/`.
`capture.py`, `capture_extra.py` and `capture_statement.py` read pinned original
PEs and run the frozen native disassembler. Their normalization scripts compare
the supplied original split COFF bodies with the PE and retain complete
relocation identities. `retail-guards.json`, `extra-guards.json` and
`statement-guards.json` contain original-image and split-object hashes, body
hashes, address/size metadata, relocation offsets/kinds, target symbol metadata
and raw addends. `clone-callers.json` and `initializer-callers.json` enumerate
direct split relocations to the investigated helpers. `jump-tables.json` includes
actual table bytes, all entry relocations and normalized target offsets.

The read-only input is the sealed V6 producer
`build/source-guided/call-argument-audit/windows-producer4/rust-producer-hdo17756`.
Only its original PE and uninstrumented `build/VERSION/obj` splits were inspected.
The listings are actual retail instructions, not compiled reconstructed C.

## Verified entry points and ownership

All coordinates and sizes in this document are hexadecimal canonical retail
addresses. The ABI column counts original 32-bit stack words; a byte-valued
parameter must still be forwarded as the complete incoming word.

| Entry | GC 1.3 | GC 1.3.2 | GC 2.7 | Size | ABI |
| --- | --- | --- | --- | --- | --- |
| Recursive clone/substitute, `fn_00513240` | `55f360` | `55fec0` | `561dd0` | `692` | One input ENode; output EAX |
| Mode-setting clone wrapper, `fn_00513040` | `55f150` | `55fcb0` | `561bc0` | `90` | Input ENode, raw mode word; output EAX |
| `copy_enode_list` | `55fa00` | `560560` | `562470` | `61` | Input list; output EAX |
| `CInit_InitializeAutoData` | `5099e0` | `50a4c0` | `50b240` | `340` | Object, emit callback, destructor callback |
| `append_or_defer_expression_statement` | `4f33e0` | `4f3e90` | `4f4d50` | `bf` | Exact input ENode |
| `parse_statement` | `4ef910` | `4f03c0` | `4f1280` | `23d0` | StatementContext pointer |
| `CFunc_ParseFuncDef` | `4edf60` | `4eea10` | `4ef8d0` | `ab2` | Six raw words; first is explicit function Object |
| `CScope_SetFunctionScope` | `4da020` | `4da800` | `4db3f0` | `80` | Function Object, save record |
| `CScope_SetMethodScope` | `4d9fd0` | `4da7b0` | `4db3a0` | `4e` | Function Object, class, raw flag word, save record |
| `CScope_RestoreScope` | `4d9fa0` | `4da780` | `4db370` | `24` | Save record |

The mode-wrapper and list addresses above are independently present in the
retained metadata/listings. Function ownership must be captured before backend
`pv.function.begin`: ParseFuncDef provides an explicit function, scope setters
store it into `cscope_currentfunc`, and RestoreScope restores that field from
save-record offset 8. A new frontend owner ID must not inherit whichever backend
function ID happens to be active.

Inline body registration has an additional explicit owner:

| Operation | GC 1.3 | GC 1.3.2 | GC 2.7 |
| --- | --- | --- | --- |
| Save inline info, three words `(out, statement sentinel, function)` | `55af10`, size `436` | `55baa0`, interior `fn_0055b600+4a0` | `55d9e0`, interior `fn_0055d540+4a0` |
| Reconstruct function, three words `(function, info, output sentinel)` | `55aa70`, size `496` | `55b600` | `55d540` |

The later combined containing bodies are size `8d6`. Guard the entire containing
body and exact interior entry/call relocation before observing an interior
function; do not invent a stand-alone split symbol. The actual CInlineInfo size
is `a2` in GC 1.3 and `a6` in GC 2.7. Its common prefix is `nargs:0`,
`arginfo:2`, `nlocals:6`, `localinfo:8`, `nstmts:c`, `stmtinfo:e`; fields after
backend options shift in GC 2.7. Later fields need version-specific guards.

Preserve separate origin-body and destination-owner identities. During
`generate_inline_statements`, its explicit function is the callee, whereas the
containing function is the expansion destination. Deferred code generation can
occur after the original parsing scope ended. Existing explicit
`generate_inline_code` context and guarded `expanding_function` writes are better
destination evidence than an unchecked `cscope_currentfunc` value.

## Clone behavior and guard requirements

All three clone bodies have relocation-zeroed SHA-256
`591fd3f459af727bfe7b21537390fe27d6b744fe6e11f9a1542ffac5f4967b3b`.
They allocate `1e` bytes through galloc/lalloc, then copy seven dwords and one
word. The 30-byte allocation alone is not proof that an allocation is an ENode:
Statement allocations also request 30 bytes.

Useful guarded positions relative to the clone entry are:

| Offset | Actual behavior |
| --- | --- |
| `16`, `22` | Global/local 30-byte allocation calls |
| `30`..`3c` | Copy input EBP into allocated output EBX |
| `340`..`385` | Copy an actual argument expression with temporarily changed allocation/mode state; an adjusting conversion can replace the initially allocated shell |
| `3fa` | Alias-transform call on the original input; `+400` loops back to the copy at `+30` |
| `42a`, `471` | Registration writes argument/local slot tags `3d`/`3e` |
| `4cf`, `505`, `55e`, `58a` | Expansion writes object-reference tag `38` through the applicable argument/local route |
| `688` | Common output return path; other branches return earlier |

Capture function entry and every actual return through a wrapper, with parent
invocation ID, caller site, mode and allocation state. Do not instrument only the
common epilogue. Capture input before execution, input after execution, and
returned output separately. The input can mutate during alias normalization; the
returned node can be a recursively copied actual argument or a new conversion,
rather than the initially allocated shell. Record the actual transformation edge
and nested invocation; do not label every result a simple unchanged leaf copy.

The actual tag switch rejects values above `4b` (75). Its supported recursive
shapes include three children for conditional tag 53 and **four** children for
inserted tag 67. Tag 76 is outside this clone switch. Several template, memo,
label and member cases have mode-dependent payloads. Reconstructed signatures
and a generic expression walker are insufficient to justify reading those
payloads. Start with explicitly supported tagged fields and report other shapes
as gaps; never serialize all 30 bytes as initialized semantic data.

The code body references five separate switch tables. Code hashing plus the
relocation naming a table does not validate that table's contents. Pin these
table bodies and each entry's relocation target/addend:

| Clone switch | GC 1.3 | GC 1.3.2 | GC 2.7 | Bytes |
| --- | --- | --- | --- | --- |
| Main tags | `5cdaec` | `5cec74` | `5d0dc4` | `130` |
| Template subtypes | `5cdacc` | `5cec54` | `5d0da4` | `20` |
| Label modes | `5cdac0` | `5cec48` | `5d0d98` | `c` |
| Argument modes | `5cdab8` | `5cec40` | `5d0d90` | `8` |
| Local modes | `5cdab0` | `5cec38` | `5d0d88` | `8` |

The mode wrapper also has a `14`-byte table at
`5cd96c`/`5ceaf4`/`5d0c44`. InitializeAutoData has a `24`-byte type table at
`5baa44`/`5bbbd4`/`5bdd44`. All audited entries resolve inside their intended
guarded helper; their relative target arrays are identical across the three
versions. Complete bytes and entry identities are in `jump-tables.json`.

## Source emission and declaration initialization

The ordinary expression-statement path in parse_statement has an especially
small observation boundary. It stores the parsed root at `+238e`
(`mov [ebx+a],eax`), then calls the unused-expression checker at `+2395`, returning
at `+239a`. The Statement is the original EBX, and the stored ENode is at its
offset `a`. A guarded post-store island can snapshot these exact values before
later transformations. Preserve the original instructions and complete machine
state; do not infer EBX through a guessed C callback prototype.

All three parse_statement bodies normalize to
`588f46ebe4b75632324411886823556359c60f3248a3a5d03b33cfb7c0c4b2cd`.
This first scope covers ordinary expression statements, including the flag
assignment inside the retained one-iteration loop and the inline helper body.
It does not cover writes inside for-initializers/steps, conditions, return
expressions or generated cleanup statements. Those need their own audited
emission points. A generic statement-allocation observer is too early because
the expression field has not yet been assigned.

InitializeAutoData normalizes to
`26c8ac7ddb01b356e6c50cebce24b4295395b10c171e27fb41cb107180563b3c`;
append/defer normalizes to
`c224ba905384ed5c0d101b24ccbc8c3df517b07f7b4242bab0a4567b86ad744f`.
The only direct call to InitializeAutoData in the audited split relocation
inventory is `declare_local_object+4cb`, passing the target Object and the exact
append/defer callback. Guard the caller body and all three call arguments.

The smallest safe implementation forwards those three arguments unchanged and
observes the known append/defer function itself. An active initializer frame
records its target Object, callback identity, frontend owner, token kind at
entry, and explicit/default syntax classification. Attribute a callback input
only when callback and owner match. Other callback routes remain unsupported;
there is no need to replace function-pointer values with an escaping thunk.

The callback has two different outputs. With deferral enabled it may append the
**previous** deferred root (`+66`), then store the current input into the deferred
slot (`+69`). Without deferral it appends the current input (`+ba`). Capture the
exact input snapshot, prior/new deferred snapshots and prior/new statement tail
separately. A currently active initializer does not own every statement appended
during its callback. Match eventual emission through the recorded deferred slot
and allocation identity, preserving revision ambiguity if the node changed.

Initializers can emit several expressions or no expression. Missing explicit
syntax and implicit class construction are separate states. A later assignment
to a declared array remains a source assignment, even if it performs the
programmer's initial setup. An assignment-shaped ENode alone never establishes
the DeclarationInitializer role.

## Proposed bounded observation contract

The following are design names, not implemented tags or a promised API:

* A compilation stream declares capability and independent totals, with balanced
  frontend owner, initializer, body-registration and clone invocation scopes.
  End totals must detect a wholly missing record family. Scope overflow, unknown
  callback, unsupported tag/list, missing owner and observed lifetime gaps remain
  explicit.
* Each node observation has a fresh immutable snapshot ID, observed stage,
  allocation identity plus offset, tag-defined scalar fields and child edges to
  other snapshots. Allocation generation resolves address reuse; snapshot IDs
  distinguish different observations of the same live allocation. Unknown
  allocation is not resolved from pointer spelling or equal bytes.
* Source emission records contain frontend owner ID, actual Statement allocation,
  exact root snapshot and source offset/line as display data. Supported assignment
  nodes get explicit LHS/RHS edges. Exact Object/constant-byte targets require a
  separately guarded LHS recognizer. Token file/offset identity needs a checked
  association with the retained source closure before becoming source location
  proof.
* Initializer scopes bind the explicit target Object to their exact callback
  input snapshot; callback emission/deferred-slot records retain distinct roles.
* Clone begin/end records include invocation and parent IDs, observed caller
  site, source-body/destination owner contexts, input-before, input-after and
  output snapshot IDs, and mode/allocation-state before and after. Formal/local
  slot substitution includes its actual table/list and bounded slot index,
  associated body registration, and exact Object/actual-expression observation.
  Names are display metadata, not formal/actual correspondence.
* Backend function Object joins require the same tracked allocation identity and
  a checked compilation timeline. Lifetime tracking does not prove that every
  in-place mutation has been observed. A gap between source snapshot and later
  clone input remains a mutation gap even when the allocation has not changed.

SaveInfo can run after frontend optimization. Its clone edges therefore do not
by themselves connect an earlier source write through unobserved lowering,
linearization, scalar replacement or reconstruction. Preserve these missing
edges rather than assuming matching pointers, source lines, names or IRO indices
complete the bridge. None of these observations proves dominance, execution,
first-write status on every path, or semantic lifetime equivalence.

Before accepting a source-origin query, validate declaration initialization and
later assignment separately; deferred callback flushing; two inline invocations
with different actual arrays; both expansion routes; nested substitution;
alias-retry input mutation; discarded clone shells; local arena reuse; surviving
global registered bodies; unsupported tags and table retargeting; missing scope
records; and stock object/relocation equality on all three compiler versions.
