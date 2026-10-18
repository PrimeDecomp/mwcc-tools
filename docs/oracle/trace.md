# Trace format, JSON and native API

Part of the [oracle documentation](../oracle.md).

Use `oracle compile-trace` to save an annotated capture and `oracle analyze-trace`
to read an existing trace. The [native API](#native-api) describes the JSON
envelope; the sections below define the underlying records.

## The raw trace

The oracle writes its trace to standard error, or to the file named by `MWCC_ORACLE_TRACE_FILE`. That file is appended
to, so delete it between runs. Set the variable to the null device when only the object is wanted. `--raw` prints the
trace lines; `--function` restricts them too.

Link names are the mangled ones; the compiler computes them if it has not yet.

### Always traced

| Lines | Content |
|---|---|
| `ra.before` / `ra.after function= link=` | Before and after each allocation, with `ra.block`, and `ra.i` per instruction (`line=`: the source line). |
| `ra.graph cls= k= start= count=`, `ra.node v= p= deg= flags= color= n=` | The interference graph before the simplification. |
| `ra.order` | The stack, first coloured first, with each node's spill cost. |
| `ra.select mask= after= ok=` | Each node's colour and flags. |
| `fr.begin` ... `fr.end` | The frame: `fr.local` / `fr.temp name= offset= size= reg= used=`, and `fr.layout linkage= outgoing= locals= above= size=`. |
| `inl.can`, `inl.begin` | An inline candidate and an expansion. `line=` is the source line of the call's statement, `link=` the callee. `inl.can` also has `chain=`: the inlined bodies the call is in, outermost first, separated by `\|`. `always=` is `#pragma always_inline`; `auto=` is `#pragma inline_bottom_up` (on by default; then the cost estimate decides), not `auto_inline`, which is not traced. |
| `inl.cost nest=`, `inl.end` | A cost estimate, and the end of an expansion. |

### Turned on by environment variables

Set each to `1`. `mwcc-tools oracle` sets them for the options that need them.

Development lifetime capture uses `MWCC_ORACLE_SOURCE_ARENAS` (`--source-arenas`)
and the compilation-wide `pv.arena.*` records. See [arena queries](source-arenas.md)
for allocation, reset and checkpoint semantics.

Experimental frontend capture uses `MWCC_ORACLE_SOURCE_NODES` (`--source-nodes`)
and `pv.source.*` records. It also enables arena capture. See
[source-node observations](source-nodes.md) for snapshot/write/clone queries and
validation status. Neither stream is accepted by graph fingerprint format 4 yet.

**`MWCC_ORACLE_BACKEND`** (`--backend`, current development Rust producer):
`pv.coalesce.begin`, `candidate`, `check`, `merge` and `end` record actual retail
coalescing paths. `pv.function.*`, `pv.value.birth`, `pv.counter.*` and
`pv.expression.*` add direct counter writes, function/creation identities, reuse
and available partial statement-root origins. `pv.value.object` adds guarded
single/pair object bindings and byte-preserving names. `pv.category.initial/window`
record completed range markers; graph headers sample their current bounds. Use `mwcc-tools provenance inspect
TRACE --json` for format-2 decisions/history and explicit coverage. `pv.graph.*`
pairs nodes with the instructions present immediately before graph simplification,
including a birth cutoff, current visibility, and per-function graph counts. See
[backend provenance](provenance.md); semantic categories, complete origins and
semantic lifetime correspondence remain incomplete.

V4 adds `pv.value.array` for guarded scalar-array promotion (original Object,
element bounds/index and complete known name/type) and `pv.value.call_result`
for six guarded call-result paths (original call ENode, return type, result part
and a partial output Operand). Call output bytes are explicitly incomplete at
birth; only their tag is initialized. Function scopes/totals provide separate
array and call-result coverage. Vector-array promotion and complete inline/source
lifetimes are not covered. Typed reports include the exact input trace SHA-256
and each observed coalescing pass's end position.

V5 adds `pv.scalar.begin/binding/gap/end` and independent function-end
scalarization totals. These record actual frontend aggregate-part temporary
creation and exact Object/VarInfo joins to backend births. Typed reports expose
`scalarization.passes` and `scalarization.backend_values`; absent or ambiguous
joins remain indeterminate. See [frontend scalarization](scalarization.md) for
the record scope, temporal checks and real promoted-flag example.

**`MWCC_ORACLE_SCHEDULE`** (`--schedule`):

| Lines | Content |
|---|---|
| `sc.block p= index= virtual=` | A block, with its `sc.i` lines. |
| `sc.node i= latency= height= latest= preds= succ=` | The dependency graph; `succ` as `pointer:latency`. |
| `sc.pick cycle= ready= pick=` | Each candidate as `pointer:earliest:latest:height:scheduled successors:check:rank`. |
| `sc.after order=` | The final order. |

**`MWCC_ORACLE_INLINE_COST`** (`--inline-cost`): `inl.expr nest= type= cost= what=`, in post-order. A call of a named
function also has `link=` and `flags=`, its function type's flags: 0x100 made by the compiler, 0x1000 a constructor,
0x2000 a destructor.

**`MWCC_ORACLE_FULL_COST`** (`--full-cost`):

* `inl.cost` gains `full=` (the whole cost) and `limit=` (1: a call at the inline depth limit).
* The `inl.expr` lines are those of the whole estimate. A call's line has `limit=1` at the depth limit, and `cand=` and
  `body=` (an inline candidate; its body is there).
* Only a body's statements have `inl.expr` lines; the compiler costs their operands without the hook. Each has
  `calls=`: the calls of inline functions in it, in the order they are costed, separated by `|`. A `!` before one marks
  the depth limit, a `?` no body yet. The other calls' bodies are the `inl.cost` lines one `nest` deeper before it, in
  that order.

**`MWCC_ORACLE_INSTANTIATION`** (`--instantiation`):

| Lines | Content |
|---|---|
| `tp.queue kind= pending= new= from= expanding= link=` | A body queued for parsing. |
| `tp.need result= ...` | The inliner asked for a body. |
| `tp.parse.begin`, `tp.parse kind= body= link=` | Around the parsing of a queued definition. |
| `tp.func` | A template function instance compiled. |
| `tp.emit caller=` | Marked to be emitted. `caller` is the return address in the compiler; `function_at(version, address)` names it. |
| `tp.gen link=` | Code generated from a saved body. |

**`MWCC_ORACLE_LOCALS`** (`--locals`). The lines of a scalarizer pass come before its `lc.pass`.

| Lines | Content |
|---|---|
| `lc.use node= kind= offset= type= size= var=` | Each use recorded. Kind 17 is a call's argument; offset -1 is the whole, or not known. |
| `lc.call node=` | A call that may reach an address-taken local. |
| `lc.temp name=` | The scalar made for each part not blocked, in the parts' order. |
| `lc.pass pointers= link=` | The end of a pass. |
| `lc.part blocked= noregister= bits= type= size= uses= var_size= var=` | Each part of the pass. |
| `lc.arg index= name= type= size= ref= dirty= mode= var= object= declared= declared_size= generated= argument= expression= link=` | Each formal of each call the inliner expands, before the function's `ra.before`. |

The `lc.arg` fields:

* `mode`: unused, substituted, reference or local.
* `var`: the fresh local. `object`: the object an aliased formal stands for, or the one a local formal points at.
* `declared`, `declared_size`: the formal's type kind and size as declared.
* `generated=1`: the callee was made by the compiler.
* `argument`: the argument's node kind, after commas, force-loads and casts. `expression`: the argument, in short form.
* `link`: the callee.

**`MWCC_ORACLE_BLOCKS`** (`--blocks`; `2` also prints each block's instructions, which `--blocks` always asks for):

| Lines | Content |
|---|---|
| `bk.stmt line= before=BLOCK:COUNT after=BLOCK:COUNT` | Each expression statement generated: its line, and the current block and its instruction count before and after. |
| `bk.list phase= link=` | The blocks: `raw` before the global optimizer, `optimized` after it, `merged` after each merge pass. |
| `bk.block p= index= count= flags= succ= pred=` | A block of the list, with its `bk.i` lines at level 2 (as `ra.i`). |
| `bk.merge prologue= link=` | A merge pass starts; `prologue=1` when it may merge the prologue block. |
| `bk.pair a= b= counts=A+B why=` | For each block and the next, as the pass starts, why it keeps them apart (`prologue`, `empty`, `call`, `branch`, `successors`, `predecessors`, `epilogue`, `flag`, `over100`), or `-`. |

**`MWCC_ORACLE_CSE`** (`--cse`; `2` also lists the linear IR as each pass starts, which `--cse` always asks for):

| Lines | Content |
|---|---|
| `ce.pass n= link=` ... `ce.end` | One IRO_CommonSubs call, the function's n-th. |
| `ce.l i= kind= op= a= b= line= expr= text=` | At level 2, each IR node: index, kind (1 operand, 2 one operand, 3 two operands...), ENode type, the operands' indices, source line, expression number, and an operand's text. |
| `ce.move expr= line= tree=` | A temporary about to be made for the expression at `expr`. `tree` is prefix form: `(TYPE#index operands)`. |
| `ce.temp name= path=` | The temporary made: `replace` (assigned in place), `comma` (where its uses meet), or `none` (over 64 uses). |
| `ce.replace at= with=` | A repeat replaced by the temporary of the expression at `with`. |
| `ce.object name= caller=` | Each temporary object made (create_temp_object), with the caller's return address. |
| `ce.vreg name= reg= class= noregister=` | Each local's register as the allocation starts (a virtual register number), before `ra.before`. |
| `st.codegen` / `st.optimized function= link=` ... `st.end` | With `MWCC_ORACLE_STAGES=1`, the PCode as code generation made it and as the global optimizer (COptimizer) left it, in the `ra.before` format (`st.block`, `st.i`). The peephole forward pass then gives the allocator's, `ra.before`. |
| `st.presched` / `st.precolor.gpr` / `st.precolor.fpr function= link=` ... `st.end` | With `MWCC_ORACLE_STAGES=1` too: the PCode as the first scheduling pass begins (a probe of gc2's sources: the WebAssembly and source-built oracles only), and as select colours a class (once a round). |
| `pp.copy at= line= tree=` | An assignment copy propagation forwards (IRO_CopyAndConstantPropagation): the left side's uses read the right side. |
| `pp.expression at= line= tree=` | An assignment expression propagation may forward into a single use in its block (IroPropagate_PropagateExpressions). |
| `pp.dead at= line= tree=` | A dead assignment removed (IROUseDef). Its tree is already gone: match `at` to an earlier `pp.copy` or `pp.expression`. |

**`MWCC_ORACLE_IRO_DUMP=FILE`** turns on the global optimizer's own dump (IroDump, normally written to `<source>.log`
in debug builds), appended to FILE. **`MWCC_ORACLE_IRO_PHASES=NAME`** adds the flowgraph after the phase NAME (as
`IRO_DumpAfterPhase` names it, such as `After IRO_Optimizer`), or after every phase with `all`; the compiler itself
asks for none (a probe of gc2's sources).

**`MWCC_ORACLE_NAMES`**: what a reader needs to name values and spell instructions as the compiler's listings do.
Records marked (gc2) come from probes of gc2's sources (the WebAssembly and source-built oracles only), and
`ra.temporaries` reads gc2's names (the WebAssembly oracle only).

| Lines | Content |
|---|---|
| `pc.opcode op= rank= name=` | An opcode's name and scheduling rank, before the first instruction record with it. |
| `pc.object p= name=` | After an instruction record, for each operand that refers to an object: its name. |
| `ra.temporaries cls= fe= first=` | After `ra.node`: the class's first front-end temporary and first temporary of the code generator. |
| `ra.value v= object= remat= argument= name=` | After it, each node's object (0: a temporary of the code generator), whether the value can be rematerialized, whether the object is an argument of the function, and its name (`-`: none). |
| `inl.decide caller= callee= candidate= pass= max=` | After `inl.can`: the plain names of the function being expanded and of the callee, whether the callee is an inline candidate, the expansion pass and the size limit. |
| `inl.expand function=` | (gc2) A function's body about to be expanded. |

**`MWCC_ORACLE_COPYPROP`** (gc2): the PCode copy propagation.

| Lines | Content |
|---|---|
| `cp.use reason= accepted=` | Whether the copy propagates into one of its uses, then the copy (`cp.copy`) and the use (`cp.into`). `reason`: the test that decided, 0 the use is a move, 1 it reads and writes the copied register, 2 the source is redefined between them, 3 the copy does not reach the use's block, 4 the source is redefined in that block before the use, 5 none failed. |
| `cp.remove removed=` | A copy every use took, then it (`cp.copy`): removed, or kept (an exception register, or a physical source in the less aggressive pass). |

**`MWCC_ORACLE_PEEPHOLE`** (gc2): the peephole pass after the allocation.

| Lines | Content |
|---|---|
| `ph.hit rule=` | A pattern (gc2's name) that changed an instruction, then it as it was (`ph.before`) and, unless the pattern removed it, as it is (`ph.after`). |
| `ph.dead` | A dead instruction about to be removed, then it (`ph.before`). |

## JSON

`--json` prints the whole report as one JSON document.

* **Registers**, `functions[].classes.GPR.vregs[]`: `vreg`, `class`, `phys`, `defs`, `uses`, `live_blocks`,
  `across_call`, `def` (`block`, `line`, `instruction`), `degree`, `neighbors` (`vreg`, `phys`), `state` (colored,
  coalesced, spilled, split), `order`, `pushed`, `select`.
* **Inlining**, `functions[].inlining[]`: `callee`, `link` (which tells overloads and template instances apart),
  `inline`, `cost`, `threshold`, `reason`, `count`, `depths`, `lines`, `chain`, and `outcome` (`inlined`, `kept`, or
  `limit`: kept by a call at the inline depth limit, known with `--full-cost`).
  * With `--full-cost`: `full_cost`, `depth_limited`, `limit_paths` and `sensitivity` (each with `path`, `kind`,
    `delta`, `flips` and `count`).
  * Every object has a `kind`: `decision`, `expansion` (a body expanded), `breakdown` (a node of a cost breakdown) or
    `near`.
  * With `--inline-cost`: `inlining[].breakdown`. Its nodes have `calls` (`[mark, link]`, with `!` at the depth limit
    and `?` no body yet) and, for a body, `of` (its callee's link name). A call statement also has `link`, `implicit`,
    `ctor`, `dtor` and `candidate`.
* **Frame:** `functions[].frame`.
* **Scheduling** (`--schedule`), `functions[].schedule[]`: `picks[]` (with `over[]` and `why`), `edges[]` (`from`,
  `to`, `latency`) and `before` (the order the scheduler was given).
* **Near** (`--near`), `near[]`: each with `margin` (the threshold minus the cost); with `--full-cost`, `full_cost` and
  `full_margin`.
* **Instantiation**, `instantiation[]`: `event` (queue, need, parse, func, emit, gen, decision, compiled), `link`,
  `position` (the trace line), and as available `queue_kind`, `new`, `from`, `expanding`, `body`, `result`, `caller`,
  `why`, `inline`, `depth`, `chain` and `in`.
* **No match** for `--function`: `similar[]`.

## Native API

Use the executable and versioned JSON for kit automation:

```text
mwcc-tools oracle compile-trace SOURCE --version GC_1_3_2 -- COMPILER_FLAGS
mwcc-tools oracle analyze-trace saved.trace --version GC_1_3_2
mwcc-tools oracle regdiff --project PROJECT --unit UNIT --function FUNCTION --json
```

`compile-trace` returns a format-1 envelope with `compilation` and the schema-6
`report`. Check `compilation.check_status.status`: `verified` means full-object
equality with the stock compiler; skipped/unavailable checks are not successes.
Keep diagnostics on stderr and handle command failures. The plain `oracle --json`
command returns the schema-6 report directly. Regdiff has its own format-1 document
and structured register explanations; do not parse human report paragraphs.

See the [native kit guide](../../integrations/decomp-kits/overlay/.agents/docs/oracle-native.md)
for project units, candidates, explicit check states, saved graphs and calibration.
Rust callers can use the workspace source API in `mwcc-oracle-core`, including
`trace`, `document`, `allocation`, `colouring`, `runner`, `provenance`,
`value_history`, `graph_history`, `allocation_category`, `scalarization` and `backend_audit`.

The Python implementations in Git history are historical behavior references. Their
imports, function signatures and rendered text are not supported production APIs.
