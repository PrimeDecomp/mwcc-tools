# What-if colouring: the `trace.*` methods

Part of the [oracle documentation](../oracle.md).

The colouring methods analyze a function whose code matches the target except that some values sit in other
registers, most often a permutation of the non-volatile registers (r14..r31, f14..f31). It replays the compiler's
colouring from the oracle's trace and searches for small changes to the interference graph that give the target's
registers. The change is expressed as a renumbering of virtual registers, a removed node, or a degree or edge change.
That change is then mapped back to source (`trace.realize` suggests the variants):

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

`trace.load` compiles a project's unit with the trace (through `files` in place of the disk's, as `oracle.trace`
does) and keeps it in the session; the other methods name it by the handle it gives (`trace:N`). A handle lives as
long as its session, so use a `mwcc-tools serve` session or the Python package:

```python
from mwcc_tools import Engine, Trace
with Engine(project='../echoes') as engine:
    trace = Trace(engine, unit='main/Kyoto/Particles/CElementGen', function='RenderParticles__')
    print('\n'.join(trace.verify(text=True)['text']))
    print('\n'.join(trace.search('RenderParticles__', text=True)['text']))
```

* `save: PREFIX` keeps the trace, the object, the source text traced and the identity manifest (`PREFIX.trace`,
  `PREFIX.o`, `PREFIX.source`, `PREFIX.manifest.json`).
* `load: PREFIX` (with `version`) analyzes saved inputs without compiling; when a manifest is present, it checks
  trace/object hashes and compiler version.
* A unit is traced in process, by its version's WebAssembly oracle (GC 2.6, 2.7). For GC 1.3 and 1.3.2, trace the
  unit's source with `oracle.trace` (`source`, `flags`, `version`, `trace_path=P.trace`, `object_path=P.o`) and
  load `P`, with `target_asm`.
* `class: FPR` (`class_='FPR'` in Python) works on the floating-point registers; the default is GPR.

## Commands

### `trace.verify`

```python
with Engine(project='../echoes') as engine:
    verified = Trace(engine, unit='main/Kyoto/Particles/CElementGen').verify(text=True)
```

Replays every function (or `function`'s) and checks that the simplification gives the traced order and select the
traced colours, including the registers it claimed. `verified['result']['functions']` has one row per class and
colouring (`text` one line each), and `verified['result']['ok']` is false on any disagreement. The other methods are only as good as this replay, so
run it first on a new compiler version or an unusual function.

Retained replay examples (all exact):

| Project | Compiler | Unit | Colourings |
|---|---|---|---|
| Echoes | GC/2.7 | CElementGen | 181, 102 of them with claims, one after a spill round |
| Prime | GC/1.3.2 | CPlayer | 274 |
| Prime | GC/1.3.2 | CStateManager | 308 |
| Prime | GC/1.3.2 | CElementGen | 134 |

### `trace.target`

The target's register for each virtual register. It pairs instructions in two steps:

* our object with the PCode after the allocation, by the physical registers each instruction names;
* our object with the target's asm, by shape (the same instruction up to registers and relocations).

Each register field of ours is then traced back to the virtual register it holds, and the target's field at the same
place votes for that register.

* The asm is the one the unit's project builds, beside its target object (`build/<id>/asm/...`), or `trace.load`'s
  `target_asm`.
* `wants` overrides or completes the derived targets (`{"165": 26}`).

Checks:

* **CONFLICT:** two interfering nodes, or a node and a physical neighbour, given one register. The pairing went wrong
  somewhere, or a `wants` entry is mistaken.
* **WARNING:** a class coloured again after a spill. The last round's node numbers need not be the PCode's, so these
  targets cannot be trusted.
* Nodes holding non-volatile registers with no target (no field paired) are listed. Search still renumbers them.

On a matched function, every derived target equals our register.

### `trace.force`

Colours the graph as it is, with the nodes that claim each non-volatile target register reordered by that register,
highest first.

* **EXACT** means the graph can give the target's colours and only the colouring order is wrong. Search the
  numbering (and the degrees, which decide the passes).
* Otherwise no order of the claims gives the target. The graph itself must change: an edge, or a node.

### `trace.search`

1. **Single changes.** Each change is tried alone, and the ones that leave fewer registers wrong are listed. A single
   exact change, such as a temporary numbered elsewhere or a coalesced argument node removed, often shows up here
   directly. The changes are:
   * each node to renumber, numbered just after each other one (or before the lowest), the nodes between shifting by
     one as they do when a declaration moves;
   * the graph edits of `kinds` (default `["drop", "degree", "unlink"]`; `[]` for a renumbering only): removing a
     coalesced node next to a wrong one, a degree one higher or lower for a node in the claims, removing (or, with `link`,
     adding) an edge between a wrong node and such a node.

   Among changes equally good, the shorter moves come first.
2. **Annealing.** A simulated annealing searches a renumbering of the nodes to renumber (the claimers, the wrong nodes
   and the untargeted non-volatile nodes), together with a set of graph edits and of the single moves that helped. It
   starts from the best single change, `restarts` times (default 10), sharing `iterations` between the runs.
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
[Registers](registers.md) and the `views.cse` view for what each temporary holds.

Options:

* `iterations` (default 20000);
* `seed` (a string; another seed can find other solutions);
* `restarts` (default 10);
* `fixed: [V, ...]` (nodes not to renumber, such as a value the source cannot name);
* `nodes: [V, ...]` (renumber exactly these);
* `top`.

### `trace.replay`

Applies the edits given (`edits`, a list) and gives which registers change and how many stay wrong. Use it to check a
hypothesis before editing the source:

* `["order", [V, V, ...]]` (the nodes renumbered in that order: the fewest of them move, each just after the one
  before it, as their declarations would);
* `["move", V, "before"|"after", W]` (the nodes between shift by one);
* `["drop", V]`;
* `["degree", V, 1]` and `["neighbours", V, 1]`;
* `["unlink", A, B]` and `["link", A, B]`.

```python
trace.replay('RenderParticles__', [['move', 97, 'after', 33], ['degree', 35, 1]], text=True)
```

`trace.replay_many` replays many edit lists (`candidates`) on the same graph, each scored against the target.

### `trace.remap`

Pairs the virtual registers of two compiles by their PCode before the allocation, instruction by instruction with the
virtual numbers left out. Load two traces (two saved prefixes, or a unit with and without `files`) and give their
handles as `first` and `second`:

```python
from pathlib import Path
from mwcc_tools import Trace, Unit
unit = Unit(engine, 'main/Kyoto/Particles/CElementGen')
edited = Path('CElementGen.edited.cpp').read_text()
before = Trace(engine, unit, function='RenderParticles__')
after = Trace(engine, unit, function='RenderParticles__', files={unit.info['source']: edited})
remap = engine.call_one('trace.remap', first=before.handle, second=after.handle,
                        function='RenderParticles__', text=True)
```

It reports the graph difference:

* the nodes numbered out of their order (`%34 (dest) from %97`);
* the nodes on one side only (`nodes only in the first: %245`);
* the edges added and removed.

Use it to see what a source change did to the graph, and to carry `wants` or edits from one numbering to the other.

### `trace.realize`

Turns one solution's edits (`search`'s `exact` or `best`) into source variants of the traced text: `experiment.run`
candidates, each labelled `suggested` with the edit it realizes. Evaluate them like any others:

