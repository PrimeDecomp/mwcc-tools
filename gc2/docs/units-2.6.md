# GC 2.6 (mwcceppc 2.4.7 build 107): translation units

One section per group of units, in link order. For each: what the unit is (and the evidence), how it is built, what
the sources draw on (and how far that source can be trusted), and what is left, with why.

This is the record of the unit pass of October 2026 (its findings stand; its counts and "left" lists are that pass's).
Where things stand now: `tools/status.py GC_2_6` (every unit: range, functions, bytes, link state; `--todo` what blocks
each), [units-2.6-table.md](units-2.6-table.md) and AGENTS.md's state section.

Tools: `tools/status.py` (every unit), `tools/evidence.py unit UNIT` (a unit's functions with the files and lines
their asserts push and Wii-confirmed names), `tools/compilers.py host` (each unit compiled with each host compiler,
code and data placement scored), `tools/rename.py` (`--unit` for a source).

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
  compiler makes more functions exact in any unit; Pro 7 is consistently behind (Unmangle 3/13 against 11/13, CPrec
  21/79 against 40/79).
* MSL C: one prebuilt library, **Pro 5.3, `-O4,p -sym on -align 8`**, built from the **Pro 5.3 updater's MSL
  sources**. Signs: alloc, printf, strtold, time and abort_exit are exact only with Pro 5.3; only Pro 5.3 puts
  explicitly zero-initialized data in .data, where 2.6 has MSL's (atexit_curr_func); `-sym on` is needed by 16 units
  and harms none; `-align 8` is needed by 5 and harms none; the updater's scanf (`__vsscanf` reading an empty string
  as EOF, `mm 990617`) is 2.6's and Pro 5's is not.
* The x86 C/C++ runtime: a second prebuilt library, **Pro 5.3, `-O4,p -sym off`, C++ exceptions on** (the default:
  even the C units have the frame code exceptions add), from the updater's runtime sources.
* Two runtime units are a later revision than the updater's (written out from the original): `setupargs`
  (Microsoft-style argument parsing) and `exchand` (console output, exception table registry). MSL's time.c too, in
  three functions (`tools/download_tool.py` patches them in).

## Runtime and MSL head (0x401000-0x4067ff)

| Unit | Range | State | Notes |
|---|---|---|---|
| runtime/maincrt.c | 401000 | exact | shipped source |
| runtime/exchand.c | 401070 | exact | was ExceptionTables.c. Hand-written: 2.6's exchand is later than the updater's (see the file) |
| runtime/ExceptionX86.cpp | 4012f0 | exact | shipped source |
| msl/startup_win32.c | 401dd0 | exact | hand-written; startup.win32 + ThreadLocalData. TODO: build from the updater's sources |
| msl/abort_exit.c | 402120 | exact | shipped source. atexit/exit/__exit were reimplemented in CLStaticMain.c under invented names |
| runtime/setupargs.c | 402260 | 2/3 | new unit (was in CLStaticMain.c). TODO `_SetupArgs`: Microsoft-style parsing (see the file) |
| runtime/runinit.c | 4024c0 | exact | new unit; shipped source. `__xc_a` is .CRT's first word (the binding was 4 bytes late) |
| driver/CLStaticMain.c | 4024f0 | exact | `main` only, as Wii's module |
| runtime/NMWException.cpp | 402620 | exact | starts with dthandler/duhandler/terminate/unexpected (were "abort_execution" etc. in CLStaticMain.c) |
| msl/printf_g.c | 402780 | 13/15 | updater's printf.c. `printf_g_float2str` 99.9%, `__pformatter` same instructions (an immediate) |
| runtime/arrcondes.cpp | 403d70 | exact | shipped source (Pro 5's was not 2.6's) |
| msl/string.c | 404eb0 | exact | one unit (string_g.c was a second copy); `strerror` was hand-written as `get_strerror` |
| msl/ansi_files.c, file_io.c | | exact | one unit each (the _g copies are gone) |
| msl/scanf.c, wscanf.c | | exact | one unit each; `__vsscanf` (was fn_00423f30, hand-written in wscanf.c) is the updater's |
| msl/time_g.c | 4102f0 | exact | updater's time.c + three later functions (see the file) |

