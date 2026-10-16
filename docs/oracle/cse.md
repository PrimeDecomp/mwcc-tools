# Common subexpressions: `mwcc-tools oracle --cse`

Part of the [oracle documentation](../oracle.md).

```
mwcc-tools oracle --from-ninja ../prime/build.ninja src/MetroidPrime/Player/CPlayer.cpp \
    --function UpdateAssistedAiming --cse --ir 1557
```

`--cse` shows the temporaries the global optimizer's common subexpression pass (IRO_CommonSubs) made, in the order
it made them, and the virtual register each got. Use it when two registers are swapped and the colouring follows the
virtual registers: the order of the temporaries sets the numbering.

## How the pass orders its temporaries

The pass works on the linear IR: each expression flattened into nodes, operands before the node that uses them.

1. It marks each expression that repeats an earlier one still available.
2. It walks the IR in order. At each repeat, the expression it repeats gets its temporary, if it has none yet.

So the temporaries are made in the order of their **first repeats**, not of the expressions themselves. An expression
that occurs first can still get the later temporary, and the higher or lower register with it.

The linear order is not always the source order. A binary node's operands can be flattened right first. In the
example, `ESUB #792` has its right operand at #786 and its left operand at #791.

## The report

```
   pass 1: 20 temporaries, 34 repeats replaced
      @9227 = *gunDirFlat  [#790 line 1557; first repeat #798; 2 repeats; assigned in place] -> %f42
      @9228 = *gunToTargetFlat  [#785 line 1557; first repeat #803; 2 repeats; assigned in place] -> %f41  <- occurs before @9227's expression (#785 < #790) but repeats after it
```

* **pass N:** the function's N-th call of the pass. The `-O4` passes call it more than once.
* For each temporary:
  * the expression, at its IR index (`#790`) and source line;
  * its first repeat, which made the temporary, and how many repeats it replaced;
  * where it is assigned: in place, or where all its uses meet (the pass then moves it into a comma expression);
  * its register as the allocation starts (`%f42`, virtual), or `memory`.
* **occurs before ... but repeats after it:** a temporary made after another although its expression comes first.
  These are the pairs whose order a source change can flip.
* **other temporaries in registers:** temporaries from elsewhere, each with its register and the compiler function
  that made it, named with what it makes temporaries for: the inliner flattening an expression, linearization (an
  EFORCELOAD's value, a comma or `&&`/`||` value), or the scalarizer. The native
  `cse::temp_creator` maps observed caller addresses to these descriptions; unknown
  creators retain their address or function name.
* A pass that made nothing says so. Each pass numbers the IR anew, so its indices are not the first pass's.

**With `--exchange`.** `--exchange A,B` also says what each of the two virtual registers holds: a local, a temporary
and the function that made it, or a common subexpression's temporary with its expression and the repeat that made it.
For those, "define it later" means: make the expression repeat later in the IR.

`--ir LINE` adds the linear IR of that source line as each pass starts. `--ir all` lists the whole function. Each node
is shown with its index, its operation and operands. Each expression is shown with the temporary it got
(`-> @9227`) or the expression it repeats (`repeats #790 (@9227)`). `--ir` turns on `--cse`.

`--ir` also lists the line's propagations, which run before and between the passes:

* **copy propagation:** `x = y` with `y` a variable of the same type (or a constant). The uses of `x` read `y`.
* **expression propagation:** `x = expr` with a single use of `x` in its basic block. The use gets `expr` itself when
  nothing between them changes what `expr` reads.
* **dead assignment removed:** nothing reads `x` any more, so its assignment goes and `x` gets no register.

This is how a local disappears. `int n = v.size();` with `size()` inlined becomes `@T = load; n = @T`. Copy
propagation forwards `@T` into the uses of `n`, and the dead `n = @T` is removed, so the load stays in the
linearizer's temporary `@T`. `const bool e = mFlag; f(e);` loses `e` to expression propagation, and the load becomes
a code generation temporary.

## Example: CPlayer::UpdateAssistedAiming (Prime, GC 1.3.2)

In `CVector3f::Cross`, inlined on line 1557, `gunToTargetFlat.x` (#785) comes before `gunDirFlat.x` (#790) in the IR.
The `y` component's operands are flattened with `rZ * lX` first, so `gunDirFlat.x` repeats first (#798, against #803).
`gunDirFlat.x` gets the earlier temporary, @9227 (`%f42`), and `gunToTargetFlat.x` @9228 (`%f41`). The target has
them the other way round.
