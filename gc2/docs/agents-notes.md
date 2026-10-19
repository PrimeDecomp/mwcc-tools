# Notes for agents working on gc2

What an agent working on this decompilation needs: how to build and check a change, the repository's conventions,
what makes a unit link, the tools' pitfalls, and how the versions differ. The compiler behaviour behind matching (inlining,
types, registers, data layout, linking) is in [matching-guide.md](matching-guide.md); this file does not repeat it.
Per-function attempts are in the ledgers ([nearmiss-2.6.md](nearmiss-2.6.md), [nearmiss-versions.md](nearmiss-versions.md));
per-unit findings in [audit-2.6.md](audit-2.6.md), [audit-data-2.6.md](audit-data-2.6.md) and
[review-2.6.md](review-2.6.md). Grep those for your unit, header or function before working on it. The ledgers'
citations of "AGENTS.md" and of this file's former sections (each unit's rewrite notes, the rounds' findings) resolve
in the full history: `git show cedeb0a0:gc2/docs/agents-notes.md`.

## Build and check a change

- `python configure.py` (GC_2_6 by default; `--version V`, `--relink`), then `ninja`. One `build.ninja` holds every
  version; `ninja verify` builds, compares, lays out and relinks all of them (CI runs it).
- After any change: `python tools/verify.py` (`ninja verify` + `tools/regressions.py`). Take a baseline first with
  `tools/regressions.py save`. Regressions are matched by address, so renames neither hide nor fake one.
- After a header edit, check every version: header changes move code in functions that use none of the
  declarations. `tools/headers.py bisect SOURCE` finds the file that changed a source's code.
- After moving code or deleting a macro: `tools/clang_check.py FILE`. MWCC compiles an undefined macro or an
  undeclared function as an implicit call, with no error; implicit declarations are bugs. Grep every use of a macro
  before deleting it, and check what an `#include` removal leaves undeclared (a `__stdcall` callee becomes cdecl).
- Format what you edit by hand: `.venv/bin/clang-format -i FILE...` (CI fails on unformatted sources). Check a mass
  reformat with `verify.py`: braces changed one function's code once.
- After rewriting a unit from Pro7: build, then `tools/update_assert_lines.py GC_2_6 --write` (Pro7's asserts carry
  Pro7's lines). It rewrites other units' sources too.
- After a change that moves code or data: `tools/layout.py` (`ninja verify` fails while symbols.txt and splits.txt
  are out of date), `tools/update_state.py`, `tools/relink.py`.
- Results computed from a stale comparison are refused: `build/VERSION/compare.json` records its inputs, and
  `regressions.py`, `update_state.py`, `layout.py`, `status.py` refuse it when a config file or object changed since.
  Run `ninja`.
- A behaviour change that is not exact is tested with `tools/equivalence.py`: a corpus compiled by the original and
  the equivalent compiler, compared byte for byte.

## Where to start

- `tools/status.py VERSION --todo`: the units nearest to linking and what blocks each.
- `tools/fn.py diff NAME`: the compiled code against the original's, references resolved to names. A line is `=`
  only when its references agree; a wrong global, callee or large constant shows as `~` even at "100% similar".
  "Same similarity" is not "same code": count the differing lines.
- `tools/fn.py variants NAME FILE` judges alternative bodies in parallel and lists the unit's other functions whose
  verdict changes (inlining, deferred coupling). `tools/fn.py decl-order NAME --swaps` searches local declaration
  order; hoist block-local declarations first so it can move them.
- `tools/relink.py VERSION --only SOURCE` links one unit and says where the bytes first differ.
- Record every attempt, failures included, in the ledger entry for the function.

## Sources and references

- **The reference is Ninji's Pro 7 (2.4.5) decompilation.** Start a unit from Pro7's verbatim text, then apply 2.6's
  changes. "Pro7's file" means Pro7's order, names and text: units once called Pro7's files were 28-47% gc1 lines,
  and their misses came from gc1 helpers standing in for Pro7's statics.
- **gc1 (1.3) is still worth reading**: where Pro7 and 2.6 differ in locals, 1.3 often has 2.6's.
- **Pro7's text has its own bugs and placeholders**: wrong error ids and allocation sizes, Ninji's placeholder names
  (`x14`, `unk*`, `OverloadMode0..3`) and joke labels, which need real names (with TODOs). Pro7's
  `CTool_CreateIndexFromPointer`/`CTool_ResolveIndexToPointer` are Ninji's 64-bit-host additions: 2.x stores the
  pointers. Check pushes and call targets before calling a difference a 2.6 change.
- **IDA treats `CError_Internal` as noreturn** and drops the code after it, and sometimes drops arguments; its
  decompiler has shown stale constants. Read the bytes.
- **The Wii compilers' CodeView** (`tools/mw/wii.py`) gives each public function's module and the data publics
  (COMMONs included, module ""). Wii's modules are a later codebase (IroType, IroExprs, CExprGen, CTypeTools and
  others are later splits of 2.6 units). Wii spells "eppc" where the repo keeps "eabi-ppc". Wii's `@N` decoration is
  not proof of `__stdcall` (2.6's Resources/TextUtils functions, OS_CanonPath, OS_MacToRef are cdecl). A Wii procedure
  with no public is `static`. Check a Wii public's section before taking it as the same entity (Wii's
  IRO_IsSubableExpression is a .data function pointer). Wii's global types (sstGlobalTypes) cover only the driver's
  and the SDK's structs, and `wii.py` does not read them yet.
