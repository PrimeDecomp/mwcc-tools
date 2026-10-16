MWCC
====

[![Build Status]][actions] [![1.0]][progress] [![1.1]][progress] [![1.1p1]][progress] [![1.2.5]][progress]
[![1.2.5n]][progress] [![1.3]][progress]

[Build Status]: https://github.com/rayanht/mwcc/actions/workflows/build.yml/badge.svg
[actions]: https://github.com/rayanht/mwcc/actions/workflows/build.yml
[1.0]: https://decomp.dev/rayanht/mwcc/GC_1_0.svg?mode=shield&measure=code&label=1.0
[1.1]: https://decomp.dev/rayanht/mwcc/GC_1_1.svg?mode=shield&measure=code&label=1.1
[1.1p1]: https://decomp.dev/rayanht/mwcc/GC_1_1p1.svg?mode=shield&measure=code&label=1.1p1
[1.2.5]: https://decomp.dev/rayanht/mwcc/GC_1_2_5.svg?mode=shield&measure=code&label=1.2.5
[1.2.5n]: https://decomp.dev/rayanht/mwcc/GC_1_2_5n.svg?mode=shield&measure=code&label=1.2.5n
[1.3]: https://decomp.dev/rayanht/mwcc/GC_1_3.svg?mode=shield&measure=code&label=1.3
[progress]: https://decomp.dev/rayanht/mwcc

A matching decompilation of `mwcceppc.exe`, the Windows/x86 CodeWarrior compiler for GameCube.

Supported versions:

