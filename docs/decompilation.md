# The decompilation

[![1.0]][progress] [![1.1]][progress] [![1.1p1]][progress] [![1.2.5]][progress] [![1.2.5n]][progress]
[![1.3]][progress]

[1.0]: https://decomp.dev/rayanht/mwcc/GC_1_0.svg?mode=shield&measure=code&label=1.0
[1.1]: https://decomp.dev/rayanht/mwcc/GC_1_1.svg?mode=shield&measure=code&label=1.1
[1.1p1]: https://decomp.dev/rayanht/mwcc/GC_1_1p1.svg?mode=shield&measure=code&label=1.1p1
[1.2.5]: https://decomp.dev/rayanht/mwcc/GC_1_2_5.svg?mode=shield&measure=code&label=1.2.5
[1.2.5n]: https://decomp.dev/rayanht/mwcc/GC_1_2_5n.svg?mode=shield&measure=code&label=1.2.5n
[1.3]: https://decomp.dev/rayanht/mwcc/GC_1_3.svg?mode=shield&measure=code&label=1.3
[progress]: https://decomp.dev/rayanht/mwcc

The repository began as a matching decompilation of `mwcceppc.exe`, the Windows/x86 CodeWarrior compiler for
GameCube. The tools still build on it: the oracles are the original compilers, split, relinked and hooked, and for
GC 2.6 and 2.7 compiled from these sources. This page covers building and checking the compilers themselves.

Supported versions:

- `GC_1_0`: GameCube 1.0
- `GC_1_1`: GameCube 1.1
- `GC_1_1p1`: GameCube 1.1p1 (a patched 1.1)
- `GC_1_2_5`: GameCube 1.2.5
- `GC_1_2_5n`: GameCube 1.2.5n (1.2.5 with Ninji's patch)
- `GC_1_3`: GameCube 1.3
- `GC_1_3_2`: GameCube 1.3.2 (130 of 242 units from the GC 2.x sources in `gc2/`, see
  [gc132-differences.md](gc132-differences.md))
- `GC_2_6`: GameCube 2.6 (split and relinked from 2.7's symbols, see [gc26-differences.md](gc26-differences.md))
- `GC_2_7`: GameCube 2.7 (220 of 253 units from the GC 2.x sources in `gc2/`, see
  [gc27-differences.md](gc27-differences.md))

`gc2/` is NWPlayer123's GC 2.x decompilation (CC0), vendored with
`git subtree` at 888bd76d. 1.3.2 and 2.7 build its sources with its own configuration (`gc2/config/<version>`), and
the link takes the split original for a unit that does not match; the SHA-1 of the result is the check. Its Python
tools are not used: `mwcc-tools` builds its library tree (`gc2/lib`, the Pro 5 MSL with the Pro 5.3 updater's
Windows and Mac groups; tool `lib-2x`, about 8 minutes the first time). The oracle builds of 1.3.2 and 2.7 stay on the
split executables of `config/<version>` (2.7's can also be built from the sources alone, see [oracle.md](oracle.md)).
`ninja build/GC_2_7/gc2/source-only/mwcceppc.exe` links 2.7's compiler from the sources alone, the units that do not
match too ([gc27-differences.md](gc27-differences.md)).
For 2.6 and 2.7, the default build also makes that compiler, and the oracle, as WebAssembly (with clang). `mwcc-tools`
then runs them in process in place of the executables ([wasm.md](wasm.md)).

## Dependencies

- Rust (the version in `rust-toolchain.toml`) and a C compiler to build `mwcc-tools`
- [ninja](https://github.com/ninja-build/ninja)
- [unshield](https://github.com/twogood/unshield), to unpack the CodeWarrior Pro 5.3 updater
- For 2.6 and 2.7's WebAssembly builds: LLVM 22 or later (`clang`, `wasm-ld`) and Rust's `wasm32-unknown-unknown`
  target

macOS: `brew install ninja unshield`. Linux: `apt install ninja-build unshield`.
Windows: `winget install Ninja-build.Ninja`, and unshield through WSL (`wsl sudo apt install unshield`).

[wibo](https://github.com/decompals/wibo) runs the compilers and is downloaded automatically.

## Building

```sh
cargo build --locked --release -p mwcc-tools
target/release/mwcc-tools configure
ninja
```

On Windows, use `target/release/mwcc-tools.exe`. For another version, pass `configure --version GC_1_2_5n` (or
`GC_1_3`). The build needs no Python.

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

`mwcc-tools verify` builds and checks all nine versions and each Matching source's functions.

## Diffing

Open the project in [objdiff](https://github.com/encounter/objdiff) after building.

## Project structure

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