- **The plugin SDK's types** are the driver's: look for the SDK's type before naming a field. SDK flag enums are
  `1L << n`; a literal where the SDK has an enumerator signals a stand-in field type.
- `tools/evidence.py` collects the evidence per function (asserts, Pro7, Wii, MSL): `unit`, `align`, `units`
  (writes [units-2.6-table.md](units-2.6-table.md)), `pair`, `sweep` ([names-2.6.md](names-2.6.md)), `review` and
  `apply` (`config/GC_2_6/name_review.json`).

## Repository conventions

- **Versions.** A version-specific body is `#if VERSION < VERSION_GC_2_5` (and so on; `include/version.h`, configure
  passes `-DVERSION`). The sources are written for 2.6 (`PRIMARY` in `tools/mw/project.py`).
- **Assert lines across versions.** An assert passes its 2.6 line plus `CError_LINE_SHIFT` (CError.h; the
  IroPointerAnalysis `IRO_ASSERT` macros too). Where another release's file had other lines above a point, a block
  redefines the shift; `tools/update_assert_lines.py VERSION` lists the points. `CError_Internal`'s file-name literal
  is not checked by the comparison.
- **`CERROR_FILE` is a mis-split marker.** The asserts' file name is `__FILE__`, which MWCC spells as the bare name;
  a source never defines it for its own name. A define left in a source says its asserts carry another file's name:
  the code is another unit's, or a file the original `#include`d (CObjC.c includes CObjCModern.c and restores
  `__FILE__` for it). Remove each by moving the code where its name says.
- **Names are global.** Function names in `functions.json` and binding names are global across units: two units'
  statics with one name need distinct global names through a `#define` (`#define instruction_timing
  m7400_instruction_timing`, `alloc_g_...` in `src/msl/alloc_g.c`, `constprop_*`), or the comparison binds the
  others' references to the bound one. Statics with no row and no binding need no prefix.
- **One name per function.** A `#define` alias of a function name makes callers reference a symbol no object
  exports (the relink fails). Two names at one .text address mean a stale copy beside the row's body, or the row's
  body is the stale one: compare both against the address before deleting one.
- **Two bindings at one address are a bug**, not an alias. Fragments of one struct bound separately are a runtime
  bug too: the equivalent compiler then has separate variables. Bind the struct's start, delete the fragment
  bindings, and make their uses fields. `tools/bindings.py refs VERSION ADDR` lists the original's functions
  referencing an address; `refs --wii NAME` and `refs --wii --function F` a Wii public's.
- **Statics while a function is lifted.** Lifted code reaches data through bound names and through the comparison's
  placements (a datum an exact function places at an original address, `compare.json`'s "data"); an address inside a
  variable a compiled object defines is that variable plus the offset. A static no exact function reaches has no
  known place: lifted code then reaches the original's data image, a copy of its own. Keep such a variable global,
  bound in `config/*/bindings.json`, with the comment `(the original's statics; global while a lifted function of this
  unit still uses them)`, until an exact function reaches it. A static function a lifted one calls is made external
  by the link. A binding for a global another unit's exact code references stays even when a compiled object
  defines it.
- **Small workarounds are acceptable** when the natural source has been tried hard and the workaround changes no
  behaviour (a `volatile` parameter, a copy through an unused local, a literal padded with NULs, a per-function
  `#pragma`). Mark each with a `TODO` saying what the original evidently did. Not for fake structs, offset macros or
  aliases. A spelling plausibly Metrowerks' is the original's shape (`(T*)((long)p + n)` is their house idiom).
- **Literal allocation sizes**: write `sizeof(T)` and check the `push` against the original's. A literal smaller
  than `sizeof(T)` where the original pushes the same literal means the repo's type is too big. Clang sizes
  byte-sized enums as 4 bytes: measure such structs by MWCC's rule.
- **Literals**: read a string's bytes in the original before trusting it (single backslashes in a `"\\StringFileInfo"`
  path compiled without complaint).
