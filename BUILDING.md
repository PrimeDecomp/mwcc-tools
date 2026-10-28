Building mwcc-tools
===================

Most users only need a release ([README](README.md#installation)). Build from source to change the tools or the
compilers' WebAssembly modules.

The tools
---------

Building the tools needs Rust (the version pinned in `rust-toolchain.toml`, which rustup installs) and a C compiler
for the bundled Capstone decoder.

```sh
git clone https://github.com/PrimeDecomp/mwcc-tools
cd mwcc-tools
gh release download --repo PrimeDecomp/mwcc-tools --pattern 'mwcc-wasm-*.zip'
for archive in mwcc-wasm-*.zip; do unzip -o "$archive"; done
cargo build --locked --release -p mwcc-tools
```

GC 1.3.2, 2.6 and 2.7's compilers and oracles run in process as WebAssembly, built from the compilers' sources in
`gc2/` (the [mweppc](https://github.com/doldecomp/mweppc) decompilation), and so do GC 1.3.2's and 2.7's linkers
and the GC 1.2.5n compiler, built from this repository's `src/`. The steps above download the prebuilt modules
instead of building them.

There is one archive per version (`mwcc-wasm-GC_2_7-*.zip`, `mwcc-wasm-GC_1_3_2-*.zip`, each with its linker,
`mwcc-wasm-GC_2_6-*.zip` and `mwcc-wasm-GC_1_2_5n-*.zip`). An archive keeps the modules' paths
(`build/GC_2_7/wasm/`, `build/LD_2_7/wasm/`), so it unpacks into the checkout; the tools read the checkout's
`config/` beside them. A released `mwcc-tools` ([native package](docs/oracle-rust-release.md)) can stand in for the
`cargo build`: set `MWCC_ORACLE_ROOT` to the checkout to use one. Builds of `main` between releases are the Build
workflow's artifacts.

On Windows, the executable is `target/release/mwcc-tools.exe`. Set `MWCC_ORACLE_ROOT` to the checkout (or pass
`--oracle-root`) when running the tools from elsewhere.

Test the tools with `cargo test --locked --workspace` (the WebAssembly tests are skipped without the modules).

The WebAssembly modules
-----------------------

Needed only to change the compilers' or linkers' sources. Besides Rust, the build needs:

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

The native oracle
-----------------

The native oracle, the original `mwcceppc.exe` relinked with trace hooks, builds with
`configure --version GC_1_3_2 --oracle` ([oracle.md](docs/oracle.md#build)). GC 1.3 has only that one
([without a module](docs/oracle/experiments.md#without-a-module)).
