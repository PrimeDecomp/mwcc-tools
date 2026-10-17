# GC 2.6 and 2.7 as WebAssembly

For GC 2.6 and 2.7, the compiler is built from the vendored `gc2/` sources for wasm32 and runs inside `mwcc-tools`.
This is the default for both versions. A compile starts no process and runs no Metrowerks or Windows binary.

`configure` adds two modules to the default targets when the version's gc2 configuration is there:

* `build/<version>/wasm/mwcceppc.wasm` is the compiler as it is;
* `build/<version>/wasm/oracle.wasm` is the oracle: the same compiler with the Rust hooks linked in.

```text
mwcc-tools configure --version GC_2_7
ninja
mwcc-tools corpus --version GC_2_7 --canaries DIR --exe build/compilers/GC/2.7/mwcceppc.exe
mwcc-tools corpus --version GC_2_7 --canaries DIR --all-traces
```

Wherever `mwcc-tools` runs a compiler (`runner`: the oracle commands and `corpus`), it runs a module in place of an
executable once the module is built:

* the stock compiler, `build/compilers/GC/<dir>/mwcceppc.exe`, runs `mwcceppc.wasm`;
* the oracle, `build/<version>/oracle/mwcceppc.exe`, runs `oracle.wasm`. The executable does not have to exist.

The module runs with the environment the process would get, including the trace options and the trace file. Set
`MWCC_IN_PROCESS=0` to run the executables instead. `corpus` always runs the original compiler (its reference side)
and `--trace-reference` as processes.

The build needs LLVM's `clang` and `wasm-ld`: version 22 or later, because they read the hooks' IR from Rust's LLVM 22.
It uses the tools on the path, or `CLANG` and `WASM_LD` at `configure` time. It also needs Rust's
`wasm32-unknown-unknown` target (`rustup target add wasm32-unknown-unknown`), and MSL's headers from the downloaded
library tree, `gc2/lib`. The 236 units compile in about 4 seconds. Cranelift compiles a module once per process
(1.5 to 2 seconds); a compile then takes 5 to 8 ms.

CI builds both modules for 2.6 and 2.7 on Linux, and compares them with the original compiler under wibo on
`tests/fixtures/oracle/sources`.

## Status

* Canaries (2313): both stock modules give 2313/2313 identical with the original compilers: objects, messages and
  exit codes.
* Traces: on the canaries, the oracle modules' traces (`--all-traces`) match those of the x86 oracles (2.6's,
  split from the original, and 2.7's source-built one) on 2308/2313. The other 5 differ in one field, `st.block`'s
  `weight`. That is a block's `loopWeight`, read before anything sets it, so it shows what the heap had there. The
  x86 traces show a stale pointer.
* Floating point: folding matches the original on 40,000 random `+ - * /` of doubles, 20,000 of floats and 20,000
  of converted integers (see below).
* Metroid Prime 2: Echoes (`corpus --prime`, 2.7): 948/950 identical. The 2 that differ are known limitations:
  * `src\Runtime\Gecko_ExceptionPPC.cp`, `extab`. `Exceptions.c` builds its action records in a structure with
    padding (`AACCC`, `AACC`) on the stack, and the original writes the padding bytes as the stack had them
    (`1a 00`, `9a 00`, `ff ff`, depending on the call path). The module's stack holds other bytes.
  * `src\MetroidPrime\Enemies\CSandworm.cpp`, `UpdateEyes`: 1420 vs 1416 bytes. Peephole's `L_S` (eliminate a store
    of a value just loaded from the same place) checks the stores in between for overlap. For a store to another
    object, it compares `defInstrOffset` and `scanOffset`, which only a store to the same object sets. The original
    compares two stack slots (`[esp+0xc]`, `[esp+0x10]`) that earlier calls left behind: it deletes the first of three
    `stfs` back to where they were loaded, and keeps the other two. The module deletes none. Traces of both oracles
    agree up to there.

## How it is built

`crates/mwcc-oracle-core/src/wasm_build.rs` has the edges of the stock module: 230 gc2 sources and 6 of `wasm/src`.

* Each source is compiled with the defines, include paths and `-align` (`-fpack-struct`) the build gives it, plus
  `-fshort-enums`. MWCC's enums are as small as their values allow (`enumsalwaysint` off), and the hooks read
  structures with that layout.
* Left out: the startup in `src/runtime` (except `setupargs.c`), `LicenseImports.c`, and MSL's x86 assembly
  (`setjmp_x86.c`, `ansifp_x86.c` and `extras.c`).