- **Headers.** The low layers define their types in Pro7's `typedef struct X {...} X;` form; common.h does not typedef
  the driver's or Win32's types. The shell's mutually dependent units get their typedefs from `driver/cmdline.h`, the
  option system's from `option_system.h`, the parser's from `parser.h`. Keep the include graph acyclic: a header
  needing a type only through pointers includes the layer header, one embedding it includes its definition. The
  compiler's headers name driver types by struct tag with a forward `struct X;`. The plugin contexts are the SDK's
  `CWPluginPrivateContext`, `CWParserContext`, `CWCompilerLinkerContext` (CWPluginsPrivate.h); Pro7's derived
  contexts work as C++ inheritance under `#ifdef __cplusplus`.
- **Types already Pro7's**: the back end's `PCode*`, `PCOp_*`, flags, `Opcode` (with `PC_INVALID = -1`), `OpcodeInfo
  opcodeinfo[]` (18 bytes an entry: `#pragma pack(2)`), `RegClass`, `used_virtual_registers[RegClassMax]` (2.6
  declares it `long`), the register allocator, `UseDefChains.h`, the IA* inline-assembler types (InlineAsm.h),
  `CompilerLinkerOptions` (`COpts`, CParser.h, its own pack(2) block), `DeclInfo`, `TokenStream`, `ParserTryBlock`,
  `DeclBlock`, `CompilerLinkerParamBlk`, the OS layer (`include/driver/oslib.h`: Pro7's types at 2.6's sizes). CExpr2.h
  is merged into CExpr.h.
- **`OPT_OFFSET` must be `&((COpts *) 0)->x`**: Pro7's `(char *) &copts.x - (char *) &copts` is not folded by MWCC x86.
  COpts members are named by offset from 2.6's `compileroptions` table against Pro7's; invented ones are TODO
  (`soft_float`, `use_fp`, `sdata_threshold`); `bssSection`/`smallDataSection`/`smallBSSSection` are really
  `#pragma section`'s const/sdata/sconst sections (misnamed, not yet renamed). A `copts` byte no pragma covers is never
  set in 2.6.
- **`Type.type` as Pro7's signed `TypeType` is not neutral**: it must cover every Type-prefix struct's kind field and
  costs functions that copy the kind into a `UInt8` local until those are `TypeType` too.
- **MSL units are the library's sources** (`lib/msl`, through `#include <file.c>` wrappers in `src/msl`). Hand copies
  of library functions give code and data the original lacks. The library's dead functions keep their data in the
  unit. `src/msl/assert.c` is MSL's `__assertion_failed` with the console branch lib/msl's copy lacks;
  `include/msl/win32/windows.h` is WINDEF.H and WINBASE.H. MSL and the runtime build with pro53 in every version
  (`compiler_overrides`). An MSL object shipped without source (ansifp_x86.obj) has its data in its symbol table.
- **2.6 was compiled against Pro 6's MSL headers and linked with Pro 5's MSL library**: the compiler's own `errno`
  reads word 1 of `_ThreadLocalData` where the library's is word 0 (a real bug of its build, reproduced by plain
  `errno` through `include/libc/errno.h`). Check any other MSL struct the compiler's own code reads against Pro 6's
  headers. `include/libc/stdarg.h` is MSL's x86 one: use `va_start`, not private macros.
