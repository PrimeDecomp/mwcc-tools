# Comparing translation units

Part of the [oracle documentation](../oracle.md).

The tools below are methods on a project's units (objdiff.json's names); [api.md](../api.md) gives their parameters
and results. Each takes `text: true` for the printed report, given as `text`.

## `inline.diff`: why one TU inlines a call another keeps

```sh
mwcc-tools call inline.diff a.unit=main/MetroidPrime/Player/CPlayer \
    b.unit=IngSpaceJumpGuardian/MetroidPrime/Enemies/CIngSpaceJumpGuardian text=true --project ../echoes
mwcc-tools call inline.diff @params.json --project ../echoes  # {"a": {"unit": ..., "files": {...}}, "all": true}
```

Compiles both translation units with the `full_cost` stream. For each callee (by link name) that the two decide on
differently, it shows side by side:

* the outcomes, the estimate and the whole cost;
* the passes, the functions and the chains;
* the calls at the depth limit (`[limit +1]`), the sensitive terms and the reasons.

Chains and paths are printed as trees.

* `a` and `b` are each a unit with its `files` (in place of the disk's) and `inline_max_size`. `b` defaults to A's
  unit as on disk, so an `a` with `files` shows what the edit changes.
* `all` shows every shared callee; `callee: TEXT` only the matching ones.

### `target`: against the target asm

Checks each TU's emitted functions against the `.fn LINK` lines of its target asm. Every compiled function is listed,
with why an inline one is emitted (from the `instantiation` stream). The asm is `DIR/**/asm/<source without src/>.s`;
DIR is `target_directory`, by default the `build/<id>` directory the project's build.ninja names most. Without `b`,
`target` checks A's decisions alone against the target.

It lists the callees emitted in one and not the other, and for each TU the functions emitted differently. Some of
these are not real differences:

* The linker keeps one copy of a weak function per module. A function emitted here but held by another TU of the
  module is not a difference.
* The main program's linker also strips the weak functions nothing references. A function emitted here only in a
  main-program TU may be one of those.

A callee the target keeps has an out-of-line body whose own calls are decided there, so one misprediction brings
others. Look at the outermost one first.

Each misprediction gets a `verdict`:

* **"not inline as written"** (or it had no body yet in the original): the target keeps a call that this TU inlines
  every time, in pass 0, with its body and far within the threshold. Depth and cost are ruled out.
* **No body, the depth limit or cost:** for a call this TU keeps.

## `tu.data_diff`: a TU's data layout against its target asm

```sh
mwcc-tools call tu.data_diff project=../echoes unit=main/Kyoto/Particles/CElementGen text=true
mwcc-tools call tu.data_diff project=../echoes unit=main/Kyoto/Particles/CElementGen section=.data all=true text=true
```

Each data section of the object (from its symbol table and relocations) is compared with the `.obj` blocks of the same
section in the target asm. Objects are paired by contents, and the report shows the ones that are:

* missing, or extra;
* in another place (`[at 0x14 in the other]`);
* in another section (`[in .sdata in the other]`).

Each is shown with the first function that references it. Local names (`@N`, `lbl_...`, `...bss.0`) are not compared,
and a string pool is split into its strings.

**Float literals** are laid out in the order they were made (by `TOC_GetFloatObject`, while their first user was
compiled). A literal in another place usually means a function compiled in another order, or one the source does not
have. When a target literal comes before the literals of a function compiled before its first user, the report says
so: `made earlier, before F's literals (after G's)`. It also lists the oracle's functions compiled between G and F that
the target's text lacks: one of them made the literal, or one whose use of it was optimized away. (2.7 merged
`TOC_GetFloatObject` and `EmitFloatObject` into their callers, so they cannot be hooked there. A literal's first user
in compile order is still what made it.)

**Compared as linked:**

* Dropped, and listed in one line: local objects nothing references, those only used by functions missing from the
  target, and weak objects another TU of the module holds (vtables).
* Code that reaches a section through its anchor (`...bss.0` plus an offset) uses all of that section's objects, as far
  as the object file tells.
