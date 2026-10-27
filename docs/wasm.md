# GC 1.2.5n, 1.3.2, 2.6 and 2.7 as WebAssembly

For GC 1.3.2, 2.6 and 2.7, the compiler is built from the vendored `gc2/` sources for wasm32 and runs inside
`mwcc-tools`. This is the default for those versions once the module is built. A compile starts no process and runs
no Metrowerks or Windows binary. GC 1.3.2's and 2.7's linkers (`mwldeppc.exe`) and the GC 1.2.5n compiler are
built the same way from this repository's own sources ([The linkers](#the-linkers), [GC 1.2.5n](#gc-125n)).

`gc2/` is [mweppc](https://github.com/doldecomp/mweppc)'s decompilation (henriquegemignani/mweppc's `gc-1.3.2`
branch, 9c23a57, which adds 1.3.2 to the same sources and the fixes other builds need), with the probes and the
WebAssembly changes below. The WebAssembly build defines `__MWERKS__`, so it compiles mweppc's sources as the
Metrowerks build does, not mweppc's own clang paths (`#ifndef __MWERKS__`). It also defines `NONMATCHING`, so reads
that depend on where the original's objects are (`TYPE_INTEGRAL_UNCHECKED`, `ORIGINAL_ADDRESS` in
`include/compiler/CParser.h`) take the clang build's emulation. For example, the transformation that drops `!!` reads the "integral kind" of a call's `void` return type,
which the original finds as 0 (`IT_BOOL`) past `stvoid`. Without the emulation, the module kept `!!b` and
`static_cast<bool>(b)` of a `bool` at `-O1` and above.

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
path, or `CLANG` and `WASM_LD` at `configure` time. It also needs binaryen's `wasm-opt` (CI's is version_133; on the
path, or `WASM_OPT`) and Rust's `wasm32-unknown-unknown` target. Nothing is downloaded: the C library is
`wasm/libc`'s, not MSL's. The 203 units compile in about 4 seconds. Cranelift compiles a module once per process (1.5
to 2 seconds), and keeps what it compiled beside the module (`<module>.cache`); a compile then takes 5 to 8 ms.

wasm-ld links each module in `linked/` beside it (`build/<…>/wasm/linked/<module>.wasm`), and `wasm-opt -O3 -g` makes
the module of that (`rule wasm_opt`, `wasm_build::OPT_FLAGS`):

