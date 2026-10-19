# Real case: flag initialization and the second promoted array element

The extra flag-initialization move is reproducible and has an observed deciding
condition. In the retained two-element `RenderParticles` experiment, the optimizer
promotes `noRota[1]` after the coalescing window has closed. Its initializer copy
passes the interference test, then fails because its destination is above the
window. A standalone example reproduces the same mechanism on all three supported
compiler versions on Windows and Linux.

This explains a concrete compiler behavior. It does not prove that all equivalent
C++ forms must retain the move or that any retained experiment matches the game.
The game checkout was only read; all compiles use private source copies and sealed
oracle producers.

## Actual RenderParticles source and observations

The clean symbolic baseline has scalar `bool noRota`, whose pointer-zero
initialization writes the scalar directly. The array experiments are separate
historical matching candidates, retained read-only under the game's ignored
`.agents/state/renderparticles/` directory:

- `cpp-array-init-select-1` declares `uint noRota[1]` and initializes index 0 in a
  one-iteration loop.
- `cpp-array-init-select-2` declares `uint noRota[2]` and initializes index 1 with
  `for (int k = 1; k < 2; ++k) noRota[k] = ...;`.

The condition is the same pointer-zero test as the original flag, written in these
experiments as `(reinterpret_cast<uint>(mLoadedGenDesc->mROTA) ? 0u : 1u)`. Subsequent
tests use the selected array element. These sources retain symbolic EnvModulate
references; they contain no fixed retail-address replacement for those arguments.

Fresh GC 2.7 V3 captures established the helper births and actual decisions. Fresh
V4 captures add the original array Object, complete name/type, element bounds and
index at the guarded `COpt_ArrayToRegister` increment. The source role is therefore
an observed binding, rather than an interpretation of a register number or range.
Both producer generations reproduce the same stock objects for the exact sources.

| Observation | One-element control | Second-element case |
|---|---|---|
| Selected source element | `noRota[0]` | `noRota[1]` |
| Creation phase | optimizer | optimizer |
| Guarded GC 2.7 creation site | `0x574005` | `0x574005` |
| Selected birth sequence / GPR | 1116 / 598 | 1120 / 602 |
| GPR coalescing window | 56–598 inclusive | 56–601 inclusive |
| Initializer copy | 598 ← 200 | 602 ← 203 |
| Actual decision | merged into 200 | rejected: `destination_above_window` |
| Later assignment of true | 598 ← 208, merged | 602 ← 211, rejected above window |
| Selected function size | 6,668 bytes | 6,672 bytes |
| Function relocations / EnvModulate relocations | 166 / 6 | 166 / 6 |

For both rejected copies, the recorded short-circuit checks are
`interference = false`, `destination_below_window = false`, then
`destination_above_window = true`. Interference is not the deciding reason.
The initial copy is absent after allocation in the control. It remains an actual
`mr` after allocation in the second-element case; its observed physical operands
are incidental evidence, not the preservation predicate.

The second case also creates `noRota[0]` as GPR 601, birth 1119. That unused first
element is invisible and has degree zero. The live second element has degree 159.
The one-element control's promoted node retains degree 159 after its copy merges
and the node disappears from instructions. These are graph observations, not
claims of semantic elimination.

The original coalescing window closes during code generation. In these cases, the
optimizer's first promoted GPR equals the inclusive upper bound; its second GPR
exceeds that bound. The guarded scalar promotion loop assigns indices in ascending
order. This accounts for the observed first/second difference within this specific
lowering path. Earlier frontend scalarization or another lowering path needs its
own provenance and must not be ruled out by this observation.

The exact whole-object SHA-256 values are:

```text
first:  09ce9ce81cebbf88880ac84fb4a0b36fc83db81eb85d47736b1be43178d54faa
second: 3d207d98e4b05e13cf7340acef4581959969735f1dc31077a8512d99e885b536
```

