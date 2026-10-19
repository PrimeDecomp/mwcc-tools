# Case: flag initialization and the second promoted array element

In experiments on Echoes' `CElementGen::RenderParticles` (GC 2.7), a flag kept in a two-element array
(`uint noRota[2]`, set by `for (int k = 1; k < 2; ++k) noRota[k] = ...`) leaves an extra initialization move that the
one-element form (`uint noRota[1]`, index 0) does not. The oracle explains it, and a small fixture reproduces it on GC
1.3, 1.3.2 and 2.7. All edits were tried on private copies; none was applied to the game.

## The mechanism

The optimizer promotes the array elements to registers (`COpt_ArrayToRegister`, observed as `pv.value.array`) after
the coalescing window has closed. The window closes during code generation; the promotion loop assigns indices in
ascending order, so the first promoted GPR equals the window's inclusive upper bound and the second exceeds it.

| | One-element control | Second-element case |
|---|---|---|
| Selected element | `noRota[0]` | `noRota[1]` |
| Promoted GPR / window | 598 / 56 to 598 | 602 / 56 to 601 |
| Initializer copy | merged | rejected: `destination_above_window` |
| Later assignment of true | merged | rejected above the window |
| Function size | 6,668 bytes | 6,672 bytes |

For both rejected copies the recorded checks are `interference = false`, `destination_below_window = false`, then
`destination_above_window = true`: interference is not the reason. The unused `noRota[0]` of the second case is an
invisible node of degree zero. The `promoted_copy_coalescing` objective ([source search](oracle/source-search.md))
asks for this decision.

This explains this lowering path only. It does not show that every equivalent C++ form keeps the move.

## The fixture

`tests/fixtures/source-guided/promoted_flags.cpp` has scalar, first-element and second-element controls with the same
calls, e.g.:

```cpp
#pragma opt_unroll_loops off
unsigned second_promoted_flag(const void* condition, unsigned input) {
    unsigned flag[2];
    int k;
    for (k = 1; k < 2; ++k) flag[k] = condition ? 0u : 1u;
    if (k > 0) observe(input + 7);
    if (observe(input)) flag[1] = 1u;
    observe(input + 1);
    return flag[1];
}
#pragma opt_unroll_loops reset
```

The post-loop test of `k` is needed: without it the array stays on the stack and never reaches the promotion. Whether
a candidate reproduces the mechanism is decided by the observed creation events, not by how simple the array looks.
On every version the first element coalesces and the second is rejected above the window (108 and 112 bytes). On GC
2.7, the reducer shrinks the fixture from 1,117 to 255 bytes and keeps the rejection.

## Variants and hard negatives

* No variant with the same late index-1 promotion makes the incoming copy coalesce: null-test spellings, signed and
  narrow types, unrolling on, off or reset, `while`/`do` forms, increment forms, a local reference.
* Narrow types can replace the move with a masking operation: a missing `mr` alone is not the wanted graph change.
* Moving the store after an empty loop, or into an inline helper, still produces guarded array births, but **both
  promoted nodes are degree-zero, invisible ghosts**: the live value is a frontend scalar (`@17`, `@21`; `@5986`,
  GPR 162, in the full function), made by scalarization ([scalarization](oracle/scalarization.md)). An evaluator must
  not report coalescing because it finds no incoming copy to an unused promoted node; `promoted_copy_coalescing`
  stays indeterminate there. Several `do` forms have no guarded promotion at all.
* In the full function, that store-after rewrite removes the move (6,668 bytes) but matches worse: 99.673% against
  99.937% for the second-element source. Removing the move is not the required allocation.
* The scalarization binding joins `noRota` bytes 4 to 7 to the frontend scalar and its backend birth;
  `scalarized_array_definition` then makes its definition queryable (an incoming copy from GPR 172 that coalesces,
  the producer surviving to the final register). Neither proves the old promotion was eliminated or that the
  function matches.
