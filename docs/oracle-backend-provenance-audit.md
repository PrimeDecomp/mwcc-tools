# Backend provenance: what the probes observe

What the retail machine code establishes about the x86 oracle's guarded backend probes (`pv.coalesce.*`,
`pv.value.*`, `pv.counter.*`, `pv.graph.*`, `pv.scalar.*`), and why some results stay unknown or indeterminate. The
records are read with `provenance.inspect` and `provenance.compare` ([api.md](api.md),
[provenance](oracle/provenance.md)). Each probe is guarded (normalized body hashes, relocation identities, short instruction
guards), and the native link checks its guard before it installs it. The guards are in `crates/mwcc-oracle-core/src`
(`backend_probe.rs`, `counter_probe.rs`, `call_argument_probe.rs`, the `*_guards.json` catalogs);
`mwcc-tools provenance derive-guards` recomputes the arena, source-node, call-argument and scalarizer catalogs.

General rules:

* The reconstructed C is a lead, not a hook specification: its prototypes, predicates, names and version branches
  are not authoritative. The probes read the retail registers and frame at audited instructions.
* Birth IDs, raw pointers and register numbers are capture-local. Nothing joins two captures by them.
* Missing records, guard failures and missing scope declarations never establish absence: coverage stays unknown.

## Coalescing

`SpillCode_CoalesceCopies` (`0x44A` bytes in GC 1.3, 1.3.2 and 2.7) reads the active class and register count from
globals and inlines its range, root-finding and interference predicates; its first filter uses instruction flags.
Observing its entry and exit would miss the deciding branches, so the probes sit at them: copy-flag filtering
(`0x10`), rejection by another flag (`0x100`), operand and class checks, root equality, the interference-bit test, the
four window bounds, a GPR/special-register equality guard (its role is not confirmed, so its value is recorded, not
named), the union-parent write (the merge is observed after it) and the instruction unlinking. Bounds come from
class-indexed tables.

## Register counters and births

* `provenance audit-counters` finds every direct counter reference in the pinned images: 1,188/1,191/1,200 in GC
  1.3/1.3.2/2.7, of them 570/571/578 writes. Four writes per version are not increments: the initial reset, the
  scratch rewind, the high-water restoration and the allocation reset. Computed aliases and bulk writes are outside
  this scope.
* Names mislead: `Operands_AllocateGPR` sets the current instruction's flags and creates no virtual register;
  `fn_004cf6d0` computes a physical-register range; the variable-assignment increment is in `fn_004fcba0`.
* The host keeps separate generations when a register number recurs, within a counter-reset epoch; it never takes
  the latest birth for a reused number.
* The graph hook captures the graph just before simplification (`pv.graph.*`: visibility, graph-only nodes, stored
  degrees, ordered adjacency). It reads the live physical limits, because target initialization can come after the
  generator's entry. Function-end totals detect a missing snapshot.

## Object bindings and range categories

* `fn_004fcba0` (the same normalized body in every version): at its increment (`+0x28`), EBX is the VarInfo and the
  object argument is at retail ESP+0x1C. `Registers_BindVR` is really the paired GPR assignment path (EBX the object,
  EDI the VarInfo, at `+0x9A` and `+0xA6`). Pair parts are in creation order.
* The range categories (initial object range, before, inside and after the coalescing window) come from the
  completed stores at `Registers_SnapshotInitialObjectRange+0x21` and `Registers_BeginCoalesceWindow+0x42`, not from
  guessed source roles. The `-O0` path has no range markers, so its categories are unknown.
* The two Normalize symbol-address births bind the Operand and Object of each high/low half, reading the cached data
  link name without calling the mangler. The cache is at offset 56 in GC 1.3/1.3.2 and 54 in GC 2.7 (the retail
  accessor's bytes; the reconstructed header has the older layout).

## Array promotion and call results

* `COpt_ArrayToRegister` (`0x5c8`, the same normalized body in every version) increments FPR at `+0x332` and GPR at
  `+0x345`; ESI is the promotion record, ECX the element index, EAX the new register. `pv.value.array` binds the birth
  to the array Object and element (one to eight elements). The vector-array pass's VR increment
  (`fn_0052cf10+0x14b`) is only an ordinary birth.
* `FunctionCalls_GenerateCall` (`0x6a6`) has six result increments, including a second FPR branch the reconstructed C
  omits. `pv.value.call_result` records the call node, callee, return type and output Operand. The result register
  is not stored yet at the birth, so `output_complete=0` and `tree_complete=0`; the raw output bytes can hold stale
  data and are not fingerprint inputs. The helper's frame covers the dispatcher's route and the two intrinsic
  fallbacks.
* Function headers declare `guarded_scalar_v1` and `guarded_call_result_v1`; function ends count the observations.

## Frontend aggregate scalarization

Backend array promotion is separate from the frontend rewrite of an aggregate part into a new scalar Object
(`IRO_ScalarizeClassDataMembers`). Values such as `@17`, `@21` or `@5986` in the Echoes cases are made at the
scalarizer's `create_temp_object` return (`0x4608a8`, `0x460b28`, `0x461748` in GC 1.3, 1.3.2, 2.7); they are not
CSE objects. The probe keeps the original EBX at the naked `create_temp_object` entry, forwards the caller's address
and recognizes only that return.

* At the return, EBX is the `IROVarPart` and EAX the new Object. The part's fields: `+0` source linear, `+4` flags,
  `+6` VarRecord (its `+2` is the source Object), `+0xa` byte offset, `+0xe` byte size, `+0x12`/`+0x16` first/last
  bit, `+0x1a` Type, `+0x22` use list.
* `pv.scalar.begin/binding/end` give a capture-local pass ID, the function Object, the source and temporary Objects
  and VarInfos, types, names and byte/bit intervals. Function ends carry a summary of the passes and binding totals;
  a missing, overflowed or inconsistent summary is unknown, never zero passes.
* A backend join needs the same function Object, capture interval, temporary Object and VarInfo; a whole-element
  role also needs the element type and size, an in-bounds byte interval, a full-width bit interval and no bitfield
  flag. Ambiguous or reused identities stay gaps.
* In every capture so far, a function's scalar passes end before its first backend birth (so the summaries are given
  at the function's end, not its beginning).
* A join gives the source element's live value. It does not prove initialization or elimination: an unobserved
  incoming copy leaves coalescing indeterminate, and a function with no observed pass has unknown frontend coverage.

## Graph comparison

`trace.remap` gives the captured node domains, graph-only and instruction-only values,
stored and observed degrees, every instruction vote and the ambiguous targets. Many-to-one votes are not reported as
merges, and elimination stays indeterminate.
