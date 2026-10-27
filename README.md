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

GC 1.3.2, 2.6 and 2.7's compilers and oracles run in process as WebAssembly, built from the compilers' sources in
`gc2/` (the [mweppc](https://github.com/doldecomp/mweppc) decompilation), and so do GC 1.3.2's and 2.7's linkers
and the GC 1.2.5n compiler, built from this repository's `src/`. The expected setup downloads the prebuilt modules instead of building them:

```sh
git clone https://github.com/PrimeDecomp/mwcc-tools
cd mwcc-tools
gh release download --repo PrimeDecomp/mwcc-tools --pattern 'mwcc-wasm-*.zip'
for archive in mwcc-wasm-*.zip; do unzip -o "$archive"; done
cargo build --locked --release -p mwcc-tools
```

There is one archive per version (`mwcc-wasm-GC_2_7-*.zip`, `mwcc-wasm-GC_1_3_2-*.zip`, each with its linker, and
`mwcc-wasm-GC_1_2_5n-*.zip`). An
archive keeps the modules' paths (`build/GC_2_7/wasm/`, `build/LD_2_7/wasm/`), so it unpacks into the checkout; the tools read the
checkout's `config/` beside them. Each release also has a prebuilt `mwcc-tools` for Windows x64, Linux x64 and
macOS arm64 ([native package](docs/oracle-rust-release.md)) in place of the `cargo build`: set `MWCC_ORACLE_ROOT`
to the checkout to use one. Builds of `main` between releases are the Build workflow's artifacts.

Building the tools needs Rust (the version pinned in `rust-toolchain.toml`, which rustup installs) and a C compiler
for the bundled Capstone decoder. On Windows, use `target/release/mwcc-tools.exe`. Test the tools with
`cargo test --locked --workspace` (the WebAssembly tests are skipped without the modules).

Using it for GC 1.3.2
---------------------

GC 1.3.2 (Metroid Prime) has a module like 2.7's: with `mwcc-wasm-GC_1_3_2-*.zip` unpacked, a unit built with the
original 1.3.2 compiler (its size and SHA-1 are checked against `config/GC_1_3_2/config.json`) traces in process:

```sh
export MWCC_ORACLE_ROOT=/path/to/mwcc-tools   # when running from elsewhere
mwcc-tools call oracle.trace project=../prime unit=main/Foo function=Draw views.text=true
mwcc-tools call oracle.regdiff project=../prime unit=main/Foo function=Draw text=true full=true
```

For a bare source, pass `version=GC_1_3_2` (the default). The 1.3.2 module does not report the register counters'
provenance streams yet ([details](docs/wasm.md)). `mwcc-tools run build/compilers/GC/1.3.2/mwldeppc.exe ...` (or a
build's `--wrapper`) links with the 1.3.2 linker module. The native oracle, the original `mwcceppc.exe` relinked with
trace hooks, still builds with `configure --version GC_1_3_2 --oracle` ([oracle.md](docs/oracle.md#build)); GC 1.3 has
only that one ([without a module](docs/oracle/experiments.md#without-a-module)). To compile a unit with 1.3.2 from a
project built with another compiler, see [other versions](docs/versions.md). Differences between 1.3.2 and 2.7 are in
[gc27-differences.md](docs/gc27-differences.md).

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

Needed for GC 2.6 (only 1.3.2's and 2.7's are published) or to change the compilers' or linkers' sources. Besides Rust, the build needs:

- the `wasm32-unknown-unknown` target (`rustup target add wasm32-unknown-unknown`)
- [ninja](https://github.com/ninja-build/ninja)
- LLVM 22 or later (`clang`, `wasm-ld`), on the path or named by `CLANG` and `WASM_LD` when running `configure`
- [binaryen](https://github.com/WebAssembly/binaryen)'s `wasm-opt` (CI uses version_133), on the path or named by
  `WASM_OPT` when running `configure`: every module is its `-O3` of what wasm-ld links

macOS: `brew install ninja llvm lld binaryen` (Homebrew's LLVM is not on the path: set `CLANG` and `WASM_LD`).
Linux: `apt install ninja-build`, clang-22 and wasm-ld-22 from [apt.llvm.org](https://apt.llvm.org), and
binaryen from its [releases](https://github.com/WebAssembly/binaryen/releases).
Windows: `winget install Ninja-build.Ninja` and `winget install LLVM.LLVM`, and binaryen from its
[releases](https://github.com/WebAssembly/binaryen/releases).

```sh
target/release/mwcc-tools configure --version GC_2_7
ninja build/GC_2_7/wasm/mwcc.wasm build/LD_2_7/wasm/mwldeppc.wasm
```

Use `--version GC_1_3_2` for 1.3.2 (and `build/LD_1_3_2/wasm/mwldeppc.wasm`), `--version GC_2_6` for 2.6, and
`--version GC_1_2_5n` for 1.2.5n (`build/GC_1_2_5n/wasm/mwcc.wasm`, no oracle and no linker). [wasm.md](docs/wasm.md)
has the details.

License
=======

The project's own files are dedicated to the public domain under CC0 (`LICENSE`); third-party components retain their
upstream licenses. The project started from
[inspiredrobot/mwcc](https://github.com/inspiredrobot/mwcc), also CC0. `gc2/` is mweppc, NWPlayer123's
decompilation of the GC 1.3.2 to 2.7 compilers, CC0 (`gc2/LICENSE`). `wasm/libc` adapts parts of the MSL of
[PrimeDecomp/echoes](https://github.com/PrimeDecomp/echoes), the Metroid Prime 2: Echoes decompilation, also CC0.
