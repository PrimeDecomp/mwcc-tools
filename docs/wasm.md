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
  * On Windows, a run lists each directory it looks in once, and the listing answers whether a file is there and what
    it is: the compiler looks each header up in every include directory, and a query of the file system there opens
    the file. Anything the run creates, writes or deletes drops the listings. This took a compile of an Echoes unit
    (`CWorldTransManager.cpp`) from about 2.7 s to 0.85 s, as long as the executable takes.
  * A handle gets the lowest free value, as on Windows. The compiler keeps file handles in 16-bit reference numbers,
    and values that only grew passed 0x8000 in a large precompile.
  * On a Unix host, the compiler sees the file system as drive `Z:`, as under wibo: `/home/x/a.c` is `Z:\home\x\a.c`.
    Other lookups follow the host's file system, so there they are case-sensitive. Checked on Linux (WSL) with the
    fixture corpus, for both versions and both modules.
  * The standard handles are captured. `console` keeps both streams in the order they were written, as the oracle's
    log does.
  * The command line and environment are the caller's. `GetFullPathNameA` normalizes paths the Windows way and
    leaves verbatim (`\\?\`) paths as they are. The run's directory is given without the verbatim prefix, as a
    process's current directory has it.
  * There is one thread: thread-local slots and no-op critical sections (the x86 oracle's MSL calls them).
  * `GlobalAlloc` has its own heap above the module's memory, because the allocator gets its pools from it. As
    Windows's heap, it reuses freed memory (the smallest free block that fits, neighbours merged) and grows a block in
    place where it can: the compiler grows its buffers by `GlobalReAlloc` in small steps, and blocks that always moved
    took a compile with `-g` of a large context to almost 4 GiB (76 MB now).
  * Times are UTC.
  * `CreateProcessA` fails, because nothing in a compile calls it. `FindResourceA` fails with 1813, as the original
    does.
* The driver checks its own executable and reads its version information. The run names the executable's path as
  its program. Its `VS_VERSIONINFO` is `source_only::compile_resources`'s, from gc2's `mwcceppc.rc`.
* `ExitProcess` ends the run with its status.
* The timeout is wasmtime's epoch interruption, ticked every 10 ms. A cancelled run stops the same way.

## A project's build: `mwcc-tools run`

`mwcc-tools run [sjiswrap.exe] EXE ARGS...` is a wrapper in wibo's place, for a dtk-template project's
`configure.py --wrapper`, which puts it before every compiler and linker command. A link or copy of `mwcc-tools` named
`mwcc-run` is the same command in one path. dtk-template writes the configure arguments back into `build.ninja`
unquoted, so a wrapper with arguments breaks when ninja runs configure again.

```text
mklink /H target\release\mwcc-run.exe target\release\mwcc-tools.exe
python configure.py --wrapper F:\...\mwcc\target\release\mwcc-run.exe
```

* An EXE with the bytes of `build/compilers/GC/<dir>/mwcceppc.exe` (2.6 or 2.7), with that version's module built, runs
  in process. It gets the wrapper's arguments, directory and environment as they are, and writes the object and the
  `-MMD` `.d` file itself. The wrapper prints its stdout and stderr and exits with its status. The modules are
  `MWCC_ORACLE_ROOT`'s, or else the checkout the executable was built in.
* Anything else runs as a process: natively on Windows, through wibo elsewhere. That covers the linker and the
  other compilers.
* `sjiswrap.exe` is skipped for a compiler that runs in process. The sources are read as they are, so this is only
  right for sources that are all ASCII.
* A process loads the module from `mwcceppc.cache/` in about 7 ms. A trivial unit takes 24 ms, against 16 ms for the
  executable and 20 ms with sjiswrap. With no artifact, each process compiles the module, which takes 3 to 3.5 s with
  24 at once. They all succeed and leave one artifact.

Metroid Prime 2: Echoes (2026-10-18, Windows, 24 jobs, from clean, both runs of each):

| | USA (G2ME01) | PAL (G2MP01) |
|---|---|---|
| GC/2.7 objects identical | 950 / 951 | 733 / 734 |
| GC/2.7 `.d` files identical | 951 / 951 | 734 / 734 |
| GC/1.3.2 and 1.2.5n (run as processes) identical | 112 / 112 | 111 / 111 |
| Hash checks; linked binaries identical | pass (87 files); DOL, 86 RELs | pass (DOL); DOL |
| Full build, executable | 60.9 s, 61.8 s | 44.6 s, 42.0 s |
| Full build, `mwcc-run` | 41.0 s, 35.8 s | 25.5 s, 30.4 s |
| A GC/2.7 compile on average, executable | 1.31 s | 1.15 to 1.23 s |
| A GC/2.7 compile on average, `mwcc-run` | 0.63 to 0.72 s | 0.56 to 0.63 s |

The object that differs is `Gecko_ExceptionPPC.o`, which is not linked: extab padding taken from a stack address (see
[Status](#status)). The executable writes different bytes from one run to the next (`6f 00`, `2f 01`, `f6 00`,
`53 00`), and the module always writes `a5 00`. One unit run on its own is about 10% slower in process (1.65 s against 1.46 s).

## Parse once, compile many: `persist.wasm`

`build/<version>/wasm/persist.wasm` is the stock module with `CPrep.c` and `CParser.c` compiled again with
`MWCC_PERSIST`, and its stack pointer exported. `mwcc_wasm::Compiler::persistent` runs it on a source that
`persistent_source` makes: a context (the headers and declarations many candidates share), a `;`, the line
`#define MWCC_PERSIST_MARKER 1` and a slot. The context is parsed once. Each `Persistent::compile` writes a candidate
into the slot and gives its exit status, its object and its messages, as a compile of the same text would give them.
mwdec's search loop compiles many candidates after one context, as `persist.rs` did with the executable under a
debugger.

* The module's call stack is wasmtime's, on the host's stack: a copy of the memory cannot bring back the frames that
  were live at the marker. gc2 keeps them live and unchanged instead. The checkpoint is in `C_Compiler`, the one frame
  live both at the marker and after the object is built:
  * the marker's `#define`, read by the `lex()` of `cparser`'s top-level loop, unwinds the parser to `C_Compiler`
    (`longjmp`). `lex` keeps nothing across `preprocessor()`, so a new `lex()` reads on as the old one would have. The
    `;` before the marker makes sure the top level reads it: after a function's definition, among others, the parser
    reads a token ahead;
  * `C_Compiler` calls `persist_checkpoint` (the host) and `cparser_resume`, the rest of `cparser`. Once the object is
    built (`ObjGen_Finish`), or the compile failed, `persist_done` gives the result to the host, and a `longjmp`
    starts the next candidate in `C_Compiler`. The cleanups, the driver's tail and `ExitProcess` never run for a
    candidate.
* The first `persist_checkpoint` snapshots the module: its memory, its stack pointer (its one mutable global) and what
  the host keeps of the run (`GlobalAlloc`'s heap, TLS slots, the last error, the console, the find handles; no file
  may be open). Each later one restores it, and zeroes the memory above it that the last candidate's heap used: fresh
  memory reads as zero, and the compiler reads uninitialized heap. The restore is done before the next candidate is
  asked for.
* A run lives on a thread of its own, which owns its store; the imports wait on channels between candidates. Each
  candidate has its own timeout.
* The exit status is the driver's: 0, 1 when the compile failed, 2 when it failed after a user break (too many
  errors), as `CompileFilesInProject` returns. The messages are the compiler's; the driver's tail (`User break,
  cancelled...`, `Errors caused tool to abort.`) is not written.
