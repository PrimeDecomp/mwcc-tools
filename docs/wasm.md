# The GC 2.7 compiler as WebAssembly

The GC 2.7 compiler can be built from the vendored `gc2/` sources for wasm32 and run inside `mwcc-tools`. A run
starts no process and needs no Metrowerks or Windows binary. The build still reads MSL's headers from the downloaded
library tree, `gc2/lib`. It is opt-in: the normal build and the oracle do not use it.

```text
mwcc-tools configure --version GC_2_7
ninja gc2/lib/ok.pro53
mwcc-tools wasm-build GC_2_7
cargo build --release -p mwcc-tools --features wasm
mwcc-tools corpus --version GC_2_7 --canaries DIR --wasm build/GC_2_7/wasm/mwcceppc.wasm
```

`wasm-build` needs LLVM's `clang` and `wasm-ld` (`--clang PATH`, `--wasm-ld PATH`; it was made with clang 23). It
compiles each `gc2` source with the defines, include paths and `-align` that the build gives it, links
`build/GC_2_7/wasm/mwcceppc.wasm` and takes about 4 seconds. `corpus --wasm MODULE` runs the module in the oracle's
place and compares it with the original compiler.

## Status

* The canary corpus gives 2313/2313 identical: objects, messages and exit codes.
* A canary takes 5 to 8 ms in the module. Compiling the module (wasmtime's Cranelift) takes 1.5 to 2 s once per
  process. The whole canary comparison takes 18 s with the module and 26 s with the source-only executable. Both
  times include the original compiler's runs.
* Folding floating-point constants matches the original on 40,000 random `+ - * /` of doubles, 20,000 of floats and
  20,000 of converted integers. Before the x87 emulation of folding (below), 2 of the 40 files of doubles differed in
  `.sdata2`.
* Metroid Prime 2: Echoes (`--prime`) has not been tried.

## How it is built

`crates/mwcc-oracle-core/src/wasm_build.rs` compiles 230 `gc2` sources and 6 of `wasm/src`, and then links them.

* Left out: the startup in `src/runtime` (except `setupargs.c`), `LicenseImports.c`, and MSL's x86 assembly:
  `setjmp_x86.c`, `ansifp_x86.c` and `extras.c`.
* `wasm/src` replaces them:
  * `start.c` is the entry point `_start`.
  * `sjlj.c` and `sjlj_tag.s` give `setjmp`/`longjmp` on WebAssembly exceptions. They are the standard exceptions
    (`-wasm-use-legacy-eh=false`), which wasmtime runs.
  * `x87.c` emulates `__dec2num` and `__num2dec`, MSL's decimal conversions on the x87.
  * `msl_extras.c` has the functions of `extras.c` and MSL's memory copies in C.
  * `new.cpp` has `operator new`, and `platform.c` has the license calls (always granted).
* `wasm/include` has the C headers clang lacks (`stdarg.h`, `setjmp.h`). It also has Windows headers made from gc2's
  `include/compiler/win32.h` plus `win32_extra.h`, the few declarations MSL's Win32 layer takes from the SDK. No SDK
  header is used.
* MSL's `long double` is a double (`-mlong-double-64`). gnu89 `extern inline` functions are defined in each unit, so
  the link takes any one copy (`--allow-multiple-definition`).
* Some sources are adapted in a copy under `build/GC_2_7/wasm/adapted`. `gc2/` is not changed.
  * A function called through a pointer of another type traps in wasm, where x86 just ignores the extra argument.
    That happens with `CPrepTokenizer.c`'s character handlers (211, declared `(void)`, called with the character) and
    `Parameter.c`'s `CompareParam_SetString`. Both are given the pointer's parameters. clang's
    `-Wcast-function-type-mismatch` finds no others.
  * Constant folding, in `CMachine.c` (below).

The module's imports are 63 Win32 functions of kernel32 and version.dll. MSL's Win32 layer and the driver call them.

## Floating point: the x87

The original compiler runs on the x87, and two of its behaviors reach its output:

* `__dec2num` reads each floating-point literal. It runs `fbld` and multiplies or divides by powers of ten, all in
  80-bit arithmetic. `__num2dec` writes floating-point values in messages and listings.
  * `x87.c` does both on a software 80-bit number, with 64-bit significands, rounding to nearest even and the
    original's tables of powers of ten.
  * Checked against the real x87 code of `ansifp_x86.c` on 6 million random inputs: no difference.
* `__dec2num` sets the x87 to 64-bit precision (`or cw, 0x300`) and never restores it. So, from the first literal
  on, each `+ - * /` the compiler computes on doubles is rounded twice: first to 64 bits, then to 53 when it is stored.
  * Before any literal is read, the x87 is at Windows's 53 bits, and a result is rounded once.
  * `CMach_CalcFloatDiadic` (folding) and `CMach_FloatReciprocal` call `__x87_double` instead. It rounds as the x87
    does and switches to double rounding once `__dec2num` has run.
  * Checked against a real x87 at 64-bit precision on 8 million operations: no difference. 5,439 of them round
    differently at 53 bits.

Other floating-point code in the compiler also rounds once in wasm, where the x87 may round twice. This includes its
heuristics' `float` and `double` arithmetic and conversions. No difference has been seen from it. The corpus is what
checks it.

## How it runs

`crates/mwcc-wasm` runs the module with wasmtime 49 and needs its `gc` feature for exceptions.

* The module is compiled once. Each run instantiates it again, because the compiler keeps its state in globals.
* The host implements the 63 imports with Win32 semantics, in Rust:
  * Files and directories use `std::fs`, relative to the run's directory. `FindFirstFileA` matches case-insensitively
    and returns its matches in sorted order.
  * The standard handles are captured. `console` keeps both streams in the order they were written, as the oracle's
    log does.
  * The command line and environment are the caller's. `GetFullPathNameA` normalizes paths the Windows way and
    leaves verbatim (`\\?\`) paths as they are. The run's directory is given without the verbatim prefix, as a
    process's current directory has it.
  * There is one thread: thread-local slots and no-op critical sections.
  * `GlobalAlloc` has its own heap above the module's memory, because MSL's allocator gets its pools from it.
  * Times are UTC.
  * `CreateProcessA` fails, because nothing in a compile calls it. `FindResourceA` fails with 1813, as the original
    does.
* The driver checks its own executable and reads its version information. The run names a program path (`program`,
  which is the oracle's path in `corpus`). Its `VS_VERSIONINFO` is `source_only::compile_resources`'s, from gc2's
  `mwcceppc.rc`.
* `ExitProcess` ends the run with its status.

Not done yet:

* The corpus `--timeout` is not applied to the module. wasmtime's epoch interruption would provide it.
* Only Windows has been tried. A Unix host turns `\` into `/`, but case-insensitive lookups and drive letters are
  not handled. The overlay headers rely on a case-insensitive file system (`WINDEF.H` and `windef.h` are one file).
* There are no trace or oracle hooks yet (below).

## Hooks: the module as an oracle

The source-built oracle ([oracle.md](oracle.md#gc-27-without-the-original-compiler)) links the Rust hooks into the
source-only executable. It maps `config/GC_2_7/symbols.txt`'s names to gc2's by address, renames each hooked function
`<hook>__retail`, and redirects calls to the wrapper. The same design suits the module, with three changes:

1. **Link by name with `wasm-ld --wrap`.** wasm-ld redirects calls to `__wrap_X` and gives the original as `__real_X`.
   The hook table that `source_oracle` already prints (`hook` = gc2 function) becomes `--wrap=simplifygraph` and so
   on, plus small aliases from the hooks' `fn_` names to `__wrap_`/`__real_` names. The rename of COFF objects is not
   needed. Unlike ELF's `--wrap`, wasm-ld's also redirects calls from the function's own object (checked with clang 23),
   and so from inside its own body too. Where the oracle keeps a recursive call retail, the adapted copy calls
   `__real_X` there. Variables the hooks read are named by gc2's symbols. Its statics need a global alias, which the adapted
   copies can add, as `__x87_double`'s prototype is added now.
2. **Build the hooks crate for wasm32-unknown-unknown.** It is `no_std` and reads the compiler's structures by
   offset. wasm32 is ILP32, and the build packs structures as MWCC's `-align` does, so most offsets carry over. A
   check of each offset the hooks use (`offsetof` in a C unit of the build, compared with the hooks' constants)
   should come first. Its only x86-specific code is 15 `naked_asm!` trampolines for variadic calls (the trace
   printf-style writers). In wasm, a C variadic call passes one pointer to its arguments, so these become plain Rust
   functions taking `(format, args)`.
3. **Write traces to the host.** The hooks' writer becomes an import (`oracle.trace(pointer, length)`), so a trace
   goes straight into the run's `Outcome` instead of a file. `corpus --all-traces` and `--trace-reference` then work
   in process.

As with the source-built oracle, the guarded probes, which are pinned to the original's bytes, do not apply, and so
the `pv.*` provenance streams do not either. In the module they could be made as source hooks at the same functions.
