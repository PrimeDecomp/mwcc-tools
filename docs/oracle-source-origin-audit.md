# Source write and initializer provenance audit

This research record identifies the observations needed to connect original C++
writes and initializers to backend values. It began after `f164a24`; its captures
describe that historical scope. Current development adds a bounded
[source-node API](oracle/source-nodes.md), with native validation still pending.
The complete source-to-backend bridge remains open.

Related references: [inline/write emission](oracle-inline-clone-audit.md),
[IRO transformations](oracle-iro-origin-audit.md) and
[arena lifetimes](oracle/source-arenas.md).

## What retained captures establish

The reproducible inspection script is `build/source-guided/source-origin-audit/audit.py`. Run it with Python from this checkout. It only reads the existing compilation envelopes and writes `retained-capture-summary.json` beside itself. That summary records each envelope, source, object, and raw trace SHA-256; stock verification status; selected function; raw event positions; target scalar binding; birth contexts; and relevant `ce.l` records. It covers the full V5 Render case, nine tiny V5 cases across GC 1.3/1.3.2/2.7, and the earlier clean CElementGen compilation.

The full V5 Render capture is `build/source-guided/scalar-origin-audit/windows-render/enabled.json`, SHA-256 `7885697f4010477ef12ca6aae0966a21c75376c12dbac0bc2d3069525c8426e8`. It is a stock-verified modified source experiment. **Its RenderParticles function is not an exact retail match.** Likewise, the tiny examples establish compiler mechanisms, not a match to a retail target. The older `materializer-audit/particle-case-verified/capture.json` is the clean source baseline; its RenderParticles is also not claimed matched.

| Evidence | Observed result |
| --- | --- |
| V5 full Render, function 54, live birth `54:209`, GPR162 | `expression=0 phase=0 statement=00000000 line=-1` |
| Its exact frontend scalar binding | Pass107/binding1; source `noRota`, Object`01ac4fd0`, VarInfo`01afc710`; whole element byte4..7/bit32..63; temporary Object`01a35a18`; `source_linear=01d7c980 source_index=226 source_kind=2` |
| Later Render CSE snapshots with index226 | First `kind=1 text=60 line=1397`; a later snapshot has `kind=2 op=4 a=225 line=1396` |
| Six tiny ghost cases, both source forms on all three versions | One exact whole-element frontend binding for `flag[1]`; live GPR35 birth has expression0/null statement/line-1 |
| Tiny inline-helper provenance | `lc.arg` describes substituted flag/condition, but the entire capture has **zero** `inl.begin`/`inl.end` records; inlining took the statement-expansion route |
| Three array-control `second_promoted_flag` functions | No frontend scalar binding; backend promotion alone remains a different mechanism |
| V5 full Render births | Phase0:1106 births,304 have expression0/null statement,766 link to codegen dispatch,36 to statement-root context; phase1:2 births, both expression0/null statement |
| Clean baseline Render births | Phase0:1104 births,302 expression0/null statement;760 dispatch contexts and42 statement-root contexts |

`ce.l` has no raw linear pointer or lifetime identifier and its indices are reused/reassigned between snapshots. The records above cannot be joined to the earlier scalar binding by index, line, or spelling. In particular, they do not establish that the same raw linear pointer changed kind. Neither those rows nor the allocation of the scalar temporary tells us which original source write produced its first live definition.

The full capture has hundreds of dispatch/statement roots of kind30 (`EASS`), but the bounded expression text is not a complete ENodes graph. No `noRota` spelling appears in the selected full Render expression text after lowering; the tiny live scalar appears as anonymous `@21`. Absence of a name in that text is not absence of an originating object.

## SourceWrite and DeclarationInitializer are different roles

The tiny ghost sources declare `unsigned flag[2];` without an initializer, then execute `flag[1] = condition ? 0u : 1u` directly or through an inline helper. The full ghost source similarly declares an array and later assigns `noRota[1]`. These writes must be called **source assignments**, even when they serve the programmer's initial setup. The later conditional assignment of1 is another source assignment. Source-level declaration initialization is a separate compiler-observed role.

`CFunc.c:1489` creates a local Object in `declare_local_object`; the automatic, non-parameter path at1652 calls `CInit_InitializeAutoData(object, append_or_defer_expression_statement, register_destructor_object)`. `CInit.c:2536` has the authoritative target Object and distinguishes explicit `=`/C++`(` from missing initializers. It emits assignment/aggregate-construction trees through its callback. Class default construction can occur without explicit initializer syntax, and aggregate initialization can be a constant-object copy plus runtime member assignments. An `EASS` shape, Object name, or line therefore cannot classify a declaration initializer or a unique source subexpression by itself.