V4 source, command, object, raw capture and typed provenance are retained at
`build/source-guided/promotion-audit/real-{first,second}-v4/`. Exact dependency and
runtime snapshots, preprocessing scans and raw input blobs are in each
`inputs-absolute/` directory. The paired summary is
`build/source-guided/promotion-audit/real-case-v4-verification.json`.
Array, call-result, counter, graph and decision coverage are satisfied within their
declared guarded scopes. This does not complete semantic lifetime or inline-origin
coverage.

## Small standalone reproducer

[`promoted_flags.cpp`](../tests/fixtures/source-guided/promoted_flags.cpp) removes
SDK types and the rest of the particle renderer. It contains scalar, first-element
and second-element controls with the same observable call sequence. The relevant
second-element function is:

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

The post-loop counter test is an essential reproducer precondition. Without it,
the first small examples retained stack arrays and never reached the guarded
array-promotion path. This is consistent with the reconstructed pipeline, which
conditionally runs array promotion after later constant propagation. The
pass-enabling condition itself has not been directly instrumented.
Actual creation events, rather than the apparent simplicity of the C++ array,
decide whether a candidate reproduces the mechanism.

The first-element control changes the loop to `k = 0; k < 1` and the selected
element to 0. On all three versions, its promoted element equals the window's
upper bound and its initializer coalesces. In the second-element control the
selected element is one above the upper bound and the initializer is rejected.
The functions are 108 and 112 bytes respectively; each retains three `observe`
REL24 relocations. Physical register choices are not asserted.

The exact fixture bytes are stock-verified on both hosts for GC 1.3, 1.3.2 and 2.7;
the complete objects agree across hosts for each version. Windows V4 probe off/on
checks also preserve those objects. New origin records bind the original `flag`
array to indices 0 and 1, element size 4, array size 8 and count 2. The creation
sites are `0x571085`, `0x571d45` and `0x574005` respectively.

V3 cross-host evidence is under `promotion-audit/fixture-v1/`; V4 Windows evidence
and exact snapshots are under `promotion-audit/fixture-v4/windows/`. Linux V3
inputs/results are retained at `<linux-cache>/mwcc-promotion-audit/fixture-v1/`.
The fixture SHA-256 is
`58850d01da35ac31312c13155316c71e3132d99cf109dd605785e46a9571ddf7`.

## Bounded source experiments and uncertainty cases

The native reducer preserves this behavior without fixing physical registers.
On GC 2.7 it shrinks the complete 1,117-byte three-control fixture to 255 bytes
over 59 candidates and six independently verified acceptances. The selected
promoted register/window changes from 44/43 to 41/40, while incoming-copy ordinal
zero still rejects at `destination_above_window` and the required symbolic
`observe` REL24 relocation remains. Three fresh replay builds reproduce the final
object exactly. The final source hash is
`cea92a0e40f1f23e4910d129d9dea7bb1769317c3ec50693151b611a4028ea37`;
the object hash is
`1ef5499369e1cba4a22e9ba0aea7a54b7ea1f8a47ac41bb80ec795284c4a2c5d`.
This is a fixed point of configured byte/line reductions, not a global minimum
or a semantics-preserving transplant. The native run, replay, exact inputs and
ghost-baseline rejection are retained under
`build/source-guided/promoted-reducer-audit/`.

The retained standalone families compare equivalent null-test spellings, signed
and narrow array types, one/two-element initialization, debug listing, unroll
on/off/reset/default, `while`/`do` loop forms, increment forms, an empty loop followed
by a fixed-index store, a local reference and an inline helper. The exact sources,
native commands, stock objects, traces and typed decisions are retained in
`promotion-audit/bounded-v4/` and `promotion-audit/bounded-stage2-v4/`. Each family has
a closed source allowlist and bounded justification; this is not a general C++
equivalence checker.

None of the variants with the same live, late index-1 promotion makes its incoming
copy coalesce. Enabling or resetting loop unrolling preserves the observed
above-window rejection. Narrow types can replace an initialization move with an
extra masking operation; a missing `mr` alone is not evidence of the desired graph
change.