```python
found = trace.search('RenderParticlesIndirectTexture', text=True)['result']
realized = trace.realize('RenderParticlesIndirectTexture', found['exact'][0]['edits'], text=True)
rows = list(unit.experiment('runs/realize', {'list': realized['candidates']},
                            baseline={unit.info['source']: traced_text}, symbols=[link]))
```

It finds each vreg in the function's own body: its definitions and uses (the trace's instruction lines) and its
name and kind (`--cse`'s: a local of the source, a common subexpression's temporary, another temporary).
`vregs` reports them, and `skipped` what no rewrite was found for. The rewrites, by edit:

| Edit | Rewrites |
|---|---|
| `move`, `order` of a named local (and a `move`'s anchor) | `const` added or dropped, the signedness swapped (`int` / `unsigned int`, `s32` / `u32`), both; the declaration moved beside the anchor's; the initializer split from the declaration; the local made a temporary |
| `move`, `order` of a temporary | the temporary named (`__typeof__(e) temp97 = e;`, with and without `const`), the expressions sharing most names with its CSE tree first; the declarations on its statement as for a local |
| `drop` | a local made a temporary; on a temporary's statement, `e != 0` as `e ? 1 : 0`, its locals' declarations, a part named |
| `degree +`, `neighbours +`, `link` | a use extended: the statement of its last use swapped with the next |
| `degree -`, `neighbours -`, `unlink` | the definition split (`x = a + b;` as `x = a; x += b;`), started later, or a part of it named |

`limit` (default 32) caps the candidates, taken from the edits in turn. A trace keeps the text it compiled (a
saved one in `PREFIX.source`), so the variants are of that text, not of the disk's.

The rewrites read the source as text: a variant that does not compile is reported as such by its row. Statements
are only swapped when neither has a call and they share no identifier, so the swap keeps the meaning. The other
rewrites can change it: a named temporary evaluates its call earlier, and a changed type converts differently. A
matching row is a suggestion to review, not a proof.

## Worked examples (Echoes CElementGen, GC/2.7)

Each variant below undoes one of the changes that matched `RenderParticlesIndirectTexture`.

| Variant | `target` | `search` | `remap` against the matched source |
|---|---|---|---|
| `GX_TF_RGB565, indScale != 0` | 25 wrong | single change `remove %245 (coalesced)`: exact (the fix: `indScale ? GX_TRUE : GX_FALSE`) | node only in the first: %245 |
| `void* dest` | 8 wrong | single change `number %97 (@4275) just after %33 (useVideoFilter)`: exact (the fix: `void* const dest`, a named local) | `%34 (dest) from %97` |
| `const uint bufSize` | 23 wrong | single change `number %34 (bufSize) just after %86 (@4344)`: exact (the fix: `int bufSize`) | `%97 (@4285) from %34` |

`trace.realize` of each search's exact edit, every suggestion evaluated by an experiment against the variant (about
6 s a case: trace 1.6 s, search 0.4 s, realize 0.1 s, experiment 3.5–4.5 s):