- **Option and message tables are generated.** ParserGlue-eabi-ppc-cc.c's .data is Pro7's ParserGlue: the preference
  panels (`include/driver/pref_structs.h`), the option tables `#include`d from `src/driver/Opts-eabi-ppc-cc/*.opt`,
  then `prefs`, `optLists`, `stPrefPanels` and the `parser`. `tools/gen_options.py VERSION ROOT` writes the .opt files
  (`--report`, `--name`, `--string-table ADDR=NAME`). The tables are 2.6's; other versions' differ. Big tables
  (opcodeinfo, the mnemonics, the SPR/DCR tables, Intrinsics' type tables) were generated from the binary with
  `#pragma pool_strings on` so the strings follow the table; a `pool_strings` unit needs every literal in its pool.
- **Intrinsic names**: the 315 `Intrinsic_NNN` are named after the runtime object each index gets (TODO).

## Repository-specific code shapes

The general shapes are in [matching-guide.md](matching-guide.md) sections 2 and 3. These hold for this codebase:

- The pointer analysis' `IRO_ASSERT` is the expression form `((expr) ? (void) 0 : __assertion_failed(...))`.
- 2.6's `CError_FATAL` is the bare call, not Pro7's do/while(0).
- A `short` token compared with `movsx` then a 32-bit compare is `(int) tk == K`; the `TK_` constants stay
  `#define`s (an enum lost 20 functions). Section kinds (`SECT_DEFAULT` ...) and opcodes are `#define`s too.
- `CError_ASSERT(n, v = f())` keeps an inlined result's copy-then-test; `left = conv.left; CError_ASSERT(n, left !=
  NULL);` is a separate statement (and counts toward the auto-inline limit).
- Pointer-analysis walks (`PointerAnalysis_LookupLinearNodePointerExpr` loops in IroCSE, IroVars, IROUseDef,
  IroRangePropagation) are mostly `if (resultList) { for (scan = resultList; ...)` with the second walk reusing
  `scan`, where Pro7 has `if ((list = resultList))`; try the combinations first.
- 2.6's EABI pattern in an InstrSelection generator: `if (TYPE_IS_SOFT_FLOAT(type)) { SFPE_gen_*(...); return; }`
  first, then `IS_TYPE_VEC2X32FLOAT` (`PC_PS_*`), then `IS_TYPE_FLOAT_NOT_VEC2X32FLOAT` (Pro7's float branch, its
  `return` become an `else if`), then the integer path. `RLWINM_MASK` is `((x) > 31) ? 0 : (0xFFFFFFFF >> (x))`. A
  64-bit `switch` needs a signed `long long` selector (`ULL` cases crash MWCC).
- 2.6's use-def queries are `may_alias_object(pcode, Defs[id].v.u.object)` / `must_alias_object` (Pro7: `may_alias`
  / `uniquely_aliases` on two pcodes), with `TinyValue* v = &def->v;` right after `def = &Defs[defID]` (the sign: a
  `lea r, [def + 4]` in the prologue).
- The driver's `OS_ASSERT(line, cond)` is `if ((cond) == 0)`; the C++_Parser asserts are MSL's `assert` form
  (`OPTION_ASSERT`, oslib.h). Varargs reporters (`CLReport` and the CLP* family) take `SInt16` ids.
- The OS layer returns Win32 error codes where Pro7 has errno values: write them with WINERROR.H's names. OS layer
  functions are `__stdcall` where Wii decorates them unless a `ret` says otherwise.
- The C++ driver units need `-bool off`; C++ units with implicit `operator=` calls need `-inline off`; runtime C++
  units need `-RTTI off` (their shared inline virtuals are COMDATs, so relink.py's "defines functions of other units"
  is noise there).
- 2.6's own auto-inline limit (for the code it compiles) is 30 statements; the host's is Pro7's 15.

## Making a unit link

A unit counts once its object links into the original byte for byte. What blocks a unit whose functions are all exact,
beyond [matching-guide.md](matching-guide.md) section 1.4:

- **Its boundary.** A deferred unit's first source function is its last row: a run of rows at the end of unit X
  named like X+1 is X+1's tail. A table of function pointers names a unit's functions. A function of one unit
  writing another's static means one object. A new 8-aligned run of data inside a unit's range is a new object; a
  range marked `align:4` in splits.txt has a wrong boundary. A Pro7 file another `#include`s is not a source of its
  own. Give a new unit its own source (`config/sources.json`, `tools/unit.py assign`, every version) and rename with
  `tools/rename.py --unit`.
- **Its flags.** Sweep `-sym on/off` and deferred before hand-shaping. Only on/off matters for `-sym`: the format
  keywords compile like `on`, and `-sym full` is not an option of these compilers. A per-version `flags` override
  can be stale: one left from a unit that held another unit's function, or one 2.6 has dropped. `-sym off` for every unit loses
  hundreds of functions: the per-unit settings are the original build's. A file-top `#pragma sym`, `scheduling off`
  or a compiler override was often a stand-in for the unit's real flags.
- **Statics.** Allocated in reverse declaration order, near their first user in 2.6. Statics left `extern` and bound
  leave a unit's .bss empty with nothing flagging it: define them `static` in the reference's order and check that the
  comparison's placements share one base. The packing in force at the end of the file lays out .bss: several units
  end in `#pragma pack(2)` (TODO). `#pragma options align=reset` works.
- **COMMONs.** mwld lays out COMMONs of one alignment by `split.mwld_hash` of the name, so a guessed name must sort
  between its neighbours (take them from the current symbols.txt). A COMMON the original lacks moves the COMMON tail
  and breaks every relink at the tail's references (a relink differing only at references to
  `setOldOptimizerOptions` says so): keep such names `extern`. COMMONs no source defines compile silently and only
  fail once the unit links; define them where Pro7 does. Where a tentative definition lives moves COMMON too.
