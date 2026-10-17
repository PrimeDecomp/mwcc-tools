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
`wasm32-unknown-unknown` target (`rustup target add wasm32-unknown-unknown`). Nothing is downloaded: the C library is
`wasm/libc`'s, not MSL's. The 203 units compile in about 4 seconds. Cranelift compiles a module once per process
(1.5 to 2 seconds); a compile then takes 5 to 8 ms.

CI builds both modules for 2.6 and 2.7 on Linux, and compares them with the original compiler under wibo on
`tests/fixtures/oracle/sources`.

## Status

* Canaries (2313): both stock modules give 2313/2313 identical with the original compilers: objects, messages and
  exit codes.
* Traces: on the canaries, the oracle modules' traces (`--all-traces`) match those of the x86 oracles (2.6's,
  split from the original, and 2.7's source-built one) on 2313/2313. A block's `loopWeight` (`st.block`'s `weight`)
  is read before anything sets it, so it shows what the heap had there; `wasm/libc`'s allocator places blocks as
  MSL's does, so it shows the same stale value.
* Floating point: folding matches the original on 40,000 random `+ - * /` of doubles, 20,000 of floats and 20,000
  of converted integers (see below).
* Metroid Prime 2: Echoes (`corpus --prime`, 2.7): 949/950 identical. The one that differs depends on the host:
  * `src\Runtime\Gecko_ExceptionPPC.cp`, `extab`. `Exceptions.c` builds its action records in structures with
    padding (`AACCC`, `AACC`) on the stack, and the original writes the padding bytes as the stack had them. For the
    catch record here, that is the high half of a stack address that `Memory_NewHandle` pushed (`lea eax,[esp]`): `1a
    00` on Windows (the stack's base is `0x1b0000`), `ff 7f` under wibo. The module writes the high half of its own
    stack's address (`a5 00`).
* Uninitialized reads the module reproduces:
  * Peephole's `L_S` (eliminate a store of a value just loaded from the same place) checks the stores in between for
    overlap. For a store to another object, it compares `defInstrOffset` and `scanOffset`, which only a store to the
    same object sets. The original then compares two stack slots (`[esp+0xc]`, `[esp+0x10]`). The second holds the
    `nop` pointer that `computeinstructionpredecessors` left there, so the stores never overlap. Under `__wasm__` the
    offsets start as 0 and `0x7FFFFFFF`, which compare the same way. In Echoes' `CSandworm::UpdateEyes`, this deletes
    the first of three `stfs` that store values back to where they were loaded.

## How it is built

`crates/mwcc-oracle-core/src/wasm_build.rs` has the edges of the stock module: 190 gc2 sources, 6 of `wasm/src` and
the 7 of `wasm/libc`.

* Each source is compiled with the defines, include paths and `-align` (`-fpack-struct`) the build gives it, plus
  `-fshort-enums`. MWCC's enums are as small as their values allow (`enumsalwaysint` off), and the hooks read
  structures with that layout.
* Left out: `src/msl` and `src/runtime` (MSL and its startup, compiled from the downloaded `gc2/lib`), and
  `LicenseImports.c`.
* `wasm/libc` replaces MSL (below, [The C library](#the-c-library)).
* `wasm/src` replaces the rest:
  * `start.c` is the entry point `_start`.
  * `sjlj.c` and `sjlj_tag.s` give `setjmp`/`longjmp` on WebAssembly exceptions. They are the standard exceptions
    (`-wasm-use-legacy-eh=false`), which wasmtime runs.
  * `x87.c` emulates the x87 (below).
  * `new.cpp` has `operator new`, and `platform.c` has the license calls (always granted).
* `wasm/include` has the C headers clang lacks (`stdarg.h`, `setjmp.h`). It also has Windows headers made from gc2's
  `include/compiler/win32.h` plus `win32_extra.h`, the few declarations the C library takes from the SDK. No SDK
  header is used. `wasm/include-case` has the other spellings the sources use (`windef.h`, `Windows.h`, ...), for
  case-sensitive file systems.
* `long double` is a double (`-mlong-double-64`). gnu89 `extern inline` functions are defined in each unit, so
  the link takes any one copy (`--allow-multiple-definition`).
* What WebAssembly needs of gc2's sources is in gc2 itself, under `#ifdef __wasm__`. MWCC builds of them are
  unchanged.
  * A function called through a pointer of another type traps in wasm, where x86 just ignores the extra argument.
    That happens with `CPrepTokenizer.c`'s character handlers (declared `(void)`, called with the character) and
    `Parameter.c`'s `CompareParam_SetString`. `__wasm__` gives them the pointer's parameters. clang's
    `-Wcast-function-type-mismatch` finds no others.
  * Constant folding goes through `CMACH_FOLD` and `CMACH_RECIPROCAL` (`include/compiler/CMachine.h`).

The module's imports are 56 Win32 functions of kernel32 and version.dll. The C library and the driver call them.

### The C library

`wasm/libc` is a C library written for the module, from the Metroid Prime 2: Echoes decompilation's MSL
([PrimeDecomp/echoes](https://github.com/PrimeDecomp/echoes), `src/Runtime`, CC0), changed to do what CodeWarrior
Pro 5.3's Win32 MSL does wherever the compiler's output or its traces can tell. It uses gc2's own C headers
(`include/libc`), so its structures have their layout.

* `alloc.c`: MSL's allocator (`malloc`, `free`, `realloc`, `calloc`), with the Win32 build's settings. The heap's
  layout reaches the traces (uninitialized reads, pointer order in hash tables), so blocks are placed as MSL places
  them:
  * variable-size pools from `GlobalAlloc`, each the size plus 24, rounded up to 4 KiB, and at least 64 KiB; 8-byte
    alignment, and no piece smaller than 80 bytes is split off;
  * four fixed-size pools, of 12, 20, 36 and 68 bytes, in blocks of 4000 bytes, freed when no piece is in use.
* `string.c`: the string and memory functions, `qsort` (a heapsort, as MSL's: the order of equal elements shows),
  `strcmp` (the difference of the unsigned characters), `memcpy` (forward, by words from 32 bytes on), the
  character tables and `strerror`'s messages.
* `printf.c`: the `printf` family, with MSL's spellings (`(null)`, `NaN`, no `%a`).
* `scan.c`: `strtol`, `strtoul`, `strtod` (20 significant digits, read by `__dec2num`; below `DBL_MIN` is 0) and
  `sscanf`'s integer conversions.
* `stdio.c`: the files: `fopen`, `fwrite`, `fputs`, `setvbuf`, `fflush`, `fclose`, on the Win32 handles. The
  standard streams are static, buffers and the handle table are allocated as MSL allocates them, and text mode
  writes `
`.
* `runtime.c`: the startup (`_CRTStartup`: handles, environment, thread data and arguments, in MSL's order), the
  thread data (596 bytes, `errno` first), `atexit`/`exit`, `signal`/`raise`/`abort`, the time functions and
  `__assertion_failed`.
* `extras.c`: the Metrowerks extras the compiler calls (`_strnicmp`, `GetHandle`, ...) and MSL's word copies.

It was checked against the original compilers and the earlier module (built with Pro 5.3's MSL itself):

* the canaries, 2.6 and 2.7: 2313/2313 identical objects and messages, and 2313/2313 identical traces;
* the raw traces with the source arenas, every 40th canary: the same records, the addresses of statics apart;
* the module's imports: a subset of the earlier module's (no critical sections or thread-local slots);
* floating-point literals, of exponents from -340 to 310, and halfway cases: the same values as the original.

### The oracle

`crates/mwcc-oracle-core/src/wasm_oracle.rs` (`mwcc-tools source-only VERSION --oracle --wasm`, a ninja edge) links
`oracle.wasm`. Its objects are compiled again with `ORACLE_PROBES` defined, in `build/<version>/wasm/oracle-obj`
(below, [Probes](#probes)). The stock module's are compiled without it.

* `build-hooks --wasm` builds the hooks, and the version's frontend hooks, for `wasm32-unknown-unknown` as LLVM IR.
  `trace_output.rs` and the naked trampolines are x86 only. In wasm, a C variadic call passes one pointer to its
  arguments, and the hooks take it as that.
* Hook names are mapped to gc2's names by address, as in the source-built oracle
  ([oracle.md](oracle.md#gc-27-without-the-original-compiler)). The units that define a hooked function are compiled
  to IR (`-disable-llvm-passes`) and edited:
  * the function becomes `<hook>__retail`, global, and keeps its calls to itself;
  * the unit's other references go to the Rust wrapper;
  * a global function of another name becomes a thunk to the wrapper, for the other units' calls;
  * a function of the unit that the original lacks (MWCC inlined it everywhere), that the hooked function calls and
    that calls it back, gets a copy for it whose calls stay retail. In the original, those calls are the function's
    calls to itself, which the hook does not see. (`CInline_CopyNodes`'s `CInline_CopyExpressionSave` and
    `CInline_CopyEMemberInfo`.)
* A static variable the hooks read gets a global alias. A static function gets a global entry. The Rust IR then
  names gc2's symbols, with offsets as constant addresses (`copts+0xf8`). A name that the hooks and gc2 both define
  (`initheaps`) is renamed on the Rust side.
* Where an x86 hook finds a variable in the retail function's machine code (the frame layout, the colouring graph's
  globals, the scheduler's machine and opcode table), the wasm build reads it by gc2's name.
* `wasm/src/oracle.c` has the trace writer (`MWCC_ORACLE_TRACE_FILE`, or stderr), the variadic entry of
  `IroDump_Print`, and the source-built oracle's stand-in for the call arguments' probe island.
* The link fails if a hook and its function differ in signature (in wasm, that call would trap). It also fails if
  the module imports anything the stock module does not.

#### Probes

The x86 oracle installs guarded probes at instructions of the original's split objects. They read its registers and
write the `pv.*` provenance streams. The module has them in gc2's sources instead: `include/compiler/OracleProbes.h`
declares a call for each, which `ORACLE_PROBE(...)` makes only under `ORACLE_PROBES`. Each call passes the values the
probe reads, and the hooks give the records the original's site.

* The source arenas (`pv.arena.*`, `--source-arenas`): `CompilerTools.c`'s allocators report each request and commit.
  The original inlines `galloc` in `InitNameHash` and in `GetHashNameNode` and `GetHashNameNodeExport` (pairs 5 to 9);
  under `ORACLE_PROBES` they call `galloc_at`, which names the pair. The request's stack pointer, by which a commit
  is matched with its request, is the address of the allocator's local. gc2 defines the arena globals there.
* The source nodes (`pv.source.*`, `--source-nodes`): `parse_statement` reports each expression statement. A clone's
  `caller` (its return address in the original) is 0: nothing reads it.
* The register counters' structural sites (`pv.function.*`, `pv.category.*`, `pv.counter.change`): `CodeGen_Generator`
  reports a function's beginning and end, `set_last_exception_registers` and `open_fe_temp_registers` the ranges,
  and `Registers.c` and `colorinstructions` each reset of a counter. gc2 defines the counter globals in `Registers.c`.
* The register counters' increments (`pv.value.birth`): each is an `ALLOC_GPR()`, `ALLOC_FPR()`, `ALLOC_VR()` or
  `ALLOC_REG(c)` (`include/compiler/Registers.h`), which reports its file and line. The hooks look the original's
  site up in `crates/mwcc-oracle-hooks/src/counter_sites.rs`, which `mwcc-tools provenance counter-sites --write`
  generates (after `ninja all_source` for GC 2.7: see `crates/mwcc-oracle-core/src/counter_sources.rs`; a test
  checks it is fresh). It aligns each function's increments in the original with its writes by the line numbers of
  gc2's objects. The original inlines 23 functions at some or all of their calls, and their increments are then at
  the call: those calls go through `ORACLE_INLINED`, which reports the call's line too. A wrapper macro after each
  such function's definition makes its later calls so (`#define f(...) ORACLE_INLINED(f(__VA_ARGS__))`), and the
  calls before it are written `ORACLE_INLINED(f(...))`.
* The births' bindings (`pv.value.object`, `pv.value.materialization`, `pv.value.array`, `pv.value.call_result`):
  before each of the 13 increments that bind their birth, a call gives what the x86 probe reads in its registers and
  frame (an object and its VarInfo, an operand and its object, an array's promotion and the element's index, or a
  call, its output and its type). The table gives each site's bindings.
* The scalarization bindings (`pv.scalar.*`): `IRO_ScalarizeClassDataMembers` names the part whose temporary
  `create_temp_object` makes next.

The other streams' probes (expressions, dispatch, coalescing and call arguments' contexts) are not in gc2's sources
yet, so those streams are absent from the module's traces, or partial. A birth's `expression` and the counts a function's end gives of them
differ. Use `MWCC_IN_PROCESS=0` to run the x86 oracle for them.

`corpus --source-arenas` (or `--source-nodes`) traces those streams and compares them with `--trace-reference`'s, the
x86 oracle's. Addresses are masked, but not the original's sites. Between two other records, requests and commits are
compared as a multiset, without their numbers and the pool's free space: their order follows the order in which the
compiler evaluates a call's arguments, which C leaves unspecified (MWCC evaluates them from the last, clang from the
first: `pclabel(makepcblock(), makepclabel())` allocates the label first in the original). Nothing joins two traces
by these numbers. With `--all-traces`, the functions' beginnings, the counters' changes and ranges, the births
and their bindings (but for a birth's `expression`) and the scalarization bindings are compared too. The canaries reach
268 of the 574 increments, with Echoes 300 (20 of the 62 in inlined calls); Echoes' counter records compare equal too. On the canaries, for 2.6 and 2.7, the streams compare equal but for the inputs that stop on too many
errors. Those report a user break, and how far the compiler gets before it checks for one depends on the clock
(`CWUserBreak` every 8 ticks), in the original too.

## Floating point: the x87

The original compiler runs on the x87, and two of its behaviors reach its output:

* `__dec2num` reads each floating-point literal. It runs `fbld` and multiplies or divides by powers of ten, all in
  80-bit arithmetic. `__num2dec` writes floating-point values in messages and listings.
  * `x87.c` does both on a software 80-bit number, with 64-bit significands, rounding to nearest even and the
    original's tables of powers of ten.
  * Checked against the real x87 code of `ansifp_x86.c` on 6 million random inputs: no difference.
  * `__dec2num` scales its 18 digits by `10^n`. From `|n| = 384` on (a literal far beyond a double's range, such as
    `1e-380`), the original takes a power of ten past the end of its table, from whatever bytes follow it. `x87.c`
    uses the table's last entry instead, so such a literal can get another value. Literals of exponents between
    -340 and 310 stay within the table.
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
  * There is one thread: thread-local slots and no-op critical sections (the x86 oracle's MSL calls them).
  * `GlobalAlloc` has its own heap above the module's memory, because the allocator gets its pools from it.
  * Times are UTC.
  * `CreateProcessA` fails, because nothing in a compile calls it. `FindResourceA` fails with 1813, as the original
    does.
* The driver checks its own executable and reads its version information. The run names the executable's path as
  its program. Its `VS_VERSIONINFO` is `source_only::compile_resources`'s, from gc2's `mwcceppc.rc`.
* `ExitProcess` ends the run with its status.
* The timeout is wasmtime's epoch interruption, ticked every 10 ms. A cancelled run stops the same way.