* A timeout or a trap ends the run: `compile` fails and the `Persistent` is no longer `alive()`; start another.
* Checked against the stock module (`crates/mwcc-wasm/examples/persist_bench.rs`), both versions, on the same text
  and (`PLAIN=1`) on the context and the candidate alone, as a project builds them:
  * Echoes' `CWorldTransManager.cpp` cut into 83 candidates (its whole body, the body without each of its 38
    top-level declarations, each alone, a syntax error, a `#pragma`, static initializers, 300 functions), for 3
    rounds;
  * mwdec's `persist-bench --mixed` candidates after `CStateManager.hpp` and `CActor.hpp`;
  * contexts that end in a function's definition, a class, a template, a macro;
  * C (two MusyX units, also with `-w all -w error` and `-sym on`, and `-ansi strict` on a small unit).

  Identical objects and exit statuses in every case, and identical messages on the same text. With `-sym on`, an
  object records its source's modification time, so the reference must have the persistent source's; on the plain
  text its line numbers differ by the two lines before the slot.
* Measured (2.7, Windows): `CStateManager.hpp` + `CActor.hpp` context, 120 mixed candidates: 1.9 ms per candidate (a
  restore of 15 MB, 1.0 ms, and the compile, 1.5 ms), against 234 ms for a compile of the module. (mwdec's debugger
  path measured 17.7 ms per candidate on these candidates, before Echoes' headers moved, with its own flags.)
  `CWorldTransManager.cpp`'s candidates: from 0.3 ms (a single declaration that fails) to 170 ms (its whole body),
  against about 650 to 800 ms; the start (the context and the snapshot) costs 0.3 to 0.6 s.
