# Basic blocks: `oracle.trace`'s `views.blocks`

Part of the [oracle documentation](../oracle.md).

```sh
mwcc-tools call oracle.trace project=../echoes unit=main/MetroidPrime/ScriptLoader function=__sinit \
    views.blocks=true views.text=true
```

`views.blocks` shows how code generation cut a function into basic blocks, and why the merge pass left them apart.
Use it when the target's blocks are cut in other places than ours: the scheduler and the allocator work block by
block, so the cuts shape the code.

## How the compiler cuts and merges

1. **Cuts.** Code generation starts a new block after a call or a branch, and at a label. It also starts one at the
   start of a statement once the current block holds more than 100 instructions. That count is of the raw PCode,
   before any optimization.
2. **Optimization.** The global optimizer (COptimizer_Optimize) then removes instructions in each block. Blocks usually
   shrink, often by a third or more.
3. **Merges.** Peephole_MergeAdjacentBlocks merges a block with the next one when:
   * the first has no call or side effect, and does not end with a conditional branch;
   * each is the other's only successor or predecessor;
   * the second is not the epilogue, nor flagged (an exception handler or the like);
   * together they hold 100 instructions or fewer.

   The pass runs twice: before the allocation, then again with the prologue.

So a block cut at the 100-instruction limit stays cut once two optimized blocks together are over 100, even though
each is far below the limit.

## The report

```
== __sinit_ScriptLoader_cpp: blocks ==
   blocks: raw 25, optimized 25, merged 25, merged2 24; instructions: raw 2392, optimized 1402, merged 1402, merged2 1407
   552 statements, 22 cuts: 22 the block held more than 100 instructions
   merge pass 1: 22 together more than 100 instructions; 1 the prologue block (not merged in this pass); ...
   per block: instructions (raw -> optimized -> merged -> merged2), the last instruction, why it was cut, why ...
      block 1: 104 -> 63 -> 63 -> gone; ends stw; cut: the block held more than 100 instructions; next: together ...
```

* **blocks, instructions:** the count at each stage: `raw` before the optimizer, `optimized` after it, `merged` after
  each merge pass.
* **statements, cuts:** each expression statement generated, and each place the code moved to a new block, by cause.
* **merge pass:** for each pair of neighbouring blocks, why the pass kept them apart, or `merged`.
* **per block:** its size at each stage (`gone` when merged into another), its last raw instruction, why it was cut,
  and why it was not merged with the next. Neighbouring blocks with the same row are grouped. A block shows its source
  lines when an expression statement generated code into it; conditions of `if` and loops are not counted as
  statements.

With `views.pcode`, the report also lists each raw block's instructions, with virtual registers.

## Other limits: `cut_limit`, `merge_limit`

The limits take a source compile (`source`, `flags`, `directory` and `version`), not a unit; `unit.load` gives a
unit's:

```python
from mwcc_tools import Engine, Unit
with Engine(project='../echoes') as engine:
    unit = Unit(engine, 'main/MetroidPrime/ScriptLoader').info
    result = engine.call_one('oracle.trace', source=unit['source'], flags=unit['flags'],
                             directory=unit['directory'], version=unit['version'], function='__sinit',
                             views={'blocks': True, 'text': True}, cut_limit='77', object_path='sinit-77.o')
```

These compile with other block limits, to test whether the target's blocks were cut elsewhere: give the size a
guessed source would reach, then compare the object with the target.

* `cut_limit: "N"`: a new block at the start of a statement once the block holds more than N instructions.
* `merge_limit: "N"`: blocks are merged when together they hold N or fewer.
* N is 0 to 127 (the compare takes a signed byte), or `none`: never cut, or merge whatever the size.
* `object_path: PATH`: also write the oracle's object to PATH, to disassemble or diff. It works without the limits
  too, and on a unit.

The runner patches temporary copies of the oracle and the stock compiler (`build/` is unchanged), so the stock
check still compares the two; the report's first line gives the limits. `runner::patch_limits` in `mwcc-oracle-core`
checks every patch site (all seven inlined cut checks) before changing any, for GC 1.3, 1.3.2 and 2.7.

## Example

In the ScriptLoader example, each of the 184 table entries is three statements of 13 raw instructions in all. Eight
entries make 104 instructions, so each block holds eight entries. The optimizer shrinks the blocks to about 61
instructions, and no two fit in 100 together, so the 23 blocks stay apart.
