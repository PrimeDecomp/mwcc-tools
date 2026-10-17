# Oracle tools

The oracle is the original CodeWarrior compiler relinked with Rust trace hooks.
`mwcc-tools` uses its trace to explain register allocation, inlining, scheduling
and stack layout. Supported oracle versions are `GC_1_3`, `GC_1_3_2`, `GC_2_6` and `GC_2_7`.

Compilation checks compare the oracle's object with the stock compiler's object.
Use a verified result before relying on the trace to explain your build.

## Choose a tool

| Command | Use it to |
|---|---|
| `oracle` | Compile a source and explain registers, inlining and stack layout. |
| `oracle run-unit` | Run the oracle with a decomp project's unit flags. |
| `oracle regdiff` | Explain a function's register differences from its target. |
| `oracle compile-trace` | Save raw trace, analysis, hashes and an explicit stock check in one JSON document. |
| `oracle analyze-trace` | Analyze a saved trace without running a compiler. |
| `colouring` | Replay allocation or search for graph changes that produce target registers. |
| `inline-diff` / `inline-window` | Compare inline decisions or find compatible inline-limit ranges. |
| `inline-calibrate` | Measure inline thresholds for a project's callee. |
| `data-diff` | Compare a translation unit's data layout with target assembly. |
| `header-impact` | Find functions affected by a header edit. |
| `corpus` | Check oracle/stock equality over many inputs. |

Development tools add actual coalescing explanations, graph-scored C++ search,
reduction and matched-source queries. See [their status and guides](oracle-source-guided-status.md).

## Build

```text
cargo build --release --locked -p mwcc-tools
rustup target add i686-pc-windows-msvc --toolchain 1.96.0
mwcc-tools configure --version GC_1_3_2 --oracle
ninja build/GC_1_3_2/oracle/mwcceppc.exe
```

Add `target/release` to PATH, or use the executable's full path. On Windows its
name is `mwcc-tools.exe`. Repeat configure/build for another version; configure
writes one version's Ninja graph at a time.

Rust hooks are the default. `configure --oracle --producer c` selects the retained
C rollback. The original compiler and runtime are downloaded during the build.
Avoid `ninja -t clean` on the oracle target: it removes downloaded compilers.

Set `MWCC_ORACLE_ROOT` to the built checkout when running tools elsewhere.
Linux/WSL and macOS run the compiler through wibo 1.2.0; `MWCC_WIBO` selects a loader,
otherwise the runner checks `build/tools/wibo`, then PATH. Windows runs it directly.
The pinned macOS wibo is Intel-only and needs Rosetta 2 on Apple Silicon.

For a decomp kit, follow the [native kit workflow](../integrations/decomp-kits/overlay/.agents/docs/oracle-native.md).

### GC 2.7 without the original compiler

