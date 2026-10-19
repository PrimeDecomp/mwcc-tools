# Retail IRO and scalar replacement provenance audit

This read-only audit identifies guarded observation opportunities in GC 1.3, 1.3.2 and 2.7. It establishes actual x86 entrypoints, ABIs, layouts and scalar replacement stores. It does **not** implement hooks or establish complete source ancestry. See the [source-origin audit](oracle-source-origin-audit.md), [inline clone audit](oracle-inline-clone-audit.md) and [arena audit](oracle/source-arenas.md) for the adjoining gaps.

## Reproducible evidence

The scripts read retail PE files under `<frozen-oracle>` and untouched split COFF objects from `build/source-guided/call-argument-audit/windows-producer4/rust-producer-hdo17756`. They write only their scratch directory:

1. [audit_retail.py](../build/source-guided/source-origin-bridge/iro-audit/audit_retail.py) verifies configured compiler SHA-1, extracts retail bytes/disassembly and checks 66 retained split bodies after masking only their actual COFF relocation words.
2. [local_entries.py](../build/source-guided/source-origin-bridge/iro-audit/local_entries.py) identifies private functions inside coarse split symbols, verifies complete disassembly and actual RET boundaries, and retains exact body/relocation guards.
3. [finalize.py](../build/source-guided/source-origin-bridge/iro-audit/finalize.py) generates [scalar-sites.json](../build/source-guided/source-origin-bridge/iro-audit/scalar-sites.json), checking 15 scalar sites per version against actual instruction boundaries and expected instruction prefixes.

Run those scripts in order with Python and Capstone. The ignored scratch artifacts are local evidence, not guaranteed present in a fresh checkout. [retail-functions.json](../build/source-guided/source-origin-bridge/iro-audit/retail-functions.json) retains full bytes, calls and split relocation identities/addends; [local-entry-guards.json](../build/source-guided/source-origin-bridge/iro-audit/local-entry-guards.json) retains private entry guards. The compact manifest contains all per-version raw and normalized body hashes, owner offsets, split object hashes, relocation identity hashes and site windows. Its recorded SHA-256 is `6b93b4b7d64fd5180b60d785f0cd72875e1cdcbc1d780dbac47bc2d651d0d7b6`.

| Retail compiler | PE SHA-256 |
| --- | --- |
| GC 1.3 | `4e502c38465500d4fda8d966b268151a6c74c730508e3d9b7efd23d1a6083715` |
| GC 1.3.2 | `7cbae0a5bd81e07d7fa8975bbc4e969b5dea265cc29c5ee6bae0453a6e25f225` |
| GC 2.7 | `04ece8178961bdbaeebe2d4e5922ed542c4d82b2fc3de996c41c9e193bd49eea` |

These checks cover the source/IRO/scalar helpers below. The audit's missing `_freeoheap` symbol spelling is not an allocator coverage claim; heap reset and lifetime coverage belong to the separate arena audit.

## Actual entries and ABI

Addresses are retail virtual addresses. Arguments occupy ordinary 32-bit x86 stack slots, the caller removes them, and pointer results return in EAX. `Boolean` describes the source parameter, not a one-byte stack slot. All listed functions have ordinary RETs. A future detour must separately validate displaced instructions, relocation repair, all return paths and preservation of machine state; these addresses alone are not hook constructors.

| Actual role | GC 1.3 | GC 1.3.2 | GC 2.7 | Size | Arguments → result |
| --- | --- | --- | --- | --- | --- |
| IRO → ENode reconstruction | `0046BE00` | `0046C0A0` | `0046CCC0` | `0x2B7` | `(IROLinear*) → ENode*` |
| Linear list → statements | `0046B590` | `0046B830` | `0046C450` | `0x86A` | `(IRONode* start, IROLinear* head) → Statement*` |
| Statements → linear list | `0046C0F0` | `0046C390` | `0046CFB0` | `0x741` | `(Statement*) → void` |
| Visit/lower statement expressions | `0046C840` | `0046CAE0` | `0046D700` | `0x14E` | `(Statement*) → void` |
| ENode → IRO linearization | `0046C990` | `0046CC30` | `0046D850` | `0x55E` | `(ENode*) → IROLinear*` |
| Recursive in-place lowering | `0046CEF0` | `0046D190` | `0046DDB0` | `0x52A` | `(ENode*, ENode* parent, Boolean force, Boolean flag2) → void` |
| Scalar pass | `00460780` | `00460A00` | `00461620` | `0x3A2` | `() → void` |
| Record scalar part use | `004601F0` | `00460470` | `00461090` | `0x3FC` | `(IROLinear* source, VarRecord*, int byte_offset, Type*) → int` |
| NewLinear | `0046ECE0` | `0046EFC0` | `0046FBE0` | `0x55` | `(unsigned char type) → IROLinear*` |
| NewENode | `0046C0C0` | `0046C360` | `0046CF80` | `0x22` | `(unsigned char type) → ENode*` |

