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
`--all-traces` enables optional capture streams.
Historical migration results are in the [acceptance audit](oracle-rust-completion.md).
