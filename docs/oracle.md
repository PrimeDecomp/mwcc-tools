# Oracle tools

The oracle is the original CodeWarrior compiler relinked with Rust trace hooks. `mwcc-tools` uses its trace to
explain register allocation, inlining, scheduling and stack layout. The oracle versions are `GC_1_3`, `GC_1_3_2`,
`GC_2_6` and `GC_2_7`.

A traced compile is checked against the stock compiler's object (or the build's, for a unit). Rely on the trace only
when that check passes.

## The methods

Each tool is a method, run by `mwcc-tools call METHOD KEY=VALUE ...`, in a `mwcc-tools serve` session, or from the
Python package `mwcc_tools`. [api.md](api.md) gives every parameter and result.

| Method | Use it to | Guide |
|---|---|---|
| `oracle.trace` | Compile a unit, or a source and flags, and explain registers, inlining and the frame. | below |
| `oracle.regdiff` | Explain a function's register differences from its target. | [registers](oracle/registers.md) |
| `oracle.analyze` | Analyze a saved trace without compiling. | [trace](oracle/trace.md) |
| `trace.*` | Replay the colouring and search graph edits that give the target's registers. | [colouring](oracle/colouring.md) |
| `experiment.run`, `source.variants` | Compile and score many candidate sources. | [experiments](oracle/experiments.md) |
| `inline.diff`, `inline.window`, `inline.calibrate` | Compare inline decisions, find the inline limit a target agrees with. | [TU tools](oracle/tu-tools.md) |
| `tu.data_diff`, `tu.header_impact` | Compare data layout with the target; find what a header edit changes. | [TU tools](oracle/tu-tools.md) |
| `provenance.compare` | Compare two captured allocation graphs. | [graph correspondence](oracle/graph-correspondence.md) |
| `corpus` (a command) | Check oracle/stock equality over many inputs. | [below](#check-many-inputs) |

The development tools have their own guides: [backend provenance](oracle/provenance.md) (with
[call arguments](oracle/call-arguments.md), [scalarization](oracle/scalarization.md) and the
[producer](oracle/producer.md)), [source search](oracle/source-search.md), [reduction](oracle/source-reduce.md),
[matched-source index](oracle/source-index.md), [reproducers](oracle/source-repro.md), and the frontend's
[arenas and lifetimes](oracle/source-arenas.md) and [nodes](oracle/source-nodes.md).

## Build

```text
cargo build --release --locked -p mwcc-tools
rustup target add i686-pc-windows-msvc --toolchain 1.96.0
mwcc-tools configure --version GC_1_3_2 --oracle
ninja build/GC_1_3_2/oracle/mwcceppc.exe
```

* Add `target/release` to PATH (`mwcc-tools.exe` on Windows). Configure writes one version's Ninja graph at a time.
* `configure --oracle --producer c` selects the C hooks instead of the Rust ones (the default).
* The original compiler and runtime are downloaded during the build. `ninja -t clean` on the oracle target removes
  them.
* `MWCC_ORACLE_ROOT` names the built checkout when the tools run elsewhere.
* Linux/WSL and macOS run the compiler through wibo 1.2.0: `MWCC_WIBO`, else `build/tools/wibo`, else PATH. The
  pinned macOS wibo is Intel-only (Rosetta 2 on Apple Silicon). Windows runs it directly.

For a decomp kit, follow the [native kit workflow](../integrations/decomp-kits/overlay/.agents/docs/oracle-native.md).

### GC 2.7 without the original compiler

GC 2.6 and 2.7 build by default as WebAssembly, `build/<version>/wasm/mwcc.wasm` with the oracle's hooks, and
`mwcc-tools` runs it in process wherever it would run the stock compiler or the oracle executable
([wasm.md](wasm.md#the-oracle)). `MWCC_IN_PROCESS=0` runs the executable instead.

The GC 2.7 configure (without `--oracle`) can also build an x86 oracle from the `gc2` sources alone. It is not built
by default:

```text
mwcc-tools configure --version GC_2_7
ninja build/GC_2_7/gc2/source-oracle/mwcceppc.exe
```

`mwcc-tools source-only GC_2_7 --oracle` links every compiled `gc2` object ([gc27-differences.md](gc27-differences.md))
with the three Rust objects. The original `mwcceppc.exe` and its split are not read; the Metrowerks toolchain
(compilers, `mwld`, `gc2/lib`'s runtime and MSL) is still downloaded.

How the hooks are linked (names from `config/GC_2_7/symbols.txt`, taken to the `gc2` symbol at the same address):

* A hooked function is renamed `<hook>__retail`; calls to it go to the wrapper `<hook>`, except from its own body.
  The link prints the 46 hooks and writes them to `source-oracle/hooks.json`.
* A function or variable the hooks read gets the oracle's name as a second global name, or `<name>__oracle` where
  `gc2` uses that name for something else (`CMangler_GetLinkName`).
* A static variable is found by its offset in its unit's section (`gc2/config/GC_2_7/splits.txt`); the link checks
  the section size and the reference. A common block's references are renamed with the offset added
  (`iro_pointer_analysis` is `copts+0xf8`).

Only the hooks are linked, not the guarded probes, which are pinned to the original's split bytes. So on this
executable:

* The `pv.*` provenance records (`backend`, counters, dispatch, arguments, coalescing) are not written. Without the
  counter probe the graph wrapper writes `pv.gap reason=graph_context` and the scalarizer `pv.scalar.gap
  reason=scalar_function_context`. The `source_nodes` stream is incomplete.
* `OracleRust_ArenaGlobals` is a zero stand-in: `source_arenas` reports `invalid_globals` and `unsupported=1`.
* The hooks' own records (`ra.*`, `fr.*`, `sc.*`, `lc.*`, `ce.*`, `tp.*`, `bk.*`, ...) are complete.
* `ce.object caller=` and `tp.emit caller=` are this executable's addresses: look them up in
  `source-oracle/mwcceppc.map`, not `symbols.txt`.

Check it with `corpus --trace-reference`, which compares its traces with a second oracle's (`pv.*` records left out,
addresses masked):

```text
mwcc-tools corpus --version GC_2_7 --canaries DIR --all-traces \
  --exe build/GC_2_7/gc2/source-oracle/mwcceppc.exe --trace-reference build/GC_2_7/oracle/mwcceppc.exe
```

On the 2313 mwcc-rs canaries, every object, message, exit code and trace is identical.

The hooked functions in units that do not match yet differ from the original's split (relocations masked) in:
`CInline_EstimateSizeOfExpr` (48 of 495 bytes), `CInline_ObjectAddrRef` (4 of 340), `CInline_CopyNodes` (88 of
1682) and `analyze_arguments` (33 of 1551). `fn_00560c80`'s symbol (0x19d) also covers the next `gc2` function. Of
the functions the hooks call, only `CMangler_FunctionLinkName` differs (8 of 416 bytes). Every other hooked function
is identical.

## Explain a source

```sh
mwcc-tools call oracle.trace project=../echoes unit=main/GuiSys/CGuiFrame function=Draw views.text=true
mwcc-tools call oracle.trace source=example.cpp version=GC_2_7 flags='["-O4,p","-inline","auto"]' views.text=true
```

* A unit (`project` and `unit`, or a `handle` from `unit.load`) compiles as its build compiles it, in process by its
  version's WebAssembly oracle (GC 2.6, 2.7). For a GC 1.3 or 1.3.2 unit, compile its source with the `source`,
  `flags`, `directory` and `version` that `unit.load` gives ([example](oracle/blocks.md#other-limits-cut_limit-merge_limit)).
* A source compiles with `flags` in `directory` (default the current one). `version` defaults to `GC_1_3_2`.
* `function` matches a display or link name; `link` takes an exact link name.
* `document` is the schema-6 report. With `views.text`, `text` is the printed report: the object check, then
  inlining, register allocation and the frame of each selected function.
* `files` (a unit) or `overrides` (a source, `["PATH=REPLACEMENT"]`) replace files privately. `object_path` saves
  the object, `trace_path` the trace. `check: false` skips a source's stock comparison.

## Views

The fields of `views` select the text views, and each turns on the trace stream it needs; `streams` turns on
others ([trace.md](oracle/trace.md)).

| View | Shows | Guide |
|---|---|---|
| `pcode`, `exchange`, `schedule` | The allocator's input, why two vregs got their colours, the scheduler's picks. | [registers](oracle/registers.md) |
| `inline_cost`, `near`, `instantiation` (and `streams.full_cost`) | Inline costs, close decisions, when bodies came to be. | [inlining](oracle/inlining.md) |
| `locals` | Scalarization and the frame against the target's. | [locals](oracle/locals.md) |
| `blocks` | Block cuts and rejected merges. | [blocks](oracle/blocks.md) |
| `cse`, `ir` | Common-subexpression temporaries, and the IR of a line. | [CSE](oracle/cse.md) |
| `raw` | The raw trace records. | [trace](oracle/trace.md) |

## Save results

```sh
mwcc-tools call oracle.trace source=SOURCE version=GC_2_7 flags='["COMPILER_FLAGS"]' capture_path=capture.json
mwcc-tools call oracle.analyze trace_path=saved.trace version=GC_2_7
```

`capture_path` writes a source compile's envelope (`compilation` and `report`). Require
`compilation.check_status.status == "verified"` when stock equality matters. See
[trace.md](oracle/trace.md#native-api) for the formats.

## Function names

Reports name the compiler function behind a temporary (`made by`) or a deferred emission (`caller`). Where
`symbols.txt` has only an address name, they give both: `CInline_ExportCheck (fn_005114e0)`. Hooks, matches and links
keep the symbol.

The names are in `config/<version>/names.txt` (address, size, name, symbol), from NWPlayer123's CC0 GC 2.x
decompilation (its 2.7 and 2.6 `symbols.txt` at commit `888bd76d`), which checks them against Pro7, the Wii
compilers' CodeView and the assert lines.

* 2.7: 1092 of 1093 `fn_` symbols. Where one symbol holds several functions, the one at its address names it.
* 2.6: 1090 of 1091 ([2.6's differences](gc26-differences.md)).
* 1.3 and 1.3.2 take a 2.7 name only where the same `fn_` symbol has the same size (822 and 841 symbols). With call
  targets and addresses masked, all but six of each are at least 90% 2.7's bytes and five more 78–89%;
  `fn_005288e0` (`findinitializer` in 2.7, 20%) is left out. Their other `fn_` symbols stay unnamed.

## Check many inputs

```text
mwcc-tools corpus --version GC_1_3_2 --canaries path/to/canaries --jobs 8
mwcc-tools corpus --version GC_2_7 --project path/to/echoes --keep out-dir
```

`corpus` compares objects, diagnostics and exit codes, and fails if any input differs. `--limit` and `--list FILE`
narrow the inputs, `--all-traces` turns on the optional streams, `--exe` checks another oracle executable, and
`--trace-reference OTHER` compares its traces with OTHER's. `mwcc-tools --help` lists the rest.
