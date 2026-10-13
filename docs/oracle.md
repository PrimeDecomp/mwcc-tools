# The oracle executable (GC 1.3, 1.3.2 and 2.7)

The oracle is the original `mwcceppc.exe` relinked with trace wrappers around the register allocation and the inliner.
It writes the same objects, messages and exit code as the original (the wrappers only print), and says why it
assigned the registers it did and which calls it inlined. It is **not** built from this repository's C sources of the
allocator and inliner: those are not byte-exact for 1.3/1.3.2 (2.7 has no sources at all), so the original's own code runs and the wrappers
(`src/oracle/Hooks.c`, `src/oracle/OracleTrace.c`) are linked around it.

It needs the original compiler, which `ninja` downloads to `build/compilers/GC/<version>/` (do not
`ninja -t clean` the oracle target: it removes the downloaded compilers). `tools/oracle.py` also runs that original
compiler, to check that the oracle's object is the same (`--no-check` skips it), so both are needed where it runs.

## Build

```
python configure.py --version GC_1_3_2 --mode link --oracle
ninja build/GC_1_3_2/oracle/mwcceppc.exe
```

`GC_1_3` and `GC_2_7` work the same way. `configure.py` writes `build.ninja` for one version, so re-run it when switching.
Which functions are wrapped is `oracle.hooks` in `config/<version>/config.json` (retail symbol names; keep the
wrappers in `src/oracle/Hooks.c` and that list consistent). The licence stub `lmgr326b.dll` (`tools/lmgr_stub`) is
copied beside the executable, on every platform: to run the oracle on another Windows machine, copy
`build/<version>/oracle/mwcceppc.exe` and `lmgr326b.dll` (and `build/compilers/GC/<version>/` for the check).

The executable is a Windows program. On Linux or WSL the build runs it through wibo, and `tools/oracle.py` does the
same (`build/tools/wibo`, or a `wibo` on the PATH); it does not need binfmt_misc or the executable bit.

## Explain a source: `tools/oracle.py`

```
python tools/oracle.py [--version V] [--function NAME] [--raw] [--pcode] [--schedule] [--inline-cost CALLEE]
    [--near N] [--exchange A,B] [--override PATH=REPLACEMENT] [--json] [--no-check] SOURCE -- [COMPILER FLAGS...]
python tools/oracle.py --from-ninja ../prime/build.ninja src/GuiSys/CGuiFrame.cpp --function Draw
```

