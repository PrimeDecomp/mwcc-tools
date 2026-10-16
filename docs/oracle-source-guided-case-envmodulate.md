# Real case: symbolic EnvModulate arguments

This is a negative source-variant experiment and an observed compiler mechanism,
not a new RenderParticles match or a successful source-search objective.

The isolated clean source comes from Echoes main `7968f74ae1c3268a9cb03104a60635673d31deb5`.
Its SHA-256 is `0a216aa15cd1f169e67ff43bdec4867f55ae5f1f8da4cbc883ce1b6ec60dfab3`.
Fresh GC 2.7 compiles on Windows and Linux produce the stock-identical complete
object `8c1304a643c8e8ab49fa2dd2331073d5d2c0ec0341bfb58270417becac835161`.
The live game source, including its externally applied fixed-address workaround,
was preserved. Every variant is a private copy with the `CElementGen.cpp` basename.

## What the new observations establish

RenderParticles has 4,080 balanced expression-dispatch observations and 1,104
birth observations on both hosts. Of those births, 760 have an active codegen
dispatch context, 42 have only a statement-root context and 302 have neither.
These counts do not establish complete source or semantic lifetime provenance.

Three codegen leaves describe `kEnvModulate`. Each returns an Operand of kind 8
(symbol), with the same capture-local Object identity and displacement zero.
The leaf itself creates no virtual register. Its enclosing `SetTevOp(0,..)` call
subsequently creates a pair of GPR temporaries at retail sites `0x4e2fea` and
`0x4e2ff6`. Those sites belong to metadata container `_fn_004e1d90`; the container
name is not a demonstrated semantic function boundary.

The before-allocation PCode connects the same observed Object to a LIS and ADDI
pair. The last value is copied to argument register r4. Actual coalescer parent
writes and the graph boundary then establish:

| Call statement line | High/address registers | Birth sequences | Address-result graph observation |
|---|---|---|---|
| 1427 | 217 / 218 | 358 / 359 | 218 merges into r4; invisible, degree 7 |
| 1429 | 219 / 220 | 360 / 361 | 220 merges into r4; invisible, degree 7 |
| 1432 | 225 / 226 | 366 / 367 | 226 merges into r4; invisible, degree 7 |

These are capture-local numbers, not source selectors. Line 1429 includes an
inlined helper: the line is the current caller statement, not a recovered original
leaf/inline source location. The high halves remain instruction-visible, with
degree 8. Both halves are inside the observed coalescing window.

Each invisible address-result node still has neighbors
`[1, 3, 31, 32, 53, 55, 186]`. Thus it remains adjacent to `noRota` (55) and the
zero-vector address (186). `noRota` has stored degree 159. The recorded graph
retains these nodes after their instructions disappear, rather than inferring
that the values were eliminated. The source-expression, birth, PCode-symbol and
confirmed-merge observations explain a concrete part of the original tooling gap.

## Bounded source experiment

The two direct accessor call sites were separately replaced by a small inline
forwarder with one of these parameter forms:

- `const CTevCombiners::CTevPass&`
- `const CTevCombiners::CTevPass*`
- `const CTevCombiners::CTevPass* const`

The reference version passes `CGraphics::EnvModulate()`; pointer versions pass
`&CGraphics::kEnvModulate` and dereference the parameter in the helper. Each helper
forwards the same const reference to `CGraphics::SetTevOp(kTS_Stage0, ...)`.
There are no CTevPass copies or changed includes. The accessor's exact pure return body
was checked in the retained header. This is a local rewrite justification, not an
unrestricted equivalence checker or a gameplay test.

All three variants are stock-verified. Native remap pairs all 1,631 pre-allocation
instructions. Every numbered GPR graph node, including all 355 instruction-invisible
nodes in that graph view, has identical degrees, flags, colors and ordered neighbor
records. The 6,668-byte RenderParticles instruction body is identical to baseline.
The actual `before_simplify` snapshots also agree for every node and visibility
observation: 555 GPR nodes (368 currently invisible) and 549 FPR nodes (114 invisible).
The earlier remap view uses before-allocation instructions and therefore has a
different visibility boundary; its 355-node count must not be substituted here.
All six `kEnvModulate` relocations are exact; all 166 function relocations remain.
Independent objdiff with `functionRelocDiffs=data_value` reports 100% against
baseline and 99.94901% against the retail target, retaining 17 differing rows.

Raw relocation-name multisets differ because inserting a helper shifts generated
literal/local-static suffixes, such as `@2573` and `skVtxDescList$1680`. That raw
name comparison is retained as false in the initial results; it was not silently
treated as semantic equality. The checked result uses independent objdiff evidence
and explicitly verifies the required global-symbol relocations.
The generic state fingerprint still retains these raw names and can miss this
equivalence; a checked normalization must preserve global/address-significant
identities rather than erase arbitrary symbol names.

## Retained evidence and next observation

Windows evidence is under `build/source-guided/dispatch-audit/particle-case/`:
baseline and repeated captures, objects, traces, typed provenance, private candidate
source, commands/hashes, complete remap JSON, objdiff JSON and
`checked-variant-results.json`. `retained-inputs/` holds both compiler scans and
raw dependencies/runtime files by hash. All 127 project header dependencies were
checked before and after each compile. The source/capture input SDK example is
[`capture_inputs.rs`](../crates/mwcc-oracle-core/examples/capture_inputs.rs).

Linux independently recompiles the same source and retained header closure under
`/home/henrique/.cache/mwcc-dispatch-particle-case/`; its stock object, dispatch
counts and births agree. Paths in compiler flags were translated to that private
project; source and header bytes were unchanged.

This experiment used native capture/remap and a closed set of justified source
edits. It is not a claimed harness winner: the current named-object objective
selector cannot reliably select these anonymous materializations. The next useful
observation is an explicit source-operand/argument-to-birth binding at the address
materializer, including qualified symbol identity. Then the shared evaluator can
select the mechanism across edits and reduction. Direct helper calls, exact inline
ancestry, optimizer-created values and semantic lifetime correspondence remain open.
The three wrapper forms tested here did not realize the desired graph change.
The const-pointer form adds two dispatch observations (4,082 versus 4,080) while
retaining the same births and graphs. Extra lowering work is not itself a useful
allocation change.

A fourth private experiment replaces only the first accessor argument with an
explicit pointer/`unsigned long`/pointer round trip. That round trip is justified
for this compiler's 32-bit target ABI only. It is also stock-verified, retains all
six required symbol relocations and all 166 function relocations, and leaves the
instruction bytes and complete numbered GPR remap graph unchanged. Independent
objdiff again reports 100% against baseline and 99.94901% against target. Its
capture, source and checks are under `integer-roundtrip-first/checked-result.json`.
This cast does not expose a useful alternative materialization path.
