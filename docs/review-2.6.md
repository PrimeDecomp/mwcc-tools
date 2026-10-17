# Rock-solid review of 2.6's units

Matching is a means: a unit that compiles byte-identical can still carry stand-in types, invented names, workarounds
that only happen to compile the same, or a wrong reading in a function the bytes cannot catch. This list records units
re-derived from first principles (the original's disassembly, Pro7, gc1, Wii's CodeView, the SDK) by a reviewer, then
attacked by a second, adversarial reviewer whose job was to break the claim. Only linking units are reviewed.

## Grades

- **SOLID**: every item of the bar below holds, with evidence; no fakematch, no invented name.
- **SOLID WITH TODOS**: everything holds except listed, justified TODOs (an invented name with nothing to take one
  from; a fakematch shown necessary, with what the original evidently did). Good for now: TODOs are revisited after
  the decompilation reaches 100%.
- **SOLID PENDING HEADERS** (or **SOLID WITH TODOS, PENDING HEADERS**): the unit itself is solid (with its TODOs), but a
  header it relies on has open problems or is not reviewed.
- **NOT SOLID**: something in the bar fails; the entry says what.

## The bar

1. **Boundary**: every function belongs to the unit and none is missing (Wii's module table, the Pro7 file's list and
   order, asserts' file names, the 8-aligned .data/.bss cut); dead functions whose data the original keeps are present.
2. **Flags**: the compiler, `-inline auto,deferred` (iff the functions descend in address) and `-sym` are justified.
3. **Functions**: every departure from the reference's spelling is a 2.6 change confirmed in the disassembly, or a
   shape MWCC needs (the natural spelling tried and failing). Every unusual construct had its natural spelling tried
   (`fn.py variants`). A spelling that is plausibly what Metrowerks wrote and not obscene (an integer cast for
   pointer arithmetic, `(T*)((long)p + n)`, their house idiom; a chained assignment; a local copy) counts as the
   original's shape. A *fakematch* is a construct no one would plausibly have written (a `volatile` read, a dummy
   local, a parameter typed as another pointer and cast back) kept only because nothing else matches; it stays with a
   TODO, which is acceptable until the decompilation reaches 100%. An assignment nothing reads is a judgement call:
   the original's shape when context gives a plausible story (debug code compiled out, a use Pro7/gc1 has that 2.6
   dropped, an oversight), a fakematch when only register placement explains it.
4. **Behaviour**: checked against the original's pseudocode where not straight from a reference: constants, offsets,
   members, error ids, literals. Bugs in the original are kept and noted; bugs of ours fixed.
5. **Names**: Pro7/Wii/`docs/names-2.6.md`; invented ones TODO-marked.
6. **Types**: Pro7's/the SDK's types and members; no stand-ins, placeholder fields or offset macros.
7. **Data**: .data/.bss byte-identical (the relink), statics `static`, literals at their use, no stale bindings.
8. **Header and file top**: the header is the unit's API (each declaration used elsewhere), variables `extern`; the
   file top holds its own structs, statics in 2.6's order, prototypes only where a use precedes the definition.
9. **Hygiene**: braces, `TRUE`/`FALSE`, `//` comments, clang-format and clang clean, no stale comments.
10. **Headers relied on**: each project header the unit includes, and what it uses from it, checked; problems are
    handed to that header's own review.

## Summary

| Unit | Grade | Open |
|---|---|---|
| src/optimizer/LoadDeletion.c | SOLID | (its headers are done: copy propagation, PCode.h) |
| src/driver/CLToolExec.c | SOLID | (its headers are done: `File`, `Target` are Pro7's) |
| src/backend/Switch.c | SOLID WITH TODOS | `SwitchInfo.exprtype`, `createIndirect`'s parameters: invented names (its headers are done) |
| src/backend/PCodeAssembly.c | SOLID WITH TODOS | invented `WeirdOperand` members (the flag is `fPCodeFlag80000` now; its headers are done) |
| src/frontend/CTemplateTools.c | SOLID WITH TODOS | (headers done: Pro7's template types and `TypeType`; templates.h's invented names for Pro7's placeholders) |
| src/driver/CLFiles.c | SOLID | (its headers are done: Memory.h's Handle, MemUtils.h, CLIO.h; FixTextHandle is Pro7's `short`) |
| src/driver/CLTarg.c | SOLID | (its headers are done: the common set; CLTarg.h/CLFiles.h ... are Pro7's records) |
| src/driver/CLSegs.c | SOLID | (its headers are done: the common set) |
| src/driver/CLOverlays.c | SOLID | (its headers are done: the common set) |
| src/driver/CLAccessPaths.c | SOLID | (its headers are done: the common set, Files.h, Generic.h, MacSpecs.h, MsDos.h) |
| src/driver/CLPrefs.c | SOLID | (its headers are done: the common set; CLPrefs.h is Pro7's opaque PrefPanel) |
| src/backend/Scheduler.c | SOLID WITH TODOS | invented DGNode and MachineInfo member names (no source exists); headers' open items above. |
| src/backend/MachineSimulation601.c | SOLID WITH TODOS | invented MachineInfo/OpcodeInfo member names (TODO in Scheduler.h/PCodeInfo.h; no source has names) |
| src/backend/MachineSimulation603.c | SOLID WITH TODOS | invented MachineInfo/OpcodeInfo member names (TODO in Scheduler.h/PCodeInfo.h; no source has names) |
| src/backend/MachineSimulation603e.c | SOLID WITH TODOS | invented MachineInfo/OpcodeInfo member names (TODO in Scheduler.h/PCodeInfo.h; no source has names) |
| src/backend/MachineSimulation604.c | SOLID WITH TODOS | invented MachineInfo/OpcodeInfo member names (TODO in Scheduler.h/PCodeInfo.h; no source has names) |
| src/backend/MachineSimulation750.c | SOLID WITH TODOS | invented MachineInfo/OpcodeInfo member names (TODO in Scheduler.h/PCodeInfo.h; no source has names) |
| src/backend/MachineSimulation821.c | SOLID WITH TODOS | invented MachineInfo/OpcodeInfo member names (TODO in Scheduler.h/PCodeInfo.h; no source has names) |
| src/driver/MsDos.c | SOLID WITH TODOS | the 32 KB zero block's owner (MsDos' unused static or a data-only object: no evidence either way); OS_NewHandle's TODO; headers above. |
| src/driver/MacSpecs.c | SOLID WITH TODOS | invented OS_ShortenSpec/OS_CanonNameCase (TODO, no source); its headers are done |
| src/driver/Generic.c | SOLID | (its headers are done: StringExtras.h's `strcatn` takes `const char*`, win32.h rewritten) |
| src/driver/LowMem.c | SOLID | none. |
| src/driver/FileHandles.c | SOLID | (its headers are done: `Ptr` is common.h's, OS_LockFileHandle takes Pro7's `Ptr*`) |
| src/backend/Registers.c | SOLID | (its headers are done: VarInfo in objects.h, ALLOC_* in RegisterInfo.h) |
| src/backend/RegisterInfo.c | SOLID WITH TODOS | the RegInfo TODO names (no Pro7 or Wii source to take them from). |
| src/backend/LiveVariables.c | SOLID WITH TODOS | RegisterInfo.h's RegInfo names are TODO-invented; LiveVariables.h, BitVectors.h, PCode.h done |
| src/backend/InterferenceGraph.c | SOLID WITH TODOS | the IGNode member names (no source names them). |
| src/backend/Coloring.c | SOLID WITH TODOS | InterferenceGraph.h's IGNode member names are TODO (Pro7 placeholders); its headers are done |
| src/backend/BitVectors.c | SOLID WITH TODOS | the invented type and static names (no source). ## Files changed (adversarial pass) |
| src/driver/Memory.c | SOLID | (its headers are done: Pro7's `Handle` replaces StorageHandle everywhere) |
| src/driver/TextUtils.c | SOLID | (its headers are done: Resources.h's GetResource returns Pro7's `Handle`) |
| src/driver/StringUtils.c | SOLID | (its headers are done: Pro7's `Handle`; StringUtils.h's mprintf is Pro7's) |
| src/driver/StringExtras.c | SOLID | none. |
| src/driver/MacFileTypes.c | SOLID | (its headers are done: OSFileTypeMappings is Pro7's, `mactype`) |
| src/driver/Files.c | SOLID | none. |
| src/driver/CLDependencies.c | SOLID WITH TODOS | the `syspath` parameter name (TODO); header problems (CPrep.h PFile, cmdline.h tags, CLIO.h macro, Memory.h StorageHandle, the ... |
| src/driver/CLFileOps.c | SOLID WITH TODOS | the invented name set_bytes (no source names it); header problems (Files.h FSpDelete, CLIO.h macro, cmdline.h tags, Memory.h StorageHandle). |
| src/driver/CLWriteObjectFile.c | SOLID | (its headers are done: CLIO.h macro, StorageHandle and cmdline.h tags all gone) |
| src/driver/CLIncludeFileCache.c | SOLID | (its headers are done: Pro7's `Handle`) |
| src/driver/CLLoadAndCache.c | SOLID | (its headers are done: Pro7's `Handle`, CopyFileText's `char**, SInt32*`) |
| src/driver/CLBrowser.c | SOLID WITH TODOS | the S43/S49/T53 names; Memory.h's StorageHandle. |
| src/backend/PCode.c | SOLID WITH TODOS | the makepcblock comma TODO. |
| src/backend/PCodeUtilities.c | SOLID WITH TODOS | COpts' remaining invented member names (TODO, CParser.h); Registers.h done |
| src/backend/PCodeListing.c | SOLID WITH TODOS | the literal-reconstructed dead bodies (no source to take them from). |
| src/backend/PPCError.c | SOLID | none. |
| src/optimizer/GlobalOptimizer.c | SOLID | (its headers are done: the pass headers reviewed in the Optimizers family) |
| src/optimizer/COptimizer.c | SOLID | (its headers are done: Pro7's CLabel.stmt, ExceptionAction, ENode; COptimizer.h) |
| src/optimizer/IroBitVect.c | SOLID | none. |
| src/optimizer/IroSubable.c | SOLID WITH TODOS | IroLinearForm.h's remaining TODO names for Pro7's placeholders (no source names) |
| src/optimizer/IroJump.c | SOLID WITH TODOS | IroLinearForm.h/IroFlowgraph.h TODO names only; the CException stand-in is gone |
| src/optimizer/IroFlowgraph.c | SOLID WITH TODOS | the invented and placeholder IRONode member names (TODO); the IRONodes private copy in IroExprRegeneration.c; CFunc.h's CLabel `target` ... |
| src/optimizer/IrOptimizer.c | SOLID WITH TODOS | IAOpnd_3 is now IAOpnd_GlobalObj (TODO-invented, InlineAsm.h) |
| src/optimizer/IroMalloc.c | SOLID WITH TODOS |  |
| src/optimizer/IroEmptyLoop.c | SOLID WITH TODOS | the EmptyLoop register shapes are unproven as the original's (TODO comment), and the IroLoop.h placeholders. |
| src/driver/CLMain.c | SOLID WITH TODOS | the `(void)0` TODO; `CLState` and `OptsCmdLineState_*` are TODO-invented names |
| src/driver/CLIO.c | SOLID WITH TODOS | invented `TextBuffer`/`pagePrompt`/helper names (no source has them), CLIO.h's `CLIO_OutOfMemory` macro (needs edits in three other ... |
| src/driver/CLErrors.c | SOLID | (its headers are done: CLIO.h's items, StringUtils.h's mprintf) |
| src/driver/CLStaticMain.c | SOLID | (its headers are done: cc-eabi-ppc-mw.h's OSType, the Driver family) |
| src/driver/CLProj.c | SOLID | (its headers are done: CLProj.h's includes trimmed, CLIO.h's items gone) |
| src/driver/MemUtils.c | SOLID | none. |
| src/driver/ErrMgr.c | SOLID | none. |
| src/driver/CLPlugins.c | SOLID WITH TODOS | the `#pragma pack(2)`/static-order TODO (the .bss). |
| src/driver/CLPluginRequests.cpp | SOLID | (its headers are done: Pro7's context classes, UCBGetFileText's types) |
| src/driver/CWPluginsPrivate.cpp | SOLID | (its headers are done: CWPluginsPrivate.h takes the SDK's parameter types) |
| src/driver/CLLicenses.c | SOLID WITH TODOS | invented names and the unverified FLEXlm code-handle name. The code was not changed for any of these. |
| src/driver/LicenseImports.c | SOLID WITH TODOS | the invented unit name and LPCODE_HANDLE (FLEXlm lmpolicy.h's real type name unverified). |
| src/driver/CWSecretPluginCallbacks.cpp | SOLID |  |
| src/driver/ResourceStrings.c | SOLID | none. |
| src/backend/InlineAsm.c | SOLID WITH TODOS | the GetObjectOffset fakematch, the invented `isbaseclassmember`, InlineAsmPPC.h's placeholder members. |
| src/backend/InlineAsmRegisters.c | SOLID | (its headers are done: InlineAsm.h, common.h's dead typedefs removed) |
| src/backend/InlineAsmRegistersPPC.c | SOLID WITH TODOS | the three invented names (no source has them). |
| src/backend/InlineAsmMnemonicsPPC.c | SOLID WITH TODOS | IAMnemonic's two TODO-invented member names (Pro7: x4/x10) |
| src/backend/GCCInlineAsm.c | SOLID WITH TODOS | InlineAsm.h's remaining TODO-invented names (gccreg, gccimm, IAOpnd_*) |
| src/backend/FuncLevelAsmPPC.c | SOLID WITH TODOS | InlineAsm.h's TODO names (IAEntryPoint's overlay names, IAOpnd_*) |
| src/optimizer/Alias.c | SOLID WITH TODOS | - The invented names of the 2.6-only statics (nothing can name them). - The `goto` and `skip` TODOs. - The invented members in ... |
| src/optimizer/StructMoves.c | SOLID WITH TODOS | - The `int w = width` workaround and the invented helper names. - The helpers' shape is inferred from 2.6's parameter copies. Wii has ... |
| src/backend/SFPE_PPC_EABI.c | SOLID WITH TODOS | the invented names; this is 2.6's own unit with gc1's helper split, so there is no stronger reference. |
| src/backend/MachineSimulation7400.c | SOLID WITH TODOS | Scheduler.h's invented `MachineInfo` member names (TODO; no source has names) |
| src/backend/MachineSimulationAltiVec.c | SOLID WITH TODOS | the invented static names, the loop-bound shape and Scheduler.h. |
| src/optimizer/CopyPropagation.c | SOLID WITH TODOS | invented names cp_mask and PropInfo members (no source to take them from). |
| src/optimizer/AddPropagation.c | SOLID WITH TODOS | none in the unit beyond the header placeholders. |
| src/optimizer/UseDefChains.c | SOLID WITH TODOS | invented updateusedefset and UseDefInfo member names. |
| src/optimizer/ValueNumbering.c | SOLID WITH TODOS | RegValue.copyreg invented. No named constant for SPR 912 exists in the repo. |
| src/optimizer/StrengthReduction.c | SOLID WITH TODOS | invented member names (no source). |
| src/optimizer/LoopDetection.c | SOLID WITH TODOS | invented Loop member names (no source). |
| src/backend/BE_symbol.c | SOLID WITH TODOS | the invented names above (TODO-marked). |
| src/backend/ELF_Endian.c | SOLID WITH TODOS | TODOs 1-4 above (1 is a fakematch proven necessary), and the invented static names. |
| src/backend/Exceptions.c | SOLID WITH TODOS | the invented member names above. |
| src/backend/DumpIR.c | SOLID WITH TODOS | the `unknown_statics[8]` placeholder; the TD case label. |
| src/backend/OpcodeInfo.c | SOLID WITH TODOS | PCode.h's TODO flag names (`fHasBaseReg`) |
| src/backend/ScanFloatX86.c | SOLID | none. |
| src/backend/Intrinsics.c | SOLID WITH TODOS | three TODOs, all justified. (1) `#pragma dont_inline` on promotable_types_are_equal. Whatever kept 2.6's copy out of line is unknown. No ... |
| src/optimizer/IroLinearForm.c | SOLID WITH TODOS | MarkSubExpr (TODO); the headers' TODO names for Pro7 placeholders |
| src/optimizer/IROUseDef.c | SOLID WITH TODOS | TODO names in IROUseDef.h/IroVars.h/IroFlowgraph.h/InlineAsm.h; IT_17 exists now |
| src/optimizer/IroRangePropagation.c | SOLID WITH TODOS | IroLinearForm.h's `range` (TODO name for Pro7's x16) |
| src/optimizer/IroDump.c | SOLID WITH TODOS | TODO names in IroFlowgraph.h/IroVars.h/IroCSE.h only |
| src/optimizer/CIRTransform.c | SOLID WITH TODOS | `StrangeRuntimeFunction` (Pro7's decompiler name, TODO); its headers are done |
| src/optimizer/IroUtil.c | SOLID WITH TODOS | the invented static name `FindFirst` and IROAddrRecord.x16 (TODO). |
| src/optimizer/IroVars.c | SOLID WITH TODOS | the CheckAddress `hi` workaround (TODO). |
| src/optimizer/IroCSE.c | SOLID WITH TODOS | invented field names only (TODO). |
| src/optimizer/IroEval.c | SOLID WITH TODOS | invented ReduceInfo field names (TODO); CMachine.h's `volatile double` stand-in (header). |
| src/backend/ObjGen_PPC_EABI.c | SOLID WITH TODOS | - two fakematches (volatile, void*); - invented names with no source; - two unnamed only-cleared statics; - the header problems above ... |
| src/driver/ELF_Lib_Import.c | SOLID WITH TODOS | the optimization_level pragma's origin (one object either way, below); the LinkInfo (`unknown000`/`unknown291` placeholders) and ... |
| src/backend/DWARF.c | SOLID WITH TODOS | the two fakematch TODOs (emit_dwarf_ref's switch lvalue, newDWARF_file's `void*` parameter); invented names with nothing to take them ... |
| src/driver/Resources.c | SOLID WITH TODOS | the TODO pun; Ty70..Ty82 placeholder struct names (no source for real ones) |
| src/driver/CodeGenOptPPC.c | SOLID WITH TODOS | the invented PProject field name; the pref_structs placeholders. |
| src/driver/OptimizerHelpers.c | SOLID WITH TODOS | the two TODO shapes above; PR_* would belong in parser.h (ParserHelpers-cc.c keeps a private `#define PR_UNSET 0`). |
| src/driver/TargetOptimizerHelpers-eabi-ppc.c | SOLID WITH TODOS | pref_structs.h's PBackEnd `unknownXX` fields (TODO: real, never set or read; no source names them). |
| src/driver/StdTargetWarningHelpers-cc.c | SOLID | (its headers are done: Pragma and PR_* are Pro7's parser.h) |
| src/driver/WarningHelpers.c | SOLID WITH TODOS | the SetWarningFlags TODO shape. |
| src/frontend/CClass.c | SOLID WITH TODOS | the TODOs (3 invented field names, the reset-less pack); headers left only TODO names |
| src/frontend/CABI.c | SOLID WITH TODOS | the reset-less pack TODO; headers left only TODO names (CABIDestroy0..3, DeclInfo) |
| src/frontend/CRTTI.c | SOLID WITH TODOS | the TODO-marked invented names; headers left only TODO names |
| src/driver/ParserFace.c | SOLID |  |
| src/driver/ParserHelpers.c | SOLID WITH TODOS | the volatile/`t` fakematch. |
| src/driver/ParserHelpers-cc.c | SOLID | (its headers are done: the parser library's shared items) |
| src/driver/ParserGlue-eabi-ppc-cc.c | SOLID WITH TODOS | pref_structs.h's EABI panels' `unknownXX` fields (TODO: real, never set or read; no source names them). |
| src/driver/StaticParserGlue.c | SOLID | (its headers are done: StaticParserGlue.h) |
| src/driver/CWParserPluginsPrivate.cpp | SOLID | (its headers are done: CWParserCallbacks typed as Pro7's plugin.h) |
| src/driver/CLParserCallbacks_v1.cpp | SOLID WITH TODOS | the UCB functions' C vs C++ linkage (Wii says C++; not observable in 2.6). |
| src/driver/Projects.c | SOLID | (its headers are done: Projects.h Pro7's, CWPluginsPrivate.h's SDK types) |
| src/driver/ToolHelpers.c | SOLID WITH TODOS | OutputOrdering_None/_After/_Before are TODO-invented names (Pro7 placeholders) |
| src/driver/ToolHelpers-cc.c | SOLID WITH TODOS | the OutputOrdering_* names it uses are TODO-invented (ToolHelpers.h) |
| src/driver/IO.c | SOLID WITH TODOS | the two invented local names (`vdriver`, `vparser`). |
| src/frontend/CExpr.c | SOLID WITH TODOS | the ParseNameResultExpr fakematch, EMemberInfo's TODO names; tokens.h's TK_* stay `#define`s (Pro7's enum: blocked, ledger). |
| src/frontend/CTemplateClass.c | SOLID WITH TODOS | the pqual fakematch; headers left only TODO names (templates.h, DeclInfo) |
| src/frontend/CTemplateFunc.c | SOLID WITH TODOS | the invented parameter name. |
| src/frontend/CTemplateNew.c | SOLID WITH TODOS | the ParseTemplArgs fakematch, invented names. |
| src/frontend/CParser.c | SOLID WITH TODOS | the two TODO workarounds above and the invented helper name; the DeclInfo/NameResult (was CScopeParseResult) invented member names, TODO-marked. |
| src/frontend/CPreprocess.c | SOLID WITH TODOS | invented names (no source to take them from). |
| src/frontend/CBrowse.c | SOLID WITH TODOS | the TODO-named DeclInfo/Macro members (Pro7 has only offset names for them) |
| src/frontend/CScope.c | SOLID WITH TODOS | the CScope_AddObject TODO; the invented names above (no source to take them from). |
| src/frontend/CException.c | SOLID WITH TODOS | headers left only TODO names (DeclInfo, CInlineBackendOptions, CSOMStub.kind, COpts) |
| src/driver/Option.c | SOLID WITH TODOS | the three invented local names and Pro7's placeholder enum names. |
| src/driver/Parameter.c | SOLID WITH TODOS | two invented local names. |
| src/driver/Help.c | SOLID WITH TODOS | `currentArg` (invented). |
| src/driver/Utils.c | SOLID WITH TODOS | five invented dead-local names. |
| src/driver/ParserErrors.c | SOLID | (its headers are done: IO.h's ShowVersion/ShowTextHandle, `helpFlags` SInt32) |
| src/driver/Targets.c | SOLID | (its headers are done: IO.h's ShowVersion/ShowTextHandle, `helpFlags` SInt32) |
| src/driver/Arguments.c | SOLID WITH TODOS | PtrList (invented), Pro7's COMPAT_n placeholders. |
| src/frontend/CInit.c | SOLID WITH TODOS | the 4 `size =` workarounds, the invented member names; headers left only TODO names |
| src/frontend/CExprConvMatch.c | SOLID WITH TODOS | the invented names; ArgMatch/ComparisonValues TODO names (CExpr.h) |
| src/frontend/CError.c | SOLID WITH TODOS | the two invented names and the pack(2) TODO (headers done) |
| src/frontend/CInt64.c | SOLID WITH TODOS | the four invented names and the `float one` TODO. |
| src/frontend/CompilerTools.c | SOLID WITH TODOS | the three TODO workarounds (headers done: COS_* in COSToolsCLT.h) |
| src/frontend/CMangler.c | SOLID WITH TODOS | the void* cast TODO (headers done: types.h has Pro7's IT_17..IT_24) |
| src/frontend/Unmangle.c | SOLID | none. |
| src/driver/CLDropinCallbacks_V10.cpp | SOLID WITH TODOS | the UCBSecretAttachHandle cast (TODO). |
| src/driver/CLCompilerLinkerDropin_V10.cpp | SOLID WITH TODOS | the size spelling noted above (a `TODO`-level guess, not a fakematch). |
| src/driver/DropInCompilerLinkerPrivate.cpp | SOLID WITH TODOS | the invented `CWTargetInfo_V8` (no source names it). |
| src/driver/CCompiler.c | SOLID WITH TODOS | `cc_tolower` (TODO; needs MSL's inline `tolower` in include/libc/ctype.h). |
| src/driver/cc-eabi-ppc.c | SOLID | none. |
| src/driver/cc-eabi-ppc-mw.c | SOLID | none. |
| src/driver/libimp-eabi-ppc.c | SOLID | none. |
| src/driver/ClientGlue.c | SOLID | none. |
| src/driver/COSToolsCLT.c | SOLID | none. |
| src/driver/DateTimeUtils.c | SOLID WITH TODOS | invented names for the dead date formatters; the date tables' owner is inferred. |
| src/driver/LinkErrorsPPC_EABI.c | SOLID WITH TODOS | the invented names (and LinkInfo's, in ELF_Lib_Import.h). |
| src/frontend/CObjC.c | SOLID WITH TODOS | the GetMethodFuncType cast (TODO). |
| src/frontend/CObjCModern.c | SOLID | (its headers are done: CObjC.h, objc.h, CExpr.h) |
| src/frontend/CSOM.c | SOLID WITH TODOS | the SetNibble body (TODO), the three invented field-name groups (no source has names) |
| src/backend/TOC.c | SOLID WITH TODOS (two register-inexact functions, does not link) | the two inexact functions (does not link); Optimize64bitMath's GetSizeSkip (TODO); invented names |
| src/backend/Operands.c | NOT SOLID (three fakematch TODOs, two of them a likely stand-in field type; two register-inexact functions, does not link) | the two I8 functions (the unit does not link); the three fakematch TODOs (two tied to Operand's integer type); one_point_zero_single's ... |
| src/backend/BE_elf.c | SOLID WITH TODOS | BackEnd_WriteElf 99.5 and InitElfFile 91.7 (does not link); `sym off`; invented names |
| src/optimizer/FunctionCalls.c | SOLID WITH TODOS | analyze_arguments register swap (does not link); the .bss 20-vs-32 question |
| src/optimizer/ConstantPropagation.c | SOLID WITH TODOS | propagateconstantstoblock 98.9 (does not link); invented names; `loadSize` TODO |
| src/optimizer/IroPropagate.c | SOLID WITH TODOS | IRO_CopyAndConstantPropagation 99.7 (one spill reload) keeps the unit from linking |
| src/optimizer/IroLoop.c | SOLID WITH TODOS | - RearrangeInvarianceInAddressExpr 99.5 and ExtractLoopInfo 93.1 (register/order only), which keep the unit from linking. - The `div` ... |
| src/backend/SpillCode.c | SOLID WITH TODOS | insertspillcode 91.1 (registers only, re-diffed: two-register swaps in the marking pass and the second loop; tried: declaration orders ... |
| src/optimizer/VectorArraysToRegs.c | SOLID WITH TODOS | computeaddilist 94.8 (registers only, re-diffed: the block index and `reg` in ebp where 2.6 has ebx/ecx; tried: block-local ... |
| src/optimizer/CodeMotion.c | SOLID WITH TODOS | isloopinvariant 97.3 (registers only; does not link) |
| src/optimizer/LoopOptimization.c | SOLID WITH TODOS | unrollloopconditional 88.0 (2.6 keeps `firstInstr` in the body-copy walker's register and enters the loop at its step; adversarial ... |
| src/frontend/CPrepTokenizer.c | SOLID WITH TODOS | tquot's stack slot (register/frame only; does not link) |
| src/frontend/CPrep.c | NOT SOLID | PragmaOnce and pragma_on_off_reset (the unit's only link blockers; also tried this round: pragma_on_off_reset's parameter ... |
| src/frontend/CInline.c | SOLID WITH TODOS | CInline_UnpackIFunctionData 92.2 (register-only) keeps it from linking; the TODO workarounds |
| src/frontend/CDecl.c | NOT SOLID | CDecl_AddFunctionMember 99.3 (one `tclass->flags = tclass->flags / CLASS_ABSTRACT`; with both flag updates `/=`, as 2.6 has, MWCC spills ... |
| src/backend/StackFrame.c | SOLID WITH TODOS (four register-inexact functions, does not link) | the four register-only near-misses; `32L - i`, pack(2), invented names; `interrupt_routine_flags` hash order |
| src/backend/CMachine.c | SOLID WITH TODOS (two register-inexact functions, does not link) | two register near-misses; the GetPPCTypeAlign constant, per-function `sym off`, invented names |
| src/backend/InlineAsmPPC.c | NOT SOLID | IAExpr_0..10; the 1.x stand-ins in the header; the flag3..5 cast TODO; the two near-misses. |
| src/backend/InstrSelection.c | NOT SOLID | does not link: I8_gen_DIV_MOD 96.2, I8_ShiftRightImmediate 89.1, gen_CONDASS 98.4, gen_COND 78.3, gen_TYPCON 99.3, gen_ASS 83.5, gen_AND ... |
| src/backend/CodeGen.c | SOLID WITH TODOS | invented names (TODO), types.h's TYPE_IS_GPR_PAIR/TYPE_IS_SOFT_FLOAT among them. |
| src/backend/PCodeInfo.c | SOLID WITH TODOS | all exact and linking (NM5); TODO names |
| src/backend/Peephole.c | NOT SOLID | adjustforward 68.1 and ADDI_LU_SU 98.0 (this round's ADDI_LU_SU tries in the ledger); the `PCode* t` workaround; clear_base_register; ... |
| src/frontend/CExpr2.c | SOLID WITH TODOS (does not link: 2 register-only near-misses) | scannew (86.5) and user_assign_check (97.5) register near-misses; the four TODOs |
| src/frontend/CFunc.c | SOLID WITH TODOS (does not link: 2 register-only near-misses) | statement (73.6) and DestructorReturnTransform (97.3) near-misses; the DestructorCleanup TODO; dead bodies Pro7's |
| src/frontend/CPrec.c | SOLID WITH TODOS (does not link: 4 near-misses) | the four near-misses; the three TODOs; `macrohashtable` belongs in CPrep.c |
| src/optimizer/IroPointerAnalysis.c | NOT SOLID | the .data literal order (24 bytes from 0x5ad2ec): "!changed" (KillAllAliasingExtParamLocsAction's 2.6 assert, which Pro7 lacks) and "set ... |
| src/optimizer/IroTransform.c | SOLID WITH TODOS | DoTransformations24 and 11 (registers; does not link); InsertTypeConversion (TODO) |
| src/optimizer/IroExprRegeneration.c | SOLID WITH TODOS | the four register-level near-misses; GeneratePossibleCondAss' head (equivalent) |
| src/optimizer/IroUnrollLoop.c | SOLID WITH TODOS | UnrollWhileLoop 89.1 (register only; does not link) |

## Bugs in the original

Kept as they are (the matching build reproduces them); collected from the entries below for a fix pass once the
compiler builds from source alone. "Pro7 too" marks bugs Ninji's Pro7 has as well. Grouped by effect.

**Visible behaviour**
- CLIO.c, ScanFloatX86.c: `errno` is read one word off. 2.6 was compiled against Pro 6's MSL headers (whose
  `_ThreadLocalData` starts with a `next` link) and linked with Pro 5's MSL library (errno first), so the compiler's
  own `errno` reads the library's `random_next`. "Write error on output (errno=%ld)" (CLStr10) and CLStr8 print the
  wrong number; ScanFloat's out-of-range check (`errno = 0; strtod(); errno != 0`) never fires and resets the `rand()`
  seed instead. Fix: build against the MSL actually linked.
- CLLicenses.c: License_Checkin ignores every cookie that is not positive, and License_AutoCheckin only passes negative
  ones, so automatic check-in never checks anything in (and allocate_license_slot's "check in when full" frees no slot).
- CLDependencies.c: FindDepFile searches `gTarg->incls` instead of the `deps->incls` it was given; AddDepFile ignores
  its dependencyType; the dependency list's quoting is `spaces ? "" : ""` (names with spaces get escaped, never quoted).
- MsDos.c: OS_IsFullPath passes `strlen(cygdrive_prefix) == 0` (0 or 1) as ustrncmp's length; OS_MacToTime's leap-year
  test is `wYear % 4` alone (wrong for 2100); OS_ReadDir clamps a long alternate name with `len > 256` (to 255).
- CLFileOps.c: GetOutputFile's length cap is measured from the file name while the copy starts at `inname`;
  LinkProject leaves `fss` unset if a post-linker's target info switches outputType to 1.
- InlineAsmRegistersPPC.c: InlineAsm_InitializeRegistersPPC inserts each DCR with its number and CPU mask swapped, so
  every DCR name assembles to a wrong number (run under the original: `-proc 403`, `mfdcr r3,exisr` is DCR 0x80, with
  a bogus "not applicable to this processor" warning). PowerPC 40x inline assembly using DCR names is broken.
- Intrinsics.c: `vector_all_lt`'s type table has no NULL terminator (Pro7 too), so a `vec_all_lt` with argument types
  no entry matches walks into the next table and the compiler crashes (reproduced under wine: `-proc 7400 -vector on`,
  `vec_all_lt(vector float, vector signed int)`, page fault reading 0x119 at 0x4CEFDE).
- Alias.c: make_distinct_virtual_alias tests `IS_TYPE_CLASS` on the pointer type itself, so the class branch never
  runs (Wii has the same bug).
- LoopDetection.c: analyzeloop never clears `has_indexed_load`/`has_indexed_store` (Pro7 too).
- GlobalOptimizer.c: the second propagateconstants after arrays-to-registers runs only when `copts.debuglisting` is on
  (Pro7 too): turning on a listing changes the generated code.

**Memory errors (latent)**
- Buffer size 260 passed for 256-byte buffers: Generic.c's OS_NameSpecChangeExtension/SetExtension, CLIO.c's
  WriteHandleToFile/AppendHandleToFile, CLLicenses.c's License_Checkout (`sizeof(OSPathSpec.s)` for a 0x100 frame slot).
- IroBitVect.c: Bv_IsSubset tests `b->size < i`, so `i == b->size` reads one word past `b`.
- CLPlugins.c: GetToolVersionInfo allocates `strlen + 2` for the copyright string, room for only one (c) expansion.
- CLPluginRequests.cpp: SendDisassemblerRequest calls `Plugin_GetDropInName(linker)` when `linker` is NULL;
  SendInitOrTermRequest stores through `plugin->context` after setting it NULL for an unknown plugin type (Pro7 too).
- CLLicenses.c: License_Terminate clears the vendor code without freeing it.

**Harmless as built**
- BitVectors.c: bitvector_compress's free-list search never advances `scan` (an endless loop on a size mismatch), but
  nothing ever puts a vector on the list.
- IroEmptyLoop.c: `type != IROLinearIf || type != IROLinearIfNot` is always true (Pro7 too).
- CLFileOps.c: CompileFile clears `recordbrowseinfo` twice.
- Files.c: SetFPos calls `OS_MacToRef` before testing `fRefNum == 0`, and `npos > size` is an unsigned compare (Pro7 too).
- CLToolExec.c: a failed GetOutputFile/StoreObjectFile for a single file is ignored; an extra argv slot is
  reallocated (Pro7 too).

Fixed by 2.6 itself: Switch.c's I8_treecompare used `caseranges[end]` for the ENOTEQU arm in Pro7 (2.6: `[start]`).

## Units

### src/optimizer/LoadDeletion.c: SOLID (headers done October 16)
- Boundary: 4 functions at 0x5770e0-0x5771f0, Pro7's whole LoadDeletion.c in reverse; Wii's module agrees; .data
  0x5d1ac0 (8-aligned) to 0x5d1b06: three literals and the 30-byte record; no .bss (`deletedloads` is a COMMON).
- Flags: deferred (functions descend in address). `-sym on` not shown to matter here (the optimizer default).
- Functions: 4/4 exact. Departures: is_load's `!(a < b)` (Pro7's `>=` and four other spellings tried: 63-94.6%);
  the record's `mask` (a 2.6 change: CopyPropagation tests it); `"l%ld"` is Pro7's `"l%" PRId32`. No fakematch.
- Behaviour: no bugs in the original or ours. Record bytes re-read: mask 0x20000100 = `fSideEffects | fRecordBit`.
- Names: all Pro7's; Wii confirms `deletedeadloads`, `deletedloads`. Invented: none.
- Types: Pro7's `Candidate` and `Propagation` (were 1.x's `AddPropagationEntry`, `CopyPropConfig`), 2.6's `UInt32 mask`.
- Data: relinks byte-identical; `load_immediate_prop` static, its bindings deleted (2.0/2.0p1 keep it: deletedeadloads
  is lifted there).
- Header: Pro7's LoadDeletion.h.
- Headers relied on: Propagation.h, AddPropagation.h, CopyPropagation.h (the record and candidate types): still the
  1.x split and names (`CodeBits` for Pro7's `PropInfo`, `addPropagationBlockBits` for `propinfo`,
  `add_propagation_entry_count` for `number_of_candidates`; Pro7 keeps them all in CopyPropagation.h). PCode.h (flags).
- Open: the propagation headers.
- Adversarial review: five natural spellings of is_load tried (none matches), the record's bytes and xrefs re-read in
  IDA, the boundary's neighbours checked against Wii; nothing broke.

### src/driver/CLToolExec.c: SOLID (headers done October 16)
- Boundary: correct. 7 rows at 0x418910-0x419270 = Pro7 CLToolExec.c's 7 functions in the same order; Wii module
  CLToolExec has the same 7 .text contributions (publics only for AppendArgumentList, SetupTemporaries,
  DeleteTemporaries, ExecuteLinker, so the other three are static, as in Pro7). The asserts say "CLToolExec.c". .data
  0x5a1b68 (8-aligned)-0x5a1c1c holds only this unit's literals. No .bss. No dead functions.
- Flags: pro53 mapped to pro6 by GC_2_6's config (real pro53 gives 0/7). `-O4,p`, not deferred (addresses ascend).
  `-sym off` matters: with `-sym on` SetupLinkerCommandLine drops to 92.8 and ExecuteLinker to 98.8.
- Functions: 7/7 exact. Rewritten from Pro7 (the old body was gc1 text: invented names, magic numbers, `#define free
  xfree`, a do/while for Pro7's for). Departures from Pro7:
  - FreeArgumentList walks with an index `argv[x]` (Pro7 uses a `char** scan`). This is 2.6's shape: the
    disassembly reads `[ebx + ebp*4]` and increments ebp.
  - ExecuteLinker's `cname[256]` (Pro7: 64). A 2.6 change: the frame is 0x318 against 0x258.
  - ExecuteLinker's `UInt32 dropinflags` (Pro7: SInt32) was needed once (95.8 with SInt32). (H2_DRV adversary,
    October 2026) Pro7's SInt32 now compiles to a byte-identical object, so the parameter is Pro7's again.
  - Both memmoves take `(char*)((long)ptr + 2)` / `+ 3` (was `(int)`). Original's shape (plausible: Metrowerks' house
    idiom; graded so by the maintainer), with a TODO in the source listing what was tried, now spelled in
    Metrowerks' own idiom (`(T*)((long)p + n)`, as Pro7's IroMalloc writes it). 2.6 copies ptr and adds to the copy
    (`mov eax, esi; lea ecx, [eax+3]; add eax, 2`) and in the "mwc" branch adds 3 to a computed `ptr + 2` without
    folding: an integer conversion node is what IRO CSEs and does not reassociate. The adversary tried 40 more
    spellings: `ptr + 2L/2u/(short)2/(char)2`, `2 + ptr`, `ptr - -2`, `&*ptr`, `&ptr[0] + 2`, `register` ptr, an
    offset macro, `cname + (ptr - cname) + 2`, `(char*)(void*)ptr`, `(unsigned char*)` both sides, a ptr2 copy (char*,
    unsigned char*, void*, const char*; whole-function or block-local; assigned before or inside the call), the cast on
    one memmove only, and memmove declared with `char*`, `int`, `()` or implicitly (through a `#define` to an unbound
    name, which matched 100% with the real casts, so the method was sound). All 83-98.8; only `long`, `int`, `UInt32`
    and `size_t` casts match.
  - Pro7's `ptr2` is gone: it does not match in 2.6.
- Behaviour: checked statement by statement against the disassembly. Plugin type 'Link', mask
  isPostLinker|isPreLinker, assert lines 89/269, message ids 19/65-69, `pCmdLine.toDisk & 4` against
  `optsCmdLine.toDisk & 2`, and the STSbuf size 260 all match. The assert string is "dropinflags & isExecutableTool",
  so the SDK enumerator is `isExecutableTool`. Fixed: it was named `dropInExecutableTool`; it is renamed everywhere
  (code unchanged). Genuine bugs in the original: none found (Pro7's quirks are kept: for a file, a failed
  GetOutputFile/StoreObjectFile is ignored, and an extra argv slot is reallocated).
- Names: functions are Pro7's and Wii's. Parameters and locals are Pro7's (`argc/argv/Argc/Argv`, `dropinflags`,
  `baseargs`, `linkerspec`, `cname`, `linkertype` ...). Invented names: none.
- Types: CWCommandLineArgs is used (the stand-in `ToolCommandLine` is deleted). `File.wroteToDisk` (the repo's
  `validatedOutputMask`) is now Pro7's SInt16, not unsigned short: 2.6 reads it with movsx, which removed the
  `(short)` cast. Added Pro7's `CmdLineStageMask_*` enum, used instead of magic 1/2/4. STILL OPEN: the file record is
  the repo's stand-in `DropinFileRecord`, with its invented field names (inputArgumentMask = Pro7 sourceUsage,
  outputArgumentMask = objectUsage, inputName = srcfilename, outputName = outfilename, outputPath = outfss,
  outputMask = writeToDisk, validatedOutputMask = wroteToDisk, temporaryOutputMask = tempOnDisk). `CLTarget.settings`
  is Pro7's `targetinfo`. Both are cross-unit renames (AGENTS.md lists `DropinFileRecord` = Pro7 `File` as open).
- Data: relink proves .data identical; there is no .bss; all literals are at their use; no bindings point into this
  object's .text or .data.
- Header: it now declares only AppendArgumentList (used by CLMain), SetupTemporaries, DeleteTemporaries (CLFileOps,
  CLMain) and ExecuteLinker (CLFileOps). Removed: the stray `extern SInt32 __argc`, `ToolCommandLine`, and the
  prototypes of the three statics. Return types are now Pro7's `int` (they were `unsigned int`). Five sources include
  it but use nothing from it (CLParserCallbacks_v1.cpp, Arguments.c, Targets.c, CLStaticMain.c, ParserFace.c); their
  includes were left alone.
- Headers relied on: CLErrors.h (CLReport/CLReportWarning/CLReportError, CLStr19/65-69: fine); CLFileOps.h
  (GetOutputFile, StoreObjectFile, gTarg, CmdLineStageMask_*, and `DropinFileRecord` = Pro7's `File`: invented field
  names, placeholders `pad0A`, `pad4dd`, `reserved4f5`, `f69c`, and `short` for Pro7's SInt16 writeToDisk/tempOnDisk/
  sourceUsage/objectUsage beside an SInt16 wroteToDisk; `gTarg` and the stage enum are Pro7 cmdline.h's, declared
  here); CLFiles.h (Files_Count, Files_GetFile); CLTarg.h (`CLTarget` = Pro7's `Target`: placeholders
  `total00`/`count04`/`count08`/`count0c` are Pro7's `BuildInfo info` (linesCompiled, codeSize, iDataSize, uDataSize),
  `settings` = `targetinfo`, `name` = `targetName`, `fileLookup` = `virtualFiles`?); CWPlugins.h (CWCommandLineArgs,
  CWDROPINLINKERTYPE, isExecutableTool/isPreLinker/isPostLinker: SDK names, fine); CLMain.h (numPlugins, pluginInfo,
  plugin_args, clState, optsCompiler, optsCmdLine, Main_PassSpecialArgs: Pro7/Wii names); CLPlugins.h
  (Plugin_GetDropInName); CLIO.h (CLPrint); Generic.h (STSbuf, OS_FindProgram); MacSpecs.h (OS_FSSpec_To_OSSpec);
  MemUtils.h (xmalloc/xrealloc/xstrdup/xfree); MsDos.h (OS_SpecToString, OS_Execute, OS_Delete, OS_GetErrText);
  pref_structs.h (pCmdLine); oslib.h (OS_ASSERT, OSSpec). Not reviewed as units: CLFileOps.h's and CLTarg.h's structs
  are the open problems.
- Open: the `DropinFileRecord`/`CLTarget.settings` stand-in names in CLFileOps.h/CLTarg.h, a cross-unit rename to
  Pro7's `File` and `targetinfo` (header track). Everything else holds; the memmove casts are the original's shape by
  the maintainer's rule (plausible, not obscene).
- Adversarial review: attacked the memmove casts (40 more spellings, memmove prototypes, a copy pointer of each type,
  macros: none but integer casts match; `int` changed to the house idiom `long`, still a fakematch), FreeArgumentList's
  index (Pro7's `scan` 72.2, a while 81.1: held), ExecuteLinker's `cname[256]` (64: 87.3, held) and UInt32 flags,
  header users (all four declarations used by CLMain/CLFileOps: held), and the includes: `CWParserPluginsPrivate.h`
  was unused and is gone (includes.py trim also offered five directly used headers, kept). The entry lacked item 10's
  `Headers relied on` line: added, with CLTarget's and DropinFileRecord's placeholders. Grade NOT SOLID holds (the
  cast fakematch and the stand-in File/Target structs). Rebuilt: 4020 exact before and after, nothing lost; layout ok;
  the unit links.

### src/backend/Switch.c: SOLID WITH TODOS (headers done October 16)
- Boundary: .text 0x4fa3e0-0x4fb890 holds 9 rows, Pro7 Switch.c's 9 functions in reverse order. Wii's module "Switch" has 9 .text contributions, 12 bytes of .data (the assert file name) and 60 bytes of .bss. .data 0x5bc5a8-0x5bc5bc is "Switch.c" padded to 12 bytes plus generate_table's `{0, 3}` initializer template. .bss 0x5e0d08-0x5e0d44 is Pro7's 12 statics in reverse declaration order. Pro7 has no dead functions here. Exceptions starts at 0x4fb890.
- Flags: `-O4,p -inline auto,deferred -sym on`. Deferred because the addresses descend in Pro7's order. `-sym off` gives 5/9.
- Functions: 9/9 exact. Departures from Pro7:
  - 2.6 changes, each confirmed by reverting it (the function stops matching) and in IDA:
    - switchstatement disables tables under `copts.prepare_compress`. IDA reads the byte after `switch_tables`. The `&& !prepare_compress` spelling gives 93.4%.
    - create_switch_table sets `obj->section = GetCurrentSection(2)` and `obj->type` before createIndirect. IDA shows `push 2` with one argument, and `void_ptr` stored before the call. Pro7's order gives 99.1%.
    - generate_table:
      - It tests `LOW_PART(value)`; Pro7's `value` gives 99.7%.
      - It uses `FITS_IN_USHORT`, so cmpli takes 0x8000..0xFFFF; Pro7's `FITS_IN_SHORT` gives 99.7%.
    - I8_treecompare's `start == r30` arm branches ENOTEQU (24) to `caseranges[start]`. IDA confirms `case_ranges + 20 * a1`; Pro7 has `[end]`.
  - Not a 2.6 change (the first reviewer's classification was wrong): the switch table holds label pointers directly. Pro7's `CTool_CreateIndexFromPointer`/`CTool_ResolveIndexToPointer` are Ninji's 64-bit-host additions, which Pro7's CompilerTools.h says "don't exist in the original source". On 32-bit hosts they are identity macros.
  - Shapes the code needs, all re-tested with `fn.py variants`:
    - treecompare:
      - Top-level `reg`/`value`. Pro7's form gives 73.7%, a block-local reg 74.6%.
      - `load_immediate(reg = ALLOC_GPR(), value)`. The separate statement gives 99.5%, `else if (FITS_IN_SHORT(value = ...))` 97.5%.
    - generate_table reuses `reg2` inside the call. Pro7's `tmp` gives 93.5%. A `tmp` inside the call, `reg3`, or a function-level `tmp` each give 93.8%. Separate statements give 99.7%.
    - create_switch_table's `lalloc(4 * (size = ...))`. Pro7's `size` local gives 96.4%, the store-then-reload 97.3%. Dropping the dead `obj->type = NULL` (it is Pro7's) gives 98.7%.
    - I8_treecompare's function-level a-d. Pro7's per-block form gives 73.9%; separate declarations also match.
    - dumpswitchtables:
      - Its `while (size) {...; size--;}`. Pro7's `while (size--)` gives 87.5%, `for (; size; size--)` 98.0%, `size > 0` 89.8%.
      - Its declaration order. Pro7's gives 65.3%.
    - switchstatement's if/else chain. Pro7's early return gives 96.1%.
    - build_case_ranges' `*caseptr++ = cases`. Pro7's three statements give 99.6%. `*caseptr = cases; caseptr++; cases = cases->next;` also matches; the idiom was kept.
    - The duplicated `integral != IT_ULONGLONG && ... != IT_ULONGLONG` is Pro7's text; the binary has one compare (`+6 != 12`).
  - No fakematch, `volatile`, pragma, or invented helper.
- Behaviour: switchstatement, create_switch_table and I8_treecompare were checked against IDA statement by statement: the Object offsets (+4 section, +0x1e toc, +0x2a/+0x2e table data/size), the ncases > 0x3333332 assert (656), and the opcodes and condition codes 23/24. Bug in the original: Pro7's `caseranges[end]` in I8_treecompare's ENOTEQU arm, fixed in 2.6. Bugs of ours: none.
- Names: all functions, statics and locals are Pro7's. Wii's "Switch" module agrees. Invented: none.
- Types: `CaseRange` is Pro7's, at the file top. `SwitchCase` is Pro7's without `max` (16 bytes). `short` selector registers and `SInt32` counters are Pro7's.
- Data: .data and .bss relink byte for byte (`relink.py --only`). The statics are `static`. `switchtables` is defined here as in Pro7, moved from CodeGen.c. It is a COMMON laid out by hash, still at 0x5ea594 in symbols.txt. A full `relink.py GC_2_6` still links 217/217 Matching sources (2806/4080 functions, 798275 bytes), identical to the main tree's build/GC_2_6/ok. No GC_2_6 binding remains for this object's statics; `_switchtables` is a COMMON, which is fine.
- Header: Switch.h declares `SwitchCase` (CFunc, CInline, Iro*, COptimizer, DumpIR), `extern switchtables` (CodeGen, FuncLevelAsmPPC, ObjGen), `switchstatement` (CodeGen) and `dumpswitchtables` (CodeGen). Every declaration has an outside user. No prototypes are in the source.
- Headers relied on:
  - CError.h: asserts. No issue.
  - CFunc.h: `SwitchInfo` and `CLabel.pclabel`.
    - `SwitchInfo` lives here; Pro7 puts it in Switch.h. Its third field is gc1's `sizetype` (Pro7 calls it `x8`). It carries `// 0x00:`-style offset comments and a needless `#pragma pack(push, 2)`.
  - CInt64.h: the helpers, `cint64_zero`/`one`. No issue.
  - CompilerTools.h: lalloc, galloc, memclrw, CTool_EndianConvertWord32. Now included directly, as Pro7 does; it used to come only through ObjGen_PPC_EABI.h.
  - CParser.h: CParser_GetUniqueName, is_unsigned, void_ptr, copts. No issue.
  - enode.h: ENode, the E* condition names. No issue.
  - InstrSelection.h: GEN_NODE, ENSURE_GPR. No issue.
  - objects.h: Object, ObjectList, OT_/ACCESS/D*/OBJECT_* flags.
    - Pro7's `Object.toc` is spelled `dwarfLinks.toc`.
    - `switchtable.data` is `SInt32*` (Pro7: `char*`). LoopDetection and ObjGen cast it anyway.
  - ObjGen_PPC_EABI.h: ObjGen_DeclareSwitchTable, GetCurrentSection. The section kinds (1, 2, 0x20-0x22) have no names; TOC, CodeGen, InlineAsmPPC and Switch all pass bare numbers.
  - Operands.h: Operand, Coerce_to_register, coerce_to_register_pair, indirect, extend32, load_immediate. No issue.
  - PCode.h: emitpcode, makepclabel, pcbranch, pclastblock, PC_*. No issue.
  - PCodeUtilities.h: branch_*.
    - It privately re-declares Operands' `load_immediate(SInt16 target, ...)` and `insert_load_immediate_after` with other parameter names: a duplicate to delete.
    - `branch_conditional`'s parameter names are not Pro7's (`a, compareop, c, label`).
  - Registers.h: ALLOC_GPR. No issue.
  - TOC.h: createIndirect, with Pro7's `flag1`/`flag2` placeholder names.
- Open: the header problems above, which belong to the owners of CFunc.h, objects.h, PCodeUtilities.h and ObjGen_PPC_EABI.h. Outside GC_2_6: GC_2_5/2_7 bindings.json still name this unit's statics `switchtable_base`/`switchtable_max`, names no source defines any more.
- Adversarial review: I re-tested every non-Pro7 spelling with `fn.py variants` (27 variants across 7 functions). Every claimed shape held. Two natural alternatives also match: `caseptr++` as its own statement, and separate a-d declarations; the current spellings are equally plain, so I kept them. In IDA I checked switchstatement, create_switch_table (including GetCurrentSection's single argument) and I8_treecompare. What broke:
  - The reviewer's "2.6 change" for the label-pointer store is really Ninji's 64-bit addition.
  - The reviewer's entry had no "Headers relied on" line, so the grade is down to PENDING HEADERS.
  - Added the direct `CompilerTools.h` include.
  - After the `switchtables` move to Switch.c, a full relink is unchanged (217/217, same bytes as the main tree).

### src/backend/PCodeAssembly.c: SOLID WITH TODOS (headers done October 16)
- Boundary: .text 0x4eb120-0x4ed2d0 has 7 rows. They are Pro7's PCodeAssembly.c in reverse order: pcode_update_mem_labeldiff_imm is the highest, assemblefunction the lowest. Pro7's static `targetinstruction` is inlined everywhere, so it has no row. Wii's PCodeAssembly module has the same functions plus one 242-byte Wii-only function, and its publics are exactly the header's three (assemblefunction, optimizefinalbranches, assemblepcode). The next unit, SFPE_PPC_EABI, starts at 0x4ed2d0. .data 0x5bba10 (8-aligned)..0x5bc2d4 holds the "PCodeAssembly.c" assert name and the jump tables. .bss 0x5e0bd0..0x5e0bd4 is `codebase`. There are no dead functions: Pro7 has none here.
- Flags: `-inline auto,deferred` is right because the functions descend in address (reverse of Pro7's order). Non-deferred is also 7/7 per function, but it puts them in the wrong order. `-sym on` is shown to matter: with `-sym off` only 3/7 are exact (assemblepcode 75.1, optimizefinalbranches 95.4, assemblefunction 97.6, pcode_update_mem_labeldiff_imm 97.2).
- Functions: 7/7 exact. Departures from Pro7:
  - 2.6 changes, all confirmed in the disassembly:
    - ELF relocation kinds: an absolute b/bl/bc gets kind 10 and `bits |= 2` where Pro7 fails with a FATAL.
    - DDATA: the NeedsXREF/`arg == 10` check, and pooled objects (`arg == 14`, GetPooledObjectOffset with the asserts at 106 and 114, two separate sites in the binary).
    - The relocation addend is kept (`wop->addend = offset; offset = 0`) where Pro7 asserts `offset == 0`.
    - New assemblepcode cases: ISEL, psq_l/st with `reg - 912` and a 12-bit displacement, dcbz_l, ps_*.
    - `insertlongbranches(max, min)` from `copts.bc_range`, with the `>=` test.
    - assemblefunction calls ObjGen_BestSection, ObjGen_SymFunc, newDWARF_function, ObjGen_SourceRef, init_xref_anchors, the MWCATS hooks and ObjGen_DeclareSymInfo.
  - Needed shapes: each natural spelling was tried with `fn.py variants`, and each shape is catalogued in AGENTS.md:
    - A separate `diff` local in the labeldiff branch (Pro7's `offset`: 65%).
    - `RegClass arg` (Pro7's UInt8: 92%; RegClass is the field's own type).
    - A function-level `int flag` (Pro7's per-case flag: 99.0).
    - `(UInt16)value` for `value & 0xFFFF` (99.6). gc1 has the cast, so this is the original's shape.
    - insertlongbranches' own `offset` variable (Pro7 reuses `i`: 92.5).
    - `(instr = block->lastPCode)->flags` (98.0) and `targetinstruction(label = ...)` (93-97) in optimizefinalbranches.
    - A `pred` local in the BC→BCLR merge only (Pro7's form 98.9, register only; the other three copies match either way).
    - `nop->flags = nop->flags & ~fSideEffects` (`&=` is 19 bytes shorter).
    - assemblefunction's declaration order (Pro7's: 75%).
    - `codesize + offset` in AppendGListNoData: the original reads the `offset` slot, and Pro7's `tbsize` gives 99.6.
  - Changed to Pro7's spelling where it also matches:
    - Pro7's two-branch PPCErrorStr109 test.
    - Pro7's block-scoped `SInt32 value` in assemblepcode.
    - Pro7's `prev->lastPCode` with no `last` local in insert_align_nops.
  - Reference convention, not needed for the match: `(UInt8)op->arg` on 2.6's added `== 10`/`== 14` tests. The plain spelling is also exact; it is kept as Pro7's spelling for `arg` read as a RefType, and the switch casts are needed (98.1 without them). It is commented in the source.
  - Fakematches: none. There is no `volatile`, pragma, `(void)0` or invented helper.
- Behaviour: all functions are byte-exact with references resolved, and the relink is byte-identical. I checked the 2.6-only paths against the original's disassembly: reloc kinds 2/3/4/5/6/7/8/10/14/15, the bc_range computation, and the argument order of ObjGen_RelocateObj and MWCATS_AddExit. Genuine bugs in the original: none found. The BL case's redundant `wop->type = 2` is Pro7's too. Bugs of ours fixed: none. One wrong comment was fixed: the header called `WeirdOperand.type` an ELF relocation type, but it is the MW kind that emit_relocation maps to ELF.
- Names: all functions and the static `codebase` are Pro7's, and Wii confirms the three publics. Invented: WeirdOperand's `object`/`addend` (Pro7 has the placeholders `x2`/`x6`; now marked TODO).
- Types: Pro7's PCode, PCodeArg, OpcodeInfo, WeirdOperand and EntryPoint at Pro7's 2-byte packing (mac68k). `addend` is now Pro7's `UInt32` (it was SInt32; no code change). Some numbers have no repo enum and stay literal:
  - the RefType numbers (Pro7's `RefType_N` placeholders) and the reloc kinds (Pro7's Mach-O MWReloc names do not cover 2.6's 10/14/15);
  - `func->section` 0/1 (Pro7's SECT_DEFAULT/SECT_TEXT; 2.6's field is a section index);
  - the PCode flag 0x80000 tested before MWCATS_AddExit. It is a 2.6 addition, TODO-marked. I found no setter in the binary (I scanned for imm 0x80000 ors/movs and for byte-ors at +0x16).
- Data: the relink is byte-identical (.text, .data, .bss). `codebase` is `static`, and only assemblefunction (exact) references it, so I deleted its `_codebase` GC_2_6 binding. symbols.txt now shows that slot as `lbl_005E0BD0`: layout.py never names .bss statics from objects. Literals are at their use; there is no pragma pack in the source.
- Header: it declares only Pro7's API: WeirdOperand, EntryPoint, assemblepcode, optimizefinalbranches and assemblefunction.
  - assemblefunction is used by CodeGen. EntryPoint is used by FuncLevelAsmPPC.
  - assemblepcode and optimizefinalbranches have no 2.6 caller. They are kept because Wii exports both as publics and Pro7's PCodeListing calls assemblepcode.
  - The file top has only `codebase`; there are no prototypes and no private copies.
- Headers relied on (added by the adversarial pass; the first entry had no such line):
  - PCodeAssembly.h (own): WeirdOperand, EntryPoint and the three prototypes; `#pragma pack(push, 2)`/`pop` balanced. Invented WeirdOperand members are TODO-marked. Seven units include it without using it (AddPropagation, LoopOptimization, CopyPropagation, PCode, PCodeUtilities, Registers, RegisterInfo).
  - PCode.h: PCode/PCodeBlock/PCodeLabel/PCLink, the PC_* opcodes, the f* flags, makepcode/appendpcode/deletepcode/pccomputeoffsets. Problems: `change_num_operands` is declared in both PCode.h and PCodeInfo.h (Pro7: PCodeInfo.h only); `pccomputeoffsets` returns `unsigned int` where Pro7 has `int` (PCode.c's definition says unsigned; this unit matches either way); `appendpcode` returns `PCode*` (Pro7: void); the flag enum has no name for 0x80000 (it uses Pro7's `fPCodeFlagNNNN` placeholders elsewhere).
  - PCodeInfo.h: `opcodeinfo` (Pro7's 18-byte OpcodeInfo), change_num_operands. No problem found.
  - CInline.h: only `functionbodyoffset`. It is CFunc.c's variable, and Pro7 declares it in CFunc.h as `SInt32`; the repo has it in CInline.h as `UInt32` (and CFunc.c defines it `UInt32`). Misplaced declaration.
  - ObjGen_PPC_EABI.h: ObjGen_DeclareCode, ObjGen_GetSectionGList, ObjGen_DeclareEntry, ObjGen_BestSection, ObjGen_SymFunc, ObjGen_SourceRef, ObjGen_DeclareSymInfo, ObjGen_RelocateObj, init_xref_anchors, MWCATS_AddExit/WriteFunction. Problems: stand-in parameter names: `ObjGen_RelocateObj(ObjGenSection* context, int section, Object* object, UInt8 flags, unsigned int options)` (`section` is the code offset, `flags` the relocation kind, `options` the addend); `ObjGen_GetSectionGList(ObjGenSection* a0)`; `MWCATS_WriteFunction(Object* key, int index)` (the index is the code size); `ObjGen_DeclareEntry`'s `int offset` (Pro7: SInt32).
  - objects.h / common.h: `Object.section` (UInt16). No Section enum, so `func->section == 0` / `= 1` stay literals where Pro7 has SECT_DEFAULT/SECT_TEXT.
  - StackFrame.h: local_offset_16/lo/ha and generate_traceback, which match Pro7's. BE_symbol.h: NeedsXREF and GetPooledObjectOffset, both row-unnamed in names-2.6.md. DWARF.h: newDWARF_function. PCodeListing.h: pclistblocks (`const char*`; Pro7 has `char*`). PPCError.h: PPCError_Error/PPCErrorStr109/190. CMangler.h, CError.h, RegisterInfo.h (spr_to_sysreg) and CompilerTools.h (AppendGListNoData, CTool_EndianConvertWord32) match Pro7. CPrep.h: copts (bc_range, cats, traceback, filesyminfo, function_align, peephole, opt_bcc_lr_ctr, optimizationlevel; the offsets agree with the original's reads).
- Open (the TODOs that keep it from SOLID):
  - the invented WeirdOperand member names;
  - the unnamed 0x80000 PCode flag.
  - Pending headers (owners' work, listed above): functionbodyoffset misplaced in CInline.h; duplicate change_num_operands; ObjGen_PPC_EABI.h's stand-in parameter names; no Section enum.
  - Outside the unit: AddPropagation, LoopOptimization, CopyPropagation, PCode, PCodeUtilities, Registers and RegisterInfo include PCodeAssembly.h without using it. I left this alone because of AGENTS.md's header-edit hazard.
- Adversarial review: I re-ran every documented workaround with `fn.py variants`, and each one held:
  - `&=` gives 91.1;
  - Pro7's `instr`/`label` statements give 98.0/94.8/93.0;
  - Pro7's predecessor reads give 98.9;
  - `tbsize` for `offset` in AppendGListNoData gives 99.6 (`offset` in ObjGen_DeclareCode as well is also exact, and no more natural);
  - Pro7's declaration order gives 75.2;
  - `UInt8 arg` gives 92.1;
  - no `diff` gives 65.2;
  - reusing `i` in insertlongbranches gives 92.5.
  `(UInt16)` for `& 0xFFFF` is in gc1's text, so it is the original's shape. One construct did break: the `(UInt8)` casts on 2.6's added `arg == 10/14` comparisons are unnecessary (the plain comparison is also exact). I kept them as Pro7's convention for reading `arg` as a reference type, since Pro7 casts in every such switch and removing the switch casts drops to 98.1. A comment now says so.
  Behaviour I checked against IDA:
  - pcode_update_mem_labeldiff_imm, statement by statement: case 12→local_offset_ha, case 13→lo, the XREF/pooled paths and lines 100/106/114/145/174;
  - assemblefunction's call order and arguments, the copts offsets, and the 0x80000 test on raw flags;
  - the .data cut: 0x5bba10 starts with "AFTER INSERT ALIGN NOPs" and then the assert file name, after the previous unit's table ends at 0x5bba0c.
  There is still no setter of PCode flag 0x80000 anywhere: I scanned for `or r32, 80000h`, `or byte [r+16h], 8`, `push 80000h` and an 81-form `or [r+14h]`. A normalised whole-file diff against Pro7 found no unexplained departure.
  Checks: nothing anywhere lost against compare_start.json; layout.py runs; relink links.

### src/frontend/CTemplateTools.c: SOLID WITH TODOS (headers done October 16; graded NOT SOLID before)
- Boundary: 43 rows, 0x562380-0x565850, all CTemplateTools.c asserts. They are Pro7's file in reverse: PushInstance is highest, IsSameTemplateType lowest. Pro7's statics GetFirstRealArg, FindNestedClass, DeduceExprList, FindTemplArg and DeduceArrayCopy are inlined and have no rows. The neighbours CInline (first function CInline_Init at 0x562350) and CTemplateFunc (0x565850) agree. Every Wii module name in this range is a row. .data runs 0x5D12A8-0x5D16E8, 8-aligned at both ends, and there is no .bss. Pro7's dead CTempTool_GetPTMTemplArgExpr and CTemplTool_ParamHasDefaultArg are left out. ParamHasDefaultArg brings a 12-byte jump table the original's 0x440 bytes of .data have no room for. GetPTMTemplArgExpr leaves no data, so the original's data cannot show whether 2.6 has it; Wii does. With both functions added the unit still compiled 43/43 and linked.
- Flags: pro6 (pro53 maps to it), `-O4,p -inline auto,deferred -sym off -Cpp_exceptions off`. Deferred because the functions descend in address (non-deferred also gives 43/43, but in the wrong order for the link). `-sym on` gives 33/43 and leaving out `-Cpp_exceptions off` gives 2/43.
- Functions: 43/43 exact. The source is now Pro7's text: TPT_*, TEMPLDEP_*, TDE_* and ERR_* names instead of numbers, Pro7's macros, `sizeof` instead of `galloc(0x12)`/`galloc(0xc)`, Pro7's static linkage, and the templdep view instead of `templatecomparison` in EqualExprTypes. Where it still departs from Pro7, Pro7's spelling was tried and does not match; each place has a comment:
  - 2.6 change: DeduceBitfieldCopy has `case 8: maxsize = 64`.
  - Needed shapes, each shown by the disassembly:
    - IsTemplateArgumentDependentExpression returns the comparison (`sete`).
    - IsTemplateArgumentDependentType returns `(flags & CLASS_IS_TEMPL) != 0` (`setne`).
    - EqualParams and IsIdenticalTemplArgList end in `x == NULL` (`sete`; Pro7's `!x` gives a branch).
    - CheckTemplArgType has an extra `return;` after its error (a second epilogue).
    - SetupTemplateArgumentNameSpace has `is_global = clear_global = TRUE`.
    - DeduceDefaultArg reads the class template through `deduce.instance`.
    - DeduceExpr declares `type, pr, saved` in that order (stack slots) and calls `newExpr = ...; getnodeaddress(newExpr, TRUE)` (the push order).
    - DeduceTemplDepType has a block-scoped `arg` in each branch (registers). Adversary: only one of the two has to
      be block-scoped (one at function scope plus one block-local also matches, both at function scope 97.4); that
      spelling shadows `arg`, so the two block-locals stay as the plainest exact form.
  - GetSelfRefTemplate: `target = type->u.templ.templ; if (target->specializations)` (adversary: the plainer
    statement form matches as well as the reviewer's gc1 `(target = ...)->specializations`, so it replaced it; Pro7's
    no-`target` spelling is 98.0).
  - Original's shape, from gc1's 1.x bodies (gc1 is itself a matching decomp of 1.3, so this is weak evidence):
    - InitDeduceInfo increments its counters before the advance, walks the ids in a while loop, and copies `args` to `arg`. Its Pro7 parts that do match (int, one index, while(args), the flag loop as a for) were kept.
  - InitDeduceInfo's `arg = args;` copy: `buffer[i].data = args->data` matches only while an (unused) `arg`
    declaration remains (removed: 96.9); `arg` as a block-local of the loop (gc1's shape) also matches. Kept.
  - TODO workaround: `switch ((SInt8)type->type)` twice (IsTemplateArgumentDependentType, DeduceTypeCopy). It stands in for Pro7's signed byte enum TypeType; the repo's `Type.type` is a UInt8 and gives `movzx`.
  - No fakematch.
- Behaviour: (adversary: DeduceBitfieldCopy checked against IDA's pseudocode: the 64-bit case, error ids 138/124,
  sizes, `stunsignedint`, the deduced-type argument all agree.) Every function is byte-exact with references resolved, so error ids, constants and fields are the original's. The only literal is `__FILE__`. Bugs in the original: none seen. Bugs of ours: none (the rewrite changed spelling only).
- Names: functions are Pro7's (and Wii's where Wii names them). Parameters and locals are now Pro7's (stack/tmclass/func, info/params/args/flag, a/b/copyNames, deduce/templ/resultQual ...), replacing gc1's (entry/value, left/right/copyValue, ctx/ty/node/out, context/typeArg ...). `ctempl_instdepth` is now defined here as Pro7's `short` (moved from CTemplateNew.c; it is a COMMON, and both units still link). No invented names in the unit.
- Types: NOT clean. The template types are repo-wide stand-ins with invented names and fields, used in about 20 files:
  - `CTStateElem` is Pro7's TemplArg (`argument.type/expression`, `qualifiers`, `bound`, and an invented `struct TemplateArgument` for its `data`).
  - `TemplateMatchState` is DeduceInfo.
  - `TemplateContext` is TypeDeduce (`modes.processingClassTypes/processingArgument`, `f16`, `parameterNIndex`).
  - `TypeClassTemplate` is TemplClass. Its `relatedClass` and `replacementTemplate` are typed `Type*` where Pro7 has TemplClassInst*/TemplClass*. `hasVirtualFunction & 2` is really Pro7's `flags & TEMPLCLASS_FLAGS_2`.
  - `TypeClassExt800` is TemplClassInst (`relatedClass` for `parent`, `classTemplate` typed `Type*`).
  - `TemplStack` uses `value`/`kind` where Pro7 has `u.theclass/u.func` and `is_func`.
  - `ENode.memberfunc` is Pro7's `emember`.
  - Object.u.func.inst is `void*` (objects.h), cast to the stand-in `TemplateSpecializationData` (Pro7's
    TemplFuncInstance, CTemplateFunc.h) in DeduceDefaultArg.
  - TypeMemberPointer's `memberType`/`owner.type` are Pro7's `ty1`/`ty2`.
  - CError_LockErrorPos takes `int*` where Pro7 has `TStreamElement**`.
  - These force the `TEMPL_CLASS`/`TEMPL_CLASS_INST`/`TYPE_CLASS` casts throughout.
  - The placeholder `unk11` (padding) was removed from CTStateElem; it stays 0x12 bytes at pack(2).
  - MergeArgNames keeps `Type*` parameters (Pro7: `TypeFunc*`) because its callers pass `Type*`.
  - Error 374 stays a number: CError.h's enum stops at 371, and other units also write 37x-38x ids as numbers.
- Data: the relink proves .data byte-identical. No statics. The only literals are the jump tables and `__FILE__`. bindings.json has 10 function-name bindings at this unit's row addresses (the repo-wide row-duplicating pattern, 692 of them), left alone.
- Header: it declares only what other units use, plus `extern short ctempl_instdepth`. Moved out of it:
  - IsIdenticalTemplArgList and DeduceArgCopy are used only here; they stay non-static, as in Pro7.
  - FindTemplateInstance, DeduceBitfieldCopy, DeduceTypeQualCopy, DeduceExSpecCopy, DeduceClassInstanceCopy, FindNestedTemplateInstance, DeduceTemplArg and DeduceTemplDepType are static, as in Pro7.
  - The dead stand-in structs TemplateLookupContext, TemplateParameterIDEntry and TemplateSlot are deleted, with their forward declarations in common.h.
  - The header now includes enode.h, as Pro7's does, for `ENodeType nt` in CTempl_MakeTemplDepExpr.
  - The file top has no private declarations.
- Headers relied on (missing from the first entry; adversary): all project headers are needed (`includes.py trim`
  removes none). types.h: Type structs, TYPE_* macros; `Type.type` and every Type-prefix struct's kind are `UInt8`
  with a separate `TYPEILLEGAL = 0xFF` enum where Pro7 has the signed `TypeType` (the cause of the casts),
  TypeMemberPointer's invented member names, TypeClassExt800 stand-in, `IS_TYPE_VEC2X32FLOAT` defined twice
  (clang -Wmacro-redefined). CTemplateClass.h: TypeClassTemplate stand-in (Pro7's TemplClass). CTemplateFunc.h:
  TemplateSpecializationData stand-in (TemplFuncInstance), TemplateMatchState/TemplateContext stand-ins. objects.h:
  `void* inst`. enode.h: `ENode.memberfunc` (Pro7 `emember`). CError.h: enum stops at 371 (374 written as a number),
  CError_LockErrorPos' `int*`. templates.h/scopes.h/CScope.h/CExpr.h/CDecl.h/CABI.h/CInt64.h/CMachine.h/
  ObjGen_PPC_EABI.h: only Pro7-named functions used, no problem seen. None of these were rewritten here.
- Hygiene: braces, TRUE/FALSE, `//` comments, clang-format clean, clang_check 0 errors, no `#if 0`, no CERROR_FILE.
- Open: the template types above (in the unit's own header, CTStateElem and the invented `struct TemplateArgument`). They should become Pro7's TemplArg/DeduceInfo/TypeDeduce/TemplClass/TemplClassInst/TemplStack with Pro7's member names, moved into templates.h, and `Type.type` should become Pro7's TypeType, which would retire the two `(SInt8)` casts. That is a cross-unit rename (CTemplateClass, CTemplateFunc, CTemplateNew, CError, CPrec, CDecl, CExpr, CScope, ...) beyond this review. Otherwise the unit is SOLID.
- Adversarial review: re-ran every claimed needed shape with `fn.py variants` (EqualParams `!b` 93.4,
  IsIdenticalTemplArgList `!params` 97.4, `? TRUE : FALSE` 93.2, IsTemplateArgumentDependentExpression's if 71.4 and
  costs three others, CheckTemplArgType without `return` 94.5, unchained is_global 98.5, DeduceDefaultArg via the
  method 78.7, DeduceExpr nested call 99.7: all held). Broke: GetSelfRefTemplate's assignment-in-condition (plain
  statement matches, applied), the DeduceTemplDepType claim (only one block-local needed), the missing `Headers relied
  on` line (added). `(SInt8)` casts: Pro7's signed `TypeType` for Type.type and all ten Type-prefix structs' kind
  retires both casts here (43/43) but costs four functions elsewhere (TransformMonadicSelfAssignmentToDiadic-
  SelfAssignment, set_out_param_displ, CInit_InitializeData, CInline_ExportCheck), all the same diff: a byte holding a
  type kind is now `movzx`ed and compared as an int against the signed enumerators; in set_out_param_displ and
  CInit_InitializeData that byte is a `UInt8 kind` local, and typing it `TypeType` made both exact again (the other
  two not tried), so the cost looks fixable (not applied: repo-wide). `ctempl_instdepth` moved to
  CTemplateTools.c (Pro7's home): full `relink.py GC_2_6` 217/217, 2806 functions / 798275 bytes, identical to the
  main tree with it in CTemplateNew.c; compare 4020 exact, nothing lost.

#### Shared evidence for CLFiles.c, CLTarg.c, CLSegs.c, CLOverlays.c, CLAccessPaths.c, CLPrefs.c

Shared evidence for all six units (GC_2_6):
- Wii 1.0RC1 CodeView puts each function in its module (CLFiles, CLTarg, CLSegs, CLOverlays, CLAccessPaths, CLPrefs);
  each unit's 2.6 functions are in Pro7's file order and ascend in address.
- Flags `pro53` (= pro6 for 2.6) `-O4,p -sym off`, plain `-inline auto`. Sweep: `-sym on` loses CLFiles 1, CLTarg 1,
  CLSegs 1, CLAccessPaths 3, CLPrefs 1 (CLOverlays the same either way); `-inline auto,deferred` loses 1-10 per
  unit (only CLPrefs is unchanged); `-inline off` compiles the same (no inlining candidates), so default kept.
- Every unit relinks (`relink.py --only`, all six), so .text, .data and .bss are byte-identical. Each object's .data
  starts 8-aligned (0x5a4778, 0x5a47b8, 0x5a1ee0, 0x5a55d8, 0x5a5658, 0x5a19d8). Full compare: nothing lost.
- Common headers (all units): common.h; driver/oslib.h (OS_ASSERT, OSSpec/OSPathSpec/OSType/OSTime); driver/CLIO.h
  (`__assertion_failed`, CheckForUserBreak, CLPrint); driver/MemUtils.h (xmalloc/xcalloc/xrealloc/xstrdup/xfree);
  driver/Memory.h (StorageHandle, NewHandle, HLock ...); driver/cmdline.h (forward typedefs). Problems found:
  oslib.h's OS_ASSERT calls `__assertion_failed`, which is declared in CLIO.h, not oslib.h (every user has to include
  CLIO.h); MemUtils.h's parameters are stand-ins (`a1`, `detail`, `pool`, `text`; `unsigned int size` where Pro7 has
  `const char *what, int size`; types not checked against 2.6) and it includes `<setjmp.h>` for nothing;
  Memory.h's `StorageHandle` (with a `tokens` union member) stands in for Pro7's `Handle` (known, AGENTS.md);
  CLIO.h's `CLIO_OutOfMemory()` is an invented macro (Pro7 writes `fprintf(stderr, ...); exit(-23);` inline) with
  `#include <stdio.h>`/`<stdlib.h>` in the middle of the header; cmdline.h forward-declares a dozen invented
  stand-in struct names (`StringLookupResult`, `DropinResultSlot`, `CallbackOverlayRecord`, ...).

### src/driver/CLFiles.c: SOLID
- Boundary: 14 functions = Pro7's CLFiles.c whole, same order; Wii module CLFiles has the same 14. .data
  0x5a4778-0x5a47b4 (8-aligned start).
- Flags: as above (`-sym off` needed: Files_InsertFile inexact with `on`).
- Functions: 14/14 exact. Departures from Pro7: Files_InsertFile's `while (position-- > 0)` (Pro7 decrements in the
  body: tried, 77.1%, 114 bytes vs 126) - needed shape, original's (a plain idiom). Otherwise Pro7's text.
- Behaviour: Pro7's text throughout; exact. No bugs found.
- Names: Pro7's/Wii's for all functions, parameters and locals. Invented: none.
- Types: Pro7's `File`, `Files`, `VFile` (CLFiles.h); `File` in 2.6's layout (`OSTime` FILETIME for Pro7's `time_t`
  dates; 0x6a0 bytes, File_New's `push 6A0h`). Handles are `StorageHandle*` (Pro7 `Handle`, repo-wide).
- Data: only literals (file name, assert strings); no statics; no data bindings.
- Header: removed `File_Free` (no user outside the unit; defined before its use). Rest used by other units.
- Headers relied on: common set above; CLDependencies.h (`Deps` in File: Pro7's shape, fine; also declares
  `CLT_filesp`/`CLT_filestack` "defined in CPrep.c" - not this unit's concern); CLLoadAndCache.h (FixTextHandle
  returns `unsigned int`, unchecked); MsDos.h (OS_EqualSpec). Include list trimmed: CLFileOps.h replaced by MsDos.h,
  `<stdlib.h>` dropped.
- Open: headers only.
- Header round H2 (October 16): the common set's items are resolved: Memory.h's `StorageHandle` is Pro7's `Handle` (common.h), MemUtils.h has Pro7's `const char* what, int size`, `__assertion_failed` is declared in oslib.h with OS_ASSERT, CLIO.h lost `CLIO_OutOfMemory()` and its mid-header includes, cmdline.h holds only Pro7's forward typedefs; CLLoadAndCache.h's FixTextHandle returns Pro7's `short`. Nothing open.

### src/driver/CLTarg.c: SOLID
- Boundary: Target_New, Target_Free, Targets_Term, Target_Add = Pro7's CLTarg.c whole, same order (Wii's CLTarg is a
  later, larger file: directory cache, Targ_AddFile). .data 0x5a1ee0.
- Flags: as above (`-sym on` loses one).
- Functions: 4/4 exact; Pro7's text verbatim, no departures.
- Behaviour: Pro7's; exact. No bugs.
- Names: Pro7's. Invented: none.
- Types: Pro7's `Target` (with `struct BuildInfo`, the `linkage` struct, `LinkModel*` enum) in CLTarg.h.
- Data: literals only ("target", "target info", file name, assert text).
- Header: removed `Target_Free` (only Targets_Term uses it). Includes trimmed to CLTarg.h, CLIO.h, MemUtils.h, MsDos.h.
- Headers relied on: common set; CLAccessPaths.h/CLFiles.h/CLOverlays.h/CLSegs.h/CLDependencies.h (embedded
  structs and their Initialize/Terminate); DropInCompilerLinker.h (CWTargetInfo size); MsDos.h (OS_GetCWD).
- Open: headers only.
- Header round H2 (October 16): the common set's items are resolved: Memory.h's `StorageHandle` is Pro7's `Handle` (common.h), MemUtils.h has Pro7's `const char* what, int size`, `__assertion_failed` is declared in oslib.h with OS_ASSERT, CLIO.h lost `CLIO_OutOfMemory()` and its mid-header includes, cmdline.h holds only Pro7's forward typedefs; the embedded records are Pro7's (Driver records family). Nothing open.

### src/driver/CLSegs.c: SOLID
- Boundary: 8 rows in Pro7's order, plus Pro7's dead Segments_InsertSegment and Segments_DeleteSegment (restored by
  the adversarial review): the linker dropped them, but 2.6's asserts after them carry Pro7's line numbers (137, 147),
  so Pro7's lines 79-104 were in 2.6's file. Wii lacking them says nothing (its linker drops dead code too). .data
  0x5a5658.
- Flags: as above (`-sym on` loses one).
- Functions: 8/8 exact; Pro7's text verbatim (static Segments_GrowSegments as in Pro7).
- Behaviour: Pro7's ("Jump Table" 0x28, "Main" 0xFFFF, grow by 20). No bugs.
- Names: Pro7's. Invented: none.
- Types: Pro7's `Segment`/`Segments`.
- Data: literals only.
- Header: removed `Segment_Free` (unit-internal). Includes trimmed (oslib.h comes through CLIO.h).
- Headers relied on: common set only.
- Open: headers only.
- Header round H2 (October 16): the common set's items are resolved: Memory.h's `StorageHandle` is Pro7's `Handle` (common.h), MemUtils.h has Pro7's `const char* what, int size`, `__assertion_failed` is declared in oslib.h with OS_ASSERT, CLIO.h lost `CLIO_OutOfMemory()` and its mid-header includes, cmdline.h holds only Pro7's forward typedefs. Nothing open.

### src/driver/CLOverlays.c: SOLID
- Boundary: 17 rows in Pro7's order = Wii's CLOverlays module's 17, plus Pro7's dead Overlays_AddFileToOverlay
  (restored: its literals are pooled with live ones, but the asserts after it are at Pro7's lines 144, 160, ...).
  .data 0x5a47b8.
- Flags: as above (`-sym` indifferent here; kept like its neighbours).
- Functions: 17/17 exact. Departures from Pro7, each tested: chained `addr.lo = addr.hi = 0`,
  `grp->olys = grp->lastoly = NULL`, `oly->cnt = oly->max = 0` (Pro7's two statements in either order: 93.7%, 66.7%,
  93.1%) - original's shape (2.6's catalogued chained assignment), commented; Overlays_CountGroups and
  OvlGroup_CountOverlays count before advancing (`num++; scan = scan->next;`, Pro7's order 95.5%/95.7%) - original's
  shape, commented. `CLInternalError(__FILE__, n, ...)` is Pro7's `DO_INTERNAL_ERROR` expanded (Ninji's macro).
- Behaviour: Pro7's; exact. No bugs.
- Names: Pro7's. Invented: none.
- Types: Pro7's `OvlAddr`, `Overlay`, `OvlGroup`, `Overlays` (`int olycnt` as Pro7).
- Data: literals only ("main_application", "MAIN", "Could not allocate %s", "overlay group", "overlay",
  "overlay file list").
- Header: removed OvlGroup_Delete, Overlay_Delete, Overlay_GetFile (unit-internal); since they are used before their
  definitions the file top now has their three prototypes. Includes trimmed (oslib.h).
- Headers relied on: common set; CLErrors.h (CLInternalError). CLOverlays.h's unused cmdline.h include removed
  (adversarial review; nothing it defines comes from there; full compare unchanged).
- Open: headers only.
- Header round H2 (October 16): the common set's items are resolved: Memory.h's `StorageHandle` is Pro7's `Handle` (common.h), MemUtils.h has Pro7's `const char* what, int size`, `__assertion_failed` is declared in oslib.h with OS_ASSERT, CLIO.h lost `CLIO_OutOfMemory()` and its mid-header includes, cmdline.h holds only Pro7's forward typedefs. Nothing open.

### src/driver/CLAccessPaths.c: SOLID
- Boundary: 18 functions in Pro7's order; Wii's module has the same (later names Path_Gather,
  Paths_CopyRecurseSpecs, plus later Paths_Order/Paths_AddAccessPath). Pro7's dead Paths_FindPath restored (the
  next assert is Pro7's line 182, so its lines 165-180 were there). Absent: the whole Frameworks section and its `FrameworkPaths`/`FrameworkInfo` (Mac-only: none of
  its strings, "MWFrameworkPaths", "access frameworks", ".framework", is in 2.6's binary). .data 0x5a55d8.
- Flags: as above (`-sym on` loses three).
- Functions: 18/18 exact. Departures: GatherRecurse's `char filename[256]` (Pro7: 64) - 2.6 change, the frame size
  shows it (commented); otherwise Pro7's text.
- Behaviour: Pro7's; exact. No bugs.
- Names: Pro7's (Wii renamed two later; Pro7's kept for the same bodies). Invented: none.
- Types: Pro7's `Path`/`Paths`. Changed `CWFileSpec` to Pro7's `FSSpec` in CopyRecurseFSS/Paths_CopyRecurseFSS
  (same type: Files.h's `typedef struct FSSpec CWFileSpec`).
- Data: literals only; the bindings at Path_New, Paths_GrowPaths, Paths_FindPathSpec are function bindings (not data).
- Header: removed Path_Init, Path_Free, Paths_DeletePath (unit-internal, defined before use). Includes trimmed from
  13 to CLAccessPaths.h, CLIO.h, Generic.h, MacSpecs.h, MemUtils.h, MsDos.h (dropped win32.h, CLDependencies.h,
  CLPluginRequests.h, CLProj.h, CLTarg.h, CWParserPluginsPrivate.h, Files.h).
- Headers relied on: common set; Files.h (FSSpec: comment says its UInt16/UInt32 signedness differs from Ninji's
  short/long on purpose, `#pragma pack(push, 2)` balanced); Generic.h (OS_GetFileNamePtr, OS_MakeSpecWithPath);
  MacSpecs.h (OS_OSSpec_To_FSSpec); MsDos.h (OS_OpenDir/ReadDir/CloseDir/EqualPathSpec, OS_PATHSEP via oslib.h).
- Open: headers only.
- Header round H2 (October 16): the common set's items are resolved: Memory.h's `StorageHandle` is Pro7's `Handle` (common.h), MemUtils.h has Pro7's `const char* what, int size`, `__assertion_failed` is declared in oslib.h with OS_ASSERT, CLIO.h lost `CLIO_OutOfMemory()` and its mid-header includes, cmdline.h holds only Pro7's forward typedefs; Files.h is DONE (Pro7's FSSpec, pack balanced). Nothing open.

### src/driver/CLPrefs.c: SOLID
- Boundary: PrefPanel_New, PrefPanel_GetHandle, PrefPanel_PutHandle, Prefs_AddPanel, Prefs_FindPanel in Pro7's order
  (Wii's CLPrefs module). Pro7's Prefs_Initialize/Prefs_Terminate restored as dead code: no caller in Pro7 or 2.6 (2.6's
  PutHandle ends at 0x417759 and AddPanel starts at 0x417760, so the linker dropped them), but the file had them:
  Pro7 (earlier) has them and Wii's CLPrefs module (later) keeps both, live there. The reviewer's "Wii's CLPrefs
  module" has 8 functions, not 5. .data 0x5a19d8,
  .bss 0x5de210 (`panellist`).
- Flags: as above (`-sym on` loses one).
- Functions: 5/5 exact. Rewritten from Pro7: the old source was a stand-in (a `NameTableEntry` with a
  `handles.storage.destination/temporary` union and an unused `handles.preference` arm, `xmalloc(NULL, 16U)`, a dead
  `end` pointer in PrefPanel_New, a hand-written `__assertion_failed("MemError()==noErr", "CLPrefs.c", 60)`,
  `CLReportError(0x5a, ...)`, `while` walks). Pro7's text matched at once with only `*h` written `h->data`
  (the repo's StorageHandle). No departures.
- Behaviour: Pro7's (out of memory: fprintf + exit(-23), as the disassembly; error CLStr90 for a duplicate panel).
  No bugs.
- Hygiene: PrefPanel_PutHandle's yes/no `int` returns now `TRUE`/`FALSE`.
- Names: Pro7's (`PrefPanel`, `data`, `workHandle`, `pnl`, `panel`, `len`, `scan`). Invented: none.
- Types: Pro7's `struct PrefPanel` defined in CLPrefs.c (opaque `typedef struct PrefPanel PrefPanel` in CLPrefs.h);
  Pro7's signatures (`const char* name, void* initdata, SInt32 initdatasize`; UInt32 len).
- Data: `panellist` static, at 0x5de210 (relinks); no binding for it.
- Header: now declares only the opaque type and the five functions (all used by CLMain, CLPlugins,
  CLParserCallbacks_v1, CLDropinCallbacks_V10); dropped `<setjmp.h>` and the stand-in struct.
- Headers relied on: common set (CLPrefs no longer uses CLIO_OutOfMemory); CLErrors.h (CLReportError, CLStr90);
  CLMain.h (`clState.pluginDebug`); StringExtras.h (ustrcmp).
- Open: headers only.

- Adversarial review: attacked the departures (Pro7's spelling of Files_InsertFile's loop, Overlays' three chained
  assignments and both count-before-advance loops under every -O level (-O1..-O4, ,p/,s), -sym on/off and the pro5
  host compiler: Pro7's text never matches, best 13/14 and 12/17; five more spellings of InsertFile's loop, all 71-77%),
  the boundary, the headers' users, includes, Wii's module lists and IDA's CLPrefs range. Broke: the "dead functions
  are absent" claims. Pro7's assert line numbers after Segments_InsertSegment/DeleteSegment,
  Overlays_AddFileToOverlay and Paths_FindPath are 2.6's, so those functions were in 2.6's files; Wii's CLPrefs has
  Prefs_Initialize/Prefs_Terminate (the reviewer said it has only the five). All six restored as dead code (the linker
  drops them; every unit still links, full compare 4020/4077 with nothing lost). Also fixed CLOverlays.h's stray
  cmdline.h include and PutHandle's 1/0. Held: every departure, the flags, the types (File/Target sizes are proven
  by the pushes), the header lists (every declaration has an outside user), the .data cuts. Grades unchanged:
  each unit is SOLID PENDING HEADERS (the shared driver headers' problems listed at the top stand).
- Header round H2 (October 16): the common set's items are resolved: Memory.h's `StorageHandle` is Pro7's `Handle` (common.h), MemUtils.h has Pro7's `const char* what, int size`, `__assertion_failed` is declared in oslib.h with OS_ASSERT, CLIO.h lost `CLIO_OutOfMemory()` and its mid-header includes, cmdline.h holds only Pro7's forward typedefs; CLPrefs.h declares Pro7's opaque `PrefPanel`. Nothing open.

#### Shared evidence for Scheduler.c, MachineSimulation601.c, MachineSimulation603.c, MachineSimulation603e.c, MachineSimulation604.c, MachineSimulation750.c, MachineSimulation821.c

# Review: the scheduler and the machine models (GC_2_6)

Common to the six machine models: their stale `include_dirs` (`src/optimizer`, `src/backend`, `include`) were
removed from config/sources.json (every function still exact, all link); `can_issue`'s returns, the completion
flags and `MachineInfo.renames_registers` now use `TRUE`/`FALSE`.

Shared headers, reviewed once (the units' `Headers relied on` lines refer here):

- `include/compiler/Scheduler.h` (this review's own header): Pro7's API, `MachineInfo` at 2-byte packing (Pro7's
  `align=mac68k`), push/pop balanced, every variable `extern`. Users: Scheduler.c (machine601..821, 7400, 7450),
  every machine model (`MachineInfo`, `default_uses_vpermute_unit`, `is_dependent`), Peephole.c
  (`uses_vpermute_unit`), CodeGen.c (`scheduleinstructions`). `is_same_operand` has no
  user anywhere (not in 2.6's binary, not in Wii's Scheduler publics) but stays as Pro7's external function.
  Open: `MachineInfo`'s first three members are invented names (`issue_width`, `renames_registers`,
  `branch_latency`; Pro7 x0/x4/x8), TODO-marked; no source has the real ones (Wii's CodeView carries no types).
- `include/compiler/PCodeInfo.h`: `PCOpKind` (Pro7's), `OpcodeInfo` (Pro7's layout, pack(2)), the `PC_*` opcode numbers
  and `OPCODE_MAX` 469, `opcodeinfo`. Problems: `OpcodeInfo.operand_count`/`.rank` are invented (Pro7 x8/x9), marked
  only "(Pro7: x9 ...)", not `TODO: invented name`; the opcodes are `#define`s, not Pro7's enum (documented there as
  a matching need).
- `include/compiler/PCode.h`: `PCode`, `PCodeArg` (`placeholder[10]` is Pro7's), `PCodeBlock`, the `f*` flags,
  `PCODE_FLAG_SET_F`, `pcbasicblocks`, `appendpcode`. Problems: numeric flag names `fPCodeFlag20000`/`40000` (Pro7's
  own); AGENTS.md notes some flag values (`fIsConst` ...) are not Pro7's; the multi-line `PC_OP_IS_*` macros' layout is
  clang-format's operand-per-line (harmless). Not reviewed as a unit.
- `include/compiler/Registers.h`: `Register0`/`Register2`, `used_virtual_registers` (`SInt32`, 2.6's `long`, documented),
  `coloring` (`SInt32`; Pro7 `int`: unjustified in the header, compiles the same here). RegClass itself is common.h's.
- `include/compiler/Alias.h` (`gather_alias_info`, `may_alias`), `CError.h` (`CError_FATAL`), `CompilerTools.h`
  (`oalloc`, `freeoheap`), `CParser.h`/`CPrep.h` (`copts.scheduling`, `copts.altivec_model`), `common.h` (typedefs,
  `RegClass`, `TRUE`/`FALSE`): only those declarations used; nothing wrong seen in them. CParser.h has an open TODO
  (`cerror_locktoken` type) unrelated to these units.

### src/backend/Scheduler.c: SOLID WITH TODOS
- Boundary: 0x507ba0-0x508610, 11 rows, Pro7's Scheduler.c in reverse (initresources highest, default_uses_vpermute_unit
  lowest; the last two rows were filed under Peephole, moved earlier); Wii's `Scheduler` module has the same publics;
  .data 0x5bdc68 (8-aligned) is the assert name "Scheduler.c" + findsuccessors' 4-entry jump table, .bss 0x5e1858 66
  bytes = the nine statics at pack(2) (Wii's module .bss is 66 too). Inlined statics (makedgnode, adddgnode, removedgnode,
  addtolist, makedglistnode, serializeall/list/load, computedeadlines, uncovering, holdoffsuccessors) have no rows.
  `is_same_operand` is Pro7's dead external: no caller, absent from 2.6 (its 20-byte table is dropped with it).
- Flags: pro53 `-O4,p -inline auto,deferred -sym on`. Deferred: rows descend in address. `-sym off` loses
  scheduleblock and selectinstruction (86.7/82.5), so `on` is shown.
- Functions: 11/11 exact. Departures from Pro7:
  - initresources: `RegClass rclass` counter for Pro7's `int` + `(char)` casts (needed shape: Pro7's 91.6).
  - addsuccessor: early `if (a == b) return;`, latency without Pro7's `> 0` clamp (needed shape: Pro7's 90.1, without
    the clamp 92.6). serializeregister: `RegClass` parameter (Pro7's `int` + `(char)` also exact; kept the typed one).
  - findsuccessors: 2.6 change, r13 skipped like r2 (confirmed: an extra compare in 2.6); an `(int)` cast on the r13
    compare (needed shape, the catalogued movsx + 32-bit compare; natural 96.5, also as `r2 || r13`). The casts on the
    r2 and r0 compares were unneeded and are removed.
  - selectinstruction: `b = uncovering(node2); a = uncovering(node);` (2.6 change in order, confirmed in the
    disassembly: node2's successor loop runs first; Pro7's spelling 75.7, in-order 96.5).
  - scheduleblock: the first loop reuses `node` where Pro7 declares a block-local `DGNode *n` (needed shape: with
    Pro7's `n` MWCC keeps `block` in a register for the three stores after it where 2.6 reloads it for each, 97.9).
    Pro7's plain stores otherwise (the earlier `PCodeBlock** pb = &block` fakematch is gone).
  - is_dependent: a `def = b->args` local (needed shape: Pro7's `b->args[0]` 89.3); now one flat `&&` condition as
    Pro7's (the extra nested `if` was unneeded and is removed).
- Behaviour: statement by statement Pro7's apart from the above; constants (cpu ids 1-10, `pcodeCount > 2`, the
  assert 470) and offsets checked in the disassembly. Genuine bugs in the original: none seen. Bugs of ours: none.
- Names: functions Pro7's/Wii's; statics Pro7's (`register_uses` ... `MI`). Invented: DGNode/DGSuccessor member names
  (`next`, `prev`, `latency`, `earliest`, `latest`, `height`; Pro7 x0..x16), TODO-marked.
- Types: Pro7's DGNode/DGSuccessor/DGNodeList at pack(2); UInt16 fields and counters as Pro7's.
- Data: relink proves .data/.bss; statics all `static`. Fixed: three stale bindings inside this object's .bss
  (`_data_00581b80` = MI, `_max_height` = criticalpath, `_data_00581b7c` = defaultsuccessor, sized 0x3c) deleted; no
  other code references them; symbols.txt now names them MI/criticalpath/defaultsuccessor. The file-level
  `#pragma pack(2)` without a reset is what lays the statics out at 2-byte packing (66 bytes): explained in a comment.
- Header: see Scheduler.h above.
- Headers relied on: Scheduler.h (MachineInfo invented member names), PCode.h, PCodeInfo.h (OpcodeInfo `rank` invented,
  used by selectinstruction), Registers.h (`coloring` SInt32 vs Pro7 int), Alias.h, CError.h, CompilerTools.h,
  CParser.h/CPrep.h, common.h: see above. Includes are now Pro7's list (PCodeInfo.h dropped: PCode.h includes it).
- Open: invented DGNode and MachineInfo member names (no source exists); headers' open items above.

### src/backend/MachineSimulation601.c: SOLID WITH TODOS
- Boundary: 0x57a880-0x57aaf0, 6 rows = Pro7's six statics in reverse (deferred); helpers inlined; Wii module
  MachineSimulation601 is the same file (later compiler, more out of line). .data 0x5d6678 (8-aligned) starts with
  `machine601`, then `instruction_timing`; .bss 0x5e40a8 is `pipeline`.
- Flags: deferred (rows descend), `-sym on` (`-sym off` loses serializes and can_issue).
- Functions: 6/6 exact. Departures: `MachineInfo` defined before the table with prototypes (2.6's data order,
  AGENTS.md); serializes through an `int stage` local (needed shape: Pro7's direct return 40.0, an `(int)` cast 75.0;
  a local copy, plausibly theirs).
- Behaviour: Pro7's bodies. 2.6 change: the table has 469 entries (Pro7 428): ISEL and the extra opcodes
  Unsupported. Bugs: none.
- Names: Pro7's statics, `#define`d to `m601_*` row names (row names are global). Invented: none.
- Types: Pro7's. Data: byte-identical (relink); all `static` but `machine601` (global, referenced by Scheduler; its
  binding is needed by the comparison to resolve Scheduler's reference). Table comments fixed: rows 236-238 were
  labelled `// 236`..`// 238`, now `PC_ISELGT/EQ/LT`; every row's comment checked against PCodeInfo.h's numbers.
- Header: none of its own (Scheduler.h). File top: includes now Pro7's (Scheduler.h, PCode.h, PCodeInfo.h; Registers.h
  and common.h were unneeded), prototypes needed by the MachineInfo that precedes the definitions.
- Headers relied on: Scheduler.h, PCode.h (`PCODE_FLAG_SET_F`, `fRecordBit`, `PC_LMW`...), PCodeInfo.h (`OPCODE_MAX`):
  see above.
- Open: the shared headers' items (MachineInfo/OpcodeInfo invented member names).
- Header round H2 (October 16): Scheduler.h and PCodeInfo.h are DONE WITH TODOS: `MachineInfo`'s members and OpcodeInfo's `operand_count`/`rank` stay TODO-invented (Pro7 has only x0/x4/x8, x8/x9; no source names them); PCode.h is DONE WITH TODOS (opcodes now Pro7's `Opcode` enum). Nothing else open.

### src/backend/MachineSimulation603.c: SOLID WITH TODOS
- Boundary: 0x5791d0-0x5796f0, 6 rows, Pro7's order reversed; .data 0x5d3970 (machine603, then the table), .bss
  0x5e3e48 = completionbuffers 0x38 + pipeline 0x40 (reverse declaration order).
- Flags: deferred (rows descend), `-sym on` (`-sym off` loses serializes, 20.0).
- Functions: 6/6 exact. Departures: none in the bodies (identical to Pro7's); MachineInfo first (data order).
- Behaviour: Pro7's; 2.6's table has 469 entries (the new ones BPU, 0 cycles). Bugs: none.
- Names: Pro7's, `m603_*` row defines. Invented: none.
- Types: Pro7's. Data: relink proves. Fixed: three stale fragment bindings deleted: `_data_005d06c5` (inside
  `instruction_timing`, 0x5d399d), `_data_005dfec8` (= completionbuffers), `_data_005dff00` (= pipeline); only this
  unit's exact functions referenced them. ISEL row comments fixed.
- Header: Scheduler.h. Includes now Pro7's.
- Headers relied on: as 601.
- Open: shared headers' items.
- Header round H2 (October 16): Scheduler.h and PCodeInfo.h are DONE WITH TODOS: `MachineInfo`'s members and OpcodeInfo's `operand_count`/`rank` stay TODO-invented (Pro7 has only x0/x4/x8, x8/x9; no source names them); PCode.h is DONE WITH TODOS (opcodes now Pro7's `Opcode` enum). Nothing else open.

### src/backend/MachineSimulation603e.c: SOLID WITH TODOS
- Boundary: 0x5796f0-0x579c60, 6 rows reversed; .data 0x5d4498 (machine603e), .bss 0x5e3ec0.
- Flags: deferred, `-sym on` (`off` loses serializes).
- Functions: 6/6 exact; bodies identical to Pro7's; MachineInfo first. (Wii's 603e is the later e300 model: not 2.6's.)
- Behaviour: Pro7's; table 469 entries. Bugs: none.
- Names: Pro7's, `m603e_*`. Invented: none.
- Types: Pro7's. Data: relink proves. Fixed: stale `_data_00582d40` binding (0x5e3f20, inside this unit's .bss)
  deleted; ISEL comments fixed.
- Header: Scheduler.h; includes now Pro7's. Headers relied on: as 601 (+ `is_dependent`, `RegClass_GPR` from common.h).
- Open: shared headers' items.
- Header round H2 (October 16): Scheduler.h and PCodeInfo.h are DONE WITH TODOS: `MachineInfo`'s members and OpcodeInfo's `operand_count`/`rank` stay TODO-invented (Pro7 has only x0/x4/x8, x8/x9; no source names them); PCode.h is DONE WITH TODOS (opcodes now Pro7's `Opcode` enum). Nothing else open.

### src/backend/MachineSimulation604.c: SOLID WITH TODOS
- Boundary: 0x579c60-0x57a240, 6 rows reversed; .data 0x5d5028 (machine604), .bss 0x5e3f38.
- Flags: deferred, `-sym on` (`off` loses serializes and can_issue).
- Functions: 6/6 exact. Departures: advance_clock counts the retirements in `stage` (needed shape: Pro7's separate
  `int i` 73.3; the catalogued reused walker); `sciu_completed_instruction`/`sciu2_completed_instruction` declared
  before `completionbuffers` (Pro7 after): 2.6's .bss order (statics are allocated in reverse; relink proves).
- Behaviour: Pro7's; table 469 entries. Bugs: none.
- Names: Pro7's, `m604_*`. Invented: none. Types: Pro7's.
- Data: relink proves; nothing bound inside the object but `machine604`. ISEL comments fixed.
- Header: Scheduler.h; includes now Pro7's. Headers relied on: as 603e.
- Open: shared headers' items.
- Header round H2 (October 16): Scheduler.h and PCodeInfo.h are DONE WITH TODOS: `MachineInfo`'s members and OpcodeInfo's `operand_count`/`rank` stay TODO-invented (Pro7 has only x0/x4/x8, x8/x9; no source names them); PCode.h is DONE WITH TODOS (opcodes now Pro7's `Opcode` enum). Nothing else open.

### src/backend/MachineSimulation750.c: SOLID WITH TODOS
- Boundary: 0x57a240-0x57a880, 6 rows reversed; .data 0x5d5b50 (machine750), .bss 0x5e4018.
- Flags: deferred, `-sym on` (`off` loses serializes).
- Functions: 6/6 exact. Departure: can_issue's `if (!isClear1) { IU1 } else { IU2 }` (needed shape: Pro7's
  `if (isClear1)` order 95.3; the catalogued inverted if/else).
- Behaviour: Pro7's code. 2.6 change in the table (data bytes): 469 entries, and the ISEL/AltiVec rows Pro7 has as
  BPU are FPU1 (0 cycles). Bugs: none.
- Names: Pro7's, `m750_*`. Invented: none. Types: Pro7's.
- Data: relink proves; only `machine750` bound. ISEL comments fixed.
- Header: Scheduler.h; includes now Pro7's. Headers relied on: as 603e.
- Open: shared headers' items.
- Header round H2 (October 16): Scheduler.h and PCodeInfo.h are DONE WITH TODOS: `MachineInfo`'s members and OpcodeInfo's `operand_count`/`rank` stay TODO-invented (Pro7 has only x0/x4/x8, x8/x9; no source names them); PCode.h is DONE WITH TODOS (opcodes now Pro7's `Opcode` enum). Nothing else open.

### src/backend/MachineSimulation821.c: SOLID WITH TODOS
- Boundary: 0x57aaf0-0x57aec0, 6 rows reversed; .data 0x5d71a0 (machine821), .bss 0x5e40d8.
- Flags: deferred, `-sym off` (the only model built so: `-sym on` loses serializes, 20.0).
- Functions: 6/6 exact; bodies identical to Pro7's; MachineInfo first.
- Behaviour: Pro7's; table 469 entries. Bugs: none.
- Names: Pro7's, `m821_*`. Invented: none. Types: Pro7's.
- Data: relink proves; only `machine821` bound. ISEL comments fixed.
- Header: Scheduler.h; includes now Pro7's. Headers relied on: as 601.
- Open: shared headers' items.

- Adversarial review: attacked every departure with `fn.py variants` and IDA's disassembly. Broke: scheduleblock's
  `&block` fakematch (the original's shape is a reused `node` in the build loop, Pro7's plain stores then match:
  exact) and the `(int)` cast on findsuccessors' r0 compare (unneeded, removed); the review's list of
  `scheduleinstructions` users (only CodeGen.c) and Scheduler.h's `renames_registers` comment (it is 1 for the
  603, 603e, 604, 750, 7400 and 7450: all but the 601 and 821) corrected. Held: the r13 cast (no cast or `r2 || r13`
  96.5), addsuccessor's early return without the clamp (no clamp in 2.6's code; Pro7's 90.1/92.6),
  selectinstruction's node2-first order (Pro7's 75.7, the reversed comparison 76.5), is_dependent's `def` local
  (`&b->args[0]` the same; assigned inside the flat condition 98.1), 601's `int stage` local (Pro7's 40.0, `Stage`
  local 37.5, `(UInt8)` 40.0, `(int)` 75.0), 821's `-sym off` (its serializes is 603's body, compiled edx/ecx as
  AGENTS.md's sym off), the boundary (Wii's Scheduler publics are the four externals at 0x507ba0..0x507c70's order;
  `is_same_operand` absent from both, so dead). After: 4020/4077 exact as before (nothing lost), all seven link.
- Header round H2 (October 16): Scheduler.h and PCodeInfo.h are DONE WITH TODOS: `MachineInfo`'s members and OpcodeInfo's `operand_count`/`rank` stay TODO-invented (Pro7 has only x0/x4/x8, x8/x9; no source names them); PCode.h is DONE WITH TODOS (opcodes now Pro7's `Opcode` enum). Nothing else open.

### src/driver/MsDos.c: SOLID WITH TODOS
- Boundary: 60 rows 0x4118f0-0x413920; every public name is Wii's MsDos module, in its order (Wii adds later ones:
  OS_Rename, OS_MakeSpecEx/FileSpecEx/PathSpecEx, OS_CloseLibrary, OS_CreateMutex, OS_MapFile/UnMapFile); the two
  statics sit where Wii's MsDos has unnamed statics (after OS_Rmdir, after OS_GetCWD). Last two rows are Pro7
  Posix.c's last functions (OS_LoadMacResourceFork, OS_IsMultiByte); MacSpecs starts right after. Assert file
  "MsDos.c" (`(long)ref < 0xffff`). .bss 0x5dd390 (8-aligned; the 4 bytes below are `intbuf[len - 1]`'s address).
  .data: the unit's own 312 bytes match; the 32 KB of zeros before them (0x599418) have no known owner (below):
  the PE's base relocations hold no address in 0x599418..0x5a1418 (the only one near is 0x5a1418, OS_GetErrText's
  literal), MSL's wctype.c compiles to exactly 0x658 bytes ending at 0x599418, and no Wii build has a 32 KB .data
  contribution (in MsDos or anywhere); "MsDos' compiled .data equals the original's range" holds only because the
  stand-in `unused_zeroes` is defined in MsDos.c (0x8138 = 0x8000 + 0x138), so it is not evidence of ownership.
  No dead-function literals missing (the object's .data matches byte for byte).
- Flags: not deferred (addresses ascend); `-sym off` shown to matter (with `-sym on` 44/60).
- Functions: 60/60 exact. No source text to follow (Pro7's Posix.c is only the API); departures checked:
  - this pass replaced decompiler shapes with the plain spelling wherever it also matched: `err = GetLastError();
    return err;` -> `return GetLastError();` (Status, Open, Read, Seek, SetSize, Delete, Mkdir, Rmdir, GetCWD,
    CloseDir, FreeHandle), `ok = F(); if (ok == 0)` -> `if (!F())`, dead `newpos`/`ok` assignments in OS_SetSize's
    condition dropped, OS_Mkdir's `char* buf = intbuf` alias, OS_ResolveLink's `src` copy, OS_Open's `h` and
    OS_OpenDir's `data`/`h` temporaries, `unsigned int eq` and the flag in OS_EqualSpec (now one `&&`),
    `(DWORD)-1` in OS_Tell, OS_SetFileTime's if/else pointer picks (ternaries), `0x144` -> `char path[324]` with
    `sizeof(path)`, OS_MakeFileSpec's `DWORD err; if (err > 0)` -> Pro7's `int err; if (err)`. Adversarial pass, all still
    exact: the `char* path = OS_*ToString(...); if (path == NULL)` temporaries in 8 functions (Create, Status,
    Open, Delete, Mkdir, Rmdir, IsFile, LoadMacResourceFork) -> the call tested directly; OS_Open's and OS_Seek's
    `err = SetFilePointer(...)` temporaries, OS_OpenDir's `err = GetLastError(); return err;`, `isalpha(..) != 0`,
    `WriteFile/CloseHandle/SetStdHandle/SetEndOfFile(..) == 0`, `ok == 0`, `isfile != 0` and OS_IsMultiByte's
    `lead == 0`/`!= 0` -> plain tests.
  - OS_GetDirPtr's redundant `if (*path == OS_PATHSEP) return path;` and OS_MakePathSpec's dead `isfile = FALSE`:
    needed shapes (removed: 92.8% and 98.7%), original's shape (dead code a programmer left), commented.
  - `(long)ref` casts in OS_RefToMac: original's shape (the assert string in .data is `(long)ref < 0xffff`; the
    uncast first test 87.8%, a single `ref < 0` 80%).
  - OS_GetTime's `*p = *(OSTime*)&ft`: original's shape (OSTime is its own tag, Wii's `??4OSTime`; field copies
    and passing `(FILETIME*)p` compile otherwise).
  - OS_NewHandle's `len` copy (TODO, workaround): `int`/`unsigned int`/`size_t` all match; plain `size`,
    `(size_t)size`, `hand->used`, separate stores and an address local tried (89-96%).
  - OS_IsFullPath's flag variables: needed shape (read like expanded macros; one `||` expression 88%, an if chain
    50%, inline helpers 88%); comment added.
  - OS_Write's signed `pos`/`chunk`: needed shape (comment: the dead `jmp` after the last `ret`).
- Behaviour: exact code, so constants, callees and literals are the original's; Win32 error names checked against
  values (ERROR_BUFFER_OVERFLOW 111, ERROR_DIRECTORY 267, ...). Bugs in the original (kept): OS_IsFullPath passes
  `strlen(cygdrive_prefix) == 0` as ustrncmp's length; OS_ReadDir clamps a long alternate name with `len > 256`
  (to 255); OS_MacToTime's leap test is `wYear % 4` only. Bugs of ours: none found.
- Names: Wii's for every public; Pro7's for the API. Invented (TODO-marked): `ensure_trailing_backslash`,
  `redirect_standard_handle_to_file` (Wii statics unnamed), `unused_zeroes`.
- Types: Pro7's oslib.h types at 2.6's sizes; Win32 types now WINBASE spellings in this unit (`SYSTEMTIME`,
  `struct _SECURITY_ATTRIBUTES`, `struct _STARTUPINFOA` instead of win32.h's `_SYSTEMTIME`-style typedefs).
  OS_GetMilliseconds is cdecl in the repo, `@0` in Wii: undecidable from code.
- Data: relink proves .data/.bss; statics `static`; `OS_TEXTTYPE` is a COMMON (bound; users in Files.c,
  MacFileTypes.c). No static binding.
- Header: OS_EqualNameSpec (no other user) moved out to a file-top prototype (OS_EqualSpec uses it first);
  calling-convention comment corrected. Everything else in MsDos.h has users.
- Headers relied on: common.h (types); compiler/win32.h (Win32 API and error codes: Ghidra-generated in places:
  `typedef struct _SECURITY_ATTRIBUTES _SECURITY_ATTRIBUTES, *P_SECURITY_ATTRIBUTES`, `_STARTUPINFOA`, no WINBASE
  `SECURITY_ATTRIBUTES`/`STARTUPINFOA` names, an `IMAGE_SCN_*` "SectionFlags" enum, tool offset comments on
  PROCESS_INFORMATION ("0x00: MsDos.c: ..."), a `/* */` group style mix); driver/oslib.h (OS types, OS_ASSERT:
  no `Ptr` typedef, else clean, pack balanced); driver/CLIO.h (only for `__assertion_failed`, which is MSL's
  assert.c function declared in CLIO.h and drags MsDos.h/Memory.h/cmdline.h in); driver/StringExtras.h (ustrcmp,
  ustrncmp: `strcatn`'s `char* src` is not const); compiler/types.h. Not reviewed in full: cmdline.h, Memory.h.
- Open: the 32 KB zero block's owner (MsDos' unused static or a data-only object: no evidence either way);
  OS_NewHandle's TODO; headers above.

### src/driver/MacSpecs.c: SOLID WITH TODOS
- Boundary: 15 rows 0x413920-0x4143c0; Pro7 MacSpecs.c's functions in Pro7's order, the publics Wii's MacSpecs
  (Wii adds MacSpecs_Initialize/Terminate later and lacks the two 2.6 helpers); asserts "MacSpecs.c" lines 166/267
  (`dirID != 2`, `*pb == OS_PATHSEP`). .data 0x5a1550 and .bss 0x5dd598 8-aligned; .bss = 4+1024+4+64+260+260+256
  = Wii's 1872. Pro7's dead OS_VolDir_To_OSNameSpec added back (marked dead; no data of its own, like the group's
  other kept dead Pro7 functions).
- Flags: not deferred (ascending); `-sym off` matters (`-sym on` 12/15).
- Functions: 15/15 exact. Departures from Pro7: 2.6 changes (all in the code): errno -> Win32 codes, 260/64
  sizes, `"RESOURCE.FRK"` + SetFileAttributesA (Pro7's own comment says Windows has it), OS_OSSpec_To_FSSpec
  shortening names >= 64 via OS_ShortenSpec, OS_FSSpec_To_OSSpec ending in OS_CanonNameCase. Shapes:
  FindOrAdd's `(Boolean)(ch == 0)` cast replaced by a `Boolean last = ch == 0;` local set before `*ptr = 0`
  (matches; Pro7's `if (!ch)`, `ch == 0`, `!(ch != 0)`, an `int` flag, `!ch` into a Boolean all fail, as does Pro7's
  `if (!ch)` with `ch` char/signed/unsigned char/short/int/SInt32/unsigned, 94.3-97.6%: original's
  shape, a flag); the inner `ch != OS_PATHSEP` test (Pro7's `*ptr` 82%) and StepVolDir's else-if chain (Pro7's
  early return 94%) are needed shapes.
- Behaviour: exact; nothing found.
- Names: Pro7's; invented (TODO-marked now): OS_ShortenSpec, OS_CanonNameCase (2.6 additions, no source; no Wii
  module names them). Yes/no returns of AddDirMapEntry, FindOrAdd, ResolvePath, ResolveVolDir now TRUE/FALSE.
- Types: Pro7's DirNode/VolNode; FSSpec from Files.h.
- Data: relink proves it; statics `static` in reverse Pro7 order (comment); bindings `_dirIDMap`, `_lastDirID`,
  `_stname` deleted (only this unit's exact functions reference them; symbols.txt still names them from the object).
- Header: OS_OSPathSpec_To_VolDir and OS_VolDir_To_OSPathSpec dropped (no other users; defined before use).
- Headers relied on: oslib.h, MsDos.h, Generic.h (STSbuf, OS_GetFileNamePtr, OS_MakeSpec2), Files.h (FSSpec, pack(2)
  balanced), StringUtils.h (c2pstrcpy/p2cstrcpy: `unsigned char*` vs `const UInt8*` inconsistent; HPrintF on the
  `struct StorageHandle*` stand-in for Pro7's Handle), CLIO.h (`__assertion_failed`, as above), win32.h (as above).
- Open: invented names (no source to take them from) and the headers above.
- Header round H2 (October 16): StringUtils.h's c2pstrcpy/p2cstrcpy are Pro7's `StringPtr`/`ConstStringPtr`; HPrintF takes Pro7's `Handle`; `__assertion_failed` is oslib.h's; win32.h is WINDEF/WINNT/WINBASE's typedefs (Driver low level). All resolved; the unit's own invented names keep their TODO.

### src/driver/Generic.c: SOLID
- Boundary: 12 rows 0x419270-0x419e50 = Wii's Generic module less OS_GetDirName (dead in 2.6, kept from Pro7) and
  the later OS_MakeRemoteSpec; Pro7 Generic.c order. Dead OS_CompactPaths kept for its `"%s%s"` in .data. .data
  0x5a1c20/.bss 0x5de328 8-aligned.
- Flags: not deferred; `-sym off` matters (`-sym on` 10/12).
- Functions: 12/12 exact. 2.6 changes: Win32 sizes (260/256 buffers, 259/255 statics), case-insensitive
  `tolower` where Pro7 has `dummyfunc`, `"..\\"`, `';'`/`";,"` path lists, OS_FindProgram's .exe suffix and
  system/Windows directories, ERROR_BUFFER_OVERFLOW. WildCardMatch made `static` (Wii's Generic has it with no
  public; still exact; returns FALSE now) and given Pro7's final else-if; needed shapes: `next = *++wild` (Pro7's two statements 88%)
  and the `*name == 0 || ...` return (Pro7's `!name[0]` 94%), a `dummyfunc` inline returning tolower (76-98%).
  OS_FindFileInPath now Pro7's `if (!OS_MakeSpec2(...)) { if (!OS_Status(spec)) ...` and declaration order (both
  match); its `plist = *next ? next + 1 : NULL` ternary and `return 0` are needed (Pro7's if/else 97%, `err = 0`
  97%). OS_GetFileNamePtr's if/else (Pro7's ternary 85%), ChangeExtension's `*per = '.'; per++; *per = 0;` (Pro7's
  99%), SpecToStringRelative's `full--; cwd--;` (Pro7's order 99.5%): needed shapes.
- Behaviour: exact. Original's bug kept: OS_NameSpecChangeExtension/SetExtension pass 260 for a 256-byte buffer
  (Pro7 passes 256 for 64).
- Names: Pro7/Wii; none invented.
- Types: Pro7's.
- Data: relink proves it; statics `static` (reverse Pro7 order); STSbuf is the COMMON Wii/Pro7 have. Bindings
  `_wildname`, `_wilddir`, `_wilddirref`, `_wildmatch` deleted (only OS_MatchPath uses them).
- Header: WildCardMatch and OS_FindFileInPath dropped (no other users); `/* */` comment made `//`.
- Headers relied on: oslib.h, MsDos.h, StringExtras.h (strcpyn, strcatn, ustrcmp; `strcatn` src not const),
  win32.h (GetSystemDirectoryA/GetWindowsDirectoryA; problems above).
- Open: headers only.
- Header round H2 (October 16): StringExtras.h's `strcatn` src is `const char* s`; win32.h is WINDEF/WINNT/WINBASE's typedefs. All resolved.

### src/driver/LowMem.c: SOLID
- Boundary: 2 rows 0x41a4f0-0x41a520 (inside the old CLFileOps range), Pro7 MacEmul/LowMem.c; its dead
  LMGetTime/LMSetMemErr kept. .bss 2 bytes at 0x5de840 (memErr).
- Flags: not deferred; `-sym` makes no difference here (both 2/2), kept like its neighbours.
- Functions: 2/2 exact, Pro7's text verbatim.
- Behaviour/Names/Types: Pro7's.
- Data: relink proves it; `memErr` static. Its `_memErr` binding is kept: the object's 2-byte .bss is too small for
  layout.py to place, and without the binding symbols.txt calls it `lbl_005DE840` (tried).
- Header: LMGetTicks (CLMain, COSToolsCLT), LMGetMemErr (COSToolsCLT): both used.
- Headers relied on: oslib.h (OSErr), MsDos.h (OS_GetMilliseconds), common.h: no problems for what it uses.
- Open: none.

### src/driver/FileHandles.c: SOLID
- Boundary: 5 rows 0x421950-0x421c00 = Wii's FileHandles module (two unnamed statics + three publics); Pro7
  OSLib/FileHandles.c, its dead OS_UnlockFileHandle/OS_GetFileHandleSpec kept. No .data/.bss.
- Flags: not deferred; `-sym off` matters (`-sym on` 4/5).
- Functions: 5/5 exact (Boolean fields now set TRUE/FALSE), Pro7's text with Win32 codes (EACCES -> ERROR_INVALID_ACCESS 12, ENOMEM ->
  ERROR_NOT_ENOUGH_MEMORY 8, checked in the code) and `__stdcall` (Wii's `@N`).
- Behaviour: exact; nothing found.
- Names: Pro7's.
- Types: OS_LockFileHandle's `char** ptr` is Pro7's `Ptr*` (the project has no `Ptr` typedef).
- Data: none.
- Header: the three publics, each used (CLIO, Arguments, CLCompilerLinkerDropin_V10).
- Headers relied on: oslib.h (OSFileHandle, OSHandle, OSSpec: no `Ptr`), Generic.h (OS_CopyHandle), MsDos.h.
- Open: `Ptr` in oslib.h (header owner).
- Adversarial review: attacked the boundary (Wii 0x4201_127/1.0RC1/1.0/1.1 module tables: MsDos' two invented
  statics and Generic's/FileHandles' unnamed statics are unnamed in every Wii build; Wii's other unnamed MsDos
  statics are later link support 2.6 lacks), the 32 KB zero block (claim holds: no relocation into it, not
  wctype's, absent in Wii; the "MsDos' compiled .data equals the range" finding is circular, made by the stand-in),
  OS_GetMilliseconds' convention (Wii's `@0` cannot decide it: Wii also decorates OS_CanonPath `@8` and OS_MacToRef
  `@4`, cdecl in 2.6), header users (every declaration in the five headers has a user outside its unit; no private
  prototype copies), and every unusual construct with `fn.py variants`. Broke: 10 decompiler temporaries and ~15
  non-plain tests in MsDos were not needed (rewritten plain, all exact); boolean 1/0 in FileHandles, MacSpecs and
  Generic (now TRUE/FALSE). Held: OS_RefToMac's casts, OS_GetDirPtr's redundant test, OS_MakePathSpec's dead store,
  FindOrAdd's `last` flag, the documented TODOs (OS_NewHandle, OS_IsFullPath, OS_Write), LowMem's `_memErr` binding
  (naming only; only exact LMGetMemErr references it). Grades unchanged. 4020/4077 exact before and after (nothing
  lost anywhere); all five relink; layout.py, clang-format and clang_check clean.
- Header round H2 (October 16): `Ptr`/`Handle` are Pro7's typedefs in common.h, and OS_LockFileHandle takes Pro7's `Ptr*` (FileHandles.h DONE). Resolved.

### src/backend/Registers.c: SOLID
- Boundary: 0x4fe310..0x4fe6a0 (between COptimizer and CMangler), 8 rows. Wii's Registers module lists the same functions in the same order (plus later is_scratch/is_nonvolatile/available_registers_mask, and retain_register, which 2.6 has in RegisterInfo like Pro7). The rows' reverse is Pro7's order. 2.6 has no Registers .bss, so Pro7's `mod_regs_table` and the dead *_modified_registers, count_scratch_registers and retain_register_for_argument functions are not in it; none of them has literals. No asserts, so there is no file-name string.
- Flags: changed to `-inline auto,deferred` (was plain `auto`, with the source in binary order). The functions descend in address against Pro7's file, and the source is now in Pro7's order. Pro7 order without deferred gives 7/8, deferred gives 8/8. `-sym off` changes nothing here, so `-sym on` (the default) stays.
- Functions: 8/8 exact, and every body is Pro7's text except check_temp_registers. That one is a 2.6 change (an added loop that reports error 219 when a class has more than 0x7FFF virtual registers) plus a needed shape: an early `return` and an `int r` copy of the counter. Pro7's `if (!optimizing)` form is 91.2%, either change alone 94-97%. assign_register_to_variable's `short reg` and obtain_nonvolatile_register's `short` return are Pro7's.
- Behaviour: the bodies are Pro7's; the 219 check was confirmed in the bytes. No bugs found.
- Names: all Pro7's, also confirmed by Wii's publics. Invented: none.
- Types: Pro7's, except the globals, which are `SInt32` where Pro7 has `int`. AGENTS.md records that 2.6 declares them `long`; an `int` array moves registers.
- Data: no .data or .bss of its own. Its globals are COMMONs, now defined here as in Pro7 (they had no definition in any source; the split supplied them); still bound for lifted code. The unit links, and so does the whole relink.
- Header (Registers.h): dropped `first_temporary_register` (only this unit uses it). Fixed `available_registers(RegClass)` (was `signed char idx`) and `short obtain_nonvolatile_register`. Removed the 1.x leftovers no unit uses: VarInfo's union of 1.2.5 overlay names (`in_param_area`, `is_fpr`, `is_vector`, `unused24`; now Pro7's plain `flags`, `rclass`), `Registers_BindFPR`, `REGISTERS_BIND_GPR/VR`, and the COptCSE prototypes `Registers_GetOperandRegMask`, `Registers_InvalidateCSE`, `Registers_GetCSEWeight`, `Registers_ContainsCOptCSE`, `Registers_DivideUses`. Coloring's `(short)` cast on the call went too.
- Headers relied on: Registers.h (VarInfo, RegState*, RegisterMax, the globals). Open there: VarInfo lives here where Pro7 has it in objects.h, and its `deftoken` is a BufferedToken (Pro7: TStreamElement, hence the CPrep.h include). The ALLOC_GPR/FPR/VR macros are here where Pro7 has them in RegisterInfo.h. Also: PPCError.h (PPCError_ErrorTerm, PPCErrorStr219), RegisterInfo.h (init_target_registers, retain_register, Registers_GetVarInfo), common.h (RegClass). None of these reviewed in full.
- Open: the header placement items above.
- Header round H2 (October 16): Registers.h/RegisterInfo.h are DONE: VarInfo lives in objects.h (no BufferedToken), the ALLOC_* macros in RegisterInfo.h, the never-defined Registers_* prototypes are gone; PPCError.h is DONE. All resolved.

### src/backend/RegisterInfo.c: SOLID WITH TODOS
- Boundary: 0x4cfde0..0x4d1040, 23 rows, all in Wii's RegisterInfo module (Wii adds later machine_specific_* functions before them). The asserts say "RegisterInfo.c". Reversed, the order is Pro7's, with 2.6's Registers_GetRegInfo, is_global_registers_variable, setup_global_registers and finish_global_registers (Wii publics) where the binary has them. Pro7's used_vrstate_VRs and is_nonvolatile_register are dead and have no data, so their absence cannot be checked. .data starts at 0x5b9a50 with spr_to_sysreg (8-aligned). .bss 0x5e0ad0..0x5e0af2 is save_state[32] then used_regs_before_coloring.
- Flags: deferred (the functions descend against Pro7's order). `-sym on`: with `-sym off` 17/23.
- Functions: 23/23 exact. Departures from Pro7:
  - 2.6 changes, each seen in the bytes:
    - GetABIFirstNonVolatile gives 14 for GPRs (EABI: r13 is the small-data base); init_target_registers adds "SDA" and `reg_state[GPR][13]`.
    - is_register_object also accepts a global register variable.
    - assign_register_by_type has soft-float GPR and paired-single FPR branches.
    - Registers_GetVarInfo has no DNONLAZYPTR case (the switch is 0..2).
    - first_nonvolatile_reg is Pro7's static grown into the global-register-variable binder (asserts 279, errors 202/204/199). Its name is Pro7's, by place and by init_target_registers' `end = first_nonvolatile_reg(rclass)`. Wii has it unnamed (static).
  - Needed shapes (natural spelling tried):
    - asm_used_register: an early `return` plus a `long i` counter (Pro7 91.7%, the `int` early-return form 93.8%).
    - retain_register: the assert as `reg < 32`. 2.6 compares the short (`cmp bp, 0x20`); Pro7's `(short) reg < RegisterMax` compares an int (80%).
    - colored_vrs_as_vrsave: a `for` with its own block walker (Pro7's `while (block)` 86.8%).
    - first_nonvolatile_reg: `count` as the clearing loop's index, and `used[]` indexed through a copy `base` of `first` (93-97% otherwise). This is 2.6 code with no reference, so a plausible original shape.
  - Original's shapes: open_fe_temp_registers now has Pro7's `r` local.
  - assign_register_by_type's comment said "-operandsDebug"; it is `copts.soft_float` (a float in a GPR, a double in a GPR pair) and paired singles in FPRs. Fixed.
- Behaviour: checked first_nonvolatile_reg against IDA statement by statement (the regHi = 0 store is folded into one dword store; error ids, the GetRegisterClassName inlining). Bugs of ours fixed:
  - `copts.altivec_vrsave` was read through an offset macro, `data_005e70f5` = `*(UInt8*)((char*)&copts + 0x13)`.
  - Registers_GetRegInfo returned `char*` with `info[1]` for `->reg`. It now returns `ObjRegInfo*` and DWARF.c's cast is gone.
  - Genuine bugs in the original: none found.
- Names: the statics took Pro7's `save_state` and `used_regs_before_coloring` (were `data_005dca70`/`data_005dca90`, with bindings). Locals now follow Pro7 (`pc`, `flag`). Invented: none in the .c. In the header, the `RegInfo` type name and its `nspace`/`is_static` members carry TODOs; the list's name `reg_info_list` is Wii's.
- Types: Pro7's. Magic numbers became names: VarInfoFlag2/4, RegState0/1/2, RegisterMax, STRUCT_VECTOR_UCHAR..PIXEL, fOpTypeVR, PCOp_REGISTER. Booleans are TRUE/FALSE.
- Data: .data and .bss link byte for byte. The statics are `static` in Pro7's order (save_state lowest). Pro7's COMMON definitions (`last_exception_register`, `first_fe_temporary_register`, `last_argument_register`, `_FP_`, `_CALLER_SP_`, `special_register_names`) are now defined here; none had a definition except `last_argument_register`, which CodeGen.c also defines among its pile of other units' COMMONs (left for CodeGen's owner). Deleted GC_2_6's bindings `_data_005dca70` and `_data_005dca90` (only this unit references them). spr_to_sysreg's binding stays, because lifted InlineAsmPPC, PCodeAssembly and PCodeInfo code reads it.
- Header (RegisterInfo.h): first_nonvolatile_reg is now `static` and out of the header (Pro7 has it static). Removed this file's private `extern`s (disable_optimizer, high_reg/low_reg/high_reg2/low_reg2: they come from CodeGen.h). Includes cut from 51 to Pro7's set (CError, CodeGen, CompilerTools, CParser, objects, PCode, PPCError, types, string.h); every version unchanged.
- Headers relied on:
  - RegisterInfo.h: RegInfo has an anonymous union aliasing ObjRegInfo with `rclass`/`reg`, and invented member names (TODO).
  - CPrep.h: COpts' `altivec_vrsave`, `littleendian`, `soft_float`, `ignore_global_reg_vars`, `optimizationlevel`. COpts still has placeholder `pad01`/`pad0b`/`pad15` and TODO-named members.
  - CodeGen.h: high_reg and the rest, disable_optimizer. SFPE_PPC_EABI.h duplicates `low_reg`/`high_reg`/`low_reg2`/`high_reg2` with mixed `short`/`SInt16`.
  - objects.h: u.data.info and reginfo, u.var.info. `islocalstatic` is still a TODO.
  - common.h: ObjRegInfo `char rclass, reg`.
  - Also used: types.h, PCode.h, PPCError.h, CParser.h (TK_REGISTER).
- Open: the RegInfo TODO names (no Pro7 or Wii source to take them from).

### src/backend/LiveVariables.c: SOLID WITH TODOS
- Boundary: 0x57aec0..0x57b4b0 (dead, computelivevariables, computeglobalinout, computelocalusedef). Reversed, that is Pro7's LiveVariables.c; allocateliveinfo is inlined. Wii rewrote the unit as LiveRegisters. No data of its own; `liveinfo` is a COMMON.
- Flags: deferred (descends against Pro7's order). `-sym on`: with `-sym off` 3/4.
- Functions: 4/4 exact.
  - 2.6 changes: computelivevariables keeps the global register variables live at the last block, and returns small structs in r3/r4, soft-float values in r3/r4 and paired singles in f1.
  - Needed shapes: computeglobalinout's `SInt32` blockIndex/i and its declaration order (Pro7 70.7%, ints 96.7%). dead's `if (optimizationlevel <= 0) return FALSE; return TRUE;` (Pro7's `return copts.optimizationlevel > 0;` 92.2%).
  - Made computelocalusedef and computeglobalinout `static` (Pro7; still exact).
- Behaviour: Pro7's plus the 2.6 branches, which are exact. No bugs found.
- Names: Pro7's. Invented: none.
- Types: Pro7's LiveInfo.
- Data: none. Links.
- Header (LiveVariables.h): Pro7's LiveInfo.h under the unit's name. It declares only liveinfo, computelivevariables and dead (used by InterferenceGraph and SpillCode).
- Headers relied on: BitVectors.h, Coloring.h (coloring_class), PCode.h (the PC_OP_IS_* macros, PCLink, depthfirstordering), RegisterInfo.h (reg_info_list, RegInfo: see above), CMachine.h (Type_RequiresMemoryReturn), types.h (TYPE_* macros), CompilerTools.h (oalloc). PCode.h, types.h and CMachine.h were not reviewed.
- Open: headers only.
- Header round H2 (October 16): LiveVariables.h and BitVectors.h are DONE (Optimizers); PCode.h is DONE WITH TODOS; types.h is reviewed (Types family); CMachine.h has no stand-in left (Pro7's `Float` signatures). RegisterInfo.h's `RegInfo` type and member names stay TODO-invented. Nothing open.

### src/backend/InterferenceGraph.c: SOLID WITH TODOS
- Boundary: 0x57b4b0..0x57c390 (6 rows; the old filing had LiveVariables' four rows here). In reverse, Pro7's order; makeinterfere, interferes and coalesced_path are inlined. Wii's InterferenceGraph module agrees. .data 0x5d7cc8 is "InterferenceGraph.c" (20 bytes). .bss 0x5e4148..0x5e4150 is `coalesced` then `interferencematrix`. Pro7's `Boolean coalesced_nregisters` has no trace in 2.6: no reference, no Wii public.
- Flags: deferred. `-sym on`: with `-sym off` 1/6.
- Functions: 6/6 exact. Departures:
  - 2.6 change: PC_DCBZ_L (Gekko) joins buildinterferencematrix's cache-instruction cases.
  - eliminatedeadcode: Pro7's `continue` after deletepcode is restored (it matches).
  - Needed shapes:
    - buildinterferencematrix and eliminatedeadcode: `bitvectorclearbit(reg = op->data.reg.reg, vec)` with a function-level `short reg` (Pro7's block `int reg` 74.2% / 52.5%). In eliminatedeadcode's write loop `reg` is set but not read. That is the original's shape (a copy of buildinterferencematrix's loop, where `reg` is used); the plain field is 85.6%.
    - coalescenodes: one negated range test for Pro7's four `continue`s (catalogued).
    - buildadjacencyvectors: `node->degree = node->arraySize` (Pro7's `counter` 98.8%). Its declaration order: Pro7's 81.2%.
- Behaviour: Pro7's; the range test is equivalent to Pro7's four tests. No bugs found.
- Names: functions and statics are Pro7's. Invented: IGNode's members `instr`, `reg`, `degree`, `color` (TODO in the header; Pro7 has placeholders `instr8`, `x10`, `x12`, `x14`).
- Types: Pro7's, with IGNode packed to 2 (Pro7: mac68k).
- Data: .data and .bss link; the statics are `static`. No bindings for them.
- Header (InterferenceGraph.h): Pro7's, less `coalesced_nregisters`. Removed stale `// r30` register comments from the source.
- Headers relied on: InterferenceGraph.h (IGNode TODO names), BitVectors.h, LiveVariables.h, Coloring.h, RegisterInfo.h (`_CALLER_SP_`, first_fe_temporary_register), Registers.h (last_temporary_register), PCode.h (flags, PCOp_PLACEHOLDEROPERAND, PCODE_FLAG_SET_F), PCodeListing.h (pclistinterferences), PCodeUtilities.h (deletepcode, branch_count_volatiles, is_location_independent), CError.h, CompilerTools.h.
- Open: the IGNode member names (no source names them).

### src/backend/Coloring.c: SOLID WITH TODOS
- Boundary: 0x508610..0x508d30, 5 rows. Reversed, Pro7's file; Wii's Coloring module has colorinstructions plus three unnamed statics. The asserts say "Coloring.c" (.data 0x5bdc88). .bss 0x5e18a0 is the 2-byte static.
- Flags: deferred. `-sym on`: with `-sym off` 3/5.
- Functions: 5/5 exact. markspecialregisters, simplifygraph, colorgraph and rewritepcode are now `static`, as in Pro7. Departures:
  - 2.6 changes:
    - markspecialregisters: FATAL(58) for FPRs under soft float, plus a loop over reg_info_list.
    - colorgraph: FATAL(278) when a global register variable would be spilled.
    - colorinstructions: FATAL(375) when there are soft-float FPR virtuals.
  - Needed shapes (original's, Ninji's text being a guess):
    - colorinstructions: `flag = !colorgraph(simplifygraph());` (the reviewer's `flag = FALSE; if (!...) flag = TRUE;` was a needless spelling; both exact; Pro7's ternary 93.3%, an if/else 95.9%).
    - colorgraph reads `otherNode->color` twice (Pro7's `reg` copy 95.7%).
    - rewritepcode stores `node->color` directly (Pro7's `reg = ...` 75.0%).
    - simplifygraph's declaration order (Pro7's 94.3%).
- Behaviour: no bugs found. Fixed: a redundant `(Object*)` cast, and the `(short)` cast on obtain_nonvolatile_register.
- Names: Pro7's. Invented: none.
- Types: magic numbers became names: VarInfoFlag2/4, fPairLow/High, fPushed, fCoalesced, fSpilled, fIsMove, RegClass_FPR/GPR. Booleans are TRUE/FALSE.
- Data: links. Deleted GC_2_6's `_used_regs_before_coloring` binding: only this unit's functions reference it, and the name is also RegisterInfo's static (Pro7 has both).
- Header (Coloring.h): coloring_class and colorinstructions, both used elsewhere.
- Headers relied on: Registers.h and RegisterInfo.h (see above), InterferenceGraph.h, SpillCode.h (estimatespillcosts, insertspillcode), StackFrame.h (check_dynamic_aligned_frame), CFunc.h (arguments, locals), PCode.h, PPCError.h, CompilerTools.h. Not reviewed: SpillCode.h, StackFrame.h, CFunc.h.
- Open: headers only.
- Header round H2 (October 16): Registers.h/RegisterInfo.h are DONE; SpillCode.h DONE; StackFrame.h DONE WITH TODOS; CFunc.h is Pro7's; InterferenceGraph.h is DONE WITH TODOS (IGNode's member names stay TODO, Pro7 has placeholders). Nothing open.

### src/backend/BitVectors.c: SOLID WITH TODOS
- Boundary: 0x57d150..0x57d5a0, 11 rows; Wii's BitVectors module has the same functions. Reversed, the order is Pro7's copy, changed, initialize, intersect, union, intersectionisempty, with 2.5's range functions after initialize and the compressed vectors last. Pro7's dead bitvectorcount is there: its static `nbits` popcount table is this object's .data (0x5d7d60..0x5d7e60, 8-aligned after SpillCode's 124 bytes; it had been in SpillCode's range), placed by `data_at` (Y_BE1 adversarial review). The dead difference, complement and isempty functions cannot be checked. .bss 0x5e4158..0x5e4164 holds three statics, as Wii's 12 bytes do.
- Flags: deferred (descends against Pro7's order).
- Functions: 11/11 exact. Rewritten from the decompiler-style bodies (`empty`/`equal`/`more` locals, `wordCount = wordCount - 1`, `(int)count >> 5`) into one plain shape, with Pro7's parameter names. Needed shapes:
  - 2.6's loops are `while (i != 0) { ...; dst++; src++; i--; }`. Pro7's `while (i--)` with `*(dst++)` is 13.8-65.3%.
  - bitvectorinitialize: the reviewer's local copies of both parameters were a fakematch caused by a wrong type: `initval` is Pro7's `UInt32`, not `unsigned int` (the header's prototype). With `UInt32` the plain loop matches (also as a `for`, or `*vec++`); Pro7's `while (i--)` is still 20.7%.
  - bitvector_compress sets `i = 0` outside the `for` (99.3% inside).
  - The range word count is now written at both uses instead of an invented inline `BitRange_WordCount` (both match).
- Behaviour: checked bitvector_compress against IDA. Genuine bug in the original: its free-list search never advances `scan`, so a size mismatch would loop forever. It is harmless because nothing ever adds to the list. Bugs of ours: none.
- Names: Wii's and Pro7's, and the COMMON `bvc_bytes_used` is Wii's. Invented (TODO): the types BitVectorRange, CompressedBitRun, CompressedBitVector and FreeCompressedBitVector with their members, and the statics free_compressed_bitvectors, compress_buffer, empty_compressed_bitvector. Wii's statics are unnamed.
- Types: the prototypes keep 2.6's `SInt32` lengths; callers depend on them (AGENTS.md).
- Data: .bss links, `static`, in 2.6's order (empty_compressed_bitvector lowest). The COMMON `bvc_bytes_used` is now defined here (2.5+, it had no definition) and out of the header (no other user).
- Header (BitVectors.h): prototypes now use Pro7's parameter names. The range bit helpers are inline there (TODO invented names).
- Headers relied on: BitVectors.h (the TODO names above), CompilerTools.h (oalloc, memclrw).
- Open: the invented type and static names (no source).

### src/driver/Memory.c: SOLID
- Boundary: 0x4143c0-0x4146a0, 15 rows, all Pro7 MacEmul/Memory.c in Pro7 order. Wii module Memory has the same functions; its unnamed 11-byte function at 0x42e770 is FixHandlePtr, a static in Pro7. TempNewHandle, HLockHi and HandAndHand are not in Wii but are Pro7's, and 2.6 callers use them (COS_NewOSHandle, COS_LockHandleHi, CLFiles_CreateChainRecord). .data 0x5a15d8 starts 8-aligned and holds only `"%*.*s"`, the literal of the dead DebugStr, which is kept (placed by `data_at`). .bss is 2 bytes: `memError`. Pro7's other dead functions have no data in 2.6 and are left out.
- Flags: `-O4,p -sym off`. The unit is not deferred: its functions ascend in address. With `-sym on`, SetHandleSize loses its match.
- Functions: 15/15 exact. Departures from Pro7:
  - FixHandlePtr is `static`, as in Pro7. It used to be global, with a header prototype no other unit uses.
  - SetHandleSize and PtrAndHand take `&h->hand` once, into `oh`, assigned inside the call. This is a needed shape: Pro7's three `&HS(h)->hand` and an initialised `OSHandle* oh = &h->hand` both reach 91/71%. I class it as the original's shape (a plain local copy), with a comment.
  - HandAndHand now uses Pro7's `size` local and PtrAndHand Pro7's `OSErr err`. Both are back to Pro7's spelling and still exact.
- Behaviour: Pro7's code. 2.6 has `OS_OSErrorToMacError` where Pro7 has `OS_MacError`, and the stdcall/cdecl split matches the `ret`s. Bug of ours removed: a `#define free xfree` hack with `#undef` around the only `free`. It was dead but misleading.
- Names: Pro7's and Wii's. None invented.
- Types: `StorageHandle` stands in for Pro7's `Handle`, a project-wide choice (AGENTS "Left there"). I removed its stand-in union member `struct BufferedToken* tokens`, which nothing used.
- Data: links. `memError` is `static`, and its binding `_memError` is deleted.
- Header: declares only what other units use (FixHandlePtr's prototype removed). `win32.h`, `MemUtils.h` and `string.h` dropped from the source.
- Headers relied on: oslib.h (OSErr, OSHandle, error enum, ConstStringPtr): fine. MsDos.h (OS_* handle functions, OS_OSErrorToMacError): fine. Generic.h (OS_AppendHandle): fine. Memory.h: `StorageHandle` vs Pro7's `Handle`/`char**`, with `data` where Pro7 has `addr`, is open project-wide.
- Open: `StorageHandle` as Pro7's `Handle` (header-wide, not this unit's code).
- Header round H2 (October 16): Pro7's `typedef char* Ptr; typedef Ptr* Handle;` (common.h) replaces `StorageHandle` everywhere (Driver low level). Resolved.

### src/driver/TextUtils.c: SOLID
- Boundary: 0x415620-0x415830, 4 rows (c2pstr, p2cstr, GetIndString, getindstring), Pro7 order. Wii module TextUtils has three of them; getindstring is Pro7's, and CLGetErrorString and CLPGetErrorString call it. .data 0x5a1638 starts 8-aligned: the "[Resource string id=%d index=%d not found]" literal, then `"%d"` from the dead NumToString/StringToNum, which are kept. Pro7's other dead functions (CharacterByteType, CharByte, GetDateTime, EqualString) have no data and are kept as Pro7's file.
- Flags: `-O4,p -sym off`, not deferred (functions ascend). `-sym on` changes nothing in this unit; `off` is consistent with its OSLib/MacEmul neighbours.
- Functions: 4/4 exact. Departures from Pro7 in GetIndString:
  - Locals are declared in 2.6's order. This is a needed shape: Pro7's order gives 89%.
  - A `strings` copy of the locked handle's address bounds the walk, where Pro7 keeps the end in `dptr`. Needed shape: Pro7's form (84.8%) and `strs->data + len` written twice (82.9%) both lose. It is now declared with the other locals (second, after `ptr`; exact) instead of block-local; with Pro7's declaration order it is 89.1% wherever it goes.
  - The ellipsis' third dot is stored after `dptr += 2`. This is a 2.6 change in shape, confirmed by `add eax,2; mov [eax],'.'` (Pro7's `dptr[2] = '.'` first: 99.3%, one byte longer).
  - Moved to Pro7's spelling and still exact: the big-endian `last` expression order, `ptr += *ptr + 1` (the `l` temporary is gone), `if (ret)` and `if (strs)`.
  - A no-op `(StorageHandle*)` cast on GetResource is removed.
- Behaviour: as Pro7. The ellipsis expansion is guarded by `< 253`. Pro7's dead EqualString has Pro7's own bugs (it reads `strA[0]` before the NULL test and compares from index 0); they are not in 2.6's binary.
- Names: Pro7's and Wii's. None invented.
- Types: StorageHandle as above. Otherwise Pro7's (`StringPtr`, `SInt16`).
- Data: links. Literals at their use.
- Header: c2pstr (cdecl), p2cstr, GetIndString and getindstring (stdcall) all have outside users.
- Headers relied on: Resources.h (GetResource returning `struct StorageHandle*`, the same Handle question). ResourceStrings.h (Res_GetResource): fine. Memory.h, StringUtils.h (_pstrcpy): fine. MsDos.h (OS_GetTime, OS_TimeToMac, for dead GetDateTime): fine.
- Open: `StorageHandle` for `Handle` only.
- Header round H2 (October 16): `StorageHandle` is gone: Resources.h's GetResource returns Pro7's `Handle`. Resolved.

### src/driver/StringUtils.c: SOLID
- Boundary: 0x40ec10-0x40eec6, 8 rows, the same 8 as Wii's StringUtils module, in Pro7's order. Pro7's dead `_pstrcat`, `_pstrcharcat`, `pstrncpy` and `pstrncat` have no data and are left out. .data 0x5986f0 starts 8-aligned: "mybuf != NULL", "StringUtils.c" (the assert at Pro7's line 134) and "<out of memory>". .bss 0x100 is Pro7's `pfbuf`.
- Flags: `-O4,p -sym off`, not deferred. With `-sym on`, `_pstrcpy` and `pstrcmp` lose their match.
- Functions: 8/8 exact. The unit is rewritten from Pro7. The old text had invented names, a private `va_start` macro, hand-computed varargs, an explicit `__assertion_failed` call and stand-in types. Seven bodies are Pro7's verbatim (plus braces). The one departure is pstrcmp: `if ((d = (n = *(a++)) - *(b++)))` and `if ((d = *(a++) != *(b++)))`. This is a needed shape: 2.6 reads a's length before b's. Pro7's text gives 50%; the same logic as plain statements gives 62.5%. gc1's 1.x pstrcmp has the same assignment-in-condition shape, so it is the original's shape. There is a comment.
- Behaviour: Pro7's. The assert uses `OS_ASSERT(134, mybuf != NULL)`, and its bytes and line match. mvprintf reuses `va` after vsnprintf; on x86 MSL va_list is a `char*` passed by value, so this is not a bug.
- Names: Pro7's (`pfbuf`, `mybuf`, ...). None invented.
- Types: Pro7's (`StringPtr`/`ConstStringPtr`, an `int` pstrcmp, `unsigned int len` in mprintf). HPrintF takes `StorageHandle*` (the Handle question).
- Data: links. `pfbuf` is static, and the stale binding `_hprintf_buffer` is deleted (symbols.txt now names it `_pfbuf`).
- Header: Pro7's prototypes. Every one has users (mvprintf and mprintf in CLIO and CLErrors). `win32.h` is replaced by `oslib.h` and `<stdarg.h>`.
- Headers relied on: oslib.h (OS_ASSERT; it now declares `__assertion_failed`, moved from CLIO.h, see below). Memory.h (PtrAndHand, StorageHandle). MSL's stdarg/stdio.
- Open: `StorageHandle` for `Handle` only.
- Header round H2 (October 16): `StorageHandle` is gone (Pro7's `Handle`); StringUtils.h's mprintf and c2pstrcpy/p2cstrcpy are Pro7's. Resolved.

### src/driver/StringExtras.c: SOLID
- Boundary: 0x417400-0x417550, 4 rows. These are exactly Wii's StringExtras module (`strcatn@12` ... `ustrncmp@12`), in Pro7's order. There is no data.
- Flags: `-O4,p -sym off`, not deferred. `-sym on` loses ustrncmp.
- Functions: 4/4 exact. The unit is rewritten from Pro7: the old text had invented parameter names, an `output = 1 + output` loop and `unsigned int` comparisons. The bodies are Pro7's. One departure: ustrncmp's `while (len-- != 0)`. Pro7's `while (len--)` compiles to `sub 1; jb`, while 2.6 copies and tests the count. A signed `len` does not fix it; the explicit `!= 0` does. This is the original's shape and is commented.
- Behaviour: Pro7's.
- Names: Pro7's parameters (`d`, `s`, `max`, `len`). None invented.
- Types: Pro7's (`const char* s`, `SInt32 max`/`len`, `UInt32 len` in ustrncmp). Its callers (Parameter, Generic, CLPrefs, Arguments, MsDos, Targets, CLPlugins, CLMain) are still exact.
- Data: none.
- Header: the four functions, `__stdcall` (Wii's decorations, 2.6's `ret 0Ch`).
- Headers relied on: common.h (SInt32/UInt32): fine.
- Open: none.

### src/driver/MacFileTypes.c: SOLID
- Boundary: 0x422cf0-0x423000, 6 rows, exactly Wii's MacFileTypes module, in Pro7 order. Pro7's dead `OS_UseFileTypeMappings` has no data. .data 0x5a45e8 starts 8-aligned: `fmList = &defaultList`, then the assert literals of OS_AddFileTypeMappingList (Pro7's line 40). .bss 4 bytes: `defaultList`.
- Flags: `-O4,p -sym off`, not deferred. `-sym on` loses 3 functions.
- Functions: 6/6 exact. The unit now uses Pro7's bodies. The old text had while-loops, a `head` local, `DWORD err` and an `rref` local; Pro7's for-loops and the block-local `int ref` all match. Departures, each a 2.6 change confirmed in the disassembly:
  - OS_MacType_To_OSType clears the type with `memset(type, 0, sizeof(*type))` (an inline `rep stosd`) where Pro7 sets 0777/0666; Win32 has no executable bit.
  - OS_GetMacFileType tests `err` and `!err` where Pro7 has `< 0` and `>= 0`. 2.6's OS layer returns positive Win32 codes (`test eax,eax; jz`).
- Behaviour: as above. The extended-type hook is called `__stdcall`, matching the code.
- Names: Pro7's and Wii's. None invented.
- Types: Pro7's. `OSFileTypeMapping` (CWPlugins.h) now has Pro7's `OSType mactype` first field (was `CWDataType filetype`; MacFileTypes.c is its only reader, the three `ftmes` tables are positional).
- Data: links. `fmList` is static, and the binding `_fmList` is deleted. layout.py still names `defaultList` `lbl_005DEAB8` (as before): only the `fmList` initializer's relocation reaches it.
- Header: OS_MacType_To_OSType and the hook's `extern` are removed (no outside user); `win32.h` dropped. The source includes MsDos.h instead of CLIO.h (which it had used only for `__assertion_failed`).
- Headers relied on: CWPlugins.h (OSFileTypeMappingList/OSFileTypeMapping: member fixed to `mactype`; the two structs are Pro7's oslib.h types sitting in the SDK header, and CWPlugins.h itself is not reviewed). oslib.h (OS_ASSERT, uOSTypePair). MsDos.h (OS_Open/Read/Close/GetSize/SetFileType, OS_TEXTTYPE). MacSpecs.h (OS_GetRsrcOSSpec): fine.
- Open: CWPlugins.h unreviewed (it carries Pro7's oslib.h mapping types).
- Header round H2 (October 16): CWPlugins.h's mapping types are Pro7's (MacFileTypes.h DONE: `OSType mactype`). Resolved.

### src/driver/Files.c: SOLID
- Boundary: 0x4146a0-0x4155f0, 22 rows in Pro7 order. Wii's Files module has the same functions; its two unnamed first entries are OS_Get/SetFileParamInfo, which are static in Pro7. HOpenDF, HDelete, HGetFInfo, FSpGetFInfo and GetFPos are Pro7's. The assert "vRefNum!=0" at Files.c:840 is Pro7's line. .data 0x5a15e0 starts 8-aligned: `MacOpens`, then `""` from the dead HGetVol (kept), then `MacSeek` (only Pro7's dead PBWriteSync reads it; kept), then FSMakeFSSpec's "Files.c", "vRefNum!=0" and "..\\".
- Flags: `-O4,p -sym off`, not deferred. `-sym on` loses 6 functions.
- Functions: 22/22 exact. The unit is rewritten from Pro7 with Pro7's Mac types. The old text was gc1-style, with invented names and stand-in structs (`FileIdentifierInfo`, `FileQueryInfo`, `RecordQuery` with `queryValueN` fields, `RecordData`, a pack(2) union of fake `values`). Departures:
  - **2.6 changes, confirmed:**
    - Error tests are `if (err)` where Pro7 has `< 0` (the Win32 layer).
    - FSMakeFSSpec takes a 260-byte path, a 64-byte volume name and `"..\\"`, and caps a name at 63 (Pro7: 8, `"../"`, 255).
  - **Needed shapes:**
    - The else arm of OS_GetFileParamInfo chains `ioFlRLgLen = ioFlRPyLen = 0`; the original reloads the stored field.
    - FSMakeFSSpec uses `dirID == 0 ? 2 : dirID`, Pro7's ternary turned round.
    - FSMakeFSSpec tests `idx >= 64` (signed; `sizeof(vol)` compiles unsigned).
    - In FSMakeFSSpec's `"..\\"` loop, `idx++` is a statement of its own rather than Pro7's `++idx <` in the condition.
    - SetFPos has `posMode &= 3; mode = posMode;` with an `int mode`. Pro7 tests `posMode & 3` each time; `mode = posMode & 3` reaches 95.9%.
    Each is commented, and each is a plausible original rewrite, not a fakematch.
- Behaviour: statement by statement against Pro7 and the 2.6 disassembly. No bug of ours remains: HCreate's and HOpen's old `(short)result` casts now come from a real `OSErr`. Original quirks, kept: SetFPos calls `OS_MacToRef` before the `fRefNum == 0` test, and its `npos > size` is an unsigned compare (as in Pro7).
- Names: Pro7's throughout (MacFileInfo, `fss`, `mfi`, `fi`, `hpb`, `__err` ...). None invented.
- Types: Files.h has Pro7's mac68k types under one `#pragma pack(2)`: FSSpec (now Pro7's `SInt16 vRefNum`, `SInt32 parID`; all 4020 functions still exact), Point, FInfo, HFileParam, HParamBlockRec/HParmBlkPtr, and the fsXxPerm/fsFromXx/ioDirMask/kIsAlias enums. Prototypes are Pro7's (OSErr returns, SInt16/SInt32/OSType parameters), all `__stdcall` as Wii decorates them. MacFileInfo is file-local, as in Pro7. The stand-in structs are gone.
- Data: links. MacOpens is function-static and MacSeek is file-static, as in Pro7.
- Header: only what other units use. FSpGetFInfo, FSpSetFInfo and PBHSetFInfoSync are extern as in Pro7/Wii and prototyped at the file top, because uses precede their definitions. `win32.h` dropped.
- Headers relied on: oslib.h (OSSpec, OSTime, OSOpenMode/OSSeekMode, errors, OS_ASSERT). MsDos.h (file, time and ref functions; OS_TEXTTYPE). Generic.h (OS_GetFileNamePtr). MacSpecs.h (OS_FSSpec_To_OSSpec, OS_OSSpec_To_FSSpec, OS_GetRsrcOSSpec). Resources.h (the OS_*MacResourceFork* functions, cdecl). MacFileTypes.h. StringUtils.h. No problems found in the declarations used.
- Open: none.
- Adversarial review: re-ran the natural spellings of every documented departure with `fn.py variants`: Memory's SetHandleSize (Pro7's three `&h->hand` 65.2%, initialised or separate `oh` 91.3%, OS_PeekMacHandle 53.8%), StringUtils' pstrcmp (five plain spellings, 62.5-74.4%), TextUtils' GetIndString (Pro7's dot order 99.3%, Pro7's end pointer 84.8%, no copy 82.9%, Pro7's declaration order 89.1% with `strings` anywhere), Files' SetFPos (`SInt16 mode`, `posMode` alone, assignment in the `if` 92.7%, `int mode = posMode & 3` 95.9%, a `(SInt16)` cast 98.6%) and FSMakeFSSpec (Pro7's `++idx` loop 99.6%, Pro7's ternary 97.9%, `sizeof(vol)` 99.6%). Every one held, so no fakematch was found. Two things broke and are fixed. GetIndString's block-local `strings` was not needed: declared at the top it is exact. CWPlugins.h's `OSFileTypeMapping.filetype` was a stand-in name, and MacFileTypes was its only reader; it is now Pro7's `OSType mactype`. Also checked: Wii's module table and publics for all six modules (Memory, Files and the other four; the unnamed 0x42e770, 0x42eae0 and 0x42ed30 are the statics FixHandlePtr and OS_Get/SetFileParamInfo), that every header declaration has a user outside its unit, bindings (only the COMMON `___OS_ExtendedGetMacFileTypeHook` is still bound), clang_check, and clang-format. Result: 4020 exact before and after, nothing lost, and all six relink. Grades: TextUtils and Files hold. The others are unchanged except MacFileTypes, whose open item is now only the unreviewed CWPlugins.h.

### src/driver/CLDependencies.c: SOLID WITH TODOS
- Boundary: 0x428e30-0x429b30, the 20 rows in Pro7 CLDependencies.c's order; Wii's CLDependencies module has the same functions plus Deps_Terminate (and a later Deps_EmitDependencyList). Deps_Terminate (Pro7, Wii; dead in 2.6, no data) is back in the source. Assert file "CLDependencies.c" (line 175). .data 0x5a56a8, .bss 0x5deac0 (8-aligned); relink proves both.
- Flags: `-O4,p -sym off`, not deferred (ascending addresses). `-sym on` loses 2 functions.
- Functions: 20/20 exact, now Pro7's text (the old body had invented locals, `#define free xfree`, `EMPTYS`, a do/while). Departures: Incls_Initialize chained `numincls = maxincls = 0; buflen = bufpos = 0` (original's shape: the original stores 0 and then copies it; Pro7's four stores do not match). Incls_FindFileInPaths: no MakeFrameworkPath branch (2.6 change: no frameworks); `globalpath = NULL; accesspath = NULL;` as two statements; `OS_MakeFileSpec(...) == 0 && OS_Status(...) == 0` (Pro7's `!f()` materialises a value: 92.5%); `globalpath = specialpath = Deps_GetSpecialAccessPath()` and `_FindFileInPath(globalpath, ...)` (Pro7 passes specialpath: 99.1%); `userpath` declared before `foundglobal` (stack slots). All of these are the original's shape. SetSpecialAccessPathFromIncludeStackTOS: a `file` local loads the stack entry once before `index--` (needed shape: Pro7 reloads it, and Ninji's comment says it never matched); the stack is indexed directly (2.6: one load). Deps_AddDependency: the ternary goes into a `Boolean sys` local (needed shape: Pro7's `!(c ? f() : 0)` tests a dword where 2.6 tests `al`; a `(Boolean)` cast also matches; I took the local, the most natural of the two). Deps_ListDependencies: 2.6's buffers are 520/260/260/516 (from the frame and the `0x104` pushes), and there is a `return;` after the loop with `memFail:` after the if (2.6's epilogue order). Pro7's function-local `static currentsrcfss` is at file scope, declared first (.bss: currentsrcfss 0x5deac4 sits above specialAccessPath 0x5deac0). Pro7's DO_INTERNAL_ERROR is written out as `CLInternalError(__FILE__, 468, ...)`.
- Behaviour: code exact and data relinked. Bugs in the original (kept): AddDepFile ignores dependencyType; FindDepFile compares in gTarg->incls rather than deps->incls; the list's quote arms are `spaces ? "" : ""` (names with spaces are escaped instead). Bugs of ours: none found.
- Names: Pro7/Wii throughout; parameters and locals are Pro7's except Deps_AddDependency's unused fourth parameter, Pro7's placeholder `unk`, now `syspath` (`TODO: invented name`; the missing-file callback passes `!(fullsearch || noSysPath)`). `sys` is a new local. No invented function or data names.
- Types: Pro7's InclFile/Incls/Deps. Added Pro7's `IncludeSearch_*` enum (pref_structs.h, next to PCmdLineCompiler) and used it instead of 0..3. `Deps_AddDependency`'s last parameter stays `char*` with a TODO: Pro7's is `Boolean*`, but the caller (CLDropinCallbacks_V10.cpp, C++) passes a field of its stand-in for the SDK's CWFileInfo.
- Data: statics `static`; no bindings. `CLT_filesp`/`CLT_filestack` are COMMONs that CPrep.c defines (Pro7 defines them here; their placement was left alone).
- Header: declares only what other units use. Deps_GetSpecialAccessPath (used by no other unit) moved to a prototype at the file top. The TODO comment on CLT_* was reworded.
- Headers relied on: CLDependencies.h; CPrep.h (struct PFile, which is Pro7's CPrepFileInfo under a stand-in name, `header` for `textfile`; open); CLAccessPaths.h (Path/Paths: fine); CLFiles.h (File: fine); CLTarg.h (Target: fine); CLMain.h (optsCompiler, optsCmdLine, gTarg: fine); pref_structs.h (enum added; the EABI panels still have `unknownXX` fields, not used here); CLErrors.h (CLStr105, CLInternalError: fine); Generic.h, MacSpecs.h, MsDos.h, MemUtils.h, oslib.h (fine as used); cmdline.h (forward typedefs plus invented tags: StringLookupResult, StoredRecord, DropinSettings ... open); CLIO.h (invented `CLIO_OutOfMemory` macro, with `#include <stdio.h>` inside `extern "C"`; open; this unit no longer uses it); Memory.h (StorageHandle stands in for Pro7's Handle, with a front-end `tokens` member in its union; open); CLDropinCallbacks_V10.h (DropinFileCallback stands in for the SDK's CWFileInfo; open).
- Open: the `syspath` parameter name (TODO); header problems (CPrep.h PFile, cmdline.h tags, CLIO.h macro, Memory.h StorageHandle, the CWFileInfo stand-in behind the `char*` TODO).

### src/driver/CLFileOps.c: SOLID WITH TODOS
- Boundary: 0x41a520-0x41c130, 16 rows in Pro7 CLFileOps.c's order. set_bytes (0x41b5d0) is 2.6's and sits where Wii's CLFileOps has an unnamed 21-byte static. Asserts are "CLFileOps.c" (835, 1024, 1039, 1054, 1069). .data starts at 0x5a1ca8 (8-aligned) and relinks byte for byte. Pro7's dead statics fstrcat/extstrcat/PostLinkFilesInProject leave no data of their own (their literals are pooled with live ones or absent), so the evidence cannot say whether 2.6's file had them: not added.
- Flags: `-O4,p -sym off`, not deferred. `-sym on` loses 7 functions.
- Functions: 16/16 exact, now Pro7's text (the old body was 1.x-shaped: invented helpers such as applyClassTypes, magic numbers, `(Boolean)` casts, `#pragma bool off`, all gone). Departures: 2.6 changes: GetOutputFile's buffers (text[256], `>= sizeof(text)`, 260-byte names), no "/tmp" fallback, `OS_PATHSEP` '\\'; 260-byte buffers in CompileFile and DisassembleWithLinker; DumpFileAndPathInfo has no frameworks section; CompileEntry and CompileFilesInProject call set_bytes where Pro7 writes `cun = dun = uun = "bytes"`. Needed shapes: GetOutputFile `(file->tempOnDisk & stage) == 0` (Pro7's `!(...)` materialises: 99.3); SyntaxCheckFile `if (!f()) return 0; return 1;` (Pro7's `!= 0`: 72.0); CompileFilesInProject `if (wascompiled) ... else ignored++` (Pro7's two ifs: 99.0); LinkProject compares `outputType` with the SDK's `linkOutputFile`/`linkOutputNone` (adversarial review: the literals were a workaround for a stand-in type: the repo's CWTargetInfo had `UInt16 outputType` where the SDK has `short`; with the SDK's type the enumerators match). Pro7's unused `Plugin* disasm` was dropped (it changes nothing).
- Behaviour: exact code and relinked data. Bugs in the original (kept): CompileFile stores `recordbrowseinfo = 0` twice; GetOutputFile's length cap measures from the file name while the copy starts at `inname`; in LinkProject, `fss` is unset if the post-linker's target info changes outputType to 1. Ours: the old source's `#pragma bool off` and stand-in casts are removed.
- Names: Pro7/Wii. Invented: `set_bytes` (gc1's name; Wii has it unnamed), marked `TODO: invented name`.
- Types: Pro7's File/Target/BuildInfo, `Boolean* compiled`. Added the SDK's CompilerMappingFlags enum (`kPrecompile`, `kRsrcfile` ...) to CWPlugins.h for `file->mappingflags`. DropInCompilerLinker.h: `CWTargetInfo.outputType` (and the V8 copy's) is the SDK's `short`, with the SDK's `linkOutputNone/File/Directory` enum (adversarial review; its users CLPluginRequests, DropInCompilerLinkerPrivate, CCompiler, CLTarg stay exact). `isExecutableTool` is 2.6's own name (CLToolExec's assert string), not Pro7's `dropInExecutableTool`.
- Data: no statics. No bindings to this object.
- Header: GetOutputFile, StoreObjectFile (CLToolExec, CLCompilerLinkerDropin_V10), CompileFilesInProject, LinkProject (CLMain), and Pro7's CmdLineStageMask enum (CLWriteObjectFile, CLToolExec). Its includes were cut to common.h and cmdline.h: setjmp, win32, CLDependencies, CLFiles, Files and MsDos were unused, and no includer needed them (full build, clang_check of the 13 includers clean). The source's include list went from about 50 headers to 15 (`includes.py trim`, then the compiler/ headers it kept removed by hand: unit exact in all 5 versions, 2.6 relinks).
- Headers relied on: as for CLDependencies, plus CLPluginRequests.h, CLPlugins.h (Plugin_CL_GetObjectFlags: fine), CLToolExec.h (ExecuteLinker's flags: Pro7's SInt32 since the H2_DRV review), CLIO.h (ShowHandle etc.; the CLIO_OutOfMemory macro is open), CLBrowser.h, CLIncludeFileCache.h, CLWriteObjectFile.h, Files.h (FSpDelete is declared `DWORD __stdcall FSpDelete(CWFileSpec*)` where Pro7 has `OSErr FSpDelete(const FSSpec*)`; open), DropInCompilerLinker.h (CWTargetInfo_V8 has an invented name, already TODO there).
- Open: the invented name set_bytes (no source names it); header problems (Files.h FSpDelete, CLIO.h macro, cmdline.h tags, Memory.h StorageHandle).

### src/driver/CLWriteObjectFile.c: SOLID
- Boundary: 0x429b30-0x429d10, Pro7's two functions and Wii's CLWriteObjectFile module (WriteObjectFile, WriteBrowseData). Assert "CLWriteObjectFile.c" line 22. .data 0x5a5778, relinked.
- Flags: `-O4,p -sym off`, not deferred. `-sym on` loses both functions.
- Functions: 2/2 exact, Pro7's text (the old body used a `ready` local, string casts and magic numbers). Departure: WriteBrowseData ends `if (!f()) return 0; return 1;` (needed shape: Pro7's `return f() != 0;` is 93.1). WriteObjectFile is Pro7's verbatim; OS_ASSERT computes the `&&` first, as the repo's macro does.
- Behaviour: exact and relinked. No bugs found.
- Names: Pro7/Wii; Pro7's parameters.
- Types: FSSpec/OSSpec/OSHandle as Pro7.
- Data: no statics or bindings.
- Header: both functions are used by CLFileOps (and CLPlugins, which has a member of the same name); variables none.
- Headers relied on: CLWriteObjectFile.h, CLFileOps.h (CmdLineStageMask), CLPlugins.h (Plugin_CL_WriteObjectFile, Plugin_CL_GetObjectFlags), CLBrowser.h, CLMain.h, CLErrors.h, Generic.h, MacSpecs.h, MsDos.h (OS_NameSpecSetExtension, WriteBinaryHandleToFile via CLIO.h); the problems are the same open ones as above (CLIO.h macro, Memory.h StorageHandle, cmdline.h tags).
- Open: header problems only.
- Header round H2 (October 16): CLIO.h's `CLIO_OutOfMemory` is gone, `StorageHandle` is Pro7's `Handle`, cmdline.h holds only Pro7's tag names; CLWriteObjectFile.h and CLPlugins.h are DONE. Resolved.

### src/driver/CLIncludeFileCache.c: SOLID
- Boundary: 0x42a220-0x42a4ed, 8 rows in Pro7 CLIncludeFileCache.c's order, and Wii's CLIncludeFileCache module has the same (plus a later PollIncludeCache). .data 0x5a5848 ("include file cache"), .bss 0x5deac8-0x5dead4 (12 bytes, the three statics: availablecache lowest, so declared last); relinked.
- Flags: `-O4,p -sym off`, not deferred. `-sym on` changes nothing here (it is kept like its neighbours).
- Functions: 8/8 exact. Departures from Pro7, each needed (Pro7's text tried): makecacheentry `if (freelist) { c = freelist; freelist = c->next; } else { c = xmalloc(...); } return c;` (Pro7's two returns: 54.5); InitializeIncludeCache `cachelist = freelist = NULL;` chained (Pro7's two stores: 66.7); CacheIncludeFile `SInt32 size` (GetHandleSize's type, Mac's `Size`) with Pro7's casts (Pro7's `UInt32`: 86.8; `SInt32` without the casts 97.3; adversarial review: the reviewer's `int` also matched, `SInt32` with Pro7's casts is exact and closer to Pro7). These are the original's shape: plain alternatives, not workarounds.
- Behaviour: exact and relinked. No bugs found.
- Names: Pro7/Wii (CacheEntry and its fields, CACHE_SIZE: Pro7's).
- Types: StorageHandle for Pro7's Handle (Memory.h stand-in; open).
- Data: statics `static`. Removed the GC_2_6 bindings `_availablecache`, `_cachelist`, `_freelist`: only this unit's exact functions reference them (`bindings.py refs`), and symbols.txt now has them as local. The equivalent compiler still links.
- Header: the five public functions, each used by CLFileOps, CLLoadAndCache, CLCompilerLinkerDropin_V10 or CLDropinCallbacks_V10.
- Headers relied on: CLIncludeFileCache.h, Memory.h (StorageHandle stand-in: open), MemUtils.h, MsDos.h (OS_EqualSpec, OS_GetMilliseconds: fine), oslib.h.
- Open: Memory.h's StorageHandle (Pro7's Handle).
- Header round H2 (October 16): `StorageHandle` is Pro7's `Handle`; CLIncludeFileCache.h is DONE. Resolved.

### src/driver/CLLoadAndCache.c: SOLID
- Boundary: 0x443ea0-0x444080, Pro7's three functions; Wii's CLLoadAndCache module has the same three. .data 0x5a8f98 ("\n*** Out of memory\n"), relinked.
- Flags: `-O4,p -sym off`, not deferred. `-sym on` changes nothing (kept like its neighbours).
- Functions: 3/3 exact, now Pro7's text (the old one had renamed locals, a stray `buffer` copy, CLIO_OutOfMemory). One departure: LoadAndCacheFile writes `OS_GetMacFileType(...) == 0 && mactype != 'TEXT'` (needed shape: Pro7's `!f() && ...` materialises: 98.1; it is the same `== 0`/noErr spelling as in CLDependencies). FixTextHandle is Pro7's verbatim, returning `short` (the old source had `unsigned int`).
- Behaviour: exact and relinked. No bugs found.
- Names: Pro7/Wii and Pro7's locals. CopyFileText's parameters are Pro7's names.
- Types: CopyFileText keeps `void**, unsigned int*` where Pro7 has `char**, SInt32*`: CLDropinCallbacks_V10.cpp's stand-in fields require that (TODO there). StorageHandle for Handle.
- Data: no statics or bindings.
- Header: FixTextHandle (CLFiles), LoadAndCacheFile and CopyFileText (CLDropinCallbacks_V10). Declarations renamed to Pro7's parameters; `short FixTextHandle`.
- Headers relied on: CLLoadAndCache.h, CLIncludeFileCache.h, MacFileTypes.h (OS_GetMacFileType: fine), MsDos.h (OS_Open/OS_Read/OS_GetSize/OS_Close: fine), Memory.h (StorageHandle: open), MemUtils.h, oslib.h (OSReadOnly).
- Open: Memory.h's StorageHandle; CLDropinCallbacks_V10's stand-in types behind CopyFileText's parameter types.
- Header round H2 (October 16): `StorageHandle` is Pro7's `Handle`; CLDropinCallbacks_V10.h's stand-ins are gone and CopyFileText takes Pro7's `char**, SInt32*` (CLLoadAndCache.h DONE). Resolved.

### src/driver/CLBrowser.c: SOLID WITH TODOS
- Boundary: 0x429d10-0x42a220, 9 rows in Pro7 CLBrowser.c's order, and Wii's CLBrowser module has the same 9. Asserts "CLBrowser.c" (114, 115, 253, 254). .data 0x5a57c0, relinked. The neighbours are CLWriteObjectFile before (WriteBrowseData is Wii's CLWriteObjectFile) and CLIncludeFileCache after.
- Flags: `-O4,p -sym off`, not deferred. `-sym on` loses 3 functions.
- Functions: 9/9 exact. Departures from Pro7, each tried both ways: Browser_SearchFile advances in the `for` (`idx++, scan++`; Pro7's `scan++` in the body: 98.5) and ConvertMemToDisk does `mem++` before advancing `disk` (Pro7's order: 97.8). Both are the original's shape. In Browser_PackBrowseFile, the memset address is `(char*)((int)ptr + sizeof(header) + datasize)` (needed shape: Pro7's `&ptr[...]` adds in the other order, 90.5%; Ninji's comment says he could not match it; integer pointer arithmetic is plausible Metrowerks). Also `if ((err = OS_NewHandle(totalsize, &h)), *browsefileptr = h, err)`: original's shape (one terse condition, plausibly Metrowerks', per the maintainer's rule; re-classed by the adversarial review). 2.6 reloads `browsefileptr` for each word of the struct copy; Pro7's three statements, `if (*browsefileptr = h, err)`, `err != 0`, a comma statement, `err` as SInt32/long/UInt32/OSErr/short, `err` declared first, a field-wise copy all give 99.0-99.7; only a block-scoped `int e` temporary also matches (less plausible). The memset's `(int)ptr` arithmetic: `ptr + sizeof(header) + datasize`, `&ptr[sizeof(header)] + datasize` and the other orders give 98.7 or 90.5.
- Behaviour: exact and relinked; TRUE/FALSE returns. No bugs found.
- Names: Pro7/Wii. Invented: the record typedefs S43/S49/T53 (Pro7's decompilation's names), marked `TODO: invented names`.
- Types: Pro7's; StorageHandle for Handle.
- Data: no statics or bindings.
- Header: the five public functions (CLFileOps, CLWriteObjectFile, CLDropinCallbacks_V10).
- Headers relied on: CLBrowser.h, CLErrors.h (CLReportOSError), MemUtils.h (xstrdup, xfree), MsDos.h (OS_*Handle: fine), Memory.h (StorageHandle, HLock: open), oslib.h.
- Open: the S43/S49/T53 names; Memory.h's StorageHandle.
- Adversarial review: attacked every unusual spelling with `fn.py variants` (Browser_PackBrowseFile's comma condition: 15 spellings, and its `(int)ptr` memset: 7; CacheIncludeFile's size type and casts: 5), the stand-in types behind workarounds, placeholder names and the headers' users. Broke: LinkProject's `outputType == 1`/`!= 0` literals were a stand-in-type workaround (CWTargetInfo.outputType `UInt16` for the SDK's `short`): fixed in DropInCompilerLinker.h with the SDK's enum, no function anywhere lost; Deps_AddDependency kept Pro7's placeholder parameter `unk` (renamed `syspath`, TODO); CacheIncludeFile's `int size` (now `SInt32`, Pro7's casts kept). Re-classed: the PackBrowseFile comma expression (original's shape under the maintainer's rule; its TODO is now a comment). Held: every other needed shape (catalogued `== 0`/`if (!f()) return 0; return 1;` forms), the boundaries and data (all six relink after the changes), flags, header users (each declaration has a user outside its unit; CLT_filesp/CLT_filestack are defined by CPrep.c), `isExecutableTool` (2.6's assert string names it). Not re-attacked: the documented TODOs (set_bytes, S43/S49/T53, the CWFileInfo stand-in behind `char* alreadyincluded` and CopyFileText's `void**`/`unsigned int*`). Build: 4020/4077 exact before and after, nothing lost; layout.py and relink --only for all six clean; clang_check and clang-format clean.

### src/backend/PCode.c: SOLID WITH TODOS
- Boundary: 17 rows 0x4dcdc0..0x4dd3b0 are Pro7's PCode.c in reverse order (computedepthfirstordering lowest, initpcode highest); Wii's PCode module has the same run (plus later additions). Pro7's deleteblock (inlined into deleteunreachableblocks) and the dead clearpcodeflags are back in Pro7's places; neither has data.
- Flags: was `-inline auto` with the file written in binary order (1.x text); now Pro7's order with `-inline auto,deferred` (descending addresses), 17/17. `-sym on` unchanged.
- Functions: 17/17 exact; the source is now Pro7's text (the old one had 1.x bodies, magic flag numbers and a fake opcode enum). Departures: copypcode pads every copy to 5 operands (a 2.6 change: `cmp esi, 5` and the placeholder loop in the disassembly; it allocates `offsetof(PCode, args) + 12n`, as the original's `0x24 + 12n` shows); initpcode and appendpcode use chained assignments (needed shape: Pro7's separate statements make emitpcode, which inlines appendpcode, and initpcode inexact); computedepthfirstordering sets `index = 0` and does `index++` after filling dfo[0], and copies `block = dfo[--index].block` before storing it (needed shape: Pro7's `index = 1` and one-statement store are 71-88%); makepcblock links the blocks with one comma expression (TODO, catalogued: two statements put pclastblock in eax where 2.6 has ecx; inverted arms, `!= NULL` and `(a = b)->c` tried).
- Behaviour: checked against the disassembly; no bugs found.
- Names: Pro7's throughout. The binding `_pcodeBlockOrderIndex` (a 1.x name for the static `depthfirstorder`, read only by this unit) was deleted.
- Types: Pro7's; the stand-in `BlockOrderEntry` (BE_elf.h) replaced by Pro7's file-local `DFO`. `pcloopweight` `unsigned int` and `setpcodeflags(unsigned int)` keep the header's documented 2.6 evidence (CodeGen_Generator, Coerce_to_register).
- Data: .bss (`pclabelcount`, `depthfirstorder`) static; relinks.
- Header: Pro7's API (deleteblock/clearpcodeflags not declared: no other unit uses them).
- Headers relied on: PCode.h (structs, flags: Pro7's, `fPCodeFlagNNN` placeholders are Pro7's own), PCodeInfo.h (vformatpcode, PCOp kinds; no Pro7 `Opcode` typedef), Alias.h, CodeGen.h (current_statement), CFunc.h (Statement), CompilerTools.h. Problems: BE_elf.h `struct BlockOrderEntry` and common.h's typedef are now unused (delete with the next header pass); CFunc.h's issues listed under COptimizer.
- Open: the makepcblock comma TODO.

### src/backend/PCodeUtilities.c: SOLID WITH TODOS
- Boundary: 18 rows 0x4ea490..0x4eafc0, Pro7's PCodeUtilities.c in reverse plus 2.6's Gekko pair after load_store_register (Wii's module has Select_Gecko_Load_Store and gecko_load_store_register at the same end). branch_count_volatiles is this unit's (the old "outside this unit's range" comment was stale).
- Flags: deferred (descending), `-sym on`; gecko_load_store_register inlines Select_Gecko_Load_Store, which the binary shows, so it now calls it instead of a pasted copy.
- Functions: 18/18 exact; rewritten from Pro7 (the old text was 1.x with literal opcodes, `SInt32(*)[32]` casts, invented inline helpers and a CE_ASSERT macro). Departures: branch_record_volatiles marks cr1 read (2.6: `cmp dl,1; cmp word [edi+4],1; or 1`); branch_subroutine tests ObjGen_GetAddressing for 10 then 11 and sets fAbsolute for 11 (2.6: two compares, the 10 arm identical to the default as in the binary); add_immediate addresses small data from r2/r13 absolutely, asserting non-DLOCAL (534) and rewriting addi to li, else asserts base_reg (557) (2.6, from IDA); load_store_register replaces Pro7's add_immediate_lo step with the same is_16bitdata test and uses `(offset >> 15) & 1` (Pro7's `& 0x8000 >> 15` is 98.8%), its vector-block registers block-local (needed shape: 2.6 has fresh variables where Pro7 reuses offset_reg2); pcsetrecordbit has early returns and an `arg` pointer for args[3]/[4] (needed shape, catalogued; Pro7's else-if is 79%); branch_count_volatiles tests `limit > i` (catalogued; Pro7's breaks it and both callers); `(int)` casts in the r2/r13 compares (catalogued movsx+cmp); gecko keeps the opcode in an `int` and a `short` copy (original's shape: one variable is 83%).
- Behaviour: every 2.6 addition read from IDA; Pro7's codegen_pic read in op_absolute_ha is copts+0x24.
- Names: Pro7's (functions, parameters, locals); gecko's are Wii's names with Pro7-style locals. No invented names.
- Types: Pro7's; `SInt16` offsets as Pro7.
- Data: no .data/.bss of its own besides the assert file name; relinks.
- Header: Pro7's API plus the two Gekko functions (used by SpillCode, CodeGen, Operands, InstrSelection, FunctionCalls); branch_conditional's parameters are Pro7's names again.
- Headers relied on: Registers.h (scratch registers, used_virtual_registers), ObjGen_PPC_EABI.h, StackFrame.h, Exceptions.h, PCodeInfo.h, CPrep.h (COpts). Fixed: COpts/CInlineBackendOptions `useRegisterSaveHelpers` renamed `codegen_pic` (Pro7's op_absolute_ha reads codegen_pic). Verified: copts+0x24 is in no entry of 2.6's `compileroptions` (its neighbours: cats 0x15, force_cats 0x16, pool_data 0x20; the register-save pragma is `no_register_save_helpers` at 0xb); IDA's xrefs to 0x5eb0b6 are op_absolute_ha and CMachine's backend-options save/restore only, and 2.6 has no `pic`/`dynamic` pragma strings (the driver's `-model pic` sets the project panel's codemodel, which CodeGen_InitBackEndOptions does not copy here): the byte is always 0 in 2.6, as codegen_pic is on a non-Mach-O target. Problems: Registers.h's VarInfo 1.x union overlay, Registers_InvalidateCSE/GetCSEWeight/ContainsCOptCSE/DivideUses declared with no definition, no Register13; COpts still has placeholders (`f25`/`f26`/`f27`, `pad*`) and TODO names.
- Open: the Registers.h and COpts problems above (none in the unit itself).
- Header round H2 (October 16): Registers.h is DONE (VarInfo is objects.h's, no 1.x overlay, the undefined Registers_* prototypes gone); its Gecko helpers use Pro7's `Opcode`. COpts is Pro7's CompilerLinkerOptions in CParser.h: the f25/f26/f27 and pad* placeholders are named, the remaining invented members carry TODOs. Nothing open.

### src/backend/PCodeListing.c: SOLID WITH TODOS
- Boundary: 5 rows 0x500bd0..0x500c20, Pro7's listing functions compiled to empty bodies, in reverse (deferred); Wii has pccleanuplisting in module PCodeListing. The dead helpers' literals are the unit's .data, which is why they are present.
- Flags: deferred, `-sym on`.
- Functions: 5/5 exact (four empty, pclistblocks_end_scheduler Pro7's with 2.6's line 1525). Departures: formatdataflowset, pclistblock, formatalias and dumpalias bodies are reconstructed from 2.6's literals (TODO-marked; dead code, so only their literals' order can be checked, and the relink checks it).
- Behaviour: live code is the one assert; nothing else runs.
- Names: Pro7's.
- Types: Pro7's.
- Data: .data relinks byte for byte; no .bss (2.6's object has none), so Pro7's statics are absent and `pcfile` is an extern declaration (TODO, documented).
- Header: the five functions other units call plus `pclist_bad_operand` (PCodeInfo); formatalias, dumpalias, GetLine* and DumpSourceCode had no outside user and left the header (file-top prototypes for dumpalias and GetLineOffset, used before their definitions); `const char*` where Pro7 has `char*`.
- Headers relied on: Alias.h, BitVectors.h, InterferenceGraph.h (IGNode flags), LiveVariables.h, PCode.h, PCodeInfo.h, CMangler.h. No new problems found.
- Open: the literal-reconstructed dead bodies (no source to take them from).

### src/backend/PPCError.c: SOLID
- Boundary: 4 rows 0x4b5ae0..0x4b5cb0, Pro7's PPCError.c in reverse (deferred); Wii's module (plus a later PPCError_Abort).
- Flags: deferred, `-sym on`.
- Functions: 4/4 exact. The invented second helper `PPCError_VAErrorMessageTerm` is gone: one PPCError_GetErrorString with `(int)code` compares serves all three callers (catalogued `(int)` shape; Pro7's `short scode` copy is 85-88% in all three, 11 other spellings tried). Warning and Message use early returns (needed shape; Pro7's `if (!trychain) {...}` is 93%). Message is 2.6's: it also tests `copts.supress_warnings` and reports id 10221 (0x27ed in the disassembly; Pro7 10213).
- Behaviour: checked against the disassembly.
- Names: Pro7's; the message enum Pro7's `PPCErrorStrNNN`.
- Types: Pro7's.
- Data: only the file name literal; relinks.
- Header: the four reporters, used throughout the back end.
- Headers relied on: PPCError.h, CError.h (CError_ErrorMessageVA), CParser.h (trychain, errorreturn), CPrep.h (copts), InlineAsm.h (in_assembler, AssemblerError), CompilerTools.h (COS_GetString). None found wrong for this unit.
- Open: none.

### src/optimizer/GlobalOptimizer.c: SOLID
- Boundary: 3 rows 0x500500..0x500950 (optimizelevel2 inlined), Pro7's file in reverse; Wii module GlobalOptimizer. The 1.x AddPropagation/CopyPropagation leftovers AGENTS.md mentions are no longer here.
- Flags: deferred, `-sym on`.
- Functions: 3/3 exact, Pro7's text. One departure: optimizelevel4 returns early when no loops are found the second time (needed shape, catalogued; Pro7's block is 99.4%).
- Behaviour: Pro7's. Original's quirk (Pro7 has it too): the second propagateconstants after the array-to-register transform runs only when `copts.debuglisting` is on.
- Names: Pro7's.
- Types: n/a.
- Data: the listing literals; relinks.
- Header: globallyoptimizepcode only.
- Headers relied on: the global optimizer's pass headers (CodeMotion, ConstantPropagation, CopyPropagation, LoadDeletion, LoopDetection, LoopOptimization, Peephole, StrengthReduction, UseDefChains, ValueNumbering, VectorArraysToRegs, AddPropagation), PCodeListing.h, CMangler.h, CodeGen.h (has_catch_blocks), CPrep.h (COpts). Not reviewed here beyond the globals used.
- Open: the pass headers are unreviewed (the unit itself has nothing open).
- Header round H2 (October 16): the pass headers are reviewed (Optimizers family: all DONE or DONE WITH TODOS, none used here with an open item); PCodeListing.h, CMangler.h DONE; CodeGen.h DONE WITH TODOS; copts is CParser.h's. Resolved.

### src/optimizer/COptimizer.c: SOLID
- Boundary: 12 rows 0x4fd8c0..0x4fe0c0, Pro7's COptimizer.c in reverse without the statement-level CSE (its jump tables are not in 2.6's .data, no 2.6 function calls it); Wii module COptimizer (only the three externs are publics, consistent with Pro7's statics). Pro7's dead RegAllocMask/MarkRegAllocMask and `basicblocks` are absent (no data evidence; adding a COMMON 2.6 lacks would move the COMMON tail).
- Flags: deferred, `-sym on`.
- Functions: 12/12 exact, now Pro7's text with Pro7's static helpers (finallabel, optimizegoto, removeif, optimizebranches, checklocalusage: all inlined, as the binary has no rows for them); the gc1 shapes (COpt_Follow, COpt_Pass with `goto next`, set_statement_location/optimize_expression inline helpers, ELABELCallBack's double load) are gone. Departures: optimizeswitch compares `CInt64_Equal(min, value)` (2.6's SwitchCase has no range); SetVarUsage counts DLOCAL only (Pro7 also DNONLAZYPTR, Mach-O) with an early return (catalogued shape); `flags |= StmtFlag_1` for Pro7's `flags = flags | ...` (catalogued); optimizebranches walks with its own `scan` (needed: walking the parameter inlines COpt_Optimize into both callers); COpt_Optimize copies `*ptr` into `scan` (needed: same reason); COpt_ReturnCheck walks a copy of `stmt` (original's shape, catalogued "a local copy keeps it out"; Pro7's is 72%); optimizeif walks on with `scan` and keeps the goto/return in `scan2` (needed: Pro7's is 54%; same behaviour, the outer loop ends after either).
- Behaviour: Pro7's otherwise; nothing found wrong.
- Names: `data_00588513` is Pro7's/Wii's `copt_isleaffunction` (Wii's COpt_Optimizer writes it; defined here as in Pro7, removed from DumpIR.c; bindings renamed in every version). Its 2.6 COMMON slot 0x5eb38e fits hash order only if the next byte's name (repo: `in_parameter_type_list`, hash 1836, no Pro7 or Wii source) hashes above 1919: that name is the suspect one. The binding `_optimizer_changed` on the static `stmtchanged` was deleted.
- Types: the old header's invented CSE structs (COptBlock with "referenceBits", COptBlockLink, COptCSE, OptimizerOccurrence, `cse_entries[75]`, `occurrence_list`) are gone; the dead statics use the CSE types as incomplete pointers (Pro7's members are placeholders, x1C/x1E).
- Data: `bitmasks` (.data) and the CSE statics (.bss) kept for the original's layout; relinks with DumpIR.
- Header: copt_isleaffunction, SetVarUsage (InlineAsm.c), COpt_SimpleOptimizer (CInline.c), COpt_Optimizer (CodeGen.c), as Pro7's minus what 2.6 lacks.
- Headers relied on: CFunc.h (Statement, CLabel, StmtFlag_*: `CLabel.target.stmt` union where Pro7 has `stmt`; `current_statement_number` renamed Pro7's/Wii's `curstmtvalue` (fixed, every version: Wii's public is read by COpt_Optimizer and SetVarUsage, and its COMMON hash 432 fits between `CError_BreakPointcount` 431 and `DefaultContext` 444 where the old name's 1920 did not); it is still defined in DumpIR.c where Pro7 defines it in CFunc.c; `in_parameter_type_list` invented, see Names), CException.h (`CException` is a stand-in for Pro7's `ExceptionAction`: `kind`/`next` for `type`/`prev`, `pad_1d`, `slots`/`operands`/`bytes` overlays), enode.h (`data.labelAddress` for Pro7's `data.label`, `unk0e`, gc1 commentary members), Switch.h (SwitchInfo/SwitchCase), InlineAsm.h, IrOptimizer.h, CExpr2.h, common.h (stale `COptBlockLink`, `OptimizerOccurrence`, `BlockOrderEntry` typedefs).
- Open: the header problems above (CException/ENode/CLabel stand-ins), none in the unit itself.
- Adversarial review: attacked every unusual construct with natural spellings via `fn.py variants` (makepcblock: Pro7's two statements, swapped order, a local, `(a = b)->c`, reordered stores, merged arms, 79-93%; copypcode: `extra_arg = 5 - argCount`, a `for` placeholder loop, pre/post-decrement loops, 61-98%; pcsetrecordbit with `pc->args[3]` directly 90.2%; PPCError_GetErrorString without the `(int)` casts: ErrorTerm 82.5%; COptimizer's `|=` vs Pro7's `x = x | F` 96.8%/84.2%, SetVarUsage nested 92.6%, optimizeif Pro7's 54.3% and 16 bytes short): every documented shape held. Checked against IDA: add_immediate, load_store_register (r2/r13 test, `sar 15; and 1`), branch_subroutine (only the `== 11` arm differs), the codegen_pic byte (above). Broke: the 1.x name `current_statement_number` (renamed `curstmtvalue` by Wii and hash order; DumpIR, CFunc, TOC, COptimizer, CFunc.h and every version's bindings/symbols touched); PCodeListing.h declared five unit-private functions (moved). Also found, not fixed: `blank_argument_name` (hash 2563 at 0x5e9f4c between 432 and 444) is a misnamed COMMON, as is `in_parameter_type_list` (1836 after copt_isleaffunction's 1919); `curstmtvalue` belongs defined in CFunc.c (a COMMON move: check the relink). After the changes: 4020/4020 exact as at the start, no function lost, all six units and DumpIR relink. Grades held.
- Header round H2 (October 16): CFunc.h's `CLabel.stmt` is Pro7's and `curstmtvalue` is defined in CFunc.c; Exceptions.h's ExceptionAction is Pro7's (`prev`/`type`, kind-specific members); enode.h's `labelAddress`/`unk0e` views are Pro7's `label`; CExpr2.h is merged into CExpr.h; common.h's stale typedefs are gone; COptimizer.h DONE; `in_parameter_type_list` is Pro7's `in_func_arglist`. All resolved.

### src/optimizer/IroBitVect.c: SOLID
- Boundary: 12 rows 0x52bcc0..0x52bf6f are Pro7 IroBitVect.c's 13 functions in reverse order (Bv_AllocVectorLocal is missing: dead, no data). Wii module irobitvect for 7; assert "IroBitVect.c" line 73 (Bv_ClearBit). Links.
- Flags: was `-inline auto` with the source in binary order; the rows descend against Pro7's order, so it is now Pro7's order with `-inline auto,deferred` (links). `-sym on`: `-sym off` loses one function.
- Functions: 12/12 exact, now Pro7's text (was a non-Pro7 rewrite with a fakematch: Bv_Or walked `&bv->size` as a `UInt32*`; Bv_AllocVector returned `unsigned int` with no return statement). Departures: Bv_AllocVector reuses `size` where Pro7 has a local `long_size` (needed shape, catalogued; with the local the registers differ). Bv_IsSubset's counter is `int` where Pro7 has `UInt32` (2.6's type: with UInt32 the `mov eax, ebx` copy before the compare is missing). Bv_AllocVectorLocal is kept as Pro7's dead function (comment), with no prototype.
- Behaviour: Pro7 text, exact. Bug in the original, kept: Bv_IsSubset tests `b->size < i`, so `i == b->size` reads one word past `b`.
- Names: Pro7's (functions, parameters, locals). None invented.
- Types: `BitVector` is Pro7's `{UInt32 size; UInt32 data[0];}`. The member was renamed from `bits` to `data` (BitVector.h, one use in IROUseDef.c). The unexplained `#pragma pack(push, 2)` and `bits[1]` are gone (sizes unchanged for the code: the allocations use `sizeof(BitVector)` = 4).
- Data: .data is only the assert file name. Nothing static. Links.
- Header: IroBitVect.h declares the 12 used functions with Pro7's const-qualified signatures; `Bv_AllocVectorLocal` is not declared (no user).
- Headers relied on: CompilerTools.h (oalloc, lalloc), CError.h (CError_FATAL): fine. BitVector.h: now Pro7's Bv_SetBit, includes inside the guard.
- Open: none.
- Adversarial review: tried Pro7's `long_size` local (50.0%, registers only: the original keeps the parameter in edi) and `UInt32 i` in Bv_IsSubset (91.8%): both departures hold. Wii's irobitvect module has Bv_AllocVectorLocal (0x5df100), so keeping the dead function is evidenced. Header users all checked. BitVector.h fixed (shared, owned elsewhere; nothing lost in any GC_2_6 function, the units using it still link): its includes moved inside the guard, and Bv_SetBit is Pro7's text (`bit / 32`, `1 << (bit & 31)`, `CError_FATAL(56)`: MWCC's `__FILE__` in the header is "BitVector.h", so the hand-spelt name was not needed). Grade holds.

### src/optimizer/IroSubable.c: SOLID WITH TODOS
- Boundary: 2 rows (0x457500 IRO_IsSubableExpression, 0x457630 IRO_InitializeIsSubableOpArray) = Pro7 IroSubable.c in reverse. IsSubscript is inlined. Pro7's last function, IRO_IsVectorTempCandidate, has no caller and no data; it was added back as Pro7's dead function. .bss = IsSubableOp[MAXEXPR] (0x5dff68, 0x4d bytes). Links.
- Flags: deferred (rows reversed). `-sym off` loses a function.
- Functions: 2/2 exact; the text is Pro7's verbatim. No departures.
- Behaviour: as Pro7; exact.
- Names: Pro7's.
- Types: Pro7's.
- Data: `IsSubableOp` is `static`. Deleted its stale "global while lifted" comment, and its GC_2_6 binding (only this unit's two functions reference 0x5dff68). Links.
- Header: declares the two functions IrOptimizer.c uses.
- Headers relied on: IroLinearForm.h (IROLinear: `u.node`, `u.diadic`, `u.monadic`, flags). Open problems there: invented union members (`branch`, `swtch.cond`, `args3`, `f1e`, `pad20[20]`), and `u.label` where Pro7 has `u.label.label`/`.x4`. Registers.h: `VarInfo` lives there (Pro7: objects.h), so it must be included. IroUtil.h (IRO_IsConstant, IRO_IsVariable, IRO_IsRegable): fine.
- Open: IroLinearForm.h's IROLinear placeholders (owned by IroLinearForm).
- Adversarial review: text compared line by line with Pro7 (identical apart from braces/TRUE/FALSE); binding removal checked (0x5dff68 is `_IsSubableOp` scope:local in symbols.txt, no binding left); header users checked. IRO_IsVectorTempCandidate is Pro7's dead function with no 2.6 evidence either way (harmless: dropped by the linker). Nothing broke; grade holds.
- Header round H2 (October 16): IroLinearForm.h's `branch`/`f1e`/`pad20` are gone and `u.label` is Pro7's {label, cond}; `swtch.cond` and the other placeholders are TODO names; VarInfo is objects.h's. Resolved as TODO names.

### src/optimizer/IroJump.c: SOLID WITH TODOS
- Boundary: 6 rows 0x49cf80..0x49d4eb = Pro7 IroJump.c (CheckChain .. IRO_RemoveLabels) reversed. Wii module irojump for 3. Links.
- Flags: deferred (reversed). `-sym off` loses one function.
- Functions: 6/6 exact, rewritten as Pro7's text (it was an invented-name paraphrase with `(CLabel**)&...->u.label` casts, `kind == 0xd/0xf` and `flags &= ~1`). One departure: IRO_MakeReachable walks with a separate `scan`, where Pro7 reuses the parameter `node`. That is a needed shape (Pro7's spelling is 62.2%), commented.
- Behaviour: exact; constants by name (EAT_CATCHBLOCK, EAT_SPECIFICATION, IROLF_1). If/IfNot targets are read through `u.branch.label` (the same offset as Pro7's shared `u.label.label`).
- Names: Pro7's. IRONode's `reachable`/`visited`/`referenced` stand for Pro7's `x36`/`x37`/`x39` (see IroFlowgraph).
- Types: `CException` (the repo's stand-in for Pro7's `ExceptionAction`): its `next` is Pro7's `prev`, `kind` is Pro7's `type`, `data.catch_block.label` is `catch_label`, `data.specification.label` is `unexp_label`.
- Data: only IRO_Dump literals; links.
- Header: 5 functions, all used by IrOptimizer/IroUnrollLoop/IroPointerAnalysis.
- Headers relied on: CFunc.h (`CLabel.target` union is a stand-in for Pro7's `stmt` field, cast at its uses). CException.h (stand-in for ExceptionAction, as above; also `pad_1d`, and `u.var`-style placeholder unions such as `types.type[4]`, `slots[6]`, `bytes[24]`). CFunc.h (`Statement.dobjstack` is `CException*` with a TODO saying Pro7's ExceptionAction). Switch.h (SwitchInfo/SwitchCase): fine. IroLinearForm.h (as in IroSubable).
- Open: the CException stand-in (owned by CException.h/Exceptions.h).
- Adversarial review: Pro7's IRO_MakeReachable (walking with `node`) is 62.2%, and the diff shows why: 2.6 loads the parameter into eax and walks in ebp, a separate variable, so `scan` is the original's shape (comment now says so). Fixed a `reachable = 1` to TRUE. Found the CLabel stand-in: CFunc.h's `union {stmt; node;} target` is not Pro7's (Pro7 has `Statement *stmt` and casts `(IRONode *) label->stmt`): reported, not changed (shared header). Grade holds.
- Header round H2 (October 16): the CException stand-in is Pro7's ExceptionAction (Exceptions.h); CFunc.h's CLabel is Pro7's `stmt` and `Statement.dobjstack` an ExceptionAction*; IroLinearForm.h/IroFlowgraph.h keep TODO names for Pro7's placeholders. Resolved as TODO names.

### src/optimizer/IroFlowgraph.c: SOLID WITH TODOS
- Boundary: 6 rows 0x4712a0..0x471e56 = Pro7 IroFlowgraph.c reversed; the inlined statics are AddSucc, AddLabelSucc, AddSwitchSucc and AddPred. Assert "IroFlowgraph.c" line 126. Pro7 defines IRO_VarKills, IRO_Avail, IRO_FuncKills and IRO_ExprKills here (COMMONs); they had been defined in IroCSE.c and are now moved here. COMMON is laid out by name, so both units still link.
- Flags: deferred (reversed). `-sym off` loses 3 functions.
- Functions: 6/6 exact. Each departure was tried in Pro7's spelling:
  - AddSwitchSucc counts with `i++` before the advance (Pro7's order is 99.8%). Needed shape (catalogued).
  - AddPred takes `UInt32 b` (Pro7: `UInt16`, which loses a function). 2.6's type.
  - IRO_ComputeSuccPred declares `linear` last (Pro7's place: 75.9%, other code). Needed shape.
  - IRO_ComputeDom declares in the order bv, node, repeat, i (Pro7's order is 81.9%: registers). Needed shape.
  - IRO_MergeFlowGraphNodes passes `succ` (Pro7 indexes IRO_NodeTable again, which loses a function). Needed shape.
  - `linear->stmt &&` before IRO_FunctionCallMightThrowException in three places: a 2.6 change (the null test is in the code).
  - Labels map to nodes through the repo's `CLabel.target.node`, where Pro7 casts `label->stmt`: the same field.
- Behaviour: exact; EAT_* and IROLF_1 by name (were 13, 15, 1).
- Names: Pro7's for functions, locals and globals (`Labels`, was `clabels`). IRONode members (header) are TODO-marked: `reachable`/`visited`/`referenced` (Pro7 x36/x37/x39) and `copyOut` (x2A, dumped as "AA") are invented; `x26` and `x3C` keep Pro7's placeholder names (x26 is only ever cleared; x3C is IroExprRegeneration's mark). `in`/`out`/`gen`/`kill` are IRO_DumpGraph's labels.
- Types: IRONode is Pro7's layout (`#pragma pack(2)` stands for Pro7's `align=mac68k`). The explicit `UInt8 pad43` placeholder is removed: the size stays 0x44 through the packing.
- Data: no statics; links.
- Header: the IRONode struct, the node globals, the four kill vectors and 5 functions, all used elsewhere. Pro7's `IRONodes` queue type and its inline helpers live as a private copy in IroExprRegeneration.c, not here (reported, not moved: another unit).
- Headers relied on: InlineAsmPPC.h (IAEffects, CodeGen_GetAsmEffects: fine), CException.h/Exceptions.h (stand-in, above), IroLinearForm.h (above), IroCSE.h/IroPropagate.h/IROUseDef.h (IRO_FirstExpr, IROAssign, IROUse lists), BitVector.h. BitVector.h: fixed in the adversarial pass (includes inside the guard, Pro7's Bv_SetBit text).
- Open: the invented and placeholder IRONode member names (TODO); the IRONodes private copy in IroExprRegeneration.c; CFunc.h's CLabel `target` union (stand-in for Pro7's `stmt`) and `Labels` defined in no source.
- Adversarial review: the labels list was the 1.x name `clabels`; Wii's data public and Pro7 call it `Labels` (0x5eabe4), and COMMON hash order agrees (`Labels` 3519 between `disable_optimizer` 3500 and `prologue` 3529; `clabels` 382 does not fit). Renamed everywhere with `tools/rename.py clabels Labels` (CFunc.h, CFunc.c, CExpr.c, InlineAsm.c, IroUtil.c, IroFlowgraph.c, every version's bindings/symbols): no function changes, every touched unit that linked still links. The other departures re-checked against the catalogue; Wii confirms the module (IRO_ComputeSuccPred, IRO_ComputeDom, IRO_BuildFlowgraph, IRO_NewFlowGraphNode, IRO_MergeFlowGraphNodes in IroFlowgraph). Open adds the CLabel `target` union (CFunc.h) and `Labels` having no definition in any source (Pro7 defines it in CFunc.c). Grade holds.

### src/optimizer/IrOptimizer.c: SOLID WITH TODOS
- Boundary: 6 rows 0x42dd20..0x42e9ab = Pro7 IrOptimizer.c reversed; CountRefToObject is inlined. Asserts "IrOptimizer.c". Pro7's IRO_Cleanup is dead in 2.6 (no data) and was added back as Pro7's dead function. `IRO_Log` is now defined here, Pro7's place (it was in IroDump.c). AGENTS.md says that move broke the relink; it no longer does, since COMMON is ordered by name hash.
- Flags: deferred (reversed). `-sym` makes no difference to the code here; `on` like the unit's neighbours.
- Functions: 6/6 exact. Departures:
  - CountRefToObject clamps a copy `index` where Pro7 clamps `depth` itself. Needed shape (Pro7's spelling makes CountUsage 59.8%); local copy, catalogued.
  - CodeGen_UpdateOptimizerOptions is chained assignments where Pro7 has a `flag` local (Pro7's form is 50%). Original's shape: chained assignments are a 2.6 idiom.
  - 2.6 changes, all in the code: IRO_CheckInit only under `copts.warn_uninitializedvar` (copts+0xd2); `changed = FALSE` at the top of the repeat loop, with `changed |= IRO_RemoveRedundantJumps()`; CountUsage after IRO_RegenerateExpressions; ResetVars() after IRO_ZapVarPtrs.
  - Now spelled with IS_LINEAR_ENODE/IS_LINEAR_MONADIC as Pro7 (still exact) and IAOpnd_3 (was a bare 3).
- Behaviour: exact; the copts members are read at the offsets of their pragma names.
- Names: Pro7's.
- Types: Pro7's.
- Data: statics `stIsSetup` and the function-local `ENodeArraysHaveBeenInitialized`. Their GC_2_6 bindings were deleted (only this unit references 0x5dec20/21; symbols.txt now names them from the object). The globals are COMMONs, named by bindings. Links.
- Header: dropped the unused `extern`s DoScalarize, EarlyReturn and IRO_CPFirstTime (no other unit uses them; nothing changed in any object).
- Headers relied on: InlineAsmPPC.h (`IAOpnd_3` is a placeholder enumerator name). The Iro* headers for the phases. CParser.h (`copts`).
- Open: IAOpnd_3's name (InlineAsmPPC.h).
- Adversarial review: CountRefToObject's copy re-tried (Pro7's clamp of `depth`: CountUsage 59.8%; a ternary index: 55.2%): holds. The 33 `IRO_DumpAfterPhase(..., 0)` and `IRO_FindExpressions(NULL, 0)` calls passed literals to Boolean parameters: now FALSE (still exact). Found a private copy elsewhere: ObjGen_PPC_EABI.c:170 declares `extern void IRO_Setup(void);` instead of including IrOptimizer.h (reported to its owner). Grade holds.
- Header round H2 (October 16): `IAOpnd_3` is `IAOpnd_GlobalObj`, TODO-invented (InlineAsm.h DONE WITH TODOS); the Iro* headers are reviewed; copts is CParser.h's. Resolved as a TODO name.

### src/optimizer/IroMalloc.c: SOLID WITH TODOS
- Boundary: 12 rows 0x52b220..0x52ba68 = Pro7 IroMalloc.c reversed; Wii module iromalloc for 9. Pro7's dead IRO_msize, IRO_realloc, IRO_calloc and IRO_pool_free_all are present (no data). Links.
- Flags: deferred (reversed; non-deferred loses 2). `-sym off` loses 3.
- Functions: 12/12 exact. 2.6's allocator is MSL's alloc.c of its time (lib/msl alloc.c 1.16, 1999: _MSL_MALLOC_DESIGN_2, fixed pools {12,20,36,68}, alignment 8); Pro7's is the 16-byte Mac variant. Every departure from Pro7 is MSL's text:
  - FLAGMASK 7, the pool sizes, and 0x50 as SubBlock_min_size: 2.6 constants.
  - Block has no x8/xC reserve words (MSL's align_ratio 2).
  - Block_subBlock's `start` local and `best = check`: MSL's `st` and `max_found = sb_size`.
  - deallocate_from_var_pools' comma expression: MSL's own `Block_empty` macro and `_sb` local. Original's shape.
  - FixBlock_construct's `char*` walk: MSL's `p`/`np`.
  - IRO_malloc's `result` local and IRO_free's early return: MSL's malloc/free.
  - unlink_block's two separate NULL stores: MSL's `unlink`.
- Behaviour: exact. Pro7's IRO_pool_free_all walks the blocks without freeing them (dead in 2.6; kept as is).
- Names: the struct members are now MSL's (`size_`, `bp_`, `prev_`, `next_`, `max_size_`, `reserve_`, `client_size_`, `block_`, `start_`, `n_allocated_`) instead of Pro7's placeholders (x0, x14, x8, xC, remain, unk, a, b, count). `fix_pool_sizes` is `#define`d to `IRO_fix_pool_sizes`, because binding names are global and MSL's alloc_g already binds `_fix_pool_sizes`; the plain name loses 3 functions.
- Types: `FixStart.n_allocated_` is now MSL's unsigned `size_t` (was `long`; same code).
- Data: `fix_pool_sizes`, `start_`, `fix_start` and `initialized` are static; links.
- Header: added IRO_InitializeAllocator and IRO_TerminateAllocator (Pro7 declares them here) and removed IroPointerAnalysis.c's private `extern` copies (IroPointerAnalysis loses nothing).
- Headers relied on: CompilerTools.h (galloc). Fine.
- Open (TODO): the helper structs and macros (SubBlockBase, SubBlockTail, BlockTail, FixSubBlockBase, BLOCK_START, VARBUF_*, FIXBUF_SIZE) are Ninji's stand-ins for MSL's `Block_start`, `SubBlock_size`, `classify` and the `*_overhead` constants. A full rewrite to MSL alloc.c's own macros would be the original text. Not done (time-box).
- Adversarial review: the claimed TODO for the helper structs and macros was not in the source; added it. deallocate_from_var_pools' hand-inlined comma expression is now MSL's own `Block_empty` macro with its `_sb` local (MSL alloc.c 1.16 lines 326-328; still exact), so the comma is MSL's text, not ours. `_fix_start`/`_fix_pool_sizes` bindings are alloc_g's (MSL), not this unit's. Wii's IroMalloc is a later heap-based allocator, so it gives no names for these statics; Pro7's stand. Grade holds (with the TODO).

### src/optimizer/IroEmptyLoop.c: SOLID WITH TODOS
- Boundary: 7 rows 0x49ee70..0x4a00b2 = Pro7 IroEmptyLoop.c reversed; asserts "IroEmptyLoop.c" (3). Links.
- Flags: deferred (reversed). `-sym` makes no difference to the code here.
- Functions: 7/7 exact. Departures, each tried in Pro7's spelling:
  - EmptyLoop returns `int` (Pro7: Boolean, 99.5%). 2.6's type (catalogued).
  - CanRemoveRedundantLoop's `if (RedundantLoopCheck(loop)) return TRUE; return FALSE;` (Pro7's `!= 0` is 98.4%). Catalogued 2.6 shape.
  - EmptyLoop's declaration order, the separate `k` counter for the successor loop (with `i`: 83.4%), the block-local `node` (Pro7's re-indexing: 89.2%), `counter++`/`counter3++` before the assignment beside them (99.7% each), and the second branch's own `type2`/`const2`/`save2`. Pro7's whole body is 56.7%. These are register-allocation shapes found by search. They are plausible as the original's (a pasted block with fresh locals), not proven; commented.
  - `is_unsigned` (CParser, 0x4e7fc0, the call target) where Pro7 has the inline `IRO_IsUnsignedType`.
- Behaviour: exact. Bug in the original (Pro7 too), kept: `LoopNode->last->type != IROLinearIf || ... != IROLinearIfNot` is always true.
- Names: Pro7's. `IROLoop.init`/`cond`/`lo`/`hi` are the repo's names for Pro7's nd14/nd18/index20/index24; `x28`/`x30` are still placeholders.
- Types: Pro7's.
- Data: only literals; links.
- Header: IRO_FindEmptyLoops (IrOptimizer uses it).
- Headers relied on: IroLoop.h (IROLoop placeholders `x28`, `x30`; LoopFlags_200/10000/20000 unnamed flags). IroVars.h (`VarRecord.inductionState` for Pro7's xA: invented). IroFlowgraph.h and BitVector.h (above).
- Open: the EmptyLoop register shapes are unproven as the original's (TODO comment), and the IroLoop.h placeholders.
- Adversarial review: r20/r21/r24/type20 are Pro7's (Ninji's) local names, not ours; no gc1 counterpart exists to settle the register shapes. The TODO stands as documented (not re-searched). Pro7 also has CheckStepOverFlow2's `CInt64_Greater(work, nd1value)` against the unsigned branch's `nd2value`: an original bug, kept. Grade holds.

#### Shared evidence for CLMain.c, CLIO.c, CLErrors.c, CLStaticMain.c, CLProj.c, MemUtils.c, ErrMgr.c

# Review: the command-line core (CLMain, CLIO, CLErrors, CLStaticMain, CLProj, MemUtils, ErrMgr)

Checked at the end: `ninja build/GC_2_6/compare.json` 4020/4077 exact, no function's `exact` flag lost against
compare_start.json (none gained either); `tools/layout.py GC_2_6`; `tools/relink.py GC_2_6 --only` links all seven
units into the original; `clang_check` 0 errors on each (and on the C++ includers of CLProj.h); clang-format clean.

Shared headers, reviewed once (the entries below refer to this list):
- **CLIO.h** (mine): `MessageRef` (Pro7's), the `Msg_*` message types (moved here from CLIO.c; Pro7's cmdline.h calls them
  `CLStyledMessageDispatch_Type0..5`), the API other units call. Trimmed to what other units use. Open: `CLIO_OutOfMemory()`
  is an invented macro (CLLoadAndCache, CLPrefs, CLDependencies use it; Pro7 writes `fprintf(stderr, "\n*** Out of
  memory\n"); exit(-23);` inline), and it `#include`s <stdio.h>/<stdlib.h> inside `extern "C"`; MSL's
  `__assertion_failed` is declared here for oslib.h's `OS_ASSERT` (belongs to an MSL assert header or oslib.h). It keeps
  `StringExtras.h`, `<setjmp.h>`, `MsDos.h` only for its includers (see the AGENTS.md finding).
- **CLMain.h** (mine): Pro7's `CLState` (types now `OSType`, `const char* programName`) and globals. Open: the type name
  `CLState` is Ninji's (TODO: invented name); `OptsCmdLineState_0..3` and `Result_*` are Ninji's placeholder names
  (shared with CLFileOps).
- **cmdline.h**: forward typedefs; holds stand-in struct names (`StringLookupResult`, `DropinFileValue`, ...) for other
  units. **oslib.h**: `OSSpec`/`OSPathSpec` (260, Win32), `OS_ASSERT`. **Memory.h**: `StorageHandle*` stands in for
  Pro7's `Handle` (AGENTS.md's known item). **CLPrefs.h**: `NameTableEntry` stands in for Pro7's `PrefPanel`.
  **win32.h**: a hand subset with IDA-export leftovers (`_struct_519`; `_COORD`/`_SMALL_RECT` typedefs
  under tag names; no `MAX_PATH`); the adversary added WINCON.H's `CONSOLE_SCREEN_BUFFER_INFO` typedef and
  `CTRL_C_EVENT`/`CTRL_BREAK_EVENT`. **msl/startup_win32.h** was an invented header (stand-in `CFileRec` with a
  `readonly` that is MSL's `translate` and a `trailingPadding` placeholder, an unused `EnvironmentBlock`, a
  `ThreadLocalData` with made-up fields, `_GetThreadLocalData` typed `SInt32*`): deleted by the adversary for
  **msl/crtl.h** (MSL's `FileStruct`, `_HandleTable`) and **libc/errno.h** (Pro 6 MSL's `_ThreadLocalData`, `errno`).
  **StringUtils.h**: `mprintf(char* argument, int kind, char* format, ...)` has wrong parameter names/types (Pro7:
  `char* mybuf, unsigned int len, const char* format`). **CWPlugins.h**: no `CompilerMapping.h` (`Lang_C_CPP`,
  `Lang_Pascal`), so CLIO writes `'c++ '`/`'pasc'`. **pref_structs.h**, **ClientGlue.h**, **CLPlugins.h**,
  **CLTarg.h**, **TextUtils.h**, **MsDos.h**, **Generic.h**, **FileHandles.h**, **CLLicenses.h**, **LowMem.h**,
  **Resources.h**, **CLParserCallbacks_v1.h**, **CLPluginRequests.h**, **CLToolExec.h**, **StaticParserGlue.h**,
  **ParserGlue-eabi-ppc-cc.h**, **cc-eabi-ppc-mw.h**: only the declarations used were looked at; nothing wrong seen in
  those, not reviewed as wholes.

### src/driver/CLMain.c: SOLID WITH TODOS
- Boundary: 28 rows 0x40a950-0x40b958 = Pro7 CLMain.c's functions in Pro7's order (names-2.6: pro7 clmain 27/27 +
  dead CLT_GetTargetList, Wii module CLMain); its literals and statics are in CLMain's .data/.bss (relink). 2.6 has
  `special_license_file`/`--use-license-file` (Pro7's "TODO: special function goes here"), not Pro7's
  `Framework_GetEnvInfo` (Mac).
- Flags: pro6 `-O4,p -sym off -Cpp_exceptions off`; ascending addresses, so not deferred (deferred: 24/28); `-sym on`:
  22/28.
- Functions: 28/28 exact. Departures: Main_PreParse's early `return` for Pro7's `if (*argc > 1) {}` (needed shape:
  Pro7's 93.7); `secret`/`warned*`/`MainInitialized` at file scope (2.6's .bss order); `SInt32 vislang` (Pro7
  FourCharCode: 99.1); 2.6 changes: four special options, `exename[260]` plus the ".exe" suffix, `stderr2stdout = 1`
  for version 0x1000 panels, no Framework_GetEnvInfo. Main_Driver's `if (!(result = Main_ResolveProject())) { (void)0; }`
  keeps 2.6's `test; jne` (Pro7's plain assignment, `{}` and `;` tried: 96.7): original's shape, story a compiled-out
  debug macro (TODO kept).
- Behaviour: straight Pro7 plus the 2.6 changes above, every function exact. No bugs found.
- Names: Pro7's/Wii's. Invented: none in the .c (CLState in the header, see above).
- Types: Pro7's, except `NameTableEntry*`/`StorageHandle*` in UpdatePrefPanels (header stand-ins).
- Data: links; `secret`, the `warned` flags and `MainInitialized` static; `gProj`'s binding (only CLMain references it)
  deleted.
- Header: `exit_program`, `mainProj`, `gProj`, `Main_PreParse` dropped from CLMain.h (no other user).
- Headers relied on: CLMain.h, CLIO.h, CLErrors.h, CLPrefs.h (NameTableEntry stand-in), Memory.h (StorageHandle),
  pref_structs.h, CLPlugins.h, ClientGlue.h, CLParserCallbacks_v1.h, CLPluginRequests.h, CLLicenses.h, CLTarg.h,
  Generic.h, LowMem.h, Resources.h, MemUtils.h, CLFileOps.h, CLToolExec.h, CLErrors.r: problems as listed above.
- Open: header stand-ins (`PrefPanel`, `Handle`), the `CLState`/`OptsCmdLineState_N` names; the `(void)0` TODO.
- Header round H2 (October 16): CLPrefs.h's `NameTableEntry` is Pro7's `PrefPanel`; `StorageHandle` is Pro7's `Handle`; `CLState` (CLMain.h) and `OptsCmdLineState_Unset/NoAction/Compile/CompileAndLink` (pref_structs.h) are TODO-invented names. Resolved; the unit's `(void)0` TODO stays.

### src/driver/CLIO.c: SOLID WITH TODOS
- Boundary: 39 rows 0x415830-0x41720e = Pro7 CLIO.c's in order (appendText/appendChar/newLine are the `inline`
  helpers' out-of-line copies after their first user); assert file "CLIO.c"; .data 0x5a1668 (8-aligned), .bss
  0x5ddcf0. Pro7's dead FixHandleForIDE, IO_HelpInitialize, ProgressFunction, CLPrintWarning kept (no data).
- Flags: pro6 `-O4,p -inline off -sym off -Cpp_exceptions off`: `-inline auto` 28/39, `auto,deferred` 26/39, `-sym on`
  31/39 (the helpers' out-of-line copies need `-inline off`).
- Functions: 39/39 exact. Departures: WrapText joins Pro7's three early returns into one test and counts `wrapped++`
  before `f->pos--; f->col--;` (needed shape: each Pro7 spelling tried, 91.7/99.0); appendChar `maxlen <<= 1` (Pro7
  `*= 2` 94.1, `= maxlen * 2` 92.9: re-tried); IsLikelyAnImporter `if (...) return FALSE; return TRUE;` (Pro7's
  `return a != b`: 87.5); GetFileInfo's early return (Pro7's `if (ref)`: 90.9); styledMessage_Default's
  `(cols = optsEnvir.cols)` local and `lineBuf[324]` (the original's frame is 0x248 = 4 + 324 + 256, lineBuf at
  esp+4, msgBuf at esp+0x148; Pro7's 320 90.2, no local 98.1, `int cols` 98.1, a separate statement 98.1: re-tried,
  commented in the source until the comment pass); CLStyledMessageDispatch reuses `ptype` for `msgStyle` (original's shape: reuse of an
  existing local). catchinterrupt now `ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT` (was `<= 1` with
  the names in a comment; matches). 2.6 changes: Win32 console setup (SetConsoleCtrlHandler,
  GetConsoleScreenBufferInfo, text translation turned off on the three standard handles), `"\r\n"` line ends, 0x1000
  buffers and other setvbuf modes, stderr2stdout == 2.
- Behaviour: original's bugs kept: `errno` (CLStr10 "Write error on output (errno=%ld)", CLStr8) is read at offset 4 of
  the thread-local data, but the linked MSL (lib/msl, Pro 5 + updater: `{int errno; random_next; ...}`; its strtod,
  strtol and stdio store errors at `[eax]`, every other `_GetThreadLocalData` caller in the binary uses offset 0 or
  0x14 = gmtime_tm) keeps errno at 0. Pro 6's MSL headers (ThreadLocalData.h, 2000/08/19) put a `next` link first, so
  errno is at 4: the driver (and ScanFloatX86) were compiled against Pro 6's MSL headers and linked with the older
  library, and read `random_next`. A real bug of the original's build, now reproduced by the plain `errno` through
  `include/libc/errno.h` (Pro 6's layout and macro), not by `_GetThreadLocalData()[1]`. Write/AppendHandleToFile pass
  260 for a 256-byte `path` (written `sizeof(OSPathSpec)`, commented). Ours fixed: the setvbuf constants named
  (reviewer); `_HandleTable[i]->readonly = FALSE` was a misreading: offset 4 of MSL's `FileStruct` is `translate`
  (crtl.h, Pro 5 and Pro 6 alike), so 2.6 switches the standard handles to binary mode, consistent with its own
  `"\r\n"` line ends (adversary).
- Names: Pro7's; `GetFileInfo` is `#define`d to its row name `CLIO_GetFileInfo` (Projects.c defines a global
  GetFileInfo). Invented (NOTE since the comment pass): `TextBuffer` (was Ninji's `UnkCLIOStruct`), `pagePrompt`
  (Pro7's `ioLineNum`, counted in CLPrintDispatch and never read, is what the pager used); the helper names
  appendText, appendChar, newLine are Ninji's (rows).
- Types: Pro7's; MSL's `FileStruct` and `_ThreadLocalData` (was the invented startup_win32.h); `StorageHandle*` for
  `Handle` (header); `CONSOLE_SCREEN_BUFFER_INFO` (was the tag name).
- Data: relinks; the statics are declared in reverse of Pro7's order (2.6's .bss has Pro7's first one lowest);
  msgnames before GuessTool and msgnames_gcc after styledMessage_Default proven by moving each (relink
  fails). 22 fragment bindings into CLIO's .data/.bss deleted (only CLIO's own exact functions referenced them).
- Header: see CLIO.h above; functions only CLIO.c calls left the header (prototypes at the file top where a use
  precedes the definition).
- Headers relied on: CLIO.h, CLMain.h, CLErrors.h, CLPlugins.h, FileHandles.h, Generic.h, MemUtils.h, StringUtils.h,
  msl/crtl.h (new, MSL's), libc/errno.h (new, Pro 6 MSL's), win32.h (IDA-export leftovers), Memory.h, oslib.h,
  CWPlugins.h (no Lang_*).
- Open: invented `TextBuffer`/`pagePrompt`/helper names (no source has them), CLIO.h's `CLIO_OutOfMemory` macro (needs
  edits in three other units) and `__assertion_failed`.

### src/driver/CLErrors.c: SOLID
- Boundary: 9 rows 0x4087b0-0x408b20, Pro7 CLErrors.c in order (Wii module CLErrors 9); .data 0x58da18 (the four
  literals), .bss 0x5dc5e8 = `stmsg`+`stbuf` (512).
- Flags: pro6 `-O4,p -sym off -Cpp_exceptions off`: deferred 3/9, `-sym on` 8/9. The file's `#pragma exceptions
  off/reset` did nothing (removed; still exact and links).
- Functions: 9/9 exact, Pro7's text: CLGetErrorString and CLMessageReporter `static` again (Pro7; Wii has no publics
  for them), `messagetype*` and `CLStr99`/`CLStr100` for the literals, and the `(UInt8)3/2/5` casts gone: `Msg_Error`,
  `Msg_Warning`, `Msg_Status` are byte-sized enumerators, which is what the casts imitated.
- Behaviour: Pro7's. No bugs found.
- Names: Pro7's. Invented: none.
- Types: Pro7's.
- Data: relinks; `_DAT_0057d5f8` and `_diagnostic_message_buffer` (fragments in `stbuf`/`stmsg`) deleted (only
  CLMessageReporter referenced them).
- Header: CLMessageReporter/CLGetErrorString prototypes removed; CLStr enum is CLErrors.r's ids.
- Headers relied on: CLErrors.h, CLIO.h (open items above), CWPlugins.h (added the SDK's `messagetypeInfo/Warning/
  Error` enum), StringUtils.h (mvprintf fine; mprintf's declaration wrong), TextUtils.h, MsDos.h.
- Open: CLIO.h's items.
- Header round H2 (October 16): CLIO.h lost `CLIO_OutOfMemory`, its mid-header includes and `__assertion_failed` (now oslib.h's); StringUtils.h's mprintf is Pro7's. Resolved.

### src/driver/CLStaticMain.c: SOLID
- Boundary: one row, `main` at 0x4024f0 (Wii module CLStaticMain), .data 0x582298 (its five literals).
- Flags: now pro53 `-O4,p -opt noschedule -sym off -Cpp_exceptions off` (was `-inline auto` plus `#pragma exceptions
  off` and `#pragma scheduling off` around main): without `-opt noschedule` 91.5; with exceptions pro53 gives main an
  ebp frame; `-inline` irrelevant (one function); `-sym` makes no difference here (off as its neighbours).
- Functions: 1/1 exact. Departures: four `OSType` variables, Pro7's `lang` also taking the parser style, declared
  lang, type, cpu, os (Pro7's five variables 81.5-86.3; Pro7's order gives other slots): needed shape, plausibly the
  original's (a reused local); `"\r\n"` in the messages (2.6). `int ret` is Pro7's (the old `SInt32` was not needed).
- Behaviour: Pro7's. No bugs.
- Names: Pro7's (old `primaryToolIdentifier`-style names gone).
- Types: `OSType`, `int`.
- Data: relinks.
- Header: CLStaticMain.h deleted: it declared MSL's `__stdio_exit`, `__aborting`, `_SetupArgs`, `_RunInit` (nobody
  used them through it) and `main`; the `CMDLINE_BUILD_*` externs moved to the file top, as Pro7 has them.
- Headers relied on: ClientGlue.h, StaticParserGlue.h, ParserGlue-eabi-ppc-cc.h, cc-eabi-ppc-mw.h (`UInt32*` for
  OSType: same type). `CMDLINE_BUILD_DATE/TIME` are defined by no source (bound in bindings.json).
- Open: none in the unit.
- Header round H2 (October 16): cc-eabi-ppc-mw.h, StaticParserGlue.h, ParserGlue-eabi-ppc-cc.h and ClientGlue.h checked: nothing pending (Driver family). Resolved.

### src/driver/CLProj.c: SOLID
- Boundary: 2 rows 0x419e50-0x419eb0 (Wii module CLProj), .data 0x5a1c50 (`""`, the assert text, "CLProj.c").
- Flags: pro6 `-O4,p -sym off -Cpp_exceptions off`; `-sym` and deferred make no difference (2 functions, ascending).
- Functions: 2/2 exact, Pro7's text verbatim (`TRUE`).
- Behaviour: Pro7's.
- Names/Types: Pro7's (`Project { Target* targets; OSSpec projectDirectory; }`).
- Data: relinks; `_data_0054beec`, `_this_not_null_string` (literals) deleted.
- Header: prototypes name the parameter `proj` (`this` broke nothing in C, but C++ includers parse it); `Target`
  from cmdline.h. Its other includes (StringExtras.h, win32.h, CLAccessPaths.h, MsDos.h) only serve includers.
- Headers relied on: CLProj.h, cmdline.h, oslib.h, CLIO.h (OS_ASSERT's `__assertion_failed`), CLTarg.h, MsDos.h.
- Open: the transitive includes in CLProj.h; CLIO.h's items.
- Header round H2 (October 16): CLProj.h includes only common.h, cmdline.h and oslib.h; CLIO.h's items are gone. Resolved.

### src/driver/MemUtils.c: SOLID
- Boundary: 5 rows 0x417280-0x417400, Pro7's whole file in order (Wii module MemUtils, which adds two later
  functions); .data 0x5a1960 = its two formats, " for ", "".
- Flags: pro6 `-O4,p -sym off -Cpp_exceptions off`; deferred 3/5; `-sym` no difference.
- Functions: 5/5 exact, Pro7's text (names `what`, `ret`, `old`, `str`, `ptr`; `int size`, as Pro7), `__stdcall`
  (Wii's `_xmalloc@8` ...). The `XMEMCALL` macro and the stray includes/comment are gone.
- Behaviour: Pro7's. Names, types: Pro7's.
- Data: relinks.
- Header: the five functions; keeps `<setjmp.h>` for includers.
- Headers relied on: common.h only.
- Open: none.

### src/driver/ErrMgr.c: SOLID
- Boundary: 2 rows 0x4440d0-0x444370 (Pro7 MacEmul/ErrMgr.c whole, in order), .data 0x5a9010 (8-aligned) = the 36
  messages and "Operating system error %ld".
- Flags: pro6 `-O4,p -sym off -Cpp_exceptions off`: `-sym on` 0/2; ascending, not deferred.
- Functions: 2/2 exact, Pro7's text (`return NULL` for its `0`); GetSysErrText `__stdcall` (its callers' convention).
- Behaviour: Pro7's (`%ld` with an `int` argument is the original's).
- Names, types: Pro7's.
- Data: relinks.
- Header: GetSysErrText only.
- Headers relied on: common.h.
- Open: none.
- Adversarial review: attacked the errno claim, the startup_win32.h types, every CLIO/CLMain workaround and the
  headers' users. Broke: the errno reads were right as code but wrongly spelled and half-explained: they are `errno`
  through Pro 6 MSL's header layout (errno at offset 4, after Pro 6's new `next` link), linked with the Pro 5 MSL whose
  errno is at 0 (every MSL caller in the binary stores at offset 0): the original's bug, now written as `errno` with
  `include/libc/errno.h` (CLIO and ScanFloatX86 exact); `_HandleTable[i]->readonly` was MSL's `FileStruct.translate`
  (an invented header, now MSL's crtl.h); catchinterrupt's `<= 1` now the WINCON names; lineBuf[324]/`cols`
  re-measured against the original's frame and commented. Held: appendChar's `<<= 1`, styledMessage_Default's
  shape, Main_Driver's `(void)0` (an `&&` chain needs it too; `{}`, `else {}`, `!= 0` 96.7-96.8), CLStaticMain, every
  header declaration has an outside user. 4020/4077 exact before and after, nothing lost; layout; CLIO, CLMain,
  ScanFloatX86 and CTemplateClass relink; clang_check 0; clang-format clean.

### src/driver/CLPlugins.c: SOLID WITH TODOS
- Boundary: 40 rows, 0x408cc0..0x40a950, in Pro7 CLPlugins.c's order. Wii's CLPlugins module has the same functions in the same order, plus later additions (Plugin_GetPluginInfo, Plugins_GetDisassembler, Plugins_Iterate). All 13 OS_ASSERT lines are Pro7's (251..1271). Pro7's dead Plugins_CL_HaveMatchingTargets, Plugins_MatchName, Plugins_CL_MatchFileType and Plugins_GetCompilerForLinker appear in neither 2.6 nor Wii and have no data, so they are left out. The dead static Plugin_GetDisplayName is kept: it is Pro7's, and Wii has it live at 0x415010. Relink: links into the original, so .data and .bss are cut right.
- Flags: functions ascend in address, so not deferred (correct). `-sym on` loses 16 of 40, so `-sym off` is shown to matter.
- Functions: 40/40 exact. Departures from Pro7:
  - GetToolVersionInfo: a 2.6 change. It is Win32 VerQueryValue code where Pro7 has a Mac stub. It is now `static`, as in Pro7, and Wii has it unnamed.
  - `strcmp(...) == 0` in Plugin_MatchesName: needed shape (Pro7's `!strcmp` gives 62.5%).
  - `(dropinflags & 1) != 0` in Plugins_GetPluginList: needed shape (Pro7's gives 82.4%).
  - `UInt32 cpu`/`UInt32 os` copies in Plugins_Add's two loops: needed shape (Pro7's spelling gives 99.6%, tried one at a time). Classified as the original's shape: a plausible unsigned copy of the 2.x-signed element.
  - `SInt32 vislang`: needed shape. Pro7's CWDataType gives 99.6%, and the 2.x field is SInt32.
  - Static declaration order plus `#pragma pack(2)`: needed for .bss (TODO comment explains the layout).
  - The rest is Pro7's text.
- Behaviour: genuine bugs in the original: GetToolVersionInfo allocates `strlen + 2` for the copyright, enough for one (c) expansion of 0xA9 only. fn.py diff showed the version words are VS_FIXEDFILEINFO's dwProductVersionMS/LS (offsets 0x10/0x14), not the file version. Fixed: they had been `fixed[4]`/`fixed[5]`. Nothing wrong in the original.
- Names: Pro7/Wii names throughout. Invented: GetToolVersionInfo's locals only (no reference).
- Types: `DropInFlagsSize[]` now uses the SDK's `sizeof(DropInFlagsV1)` instead of `sizeof(DropInFlags) - 2`. The magic numbers 0x104 and 0x50 are now `sizeof`. `fixed` is a `VS_FIXEDFILEINFO*`.
- Data: the relink is byte-identical. Removed the 2.6 bindings `_plugin_desc` (retdf), `_toolVersion`, `_toolVersionInfo` and `_useToolVersion`, all statics this linking object defines. Function bindings at its rows remain (repo-wide practice).
- Header: removed the `GetToolVersionInfo` prototype and a stale "lifted" comment. (Adversary) also removed the 12 declarations no other unit uses (Plugin_GetVersionInfo, Plugin_GetVersionInfoASCII, Plugin_CL_GetTargetList, Plugin_GetPanelList, Plugin_CL_GetExtMapList, Plugin_GetFileTypeMappingList, Plugin_MatchesName, Plugin_CL_MatchesFileType, Plugin_Pr_MatchesPlugin, Plugin_Pr_MatchesPanels, Plugin_Free, Plugin_AddFileTypeMappings); Plugin_GetVersionInfoASCII, which GetToolVersionInfo calls before its definition, has a prototype at the file top. Every remaining declaration has an outside user.
- Headers relied on:
  - CWPlugins.h (callback tables, DropInFlags, flags; I added DropInFlagsV1 and the SDK's `1L << n` flag spellings). Open problems:
    - (Corrected by the adversary) `isExecutableTool` is 2.6's real name, not a stand-in for Pro7's `dropInExecutableTool`: 2.6's assert string in CLToolExec_ExecuteLinker is "dropinflags & isExecutableTool" (0x5a1ba4, the only reference 0x418e70), and Ninji's Pro7 CLToolExec.c carries a TODO to rename the flag to it. Now commented in CWPlugins.h.
    - `struct CWTargetList` lacks a typedef.
    - CWParserCallbacks entries are `void*`.
    - `CWDROPINDRIVERTYPE`/`CWDROPINVCSTYPE` are #defines rather than SDK enumerators (needed).
    - Pro7's `Handle` is `struct StorageHandle*`.
  - win32.h: I added VS_FIXEDFILEINFO.
  - CWParserPluginsPrivate.h: ToolVersionInfo.
  - CLErrors.h, CLIO.h, CLMain.h (clState), CLPrefs.h, MacSpecs.h, MemUtils.h, oslib.h (OS_ASSERT).
  - The `alloca` prototype stays local because no project or MSL header declares it.
- Open: the header problems above. The `#pragma pack(2)`/static-order TODO is a workaround whose evidence is the .bss.
- Header round H2 (October 16): CWParserCallbacks' entries are typed (Pro7's plugin.h), Pro7's `Handle` replaces StorageHandle, CLPlugins.h is DONE (Pro7's types), `isExecutableTool` and the CWDROPIN* #defines are commented. Still open: CWPlugins.h's `struct CWTargetList` has no typedef (the SDK's CompilerMapping.h has `typedef struct CWTargetList {...} CWTargetList;`). The pack(2)/static-order TODO is the unit's own.
- Follow-up (October 16, after round H2): CWPlugins.h's CWTargetList is the SDK's typedef (and the driver writes Pro7's bare `CWTargetList`): no longer pending.

### src/driver/CLPluginRequests.cpp: SOLID
- Boundary: 11 rows, 0x4177f0..0x418310. These are Pro7's CallPlugin, SetupPluginContext, five Send*Request functions and SendInitOrTermRequest. The two implicit `operator=` copies (CWTargetInfo, FSSpec) are emitted because of `-inline off`. Assert line 298 is Pro7's. Pro7's dead empty DestroyPluginContext has no data and is left out.
  - AddFileTypeMappingList (16 bytes, the last row) is in Pro7's CLFileTypes.c. It may be a separate CLFileTypes.c object linked right after this one, with no data and the same code either way. Wii has neither the function nor the module. Not split, because there is no evidence either way.
- Flags: functions ascend in address, so not deferred. Each flag was tested by removing it:
  - `-inline auto` gives 7/11 (the operator= copies get inlined).
  - `-sym on` gives 6/11.
  - Without `#pragma bool off`, 5/11 are exact. The pragma is now commented: the command line's C++ is built with bool off, the plugin library's with bool on.
- Functions: 11/11 exact. The source is now Pro7's text. Departures:
  - Two chained assignments in SetupPluginContext: needed shape (Pro7's separate stores give 71%).
  - `(mappingflags & kPrecompile) != 0` for `!!`: needed shape (97.5% otherwise).
  - CallPlugin is `static CW_CALLBACK` inside `extern "C"`. Pro7's is `extern "C"` and static; 2.6's `ret 4` shows `__stdcall`.
  - SetupPluginContext is also inside `extern "C"`, which only keeps its row's C name.
  - The old `UInt8 msg`, `Boolean linkmodel` and `SInt8 request` locals were stand-ins: Pro7's ternaries match.
- Behaviour: genuine bugs in the original (Pro7's too): SendDisassemblerRequest reports `Plugin_GetDropInName(linker)` even when `linker` is NULL (the compiler's disassembly, verbose); SendInitOrTermRequest stores through `plugin->context` after setting it NULL for an unknown plugin type. The parser requests 1 and 2 have no names anywhere (ParserFace switches on the numbers too). Every other constant is now named (CLStr*, req*, exelinkage*, linkOutputFile, kPrecompile, isPostLinker/isPreLinker, `'CWIE'`, `'APPL'`, `'MWDB'`).
  - Bug of ours fixed: the SDK linker flags had been written `0x40000000`, making the enum unsigned. The SDK's `1L << 30` is what 2.6 compiled, and it was needed for SendLinkerRequest.
  - `'????'` is written `'?\?\?\?'`: clang read it as a trigraph.
- Names: Pro7's names, including parameters and locals.
- Types: Pro7's `shellContextType` replaces the 8-byte stand-in `PluginOutputItem` (deleted, also from cmdline.h). `new shellContextType` replaces `operator new(6)`.
- Data: the relink is byte-identical.
- Header: now only the five Send* functions, SendInitOrTermRequest and AddFileTypeMappingList, all with Pro7's parameter names. CallPlugin, SetupPluginContext and PluginOutputItem were removed (no outside users). Unused includes trimmed.
- Headers relied on:
  - mwcc/Plugins.h: `PluginA/B/C` are stand-ins with pad fields for Pro7's CWPluginPrivateContext(ty), CWParserContext and CWCompilerLinkerContext. Their constructors live in CLDropinCallbacks_V10, CLParserCallbacks_v1 and CLCompilerLinkerDropin_V10; TODO left at the `new`.
  - CLDropinCallbacks_V10.h: UCBGetFileText takes `void**`/`unsigned int*`, so the cast is kept with a TODO; Pro7 takes `const char**`/`SInt32*`. shellContextType is there too.
  - DropInCompilerLinker.h: I added the SDK's req*, exelinkage* and linkOutput* enums.
  - CLTarg.h (Target, LinkModel*), CLFileOps.h (CmdLineStageMask_*), CLErrors.h, CLMain.h, CLPlugins.h, Generic.h, MacSpecs.h, CLLicenses.h, CLIO.h (`__assertion_failed`, which belongs to MSL's assert.h).
- Open: the stand-in context classes, the UCBGetFileText prototype, and the AddFileTypeMappingList unit question.
- Header round H2 (October 16): the stand-in context classes are Pro7's CWPluginPrivateContext/CWParserContext/CWCompilerLinkerContext; UCBGetFileText takes `const char**, SInt32*` (CLDropinCallbacks_V10.h); AddFileTypeMappingList stays in CLPluginRequests (2.6 has it there; CLPluginRequests.h DONE). Resolved.

### src/driver/CWPluginsPrivate.cpp: SOLID
- Boundary: 29 rows in Pro7's file order. Wii's module has the same functions plus the wrappers 2.6 dropped as unreferenced (no data). ValidateContext, ValidateInitTermContext, IsVCSContext and EnsureCachedAccessPaths are static: Wii has them as procedures with no public, so they are now `static` (inside `extern "C"`).
- Flags: not deferred. `-inline off` is needed: `-inline auto` takes CWGetProjectFile to 57.9%, because the FSSpec `operator=` call gets inlined. `-sym` makes no difference here (on matches too); off is kept as the plugin library's other units have it.
- Functions: 29/29 exact. The source is now Pro7's text calling through `context->callbacks->cbX` (a typed CW_BasePluginCallbacks), replacing `callbacks[N]` with 20 cast typedefs. Departures:
  - ValidateContext's `!(a || b || c)`: needed shape (Pro7's `!= && !=` gives 38.1%).
  - CWReleaseFileText's early return: needed shape (Pro7's if/else gives 78.6%).
  - (Adversary) IsVCSContext's `(SInt32)` cast on the already-SInt32 `pluginType` changed nothing: removed (Pro7's spelling, exact, no other function moved).
- Behaviour: return codes named (cwErrInvalidParameter/InvalidCallback/RequestFailed). Request codes named (reqInitialize/reqTerminate/reqIdle). The callback slots' indices are proven by the exact match: 34, 35, 40 and 41 are cbGetTargetName, cbCacheAccessPathList, cbCheckoutLicense and cbCheckinLicense.
- Names: Pro7's. Parameter names `argument2`... replaced throughout the header.
- Types:
  - Return types are now `CW_CALLBACK` (CWResult `__stdcall`), and SInt32* is used for the `long*` counts.
  - CWGetNamedPreferences takes `CWMemHandle*`.
  - CWReportMessage, CWAlert and CWShowStatus take `CWPluginContext` and `const char*`.
  - Still stand-ins, each with a TODO in the header, because callers in other units pass their own types: ExportedRecord (CWProjectFileInfo), the CWFindAndLoadFile `void*` (CWFileInfo), CWGetFileText's ints (CPrep passes ints), MessageContext (CWMessageRef), CWAlert's last two parameters, FileProcessingInfo (CWFileSpec), the `unsigned int` CWMemHandles (CPrep), and the `int*` cookie (CCompiler).
- Data: none. Relink: links (its FSSpec `operator=` COMDAT is noise).
- Header: CWPluginsPrivate.h now has Pro7's CW_BasePluginCallbacks (43 slots; the ones 2.6 never calls are `void*`). Removed the ValidateContext/ValidateInitTermContext/IsVCSContext/EnsureCachedAccessPaths prototypes (no outside users) and the unneeded `#include "compiler/CPrep.h"` (whole tree: still 4020 exact). CWGetOutputFileDirectory stays declared for Projects.c's dead code.
- Headers relied on:
  - CWPlugins.h: I added CWResult, CW_CALLBACK, CW_CALLBACK_PTR, reqInitialize/reqTerminate/reqIdle, cwErrInvalidParameter/InvalidCallback and CWDROPINVCSTYPE.
  - DropInCompilerLinker.h: CWCompilerLinkerCallbacks has `void*` and `unsigned int` stand-in entries.
  - CLDropinCallbacks_V10.h: ExportedRecord and IDEAccessPathList.
  - CError.h: MessageContext.
  - Files.h: CWFileSpec.
- Open: SDK parameter types need the callers changed (CPrep, CPrec, CError, CCompiler, Projects, ToolHelpers, ParserHelpers, ParserErrors, ELF_Lib_Import, IroUtil).
- Header round H2 (October 16): CWPluginsPrivate.h now has the SDK's types throughout (CWProjectFileInfo, CWFileInfo*, `SInt32*` lengths, CWMessageRef, `const char*` alert lines, CWFileSpec, CWMemHandle, `SInt32*` cookie); DropInCompilerLinker.h's callbacks are Pro7's plugin.h. Resolved.

### src/driver/CLLicenses.c: SOLID WITH TODOS
- Boundary: 10 rows at 0x418310..0x418910. Pro7's file is a Mac stub. Wii's CLLicenses module has the same five unnamed statics, then License_Initialize/Terminate/Checkout/Checkin/AutoCheckin (and a later License_Checkout_fullsearch). Wii's .bss is 0x10c bytes, the same as 2.6's: vendor code pointer, 32 slots, count, counter. Relink: links.
- Flags: not deferred. `-sym on` loses 2 (get_license_slot_values, License_Checkout).
- Functions: 10/10 exact, rewritten from the gc1/Ghidra text. Natural spellings were tried and all match, so these were removed:
  - fakematch function-pointer locals and casts in License_Terminate/License_AutoCheckin (plain calls match);
  - the needless block scope in License_Checkout;
  - the `do/while` and `x = x + 1` forms in allocate_license_slot;
  - a dead `return;`.
  The five helpers are now `static` (Wii shows them static).
- Behaviour, genuine bugs in the original (kept; the adversary re-read License_Checkin's `test ecx, ecx; jle` and AutoCheckin's unchanged negative argument in IDA, and License_Checkout statement by statement):
  - License_Checkout passes `sizeof(OSPathSpec.s)` (260) to OS_PathSpecToString for its 256-byte path buffer (the original's frame has 0x100 bytes there).
  - License_Checkin ignores every cookie that is not positive (`jle`). License_AutoCheckin only ever passes it negative cookies, so it checks nothing in, and allocate_license_slot's "auto checkin when full" frees nothing.
  - License_Terminate clears the vendor code (`memset`) without freeing it.
- Names: invented, each with a TODO; Wii names none of these:
  - the five statics allocate_license_slot, find_license, get_license_slot_values, delete_license, get_license_slot_count;
  - `vendorcode`, `LicenseSlot`, `LPCODE_HANDLE`.
- Types:
  - FLEXlm's VENDORCODE (lmclient.h's VENDORCODE5 layout: type, data[2], keys[4], flexlm_version/revision/patch, behavior_ver; 40 bytes, values FLEXlm 6.1) replaces `struct License {k4, k8, w1c, b20, blob}`.
  - `LP_HANDLE*` handles replace `int`, and slots are `{LP_HANDLE* handle; SInt32 cookie}` instead of `UInt32[32][2]`.
  - License_Checkout takes `const char* featureName, const char* licenseVersion` (Pro7's), where it had taken `int request, int options`.
  - Its third parameter is `int`, not Pro7's `Boolean`: 2.6 pushes the dword unchanged, and Boolean fails the match.
- Data: relink byte-identical. Statics are static. `licenseFile` is a COMMON (Wii: module ""), so its binding stays.
- Header: CLLicenses.h now declares only License_* and licenseFile. The FLEXlm types and the five helper prototypes left it.
- Headers relied on: LicenseImports.h (the FLEXlm API, below), CLErrors.h (CLInternalError), CLMain.h (clState), MemUtils.h, MsDos/MacSpecs via CLMain (OS_*).
- Open: invented names and the unverified FLEXlm code-handle name. The code was not changed for any of these.

### src/driver/LicenseImports.c: SOLID WITH TODOS
- Boundary: three 6-byte `jmp [__imp__lp_*]` thunks at 0x4284bc..0x4284ce, from LMGR326B.dll's import library (Wii links LMGR8C). This is not C in the original; the naked functions reproduce the thunks.
- Flags: `-O4,s` is needed. The relink with `-O4,p` fails, because the thunks get padded apart. The comparison alone cannot tell them apart.
- Functions: 3/3 exact.
- Behaviour: none.
- Names: lp_checkout, lp_checkin and lp_errstring are FLEXlm's. The unit's file name is the repo's (TODO in the file comment).
- Types: FLEXlm's prototypes, `lp_checkout(LPCODE_HANDLE*, int policy, feature, version, nlic, path, LP_HANDLE**)`, replace the `int`s. Removed the binding `_lp_checkin`, which this object defines.
- Data: none.
- Header: LicenseImports.h is now the FLEXlm API (VENDORCODE, the code handle, opaque LP_HANDLE, lp_*, the `_imp__` slots). It no longer includes CLLicenses.h.
- Headers relied on: common.h only.
- Open: the invented unit name and LPCODE_HANDLE (FLEXlm lmpolicy.h's real type name unverified).

### src/driver/CWSecretPluginCallbacks.cpp: SOLID
- Boundary: 4 rows at 0x41c390..0x41c480. Wii's module has ValidateContext (static, procedure only) and the three CWSecret* publics. Pro7's dead CWSecretPeekHandle is in neither (no data).
- Flags: renamed from `.c` to `.cpp`; the adversary checked every version: sources.json, and functions.json (4 rows), splits.txt and state.json of GC_2_0, GC_2_0p1, GC_2_5, GC_2_6 and GC_2_7 all say `.cpp`, build.ninja has no `.c` left. The one stale `.c` was a note in config/GC_2_6/name_review.json (fixed); docs/units-2.6*.md, names-2.6.md and the audits are dated snapshots that still say `.c`. C++ is the evidence: ValidateContext's `xor al, al ... mov al, 1` is C++'s bool `&&`, and C's int `&&` gives 75%. Pro7's text, including its declaration after a statement, then matches unchanged. `-inline` and `-sym` are not distinguishing (both match).
- Functions: 4/4 exact, Pro7's text verbatim. The old source had a `valid` flag, `callbacks[26]` casts and `argument2` names.
- Behaviour: none. Matches Pro7, calling cbInternal[0] and [1].
- Names: Pro7's. `#define ValidateContext CWSecretPluginCallbacks_ValidateContext` exists only because row names are global (AGENTS' rule).
- Types: `CWMemHandle` and `CWResult`. Handle parameters keep the callers' types (`struct StorageHandle*`, `long*`, `void*`), with TODOs in the header.
- Data: none.
- Header: CWSecretAttachHandle and CWSecretGetNamedPreferences (the adversary removed CWSecretDetachHandle: no outside user, defined before its one use).
- Headers relied on: CWPluginsPrivate.h (above) and CWPlugins.h.
- Open: the unit's own header still has stand-in parameter types (TODO, above), hence PENDING HEADERS. They await the callers (CBrowse, CCompiler, IO, Projects, ELF_Lib_Import, ParserFace, CodeGenOptPPC).
- Header round H2 (October 16): Attach/DetachHandle take Pro7's `Handle`/`Handle*`. Still open: CWSecretGetNamedPreferences' `void* prefsdata` (TODO: the SDK's `Handle*`), which awaits its callers (CCompiler, ParserFace, CodeGenOptPPC, ELF_Lib_Import).
- Follow-up (October 16, after round H2): CWSecretGetNamedPreferences takes the SDK's `(CWPluginContext, const char*, Handle*)`; ELF_Lib_Import's handles are `Handle` locals read through casts, as CodeGenOptPPC's (both units still link): no longer pending.

### src/driver/ResourceStrings.c: SOLID
- Boundary: 2 rows (Res_AddResource, Res_GetResource). Wii's module has the same two. Pro7's Res_Initialize/Res_Cleanup are in neither and have no data. Relink: links (rlist, err[256] and the literals).
- Flags: not deferred. `-sym` is neutral (on matches too).
- Functions: 2/2 exact. Pro7's names, loops and constants. 2.6's Res_GetResource departs from Pro7 in two places (Ninji notes Pro7's does not match):
  - it decrements `index` once at the top, where Pro7 recomputes `(SInt16)(index - 1)`;
  - it loops `for (scan < 16)`, where Pro7 uses `do/while (scan++ < 16)` (Pro7's version reads rlist[16]).
  Both are needed 2.6 shapes, commented.
- Behaviour: `sizeof(err)` for 0x100. TRUE/FALSE.
- Names: Pro7's.
- Types: Pro7's.
- Data: byte-identical.
- Header: the two functions, both used elsewhere (ClientGlue, TextUtils).
- Headers relied on: common.h.
- Open: none.

- Adversarial review: attacked every unit's boundary (Wii modules re-listed: CLLicenses' 5 unnamed statics + License_*, CWSecretPluginCallbacks' 4, CLPluginRequests' without AddFileTypeMappingList, ResourceStrings' 2), the CLPlugins departures (Pro7's `!strcmp` re-tried: 62.5%; the signed `CWTargetList.cpus/oss` stand-in re-tested: the SDK's `CWDataType*` takes Plugin_CL_MatchesTarget to 74.8%, a signed `CWDataType` everywhere loses SupportedPlugin, Plugins_Add, Plugins_GetParserForPlugin and SendParserRequest, so both held), License_Checkout/Checkin/AutoCheckin, allocate_license_slot and GetToolVersionInfo against IDA (all hold; three more original bugs noted), header users of all seven headers, and the `.c` -> `.cpp` rename. What broke: 12 CLPlugins.h and 1 CWSecretPluginCallbacks.h declarations had no outside user (removed); a no-op `(SInt32)` cast in IsVCSContext and a `(char*)`/`0` on xmalloc in GetToolVersionInfo (removed, `NULL`); the REVIEW's `isExecutableTool` claim was wrong (it is 2.6's name); CWSecretPluginCallbacks was graded SOLID with stand-in parameter types in its own header (now PENDING HEADERS); a stale `.c` in name_review.json. Exact 4020 -> 4020 (none lost), every source still links, layout run, clang-format and clang_check clean.

### src/backend/InlineAsm.c: SOLID WITH TODOS
- Boundary: 23 rows 0x52f740..0x530b90 = Pro7's InlineAsm.c, reversed (deferred); Wii module "InlineAsm" holds the same
  functions; asserts "InlineAsm.c" (CError_FATAL(248)); .data from 0x5c0b00 (8-aligned, `allow_array_expressions` first),
  .bss 0x5e3190..0x5e3594 = ASMstmtnb + DumpStatement's 1024-byte buffer. Pro7's dead InlineAsm_DumpStatement kept (its
  literals are in .data). Relink: links.
- Flags: deferred (rows descend against Pro7's order); `-sym off` shown: `-sym on` loses 3 functions.
- Functions: 23/23 exact. Started from Pro7's text; each departure tested alone with Pro7's spelling (all lose a function):
  `if (!(label = InlineAsm_LookupLabel(name)))` in DefineLabel (needed: inlined into ScanOptionalLabel), an `Object*`
  local for OT_OBJECT in LookupSymbolOrTag (needed), `!PrimaryExpression()` (needed), `if (GetPrec) value = Tail(value);
  return value;` (needed), ScanOptionalLabel's early returns (needed), `int size` in PackAsmStatement (needed: SInt32 is
  18 bytes longer), `case IAOpnd_Imm` in Pack/Unpack (2.6's jump tables start at 1; Pro7 `IAOpnd_0`), `char flag` in
  UnpackAsmStatement (needed; passed to CInline_GetLocalObj's char). 2.6 changes: `InlineAsm_setjmp_valid` around every
  setjmp (Wii's name) and AssemblerError's test of it, isclassmember's base-class search (new static
  `isbaseclassmember`, Wii also has an unnamed static there). Pro7's order of the two GetReferencedLabel locals now used
  (matches). Fakematch kept: InlineAsm_GetObjectOffset's `void* p` parameter copied into `InlineAsm* ia` (only the prologue
  load order differs with Pro7's parameter; six natural variants tried, listed in the TODO).
- Behaviour: straight from Pro7 plus 2.6 additions confirmed in the bytes; error ids by name; CError_FATAL line 248.
  No bugs found.
- Names: Pro7's/Wii's. Invented: `isbaseclassmember` (TODO; Wii's copy is an unnamed static). The file-scope DumpStatement
  buffer now has Pro7's name `buffer` (was the invented `dumpbuffer`).
- Types: Pro7's IALookupResult, InlineAsm, IAOperand.
- Data: byte-identical (relink). Statics `static`; `buffer` must stay at file scope (Pro7's function-local static moves
  it above ASMstmtnb: relink fails, tried). Bindings `_ASMstmtnb` deleted (only exact own functions use it);
  `_allow_array_expressions` kept (InlineAsmPPC's exact functions reference it across objects; deleting it loses 2).
- Header: InlineAsm.h is Pro7's API (entries no other unit uses, checked by grep: backtracking, backtrack,
  InlineAsm_LookupLabel, InlineAsm_ConstantExpression, InlineAsm_ScanStatements, InlineAsm_DumpStatement; all
  non-static in Pro7's header, kept as Pro7's API, a deliberate exception to "declare only what others use"). Now also holds Pro7's `IAMnemonic` and
  `IARegister`. `InlineAsm_gccmode` moved out (Pro7: InlineAsmPPC.c/.h). Private externs removed: `first_ST_ASM` and
  `InlineAsm_gcc_parse` come from the new GCCInlineAsm.h, `InlineAsm_GetMnemonic` from InlineAsmPPC.h (Pro7's places).
- Headers relied on: InlineAsm.h (fine); InlineAsmPPC.h (IAOperand/IAExpr types; problems: placeholder members
  `u.obj.unk`, `u.unk6.unk4`, `u.unk7.unk1/unk2/unk3`, `IAExpr.xC/x10/x18`, `IAEntryPoint.x0/x2/x4`, `IAFlagsB_*`,
  `CPUFLAG_*` hex names; the 1.x `EncodedOperand`/`ParsedAsmInstruction` stand-ins still used by TOC.c and CodeGen.c;
  `InlineAsm_labelref` declared but defined nowhere (Pro7 defines it in InlineAsmPPC.c; a COMMON)); CInline.h
  (`CInline_GetLocalObj(SInt32, char)`, Pro7 Boolean; 2.6 needs char here, so probably right); CScope.h, CInit.h,
  CMachine.h, CPrep*.h, CFunc.h, CError.h, types.h (not re-reviewed); common.h (junk forward declarations such as
  RegistrationEntry, RegistrationHashEntry, SecondaryRegistrationEntry: IDA names, not types).
- Open: the GetObjectOffset fakematch, the invented `isbaseclassmember`, InlineAsmPPC.h's placeholder members.

### src/backend/InlineAsmRegisters.c: SOLID
- Boundary: 3 rows 0x53c3f0..0x53c500 = Pro7's whole file reversed; Wii module "InlineAsmRegisters" (Wii adds
  LookupRegisterWithObject); .bss 0x5e35c8..0x5e36c8 = `hashedregisters[64]`; no .data. Relink: links.
- Flags: changed to `-inline auto,deferred` (rows descend against Pro7's order; names-2.6.md had it "to change"). `-sym`
  makes no difference to the code (both compile the same).
- Functions: 3/3 exact. Rewritten as Pro7's text verbatim (the old source was 1.x: `RegisterBinding` with an
  `unsigned int key`, `void*`/`unsigned int*` lookup, `#pragma auto_inline off`/`opt_propagation off`, reversed order, ~50
  unused includes). One departure: LookupRegister compares `reg->name` (Pro7 `scan->reg.name`): needed (2.6 reads
  through the local; same in InlineAsmRegistersPPC).
- Behaviour: Pro7's. No bugs.
- Names: Pro7's (`HashedRegister`, `hashedregisters`, `IARegister`). None invented.
- Types: Pro7's IARegister (`name`, `rclass`, `num`, `object`), `#pragma pack(2)` for Pro7's mac68k alignment (14 bytes,
  as the 22-byte lalloc in the original shows). InsertRegister's `char* name, RegClass rclass, short num`, LookupRegister
  `char*` -> `IARegister*`: Pro7's.
- Data: .bss placed by the object; binding `_register_binding_hash` deleted.
- Header: Pro7's three prototypes; users: InlineAsmPPC.c, InlineAsmRegistersPPC.c, FuncLevelAsmPPC.c. (Unmangle.c
  includes it for nothing: its owner should drop it.)
- Headers relied on: InlineAsm.h (IARegister; fine), CompilerTools.h (CHash, lalloc), common.h (RegClass; the junk
  forward declarations above).
- Open: none in the unit; common.h's review.
- Header round H2 (October 16): common.h lost its dead forward declarations and typedefs (Front-end core, Front end); InlineAsm.h's IARegister is fine. Resolved.

### src/backend/InlineAsmRegistersPPC.c: SOLID WITH TODOS
- Boundary: 4 rows 0x53c500..0x53cc10 = Pro7's file reversed (InsertSPR/InsertRegisterDCR inlined); Wii module
  "InlineAsmRegistersPPC" (Wii adds LookupPMRRegister and the _GCC lookups); .data 0x5cbec8..0x5cee70 (registers, SPRs,
  `spr_cpus`, 2.6's DCR table, pooled strings), .bss 0x5e36c8..0x5e3900. Relink: links.
- Flags: deferred (descending); `-sym off` shown (on loses one function).
- Functions: 4/4 exact. Departures tested against Pro7's spelling: `strcmp(check->name, buf)` (needed),
  `Boolean overflow; UInt32 result;` order (needed), `char buf[20]` (Pro7 16; needed: frame), the
  `IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(t) || IS_TYPE_VEC2X32FLOAT(t)` test (2.6: 34 bytes more than IS_TYPE_FLOAT, and
  `IS_TYPE_FLOAT || IS_TYPE_VEC2X32FLOAT` is 2 short), the DCR table and its loop (2.6). Removed: the `UInt32 mask` local
  (Pro7's expression matches), `0xFFFFF` literals (now CPUMask_Generic/CPUFLAG_LOW_MASK), the `(SInt32*)&result` cast.
- Behaviour: genuine bug in the original, kept and commented (re-proved by the adversary, not a misreading of our
  types): InitializeRegistersPPC stores each DCR entry's first column at RegInfo+4 (the CPU mask both lookups test
  against `cpu`) and its third at RegInfo+0xe (`reg.num`, which InlineAsm_ScanAssemblyOperands' 'D' case reads at
  IARegister+6 as the encoded DCR: 0x4afd5b). The columns are unambiguous: the first is the PPC403 DCR map (EXISR 0x40,
  EXIER 0x42, BRH0-7 0x70-77, BR0-7 0x80-87, BEAR 0x90, IOCR 0xA0, DMACR0 0xC0, DMASR 0xE0), the third CPU masks
  (CPUMask_401 0x40 / CPUMask_403 0x80: `br0` 0x80 vs its 401 twin `brcr0` 0x40 at the same number; 0x7C0 exceeds the
  10-bit DCR space; 0x300 is shared by five DMA registers). Run time: the original 2.6 with `-proc 403` assembles
  `mfdcr r3,exisr` as 0x7c602286 (DCR 0x80, objdump "mfbr0") and warns "'exisr' register not applicable to this
  processor"; `mfdcr r4,dmasr` also becomes DCR 0x80. SPRs go through the same struct correctly. The helper with
  InsertSPR's parameter order called in that order is 99.5% (argument load order), so 2.6's helper takes the mask
  before the number and the call passes them in table order. Ours: ScanDec was declared `SInt32*` (Pro7 `UInt32*`),
  fixed in CompilerTools.h/.c.
- Names: Pro7's, except RegInfo's placeholder `b` (Pro7/Ninji) now `cpus` (the CPU mask; commented). Invented (TODO): `IARegInfo` (Pro7's `RegInfo`, which RegisterInfo.h's 2.6 RegInfo already names),
  `AsmDcr` (2.6's table type), `unusedregisters` (28 bytes of .bss below the hash tables that no code references; Wii's
  module has more such `thespr` statics, so probably two dead functions' statics).
- Types: Pro7's; IARegister now the real type (was `InlineAsmRegisterEntry` with `kind`/`number`).
- Data: byte-identical; statics static; bindings `_hasheddcrs`, `_hashedsprs`, `_spr_cpus` deleted (own exact functions
  only). `spr_cpus` stays global (Pro7, Wii public).
- Header: Pro7's four prototypes (all used by InlineAsmPPC.c); the unused `extern spr_cpus` removed; the stand-in struct
  `InlineAsmRegisterEntry` removed.
- Headers relied on: InlineAsm.h (IARegister, IALookupResult), InlineAsmPPC.h (`cpu`, CPUMask/CPUFLAG names: placeholder
  hex names), InlineAsmRegisters.h, RegisterInfo.h (setup_diagnostic_reg_strings, register_class_format; its 2.6
  `RegInfo` forces the invented name here), PPCError.h, CompilerTools.h (ScanDec fixed), types.h (IT_VEC2X32FLOAT and
  the VEC2X32 macros are invented names, TODO there), CFunc.h (arguments/locals).
- Open: the three invented names (no source has them).

### src/backend/InlineAsmMnemonicsPPC.c: SOLID WITH TODOS
- Boundary: 2 rows 0x53c2e0..0x53c3f0 = Pro7's file reversed; Wii module agrees; .data 0x5c40d0..0x5cbec1 (the table and
  its pooled strings, generated from the original's bytes); `hashedmnemonics` is a COMMON (Pro7 non-static). Links.
- Flags: deferred (descending); `-sym off` shown (on loses one function).
- Functions: 2/2 exact; Pro7's text verbatim (braces only).
- Behaviour: Pro7's; 2.6's table (Gekko ps_*, psq_*, ...) from the bytes.
- Names: Pro7's.
- Types: `IAMnemonic` is now Pro7's type in InlineAsm.h (was the stand-in `AsmOperandPattern` with pack(1) and fields
  `operands`/`processorMask`, renamed to Pro7's `format`/`cpu`). Pro7's placeholder members `x4`/`x10` are named
  `opcode`/`instruction` with TODO comments (their uses: the opcodeinfo index, the instruction word).
- Data: byte-identical; binding `_mnemonics` deleted (static; own exact function only); `_hashedmnemonics` kept (COMMON).
- Header: Pro7's two prototypes (InlineAsmPPC.c uses both).
- Headers relied on: InlineAsm.h (IAMnemonic: the two invented member names), CompilerTools.h, PCode.h (the PC_ opcode
  enumerators).
- Open: IAMnemonic's two member names (header-level TODO).
- Header round H2 (October 16): IAMnemonic's two member names stay TODO-invented (Pro7's x4/x10; Back end family). Resolved as TODO names.

### src/backend/GCCInlineAsm.c: SOLID WITH TODOS
- Boundary: 3 rows 0x57eb50..0x57f04c (InlineAsm_gcc_parse, gcc_replace_arg_st_asm, gcc_parse_name) = Pro7's file
  reversed with the other statics inlined; .data 0x5d7f88..0x5d7fb4 = "GCCInlineAsm.c" (CError_FATAL(365)) + the switch's
  jump table; `first_ST_ASM`, `gcc_name_list`, `gcc_name_list_index` are COMMONs. Pro7's dead InlineAsm_SkipComment
  kept (no data evidence either way; Pro7 has it, nothing calls it). Links.
- Flags: changed to `-inline auto,deferred` (rows descend against Pro7's order; the old source had them reordered with
  prototypes). `-sym` makes no difference.
- Functions: 3/3 exact. Rewritten from Pro7's text (the old one used a stand-in `GCCAsmOperand`/`GCCNameListEntry`, 1.x
  `ParsedAsmInstruction`, numeric statement types and error ids, non-static functions). Departures: gcc_parse_name
  declares `expr` before `nameentry` (needed: registers swap with Pro7's order); `static inline` on gcc_parse_killed
  (needed: auto-inline's statement limit keeps Pro7's `static` copy out of line, and 2.6 has no copy; original's shape).
- Behaviour: Pro7's (CInt64_GetULong of the constant, ST_EXPRESSION/ST_ASM, ERR_ names for 105/114/115/144).
- Names: Pro7's; none invented.
- Types: Pro7's IALookupResult for `gcc_name_list`, InlineAsm/IAOperand for the operands.
- Data: byte-identical; COMMON bindings kept (COMMONs).
- Header: new `include/compiler/GCCInlineAsm.h`, Pro7's (first_ST_ASM, InlineAsm_SkipComment, InlineAsm_gcc_parse; the
  first and last used by InlineAsm.c, which had private externs).
- Headers relied on: InlineAsmPPC.h (IAOperand: the placeholder members `u.unk6.unk4`, `u.unk7` used here), InlineAsm.h,
  CExpr.h/CExpr2.h, CFunc.h, CPrep*.h, CParser.h, Registers.h (VarInfo.used), enode.h, CInt64.h, CError.h.
- Open: InlineAsmPPC.h's placeholder members.
- Header round H2 (October 16): IAOperand's `unk6`/`unk7` placeholders are `gccreg`/`gccimm` (TODO-invented) and live in InlineAsm.h; CExpr2.h is merged; Registers.h is DONE. Resolved as TODO names.

### src/backend/FuncLevelAsmPPC.c: SOLID WITH TODOS
- Boundary: 6 rows 0x525eb0..0x526980 = Pro7's whole file reversed (FuncAsm_AddEntryPoint inlined). Corrected by the
  adversary: Pro7's SetupAssembler and CleanupAssembler were CSOM.c's rows `CSOM_NoOp` (0x525ec0, 2.6's body clears
  `InlineAsm_setjmp_valid`) and `fn_004e67a0` (0x525eb0, a bare `ret`): Pro7's last two functions, so the lowest in a
  deferred object, right below Assembler; CPrep calls them exactly where Pro7's CPrep calls SetupAssembler/
  CleanupAssembler (after SetupPrecompiler, after CleanupPrecompiler); Wii's FuncLevelAsmPPC module has both publics.
  Moved (`unit.py move`), renamed in every version, review decisions recorded; CSOM's .text now ends at 0x525eb0.
  .bss = the two entry-point statics. Links (and CSOM still links).
- Flags: deferred (descending). `-sym` makes no difference.
- Functions: 6/6 exact (SetupAssembler: Pro7's empty body plus 2.6's `InlineAsm_setjmp_valid = FALSE`). Now uses Pro7's `InlineAsm`/`IAEntryPoint` (was 1.x `ParsedAsmInstruction` with
  `specialFlags`, `data.directive.object` and `(int)ia->opcode == 1` casts: the casts are gone, Pro7's IADirective_*
  and AssemblerType_1 names match), FuncAsm_PreScanDirectives `static` again (Pro7; Wii has no public). Departures
  tested: `(int)reg < 10` (needed), `Statement* scan` for the loop (needed; Pro7 reuses `stmt`),
  `(epilogue = pclastblock)->flags |= fIsEpilogue` twice and `generate_epilogue(epilogue = pclastblock, FALSE)` (needed),
  `!(asm_alloc_flags[6] || asm_alloc_flags[7])` (needed). 2.6 changes confirmed in the bytes: soft-float arguments in
  GPRs, `IS_TYPE_FLOAT_NOT_VEC2X32FLOAT` (IS_TYPE_FLOAT is 6 bytes short), `switchtables = NULL`,
  `update_section`, set_vr_reg_flags, rfi for interrupt functions, InlineAsm_setjmp_valid, saving/restoring
  `copts.peephole` (Pro7 only clears it: 12 bytes short), no pic_base_label handling.
- Behaviour: as above; CError_FATAL(530).
- Names: Pro7's/Wii's; none invented (`IAEntryPoint.x8` renamed `object` for its use).
- Types: Pro7's.
- Data: byte-identical; bindings `_entrypoints_head`/`_tail` deleted (own exact function only).
- Header: five prototypes (setup_assembly_argument and assign_local_addresses used by InlineAsmPPC.c, Assembler by
  CFunc.c and CLIO.c, SetupAssembler/CleanupAssembler by CPrep.c, moved from CSOM.h); FuncAsm_PreScanDirectives removed
  (static).
- Headers relied on: InlineAsmPPC.h (`asm_alloc_flags[10]` indexed by bare numbers as in Pro7, IAEntryPoint's
  `x0`/`x2`/`x4` placeholders, sm_section, fralloc globals), InlineAsm.h, InlineAsmRegisters.h, CodeGen.h, StackFrame.h,
  Coloring.h, PCode*.h, TOC.h, Registers.h, PPCError.h, ObjGen_PPC_EABI.h (not re-reviewed).
- Open: InlineAsmPPC.h's placeholder members.
- Adversarial review: attacked the DCR "bug", the boundaries, header users, the IAMnemonic member names and the
  helpers' shapes. The DCR bug held, now proved three ways (store offsets vs. the lookup's and the consumer's reads,
  the table's columns against the 403 DCR map and CPU masks, and the original compiler run on `mfdcr r3,exisr`); it is
  not a naming error in our types (SPRs use the same struct correctly, and the swapped-signature spelling does not
  match). Broke: FuncLevelAsmPPC's boundary (Setup/CleanupAssembler were misfiled in CSOM.c under invented names;
  moved and renamed). Tidied: RegInfo's placeholder field `b` -> `cpus`. Held: InlineAsm.c's departures (Pro7's
  `volatile short endToken` is Pro7's own; `case IAOpnd_Imm` is behaviour-neutral and 2.6's table starts at 1), the
  other units' boundaries (neighbouring rows are CClass/CTemplateNew/CDecl/CExpr/CRTTI), IAMnemonic's `opcode`/
  `instruction` (uses and table bytes agree: `add` = PC_ADD, 0x7C000214). Not resolved: `unusedregisters` (no literal
  of a dead function is in the pooled .data, so its owner stays unknown), the GetObjectOffset fakematch (left as
  documented). Exact flags unchanged (4020 before and after); all six sources and CSOM.c link.
- Header round H2 (October 16): InlineAsmPPC.h/InlineAsm.h are DONE WITH TODOS: IAEntryPoint's overlay names and the IAOpnd_ names are TODO-invented; `asm_alloc_flags` indexed by numbers as Pro7. Resolved as TODO names.

#### Shared evidence for Alias.c, StructMoves.c, SFPE_PPC_EABI.c, MachineSimulation7400.c, MachineSimulationAltiVec.c

# Review: Alias, StructMoves, SFPE_PPC_EABI, MachineSimulation7400, MachineSimulationAltiVec (GC_2_6)

What I checked for all five: `ninja build/GC_2_6/compare.json` (4020/4077 exact, no function lost or gained against
compare_start.json), `tools/layout.py GC_2_6`, and `tools/relink.py GC_2_6 --only` for each unit (all five still link).
clang_check reports 0 errors, and clang-format is clean on the five sources and their headers.

Headers shared by these units:
- **CPrep.h** (`COpts`): `use_fp` and `soft_float` are invented names (TODO in AGENTS.md).
- **PCode.h**: PCode/PCodeArg/flags. Pro7's types, no placeholders.
- **UseDefChains.h**: `UseDefInfo` member names are invented (TODO at line 58).
- **CopyPropagation.h**: `PropInfo` member names are invented (TODO at line 37). `Propagation` adds 2.6's `mask`.
- **RegisterInfo.h**: `RegInfo` (TODO: made-up type and member names), plus `OBJECT_REG` and `Registers_GetVarInfo`.
- **Registers.h**: `used_virtual_registers` and `n_real_registers`.
- **InstrSelection.h**: Ninji's `GEN_NODE` and `ENSURE_GPR` macros. One prototype has an `int unk` parameter at line 123.
- **PCodeInfo.h**: `OpcodeInfo.rank` is an invented name (Pro7 calls it `x9`).
- **CodeGen.h**: it holds COMMONs of other units (TODO at line 93).
- **Scheduler.h**: the `MachineInfo` member names are invented (Pro7: x0/x4/x8, TODO).
- **Operands.h**, **CParser.h**, **CError.h**, **CompilerTools.h**, **objects.h**, **types.h** and **enode.h**: nothing
  wrong found in the declarations these units use.

### src/optimizer/Alias.c: SOLID WITH TODOS
- Boundary: 21 rows from 0x511990 to 0x5133f0. Wii's `Alias` module holds the same functions in the same reverse order
  (its later additions aside).
  - The assert file name is "Alias.c".
  - .data 0x5be0a8–0x5be334 and .bss 0x5e2040–0x5e2fe4 are 8-aligned, and the relink is byte-identical.
  - Pro7 functions missing from 2.6: `make_alias_set_from_IR`, `uniquely_aliases` and `may_alias_worst_case` (2.6's
    0x511cc0 is the two-pcode `may_alias`). None of them left literals behind: .data matches.
- Flags: `-inline auto,deferred` is right because the rows descend against the file's order (`is_safe_const` is
  highest). `-sym on` matters: with `-sym off` only 16/21 match.
- Functions: 21/21 exact. Departures from Pro7:
  - 2.6 changes, all seen in the disassembly:
    - `make_alias` tests only `!size`, then clears the offset.
    - The global/virtual memory split (`worst_case_ptr`/`worst_case_func`, `is_global_memory`, `same_object` with
      `CMangler_GetLinkName`).
    - `add_alias_member`'s `parent == child` and set tests.
    - `is_address_load` adds an LI case and drops the DNONLAZYPTR case.
    - The 2.6 helpers (`address_def_alias`, `propagate_alias_to_use`, `operand_alias`, `gather_instruction_alias`,
      `add_aliased_locals_to_worst_case`, `make_distinct_virtual_alias`, `must_alias_object`).
    - `alias_prop` gains the `mask` field.
  - Shapes 2.6's code needs:
    - `make_alias` searches with a `goto found` and clears `alias` after the loop. Pro7's `lookup_alias`, inlined,
      copies the parameters and scores 57.6–91.4%. TODO in the source.
    - `hash_alias` takes `int` offsets; with Pro7's `SInt32` make_alias scores 83.9.
    - `(int) reg2 == 0`: the cast-to-int compare of a short that AGENTS.md lists. The plain spelling scores 98.9.
    - `gather_instruction_alias`'s `skip = (flags & fIsWrite) ? args : args`. 2.6 tests the flag for nothing (a `je`
      to the next instruction). An if/else with equal arms also matches, a plain assignment gives 98.2. TODO kept.
      This is the original's shape: something in the arms compiled to one value.
  - Natural spellings that now replace workarounds (all exact):
    - The `candidate_pcode = (PCode*) address_def_alias(...)` reuse (a fakematch) is now an `Alias* alias` local.
    - The written-out bit operations are now BitVectors.h's macros.
    - The `(SInt8)` cast on `def->v.arg` is gone.
    - The literal `4` is now `RegClass_GPR`.
- Behaviour: every constant and offset is from exact code. Bug in the original, kept:
  `make_distinct_virtual_alias` tests `IS_TYPE_CLASS` on the pointer type itself, which is never a class.
- Names:
  - Two inlined regions matched Wii functions by body, and the extracted helpers still match:
    - `merge_aliases` is now `create_alias_union`.
    - The displaced-alias block of `propagate_alias_to_use` is now `make_displ_alias` (its assert, 948, is the only
      one).
  - The invented static `alias_reset_count` is now `set_aliases` (`Alias*`). Wii's `initialize_aliases` clears its
    COMMON `set_aliases` at that same step, and Wii's `create_alias_set` chains set aliases through it. 2.6 only
    clears it.
  - Still invented, with TODOs: `address_def_alias`, `propagate_alias_to_use`, `operand_alias`,
    `gather_instruction_alias`, `same_object`, `is_virtual_or_local_static`, `virtual_object_type`. Wii has these
    statics unnamed.
  - (Adversary) `is_global_memory` and `worst_case_memory_type_global` were misleading: the test picks the objects
    make_alias puts in `worst_case_func` and *not* in `worst_case_ptr` (virtual objects and local statics: Wii's
    make_alias makes the same test inline, `is_virtual_object_alias` being its flag form). Renamed to
    `is_virtual_or_local_static` and `virtual_object_type`, comments corrected.
- Types: Pro7's `Alias`/`AliasMember` at 2-byte packing (an Alias is 0x2e bytes, matching the `push 0x2e`).
  `AliasType` is a byte enum. `AliasType0..2` and `vec24` are Ninji's placeholder names (no source names them):
  now TODO-commented in Alias.h with their meaning (whole object, part, set).
- Data: the relink is byte-identical and the statics are `static`. I deleted the five bindings of these statics
  (`_alias_hash`, `_aliases`, `_n_aliases`, `_n_gathered_aliases`, `_set_aliases`): only Alias.c's own exact functions
  reference them. symbols.txt now names them from the object (scope:local).
- Header: it declares the API other units use (each declaration checked: every one has a user outside Alias.c).
  - `add_alias_member` has no other 2.6 user, so its prototype moved from the header to the top of Alias.c (it is
    used before its definition; still external, as in Pro7 and Wii). Code unchanged everywhere.
  - The header's `#pragma pack` is balanced.
- Headers relied on:
  - Alias.h (OK).
  - PCode.h: `alias`, `flags` and `defID`.
  - UseDefChains.h: `Defs`, `reg_Defs`, `usedefinfo`, `canidate_reaches_use`. `UseDefInfo` member names are invented.
  - CopyPropagation.h: `Propagation`, `Candidates`, `Uses`, `propagateinstructions`, `recursive_propagation`,
    `propagated_instructions`. `PropInfo` member names are invented.
  - ValueNumbering.h: `killmemory`, `nextvaluenumber`.
  - CParser.h: `CParser_GetTypeQualifiers`, `IsReadOnly`, `GetUniqueName`.
  - CClass.h, CMangler.h, CMachine.h, BitVectors.h, Registers.h, CompilerTools.h, objects.h, types.h: OK.
- Open:
  - The invented names of the 2.6-only statics (nothing can name them).
  - The `goto` and `skip` TODOs.
  - The invented members in UseDefChains.h and CopyPropagation.h.

### src/optimizer/StructMoves.c: SOLID WITH TODOS
- Boundary: 12 rows from 0x50fbe0 to 0x511990, matching Pro7's StructMoves.c list in reverse. Wii's `StructMoves`
  module agrees (Wii adds `clear_block` and `uint_type_for_size`). Assert file name "StructMoves.c" at 0x5be038.
  - The .data range 0x5be024–0x5be0a8 starts 4-aligned (`align:4`). The bytes there are
    `store_word_of_small_struct`'s 4-entry jump table, which points into this unit's .text, then the file name. The
    relink is byte-identical, so the cut is right: this deferred object's first data section is a 4-aligned jump
    table.
- Flags:
  - Deferred, because the rows descend (`make_addressable` is highest).
  - `-sym on` and `-sym off` give the same code here (12/12 either way). `on` is the project's per-file default.
- Functions: 12/12 exact. Departures from Pro7:
  - 2.6 changes, all in the disassembly:
    - The `copts.use_fp && !copts.soft_float` guards on the FPR moves.
    - The step computation (`align <= 0` gives 1, a signed `align < 4` test, then 4/2/1).
    - `load_small_block_into_reg`'s Indexed case lost Pro7's `CError_FATAL`.
    - `load_word_of_small_struct` uses a separate result register when `dstReg == srcReg`.
    - New assert lines.
  - Needed shapes:
    - The move step is computed by two inline helpers, `GetStep` and `GetStepForSize`. 2.6 copies the arguments as
      an inline's parameters would; writing the computation out in place scored 80.8%.
    - The tail moves of `move_block_via_inline_loop` go through `copy_chunk`. Inside it, `int w = width;` is a
      workaround (fakematch class): the same helper without the copy scores 99.6 (`int`), 89.5 (`SInt32`) and 90.7
      (`short`). TODO in the source.
    - `load_immediate(reg1 = ...++, ...)` puts the assignment in the argument; separate statements score 99.3.
    - `move_block_via_load_store_sequence`'s `SInt32 i`: Pro7's `int` scores 81.4 (it drops the loop-entry jump).
      This now has a comment.
    - `load_small_block_into_reg(_pair)`'s `int reg` for the Indexed case: Pro7's `short tmpReg` scores 85.2/71.0.
  - Natural spellings that now replace workarounds (all exact):
    - `make_addressable` is Pro7's text verbatim with Pro7's `SInt32 offset` prototype. The old version had an
      `unsigned int` parameter, an `unsigned int reg`, `+= 1U` and an `(int)(...)` cast.
    - `load_displaced_address` is Pro7's, with `int reg`.
    - `load_word_of_small_struct` uses Pro7's `short tmpReg`, so the six `(short)tmpReg` casts are gone.
    - `FITS_IN_SHORT`, `HIGH_PART` and `LOW_PART` replace the written-out expressions. 2.6 tests
      `LOW_PART(absAddress)` where Pro7 tests `absAddress`.
- Behaviour: from exact code against Pro7. No bugs found.
- Names: Pro7's and Wii's for every row. Invented, with TODOs: `GetStep`, `GetStepForSize`, `copy_chunk`.
- Types: Pro7's `Operand` fields. `use_fp` and `soft_float` are COpts names invented in CPrep.h.
- Data: the relink is byte-identical. There are no statics and no bindings in range.
- Header: what other units use (FunctionCalls, CodeGen, Intrinsics, InstrSelection).
  - `make_addressable` now takes `SInt32 offset` (Pro7's type). Its one external caller, FunctionCalls, is unchanged:
    nothing was lost.
- Headers relied on: StructMoves.h (OK), Operands.h, PCodeUtilities.h (`load_store_register`, `add_immediate`,
  `load_address`, `branch_*`), Registers.h, CParser.h/CPrep.h (`COpts`), InstrSelection.h, CError.h. Open there:
  CPrep.h's invented `use_fp`/`soft_float`.
- Open:
  - The `int w = width` workaround and the invented helper names.
  - The helpers' shape is inferred from 2.6's parameter copies. Wii has inlined them too: its StructMoves statics
    hold no step function.

### src/backend/SFPE_PPC_EABI.c: SOLID WITH TODOS
- Boundary: 8 rows from 0x4ed2d0 to 0x4ef440, the same set and order as Wii's `SFPE_PPC_EABI` module (Wii adds
  `SFPE_multiply_add`). Assert file name "SFPE_PPC_EABI.c". .data 0x5bc2d8–0x5bc33c holds three used-register arrays,
  the file name and the comparison's jump table. The relink is byte-identical.
- Flags:
  - Deferred, because `SFPE_gen_OP` (lowest assert lines) is at the highest address.
  - `-sym off` loses one function, so `on` is shown to matter.
- Functions: 8/8 exact. There is no Pro7 file; the reference is gc1's SFPE_PPC_EABI.c (1.x), checked against the
  disassembly.
  - 2.6 changes:
    - `branch_subroutine` takes used-register arrays where gc1 passes an immediate 0x18.
    - `LoadFloatArg` no longer moves the value into r4.
    - Error 130 is raised for unsigned 64-bit division.
  - Needed shapes:
    - The `LoadFloatArgGPR` wrapper is gc1's. Calling `LoadFloatArg` directly loses `POSTINCDEC` and `ASS`. There
      is now a comment.
    - In `SFPE_gen_POSTINCDEC` a bare `Registers_GetVarInfo(...)` call is kept. Original's shape: it is the
      call whose `regHi` the double branch reads; `sourceReg2 = ...->regHi;` matches this function but breaks
      `SFPE_gen_ASS`, and dropping the call scores 98.4. There is now a comment.
    - `(SInt32) leftOperand.reg != 3` in the condition: without the casts it scores 92.9. gc1 has the same casts.
  - (Adversary) the block-local `{ SInt32 reg = value.reg; if (reg != 3) ... }` copies (six sites) were not in the
    entry: CodeGen.c's catalogued shape. In SFPE_gen_POSTINCDEC the plain `value.reg != 3` (gc1's) scores 99.6 and
    `(SInt32) value.reg != 3` matches there but breaks SFPE_gen_ASS (deferred coupling). Original's shape; now
    commented.
  - The bare `Registers_GetVarInfo(...)` call is gc1's too (positive evidence for original's shape), as is the
    `LoadFloatArgGPR` wrapper.
  - Original's shape: `SFPE_gen_TYPCON` tests `output->optype != OpndType_GPR` twice in a row (`test al; je; test
    al; je` in the binary). This is `ENSURE_GPR` inside an `if` that already tested the same thing, so the file now
    uses `GEN_NODE`/`GEN_NODE_TO_REG`/`ENSURE_GPR` throughout. The exception is `SFPE_gen_POSTINCDEC`'s `value`
    coercion: `ENSURE_GPR` there scores 98.8, so that one stays a plain `if`.
  - Cleaned up:
    - `OBJECT_REG(...)` replaces four hand expansions.
    - The switch's `case 0x13..0x18` are now `ELESS..ENOTEQU`, and `regOffset = 0x17` is now `EEQU`.
    - The `(short)` cast on a register number and the `(*cgdispatch[...])` spelling are gone.
    - The unused `IroLoop.h` include is gone.
    - The unit now has a file-top comment.
- Behaviour: constants, error ids and runtime routine objects checked against the exact code. No bugs found.
- Names: the rows are Wii's.
  - Invented, with TODOs: `used_regs_r3_r4`, `used_regs_r3_r6`, `used_regs_r3` (statics, unnamed in Wii).
  - Invented by gc1, with a TODO: `lift_result`, `LoadFloatArg`, `LoadFloatArgGPR`, `LoadDoubleArg` (2.6 inlines
    every call).
- Types: Pro7's `Operand`/`ENode` and `UInt32[RegClassMax]` for the masks (`branch_subroutine`'s parameter).
- Data: byte-identical. The statics are `static`, with no bindings in range.
- Header: rewritten to hold only the eight generators.
  - It had private copies of CodeGen.h's `low_reg`/`high_reg`/`low_reg2`/`high_reg2` and four `rt_sfpe_*`, which
    are removed. Every includer also includes CodeGen.h.
  - Parameter names now follow the definitions.
  - InstrSelection and CodeGen are unchanged (75/83 and 42/43, as before).
- Headers relied on:
  - CodeGen.h: `rt_sfpe_*`, `low_reg` etc. It carries other units' COMMONs (TODO).
  - InstrSelection.h: `cgdispatch`, the GEN_NODE/ENSURE macros, `indirect`, `store`, `store_pair`,
    `ispostincrementopportunity`. Ninji's macros are a reconstruction, and one prototype has an `int unk`.
  - Operands.h: `Coerce_to_register`, `coerce_to_register_pair`, `one_point_zero`.
  - RegisterInfo.h: `Registers_GetVarInfo`, `OBJECT_REG`. `RegInfo` names are made up.
  - PCodeUtilities.h: `branch_subroutine`, `insert_load_immediate_after`.
  - PPCError.h, CError.h, CParser.h, types.h (`TYPE_IS_SOFT_FLOAT`): OK.
- Open: the invented names; this is 2.6's own unit with gc1's helper split, so there is no stronger reference.

### src/backend/MachineSimulation7400.c: SOLID WITH TODOS
- Boundary:
  - 7 rows from 0x5787e0 to 0x5791d0, matching Pro7's file in reverse and Wii's module (5 functions; the rest are
    inlined).
  - .data 0x5d2c70–0x5d396b is `machine7400` (0x28 bytes) followed by 469 × 7-byte `instruction_timing` entries.
    2.6's entry drops Pro7's `char unused`, which the data size proves.
  - .bss 0x5e3d60–0x5e3e48. The relink is byte-identical.
- Flags:
  - Deferred, because `uses_vpermute_unit_7400` is lowest.
  - `-sym on` is required: with `-sym off` only 6/7 match.
- Functions: 7/7 exact. The code is Pro7's text, with these departures:
  - `MachineInfo` is defined before the table: 2.6's data order, as AGENTS.md notes.
  - `can_issue` inverts Pro7's `if (isClear1)` test; Pro7's order scores 97.9. There is now a comment, and the stale
    `// TODO: stack swap issue` is gone.
  - The include list is Pro7's (`Registers.h` dropped, `PCodeInfo.h` added; still exact).
  - `TRUE`/`FALSE` in `can_issue` and for the completion flags.
- Behaviour: the timing table is byte-identical. No bugs found.
- Names: Pro7's. The statics are `#define`d to `m7400_*` row names, by design (row names are global). `machine7400`
  is Wii's public.
- Types: Pro7's.
- Data: byte-identical.
  - The `_machine7400` binding stays: deleting it made Scheduler.c's `scheduleinstructions` and `uses_vpermute_unit`
    inexact ("not bound").
- Header: Scheduler.h (no own header).
- Headers relied on:
  - Scheduler.h: `MachineInfo`, `is_dependent`. `MachineInfo` member names are invented (Pro7: x0/x4/x8, TODO).
    `pragma pack(2)` is not needed for a 0x28-byte struct but is harmless.
  - PCode.h, PCodeInfo.h (`OPCODE_MAX`): OK.
- Open: Scheduler.h's invented `MachineInfo` member names.
- Header round H2 (October 16): Scheduler.h is DONE WITH TODOS: `MachineInfo`'s member names stay TODO-invented, and its pack(2) is gone (all ints and pointers). PCode.h/PCodeInfo.h are DONE WITH TODOS. Nothing else open.

### src/backend/MachineSimulationAltiVec.c: SOLID WITH TODOS
- Boundary:
  - 7 rows from 0x577fe0 to 0x5787e0. `uses_vpermute_unit_altivec` was once filed under VectorArraysToRegs; Wii's
    module agrees with the current cut.
  - .data 0x5d1b20–0x5d2c6c: the stage-name literals and the pointer table, `machine7450`, `finalstages[12]`,
    `pipeline_units[6]`, and a 404-entry `instruction_timing`.
  - .bss 0x5e3bd8–0x5e3d5c. The relink is byte-identical. Assert lines 945 and 1078 (Pro7: 879 and 991).
- Flags:
  - Deferred, because the rows descend.
  - `-sym off` is required: with `-sym on`, 4/7 match (serializes 20%, advance_clock 84%).
- Functions: 7/7 exact.
  - 2.6 changes, all in the disassembly:
    - The stages are renamed after 2.6's own name table.
    - `fetchqueues` start at 4,3,2,3,2.
    - The FPU3→FPU4 cycle adjustment, and the `fpu_completed` flag with the three-FPU-in-flight check.
    - Pipelines advance from their last stage back.
    - `finalstages` has 12 entries, of which 11 are used.
  - Needed shapes:
    - `for (i = 0; i < sizeof(finalstages)/sizeof(finalstages[0]) - 1; i++)` and the `sizeof` bound on the units
      loop. 2.6 enters both loops with a jump to the test, and an enumerator bound drops it; `(unsigned int)
      NumFinalStages` also matches. This is commented.
    - The `Stage` counter in the FPU count (AGENTS.md).
    - In `complete_instruction`, 2.6's FPU test sits between `int buf;` and `buf = 0;`. Pro7's initializer changes
      advance_clock (99.0). There is now a comment.
  - Natural spellings that now replace workarounds:
    - `can_issue`'s `isClear1`/`isClear2` are back in Pro7's declaration order.
    - `advance_clock`'s first two clamps are back to Pro7's ternaries. One `if` form alone gives 79.3.
- Behaviour: the tables are byte-identical. No bugs found.
- Names: Pro7's, `m7450_*` row `#define`s, and `machine7450` (Wii). Invented, with TODOs: `fpu_completed`,
  `stage_names` (statics; Wii's are unnamed).
- Types: Pro7's.
- Data: byte-identical. The `_machine7450` binding stays (Scheduler.c needs it, as for 7400).
- Header: Scheduler.h.
- Headers relied on: Scheduler.h (invented `MachineInfo` member names), PCode.h, PCodeInfo.h, CError.h.
- Open: the invented static names, the loop-bound shape and Scheduler.h.
- Adversarial review: attacked the Wii-name identifications, the bug claim, the header, the boundary and the
  unexplained constructs. Held: `create_alias_union` (Wii 0x5bf1b0: a==b returns a; two non-set aliases of one object
  give `make_alias(obj, 0, 0)`; else both into `worst_case_ptr`, which it returns; Wii adds only a set branch),
  `make_displ_alias` (Wii 0x5bef80: `size && alias->size != alias->object->type->size` then
  `make_alias(obj, alias->offset + offset, size)`, else whole object, then the one assert; 2.6 adds the worst-case
  fallback), `set_aliases` (Wii's initialize_aliases clears `aliases`, `set_aliases`, `n_aliases`, `n_gathered_aliases`
  in that order, as 2.6 does at 0x5e2fe0/0x5e2fdc/0x5e2fd8/0x5e2fd4; Wii's create_alias chains set aliases there; in
  2.6 only initialize_aliases references it), the make_distinct_virtual_alias bug (2.6 `cmp byte [esi], 5` on the
  pointer type; Wii's copy has the same bug, `cmp byte [edi], 6` on `obj->type` in its shifted type enum), the
  StructMoves 4-aligned .data start (0x5be000..0x5be023 is CInit's jump table into 0x50f4xx..0x50faxx; 0x5be024 is
  StructMoves' table into its own .text), the SFPE gc1 shapes, the AltiVec/7400 data. Broke (fixed): the misleading
  invented names `is_global_memory`/`worst_case_memory_type_global`; `add_alias_member` in the header with no outside
  user; Alias.h's unflagged placeholder names; SFPE's unlisted block-local copies. Still open as before: the `skip`
  ternary and `goto found` TODOs, `int w = width`, AltiVec's `sizeof(finalstages)/sizeof(finalstages[0]) - 1` bound
  (Pro7 has `finalstages[16]` with an enum `NumFinalStages`; 2.6's array is 12), the invented helper/static names. After
  the changes: 4020/4077 exact, nothing lost or gained, layout and all five relinks pass, clang_check 0, formatted.

### src/optimizer/CopyPropagation.c: SOLID WITH TODOS
- Boundary: 9 rows, 0x56ab90-0x56b980, Pro7 CopyPropagation.c's functions in reverse (deferred). Pro7's statics allocatepropinfo, canidatepropagatestoalluses and propagatecandidates are inlined (no rows). Pro7's `precedes` is UseDefChains' in 2.6 (Wii module UseDefChains, 0x56e130). .data (copy_prop + strings) and .bss are placed and link.
- Flags: pro6, -O4,p, -inline auto,deferred, -sym on. The rows descend against Pro7's order, so deferred is right.
- Functions: 9/9 exact. The file was rewritten from Pro7's text. Departures:
  - 2.6 changes: Propagation.mask, copied to a static (`cp_mask`) and tested in canidatepropagatestoalluses (Pro7 tests a hard-coded fRecordBit|fSideEffects). copypropagatestouse rejects any use that reads and writes the register (Pro7 checks only rlwimi). PCODE_FLAG_SET_F(pcode) & fUpdatesPtr in computecandidatelist. Format "c%ld". buf[52] (frame size).
  - Needed shapes: `!(reg < n_real_registers[..])` in is_copy (catalogued). `int reg1/reg2` (Pro7: short). Swapped i/list declarations. A `pcode` local in canidatepropagatestoalluses. `op = args; i = argCount` order and nested ifs in both branches of computelocalpropinfo. `number_of_candidates > i` (number_of_candidates is SInt32). computeglobalpropinfo's `preds` declared first (decl-order).
  - Fakematches: none.
- Behaviour: matches the bytes. The magic flag numbers are now names (fIsBranch, fIsMove, EffectWrite, RegClass_GPR ...). No bugs found in the original. Bugs of ours: none.
- Names: Pro7's throughout. Invented: `cp_mask` (TODO). PropInfo's gen/kill/in/out (TODO; Pro7's are offset placeholders vec0..vecC).
- Types: number_of_candidates is SInt32. Evidence: with int, AddPropagation's `i < number_of_candidates` cannot match. Tried `>`, an SInt32 `i`, and int with Pro7's `<`.
- Data: links byte for byte. 7 bindings for this object's statics and data removed (`_cp_mask`, `_is_candidate`, `_propagatestouse`, `_propagateandfinish`, `_data_005dfc04` (moreaggressiveoptimization), `_data_005ce5f8` (copy_prop)). `bindings.py refs` showed only this unit's functions referencing them.
- Header: declares what AddPropagation, Alias, LoadDeletion and GlobalOptimizer use. (Adversary: `ncandidatesinblock`/`firstcandidateinblock` had no user outside the unit, in 2.6's code either (`bindings.py refs`: only CopyPropagation's functions); their externs left the header, the COMMON definitions stay global in the .c.) #pragma pack(push/pop) balanced around Propagation (30 bytes). The SInt32 comment is updated.
- Headers relied on: UseDefChains.h (Uses/Defs, reg_Uses, usedefinfo, precedes; reviewed below), PCode.h (PCode/PCodeArg/flags: Pro7's names and values), Registers.h / RegisterInfo.h (n_real_registers, last_exception_register), BitVectors.h (bit macros, bitvector* with SInt32 counts; the 2.6 range/compressed types have invented names, TODO), CompilerTools.h (oalloc), PCodeListing.h, CMangler.h, CPrep.h (copts). No problems found beyond the listed TODO names; not reviewed in depth.
- Open: invented names cp_mask and PropInfo members (no source to take them from).

### src/optimizer/AddPropagation.c: SOLID WITH TODOS
- Boundary: 4 rows, 0x56b980-0x56c180, Pro7's 4 functions in reverse (deferred). .data 0x5d18f0 (add_prop, strings, assert file name, jump table) links.
- Flags: as CopyPropagation, deferred (rows descend).
- Functions: 4/4 exact. Rewritten from Pro7's text (the functions are `static` again; the old "global for lifted code" comment was stale). Departures:
  - 2.6 changes: addpropagatestouse limits psq_l/psq_lu/psq_st/psq_stu to a 12-bit displacement (now PC_PSQ_* names, not 0x193..). propagateandremoveadd first clears, in every block's in-set, the bit of a use that is itself an add candidate. Assert lines 258/392/406/436. Mask and "a%ld" in add_prop.
  - Needed shapes: `!(reg < ...)` in is_add. `reg27 = candidateInstr->args[1].data.reg.reg` (Pro7 `reg27 = reg2`; decl-order finds nothing better). `instr = Candidates[id].pcode` (Pro7 `candidate->pcode`). 2.6's declaration order.
  - Fakematches: none.
- Behaviour: matches. Magic numbers replaced: 0x60006 is fIsRead|fIsWrite|fPCodeFlag20000|fPCodeFlag40000, 0x20 is fIsPtrOp, `& 9` is PCODE_FLAG_SET_F. `args[2].arg == 1` stays a number (the repo has no PCOpMemoryArg enum).
- Names: Pro7's. Invented: none in this file.
- Types: Pro7's.
- Data: links. Binding `_data_005ce644` (add_prop) removed (only this unit referenced it).
- Header: propagateaddinstructions (GlobalOptimizer). (Adversary: the reviewer's "propagatedadds (GlobalOptimizer, LoopOptimization)" was wrong: nothing outside reads it, in the repo or in 2.6's binary; its extern left the header, the COMMON stays defined here.)
- Headers relied on: CopyPropagation.h, UseDefChains.h, Alias.h (make_alias, AliasType0; Alias.vec24 and AliasType0/1/2 are Pro7's placeholder names), StackFrame.h (can_add_displ_to_local), PCodeInfo.h (opcodes, change_opcode, nbytes_loaded_or_stored_by), CError.h, objects.h. Not reviewed in depth.
- Open: none in the unit beyond the header placeholders.

### src/optimizer/UseDefChains.c: SOLID WITH TODOS
- Boundary: 13 rows, 0x56e010-0x56f520 (Wii module UseDefChains). Pro7's file in reverse (deferred), plus 2.6's `precedes` (moved from Pro7's CopyPropagation.c) and `canidate_reaches_use` (Wii's spelling), the file's last two functions. Inlined, no rows: allocateusedefinfo and addworstcase (Pro7's static, restored). No .data/.bss of its own (COMMONs only).
- Flags: deferred, justified (rows descend).
- Functions: 13/13 exact. Departures:
  - 2.6 changes: ObjectUseDef direct/indirect counts. memclrw in addobjectusedef. worst_case_ptr/worst_case_func. Alias-type tests `type != AliasType1 && type != AliasType0` (Pro7: fIsPtrOp / `== AliasType2`; Pro7's spelling does not match). precomputeusedefcounts allocates usedefinfo and records each block's use/def ranges, with no fIsBranch test. Calls use may_alias_object (Pro7: a datatype test). reg_Uses/reg_Defs for all RegClassMax classes. must_alias_object/may_alias_object where Pro7 has uniquely_aliases/may_alias. Range/compressed gen/kill sets and the merge helper `updateusedefset`. last_usedef_register.
  - Needed shapes: pointer compares without Pro7's `(unsigned long)` casts (with the casts, findobjectusedef and computeusedeflists differ). `static inline addobjectusedef` (plain static is not inlined into addaliasusedef/precomputememoryusedefs). Chained `objectusedefs = objectusedeflist = NULL` and `number_of_Uses = number_of_Defs = 0`. `for (i = argCount; i--; op++)` in precomputeusedefcounts. The comma `reg_Uses[c][i] = NULL, reg_Defs[c][i] = NULL;`. precedes walking with its parameter. All tried in Pro7's spelling first.
  - Original's shape kept from Pro7: nested ifs in computelocalusedefinfo (Pro7's nesting now; the old `&&` forms were not needed). `flag = 1; while (flag)` in the global passes (was do/while).
  - Fakematches: none.
- Behaviour: matches the bytes (checked updateusedefset against IDA's pseudocode). Original's quirk kept: a direct memory use or def does not advance use/useID (as in Pro7; harmless, nothing follows).
- Names: Pro7's/Wii's. Invented: `updateusedefset` (TODO; Wii static, unnamed). UseDefInfo members (TODO, Pro7's are offset placeholders).
- Types: canidate_reaches_use's class parameter is RegClass (was char; Alias stays 21/21). updateusedefset's locals are plain int (the `(int)` casts were not needed).
- Data: no own data; the COMMONs link.
- Header: (Adversary: `objectusedefs`, `objectusedeflist` and `last_usedef_register` had no user outside the unit in 2.6 (Wii's LoopOptimization reads last_usedef_register, 2.6's does not); externs removed, definitions stay global COMMONs.) Everything else there is used by CopyPropagation, AddPropagation, Alias, CodeMotion, ConstantPropagation, LoopDetection, LoopOptimization, Peephole, StrengthReduction and VectorArraysToRegs. Pack balanced. canidate_reaches_use's prototype is now RegClass.
- Headers relied on: BitVectors.h (BitVectorRange/CompressedBitVector and the range helpers: invented names, TODO), Alias.h (placeholders Alias.vec24, AliasType0..2), PCode.h, Registers.h (used_virtual_registers SInt32), CompilerTools.h.
- Open: invented updateusedefset and UseDefInfo member names.

### src/optimizer/ValueNumbering.c: SOLID WITH TODOS
- Boundary: 14 rows, 0x508d30-0x50a3f0 (Wii module LocalCSE). Pro7's file in reverse (deferred). Inlined statics: labelvalue, unlabelvalue, stack*, unstackvalue, samevalue, killregister, killallCSEs, killregisters, chooselocation, isCSEload, registercopy, recentlystored, simplestore, pointerstore, arithmeticop, operatefrom/tomemory, propagatecopiesto, get/set/forkvaluestate, allocatecsedatastructures. killspecificCSEs is Pro7's dead static, kept.
- Flags: deferred, justified.
- Functions: 14/14 exact. The source is Pro7's text. Each remaining departure was reverted to Pro7's spelling one at a time; each loses its function:
  - unstackvalue's `op->kind`.
  - chooselocation's declaration order.
  - The comma in addavailablevalue.
  - Else-if chains in simpleload/pointerload (Pro7: early return).
  - `removecsesfrombasicblock(block = ...)`.
  - `if (!counter) return;`.
  - propagatecopiesto's `value` local.
  - simpleload's `if (rs) kind = ...; if (rs && kind)`. 2.6 change: makecopyforload. Tried `rs && (kind = ...)` in one condition and `kind = 0`.
  
  Further 2.6 changes: opvalue[OPCODE_MAX]; GQR0 (SPR 912) accepted in matchvalues, where Pro7 has CError_FATAL; assert lines 1015/1046.
- Behaviour: matches. No bugs found.
- Names: Pro7's. Invented: RegValue.copyreg (TODO; Pro7's x4).
- Types: Pro7's.
- Data: links. Bindings `_stackedvalues` and `_valueceiling` removed (only this unit referenced them).
- Header: killmemory (Alias), removecommonsubexpressions, removedcommonsubexpressions, nextvaluenumber (GlobalOptimizer, CodeGen). Fine.
- Headers relied on: Alias.h, PCodeInfo.h (OPCODE_MAX, makecopyforload, can_reuse_stored_value), Registers.h (coloring, used_virtual_registers), RegisterInfo.h (first_fe_temporary_register, last_temporary_register). No new problems.
- Open: RegValue.copyreg invented. No named constant for SPR 912 exists in the repo.

### src/optimizer/StrengthReduction.c: SOLID WITH TODOS
- Boundary: 18 rows, 0x571740-0x572fb0, Pro7's file in reverse (deferred). isbasicinductionvariable, copyinductionvariable and strengthreducenestedbiv are inlined; addinductionvariable has a row and is also inlined into findnonbasicinductionvariables.
- Flags: deferred, justified.
- Functions: 18/18 exact. Departures:
  - 2.6 changes: initializeinductionvariable reads the initializer value through Pro7's `value30`. The lis+addi pairs test `LOW_PART(v) != 0` (Pro7 `v != 0`) in initializeinductionvariable and incrementinductionvariable.
  - Needed shapes: findbasicinductionvariables' nested ifs with separate assignments. The variables are now at function level as in Pro7 (the block-local declarations were unnecessary). `if (counter != 1) return FALSE; return TRUE;`. Declaration orders in isinductionvariable, initializeinductionvariable, testnestediv, calc_biv_init and combineinductionvariables. combineinductionvariables' `last` local. `data.imm.value` reads of memory offsets (same field offset).
  - Original's shape (was filed as a fakematch): addinductionvariable's `iv->next = biv->inductionVars, biv->inductionVars = iv;`. 2.6 inlines the function, so its body is within the 15-statement auto-inline limit, one statement shorter than Pro7's: the original joined two statements somehow. A comma anywhere (`iv->basicVar = biv, iv->instr = instr;`) and `(iv = oalloc(...))->next = ...` match as well, so which pair is unknowable; a comma on a list push is 2.6's idiom elsewhere (ValueNumbering's addavailablevalue, UseDefChains). Pro7's separate statements: EXACT for it but findnonbasicinductionvariables and findallnonbasicinductionvariables lose. Presetting source_register or reordering the stores: 92-95%. `static inline` breaks the link.
- Behaviour: matches. Stale top-of-file TODO removed: it described private helpers that no longer exist.
- Names: Pro7's. Invented (TODO): InductionVar/BasicInductionVar members and BivInit members (Pro7's x-offset placeholders).
- Types: Pro7's. BivInit is 2-byte packed (Pro7: mac68k), balanced.
- Data: links (.data is the assert file name).
- Header: strengthreduceloops and strengthreducedloops (GlobalOptimizer, LoopOptimization).
- Headers relied on: LoopDetection.h (below), UseDefChains.h, PCodeUtilities.h / PCode.h (makepcode, insertpcode*), Registers.h, CError.h, CompilerTools.h; FITS_IN_SHORT, HIGH_PART and LOW_PART come from common.h.
- Open: invented member names (no source).

### src/optimizer/LoopDetection.c: SOLID WITH TODOS
- Boundary: 17 rows, 0x56c180-0x56e010, Pro7's file in reverse (deferred). makepreheaderblock and insertpreheaderbefore are inlined. Pro7's COMMONs LoopTemp and LoopList_First are now defined here, where Pro7 defines them (moved from IroLoop.c). Every unit still links and the link count is unchanged (2806).
- Flags: deferred, justified.
- Functions: 17/17 exact. Departures:
  - 2.6 changes: the switch table holds PCodeLabel pointers directly; Pro7's CTool_ResolveIndexToPointer/CreateIndexFromPointer calls are absent from the bytes. checkcountingloop's back-edge test uses `pc8->block` (Pro7: `prevpcode->block`; Pro7's form loses the match). The assert lines.
  - Needed shapes: computedominators' `SInt32` i/blockCount and declaration order (`flag` is int again, `flag = 1; while (flag)` as Pro7). Chained `predecessors = successors = NULL`. `(SInt32)*result < 0` in checkunsignedlooplimits. `(UInt16)` for `& 0xFFFF` (catalogued). One of checkcountingloop's two `== 0` step tests is `!(...)`: either one alone matches, both change insertpreheaderblock's registers (deferred coupling; comment in source). Removed Pro7's `check` local (with it, 99.0%).
  - Original's shape restored: loopheaders is Pro7's BlockList (was retyped PCLink). The stray `;` (Pro7's `;;`), Pro7's PPC register comments and commented-out code are removed.
  - Original's shape (was filed as a fakematch): findnaturalloop walks a block's successors (PCLink) and the loop's blocks (BlockList) with one variable, through a cast (the two have one layout). Pro7's `block` + `link`, `link` alone, a block-local `PCLink* succ`, a separate PCLink in each declaration position: all 77.7-83.1%, registers only (the walkers' colouring). Evidence it is 1.x's code kept: gc1's matched compute_loop_block_sets (1.2.5) walks successors and the block list with one variable `sub` of one link type. A PCLink-typed walker cast the other way also matches; the BlockList one stays.
- Behaviour: matches. Original's bug: analyzeloop never clears has_indexed_load/has_indexed_store (Pro7's x53/x54 too). Kept.
- Names: Pro7's. Invented (TODO): Loop members. Three were misleading and are renamed with tools/rename.py --member: block_membership → exit_dominators (Pro7's CodeMotion reads vec28 in `dominatesallexits`; findnaturalloop sets a block's bit when no exit block escapes its dominance: right), skip_leaf_pass_4f → is_straight_line (x4F: cleared when a non-header block has no or several predecessors or successors; unrollloop when set, simpleunswitchloop only when clear, StrengthReduction's nested-biv test: right), has_block_flag_40 → has_block_flag_4000, renamed again by the adversary to `has_unrolled_code`: fPCBlockFlag4000 is set only by CodeGen's newstatement for statements with StmtFlag_10, which only IroUnrollLoop sets; LoopOptimization skips unrollunknownBDNZ for such loops (Pro7 x52).
- Types: Pro7's; Loop.bodySize is SInt32 (2.6, per the existing comment).
- Data: links. Bindings `_dominators`, `_loopheaders`, `_loopstack` and `_nloopheaders` removed (only this unit referenced them).
- Header: Loop/BlockList/InstrList/(Basic)InductionVar are used by CodeMotion, LoopOptimization, StrengthReduction and GlobalOptimizer. Pack balanced. The LoopTemp/LoopList_First declarations stay in IroLoop.h.
- Headers relied on: UseDefChains.h, PCode.h (PCodeBlock flags fPCBlockFlag2000/4000 are placeholders), CFunc.h (codelabellist, CodeLabelList), objects.h (switchtable), enode.h (ELESS..), TOC.h, CError.h, CompilerTools.h.
- Open: invented Loop member names (no source).

- Adversarial review: attacked the two fakematches, the three Loop renames, the headers' declarations and Wii names. The comma (any statement pair, or `(iv = oalloc())->next`, matches; Pro7's spelling un-inlines it) and the findnaturalloop walker (gc1's matched 1.2.5 code reuses one walker the same way; every separate-variable spelling 77-83%) are the original's shape, not fakematches: comments rewritten. exit_dominators and is_straight_line hold against Pro7's uses; has_block_flag_4000 renamed `has_unrolled_code` (front-end-unrolled statements). Broke: the "header declares only what other units use" claims: six unit-private COMMONs (`propagatedadds`, `ncandidatesinblock`, `firstcandidateinblock`, `objectusedefs`, `objectusedeflist`, `last_usedef_register`) were declared in the headers (Pro7's headers have most of them; 2.6's binary has no outside reference); removed. Held: 75/75 exact, all six link, exact flags unchanged (4020 -> 4020, none lost), clang_check and clang-format clean.

### src/backend/BE_symbol.c: SOLID WITH TODOS
- Boundary: 18 rows 0x4d1040-0x4d1ec0 = Wii's BE_symbol module minus what 2.6 lacks (Wii adds MakeSectionEndElfSymbol, MakeSectionNameTableEntry, ELF_DECLARE_SYMBOL, MakeNamedDWARF1SymbolTableEntry, DWARF2_*). The 136-byte static at 0x4d1650 is Wii's unnamed 0x88-byte static at the same place. MakeNamedSymbolTableEntry has no copy in 2.6 (inlined everywhere); it now sits where Wii has it, after InorderFirst. .data is 20 bytes ("BE_symbol.c", "..", ".0"), so no dead functions are missing. Removed `BE_symbol_CreateSectionSymbol`: a 1.x function the original lacks, with no callers (the "ObjGen still calls it" comment was stale).
- Flags: deferred, because the rows descend against Wii's (deferred) file order; 15/18 exact without it. `-sym on` matters: 12/18 with `-sym off`.
- Functions: 18/18 exact. Departures:
  - `Next` is now the plain `CError_ASSERT` plus chained assignment (the old copy through `entry` was not needed).
  - `elf_declare_symbol` now uses a plain `next` local; the old `tail = (head = ...)->next` shape was not needed.
  - `CreateDataPoolAnchorSymbol` now calls AddSymbolTableEntry (the written-out copy was not needed).
  - `GetDataPoolRoot`: the `(UInt32)` cast was dropped (not needed). Kept: `pool = link = ...`, two copies of the link, because 2.6 copies it into a second register. One local, the copy as its own statement, and an inline helper for the condition all reach 95-97%. Classed as the original's shape (chained assignment).
  - `MakeNamedSymbolTableEntry` keeps AddSymbolTableEntry written out: calling it breaks 9 callers.
  - `FindSymbolTableEntry` (needed for the `node = NULL` after the loop) and `FindUsedObjectExtension` (written into the callers, they compile shorter) are invented inline helpers, both needed.
  - The group list is the tail-pointer idiom: `ObjGenSymbolLink.tail` is now `BE_SymGroup**` (`tail = &groups; *tail = group; tail = &group->next;`), exact in BE_symbol and in TOC's reset loop with no cast (`next` is the first member, so the bytes are the same). This replaces the reviewer's `(BE_SymGroup*)&link->groups` cast.
  - AddFunction's `binding <<= 4; binding += 2;` and AddEntry's `binding <<= 4;` are now the plain `st_info = (binding << 4) + 2` / `binding << 4`: both exact.
- Behaviour: checked against IDA. The 0x102/0x103 "kind" is the storage class: `CParser_HasInternalLinkage ? TK_STATIC : TK_EXTERN`, and the anchor object's sclass is 258 too. It is now named. `flags & 0x40` is `OBJECT_EXPORT`. GetDataPoolRoot compares the symbol's *size* with the window's base + 0x8000, as the original does. Bugs of ours: none found.
- Names: Wii's for all functions. Invented, with TODOs: FindOrAddObjectExtension (Wii has it unnamed), FindSymbolTableEntry, FindUsedObjectExtension, `last_section_symbol` (an unnamed .bss static), `BE_SymbolTable` and its members (Wii names only the COMMON `TheSymbolTable`, whose hash order checks: 3024, between optimizedloops 3024 and rt_mod2i 3031).
- Types: BE_SymNode reworked (BE_symbol.h):
  - The 1.x wrappers `nameData.hashName` and `sectionData.section` are now `name` and `section`.
  - The six fields at +8 are now an embedded `Elf32_Sym elf`: Convert_Elf32_Sym runs on it, and WriteSsymtab writes those 16 bytes.
  - `kind` is now `sclass`.
  - `alignment` through `reserved2a` are the 8-byte per-symbol record WriteSsymtab appends to .comment (alignment, a linkage byte 13/14 for implicit-weak/weak, flags 8 force-active/exported and 0x10 pool anchor, 2 bytes of zero padding). The original reads nothing at +0x2a (scan of BE_elf/ObjGen/BE_symbol), so `reserved2a` is padding. The names are invented; the header now has the TODO the first entry claimed.
- Data: relinks. `last_section_symbol` is static, and its binding was deleted (only BE_symbol references 0x5e0af8). `TheSymbolTable` is a COMMON and keeps its binding, because inexact BE_elf code reads it.
- Header: declares what BE_elf, ObjGen, TOC, DWARF, StackFrame, PCodeAssembly, Operands, InstrSelection and CodeGen use. The dead `BE_symbol_CreateSectionSymbol` prototype was still there and is now removed; `AddSymbol` (no user outside the unit) is removed. It includes ELF_Endian.h for Elf32_Sym.
- Headers relied on:
  - BE_elf.h: ElfFile has invented member names. Its `struct Elf32Rela` was moved to ELF_Endian.h as `Elf32_Rela`.
  - ObjGen_PPC_EABI.h: `BE_SymGroup` and `ObjGenSymbolLink` have made-up names (TODO); ObjGenSection embeds an Elf32_Shdr at `nameoff` that BE_elf casts to. Changed here: `ObjGenSymbolLink.tail` is `BE_SymGroup**` (its only users are BE_symbol and TOC).
  - CPrep.h/CInline.h (COpts): WriteSsymtab (BE_elf) tests `copts.no_comment_records` to choose whether the .comment records are written (main's name; it was the placeholder `f27`).
  - CParser.h (TK_*), objects.h (OBJECT_EXPORT, Q_*): fine.
  - CMachine.h, CMangler.h, CodeGen.h, RegisterInfo.h, TOC.h: only prototypes used; not reviewed.
- Open: the invented names above (TODO-marked).
- Adversarial review: attacked the list-head cast (natural tail-pointer idiom matches: fixed, TOC too), the st_info shift statements (natural spelling matches: fixed), GetDataPoolRoot's `pool = link` (held: one local 97.4%, either chained order or split 97.4-98.5%), the inline helpers (held: 2.6's double `jmp` is an inlined return), the st_size comparison (held: `cmp [esi+0x10]`, unsigned `jae`), `reserved2a` (it is the .comment record's padding), the header (a dead and an unused prototype removed; the TODO on the linkage names was missing).

### src/backend/ELF_Endian.c: SOLID WITH TODOS
- Boundary: 8 rows 0x4424a0-0x442e00, in Wii's ELF_Endian order: Convert_DWARF_Info, 2 unnamed statics, Convert_DWARF_line_info, Rela, Sym, Shdr, Ehdr. The two statics are now `static`. .bss is one static (the error callback).
- Flags: not deferred (ascending, like Wii's order). `-sym on` matters: 3/8 with off.
- Functions: 8/8 exact. Rewritten:
  - Dropped: 19 unused includes; gc1's `readhdr`/`readheader` inline helpers (not needed); dead `value` reads and `(void)value` (not needed); `svalue`.
  - Convert_Elf32_* now take typed ELF structs. Convert_Elf32_Sym was `void*` with a pre-read and a return value nothing uses.
  - Convert_DWARF_line_info no longer uses the invented ConversionBlock structs.
  - read_double returns `double` and has become a static.
  - Kept, with TODOs:
    1. Convert_DWARF_Info's entry length and end are in a `volatile` struct. This is a fakematch: plain locals in all 120 declaration orders reach 89.5%, and a non-volatile struct 95.9%. The volatile struct also forces `x = x - 6`. Re-tried adversarially: `long`/`SInt32`/`unsigned long` locals (85-89.5), a long struct (95.9; decl-order with swaps finds nothing better), `int`/`long[2]` arrays (88-95.9), volatile scalars (98.8), gc1's inline reader taking `&length` (88-95.9), the dead value reads restored in every identical arm (89.5). The original's 16-byte frame has an unused slot at [esp+0] that only the volatile struct reproduces.
    2. 2.6 places `end += length` after the function's return and jumps back. Only the `goto` shape reproduces this; an if/else, a nested loop and a `for` initializer reach 86-87%.
    3. In swap_tagged_record_data, `op = *(p = data + offset)` with `p` unread (99.1% without it).
    4. `CTool_...(p32 = ...)`, the pointer assigned in the argument (98% otherwise).
  - Identical if/else arms are what remains of a read-and-swap whose value is unused. The original's code keeps the two arms apart; a macro form compiled differently (84%).
- Behaviour: checked against the disassembly. A genuine bug in the original, kept: read_double calls `CTool_EndianConvertMem(&bytes, 8)`, which swaps the pointer parameter and the 4 stack bytes after it, never the double (confirmed: after `push 8`, `lea eax, [esp + 0x10]` is the address of the parameter slot at [esp+0xc] on entry; CTool_EndianConvertMem reverses bytes in place unless copts' first byte is set). So DWARF FORM_DATA8 values are never byte-swapped; the other arm would read through the reversed pointer, but BE_elf only passes swapBeforeRead = 1. The callback is CError_Internal (line 396). Bugs of ours: none.
- Names: functions are Wii's. Invented, with TODOs: `swap_tagged_record_data`, `read_double` (Wii's are unnamed statics) and `error_handler`. Locals were renamed to DWARF terms (attr, op, inSubscript).
- Types: ELF_Endian.h now has the specification's `Elf32_Ehdr` (`e_ident[16]` ...), `Elf32_Shdr`, `Elf32_Sym` and `Elf32_Rela`, replacing the invented `Elf32Header` (magic/elfClass/...), `Elf32Section`, `ElfSymbolRecord`, `ConversionBlockEntry/Header` and `RecordBounds`. Exception: `st_value`/`st_size` are SInt32, because ObjGen_DeclareExceptionTables' chained assignment needs signed (it lost its match with UInt32). Uses were renamed in BE_elf.c and ELF_Lib_Import.c/.h (`e_ident[4..6]` for class/encoding/version), and the typedefs dropped from common.h. The DWARF attribute constants (0x23, 0xa3, 0x2013, ...) stay numeric: no project header names them.
- Data: relinks (.data: "ELF_Endian.c" plus the jump tables; .bss: the callback).
- Header: declares only the six public converters; the swap helpers became statics.
- Headers relied on:
  - CompilerTools.h: CTool_EndianConvertInPlaceWord16Ptr(`short*`) and Word32Ptr(`unsigned int*`), where Pro7 has `UInt16*`/`UInt32*`; CTool_EndianConvertMem(`void*`), where Pro7 has `UInt8*`. Not changed: shared.
  - ELF_Endian.h: own, fixed.
- Open: TODOs 1-4 above (1 is a fakematch proven necessary), and the invented static names.
- Adversarial review: re-derived the read_double bug from the bytes (holds); attacked the volatile struct with nine more shapes (none matches; the TODO now lists them), TODO 3's unread `p` (six spellings, only the assignment-in-expression matches; even `p = data + offset; op = *p;` is 99.1); grade held.

### src/backend/Exceptions.c: SOLID WITH TODOS
- Boundary: 8 rows 0x4fb890-0x4fd8a0 = Pro7's Exceptions.c (Wii's module has the same 8 plus statics). addrelocation, deleteexceptionaction and findPC_long are inlined everywhere; their out-of-line copies are unreferenced and dropped by the linker. findPC is Pro7's dead function, with Pro7's assert line (704).
- Flags: deferred (rows reverse Pro7's order). `-sym on` matters: 5/8 with off.
- Functions: 8/8 exact. Departures from Pro7, each tried both ways:
  - 2.6 changes:
    - Naturally aligned records, so relocation offsets are +8/+12/+16/+20 where Pro7 has +6/+10/+14/+18.
    - Each action chain starts 4-aligned (zero padding).
    - `| 2` in the flag word for paired singles on ELF/Gekko with StackFrame_has_vectors.
    - The VR count has no `& 0x1F`.
    - Assert lines 717 and 1264.
  - Needed shapes:
    - `(reg ? 1 : 0) << 7`: Pro7's `(reg != 0) << 7` gives 96.9%.
    - `((reg2 ? 1 : 0) & 1) << 6`: 99.7% without the `& 1`.
    - noteexceptionactionregisters' `reg = ...; if (reg)`: Pro7's `if ((reg = ...))` gives 94.1%.
    - mergeexceptionactions' `if (!pc_actions)` plus `for`: Pro7's gives 96.9%.
    - dumpexceptiontables:
      - `sh[0] = sh[0] | 4` (`|=` gives 97.9%).
      - A `char* data` beside `sh`: 2.6 copies the buffer pointer into two registers (Pro7's single `sh` gives 91.6%). Classed as the original's shape.
  - No fakematches.
- Behaviour: checked against IDA for the flag word, the VR word and the padding. Genuine bugs in the original: none seen. Bugs of ours: none.
- Names: Pro7's. Invented, with TODOs: `EANode.actionOffset` (Pro7: xE) and `ExceptionThing.pcOffset/pcCount/actionOffset` (Pro7: x0/x4/x6). The record structs (AABC ...) are Pro7's.
- Types: Pro7's EANode and PCAction, and Pro7's ExceptionAction (Exceptions.h).
- Data: relinks. The statics were already `static`; their stale bindings were deleted (pc_actions, last_pc_action, exceptmodule, except_refs, last_except_ref: only Exceptions references them). DAG is a COMMON (Wii's) and keeps its binding.
- Header: Pro7's Exceptions.h minus mac68k alignment, and now minus `EANode`, `PCAction` and `extern DAG`, which no other unit uses (moved to the top of Exceptions.c; the unit and every other stay exact). EAT_* are used by COptimizer, IroJump, DumpIR and CPrec; the five functions by CodeGen, InlineAsmPPC and PCodeUtilities.
- Headers relied on:
  - Exceptions.h: ExceptionAction now has Pro7's name and kind-specific members (`type`/`prev`/`data.destroy_local`/`data.catch_block.catch_label`/`catch_typeid` ...); the generic `slots`/`operands` words CInline walks remain (TODO there).
  - PCode.h, StackFrame.h, RegisterInfo.h, PCodeUtilities.h: prototypes only.
  - ObjGen_PPC_EABI.h: `RelocationList` is Pro7's OLinkList, with `object`/`addend` for Pro7's `obj`/`somevalue`.
- Open: the invented member names above.
- Adversarial review: re-ran the natural spellings of dumpexceptiontables (single `sh` 91.6, `*exceptmodule.data + 4` 91.7, `|=` and Pro7's swap-then-or 97.9: held); found the header exported three file-private declarations (fixed). Grade held.

### src/backend/DumpIR.c: SOLID WITH TODOS
- Boundary: DumpIR and CleanupDumpIR (1-byte `ret`s) plus Pro7's dead dump functions, kept for their literals and the nodenames table. .data 0x5bc698-0x5bcfc8 is placed by `data_at`.
- Flags: deferred (DumpIR sits below CleanupDumpIR: the reverse of Pro7's order). `-sym` makes no difference here.
- Functions: 2/2 exact. The dead bodies are Pro7's, with 2.6's "\r\n" and `%ld` literals.
- Behaviour: n/a (2.6's dump is compiled out). TODO kept: 2.6 spells "TD" between {PR} and {RW} where Pro7 has {NL} for DNONLAZYPTR; the `DVFUNC` case label is a guess, since dead code leaves no jump table.
- Names: Pro7's.
- Types: the ENode members used are enode.h's (see Headers relied on).
- Data: relinks.
  - `curstmtvalue` (a COMMON) is defined in CFunc.c, where Pro7 defines it (main had already moved it). It had been here under "Globals (generated from the original's .bss)".
  - The .bss placeholder shrank from 12 to 8 bytes: the object's .bss is then 20 bytes, Wii's DumpIR size, consistent with the next object starting 8-aligned at 0x5e0d80. It is still a placeholder (TODO): nothing references those bytes.
- Header: now only DumpIR and CleanupDumpIR (CodeGen, FuncLevelAsmPPC, ObjGen use them); Pro7's SetupDumpIR, DumpExpression, DumpSwitch, DumpType and DumpStack had no user outside the unit (DumpType gets a prototype in the source, used before its definition).
- Headers relied on:
  - enode.h: ENode member names differ from Pro7's (`string.useExplicitSize` for `ispascal`, `data.precomp.*` for `nullcheck.*`, `labelAddress` for `label`, `vector128` for `vector128val`).
  - Switch.h, Exceptions.h (ExceptionAction): as above.
  - CInt64.h, CMachine.h, CMangler.h, types.h: prototypes.
- Open: the `unknown_statics[8]` placeholder; the TD case label.
- Adversarial review: checked the header against its users (five declarations unused outside: removed); the .bss placeholder holds (dead unit, so nothing references any of its 20 bytes; Pro7's statics are 12); grade held.

### src/backend/OpcodeInfo.c: SOLID WITH TODOS
- Boundary: a data-only object (Pro7's unsorted/OpcodeInfo.c), placed after CDecl.c by `data_units`.
- Flags: no code; `#pragma pool_strings on` is what puts the strings after the table, as in 2.6's .data.
- Functions: none. The table has 2.6's 471 entries (Pro7: 430). Every row shared with Pro7 now has Pro7's flags; 4 rows (LSWI, LSWX, ECOWX, LSCBX) were put back in Pro7's flag order. 2.6 differences: the 41 Gekko entries, and "=`f" operand strings for single-precision FP.
- Behaviour, Names, Types: Pro7's; the bytes are checked by the relink.
- Data: relinks.
- Header: PCodeInfo.h (`OpcodeInfo`, `extern opcodeinfo[]`, 2-byte packing). The include is restored, as in Pro7.
- Headers relied on: PCodeInfo.h and PCode.h (the f* flag names), not reviewed here. AGENTS notes that some PCode.h flag macros are not Pro7's values, but every name used here gives 2.6's bytes.
- Open: header review only.
- Adversarial review: nothing attacked beyond the relink (data only, byte-identical); grade held.
- Header round H2 (October 16): PCodeInfo.h and PCode.h are DONE WITH TODOS; the table uses `fHasBaseReg` (TODO-invented, Pro7 fPCodeFlag400000). Resolved as TODO names.

### src/backend/ScanFloatX86.c: SOLID
- Boundary: one function, Wii's ScanFloatX86 module (Pro7's ScanFloat.c is the Mac version).
- Flags: `-sym` and deferred make no difference (one function).
- Functions: 1/1 exact. Pro7's body plus 2.6's errno reset and test, written `errno = 0; ... *failed = errno != 0;` through include/libc/errno.h (in MSL Win32, errno is `_GetThreadLocalData()->errno`, the second word; the disassembly's two `_GetThreadLocalData()` calls and `[eax + 4]` agree). Parameters are now Pro7's names (`str`, `result`, `failed`, `const char*`).
- Behaviour: checked against the disassembly (the reset, strtod, `setne` into `*failed`).
- Names: Pro7/Wii.
- Types: `char* failed` where Pro7 has `Boolean*`: CMach_FloatScan passes a `char*`.
- Data: none.
- Header: ScanFloat.h (Pro7's) declares it for CMachine.
- Headers relied on: include/libc/errno.h (`errno`).
- Open: none.
- Adversarial review: graded on the errno form (the `_GetThreadLocalData()[1]` stand-in is gone); the parameters were `char* value`/`char* error` and are now Pro7's `const char* str`/`failed`.

### src/backend/Intrinsics.c: SOLID WITH TODOS
- Boundary: 30 rows 0x4c45b0-0x4cfde0. They are Pro7's Intrinsics.c function list in reverse, plus two 2.6 additions: `rtos_change_stack_intrinsic` (inlined) and `va_info_intrinsic` (the lowest row, so the file's last function). No Pro7 function is missing: a name diff against Pro7's file shows only those two additions. Wii's Intrinsics module has 38 functions, with Wii-only additions (`Intrinsics_HasBuiltinIntrinsic`, `intrinsic_id`). An unnamed static sits right below `Intrinsics_IsPublicRuntimeObject` there, which matches va_info_intrinsic. The neighbours on either side are CInt64 and RegisterInfo. .data starts at 0x5b1d68 and .bss at 0x5e05e0, both 8-aligned. The relink confirms the cut.
- Flags: `-O4,p -inline auto,deferred -sym on`. Deferred is justified because the rows run in reverse of Pro7's order. Tests: without deferred, 28/30 match, and the function order is reversed so the unit would not link. With `-sym off`, 20/30 match.
- Functions: 30/30 exact. Departures from Pro7 and why each stays:
  - `short opcode` instead of Pro7's `Opcode`: the repo's opcodes are `#define`s (PCodeInfo.h), which is a header decision.
  - `SInt32 reg` in load_bytereversed and alloca: needed shape, already in AGENTS.
  - checkconstintarg: `PPCError_ErrorTerm(210, name, 3)` is a 2.6 change (bytes). It reads `expr->data.intval.lo` because `CInt64_GetULong` makes rlwimi_intrinsic 99.7 (tested).
  - promotable_types_are_equal: Pro7's body plus a `TypeIntegral* bi` copy of `b`. The copy is 2.6's shape: it gives the `mov edx, eax`, and with only the copy the function is exact but gets inlined. It is wrapped in `#pragma dont_inline` (TODO). This replaces the old padded body (`result` and `is_int` locals that existed only to exceed 15 statements), which was a fakematch. Pro7's verbatim body is 70%.
  - VerifyParameterCount: `count = 0; scan = args; while (scan) {scan = scan->next; count++;}`. Original's shape. Pro7's `for` gives HandleIntrinsicCall 71%; `for` with `count = 0` first gives 97.3; incrementing before advancing gives 97.3.
  - `Type* typeB;` declared before `typeA` in 5 functions: original's shape. Each Pro7 order was tested: 78-94%.
  - Early `return NULL` in Verify1VectorArg and Verify1VectorArg2Ops: Pro7's `rtype = NULL` gives 98.2 and 95.5.
  - In HandleIntrinsicCall, `rtype = f(); if (rtype)`: Pro7's assignment inside the condition gives 98.9. All of these are AGENTS-catalogued 2.6 shapes.
  - `rtos_change_stack_intrinsic` is a separate helper: written in the case it gives 60.3. Needed shape, invented name with a TODO.
  - 2.6 changes, all confirmed by exact bytes: the FP guard (error 131 when `!use_fp || soft_float`) on the four FP intrinsic cases; ids 52/54 pass their arguments swapped; ids 311-314; the 2.6 type tables.
- Behaviour: every function is byte-exact and the unit links.
  - Genuine bug in the original (kept): `vector_all_lt_type_table` has no NULL terminator (Pro7's has none either, so it is not a 2.6 change). Verify2VectorArgs's loop is bounded only by `table->rtype`, so a failed search reads vector_all_nan's TypeTable21 row as a TypeTable31 row and takes its opcode PC_VCMPEQFP (281 = 0x119) as an arg2 type. The pointer test `mov al, [ebp]` at 0x4cefde then faults. Reproduced: the original mwcceppc.exe under wine, `-proc 7400 -vector on`, `vec_all_lt(vector float, vector signed int)`, gives a page fault reading 0x119 at 0x4CEFDE. The equivalent compiler faults the same way. A matching call does not fault.
  - Pro7 bug that 2.6 fixed: vec_cmple/vec_cmplt now pass their operands swapped.
  - Bugs of ours: none found. Only a stale comment, the opcode table's `/* Intrinsic_004 */` label for PC_FSEL, which is Intrinsic_034 (fixed).
- Names: functions and statics are Pro7's. Invented, each with a TODO: `va_info_intrinsic` (Wii has it static and unnamed; Pro7 lacks it), `rtos_change_stack_intrinsic`, and va_info's `counts` union members.
- Types: Pro7's. `Intrinsics_ReInitRuntimeObjects` is now `Boolean (Boolean flag)` as Pro7 and the header have it; the definition had been `unsigned char (unsigned char active)`. The `(Intrinsics)` casts on `intrinsicid` (Pro7 `SInt32`) were removed, as were all `TYPE(TYPE(&x))` double casts and `(Type*)&x` casts. The setup and reinit runs use `Intrinsic_NNN` enumerators instead of bare numbers. Setup uses Pro7's `TYPE_FUNC(...)->` instead of a `function` local, and `MaxIntrinsics` and `CError_ASSERT` instead of `315` and `if/CError_FATAL` (all still exact). CheckRuntimeObject is Pro7's text.
- Data: .data and .bss are byte-identical: the object's non-code sections were compared against the starting object, and the relink passes. The statics are `static`. `va_info_intrinsic` is now `static` with a prototype at the file top, because call_intrinsic_function uses it before its definition. The literals are at their uses. In GC_2_6 I deleted 317 bindings inside this object's .data/.bss (`_registration_name_*`, `_initializer_data_*`, `_typeTable`, `_opcode$1289`, `_char_ptr$1644`, `_intrinsics`, `_cur_intrinsic_object`). `bindings.py refs` shows only this unit's exact functions reach them. `___memcpy_object` (a COMMON) is kept.
- Header: declares only what other units use: the enum (TOC), `__memcpy_object` (TOC), is_intrinsic_function_call (TOC, InstrSelection), and the rest (CodeGen, InstrSelection). Removed `va_info_intrinsic` (now static), `extern int VectorConditions` (defined nowhere, used nowhere) and the `#include "compiler/InlineAsmPPC.h"` that was only there for Operand. Every includer was rebuilt and no function anywhere was lost. The source no longer includes CodeGen.h, enode.h or Registers.h (`includes.py trim`; none is in Pro7's list). CPrep.h was dropped too but is back (replay onto main): main's Registers.h no longer includes it, and the unit reads `copts` directly, so it includes it itself (it already arrives first through CExpr2.h -> CSOM.h -> CTemplateClass.h -> CPrec.h, so the include generates nothing new).
- Headers relied on:
  - CPrep.h (COpts: `cplusplus`, `use_fp`, `soft_float`, `littleendian`): `use_fp` and `soft_float` are invented names with TODOs. The placeholder left is `unknown66` (main has named the others).
  - PCodeInfo.h (the PC_* opcodes): `#define`s where Pro7 has the `Opcode` enum, hence `short opcode` throughout. It also has invented OpcodeInfo member names (TODO).
  - PCode.h (emitpcode, setpcodeflags, fSideEffects): nothing found.
  - InstrSelection.h (GEN_NODE_TO_*/ENSURE_* macros, cgdispatch, unary_operator...): one parameter named `unk`.
  - Operands.h (Operand, memclrw users): nothing found.
  - RegisterInfo.h (`last_argument_register`, `used_virtual_registers`, ALLOC_GPR): nothing found.
  - objects.h (Object, `u.func.u.intrinsicid`): nothing found.
  - types.h (TypePointer, TypeIntegral, TYPE_* macros): `short x12` placeholder.
  - CMachine.h (the st* type objects): nothing found.
  - PPCError.h (PPCErrorStr ids): nothing found.
  - CExpr.h, CExpr2.h (CExpr_PointerGeneration, CExpr_AdjustFunctionCall, create_objectrefnode): CExpr.h `data_0055f5fa`; CExpr2.h `xE` and `data_00580748`.
  - CScope.h (CScope_FindNameSpaceName, CScope_AddGlobalObject, cscope_root): `x1F` and `x21` placeholders.
  - CParser.h (CParser_NewRTFunc, void_ptr...): nothing found.
  - CompilerTools.h (GetHashNameNodeExport, CTool_EndianConvertWord16, lalloc): `data_00587958` and `data_00588228`.
  - CMangler.h, CInt64.h, StackFrame.h (`local_base_register`, dummyva*), StructMoves.h, FunctionCalls.h, scopes.h: nothing found, except StackFrame.h's `data_005884ff` and the Pro7 `x0..xB` TODO.
  - CError.h: `data_005830c8`. enode.h: `unk08`.
  - Every `#pragma pack` push is balanced by a pop.
- Open: three TODOs, all justified. (1) `#pragma dont_inline` on promotable_types_are_equal. Whatever kept 2.6's copy out of line is unknown. No Wii pair exists, so the earlier "Wii has it out of line" claim was removed. (2) The invented names `va_info_intrinsic` and `rtos_change_stack_intrinsic`. (3) The invented `counts` union member names. Other versions' bindings (2.0, 2.0p1, 2.5, 2.7) still carry the `_data_00588476`/`_data_00588478` fragments inside `last_argument_register` and this unit's placeholder data bindings (not touched: GC_2_6 only).
- Adversarial review: I attacked the dont_inline TODO, the vector_all_lt claim, the header's users and the comments.
  - dont_inline: 12 natural spellings tried. A body over 15 statements does stay out of line with every caller exact (CInline_IsSmallFunction), so the pragma is not the only route. But none of those bodies compiles to 2.6's 99 bytes: separate ifs and else-if chains 147 bytes, a result flag 92-106, `goto yes` 86 (87.1 with `bi`), a helper 66.7. A block-local `bi` is still inlined (96.8). The TODO holds, now with the list of what was tried.
  - vector_all_lt: the crash is confirmed by running the original. The comment wrongly called the missing NULL a 2.6 difference (Pro7 lacks it too); fixed.
  - Header: every declaration has an outside user (CodeGen, InstrSelection, TOC), and the types are Pro7's.
  - Held: grade SOLID WITH TODOS. Nothing lost: 4020/4077 exact before and after, and the unit links.

### src/optimizer/IroLinearForm.c: SOLID WITH TODOS
- Boundary: 34 rows 0x46c0a0..0x46fc40 are Pro7's IroLinearForm.c in reverse (IRO_NewLinear highest, IRO_RestoreLinearIR
  lowest); Wii's IroLinearForm module has the same publics; no other unit's names in range. Pro7's statics LinkLinear,
  MarkSubExpr, MarkAllSubExprs, NewStatement, PutTempOnNullCheckList, GetTempFromtheList (inlined, no rows) and the dead
  AssignDangerousArgumentToTemp are in the file. Its asserts follow Pro7's lines (+0 before GenerateExpr2TempAssignment,
  +3 after its 3 new lines, +7 after IRO_Delinearize), which also places the dead function's 627. .data 0x5abf28..0x5ac534
  and .bss (12 bytes: PrevStmt, CurrStmt, NullCheckList, reverse of Pro7's declaration order) unchanged by layout.py.
- Flags: changed to `-inline auto,deferred` (rows descend against Pro7's order; names-2.6.md listed it "to change").
  `-sym on` is the default; nothing showed it matters.
- Functions: 34/34 exact. The file is now Pro7's text (the gc1-style rewrite with ~10 invented helpers, LINEARFORM_FATAL,
  the fake `IrNodeList` struct, literal 0x1e allocation sizes and numeric statement kinds is gone). Pro7's verbatim text
  gave 20/34 deferred; departures, each confirmed in the disassembly:
  2.6 change: GenerateExpr1/Expr2TempAssignment also test `!IS_TYPE_VOID(expr1/expr2->rtype)`, Expr2 creates the
  temporary when `*objptr` is NULL, LinearizeExpr1's ECOND starts `obj = NULL`; MarkArgs has `case IROLinearNop`;
  IRO_Delinearize calls IRO_RenumberInts() first (inlined). Needed shape: argument-count loops `count++` before
  `list = list->next` (LinearizeExpr, LinearizeExpr1; catalogued); BuildExpr's funccall block declares `ENodeList* list`
  at the block top before `int i` (Pro7 declares it in the loop: 95.9 -> exact). MarkSubExpr is `static inline`
  (Pro7 `CW_INLINE`; same code, no out-of-line copy for clang). No fakematch.
- Behaviour: the old text matched; the new one is Pro7's plus the five confirmed additions. Bugs of ours: none found
  (the old helpers allocated `0x1e` for a 30-byte ENode/Statement by literal; gone).
- Names: Pro7's throughout (functions, locals, statics). Invented: MarkSubExpr (Pro7: "assumed name, position"),
  TODO-marked.
- Types: Pro7's (IROLinearIRSave, NullCheckListNode); `StatementType` for NewStatement's parameter (CFunc.h's typedef).
- Data: links (relink: "links into the original"); statics `static`; deleted 2.6 bindings `_PrevStmt`, `_CurrStmt`,
  `_NullCheckList` (only this unit's exact functions reach them). COMMONs IRO_FirstLinear/LastLinear/NumLinear/CurStat
  defined here as in Pro7.
- Header: IroLinearForm.h is Pro7's (plus TODO-marked member names); every declaration has users elsewhere.
- Headers relied on: IroLinearForm.h (IROLinear: Pro7 placeholders `ctch.x4/x8` TODO, `flags` UInt32 with a stated
  reason); IrOptimizer.h, IroUtil.h, IroVars.h, IroFlowgraph.h, IroDump.h (fine); CFunc.h (Statement, CLabel:
  `CLabel.stmt` is Pro7's (the repo's `target.{stmt,node}` union is gone in main); `typedef enum StatementType`, which
  this review added, main has too);
  enode.h (added Pro7's `ENODE_IS4`; Pro7's `ENODE_CASE_*` macros absent, cases written out); InlineAsmPPC.h (IAEffects:
  member names invented from Pro7 x0..x5, commented); CPrep.h (`copts` lives here, Pro7: CParser.h); CExpr2.h
  (create_objectrefnode; Pro7: CExpr.h). None reviewed as units.
- Open: header review only.
- Header round H2 (October 16): its headers are reviewed: IroLinearForm.h's `ctch` and other placeholders are TODO names; copts is CParser.h's; CExpr2.h is merged into CExpr.h; IAEffects' members are TODO names (InlineAsm.h). Resolved as TODO names.

### src/optimizer/IROUseDef.c: SOLID WITH TODOS
- Boundary: 24 rows 0x45dc70..0x461090, Pro7's IROUseDef.c reversed plus 2.6's IRO_AddTypeConversion (Wii: IroType's
  public of that name, in 2.6's IROUseDef range) and the out-of-line `Bv_SetBit` header inline. Asserts are Pro7's +159
  throughout. Now defines Pro7's COMMON `ENodeType IRO_NonAssignmentOp[MAXEXPR]` (no source defined it; relink holds).
- Flags: deferred (reversed order); `-sym off` justified: `-sym on` gives 16/24.
- Functions: 24/24 exact. Departures from Pro7:
  2.6 change: UpdateUse and UpdateUseForOtherSelfAssignment promote small integer/enum types through
  IRO_AddTypeConversion (callers pass `father->rtype`; the pointer -> `stunsignedlong` choice moved into UpdateUse);
  GetAssigned skips an EBITFIELD; MakeDef's global test includes DABSOLUTE; FindDefsAndUses uses IAEffect_* kinds.
  Needed shape (catalogued): FunctionName/`scan` reuse in the pointer-analysis walks; `if (!(r = f()))` forms;
  MarkRemovableLoopDefs' `(var = def->var)` (Pro7's `def->var` loses it); MarkUses' declaration order; a block-local
  `IRODef* d` in IRO_UseDef's call/indirect branch (Pro7 reuses `def`: 98.2); hoisted `CInt64 v; int powvalue;` in
  UpdateUseForOtherSelfAssignment. Original's shape: `int isBool = cond;` in IRO_AddTypeConversion (2.6 materializes
  it in ecx; the plain `if (cond)` gives 97.4), `Boolean isInt = cond; if (isInt || enum)` (materialized in al).
  Restored to Pro7's text where it matched: IRO_InitializeNonAssignmentOpArray / AssignmentFoldingFunctionArray
  (enumerators, not numbers), AllLoopDefUsesAreInSimpleLoopDefs (was IDA's inline bit test), GetAssigned
  (`CError_FATAL`, was `CError_Internal("IROUseDef.c", 2497u)` and a decompiler temporary), PropagateOtherSelfAssignments'
  `while`/label, `IS_TYPE_INT_OR_ENUM`, `NumUses` SInt32, UpdateUse's `if ((father = ...))` and Pro7's names.
  Fakematch removed: `int isInt; ... if ((Boolean)isInt || ...)`.
- Behaviour: checked against the disassembly where not Pro7's (the promotion block, AddTypeConversion's bool double
  ELOGNOT); 0x11 in the integer test is Pro7's IT_17 (commented). No bugs found.
- Names: Pro7's; IRO_AddTypeConversion is Wii's. `var_30` is Pro7's own placeholder local. Invented: none.
- Types: Pro7's except the documented member names in IROUseDef.h.
- Data: links; statics static; .bss 0x5e00a0..0x5e01fc; no bindings into it.
- Header: declares only what IrOptimizer, IroVars, IroTransform, IroFlowgraph, IroExprRegeneration use.
- Headers relied on: IROUseDef.h (IRODef/IROUse members invented from Pro7's x18..x1D, TODO-marked); IroVars.h
  (VarRecord: `x6` placeholder); IroFlowgraph.h (`x26` placeholder); IroUtil.h (`IRO_IsPow2(IROLinear*, int*)`: Pro7
  `SInt32*`, which is why `powvalue` is `int` here); objects.h (`Inline_IsObjectDataOrAbsolute` is an invented helper;
  every former Pro7 `Inline_IsObjectData` user now calls it and the DDATA-only one has no users, so 2.6's
  `Inline_IsObjectData` probably is the DDATA||DABSOLUTE test: rename it in objects.h and its 8 users);
  InlineAsmPPC.h (`IAEffect_0..4`, `IAEntryPoint.x0/x2/x4` Pro7 placeholders); CPrep.h (copts).
- Open: `0x11` is Pro7's `IT_17`, which types.h lacks (a header fix: add Pro7's enumerator); header items above.
- Header round H2 (October 16): types.h now has Pro7's `IT_17` (the unit still writes `0x11` with a comment: a unit edit, not made); IROUseDef.h's members, IroVars.h's `unused` (x6), IroFlowgraph.h's `unused` (x26) and InlineAsm.h's IAEffect_* are TODO names; `Inline_IsObjectDataOrAbsolute` is gone (Inline_IsObjectData has 2.6's body); IRO_IsPow2's `int*` is 2.6's (blocked, ledger). Resolved as TODO names.

### src/optimizer/IroRangePropagation.c: SOLID WITH TODOS
- Boundary: 5 rows 0x49d4f0..0x49ee70, Pro7's file reversed (its small statics inlined); .data starts at 0x5ae984, the
  first word after IroJump's last jump-table entry (a 4-aligned cut that relinks).
- Flags: deferred; `-sym on` justified (`-sym off`: 2/5).
- Functions: 5/5 exact. Departures: 2.6 change: `is_unsigned` (CParser) is called where Pro7 inlines IRO_IsUnsignedType
  (call target in the original). Needed shape (catalogued): FunctionName for Pro7's `proc` (with `proc`: 95.3),
  `analysend` assigned then `inner = analysend`, `if (resultList) for (scan = resultList ...)`, `if (!(record = ...))`
  in two places and `record = ...; if (record)` in one (each other spelling tried: 98.5, 99.9, 88.1).
  Restored: `ERange.type` is Pro7's `ERangeType` (was UInt8), `nd->range = NULL`, TRUE/FALSE, includes cut to Pro7's set.
- Behaviour: Pro7's; no bugs found.
- Names: Pro7's (`v11` is Pro7's local); member `range` is IroLinearForm.h's TODO for Pro7's x16.
- Types: Pro7's ERange/ERecord at 2-byte packing.
- Data: links; ERangeFirst/ERangeLast static; no bindings.
- Header: only IRO_RangePropagateInFNode (IrOptimizer uses it).
- Headers relied on: IroLinearForm.h, IroPointerAnalysis.h, IroUtil.h, IroVars.h, IroFlowgraph.h, IroMalloc.h,
  CInt64.h, CParser.h, CPrep.h (copts here, Pro7: CParser.h), objects.h, types.h: not reviewed as units.
- Open: header review only.
- Header round H2 (October 16): its optimizer headers are reviewed, with TODO names only (`range`); copts is CParser.h's. Resolved as TODO names.

### src/optimizer/IroDump.c: SOLID WITH TODOS
- Boundary: 8 rows 0x454a50..0x455850, Pro7's IroDump.c reversed. Fixed the .data cut: the unit starts at 0x5a9838
  (8-aligned, right after InstrSelection's last jump table), not 0x5a9a08; the 464 bytes were Pro7's dead
  IRO_SpellType's literals (`void` ... `class `, `freturns(`), which InstrSelection's split object had supplied. The
  dead IRO_SpellType is now Pro7's as written (the old "(" ")" body was a guess from the wrong cut). All of Pro7's dead
  dump functions are in the file; their literals are the unit's .data.
- Flags: deferred; `-sym` makes no difference (default `on`).
- Functions: 8/8 exact. Now Pro7's text; departures: 2.6 change: IRO_SetupDump names the log after the source file
  (`COS_FileGetPathName(name, &cparams.sourcefile)`, strip the extension, `.log`; Pro7 "OPT.LOG"). Needed shape
  (catalogued early returns): DumpLinearNode, IRO_DumpExprs, IRO_Dump (`if (!IRO_Log) { return; }` then
  `va_start`/`vfprintf`/`va_end`), IRO_DumpFlowgraph (two returns; `!IRO_Log || !DumpFile` gives 97.1). Restored:
  enumerators in IRO_InitializeNodeNamesArray (was `0x4d` and numbers), `va_start` for the hand-computed va_list,
  `u.ctch.linear` for BeginCatch, TRUE/FALSE.
- Behaviour: bug of ours fixed: IRO_SetupDump read `data13_005e7064`, a binding into the middle of `cparams`
  (cparams + 0x92 = `sourcefile`); it is `cparams.sourcefile` now (same address, real struct).
- Names: Pro7's; no invented names.
- Types: Pro7's.
- Data: links with the new cut; statics `nodenames`, `DumpFile` static; deleted the 2.6 bindings `_DumpFile`,
  `_nodenames` (only this unit's exact functions reach them) and `_data13_005e7064` (the cparams fragment).
- Header: trimmed by the adversary to what other units use (IRO_InitializeNodeNamesArray, IRO_DumpBits,
  IRO_DumpAfterPhase, IRO_DumpExprs, IRO_SetupDump, IRO_CleanupDump, IRO_Dump); the 11 others (Pro7's dead dump
  functions, IRO_DumpFlowgraph, IRO_NodeName) had no user outside; IroDump.c gained prototypes for the two used
  before their definitions (IRO_DumpFlowgraph, IRO_SpellType). Every function stays exact, every unit unchanged.
  Header round H2 (Optimizers family) put back Pro7's IRO_DumpIntTree, IRO_DumpFlowgraph, IRO_DumpNode and
  IRO_SpellType (Pro7's API with Wii publics) and dropped IroDump.c's two private prototypes.
- Headers relied on: CompilerTools.h (`COS_FileGetPathName(void*, CPrepFileInfo*, SInt32*)`: Pro7 `(char*, const FSSpec*,
  SInt32*)`, hence the `(CPrepFileInfo*)` cast on `&cparams.sourcefile`); CPrep.h (CompilerLinkerParamBlk definition);
  driver/CCompiler.h (cparams); IroFlowgraph.h (IRONode set names invented, TODO); IroVars.h, IroCSE.h (TODO member
  names); enode.h (`floatval.data.value`, `vector128`).
- Open: header items above.
- Header round H2 (October 16): COS_FileGetPathName has Pro7's `(char*, const FSSpec*, SInt32*)` (COSToolsCLT.h) and the cast is gone; IroDump.h is DONE; IroFlowgraph.h/IroVars.h/IroCSE.h keep TODO names. Resolved as TODO names.

### src/optimizer/CIRTransform.c: SOLID WITH TODOS
- Boundary: 3 rows 0x52ba70..0x52bcc0 (TransformOpAss, Cleanup, Setup: Pro7's order reversed); the rest of Pro7's file
  is dead and present (its `"__alloca"` literal and TransExpr's jump table are in the unit's .data); .bss 4 bytes
  (cirtrans_temps).
- Flags: deferred (reversed); `-sym` makes no difference.
- Functions: 3/3 exact, Pro7's text with Pro7's +5 assert offset. Departures: `cirtrans_rtfunc2/4/8` defined in 2.6's
  .data order (Pro7's order fails the relink at those bytes); Pro7's COMMONs `modulo_generated`, `bigswitch_generated`,
  `alloca_called` are not defined (defining them moves the COMMON tail: relink fails), `alloca_called` is declared
  `extern` for the dead TransExpr. Restored: `mfpointer.accessnode/mfpointer` and `nullcheck.*` union members (Pro7's;
  were `diadic.left/right`), TRUE/FALSE.
- Behaviour: Pro7's; no bugs.
- Names: Pro7's. `StrangeRuntimeFunction` (with its `unk` field) is Pro7's decompiler name for a type only dead code
  uses (never instantiated in Pro7 or here); now TODO-marked as invented.
- Types: Pro7's.
- Data: links; no bindings.
- Header: dropped the three unused COMMON externs; the adversary dropped `CIRTrans_Transform` too (dead, no user
  outside); the other three are used by CParser, CExpr and IroTransform.
- Headers relied on: enode.h (added Pro7's `mfpointer` struct to ENodeUnion); CExpr.h/CExpr2.h, CParser.h, CInit.h,
  CMachine.h, CScope.h, CDecl.h, CompilerTools.h: not reviewed.
- Open: header review only.

- Adversarial review: attacked every "original's shape" claim and the layout.py change. Held: IRO_AddTypeConversion's
  `int isBool` (Boolean 99.0, plain `if` and `IS_TYPE_INT` 97.4; `int isBool = cond;` and `isBool != 0` also exact),
  UpdateUse's `Boolean isInt` (int 99.5, plain 96.0, `<= IT_LONGDOUBLE` 99.0), the doubled `promoted = type;` in both
  promotion blocks (either removed: 99.5 / 99.8 / 75.7; a shared static or inline promotion helper 74-75%),
  IRO_DumpFlowgraph's two early returns (Pro7's `if (IRO_Log && DumpFile) {...}` and nesting 95.0, `||` 97.1).
  Exactness is byte-and-reference strict, so behaviour equals the binary; statement-by-statement IDA checks were not repeated.
  Broke: IroDump.h declared 11 functions no other unit uses and CIRTransform.h the dead CIRTrans_Transform (bar item
  8; fixed), and `StrangeRuntimeFunction`/`unk` lacked a TODO (fixed). The IroRangePropagation `.data` start
  (0x5ae984, 4-aligned) is right: its object's first .data section is a 4-aligned jump table (no literal section
  first), so the "8-aligned start" rule does not apply. layout.py: principled (a function is absent when a Matching
  object defines it but no row of that source has its name, the same rule as relink.py's dead()), and every moved
  boundary checked against the original's bytes: NewHand's .data is 0x5a8fb0..0x5a9004 (its literals; the 4 zero bytes
  at 0x5a8fac are padding, unreferenced) and COptimizer's starts at 0x5bcfc8 with `bitmasks` (8-aligned; old starts
  were align:4 and wrong); newop ends 0x5a4f5c (its bad_alloc vtable at 0x5a4f4c), IroLinearForm 0x5ac534,
  PCodeListing 0x5bdc64, Scheduler 0x5bdc84, CClass 0x5c0afc: each is the last byte of its data before padding up to
  the next 8-aligned object, as elsewhere in splits.txt. One inaccuracy: in NewHand and newop the skipped sections are
  C++ vtables, not jump tables (COMDAT copies of ExceptionX86's and newop's rows, which the linker took from those
  objects); the outcome is right, the docstring said otherwise (reworded). The predicate duplicates relink.py's
  `dead()`; a shared helper in tools/mw would remove the copy. After the changes: 4020/4020 exact (nothing lost),
  layout unchanged, all five units (and the six whose ranges moved) link.
- Header round H2 (October 16): CIRTransform.h is DONE; enode.h, CExpr.h (CExpr2.h merged), CParser.h, CInit.h, CScope.h, CDecl.h are reviewed. Resolved; the unit's own TODO name stays.

### src/optimizer/IroUtil.c: SOLID WITH TODOS
- Boundary: 55 rows 0x468130-0x46a5d0, every one Wii module IroUtil (IroType for is_CPtypeequal/UnsignedType/TypesEqual: a later Wii split), Pro7 iroutil.c order reversed; asserts "IroUtil.c" at Pro7 lines +9 from IRO_Cut on (2.6's FindFirst wrapper and WalkInts cases come before). Pro7's dead IRO_IsVector128Constant, SignedType, FindPreced*, ClipExprTree, WalkTreeToPropagateFlags, DeleteCommaNode, RemoveCommaNodeFromIR kept. Pro7's dead IRO_BuildList is left out: the only thing it writes is IroLoop's IRO_InitLList, which is no COMMON in 2.6, so 2.6 has neither. .bss 0x5e0220-0x5e0234 is the five statics in reverse order. .data starts at 0x5ab89c, right after CError's (0x5ab4b0 + its object's 1004 bytes): IroUtil's object .data is 1420 bytes, its first section 4-aligned. relink's ".data is 1424 bytes, the unit's 1420" is its estimate summing the raw sections' alignment from 0: section 21 (100 bytes, `@988`/`@1065`) is 8-aligned and gets 4 bytes of padding from base 0 that a base of 4 mod 8 does not need. The unit links.
- Flags: pro53->pro6 `-O4,p -inline auto,deferred -sym on`. Deferred is right: the rows descend against Pro7's order.
- Functions: 55/55 exact. Rebuilt from Pro7's text. Departures:
  - IRO_IsPow2: 2.6 change. It starts at value 2, i 1 (disasm `mov eax,2; mov edx,1`), so 1 no longer counts. Its counter is unsigned (`jb`). It reads intval.hi directly (Pro7's CTool_EndianReadWord32 leaves a `lea`). The old stand-in body (do/while, `(node = ...)` in the condition) is gone. The `int*` parameter stays: the callers' locals are `int`.
  - FindFirst/IRO_FindFirst: 2.6 change. IRO_FindFirst calls IRO_RenumberInts and then the static (disasm 0x469c50). 2.6 also fixes Pro7's `args3.b` typo to `args3.a` in the ternary case (disasm).
  - WalkInts: 2.6 adds `case Nop/18/19/End` and returns early. Both are needed shapes: the jump table starts at 0, and there is a separate epilogue. Pro7's `ctch.linear` is restored (the code used `u.monadic`). DuplicateExpr's `case IROLinearNop` is a needed shape for the same jump-table reason.
  - HasSideEffect: the Op3Arg case is back to Pro7's else-if chain. The Op2Arg case keeps `tmp = left; if (!tmp) tmp = right;` because Pro7's spelling gives 97.7.
  - Needed shapes, natural spelling tried: MoveExpression's early return; `(nodeflags & ENODE_FLAG_80) == 0`; TypesEqual's TypeBitfield locals (Pro7 75%); `!!is_typeequal` twice (the original materialises both negations; `!= 0` and if/return do not match).
  - ReplaceReferenceWithNode is Pro7's verbatim, unused `IROList list` included.
  - Original's shape: IRO_UnsignedType tests a `TypeIntegral* t = TYPE_INTEGRAL(type)` copy and returns `type` (catalogued in AGENTS.md). Pro7's body, a `Type*` copy and `return TYPE(t)` give 58.6%. A typed local is plausibly Metrowerks'; the comparisons are now plain `t == &stbool` (the `TYPE(t) == TYPE(&st...)` casts were noise, also exact). No longer a TODO.
- Behaviour: checked against IDA for IsPow2, FindFirst/IRO_FindFirst, is_CPtypeequal and WalkExcActions (cases and operand slots 0/1/3 and 0/2 = Pro7's members). Bug in the original: none. Bug of ours fixed: none (the code was exact). Magic numbers replaced by names: `nodeflags & 3` -> ENODE_FLAG_QUALS, 0x8000 -> IROLF_8000, case 18/19 -> IROLinear18/19, WalkExcActions' 1..17 -> EAT_*.
- Names: rows per names-2.6.md/Wii/Pro7. Invented: the static `FindFirst` (TODO; Wii's procedure there has no name).
- Types: Pro7's, WalkExcActions' `ExceptionAction` included (main's front-end core merge replaced the CException stand-in).
- Data: 4 stale or redundant 2.6 bindings to this unit's statics deleted: `_ExprStart`, `_IRO_LastUserBreakTick`, and the 1.x names `_linear_range_end` (= ExprEnd) and `_move_expr_before_node` (= IRO_Node). Only IroUtil functions reference those addresses. The COMMONs (FunctionName, IRO_IsLeafFunction...) are defined here, as in Pro7.
- Header: IroUtil.h no longer declares 9 functions no other unit uses (IsVector128Constant, SignedType, FindPrecedAfter, FindPreced, WalkTreeToPropagateFlags, Cut, ClipExprTree, DeleteCommaNode, RemoveCommaNodeFromIR). IROAddrRecord.x16 is a Pro7 placeholder that nothing reads (TODO). IROElmList.element stays `IROLinear*`: every user dereferences it. IRO_MakeLong's 2.6 shape is noted there.
- Headers relied on:
  - Exceptions.h (WalkExcActions): Pro7's `ExceptionAction` (`prev`, `type`, kind-specific union members such as `destroy_partial_array.arraypointer/arraycounter/element_size`). (At review time this was the gc1 stand-in `struct CException` with `next`, `kind` and `operands[n].object`; main's front-end core merge replaced it, and the replay uses Pro7's members.)
  - common.h: `Float` is `{FloatStorage data}` (Pro7 `{double value}`), hence `floatval.data.value`.
  - enode.h: `vector128` (Pro7 `vector128val`), `bits`, `unk0e`.
  - IroLinearForm.h: IROLinear18/19, `ctch.x4/x8` placeholders; invented `callee`, `cond`, `range`, `evaluated`.
  - CMachine.h: CMach_FloatIsZero/One/NegOne take `double` (Pro7 `Float`).
  - driver/CWPluginsPrivate.h, CCompiler.h: CWUserBreak, cparams.
- Open: the invented static name `FindFirst` and IROAddrRecord.x16 (TODO).
- Adversarial review: attacked the UnsignedType workaround (natural `t == &stX` comparisons also match and replaced the casts; reclassified original's shape), `!!is_typeequal` (a common idiom: original's shape, held), the boundary (Wii modules per row: only is_CPtypeequal/UnsignedType/TypesEqual are Wii IroType, Pro7 order holds), the header (every declaration has a user outside), and the .data note (the first reviewer's CError-jump-table reading was wrong: it is relink's alignment estimate; the data links). Nothing lost in 2.0/2.5/2.6/2.7.

### src/optimizer/IroVars.c: SOLID WITH TODOS
- Boundary: 23 rows 0x461090-0x463710 = Pro7 irovars.c in reverse; asserts "IroVars.c" at Pro7 lines -1. DisableEntries and ObjectIsArg are inlined. .bss 0x5e0200-0x5e020c = TheBaseTerm, LastIndirectRecord, FirstIndirectRecord (reverse declaration order). Pro7 defines the COMMONs IRO_VarTerms, IRO_IsModifyOp and IRO_IsAssignOp here; no source defined them, and they are now defined here (relink fine).
- Flags: `-O4,p -inline auto,deferred -sym on`, descending rows.
- Functions: 23/23 exact. The old text was mostly 1.x/IDA-style (magic-number tables, `entry`/`create`/`mode`, a stand-in IndirectRecord with offset comments). It is now Pro7's text; 12 functions match verbatim.
  - 2.6 changes: Inline_IsObjectDataOrAbsolute (DABSOLUTE) in FindAllVars and UpdateVars. ResetVars is external, because 2.6's IRO_Optimizer calls it (Pro7 static).
  - Needed shapes, each tried against Pro7's spelling:
    - CheckAddress and CheckAddressConsistency return `int` (`xor eax,eax`).
    - An `Object* obj` local in FindAllVars, UpdateVars and CheckAddress (1.x's shape, gc1 record_monadic_var_part_use).
    - A TypeBitfield local in CheckAddressConsistency.
    - The catalogued pointer-analysis walk (`if (resultList)`, scan reused) plus declaration order in GetKillsByIndirectAssignment. GetKillsByFunctionCall frees through Pro7's unused `list`.
    - CheckInit: a second loop counter `j` and the nested ifs.
    - RewriteIndDec's declaration order.
    - FunctionCallMight...: early `break` and function-scope `next`s. IndirectMight...: parameter reused, `next` at function scope.
    - ScalarizeClassDataMembers: function-scope lists, the `argobj` copy, and Pro7's DisableAddressedEntries written in place walking with `irec`. The helper was then deleted (the inlined call does not match).
  - Workaround (TODO, fakematch-class): CheckAddress's `(hi = ...intval.hi) == 0`, an assignment nothing reads; no story but register placement (Pro7 and gc1 both test `.hi == 0` in memory; 2.6 loads it into eax and tests it). Tried: Pro7's spelling, `!X.hi`, int/long/unsigned casts, `0 == X.hi`, Pro7's CTool_EndianReadWord32 (repo inline and Pro7's macro form), a CInt64/CInt64*/ENode*/IROLinear* local. An inline `SInt32 f(CInt64* n) { return n->hi; }` also matches (a UInt32 return does not): 2.6 may test through such a helper, which nothing names. IroLoop's dead `div` store is probably the same construct.
- Behaviour: magic numbers replaced: the 77-entry tables are Pro7's ENodeType names (EINITTRYCATCH is skipped in both, same stores), IAEffect types, EAT_* cases, ERR_VARIABLE_NOT_INITIALIZED_BEFORE_BEING_USED. The asm effects flags now read `writesMemory`/`branchWithLink` (Pro7 x1/x4). Bug in the original: none. Bug of ours: none.
- Names: Pro7's throughout. Invented: none here. IndirectRecord is Pro7's, with placeholder `x26`, which only IRO_ScalarizeClassDataMembers writes.
- Types: Pro7's IndirectRecord/IndirectRecordMatch; the stand-in `alignmentPadding` and `IROElmList* uses` are gone.
- Data: bindings `_TheBaseTerm`, `_IRO_FirstIndirectRecord`, `_IRO_LastIndirectRecord` deleted (statics only IroVars functions reach). Links.
- Header: IroVars.h no longer declares IRO_LastVar (no other user). VarRecord keeps Pro7 placeholder `x6` (TODO) and invented `inductionState`, `noregister`, `usedAtCall`, `bitfieldType`, `bitFieldReplacement` (all TODO).
- Headers relied on:
  - Exceptions.h: Pro7's `ExceptionAction` (as above).
  - objects.h: Inline_IsObjectDataOrAbsolute is an invented name (TODO added by the adversarial review).
  - IroFlowgraph.h: in/gen/reachable (invented names, TODO), `x26` placeholder.
  - InlineAsmPPC.h: IAEffect_0..4 placeholders.
  - Registers.h: VarInfo.
  - IroPointerAnalysis.h.
- Open: the CheckAddress `hi` workaround (TODO).
- Adversarial review: attacked the `hi` dead store (11 more spellings; only a dead local or an invented signed inline accessor match: kept as TODO, now classed fakematch-class), the hand-inlined DisableAddressedEntries (Pro7's helper restored before/after, static and static inline: all 99.2, so writing it out holds), the boundary (Wii puts AddElmToList, DecomposeAddressExpression*, the IsAssignOp/IsModifyOp initialisers in IroUtil and three rows in IroScalarization/IroDependsKills: later Wii splits; Pro7's irovars.c order and the "IroVars.c" asserts hold), the header. objects.h: Inline_IsObjectDataOrAbsolute now carries its invented-name TODO.

### src/optimizer/IroCSE.c: SOLID WITH TODOS
- Boundary: 18 rows 0x46a5d0-0x46c0a0 = Pro7 irocse.c reversed; asserts "IroCSE.c". .bss 0x5e0238 = HasVectorOperand. Pro7's dead IRO_GenerateTopLevelExprsForSubableOperands (Wii's IroCSE module still has it) and its statics are back at the file's end; they have no data. Pro7's `BitVector* IRO_Depends` COMMON is defined here again (no source defined it).
- Flags: as above, deferred (descending).
- Functions: 18/18 exact. AddExpression, MakeReplacementEmbedded, CommonSubs, CheckCommonSub, ReplaceCommonSub and five more are Pro7's verbatim.
  - MoveCommonSub was rewritten from Pro7. It needs 2.6's declaration order and `scan` loaded before `sz2 = 0` (Pro7's SInt32 types restored).
  - ComputeAvail was gc1 text; it is now Pro7's, with a separate walker in the do-loop (reusing `node` swaps registers).
  - Catalogued walks (`if (resultList)` + scan reuse) and declaration orders in GetDependsOf*, GetExprKillsBy*.
  - IRO_DependsOn returns early. IRO_GetExprKills declares its locals at function scope. `!HasVectorOperand`, because Pro7's `== 0` breaks the inlining caller.
  - All of these were tried against Pro7's spelling.
- Behaviour: no changes in behaviour. Bug in the original: none. Bug of ours: none.
- Names: Pro7's. IROExpr's invented `state`, `temp`, `use`, `factor`, `var`, `expr` are TODO-marked in IroCSE.h.
- Types: Pro7's.
- Data: binding `_HasVectorOperand` deleted (static, only IroCSE functions reach it). Links.
- Header: IroCSE.h declares only what other units use.
- Headers relied on:
  - IroFlowgraph.h, IROUseDef.h (`reachingDefCount` invented, TODO).
  - IroSubable.h, IroPointerAnalysis.h, BitVector.h.
  - IroLinearForm.h (as above).
- Open: invented field names only (TODO).
- Adversarial review: checked the header (all declarations used elsewhere), the dead GenerateTopLevel functions (no data, Wii's IroCSE has them), the .data/.bss cut (0x5abe28, 8-aligned; .bss 1 byte = HasVectorOperand). Held; nothing changed.

### src/optimizer/IroEval.c: SOLID WITH TODOS
- Boundary: 14 rows 0x455850-0x457500, Pro7 iroeval.c reversed. Pro7's static helpers (EEquConst, VEquConst, IsConsecutive, findLabel, checkLabel, checkExpr...) are present and inlined. .bss = IsAssociativeENodeType (static).
- Flags: as above, deferred.
- Functions: 14/14 exact. IRO_ConstantFolding is now Pro7's text. The stand-ins `union Val`, `Val_SetULong` and the `LongLongDiadic` cast of CMach_CalcFloatDiadic were fakes: Pro7's CInt64/Float body is exact with the repo's `floatval.data.value`. The magic 0x168..0x16b are now TK_LOGICAL_EQ/NE, TK_LESS_EQUAL and TK_GREATER_EQUAL.
  - EvaluateConditionals: 2.6 change. 2.6's switch cases are single values, `CInt64_Equal(swcase->min, val)` where Pro7 tests a range (SwitchCase has no `max`).
  - Needed shapes: DoReducible1's declaration order (the separate `scan2` was not needed and is gone). ReducePattern1's `fnode` set before `count` and `info = info->next`. EvaluateDefinitePointers' `obj` local (1.x shape). TruncateBitfieldValueToType's TypeBitfield* parameter (catalogued).
  - Original's shape (maintainer's rule: plausible, not obscene): EvaluateConditionals' `(!isZero != 0)`, a redundant normalisation the disassembly shows (`!isZero` into edx, then `test edx; setne cl`). Tried and not matching: Pro7's `(isZero == 0)`, `!isZero`, `!!(!isZero)`, `!(isZero != 0)`, `(Boolean)`/`(int)` casts, swapped operands, `isZero != (type == If)`, `== TRUE`, an `int isZero`, a `!CInt64_IsZero` local, ternaries; `!= FALSE` matches equally. No longer a TODO.
- Behaviour: Bug in the original: none. Bug of ours: the stale comment "x0 cleared, never read" was wrong (it is the reduction mark).
- Names: Pro7's; ReduceInfo `mark`, `state` and `var` are invented (TODO; Pro7 x0/x4/x8). IsAssociativeENodeType's stale "global while lifted" comment removed.
- Types: Pro7's.
- Data: links.
- Header: IroEval.h: every declaration used. The 2.6 TypeBitfield* parameter is noted there.
- Headers relied on:
  - common.h: Float stand-in.
  - CMachine.h: CMach_CalcFloatDiadicBool takes `volatile double` (a stand-in; Pro7 `Float`); CMach_CalcFloatMonadic takes `double`.
  - Switch.h: 2.6's SwitchCase without `max`.
  - CInt64.h, CParser.h (TK_*).
- Open: invented ReduceInfo field names (TODO); CMachine.h's `volatile double` stand-in (header).
- Adversarial review: attacked `(!isZero != 0)` (16 spellings; reclassified original's shape under the maintainer's rule, comment kept with what was tried), the .bss (77 bytes = IsAssociativeENodeType[MAXEXPR]). Held.

### src/backend/ObjGen_PPC_EABI.c: SOLID WITH TODOS
- Boundary: .text 0x4b26a0-0x4b5ae0, .data 0x5b0748, .bss 0x5e03c0 (both 8-aligned). It relinks byte for byte (`relink.py --only`). Wii's ObjGen_PPC_EABI module holds the same functions in the same reverse order: 30 by public name, plus unnamed statics at the places of create_main_file_object, emit_relocation, emit_object_data/allocate/report/select and SymContainedVars. Wii's leading CodeGen_*PrecompilerData, is_big_endian, get_/create_pseudo_section_info and init_debug_sections are later additions (I read the disassembly of the last three: they are other functions, not renames of ours). Every assert says "ObjGen_PPC_EABI.c", and the asserts' lines fall as the addresses rise (2528 down to 422). No Pro7 file exists (ObjGenMachO.c is only an analogue), so no dead functions could be shown missing.
- Flags: pro6 -O4,p, `-inline auto,deferred` (the asserts descend: 51/51; non-deferred gives 48/51), -sym on (-sym off gives 32/51). The `#pragma pack(2)` at the end of the file is needed: without it .bss is 65 bytes where the unit's is 63.
- Functions: 51/51 exact. Departures from the plain spelling:
  - Natural spellings that also matched, and are now in the source:
    - Pro7's `declaredata` restored as a static, shared by DeclareData and ReadOnlyData. create_main_file_object now calls ObjGen_DeclareData (its "dead" `if (copts.filesyminfo...)` block was that call inlined).
    - Decompiler temporaries removed: the `unit` locals and reloads in ObjGen_Finish and create_main_file_object, `record`/`target`/`node` copies, `(UInt8)x != 0`.
    - The range casts are gone: `(UInt8)(dt - 3) <= 2` and `(UInt8)(dt - DVFUNC) > 1` became `!(dt == DFUNC || dt == DVFUNC || dt == DINLINEFUNC)`. `(UInt8)(v - 6) <= 2` became `v == AM_SDATA || ...` on an `AddressingMode` local. base_register switches on `(AddressingMode)`.
    - The `(limit = ...) != 0` assignments in PPC_EABI_SetSection are gone.
    - The MWCATS functions were rewritten on real types, with no dead `position = location.offset;` and no trailing `return;`.
    - PragmaPush/Pop use typed tables and `for (i = 0, s = list; s; s = s->next, i++)`.
    - GetSourceFileName reads `context` as an integer uid.
    - The invented `galloc_clear` is gone.
    - Each relocation record is an `Elf32_Rela` (ELF_Endian.h); declareinit is Pro7-style.
  - Needed shapes, each tried both ways:
    - GetSourceFileName's `uid` copy declared after `file`.
    - Finish_Pragma_Section reuses `symbolLink` for the second link.
    - The SetDataSectionOptions/SetCodeSectionOption inline helpers: written out, Finish_Pragma_Section's registers differ.
    - DeclareSwitchTable's two-step addend.
    - ObjGen_Setup's double fixups through a `UInt32*` (2.6 goes through a pointer; plain stores do not match).
    - SourceRef's signed copies of its UInt32 parameters (SInt32 parameters load both in the prologue).
  - Original's shape:
    - SymContainedVars counts argument GPRs into `gpr` and never reads it. The binary does all of that arithmetic.
    - base_register's default case calls ObjGen_GetAddressing a second time before CError_FATAL (it is in the binary).
  - Fakematches (TODO with what was tried):
    - resolve_object_pseudo_section's `*(volatile UInt16*)&prev->sectionIndex`.
    - emit_relocation's `void* section`, cast back (typed, the load goes to ecx where 2.6 uses eax).
- Behaviour: checked against IDA for the rewritten functions (MWCATS, create_main_file_object, resolve_object_pseudo_section, SymContainedVars, base_register/is_16bitdata). Error ids are now the PPCErrorStr names: 128, 131, 142, 143, 145, 157.
  - Real misreadings fixed:
    - `ObjGenSection.context` is the source file's uid, not a section pointer.
    - copts' `bssSection`/`smallBSSSection`/`smallDataSection` are the const/sconst/sdata sections.
    - ObjGenSection's `alignment`/`maximumSize` are the Shdr's sh_flags/sh_addralign.
    - SectionSymbolAttributes' `kind`/`alignment` are the data/code addressing modes.
    - The statics' names were swapped between sections.
  - Original's quirks, kept:
    - select_object_section passes isConst=TRUE for functions in ObjGen_BestSection.
    - PPC_EABI_SetSection's allowSmall test is true whenever isConst.
  - No bugs of ours were found in the code.
- Names: functions are Wii's where Wii has them. These are 2.6 statics with no public name (TODO invented): create_main_file_object, resolve_object_pseudo_section, pseudo_section_index (global: BE_elf calls it), emit_relocation, emit_object_data_and_relocations, allocate_object_storage, report_section_permission_conflict, select_object_section. SymContainedVars, declaredata, last_base_offset and last_function_section are Pro7's names. COMMONs: AllFiles, CurFile and objcodesize are Wii's. `ok_to_compress` (Wii's COMMON, defined by no source before) is now defined here. Invented (TODO):
  - the default_*_section statics, uid_counter, section_table_dirty, saved_section_table, pending_*, object_*_size, last_line_offset;
  - data_00580d80 and data_00580da4 (only cleared; every version's bindings carry those names, so they were left);
  - the local types and enums (AddressingMode AM_*, SectionKind_*, InterruptRecord_*, MWCATS* types, CNameNode, BufferUpdate, ExtabIndexEntry);
  - the OBJGEN_LOWER stand-in for MSL's inline tolower.
- Types:
  - Stand-ins removed: SerializedFormat/FormatLink/ValueList/Location/ShortHeader/WideHeader. Those were a misread of SectionAttributeNode: AddExit reads `attributes->tail`.
  - SectionAttributeNode's placeholder fields f08/f0c/f10/f15 are now exits/lastExit/exitCount/flags, at pack(2), 0x16 bytes.
  - RelocationRecord/EABIRelocationRecord became the existing `Elf32_Rela` (ELF_Endian.h; BE_elf's WriteDebugRelocation too).
  - `SInt32* sectionHeaderTable` became `SectionSymbolAttributes**`.
  - R_PPC_* names for the relocation kinds.
  - The BE_ELF_FIND_SECTION alias macro was deleted (its only user was this unit).
  - Left: the `sectionData` union has one member.
  - Comment fixed (adversary): Finish_Pragma_Section's `& ~0x40000004` drops SHF_EXECINSTR as well as bit 30 for the linked (.bss-like) section; the old comment named only bit 30.
- Data: .data/.bss byte-identical (relink). The statics are `static` with no bindings. The 2-byte .bss packing is shown to be needed. The static functions are now `static` (all exact, still links).
- Header: removed select_object_section (a static) and the stand-in types. AddressingMode, CGList and the InterruptRecord_*/R_PPC_*/SectionKind_* enums have no user outside the unit; they stay because they describe header structs' fields (SectionKind_* belongs with BE_elf, which still writes bare numbers). Added the AddressingMode, InterruptRecord_*, R_PPC_* and SectionKind_* enums, and the ObjGenSection field comments. Everything left is used by other units (grep). `InlineAsmPPC.h` stays only because includers rely on it transitively (removing it breaks FuncLevelAsmPPC, InlineAsm ...).
- Headers relied on:
  - CPrep.h (COpts, copts, cparamblkptr): `int sdata2_threshold` beside `SInt32 sdata_threshold`; `CTool_GetPathName(CPrepFileInfo*)` is declared here (Pro7: `const FSSpec*` in CompilerTools.h), hence ObjGen_Finish's cast of `&cparamblkptr->sourcefile`. (The old placeholders pad15/f26/f27/f25 are now main's `no_comment_records`, `rel109_offset` ...; `CompilerLinkerParamBlk.objectdata` is now a `Handle`.) Fixed here: `sectionHeaderTable`'s type (`SectionSymbolAttributes**`) and the section members' comments; the members' names `constSection`/`sdataSection`/`sconstSection` are main's, the same the review chose.
  - common.h: `Float` wraps a union (`floatconst->data.value`) where Pro7 has `double value`.
  - BE_elf.h (SectionRec, ELFfile, InitElfSection ...): SectionRec's far_reloc/near_reloc are data_mode/code_mode; ElfFile's member names are invented; section kinds are bare numbers (the SectionKind_* enum belongs there). (BE_elf.c's `copts.fd5` is now main's `copts.dwarf_relocations`.)
  - BE_symbol.h/.c: (the stale comment that select_object_section calls BE_symbol_CreateSectionSymbol is gone in main's BE_symbol review.)
  - CodeGen.c's parse_section_pragma writes the AM_* values as numbers.
  - PCodeAssembly.h's MWReloc kinds are numbers.
  - include/libc/ctype.h declares tolower out of line, while the compiler's units inline MSL's (private copies here, in CompilerTools and in CPrep).
  - Not reviewed: CompilerTools.h, CFunc.h, CParser.h, CMangler.h, CInit.h, StackFrame.h, Switch.h, TOC.h, DWARF.h, Operands.h, PPCError.h, objects.h, types.h.
- Open:
  - two fakematches (volatile, void*);
  - invented names with no source;
  - two unnamed only-cleared statics;
  - the header problems above (hence not SOLID).
- Adversarial review: attacked the renames by meaning (context, elfFlags/elfAlignment, dataMode/codeMode, the const/sconst/sdata sections and their default_* statics), the volatile fakematch, the dead `gpr` count and the header. Held: `context` is read as an int at +0x64 and compared with CNameNode.uid at +8 (GetSourceFileName, CloneSection); nameoff..entrySize are the Elf32_Shdr BackEnd_WriteElf appends (40 bytes from nameoff), so elfFlags is sh_flags and elfAlignment sh_addralign; CodeGen's section pragma writes far_reloc 1/2/5 for data_mode and near_reloc 10/11/12 for code_mode, and ObjGen_GetAddressing returns codeMode for functions; Initialize_Input's type bits (.rodata 4 const, .sdata 8, .sdata2 0x10 sconst) and PPC_EABI_SetSection's sdata2_threshold test agree with the section names; the `gpr` arithmetic is in the binary. The volatile: plain, reversed, `(int)`, the assignment passed to pseudo_section_index, and SInt16 sectionIndex all fail (83-96%); stays a fakematch. Broke: one misleading comment (fixed). Not found: names for data_00580d80/data_00580da4 (only cleared); ObjGen's object_storage_size/object_data_size are probably `objudatasize`/`objidatasize` like the importer's COMMONs and Wii's `objcodesize` (not renamed: statics, no evidence beyond analogy).

### src/driver/ELF_Lib_Import.c: SOLID WITH TODOS
- Boundary: .text 0x41fa60-0x4209d0, .data 0x5a2250, .bss 0x5de9a0 (8-aligned). It relinks byte for byte. Wii's modules split it into ELF_Lib_Import (13) and EPPC_LinkUtilities (8), interleaved in one 2.6 range, so it is one 2.6 object (docs/names-2.6.md). No Pro7 file exists; the 2.6 functions setup_param_block and store_compile_results are Pro7's CCompiler.c ones, adapted.
- Flags: pro6 -O4,p, -sym on, not deferred (ascending; deferred gives 15/21; -sym off gives 18/21).
  - New finding: the file is -O4,p up to store_compile_results and `#pragma optimization_level 2` from ELFImport_main to the end. One pragma gives 21/21. Without it -O4,p gives 14/21 and -O2,p for the whole file 15/21. Optimization levels 1 and 3, `global_optimizer off`, `opt_lifetimes off` and `optimize_for_size` at that point all fail.
  - That pragma replaces 14 per-function `optimization_level 2`/`scheduling off`/`sym on` pragmas. Those were stand-ins: the scheduling and sym ones did nothing. It is a TODO: the original's spelling of the setting is unknown. Two source files are ruled out (adversary: a literal and a static are shared across the cut).
- Functions: 21/21 exact. Departures:
  - Natural spellings now in:
    - IsELF's local `char magic[4] = {0x7F,'E','L','F'}` (the 4-byte .data template is its initializer, not a static int).
    - IsMWLibraryFile's `SInt32 header[2]` (no ReadValue union).
    - Inspect_ELF_Header written out (the invented ReportModeError/ReportInvalidMode/ReportUnsupportedKind inline helpers are gone).
    - `isdigit` from <ctype.h> (no private archive_isdigit), `setjmp` (not `_Setjmp`).
    - memFullErr, 60 and 48 for the magic numbers, TRUE/FALSE.
    - The prefs read through PProject/PLinker.
  - Needed shape: store_compile_results reuses one variable for the handle and the results, and ends `if (r) return r; return r;` (the Pro7-like forms are 97.8% and 90%).
  - Original's shape (TODO comment): `nonNativeByteOrder = (!copts.littleendian) != 0` (macros, probably).
  - Needed shape (adversary): ImportObjectLibrary's `COS_NewOSHandle(length = 8)`, and `length` is now the size of all four uses (COS_NewHandle, strncpy, memcpy), which also matches exactly; the dead store is gone. 2.6 loads the 8 into a register for the first call only; a separate `length = 8;`, a plain 8, casts, `SInt32`/`size_t` locals, `8UL`, `strlen("!<arch>\n")` and COS_NewOSHandle declared with SInt32/int/UInt32/short all give 98.8%.
- Behaviour: checked against the original's code. Quirks of the original, kept: IsMWLibraryFile's "mw" test returns 0 either way; ELFImport_main clears only the first 264 bytes of copts (`sizeof(copts)` does not match); the import adds section sizes by SHF_EXECINSTR, SHF_ALLOC+PROGBITS and SHF_ALLOC+NOBITS.
- Names: Wii's, each checked against COMMON hash order: linkerexit (was file_input_jmpbuf), linkererrors/linkerwarnings (limited_diagnostic_count/data_00588228), Imported_Obj_Codesize (accumulated_section_size), Target64 (data_00588517), linkinfo (data_0058770c). Wii's Import_objudatasize/Import_objidatasize fail the hash order at those two slots, but so did the repo's names; `objudatasize` (hash 346, slot 343..365) and `objidatasize` (3419, slot 3392..3452) fit, and they are what ImportObjectLibrary returns as udatasize/idatasize: renamed (adversary; every version's bindings by rename.py). Invented (TODO): mwmagic, next_break_check, FileInputNode, LinkInfo and its members, LibImportCU's later fields.
- Types:
  - ConfigurationBlock60/116 are PProject/PLinker (pref_structs.h; endian at 0x2c, unknown05).
  - `char data_0058770c[588]` + `char data_00587958` were one 0x598-byte COMMON (ELFImport_main's memset of 1432 bytes covers both). It is now `LinkInfo linkinfo` with `nowarnings` at 0x290; the fragment's binding was deleted in every version.
  - LibImportCU embeds CWObjectData (no pad0c/pad20) and has CompilerLinkerParamBlk's member names. It cannot be CompilerLinkerParamBlk itself: the importer's struct is 0x10c bytes, 8 more than the compiler's (.bss 268 vs 276).
  - FileInputNode: lost its explicit padding.
  - The local `typedef char** Handle` was removed (main had removed it too).
- Data: byte-identical (relink); the statics are `static` in 2.6's order.
- Header: now only what other units use: linkerexit, linkinfo/LinkInfo, ReportLinkerError/Warning, MyAlert, OSAlert (LinkErrorsPPC_EABI.c), ELFImport_main (libimp-eabi-ppc.c). Everything else moved to the file top or into prototypes.
- Headers relied on:
  - DropInCompilerLinker.h, CWSecretPluginCallbacks.h, CWPluginsPrivate.h: now the SDK's types (`SInt32*` file number, `CWObjectData*`, `Handle`/`CWMemHandle*`); the only cast left is store_compile_results' `(Handle)result` (the param block's `objectdata` is kept an `SInt32` there).
  - pref_structs.h: PLinker.unknown05 and PProject.unknown* placeholders.
  - CompilerTools.h: declares LinkErrorsPPC_EABI's functions and counters (`linkererrors`, `linkerwarnings`); `COS_NewOSHandle(SInt32)` now, as Pro7 (ImportObjectLibrary is still exact with it).
  - Memory.h: GetHandleSize takes a `Handle` now (the casts are gone).
  - CPrep.h (copts, CWObjectData): as above.
  - Not reviewed: ELF_Endian.h (Elf32_Ehdr and Elf32_Shdr are fine for this use; EI_DATA/EI_CLASS are `e_ident[5]`/`e_ident[4]`), Files.h, oslib.h, StringUtils.h, CError.h.
- Open: the optimization_level pragma's origin (one object either way, below); the LinkInfo (`unknown000`/`unknown291` placeholders) and LibImportCU (`pad_e9`, `pad_fb`, `browseOptions` as bytes) guesses; the header problems above.
- Adversarial review: attacked the pragma (two objects?), the `length = 8` store and the COMMON names. Held: one object. "!<arch>\n" is in .data once (0x5a2254) and both ImportObjectLibrary (before the pragma) and IsMWLibraryFile (after) push that one literal; the static libimp_cu is used on both sides (setup_param_block writes it, MyAlert reads it), and next_break_check (used only after) sits in the same .bss block below it. Separate objects would each have their own literal and could not share a static, so the cut is a pragma (or an equivalent setting) in one file. Wii's split into ELF_Lib_Import and EPPC_LinkUtilities is later. Broke: the `length` dead store (a natural form with `length` used matches: the fakematch is gone) and the names of two COMMONs (now objudatasize/objidatasize, by hash order). Checked: linkererrors, linkerwarnings, Target64 and Imported_Obj_Codesize fit their hash neighbours.

### src/backend/DWARF.c: SOLID WITH TODOS
- Boundary: 0x547ad0-0x54cab0, 38 rows with no gaps (the last ends at 0x54caa7, and CExprConvMatch starts at 0x54cab0). Wii's DWARF module has the same functions in the same order: its 14 publics match 2.6's 14 public rows one for one, and its extra statics (0x4eaa30, 0x4eab00, 0x4eb1b0, 0x4ec220, 0x4f0030, 0x4f00b0) are helpers that 2.6 inlines or that Wii added, such as the symbol makers. Asserts carry "DWARF.c" and their line numbers run up with the file (331 ... 2719). Lines 2614/2618, inlined into both insert_type_node_before (2632/2649) and create_type_node (2683), show a static helper defined just before them, and that helper is now written as one (NewDwarfNodeBefore). .data 0x5cf738 and .bss 0x5e3900 both start 8-aligned. The .data holds the six initialized statics, "DWARF.c", "MW EABI PPC C-Compiler", "", "::" and then the jump tables. "::" is a dead function's literal: Wii's DWARF .data has the same literals plus its symbol makers' "%04x.", ".dwarf.", ".line.", and its last function (0x4f00b0, called only by the named-symbol maker 0x4f0030) is Pro7's CExcept_MangleNameSpaceName (the namespace walk appending each name and "::" to name_mangle_list). So 2.6 had that helper, dead, and not the makers: it is now written as that body (DWARF_MangleNameSpaceName), replacing the invented `DWARF_QualifiedNameSeparator` that only returned "::".
- Flags: pro6 (sources.json's pro53 maps to pro6), `-O4,p -inline auto,deferred -sym off`. Deferred is right because the rows descend against the file's order (and Wii's). `-sym on` gives 16/38, so `-sym off` is needed. A plain `-inline auto` build also gives 38/38 per function, but the descending order needs deferred to link.
- Functions: 38/38 exact. There is no Pro7 file, so the reference is gc1's DWARF.c plus 2.6's and Wii's disassembly. Natural spellings now used, each shown to match: the invented inline helpers DWARF_FullDebugEnabled, DWARF_ActivateFunctionState (a copy of SetScope) and dwarf_zero_addend_mode are gone; `TYPE_IS_SOFT_FLOAT(rtype)` replaces the `Boolean gpr = copts.soft_float` copy in setup_return_operand; the `(SInt8)` mode local is gone from emit_class_dwarf; emit_function_entry lost a dead `list = pending->next`; emit_array_type calls ResolveFixups instead of a written-out copy; InitDwarf and InitDebugSections lost their no-op casts and pointless locals; insert_type_node_before and create_type_node share NewDwarfNodeBefore, with the scope store in the helper and in the NewDwarfNode arm (their duplicated code and `owner = (DWInfo*)node->type` are gone); `(Type*)` casts became `TYPE()`. What stays:
  - *Original's shape:* `if (name) { symbol = NULL; } else { symbol = NULL; }` in enum, struct and class entries. 2.6 has an empty `cmp; je` to the next instruction. Wii makes a symbol in *both* arms (named: 0x4f0030 + 0x4eaa30 with kind 0x103; anonymous: 0x4eaa30(NULL, 0x102, ...)), so 2.6's two NULL arms are where those calls go. emit_variable_entry is the same with Wii's per-arm tag and symbol kind and one maker call after the join; in 2.6 each arm sets the tag and `symbol = NULL`. One assignment (before or after the if, or `(void)0` arms) gives 81.7% (enum) and 71.8% (variable). newDWARF_file's dead `symbol = NULL` (dropping it: 92.5%) is where Wii calls 0x4eab00 and keeps the result for AddSymbol.
  - emit_parameter_entry now passes a literal NULL to get_type_dwarf_ref: exact (the first review's "68%" did not reproduce), and Wii pushes an immediate 0 there too. `symbol = NULL` before the call stays: 2.6 zeroes it first and passes it to emit_entry_header after CError_FATAL(2199) on a NULL `var->func` (Wii likewise); moving it after the call gives 98.5-99.5%.
  - *Original's shape:* the repeated `TYPE_IS_SOFT_FLOAT(type) && size != 4` inside `if (TYPE_IS_SOFT_FLOAT(type))` is a macro expansion (dropping it gives 95%).
  - *Fakematch, TODO:* emit_dwarf_ref's `switch (*(UInt16*)ref)`. Tried: casts to int, SInt16, SInt32 and unsigned int, `& 0xFFFF`, `(*ref).tag`, a local, an initialized `ref`, a 16-bit enum field (96%), and `info->rec.tag` (99.5%).
  - *Fakematch, TODO:* newDWARF_file takes `void*` and copies it to an `ObjGenSection*`. Wii's newDWARF_file has the same `mov ebx, ecx` copy, so the original's parameter type differs from the local's.
  - *Original's shape:* the NEXT_QUAL macro (an inline function forces memory).
- Behaviour: all exact, references included. The literals' bytes match .data. Kinds 3, 6 and 11 of the location switch are never produced in 2.6 (dead branches of the original's). Genuine oddities in the original (kept): emit_class_dwarf tags a static data member 0x16 (TAG_typedef); the code after CError_FATAL(1604) (a link-name attribute) is kept; create_type_dwarf_ref gives `void*` FT_pointer and drops the qualifier list it built. None of ours found.
- Names: the 14 public functions are Wii's. The globals are Wii's COMMON names (DefaultContext, CurContext, text, LineSect, dwarf, DebugSect, debug_records, func_link, dwarfline, last_debug_record, ReturnAddress). Invented, each with a TODO: the 24 statics Wii leaves unnamed (gc1's names, or the repo's), the inlined helpers, NewDwarfNodeBefore, DWARF_MangleNameSpaceName (Pro7's CException name with DWARF's prefix), all the statics, dwinfo_buckets and pending_objects (Wii's pending_objects is a static), and the vendor `AT_MW_*`/`FT_MW_*` constants. Parameters renamed from a/b and typeLink.
- Types: DWARF 1 constants are now named: TAG_, AT_, FT_, OP_, FMT_, LANG_ (as #defines, which compile like the literals). `case 19` is now `IT_VEC2X32FLOAT` (the old comment was stale). DwarfLocationOperand's `reference {value; UInt16 reserved; object}` placeholder and `indirect` were merged into `address {offset; reg; object}` at the same offsets, and `pair` became `objectOffset {object, offset}`. The location kinds are documented. Node kinds stay numeric (Metrowerks-internal: 0x4080, 0x4081, 0xffff).
- Data: links (relink `--only`). The 24 functions are now `static`, as are the six initialized .data globals (Wii has them unnamed in DWARF's .data); their 6 bindings were deleted. Only DWARF.c's code references them in the original. The .bss is still byte-identical with `#pragma pack(2)` set once before the structs (MWCC takes the statics' packing from the end of the file). This replaces two push/pop pairs plus a trailing pack(2); `options align=mac68k` also works but clang rejects it. ReturnAddress (a COMMON only DWARF references in the original) is now defined here, not in CodeGen.c: same link, same COMMON layout. The TODO stays for the 8 unreferenced bytes at the top of the .bss.
- Header: DWARF.h declares 13 of Wii's 14 publics, each with a caller outside the unit (ObjGen_PPC_EABI, TOC, PCodeAssembly, BE_elf, InlineAsm). DWARF_EstablishContext (public in Wii) has no caller outside DWARF.c (its two calls follow its definition), so its declaration was dropped; it stays non-static. The private structs, the static prototypes, the ReturnAddress extern and the CompilerTools.h include moved into DWARF.c. Dropped the unused CPrec.h, CTemplateFunc.h and Registers.h includes.
- Headers relied on:
  - common.h: RegClass, plus typedefs of DWARF's private structs (DWInfo, DwarfFixup, DwarfRef, QualNode, PendingObject ...). Problem: `typedef struct DwarfSymbol` names a struct nothing defines.
  - objects.h: `Object.debugInfo`. Problem: its `DwarfSymbol* symbol` member is a stand-in that only CPrec writes.
  - CPrep.h (COpts): full_dwarf, filesyminfo, dwarf_relocations, dwarf_siblings, absolutepath, ignore_global_reg_vars, soft_float, littleendian, cplusplus. Problems: `full_dwarf` (invented) has its comment's sense inverted (when set, the patches go into each section's own state); `soft_float` is invented.
  - CompilerTools.h: GList, galloc, memclrw. Fine (`AppendGListByte` takes SInt8, hence the `-0x6d`/`-0x7f` spellings).
  - BE_elf.h: ELFfile (invented ElfFile type and members, TODO-marked there), WriteDebugRelocation, BackPatchRelocationAddend, TransferSectionData.
  - BE_symbol.h: BE_SymNode, MakeSymbolTableEntry.
  - ObjGen_PPC_EABI.h: ObjGenSection.debugState, ObjGen_GetSectionGList, GetSourceFileName.
  - CSOM.h: used only for `ScopeSearch`. Problem: it defines `ScopeSearch`, which is Pro7's `CScopeObjectIterator` from CScope.h.
  - CScope.h: the object iterator.
  - CABI.h, CMachine.h, CMangler.h, CParser.h, CError.h, CodeGen.h (low_reg/high_reg), RegisterInfo.h, StackFrame.h, types.h, scopes.h, enode.h: plain declarations, no problems seen.
  - None of these was otherwise reviewed here.
- Open: the two fakematch TODOs (emit_dwarf_ref's switch lvalue, newDWARF_file's `void*` parameter); invented names with nothing to take them from (Wii leaves the statics unnamed); the 8 unreferenced .bss bytes; the header problems above.
- Adversarial review: re-read Wii's DWARF disassembly at both Wii-based "original's shape" claims. The if/else NULL arms hold, with corrected evidence (Wii makes a symbol in both arms, not only the first; natural one-assignment spellings re-tried: 81.7%/71.8%). The emit_parameter_entry claim broke: a literal NULL matches (and is Wii's), so the "still-NULL `symbol`" spelling and its comment went. Following Wii's "::" user replaced the invented one-line DWARF_QualifiedNameSeparator with the Pro7-shaped dead DWARF_MangleNameSpaceName (relink still byte-identical). Also dropped DWARF_EstablishContext from the header (no outside user) and spelled the private structs' `struct BE_SymNode*`/`struct ObjGenSection*` members by their typedefs. Held: static data member tag 0x16 (Wii pushes 0x16 too), the class entries' other tags (0x1c, 0xd), the boundary against Wii's module list, the header's other 13 users. Not re-tried (documented TODOs): emit_dwarf_ref's switch lvalue, newDWARF_file's `void*` (Wii has the same `mov ebx, ecx` copy). After the changes: 38/38, no exact flag lost anywhere (compare_start.json), layout run, relink `--only` links, clang_check and clang-format clean.

### src/driver/Resources.c: SOLID WITH TODOS
- Boundary: 29 rows 0x406650-0x4087b0, the same 29 functions (statics included) as Wii's "Resources" module, in Pro7's
  MacEmul/Resources.c order. Dead Pro7 functions: OS_MacDumpResourceFork kept (its literals are in the unit's .data);
  OS_UseMacResourceForkInfo added back (dead, no data; Pro7 has it before WriteResourceFork, whose asserts carry Pro7's
  line numbers 943-973). The rest of Pro7's dead Toolbox emulation (InitResources, Get1Resource, ...) has no data and
  no assert after it: left out.
- Flags: pro6 -O4,p -sym off, not deferred (rows ascend in Pro7's order). -O2 loses 16 functions, -sym on 6.
- Functions: 29/29 exact. Departures from Pro7: the endian swaps (2.6 Win32 change: SWAP16/SWAP32 of every on-disk
  field read or written, confirmed in the code); Win32 error codes ERROR_FILE_NOT_FOUND/ERROR_INVALID_ACCESS where Pro7
  returns errno's 2/13 (2.6 returns 12); needed shapes, each tried in Pro7's spelling first (fn.py variants):
  DeleteResources' early return (Pro7's `if (rm)` 90.2), GetResourceSizes' `== NULL` in the ternaries (78.1), the
  count-before-advance in Get*Count (90.9), OS_Set/GetMacResourceForkCreatorAndType's early returns and
  `(x >> 24) & 0xFF` bytes (86.7 / 95.0), WriteResourceFork's `map_offs = data_len + data_offs`, `data_offs = name_offs
  = 0`, `(data_offs >> 16) & 0xFF` (each 99.5-99.8 in Pro7's), ReadResourceFork's `ref_offs` running offset and the
  places of `dhdr`/`dsys`/`dmap` (52-99%), FSpOpenResFile's `(SInt8)fsRdWrPerm` arm (95.5 without; catalogued
  byte-literal ternary). One fakematch (TODO): ReadResourceFork's `*(SInt32*)&tent.len` in RF_READ (2.6 compares and
  reloads the length signed; Pro7's `tent.len`, a signed Ty82.len and a cast inside RF_READ were tried). Reverted to
  Pro7: the ref_list bounds check's operand order, CloseResFile's `rm` local, FSpCreate without a const cast, Pro7's
  `Handle` fields in Ty73/Ty77.
- Behaviour: checked against the disassembly (exact). Genuine bug in the original (kept): WriteResourceFork clears
  `resChanged` from the fork attributes where `mapChanged` is meant (Pro7 has it too).
- Names: Pro7's (functions agree with Wii). The on-disk struct names Ty70..Ty82 and MemRsrcSysData are Ninji's
  placeholders/assumed names, kept from Pro7.
- Types: Pro7's; ERROR_* from compiler/win32.h (through MsDos.h), fsRdPerm for the permission literals.
- Data: links (.data, .bss byte-identical). Statics static; ResLoad global (COMMON, bound). Bindings of the six
  statics (resError, cur_res_file, maplist, resForkList, resForkLast, UseResourceForkInfo) deleted: only this unit's
  functions reference them.
- Header: Resources.h now declares only what Files.c, CLMain.c, ClientGlue.c, TextUtils.c use; ResLoad,
  OS_AddMacResourceForkRef, FSpOpenRF dropped, FSpOpenResFile/CloseResFile prototyped at the file top (used before
  their definitions).
- Headers relied on: oslib.h (OSSpec, OS_* file API, OS_ASSERT), Files.h (FSSpec, FSpCreate/HOpen/FSRead...,
  fs*Perm), Memory.h (Handle API), MacSpecs.h, MsDos.h (and compiler/win32.h's ERROR_*), CLIO.h, StringUtils.h: no
  problem found in what this unit uses.
- Open: the TODO pun above; Ty70..Ty82 are placeholder struct names (no source for real ones).
- Header round H2 (October 16): Resources.h is DONE (Files.h/Memory.h includes dropped); nothing of the headers is pending. The unit's own TODO and placeholder names stay.

### src/driver/CodeGenOptPPC.c: SOLID WITH TODOS
- Boundary: 4 rows 0x42d580-0x42dd10, Pro7's 4 functions; Wii's CodeGenOptPPC module ends with the same four (its
  earlier processor/licence functions are later additions). .data: the three panel names as literals.
- Flags: was plain `-inline auto` with the functions written in binary order; the rows run in reverse of Pro7's order
  (Test_Version_Numbers lowest, CodeGen_InitCompiler highest), so now `-inline auto,deferred` in Pro7's order
  (config/sources.json, every version). -sym on matters (off: 3/4).
- Functions: 4/4 exact. Rewritten from Pro7: the old source's invented inline helpers (SetProcessor,
  LoadPreferenceResource, ReadBackEnd/Project/Linker, SetProcessorModel, SetCodeAlignment, a SetFloatingPointSupport
  that wrote schedule_factor, a SetTargetFeatureEnabled that wrote altivec_vrsave) and its string globals are gone:
  Pro7's `Handle handle; pb = *((PBackEnd*)*handle);`, literal panel names, plain copts stores, the CPU_* names for
  copts.processor. 2.6 changes (confirmed in the disassembly): the PPC EABI panels (CodeGen, Project, Linker),
  `cparamblkptr = &cparams`, the EABI copts (cats, sdata thresholds, pool_data, soft float, Gekko stack frame,
  bc_range, ...), and the panel-version cascade of processor switches. Needed shapes: `handle` declared last (stack
  slots), the explicit `case CPU_PPC602` beside default (99.7 without), two stores overwritten later
  (misaligned_mem_access = 1, unsafe_global_reg_vars = 0: dropped by MWCC but removing either changes the code;
  Pro7 has the first). `pb.floatingpoint != 0` made natural.
- Behaviour: statement by statement against IDA's pseudocode. Genuine bug in the original (kept, commented): the
  panel-version cases fall through, but each switch reads pb.processor again, so the oldest mapping wins (a version 6
  panel's 4, the 555, comes out as a 601).
- Names: Pro7's/Wii's. Invented: PProject.no_comment_records (pref_structs.h, TODO: after the copts member it feeds;
  was `unknown34[3]`).
- Types: Pro7's; scheduling models stay numbers as in Pro7.
- Data: links. pref_versions static (binding deleted: only this function references it). `cparams` moved to
  CCompiler.c, where Pro7 defines it (COMMON; both units still link).
- Header: CodeGenOptPPC.h has Pro7's four prototypes (the three resource-name externs gone); users CCompiler.c,
  BE_elf.c, InlineAsmPPC.c.
- Headers relied on: CPrep.h (copts, CPU_* enum, cparamblkptr: the CPU enum is anonymous, CPU_PPCELF is 2.6's e500
  slot), CCompiler.h (cparams), InstrSelection.h (init_cgdispatch), CWSecretPluginCallbacks.h
  (CWSecretGetNamedPreferences takes `void*` where the SDK has `Handle*`, TODO there), pref_structs.h (PBackEnd
  placeholders unknown05/07/09[2]/0c/16[22], PProject unknown02[34]/2d/34[3], PLinker unknown05/0c/2f).
- Open: the invented PProject field name; the pref_structs placeholders.

### src/driver/OptimizerHelpers.c: SOLID WITH TODOS
- Boundary: 3 rows 0x40db40-0x40dea0 = Pro7's OptimizerHelpers.c and Wii's module; .data has pIrOptimizer (7 bytes
  at its start, 0x597e60: `= {0}` puts it in .data, where Wii's is .bss), its literals and irPragmas; no .bss.
- Flags: pro6 -O2,p -sym off, not deferred (ascending). -O4 loses 2, -opt intrinsics 1; -sym makes no difference
  (kept).
- Functions: 3/3 exact, now Pro7's text. Departures: SetOptFlags' `if (flags & 8) no = TRUE; else no = FALSE;` and
  `set = bar = (minus = no) ^ 1` with two int copies read by the '-'/'|' cases (TODO: the same shape as SetWarningFlags,
  where every natural spelling was tried; Pro7's own text does not match its binary there either: "type issue with
  'no'"); `set ? PR_ON : PR_OFF` (Pro7 writes 1 : 2, its enum being assumed: 96.1 with numbers); optfor's
  `(Boolean)1`/`(Boolean)0` arms (2.6 stores byte constants: probably a lost byte-sized speed/size enum; TRUE/FALSE,
  `!= 0` and plain ints tried). 2.6 change: DisplayOptimizationOptions tests `propagation` where Pro7's text tests
  `commonsubs` (disassembly).
- Behaviour: checked against IDA. No bugs.
- Names: Pro7's (PIROptimizer is Pro7's assumed name). None invented.
- Types: PIROptimizer moved from the header to the file (only this unit uses it); Pragma's `value` is now `void*`
  (Pro7's), so the table needs no casts.
- Data: links. pIrOptimizer static (binding deleted); setOldOptimizerOptions is a COMMON (bound, Pro7 defines it here).
- Header: prototypes the three option callbacks (the .opt tables use them), irPragmas (PostParse), the PR_* enum (also
  used by StdTargetWarningHelpers-cc.c). setOldOptimizerOptions and PIROptimizer dropped (no other user).
- Headers relied on: ParserGlue-eabi-ppc-cc.h (Pragma: fixed here to Pro7's `void* value; const char* pragma`; it
  also re-declares `extern Pragma irPragmas[]`, a private copy, and declares `char symFullPath`/`char output_path`,
  stand-ins for arrays), Option.h (Option_Warning(unsigned int, ...) where Pro7 has `SInt16 id`, AGENTS' varargs rule;
  CLPStr62 has no name here), option_system.h (PARAMPARSEFLAGS_* added from Pro7; GENERIC_T.parse is `void*` where
  Pro7 has the typed callback), TargetOptimizer-ppc-eabi.h, pref_structs.h (pGlobalOptimizer), IO.h
  (ShowTextHandle(char*) where Pro7 has const char*), StringUtils.h.
- Open: the two TODO shapes above; PR_* would belong in parser.h (ParserHelpers-cc.c keeps a private `#define
  PR_UNSET 0`).

### src/driver/TargetOptimizerHelpers-eabi-ppc.c: SOLID WITH TODOS
- Boundary: 3 rows 0x4216f0-0x421940 = Pro7's TargetOptimizer-ppc-mach.c's three and Wii's module
  "TargetOptimizerHelpers-eppc" (the same three functions in 1.0RC1, 1.0 and 1.7). Renamed from
  TargetOptimizerHelpers-eabi-ppc.c (gc1's invented name, in an order no other 2.x file uses): 2.6 has no assert, file-name
  literal or other evidence of its own, so Wii's module name in the repo's 2.x spelling (eppc -> eabi-ppc, as
  cc-eppc -> cc-eabi-ppc and ParserGlue-eppc-cc -> ParserGlue-eabi-ppc-cc) is the best there is. Header renamed with
  it; the four includers, every version's functions.json/splits.txt/state.json, sources.json and docs updated. Pro7's COMMON
  `schedule_ppc_default` belongs to the Mach scheduling cases 2.6 lacks; 2.6 and Wii have no such symbol.
- Flags: pro6 -O4,p -inline auto -sym on, not deferred. -sym off loses one; -O2 and -opt intrinsics change nothing.
- Functions: 3/3 exact. Pro7's TargetSetOptFlags without the Mach S? cases (2.6 has only Pe/Sn/Sh); Display adds the
  peephole line (2.6) and the EABI processor list; `int p` for Pro7's `PrefCPU p`.
- Behaviour: checked against IDA (switch constants, string order).
- Names: the invented `PrefCPU_*` enum is gone: 2.6 numbers the panel's processors as COpts.processor
  (CodeGen_InitBackEndOptions copies it for current panels), so the tests use CPrep.h's Pro7 CPU_* names.
- Types / Data: as Pro7; literals only, no statics.
- Header (TargetOptimizerHelpers-eabi-ppc.h): three prototypes, used by OptimizerHelpers.c (Targets.c, Arguments.c and CLCompilerLinkerDropin_V10.cpp
  include it for nothing: their owners' to drop).
- Headers relied on: CPrep.h (CPU_* names: anonymous enum, CPU_PPCELF prints "e500"), pref_structs.h (pBackEnd:
  placeholder fields, see CodeGenOptPPC), StringUtils.h.
- Open: pref_structs.h's placeholders.
- Header round H2 (October 16): CPU_* names are CParser.h's. Still open: pref_structs.h's PBackEnd `unknown05/07/09/0c/16` (OPEN: real fields, no source names them).
- Follow-up (October 16, after round H2): the pref_structs.h `unknownXX` fields are a documented TODO (the H2 driver adversary found every one real, never set by an option and never read; Pro7's Mac panels are laid out differently and the EABI SDK's panel headers are not available), so nothing is pending on a header.

### src/driver/StdTargetWarningHelpers-cc.c: SOLID
- Boundary: 2 rows 0x40e250, 0x40e450 = Pro7's file and Wii's module; .data holds pExtraWarningC, warningPragmas and
  the literals.
- Flags: pro6 -O2,p -opt intrinsics -sym on, not deferred. -sym off loses one; -O4 and dropping -opt intrinsics change
  nothing (kept, unproven).
- Functions: 2/2 exact: Pro7's text, with Pro7's signatures now (`short val, Boolean set`, `Handle txt`; were `char
  enabled`, `void* self`).
- Behaviour: as Pro7, confirmed.
- Names / Types: Pro7's. PExtraWarningC (Pro7's assumed name) moved from the header to the file with Pro7's `UInt8`
  fields (were `char`).
- Data: links. pExtraWarningC's binding deleted (only this unit references it); warningPragmas stays bound (PostParse,
  in ParserGlue, needs it).
- Header: the two functions (WarningHelpers.c) and warningPragmas (PostParse); the pExtraWarningC extern dropped.
  Projects.c includes it for nothing.
- Headers relied on: ParserGlue-eabi-ppc-cc.h (Pragma, see OptimizerHelpers), OptimizerHelpers.h (PR_*),
  pref_structs.h (pWarningC, pFrontEndC), StringUtils.h.
- Open: header issues only.
- Header round H2 (October 16): `Pragma` and the PR_* enum are Pro7's, in parser.h; PWarningC/PFrontEndC have no placeholders. Resolved.

### src/driver/WarningHelpers.c: SOLID WITH TODOS
- Boundary: 2 rows 0x40e080, 0x40e1b0 = Pro7's file and Wii's module.
- Flags: pro6 -O2,p -opt intrinsics -sym on, not deferred; no flag tried changes the code (-sym, -O4, -opt): kept,
  unproven.
- Functions: 2/2 exact, rewritten from Pro7 (the old text was IDA-shaped: `optionCode`, `0x4e77` for 'Nw',
  `defaultEnabled`). DisplayWarningOptions is Pro7's verbatim, with Pro7's callback signature (it was `unsigned int
  DisplayWarningOptions(void)`). SetWarningFlags: Pro7's but for the flag setup (TODO): `if (flags &
  PARAMPARSEFLAGS_8)` and `set = bar = (minus = no) ^ 1`, the '-'/'|' cases reading the int copies. Tried: Pro7's
  `(Boolean)((flags & 8) >> 3)` (88.6), `!= 0`, `? 1 : 0`, `!!`, `set = no; set ^= 1;` (ToolHelpers' Opt_SetStage
  shape), no casts.
- Behaviour: checked against the disassembly.
- Names / Types: Pro7's; `int` callback signature as in the option tables.
- Data: links (literals only).
- Header: both callbacks (the .opt tables), now with Pro7's signature.
- Headers relied on: option_system.h (PARAMPARSEFLAGS_8, added), pref_structs.h (pCmdLine), ParserFace.h
  (Parser_StorePanels returns `int`, Pro7 `SInt32`; parseopts), ParserErrors.h, IO.h (ShowTextHandle's non-const
  key), StdTargetWarningHelpers-cc.h, StringUtils.h.
- Open: the SetWarningFlags TODO shape.
- Adversarial review: attacked all six. Held: the CodeGen_InitBackEndOptions "bug in the original" (IDA: cases 6-9,
  5, 4 and 3 each end by jumping into the next older switch, which re-reads the unmodified stack copy pb.processor,
  so every panel version 3-9 ends with the version 1/2 mapping for 0..13 and CPU_Generic above it: a version 6 555
  (4) is a 601; and a version 1/2 panel's value >= 14 leaves copts.processor as it was); every pb/pp/pl byte offset
  against IDA; the processor compare constants of TargetDisplayOptimizationOptions; the three dropped/overwritten
  copts stores (13 stores in the binary for 15 in the source; Pro7 overwrites schedule_factor the same way); the
  Wii module contents of all six units; the Resources header's users. The fakematch TODOs were re-tried and still
  hold: SetOptFlags' `minus`/`bar` int locals (fakematch: Pro7's text 84.1, if/else with Pro7's casts 86.1, ternary
  85.7, `!no` 84.0-87.7), ReadResourceFork's `*(SInt32*)&tent.len` (Pro7's `tent.len`, a copy in esize, cnt or
  msize: 78.4). optfor's `(Boolean)` arms: `(UInt8)` matches too (same type), plain/TRUE/FALSE 96.1, `set`/`!set`
  94.1. Broke: TargetOptimizerHelpers-eabi-ppc.c's name (renamed to TargetOptimizerHelpers-eabi-ppc.c, see its entry);
  OptimizerHelpers' entry put pIrOptimizer in .bss (it is the unit's first .data). After: 4020/4077 exact as
  before (none lost or gained), layout clean, all six link, clang_check 0 errors.

### src/frontend/CClass.c: SOLID WITH TODOS
- Boundary: 67 rows 0x52bf70-0x52f73b, between IroBitVect and InlineAsm. The definitions are Pro7's 75 functions, in Pro7's order, none missing or extra (the other 8 are inlined statics). Wii's pairings outside CClass (IsTrivialCopy*/IsPODClass to CExpr, DummyDefaultConstructor to CABI_, Init to CDecl_Init) are weak "order/caller" or trivial-body matches. Every address sits inside the run, in Pro7's order.
- Flags: pro6 `-O4,p -inline auto,deferred -sym on`. Deferred because the rows fall in address against Pro7's order (CClass_Init, Pro7's first function, is at the highest address). Non-deferred gives 62/67 exact; `-sym off` gives 49/67.
- Functions: 67/67 exact. Restored to Pro7: the TPTR_QUAL/ENODE_QUALS/IS_TYPE_METHOD/IS_TYPEFUNC_NONSTATIC_METHOD macros, the redundant TYPE_CLASS() casts on `cfriend->u.theclass` removed, and PathCleanup without the invented `next` local. Departures, each checked by testing Pro7's spelling:
  - 2.6 changes: OptimizeBitFieldAccess handles 64-bit bitfields (cases 32..56, offsets >= 32).
  - Needed shapes (catalogued): ClassAction uses an early return where Pro7 has else-if (97.4). MemberDef returns in each arm, with cases 0 and 3 merged (87.8-98.7 otherwise). CheckOverrides needs `SInt32 i` (95.4 with int). ReferenceArgument needs `!= NULL` (72.4 without). Declaration order differs in GetBasePathRec, DefineCovariantFuncs and CheckVirtualBaseOverrides (78-83 in Pro7's order).
  - Needed shape: IsEmpty's `CClass_IsEmpty(base->base) == FALSE` (Pro7's `!` gives 76.8; commented in the source).
  - Original's shapes: PathCleanup's tail is now `return path->type == TYPE(tclass) ? first : NULL;` (exact; the reviewer's `first = NULL; return first;` also matched, Pro7's two returns and an if/else give 89.1). PointerTypeCopy uses typed `TypePointer*` and `TypeMemberPointer*` copies (Pro7's single `Type*` gives 86.6). AssignmentOperator assigns `args` inside the condition (Pro7 86.7). OverridesBaseMember has an `object` local (Pro7 94.8). OptimizeBitFieldAccess has a `bitfield` local and firstBit/lastBit; gc1 has the same (Pro7's spelling gives 90-91). CheckPathAccess keeps `!= NULL`: Pro7's `path && obj` matches itself but breaks the next function generated, DefaultConstructorCall.
  - No fakematches.
- Behaviour: every function is exact, so it behaves as the original. No bugs of ours found.
- Names: functions are Pro7's and confirmed in names-2.6.md. Renamed ThunkList's a/b/c and CClass_ThunkObject's parameters to this_delta/return_delta/ctoroffset (Pro7's CMangler_ThunkName parameter names). Invented (TODO): OVFunc's overriderclass/overrider/overrider2 (Pro7: ovc8/ovfC/ovf10). Ninji's register-named locals (offset27, offset26, offset23) are kept as Pro7's.
- Types: the file-local structs are Pro7's. Stand-ins come only from headers (below).
- Data: the unit relinks byte-identical. Its 20 statics are `static`. Deleted 8 bindings (cclass_root, _ovbase, _vbase, _thunklist, _isbase_*, _founddepth, _isambigbase): `bindings.py refs` shows only CClass functions reference them. The old `pack(push,2)`...`pop` plus a `#pragma pack(2)` at the end is now one `#pragma pack(2)` with no reset, marked TODO: .bss needs 2-byte packing at the end of the file, and Pro7 resets mac68k alignment after its structs.
- Header: CClass.h is Pro7's list. Every declaration has outside users except CClass_IsNonStaticMemberFunc and CClass_GetMemberOffset, which are dead in 2.6 and in Pro7 (no row, no caller outside the file, GetMemberOffset only recursive) and kept as Pro7's API. Removed a stale TODO comment that described nothing.
- Headers relied on: 37 project headers, mainly CScope, CDecl, CExpr2, CInline, CInit, CError, types and objects. Problems:
  - CScope.h: `ScopeSearch` is Pro7's CScopeObjectIterator. `CScopeParseResult` is Pro7's NameResult and has the placeholders trailingPadding[3], x1F and x21.
  - CExpr2.h: `MatchLink` is a private copy of objects.h's ObjectList (used by CError_OverloadedFunctionError and CTempl_PartialOrdering). `ArgMatch.xE` is a placeholder.
  - CInline.h: CInlineInfo is Pro7's CI_FuncData.
  - CInit.h: RelocationList{object, addend} is Pro7's OLinkList{obj, somevalue}.
  - CError.h: the enum stopped at 371; added ERR_ILLEGAL_ACCESS_FROM_TO_PROTECTED_PRIVATE_MEMBER (381) and ERR_IMPLICIT_COPY_ASSIGNMENT_CONST_REF_MEMBER (387), texts read from the binary's message table, and the two calls use them. `CError_Error` is declared returning `NameSpaceList*` (Pro7: `void`; no caller uses the result): a stand-in for the header's owner.
  - CDecl.c:5221 has a private block-scope prototype of CClass_ClassDefaultFuncAction.
  - types.h has an `x12` placeholder.
- Open: the TODOs (3 invented field names, the reset-less pack) and the headers' problems above.
- Adversarial review: the entry claimed IsEmpty was restored to Pro7's `!f()`; the source has `== FALSE`, and `!` gives 76.8 (corrected, commented). PathCleanup's `first = NULL; return first;` replaced by the ternary, which also matches. CheckPathAccess `path && obj` matches itself and breaks DefaultConstructorCall (confirmed), ReferenceArgument without `!= NULL` 72.4 and as if/return 91.2: held. Error 381 checked in IDA (381 with path->type, else 187) and now named. Header claim corrected (two dead declarations). The Wii pairs outside CClass rechecked: IsTrivialCopy*/IsPODClass go to unnamed CExpr functions by caller/order (0.57-0.64), DummyDefaultConstructor to CABI by strings-set 0.59, Init to CDecl_Init (a 2-instruction body); Pro7 defines all of them in CClass.c, so the boundary holds. Grade lowered from SOLID WITH TODOS to PENDING HEADERS: the entry lists open header problems (ScopeSearch, CScopeParseResult with x1F/x21, MatchLink, ArgMatch.xE, types.h x12), and the bar grades that way.
- Header round H2 (October 16): ScopeSearch is Pro7's CScopeObjectIterator, CScopeParseResult Pro7's NameResult (trailingPadding/x21 gone), MatchLink gone (ObjectList), ArgMatch's `xE` is `userconversions` (TODO), CInlineInfo Pro7's CI_FuncData, RelocationList Pro7's OLinkList, CError_Error returns `void`, CDecl.c's private prototype gone. Left as TODO: types.h's `TypeObjCID.x12` and NameResult's `x1F` (real members, no user).

### src/frontend/CABI.c: SOLID WITH TODOS
- Boundary: 37 rows 0x5570e0-0x55b9f3. The definitions are Pro7's 56 functions, in Pro7's order (AppendCopyRegion included), none missing. Wii puts all its pairs in CABI. Before the run is CObjCModern's last function. GetStructResultArgumentIndex is the run's last row; names-2.6.md says it was once filed under CInline. MakeLayeredDestructor and AddLayeredDestructors are dead in 2.6 (no rows, no callers) and kept as Pro7's.
- Flags: pro6 `-O4,p -inline auto,deferred -sym on`. Deferred because the rows fall in address against Pro7's order (Pro7's first function is the last row). Non-deferred gives 35/37; `-sym off` gives 28/37.
- Functions: 37/37 exact. Restored to Pro7:
  - ApplyClassFlags' 3-parameter signature, called with `TRUE` in AddLayeredDestructor.
  - StructSizeAlignValue as Pro7's text with `int align`; the old body had an `align32` copy and `1U`s.
  - ReverseBitField as Pro7's text with `int bits`.
  - TransDestructor as Pro7's text, declaration order included. The only changes are `for (scan = stmt; scan; scan = next)` with `(next = scan->next)` in the test, and the chained flag assignments. The do/while and the three other uses of `next` were not needed.
  - DestroyBases' outer `while`.
  - DestroyObject's declaration order.
  - AppendCopyRegion's while loop.
  - OBJ_MEMBER_VAR/OBJ_BASE where there were raw casts.
  - 8 functions made `static` as in Pro7. They had been global for lifted code; the unit links now.

  Departures:
  - 2.6 changes: MakeDefaultAssignmentOperator does a block copy under `copts.block_assign && CClass_IsTrivialCopyAssignClass`. CopyConAssignCB tests `!CClass_IsTrivialCopyAssignClass` where Pro7 tests CClass_AssignmentOperator. The assert lines are 2.6's.
  - Needed shapes: `|=` everywhere Pro7 writes `x = x | F` (each reverted spelling failed). IsOperatorNew takes `Object*` and tests `next == NULL` (Pro7's ObjBase* or `!` give 83-95). InitVBasePtr1 inverts the if/else. The assert in InitVBaseCtorOffsets assigns inside it (96.0 without). TransDestructor needs its flag chain (74-98 without). InitVTablePtrs needs its declaration order.
  - Original's shapes: InitVTablePtrs computes `voffset + CABI_GetVTableOffset(baseclass)` twice and writes `offset != 0` (98.3 and 97.6 otherwise). TransConstructor builds the funccall expression first (97.6). CopyConAssignCB has a separate `off` counter, which gc1 has too. DestroyBases' inner `i = 1; while (i < count) { i++; ...}` (none of six other spellings matched).
  - No fakematches.
- Behaviour: every function is exact. Genuine quirks in the original, kept:
  - CopyConAssignCB tests `CClass_CopyConstructor(t) || CClass_CopyConstructor(t)`; Pro7 has the same.
  - TransDestructor drops CABI_InitVBaseCtorOffsets' result.
  - MakeLayeredDestructor starts with CError_FATAL(2557) and can use `mode` uninitialized.

  Bug of ours fixed: GetStructResultArgumentIndex is Pro7's `(TypeFunc* tfunc)`. Its 7 callers had cast to `Type*`.
- Names: functions and statics are Pro7's. No invented names. CABIDestroy0..3 and TransDestructor's flag23..29 are Pro7's placeholder names; no meaning is evident to rename them by.
- Types: CopyRegion and OffsetList are Pro7's. `CExpr_ConstructObject(TYPE(...))` and the ClassLayoutInput casts come from headers (below).
- Data: the unit relinks byte-identical. Its statics are `static` and none is bound. The CopyRegion `pack(push,2)`...`pop` plus the `#pragma pack(2)` at the end is now one `#pragma pack(2)` with no reset, marked TODO, as in CClass.
- Header: CABI.h matches Pro7's list. MakeLayeredDestructor and AddLayeredDestructors have no users outside the file, as in Pro7 (dead).
- Headers relied on: 36 headers. Problems:
  - CDecl.h: `ClassLayoutInput` is Pro7's ClassLayout with renamed fields. `entries` is `Object**` where Pro7 has `ObjBase**`, and CDecl.c's CDecl_SetupClassLayout uses it the same way. `DefArg{obj, expr}` is Pro7's DefArgCtorInfo{default_func, default_arg}. Placeholders x54..x58 and pad66.
  - CExpr2.h: CExpr_ConstructObject's first parameter is `Type*` where Pro7 has `TypeClass*`, so callers need TYPE() casts.
  - CError.h: 387 now named (above).
  - CScope.h: as for CClass.
  - Callers in CFunc, CExpr, CExpr2 and CInit pass raw numbers for CABIDestroyMode.
- Open: the reset-less pack TODO and the headers' problems above.
- Adversarial review: re-tried Pro7's spellings with `fn.py variants`: StructSizeAlignValue `SInt16 align` 87.5 (and breaks CABI_LayoutClass), ReverseBitField `UInt8 bits` 84.7 (breaks CABI_AllocateMembers; `SInt32` also exact, `int` kept), InitVTablePtrs with Pro7's `vtblOffset =` 98.3 / without `!= 0` 97.6, AppendCopyRegion's two statements 91.8: all held. Tried Pro7's own `#pragma options align=mac68k` ... `align=reset` in place of the reset-less pack(2): exact code but .bss 24 bytes for the unit's 22, so reset works in MWCC x86 and the TODO stands. IsOperatorNew's `Object*` and `== NULL` are gc1's text (original's shape confirmed). Named error 387. Boundary: the Wii pairs outside CABI are all name-less or weak (CABI_AddVTable ~ CABI_AcquireGuardVariable, 7 instructions). Grade lowered to PENDING HEADERS: the entry lists open header problems, which the bar grades that way.
- Header round H2 (October 16): ClassLayoutInput is Pro7's ClassLayout (`ObjBase** objlist`), DefArg Pro7's DefArgCtorInfo, CExpr_ConstructObject takes Pro7's `TypeClass*`, CError names 387, DeclInfo's x54..x58/pad66 are gone (the rest TODO names); CABIDestroy0..3 are Pro7's placeholders (TODO). Other units' raw CABIDestroyMode numbers are theirs.

### src/frontend/CRTTI.c: SOLID WITH TODOS
- Boundary: 14 rows 0x57d5a0-0x57eb49, between BitVectors and GCCInlineAsm. The definitions are Pro7's 18 functions, in Pro7's order (FindTypeInfoType, ConstCastQualCheck, IncompleteCheck and IsSameType are inlined). Wii puts its pairs in CRTTI; its later names CRTTI_TypeID and CRTTI_GetTypeInfoObject are weak pairs, so Pro7's names are kept.
- Flags: pro6 `-O4,p -inline auto,deferred -sym on`. Deferred because the rows fall in address against Pro7's order; non-deferred also compiles 14/14, so the address order is the evidence. `-sym off` gives 7/14.
- Functions: 14/14 exact. Restored to Pro7:
  - CreateBaseListObject: Pro7's `work2` loop with `SInt32* buf`. The old `group = (SInt32*)(buf + ((char*)work - buf))` was a fakematch and is gone. The `flag` local is gone too, and the declarations are within four moves of Pro7's order.
  - Parse_dynamic_cast: Pro7's text plus one `TypePointer* tptr = TYPE_POINTER(di.dtype)` copy (the catalogued typed-local shape). The gc1-shaped `desttypekind` assignment in the condition is gone.
  - ConstructTypeInfoObject: two declaration moves from Pro7's order.
  - TPTR_QUAL, ENODE_QUALS and OBJ_NAMESPACE macros where they had been expanded.

  Departures:
  - Needed shapes: `!scan->isambiguous` where Pro7 has `x13 == 0` (74.9). `count2++` before the sum (99.6). CreateBaseList inverts the if/else (94.6). UniversalCast inverts the old_argmatch test (95.6). reinterpret_cast reuses `expr` for Pro7's `lvalue` (73.9).
  - Original's shapes: ConstructVTableHeader indexes `((SInt32*)(data + voffset))[1]` (97.7 and 98.6 with Pro7's `+ 4`). IsSameType is `static inline`, catalogued: 2.6 has no out-of-line copy of it.
  - No fakematches left.
- Behaviour: every function is exact; no bugs.
- Names: Pro7's. Invented (TODO): RTTIBaseList's isprivate/isambiguous (Pro7's placeholders x12/x13) and the local `ambiguous` (Pro7's flag27).
- Types: Pro7's local structs. `RelocationList` is the repo's name for Pro7's OLinkList.
- Data: the unit relinks byte-identical. The 2 statics are `static`, and their bindings (crtti_offsets, crtti_olinks) are deleted.
- Header: CRTTI.h matches Pro7's list and every declaration is used (CClass, CExpr). LoopOptimization.c includes it but uses nothing from it.
- Headers relied on: 36 headers. CInit.h's RelocationList and CDecl.h's `DeclInfo.dtype` (Pro7: thetype) are the repo's names for Pro7's. Otherwise as for CClass. (Since: DeclInfo has Pro7's member names where Pro7 has names (thetype, storageclass, section, is_extern_c, in_friend_decl, expltargs, file/file2/sourceoffset ...) and TODO-marked invented ones for Pro7's offset members; the f38/f3d/x54..x58/pad66 placeholders are gone.)
- Open: the TODO-marked invented names and the headers' problems (as for CClass).
- Adversarial review: Parse_dynamic_cast with Pro7's TPTR_* macros and no `tptr` 96.2, UniversalCast in Pro7's order 95.6: held. Checked the invented RTTIBaseList names against their uses (`isprivate` is the `flag || access == ACCESSPRIVATE` value, `isambiguous` set when a base recurs at another voffset): they fit. Wii's CRTTI_TypeID/GetTypeInfoObject pairs are strings-single 0.44 and order 0.38: weak, Pro7's names stand. Grade lowered to PENDING HEADERS as for CClass.
- Header round H2 (October 16): OLinkList and DeclInfo's `thetype` are Pro7's; the rest as for CClass (types.h `x12`, NameResult `x1F`: TODO). Resolved as TODO names.

#### Shared evidence for ParserFace.c, ParserHelpers.c, ParserHelpers-cc.c, ParserGlue-eabi-ppc-cc.c, StaticParserGlue.c, CWParserPluginsPrivate.cpp, CLParserCallbacks_v1.cpp, Projects.c, ToolHelpers.c, ToolHelpers-cc.c, IO.c

# Review: the command-line parser library and its glue (GC_2_6)

All eleven units were rewritten from Pro7's text, word for word, before any 2.6 change went in. The
earlier text was IDA-shaped, with stand-ins such as `EnvInfo`, `driverStatus()` inline helpers,
`PragmaHasSetting`, `CharToLower` casts, `calltool`/`resources =` copies and `data_0058851d`. After
the rewrite every function is exact in every version (2.0, 2.0p1, 2.5, 2.6 and 2.7), all eleven units
link (`relink.py --only`), and nothing was lost in 2.6's compare.json: 4020/4077 exact before and
after. As a run-time check, 17 command lines (-D/-U, -I-, -prefix, -stages, -o twice, wildcards,
-use_lmw_stmw with -little, -version, -help, a link) gave identical output and objects from the
original and from the equivalent compiler.

Common evidence:
- Order: the rows ascend in address and follow Pro7's file order, so no unit is deferred.
- Code alignment: an -O1 object's .text is 1-byte aligned and an -O4,p object's is 16-byte aligned.
  The parser library's units start at odd addresses, which confirms -O1; the -O4,p units start
  16-aligned.
- Message ids: `CLPStrNN`, Pro7's names, now in ParserErrors.h. Each comment was checked against
  2.6's STR12010. Only 76's text differs: 2.6 has `'%s'` where Pro7 has `\"%s\"`.

### src/driver/ParserFace.c: SOLID
- Boundary: 14 rows, in Pro7's order. Wii's module "ParserFace" has the same publics (the statics
  SetupParamBlock, SetupOptions, Parse and StoreResults are, as expected, not publics). The .data
  holds the literals of the dead `CWParser_GetTargetList` (`'****'` ×2, the target list), so that
  function stays.
- Flags: -O1 -opt intrinsics -sym off. The functions are not 16-aligned; -O4,p gives 1/14 exact and
  -sym on gives 12/14.
- Functions: 14/14 exact. Departures:
  - `parser_main`: `case reqTerminate` where Pro7 has `case 0` (2.6 change: the compare chain tests
    -1).
  - `parser_main`: `CWResult result = cwNoErr;`, a store that setjmp's result replaces (original's
    shape: the xor/mov comes before the request; without it the function is 4 bytes short).
  - Otherwise Pro7 verbatim: static functions, the `if (result) return result` chains, and
    `ustrcmp` instead of the old `CLIO_STRINGS_EQUAL` macro.
- Behaviour: as Pro7. Parse's `? cwNoErr : cwErrRequestFailed` needed the SDK's real error enum (see
  CWPlugins.h below).
- Names: Pro7/Wii. `failedCallback`, `exit_plugin` and `parseopts` are Wii COMMONs. No invented
  names.
- Types: the stand-in `EnvInfo {f0, width, height, parserEnvironmentFlag, f7}` is gone; Pro7's
  `PCmdLineEnvir` (pref_structs.h) is used. `sCPU`/`sOS` are `SInt32` because 2.x's
  `CWTargetList.cpus` is signed (commented).
- Data: links, so .data and .bss are byte-identical. The binding `_SetupParamBlock` (a function
  address, now a static) was deleted from GC_2_6's bindings.json.
- Header: ParserFace.h now declares only what other units use: Parser_StorePanels and the plugin
  entry points that StaticParserGlue uses. Removed: the statics' prototypes, Parser_FindPrefPanel
  (no other user), the dead GetTargetList, EnvInfo, and the private copy of `extern pTool`, which
  moved to Targets.h (Targets.c defines it; every user already includes Targets.h).
- Headers relied on: see the shared list after the last entry.
- Open: none in the unit.
- Header round H2 (October 16): ParserFace.h is DONE (OSType, `helpFlags` SInt32); Targets.h's DispatchObject stand-ins and ClientGlue.h's macros are gone; Option.h is Pro7's. Still open: CWSecretPluginCallbacks.h's `void* prefsdata` (TODO: the SDK's `Handle*`), which this unit's CWSecretGetNamedPreferences call passes `&h` to.
- Follow-up (October 16, after round H2): CWSecretGetNamedPreferences has the SDK's `Handle* prefsdata`: no longer pending.

### src/driver/ParserHelpers.c: SOLID WITH TODOS
- Boundary: 12 rows, in Pro7's order (Wii's "ParserHelpers" has the same publics plus a later
  ParserInsertOptions). The .data holds `"In environment variable"`, `"*?"`, `".lib|.a"`,
  `"lib%s%*.*s"`, `"Lib"` and `""`. So Pro7's dead Opt_AddUnixLibraryFile, GetFirstSourceFilenameBase
  and ParseNumber stay. Pro7's other dead functions (Opt_AddFrameworkPath, Opt_AddFramework,
  Opt_FindAndAddFileRef, AddFileList, IsFileInOutputDirectory, Get[CP]FileNameInOutputDirectory)
  leave no data and are not added. The .bss is the 260-byte static STSbuf.
- Flags: -O1, as above. -sym on gives 11/12 exact; -O4,p gives 0/12.
- Functions: 12/12 exact. Departures:
  - 2.6 changes: 260 replaces 256 throughout. `ERROR_FILE_NOT_FOUND || ERROR_PATH_NOT_FOUND` where
    Pro7 has `ENOENT || ENOENT`, as Win32 codes. AddAccessPathList has a drive-letter shortcut
    (`(sep1==':'||sep2==':') && my_isalpha && list[1]==':' && OS_MakePathSpec ok`).
    Opt_FindAndAddFile has a wildcard branch (`!strpbrk(arg,"*?")` added to the OSAlert test, then an
    `OS_MatchPath` loop that adds every match), confirmed in IDA.
  - Needed shapes:
    - MatchesExtension uses an if/else for Pro7's `continue`, and `*list == 0 && *ptr == 0` for
      `!*list && !*ptr`. Both are needed, each tested alone.
    - Opt_AddAccessPath and Opt_FindAndAddFile put `arg ? arg : opt` into a new local `path` where
      Pro7 reassigns `arg`. The disassembly shows the copy into another register.
    - ListParseMessage calls `va_start` before CLPGetErrorString (2.6's order).
    - AddAccessPathList uses `int type` (Pro7: short, which gives a movsx),
      `(system==1) ? (Boolean)1 : (Boolean)0` and `(source == 0) ? 0 : 2` (each alternative was
      tried, 96.7-98.5%), and `Boolean recurse` declared after `type` (stack slots).
  - Fakematch (TODO in the source): AddAccessPathList's `volatile int source` and the copy through
    `t = type` for the loop's call. 2.6 keeps `source` in its stack slot and loads `type` into eax.
    Tried without effect: `SInt32 source`, `short type`, `(SInt16) type`, an `else if` after the
    shortcut. Without both, the best is 99.1-99.4%.
  - Workaround: `#define STSbuf ParserHelpers_STSbuf`. Generic.h declares the driver's COMMON
    STSbuf, and a static of that name would redeclare it (commented).
- Behaviour: Pro7 plus the 2.6 branches above, checked against IDA's pseudocode. No bugs found.
- Names: Pro7/Wii. `data_0054a0b8` is now `lastStage`: Pro7 defines it in this file, Wii has it as a
  COMMON, and its mwld hash 2427 falls between nlflag (2341) and asm_alloc_flags (2472). It was
  renamed in every version and is now defined here, as in Pro7. No invented names.
- Types: Pro7's prototypes (`const char*`, `Boolean warn`, the Opt_ handler signature
  `(opt, var, arg, flags)`). The repo's old `(char* name, int recursive, char* override)` guesses are
  gone. MatchesExtension is static, and the `CharToLower` cast stand-in is removed.
- Data: links. The `_ParserHelpers_STSbuf` binding was deleted: only this unit's exact code reads
  it.
- Header: declares the four COMMONs, plus GetEnvVar, Opt_AddAccessPath, AddAccessPathList,
  Opt_FindAndAddFile, AddStringToHandle, Opt_PrintVersion, Opt_SavePrefs and
  Opt_MaybeMoveAccessPaths, each used by ParserGlue, the .opt tables, ParserHelpers-cc or
  ToolHelpers(-cc). FindFileInPath, MatchesExtension, ListParseMessage, AddStringLenToHandle and
  the dead functions were dropped from it.
- Headers relied on: see the shared list. `compiler/CPrep.h` was removed (adversarial pass): nothing
  in the unit uses it, clang_check is clean, 12/12 stay exact and the unit still relinks, so no
  section changed.
- Open: the volatile/`t` fakematch.

### src/driver/ParserHelpers-cc.c: SOLID
- Boundary: 5 rows, in Pro7's order (Wii's module has them plus later additions). The .data is
  exactly these functions' literals plus the assert file name `"ParserHelpers-cc.c"`. Pro7's
  Opt_Pragma{TrueFalse,FalseTrue,OnOff,OffOn} leave no data that would prove them, and neither 2.6
  nor Wii has them, so they are not added.
- Flags: -O1. -sym on/off both match (not decided by the code; kept like the library). -O4,p gives
  0/5.
- Functions: 5/5 exact, Pro7 verbatim except two needed shapes:
  - Opt_AddPrefixFile picks the handle into a local `storage` by if/else, where Pro7 reassigns the
    parameter by ternary (97.3% with a ternary into a local).
  - SetupPragmas tests `reverse = flags == PRAGMA_FLAGS_1` once into a Boolean (the original's
    `sete dl`).
  - The asserts are Pro7's `OS_ASSERT(186/195, …)` with Pro7's stringified conditions, and their
    bytes are in .data.
- Behaviour: as Pro7. `CLPStr28_WarningText` replaces 0x1c.
- Names: `directive_storage` is now Pro7's `definesHandle` (Wii COMMON; hash 3915 falls between
  Xicth_func 3876 and exceptionlist 3934). It was renamed in every version and is defined here
  (Pro7), no longer in ParserGlue.
- Types: Pro7's prototypes. `Pragma` is Pro7's `{void* value; const char* pragma; int flags}`, now
  in parser.h. The old copy in ParserGlue-eabi-ppc-cc.h had per-field offset comments.
- Data: links.
- Header: definesHandle and the five handlers (used by ParserGlue and the .opt tables). It includes
  parser.h for Pragma.
- Headers relied on: see the shared list.
- Open: none in the unit.
- Header round H2 (October 16): the shared list's items are resolved (Targets.h's stand-ins gone, Option.h Pro7's, ClientGlue.h's macros gone); ParserHelpers-cc.h DONE. Resolved.

### src/driver/ParserGlue-eabi-ppc-cc.c: SOLID WITH TODOS
- Boundary: 4 rows (PreParse, MidParse, PostParse, RegisterStaticParserToolInfo) and 39 KB of
  generated panel/option tables, all byte-identical. Wii's "ParserGlue-eppc-cc" has
  RegisterStaticParserToolInfo and the panels as publics.
- Flags: -O4,p -inline auto -sym on. -sym off gives 3/4, -O1 gives 1/4, and -inline off also matches
  4/4 (not decided by the code). The functions are 16-aligned.
- Functions: 4/4 exact, Pro7's mach-ppc-cc text with 2.6 changes, all confirmed in IDA:
  - PreParse sets `pLinker.sreceol = TRUE` where Pro7 resets schedule_ppc_default.
  - MidParse: the default state is `OptsCmdLineState_3` (compile and link; Pro7: 2), and the env vars
    are `"MWCEABIPPCIncludes\0MWCIncludes\0"` with separators `;`/`:`.
  - PostParse adds the `-use_lmw_stmw` little-endian note (CLPStr28) and `pLinker.debugInfo` /
    `pLinker.symfullpath` stores.
  - The old `#pragma scheduling off/reset` around the functions was not needed and was removed.
  - The `(unsigned char*)` casts in stPrefPanels were removed (the `ptr` field is void*).
- Behaviour: as above. No bugs found.
- Names: the old `output_path` is Pro7's `linkerOutputFilename` and `data_0058851d` is
  `setLinkerOutputFilename` (both are ToolHelpers.c's, below). `symFullPath` (a Wii COMMON) is now
  defined here: Pro7's Mach-O tool has no such option, and this unit's tables set it. No invented
  names.
- Types: the panels are pref_structs.h's (see headers).
- Data: links.
- Header: now only `RegisterStaticParserToolInfo` (CLStaticMain uses it). The other declarations it
  held were moved to their owners: Pragma to parser.h, irPragmas (OptimizerHelpers.h already had
  it), definesHandle to ParserHelpers-cc.h, and linkerOutputFilename to ToolHelpers.h.
- Headers relied on: see the shared list. pref_structs.h's EABI panels still have `unknownXX` fields.
- Open: those pref_structs.h placeholder fields (the header owner's).
- Header round H2 (October 16): `PLinker.unknown05` is `suppresswarn` (TODO) and `PProject.unknown2d` was an alignment hole (removed). Still open: pref_structs.h's other `unknownXX` (PBackEnd 05/07/09/0c/16, PLinker 0c/2f, PDisassembler 05, PProject 02/34).
- Follow-up (October 16, after round H2): the pref_structs.h `unknownXX` fields are a documented TODO (the H2 driver adversary found every one real, never set by an option and never read; Pro7's Mac panels are laid out differently and the EABI SDK's panel headers are not available), so nothing is pending on a header.

### src/driver/StaticParserGlue.c: SOLID
- Boundary: 2 rows plus `ParserErrors.r`'s STR12010 table, Pro7 verbatim. Wii's
  "StaticParserGlue" has the same two publics.
- Flags: changed from -O4,p to -O1 -opt intrinsics -sym off, the parser library's flags. The code
  matches under both, but the unit starts at 0x40532e, which an -O4,p object's 16-aligned .text
  rules out. It relinks under -O1.
- Functions: 2/2 exact, Pro7 verbatim.
- Behaviour: as Pro7.
- Names: Pro7's.
- Types: BasePluginCallbacks and ParserPluginCallbacks (CWPlugins.h/CWPluginsPrivate.h).
- Data: links.
- Header: two prototypes; CLStaticMain uses both.
- Headers relied on: see the shared list.
- Open: none in the unit.
- Header round H2 (October 16): StaticParserGlue.h DONE; the shared list's items are resolved. Resolved.

### src/driver/CWParserPluginsPrivate.cpp: SOLID
- Boundary: 16 rows, in Pro7's order (Wii's "CWParserPluginsPrivate" matches, plus a later
  GetCurrentPrefs).
  - Pro7's dead CWParserStoreCommandLineForPanel stays: the implicit `??4CWCommandLineArgs` is placed
    after it.
  - Pro7's four overlay/segment wrappers were added as dead code. They leave no data, but 2.6 has
    their callbacks (CLParserCallbacks_v1's UCBParserAddOverlay1Group …) and Projects.c's dead
    functions call them.
  - No .data.
- Flags: -O4,p -inline off -sym on. -sym off gives 2/16 and -inline auto gives 3/16.
- Functions: 16/16 exact, Pro7 verbatim with the cw error enumerators (the old text had 3/4
  literals).
  - CWParserCreateVirtualFile and CWParserDisplayTextHandle now return CWCreateNewTextDocument's
    result as CW_CALLBACKs. The old `int` with no return was undefined behaviour, and the old `void`
    differed from Pro7.
  - The `#pragma bool off` at the file top was not needed and was removed.
- Behaviour: as Pro7. Bug of ours fixed: CreateVirtualFile returned no value.
- Names: Pro7/Wii. The old parameter names (`query`, `firstValue`, `record` …) were replaced by
  Pro7's.
- Types: CWParserContext (CWPluginsPrivate.h).
- Data: no .data or .bss.
- Header: Pro7's API. The overlay/segment prototypes are commented as having no 2.6 caller.
- Headers relied on: see the shared list.
- Open: none in the unit.
- Header round H2 (October 16): CWPlugins.h's CWParserCallbacks entries are typed (Pro7's plugin.h); the shared list's items are resolved. Resolved.

### src/driver/CLParserCallbacks_v1.cpp: SOLID WITH TODOS
- Boundary: 11 rows (UCB*, the implicit `??4OSPathSpec`, the CWParserContext constructor), in Pro7's
  order.
  - The .data is `parser_cb` plus `" search path"`.
  - Pro7's empty `~CWParserContext()` was added (Pro7 has it; it is dropped, with no trace).
  - relink notes the implicit `??4FSSpec` as another unit's. This was already the case before the
    rewrite: a COMDAT-like copy.
- Flags: -O4,p -inline off -sym off. -sym on gives 5/11 and -inline auto gives 9/11.
- Functions: 11/11 exact, Pro7 verbatim except:
  - UCBParserAddAccessPath uses an `int failed` assigned in if/else where Pro7 has a ternary inside
    the `if` (needed shape; the Boolean and other declaration orders give 83-85%).
  - UCBParserSetFileOutputName compares `(int) which == 1/2/3` (the catalogued movsx-then-32-bit
    shape).
  - The constructor chains `cpu = os = 0` (2.6's chained form).
  - `OS_OSErrorToMacError` for Pro7's `OS_MacError`, and CW_CALLBACK (`__stdcall`) on Win32.
  - `#pragma bool off` at the file top (the build's bool setting, as in CLDropinCallbacks_V10,
    CLCompilerLinkerDropin_V10 and CLPluginRequests): without it UCBParserAddAccessPath is 84.3%,
    and with bool on no spelling of the test matches (Pro7's ternary 90.1%). `-bool off` in
    sources.json instead of the pragma also gives 11/11; kept as the pragma for consistency with
    the other three units.
- Behaviour: as Pro7 (`CLStr77/78/79/103` and the cw error enumerators instead of numbers).
- Names: Pro7's. Wii mangles the UCB functions as C++ (`?UCBParserAddAccessPath@@YGJ…`); the repo
  keeps them in `extern "C"`. Linkage is not observable in 2.6, so this is left as an open item.
- Types: the old stand-in locals (`struct Target* paths`, `Paths* paths_list` …) are gone.
- Data: links. The `_parser_cb` binding was deleted: only this unit's exact constructor uses it.
- Header: now only `PrefPanelsChangedCallback` (CLMain uses it; it is a Wii C COMMON). The UCB
  prototypes had no other users.
- Headers relied on: see the shared list. (`<string.h>` had to stay inside `extern "C"` while the
  project's include/libc headers had no C++ guards; they have them now, see the follow-up below.)
- Open: C vs C++ linkage of the UCB functions (Wii says C++).
- Header round H2 (October 16): CLParserCallbacks_v1.h is DONE. Still open: include/libc/string.h has no `extern "C"` guards (the reason `<string.h>` sits inside `extern "C"` here), and the UCB functions' C vs C++ linkage (Wii says C++) is unsettled.
- Follow-up (October 16, after round H2): include/libc's twelve headers have `extern "C"` guards, as MSL's own headers do, so `<string.h>` is an ordinary include here and in CLDropinCallbacks_V10.cpp and CLCompilerLinkerDropin_V10.cpp (every version unchanged). No longer pending; the UCB linkage question stays open (a TODO).

### src/driver/Projects.c: SOLID
- Boundary: 7 rows, in Pro7's order. The .data holds every Pro7 failedCallback name, including the
  dead GetOutputFileDirectory, AddOverlayGroup, AddOverlay, AddSegment and ChangeSegment, which
  stay, and the dead GetFileInfo, which the dead GetFirstSourceFilenameBase calls. Pro7's
  GetSegment has no literals and no 2.6/Wii trace, so it is not added.
- Flags: -O1. -sym on gives 6/7 and -O4,p gives 0/7.
- Functions: 7/7 exact, Pro7 verbatim except two needed shapes:
  - AddFileToProject passes `!exists` (Pro7: `exists == 0`; the original's xor/cmp/inc) and chains
    `mergeIntoOutput = weakImport = initBefore = FALSE` (2.6 reloads between the stores).
  - AddAccessPath writes `((type & 1) == 0) ? cwUserPath : cwSystemPath` and
    `|= (type & 2) ? cwAccessPathTypeFlag2 : 0`. The Pro7 spelling and four others give 94-97.7%.
- Behaviour: as Pro7, with 2.6's 260-byte STSbuf (Generic.h, `sizeof`). Pro7's dead branch
  `position == -1` after `position == -1 || position == 0` is kept (Pro7 and 2.6 both fold it).
- Names: Pro7's.
- Types: Pro7's prototypes. SInt16 type/which, Boolean recursive/exists, SInt32 position and
  `const char*` replace the old `(int use_first, int value, unsigned char option)` guesses.
- Data: links.
- Header: Pro7's API: GetFileCount, SetFileOutputName, AddFileToProject, GetFileInfo (used by
  ParserHelpers' dead function), AddAccessPath, MoveSystemPathsIntoUserList, AddVirtualFile and
  SetOutputFileDirectory.
- Headers relied on: see the shared list.
- Open: none in the unit.
- Header round H2 (October 16): Projects.h is DONE (Pro7's prototypes); CWPluginsPrivate.h takes the SDK's types. Resolved.

### src/driver/ToolHelpers.c: SOLID WITH TODOS
- Boundary: 4 rows, in Pro7's order. The .data holds `"CWParserSetOutputFileDirectory"`,
  `"Bad stage settings…"` and `"wt"`, so the dead Opt_RedirectStream stays, now with Pro7's body: the
  old one had been cut down to a bare `freopen`. Pro7's ToolReport*, which have no literals and no
  Wii public, are not added. The .bss is the 1-byte static `setStage`.
- Flags: -O1. -sym on gives 3/4 and -O4,p gives 0/4.
- Functions: 4/4 exact.
  - Opt_HandleOutputName and ValidateToolState are Pro7 verbatim. The only 2.6 change is
    OS_NameSpecToString's 260.
  - Opt_SetStage writes `if (flags & 8) no = TRUE; else no = FALSE;` where Pro7 has
    `(Boolean)((flags & 8) >> 3)`. Pro7's own comment says its line does not match; a ternary or
    `!= 0` gives 96.9-97.9%.
- Behaviour: as Pro7. The old ValidateToolState had invented `driverStatus()` inline helpers, which
  are gone.
- Names: four COMMONs (all Wii publics, all renamed in every version, all defined here as in Pro7),
  each checked against its neighbours' mwld hashes:
  - `output_path` is `linkerOutputFilename`: hash 3022, in the 8-aligned group between 2444 and
    3943.
  - `data_0058851d` is `setLinkerOutputFilename`: 2520, between 2488 and 2522.
  - `output_path_set` is `setOutputDirectory`: 526, between 520 and 545. Its old name's hash (2500)
    does not fit there.
  - `data_00588530` is `outputOrdering`: 3544, between 3346 and 3720.
  - The `OutputOrdering0..2` names are Pro7's placeholders (TODO).
- Types: `char linkerOutputFilename[264]`: the COMMON is 0x108 bytes and laid out in the 8-byte
  alignment group, so its size is a multiple of 8. 260 would fall in the 4-group, so 264 it is
  (commented). PCmdLine's state and stage values use Pro7's enums, now in pref_structs.h.
- Data: links.
- Header: Pro7's API, the COMMONs `extern`.
- Headers relied on: see the shared list.
- Open: none in the unit (the placeholder enum names are Pro7's).
- Header round H2 (October 16): ToolHelpers.h is DONE WITH TODOS: Pro7's OutputOrdering0..2 became OutputOrdering_None/_After/_Before (TODO: invented names); `linkerOutputFilename[264]` is 2.6's .bss. Resolved as TODO names.

### src/driver/ToolHelpers-cc.c: SOLID WITH TODOS
- Boundary: 2 rows plus the dead Opt_DummyLinkerSettingRoutine, whose string is in .data, as in
  Pro7.
- Flags: -O1. -sym on also matches; -O4,p gives 0/2.
- Functions: 2/2 exact, Pro7 verbatim (`SInt32 numfiles`, the CmdLineStageMask/OutputOrdering
  names, `CLPStr41/42`).
- Behaviour: as Pro7.
- Names: Pro7's.
- Types: Pro7's handler signature for Opt_DummyLinkerRoutine. The old one had a single parameter.
- Data: links.
- Header: Opt_DummyLinkerRoutine (.opt tables) and FinishCompilerTool (ParserGlue). The dead
  setting routine was removed from the header.
- Headers relied on: see the shared list.
- Open: none in the unit.
- Header round H2 (October 16): ToolHelpers-cc.h is DONE; the OutputOrdering_* names it uses are TODO-invented (ToolHelpers.h). Resolved as TODO names.

### src/driver/IO.c: SOLID WITH TODOS
- Boundary: 2 rows, in Pro7's order. The .data is their literals.
- Flags: -O1. -sym on also matches; -O4,p gives 0/2.
- Functions: 2/2 exact, Pro7 verbatim (CLPStr71, `parseopts.toolVersion->…` directly, without the
  old cast local) except one original's shape. ShowVersion stores the driver's and the parser's
  version strings into two locals that nothing reads, through an if/else-if chain, where Pro7 has
  two `continue`s (both dead stores land in edi). gc1's 1.x ShowVersion is the evidence: it has the
  same two variables (a driver version for 'cldr', a parser version for 'Pars'), both set and never
  read. The adversarial pass replaced the reviewer's single shared `vother` with the two locals
  (also exact; without the `= 0` initializations 99.3%).
- Behaviour: as Pro7.
- Names: the two locals, `vdriver` and `vparser`, are invented (`TODO: invented names`; gc1's
  names are invented too).
- Types: Pro7's prototypes (`const char* description`, `Boolean decorate`).
- Data: links.
- Header: both functions (used by ParserGlue and ParserHelpers/ToolHelpers).
- Headers relied on: see the shared list.
- Open: the two invented local names.

### src/frontend/CExpr.c: SOLID WITH TODOS
- Boundary: 90 rows, 0x53cc10-0x547ad0, the same set and order as Pro7's CExpr.c reversed. CExpr_RewriteConst, Pro7's first function, is at 0x5479e0, just below DWARF's first row (0x547ad0). CExpr_CheckUnwantedAssignment is the lowest row, just above InlineAsmRegistersPPC's last row (0x53cb40). Pro7's statics with no row are inlined: CExpr_ConstResult, CExpr_MemberVarAccess, CExpr_IsTemplateFunc, CExpr_TemplArgDepCast, BuiltInComputeAlign/ClassifyType, CExpr_ParseBuiltin, SimpleExplicitConversion, logicalexpression, CExpr_MakePTDM, getpointertomember, CExpr_MemberPointerCast, isvectorconst, the EMUL/EDIV/EADD/ESUB nodes, IsBlockMoveType, TransformOpAssign, HasSideEffect. Pro7's dead functions stay in the file: CExpr_ParseRotate, addconst, skipcommaexpr, CExpr_ParseBuiltin_isintconst, CExpr_NewPTMFCall, scanalignof, CExpr_ParseAsmExpr. Nothing calls them in 2.6, and the relink shows their absence from the binary does not matter. The asserts carry Pro7's lines plus 2.6's offset (+2 from about line 2568, +7 by the end). The names list's open row 0x53ce70 is closed: IDA shows CheckUnusedExpression called from 0x53cda0 and from 0x53ce70, so 0x53ce70 is s_expression and 0x53cda0 is expression with s_expression inlined. That decision is recorded `ok` in name_review.json. The unit links.
- Flags: pro53 `-O4,p -inline auto,deferred -sym on -Cpp_exceptions off`. Deferred because the rows descend against Pro7's order; plain `-inline auto` gives 84/90. `-sym off` gives 45/90.
- Functions: 90/90 exact. The source was rewritten from Pro7's CExpr.c verbatim, replacing the old gc1/IDA text: `'2'` literals for EINTCONST, `#pragma dont_inline` around optimizecomm, `0x179` error ids, hand-inlined HasSideEffect, a missing `return` in makepassignmentnode. Then only 2.6's departures were applied, each kept only after a variant showed it was needed. Pro7's verbatim text gave 29/90 before the 2.6 changes and asserts went in.
  - 2.6 changes, each in the disassembly:
    - `CodeGen_CheckExpr` after building a node in: makemult/div/add/subnode, the EOR/EXOR/EAND, comparison, EEQU/ENOTEQU and ESHL/ESHR nodes, ELAND/ELOR, BINNOT, MONMIN, LOGNOT, ECOND, MakeOpAssNode, makeassignmentnode, PREINC/PREDEC and POSTINC/POSTDEC.
    - HasSideEffect lists 2.6's extra node kinds before `default` (its assert is 7063).
    - cast_expression turns `warn_no_side_effect` off around the AltiVec vector literal.
    - getpointertomemberfunc byte-swaps its data with CTool_EndianConvertWord32 (the host is little-endian).
    - call_ptmf uses `Type_RequiresMemoryReturn`.
    - argumentpromotion copies a struct passed through a hidden argument into a temporary.
    - postfix_expression stores a small struct returned in registers to a temporary before `.`/`->`.
    - The EABI va_arg classes in CExpr_BuiltInComputeVArgType, and the `_INFO`/`_var_arg_typeof` builtins in primary_expression.
    - CExpr_ParseTypeExpression clears `*outflag` first.
  - Original's shape. Pro7 is a PPC compiler, where `return` and a jump to the shared epilogue look the same, so its text cannot tell them apart; x86 can:
    - CheckUnusedExpression's result-not-used part is `if (!void) {... break ...}`.
    - makepassignmentnode has one `return left` after the if/else.
    - CExpr_CompareConvert's float case is `if (==) return; convert; return;`.
    - IsBlockMoveType is `if (size != 4) return TRUE; return FALSE;`.
    - CExpr_MemberPointerConversion returns in each arm.
    - VArgType has one `result` variable.
  - Original's shape, other plausible spellings:
    - pointer_generation2 has a `break;` after the inner switch. It is one more statement, so -inline auto keeps it out of line, as 2.6 does; it also made 6 other functions exact.
    - optimizecomm and makeassignmentnode assign as plain statements rather than inside the condition.
    - Nested calls (PointerToMemberCast, argumentpromotion).
    - `|=` (do_typecast, getnodeaddress, primary_expression).
    - Declaration order in pm_expression, cast_expression, getpointertomemberfunc, ConstPointerCheck, DummyDestr and makeassignmentnode.
    - `SInt32 orig_qual` in oldassignmentpromotion, matching the callee's parameter.
    - `if ((tmp = conv.expression))` in unary_expression.
    - `expr = CExpr_Convert(...); return expr;` in do_typecast.
    - A separate `expr` local in makeassignmentnode.
    - ParseNameResultExpr reuses `ta_expr`/`expr` per branch and has `(tenum = ...)->nspace`.
    - `right_info.prec <= prec` in ParseDyadicExpression.
    - `TPTR_TARGET(ltype)` in pointercompare.
  - Needed shapes, each commented in the source. The natural spelling was tried for each:
    - CExpr_AtomTypeID takes `UInt8` (Pro7: IntegralType). The enum changes how far BuiltInComputeType's recursion is inlined.
    - `(int) tk` compares in DummyDestr and SimpleExplicitConversion (catalogued in AGENTS.md).
    - pointercompare goes through a local `expr` in its right-hand branch only (direct: 99.9%; through the local in both branches: 98.7%).
    - do_typecast reuses `tptr` (a TypePointer*) for the class target. A Type* local of its own is 99.2%.
    - call_ptmf builds both nodes in `list` and prepends the first to `args` (`list->next = args; args = list;` before the node store); the second reviewer replaced the extra `list1` copy with this (exact, one local fewer). Pro7's list1/list2 is 67.5%, `args = list` after the node store 83.8%.
  - TODO (fakematch): ParseNameResultExpr's DEXPR branch tests `(Type*)(expr = (ENode*)result->rtype)`. It is kept from the old source. Pro7's plain test and keeping the copy in `expr` throughout both give 99.0%.
- Behaviour: every function is byte-exact with references resolved, and the unit links, so constants, members, callees and literals (`"_INFO"`, `"_var_arg_typeof"`, `__FILE__`) are the original's. Genuine bugs in the original: Pro7's makeassignmentnode tests `ENODE_IS(tmp, EINDIRECT)` but then reads `right->data.monadic`. 2.6 keeps this (exact). Bugs of ours fixed: makepassignmentnode's missing `return` before CExpr_MakeOpAssNode; CExpr_HasSideEffect was a dead duplicate beside a hand-inlined copy carrying a different assert line.
- Names: functions are Pro7's, and the open row is resolved as above. Globals: `name_obj_check` and `disallowgreaterthan` are Pro7's, defined here as Pro7 does. COMMON hash order confirms two renames:
  - `non_type_template_argument_mode` -> `disallowgreaterthan` (hash 154 fits between IRO_IsModifyOp 151 and cscope_is_member_func 198; 732 did not).
  - `converted_expr` -> `assign_node` (1538 fits between rt_profile_exit 1516 and IRO_FirstExpr 1540; 2272 did not).
  Parameters and locals are Pro7's. 2.6-only locals have plain names: `save_warn`, `temp`/`tempcopy`/`indirect`/`result`, `result`, `tenum`. No invented function or global names here. The invented `EMemberInfo.is_qualified`/`addressTaken` (Pro7 has placeholders pr_1D/x11) keep their TODO in the header.
- Types: `ENode.data.string.useExplicitSize` renamed to Pro7's `ispascal` (enode.h; CExpr2.c, CObjC.c, DumpIR.c; code-neutral). Pro7's, through the repo's names for its structs: NameResult is CScopeParseResult, Conversion is BinaryOperatorResult, OLinkList is RelocationList. `disallowgreaterthan` and `temp_reference_init` are now Pro7's Boolean (they were char/UInt8). memberpointercompare and CExpr_NewDyadicNode take ENodeType (were UInt8). No offset macros or placeholder fields in the unit. Error 376/377 stay numbers because CError.h's enum has no names for them.
- Data: the relink proves .data and .bss byte-identical. The unit has no statics. COMMONs defined here are Pro7's two. The other units' COMMONs that were defined here moved to where Pro7 defines them:
  - rt_ptmf_cast/cmpr/test/scall/scall4 -> CParser.c
  - assign_node, temp_reference_init -> CExpr2.c
  - tksize, ispascalstring -> CPrep.c
  The full relink is unchanged: 217/217 sources, 2806 functions, 798275 bytes. bindings.json has only the COMMON bindings and the repo-wide row-name ones.
- Header: CExpr.h now declares only what other units use, plus Pro7's combined CExprConvMatch section (there is no CExprConvMatch.h; Pro7's CExpr.h does the same).
  - Dropped: the externs of Pro7's statics, the EMODULO/ESHL/ESHR/ELESS/EGREATER*/ENOTEQU/EAND/EXOR/EOR/ELAND/ELOR node functions and scanalignof (no outside users), the unused MemberPointerInitializer struct and `data_0055f5fa`, and `<setjmp.h>`.
  - Moved: the stbool and sttemplexpr externs (they belong to CMachine.h and CParser.h; sttemplexpr added to CParser.h), and CodeGen_CheckExpr (already in CodeGen.h).
  - Added: Pro7's `NameObjCheckCB` and `#include "compiler/enode.h"`, as Pro7's CExpr.h has.
  - The file top has the two globals and Pro7's two forward declarations. The includes are Pro7's list plus CExpr2.h, CFunc.h and CScope.h; the 28 unused ones (Iro*, InlineAsm*, DWARF ...) are gone.
- Headers relied on:
  - enode.h: ENode/ENodeUnion, with stand-in members (`string.ispascal` now Pro7's name, still `unsigned char` where Pro7 has `char`; invented `objaccess`/`explicitconversion`/`defaultargument`/`templatecomparison` views with `unk08`; MemberFunctionPointerData's `metadata[4]`).
  - types.h: IT_17..IT_24 added (Pro7's placeholders). IT_VEC2X32FLOAT is invented.
  - CScope.h: CScopeParseResult is Pro7's NameResult with invented member names, a `type.base` wrapper, and `x1F`/`x21` placeholders.
  - CDecl.h: DeclInfo stand-in names (`dtype`, `storage`, `f38` union, `f3d` ...). (Since: DeclInfo has Pro7's member names where Pro7 has names (thetype, storageclass, section, is_extern_c, in_friend_decl, expltargs, file/file2/sourceoffset ...) and TODO-marked invented ones for Pro7's offset members; the f38/f3d/x54..x58/pad66 placeholders are gone.)
  - CMachine.h: CMach_FloatIsZero/IsOne/IsNegOne and CalcFloatMonadic take `double` (Pro7: Float), and CalcFloatDiadicBool takes `volatile double`, which looks like a fakematch. Hence `.floatval.data.value` at the calls.
  - CParser.h: TK_ tokens are `#define`s (Pro7: an enum); atomtype returns TypeIntegral* (Pro7: Type*).
  - CABI.h: GetStructResultArgumentIndex takes Type* (Pro7: TypeFunc*).
  - CExpr2.h: CExpr_ConstructObject takes Type* (Pro7: TypeClass*).
  - CInit.h: CInit_DeclareData takes `void* list` (Pro7: OLinkList*).
  - CodeGen.h: CollapseVectorExpression takes TypeStruct* (Pro7: Type*). These four (CABI.h through CodeGen.h) force TYPE()/TYPE_STRUCT() casts here.
  - CError.h: the enum stops at 371 though 2.6's message table runs on (its strings follow 371's in the binary: 376 "illegal operand '%t'", 377 "illegal operands '%t' %u '%t'", same text as Pro7's CErrorStr376/377), so CExpr, CExpr2, CABI, CScope, CDecl and CParser pass raw 373..387; CError_Error returns `NameSpaceList*` (Pro7: void).
  - CExpr2.h: also CExpr_NewETEMPNode's `unsigned int withAuxiliary` (Pro7: `Boolean assign_id`).
  - CObjC.h: CObjC_IsIdOrSelType/CObjC_FindObjCClass are Pro7's CObjC_IsType_id/GetObjCType_id (gc1 names).
  - common.h: still typedefs the deleted MemberPointerInitializer.
  - The others: only Pro7-named functions used, no problem seen.
- Hygiene: braces everywhere. TRUE/FALSE for boolean returns, flags and boolean arguments (the second reviewer converted three the first missed: classargument's CExpr_ConstructObject flags, two CExpr_NewETEMPNode calls in postfix_expression). `//` comments. clang-format clean, clang_check adds no errors (83 baseline), no `#if 0`, no CERROR_FILE.
- Open: the ParseNameResultExpr TODO is a fakematch by the bar's own definition (a Type* stored into an ENode* parameter through two casts), and EMemberInfo's two invented field names are TODOs: so SOLID WITH TODOS, not SOLID PENDING HEADERS. Then the headers above.
- Adversarial review: attacked the fakematch (ParseNameResultExpr: 18 more variants, every combination of result/expr/ta_expr in the DEXPR and EOBJLIST branches, a `Type*` local at every declaration position, gc1's `obj` local: best 99.7, the TODO holds), call_ptmf's copy (broke: `args = list` reuse is exact and drops `list1`), do_typecast (Pro7's direct TPTR_TARGET 99.5, `return CExpr_Convert(...)` 98.5: holds; the tptr reuse is AGENTS-catalogued), CExpr_AtomTypeID (IntegralType exact alone but loses CExpr_BuiltInComputeType: holds), the boundary (rows = Pro7's 117 definitions minus 27 inlined/dead statics, in order; neighbours InlineAsmRegistersPPC/DWARF), the dead non-static scanalignof/CExpr_ParseAsmExpr (Pro7 has them dead too; no literals), the COMMON hash claims (disallowgreaterthan, assign_node, temp_reference_init, name_obj_check all fit their neighbours), name_review (no open or differing rows), header users (every declaration used outside). Broke: the grade (a documented fakematch is not SOLID), three 0/1 boolean arguments, the `useExplicitSize` stand-in name, and CError.h's missing 372+ names (reported). Every function exact before and after (compare_start), CExpr links, CObjC and DumpIR still link, clang-format and clang_check clean.
- Header round H2 (October 16): resolved: enode.h's invented views (Pro7's), CMachine.h's `double`s (Pro7's `Float`), atomtype's `Type*`, CABI/CExpr/CInit/CodeGen parameter types (Pro7's: the casts went), CError.h (ids to 392, `void`), CExpr_NewETEMPNode's `Boolean`, CObjC.h's names, MemberPointerInitializer. TODO names: NameResult, DeclInfo, IT_VEC2X32FLOAT (IT_17..24 are Pro7's). Still open: tokens.h's TK_* are `#define`s where Pro7 has the `EToken` enum (no entry addresses it). (Since, the same day: Pro7's `EToken` measured; it costs TOC.c's createfloatconstant, so the `#define`s stay, recorded in docs/nearmiss-2.6.md's blocked section: no longer pending.)

### src/frontend/CTemplateClass.c: SOLID WITH TODOS
- Boundary: .text 0x567e90-0x56aa30, .data 0x5d17d8 (8-aligned), no .bss; 31 rows = every out-of-line function of Pro7's CTemplateClass.c; Pro7's SetupActionErrorRef, RestoreActionErrorRef, AppendTemplateAction, NewDefAction, InsertTemplateAction, ParseBody, PartialClassIsMoreSpecialized, CopyObjMemberVarPath, CopyObjTypeTag, CompleteObjectInit, CopyUsingDecl are present as statics inlined by deferred inlining; dead TypeParamCompare kept (Pro7 has it, no evidence against). Asserts say "CTemplateClass.c"; Wii module CTemplateClass has the same API names. Relink proves the cut.
- Flags: was `-inline auto` in binary order; rows run InstantiateTemplateClass (lowest) to GetMasterTemplate (highest), Pro7's order reversed, so now `-inline auto,deferred` in Pro7's order. `-sym off` kept (unchanged; every function exact with it).
- Functions: 31/31 exact, rewritten from Pro7's text. Departures: 2.6 assert lines (+3 after 288, +11 from 789); `count++` before the advance (FindMostSpecialized, catalogued); `count = 0; scan = list;` order; CopyObjType returns in the if arm (duplicated epilogue in 2.6); ParsePartialSpecialization uses a typed `TemplClass* tmclass` copy of `type` (original's shape, catalogued typed-local); PartialTemplateArgMatch has a separate `index` for the non-type case, a counter `i = 0; ... i++` per argument that nothing reads (original's shape: 2.6's code computes it, `xor edi,edi`/`inc ebx`, and gc1's 1.3 body has the same unread `matched` counter; a separate `matched` variable is eliminated by MWCC (84.7%), so 2.6 reused `i`), and `return flag ? MakeTemplArgList : args`; CopyEnum tests `(src = ...objenumconst)->type` reusing its walker (original's shape); CopyIVars/CopyObject/CopyMemberTemplate take NewDefAction's result into a `DefAction* defAction` local, and CopyObject reuses `action` for its second walk (needed shapes; Pro7's chained `NewDefAction(...)->refobj =` reloads `deduce`); CompleteObject works on `OBJ_MEMBER_VAR(refobj)`/`OBJ_TYPE(refobj)` without Pro7's copies, one `||` condition for Pro7's nested experimental test, a return in each branch (needed: the original has one IsCompleteType call and no copies); CopyFriend computes `nspace = CScope_FindGlobalNS(...)` first and declares `flag` last; CopyBaseClasses reuses `base` for the template walk; InstantiateTemplateClass `!= 0`/`== 0` for hasNewVBases and hasVirtualFunction (2.6 materialises the values), declaration order by `fn.py decl-order`. Fakematch (TODO): CopyBaseClasses' `UInt32* pqual` assigned `&qual` in the call (2.6 holds &qual in ebx; Pro7's &qual, a Type* temporary, other positions tried).
- Behaviour: Pro7 text; the 2.6 departures above change no behaviour. Runtime check: a template-heavy C++ file (partial specialization, member templates, friends, explicit instantiation/specialization) compiles byte-identically (objects and diagnostics, -O0/-O4) with the equivalent compiler and the original. Bugs of ours fixed: the old file's `CTemplClass_RegisterBaseClass` returned the old action head (an invented `unsigned int` return); gc1/IDA text with magic numbers (`flags & 8312`, `0x1000`, `galloc(0x44)`, `memclrw(..., 14)`).
- Names: Pro7's for every function, parameter and most locals; non-Pro7 locals: tmclass, index, defAction, nspace, pqual. Invented: none.
- Types: Pro7's (TemplClass, TemplClassInst, TypeDeduce, DefAction, TemplateAction, PSpecList now file-local as in Pro7). Repo stand-ins used through headers: ClassLayoutInput (Pro7 ClassLayout), PrepTokenBuffer (TokenStream), TemplateScopeState (DeclFucker), DeclInfo field names; casts needed where headers differ from Pro7 (DeclInfo.pendingClass is TypeClass*, CBrowse_NewTemplateClass takes TypeClass*, CDecl_AddFriend's third parameter is Type*). (Since: PrepTokenBuffer is Pro7's TokenStream (tokens.h); DeclInfo has Pro7's member names where Pro7 has names (thetype, storageclass, section, is_extern_c, in_friend_decl, expltargs, file/file2/sourceoffset ...) and TODO-marked invented ones for Pro7's offset members; the f38/f3d/x54..x58/pad66 placeholders are gone.)
- Data: .data/.bss byte-identical (relink: links). No statics. No bindings for this object's data.
- Header: CTemplateClass.h declares only what other units use (checked by grep); the "own functions" block (11 statics declared extern, PSpecList, a TODO claiming Intrinsics depends on it: false, nothing changed) removed; `ctempl_templates` moved to CTemplateNew.h (Pro7); RegisterBaseClass returns void; parameter names Pro7's.
- Headers relied on: templates.h (all template types: TypeDeduce's x14-x17 have invented names with TODOs, Pro7's x19 missing; DefAction.xC placeholder; TemplateScopeState stand-in for DeclFucker); CDecl.h (DeclInfo: f38/f3d/x54-x58/pad66 placeholders, pendingClass typed TypeClass* where Pro7 has TemplClass*, ClassLayoutInput stand-in); CPrep.h (PrepTokenBuffer {count, SavedPrepToken*} stands in for TokenStream {tokens, TStreamElement*}); CScope.h (CScopeSave fixed here, see Open; ObjectList/NameSpaceObjectList mixing in CScope_GetLocalObject); CBrowse.h (CBrowse_NewTemplateClass takes TypeClass*); CTemplateTools.h (GetSelfRefTemplate(TypeTemplDep*), MergeArgNames(Type*, Type*), IsTemplate returns TypeClass*, ResolveMemberSelfRefs(TypeClass*): Pro7 types differ); CError.h (message enum lacks 2.6's 372+ ids; upstream has since added some); CTemplateClass.h itself still includes CInline/CPrec/CPrep/enode/objects more than it needs (not trimmed: include changes move other units' code). (Since: PrepTokenBuffer is Pro7's TokenStream (tokens.h); DeclInfo has Pro7's member names where Pro7 has names (thetype, storageclass, section, is_extern_c, in_friend_decl, expltargs, file/file2/sourceoffset ...) and TODO-marked invented ones for Pro7's offset members; the f38/f3d/x54..x58/pad66 placeholders are gone; CTemplateTools.h's GetSelfRefTemplate/ResolveMemberSelfRefs/IsSameTemplateType take Pro7's types; CError.h names every id to 392.)
- Open: the pqual fakematch; header stand-ins above; CTemplateClass.h includes objects/enode/CInline/CPrec/CPrep/CTemplateNew though it needs only common.h (trimming it breaks 14 units that rely on it transitively: GCCInlineAsm, CBrowse, CScope, CMangler, CObjCModern, SFPE, DWARF, IroUtil, FunctionCalls, InstrSelection, Intrinsics, CTemplateTools, CTemplateFunc, itself; a header-family job).
- Header round H2 (October 16): PrepTokenBuffer is Pro7's TokenStream; `pendingClass` is a TemplClass*; ClassLayoutInput is Pro7's ClassLayout; CBrowse_NewTemplateClass takes TemplClass*; CError.h names every id; CTemplateClass.h includes only common.h. TODO names: TypeDeduce x14-x17, DefAction.xC, TemplateScopeState (DeclFucker), DeclInfo. Pro7's unused `TypeDeduce.x19` is absent (pack(2) pads to the same 0x1a).

### src/frontend/CTemplateFunc.c: SOLID WITH TODOS
- Boundary: .text 0x565850-0x567e40, .data 0x5d16e8, .bss 0x5e3b68 (4 statics); 24 rows = Pro7's out-of-line functions; SetupTypeDeduce, CheckFuncInstance, GetSpecifiedType, DeduceTemplDepTemplate, DeduceTemplateType, DeduceType4, TemplateNestLevel inlined. Wii module CTemplateFunc has the same names. Relink: links.
- Flags: `-inline auto,deferred` (rows descend against Pro7's order: CanDeduceFunc highest), `-sym off` (unchanged).
- Functions: 24/24 exact, now Pro7's text (the old file mixed Pro7 with IDA text: MatchTemplFunc had GetSpecifiedType hand-inlined, magic `70U`/`0xF0000001`). Departures: assert lines +2 after line 160, explained by 2.6's `Boolean ok; ok = CDecl_CheckCtorIntegrity(...); CError_ASSERT(162, ok);` (2.6 change; exactly two lines, the plain `CError_ASSERT(162, f())` does not match); `paramCount++`/`args++` before the advance, `count++` in FuncMatch (catalogued); early `return FALSE` for Pro7's if/else in DeduceType1-3 (catalogued); `args2 == NULL` for `!args2`; `(index = f()) < 0`; DeduceFunc's inlined CheckFuncInstance takes `templ = GetFuncTempl(object1)` first; `modqual = qual2 = TPTR_QUAL(type2)` and `elements` computed first in DeduceTypeTemplDep; FuncIsAtLeastAsSpecialized counts into `count` then `for (i = count; ...)` with `info` before the quals (frame order); PartialOrdering has a local `count` separate from its (unread) parameter and `for (i = 0, object = NULL; ...)`; CExpr_DeduceFromObjList `size_t size` (catalogued) and no `obj` copy; MatchTemplFunc `if ((type = GetSpecifiedType(...)) && ...)`. All original's shape or needed; none is a fakematch.
- Behaviour: Pro7 text plus the CheckCtorIntegrity assert; runtime check as above (deduction, partial ordering, explicit specialization).
- Names: Pro7's. Invented (TODO): PartialOrdering's parameter `argcount` (Pro7 reuses the parameter as the local count).
- Types: header prototypes now Pro7's (TypeFunc* for CanDeduceFunc/DeduceFunc, NameSpaceObjectList* for FuncMatch/TemplateFunctionCheck; callers' casts removed in CDecl.c, CExpr2.c, CExprConvMatch.c). PartialOrdering keeps MatchLink* (the repo's stand-in for ObjectList, which ArgMatch.list uses).
- Data: statics `static`, 2-byte packed through the existing end-of-file `#pragma pack(2)` (TODO kept; proven needed: without it .bss is 10 bytes, the unit's 8). Deleted 2.6's bindings of the four statics (no lifted code; the object places them).
- Header: declares only other units' uses (CheckFuncInstance dropped: no user).
- Headers relied on: CExpr2.h (ArgMatch has a placeholder `SInt16 xE`; ArgMatch/ComparisonValues are Pro7's Match13/Match5 with invented names; MatchLink duplicates ObjectList; MemberCallArguments is Pro7's FuncMatchArgs); CDecl.h (DeclInfo, as above); CTemplateTools.h (MergeArgNames(Type*, Type*), as above); CPrep.h (PrepTokenBuffer); templates.h (DeduceInfo.nindex invented for x12C). (Since: PrepTokenBuffer is Pro7's TokenStream (tokens.h); ArgMatch.list is an ObjectList (MatchLink is gone) and xE is `userconversions`, TODO-named.)
- Open: the invented parameter name; header stand-ins.
- Header round H2 (October 16): ArgMatch's `xE` is `userconversions` (TODO), MatchLink is gone, PrepTokenBuffer is TokenStream, DeclInfo and DeduceInfo.nindex are TODO names. Still open: CTemplateTools.h's `CTemplTool_MergeArgNames(Type*, Type*)` (Pro7: TypeFunc*; kept because callers pass Type*, not shown blocked).
- Follow-up (October 16, after round H2): CTemplateTools.h's MergeArgNames takes Pro7's `TypeFunc*` (callers wrap in `TYPE_FUNC`, as Pro7's do; code unchanged): no longer pending.

### src/frontend/CTemplateNew.c: SOLID WITH TODOS
- Boundary: .text 0x530b90-0x5339a0, .data 0x5c0bf0, .bss 0x5e3598 (ctempl_parseparse, ctempl_scanfuncparams); 25 rows. 2.6's assert lines equal Pro7's throughout (228 ... 2422), so 2.6's file is Pro7's line for line: Pro7's dead MemberParseTempl, ParseTemplateMember and ParseMemberFunction are back (MemberParseLex survives inlined in MemberParseTemplArgList), GenMemberInstance, ParseTemplArgType, GetTemplateNestIndex, FindConversionFuncName, ParseConversionFunctionTemplate, SetupClassParamNameSpace are Pro7's statics, inlined. Removed: the invented `CTemplateNew_ShouldWarnUnsupportedInlineAsmRegister`, `TemplateObjectInstance_Source`, `CTempl_GenMemberInstance26`, `struct ParserPosition`, and unreachable 1.x code after a `return` in IsQualifiedMember. Pro7's three COMMON definitions (ctempl_templates, ctempl_templatefuncs, ctempl_curinstance) now live here as in Pro7 (adversarial round: moved out of CPrec.c and CError.c, whose "the original defines here" had no evidence; all 217 Matching sources still link, COMMON names fit hash order 317<319<319, 444<462<466, 2823<2851<=2851).
- Flags: was `-inline auto` in binary order; rows run InlineFunctionCheck (lowest) to Setup (highest), Pro7's order reversed: now `-inline auto,deferred`, Pro7's order. `-sym off` kept.
- Functions: 25/25 exact from Pro7's text. 2.6 changes (from the disassembly): no `copts.no_implicit_templates` (COpts has none; Pro7's first tests in GenFuncInstance, InstantiateMember, GenClassInstance dropped); an explicit instantiation (`flag`) sets `sclass = TK_EXTERN` in GenFuncInstance and `sclass = 0` in InstantiateMember where Pro7 sets Q_WEAK; Q_INTERRUPT accepted in ParseFunctionOrMemberTemplate's qualifier check. Needed shapes: ParseTemplArgExpr's inverted if/else; ParseParam leaves its `params` parameter unread and builds a local `param`, `startToken = tk; switch (tk)` (2.6 reloads tk); InstantiateMember/GenFuncInstance/Parse/ExplicitSpecialization/ParseMember declaration orders; IsQualifiedMember a `TemplClass* templ` local; ParseMember's `flag` hoisted and if/else for the last error; ParseFunctionOrMemberTemplate: NewDefAction-style split calls (`templfunc = Declare...; ParseTemplateFunction(templfunc, ...)` there and in the conversion helper), `|=` for CLASS_IS_CONVERTIBLE, OBJECT(di.resolvedObject) without Pro7's `tfunc` copy, early returns in the ctor/dtor/operator branches, `STORAGE_STATIC`/`STORAGE_EXTERN` enumerators (CDecl.h's; Pro7's TK_ compare differs), `object` declared after `templfunc`. Original's shape (kept, commented): ClassGetType's `pspecOwner = (owner = templ)->pspec_owner` (natural spellings tried); ExplicitSpecialization's `while ((int)tk == TK_TEMPLATE)` (the front end's token-compare idiom; plain, for(;;)+break, for and goto spellings tried). Fakematch (TODO): ParseTemplArgs' `UInt32* q = &arg->data.typeparam.qual` (2.6 keeps the pointer in edi across the inlined ParseTemplArgType; the other two callers match with Pro7's direct argument).
- Behaviour: as Pro7 plus the 2.6 changes listed; runtime check as above. Bug of ours fixed: `source_line` was a stand-in COMMON bound inside `symdecltoken` (+8 = tokenoffset), so the equivalent compiler wrote a variable nothing read; now `symdecltoken.tokenoffset` (Pro7) and the binding is deleted.
- Names: renamed by Pro7 and COMMON hash order: declaration_token -> symdecltoken (hash 222 between elipsis 188 and 1479), template_recordbrowseinfo -> gForceSourceLoc (1088 between no_descriptors 1063 and large_stack 1163; also Wii's name), non_type_template_argument_mode -> disallowgreaterthan (154 between IRO_IsModifyOp 151 and cscope_is_member_func 198; the old name, 732, did not fit). Invented (TODO): ParseMemberFunction's unused parameters arg1-arg3 (Pro7's decompilation has placeholders).
- Types: Pro7's; header prototypes Pro7's for IsQualifiedMember (Type*) and Parse (TemplClass*), caller casts in CDecl.c adjusted. Casts remain where repo headers differ (CScope_GetLocalObject returns ObjectList*, DeclInfo.resolvedObject is ObjBase*, CDecl_NewTemplDepType returns TypeTemplDep*).
- Data: byte-identical (links). The three COMMONs defined here (Pro7). Statics `static`; end-of-file `#pragma pack(2)` restored (needed: ctempl_parseparse is 2-byte aligned in 2.6's .bss; TODO). Raw message id 378 (CError.h's enum has no entry for it in this tree).
- Header: CTemplateNew.h declares the three template globals (Pro7) and only functions other units use.
- Headers relied on: CDecl.h (DeclInfo field names mapped by offset from the old file: resolvedObject/resolvedObjects/templateScope/allowTemplateArguments/hasTypename/isConstructor/parserOption/isType/missingTypeSpecifier are invented for Pro7's x10/x14/fucker34/x51/x53/x4B/x4A/x49/x48); CScope.h (GetLocalObject returns ObjectList*; ScopeSearch for Pro7's CScopeObjectIterator; CScopeParseResult for NameResult); CTemplateTools.h (as above); CPrep.h (PrepTokenBuffer; gForceSourceLoc declared `char`, Pro7 Boolean); CExpr.h (disallowgreaterthan declared `char`); CParser.h (symdecltoken); CError.h (missing 2.6 ids such as 378); templates.h (TemplateFunction.original invented for unk4). (Since: PrepTokenBuffer is Pro7's TokenStream (tokens.h); CScopeParseResult is Pro7's NameResult; CError.h names every id to 392, 378 included.)
- Open: the ParseTemplArgs fakematch, the invented parameter names, header stand-ins.

Shared edits made for these units (first reviewer) (only GC_2_6 was built and checked): include/compiler/CScope.h `CScopeSave` loses its invented `trailingPadding[3]` (Pro7's 13-byte struct; CopyFriend's frame needs it; no function anywhere changed); tools/rename.py renames above (CBrowse.c, CExpr.c, CFunc.c, CInline.c, CParser.c, CPrep.c, CExpr.h, CParser.h, CPrep.h, every version's bindings.json/symbols.txt); cast removals/additions at callers in CDecl.c, CExpr2.c, CExprConvMatch.c; config/sources.json (two units deferred); config/GC_2_6/bindings.json (five bindings deleted); generated splits/symbols/state for GC_2_6. Result: 4020/4077 exact (no function lost), 217/217 Matching sources link, equivalent compiler builds.

- Adversarial review: attacked the "original's shape" claims, the two fakematch TODOs, the boundary/assert evidence, the COMMON placement, names (Wii, hash order), the header users, placeholder fields and magic numbers. Broke: the three template COMMONs were defined in CPrec.c/CError.c on an unevidenced "the original defines here"; moved to CTemplateNew.c where Pro7 defines them (relink: 217/217 still link; 4020/4077 exact, nothing lost or gained; equivalent compiler builds). Also found unreported header problems (CExpr2.h `ArgMatch.xE`; CTemplateClass.h's surplus includes that 14 units rely on). Held: ClassGetType's chained `(owner = templ)->pspec_owner` (8 natural spellings: ternary, `if (!(owner = ...))`, separate local in three orders, walker-only tests; best 94.4%); CopyBaseClasses' `pqual` (Pro7 verbatim, block-local scan/qual, separate templbase, qual first: 79.5-93.0%); ParseTemplArgs' `q` (plain, Type* temporary, separate qual, arg copy: 91.8-92.9%); ParseParam's local `param` and `startToken = tk; switch (tk)` (Pro7's parameter reuse and switch assignment: 89.6-98.2%); PartialTemplateArgMatch's reused `i` (gc1 has the counter); CTemplateNew's assert lines equal Pro7's (all 0 differences, dead functions included), CTemplateFunc's +2 after 162 and CTemplateClass's +3/+11 constant across regions; every header declaration has a user outside its unit; no bindings left inside the three objects' .data/.bss; no placeholder fields or magic constants in the sources; clang_check and clang-format clean. Grade lowered to PENDING HEADERS: the DeclInfo/templates.h/CExpr2.h/CPrep.h stand-ins listed above are real and open.
- Header round H2 (October 16): resolved: PrepTokenBuffer, NameResult, CScope_GetLocalObject, `gForceSourceLoc`/`disallowgreaterthan` Boolean, CError ids (378); TODO names: DeclInfo, TemplateFunction.original. Still open: CTemplateTools.h's `CTemplTool_MergeArgNames(Type*, Type*)` (Pro7: TypeFunc*) and `CTemplTool_IsTemplate` returning TypeClass* (Pro7: TemplClass*).
- Follow-up (October 16, after round H2): CTemplateTools.h's MergeArgNames (`TypeFunc*`) and IsTemplate (`TemplClass*`, compared with `TEMPL_CLASS(tclass2)`) take Pro7's types; code unchanged: no longer pending.

### src/frontend/CParser.c: SOLID WITH TODOS
- Boundary: 89 rows 0x4e3e10-0x4ea490, Pro7 CParser.c's functions in Pro7's order (reversed in the binary), every row's Pro7 name; Wii's CParser module has the same names (IsReadOnly included). The file is now Pro7's text in Pro7's order (was IDA/gc1 text with `lift_value_0`, `save`/`restore` copies of the try-block helpers, an `fn_0048be40_inline1` stand-in for CParser_FindOverloadFunc, the AltiVec helper moved to the top). Pro7's dead CParser_FindPublicRuntimeObject, CParser_TryFuncDecl, is_cfm_type, CParser_IsVTableObject are kept (marked; global, so the link drops them): the asserts after FindPublicRuntimeObject (379, 387, 534 ... 1748) carry Pro7's line numbers exactly. 2.6 additions in place: CParser_ClassHasMutableMember + CParser_IsReadOnly between GetCVTypeQualifiers and IsConst (address order). .data/.bss link byte for byte.
- Flags: `-O4,p -inline auto,deferred -sym on` (pro53 = pro6). Rows descend against Pro7's order (cparser lowest), so deferred; `-sym off` gives 71/89, so `on` is shown.
- Functions: 89/89 exact (was 89/89 on IDA-style text; now 47/89 straight from Pro7's text, the rest by the departures below). Departures from Pro7:
  - 2.6 changes (each in the disassembly): NewRTFunc calls CodeGen_UpdateObject; UpdateObject copies `reginfo` (DDATA) and `interruptflags` (functions); is_unsigned calls CMach_IsUnsignedType; GetWCharType returns stunsignedshort in C; getthetype and GetDeclSpecs take the target's type keywords 0x113-0x11A (CMach_GetCustomType); ParseDeclaration allows `register` (global register variables: error only after scandeclaratorlist when no `reginfo.reg`); ParseAttribute is 2.6's (comma lists, `section`, no packed/unused/format/mode, `asm` register after the list); IsReadOnly/ClassHasMutableMember new; GetUniqueName's buffer is 16 bytes; assert lines after 1748 are +49/+50.
  - needed shapes (catalogued; the plain Pro7 spelling compiled and failed each time): `(int)tk`/`(int)typetoken`/`(int)di.storage` compares (isdeclaration, islookaheaddeclaration, GetDeclSpecs, ParseGlobalDeclaration); chained assignments (`typesize = signedness = typetoken = 0`, `args = arg = CParser_NewFuncArg()`, `dst = buf = ...`); early `return`s where Pro7 has if/else chains (SetCFMFlags, ParseDeclSpec, ParseDeclaration's cases, ParseLinkageSpecification's nested `extern "C"`, the final error of ParseAttributeFunctionSummary, `if (!b) return TRUE; return FALSE;` in is_arglistequal); `|=` for `x = x | F` (SetCFMFlags, ParseDeclSpec, NewCallBackAction); `x != 0 &&` spelled out (is_typeequal, iscpp_typeequal size test); `(b & Q) == 0` for `!(b & Q)` (the CV-qualified pair); `% 10` in PrintUniqueID (Pro7's expanded `id - id/10*10`); `while (list) { s = list; ...` (FreeLocalHeap); declaration order (try block before DeclInfo in TryIsDeclaration/TryParamList/ParseTypeID; `sizet` before `exspecs`); is_memberpointerequal advancing its locals before the call; ParseObject as one `&&` condition with no `obj` temporary (the old source's `(object = ...)->qual` argument-order trick is gone).
  - original's shape: CheckAnonymousUnion's `break` + `default:` warning (same behaviour as Pro7's return); FindDeallocationObject's `if ((obj = OBJECT(pr.object)))` (obj not read before the loops reassign it: a leftover; reading it in `mylist.object` fails, 98.7); oldstylecompatible's `default: break;` (keeps it out of line: 2.6 calls it, 15-statement auto-inline limit); ParseDeclaration's `(Boolean)(di->storage == TK_AUTO)` (2.6 materialises the test as Pro7 did its `(a || b) != 0`; `== TRUE`, `!= FALSE`, `!= 0` fail).
  - fakematch (TODO in source): UpdateObject's `((volatile DeclInfo*)declinfo)->extraQualifiers` (2.6 compares the section in memory and reloads it; tried `!= 0`, nested ifs, a cast, the field as UInt16/short/16-bit enum). Down from three volatiles (NewFunctionObject's `volatile DeclInfo*` parameter and NewGlobalDataObject's volatile local are gone: they inline UpdateObject). IsReadOnly's `!CParser_ClassHasMutableMember(...) == FALSE` in the `TYPECLASS` test (TODO: 2.6 materialises the negation in a register of its own; this replaced the reviewer's reuse of the unrelated `qual` as that temporary; a local of its own 91%, `!= FALSE`, a ternary, `!!`, an early return tried).
- Behaviour: all exact, references resolved, so constants/members/callees are the original's. Genuine original quirks kept: HasInternalLinkage(2) write Q_IMPLICIT_WEAK through a const object; ParseTypeID/TryIsDeclaration return through longjmp state. Bugs of ours fixed: none in code; data: `symdecltoken` (Pro7's) was CFunc's `declaration_token` with a separately bound fragment `source_line` at +8 (its `tokenoffset`, written by CTemplateNew) - now one variable, binding deleted in every version.
- Names: rows Pro7/Wii. COMMONs renamed by Pro7 + hash order: `DAT_0058852e` -> `dont_set_references` (3436 between 3410/3495), `pending_object_classes` -> `callbackactions` (3263 between 3260/3269; Pro7's CallbackAction type replaces `struct PendingObjectClass`), `non_type_template_argument_mode` -> `disallowgreaterthan` (154 between 151/198), `declaration_token` -> `symdecltoken`. Invented: CParser_ClassHasMutableMember (TODO; Wii has it as a nameless static). Locals are Pro7's (Ninji's r23/r24/r28 kept as the reference's).
- Types: Pro7's prototypes throughout (Boolean returns, `short` comparisons, `SInt32* index` in IsVirtualFunction with its two callers' locals made SInt32, `Type**` GetVectorDeclSpec, `TemplClass*`); `unsigned char filename[256]` for Mac's Str255 (no SDK typedef in the tree); NameResult is the repo's CScopeParseResult.
- Data: relink links (.data and .bss byte for byte). Statics `static`, in Pro7's order (`uniqueid` Pro7's SInt32). Bindings deleted for the five statics (no lifted user). COMMON definitions moved to CParser.c where Pro7 defines them: newp/newa/delp/dela_fobj (CDecl.c), rt_ptmf_cast/cmpr/test/scall/scall4 (CExpr.c), rt_ptmf_null and Xecth_func (CodeGen.c), Xgreg_func (CInit.c), Xthrw/Xunex_func (CException.c), symdecltoken (CFunc.c), and the undefined cnar/dnar/dnar3_func, name_mangle_list, clear_func, illegalimplicitconversion; all affected units relink as before. Not added: Pro7's `inassembler`, `string[256]`, `compilererr*` (unreferenced; adding an unreferenced COMMON broke the link, see CBrowse).
- Header: (adversary: `dont_set_references` removed: only CParser_Setup touches it, as in Pro7) rewritten as Pro7's CParser.h (ParserTryBlock, CallbackAction, ATOM_ enum, Pro7's variable and function lists), filtered to what other units use; dropped duplicates and stand-ins (`DAT_0058852e`, the doubled prototypes, `volatile DeclInfo*` parameters, CParser_FindClassMemberOrNamespaceFunctionObject which nothing defines, the `CPARSER_PARSE_OPERATOR_NAME` alias, CParser_GetVectorDeclSpec/ParseDeclaration/getthetype now static as in Pro7). CMachine's st* types, `tk` and the TK_ token numbers moved to their headers (CMachine.h, CPrep.h, tokens.h).
- Headers relied on: tokens.h (TK_ numbers, TStreamElement, FileOffsetInfo: TK_UNK_113..11A are placeholders, 0x115 is `__vec2x32float__`); CDecl.h DeclInfo (since renamed to Pro7's names: `thetype`, `storageclass`, `operator_token`, `exportflags`, `expltargs`, `has_expltargs`, `file`/`file2`/`sourceoffset`; invented/placeholder fields `f38` union, `f3d`, `x54`-`x58`, `pad66`; `extraQualifiers` is Pro7's `section` (2-byte in 2.6); `requireMangledName` is Pro7's `is_extern_c`; `allowForeignNamespace` is `in_friend_decl`; `resolvedObject` is `ObjBase*` where Pro7 has `Object*`; `sourceLine` is an offset); CScope.h CScopeParseResult (open: `struct { Type* base; } type` wrapper, `x1F`/`x21` placeholders); CError.h (open: no names for 375, 385); CTemplateTools.h (CTemplTool_IsSameTemplateType takes TypeClass*/TypeTemplDep* where Pro7 takes Type*); CompilerTools.h (COS_FileGetFSSpecInfo `void*` name; COS_FileGetPathName takes a `CPrepFileInfo*` where Pro7's cos.h has `const FSSpec*`, so CPreprocess passes `prep_file` for `&prep_file->textfile` and IroDump/CPrep cast `(CPrepFileInfo*)&...filespec`; CPrep.h's CTool_GetPathName has the same stand-in, Pro7 `const FSSpec*`: callers in CPrep, CError, ObjGen cast; one cross-unit fix, left to the owners); CodeGen.h (fixed: CodeGen_ReInitRuntimeObjects returns Boolean, as Pro7 and the code); CPrep.h, CMachine.h, objects.h, types.h, enode.h, scopes.h, templates.h, CExpr2.h/CInit.h/CException.h (duplicate externs removed). Private copies of CParser.h's variables remain in other headers: `errorreturn`, `trychain` (CError.h), `stillegal`, `no_name_node`, `this_name_node` (CDecl.h), `self_name_node` (CObjC.h) (`sttemplexpr` left CExpr.h in main's header work). BitVector.h is needed by clang for CParser.c (219 errors without it): some type lives in the wrong header. (Since: DeclInfo has Pro7's member names where Pro7 has names (thetype, storageclass, section, is_extern_c, in_friend_decl, expltargs, file/file2/sourceoffset ...) and TODO-marked invented ones for Pro7's offset members; the f38/f3d/x54..x58/pad66 placeholders are gone; CScopeParseResult is Pro7's NameResult, its `qual` Pro7's, the other members TODO-named; CError.h names 375 and 385; CTemplTool_IsSameTemplateType takes Pro7's Type*/Type*; TK_UNK_115 is `TK_UU_VEC2X32FLOAT_UU` (TODO-named).)
- Open: the two TODO workarounds above and the invented helper name; the TODO-named DeclInfo/NameResult members.

### src/frontend/CPreprocess.c: SOLID WITH TODOS
- Boundary: 7 rows 0x514c50-0x516230: Pro7's five functions plus 2.6's two directive-dump functions (called by CPrep's `#pragma` handling), in reversed source order; .data 0x5be348 and the 8-byte .bss link. Wii's module has a redesigned preprocessor (callbacks), so no Wii names for the two.
- Flags: deferred (addresses descend against source order); `-sym off` gives 6/7.
- Functions: 7/7 exact. Departures from Pro7: 2.6 changes: "\r\n" line ends (NewLine, FileInfo, Preprocess), FileInfo's newline test (`pplist.size > 0 && ((nlflag && flag) || last byte != '\n')`), the `__vec2x32float__` keyword, the two directive functions. Needed shapes: `int innertoken` (Pro7's short: 98.4), `startToken = endToken = 0` (99.0 split), `if (token) continue;` (99.9 Pro7's else-if), FileInfo's local order (Pro7's: 6/7). Original's shape: `"\r\n"[strlen("\r\n") - 1]` (a newline macro, catalogued). Pro7's start/end declaration order adopted (it matches). A literal tab in a format string written `\t`.
- Behaviour: exact; no bugs found.
- Names: Pro7's; invented with TODO: CPrep_PreprocessDumpDirective, CPrep_PreprocessDumpDirectiveEnd, statics pp_text_start, pp_text_depth (their bindings deleted: no lifted user).
- Types: Pro7's; Str255 as `unsigned char[256]`.
- Data: links; statics `static` in 2.6's order (pp_text_start lowest).
- Header: now Pro7's CPreprocess.h plus the two 2.6 functions; `pplist` (CPrep's variable) moved to CPrep.h.
- Headers relied on: CPrep.h (pplist, filesp, nlflag, prep_file, pos, macropos, cprep_nostring: fine), tokens.h (TK_UNK_115 placeholder for `__vec2x32float__`), CMachine.h (stwchar), CompilerTools.h (GList; the COS_File* prototypes above), CParser.h, CPrepTokenizer.h, CError.h.
- Open: invented names (no source to take them from).

### src/frontend/CBrowse.c: SOLID WITH TODOS
- Boundary: 21 rows 0x5133f0-0x514c50 = Pro7's CBrowse.c functions (Wii's module confirms the public ones); .data 0x5be338 (gFromAccessType + literals) and .bss 0x34 (three GLists + an int, as Wii's 0x34 contribution) link.
- Flags: deferred (Setup highest); `-sym off` gives 19/21.
- Functions: 21/21 exact, now from Pro7's text: 15 matched verbatim. Departures: BeginClass, AddClassMemberFunction, RecordFunction, NewData keep 2.6's shape, which is 1.3's (gc1: a second buffer for base names and `buf++` before `i = -1`, a `for` count loop; early `return` + nested `!(FUNC_AUTO_GENERATED)` + `id = next++; funcid = id`; separate `linkid`/`nameid` locals; RecordFunction's name handling); EmitStandardData's `item` is `enum EBrowserItem` (Pro7's int: NewData 97.5); RecordFunction keeps `TypeMemberFunc*` (gc1's). gFromAccessType is Pro7's `enum EAccess` table (byte-sized enum: same bytes, relink links), not a stand-in UInt8 table.
- Behaviour: exact; no bugs.
- Names: renamed to Pro7's: data_00581ba8 -> gBrowseData, browse_member_list -> gClassData, browse_function_buffer -> gMemberFuncList, nextFunctionId -> gNextMemberFuncID (Pro7's int), data_005884f5 -> gUseNameTable (hash 70 fits 14..151), template_recordbrowseinfo -> gForceSourceLoc (hash 1088 fits 1063..1163; CTemplateNew and CPrep use it as Pro7 does), its definition moved from CPrep.c to CBrowse.c as Pro7's Boolean (was `char`). Pro7's third COMMON gUseTokenStreamSource is not in 2.6: defining it breaks the relink (COMMON shifted at SetOptFlags).
- Types: header prototypes are Pro7's (SInt32 offsets, TemplClass* for NewTemplateClass: CTemplateClass's caller now passes `templ`).
- Data: links; statics `static` in Pro7's order; no bindings for them.
- Header: Pro7's CBrowse.h (only gForceSourceLoc exported: gUseNameTable has no other user); include of CodeGen.h trimmed.
- Headers relied on: CDecl.h DeclInfo (file/file2/sourceoffset, Pro7's names since the DeclInfo rename; see CParser), CPrep.h Macro (`flag` is Pro7's `is_special`), templates.h (TEMPL_CLASS_INST), CMangler.h, Unmangle.h, CExpr2.h, CError.h, driver/CWSecretPluginCallbacks.h: no other problems found. (Since: Macro's `is_special` and `names[0]` are Pro7's; text/nargs/isExpanding are TODO-named for Pro7's c/xC/xF.)
- Open: the TODO-named DeclInfo/Macro members (Pro7 has only offset names for them).
- Adversarial review: attacked every TODO and unusual spelling with `fn.py variants`, the boundary against Wii's module tables, header users, the invented names, and duplicate declarations. Broke: IsReadOnly's reuse of `qual` as a temporary (a fakematch: `!f() == FALSE` in one `&&` test matches, no reused variable; kept as a TODO since 2.6's materialised negation is still unusual); CParser.h's `dont_set_references` extern had no user outside CParser.c (removed); headers carry private copies of CParser's variables and the COS_FileGetPathName/CTool_GetPathName `CPrepFileInfo*` stand-in (reported, not fixed: shared headers and five other units). Held: UpdateObject's volatile (plain, `!= SECT_DEFAULT`, nested, `(int)`, a pointer copy, and DeclInfo's field as a 16-bit enum all 94.7 and break the two inlining callers); FindDeallocationObject's dead `obj =` (`pr.object`, `!= NULL`, reordered stores 92.0; a separate `obj = ...; if (obj)` also matches, same story); ParseDeclaration's `(Boolean)(storage == TK_AUTO)` (`!= 0`, `|| 0`, `(int)`, a ternary 96-97.8; a block-local Boolean also matches: no plainer spelling); the boundary (2.6's CParser/CBrowse/CPreprocess orders agree with Wii's modules; Wii's CPrep_PreprocessDump* neighbours are 4.x callbacks of another design, so the invented names stand; Wii's IsReadOnly 107 bytes is 2.6's 0x4e7630, the 168-byte static after it the helper); every other header declaration has an outside user. After the changes: 89/7/21 exact, nothing lost anywhere (compare_start.json), layout.py, all three relink, clang_check and clang-format clean.
- Header round H2 (October 16): DeclInfo's and Macro's remaining members are TODO-invented names for Pro7's offset members (CDecl.h, CPrep.h); CExpr2.h is merged; CWSecretAttachHandle takes Pro7's Handle. Resolved as TODO names.

### src/frontend/CScope.c: SOLID WITH TODOS
- Boundary: 0x4d6de0-0x4db74a, 68 rows = Pro7 CScope.c's 86 functions in reverse order. The 18 without a row are 16 statics 2.6 inlines everywhere, plus CScope_IsStdNameSpace and CScope_FindObject, which are dead but kept: removing them changes the neighbouring code and the relink fails. Wii's CScope module and docs/names-2.6.md agree. The relink takes the whole object (.text/.data/.bss byte for byte).
- Flags: `-inline auto,deferred`: the rows descend against Pro7's order (Setup is at the highest address). `-sym on`: with `-sym off` 47/68 are exact. Compiler pro53, `-O4,p`, like the other front-end units.
- Functions: 68/68 exact. Rebuilt from Pro7's text verbatim; 49 matched as is. Departures:
  - 2.6 change: CScope_AmbigNameError reports error 188 with no argument (Pro7's extra `nspace2` arg broke 3 functions).
  - Shapes from AGENTS.md's catalogue: `if (f) return TRUE; return FALSE;` for `return f() != 0` (FindQualifiedTag); early returns (AddUsingObject); `if (!(list = f()))` (ParseNameSpaceAlias); `classname == name` (ParseDeclName); `== NULL` for `!` (IsEmptyNameSpace); `(int)` casts on a short token, plus one walker reused where Pro7 has newtype2 (DependentTemplateMember); parameter `bcl` reused (AddUsingObject); declaration order (GetType, GetTagType, PossibleTypeName, BuildNameSpaceLookupList, FindLookupName, RecFindClassMember, GetAccessPathRec).
  - Original's shape: a `for` loop with an `SInt32` counter in IsEmptySymTable (Pro7's do/while and `int` both fail); `!is_static` (Pro7's `== 0` fails); a ternary allocation in CScope_InsertName, which brings it under the 15-statement auto-inline limit (2.6 inlines it into AddObject).
  - Fakematch, TODO: CScope_AddObject (the materialised `otype == OT_OBJECT && IS_TYPE_FUNC` value in 2.6's code is an inlined Boolean, so a Boolean assigned in the condition is close to the original; adversary also tried the test split out of the `||`, a FALSE-initialised flag, reusing isExistingFunction (also exact, no better), and four inline ObjectIsFunction bodies with goto/return: 85-98.6). The old body was a goto mess. It is now Pro7's control flow with a `UInt8 otype` local, a separate walker and a cast ternary. Still left: two Boolean locals (one, `isFunction`, assigned inside a condition and never read) and a `goto done` after the 739 block. Tried: Pro7's CScope_ObjectIsFunction calls, 95.6; `return`, 98.6; no dead assignment, 94.3; variants of ObjectIsFunction's body. Possibly tied to `ObjBase.otype` being UInt8 where Pro7 has an enum (see Headers).
- Behaviour: every function is exact with relocations resolved, and the unit links. No bugs found in the original. Fixed in ours: CScope_DefineTypeTag read the global as `member_access`; Wii confirms the name `global_access` (`bindings.py refs --wii`), and it is now Pro7's `AccessType` (CDecl.h/.c; the `(AccessType)` cast in CDecl is gone).
- Names: functions are Pro7's and Wii's. Pro7 placeholders replaced, each with a TODO: `Lookup_0..2` became Lookup_Object/Tag/NameSpace; FindLookupName's register-named locals r27-r31/r6 became result/cand/scan/resultnspace/copied/check; NSIteratorInit/Next stay Pro7's "assumed" names. Stale comments removed (Pro7's register notes, "not 100% sure").
- Types: the iterator is Pro7's `CScopeObjectIterator {nspace, nextname, currlist, int hashindex}`, replacing CSOM.h's stand-in `ScopeSearch {owner, nextName, nextObject, bucketIndex}`; renamed in every user and moved to CScope.h. CScope_NextObjectIteratorObject returns Pro7's `ObjBase*` and CScope_GetLocalObject returns `NameSpaceObjectList*` (they had been `Object*` and `ObjectList*` stand-ins, with casts at callers). `CScopeSave` has Pro7's fields (current, currentclass, currentfunc, is_member_func) and 14 bytes at pack(2): the `trailingPadding[3]` placeholder is gone. `CScopeParseResult.type` is a plain `Type*` (the `struct { Type* base; } type` stand-in is gone, 88 uses), and x21 is now `is_conv_operator`. ELookupType is Pro7's (parameter and static).
- Data: .data/.bss byte-identical (relink). The statics are `static`, in reverse of Pro7's order (2.6 has cscope_isambig lowest), with `#pragma pack(2)` at the end for their 2-byte offsets. Pro7's COMMONs (cscope_is_member_func, currentfunc, currentclass, current, root) are now defined here; BE_elf.c's stand-in definitions are deleted (one was `UInt8 cscope_is_member_func`). The 7 static bindings are deleted from GC_2_6 bindings.json.
- Header: CScope.h declares only what other units use: 23 prototypes of Pro7 statics are gone, and NameSpaceLookupList and CScopeNSIterator moved to CScope.c's top. Externs are grouped. Includes were set to Pro7's list, then trimmed (`includes.py trim`); `<string.h>` stays for strcmp.
- Headers relied on:
  - CScope.h: CScopeParseResult's flag members are invented (TODO); `x1F` is still a placeholder (no user in Pro7 or 2.6); the type name is the repo's, where Pro7 calls it NameResult.
  - scopes.h: Pro7's shapes, no problems.
  - objects.h: every `otype` is `UInt8` where Pro7 has the `ObjectType` enum; it may explain the AddObject TODO.
  - CDecl.h: DeclInfo has invented names and placeholders (`f38[4]`, `f3d`, `x54..x58`, `pad66`); only `global_access` was fixed here. (Since: DeclInfo has Pro7's member names where Pro7 has names (thetype, storageclass, section, is_extern_c, in_friend_decl, expltargs, file/file2/sourceoffset ...) and TODO-marked invented ones for Pro7's offset members; the f38/f3d/x54..x58/pad66 placeholders are gone.)
  - types.h, templates.h, enode.h: one placeholder each (`TypeObjCID.x12`, `DefAction.xC`, `unk08[4]`).
  - CError.h: gained ERR_ILLEGAL_USE_ABSTRACT_CLASS_TYPE, ERR_ILLEGAL_TEMPLATE_PREFIX and ERR_TEMPLATE_PARAMETER_ARGUMENT_LIST_MISMATCH (372-374, texts from the binary's table) in place of the raw 373/374.
  - CClass.h, CPrepTokenizer.h, CTemplateClass.h, CTemplateTools.h, CompilerTools.h: not reviewed beyond what is used here.
- Open: the CScope_AddObject TODO; the invented names above (no source to take them from).

### src/frontend/CException.c: SOLID WITH TODOS
- Boundary: 0x51ff40-0x522fb3, 41 rows = Pro7 CException.c's 52 functions in reverse order. The 11 without a row (IsSubList, FindLocalObject, DestroyLocal, DestroyLocalPointer, DestroyLocalArray, EndCatch, TypeID, MightNeedDtor, CheckTryObjects, InsertPrevStatement, GetETEMPObject) are inlined or dead, as in Pro7. The old file's invented helpers (CException_StdType, `_004e2c40_inline1/2`, object_statement, finish_label, NewTypeNode ...) are gone. The relink takes the whole object.
- Flags: deferred: rows descend, Setup is highest. `-sym on`: 21/41 with `-sym off`. Non-deferred also compiles 41/41 exact, so deferred rests on the address order alone.
- Functions: 41/41 exact. Rebuilt from Pro7's text (21 matched verbatim). 2.6 changes:
  - TempTransform finds the last statement of a serialized expression (new loop, assert 2428) and dtor-transforms from it; this is also why 2.6's assert lines after it are Pro7's +10 (`update_assert_lines --write`).
  - MangleClass `char buf[16]` (Pro7 20; frame size).
  - CanThrowCheck tests `cexcept_canthrow` after each search, and both outcomes continue the loop. Written as `if (cexcept_canthrow) continue;` in a `for`: original's shape, an evident oversight (meant to stop the scan early). Pro7's form, `break` or no test give 96.4.
  - ScanTryBlock allocates `sizeof(CatchBlock)` (28; Pro7's text has `sizeof(ExceptionAction)`) and reports error 135 for a missing '{' (Pro7's text has 123). Both read from the pushes.
- Needed shapes, all from AGENTS.md's catalogue: early returns (CompareSpecifications, MangleNameSpaceName); parameter reuse `dtor = CABI_GetDestructorObject(dtor, ...)` (RegisterDestructorObject, RegisterLocalArray); a ternary for DestroyLocalArray's dtor expression (inlined into ActionCleanup); if/else for Pro7's call ternary (GetBaseClassList); a `TypeClass*` parameter (CallCopyCtor); `(x & F) == 0` as an argument (HasFuncCallCallBack); counter before advance and a `for` over the specs (InsertSpecificationActions, TempTransFuncCall); a separate `ea_catch` local (InsertSpecificationActions); `(ea = iter->dobjstack)` in the condition and `iter = cexcept_prevstmt = stmt` (CleanupExceptionActions); sequential expression building (TransNewException); declaration order (TempTrans_ETEMP, TransInitTryCatch, TempTransFuncCall, PatchConstructorAction); ScanTryBlock now has Pro7's declaration order.
- Original's shape (no fakematch left): ScanTryBlock builds the ST_BEGINCATCH statement in `stmt` and copies it to `beginCatchStmt` (Pro7's register notes give both r14), and passes its first label through `catchEndLabel` (a reused variable). The invented `CExcept_AppendBeginCatch` helper that returned the statement as an `SInt32` is gone (adversarial review). Without the `catchEndLabel` reuse 97.0-99.8 (seven spellings).
- Behaviour: everything is exact and links. The two ScanTryBlock fixes above are errors in Pro7's text, not 2.6 changes we invented; no bugs found in the original apart from the CanThrowCheck oversight.
- Names: Pro7's throughout (functions, statics, CatchBlock, BCL, DtorTemp, UniqueObj). Invented: only the local `ea_catch` (a descriptive local name).
- Types: Pro7's: `Boolean cexcept_hasdobjects`/`cexcept_magic` (were UInt8/SInt8), `Boolean` flag parameters for RegisterDestructorObject and ScanTryBlock (were int/char), Boolean ActionNeedsDestruction. Every user's code is unchanged. The stand-in structs ClassNode, ECacheNode, TemporaryObject and ExceptionHandlerRecord are gone, along with the `mac68k` option, the `CE_ASSERT` macro, magic case numbers and the private CInline_SerializeStatement prototype (now declared in CInline.h).
- Data: byte-identical (relink). Pro7's statics are in Pro7's order (2.6 has cexcept_uniqueobjs highest), with `#pragma pack(2)` at the end. Pro7's COMMONs cexcept_dobjstack/hasdobjects/magic are defined here. Xthrw_func and Xunex_func moved to CParser.c (Pro7's home) with Xthrw_func's extern in CParser.h. The 9 static bindings are deleted from GC_2_6 bindings.json.
- Header: CException.h is Pro7's minus what no other unit uses (cexcept_magic, IsSubList, Xthrw_func). It still includes Exceptions.h, which it does not need for itself: 7 units (IroUtil, IroVars, CError, CFunc, CInline, TOC, CException) get ExceptionAction from it, so it stays until they include it.
- Headers relied on:
  - Exceptions.h: ExceptionAction and EAT_*; the field types (`type` is a byte) were not reviewed.
  - CFunc.h: Statement, DeclBlock, DeclThing; `LoopInfo` keeps Pro7's x0/x4/x8.
  - CDecl.h: DeclInfo's stand-in names and placeholders, as above; `missingTypeSpecifier` at Pro7's x48 is taken on trust. (Since: DeclInfo has Pro7's member names where Pro7 has names (thetype, storageclass, section, is_extern_c, in_friend_decl, expltargs, file/file2/sourceoffset ...) and TODO-marked invented ones for Pro7's offset members; the f38/f3d/x54..x58/pad66 placeholders are gone.)
  - CInit.h: placeholders `unk08`, `unknown9`.
  - CInline.h: pad0b/pad15/pad20/pad23/pad26/pad54; gained the CInline_SerializeStatement prototype, which CInline.c also declares privately (line 201).
  - CExpr2.h: `xE` placeholder; it now includes CScope.h for CScopeObjectIterator.
  - CSOM.h: `pad36[0x0a]`; lost ScopeSearch.
  - CPrep.h: unknown66, unknownfd, unkfield123.
  - CParser.h: Xthrw/Xunex externs (now in the Pro7-shaped CParser.h of the CParser review).
  - CABI.h, CClass.h, CMachine.h, CMangler.h, CompilerTools.h, CError.h: no placeholders found; not reviewed further.
- Open: the headers above (DeclInfo, CInit.h, CInline.h, CExpr2.h, CSOM.h, CPrep.h placeholders).
- Adversarial review: attacked both TODOs, the boundary rows, ScanTryBlock, TempTransform and CanThrowCheck against IDA (pushes, error ids 135/114/115/242/252, asserts 2428/2435/2439/2466/2698, the double-target `jnz/jmp` of CanThrowCheck's test is real), RecFindClassMember and AmbigNameError against Pro7, COMMON names against Wii (`cscope_*`, `cexcept_*`, `global_access`, `Xthrw_func`/`Xunex_func` all Wii publics), header users (every CScope.h/CException.h declaration has a user outside its unit), clang_check (0). Broke: CException's ScanTryBlock fakematch (the invented SInt32 helper: the natural `stmt = ...; stmt->expr = ...; beginCatchStmt = stmt;` with Pro7's declaration order matches; CException now SOLID PENDING HEADERS); CScope's Flags line named the wrong compiler (pro53, not pro6). Held: CScope_AddObject's TODO (13 more spellings tried, none exact but the reviewer's and an equivalent reuse of isExistingFunction), the rest. Note: CScopeParseResult's `is_destructor` (Pro7 x1C) is set when CScope_CheckDtorName reports a non-class (pseudo) destructor name, not for every qualified destructor; the name is TODO-marked invented. compare: 4020/4077 exact before and after, nothing lost; both units relink.
- Header round H2 (October 16): Exceptions.h is Pro7's ExceptionAction; CInit.h's OLinkList, CExpr2.h merged, CSOM.h's pad gone, CInline.h's pads gone and its private CInline_SerializeStatement prototype removed; COpts is CParser.h's. TODO names: DeclInfo, CFunc.h's LoopInfo, CInlineBackendOptions, CSOMStub.kind, COpts' `unknown66`/`unknownfd`/`unkfield*`.

#### Shared evidence for Option.c, Parameter.c, Help.c, Utils.c, ParserErrors.c, Targets.c, Arguments.c

# Review: the command-line parser library (Option, Parameter, Help, Utils, ParserErrors, Targets, Arguments)

Shared evidence, true of all seven: every unit's functions ascend in the binary in Pro7's file order and
match Wii 1.0RC1's module of the same name; every object's .data/.bss starts 8-aligned at its splits.txt
cut; `relink.py GC_2_6` links 213 sources (2806 functions, unchanged); 4020/4077 exact,
nothing lost against compare_start.json; `clang_check src/driver` 0 errors; the equivalent compiler
(build/GC_2_6/mwcceppc.exe) gives byte-identical stdout/stderr, exit codes and objects to the original
for 24 driver command lines (`-help all/usage/opt=/tool=/group=/obsolete,...`, unknown and malformed
options, numbers in hex/octal/overflow, a response file with `\r\n` and ^Z, -D/-U/-I/-ir, -O4,p, -w,
-opt, -sym/-g, -prefix/-include errors, duplicate -o, -version). The units were rewritten from Pro7's
text verbatim (the old sources were gc1-style: stand-in structs, hand-rolled varargs, `register`
parameters, numbers for every flag) and only then given 2.6's changes, each one tested against
Pro7's spelling.

Headers reviewed with them (all rewritten or edited here): `driver/option_system.h` (now Pro7's:
flat 1-packed PARAM_T kinds with Pro7's fields, `Option {names, avail, param, sub, conflicts, help}`,
`OptionList {help, flags, list}`, Pro7's OTF_/HELPFLAGS_/PARAMWHICH_/PARAMFLAGS_/SLFLAGS_/LISTFLAGS_/
OFLAGS_/PFLAGS_ enums, `ArgToken`, `Opt48`; the stand-ins `BytePair`, `ByteQuad`, `DumpTextRecord`,
`FloatParameterCallback`, `HANDLER_T`, `FlagTextBuffer`, `Triple`, `HelpColumn`, `OStack` with its
`alignmentPadding` are gone), the seven units' own headers, `driver/oslib.h` (OPTION_ASSERT added),
`driver/parser.h` (stale DispatchObject/DispatchTable forward declarations dropped), `libc/ctype.h`
(MSL's inline isspace added).

### src/driver/Option.c: SOLID WITH TODOS
- Boundary: Pro7's Option.c, 38 rows in Pro7's order, Wii module "Option" the same (Wii's extra
  Options_Term is later; its static Option_PushList.. contributions are unnamed, as ours are static).
  .data 0x5a2040 (8-aligned), .bss 0x5de848 (optionlists/internalset/legalset). Pro7's dead
  Option_ParseDefaultOption kept: "defaultoptions"/"Default options not defined" are in 2.6's .data
  (0x19ed38). Pro7's dead Options_AddLists and Option_AlsoPassedToTool kept (no data; Pro7's assert
  line 1335 equals 2.6's, so the file's lines before it are Pro7's).
- Flags: pro53 -O1 -opt intrinsics -sym off -Cpp_exceptions off (all seven alike), not deferred (rows ascend in Pro7's order). `-sym on` loses 3 functions.
- Functions: 38/38 exact. Departures from Pro7: asserts are MSL's assert() expression form
  (OPTION_ASSERT; 2.6's strings keep "(oStack[oStackPtr-1].flags & prev)" with parentheses, read at
  0x19ec60) - original's shape. Option_ForTool `which == 0 ||` (Pro7 `!which`), Option_SpellList
  `if (!first) ... else first = 0`, Option_IsEndingThisLevel `tok = Arg_UndoToken()`, Option_Help
  `ret = 0` at the top, Option_Parse wraps the parameter/sub-option part in `if (ret)` and assigns
  `tok = Arg_UsedToken()`: needed shapes (each Pro7 spelling tried, 82-99.8%). Options_DoParse: 2.6
  keeps Pro7's `mystery_r31` as two variables (`nested`, `level`), clears `opt`/`isOpt` at the top of
  each pass, groups `(level == 2) && ((tok->val == ATK_ARG) && (...))`, counts `parsedany++`, writes
  `haderrors++; failed = 1` and several `tok = Arg_UsedToken()`: needed shapes (Pro7 70%; each
  reverted alone measured 99.6-99.9%; Pro7's `break`/`||` forms were kept where they match).
  Original's shape, invented names (real code in the binary): Options_DoParse computes
  `endingStack[level]` and never uses it (0x41da16); Option_Lookup clears a second Boolean each pass
  (0x41d870; `matched = 0` alone is 99.1%, a split `matched`/`sticky` 96.1%).
- Behaviour: from Pro7 statement by statement; message ids now Pro7's CLPStr names (verified against
  ParserErrors.r, STR 12010). Bugs of ours fixed: the old Option_Error/Option_Warning built their
  va_list by hand from `&messageId` (now va_start); old Option_Parse used `0x880000` etc. as numbers.
- Names: Pro7's/Wii's (curopt, oStack, oStackPtr, numoptionlists, numlegalset, maxlegalset,
  numinternalset are Wii COMMONs). Invented: `ending`, `nested`, `unused` (TODO each); Pro7's own
  placeholders kept with a note (ARGFLAG_n, ARGSPELLFLAG_n, OPTSPELLFLAG_n, Opt48/50/52).
- Types: Pro7's (Opt48 stack entries, SInt16 flags, va_list reporters).
- Data: relink links; statics `static`; the stale bindings of its statics (_endingStack, _internalset,
  _legalset, _optionlists) deleted: the object names them.
- Header: Option.h declares only what Parameter, Help, Targets, ParserFace and OptimizerHelpers use
  (it had 47 declarations, 1.x types and wrong signatures); prototypes at the file top only for
  Option_Error (used before its definition).
- Headers relied on: option_system.h (rewritten, above); Arguments.h (PtrList, ArgToken calls);
  Help.h, Parameter.h, Utils.h (callees); ParserErrors.h (CLP* reporters, CLPStr ids: now Pro7's enum);
  ParserFace.h (parseopts); Targets.h (pTool); IO.h (ShowVersion
  takes `char includeValue`, Pro7 `Boolean decorate`; ShowTextHandle `char *` for Pro7's
  `const char *`: open); MemUtils.h (xstrdup, xrealloc with `int size`); CWPlugins.h
  (CWDROPINCOMPILERTYPE); oslib.h (OPTION_ASSERT).
- Open: the three invented local names and Pro7's placeholder enum names.

### src/driver/Parameter.c: SOLID WITH TODOS
- Boundary: Pro7's Parameter.c, 55 rows in Pro7's order (DescHelp/Compare/Param per kind, the three
  tables, dispatchers, GetArgument, Params_Parse, Param_Error/Warning); Wii "Parameter" the same
  (publics: the tables, descparam/helpparam/defaultparam, Param_DescHelp/Compare, Params_Parse,
  Param_Error/Warning). .data 0x5a5860. The assert's file name is "Parameter.c" (0x1a26ec), so the old
  `CERROR_FILE "unknown.c"` is gone.
- Flags: as Option.c (`-sym on` loses 6).
- Functions: 55/55 exact. 2.6 changes: Pro7's byte-copy macros differ in two ways that Param_Set/Number
  and Param_Toggle show (inline_write_* copies the destination first; inline_xor_* uses `^=`);
  DescHelpParam_FTypeCreator spells the four characters from the high byte in a loop and
  Param_FTypeCreator fills `x[4 - ++cnt]` (2.6's byte order of an OSType); Param_FilePath's buffer is
  260 bytes (MAX_PATH, Pro7 256); DescHelpParam_IfArg passes `d2` to the "(if blank)" sprintf (a push
  at 0x42bab4 the format ignores: the original's quirk). Needed shapes: `!x` for Pro7's `(x == 0)`
  (Param_Set's three, Param_OnOff/OffOn's `Boolean mytrue`), `|=` for `and = and | or` (Param_Mask),
  `!pstrcmp`/`!strcmp` (CompareParam_SetString), `*defaul = 0; *help = 0;` for the chained store,
  `dptr +=` on DescHelpParam_Setting's second sprintf, `*dptr = 0; *hptr = 0;`, a `ret` local in
  Param_Parse, `lo = 0; hi = 0;` in Param_Number, `cnt = 0;` before the quote, and `tok =` on three
  Arg_UsedToken calls in Param_GetArgument (one is tail-merged with another `tok = Arg_UsedToken()`).
  Original's shape, invented names: DescHelpParam_FilePath loads helpparam into an unused local
  (0x42ae9a), Params_Parse computes `(flags & 4) != 0` and never uses it (0x42c09f).
- Behaviour: Pro7's text; genuine bug in the original kept: CompareParam_Toggle copies two bytes of a
  32-bit value (copy_32 does not match, 93.4%). Bugs of ours fixed: the old Param_FilePath and
  DescHelp functions used `int*` outputs and casts through gc1 stand-ins; implicit declarations of
  OS_* gone (each callee's header included).
- Names: Pro7's and Wii's; invented `hptr`, `subparse` (TODO).
- Types: Pro7's PARAM_T kinds (packed 1), PARSE_FUNC/DESC_HELP_FUNC/COMPARE_FUNC tables. Confirmed
  by Wii 1.0RC1's CodeView global types (ParserGlue's module has them): PARAM_T, MASK_T, STRING_T
  (maxlen short, pstring uchar), SET_T, SETSTRING_T (pstring char), GENERIC_T, SETTING_T, NUM_T,
  FILEPATH_T, IFARG_T, ONOFF_T, OptionList and Option field for field, offsets and signedness.
- Data: relink links; descparam/helpparam/defaultparam and the tables are Wii's publics.
- Header: Parameter.h: curparam, Param_DescHelp, Param_Compare (Help), Params_Parse, Param_Error
  (Option); it had 70 lines of fn_XXXX/return_true stand-ins for functions that do not exist.
- Headers relied on: option_system.h; Option.h; Arguments.h; MsDos.h (OS_MakeFileSpec,
  OS_SpecToString, OS_MakeSpec, OS_Status, OS_GetErrText); MacFileTypes.h (OS_GetMacFileType);
  StringUtils.h (c2pstrcpy, pstrcmp); TextUtils.h (c2pstr); StringExtras.h (ustrcmp); ParserFace.h
  (parseopts); Targets.h (pTool); Utils.h.
- Open: two invented local names.

### src/driver/Help.c: SOLID WITH TODOS
- Boundary: Pro7's Help.c, 18 rows in Pro7's order; Wii "Help" the same (publics Help_Option..
  Help_Term). Pro7's dead Help_Null kept (its literal is at 0x1a31cc; the old copy passed the wrong
  arguments). .data 0x5a5b20, .bss 0x5dead8: the opttool `= ""` template (0x40) then outLine.
- Flags: as Option.c (`-sym on` loses 2).
- Functions: 18/18 exact. Needed shapes: Side_Print `text++` before `c = *MAINOPTCHAR`; Side_DumpLine
  `len++` before `s->vcol++`, `while (afterspace < len)`, `s->blen == 0`; Help_Option
  `!subprint ? (char) SLFLAGS_1 : (char) SLFLAGS_2` (byte-enum ternary), `slflags |=`,
  `strchr(opt->names, '|') == NULL` (Pro7's `!strchr` is 86%), `isdefault = scan != NULL`. Original's
  shape, invented name: Help_Option copies each described parameter into an unused register
  (0x42cc78, `currentArg`).
- Behaviour: Pro7's text; help output identical to the original's (-help all and 5 other forms).
- Names: Wii's COMMONs replace the invented ones: `left` (was firstHelpColumn), `right`
  (third_help_column), `all` (helpTextColumn), `helptext` (help_output); outLine (was help_line).
  helpExtras and showedHelp (Pro7's, Wii's COMMONs, unused) are defined: their names' hash order fits
  the two hidden bytes in 2.6's COMMON (cerror_synchoffset's extent 4 at 0x5eb08a for an SInt16,
  LoopOptimizerRun's extent 2 at 0x5eb3b3); bound in every version so the split names them (the
  relink links with or without them, so the evidence is the hash order and Wii).
- Types: Pro7's `Side` (was the stand-in HelpColumn), Handle helptext.
- Data: relink links; stale literal bindings (_option_usage_notes*, _data_0054*) and the template's
  deleted.
- Header: Help.h: Help_Option/Options/Usage/Init/Line/Term (Option.c); Side_* and the columns are
  static now.
- Headers relied on: option_system.h; Option.h (tool tests); Parameter.h (DescHelp/Compare);
  Arguments.h (MAINOPTCHAR, SEPOPTSTR, compat); Generic.h (OS_GetFileNamePtr); Memory.h; StringUtils.h
  (HPrintF); IO.h (ShowTextHandle `char *`, open); ParserFace.h (parseopts);
  CWParserPluginsPrivate.h (CWCommandLineArgs).
- Open: `currentArg` (invented).

### src/driver/Utils.c: SOLID WITH TODOS
- Boundary: Pro7's Utils.c, 7 rows; Wii "Utils" the same; .data 0x5a22d0 ("=...").
- Flags: as Option.c (`-sym` changes nothing here; the library's setting).
- Functions: 7/7 exact. my_isxdigit groups the two ranges (Pro7's flat chain is 62.9%): original's
  shape. Utils_SpellList: 2.6's loops `for (cnt = 0; cnt++ < npflen;)` and `while (undo--)` (Pro7's
  each 98-99%): needed shapes; five locals set and never read (stores at 0x420ca2..0x420cb8 and after
  each prefix copy; gc1 has the same stores): original's shape, invented names x/y/z/c/d (TODO).
- Behaviour: Pro7's; help spellings identical in the run test.
- Names: Pro7's/Wii's; invented x, y, z, c, d.
- Types: as Pro7.
- Data: relink links.
- Header: Utils.h: every function has a user (Option, Help, Parameter, ParserHelpers); the stray
  `<setjmp.h>` dropped.
- Headers relied on: Arguments.h (MAINOPTCHAR).
- Open: five invented dead-local names.

### src/driver/ParserErrors.c: SOLID
- Boundary: Pro7's ParserErrors.c, 13 rows in Pro7's order; Wii "ParserErrors" lacks CLPReport_V and
  CLPReport (dropped there). Pro7's dead CLPAlert and CLPProgress restored (no data either way).
  .data 0x598720, .bss 0x5dcd28 (errorbuf, 0x400).
- Flags: as Option.c (`-sym` neutral).
- Functions: 13/13 exact, Pro7's text verbatim (messagetype enumerators for 2/1/0, NULL for the
  CWAlert strings).
- Behaviour: as Pro7; the error output of the run tests is identical.
- Names: Pro7's; binding `_formatted_message` was errorbuf (renamed in every version, then deleted in
  2.6: the object names it).
- Types: SInt16 ids, va_list.
- Data: relink links.
- Header: ParserErrors.h declares the reporters other units call (adversary: CLPReport_V, CLPStatus_V,
  CLPAlert_V and CLPOSAlert_V had no user outside the unit and are dropped; all are defined before
  their callers here) and now Pro7's CLPStr message enum
  (texts checked against ParserErrors.r; 76's comment is 2.6's text).
- Headers relied on: CWPlugins.h (messagetype*), CWPluginsPrivate.h (CWReportMessage, CWAlert,
  CWShowStatus), MsDos.h (OS_GetErrText), TextUtils.h (getindstring), ParserFace.h (parseopts,
  exit_plugin).
- Header round H2 (October 16): IO.h's ShowVersion/ShowTextHandle are Pro7's and ParseOptsType.helpFlags is SInt32 (Wii's long). Resolved.

### src/driver/Targets.c: SOLID
- Boundary: Pro7's Targets.c, 4 rows; Wii "Targets" the same; .data 0x5988d0 ("Targets.c", the
  assert text, the fatal message).
- Flags: as Option.c (`-sym` neutral).
- Functions: 4/4 exact, Pro7's text; the assert is OPTION_ASSERT(16, ...) (MSL's expression form; the
  old body wrote the call out with a literal file name).
- Behaviour: as Pro7.
- Names: Pro7's (pTool is Wii's COMMON).
- Types: OSType parameters (Pro7's; were UInt32).
- Data: relink links.
- Header: Targets.h: its four functions (ParserFace, ParserGlue) and `pTool` (moved there from
  ParserFace.h in main since); the stand-in DispatchObject/
  DispatchTable structs (used nowhere) and the win32.h include dropped. The source lost a private
  va_start macro, `#define ClientGlue_DuplicateString xstrdup`-style aliases, 20 unused includes and a
  dead `CERROR_FILE "options.c"`.
- Headers relied on: ParserFace.h (ParserTool, parseopts), Option.h,
  ParserErrors.h, StringExtras.h (ustrcmp), CWPlugins.h, oslib.h.
- Header round H2 (October 16): IO.h's ShowVersion/ShowTextHandle are Pro7's, ParseOptsType.helpFlags is SInt32, Option.h is Pro7's. Resolved.

### src/driver/Arguments.c: SOLID WITH TODOS
- Boundary: Pro7's Arguments.c, 25 rows in Pro7's order; Wii "Arguments" the same (its
  Arg_FreeToolArgs public, 2.6 has none: Pro7's dead Arg_FreeToolArgs and Arg_Stop kept). .data
  0x598738, .bss 0x5dd128 (statics in reverse declaration order: scantok lowest, argtoks highest,
  respfilehandle 0x210). SEPOPTCHAR stays out (2.6 lacks it, AGENTS).
- Flags: as Option.c (`-sym on` loses 2).
- Functions: 25/25 exact. 2.6 changes: Arg_SkipRespFileWS ends the file at a ^Z; Arg_GetNext strips a
  trailing '\r' (`rfcequ = 0` first, needed). Needed shapes (each Pro7 spelling 95-98%): `pprev = 0;
  ppprev = 0; pppprev = 0;`; `ta->nargv += 16`; `(maxlen > 32) ? 32 : maxlen`; Arg_GetRespFileToken
  `*(ptr++) = '"'`; Arg_Parse `bufptr`-based stores, `ch == '\\'` on the loaded char, and
  `if (isOpt || isList)` before an option's ARG_END (isList is 0 there: the original's redundancy).
- Behaviour: Pro7's text; response files with \r\n and ^Z parse identically to the original. Bug of
  ours fixed: OS_AppendHandle was called through an implicit declaration (cdecl for a __stdcall
  function: an extra `add esp, 0xc`); the forward declarations are prototypes now (`(void)`; Pro7's
  `()` compiles the same).
- Names: Pro7's statics; the bindings had invented names for them (_token_texts, _arg_index,
  _coalesced_argument_count/_capacity, _data_0057e0xx, _data_0057e1d8): renamed to argtoks, margind,
  numargtoks, maxargtoks, margc, in_response_file, margv, scantok in every version, then deleted in 2.6
  with the respfile* ones (the object names them). PtrList is invented (Pro7: anon0_50; TODO; Wii's CodeView has the struct {argc, nargv, argv}
  with no tag, so only a typedef name was ever there).
- Types: Pro7's; MSL's inline isspace now in include/libc/ctype.h (was a private copy here).
- Data: relink links; literal binding _empty_string deleted.
- Header: Arguments.h: what ParserFace, ParserGlue, Option, Parameter, Help, Utils, IO use; FIRSTARGCHAR,
  SEP1-3, RESPFILE*, parserDebug and Arg_FinishToolArgs (no outside users) dropped.
- Headers relied on: option_system.h (ArgToken), CWParserPluginsPrivate.h (CWCommandLineArgs),
  FileHandles.h, Generic.h (OS_AppendHandle), MsDos.h, MemUtils.h, StringExtras.h (ustrncmp),
  ParserErrors.h, libc/ctype.h.
- Open: PtrList (invented), Pro7's COMPAT_n placeholders.

- Adversarial review: attacked the dead-local TODOs, the "needed shapes", the header contents, the
  types, names, flags and boundary. Broke: (1) ParserErrors.h declared four _V reporters
  (CLPReport_V, CLPStatus_V, CLPAlert_V, CLPOSAlert_V) nobody outside the unit uses: dropped (all
  4020 exact kept, the seven units and their includers relink); (2) the flags line said pro6, the
  units are pro53 (`config/sources.json`); (3) the shared-evidence count said 217 linking sources,
  the build says 213. Held: every dead local is a real store in 2.6 (Option_Lookup's second Boolean
  at 0x41d870, `ending` at 0x41da16 (`lea ebx,[ebx+ebx*2]` into endingStack, then dead), Params_Parse's
  `setnz` at 0x42c0a8 into a register never read); removing each, or reusing an existing variable
  (Help_Option's `lastparam = scan` for `currentArg`), loses the match (99.1-99.9%). Their story is
  better than "invented": the driver is built -O1 here, which keeps dead stores, where Pro7's PPC
  build optimised them away, so Ninji's text could not show them; the names stay TODO. The Pro7
  spellings the reviewer called needed do not match (`mytrue == 0` 51.9%, Param_Set's `== 0` 91.1%,
  CompareParam_SetString's `== 0` 60.0%, Help_Option's `scan ? 1 : 0` 91.0, `!strchr` 86.4, its
  ternary 99.6, `slflags = slflags | ...` 99.8, Param_Number without `lo = hi = 0` 77.8); the
  "(if blank)" push of d2 is not a ternary format (`d2 ? "%s" : "(if blank)"` 98.9: 2.6 pushes d2
  in each arm, then tail-merges the call). Wii's CodeView types confirm option_system.h's parameter
  kinds and Option/OptionList and ParserTool's layout. Headers relied on, new: ParserFace.h's
  ParseOptsType.helpFlags is `long` in Wii's types where the repo has `int` (its comment says an
  SInt32 moved a constant: for ParserFace.h's owner to settle). (EnvInfo, the "CmdLine
  Environment" resource ParserFace.c reads, whose f0/f7 placeholders this review left open, is
  Pro7's PCmdLineEnvir (pref_structs.h) in main since.)

### src/frontend/CInit.c: SOLID WITH TODOS
- Boundary: 50 rows 0x50a3f0..0x50fba0 descend in exactly Pro7 CInit.c's function order reversed (deferred). Wii module `cinit` (8 names) and the asserts ("CInit.c", 11 sites) agree. The relink passes, which proves the .data/.bss cut (.bss 0x5e2020..0x5e203a holds the 7 statics). Pro7's dead functions are back: CInit_ParseInitializerList/Clause, CInit_IsSimpleInit, CInit_ClassInitLoopCallBack. The evidence: 2.6's assert lines equal Pro7's up to 521 (389, 521) and keep a constant +3 around ClassInitLoopCallBack (2807->2810, 3164->3167), and cinit_loop_class is in 2.6's .bss.
- Flags: pro53 (pro6) `-O4,p -inline auto,deferred -sym on`. Deferred because the rows descend; non-deferred gives 38/50. `-sym off` gives 33/50.
- Functions: 50/50 exact. **Rewritten from Pro7's text**: the old file was decompiler/gc1 text with stand-in structs (CInitSave, InitializerData, NameEntry, CInit_StrNode...), `CE_ASSERT`, `TYPESTRUCT_004d2700` enums, hand-inlined helpers and casts through function pointers. Departures from Pro7:
  - 2.6 changes (confirmed by exact code): CInit_CreateStaticData calls CodeGen_UpdateObject. CInit_DeclarePooledString calls `PPC_EABI_SetSection(obj, 0, copts.readonly_strings, TRUE)` (Pro7: SECT_CONST); the wide one sets no section; CInit_DeclareString has no Mach-O section choice. The assert lines are 2.6's (2750/2759/2779/2810/3167/3175/3216/4229). RegisterObjectCB's flag is a dword (`int`).
  - needed shapes (Pro7's spelling tried each time and failed; all catalogued in AGENTS):
    - a `return` in each arm (TypeExpr, Type, FindDtorTemp, InitType's void case, InitTypeClass, InitTypeArray, InitializeAutoData) and early returns (declaredata, InitializeData);
    - `x |= F` (declaredata, InitializeData);
    - `return s->stage` after each store (ParseNextInit: the field is reloaded);
    - one walker variable for scan and the new entry (both pooled-string functions);
    - a parameter reused (InitConcat's `prev`);
    - `if (!(targetsize = ...))`, chained `targetsize1 = targetsize2 = TYPE_POINTER(tptr->target)->size`, `tptr->size == 0`;
    - `IS_TYPE_ARRAY((newtype = type))` (AutoObject);
    - `obj == NULL` inside `||` (AutoObject);
    - a `TypePointer*` copy (AutoInit, GenericData);
    - the chained `cinit_initinfo->obj = obj = ...`;
    - an `if (...) { CError_FATAL(2639); }` assert;
    - if/else for Pro7's ternary and a separate `expr = assignment_expression();` (InitReference);
    - a block-local temporary for InitExpr_Register's argument;
    - `objexpr` reused in RefInit;
    - InitTypeStruct's `UInt8 *p, val` order and its InitVectorMem call written out;
    - decl-order (InitializeClassArray, GlobalTempNode, GenericData, InitializeData).
  - original's shape: InitializeAutoData writes CInit_IsSimpleStructArrayInit's switch in place. Every inline-call spelling tried (6) gives other registers; only the in-place switch with its own walker `type` matches. Plausible 2.6 text, commented in the source.
  - fakematch (TODO in source): `CInit_DeclareData(obj, buf, list, size = obj->type->size)` in GenericData (2 calls) and InitReference (2). 2.6 loads the size before pushing FALSE, as for a variable. The plain argument, a statement before the call, Pro7's CInit_AdjustObjectDataSize (kept, or as the argument, or returning the size) and IsAllZero's `size` were all tried and fail.
- Behaviour: all code is exact (references resolved), from Pro7 plus the 2.6 changes above. Pro7's CInit_Array reads the target's size twice (an incomplete array of arrays always errors): 2.6 keeps that, as an original's quirk. Bugs of ours fixed: CFunc's `ainit_register_object` returned void (CInit's code uses its ENode result: it worked only through eax); it now returns Pro7's ENode* with 2.6's `int` flag.
- Names: Pro7's throughout. `cinit_state` is now Pro7's `cinit_initinfo`: hash 1208 fits between pTool 1198 and LoopTail 1209, where `cinit_state` (2024) did not. Bindings renamed in every version. Xgreg_func moved to CParser.c/CParser.h, Pro7's home (relinks; main had made the same move). Register-derived locals are renamed (r30/r29/a1/a5/common_8068C -> head/tail/prev/value/inline_data).
  - invented (TODO): CInit_NonConst (Pro7 CInit_1C); member names for Pro7's placeholder members (CInit_Stuff x0/x4/xC/x18/x1C/flag, CInit_Stuff2 x23/x24, InitInfo x8/x15/x16/obj1C).
- Types: Pro7's InitInfo, PooledString (22 bytes, pack 2: galloc(0x16)), CInit_Stuff (38: lalloc(0x26)), CInit_Stuff2, Stage enum, OLinkList. The 1.x stand-ins are gone from CInit.h and common.h (CInit, CInitSave, CInit_ArgumentList, CInit_StrNode, FloatNode, InitListItem, InitializerData, InitializerEntry, NameEntry). CodeGen.c uses PooledString. CMach_InitFloatMem takes Pro7's `void*`.
- Data: the relink is byte-identical. The statics are `static` in Pro7's order (2.6 has them in reverse, lowest = cinit_fdtambig). The 6 static bindings (_cinit_fdtambig/fdtnode/pooledstringlist/pooledwstringlist/stringlist/tentative) are deleted in GC_2_6; the COMMON bindings stay. The `#pragma pack(2)` at the end is needed for the 2-aligned .bss (cinit_fdtnode at 0x5e2022), TODO kept.
- Header: CInit.h is Pro7's API (the callback typedefs, OLinkList, PooledString, cinit_tempnodefunc, and the 16 functions other units call). InitInfo, ExprCB and cinit_initinfo moved into CInit.c (only it uses them). The file top has its own structs and the 2 prototypes Pro7 has.
- Headers relied on:
  - CObjC.h: RelocationList is Pro7's OLinkList (`typedef RelocationList OLinkList` in CInit.h; the 1.x name belongs in CInit.h). CObjC.c builds relocations as `SInt32[4]` (casts added at its 2 calls).
  - CDecl.h: DeclInfo is a stand-in layout with invented names (dtype/storage/requireMangledName for Pro7's thetype/storageclass/is_extern_c; placeholders f38, f3d). (Since: DeclInfo has Pro7's member names where Pro7 has names (thetype, storageclass, section, is_extern_c, in_friend_decl, expltargs, file/file2/sourceoffset ...) and TODO-marked invented ones for Pro7's offset members; the f38/f3d/x54..x58/pad66 placeholders are gone.)
  - enode.h: `data.string.ispascal` (Pro7's name; renamed in main since); ENodeUnion keeps placeholders (`unk08`).
  - CParser.h: `atomtype()` returns Pro7's Type* (main changed it since; the TYPE() casts at the uses were dropped).
  - CPrec.h: PreComp_StaticData's `int size` (Pro7 SInt32).
  - ObjGen_PPC_EABI.h: `unsigned int size`.
  - CException.h: CExcept_RegisterDestructorObject's `int flag` (consistent with 2.6).
  - CObjC.h/common.h: `RelocationList` (117 uses in 10 units) should become Pro7's `struct OLinkList` defined in CInit.h; the typedef alias stands in until that cross-unit rename.
  - CInit.h: `CInit_DeclarePooledWString` has no user outside the unit (kept: Pro7's CInit.h declares it).
  - CExpr.h, CExpr2.h, CFunc.h, CClass.h, CABI.h, CMachine.h, CScope.h, CInline.h, CInt64.h, CodeGen.h, CompilerTools.h, CPrep.h, CPrepTokenizer.h, objects.h, types.h: used for declarations only; not reviewed in depth.
- Open: the 4 `size =` workarounds, the invented member names, and the headers above (unreviewed or with stand-ins: DeclInfo, RelocationList, ENodeUnion placeholders), hence PENDING HEADERS rather than WITH TODOS.
- Adversarial review: attacked the `size =` fakematch (8 spellings in CInit_InitReference: plain, a statement before the call, the Adjust call kept/as argument, declaredata called directly, a dead store; and CInit_DeclareData/declaredata's size typed UInt32/int/SInt32 with the plain argument: all 91.5/94.4, the TODO holds; the miss is a whole coloring change, not only the push order), AutoObject's `IS_TYPE_ARRAY((newtype = type))` (Pro7's two spellings 99.5: holds), the COMMON hash order of `cinit_initinfo` (1208 between pTool 1198 and LoopTail 1209: holds; `cinit_tempnodefunc` 755 also fits) and the .bss order (reverse declaration, fdtambig lowest at 0x5e2020: holds), every header declaration's users (all used outside but CInit_DeclarePooledWString, Pro7's API). Broke: hygiene, 47 boolean literals passed to Boolean parameters were `0`/`1` (now TRUE/FALSE, found with clang's AST). Raised the grade's header caveat: headers with known stand-ins are relied on, so PENDING HEADERS.
- Header round H2 (October 16): RelocationList is Pro7's `struct OLinkList` (`obj`; `addend` TODO for somevalue), ENodeUnion's placeholders are gone, CInit_DeclareData takes `OLinkList*`; DeclInfo keeps TODO names. Resolved as TODO names.

### src/frontend/CExprConvMatch.c: SOLID WITH TODOS
- Boundary: 27 rows 0x54cab0..0x5518b0, Pro7 CExprConvMatch.c's order reversed (all 42 Pro7 functions present in Pro7 order; the 15 not rows are inlined statics). The asserts ("CExprConvMatch.c") agree, and so does Wii (FuncArgMatch in CExprConvMatch). Wii's `CClass_IsBaseClassOf` for CExpr_IsBaseOf is a later move: Pro7's name is kept. The relink passes.
- Flags: deferred (rows descend; non-deferred gives 26/27). `-sym on` (off: 17/27).
- Functions: 27/27 exact. Each departure was tested function by function against Pro7's verbatim text:
  - **now Pro7's text** (the old shapes were not needed): CExpr_IsReferenceCompatible (the old if/else chain with a result variable), CExpr_NextArithmeticType's if/else, the `static` (not `static inline`) on the Next*Type helpers and IsReferenceCompatible, CheckBinaryBuiltin's declaration order, Pro7's include list (the old one held Iro*, DWARF, Intrinsics, driver/Files ...). The private `extern CDecl_NewRefPointerType` is dropped: CDecl.h has it now.
  - 2.6 changes (in the exact code):
    - CodeGen_CheckExpr in CExpr_Convert (4 sites);
    - `q1 = expr->flags` unmasked in UserConversion (there is no `and` in the binary);
    - the constructor loop of UserConversion builds objlist24 from object27 (Pro7's text: objlist25/object28, its conversion-function loop's variables: a 2.6 fix or a transcription slip).
  - original's shape: `ENODE_QUALS(expr) != (cv & Q_CV)` with `cv` already masked. The binary has the redundant `and edi, 3`; `!= cv` and `(qual & Q_CV)` fail.
  - needed shapes (Pro7's spelling fails each time, mostly catalogued):
    - `inline` on CExpr_DerivedToBase's first declaration (with plain `static`: 23/27);
    - its call and EINDIRECT split into two statements;
    - assignments in conditions (ClassReferenceConversion, BindToReference, OverloadFuncMatch's `TypeFunc* functype`, CheckBinaryBuiltin's `scan`, FuncArgMatch's `type`);
    - ternaries in NextPromotedIntegralType/NextPromotedArithmeticType (if/else breaks CheckUnary/BinaryBuiltin);
    - chained Boolean assignments (CheckBinaryBuiltin);
    - MatchArgs' counter before pointer, `if (args->dexpr) return TRUE; return FALSE;` and its declaration order;
    - IsBetterMatch's `convA++; convB++;` as statements;
    - early `return` in FuncArgMatch;
    - UserConversion's declaration order, `tfunc23` assigned after the test, `flag3 && (tmpExpr = ...)`, and the temp node in `newExpr` with the call in `tmpExpr`;
    - Convert's single `return expr` after the reference if/else;
    - CheckIncDecBuiltin's `type = tfunc->functype` local;
    - declaration order in FindBestMatch and OperatorMatch.
  - fakematches: none.
- Behaviour: exact throughout. StandardConversionMatch's own flag (Pro7 x14) was misnamed `is_ref_binding`: its only non-zero caller is CExpr_MatchOperands for a member operator's implicit object parameter (which an rvalue may bind, [over.match.funcs]), and it is passed in the setup's `refFlag` slot (skip the lvalue check) at 14 of 17 calls, while the setup's own `is_ref_binding` gets the local refFlag (the target is a reference), which IsBetterStandardConv ranks as a reference binding. Renamed `implicit_object` (TODO invented, commented). The 3 calls passing the pair the other way round (the two null pointer constant conversions, the Objective-C one) are 2.6's code (exact) and Pro7's text: kept, commented as such. No bugs of ours found.
- Names: Pro7's function names and Pro7's register-derived local names (object28, flag23 ...).
  - invented (TODO), for Pro7's placeholders:
    - StandardConv members (x10..x15 -> lvalue_transform (never used), qual_adjust, promotion, conversion, ref_binding, nonconst_ref_result);
    - ICT_0..3 -> ICT_NONE/ELLIPSIS/USER/STANDARD, and ic2/ic3/x2 -> user/standard/func;
    - SSCR_0..2 -> EXACT/PROMOTION/CONVERSION; MysteryEnum -> QualDiff (QD_SAME/MORE/LESS);
    - Inline_501D40 -> CExpr_IsPointerToBoolConv, Inline_501FF0 -> CExpr_CompareQualifiers;
    - Setup's x14 parameter -> is_ref_binding; StandardConversionMatch's x14 -> implicit_object (adversarial review; it was is_ref_binding, a wrong reading).
- Types: Pro7's file-local structs under pack(2) (mac68k). The header types it uses are the repo's names for Pro7's: MatchLink = ObjectList, ArgMatch = Match13, BinaryOperatorResult = Conversion, ConversionSearchState = ConversionIterator, CScopeParseResult = NameResult.
- Data: the relink is byte-identical; there are no statics and no bindings for this object.
- Header: no header of its own; its public functions are declared in CExpr.h (Pro7's arrangement).
- Headers relied on:
  - CError.h: the pointer-to-int truncation warning was a raw `CError_Warning(382)` (Pro7's CErrorStr382) while the message enum stopped at 371; with the CError review's ids 372..392 it is `ERR_INTEGRAL_TYPE_TOO_SMALL_FOR_POINTER` (replay).
  - CTemplateFunc.h: CTempl_CanDeduceFunc/CTempl_DeduceFunc take Pro7's TypeFunc* (main changed them since; the calls pass `TYPE_FUNC(type)` and `functype`); CTempl_PartialOrdering takes MatchLink*.
  - CExpr2.h: ArgMatch keeps a placeholder `xE`; MatchLink duplicates ObjectList.
  - CExpr2.h: `CExpr_NewETEMPNode(Type*, unsigned int withAuxiliary)` where Pro7 has `Boolean assign_id`.
  - CExpr.h/CExpr2.h, CABI.h, CClass.h, CDecl.h, CFunc.h, CInt64.h, CMachine.h, CMangler.h, CObjC.h, CodeGen.h, CompilerTools.h, CParser.h, CScope.h, objects.h, scopes.h, templates.h: used for declarations only; not reviewed in depth.
- Open: the invented names; the headers above (ArgMatch's `xE`), hence PENDING HEADERS.
- Adversarial review: attacked the names of the setup/match flags against their callers and IsBetterStandardConv (broke: StandardConversionMatch's `is_ref_binding` was the implicit-object flag, renamed), the header claim (all six public functions have users outside), hygiene (broke: 84 `0`/`1` literals to Boolean parameters, including the DerivedToBase calls the first entry listed as open, now TRUE/FALSE), the Pro7-order boundary and the absence of statics (held).
- Header round H2 (October 16): ArgMatch's `xE` is `userconversions` (TODO), MatchLink is gone (CTempl_PartialOrdering takes ObjectList*), CExpr_NewETEMPNode takes Boolean; CExpr2.h is merged into CExpr.h. Resolved as TODO names.

#### Shared evidence for CError.c, CInt64.c, CompilerTools.c, CMangler.c, Unmangle.c

# Review: CError, CInt64, CompilerTools, CMangler, Unmangle (GC_2_6)

All five were rebuilt from Pro7's text. After every step: `unit.py check`, `ninja build/GC_2_6/compare.json` (no function lost
against compare_start.json, 4020/4077 exact as at the start), `layout.py GC_2_6`, `relink.py --only` (all five link, and so
does every other touched unit that linked before), `validate.py` clean, and the equivalent compiler builds. On a C++ file
full of errors (abstract class, an argument-list mismatch with templates and namespaces, bad conversions, undefined
identifier) the equivalent compiler prints exactly what the original prints.

### src/frontend/CError.c: SOLID WITH TODOS
- Boundary: the 40 rows are Pro7's CError.c in Pro7's order, reversed in the binary. Pro7's static
  `CError_VAErrorMessage` and the buffer helpers are inlined. Pro7's dead `CError_SetNullErrorToken`,
  `CError_OverloadedFunctionError2` and `CError_ExpressionTooComplex` are back: no literals, so the data is neutral.
  `CError_BreakPoint` is back too: its inlined `CError_BreakPointcount++` after Internal's longjmp shows it was in the
  source. .data 0x5ab4b0-0x5ab89c and .bss 0x5e0210-0x5e021a link byte for byte.
- Flags: `-inline auto,deferred` (rows run in reverse of Pro7's order). `-sym` changed from off to **on**: the old
  source had `-sym off` plus a `#pragma sym on` at the top of the file, which does the same thing. With Pro7's text,
  -sym off gives 25/40 and on gives 31/40. Non-deferred gives 28/40.
- Functions: 40/40 exact. Departures from Pro7:
  - 2.6 changes: the `IT_VEC2X32FLOAT` "__vec2x32float__" case (BufferAppendType); the `Q_INTERRUPT`
    "__declspec(interrupt)" check (QualifierCheck).
  - Needed shapes (Pro7's spelling tried each time):
    - `UInt32 amount` in BufferGrow and `UInt32 len` in BufferAppendString (Pro7 has SInt32 and size_t).
    - An early return in BufferAppendTemplArgs.
    - `return` in place of `break` in BufferAppendPType.
    - `!= 0` written out in BufferAppendType's array test; this decides how the inlined AppendString copies take
      registers.
    - BufferAppendNameSpace's `if (!(targs = oargs)) targs = inst_args;`, which is the original's shape. Pro7's ternary
      makes it small enough to be inlined into AppendObjectName, and AppendObjectName then stops being inlined into
      TemplArgExpr.
    - `return strcpy(lalloc(...), eb.start)` in GetNameString.
    - ErrorMessage: `int messagetype = flag2 ? messagetypeWarning : messagetypeError`, the chain
      `fatalerrors = anyerrors = TRUE`, and the locals in another order.
    - ErrorMessageVA's `%f` case reads through the locals `spec` and `file`.
  - Workarounds: `#pragma pack(2)` at the end of the file. The statics are 2-byte packed (cerror_token is at +6); this
    is a TODO, like CompilerTools'. No fakematches.
- Behaviour: the literals and data link byte for byte. Bugs in the original: none new. CError_BreakPoint after the
  longjmp is unreachable, as in Pro7. Bugs of ours fixed:
  - `CError_Error` was declared as returning `NameSpaceList*` and returned nothing.
  - `cerror_token` was an `int`.
  - `CPrep_GetTokenContext` was implicitly declared (the project's own clang error); it is now in CPrep.h.
  - `cerror_synchoffset` was a `char[2]` (Pro7's `short`, which hash order confirms).
- Names: Pro7's throughout. `error_message_buffer` was invented: it is Pro7's `char string[256]` (CParser.c). Its hash
  (2991) sits between `pplist` (2902) and `output_path` (3748). Renamed in every version's bindings.json, and the
  definition moved to CParser.c.
  - `data_005830c8` was left invented on a hash argument that turned out stale (`source_line` (3577) is no longer a
    symbol); header round H2 renamed it Pro7's `cerror_synchdata` (242), which sits in hash order between
    `symdecltoken` (222) and `all` (304) in every version's layout.
  - Invented, with a TODO: the `ERR_*` message names, a repo-wide convention. I added 372..392 and `ERR_MAX` = 393 from
    cc-eabi-ppc.r; the range check and the abstract-class (372) and ambiguous-call (392) ids need them. (Main had
    meanwhile named 372-374, 381 and 387 for its own users; those keep main's names: ERR_ILLEGAL_TEMPLATE_PREFIX,
    ERR_TEMPLATE_PARAMETER_ARGUMENT_LIST_MISMATCH, ERR_ILLEGAL_ACCESS_FROM_TO_PROTECTED_PRIVATE_MEMBER,
    ERR_IMPLICIT_COPY_ASSIGNMENT_CONST_REF_MEMBER.)
- Types: Pro7's `CErrorBuffer {start, end, size, remaining}` replaces the stand-in `StrBuf {start, cursor, size,
  avail}`. Signatures are Pro7's: `CError_Internal` returns void (ELF_Endian's callback type is fixed to match),
  `CError_Warning(int, ...)`, `GetTypeName(Type*, UInt32, Boolean)`, `GetFunctionName(..., TypeFunc*)`, and
  `ErrorFuncCall(short, NameSpaceObjectList*, ...)` (CExpr2's three `(ObjectList*)` casts are gone).
  `OverloadedFunctionError` still takes `MatchLink*`, CExpr2.h's copy of Pro7's ObjectList (see headers).
- Data: the statics are `static`, and their three bindings are deleted. COMMONs now live where Pro7 defines them:
  `lines` and `cprep_nomem_exit` in CPrep.c, `ctempl_curinstance` in CTemplateNew.c (main had made that move too),
  `string` in CParser.c. All of these
  still link.
- Header: Pro7's API. Removed: the private `errorreturn`/`trychain` externs (now in CParser.h, as in Pro7),
  `error_message_buffer`, `append_qualified_function_signature`, and the internal buffer helpers (static, as in Pro7).
  The file top has the statics in 2.6's order (lasterrorline lowest) and two forward declarations, as Pro7 does.
- Hygiene: braces everywhere, TRUE/FALSE, SDK constants (`cwNoErr`, `messagetype*`), clang-format and clang_check
  clean, Pro7's stale "not matching" comments dropped, no CERROR_FILE.
- Headers relied on:
  - CError.h: the ERR_ names are invented; it still carries the CERROR_FILE machinery for the 9 units that define it.
  - CParser.h: `tk`, `in_assembler`, `anyerrors`, `fatalerrors`, `trychain`, `errorreturn` and `string` (the last three
    added here). CParser.c has private `va_start`/`va_arg` macros (AGENTS says use stdarg).
  - CPrep.h: `lines`, `cprep_nomem_exit`, `preprocessing_only`, `CPrep_GetTokenContext` (added). A private copy of
    `CTool_GetPathName` was removed. The "CodeGen.c's special macros" block (vecM, altivecM) is still there.
  - CObjC.h: `MethRec` is Pro7's ObjCMethod under 1.x names. Its `isinst` is Pro7's `is_class_method` (it selects '+'),
    `ObjCParameterNode.selectorName` is Pro7's `selector`, and `alignmentPadding` is a placeholder field.
  - CExpr2.h: `MatchLink` is a stand-in for ObjectList (used by CExpr2, CExprConvMatch, CTemplateFunc, CClass).
  - Not reviewed in depth: CTemplateNew.h (`ctempl_curinstance`), CClass.h (`CClass_CheckPures`), CMangler.h,
    CompilerTools.h (below), CInt64.h, Unmangle.h, objects.h, scopes.h, templates.h, types.h (`IT_*`), tokens.h,
    InlineAsm.h (`AssemblerError`), CWPluginsPrivate.h (`CWReportMessage`) and DropInCompilerLinker.h
    (`CWDisplayLines`).
- Open: the two invented names and the pack(2) TODO; the CObjC.h and CExpr2.h stand-ins. (H2: the CObjC.h and CExpr2.h stand-ins are resolved, and `data_005830c8` is Pro7's `cerror_synchdata`, in hash order.)
- Adversarial review: I attacked the grade, the needed shapes and the header. Grade: the unit carries two invented names and a pack(2) workaround, so it is not "solid pending headers"; I regraded it SOLID WITH TODOS. Shapes: each claimed one was re-tried in Pro7's spelling with `fn.py variants` or a trial edit, and none matches:
  - ErrorMessage: `unsigned char messagetype` 84.8; Pro7's local order 75.9; separate `anyerrors`/`fatalerrors` stores 91.0.
  - GetNameString's `ptr` local: 97.8.
  - BufferGrow `SInt32`: costs 20 functions.
  - BufferAppendNameSpace's ternary: 66.7, and it breaks TemplArgExpr.
  Header: `data_005830c8`, `cerror_synchoffset` and `CError_BreakPointcount` have no users outside CError.c, but Pro7's CError.h declares all three, so they stay as Pro7's API. Pro7's stale `//token = ...` comment is still in ErrorMessage. Boundary (CError_Init at 0x468100 is highest, QualifierCheck at 0x463710 lowest, between IroVars and IroUtil) and data held. Nothing broke.
- Header round H2 (October 16): the CObjC.h and CExpr2.h stand-ins are gone, `data_005830c8` is `cerror_synchdata`, CERROR_FILE is gone, vecM/altivecM moved to CodeGen.h. The ERR_* names stay TODO-invented. Resolved.

### src/frontend/CInt64.c: SOLID WITH TODOS
- Boundary:
  - Functions: Wii's CInt64 module has the same 46 functions as 2.6's rows. Pro7's dead `CInt64_UnsignedCompare` and
    `CInt64_SignedCompare` (inlined in every comparison), `CInt64_ScanAsmNumber`, `CInt64_PrintHex` and
    `CInt64_PrintBin` are back.
  - .data cut: wrong before. The object's .data starts at **0x5b1ca8** (8-aligned) with `cint64_negone..cint64_min`,
    then ScanAsmNumber's "01", "bB", "234567", "89", "acdefACEDF". The split had given those 0x48 bytes to CMachine.
    Pro7 defines the constants here; they were defined nowhere.
  - .bss is the power-of-two table (0x5e04d0).
- Flags: `-inline auto,deferred` (was `auto`): the rows run in reverse of Pro7's order. Non-deferred gives 38/46, and
  the old source had needed copies of Neg for Mul and Div. -sym on (off: 33/46).
- Functions: 46/46 exact.
  - 2.6 changes:
    - ConvertUFromLongDouble takes the powers of two from a table built on first use (`powers_of_two[65]`,
      `next_power`, `powers_built`, the inline `power_of_two`, read in the disassembly), where Pro7 builds each float's
      bits.
    - PrintDec's buffer is `char buf[32]` (Pro7's is 40; the frame says 32).
  - Needed shapes, each from Pro7's text with Pro7's spelling tried:
    - The header inlines `CInt64_IsNegative`/`CInt64_IsZero` where Pro7 writes the test out (Not, Shr, IsInRange,
      ConvertToLongDouble; setne materialised).
    - UnsignedCompare as an if/else-if chain.
    - DivMod's early return.
    - `lo = 0; hi = 0;` where Pro7 chains them (MulU, the Scan functions).
    - `char ch` with `(ch = *str) >= '0' && ch <= '9'` (Oct, Dec).
    - `char digit` with `digit -= '0'` (Hex).
    - A local copy of `value.hi` (IsInURange).
    - `SInt32 aaaa`, which is added to `result.hi` (MulU).
    - Declaration order (Shl, Shr, ShrU, MulU).
    - `if ((threshold = power_of_two((short)bits)) <= value)`.
  - Workaround (TODO): ConvertUToLongDouble's `float one = 1.0f`. 2.6 adds the low word's bits through `fadd dword`
    (the pooled 1.0f) and the high word's as a double. Tried 1.0f, (float)1.0, (double)1.0f and 1.
- Behaviour: straight from Pro7 plus the disassembly-read table; .data and .bss link byte for byte. Bugs in the
  original: none. Bugs of ours: none in behaviour. The old source was IDA text with about 15 invented helper copies
  (modu, divu, xor64, unmask, isneg_ptr, lneg, not64, `recovered_*` ...) and included IroLoop.h and CExpr2.h. It is
  gone.
- Names: Pro7's, and Wii's for the functions. Invented, with a TODO: `powers_built`, `next_power`, `powers_of_two`,
  `power_of_two` (Wii has no publics for them).
- Types: Pro7's prototypes. The old header had made ConvertInt32 return int, ScanDecString return UInt8*, and one
  parameter `register`.
- Data: deleted bindings: the literal pseudo-names `_data_00555468$4384`/`$4385`/`$4386` and `_DAT_0055542c` (a
  fragment inside `cint64_one`). The `cint64_*` bindings stay: other units' comparisons resolve references through
  them (deleting them cost 99 functions).
- Header: Pro7's CInt64.h (inlines plus every external function).
- Hygiene: TRUE/FALSE, clean.
- Headers relied on: CInt64.h (Pro7's; no problems) and common.h (`CInt64`).
- Open: the four invented names and the `float one` TODO.
- Adversarial review: I re-tried IsInURange's `SInt32 hi` copy. Pro7's direct `value.hi` gives 85.2, an if-form 38.7 and `(int)len` 85.2, so the copy is needed (an `UInt32 hi` matches too, so its signedness is unproven). The boundary held: PrintDec at 0x4c2920 to Init at 0x4c45a0, between CMachine and Intrinsics. The deferred order and the data held too. The grade stands.

### src/frontend/CompilerTools.c: SOLID WITH TODOS
- Boundary:
  - Functions: Wii's CompilerTools module has 2.6's 48 functions, plus later ones. Pro7's dead functions are back
    (UnlockGList, RemoveGListData, GetGList*, hashpjw, PHash, GetHashNameNodeHash*, NameHashExportReset,
    NameHashWriteNameTable, getheapinfo, relheap, balloc, freebheap, ScanHex, ScanOct, UnmangleClassname,
    UnmangleAppend, OldUnmangle, hash, memclr, CTool_CtoPstr, CTool_EndianConvertVector128, strcat_safe).
  - .data cut: wrong before. The object's .data starts at **0x5a8bd8** with `bit_masks[32]`, then OldUnmangle's
    "operator new", "operator delete", "(void)", "(...)", then NameHashWriteTargetEndianNameTable's "", then
    GetHeapInfo's jump table. The split had all of it in CPrep's range; bit_masks was defined nowhere. In Pro7's
    position NameHashWriteTargetEndianNameTable links; the old comment that moved it was wrong.
  - Left out: Pro7's `CompilerGetPString`. It calls `COS_GetPString`, which 2.6's COSToolsCLT lacks (so does Wii's),
    and the relink fails on the undefined symbol.
  - Left out: Ninji's CW_64_BIT_SUPPORT pool, which is not original code.
- Flags: deferred (rows in reverse of Pro7's order; non-deferred gives 37/48), -sym on (whole-unit -sym off: 26/48).
- Functions: 48/48 exact.
  - 2.6 changes (from the disassembly): the endian functions return early on `copts.littleendian`, and
    EndianConvertWord64 orders the words by it. Pro7's spellings with the test added reach 51-97%; the union form
    (Word32 now with `UInt32 w`) matches.
  - Needed shapes:
    - `curfreep += s` in the four allocators.
    - `gl->size = gl->hndlsize = 0` in FreeGList.
    - `name_hash_nodes[n] = node = galloc(...)` in GetHashNameNode.
    - `size_t n` in AppendGListID/Name (Pro7: UInt32).
    - `UInt16`/`UInt32` parameters for AppendGListTargetEndian*.
    - The statics declared in reverse of Pro7's order (the .bss has lheaplockcount lowest).
    - CToLowercase in gc1's while form (Pro7's do/while: 51%).
  - Workarounds (TODOs):
    - `#pragma pack(2)` at the end of the file (heaperror at +2). The old source had it at the **top**, where it also
      packed the file's own declarations.
    - One `#pragma sym off` region over freelheap, freeaheap and freeoheap. It replaces three per-function pragmas;
      tried a chained assignment, a for loop and other reads; -sym off for the unit loses 22 functions.
    - A local inline `CTool_ToLower` (`__lower_map`). 2.6's MSL inlines tolower; include/libc's ctype.h declares it
      out of line for the driver. CPrep, ObjGen_PPC_EABI and CCompiler keep the same copy.
- Behaviour: Pro7's plus the 2.6 endian checks; the data links. Bugs in the original: none. Bugs of ours fixed:
  - `CTool_GetPathName` and `COS_FileGetPathName` took a `CPrepFileInfo*` stand-in where the original takes an FSSpec.
    Callers cast `(CPrepFileInfo*)&x.filespec`, and COSToolsCLT read `record->textfile`. Now `const FSSpec*`, with
    callers passing `&file->textfile` (CPrep, CError, CPreprocess, IroDump, ObjGen_PPC_EABI).
  - The invented `CompilerTools_ConvertCStringToPString` is replaced by Pro7's `CTool_CtoPstr` (dead; no data).
- Names: Pro7's/Wii's. No invented names left (`CTool_ToLower` is a TODO'd workaround helper).
- Types: Pro7's HeapBlock/HeapMem/HeapInfo/GList, `heaperror_t`, `long hash_name_id`, `short InitGList/initheaps`.
  GList's old `#pragma pack(push, 1)` was dropped (the build is unchanged and everything relinks).
- Data: bit_masks is defined. The statics are static. No bindings fall inside the object's ranges.
- Header: rewritten as Pro7's CompilerTools.h. The linker-error declarations (`linkerror`, `linkwarning`, `maxerrors`,
  `maxwarnings` ...) moved to a new include/driver/LinkErrorsPPC_EABI.h, included by its two users. `systemHandles` is now
  declared in driver/CCompiler.h (Pro7's place).
- Hygiene: TRUE/FALSE, clean.
- Headers relied on:
  - CompilerTools.h itself still declares COSToolsCLT's COS_* functions (Pro7's cos.h; 20 users). They belong in a
    cos.h; open.
  - CPrep.h: copts. CInt64.h. win32.h: `Handle`.
  - LinkErrorsPPC_EABI.h, now separate, declares that unit's COMMONs under main's names `linkerwarnings` and
    `linkererrors` (the old `data_00588228`, `limited_diagnostic_count`); `data_00587958` is main's
    `linkinfo.nowarnings` (ELF_Lib_Import.h).
- Open: the COS declarations; the three TODO workarounds. Also open:
  - The header's `CTool_EndianConvertInPlaceWord16Ptr(short*)` / `Word32Ptr(unsigned int*)` are not Pro7's `UInt16*`/`UInt32*` and were not explained. Pro7's types match in this unit (with Pro7's `v` local dropped: `return *x`). They fail only because ELF_Endian.c's callers pass `short*`/`unsigned int*` and read `*(short*)` signed, so this belongs to ELF_Endian's review.
  - `CTool_EndianConvertMem(void* data, ...)` (Pro7: `UInt8*`) is needed: with `UInt8*`, Pro7's `while (--b > a)` loop gives 81.8-86.4, and ELF_Endian passes `&bytes`.
  - The COS block has an invented `CompilerTools_MakeCWFileSpecFromPString(void*, ...)` with no TODO, and `TickCount` returning `unsigned int`.
- Adversarial review: I attacked the needed shapes, the header and the grade.
  - Grade: three TODO workarounds, so SOLID WITH TODOS.
  - Fixed: EndianConvertWord64's `copts.littleendian == 0` is now the plain `!copts.littleendian`, which also matches.
  - Fixed: `systemHandles` (CCompiler.c's) is no longer declared in CompilerTools.h. It is in include/driver/CCompiler.h, as in Pro7's CCompiler.h, and CompilerTools.c and COSToolsCLT.c include it.
  - Held: dropping the `#pragma sym off` region loses all three free*heap (41-45%). Pro7's do/while CToLowercase gives 51.4. The `(char)` cast and `!= 0` are gc1's text (`while ((*dst++ = (char)tolower(*src++)) != 0)`). EndianConvertMem's `for (;;)` form is needed (Pro7's while: 86.4). InPlace's `return *x` is needed (a `v` local: 96.6/97.6).
  - The private `CTool_ToLower` is one of four identical copies (CPrep, ObjGen_PPC_EABI, CCompiler). The fix belongs in include/libc/ctype.h, a shared header, which needs an inline `tolower` for the compiler's units and an out-of-line one for the driver's.
- Header round H2 (October 16): the COS_* declarations live in COSToolsCLT.h (Pro7's cos.h); the endian converters take `UInt16*`/`UInt32*`; `CTool_EndianConvertMem(void*)` is 2.6's (commented); MakeCWFileSpecFromPString is gone, TickCount is DateTimeUtils.h's `UInt32`. Resolved.

### src/frontend/CMangler.c: SOLID WITH TODOS
- Boundary: the 18 rows are Pro7's file reversed (Wii's CMangler module agrees, plus later functions). Pro7's
  statics are inlined. .data 0x5bd1c8-0x5bd820 links.
- Flags: deferred (non-deferred: 12/18), -sym on (off: 13/18).
- Functions: 18/18 exact.
  - 2.6 changes, all in the data or the disassembly:
    - Link names have no leading underscore: DataLinkName appends "" of length 0 (the "" at 0x5bd1d4, `push 0`), and
      ThunkName uses "@%ld@" and `linkname->name`.
    - The integral types 17..24 mangle as '0'..'7' (19 is `IT_VEC2X32FLOAT`).
    - GetLinkName answers 8 the way Pro7 answers DLABEL, and has no DNONLAZYPTR case.
  - Needed shapes: MangleTypeAppend returns from each case and uses an early return/else for member pointers and
    structs. Pro7's break/else forms reach 92.6-98.6%. MangleNameSpaceName walks with a local `scan` (Pro7's loop on
    the parameter: 94.8%).
  - Fakematch (TODO, kept): `CMangler_CheckTemplateArguments((TemplArg*)(void*)args)` in TemplateInstanceName. 2.6
    tests the inlined callee's copy of `args`. Tried a local copy, a for loop, a const parameter and statement order.
- Behaviour: straight from Pro7 apart from the 2.6 changes above. Bug of ours fixed: the TPT_TEMPLATE argument read
  `data.typeparam.type` where Pro7 reads `data.ttargtype` (same offset).
- Names: Pro7's. `DUNUSED` in objects.h is renamed to `DLABEL` (value 8, with a comment); CMangler was its only user.
- Types: Pro7's.
- Data: no bindings in its ranges.
- Header: Pro7's CMangler.h.
- Hygiene: clean. Includes trimmed (CPrep.h and CTemplateFunc.h were unused).
- Headers relied on:
  - types.h: has no names for IT_17..IT_24 apart from the invented `IT_VEC2X32FLOAT`; CMangler uses 17, 18 and 20-24 as
    numbers.
  - objects.h: DataType (fixed).
  - CExpr2.h: `is_pascal_object` and the MatchLink stand-in.
  - CScope.h (`cscope_root`), CParser.h (`name_mangle_list`), CompilerTools.h (COS, above), CTemplateTools.h, CFunc.h,
    scopes.h, enode.h: used for their real declarations; not reviewed in depth.
- Open: the void* cast TODO; types.h's IT names. Pro7's types.h has `IT_17`..`IT_24` names. The repo's lacks them, so CMangler uses 17, 18 and 20-24 as bare numbers.
- Adversarial review: the grade was wrong. A fakematch (the `(TemplArg*)(void*)` cast) cannot be "solid pending headers", so I regraded it SOLID WITH TODOS.
  - Re-tried the natural spellings; each gives 99.0, with only `test ebx` against 2.6's `test edi`: a plain call, `(TemplArg*)args`, extra parentheses, a comma expression, and a local copy in the caller.
  - In the callee: a `TemplArg* arg = args` copy (79.2), the loop written out by hand in the caller (79.2), `!= NULL`, and moving the `size = 0` store first (96.6).
  - A `void*` parameter in the callee also matches, but it is no more natural. The TODO stands.
  - Not in the entry before: 2.6's FunctionLinkName returns `obj->name` for unmangled names, and DataLinkName returns it when there is no namespace. Both are exact, as 2.6 changes.
  - Boundary (0x4fe6a0-0x5004c0) held.
- Header round H2 (October 16): types.h has Pro7's `IT_17`..`IT_24` and CMangler uses them; MatchLink is gone; is_pascal_object is CParser.h's. Resolved.

### src/frontend/Unmangle.c: SOLID
- Boundary: the 13 rows are Pro7's Unmangle.c with its five buffer helpers and Unmangle_ClassName inlined. Wii's
  Unmangle module also starts with MWUnmangle. .data 0x5bf9e8-0x5c029c links.
- Flags: `-inline auto,deferred` (was `auto`). Every function also matches non-deferred, but the relink then fails:
  the object's order and data are deferred's. -sym on (off: 11/13).
- Functions: 13/13 exact.
  - 2.6 change: `""[0]` for Pro7's `"_"[0]` (no underscore prefix on x86).
  - Needed shapes:
    - `p--`/`p++` before the `what =` beside it (4 places in SpecialName) and before the flag in FuncArgList (the
      catalogued counter-first shape).
    - `char ch` (ClassName, QClassName, MWUnmangle).
    - ClassName's `while (len-- != 0)` with `if ((ch = *(p++)) == 0)`.
    - A separate `lastLen` in QClassName (Pro7 reuses `len`: 66%).
    - MWUnmangle's errorbuf declared first.
    - Each tried in Pro7's spelling first.
- Behaviour: Pro7's. Bug in the original (kept, with a comment): the literal "vector bool int " has a trailing space.
  The old source was IDA text with invented helpers (CopyTemplate, AppendText, put, put_2, put_c, scan,
  scan_component, PutChar), a stand-in `MOutBuf` and `int*` for jmp_buf. It is gone.
- Names: Pro7's. None invented.
- Types: Pro7's `UnmangleBuffer`, `size_t` lengths. `MOutBuf` is gone from common.h.
- Data: literals only, linking.
- Header: Pro7's (MWUnmangle and MWUnmangleClassName; the other twelve were internal).
- Hygiene: TRUE/FALSE, clean.
- Headers relied on: Unmangle.h (stddef.h for size_t) and common.h (Boolean); no problems.
- Open: none.
- Adversarial review: I re-ran Pro7's spelling of the shapes. The boundary (0x526980-0x528110), the deferred order and the data held. I found no fakematch. It depends only on common.h (shared, not reviewed). SOLID stands.

#### Shared evidence for CLDropinCallbacks_V10.cpp, CLCompilerLinkerDropin_V10.cpp, DropInCompilerLinkerPrivate.cpp, CCompiler.c, cc-eabi-ppc.c, cc-eabi-ppc-mw.c, libimp-eabi-ppc.c, ClientGlue.c, COSToolsCLT.c, LinkErrorsPPC_EABI.c

# Review: the driver's plugin glue (bundle Z_DR3)

Every unit below is exact and links (`relink.py --only`). Across the whole build nothing lost its `exact` flag
against compare_start.json (4020/4077), and one unit was added (DateTimeUtils.c, Matching, links: 2806 linked
functions). The headers these units share are listed once, after the entries.

### src/driver/CLDropinCallbacks_V10.cpp: SOLID WITH TODOS
- Boundary: 47 rows, 0x424660-0x426ed0, in Pro7's order. Its .data (0x5a4848) holds Pro7's literals, so Pro7's dead
  `UCBMacOSErrToCWResult` stays (its literal is there). The ??4OSTime/OSSpec/OSNameSpec rows are the implicit
  operator= copies that `-inline off` keeps out of line.
- Flags: `pro6 -O4,p -inline off -sym off -bool off`, and each one shows in the code: without `-bool off` 41/47
  match, with `-inline auto` 41/47, with `-sym on` 38/47. `-bool off` replaces the old `#pragma bool off`. The
  `CERROR_FILE` define is gone (nothing used it) and so is `#define free xfree` (Pro7 calls xfree).
- Functions: 47/47 exact, rebuilt from Pro7's text. Where 2.6 departs from it:
  - 2.6 changes, each seen in the disassembly:
    - Win32 buffer sizes: `fullpath`/`filename` are 260 where Pro7 has 256 (seen in the frame sizes and pushes);
      UCBGetFileText's `filename` is 256 where Pro7 has 64.
    - `OSTime mdtm` (FILETIME) where Pro7 has `time_t`.
    - UCBCheckoutLicense/UCBCheckinLicense really check the license out and in through CLLicenses (Pro7's are
      stubs). They were rewritten in Pro7's style with `autoCheckin`, `cwErrLicenseCheckFailed` and `CLStr87`.
    - `isExecutableTool` where Pro7 has `dropInExecutableTool`.
  - Shapes MWCC needs (in each case Pro7's spelling was tried and does not match):
    - Locals declared in another order: UCBGetFileInfo needs `tmptime` first.
    - Chained assignments: FindAndLoad_VirtualFile's `vRefNum = parID = 0`, UCBAddProjectEntry's
      `writeToDisk = wroteToDisk = tempOnDisk = 0`, UCBCreateNewTextDocument's `entry.*` chains.
    - FindAndLoad_MissingFile's `!(a || b)` where Pro7 has `!a && !b`.
    - UCBGetNamedPreferences keeps the handle in a local before the call.
    - UCBReportMessage has no `clx` local and declares `eeptr` separately in each arm.
    - UCBAddProjectEntry uses one `index` local and `failed` where Pro7 has ternaries.
    - UCBCreateNewTextDocument puts GetHandleSize into `size` locals and uses an if/else where Pro7 has a ternary
      of fopens.
  - Fakematch (TODO): UCBSecretAttachHandle stores through `*reinterpret_cast<Handle*>(memHandle)` (was
    `*(Handle*)memHandle` and `return 0`; now the C++ cast and `cwNoErr`, same code). Pro7's
    `*memHandle = reinterpret_cast<CWMemHandle>(handle)` scores 69.2% and `*reinterpret_cast<CWMemHandle*>(&handle)`
    66.7%: 2.6 loads both parameters at the store, only a cast of the parameter to another pointer type does.
- Behaviour: checked statement by statement against the old exact text (IDA-derived) and Pro7. Genuine bug in the
  original, kept: UCBAddProjectEntry calls Files_AddFile when `position != -1` and
  `Files_InsertFile(..., -1)` otherwise (Pro7 has the same inverted test). Bugs of ours fixed: none. The old
  version's magic numbers were all right; they are named now.
- Names: Pro7's throughout (rows confirmed in names-2.6.md). No invented names.
- Types: the SDK's/Pro7's (`CWPluginContext`, `CWFileInfo`, `MessageRef`, `Msg_*` for the
  CLStyledMessageDispatch types). There were no stand-ins left.
- Data: links byte for byte, including Mac_To_ANSI_Time's function-local `monthdays` and `fmts`, and
  `sBasePluginCallbacks` and `sIDEInfo`.
- Header: CLDropinCallbacks_V10.h now declares only the five callbacks other units call (UCBFindAndLoadFile,
  UCBGetFileText, UCBReleaseFileText, UCBSecretAttach/DetachHandle), plus shellContextType and
  IDEAccessPath(List). The other 34 prototypes (UCBCheckoutLicense's included; its `UInt32 flags` comment moved
  to the definition) had no user outside the file.
- Headers relied on: see the shared list (CLDropinCallbacks_V10.h, CWPluginsPrivate.h, CWPlugins.h, CLErrors.h,
  CLIO.h, CLFiles.h, oslib.h, libc).
- Open: the UCBSecretAttachHandle cast (TODO).

### src/driver/CLCompilerLinkerDropin_V10.cpp: SOLID WITH TODOS
- Boundary: 22 rows, 0x426fc0-0x4284bc, in Pro7's order. Pro7's framework callbacks are absent from 2.6's table and
  are dropped. Pro7's destructor is kept: it is dead and the linker drops it.
- Flags: `pro6 -O4,p -inline off -sym off -bool off`. Without `-bool off` 20/22 match, `-inline auto` 20/22,
  `-sym on` 18/22. The `CERROR_FILE` define is gone.
- Functions: 22/22 exact, rebuilt from Pro7's text. Where 2.6 departs from it:
  - 2.6 changes:
    - `OSTime unittime`.
    - `sbmpath`, `unitname` and `target` are 260 bytes (Pro7 256).
    - 260 is passed as the size of File's 256-byte `srcfilename`/`outfilename`. It is spelled `sizeof(sbmpath)` /
      `sizeof(target)`; that spelling is a guess, marked in the code.
    - The constructor chains `targetCPU = targetOS = targetCPUAny` and `precompile = autoprecompile =
      preprocess = 0`. Both are seen in the reloads in the disassembly.
  - Needed shapes:
    - UCBCachePrecompiledHeader declares `handle` before `spec`.
    - `!optsCompiler.relPathInOutputDir` where Pro7 has `== 0`: Pro7's spelling compiles to a sete.
- Behaviour: as Pro7, with 2.6's sizes. Genuine bug in the original: up to 260 bytes can be written into 256-byte
  File name fields. Ours: none.
- Names: Pro7's. Types: Pro7's/the SDK's (`OptsCompilerSbmState_*`, `cwErrSBMNotFound`, `CmdLineStageMask_Cg`,
  `DO_INTERNAL_ERROR`). The old `(short)(3U)`, `9U` and `0x436f6d70` spellings are gone.
- Data: links byte for byte.
- Header: none. CLCompilerLinkerDropin_V10.h declared its 20 table entries and only its own source included it,
  so it was deleted (the source defines every entry before the table).
- Headers relied on: see the shared list.
- Open: the size spelling noted above (a `TODO`-level guess, not a fakematch).

### src/driver/DropInCompilerLinkerPrivate.cpp: SOLID WITH TODOS
- Boundary: 18 rows, 0x43ffe0-0x4405f0, Pro7's PluginLib file. The functions nothing calls are absent from 2.6.
  Wii's module has the same file.
- Flags: `pro6 -O4,p -inline off -sym on`. `-sym on` for the whole file replaces two per-function
  `#pragma sym on` blocks and a `#pragma auto_inline off` (all three removed). `-inline auto` gives 1/18 (the
  operator= copies get inlined).
- Functions: 18/18 exact, Pro7's text verbatim. The old decompiler-shaped text (parameters named `key`,
  `selector`; literal 3/4 returns) was replaced. Departures from Pro7:
  - 2.6 change: CWGetTargetInfo copies the version 8 layout in one step,
    `*(CWTargetInfo_V8*)targetInfo = *(CWTargetInfo_V8*)cl->targetinfo`, which is the ??4CWTargetInfo_V8 row
    (0x4402e0 copies 0xf0 bytes, Pro7's Mac-host fields included, where ??4CWTargetInfo goes on to
    linkAgainstFile: so 2.6 had a struct type of its own for it). The type's name is invented (TODO in
    DropInCompilerLinker.h), which is why this is not SOLID.
  - Convention: `#define GetContext DropInCompilerLinkerPrivate_GetContext`, because CWParserPluginsPrivate.cpp's
    row already uses that name (row names are global).
- Behaviour: Pro7's. Names: Pro7's; names-2.6.md's "plausible" Wii disagreements are Wii's shifted order. Each
  body's callback slot is the named one. Types: the SDK's.
- Data: none. Header: DropInCompilerLinker.h (the SDK's) declares them. OK.
- Headers relied on: DropInCompilerLinker.h (the CW* prototypes, CWTargetInfo(_V8), CWObjectData; `CWTargetInfo_V8`
  is an invented name, TODO already there), CWPluginsPrivate.h (CWCompilerLinkerContext), CWPlugins.h (added
  `CWFileSpecNotEmpty`).
- Open: the invented `CWTargetInfo_V8` (no source names it).

### src/driver/CCompiler.c: SOLID WITH TODOS
- Boundary: 4 rows, 0x41f1d0-0x41fa60. Pro7's get_extension, store_compile_results, GetLicense and ReleaseLicense
  are inlined. The dead `CWPlugin_GetHelpInfo` is kept for its literal "CCompiler.hlp". Wii's CCompiler module
  agrees.
- Flags: `pro6 -O4,p -inline auto,deferred -sym on`. Deferred because the rows descend against Pro7's order:
  PrintProgressFunction, MWC_main, initialize_compiler_options and setup_param_block are Pro7's last four in
  reverse. The old flags lacked deferred. Without it the functions compile to the same bytes, but in Pro7's
  address order. `-sym off` gives 2/4.
- Functions: 4/4 exact. The old file was 1.x-shaped and used stand-in structs. It is now Pro7's text with these
  departures:
  - 2.6 changes:
    - Licensing: `crippled = FALSE`, then "Win32_Plugins_PPC_Nintendo" version "2" on a Japanese system (language
      0x411), else "1".
    - The panel is "EPPC Global Optimizer".
    - `inlinemaxtotalsize = 7000`.
    - setup_param_block returns `Boolean` (`xor al, al`).
    - Pro7's function-local `target_name` and `pFrontEnd` are at file scope; 2.6's .bss order shows it (and
      AGENTS records it).
  - Needed shapes:
    - `!cparams.preprocess` where Pro7 has `== 0`.
    - setup_param_block's final test is written as an if.
    - Local declaration order (from decl-order).
  - The language test is `if ((Boolean)(GetSystemDefaultLangID() == 0x411))` (`sete; and; test al`), the
    file's own idiom (Pro7's `(Boolean)(pGlobalOptimizer.optfor == 1)`). The reviewer's invented
    `IsJapaneseSystem` helper is gone: the cast and a `Boolean` local match too, the plain comparison does not
    (96.5%). A `LANGID`-typed (`unsigned short`) prototype gives a movzx 2.6 lacks (99.2%).
  - setup_param_block's returns are `TRUE`/`FALSE` (were 0/1).
  - TODO: `cc_tolower` stands in for MSL's inline `tolower`, which include/libc/ctype.h lacks.
- Behaviour: checked against the disassembly. Fixed on the way (layout-neutral):
  - `COpts.oldprefixname` is now Pro7's `StringPtr`.
  - `cparams` is defined here (Pro7) instead of in CodeGenOptPPC.c.
  - `crippled` is Pro7's name for the old `data_005e73cf`; the name fits the COMMON hash order (1468, between
    `symFullPath` 1426 and `preprocessing_only` 1553).
- Names: Pro7's (`MWC_main`, `crippled`, `systemHandles`, `license_cookie`, `cparams`). Invented: `cc_tolower`
  (TODO, a stand-in for MSL's inline `tolower`).
- Types: Pro7's pref_structs (`PFrontEndC`, `PWarningC`, `PGlobalOptimizer`) replace the stand-ins
  `DriverSettings`, `LanguageSettings` and `SymbolSettings` (b[9], options[15], tail[4] ...), which were deleted.
  `CWPluginContext` throughout.
- Data: links byte for byte (.data and .bss).
- Header: CCompiler.h holds only what others use: `systemHandles`, `crippled`, `cparams`, MWC_main,
  PrintProgressFunction (`license_cookie` removed: no user outside the file). The private `struct CWHelpInfo`
  (a copy of an SDK type) is gone: CWPlugins.h now has the SDK's `CWHelpInfo` and `kCurrentCWHelpInfoVersion`,
  and CWPlugin_GetHelpInfo uses them.
- Headers relied on: pref_structs.h, CWSecretPluginCallbacks.h (CWSecretGetNamedPreferences still takes `void*`
  prefsdata, TODO there), CWPluginsPrivate.h, CPrep.h (COpts, CompilerLinkerParamBlk, C_Compiler), libc.
- Open: `cc_tolower` (TODO; needs MSL's inline `tolower` in include/libc/ctype.h).

### src/driver/cc-eabi-ppc.c: SOLID
- Boundary: 17 rows, 0x40bfb0-0x40c190: Pro7's cc-mach-ppc.c, with 2.6 placing CmdLine_GetObjectFlags after
  GetFileTypeMappings. Its .data includes the generated cc-eabi-ppc.r tables.
- Flags: now `pro6 -O4,p -sym on` (`-sym off` gives 2/17). The old setup was a pro53 compiler override plus
  `#pragma scheduling off` around the file, both unneeded. Pro6 matches all 17 with Pro7's spellings, and the data
  links. (Replay: 2.0, 2.0p1, 2.5 and 2.7 had the same pro53 override; without the pragma pro53 lost all 17 there,
  so the override is dropped in every version, which brings them back.)
- Functions: 17/17 exact, Pro7's text. Departures:
  - 2.6 changes: 'ePPC'/'EABI', 2.6's names, panels, flags and version, the 'ELF ' object type, and four
    RegisterResource tables.
  - Original's shape: `const char* name = "..."` in Linker_GetDropInName/DisplayName. Pro7's direct store scores
    44%; 2.6 loads the literal into ecx first. This is a plausible local and not a fakematch.
  - The `int result = 0` locals and the `(void*)` casts in the callback tables were not needed and are gone. So is
    the comma-chained `unsigned int` RegisterCompilerResources: Pro7's `a && b && c && d` matches.
- Behaviour: data unchanged. Names: Pro7's. The statics follow Pro7: Pro7-static functions are static.
- Types: SDK constants (`kCurrentDropInFlagsVersion`, `DROPINCOMPILERLINKERAPIVERSION_8/12`, added to
  DropInCompilerLinker.h, `kGeneratescode`, `kIgnored`, `kPrecompile`) replace the literals.
- Data: links byte for byte.
- Header: cc-eabi-ppc.h now has only RegisterStaticCompilerPlugin and RegisterCompilerResources (`int`, Pro7).
- Headers relied on: CWPlugins.h, DropInCompilerLinker.h, ClientGlue.h, CCompiler.h.
- Open: none.

### src/driver/cc-eabi-ppc-mw.c: SOLID
- Boundary: 5 rows, Pro7's Static/cc-mach-ppc-mw.c (Wii "cc-eppc-mw"). Flags: `-O4,p -sym off`; `-sym` makes no
  difference here.
- Functions: 5/5 exact, Pro7's text. 2.6 changes: 'ePPC'/'EABI', `Lang_Any`, and no OS_UseMacResourceForkInfo call
  (30 bytes, no call). The old `success` local and `unsigned int` return were not needed.
- Behaviour/Names/Types: Pro7's, with `OSType` parameters and `CWDROPINCOMPILERTYPE`. Data: none.
- Header: cc-eabi-ppc-mw.h has the 5 functions (CLStaticMain uses them), with OSType.
- Open: none.

### src/driver/libimp-eabi-ppc.c: SOLID
- Boundary: 11 rows, Pro7's libimp-mach-ppc.c (Wii's imp-elf renamed it). Flags: `-O4,p -sym on` (`-sym off` 8/11).
  Its `#pragma scheduling off` was unneeded and is removed.
- Functions: 11/11 exact. 2.6 changes: ELF mappings and magic, 'ePPC'/'EABI', `ELFImport_main`, and
  RegisterLibImporterResources returning 1 (no error table). The `(void*)` casts are gone; statics follow Pro7.
- Names: Pro7's (Wii names RegisterImporterResources; the body is Pro7's, so Pro7's name stays). Types: SDK
  constants (`kGeneratescode | kCompMultiTargAware`, `Lang_MISC`).
- Data: links. Header: only the two Register functions.
- Headers relied on: CWPlugins.h, DropInCompilerLinker.h, ELF_Lib_Import.h (`ELFImport_main`; see below).
- Open: none.

### src/driver/ClientGlue.c: SOLID
- Boundary: 10 rows, Pro7's file, the same as Wii's ClientGlue. Flags: `-O4,p -sym off` (`-sym on` gives 6/10).
- Functions: 10/10 exact, Pro7's text. The only departure is the 2.6 change to `__stdcall` (Wii's `@N`
  publics). It now uses `OSType` parameters and TRUE/FALSE/NULL.
- Includes trimmed (objects.h, scopes.h, CLFileOps.h and CLIO.h were unused).
- Header: ClientGlue.h is reduced to its own API. Removed: the junk macros `GLUECALL`, `STRINGS_EQUAL`,
  `STRINGS_DIFFER` and `CLIO_STRINGS_EQUAL`, and the MsDos.h and StringExtras.h includes. Their users were
  rewritten to Pro7's spellings: ParserFace `!ustrcmp(...)`, CLCompilerLinkerDropin `!OS_EqualPath(...)`, and
  Parameter.c now includes StringExtras.h.
- Open: none.

### src/driver/COSToolsCLT.c: SOLID
- Boundary: 23 rows, 0x442100-0x442480: Pro7's PluginLib Internal/COSToolsCLT.c (Wii "COSToolsWin32"). Moved out:
  - TickCount (0x442480) is Wii's DateTimeUtils module, now its own unit (next entry).
  - The date tables and dead date formatters went with it.
  - The dead, invented `CompilerTools_MakeCWFileSpecFromPString` was deleted: no row and no data.
  The object has no .data of its own.
- Flags: `-O4,p -sym on` (`-sym off` gives 14/23).
- Functions: 23/23 exact, Pro7's text verbatim. Departures:
  - 2.6 changes: COS_pstrcpy returns `void` (Pro7's return compiles a `mov eax, edx` that 2.6 lacks), and
    COS_MakePath passes 260.
  - The old gc1 stand-ins were reverted to Pro7's: `int cnt` with `(short)cnt`; the `UInt16*`/`void*` and
    `CPrepFileInfo*` parameters; parameter names like `category`/`recordId`; the `void` returns of COS_FileClose.
- Behaviour: Pro7's. Fixed in callers (code unchanged):
  - COS_FileGetPathName and CTool_GetPathName take `const FSSpec*` (Pro7), so CPrep, CPreprocess, IroDump,
    ObjGen and CError (`va_arg(args, FSSpec*)`) pass `&x->textfile`/`&fileinfo.filespec` instead of casting a
    spec to `CPrepFileInfo*`.
  - CPrep's `file_vrefnum` and CPreprocess' `tmp16` are Pro7's `SInt16`.
  - Pascal-string buffers are cast to `StringPtr` (BE_elf, CParser, CPrep).
- Names: Pro7's. Types: Pro7's cos.h (`OSErr`, `OSType`, `FSSpec*`, `StringPtr`, `SInt32 newSize`).
- Data: none. Header: new include/driver/COSToolsCLT.h (Pro7's cos.h, 2.6's functions). The COS_* declarations,
  `systemHandles` and TickCount were removed from compiler/CompilerTools.h, and every user includes
  COSToolsCLT.h. `systemHandles` moved to CCompiler.h.
- Open: none.

### src/driver/DateTimeUtils.c: SOLID WITH TODOS
- New unit, split from COSToolsCLT.c.
- Boundary: TickCount (0x442480) is in Wii's own DateTimeUtils module, between COSToolsWin32 and ELF_Endian as in
  2.6. The date tables and the gc1 "COS_DateString/COS_TimeString" bodies are not in any Pro7 COS file; they are
  Mac DateTimeUtils emulation and come with it. The .data cut (0x5a8d20-0x5a8eb0) moved to this unit: config.json
  `data_at` was moved from COSToolsCLT.c. Also added to sources.json and state.json (Matching), with every
  version's functions.json row updated by `unit.py move`.
- Flags: `-O4,p -sym on` (`-sym` makes no difference).
- Functions: 1/1 exact (TickCount, now `* 60 / 1000` and `UInt32`; `__stdcall` since the adversarial pass, as
  Wii's `_TickCount@0` and the other Mac emulation calls: the binding is `_TickCount@0` in every version). The two dead formatters keep the original's
  data (tables, then the sprintf formats).
- Names: invented (TODO), IUDateString/IUTimeString after the Mac APIs: the DateForm switch and the
  `wantSeconds` flag match them. The old names were gc1's COS_* inventions.
- Data: links byte for byte (`data_at` .data 0x5a8d20). Caveat: Wii's DateTimeUtils object has no .data and no
  Wii build has these tables, so their owner is an inference (an 8-aligned run between LinkErrors' and
  ELF_Endian's .data, after the only two candidates COSToolsCLT and DateTimeUtils; COSToolsCLT's Pro7 file has
  no date code).
- Header: DateTimeUtils.h (TickCount, used by ELF_Lib_Import).
- Open: the invented names (no source exists to take them from).

### src/driver/LinkErrorsPPC_EABI.c: SOLID WITH TODOS
- Boundary: 5 rows, the same as Wii's LinkErrorsPPC_EABI module. Its .data/.bss cut is right: it links. Its dead
  "Internal Memory Error" literal is kept through a dead function.
- Flags: `-O2,p -sym off` (`-O4,p` 2/5, `-sym on` 4/5).
- Functions: 5/5 exact. There is no Pro7 or Wii text for this file; the reference is gc1 plus IDA:
  - The private `VA_ARG` macro and the hand-computed `(char*)&parm + ...` va_starts were replaced by MSL's
    `va_start`/`va_arg`, with `va_list` parameters.
  - linkwarning returns `void`: it never set a return value.
  - linkfailed is `(SInt32 code, int status, ...)`: its arguments start after `status`, as the code shows.
- Names:
  - The counters are Wii's `linkererrors` and `linkerwarnings` (for `limited_diagnostic_count` and
    `data_00588228`). Wii's linkerror/linkwarning read them, and both names fit the COMMON hash order (1054
    between 1040 and 1071; 3512 between 3500 and 3519). They were renamed in every version's bindings.
  - Invented (TODO): `format_string` (gc1's; Wii's static is unnamed), `message_buffer` (Wii's 0x800-byte .bss
    static, unnamed; it had no TODO) and the dead `LinkErrors_MemoryErrorMessage`.
  - `data_00587958`, the importer preference that silences warnings, is main's `linkinfo.nowarnings` (ELF_Lib_Import.h's
    LinkInfo, invented names with a TODO there); linkfailed's jump buffer is main's `linkerexit` (was `file_input_jmpbuf`).
- Types: OK. Data: links (`maxerrors`, `maxwarnings` are Wii's names).
- Header: new include/driver/LinkErrorsPPC_EABI.h with the four link* functions (ELF_Lib_Import calls them).
  The declarations were in compiler/CompilerTools.h and were removed there. `maxerrors`/`maxwarnings` were taken
  out of it (no user outside the file; they stay global, as Wii's publics). The counters' externs went to ELF_Lib_Import.h, which defines them.
- Headers relied on: COSToolsCLT.h, ELF_Lib_Import.h (stand-ins, see below), Unmangle.h.
- Open: the invented names (and LinkInfo's, in ELF_Lib_Import.h).

### src/frontend/CObjC.c: SOLID WITH TODOS
- Boundary: 0x5518b0-0x556d20, .data 0x5cfbb0-0x5cfed8, .bss 0x5e3ab8-0x5e3ac0 (Pro7's two statics `cobjc_categories`, `cobjc_strings`). The rows are exactly Pro7 CObjC.c's out-of-line functions in reverse order; every other Pro7 function is inlined (FindProtocol, FindSelector, NewSelector, IsSameType, GetClassRefObject's callers, MakeObject, FindSendMessageRTFunc, FindObjCClass, FindObjCType, GetObjCType_Class, FindObjCType_SEL, IsType_SEL, ParseTypeName, CloneMethod, AddProtocolMethods, AppendArgument, GetMethodObject, AddMethod, IsSameProtocolList, EncodeTypeStruct, Encode*String, IsSameMemberList, CoerceMethodArgs). Dead `CObjC_GetClassRefObject` is kept: removing it leaves .data 28 bytes short (its "L_OBJC_CLASS_REFERENCES_%ld"). Pro7's dead `CObjC_ParseIdentifier` stub has no data or row and is left out. Asserts are "CObjC.c" with Pro7's lines. Rows renamed: NewSelector->CObjC_SectionString, IsIdOrSelType->CObjC_IsType_id, FindObjCClass->CObjC_GetObjCType_id, DefineProtocolMethodList->CObjC_DefineProtocolMethodListObject (by body and Pro7 order).
- Flags: now `-inline auto,deferred` (was `auto`): rows descend against Pro7's order, and with `auto` all 50 compile exact but the relink fails (functions come out in the wrong order). `-sym off` loses 17 functions, so `-sym on` stays.
- Functions: 50/50 exact. Rewritten from Pro7's text (the old file was gc1 text with stand-in types). Departures:
  2.6 changes (each confirmed in the disassembly): the RT send functions' second names have no `_`; NewObjCClass leaves the class objects' sclass alone; MangleMethodName first appends `""` (byte_5cfd84, as gc1 does); EncodeType gives wchar_t 'S'; GetProtocolObject's isa is the Protocol class object and its fields are `CTool_EndianConvertWord32(0)`; DefineClassObjects points the metaclass's isa at itself and the super links at the base's class/metaclass objects; ParseMessageExpression starts a super send at the super class and sends class messages to the class object itself (so GetClassRefObject is dead); MakeSendMsgExpr's super uses the class's metaobject directly; ParseAtExpression's isa is NSConstantString's class object.
  Needed shapes (Pro7's spelling tried first, each on its own): `if (!(x = f()))` plus a chained `return cobjc_type_x = type;` (GetObjCType_*, MakeSelector, ParseSelectorExpression); `return b == NULL;` for `!b` (IsSameMethod, IsSameProtocolList, IsSameMemberList); `i++` in the for-increment (GenerateModule), `rawvar++, ivar = ivar->next`; `(funcarg = funcarg->next) && (funcarg = funcarg->next)` (DefineMethod; CSOM uses the same idiom); AppendArgument reloads `tfunc->args`; `TypePointer* sel` in IsType_SEL (catalogued "another pointer type"); a second index `j` in DefineProtocolListObject; `protocol->object = object = ...` (gc1's); `refs = NULL` before `data[0]` (DefineClassObjects); `flag2 = (flags & COMPLETED) != 0`; ParseClassDeclaration `return` for the first `break`; SelectorCompare's own walker and inverted last test; ParseInstanceVariables' `scan` walker for the duplicate search; MakeSendMsgExpr's `callexpr->...args = arg = lalloc()`, the metaobject ref as an argument and one return; the calltype ternary with `(UInt8)` arms; `CoerceMethodArgs` `static inline` (over the auto-inline limit, 2.6 inlines all four calls); declaration orders (decl-order).
  Fakematch (TODO in the source): GetMethodFuncType's `TYPE(TYPE_POINTER(CObjC_GetObjCType_id(TRUE)))`; ten natural spellings were tried (listed in the comment); gc1 used a cast through SInt32 there. Any cast through another pointer type works (`(Type*)(void*)`, a `TypePointer*` local declared first), and the same cast makes IsType_SEL's `Type* sel` match: one unknown cause behind both (the lookups' inlined result), recorded in the comment.
- Behaviour: every function exact; ObjC diagnostics checked at run time (a file hitting 14 error paths: identical output from the original and the equivalent compiler). Code generation cannot be run: both compilers stop with the same ICE (ObjGen_PPC_EABI.c line 1301; ELF has no ObjC sections). Genuine bugs in the original, kept and commented: GetObjCType_id tests `cobjc_type_class`; IsSameMethod compares argB's type with itself; GenerateModule sets sel_ref_cnt twice (Pro7's too).
- Names: Pro7's throughout (functions, statics, locals). COMMONs renamed to Pro7's, each checked against hash order: selector_hash->cobjc_selhashtable, data_00588064->cobjc_protocols, class_pointer_type->cobjc_type_class, id_type->cobjc_type_id, sel_type->cobjc_type_sel, class_type_entries->cobjc_classdefs, selector_reference_count->cobjc_selrefcount, data_00587f6c->cobjc_classrefcount, objc_string_constant_count->cobjc_stringcount, data_00588507->cobjc_encodemethod. Pro7's placeholder fields named from the ObjC runtime's structs (objc_symtab, objc_method_list, objc_protocol_list, Protocol); DefineMethod's meth30/meth26 are `meth`/`parsed`. No invented names.
- Types: Pro7's objc.h records (new include/compiler/objc.h): ObjCMethod, ObjCMethodArg, ObjCMethodList, ObjCSelector, ObjCProtocol, ObjCProtocolList, ObjCCategory, ObjCInfo, ObjCNamedArg replace CRec, MethRec, HashEntry, SelectorMethod, ObjCParameterNode, MessageArgument, ObjCDefinition, PrecTypeEntry, NamedObjectCacheEntry, ObjCSymbolTable, ObjcModule, IvarEntry, ObjCMethodEntry/List and the old ObjCProtocolList (users in CPrec, CError, CodeGen, CDecl, CParser, types.h converted). Pro7's local raw structs (RawIVar etc.) and ObjCString/ObjCCategoryEntry at 2-byte packing.
- Data: relink byte-identical (.data, .bss). Statics `static`; COMMONs defined here (moved from CDecl.c, where 3 of them were; CDecl did not link before and still does not). No bindings for the statics.
- Header: CObjC.h is Pro7's: the COMMONs other units use (CPrec, CClass) and the public functions, each used elsewhere; `cobjc_encodemethod`'s extern dropped (only CObjC uses it). RelocationList's definition moved to CInit.h (beside main's `typedef RelocationList OLinkList`, Pro7's name); self_name_node's extern moved to CParser.h (its definer).
- Headers relied on: objc.h (new, Pro7's; fine). objects.h: `ObjMemberVar.access` and `ObjMemberVarPath.access` were `UInt8` stand-ins for Pro7's `AccessType` (fixed: neutral everywhere, and ParseInstanceVariables then takes Pro7's `AccessType access`); the other Obj* records' `UInt8 access` not checked. CDecl.h: DeclInfo/MemberDecl fields are invented names (`parserOption` is Pro7's x4A "objc related", `hasParameterNames` x44, `missingTypeSpecifier` x48, MemberDecl is Pro7's BigDeclInfo with `valid`=xCD), ClassLayoutInput is Pro7's ClassLayout, CDecl_DefineClass takes `char` flags, CanCreateObject returns unsigned char (Pro7: Boolean). CScope.h: CScope_GetLocalObject returns Pro7's NameSpaceObjectList* and the object iterator is Pro7's CScopeObjectIterator (both already so in main; the old ScopeSearch stand-in's definition in CSOM.h is gone). CInit.h: RelocationList (Pro7's OLinkList, typedef'd so: obj, somevalue). CMachine.h: CMach_GetFunctionResultClass returns Boolean (Pro7: UInt8). CodeGen.h: the objc offsets returned unsigned int (Pro7: SInt32; fixed in CodeGen.h/CodeGen.c, neutral, which also clears clang's -Wformat on the `%ld`s); CodeGen.c's four objc bodies are 1.x text (parameters `record`, `function`, `p`), exact, for CodeGen's owner. CFunc.h: CFunc_ParseFuncDef's `unsigned char scopeFlag`. types.h: TypeObjCID.x12 placeholder (Pro7's), protocols now ObjCProtocolList*, IS_TYPE_OBJC_CLASS added. objects.h: `Section` typedef and SECT_OBJC_* added (Pro7's values). CExpr2.h now includes CScope.h, CTemplateClass.h, objects.h, types.h instead of CSOM.h. Not reviewed beyond this unit's uses: CABI.h, CClass.h, CError.h, CExpr.h, CompilerTools.h, CPrepTokenizer.h, scopes.h.
- Open: the GetMethodFuncType cast (TODO, fakematch); the header problems above (so also pending headers).
- Adversarial review: attacked the TODOs (GetMethodFuncType's cast: 6 more spellings, plus Pro7's GetObjCType_id/AppendArgument shapes, which cost 8 functions; the cast's cause also explains IsType_SEL), the calltype ternary (7 spellings), DefineMethod's walk, the boundary (rows = Pro7's out-of-line functions in reverse, 8-aligned cuts, COMMON hash order of all 11 names re-checked), header users (every declaration has an outside user). Broke: ParseInstanceVariables' `UInt8 access` (a stand-in field type in objects.h; Pro7's `AccessType` now) and the objc offset helpers' `unsigned int` (Pro7's SInt32). Held: the double cast, the chained returns, `return b == NULL`, the `(UInt8)` ternary, the walker idioms.
- Header round H2 (October 16): resolved: OLinkList, CExpr2.h merged, MemberDecl is BigDeclInfo, ClassLayout, CanCreateObject Boolean, CFunc_ParseFuncDef and CDecl_DefineClass Pro7's, CMach_GetFunctionResultClass Pro7's UInt8; TODO names: DeclInfo, TypeObjCID.x12. Still open: objects.h's ObjBase/ObjNameSpace/ObjType/ObjTypeTag/Object keep `UInt8 access` where Pro7 has AccessType (no entry addresses it).
- Follow-up (October 16, after round H2): objects.h's ObjBase/ObjNameSpace/ObjType/ObjTypeTag/Object have Pro7's `AccessType access` (every function's similarity unchanged): no longer pending.

### src/frontend/CObjCModern.c: SOLID
- Boundary: 0x556d20-0x5570e0, .data 0x5cfed8-0x5cff00, no .bss: Pro7 CObjCModern.c's five functions in reverse order. 2.6's own object (its "super" literal and the assert's "CObjCModern.c" sit after CObjC's jump tables). The old file's comment "Pro7 has none" was wrong and is gone. The row CObjC_SplitNameIntoMessageArguments (invented) is Pro7's `CObjC_GetKeywordArgList`.
- Flags: now `-inline auto,deferred` (rows descend against Pro7's order; with `auto` the relink fails); `-sym off` loses one.
- Functions: 5/5 exact, Pro7's text. One departure: GetKeywordArgList's `p++; if (*p == 0)` (Pro7: `*(++p) == 0`, 78.9), needed shape; the first reviewer's `p[1] == 0` then `p++` replaced by this closer spelling.
- Behaviour: Pro7's; assert line 92 is Pro7's. Bugs of ours fixed: none needed.
- Names: Pro7's. Invented: none.
- Types: Pro7's ObjCNamedArg (objc.h).
- Data: relink byte-identical.
- Header: CObjCModern.h is Pro7's three functions (CExpr and CExpr2 use them); the CObjC functions, HashEntry/NamedObjectCacheEntry/ObjCSymbolTable/ObjcModule stand-ins and the misplaced globals it declared are gone.
- Headers relied on: CObjC.h, objc.h (above); CExpr.h (argumentpromotion's `short qual` is Pro7's), CExpr2.h (above), CompilerTools.h, CParser.h, CPrepTokenizer.h, CError.h (not reviewed further).
- Open: the shared headers' problems listed under CObjC.c.
- Adversarial review: tried Pro7's `*(++p)`, `!*++p`, `start = ++p` and `p++; if (*p == 0)`; only the last two-step forms match, and the closer one is now used. Boundary (Pro7's five functions reversed, .data 8-aligned) and header users re-checked. Held otherwise.
- Header round H2 (October 16): CObjC.h, CObjCModern.h and objc.h are DONE, CExpr2.h is merged into CExpr.h; the objects.h item kept for CObjC.c is not one this unit uses. Resolved.

### src/frontend/CSOM.c: SOLID WITH TODOS
- Boundary: 0x522fc0-0x525eb0, .data 0x5bf6c8-0x5bf91c (SOMIDT_arg1, SOMIDT_type, then literals), .bss 0x5e3098-0x5e30a8 (Pro7's four name statics, reverse declaration order). Rows are Pro7 CSOM.c's out-of-line functions reversed; the row named CSOM_GenerateParentVersion is Pro7's `CSOM_SetupClassCounts` (284 bytes, the counts; GenerateParentVersion, SetNibble, GenerateSlotUsage, Signature, MethodNames, MigrateData and GenerateDLLDFunc are inlined), renamed. Assert lines are Pro7's (CError_FATAL(1048) etc.). The two stubs at 0x525eb0 are FuncLevelAsmPPC's.
- Flags: `-inline auto,deferred` (rows descend; `auto` compiles 35/35 but breaks the relink); `-sym off` loses 8.
- Functions: 35/35 exact. Rewritten from Pro7's text (the old file was gc1 text: `SOMClassBuildState`, `MakeKinds`, `setnumber(int raw, ...)`, `*(SInt8*)ty`, `return (result = 7)`, `CSOM_004e38b0_inline1`, offset macros).
  2.6 changes: the glue stubs are `"__glue_"` (ELF, no leading underscore); CSOM_New negates the call itself, not its temporary (`makemonadicnode(expr, ELOGNOT)`, confirmed in IDA).
  Needed shapes: IsTokenListFunc reads `object->type` directly; FindClassType returns `&stvoid` early; FindAddAncestor reads `gen->ancestors` again (no preloaded `ancestor`); ClassComplete's `(arg = ...->args) && (arg = arg->next)`; GenerateOverrideIntroLists hoisted to function scope, `if ((override = ancestor->overrides))` (sic, gc1's) and `arrayCount > i`; `memclrw(buf = lalloc(size), size)`; ClassAncestors' `int size`; OverrideProcs' `size = count; size *= 4;`; SCIObject's list starts with `relocs = NULL`; FindRTFunc's cases as `if (!cond) break; continue;`; CanUseGlueCall's `gprCounter--`; InitSOMSelf's own `newStmt`; EnvCheck's `checkCall` local; SOMGlueCall's if/else and `char ch`; MethodAccess's `classref`; New hoisted locals; `|=` for flag ORs; NameTranslate with early returns; declaration orders.
  TODO (invented shape): CSOM_SetNibble's body. Pro7's (block-local left/right) is over the 10-statement limit for nested inlining, and 2.6 inlines it inside the inlined GenerateSlotUsage; `buf += offset >> 1; if (offset & 1) ... else ...` with an `SInt32 offset` gives 2.6's code exactly.
- Behaviour: every function exact (statement-by-statement against IDA for New and SCIObject). Genuine bugs in the original: CSOM_New tests the call node where Pro7 tests the temporary.
- Names: Pro7's. Invented (TODO-marked): TypeSig's `argCount`/`flags`/`returnType` (Pro7 x0-x2), SOMAncestor's `isMetaClass`/`isDirectParent` (Pro7 xC/xD), CSOMStub's `kind` (Pro7 x10). EnvCheck's register-numbered expr26-28 are `resultTemp`, `objTemp`, `checkExpr`. COMMON somReferences -> Pro7's `csom_stubs` (hash fits 2009..2021), defined here (it was defined in CPrec.c).
- Types: Pro7's som.h (new: SOMMethodState, SOMReleaseOrder, SOMInfo) and CSOM.h's CSOMStub replace SOMInfoEntry, CSOMRefNode, SOMClassBuildState, SOMClassDescriptor, SOMDescriptorOutput, SOMEntry, SOMSlot, SOMVTable, SOMPragmaNames (CPrec converted). Pro7's local SOM runtime structs (somStaticClassInfo etc.) and SOMGenerator/SOMMethod/SOMAncestor/SOMOverride at 2-byte packing. CSOM_CheckFuncType takes TypeFunc* (CDecl's caller adjusted).
- Data: relink byte-identical. 2.6's statics are `static`; their GC_2_6 bindings (_csom_*, _SOMIDT_*) deleted. 2.0 keeps them global under `#if VERSION < VERSION_GC_2_5` (its lifted functions; 2.0 not checked).
- Header: CSOM.h is Pro7's (CSOMStub, csom_stubs, the 19 public functions, each used by another unit); the internal prototypes, structs, MemberVarAlias_GetBases and the ScopeSearch stand-in (main has Pro7's CScopeObjectIterator in CScope.h) are gone. Units that had CSOM.h only for its transitive includes now include what they used (CExpr2.h, CScope.c, DWARF.c: nothing lost; CScope and DWARF still link; CParser.c now includes CSOM.h itself, or CSOM_Setup became an implicit `int` call and CParser_ReInitRuntimeObjects went inexact).
- Headers relied on: som.h (new), CSOM.h; CScope.h (CScopeObjectIterator, CScope_GetLocalObject's NameSpaceObjectList*: Pro7's); CExpr2.h (stand-in types such as PowerOfTwo, not reviewed); CInit.h (RelocationList/OLinkList); objects.h (ObjMemberVarPath/has_path fine); CMachine.h (CMach_GetFunctionResultClass returns Boolean); CPrep.h (`CPrep_PragmaLex(char)` was a stand-in for Pro7's `Boolean no_err`: fixed in CPrep.h/CPrep.c, neutral, and ReleaseOrder's flag is Pro7's `Boolean` again); CClass.h, CDecl.h, CException.h, CFunc.h, CInt64.h, CMangler.h, CodeGen.h, CompilerTools.h, CPrepTokenizer.h, scopes.h not reviewed further.
- Open: the SetNibble body (TODO), the three invented field-name groups (no source has names), the header problems above (so also pending headers).
- Adversarial review: retried SetNibble (Pro7's, Pro7's with SInt32, index or pointer forms with int/SInt32: 61-99%; held), OverrideProcs' two-step size (5 spellings incl. sizeof: held), CanUseGlueCall's `gprCounter--`, AppendString's `char ch`, ClassAncestors' `int size`, EnvCheck's `checkCall`, InitSOMSelf's `newStmt`, ClassComplete's walk (each Pro7 spelling loses its function: held); checked gc1 for the `(override = ancestor->overrides)` sic (gc1 has it); ReleaseOrder's `char flag` broke: it followed the stand-in `CPrep_PragmaLex(char)`, now Pro7's `Boolean` both sides. Wii 1.0RC1 has no CObjC, CObjCModern or CSOM module and no cobjc/csom publics, so names rest on Pro7.
- Header round H2 (October 16): CExpr2.h's stand-ins (PowerOfTwo) are gone with the merge, OLinkList is Pro7's, som.h/CSOM.h DONE (CSOMStub.kind TODO). Resolved.

### src/backend/TOC.c: SOLID WITH TODOS (two register-inexact functions, does not link)
- Boundary: 29 rows 0x4dd3f0..0x4e0d70 plus inlined statics. They match Wii's TOC module list in reverse (setupaddressing, reset_anchor_varinfo, used_anchor, is_pooled_object, createIndirect, createfloatconstant, DeclarePooledConstants, dumpcodelabels, canoptimizevectorconst, TOC_use_fsel/isel, GetSizeSkip, Optimize64bitMath, expandTOCreferences, createstaticinitobject) and Pro7's toc.c order. Pro7's countindirects and OptimizeNestedAssginments are absent: 2.6's .data has no jump tables for them. Pro7's createNonLazyPointer, referenceIndirectPointer, createvectorconstant, CreatePooledFloatConst, CreateFloatConst and RewriteFloatConst are Mach-O/PIC code with no rows and no data, so they are gone. needdescriptor (Pro7's, dead, no data) is kept unverified. lvslBytes/lvsrBytes come after comparewithzero's float0 in .data, as defined.
- Flags: pro6 -O4,p -inline auto,deferred -sym on. Deferred is right: the rows descend against Pro7's order (createstaticinitobject is lowest, setupaddressing highest).
- Functions: 28/30 exact. Departures from the reference:
  - 2.6 changes, all confirmed by exact code: anchors/pooled data (reset, add_anchor_reference, used_anchor, is_pooled_object, createIndirect); the EABI sections in createfloatconstant, RewriteVectorConst, createcodelabel and createstaticinitobject; paired singles and soft float throughout (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT, the SFPE "hascall" blocks); the comparewithzero cast walk; GetSizeSkip's float/signedness test; the rewrite_preincdec assert; the ETYPCON constant folding; the swapped integer-constant compare; the r1 write in asm; `ignored = expr->ignored = FALSE`; DWARF1_AddToFunctionRefList; Type_RequiresMemoryReturn; the stack intrinsics 311/312; the strcpy size.
  - Needed shapes (natural spelling tried, does not match): common_type's ternary (Pro7's if/else 95.7); DetectCondSideAffect and WeightandSumOps in 2.6's form (Pro7's 88/86); GetSizeSkip's `size` local (Pro7's returns 93); the `(Intrinsics)` casts (Pro7's `& 0xFFFF` 98, none 99.5); TOC_use_isel's `(RegClass)` cast (86.8 without) and its block `Type* t` (95.4 without); expandTOCexpression's `f(tmp = x)` argument forms and its funcref local (97.6-99.9 without); rewritestrcpy's written-out CInt64_SetLong (the call breaks expandTOCexpression's inlining); Optimize64bitMath's GetSizeSkip written out twice (calls or inline helpers 84-85; TODO); createstaticinitobject's declaration order (Pro7's 63).
  - Original's shape: setupaddressing's chained `toclist = descriptorlist = floatconstlist = NULL` (the parenthesised one was not needed).
  - Original's shape (was a fakematch): createfloatconstant now copies `data` into Pro7's own `UInt32*` type (`value = (UInt32*)data`, Pro7's for loop and `(UInt32*)` check): a typed local of another pointer type keeps `data` in its slot, which is what 2.6's reloads show. The `(unsigned long)` round trip and `newlist` are gone.
  - Needed shape: expandTOCreferences' `(RegClass)` cast on the r1 test (99.6 without); the cast on asm_used_register's argument was not needed and is gone (Pro7's spelling).
  - Not exact:
    - canoptimizevectorconst, 82.1 (was 71.4). Now Pro7's per-iteration `l`/`r` rows and Pro7's `IS_TYPE_VECTOR` test (same code as the `(int)stype >= 4` spelling). 2.6 recomputes `lvslBytes + ci * 16` every iteration and keeps l2/l3 in edi/ebp where ours strength-reduces the row pointers. Tried: block or function-level l/r, no casts, SInt8/short/int ci, a while loop, l0..l3 from vec, from `v` or from `p32` (80.2), `&rows[ci][0]`, `rows + ci`, a byte offset, `UInt8*` rows, direct indexing (65), Pro7's direct `vec.sc[]` walks (56.9), declaration order (search).
    - rewrite_opassign, 66.3 (was 60.7). Better with Pro7's promote_type. 2.6 reloads `expr` and `define` from the stack after every store, as if their addresses were taken (`(void)&expr` reproduces expr's reloads), and swaps esi/edi. Tried: Pro7's order and scopes, a declaration-order search.
- Behaviour: every exact function is byte-identical. The two inexact ones were read against IDA statement by statement and are equivalent (no constant, member or callee differs; canoptimizevectorconst's opcodes 353/354/355/248/249 are PC_VSPLTISB/H/W, PC_LVSL/R). Bugs of ours fixed: none. Genuine bugs in the original: none found. Pro7's lvslBytes typo (row 12 `0x00`) is 0x10 in 2.6.
- Names: Pro7's and Wii's names, except:
  - Invented, each with a TODO: reference_anchor, add_anchor_reference, swap_vector_elements, signed_vec (Pro7's invented typedef, now TODO-marked), AnchorEntry, and createIndirect's parameter names. swap_vector_elements' cases are now the STRUCT_VECTOR_* enumerators, not 4..14.
  - Renamed: 0x4e0cc0 `resetTOCvarinfo` -> `reset_anchor_varinfo` (Wii's). Not the same body as Pro7's: 2.6 moved it beside setupaddressing, exactly Wii's slot (Pro7's slot after expandTOCreferences is empty in 2.6), and Wii's code is 2.6's plus an `sdalist` loop. name_review.json updated; callers CodeGen.c and FuncLevelAsmPPC.c follow.
- Types: Pro7's types and members. checkexceptionreferences now uses Pro7's EAT_* cases and ExceptionAction members, not `data.slots[n]`. expandTOCreferences uses Pro7's InlineAsm/IAOperand, not 1.x's ParsedAsmInstruction/EncodedOperand, and takes `Statement**` like Pro7 (callers pass `&statements->next`; same code since `next` is at offset 0).
- Data: .data is lvslBytes, lvsrBytes and float0. The globals are COMMONs named by Wii's publics. Their bindings stay because the unit does not link and lifted code reads them. Statics are static.
- Header: TOC.h is Pro7's API plus 2.6's AnchorEntry, used_anchor and is_pooled_object. No user outside the unit: needdescriptor, Optimize64bitMath and the COMMONs descriptorlist, floatconstpool, doubleconstpool, floatconstlist, vectorconstpool, vectorconstlist, no_descriptors (all Pro7's TOC.h API; left, a header change elsewhere moved code before). Pack is balanced. expandTOCreferences now has Pro7's `Statement**`.
- Headers relied on:
  - Exceptions.h: ExceptionAction members, EAT_*. Its `slots[6]`/`operands[6]` stand-ins are still there for CInline.
  - InlineAsmPPC.h: InlineAsm, IAOperand, IAFlag*, IADirective_FrFree. It has placeholder members (IAOperand `unk*`, IAExpr `xC`/`x10`/`x18`, IAEntryPoint `x0`..), and the 1.x EncodedOperand/ParsedAsmInstruction are still kept for other units.
  - ObjGen_PPC_EABI.h: BE_SymGroup and ObjGenSymbolLink (invented names, TODO), is_16bitdata, GetDataPoolRoot.
  - objects.h: Object, VarInfo. `VarInfo.rclass` is UInt8 (Pro7 too), yet TOC_use_isel needs a `(RegClass)` cast; typing it `RegClass` (cast removed) loses I8_gen_INDIRECT, I8_gen_ASS, gen_INDIRECT, assign_register_by_type and markspecialregisters, so UInt8 stands.
  - common.h: Float, RegClass. ObjRegInfo is an invented name.
  - enode.h, types.h, CInt64.h, CMachine.h, CParser.h, CScope.h, CodeGen.h, CompilerTools.h, CDecl.h, CExpr2.h, CInit.h, CMangler.h, DWARF.h, Intrinsics.h, InstrSelection.h, PCode.h, PPCError.h, Registers.h, RegisterInfo.h, StackFrame.h, BE_symbol.h, COSToolsCLT.h: plain declarations only, nothing wrong seen. Not reviewed in depth.
- Open: the two inexact functions (the unit does not link); Optimize64bitMath's written-out GetSizeSkip (TODO; calls 84.0 again checked, with Pro7's and 2.6's GetSizeSkip shapes); the invented names; the headers above. canoptimizevectorconst now compiles 699 bytes against the original's 667, so `relink --only` differs from the first overflow on (still "links, not to the original").
- Header round H2 (October 16): Exceptions.h's `slots`/`operands` are gone; InlineAsm.h's placeholders are TODO names and EncodedOperand/ParsedAsmInstruction are deleted; ObjGen_PPC_EABI.h's BE_SymGroup/ObjGenSymbolLink and common.h's ObjRegInfo are TODO names. Resolved as TODO names.

### src/backend/Operands.c: NOT SOLID (three fakematch TODOs, two of them a likely stand-in field type; two register-inexact functions, does not link)
- Boundary: 29 rows 0x4e0df0..0x4e3d80. They match Wii's Operands module in reverse and Pro7's operands.c order. 2.6 additions:
  - insert_load_immediate_after, after load_immediate;
  - vectorstore, between Coerce_to_v_register and store; asserts 1084/1089;
  - I8_extract_bitfield and I8_insert_bitfield at the end; asserts 1823-1977, past Pro7's last (1849).

  The row Wii calls extendpair is Pro7's extend64, the same body: renamed back to `extend64` (AGENTS: prefer Pro7's name for the same body; extend32 already kept Pro7's over Wii's extendgpr). The 8 bytes after one_point_zero (`one_point_zero_single`, TODO-named, read by nothing) keep .data's layout.
- Flags: pro6 -O4,p -inline auto,deferred -sym on. Deferred is right: the rows descend against Pro7's order.
- Functions: 27/29 exact. Departures from the reference:
  - 2.6 changes: soft-float asserts and errors (PPCErrorStr132); paired-single loads and stores (psq_l, psq_lx, gecko_load_store_register); TYPE_IS_GPR_PAIR; pooled and small-data addressing in coerce_to_addressable; little-endian bitfields (CABI_ReverseBitField); Float doubles as big-endian word pairs; `LOW_PART` where Pro7 has `LOW_PART_BUGGY`; createfloatconstant called directly.
  - Needed shapes, each with a comment:
    - load_immediate's SInt32 copy: Pro7's `tmpreg2` gives 71, and dropping the copy breaks Coerce_to_register's inlined copy.
    - extend32's SInt32 `tmp` (Pro7's 91.9).
    - `(int)op->reg != 1` (91.2 without).
    - Coerce_to_v_register's `TypeStruct* tstruct`.
    - Coerce_to_register's 16-bit bit-offset arithmetic.
    - The convert_* `const_reg = ...` inside the call (Pro7's own house style; written out it is 98).
  - Original's shape: extract_bitfield's Pro7 operand order (now restored).
  - Fakematch (TODO):
    - load_address's `volatile` read: the plain test, `== 0`, the casts and swapped arms all reach 73-95.7.
    - combine's `(short)(UInt32)` (LOW_PART/short/SInt16 give 99.0).
    - Both are probably one stand-in type: with Pro7's `SInt32 immOffset, immediate` in Operand (InstrSelection.h has `int`), load_address matches as `op->immOffset == 0 && op->object == NULL` and combine with plain LOW_PART, and nothing else in 2.6 changes, except Coerce_to_register, which then stops reloading `op` before its last store (99.9; decl-order with swaps, Pro7's declarations, `int`/no/moved `offset`, Pro7's `opcode` and CRField spellings tried). Kept `int` (nothing may be lost); both TODOs say so.
    - Coerce_to_register's `bit_offset` holding the IndirectGPR_ImmOffset case's opcode (Pro7's `opcode` gives 98.6; one merged variable gives 99.9).
  - Not exact (no reference; read from the disassembly):
    - I8_insert_bitfield, 90.8, registers only. Tried all 128 int/SInt32 combinations of its locals, a declaration-order search with swaps, and the operand order of `loBits + offset - 1`.
    - I8_extract_bitfield, 83.8. Registers, plus 2.6 updates `offset`/`bitlength` in place and tail-merges the signed srawi paths differently. Tried short reg/regHi (worse).
- Behaviour: the exact functions are byte-identical. The I8 pair was checked against IDA's control flow and opcode sequence (rlwinm/rlwimi/srawi/li/mr, the assert lines); I re-checked the emitpcode/push sequences: I8_insert_bitfield's are identical, I8_extract_bitfield's differ only by 2.6 sharing one srawi between paths. Genuine bugs in the original: I8_extract_bitfield's offset >= 32 branch tests `offset == 0` (the `cmp` reads `offset`'s slot), which cannot hold there, so its srawi-only path is dead (commented). Bugs of ours: none found.
- Names: Pro7's and Wii's (extend64 now Pro7's, see Boundary). Pro7's register placeholders `r27`/`r7` (extract/insert_bitfield) are now `start`, and `r28` (extend32) is now `loaded`. Invented: one_point_zero_single (TODO).
- Types: Pro7's Operand (except `int immOffset, immediate` where Pro7 has SInt32: see the fakematches) and TypeBitfield. `short opcode` stands in for Pro7's `Opcode`: 2.6's opcodes are `#define`s.
- Data: .data is one_point_zero, the unread 8 bytes, int_to_float_cc and uns_to_float_cc, in 2.6's order, with the word-pair contents ObjGen_Setup swaps. The bindings stay while the unit does not link. Statics are static.
- Header: Operands.h declares only functions other units call; Pro7's static set_op_flags is public in Wii, and InstrSelection uses it. one_point_zero_single is not declared (nothing uses it). No pack.
- Headers relied on:
  - PCodeUtilities.h: add_immediate and friends take `short`, which 2.6 confirms by pushing words.
  - CPrep.h: `copts` is declared there (Pro7 has it in CParser.h). The include was moved from the end of the list into its sorted place.
  - common.h: FloatStorage words, Float, LOW_PART/HIGH_PART.
  - InstrSelection.h: Operand. `immOffset`/`immediate` are `int` where Pro7 (and AGENTS.md's Operands note) has SInt32; SInt32 removes two Operands fakematches and costs only Coerce_to_register: open for the header's owner.
  - The same back-end headers as TOC.c (objects.h `VarInfo.rclass` UInt8; ObjGen_PPC_EABI.h's invented names).
- Open: the two I8 functions (the unit does not link); the three fakematch TODOs (two tied to Operand's integer type); one_point_zero_single's owner and name.
- Adversarial review: attacked the fakematches, names, the boundary and the I8 readings. Broke: createfloatconstant's `(Float*)(unsigned long)` fakematch (Pro7's `UInt32*` typing matches: the lesson "a typed local of another pointer type keeps the parameter in memory"); an unneeded `(RegClass)` cast (expandTOCreferences); magic numbers 4..14 for STRUCT_VECTOR_* (swap_vector_elements, canoptimizevectorconst, now Pro7's IS_TYPE_VECTOR); two names (extendpair -> Pro7's extend64 for the same body; resetTOCvarinfo -> Wii's reset_anchor_varinfo, moved and rewritten in 2.6); found Operand's `int immOffset/immediate` (Pro7: SInt32) behind two Operands fakematches, not switchable without losing Coerce_to_register; found 2.6's dead `offset == 0` test in I8_extract_bitfield. Held: the boundary (Wii's TOC and Operands modules in reverse, Pro7's order), the flags, the data (one_point_zero's 8 unread bytes have no Wii counterpart), VarInfo.rclass as UInt8 (RegClass loses 5 functions elsewhere), the remaining needed shapes, both inexact TOC functions' equivalence. rewrite_opassign's reloads of `expr` after every store also appear in Wii's compile of it: a compiler behaviour still unexplained. Exact count unchanged (4020), relink as before.

### src/backend/BE_elf.c: SOLID WITH TODOS
- Boundary: 21 rows 0x4db750-0x4dcdc0 = Wii's BE_elf module (15 publics by name, 4 unnamed statics by order and size; Wii's own extra static 0x568960 is the section-contents loop 2.6 inlines into BackEnd_WriteElf, now `WriteSectionData`). The OPEN row 0x4dc350 is DoParseDeclSpec (Wii's has the same section walk, strcmp and three PPCError reports; Wii's elf_define_section, galloc(0x10) + DoElfContentSection, is ObjGen's work in 2.6): recorded in name_review.json. Wii's elf_insert_*/elf_append_*/AppendSection* are absent from 2.6 and leave no data. Asserts all "BE_elf.c" (.data 0x5ba948: padding_data, max_padding_size, "BE_elf.c", ".text", ".rela", one jump table). .bss 0x5e0b20-0x5e0bb0 is the four statics in reverse declaration order.
- Flags: pro53 -O4,p `-inline auto,deferred -sym on`: rows descend against Wii's (also deferred) order; whole-file `-sym off` loses 6 exact functions (re-run: BackPatchRelocationAddend, write_codewarrior_version_record, WriteSsymtab, build_ordered_section_index, CloneSection, Initialize_Input).
- Functions: 19/21 exact. Inexact: BackEnd_WriteElf 99.5 (one register: `section_list` temp in esi vs edi; tried decl-order --swaps, direct symtab stores, loop/decl/scope variants, Wii's helper split), InitElfFile 91.7 (the 257-byte filename buffer sits at esp+7, ours at esp+4; tried struct/len+name struct/pack(1)/align=packed/decl order/inline p2cstr helper). Departures: `#pragma sym off` on BackEnd_WriteElf (fakematch-grade TODO: 86.7 and 8 bytes short with symbols on; is_big_endian/WriteSectionData written in place 82.7-96.0; per-function sym off on InitElfFile does nothing); find_section_symbol_attributes is now a `continue` per field (original's shape: over the auto-inline limit, so it stays out of line as in 2.6 and Wii; the `dont_inline` pragma is gone, the `&&` spellings all inline); `(section = InitElfSection(...))->context` (needed, original's chained shape); Initialize_Specific's `last` walker copy (needed: plain `tail` walk 99.3); WriteSstrtab's `list` temp (needed: 78.8 without); `ElfHeader.e_shoff = offset = ...` (needed); U16Bytes union byte copy (the original copies the converted halfword byte by byte through a 2-byte stack temp: original's shape). Removed as not needed: invented `FindSectionByName` (GetSectionByName with `!name || !*name` matches both it and build_ordered_section_index), invented `MakeSectionRec` (it was Initialize_Input inlined, whose parameters are `UInt8`, not `unsigned int`: Wii inlines it too), `(UInt8)(kind - 1) <= 2` (three `==` tests), `(UInt32)` casts in SectionAlign (`int size`), InitElfSection's `size` local and shadowed `head`, DoParseDeclSpec's `head`, `15U`/`0U` literals, `ElfIsBigEndian` (now Wii's name `is_big_endian`).
- Behaviour: all exact functions byte-identical with references; the two inexact ones are register/frame only. Genuine bugs in the original: none found (BackPatchRelocationAddend keeps code after CError_FATAL(1025), as 2.6). Ours fixed: none functional; IndexedEntry stand-in replaced by Elf32_Rela.r_addend.
- Names: Wii's for all publics (ElfHeader, SpecificNum, ELFfile, elfmodule, allrelocations are Wii data publics referenced only by BE_elf code: this unit's COMMONs); invented (TODO-marked): assign_symbol_order, find_section_symbol_attributes, build_ordered_section_index, write_codewarrior_version_record, WriteSectionData, the statics elfBigEndian/symbol_order_count/padding_buffer/ordered_section_index/padding_data/max_padding_size (unnamed in Wii), ElfFile's type and member names, write_codewarrior_version_record's local record's field names (not TODO-marked). ElfFile's `unused48` holds: no 2.6 xref at ELFfile+0x48, and Wii's ELFfile has the same unreferenced word after its abs section (+0x6a). WriteSstrtab/WriteSsymtab are Wii publics (kept extern).
- Types: SectionRec's `far_reloc`/`near_reloc` are now `dataMode`/`codeMode` (the `#pragma section` data_mode/code_mode they hold, as SectionSymbolAttributes names them; every use in CodeGen.c/ObjGen_PPC_EABI.c checked: 1/2/5 data modes, 0xa-0xc code modes), and Initialize_Specific's/find's parameters `kind`/`alignment` were misnamed (they are the two AddressingModes): renamed likewise (UInt8 kept; AddressingMode not tried as a field type); section kinds now ObjGen's SectionKind_* enumerators, PPCError ids PPCErrorStrNNN, InterruptRecord_DeclSpec; Initialize_Input takes UInt8 (was unsigned int); SectionRec comments corrected (16 bytes, offsets were wrong). ElfFile has a placeholder `unused48` (nothing in 2.6 touches offset 0x48; checked xrefs). The record struct in write_codewarrior_version_record is local (field `languageVersion` renamed `processor`: it holds copts.processor).
- Data: .data/.bss match by placement of the exact functions; statics stay global (bound) while BackEnd_WriteElf and InitElfFile are lifted. copts and cparamblkptr now defined where Pro7 defines them (CParser.c after Xunex_func, CPrep.c; CParser links, all 218 units still relink). previous_ps_ID and interrupt_generation_records stay here (TODO: 2.6-only, ObjGen's ELF_Setup resets both; neither is in COMMON hash order at its address: previous_ps_ID 1318 between 1198/1208, interrupt_generation_records 2801 between 3651/3663; Wii's interrupt_routine_flags fails the same check). Relink fails only on the statics left COMMON.
- Header: BE_elf.h now declares only what other units use (removed ElfHeader, SpecificNum, the static externs, WriteSstrtab, WriteSsymtab, write_codewarrior_version_record, build_ordered_section_index, find_section_symbol_attributes, assign_symbol_order, IndexedEntry, U16Bytes; U16Bytes moved to the .c; IndexedEntry/U16Bytes typedefs dropped from common.h).
- Headers relied on: ObjGen_PPC_EABI.h (ObjGenSection, SectionSymbolAttributes, SectionKind_*, InterruptGenerationRecord; invented enum names TODO; no SectionKind for kind 0; ObjGenSection's nameoff..entrySize are an Elf32_Shdr written as loose fields, cast `(Elf32_Shdr*)&s->nameoff`); BE_symbol.h (BE_SymNode, TheSymbolTable, First/Next/InorderFirst; invented names TODO); CDecl.h (DeclInfo: placeholders f38, f3d, x54..x58, pad66; `extraQualifiers` is Pro7's `section`, widened to SInt16 in 2.6); ELF_Endian.h (Elf32_*; no SHT_/STT_ constants, so numbers are commented); COSToolsCLT.h, CompilerTools.h, CError.h, PPCError.h, CodeGen.h (reg_info_list), DWARF.h (tweakDWARF_file), CPrep.h (cparamblkptr). CDecl.c and LoopOptimization.c include BE_elf.h without visible use (not checked further). Grade: the unit's own TODOs (sym off, invented names, two COMMONs) plus these headers' placeholders and invented names.
- Open: BackEnd_WriteElf 99.5 and InitElfFile 91.7 (so the unit does not link; InitElfFile's 257-byte buffer is placed at the frame's top, esp+7..0x108, ours at its bottom: 16 more declarations/sizes/scopes tried); the per-function `sym off` pragma; invented names; previous_ps_ID/interrupt_generation_records' home and names.
- Adversarial review: attacked the pragmas, the union and list temps, parameter and field names, the COMMON definitions and hash order, out-of-line copies. Broke: `dont_inline` (a `continue` chain matches without it), the `kind`/`alignment` and `far_reloc`/`near_reloc` names (they are data/code modes), copts/cparamblkptr defined here (moved to Pro7's units, relink holds), interrupt_generation_records/previous_ps_ID hash order. Held: `sym off` (no natural spelling within 4 tries), U16Bytes (pointer-cast 96.2, memcpy 93.1, direct 64.9), WriteSstrtab's `list` (plain 78.8, hoisted 82.4), the `unused48` placeholder, InitElfFile's frame (unexplained).
- Header round H2 (October 16): ObjGenSection holds an `Elf32_Shdr shdr`; ELF_Endian.h has the SHT_ constants (the unit still writes numbers); ObjGen_PPC_EABI.h, BE_symbol.h and BE_elf.h keep TODO names; DeclInfo's placeholders are gone (TODO names). Resolved as TODO names; previous_ps_ID/interrupt_generation_records' home is the unit's.

### src/optimizer/FunctionCalls.c: SOLID WITH TODOS
- Boundary: 5 rows 0x528290-0x529b00 = Wii's FunctionCalls module (5 functions, call_function public); Pro7's file order reversed (deferred); Pro7's helpers needs_TOC_reload, load_virtual_function, branch_subroutine_indirect(_ctr), pass_in_register_and_memory, make_arginfo inlined (no out-of-line copies in 2.6's range; ours emits copies, which have no data and are not rows). .data "FunctionCalls.c"; .bss 0x5e30e0 is call_function's zeroed `used_regs = {0}` template (20 bytes; the range's last 12 bytes are unreferenced padding before IroMalloc).
- Flags: pro6 -O4,p `-inline auto,deferred -sym on`: rows descend.
- Functions: 4/5 exact. analyze_arguments 92.5: register-only (`used_regs` and `type` swap esi/ebx; same size and instructions, statement-by-statement equal to IDA); tried decl-order --swaps, Pro7's local order (81.1), block-scoped spilledVectorFlag, array parameter, while-loop clear. Departures: 2.6 changes (EABI odd register pairs, soft float in GPRs, f1-f8, Gekko paired singles, small-struct/soft-float returns, CR bit 6 for varargs, `ObjGen_GetAddressing == 12` calls through CTR, Type_RequiresMemoryReturn, FATAL(398) before Pro7's aggregate branch: all in the disassembly). Needed shapes: `(int)addrOpnd->reg != 12` (catalogued; plain 98.3/99.4), call_function's `tclass`/`vfOffset` at function scope (block scope 97.6). Soft-float tests now TYPE_IS_SOFT_FLOAT / IS_TYPE_FLOAT_NOT_VEC2X32FLOAT (identical code; the result test was a redundant double condition).
- Behaviour: elipsis/oldstyle checked against IDA (0x583098 is elipsis). No bugs found.
- Names: Pro7's throughout; nothing invented.
- Types: Pro7's ArgInfo (pack(2) for Pro7's mac68k alignment); booleans now TRUE/FALSE. make_arginfo is Pro7's plain `static` again (the `inline` changed nothing).
- Data: as above; no statics.
- Header: FunctionCalls.h declares call_function only (InstrSelection, Intrinsics use it).
- Headers relied on: InstrSelection.h (GEN_NODE macros; a parameter named `unk`), types.h (type macros; a placeholder `x12`), Operands.h (Operand: Pro7's), StackFrame.h (set_out_param_displ, low/high_offset), CParser.h (elipsis, oldstyle, CParser_IsVirtualFunction), CMachine.h, ObjGen_PPC_EABI.h, PCode*.h, Registers.h, StructMoves.h, CodeGen.h. Not every one reviewed.
- Open: analyze_arguments register swap (so the unit does not link; also tried block-scoped/late `type`, a while clear loop, int/short/char counters: 59-92.5); relink also reports .bss 20 against a 32-byte range (12 unreferenced bytes before IroMalloc: unexplained); headers above (InstrSelection.h's Operand.immOffset/immediate are `int`, where AGENTS.md records Pro7's `SInt32` as needed by Operands' combine).
- Adversarial review: re-diffed against Pro7 (every departure a 2.6 change, a brace/format, or the `info` declaration order), retried analyze_arguments' swap (8 variants, none closer), checked the out-of-line helper copies against 2.6's range and data. Broke: make_arginfo's needless `inline`. Held: the rest.
- Header round H2 (October 16): InstrSelection.h's `unk` is `for_division`, Operand's immOffset/immediate are SInt32 (Operands.h); types.h's `x12` is a TODO. Resolved as TODO names.

### src/optimizer/ConstantPropagation.c: SOLID WITH TODOS
- Boundary: 5 rows 0x575c20-0x5770e0; Wii's module has the same file with one more static; Pro7's file reversed (deferred). Pro7's statics plus 2.6's `fprdefininginstruction` in .bss 0x5e3bb0 (reverse declaration order: changed highest). .data is only one jump table at 0x5d19ac (the switch of issingleoperand inlined into propagateconstantstoblock; the main switch compiles to compares) (4-aligned: no literals; "LoopOptimization.c" at 0x5d1998 is LoopOptimization's); cut plausible but unproven until it links.
- Flags: pro6 -O4,p `-inline auto,deferred -sym on`: rows descend.
- Functions: 4/5 exact. propagateconstantstoblock 98.9: one register pair (edi/ebp) in the RLWINM mask computation; tried Pro7's `mask2` (98.0, in any position, `int` or `UInt32`). Departures: issingleoperand is `static inline` (2.6 change, proven by data: 2.6's only .data is that inlined switch's 272-byte table at 0x5d19ac, the `sub eax,0x8e; cmp eax,0x43; jmp [table]` at +0x708; a plain static's out-of-line copy added a second 272-byte table, which would have kept the unit from ever linking; the other helpers' copies have no data); 2.6 changes (FPR defining table, volatile instructions skipped, `frsp` of single-precision to `fmr`, ADDI with non-immediate skipped, `addi rD,rA,0` to `mr`); needed shapes: `for (instr = NULL, list = ...)` (Pro7's split 98.4), `changed = propagatedconstants = TRUE` (the disassembly reloads propagatedconstants), `immAddend = value1 = 0` (split 98.8), `SInt32 i` in propagateconstants (`int` 95.8), `loadSize` reused for Pro7's `mask2` (TODO, as found).
- Behaviour: register-only difference; no bugs found.
- Names: Pro7's; invented (TODO-marked): fprdefininginstruction, issingleoperand. Statics `#define`d to `constprop_*` while lifted (binding names are global; Peephole has defininginstruction too).
- Types: `short newOpcode`/`short* resultNewOp` for Pro7's `Opcode` (the repo has no such typedef); booleans now TRUE/FALSE.
- Data: statics global and bound while propagateconstantstoblock is lifted (relink reports them COMMON).
- Header: ConstantPropagation.h: propagatedconstants (GlobalOptimizer) and propagateconstants; LoopOptimization.c includes it apparently without use.
- Headers relied on: UseDefChains.h (Pro7's, reviewed October 15), PCode.h/PCodeInfo.h (opcodes, flags; no Pro7 `Opcode` type, so `short` stands in; Pro7's placeholder flag names fPCodeFlag20000/40000), BitVectors.h, StackFrame.h (can_add_displ_to_local), Registers.h, RegisterInfo.h, Alias.h (included as Pro7 does).
- Open: propagateconstantstoblock 98.9 (unit does not link; mask2 as a separate UInt32 in five positions 98.0, `i` reused 89.4); invented names; the `loadSize` reuse TODO (judgement: fakematch-grade until a better story, the only evidence is the register pair).
- Adversarial review: checked every Pro7 departure, the statics' order and hash (propagatedconstants 3642 between 3639/3651), the helpers' out-of-line copies against the data, retried the mask2 shape. Broke: issingleoperand's extra jump table (fixed with `inline`). Held: the chained assignments, `SInt32 i`, the deferred flags.
- Header round H2 (October 16): PCode.h's opcodes are Pro7's `Opcode` enum and fPCodeFlag20000/40000 are fOpFromMemory/fOpToMemory (TODO); Alias.h's AliasType names are TODO. Resolved as TODO names.

### src/optimizer/IroPropagate.c: SOLID WITH TODOS
- Boundary: .text 0x46fc40-0x4712a0, .data 0x5ac538-0x5ac624 (8-aligned), no .bss. The rows run in reverse of Pro7's IroPropagate.c (VarIsUsedInOtherFNodes is highest, IRO_ExpressionPropagation lowest), and Wii puts every named row in module IroPropagate (IRO_IsRegable's pair is in IroVars: a later move). All of Pro7's functions are present. CheckUnorderedRegionForDefs, IsIntGenKillCandidate and IsPropagatableExpr are inlined. ClearAvailibilityOnNode is dead and leaves no data. The .data is the two functions' literals, "BitVector.h" from an inlined Bv_SetBit assert, "IroPropagate.c" (CError_FATAL 768) and CheckUnorderedRegionsForDefs' 9-entry jump table; the relink differs only inside IRO_CopyAndConstantPropagation's .text.
- Flags: pro6 `-O4,p -inline auto,deferred -sym on`. Deferred because the rows descend in address against Pro7's order. `-sym off` drops the unit to 6/9 exact (8/9 with on).
- Functions: 8/9 exact. Rewritten from Pro7's text. Departures:
  - IRO_IsRegable `return !...->noregister;` (Pro7 `== 0`): needed shape; `== 0`, `!= NULL`, a ternary, if/return and `&&` were tried and do not match. It also made the inlined copy in IsPropagatable exact.
  - IsPropagatable calls `Inline_IsObjectData`, whose 2.6 body also accepts DABSOLUTE: 2.6 change (the code compares both; see objects.h below).
  - PropagationHasDefsInUnorderedRegions keeps a separate walker `linear = a` and declares `flag` after `bv2`: needed shape (the original copies `a` with `mov esi, ebx`; Pro7's reuse of `a` reads 82.2-88.9).
  - IRO_ExpressionPropagation declares `ass` before `var`: needed (register order).
  - IRO_CopyAndConstantPropagation (99.7, not exact) declares `newass` at function scope in 2.6's order. Pro7's order reads 90.6 and a block-local `newass` 97.5. What remains is one reload: 2.6 reloads the spilled `linear` after `linear->type = IROLinearOperand`. Tried: the stores in other orders, a byte-typed store, `enode` at function scope or declared apart, a decl-order search. Equivalent; TODO in the source.
  - No fakematches.
- Behaviour: every function is Pro7's statement for statement apart from the DABSOLUTE test. An equivalent compiler built with this source compiles a loop and copy-propagation test identically to the original at -O2, -O3, -O4, -O4,p and -O4,s. Bugs of ours fixed: the generated global `propagation_dump_message` was a stand-in for the literal "Found propagation at %d from %d\n"; it is now a literal at its use and its binding is gone. Bugs in the original: none found.
- Names: all rows Pro7's. COMMONs IRO_FirstAssign, IRO_LastAssign and IRO_NumAssign fit hash order (2293, 1265, 50 between their neighbours). Invented: none in the unit; IROAssign.useCount is the header's TODO, below.
- Types: Pro7's. The `(TypeBitfield*)` cast on VarRecord.bitfieldType follows the repo's IRO_TruncateBitfieldValueToType prototype (Pro7 has `Type*` for both).
- Data: identical in the relink. No statics. `TRUE`/`FALSE` are used throughout, including IRO_FindVar's Boolean arguments.
- Header: IroPropagate.h declares IROAssign (used by IroDump and IroFlowgraph), IRO_FirstAssign and IRO_LastAssign (IrOptimizer, IroDump, IroFlowgraph; the extern of IRO_NumAssign, which no other unit reads, is removed), IRO_IsRegable (IroSubable, IroLoop) and the two passes (IrOptimizer). It uses `pack(push, 2)` for Pro7's mac68k alignment. IROAssign.useCount is an invented name with a TODO (Pro7: x20).
- Headers relied on: IroLinearForm.h (IROLinear, IROList and the walkers; `flags` is UInt32 where Pro7 has SInt32, with a comment; the `funccall` x4/x8 placeholders), IroFlowgraph.h (IRONode; placeholder `x26`, invented `copyOut`, `reachable` and the rest with TODOs), IroVars.h (VarRecord; placeholder `x6`, invented `bitfieldType` typed `Type*` while callers cast to TypeBitfield*), IROUseDef.h (IROUse.reachingDefs is invented), IroCSE.h (IRO_FindDepends, IRO_Depends), IroEval.h (IRO_TruncateValueToType, IRO_TruncateBitfieldValueToType), IroUtil.h, IroDump.h, BitVector.h, InlineAsmPPC.h (IAEffects; IAEffect_0..4 and IAOpnd_3/4 are Pro7 placeholder names, and some members are named `unk`), objects.h (Inline_IsObjectData now has 2.6's body, below), CExpr2.h (create_objectrefnode), CParser.h (is_volatile_object), CompilerTools.h (oalloc), CError.h, enode.h, types.h. Also CInt64.h, reached transitively: its `extern "C" {` is closed at once by a nested `#ifdef __cplusplus }`, so its declarations sit outside the linkage block. The include of Registers.h was unused and is removed.
- Open: IRO_CopyAndConstantPropagation 99.7 (one spill reload), which keeps the unit from linking. Header placeholders as listed.
- Adversarial review: attacked IRO_IsRegable's `!` (`== 0`, `== FALSE`: 57.1, and IsPropagatable and IRO_ExpressionPropagation lose their match: held), the IRO_CopyAndConstantPropagation reload (comma expressions, a block-local walker copy: no change, held as a register-only TODO; the diff is that one reload and the alignment padding after it), the invented helper name (broke: `Inline_IsObjectDataOrAbsolute` was 2.6's `Inline_IsObjectData`: every Pro7 call of it, in IroVars, IROUseDef, IroPropagate and IroPointerAnalysis, compares DDATA and DABSOLUTE in 2.6, and nothing in 2.6 uses a DDATA-only test, so objects.h now gives Inline_IsObjectData 2.6's body and the invented name is gone; no function anywhere changed), the header (broke: IRO_NumAssign's extern had no user outside the unit; removed), the COMMONs' hash order and the VarRecord cast (held: the cast follows IRO_TruncateBitfieldValueToType's 2.6 `TypeBitfield*` parameter while `bitfieldType` is assigned from `rtype`). Grade unchanged: SOLID PENDING HEADERS.
- Header round H2 (October 16): IroLinearForm/IroFlowgraph/IroVars/IROUseDef keep TODO names (`bitfieldType` is TypeBitfield*); InlineAsm.h's IAEffect_*/IAOpnd_* are named (TODO); CInt64.h's linkage block closes at the end. Resolved as TODO names.

### src/optimizer/IroLoop.c: SOLID WITH TODOS
- Boundary: .text 0x4a4580-0x4a8e50, .data 0x5afd10-0x5b0080, .bss 0x5e0338-0x5e035c (8-aligned). The rows are Pro7's IroLoop.c reversed: FindMustReach is highest, IsAssignmentReductionCandidate lowest. Two 2.6 additions:
  - row 0x4a8990, a tree-walk visitor between IsSafeTypcon and Reducable (Wii: an unnamed static of IroStrengthReduction);
  - its inlined wrapper, used by Reducable and CreateRIS.

  Pro7's inlined or dead helpers are present: IncLoopDepth, RemoveNoopsFromExprList, CheckSubableSub, NoSubableSubExprs, UnmarkSubexpressionsOfInvariantExpressions, ComputeIntWeight, IsTypconVar. `NoSubableSubs` holds 4 bytes of 2.6's .bss, which confirms the dead pair. The relink differs only in the two inexact functions' .text. Its "defines _Bv_SetBit" note is the inline's out-of-line copy; the original's AddPreds also calls Bv_SetBit out of line.
- Flags: pro6 `-O4,p -inline auto,deferred -sym on`. Deferred because the rows descend. `-sym off` drops the unit to 20/33 exact.
- Functions: 31/33 exact, with every function rewritten from Pro7's text. Plain Pro7 text matched 18. Departures:

  2.6 changes, each confirmed by a call, constant or branch in the disassembly:
  - MoveInvarianceInAddressExpr ends with ComputeLoopInvariance().
  - ReorderOperandsForReductionPattern moves the operands as copies (Duplicate, NopOut, Paste; under `#if VERSION >= VERSION_GC_2_5`), and re-reads `b->u.diadic.left` and `right` after the swap.
  - MyHandleLoop_Vector has a `break` after reusing an already-removed expression.
  - CheckAllLoopAddresses: a non-intrinsic call (FUNC_INTRINSIC) or an asm statement clears the flag.
  - Reducable looks the induction record up for every candidate. It requires `addConst % val == 0`, and refuses an add that the induction update uses as a value (the new tree walk).
  - CreateRIS pastes the update after the induction node in the case Reducable allows.
  - ExtractLoopInfo calls IRO_RenumberInts() first. Its "Induction Used in loop" test is `!Assigned || Used` (Pro7 `&&`). It reads the IfNot condition as `u.label.cond` (Pro7 `u.diadic.right`, the same offset).

  Needed shapes, each natural spelling tried first:
  - declaration orders: MoveInvarianceInAddressExpr, CreateNewLoopExitSuccessor, InsertBranchAroundLoopPreheaders, MyHandleLoop_Motion; CreateRIS with Pro7's variables, by a decl-order search;
  - the chained `IRO_EndNode = IRO_LastNode = fnode`;
  - inverted if/else for `node21`/`node22` (MyHandleLoop_Motion/Vector);
  - an early `return` in FindAssignmenttoInductionVar (separate epilogue);
  - `(flags & IROLF_Reffed) == 0` in CheckLoopAddress (`!` does not match);
  - Reducable: a block-local `addConst` copy, `IRO_IsConstant(nd->u.diadic.right)` read again, and `val64.hi` (no Pro7 CTool_EndianReadWord32, Ninji's host-endian helper).

  Original's shape: CheckAllLoopAddresses tests `nd->type == IROLinearFunccall` twice, as the binary does. A single test, or an inline helper, does not match; probably a macro.

  Fakematch (TODO): Reducable's ESHL check stores `(div = ...intval.hi)` so that the high word is tested in a register. Tried: the plain field, `!= 0`, casts, `*(UInt32*)&`, CTool_EndianReadWord32, and the ENode in `enode`.

  Inexact:
  - RearrangeInvarianceInAddressExpr 99.5: one instruction-order difference. Pro7's statement order matches that point but swaps the second loop's registers (94.2 over every declaration order, loop-header spelling and split variable tried).
  - ExtractLoopInfo 93.1: registers and one stack slot. Pro7's text plus 2.6's changes reads 65.6. The earlier pass's split variables (`ind`, `right2`, `scannd2`, reused `right18`) and a dead `hi` store (TODO; 92.7 without it) are kept as the closest known. decl-order finds nothing better.
- Behaviour: as above. The equivalent compiler built with this unit compiles the loop test identically to the original (-O3, -O4, -O4,p, -O4,s). Bugs in the original: none found. Bugs of ours: none in exact code.
- Names: Pro7's for every row and global. Wii's names (IRO_IsValuePreservingTypcon, IRO_MoveInvariantsInAddressExpr, ...) are later renames in split modules. The COMMONs fit hash order: LoopNode, ConditionalHeaderAtBottom, FirstInd, InLoop, InLoop_Exits, InLoop_Tails, LoopExitNumber, LoopTailNum, LoopExitSuccessor, LoopTail, IRO_LoopMemRefFirst and IRO_LoopMemRefCurrent. The last eight were referenced but no source defined them; they are now defined here as Pro7 does. IRO_InitLList is unreferenced in 2.6 (Pro7's IRO_BuildList is gone) and left out. Invented, with TODOs: IRO_ActFindSearchTarget, TreeContains, iro_loop_search_found and iro_loop_search_target (2.6 additions; Wii has no name).
- Types: Pro7's. IROLoopMemRef moved to the file (only this unit uses it) inside `pack(push, 2)`. Its flags are a byte enum, so the packing places the pointers: without it, 3 functions lose their match.
- Data: .data and .bss are identical in the relink. All statics are now `static` in Pro7's declaration order, with 2.6's two after `NoSubableSubs`. `#pragma pack(2)` at the end of the file (TODO, like CABI and CError) gives 2.6's 2-byte .bss layout. Without it the unit's .bss sits 2 bytes low and NoSubableSubs is misplaced. 9 bindings for these statics are removed (every one is reached by an exact function); the equivalent compiler still builds.
- Header: IroLoop.h keeps what IroEmptyLoop, IroUnrollLoop and IrOptimizer use: the IROLoop, IROLoopInd and flag types, LoopNode, ConditionalHeaderAtBottom, FirstInd, InLoop, and FindMustReach, AddPreds, IRO_SetLoopDepth, IRO_FindLoops, ComputeLoop*, FindAssignmenttoInductionVar, ExtractLoopInfo and BuildLabel. The externs nobody else uses are removed (InLoop_Exits, InLoop_Tails, LoopExitNumber, LoopTailNum, LoopExitSuccessor, LoopTail, the two memref lists, FindMustReach1), and IROLoopMemRef moved to the .c. IROLoop has placeholder `x8` and invented members, all with TODOs.
- Headers relied on: the same Iro headers as IroPropagate (IroLinearForm, IroFlowgraph, IroVars, IroCSE, IroUtil, IroDump, BitVector; same problems), plus:
  - IroSubable.h;
  - IroPropagate.h (IRO_IsRegable);
  - CFunc.h (create_temp_object; `LoopInfo` has placeholder x0/x4/x8);
  - CInt64.h (malformed extern "C", above);
  - CMachine.h (stunsignedlong);
  - CParser.h (copts, is_unsigned);
  - Switch.h (SwitchInfo, SwitchCase);
  - CPrep.h, objects.h, types.h, CompilerTools.h, enode.h, CError.h.

  The include of CodeGen.h was unused and is removed.
- Open:
  - RearrangeInvarianceInAddressExpr 99.5 and ExtractLoopInfo 93.1 (register/order only), which keep the unit from linking.
  - The `div` fakematch in Reducable.
  - The invented names of 2.6's tree-walk helper.
  - Header placeholders.
  - Other versions' bindings.json still bind the now-static names (only GC_2_6 was updated).
- Adversarial review: re-checked the boundary (the IDA strings at 0x5afd10.. are ExtractLoopInfo's, the rows descend against Pro7's order), the eight COMMONs defined here against mwld hash order (all in order between real neighbours), the .bss size (36 bytes needs the 2-byte packing: natural alignment gives 40) and ExtractLoopInfo against IDA statement by statement (the `if (ind23)` test IDA drops is in the bytes; the `||` of "Induction Used in loop", the IRO_RenumberInts call, the unsigned `> 0` step test all hold; the remaining difference is an esi/edi swap from the induction-step block on and flag28's stack slot). Pro7's text with 2.6's changes reads 65.5 (confirmed); merging the earlier pass's split variables back (`ind` into `scanind`, `scannd2` into `scannd`, `right2` into `right`, every combination) reads 87.9-92.3, so the splits stay as the closest known, TODO. Fakematch hunt: Reducable's `div = ...hi` (plain field, `!= 0`, `(UInt32)`, `!(== 0)`: 99.8; Pro7's CTool_EndianReadWord32 cast: 99.3; `val` assigned in the condition: 95.6) and its block-local `addConst` (Pro7's `ind->addConst`: 99.3) and reread `nd->u.diadic.right` (`right`: 99.5) hold; CheckAllLoopAddresses' double funccall test matches IDA's bytes (two `cmp dl, 0Bh`). ReorderOperandsForReductionPattern's 2.5 block matches IDA's pseudocode. Wii's CodeView types (checked: 1.0RC1's global types hold only the driver's and SDK's structs) cannot name IROLoop's placeholder members. Nothing broke in this unit; the header fix (Inline_IsObjectData) is listed under IroPropagate. Grade unchanged: SOLID WITH TODOS.

### src/backend/SpillCode.c: SOLID WITH TODOS
- Boundary: 8 rows 0x57c390-0x57d150, Pro7's SpillCode.c in reverse (insertspillcode lowest, estimatespillcosts highest); makespilltemporary and rematerialize_spilled_register are inlined (no rows), nothing missing. Asserts say "SpillCode.c" (.data 0x5d7ce0). .bss 0x5e4150-0x5e4158 is the three statics. Data cut fixed (adversarial review): the unit's .data was 0x5d7ce0-0x5d7e60 (384 bytes) though it owns 124 (name + 7 jump tables, to 0x5d7d5c); the 256 bytes 8-aligned at 0x5d7d60 are Pro7's BitVectors.c `bitvectorcount` static `nbits[]` (popcount table, IDA bytes 00 01 01 02 01 02 02 03 ...). BitVectors.c (the next object in .text) now has Pro7's dead bitvectorcount back and `data_at` places its .data at 0x5d7d60; SpillCode's range is 0x5d7ce0-0x5d7d5c and its relink no longer reports .data (only the global statics' .bss, expected while insertspillcode is lifted). BitVectors still links.
- Flags: deferred (rows descend against Pro7's order). `-sym on` shown: `-sym off` 5/8.
- Functions: 7/8 exact; insertspillcode 91.1 (registers only: esi/edx swapped for `flag`/`op` and the first loop's ternary temporaries; equivalent). Departures:
  - 2.6 changes (all in the bytes): paired-single spills (psq_l/psq_st, or addi + psq_lx/stx through a second GPR temporary, GQR 0x390) in insert_load/insert_store, a marking pass in insertspillcode setting IGNode flags 0x800/0x1000 from operand effect bits 0x8000/0x4000; error 219 when a virtual register number passes 0x7FFF (spillinstruction, spillcopy); FPR spill slots are `double` (or `stvec2x32float`), not Pro7's `unsigned long`; Pro7's duplicate assert (237) gone; assert lines 130, 227, 238, 257, 304, 317, 372, 491, 528, 743.
  - Needed shapes: insert_load's FPR offset in its own `offset2` (shared `offset` 97.7) and no `object` local (Pro7's 96.0/99.6); insert_store's paired branch reads `node->spillTemporary` (Pro7's `object`: 90.3); spillinstruction's `interferencegraph[reg = op->data.reg.reg]` (Pro7's `(reg = ...) < regs`: 88.6); spillcall reuses `i` with `i++, opSrc++` in the `for` (Pro7's `volatileCount`: 56.7). insertspillcode now has Pro7's `flag = FALSE; op = instr->args;` order (the original stores `flag` first; the old swapped order was 94.9 but the wrong statement order).
  - Adversarial review: `Select_Gecko_Load_Store`'s boolean `load` argument is now `TRUE`/`FALSE`.
  - Fixed here: `case RegClass_VR:` used for the size-2 store case (a value-2 rename accident) is `case 2:`; `(short)psop` casts gone (`short psop`, exact); `TYPE(TYPE(&stvectorunsignedchar))`; the `if (instr->block == NULL) FATAL` is Pro7's `CError_ASSERT`; insertspillcode's test uses PC_OP_IS_ANY_REGISTER; TRUE/FALSE.
- Behaviour: exact code is the original's. Genuine bug in the original (kept, commented): insert_store's paired-single path inserts the addi a second time after the flag branch (the load path's two inserts pasted in), so the pcode is linked twice (re-checked in IDA's pseudocode of 0x57caa0: `insertpcodebefore/after(a1, v10)` then `insertpcodebefore(a1, v10); insertpcodeafter(v10, v11)`). insertspillcode checked against IDA: same statements (the marking pass tests effect 0x4000 then 0x8000; ours computes them in that order too).
- Names: Pro7's. Invented (TODO): `rTEMP_for_PS_spill` (2.6's second temporary, no source names it; the Wii compilers' CodeView has no S_LDATA32 records for this module's statics, checked across all nine Wii builds).
- Types: Pro7's; opcode locals `int` (Pro7's `Opcode` enum is not in the repo: opcodes are #defines).
- Data: .data/.bss match in content; statics stay global with the lifted-code comment while insertspillcode is inexact (2.6's order: PS lowest, then VR, then the int). Bindings `_rTEMP_for_*`, `_last_unused_vreg_before_spilling` needed for that.
- Header: estimatespillcosts, insertspillcode (Coloring). Fine.
- Headers relied on: InterferenceGraph.h (IGNode members invented, TODO; flags 0x800/0x1000 have no names, used as literals here, in Coloring and CodeGen), InstrSelection.h (defines `Operand`, which Pro7 has in Operands.h: SpillCode must include InstrSelection.h for it), Operands.h (coerce_to_addressable), CodeGen.h (is_paired_single_operand returns `int`, low_offset/high_offset, CodeGen_GetNewVarInfo), PCodeUtilities.h (Select_Gecko_Load_Store), StackFrame.h (local_base_register, assign_local_memory), Registers.h/RegisterInfo.h (used_virtual_registers, n_real_registers, Registers_GetVarInfo), Coloring.h, PPCError.h (PPCErrorStr219), CMachine.h (stvec2x32float, stdouble), CParser.h, CPrep.h (copts), PCode.h, objects.h, CError.h, CompilerTools.h. Not reviewed: Operands.h, StackFrame.h, InstrSelection.h; GQR0 (0x390/912) has no name anywhere.
- Open: insertspillcode 91.1 (registers only, re-diffed: two-register swaps in the marking pass and the second loop; tried: declaration orders and --swaps, a separate first-loop counter, block-local `arg`, `op` reused, the flags through a local, nested ifs, the ternaries swapped, the loop as an inlined helper); the invented static name. (The BitVectors .data cut is fixed.)
- Adversarial review: attacked the data cut (broke: fixed by giving BitVectors its dead bitvectorcount and `data_at`), the insert_store double-insert reading (held, IDA), insertspillcode's statement order (held, IDA), the static's name (held: nothing names it), boolean arguments (`load` 0/1, fixed). Everything else held.

### src/optimizer/VectorArraysToRegs.c: SOLID WITH TODOS
- Boundary: 9 rows 0x5771f0-0x577fe0, Pro7's file in reverse; lookup_vector_array_object, allocatevectorpropinfo and checkalluses inlined; Pro7's unused static `precedes` (no literals, nothing to place) left out (comment: UseDefChains.h has a `precedes` of its own). .data 0x5d1b08 "VectorArraysToRegs.c" only; .bss 0x5e3bc0-0x5e3bd8 the six statics at 2-byte packing (converted_arrays lowest = reverse declaration order). Names doc: wii vectorarraystoregs 6.
- Flags: deferred (descending vs Pro7). `-sym off` shown: `-sym on` 4/9.
- Functions: 8/9 exact; computeaddilist 94.8 (registers only: the block index and `op`/`reg` take ebp/ecx where 2.6 has ebx; equivalent). Departures:
  - computeglobalvectorpropinfo is now Pro7's text (the old one was a reshaped copy with eight renamed pointers and a do/while); only its declaration order differs (`preds` first, `j` last; Pro7's order 65.5).
  - checkvectorstoreorload: Pro7's text plus a `reg` local and `if (... != reg) return FALSE; return TRUE;` (Pro7's `return a == b` 46.5, reg alone 90.0).
  - computelocalvectorpropinfo: `op = instr->args` (was `&instr->args[0]`), assigned before `i` (Pro7 assigns `i` first: 99.0, not previously listed); the ADDI test keeps an `addiop` local with nested ifs (Pro7's one `&&` chain 95.9, lookup in the condition 99.0).
  - scanforlocalvectorarrays: `elementCount = arraySize / 16` recomputed for the store and declaration order (each alone: 80.0 / 68.9).
  - scaninstructions, computeaddilist: nested ifs with the lookup as a statement (Pro7's chain 98.5 / 90.9).
  - convert_array_to_register reads `addi->instr` each time (Pro7's `instr` local 98.5, and 87.4 with Pro7's `deletepcode(instr)` too: re-checked); convert_arrays_to_registers hoists `best`/`score` to function level as `SInt32` (catalogued; Pro7's 61.9-82.7).
- Behaviour: as Pro7. No bugs found. The memory-operand tests are now Pro7's `(PCOpMemoryArg) op->arg == PCOpMemory1` (the enum added to PCodeInfo.h, below; same code), not a bare `1`; `computeusedefchains(FALSE)`.
- Names: Pro7's. Invented: none (`addiop` is a local).
- Types: Pro7's structs; `SInt32` loop counters/score where 2.6 needs them.
- Data: .data matches; statics global with the lifted-code comment while computeaddilist is inexact (making them `static` with `#pragma pack(2)` at the end of the file was tried: still 8/9, so that is the shape once it links). Bindings `_ADDIs` .. `_converted_arrays` needed meanwhile.
- Header: vectorarraystoregs (GlobalOptimizer, checked). Fine. Includes now Pro7's list (BitVectors, PCode, PCodeInfo, types back; CodeMotion.h dropped).
- Headers relied on: UseDefChains.h (Uses/Defs, reg_Uses, usedefinfo .useout, computeusedefchains, precedes), BitVectors.h, PCode.h/PCodeInfo.h (change_opcode, change_num_operands, PC_*), Registers.h, CFunc.h (locals), types.h, objects.h, CompilerTools.h, CError.h. PCodeInfo.h now has Pro7's `PCOpMemoryArg` (PCOpMemory0/1; added in the adversarial review, no function anywhere changed); AddPropagation.c (another unit) still writes `arg == 1`.
- Open: computeaddilist 94.8 (registers only, re-diffed: the block index and `reg` in ebp where 2.6 has ebx/ecx; tried: block-local `reg`/`node`/`l` as Pro7, the walkers merged, `short`/`SInt32 reg` (short 95.8 but adds a movsx), pointer arithmetic for `addi`, `defID` set apart, Pro7's `&&` chain with the `op` local, decl-order --swaps, the statics `static`).
- Adversarial review: attacked every listed needed shape with variants (checkvectorstoreorload's `return a == reg` 90.0, convert_array_to_register's `instr` local 98.5/87.4, computelocalvectorpropinfo's statement order 99.0: all held), the magic memory-arg `1` (fixed: Pro7's enum), boolean arguments (fixed). Nothing broke beyond those spellings.

### src/optimizer/CodeMotion.c: SOLID WITH TODOS
- Boundary: 19 rows 0x56f520-0x571740, Pro7's file in reverse; removeblockfromloop and dominatesallexits inlined. .data 0x5d1960 "CodeMotion.c" + the isloopinvariant switch table; .bss 0x5e3b98 is srawi_addze_isloopinvariant's `oldNextInstr`. The two COMMONs are Pro7's. Relink: everything but isloopinvariant's 17 bytes is the original's (data and .bss included).
- Flags: deferred. `-sym on` shown: `-sym off` 13/19.
- Functions: 18/19 exact; isloopinvariant 97.3 (registers only, in the memory case's write loop: ebx/edx swapped; equivalent). Departures:
  - 2.6 changes: may_alias_object/must_alias_object on the use-def's object (Pro7: may_alias/uniquely_aliases on two pcodes); r13 is invariant like r2 (`(int)reg == 13`); assert lines as Pro7.
  - Catalogued 2.6 shape: `TinyValue* v = &def->v;` in isuniquedefinition, uniquelyreachesuse, uniquelyreachesalluses, moveinvariantcomputation, srawi_addze_maymove (Pro7's 73-99%). maymove's `reg` is `int` (Pro7's `short` 92.2). moveinvariantsfromloop: `def` declared last.
  - simpleunswitchloop is now Pro7's text (the old one was a rewrite with its own locals and a stale "frame 8 bytes larger" comment). The first reviewer's "2.6 has no `lastpcode` local" was wrong: Pro7's `if (!(lastpcode = loop->body->lastPCode))` and its uses match too (variants: EXACT), so they are back. Needed: one `target` for Pro7's op27/op23, one `path` walker for Pro7's pathiter1/scanlist and the common-path copy (`for (path = common; ...)`, Pro7's `while (bestpath1)` 76.5), a separate `instr` walker for the two copies into the branch path, and its own declaration order; each is needed (61.5 -> 81.8 -> 96.1 -> exact). Pro7's register-tagged placeholder names (pc29, op27, path2_24 ...) are replaced by plain ones.
  - `def->v.arg == PCOpMemory0` (Pro7's name; was `== 0`).
  - Everything else is Pro7's text.
- Behaviour: Genuine bug in the original (Pro7's too): isloopinvariant's memory case tests `!pcode->flags & fIsConst` (precedence: always 0), so the read-def loop never runs; 2.6 still computes it at run time (IDA: `test edi; jne; inc eax; and eax, 0x40`). Kept with a comment, now with the flag's name; Pro7's `(flags == 0) & 0x40` spelling is 96.6.
- Names: Pro7's where Pro7's are real; Pro7's register-tagged placeholders in simpleunswitchloop (pc29, op27, path2_24 ...) are plain local names. Invented: none of the unit's own (Loop members' TODO names are LoopDetection.h's).
- Types: Pro7's.
- Data: matches (relink). Binding `_oldNextInstr$391` is only referenced by exact code; left while the unit does not link.
- Header: `extern int unswitchedinvariantcode;` was removed here (2.6 references it only from CodeMotion: bindings.py refs; the COMMON stays defined here), then put back by header round H2 (Pro7's API, and a Wii public COMMON). movedloopinvariantcode and moveloopinvariantcode are GlobalOptimizer's. Includes are Pro7's list plus CError, PCodeUtilities.
- Headers relied on: LoopDetection.h (Loop: invented member names TODO, has_memory_barrier/has_call/is_straight_line/exit_dominators/backedge_dominators; addblocktoloop, insertpreheaderblock, analyzeloop), UseDefChains.h (reviewed), Alias.h (AliasType0..2 are Ninji's placeholders, TODO), PCode.h (fPCodeFlag20000/40000 placeholders, fRecordBit, PCODE_FLAG_SET_F), LoopOptimization.h (pccomputepredecessors1), RegisterInfo.h (_FP_, _CALLER_SP_), BitVectors.h, PCodeUtilities.h, CompilerTools.h, CError.h.
- Open: isloopinvariant 97.3 (registers only; the statements re-checked against IDA's pseudocode of 0x571290: the r2/r13/_FP_/_CALLER_SP_ tests, the folded read-def loop and the write-use loop are as ours; tried: decl-order --swaps, Pro7's flag spelling, `(int)` casts dropped). Headers: Alias.h's AliasType0..2 and PCode.h's fPCodeFlag20000/40000 are placeholders, LoopDetection.h's Loop members invented (TODO), hence PENDING HEADERS.
- Adversarial review: attacked the "needed" simpleunswitchloop shapes (broke: `lastpcode` was not needed, Pro7's text restored), the precedence-bug reading and r13 (held, IDA), the bare `0` memory arg (fixed), header users (held). Graded PENDING HEADERS rather than WITH TODOS: the unit has no TODO of its own, its open items are the headers' placeholders.
- Header round H2 (October 16): Alias.h's AliasType_* and PCode.h's fOpFromMemory/fOpToMemory are TODO names; LoopDetection.h's Loop members TODO (Pro7 placeholders); CodeMotion.h DONE. Resolved as TODO names.

### src/optimizer/LoopOptimization.c: SOLID WITH TODOS
- Boundary: 16 rows 0x572fb0-0x575c20, Pro7's file in reverse; ispowerof2, insertupdateinstructions, lookup_array_object inlined. Pro7's dead `deleteloop` was missing: added back between unrollunknownBDNZ and deleteloopheader (2.6 has no code for it: deleteloopheader ends where unrollunknownBDNZ starts; its assert 1294 lies between asserts 764 and 1857, which 2.6 carries at Pro7's numbers). .data 0x5d1998 "LoopOptimization.c"; .bss 0x5e3ba0-0x5e3bac the three statics. Relink: only the two inexact functions differ.
- Flags: deferred. `-sym on` shown: `-sym off` 10/16.
- Functions: 14/16 exact; unrollloopconditional 88.0 (Pro7's text; registers, and the body-copy loop is entered through its step `firstInstr->nextPCode` in 2.6 where ours loads it before the jump; equivalent), unrollloop 98.1 (one register, `factor`'s copy). Departures:
  - Rewritten from Pro7 (they were IDA/decompiler text with invented locals): eliminateinductionvariables (do/while pseudocode, open-coded bit tests, `arg == 4`) is Pro7's plus an `int reg` loaded once per test, and declaration order (88.0 -> 94.7 -> exact); scanforlocalarrays (a `continue` chain) is Pro7's plus 2.6's `!copts.soft_float &&` on isFloat, `elementCount` recomputed, decl order; changearraytoregisters (`n`/`g`/`f`/`min`/`minf`, `long` counters, `data.imm.value` for the memory offset) is Pro7's with 2.6's shapes: lookups as statements in nested ifs (Pro7's chains 82.2/99.6), `*ptr = array->next; array = *ptr;` + continue, if/else counters (Pro7's two ifs 79.9), function-level `best` with separate `score`/`score2` (block-local 86.8), `if (!(array = arrays)) return;` (as Pro7's VectorArraysToRegs; Pro7's 97.6), decl order; rewriteunknownloopwithBDNZ (24 invented locals, ispowerof2 open-coded, `0x3f` for PC_ADDI, casts) is Pro7's names and loops back, with what 2.6 needs: a `while` for the addi loop (Pro7's `for` 99.8), separate `branch` for the bdnz (Pro7's reused `instr` lets MWCC tail-merge four inserts: 80.6), two lis temporaries and a function-level `tmpReg` (merged 92.9-94.6), `lo = LOW_PART(value1); if (value1 == lo)` with `lo` reused (FITS_IN_SHORT 59.4, FITS_IN_SHORT2 99.1), and its declaration order (Pro7's 81.3).
  - Adversarial review: rewriteunknownloopwithBDNZ's `value1`, `value2`, `absStep` are Pro7's `SInt32` again (they had been `int`; both compile the same, variants EXACT), its two bound tests are spelled `LOW_PART(loop->iterationCount) != 0` like rewriteloopwithBDNZ's (were `(SInt16)` casts; same code) with the original's bug commented at the site (it was only in REVIEW.md); `value2 = loop->upper + absStep` (and `loop->lower + absStep`) is a needed operand order (Pro7's `absStep + ...`: 94.9), not listed before; changearraytoregisters' shapes re-checked (one `score` 94.3, Pro7's two ifs 99.4, Pro7's chained `*ptr = array = array->next` 79.7); memory-operand tests use Pro7's `(PCOpMemoryArg) ... == PCOpMemory1`; `computeusedefchains(FALSE)`.
  - 2.6 changes: `LOW_PART(x) != 0` for Pro7's `x != 0` before the lis/addi pairs (rewriteloopwithBDNZ, rewriteunknownloopwithBDNZ; as in StrengthReduction); unrollunknownBDNZ's `mtctr_reg` is `short` and `postheader = loop->preheader->nextBlock` (each alone 88.5/70.5).
  - Needed shapes: computeliveonexit `last_virtual_GPR > gpr` (catalogued); insertnewpcblock declares `label` first (Pro7's 76.3); unrollloop's two walkers for Pro7's one `instr` (97.1; that and the plain assert are the rest); `last_virtual_GPR`, `totalUses`, `elements[]` are `SInt32` (int: 13/16). LocalArray's flags are Pro7's `unsigned int` bitfields again (were `UInt8`, same code).
- Behaviour: as Pro7 apart from the 2.6 changes. Genuine bug in the original: rewriteunknownloopwithBDNZ's lis/addi pairs for the compare bound test `LOW_PART(loop->iterationCount)` instead of the bound's low half (Pro7: `iterationCount != 0`), a copy of rewriteloopwithBDNZ's test; confirmed in the bytes (`cmp word ptr [ebx + 0x3c], 0`, 0x3c = Loop.iterationCount, at +0x943 and +0xa06 of the exact function); kept, now with a comment at the site.
- Names: Pro7's; unrollunknownBDNZ keeps Ninji's placeholder locals (blockA, instr28, reg25 ...) minus their register comments. Invented: none (the `lis`/`high`/`branch`/`tmpReg`/`lo`/`score2` locals are plain names).
- Types: Pro7's (rewriteunknownloopwithBDNZ's SInt32s restored; `branchOpcode` is `int` because the opcodes are #defines).
- Data: matches (relink: only .text differs). Statics `static` (no lifted function uses them).
- Header: optimizedloops* (GlobalOptimizer), pccomputepredecessors1 (CodeMotion), changearraytoregisters, optimizeloops. Fine. Includes: the 47-header list (most unused) is now Pro7's list plus CError, CPrep (copts), PCodeUtilities, <stdlib.h>; same code.
- Headers relied on: LoopDetection.h (Loop/BasicInductionVar invented member names, LoopBound), UseDefChains.h, BitVectors.h, PCode.h/PCodeUtilities.h, Registers.h, CFunc.h, CParser.h, CPrep.h, objects.h, types.h, CompilerTools.h, CError.h; PCodeInfo.h (PCOpMemoryArg, added in the adversarial review).
- Open: unrollloopconditional 88.0 (2.6 keeps `firstInstr` in the body-copy walker's register and enters the loop at its step; adversarial tries: a separate `bodyInstr` walker in three declaration positions 88.9, `firstInstr` itself as the walker 88.0, `while ((instr = instr->nextPCode) && ...)` and the `for(;;)` equivalent 69.3; earlier: a target-block local, the walker reused or `firstInstr` as the walker, `while` loops for the copy (66-69), hoisted `op`/`opID` and decl-order --swaps), unrollloop 98.1 (decl-order --swaps, one or two walkers, `SInt32 factor`).
- Adversarial review: attacked rewriteunknownloopwithBDNZ's types and spellings (broke: Pro7's SInt32 types match, so the `int`s went; the bug was undocumented in the source), the listed changearraytoregisters shapes (held), the original's bug reading (held, bytes), unrollloopconditional's loop entry (not fixed, tries listed), header users (held). Ninji's placeholder locals in unrollunknownBDNZ/unrollloopconditional (blockA, instr28, instr6, reg25, label24) remain Pro7's names: a cosmetic open item, not renamed.

### src/frontend/CPrepTokenizer.c: SOLID WITH TODOS
- Boundary: Pro7's CPrepTokenizer.c function list exactly (69 rows + the inlined prepcurstringchar/_skip and tohex, and Pro7's dead CPrep_SkipAsmComment); rows run 0x4d6960 (prepskipnextchar, Pro7's first) down to 0x4d1ec0 (lexidentifier, Pro7's last). .data 0x5b9bd8 (8-aligned), .bss is the one static `prepnextstringchar_foundnl` (0x5e0b00). Relink differs only inside tquot's .text.
- Flags: pro6 `-O4,p -inline auto,deferred -sym off`: deferred because the rows descend against Pro7's order (non-deferred is also 68/69 but lays the functions out ascending); `-sym on` gives 61/69.
- Functions: 68/69 exact; tquot 97.7 (one stack slot: 2.6 has `start_pos` at [esp], ours at [esp+4]; tried Pro7's declaration order 82.8, decl-order --swaps, block-scoped save_pos/ch64/offset, `int c`, a dead store to `t`; equivalent). Departures from Pro7, each tested against Pro7's spelling: needed shapes: prepskipnextchar `nextcharpos = pos = p` (Pro7 99.4), CPrep_MatchChar early `return` (97.3), CPrep_StringConCat's `galloc(tksize = offset)`, early returns, `ts_current--` + memmove on ts_current (Pro7's `elem = --ts_current` 91.6) and `buffer` declared after the chunk pointers (decl-order; Pro7's call of lookahead_noeol and Pro7's `chunk` walker restored), tapos `SInt32 t` and `t & 0xFF`/`0xFFFF` (Pro7's `(UInt8)t` 76.9, `int t` 98.2), tquot `size` declared after `save_pos` (Pro7's order 82.8); original's shape: the operator scanners (tless, tgrea, tplus, tminu, torrr, tampe, tpowe, tdivi, tzero, tpoin, tchrL, intsuffix) reuse the table's `short t` parameter where Pro7 has a local (Pro7 81.9-99.2; gc1's 1.3 decompilation has the same reuse); 2.6 changes: tchrL's wide character constant is IT_USHORT (`mov bl, 6`), tchr_ knows `__vec2x32float__`, CPrep_StringConCat decrements chunk size/offset (`dec`, where Pro7 adds `1 - stwchar.size`), lex drops TK_ILLEGAL after reporting it (`goto skipped`), assert lines (1393, 1431, 3022). No fakematch.
- Behaviour: every exact function byte-identical; tquot checked equivalent. Bugs of ours fixed: tzero/tpoin/tnumb passed `(char*)&failed` to CMach_FloatScan and cast CInt64_ScanDecString's result (stand-in prototypes; now Pro7's `Boolean*`, tnumb is Pro7's text verbatim).
- Names: all Pro7's; none invented in the unit (TK_UNK_115 for `__vec2x32float__` is tokens.h's placeholder, see headers).
- Types: Pro7's; `(Type*)&stwchar` casts now Pro7's `TYPE()`.
- Data: .data and .bss byte-identical (relink: only tquot's bytes differ); `prepnextstringchar_foundnl` is `static Boolean` again (exact functions place it); its binding in bindings.json stays until the unit links.
- Header: CPrepTokenizer.h now declares only the tokenizer's functions (Pro7's list); the 17 CPrep.c variables it carried moved to CPrep.h (Pro7's place); `prepcurchar`/`plex`/`lexidentifier` return `short` (Pro7). CPrep_SkipNewComment is used only here but before its definition (Pro7's header has it). CPrep_SkipAsmComment's declaration removed: nothing calls it anywhere (Pro7 neither); the function stays as Pro7's dead code, commented.
- Headers relied on: CPrep.h (the lexer globals, TokenStack, CPrepFileInfo, COpts: see CPrep.c's entry), CPrepTokenizer.h (its API; `unsigned char*` where Pro7 has StringPtr, since StringPtr is the driver's oslib.h type), tokens.h (TK_*, TStreamElement, now Pro7's TokenStream; `TK_UNK_113..11A` are Pro7's placeholders and 2.6 uses 115 for `__vec2x32float__` (CExpr, CPreprocess too); `TK_UU_ATTRIBUTE` is Pro7's `TK_UU_ATTRIBUTE_UU`), CError.h (ERR_* names are invented, TODO there), CMachine.h (CMach_FloatScan was `void* (char*, Float*, char*)`: fixed to Pro7's `char* (char*, Float*, Boolean*)`), CInt64.h, ObjGen_PPC_EABI.h/CDecl.h/COSToolsCLT.h (InlineAsm_gccmode, labelref, COS_IsMultiByte).
- Open: tquot's stack slot (register/frame only; also tried this round: block-local `ch64`, `save_pos`, a function-level `offset` at every position); the headers above.
- Adversarial review: re-diffed all 69 functions against Pro7 (all Pro7 text but the listed shapes), re-tested the operator scanners' `t` reuse against Pro7's `void` form (tdivi 88.9), checked the header's users and the function list against Pro7's (identical). Broke: CPrep_SkipAsmComment was declared in the header with no caller anywhere (removed). Held: boundary, flags, every listed departure, data.
- Header round H2 (October 16): COpts is CParser.h's CompilerLinkerOptions; tokens.h's TK_UNK_* and `__vec2x32float__` token are TODO names; CPrepTokenizer.h DONE WITH TODOS; CMach_FloatScan is Pro7's. Resolved as TODO names.

### src/frontend/CPrep.c: NOT SOLID
- Boundary: Pro7's CPrep.c in Pro7's order (cannotopenerror moved back to the top, CPrep_Reconfig back after CPrep_RemoveSpecialMacros, pragma_push/pragma_pop back between pragma_options and pragma_overload: the move had been unneeded); rows descend from 0x43ffa0 (CPrep_InsertSpecialMacro; insertmacro/cannotopenerror inlined) to 0x436380 (CPrep_BrowserCurrentFile). Pro7's dead `pragma_comment` added: 2.6's .data has its literals ("compiler", "exestr", "linker", "user", "lib") between preppragma's and pragma_pack's, and no "comment" dispatch (the unit's .data was 44 bytes short). Pro7's dead CPrep_StreamGetTemplate, CPrep_RemoveTokens, CPrep_ErrorMessage, CPrep_ErrorMessageVA, prepoffset and prepoffset2 are back (no rows, no literals, no jump tables: the object's .data and .bss sizes are unchanged): 2.6's asserts sit at Pro7's line + 9 from Pro7 line 673 to 1424 (909/900, 1463/1454, 2019/2010), so the file has Pro7's line count over that span, these functions included; and `lineoffset` is written but read only by prepoffset2. pragma_precompile_target is Pro7's static, inlined into preppragma; getfiledata is written in place (TODO). .bss now starts at 0x5ded90 (8-aligned; `exprtk`, which the old cut gave CodeGen) and ends 0x5df566; .data 0x5a76f0..0x5a8bd8. Relink: data and .bss byte-identical, only PragmaOnce/pragma_on_off_reset .text differs.
- Flags: pro6 `-O4,p -inline auto,deferred -sym on`: deferred by address order; `-sym off` 76/92, non-deferred 88/92.
- Functions: 90/92 exact. Inexact: CPrep_PragmaOnce 79.6, pragma_on_off_reset 91.9 (adjacent in generation order; registers only: the inlined strcmp copies take ecx/edx the other way round and pragma_on_off_reset's inlined CPrep_PopOption keeps `bits`; tried Pro7's text for both and its helpers, `return`/else shapes, a local flag/file, the helpers' parameter types (int/char), their local orders, PopOption `SInt32`/PushOption `char` (Pro7's, kept), MSL's exact tolower text). Now Pro7's text (normalised to the repo's member names) for C_Compiler (+ 2.6's `warn_uninitializedvar` and chained stores; the IDA locals `currentCU`/`optionSnapshot` gone), setupprep (+ ELF_Setup, chained stores), CPrep_TokenSize (`int len; short c` swapped), CPrep_GetFileName (+ 2.6's index loop), CPrep_ErrorName, setupfile (Pro7's `!flag1`, enumerators, Str255/OSType/void* types; the `isDefault` local and int casts were not needed), cannotopenerror (Pro7's `static char fname[64]`), is_nextchar (Pro7's text with `short c`: exact; the IDA-shaped for/continue body went), CPrep_PushOption/PopOption/PragmaLex/Error, preprocessor, prepifskip, ParseCond, pragma_overload, notendofline, foundnl, ismacroname4, GetPrepPos, NewFileOffsetInfo, BrowserFilePosition. Departures shown needed (Pro7 fails): chained assignments (cleanupprep, popfile, pragma_pack, CPrep_Define), early returns for Pro7's if/else (popfile, SetSourceFile, prepundefine, the inline pragmas), `int`/`SInt32` for Pro7's Boolean/int (ismacroname, StreamGetSemicolon, TSBufferGrow, setupprep's int return: `xor eax, eax`), `== 0` for `!` (StreamGetBlock, CheckTarget), a parameter reused for Pro7's local (StreamSkipToBrace/SkipBlock `val`), if/else for a ternary (ParseUnary), field-into-local copies (GetFileOffsetInfo/2); 2.6 changes: `__env_var` (CPrep_GetQuotedEnvVar), `__FILE__` with fullpath_file (CPrep_GetQuotedFileName), cprep_extrafile release, preprocess-dump calls in preppragma, `#include "x"` scanned like `<x>`, ErrorName without the name argument, crippled optimisation-level warning, CPrep_ResetFileInfo taking the file, GetFileOffsetInfo2's `linedelta`, the line count always computed in GetTokenContext. Shown needed this round: CPrep_Define's single `if (!existing_macro || error_flag)` block (Pro7's early return 99.3: 2.6 has one epilogue there; Pro7's `existing && error_flag` is Ninji's inversion, a bug of the reference) and its `length` copy of `mlist.size` (Pro7's 99.2), SetSourceFile's early returns (Pro7's nesting 88.0: 2.6 has an epilogue per exit). Fixed: preppragma's `(UInt32)opt->bits` cast was a wrong parameter type: pragma_on_off_reset takes `int bits` (Pro7 UInt32) and Pro7's `!(opt->bits & OPT_FLAG_4000)` then matches. TODOs (natural spelling tried): ErrorName's `(unsigned short)code` (natural 71.7: 2.6 compares the short 16-bit and sign-extends only for the push), poptokenseqInline (one poptokenseq with the local costs four functions), CPrep_ToLower/CPrep_PToCString (MSL-style inlines), getfiledata in place (98.9% as a function). Not re-derived (IDA-shaped, exact, Pro7's text 73-87%): XpandMacro (IDA locals; character and error literals now named), XpandString (`memcmp(.., "defined", 8)` was a stand-in: Pro7's strcmp matches), CPrep_GetTokenContext (its unused `lineno` local removed: it matches without; Pro7's text with getfiledata, plus every combination of the ternary and the context-loop spellings, 72.8-73.5), prepinclude, prepline, preppragma.
- Behaviour: exact functions byte-identical. Ours fixed: literal error ids (120, 0x66, 0x6f, 0x73, 0x74, 385, and 365 = ERR_ILLEGAL_MACRO_ARGUMENT_NAME in CPrep_Define) named; `memcmp` stand-in for strcmp. No genuine original bugs found.
- Names: Pro7's; COptPush's placeholder `x8` is `pushed` (TODO: invented). Wii's CPrep module is a later rewrite (CPrep_ExpandSpecialMacro, CPrep_PushString ...) and names none of the statics. Invented (TODO-marked): cprep_extrafile, envM, CPrep_GetQuotedEnvVar, CPrep_GetQuotedFileName, poptokenseqInline, CPrep_PToCString, CPrep_ToLower. IDA-named locals remain in XpandMacro/XpandString/GetTokenContext.
- Types: Pro7's except where tested (above). Macro's `args[1]` (Pro7 `names[0]`) makes CPrep_Define allocate `sizeof(Macro) - sizeof(HashNameNode*)`.
- Data: statics are `static` again in Pro7's order with 2.6's additions (cprep_extrafile after cprep_files, envM after trgtM), `high_mem_mark` declared before cleanupprep and `exprtk` before the expression parser (where 2.6's .bss puts them among the function-local statics `fname` and `dummyelement`), `#pragma pack(2)` to the end of the file (TODO: Pro7 resets after its globals); .bss layout now offset for offset the original's. Pro7's COMMONs `was_escchar` and `CPrep_SkipNewCommentChar` defined here (hash order checked: 383<397<520, 2296<2356<2370); tk/tkintconst/tkstring/tkidentifier/tkfloatconst/cparamblkptr/spaceskip/macrohashtable stay where Wii's modules put them. Bindings of the statics kept until the unit links (`_file_cannot_opened_name` is now the local `fname`).
- Header: the stand-in `PrepTokenBuffer {int count; SavedPrepToken* tokens}` (and CPrec.h's `SavedPrepToken`, a copy of TStreamElement) is Pro7's `TokenStream {SInt32 tokens; TStreamElement* firsttoken}`, defined where Pro7 has it (tokens.h), at every use (CPrec, CInline, CDecl, CTemplateNew/Class/Func: no function's verdict changed; CPrec's casts to TStreamElement* gone). CPrep.h's variable block is Pro7's list (it gained the lexer globals from CPrepTokenizer.h); setupprep, cleanupprep, CPrep_WarningName, CPrep_GetSourceFilePath, cprep_cursymfile and macrostart removed (no other user); PushOption/PopOption Pro7's types. File top: Pro7's six forward declarations replace a 66-line prototype list; 26 unused includes dropped (trim, Pro7's list kept).
- Headers relied on: CPrep.h: holds Pro7's common.h types (COpts with invented/`unknown66`/`unknownfd` members, CompilerLinkerParamBlk, Macro with renamed Ninji fields and `args[1]`), (TokenStream fixed, above; CPrec.h's inline action still calls its stream `u.k0.tokenBuffer` where Pro7's CInline.h has `inlinefunc.stream`), CPrepFileInfo's `unkfield123[3]` placeholder, types `UInt16 ispascalstring`, `int linenumber`, `int ts_preread_elements` differ from Pro7 (untested); CompilerTools.h: GList must be 2-byte packed (fixed: `mlist` sits at 2 mod 4 in 2.6's .bss; 2.6's exact set and all 218 relinks unchanged, other versions not checked); objects.h: Pro7's OBJECT_LIB_EXPORT added; include/libc/ctype.h: lacks Pro 6 MSL's inline tolower (four units carry copies); tokens.h/CError.h as above; CodeGen.h/IroPointerAnalysis.h/IrOptimizer.h/InstrSelection.h/PCode.h/Registers.h/BitVector.h/CDecl.h/CExpr2.h/CFunc.h (setup/cleanup calls) not reviewed here.
- Open: PragmaOnce and pragma_on_off_reset (the unit's only link blockers; also tried this round: pragma_on_off_reset's parameter int/SInt32/short/unsigned/long, five CPrep_ToLower spellings (`c & 0xFF`, a `char` parameter, an if/return, a `c = (unsigned char)c` local): 2.6 keeps PragmaOnce's `flag` in bl and its tolower index in eax, ours spills `flag` and indexes through edx); the IDA-shaped functions above (XpandMacro, XpandString, GetTokenContext, prepinclude, prepline, preppragma) still need Pro7-based rewrites (XpandString: Pro7's text with 2.6's `macropos = pos` and either poptokenseq 85.3-87.1); the invented poptokenseqInline copy; Macro's layout in CPrep.h.
- Adversarial review: attacked the boundary (assert-line offsets against Pro7: brought back six dead Pro7 functions the reviewer had left out; pragma_push/pop order), every TODO's natural spelling (the preppragma cast fell to a parameter type; ErrorName's cast held), the IDA-shaped bodies against Pro7 (is_nextchar now Pro7's; CPrep_Define and SetSourceFile's departures held as one-epilogue/epilogue-per-exit shapes; GetTokenContext lost a dead local), names (365, `x8`) and the header's stand-ins (TokenStream replaced across the front end). Held: the data, the statics' order, the flags, the remaining TODOs; the grade stays NOT SOLID for the IDA-shaped functions.

### src/frontend/CInline.c: SOLID WITH TODOS
- Boundary: Pro7's CInline.c whole: the 57 rows ascend 0x55ba00 (CInline_Finish) to 0x562350 (CInline_Init), exactly Pro7's file order reversed, with Pro7's statics inlined (MakeNotNot, TranslateID, CopyEMemberInfo, Return*, NewStatement, LoadToTemp, Serialize{EFORCELOAD,ECOMMA,EPRECOMP}, AddFRefList_{Expr,Statement}, GenerateTemplateInline, FindNestedTemplInst, AddDefaultFunctionAction); Wii has a "CInline" module; the asserts are Pro7's line numbers (+1 from GenerateDefaultFunc on: 4771/4772/4806/4861/5002), so Pro7's dead functions (ForceReverseSearch/Evaluation, NoFPLocals, AddMemberFunctionAction) are kept (no literals or tables). Relink: .data 0x5CFFC0-0x5D12A8 and .bss 0x5E3AD8-0x5E3B66 are byte-identical; the only differing bytes are UnpackIFunctionData's.
- Flags: pro6 `-O4,p -inline auto,deferred -sym on`: deferred because the rows descend against Pro7's order; unchanged.
- Functions: 56/57 exact (was 55/56: the old file was gc1/IDA text with ~20 invented inline helpers, rewritten from Pro7's text). CInline_UnpackIFunctionData 92.2: ecx/edx exchanged throughout the function, nothing else (tried: decl-order --swaps, int/SInt32/short and per-loop counters, sflags typed char/SInt8/int/short in CI_Var and in CInline_SetObjectSFlags, `inline`, Pro7's reuse of `object` (73%), `#pragma sym off` (90.5), gc1's separate `cursor` walker and declaration order (92.2/86.8), Boolean flag parameters). Departures: 2.6 changes (each in the binary): IsSmallFunction limit 30 (`cmp ecx, 0x1e`); InlineFunctionCheck lacks Pro7's `Q_INLINE && level == 0` shortcut; PackIFunctionData calls CMach_GetBackEndOptions; GenerateDeferredFuncs saves/restores the back-end options around GenIFunctionCode and clears copts.defer_codegen; switch cases have no `max`; PackSwitch allocates exactly the cases (`sizeof(CI_Switch) - sizeof(CI_SwitchCase) + n * sizeof(CI_SwitchCase)`); EstimateExpandedSizeOfFunction adds the ST_RETURN size (Pro7's text has `size =`; binary `add`). Original's shapes (each needed, natural form tried): IsTrivialExpression's EINDIRECT test joins Pro7's three returns at one value through a block-local `object` and `result` (Pro7's returns 89.6, block-local object with returns 96.9; this replaced the reviewer's invented inline helper CInline_IsWritableObject); `int` counter in GetLocalID (Pro7 SInt32: GetLocalID 166 bytes, PackActions 58.7); `(left)->type == (right)->type` then `left = ...` and switch through expr (FoldConst); function-level `list`/`i` and `uniqueid != 0` (CopyNodes); `expr = expr->data.monadic` before the EARGOBJ test (CheckUsage); `(arg = ...args)` in the constructor test (RefArgTransform); chained `local_dobjects[i] = obj = ...` with a local of its own and if/else for Pro7's ternary (SetupArgsExpression); `size++; size += ...` (EstimateExpandedSizeOfExpr); block-local funcdata + if/else level (InlineFunctionCheck); `return cinline_cur_serial_stmt = stmt` (NewStatement); `if (!(object = *objectptr))` (LoadToTemp); serialize-then-makemonadicnode statements (ELAND/ELOR); hoisted tfunc (ExtractInlineFunction); `(expr = stmt->expr)->type ==` (ExpandStatement); a `scan` walker for the export loop (Expand); separate returns (GenerateDefaultFunc, GenIFunctionCode); else-arm codegen and chained flag stores (GenFunc); `numcases++` before the walk; `~0x80000000` (GetLocalObj); direct `statements[i]` (AddFRefList_InlineFunc); declaration orders (CopyNodeList, SerializeECOND, IsSmallFunction, GenIFunctionCode). TODO workarounds: `(SInt16)object->sclass` (repo's Object.sclass is UInt16, Pro7's SInt16; this switch is movsx). CInline_GetLocalID2 is Pro7's (returns the index; the reviewer's `int*` out-parameter was not needed: Pro7's form with `int` counters in the callers matches). GetLocalObj/UnpackActions take Pro7's `Boolean` (the first reviewer's `char`, and InlineAsm_UnpackAsmStatement's `char` that kept it matching, were stand-ins: all three Boolean are exact).
- Behaviour: every exact function is byte-identical; the lifted one is equivalent statement by statement to Pro7 + 2.6's store order; the equivalent compiler (statics now `static`, its static bindings deleted) compiles C/C++ test files with inline, template, static-init, -inline auto/deferred/all/level=3/smart, always_inline + inline_bottom_up pragmas and -g identically to the original. Genuine bugs in the original kept: SerializeExpr's EMFPOINTER overwrites accessnode (Pro7 too); UnpackActions/PackActions translate EAT_DESTROYPARTIALARRAY's dtor and element_size as locals (Pro7 too). Bugs of ours fixed: none in matched code.
- Names: statics and functions Pro7's (renamed from evalMode, alloc_state, memo_list, fixup_list, data_0058245x/005dfb6x, pending_prec_nodes, deferredInlineNodes, undefined_function_objects ...); COMMON `cinline_tactionlist` (was pendingInlineWork) hash 336 between 319 and 343. Invented (TODO): CInline_GetLocalID2 (Ninji's), CI_Var.used/unmodified (Pro7 xD/xE), CI_Export.mysteryFlag (xC), CI_Switch.exprtype (unkSwitch8), CInline_NewLocalObject's `unused` (unk), CInlineBackendOptions.
- Types: Pro7's CI_FuncData/CI_Var/CI_Statement/CI_Switch/CI_SwitchCase/CI_Action/IDTrans/LabelTrans/UIDTemp/CI_Export/AObject/CI_StmtLink replace CInlineInfo/IStmtRec/CInlineVar/InlineSwitchData/CIBEntry/CPrecNode/IFixup/MemoNode/ChainRec/InlineSlot/InlineNode/InlineObjectEntry; ExceptionAction read by member (Exceptions.h's `operands[6]`/InlineOperand stand-ins deleted); ENode by Pro7's members (enode.h's MemberFunctionPointerData/InlineMemberPointerTarget stand-ins deleted: ELABEL is `data.label->stmt->flags`). CInlineBackendOptions' pad bytes now named: its layout is COpts' from `processor` to `old_style_bitfields` (offset - 2 throughout, alignment pads implicit at pack 2), then the optimizer members; the untouched bytes are the COpts members at those offsets (use_fp, soft_float, full_dwarf, the section pointers, report_heap_info..old_style_bitfields), marked "(not copied)" (CMachine, CInline, CPrec, CFunc, CClass unchanged).
- Data: statics Pro7's, `static`, in Pro7's order; .bss byte-identical (2-byte packing: `#pragma pack(2)` at the end of the file, TODO: Pro7's mac68k left on); 27 static bindings deleted from GC_2_6 (the lifted function reaches them through placements); symdecloffset (not CInline's in Pro7) defined beside symdeclend in CFunc.c (relink unchanged).
- Header: Pro7's CInline.h shape; declares only what other units use (CI_SFLAGS/CI_CanInline enums and the AddDefault/AddMember prototypes moved into the source); CMach_Get/SetBackEndOptions moved to CMachine.h; AddTemplateFunctionAction keeps `void*` parameters (TODO: typed, CTemplateFunc's CTempl_GetCreateFuncInstance loses its match).
- Headers relied on: common.h (typedefs; dead stand-in typedefs removed), CFunc.h (Statement, StatementType; symdecloffset extern added), CPrep.h (stream insert/remove, lines), tokens.h (FileOffsetInfo, TokenStream: Pro7's, the former PrepTokenBuffer), enode.h (still has placeholder views: explicitconversion/defaultargument `unk08`, templatecomparison, objaccess, scopebegin; comments naming CInline wrongly), Exceptions.h (`slots[6]` stand-in left for TOC's referenceexception), objects.h (Object.sclass UInt16 vs Pro7 SInt16), CMachine.h (CMach_Calc* take `double`/`volatile double` where Pro7 takes Float), CExpr2.h (makemonadicnode/makediadicnode take UInt8 for ENodeType), Switch.h (SwitchInfo.exprtype invented), CodeGen.h (mysteryFlag), InlineAsm.h (InlineAsm_UnpackAsmStatement's flag now Pro7's Boolean, InlineAsm.c exact and linking), templates.h, types.h, CParser.h, CScope.h, CClass.h, CABI.h, CDecl.h, CError.h, CException.h, CExpr.h, CInit.h, CInt64.h, CMangler.h, COptimizer.h, CPrepTokenizer.h, CTemplateNew.h, CompilerTools.h, ObjGen_PPC_EABI.h, scopes.h, DropInCompilerLinker.h: used as declared, not reviewed beyond the items named. (Since: enode.h's invented views are gone (Pro7's nullcheck, templdep.u.sourceref and templdep.subtype in their place, `vector128val`), Object.sclass is Pro7's SInt16, makemonadicnode takes ENodeType (makediadicnode keeps UInt8: ENodeType costs CExpr's pointercompare).)
- Open: CInline_UnpackIFunctionData (92.2, register-only) keeps the unit from linking (relink: its 33 bytes are the only difference); the TODO workarounds above (the sclass cast, AddTemplateFunctionAction's void*: rechecked, either typed parameter alone moves one reload in CTempl_GetCreateFuncInstance, a dummy declaration does not); symdecloffset/symdeclend are Pro7's ObjGenMachO.c COMMONs, defined in CFunc.c here (ObjGen_PPC_EABI.c would be Pro7's place; layout is by name, so neutral); header problems listed.
- Adversarial review: attacked the TODO workarounds, parameter types, the header's placeholder fields, the declared needed shapes and the inexact function. Broke: GetLocalID2's `int*` out-parameter (Pro7's return form matches with `int` counters), the invented CInline_IsWritableObject helper (an in-place result variable matches), GetLocalObj/UnpackActions/InlineAsm_UnpackAsmStatement's `char` (Pro7's Boolean matches in both units), CInlineBackendOptions' pad fields (named from COpts' layout). Held: IsSmallFunction's limit 30 (`cmp ecx, 0x1e`) and declaration order, CopyNodes' function-level `list`/`i` (Pro7's block locals 98.8), NewStatement's chained return and LoadToTemp's assignment in the condition (Pro7's spellings lose 5-6 functions), the void* in AddTemplateFunctionAction, UnpackIFunctionData's register-only miss. Grade lowered to SOLID PENDING HEADERS: the unit's own TODOs (invented names with no source, the sclass cast, the void* prototype) are justified, but enode.h's placeholder views and objects.h's sclass type are open problems of headers it relies on; the unit does not link only because of one register-only function.
- Header round H2 (October 16): enode.h's views, Exceptions.h's `slots[6]` and the `double` CMach_* are gone; Object.sclass is SInt16; makediadicnode's UInt8 and AddTemplateFunctionAction's `void*` are blocked (ledger); SwitchInfo.exprtype and CInline.h's names are TODO. Resolved as TODO names.

### src/frontend/CDecl.c: NOT SOLID
- Boundary: 0x5339a0..0x53c2e0, .data 0x5c0cc0 (8-aligned), no .bss of its own (COMMONs only); the 78 rows are Pro7
  CDecl.c's functions in exact reverse order, plus 2.6's CDecl_MergeArrayArgFlags where Pro7 has FuncRedeclCheck's
  callee slot; Pro7's statics 2.6 inlined everywhere and its dead functions (CDecl_ScanDeclarator, OldParseEnumList,
  MaxType, Find(U)SignedType, TemplatePTM ...) are kept: the original's assert lines run +65 across them unchanged.
  Relink --only: .data and .bss byte-identical, the only bytes that differ are CDecl_AddFunctionMember's.
- Flags: pro6 -O4,p `-inline auto,deferred` (rows descend against Pro7's order), `-sym on` (with `-sym off` 48/78).
- Functions: 77/78 exact (was 77/78, the same function inexact). The file is now Pro7's text (the old one was
  gc1/decompiler text with ~15 invented inline helpers, stand-in macros and a fake struct); departures:
  - 2.6 changes (confirmed in the disassembly): scandeclarator's `TK_ASM` -> CMach_ParseGlobalRegisterVariable (twice);
    GetFunctionObject allows Q_INTERRUPT and builds a TypeMemberFunc of its own for a cv method; MergeArrayArgFlags
    (new, called by FuncRedeclCheck); OverloadFunctionObject compares `qual & (Q_CONST | Q_PASCAL)` (Pro7 text: Q_CV,
    probably a Pro7 text slip: everywhere else Pro7 compares Q_PASCAL | Q_CONST); CheckCtorIntegrity returns Boolean
    and the caller reports error 239 (CTemplateFunc asserts on it); ParseBaseClassList drops HandleObject,
    SingleObject, SingleInheritance, __comobject; ScanStructDeclarator's 8-byte bitfields (`copts.has8bytebitfields`)
    and `suppressAlignment = is_bitfield`; ParseClassMembers keeps the halfword section (SInt16 r18);
    CDecl_NewTemplDepType returns TypeTemplDep* (its inlined copies in ScanStructDeclarator only match so).
  - Needed shapes, plausible originals: `(int)tk == K` and `(int)declinfo->storageclass != TK_x` (catalogued); `p == NULL ?`
    in AddThisPointerArgument; `x->f |= F` where Ninji has `x->f = x->f | F` (7 functions); an early `return` where
    Pro7's text has if/else (scandecl, scandirectdecl1, ScanPointer, ParseFriendDecl, ParseClass, TypedefDeclarator's
    single epilogue); inverted if/else in ParseDirectFuncDecl; `!r19`, `x->type != TYPECLASS`; block-local `ttempl`s
    (scandirectdecl1), a block-local `nspace` copy (conversion_type_name), `vbase` as the first walker
    (AddVBaseToList), `if ((i = vtbl_index) > 0) { --i; ...` (SetupClassLayout: a separate `i--;` is copy-propagated
    back into `i - 1`), `while (!(nspace = nspace->parent)->is_global) {}` + `enumlist = last = oec` (ParseEnumList),
    a fresh walker in CheckDefaultArgs, function-level CInt64s (ComputeUnderlyingEnumType, no Pro7 `unused`),
    declaration orders (OverloadFunctionObject, TypedefDeclarator, ParseBaseClassList, ParseEnumList, ParseClass,
    ParseClassMembers).
  - TODO workarounds (natural spellings tried, recorded at each): IsSingleArg inline helper + `int isMethod` tested as
    `(Boolean)` (CheckOperatorType; fakematch); MergeDefaultArgs keeps the old per-loop walkers (Pro7's do/while is
    the same control flow, 78% at best); GetFunctionObject's `tmp` union (catalogued); CompleteClass's unread
    `baseclass` (fakematch); DataDeclarator's `(UInt16)declinfo->storageclass` (a field type is probably wrong);
    `#pragma dont_inline` on the empty AddDefArgConstructor (2.6 calls it; probably a disabled body).
- Behaviour: all but AddFunctionMember byte-identical; AddFunctionMember differs only in one `|` vs `|=`
  (same effect). Genuine bugs in the original: none found. Bugs of ours fixed: none (the old text matched).
- Names: rows per names-2.6 (all ok); globals Wii publics + hash order (member_foi -> Pro7/Wii `member_fileoffset`,
  2889 between ptmstruct 2707 and first_fe_temporary_register 2915; global_access 139 between 70 and 151);
  Pro7's locals. Invented (TODO): CDecl_MergeArrayArgFlags, IsSingleArg, DeclInfo's offset members (now named:
  isConversionOperator, isTypeID, allowTemplateName,
  isTemplateName for x54..x57; firstDeclaration for x64), ClassLayout.firstVirtualSlot, BigDeclInfo.unused/valid,
  OverloadMode_NonMember/StaticMember/Member/MemberWithoutThis (Pro7's placeholders OverloadMode0..3, named from the
  switch in OverloadFunctionObject and the four call sites), labels binary_operator/define_enum/parse_width (Pro7:
  whatever/do_shit/fuckup; the TODOs were missing and do_shit was still in the file). DeclInfo's members that had
  descriptive names where Pro7 has real ones are now Pro7's (dtype=thetype, storage=storageclass,
  operatorToken=operator_token, declarationAttributes=exportflags, parsedData=expltargs,
  hasTemplateArguments=has_expltargs, browseFile/sourceFile/sourceLine=file/file2/sourceoffset), by one coordinated
  `rename.py --member DeclInfo.dtype=thetype ...` after the replay onto main (726 uses in 17 sources; every version's
  exact flags unchanged). The invented names left are those for Pro7's offset-named members (TODO in CDecl.h).
- Types: Pro7's ClassLayout and BigDeclInfo (were stand-ins ClassLayoutInput/MemberDecl with the fake
  `ClassTail13`/MAKE_NODE/CE_ASSERT/btype in the .c), DefArgCtorInfo (Pro7, CFunc.h; was CDecl.h's `DefArg`);
  DeclInfo fixed: `section` (was `extraQualifiers`), `in_friend_decl` (was `allowForeignNamespace`), `is_extern_c` (was
  `requireMangledName`, the opposite meaning), padding placeholders f3d/f38/x58 removed, Booleans per Pro7.
- Data: .data/.bss identical (relink); COMMONs only (global_access, member_fileoffset); tkintconst moved to CPrep.c
  (Pro7's place); bindings kept (COMMONs other lifted code reaches).
- Header: CDecl.h is Pro7's (structs at pack(2), push/pop balanced) with only the functions other units call;
  removed: private copies of other headers' externs (stsigned*, no_name_node, this_name_node, stillegal...),
  STORAGE_* enum (callers use `(int)storageclass == TK_x`), FunctionTypeBuffer, unused prototypes, includes of
  CInline/CPrep/CParser/setjmp (10 includers given the headers they use, `includes.py add`).
- Headers relied on: CError.h (invented ERR_ names, known); CScope.h (CScopeParseResult = Pro7 NameResult, invented
  member names, TODO there); tokens.h (TokenStream and TStreamElement are Pro7's, with Pro7's members tokens/firsttoken, since main's header work);
  CTemplateTools.h (GetSelfRefTemplate takes TypeTemplDep*/returns TemplClass*, ResolveMemberSelfRefs takes
  TypeClass*: Pro7 Type*/Type*, TemplClass*); types.h (TypeBitfield.suppressAlignment and CLASS_SOM_INIT invented,
  TODO there); common.h (Q_IMPLICIT_WEAK invented for Pro7 Q_20000; EXPORT_FLAGS_* added, Pro7's); objects.h
  (Object.sclass UInt16 vs DeclInfo.storageclass SInt16, see DataDeclarator; storageclass UInt16 loses 7 functions);
  types.h and templates.h placeholders this unit uses (Pro7's own: FUNC_FLAGS_4/80/4000000/10000000,
  CLASS_ACTION_0/1/3, TEMPLCLASS_FLAGS_2).
- Open: CDecl_AddFunctionMember 99.3 (one
  `tclass->flags = tclass->flags | CLASS_ABSTRACT`; with both flag updates `|=`, as 2.6 has, MWCC spills `tclass`
  instead of the inlined MakeTypeMemberFunc's `method`: 76.9; tried ~70 combinations of
  parameter/local types, declaration orders, MakeTypeMemberFunc and AddThisPointerArgument shapes, if/else inversion,
  extra uses of tclass); the unit links once it is exact. The fakematches above.
- Adversarial review: re-checked the boundary (78 rows exactly Pro7's order reversed, the extra row where
  FuncRedeclCheck's callee sits; Wii has no public for it, so the invented name stands), relink (only
  AddFunctionMember's bytes differ), COMMON placement (global_access/member_fileoffset where Pro7 defines them),
  copts members (all Pro7's plus the 2.6 pragma has8bytebitfields), Q_CONST|Q_PASCAL values. Tried the natural
  spellings again: Pro7's argCount ternary with a Boolean isMethod (99.1; with `== 1`, `(Boolean)`, an int or Boolean
  temporary, an if/else: 83.8-99.3, never exact: the IsSingleArg/int-isMethod fakematch holds), Pro7's
  CompleteClass loop (92.2, also while/`!= NULL`/TYPE_CLASS/decl order), Pro7's MergeDefaultArgs (43.9) and four
  Pro7-shaped walker layouts (55-64), Pro7's GetFunctionObject `TypeMemberFunc tmp` (98.9; memcpy, field copy),
  Pro7's `!tclass->sominfo` in AddThisPointerArgument (91.1, six callers lost), ParseEnumList's do/while and
  `last = oec` (99.3/79.0), conversion_type_name's declaration order and nspace copy (the frame shows the original's
  order), DeclInfo.storageclass UInt16 to drop DataDeclarator's cast (7 functions lost in 4 units), Pro7's `Type*`
  NewTemplDepType (ScanStructDeclarator lost). AddFunctionMember with `|=`: 39 more variants (MakeTypeMemberFunc's
  Pro7 shape, an assigning `declinfo->thetype = TYPE(tfunc = ...)`, assert operand order, `!= NULL` spellings,
  memclrw(galloc), flag==0, all 31 combinations of five toggles, Pro7's both-`=|`): 67.7-98.5, the spill choice
  (tclass vs the inlined `method`) never flips. What broke: three invented label names lacked TODOs (one still
  Pro7's obscene `do_shit`), OverloadMode0..3 placeholders in the file, and the unit's own DeclInfo names are not
  Pro7's (the first review filed that under "headers relied on"). Fixed the first two (the third after the replay, above); every function's exact flag
  unchanged (4020/4077), layout and relink as before. Grade stays NOT SOLID (two fakematches, one
  register-only near-miss keeping the unit from linking).

### src/backend/StackFrame.c: SOLID WITH TODOS (four register-inexact functions, does not link)
- Boundary: 41 rows, 0x4f6490-0x4fa3d6, all StackFrame's: Wii's StackFrame module names the 24 public rows; the other 17 are Wii statics (no public): Pro7's statics plus 2.6's `#pragma interrupt` trio and allocate_valocal. Order is Pro7's file reversed (deferred). Pro7's dead `is_large_frame`, `update_out/in_param_align` and `nearest_power_of_two` are in the source (assert 3070 is nearest_power_of_two's; asserts carry 2.6's lines 913..3218). Relink (re-checked per section): .data and .bss byte-identical; the only differences are the 4 inexact functions' code and the .reloc entries that follow it.
- Flags: pro6 -O4,p `-inline auto,deferred -sym on`: rows descend against Pro7's order. `-sym on` kept (earlier sweeps).
- Functions: 37/41 exact. Inexact (register-only): compute_interrupt_frame 92.9 (equivalent, checked against IDA), compress_data_area 73.8 (limit formula and list merge re-checked against IDA: same; IDA's `>= 0` branch order tried, worse), generate_traceback 98.4, assign_local_memory 83.8 (now Pro7's operand order `ALIGN_REMAINDER + ALIGN`, the order 2.6's code evaluates in; same score); tries in the ledger. Departures from Pro7: 2.6 changes (EABI frame: 8-byte linkage, LR at 4, `save_slot_size`, interrupt frames, soft-float/paired-single FPR saves with psq_st/psq_l, `__rtos_change_stack`, `<valocal>`, `MakeSymbolTableEntry` in call_helper_function, no `codegen_pic` test in need_link_register, align_bits' 31/32 base, error 215 on r31, `_savevr%d`/`_restvr%d`) confirmed in the disassembly. Needed shapes (natural spelling tried and worse): `int` for call_helper_function's `effect` (Pro7 `short`) and can_add_displ_to_local's `displ`, the `SInt32 n` copy in save/restore_nonvolatile_GPRs, local_base_register's `reg`, init_frame_sizes' ALIGN, local_offset_32 written out, `short vrsave_high/vrsave_low`. Original's shape: the `break;` after each `return` in use_helper_function (re-tried: without them it is auto-inlined and 7 callers break, 30/41; `if (...) { return TRUE; } return FALSE;` per case keeps it out of line but 62.8). TODO workaround: `32L - i` in the FPR/VR loops (re-tried: an `int` counter 72-75, `SInt32` 95-96, `short` with `32 - i` 88.4; 2.6's counter is a short sign-extended once).
- Behaviour: no bug of ours found; runtime check repeated (asm, Gekko `psq_st`, bitfields, varargs/interrupt test files compile byte-identically with the original and the equivalent compiler at -O0, -O4,p, -O4,s). Genuine bug in the original (exact code): a `#pragma interrupt` routine that makes no call saves its GPRs and FPRs at `o + i * slot` (i over every scratch register) but restores them, and compute_interrupt_frame sizes the area, by the count of saved registers, so a sparse mask restores the wrong slots.
- Names: Pro7's/Wii's. COMMON hash order holds for requires_frame, makes_call, uses_globals, dynamic_stack, large_stack, StackFrame_has_vectors, dummyvaparam, dummyvalocal, fixup_rtos_stacks; it does NOT for `interrupt_routine_flags` (hash 1319 at 0x5eac88 between LoopExitNumber 3993 and doubleconstpool 4023), though Wii's public of that name is read by the same functions (init_stack_globals, init_frame_sizes, compute_frame_sizes, the prologue/epilogue, check_dynamic_aligned_frame, ObjGen_DeclareCode, the asm generator): open (2.6's name may differ from Wii's; the source comment already says Wii's is the flags). Invented (TODO): compute_interrupt_frame, save/restore_interrupt_registers, allocate_valocal (Wii statics, unnamed), `frame_sizes_computed`, the interrupt save-area statics, `FixupRTOSStack`. Pro7's register-named locals `r30` (init_frame_sizes) and `r0` (compress_data_area) kept.
- Types: Pro7's; Traceback members after the XCOFF traceback table (TODO).
- Data: statics `static` in 2.6's order; .bss needs `#pragma pack(2)` at the end of the file (TODO: as CMachine, CompilerTools, CError). Bindings for the statics kept while the unit does not link.
- Header: StackFrame.h is Pro7's API: everything has a user outside the unit except Pro7's non-static `update_out_param_align`, `update_in_param_align`, `is_large_frame` (defined here, dead or inlined in 2.6), `large_stack` and `ObjClass` (kept as Pro7's declarations).
- Headers relied on: CFunc.h (locals, arguments), CodeGen.h (CodeGen_GetNewVarInfo, has_altivec_arrays, InterruptGenerationRecord), objects.h (VarInfo; VarInfoFlag1/2/80 placeholder names), Registers.h/RegisterInfo.h (used_nonvolatile_registers, scratch_registers, reg_state/RegState1 placeholder), PCode.h (no names for the paired-single effect bits), PCodeUtilities.h, Operands.h, ObjGen_PPC_EABI.h, CPrep.h (COpts: placeholders and TODO names remain), CError.h, PPCError.h, CParser.h, CScope.h, BE_symbol.h, CDecl.h, CMachine.h, types.h.
- Open: the four register-only near-misses; TODOs `32L - i`, the end-of-file pack(2), invented names; the `interrupt_routine_flags` hash-order conflict; the headers above.
- Adversarial review: attacked the `break`s in use_helper_function, `32L - i`, the parameter/local types, the inexact functions against IDA (compress_data_area, assign_local_memory, generate_traceback), header users, COMMON names by hash order, relink data per section. Held: the `break`s (natural spellings re-tried), `32L - i`, the types, the data. Broke: `interrupt_routine_flags` is out of hash order (open); assign_local_memory now has Pro7's operand order (2.6's evaluation order); an original bug noted in the interrupt save/restore; grade lowered to PENDING HEADERS for the headers' open placeholders.
- Header round H2 (October 16): PCode.h's paired-single effect bits are named (TODO); COpts' remaining names are TODO; ObjGen's InterruptGenerationRecord TODO; VarInfoFlag1/2/80 and RegState1 are Pro7's own placeholders (naming sweep). Resolved as TODO names.

### src/backend/CMachine.c: SOLID WITH TODOS (two register-inexact functions, does not link)
- Boundary: 48 rows 0x4beeb0-0x4c2910, Pro7's CMachine.c reversed plus 2.6's additions (bitfield layout helpers, Type_RequiresMemoryReturn, IsUnsignedType, GetCustomType, CustomIntConvert, ParseGlobalRegisterVariable, Get/SetBackEndOptions). Dead Pro7 functions kept for data (CMach_PrintFloat's "%g", CMach_AdjustFuntionArgs). Relink (re-checked per section): .data/.bss identical; only the 2 inexact functions differ.
- Flags: pro6 -O4,p deferred `-sym on` (CMach_Configure, Pro7's first, is the highest row).
- Functions: 46/48 exact. Inexact: CMach_ParseGlobalRegisterVariable 99.4, CMach_StructLayoutPackedBitfield 87.9 (registers; not retried). Changed this round: CMach_GetPPCTypeAlign is now Pro7's body with its PowerPC-alignment rules disabled by a constant (`if (0 && align <= 8)`), which keeps it out of line as 2.6 does and replaces the per-function `#pragma dont_inline` (both match; TODO: the original's spelling of the disabled rules is not known, but a dead body explains a 21-byte function 2.6 never inlines, a pragma does not); the custom types' integrals are Pro7's `IT_17`/`IT_18` (were 17/18). Needed shapes kept: AllocationAlignment's merged `tmpalign`, StructLayoutGetOffset's `qualifiers` local, Type_RequiresMemoryReturn's inlined trivial-class test, `(SInt32)align` and `== 0` comparisons. Workarounds (TODO, proven needed): `#pragma sym off` on ParseGlobalRegisterVariable, PackedBitfield's mask spelling and its CMach_BitfieldPadding helper (the same padding is written out twice above it), CMACH_SKIP_REGISTER_ARGUMENT macro.
- Behaviour: 2.6 changes confirmed (EABI small-struct returns, `__PPCe500__`, Gekko paired single type, bitfield packing, global register variables). Runtime check above (struct layout with bitfields and doubles) identical.
- Names: Pro7's; invented (TODO): the bitfield helpers, stcustomtype113/114, the skip macro.
- Types: Float is common.h's `struct Float { FloatStorage data; }` wrapping a union, so the float functions read `fval.data.value` where Pro7 has `fval.value`; only Operands' word-pair initializers need the union (nothing reads `.words`): a stand-in shape, reported under headers.
- Data: statics `static` in Pro7's order, `#pragma pack(2)` at the end (TODO, as StackFrame).
- Header: CMachine.h: all declarations used outside except Pro7's CMach_GetQUALalign/AdjustFuntionArgs (kept as Pro7's API).
- Headers relied on: common.h (Float's two-level wrapper; ObjRegInfo made-up name with `char` members; `typedef struct FloatFormatBuffer` names nothing), CPrep.h (COpts, AlignMode*), tokens.h (TK_UNK_113/114 placeholders), types.h (IT_17/IT_18 placeholders), RegisterInfo.h (RegInfo made-up names), CInline.h (CInlineBackendOptions mirror), CInt64.h, CClass.h, CParser.h, PPCError.h, ScanFloat.h.
- Open: two register near-misses; the GetPPCTypeAlign constant, per-function `sym off`, invented names; the headers above.
- Adversarial review: attacked the per-function pragmas, magic numbers, header users, relink data. Broke: the `dont_inline` pragma (a dead Pro7 body does the same: kept with a TODO naming both), the raw 17/18 integrals; grade lowered to PENDING HEADERS (Float, ObjRegInfo, the token/integral placeholders). Held: the rest.
- Header round H2 (October 16): FloatFormatBuffer is gone; `Float`'s FloatStorage union is documented (2.6's word-pair initializers); ObjRegInfo/RegInfo, COpts' members, CInlineBackendOptions and TK_UNK_113/114 are TODO names; IT_17/IT_18 are Pro7's. Resolved as TODO names.

### src/backend/InlineAsmPPC.c: NOT SOLID
- Boundary: 26 rows 0x4ac980-0x4b2690, Pro7's InlineAsmPPC.c reversed (Wii module agrees). Pro7's InlineAsm_GetMnemonic present (dead in 2.6). Relink (re-checked per section): .bss identical; .data differs only in the two jump tables of the inexact functions (lbl_005B0440, lbl_005B045C).
- Flags: pro6 -O4,p deferred `-sym off`.
- Functions: 24/26 exact. Inexact: InlineAsm_TranslateIRtoPCodePPC 89.5 (checked against IDA statement by statement: no behaviour difference; with Pro7's `int i` the SPR loop is 2.6's but `r20` spills, 85.4), InlineAsm_ScanAssemblyOperands 92.5 (`effect` types tried); ledger updated. Fixed this round: retain_GPR_pair takes Pro7's `reg`/`regHi` locals (same score); the no-op `(SInt32)` casts on `opcodeinfo[].insn` (already SInt32) removed; `PC_PSQ_L + 1`-style opcodes named (`PC_PSQ_LU`, `PC_PSQ_LUX`, `PC_PSQ_STU`, `PC_PSQ_STX`, `PC_PSQ_STUX`), `0xC0000000`/`0x40000000` as `fOpTypeMask`/`fOpTypeFPR`/`fOpTypeVR`, AltiVec's `CPUFLAG_40000000`, `0x11`/`0x12` integrals as `IT_17`/`IT_18`. Found undocumented: InlineAsm_ScanAssemblyInstruction's `UInt32 flag3..5` tested through `(UInt8)` casts (Pro7: Boolean): natural spellings tried (Pro7's 87.3, Boolean 88.7, words tested whole 94.1); now `(Boolean)` with a TODO (fakematch: 2.6 stores words and tests bytes). Needed shape: the opcode-field tests written `(x >> 26) & 0x3F` there (OPCODE_PART_1 99.3; a masked macro breaks InlineAsm_TranslateIRtoPCode).
- Behaviour: runtime check repeated with Gekko asm (`machine gekko`, psq_st, ps_add, `@ha`/`@l`): identical objects at -O0, -O4,p, -O4,s. Genuine bugs in the original (Pro7's too): memoryoperand passes the Object, not its name, to error 122 (commented); the psq side-effect test lists PSQ_L/LU/LX/LUX/ST/STX but not STU/STUX.
- Names: invented (TODO): `warn_def_reserved_reg`, `asm_volatile`, and this round's names for Pro7's placeholders: IAOpnd_Placeholder/GlobalObj/LocalObj/GCCReg/GCCImm (IAOpnd_0/3/4/6/7), `u.obj.reftype` (unk), `u.gccreg`/`u.gccimm` with `kind`/`effect`/`rclass` (unk6/unk7: one layout, kind 2 register, 1 immediate), IAExpr's `var`/`var2`/`label2` (xC/x10/x18), IAEntryPoint's `opcode`/`flags`/`argcount` (x0/x2/x4, the InlineAsm fields they overlay), IAFlagsB_Record..Volatile (IAFlagsB_1..40).
- Types: `assembledinstructions` is Pro7's UInt32 (was SInt32; every user unchanged). IAExpr_0..10 are still Pro7's placeholders (they number relocations as PCode's equally unnamed RefType does); RegClass_6 is a Registers.h placeholder.
- Data: COMMON `InlineAsm_labelref` was defined nowhere (CPrepTokenizer reads it): now defined here, Pro7's place, hash order checked (2783 between 2751 and 2834), relink unchanged.
- Header: InlineAsmPPC.h now declares only what other units use: IAExpr, its type enum and three inline helpers, the IAFlagsB flags and OPCODE_PART_* (used only here; Pro7 puts them in InlineAsm.h) moved into InlineAsmPPC.c. Still there: the 1.x EncodedOperand/ParsedAsmInstruction stand-ins (with `short pad`) for TOC.c, GCCInlineAsm.c, FuncLevelAsmPPC.c, CodeGen.c and CPrec.c (TODO), Pro7's `supports_hardware_fpu`/`assembler_type` externs (no outside user).
- Headers relied on: InlineAsm.h, InlineAsmRegisters*.h, InlineAsmMnemonicsPPC.h, PCodeInfo.h (opcodeinfo; RefType placeholders), PCode.h (no names for the paired-single effect bits 0x2000/0x4000/0x8000/0xC000), Registers.h (RegClass_6), CPrep.h (COpts), StackFrame.h, TOC.h, CodeGenOptPPC.h, the CExpr/CParser headers.
- Open: IAExpr_0..10; the 1.x stand-ins in the header; the flag3..5 cast TODO; the two near-misses.
- Adversarial review: attacked casts, magic numbers, placeholder members, the header's users, undefined COMMONs, the inexact functions against IDA. Broke: an undocumented fakematch cast, no-op casts, magic opcode offsets and flag constants, a COMMON defined nowhere, Pro7's UInt32 type, header-private types; placeholders named. Held: the directives, the 2.6 changes, the data.

### src/backend/InstrSelection.c: NOT SOLID
- Boundary: 83 rows in Pro7's InstrSelection.c order, descending addresses (0x444370-0x454a50), one object; Pro7's
  statics without rows (small, shift_right_immediate, invert_relop, ispowerof2, shift_and_mask, ...) present and
  inlined; asserts carry "InstrSelection.c" (0x5a9480); .data 0x5a9430 (8-aligned) to IroDump's 0x5a9838, no .bss;
  Wii module InstrSelection holds the public rows. Pro7's COMMONs `cgdispatch[MAXEXPR + 1]` (no source defined it;
  0x138 bytes = the original's extent) and `precomputedoperands` (was in CodeGen.c) are now defined here.
- Flags: pro6 -O4,p, `-inline auto,deferred` (rows descend against Pro7's order), `-sym on` (whole-unit `-sym off`:
  60/83 exact against 75/83).
- Functions: 75/83 exact. Departures from the reference: EABI changes throughout (soft float first, paired singles,
  small-struct returns, 64-bit bitfields, ISEL; confirmed in the code, AGENTS.md); `SInt32` where Pro7 has `int`
  for `reg`/`finalReg`/`tmp`/`shift` in 10 functions (needed shape, each shown by variants); Pro7's unused
  `short tmpreg3` in I8_gen_SUB (original's shape: Pro7 has it; without it the old source needed an `Operand* out`
  alias, now gone); `#pragma sym off` around I8_ShiftRightImmediate (TODO, inexact anyway); `is_gpr_float_copy`
  (invented inline helper for gen_ASS's materialised gprfloatcopy test, TODO, fakematch-grade until gen_ASS matches).
- Behaviour: gen_ASS read branch by branch against IDA (same tests, callees, constants). Genuine bug in the original:
  I8_ShiftLeftImmediate's signed path emits `srawi regHi, lo, shift` (should be 32 - shift; the extra two
  arguments are ignored), kept and commented. Bugs of ours fixed: none found; `cgdispatch` was never defined.
- Names: Pro7's/Wii's for every row. (invented: is_gpr_float_copy)
- Types: I8_gen_ADD, I8_gen_SUB, I8_gen_MUL had `int` registers with ~80 `(short)` casts and an `Operand* out`
  alias: now Pro7's `short` locals, no casts, no alias (exact). The `IS_TYPE_VECTOR_STYPE` macro and its dummy
  `int stype` locals (gen_ASS, gen_TYPCON) removed: plain IS_TYPE_VECTOR gives identical code. Magic numbers named
  (OBJECT_REG, RegClass_*, PCOp_REGISTER, Effect*, VarInfoFlag1, OpndFlags_Volatile, CPU_PPCGEKKO, PC_ISELLT/GT/EQ,
  IT_17/IT_18); expanded `copts.soft_float && IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(t)` written as TYPE_IS_SOFT_FLOAT;
  `if TYPE_IS_8BYTES (t)` macro-paren spellings fixed.
- Data: .data cannot be checked by relink while 8 functions are inexact; `_used_regs$NNNN` statics and jump tables
  are only "not placed" because of that.
- Header: InstrSelection.h is Pro7's API (gen_*/I8_* are Wii publics, only cgdispatch uses most); variables extern.
  It defines `Operand` (Pro7: Operands.h) with `unsigned int flags` where Pro7 has `UInt32` (not changed: shared).
- Headers relied on: InstrSelection.h (Operand, GEN_NODE/ENSURE_* macros: fine; Operand placement/`flags` type
  open), PCodeInfo.h (opcodes; see PCodeInfo entry), PCode.h (paired-single effect bits 0x2000/0x4000/0x8000/0xC000
  have no names; `fPCodeFlag*`/`fPCBlockFlag*` placeholders), types.h (IT_17..IT_24 placeholders), RegisterInfo.h
  (OBJECT_REG), Operands.h, SFPE_PPC_EABI.h, CMachine.h, CodeGen.h (reviewed below); not reviewed in depth: CError.h,
  CInt64.h, CParser.h, FunctionCalls.h, Intrinsics.h, ObjGen_PPC_EABI.h, StructMoves.h, TOC.h, IroLoop.h, BE_symbol.h.
- Open: does not link: I8_gen_DIV_MOD 96.2, I8_ShiftRightImmediate 89.1, gen_CONDASS 98.4, gen_COND 78.3, gen_TYPCON
  99.3, gen_ASS 83.5, gen_AND 99.4, gen_MODULO 97.7 (register/stack-slot only; ledger has every attempt, this round's
  added); `is_gpr_float_copy` and the per-function `sym off` TODOs.

### src/backend/CodeGen.c: SOLID WITH TODOS
- Boundary: 43 rows in Pro7's CodeGen.c order, descending (0x42e9b0-0x436380), deferred; 2.6 additions in place
  (set_vr_reg_flags, CodeGen_CheckExpr, CodeGen_FindInterruptGenerationRecord: Wii publics; parse_section_pragma,
  is_paired_single_operand, update_bc_range ...); Pro7's allocate_temporaries and CodeGen_ParseLongIntegerORonORoff
  absent (no rows, no data needing them). .data 0x5a6c48, .bss 0x5dec28 (8-aligned). **Links** (relink: 219/219
  Matching sources; 2806 -> 2849 functions linked, 52.0 -> 54.0% of bytes).
- Flags: pro6 -O4,p deferred, `-sym on` (`-sym off`: 34/43).
- Functions: 43/43 exact. move_assigned_argument (99.6, an IDA-shaped `int r` / `(r = reg) < 10` / `++r` body) went
  exact on Pro7's text plus 2.6's changes (the GPR-pair test, FATAL 941/956 before the reg>=10 loads, paired-single
  PS_MR/psq, the MR with a distinct alias for pointers, soft-float stores/loads). CodeGen_AssignCheck now uses
  IS_TYPE_VECTOR on its TypeStruct copies (was the expanded `(int)stype >= 4 && <= 14` chain; the TypeStruct locals are
  the original's shape: Pro7's `Type* exprtype` is 92.9). Pro7's text replaced IDA's elsewhere earlier (reviewer:
  assign_labels, HandleIntrinsicCall/HandleTypeCast/IsPublicRuntimeObject/UpdateBackEndOptions, objc offsets,
  SOMStub, GenVDispatchThunk's saves). Remaining shapes: `{ SInt32 reg = opnd.reg; if (reg != N) ... }` (catalogued
  needed shape, in returnstatement, callprofiler, SOMStub); `thunk_this_register` (invented inline helper, TODO:
  Pro7's inline `char reg = ... ? 4 : 3` is 79.4, 2.6 returns the register in al from an inlined call).
- Behaviour: move_assigned_argument read against IDA branch by branch: 2.6 passes `reg` (0 there) instead of
  `vi->reg` to gecko_load_store_register in the paired-single load path (a bug of the original, kept and
  commented). set_vr_reg_flags, parse_section_pragma (reviewer). No bugs of ours found.
- Names: Pro7/Wii. (invented, TODO: cpuM, eabiM, rt_ptr_glue, thunk_this_register, paired_single_mask)
- Types: magic numbers named (reviewer's set, plus this round: `copts.processor == 0x16` -> CPU_PPCGEKKO (3 sites),
  `emitpcode(0x1a9` -> PC_PS_MR, `0x42` -> PC_ADDIS, `vi->flags & 2` -> VarInfoFlag2, `(Type*)&void_ptr` ->
  TYPE(&void_ptr), the hand-expanded HIGH_PART/LOW_PART/FITS_IN_SHORT and `0xb` registers in GenVDispatchThunk and
  load_TOC_pointers). Left as numbers: the paired-single effect bits (`opnd.flags & 0x8000 || & 0x4000 || & 0xc000`,
  load_TOC_pointers and is_paired_single_operand: PCode.h has no names; testing an Operand's flags with effect bits
  looks like 2.6's own slip), `#pragma section` typebits, GQR0 `0x390`.
- Data: .data and .bss byte-identical (the unit links). `saveheaperror` is `static heaperror_t saveheaperror =
  NULL;`: an explicitly zero-initialized static is laid out after the deferred functions' local-initializer templates
  (the three `UInt32 masks[RegClassMax]` zero templates), so it lands at the bottom of the .bss (0x5dec28) as in 2.6;
  Pro7's tentative definition, in any position, lands above them (tested: before CodeGen_Generator, before
  CodeGen_heaperror, end of file, block extern, a function-local static: all at +0x3c). The reviewer's removal of
  `data_head` from config.json is right (the object now places the static itself); 2.5/2.7 keep theirs until their
  sources are checked. The 14 bindings of CodeGen's statics (`_saveheaperror`, `_temps`, the 12 `Macro`s) deleted:
  nothing outside the unit references them and the unit links.
- Header: CodeGen.h keeps Pro7's API (some unused outside: altivecM, returnlabel, rt_profile_*,
  CodeGen_reportheapinfo, as Pro7 declares them). `interrupt_generation_records` moved to BE_elf.h (BE_elf.c defines
  it; ObjGen and CodeGen include BE_elf.h); nothing changed in any object.
- Headers relied on: CodeGen.h (fixed), BE_elf.h (gained interrupt_generation_records; SectionRec typebits
  unnamed), StackFrame.h (`StackFrame_has_vectors` invented, Wii has pcode_has_vectors), types.h
  (`TYPE_IS_GPR_PAIR` is an invented macro without a TODO; IT_17..IT_24 placeholders), PCode.h (paired-single
  effect bits unnamed), CPrep.h, InstrSelection.h, PCodeInfo.h; the other ~50 includes not reviewed (`includes.py
  trim` not run).
- Open: invented names; `next_varnumber` defined here without evidence (TODO); thunk_this_register; the unnamed
  effect bits; symbols.txt gives `saveheaperror` size 0x40 (the unnamed templates after it have no symbol).
- Header round H2 (October 16): resolved: CodeGen.h (DONE WITH TODOS), the paired-single effect bits (EffectSingle/PS0/PS1) and SPR_GQR0/1, `StackFrame_has_vectors` (Wii's public), SectionRec's `typebits` (documented), `next_varnumber` (Pro7's `localcount`, defined in CFunc.c). Still open: types.h's `TYPE_IS_GPR_PAIR` (and `TYPE_IS_SOFT_FLOAT`) are invented macro names without a TODO.
- Follow-up (October 16, after round H2): types.h's TYPE_IS_GPR_PAIR and TYPE_IS_SOFT_FLOAT carry `TODO: invented name`: no longer pending.

### src/backend/PCodeInfo.c: SOLID WITH TODOS
- Boundary: 11 rows in Pro7's PCodeInfo.c order, descending (0x529b00-0x52b220), deferred; Pro7's dead formatters
  (expectandformatoperand, formatoperand, formatoperands) present for their literals in .data (0x5c0360-0x5c0830,
  8-aligned); makecopyforload is 2.6's (Wii public, PCodeInfo module). Relink: only vformatpcode's 7 bytes differ,
  so .data is byte-identical.
- Flags: pro6 -O4,p deferred, `-sym on` (`-sym off`: 8/11), `#pragma pool_strings on`.
- Functions: 10/11 exact. Departures: 2.6's Gekko format letters, RefType_2/RefType_E cases, PSQ/LHA reuse cases,
  `is_location_independent`'s r13/r2 (2.6 changes); pcode_get_hi_lo's else-if chain (one epilogue; Pro7's returns
  93.5); `char buf[4]` (Pro7 buf[2] gives a smaller frame, 96.8); change_opcode's `UInt32 flags` read-back (Pro7's
  82.0) - needed shapes, commented. nbytes_loaded_or_stored_by and change_num_operands are Pro7's text again.
- Behaviour: no bugs found; ours fixed earlier (is_16bitdata).
- Names: placeholders renamed (unkreg_r23/r22 -> nregs/extra, thing* -> pbits/spr/bits/suffix, r6/r0 -> hi/lo,
  per-case block locals -> mask/sysreg/baseEffect), parameters Pro7's.
- Types: `pcode_const_from_format` takes Pro7's `SInt32*` (callers' locals SInt32; InlineAsmPPC's four `int` locals
  changed to Pro7's SInt32, no code change); every opcode number, RegClass, PCOp kind, flag mask named; Pro7's
  `RefType` enum (RefType_0..D, plus 2.6's RefType_E) replaces the partial stand-in `PCOpMemoryArg`.
- Data: no .bss (Pro7's orig_buf COMMON and do_show_basic_blocks static are absent in 2.6: the dead formatters use
  them through a TODO-marked `extern`).
- Header: PCodeInfo.h: opcode table type and PC_* numbers, RefType, prototypes all used elsewhere.
- Headers relied on: PCodeInfo.h (OpcodeInfo's `operand_count`/`rank` invented for Pro7's x8/x9, TODO), PCode.h
  (unnamed paired-single effect bits; placeholder flag names), ObjGen_PPC_EABI.h, Alias.h, Registers.h, TOC.h
  (fine for what is used).
- Open: vformatpcode 99.7 (one register in case 'T'; switch and block-local spellings tried); TODO names.
- NM5 (October 16): vformatpcode exact in every version: the 's' case's SPR-index counter and the 'T' case's
  time-base register number are case-block locals (`sysreg`, `tbr`); the unit links in every version.
- Header round H2 (October 16): OpcodeInfo's operand_count/rank stay TODO; PCode.h's flags and effect bits are named (TODO); the opcodes are Pro7's `Opcode` enum. Resolved as TODO names.

### src/backend/Peephole.c: NOT SOLID
- Boundary: 49 rows in Pro7's Peephole.c order, descending (0x500c30-0x507ba0), deferred; Wii later split it into
  Peephole/PeepholeForward/PeepholePatterns (no 2.6 evidence of a split: one descending run). No .data. The split's
  .bss start 0x5e0ecb is wrong (not 8-aligned): the statics are 0x5e10e8-0x5e1854 (defininginstruction lowest,
  Pro7's reverse order); 0x5e0ecb-0x5e10e8 (0x21d bytes) belongs to no reviewed unit.
- Flags: pro6 -O4,p deferred, `-sym on` (`-sym off`: 33/49).
- Functions: 47/49 exact. Now Pro7's text: VMR_VMRP, peepholemergeblocks (one departure: `block->lastPCode` read
  directly in the first test), computeliveness (SInt32 counters, `block` declared last), computepossiblemask (except
  the RLWIMI case, TODO), rotatedefinedusedtest (SInt16 copies), LBZ_EXTSB_CMPI_BC; Pro7's functions are `static`
  again; installpattern takes `short` (Pro7's 2-byte Opcode). Removed the invented helper `usedordefinedbetween`.
  Remaining shapes: `PCode* t = instr`/`defInstr` block copies in ADDI_L_S (2), RLWINM_RLWIMI_STW and
  LBZ_EXTSB_CMPI_BC (fakematch, TODO: 2.6 does not reload a spilled pointer after stores through it), `p = array[i];
  scan = p;` in the RLWINM_RLWIMI_ST* pair (local copy, plausible), canuseupdatetest's if/return (catalogued),
  canmergemasks' `int` b1/c1 (Pro7 SInt32 costs adjustforward).
- Behaviour: genuine bug in the original (and Pro7): rotatedefinedusedtest scans `instr`'s operands for every `scan`
  instead of `scan`'s; kept. computeliveregisters' 2.6 additions match the code.
- Names: Pro7's. (invented, TODO: LI_L_S, clear_base_register, LiveRegs' members)
- Types: magic numbers gone with the Pro7 bodies; TYPE_IS_SOFT_FLOAT for its expansion.
- Data: statics global (`liveregs`, `peepholepatterns`, `defininginstruction`) with the lifted-code comment while two
  functions are inexact.
- Header: Peephole.h declares the three functions others call; fine.
- Headers relied on: PCode.h, PCodeInfo.h (above), UseDefChains.h, Registers.h, RegisterInfo.h (reg_info_list),
  Scheduler.h (uses_vpermute_unit), InstrSelection.h (ismaskconstant), Alias.h, CMachine.h: what is used is fine.
- Open: adjustforward 68.1 and ADDI_LU_SU 98.0 (this round's ADDI_LU_SU tries in the ledger); the `PCode* t`
  workaround; clear_base_register; computepossiblemask's RLWIMI spelling; the .bss cut.

- Adversarial review: attacked the four entries' Pro7-text claims, the saveheaperror/data_head question and the
  remaining magic numbers. Broke: CodeGen's "needed shapes" -- move_assigned_argument was still IDA's body, and
  Pro7's text with 2.6's changes is exact, so CodeGen links (2849 functions linked); saveheaperror's "no position
  moves it" (`= NULL` does, by MWCC's layout rule for initialized statics); CodeGen_AssignCheck's expanded vector
  test; seven unnamed constants in CodeGen; interrupt_generation_records' declaration. PCodeInfo regraded SOLID PENDING HEADERS (its entry lists open PCodeInfo.h/PCode.h placeholders). Held: InstrSelection's,
  PCodeInfo's and Peephole's entries (Pro7's text scores below the current bodies: adjustforward 67.8 vs 68.1,
  gen_AND with FITS_IN_USHORT 98.8 vs 99.4, GenVDispatchThunk on Pro7 79.4 vs exact; the remaining differences
  are register/slot/padding only), the I8_ShiftLeftImmediate and rotatedefinedusedtest original bugs. Nothing
  lost: compare_start.json -> now, 0 functions lost, 1 gained; 219/219 Matching sources link.

### src/frontend/CExpr2.c: SOLID WITH TODOS (does not link: 2 register-only near-misses)
- Boundary: 85 rows, every one Pro7 CExpr2.c's, in Pro7's order reversed (deferred); Pro7's dead/inlined statics present (SearchExprTreeReplace, HasFuncCall, DerefETEMPCopy, stringconstnode ...): their statics `cexpr_rsearch_*`/`cexpr_hascall` are in 2.6's .bss (0x5e0400..0x5e04be lays out exactly as Pro7's statics in reverse at 2-byte packing: right/left conversion types 0x400/0x404, callbacks 0x408/0x40c, rsearch/esearch bools 77 bytes each, hascall 0x4aa, user_std_match 0x4ac, firstarrayexpr 0x4b6, assign_value 0x4ba). Wii splits the file (CExpr2/CExprGen/CExprTools) so it only confirms names. The .data cut starts at 0x5b09bc `align:4`: 0x5b09bc..c0 is unreferenced padding of PPCError's .data (IDA: no xrefs), the object starts at 0x5b09c0 (`mon_arg`) — fixes itself when the unit links.
- Flags: pro6 -O4,p `-inline auto,deferred` (rows descend against Pro7's order), `-sym on` (unchanged; nothing here shown to need off).
- Functions: 83/85 exact (scannew 86.5, user_assign_check 97.5, unchanged). 26 more functions are now Pro7's verbatim text (types renamed to the repo's) (promote, RecSearchExprTree via `data.mfpointer`, forceintegral, GetETEMPCopy, TempModifyExpr, MakeObjRefNode, AdjustFunctionCall, CalcStdAssign, MatchCV, MatchAssign, StdMatchCompare, MemberPointerConversion, ClassPointerCast, ConvertToCondition/Integral, CheckArithmConversion, get_address_of_temp_copy, MatchOverloadFunc, GetFuncMatchArgs, GenericPtmfCall, scan_new_declarator (no `(DeclInfo*)&di->thetype` cast), assign_check, wild_conversion_check, CheckOperatorConversion, ConversionIteratorInit/Next, NewETEMPNode's ternary, ClassPointerCast's `SInt32 offset`, MakeFunctionCall without the dead `tfunc =`). Departures left, each tried against Pro7's spelling with `fn.py variants`:
  - 2.6 changes (confirmed by exact code/IDA): IntConstConvert's custom-int branch (`integral >= IT_17` → CMach_CustomIntConvert; was a magic 0x11 and a wrong "vector" comment), LValue's EFUNCCALL small-struct temporary, VarArgPromotion's hidden-arg promotion, ConstructObject's template-ctor filter for a same-class copy, IsStaticType's second argument (the caller pushes two), SearchExprTree's `SInt32 count` (`int`: 46.1%, 430 bytes).
  - needed shapes (Pro7 spelling n%): AliasTransform `!= 0` (88.3), ArithmeticConversion's restructure (93.9), GetClassAccessNode reusing `expr` for Pro7's `tmp` (95.2), MatchCompare/MatchArgList/GetDefaultArgument single-exit forms (95.0/97.1/93.9: Pro7's text cannot tell return from epilogue), std_assign_check `ptr1/ptr2` locals (99.6), GenericFuncCall's if/else for Pro7's ternary (99.6), monadic_conversion_check `(int)which` (98.6), is_legal_type_combination's chained `diadic_arg1.type = diadic_arg2.type = x` and short kinds (97.4), NewArray's ternary forms (99.2/99.8), NewExceptionSafeAlloc's `else` (95.3), scandelete's block-local `TypeFunc* tfunc` (98.9; a typed local of another pointer type, original's shape), cv_qualifier_list's `t = tk = lex()` local copy (90.4), ConversionIteratorIsHidden `static inline` (plain static: not inlined, Next 37.8) and its re-read `TYPE_FUNC(OBJECT(obj)->type)->flags` (97.7), ConIteratorInit's declaration order (79.6).
  - TODOs: ConstructObject's `(expr = args->node)->rtype` assignment nothing reads (fakematch-grade: without it 96.1, Pro7's form 97.6, using `expr` 77.5); std_assign_check_overload's CExpr_MakeObjRefNode written out (call form 92.6 in every declaration order); IS_FUNC_LIST/IS_TEMPL_FUNC_LIST invented inline helpers (Pro7's IS_TEMPL_FUNC macro inline: 54.1; `Boolean` helper: 19.6); user_assign_check's `IS_TYPE_CLASS(r18b = expr->rtype)` (natural 97.2 vs 97.5, inexact function).
- Behaviour: exact code; checked at run time (C++ test with overloads, templates, new[]/delete[], conversions at -O0/-O2/-O4,p: equivalent compiler's objects identical to the original's; (adversary) except `new T[n]` with a constructor and a variable n, one extab byte, from another unit's data: see below). Bugs in the original: none found. Bugs of ours: none (the IntConstConvert comment was wrong).
- Names: (adversary) Pro7's placeholder enumerators CheckResult0..4 named by their C++ rank (CheckResult_NoMatch/Exact/Promotion/Conversion/UserConversion, TODO: invented), and ComparisonValues/ArgMatch's members (kind1..3/kind3Weight, anotherm5/match5) named exactMatches/promotions/conversions/conversionWeight, standard/userStandard (TODO: invented; CTemplateFunc.c's two uses renamed with them). Pro7's `diadic_arg1`/`diadic_arg2` for the invented `operator_operand_arg`/`right_operand_arg` (rename.py, every version); Pro7's ConIteratorList/ConIterator/ConversionIterator (members iter/children/tclass, objiter/myconiter/coniter) and FuncMatchArgs (exprs/args) for the stand-in names ConvItem/ConvNode/ConversionSearchState/MemberCallArguments (also in CExprConvMatch.c, CTemplateFunc.c, common.h). Invented (TODO): ComparisonValues/ArgMatch and their members (Pro7's placeholders Match5/Match13, x0..xE), IS_FUNC_LIST/IS_TEMPL_FUNC_LIST.
- Types: Pro7's prototypes restored: `short assign_check(..., UInt32 qual, ...)`, `AccessType` for GetClassAccessNode, `short`/`Boolean` std_assign_check_overload, `UInt32 cv_qualifier_list`, `Boolean`/`short` iszero/isnotzero/IsOne/AllBitsSet, `TypeClass*` for CExpr_ConstructObject (callers in CExpr, CInit, CABI, CFunc now pass Pro7's `TYPE_CLASS(x)`), `Boolean cexpr_esearch_bool[]`. Left: `makediadicnode(UInt8 ty)` (documented TODO in the header: Pro7's ENodeType costs CExpr's pointercompare).
- Data: every static is `static` now: (adversary) `assign_value`, `firstarrayexpr`, `user_std_match` too, their GC_2_6 bindings deleted: exact functions place each (compare.json: .bss offsets 172/182/186 -> 0x5e04ac/b6/ba, one base), and lifted scannew/user_assign_check reach them through those placements (equivalent compiler built and checked). `assign_node`/`temp_reference_init` are Pro7's globals (COMMON). File ends `#pragma pack(2)` (TODO-commented; without it the placements disagree, so it is verified, not a guess). `CInline_CopyExpression(.., CopyMode0)` for literal 0s.
- Header: CExpr2.h now declares only what other units use (about 30 declarations with no outside user removed, statics made static as in Pro7: CalcStdAssign, std_assign_check(_overload), ConIteratorInit, MatchArgList, IsStaticType, monadic_conversion_check, cv_qualifier_list, NewAlloc, NewClass); dropped the invented `PowerOfTwo` struct and a private `extern TypeIntegral stsignedlong`. Includes cut to Pro7's list (+CPrep.h for copts).
- Headers relied on: CExpr2.h (above; still includes CTemplateClass.h only for TemplArg, and keeps the Match5/Match13 stand-in names); CExpr.h (`BinaryOperatorResult` = Pro7 `Conversion` with `expression` for x0, `NameResult` names objects/object/basePath/is_qualified, `data_0055f5fa` per HEADER_ISSUES); CMachine.h (`CMach_FloatIsZero/IsOne(double)` where Pro7 takes `Float`: callers write `floatval.data.value`); CDecl.h (DeclInfo invented member names with TODO: isNewExpression/arrayBound/isNewTypeId); enode.h (`data.longval` for Pro7's `data.precompid` in EPRECOMP nodes); types.h (IT_17/IT_18 placeholders); CInline.h fine; CScope.h `x1F` placeholder.
- Open: scannew (86.5: 2.6 spills `newESalloc`) and user_assign_check (97.5) register near-misses (diffs re-read: no wrong reference); the four TODOs above; the .data cut fixes itself once it links; other versions' bindings still name the three statics.
- Adversarial review: attacked the TODOs (std_assign_check_overload's call form re-tried: 92.6, holds), the statics left global (all three reachable through exact functions' placements: made static, nothing lost), the IsStaticType second argument (original pushes `TYPE_METHOD(tfunc)->theclass` in edx and ignores it: holds), placeholders left unnamed (CheckResult0..4, Match5/Match13 members: named), the inexact functions' diffs for wrong references (none), boolean hygiene (iszero/isnotzero and six Boolean locals took TRUE/FALSE), header users (every CExpr2.h declaration has an outside user). Held: boundary, flags, the .bss reading, the 2.6 changes. Run time: new[] of a class with a constructor and a variable count gives one different extab byte (06 vs 0a) in the equivalent compiler, but not in the relinked one and not with any of CExpr2/CFunc/CPrec lifted (pre-existing, also in the reviewer's and the main tree's builds): the cause is the data of some non-linking unit, not this one's code.
- Header round H2 (October 16): CExpr2.h is merged into CExpr.h (Conversion is Pro7's name; Match5/Match13 and CheckResult names TODO); `data_0055f5fa` gone; CMachine.h takes Pro7's Float; enode.h has Pro7's `precompid` (the unit still writes `data.longval`); DeclInfo, NameResult `x1F` are TODO. Resolved as TODO names.

### src/frontend/CFunc.c: SOLID WITH TODOS (does not link: 2 register-only near-misses)
- Boundary: 51 rows, Pro7 CFunc.c's functions in Pro7's order (reversed in the binary), Wii's CFunc module has the publics (CFunc_AppendStatement, newlabel, findlabel, create_temp_node(2), CheckCLabels, parameter_type_list ...). (adversary) The reviewer's "left out" was wrong: 2.6's asserts carry Pro7's line numbers across them (SetupFunctionArguments 2797 and CFunc_ParseCtorInitializer 3286 on either side of CFunc_Get/SetGlobalCompilerState; DestructorCleanup's 1045 after TempTransDestroy's 738), and every global CFuncSave saves exists in 2.6 (sinit_*, ainit_*, check_arglist, cfunc_is_extern_c are CFunc's own statics): Pro7's four dead functions and CFuncSave are back (dead: the linker dropped them; no data), and DtorTemp is back in CException.h where Pro7 has it (CException 41/41, links). .bss 0x5e0bd8..0x5e0c08 matches Pro7's statics in reverse at 2-byte packing (cfunc_hasdtortemp lowest, destroyobjects 0x5e0bda, firstblock 0x5e0bfa, the three `temp_*` shorts last).
- Flags: pro6 -O4,p `-inline auto,deferred` (rows descend), `-sym on`.
- Functions: 49/51 exact (statement 73.6, DestructorReturnTransform 97.3, as before). The file was gc1's text with ~25 invented helpers (AppendStmt, NewLabel, FindLabel, NewScope, RestoreBlock, ScopedBody, CondJump, GenLoop_Append/Insert, MakeCaseStatement, CFunc_InitVariableInfo, FindNameLink, the `keep_fn` function-pointer hack, `struct Fn004f2700` with `pad[0x12]`, `lalloc(0xe)`/`lalloc(30)` literals, `*(UInt16*)&curstmtvalue`); it is now Pro7's text. 2.6 changes found while doing it: scancase has no case ranges (one value per case), GenerateSingleExprFunc appends an explicit ST_RETURN, CFunc_SetupNewFuncArgs and CFunc_AdjustOldStyleArgs turn arguments passed through a hidden pointer (CMach_PassResultInHiddenArg) into references, ParseFuncDef calls CPrep_ResetFileInfo. Needed shapes (each tried in Pro7's spelling): chained `currentblock = firstblock = block` (NewDeclBlock, FuncGenSetup) and `stmt->value = curstmtvalue = 1` (replaces the `*(UInt16*)&` fakematch), `|=` for Pro7's `x = x | F` (ifstatement, makeifstatement), early returns for Pro7's wrapping ifs (GenerateDummyFunction, CheckCtorInitializer, ParseCtorInitializer), `return` for `break` (identifier_list, LocalDataDeclarator), `(int)` storage-class compares (parameter_list, LocalDataDeclarator), declaration orders (CopyObjectList, GenerateDummyCtorFunc with one `arg1` walker, ParseFuncDef, ParseCtorInitializer), returnstatementadjust's single exit, GenerateLoop's `s = stmt = CFunc_InsertStatement(..)`, ParseCtorInitializer's separate `previous` walker (merged: 80.8), ParseFuncDef's chained `firstStmt.sourceoffset = functionbodyoffset = sourceoffset` (98.7 split) and `stmt16` after the test (99.3), AutoResultCheck without Pro7's `default: break;` (that statement stops its one-level self-inlining). TODO: CFunc_DestructorCleanup keeps seven invented inline helpers (CFunc_CheckJump ... CFunc_EmitAllCleanups) that are Pro7's CheckInitSkip/NeedsDestruction/DestructorNeeded/DestructLocals/FindLastNonCommonStackObj written over dobjstacks; with Pro7's helpers (which DestructorIfTransform/ReturnTransform use exact) it is 78.4% (stmt/next registers; decl-order --swaps: nothing better).
- Behaviour: Pro7's text has a bug 2.6 lacks: CFunc_GenerateLoop initialises the second variable to `upperBound` and steps it by `increment2`; 2.6 (and CABI's caller, which passes the source pointer and the element size) initialises it to `increment2` and steps it by `increment1` (fixed, commented). Run-time check (loops, switch, new[]/delete[], ctor initializers, overloads at -O0/-O2/-O4,p): identical objects. Original's bugs: none found.
- Names: Pro7's/Wii's throughout; `localcount` is Pro7's name for the COMMON the repo called `next_varnumber` (COMMON hash 1917 fits between cparser_fileoffset 1671 and high_reg 1983 at 0x5eb1d0; `next_varnumber`'s 3166 does not), renamed everywhere and defined here as in Pro7 (moved from CodeGen.c). Invented (TODO): the DestructorCleanup helpers.
- Types: (adversary) CFunc_DefaultArg is `static` (Wii: no public; no outside user). Pro7's prototypes (CFunc.h's block replaced by Pro7's: `CFunc_DefaultArg(Type*, UInt32, FuncArg*)`, `Boolean CFunc_ParseFakeArgList(Boolean)`, `CFunc_ParseFuncDef(..., Boolean is_method, Boolean is_static, ...)`), `short blockcount`, Pro7's `Boolean` flags for ParseLocalDeclarationList/LocalDataDeclarator (the old `char`/`int` + `(char)` casts were the stand-in), CInit.h's `RegisterObjectCB` takes `Boolean` (Pro7's; CInit unchanged).
- Data: (adversary) every static is `static` now and the `#define currentblock cfunc_currentblock` hack is gone: each of the eleven left global (firstblock, currentblock, blockcount, sinit_*, ainit_*, check_arglist, cfunc_is_extern_c, cfunc_staticvarcount, cfunc_hasdtortemp) is reached by exact functions, whose placements put the .bss at 0x5e0bd8 with one base, so lifted statement() reaches them through those; their 11 GC_2_6 bindings deleted. The file ends `#pragma pack(2)` (TODO-commented: without it the placements disagree, offset 8 -> 0x5e0bde). `"$localstatic%ld$"` literal as the binary.
- Header: CFunc.h is Pro7's prototype list (minus CFuncSave, and (adversary) minus CFunc_DefaultArg and CFunc_DestructorCleanup, which no other unit calls; DestructorCleanup stays extern: Wii has its public), with `extern short localcount`; the `symdecloffset/symdeclend` (Pro7: ObjGenMachO.h) TODO stays. Includes trimmed (includes.py trim) and Pro7's directly used ones restored.
- Headers relied on: CFunc.h (CLabel/Statement/DeclBlock Pro7's; LoopInfo invented TODO; `CFunc_Gen(.., UInt8 unk)` is Pro7's placeholder); CException.h (ExceptionAction stand-in per HEADER_ISSUES; DtorTemp (adversary) back in CException.h as in Pro7); Switch.h (`SwitchInfo.exprtype` invented for x8, TODO); CDecl.h (DeclInfo invented names); CInit.h (RegisterObjectCB fixed); CodeGen.h (still defines other units' COMMONs); types.h (added Pro7's `IS_TYPE_SOM_CLASS`); CPrep.h (`spaceskip` defined here, Pro7 defines it in CPrep.c).
- Open: statement (73.6; decl-order --swaps best 74.1, two spill slots swapped, no wrong reference) and DestructorReturnTransform (97.3) register near-misses; the DestructorCleanup helper TODO; the dead functions' bodies are Pro7's (2.6's are unseen); `StmtFlag_1..10` and `CFunc_Gen`'s `unk` are Pro7 placeholders (header owner); other versions' bindings still name the statics.
- Adversarial review: broke the boundary claim (four Pro7 functions the assert lines put in the file, restored with CFuncSave; DtorTemp back in CException.h) and the data claim (all statics can be static; the `currentblock` #define gone, pack(2) verified by placements); CFunc_DefaultArg made static and two header declarations without outside users dropped; `flag = TRUE/FALSE` in CFunc_Gen. Held: flags, the localcount rename (hash 1917 between 1671 and 1983), the GenerateLoop reading, the 2.6 changes, the needed shapes (exact; not re-litigated), every exact function stayed exact (49/51) and nothing elsewhere moved.
- Header round H2 (October 16): Exceptions.h's ExceptionAction is Pro7's; LoopInfo, SwitchInfo.exprtype and DeclInfo are TODO names; StmtFlag_1..10 and CFunc_Gen's `unk` are Pro7's own placeholders (naming sweep); CodeGen.h's COMMONs are CodeGen.c's. Resolved as TODO names.

### src/frontend/CPrec.c: SOLID WITH TODOS (does not link: 4 near-misses)
- Boundary: 84 rows, Pro7's CPrec.c in reverse order (PrecompilerRead lowest, SetupPrecompiler highest); Pro7's RelocateTable/RelocateRootNameSpace/FixNameIds/DefineStaticData/GetObjNameSpacePatch/GetObjectListPatch are inlined (no rows). .data 0x5bee68 and .bss 0x5e3028 start 8-aligned. `macrohashtable` is defined here though Pro7 defines it in CPrep.c (CPrep.c defines none of Pro7's tkfloatconst/tkstring/tkidentifier/spaceskip/macrohashtable block): left for CPrep's owner.
- Flags: pro6 -O4,p `-inline auto,deferred`, `-sym on`.
- Functions: 80/84 (DumpColorSymbolTable 92.3, GetObjectPatch 95.0, GetTypePatch 64.6, GetArgListPatch 97.9; unchanged). Pro7's verbatim text adopted where it matches (13: CompressWrite, GetInlineFuncPatch, GetTemplateFriendPatch, GetObjCInfoPatch, GetSOMInfoPatch, GetVTablePatch, AppendData, NamePatch, NewAddrPatch, FindAddrPatch, OLinkListCopy, Cleanup/SetupPrecompiler). Needed shape across the Get*Patch family: 2.6 works in image offsets (`SInt32 CPrec_AppendAlign`, `p + offsetof(T, f)`, `CPrec_AppendAlign(); next = cprec_offset;`): Pro7's pointer text is 63-98% per function. GetTypeClassPatch's `found` fakematch replaced by Pro7's `if (!(addrPatch = CPrec_FindAddrPatch(..)))` (exact). TODOs: PrecompilerRead is IDA's shape with Pro7's statics written inline (Pro7's helpers and RESOLVE_SAFE: 61.1; with if/else assignments 66.7); GetObjBasePatch writes out Pro7's GetObjNameSpacePatch (as a static it is not inlined: 34.9/48.4); AddressHashVal's union (Pro7's byte casts: 37.5 and it changes every inliner).
- Behaviour: precompiled header written by the equivalent compiler is byte-identical to the original's and its use compiles identically (run-time check). Version 5145 (Pro7 1047), the compressed patch offsets big-endian (2.6 additions, commented).
- Names: Pro7's; invented with TODO: CPrec_AppendBigEndianWord32, the Header's raw-buffer fields (`rawsize`, `rawdatasize`, `rawdataoffset` for Pro7's x28/x2C/x30). Header's other x6/xC..x24/x9C..xE4 are Pro7's own placeholders (written, never read).
- Types: Pro7's (`Header`, `Patch`, `AddrPatch`, `BuiltIn`, `TokenPatch`, `PointerHash`, `StaticData`); `PreComp_StaticData(..., SInt32 size)` and `SetupPrecompiler(Boolean)` as Pro7.
- Data: statics `static`, declared in reverse (flushmax lowest); the file ends `#pragma pack(2)`; the `pack(push, 2)` around the typedefs and statics only packs the structs.
- Header: CPrec.h is Pro7's: dropped the unused `PRecData` (and its common.h typedef), the private `extern init_expressions` copy and four includes it did not need (GCCInlineAsm.c and CFunc.c now include InlineAsm.h/InlineAsmPPC.h themselves: without them InlineAsm_Assemble went implicit in CFunc).
- Headers relied on: CPrec.h (fixed); CTemplateClass.h includes CPrec.h for nothing; CInline.h, InlineAsm.h/InlineAsmPPC.h (IA types for the asm patches; placeholders per HEADER_ISSUES); CompilerTools.h (GList); driver CWPluginsPrivate.h/COSToolsCLT.h (SDK parameter types still pending per review table).
- Open: the four near-misses (diffs re-read: GetObjectPatch a stack slot, GetTypePatch the inlining, DumpColorSymbolTable the reload; no wrong reference); the three TODOs above; `macrohashtable` belongs in CPrep.c.
- Adversarial review: the review called PrecompilerRead a TODO but the source had no comment: it now says why (Pro7's helpers inlined with no out-of-line copies; Pro7's text with them `static`/`static inline` 61.1%; the comma expression needed: 90.0/90.5 as two statements). AddressHashVal's union tried against Pro7's byte casts (48.4), a byte pointer (74.6), `u.v` for the address (96.8): only the union matches; a plausible spelling (original's shape). `flag = TRUE/FALSE` in SetupBuiltInArray. Held: boundary (Pro7's six inlined statics, rows in reverse), flags, data (statics static, pack(2)), header (every declaration used outside).
- Header round H2 (October 16): CTemplateClass.h includes only common.h; OLinkList is Pro7's; InlineAsm.h's placeholders are TODO names; CWPluginsPrivate.h/COSToolsCLT.h take Pro7's/the SDK's types. Resolved as TODO names.

### src/optimizer/IroPointerAnalysis.c: NOT SOLID
- Boundary: .text 0x471e90-0x49cf80, .data 0x5ac6d0-0x5ae898 (8-aligned), .bss 0x5e0250-0x5e032c. One object: IroPointerAnalysis.c `#include`s IroPointerAnalysisADTs.c (Pro7's arrangement; the ADT helpers' out-of-line copies are this unit's rows, and their asserts say "IroPointerAnalysisADTs.c" while the .c's own CError_FATALs say "IroPointerAnalysis.c", read at 0x5ac7b0 and 0x5ac6f8). The lowest row is Pro7's last function (PointerAnalysis_ParseFunctionModifiesSpecifier), so the file is reversed (deferred). 196 rows; Wii pairs 74 of them in module IroPointerAnalysis. Pro7's dead API (PointerAnalysis_TwoLinear/ENodePointerExprsMightAlias, IsENodePointerExprDefinite, IsVariableValueDefinite, LookupENodePointerExpr, LookupVariableInto*Exprs) is kept for its literals.
- Flags: pro6 `-O4,p -inline auto,deferred -sym on`; deferred because rows descend against Pro7's order. Two per-function `#pragma sym off` (ParseLocationSet, ADTs' LocationSets_Overlap) are documented TODOs from earlier rounds (Overlap needs it; ParseLocationSet 93.6 -> 97.8 with it).
- Functions: 194/196 exact. ParseLocationSet 97.8 and MatchPTF 88.9 are register-only (calls and references identical); nothing new tried on their bodies (ledger). Departures: Pro7's linkage restored (113 functions made `static` again, the 97 prototypes no use needed removed: Pro7's own forward-declaration shape); IRO_ASSERT_MSG/IRO_ASSERT_T are documented workarounds for assert texts that differ from the expression's spelling (needed shape, TODO); UpdatePTFDomain is a stub for 2.6's dead `home != NULL` assert (Pro7 has an empty placeholder; TODO). Not reviewed function by function against Pro7 in this round (6500 lines; the bodies are exact, so behaviour is pinned by the bytes, but spellings are not audited).
- Behaviour: an equivalent compiler built from this tree compiles a pointer-analysis test (`#pragma opt_pointer_analysis on`) identically to the original at -O3, -O4,p, -O4,s and with -opt unroll. Bugs of ours fixed: the 13 analysis statics and the unknown block/location set (`stDummyMemoryBlock`, `stUnknownMb`, `stDummyLocationSet`, `stUnknownLs`) were only `extern`s resolved by bindings, so the object had no .bss and 40 bytes less .data; EvalProcAction's break counter was a global `data_005dc1f8` (Pro7's function-local `static int userBreakCounter`). Bugs in the original: none found.
- Names: rows Pro7's or reviewed; `ObjectSet_AddUnique` renamed to Wii's `ObjectSet_Add` (Pro7 ObjectSet_sub_4867D0; Wii has a public ObjectSet_Add in this module; name_review updated). Invented: the ADT helpers Pro7 leaves as `*_sub_XXXX` placeholders (Stack_Push/Pop/FindByKey, ObjectSet_Find/SimpleAdd/AddSet/RemoveSet, ObjectSets_Equal, ExtendedParamSet_*, PTFList_Find/SimpleAdd/Add, ...), now listed in one TODO in IroPointerAnalysisADTs.c; `stTabs`' size (TODO).
- Types: Pro7's ADTs at 2-byte packing. Placeholder fields remain from Pro7: the Params structs' `x10`, `x14`, `x15`, `x18`, `x19`, `x1C`, `xC`, `x0`, PartialTransferFunction.x10, EvalProcActionStatic.x18/x19, and parameters `unk`/`unkflag`.
- Data: .bss now byte-for-byte placed (every static at its original address: stExceptionFNode 0x5e0254, stEvalProcActionParams 0x5e0258 (0x1A), ..., stUnknownPTF 0x5e0324, stExtendedParamNum 0x5e0328; `stIndentationLevel` + `stTabs[0x2A]` fill 0x5e02f6-0x5e0324, unreferenced) with Pro7's declaration order, stEvalProcActionParams declared between stMaxPassCount and stExceptionFNode where Pro7's TODO puts it, and a trailing `#pragma pack(2)` (TODO, as CompilerTools) for the 2-byte .bss alignment. Their 15 bindings deleted (exact functions place them). .data: the first 40 bytes now match (the dummies); from 0x5ad2ec on, our literal stream is 24 bytes early: 2.6 emits "!changed" and "set != NULL" first just before FindMainEntryPoint's "function != NULL", ours earlier/elsewhere (pre-existing; the audit's "60 bytes short"). That shift makes ~70 exact functions' literal references and ~100 .data items differ in the relink.
- Header: IroPointerAnalysis.h declares only what other units use (Setup/Cleanup/PragmaMode: CPrep; IRO_AnalyzePointers: IrOptimizer; IsLinearNodePointerExprDefinite, LookupLinearNodePointerExpr, GetFunctionKills/Dependencies: IroEval, IroTransform, IroCSE, IroVars, IROUseDef, IroRangePropagation; the three Parse*Specifier: CParser). Seven declarations of dead functions removed (no function anywhere changed). File top: `CERROR_FILE` defines removed from IroPointerAnalysisADTs.c (`__FILE__` gives the same names), mid-file `#include <time.h>` moved up, stale "ported from Ninji" and "not yet checked" comments (on exact functions) removed, a no-op `#define CInt64_Neg CInt64_Neg` removed.
- Headers relied on: see the shared list under IroUnrollLoop.c; also CExpr2.h, CDecl.h, CScope.h, CPrepTokenizer.h (the specifier parsers), IroJump.h, IroEval.h, <setjmp.h>.
- Open: the .data literal order (24 bytes from 0x5ad2ec): "!changed" (KillAllAliasingExtParamLocsAction's 2.6 assert, which Pro7 lacks) and "set != NULL" (EvaluatePartialAbsolute/AddToInitialPointsToFunc) are created in the original between GetDefiniteObjectOfExtendedParamLoc's "resultField != NULL" and IRO_AnalyzePointers' "function != NULL", i.e. while generating the functions defined between IRO_AnalyzePointers and FindGlobalObjectAction (in reverse order: IsVariableValueDefinite, IsENodePointerExprDefinite, IsLinearNodePointerExprDefinite, TwoENode/TwoLinearNodePointerExprsMightAlias, RemoveRestrictedExtendedParams*), all dead in 2.6 but IsLinear...; so one of 2.6's dead functions there differs from Pro7's (it asserts `!changed` after GetActualLocsOfExtendedParam-style calls and reaches a `set != NULL` assert). Wii 1.0RC1 keeps TwoLinearNodePointerExprsMightAlias live in Pro7's shape (two RemoveRestrictedExtendedParams, two ExpandLocationSetSetToActuals, no `changed`) and has no assert strings, so it cannot say which; ParseLocationSet and MatchPTF (register-only); Pro7's placeholder fields and the invented ADT names; a statement-level audit of the 190 exact bodies against Pro7.

### src/optimizer/IroTransform.c: SOLID WITH TODOS
- Boundary: .text 0x4578d0-0x45dc70; .bss 0x5dffb8 (AssignmentOp, ComplementaryOpLogical, ComplementaryOp, 3 x 77 bytes); .data from 0x5aa35c (directly after IroSubable's 12-byte jump table at 0x5aa350). Rows run in reverse of Pro7's file (IRO_InitializeAssignmentOpArray highest, IRO_TransformTree lowest); Wii puts the named ones in IroTransform. 2.6 adds InsertTypeConversion (0x45b720, between DoTransformations23 and 22). Pro7's GetTypeSize (unused even in Pro7) and PullOutPostOps (inlined into IRO_TransformTree) have no rows.
- Flags: pro6 `-O4,p -inline auto,deferred -sym on`; deferred (rows descend against Pro7's order).
- Functions: 29/31 exact (DoTransformations24 98.5, DoTransformations11 96.0: register/reload only, references identical). Now Pro7's verbatim text (exact as is): CombineConstants, AddAddend, ReconcileAssignments, DoTransformations12/13/21/22/25, switchFatherLeft, ReplaceExprWithMonaminLeft/Right, the three IRO_Initialize*Array (they were raw numbers 0..76; Pro7's enumerator names give the same stores), TransformExprTree/1/2 (with Pro7's ENODE_CASE_* labels and `nullcheck` members, replacing IDA-style text). Departures, each tried against Pro7's spelling: RemoveRedundantMonadicOp `nd->type = nd2->type = IROLinearNop` (needed shape; Pro7's two statements and the other chain order 98.5/96.9) and 2.6's `case IT_WCHAR_T` (2.6 change: the jump table); FoldConstantsinAssociativeExprs drops `changed = 1` after `*expr = *expr->data.diadic.right` (2.6 change: exact only without it) and keeps the sum in a `CInt64 sum` (needed shape: 93.0 without); TransformExprNode swaps through a `left` temporary (needed: Pro7's assignments in the conditions 84.6); RemoveRedundantBitOperations `if (... != OperandObject) return 0; return 1;` for Pro7's `return ==` (catalogued shape; 95.3); TransformMonadicSelfAssignmentToDiadicSelfAssignment `varExpr = incExpr = NULL` (needed; 99.5); FrontendTransformSelfAssignmentToAssignment keeps the parameter and the walker apart (`expr`, `nd`: Pro7's reuse 78.0) and 2.6's assert lines 3645/3647/3652; DoTransformations' `right` local and 2.6's `(nd5 = ...) && (If || IfNot)` grouping (Pro7's text has the parenthesis misplaced; 79-90 otherwise); DoTransformations23's chained `changed = isCompare = 0` and 2.6's InsertTypeConversion calls; DoTransformations11's `nd->stmt &&` test and bitfield branch (2.6 changes). No fakematches.
- Behaviour: see IroPointerAnalysis (equivalent compiler identical at -O3/-O4,p/-O4,s). Bugs of ours: none found this round (the earlier stand-in helpers are gone).
- Names: rows Pro7's; `fn_0045b720` renamed `InsertTypeConversion` (invented, TODO: no Pro7 or Wii counterpart; it is IROUseDef's IRO_AddTypeConversion without the list). COMMONs ExprType, IndirectRef, FirstTime, OperandConst, OperandObject, FirstAddend, LastAddend are Pro7's globals.
- Types: Pro7's. `IRO_IsPow2`'s `int*` (IroUtil.h; Pro7 SInt32*) is why DoTransformations' `value` is `int`.
- Data: .bss placed at 0x5dffb8 with the arrays declared in reverse of Pro7's order (AssignmentOp lowest), now `static` with their three bindings deleted. .data: relink.py reports 3368 bytes against the range's 3364 (pre-existing): the range starts at 0x5aa35c, 4 mod 8, and relink.py pads the object's 8-aligned "IroTransform.c" literal section as if the object started 8-aligned; the unit does not link anyway (two inexact functions).
- Header: IroTransform.h is Pro7's six declarations, all used (IrOptimizer; IRO_TransformSelfAssignmentToAssignment by IROUseDef and IroExprRegeneration). File top: the 22-line prototype block cut to Pro7's single `static void DoDiadic(IROLinear*)`; Pro7's static linkage restored on every function; the helpers (IRO_SwitchChildren ... PickCommonsubAtRightRight, isOrderingOperator, RemoveUnreffed) moved back to Pro7's places (no code change); name-only and stale comments removed. `isOrderingOperator`, `RemoveUnreffed`, `switchFatherLeftMonadic` are `static inline` (2.6 has no out-of-line copies).
- Headers relied on: enode.h gained Pro7's ENODE_CASE_MONADIC/DIADIC_1/ASSIGN/DIADIC_ALL macros (macros only; nothing else changed); otherwise as listed under IroUnrollLoop.c, plus CIRTransform.h (CIRTrans_TransformOpAss), CMachine.h (CMach_CalcIntDiadic), IroCSE.h, IroPointerAnalysis.h.
- Open: DoTransformations24 (2.6 reloads `nd` before each store in the inlined ReplaceExprWith*Child copies; `(void)&nd` and the earlier rounds' attempts do not reproduce it) and DoTransformations11 (spill choice); the .data size question above; InsertTypeConversion's invented name.
- Header round H2 (October 16): the shared Iro* headers keep only TODO names (Optimizers/IR optimizer families); IRO_IsPow2's `int*` is 2.6's (blocked, ledger); CMachine.h, CIRTransform.h, IroCSE.h are reviewed. Resolved as TODO names.

### src/optimizer/IroExprRegeneration.c: SOLID WITH TODOS
- Boundary: .text 0x4a8e50-0x4ac980, .data 0x5b0080-0x5b01e4, .bss 0x5e0360-0x5e03b6. Rows reversed against Pro7's file (IRO_RegenerateExpressions, Pro7's last function, is the lowest row); Wii puts 12 in IroExprRegeneration. 2.6 adds CheckUnorderedRegionsForSideEffects, CheckUnorderedRegionForUses/CheckUnorderedRegionsForUses (called from HasSideEffectsBeforeUse/UsesAKilledVarBeforeUse, exact) and the worklist row IRO_FNodeWorkStack_Add (IroFlowgraph.h's inline, out of line here).
- Flags: pro6 `-O4,p -inline auto,deferred -sym on`; deferred (rows descend).
- Functions: 17/21 exact. GeneratePossibleCondAss 78.0, RebuildPossibleLogicalExpression 93.5, RebuildPossibleCondExpression 97.6, AddNodeAndSuccessorsRecursively 98.0: all register/stack-slot only (every call and reference identical). Departures: 2.6's additions above; `IRO_NumLinear++` post-increments (2.6 change, read in the disassembly); GeneratePossibleCondAss' head (`node1 = node2 = ...`, `succ`, `last`, `succ0`, `succnode`, `node2last`) is the IDA-shaped text, not Pro7's: Pro7's head verbatim gives the same 16-bit [esp+0x18] slot through MWCC's CSE (74.4, same size), so the slots alone do not prove named locals, but the original's `mov ebx, esi` is the `node1 = node2 =` chain only this head has (78.0); `succnode`, assigned in a condition and never read, is the original's [esp+0x14] value that 2.6 copies into r29's slot (`(r29 = succnode)` would be the reading; 76.6), so it is the original's local (needed shape, with an unread assignment that a copy-propagated `r29 = succnode` explains); RebuildCondExpressions' `(nd = fnode->first) &&` and RebuildPossibleReturnCondExpression's `(last = fnode->last)` (needed: Pro7's text 86.5/87.3); CheckForTopLevelExpressions indexes IRO_NodeTable twice (needed: 93.5 with Pro7's `fnode`); RebuildPossibleCondExpression's `(void)0;` arm is Pro7's text verbatim; `leftAss->u.diadic.right` for Pro7's `u.label.x4` (same offset; an assignment's right operand). No fakematches.
- Behaviour: as above (equivalent compiler identical). Removed an unused invented helper `IsConditionalBranch`.
- Names: Pro7's; `Regen_GetExprUses`/`Regen_GetExprKills` are Pro7's static GetExprUses/GetExprKills prefixed (row names are global; IroPropagate has its own), recorded in name_review. Locals keep Pro7's register names (r13..r31) where the bodies are Pro7's.
- Types: Pro7's; IRONodes and its worklist inlines are IroFlowgraph.h's (invented names there, TODO).
- Data: statics `scuai`/`scseai` now `static` (were global "for lifted code"; exact functions place them at 0x5e0360/0x5e038a) and their bindings deleted. Every .data/.bss item the exact functions reference is placed consistently. The relink cannot be judged while two inexact functions differ in size (16 bytes each).
- Header: IroExprRegeneration.h declares only IRO_RegenerateExpressions (IrOptimizer). File top: Pro7's three forward declarations; Pro7's `static` restored on 19 functions.
- Headers relied on: as under IroUnrollLoop.c, plus IroSubable.h, IroTransform.h, IroCSE.h, CExpr2.h, CodeGen.h, Registers.h.
- Open: the four register-level near-misses; GeneratePossibleCondAss' head is not Pro7's text (equivalent; Pro7's spelling scores lower, see the ledger).
- Header round H2 (October 16): the shared Iro* headers keep only TODO names; CExpr2.h is merged; CodeGen.h and Registers.h are DONE (WITH TODOS). Resolved as TODO names.

### src/optimizer/IroUnrollLoop.c: SOLID WITH TODOS
- Boundary: .text 0x4a00c0-0x4a4580, .data 0x5af540-0x5afd0c (8-aligned), no .bss. Rows are Pro7's file reversed (IRO_LoopUnroller highest, UpdateInductionIncrement lowest); Wii pairs 9 in IroUnrollLoop. Pro7's UnrollStandardLoop, BuildEarlyLoopExitTest, BuildLoopExitTest, NoOpBlock are inlined; IRO_LinearizeForLoopPostLoop, GenInitialAssignment, GenNewInduction are Pro7's dead functions (kept). Wii's later names IRO_UpdateInductionIncrement, IRO_BuildUnrolledBodyEntryTest, IRO_BuildNewFinalValue are renames of Pro7's (Pro7's kept).
- Flags: pro6 `-O4,p -inline auto,deferred -sym on`; deferred (rows descend).
- Functions: 21/22 exact; UnrollWhileLoop 89.1 (register only now: frame 0x98 as the original's and every address-taken slot in place; calls and references identical). Now Pro7's text: BuildOrigIterationCount (was IDA-shaped with invented names `context`, `statement`, a hand-expanded CInt64_SetLong and a hand-merged tail; Pro7's text with only its declarations reordered is exact), IsIterationCountConstant (verbatim). Departures: declaration orders (needed: Pro7's orders 72-89; UnrollWhileLoop's four address-taken locals are list, result1, result2, pattern, read from the frame (the first review's list, result2, result1 had the two counters swapped), and its 43 register locals are in the order `fn.py decl-order --swaps` found with that fixed (69.4 -> 89.1; no evidence beyond the code for any order of those)); IsDifferenceOfTermsConstant's `CInt64 diff` (needed: 90.7 without); LoopUnroll's `next` local, one counter `i` for both loops, `predcount++` before `prevpred = tmp` and `if (success) {}` for Pro7's early return (each needed: 96.2/98.3 without); UpdateInductionIncrement's `dup` where Pro7 reuses `ind_nd` (needed); UnrollForLoop's separate `labelnode` (needed); `shl.hi`/`val14.hi` read directly where Pro7 calls CTool_EndianReadWord32 (2.6 is little-endian; Pro7's spelling 63.3); CheckConstant keeps Pro7's text plus `i = cint64_zero` and an `else` (needed shape, commented: Pro7's spelling or either change alone is small enough to be auto-inlined into PatternMatchLoop, which then differs). No fakematches.
- Behaviour: equivalent compiler identical (above, including -opt unroll). Bugs: none found.
- Names: Pro7's. LoopList.xE keeps Pro7's placeholder (TODO: written 0, never read; it gives the 18-byte size the oalloc pushes).
- Types: Pro7's; LoopList at 2-byte packing (Pro7 mac68k). `int powval` follows IroUtil.h's `IRO_IsPow2(IROLinear*, int*)`.
- Data: the relink differs only in UnrollWhileLoop's .text (2136 bytes, re-run): .data is identical.
- Header: IroUnrollLoop.h now declares only IRO_LoopUnroller (IrOptimizer); IRO_IterateForLoopBody and IRO_LinearizeForLoopPostLoop (Pro7's header) have no user outside the unit and are removed. File top: Pro7's prototypes for functions used before their definition; the two for GenInitialAssignment/GenNewInduction (never called) removed.
- Headers relied on (shared by the four units): IroLinearForm.h (IROLinear, IROList, walkers; invented member names with TODOs: range, evaluated, funccall.callee, label.cond, unused1/2), IroFlowgraph.h (IRONode; invented names with TODOs: unused, copyOut, reachable, visited, referenced, marked; IRONodes and the IRO_FNodeWorkStack_* inlines are invented names), IroLoop.h (IROLoop: invented preheader/body/init/cond/lo/hi/count with TODOs; LoopList_First, InLoop, LoopNode, FirstInd), IroVars.h (VarRecord: invented unused/inductionState/noregister/usedAtCall/bitfield members, TODOs), IROUseDef.h (IROUse/IRODef: invented reachingDefs/reachingDefCount/useCount/global/noregister/definite/removable, TODOs), IroUtil.h (IRO_IsPow2 takes `int*` where Pro7 has `SInt32*`; Pro7's inline IRO_IsUnsignedType is absent, callers use CParser.h's is_unsigned directly), IroDump.h, IrOptimizer.h, BitVector.h, CInt64.h (inline CInt64_SetLong etc.; its `extern "C"` closed early, noted by Y_IRO), CompilerTools.h (oalloc, CTool_EndianReadWord32), CError.h, CParser.h (copts, is_unsigned, stbool/stsignedint), types.h, enode.h (ENODE_CASE_* added, above), objects.h, CFunc.h, CMachine.h, LoopDetection.h, ObjGen_PPC_EABI.h, CTemplateNew.h (the last three only for declarations IroUnrollLoop no longer obviously needs: not trimmed this round).
- Open: UnrollWhileLoop 89.1: 2.6 keeps the inlined BuildLoopExitTest's `LoopNode->last->type` byte in bl where ours spills it (register only). Its address-taken locals are settled from the frame: the PatternMatchLoop call's three `lea [esp+0x74]` at 0/4/8 bytes pushed put pattern at 0x74, result2 0x70, result1 0x6c, list 0x64, and MWCC lays them out ascending in declaration order.
- Adversarial review: attacked the UnrollWhileLoop frame reading, the GeneratePossibleCondAss "frame slot" locals, the IroPointerAnalysis .data cut, the headers' users, CheckConstant/LoopUnroll/IRO_FindLoops_Unroll against Pro7, FoldConstantsinAssociativeExprs and RemoveRedundantBitOperations against Pro7, the line-0 asserts (Pro7's own, in dead EvalENodeExpr), and format. Broke: the UnrollWhileLoop order had result1/result2 swapped (the three PatternMatchLoop pushes read at 0/4/8 bytes pushed give result1 below result2; compiled, the reviewer's order puts them at 0x6c/0x68/0x70 at the call where the original has 0x74/0x74/0x74); fixed to list, result1, result2, pattern, after which decl-order (the first review's run with the swapped order had found nothing) took the function 69.4 -> 89.1 with the original's frame, so the "8-byte larger frame / two missing locals" reading was wrong too. GeneratePossibleCondAss: the 16-bit and pointer slots do not prove named locals (Pro7's head yields the same slots by CSE, 74.4), but the `node1 = node2 =` chain and `r29` copied from the [esp+0x14] value do support the current head; the claim is weakened, not broken. The IroPointerAnalysis .data order narrowed to a dead 2.6 function between IRO_AnalyzePointers and FindGlobalObjectAction (Wii keeps TwoLinearNodePointerExprsMightAlias live in Pro7's shape, no `changed`). Held: the boundaries, flags, header users (every declaration has one outside), the IroTransform departures, clang-format. Grades of the three SOLID WITH TODOS units lowered to SOLID PENDING HEADERS: the headers they rely on carry the open problems their own entries list (invented IroLinearForm/IroFlowgraph/IroLoop/IroVars/IROUseDef members, IRO_IsPow2's `int*` for Pro7's `SInt32*`, CInt64.h's early `extern "C"` close). Exact functions everywhere: 4018 before and after.
- Header round H2 (October 16): IroLinearForm/IroFlowgraph/IroLoop/IroVars/IROUseDef keep TODO names; IRO_IsPow2's `int*` is 2.6's (blocked); CInt64.h's `extern "C"` closes at the end. Resolved as TODO names.

## Headers

Headers are reviewed by family, each by one owner, to the bar's items 5, 6 and 8: names and types Pro7's (or the SDK's,
Wii's), each declaration in the header of the unit defining it, no duplicates or private copies in sources, variables
`extern`, packing justified; no compiled byte anywhere changed.

### Parser library (the Z_DR2 units' headers): shared by ParserHelpers, IO and the other nine parser units
Fixed for these units:
- ParserErrors.h: added Pro7's `CLPStrNN` enum.
- CWPlugins.h:
  - the SDK's full CWPluginErrors enum, with `cwErrLastCommonError = 512`, which makes the
    enumerators 16-bit;
  - `kCurrentCompiledFile`;
  - `cwSystemPath`/`cwUserPath`.
- pref_structs.h: received `OptsCmdLineState_*` (from CLMain.h), `CmdLineStageMask_*` (from
  CLFileOps.h) and the new `CmdLineStage_*`, the PCmdLine state and stage values.
- parser.h: received Pragma, PRAGMA_FLAGS and PR_* (PR_* from OptimizerHelpers.h);
  OptimizerHelpers.h and StdTargetWarningHelpers-cc.h now include parser.h instead of ParserGlue's
  header.
- Targets.h: received `extern ParserTool* pTool`.
- CWParserPluginsPrivate.h: two return types.
- This group's own headers were rewritten.

Open problems for the headers' owners:
- Targets.h: the unused stand-ins `DispatchObject {opaque_state[614]}` and `DispatchTable`, plus
  parser.h's forward declarations of them.
- Option.h: `unsigned int Options_Parse(OptionList*, unsigned int)` and `unsigned char
  Options_DisplayHelp`, where Pro7 has `int`/`int` and `int`.
- pref_structs.h: the EABI panels' `unknownXX` fields, and placeholder enum names (Pro7's own).
- ClientGlue.h: `CLIO_STRINGS_EQUAL`/`STRINGS_EQUAL` cast macros (not Pro7's; no longer used here).
- CWSecretPluginCallbacks.h: a TODO on GetNamedPreferences' `Handle*` type.
- CWPlugins.h: `CWAccessPathType` is a UInt8 typedef standing for the SDK enum (2.6 reads a byte).
- include/libc/string.h: no `extern "C"` guards.
- Not reviewed here: Memory.h, MsDos.h, Generic.h, MacSpecs.h, Utils.h (`my_tolower(char)`),
  StringUtils.h, StringExtras.h, Arguments.h, CLMain.h, CLTarg.h, CLAccessPaths.h, CLFiles.h,
  CLPrefs.h, CLOverlays.h, CLSegs.h, CLErrors.h and CLDropinCallbacks_V10.h (shellContextType). No
  placeholder fields turned up in a grep of them.

- Adversarial review: attacked every non-Pro7 spelling with `fn.py variants` (ShowVersion,
  Opt_SetStage, SetupPragmas, AddFileToProject, AddAccessPath, UCBParserAddAccessPath), behaviour
  against IDA (PreParse, MidParse, PostParse, SetupPragmas, Opt_FindAndAddFile: constants,
  arguments, fields, literals agree), header users (every declaration has a user outside its
  unit), the StaticParserGlue flag change, the six COMMON renames and `linkerOutputFilename[264]`.
  Broke: IO's single `vother` (two locals, gc1's shape, also match: changed); ParserHelpers'
  unused `compiler/CPrep.h` (removed); CLParserCallbacks' undocumented `#pragma bool off` (needed,
  now listed). Held: every other needed shape (each Pro7/natural spelling 87-98%); the -O1 argument
  (MWCC emits one COFF .text section per function, aligned 16 under -O4,p and 1 under -O1; all 225
  -O4,p/-O2,p units in 2.6 start 16-aligned, 14 of 15 pro53 -O1 units do not, and both
  StaticParserGlue functions, at 0x40532e and 0x40533e, are off 16); the renames' hash order
  (lastStage 2341<2427<2472, definesHandle 3876<3915<3934, setLinkerOutputFilename 2488<2520<2522,
  setOutputDirectory 520<526<545, outputOrdering 3346<3544<3720, linkerOutputFilename
  2902<3022<3943; its lower neighbour error_message_buffer (2444) is itself out of order after
  pplist (2902), so it is a weak neighbour, but 3022 fits either way); 264 bytes (ReturnAddress,
  referenced at 0x5e9db0 and +8, is the next symbol, nothing references 0x5e9da8..af, an 8-group
  slot needs a multiple of 8, and no Wii COMMON of 8 bytes has a hash in 3022..3943 to fill a gap
  after a 256-byte array; Wii's later build has 256). Not settled: the UCB functions' C linkage
  (Wii mangles them as C++; the repo's C++ callback units all use `extern "C"`).

### Copy propagation and loops: CopyPropagation.h, AddPropagation.h, LoadDeletion.h, StrengthReduction.h, LoopDetection.h, UseDefChains.h, Alias.h
- Propagation.h was Pro7's CopyPropagation.h under another name: deleted, its users include CopyPropagation.h, which is
  Pro7's (`Propagation` with 2.6's `UInt32 mask`, 30 bytes at mac68k packing; `Candidate`, `PropInfo`; the
  `IsCandidateFunc` family). AddPropagation.h and StrengthReduction.h are Pro7's (their 1.x stand-ins `CMNode`,
  `CMBody`, `RegisterRewriteEntry`, `ScaleTarget*` deleted; `BivInit` is StrengthReduction.c's static struct).
- LoopDetection.h has Pro7's `BlockList`, `InstrList`, `LoopBound`, `BasicInductionVar` (26 bytes: `push 0x1a`) and
  `InductionVar`; `Loop.blocks` is a `BlockList*`.
- Names now Pro7's (and in COMMON hash order): `firstcandidateinblock`, `ncandidatesinblock`, `propinfo`,
  `number_of_candidates`, `is_candidate`, `propagatestouse`, `propagateandfinish`; Pro7's COMMONs are defined in
  CopyPropagation.c and AddPropagation.c, where Pro7 defines them.
- 2.6's types where they differ from Pro7 (the code decides): `number_of_candidates` and `Loop.bodySize` `SInt32`.
- TODOs: `PropInfo`'s `gen`/`kill`/`in`/`out`, `UseDefInfo`'s eight vectors, `BasicInductionVar`/`InductionVar`
  members, `Loop`'s flags, `LiveRegs` (Peephole) and `RegValue.copyreg` (ValueNumbering): Pro7 has only offset
  placeholders there (`vec0`, `x4D`), so the names are descriptive inventions. findnaturalloop walks a `PCLink` list and
  a `BlockList` with one variable (a cast, TODO). `make_addressable`'s `unsigned int offset` (Pro7 `SInt32`) untried.

### Back-end core: PCode.h, PCodeInfo.h, PCodeUtilities.h, Switch.h (SwitchInfo), CFunc.h, objects.h, ObjGen_PPC_EABI.h, TOC.h
- PCode.h includes PCodeInfo.h as Pro7's does; `PCOpKind` and the opcodes live there; each declaration once
  (`change_opcode`, `change_num_operands`, `vformatpcode`, `formatoperands`); Pro7's signatures (`appendpcode`,
  `emitpcode` void, `makepcode(short, ...)`, `pccomputeoffsets` int) and PCode.c's bodies Pro7's; the 1.x `PCOp_FPR` ...
  defines and the `PC_SETCOUNT`/`PC_SETOPCODE` macros gone; flag 0x80000 is `fPCodeFlag80000`.
- PCodeUtilities.h is Pro7's, without its private copies of Operands' functions and `current_statement`; private
  externs removed from PCodeUtilities.c, InlineAsmPPC.c, CodeGen.c, AddPropagation.c.
- `SwitchInfo` is in Switch.h; `functionbodyoffset` is CFunc.h's `SInt32`; objects.h has Pro7's `toc` and `varptr`,
  `switchtable.data` `char*`, no padding members, section kinds named.
- ObjGen_PPC_EABI.h is Pro7's ObjGen API with Pro7's parameter names plus a 2.6 (EABI) group; unit-only declarations and
  types moved into ObjGen_PPC_EABI.c. TOC.h lost three unit-only declarations.
- Placeholder members named from use (TODO: invented): `IGNode`, `MachineInfo`, `DGNode`, `DGSuccessor`, `EANode`,
  `ExceptionThing`; StackFrame.h's `Traceback` takes AIX `<sys/debug.h>`'s traceback-table names (it is that table).
- 2.6's types kept where Pro7's break code (commented): `pcloopweight`, `setpcodeflags`' parameter, `Object.sclass`.
- Left: `SecKind`'s `SK_1`/`SK_5`, SectionAttributeNode's unread `f08..f15`, ObjGen.c's invented static names,
  InlineAsmPPC's bare section 29 (`smclass GL`), CFunc.h's `LoopInfo` placeholders and front-end `data_*` externs,
  COpts' misnamed section members (CPrep.h).

### Driver records and plugin context: CLFiles.h, CLFileOps.h, CLTarg.h, CLSegs.h, CLAccessPaths.h, CLOverlays.h, CLDependencies.h, CWPluginsPrivate.h, CWPlugins.h, DropInCompilerLinker.h, option_system.h, Parameter.h
- Pro7's `File` (0x6a0 bytes, natural alignment: the old `pack(2)` and its padding members were alignment), `Files`,
  `VFile`, `Target` (with `BuildInfo info`, `linkage`), `Segment(s)`, `Path(s)`, `OvlAddr`/`Overlay`/`OvlGroup`/
  `Overlays`, `InclFile`/`Incls`/`Deps`, each with Pro7's members (about 120 renames; the repo's `sysPaths` and
  `userPaths` were swapped). CLFiles.c, CLTarg.c, CLSegs.c, CLOverlays.c and CLAccessPaths.c are Pro7's text.
- The plugin context is Pro7's `CWPluginPrivateContext`, with `CWCompilerLinkerContext` and `CWParserContext` derived
  from it in C++ (byte-identical); SDK types from `../MWCC_Pro7/sdk_hdrs`; its eight padding members gone.
- Parameter.c uses the generated tables' `*_T` types; the stand-ins `MaskEntry`, `Setting`, `NumParm` ... are gone.
- Each header exports Pro7's API; unit-internal functions are `static`; private externs moved to headers.
- Left: CLDropinCallbacks_V10.h's stand-ins for the SDK callback types (`DropinFileCallback`, `CallbackRecord` ...: a
  family of their own), `StorageHandle*` for Pro7's `Handle`, the base context's `void** callbacks`,
  `FileProcessingInfo` (CPrec.c), CLDependencies.c's body, two TODO casts (UCBGetFileText, CWGetMainFileText).

### Types and templates: types.h, templates.h, CTemplateTools.h, CTemplateFunc.h, CTemplateNew.h, CTemplateClass.h, objects.h (templates)
- `Type.type` and every Type-prefix struct's kind field are Pro7's signed `TypeType` (`TYPEILLEGAL = -1`); about 136
  `(SInt8)`/`(signed char)`/`(char)` casts on `->type` in 23 units are gone. The functions it cost were fixed by typing
  their kind locals `TypeType` (set_out_param_displ, CInit_InitializeData, IroTransform's
  TransformMonadicSelfAssignmentToDiadicSelfAssignment) or by a real bug: CInline_ExportCheck compared `datatype` with
  `TYPEFUNC`/`TYPECLASS` where it meant `DALIAS`/`DINLINEFUNC`. Kept: three `SInt8` kind locals (CInline 608/628,
  CMachine 1572), with which CInline_FoldConst and CMach_GetFunctionResultClass match.
- types.h is Pro7's order and shape (`TypeMemberPointer.ty1/ty2`, `TypeTemplDep.dtype`, `TypeClass.action`, Pro7's
  function and class flag enums, `ClassFriend`, `TypeObjCID`, `TypeList`); the `st*` externs are CMachine.h's.
- templates.h holds every template type, Pro7's, at pack(2): `TemplArg`, `DeduceInfo`, `TypeDeduce`, `TemplClass`,
  `TemplClassInst`, `TemplateFunction`, `TemplFuncInstance`, `TemplPartialSpec`, `TemplStack`, `TemplateAction` ...
  (the repo's `CTStateElem`, `TemplateMatchState`, `TemplateContext`, `TypeClassTemplate`, `TypeClassExt800` and a
  dozen stand-ins for `TemplateAction`'s arms). objects.h has Pro7's `ObjectTemplated` and `TemplFuncInstance* inst`;
  enode.h Pro7's `emember` (`EMemberInfo`); `CError_LockErrorPos` takes `BufferedToken**`.
- TODOs: Pro7's placeholder members (x14 ...) named from use; `TemplateScopeState` is Pro7's `DeclFucker`; `Type`'s
  one-member `array[0]` stand-in for CExpr_BuiltInComputeType (any cast there makes its recursive self-inline one
  level shallower); CTemplClass_CopyEnum's `(TypeEnum*)TYPE(...)` cast; CTemplateClass.h keeps its unit's own
  prototypes, which Intrinsics.c's rlwnm_intrinsic needs (it matches only within a window of declaration counts).

### IR optimizer: IroLinearForm.h, IroFlowgraph.h, IroLoop.h, IroVars.h, IroUtil.h, IroCSE.h, IroPropagate.h, IROUseDef.h, IroEval.h, IroTransform.h, IroPointerAnalysis.h, IroUnrollLoop.h, BitVector.h
- Pro7's layouts and types: `IROLinear` (`IROLinearType`, the `IROLF_*` enum, Pro7's `u.label {label, cond}`,
  `ispascal`, `evaluated`, `ctch`), Pro7's loop flag enums and `IROLoopInd.fnode`/`addNode`, `IROExpr.couldError`/
  `notSubable`, `IROAssign.useCount`, `IRODef.removable`, `VarRecord.bitfieldType`; Pro7's `IRONodes` and its inline
  work-stack helpers in IroFlowgraph.h (the original's out-of-line copy asserts "IroFlowgraph.h" line 93, Pro7's line;
  Wii names it `IRO_FNodeWorkStack_Add`); IroBitVect.h merged into Pro7's single BitVector.h; Pro7's
  `NullCheckListNode` (IroLinearForm compared the same offset through a wrong union member). Globals are defined in
  Pro7's units (IroVars.c, IroTransform.c, IroLoop.c, LoopDetection.c) and named Pro7's (`NullCheckList`, `CurrStmt`,
  `PrevStmt`); headers declare only the cross-unit API.
- 2.6's types where Pro7's change code (commented): `IROLinear.flags` `UInt32`, `IRO_IsPow2(int*)`.
- TODOs: members Pro7 itself only has as `xNN` (named from use: `iterCount`, `finalStoreVal`, `marked`, ...);
  IroPointerAnalysis.c's private structs; IroLinearForm.c's own gc1 helper names (that unit's review); `IRO_Depends`,
  `IRO_VarTerms`, `InLoop_Exits` and a few others are declared, bound and defined nowhere.

### Front-end core: CFunc.h, enode.h, CException.h + Exceptions.h, CPrep.h + tokens.h, Registers.h, RegisterInfo.h, SFPE_PPC_EABI.h, common.h
- CFunc.h is Pro7's: `CLabel.stmt` (the invented `target.stmt`/`target.node` union gone; the IR optimizer casts, as
  Pro7 does), the `StatementType` enum, `InitExpr`, `CtorChain` (a byte-sized `CtorChainType` at pack(2)), `DeclThing`,
  Pro7's extern list; CFunc's globals and statics have Pro7's names (from the .bss layout and COMMON hash order:
  `temp_argument_name`, `in_func_arglist`, `sourcefilepath`, `sourceoffset`, `symdeclend`, `ctor_chain`, ...) and Pro7's
  definitions live in CFunc.c (`elipsis`, `locals`, `arguments`, `oldstyle`, `curstmtvalue`, `Labels` ...).
- CException is Pro7's `ExceptionAction` (in Exceptions.h, Pro7's place), `prev`/`type` and kind-specific union
  members (about 300 accesses remapped by action kind; the old member views had been used inconsistently).
- CPrep.h: `COpts` is 2-byte packed without hand padding (sizeof 274 unchanged), its members named from 2.6's pragma
  table (`constSection`/`sdataSection`/`sconstSection`, signed `rel109_offset`, Pro7's `appltype`/`headtype`/
  `texttype`); `CPrepFileInfo`, Pro7's `TStreamElement` and `FileOffsetInfo` in Pro7's tokens.h; CPrep's private types
  in CPrep.c. `VarInfo` is objects.h's, the `ALLOC_*` macros RegisterInfo.h's; `RegInfo` embeds `ObjRegInfo`.
- common.h lost 65 unused typedefs and 15 dead forward declarations.
- TODOs: `LoopInfo`'s Pro7 placeholders; CException's raw `slots[6]`/`operands[6]` views (CInline walks the action
  words generically); COpts' `full_dwarf`, `no_comment_records`, `dwarf_siblings`, `dwarf_relocations` (invented);
  CFunc.c itself is still gc1 text (its review).

### Driver low level: Memory.h, oslib.h, CLIO.h, cmdline.h, MemUtils.h, StringUtils.h, win32.h, the COS_* prototypes
- Pro7's `typedef char* Ptr; typedef Ptr* Handle;` (common.h, Pro7's place) replaces the `StorageHandle` struct
  everywhere, compiler headers included (`h->data` is `*h`; Memory.c keeps Pro7's private `HandleStructure`); the COS_*
  functions have Pro7's types and parameter names, and `COS_FileNew` returns `err` (2.6's `ax`: Pro7's body).
- win32.h is WINDEF/WINNT/WINBASE's typedefs (the Ghidra `_X`/`P_X` forms, a stray IMAGE_SCN enum and tool comments
  gone; it no longer includes compiler/types.h); CLIO.h lost the invented `CLIO_OutOfMemory()` macro; cmdline.h lost 15
  dead stand-in tags; CompilerMapping.h's `Lang_*` names replace literals.
- Left: `COS_FileGetFSSpecInfo`'s `UInt16*`/`void*` and `COS_FileGetPathName`'s `CPrepFileInfo*` (Pro7's types need
  the compiler callers changed: MWCC rejects the pointer mismatches clang only warns about); the COS_* still live in
  CompilerTools.h (Pro7: cos.h); cmdline.h's live stand-in typedefs (the plugin SDK family); `OptsCmdLineState_0..3`
  (Pro7's placeholders, meanings in a comment).

### Optimizers: BitVectors.h, CodeMotion.h, ConstantPropagation.h, COptimizer.h, GlobalOptimizer.h, LiveVariables.h, LoopOptimization.h, ValueNumbering.h, VectorArraysToRegs.h, IroDump.h, IroEmptyLoop.h, IroExprRegeneration.h, IroJump.h, IroMalloc.h, IrOptimizer.h, IroRangePropagation.h, IroSubable.h

- BitVectors.h: DONE WITH TODOS. Dropped the `const` on bitvectorcopy/bitvectorunion's `src` and
  bitvectorintersectionisempty's `a`/`b`. Pro7 has no `const` and there is no evidence for it; header and BitVectors.c
  changed, still exact. bitvectorchanged now returns Pro7's `int` instead of `SInt32` (exact). The `SInt32` lengths
  stay, with a comment: each of Pro7's `int` lengths alone costs an exact function (LoopDetection's computedominators
  for copy/changed/initialize/intersect, LiveVariables' computeglobalinout for copy/union; numbers in the ledger's
  blocked section). bitvectorintersectionisempty keeps Pro7's `int` (SInt32 changes nothing). bitvectorcreate_range,
  bitvectorinitialize_range and the three compressed-vector functions are Wii's public names (commented so). Left: the
  invented names of the 2.6/Wii-era types and their members (BitVectorRange, CompressedBitRun, CompressedBitVector)
  and the inline range helpers, TODO-marked (no source names them). Pro7's dead difference/complement/count/isempty
  stay undeclared: nothing calls them, and Wii has no public for them (bitvectorcount is defined, dead, for its
  `nbits` table).
- CodeMotion.h: DONE. Put back Pro7's `extern int unswitchedinvariantcode;`. Pro7 declares it here and Wii 1.0RC1 has
  it as a public COMMON, so it follows the "Pro7 API + Wii public" rule. The CodeMotion.c review had removed it as used
  only by its own unit.
- ConstantPropagation.h: DONE. Same as Pro7. Both declarations are used (GlobalOptimizer).
- COptimizer.h: DONE. Pro7's declarations minus what 2.6 lacks: basicblocks, RegAllocMask and MarkRegAllocMask belong
  to the dead statement-level CSE, which 2.6 does not have. copt_isleaffunction is used only by its unit but stays: Pro7
  declares it and Wii has the public. Pro7's COptBlock/COptCSE/COptCSEList/COptBlockList layouts are not added
  because 2.6 keeps only pointers to them (COptimizer's statics), so their layouts cannot be checked. The
  `typedef struct COptBlock/COptCSE` lines moved out of common.h (only COptimizer.c uses them) and now sit next to
  `COptCSEList` at COptimizer.c's top, with a comment.
- GlobalOptimizer.h: DONE. Same as Pro7.
- LiveVariables.h: DONE. Pro7's LiveInfo.h under the unit's name, as the LiveVariables review chose; the file was not
  renamed because nothing in 2.6 shows the header's name. Dropped the `#include "compiler/Registers.h"`: `RegClass`,
  `PCode` and `Object` all come from common.h. All three includers still build, and clang_check shows no new errors.
- LoopOptimization.h: DONE. Same as Pro7. pccomputepredecessors1 is used by CodeMotion; the rest by GlobalOptimizer.
- ValueNumbering.h: DONE. Same as Pro7 (killmemory and nextvaluenumber are used by Alias.c).
- VectorArraysToRegs.h: DONE. Same as Pro7.
- IroDump.h: DONE. Put back four of Pro7's declarations that Wii 1.0RC1 has as publics: IRO_DumpIntTree,
  IRO_DumpFlowgraph, IRO_DumpNode and IRO_SpellType. A comment says why, since no other 2.6 unit calls them (the weak
  point: Wii's publics for three of them are in other modules, IroLinearForm, IroFlowgraph and IroType, where Wii moved
  them; 2.6's IroDump.c defines them, and 2.6's binary keeps only IRO_DumpFlowgraph, so the declarations are Pro7's API
  rather than proven 2.6 exports). IroDump.c's own prototypes for IRO_DumpFlowgraph and IRO_SpellType (added when the
  header was trimmed) are now duplicates of the header's and are removed. Pro7's other dropped declarations
  (IRO_NodeName, DumpLinearList, LogForFunction, DumpAssignments, DumpVars, DumpDf, DumpAddr) stay out: they have no
  Wii public and no other user. Dropped `#include "compiler/enode.h"`: it was needed only for Pro7's
  IRO_NodeName(ENodeType).
- IroEmptyLoop.h, IroExprRegeneration.h, IroJump.h, IroRangePropagation.h: DONE. Each is Pro7's text and every
  declaration has a user in another unit.
- IroMalloc.h: DONE. Pro7's minus IRO_msize/realloc/calloc/pool_free_all: no other user and no Wii public.
  `<stddef.h>` is included for size_t.
- IrOptimizer.h: DONE. Pro7's minus DoScalarize, EarlyReturn, IRO_CPFirstTime and IRO_Cleanup, which have no other user
  and no Wii public. Pro7's forward IRO* typedefs are in common.h, as everywhere in this repo.
- IroSubable.h: DONE. Pro7's minus IRO_IsVectorTempCandidate: no other user, and Wii's public of that name is a later
  function-pointer variable in IrOptimizer, not this function.
- Includes of the family's headers by other units:
  - Removed includes that used nothing from the header:
    - CodeGen.c: LoopOptimization.h and IroJump.h.
    - IroPointerAnalysis.c: IroDump.h, IroJump.h and IrOptimizer.h. IroPointerAnalysisADTs.c, which it includes,
      uses none of them either.
    - IroTransform.c: IroJump.h.
    - IroLinearForm.c: IrOptimizer.h.
  - Added `#include "compiler/BitVectors.h"` to the five units that call its functions directly but got them only
    through UseDefChains.h: AddPropagation, CopyPropagation, LoopDetection, StrengthReduction and UseDefChains. Pro7
    includes it in LoopDetection and StrengthReduction.
  - No private `extern` copies of this family's declarations exist in any source.
- Items left on the reviewed optimizer headers:
  - IroLinearForm.h: resolved by an earlier round. `branch`, `f1e` and `pad20` are gone. `args3` is Pro7's own name.
    `swtch.cond`, `label.cond`, `range`, `evaluated`, `funccall.callee` and `ctch.unused1/2` are Pro7 placeholders
    renamed by meaning, each with a TODO. I checked the uses: `cond` is the If/IfNot/Switch condition everywhere.
  - IroFlowgraph.h `x26`: resolved, now `unused` (TODO: only StartNode writes it).
  - IroLoop.h `x28`/`x30`: resolved, now `iterCount`/`finalStoreVal` (TODO, from IroEmptyLoop's dump labels).
  - IroVars.h `inductionState`/`x6`: resolved as TODO-marked names (`x6` is now `unused`). `x1A` is now typed
    `TypeBitfield*`.
  - IROUseDef.h's invented members (reachingDefs, reachingDefCount, useCount, global, noregister, definite,
    removable): TODO-marked, and each matches its uses. Nothing gives real names, so they stay TODO.
  - IroUtil.h `IRO_IsPow2(IROLinear*, int*)`: re-tested and blocked. With Pro7's `SInt32*` and its callers' `SInt32`
    locals, DoTransformations drops to 88.3, UpdateUseForOtherSelfAssignment to 96.7 and BuildOrigIterationCount to
    97.1. 2.6's type is `int`. The header comment now gives the numbers, and the change is in the ledger's blocked
    section.
  - CInt64.h's early `extern "C"` close: resolved by an earlier round. The block now closes at the end of the file.
  - Alias.h `vec24`, `AliasType0/1/2`: resolved by an earlier round. They are now `membervec` and
    `AliasType_Object`/`_ObjectPart`/`_Set`, named by meaning with a TODO.
  - docs/names-2.6.md: the "Deferred units" table's repo column was a stale snapshot. I recomputed it from
    config/sources.json and `defer_codegen` pragmas. 32 rows changed, IroLinearForm among them, and no row is still
    marked "(to change)". The evidence.py sweep, if re-run, produces the same column.
- Units re-graded (review-2.6.md entries that are PENDING HEADERS or have an Open item naming these headers):
  - GlobalOptimizer.c: resolved. Its open item was the unreviewed pass headers, which this review covers. The
    UseDefChains, LoopDetection, Peephole, CopyPropagation and AddPropagation headers were already reviewed.
  - COptimizer.c: the COptimizer.h part is resolved. Still open in other families: the CException, ENode and CLabel
    stand-ins.
  - LiveVariables.c: the LiveVariables.h and BitVectors.h parts are resolved. Still open: PCode.h, types.h and
    CMachine.h, which belong to other families.
  - IroDump.c: the IroDump.h part is resolved (the four Wii publics are back; its entry's "IroDump.c gained
    prototypes for IRO_DumpFlowgraph and IRO_SpellType" is now stale: the header declares them and the private copies
    are gone). Still open: CompilerTools.h's COS_FileGetPathName types, which belong to another family.
  - IroSubable.c: resolved. Its only item was IroLinearForm.h's placeholders, and those are now Pro7 names or
    TODO-named.
  - IroLinearForm.c, IroRangePropagation.c: "header review only". Their optimizer headers are now all reviewed, with
    TODO names only.
  - IROUseDef.c: header items resolved to TODO names. Still open: IT_17 in types.h, which is not this family's.
  - IroEmptyLoop.c: its IroLoop.h and IroVars.h items are resolved as TODO names. Still open: its own register-shape
    TODO.
  - CodeMotion.c, ConstantPropagation.c: the Alias.h AliasType0..2 item is resolved (TODO names). Still open in other
    families: PCode.h's fPCodeFlag20000/40000 and LoopDetection.h's invented Loop members. Their own near-misses
    remain.
  - IroPropagate.c, IroTransform.c, IroExprRegeneration.c, IroUnrollLoop.c: every optimizer-header item they list is
    resolved or TODO-named (IRO_IsPow2's `int*` is shown to be 2.6's). Their near-misses remain.
  - IrOptimizer.c (IAOpnd_3 in InlineAsmPPC.h) and IroJump.c (the CException stand-in) are unchanged: their items
    belong to other families.
- Renames: none of identifiers (the adversary added none either). Moves: `typedef struct COptBlock COptBlock;` and
  `typedef struct COptCSE COptCSE;` from include/compiler/common.h to src/optimizer/COptimizer.c. Type changes: bitvectorchanged now returns `int`
  (was `SInt32`); bitvectorcopy/bitvectorunion `src` and bitvectorintersectionisempty `a`/`b` lost `const`.
- Checks:
  - Exact functions: 4019/4077 before and after, with no function changed either way, not even in similarity.
  - The relink: all 219 Matching sources link (2849/4080 functions, 829108 bytes).
  - `tools/layout.py GC_2_6` rewrote symbols.txt. One unit's local literal labels near .data 0x5bd9c0 were renumbered
    (`@459`... became `@457`...) because of the include changes; the bytes are identical.
  - `clang_check`: 82 errors, none new against the baseline (I copied build/clang_errors.txt from the main tree).
  - `validate.py GC_2_6`: clean.
  - `update_assert_lines.py GC_2_6`: 0 lines to change.
  - After the adversary's edits (BitVectors.h comments, IroDump.c's two prototypes): exact 4019/4077, no function
    changed either way; layout.py leaves symbols.txt unchanged; relink 219/219 Matching sources (2849/4080 functions,
    829108 bytes); clang_check 82, none new; validate clean; update_assert_lines 0; clang-format clean.
- Adversarial review: attacked every claim with a re-run or a test. Held: the five BitVectors lengths, re-tested one
  at a time (each as `int` costs computedominators 92.7/96.0/95.7/45.2 or computeglobalinout 91.0, so `SInt32` is
  2.6's; bitvectorintersectionisempty's `int` is neutral, Pro7's kept); every family declaration against Pro7's
  headers (the nine "same as Pro7" headers are, declaration for declaration); every Wii-public claim (`wii.py`:
  unswitchedinvariantcode, copt_isleaffunction, SetVarUsage are publics; IRO_Cleanup, DoScalarize, EarlyReturn,
  IRO_CPFirstTime, IRO_msize/realloc/calloc/pool_free_all, bitvectordifference/complement/count/isempty are not;
  IRO_IsVectorTempCandidate is IrOptimizer's .data); every remaining declaration has a user outside its unit except
  the four Pro7+Wii ones in IroDump.h, unswitchedinvariantcode and copt_isleaffunction (the stated rule); every family
  unit includes its own header; no private `extern` or prototype copies of the family's declarations in any source
  (after the fix below) and no duplicate declarations in other headers; the include removals and additions (no
  family header is included for nothing; clang_check and the compare unchanged); the family's structs (LiveInfo,
  BitVectorRange, CompressedBitRun/Vector) are reached only by exact functions, so their offsets are checked; the
  already-reviewed headers' items (IroLinearForm.h's union, IroFlowgraph/IroLoop/IroVars/IROUseDef TODO names,
  CInt64.h's linkage block, Alias.h) are as the bullet above says, and IRO_IsPow2's `int*` is backed by Pro7's
  `SInt32` callers' locals failing in three units; names-2.6.md's deferred column agrees with evidence.py's own rule.
  Broke: IroDump.c kept two private prototypes the restored header now duplicates (removed); the BitVectors comment
  credited one function with the whole cost and left the range functions' Wii names unmarked (rewritten). Noted, not
  this family's: IroVars.c's private IndirectRecord keeps Pro7's placeholder `x26` (the IROLinear the bit-field
  rewrite's copy ends in); IroPointerAnalysis.c sets IROLinear.evaluated with `1`/`0` rather than TRUE/FALSE.

### Driver: version.h, cc-eabi-ppc.h, cc-eabi-ppc-mw.h, CCompiler.h, CLBrowser.h, CLIncludeFileCache.h, CLLicenses.h, CLLoadAndCache.h, CLParserCallbacks_v1.h, CLPluginRequests.h, CLPlugins.h, CLProj.h, CLToolExec.h, CLWriteObjectFile.h, CodeGenOptPPC.h, COSToolsCLT.h, DateTimeUtils.h, ELF_Lib_Import.h, ErrMgr.h, FileHandles.h, Files.h, Help.h, IO.h, libimp-eabi-ppc.h, LicenseImports.h, LinkErrorsPPC_EABI.h, LowMem.h, MacFileTypes.h, ParserFace.h, ParserGlue-eabi-ppc-cc.h, ParserHelpers-cc.h, ParserHelpers.h, Projects.h, Resources.h, ResourceStrings.h, StaticParserGlue.h, TargetOptimizerHelpers-eabi-ppc.h, TextUtils.h, ToolHelpers-cc.h, ToolHelpers.h, WarningHelpers.h

How I checked them: I compared every header with Pro7 (includes/cmdline.h, parser.h, macemul.h, oslib.h, cos.h,
compiler/CCompiler.h) and with Wii 1.0RC1's publics and modules. For every declaration I grepped its definition and its
users in other units (the .opt tables included). Calling conventions were checked against the exactness of the defining
rows. Packing and includes were checked too.

- version.h: DONE. Nothing to change. It holds only the VERSION_* indices and the default.
- cc-eabi-ppc.h: DONE. Nothing to change: Pro7's names and types (`int`). Wii's later names are
  RegisterStaticCompilerPlugins (plural) / RegisterCompilerResources. Pro7's (from cc-mach-ppc-mw.c) stay, as the
  unit review decided.
- cc-eabi-ppc-mw.h: DONE. Nothing to change (Pro7's CLStaticMain externs, OSType, all 5 Wii cc-eppc-mw publics).
- CCompiler.h: DONE. Added Pro7's `extern SInt32 license_cookie;`. Pro7's compiler/CCompiler.h declares it, and Wii has
  it as a public COMMON (only CCompiler.c uses it; kept as Pro7's API). The rest is exactly Pro7's. (Adversary) dropped
  the DropInCompilerLinker.h include: nothing in the header uses it (`CompilerLinkerParamBlk` is common.h's forward
  typedef, as Pro7 relies on), and none of the 11 including units needed it (clang_check, objects unchanged).
- CLBrowser.h: DONE. Nothing to change: identical to Pro7 cmdline.h's CLBrowser block. Every function has an outside
  user.
- CLIncludeFileCache.h: DONE. Nothing to change (Pro7's block; every function used by CLFileOps, CLLoadAndCache or
  CLCompilerLinkerDropin_V10).
- CLLicenses.h: DONE WITH TODOS. `License_Checkout`'s `int autoCheckin` stays. Pro7's `Boolean flag` (a guess there)
  costs License_Checkout and UCBCheckoutLicense, so it is recorded in nearmiss's "Changes blocked by exact functions" and
  commented in the header. `licenseFile` (Wii public COMMON) is used by CLMain. `License_Checkin(SInt32 cookie)` fills
  Pro7's unknown arguments.
- CLLoadAndCache.h: DONE. Dropped the unused Memory.h include (only `Handle`, from common.h, is used). Prototypes are
  Pro7's. CopyFileText is `char**, SInt32*` now, which resolves the old CLDropinCallbacks stand-in item.
- CLParserCallbacks_v1.h: DONE. Nothing to change (Pro7's PrefPanelsChangedCallback, used by CLMain). The C vs C++
  linkage question for the UCB functions is CLParserCallbacks_v1.cpp's, not this header's.
- CLPluginRequests.h: DONE. `cpu`/`os` are now Pro7's `OSType`, and `const ToolVersionInfo*` is spelled through the
  typedef. AddFileTypeMappingList stays here: Pro7's CLFileTypes.c has no 2.6 counterpart, and 2.6 has it in
  CLPluginRequests.
- CLPlugins.h: DONE.
  - Pro7's types: `Plugin.context` is `CWPluginContext`; the cpu/os/type/lang/style/plugintype parameters and
    Plugin_GetPluginType's return are `OSType`; Plugin_CL_WriteObjectFile takes `FSSpec*`.
  - All of these are the same types as before, so the code is unchanged.
  - Dropped the unused Memory.h include.
  - Every declaration has an outside user.
- CLProj.h: DONE. This resolves CLProj.c's "transitive includes" item. Includes went from StringExtras, oslib, win32,
  CLAccessPaths, MsDos and cmdline down to common.h, cmdline.h and oslib.h (what `Project` needs). CLProj.c now includes
  MsDos.h for OS_GetCWD/OS_MakeNameSpec, and CLMain.c includes StringExtras.h. Project is Pro7's.
- CLToolExec.h: DONE. (Adversary) ExecuteLinker's `dropinflags` is Pro7's `SInt32` again: the "2.6 difference" no
  longer holds. CLToolExec.c's object is byte-identical with either type (the UInt32 was needed once, at 95.8; later
  changes, likely CWPlugins.h's typed flags enum, made it moot). Header and source comments and review-2.6.md's
  CLToolExec entry updated.
- CLWriteObjectFile.h: DONE. Nothing to change (Pro7's).
- CodeGenOptPPC.h: DONE. Nothing to change: the 4 Wii CodeGenOptPPC publics, each with an outside user.
- COSToolsCLT.h: DONE. Dropped the unused Memory.h include. The prototypes are Pro7's cos.h, so the old items "COS_*
  prototypes living in CompilerTools.h" and "COS_FileGetFSSpecInfo/COS_FileGetPathName types" are resolved: CompilerTools.h
  has no COS_ declarations left, and both functions have Pro7's types.
- DateTimeUtils.h: DONE. Nothing to change (TickCount __stdcall: Wii `_TickCount@0`, row exact).
- ELF_Lib_Import.h: DONE WITH TODOS. (Adversary) ReportLinkerError/ReportLinkerWarning take `(char* message, char*
  line2)` and OSAlert `(char* message, short error)`, for the `void*` stand-ins (the definitions cast the `void*` back
  to `char*` for CWReportMessage's line1/line2; every caller passes a string); the definitions' invented parameter names
  (`name`, `argument`, `value`, `request`) follow. All 21 rows stay exact. Its comment now names
  `PLinker.suppresswarn` (see pref_structs.h). LinkInfo keeps
  its invented type name and its `unknown000`/`unknown291` placeholders, with the TODO. Those gaps (0x290, 0x307) are too
  large to be alignment holes, and no source names the linker's state. Its size is 0x598, which is Wii's COMMON and the
  memset's.
- ErrMgr.h: DONE. Nothing to change (GetSysErrText __stdcall, row exact; Pro7 macemul.h).
- FileHandles.h: DONE. Changed the `/* */` prose comment to `//`. The old "oslib.h has no Ptr-based types" item is
  resolved: `Ptr` is common.h's, and OS_LockFileHandle has Pro7's `Ptr*`.
- Files.h: DONE.
  - The old item "FSpDelete declared `DWORD(CWFileSpec*)`" is resolved: it is Pro7's `OSErr FSpDelete(const FSSpec*)`.
  - `#pragma pack(push, 2)`/`pop` is balanced, and FSSpec needs it (SInt32 parID at offset 2).
  - Pro7's `ConstStr255Param` is written `ConstStringPtr`, which is the same type. oslib.h (not this family) has no
    ConstStr255Param typedef.
- Help.h: DONE. Nothing to change (Pro7's; all 6 used by Option.c).
- IO.h: DONE. Dropped the unused Memory.h include. ShowVersion/ShowTextHandle are exactly Pro7's, which resolves the
  ParserErrors/Targets "IO.h parameter types" item.
- libimp-eabi-ppc.h: DONE. Nothing to change. Pro7's names stay (Wii: RegisterStaticImporterPlugins/
  RegisterImporterResources), as the unit review decided.
- LicenseImports.h: DONE WITH TODOS. The `_imp__lp_*` import-slot externs moved into LicenseImports.c, their only
  user; they belong to the DLL's import library, not to a unit. LPCODE_HANDLE keeps its TODO (FLEXlm's real type name is
  unverified).
- LinkErrorsPPC_EABI.h: DONE. Nothing to change (the 4 Wii publics, used by ELF_Lib_Import).
- LowMem.h: DONE. Changed the `/* */` prose comment to `//`. Prototypes are Pro7's.
- MacFileTypes.h: DONE. Nothing to change. OSFileTypeMappings is Pro7's, and the old CWPlugins.h
  `OSFileTypeMapping.filetype` item is resolved (it is Pro7's `OSType mactype`).
- ParserFace.h: DONE.
  - ParserTool's TYPE/LANG/CPU/OS and ParseOptsType's cpu/os are now Pro7's `OSType`, and `const ToolVersionInfo*`
    goes through the typedef.
  - Includes: replaced Arguments.h and Targets.h, which it never used, with cmdline.h. Help, IO, Option, Parameter,
    ParserFace, ParserGlue-eabi-ppc-cc, ParserHelpers-cc and ToolHelpers now include Targets.h (and IO and ParserGlue
    Arguments.h) themselves.
  - `helpFlags` is SInt32 (long), as Wii has it: resolved.
- ParserGlue-eabi-ppc-cc.h: DONE. Nothing to change.
- ParserHelpers-cc.h: DONE. Dropped the unused Memory.h include. The Opt_* callbacks stay: the .opt tables included by
  ParserGlue-eabi-ppc-cc.c use them, and they are Pro7's parser.h API and Wii publics.
- ParserHelpers.h: DONE. Dropped the unused `<setjmp.h>`, win32.h, Memory.h and oslib.h includes. The three Boolean
  globals stay, because OptsCmdLineCompiler.opt takes their addresses.
- Projects.h: DONE. Dropped `<setjmp.h>` and Memory.h, and replaced CLAccessPaths.h with oslib.h (OSSpec/OSPathSpec
  only). Prototypes are Pro7's.
- Resources.h: DONE. Dropped the unused Files.h and Memory.h includes. Resources.c now includes Memory.h itself. The
  cdecl/__stdcall split is checked by the exact rows: Wii decorates all of them `@N`, but 2.6 does not.
- ResourceStrings.h: DONE. Nothing to change.
- StaticParserGlue.h: DONE. Nothing to change.
- TargetOptimizerHelpers-eabi-ppc.h: DONE. Nothing to change (Pro7's three).
- TextUtils.h: DONE. Changed the `/* */` prose comment to `//`. The conventions are checked by the exact rows (c2pstr is
  cdecl in 2.6; Wii has `@4`).
- ToolHelpers-cc.h: DONE. Nothing to change.
- ToolHelpers.h: DONE WITH TODOS. Pro7's placeholders OutputOrdering0..2 became OutputOrdering_None/_After/_Before,
  with a comment on what each means (read from Opt_HandleOutputName, FinishCompilerTool and ParserHelpers'
  AddFileToProject calls). They keep `TODO: invented names`. `linkerOutputFilename[264]` (Pro7 256) is 2.6's .bss.
- WarningHelpers.h: DONE. Nothing to change. WarningHelpers.c now includes option_system.h for `PARAMPARSEFLAGS_8`, which
  it used to get only through ParserFace.h.
- Items left on other driver headers:
  - pref_structs.h: partly done.
    - PLinker.unknown05 -> `suppresswarn` (Boolean). It is the byte ELF_Lib_Import copies into
      linkinfo.nowarnings. The name is Pro7's PMachOLinker one for that panel setting; TODO.
    - (Adversary) PProject.unknown2d dropped: it is the hole before `SInt16 sdatathreshold` under the file's
      `#pragma pack(2)` (endian at 0x2c, sdatathreshold at 0x2e either way); pProject's initializer loses its 0, .data
      and every row are unchanged.
    - The other unknownXX (PBackEnd 05/07/09/0c/16, PLinker 0c/2f, PDisassembler 05, PProject 02/34) stay OPEN. Each
      sits between byte members (or is a tail), so none is an alignment hole; no option table takes their address and
      CodeGen_InitBackEndOptions reads none; gc1 has only `unkNN` names.
  - StringExtras.h `strcatn` src: resolved (`const char* s`).
  - StringUtils.h `mprintf`: resolved (Pro7's `char*, unsigned int, const char*, ...`).
  - StringUtils.h c2pstrcpy/p2cstrcpy: resolved (`StringPtr` / `ConstStringPtr`, Pro7's).
  - CLDropinCallbacks_V10.h: resolved. `DropinFileCallback` is gone (`CWFileInfo` is the SDK's), Deps_AddDependency's
    last parameter is Pro7's `Boolean*`, CopyFileText is Pro7's, and UCBGetFileText takes `const char**, SInt32*`.
  - The CW* wrapper SDK parameter types: these are CWPluginsPrivate.h's, outside this family, so I left them.
  - cmdline.h: resolved. It holds only forward typedefs of Pro7's real tag names.
  - `OptsCmdLineState_0..3`: already replaced by invented names with a TODO (pref_structs.h).
  - `data_00588258`: resolved. It is `SInt32 license_cookie` (CCompiler.c, bound as `_license_cookie`), now also
    declared in CCompiler.h.
- Units re-graded:
  - CLProj.c (PENDING HEADERS: "transitive includes in CLProj.h"): resolved. CLIO.h's items are another family's.
  - FileHandles.c (`Ptr` in oslib.h): resolved (common.h's `Ptr`).
  - CLLoadAndCache.c (StorageHandle; CopyFileText's stand-in types): both resolved.
  - CLIncludeFileCache.c, Memory.c, TextUtils.c, StringUtils.c (StorageHandle): resolved; no StorageHandle is left.
  - CLBrowser.c (StorageHandle part): resolved. The S43/S49/T53 names are the unit's.
  - CLFileOps.c (Files.h FSpDelete, StorageHandle): resolved. The CLIO.h macro and cmdline.h tags are not this family's;
    cmdline.h is clean now.
  - CLDependencies.c (the CWFileInfo stand-in behind the `char*` TODO, StorageHandle): resolved. CPrep.h's PFile is
    another family's.
  - MacFileTypes.c (CWPlugins.h mapping types): resolved.
  - ParserErrors.c, Targets.c (IO.h ShowVersion/ShowTextHandle, ParseOptsType.helpFlags): resolved.
  - CodeGenOptPPC.c, TargetOptimizerHelpers-eabi-ppc.c, ParserGlue-eabi-ppc-cc.c (pref_structs placeholders): partly
    resolved (suppresswarn). The rest stay open because nothing sets or reads them.
  - ELF_Lib_Import.c, LinkErrorsPPC_EABI.c (LinkInfo placeholders): still open (no source names them).
  - ToolHelpers.c (Pro7 placeholder enum names): replaced by invented names with a TODO.
  - CLPlugins.c, CLPluginRequests.cpp, CLParserCallbacks_v1.cpp: their items are other families' (context classes,
    linkage). AddFileTypeMappingList stays in CLPluginRequests.
  - ParserFace.c, ParserHelpers-cc.c, StaticParserGlue.c, Projects.c, ToolHelpers-cc.c, CLStaticMain.c,
    CLWriteObjectFile.c, Resources.c: nothing of this family is left pending.
- Renames:
  - `PLinker.unknown05 -> suppresswarn`
  - `PProject.unknown2d` removed (alignment hole; ParserGlue-eabi-ppc-cc.c's initializer drops its `0`)
  - `OutputOrdering0 -> OutputOrdering_None`
  - `OutputOrdering1 -> OutputOrdering_After`
  - `OutputOrdering2 -> OutputOrdering_Before`
- Checks:
  - Exact functions: 4019/4077 before and after, with no function changed either way (adversary re-run: no exact flag,
    similarity or data entry differs from compare_start.json).
  - Relink: all 219 of 219 Matching sources link (2849/4080 functions, 829108 bytes, 215 sources), after
    `tools/layout.py GC_2_6` (adversary re-run after its edits: the same).
  - clang_check: 82 errors, 0 not in the baseline (adversary re-run, after its edits). The baseline was saved in this copy before any edit; mwcc-gc2's file
    has 83.
  - validate GC_2_6: clean.
  - update_assert_lines: 0 lines to change.
  - The symbols.txt regenerated by layout renumbers some anonymous `@N` literal labels in one unit's .data. The count
    of `@` objects MWCC makes follows the declarations in scope; it did not affect linking.
- Adversarial review: attacked the names (suppresswarn is Pro7's PMachOLinker name, UInt8 there; the OutputOrdering
  meanings read again from Opt_HandleOutputName; the ELF_Lib_Import/LinkErrors names are Wii's publics; license_cookie
  is bound and Wii's), the types (Pro7's parser.h/cmdline.h/cos.h/macemul.h prototypes re-diffed: all match; the
  pref_structs unknowns checked for alignment holes; `License_Checkout`'s Boolean re-tried: 4019 -> 4017, License_Checkout
  99.5 and UCBCheckoutLicense 87.3, the claim holds; ExecuteLinker's UInt32 re-tried: the claim no longer holds), where
  things live (every family declaration has a definition and, except Pro7/Wii API kept on purpose, an outside user; no
  duplicate declarations across headers or `extern` copies in sources; the 12 Pro7 CLPlugins declarations without
  outside users stay removed, as the CLPlugins adversary decided), the includes (a header-by-header unused-include
  scan) and the checks (compare, layout, full relink, clang_check, validate, update_assert_lines, clang-format, all
  re-run). What broke: ExecuteLinker's documented UInt32 (now Pro7's SInt32), ELF_Lib_Import's `void*` parameters
  (now `char*`), PProject.unknown2d (an alignment hole, removed) and CCompiler.h's unused DropInCompilerLinker.h
  include (removed). What held: everything else, including the CLLicenses int, LinkInfo's placeholders, the
  `_imp__lp_*` move and the earlier-round items the first reviewer marked resolved (most were already fixed before
  this round: Files.h, StringExtras/StringUtils, CLDropinCallbacks_V10, cmdline.h, MacFileTypes, data_00588258).

### Back end and assembler: BE_elf.h, BE_symbol.h, CodeGen.h, Coloring.h, InterferenceGraph.h, SpillCode.h, Operands.h, InstrSelection.h, Intrinsics.h, FunctionCalls.h, StructMoves.h, PCodeAssembly.h, PCodeListing.h, Peephole.h, Scheduler.h, PPCError.h, InlineAsm.h, InlineAsmMnemonicsPPC.h, InlineAsmPPC.h, InlineAsmRegisters.h, InlineAsmRegistersPPC.h, FuncLevelAsmPPC.h, GCCInlineAsm.h, DWARF.h, ELF_Endian.h (+ PCodeInfo.h, PCode.h, PCodeUtilities.h, Registers.h, RegisterInfo.h, SFPE_PPC_EABI.h, ObjGen_PPC_EABI.h, StackFrame.h items)

- BE_elf.h: DONE WITH TODOS. Dropped the unused ELF_Endian.h include. SectionRec loses its pack(2) (all members
  naturally aligned, 16 bytes either way: neutral). ElfFile keeps pack(2), now commented: dropping it loses 37 exact
  functions (file_symbol sits at offset 2; IDA: +2, +6, the section pointers, +0x4C and a 16-bit count at +0x50 are what
  the code touches). Left: ElfFile's invented type/member names and `unused48` (nothing reads or writes +0x48; TODO, no
  source: Wii has only the COMMON `ELFfile` and the separate `debug_sections_inited`).
- BE_symbol.h: DONE WITH TODOS. Checked every declaration (all defined in BE_symbol.c, used by ObjGen/BE_elf/TOC/DWARF).
  Left: BE_SymNode's linkageKind/linkageFlags/reserved2a (a real 2-byte field under pack(2): sclass follows at 0x2C) and
  BE_SymbolTable's names (TODO-invented; Wii names only the COMMON TheSymbolTable).
- CodeGen.h: DONE WITH TODOS. Pro7's types where neutral: `disable_optimizer` int (was SInt32), `short
  CodeGen_AssignCheck(const ENode*, const Type*, Boolean convert, Boolean checkAccess)` (was int, non-const),
  `CodeGen_CollapseVectorExpression(ENode*, MWVector128*, Type*)` (was `union MWVector128*`, `TypeStruct*`; body uses
  `TYPE_STRUCT(type)->stype`, CExpr.c's call loses its TYPE_STRUCT cast). InterruptList: pack(2) dropped (two
  pointers), name marked TODO-invented. Every variable it declares is defined in CodeGen.c (Wii: all .bss COMMONs);
  those unused outside CodeGen.c stay only where they are Pro7's API (altivecM, returnlabel, rt_profile_*,
  CodeGen_reportheapinfo). rt_ptr_glue, rt_sfpe_d_neg and rt_sfpe_f_neg (2.6's, no user outside CodeGen.c) left the
  header (adversary; neutral).
- Coloring.h: DONE. Dropped the Registers.h include (RegClass is common.h's), as Pro7.
- InterferenceGraph.h: DONE WITH TODOS. `fIGNode40` -> `fMultipleDefs`; new enum fPairedSingle1 (0x800) /
  fPairedSingle0 (0x1000) for the IGNode flags SpillCode set and tested as literals (a separate enum: the byte-sized
  flag enum stays byte-sized). pack(2) dropped (layout identical, 28 bytes). Left: IGNode's member names (TODO, Pro7
  placeholders); `interferes` is Pro7's API and Wii's public (kept).
- SpillCode.h, FunctionCalls.h, StructMoves.h, Peephole.h, GCCInlineAsm.h, FuncLevelAsmPPC.h, InlineAsmRegisters.h,
  InlineAsmRegistersPPC.h, InlineAsmMnemonicsPPC.h, DWARF.h: DONE. Every declaration checked against its definition and
  users (Pro7's declarations; InlineAsm_SkipComment is used only in its unit but is Pro7's API). InlineAsmMnemonicsPPC's
  IAMnemonic member names stay TODO-invented (Pro7's x4/x10).
- Operands.h: DONE. Holds Pro7's OperandType, OpndFlags and struct Operand (moved from InstrSelection.h); Operand's
  members as Pro7's (SInt16 regs, `UInt32 flags`, was `unsigned int`: neutral); pack(2) dropped (24 bytes either way).
- InstrSelection.h: DONE. Includes Operands.h and PCodeInfo.h (as Pro7) instead of defining Operand; dropped
  <stddef.h>. binary_operator, unary_operator, fp_unary_operator and fp_multiply_add take Pro7's `Opcode opcode`
  (adversary; neutral). The `int unk` item was already fixed (`for_division`). gen_* stay declared (Pro7's API; only
  cgdispatch uses them).
- Intrinsics.h: DONE WITH TODOS. The 315 placeholders Intrinsic_000..314 are named after the runtime object each
  index gets in Intrinsics_SetupRuntimeObjects, less leading underscores (Intrinsic_eieio, Intrinsic_alloca,
  Intrinsic_vec_add, Intrinsic_rtos_change_stack, Intrinsic_builtin_va_info, ...), TODO-invented (Pro7's are
  placeholders too). ReInit's CheckRuntimeObject names differ for 10 indices (copy-paste slips of the original, e.g.
  163 checks "vec_vaddubs"); Setup's names used. The eight front-end includes are gone.
- PCodeAssembly.h: DONE WITH TODOS. pack(2) narrowed to WeirdOperand (object at 2, addend at 6: Pro7's x2/x6);
  EntryPoint (three pointers) unpacked. WeirdOperand member names TODO.
- PCodeListing.h: DONE. pclistblocks/pclistinterferences/pclistblocks_start_scheduler take Pro7's `char*` (were
  `const char*`; neutral). Pro7's other listing functions are 2.6 stubs used nowhere else (not declared).
- Scheduler.h: DONE WITH TODOS. pack(2) dropped (MachineInfo is all ints/pointers). MachineInfo's member names stay
  TODO-invented (Pro7 x0/x4/x8). is_same_operand stays (Pro7's API; Wii moved it to PCodeInfo, 2.6 has it here).
- PPCError.h: DONE. Pro7's PPCErrorStrNNN plus 2.6's 212-219; prototypes as Pro7.
- InlineAsm.h: DONE WITH TODOS. Now Pro7's InlineAsm.h: includes PCodeInfo.h (as Pro7) and holds IAOperandType/
  IAOperand/InlineAsm/IAEntryPoint/IAEffect*/IAEffects (moved from InlineAsmPPC.h), IAExprType/IAExpr, the IAFlagsB enum
  and Pro7's three inline expr helpers (moved from InlineAsmPPC.c; GetExprValue uses HIGH_PART instead of its
  expansion), one pack(2) region in Pro7's order. InlineAsm.opcode is Pro7's `Opcode` (adversary; neutral).
  IAEffect_0..4 named (Read, Write, Clobber, AddressOf, ReadWrite; TODO). IAOperand comments name the IAOpnd kinds.
  Left: IAExpr_0..10 (Pro7 placeholders, documented which are value/@l/@h/@ha), IAFlag1/2, the invented
  IAOpnd_/u.obj.reftype/gccreg/gccimm/IAExpr var names (TODO), IAEntryPoint's overlay names (its `argcount` is an int
  at 4 where InlineAsm's is a short; only `object`/`size` are read), InlineAsm_GetObjectOffset's `void*` (InlineAsm.c's
  fakematch, unit owner's).
- InlineAsmPPC.h: DONE WITH TODOS. Includes InlineAsm.h (as Pro7); gained OPCODE_PART_1..3 (from InlineAsmPPC.c, Pro7's
  place). EncodedOperand/ParsedAsmInstruction (1.x) deleted with their common.h typedefs; CodeGen_Generator's local is
  now `InlineAsm* ia` testing `ia->flags & IAFlag1`. CPUFLAG_800000..80000000 named by the mnemonics needing each
  (GRAPHICS, SEGMENTS, EXTERNAL_CONTROL, TLB, CACHE, TIME_BASE, PAIRED_SINGLE, ALTIVEC, FPU; TODO; checked against the
  mnemonic table's masks and Pro7's CPUMask_*); AssemblerType_0/1 -> AssemblerType_Statements/Function (TODO).
  InlineAsm_labelref is defined (InlineAsmPPC.c): resolved. Unmangle.c no longer includes InlineAsmRegisters.h.
- ELF_Endian.h: DONE. ELF specification names; signed st_value/st_size documented.
- PCodeInfo.h: DONE WITH TODOS. OpcodeInfo's operand_count/rank are TODO-invented (Pro7 x8/x9). The opcodes are now
  Pro7's `typedef enum Opcode`, with `PC_INVALID = -1` (a signed 2-byte enum) and OPCODE_MAX (469) as an enumerator
  before 2.6's PC_PENTRY/PC_PEXIT (adversary). The first review measured the enum without PC_INVALID (unsigned: 51,
  now 56, exact functions lost) and kept `#define`s; with it only three functions changed, and each is exact again
  with Pro7's types: Select_Gecko_Load_Store returns `Opcode`, gecko_load_store_register's `op` is `Opcode` (its short
  copy kept, commented), insert_load_spilled_register's `opcode` is Pro7's `Opcode`. makepcode/emitpcode stay `short op`
  (Pro7's `Opcode` costs vector_intrinsic_abss: ledger); PCode.op and vformatpcode are `short` in Pro7 too.
- PCodeUtilities.h (outside the family, touched for the enum): includes PCodeInfo.h as Pro7; Select_Gecko_Load_Store
  returns `Opcode`; load_store_register and branch_decrement_always take Pro7's `Opcode opcode`. That parameter type is
  what Coerce_to_register's "reload after a store" was: it is exact again (lost to S_BACK's `SInt32 immOffset`).
- PCode.h: DONE WITH TODOS. fPCBlockFlag2000/4000/8000 -> fInnerLoop/fUnrolled/fEntryPoint (fPCBlockFlag6000 dropped,
  its one use is `(fInnerLoop | fUnrolled)`); fPCodeFlag400000 -> fHasBaseReg; Effect4/8/40 -> EffectLastUse/
  EffectPinned/EffectSpilled; new SPR_GQR0 (#define 912, replacing 912/0x390 in 20 sites) and SPR_GQR1 (913,
  adversary: InstrSelection's seven 0x391); new enum EffectSingle/EffectPS0/EffectPS1 (0x2000/0x4000/0x8000) for the
  paired-single operand halves written as literals in PCodeInfo, InlineAsmPPC, CodeGen, InstrSelection, SpillCode. All
  TODO-invented. Left: Pro7's unused placeholders fPCodeFlag200/400/800/1000/80000/200000, fPCBlockFlag10; CodeGen.c's
  `opnd.flags & 0x8000` stays literal.
- Registers.h / RegisterInfo.h: DONE (already). VarInfo is in objects.h, no BufferedToken, ALLOC_* in RegisterInfo.h,
  `coloring` int, the four never-defined Registers_* prototypes gone, RegInfo holds an ObjRegInfo (no anonymous union).
- SFPE_PPC_EABI.h: DONE (already): no duplicate high_reg/low_reg declarations.
- ObjGen_PPC_EABI.h: DONE WITH TODOS. Dropped its include of InlineAsmPPC.h (FuncLevelAsmPPC.c and CPrepTokenizer.c now
  include it themselves). CGList unpacked (three pointers). Already done: ObjGenSection's `Elf32_Shdr shdr`, IRO_Setup
  from IrOptimizer.h. Left: BE_SymGroup/ObjGenSymbolLink/AddressingMode/InterruptRecord_*/SectionKind_* invented names
  (TODO).
- StackFrame.h: DONE WITH TODOS. `data_005884ff` gone and no `_data_00588476/8` bindings in any config (resolved).
  StackFrame_has_vectors is a real name (Wii public). Pro7's `SInt32` for update_*_align (costs 3 exact, re-measured)
  and can_add_displ_to_local (costs itself) blocked: ledger.
- Units re-graded:
  - MachineSimulation601.c, MachineSimulation7400.c (MachineInfo/OpcodeInfo member names): unchanged, the names are
    TODO-invented with no source to replace them.
  - PCodeUtilities.c (Registers.h problems): Registers.h resolved; its Gecko helpers now use Pro7's Opcode.
  - IrOptimizer.c (IAOpnd_3's name): resolved (IAOpnd_GlobalObj, TODO-invented).
  - InlineAsmMnemonicsPPC.c (IAMnemonic's two names): TODO-invented, nothing better exists.
  - GCCInlineAsm.c, FuncLevelAsmPPC.c, InlineAsm.c (InlineAsmPPC.h's placeholder members): resolved; remaining names
    TODO-invented.
  - PCodeInfo.c (PCodeInfo.h/PCode.h placeholders): flags and effect bits named, opcodes Pro7's enum; OpcodeInfo TODO
    names stay.
  - CodeGen.c (paired-single effect bits, GQR0 0x390): resolved (named).
  - Operands.c: Coerce_to_register exact again; the unit keeps two inexact functions (I8_insert_bitfield,
    I8_extract_bitfield) and the `bit_offset` stand-in (Pro7's `opcode`, even as `Opcode`, gives 98.6).
  - BE_elf.c, FunctionCalls.c (header items): headers checked; their open items are their own near-misses.
  - CExpr.c (CollapseVectorExpression's TypeStruct*): resolved.
  - InlineAsmRegisters.c: no item of mine (common.h).
- Renames: `IAEffect_0 -> IAEffect_Read`, `IAEffect_1 -> IAEffect_Write`, `IAEffect_2 -> IAEffect_Clobber`,
  `IAEffect_3 -> IAEffect_AddressOf`, `IAEffect_4 -> IAEffect_ReadWrite`; `CPUFLAG_800000 -> CPUFLAG_GRAPHICS`,
  `CPUFLAG_1000000 -> CPUFLAG_SEGMENTS`, `CPUFLAG_2000000 -> CPUFLAG_EXTERNAL_CONTROL`, `CPUFLAG_4000000 ->
  CPUFLAG_TLB`, `CPUFLAG_8000000 -> CPUFLAG_CACHE`, `CPUFLAG_10000000 -> CPUFLAG_TIME_BASE`, `CPUFLAG_20000000 ->
  CPUFLAG_PAIRED_SINGLE`, `CPUFLAG_40000000 -> CPUFLAG_ALTIVEC`, `CPUFLAG_80000000 -> CPUFLAG_FPU`;
  `AssemblerType_0 -> AssemblerType_Statements`, `AssemblerType_1 -> AssemblerType_Function`; `fPCBlockFlag2000 ->
  fInnerLoop`, `fPCBlockFlag4000 -> fUnrolled`, `fPCBlockFlag8000 -> fEntryPoint`, `fPCBlockFlag6000 -> (fInnerLoop |
  fUnrolled)` (deleted); `fPCodeFlag400000 -> fHasBaseReg`; `Effect4 -> EffectLastUse`, `Effect8 -> EffectPinned`,
  `Effect40 -> EffectSpilled`; `fIGNode40 -> fMultipleDefs`; `Intrinsic_NNN -> Intrinsic_<runtime name less leading
  underscores>` for all 315 (pairs in work/intr_map.txt; apply with `tools/rename.py $(cat work/intr_map.txt)`).
  Literals given names: 912/0x390 -> SPR_GQR0; 0x391 -> SPR_GQR1 (InstrSelection.c, adversary); effect
  0x2000/0x4000/0x8000/0xC000 -> EffectSingle/EffectPS0/EffectPS1/(EffectPS0 | EffectPS1); IGNode 0x800/0x1000 ->
  fPairedSingle1/fPairedSingle0. Deleted types: EncodedOperand, ParsedAsmInstruction. Adversary type changes (no
  renames): `#define PC_* N` -> `typedef enum Opcode {PC_INVALID = -1, ...}` (OPCODE_MAX an enumerator); `short`/`int`
  -> `Opcode` in Select_Gecko_Load_Store (return), load_store_register, branch_decrement_always, binary_operator,
  unary_operator, fp_unary_operator, fp_multiply_add (parameters), InlineAsm.opcode, and the locals of
  gecko_load_store_register (`op`), insert_load_spilled_register (`opcode`) and SpillCode's two `psop`.
- Includes dropped: InlineAsmPPC.h from ObjGen_PPC_EABI.h, GCCInlineAsm.c, CPrec.c, IroTransform.c; Registers.h from
  Coloring.h; ELF_Endian.h from BE_elf.h and CodeGen.c; <stddef.h> from InstrSelection.h; CodeGen.h from PCodeInfo.c;
  Coloring.h and Operands.h from StackFrame.c; PPCError.h from CFunc.c; DWARF.h from CodeGen.c and IroTransform.c.
  Added: InlineAsmPPC.h to FuncLevelAsmPPC.c and CPrepTokenizer.c; Operands.h to InstrSelection.h; InlineAsm.h to
  InlineAsmPPC.h; PCodeInfo.h to InstrSelection.h, InlineAsm.h and PCodeUtilities.h (adversary, as Pro7).
- Checks (final): GC_2_6 exact 4019/4077 before, 4020/4077 after: Coerce_to_register gained (99.86 -> 100), nothing
  lost, no other similarity changed; linked 2849/4080 unchanged (Operands.c still has two inexact functions);
  `tools/relink.py GC_2_6`: 219 of 219 Matching sources link; `ninja verify` passes for all five versions (2.0 3384,
  2.0p1 3383, 2.5 3961, 2.7 3928 exact: 2.5 and 2.7 one more each; other versions' symbols.txt regenerated, only
  `@NNN`/`lbl_` local-label names moved); clang_check: 82 errors, none new against build/clang_errors.txt (the
  baseline's 83 less CError.c's); validate GC_2_6 clean; update_assert_lines: 0 to change; clang-format clean over
  src/ and include/. Blocked changes are under "Changes blocked by exact functions" in docs/nearmiss-2.6.md
  (Coerce_to_register's entry removed, counts updated).
- Adversarial review: attacked the 315 Intrinsic renames (re-derived from the pre-review Intrinsics_SetupRuntimeObjects:
  every index maps to its runtime name, all unique, enum order intact: held); the CPUFLAG names against the mnemonic
  table's masks (held); the PCode/IGNode/effect flag names against their uses (fInnerLoop from LoopDetection,
  fUnrolled from newstatement, fEntryPoint from FuncAsm_AddEntryPoint, fHasBaseReg on addi/addis/lvsl, EffectPS1
  cleared by psq_l's W=1: held); IAEffect/IAOpnd/gccreg names against IROUseDef, IroVars and GCCInlineAsm (held);
  ElfFile's layout in IDA (held, `unused48` untouched, the count 16-bit); BE_SymNode, IGNode, Operand, SectionRec and
  WeirdOperand layouts (held); every family declaration's definition and users (no duplicates or private externs;
  three CodeGen.h externs with no outside user and no Pro7 source removed). Re-measured the blocked claims: StackFrame's
  `SInt32 align` loses exactly the three recorded functions (held); the Opcode-enum claim broke (Pro7's PC_INVALID had
  been left out; the enum is adopted, with a gain). Also broke: SpillCode.c was left unformatted by the fPairedSingle1
  literal rename (a 101-column line; formatted), and GQR1's 0x391 was left a literal. Not tried: InstrSelection.c's
  statics fp_binary_operator/binary_immediate/or_xor_immediate with Pro7's `Opcode` (the unit's own source).

### Front end: CABI.h, CBrowse.h, CClass.h, CDecl.h, CError.h, CExpr.h, CExpr2.h, CInit.h, CInline.h, CIRTransform.h, CMangler.h, CObjC.h, CObjCModern.h, objc.h, CParser.h, CPrec.h, CPreprocess.h, CPrepTokenizer.h, CRTTI.h, CScope.h, scopes.h, CSOM.h, som.h, Unmangle.h, ScanFloat.h, DumpIR.h

How each header was checked: every `extern` was compared with Pro7's header of the same name (declared names and
parameter types, after normalising spelling); each declaration's definer and outside users were found (all are defined
in their own unit; no `extern` copies or block-scope prototypes of these names are left in any source); each header was
compiled alone with clang and once per include with that include removed; the layout of every struct was dumped by
clang with and without the header's `#pragma pack(push, 2)` (offsets and size compared).

- CABI.h: DONE WITH TODOS. Pro7's declarations, every one used outside CABI.c except CABI_MakeLayeredDestructor and
  CABI_AddLayeredDestructors (kept: Pro7's API). `CABIDestroy0..3` are Pro7's placeholder names: marked TODO.
- CBrowse.h: DONE. Pro7's list minus gUseTokenStreamSource/gUseNameTable (no outside user; gUseNameTable is CBrowse.c's
  own global). Dropped the CompilerTools.h include (nothing in the header needs it; CBrowse.c now includes it).
- CClass.h: DONE. Identical to Pro7's declarations. CClass_IsNonStaticMemberFunc and CClass_GetMemberOffset have no
  outside users (kept as Pro7's API, as the CClass review decided).
- CDecl.h: DONE WITH TODOS. Pro7's list (functions used only in CDecl.c are left out, as before). Commented the two
  2.6 departures: CDecl_NewTemplDepType returns `TypeTemplDep*` (Pro7 `Type*`), CDecl_CheckCtorIntegrity returns
  Boolean and the callers report (Pro7 `void`). Left: DeclInfo/ClassLayout/BigDeclInfo's TODO-marked invented names
  for Pro7's offset members (x10..x57, xA, xCC/xCD). Packing: 2-byte packing is needed (ClassLayout is 0xe bytes,
  DeclInfo's and BigDeclInfo's offsets).
- CError.h: DONE WITH TODOS. Removed the CERROR_FILE machinery (no source defines it any more); the asserts pass
  `__FILE__` as Pro7's. `data_005830c8` is Pro7's `cerror_synchdata` (adversary: the "hash order rules it out" claim
  was stale, from a layout where `source_line` (3577) preceded the slot; in every version's layout the slot is between
  `symdecltoken` (222) and `all` (304) in the 8-aligned COMMON run, where 242 fits and the first reviewer's invented
  `cerror_synch` (3885) did not). CError_Error already returns `void`. CError_SetNullErrorToken, CError_BreakPoint,
  CError_ExpressionTooComplex and the three data have no user outside CError.c; kept as Pro7's API (as CError's own
  review decided; Wii has publics only for CError_BreakPointcount and CError_OverloadedFunctionError2).
  Pro7's `CError_FATAL` is a do/while(0): tried, it costs I8_gen_ADD and
  CExcept_ScanTryBlock, so 2.6's bare call stays (commented, ledger). The ERR_ names stay invented (TODO).
- CExpr.h: DONE WITH TODOS. Now Pro7's single header for CExpr.c, CExpr2.c and CExprConvMatch.c (CExpr2.h is gone;
  its 41 includers include CExpr.h). Pro7's order: the iterator records, CheckResult, Match5/Match13
  (ComparisonValues/ArgMatch, TODO-invented names kept), FuncMatchArgs, `Conversion` (was BinaryOperatorResult: Pro7's
  name; member `expression` stays for Pro7's x0, TODO), then the three units' declarations. EMemberInfo moved to
  enode.h (Pro7's place). `canadd` returns `short` as Pro7. 2-byte packing is now only around ComparisonValues and
  ArgMatch (ArgMatch.list at 0x1a needs it; the others lay out the same naturally). Left: `makediadicnode`'s `UInt8`
  type and `CExpr_SearchExprTree`'s `SInt32 count` (Pro7: ENodeType, int; each costs an exact function: ledger).
- CExpr2.h: DONE (merged into CExpr.h, deleted). Its stand-ins listed in earlier entries (MatchLink, `xE`,
  `data_00580748`, CExpr_ConstructObject's `Type*`, CExpr_NewETEMPNode's `withAuxiliary`) were already gone.
- CInit.h: DONE WITH TODOS. `RelocationList` (and its `typedef RelocationList OLinkList`) is Pro7's `struct OLinkList`
  everywhere, member `object` -> `obj` (Pro7's); `addend` stays for Pro7's placeholder `somevalue` (TODO). Dropped the
  enode.h include (Pro7's header includes only common.h). PooledString needs 2-byte packing (0x16 bytes).
  InitInfo/ExprCB/cinit_initinfo stay in CInit.c (only it uses them).
- CInline.h: DONE WITH TODOS. Dropped the CPrep.h include (unneeded; Pro7 includes common/tokens/CFunc). `CopyMode0..4`
  marked as Pro7's placeholders (TODO). Left: CInline_AddTemplateFunctionAction's `void*` parameters (rechecked: Pro7's
  types still cost CTempl_GetCreateFuncInstance), CInlineBackendOptions and CI_Var's TODO-invented names.
  CInline_AddDefaultFunctionAction/AddMemberFunctionAction are used only in CInline.c (not declared, as before).
  `functionbodyoffset` (an older review's item) is no longer declared here.
- CIRTransform.h: DONE. Pro7's declarations less CIRTrans_Transform (no caller outside its unit).
- CMangler.h: DONE. Identical to Pro7's.
- CObjC.h: DONE. Pro7's (`long` counters are SInt32, the same type); cobjc_encodemethod is CObjC.c's own.
- CObjCModern.h: DONE. Pro7's three functions.
- objc.h: DONE. Pro7's records; 2-byte packing narrowed to ObjCInfo, the only record whose layout needs it (0x1a
  bytes).
- CParser.h: DONE. Now holds Pro7's CompilerLinkerOptions (was COpts in CPrep.h: renamed to Pro7's name and moved
  here with the AlignMode/CPU enums and `extern copts`, inside its own `#pragma pack(push, 2)`: bc_range at 0x5e,
  inlinemaxsize at 0x6e). It includes driver/CWPlugins.h for CWDataType/StringPtr. The 16 sources that used copts
  without CParser.h now include it. Left on the struct (unchanged, TODO-marked): `use_fp`, `soft_float`, `full_dwarf`,
  `no_comment_records`, `sdata_threshold`/`sdata2_threshold`/`common`, the section members, `dwarf_siblings`/
  `dwarf_relocations`, and the unused `unknown66[2]`/`unknownfd` (real 2-byte/1-byte fields: under 2-byte packing a
  hole cannot be there, and no code or pragma names them). CParser_IsReadOnly is 2.6's (Alias.c uses it).
- CPrec.h: DONE. Pro7's (OLinkList).
- CPreprocess.h: DONE. Pro7's plus 2.6's CPrep_PreprocessDumpDirective/DirectiveEnd (CPrep.c calls them).
- CPrepTokenizer.h: DONE WITH TODOS. Pro7's list; `unsigned char*` stays where Pro7 writes StringPtr (the same type;
  StringPtr is driver/oslib.h's here). CR/LF are here where Pro7 has them in common.h (commented).
- CRTTI.h: DONE. Pro7's (OLinkList).
- CScope.h: DONE WITH TODOS. NameResult's `x1F` is a real byte (is_type follows at 0x20 and the record is 0x22 bytes;
  no user known): kept with its TODO; the other NameResult names are TODO-invented. CScopeSave and NameResult need the
  2-byte packing.
- scopes.h: DONE. Pro7's records (NameSpaceLookupList and CScopeNSIterator are defined in CScope.c, their only
  user; Pro7 has them in scopes.h/CScope.h); removed the stale `0x00:`/`0x04:` offset comments and the `#pragma pack(push, 2)`:
  none of NameSpace/NameSpaceList/NameSpaceObjectList/NameSpaceName changes layout without it (checked by clang's
  layout dump, and every function and relink stayed the same). Their alignment goes from 2 to 4 (as for objc.h's
  records and the CExpr.h iterators, FuncMatchArgs and Conversion): none is a global or a member of another record,
  only locals, and no function changed.
- CSOM.h: DONE WITH TODOS. Pro7's; CSOMStub.kind is TODO-invented (Pro7 x10); CSOMStub needs the packing.
- som.h: DONE. Pro7's; SOMInfo needs the packing.
- Unmangle.h, ScanFloat.h, DumpIR.h: DONE. Each declaration has an outside user; DumpIR's other Pro7 functions are
  used only in DumpIR.c.

Items left on other front-end headers:
- CPrep.h: COpts placeholders `pad01/pad0b/pad15`, `f25/f26/f27` were already named; `use_fp`/`soft_float` stay
  invented (TODO); `copts` and the struct now live in CParser.h as Pro7 (above). FIXED (the move); TODOs remain.
- enode.h: `unk0e`, `unk08`, `data.labelAddress`, `string.useExplicitSize` were already Pro7's (`label`, `ispascal`);
  precomp/nullcheck members are Pro7's. EMemberInfo now lives here (Pro7). FIXED.
- CFunc.h: CLabel has Pro7's plain `Statement* stmt`; `curstmtvalue` and `Labels` are defined in CFunc.c. Already
  FIXED.
- types.h: `TypeObjCID.x12` is a real 2-byte member (sizeof is 0x14: CObjC allocates it, CPrec writes it; without it
  the record is 0x12) with no known user: kept, TODO. VEC2X32 names: no placeholders left. objects.h:
  `Inline_IsObjectDataOrAbsolute` is gone. Already FIXED but x12.
- common.h: the IDA-name forward declarations and COptBlockLink/OptimizerOccurrence/BlockOrderEntry were already gone;
  removed 11 more typedefs no code names (FloatFormatBuffer, MemberPointerInitializer, NameEntry, BVWord,
  BrowseObjectBuffer, CPrecBytes, CPrecKey, CPrecPtrU, LongBytes, OutputRelocation, UInt32ByteSwapStorage: the first
  reviewer claimed this but the file still had all 11; the adversary removed them, no function changed);
  RelocationList's and COpts' typedefs renamed. Left: COptBlock/COptCSE (COptimizer.c's), DwarfSymbol (objects.h) and
  ExceptionTableHeader (ExceptionX86.cpp) are named but defined nowhere (pointer-only uses).
- CompilerTools.h: the endian converters take `UInt16*`/`UInt32*` (Pro7); `CTool_EndianConvertMem(void*)` is 2.6's
  (commented); `data_00587958`/`data_00588228` are gone. Already FIXED. CExpr2.h `xE`/`data_00580748`/MatchLink, CExpr.h
  `data_0055f5fa`: already gone. CScope.h `x1F` (above); `x21` is no longer in CScope.h (IroPointerAnalysis.c has
  its own `x21` in a local record, not this family).
- COMMON names: 0x5e9f4c is `temp_argument_name` (hash 438, between curstmtvalue 432 and DefaultContext 444) and
  0x5eb38f is `in_func_arglist` (1959, between copt_isleaffunction 1919 and widestring 2021): both already fixed and in
  hash order.
- CTemplateClass.h: now includes only common.h. Its 6 remaining includers (DWARF, CTemplateClass, CScope,
  CTemplateTools, CDecl, CTemplateNew) include what they use (templates.h, CTemplateNew.h, CPrep.h, objects.h,
  CompilerTools.h as needed). FIXED.

- Units re-graded:
  - CException.c (PENDING HEADERS: DeclInfo, CInit.h, CInline.h, CExpr2.h, CSOM.h, CPrep.h placeholders): CInit.h
    (OLinkList), CExpr2.h (merged, no placeholders) and CPrep.h (COpts moved/renamed) resolved; what is left is
    TODO-marked invented names (DeclInfo, CInlineBackendOptions, CSOMStub.kind, a few COpts members). Resolved as far as
    sources allow -> SOLID WITH TODOS.
  - CError.c (CObjC.h and CExpr2.h stand-ins, `data_005830c8`): resolved (CObjC.h has none left; CExpr2.h merged;
    Pro7's `cerror_synchdata`, in hash order: no TODO left on it).
  - CClass.c, CABI.c, CRTTI.c ("the headers' problems above": ScopeSearch, CScopeParseResult, MatchLink, xE,
    CInlineInfo, RelocationList, CError_Error's type, the private prototype, types.h x12): all resolved except
    types.h's x12 (real member, TODO) and NameResult.x1F (real member, TODO).
  - CExpr.c (pending headers: BinaryOperatorResult, NameResult names, data_0055f5fa): resolved (Conversion; the rest
    were already done).
  - CTemplateClass.c (CTemplateClass.h's includes): resolved.
  - CBrowse.c (DeclInfo/Macro TODO names): unchanged; those are TODO-invented names for Pro7 offset members (no source
    has names). Not resolvable from the sources.
  - CInit.c (DeclInfo, RelocationList, ENodeUnion placeholders): RelocationList resolved; ENodeUnion placeholders were
    already gone; DeclInfo as above.
  - CExprConvMatch.c (ArgMatch's `xE`): resolved earlier (`userconversions`); ArgMatch/ComparisonValues keep
    TODO-invented names for Pro7's Match13/Match5.
  - CompilerTools.c (header endian types): resolved earlier.
  - CMangler.c (types.h IT_ names): not this family (types.h); unchanged.
  - CObjC.c, CObjCModern.c, CSOM.c (shared header problems): resolved (RelocationList, CExpr2.h stand-ins); TODOs left
    are the units' own.
  - CPrepTokenizer.c ("the headers above": CPrep.h COpts, TK_UNK names): COpts resolved; tokens.h's TK_UNK names are
    not this family.
  - CInline.c (header problems listed: AddTemplateFunctionAction void*, CPrec.h stream name): the stream name is
    Pro7's `inlinefunc.stream` already; the void* is blocked by an exact function (ledger).
  - CExpr2.c (Match5/Match13 stand-in names, CTemplateClass.h include): CExpr2.h merged; the names are TODO-invented
    for Pro7's placeholders. Header part resolved.
  - CPrec.c (CTemplateClass.h includes CPrec.h for nothing; RelocationList): resolved.
  - CIRTransform.c ("header review only"): CIRTransform.h reviewed, nothing to change -> SOLID.
- Renames:
  - `data_005830c8 -> cerror_synchdata` (bindings and symbols of GC_2_0/2_0p1/2_5/2_6/2_7; the first reviewer's
    intermediate `cerror_synch` is gone)
  - `BinaryOperatorResult -> Conversion`
  - `RelocationList -> OLinkList` (the `typedef RelocationList OLinkList` deleted), `OLinkList.object -> obj`
  - `COpts -> CompilerLinkerOptions`
  - include `"compiler/CExpr2.h" -> "compiler/CExpr.h"` (CExpr2.h deleted; one line where a file had both)
  - moved: EMemberInfo CExpr.h -> enode.h; the AlignMode/CPU enums, CompilerLinkerOptions and `extern copts`
    CPrep.h -> CParser.h
- Checks: exact functions 4019 before, 4019 after; no function changed verdict or similarity (every row's similarity
  compared with compare_start.json). Full `tools/relink.py GC_2_6`: 219 of 219 Matching sources link.
  `tools/clang_check.py`: 82 errors, 0 not in the baseline (build/clang_errors.txt). `tools/validate.py GC_2_6`: clean.
  `tools/layout.py GC_2_6` rewritten (symbols.txt: generated local label numbers moved; regenerate after the merge).
  `tools/update_assert_lines.py GC_2_6`: 0 lines to change. Two header edits flipped IroPointerAnalysisADTs.c's
  LocationSetSet_Remove/Add (a spill) though the unit uses nothing of them; the fix was Pro7's include order for
  IroPointerAnalysis.c (the ADTs file included after every header, where it had been 10th), with which both are exact
  under the new headers and no workaround include is needed.
  Re-run after the adversary's changes: 4019 exact, no row's verdict or similarity changed; 219 of 219 Matching
  sources link; clang_check 82, 0 new; validate clean; update_assert_lines 0; clang-format clean on every changed file.
- Adversarial review: attacked the COMMON-name claim, the layout claims, where each declaration lives, the "removed"
  claims, one blocked-by-exact claim, and the checks. Broke: (1) `cerror_synch` was the wrong call: the "needs >= 3577"
  bound came from a stale layout; every version puts the slot between symdecltoken (222) and all (304), so Pro7's
  `cerror_synchdata` (242) fits and is now the name (the only other out-of-order names in that run, `dwinfo_buckets`
  and `lbl_005E988C`, are invented/placeholders too, which supports the hash check). (2) common.h's 11 dead typedefs were
  claimed removed but were all still there: removed now. (3) The COpts -> CompilerLinkerOptions rename pushed four
  comments past 100 columns (CInline.h's CInlineBackendOptions note, CPrep.c's compileroptions note,
  TargetOptimizerHelpers-eabi-ppc.c, driver/pref_structs.h): reflowed. (4) Minor wording: scopes.h is not all of
  Pro7's records (NameSpaceLookupList lives in CScope.c), and narrowing the packing changed those records' alignment
  (2 -> 4), not only "nothing": harmless (locals only). Held: a whole-tree clang record-layout dump of every struct
  (base copy against now, renames mapped) shows identical offsets and sizes for all 271 records; no family
  declaration is defined outside its unit or declared in two headers, and no source keeps an `extern` copy (the one
  left, CIRTransform.c's `alloca_called`, is documented: 2.6 has no such COMMON); copts+0x66/0x67/0xfd are referenced
  nowhere in the binary (no absolute operand), so `unknown66`/`unknownfd` are unread; `sizeof(CompilerLinkerOptions)`
  is fixed by the exact C_Compiler; NameResult.x1F is a real byte (unused in Pro7 too); the
  IroPointerAnalysis.c include order is Pro7's; `CExpr_SearchExprTree`'s Pro7 `int count` re-tried: the function drops
  to 46% (stays SInt32); temp_argument_name and in_func_arglist are in hash order. Left as judged: functions with no
  outside user that Pro7's header declares (CABI_MakeLayeredDestructor/AddLayeredDestructors, CClass_IsNonStaticMemberFunc/
  GetMemberOffset, CMangler_BasicDtorName/ArrayDtorName, CPrep_SkipNewComment, CError's above) stay; Wii has no public
  for most of them, which the bar's "Wii has the public" test does not support, but a static in Wii is not proof of one
  in 2.6.

### Follow-ups after round H2 (October 16): the items that kept the last units pending
- objects.h: ObjBase, ObjNameSpace, ObjType, ObjTypeTag and Object have Pro7's `AccessType access` (was `UInt8`;
  every function's similarity unchanged).
- CTemplateTools.h: CTemplTool_MergeArgNames takes Pro7's `TypeFunc*` (callers wrap in `TYPE_FUNC` as Pro7's do) and
  CTemplTool_IsTemplate returns Pro7's `TemplClass*`; code unchanged.
- CWSecretPluginCallbacks.h: CWSecretGetNamedPreferences is the SDK's `(CWPluginContext, const char*, Handle*)`;
  ELF_Lib_Import's preference handles are `Handle` locals read through casts, as CodeGenOptPPC's. Both units link.
- CWPlugins.h: CWTargetList is the SDK's typedef; the driver and plugins write Pro7's bare `CWTargetList`.
- types.h: TYPE_IS_SOFT_FLOAT and TYPE_IS_GPR_PAIR carry `TODO: invented name`.
- include/libc: all twelve headers have `extern "C"` guards, as MSL's do; the .cpp files' wrapper blocks are gone.
- pref_structs.h: the EABI panels' `unknownXX` fields are a documented TODO (real, never set or read, no source).
- tokens.h: Pro7's `EToken` enum measured: it costs TOC.c's createfloatconstant (100 -> 98.8), so the `#define`s
  stay (docs/nearmiss-2.6.md, blocked section; a lead for the matching round).
- Sources: IROUseDef compares with `IT_17`; CExpr2 uses Pro7's `data.precompid`; the raw destructor modes in CABI,
  CExpr, CExpr2 and CFunc are CABIDestroy0..2 (with TRUE/FALSE flags).
- After these: no unit is graded PENDING HEADERS (51 SOLID, 132 SOLID WITH TODOS, 7 NOT SOLID). Every version
  unchanged (2.6 4020/4077 exact, 219/219 Matching link), clang_check 82/0 new.

## MSL and runtime wrappers: light pass (October 16, inline)

The 57 library units all link; the pass checked each one's shape (an `#include` of the library's own source where
lib/ has it), its build entry and its data cut, and rewrote the hand-decompiled ones from the library's text.
- msl/critical_regions_win32.c: was a Ghidra-shaped body (`DAT_0057d228`, `iVar1`); now an `#include` of MSL Win32's
  critical_regions.win32.c with `_MWMT` set (2.x links the begin/end functions, compiled only under it), and the MSL
  system include paths of its neighbours (config/sources.json). All four exact; the `.bss` is the source's own `cs`.
- runtime/exchand.cpp: was a disassembly-shaped decompilation with its own stand-in types
  (include/runtime/ExceptionTables.h, deleted). Now lib/runtime's text: `_MWCHandler` with the SDK's types, writing
  to the standard error handle (`GetStdHandle(STD_ERROR_HANDLE)`; the old header comment said standard output), and
  `_RegisterExceptionTables` and `ExX86_FindExceptionRecord` verbatim from lib/runtime's ExceptionX86.cpp (2.6's
  revision moved them here and made the lookup extern). The lookup's `static long def = 0` is what the former
  `data_0057d004`/`data_0057d008` were (the static and its guard). Renamed: `lookup_runtime_record_and_call_value`
  -> `ExX86_FindExceptionRecord`, `data_0057d000` -> `FirstExceptionTable` (the library's names for the same
  bodies). Exact, links.
- runtime/setupargs.c: hand-written for a revision later than lib/runtime's (it handles `""` inside quotes and
  backslash runs); its locals now carry the library's names (`inptr`, `outptr`, `maxargc`, `quoting`, `newargs`)
  and a header says what changed. The `#pragma auto_inline off` around `_MemFailure` was unneeded (removed). Exact.
- msl/time_win32.c: `subtract_time_zone_bias` written as `__to_gm_time`'s inverse in the library's style; its name
  is invented (TODO). Exact.
- Bindings removed in every version, the objects now naming the data: `_DAT_0057d228` (`cs`), `_DAT_0057d018`
  (abort_exit's `atexit_funcs`), `_data_00536030` (exchand's message, a literal), `_data_0057d004`/`_data_0057d008`.
  `_data_0057d2a4` was no datum: it is `signal_funcs - 4`, the base signal.c's `signal_funcs[signal-1]` indexes
  from; the cut now gives it to signal.c (was alloc_g.c), unnamed.
- tools/layout.py: a compiled function whose masked body matches more than one place in the original (bodies that
  differ only in what they call: `__init_`/`__kill_critical_regions`) is placed at its own row's address, where it
  was left unplaced. That placed critical_regions' data, and named four literals elsewhere (`@4528`, `@4553`,
  `@5288`, `@5307`, were `lbl_`).
- Left for the naming sweep: ExceptionX86.cpp, ThreadLocalData.c and NewHand.cpp are the library's text with 2.x's
  changes, as their headers say. Every version unchanged (2.6 4020/4077 exact, 2849 linked); clang_check 84, the
  two new ones MSL headers exchand now includes as ExceptionX86 does.