| Variant | Suggestions | Compile | Strict-exact | Among them |
|---|---|---|---|---|
| `void* dest` | 4 | 4 | 3 | `void* const dest`; `CGraphics::GetDolphinSpareBuffer()` named, with and without `const` |
| `const uint bufSize` | 4 | 4 | 3 | `int bufSize`; `const int bufSize`; `bufSize` made a temporary |
| `indScale != 0` | 13 | 13 | 1 | `indScale ? GX_TRUE : GX_FALSE` (`? true : false` and `? 1 : 0` do not match) |

A round trip over CElementGen's strict-exact functions undid one choice per local declaration, then traced,
searched, realized and evaluated each case. The `const` cases used the search's defaults, and the signedness cases
4,000 iterations and 3 restarts (5–9 s a case):

| Undone | Cases | Registers differ | An edit found | A suggestion strict-exact |
|---|---|---|---|---|
| `const` added or dropped | 80 | 1 | 1 | 1 |
| signedness swapped | 48 | 11 | 11 | 8 |

Most `const` changes either change nothing or do not compile. The registers counted are those that differ beyond
the unchanged source's: `RenderParticlesFlameThrower` has 14 wrong as it is, behind `target`'s spill warning, and
one of its cases was restored all the same. One restored function took a different local's type
(`const uint packedColor` as `int packedColor`): it matches, but it is not the source that was undone. The three
misses: two have a two-edit solution (`degree` and `move`) that no single rewrite realizes, and one is a `move`
whose suggestions compile but do not match.

On the retained `RenderParticles` example (14 wrong, `force` EXACT), a
renumbering-only search (`kinds: []`) found solutions with four to six moves.
One four-move hypothesis put `noRota` lowest, `constUVs` after `hasModuColor`, and
two temporaries (`@5732`, `%252`) after `sortItems`. Compare seeds and verify the
result by recompiling; a graph solution does not establish a realizable source edit.

## Limits

* Spills are not modelled. An edit that leaves a node uncoloured is rejected, not scored.
* After a class is coloured again following a spill, `target` warns and its targets cannot be trusted. `verify` and
  the replay still use the last round.
* A renumbering is a hypothesis about the numbering, not about the source. `trace.realize` gives source variants
  that may realize it, and an experiment shows which do. Run `trace.remap` to see how a variant changed the graph.
* The search is randomised. When it finds nothing, try another `seed`, more `iterations`, a narrower `nodes`,
  or `kinds` with `link`.

## Native API

`trace.remap`'s `result` is format 2. It retains all captured graph nodes (including
instruction-invisible temporaries), raw/unique adjacency and stored degrees, all
operand votes, ambiguous/competing targets and unmatched boundary edges. Its
mapping and comparable edge changes are explicitly inferred from instructions.
Unmatched nodes are not proven eliminated; the elimination result remains
`indeterminate` without backend lifecycle coverage.

Use `first_round` and `second_round` for zero-based round occurrences within
the selected class; the default is the last recorded round on each side. Snapshot
selection uses `first_snapshot` and `second_snapshot` (`before` or `after`).
These names refer to the existing trace snapshots, not newly instrumented backend
phases. Missing default snapshots remain explicit; unavailable explicit round
indices are errors. For actual `before_simplify` snapshots and conditional
provenance anchors, use the separate [graph correspondence API](graph-correspondence.md).

Each method gives its structured `result`, and `text: true` adds the report's lines.
`trace.load` resolves project units and takes private candidates as `files`;
`load: PREFIX` with `version` analyzes saved input without compiling.
[api.md](../api.md) gives every parameter. Rust library callers use the `colouring`
module in `mwcc-oracle-core`. Tests live in `crates/mwcc-oracle-core/tests/colouring.rs`
and `crates/mwcc-methods/tests/offline.rs`.
See the [native kit guide](../../integrations/decomp-kits/overlay/.agents/docs/oracle-native.md).
