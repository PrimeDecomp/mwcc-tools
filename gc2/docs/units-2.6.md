# GC 2.6 (mwcceppc 2.4.7 build 107): translation units

How 2.6's translation units were identified and built: the evidence, the host compilers, and the units that need
explaining. Where each unit stands now: `tools/status.py GC_2_6` (range, functions, bytes, link state; `--todo` what
blocks each) and [units-2.6-table.md](units-2.6-table.md). Per-unit findings are in [audit-2.6.md](audit-2.6.md) and
[review-2.6.md](review-2.6.md).

Tools: `tools/status.py`, `tools/evidence.py unit UNIT` (a unit's functions with the files and lines their asserts
push and Wii-confirmed names), `tools/compilers.py host` (each unit compiled with each host compiler, code and data
placement scored), `tools/rename.py --unit` (rename a source).

## Sources of truth

| Source | What it gives | Confidence |
|---|---|---|
| 2.6 itself: assert sites | `CError_Internal(file, line)` pushes each function's file name and line: the unit of every function that asserts, in link order | Certain for the unit (asserts in inlined header functions name the header) |
| 2.6 itself: ENode name table (0x5bc980) | the ENodeType numbering | Certain |
| Wii compilers' CodeView (build/compilers/Wii/*) | public names, and every module (.obj) with its code ranges | Certain for names; module *order* and contents are a later codebase (314 modules) |
| Ninji's decompilation (Mac OS X mwccppc, Pro 7, 2.4.5) | names and source of most frontend/optimizer/backend functions; file = unit | High for names and logic; codegen is Pro 7's and the code is a slightly different version |
| CodeWarrior Pro 5.3 updater (Pro5.3_Factory_Upd_NoLibs.exe) | MSL C and the x86 runtime sources, the revision 2.6's libraries were built from (lib/) | Certain where a unit builds exact from it unchanged |
| rayanht's 1.2.5 / Henrique's 1.3 decompilations | where this repository's sources came from (ported by tools/port_version.py) | Their unit split is 1.x's and partly approximate; names are theirs |
| Ghidra (scratch) | decompiled C for unnamed code | Low: shape only |

## Host compilers

`tools/compilers.py host` (all units, Pro 5 / 5.3 / 6 / 7):

* The compiler's own code (driver, frontend, optimizer, backend): **Pro 6.0** (2.4 build 131) everywhere. No other
  compiler makes more functions exact in any unit; Pro 7 is consistently behind.
* MSL C: one prebuilt library, **Pro 5.3, `-O4,p -sym on -align 8`**, built from the **Pro 5.3 updater's MSL
  sources**. Signs: alloc, printf, strtold, time and abort_exit are exact only with Pro 5.3; only Pro 5.3 puts
  explicitly zero-initialized data in .data, where 2.6 has MSL's (atexit_curr_func); `-sym on` is needed by 16 units
  and harms none; `-align 8` is needed by 5 and harms none; the updater's scanf (`__vsscanf` reading an empty string
  as EOF, `mm 990617`) is 2.6's and Pro 5's is not.
* The x86 C/C++ runtime: a second prebuilt library, **Pro 5.3, `-O4,p -sym off`, C++ exceptions on** (the default:
  even the C units have the frame code exceptions add), from the updater's runtime sources.
* Two runtime units are a later revision than the updater's (written out from the original): `setupargs`
  (Microsoft-style argument parsing) and `exchand` (console output, exception table registry). MSL's time.c too, in
  three functions (`tools/download_tool.py` patches them in; `src/msl/time_g.c`).

## `-inline auto,deferred`

Most units of the frontend, optimizer and backend (`order` = `rev` in [units-2.6-table.md](units-2.6-table.md)) were
compiled with **deferred inlining**: Pro 6 parses the whole file first, then emits its functions **in reverse source
order**, inlining any function into any other regardless of where it is defined. Read from the binary:

* Assert lines **descend** along a unit's `.text` (CExprConvMatch: 3125 at its first function, 581 near its last).
  Ninji's Pro 7 files keep the same order with nearly the same lines, so a unit's rows map onto Ninji's functions
  one-to-one, last first. Most names and boundaries were settled this way.