There are two particularly useful hard negatives. Moving the initialization to a
fixed-index store after an empty loop, or routing that store through an inline
helper, still produces guarded array births. However, **both promoted nodes are
degree-zero, instruction-invisible ghosts**. The live initializer instead defines
a frontend anonymous scalar (`@17` or `@21` in these exact captures). An evaluator
must not report successful coalescing merely because it cannot find an incoming
copy to the now-unused promoted node. Several `do` forms have no guarded promotion
at all and likewise cannot establish success for an array-promotion selector.

These examples motivated explicit frontend scalarization correspondence in addition
to backend array-origin records. The V5 extension below closes the exact observed
creation join for these cases. Full semantic lifetimes and the final symbolic
RenderParticles match remain open.

## The same transition in the full function

The exact outside-loop-store rewrite was also tested in the private full
second-element `RenderParticles` source. Its one-iteration initializer becomes an
empty loop followed by the same assignment to `noRota[1]`; the source and all other
statements are unchanged. It remains stock-verified and retains all 166 function
relocations, including the six symbolic EnvModulate relocations.

V4 still observes both `noRota` promotions, now GPR 589/index 0 and 590/index 1.
Both are degree-zero, instruction-invisible nodes. The live pointer-zero
initializer instead defines anonymous scalar `@5986`, GPR 162, directly. This is
an observed change of lowering mechanism; it is not coalescing of the selected
promoted node. Correspondence between the old live promoted value and this new
scalar still needs justification beyond their source construction and instruction
uses.

The function shrinks from 6,672 to 6,668 bytes, but independent objdiff with
`functionRelocDiffs=data_value` gets worse: 99.673065%, against 99.93701% for the
second-element control (the one-element control is 99.838036%). Thus removing the
extra move is separate from obtaining the required final allocation. No source
was applied to the game.

The exact recipe, source, native command, stock object, complete trace, typed
provenance, graph summary and objdiff output are retained under
`build/source-guided/promotion-audit/real-store-after-v4/`. The source SHA-256 is
`ad2019e7951fef2bacd5f15e940a5bd72f8bd98e75d02636bd4d4fd0eaeda981`;
the object SHA-256 is
`943ba6ad056e1c6aecc71c49df3b3f5d9733f5196804aa632f8c511b548d6e14`.

## V5: identify the frontend-created live value

The new [scalarization trace](oracle/scalarization.md) directly observes the retail
scalarizer's return. In a fresh full-function capture it binds original `noRota`
bytes 4–7/bits 32–63 to the returned scalar Object and VarInfo, then to backend
birth `54:209`, GPR 162. It checks the exact function Object, complete pass/end
totals and all scalar passes ending before backend creation. Two passes contain
79 bindings. The stock object and all preexisting trace records agree with the
same input without the new probes. The object hash remains the one above.

Nine standalone captures span all three supported versions. Six outside-loop or
inline-helper cases establish the expected live `flag[1]` scalar join; three
original-promotion controls do not. This is observed creation provenance, not a
proof of source declaration ancestry, dominance, lifetime or equivalence of two
values across variants. The old promoted nodes remain unused and the
`promoted_copy_coalescing` predicate correctly stays indeterminate for them.

The narrow constant-LI initializer predicate also remains indeterminate: the live
pointer-zero computation uses a different instruction sequence. Retained V5 raw,
typed and stock-verification evidence is under
`build/source-guided/scalar-origin-audit/`.

The separate `scalarized_array_definition` objective makes this observed
computation queryable without calling an instruction ordinal a C++ initializer.
For the full function, definition zero is the incoming copy from GPR 172 into
the frontend scalar GPR 162. Actual coalescing merges these values; the opcode-103
producer survives into the graph and defines the recorded final register, GPR 27,
with no surviving incoming copy. The predicate can preserve this observed
mechanism during reduction or source search while the final matching score and
semantic-lifetime questions remain separate. It does not establish that the old
unused promotion was eliminated or that the complete function now matches.