For GC 2.6 and 2.7, the default build makes the oracle as WebAssembly, `build/<version>/wasm/oracle.wasm`, and
`mwcc-tools` runs it in process wherever it would run `build/<version>/oracle/mwcceppc.exe` (see
[wasm.md](wasm.md#the-oracle)). `MWCC_IN_PROCESS=0` runs the executable instead.

The normal GC 2.7 configure (the vendored `gc2/` tree, without `--oracle`) can also build an x86 oracle whose compiler
is made from the GC 2.x sources alone. It is opt-in and not built by default:

```text
mwcc-tools configure --version GC_2_7
ninja build/GC_2_7/gc2/source-oracle/mwcceppc.exe
mwcc-tools corpus --version GC_2_7 --canaries DIR --all-traces --exe build/GC_2_7/gc2/source-oracle/mwcceppc.exe
```

`mwcc-tools source-only GC_2_7 --oracle` (the target's command) links the source-only compiler (every compiled
`gc2` object; see [gc27-differences.md](gc27-differences.md)) with the three Rust objects. Neither the original
`mwcceppc.exe` nor its split is read. The Metrowerks toolchain is still downloaded: the compilers, `mwld` and
`gc2/lib`'s runtime and MSL.

The hooks name what they wrap and read by `config/GC_2_7/symbols.txt`. The link takes each name to the `gc2`
symbol at the same address (`gc2/config/GC_2_7/symbols.txt`):

* A hooked function is renamed `<hook>__retail` and made global. The calls to it, from its own object and from
  the others, go to the wrapper `<hook>`. Calls from inside its own body stay retail, as in the oracle.
  * The 46 hooks are `fn_00507070` = `simplifygraph`, `COptimizer_Optimize` = `globallyoptimizepcode`, and so on.
  * The link prints the whole table and writes it to `source-oracle/hooks.json`.
* A function or variable the hooks read gets the oracle's name as a second, global name in its object. Where
  `gc2` already uses that name for something else, it gets `<name>__oracle` instead. This happens for
  `CMangler_GetLinkName`, which is `gc2`'s `CMangler_FunctionLinkName`.
* MWCC addresses a static variable through its section, so it has no symbol. Its place is its offset in its unit's
  section, from `gc2/config/GC_2_7/splits.txt`. The link checks that the object's section has the original's size
  and that the object refers to that offset. Examples are `data_00582468` = `CInline.c`'s `cinline_level`, and
  `heap_pool`/`data_0057fdd8` in `CompilerTools.c`.
* A common block (`copts`, `pclastblock`, ...) has no section yet. The Rust objects' references to it are renamed
  instead, with the offset added: `iro_pointer_analysis` is `copts+0xf8`.

Only the hooks are linked. The guarded probes that the oracle installs into the original's split objects are not
(dispatch, call arguments, arenas, source nodes, counters, coalescing), because they are pinned to that split's
bytes and the original's image. (The WebAssembly oracle has the arena and source node streams from calls in gc2's
sources, under `ORACLE_PROBES`: [wasm.md](wasm.md#probes). This executable is built without them.) So:

* The provenance streams are not supported on this executable: `--backend` explanations, `--source-nodes`, and every
  `pv.*` record of the probes (counters, expression dispatch, argument observations, coalescing).
  * The counter probe also gives the hooks their function context (`pv.function.begin`). Without it, the graph
    wrapper writes `pv.gap reason=graph_context` instead of `pv.graph.*`. The scalarizer writes `pv.scalar.gap
    reason=scalar_function_context`.
  * The source-node stream is written but incomplete.
  * The hooks' own records (`ra.*`, `fr.*`, `sc.*`, `lc.*`, `ce.*`, `tp.*`, `bk.*`, ...) are complete.
* `OracleRust_ArenaGlobals` is a zero stand-in. The `--source-arenas` stream reports `invalid_globals` and
  `unsupported=1`, with resets but no allocations. `OracleRust_ArgumentForceGprRetail` is a `ud2` stand-in that
  only an installed probe would call.
* Two traced fields are return addresses into the compiler: `ce.object caller=` and `tp.emit caller=`. They are
  this executable's addresses. Names that a report resolves through `config/GC_2_7/symbols.txt` are wrong for them.
  No tool reads `source-oracle/mwcceppc.map` yet, so look callers up there by hand.

Check it against the oracle with `corpus --trace-reference`. It runs a second oracle with the same streams and
compares the two traces. The `pv.*` records are left out, and addresses are masked: the image and heap differ
between two links. The masked values are hex words, 7- to 9-digit decimals (`st.block weight=` is not set yet and
holds a stale pointer), `caller=`, and `ops=` operands that hold a pointer.

```text
mwcc-tools corpus --version GC_2_7 --canaries DIR --all-traces \
  --exe build/GC_2_7/gc2/source-oracle/mwcceppc.exe --trace-reference build/GC_2_7/oracle/mwcceppc.exe
```

On the 2313 mwcc-rs canaries:

* every object, message and exit code is identical to the original's;
* every trace is identical to today's oracle's (2313/2313).

The hook points in units that do not match yet are listed below. The byte comparison is against the original's
split, with relocations masked. Every other hook is in a matching unit, and its function's bytes are identical.

| Hook | `gc2` function | Unit | Function bytes |
|---|---|---|---|
| `setup_inline_locals_and_arguments` | `CInline_SetupArgsExpression` | `CInline.c` | identical |
| `fn_00560c80` | `CInline_InlineFunctionCheck` | `CInline.c` | identical start; the oracle's symbol (0x19d) also covers the next `gc2` function (0x118) |
| `fn_0055e540` | `CInline_EstimateSizeOfFunc` | `CInline.c` | identical |
| `fn_00561260` | `CInline_InlineFunctionExpression` | `CInline.c` | identical |
| `fn_0055e600` | `CInline_EstimateSizeOfExpr` | `CInline.c` | 48 of 495 bytes differ |
| `inline_statement` | `CInline_ExpandStatement` | `CInline.c` | identical |
| `generate_inline_statements` | `CInline_ExpandStatements` | `CInline.c` | identical |
| `fn_0055c730` | `CInline_GenerateDeferredFuncs` | `CInline.c` | identical |
| `CInline_AddFunctionPrecNode` | `CInline_AddInlineFunctionAction` | `CInline.c` | identical |
| `CInline_AddSpecialization` | `CInline_AddTemplateFunctionAction` | `CInline.c` | identical |
| `generate_inline_code` | `CInline_GenIFunctionCode` | `CInline.c` | identical |
| `fn_0050f240` | `CInline_ObjectAddrRef` | `CInline.c` | 4 of 340 bytes differ |
| `fn_00513040` | `CInline_CopyExpression` | `CInline.c` | identical |
| `fn_00513240` | `CInline_CopyNodes` | `CInline.c` | 88 of 1682 bytes differ |
| `create_temp_object` | `create_temp_object` | `CFunc.c` | identical |
| `CFunc_ParseFuncDef` | `CFunc_ParseFuncDef` | `CFunc.c` | identical |
| `fn_004f7fb0` | `compute_frame_sizes` | `StackFrame.c` | identical (the frame wrapper's operand reads hold) |
| `Peephole_MergeAdjacentBlocks` | `peepholemergeblocks` | `Peephole.c` | identical |
| `FunctionCalls_GenerateCall` | `call_function` | `FunctionCalls.c` | identical |
| `assign_argument_locations` | `analyze_arguments` | `FunctionCalls.c` | 33 of 1551 bytes differ |
| `load_argument_registers` | `pass_in_register` | `FunctionCalls.c` | identical |
| `store_argument_on_stack` | `pass_in_memory` | `FunctionCalls.c` | identical |

Of the functions the hooks call, `CMangler_FunctionLinkName` (in the matching `CMangler.c`) differs in 8 of 416
bytes. The others are identical.

## Explain a source

```text
mwcc-tools oracle --from-ninja ../prime/build.ninja src/GuiSys/CGuiFrame.cpp --function Draw
mwcc-tools oracle example.cpp --version GC_2_7 -- -O4,p -inline auto
```

`--from-ninja` reads the source's working directory, compiler version and flags.
The source path is relative to that Ninja file's directory, or absolute.
Do not combine it with explicit compiler flags. Without a Ninja rule or
`--version`, the version defaults to `GC_1_3_2`.

The report starts with the stock-object check, then explains inlining,
register allocation and the stack frame for each selected function.
`--function NAME` matches a display or link name; use exact link names when
overloads would be ambiguous.

## Select a view

| Option | Shows |
|---|---|
| `--pcode` | Instructions given to the allocator, including virtual registers. |
| `--exchange A,B` | Why two virtual registers were colored in that order. |
| `--schedule` | Scheduler picks and why other candidates lost. |
| `--inline-cost CALLEE` | Inline costs broken down by statement. |
| `--near N` | Automatic inline decisions within N of the threshold. |
| `--full-cost` | Costs beyond the cutoff and inline-depth limits. |
| `--instantiation TEXT` | Matching bodies being queued, parsed and emitted. |
| `--locals` | Aggregate scalarization and stack-frame comparison. |
| `--blocks` | Block boundaries and rejected merges. |
| `--cse` | Common-subexpression temporaries and their registers. |
| `--ir LINE` | With CSE, optimizer IR for a line or `all`. |
| `--raw` | Raw trace records. |
| `--json` | The schema-6 report. |

Use `--override PATH=REPLACEMENT` to test a source or header privately; both
oracle and stock compilers receive the replacement. Repeat it for multiple files.
`--keep-object PATH` saves the generated object.
`--no-check` skips stock comparison and labels the run unchecked.

Reference guides:
[registers](oracle/registers.md), [colouring](oracle/colouring.md),
[inlining](oracle/inlining.md), [locals](oracle/locals.md),
[blocks](oracle/blocks.md), [CSE](oracle/cse.md) and
[translation-unit tools](oracle/tu-tools.md).

## Save results for automation

```text
mwcc-tools oracle compile-trace SOURCE --version GC_2_7 -- COMPILER_FLAGS > capture.json
mwcc-tools oracle analyze-trace saved.trace --version GC_2_7 --json
```

`compile-trace` returns a format-1 envelope with `compilation` and `report`.
Require `compilation.check_status.status == "verified"` when stock equality is
needed. A successful process exit alone does not establish that equality.
Plain `oracle --json` returns the report directly.

Use native commands and versioned structured results in integrations.
The [trace/API reference](oracle/trace.md) documents records and JSON fields.

## Function names

Reports name the compiler function behind a temporary (`made by`) or a deferred emission (`caller`).
Where `symbols.txt` only has an address name, they give the function's name followed by that symbol, as in
`CInline_ExportCheck (fn_005114e0)`. Hooks, matches and links keep using the symbol.

The names come from `config/<version>/names.txt` (address, size, name, symbol). They are taken from
NWPlayer123's CC0 GC 2.x decompilation, from its 2.7 `symbols.txt` at commit `888bd76d`. That project checks
its names against Pro7 (Ninji's decompilation), the Wii compilers' CodeView and the assert lines. The tables
cover 1092 of 2.7's 1093 `fn_` symbols. One `fn_` symbol can hold several of its functions: 2.7's `fn_004a7500`
is `CreateNewLoopExitSuccessor`, `CreatePreHeader` and `CreateRIS`, and the name of the function at the address
is used. 1.3 and 1.3.2 take a 2.7 name only where the same `fn_` symbol has the same size (822 and 841 symbols).
With call targets and addresses masked, all but six of each are at least 90% the same bytes as 2.7's. Five of those
six are at 78–89% and keep the name. `fn_005288e0` (`findinitializer` in 2.7) is at 20% and is left out. Their other `fn_` symbols changed by
2.7 and stay unnamed. 2.6's table is made the same way from that commit's 2.6 `symbols.txt` (1090 of its 1091 `fn_`
symbols); see [2.6's differences](gc26-differences.md).

## Check many inputs

```text
mwcc-tools corpus --version GC_1_3_2 --canaries path/to/canaries --jobs 8
mwcc-tools corpus --version GC_2_7 --project path/to/echoes --keep out-dir
```

`corpus` compares objects, diagnostics and exit codes. It returns failure if
any input differs. `--limit` and `--list FILE` narrow the inputs;
`--all-traces` enables optional capture streams. `--exe` checks another oracle executable, and
`--trace-reference OTHER` also compares its trace with OTHER's (see the source-built oracle above).
Historical migration results are in the [acceptance audit](oracle-rust-completion.md).