The 1.3.2/2.7 split symbols are **coarse containers containing separate private functions**. They are not fused runtime entrypoints:

| Container | GC 1.3.2 range, end exclusive | GC 2.7 range, end exclusive | Actual private entries relative to container |
| --- | --- | --- | --- |
| `convert_group` | `[0046B830,0046C357)` | `[0046C450,0046CF77)` | Converter `+0`; reconstruction `+0x870` |
| `build_group` | `[0046C390,0046D961)` | `[0046CFB0,0046E581)` | Build `+0`; visit `+0x750`; linearize `+0x8A0`; lower `+0xE00` |

The second container also has another helper at `+0x1330`, outside the six audited private roles. GC 1.3 gives those roles independent split symbols. The 1.3.2/2.7 reference-marking container at `0046B540`/`0046C160` is not reconstruction. Observing only a container entry/exit would miss internal calls and mix ABIs. Some early scratch `.asm` filenames predate this correction; use `*-actual-*.asm` and manifest role keys.

The private bodies require per-version guards: GC 1.3 differs in diagnostic constants, and lowering has a genuine instruction-sequence difference. The 1.3.2/2.7 instruction shapes agree, but shape masking is only an audit aid. Do not substitute it for exact bytes plus relocation identities.

The scalar pass and use recorder have identical relocation-normalized bodies across all three versions:

| Body | Normalized SHA-256 |
| --- | --- |
| Scalar pass | `7ff5c374f005e7bf1c782113ee92c9c311518e26bf27c3f88a0bab8072d46b3a` |
| Record scalar part use | `d528fa438432ca999f55a60133384fc4b116313a9569f601ba5ef6779e2f65cd` |

## Complete scalar-use/replacement observation

Offsets below are relative to the scalar pass entry in the ABI table. All are verified instruction boundaries. Full bytes and call relocations are in the manifest.

| Site | Offset | Actual state or operation |
| --- | --- | --- |
| Scalar Object created | `+0x128` | Return from `create_temp_object`; EAX=new Object, EBX=IROVarPart |
| Load uses | `+0x13D` | `8B7322`: ESI=`[EBX+0x22]` |
| Iteration begins | `+0x148` | EBX=part, ESI=IROElmList, `[ESI]`=old member element |
| Direct leaf replacement | `+0x1C0` | `894520`: `[EBP+0x20]=EAX`; EBP=old IRO leaf, EAX=new Object-ref ENode |
| Direct replacement completed | `+0x1C3` | New ENode is stored; original payload must already have been retained |
| Nonleaf parent lookup | `+0x1D1` | `FindNextUse(member.element)`; result subsequently becomes EDI |
| Clear old parent subtree | `+0x206` | `ClearZeroOperands(parent->u.monadic)` |
| Replace parent child | `+0x214` | `894720`: `[EDI+0x20]=EAX`; EAX=new IRO leaf |
| Populate new leaf | `+0x224` | `894520`: `[EBP+0x20]=EAX`; EBP=new leaf, EAX=new Object-ref ENode |
| Clear old member subtree | `+0x24F` | `ClearZeroOperands(member.element)` |
| Insert replacement | `+0x25D` | `InsertLinearRangeAfter(new_leaf,new_leaf,old_member.element)` |
| Both branches complete | `+0x265` | Before `8B7604` advances ESI to the next list link |
| Parameter check | `+0x270` | Compares original aggregate Object with compiler `arguments` list |
| Parameter-prefix copy | `+0x2A0` | Constructs a synthetic entry assignment if the aggregate is a parameter |
| Next part | `+0x387` | `8B5B1E`: EBX=`[EBX+0x1E]` |

The existing scalar binding point at `+0x128` precedes all replacements for that part. It can retain the complete bounded `part.uses` chain before mutation, including each link allocation identity, old element identity and typed payload. Actual iteration begin/end and slot-store observations can then reconcile the expected use count, list order, part/generation and both replacement branches. Cycles, truncation, unknown generations, unexpected link changes or unfinished iterations must produce gaps. A post-rewrite snapshot cannot recover the old operands.

