# GC 2.6 and 2.7 as WebAssembly

For GC 2.6 and 2.7, the compiler is built from the vendored `gc2/` sources for wasm32 and runs inside `mwcc-tools`.
This is the default for both versions. A compile starts no process and runs no Metrowerks or Windows binary.

`configure` adds the compiler module to the default targets when the version's gc2 configuration is there:
`build/<version>/wasm/mwcc.wasm`, the compiler with the oracle's Rust hooks linked in, which trace only when a
compile asks for a trace. `mwcceppc.wasm` (the compiler as it is), `oracle.wasm` and `persist.wasm` are also built,
for mwdec, which loads them itself; `mwcc-tools` needs only `mwcc.wasm` (`ninja build/<version>/wasm/mwcc.wasm`).

```text
mwcc-tools configure --version GC_2_7
ninja
mwcc-tools corpus --version GC_2_7 --canaries DIR --exe build/compilers/GC/2.7/mwcceppc.exe
mwcc-tools corpus --version GC_2_7 --canaries DIR --all-traces
```

Wherever `mwcc-tools` runs a compiler (the methods, `run` and `corpus`), it runs `mwcc.wasm` in place of an
executable once the module is built: the stock compiler (`build/compilers/GC/<dir>/mwcceppc.exe`), the oracle
(`build/<version>/oracle/mwcceppc.exe`) and a project's own copy of either (known by its size and SHA-1). The
executable does not have to exist. The module gets the environment the process would get, including the trace
options and the trace file. `MWCC_IN_PROCESS=0` runs the executables instead. `corpus` always runs the original
compiler (its reference side) and `--trace-reference` as processes.

The build needs LLVM's `clang` and `wasm-ld` 22 or later, because they read the hooks' IR from Rust's LLVM 22: on the
path, or `CLANG` and `WASM_LD` at `configure` time. It also needs Rust's `wasm32-unknown-unknown` target. Nothing is
downloaded: the C library is `wasm/libc`'s, not MSL's. The 203 units compile in about 4 seconds. Cranelift compiles a
module once per process (1.5 to 2 seconds); a compile then takes 5 to 8 ms.

CI builds GC 2.7's modules on Linux and publishes them with each release (`mwcc-wasm-GC_2_7-VERSION.zip`). The
comparison with the original compiler (the `corpus` commands above, on `tests/fixtures/oracle/sources`) is run
locally.

## Status

* Canaries (2313): both stock modules give identical objects, messages and exit codes to the original compilers, and
  the oracle modules' traces (`--all-traces`) match the x86 oracles'. A block's `loopWeight` (`st.block`'s `weight`)
  is read before anything sets it; `wasm/libc`'s allocator places blocks as MSL's does, so it shows the same stale
  value.
