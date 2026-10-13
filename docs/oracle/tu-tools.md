# Comparing translation units

Part of the [oracle documentation](../oracle.md).

## `tools/inline_diff.py`: why one TU inlines a call another keeps

```
python tools/inline_diff.py --from-ninja FILE A [B] [--all] [--callee TEXT] [--target [DIR]] [--json]
```

Compiles both translation units with `--full-cost`. For each callee (by link name) that the two decide on
differently, it shows side by side:

* the outcomes, the estimate and the whole cost;
* the passes, the functions and the chains;
* the calls at the depth limit (`[limit +1]`), the sensitive terms and the reasons.

Chains and paths are printed as trees.

* B defaults to A. A and B can also be objdiff.json unit names, or their ends (`Enemies/CIngSpaceJumpGuardian`).
* `--all` shows every shared callee; `--callee TEXT` only the matching ones.

### `--target`: against the target asm

Checks each TU's emitted functions against the `.fn LINK` lines of its target asm. Every compiled function is listed,
with why an inline one is emitted (from `--instantiation`). The asm is `DIR/**/asm/<source without src/>.s`; by default
DIR is the `build/<id>` directory the build.ninja names most.

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

## `tools/data_diff.py`: a TU's data layout against its target asm

```
python tools/data_diff.py --from-ninja FILE SOURCE [--section NAME] [--all] [--json]
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

Without an oracle for the version (GC 1.2.5n), the original compiler compiles the source. On Echoes' 73 matching TUs,
67 of the 68 it can build come out the same; the last is a global the target's split does not place.