* `-O3` leaves Cranelift less to do. GC 2.7's compiler module runs 26 Echoes compiles in 5.1 s instead of 5.5; the
  linker (with decomp/mwcc's buffered output, 5e85e0c) links Echoes' `main.elf` in 0.97 s instead of 1.19 (0.48 s of
  CPU instead of 0.72), as fast as a native build of it. The modules are about 15% smaller.
* `-g` keeps the name section, for the backtraces of traps and `MWCC_WASM_STOP_AFTER` (without it, `<wasm function
  154>`). Functions wasm-opt inlined are not in them.
* The features are the module's own: `--all-features` writes compact imports, which wasmtime 49 does not read.
* The check for trapping stubs reads the link: they are its functions, which wasm-opt may inline or remove.
* What was checked on GC 2.7's optimized modules against the unoptimized ones: the canaries with `corpus
  --all-traces` (2313 identical) and `--trace-reference` (the same 2308, and the same five differences); mwcc-wasm's
  persist tests and `persist_bench` on Echoes units (the same results); the linker's `main.elf` and map for Echoes
  (identical).

CI builds GC 2.7's and 1.3.2's modules, with their linkers, and GC 1.2.5n's on Linux and publishes them with each
release (`mwcc-wasm-GC_2_7-VERSION.zip`, `mwcc-wasm-GC_1_3_2-VERSION.zip`, `mwcc-wasm-GC_1_2_5n-VERSION.zip`). The
comparison with the original compiler (the `corpus` commands above, on `tests/fixtures/oracle/sources`) is run
locally.

## Status

* Canaries (2313), GC 2.7 and 1.3.2: the stock modules give identical objects, messages and exit codes to the
  original compilers, and so do the modules with every trace on (`corpus --all-traces`).
* Their traces against the x86 oracles' (`corpus --all-traces --trace-reference build/<version>/oracle/mwcceppc.exe`):
  * 2.7: identical on 2308 with `--ignore st. --ignore pv.expression.` (the module's `st.presched` snapshots have no
    x86 hook; `pv.expression.*` has no probe yet, [Probes](#probes)).
  * 1.3.2: identical on 2308 with `--ignore st. --ignore pv.`. The register counters' provenance (`pv.function.*`,
    `pv.counter.*`, `pv.value.*`, and the function numbers of `pv.scalar.*`) needs the original's sites, and
    `counter_sites.rs` has 2.7's and 2.6's only: the 1.3.2 module reports no increments.
  * In both, the other 5 differ in a block's weight (`ra.block`'s `weight`, its `loopWeight`), which is read before
    anything sets it: the x86 oracles show a stale pointer, the module a small number. That predates the re-vendor.
* Floating-point folding matches the original (2.7 and 1.3.2) on 40,000 random `+ - * /` of doubles and floats and
  10,000 of converted integers before any literal is read ([below](#floating-point-the-x87)).
* Metroid Prime (`corpus --project`, 1.3.2): 698/698 identical. A full build of Prime (USA 1.00) through `mwcc-tools
  run`, where the 1.3.2 units compile in the module and the other compilers run as processes, gives the same 1718
  objects and binaries as the native build, and `main.dol` passes its hash check.
* The linkers, on decomp/mwcc's `tools/linker_equivalence.py` (324 cases against the original executables):
  * 2.7: 320 identical. Of the other 4, the original crashes on 2 (`dump-rom`, `link-includedwarf`), 1 gives a
    message the host's locale writes (an OS error text), and 1 names a long file by its 8.3 name in a message
    ([The linkers](#the-linkers)).
  * 1.3.2: 322 identical. The original itself fails one of the other 2.
* Metroid Prime 2: Echoes (`corpus --project`, 2.7): 953/954 identical. The one that differs depends on the host:
  `src\Runtime\Gecko_ExceptionPPC.cp`'s `extab`. `Exceptions.c` builds its action records in structures with padding
  on the stack, and the original writes the padding as the stack had it: the high half of a stack address
  (`1a 00` on Windows, `ff 7f` under wibo, other bytes from one run to the next). The module writes its own stack's
  (`a5 00`). The object is not linked.
* An uninitialized read the module reproduces: peephole's `L_S` (drop a store of a value just loaded from the same
  place) compares `defInstrOffset` and `scanOffset` for a store to another object, which only a store to the same
  object sets. In the original they are stack slots whose values never overlap; under `NONMATCHING` they start as 0
  and `0x7FFFFFFF`, which compare the same way. (In Echoes' `CSandworm::UpdateEyes`, this deletes the first of three
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
* What WebAssembly needs of gc2's sources is in mweppc's sources (9c23a57): in the plain source where the original's
  build is unchanged, else under `#ifdef NONMATCHING`, which every module build defines (it defines `__MWERKS__`
  too). Only constant folding's x87 arithmetic is gc2's own, under `#ifdef __wasm__`.
  * A function called through a pointer of another type traps in wasm, where x86 ignores the extra argument:
    `CPrepTokenizer.c`'s character handlers and `Parameter.c`'s `CompareParam_SetString` take the pointer's
    parameters. clang's `-Wcast-function-type-mismatch` finds no others.
  * `ORIGINAL_ADDRESS` and `TYPE_INTEGRAL_UNCHECKED` (`CParser.h`) use the clang build's emulation, and
    `Peephole.c`'s redundant-store check gets the offsets the original's stack slots hold.
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

#### A probe of your own

Every compile and trace of GC 2.6 or 2.7 runs `build/<version>/wasm/mwcc.wasm`, the module of gc2's sources compiled
with `ORACLE_PROBES` and the Rust hooks (not `oracle.wasm` or `mwcceppc.wasm`, which only mwdec loads). A trace's
document names it: `oracle` is the executable the compile stands for (`build/<version>/oracle/mwcceppc.exe`), `module`
the module that ran in its place and `producer_sha256` its hash (`module` is absent when the oracle ran as a process).