There is an attribution trap: `append_or_defer_expression_statement` (`CFunc.c:1447`) can emit a **previous** deferred expression while called for the current initializer, then defer the current input. Applying the active initializer scope to every appended statement would give the previous expression the wrong target. Record the callback's exact input root and its initializer identity; associate any eventual statement through that root's observed lifetime, not merely through the active callback scope.

Declaration parsing and inline-body registration can precede backend `pv.function.begin`. Future source origins need compilation-local identities and the owning frontend function Object, followed by a checked join to the backend function. They must not inherit whichever backend function ID happens to be current.

## Existing observers and exact missing edges

* `lifecycle.rs` records nested, balanced `codegen_dispatch` scopes with exact current ENode pointer, kind, retail target, arguments, and tagged output operand. Its spelling is deliberately bounded and always `complete=0`. A dispatch is actual backend execution context, not an original-source expression identity or statement-wide data-flow proof.
* `statement_origin` first returns any active explicit expression, then rejects fallback when `phase != 0`, the statement is null, or its tag has no inspected ENode payload. Thus optimizer/allocation births without active dispatch already get expression0. However, raw birth `statement`/`line` are copied unconditionally from `gCurrentStatement`, and the host Birth currently omits the raw statement field. Those raw fields can be stale outside codegen; they must never become causal origin evidence. The explicit-expression branch precedes the phase check, so a future unexpected cross-phase active scope needs a coverage gap, not an inferred source role.
* `frontend.rs` sees the original call ENode and returned replacement in the call-expression expansion wrapper, but emits names/depth/result booleans rather than those identities. The separate statement expansion path is crucial for the tiny helper.
* `origins.rs` sees both `inline_statement` and `generate_inline_statements` entry/return, but its legacy table tracks only call-reference ENode pointers and callee link chains. It has fixed capacity, silent depth truncation, no per-node lifetime identities, and no formal/actual or copied leaf mapping. Its chains cannot certify a complete leaf ancestry.
* `bindings.rs` sees formal and actual argument data, but `lc.arg` retains names and bounded text rather than exact expansion/Object/ENode identities. A formal named `flag` cannot be joined to a caller array by its name.
* `CInline.c:1803`, `fn_00513240(ENode*)`, is a concrete recursive clone/substitution boundary. It allocates a new ENode, traverses children, and returns replacements. Registration changes Object refs into `EARGOBJ`/`ELOCOBJ`; expansion substitutes them back or copies an actual argument expression. It is used by both expression and statement expansion. Recording each actual invocation's input and output, with its evaluation mode and active expansion, would preserve leaf-copy edges without guessing a structural correspondence between unrelated tree snapshots. Input must be snapshotted before retail executes because alias normalization can mutate it. A returned replacement is a transformation edge, not necessarily an equivalent same-role node.
* `IrOptimizer.c:1765`, `linearize_expression`, returns the exact new IROLinear for an input ENode. Operand kind1 retains an ENode pointer; operators become IRO child edges and lose their original ENode pointer. `new_linear` assigns `current_statement`. Before that, `lower_expression_to_statements` recursively mutates/splits ENode trees and creates statements; a root-only linearization observer would miss those transformations.
* `IroVars.c:1258`, `record_var_part_use`, records an access, not a definition. Its first matching access becomes `part.source`; further matching accesses go into `part.uses`, and scalarization replaces those uses. The existing guarded temporary-return observer can see the part before this replacement loop, but only records its first source pointer/index/kind and target temporary. A new source bridge must preserve all participating uses and their parent/write roles, not relabel `part.source` as an initializer.
* `IrOptimizer.c:2269`, `fn_0042eb40(IROLinear*)`, reconstructs an ENode for every recursive IRO operation, including fresh leaf copies. `convert_linear_to_statements` puts those results into new statements before backend dispatch. This is the concrete reverse bridge needed to attach an eventual dispatch to its observed IRO origin. Source lines are copied independently and do not substitute for that edge.

## Minimum guarded bridge proposal

The following describes the complete target contract. The experimental
[source-node API](oracle/source-nodes.md) implements selected snapshots and actual
clone-call observations; the remaining edges and the APIs below are proposals:

1. **Source snapshots and write roles.** At a guarded frontend statement emission or pre-inline body-registration boundary, record the complete supported tagged ENode graph, its exact owning function Object and statement/body slot. Assign `SourceWrite` only to an observed assignment node with explicit LHS/RHS edges. Record direct Object/constant-byte-interval targets only when an audited LHS recognizer proves them; otherwise the target remains unknown. Add `DeclarationInitializer` only through a matched `CInit_InitializeAutoData` target scope and its exact callback input root. Keep explicit syntax/default construction separate. Record declaration token file/offset only when its compiler token identity can be bound to the retained source closure; line is display data.
2. **Actual copy/lowering edges.** Instrument each `fn_00513240` invocation, both inline expansion routes, and formal/actual substitution identity. Capture input/output node observations, evaluation mode, callee/body slot and parent invocation. For lowering, record recursive input scope and actual generated statements/rewritten nodes; a transformation that has not been observed remains a gap. Do not zip equal-looking trees or treat the outer call's replacement root as complete leaf ancestry.
3. **ENode to IRO and scalar-use edges.** At `linearize_expression` entry/return record exact input observation, produced linear, child roles, current statement, function, and construction/pass identity. At the existing guarded scalar temporary-return sites, record complete part/source/use-list snapshots **before** replacement. To assert that a particular write becomes a temporary write, also observe the actual use replacement or its verified post-state. Join a scalarized source write through its exact assigned/access IRO node and unique whole-element binding; the representative `part.source` alone is insufficient.
4. **IRO to backend ENode.** At `fn_0042eb40` entry/return record exact IRO observation and generated ENode, including recursive children. Existing codegen dispatch/result then supplies the actual backend ENode/operand context. Named-local register allocation may happen before any dispatch, so source lineage belongs to the Object/scalar binding and actual defining instruction, rather than being fabricated on that allocation birth.

Suggested native surface:

```rust,ignore
enum SourceRole {
    SourceWrite { operation: AssignmentOperation, lhs: NodeId, rhs: NodeId },
    DeclarationInitializer { target: ObjectId, syntax: InitializerSyntax, emission: u32 },
}
enum OriginEdgeKind {
    InlineCloneOrSubstitution, Lowering, Linearization,
    ScalarUseReplacement, Reconstruction,
}
// Exact capture-bound observations and explicit edges; multiple origins survive.
fn source_writes_for_scalar_binding(history: &History, binding: BindingId)
    -> Evidence<Vec<SourceWritePath>>;
fn source_origins_for_backend_expression(history: &History, expression: ExpressionId)
    -> Evidence<Vec<SourceOriginPath>>;
```

Every ID is compilation-local and includes an observed construction/pass/arena lifetime where relevant. Reused raw pointers, reused indices, or identical names are never identity. An untracked pre-existing node or unobserved in-place rewrite cannot acquire a proven lifetime merely because its current bytes equal an older snapshot. Until guarded construction/arena-reset coverage resolves that ambiguity, the corresponding path stays indeterminate. A cheaper first increment can expose exact scalar-use/Statement/ENode snapshots as **stage-local context**; it must not claim the complete original source bridge above.

Each supported producer path needs guarded instruction/body fingerprints and ABI layouts on all three versions, balanced invocation/end records, independent observed totals, function/owner checks, and explicit gaps for unsupported node tags, list cycles, capacity/depth limits, absent callbacks, or pointer-lifetime ambiguity. Source and inline histories can precede backend functions, so retain them at compilation scope. Later retail/split audits resolve the optimizer entries below. On GC1.3.2/2.7 some are private entries inside coarse split containers, each with its own ABI and return; they are not fused runtime functions. Actual entry points are:

| Candidate | GC1.3 | GC1.3.2 | GC2.7 |
| --- | --- | --- | --- |
| CInit_InitializeAutoData | `005099e0` | `0050a4c0` | `0050b240` |
| append_or_defer_expression_statement | `004f33e0` | `004f3e90` | `004f4d50` |
| fn_00513240 clone/substitute | `0055f360` | `0055fec0` | `00561dd0` |
| linearize_expression | `0046c990` | `0046cc30` | `0046d850` |
| lower_expression_to_statements | `0046cef0` | `0046d190` | `0046ddb0` |
| fn_0042eb40 reconstruction | `0046be00` | `0046c0a0` | `0046ccc0` |

Validation should include explicit scalar/aggregate declaration initializers versus later assignments; no initializer versus implicit construction; deferred callback flush; both inline routes; two invocations of the same helper with different actual arrays; nested inline formal substitution; shadowed names; in-place ENode rewrites; pass renumbering; arena/pointer reuse; an unobserved origin remaining indeterminate; truncated trees/lists; and all original-vs-instrumented object/relocation comparisons. The existing full and tiny captures are controls for present gaps, not positive evidence for the proposed new roles.

Even a complete path proves where a compiler value came from and which source role emitted it. It does not prove runtime dominance, that a write executes, that it is the first write along every path, or that a source lifetime matches a machine live range. A later initializer objective must state its separate CFG/execution requirements; source role cannot silently broaden the current conservative initializer recognizer.
