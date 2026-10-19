# Notes for agents: the full history

Moved out of AGENTS.md (October 16, 2026) to keep the file every agent reads short. Nothing was dropped: this is
AGENTS.md's catalogue of lessons, each unit's rewrite and 2.6 changes, the rounds' findings, and the state of the
optimizer cleanup, as they were written. Grep it for your unit, header or function before working on it; the general
lessons are distilled in docs/matching-guide.md. Add new unit-specific findings here, general ones there.

## Lessons by topic (from AGENTS.md's Style section)

- **MWCC's code depends on more than the code.** Adding or removing declarations in a widely included header changed
  register allocation in functions that use none of them (Intrinsics.c `fn_004cead0`, twice: a prototype added to
  PPCError.h; type definitions removed from IroLoop.h). Even the include directory's name length changed it. After any
  header edit check every version (`verify.py` + `regressions.py`); `tools/headers.py bisect SOURCE` finds the changed file.
  What fixed it both times: give the affected unit only the includes it uses. The third time (October 2026, the CW*
  prototypes moved from CPrep.h to DropInCompilerLinker.h) nothing in the headers brought it back. A function that only
  matches under one set of declarations is a sign its source is not yet the original's: clean the unit rather than
  shaping headers around it (Intrinsics.c was then rewritten from Pro7, below; that function is its
  `load_bytereversed_intrinsic`). A bisect showed what such matches are: the old body matched (non-deferred) only
  while the declarations before it fell in a window (52ce173 plus 0..280 dummy `extern int`s matched, 281+ did not;
  083fe88 plus 150..470 matched), no single header hunk flipped it either way, and in a deferred build the same
  function instead flipped with the count of its own unused locals (1, 3, 5-7, 10+ yes; 0, 2, 4, 8, 9 no). The real
  fix was a type: its `reg` is `SInt32`, not Pro7's `int`. So when a function flips with unrelated declarations or
  dead locals, look for the wrong type (or shape) in the function itself; sweeping dummy declarations
  (`extern int dummy_N;` before the unit, unused locals in the function) is a quick test of whether it is that kind.
- **clangd's "header not used directly" is wrong for struct types here**: include-cleaner credits a struct to the
  `typedef` in `common.h`, so it calls the header with the definition unused, and removing it breaks the MWCC build.
  Use `tools/includes.py trim` (clang errors decide) and `fix` (MWCC errors decide); `.clangd` turns the hint off.