To observe something no stream records, change gc2's sources in a checkout of your own and build its module:

1. Declare the probe in `include/compiler/OracleProbes.h` and call it at the site with `ORACLE_PROBE(...)` (nothing
   without `ORACLE_PROBES`, so the matching builds are unchanged). Pass it the values it reports: the Rust hooks read
   the compiler's structures by offset, and a value passed needs none. A probe that only prints can call
   `OracleTrace_Printf` (`wasm/src/oracle.c`) directly; one the trace readers know is defined in
   `crates/mwcc-oracle-hooks`, gated on its `MWCC_ORACLE_*` variable, and listed in `producer.rs`'s exports.
2. `mwcc-tools probe-build --from MAIN_CHECKOUT` in the checkout copies the downloaded compilers and tools from the
   main checkout (when it has none), configures GC 2.7 (`--version` for another) and builds `mwcc.wasm`.
3. Run that checkout's `mwcc-tools` with the checkout as its root: its compiles use the new module.

## The linkers

`crates/mwcc-oracle-core/src/wasm_linker.rs` builds GC 1.3.2's and 2.7's linkers, `build/LD_1_3_2/wasm/mwldeppc.wasm`
and `build/LD_2_7/wasm/mwldeppc.wasm`, each with its `.version` (the VS_VERSIONINFO of `src/linker/mwldeppc13.rc` and
`mwldeppc27.rc`). `configure --version GC_1_3_2` (or `GC_2_7`) adds the version's linker to the default targets.