- **Dead functions.** The linker drops them but keeps their literals and statics: add Pro7's function back (with a
  comment) when the data has them. A dead function calling something 2.6 lacks fails the relink, so leave it out. A
  dead function's jump table in a deferred unit shifts later tables: write dead functions without tables. Assert line
  numbers on both sides of a Pro7 dead function put it in the file.
- **Extra out-of-line copies.** A static inlined at every call still gets an out-of-line copy, and its switch table
  stays in .data: `static inline` drops it.
- **Data nothing places**: `config.json` keys place it.
  - `"data_units": {SOURCE: AFTER_UNIT}` links a data-only object (MSL's locale.c and float.c after Unmangle,
    throwsbad.cpp after NewHand, OpcodeInfo.c after CDecl).
  - `"data_at": {SOURCE: {".data": "0x..."}}` (also `.bss`; `.data_end` bounds it, leaving out a dead function's
    table the linker dropped) places data no exact function references and
    too short for its bytes to place (`layout.py` needs 8).
  - `"data_head": {SOURCE: {".data": ADDR}}` gives a unit the data before what its object places.
  - `compiler_overrides` and `flags` set a source's compiler and flags per version (`config/sources.json` has the
    defaults).
- **A stand-in proves nothing.** MsDos.c defines `unused_zeroes[0x8000]` (TODO) for 32 KB of unreferenced zeros
  whose owner is unknown.

### layout.py

- symbols.txt names come from the configuration: rows name functions, bindings name data; the compiled objects only
  place units and data, and name what the configuration does not. `layout.py` warns on each disagreement;
  `validate.py` refuses a binding at a row's address under another name.
- **No symbol may contain another.** A symbol a compiled object places has its object's extent; one named only by a
  binding runs to the next symbol. A symbol inside another is a stale fragment binding, a struct whose size differs
  from 2.6's, or a size guessed across another object.
- **.bss statics are named** from each placed object's symbols-on CodeView (`S_LDATA32` records): MWCC's COFF has no
  symbol for a `.bss` static. A static's binding can go once its unit links.
- **Every symbol is a real name or the compiler's.** Exception action tables are `@exc_FUNCTION`; an inexact
  function's tables are named by its compiled code's references in the same order; a COMMON's size comes from its
  symbol. Three per version are left that nothing can name: Help_Option's and call_function's local initializer
  templates (MWCC gives a zeroed aggregate's template in `.bss` no symbol and no CodeView record) and 0x21d
  unreferenced `.bss` bytes before Peephole's `defininginstruction`.
- A unit's data extent ends at the last section its exact functions anchor (an inexact function's jump table, sized by
  its wrong cases, would cover the next unit). A placed unit's .bss runs to the last address it owns, and an aligned
  gap of 8 or more between two placed units goes to the first object between them. The padding before a placed unit
  that holds an unnamed referenced address (`intbuf[len - 1]`, `signal_funcs[sig - 1]`) is that unit's.
- "One Matching claimant wins" applies only when the row's own source is not a claimant.
- An object's .data need not start 8-aligned after a 12-byte table: relink.py's size check then reports a phantom +4.
- Dropping includes renumbers MWCC's `@N` literal labels: a symbols.txt diff after an include cleanup is expected.

## Tool pitfalls