## How the compiler's own units were built: `-inline auto,deferred`

Every unit in the frontend/optimizer/backend whose asserts can tell (`order` = `rev` in
[units-2.6-table.md](units-2.6-table.md), 47 units) was compiled with **deferred inlining**: Pro 6 parses the whole
file first, then emits its functions **in reverse source order**, inlining any function into any other regardless of
where it is defined. Read from the binary:

* Assert lines **descend** along a unit's `.text` (CExprConvMatch: 3125 at its first function, 581 near its last).
  Ninji's Pro 7 files keep the same order with nearly the same lines, so a unit's rows map onto Ninji's functions
  one-to-one, last first. This is how most of this pass's names and boundaries were settled.
* A unit's **first source functions are its last rows**, and its last source functions its first rows. When a run of
  rows at the *end* of unit X is named like unit X+1, it is X+1's tail, emitted first: the boundary was wrong, not
  the names. Examples fixed this pass: CInt64 (PrintDec .. Xor were "CExpr2_*" rows in CMachine's range), IroUtil
  (CheckForUserBreak .. GetTemp in CError's range; IsIntConstant .. IsVariable at its other end), PCodeInfo
  (change_opcode/change_num_operands), CObjC, CompilerTools (CTool_GetPathName), BitVectors.
* Functions nothing calls are **dead-stripped** by the linker, so a gap in Ninji's order is not evidence against a
  unit (CExprConvMatch has no ClassCopyInit; Registers.c has no retain_register_for_argument).

At the time of this pass the sources were written in **ascending** address order with plain `-inline auto` (built
reversed with deferred inlining as they were, no unit gained and the driver units lost: `tools/compilers.py deferred`
tries it). The consequence: where 2.6 inlines a function into one defined *before* it in the original's source (i.e.
after it in address order), such a source needs a static copy. Those copies are named `*_Inline` and each has a TODO
(`CInt64_Neg_Inline`, `IRO_RenumberInts_Inline`, `IRO_IsIntConstant_Inline`, `Registers_GetVarInfo_Inline`,
`CExpr_AssignmentPromotion_` were the first). They disappear once a unit is rewritten in the original's (Pro7's) order
and built deferred, as most of the optimizer, the front end and much of the back end since have been
([names-2.6.md](names-2.6.md)'s "Deferred units" lists which).

Two more things that make moving code between files unsafe without a rebuild:
* Pro 6 carries state across functions: a function's mere presence can change how a later one is scheduled
  (fn_004c45b0 perturbed fn_004cead0, so it was a unit of its own, `Unit_004c45b0.c`, until it turned out to be
  Intrinsics.c's last function, `va_info_intrinsic`).
  For the same reason, **275 definitions that no 2.x binary has were removed** (by a one-off script of this pass): 1.x's
  statement-level CSE in COptimizer.c, first drafts superseded by later code (InterferenceGraph's SpillCode_* helpers,
  Coloring_*), placeholder stubs at made-up addresses (Parameter.c).
* Static inline helpers, macros, `#pragma` on/reset pairs and struct alignment pragmas must travel with a function,
  and definition order decides what gets inlined (`tools/unit.py move` and `order` carry a function's comments and
  the pragmas and `#if` that belong to it alone, and refuse a move that changes its pragma settings or `#if` branch;
  the result still needs a build and `tools/verify.py` to confirm nothing changed).

## Unit evidence and naming policy

For each unit the table's `name` column says what supports its file name: 2.6's own **assert** strings (certain),
a **Wii** module of that name (certain for the name; Wii's module contents are a later codebase: IroType, IroExprs,
CExprGen, CExprTools, CTypeTools, CTemplateFuncInst are later splits of 2.6 units, so Wii placing a function there is
not a disagreement), **Ninji**'s file (high). A function's name is taken, in this order of preference, from: a Wii
public symbol; Ninji's function with exactly the same assert lines (the same lines in the same file identify the code,
unless the row only inlines that function -- rows that share one helper's assert, or are far larger than Ninji's
function, are left alone); Ninji's function in the same position of a deferred unit, confirmed by body and callers.
This pass's `name_evidence.py` and `check_units.py` graded every row and listed the units whose evidence disagreed
(after it, only the Wii later-split cases above); `tools/evidence.py sweep` does both now, into
[names-2.6.md](names-2.6.md).

## The CodeWarrior plugin library (the "prebuilt SDK" question)

The command-line driver and the compiler plugin both link the CW **PluginLib** (Ninji:
`command_line/PluginLib/Src/Library`), and 2.6 has it twice over:

| Unit | Range | Half | Notes |
|---|---|---|---|
| driver/CWSecretPluginCallbacks.c | 41c390 | driver | Wii module and publics (CWSecretAttachHandle/DetachHandle/GetNamedPreferences). Was "DropInCompilerLinkerPrivate.c" |
| driver/CWParserPluginsPrivate.cpp | 41c480 | driver | one C++ unit (was a .c and a "13.cpp" whose rows interleaved: neither could link). Emits ??4CWCommandLineArgs |
| driver/CWPluginsPrivate.c | 41e8a0 | driver | |
| driver/DropInCompilerLinkerPrivate.cpp | 43ffe0 | compiler | was CPrep_* in CPrep.c. Wii's decorated names (CWIsPrecompiling@8 ...) |

What sets them apart from the units around them:
* **C++ with `-inline off`**: implicit copy-assignment operators (`??4X@@QAEAAU0@ABU0@@Z`) are emitted out of line
  only without auto-inlining, and 2.6 calls them. CLPluginRequests.cpp is built the same way.
* The compiler half's copy has its own `operator=` for the API v8 CWTargetInfo layout (0x4402e0), which the linker
  did not fold into the driver's `??4CWTargetInfo` -- a different type, from a different build of the SDK headers.
* Uncalled entry points (CWGetBuildSequenceNumber, CWSetTargetInfo, CWParserStoreCommandLineForPanel ...) are
  dead-stripped. CWParserStoreCommandLineForPanel is back in the source although 2.6 does not have its code: it is
  the first user of `??4CWCommandLineArgs`, which is why that operator sits before SetParserEntry. The unit now
  links from source with it.

That fits the suspicion that the SDK came prebuilt: the PluginLib objects are compiled with settings of their own
(and twice, with different header versions), unlike the units around them. The driver's `cc-eabi-ppc-mw.c` (the
static plugin's accessors) is likewise one unit with a compiler of its own, Pro 5.3 among Pro 6 driver units.

## What this pass changed, by unit (2.6)

| Unit(s) | Change | Evidence | Confidence |
|---|---|---|---|
| CExprConvMatch.c | whole unit 54cab0-5518b0; all rows named | asserts (reverse) + Ninji + call graph; Wii | high |
| CExpr2.c | CExpr_MatchArgList (was CExpr_FuncArgMatch): lacks its `!copts.old_argmatch` branch (TODO in file) | Ninji | high |
| PCodeInfo.c | starts 529b00 (change_opcode, change_num_operands) | asserts + Ninji | certain |
| CObjC.c | starts 5518b0 (ParseSelectorExpression, ParseProtocolExpression; ParseAtExpression) | Ninji order | high |
| IroPointerAnalysis.c | starts 471e90 | asserts (via __assertion_failed) | certain |
| COptimizer.c | 1.x CSE removed (no 2.x row) | dead_code | certain |
| RegisterInfo.c (was Registers.c) | 4cfde0-4d1040; rows named | asserts (all "RegisterInfo.c"), Ninji, Wii | certain |
| Registers.c (new) | Ninji's Registers.c, 4fe310-4fe6a0 | Ninji order + bodies | high |
| CMangler.c | GetLinkName / DataLinkName / FunctionLinkName were rotated | Ninji order + asserts | high |
| CInt64.c | starts 4c2920 | Ninji order + bodies | high |
| IroUtil.c | starts 468130; first functions at its end | Ninji order + bodies; Wii | high |
| IROUseDef.c | starts 45dc70 | Ninji order | high |
| InlineAsmMnemonicsPPC.c, InlineAsmRegisters.c, InlineAsmRegistersPPC.c (new) | 53c2e0-53cc10 (were in CTemplateNew.c) | Wii publics, Ninji order + bodies | high |
| BitVectors.c | starts 57d350 | Wii/Ninji names, bodies | high |
| ParserHelpers-cc.c | starts 40d215 | matches only in that unit's context; Wii/Ninji names | high |
| CompilerTools.c | starts 4405f0 (CTool_GetPathName) | Ninji order | high |
| MachineSimulation603/603e/AltiVec | own copies of their helpers | ranges | high |
| cc-eabi-ppc-mw.c | one Pro 5.3 unit (mw13.c split gone) | both compilers tried | high |
| ~130 functions | renamed to Wii/Ninji names (R/X grades, call graphs) | see commit messages | per row |
| 14+5 functions | moved into the unit whose range they sit in | linker placement | certain |
| Parameter.c | starts 42a4ed (Param_None); Ninji's names, order and tables (ParamParsers, DescHelpParams, CompareParams); 18 functions only the tables reach, which had no rows | the three tables in .data list every function, in Ninji's order; bodies | certain |

## Open unit questions (TODO)

* Code with no row: a byte-exact function that objdiff shows below 100% because its row runs on into code the
  inventory has no row for (COFF has no symbol sizes: objdiff sizes a symbol up to the next one). Parameter.c's were
  functions reached only through function tables; left: ParserHelpers (40d1de), Option (41e53b, 344 bytes),
  operator new (426f4f), CLCompilerLinkerDropin_V10 (427790, 1344 bytes). Ninji's files should name them.

Functions whose code lies in one unit's range but whose source is another's (`tools/status.py`, `for`/`awy`
columns). Each is either the containing unit's (move it) or the neighbouring unit's tail or head (move the
boundary); without asserts the order cannot decide. Hypotheses:

* **IroEval.c tail 4571f0-457fc0** (two IroJump, one CTemplateNew, eight IroCSE functions): no asserts on either
  side. Ninji's IroTransform does not start with them; probably IroEval's (IroEval has no asserts to order it).
* **NewHand.cpp tail 4440d0-444370** (get_status_string, CError_GetErrorString): compiler code between a Pro 5.3
  runtime unit and InstrSelection -- likely a unit of its own.
* **CParser.c tail 4ea490-4ead60** and **Operands.c tail 4e3d00-4e3e10**: PCodeUtilities-style emitters on both
  sides of CParser; PCodeUtilities follows CParser. Probably PCodeUtilities' head runs from 4ea490.
* **PCodeInfo.c tail 52b220-52ba70** (FunctionCalls / IroPointerAnalysis functions), **ValueNumbering 50a110**
  (Coloring_SetupGPRs), **CInit 50fc90**, **LoopDetection 56e130**, **CInline 562350**, **CTemplateTools 565800**,
  **CTemplateFunc 567da0**, **IroPropagate 471550**, **IroLoop 4ac8e0**, **VectorArraysToRegs 578000**: single
  functions at a range end.
* Moves that need their types brought along first: fn_00574c60 (into ConstantPropagation.c), update_alias_value
  (into StructMoves.c).
* MSL odds: ctype 405830 (MsDos-sourced), string_io 40c6d0 (ttyname), char_io 443ea0 (CLPrefs_ConvertLoneLFToCR).

## Other versions

The renames and moves apply to every version's `functions.json` (a name is the same function in all of them), and
the unit structure carries over: after this pass 2.0, 2.0p1, 2.5 and 2.7 also build without errors and relink to
their SHA-1s (50 sources linked from source each; their generated `splits.txt`/`symbols.txt` are now tracked too).
Their compiler overrides were not revisited (2.6's `cc-eabi-ppc-mw.c` Pro 5.3 override, for one, is 2.6's only).