Their sources are this repository's own (`src/`, decomp/mwcc's linker decompilation, 7391bd7): a linker is the 2.7
compiler's driver and C library with the linker plugin (`src/linker`) in place of the compiler. Its units are
`config/LD_2_6/splits.txt`'s with the sources `config/LD_2_7/config.json`'s `replace` gives, compiled with the 2.7
compiler's flags, `-DVERSION` of GC_2_7 and `-DLINKER=13` or `-DLINKER=27`. As in the compiler, `wasm/libc` and
`wasm/src` replace MSL, the runtime and the license imports (compiled as the GC 2.7 compiler module's are).

* The sources say `#pragma options align=mac68k` (and `packed`, `native`, `reset`), which clang reads only for
  Darwin targets. The build compiles a mirror of `src/` and `include/` (`build/wasm-ld/tree`, `mwcc-tools
  wasm-mirror`) with each written as the `#pragma pack` that gives the same layout: `mac68k` as `pack(push, 2)`,
  `packed` as `pack(push, 1)`, `native` as `pack(push, 8)`, `reset` as `pack(pop)`. Any other form stops the build.
  The matching build's sources stay as they are.
* The original's uninitialized data is one COMMON block (`src/linker/common.s`, x86 assembly, each name at the
  original's offset), and its code relies on that layout: `initialize_cmdline_environment` clears 0x198 bytes from
  `pluginPrivateContext` on, and some names are inside others (`clState`). `mwcc-tools wasm-common` writes the block
  as WebAssembly assembly (`build/<linker>/wasm/obj/common.s`) with the same names, offsets and sizes, linked first:
  with `--allow-multiple-definition` the first definition is kept, so the units' C definitions of those names are
  left unused, as MWCC's COMMON symbols merge into the block.
* The module is linked with `--allow-undefined-file=wasm/linker-imports.txt`, the Win32 functions the host
  (`crates/mwcc-wasm`) provides, in place of `--allow-undefined`: any other undefined symbol (a missing data
  definition, say) fails the link.
* What WebAssembly needs of the sources themselves (the varargs the driver takes from the stack by hand, prototypes
  that differ from their definitions, `to_lowercase` called through an `int (*)(char)` cast, `TO_LOWERCASE_CHAR`
  in `include/driver/Utils.h`; the ELF writer's callbacks without an argument, called with one; the lists of
  environment variable names, which the original ends with a literal's padding, `ENVIRONMENT_LIST_END`; a command
  file's last token, which the original reads past the file's end) is decomp/mwcc's own (7391bd7), under `#ifdef
  NONMATCHING`, which the module build defines. The Metrowerks build preprocesses to the same tokens as before, for
  every version and both linkers.
* The 2.7 drivers give a file name of 64 characters or more as its 8.3 name (`GetShortPathNameA`, and
  `cAlternateFileName` of a directory listing). The host gives Windows' 8.3 name where it has one (on Windows,
  where 8.3 names are on), so such a file is found as the original finds it; elsewhere it is not found. A message
  that names such a file gives Windows' 8.3 name, which can differ from the one the original prints.

`mwcc-tools run` runs the module in place of the original linker (`build/compilers/GC/<dir>/mwldeppc.exe`, or a copy
of it anywhere, known by the size and SHA-1 of `config/LD_*/config.json`) once it is built, as it does the compilers.

## GC 1.2.5n

`wasm_linker.rs` also builds GC 1.2.5n's compiler, `build/GC_1_2_5n/wasm/mwcc.wasm`, from the root `src/` (the
decompilation the matching build compiles): the units of `config/GC_1_2_5n/splits.txt` with their flags and
`-DVERSION` of GC_1_2_5n. It is built as the linkers are: the mirror's pragmas, `wasm/libc` and `wasm/src`, the
imports file, and a COMMON block, which `mwcc-tools wasm-common` writes from the split's `common` unit and the
names of `symbols.txt` in it (a port of decomp/mwcc's `tools/common_source.py`). `configure --version GC_1_2_5n`
adds it to the default targets. The original has no resources, so its `.version` is empty, and a version query fails
as it does on Windows.

The module runs in the place of the stock compiler and of a project's copy of it, as the others do. It has no
oracle hooks: a traced run of 1.2.5n goes to the native oracle. GC 1.3 cannot be built so: 371 of its functions
(about 300 KB) have no source, so it runs as a process.

What WebAssembly needs of the compiler's sources is under `#ifdef __wasm__` (the driver's, shared with the linkers,
under decomp/mwcc's `NONMATCHING`), and the Metrowerks build preprocesses to the same tokens for every version and
both linkers:

* Varargs the code takes from the stack by hand (`CError.c`, `PPCError.c`, `PCodeUtilities.c`, `IroDump.c`,
  `CParser.c`, `CExpr2.c`) use `__va_start`.
* Calls whose type is not the callee's (a call through a cast to another type, or a declaration that is not the
  definition's) trap in WebAssembly. The tables of option handlers, token scanners, peephole rules, code generators
  and IR visitors get thunks with the table's type; direct calls replace the casts elsewhere, including the casts
  of the variadic `PCodeUtilities_EmitInstruction`. LLVM puts a trapping stub (`*_bitcast_invalid`) in place of
  such a call without a warning, and wasm-ld its own (`signature_mismatch:*`) with one. `mwcc-tools wasm-stubs`
  lists them from a module's name section; every module has the check of its link as a default target
  (`build/<…>/wasm/linked/<module>.wasm.stubs`), which fails the build where there are any.
* Functions the sources declare `void`, or that end without a `return`, whose x86 leaves a value in EAX that the
  caller uses (`create_objectnode`, `make_funccall_with_dexprs`, `convert_right_and_make_diadic_node`, …):
  `WASM_RETURNS(type)` and `WASM_RETURN` (`include/compiler/common.h`) return it.
* `IRO_RangePropagateInFNode` never sets `changed`, which is its caller's EBX: the pass number
  (`IroRangePropagation_Pass`). Branching on an unset value let LLVM drop the loop's exit, and every compile at
  `-O3` and above hung.
* The machine simulations index their pipeline stages from the first of several statics in a row: one array.
* `eliminate_redundant_store` compares two offsets that a store to another object leaves unset (the caller's EDI
  and a stack slot): they are set so that the objects do not overlap, as gc2's `L_S` has them.
* Reads past an object (`stvoid`'s padding, `UNCHECKED_INTEGRAL`) and constant folding on the x87
  ([below](#floating-point-the-x87)) as in gc2.

Ninji's patch of 1.2.5n (2023-07-15, x86 code in the room of a `CObjC.c` function) gives the instruction that pops
the frame flag 0x400. It is in `StackFrameEABI.c` for VERSION GC_1_2_5n, outside `__wasm__`: the source describes
the binary. `StackFrameEABI.c` is not in 1.2.5n's `matching` list, so the matching build keeps the original's bytes.

`MWCC_WASM_STOP_AFTER=SECONDS` stops a run that takes longer as a trap does, with the module's backtrace: how the
hangs above were found.

Status:

* Canaries (2313): identical objects, messages and exit codes to the original's.
* The units that use 1.2.5n (`corpus --version GC_1_2_5n --prime`, which compiles each without sjiswrap on both
  sides): Metroid Prime (USA 1.00), 94/94 identical; Metroid Prime Trilogy's (R3ME01), 26/26; Metroid Prime 2:
  Echoes (USA), 81/81.
* Full builds through `mwcc-tools run` ([below](#a-projects-build-mwcc-tools-run)), with 1.2.5n, 1.3.2 and 2.7 and the
  1.3.2 linker in process: Metroid Prime (USA 1.00: 94 units on 1.2.5n, 627 on 1.3.2; GC 1.3's 64 run as a process)
  gives the same objects as the native build, and `main.dol` and `NESemuP.rel` pass their hash checks. Metroid Prime
  2: Echoes (USA: 81 units on 1.2.5n, 31 on 1.3.2, 725 on 2.7; its GC 3.0a5 linker runs as a process) passes its
  hash checks (87 files).
* Floating-point folding: 7600 functions of random `+ - * /` chains of literals (doubles, floats, integers, casts),
  identical.

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
  * A run reads the disk once (`crates/mwcc-wasm/src/files.rs`). It lists each directory it looks in once, and the
    listing answers whether a file is there and what it is: the compiler looks each header up in every include
    directory. This took an Echoes unit from about 2.7 s to 0.85 s. It reads each file once, at its first open, and
    later opens are served from memory. The linker opens each object seven times: the driver identifies it and
    takes its time, the importer plugin checks its header and reads it for the project's sizes, and the link checks
    it and reads it again.
  * What a run creates, writes, deletes or removes stays in memory, and the run sees it from then on: a file it
    reopens, `GetFileAttributesA` and `FindFirstFileA`. It reaches the disk when the module ends (`ExitProcess` or
    its return, whatever the exit code): the directories it created, then the files it deleted, the files it wrote,
    and the directories it removed. A run that traps, times out or is cancelled leaves the disk as it was. A write
    that fails then (a full disk, a file another process has locked) is reported on the standard error, and a run
    that succeeded exits with 1.
  * With both, Echoes' `main.elf` links in 0.68 s instead of 1.03 s; the system's time in it went from 0.52 s to
    0.21 s, and its other I/O calls from 29,564 to 4,635. A persistent run reads the files again for each candidate,
    and a candidate does not write any.
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
  * The command line and environment are the caller's. Under `mwcc-tools run` on Windows, the compiler gets the
    wrapper's own command line from the executable on, with the caller's quoting (`GetCommandLineA` gives what the
    original's process would get); elsewhere, and in `corpus`, it is joined from the arguments.
    `GetFullPathNameA` normalizes paths the Windows way and leaves
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

* An EXE that is the original 1.2.5n, 1.3.2, 2.6 or 2.7 compiler, or the 1.3.2 or 2.7 linker, wherever it is, runs in
  process when its module is built. It is recognised by the size and SHA-1 in `config/<version>/config.json`, so the checkout needs no copy of the
  compilers; an executable of another size is never read. It gets the wrapper's arguments, directory and environment,
  and writes the object and the `-MMD` `.d` file itself. The wrapper prints its stdout and stderr and exits with its
  status. The modules and configs are `MWCC_ORACLE_ROOT`'s, or else those of the checkout the executable was built in.
* Anything else (the other compilers and linkers) runs as a process: natively on Windows, elsewhere through wibo,
  taken from the first of `MWCC_WIBO`, the project's own `build/tools/wibo` (relative to the build's directory), the
  checkout's `build/tools/wibo` and `wibo` on the path.
* `sjiswrap.exe` does not run for a compiler that runs in process: the host gives the sources and headers the
  compiler opens for reading as sjiswrap gives them (UTF-8 converted to Shift JIS, and `IsDBCSLeadByte` true of a
  byte with its high bit set).
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
