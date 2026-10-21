# Memory order: `oracle.trace`'s `views.memory_order`

Part of the [oracle documentation](../oracle.md).

```sh
mwcc-tools call oracle.trace project=PROJECT unit=UNIT function=NAME views.memory_order=true views.text=true
```

The list scheduler orders two memory accesses only when the alias analysis says they may touch the same memory:
then the later one waits the earlier one's latency (a load's 2 cycles on the 750). When it says they cannot, the
scheduler is free to move a store above a load, or into the cycles a load leaves empty. This changes which values
are live together before the allocation, so the registers too. No colouring search can reach that difference: the
graph itself changes ([what-if colouring](colouring.md)).

The view lists, for each scheduled block of the function, each pair of accesses the scheduler asked about and was
told cannot alias, that nothing else orders, and whose order the answer changed:

* the two accesses and the cycles they issued at;
* each one's alias: a part of an object, any memory a pointer can reach (the worst case), or the memory of a
  read-only or restrict pointer;
* the block scheduled again with the edge a may-alias answer adds, against the recorded order;
* for a read-only or restrict pointer's memory, the source that makes it so, and what undoes it.

It needs gc2's alias probes (`streams.alias_queries`, which it turns on with the scheduler's stream): GC 2.6 and
2.7 in process, from a module built with them. When `MWCC_ORACLE_ROOT` names another checkout, its module runs: one
built before the probes gives no `al.*` records, and the view finds nothing (`0 unordered`). `backend.memory_order` has the same as JSON: `blocks` (each with its instructions, the recorded
order and its `pairs`), `unordered` (all the pairs left unordered), `scheduled` and `replayed`.

## Read-only memory

At each pointer argument (`this` included) the backend gives the memory it points to an alias of its own
(`make_distinct_virtual_alias`, gc2's Alias.c), when the pointer is `restrict` or points to read-only memory:
`const` memory, of a class with no `mutable` member in itself, its bases or its members' classes
(`CParser_IsReadOnly`). The alias analysis then takes it that no store through another pointer reaches that memory.
A const method's loads through `this` are such accesses. The view shows the alias as `@N+0x58 (4 bytes), the
pointee of a read-only or restrict pointer` (the object's offsets count from 0x7FFFFF).

What gives the worst case instead, so that the two accesses may alias:

* a `mutable` member (in the class, a base, or a member's class);
* a non-const method, or a pointer to non-const memory;
* no `restrict`.

A `mutable` member changes every const method of the class, and of each class that holds it or derives from it. Check
the change over the whole project, not only the function it was for.

## Scheduling a block again

The what-if schedules the block again from the DAG the compiler recorded, with the edge added, by the pick rule of
[`sched`](../../crates/mwcc-trace/src/sched.rs) and gc2's 750 machine model (MachineSimulation750.c, ported in
`crates/mwcc-trace/src/machine.rs`; `tools/m750_table.py` writes its timing table). The view first schedules each
block again without the edge, and gives the what-if only when that gives the recorded order. The summary line counts
these blocks (`scheduled again, N of M blocks give the recorded order`).

## Example: Echoes `CKnockBackMgr::GetConditionValue` (GC 2.7)

Each case `return CRuleValue(member == constant);` (the constructor `mType(0), mBool(value)`, inlined) differs from
the target in the same way:

```
target: lwz r3, 0x58(r4); li r0, 0; cntlzw r3, r3; stw r0, 0(r31); extrwi r0, r3, 8, 19; stb r0, 4(r31)
ours:   lwz r3, 0x58(r4); li r0, 0; stw r0, 0(r31); cntlzw r0, r3; extrwi r0, r0, 8, 19; stb r0, 4(r31)
```

`trace.verify` replays our colouring exactly, `trace.target` wants r3 for the 25 compare results, and `trace.search`
finds no fix by colouring alone. The IR order is already the target's (the compare, then the store of `mType`). The
view gives, for each of the 27 cases:

```
-- block 66 (before the allocation): 1 pair(s) of accesses left unordered changed the order
   #0   lwz      r131=, r33, #88
   #1   cntlzw   r132=, r131
   ...
   #4   stw      r133, r32, #0
   ...
   #0 [lwz      r131=, r33, #88] and #4 [stw      r133, r32, #0] cannot alias: #4 issued at cycle 1, not at 2 or later
      #0: @1142+0x58 (4 bytes), the pointee of a read-only or restrict pointer
      #4: any memory a pointer can reach (the worst case)
      had they may-aliased: #0 #3 #1 #4 #5 #6 #2 #7  (recorded: #0 #3 #4 #1 #5 #6 #2 #7)
```

The method is const and `CKnockBackMgr` has no `mutable` member, so the scheduler moves the store of `mType` into the
load's empty cycle, before the compare. Had they may-aliased, the order would be `lwz, li, cntlzw, stw`: the zero is
live across the compare, which then takes r3, as in the target. A `mutable` member of `CKnockBackMgr` gives that
order: the function goes from 86.6% to 100%, every function of the unit scores 100%, and the view lists nothing. All 299 scheduled blocks of the
unit schedule again as recorded.
