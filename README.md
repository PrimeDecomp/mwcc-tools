mwcc-tools
==========

[![Build Status]][actions]

[Build Status]: https://github.com/rayanht/mwcc/actions/workflows/build.yml/badge.svg
[actions]: https://github.com/rayanht/mwcc/actions/workflows/build.yml

Tools for matching code compiled by `mwcceppc.exe`, the CodeWarrior compiler for GameCube. They explain register
assignment, inlining, scheduling and stack layout from a trace of the compiler (the oracle), and search,
reduce and compare candidate sources against it.

Getting started
===============

The tools are built with Rust, and GC 2.6 and 2.7's compiler and oracle are built from the compiler's sources in
`gc2/` as WebAssembly, which `mwcc-tools` runs in process. The build needs:

- Rust (the version pinned in `rust-toolchain.toml`, which rustup installs) with the `wasm32-unknown-unknown`
  target, and a C compiler for the bundled Capstone decoder
- [ninja](https://github.com/ninja-build/ninja)
- LLVM 22 or later (`clang`, `wasm-ld`), on the path or named by `CLANG` and `WASM_LD` when running `configure`

macOS: `brew install ninja llvm lld` (Homebrew's LLVM is not on the path: set `CLANG` and `WASM_LD`).
Linux: `apt install ninja-build`, and clang-22 and wasm-ld-22 from [apt.llvm.org](https://apt.llvm.org).
Windows: `winget install Ninja-build.Ninja` and `winget install LLVM.LLVM`.

```sh
rustup target add wasm32-unknown-unknown
cargo build --locked --release -p mwcc-tools
target/release/mwcc-tools configure --version GC_2_7
ninja build/GC_2_7/wasm/mwcceppc.wasm build/GC_2_7/wasm/oracle.wasm
```

On Windows, use `target/release/mwcc-tools.exe`. Use `--version GC_2_6` for 2.6. Test the tools with
`cargo test --locked --workspace`.

Using the tools
===============

```sh
mwcc-tools oracle --from-ninja ../project/build.ninja src/Foo.cpp --function Draw
mwcc-tools oracle analyze-trace saved.trace --version GC_2_7 --json
mwcc-tools oracle regdiff --project ../project --unit main/Foo --function Draw --full
mwcc-tools colouring verify --load saved --json
```

Set `MWCC_ORACLE_ROOT` to this checkout when running the tools from elsewhere. Offline analysis of a saved trace
needs only the `mwcc-tools` executable. `mwcc-tools --help` lists every command; the [oracle guide](docs/oracle.md)
says which to use when.

The WebAssembly oracle does not have every provenance stream yet (expressions, dispatch, coalescing and call
arguments' contexts; see [wasm.md](docs/wasm.md#probes)).

Documentation
=============

| Documentation | Covers |
|---|---|
| [Oracle guide](docs/oracle.md) | Build, tool selection and everyday commands. |
| [WebAssembly builds](docs/wasm.md) | The modules in detail: CI, Linux, the probes in the compiler's sources. |
| [Native kit workflow](integrations/decomp-kits/overlay/.agents/docs/oracle-native.md) | Project units, candidates, register differences and structured checks. |
| [Trace/API reference](docs/oracle/trace.md) | Raw records and versioned JSON. |
| [Source-guided tools](docs/oracle-source-guided-status.md) | Provenance, source search, reduction and the matched-source index. |
| [Decompilation](docs/decompilation.md) | The compilers' sources and their other builds. |
| [Migration status](docs/oracle-rust-status.md) | Completed scope and release evidence. |
| [Benchmarks](docs/oracle-rust-benchmarks.md) | Analysis/search speed and memory costs. |

Third-party notices are under [licenses](licenses/README.md).

License
=======

The project's own files are dedicated to the public domain under CC0 (`LICENSE`); third-party components retain their
upstream licenses. The project started from
[inspiredrobot/mwcc](https://github.com/inspiredrobot/mwcc), also CC0. `gc2/` is NWPlayer123's GC 2.x
decompilation, CC0 (`gc2/LICENSE`). `wasm/libc` adapts parts of the MSL of
[PrimeDecomp/echoes](https://github.com/PrimeDecomp/echoes), the Metroid Prime 2: Echoes decompilation, also CC0.