The compiler's flags come after `--` (or, the older form, after SOURCE: the script's own options, which have two
dashes, are still recognised there). `--from-ninja FILE` takes the flags and the working directory from the rule that
builds SOURCE in a decomp project's `build.ninja` (the directory is the one that file is in; SOURCE is relative to it
or absolute, with `/` or `\`, in any case on Windows); flags given as well are an error. It also takes the version from the rule's
`mw_version` (`GC/2.7` is `GC_2_7`), and warns when `--version` names another. Without either, the version is
`GC_1_3_2`. The report, per function (named by its name and its link name, `Think (Think__10CPowerBombFfR13CStateManager)`),
lists

* the inliner's decisions: inlined (with the cost estimate against the threshold) or kept, and why (not declared
  inline, over budget, ...); callees are qualified (`CWeapon::GetOwnerId`), and a decision made more than once is given
  once with how many times, at which inline depths and on which source lines (the line of the statement the call is
  in, in the file that statement comes from: a header's line for a call inside an inline body);
* the register allocation, per class: K, the class's colour mask, earlier colourings that failed and spilled, and the
  **traced** colouring order (the stack the simplification leaves, first coloured first). Each virtual register gets
  its physical register, def/use counts, the blocks it lives in, the instruction (block and source line) that first
  defines it, and why: the calls it lives across, why the simplification took it (its degree below K, or the spill
  candidate with the lowest cost / degree), the colours its neighbours held when it was coloured, and the colours free
  in the mask (the lowest is taken; when none is, a non-volatile register is claimed, from r31 down). Select is
  replayed from the traced graph and mask (`select replayed: N of M agree`; a register it disagrees on is flagged), and
  so is the simplification (a note says when its order differs from the trace). r0 as a neighbour is pointed out: r0
  cannot be a base register (an address operand of r0 reads as 0);
* the stack frame once it is laid out: its layout (linkage area, outgoing arguments, locals area, the area above it
  and the frame size, read from the compiler's globals), the locals and the compiler's temporaries in memory by
  offset in the locals area and by offset from r1 (`r1+28`, the address the code uses), with their size, and the
  locals kept in registers.

`--function` restricts the report, and `--raw`'s trace lines, to the functions whose name or link name contains NAME.
`--pcode` prints the PCode the allocator was given, per block, with the virtual register numbers (`%r60` is virtual
register 60) that the register report names. `--json` prints all of it as one JSON document instead
(`functions[].classes.GPR.vregs[]`: `vreg`, `class`, `phys`, `defs`, `uses`, `live_blocks`, `across_call`, `def`
(`block`, `line`, `instruction`), `degree`, `neighbors` (`vreg`, `phys`), `state` (colored, coalesced, spilled,
split), `order`, `pushed`, `select`; `functions[].inlining[]`: `callee`, `link` (the callee's link name, which tells
overloads and template instances apart), `inline`, `cost`, `threshold`, `reason`, `count`, `depths`, `lines`;
`functions[].frame`).

When no function matches `--function`, the traced functions that look most like NAME are listed, by name and link
name (`--json`: `similar[]`).

More views, each traced only when asked for (the objects are the same either way):

* `--schedule`: the instruction scheduler, per block, before the allocation (virtual registers) and after it. Each
  cycle's pick, and for every other candidate why it lost: not ready yet (and the instruction whose latency it waits
  for), its unit busy, or the first tie-break it lost (urgency: its latest cycle is past; fewer scheduled successors;
  lower height; lower opcode rank, before the allocation only; else the earlier instruction wins). The picks are
  replayed from the traced candidates (`select replayed: N of M picks agree`). `--json` adds
  `functions[].schedule[]` with `picks[]` (`over[]` with `why`), `edges[]` (`from`, `to`, `latency`) and `before`
  (the order the scheduler was given).
* `--inline-cost CALLEE`: only the inliner's decisions about callees whose name or link name contains CALLEE, each
  with its cost broken down: one line per statement-level expression of the callee's body (`*(arg0+8) = arg1`: 5),
  and the bodies of the calls inlined inside it nested under their call. A call names its callee's link name and
  says when the compiler made the callee (an implicit destructor or constructor). `--json`: `inlining[].breakdown`,
  whose call nodes have `link`, `implicit`, `ctor` and `dtor`.
* `--near N`: every decision of the automatic inliner whose cost is within N of the threshold, in all functions,
  with the callee's link name, the function and the lines: the close calls a header change can flip (`--json`:
  `near[]`, each with `margin`, the threshold minus the cost). The estimate stops once over the threshold, so a kept
  call's cost is where it stopped.
* `--exchange A,B` (`--exchange r41,r42`): why two virtual registers of a class were coloured in that order and got
  the registers they did: when the simplification pushed each and the neighbour whose removal let it, degrees,
  spill costs, neighbours, and what select found free for each.
* `--override PATH=REPLACEMENT` (repeatable): compile with REPLACEMENT in place of PATH (relative to the directory
  compiled in), without touching the checkout. For a header, REPLACEMENT is copied under a temporary directory that
  is searched just before the include directory PATH is in; for SOURCE itself, REPLACEMENT is compiled with SOURCE's
  directory searched first. The check compiles the same replacement with the original.

The first line says whether the object equals the original compiler's; when it says NO, the sections and the
functions that differ are listed, and the explanation is of a different object and should not be trusted.

From Python, `tools/oracle.py` can be imported. `analyze_source(source, flags=(), from_ninja=None, version=None,
overrides=(), function=None, link=None, schedule=False, inline_cost=False, check=False, near=None)` compiles and
returns the document `--json` prints (`overrides` as `--override` takes them, `near` as `--near`); it raises
`OracleError` when it cannot compile. `compile_trace(...)` (the same arguments) only compiles and returns the trace.
`analyze_file(trace, function=None, link=None, version="GC_1_3_2")`
takes a trace file (or its lines) and returns one dict per function, as `--json` prints them (`function` a substring
of the name or link name, `link` the exact link name); each has `schema_version` (`SCHEMA_VERSION`, raised when a
field changes meaning or goes away). `explain_exchange(analysis, a, b)` and `similar_functions(...)` are the
`--exchange` and suggestion views. Lower down, `parse_trace(lines)` gives the snapshots before and after each
allocation (the before snapshot also holds the colouring rounds and the frame), `analyze(...)` the data `--json`
prints and `opcode_names(version)` the opcode names, for views of your own.

The raw trace goes to standard error, or to the file named by `MWCC_ORACLE_TRACE_FILE` (set it to the null device when
only the object is wanted). Its lines: `ra.before`/`ra.after function= link=` with `ra.block` and `ra.i` (`line=` is
the source line) per instruction; `ra.graph cls= k= start= count=` and `ra.node v= p= deg= flags= color= n=` (the
graph before the simplification); `ra.order` (the stack, first coloured first, with each node's spill cost);
`ra.select mask= after= ok=` (each node's colour and flags); `fr.local`/`fr.temp name= offset= size= reg= used=`
between `fr.begin` and `fr.end`, with `fr.layout linkage= outgoing= locals= above= size=`; `inl.can` and `inl.begin`
(with `line=`, the call's statement's source line, and `link=`, the callee's), `inl.cost` (`nest=`), `inl.end`. With `MWCC_ORACLE_SCHEDULE=1`: `sc.block p= index= virtual=` with its `sc.i`
lines, `sc.node i= latency= height= latest= preds= succ=` (the dependency graph, `succ` as `pointer:latency`),
`sc.pick cycle= ready= pick=` (each candidate `pointer:earliest:latest:height:scheduled successors:check:rank`) and
`sc.after order=`. With `MWCC_ORACLE_INLINE_COST=1`: `inl.expr nest= type= cost= what=`, in post-order (a call of a named function also has `link=` and `flags=`, its
function type's flags: 0x100 made by the compiler, 0x1000 a constructor, 0x2000 a destructor). The link
names are the mangled ones (the compiler computes them when it has not yet). The file is appended to, so delete it
between runs.

## Check the equivalence: `tools/corpus.py`

```
python tools/corpus.py --version GC_1_3_2 --canaries path/to/mwcc-rs/canaries --jobs 8
python tools/corpus.py --version GC_1_3_2 --prime path/to/prime --keep out-dir
```

Compiles every input with the original and the oracle and compares the sections that are not debug information (the
size of `.bss`), the messages and the exit code; prints `DIFF name: ...` per difference and `N/M identical`.
`--canaries` takes a directory of small programs (a first line `// flags: ...` overrides the baseline flags), `--prime`
a decomp project whose `build.ninja` rules that use this version's compiler are compiled with their own flags.
`--limit`, `--list FILE` and `--keep DIR` narrow the input or keep the objects of the inputs that differ. The exit
status is 0 when nothing differs. Last result: 2313/2313 canaries on all three versions; Prime 604/604 (1.3.2) and 64/64
(1.3); Metroid Prime 2: Echoes 729/729 (2.7).

## Other tools

* `tools/locate.py VERSION SOURCE...`: where a compiled source's functions are in the executable.
* `tools/nearest.py VERSION SOURCE...`: the functions of the executable a not-yet-exact function resembles most.
* `tools/fdiff.py VERSION SOURCE FUNCTION ADDRESS`: instruction diff of a compiled function against the original's.
* `tools/disasm.py VERSION ADDRESS [SIZE]`: annotated disassembly of the original executable.
* `tools/gates.sh`: every version's relink/compare gate, one after the other.
* `docs/gc132-differences.md`: what changed between 1.3 and 1.3.2.
* `docs/gc27-differences.md`: what changed between 1.3.2 and 2.7, and how 2.7's configuration was made.