The recorder starts from `source->u.monadic`; the first matching access becomes `part.source`, and subsequent uses are prepended. This is access bookkeeping order, **not source definition or initialization order**. In the nonleaf branch, observe the actual `FindNextUse` result and stored child slot rather than deriving a parent from pointer equality. Parts with `flags & 2` also use bitfield bookkeeping; whole-array-element recognition still requires the independent byte/bit interval and target type checks.

The parameter-prefix assignment is additional compiler-generated work after the member loop. Count and label it separately. It is not a source declaration initializer or an ordinary member iteration.

## Allocation and layout

The following offsets and allocation sizes agree in the audited retail bodies for all three versions. Interpret tagged unions by the actual node type; pointers and mutable indices are not origin identities.

| Structure | Size / heap | Fields and offsets |
| --- | --- | --- |
| `IROLinear` | `0x38`, `oalloc` | type u8 `+0`; nodetype u8 `+1`; flags u32 `+2`; nodeflags u16 `+6`; index u16 `+8`; Statement* `+0xA`; Type* `+0xE`; IROExpr* `+0x12`; ERange* `+0x16`; copied word `+0x1A`; byte `+0x1E`; tagged payload `+0x20`; next* `+0x34` |
| `ENode` | `0x1E`, `lalloc` | type `+0`; cost `+1`; flags u16 `+2`; ignored `+4`; hascall `+5`; Type* `+6`; copied word `+0xA`; tagged payload `+0xE` |
| `IROVarPart` | `0x2A`, `oalloc` | source* `+0`; flags `+4`; VarRecord* `+6`; byte offset `+0xA`; byte size `+0xE`; bit interval start/end `+0x12/+0x16`; Type* `+0x1A`; next* `+0x1E`; uses* `+0x22`; bookkeeping word `+0x26` |
| `IROElmList` | `8`, `oalloc` | element* `+0`; next* `+4` |
| `VarRecord` | Selected fields only | index u16 `+0`; Object* `+2`; noregister byte `+0xB` |

The semantic role of IRO `+0x1A` / ENode `+0xA` is not established here beyond the observed copy. Part `+0x26` is assigned from local list bookkeeping; this audit does not establish a stable source-origin role for it. Names for other fields follow the decompilation headers and their corroborating accesses, not a claim that every field was dynamically exercised.

Constructor-only instrumentation is insufficient. Linearization directly calls `oalloc(0x38)` and initializes nodes itself. Reconstruction directly calls `lalloc(0x1E)`, including fresh leaf copies. NewLinear/NewENode observers therefore miss real nodes. Lowering mutates in place and returns void; EAX cannot be treated as a rewritten-root result.

`IRO_Optimizer` has actual entries `0042E1B0` / `0042DDD0` / `0042DE10`, sizes `0x619` / `0x620` / `0x620`, with `(Object* function, Statement* body) → Statement*`. Its retail split is `backend/CodeGen.c.obj`, despite the related decompiled implementation being in `optimizer/IrOptimizer.c`. It stores the function Object in the optimizer's current-function global before visit/build. Its body and relocation guards are retained in the manifest.

## Minimum bridge and remaining causal gaps

A guarded implementation can bind an optimizer invocation to its function Object allocation identity and incoming/outgoing body, then record recursive lowering, linearization and reconstruction frames with parent IDs, typed snapshots, allocation generations and payload revisions. Lowering needs actual rewritten nodes and inserted statement roots; linearization and reconstruction have real input/output pointer edges. The scalar use-list and exact stores above can connect a part to its observed replacement Object and ENodes. Nested function scopes must suspend independently; stale `gCurrentStatement` is context rather than origin.

That still does not make the whole path complete. IRO passes mutate nodes between initial linearization and scalarization. `ClearZeroOperands` explicitly sets subtree type to zero and `expr` to null. After IRO reconstruction, `DumpIR_OptimizeStatements` runs `COptimizer_OptimizeStatementList` and expression optimization before backend dispatch. Allocation generation identities do not prove unchanged payloads across those operations. An unobserved rewrite must terminate the causal path with a gap; names, lines, CSE indices, copied words or matching bytes cannot repair it.

An explicitly observed assignment with destination/value children can support a **stage-local SourceWrite**. It cannot establish declaration initialization, dominance or source lifetime. **DeclarationInitializer** requires the guarded frontend initialization action and exact emitted root described in the source-origin audit, followed by observed transformation edges. Inline clones and formal/actual substitutions likewise need their own edges. No new complete runtime path for `noRota` or `flag` is claimed by this static audit.
