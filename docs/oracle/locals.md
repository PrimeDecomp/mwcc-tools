# Locals and the stack frame: `--locals`

Part of the [oracle documentation](../oracle.md).

```
python tools/oracle.py --from-ninja ../echoes/build.ninja src/GuiSys/CAuiBitmapMeter.cpp --function Draw --locals
```

`--locals` explains why a local struct or class ends up in registers or on the stack. With the target asm, it also
shows where the target's frame differs from ours. Use it with `--function`.

The report has five parts:

1. [The scalarizer](#the-scalarizer): each local aggregate, split into scalars or kept in memory, and why.
2. [Formals of inlined calls](#formals-of-inlined-calls): how the inliner bound each argument.
3. [The target's frame](#the-targets-frame): its size and the r1 offsets its code uses.
4. [Slot shifts](#slot-shifts): objects the target places in another order.
5. [One-sided objects](#one-sided-objects): objects only one side keeps in memory.

Parts 3 to 5 need the target asm: `--from-ninja`, or `--target DIR` with a dtk build directory.

## The scalarizer

The global optimizer's scalarizer (IRO_ScalarizeClassDataMembers) runs once or twice per function. It tries to replace
each local aggregate, and each compiler temporary, by one scalar per member. For each one, the report says:

* **split into scalars:** each part, by bytes, with the scalar made for it and where that ended up (a register,
  memory, or gone). A line says what would keep it in memory.
* **KEPT IN MEMORY:** why, and what would let it be split.

Locals in memory that the pass never saw are listed after.

### Why a local is kept in memory

| Reason | Typical cause |
|---|---|
| A use of the whole as a struct or class | A copy of it, or of a member aggregate: a by-value argument, a return, an assignment. |
| A use at an unknown offset | An index that is not a constant. |
| Overlapping uses | The same bytes used at different sizes or types, such as bytes stored one by one, then read as a word. |
| Address taken, pointer analysis off | Its member uses are not even recorded. |
| Address taken, pointer analysis on | A call may reach it. |

### Copies: which ones keep a local in memory

All of this was checked on 2.7.

| Copy | Effect |
|---|---|
| `g = f` of a POD | Member by member. Does not keep a local in memory. |
| Copy construction of a class with no copy constructor (`F h = f;`, or a member initialiser `mOrigin(o)`) | A whole copy: keeps both in memory. |
| The same, with an inline copy constructor that copies each member | Split into registers. |
| Assignment of a class through the operator= the compiler makes | Copies each member whose class has no **user** operator= whole, even if that class has a user copy constructor. |

So:

* For an assignment, give the class that is copied whole a user operator= that copies each member, inline.
* For a construction, give it a user copy constructor.

For example, a user copy constructor on CVector3f does not split a CLine that `mAxis = axis` copies, but a user
operator= on CVector3f that copies each member does.

Under each local kept by a whole copy, the report lists the inlined constructors and operator= it is the argument of:

```
l is the argument of inlined L::operator= (__as__1LFRC1L, made by the compiler)
```

## Formals of inlined calls

For every call inlined into the function, grouped by callee, the report shows how each formal was bound:

| Mode | Meaning |
|---|---|
| substituted | The caller's expression stands for the formal (aliased): a const reference to an lvalue, a `this`. |
| reference | Bound to the temporary the argument constructs. |
| local | A fresh local holding the argument (a modified formal, or an argument with side effects), or its address for a reference formal. |
| unused | Not used. |

Each local and temporary is shown with where it ended up.

**By-value class arguments.** These come as `reference`: the caller copy-constructs the argument into a temporary.
`v (reference): reference -> @5 (memory r1+8)` is that stack copy. A const reference to the same lvalue is
substituted instead, with no copy. A by-value formal that is missing in ours explains a target's extra stack copy of an
argument.

By then the inline variable is already a reference, so `reference` alone does not tell a by-value formal from a
`const &` bound to a temporary such as `V(a, b, c)`. The report labels each formal from the callee's declared argument
types: `BY VALUE, a class of N` when the declaration takes it by value.

**What a local pointer formal holds.** An inlined member function's `this`, or a `const &` with side effects, can
become a local holding an address. The report shows what that address is:

* `@729=&tmp`: the address of an object, followed through other such locals;
* otherwise the argument in short form. `@729=(data((*..+188))+12)` is element 1 of a reserved_vector member at
  this+188, and `@654=__vc((*this+188),..)` is that vector's operator[].

An object whose address such a local holds is address-taken. These objects are listed under "objects whose address an
inlined call holds", in the order the formals were bound.

## The target's frame

The report compares the target's frame size with ours. It gives the target's locals area by the r1 offsets its code
uses, grouped into runs:

* the loads, stores and `addi` at each offset (an `addi` means the address is taken);
* our locals at those offsets.

For example, a run of `stw`/`stb`/`stb`/`sth`/`stw` at r1+0x4c is a 12-byte struct the target keeps in memory, member
by member.

## Slot shifts

Our instructions are paired with the target's: difflib over the instruction words, with frame offsets, relocated
fields and branch targets masked. For each pair of r1 accesses below the register saves, the report takes the target's
offset minus ours, and files it under the object of ours that the access is in.

Objects are grouped by their usual delta, smallest group first:

* A group of one is an object the target places in another order. The report names the object of ours at the offset
  where the target has it.
* Our temporaries get their slots in the order they are made. So that object is the one whose place in the order the
  original source differs on.

Example, LoadSurfaceCamera: `-0x120: @996 (12@r1+0x154)`. The target has @996 where ours has @1043, while the 24 other
12-byte temporaries are all at +0xc.

## One-sided objects

The same pairing finds the objects only one side keeps in memory:

* **Target only:** the target's r1 offsets with no paired access of ours, merged into runs, with their loads and
  stores. Example, PreRender: `r1+0x4c..0x57 (12 bytes): stw x8, stb x8, sth x4, ...` is a CModelFlags the target keeps
  in memory, member by member, where ours splits it.
* **Ours only:** our in-memory objects with no paired access of the target's.

**Identical copies.** Within a target-only run, the report names copies of one value: stores of the same registers, not
rewritten in between, in the same order at the same distances. For each group, it gives where the copies are, whether
they were stored copy by copy or interleaved, which copy was stored first, and which members are read back.

Example, CAuiBitmapMeter::Draw (Echoes):

```
3 identical copies of a 12-byte value (f4, f6, f5) at r1+0x5c, 0x68, 0x74, stored interleaved (first: 0x74)
   read back: the copy at 0x74: +0 (lfs); the 2 other(s) never read (dead stores)
```

These are temporaries the target keeps and ours splits. Only one member of one copy is read; the rest are dead stores.
