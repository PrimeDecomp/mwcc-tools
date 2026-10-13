# Inlining

Part of the [oracle documentation](../oracle.md).

## How the inliner goes over a function

The inliner makes one pass over a function per inline depth, up to the `-inline` depth limit (8 by default). A
decision's `depth` is that pass.

* A call that is kept is decided again in every pass, with less depth left for its estimate each time.
* A call first met in a later pass comes from a body inlined in an earlier one. A decision's `chain` lists those
  bodies, outermost first, by link name (`inside:` in `--inline-cost` and `--near`).
* Callees are shown qualified (`CWeapon::GetOwnerId`). A decision made several times is shown once, with how many
  times, at which depths and on which source lines. The line is that of the statement containing the call, in the
  file it comes from: for a call inside an inline body, a header's line.

## `--inline-cost CALLEE`

Shows only the decisions about callees whose name or link name contains CALLEE, each with its cost broken down:

* one line per statement-level expression of the callee's body, for example `*(arg0+8) = arg1: 5`;
* under it, the bodies of the inline functions that statement calls (`the body of LINK`), with what each would cost as
  a plain call instead, in the order they are costed.

Marks in the breakdown:

| Mark | Meaning |
|---|---|
| `[limit +1: LINK ...]` | A call at the inline depth limit. |
| `[LINK: no body yet ...]` | An inline function with no body yet, costed as a call. |
| made by the compiler | The callee is an implicit constructor or destructor. |

Only statements go through the hook; the compiler costs an expression's operands itself. So a body is shown under the
statement it is in, named by its callee.

## `--near N`

Every decision of the automatic inliner whose cost is within N of the threshold, in all functions, with the callee's
link name, the function and the lines. These are the close calls a header change can flip.

The estimate stops as soon as it passes the threshold. So a kept call's cost is where it stopped (126 to 128 with
threshold 125), unless `--full-cost` is given.

## `--full-cost`

The estimates are made a second time, past the threshold, to get the whole cost. It also reports:

* **The depth limit.** Whether a call in the body is at the inline depth limit. If one is, the compiler sets the
  estimate to the threshold plus one, whatever the size, and the body is never inlined. Many calls kept at 126 are
  this, not their size. The whole cost leaves such calls out, but counts their arguments.
* **`limit_paths`:** each such call, as the chain of callees from the body down to it, printed as a tree.
* **`sensitivity`:** the calls whose cost would change if the callee's body were, or were not, available, when that
  alone flips the decision or moves the cost by 2 or less:
  * `body`: an inline function costed as its body would cost 1 as a plain call (not inline in another TU, or not
    defined yet there);
  * `no body`: one with no body yet would cost its body, as costed elsewhere in the TU.

## `--instantiation TEXT`

For the functions whose link name contains TEXT, lists in order when their bodies came to be:

1. queued for parsing: an inline definition, a template class member, a template function instance or a
   compiler-made method. `from=` is the function being parsed then, `expanding=` the one being inlined into;
2. asked for by the inliner;
3. parsed, and whether the inliner has the body after;
4. compiled as a template function instance;
5. marked to be emitted, and why;
6. code generated from its saved body, then compiled.

The inliner's decisions about calling these functions are interleaved, with the function they are in, the pass, and
`NO BODY yet` when the body was missing.

**Why a function is emitted.** Something references it (`fn_0050f240`). That is one of:

* a call or an address left in the code after inlining;
* the destructor of an object live in a statement, through its exception actions. It is emitted even when every call
  of it was inlined and no reference is left in the object;
* emitted data: a vtable or an initializer.
