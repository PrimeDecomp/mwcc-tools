# Case: symbolic EnvModulate arguments

Echoes' `CElementGen::RenderParticles` (GC 2.7) passes `CGraphics::EnvModulate()` (a const reference to the object
`kEnvModulate`) to `CGraphics::SetTevOp` three times, twice directly and once inside the inlined
`SetParticleTevScale`. The case shows how [source search](oracle/source-search.md) reduces a backend mechanism, and
that a satisfied graph objective is not a better match. All edits were tried on private copies; none was applied to
the game.

## The mechanism

* Each `kEnvModulate` leaf returns a symbol Operand (kind 8) and creates no register. The enclosing call's Normalize
  path creates a GPR pair (LIS, ADDI); the address half is copied to the argument register r4.
* Per call, the address result merges into r4 and stays in the graph as an instruction-invisible node of degree 7,
  adjacent to `noRota` (stored degree 159) and to the zero-vector address. The high halves stay visible (degree 8).
  Both halves are inside the coalescing window.
* The guarded Normalize births bind each half to its Operand and Object, with the cached qualified name. The
  `symbol_address_pairs` objective counts these pairs by symbol; `call_argument_address_pairs` counts them inside
  one call argument. `tests/fixtures/backend/symbolic_address.cpp` reproduces the mechanism on every version.
* A search that wants fewer pairs must require at least one HA (kind 6) and one LO (kind 4) relocation of the symbol
  with addend 0, not all six: requiring all six forbids a reduction.

## Source edits tried

| Source | EnvModulate pairs | Result |
|---|---:|---|
| Baseline | 3 | 6,668 bytes; 99.949% against retail |
| Forwarding helper (reference, pointer or const-pointer parameter); pointer/integer round trip | 3 | identical graph and instructions |
| Shared `const CTevPass* const` at function entry, used by the two direct calls | 2 | one address result stays virtual without a `noRota` edge; 6,672 bytes and 99.492%: worse |
| The same pointer, or a reference, declared after the texture `Load` | 2 | the two forms give identical objects; the address results still merge into r4 |
| The direct call factored out of the `mXTAD` conditional | 2 | 6,648 bytes, one `SetTevOp` call fewer |
| Factored, and the helper's `scale == kTS_Scale1` branch dropped where the caller already excludes it | 1 | 6,624 bytes; `noRota` degree 155; retail match not measured |

## What it shows

* Graph-objective satisfaction and the final match are separate results: the first winner reduced the pairs and
  made the function match worse.
* Equal pair counts or headline graph metrics do not mean equal state; the full-state fingerprint
  (`source_search.state`) tells candidates apart.
* Call occurrence numbers and capture-local IDs are not source identities across differently lowered programs.
* Inserting a helper shifts generated local and static names (`@2573`, `skVtxDescList$1680`), so a raw
  relocation-name comparison fails on objects that are otherwise equal; the fingerprint keeps those names too.
* Extra lowering work (two more dispatch observations from the const-pointer wrapper) is not an allocation change.
* State grouping needs the fixed GQR operand of `psq_l`/`psq_st` (opcodes 403/407): the normalizer keeps its hardware
  IDs (912 to 919) rather than renaming them as virtual registers; other kind-1 operands are unsupported and make
  the fingerprint indeterminate.
* The reducer keeps the mechanism while shrinking the unit (a 222-line constructor removed): a fixed point of its
  recipe, not a minimal or transplantable source.
