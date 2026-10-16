# What-if colouring: `mwcc-tools colouring`

Part of the [oracle documentation](../oracle.md).

`mwcc-tools colouring` analyzes a function whose code matches the target except that some values sit in other
registers, most often a permutation of the non-volatile registers (r14..r31, f14..f31). It replays the compiler's
colouring from the oracle's trace and searches for small changes to the interference graph that give the target's
registers. The change is expressed as a renumbering of virtual registers, a removed node, or a degree or edge change.
You then map that change back to source:

* a named local declared elsewhere;
* a temporary made a named local (`static_cast<int>(...)`, a `const` local);
* a value no longer computed into a register of its own, such as `x ? GX_TRUE : GX_FALSE` for an argument `x != 0`.

The Rust replay follows the retail GC/2.7 colouring rules and also reproduces GC/1.3.2 traces:

* **Simplification:** `allocation::simplify` in `mwcc-oracle-core`. Nodes are scanned in index order, and a node is pushed
  when its degree is below K. When none is, the cheapest spill candidate is pushed.
* **Select:** in the reverse of the push order (a later pass is coloured first; within a pass, the higher index is
  coloured first). Each node takes the lowest colour of the mask that its neighbours do not hold.
* **Claims:** when no colour is free, select claims the highest non-volatile register not used yet and adds it to the
  mask. The claimed register is predicted rather than read back from the trace.
  * The registers already used when select starts are the physical nodes an instruction names (r31 holding the frame,
    say), as neighbours in the graph.
  * The traced claims' gaps are a consistency check on that. On every traced function so far, they add nothing.

```
mwcc-tools colouring COMMAND [OPTIONS] SOURCE -- [COMPILER FLAGS...]
mwcc-tools colouring search --from-ninja ../echoes/build.ninja src/Kyoto/Particles/CElementGen.cpp \
    --function RenderParticles__
```

Compile options are shared with `mwcc-tools oracle`: `--from-ninja`, `--version`, `--override P=R`, and the
compiler's flags after `--`.

* `--save PREFIX` keeps the trace, object and identity manifest (`PREFIX.trace`, `PREFIX.o`, `PREFIX.manifest.json`).
* `--load PREFIX` analyzes saved inputs without compiling; when a manifest is present, it checks trace/object hashes and compiler version.
* `--class FPR` works on the floating-point registers; the default is GPR.

## Commands

### `verify`

```
mwcc-tools colouring verify --from-ninja ../prime/build.ninja src/MetroidPrime/CStateManager.cpp
```