* The target's alignment, and the padding at the end of a section, come from the split and are not compared.

A unit whose compiler has no WebAssembly module (Echoes' GC 1.2.5n SDK units) cannot be traced in process, so it is
refused: these methods read the trace. (The removed command compiled such units with the original compiler; on
Echoes' 73 matching TUs, 67 of the 68 it could build came out the same.)

## `inline.window`: the inline limit a TU's target agrees with

```sh
mwcc-tools call inline.window project=../echoes unit=main/Collision/CollisionUtil text=true
mwcc-tools call inline.window project=../echoes units='["main/Collision/CollisionUtil", "main/GuiSys/CGuiFrame"]' \
    verbose=true text=true
```

Finds the range of inline limits (`inline_max_size`) that agrees with every inlining decision the target made. Use it
when a unit's target inlines calls ours keeps, or the reverse, with no source change that explains it.

For each function the target also has, each callee's calls are counted in our object and in the target asm (`bl`).
With the costs from the `full_cost` stream:

| Calls | Bound on the limit |
|---|---|
| Same count, inlined in both | At least the cost of the costliest one. |
| Same count, kept in both | Below the cost of the cheapest one. |
| The target makes fewer | It inlined calls ours keeps: at least their cost. |
| The target makes more | It kept calls ours inlines: below their cost. |

The report gives the window, and the calls that set each end:

```
== src/Collision/CollisionUtil.cpp: limit 125, the target agrees with 201..-
   the current limit 125 is outside it: the target's build likely used another limit for this unit
   at least 201: planeBoxOverlap in TriBoxOverlap (calls: ours 1, target 0): the target inlines all of them, ...
```

* **The current limit is outside the window:** the target's build likely used another limit for this unit. This is a
  build setting, not the source.
* **The window is empty:** no single limit agrees. Either the source still differs, or the limit differs per function.

Calls kept for another reason (not declared inline, no body, the depth limit) are not bounds; they are listed as "not
a limit question". The bounds are first-order: inlining one call changes the costs of the calls around it. When a
function calls one callee several times, the calls are not told apart, so the bound is the one that holds for at least
one of them.

## `tu.header_impact`: what a header change does to every unit

```sh
mwcc-tools call tu.header_impact project=../echoes git=REV units='["Kyoto"]' jobs=4 text=true
```

```python
from pathlib import Path
from mwcc_tools import Engine
with Engine(project='../echoes') as engine:
    edited = {'include/Kyoto/Math/CVector3f.hpp': Path('edited.hpp').read_text()}
    print('\n'.join(engine.call_one('tu.header_impact', files=edited, text=True)['text']))
```

Run it before merging a change to shared headers. It finds the units that include a changed file (from ninja's deps
log), and compiles each one with the oracle before and after the change. For every function whose code differs, it
shows:

* its size before and after;
* its similarity to the target before and after, marked `WORSE` or `better`;
* the inlining decisions that changed, with their costs;
* the frame size and the stack slots that moved.

When neither inlining nor the frame changed, the difference is in register allocation or scheduling. The last line
lists the functions that moved away from the target.

The change is given one of two ways:

* `git: REV`: the working tree against REV (`git diff --name-only REV`). Before is REV's version of each changed
  file, after is the working tree's.
* `files: {PATH: TEXT}`: after is each PATH with TEXT in place of its own; before is the checkout.

The native `header_impact::similarity` compares instruction tokens with frame offsets,
relocated fields and branch targets masked. Its score is twice the matched sequence
length divided by the combined lengths, with popularity filtering disabled. It is
a diagnostic similarity measure, not objdiff's score.

* Functions are paired by name with the compiler's local numbers left out, so `__arraydtor$89` and `__arraydtor$91`
  are the same function.
* Units whose compiler has no WebAssembly module (GC 1.2.5n) are reported with an error: the comparison reads each
  compile's trace.
* A header included through a path relative to the including file, rather than through the include path, is not
  replaced by `files`.
* Each unit is compiled twice. Shared headers can affect hundreds of units; use
  `units` to narrow the run and `jobs` to control concurrency.