- `tools/rename.py` leaves struct members alone (after `.`/`->`, or declared in a record body) and lists them: use
  `--member RECORD.OLD=NEW` (needs a record name; anonymous union members by hand). `--member` flattening an embedded
  struct (`File.listEntry=next`) writes `&file->next`: fix those sites by hand. After a type swap, grep the struct
  bodies. A `#define x ((T *) x)` alias becomes self-referential after a data rename. A rename to a name another unit
  uses is refused; `--force NEW` once the other is out of the way. It renames every spelling of a binding (`_OLD@N`).
- `tools/includes.py trim` drops headers a unit uses directly when another include brings them in: check what it
  removes. `includes.py fix` gives up when a type is also forward-declared in common.h; `includes.py add` does not
  handle undeclared enumerators. clangd's "header not used directly" is wrong for struct types here (include-cleaner
  credits a struct to common.h's typedef; `.clangd` turns the hint off).
- `tools/update_assert_lines.py --write` rewrites other units' sources: run it with care while others are in flight.
- A function's variant in a deferred unit can break another: check the whole unit (`fn.py variants`' "others:").
- lib/msl's files have CR line endings and a (c) byte: ugrep skips them silently; use `grep -a`.
- A scratch script named like a standard module (`dis.py`) breaks Python's imports: name scratch files distinctly.
- Copy merged files with a fresh mtime (`shutil.copy`, not `copy2`): an older mtime leaves ninja thinking objects are
  current. After merging renames, rebuild, then run `tools/layout.py`.
- An empty string passed to `str.replace("", new)` writes `new` between every character (a source became 54 MB and
  MWCC seemed to hang).
- ObjC data stops both 2.6 and the equivalent compiler with an internal error (ObjGen_PPC_EABI.c line 1301): compare
  diagnostics only.

## The versions

All versions build from one source. 2.6 is the primary; the others' differences sit under `#if VERSION ...`. Every row
is compared but the three version.dll import thunks. Port a version's layout from another with
`tools/port_version.py FROM TO`. Per-version configuration (`data_at`, `data_units`, overrides, hand-added rows) must
follow 2.6's: 2.5's and 2.7's data addresses are 2.6's, 2.0's about 0x1000 lower (find them by the bytes around 2.6's).

### What each version differs in (the version round)

- **2.0 front end**: `CompilerLinkerOptions` has no `old_style_bitfields` and no `unknown66[2]`, so every member from
  `oldprefixname` on is 2 bytes lower (`#if VERSION >= VERSION_GC_2_5` in CParser.h; CInlineBackendOptions likewise).
  Its `compileroptions` table is 2.6's less `old_style_bitfields`. `Object.u.data.reginfo` comes before `linkname`;
  `GetLicense` always asks for license version "1"; LinkErrorsPPC_EABI's `format_string` unmangles into 256 bytes;
  `CPrec_GetStatementPatch` has no `sourcefilepath` patch.
- **2.0 bitfields**: `CMach_StructLayoutBitfield` is one function, Pro7's old layout with two additions (a first
  bitfield after other members shares the unit those members end when its bits fit; a unit of a smaller type grows
  into the bitfield's larger type), so `U8 a : 8; U32 b : 24;` is one 4-byte unit.
- **2.0 alias analysis and use-def**: Pro7's model with some of 2.6's helpers (`#if VERSION < VERSION_GC_2_5` in
  Alias.c, UseDefChains.c): one worst case (calls get `worst_case_ptr`); `may_alias`, `uniquely_aliases`,
  `may_alias_object` on PCodes; Pro7's two-register rule in `gather_instruction_alias`; `UseDefInfo` is 32 bytes with
  eight full `UInt32*` sets updated in place; `computelocalusedefinfo` skips branches; no `last_usedef_register`.
  2.0's IROUseDef `UpdateUse`/`UpdateUseForOtherSelfAssignment` are Pro7's. 2.0's ObjectIsAnExtendedParamCandidate
  also tests `datatype == DABSOLUTE`; its EvalProcAction analyses an asm statement where 2.5 aborts; it has no
  TreeContains; `precedes` is CopyPropagation.c's in 2.0.
- **2.0 and 2.5** convert constants to and from the paired-single custom types (CMach_CustomIntConvert); 2.6 and 2.7
  return 0.