* A unit's **first source functions are its last rows**, and its last source functions its first rows. When a run of
  rows at the *end* of unit X is named like unit X+1, it is X+1's tail, emitted first: the boundary is wrong, not
  the names.
* Functions nothing calls are **dead-stripped** by the linker, so a gap in Ninji's order is not evidence against a
  unit (CExprConvMatch has no ClassCopyInit; Registers.c has no retain_register_for_argument).

Such a unit is written in Pro7's order and built with the flag ([names-2.6.md](names-2.6.md)'s "Deferred units" lists
which are). Two more things make moving code between files unsafe without a rebuild:

* Pro 6 carries state across functions: a function's mere presence can change how a later one is scheduled. Code no
  2.x binary has (1.x leftovers, drafts, stubs) is removed for that reason, not only for tidiness.
* Static inline helpers, macros, `#pragma` on/reset pairs and struct alignment pragmas must travel with a function,
  and definition order decides what gets inlined. `tools/unit.py move` and `order` carry a function's comments and
  the pragmas and `#if` that belong to it alone, and refuse a move that changes its pragma settings or `#if` branch;
  the result still needs a build and `tools/verify.py`.

## Unit evidence and naming policy

For each unit the table's `name` column says what supports its file name: 2.6's own **assert** strings (certain),
a **Wii** module of that name (certain for the name; Wii's module contents are a later codebase: IroType, IroExprs,
CExprGen, CExprTools, CTypeTools, CTemplateFuncInst are later splits of 2.6 units, so Wii placing a function there is
not a disagreement), **Ninji**'s file (high). A function's name is taken, in this order of preference, from: a Wii
public symbol; Ninji's function with exactly the same assert lines (the same lines in the same file identify the code,
unless the row only inlines that function: rows that share one helper's assert, or are far larger than Ninji's
function, are left alone); Ninji's function in the same position of a deferred unit, confirmed by body and callers.
`tools/evidence.py sweep` grades every row and lists the units whose evidence disagrees, into
[names-2.6.md](names-2.6.md).

## The CodeWarrior plugin library

The command-line driver and the compiler plugin both link the CW **PluginLib** (Ninji:
`command_line/PluginLib/Src/Library`), and 2.6 has it twice over:

| Unit | Range | Half | Notes |
|---|---|---|---|
| driver/CWSecretPluginCallbacks.cpp | 41c390 | driver | Wii module and publics (CWSecretAttachHandle/DetachHandle/GetNamedPreferences) |
| driver/CWParserPluginsPrivate.cpp | 41c480 | driver | one C++ unit. Emits ??4CWCommandLineArgs |
| driver/CWPluginsPrivate.cpp | 41e8a0 | driver | |
| driver/DropInCompilerLinkerPrivate.cpp | 43ffe0 | compiler | Wii's decorated names (CWIsPrecompiling@8 ...) |

What sets them apart from the units around them:
* **C++ with `-inline off`** (all but CWSecretPluginCallbacks.cpp): implicit copy-assignment operators (`??4X@@QAEAAU0@ABU0@@Z`) are emitted out of line
  only without auto-inlining, and 2.6 calls them. CLPluginRequests.cpp is built the same way.
* The compiler half's copy has its own `operator=` for the API v8 CWTargetInfo layout (0x4402e0), which the linker
  did not fold into the driver's `??4CWTargetInfo`: a different type, from a different build of the SDK headers.
* Uncalled entry points (CWGetBuildSequenceNumber, CWSetTargetInfo ...) are dead-stripped.
  CWParserStoreCommandLineForPanel is in the source although 2.6 does not have its code: it is the first user of
  `??4CWCommandLineArgs`, which is why that operator sits before SetParserEntry.

That fits the suspicion that the SDK came prebuilt: the PluginLib objects are compiled with settings of their own
(and twice, with different header versions), unlike the units around them.

## Open

* Code with no row: a byte-exact function that objdiff shows below 100% because its row runs on into code the
  inventory has no row for (COFF has no symbol sizes: objdiff sizes a symbol up to the next one). Left: 0x426f4f,
  after operator new. Ninji's files should name it.