* Floating-point folding matches the original on 40,000 random `+ - * /` of doubles, 20,000 of floats and 20,000 of
  converted integers ([below](#floating-point-the-x87)).
* Metroid Prime 2: Echoes (`corpus --prime`, 2.7): 949/950 identical. The one that differs depends on the host:
  `src\Runtime\Gecko_ExceptionPPC.cp`'s `extab`. `Exceptions.c` builds its action records in structures with padding
  on the stack, and the original writes the padding as the stack had it: the high half of a stack address
  (`1a 00` on Windows, `ff 7f` under wibo, other bytes from one run to the next). The module writes its own stack's
  (`a5 00`). The object is not linked.
* An uninitialized read the module reproduces: peephole's `L_S` (drop a store of a value just loaded from the same
  place) compares `defInstrOffset` and `scanOffset` for a store to another object, which only a store to the same
  object sets. In the original they are stack slots whose values never overlap; under `__wasm__` they start as 0 and
  `0x7FFFFFFF`, which compare the same way. (In Echoes' `CSandworm::UpdateEyes`, this deletes the first of three
  `stfs` that store values back to where they were loaded.)
* A crash the module turns into an error: a precompiled header that declares a function under `#pragma section
  code_type ".init"` (Echoes' `libc/string.h` does so for `memset` and `memcpy`), and a unit that calls it. The
  original faults in `PPC_EABI_SetSection`; the module stops with an internal compiler error (`ObjGen_PPC_EABI.c`,
  line 2384). Both fail, so such units must be compiled with those headers left out of the precompiled header
  (mwdec's `split.rs` does).

## How it is built

`crates/mwcc-oracle-core/src/wasm_build.rs` has the edges of the stock module: 190 gc2 sources, 6 of `wasm/src` and
the 7 of `wasm/libc`.

* Each source is compiled with the defines, include paths and `-align` (`-fpack-struct`) the build gives it, plus
  `-fshort-enums`: MWCC's enums are as small as their values allow (`enumsalwaysint` off), and the hooks read
  structures with that layout.
* Left out: `src/msl` and `src/runtime` (MSL and its startup), and `LicenseImports.c`. `wasm/libc` replaces MSL
  ([below](#the-c-library)).
* `wasm/src` replaces the rest: `start.c` (the entry point `_start`); `sjlj.c` and `sjlj_tag.s` (`setjmp`/`longjmp`
  on WebAssembly's standard exceptions, `-wasm-use-legacy-eh=false`, which wasmtime runs); `x87.c` (below);
  `new.cpp` (`operator new`); `platform.c` (the license calls, always granted).
* `wasm/include` has the C headers clang lacks (`stdarg.h`, `setjmp.h`), and Windows headers made from gc2's
  `include/compiler/win32.h` plus `win32_extra.h`; no SDK header is used. `wasm/include-case` has the other
  spellings the sources use (`windef.h`, `Windows.h`, ...), for case-sensitive file systems.
* `long double` is a double (`-mlong-double-64`). gnu89 `extern inline` functions are defined in each unit, so the
  link takes any one copy (`--allow-multiple-definition`).
* What WebAssembly needs of gc2's sources is in gc2 itself, under `#ifdef __wasm__`; MWCC builds of them are
  unchanged.
  * A function called through a pointer of another type traps in wasm, where x86 ignores the extra argument:
    `CPrepTokenizer.c`'s character handlers and `Parameter.c`'s `CompareParam_SetString` get the pointer's
    parameters. clang's `-Wcast-function-type-mismatch` finds no others.
  * Constant folding goes through `CMACH_FOLD` and `CMACH_RECIPROCAL` (`include/compiler/CMachine.h`).

The module's imports are 56 Win32 functions of kernel32 and version.dll, called by the C library and the driver.

### The C library

`wasm/libc` is a C library written for the module, from the Metroid Prime 2: Echoes decompilation's MSL
([PrimeDecomp/echoes](https://github.com/PrimeDecomp/echoes), `src/Runtime`, CC0), changed to do what CodeWarrior
Pro 5.3's Win32 MSL does wherever the compiler's output or its traces can tell. It uses gc2's own C headers
(`include/libc`), so its structures have their layout.

* `alloc.c`: MSL's allocator with the Win32 build's settings. The heap's layout reaches the traces (uninitialized
  reads, pointer order in hash tables), so blocks are placed as MSL places them:
  * variable-size pools from `GlobalAlloc`, each the size plus 24, rounded up to 4 KiB, and at least 64 KiB; 8-byte
    alignment, and no piece smaller than 80 bytes is split off;
  * four fixed-size pools, of 12, 20, 36 and 68 bytes, in blocks of 4000 bytes, freed when no piece is in use.
* `string.c`: the string and memory functions, `qsort` (a heapsort, as MSL's: the order of equal elements shows),
  `strcmp` (the difference of the unsigned characters), `memcpy` (forward, by words from 32 bytes on), the character
  tables and `strerror`'s messages.
* `printf.c`: the `printf` family, with MSL's spellings (`(null)`, `NaN`, no `%a`).
* `scan.c`: `strtol`, `strtoul`, `strtod` (20 significant digits, read by `__dec2num`; below `DBL_MIN` is 0) and
  `sscanf`'s integer conversions.
* `stdio.c`: `fopen`, `fwrite`, `fputs`, `setvbuf`, `fflush`, `fclose`, on the Win32 handles. The standard streams
  are static, buffers and the handle table are allocated as MSL allocates them, and text mode writes CR LF.
* `runtime.c`: the startup (`_CRTStartup`: handles, environment, thread data and arguments, in MSL's order), the
  thread data (596 bytes, `errno` first), `atexit`/`exit`, `signal`/`raise`/`abort`, the time functions and
  `__assertion_failed`.
* `extras.c`: the Metrowerks extras the compiler calls (`_strnicmp`, `GetHandle`, ...) and MSL's word copies.

Against a module built with Pro 5.3's MSL itself, it gives the same objects, messages and traces on the canaries,
the same raw source-arena records (but the addresses of statics), and the same values for floating-point literals of
exponents -340 to 310.

### The oracle

`crates/mwcc-oracle-core/src/wasm_oracle.rs` (`mwcc-tools source-only VERSION --oracle --wasm`, a ninja edge) links
`oracle.wasm`. Its objects are compiled again with `ORACLE_PROBES` defined, in `build/<version>/wasm/oracle-obj`
([Probes](#probes)).

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
    that calls it back, gets a copy whose calls stay retail: in the original, those are the function's calls to
    itself, which the hook does not see (`CInline_CopyNodes`'s `CInline_CopyExpressionSave` and
    `CInline_CopyEMemberInfo`).
* A static variable the hooks read gets a global alias, and a static function a global entry. The Rust IR then names
  gc2's symbols, with offsets as constant addresses (`copts+0xf8`). A name both define (`initheaps`) is renamed on the
  Rust side.
* Where an x86 hook finds a variable in the retail function's machine code (the frame layout, the colouring graph's
  globals, the scheduler's machine and opcode table), the wasm build reads it by gc2's name.
* `wasm/src/oracle.c` has the trace writer (`MWCC_ORACLE_TRACE_FILE`, or stderr), the variadic entry of
  `IroDump_Print`, and the source-built oracle's stand-in for the call arguments' probe island.
* The link fails if a hook and its function differ in signature (in wasm, that call would trap), or if the module
  imports anything the stock module does not.

#### Probes

The x86 oracle installs guarded probes at instructions of the original's split objects; they read its registers and
write the `pv.*` provenance streams. The module has them in gc2's sources instead: `include/compiler/OracleProbes.h`
declares a call for each, which `ORACLE_PROBE(...)` makes only under `ORACLE_PROBES`. Each call passes the values the
probe reads, and the hooks give the records the original's site.

* Source arenas (`pv.arena.*`, `--source-arenas`): `CompilerTools.c`'s allocators report each request and commit.
  The original inlines `galloc` in `InitNameHash`, `GetHashNameNode` and `GetHashNameNodeExport` (pairs 5 to 9); under
  `ORACLE_PROBES` they call `galloc_at`, which names the pair. A request's stack pointer, by which a commit is matched
  with its request, is the address of the allocator's local.
* Source nodes (`pv.source.*`, `--source-nodes`): `parse_statement` reports each expression statement. A clone's
  `caller` (its return address in the original) is 0: nothing reads it.
* The register counters' structural sites (`pv.function.*`, `pv.category.*`, `pv.counter.change`):
  `CodeGen_Generator` reports a function's beginning and end, `set_last_exception_registers` and
  `open_fe_temp_registers` the ranges, and `Registers.c` and `colorinstructions` each reset of a counter.
* The counters' increments (`pv.value.birth`): each is an `ALLOC_GPR()`, `ALLOC_FPR()`, `ALLOC_VR()` or
  `ALLOC_REG(c)` (`include/compiler/Registers.h`), which reports its file and line. The hooks look the original's site
  up in `crates/mwcc-oracle-hooks/src/counter_sites.rs`, which `mwcc-tools provenance counter-sites --write`
  generates (after `ninja all_source` for GC 2.7: see `crates/mwcc-oracle-core/src/counter_sources.rs`; a test checks
  it is fresh). It aligns each function's increments in the original with its writes by the line numbers of gc2's
  objects. The original inlines 23 functions at some or all of their calls, and their increments are then at the
  call: those calls go through `ORACLE_INLINED`, which reports the call's line too (a wrapper macro after the
  function's definition, `#define f(...) ORACLE_INLINED(f(__VA_ARGS__))`, and `ORACLE_INLINED(f(...))` before it).
* The births' bindings (`pv.value.object`, `pv.value.materialization`, `pv.value.array`, `pv.value.call_result`):
  before each of the 13 increments that bind their birth, a call gives what the x86 probe reads in its registers and
  frame.
* Scalarization bindings (`pv.scalar.*`): `IRO_ScalarizeClassDataMembers` names the part whose temporary
  `create_temp_object` makes next.

The other streams' probes (expressions, dispatch, coalescing and call arguments' contexts) are not in gc2's sources
yet, so those streams are absent from the module's traces, or partial; a birth's `expression` and the counts a
function's end gives of them differ. `MWCC_IN_PROCESS=0` runs the x86 oracle for them.

`corpus --source-arenas` (or `--source-nodes`) compares those streams with `--trace-reference`'s, the x86 oracle's.
Addresses are masked, but not the original's sites. Between two other records, requests and commits are compared as
a multiset, without their numbers and the pool's free space: their order follows the order in which the compiler
evaluates a call's arguments, which C leaves unspecified (MWCC evaluates from the last, clang from the first). With
`--all-traces`, the functions' beginnings, the counters' changes and ranges, the births and their bindings (but a
birth's `expression`) and the scalarization bindings are compared too. The canaries reach 268 of the 574 increments,
Echoes 300. On the canaries the streams compare equal but for inputs that stop on too many errors: those report a
user break, and how far the compiler gets before it checks for one depends on the clock (`CWUserBreak` every 8
ticks), in the original too.

## Floating point: the x87

The original compiler runs on the x87, and two of its behaviors reach its output:

* `__dec2num` reads each floating-point literal, with `fbld` and multiplications or divisions by powers of ten, all
  in 80-bit arithmetic; `__num2dec` writes floating-point values in messages and listings. `x87.c` does both on a
  software 80-bit number (64-bit significands, round to nearest even, the original's tables of powers of ten); it
  matched the real x87 code of `ansifp_x86.c` on 6 million random inputs. From `|n| = 384` on (a literal far beyond a
  double's range, such as `1e-380`), the original reads a power of ten past the end of its table; `x87.c` uses the
  last entry, so such a literal can get another value.
* `__dec2num` sets the x87 to 64-bit precision and never restores it. So, from the first literal on, each `+ - * /`
  the compiler computes on doubles is rounded twice (to 64 bits, then to 53 when stored); before it, once.
  `CMach_CalcFloatDiadic` (folding) and `CMach_FloatReciprocal` call `__x87_double`, which rounds as the x87 does and
  switches to double rounding once `__dec2num` has run. It matched a real x87 on 8 million operations.

Other floating-point code in the compiler (its heuristics' arithmetic and conversions) rounds once in wasm where the
x87 may round twice. No difference has been seen from it; the corpus is what checks it.

## How it runs

`crates/mwcc-wasm` runs a module with wasmtime 49 (its `gc` feature, for exceptions).
`crates/mwcc-oracle-core/src/in_process.rs` maps executables to modules, keeps each compiled module (until its file
changes), and runs each compile on a thread of its own.

* The module is compiled once. Each run instantiates it again, because the compiler keeps its state in globals.
  Instantiating takes 0.1 ms; wasmtime's pooling allocator with copy-on-write memory made compiles no faster
  (`examples/instantiate_bench.rs`).
* The host implements the imports with Win32 semantics, in Rust:
  * Files and directories use `std::fs`, relative to the run's directory. `FindFirstFileA` matches
    case-insensitively and returns its matches in sorted order.
  * On Windows, a run lists each directory it looks in once, and the listing answers whether a file is there: the
    compiler looks each header up in every include directory. Anything the run creates, writes or deletes drops the
    listings. This took an Echoes unit from about 2.7 s to 0.85 s.
  * A handle gets the lowest free value, as on Windows: the compiler keeps file handles in 16-bit reference numbers,
    and values that only grew passed 0x8000 in a large precompile.
  * On a Unix host, the compiler sees the file system as drive `Z:`, as under wibo: `/home/x/a.c` is `Z:\home\x\a.c`.
  * Names are matched case-insensitively there too, as Windows and wibo match them (Echoes includes
    `Kyoto/CCRC32.hpp`; the file is `CCrc32.hpp`). A path that is there as spelled is used as it is. Otherwise each
    name that is not there as spelled becomes its directory's entry with that name in another case; from the first
    name that is not there in any case (a file to create), the rest is kept as given. This applies to every file
    call, including the `-MMD` `.d` file.
  * The standard handles are captured. `console` keeps both streams in the order they were written, as the oracle's
    log does.
  * The command line and environment are the caller's. `GetFullPathNameA` normalizes paths the Windows way and leaves
    verbatim (`\\?\`) paths as they are. The run's directory is given without the verbatim prefix.
  * There is one thread: thread-local slots and no-op critical sections.
  * `GlobalAlloc` has its own heap above the module's memory. As Windows's heap, it reuses freed memory (the smallest
    free block that fits, neighbours merged) and grows a block in place where it can: the compiler grows its buffers
    by `GlobalReAlloc` in small steps, and blocks that always moved took a `-g` compile of a large context to almost
    4 GiB (76 MB now).
  * Times are UTC. `CreateProcessA` fails (nothing in a compile calls it). `FindResourceA` fails with 1813, as the
    original does.
* The driver checks its own executable and reads its version information: the run names the executable's path as its
  program, and its `VS_VERSIONINFO` is `source_only::compile_resources`'s, from gc2's `mwcceppc.rc`.
* `ExitProcess` ends the run with its status.
* The timeout is wasmtime's epoch interruption, ticked every 10 ms. A cancelled run stops the same way.
* A run's memory is limited to 1 GiB (`MWCC_WASM_MEMORY_MB`, in MiB). A run that needs more stops with "out of
  memory: more than N MiB".

## A project's build: `mwcc-tools run`

`mwcc-tools run [sjiswrap.exe] EXE ARGS...` is a wrapper in wibo's place, for a dtk-template project's
`configure.py --wrapper`, which puts it before every compiler and linker command. A copy of `mwcc-tools` named
`mwcc-run` is the same command in one path (dtk-template writes the configure arguments back into `build.ninja`
unquoted, so a wrapper with arguments breaks when ninja runs configure again).

```text
copy target\release\mwcc-tools.exe target\release\mwcc-run.exe
python configure.py --wrapper <mwcc>\target\release\mwcc-run.exe
```

Copy it again after each `cargo build`: a hard link made before a rebuild keeps running the old executable, because
cargo replaces `mwcc-tools.exe` with a new file.

* An EXE that is the original 2.6 or 2.7 compiler, wherever it is, runs in process when that version's module is
  built. It is recognised by the size and SHA-1 in `config/<version>/config.json`, so the checkout needs no copy of the
  compilers; an executable of another size is never read. It gets the wrapper's arguments, directory and environment,
  and writes the object and the `-MMD` `.d` file itself. The wrapper prints its stdout and stderr and exits with its
  status. The modules and configs are `MWCC_ORACLE_ROOT`'s, or else those of the checkout the executable was built in.
* Anything else (the linker, the other compilers) runs as a process: natively on Windows, elsewhere through wibo,
  taken from the first of `MWCC_WIBO`, the project's own `build/tools/wibo` (relative to the build's directory), the
  checkout's `build/tools/wibo` and `wibo` on the path.
* `sjiswrap.exe` is skipped for a compiler that runs in process. The sources are read as they are, so this is only
  right for sources that are all ASCII.
* A process loads the module from `mwcceppc.cache/` in about 7 ms. With no artifact, each process compiles the
  module (3 to 3.5 s with 24 at once); they all succeed and leave one artifact.

On Metroid Prime 2: Echoes (USA and PAL, Windows, 24 jobs), every object but `Gecko_ExceptionPPC.o`
([Status](#status)) and every `.d` file is identical, the linked binaries pass their hash checks, and a full build
takes 25 to 41 s against 42 to 62 s with the executable (a GC 2.7 compile 0.6 s on average, against 1.2 s).

## Parse once, compile many: `persist.wasm`

`build/<version>/wasm/persist.wasm` is the stock module with `CPrep.c` and `CParser.c` compiled again with
`MWCC_PERSIST`, and its stack pointer exported. `mwcc_wasm::Compiler::persistent` runs it on a source that
`persistent_source` makes: a context (the headers and declarations many candidates share), a `;`, the line
`#define MWCC_PERSIST_MARKER 1` and a slot. The marker takes those two lines off the line counter, so the slot's lines
are numbered as right after the context: `-g` and `-sym on` objects and the messages equal a normal compile of the
context followed by the candidate. The context is parsed once; each `Persistent::compile` writes a candidate into
the slot and gives its exit status, object and messages, as a compile of the same text would.

* The module's call stack is wasmtime's, on the host's stack, so a copy of the memory cannot bring back the frames
  that were live at the marker. gc2 keeps them live and unchanged instead. The checkpoint is in `C_Compiler`, the one
  frame live both at the marker and after the object is built:
  * the marker's `#define`, read by the `lex()` of `cparser`'s top-level loop, unwinds the parser to `C_Compiler`
    (`longjmp`). `lex` keeps nothing across `preprocessor()`, so a new `lex()` reads on as the old one would have.
    The `;` before the marker makes sure the top level reads it (after a function's definition, among others, the
    parser reads a token ahead);
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
  errors). The messages are the compiler's; the driver's tail (`User break, cancelled...`, `Errors caused tool to
  abort.`) is not written.
* A timeout or a trap ends the run: `compile` fails and the `Persistent` is no longer `alive()`; start another.
* `crates/mwcc-wasm/examples/persist_bench.rs` checks it against the stock module, on the same text and (`PLAIN=1`) on
  the context and the candidate alone. With `-sym on`, an object records its source's modification time, so the
  reference must have the persistent source's; on the plain text its line numbers differ by the two lines before the
  slot.
* After `CStateManager.hpp` and `CActor.hpp` (2.7, Windows), a candidate takes 1.9 ms (a 15 MB restore, 1.0 ms, and
  the compile), against 234 ms for a normal compile; the start (the context and the snapshot) costs 0.3 to 0.6 s.

### Many callers: `PersistPool`

`mwcc_wasm::PersistPool` keeps persistent compiles for many contexts (`pool.rs`, ported from mwdec's fast path,
CC0 1.0). A caller gives a context (the unit's arguments, files and the source's text before the candidate) and its
own normal compile. A request goes to an idle persistent compiler of its context, or waits for a busy one; a
context's first one starts in the background from its third request, and at most `max_live` are alive (the least
recently used idle one of another context is dropped for a start). A failure with the compiler's `Error:` report is
trusted, except one in `recheck_every` and with `confirm_failures`; a normal compile that accepts what a persistent
one rejected drops it. `split_point` cuts a candidate before its first difference from a reference. The experiments
use it (docs/oracle/experiments.md, "Persistent compiles").