- **2.0p1** is 2.0's executable with three bytes changed, in two functions (`#if VERSION == VERSION_GC_2_0P1`):
  TOC.c `createfloatconstant` (the literal's `qual` is `Q_INLINE_DATA` without `Q_CONST`) and CodeMotion.c `maymove`
  (in loops over 25 instructions it keeps `lfs`..`lfdux` in the loop instead of the vector splats). The patch rewrote
  a compiled range test's constants; from source MWCC compiles those cases to a jump table, so 2.0p1's `maymove`
  cannot match.
- **2.5**: `TypeBitfield` is 0x10 bytes (two chars after `suppressAlignment`; the others 0xe); its bitfield layout
  is a third form (2.0's function refined when `old_style_bitfields` is off, with a static `cmach_curbfintegral`).
- **2.7**: the object writer looks symbols up through a 4096-bucket hash table (BE_symbol.h: `hash[0x1000]` before
  `first`, a `hashNext` link); every DWARF entry and line table gets a symbol of its own (`.dwarf.%04x.NAME`,
  `.line.NAME`) and relocations refer to it (2.6 has the feature's leftovers); BE_elf's debug relocations are always
  kind 24, BackPatchRelocationAddend has no assert, the CodeWarrior version record major is 11; MakeSymbolTableEntry
  is no longer inlined.
- Rows that were cut one function off in the other versions: OS_Delete, OS_AppendHandle, InstrSelection's
  gen_OR/XOR/AND/SHR/SHL.

### GC_2_0p2

BFBB's retail compiler is a 2.0 that no release matches. GC_2_0p2 is a derived version (`config/GC_2_0p2/config.json`
`"derived_from": "GC_2_0"`): 2.0 with these rules, each one statement about 2.0's alias analysis, fitted by
measurement on BFBB's code:

- floating-point literals are not const (2.0p1's TOC.c patch; 2.0p1's `maymove` patch is not in it);
- a literal load's alias is `worst_case_ptr` (`gather_instruction_alias`), so a store to a global or through a pointer
  kills it;
- 2.5's const-pointee alias (`make_alias(pseudo, 0x7FFFFF, 1)`) and 2.5's rule for two known address registers;
- the scheduler orders loads and stores addressed by a global's symbol like accesses through a pointer
  (`may_alias_scheduling`);
- such a store's alias is `worst_case_ptr` (value numbering forgets other globals and literals; `return --g;`
  reloads).

Tried and worse: every store to a global as worst case, indexed stores to globals too, all global loads in alias
analysis, the literal rule in value numbering only, the whole 2.5 back end. Left: front-end CSE across an indexed
store to a global array (`a[n] = x; n++` reloads `n` in retail). It has no original: built with
`ninja build/GC_2_0p2/source-only/mwcceppc.exe`, and on the BFBB decomps it matches as many functions as the best
binary-patched compiler. GC 1.2.5n through 1.3.2r keep the literal across stores as 2.0 does: BFBB's compiler is not a
1.3-era one. Our source-only 2.0p1 compiles all 224 BFBB game objects byte-identically to the real 2.0p1.

## Bugs of the original

Found while matching (details in the review entries of [review-2.6.md](review-2.6.md) and comments in the source):

- `I8_ShiftLeftImmediate` emits `srawi` by `shift` where `32 - shift` was meant.
- `rotatedefinedusedtest` scans `instr`'s operands (Pro7 too).
- A non-calling interrupt routine restores the wrong slots when its mask is sparse.
- The paired-single load passes `reg` where `vi->reg` was meant; the psq side-effect test omits STU/STUX.
- `memoryoperand` passes the Object to error 122.
- `I8_extract_bitfield` has a dead `offset == 0` path.
- `Intrinsics_ReInitRuntimeObjects` checks the wrong names for indices 163/165/167 and 204-210.
- A failed type search in Intrinsics reads 0x119 (`vector_all_lt` has no NULL entry).
- 2.0's `gen_negated_condition_gpr` returns the RLWINM's register in its `value == mask` case (fixed in 2.5).
- The build's `errno` reads the wrong word of the thread-local data (above).

## Open

- `new T[n]` (T with a constructor, `n` variable) at -O0 or -O4,p gives one exception-table byte 06 for the
  original's 0a, in the equivalent compiler only (not the relinked one): data in a non-linking unit.
- `interrupt_routine_flags` does not fit COMMON hash order though Wii's public matches the referencing functions.
- LocationSetSet_Remove/Add are exact only under some header sets: their source is probably not the original yet.
- IroVars.c's private IndirectRecord `x26`; IroPointerAnalysis.c sets `IROLinear.evaluated` with 1/0.
- The near-misses left are in the ledgers; `tools/status.py VERSION --todo` is the current state.
