mwcc-tools
==========

[![Build Status]][actions]

[Build Status]: https://github.com/PrimeDecomp/mwcc-tools/actions/workflows/build.yml/badge.svg
[actions]: https://github.com/PrimeDecomp/mwcc-tools/actions/workflows/build.yml

Tools for matching code compiled by `mwcceppc.exe`, the CodeWarrior compiler for GameCube. They explain register
assignment, inlining, scheduling and stack layout from a trace of the compiler (the oracle), and search,
reduce and compare candidate sources against it.

Getting started
===============

GC 2.6 and 2.7's compiler and oracle run in process as WebAssembly, built from the compiler's sources in `gc2/`.
The expected setup downloads GC 2.7's prebuilt modules instead of building them:

```sh
git clone https://github.com/PrimeDecomp/mwcc-tools
cd mwcc-tools
gh release download --repo PrimeDecomp/mwcc-tools --pattern 'mwcc-wasm-GC_2_7-*.zip'
unzip -o mwcc-wasm-GC_2_7-*.zip
cargo build --locked --release -p mwcc-tools
```

The archive keeps the modules' paths (`build/GC_2_7/wasm/`), so it unpacks into the checkout; the tools read the
checkout's `config/` beside them. Each release also has a prebuilt `mwcc-tools` for Windows x64, Linux x64 and
macOS arm64 ([native package](docs/oracle-rust-release.md)) in place of the `cargo build`: set `MWCC_ORACLE_ROOT`
to the checkout to use one. Builds of `main` between releases are the Build workflow's artifacts.

Building the tools needs Rust (the version pinned in `rust-toolchain.toml`, which rustup installs) and a C compiler
for the bundled Capstone decoder. On Windows, use `target/release/mwcc-tools.exe`. Test the tools with
`cargo test --locked --workspace` (the WebAssembly tests are skipped without the modules).

Using the tools
===============

The oracle's tools are methods: JSON parameters in, JSON results out. `mwcc-tools call METHOD KEY=VALUE ...` runs
one (a VALUE that is JSON is read as JSON), `mwcc-tools serve` keeps a session over JSON lines, and the Python
package `mwcc_tools` drives that session.

```sh
mwcc-tools call oracle.trace project=../project unit=main/Foo function=Draw views.text=true
mwcc-tools call oracle.analyze trace_path=saved.trace version=GC_2_7
mwcc-tools call oracle.regdiff project=../project unit=main/Foo function=Draw text=true full=true
```

```python
from mwcc_tools import Engine, Trace
with Engine() as engine:
    print('\n'.join(Trace(engine, load='saved', version='GC_2_7').verify(text=True)['text']))
```

Set `MWCC_ORACLE_ROOT` to this checkout (or pass `--oracle-root`) when running the tools from elsewhere. Offline
analysis of a saved trace needs only the `mwcc-tools` executable. `mwcc-tools methods` lists every method and
[api.md](docs/api.md) gives their parameters and results; the [oracle guide](docs/oracle.md) says which to use when.

The WebAssembly oracle does not have every provenance stream yet (expressions, dispatch, coalescing and call
arguments' contexts; see [wasm.md](docs/wasm.md#probes)).

Documentation
=============

| Documentation | Covers |
|---|---|
| [Oracle guide](docs/oracle.md) | Build, tool selection and everyday commands. |
| [Methods](docs/api.md) | Every method's parameters and results. |
| [WebAssembly builds](docs/wasm.md) | The modules in detail: how they are built and run, the probes in the compiler's sources. |
| [Native kit workflow](integrations/decomp-kits/overlay/.agents/docs/oracle-native.md) | Project units, candidates, register differences and structured checks. |
| [Project scan](docs/scan.md), [other versions](docs/versions.md) | Instruction patterns over a whole project; compiling a unit with another of its compilers. |
| [Trace reference](docs/oracle/trace.md) | Raw records and versioned JSON. |
| [Provenance](docs/oracle/provenance.md), [source search](docs/oracle/source-search.md) | Value origins, source search, reduction and the matched-source index. |
| [Decompilation](docs/decompilation.md) | The compilers' sources and their other builds. |

Third-party notices are under [licenses](licenses/README.md).

Building the WebAssembly modules
================================

Needed for GC 2.6 (only 2.7's are published) or to change the compiler's sources. Besides Rust, the build needs:

- the `wasm32-unknown-unknown` target (`rustup target add wasm32-unknown-unknown`)
- [ninja](https://github.com/ninja-build/ninja)
- LLVM 22 or later (`clang`, `wasm-ld`), on the path or named by `CLANG` and `WASM_LD` when running `configure`

macOS: `brew install ninja llvm lld` (Homebrew's LLVM is not on the path: set `CLANG` and `WASM_LD`).
Linux: `apt install ninja-build`, and clang-22 and wasm-ld-22 from [apt.llvm.org](https://apt.llvm.org).
Windows: `winget install Ninja-build.Ninja` and `winget install LLVM.LLVM`.

```sh
target/release/mwcc-tools configure --version GC_2_7
ninja build/GC_2_7/wasm/mwcc.wasm
```

Use `--version GC_2_6` for 2.6. [wasm.md](docs/wasm.md) has the details.

License
=======

The project's own files are dedicated to the public domain under CC0 (`LICENSE`); third-party components retain their
upstream licenses. The project started from
[inspiredrobot/mwcc](https://github.com/inspiredrobot/mwcc), also CC0. `gc2/` is NWPlayer123's GC 2.x
decompilation, CC0 (`gc2/LICENSE`). `wasm/libc` adapts parts of the MSL of
[PrimeDecomp/echoes](https://github.com/PrimeDecomp/echoes), the Metroid Prime 2: Echoes decompilation, also CC0.
