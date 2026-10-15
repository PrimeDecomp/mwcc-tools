# Trace format, JSON and Python API

Part of the [oracle documentation](../oracle.md).

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
| `inl.can`, `inl.begin` | An inline candidate and an expansion. `line=` is the source line of the call's statement, `link=` the callee. `inl.can` also has `chain=`: the inlined bodies the call is in, outermost first, separated by `\|`. |
| `inl.cost nest=`, `inl.end` | A cost estimate, and the end of an expansion. |

### Turned on by environment variables

Set each to `1`. `tools/oracle.py` sets them for the options that need them.

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
| `pp.copy at= line= tree=` | An assignment copy propagation forwards (IRO_CopyAndConstantPropagation): the left side's uses read the right side. |
| `pp.expression at= line= tree=` | An assignment expression propagation may forward into a single use in its block (IroPropagate_PropagateExpressions). |
| `pp.dead at= line= tree=` | A dead assignment removed (IROUseDef). Its tree is already gone: match `at` to an earlier `pp.copy` or `pp.expression`. |

**`MWCC_ORACLE_IRO_DUMP=FILE`** turns on the global optimizer's own dump (IroDump, normally written to `<source>.log`
in debug builds), appended to FILE.

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

## Python API

`tools/oracle.py` can be imported.

| Function | Does |
|---|---|
| `analyze_source(source, flags=(), from_ninja=None, version=None, overrides=(), function=None, link=None, schedule=False, inline_cost=False, check=False, near=None, full_cost=False, instantiation=False)` | Compiles and returns the document `--json` prints. `overrides` as `--override` takes them, `near` as `--near`. Raises `OracleError` when it cannot compile. |
| `compile_trace(...)` | The same arguments; only compiles and returns the trace. |
| `analyze_file(trace, function=None, link=None, version="GC_1_3_2")` | Takes a trace file (or its lines) and returns one dict per function, as `--json` prints them. `function` is a substring of the name or link name, `link` the exact link name. |
| `explain_exchange(analysis, a, b)`, `similar_functions(...)` | The `--exchange` and suggestion views. |
| `parse_trace(lines)` | The snapshots before and after each allocation; the before snapshot also holds the colouring rounds and the frame. |
| `analyze(...)` | The data `--json` prints. |
| `opcode_names(version)` | The opcode names, for views of your own. |
| `cse_trace(lines)`, `cse_tree(text, types)` | The `ce.*` and `pp.*` lines by link name (`compile_trace(..., cse=True)`), and a `ce.move` tree in infix form. |
| `block_trace(lines)`, `block_cuts(record, names)` | The `bk.*` lines by link name (`compile_trace(..., blocks=True)`), and where each function's statements moved to a new block, and why. |

Each function's dict has `schema_version` (`SCHEMA_VERSION`), raised when a field changes meaning or goes away.