* `wasm/src` replaces them:
  * `start.c` is the entry point `_start`.
  * `sjlj.c` and `sjlj_tag.s` give `setjmp`/`longjmp` on WebAssembly exceptions. They are the standard exceptions
    (`-wasm-use-legacy-eh=false`), which wasmtime runs.
  * `x87.c` emulates the x87 (below).
  * `msl_extras.c` has the functions of `extras.c` and MSL's memory copies in C.
  * `new.cpp` has `operator new`, and `platform.c` has the license calls (always granted).
* `wasm/include` has the C headers clang lacks (`stdarg.h`, `setjmp.h`). It also has Windows headers made from gc2's
  `include/compiler/win32.h` plus `win32_extra.h`, the few declarations MSL's Win32 layer takes from the SDK. No SDK
  header is used. `wasm/include-case` has the other spellings the sources use (`windef.h`, `Windows.h`, ...), for
  case-sensitive file systems.
* MSL's `long double` is a double (`-mlong-double-64`). gnu89 `extern inline` functions are defined in each unit, so
  the link takes any one copy (`--allow-multiple-definition`).
* What WebAssembly needs of gc2's sources is in gc2 itself, under `#ifdef __wasm__`. MWCC builds of them are
  unchanged.
  * A function called through a pointer of another type traps in wasm, where x86 just ignores the extra argument.
    That happens with `CPrepTokenizer.c`'s character handlers (declared `(void)`, called with the character) and
    `Parameter.c`'s `CompareParam_SetString`. `__wasm__` gives them the pointer's parameters. clang's
    `-Wcast-function-type-mismatch` finds no others.
  * Constant folding goes through `CMACH_FOLD` and `CMACH_RECIPROCAL` (`include/compiler/CMachine.h`).

The module's imports are 63 Win32 functions of kernel32 and version.dll. MSL's Win32 layer and the driver call them.

### The oracle

`crates/mwcc-oracle-core/src/wasm_oracle.rs` (`mwcc-tools source-only VERSION --oracle --wasm`, a ninja edge) links
`oracle.wasm`.

* `build-hooks --wasm` builds the hooks, and the version's frontend hooks, for `wasm32-unknown-unknown` as LLVM IR.
  `trace_output.rs` and the naked trampolines are x86 only. In wasm, a C variadic call passes one pointer to its
  arguments, and the hooks take it as that.
* Hook names are mapped to gc2's names by address, as in the source-built oracle
  ([oracle.md](oracle.md#gc-27-without-the-original-compiler)). The units that define a hooked function are compiled
  to IR (`-disable-llvm-passes`) and edited:
  * the function becomes `<hook>__retail`, global, and keeps its calls to itself;
  * the unit's other references go to the Rust wrapper;
  * a global function of another name becomes a thunk to the wrapper, for the other units' calls.
* A static variable the hooks read gets a global alias. A static function gets a global entry. The Rust IR then
  names gc2's symbols, with offsets as constant addresses (`copts+0xf8`). A name that the hooks and gc2 both define
  (`initheaps`) is renamed on the Rust side.
* Where an x86 hook finds a variable in the retail function's machine code (the frame layout, the colouring graph's
  globals, the scheduler's machine and opcode table), the wasm build reads it by gc2's name.
* `wasm/src/oracle.c` has the trace writer (`MWCC_ORACLE_TRACE_FILE`, or stderr), the variadic entry of
  `IroDump_Print`, and the source-built oracle's stand-ins for the probes' islands.
* The link fails if a hook and its function differ in signature (in wasm, that call would trap). It also fails if
  the module imports anything the stock module does not.

As with the source-built oracle, the guarded probes are not installed, so the `pv.*` provenance streams are absent.
Two scalarization bindings, which need the x86 caller's EBX and return address, are gaps.

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

`crates/mwcc-wasm` runs a module with wasmtime 49 and needs its `gc` feature for exceptions.
`crates/mwcc-oracle-core/src/in_process.rs` maps executables to modules, keeps each compiled module (until its file
changes), and runs each compile on a thread of its own.

* The module is compiled once. Each run instantiates it again, because the compiler keeps its state in globals.
* The host implements the 63 imports with Win32 semantics, in Rust:
  * Files and directories use `std::fs`, relative to the run's directory. `FindFirstFileA` matches case-insensitively
    and returns its matches in sorted order.
  * On a Unix host, the compiler sees the file system as drive `Z:`, as under wibo: `/home/x/a.c` is `Z:\home\x\a.c`.
    Other lookups follow the host's file system, so there they are case-sensitive. Checked on Linux (WSL) with the
    fixture corpus, for both versions and both modules.
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
* The driver checks its own executable and reads its version information. The run names the executable's path as
  its program. Its `VS_VERSIONINFO` is `source_only::compile_resources`'s, from gc2's `mwcceppc.rc`.
* `ExitProcess` ends the run with its status.
* The timeout is wasmtime's epoch interruption, ticked every 10 ms. A cancelled run stops the same way.