Replays every function (or `--function`'s) and checks that the simplification gives the traced order and select the
traced colours, including the registers it claimed. Prints one line per class and colouring, and exits 1 on any
disagreement. The other commands are only as good as this replay, so run it first on a new compiler version or an
unusual function.

Retained replay examples (all exact):

| Project | Compiler | Unit | Colourings |
|---|---|---|---|
| Echoes | GC/2.7 | CElementGen | 181, 102 of them with claims, one after a spill round |
| Prime | GC/1.3.2 | CPlayer | 274 |
| Prime | GC/1.3.2 | CStateManager | 308 |
| Prime | GC/1.3.2 | CElementGen | 134 |

### `target`

The target's register for each virtual register. It pairs instructions in two steps:

* our object with the PCode after the allocation, by the physical registers each instruction names;
* our object with the target's asm, by shape (the same instruction up to registers and relocations).

Each register field of ours is then traced back to the virtual register it holds, and the target's field at the same
place votes for that register.

* The asm is the one `--from-ninja`'s project builds (`build/<id>/asm/...`), or `--target-asm FILE`.
* `--want V=R` overrides or completes the derived targets (`--want 165=26`).

Checks:

* **CONFLICT:** two interfering nodes, or a node and a physical neighbour, given one register. The pairing went wrong
  somewhere, or a `--want` is mistaken.
* **WARNING:** a class coloured again after a spill. The last round's node numbers need not be the PCode's, so these
  targets cannot be trusted.
* Nodes holding non-volatile registers with no target (no field paired) are listed. Search still renumbers them.

On a matched function, every derived target equals our register.

### `force`

Colours the graph as it is, with the nodes that claim each non-volatile target register reordered by that register,
highest first.

* **EXACT** means the graph can give the target's colours and only the colouring order is wrong. Search the
  numbering (and the degrees, which decide the passes).
* Otherwise no order of the claims gives the target. The graph itself must change: an edge, or a node.

### `search`

1. **Single changes.** Each change is tried alone, and the ones that leave fewer registers wrong are listed. A single
   exact change, such as a temporary numbered elsewhere or a coalesced argument node removed, often shows up here
   directly. The changes are:
   * each node to renumber, numbered just after each other one (or before the lowest), the nodes between shifting by
     one as they do when a declaration moves;
   * the graph edits of `--kinds` (default `drop,degree,unlink`; `none` for a renumbering only): removing a coalesced
     node next to a wrong one, a degree one higher or lower for a node in the claims, removing (or, with `link`,
     adding) an edge between a wrong node and such a node.

   Among changes equally good, the shorter moves come first.
2. **Annealing.** A simulated annealing searches a renumbering of the nodes to renumber (the claimers, the wrong nodes
   and the untargeted non-volatile nodes), together with a set of graph edits and of the single moves that helped. It
   starts from the best single change, `--restarts` times (default 10), sharing `--iterations` between the runs.
   * It first minimises the registers wrong, then the claims coloured out of order.
   * Once exact, it minimises the change: each node moved out of order and each edit counts 100, then the pairs of
     nodes swapped and the nodes a move jumps.
3. **Tightening.** Each solution has moved nodes put back, swaps undone and edits removed while it stays exact. On
   the best two, groups of two or three moved nodes are also put back together, and two moves are replaced by one.

A single move is printed as `number %97 (@4275) just after %33 (useVideoFilter)`. A renumbering of several nodes is
printed lowest number first, with the moved nodes starred. Each moved node is numbered just after the one before it
in the list, and every other node keeps its order, as when the declarations move:

```
numbered, lowest first: %51 (noRota)* < %32 < %44 (i) < %45 (SORT) < %46 (sortItems) < ... < %52 (constUVs)* < ...
```

For the move above, `%97`, a temporary (`@4275`), needs a number right after the named local `useVideoFilter`.
One source hypothesis is to make it a named local declared there; recompilation must confirm the effect.
Named locals are numbered in reverse declaration order, with inner scopes
lower; the `@` and `$` temporaries come after them, then the code generation temporaries. See
[Registers](registers.md) and the `--cse` view for what each temporary holds.

Options:

* `--iterations N` (default 20000);
* `--seed N` (another seed can find other solutions);
* `--restarts N` (default 10);
* `--fixed V,...` (nodes not to renumber, such as a value the source cannot name);
* `--nodes V,...` (renumber exactly these);
* `--top N`.

### `replay`

Applies the edits given and prints which registers change and how many stay wrong. Use it to check a hypothesis
before editing the source:

* `--order V,V,...` (the nodes renumbered in that order: the fewest of them move, each just after the one before it,
  as their declarations would);
* `--move V:before|after:W` (the nodes between shift by one);
* `--drop V`;
* `--degree V:+1`;
* `--unlink A,B` and `--link A,B`.

### `remap`

Pairs the virtual registers of two compiles by their PCode before the allocation, instruction by instruction with the
virtual numbers left out. Give two `--load` prefixes, or SOURCE with `--override` (compiled with and without it). It
reports the graph difference:

* the nodes numbered out of their order (`%34 (dest) from %97`);
* the nodes on one side only (`nodes only in the first: %245`);
* the edges added and removed.

Use it to see what a source change did to the graph, and to carry `--want` or edits from one numbering to the other.

## Worked examples (Echoes CElementGen, GC/2.7)

Each variant below undoes one of the changes that matched `RenderParticlesIndirectTexture`.

| Variant | `target` | `search` | `remap` against the matched source |
|---|---|---|---|
| `GX_TF_RGB565, indScale != 0` | 25 wrong | single change `remove %245 (coalesced)`: exact (the fix: `indScale ? GX_TRUE : GX_FALSE`) | node only in the first: %245 |
| `void* dest` | 8 wrong | single change `number %97 (@4275) just after %33 (useVideoFilter)`: exact (the fix: `void* const dest`, a named local) | `%34 (dest) from %97` |
| `const uint bufSize` | 23 wrong | single change `number %34 (bufSize) just after %86 (@4344)`: exact (the fix: `int bufSize`) | `%97 (@4285) from %34` |

On the retained `RenderParticles` example (14 wrong, `force` EXACT), a
renumbering-only search (`--kinds none`) found solutions with four to six moves.
One four-move hypothesis put `noRota` lowest, `constUVs` after `hasModuColor`, and
two temporaries (`@5732`, `%252`) after `sortItems`. Compare seeds and verify the
result by recompiling; a graph solution does not establish a realizable source edit.

## Limits

* Spills are not modelled. An edit that leaves a node uncoloured is rejected, not scored.
* After a class is coloured again following a spill, `target` warns and its targets cannot be trusted. `verify` and
  the replay still use the last round.
* A renumbering is a hypothesis about the numbering, not about the source. Whether a local can be declared there, or a
  temporary made a named local, is for you to check. Recompile and run `remap` to confirm the graph changed as
  intended.
* The search is randomised. When it finds nothing, try another `--seed`, more `--iterations`, a narrower `--nodes`,
  or `--kinds` with `link`.

## Native API

`remap --json` now returns format 2. It retains all captured graph nodes (including
instruction-invisible temporaries), raw/unique adjacency and stored degrees, all
operand votes, ambiguous/competing targets and unmatched boundary edges. Its
mapping and comparable edge changes are explicitly inferred from instructions.
Unmatched nodes are not proven eliminated; the elimination result remains
`indeterminate` without backend lifecycle coverage.

Use `--first-round N` and `--second-round N` for zero-based round occurrences within
the selected class; the default is the last recorded round on each side. Snapshot
selection uses `--first-snapshot before|after` and `--second-snapshot before|after`.
These names refer to the existing trace snapshots, not newly instrumented backend
phases. Missing default snapshots remain explicit; unavailable explicit round
indices are errors. For actual `before_simplify` snapshots and conditional
provenance anchors, use the separate [graph correspondence API](graph-correspondence.md).

Use `mwcc-tools colouring verify|target|force|search|replay|remap` directly.
The `colouring unit` dispatcher resolves project units and accepts private candidates;
`--load PREFIX --version VERSION` analyzes saved input without compiling.
Use `--json` for structured results. Rust library callers use the `colouring`
module in `mwcc-oracle-core`. Tests live in `crates/mwcc-oracle-core/tests/colouring.rs`
and the CLI fixture suite. See the [native kit guide](../../integrations/decomp-kits/overlay/.agents/docs/oracle-native.md).