- `GC_1_0`: GameCube 1.0
- `GC_1_1`: GameCube 1.1
- `GC_1_1p1`: GameCube 1.1p1 (a patched 1.1)
- `GC_1_2_5`: GameCube 1.2.5
- `GC_1_2_5n`: GameCube 1.2.5n (1.2.5 with Ninji's patch)
- `GC_1_3`: GameCube 1.3
- `GC_1_3_2`: GameCube 1.3.2 (split and relinked; no sources of its own, see `docs/gc132-differences.md`)
- `GC_2_7`: GameCube 2.7 (split and relinked from 1.3.2's symbols; no sources of its own, see `docs/gc27-differences.md`)

Dependencies
============

- Rust 1.96.0 and a C compiler to build `mwcc-tools`, or a prebuilt native distribution
- [ninja](https://github.com/ninja-build/ninja)
- [unshield](https://github.com/twogood/unshield), to unpack the CodeWarrior Pro 5.3 updater

macOS: `brew install ninja unshield`. Linux: `apt install ninja-build unshield`.
Windows: `winget install Ninja-build.Ninja`, and unshield through WSL (`wsl sudo apt install unshield`).

[wibo](https://github.com/decompals/wibo) runs the compilers and is downloaded automatically.

Building
========

```sh
cargo build --locked --release -p mwcc-tools
target/release/mwcc-tools configure
ninja
```

On Windows, use `target/release/mwcc-tools.exe`. For another version, pass
`configure --version GC_1_2_5n` (or `GC_1_3`). Python is used by independent migration
validation and unrelated kit support, not the native production build commands.

The build downloads what it needs but this repository does not contain:

- the original executables, from the [decomp.dev compiler archive](https://files.decomp.dev), checked against
  their published SHA-1 (`build/compilers/GC`)
- the CodeWarrior Pro 4, 5, 5.3 and 6 Windows/x86 compilers that built them and the Pro 5.3 linker, from the
  Internet Archive (`build/compilers/pro*`)
- the MSL C library and runtime sources of CodeWarrior Pro 5, from the same Internet Archive disc, and the runtime
  sources of its 5.3 updater (`lib/`), with the compiler's own changes to `printf.c`, `time.c`, `time.win32.c`,
  `startup.win32.c`, `ThreadLocalData.c` and `exchand.cpp` applied (`lib/extra`), and the Win32 import library the
  executable links

For 1.1, 1.1p1, 1.2.5 and 1.2.5n, as in [decomp-toolkit](https://github.com/encounter/decomp-toolkit) projects, the
original is split into one object per translation unit, and `ninja` links the executable with the CodeWarrior linker
from the compiled objects of the Matching sources and the split objects of the others, then checks it against the
original's SHA-1. A source is Matching when its object, code and data, links into the original. For the other versions,
each compiled function is compared with the original's; `ninja` fails when a function of a Matching source differs.

`python tools/verify.py` builds and checks every version, and that every function of a source matches.

For 1.3, 1.3.2 and 2.7, `docs/oracle.md` describes the oracle executable (the original compiler with trace wrappers) and
`tools/oracle.py`, which explains a source's register assignment and inlining with it.

Diffing
=======

Open the project in [objdiff](https://github.com/encounter/objdiff) after building.

Project structure
=================

- `config/sources.json`: each source's compiler, flags and status (Matching or NonMatching)
- `config/<version>/config.json`: the original executable and its SHA-1; for the versions other than 1.2.5, the
  sources Matching in that version; for 1.3, the compilers that replace Pro 5 and 5.3, and for 1.0, the options that
  replace the sources' (it does not auto-inline)
- `config/<version>/symbols.txt`, `splits.txt` (1.1, 1.1p1, 1.2.5, 1.2.5n): the original's symbols and its
  translation units, in decomp-toolkit's formats
- `config/<version>/functions.json`: each function's address, size and source (none yet for a function not
  decompiled)
- `config/<version>/bindings.json`: the addresses of the data and functions the sources reference
- `config/lmgr326b.def`: the FLEXlm library the compiler imports by ordinal
- `src/`: the compiler (`driver`, `frontend`, `optimizer`, `backend`) and its statically linked C library and
  runtime (`msl`, `runtime`: one source per object of the original, which builds the library's source of it)
- `include/`: headers; `include/libc` is the C library as the compiler's sources see it
- `lib/` (downloaded): the MSL C library and runtime sources, built by `src/msl` and `src/runtime`

The source is formatted with `clang-format` (`uv run clang-format -i FILE`).

Rust oracle tools (migration in progress)
========================================

The native host tools are being ported from the `oracle` branch. Build and test them with:

```sh
cargo build --release --locked
cargo test --locked --workspace
```

Building the native tools requires a C compiler for the statically linked x86 Capstone decoder (MSVC on Windows or a
working C toolchain on Linux). The decoder and Rust bindings are pinned; their notices are retained under
[licenses](licenses/README.md). Released native binaries do not require a Python or Capstone installation.

The toolchain is pinned in `rust-toolchain.toml`. The native tool can analyze an existing trace without a compiler,
compile a source using a built oracle, and verify saved colourings:

```sh
mwcc-tools oracle analyze-trace saved.trace --version GC_1_3_2 --json
mwcc-tools oracle --oracle-root /path/to/mwcc-oracle --from-ninja ../prime/build.ninja src/GuiSys/CGuiFrame.cpp --json
mwcc-tools colouring verify --load saved --json
mwcc-tools oracle regdiff --project ../prime --unit main/GuiSys/CGuiFrame --function Draw --full
mwcc-tools header-impact --from-ninja ../prime/build.ninja --override include/Header.hpp=/path/to/candidate.hpp --json
mwcc-tools corpus --oracle-root /path/to/mwcc-oracle --canaries /path/to/canaries --version GC_2_7 --jobs 8 --all-traces
mwcc-tools split GC_1_3_2 --root /path/to/mwcc-oracle --output /path/to/split-output
mwcc-tools compare compare GC_1_3 --root /path/to/mwcc-oracle
mwcc-tools funcmatch 1.3 1.3.2 --root /path/to/mwcc-oracle --json
```

Set `MWCC_ORACLE_ROOT` instead of passing `--oracle-root` on each command. The original compiler and trace producer
are still built as described in [the oracle guide](docs/oracle.md). `oracle compile-trace` returns a versioned envelope
with explicit check status, hashes, diagnostics, raw trace and the schema-6 report. Ordinary `oracle --json` returns
the compatibility report. See [migration status](docs/oracle-rust-status.md) for supported commands, verification
evidence and remaining work; existing Python integrations remain available during the migration.
On Linux, `MWCC_WIBO` explicitly selects the loader; otherwise the runner uses `build/tools/wibo` under the oracle
root, then `wibo` on PATH. Use the pinned loader for flags such as `-multibyte`; an older global loader can lack
required Windows APIs. Windows continues to launch the compiler directly.
Use the [native oracle kit API](integrations/decomp-kits/overlay/.agents/docs/oracle-native.md)
for kit automation and structured results. Optional old-name launchers and adoption evidence are documented in
[the kit integration guide](integrations/decomp-kits/README.md). New native kits are generated separately from the retained historical archives.
Native `implib`, `rsrc2res`, `split`, `link` and `compare` cover executable construction; `link` still uses the historical linker
and `compare report` uses objdiff. Native `locate`, `bindfrom` and `funcmatch` support binary investigation; `bindfrom`
updates the selected version's bindings file when explicitly invoked.
Native binary `remap` carries function lists and bindings into a selected version; `derive-splits` writes inferred
split/symbol metadata and supports `--out DIR` for reviewing it in a separate directory.
Native `disasm`, `fdiff` and `nearest` provide annotated x86 listings, instruction diffs and nearest-function reports.
Native `configure` writes Ninja and objdiff configuration; `transform-dep` converts compiler dependencies to UTF-8
Ninja depfiles. Native `download` acquires the pinned compilers, runtime sources, objdiff and wibo, using external
`unshield` (through WSL on Windows when needed) for the Pro 5.3 updater. Native `verify` runs all eight versions;
`gates` runs the legacy comparison/relink matrix. Both run versions sequentially, return failure when a gate fails,
and restore the primary configuration. An isolated Windows bootstrap, all verification/gate configurations and all
three oracle builds have passed on Windows and Linux; each rebuilt oracle passed 2,313 canaries with all trace
options. The existing Python build entry points remain available during migration.

`configure --version GC_1_3_2 --oracle` selects link mode and the Rust producer, including
local-call, allocation-graph/entry, frame-layout, scheduling, block/stage, scalarization, CSE, propagation,
inline-ancestry/context, argument bindings and common template/instantiation hooks plus shared PCode/expression tracing and a separate
Rust trace writer. A separate frontend object supplies inline decisions/costs, expression expansion and deferred-node
dispatch for each compiler version, completing all 31 observation hooks. This mode compiles no C observation sources. Install the producer
target with `rustup target add i686-pc-windows-msvc`, then build `build/GC_1_3_2/oracle/mwcceppc.exe` with Ninja.
This source-build mode requires rustc; ordinary host analysis does not require the 32-bit target.
`--oracle --producer c` selects the retained C rollback, and regeneration preserves that choice.
`build-hooks OUTPUT` builds and checks the standalone hook object; add `--trace-shim` for the separate writer. `RUSTC` or `--rustc` selects its compiler.
The [source-guided graph-search roadmap](docs/oracle-source-guided-search.md) is being implemented after migration.
Current development tools expose [backend provenance](docs/oracle/provenance.md) and a native
[graph-directed source search API](docs/oracle/source-search.md), a
[mechanism-preserving reducer](docs/oracle/source-reduce.md) and a
[matched-source index](docs/oracle/source-index.md). A real checked C++ edit now
changes the EnvModulate allocation graph; its final match worsens. Complete
source/lifetime provenance and the remaining real mechanisms stay open; see
[implementation status](docs/oracle-source-guided-status.md).

The [audited benchmarks](docs/oracle-rust-benchmarks.md) separate analysis/search performance from compiler runtime
and report the memory tradeoffs. `package-release OUTPUT.zip --binary FILE --target TARGET` creates a native
distribution with checksums, metadata, kit documentation and matching dependency notices; see the
[distribution guide](docs/oracle-rust-release.md). Local Windows/Linux packages pass independent archive and
standalone execution audits. Native kits include their source pin and complete Git bundle; the local release does not require a public remote to contain that commit.

License
=======

The project's own files are dedicated to the public domain under CC0 (`LICENSE`); third-party components retain their
upstream licenses. The project started from
[inspiredrobot/mwcc](https://github.com/inspiredrobot/mwcc), also CC0.