- **Deferred inlining.** A unit whose functions descend in address (the unit table's `rev`) was built
  `-inline auto,deferred`: the binary holds the functions in reverse source order, and a function defined later can be
  inlined into an earlier one. Write such a unit in the reference's order with that flag.
- **Statics and lifted code.** The equivalent compiler takes each inexact function from the original ("lifted"); lifted
  code reaches data through bound names, and (since October 2026) through the comparison's placements: a datum of a
  compiled object that an exact function places at an original address (`compare.json`'s "data") is where lifted code
  reaching that address goes, static or not. A static no exact function reaches has no known place: lifted code then
  reaches the original's data image, a copy of its own, and the two split (a crash at -O2 came from that). Keep such a
  variable global, bound in `config/*/bindings.json`, with the comment `(the original's statics; global while a lifted
  function of this unit still uses them)`, until an exact function reaches it. A static function a lifted one calls is
  made external by the link.
- **Where a tentative definition lives moves COMMON.** Globals not declared `static` (even ones written `= 0`) are COMMON, and
  mwld lays COMMON out by the object that defines it first: moving `IRO_Log`'s definition from IroDump.c to IrOptimizer.c (where Pro7 has it)
  broke the relink. 2.6's .bss addresses are the evidence of where a global was defined.
- **Unit .bss** comes from statics once an exact function anchors the unit (`layout.py`); a unit whose statics are
  only partly `static` gets a short .bss and its neighbour the rest. Statics are allocated in reverse declaration order.
- **Dead functions are in the original's source.** The linker dropped functions nobody calls, but their string literals
  stay in the unit's .data (2.6's IroDump .data still has the strings of Pro7's dump functions). Keep the reference's
  dead functions when the data needs them; a relink exclusion like ".data is 1720 bytes, the unit's 1992" says so.
- **Function names are global in `functions.json`.** Two units' statics with one name cannot both be rows; the repo
  prefixes one with its unit (MSL's allocator: `alloc_g_...` through `#define`s in `src/msl/alloc_g.c`). Statics with
  no row (data) need no prefix once no lifted code uses them: IroMalloc's `start_`, `fix_start`, `initialized`.
- **Copies of 1.3 code** pasted into 2.6 units left dead duplicate definitions; check `functions.json`'s `source` before
  trusting a definition.
- `COpts` (CompilerLinkerOptions) in 2.0/2.0p1 has no `old_style_bitfields` and no `unknown66[2]`, so every member
  from `oldprefixname` on is 2 bytes lower (asmsemicolcomment 0x63): `#if VERSION >= VERSION_GC_2_5` in CParser.h,
  and CInlineBackendOptions (CInline.h) has no `old_style_bitfields` either (October 16, 2026; see "2.0's front end").
- **2.6 differs from Pro7 in small, regular ways.** Where Pro7's body is close but not exact, try: an early
  `return` for a Pro7 `if/else` (FindAssignmenttoInductionVar), `if (f()) { return TRUE; } return FALSE;` for `return f() != 0;`,
  `if (!(x = f()))` against `x = f(); if (!x)` (both directions occur), `counter++` before the assignment beside it,
  a global used directly instead of a Pro7 local copy (`FunctionName` for `proc`), and an `int` return for `Boolean`
  (`xor eax, eax` rather than `xor al, al`), `(x & F) == 0` for `!(x & F)` as a call argument, a parameter reused
  instead of Pro7's local (`indirect = indirect->u.monadic`), an early `break` for Pro7's `if (!result) { ... }`. Then
  `tools/fn.py decl-order` for the registers (hoist block-local declarations first so it can move them).
- **Pointer-analysis walks in 2.6** (the `PointerAnalysis_LookupLinearNodePointerExpr` loops in IroCSE, IroVars,
  IROUseDef, IroRangePropagation) are mostly `if (resultList) { for (scan = resultList; ...)` with the second walk
  reusing `scan` (no `list` variable), where Pro7 has `if ((list = resultList))` and `for (list = ...)`; a few keep
  Pro7's form. Try the combinations before anything else.
- **Asserts from Pro7 carry Pro7's line numbers.** After rewriting a unit from Pro7, build and run
  `tools/update_assert_lines.py GC_2_6 --write` (it rewrites only the pairs every site agrees on).
- **`int` and `long` (`SInt32`) compile differently** though both are 32 bits: a `long` local is computed in its stack
  slot where an `int` goes through a register (CObjCModern's `size`); an `int` parameter where 2.6 has `SInt32` swaps
  callee-saved registers, in the function and in its callers (CLDependencies' `make_dependency_osspec`,
  `contains_dependency`); mixed `int` + `SInt32` arithmetic adds a register copy and a saved register
  (`read_file_into_buffer`'s `extraBytes`). When only registers differ and declaration order does nothing, give
  locals and parameters Pro7's integer types, the callees' included.
- **A `#pragma` right after a function's `}` already applies to it**: MWCC generates a function's code once it has
  parsed the start of the next declaration, so `#pragma sym on ... } #pragma sym reset` compiled CWGetTargetInfo with
  symbols off; put the reset after the next function. With `-sym off` a run of field copies alternates registers
  (`ax, cl, al, edx`); with symbols on each reuses `eax`.
- **A parameter the original loads in the prologue** (into a scratch register beside the others) where ours loads it
  at its use is often typed as the struct the function dereferences: `IRO_TruncateBitfieldValueToType`'s third
  parameter is a `TypeBitfield *`, not Pro7's `Type *` read through `TYPE_BITFIELD()`.
- **A spilled variable assigned in both arms** compiles to `cmp [mem], 0` and a reload after the join; the original's
  `jne <next>`, store to the slot, `test reg` is a short-lived variable assigned in the arms and copied once into the
  spilled one (IroRangePropagation's `analysend`, then `inner = analysend`).
- **A block layout that looks like a ternary's** can come from a statement repeated in both branches: Pro7's
  `*value = parm_format_buffer;` in each branch of DescHelpParam_Number's if/else, rather than once after it, keeps
  MWCC from tail-merging one arm whole; the ternary that imitated it cost a `mov` for its value.
- **A file-level static is cached across calls**: MWCC keeps a `static` that is not address-taken in a register over
  calls (IRO_SplitLifetimes reads `NumDefs` once for two `Bv_AllocVector`s). A global the repo still keeps for lifted
  code cannot match that until the unit's other users are exact and it can become `static`.
- **Stale results are refused, not trusted**: `build/VERSION/compare.json` records what it was computed from, and
  `regressions.py`, `update_state.py`, `layout.py`, `status.py` and the rest refuse it when a config file or object
  changed since (run ninja).
- **symbols.txt names come from the configuration** (October 2026): rows name functions and bindings name data; the
  compiled objects only place units and data, and name what the configuration does not. Before, an object's names won,
  which hid stale bindings at wrong addresses (`cmachine_filename`, `text_section_name`, the objc prefixes: the objects
  of inexact units placed their globals by a section anchor their bytes did not agree with) and kept renamed rows'
  old names. `tools/layout.py` prints each disagreement as a warning; `tools/validate.py` refuses a binding at a row's
  address under another name (41 stale ones were deleted in 2.6).
- **Enum-typed fields are reloaded.** MWCC does not reuse a value it loaded through an enum type, but does for a
  `UInt8`: where the original reloads (`IROLinear.nodetype`, `IRO_NonAssignmentOp[]`), the field is Pro7's enum
  (`ENodeType`), not a byte. A `volatile` cast that "fixed" such a reload means the type is wrong.
- **More 2.6 shapes:** an inverted if/else (`if (!flag) x = pred; else x = NULL;`), a loop counter incremented before the
  pointer advance, chained assignments (`a = b = 0`, `IRO_EndNode = IRO_LastNode = fnode`), `limit > i` loop tests,
  `p != NULL && q != NULL` operands of `&&`, a local copy of `fnode->last` tested in the condition, register choice
  following the order variables are first used (reuse a different variable in an early loop). A block of `+` lines
  in a diff is usually a call or check Pro7 lacks: read the original there (IRO_RenumberInts in ExtractLoopInfo,
  ReorderOperandsForReductionPattern's duplicate-and-paste, the unsigned branch of IRO_ConstantFolding).
- **The pointer analysis' `IRO_ASSERT` is the expression form** `((expr) ? (void) 0 : __assertion_failed(...))`;
  the `if (!(expr))` form cost about 20 functions there.
- **A unit can hide another.** The repo's "IroLoop" range held IroLoop and IroExprRegeneration: list the range's
  functions in descending address with their callees and asserts' file names (the order follows the reference's file),
  and give the second unit its own source (`config/sources.json`, `tools/unit.py assign`: every version).
- **A version-specific body** uses `#if VERSION < VERSION_GC_2_5` (include/version.h; configure passes `-DVERSION`).

- A scratch script named like a standard module (`dis.py`) breaks Python's own imports; name scratch files distinctly.
- **`-sym on`/`off` changes code**, not only debug info: the same 20-byte `serializes` compiles `ecx/eax` with symbols on
  and `edx/ecx` off. A version config's `sym_on`/`sym_off` entry can be a stale knob from a unit that held another
  unit's function (MachineSimulation603 was in `sym_off` for AltiVec's `get_opcode_table_entry`). Only on/off matters:
  pro4-pro6's `-sym` takes `off`, `on`, `[no]codeview`, `[no]macsym`, and the format keywords compile CExpr.c's .text
  byte for byte the same as `on`; `-sym full` is not one of these compilers' options ("Unknown option 'full'", then
  `on`).
- **Unit boundaries from function tables.** A table of function pointers (the scheduler's `MachineInfo`s) names each
  unit's functions: every machine model's first function in the repo's range was the previous unit's `serializes`
  (the units are deferred, so it comes first).
- **Structs while a function is lifted.** Lifted code reaches data placed by exact functions (above), then bindings: an
  address with a binding of its own is that name; an address inside a variable a compiled object defines (by the
  binding at the variable's start and the object's extent of it) is that variable plus the offset
  (`tools/equivalent.py`); anything else is the original's data image, a copy of its own. So a unit's statics can be Pro7's structs while one of its functions is lifted: keep
  them global with the usual comment, bind each struct's start, and delete the bindings of the old fragments inside it
  (MachineSimulation7400). Binding names are global like row names: two units' statics named alike
  (`instruction_timing` in every machine model) need unique global names (`#define instruction_timing
  m7400_instruction_timing`), or the comparison binds the others' references to the bound one and they stop matching.
- **Data order is definition order**, but 2.6's machine models have their `MachineInfo` before the timing table Pro7
  defines first: define it first, after prototypes of its functions. A switch's jump table follows the data.
- **`CERROR_FILE` is a mis-split marker, not a tool.** The asserts' file name is `__FILE__` (common.h's default),
  which MWCC spells as the bare name ("Switch.c"), so a source never needs to define it for its own name (111 such
  defines were removed). A define left in a source says its asserts carry another file's name: the code is another
  unit's, or another file the original `#include`d (CObjC.c includes CObjCModern.c and restores `__FILE__` for it).
  Remove each by moving the code where its name says. Left (October 2026): PCodeAssembly ("SFPE_PPC_EABI.c"), Targets ("options.c"), CLFileOps ("DropinFileRecord.c"),
  CScope ("NameSpace.c", "CScopeParseResult.c"), IroPointerAnalysisADTs ("IroPointerAnalysis.c"), DWARF
  (`dwarf_filename`), CObjC (`cobjc_filename`, "id"), Intrinsics, CInline (redefinitions), the two `_V10.cpp`
  sources (".c": the original's files were C), StackFrame (`CERROR_FILE_NAME`), and "unknown.c" in Parameter and
  CRTTI (their asserts' real file names are still to be read from the original; IroTransform's is "IroTransform.c",
  the string at 0x5aa6e0).

- **More 2.6 code shapes (October 2026, second pass).** Each one turned a near-miss exact:
  - A parameter copied into a local of *another pointer type* (`TypeIntegral *t = TYPE_INTEGRAL(type)`, tests on `t`,
    `return type`) keeps two registers where a same-typed copy is coalesced (IRO_UnsignedType's `mov ecx, eax`).
  - `if (!(r = f())) { (void) 0; }`: a test whose branch generates nothing survives only with an expression statement
    as its body; `{}`, `;`, `&&` chains drop it (Main_Driver's `test eax, eax; jne` to the next `jmp`'s target).
  - A block-local copy of an array element read twice (`SInt32 r = used_virtual_registers[rclass]`), `(hi = field)
    == 0` for a field the original loads and tests in a register, an argument copied to a local before an inlined
    static call (`argobj = irec->var->object; if (ObjectIsArg(argobj))`), one walker variable reused where Pro7 has two,
    a field read into two locals rather than `b = a`: all register-only fixes found after decl_order stalled.
  - Two statements on a global pointer (`g->f = x; y->h = g;`) load `g` into eax; the comma expression `g->f = x,
    y->h = g;` gives 2.6's ecx (makepcblock).
  - `!(a < b)` where Pro7 has `a >= b` materialises the value (`xor r, r; cmp; jl; mov r, 1; test r`): is_copy, is_add.
  - Nested `if`s for one `&&` chain swap loop counters' stack slots (computelocalpropinfo).
  - An element of a global array (`reg_Defs[RegClass_GPR][r]`) compiles differently from a scalar at the same address
    (spill slots swapped): bind the array (`_reg_Defs`) rather than a fragment.
  - Byte-sized enums: an enum whose values fit a byte is a byte, and its enumerators are byte constants (`mov al, 6`,
    `mov cl, 3` in a ternary). A plain-literal ternary passed where 2.6 passes enumerators needs each arm cast
    `(UInt8)` (CLMessageReporter) or the parameter typed as the enum (CBrowse's EmitStandardData). A struct's byte
    field pushed as a raw dword (no `movzx`) means the callee's parameter is a byte type: CFunc_InsertAfterStatement
    takes `UInt8` (Pro7's StatementType), not `int`.
  - Varargs reporters take `SInt16` ids (Pro7's `CLReport(SInt16 errid, ...)`, the CLP* and CLPOSAlert family):
    declared `int`, callers' registers move. include/libc/stdarg.h is now MSL's x86 one; use `va_start`, not private
    macros computing `(char *)&parm + 2`.
  - A helper whose parameter is `char` where Pro7 has the byte-sized `RegClass` renumbers registers in every caller
    that inlines it (RegisterInfo's first_nonvolatile_reg).
  - Locals hoisted out of Pro7's block scope, then `fn.py decl-order --swaps` (convert_arrays_to_registers 82.7 -> exact);
    `SInt32` loop counters and scores where Pro7 has `int`.
- **What `-inline auto` inlines** (Pro7 CInline.c): without `inline`, a function is auto-inlinable only with at most 15
  counted statements (`CInline_IsSmallFunction`; an if/else is IFNGOTO + GOTO besides its bodies, labels do not count);
  inlined at nesting level 1 only with at most 10. When 2.6 inlines a static ours keeps out of line, count its
  statements (StrengthReduction's addinductionvariable was one over: a comma joins two stores). `inline` is not the
  fix: an `inline` function's out-of-line copy is emitted right after its first user, out of source order. Block-scoped
  declarations keep a function from being inlined (and so decide how far a recursive caller unrolls).
- **A row 30-40% similar while the rest of its unit is exact is a wrong body**, not a shape problem: another function
  under the row's name (Switch's dumpswitchtables was a cached-dispatch twin of get_dispatch_result; the real body sat
  in InstrSelection as an unrowed copy the caller used), a body missing a whole Pro7 branch (Unmangle_FuncArgList,
  CExcept_PatchConstructorAction), or a 2.6 addition (check_temp_registers' error-219 loop). Read the original first.
- **Swapped row names**: CompilerTools' GetHashNameNode and GetHashNameNodeExport rows held each other's Pro7 bodies
  (the `id = -1` one is Export: Pro7's CTool_GetPathName calls it). Check a caller before trusting a pair's names.
- **C++ units built `-inline off`**: a struct copy compiled as a call to `??4X@@QAE...` (implicit `operator=`) where
  ours is `rep movsd` means a `.cpp` source with `-inline off` (CWPluginsPrivate.cpp, like CLParserCallbacks_v1.cpp).
- **Import thunks**: Win32 functions the original calls through a `jmp [__imp__X]` thunk in .text (version.dll's
  GetFileVersionInfo*) need a binding `_X@N` at the thunk, or the caller shows "100.0 ... unbound".
- **Literals**: read a string's bytes in the original before trusting it: GetToolVersionInfo's
  `"\\StringFileInfo\\040904B0\\CompanyName"` had been written with single backslashes (`\S`, an octal `\040`),
  which compiled without complaint.
- **Statics' layout gives the declaration order** (reverse allocation): IroVars' three statics are Pro7's order;
  Propagation's are Pro7's with 2.6's `cp_mask` after `moreaggressiveoptimization`; CLMain's `secret` and the
  Update*FromVersion `warned` flags are Pro7's function-local statics but 2.6's .bss needs them at file scope
  (`MainInitialized` lowest, then the three flags in function order, then `secret`). A unit with 2-byte-aligned
  statics may need `#pragma pack(2)` in effect at the end of the file: MWCC lays out .bss with the packing in force
  then, so `pack(push)...pop` around the declarations does nothing (CompilerTools' heaps).
- **An object's sections start 8-aligned**: a unit's .bss range starting at `...4` lacks a static below it (PCode's
  `depthfirstorder`, which came with Pro7's computedepthfirstordering from BE_elf.c).
- **One object, two sources, found by a static**: a function of one "unit" writing another's static means one object.
  Propagation.c was Pro7's CopyPropagation.c's first half (2.6 has the whole file, deferred, as CopyPropagation.c).
  Conversely one range can hold several objects: CompilerTools' range was CompilerTools.c, LinkErrorsPPC_EABI.c (Wii's
  module; gc1's LibImportMessages.c) and Pro7's COSToolsCLT.c, each with its own flags. LoadDeletion.c is Pro7's whole
  LoadDeletion.c (0x5770e0-0x5771f0, .data from 0x5d1ac0), not the tail of VectorArraysToRegs.c as once thought.
- **COMMONs the original lacks**: defining one (Arguments' `SEPOPTCHAR`, which 2.6 does not have) moves the COMMON tail and breaks every unit's relink at
  references to `setOldOptimizerOptions`. Keep such names `extern`, and look for 1.x code's COMMONs in a linking
  object (AddPropagation's leftovers defined `addPropagationChanged` and `add_propagation_entries`). A COMMON the
  original has, under its real name, can be defined where Pro7 defines it (October 16: CopyPropagation.c's eight,
  AddPropagation.c's `propagatedadds`, CTemplateTools.c's `ctempl_instdepth`, Switch.c's `switchtables` relink fine).
- **1.x leftovers in a linking unit** (functions no row has, still called by unrewritten units) add .text, COMMONs and
  jump tables: AddPropagation's and CopyPropagation's now sit at the end of GlobalOptimizer.c (their only callers are
  it and ValueNumbering's 1.x code) until GlobalOptimizer is Pro7's; RegisterInfo's 1.x Registers code is at its top.
- **layout.py and inexact functions' jump tables**: a unit's data extent now ends at the last section its exact
  functions anchor; a switch table of an inexact function (sized by its wrong cases) had covered the next unit's .data
  (CodeMotion over StrengthReduction's assert file name).
- **Literal order in deferred builds**: a deferred build creates a function's literal section when it generates
  that function, and mwld lays out an object's .data sections in index order, so a dead function's literal before a
  live one's jump table says where the dead function was defined.
- **A switch's jump table gives its cases' source order.** A function "100.0% similar; references only inferred:
  @N" (its table "unverified") whose code matches has its case blocks in another order: the table's targets, in case
  order, are the blocks' offsets, so the source lists the cases in the order of those offsets (assign_spill_locations
  is Pro7's GPR, CRFIELD, FPR, VR, not numeric).
- **The back end's types are Pro7's (October 2026).** `PCode`, `PCodeArg`, `PCodeBlock`, `PCodeLabel`, `PCLink` with
  Pro7's member names (`nextPCode`, `args`, `data.reg.reg`, `data.imm.value`, `firstPCode`, `blockIndex` ...), the
  `PCOp_*` kinds (a register is `PCOp_REGISTER` with its class in `arg`: 2.6's vformatpcode does so; the repo's
  `PCOp_FPR`/`PCOp_VR`/`PCOp_CRFIELD` kinds are 1.x guesses kept only for unrewritten code), the `f*` instruction and
  block flags, `Effect*`, `PC_OP_IS_*`, `PCODE_FLAG_SET_F`. `tools/rename.py --member RECORD.OLD=NEW...` renamed them at
  every use (clang's "no member named" diagnostics, macro bodies included); use it for any struct member rename.
  Layout-neutral renames still moved code in places: Pro7's `imm.value` is `SInt32` where some 2.6 functions read
  through a local (Pro7's unused `value30` in initializeinductionvariable is the one 2.6 uses), and two Peephole
  functions needed a helper Pro7 lacks (marked TODO). The opcode table is Pro7's `OpcodeInfo opcodeinfo[]`
  (PCodeInfo.h), 18 bytes an entry: `#pragma pack(2)`, or every index scales by 20.
- **The register allocator is Pro7's (October 2026)**: LiveVariables, InterferenceGraph, SpillCode and Coloring,
  deferred; the 1.x allocator (Coloring_AllocateRegisters, SpillCode_*, gInterferenceGraph ...) is gone, and its
  callers (CodeGen_Generator, FuncLevelAsmPPC, TOC, InlineAsmPPC; all inexact, so lifted) call Pro7's functions.
  2.6 adds to it: the global register variables (`reg_info_list`, Wii's COMMON; computelivevariables and Peephole's
  computeliveregisters keep them live at the exit), small-struct and soft-float returns in r3/r4, and Gekko paired
  singles (`stvec2x32float`, the TYPEFLOAT of integral 0x13; operands with effect bits 0x4000/0x8000, nodes flagged
  0x800/0x1000, spilled with psq_l/psq_st through a second temporary).
- **CodeGen.c is Pro7's file, deferred (October 2026)**, 37/43 exact (was 22): the repo's CodeGen_Generator and its
  statement helpers were 1.x code; the original is Pro7's with EABI changes, read from IDA's pseudocode (soft float in
  GPRs, paired singles in FPRs on Gekko, small structs returned in r3/r4, `#pragma interrupt`/`__declspec(interrupt)`,
  `prepare_compress`/`bc_range`/`gprfloatcopy`/`gen_isel`, `__TARGET_CPU__`, the EABI profiler). 2.6 marks a temporary
  in use with `VarInfoFlag20` (Pro7: its uid) and gives it its stack slot in maketemporary. Left: CollapseVectorExpression
  (51%: stack-slot layout), UpdateObject, ParsePragma, parse_section_pragma (registers), move_assigned_argument (one
  register), pragma_scheduling (literal order only). Shapes that mattered: `{ SInt32 reg = opnd.reg; if (reg != 3) ... }`
  where Pro7 compares `opnd.reg` (movsx then a 32-bit compare), a local for the asm statement, `epilogue->flags` not
  `pclastblock->flags`, `Statement.value` UInt16 (Pro7's; `movzx`), `copts.report_heap_info` a signed char.
- **COpts has Pro7's member names (October 2026)**, mapped by offset from 2.6's `compileroptions` table (each pragma's
  OPT_OFFSET) against Pro7's table and struct order; EABI-only members take their pragma's name (`cats`, `pool_data`,
  `incompatible_sfpe_double_params`, `gekko_float_typecons`, `bc_range` ...), invented ones are TODO-marked (`soft_float`
  for -fp soft, `use_fp`, `sdata_threshold`). The CInline backend-options mirror has the same names. Statement, Operand,
  ObjectList, MWVector128 and CodeLabelList are Pro7's shapes too (`stmt->expr`, `opnd.optype`, `list->object`, `vec->uc`).
- **`OPT_OFFSET` must be `&((COpts *) 0)->x`**: Pro7's `(char *) &copts.x - (char *) &copts` is not folded by MWCC x86
  (two relocated addresses subtracted at run time).
- **An undefined macro compiles**: MWCC takes `IS_VEC2X32(t)` with no definition as a call to an implicit function, so
  moving code away from a file-local macro silently breaks it. Run `tools/clang_check.py FILE` after moving code.
- **Renaming a name a struct member shares**: `rename.py` leaves members alone (after `.`/`->`, or declared in a struct
  or union body) and lists them; before October 2026 renaming the global `switch_tables` also renamed
  `copts.switch_tables`. Locals and parameters of the same name are renamed with it.
- **Wii's data publics name COMMONs**: the Wii data publics (`tools/mw/wii.py`) gave CodeGen's `rt_sfpe_*`,
  `returnlabel`, `cleanreturnlabel`, `precomputedoperands`, `codelabellist`, `toclist`, `exceptionlist`,
  `has_catch_blocks`, `needs_cleanup`, `Xecth_func`, `tkidentifier`, `tkintconst`, `tkstring`, `cscope_currentfunc`.
  `requires_frame` and `makes_call` had been swapped (Pro7 sets makes_call first when profiling).
- **Two bindings at one address are a bug**, not an alias: lifted code takes either name, and when compiled code
  defines both, the equivalent compiler has two variables (or, bound together, one memory shared by two lists: the 1.x
  `toc_references` lay on 2.6's `anchorlist`). 2.6 had 40 such addresses (October 2026). `tools/bindings.py refs VERSION
  ADDR` lists the original's functions referencing an address, `refs --wii NAME` a Wii public's, `refs --wii --function
  F` the publics a Wii function reads: matching the two sets named `ok_for_global_registers`, `ok_to_compress`,
  `previous_ps_ID` (ELF_Setup's pseudo-section id, which CodeGen_UpdateObject's interrupt records count down too),
  `anchorlist` (with `is_pooled_object`, `used_anchor`), `optimizedloops`, `removedcommonsubexpressions`,
  `nextvaluenumber`. The other name was usually a 1.x variable whose binding had been guessed (`gAddPropagationChanged`,
  `maximumAlignment` (Pro7's `cmach_structalign`), FunctionCalls' `data_005884ff` (a Pro7 local), `get_type_align`);
  a 1.x global left with no binding stays a variable of its own until its unit is rewritten. A stand-in may hide a
  real reference: IroTransform's `static const cvt[32]` was `ComplementaryOp` (DoTransformations21 lost its match
  once the bogus `_cvt` binding went), Operands' `setupaddressing` clears `anchorlist`, and literals bound as globals
  (CLFileOps' six `double`s at one address were Pro7's `/ 1000.0`). Other versions' bindings were sometimes the
  2.6 guess moved (CExpr2's `right_operand_arg`/`operator_operand_arg`): symbols.txt had them right.
- **StackFrame.c is Pro7's file with 2.6's EABI frame, deferred (October 2026)**, 26/41. Its .bss is Pro7's statics
  in reverse declaration order at 2-byte packing, with 2.6's `#pragma interrupt` save-area statics between
  `non_volatile_save_offset` and `VRSAVE_save_offset`; that layout named the old `data_*` fragments. The
  `used_nonvolatile_registers[]` elements had been separate COMMONs (`gFPRSaveSpan`, `gVRSaveSpan`, ...). Inlining is
  the lever here: `local_offset_32` has Pro7's `localsbase`/`parametersbase` written out (as calls it is small enough
  to be inlined into `local_offset_lo` and the others, which 2.6 calls); `need_link_register` needs `inline` (auto-inline
  never picks it); `use_helper_function` needs `#pragma dont_inline` (TODO). An inlined `update_*_align(x)` and the
  same comparison written out compile differently (`set_out_param_displ` matched only written out).
- **COMMON hash order checks a name.** mwld lays out 4-byte COMMONs by `split.mwld_hash`, so among neighbours with real
  names the hashes increase: `__argc` (3197) and `__argv` (3176) fit between `nextvaluenumber` (3117) and `pclastblock`
  (3205) where Wii's later MSL names `argc`/`argv` (621/632) do not. Check a new COMMON name against its neighbours.
- **The compiler's parameter block is Pro7's `CompilerLinkerParamBlk`** (`cparamblkptr`, `cparams`: Wii's names),
  2-byte packed with the SDK's `CWObjectData` (0x30 bytes) and `CWBrowseOptions`; the repo's `CPrepCU`, the byte
  offsets (`cprep_cu[0xe0]` is `precompile`, `[0xe2]` `preprocess`, `+ 0x92` `sourcefile`, `[0xe6..0xeb]` the browse
  options) and stand-ins (`PragmaSettings`, `DispatchObject_0041b830`) were it. ELF_Lib_Import's `LibImportCU` is a
  naturally aligned copy (its browse options at 0xea), kept as it is.

- **Literal allocation sizes hide overruns.** `galloc(10)` for a 0x16-byte `ObjGenSymbolLink` (BE_elf), `galloc(26)` for
  a 30-byte `ENode` (CTemplateNew), `galloc(38)`/`galloc(54)` for `DwarfNode`/`Object`, `lalloc(0x2e)` for a 0x30-byte
  `ArgumentContext` (FunctionCalls): each overran the heap in the compiled compiler while the lifted original was fine.
  Write `sizeof(T)` and check the `push` against the original's. A literal smaller than `sizeof(T)` where the original
  pushes the same literal means the repo's type is too big (CScope's `ObjType` 6, CSOM's `SOMInfo` 22, DWARF's
  `QualNode` 6, Exceptions' `ObjGenRelocationRequest` 16). Clang sizes byte-sized enums as 4 bytes, so measure such
  structs (`Operand`) by MWCC's rule, not clang's.
- **PPCError.c is Pro7's (October 2026)**, deferred, with the message ids as Pro7's `PPCErrorStrNNN` enum (2.6's 120
  messages, `PPCErrorStrMAX` 220) and callers using the names. The globals it read were CParser's: `trychain`
  (`ParserTryBlock`, the repo's `CParseSave` with `g240`.. members), `errorreturn`, `in_assembler`; the try block's
  members named four more (`cscope_current`, `cscope_currentclass`, `cerror_locktoken`, `cscope_is_member_func`), and
  CExcept_Setup's statements CException's (`cexcept_dobjstack`, `cexcept_uniqueobjs`, `cexcept_hasdobjects`,
  `cexcept_magic`). Each was checked against COMMON hash order (Wii's `cprep_in_assembler` does not fit; Pro7's
  `in_assembler` does). `DeclBlock` (Pro7's, CFunc.h) was PPCError.h's `SavedGlobalValues`. ErrorTerm checks the id as
  an int where Error and Warning inline the short compare: a second VA helper (TODO).
- **FuncLevelAsmPPC.c is Pro7's (October 2026)**, deferred, 4/4 and linking: 2.6 adds `set_vr_reg_flags` before
  coloring, `switchtables = NULL`, the section index (`sm_section = update_section(func)`, stored in
  `Object.section`, the repo's `extraQualifiers`), an `rfi` for interrupt functions, restoring `copts.peephole`, and the
  soft-float argument pair. Names from IDA plus hash order: `fralloc_parameter_area_size`, `user_responsible_for_frame`,
  `assembledinstructions`, `asm_alloc_flags`, `cprep_eoltokens`, `curstmt`, `fatalerrors`, `cparser_fileoffset`,
  `InlineAsm_assemblererror`, `entrypoints_head`/`_tail`. InlineAsmPPC had written the fralloc size and the section to
  unbound copies (`data_005871a0`, `function_header_index`), so the equivalent compiler lost them. Shapes:
  `(int) reg < 10`, `(int) ia->opcode == 1`, `(epilogue = pclastblock)->flags |= ...`, `!(a || b)`, and a loop variable
  of its own for the statement walk.
- **The front end is Pro7's files, deferred (October 2026)**: CPrep, CPrepTokenizer, CPreprocess, CParser, CDecl, CInit,
  CExpr, CExpr2, CExprConvMatch, CClass, CABI, CMangler, CRTTI, CScope, CTemplateFunc, CTemplateTools, CError, CFunc,
  CInline, CPrec and CSOM all run in reverse of Pro7's order in the binary and are built `-inline auto,deferred`
  (CTemplateNew, CTemplateClass and CObjC are not yet: deferred trials broke functions). Check a unit's address
  order before rewriting it; CError laid out in binary order fell from 38 to 14 exact. 2.6 = 3628 exact after it
  (from 3447). CScope, CSOM and CRTTI are whole; CRTTI's compiled .data has one jump table more than the original's,
  which keeps layout.py from placing it. Row names that were shifted by one, found by a caller's call target:
  CDecl's `CanAllocObject`/`CanCreateObject` (the old `CanCreateHandleMemberObject` is Pro7's inlined static),
  CExpr's `expression`/`s_expression` (0x53cda0 is `expression` with `s_expression` inlined), `integralpromote`/
  `CExpr_GetETEMPCopy`, the CClass covariant trio (`CClass_OverrideOVClassTree`, `DefineCovariantFuncs`,
  `DeclareCovariantFuncs`), `CPrec_FindAddrPatch`/`CPrec_AddressHashVal`. A row whose code matches but calls the wrong
  address is the sign of swapped neighbour names.
  **Correction (October 2026 audit):** "Pro7's files" means Pro7's order and names, not Pro7's text. By line overlap
  CPrec, CFunc, CInline, CInit, CError, CDecl, CParser and Unmangle were 28-47% gc1-only lines and 9-28% Pro7's; only
  CPrep, CPrepTokenizer and CPreprocess were real Pro7 rewrites. The exact bodies are fine; the misses left in those
  units mostly come from gc1 helpers standing in for Pro7's statics (CError went 38 -> 40 and links once its helpers
  were Pro7's; CFunc's `statement` 61 -> 75% on Pro7's CFunc_AppendStatement/newlabel/CFunc_NewDeclBlock). CPrec is
  still gc1 text with about 100 invented helper copies and needs a full rewrite. gc1 is still worth reading: where Pro7
  and 2.6 differ in locals (separate loop counters, a separate offset variable, block-local copies), 1.3 often has 2.6's
  (CClass_OptimizeBitFieldAccess, CABI_CopyConAssignCB, CTemplTool_InitDeduceInfo, each 79-90% to exact).
- **Front-end 2.6 shapes** (each turned near-misses exact):
  - a `short` compared with `movsx` then a 32-bit compare is `(int) tk == K` (CExpr, CScope, CDecl); making the `TK_`
    defines an enum instead lost 20 functions;
  - `p == NULL` where Pro7 has `!p` inside `||`, `&&` and ternaries; `x != NULL`/`x != 0` spelled out in an `&&` keeps
    MWCC from folding or reusing the value (CClass_CheckPathAccess, is_typeequal's size check);
  - `x->f |= F` and `x->f = x->f | F` compile differently, also inside an inlined helper;
  - `CError_ASSERT(n, v = f())` keeps an inlined result's copy-then-test; `left = conv.left; CError_ASSERT(n, left !=
    NULL);` (not Pro7's assignment inside the assert) fixed CExpr's EOR/EAND family, and adds statements, which changes
    what `-inline auto` inlines;
  - a value already in a local is compared through it (`classname == name`, not the global `tkidentifier` again);
  - `CPrec_AppendAlign(); next = cprec_offset;` where `next = CPrec_AppendAlign();` does not match; `memclrw(buf =
    lalloc(size), size)` pushes `size` first; chained assignments (`dofreeaheap = macrocheck = 0`);
  - splitting one `result` into a variable per returning branch moves registers a lot (ParseNameResultExpr 89.9 to
    98.2); a test of `p->f` then a reload is a local assigned in the condition (`(tenum = TYPE_ENUM(t))->nspace`);
  - a recursive function in a deferred unit inlines itself once (CPrep_ParseCond, whose nested `CInt64_IsZero` stays
    out of line); inline `strcmp` alternates edx and ecx with each copy in a function, so a missing case flips every
    later one; 2.6 inlines `rep stosd` for `memset` where Pro7's `memclrw` does not;
  - parameter types reach callers: `UInt32` qualifiers (CParser_GetCVTypeQualifiers, IsConst), `TypeStruct *` for
    `ismember`, the SDK's `short` error level in `CWReportMessage` (CError_ErrorMessage went inexact without it once
    CError was deferred), `AccessType` for `ObjEnumConst.access` (2.6 reloads it).
  - 2.6 adds `CodeGen_CheckExpr` after building nodes throughout CExpr, and the EABI reaches the front end: small
    structs returned in registers get a temporary copy, `call_ptmf` and argument promotion use
    `Type_RequiresMemoryReturn`/`CMach_PassResultInHiddenArg`, va_arg builtins have a soft-float branch.
- **Braces can change MWCC's code** (rarely: one body in 3165, October 2026). CodeMotion's `appendheadercopy` (1.x
  text) lost its match when its one-statement `for` got braces, while a `while` loop, Pro7's `scan;` test or Pro7's
  bitvector macros all matched braced: a function that only matches in one spelling of a block is fragile, like one
  that flips with unrelated declarations. Check every mass reformat with `verify.py`.
- **`size_t` is not `UInt32`** to MWCC (`unsigned int` against `unsigned long`): with `memcpy` declared, a `SInt32`
  or `UInt32` size is copied to a second variable for the call, where 2.6 pushes it directly; 2.6's
  CExpr_DeduceFromObjList declares its `size` as `size_t`. A call that only matched through an implicit declaration
  (no `<string.h>`) hid that.
- **Rename pitfalls**: a `#define x ((T *) x)` alias becomes self-referential after a data rename.
- **layout.py and partly-static units**: a unit whose exact functions anchor one compiled static (CPrep's
  function-local `dummyelement$406`) but keep the rest global for lifted code got a 24-byte .bss and its neighbour the
  rest (CompilerTools stopped linking). A placed unit's .bss now runs to the last address it owns, and an aligned gap of
  8 or more between two placed units goes to the first object between them (IroMalloc, CClass).
- **Parallel agents**: `isolation: worktree` branched from the bundle remote's `origin/master`, an old commit; give each
  agent a `git archive HEAD` copy (with build/compilers, lib, .venv linked and its own build/tools) instead,
  and merge back three-way against a pristine export, taking each conflicted file from the agent that owns it and
  reapplying the others' renames (derived from bindings and functions.json by address; chains in one pass).
  Copy merged files with a fresh mtime (`shutil.copy`, not `copy2`): an agent's older mtime left ninja thinking objects
  were current, and ~30 functions looked broken. symbols.txt is generated: after merging renames, rebuild, then run
  `tools/layout.py` (names come from the configuration; the compiled objects only place things).
- **Link-round lessons (October 2026, second agent round)**, each one got units linking:
  - Build flags are part of the shape. `-sym off` fixed a deferred unit whose functions all differed the same way in
    registers (VectorArraysToRegs 3 -> 8/9). `-inline off` is the sign of implicit `operator=` calls or out-of-line
    copies of `inline` helpers in a unit's range (CLIO, CLDropinCallbacks_V10, CLCompilerLinkerDropin_V10).
    `-O4,s` packs 6-byte import thunks (LicenseImports). Runtime C++ units need `-RTTI off` (NewHand, newop; their
    shared inline virtuals are COMDATs, so relink.py' "defines functions of other units" is noise there). A flag
    sweep settled TargetSetOptFlags (`-sym on`).
  - A static inlined at every call still gets an out-of-line copy in a deferred build, and its switch table stays in
    .data; `static inline` drops it (CRTTI_IsSameType: the "extra jump table").
  - Data-only objects link as `"data_units": {SOURCE: AFTER_UNIT}` in config.json: MSL's locale.c and float.c after
    Unmangle (2.6's MSL is lib/msl's, built with pro53 for 4-byte literal padding, float.c with `#pragma
    explicit_zero_data on`), throwsbad.cpp after NewHand, Pro7's OpcodeInfo.c after CDecl. Big tables (opcodeinfo,
    the mnemonics, the SPR/DCR tables) were generated from the binary with `#pragma pool_strings on` so the strings
    follow the table. `"data_head": {SOURCE: {".data": ADDR}}` gives a unit the data before what its object places
    (CodeGen's `saveheaperror`, at offset 0 of its .bss). MsDos once had one for 32 KB of unreferenced zeros before its
    literals; since round 4 MsDos.c defines a stand-in `unused_zeroes[0x8000]` (TODO) instead. Its owner is unknown:
    nothing points into it (no base relocation targets it), wctype.c ends right before it, and no Wii build has a
    32 KB contribution. A range that "matches" because of a stand-in proves nothing about who owns it.
  - Dead-code-only files (PCodeListing, CIRTransform): the literal order in .data, reverse function order under
    deferred, gives the file's function order; 2.6's PCodeListing object has no .bss (the 532 bytes after it are not
    its own).
  - Enum-typed struct fields are reloaded: `PCodeArg.arg` as `RegClass` and `.kind` as `PCOpKind` fixed L_S,
    branch_record_volatiles, findsuccessors and MR_CMPI at once.
  - `ptmstruct`/`catchinfostruct` are `TypeStruct`s defined in CParser.c; their fragment bindings (`_DAT_0058843e` ...)
    had made the equivalent compiler never set the fields. Fragments of one struct bound separately are a runtime bug.
  - Small shapes: `"\r\n"[strlen("\r\n") - 1]`; `Boolean ok; ok = f(); CError_ASSERT(n, ok);` where the assert's line
    sits below the call; `IsZero(buffer, size = obj->type->size)`; `if (!(scan = list)) break;` vs a separate statement
    decides spill slots; `if ((err = f(&h)), *p = h, err)` for a struct copy reloading the pointer; `!!x` for a
    materialised-then-negated return; a chained `x->f = len = size` with `len` of another integer type; an expression's
    operand order (`((cpu & M) & tab[i]) != (cpu & M)`, not a `mask` local) decides when the index is copied
    (InlineAsm_LookupRegisterPPC); a parameter declared as another pointer type and cast at each use keeps 2.6's
    "never in a register" (UCBLookUpUnit). A "100% similar, other bytes" diff can be a wrong callee (CInit_GenericData
    calls `assignment_expression`). Deferred file order decides inlining: CError in Pro7's order went 14 -> 40.
  - Unit boundaries fixed this round: ScanFloat (was `CTemplateClass_ParseDouble`) is its own object
    ScanFloatX86.c; `Select_Gecko_Load_Store` is PCodeUtilities'; `assemblefunction`/`insert_align_nops` are
    PCodeAssembly's; `load_immediate`/`insert_load_immediate_after` are Operands'; `Bv_IsEmpty` is IroBitVect's; the
    three row-less functions at 0x57d150 in SpillCode's range are BitVectors.c's last (decompiled, kept aside:
    BitVectors would have to go deferred); CLFileOps was C, not C++; the Args_StackSize row was two functions.
  - A row renamed to a name another unit already uses is refused by `rename.py` (it renamed Projects.c's own
    `GetFileInfo` too, once); `--force NEW` once the other is out of the way.
  - Iro: a Pro7 file another `#include`s must not be a source of its own (IroPointerAnalysisADTs.c claimed
    IroPointerAnalysis' functions and split the units; IroFlowgraph is 3 KB, not 26). layout.py's "one Matching
    claimant wins" now applies only when the row's own source is not a claimant (a shared inline such as
    CInt64_IsNegative split a run). `-sym off` alone took IROUseDef from 14 to 22/24. Pro7's dead functions belong in a
    deferred source: the inline copies they leave out of line sit among the live functions (IroPointerAnalysis' order
    and .data). `if ((x = f()) && ...)` where 2.6 copies a result to a register before the test. One code-neutral
    statement over the 15-statement auto-inline limit keeps a small static out of line (CheckConstant).

## Making an exact unit link: what has blocked units whose every function is exact

- **In the other versions, 2.6's per-version configuration (October 17, 2026).** A dozen units exact in 2.0, 2.5 and
  2.7 did not link only because their configuration lagged 2.6's: `data_at` and `data_units` (Memory, file_io,
  string_io, locale and float after Unmangle, throwsbad after NewHand, OpcodeInfo after CDecl; 2.5's and 2.7's
  addresses are 2.6's, 2.0's 0x1000 or so lower, found by the bytes around 2.6's), the small functions added to 2.6's
  `functions.json` by hand (Parameter's `Param_None`, `CompareParam_*`, ParserHelpers' `Opt_PrintVersion`, newop's
  `~bad_alloc`; copied where the bytes at the neighbours' offset are 2.6's), and a stale `flags` override of newop.cpp
  2.6 had dropped. With them every Matching source links in every version (2.0 216 sources, 2.5 216, 2.7 215). 2.0's
  CSOM statics were global only while its functions were lifted; PCodeListing's 2.0 data is Pro7's formatdataflowset
  and its `"%0.80s%s"` alias name.
- **"Globals (generated from the original's .bss/.data)" are rarely globals.** A non-`static` uninitialized global is
  COMMON; the original's object .bss holds only statics. Those variables were `static` (IroLinearForm,
  TargetPanels, IroSubable), function-local statics (CLOverlays' `days_in_month` is Pro7's `static int monthdays[12]`),
  another unit's statics (PPCError defined three of CFunc's), or Pro7's structs split into fragments
  (MachineSimulation*). The "Strings" ones are usually string literals at their use (`"overlay group"`,
  `"temporary "`, `""`, Help's whole text): MWCC emits a literal at its first use and pools equal ones in a unit.
- **Dead functions keep their data.** The linker drops a function nobody calls, but its literals and statics stay in
  the unit's .data/.bss; add Pro7's function back (with a comment saying so) when the original's data has them:
  ParserFace's `CWParser_GetTargetList`, `Help_Null`, CCompiler's `CWPlugin_GetHelpInfo`, ToolHelpers'
  `Opt_RedirectStream`, Projects' overlay and segment functions, ParserHelpers' `Opt_AddUnixLibraryFile` and
  `ParseNumber`. They may call functions the original lacks too (CWParserAddSegment): the linker drops them unresolved.
  COptimizer keeps the dead CSE's statics: only the offsets of statics the code uses, and the .bss size, have to match.
- **An object's .data starts 8-aligned.** A unit whose .data range in splits.txt is marked `align:4` has a wrong
  boundary: its first items are in the previous unit's range (CCompiler's `systemHandles = 1` and help info sat in
  Option.c's) or the range holds more than one unit. A new 8-aligned run of data inside a unit's range is a new object:
  that is how "Targets.c" turned out to be Pro7's ParserErrors.c, Arguments.c and Targets.c, "ParserHelpers-cc.c"
  the tail of Pro7's ToolHelpers.c, ParserHelpers-cc.c and ToolHelpers-cc.c, the old "ToolHelpers-cc.c"
  StdTargetWarningHelpers-cc.c, IO.c and Projects.c, and "cc-eabi-ppc-mw.c" Pro7's cc-mach-ppc.c and libimp-mach-ppc.c
  (now cc-eabi-ppc.c and libimp-eabi-ppc.c). Each unit's dead functions' literals (above) confirm the cut.
- **The Wii compilers' CodeView names the modules.** `tools/mw/wii.py` gives each public function's module: it confirmed the driver's units and their functions' Pro7 names (ToolHelpers, Projects, IO,
  StdTargetWarningHelpers-cc, ParserHelpers' `FindFileInPath`, `GetEnvVar`, `AddAccessPathList`), and named units the
  repo had wrong (its cc-eabi-ppc.c was Pro7's CCompiler.c, CLStaticPlugins.c is "cc-eppc-mw", libimp-eabi-ppc.c's
  importer is "ELF_Lib_Import", TargetPanels-eabi-ppc.c is Pro7's CodeGenOptPPC.c). Wii spells "eppc" where the repo
  keeps 2.x's "eabi-ppc". Its data publics give the data symbols too, COMMONs included (module ""): the
  driver's state is Pro7's `clState`, `parseopts`, `optsCmdLine`/`optsEnvir`/`optsCompiler`/`optsLinker`, `gTarg`,
  `mainProj`, `pTool`, `exit_plugin`, `failedCallback`, `symFullPath`, `pCLTExtras`, `PrefPanelsChangedCallback`.
  Each was a run of fragments (`data_005871c4` was `clState.cpu`): one struct, its start bound in every version, the
  fragments' bindings deleted and their uses made fields (as for the panels).
- **A unit's first functions filed under the previous unit** leave both inexact or unlinkable: CLMain's
  `CLT_dummymain` and `CLT_GetDropInFlags` were CLPlugins' last rows, CLPlugins' `GetToolVersionInfo` and
  `Plugin_GetToolVersionInfo` ResourceStrings' (which linked once they moved). The reference file's first functions,
  and the Wii module of each name, put the boundary back.
- **Static order.** Statics are allocated in reverse declaration order, function-local ones in their function's
  place; when the functions' order fixes the wrong order, the original declared them at file scope (cc-eabi-ppc's
  `target_name`, `pFrontEnd`, `using_license_manager`). Two arrays came out the other way round once (Arguments'
  response-file buffers): swap them. Unused statics count: COptimizer's are Pro7's with `setbytes` declared second
  and `objrefnode`/`objrefnodes` gone, which the two used statics' offsets show.
- **A local aggregate with an initializer** (`char opttool[64] = "";`) is copied from a zeroed template in the unit's
  .bss; a struct copy from a named global in its place is the wrong shape.
- **One name per function.** `#define fn_0040fbe1 fn_0040fec1` made the compiled callers reference the alias, which the
  splits then exported from no object: the relink failed with an undefined symbol. Two names at one .text address in
  symbols.txt mean callers use a name whose own definition is something else: usually a stale 1.x copy beside the
  row's exact body (CScope_FindName without 2.x's `tparams` walk, PCode_NewLabel, CMach_GetClassAlign), sometimes the
  row's body is the stale one (the row named BE_symbol_SetupObjectSymbol held a copy of `fn_004d0660`, where the
  original has DefineObjectSymbol), and a "stale copy" can be a different inlined function the callers' exact code needs
  (CScope's own callers inline Pro7's `CScope_FindQualName`, the old body). Compare both bodies against the address
  before deleting one, and keep the callers exact.
- A unit's functions in reverse address order mean `-inline auto,deferred` and Pro7's order (COptimizer, IroSubable):
  deferred inlining can then inline a function into callers that the original calls; a local copy of the parameter
  (COpt_ReturnCheck) or a helper's shape keeps it out.
- **Small workarounds are acceptable** when the natural source has been tried hard and the workaround changes no
  behaviour: a `volatile` parameter, a copy through an unused local, a literal padded with NULs. Mark each with a
  `TODO` saying what the original evidently did and why this stands in (ParserHelpers_ParsePathList's `volatile mode`
  and `(t = type)`, ThreadLocalData's `"\0\0\0"`). Not for fake structs, offset macros or aliases.
- **A dead function's data is the evidence of its shape.** IroDump's .data has the literals of Pro7's dead dump
  functions in reverse file order (deferred), but none of the names Pro7's `IRO_SpellType` spells basic types with, and
  "(" ")" where Pro7 has "freturns(": 2.6's dead function was written that way (a comment in the source says so).
  (Probably wrong: the data audit found IroDump's .data starting at 0x5a9838, with Pro7's `IRO_SpellType` literals
  and `"freturns("` across the old cut at 0x5a9a08, in InstrSelection's range; `docs/audit-data-2.6.md`.) Its
  dead functions' jump tables are dropped with them; `relink.py` leaves such tables, and the functions a unit
  defines that the original has nowhere, out of its comparison.
- **COMMON is laid out by name.** mwld orders COMMON symbols of one alignment by a hash of their name
  (`split.py`'s `mwld_hash`), not by the object defining them. The split keeps the original's last COMMON bytes with
  placeholder names in byte slots when their names would order otherwise; a compiled object defining one of them
  (IroDump's `IRO_Log`) then lands elsewhere. The real names fix it (`data_0054a2f8` was Pro7's
  `setOldOptimizerOptions`, `data_00588531` CPrep's `macrocheck`, `data_00587c70` MSL's `_HandPtr`): name the
  variables in the tail, and a COMMON a unit adds that the original lacks moves every reference to the tail (a
  relink differing only at references to `setOldOptimizerOptions` says so).
- **MSL units are the library's sources** (`lib/msl`, through `#include <file.c>` wrappers): a hand-written MSL unit or
  hand copies of library functions (alloc_g's `unlink_list_elem`, `fn_00404220`, `allocate_sub_block` were alloc.c's
  `unlink`, `link`, `allocate_from_var_pools`, compiled twice) give the object code and data the original's lacks. The
  library's dead functions keep their data in the unit: string_io's `mode`, file_io's TLS `temp_name` (tmpnam), and
  extras.c's literals and `_doserrno` in what looked like wchar_io's .data. extras.c needs `<windows.h>`:
  `include/msl/win32/windows.h` is WINDEF.H and WINBASE.H, as MSL's other Win32 sources include them.
- **Names found by matching a trivial body are guesses.** MSL defines `fgetc` and `getc`, `atexit` and `__atexit` with
  one body; the original keeps the one something calls (symbols.txt names a function by its row; `layout.py` warns
  when the compiled code there carries another name). A 3-byte `xor eax, eax; ret` was named `ttyname` (unistd.win32.c) where string_io's
  `__close_string` is, and the driver's `find_driver_lookup_entry` was MSL's `find_temp_info` (file_io.win32.c's
  first function, right before `__open_file`).
- **Data nothing places**: a unit's data no exact function references and too short for its bytes to place
  (`layout.py` needs 8) goes where `config.json`'s `"data_at": {SOURCE: {".data": "0x..."}}` says, if its bytes
  are there (string_io's and file_io's 4-byte statics).
- **One object, two files.** A function of one unit writing a variable inside another unit's .bss means they are one
  object: only COMMONs are reachable across objects. CObjC's object asserts with "CObjCModern.c": 2.6's CObjC.c
  `#include`s CObjCModern.c (not built on its own; its rows' source is CObjC.c). Assert file names in .data tell such
  includes apart from merged code.
- **A run of .bss "globals" at small offsets from one another is a static struct**: libimp's were gc1's
  `static struct LibImportCU libimp_cu` (CPrepCU's fields), which fixed its .bss size and its COMMONs at once. A static
  1.x initializes `= 0` can be in .bss in 2.x: the relocations decide.
- **Option tables and message tables are generated.** ParserGlue-eabi-ppc-cc.c's 39 KB of .data is Pro7's
  ParserGlue: the preference panels (`include/driver/pref_structs.h`: Pro7's types where 2.6 has them, the EABI ones
  named after the options that set them), then the option tables `#include`d from `src/driver/Opts-eabi-ppc-cc/*.opt`,
  then `prefs`, `optLists`, `stPrefPanels` and the `parser` (a ParserTool). `tools/gen_options.py VERSION ROOT` walks
  the tables from the ParserTool and writes the .opt files (`--report` lists what the walk explains; `--name` names a
  static symbols.txt lacks; `--string-table ADDR=NAME` writes a resource's STRnnnn, as cc-eabi-ppc.r). MWCC lays out
  .data in definition order, each literal right before the first definition that uses it, so generating the objects in
  address order reproduces the bytes; compare an object's .data with the original's while ignoring relocated bytes. The
  references other units made to fragments of the panels (`DAT_00537762`) are struct fields (`pCmdLine.state`), with
  each panel's start bound in every version; pointers into `parseopts` come out as its fields too (`GLOBALS` in the
  tool). The tables are 2.6's: other versions' differ.
- **The driver is Pro7's command line with the plugin SDK's types**, and 2.x's types differ from the SDK's in small
  ways the code shows: CLPlugins needed `DropInFlags.edit_language` and `CWTargetList`'s `cpus`/`oss` signed (2.6 keeps
  a signed value in a register where an unsigned one is compared in memory twice), `CWDROPINDRIVERTYPE` a plain
  `'cldr'` beside the other types' enumerators (a comparison with it does not reuse the value loaded for the others),
  `PCmdLineCompiler.browserEnabled` a `char` (`cmp byte` where a Boolean is `movzx`), `parseopts.helpFlags` an `int`,
  `CWPLUGIN_ENTRY` the SDK's `short __stdcall` (2.6's callers test `ax`). The driver's `OS_ASSERT(line, cond)` is
  `if ((cond) == 0)`: an `&&` condition is computed to a value first. Pro7's bodies matched where the repo's had been
  wrong: `Plugins_GetPluginList` copies the cached version string (not a packed number), `Plugins_Add` prints
  "<none>" for an untyped mapping. Stand-in structs for the SDK's types were everywhere (CLPlugins' `PluginDesc`,
  `FileMap`, `PlugAux`; CLFileOps' `ObjFlagsData` and even the compiler's `TypeClassTemplate` read at
  `CWObjectFlags` offsets; MacFileTypes' `OpcodeDescriptorTable`): look for the SDK's type before naming a field.
- An MSL object shipped without its source (ansifp_x86.obj) has its data in its symbol table: ansifp's tables, bound as
  externals at addresses in another unit's range, were its own statics.
- **Where the driver's types live (October 2026).** common.h no longer typedefs the driver's or Win32's types. The low
  layers define theirs in Pro7's `typedef struct X {...} X;` form: win32.h, oslib.h, Memory.h, Files.h, CWPlugins.h
  (the SDK; `CWPluginPrivateContext` is opaque there and defined in CWPluginsPrivate.h), DropInCompilerLinker.h (the
  SDK's CWTargetInfo, CWBrowseOptions and the CW* callbacks the compiler calls) and every header outside a cycle. The
  shell's mutually dependent units' headers get their typedefs from `driver/cmdline.h` (forward typedefs, like Pro7's
  cmdline.h; definitions stay in each unit's header), the option system's types are all in `option_system.h`
  (Option.h, Parameter.h and Help.h keep their functions), the parser's from `parser.h`. Keep the include graph
  acyclic: a header needing a type only through pointers includes the layer header, one embedding it includes its
  definition. The compiler's headers name driver types by struct tag (with a forward `struct X;`) rather than
  including driver headers (Pro7's `Handle`/`Ptr` are common.h's, as in Pro7); the ELF structs are the back end's (ELF_Endian.h).
- **Intrinsics.c is Pro7's file, `-inline auto,deferred` (October 2026).** Its rows run in reverse of Pro7's order
  (the last row, at the highest address, is Pro7's first function), which said deferred; the old source was 1.x bodies
  under shifted row names. Pro7's statics that 2.6 inlined stay `static`; its `typeTable`, `intrinsics[315]`
  (`cur_intrinsic_object` 4 bytes below), `opcode` and `char_ptr` are the original's statics (the function-local ones
  bound as `_opcode$1289`, `_char_ptr$1644`). 2.6's type tables were generated from the binary (typeTable's targets,
  Pro7's names, each entry size checked against its pointers and opcodes): 26 of Pro7's TypeTable22 tables are
  TypeTable21 in 2.6, `vector_all_lt` has no NULL entry (Pro7's neither: a failed type search crashes the original, reading 0x119), and a dead `vec_step` table sits after `vector_any_out`; the
  compiled .data is then 2.6's byte for byte. 2.6 differs from Pro7 in small ways: ids 311-314 (`__rtos_change_stack`,
  `__rtos_pop_stack`, `__va_setup`, `__builtin_va_info`, the last two through `va_info_intrinsic`, the file's last function: the old Unit_004c45b0.c, its name made up), an FP guard (error 131)
  on the floating-point intrinsics, `checkconstintarg` reporting argument 3 always, ids 52/54 generating their
  arguments in the other order. Shapes that mattered: `promotable_types_are_equal` written wordily enough that
  `-inline auto` keeps it out of line (Pro7's two returns get inlined into its callers); a case's own Operand as a
  small static helper (an inlined helper's locals sit elsewhere in the frame than the function's own); `rtype = f();
  if (rtype)` for Pro7's `if ((rtype = f()))`; early `return NULL` for Pro7's `rtype = NULL`; `count = 0; scan = args;
  while (scan) { scan = scan->next; count++; }` for Pro7's `for`. PCode.h's flags (`fIsConst` 0x40, `fIsVolatile` 0x80,
  `fSideEffects` 0x100) are Pro7's values since October 16 (Intrinsics uses the names). `load_bytereversed_intrinsic` and
  `alloca_intrinsic` declare `SInt32 reg` (Pro7: `int`; with `int` the first reloads `output` after the `output->kind`
  store and the second, inlined in `call_intrinsic_function`, lacks 2.6's register copy), and Intrinsic_009 calls
  `unary_operator` (the repo's `InstrSelection_EmitUnaryGPRInstruction` row is Pro7's `shift_left_immediate`). Left
  (28/29): `rlwimi_intrinsic`, whose 2.6 code reloads `output` before `output->reg = op1.reg` and ours does not. Tried
  without effect: integer types of every local, parameter and checkconstintarg's signature, casts on the stored
  value, unused locals anywhere, renames, checkconstintarg's position and shape. Its three inlined checkconstintarg
  copies suppress the reload (out of line, or with the checks removed, it appears), so 2.6's checks probably differ
  in a way that keeps the same code. With it the unit (46.5 KB) links. Renamed on the way to Pro7's names: `cgdispatch` (and InstrSelection.h's GEN_NODE/ENSURE_* macros),
  `setpcodeflags`, `PPCError_Error`/`Warning`/`ErrorTerm`, `get_alloca_alignment`, `allocate_dynamic_stack_space`,
  `CMach_AllocationAlignment` (`SInt32`), `unary_operator`, `cscope_root`, `__memcpy_object`, the AltiVec types
  `stvector*` (CMachine's; `pass_by_address_type` was `stvectorsignedlong`), CParser's `newp_fobj` ... `dela_fobj`.
- **The OS layer is Pro7's OSLib and MacEmul on Win32.** `include/driver/oslib.h` has Pro7's types at 2.6's sizes
  (`OSPathSpec {char s[260]}`, `OSNameSpec {char s[256]}`, `OSSpec {path; name}` = 0x204, an 8-byte `OSHandle {addr,
  used}`, `OSOpenedDir` with FindFirstFile's data, `OSFileHandle`), the Mac error enum and `OS_ASSERT`; each unit's header
  has its functions, `__stdcall` where Wii decorates them `@N` unless a `ret` says otherwise (OS_CanonPath, OS_MacToRef,
  GetResource and Resources' fork-ref functions are cdecl). MsDos (Posix.c's Win32 twin: every name is Wii's) returns
  Win32 error codes where Pro7 has errno values: write them with WINERROR.H's names (`ERROR_BUFFER_OVERFLOW` for
  ENAMETOOLONG, `ERROR_PATH_NOT_FOUND` for ENOENT, `ERROR_DIRECTORY` for ENOTDIR). The stand-ins were a `char *` per spec
  and `(OSSpec *)` casts at every caller; `.path.s` passed where a spec was meant is the sign. Wii's modules found
  units hidden in other ranges: Generic.c was the first 12 functions of "CLProj.c", FileHandles.c the last five of
  "TargetOptimizer-ppc-eabi.c", TextUtils.c (and MSL's `__assertion_failed`, which every assert calls) the start of "CLIO.c",
  LowMem.c two functions inside "CLFileOps.cpp" (whose first ten were CLParserCallbacks_v1.cpp, built `-inline off`: its
  copies call the implicit `OSPathSpec::operator=` and `FSSpec::operator=`, which only `-inline off` keeps out of line), ErrMgr.c in NewHand's range (filed under CError.c), and
  MsDos.c's last three were MacSpecs' first. Each unit's data agrees: dead Pro7 functions' literals (Generic's
  OS_CompactPaths `"%s%s"`, Memory's DebugStr `"%*.*s"`, TextUtils' NumToString `"%d"`, Resources'
  OS_MacDumpResourceFork) sit in its .data, so those functions come back. Small 2.6 shapes there: OS_OSErrorToMacError
  returns `int` (callers keep it without `movsx`), MacSpecs' FindOrAdd tests `(Boolean)(ch == 0)`, Arguments'
  `operation_record` + `data_0057e1c0` were one `OSFileHandle respfilehandle`. `STSbuf` is both Generic's COMMON and
  ParserHelpers' static (`#define`d to `ParserHelpers_STSbuf`: binding names are global). MSL's `__assertion_failed` is
  `src/msl/assert.c`, the console branch lib/msl's copy lacks.
- **`__stdcall` bindings**: before October 2026 `rename.py` missed `_OLD@N` bindings (`_fn_00411780@4` survived the row's
  rename to OS_Rmdir, and the versions without a row there lost the only caller's match); it renames every spelling now.
- **Static order decides a unit's .bss placement**: Generic's and MacSpecs' statics, declared in Pro7's order, came out
  reversed (statics are allocated in reverse declaration order), and layout.py put the units' .bss hundreds of bytes
  off. Where the original has Pro7's first static lowest, declare them in reverse.
- **layout.py and `x[i - 1]`**: MsDos' code reads `intbuf[len - 1]`, the address one byte before its .bss; the padding
  gap before a placed unit that holds such an unnamed referenced address is now that unit's (a named one, a bound
  variable the compiled object leaves COMMON like Resources' `resError`, stays with the unit before).

- **The back-end global optimizer is Pro7's (October 15, 2026)**, all deferred. `UseDefChains.h` is Pro7's
  (`UseOrDef` with `TinyValue v` of `PCOpKind`/`RegClass`, `RegUseOrDef`, `ObjectUseDef`, `UseDefInfo`, `Defs`/`Uses`,
  `reg_Defs`/`reg_Uses` arrays, `usedefinfo`, `objectusedefs`/`objectusedeflist`), with 2.6's additions: each
  `ObjectUseDef` counts direct and indirect uses and definitions; `UseDefInfo` starts with each block's use and def
  ranges, its gen sets are `BitVectorRange`s and its kill sets `CompressedBitVector`s (BitVectors.h, Wii's
  `bitvector_compress` family; the range bit helpers are inline there); Wii's `last_usedef_register` copies
  `used_virtual_registers`. The 1.x use-def model (CodeMotion's `CodeMotionEntry`, the per-class head fragments
  `code_motion_register_definition_heads` ..., `CM_REG_USES2` pointer arithmetic) is gone. Each Pro7 name was checked
  against COMMON hash order. ValueNumbering (13/14), ConstantPropagation (3/5, the rest 98%), FunctionCalls (3/5, the
  rest 100% and 92.5%) and UseDefChains (8/13) were rewritten from Pro7 plus IDA's 2.6 pseudocode. Findings:
  - IDA treats `CError_Internal` as noreturn and drops the code after it: 2.6's `analyze_arguments` keeps Pro7's
    aggregate branch after `CError_FATAL(398)`. It also drops arguments now and then (`has_varargs`): check pushes.
  - Row names can be another function's: 2.6 calls row 0x509730 for pointer loads, so it is `pointerload`; Pro7's
    `arithmeticop` is inlined. Read the caller before trusting a name.
  - ConstantPropagation's first function (`propagateconstants`) was LoopOptimization's last row: a deferred unit's
    first function in the binary is the last in its file. Its statics collide with Peephole's (`defininginstruction`),
    so they are `#define`d to `constprop_*` names (binding names are global). 2.6 adds an FPR table, skips volatile
    instructions, folds `frsp` of a single-precision value to `fmr`, turns `addi rD,rA,0` into `mr`.
  - ValueNumbering's `opvalue` has `OPCODE_MAX` (469) entries; 2.6 accepts a system-register operand only for GQR0
    (SPR 912); `simpleload` reuses a stored value through `makecopyforload` (a conversion when the sizes differ).
  - FunctionCalls is EABI: register pairs start at an odd GPR, f1-f8, soft-float doubles in GPR pairs, paired singles
    in FPRs on Gekko (`IS_TYPE_VEC2X32FLOAT`), small structs returned in r3/r4, `creqv`/`crxor 6` for varargs calls,
    `ObjGen_GetAddressing(...) == 12` calls through CTR. `ObjGen_GetAddressing` returns `int` (callers compare `eax`);
    `base_register` switches on it as a byte.
  - A static inlined into its callers whose out-of-line copy wants other registers (UseDefChains' `addobjectusedef`):
    the callers win when only one can.
  - `RegClass` as a loop counter keeps the loop where `char` unrolls it (UseDefChains' `last_usedef_register` copy).

- **2.0's UseDefChains (October 16, 2026)** is Pro7's full-vector form, read from 2.0's code: `UseDefInfo` is 32
  bytes (no use/def ranges; eight `UInt32*` sets), `allocateusedefinfo` allocates `usedefinfo` and the eight vectors
  (inlined into `computeusedefchains`: nine `oalloc`s), `precomputeusedefcounts` neither allocates nor records the
  ranges, `computelocalusedefinfo` skips branches (`!(pcode->flags & fIsBranch)`) and initializes all eight with
  `bitvectorinitialize`, and the two global passes update the sets in place word by word (2.5's `updateusedefset`
  and the range/compressed vectors are not in 2.0's BitVectors). Now under `#if VERSION < VERSION_GC_2_5`, which let
  2.0 link source-only; 2.0 `computeusedefchains` went from 49.8% to 94.6% and 2.0 gained 7 exact functions. Left: 2.0
  asks its alias analysis about two PCodes (the use and each definition: Pro7's `uniquely_aliases`/`may_alias`) where
  2.6 passes an object; 2.0's Alias.c rows are named 2.6's (`must_alias_object` 351 bytes, `may_alias_object` 426) but
  are Pro7's functions, and Alias.c is 2.6's in every version, so 2.0 compiles 2.6's calls (TODO in the source).
  The 2.0 rows `fn_0056e420` (computelocalusedefinfo), `fn_0056e310` (computeglobalusedefinfo), `fn_0056e200`
  (computeglobaldefuseinfo), `fn_0056edf0`, `fn_0056e8d0` and `fn_0056ef80` are unnamed.
- **2.0's compileroptions (October 16, 2026)**: 2.0's table has 2.6's 152 entries less `old_style_bitfields`, in the
  same order with the same flags; the offsets differ because 2.0's `CompilerLinkerOptions` does (from
  `asmsemicolcomment` on, by one to two bytes; `longlong` 0x92 for 0x94). CPrep.c now defines it in every version
  (the row under `#if VERSION >= VERSION_GC_2_5`), with OPT_OFFSET over the headers' struct; with the versioned
  `CompilerLinkerOptions` (below) the table's offsets are the original's.
- **2.0's front end (October 16, 2026)**: the 2.0 front-end/driver functions exact in 2.6 but not in 2.0 (316) came
  from few causes: CompilerLinkerOptions without `old_style_bitfields`/`unknown66` (2.0's compileroptions table gives
  every pragma's offset; CMachine's InitOffset/GetCurSize/GetOffset and CodeGen_InitBackEndOptions lose the field's
  tests and store); `Object.u.data.reginfo` before `linkname` in 2.0 (objects.h); 2.0's `GetLicense` always asks for
  license version "1" (no GetSystemDefaultLangID); LinkErrorsPPC_EABI's `format_string` unmangles into 256 bytes;
  `CPrec_GetStatementPatch` has no `sourcefilepath` patch; the unbound `cerror_synchdata`/`cerror_synchoffset`; and
  assert lines (CParser.c 48 and CPrec.c 51 lines lower in 2.0 past a point: `#if VERSION < VERSION_GC_2_5` around
  each site, since the literals are per version and update_assert_lines.py writes only 2.6's). 2.0: 3419 -> 3843
  exact, 1319 -> 2411 linked functions (140 -> 182 sources). The lifted 2.0 equivalent compiler now compiles test
  files identically to the original (it failed every corpus job before: the COpts layout).
- **2.0p1 (October 16, 2026)** is 2.0's executable with three bytes changed (the files are otherwise identical), in
  two functions, both about floating point:
  - TOC.c `createfloatconstant`: the literal's `qual` is `Q_INLINE_DATA` where 2.0 has `Q_CONST | Q_INLINE_DATA`
    (the store `mov dword ptr [esi + 0x12], 0x10001` became `0x10000`).
  - CodeMotion.c `maymove`: in a loop of more than 25 instructions, 2.0 keeps `vspltisb`/`vspltish`/`vspltisw` in the
    loop; 2.0p1 keeps the floating-point loads `lfs` through `lfdux` (0x8e-0x95) instead, and the splats move like
    anything else. The patch rewrote the constants of 2.0's compiled range test (`sub eax, 0xd8; cmp eax, 2` to `sub
    eax, 5; cmp eax, 7`); from source, MWCC compiles the contiguous `li`..`lfdux` cases to a jump table (95.9%), so
    2.0p1's `maymove` cannot match. Both are `#if VERSION == VERSION_GC_2_0P1`; the source-only 2.0p1 then compiles
    the scratch corpus identically to the original 2.0p1 (before, it gave 2.0's output, which differs on t1.c).
- **BFBB's real compiler (October 16, 2026; BFBB decomp at ../../GameCube/BFBB/bfbb)**: what 2.0p1's patch
  approximates. In BFBB's game code a float literal's load is merged with an earlier load of it only while no store
  to memory other than the stack intervenes. A store to a small-data global kills it (original: survives 4 such
  float stores, reloaded after 71; int stores 0/15), stores through other pointers mostly do (24%/28% survive),
  stack stores rarely (84-89% survive); ordinary globals are not reloaded that way. Stock 2.0 and 2.0p1 keep the
  literal in a register across all of them (80 survive / 3 reloads). That is the literal load having the
  worst-case alias (`worst_case_ptr`, which every store to a global or through a pointer kills) instead of its own
  object's. Experiment: in gather_instruction_alias, under 2.0p1, a load whose alias is a float `@` literal
  (`Q_INLINE_DATA`) gets `worst_case_ptr`. Built as the source-only 2.0p1 and dropped into BFBB (configure.py
  --compilers), BFBB's game code goes from 42.85% to 45.21% matched code (5364 to 5475 functions; the stock 2.0p1
  binary gives 43.78%: our 2.0 sources still differ in 21 of BFBB's 224 game objects, scheduling and frame layout);
  the functions that differ only in how often literals are loaded drop from 315 to 251, and those where the
  original reloads more from 17 to 1. 2.0p1's two patches add nothing on top (Q_CONST off: 0; maymove: +0.015%).
  Now the derived version GC_2_0p2 (2.0p1 with the hook, `#if VERSION == VERSION_GC_2_0P2` in Alias.c; 2.0p1's two
  patches kept): built source-only and dropped into BFBB, 45.21% / 5475 functions, as the experiment.
- **2.0's bitfield layout (October 16, 2026; found through BFBB)**: 2.0 has neither 2.6's two layouts. Its
  `CMach_StructLayoutBitfield` (602 bytes, one function) is Pro7's old layout (`align <= size` clears the
  alignment) with two additions: a first bitfield after other members, at an offset its type would pad, shares the
  unit those members end when its bits fit (`cmach_curbfbasesize = basesize - padding`, `cmach_curbfsize` that in
  bits); and a unit of a smaller type grows into the bitfield's larger type from where it starts
  (`cmach_curbfbasesize < basesize`: back up by the old unit's size), so `U8 a : 8; U32 b : 24;` is one 4-byte unit.
  2.0 compiled 2.6's new layout before (copts.old_style_bitfields is 0), which gave BFBB's `xtextbox::jot` and
  zTalkBox's and zTaskBox's structs other sizes: 116 functions BFBB matches with the real 2.0p1 failed with ours.
  Now `#if VERSION < VERSION_GC_2_5` in CMachine.c (57% to 84%: registers, copts' offset and the assert line left;
  GetOffset/GetCurSize/InitOffset keep 2.6's copts tests, which 2.0 lacks but which give 2.0's behaviour). With it
  our 2.0p1 differs from the real one in 14 of BFBB's 224 game objects (was 21) and matches 5470 functions to the
  real binary's 5474; GC_2_0p2 gives 46.17% matched code, 5581 functions (118 gained, 11 lost against the real
  2.0p1). BFBB is the best test of 2.0's behaviour we have: the 14 objects left then were 2.0's Alias.c (next
  entries; now 0). The store to `sCamTweakPitch[0]` that kills a literal is the direct-global-store rule of
  GC_2_0p2.
  Not a 1.3-era compiler either: GC 1.2.5n, 1.3, 1.3.2 and 1.3.2r keep the literal across the stores just as 2.0
  does, and BFBB's game code matches less with each (1.3.2: 32.8%, 4810 functions; 2.0: 34.3%, 5012; 2.0p1: 43.8%,
  5474); no function matches under 1.3.2 that does not under 2.0p1, 18 are closer.
- **2.0's Alias.c (October 16, 2026; found through BFBB)**: 2.0 compiled 2.6's alias analysis, which is why 14 of
  BFBB's objects still differed (all were loads and stores the real compiler kept in order). 2.0's is Pro7's model
  with some of 2.6's helpers, read from the binary, now `#if VERSION < VERSION_GC_2_5` in Alias.c: one worst case
  (no `worst_case_func`; calls get `worst_case_ptr`); `may_alias`, `uniquely_aliases` and `may_alias_object` on
  PCodes (Pro7's; `may_alias_alias` inlined in each; no `same_object`, no `must_alias_object`);
  `make_distinct_virtual_alias(obj, offset, size)` a one-member set of a fresh object (CodeGen's
  `move_assigned_argument` calls it only for a restrict pointer or a pointer to const); `gather_instruction_alias`
  Pro7's two-register rule (`alias_array`, a switch on the count, asserts 1250/1258); `propagate_alias_to_use`
  without 2.6's MR handling; `addresspropagatestouse` without `canidate_reaches_use`; a helper
  (`add_escaping_locals`, invented name) using `is_nonvolatile_register` (Pro7's, in RegisterInfo.c, gone in 2.5);
  `is_safe_const` treats a pointer to an array as the array. The callers follow: CodeMotion and
  `computelocalusedefinfo` ask `may_alias`/`uniquely_aliases` with `Defs[id].pcode`; UseDefChains' memory half is
  Pro7's (`fIsPtrOp` tests, a 0x18-byte ObjectUseDef without the direct/indirect counts, `addobjectusedef` not
  inlined); PCodeListing prints `{worst_case}`. 2.0's rows were misnamed: the ones called `must_alias_object` and
  `may_alias_object` are `may_alias_object` and `uniquely_aliases`; `fn_005123b0` held `gather_instruction_alias`
  and `finishpropagatealiases` (now split); the UseDefChains rows were shifted one function (fixed, with sources for
  the unbound rows). 14 Alias.c functions exact (update_alias_value differs only in assert lines, gather_alias_info
  in a copts offset). Also 2.0's IROUseDef: `UpdateUse` and `UpdateUseForOtherSelfAssignment` are Pro7's (no
  integer promotion), and the inc/dec propagation passes the definition's type (a pointer as `unsigned long`). With
  these the source-only 2.0p1 compiles all 224 BFBB game objects byte-identically to the real 2.0p1.
- **GC_2_0p2 (October 16, 2026)**: BFBB's retail compiler is a 2.0 no release matches (2.0p1's three bytes
  approximate it). Measured on bfbb-vibe (../../GameCube/BFBB/bfbb-vibe: a near-complete decomp whose game code is
  built with GC/2.0p1g, 2.0p1 plus about fifteen binary-patched alias "clauses" and grafts of 2.5 behaviour, fitted
  by measurement; its source is tuned to that compiler) and on the BFBB decomp. Functions at 100% on vibe (of
  7673): real 2.0p1 6994 (our source-only 2.0p1 the same); 2.0p1g 7579; the first GC_2_0p2 (literal rule only)
  7285; the current one 7559 (16 functions 2.0p1g misses, 36 it gets). On the BFBB decomp (source tuned to neither):
  2.0p1 5474, 2.0p1g 5724, GC_2_0p2 5719 (48.39% matched code to 2.0p1g's 48.38%). The rules, each a measured step
  (scratch variants; vibe's count after each):
  - literal loads alias `worst_case_ptr` (gather_instruction_alias): 7285;
  - `maymove` keeps 2.0's choice (2.0p1's patch dropped) +3; 2.5's const-pointee alias
    (`make_alias(pseudo, 0x7FFFFF, 1)`) +11; 2.5's rule for two known address registers +1: 7299 together;
  - the scheduler orders a load addressed by a global's symbol (not a pointer register, not an `@` object) like a
    load through a pointer (`may_alias_scheduling`): retail never hoists such a load above a store to another
    global, through a pointer, or to an escaping local: 7396 (all global loads: 6766; including the `@` templates:
    7382);
  - stores addressed by a global's symbol too, in the scheduler: 7493;
  - such a store's alias is `worst_case_ptr` (so value numbering forgets other globals and literals, and a store
    is not forwarded to a load of the same global: `return --g;` reloads): 7559. Without the literal rule: 7237.
  Tried and worse: every store to a global as worst case (7391), indexed stores to globals too (7549), all global
  loads in alias analysis (6771; value numbering then reloads everything), the literal rule in value numbering only
  (no change), the whole 2.5 back end with the literal rule (7234). What is left (36 on vibe): front-end CSE across
  an indexed store to a global array (`a[n] = x; n++` reloads `n` in retail), and one store/literal-load order
  (zEntPlayerSpeakStop). The rules are fitted to BFBB's code like 2.0p1g's clauses, but each is one statement
  about 2.0's alias analysis, built from the sources. Since October 17 GC_2_0p2 derives from GC_2_0, not 2.0p1 (a
  binary patch, not a release): 2.0p1's const-less literals are one of its rules (TOC.c), and 2.0p1's `maymove` patch
  was never in it; the compiler built is byte-identical to the one derived from 2.0p1.
- **The version round (October 17, 2026)**: every function exact in 2.6 was made exact in 2.0, 2.5 and 2.7 where
  the version's own code allowed (four agents, merged; what is left: docs/nearmiss-versions.md). Exact functions
  2.0 3419 -> 3980 (of 4041), 2.5 3961 -> 3995, 2.7 3928 -> 4002; linked sources 140 -> 199, 188 -> 203, 186 -> 199;
  2.6 unchanged. Every row is compared now but the three version.dll import thunks (as in 2.6). Our 2.0p1 still
  compiles all 224 BFBB objects identically. What each version differs in:
  - 2.0 front end: "2.0's front end" above (CompilerLinkerOptions, CInlineBackendOptions, Object's reginfo/linkname).
    With them 2.0's lifted equivalent compiler works (scratch 11/11, procs 41/41, ttyd 129/129 identical).
  - 2.0 back end and optimizer: IroPointerAnalysis's rows named and given their sources; 2.0's
    ObjectIsAnExtendedParamCandidate also tests `datatype == DABSOLUTE` (inlined everywhere: most of the unit's size
    differences); 2.0's EvalProcAction analyses an asm statement through CodeGen_GetAsmEffects where 2.5 aborts the
    analysis (IroPointerAnalysis.c now includes InlineAsm.h and InlineAsmPPC.h in every version: 2.6's `@N` labels
    renumber, no code changes); 2.0 has no TreeContains (IroLoop.c), and its Reducable looks the induction record up
    only for ESHR/EDIV, without the `addConst % val` check; `precedes` is CopyPropagation.c's in 2.0 (inlined into
    copypropagatestouse), UseDefChains.c's from 2.5; move_assigned_argument tests restrict and pointer-to-const
    through two inline helpers (invented names); 2.0 has no `last_usedef_register`.
  - 2.0 and 2.5 really convert constants to and from the paired-single custom types (CMach_CustomIntConvert); 2.6
    and 2.7 return 0.
  - 2.5: its TypeBitfield is 0x10 bytes (two chars after `suppressAlignment`; 2.0, 2.6 and 2.7 have 0xe); its
    bitfield layout is a third form (2.0's single function refined when `old_style_bitfields` is off, with a static
    `cmach_curbfintegral` that InitOffset resets).
  - 2.0, 2.5 and 2.7 build MSL and the runtime with pro53 as 2.6 does (`compiler_overrides`; pro5 had put `= 0`
    statics in .bss and padded literals otherwise). Rows cut one function off in all three: OS_Delete and
    OS_AppendHandle (their tails were rows), InstrSelection's gen_OR/XOR/AND/SHR/SHL (named one off).
  - 2.7: the object writer looks symbols up through a 4096-bucket hash table (BE_symbol.h: `hash[0x1000]` before
    `first`, a `hashNext` link; `_TheSymbolTable` moves), which alone fixed most of BE_symbol, BE_elf, ObjGen and
    CodeGen_UpdateObject; every DWARF entry and line table gets a symbol of its own (`.dwarf.%04x.NAME`,
    `.line.NAME`) and relocations refer to it: 2.6 has the feature's leftovers (a name test giving NULL either way,
    an unused static, 8 unreferenced bytes of .bss). BE_elf: debug relocations always kind 24, no assert in
    BackPatchRelocationAddend, CodeWarrior version record major 11. 2.7's "data pool" rows are 2.6's functions,
    smaller because MakeSymbolTableEntry is no longer inlined.
  - Assert lines: an assert passes its 2.6 line plus `CError_LINE_SHIFT` (CError.h; IroPointerAnalysis's IRO_ASSERT
    macros too); where another release's file had other lines above a point, a block redefines the shift for it
    (22 blocks; `tools/update_assert_lines.py VERSION` lists the points, by the line each site passes now).
    InlineAsmPPC.c's -3 is shared by 2.0 and 2.5 (`VERSION < VERSION_GC_2_6`).
  - Tooling: the kit's merge_agent.py merged new JSON keys in set order, which scrambled `compiler_overrides`; a
    full `tools/relink.py` then linked 5 sources fewer (Targets, libimp-eabi-ppc, abort_exit, misc_io, setupargs)
    than with the agent's order, although each linked alone (`--only`). Not chased: a relink's result should not
    depend on the order of that table.
- **MachineSimulationAltiVec is Pro7's file in 2.6's shape (October 15, 2026)**, deferred, 6/7, its data byte-identical
  (only `advance_clock` keeps the unit from linking: 2.6 enters its final-stage and pipeline-unit loops with a jump to
  the test, which no spelling, counter type or flag tried reproduced). Its .data starts at 0x5d1b20 (the cut had given
  VectorArraysToRegs 4.4 KB of it): the stage names (2.6's: BPU, CFX..CFX3, SFX1..3, LSU..LSU4, FPU..FPU4, VECTOR,
  V_PERM.., V_CFX.., V_FPU..; the same 26 stages as Pro7's IU2_1.., IU1a..) as literals and an unused pointer table,
  then `machine7450`, `finalstages[12]`, `pipeline_units[6]`, and a 404-entry `instruction_timing` ending in Pro7's -1
  entry. 2.6 adds a flag set when an FPU instruction completes (issue of a fourth FPU instruction waits on it), an
  FPU3->FPU4 cycle adjustment, pipelines advanced from their last stage back, and fetch queues starting at 4,3,2,3,2.
  The loop counting busy FPU stages uses a `Stage` counter (an enum keeps the loop's entry test, as `RegClass` does).

- **Exceptions.c and DumpIR.c are Pro7's (October 15, 2026)**, deferred, both linking. 2.6's exception records are
  naturally aligned (Pro7's mac68k: relocation offsets +8 where Pro7 has +6); a two-register flag byte is
  `((reg ? 1 : 0) << 7) | (((reg2 ? 1 : 0) & 1) << 6)`; `dumpexceptiontables` pads each action chain to 4 bytes, ORs
  `| 4` (or `| 2` for ELF/Gekko with vectors) into the flag word before byte-swapping it. DumpIR has Pro7's dead dump
  functions back for their literals (0x5bc698..0x5bcfc8, placed by `data_at` with `.data_end`: dead literals no exact
  function references stay in the previous unit's range otherwise); COptimizer's .data starts with Pro7's `bitmasks`.
  An unreferenced initialized static is emitted at parse time, before a deferred unit's function literals.

- **PCodeAssembly.c is Pro7's (October 15, 2026)**, deferred, 4/7 (the rest 92-99%, registers). 2.6 writes ELF
  relocations (2 for b/bl, 8 for 16-bit branches, 10 with `bits |= 2` for an absolute memory branch);
  `insertlongbranches(max, min)` takes `(copts.bc_range >> 1) - 1, -(copts.bc_range >> 1)`; ISEL and the Gekko
  opcodes (psq_l/st with the GQR as `reg - 912`, dcbz_l, ps_*) are assembled; `assemblefunction` adds
  ObjGen_BestSection, ObjGen_SymFunc, the DWARF function entry, MWCATS hooks and ObjGen_DeclareSymInfo. Shapes:
  `bits |= (UInt16) x` for Pro7's `x & 0xFFFF` (movzx, not and); one function-level `flag`/`value` instead of
  per-case block locals in a big switch. `functionbodyoffset` (Pro7's, defined in CFunc.c) checked by hash order.
  `update_assert_lines.py --write` rewrites other units' sources too: run it with care while others are in flight.

- **Operands.c is Pro7's (October 15, 2026)**, deferred, 26/29 (setupaddressing was TOC's: moved). 2.6's doubles
  (`one_point_zero`, `int_to_float_cc`, `uns_to_float_cc`) are big-endian word pairs in .data that ObjGen_Setup swaps
  at startup (`FloatStorage` lists `words` first so initializers fill them); MWCC x86 does not 8-align doubles in .data.
  `Operand.immOffset`/`.immediate` are `SInt32` (with `int`, combine reuses a sign-extended value). A dead Pro7 branch
  (`SInt32 offset = 0; if (offset) {...}`) is what keeps load_floating_constant out of line. Left: Coerce_to_register
  (2.6 keeps `op` on the stack), the I8 bitfield pair.
- **CodeMotion.c (18/19) and LoopDetection.c (17/17, links) are Pro7's (October 15, 2026)**, deferred. 2.6's use-def
  queries are `may_alias_object(pcode, Defs[id].v.u.object)` / `must_alias_object` where Pro7 has `may_alias` /
  `uniquely_aliases` on two pcodes, and take `TinyValue* v = &def->v;` right after `def = &Defs[defID]` (the sign: a
  `lea r, [def + 4]` in the prologue; five functions turned exact on it). `isloopinvariant` treats r13 like r2,
  compared as `(int) reg == 2`. `bitvectorinitialize` takes an `SInt32` count. A variant of one function in a
  deferred unit can break another (`(loop->step = x) == 0` broke insertpreheaderblock): check the whole unit.

- **Alias.c (18/21) and StructMoves.c (9/12) are Pro7's (October 15, 2026)**, deferred, statics now `static`. The
  row once named `may_alias_worst_case` is Pro7's two-pcode `may_alias`; 2.6 lacks `uniquely_aliases`. `make_alias`
  matches only with a `goto found;` search loop followed by `alias = NULL;`. StructMoves' step computations copy
  their arguments like inline helpers' parameters (an inline GetStep is plausible).

- **CMachine.c is Pro7's (October 15, 2026)**, deferred, 45/48, with Pro7's type objects (`stbool`.., `stvector*`,
  2.6's `stcustomtype113/114`, `stvec2x32float`; `stwchar` is 2 bytes) defined there from the original's bytes and
  Pro7's `cmach_*` statics (`#pragma pack(2)` at the end). Uses of `&stvectorX` are `TYPE(&stvectorX)`. 2.6's bitfield
  test is `align < size` (Pro7 `<=`); `GetPPCTypeAlign` stays out of line only with `#pragma dont_inline` (TODO);
  `int` vs `SInt32` locals decided three functions. A dead function's jump table in a deferred unit shifts later
  tables so layout.py places none of the data: write dead functions without tables. Left: ParseGlobalRegisterVariable,
  StructLayoutPackedBitfield (registers), AllocationAlignment; check the CMachine/CInt64 .data cut before linking.

- **TOC.c is Pro7's (October 15, 2026)**, deferred, 22/30, with setupaddressing (from Operands) and Pro7's pools
  (`floatconstpool`, `floatconstlist`, `descriptorlist`, `vectorconstlist`/`pool`, `doubleconstpool`, `toc0`,
  `no_descriptors`, `lvslBytes`/`lvsrBytes`: the old fragments, each checked by hash order). 1.x's `toc_references`
  is `anchorlist` and `TOC_HasObjectReferenceWithoutExpression` is `used_anchor`. Rows renamed by their bodies:
  `countindirects` was `canoptimizevectorconst`, the old `canoptimizevectorconst` is 2.6's `swap_vector_elements`
  (invented), `rewritefunctioncallreturningstruct` was `rewritestrcpy`. A deferred build emits every static out of
  line with its jump tables, so Pro7's `countindirects` and `OptimizeNestedAssginments` (no tables in 2.6's .data)
  are not in 2.6's file and `invert_relop` is `static inline`. Intrinsic ids compare as 16-bit enums
  (`(Intrinsics) id == Intrinsic_008`); `(SInt16)`/`(RegClass)` casts on EncodedOperand fields give 2.6's movsx and
  byte compares; GetSizeSkip is inlined into Optimize64bitMath in another shape (a second helper, TODO). Pro7's
  lvslBytes has a typo (row 12 `0x00`; 2.6 `0x10`). `add_anchor_reference` lost its match on going deferred (2.6
  keeps `obj` in ebx and spills the lists); left with rewrite_opassign and canoptimizevectorconst (registers).
  rewrite_opassign (60.7, equivalent: checked path by path against IDA/Ghidra/Binja) is one spill choice: 2.6 spills
  the `expr` parameter and keeps `left_sub` in ebx. No source tried flips it (declaration order and scope, split or
  case-scoped `tmp`, dead locals, a named copy of `expr`, gc1's whole body, inline promotion, `-sym off`); it takes
  eight extra uses of `left_sub`. `promote_type` is `static inline` (2.6 has no out-of-line copy); our object still
  emits out-of-line `estimate_func_param_size`, `needdescriptor`, `magnitude`, `rewritefunctioncallreturningstruct`
  and `disables_optimizer`, which 2.6 lacks.
- **Operands' Coerce_to_register (97.4)**: 2.6 spills `op` and keeps `output_reg` in a register; Pro7's named copy
  `tmp = reg` in the CRField case was what kept `op` in a register under `-sym on` (`-sym off` showed the original's
  8-byte frame). Left: `opcode`'s register and `bit_offset`'s 16-bit add. **`-sym off` for every unit** loses 737
  functions in 2.6 (CExpr 44, CDecl 29, ...) and gains one (IroPointerAnalysis' LocationSets_Overlap): the per-unit
  settings are the original build's (CodeWarrior's per-file debug column).

- **InstrSelection.c is Pro7's (October 15, 2026)**, deferred, 65/83 (was 41). 2.6's EABI pattern in a generator:
  `if (TYPE_IS_SOFT_FLOAT(type)) { SFPE_gen_*(...); return; }` first, then `IS_TYPE_VEC2X32FLOAT` (the `PC_PS_*`
  opcodes), then `IS_TYPE_FLOAT_NOT_VEC2X32FLOAT` (Pro7's float branch, its `return` become an `else if`), then the
  integer path; `!TYPE_FITS_IN_REGISTER_2` checks gain `|| (TYPE_IS_SOFT_FLOAT(t) && t->size == 4)`; operators
  without soft float start `if (TYPE_IS_SOFT_FLOAT(expr->rtype)) { CError_FATAL(n); return; }`. Some functions join
  Pro7's early returns into one if/else chain (a `jmp` to a shared epilogue says so: I8_gen_INDIRECT, I8_gen_ASS,
  gen_MONMIN ...). `SInt32 reg` for Pro7's `int reg` fixed three, a blanket replacement broke others. 2.6 also
  excludes 0 in gen_DIV/gen_MODULO's power-of-2 test, reports division by 0 in `create_*_magic`, adds an `li` +
  `or`/`xor` case for `(value & 0xFFFF8000) == 0xFFFF8000`, 64-bit bitfields (`I8_insert_bitfield`,
  `I8_extract_bitfield`), and pooled objects in gen_OBJREF/gen_INDIRECT; `RLWINM_MASK` is `((x) > 31) ? 0 : (0xFFFFFFFF >>
  (x))` (the 0 arm first). A 64-bit `switch` needs a signed `long long` selector (`ULL` cases crash MWCC: "switch.c line 662").
  The invert_relop trio (gen_LOGICAL, logical_expression_nobranch, COND_is_COMPARE) reloaded `cond->type` for the
  inlined invert_relop: `ENode.type` is Pro7's `ENodeType`, not `UInt8` (changed October 15: those three exact in
  every version, nothing lost). Near-miss round (75/83): a local assigned `outputReg ? outputReg : ALLOC_GPR()` in
  both arms of an if/else keeps 2.6's register copy only as `SInt32` (gen_condition_gpr and
  gen_negated_condition_gpr, with the RLWINM_MASK order, went exact without a rewrite); Pro7's locals won twice
  (block-local `int t` per magic block in gen_DIV, `short reg` in I8_gen_TYPCON); a soft-float check is
  `if (copts.soft_float) { Type* type = left->rtype; if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) ... }`. Deferred
  inlining reaches across functions: variants of gen_AND flip gen_SHR, which shares the inlined `shift_and_mask`.
  Left: gen_AND 98.5 (99.4 at the cost of gen_SHR), gen_CONDASS, gen_TYPCON, I8_gen_DIV_MOD, gen_MODULO 97.7,
  I8_ShiftRightImmediate, gen_COND, gen_ASS.

- **Near-miss round NM5 (October 2026; TOC, Operands, InstrSelection)**: canoptimizevectorconst, gen_CONDASS,
  I8_insert_bitfield exact in every version, gen_negated_condition_gpr exact in 2.0/2.0p1; TOC and Operands are one
  function from linking each. What the shapes were:
  - canoptimizevectorconst: `MWVector128 vec = *vecp` (the parameter dereferenced as its own type is loaded in the
    prologue, in parameter order) with `v = (signed_vec*)&vec`; the walk pointers assigned through casts
    (`p8 = (SInt8*)&vec`, `p16 = (SInt16*)v`, `p32 = (SInt32*)v`): a cast assignment is not copy-propagated, so
    2.6's `mov edx, esi` copies appear; one union type for both conversions (`union {...} conv16, conv32;`, the
    shape swap_vector_elements has: a 2-byte union sits right-aligned in its slot, the 4-byte one left); the table
    rows indexed `[(SInt8)ci]` with a `char ci`: a conversion of the induction variable keeps IRO from
    strength-reducing the row pointers (IsSafeTypcon only sees through a typcon of the variable itself).
  - gen_CONDASS: the fsel block reuses the parameter, `outputReg = OBJECT_REG(...); final_reg = outputReg ?
    outputReg : ALLOC_FPR();` (Pro7's `tmpreg` local): the parameter's extra uses give it ebx and `expr` ebp.
  - I8_insert_bitfield: `offset + loBits - 1` (the live operand first: MWCC computes into a new register with `lea`;
    a dead first operand is updated in place with add/dec) and `end` declared after `loBits`.
  - gen_negated_condition_gpr, 2.0: the EEQU peephole's `value == mask` case returns `tmpReg` (the RLWINM's
    register), not `finalReg`: a 2.0 bug fixed by 2.5 (`#if VERSION < VERSION_GC_2_5`).
  - gen_COND's isel switch had its cases in another order (2.6's jump-table block order and bit values say
    gen_CONDASS's order).
  - **A host state switched by the number of locals**: rewrite_opassign gets 2.6's reloads of `expr` after every
    store with exactly one or three extra (unused) locals, or one `(void)0;`, or one empty inline call; never with
    two or four to 39, renamed or moved locals, or global declarations. I8_extract_bitfield flips the same way (its
    `output` reloads and one tail merge). Not adopted: the original evidently had another set of locals or nodes.
  - Probes with pro6 alone (a small C file, the unit's flags): an address-taken local is reloaded after every
    store; once any local's address escapes (passed to a call or stored to a global) every spilled value is too;
    a dead `&x` (`(void)&x`, `if (0) f(&x)`, a dead `p = &x`) changes nothing.

- **InlineAsmPPC.c is Pro7's (October 15, 2026)**, deferred with `-sym off`, 23/26, checked at run time (two asm test
  files compiled identically by the equivalent and the original at -O0, -O4, -O4,s). InlineAsmPPC.h has Pro7's
  `InlineAsm`, `IAOperand`, `IAExpr`, `IAEffect(s)` (the repo's `AsmOut`/`AsmOp`), `IAEntryPoint`; the 1.x
  `EncodedOperand`/`ParsedAsmInstruction` stay (TODO) for InlineAsm.c, TOC.c, GCCInlineAsm.c, FuncLevelAsmPPC.c,
  CodeGen.c and CPrec.c. 2.6 adds the `warn_def_reserved_reg`, `volatile`/`nonvolatile` and `scheduling` directives
  (statics `pic_base_label`, `asm_volatile`, `warn_def_reserved_reg`: the unit's 8 bytes of .bss), `smclass GL`,
  `machine gekko`, effect prefixes `^ : . \`` and an `@` post-op, psq_l/psq_st's 12-bit displacement. Traps:
  `PCODE_FLAG_SET_F(info)` on an `OpcodeInfo*` reads PCode's offset (use `FLAG_SET_F(info->flags)`); 2.6 allocates
  `offsetof(PCode, args) + n * 12`, not `sizeof(PCode)`; `InlineAsmRegisterEntry.kind` is a `RegClass`; IDA's
  decompiler showed stale CPU-mask constants (read the bytes). Data renamed to Pro7's: `allow_array_expressions`,
  `assembler_type`, `supports_hardware_fpu`. Left: three register-only near-misses.

- **DWARF.c is 2.6's (October 15, 2026)**, deferred with `-sym off`, 31/38 (was 13), checked at run time (C and C++
  with `-g` at -O2 and -O4,p, soft and hard float: identical objects). Pro7 has no DWARF 1 writer, so it is 2.6's
  disassembly in gc1's shape; DWARF.h has 2.6's layouts (`DwarfNode` 0x2e bytes, `QualNode` 6). 2.6 carries a
  `BE_SymNode*` through every relocation (`BackPatchRelocationAddend` takes it: BE_elf's exact now), and adds paired
  singles, global register variables, soft-float register pairs. Shapes: a value NULL in both arms of an if/else is
  not folded (MWCC keeps the `je` and the register; one assignment folds it all: five functions); an offset declared
  `unsigned int` (BackPatchRelocationAddend's parameter type exactly, not `UInt32`) is what passes the variable to the
  call and stores the converted copy; a qualifier walker is a macro with
  byte-enum constants (an inline `f(UInt16 *q)` forces memory); `TYPE_IS_SOFT_FLOAT(t)` and a `Boolean` copy of
  `copts.soft_float` differ; an inline `DWARF_FullDebugEnabled()` keeps a repeated test a plain field read folds.
  "100.0% similar, code differs; @N is not placed" can be one wrong jump displacement: compare the bytes. Its statics
  stay global (2-byte packing) while seven register near-misses are lifted; three COMMONs keep placeholder names
  (none found fits the hash order); `DWARF_QualifiedNameSeparator` is invented (TODO), for its `"::"` literal.
  Near-miss round (37/38): two `sym ? 0 : x` call sites with ecx/edx swapped each want their own `symbol` local
  assigned right before the call; a prologue loading parameters in the other order wants a walker copy declared
  first (`Type* t; t = type;`); a switch whose cases reload the field the switch read is `switch (*(UInt16*)ref)`
  (TODO workaround). Left: newDWARF_file (a copy of `section` and a late `xor`).

- **Near-miss round, back end (October 15, 2026)**: Scheduler, LiveVariables, MachineSimulation7400 and
  MachineSimulationAltiVec link. One variable passed to two callees whose parameters differ in type (`unsigned int`,
  `SInt32`) gets a conversion copy per callee: `bitvectorunion` and `bitvectorcopy` both take `SInt32`. A loop
  entered with a jump to its test though start and bound are constants has an `int` counter against an unsigned
  bound (`i < sizeof(a) / sizeof(a[0])`; an enumerator bound or an unsigned counter drops the entry test): that was
  AltiVec's advance_clock. MachineSimulation7400 needed only `-sym on` (and deferred, like the other models): sweep
  `-sym` and deferred before hand-shaping. A spilled pointer parameter 2.6 reloads before each store through it
  matched through `PCodeBlock** pb = &block; (*pb)->...` (scheduleblock, TODO workaround; rlwimi_intrinsic's
  missing reload of `output` resists it).
- **Near-miss round, the rest (October 15, 2026)**: ValueNumbering, Utils, ELF_Endian and IROUseDef link (IROUseDef's
  statics are Pro7's, `static`, `AssignmentFoldingFunction[78]` highest; `NumDefs` static keeps IRO_SplitLifetimes
  exact). Shapes that turned functions exact:
  - comma expressions for two adjacent statements (`a->next = tab[i], tab[i] = a;`, `x[i] = NULL, y[i] = NULL;`,
    `for (instr = NULL, list = ...; ...)`), and `for (i = n; i--; op++)` against `while (i--) {...; op++;}`;
  - the assert's spelling: `if (c) { CError_FATAL(n); }`, `CError_ASSERT` and a do/while compile differently
    (add_anchor_reference, which also reuses one `list` for three walks);
  - an argument assigned inside the call (`f(tmp = e->data.monadic, ...)`) is loaded after the other pushes;
  - an inline helper's parameter typed as the enum it receives (`invert_relop(ENodeType)`), or as 2.6's `SInt32`
    (the range bit helpers: a shared `bit >> 5` then lives in a stack slot);
  - a `TypeStruct*` copy of a `Type*` parameter that the original reloads `->type`/`->stype` through;
  - two if/else arms MWCC merges that the original keeps apart were written differently (`f(p = x)` in one arm,
    `p = x; f(p);` in the other); dead locals counted by register (Utils_SpellList has three, `x` reused);
  - a dead loop with its own block-local variable (IRO_UseDef); a jump table's case order is the source's.
  - "100.0% similar; .bss is not placed" was one jump displacement from a wrong `||` operand (call_function): such
    a function places nothing, so diff its bytes with relocations masked.
  Workaround: `(short)(UInt32)opA->immediate` in Operands' combine (TODO). addobjectusedef's out-of-line copy and its
  inlined copies want opposite local orders. 0x5e30b0..0x5e30e0 is MSL locale.c's 48-byte .bss (`data_at` `.bss`):
  FunctionCalls' range had covered it, then Unmangle's once call_function placed FunctionCalls' .bss.
  Alias links (21/21): a jump table exposed a wrong case constant (`PC_ADDIC` for 2.6's `PC_ADDICR`, a real bug):
  read the targets per index. An `SInt32` loop counter keeps the loop-entry test an `int` one loses
  (move_block_via_load_store_sequence). A doubled `jmp` is an explicit `else { x = p->f; }` after
  `else if (!p->f) {...}`. Splitting one shared variable (insertlongbranches' `offset`, a second loop counter) fixed
  two more.
- **Near-miss round, front end (October 15, 2026)**: CExprConvMatch, CTemplateClass, CTemplateNew and CABI link
  (2.6: 2349 functions, 38.3% of bytes). A local that exists only to be assigned (`IS_TYPE_FUNC(type = obj->type)`,
  `expr = f(); left = expr;`) changes coloring though nothing reads it again, and sometimes it must be one of the
  function's existing variables (do_typecast's `tptr`, CABI_TransDestructor's `next`); decl-order cannot find
  these, and a merge plus a reorder may only work together (a 144-variant `fn.py variants` run found CABI's). A
  struct copy through `*(T*)&local` computes the address first where 2.6 stores straight into a member
  (CDecl_GetFunctionObject's union `tmp`). Non-deferred units emit out-of-line copies of statics inlined at every
  call: `static inline` (CTemplateClass, CTemplateNew; relink says "defines its functions in another order").
  CTemplateNew's fake COMMONs `data_0058243x` moved the COMMON tail (deleted). Left: tquot (2.6 has a second,
  unreferenced spill before `start_pos`), CTemplTool_DeduceTemplDepType 97.4, CExpr_ParseNameResultExpr 98.2.
- **Near-miss round 3 (October 15, 2026)**: 2.6 3962 -> 3991 exact, 2384 -> 2482 linked (60.8%, 44.2% of bytes).
  Newly linking: Intrinsics (`checkconstintarg` reads `expr->data.intval.lo`, not `CInt64_GetULong`, which kept
  rlwimi_intrinsic's reload; Pro7's unused `VectorConditions` COMMON shifted the COMMON tail by 4), UseDefChains
  (addobjectusedef walks with the existing `list`: `while ((list = *ptr))`), CObjC and CObjCModern (CObjCModern.c is
  2.6's own object, not an include: its unpooled "super" and literals after CObjC's jump tables say so). StackFrame
  37/41 with two real bugs (`"_savevr%d"`/`"_restvr%d"` helper names; `copts.incompatible_sfpe_double_params`), CPrec
  80/84, StructMoves 11/12. Shapes:
  - integer types decide inlining matches: StackFrame's `update_*_align` take `int` (Pro7: `SInt32`), as do
    `can_add_displ_to_local`'s `displ` and StructMoves' size helper; `32L - i` converts a `short` once (TODO);
  - 16-bit arithmetic is compound assignment to a `short` (`s = a; s <<= 2; s += b;`, no `movsx`);
  - split and merge locals as a search (2.6 often has a fresh variable where Pro7 reuses one, and sometimes the
    reverse); check merges for overlapping lifetimes, which a search will happily accept at 98%;
  - read the jump table's targets: a branch jumping past a shared `x = 1` has no `x = 1` (FoldConstants...);
  - an integer field cast to a pointer at its use changes registers: type it as Pro7's pointer
    (`IStmtRec.sourcefilepath`);
  - stand-in copies of helpers block deferred inlining: give the real function Pro7's shape and call it
    (CInline_PackActions 55 -> exact once CInline_GetLocalID was Pro7's);
  - `f(x = New(), y)` into an inline function makes a register copy; `copy = New(); f(copy, y); x = copy;` does not;
  - per-function `#pragma sym off` again (IroPointerAnalysis' LocationSets_Overlap); a pragma on an inline function
    also applies to its inlined copies;
  - (DoTransformations24 was once thought to match with any one statement removed; round 4 found it does not: 2.6
    reloads the parameter `nd` from its slot in every statement, as if it were address-exposed. Cause unknown.)
  Left (register-only, each equivalent statement by statement): InlineAsm_GetObjectOffset, insertspillcode,
  newDWARF_file, computeaddilist, isloopinvariant, analyze_arguments, propagateconstantstoblock,
  move_block_via_inline_loop, StackFrame's 4, InstrSelection's 8, CPrec's 4, IroPointerAnalysis' 8, the front end's
  ~13; CPrec_GetTypePatch is an inlining difference (2.6 keeps two calls ours inlines).
- **Rock-solid review and the header track (October 16, 2026)**: `docs/review-2.6.md` grades linking units against a
  written bar (boundary, flags, every departure from the reference explained, behaviour, names, types, data, header,
  the headers relied on), each by a reviewer and then an adversarial reviewer; headers are reviewed by family. A
  spelling plausibly Metrowerks' and not obscene is the original's shape (`(T*)((long)p + n)` is their house idiom);
  a dead assignment is judged by context; documented TODOs wait until 100%. Findings:
  - deleting a macro does not break the build: MWCC compiles a remaining use as an implicit call (`PC_SETCOUNT`; the
    only sign was "not bound" in the comparison): grep every use first; likewise an `#include` dropped from a source
    can leave a call implicit (PCodeInfo's `is_SDAdata`, GlobalOptimizer's `computeusedefchains`);
  - a halfword field compared with a byte-sized enumerator compiles `movzx; test` where `cmp word ptr` is wanted:
    section kinds (`SECT_DEFAULT` ...) are `#define`s, like the opcodes;
  - Pro7 types 2.6 lacks: `pcloopweight` and `setpcodeflags`' parameter unsigned,
    `number_of_candidates` and `Loop.bodySize` `SInt32`; byte enums that cost nothing as field types: `Alias.type`,
    `Loop.lowerType`/`upperType`;
  - `sizeof(Object)` is 0x44 (`push 0x44`); `COpts`' `bssSection`/`smallDataSection`/`smallBSSSection` are really
    `#pragma section`'s const/sdata/sconst sections (typebits 4, 8, 0x10): misnamed, not yet renamed;
  - Pro7's `CTool_CreateIndexFromPointer`/`CTool_ResolveIndexToPointer` are Ninji's 64-bit-host additions, not MWCC's:
    2.x stores the pointers;
  - `includes.py trim` drops headers a unit uses directly when another include brings them in: check what it removes.
  - **2.6 was compiled against Pro 6's MSL headers and linked with Pro 5's MSL library**: Pro 6's `_ThreadLocalData`
    puts a `next` link first, so the compiler's own `errno` (CLIO, ScanFloatX86) is word 1 while the library's is
    word 0: the original's `errno` reads read `random_next` (a real bug of its build, reproduced by plain `errno` through
    `include/libc/errno.h`). Check any other MSL struct the compiler's own code reads against Pro 6's headers.
    `_HandleTable` entries are MSL's `FileStruct {handle, translate, append}` (`include/msl/crtl.h`). lib/msl's files
    have CR line endings and a (c) byte: ugrep skips them silently; use `/usr/bin/grep -a`.
  - Review-round lessons: start a unit from Pro7's verbatim text, then apply 2.6's changes (in the driver Pro7's text
    matched 45 of 58 functions the old sources had "shaped"); decompiler temporaries (`x = f(); return x;`,
    `p = f(); if (p == NULL)`, alias locals) compile like the plain spelling; check flags (PCode.c, Registers.c,
    IroBitVect were deferred units built non-deferred; CLStaticMain's pragmas were build flags); a needed local copy or
    integer type can hide a wrong prototype (`bitvectorinitialize`'s `unsigned int` for Pro7's `UInt32`; SDK
    `short outputType` let `linkOutputFile` match); SDK flag enums are `1L << n` (a `0x80000000` enumerator makes the
    enum unsigned); a literal where the SDK has an enumerator signals a stand-in field type; Pro7's dead functions
    belong where the asserts after them carry Pro7's line numbers (Wii lacking them proves nothing: its linker drops
    dead code too); a Wii procedure with no public is `static`; Wii's `@N` decoration is not proof of `__stdcall`
    (OS_CanonPath, OS_MacToRef are cdecl); COMMONs no source defines go where Pro7 defines them, and COMMON names are
    checked by hash order (`clabels` was Pro7's `Labels`, `current_statement_number` `curstmtvalue`); a `copts` byte no
    pragma covers is never set in 2.6 (name it from a reader); a binding for a global another unit's exact code
    references must stay even when a compiled object defines it; removing a transitive include can make a
    `__stdcall` callee implicit and called cdecl without an error: run clang_check on the includers;
    `evidence.py review` keeps name_review.json's 2-space indent (fixed).
  - `rename.py` leaves a type name inside a struct body alone (taken for a member): after a swap (`Deps` <->
    `DependencyCollection`) grep the struct bodies; `--member` flattening an embedded struct (`File.listEntry=next`)
    writes `&file->next` where `file` is meant: fix those sites by hand. The plugin SDK's real headers are in
    `../MWCC_Pro7/sdk_hdrs`; Pro7's derived plugin contexts work as C++ inheritance under `#ifdef __cplusplus`.
- **Review batch Z (October 16, 2026)**: the front end (CError, CInt64, CompilerTools, CMangler, Unmangle, CParser,
  CPreprocess, CBrowse, CExpr, CInit, CExprConvMatch, CClass, CABI, CRTTI, CScope, CException, the three template
  units, CObjC, CObjCModern, CSOM) and the driver's parser library and plugin glue are now Pro7's text plus 2.6's changes,
  every function still exact (DateTimeUtils.c split from COSToolsCLT). Lessons:
  - **Pro7's text cannot tell `return` from a jump to the shared epilogue** (Ninji matched it against the PPC compiler):
    where 2.6 has one epilogue, the original had an `if`/`break`/single `return`; several catalogued "2.6 shapes" are this.
  - **Pro7's text has bugs of its own** (CException's `lalloc(sizeof(ExceptionAction))` for a CatchBlock, error 123 for
    135, CScope's extra argument to error 188): check the pushes before calling a difference a 2.6 change. Pro7's names
    for flags passed in swapped order can follow the minority of calls (CExprConvMatch's `x14` is `implicit_object`).
  - **A "needed" local type or a stack-slot fakematch can be a stand-in elsewhere**: the field or parameter it is
    stored to (`ObjMemberVar.access` is `AccessType`, `CPrep_PragmaLex` takes `Boolean`), or a padded saved struct
    (`CScopeSave` is 13/14 bytes, not 16). Before inventing a helper, try the function's existing walker variable as the
    temporary (`stmt` then `beginCatchStmt = stmt`); Pro7's `// r14` notes on two locals hint at one.
  - **Build flags replace pragmas**: a file-top `#pragma sym`, per-function `sym on` blocks, `scheduling off` and
    compiler overrides were wrong whole-file flags (CError `-sym on`, DropInCompilerLinkerPrivate `-sym on`, the `_V10`
    units `-bool off`, cc-eabi-ppc plain pro6 `-O4,p`). The C++ driver units need bool off (`a && b` is byte arithmetic
    otherwise). Passing per function does not prove the flag: Unmangle is exact non-deferred but relinks only deferred.
  - **Per-function COFF `.text` sections** are 16-aligned under -O4,p and 1 under -O1 and -O4,s: a unit starting off the
    16-byte grid is not -O4,p. **-O1 driver units keep dead stores** Pro7's PPC build removed, so a dead local there is
    probably the original's (still a TODO name); gc1 often has it (ShowVersion's driver and parser versions).
  - **`!x` and `x == 0` differ** (most at -O1); `!f() == FALSE` in a condition is 2.6 computing `!f()` into a register;
    `a && (b && c)` differs from `a && b && c`; `(Boolean)(a == b)` in an `if` gives `sete; and; test`; a ternary
    allocation (`c ? galloc(n) : lalloc(n)`) counts one statement toward the auto-inline limit; nested inlining takes at
    most 10 statements at depth 1 (7 at depth 2) whatever `inline` says; MWCC takes `inline` from a static's first
    declaration; an inline helper and its body written in place allocate registers differently in a deferred unit;
    assigning inside a condition (`ENODE_IS((x = y->f), K)`) loses MWCC's cached byte load.
  - **`#pragma options align=reset` works**: a unit whose .bss needs pack(2) at the end of the file did not reset.
  - **COMMON evidence**: defining a COMMON the original never references (Pro7's `gUseTokenStreamSource`) breaks the
    relink; unreferenced Pro7 COMMONs can hide inside a neighbour's symbols.txt extent (helpExtras, showedHelp); hash-check
    placeholders too (CError's `data_005830c8` is out of order); a neighbour that is itself out of order weakens the
    evidence; "the original defines here" comments over COMMON blocks were unfounded (CPrec, CError). A dead Pro7 function
    calling something 2.6 lacks fails the relink on the undefined symbol (CompilerGetPString), so leave it out.
  - **Wii's CodeView has global types** (sstGlobalTypes: struct/union/enum records with fields, offsets and signedness),
    which settle stand-in structs; `tools/mw/wii.py` does not read them yet (Z_DR1's `work/wiitypes.py` did).
  - **CError.h names messages past 371 now as they are needed** (372-374, 381, 382, 387, from the binary's table; Pro7's
    CError.h has the text up to 392). The driver's C++_Parser asserts are MSL's `assert` form: `OPTION_ASSERT` (oslib.h).
  - ObjC data stops both 2.6 and the equivalent compiler with an internal error (ObjGen_PPC_EABI.c line 1301): compare
    diagnostics only. `rename.py --member` needs a record name (anonymous union members: sed after grepping). An empty
    slice passed to `str.replace("", new)` writes `new` between every character (CFunc.c became 54 MB and MWCC "hung").
- **Review round Y (October 16, 2026)**: the bar applied to 16 units that do not link (SpillCode, VectorArraysToRegs,
  CodeMotion, LoopOptimization, CPrepTokenizer, CPrep, CDecl, CInline, TOC, Operands, BE_elf, FunctionCalls,
  ConstantPropagation, IroPropagate, IroLoop): all rewritten toward Pro7's text, no exact function lost, the misses left
  register-only. DeclInfo has Pro7's member names (`thetype`, `storageclass`, `exportflags`, `expltargs`, `file`,
  `file2`, `sourceoffset` ...); `PrepTokenBuffer` is Pro7's `TokenStream` (tokens.h); `Inline_IsObjectData` is 2.6's
  `DDATA || DABSOLUTE` (no second helper). Lessons:
  - **Several "fakematches" were a wrong type one level out**: a callee's parameter (`pragma_on_off_reset(UInt32)`; a
    cast that makes a test CSE with a call argument means the callee's type differs), an inlined helper's prototype
    (`Initialize_Input` takes `UInt8`; `CDecl_NewTemplDepType` returns `TypeTemplDep*`), a header field
    (`Operand.immOffset`/`.immediate` `int` vs Pro7's `SInt32`: fixes Operands' two fakematches but costs
    Coerce_to_register one reload; open), or Pro7's own pointer cast (`UInt32* value = (UInt32*)data` keeps `data` in
    its slot). A `void*` prototype parameter can be what keeps a caller in another unit exact (CInline_AddTemplateFunctionAction).
  - **2.6 reloading a value after every store, as if address-taken** (rewrite_opassign's `expr`, createfloatconstant,
    load_address): `(void)&x` reproduces it; look for what took the address before shuffling registers.
  - **Count a deferred unit's .data tables**: a plain `static` helper's out-of-line copy adds a jump table the original
    lacks (ConstantPropagation could never link); `static inline` fixes it.
  - **Assert-line offsets locate dead functions**: a constant offset to Pro7's lines across a span means Pro7's text is
    there (CPrep: +9 over Pro7 lines 673-1424; six dead functions back). A static only a dead function reads confirms it.
  - **Statics are declared near their first user** in 2.6 (CPrep's `high_mem_mark`, `exprtk`), not in a top-of-file
    block; `GList` is 2-byte packed (a static one sits at 2 mod 4).
  - `if ((i = f()) > 0) { --i; ...}` gives 2.6's single `dec` (a separate `i--` is copy-propagated into `i - 1`);
    `x & ~0x80000000` differs from `x & 0x7FFFFFFF` (unsigned constant); `!x & FLAG` precedence bugs are compiled as
    written (CodeMotion's isloopinvariant: kept, commented); one variable reused for two results lets MWCC tail-merge
    calls the original keeps apart (rewriteunknownloopwithBDNZ's second `branch`).
  - 2.6's own auto-inline limit (CInline_IsSmallFunction, for the code it compiles) is 30 statements, not Pro7's 15.
  - A per-function `dont_inline` can be a size question: one `if` per field puts the function over the limit.
  - Wii 1.0RC1's CodeView global types cover only the driver's and the SDK's structs, and it has almost no local-data
    records: it cannot name compiler types or back-end statics.
  - Pro7's placeholders (`OverloadMode0..3`, `FUNC_FLAGS_*`) and joke labels need names (with TODOs) even when verbatim.
  - SpillCode's last 256 bytes of .data were BitVectors.c's dead `bitvectorcount` table (`data_at`, 2.6 only so far).
- **No symbol may contain another (October 16, 2026).** symbols.txt's sizes are inferred: a symbol a compiled object
  places has its object's extent; one named only by a binding runs to the next symbol of any kind. `layout.py` warns
  when a symbol starts inside another: the cause has been a stale fragment binding inside a placed object (delete it
  when no source uses the name), a struct whose size differs from 2.6's (CPrep's special macros are 20-byte statics:
  `Macro.names[1]`, not Pro7's `names[0]`), or a size guessed across another object (`_stUnknownLs` had swallowed 335
  literals). 2.6 has none; 2.0/2.0p1 keep two IroLoop statics whose bindings disagree with 2.6's layout.
- **.bss statics are named (October 16, 2026).** MWCC's COFF symbol table has no symbol for a `.bss` static (code
  refers to `.bss+offset`), so symbols.txt called them `lbl_ADDRESS` and named them only through bindings (and an
  array's every referenced offset was a label of its own). layout.py now reads each placed object's statics from its
  symbols-on build's CodeView (`S_LDATA32` records pointing into `.bss`): 546 labels in 2.6 became names. A static's
  binding can go once its unit links; the name stays. layout.py builds the debug objects first (`layout.ok` depends
  on them).
- **Every symbol is a real name or the compiler's (October 17, 2026).** symbols.txt went from 252 placeholders in 2.6
  (about 765 in each other version) to three per version, each one nothing can name. What did it: placeholder
  bindings no source uses deleted (1.x addresses and IDA's `s_register_0056335c` shapes; a binding outranks the
  compiled object's name, so `_data_005434a8` had hidden cc-eabi-ppc's `sFlags$30`); layout.py naming each function's
  exception action table `@exc_FUNCTION` (the compiler gives it none); data a `data_at` places, a linked unit's
  unanchored sections and the unit after the build stamp now named by their objects; an inexact function's tables
  named by its compiled code's references in the same order (`@8146`..`@8150` for gen_COND's: the numbers descend as
  the addresses rise, as MWCC allocates them); a COMMON's size from its symbol (`curparam[4096]`, which
  `helpparam`/`defaultparam` point into); a `.bss` static sharing a global's name (IroMalloc's and MSL's
  `fix_start`); a unit starting where its object's data does, the bytes before it the previous unit's padding though
  its code refers to them (`signal_funcs[sig - 1]`). Left: Help_Option's and call_function's local initializer
  templates (MWCC puts a zeroed aggregate's template in `.bss` with no symbol and no CodeView record) and 0x21d
  unreferenced `.bss` bytes before Peephole's `defininginstruction`.
- **Near-miss round 4 (October 16, 2026)**: 2.6 3991 -> 4020 exact, 2482 -> 2806 linked (68.8%, 52.0% of bytes).
  Newly linking: MsDos (the driver is whole), StructMoves, PCodeAssembly, InlineAsm, DWARF, ObjGen_PPC_EABI, CExpr,
  CTemplateTools. Real bugs found while matching: GeneratePossibleCondAss walked the wrong tree for kills (`r24`) and
  `logicalType` tested `copts.no_static_dtors` for `booltruefalse`; CodeGen_ParsePragma lacked the error on a clamped
  `bc_range`; user_assign_check tested `explicit` on the TypeFunc, not the Object's `qual`; PCodeInfo called
  ObjGen's invisible `static inline is_SDAdata` as an implicit `int` function (2.6: `is_16bitdata`; GlobalOptimizer.c
  also called `computeusedefchains` undeclared: `tools/clang_check.py`'s implicit-declaration errors are bugs).
  Shapes:
  - **A dead `jmp` after the last `ret` is an emptied loop preheader**: MWCC hoists a loop-invariant expression (a
    signed variable converted for a compare with an unsigned one: OS_Write's `SInt32 pos > DWORD end`) into a
    preheader at the end of the function, and when the hoisted value costs nothing only the `jmp` back is left.
  - **A stack slot holding a copy of an `int` can be MWCC's CSE temporary** for `(size_t)x` shared by a `strlen(..) +
    x` and a `memcpy(.., x)` (OS_ReadDir's "nlen"): `int x`, no invented copy. An `SInt32` makes no temporary.
  - **Parameters loaded out of order in the prologue, or loaded and copied to a second register**: the parameter is
    another pointer type copied into a typed local (`void* p` + `InlineAsm* ia = p;`, newDWARF_file's `void*
    osection`); a same-typed copy is coalesced and declaration order does nothing (TODO workarounds).
  - **Merge and split locals first**: most fixes this round were 2.6 using one variable where Pro7 has two
    (IroPointerAnalysis' `found`/`result`, `ic`/`call`, `list`/`addend`, `tmp`/`unknown`; prepinclude's `i` is the
    existing `t`) or a fresh one where Pro7 reuses (unrollloop's walkers, generate_traceback's second `arg`,
    CTemplTool_DeduceTemplDepType's block `arg`, Peephole's `p = array[i]; scan = p;`); invented intermediate locals
    cost matches (CExpr_MakeFunctionCall's `obj`, `member`). An assignment nothing reads, to an existing local inside a
    condition (`(expr = args->node)->rtype`, `IS_TYPE_CLASS(r18b = expr->rtype)`), moves registers decl-order cannot.
  - **Inline helpers' shapes reach their callers**: a 2-line accessor returning through a local (`Stack_Top`) fixed
    Lookup; an inline helper copying its parameter (`int w = width;`) places the store earlier (StructMoves'
    `copy_chunk`); a helper's local retyped (`TypeFunc* method`) took CDecl_AddFunctionMember 70.9 -> 98.5; reordering
    an inlined helper's locals moves the caller's registers (CPrep_PragmaOnce). A condition whose last operand is
    materialised (`mov eax, 1 / xor eax, eax; test`) while the earlier ones branch is an inlined helper written `if
    (...) return 1; return 0;` (gen_ASS's `is_gpr_float_copy`, invented). A shared "skip and return" written inline
    jumps to one epilogue; 2.6's separate epilogues per exit are a macro whose arms each `return`.
  - **Stack-slot order shows scope**: function-level locals are laid out first in declaration order, block-locals after
    them ascending in source order; slots descending through a run of values are function-level declarations in
    reverse (CodeGen_CollapseVectorExpression 52 -> exact). A spilled `instr` 2.6 does not reload after byte stores
    through it wants a block-local copy (`{ PCode* t = instr; t->... }`, Peephole, TODO). A local typed as the field's
    enum (`RegClass arg = op->arg`) is loaded early and held in a register where a `UInt8` copy folds into a `cmp byte`.
  - **Unreferenced bytes at the start of a unit's .bss** that layout.py gives the next unit shift every later address
    (relink shows `mainCRTStartup` diffs): a static of that size declared first (DWARF's 8 bytes, invented, TODO).
    ObjGen's five `ObjGenSection*` defaults sit in reverse declaration order. A `*(volatile UInt16*)&x` read reproduces
    2.6 comparing two loaded fields and then reloading one (resolve_object_pseudo_section, TODO).
  - MWCC pads a label to 16 bytes only when that costs about 7 bytes or less: a missing `lea eax, [eax]` follows size.
    MWCC x86 4-aligns every local array of 4+ bytes (pro5/pro53/pro6); InitElfFile's `filename[257]` at esp+7 is
    unexplained. Per-function `#pragma sym off` took CMach_ParseGlobalRegisterVariable 92 -> 99.4 but did nothing in
    InlineAsmPPC; ShouldAddNewPointsToEntryToFunction's old `sym off` was a red herring.
  Left (register or frame only, each checked equivalent): computeaddilist, isloopinvariant, analyze_arguments,
  IRO_CopyAndConstantPropagation, propagateconstantstoblock 98.9, insertspillcode, tquot, BackEnd_WriteElf 99.5,
  InitElfFile, vformatpcode 99.7, Operands' I8 bitfields, TOC's two, InstrSelection's 8, Peephole's adjustforward and
  ADDI_LU_SU, InlineAsmPPC's two, StackFrame's 4, CMachine's 2, move_assigned_argument, CPrep's 2, CInline's
  UnpackIFunctionData, CDecl_AddFunctionMember, CExpr2's 2, CFunc's 2, CPrec's 4 (GetTypePatch: 2.6's partial
  inlining appears exactly when the bitfield helper has a branch), IroPointerAnalysis' MatchPTF and ParseLocationSet,
  the Iro/loop ones (DoTransformations24's parameter reloads, GeneratePossibleCondAss' two extra spills).
- **`fn.py diff` compares references (October 15, 2026).** It used to print every address as `ADDR` and its
  similarity masked them, so a wrong global, callee or 6+-digit constant looked identical ("=" lines, 100% similar):
  a swapped `&elipsis`/`&oldstyle` (FunctionCalls), a `& ~4` where 2.6 has `& ~0x40000004` (ObjGen's
  Finish_Pragma_Section, called register-only by its agent) and `copts.logopts` where 2.6 tests
  `copts.gprfloatcopy` (gen_ASS) all hid there. Now addresses print as names (`name+0xN` inside a symbol, `.+0xN`
  for a branch inside the function, `?` unresolved), a line is '=' only when its references agree, and
  `verdict.similarity` does not count a matched instruction whose references differ; when both sides are the same
  size, a branch inside the function to another offset is '~' too (pragma_scheduling's case order and ParsePragma's
  missing call hid there). The exact verdict was always
  strict (bytes with references resolved); only the view and the score lied. A function whose similarity dropped
  when this changed has a wrong reference somewhere: check its '~' lines that differ only in a name.
- **Fragment bindings inside `copts` (October 15, 2026)**: 26 `_data_005e7xxx` bindings (and
  `cerror_suppress_warnings`, which is `copts.supress_warnings`) sat inside `copts` in every version, so lifted code
  and the stand-in externs (`data_005e7196` for `copts.old_argmatch`) read copies the pragmas never set in the
  equivalent compiler. Deleted; the code reads the members. 2.0/2.0p1 lose 9 front-end functions (their `COpts` is 2
  bytes shorter, which the stand-ins had papered over).
- **Rewrite round 2 (October 15, 2026)**, the audit's C/D units (2.6: 3888 -> 3962 exact, 2349 -> 2384 linked, 39.4%
  of bytes). Now Pro7's (or 2.6's) files, deferred: CPrec (77/84; the gc1 text and its ~100 invented helpers gone),
  ObjGen_PPC_EABI (49/51), BE_symbol (18/18, links), BE_elf (18/21), InlineAsm (22/23, Pro7's IA types),
  SFPE_PPC_EABI (8/8, links in every version), PCodeInfo (vformatpcode 11.9 -> 99.7), InterferenceGraph (links),
  SpillCode (7/8), BitVectors (+ 2.5's compressed vectors, `#if VERSION >= VERSION_GC_2_5`), LoopOptimization
  (14/16), CodeGen (39/43). Wii's data publics named whole structs: BE_elf's 82-byte `ELFfile` and `TheSymbolTable`
  replaced ~25 fragment bindings, ObjGen's `AllFiles`/`CurFile`/`objcodesize`, InlineAsm's `backtrack`/`backtracking`/
  `InlineAsm_setjmp_valid`. Findings:
  - **A header leaked `#pragma pack(2)`** (CExpr.h pushed twice, popped once) into every includer. Fixing it broke
    CError, CInit and CTemplateFunc's relinks: their .bss needs pack(2) in effect at the end of the file, so they now
    end in `#pragma pack(2)` (TODO, like CompilerTools). A unit whose .bss has 2-byte gaps the original lacks, or
    lacks gaps it has, may be one of these.
  - **`data_head` works for .bss**: CodeGen's statics made `static` moved its placed .bss start past `saveheaperror`
    (0x5dec28, a global 2.6 has at offset 0 of CodeGen's .bss), and the gap went to IrOptimizer, which then failed to
    link (undefined symbol). `"data_head": {"src/backend/CodeGen.c": {".bss": "0x005dec28"}}` (2.5, 2.6, 2.7).
  - **Statics in reverse, again**: CPrec's statics in Pro7's order put its placed .bss 0x22 too low and took
    CPreprocess' and CBrowse's ranges; 2.6 has `flushmax` lowest, so they are declared in reverse.
  - **`#pragma sym off` for one function in a deferred unit** works (before it, `reset` before the next function in
    the source): BackEnd_WriteElf 85.6 -> 97.0, where `-sym off` for the whole file cost five others (TODO). InlineAsm
    needed `-sym off` for the whole unit (PrimaryExpression's switch selector).
  - **Deferred units couple neighbours**: a change that emits no code in one function (an unused local, `(void)0;`)
    flips registers in the next function generated (binary address order). When a function's only difference is a
    parameter reloaded or not, vary the previous one and watch `fn.py variants`' "others:".
  - **Assignment inside a macro argument**: the register allocator's `bitvectorclearbit(reg = op->data.reg.reg, vec)`
    keeps a 16-bit register variable and stops MWCC merging the get/set bit computations (eliminatedeadcode 52.5 ->
    exact); a plain `short reg = ...;` is forwarded into its uses. One negated condition with one `continue` for a
    range test (coalescenodes), Pro7's four `if (...) continue;` give other registers.
  - **Per-case variables in a big switch** (`case 'S': { int i_S; ... }`) took vformatpcode 97 -> 99.7 after
    decl-order stalled. A `pool_strings` unit needs every literal in its pool: dead Pro7 functions came back.
  - **Small shapes**: `for (x = f(); c; x = f())` as `x = f(); while (c) {...; x = f();}` (another register for the
    init); `while (1) { if (!c) break; ... }` for a loop tested only at the top; a duplicated epilogue is a `return` in
    each arm; explicit case labels that only fall into the error branch still fill the jump table (CodeGen's
    EFLOATCONST: 40 bytes of .data); the order of a local copy and the load it copies decides CSE
    (`UInt8 tt = nd->rtype->type; Type* type = nd->rtype;`); a chained `a->f = b->g = call();` reloads `b->g`; a
    parameter in the wrong register of an internal function can be typed `void*` and cast (emit_relocation, TODO);
    integer parameter types again (AddSymbol's `SInt32 qual`, DoElfContentSection's `SInt8 align, SInt32 size, UInt32
    permissions` matching InitElfSection's so no argument copies; CPrec's `SInt32 CPrec_AppendAlign`, 2.6 working in
    image offsets); a literal compared against NULL is not folded (a second GetSectionByName without the test, TODO).
  - **Merging**: `merge_agent.py --apply` writes nothing at all when any file conflicts (configs included). Agents
    renaming names other agents' files use need their renames reapplied over the other side's version.
  Left (register-only unless said): InlineAsm_GetObjectOffset (parameter load order), insertspillcode, ObjGen's
  resolve_object_pseudo_section and Finish_Pragma_Section, BE_elf's BackEnd_WriteElf/CloneSection/InitElfFile (a
  buffer's stack offset), vformatpcode's `'T'` temporary, CPrec's GetTypePatch 65 (2.6 inlines its level-2 callees
  differently) and six more, Peephole's RLWINM_RLWIMI_STW/STH and ADDI_L_S (ours reloads `instr` after stores),
  CodeGen's ParsePragma/pragma_scheduling (code matches; literals unplaced until CodeGen relinks).

## State of the optimizer cleanup (October 2026)

GC_2_6: linked from source 2101/4080 functions (51.5%), 474145 of 1536118 bytes (30.9%), 183 sources; 3716
functions exact (October 14, second agent round, from 1513/300611/153/3628: PCodeListing, CRTTI, Unmangle with MSL's
locale/float data, LoadDeletion, CIRTransform, LicenseImports, NewHand, newop, CMangler, CTemplateFunc, CClass,
ScanFloatX86, CLIncludeFileCache, CLBrowser, TargetOptimizer, CLIO, CLFileOps, Resources, Option, both _V10 units,
CPreprocess, CParser, CInit, CError, GlobalOptimizer, PCodeUtilities, InlineAsmMnemonicsPPC with the generated
OpcodeInfo.c, InlineAsmRegistersPPC, IroFlowgraph; before that from 947 by IroUtil, IroVars, IroEmptyLoop, Switch,
Registers, RegisterInfo, PCode, StrengthReduction, CopyPropagation, AddPropagation, CompilerTools, CException, CBrowse,
CLMain, CLAccessPaths, CLPluginRequests, CLPlugins, CLLicenses, CLErrors, CWPluginsPrivate.cpp, Arguments,
ParserErrors). Every Matching source links; no unit has every function exact and fails the link. One function short
(`status.py --todo`): LiveVariables (computeglobalinout 87.8), Scheduler (scheduleblock 97.9: 2.6 reloads `block`
before `firstPCode = lastPCode = NULL`), CExprConvMatch, MachineSimulation7400, Intrinsics (rlwimi_intrinsic),
VectorArraysToRegs (computeaddilist 94.8; then its six statics become `static` with `#pragma pack(2)` at the end),
CPrepTokenizer (tquot: one stack slot), Utils, CTemplateClass, ELF_Endian, CTemplateNew, CTemplateTools, CABI,
IroPropagate (MsDos links since round 4). The per-unit audit
(grades A-D, rewrite lists: `docs/audit-2.6.md`) found the heavy 1.x code in the
back-end global optimizer (ValueNumbering, ConstantPropagation, FunctionCalls, the 1.x use-def model with
`reg_Defs` faked by pointer arithmetic: convert UseDefChains.h to Pro7's first), the code generator (TOC, Operands,
Exceptions, CMachine, half of InstrSelection, SFPE), DWARF, InlineAsmPPC, PCodeAssembly, MachineSimulationAltiVec and
CPrec; and almost every unit not yet rewritten is Pro7's file reversed in the binary, so it is deferred. Leftover 1.x
code with no rows still sits in non-linking callers: CopyPropagation's in ValueNumbering.c (beside COpt_CopyPropagation),
LoadDeletion's in ConstantPropagation.c, Registers' in Coloring.c, PCodeUtilities' fn_004a1cb0 in FunctionCalls.c and
PCodeUtilities_004a2290 in InlineAsmPPC.c. FuncLevelAsmPPC's setup_assembly_argument is 30%. The driver's option and
plugin tables are generated (ParserGlue-eabi-ppc-cc, cc-eabi-ppc, libimp-eabi-ppc link), and the driver and OS layer
are Pro7's files with Pro7's (Wii-confirmed) names. Left there: Utils_SpellList,
and the EABI panels' unnamed bytes. (Done since: Files.c in Pro7's shape (FInfo, HFileParam); Pro7's `Handle` for
the repo's `StorageHandle *`; the plugin contexts, mwcc/Plugins.h's old `PluginA/B/C`, are the SDK's
`CWPluginPrivateContext`, `CWParserContext` and `CWCompilerLinkerContext` in CWPluginsPrivate.h, and the shell's UCB*
callbacks take the SDK's types.)

(Not all of the following are built deferred yet: IroLinearForm, IroUnrollLoop, IroLoop, IroBitVect and most of the
front and back end are written in the binary's order with plain `-inline auto`; `docs/names-2.6.md`'s "Deferred
units" lists them.) In Pro7 shape, order and names, `-inline auto,deferred` (exact/rows): IrOptimizer 6/6, IroCSE 18/18, IroLinearForm
34/34 (names not yet cleaned), IroMalloc 12/12 (MSL's 8-byte-alignment allocator), IroUnrollLoop 19/19 (3 rows still
without source: UnrollWhileLoop, UnrollForLoop, fn_004a2db0), IroDump, IroJump, IroSubable, IroBitVect, IroUtil 54/55,
IroEval 13/14, IroVars 21/23, IroPropagate 8/9, IroFlowgraph 5/6, IroRangePropagation 4/5, IroEmptyLoop 6/7, IroLoop
29/33, IroExprRegeneration 18/21 (new unit: the upper half of the old IroLoop range), IroTransform 26/31, IROUseDef
14/24, IroPointerAnalysis 180/188. Back end: StrengthReduction 17/18, Propagation 3/4, COptimizer 10/10, Alias 9/14,
StructMoves 7/10, LoopOptimization 14/18 (rows renamed to Pro7: deleteload, computeliveonexit, optimizeif, ...).
Scheduler machine models 601, 603, 603e, 604, 750, 821 are Pro7's files (tables from the original's bytes, statics
`#define`d to `m603e_latency`-style row names) and link from source; 7400 keeps its scalar globals while `m7400_can_issue`
is lifted (Pro7's shape reaches 99.1%: one spill slot, `[esp+8]` for `[esp+4]`); AltiVec (machine7450, whose data sits
in VectorArraysToRegs' range) is untouched. `MachineInfo` (Scheduler.h) replaces the repo's `Checker`.
Shared: IROLinear.nodetype and IRO_NonAssignmentOp are `ENodeType`; IroLoop.h has `IROLoopMemRef`;
CompilerTools.h `CTool_EndianReadWord32` (inline); CInt64.h Pro7's helpers; objects.h `Inline_IsObjectData*`.

Register classes are Pro7's `RegClass` (Registers.h; RegClass_SPR..RegClass_GPR, RegClassMax) and the virtual register
counters are `used_virtual_registers[RegClassMax]` (2.6 declares it `long`: an `int` array moved registers in Registers.c
and SFPE_PPC_EABI.c); InlineAsmPPC's operand classes are `AsmRegClass`. CFunc.h has Pro7's `LoopInfo`. Typing
`Type.type` as Pro7's signed `TypeType` is not neutral (October 16): it must cover every Type-prefix struct's kind
field, retires the `(SInt8)` casts on `->type`, and costs functions that copy the kind into a `UInt8` local until those
locals are `TypeType` too.

Known blockers: GeneratePossibleCondAss' frame has two locals we lack;
StructMoves' emit_pair_copy_loop and load_small_block_into_reg_pair (GetStep is not Pro7's step computation). Relink:
IroCSE, IroLinearForm, IroMalloc are Matching but not relinkable (statics emitted under other names, COMMON .bss).

Next: the remaining register-only near-misses (decl_order, first-use order); the blockers above; IroLinearForm's
invented names; alias `#define`s in CExpr.h and CClass.h; template types into templates.h; IroDump's .data.

## Review round Z (October 16, 2026): the 14 non-linking units

InstrSelection, CodeGen, PCodeInfo, Peephole, StackFrame, CMachine, InlineAsmPPC, CExpr2, CFunc, CPrec,
IroPointerAnalysis, IroTransform, IroExprRegeneration, IroUnrollLoop, each reviewed and attacked (entries in
docs/review-2.6.md). CodeGen links (4019 exact, 2849 linked, 54.0% of bytes). Findings:

- **CodeGen's `saveheaperror` is `static heaperror_t saveheaperror = NULL;`**: an explicitly zero-initialized static is
  laid out below the zero templates of local aggregate initializers (`UInt32 masks[RegClassMax] = {0}` in
  callprofiler, exitprofiler, CodeGen_Generator); a tentative one above them. That replaced the `data_head` entry in
  2.6's config. move_assigned_argument was IDA-shaped (`int r`, `(r = reg) < 10`); Pro7's text is exact.
- **Sweeping Pro7's text through `fn.py variants`** for every function (Z_CG `work/pro7var.py`, `typevar.py`) found 15
  exact bodies in CMachine and about 20 in the code generator where IDA-shaped ones stood; CodeGen's `ERRTAIL` macro
  was Pro7's `CodeGen_EOLCheck` inlined.
- **CMach_GetPPCTypeAlign** is Pro7's body behind `CMACH_POWER_ALIGNMENT` (0): the Mac/AIX power alignment rule, which
  EABI lacks; 2.6 never inlines the function, as a body that size gives. Other near-misses short a register or never
  inlined may hide compiled-out target code too.
- **use_helper_function's `dont_inline`** is Pro7's dead `break;` after each case's `return` (counts toward the
  auto-inline limit).
- **Address-taken locals are laid out ascending in declaration order**: read the order off a call's `lea [esp+N]`
  pushes (minus the bytes already pushed), mapping each to its parameter. UnrollWhileLoop's `list, result1, result2,
  pattern` (a reviewer had result1/result2 swapped) took it 69.4 -> 89.1 with decl-order; its "two missing locals" were
  register pressure.
- **Statics left `extern` + bound** leave a unit's .bss empty with nothing flagging it while its functions are exact
  (IroPointerAnalysis, StackFrame, CFunc): define them `static` in the reference's order and check that the
  comparison's placements share one base, which also proves or disproves an end-of-file `#pragma pack(2)`.
- **COMMONs no source defines** compile silently (`cgdispatch`, `InlineAsm_labelref`) and only fail once the unit
  links. `next_varnumber` is Pro7's `localcount` (hash 1917 fits), defined in CFunc.c. `interrupt_routine_flags` does
  not fit hash order though Wii's public matches the referencing functions: open.
- **Assert line numbers on both sides of a Pro7 dead function** put it in the file (CFunc's Get/SetGlobalCompilerState
  and Pro7's `CFuncSave`).
- **`CError_Internal`'s file-name literal is not checked by the comparison**: IroPointerAnalysis' `CERROR_FILE`
  leftover pushed the wrong name and every function still counted exact.
- **An object's .data need not start 8-aligned** after a 12-byte table (IroTransform after IroSubable): relink.py's
  size check reports a phantom +4.
- **"Same similarity" is not "same code"**: compare the count of differing lines in `fn.py diff`.
- **A literal created before its function in a deferred unit** comes from a dead function generated there: 2.6's
  dead IroPointerAnalysis functions between IRO_AnalyzePointers and FindGlobalObjectAction differ from Pro7's (its
  .data literals are 24 bytes early from 0x5ad2ec).
- **Runtime difference, open**: `new T[n]` (T with a constructor, n variable) at -O0 or -O4,p gives one exception-table
  byte 06 for the original's 0a in the equivalent compiler only (not the relinked one): data in a non-linking unit
  (Z_FE `work/adv/rt/t10.cpp`, `try.sh`).
- Bugs of the original found: I8_ShiftLeftImmediate's signed `srawi` by `shift` (not `32 - shift`);
  rotatedefinedusedtest scans `instr`'s operands (Pro7 too); a non-calling interrupt routine restores the wrong slots
  with a sparse mask; the paired-single load passes `reg` for `vi->reg`; memoryoperand passes the Object to error 122;
  the psq side-effect test omits STU/STUX; Pro7's CFunc_GenerateLoop has its own bug, which 2.6 does not.

## Header round H2 (October 16, 2026)

The 109 headers without a family entry, in four families (entries at the end of docs/review-2.6.md: Optimizers,
Driver, Back end and assembler, Front end), each with an adversary. 4019 -> 4020 exact (Coerce_to_register).
- **Back end.** The opcodes are Pro7's `typedef enum Opcode` (with `PC_INVALID = -1`; OPCODE_MAX 469 is an
  enumerator before 2.6's PC_PENTRY/PC_PEXIT). Select_Gecko_Load_Store returns `Opcode`; gecko_load_store_register
  takes `Opcode op` but still needs a `short` copy; load_store_register and branch_decrement_always take Pro7's
  `Opcode opcode`, which is what made Coerce_to_register exact (its reload came from that parameter, not from
  Operand's `SInt32 immOffset`). makepcode/emitpcode stay `short op` (Pro7's `Opcode` costs vector_intrinsic_abss).
  Untried: InstrSelection's statics fp_binary_operator, binary_immediate, or_xor_immediate with `Opcode`.
- The IA* types live in Pro7's InlineAsm.h; the 1.x EncodedOperand/ParsedAsmInstruction are gone; CodeGen_Generator
  keeps a local `InlineAsm* ia` (Pro7's cast without it is not exact).
- The 315 `Intrinsic_NNN` are named after the runtime object each index gets (TODO). Intrinsics_ReInitRuntimeObjects
  checks the wrong names for indices 163/165/167 and 204-210 (the original's copy-paste slips; SetupRuntimeObjects
  has the right ones).
- Paired-single effect bits: 0x4000 is ps0 and 0x8000 is ps1 (CodeGen's `effect ^= 0x8000` on a psq load/store with
  W=1); insertspillcode spills both halves only for ps1. fPCodeFlag400000 (`fHasBaseReg`) marks rA|0 operands,
  which interfere with r0. SPR_GQR0/GQR1 are 912/913. StackFrame_has_vectors and pcode_has_vectors are Wii publics
  (not invented). Wii puts is_same_operand in PCodeInfo; 2.6 still has it in Scheduler.
- ElfFile needs pack(2) (`file_symbol` at +2; dropping it costs 37 functions); WeirdOperand's too. The other back-end
  pack(2) blocks were layout-neutral and are gone.
- **Front end.** CExpr2.h is merged into CExpr.h (Pro7's one header for CExpr, CExpr2, CExprConvMatch);
  `BinaryOperatorResult` is Pro7's `Conversion`; EMemberInfo is in enode.h. COpts is Pro7's `CompilerLinkerOptions`
  in CParser.h (its own pack(2) block, with `extern copts`); `sizeof` is pinned by the exact C_Compiler; copts
  +0x66/+0x67/+0xfd are real fields no instruction addresses. RelocationList is Pro7's OLinkList (`obj`; `addend` is
  Pro7's placeholder `somevalue`, TODO). `data_005830c8` is Pro7's `cerror_synchdata` (hash order). 2.6's
  `CError_FATAL` is the bare call, not Pro7's do/while(0) (that costs I8_gen_ADD and CExcept_ScanTryBlock).
  CExpr_SearchExprTree's `count` is `SInt32` (Pro7's `int`: 1.0 -> 0.46).
- IroPointerAnalysis.c includes IroPointerAnalysisADTs.c last, as Pro7 does. LocationSetSet_Remove/Add are exact
  only under some header sets: their source is probably not the original yet.
- **Optimizers.** IRO_IsPow2 takes `int*` and the BitVectors lengths are `SInt32` in 2.6 (measured; the ledger's
  blocked section). 2.6's IroDump keeps only 8 functions (IRO_DumpIntTree, IRO_DumpNode, IRO_SpellType,
  IRO_CleanupDump absent); IroDump.h still declares Pro7's four with Wii publics. Wii's IRO_IsSubableExpression and
  IRO_IsVectorTempCandidate are IrOptimizer .data function pointers: check a Wii public's section before taking it
  as the same entity. Not yet done: IroVars.c's private IndirectRecord `x26`; IroPointerAnalysis.c sets
  `IROLinear.evaluated` with 1/0.
- **Driver.** License_Checkout's `autoCheckin` is `int` (Pro7's guessed `Boolean` costs two functions);
  ExecuteLinker's `dropinflags` is Pro7's `SInt32` again (the old `UInt32` departure went stale); PProject's byte at
  0x2d was a pack(2) hole (removed); `PLinker.suppresswarn` is Pro7's PMachOLinker name. The remaining
  pref_structs `unknownXX` sit between byte members: real but unused fields. Wii decorates the Resources/TextUtils
  functions `__stdcall`, 2.6's are cdecl.
- **Tooling.** `tools/rename.py` misses a type name used as a member's type inside a struct body (`RelocationList*
  links;` in CPrec.c's StaticData, `COpts opts;` in CPrep.c). Fixed the same day (mw/renaming.py: a name followed by
  a declarator in a record body is the member's type and is renamed).
  `tools/includes.py fix` gives up when a type is also forward-declared in common.h, and `includes.py add` does not
  handle undeclared enumerators.
