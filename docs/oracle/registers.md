# Registers and scheduling: `oracle.trace`

Part of the [oracle documentation](../oracle.md).

Inspect register allocation and scheduling for one function:

```sh
mwcc-tools call oracle.trace project=PROJECT unit=UNIT function=NAME views.pcode=true views.schedule=true \
    views.text=true
```

## The register report

For each register class (GPR, FPR, ...) the report gives:

* K, the class's colour mask, and any earlier colouring rounds that failed and spilled.
* The **traced** colouring order: the stack the simplification leaves, first coloured first.
* For each virtual register:
  * its physical register, its def and use counts, and the blocks it lives in;
  * the instruction (block and source line) that first defines it;
  * the calls it lives across;
  * why the simplification took it: a degree below K, or the spill candidate with the lowest cost / degree;
  * the colours its neighbours held when it was coloured, and the colours left free in the mask. The lowest free
    colour is taken. When none is free, a non-volatile register is claimed, from r31 down.

**Replays.** Select is replayed from the traced graph and mask (`select replayed: N of M agree`), and a register it
disagrees on is flagged. The simplification is replayed too, and a note says when its order differs from the trace.

`oracle.regdiff` (`project`, `unit`, `function`) explains only the registers that differ from the target's. To find
what would give the target's registers (a local declared elsewhere, a temporary made a named local, a value no longer
in a register of its own), see [what-if colouring](colouring.md).

**r0.** When r0 is a neighbour, the report points it out: r0 cannot be a base register, because an address operand of
r0 reads as 0.

## `views.pcode`

Prints the PCode the allocator was given, block by block. Virtual registers are numbered as in the register report:
`%r60` is virtual register 60.

## `views.exchange: "A,B"`

Example: `views.exchange=r41,r42`. Explains why two virtual registers of one class were coloured in that order and
got the registers they did. For each, it shows:

* when the simplification pushed it, and the neighbour whose removal allowed that;
* degrees, spill costs and neighbours;
* the colours select found free.

## `views.schedule`

The instruction scheduler, per block, both before the allocation (virtual registers) and after it. For each cycle it
shows the pick, and for every other candidate why it lost:

* not ready yet, with the instruction whose latency it waits for;
* its unit is busy;
* or the first tie-break it lost, in order: urgency (its latest cycle is past), fewer scheduled successors, lower
  height, lower opcode rank (before the allocation only). If all are equal, the earlier instruction wins.

The picks are replayed from the traced candidates (`select replayed: N of M picks agree`).
