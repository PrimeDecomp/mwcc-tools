# Real case: symbolic EnvModulate arguments

The native harness now finds and independently verifies a source edit that reduces
the observed symbolic-address pairs from three to two and changes the relevant
graph. The final RenderParticles match gets worse. The edit is retained as an
isolated compiler experiment and has not been applied to the game checkout.

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

The initial experiment used native capture/remap and a closed set of justified
source edits. Its named-object selector could not select the anonymous
materializations. The subsequent operand binding and scoped objective below now
cover this mechanism. Direct helper calls, exact argument/inline ancestry,
optimizer-created values and semantic lifetime correspondence remain open.
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

## Direct operand binding and native harness result

The subsequent V3 counter observer reads the actual source Operand and Object at
each of the two Normalize birth sites. It records the cached qualified symbol and
high/low role without invoking the mangler. The retail cache field differs in GC
2.7; its accessor bytes establish offset 54, versus 56 in GC 1.3/1.3.2. The initial
incorrect-offset producer's failed full-case run is retained separately and is not
counted as a successful runtime proof.

Corrected Windows and Linux full-unit captures again produce the complete stock
object above. RenderParticles has 32 address-half bindings, all with complete
cached names. Six are `kEnvModulate__9CGraphics`, forming the same three pairs.
Their input Operand addresses and active contents match the most recent outputs
of observed expressions 313, 318 and 332 respectively. That is a state observation,
not proof of alias-free dataflow. The qualified symbol is read at the birth itself.

The native `symbol_address_pairs` evaluator now selects these pairs by qualified
symbol, with complete counter/materializer coverage and unique graph bindings.
An actual `source-search run` asks for at most two pairs and tests the first-call
pointer/integer round trip. Baseline calibration and every hard constraint pass;
both baseline and candidate have three pairs, so both predicates are **unsatisfied**.
The run ends `bounded_search_exhausted` after three fresh compiles. It does not
claim a useful transformation or a semantics-checked winner.

The run retains all six required global-symbol relocations. A future reduction in
repeated materialization must instead require at least one of each needed high/low
relocation kind; requiring all six would prevent a valid reduction to fewer pairs.
The native target diagnostic retains 17 differing bytes and unequal raw relocation
names; it is separate from the earlier objdiff row/percentage comparison.

This first run could not group relevant states because PCode operand kind 1 at
paired-single opcodes 403 and 407 was unsupported. It retained `fingerprint_error`
explicitly. The fixed-quantizer support described below closes that particular
gap; generated local-symbol suffixes remain a separate grouping limitation.

New Windows evidence is under `build/source-guided/materializer-audit/`:
`particle-case-verified/{capture,provenance,verification}.json`, the baseline object
and raw trace, and `particle-search/` with native plan, retained input closure,
calibration, three compilation artifacts and both evaluations. Linux evidence is
under `/home/henrique/.cache/mwcc-materializer-audit/particle-case/`. The new
`tests/fixtures/backend/symbolic_address.cpp` isolates the same const-reference
argument mechanism on all three versions, with complete symbol bindings and two
confirmed merges in each capture.

## A source edit that changes the graph

A later native search tests a shared local pointer. At the start of RenderParticles
it introduces:

```cpp
const CTevCombiners::CTevPass* const oracleEnvPass = &CGraphics::kEnvModulate;
```

The two direct `SetTevOp(kTS_Stage0, CGraphics::EnvModulate())` calls instead pass
`*oracleEnvPass`. The third use inside the inlined `SetParticleTevScale(scale)` is
unchanged. There are no fixed addresses, assembly, header edits or CTevPass copies.
The winning source SHA-256 is
`fa0fdcb8b823d19542741af0f38756abd154948fa16f10c3218143104a3fd55c`.

The plan selects `symbol_address_pairs` for `kEnvModulate__9CGraphics`, with a
maximum of two. It requires at least one kind-6 HA and one kind-4 LO relocation,
each with addend zero. Its `source_transformation` checker accepts only exact,
closed rewrites of the pinned baseline and checks the retained pure accessor body
and relevant header hashes. For this rewrite, a const local pointer denotes the
same global address and each dereference supplies the original const reference.
The global itself is mutable; this rewrite caches its address, not its contents.
This is a bounded source justification, not unrestricted equivalence or gameplay
testing. Two array recipes are also present in the plan, but the search stops at
the pointer winner; those array variants were not compiled or tested.

The run reports `verified_success` after four fresh builds: private baseline,
original-context calibration, candidate and independent candidate verification.
All hard constraints and configured source checks pass. The complete candidate
stock object SHA-256 is
`5a78df424169a668ec258ff0cb0b2f12c379e736fd24094131db40253475d50b`.
An additional independent Linux compile produces the same object and agrees with
both Windows captures on every numbered graph observation and stable
materialization binding.

| Observation | Baseline | Shared pointer |
|---|---:|---:|
| Observed EnvModulate Normalize pairs | 3 | 2 |
| EnvModulate address-half nodes adjacent to `noRota` | 6 | 2 |
| Stored degree of uniquely anchored `noRota` | 159 | 156 |
| Selected GPR graph nodes | 555 | 554 |
| Function birth observations | 1,104 | 1,103 |
| Function dispatch observations | 4,080 | 4,083 |

One address result remains a virtual root of degree 4, without a `noRota` edge;
the other merges into r4 and remains an invisible degree-7 node. This establishes
a useful graph change from a concrete C++ declaration, while retaining symbolic
relocations. It does not establish complete semantic lifetime correspondence or
absence of every possible address temporary.

The final function grows from 6,668 to 6,672 bytes. Independent objdiff 3.7 with
`functionRelocDiffs=data_value` reports **99.491905%** against retail, compared with
the baseline's **99.94901%**. Graph-objective satisfaction and final matching are
separate results; this is not a RenderParticles matching fix.

## Retained-state normalization and reproduction

The native state normalizer now recognizes only the observed fixed GQR operand
form at PSQ_L/PSQ_ST (opcodes 403/407). It retains all twelve bytes, including
hardware IDs 912 through 919, rather than renaming those IDs as virtual registers.
Different selectors remain different states. Other kind-1 representations remain
unsupported. A fixture covers all eight selectors at both opcodes on all three
compiler versions, with stock equality and probe off/on checks on both hosts.

`source-search state CAPTURE --object OBJECT --link RenderParticles__11CElementGenFv`
inspects a retained capture without recompiling. It validates stock-check status,
object hash and selected graph coverage before reporting a fingerprint. Unsupported
normalization returns an explicit indeterminate evaluation with no fingerprint;
invalid captures or selectors fail. This inspection does not recheck dependencies
or rerun the source checker.

Baseline fingerprint:
`92eb228a7a1a27e07e5af4f23e862fe21cd860bd798e90977b780c4a98ac2ecc`.
The winning Windows, fresh Windows verification and independent Linux captures
share fingerprint
`ae34770e49693ba5896c9fac25f9de5d141ac05869970d9f92645866089c3246`.

Windows evidence is under `build/source-guided/address-reuse-audit/`: the frozen
analyzer, plan and closed checker; `closed-variants/named-pointer/CElementGen.cpp`;
`search/` with input closure, calibration, candidate and fresh verification;
`winner-evidence.json`; `cross-host-verification.json`; retained-state reports;
and independent objdiff reports. The original search used the analyzer before
GQR normalization support, so its unavailable-grouping diagnostic is preserved.
The later state reports use the current analyzer and do not replace saved outcomes.
Linux evidence is under `/home/henrique/.cache/mwcc-address-reuse-audit/`, including
private source/header bytes, fresh capture/object, state and graph checks.
