# Per-unit audit of 2.6 (October 2026)

A snapshot, not a status page: every unit of GC_2_6 read and graded by agents on October 14, 2026, against
Pro7, gc1 and the disassembly. Units have changed since (exact counts, links, names), and `file:line`
references predate the October 15 reformat, so treat lines as approximate and check the code before acting.
`tools/status.py GC_2_6 --todo` is the current state; this file says what kind of work each unit needs.

Grades:

- **A**: links from source and is clean. Real names, the original's shape, no hacks. Done.
- **B**: the original's (Pro7 or 2.6) bodies throughout; left are near-misses, data, link mechanics or naming polish.
- **C**: mixed. Some 1.x or wrong bodies, invented or placeholder names, or hacks remain; targeted rewrites.
- **D**: mostly 1.x or stand-in code; needs a rewrite from Pro7 or the disassembly.

Effort: S an afternoon, M a focused session, L a full rewrite. Counts: 88 A, 81 B, 57 C, 15 D.

Data: the unit's data grade from [audit-data-2.6.md](audit-data-2.6.md) (October 15).

## Index

| Unit | Grade | Data | Effort | Exact/rows (then) | Linked (then) | Origin |
|---|---|---|---|---|---|---|
| backend/CMachine.c | D | D | M | 36/48 | no | gc1 (1.x) CMachine with 2.6 EABI additions; core layout/alignment code is 1.x |
| backend/DWARF.c | D | D | L | 13/38 | no | gc1's 1.x DWARF.c, missing 2.6's EABI/Gekko additions |
| backend/Exceptions.c | D | C | M | 5/8 | no | gc1 (1.x) Exceptions.c with invented types; Pro7 file unused |
| backend/InlineAsmPPC.c | D | D | L | 13/26 | no | gc1 (1.x) InlineAsmPPC.c under 2.6 row names, gc1 types |
| backend/InlineAsmRegistersPPC.c | D | A | M | 4/4 | yes | gc1-style code moved from CTemplateNew.c, invented names |
| backend/MachineSimulationAltiVec.c | D | D | M | 4/7 | no | hand-written from disassembly over raw data fragments |
| backend/Operands.c | D | C | L | 13/30 | no | gc1 (1.x) Operands with raw-offset 2.6 stand-ins |
| backend/PCodeAssembly.c | D | C | L | 1/7 | no | 1.x/gc1-style bodies with 2.6 opcode cases |
| backend/Scheduler.c | D | B | M | 10/11 | no | gc1 (1.x) Scheduler.c with gc1 types |
| backend/TOC.c | D | D | L | 17/29 | no | gc1 (1.x) TOC code under 2.6 rows, with a few Pro7/2.6 bodies (resetTOCvarinfo, used_anchor, TOC_use_fsel) |
| frontend/CPrec.c | D | C | L | 50/84 | no | gc1's CPrec.c (about 37% gc1-only lines, 9% Pro7): Ghidra-style text with gc1 addresses in its names |
| optimizer/ConstantPropagation.c | D | D | M | 1/4 | no | 1.x constant propagation plus 1.x load deletion |
| optimizer/FunctionCalls.c | D | D | M | 1/5 | no | 1.x/gc1 argument passing with EABI patches |
| optimizer/GlobalOptimizer.c | D | A | S | 3/3 | yes | 1.x pipeline plus row-less 1.x AddPropagation/CopyPropagation code |
| optimizer/ValueNumbering.c | D | D | M | 1/14 | no | 1.x/gc1 value numbering under 2.6 row names |
| backend/BE_elf.c | C | C | M | 15/21 | no | gc1's 1.x BE_elf.c (renamed, in binary order), most bodies equal to 2.6 |
| backend/BE_symbol.c | C | C | S | 13/18 | no | gc1's 1.x BE_symbol.c (renamed, reordered), mostly matching 2.6 |
| backend/ELF_Endian.c | C | C | S | 7/8 | no | gc1's 1.x ELF_Endian.c (decompiler-shaped), equal to 2.6's code |
| backend/InlineAsm.c | C | C | M | 19/23 | no | mostly gc1 bodies on gc1 types, close to Pro7 in code |
| backend/InlineAsmMnemonicsPPC.c | C | A | M | 2/2 | yes | gc1-shaped code (moved from CTemplateNew.c) for Pro7's two functions; table undefined |
| backend/InlineAsmRegisters.c | C | A | S | 3/3 | yes | Pro7's InlineAsmRegisters.c logic with a stand-in struct, binary order |
| backend/InstrSelection.c | C | C | L | 41/83 | no | mix: Pro7 bodies with 2.6 soft-float/paired-single additions plus gc1 (1.3) code and invented helpers |
| backend/ObjGen_PPC_EABI.c | C | C | M | 36/51 | no | gc1's 1.x ObjGen_PPC_EABI.c (renamed, reordered to the binary), part updated to 2.6 |
| backend/PCodeInfo.c | C | C | M | 10/11 | no | Pro7 bodies for the small helpers; a 1.x vformatpcode |
| backend/PCodeListing.c | C | A | M | 5/5 | yes | Pro7 stubs (the live functions only) |
| backend/PCodeUtilities.c | C | A | M | 18/18 | yes | mix: Pro7 bodies for the branch helpers, 1.x bodies and unrowed 1.x code elsewhere |
| backend/SFPE_PPC_EABI.c | C | C | M | 1/8 | no | gc1's SFPE_PPC_EABI.c (1.x soft-float EABI) adapted to 2.6 rows; no Pro7 counterpart |
| driver/CLBrowser.c | C | A | S | 9/9 | yes | gc1-style rewrite of Pro7's CLBrowser.c |
| driver/CLCompilerLinkerDropin_V10.cpp | C | A | M | 22/22 | yes | gc1-style decompilation of Pro7's CLCompilerLinkerDropin_V10.cpp |
| driver/CLDropinCallbacks_V10.cpp | C | A | L | 47/47 | yes | gc1-style decompilation of Pro7's CLDropinCallbacks_V10.cpp with stand-in types |
| driver/CLFileOps.c | C | A | M | 16/16 | yes | Pro7 CLFileOps.c bodies in a 1.x file layout with stand-in types |
| driver/CLFiles.c | C | A | M | 14/14 | yes | Pro7 Project/CLFiles.c bodies with 1.x stand-in types |
| driver/CLIO.c | C | A | M | 39/39 | yes | Pro7 CLIO.c with 1.x/decompiler data, stand-in types and a few 1.x bodies |
| driver/CLIncludeFileCache.c | C | A | S | 8/8 | yes | 1.x body under Pro7 names |
| driver/CLLicenses.c | C | A | S | 10/10 | yes | 2.6 FLEXlm licensing code written from the disassembly (Pro7 has only stubs) |
| driver/CLParserCallbacks_v1.cpp | C | A | S | 11/11 | yes | Pro7 CLParserCallbacks_v1.cpp bodies with stand-in types |
| driver/CLPluginRequests.cpp | C | A | M | 11/11 | yes | Pro7 CLPluginRequests.cpp bodies with fake context classes |
| driver/ELF_Lib_Import.c | C | A | M | 21/21 | yes | 2.x ELF library importer written from the disassembly (Wii module ELF_Lib_Import) |
| driver/LicenseImports.c | C | A | S | 3/3 | yes | hand-written naked-asm stand-ins for import thunks |
| driver/LinkErrorsPPC_EABI.c | C | A | S | 5/5 | yes | 2.x linker error reporting (Wii module LinkErrorsPPC_EABI), written from the disassembly |
| driver/MsDos.c | C | C | M | 58/60 | no | Pro7-era MsDos (Win32 twin of Posix.c) with some 1.x bodies |
| driver/Option.c | C | A | M | 38/38 | yes | Pro7 Option.c logic in 1.x/decompiler dress (placeholder data, shifted rows, invented names) |
| driver/TargetOptimizerHelpers-eabi-ppc.c | C | A | S | 3/3 | yes | 1.x (gc1) body under 2.6 names |
| driver/ToolHelpers.c | C | A | S | 4/4 | yes | Pro7 ToolHelpers.c bodies with 1.x/decompiler names |
| frontend/CDecl.c | C | B | L | 74/78 | no | mixed: Pro7's order and names over gc1 text (about 28% gc1-only lines, 23% Pro7) |
| frontend/CError.c | C | A | M | 40/40 | yes | gc1's CError.c (about 43% of lines are gc1-only, about 10% Pro7) with Pro7's function names, built deferred |
| frontend/CException.c | C | A | M | 41/41 | yes | gc1 (1.x) CException.c bodies, renamed to Pro7's function names and put in Pro7's order |
| frontend/CFunc.c | C | C | L | 49/51 | no | gc1's CFunc.c (about 37% gc1-only lines, 20% Pro7) under Pro7's names, deferred |
| frontend/CInit.c | C | A | M | 50/50 | yes | gc1's CInit.c (about 47% gc1-only lines, 13% Pro7) under Pro7's names, deferred |
| frontend/CInline.c | C | C | L | 53/57 | no | gc1's CInline.c (about 42% gc1-only lines, 9% Pro7) under Pro7's names, deferred |
| frontend/CInt64.c | C | A | M | 46/46 | yes | 1.x/decompiler-style rewrite in binary order (Pro7 unsorted/CInt64.c bodies, inlined helpers spelled out) |
| frontend/CObjC.c | C | C | L | 53/55 | no | gc1/1.x CObjC.c + CObjCModern.c with invented types, written in binary order (non-deferred); the bodies compile to 2.6's code |
| frontend/CParser.c | C | A | M | 89/89 | yes | mixed: Pro7's order and names over gc1 text (about 29% of lines gc1-only, 28% Pro7) |
| frontend/CSOM.c | C | A | M | 37/37 | yes | gc1 CSOM.c bodies under Pro7 public names, deferred |
| frontend/CTemplateClass.c | C | B | L | 30/31 | no | gc1-shaped CTemplateClass.c (invented types, magic numbers) in binary order, non-deferred; matches but is not Pro7's form |
| frontend/CTemplateNew.c | C | B | L | 24/25 | no | Pro7 CTemplateNew.c logic with gc1/invented template types, written in binary order (non-deferred) |
| frontend/Unmangle.c | C | A | M | 13/13 | yes | gc1's Unmangle.c (about 75% gc1 text, no Pro7 text): Ghidra-style, all exact |
| msl/critical_regions_win32.c | C | A | S | 4/4 | yes | Ghidra-style hand decompilation of MSL critical_regions.win32.c |
| msl/strtoul.c | C | A | S | 4/4 | yes | MSL/runtime library source via #include wrapper plus a placeholder-named atoi |
| optimizer/Alias.c | C | C | M | 11/21 | no | Pro7 Alias.c plus 2.6 additions written from the disassembly |
| optimizer/CIRTransform.c | C | A | S | 3/3 | yes | 1.x-style stand-in for Pro7 Common/CIRTransform.c's only live function; boundary and build flag wrong |
| optimizer/CodeMotion.c | C | C | M | 12/19 | no | Pro7 middle (exact) between 1.x use-def code and 1.x tail |
| optimizer/IroLinearForm.c | C | A | M | 34/34 | yes | Pro7 logic in binary order (not deferred) under 1.x names and duplicated helpers |
| optimizer/IroLoop.c | C | B | M | 30/33 | no | Pro7 IroLoop.c in binary order (not deferred); Reducable is a non-Pro7 body missing 2.6 code |
| optimizer/IroPointerAnalysis.c | C | C | L | 187/196 | no | Pro7 IroPointerAnalysis.c with IroPointerAnalysisADTs.c #included, not deferred, laid out by first use |
| optimizer/IroTransform.c | C | C | M | 26/31 | no | Pro7 logic in Pro7 order, deferred, but spelled through 1.x-style alias helpers and scaffolding |
| optimizer/IroUnrollLoop.c | C | B | L | 20/22 | no | Pro7-derived bodies in binary order (not deferred) with invented helpers; three rows missing |
| optimizer/LoadDeletion.c | C | A | S | 4/4 | yes | Pro7 rows plus 1.x load-liveness code; deletedeadloads elsewhere |
| optimizer/LoopDetection.c | C | B | M | 13/17 | no | gc1/1.x bodies close to Pro7, non-deferred, with hand-unrolled recursion |
| optimizer/StructMoves.c | C | C | M | 6/12 | no | Pro7 StructMoves bodies with gaps and an invented helper |
| optimizer/UseDefChains.c | C | B | L | 7/13 | no | 2.6 bodies (Pro7 + sparse bit-vectors) under placeholders; 1.x computeusedefchains |
| optimizer/VectorArraysToRegs.c | C | C | M | 8/9 | no | mixed: some Pro7 bodies, some IDA-pseudocode stand-ins |
| backend/BitVectors.c | B | A | S | 8/8 | yes | 2.6 bitvector functions, partly decompiler-shaped, binary order |
| backend/CodeGen.c | B | C | M | 37/43 | no | Pro7's CodeGen.c with 2.6 EABI additions read from the disassembly, deferred |
| backend/DumpIR.c | B | A | S | 2/2 | yes | 2.6's empty DumpIR stubs |
| backend/GCCInlineAsm.c | B | A | S | 3/3 | yes | Pro7's GCCInlineAsm.c, binary order, non-deferred |
| backend/InterferenceGraph.c | B | B | S | 3/6 | no | Pro7's InterferenceGraph.c, deferred |
| backend/Intrinsics.c | B | B | S | 29/30 | no | Pro7's Intrinsics.c in 2.6 shape, deferred, with 2.6's ids 311-314 and generated type tables |
| backend/LiveVariables.c | B | B | S | 3/4 | no | Pro7's LiveVariables.c with 2.6's global-register additions, deferred |
| backend/MachineSimulation7400.c | B | B | S | 6/7 | no | Pro7's MachineSimulation7400.c with 2.6 tables |
| backend/PCode.c | B | A | S | 17/17 | yes | Pro7's PCode.c in binary order, built non-deferred |
| backend/Peephole.c | B | C | S | 44/49 | no | Pro7's Peephole.c in 2.6 shape, deferred, with 2.6 additions |
| backend/RegisterInfo.c | B | A | S | 23/23 | yes | Pro7's RegisterInfo.c with 2.6's global register variables, deferred |
| backend/Registers.c | B | A | S | 8/8 | yes | Pro7's Registers.c in binary order, built non-deferred |
| backend/SpillCode.c | B | C | S | 3/11 | no | Pro7's SpillCode.c with 2.6 paired-single spills, deferred |
| backend/StackFrame.c | B | C | M | 26/41 | no | Pro7's StackFrame.c with 2.6's EABI frame and #pragma interrupt code, deferred |
| backend/Switch.c | B | A | S | 9/9 | yes | Pro7's Switch.c, deferred |
| driver/CCompiler.c | B | A | S | 4/4 | yes | Pro7 CCompiler.c with 2.6 licence/EABI changes |
| driver/CLDependencies.c | B | A | S | 20/20 | yes | Pro7 CLDependencies.c |
| driver/CLLoadAndCache.c | B | A | S | 3/3 | yes | Pro7 CLLoadAndCache.c |
| driver/CLMain.c | B | A | S | 28/28 | yes | Pro7 CLMain.c with 2.6 changes |
| driver/CLPlugins.c | B | A | S | 40/40 | yes | Pro7 CLPlugins.c |
| driver/CLPrefs.c | B | A | S | 5/5 | yes | Pro7 CLPrefs.c |
| driver/CLTarg.c | B | A | S | 4/4 | yes | Pro7 CLTarg.c |
| driver/CLToolExec.c | B | A | S | 7/7 | yes | Pro7 CLToolExec.c |
| driver/CLWriteObjectFile.c | B | A | S | 2/2 | yes | Pro7 CLWriteObjectFile.c |
| driver/COSToolsCLT.c | B | A | S | 24/24 | yes | Pro7 COSToolsCLT.c |
| driver/CWParserPluginsPrivate.cpp | B | A | S | 16/16 | yes | Pro7 CWParserPluginsPrivate.cpp |
| driver/CWPluginsPrivate.cpp | B | A | S | 29/29 | yes | Pro7 CWPluginsPrivate.cpp |
| driver/CWSecretPluginCallbacks.c | B | A | S | 4/4 | yes | Pro7 CWSecretPluginCallbacks.cpp |
| driver/ClientGlue.c | B | A | S | 10/10 | yes | Pro7 ClientGlue.c |
| driver/CodeGenOptPPC.c | B | A | S | 4/4 | yes | Pro7 CodeGenOptPPC.c |
| driver/DropInCompilerLinkerPrivate.cpp | B | A | S | 18/18 | yes | Pro7 DropInCompilerLinkerPrivate.cpp |
| driver/Help.c | B | A | S | 18/18 | yes | Pro7 Help.c |
| driver/IO.c | B | A | S | 2/2 | yes | Pro7 IO.c |
| driver/Memory.c | B | A | S | 15/15 | yes | Pro7 MacEmul/Memory.c |
| driver/OptimizerHelpers.c | B | A | S | 3/3 | yes | Pro7 OptimizerHelpers.c |
| driver/Parameter.c | B | A | S | 55/55 | yes | Pro7 Parameter.c |
| driver/ParserFace.c | B | A | S | 14/14 | yes | Pro7 ParserFace.c |
| driver/ParserGlue-eabi-ppc-cc.c | B | A | S | 4/4 | yes | Pro7 ParserGlue with generated 2.6 option tables |
| driver/ParserHelpers-cc.c | B | A | S | 5/5 | yes | Pro7 ParserHelpers-cc.c |
| driver/ParserHelpers.c | B | A | S | 12/12 | yes | Pro7 ParserHelpers.c |
| driver/Projects.c | B | A | S | 7/7 | yes | Pro7 Projects.c |
| driver/Resources.c | B | A | S | 29/29 | yes | Pro7 MacEmul/Resources.c in 2.6 shape |
| driver/StringUtils.c | B | A | S | 8/8 | yes | Pro7 OSLib/StringUtils.c |
| driver/Targets.c | B | A | S | 4/4 | yes | Pro7 Targets.c (ParserTool functions) |
| driver/TextUtils.c | B | A | S | 4/4 | yes | Pro7 MacEmul/TextUtils.c |
| driver/ToolHelpers-cc.c | B | A | S | 2/2 | yes | Pro7 ToolHelpers-cc.c |
| driver/Utils.c | B | B | S | 6/7 | no | Pro7 Utils.c |
| driver/WarningHelpers.c | B | A | S | 2/2 | yes | Pro7 WarningHelpers.c |
| frontend/CABI.c | B | B | M | 36/37 | no | Pro7 CABI.c in 2.6 shape, deferred |
| frontend/CBrowse.c | B | A | S | 21/21 | yes | Pro7 CBrowse.c, deferred |
| frontend/CClass.c | B | A | S | 67/67 | yes | Pro7 CClass.c in 2.6 shape, deferred |
| frontend/CExpr.c | B | B | S | 87/90 | no | Pro7 CExpr.c in 2.6 shape, deferred |
| frontend/CExpr2.c | B | C | M | 81/85 | no | Pro7 CExpr2.c in 2.6 shape, deferred |
| frontend/CExprConvMatch.c | B | B | S | 26/27 | no | Pro7 CExprConvMatch.c in 2.6 shape, deferred |
| frontend/CMangler.c | B | A | S | 18/18 | yes | Pro7 CMangler.c in 2.6 shape, deferred |
| frontend/CPrep.c | B | C | S | 89/92 | no | Pro7's CPrep.c in 2.6 shape (deferred); about 54% of its non-trivial lines are Pro7's text and about 3% are gc1's |
| frontend/CPrepTokenizer.c | B | B | S | 68/69 | no | Pro7's CPrepTokenizer.c (about 73% Pro7 text, no gc1-only lines), deferred |
| frontend/CPreprocess.c | B | A | S | 7/7 | yes | Pro7's CPreprocess.c (about 87% Pro7 text) plus two 2.6 helpers |
| frontend/CRTTI.c | B | A | S | 14/14 | yes | Pro7 CRTTI.c in 2.6 shape, deferred |
| frontend/CTemplateFunc.c | B | A | S | 24/24 | yes | Pro7 CTemplateFunc.c bodies with invented template type names, deferred |
| frontend/CTemplateTools.c | B | B | M | 42/43 | no | Pro7 CTemplateTools.c bodies with gc1/invented template type names, deferred |
| frontend/CompilerTools.c | B | A | S | 48/48 | yes | Pro7 CompilerTools.c, deferred (2.6's split: LinkErrorsPPC_EABI.c and COSToolsCLT.c are separate) |
| msl/ThreadLocalData.c | B | A | S | 5/5 | yes | MSL Win32 ThreadLocalData.c (updater's), adapted to 2.x's error path |
| msl/time_win32.c | B | A | S | 4/4 | yes | MSL/runtime library source via #include wrapper plus one hand-written function |
| optimizer/AddPropagation.c | B | A | S | 4/4 | yes | Pro7 AddPropagation.c, deferred |
| optimizer/COptimizer.c | B | A | S | 12/12 | yes | Pro7 COptimizer.c, deferred, 2.6 shape |
| optimizer/CopyPropagation.c | B | A | S | 9/9 | yes | Pro7 CopyPropagation.c (with Propagation.c folded in), deferred |
| optimizer/IROUseDef.c | B | C | M | 22/24 | no | Pro7 IROUseDef.c in Pro7 order, deferred, plus a 2.6 helper |
| optimizer/IroBitVect.c | B | A | S | 12/12 | yes | Pro7 IroBitVect.c written in binary order (not deferred) |
| optimizer/IroDump.c | B | A | S | 8/8 | yes | Pro7 IroDump.c, deferred, with 2.6's dead-function shapes |
| optimizer/IroEval.c | B | A | S | 14/14 | yes | Pro7 IroEval.c, deferred |
| optimizer/IroExprRegeneration.c | B | B | M | 18/21 | no | Pro7 IroExprRegeneration.c, deferred |
| optimizer/IroFlowgraph.c | B | A | S | 6/6 | yes | Pro7 IroFlowgraph.c, deferred |
| optimizer/IroPropagate.c | B | C | S | 8/9 | no | Pro7 IroPropagate.c, deferred |
| optimizer/LoopOptimization.c | B | B | S | 13/17 | no | Pro7 LoopOptimization.c bodies, non-deferred |
| optimizer/StrengthReduction.c | B | A | S | 18/18 | yes | Pro7 StrengthReduction.c, deferred |
| runtime/ExceptionX86.cpp | B | A | S | 26/26 | yes | copy of the Pro 5.3 updater's ExceptionX86.cpp, trimmed to 2.6 |
| runtime/NewHand.cpp | B | A | S | 1/1 | yes | MSL NewHand.cpp via #include (older revision than 2.6's) |
| runtime/exchand.cpp | B | A | S | 3/3 | yes | later revision of the runtime's exchand, written from the disassembly |
| runtime/newop.cpp | B | A | M | 4/4 | yes | MSL newop.cpp via #include |
| runtime/setupargs.c | B | A | S | 3/3 | yes | runtime setupargs, hand-written (no lib copy used) |
| backend/Coloring.c | A | A | S | 5/5 | yes | Pro7's Coloring.c, deferred |
| backend/FuncLevelAsmPPC.c | A | A | S | 4/4 | yes | Pro7's FuncLevelAsmPPC.c with 2.6 additions, deferred |
| backend/MachineSimulation601.c | A | A | S | 6/6 | yes | Pro7's MachineSimulation file, deferred, with tables taken from the original's bytes |
| backend/MachineSimulation603.c | A | A | S | 6/6 | yes | Pro7's MachineSimulation file, deferred, with tables taken from the original's bytes |
| backend/MachineSimulation603e.c | A | A | S | 6/6 | yes | Pro7's MachineSimulation file, deferred, with tables taken from the original's bytes |
| backend/MachineSimulation604.c | A | A | S | 6/6 | yes | Pro7's MachineSimulation file, deferred, with tables taken from the original's bytes |
| backend/MachineSimulation750.c | A | A | S | 6/6 | yes | Pro7's MachineSimulation file, deferred, with tables taken from the original's bytes |
| backend/MachineSimulation821.c | A | A | S | 6/6 | yes | Pro7's MachineSimulation file, deferred, with tables taken from the original's bytes |
| backend/PPCError.c | A | A | S | 4/4 | yes | Pro7's PPCError.c, deferred |
| backend/ScanFloatX86.c | A | A | S | 1/1 | yes | new this round (not audited) |
| driver/Arguments.c | A | A | S | 25/25 | yes | Pro7 Arguments.c |
| driver/CLAccessPaths.c | A | A | S | 18/18 | yes | Pro7 Project/CLAccessPaths.c |
| driver/CLErrors.c | A | A | S | 9/9 | yes | Pro7 CLErrors.c |
| driver/CLOverlays.c | A | A | S | 17/17 | yes | Pro7 Project/CLOverlays.c |
| driver/CLProj.c | A | A | S | 2/2 | yes | Pro7 Project/CLProj.c |
| driver/CLSegs.c | A | A | S | 8/8 | yes | Pro7 Project/CLSegs.c |
| driver/CLStaticMain.c | A | A | S | 1/1 | yes | Pro7 CLStaticMain.c |
| driver/ErrMgr.c | A | A | S | 2/2 | yes | Pro7 MacEmul/ErrMgr.c |
| driver/FileHandles.c | A | A | S | 5/5 | yes | Pro7 OSLib/FileHandles.c |
| driver/Files.c | A | A | S | 22/22 | yes | Pro7 MacEmul/Files.c |
| driver/Generic.c | A | A | S | 12/12 | yes | Pro7 OSLib/Generic.c |
| driver/LowMem.c | A | A | S | 2/2 | yes | Pro7 MacEmul/LowMem.c |
| driver/MacFileTypes.c | A | A | S | 6/6 | yes | Pro7 OSLib/MacFileTypes.c |
| driver/MacSpecs.c | A | A | S | 15/15 | yes | Pro7 OSLib/MacSpecs.c |
| driver/MemUtils.c | A | A | S | 5/5 | yes | Pro7 OSLib/MemUtils.c |
| driver/ParserErrors.c | A | A | S | 13/13 | yes | Pro7 ParserErrors.c |
| driver/ResourceStrings.c | A | A | S | 2/2 | yes | Pro7 ResourceStrings.c |
| driver/StaticParserGlue.c | A | A | S | 2/2 | yes | Pro7 StaticParserGlue.c |
| driver/StdTargetWarningHelpers-cc.c | A | A | S | 2/2 | yes | Pro7 StdTargetWarningHelpers-cc.c |
| driver/StringExtras.c | A | A | S | 4/4 | yes | Pro7 OSLib/StringExtras.c |
| driver/cc-eabi-ppc-mw.c | A | A | S | 5/5 | yes | 2.x static plugin table (Wii module cc-eppc-mw) |
| driver/cc-eabi-ppc.c | A | A | S | 17/17 | yes | Pro7 CCompiler-plugin glue (cc-mach-ppc analogue) with EABI names |
| driver/libimp-eabi-ppc.c | A | A | S | 11/11 | yes | Pro7 libimp-mach-ppc analogue |
| frontend/CScope.c | A | A | S | 68/68 | yes | Pro7 CScope.c in 2.6 shape, deferred |
| msl/abort_exit.c | A | A | S | 4/4 | yes | MSL/runtime library source via #include wrapper |
| msl/alloc_g.c | A | A | S | 20/20 | yes | MSL/runtime library source via #include wrapper, with unit-prefix #defines for statics |
| msl/ansi_files.c | A | A | S | 5/5 | yes | MSL/runtime library source via #include wrapper |
| msl/ansifp_x86.c | A | A | S | 3/3 | yes | reconstruction of MSL's sourceless ansifp_x86.obj |
| msl/arith.c | A | A | S | 7/7 | yes | MSL/runtime library source via #include wrapper |
| msl/assert.c | A | A | S | 1/1 | yes | MSL assert.c, 2.6's console branch, hand-written |
| msl/buffer_io_g.c | A | A | S | 6/6 | yes | MSL/runtime library source via #include wrapper |
| msl/char_io.c | A | A | S | 5/5 | yes | MSL/runtime library source via #include wrapper |
| msl/ctype.c | A | A | S | 3/3 | yes | MSL/runtime library source via #include wrapper |
| msl/direct_io_g.c | A | A | S | 2/2 | yes | MSL/runtime library source via #include wrapper |
| msl/extras.c | A | A | S | 2/2 | yes | MSL/runtime library source via #include wrapper |
| msl/fcntl_win32_g.c | A | A | S | 1/1 | yes | MSL/runtime library source via #include wrapper |
| msl/file_io.c | A | A | S | 5/5 | yes | MSL/runtime library source via #include wrapper |
| msl/file_io_win32_g.c | A | A | S | 7/7 | yes | MSL/runtime library source via #include wrapper |
| msl/file_pos_g.c | A | A | S | 4/4 | yes | MSL/runtime library source via #include wrapper |
| msl/mbstring.c | A | A | S | 3/3 | yes | MSL/runtime library source via #include wrapper |
| msl/mem.c | A | A | S | 4/4 | yes | MSL/runtime library source via #include wrapper |
| msl/misc_io.c | A | A | S | 2/2 | yes | MSL/runtime library source via #include wrapper |
| msl/pool_alloc_win32_g.c | A | A | S | 2/2 | yes | MSL/runtime library source via #include wrapper |
| msl/printf_g.c | A | A | S | 15/15 | yes | MSL/runtime library source via #include wrapper, with unit-prefix #defines for statics |
| msl/qsort_g.c | A | A | S | 1/1 | yes | MSL/runtime library source via #include wrapper |
| msl/scanf.c | A | A | S | 6/6 | yes | MSL/runtime library source via #include wrapper, with unit-prefix #defines for statics |
| msl/setjmp_x86.c | A | A | S | 2/2 | yes | MSL/runtime library source via #include wrapper |
| msl/signal.c | A | A | S | 2/2 | yes | MSL/runtime library source via #include wrapper |
| msl/startup_win32.c | A | A | S | 2/2 | yes | MSL/runtime library source via #include wrapper |
| msl/string.c | A | A | S | 11/11 | yes | MSL/runtime library source via #include wrapper |
| msl/string_io.c | A | A | S | 3/3 | yes | MSL/runtime library source via #include wrapper |
| msl/strtold.c | A | A | S | 2/2 | yes | MSL/runtime library source via #include wrapper |
| msl/sysenv_win32.c | A | A | S | 1/1 | yes | MSL/runtime library source via #include wrapper |
| msl/time_g.c | A | A | S | 17/17 | yes | MSL/runtime library source via #include wrapper (patched later-revision time/gmtime/localtime) |
| msl/unistd_win32.c | A | A | S | 4/4 | yes | MSL/runtime library source via #include wrapper |
| msl/wchar_io.c | A | A | S | 4/4 | yes | MSL/runtime library source via #include wrapper |
| msl/wcstold.c | A | A | S | 1/1 | yes | MSL/runtime library source via #include wrapper |
| msl/wcstoul.c | A | A | S | 2/2 | yes | MSL/runtime library source via #include wrapper |
| msl/wctype.c | A | A | S | 2/2 | yes | MSL/runtime library source via #include wrapper |
| msl/wscanf.c | A | A | S | 4/4 | yes | MSL/runtime library source via #include wrapper, with unit-prefix #defines for statics |
| optimizer/IrOptimizer.c | A | A | S | 6/6 | yes | Pro7 IrOptimizer.c, deferred |
| optimizer/IroCSE.c | A | A | S | 18/18 | yes | Pro7 IroCSE.c, deferred |
| optimizer/IroEmptyLoop.c | A | A | S | 7/7 | yes | Pro7 IroEmptyLoop.c, deferred |
| optimizer/IroJump.c | A | A | S | 6/6 | yes | Pro7 IroJump.c, deferred |
| optimizer/IroMalloc.c | A | A | S | 12/12 | yes | Pro7 IroMalloc.c (MSL alloc.c's allocator), deferred |
| optimizer/IroRangePropagation.c | A | A | S | 5/5 | yes | Pro7 IroRangePropagation.c, deferred |
| optimizer/IroSubable.c | A | A | S | 2/2 | yes | Pro7 IroSubable.c, deferred |
| optimizer/IroUtil.c | A | A | S | 55/55 | yes | Pro7 IroUtil.c, deferred, with 2.6's IRO_FindFirst split |
| optimizer/IroVars.c | A | A | S | 23/23 | yes | Pro7 IroVars.c, deferred |
| runtime/LongLongx86.c | A | A | S | 5/5 | yes | MSL/runtime library source via #include wrapper |
| runtime/NMWException.cpp | A | A | S | 5/5 | yes | MSL/runtime library source via #include wrapper, with unit-prefix #defines for statics |
| runtime/arrcondes.cpp | A | A | S | 1/1 | yes | MSL/runtime library source via #include wrapper |
| runtime/delarrop.cpp | A | A | S | 1/1 | yes | MSL/runtime library source via #include wrapper |
| runtime/delop.cpp | A | A | S | 1/1 | yes | MSL/runtime library source via #include wrapper |
| runtime/globdest.c | A | A | S | 1/1 | yes | MSL/runtime library source via #include wrapper |
| runtime/maincrt.c | A | A | S | 1/1 | yes | MSL/runtime library source via #include wrapper |
| runtime/runinit.c | A | A | S | 1/1 | yes | MSL/runtime library source via #include wrapper |
| runtime/stackall.c | A | A | S | 2/2 | yes | MSL/runtime library source via #include wrapper |

## D: rewrite

### src/backend/CMachine.c

D, effort M, confidence high; 36/48 exact, not linked. gc1 (1.x) CMachine with 2.6 EABI additions; core layout/alignment code is 1.x.

About half the lines are gc1-only and few are Pro7's. 36/48 exact, but those are mostly small arithmetic and float/int helpers. The struct layout and alignment core is 1.x: CMach_StructLayoutBitfield, the GetQualified*Align family, MemberAlignment, InitVectorMem. It runs on 1.x globals (data_00580fa0 = Pro7 cmach_curbfsize, data_00580fa4 = cmach_curbfoffset, structLayoutOffset = cmach_structoffset, maximumAlignment = cmach_structalign, bitfield_storage_size = cmach_curbfbasesize). The binary is Pro7's order reversed, so 2.6 built the unit deferred. CMach_ParseGlobalRegisterVariable (2 KB, a 2.6 addition) has no body.

Functions:
- `CMach_ParseGlobalRegisterVariable` 0.0: unbuilt: 2.6 addition (global register variables), write from the disassembly
- `CMach_GetQualifiedStructAlign` 41.6: 1.x body; Pro7's has the PPC/Win alignment statics inlined
- `fn_004c04a0` 43.6: 2.6 helper called by CMach_StructLayoutBitfield (asserts 1624/1669); ours is a gc1-shaped 'GCC-style' layout on 1.x globals; rewrite from the disassembly with Pro7's cmach_* statics
- `CMach_StructLayoutBitfield` 54.4: 1.x body; 2.6 is Pro7's with the split to fn_004c04a0
- `CMach_InitVectorMem` 60.2: 1.x body
- `CMach_GetQualifiedClassAlign` 64.1: 1.x body
- `CMach_ArgumentAlignment` 71.2: 1.x/shape
- `CMach_GetQualifiedTypeAlign` 72.0: 1.x body
- `CMach_StructLayoutGetOffset` 78.9: 1.x globals; Pro7 body
- `CMach_GetMemberAlignment` 80.0: 1.x/shape
- `CMach_FloatIsPowerOf2` 81.1: compares against ten `static double data_0055fd18...` stand-ins; Pro7 uses literals
- `CMach_AllocationAlignment` 87.1: shape

Names: fn_004c04a0 (2.6 helper; needs a name). fn_004bf0a0 is Wii's CMach_GetCustomType (docs/names-2.6.md). The data placeholders are worst: about 90 data_0055f*/0055fb* HashNameNode fragments written by CMach_ReInitRuntimeObjects are Pro7's stvector*.name and stVUC_unsignedchar0.name ... member fields. data_00580fa0/fa4, data_005e7168/9, data_005e71ae, data_005dc468, data_00560028 and the get_type_align leftover also need real names.

Data: Bad. The 'Strings (generated from the original's .data)' block (src/backend/CMachine.c:186-213) defines string literals as global arrays ("[0]".."[15]", "vector unsigned char" ...). Pro7 writes them inline. The vector TypeStruct/StructMember objects are split into about 90 HashNameNode * fragments. 71 named and 88 anonymous raw items (2.1 KB) remain in the 3.5 KB .data range. `char cmachine_filename[]` is used for asserts.

Structure: Should be -inline auto,deferred in Pro7's order: the binary order is Pro7's reversed, and Pro7's statics MemberAlignValue/GetPPCTypeAlign/GetWinTypeAlign are inlined. The current source order is neither the binary's nor Pro7's.

Hacks:
- src/backend/CMachine.c:186 cmachine_filename used in CError_Internal (CERROR_FILE-style)
- src/backend/CMachine.c:186-213 string literals as global arrays
- src/backend/CMachine.c:228-327 struct fields written through ~90 data_* fragment globals
- src/backend/CMachine.c:468-477 ten `static double data_0055fd..` stand-ins for literals
- include/compiler/CMachine.h: 93 fn_/data_ placeholder externs
- 3 `volatile` uses without TODO (hacks count)
- get_type_align: non-static, no row, unused elsewhere (1.x leftover)

Next:
1. Rewrite from Pro7's CMachine.c in Pro7's order, -inline auto,deferred, with Pro7's cmach_* statics and the stvector*/stVUC_* type objects defined here
2. Keep 2.6's additions from the exact bodies (CMach_SetBackEndOptions/GetBackEndOptions, Type_RequiresMemoryReturn, CMach_GetCustomType, CMach_IsUnsignedType, CustomIntConvert, GetCPU's EABI CPUs)
3. Write CMach_ParseGlobalRegisterVariable and fn_004c04a0 from the disassembly
4. Delete the strings block, the double stand-ins, cmachine_filename and the data_* fragment bindings

### src/backend/DWARF.c

D, effort L, confidence high; 13/38 exact, not linked. gc1's 1.x DWARF.c, missing 2.6's EABI/Gekko additions.

13/38 exact, 17% of bytes. Most inexact rows are 1.x bodies missing what 2.6 added: Gekko paired-single types (the cmp [type+6], 0x13 tests in get_type_dwarf_ref, newDWARF_param and others), global register variables (emit_variable_entry calls Registers_GetRegInfo in 2.6), and soft-float register pairs. emit_class_dwarf goes the other way: ours emits far more (16 EmitName, recursion, CMangler_GetLinkName) than 2.6's body. Diffs show 2.6-only blocks in nearly every inexact function. It needs rewriting from the disassembly, function by function.

Functions:
- `emit_variable_entry` 25.2: 1.x body: missing 2.6's Registers_GetRegInfo global-register branch and an assert (0x9c7)
- `newDWARF_param` 39.8: missing 2.6 branches: paired-single type test (0x13), a flag OR, and register calls
- `DWARF_004ad570` 52.6: missing 2.6 blocks (38 original-only instructions)
- `newDWARF_local` 55.1: missing 2.6 blocks like newDWARF_param's
- `emit_class_dwarf` 55.3: wrong or 1.x body: ours has 278 instructions the original lacks (extra name, mangled-name and recursion code)
- `DWARF_004b0a80` 58.4: mixed: 93 original-only and 41 ours-only instructions
- `emit_entry_header` 64.7: shape and missing pieces
- `emit_function_entry` 65.8: missing 2.6 blocks (47 original-only instructions)
- `set_type_dwarf_ref` 66.7: ours has extra code (26 ours-only instructions)
- `tweakDWARF_file` 68.5: mixed differences
- `DWARF_004ad260` 73.9: missing 2.6 blocks (paired singles / register pairs)
- `get_type_dwarf_ref` 73.8: missing 2.6's paired-single (0x13) type case and a 0x72 tag branch
- `init_dwarf_state` 75.0: missing 2.6 code
- `emit_dwarf_ref` 76.6: missing 2.6 code
- `endDWARF_function` 78.0: missing 2.6 code
- `create_type_dwarf_ref` 78.2: missing 2.6 code
- `newDWARF_function` 78.4: missing 2.6 code (46 original-only instructions)
- `emit_array_type` 78.4: near-miss
- `setup_return_operand` 80.8: missing 2.6 return-register cases (27 original-only instructions)
- `emit_enum_type` 80.0: near-miss

Names: Placeholders: DWARF_004ad260, DWARF_004ad570, DWARF_004b0a80 (1.x addresses). 24 rows are gc1 names unconfirmed by Wii (statics). Data: data_00580fb0[50], data_00581078[50], data_00581140, data_0058114e, data_00587168, DAT_00587698, data_00587ea8, data_005604a4/a8/ac. DWARF tags and attributes are bare numbers (0x38, 0x2008 ...), not DW_TAG/DW_AT names.

Data: Placed, but with 12 anonymous raw ranges (528 bytes) and 7 raw named. dwarf_filename is the "DWARF.c" literal bound as a global at 0x5cf750, with '#define data_00560530 (dwarf_filename + 8)' reaching the next pooled string: a literal stand-in.

Structure: Binary order with 2 misplacements (correctDWARF_line/newDWARF_line, DWARF_DeclareData). gc1 builds it deferred in the reverse order, so 2.6 was likely deferred (with -sym off). There are 18 non-row helpers (SetScope, InitOffsets, WritePosition, PatchDwarfLength, EmitName, DW_String ...): check that each is inlined.

Hacks:
- src/backend/DWARF.c:1 #define CERROR_FILE "DWARF.c" (redundant), and :373, :1322 #define CERROR_FILE dwarf_filename
- src/backend/DWARF.c:1419 #define data_00560530 (dwarf_filename + 8): offset into a bound literal
- src/backend/DWARF.c:1540 #define S dwarf_info_buffer: alias macro
- src/backend/DWARF.c:742 volatile DwarfRef *tagRef without a TODO
- src/backend/DWARF.c:989,1489 CError_Internal(dwarf_filename, 0x6c3) literal-line asserts
- 77 braceless ifs; stale generated comments ('The record at argument 2 holds the type at offset 0 ...', 'Declarations gathered from the merged files')

Next:
1. Rewrite the inexact functions from the disassembly, starting with get_type_dwarf_ref, newDWARF_param/local and emit_variable_entry (they share the paired-single and register-pair additions)
2. Rewrite emit_class_dwarf from the disassembly (ours emits extra entries)
3. Unbind dwarf_filename (make it the literal) and remove the CERROR_FILE, S and data_00560530 macros
4. Name the 1.x-address statics and .bss arrays; try deferred in gc1's order

### src/backend/Exceptions.c

D, effort M, confidence high; 5/8 exact, not linked. gc1 (1.x) Exceptions.c with invented types; Pro7 file unused.

About 324 of 529 lines are gc1-only and 5 are Pro7-only. 5/8 exact (the small list routines), but allocateactioninfo (5.6 KB, 41%), noteexceptionactionregisters (43%) and dumpexceptiontables (63%) are 1.x bodies. They call fn_004a9f70/fn_004a9f90, which are declared in StackFrame.h and defined nowhere. 2.6 calls Pro7's local_offset_32/local_offset_16/local_is_16bit_offset (15/17/15 call sites). The binary is Pro7's order reversed, so the unit was deferred; the repo builds it non-deferred in binary order.

Functions:
- `allocateactioninfo` 40.9: 1.x body: uses undefined fn_004a9f70/fn_004a9f90 and append_reference; 2.6 = Pro7 (local_offset_32/16, local_is_16bit_offset, addrelocation) plus Registers_GetVarInfo checks
- `noteexceptionactionregisters` 42.8: 1.x body; Pro7's walks ExceptionAction kinds with OBJECT_REG
- `dumpexceptiontables` 63.0: 1.x body (flags() helper); Pro7 + ObjGen_DeclareExceptionTables

Names: Rows are Pro7's. Types and statics are invented: CException (Pro7 ExceptionAction), ObjectGroup (EANode), ExceptionScopeEntry (PCAction), exception_scope_entries/last_exception_scope_entry (pc_actions/last_pc_action), exception_records (exceptmodule), exception_table_relocation_requests/relocation_request_tail (except_refs/last_except_ref), object_groups[18] (DAG). include/compiler/Exceptions.h defines about 14 invented record structs.

Data: The .bss globals are Pro7's statics under invented names and kept non-static. 80 raw anonymous items (3 KB) are in its .data range.

Structure: Should be -inline auto,deferred in Pro7's order (binary: dump, merge, record, note, count, init, allocate, makeEAnode = Pro7 reversed; Pro7's findPC/findPC_long/addrelocation/deleteexceptionaction are inlined statics).

Hacks:
- src/backend/Exceptions.c:452-723 about 34 calls to undefined fn_004a9f70/fn_004a9f90
- src/backend/Exceptions.c:43 `void * object_groups[72/sizeof(void *)]` generated-size array (Pro7 EANode *DAG[EAT_NACTIONS])
- src/backend/Exceptions.c:46-58 #pragma opt_lifetimes off/reset and an unused CE_ASSERT macro
- include/compiler/Exceptions.h: invented 1.x record types

Next:
1. Rewrite from Pro7's Exceptions.c (857 lines) in Pro7's order, -inline auto,deferred, with Pro7's statics and types (PCAction, EANode, ExceptionAction)
2. Add 2.6's Registers_GetVarInfo/in-param checks and EABI table emission from the disassembly
3. Delete fn_004a9f70/fn_004a9f90 from StackFrame.h once Exceptions, PCodeAssembly and ObjGen stop using them

### src/backend/InlineAsmPPC.c

D, effort L, confidence high; 13/26 exact, not linked. gc1 (1.x) InlineAsmPPC.c under 2.6 row names, gc1 types.

The source is the gc1 decomp's InlineAsmPPC with its invented types (ParsedAsmInstruction, EncodedOperand, AsmOut, InlineAsmExpression) and helpers (recovery_inline_eval/_ranged/_crbit/_isreg, parse_asm_instruction_operands), not Pro7's InlineAsm/IAOperand/IAEffects. 13/26 exact, but the six largest functions (19 KB) are 0-66% and InlineAsm_ScanAssemblyOperands (4.6 KB) has no body under its row name: the 1.3-shaped body sits under parse_asm_instruction_operands. The binary's order is Pro7's reversed, so the unit is deferred, but it is built plain -inline auto in binary order.

Functions:
- `InlineAsm_ScanAssemblyOperands` 0.0: unbuilt: 1.3 body under the name parse_asm_instruction_operands; rewrite from Pro7 InlineAsmPPC.c:979
- `CodeGen_GetAsmEffects` 26.5: 1.x body on gc1's AsmOut with invented, apparently swapped field names (writesMemory set from fIsRead); Pro7's IAEffects/VarInfo form
- `InlineAsm_TranslateIRtoPCodePPC` 31.7: 1.x body (InlineAsm_Register/RegisterHi helpers, gc1 operand kinds)
- `registeroperand` 51.6: 1.x body (int regClass/isOutput params; Pro7 has char rclass, short effect)
- `immediateoperand` 61.9: 1.x shape; returns int where Pro7 is void and fills IAOperand
- `InlineAsm_OpcodeSize` 66.3: 1.x body on ParsedAsmInstruction
- `memoryoperand` None: inexact (60-85 band); 1.x signature (no effect param); rewrite with the rest
- `InlineAsm_TranslateIRtoPCode` None: indexes a 4-entry stand-in data_005654b8[] with raw (char*)0x005671f4 pointers by opcode (out of bounds); Pro7 uses opcodeinfo[ia->opcode].x8 and assembler_type
- `InlineAsm_ProcessDirective / InlineAsm_IsDirective / InlineAsm_ScanAssemblyInstruction / InlineAsm_InitializePPC / PrimaryExpressionPPC` None: inexact gc1 bodies; rewrite from Pro7 with 2.6 additions (cpu mask, altivec_model)

Names: Rows carry Pro7 names, but the code uses gc1's: parse_asm_instruction_operands, encode_expression_operand/parse_branch_operand (#define aliases to imm_or_labeldiff_operand/labeloperand in InlineAsmPPC.h:120-121), recovery_inline_*, IA_* helpers. Globals: data_00587128 is Pro7's `cpu`, data_005654b8 is a fragment standing in for opcodeinfo, inlineAsmMode is Pro7's assembler_type; keyword strings (machine_keyword, entry_keyword ...) are globals where Pro7 has literals at the use. 75 placeholder data names in the file.

Data: 64 anonymous raw items (1380 bytes) in the .data range 0x5b01e8-0x5b0748 plus data_005652f8. `AsmEntry data_005654b8[]` (InlineAsmPPC.c:434) hard-codes the original's string addresses as integers cast to char*, so it is wrong in any rebuilt compiler, and the code indexes it by any opcode. The 'Strings (generated from the original's .data)' block defines literals as globals, and tkfloatconst, asm_alloc_flags and others are defined here as COMMON.

Structure: Should be -inline auto,deferred in Pro7's order: the binary runs CodeGen_CopyAsmStat ... IllegalObjectOperator, which is exactly Pro7's file reversed. parse_displacement_operand (0x4b0da0) is a 2.6 helper between memoryoperand and registeroperand. The range boundaries look right (no foreign functions).

Hacks:
- src/backend/InlineAsmPPC.c:434 AsmEntry data_005654b8[] with (char *)0x005671f4-style raw addresses, indexed by opcode
- src/backend/InlineAsmPPC.c:2934 `((volatile InlineAsmExpression *)right)->type` with no TODO
- include/compiler/InlineAsmPPC.h:120-121 #define encode_expression_operand imm_or_labeldiff_operand / parse_branch_operand labeloperand (alias macros)
- src/backend/InlineAsmPPC.c:57 local `typedef enum AsmRegClass` in the source, after includes
- src/backend/InlineAsmPPC.c:1205-1316 recovery_inline_* static inline helpers (invented, no TODO)
- src/backend/InlineAsmPPC.c:2166 `extern int base_register(Object *obj); /* local_base_register */` local prototype
- src/backend/InlineAsmPPC.c:2406 TODO: 1.x note on CR fields
- ~50 includes, most unused

Next:
1. Bring Pro7's InlineAsm.h/InlineAsmPPC.h types (InlineAsm, IAOperand, IAExpr, IAEffects, IAMnemonic, IARegister) and retire ParsedAsmInstruction/EncodedOperand. This also touches InlineAsm.c, FuncLevelAsmPPC, CodeGen, TOC, GCCInlineAsm, CPrec and CInline.h, which use them.
2. Rewrite the file from Pro7's InlineAsmPPC.c in Pro7 order and switch the unit to -inline auto,deferred; keep 2.6's additions (parse_displacement_operand, cpu-mask checks, altivec_model, the soft-float guard) read from the disassembly
3. Replace data_005654b8 with opcodeinfo[...] and data_00587128 with cpu; turn the keyword globals back into literals
4. update_assert_lines, then trim includes

### src/backend/InlineAsmRegistersPPC.c

D, effort M, confidence high; 4/4 exact, linked, deferred. gc1-style code moved from CTemplateNew.c, invented names.

2/4 exact, written with gc1-era invented structures (RegistrationHashEntry, RegistrationTableEntry, SecondaryRegistrationEntry) and string fragments (data_0057238c...) for what Pro7 has as RegInfo, asm_registers, asm_sprs, spr_cpus, hashedsprs/hasheddcrs, with InlineAsm_InsertSPR/InsertRegisterDCR inlined. InlineAsm_InitializeRegistersPPC (53%) and InlineAsm_LookupRegisterPPCName (78%) need Pro7's bodies.

Functions:
- `InlineAsm_InitializeRegistersPPC` 53.3: gc1 body with invented tables; Pro7's InitializeRegistersPPC + inlined InsertSPR/InsertRegisterDCR
- `InlineAsm_LookupRegisterPPCName` 77.6: gc1 body (struct AsmOperand lookup); Pro7's IALookupResult form

Names: inlineAsmRegisterHashTable/secondary_registration_hash are Pro7's hashedsprs/hasheddcrs, inline_asm_register_masks is spr_cpus, data_00587128 is cpu, data_00582434/inlineAsmRegisterEntry are Pro7's function-local `static IARegister thespr` (and the DCR one), registration_entries/registration_table are asm_registers/asm_sprs. CTemplateNew_ShouldWarnUnsupportedInlineAsmRegister is an invented helper named after the wrong unit.

Data: .data 0x5cedd6-0x5cee2e is marked align:2, a sign of a wrong boundary (an object's .data starts 8-aligned). The register tables (asm_registers, asm_sprs, spr_cpus[1024]) are raw or defined elsewhere.

Structure: Should be deferred in Pro7's order (the binary runs Initialize, DCR, PPC, PPCName: Pro7 reversed).

Hacks:
- src/backend/InlineAsmRegistersPPC.c InlineAsm_LookupRegisterPPC: `void *(*lookupBinding)(unsigned int *) = InlineAsm_LookupRegister;` function-pointer cast to call a function
- string fragments data_0057238c..data_00572397 for the class names
- CTemplateNew_ShouldWarnUnsupportedInlineAsmRegister private wrapper of copts.warn_possunwant
- ~50 includes inherited from CTemplateNew.c

Next:
1. Rewrite from Pro7's InlineAsmRegistersPPC.c with 2.6's tables generated from the original's bytes, deferred
2. Fix the .data boundary (align:2)
3. Bind cpu and the statics properly

### src/backend/MachineSimulationAltiVec.c

D, effort M, confidence high; 4/7 exact, not linked. hand-written from disassembly over raw data fragments.

4/7 exact, but the model's state is ~16 extern data_005dfcXX fragments (Pro7's completionbuffers, fetchqueues, pipeline_altivec structs), an index macro into a raw byte array for instruction_timing, and borrowed MachineSimulation603 types through `at_005ce9a5 + 11`. Its data sits in VectorArraysToRegs' range (AGENTS). It has not been given the Pro7 treatment the other machine models got.

Functions:
- `m7450_advance_clock` 61.0: shape over fragments; rewrite from Pro7's advance_clock with the struct statics
- `m7450_issue` 76.6: same
- `m7450_can_issue` 95.1: near-miss

Names: Rows m7450_* follow the sanctioned machine-model pattern, but the statics are data_005dfc58..data_005dfdd8, DAT_005d1c70 (finalstages), DAT_005d1c7c (pipeline_units), data_005ce9b2 (instruction_timing), opcode_table.

Data: 144 placeholder data mentions and 16 raw named fragments; the .data (timing tables, MachineInfo machine7450) is not defined from source; 8 anonymous raw items.

Structure: Deferred in the binary (uses_vpermute_unit_altivec lowest, latency highest: Pro7 reversed). Find its true .data range (it is said to sit inside VectorArraysToRegs').

Hacks:
- src/backend/MachineSimulationAltiVec.c: #define ALTIVEC_STAGE/ALTIVEC_CYCLES index macros into a raw byte array
- #define machine_opcode_info_603 ((MachineOpcodeInfo603 *)(at_005ce9a5 + 11)) borrowed from MachineSimulation603's file scope
- MIN26 private macro

Next:
1. Do what MachineSimulation7400 got: Pro7's file, its tables from the original's bytes, statics `#define`d to m7450_*, MachineInfo first
2. Fix the .data split against VectorArraysToRegs

### src/backend/Operands.c

D, effort L, confidence high; 13/30 exact, not linked. gc1 (1.x) Operands with raw-offset 2.6 stand-ins.

About 360 of 1022 lines are gc1-only and about 70 Pro7-only. 11/30 exact; all 17 inexact rows are below 82%. Spot checks found wrong bodies: load_floating_constant takes an Object * and does an SDA ha/lo load, where 2.6 (like Pro7) builds an Operand from createfloatconstant. The binary is Pro7's Operands.c order reversed, so 2.6 built it deferred. The range's ends are mis-split: setupaddressing at its start is TOC's, and load_immediate/insert_load_immediate_after (filed under PCodeUtilities) at its end are Operands' (Pro7's first function is load_immediate).

Functions:
- `load_floating_constant` 31.4: wrong body: 2.6 = Pro7 (memclrw Operand, createfloatconstant, symbol_operand/indirect/Coerce_to_fp_register)
- `last_matches_rlwinm_or_exts` 47.9: 1.x stand-in typed (void *operand, ...)
- `convert_integer_to_floating` 53.1: 1.x body (static helpers start/prelude on data_0055e9e8/f0)
- `Coerce_to_fp_register` 57.0: 1.x body
- `extract_bitfield` 61.0: 1.x body
- `fn_004df610` 61.9: 2.6 addition (Wii: I8_extract_bitfield); 1.x-style body with gotos
- `store_v` 62.2: 1.x body
- `Coerce_to_register` 62.4: 1.x body (Operands_GetOpcode helper with CERROR_FILE)
- `fn_004df400` 64.4: 2.6 addition (Wii: I8_insert_bitfield)
- `convert_floating_to_integer` 64.9: 1.x body
- `coerce_to_register_pair` 65.8: 1.x body
- `extend32` 66.0: 1.x body
- `combine / coerce_to_addressable` 70.9: 1.x body
- `extendpair` 74.4: 2.6 addition (Pro7 has extend64); shape
- `store` 80.2: 1.x/shape
- `fn_004e0960` 81.8: 2.6 paired-single load (gecko_load_store_register/psq opcode 0x199); written entirely with raw Operand offsets

Names: Placeholders: fn_004df400 (Wii I8_insert_bitfield), fn_004df610 (Wii I8_extract_bitfield), fn_004e0960, fn_0049f630 (1.x address, no row). Data placeholders: data_00587660, data_00588200, data_005883f0, data_00588508, data_005e6c4c, data_005e6cdc, data_0055e9e8/ec/f0. Operands_IsLogicalExpression has no row (1.x).

Data: Its .bss globals are TOC's (see TOC), not Operands'. setupaddressing does memclrw(data_005883f0, 0x44) on a declared `unsigned char data_005883f0[10]`. That is Pro7's `Object toc0`, so the declared type is wrong and a compiled copy would overrun it (it is bound to the original's address today). Float constants go through raw words data_0055e9e8/f0.

Structure: Should be -inline auto,deferred in Pro7's order (the binary is Pro7's reversed). setupaddressing goes to TOC.c. load_immediate and insert_load_immediate_after (0x4e3d00-0x4e3e10, now PCodeUtilities.c rows) belong at the top of Operands.c. Select_Gecko_Load_Store (0x4ea630) is filed here but sits inside PCodeUtilities' range, between gecko_load_store_register and load_store_register: it is PCodeUtilities'. fn_004df610/fn_004df400 are 2.6 additions after load_address at the file's end.

Hacks:
- src/backend/Operands.c:50,89 data_005883f0 declared [10] and cleared with 0x44 bytes (it is Object toc0)
- src/backend/Operands.c:978 CError_Internal(CERROR_FILE, line) inside Operands_GetOpcode
- src/backend/Operands.c:1520-1540 fn_004e0960 reads Operand/Type fields by raw offset and passes "Operands.c" literally
- src/backend/Operands.c:272 `(volatile int)operand->immOffset` without a TODO
- src/backend/Operands.c:401 symbol_operandInline: a private copy of symbol_operand
- src/backend/Operands.c:194-250 gotos in fn_004df610
- include/compiler/Operands.h: externs of data_* fragments and of Operands_ConvertIntegerToFloat / Operands_EmitSTVX (no definitions)

Next:
1. Move setupaddressing and the TOC globals to TOC.c; move load_immediate/insert_load_immediate_after here; move Select_Gecko_Load_Store to PCodeUtilities.c
2. Rewrite from Pro7's Operands.c in Pro7's order, -inline auto,deferred, keeping 2.6's additions (extendpair, the paired-single load fn_004e0960, I8_insert/extract_bitfield, Coerce_to_v_register's form, store_fp)
3. Name the 2.6 additions from Wii (I8_insert_bitfield, I8_extract_bitfield)

### src/backend/PCodeAssembly.c

D, effort L, confidence medium; 1/7 exact, not linked. 1.x/gc1-style bodies with 2.6 opcode cases.

1/5 exact. insertlongbranches (38.8%) is a 1.x body without Pro7's mask parameter, and assemblepcode (71.7%, 5 KB) is 1.x-style with raw operand-kind numbers and a 1.x fCallerSPRelative flag. optimizefinalbranches (94%) casts codeOffset to PCode** in the 1.x manner. The unit is missing its first two functions (assemblefunction and insert_align_nops, filed under PCodeUtilities), and it is deferred in the binary but built plain.

Functions:
- `insertlongbranches` 38.8: 1.x body; Pro7's takes a mask and switches on fIsBranch ops; 2.6's caller passes (max, min)
- `assemblepcode` 71.7: 1.x-shaped body (raw kinds 4/5, fCallerSPRelative, fn_004a9f70/fn_004a9f90 for Pro7's parametersbase/localsbase); rewrite from Pro7 with 2.6's EABI/Gekko opcodes
- `pcode_update_mem_labeldiff_imm` 89.9: near-miss; calls placeholders fn_004d1040/fn_004d1400 (BE_symbol rows still unnamed)
- `optimizefinalbranches` 94.3: 1.x idioms ((PCode **)blk->codeOffset); align with Pro7 body
- `assemblefunction / insert_align_nops` 75.6: belong here (see PCodeUtilities)

Names: fn_004a9f70/fn_004a9f90 (StackFrame.h: Pro7's parametersbase/localsbase, which 2.6 inlines elsewhere) and fn_004d1040/fn_004d1400 (BE_symbol, OPEN rows). Invented PCodeAssembly_Should* inline helpers and a first_instr helper.

Data: 19 anonymous raw items (2384 bytes, mostly switch jump tables of assemblepcode and insertlongbranches) stay the original's until those match. assembly_buffer_offset is defined as 'generated from .bss'.

Structure: Deferred in the binary: in Pro7's order reversed it starts at 0x4eb120 with assemblefunction and insert_align_nops, now in PCodeUtilities' range. Build it -inline auto,deferred in Pro7's order (pcode_update_mem_labeldiff_imm first).

Hacks:
- src/backend/PCodeAssembly.c:1 #define CERROR_FILE "SFPE_PPC_EABI.c" (the original's asserts use the 'PCodeAssembly.c' literal @391; the define is a mis-split marker)
- src/backend/PCodeAssembly.c:421 #define CError_FATAL redefinition to force the file name
- src/backend/PCodeAssembly.c:47 #define fCallerSPRelative 0x100000 (TODO says it is 1.x)
- src/backend/PCodeAssembly.c:249-251 private fBranchNotTaken/fBranchTaken/PCODE_FLAG_SET_T macros (PCode.h has Pro7's)
- src/backend/PCodeAssembly.c:52 an unused ExpressionGenerator typedef

Next:
1. Take assemblefunction and insert_align_nops in from PCodeUtilities
2. Rewrite the whole file from Pro7's PCodeAssembly.c (8 functions, targetinstruction inlined), deferred; then add 2.6's opcode cases from the disassembly (paired singles, EABI relocations)
3. Drop CERROR_FILE and the CError_FATAL override; update_assert_lines (2.6's lines run ~800 above Pro7's)
4. Name the BE_symbol placeholders

### src/backend/Scheduler.c

D, effort M, confidence high; 10/11 exact, not linked, deferred. gc1 (1.x) Scheduler.c with gc1 types.

This is the gc1 scheduler: CColoringNode/CColoringList/DependencyEntry in place of Pro7's DGNode/DGSuccessor/DGNodeList, and functions under invented names (build_sched_dependencies, add_memory_dependencies, check_and_add_spill_node, fn_004cd7c0, fn_004cd650). Four rows (makedgnode, addsuccessor, serializeregister, serialize_store) have no body under their names. 4/11 exact. Its .bss is ~30 data_/DAT_ fragments.

Functions:
- `makedgnode` 0.0: unbuilt: gc1 code under other names
- `addsuccessor` 0.0: unbuilt
- `serializeregister` 0.0: unbuilt (fn_004cd7c0 in the gc1 code)
- `serialize_store` 0.0: unbuilt (fn_004cd650-ish)
- `findsuccessors` 90.1: gc1's build_sched_dependencies; rewrite from Pro7
- `scheduleinstructions` 91.6: assigns MI through data_00581b80; Pro7 body
- `scheduleblock` None: gc1 body (x0 field, data_00581b7c); Pro7 body

Names: Placeholders: data_00581b00/08/18/24/30/50/7c/80 and DAT_00581b1c..6c are Pro7's statics register_uses/register_defs[RegClassMax], memory_uses, memory_defs, side_effects, volatile_refs, defaultsuccessor, criticalpath, MI. data_005ce994/data_005cf9bc are function-pointer fragments of machine7400/machine7450's uses_vpermute_unit (MachineInfo fields). fn_004cd7c0 and fn_004cd650 are placeholders.

Data: The .bss is declared as split fragments ('Globals (generated from the original's .bss)'): it should be Pro7's 9 statics. 8 raw anonymous items in .data (jump tables).

Structure: Should be deferred in Pro7's order (the binary is Pro7's file reversed: default_uses_vpermute_unit lowest, makedgnode highest).

Hacks:
- src/backend/Scheduler.c:41-73 fragment globals for static arrays
- src/backend/Scheduler.c:76-92 extern function-pointer fragments called with an (int) cast of a PCode*
- gc1 types CColoringNode/CColoringList kept in common.h and Scheduler.h only for this unit

Next:
1. Rewrite from Pro7's Scheduler.c (DGNode etc.) deferred; uses_vpermute_unit through machine7400/machine7450.uses_vpermute_unit
2. Make the statics Pro7's (static once nothing lifted uses them; until then global and bound with the usual comment)
3. Delete CColoringNode/CColoringList/DependencyEntry from common.h and Scheduler.h

### src/backend/TOC.c

D, effort L, confidence high; 17/29 exact, not linked. gc1 (1.x) TOC code under 2.6 rows, with a few Pro7/2.6 bodies (resetTOCvarinfo, used_anchor, TOC_use_fsel).

About 760 of 1619 lines exist only in gc1 and about 200 only in Pro7. The core functions are 1.x: expandTOCreferences uses gc1's ParsedAsmInstruction/EncodedOperand inline-asm types, fn_0049f630 and create_diadic_node_with_constant. 2.6's version is Pro7's: comparewithzero, asm_used_register, Registers_GetVarInfo, plus Type_RequiresMemoryReturn. The binary is Pro7's file order reversed exactly, so 2.6 built it deferred; the repo does not. The unit boundary looks wrong too: setupaddressing (0x4e0d70, filed under Operands) is Pro7's TOC function, Wii puts it in module TOC, and its "TOC" literal lies in TOC's .data range.

Functions:
- `rewrite_opassign` 43.5: 1.x body
- `expandTOCreferences` 45.9: 1.x body (gc1 asm operand types, fn_0049f630, makes_call set at entry); 2.6 = Pro7 + Type_RequiresMemoryReturn
- `DeclarePooledConstants` 52.2: 1.x body built on MemberPointerConstant (Pro7: PoolEntry vectorconstpool/floatconstpool)
- `expandTOCexpression` 56.0: 1.x body (4.7 KB); rewrite from Pro7 + 2.6 soft-float/isel/fsel/64-bit additions
- `countindirects` 72.7: 1.x/shape (a copy also lives in InstrSelection's call sites)
- `RewriteVectorConst / createfloatconstant` 86.1: 1.x data model (member_pointer_constants, float_object_list)
- `Optimize64bitMath / is_pooled_object / canoptimizevectorconst / TOC_use_isel / COND_to_COMPARE` 89.5: shape

Names: Placeholders: fn_004df140 (OPEN in names-2.6.md; called from createIndirect for SDA anchors), data_005e7102 and data_00588500. Six non-static row-less 1.x leftovers: is_small_splat_or_table_vector, create_diadic_node_with_constant, rewrite_indirect_toc_references, TOC_HasObjectReferenceWithoutExpression (still called by ObjGen), make_objectref_offset and add_toc_reference. The 1.x types (TOCReferenceEntry/toc_references, MemberPointerConstant, TOCEntry) should become Pro7's PoolEntry/ObjectList globals. resetTOCvarinfo (0x4e0cc0): Wii names it reset_anchor_varinfo, and its position does not fit Pro7's resetTOCvarinfo; check it.

Data: Its globals are defined in other units: CodeGen.c (toclist, codelabellist, exceptionlist, blank_name) and Operands.c (float_object_list, data_00587660, data_00588200, member_pointer_constants, data_005883f0, data_00588508). These map onto Pro7's TOC statics: floatconstlist, descriptorlist, vectorconstlist/pools, no_descriptors, and `Object toc0` (data_005883f0, cleared with memclrw(..., 0x44)). blank_name, bound at 0x5ea526, is toc0.name, a fragment inside toc0. Pro7's lvslBytes/lvsrBytes tables should be checked against TOC's .data.

Structure: Should be -inline auto,deferred in Pro7's order (the binary is Pro7's reversed). setupaddressing (0x4e0d70) belongs at the top of TOC.c, so TOC's .text should end at 0x4e0df0. 2.6's head of file is setupaddressing, resetTOCvarinfo/reset_anchor_varinfo, fn_004df140, used_anchor, is_pooled_object, createIndirect.

Hacks:
- src/backend/TOC.c:1640 `#define data_005e7102 (*(UInt8 *)((char *)&copts + 0x20))`, a fake offset macro into copts
- include/compiler/TOC.h:57 `#define fn_0049f230(...) ((Object *)createIndirect(...))` alias macro; createIndirect returns int 0 where Pro7 returns Object *
- src/backend/TOC.c:1647 CError_Internal("TOC.c", 0x2c4) literal file name
- 12 gotos (1.x style)
- include/compiler/TOC.h: 1.x types TOCReferenceEntry/TOCEntry/MemberPointerConstant; extern CInt64 data_0055e598, Boolean data_00588500

Next:
1. Rewrite from Pro7's TOC.c in Pro7's order, -inline auto,deferred, moving setupaddressing (and the TOC statics: toc0, floatconstlist, ...) here from Operands.c/CodeGen.c
2. Keep 2.6's exact additions: anchors (anchorlist, used_anchor, is_pooled_object, fn_004df140), resetTOCvarinfo's anchor/reg_info_list loops, TOC_use_isel/fsel, Optimize64bitMath, the EABI createIndirect
3. Replace blank_name/data_005883f0 with Pro7's toc0 and delete the fragment bindings
4. Retire the 1.x leftovers; port ObjGen's TOC_HasObjectReferenceWithoutExpression caller

### src/frontend/CPrec.c

D, effort L, confidence high; 50/84 exact, not linked, deferred. gc1's CPrec.c (about 37% gc1-only lines, 9% Pro7): Ghidra-style text with gc1 addresses in its names.

This is the worst unit in the batch: 50 of 84 exact, and 34 inexact functions totalling 22.7 KB, six of them under 52%. The source is gc1's precompiled-header writer with about 100 invented inline helpers. Each is a private copy of CPrec_AppendAlign, CPrec_AppendData, CPrec_NewAddrPatch or the pointer hash, named fn_004da6c0_inline1, CPrec_AppendData_004dd4e0, append_zero_bytes_to_alignment and so on. The types are invented (CPrecWrittenEntry, ObjectOffsetEntry, CIStatement26) rather than Pro7's AddrPatch, Patch, PointerHash, BuiltIn and CI_Statement. The low functions fail because 2.6 inlines Pro7's static Get*Patch functions into their callers (CPrec_GetObjBasePatch inlines GetObjTypePatch, GetObjTypeTagPatch and others), which our non-static, gc1-shaped functions cannot reproduce.

Functions:
- `CPrec_GetObjBasePatch` 33.9: shape: 2.6 inlines the static Get*Patch callees; ours calls them, and two cases 'return;' in an SInt32 function
- `CPrec_GetTemplFuncInstancePatch` 41.8: gc1 body/helpers; rewrite from Pro7
- `CPrec_GetTypePatch` 45.0: gc1 body plus a private CPrec_GetTypePatchInline copy; rewrite from Pro7 as one static
- `CPrec_GetObjectPatch` 45.8: gc1 body/inlining; rewrite from Pro7
- `CPrec_GetStatementPatch` 49.3: gc1 body with CIStatement26; Pro7's CI_Statement plus 2.6's sourcefilepath field
- `CPrec_GetExpressionPatch` 51.5: gc1 body; rewrite from Pro7
- `CPrec_GetTemplateFunctionPatch` 66.2: gc1 body
- `CPrec_GetExceptionPatch` 67.7: gc1 body over the 1.x CException operands layout
- `CPrec_GetLocObjectPatch` 75.8: gc1 body
- `CPrec_RelocateRawBuffer` 75.8: gc1 body
- `CPrec_GetNameSpaceObjectListPatch` 77.0: gc1 body / inlining
- `CPrec_GetNameSpacePatch` 80.3: gc1 body / inlining
- `CPrec_GetObjCProtocolListPatch` 80.8: gc1 body
- `CPrec_GetTemplateArgPatch` 82.2: gc1 body
- `CPrec_GetClassAccessPatch` 82.4: gc1 body
- `CPrec_GetObjCMethodListPatch` 82.5: gc1 body
- `CPrec_GetObjTypePatch` 83.2: gc1 body
- `CPrec_GetObjTypeTagPatch` 83.2: gc1 body
- `CPrec_GetTypeStructPatch` 83.5: gc1 body
- `CPrec_GetTemplateParamPatch` 84.1: gc1 body
- `(14 more at 86-97%: GetTStreamPatch, GetObjMemberVarPatch, GetStaticDataPatch, GetObjCCategoryPatch, GetObjCProtocolPatch, GetClassListPatch, GetObjCSelectorPatch, GetObjEnumConstPatch, DumpRootNameSpace, GetNameSpaceNamePatch, DumpColorSymbolTable, GetObjCMethodPatch, GetArgListPatch, GetSOMInfoPatch)` 86.6: same gc1 shape; expect most to follow once the helpers are Pro7's statics

Names: Function rows carry Pro7's names. The helpers are invented, about 100 of them, and many embed gc1 addresses (fn_004da6c0_part1/_inline1/_inline2/_bucket, CPrec_004daf90_Align, CPrec_AppendAlign_004de900, CPrec_AppendData_004dd4e0 ...). Data placeholders: data_0055d5c0, data_00563e40 (a status-string literal), data_00587f6c (the ObjC state), data_00588064 (defined in CDecl.c). Other units' globals defined here under invented names: templateFunctions, class_template_list, somReferences, flushmax.

Data: The .bss is partly defined. Pro7's cprec_buffer (0x5e3034) is not defined by the source and stays raw, and Pro7 has cprec_exportargnames as well. The statics are non-static globals with invented types. 19 raw named and 14 anonymous raw items are referenced.

Structure: Deferred, and the rows' order follows Pro7's, but the source interleaves the private helper copies between the rows. 2 clang errors (for example a bare 'return;' at CPrec.c:4478 and :4483 in a function returning SInt32).

Hacks:
- CPrec.c:94-218 about 100 forward-declared private inline copies of the append/align/hash helpers
- CPrec.c:3874-3889 empty #ifndef DEF_fn_004dbf80 ... blocks
- CPrec.c:3891, :4700, :4715, :4729 TODO'd copies of CPrec_GetTypePatch, CPrec_NewAddrPatch, CPrec_AppendAlign and CPrec_NamePatch for inlining callers
- CPrec.c:79-95 CIStatement26: a 2-byte-packed stand-in for CI_Statement
- CPrec.c:733-760, :1066, :1342-1353, :3122, :4449-4464 the same pointer hash written about 9 times
- CPrec.c:4478, :4483 'return;' in an SInt32 function
- CPrec.c:5322 galloc(0x50e8) header size literal

Next:
1. Rewrite the whole unit from Pro7's CPrec.c, deferred: Pro7's types and statics (all Get*Patch static, CPrec_AppendAlign/AppendData/RawMemPatch/NewPointerPatch as single statics), with 2.6's additions read from the disassembly (the statement record's sourcefilepath, the Gekko/EABI builtins in CPrec_SetupBuiltInArray, the ObjC state)
2. Delete every private helper copy and the DEF_ blocks
3. Define the missing statics (cprec_buffer, cprec_exportargnames) so make_splits can place the .bss
4. Move the other units' globals (templateFunctions, class_template_list, somReferences) back to their owners under their real names

### src/optimizer/ConstantPropagation.c

D, effort M, confidence high; 1/4 exact, not linked. 1.x constant propagation plus 1.x load deletion.

2/5 exact, and the two exact ones are fn_00574990 (Pro7's isunsignedloadoperand, verbatim) and deletedeadloads (LoadDeletion's). propagateconstantstoblock (4474 bytes, 34%) is 1.x code built on GetConst/GetSync/GetKind/SetType helpers; 2.6 is Pro7's (31 change_opcode and 22 change_num_operands calls). isstackoperand (44.7%) is a 1.x stand-in for Pro7's body, which 2.6 has exactly (_FP_/_CALLER_SP_ and can_add_displ_to_local). The file also holds 1.x COpt_LoadDeletion.

Functions:
- `propagateconstantstoblock` 34.0: 1.x body; 2.6 = Pro7 (change_opcode/change_num_operands, is*operand helpers, make_alias)
- `isstackoperand` 44.7: 1.x stand-in; 2.6 = Pro7 (can_add_displ_to_local)
- `computedefininginstructions` 67.3: 1.x use-def accessors (per-class scalars); Pro7 uses reg_Defs[]

Names: fn_00574990 is Pro7's isunsignedloadoperand (body identical; names-2.6.md row still OPEN). unique_definitions/virtual_register_definitions/fpr_definitions are Pro7's defininginstruction/vrdefininginstruction statics. constantPropagationChanged = Pro7 static `changed`; gConstantPropagationChanged = propagatedconstants. 1.x: gLoadDeletionChanged, data_00587c98, immediateLoadLiveness, data_0058820c, CBlockData, struct E.

Data: Pro7's statics (changed, defininginstruction, vrdefininginstruction) are globals here, plus 4 1.x load-deletion globals. .data split marked align:4: its range 0x5d19ac-0x5d1aec runs past its jump table into LoadDeletion's strings (LoadDeletion's .data starts at 0x5d1ac0).

Structure: Should be deferred: the binary has Pro7's order reversed. Its first function propagateconstants (0x575c20) is filed in LoopOptimization.c and its last, deletedeadloads (0x5770e0), is LoadDeletion's.

Hacks:
- ConstantPropagation.c:49 `#define NULL 0`
- ConstantPropagation.c:56-121 SetType/GetConst/GetSync/GetKind/GetKind2/GetMask 1.x helpers with raw opcode ranges (0x15f, 29..32, 0x19..0x1c)
- ConstantPropagation.c:564-600 COpt_LoadDeletion (1.x, row-less)
- ConstantPropagation.c:593 `(void)((int)entry * k);` codegen steering
- ConstantPropagation.c:606 `#define data_005e71c3 (copts.optimizationlevel)` alias; the binding _data_005e71c3 is a copts fragment
- ConstantPropagation.c:605-608 externs typed void*/char[] for propagateinstructions and the record data_005ce810

Next:
1. Rewrite from Pro7's ConstantPropagation.c (with propagateconstants moved in), deferred
2. Move deletedeadloads to LoadDeletion.c; delete COpt_LoadDeletion
3. Rename fn_00574990 -> isunsignedloadoperand (static)

### src/optimizer/FunctionCalls.c

D, effort M, confidence high; 1/5 exact, not linked. 1.x/gc1 argument passing with EABI patches.

1/5 exact. call_function, pass_in_register, pass_in_memory and analyze_arguments are 1.x code: ArgumentContext records, stack offsets as `stack_offset + 8`, and the 1.x outgoing_argument_size. 2.6's code is Pro7's ArgInfo scheme: the disassembly calls set_out_param_displ, update_out_param_size and out_param_displ_to_offset (10 times in pass_in_register), as StackFrame.c (already Pro7's) expects.

Functions:
- `analyze_arguments` 37.7: 1.x body; 2.6 = Pro7's with set_out_param_displ/update_out_param_size and soft-float/Gekko branches
- `call_function` 65.8: 1.x body (emit12 helpers, fn_004a1cb0); 2.6 = Pro7 + EABI (Type_RequiresMemoryReturn, out_param_displ_to_offset)
- `pass_in_register` 74.6: 1.x body; 2.6 uses out_param_displ_to_offset
- `pass_in_memory` 81.6: 1.x body; 2.6 uses out_param_displ_to_offset, CMach_ArgumentAlignment, gecko_load_store_register

Names: 1.x/invented: ArgumentContext (Pro7 ArgInfo), NewParm (Pro7 make_arginfo), emit12/emitobj12, fn_004a1cb0, PCodeUtilities_EmitObjectInstructionWithRegisterMasks, outgoing_argument_size, data_005884ff (AGENTS.md: a Pro7 local).

Data: Defines the 1.x COMMON outgoing_argument_size (with a TODO). .bss 0x5e30b0-0x5e3100 is placed; raw_at 276 bytes.

Structure: Should be deferred: the binary has Pro7's order reversed (call_function lowest, analyze_arguments highest; make_arginfo, needs_TOC_reload, branch_subroutine_indirect* inlined).

Hacks:
- FunctionCalls.c:44 TODO 1.x outgoing_argument_size COMMON
- FunctionCalls.c:46-57 emit12/emitobj12 1.x helpers calling fn_004a1cb0
- FunctionCalls.c:530 `(void)(argType->type == TYPEFLOAT);` dead expression to steer codegen
- FunctionCalls.c:698 CError_FATAL(398) followed by unreachable code

Next:
1. Rewrite from Pro7's FunctionCalls.c (ArgInfo, make_arginfo, static helpers), deferred, against StackFrame's out_param API
2. Add 2.6's EABI branches from the disassembly: soft float in GPR pairs, Gekko paired singles, small-struct returns, Type_RequiresMemoryReturn
3. Remove outgoing_argument_size and data_005884ff from StackFrame.h/CodeGen.c once nothing uses them

### src/optimizer/GlobalOptimizer.c

D, effort S, confidence high; 3/3 exact, linked, deferred. 1.x pipeline plus row-less 1.x AddPropagation/CopyPropagation code.

All 3 rows are 1.x-shaped stand-ins (63-82%). 2.6's are Pro7's file verbatim: the disassembly calls peepholemergeblocks/peepholeoptimizeforward, removecommonsubexpressions, propagatecopyinstructions, propagateaddinstructions and deletedeadloads, with optimizelevel2 inlined. Ours calls 1.x drivers instead (ValueNumbering_PerformValueNumbering, COpt_CopyPropagation, COpt_AddPropagation, COpt_LoadDeletion) through invented wrapper statics, and skips the peephole passes. Lines 234-1056 are row-less 1.x AddPropagation/CopyPropagation code (20 functions). This file keeps the 1.x code in other units alive, and the fix is short.

Functions:
- `optimizelevel4` 63.3: 1.x body: no peephole passes, wrong callees (ValueNumbering_PerformValueNumbering, COpt_*), invented dump wrappers; 2.6 = Pro7 verbatim
- `optimizelevel3` 67.0: same as optimizelevel4
- `globallyoptimizepcode` 81.8: 1.x shape (`char level` local, inlined level-2 path through wrappers); 2.6 = Pro7 with optimizelevel2 a static that gets inlined

Names: 1.x flag names for Pro7's: gCodeMotionChanged=movedloopinvariantcode, gStrengthReductionChanged=strengthreducedloops, gArrayToRegisterEnabled=optimizedloop_full_unroll, gArrayToRegisterChanged=optimizedloop_trans_regs (both Pro7 LoopOptimization.c globals, defined here), gConstantPropagationChanged=propagatedconstants, gCopyPropagationChanged/data_005e662c=propagatedcopies, gLoadDeletionChanged (1.x only). gVectorArrayConversion is Pro7's has_altivec_arrays. Invented: COptimizer_DumpStage/DumpIfChanged/RunCopyPropagation/RunAddPropagation, recovery_inline_5167_0/6220_0, fn_00521480, fn_00521950.

Data: Defines gArrayToRegisterEnabled/gStrengthReductionChanged/gArrayToRegisterChanged, which Pro7 defines in LoopOptimization.c and StrengthReduction.c (COMMON definition site matters). Also defines 12 1.x COMMONs (add_propagation_entries, code_motion_entries, cm_entries, data_00587e38, codeMotionEntryCount, ...) that are really UseDefChains' Defs/Uses/number_of_* and CopyPropagation's candidates. raw_at 1096 bytes in its .data range.

Structure: Should be built -inline auto,deferred (the binary has Pro7's order reversed: globallyoptimizepcode, optimizelevel4, optimizelevel3). The 1.x tail (lines 234-1056) belongs to no unit.

Hacks:
- GlobalOptimizer.c:55-78 invented static wrappers COptimizer_DumpStage/DumpIfChanged/Run*Propagation
- GlobalOptimizer.c:109 `Object *recovery_inline_5167_0 = (function);` decompiler artefact
- GlobalOptimizer.c:211 dumps after 1.x COpt_LoadDeletion ('LOAD DELETATION'); 2.6 calls deletedeadloads with no dump
- GlobalOptimizer.c:234-1056 row-less 1.x AddPropagation/CopyPropagation (COpt_AddPropagation, can_propagate_add, CopyPropagation_* ...), with a TODO
- binding _data_005e71c3 (0x5eb17a) is a fragment inside copts (copts.optimizationlevel); lifted code reads it under a second name

Next:
1. Replace the file with Pro7's GlobalOptimizer.c (optimizelevel2/3/4 static, globallyoptimizepcode), deferred, Pro7 order
2. Delete the 1.x AddPropagation/CopyPropagation tail; then delete ValueNumbering's COpt_CopyPropagation/ValueNumbering_PerformValueNumbering and ConstantPropagation's COpt_LoadDeletion and LoadDeletion's 1.x helpers, which only this file calls
3. Move the optimizedloop_* and strengthreducedloops definitions to LoopOptimization.c and StrengthReduction.c under Pro7 names (check the COMMON hash order and .bss)
4. Drop the _data_005e71c3/_data_005e71b4 fragment bindings inside copts

### src/optimizer/ValueNumbering.c

D, effort M, confidence high; 1/14 exact, not linked. 1.x/gc1 value numbering under 2.6 row names.

1/14 exact; 11 rows are 'unbuilt' because their bodies are missing: the file holds 1.x functions with other names (ValueNumbering_PerformValueNumbering, value_number_instruction, create_register_value_record, fn_0051e720, fn_0051ee40, fn_0051fd70, fn_0051ffc0 ...). removecsesfrombasicblock is 16.9%. The original is Pro7's ValueNumbering.c: removecommonsubexpressions disassembles to Pro7's body line for line (gather_alias_info, allocatecsedatastructures inlined, initializecsedatastructures).

Functions:
- `removecommonsubexpressions` 0.0: unbuilt: the 1.x ValueNumbering_PerformValueNumbering stands in (no gather_alias_info call)
- `removecsesfrombasicblock` 16.9: 1.x body
- `regressvaluestate` 70.3: 1.x data structures (register_values_by_opcode)
- `functioncall` 0.0: unbuilt
- `arithmeticop` 0.0: unbuilt
- `simpleload` 0.0: unbuilt
- `isCSEop` 0.0: unbuilt
- `addavailablevalue` 0.0: unbuilt
- `findavailablevalue` 0.0: unbuilt
- `matchvalues` 0.0: unbuilt
- `copyregister` 0.0: unbuilt
- `killmemory` 0.0: unbuilt (Alias.c calls it)
- `initializecsedatastructures` 0.0: unbuilt

Names: Mostly invented 1.x names: register_values_by_opcode[466] (Pro7 opvalue[]), register_value_state_array/fpr_value_states/gRegisterValueStates/register_value_states (Pro7 regvalue[RegClassMax]), data_00582c38 (stackedvalues), value_index_threshold (valueceiling), data_00582c48 (moreaggressiveoptimization), objectIndex/object_indices, data_00582c44, plus ~10 fn_XXXXXXXX functions and inline helpers named after 1.x addresses (fn_0051efd0_inline1, fn_0051f320_inline1).

Data: Pro7's are 5 statics (opvalue, regvalue[5], stackedvalues, valueceiling, moreaggressiveoptimization) plus the COMMONs removedcommonsubexpressions/nextvaluenumber. Ours defines 13 non-static 1.x globals. .bss 0x5e18a8-0x5e2020 is placed; raw_at 1944 bytes.

Structure: Should be deferred: the binary has Pro7's order reversed (removecommonsubexpressions lowest, initializecsedatastructures highest). The tail COpt_CopyPropagation (line 1581) is 1.x copy propagation that only GlobalOptimizer calls.

Hacks:
- ValueNumbering.c:68 ValueNumbering_PerformValueNumbering duplicates the row removecommonsubexpressions under a 1.x name
- ValueNumbering.c:1581 COpt_CopyPropagation: 1.x copy propagation in the wrong unit
- ValueNumbering.c:161-197 #pragma pack(push,2) around a 1.x record
- ValueNumbering.c:1471 ZeroArray, :1248 VN_FindNode: private 1.x helpers

Next:
1. Rewrite from Pro7's ValueNumbering.c in Pro7 order, deferred, with Pro7's statics (check opvalue's size against 2.6's .bss and PC opcode count)
2. Delete the 1.x functions once GlobalOptimizer calls removecommonsubexpressions
3. Read the disassembly of removecsesfrombasicblock/functioncall/simpleload for 2.6 additions (soft-float, Gekko paired singles)

## C: targeted rewrites

### src/backend/BE_elf.c

C, effort M, confidence high; 15/21 exact, not linked. gc1's 1.x BE_elf.c (renamed, in binary order), most bodies equal to 2.6.

15/21 exact. The inexact rows mostly lack small 2.6 additions: BackPatchRelocationAddend lacks its symbol-patching branch, build_ordered_section_index a '|| sec->flag9' test, InitElfFile a 256-byte name buffer with a length check (error 0x21f). BackEnd_WriteElf is register-level. The main problem is data: an 82-byte packed struct at 0x5eb226 (cleared with memset(&DAT_0058849e, 0, 82)) is split into about 20 fragment globals (data_005884aa ... data_005884ee). The file also tentatively defines other units' globals (copts, cparamblkptr, cscope_root, cscope_current ...).

Functions:
- `BackPatchRelocationAddend` 38.9: missing 2.6 branch: a third parameter (symbol); if set, assert and store it to the relocation (+0x14) and clear +0xc
- `build_ordered_section_index` 56.5: missing a 2.6 condition (byte at section+9) in the '.text' scan
- `BackEnd_WriteElf` 74.8: near-miss: registers and spill slots, few instruction differences
- `InitElfFile` 79.5: missing 2.6's local name buffer with a 255-character limit check

Names: Rows are fine except BE_elf_SetDeclSection (open: Wii's DoParseDeclSpec or elf_define_section). The data is mostly placeholders: data_005884aa/ae/b2/b6/ba/ca/ce/da/de/e2/ee, data_0058847a, DAT_0058849e, data_005884a0, data_00580df6 (the zero-padding buffer), data_0055e528, data_00583ae8 (the output-file section). ELF_SECTION(p) is an identity cast macro.

Data: The .data and .bss are placed. The 0x5eb1c6-0x5eb27e range is one 2-byte-packed struct of section pointers and counters (COMMON or another unit's static), bound as fragments: define it as one struct, bind its start and delete the fragment bindings (as was done for MachineSimulation7400 and the driver's clState). The tentative definitions of copts, cparamblkptr, cscope_* , interrupt_generation_records and previous_ps_ID belong to their units.

Structure: Binary order, not deferred. gc1 builds it deferred with the reverse order, so 2.6 likely was. The 12 Elf* helpers (ElfPad, ElfAppend ...), findInitialSection, MakeRecA, BE_elf_0049c670_inline1 and BE_elf_SetEABISections are non-row helpers inlined in 2.6. Name those with 1.x addresses and check that none emits out-of-line code.

Hacks:
- src/backend/BE_elf.c:71,632 UInt8 DAT_0058849e cleared with memset(&DAT_0058849e, 0, 82): a struct split into fragments
- src/backend/BE_elf.c:58-92 'Globals (generated from the original's .bss)' block that defines other units' globals (copts, cparamblkptr, cscope_root, cscope_currentclass, cscope_currentfunc, cscope_current, cscope_is_member_func)
- src/backend/BE_elf.c:144 #define ELF_SECTION(p) identity cast
- 25 braceless ifs; inline helper named BE_elf_0049c670_inline1

Next:
1. Define the 82-byte section/state struct and replace the data_005884xx fragments (bindings in every version)
2. Add the missing 2.6 branches to BackPatchRelocationAddend (callers pass a symbol), build_ordered_section_index and InitElfFile
3. Move the foreign tentative definitions out; try deferred in gc1's order

### src/backend/BE_symbol.c

C, effort S, confidence high; 13/18 exact, not linked. gc1's 1.x BE_symbol.c (renamed, reordered), mostly matching 2.6.

13/18 exact. BE_symbol_DefineObjectSymbol is a 1.x body: it keeps Q_IMPLICIT_WEAK/Q_WEAK linkage branches and an alignment switch, where 2.6 has neither and calls CMach_AllocationAlignment. The other 4 inexact rows are 80-96%. The file also defines non-row 1.x functions (BE_symbol_004913b0, BE_symbol_GetOffset, BE_symbol_CreateSectionSymbol) that inexact units still call (PCodeAssembly, TOC, ObjGen), and many unnamed rows.

Functions:
- `BE_symbol_DefineObjectSymbol` 44.9: 1.x body: 2.6 has no weak-linkage branches and computes alignment with CMach_AllocationAlignment(type, qual, sect, ...)
- `BE_symbol_004918f0` 80.8: missing a 2.6 test (qual & 0x40 under a local flag) and a store; small
- `BE_symbol_GetFunctionSymbolLinkData` 84.6: near-miss; Wii suggests GetDataPoolRoot

Names: Placeholders: fn_004d1040, fn_004d12c0, fn_004d1400, fn_004d1530, fn_004d1650, BE_symbol_004918f0, BE_symbol_004913b0. BE_symbol_SetupObjectSymbol is unconfirmed (Wii by order: AddFunction), and AGENTS records that its body was once another function's. BE_symbol_CreateSymNode and DefineObjectSymbol are unconfirmed. Data: data_0055da80, data_005e7102, data_005884aa (BE_elf's section struct).

Data: .data and .bss are placed. be_symbol_list and symbol_tail are real. data_005e7102 is a copts byte reached through an offset macro.

Structure: Binary order except InitELFSymbols, which sits after Next. gc1 builds it deferred in the reverse order, so 2.6 was likely deferred. There are 14 non-row helpers: static inline (find_func_sym, get_func_sym, find_used_group, FindBackendSymbol, get_output ...), plain static (FindFuncNode, FindSym, find_output: check that 2.6 has no out-of-line copies), and the three global 1.x exports above.

Hacks:
- src/backend/BE_symbol.c:115 #define data_005e7102 (*(UInt8 *)((char *)&copts + 0x20)): copts offset macro, needs a COpts member
- src/backend/BE_symbol.c:49 BE_symbol_004913b0, a global 1.x function with no row (PCodeAssembly calls it)
- src/backend/BE_symbol.c:118 BE_symbol_GetOffset, 1.x, no row (TOC calls it)
- src/backend/BE_symbol.c:127 BE_symbol_CreateSectionSymbol, 1.x, no row (ObjGen's select_object_section calls it)
- galloc literal sizes, 3 sites; 37 braceless ifs; '/* Declarations gathered from the merged files. */' leftovers

Next:
1. Rewrite BE_symbol_DefineObjectSymbol from the disassembly
2. Name the fn_004d1xxx rows by their callers (Wii's BE_symbol module: GetDataPoolRoot, AddFunction ...)
3. Remove the 1.x exports once PCodeAssembly, TOC and ObjGen call 2.6's functions
4. Replace data_005e7102 with the COpts member; try a deferred build

### src/backend/ELF_Endian.c

C, effort S, confidence medium; 7/8 exact, not linked. gc1's 1.x ELF_Endian.c (decompiler-shaped), equal to 2.6's code.

7/8 exact. swap_tagged_record_data (59%) has the same control flow and constants as the original, but its frame is 0x10 where 2.6's is 8: too many locals (the '(void)(size = ...)' dead stores). Convert_DWARF_Info matches only through a volatile struct, a volatile readhdr parameter and two gotos, none with a TODO. The error handler is still DAT_005805e0.

Functions:
- `swap_tagged_record_data` 59.1: shape: remove the dead '(void)(x = ...)' locals so the frame shrinks to 8 bytes; same flow as the original

Names: Rows have Wii names (Convert_Elf32_Ehdr ...). DAT_005805e0 (the static error-callback pointer at 0x5dfdd8) needs a name; DWARF tag and form numbers are bare.

Data: .data and .bss are placed; one raw named item (DAT_005805e0) and 264 anonymous raw bytes in the unit's .data range (probably the tag tables or literals: check).

Structure: Binary order, not deferred. gc1 builds it deferred in the reverse order, so 2.6 was likely deferred: try that, which may also make the volatile and goto workarounds unnecessary.

Hacks:
- src/backend/ELF_Endian.c:31,41,51 volatile parameter and volatile struct RecordBounds without a TODO
- src/backend/ELF_Endian.c:84,201 goto recordSetup / recordTest jumping into a loop, without a TODO
- src/backend/ELF_Endian.c:29 stray '/* inferred floating type; verify ABI */' comment
- src/backend/ELF_Endian.c:68 (*DAT_005805e0)("ELF_Endian.c", 0x18c): literal file name and line
- 27 includes for a byte-swapping unit

Next:
1. Fix swap_tagged_record_data's locals; name DAT_005805e0
2. Try deferred order and drop the volatile and goto workarounds if it matches; otherwise mark them with TODOs
3. Trim includes

### src/backend/InlineAsm.c

C, effort M, confidence medium; 19/23 exact, not linked. mostly gc1 bodies on gc1 types, close to Pro7 in code.

19/23 exact, but on gc1 types (ParsedAsmInstruction, struct AsmOperand for IALookupResult) and with placeholder globals for Pro7's backtrack machinery. fn_0052ecf0 (0x530810, 426 bytes, a 2.6 addition: a recursive base-class member search used by isclassmember) has no body. The source order is neither the binary's nor Pro7's, and the unit is deferred in the binary.

Functions:
- `fn_0052ecf0` 0.0: unbuilt 2.6 addition (CScope_FindName over ClassList bases, galloc'd 0x18 copy); write it from the disassembly with a TODO name
- `PrimaryExpression` 88.0: near-miss (Pro7 static)
- `InlineAsm_PackAsmStatement` 85.2: gc1 signature (ParsedAsmInstruction **); Pro7's void **output

Names: Placeholders: data_00583a68 (jmp_buf; Pro7's `backtrack`, while InlineAsm_assemblererror is defined too), DAT_00587f18 (Pro7's `backtracking`), data_005e7322, data_005652f8, and fn_0052ecf0 (its name is a 1.x address; it sits at 0x530810). parse_label is Pro7's ScanOptionalLabel. FindNode/AddNode/FindOrAdd are invented __inline helpers.

Data: .bss 0x5e3190-0x5e3598 placed; inlineasm_instruction_buffer, clabels and the jmp_bufs are defined as 'generated from .bss' globals rather than Pro7's (allow_array_expressions = 1, backtracking, backtrack, InlineAsm_assemblererror, static ASMstmtnb).

Structure: Should be -inline auto,deferred in Pro7's order (the binary runs InlineAsm_GetObjectOffset ... AssemblerError, Pro7 reversed). fn_0052ecf0's slot puts it between InlineAsm_LookupSymbol and isclassmember in the source.

Hacks:
- src/backend/InlineAsm.c:57,60 duplicate extern of fn_0052ecf0 under '(from CClass.c's file scope)' comments
- src/backend/InlineAsm.c:51-52 both data_00583a68 and InlineAsm_assemblererror jmp_bufs
- src/backend/InlineAsm.c:254 `volatile SInt16 endToken` (Pro7 has the same volatile, so this is fine)
- src/backend/InlineAsm.c:252 local extern prototype for InlineAsm_gcc_parse

Next:
1. Name the backtrack globals as Pro7 does and check the COMMON hash order
2. Write fn_0052ecf0 from the disassembly (invented name + TODO)
3. Switch to Pro7's IA types with InlineAsmPPC; reorder deferred

### src/backend/InlineAsmMnemonicsPPC.c

C, effort M, confidence high; 2/2 exact, linked, deferred. gc1-shaped code (moved from CTemplateNew.c) for Pro7's two functions; table undefined.

Both functions are exact, but on gc1 types (AsmOperandPattern, NameLookupLink) with the hash table defined in CTemplateNew.c and split into DAT_00584cb4..cc fragments (MWCC's 8x unrolling of Pro7's loop written out by hand). The 992-entry mnemonics table (Pro7's `static IAMnemonic mnemonics[]`) is not defined; it is bound raw at 0x5c40d0. The unit's .data range also holds opcodeinfo (Pro7's OpcodeInfo.c) at its start. The link fails on _opcodeinfo.

Functions:
- `InlineAsm_InitializeMnemonicsPPC` 100.0: exact, but hand-unrolled over 8 DAT_ fragments; Pro7's `for (i = 0; i < 1024; i++) hashedmnemonics[i] = NULL;` should compile the same once the table is one array

Names: Placeholders DAT_00584cb4..DAT_00584ccc; asmOperandPatternLookup is Pro7's hashedmnemonics, asm_operand_patterns is mnemonics, AsmOperandPattern is IAMnemonic, NameLookupLink is HashedMnemonic.

Data: 154 raw anonymous items (3680 bytes) plus asm_operand_patterns: essentially the whole 56 KB .data range is the original's bytes. hashedmnemonics is defined in CTemplateNew.c (src/frontend/CTemplateNew.c:38-39) and bound with fragment bindings (_DAT_00584cb4 at 0x5e6c24).

Structure: The binary has Lookup before Initialize, Pro7's order reversed, so the unit is deferred; the header comment says so but the flags are non-deferred. The .data range 0x5c0fa8-0x5cedd6 spans two objects: OpcodeInfo.c (opcodeinfo, ~0x210c bytes) and this unit's mnemonics table.

Hacks:
- src/backend/InlineAsmMnemonicsPPC.c:84-92 hand-unrolled clear loop over DAT_ fragments
- src/frontend/CTemplateNew.c:38-39 this unit's hash table defined in CTemplateNew
- ~50 unused includes copied from CTemplateNew.c, and empty '(from CTemplateNew.c's file scope)' comments
- include/compiler/InlineAsmMnemonicsPPC.h:10 #pragma pack(push,1) on a struct of 4-byte fields

Next:
1. Generate 2.6's mnemonics table from the original's bytes (Pro7's 992 entries as the template) as a static IAMnemonic array
2. Copy Pro7's file (HashedMnemonic, hashedmnemonics[1024]) in Pro7 order, deferred; delete the fragments and their bindings
3. Split off OpcodeInfo.c (opcodeinfo) as its own data-only unit in front

### src/backend/InlineAsmRegisters.c

C, effort S, confidence high; 3/3 exact, linked. Pro7's InlineAsmRegisters.c logic with a stand-in struct, binary order.

Links, but not in Pro7's shape. It uses a stand-in RegisterBinding with an 'unsigned int key' (cast to char*) in place of Pro7's HashedRegister and IARegister, sits in binary order without deferred, and relies on '#pragma auto_inline off' and '#pragma opt_propagation off' with no TODO. Pro7's 3-function file, deferred, would likely match outright.

Names: Function names are Pro7/Wii. The struct and member names (RegisterBinding.key/attribute1/registerNumber, register_binding_hash) are invented: Pro7 has hashedregisters and IARegister {name, rclass, num, object}.

Data: .bss placed (the 64-entry hash).

Structure: Pro7 order with deferred, which the names table says the original is.

Hacks:
- src/backend/InlineAsmRegisters.c:71,86 #pragma auto_inline off/reset without a TODO
- src/backend/InlineAsmRegisters.c:88,95 #pragma opt_propagation off/reset without a TODO
- src/backend/InlineAsmRegisters.c:~5 InlineAsm_LookupRegister(unsigned int *key) with (const char *) casts: a stand-in for IARegister *(char *name)
- 54 includes

Next:
1. Replace with Pro7's file (HashedRegister/IARegister, Pro7 order) built deferred, and drop the pragmas

### src/backend/InstrSelection.c

C, effort L, confidence high; 41/83 exact, not linked. mix: Pro7 bodies with 2.6 soft-float/paired-single additions plus gc1 (1.3) code and invented helpers.

The binary holds the functions in the exact reverse of Pro7's file order (init_cgdispatch at the top, gen_VECTOR128CONST at the bottom), so 2.6 built this unit -inline auto,deferred. The repo builds it plain -inline auto, with the functions in a scrambled order. About a third of the source lines exist only in gc1's InstrSelection.c. 29 static helpers have no row: genOperand, GenOperand, SelectOperand, AllocGPR_LL, ClearOps, StoreOp, AddOffset/AddOffset2, InstrSelection_GetVR0, fn_004b2500_inline1, InstrSelection_004b5ae0_inline1, and others. These are gc1 inventions where Pro7 uses GEN_NODE/ENSURE_GPR. 41/83 exact; 11 functions are below 60%.

Functions:
- `gen_INTCONST` 27.7: 1.x body: adds a soft_float fatal and writes the pair load itself; 2.6 is Pro7's with I8_gen_INTCONST inlined
- `gen_SHL` 35.9: wrong/garbled: casts binary_operator to call it as (0x6a, right, reg, result), dropping `left`; raw type offsets; Pro7 body + 2.6's soft-float assert
- `COND_has_const` 37.1: 1.x body reading ENode fields by raw offset ((char *)left + 0x12)
- `gen_LOGICAL` 39.1: 1.x shape; rewrite from Pro7
- `gen_COMPARE` 43.3: 1.x body: lacks Pro7's evaluate_and_skip_comma; calls the row-less SFPE_PPC_EABI_GenerateComparison where 2.6 calls SFPE_gen_condition (with a vec2x32 0x13 check)
- `I8_gen_ASS` 43.3: 1.x/stand-in; rewrite from Pro7
- `I8_gen_INDIRECT` 46.2: missing 2.6 branch: 2.6 handles EBITFIELD via fn_004df610 (Wii I8_extract_bitfield) after coerce_to_register_pair; ours fatals
- `gen_DIV` 53.1: gc1 shape with private helpers (signed_div_pow2, signed_div_negpow2, shift_right_imm); Pro7 inlines signed_divide_by_power_of_2
- `gen_SUB` 53.2: 1.x shape
- `gen_condition_gpr` 58.5: Pro7 body with 2.6 rlwinm tracking; mostly inlining/order (non-deferred build), recheck after deferral
- `or_xor_immediate` 59.0: the TODO'd emit_gpr_immediate_operation stand-in sits beside it; rewrite from Pro7
- `logical_expression / gen_condition / gen_AND / gen_COMMA / I8_ShiftRightImmediate / I8_gen_POSTINCDEC / gen_MODULO / gen_COND / gen_FORCELOAD` 64.0: 60-80%: mostly Pro7 bodies built non-deferred; re-measure after the deferred rebuild

Names: Row names are Pro7's. The invented helpers above need to go (Pro7 has macros GEN_NODE/ENSURE_GPR in InstrSelection.h). The header declares an unused fn_004b4aa0 (InstrSelection.h:92), and seven unused function-pointer typedefs (DispatchHandler, OperandGenFunc, SelFn, GenOperandFunc, ENodeGenFn, CodeGenFn, SelectionHandler) sit at the top of the source.

Data: `char instrSelectionFileName[] = "InstrSelection.c"` (line 47) is a CERROR_FILE-style hack used in 7 CError_Internal calls; the literal should come from __FILE__. float_one is raw. 33 raw items (4.3 KB) in the .data range belong to inexact functions.

Structure: Should be -inline auto,deferred, in Pro7's file order: the binary is Pro7's order reversed exactly. The current order is scrambled (I8_* first, gen_SHL near the end). No foreign functions.

Hacks:
- src/backend/InstrSelection.c:47 instrSelectionFileName global used as an assert file name
- src/backend/InstrSelection.c:5578-5596 gen_SHL: binary_operator called through a function-pointer cast with the wrong argument list
- src/backend/InstrSelection.c:5566-5598, 6168-6197 raw struct offsets (*(SInt32 *)((char *)ty + 2), ((char *)ty)[6] != 0x13, (char *)e + 0xe)
- src/backend/InstrSelection.c:2522 emit_gpr_immediate_operation: a TODO-marked function the original lacks
- src/backend/InstrSelection.c:52-60 unused typedefs; CE_ASSERT macro
- InstrSelection.c calls SFPE_PPC_EABI_GenerateComparison, a row-less SFPE helper

Next:
1. Rewrite from Pro7's InstrSelection.c in Pro7's order and build it -inline auto,deferred (add the flag in config/sources.json)
2. Keep 2.6's additions from the current exact bodies: soft-float checks (SFPE_gen_* calls), vec2x32 (integral 0x13) checks, I8 bitfield calls (fn_004df400/fn_004df610), rlwinm tracking in gen_condition_gpr
3. Delete the gc1 helpers and instrSelectionFileName; use Pro7's macros
4. Re-measure: many of the 60-90% rows may be inlining artefacts of the non-deferred build

### src/backend/ObjGen_PPC_EABI.c

C, effort M, confidence high; 36/51 exact, not linked. gc1's 1.x ObjGen_PPC_EABI.c (renamed, reordered to the binary), part updated to 2.6.

36/51 exact (47% of bytes), but the inexact rows are 1.x bodies, not near-misses: update_section, ObjGen_BestSection, ObjGen_GetAddressing, select_object_section and emit_relocation call different functions than 2.6 does (1.x helpers such as BE_symbol_CreateSectionSymbol, ObjGen_PPC_EABI_0048ac10, TOC_HasObjectReferenceWithoutExpression, a DWARF-init branch), and lack 2.6's PPC_EABI_SetSection, CreateDataPoolAnchorSymbol, fn_004d12c0 and ok_to_compress/switchtables checks. The file also exports 1.x functions that 2.6 does not have (ObjGen_PPC_EABI_RestoreFunctionState, _MapObjectSectionCode, _GetSectionAlignmentOrKind, _0048ac10, fn_0048b090). Inexact callers in PCodeUtilities, Operands and FunctionCalls still call them. The row fn_004b4740 is 'unbuilt' only because its body is defined as emit_dwarf_arguments_and_locals.

Functions:
- `fn_004b4740` 0.0: unbuilt: its 2.6 body exists as emit_dwarf_arguments_and_locals (line ~2057), which compiles under the wrong name. Rename one to the other and name the row (it walks arguments, then locals, calling newDWARF_param and newDWARF_local)
- `ObjGen_BestSection` 41.8: 1.x body: has a DWARF-init branch on DAT_0058849e. 2.6 asserts !ok_to_compress, sets it, checks switchtables under a copts flag, and passes that flag to GetSectionByName and CloneSection
- `update_section` 42.2: wrong body: 2.6 is select_object_section plus PPC_EABI_SetSection(obj,0,0,1) plus a 4-case switch on the interrupt record kind. Ours has 1.x DWARF_EstablishContext, the permission check and the alignment code
- `ObjGen_DeclareExceptionTables` 51.8: missing 2.6 additions: the two pooled-anchor fn_004d12c0 calls after ObjGen_GetAddressing, plus asserts
- `select_object_section` 54.4: 1.x branch: calls BE_symbol_CreateSectionSymbol where 2.6 calls CreateDataPoolAnchorSymbol, and uses an inlined initialize_section_table
- `ObjGen_DeclareCode` 59.4: missing 2.6 branch: a CError_Internal check plus PPCError_Error under copts byte 0x5eb0eb
- `ObjGen_GetAddressing` 59.6: wrong body: 2.6 inlines the section lookup (PPC_EABI_SetSection, CodeGen_FindInterruptGenerationRecord, galloc). Ours calls a 1.x SectionKind helper
- `CodeGen_PragmaPop` 64.5: shape or near-miss; check after the data fixes
- `fn_00488750` 71.7: missing 2.6 asserts and branches (uses a private CE_ASSERT macro); unnamed
- `emit_relocation` 72.9: 1.x code: calls TOC_HasObjectReferenceWithoutExpression and ObjGen_PPC_EABI_0048ac10, which 2.6 does not
- `ObjGen_DeclareSwitchTable` 84.6: missing 2.6 checks (22 original-only instructions)

Names: Placeholders: rows fn_00488750, fn_004b1d10, ObjGen_PPC_EABI_00488ee0, fn_004b4740. Non-row helpers keep 1.x address names (ensure_section_table_ac10, fill_section_table_0048ad10, ObjGen_PPC_EABI_0048ac10, fn_0048b090). There are about 20 data_/DAT_ globals. StackFrame's fn_004a9f70 and fn_004a9f90 are Pro7's parametersbase and localsbase. DAT_0055d48c is the binding of the 18-byte "ObjGen_PPC_EABI.c" assert literal at 0x5b0748, which 2.6 passes to CError_Internal. The source defines it as "RX" and passes it as a permission string.

Data: The .data and .bss are placed. Several .bss items are 1.x guesses with placeholder names: data_00580d80, data_00580da4/a8, DAT_00580dac, DAT_00580db0, data_00580db4, data_005870e8, data_00587ff8 (a CNameNode 'current pseudo-section' list). symbols.txt has DAT_0055d48c (0x5b0748, size 0x12) overlapping rw_section_permissions (0x5b074c), while bindings.json puts rw_section_permissions at 0x5b0778. Resolve this before the literals can place. Raw: stchar, data_0055e9e8, data_00583ae8 (BE_elf's ELF file section), ok_to_compress.

Structure: Rows are mostly in binary order with 7 misplacements (MWCATS_AddExit, ObjGen_RelocateObj, ObjGen_DeclareSymInfo, fn_004b1d10, get_current_pseudo_section_info, ObjGen_SetupSym, ObjGen_CodeCleanup). gc1 builds this file -inline auto,deferred, with its source in reverse binary order, so 2.6 was most likely deferred too: switch it, in gc1's order. The unit has no Pro7 counterpart (the names table compares it with ObjGenMachO by analogy only).

Hacks:
- src/backend/ObjGen_PPC_EABI.c:53 char DAT_0055d48c[] = "RX" collides with the assert-literal symbol of the same name
- src/backend/ObjGen_PPC_EABI.c:251 private CE_ASSERT macro (copied in 8 sources)
- src/backend/ObjGen_PPC_EABI.c:471 ObjGen_PPC_EABI_RestoreFunctionState, a 1.x function with no 2.6 row (PCodeUtilities' assemblefunction calls it)
- src/backend/ObjGen_PPC_EABI.c:1083 ObjGen_PPC_EABI_MapObjectSectionCode, 1.x, no row (Operands' coerce_to_addressable calls it)
- src/backend/ObjGen_PPC_EABI.c:1633 ObjGen_PPC_EABI_0048ac10, 1.x, no row
- src/backend/ObjGen_PPC_EABI.c:1755 ObjGen_PPC_EABI_GetSectionAlignmentOrKind, 1.x, no row (FunctionCalls' call_function calls it)
- src/backend/ObjGen_PPC_EABI.c:1859 fn_0048b090, 1.x, no callers at all
- src/backend/ObjGen_PPC_EABI.c:2145 CError_Internal("ObjGen_PPC_EABI.c", 0x7a2) with the literal file name and a hex line (3 sites) instead of CError_ASSERT
- galloc(12)/galloc(24)/galloc(8) literal sizes, 8 sites (MWCATS_WriteFunction ...): use sizeof
- invented MWCATS record types (SerializedWideHeader, SerializedShortHeader, SerializedValueList) without a TODO
- 73 braceless if bodies

Next:
1. Rename emit_dwarf_arguments_and_locals to the fn_004b4740 row (and give it a real name): one function moves from unbuilt to compiled
2. Settle the DAT_0055d48c / rw_section_permissions data symbols (the 2.6 literal is the assert file name)
3. Rewrite update_section, ObjGen_BestSection, ObjGen_GetAddressing, select_object_section and emit_relocation from the disassembly (2.6 callees listed above)
4. Delete the 1.x exports (RestoreFunctionState, MapObjectSectionCode, GetSectionAlignmentOrKind, 0048ac10, fn_0048b090) once PCodeUtilities.assemblefunction, Operands.coerce_to_addressable and FunctionCalls.call_function are rewritten to 2.6's calls
5. Try -inline auto,deferred in gc1's (reversed) order
6. Name the .bss fragments and replace CE_ASSERT and literal-file CError_Internal with CError_ASSERT

### src/backend/PCodeInfo.c

C, effort M, confidence medium; 10/11 exact, not linked. Pro7 bodies for the small helpers; a 1.x vformatpcode.

10/11 exact, but vformatpcode (3.5 KB, 11.7%) is a 1.x body: it uses the 1.x PCOp_FPR/PCOp_VR/PCOp_CRFIELD kinds that AGENTS says 2.6's vformatpcode does not, a private va macro, and even `opnd->kind = TYPEMEMBERPOINTER` for 'p'. Two alias #defines remain (atoi to fn_00421a50, makecopyorconversion to makecopyforload). The binary is Pro7's order reversed (deferred), but the source is in neither order.

Functions:
- `vformatpcode` 11.7: 1.x body; rewrite from Pro7 PCodeInfo.c:114 (PCOp_REGISTER with arg = RegClass) with 2.6's Gekko/paired-single operand letters, using va_list

Names: fn_00421a50 is MSL's atoi (use <stdlib.h> atoi, or the MSL unit's name); makecopyorconversion vs the row name makecopyforload is a #define alias (pick the 2.6 row name and update the header).

Data: opcodeinfo is defined nowhere in source: it is Pro7's OpcodeInfo.c, a data-only object. In 2.6 it starts at 0x5c0fa8, at the head of what splits files as InlineAsmMnemonicsPPC's .data (symbols.txt's size 0x9D1 is stale: ~470 entries x 18 bytes). _FP_ is raw. Check whether PCodeInfo's .data holds the literals of Pro7's dead formatoperands/expectandformatoperand, which the source omits.

Structure: Should be deferred in Pro7's order; the dead formatting functions (expectandformatoperand, formatoperand, formatoperands) probably belong in source for their literals.

Hacks:
- src/backend/PCodeInfo.c:74-75 extern fn_00421a50 + #define atoi fn_00421a50
- src/backend/PCodeInfo.c:403 #define makecopyorconversion makecopyforload
- src/backend/PCodeInfo.c:449 #define PCARG private va_arg macro (AGENTS: use va_start)
- src/backend/PCodeInfo.c:691 manual __ctype_map digit test instead of isdigit
- src/backend/PCodeInfo.c:425 change_num_operands asserts through CError_Internal("PCodeInfo.c", 0x866) by hand

Next:
1. Rewrite vformatpcode from Pro7 + the disassembly
2. Reorder deferred in Pro7 order; add the dead formatters if the .data has their strings
3. Create src/backend/OpcodeInfo.c (data only) for opcodeinfo at 0x5c0fa8 and split it off InlineAsmMnemonicsPPC's range
4. Remove the alias macros

### src/backend/PCodeListing.c

C, effort M, confidence medium; 5/5 exact, linked, deferred. Pro7 stubs (the live functions only).

All 5 live functions are exact (they are empty stubs and one assert), but the unit cannot link: 2.6 built it deferred, so they come out in reverse of Pro7's order, and 2.6 kept Pro7's whole listing file as dead code whose literals fill the unit's .data.

Names: data_005e6588 is Pro7's `pclist_bad_operand`. The assert line 0x5f5 (1525) puts 2.6's file ~200 lines longer than Pro7's (1318).

Data: .data 0x5bda44-0x5bda54 is placed, but per AGENTS the original's .data holds every literal of Pro7's dead listing functions; Pro7's statics (pcfile, ptime, sourcetext, ...) are missing.

Structure: Rewrite in Pro7's order with -inline auto,deferred (the binary runs pclistblocks_end_scheduler, start_scheduler, interferences, blocks, cleanuplisting: Pro7 reversed). Add the dead functions (formatdataflowset, pclistblock, dumpalias, formatalias ...) with their literals in 2.6's order.

Hacks:
- src/backend/PCodeListing.c: `extern SInt32 data_005e6588;` declared mid-file

Next:
1. Copy Pro7's PCodeListing.c whole (dead functions with a comment), deferred
2. Name pclist_bad_operand and define it here
3. Compare the .data literals with 2.6's to fix the dead code's shape

### src/backend/PCodeUtilities.c

C, effort M, confidence medium; 18/18 exact, linked, deferred. mix: Pro7 bodies for the branch helpers, 1.x bodies and unrowed 1.x code elsewhere.

12/18 exact. The branch_*/add_immediate helpers are Pro7's, but pcsetrecordbit (24%) is a 1.x body with the 1.x PCOp_CRFIELD/PCOp_SPR kinds. Two unrowed 1.x functions (fn_004a1cb0, PCodeUtilities_004a2290) remain, called from FunctionCalls.c and InlineAsmPPC.c. The range is wrong at both ends: assemblefunction/insert_align_nops are PCodeAssembly's, insert_load_immediate_after/load_immediate (0x4e3d00) are Operands', and gecko_load_store_register (0x4ea490) sits below the range. The unit is deferred in the binary but built non-deferred.

Functions:
- `pcsetrecordbit` 24.0: 1.x body (no CR-field reuse loop, no change_num_operands, 1.x operand kinds); Pro7's body (PCodeUtilities.c:13) is the shape
- `branch_record_volatiles` 78.4: two definitions/prototypes in the file (lines 295 and 594); check against Pro7's RegClass loop
- `assemblefunction` 75.6: belongs to PCodeAssembly.c (Pro7's last function); rewrite there from Pro7 + 2.6
- `insert_align_nops` 72.8: belongs to PCodeAssembly.c; written with magic numbers, a copts offset macro and a function-pointer cast to call insertlongbranches
- `op_absolute_ha` 84.2: shape; parameter names and types differ from Pro7's (short opcode vs dest_reg)
- `load_store_register` 91.5: near-miss

Names: Placeholders: fn_004a1cb0 and PCodeUtilities_004a2290 (1.x functions with 1.x addresses; still called by FunctionCalls.c:50 and InlineAsmPPC.c:693), fn_00421a50. The row named count_scratch_registers (0x4ead10) is at Pro7's branch_count_volatiles position (between branch_record_volatiles and branch_indirect), and its comment claiming it lies outside the range is wrong: it is probably branch_count_volatiles (same body).

Data: Little own data. data_005e71b4/data_005e713e are fake offset macros into copts (`*(UInt32 *)((char *)&copts + 0x5c)`, PCodeUtilities.c:653-654) and need real COpts members.

Structure: Should be deferred in Pro7's order (the binary is Pro7's file reversed). Move assemblefunction and insert_align_nops (0x4eb120-0x4eb4e0) to PCodeAssembly.c, whose deferred order puts them first. Move insert_load_immediate_after and load_immediate (0x4e3d00/0x4e3d80) to Operands.c (Pro7's first Operands function). gecko_load_store_register (0x4ea490) and Select_Gecko_Load_Store (0x4ea630, filed under Operands) sit between CParser's range and this one; in a deferred PCodeUtilities they would be its tail, after load_store_register.

Hacks:
- src/backend/PCodeUtilities.c:653-654 #define data_005e71b4/data_005e713e as copts+offset (fake offset macros)
- src/backend/PCodeUtilities.c:808 ((void (*)(int, int))insertlongbranches)(half - 1, -half): cast call around a wrong prototype
- src/backend/PCodeUtilities.c:251 private CE_ASSERT macro
- src/backend/PCodeUtilities.c:92 and :290 two private copies of the same copts.exceptions helper
- src/backend/PCodeUtilities.c:414 stale comment on count_scratch_registers
- src/backend/Operands.c:1517 gecko_load_store_register prototype with obj/offset swapped against the definition (PCodeUtilities.c:151)
- magic opcodes and flags (0x8c, 0x2000, 0x6000) in insert_align_nops
- 5 clang errors

Next:
1. Move assemblefunction and insert_align_nops to PCodeAssembly.c, and load_immediate and insert_load_immediate_after to Operands.c
2. Rename count_scratch_registers@0x4ead10 to branch_count_volatiles after checking its callers
3. Rewrite pcsetrecordbit and branch_record_volatiles from Pro7; give fn_004a1cb0/PCodeUtilities_004a2290 their real identity (or delete them once FunctionCalls and InlineAsmPPC are rewritten)
4. Reorder in Pro7 order, deferred, with the gecko functions at the tail; fix the gecko prototype in Operands.c

### src/backend/SFPE_PPC_EABI.c

C, effort M, confidence medium; 1/8 exact, not linked. gc1's SFPE_PPC_EABI.c (1.x soft-float EABI) adapted to 2.6 rows; no Pro7 counterpart.

Eight rows, 1 exact. The bodies are gc1's, reshaped toward 2.6, mostly 77-82% similar. The 2.6 binary holds the functions in the exact reverse of gc1's source order (gc1: OP, FLOATCONST, MONMIN, ASS, POSTINCDEC, INDIRECT, condition, TYPCON), so 2.6 built it deferred too. The repo has them in binary order, built non-deferred, which changes what its static helpers (lift_result, LoadFloatArg ...) inline into.

Functions:
- `SFPE_gen_ASS` 39.9: missing 2.6 branch: 2.6 starts by testing expr->type (0x4c, a conditional-assignment form) with an assert at line 413 and picks left/right differently
- `SFPE_gen_OP` 77.2: shape/inlining (non-deferred build)
- `SFPE_gen_POSTINCDEC` 78.4: shape/inlining
- `SFPE_gen_INDIRECT` 78.9: shape/inlining
- `SFPE_gen_FLOATCONST` 80.9: shape
- `SFPE_gen_TYPCON` 81.8: shape; contains a stray PPCError_Error(0x82, "SFPE_PPC_EABI.c", 0x39f) literal

Names: Rows are Wii/2.6 names (SFPE_gen_*). SFPE_PPC_EABI_GenerateComparison is non-static with no row and is called from InstrSelection's 1.x gen_COMPARE; 2.6 has no such function, since the callers call SFPE_gen_condition. The calls go through the header wrapper PCodeUtilities_EmitObjectInstructionWithRegisterMasks (invented; it builds a local masks[5] for branch_subroutine). Message ids are written as 0x82 rather than PPCError's PPCErrorStr enum.

Data: No .data/.bss placed. It uses CodeGen's rt_sfpe_* COMMONs (real Wii names).

Structure: Should be -inline auto,deferred in gc1's (= the original's) source order, which is the binary reversed. Unit boundaries look right.

Hacks:
- src/backend/SFPE_PPC_EABI.c:88 PPCError_Error(0x82, "SFPE_PPC_EABI.c", 0x39f) literal file name
- src/backend/SFPE_PPC_EABI.c:293-364,807,1015 14x PPCError_Error(0x82, CERROR_FILE, line); __FILE__ is the default, so write it plainly with the PPCErrorStr enum name
- include/compiler/PCodeUtilities.h:17 invented inline wrapper PCodeUtilities_EmitObjectInstructionWithRegisterMasks; 2.6 probably passes static UInt32 used_regs[5] tables like Pro7's I8 code (check the disassembly)
- src/backend/SFPE_PPC_EABI.c:1-15 enums/typedefs invented for literals (lift_regnum_3, ConversionTypeKind, CompareNodeType)

Next:
1. Reorder to gc1's order (the binary reversed) and build -inline auto,deferred; re-measure
2. Add SFPE_gen_ASS's 2.6 entry branch from the disassembly
3. Replace the mask wrapper with branch_subroutine and static register tables if the disassembly shows statics; remove SFPE_PPC_EABI_GenerateComparison once InstrSelection calls SFPE_gen_condition

### src/driver/CLBrowser.c

C, effort S, confidence high; 9/9 exact, linked. gc1-style rewrite of Pro7's CLBrowser.c.

8/9 exact. Browser_PackBrowseFile (66.7%) has a different shape: invented struct names (BrowserFileHeader, DstRec, CLBrowserLookupEntry for Pro7's T53/S49/S43), and an out-of-memory path calling CLIO_OutOfMemory where Pro7 inlines fprintf/exit (check 2.6). The unit cannot link because Browser_PackBrowseFile is defined before CalcDiskSpaceRequirements/ConvertMemToDisk while Pro7 and the binary have them after. It also defines `jmp_buf exit_program` (CLMain's global) and a generated "allocate" string global.

Functions:
- `Browser_PackBrowseFile` 66.7: shape/1.x: rewrite from Pro7 with OS_ASSERT and its header struct; check the OOM path in the disassembly

Names: CLBrowserLookupEntry/DstRec/BrowserFileHeader -> Pro7's S43/S49/T53 (or better names); data_0054d9c4

Data: .data placed (0x88)

Structure: order: CalcDiskSpaceRequirements, ConvertMemToDisk, Browser_PackBrowseFile (Pro7's)

Hacks:
- src/driver/CLBrowser.c:27: #define free xfree
- src/driver/CLBrowser.c:31: data_0054d9c4 string global
- src/driver/CLBrowser.c:35: jmp_buf exit_program defined here (it is CLMain's)

Next:
1. Rewrite in Pro7's order with Pro7's types; literals at their uses
2. Move the exit_program definition where it belongs

### src/driver/CLCompilerLinkerDropin_V10.cpp

C, effort M, confidence high; 22/22 exact, linked. gc1-style decompilation of Pro7's CLCompilerLinkerDropin_V10.cpp.

20/21 exact, but the code is decompiler-shaped. Parameters are `unsigned int`/NameSpaceName*/DropinConfiguration stand-ins, callback names are generated string globals, `#define free xfree` stands in, and PluginC's constructor fills a fake offset class (value1..value7, byte1..byte5, memset of opaque arrays) that is Pro7's CWCompilerLinkerContext ctor. The link fails: `data_0054d774` is only declared extern. The ??0PluginC row is not emitted under its name. The order differs from the binary (Pro7's file order), and the .data base is 0x5a4e54 vs the range start 0x5a4f60.

Functions:
- `??0PluginC@@QAE@XZ (CWCompilerLinkerContext ctor)` 0.0: emitted under the fake class's name; rewrite as Pro7's CWCompilerLinkerContext::CWCompilerLinkerContext

Names: PluginC, InitializationAuxiliaryState (Pro7: CWCompilerLinkerContext and its target-info allocation), DropinRequest/DropinContext/DropinConfiguration, NameSpaceName (a compiler type misused for a driver parameter); 13 data_0054d* string globals; data_0054d774 undefined

Data: 14 anonymous raw ranges (744 bytes); .data object 0x3e0 vs the unit's 0x678; base before the range start

Structure: NOT in binary order (Pro7 file order expected); .data boundary wrong

Hacks:
- src/driver/CLCompilerLinkerDropin_V10.cpp:1: CERROR_FILE define
- src/driver/CLCompilerLinkerDropin_V10.cpp:31: extern char data_0054d774[] (undefined in the link)
- src/driver/CLCompilerLinkerDropin_V10.cpp:41: #define free xfree
- src/driver/CLCompilerLinkerDropin_V10.cpp:535-566: PluginC ctor with memset(.., 0x136/0x46/0xa2) over opaque arrays

Next:
1. Rewrite from Pro7's file in its order with SDK types and literals
2. Replace PluginC with CWCompilerLinkerContext (plugin_internal.h layout at 2.6 sizes, pack 2)
3. Define or name data_0054d774 (a string literal the ctor stores?)

### src/driver/CLDropinCallbacks_V10.cpp

C, effort L, confidence high; 47/47 exact, linked. gc1-style decompilation of Pro7's CLDropinCallbacks_V10.cpp with stand-in types.

41/47 exact, but the source is 1.x/decompiler style. Callbacks take `int callbackContext`/DropinRequest/FileOpenOptions instead of the SDK's CWPluginContext/CWNewProjectEntryInfo, files are DropinFileRecord, and PluginA's constructor is defined here (Pro7's CWPluginPrivateContext ctor). Every "Callback: %s" name is a generated `data_0054cbxx` global instead of the literal at its use. The functions are in a scrambled order while the binary has Pro7's file order. UCBAddProjectEntry (67.2), UCBCreateNewTextDocument (73.6) and UCBSetModDate (72.7, missing Pro7's `!moddate ||`) are 1.x bodies. The three implicit operator= rows (OSSpec, OSNameSpec, OSTime) are unbuilt: 2.6 emits them out of line, which needs -inline off for this C++ unit (as CLParserCallbacks_v1.cpp).

Functions:
- `UCBAddProjectEntry` 67.2: 1.x body (198 differing lines); rewrite from Pro7
- `UCBCreateNewTextDocument` 73.6: 1.x body; rewrite from Pro7
- `UCBSetModDate` 72.7: 1.x body: missing `!moddate ||`, wrong types
- `??4OSSpec@@QAEAAU0@ABU0@@Z / ??4OSNameSpec / ??4OSTime` 0.0: unbuilt implicit operator=: build the unit -inline off

Names: 30+ generated string globals (data_0054cb54 ...); data_0054cbdc/cbec (message format tables), data_0054d050/d0fc (PluginA callback tables: Pro7's sCWPluginCallbacks etc.); stand-in types DropinRequest, DropinContext, FileOpenOptions, ExportedRecord, ChainRecord, DiagnosticSourcePosition

Data: 30 anonymous raw ranges (564 bytes) + 4 named. The object's .data base is 0x5a4644 but the range starts 0x5a4848: boundary wrong (data in the previous unit's range)

Structure: NOT in binary order: write in Pro7's file order (UCBGetFileInfo ... UCBResolveRelativePath, then the context ctor/dtor); .data range start wrong

Hacks:
- src/driver/CLDropinCallbacks_V10.cpp:1: #define CERROR_FILE "CLDropinCallbacks_V10.c" (the bare name is __FILE__'s default: drop it)
- src/driver/CLDropinCallbacks_V10.cpp:62-87: generated callback-name string globals
- src/driver/CLDropinCallbacks_V10.cpp:1318: PluginA::PluginA fake class ctor
- src/driver/CLDropinCallbacks_V10.cpp:1325-1326: fb0/f104 offset members

Next:
1. Rewrite the unit from Pro7's CLDropinCallbacks_V10.cpp in its order, with literals at their uses, SDK types and Pro7's File/Target
2. Introduce Pro7's CWPluginPrivateContext (replacing PluginA) shared with CLPluginRequests/CLCompilerLinkerDropin
3. Build -inline off for the implicit operator= rows; fix the .data start

### src/driver/CLFileOps.c

C, effort M, confidence high; 16/16 exact, linked. Pro7 CLFileOps.c bodies in a 1.x file layout with stand-in types.

14/16 exact (LinkProject 96.0, CompileFilesInProject 99.2, registers only). It cannot link mainly because the functions are in 1.x's order (SyntaxCheckFile, StoreObjectFile, ...) while the non-deferred binary has Pro7's (OutputTextData, GetOutputFile, SyntaxCheckFile, ...). Its .data base is 0x5a1c7c, before the unit's range start 0x5a1ca8, so the boundary with the previous unit is wrong. Everything takes DropinFileRecord (Pro7's File with invented member names), and the file defines CERROR_FILE twice.

Functions:
- `LinkProject` 96.0: registers; probably Pro7 local types (File/Plugin) and the inlined PostLinkFilesInProject
- `CompileFilesInProject` 99.2: one stack-slot difference

Names: DropinFileRecord members invented (validatedOutputMask, secondaryReferenceHandle, configurationReceivedWithCapability, f69c...); CLTarget for Target; set_bytes is 2.6's

Data: 8 anonymous raw ranges (804 bytes). The .data base is 0x2c bytes before the unit's range: GetOutputFile's literals start at 0x5a1c7c

Structure: NOT in binary order: reorder to Pro7's file order (OutputTextData, GetOutputFile, SyntaxCheckFile, PreprocessFile, DependencyMapFile, RecordBrowseInfo, RecordObjectData, StoreObjectFile, CompileFile, DisassembleWithLinker, DisassembleFile, CompileEntry, set_bytes, DumpFileAndPathInfo, CompileFilesInProject, LinkProject); move the .data start to 0x5a1c7c

Hacks:
- src/driver/CLFileOps.cpp:68: #define CERROR_FILE "unknown.c"
- src/driver/CLFileOps.cpp:531: #define CERROR_FILE "DropinFileRecord.c"
- src/driver/CLFileOps.cpp:115,533,540: invented static inline helpers (applyClassTypes, CLFileOps_Err0/1)

Next:
1. Reorder the functions to the binary's order (object_layout.py lists it)
2. Fix the .data boundary (0x5a1c7c) in make_splits/config
3. Replace DropinFileRecord with Pro7's File; drop the CERROR_FILE defines
4. decl_order on LinkProject/CompileFilesInProject

### src/driver/CLFiles.c

C, effort M, confidence high; 14/14 exact, linked. Pro7 Project/CLFiles.c bodies with 1.x stand-in types.

Links, but every signature uses stand-ins. DropinFileRecord stands for Pro7's File, IndexedListLink for Files, ChainRecord for VFile, StorageHandle for Handle, and parameters are 1.x names/`void *`/`unsigned char` returns. The bodies match; the API does not read like the original.

Names: types: DropinFileRecord=File, IndexedListLink=Files, ChainRecord=VFile; File_Free(void*) vs Pro7's File_Free(File*)

Hacks:
- include/driver/CLFileOps.h:18-62: struct DropinFileRecord with invented field names (validatedOutputMask, secondaryReferenceHandle, configurationReceivedWithCapability, f69c ...) is Pro7's File
- src/driver/CLFiles.c:15-186: stand-in types throughout

Next:
1. Introduce Pro7's File/Files/VFile types (cmdline.h) with Pro7 member names (rename_member.py) and retype CLFiles' API
2. Then let CLFileOps, CLDropinCallbacks_V10, CLCompilerLinkerDropin_V10, CLToolExec, CLWriteObjectFile, CLDependencies follow

### src/driver/CLIO.c

C, effort M, confidence high; 39/39 exact, linked. Pro7 CLIO.c with 1.x/decompiler data, stand-in types and a few 1.x bodies.

36/39 exact, but the data and names are placeholders. Pro7's statics are globals with address names: DAT_0057eb68/data_0057eb69 (ioInHelp/ioPiping), data_0057eb70/71 (validErr/validSrc), cachedRecordSpec/data_0057ecb6 (errSpec/srcSpec), specs_equal (srcIsErr), data_0057edfb/fc (newErr/newSrc), DAT_0057edfd (arrowBuf), data_0054b988 (ioLineNum), data_0054b9dc (printedAnything), data_0054b9c0 (msgnames table). The "parsing"/"compiling"/"linking"/"processing" literals are generated globals. styledMessage_Terse (66.7%) is a 1.x body: it lacks Pro7's #warning/#error source-line branch and its errBuf (frame 0x200 vs 0x308). CLStyledMessageDispatch misses a branch (msgStyle 2 test). SendHandleToFile calls an invented writeBufferLines helper instead of Pro7's inline loop, and lacks the `size &&` test. CLPrint/CLPrintErr use hand-computed va_lists. The object's function order differs from the binary's in two blocks.

Functions:
- `styledMessage_Terse` 66.7: 1.x body: missing #warning/#error errBuf branch
- `SendHandleToFile` 79.0: shape: Pro7's inline loop and `size &&` test instead of a writeBufferLines helper
- `CLStyledMessageDispatch` 88.1: missing a branch (msgStyle/ref handling) vs the original

Names: 11 placeholder statics (see summary); DiagnosticSourcePosition stands for Pro7's MessageRef; CLIO_GetFileInfo not a Pro7/Wii name

Data: 22 anonymous raw ranges (1124 bytes) plus 4 named raw; statics declared as globals

Structure: non-deferred; object order differs: ShowWorking/TermWorking/CheckForUserBreak and StartLine..IO_VFormatText, Arrows/IsLikelyAnImporter/GuessTool/GuessDoing emitted after their users (helpers declared inline or defined in a different order)

Hacks:
- src/driver/CLIO.c:43-60: generated string globals and placeholder .bss globals
- src/driver/CLIO.c:562-563: manual va_list arithmetic in CLPrint (and CLPrintErr etc.)
- src/driver/CLIO.c:221: _GetThreadLocalData()[1] for errno
- 4 clang errors

Next:
1. Rename the statics to Pro7's and make them static in Pro7's order (check .bss reverse allocation)
2. Rewrite styledMessage_Terse, SendHandleToFile and CLStyledMessageDispatch from Pro7 plus the 2.6 disassembly
3. Replace DiagnosticSourcePosition with MessageRef; use va_start
4. Fix the emission order (no `inline` on helpers; Pro7's order)

### src/driver/CLIncludeFileCache.c

C, effort S, confidence high; 8/8 exact, linked. 1.x body under Pro7 names.

7/8 exact. CacheIncludeFile (64.3%) is 1.x's: it lacks Pro7's `precompiled` branch (evict the cached precompiled header) and the LRU walk, and is split into invented `fn_00428e00_inline1/2/3` helpers. The statics are invented globals (browser_cache_entries, browser_cache_free_list, cache_free_size) where Pro7 has cachelist, freelist and availablecache, and BrowserCacheEntry stands for Pro7's CacheEntry. Its .bss (0x10) is unplaced because the statics are not static.

Functions:
- `CacheIncludeFile` 64.3: 1.x body missing the precompiled branch; rewrite from Pro7

Names: fn_00428e00_inline1/2/3/_unlink invented helpers; browser_cache_* -> cachelist/freelist/availablecache; BrowserCacheEntry -> CacheEntry

Data: .bss 0x10 in range but the object has 0: make the statics static (Pro7 order, reverse allocation)

Hacks:
- src/driver/CLIncludeFileCache.c:88-131: fn_00428e00_inline* helper scaffolding

Next:
1. Rewrite the file from Pro7's CLIncludeFileCache.c (small) with static data

### src/driver/CLLicenses.c

C, effort S, confidence medium; 10/10 exact, linked. 2.6 FLEXlm licensing code written from the disassembly (Pro7 has only stubs).

Links. 2.6 really checks licences through lmgr326b and Pro7 has only stubs, so the code was decompiled. Its helpers have invented names with no TODO (allocate_license_slot, find_license, get_license_slot_values, delete_license, get_license_slot_count). The cached license static is `data_0057ef08`, with fields k4/k8/kc...

Names: 5 invented function names without TODO; data_0057ef08 static; struct License fields k4..k18

Data: statics are static and in reverse .bss order (good)

Hacks:
- src/driver/CLLicenses.c:14: data_0057ef08 placeholder static
- struct License fields k4/k8/kc/k10/k14/k18 offset names

Next:
1. Look for these helpers in Wii's CodeView module CLLicenses (statics too) and take its names; otherwise add TODO-invented markers
2. Name data_0057ef08 and the License fields

### src/driver/CLParserCallbacks_v1.cpp

C, effort S, confidence high; 11/11 exact, linked. Pro7 CLParserCallbacks_v1.cpp bodies with stand-in types.

Links (-inline off, for the implicit operator=). It still defines PluginB's constructor (the fake class for Pro7's CWParserContext) and uses CLTarget, DropinFileRecord and IndexedListLink stand-ins.

Names: PluginB::PluginB -> CWParserContext::CWParserContext

Hacks:
- src/driver/CLParserCallbacks_v1.cpp:289: PluginB::PluginB() : PluginA(0x50617273, -1) fake classes

Next:
1. Move to Pro7's context classes together with CLPluginRequests

### src/driver/CLPluginRequests.cpp

C, effort M, confidence high; 11/11 exact, linked. Pro7 CLPluginRequests.cpp bodies with fake context classes.

Links, but it creates the plugin contexts as the repo's PluginA/PluginB/PluginC (include/mwcc/Plugins.h: offset-padded classes with pad0[0x9c], fb0, f104 ...). These are Pro7's CWPluginPrivateContext, CWParserContext and CWCompilerLinkerContext (plugin_internal.h). It also uses DropinFileRecord for File, and has a TODO about shellContext's size.

Names: PluginA/B/C -> CWPluginPrivateContext/CWParserContext/CWCompilerLinkerContext

Hacks:
- include/mwcc/Plugins.h: fake offset classes PluginA/B/C (pad arrays, value1..value7, byte1..byte5)
- src/driver/CLPluginRequests.cpp:374-378: (CWPluginPrivateContext *)new PluginA/B/C casts
- src/driver/CLPluginRequests.cpp:383: TODO shellContext size

Next:
1. Define Pro7's CWPluginPrivateContext hierarchy in CWPluginsPrivate.h (2-byte packed, 2.6 sizes) and delete include/mwcc/Plugins.h

### src/driver/ELF_Lib_Import.c

C, effort M, confidence medium; 21/21 exact, linked. 2.x ELF library importer written from the disassembly (Wii module ELF_Lib_Import).

Links, but its data is mostly placeholders. `char data_0058770c[588]` is cleared with `memset(data_0058770c, 0, 1432)`. In the original, data_0058770c (0x290 bytes) and data_00587958 (0x308) are one contiguous 0x598=1432-byte block, a linker-options struct whose +0x290 byte LinkErrorsPPC_EABI tests. Declared as 588 bytes, the compiled compiler's memset overruns whatever COMMON follows: a likely runtime corruption of the kind AGENTS.md warns about. LibImportCU is a documented naturally aligned copy.

Names: data_0058770c/data_00587958/data_00588228/data_00588517 COMMON placeholders, data_0054c490 (ELF magic), data_0054c4d0/4d4 statics; invented function names Inspect_ELF_Header, Import_Object_File, Import_Archive, Import_File (check Wii's module statics)

Data: COMMON sizes wrong (588 vs 0x290 in symbols.txt) and one struct split into two COMMONs

Hacks:
- src/driver/ELF_Lib_Import.c:28-32: placeholder COMMONs, data_0058770c[588] smaller than both the binding size (0x290) and the 1432-byte memset at :226
- src/driver/ELF_Lib_Import.c:40,45: pad0c/pad20 in LibImportCU (documented)

Next:
1. Make 0x5e5e28..0x5e63c0 one 1432-byte struct (the EABI linker settings; name from Wii data publics), with data_00587958 a field
2. Name data_00588228 (warning counter shared with LinkErrorsPPC_EABI), data_00588517
3. Check Wii module statics for the function names

### src/driver/LicenseImports.c

C, effort S, confidence medium; 3/3 exact, linked. hand-written naked-asm stand-ins for import thunks.

3/3 'exact', but these 6-byte `jmp [__imp__lp_*]` stubs are import-library thunks (lmgr326b.dll), not compiled source. The naked asm is inconsistent: lp_checkin's `jmp _imp__lp_checkin` has no brackets. The relink says it 'links, but not to the original'.

Functions:
- `lp_checkout/lp_checkin/lp_errstring` 100.0: not a source unit: linker-generated import thunks

Names: real (lmgr) names

Structure: should be produced by an import library with thunks, or bound like version.dll's (AGENTS.md: import thunks need `_X@N` bindings)

Hacks:
- src/driver/LicenseImports.c:4-25: __declspec(naked) asm jmp stubs; lp_checkin lacks the memory-indirect brackets

Next:
1. Emit thunks from tools/implib.py for lmgr326b (or let the split keep the original's thunks) and drop this unit from the source list
2. At minimum make the three stubs consistent (`jmp [__imp__lp_checkin]`)

### src/driver/LinkErrorsPPC_EABI.c

C, effort S, confidence high; 5/5 exact, linked. 2.x linker error reporting (Wii module LinkErrorsPPC_EABI), written from the disassembly.

Links. It uses hand-computed varargs (`(&diagnosticCode + 1) - base + 3) / 4 * 4`, four times) where AGENTS.md says to use MSL's va_start. It reads ELF_Lib_Import's placeholders data_00587958/data_00588228. format_string may be invented.

Names: format_string (no Wii/Pro7 evidence); data_00587958, data_00588228

Hacks:
- src/driver/LinkErrorsPPC_EABI.c:95,111,117,129: manual va_list arithmetic

Next:
1. Rewrite the varargs with va_start/va_end and check that the bytes hold
2. Rename the shared data with ELF_Lib_Import

### src/driver/MsDos.c

C, effort M, confidence medium; 58/60 exact, not linked. Pro7-era MsDos (Win32 twin of Posix.c) with some 1.x bodies.

54/60 exact. Per AGENTS.md, OS_ReadDir (42.4%), OS_MakeSpec (70.9%) and OS_IsFullPath (81.4%) are 1.x bodies: 2.6 accepts cygdrive forms and builds the spec itself. OS_Write (83.1), OS_NewHandle (89.8) and redirect_standard_handle_to_file (89.8) differ in registers. Its .data range in splits.txt runs 0x599418-0x5a1550 (32 KB), but the object's data is 0x118 bytes at 0x5a1418, so the range swallows something else (likely MSL wctype tables). Placement fails until the functions are exact.

Functions:
- `OS_ReadDir` 42.4: 1.x body; rewrite from IDA's decompilation
- `OS_MakeSpec` 70.9: 1.x body; 2.6 builds the spec itself and handles cygdrive
- `OS_IsFullPath` 81.4: 1.x/partial; 2.6's cygdrive forms
- `OS_Write` 83.1: shape/registers
- `OS_NewHandle` 89.8: registers
- `redirect_standard_handle_to_file` 89.8: registers

Names: Wii names throughout; ensure_trailing_backslash and redirect_standard_handle_to_file are not in Pro7/Wii publics (statics?), so check Wii's module statics

Data: .data 0x118 bytes at 0x5a1418, the split's range starts at 0x599418: boundary wrong (32 KB of another unit's data)

Structure: binary order ok; data range boundary wrong

Next:
1. Rewrite OS_ReadDir, OS_MakeSpec, OS_IsFullPath from the disassembly
2. decl_order on the three register near-misses
3. Fix the .data range start (find the owner of 0x599418-0x5a1418)

### src/driver/Option.c

C, effort M, confidence high; 38/38 exact, linked. Pro7 Option.c logic in 1.x/decompiler dress (placeholder data, shifted rows, invented names).

33/37 exact, but the source is not Pro7's file in names or data. Pro7's statics internalset/legalset (OptionList at .bss 0x5de848/0x5de854) are split into fragments (optionList, data_0057f43c, data_0057f440, data_005da858, data_005da85c). oStack/oStackPtr/curopt/numoptionlists/numinternalset/numlegalset/maxlegalset are data_00586d20, data_00587594, option_name, option_list_count, option_count, option_pending_count, option_capacity. Pro7's endingStack is `option_kind_triples`. The row 'Args_StackSize' (0x41c96f, 15 bytes) covers two functions: Pro7's Args_InitStack (9 bytes) and Args_StackSize (fn_0041c1eb, 6 bytes, no row). Option_OptionError/Warning are unbuilt because the source defines them as format_and_dispatch_option_message/report_option_message. The source order also differs (AddOption/SortOptions swapped).

Functions:
- `Option_OptionError` 0.0: unbuilt: the body exists as invented format_and_dispatch_option_message; rename
- `Option_OptionWarning` 0.0: unbuilt: the body exists as invented report_option_message; rename
- `Args_StackSize` 75.0: row spans Args_InitStack+Args_StackSize: split the row (0x41c96f Args_InitStack 9 bytes, 0x41c978 Args_StackSize 6)
- `Option_Error / Option_Warning` 100.0: exact but hand-computed va_list: use va_start
- `Options_DoParse` 99.2: near-miss; retry after Pro7 names/types (ATK_ enum, Opt48 stack)

Names: Placeholders: data_0057f43c, data_0057f440, data_00586d20, data_00587594, data_005da858, data_005da85c, fn_0041c1eb; invented: format_and_dispatch_option_message, report_option_message, option_kind_triples (endingStack), option_lists (optionlists), OStack (Opt48)

Data: Pro7's `static OptionList internalset, legalset` appear as 5 global fragments; oStack is declared `OStack data_00586d20[180]` where Pro7 has [8] (MAXSTACK). The size is wrong: symbols.txt gives 0x9 for the fragment

Structure: non-deferred, binary order = Pro7 order except Options_AddOption/Options_SortOptions swapped; row split needed at 0x41c978

Hacks:
- src/driver/Option.c:16-17: #define ClientGlue_DuplicateString xstrdup / ToolHelpers_ResizeBuffer xrealloc aliases
- src/driver/Option.c:36-37: #define oStack data_00586d20 / oStackPtr data_00587594 aliases
- src/driver/Option.c:42: OStack data_00586d20[180] (Pro7 oStack[8])
- src/driver/Option.c:952-978: manual va_list arithmetic
- src/driver/Option.c:100-102: second OPTION_ASSERT form redefined mid-file

Next:
1. Split the Args_StackSize row into Args_InitStack + Args_StackSize (functions.json/symbols)
2. Rename the two invented functions to Option_OptionError/Option_OptionWarning (short id, va_list)
3. Rewrite the file in Pro7's shape: static internalset/legalset, endingStack, ATK_* tokens, Opt48 oStack[8], Pro7 names; bind the struct starts while anything is lifted
4. Order AddOption/SortOptions as the binary has them, then check Options_DoParse

### src/driver/TargetOptimizerHelpers-eabi-ppc.c

C, effort S, confidence high; 3/3 exact, linked. 1.x (gc1) body under 2.6 names.

1/3 exact. TargetDisplayOptimizationOptions (42.9%) is 1.x's: 2.6 first prints "\t- peephole optimizations %s\n" with "on"/"off", and it knows 860, 8240, 7400, GEKKO, e500 and 7450 (cases up to 24, see .data 0x5a2358). The 1.x body maps 19 to "8260", lacks the peephole line, and keeps every CPU name as a generated `data_0054c5xx` global instead of a literal. TargetSetOptFlags (58.5%) has the right three cases ('Pe','Sh','Sn'), but `#pragma optimization_level 2` changes register choice and label alignment.

Functions:
- `TargetDisplayOptimizationOptions` 42.9: 1.x body: missing peephole line and the 2.x CPU list (860/8240/7400/GEKKO/e500/7450)
- `TargetSetOptFlags` 58.5: shape: remove the optimization_level pragma; Pro7's switch with 'Pe'/'Sn'/'Sh' char constants

Names: 18 data_0054c5xx string globals should be literals; data_0054c624 raw

Data: .data object 0x98 vs the unit's 0xf0: the strings are generated globals in the wrong order

Hacks:
- src/driver/TargetOptimizerHelpers-eabi-ppc.c:16-34: generated string globals
- src/driver/TargetOptimizerHelpers-eabi-ppc.c:36,53: #pragma optimization_level 2 / scheduling off
- TargetDisplayOptimizationOptions declared unsigned int with no return

Next:
1. Rewrite from Pro7's TargetOptimizer-ppc-mach.c plus 2.6's disassembly: the peephole line, then an if-chain or ternary on pBackEnd.processor with literals (2.6 orders case 20 first)
2. Drop the pragmas and retry TargetSetOptFlags with Pro7's body

### src/driver/ToolHelpers.c

C, effort S, confidence high; 4/4 exact, linked. Pro7 ToolHelpers.c bodies with 1.x/decompiler names.

Links, but with placeholders and stand-ins. Pro7's globals are renamed: `output_path`=linkerOutputFilename, `output_path_set`=setOutputDirectory, `data_00588530`=outputOrdering, `data_0054a0b8`=lastStage, `data_0058851d`=setLinkerOutputFilename. Unused static inline accessors (driverStatus, pendingCount ...) are 1.x leftovers. Magic numbers (0x436f6d70, 0x54455854) stand for CWDROPIN*TYPE/'TEXT', and ExportedRecord stands for CWProjectFileInfo.

Names: output_path, output_path_set, data_00588530, data_0054a0b8, data_0058851d -> Pro7's linkerOutputFilename, setOutputDirectory, outputOrdering, lastStage, setLinkerOutputFilename

Data: Globals are COMMON/statics at the right addresses; only names are wrong

Hacks:
- src/driver/ToolHelpers.c:30-57: placeholder globals and dead static inline accessor helpers
- src/driver/ToolHelpers.c:60: Opt_HandleOutputName(char*, int, char*) vs Pro7's (const char*, void*, const char*, int)
- ExportedRecord stand-in for CWProjectFileInfo

Next:
1. rename.py --data the five globals to Pro7's names (check COMMON hash order)
2. Delete the unused inline accessors; use CWDROPINCOMPILERTYPE etc.
3. Replace ExportedRecord with the SDK's CWProjectFileInfo

### src/frontend/CDecl.c

C, effort L, confidence medium; 74/78 exact, not linked, deferred. mixed: Pro7's order and names over gc1 text (about 28% gc1-only lines, 23% Pro7).

74 of 78 exact, but the source keeps gc1's helpers and hacks. These include a fake offset struct (ClassTail13, with pad[0x42] and fields f42..f59) used to initialise a template class's tail, a TypeClassExt800 stand-in, MAKE_NODE/SETFUNC/ATTR_FLAGS/CE_ASSERT macros, literal galloc sizes, and about 15 invented helpers (IsClassEnumOrRef, IsSingleArg, IsMethod, IsNonStatic, attach_node, IsZero ...) where Pro7 has its own statics. The inexact functions differ in registers and spill slots, not logic.

Functions:
- `CDecl_AddFunctionMember` 68.0: shape: a value 2.6 keeps in a stack slot ([esp+0xc]) is in a register in ours, and a parameter is loaded in the prologue; same stores and calls. Try Pro7's locals and parameter types
- `CDecl_ParseEnumList` 79.0: shape: a flag kept on the stack in 2.6 vs bl in ours; compare with Pro7's CDecl_ParseEnumList/CDecl_OldParseEnumList (the latter is missing from the source)
- `CDecl_OverloadFunctionObject` 96.9: shape (registers)
- `CDecl_GetFunctionObject` 98.9: shape

Names: Rows carry Pro7's names. Invented helpers: IsClassEnumOrRef (Pro7's CDecl_IsEnumClassTypeOrRef), IsSingleArg, IsMethod, IsNonStatic, parsePointerQualifiers, CDecl_CInt64IsZero, IsZero, attach_node, begin_class_instantiation, class_browse_enabled, scandeclarator_NewArrayType, CDecl_ParseDirectFuncDeclFromParen, prepend_class_pointer_argument, CDecl_NewTypeMemberFunc, plus CDecl_MergeArrayArgFlags (TODO'd). Data placeholder: data_00588064 (the ObjC protocol list used by CPrec). Missing Pro7 functions: CDecl_SetFuncResultReg, CanCreateHandleMemberObject, CDecl_TemplatePTM, scaninlinefunc, CDecl_ScanDeclarator, CDecl_FindSignedType, CDecl_FindUnsignedType, CDecl_OldParseEnumList, CDecl_MaxType, CDecl_DefineStruct, getaccesstype.

Data: Generated .bss globals (oldstyle, tkintconst, selector_hash, the new/delete fobjs (CParser's in Pro7), member_foi, member_access, sel_type) mostly belong to other units in Pro7. .data placed, with no .bss range of its own.

Structure: Deferred, in Pro7's order apart from CDecl_ExtractClassExportFlags, CDecl_MakeFunctionVirtual, CDecl_FuncRedeclCheck, CDecl_ParseOperatorDecl and checkfuncintegr.

Hacks:
- CDecl.c:102-112, :4918-4925 ClassTail13 fake offset struct (pad[0x42], f42..f59)
- CDecl.c:4817 TypeClassExt800 cast for a template-instance flag
- CDecl.c:70-96 CE_ASSERT, ATTR_FLAGS, SETFUNC and MAKE_NODE (galloc(0x12)) macros
- CDecl.c:209, :310, :432, :1196, :2280 literal galloc sizes (18, 26, 14, 0x1a)
- CDecl.c:4366 #pragma dont_inline for an empty function (TODO)
- CDecl.c:1694 TODO on qualifier 0x80000000

Next:
1. Replace ClassTail13/TypeClassExt800 with the real template-class struct (Pro7's TemplClass/TemplClassInst fields) and delete the macros
2. Replace the gc1 helpers with Pro7's statics (CDecl_IsEnumClassTypeOrRef, CDecl_FindSignedType, ...) and let deferred inlining place them
3. Use sizeof for the allocations
4. Run decl_order.py and Pro7's integer types on CDecl_AddFunctionMember and CDecl_ParseEnumList
5. Move the other units' globals (new/delete fobjs, selector_hash, data_00588064) to their owners

### src/frontend/CError.c

C, effort M, confidence high; 40/40 exact, linked, deferred. gc1's CError.c (about 43% of lines are gc1-only, about 10% Pro7) with Pro7's function names, built deferred.

38 of 40 functions are exact, but the text is gc1's. The function order is neither Pro7's nor the binary's (15 functions out of order), and private buffer helpers (CErrBuf_Append, CErrBuf_Putc) duplicate Pro7's CError_BufferAppendString and CError_BufferAppendChar. Every CError_FATAL site is CError_Internal's body written out by hand. The statics are data_XXXXXXXX placeholders, and the qualifier keywords are generated string globals where 2.6 has literals. The two inexact functions call helpers that 2.6 inlines, which is a consequence of the source shape, not of wrong logic.

Functions:
- `CError_BufferAppendType` 71.1: shape: 2.6 inlines the buffer helpers and callees that ours calls (non-static, gc1 helper structure); rewrite in Pro7's order with Pro7's statics
- `CError_BufferAppendTemplArgExpr` 60.1: shape: 2.6 inlines CError_BufferAppendString and CError_AppendObjectName (and through it AppendFunctionName); ours calls them. Pro7's body itself is right

Names: Function names are Pro7's. Placeholders, with their Pro7 names: data_005805ec = cerror_errorcount (short), data_005805ee = cerror_lasterrorline, data_005830c8 = cerror_synchdata[32], data_005883ec = cerror_synchoffset, data_0058715c = the counter incremented after every inlined fatal error (check whether it is fatalerrors or CError_BreakPointcount), and data_00551938/0055196c/00551988 = the "asm"/"mutable"/"out" literals. Helpers that are not Pro7's: CErrBuf_Append, CErrBuf_Putc, CError_GetErrorMessage (Pro7's CError_GetErrorString), CError_GetResourceIndex.

Data: CError.c:54-74 defines 19 char[] globals "generated from the original's .data" (const_string, volatile_string ... cerror_filename). In 2.6 these are literals at their use, cerror_filename is __FILE__, and there is 4.1 KB of anonymous raw data in the unit's range. The .bss globals are placeholders. Pro7 has cerror_token/cerror_errorcount/cerror_lasterrorline as statics.

Structure: Deferred, but the source is in neither Pro7's order (Init, SetErrorToken, ..., BufferInit, BufferGrow, AppendChar, AppendString, ...) nor the binary's reverse. AGENTS notes that binary order dropped it to 14 exact, so the rewrite has to be Pro7's order together with Pro7's statics. Missing Pro7 functions: CError_SetNullErrorToken, CError_GetErrorString, CError_VAErrorMessage, CError_OverloadedFunctionError2, CError_BreakPoint, CError_ExpressionTooComplex (some may be inlined or dead in 2.6).

Hacks:
- CError.c:93-96 private va_list/va_start/va_arg macros (AGENTS: use MSL's stdarg.h)
- CError.c:309-313, :935-939, :962-966 CError_Internal's body written out by hand (CompilerGetCString, sprintf, ErrorMessage, longjmp, counter++) in place of CError_FATAL
- CError.c:54-74 string literals as globals
- CError.c:1194-1212 CErrBuf_Append/CErrBuf_Putc duplicate the buffer helpers
- CError.c:190-231 #pragma dont_inline around CError_BufferAppendNameSpace (TODO)
- CError.c:1250 #pragma inline_depth(4)
- 1 clang error

Next:
1. Rewrite from Pro7's CError.c in Pro7's order, with Pro7's statics and CErrorBuffer helpers, built deferred; use CError_FATAL and stdarg.h
2. Replace the data_ placeholders with Pro7's names, checking COMMON hash order
3. Turn the string globals back into literals, then check the .data layout
4. Re-test the two inlining near-misses after the reorder

### src/frontend/CException.c

C, effort M, confidence high; 41/41 exact, linked, deferred. gc1 (1.x) CException.c bodies, renamed to Pro7's function names and put in Pro7's order.

Links from source (41/41), but the bodies are gc1's: the repo's `CException` struct with numeric `kind` cases (1..17) where Pro7 has `ExceptionAction` and EAT_* enumerators, magic flag numbers (0x400, 0x36/0x37 in CExpr_SearchExprTree), gc1's invented static helpers, and a `#define` alias for a copts field. Pro7's static helpers (CExcept_IsSubList, FindLocalObject, DestroyLocal*, EndCatch, TypeID, MightNeedDtor, CheckTryObjects, InsertPrevStatement, GetETEMPObject) exist under gc1 names or are inlined differently.

Names: Public functions are Pro7's. These statics are gc1/invented: CException_StdType, CException_004e2c40_inline1/2 (address-named), CException_IsClassType, object_statement, finish_label, InsertPrevStatement (Pro7 CExcept_InsertPrevStatement), CException_NewTypeNode/NewStmtNode/CopyStmtNode, CException_CachedObject (likely Pro7's CExcept_GetETEMPObject), reverse_one, NewTemporaryStatement, findTemporaryObject. The type `CException` (include/compiler/CException.h:18) is Pro7's `ExceptionAction`.

Data: Pro7's statics in Pro7's order, with `data_005df12c` for Pro7's cexcept_canthrow (line 67) and mac68k alignment for the 2-byte .bss (TODO).

Structure: ok (deferred, Pro7 order); the trailing comment at line 2186 about CPrec functions is stale (nothing follows it)

Hacks:
- src/frontend/CException.c:82 `#define data_005e71b4 (copts.exceptions)` alias
- src/frontend/CException.c:67 static named data_005df12c (Pro7 cexcept_canthrow)
- src/frontend/CException.c:75-80 private CE_ASSERT macro instead of CError_ASSERT
- src/frontend/CException.c:2137,2173 `#pragma dont_inline on/reset` around CExcept_CanThrowCheck with no TODO
- src/frontend/CException.c:528,539 address-named inline helpers CException_004e2c40_inline1/2
- src/frontend/CException.c:217-262 numeric `case 1..17` where Pro7 has EAT_* enumerators; magic flag 0x400 at line 2168
- 2 clang errors (tu_metrics)
- src/frontend/CException.c:2186 stale comment about CPrec functions

Next:
1. Rename the type CException -> ExceptionAction with Pro7's members and EAT_* enumerators (rename_member.py), keeping the code identical
2. Rename the statics to Pro7's (data_005df12c -> cexcept_canthrow; helpers to Pro7's CExcept_* names) and remove the data_005e71b4 alias
3. Replace CE_ASSERT with CError_ASSERT; put a TODO on dont_inline or replace it with Pro7's shape; fix the clang errors; relink after each step

### src/frontend/CFunc.c

C, effort L, confidence high; 49/51 exact, not linked, deferred. gc1's CFunc.c (about 37% gc1-only lines, 20% Pro7) under Pro7's names, deferred.

48 of 51 exact, but statement() (9168 bytes, 61%) is gc1's body. It is built on gc1 helpers defined after it: NewScope, RestoreBlock, ScopedBody, AppendStmt, NewLabel, FindLabel, IsLabel and CondJump, which duplicate Pro7's CFunc_NewDeclBlock, CFunc_RestoreBlock, CFunc_CompoundStatement, CFunc_AppendStatement, newlabel, findlabel, checklabel and ifstatement. The unit's statics are nearly all placeholders or invented names, and there are many literal allocation sizes. About 20 gc1 inline helpers (GenLoop_Append, CFunc_NewStatement, CFunc_0047b880_inline1, FindStatementReference ...) stand in for Pro7's statics.

Functions:
- `statement` 61.3: gc1 body with gc1 helpers; ours has ~530 extra instructions and two 'call 0' sites (calls to the gc1 statics defined after it). Rewrite from Pro7's statement() (CFunc.c:1576) plus 2.6's additions
- `CFunc_ParseFuncDef` 92.6: shape: a byte local spilled vs kept in bl, plus an extra byte copy; recheck after the statics are named
- `DestructorReturnTransform` 97.3: shape (registers)

Names: Placeholders, with their Pro7 names: data_005e669c = sourcefilepath, data_005e6744 = functionbodypath (TODO at :463), PTR_00580870 = firstblock, saved_global_values_tail = currentblock, data_00580878 = blockcount, data_005dcc80 = cfunc_hasdtortemp, data_0058757c = cinit_tempnodefunc (CInit's), data_0058087e/data_00580882/data_0058088c/data_00580890 = sinit_label, ainit_only_one, cfunc_is_extern_c and similar (map by .bss offsets). Invented: function_token_line, local_name_counter, declaration_token (Pro7 symdecltoken), blank_argument_name, deferred_expression, default_arg, ctor_initializers, StatementContext (Pro7's DeclThing), struct Fn004f2700 (an ENode).

Data: The .bss globals were generated from the original's bytes, with gc1 names. The real statics (firstblock, currentblock, blockcount ...) are kept global for lifted code. The unit also defines other units' variables (spaceskip, CPrep's; curstmt; init_expressions).

Structure: Deferred, in Pro7's order apart from identifier_list, defarg_name_obj_check, CFunc_DefaultArg, CFunc_NameLocalStaticDataObject and CFunc_AdjustOldStyleArgs. The gc1 helpers sit between statement and CFunc_CompoundStatement, where Pro7 has none. Missing Pro7 functions: CFunc_LoopIncrement/Decrement, CFunc_IsInObjList, CFunc_DestroyReverse, CFunc_TempTransDestroy, IsSubStack, CFunc_CheckInitSkip, checklabel, CFunc_Get/SetGlobalCompilerState.

Hacks:
- CFunc.c:80 static const keep_fn = CFunc_AutoResultCheck (keeps a function alive)
- CFunc.c:391-397 struct Fn004f2700 { char pad[0x12]; SInt32 f12; UInt8 f16; }: a fake offset struct for ENode in CFunc_HasDtorTempCallBack (:1694)
- CFunc.c:1702 CExpr_SearchExprTree called through a cast function pointer
- CFunc.c:2489-2620 gc1 duplicates of Pro7 helpers (AppendStmt, NewLabel, FindLabel, IsLabel, NewScope, RestoreBlock, ScopedBody, CondJump)
- CFunc.c:242, :353, :479, :826, :839, :859, :875, :2499, :2555, :2586, :2604, :3547 literal lalloc sizes (0x1e Statement, 0x14 CLabel, 0xe DeclBlock)
- CFunc.c:91-365 ~20 invented static inline helpers standing in for Pro7 statics

Next:
1. Rewrite statement() from Pro7 with Pro7's helpers (CFunc_NewDeclBlock, CFunc_RestoreBlock, CFunc_AppendStatement, newlabel, findlabel, checklabel), keeping 2.6's additions read from the disassembly; delete the gc1 helpers
2. Rename the .bss placeholders to Pro7's names (firstblock, currentblock, blockcount, sourcefilepath, functionbodypath, cfunc_hasdtortemp ...), checking COMMON hash order
3. Replace the invented inline helpers with Pro7's statics and let deferred inlining do the work
4. Use sizeof for every allocation; type CFunc_HasDtorTempCallBack's parameter as ENode*
5. Remove keep_fn once CFunc_AutoResultCheck has a real caller order

### src/frontend/CInit.c

C, effort M, confidence high; 50/50 exact, linked, deferred. gc1's CInit.c (about 47% gc1-only lines, 13% Pro7) under Pro7's names, deferred.

48 of 50 exact, but the text is gc1's. Pro7's small statics (CInit_InitTypeFloat, Enum, MemberPointer and Bitfield, CInit_ExprFloat/Enum/MemberPointer, CInit_IsAllZero, CInit_SetupInitInfo ...) are written out by hand inside their callers or under invented names. The types are gc1's (InitializerData, CInit) rather than Pro7's CInit_Stuff/CInit_Stuff2/InitInfo. There is leftover generated cruft (unused enums, CE_ASSERT), and the statics are placeholder-named.

Functions:
- `CInit_InitReference` 98.5: shape (registers)
- `CInit_GenericData` 98.9: shape: small block-order difference; 'references only inferred' for two literals

Names: Placeholders and invented names, with their Pro7 names: string_cache = cinit_stringlist, pooled_strings = cinit_pooledstringlist, pooled_wstrings = cinit_pooledwstringlist, tentative_init_list = cinit_tentative, data_00581ba0 = cinit_fdtnode, data_00581ba4 = cinit_fdtambig, cinit_state = cinit_initinfo, data_0058757c = cinit_tempnodefunc. Invented functions: initialize_bitfield (CInit_InitTypeBitfield), IsZeroed / CInit_IsZero (CInit_IsAllZero), InitBuffer (CInit_SetupInitInfoBuffer?), CreateObject / CreateTempObject (CInit_CreateStaticData*), needs_init (CInit_ClassNeedsConstruction), IsComplexClass, CInit_DefaultInit, InitExprWrap, classify, CInitPushSave (CInit_SetupInitInfo), CInit_StringEmissionMode.

Data: The .bss globals are generated, named and not static. Xgreg_func (CParser's in Pro7) is defined here. .data/.bss placed.

Structure: Deferred and mostly in Pro7's order (CInit_CloseInitList, CInit_GetRegMemType, CInit_GetInitObject and CInit_AdjustObjectDataSize are out of order). Pro7's statics are missing as functions: 23 Pro7 functions are absent, most of them written out by hand in callers, which deferred inlining would reproduce if they were written as statics.

Hacks:
- CInit.c:53-69 unused generated enums (TYPESTRUCT_004d2700, InitKind, BoolFlag), empty #ifndef TRUE/FALSE blocks, CE_ASSERT macro
- CInit.c:973-1013 Pro7's CInit_InitTypeFloat/Enum/MemberPointer written out by hand inside CInit_InitType
- CInit.c:1037 CInit_InitTypeClass called through a function-pointer cast
- CInit.c:437, :553, :586, :2883, :3058, :3110 literal lalloc/galloc sizes (0x26, 0x16, 16, 8)
- CInit.c:1594 InitExprWrap: a non-static inline helper

Next:
1. Rewrite in Pro7's shape: Pro7's types (CInit_Stuff, CInit_Stuff2, InitInfo, PooledString) and the Pro7 statics as functions, deferred
2. Rename the statics to Pro7's names and make them static where no lifted code uses them
3. Remove the generated cruft and the cast-call; use sizeof

### src/frontend/CInline.c

C, effort L, confidence high; 53/57 exact, not linked, deferred. gc1's CInline.c (about 42% gc1-only lines, 9% Pro7) under Pro7's names, deferred.

53 of 57 exact, but the text is gc1's. There are about 20 invented inline helpers (MapObj, FindIndex, gen_expr_save, CInline_0050f240_inline1/2, CInline_GetStatementNumberInline ...) where Pro7 has CInline_GetLocalID, CInline_TranslateID and similar statics. Three redundant CERROR_FILE defines remain, along with an alias #define and more than 130 placeholder data references. CInline_PackActions (3.7 KB, 55%) copies exception actions through gc1's generic CException operands[] and MapObj, where Pro7 uses ExceptionAction's typed union and CInline_GetLocalID/CInline_GetStatementNumber.

Functions:
- `CInline_PackActions` 55.0: gc1 body over the 1.x CException operands layout; rewrite from Pro7's CInline_PackActions (CInline.c:3036) with ExceptionAction's union
- `CInline_UnpackIFunctionData` 89.8: shape: one block (an allocation of 0x44 and its init) is placed in another order; field accesses through funcdata->f1c/f98 placeholders
- `CInline_InlineFunctionCheck` 93.7: shape (registers)
- `CInline_FoldConst` 99.7: shape (registers)

Names: Placeholders, matched to Pro7's statics by .bss order: data_00582456 = local_dobjects, data_0058245a = local_aobjects, data_0058245e = loc_args, data_00582462 = loc_vars, plus data_00582467/00582468/005824b5/005824c3 and data_005dfb62/66/68/6e/72/76 (cinline_level, inline_expanded, any_inline_expanded, enode_idtrans, cinline_label_trans and others). data_005e6744 = functionbodypath. data_00588454 = cparser_fileoffset.tokenline (a fragment, see CParser). Invented names: evalMode, alloc_state, memo_list, inline_call_seen, fixup_list, inline_statements, inline_call_expressions, deferredInlineNodes, pending_prec_nodes, undefined_function_objects, pendingInlineWork, plus the invented helpers.

Data: The .bss globals are generated and non-static. symdecloffset and data_00588454 are defined here (other units' COMMONs). 4.8 KB of anonymous raw data remains in the unit's range. 2.6's .data has only "__init__" and "CInline.c" as strings.

Structure: Deferred, mostly in Pro7's order (CInline_GetObjectSFlags, CInline_CanExpand, CInline_LoadToTemp and three Serialize* are out of order). 20 Pro7 functions are absent, written out by hand or replaced by invented helpers (CInline_GetLocalID2, CInline_TranslateID, CInline_SetObjectSFlags, CInline_NewLocalObject, CInline_ReturnCheck, CInline_NewStatement, CInline_AddFRefList_Expr/Statement ...).

Hacks:
- CInline.c:1, :92, :301 #define CERROR_FILE "CInline.c" (redundant with __FILE__)
- CInline.c:324 #define set_object_sclass SetObjectSFlags alias
- CInline.c:125-142 CInline_0050f240_inline1/2: gc1-address-named copies
- CInline.c:202 TODO'd copy of CInline_GetStatementNumber
- CInline.c:3776-3778, :3851 placeholder fields funcdata->f1c, ->f98, packstmt->f08
- CInline.c:557, :1529, :1545, :3054, :3546, :4309, :4329, :4334 literal allocation sizes (0x1e, 12, 0xa6, 0xe)
- CInline.c:84-86, :290, :373-375 duplicate extern declarations of the unit's own statics

Next:
1. Rewrite CInline_PackActions (and CInline_UnpackActions, which shares the layout) from Pro7 over ExceptionAction
2. Replace the invented helpers with Pro7's statics (CInline_GetLocalID, CInline_TranslateID, CInline_SetObjectSFlags, CInline_NewStatement ...) and remove the CERROR_FILE defines and the alias
3. Name the .bss statics from Pro7 by .bss order; move symdecloffset and the cparser_fileoffset fragment to their owners
4. Use Pro7's CI_FuncData/CI_Statement field names in place of f1c/f98/f08 and sizeof for allocations

### src/frontend/CInt64.c

C, effort M, confidence high; 46/46 exact, linked. 1.x/decompiler-style rewrite in binary order (Pro7 unsorted/CInt64.c bodies, inlined helpers spelled out).

Links from source (46/46), but it is not Pro7's file. The functions are in binary order, which is Pro7's order reversed, so the original was built deferred. It is built non-deferred, with about 19 private helpers standing in for the inlining: modu, divu, xor64, unmask, isneg, inv, neg, isneg_ptr, Neg, sub, lneg, not64, recovered_complement_*, CInt64_Neg_Inline.

Names: Exported names are Pro7's. The static helpers have invented, decompiler-style names (`recovered_complement_CInt64_Neg`, `recovered_result`, `CInt64_Neg_Inline`). Pro7's CInt64_SignedCompare, ScanAsmNumber, PrintHex and PrintBin are absent: either inlined, or dead and needed for .data.

Data: powers_built, next_power and powers_of_two are Pro7's statics. cint64_one and cint64_min are raw named.

Structure: Should be `-inline auto,deferred` in Pro7's order. It is written in address order with a TODO at line 782 that says so.

Hacks:
- src/frontend/CInt64.c:774-935 private copies of CInt64_Neg/Inv (recovered_complement_*, CInt64_Neg_Inline, lneg, Neg)
- src/frontend/CInt64.c:12-90 helpers modu/divu/xor64/unmask/isneg/inv/neg/isneg_ptr that stand in for Pro7's inlined CInt64_* calls

Next:
1. Rewrite from Pro7's unsorted/CInt64.c in Pro7's order and build it -inline auto,deferred (as was done for CError and the rest of the front end)
2. Delete the private helpers once the deferred build inlines CInt64_Neg/Inv itself; check that relink_units --only still links

### src/frontend/CObjC.c

C, effort L, confidence high; 53/55 exact, not linked. gc1/1.x CObjC.c + CObjCModern.c with invented types, written in binary order (non-deferred); the bodies compile to 2.6's code.

52/55 exact, but it is not Pro7's file. Types and names are gc1-invented (MethRec, CRec, MessageArgument, CObjCInfoRec, HashEntry) where Pro7 has ObjCMethod, ObjCProtocol and ObjCNamedArg. Pro7's helpers are duplicated as per-site static copies to imitate inlining: four versions of CObjC_FindObjCType (FindNamedPointerType, FindNamedPointerType_504fb0, find_named_pointer_type, lookup_named_pointer_type) and of GetObjCType_id (GetIdType, GetIdType_504fb0, get_id_type, lookup_id_type, find_id_type). The binary is Pro7's order reversed, so the original was deferred.

Functions:
- `CObjC_ParseEncodeExpression` 84.5: wrong inlining: 2.6 inlines CObjC_GetObjCType_id (FindObjCType("id") with the class_pointer_type/id_type cache) here; ours calls the out-of-line `CObjC_FindObjCClass(1)` (36 instructions missing)
- `CObjC_ParseMethod` 93.1: shape/registers
- `CObjC_GetProtocolObject` 99.1: one register

Names: Misnamed: `CObjC_FindObjCClass(Boolean)` (line 2989) is Pro7's `CObjC_GetObjCType_id`; Pro7's CObjC_FindObjCClass takes (HashNameNode *, Boolean). `CObjC_IsIdOrSelType` is Pro7's CObjC_IsType_id. `CObjC_SplitNameIntoMessageArguments` is invented (TODO). `fn_00504c90_inline1` is address-named. Globals are 1.x names: data_00587140 -> cobjc_currentclass, data_00588064 -> cobjc_protocols (likely), data_00588507 -> cobjc_encodemethod (likely), selector_hash -> cobjc_selhashtable, class_pointer_type -> cobjc_type_class, id_type -> cobjc_type_id, sel_type -> cobjc_type_sel, objc_string_constant_count -> cobjc_stringcount, data_00587f6c and selector_reference_count (CObjCModern.c:46-47) -> cobjc_classrefcount/selrefcount?

Data: String literals are defined as named globals (objc_category_prefix ... objc_instance_method_list_prefix, cobjc_filename[] at lines 1163-1167); they should be literals at their use. data_00588064 is defined in CDecl.c:64 rather than here.

Structure: Should be -inline auto,deferred in Pro7's order (unsorted/CObjC.c then the #included CObjCModern.c). AGENTS says a deferred trial broke functions, which is expected while the duplicated helpers stand in for inlining.

Hacks:
- src/frontend/CObjC.c:1 `#define CERROR_FILE "CObjC.c"`
- src/frontend/CObjC.c:1301 `#define CERROR_FILE ((char *)&cobjc_filename)` and explicit CError_Internal(cobjc_filename, ...) at 2243 and 2730
- src/frontend/CObjC.c:2971 `#define CERROR_FILE "id"`: nonsense ("id" is the type name find_id_type looks up, not a file); nothing below it asserts
- src/frontend/CObjC.c:79,95,332-370,1875-1916,2316,2676-2692,2920-2989 duplicated static copies of Pro7's FindObjCType/GetObjCType_id/GetObjCType_Class/IsType_id/FindObjCType_SEL
- src/frontend/CObjC.c:117 address-named fn_00504c90_inline1
- src/frontend/CObjC.c:1454,1560,2333,2376 `#pragma opt_propagation off/reset` with no TODO
- src/frontend/CObjC.c:695 `register` parameter

Next:
1. Rewrite from Pro7's unsorted/CObjC.c and CObjCModern.c with Pro7's types (ObjCMethod, ObjCProtocol, ObjCNamedArg, ObjCSelector ...) and globals, in Pro7's order, -inline auto,deferred; keep 2.6's differences visible in the current matching bodies
2. Drop the CERROR_FILE defines and the cobjc_filename global (the include of CObjCModern.c restores __FILE__)
3. Turn the named prefix strings into literals; move data_00588064's definition here

### src/frontend/CParser.c

C, effort M, confidence high; 89/89 exact, linked, deferred. mixed: Pro7's order and names over gc1 text (about 29% of lines gc1-only, 28% Pro7).

87 of 89 rows are exact, but much of the text is gc1's. Raw token numbers stand in for TK_ constants in TryIsDeclaration, _Setjmp and lift_value_0 are leftovers, and a duplicated pair of save/restore helpers stands in for Pro7's CParser_InsertTryBlock/CParser_RemoveTryBlock. The data is the main problem: the TypeStruct fields of catchinfostruct and ptmstruct, and cparser_fileoffset.tokenline, are written through separately bound fragment globals (_DAT_0058843e, data_00588454 ...). Under AGENTS' two-bindings rule, the equivalent compiler never sets the real fields.

Functions:
- `CParser_UpdateObject` 94.7: shape (registers)
- `CParser_FindDeallocationObject` 98.7: shape (registers)

Names: Placeholders, with their Pro7 names: data_00580dc0 = uniqueid (static), data_005dcc70 = uniquenamespacename (static, declared extern twice at :123 and :128), DAT_00587fd8 = name_obj_check, data_0058852b (also used in CExpr2), DAT_0058852e, s_vector_0055d638 (the "__vector" literal), fn_004bf0a0 (a CMachine row called from :2158), fn_0048be40_inline1 (:3660). The invented save/restore (:1545, :1561) and CParser_SaveState/RestoreState (:1692, :1704) are two copies of Pro7's CParser_InsertTryBlock/CParser_RemoveTryBlock. CParser_ClassHasMutableMember is invented with a TODO.

Data: Fragments to fold into their structs: _DAT_0058843e/DAT_0058844a/_DAT_0058844c are catchinfostruct.size/stype/align, and _DAT_0058847e/DAT_0058848a/_DAT_0058848c are ptmstruct's. Both structs are defined in other units as the 1.x 'Type' (CException.c:53, CExpr.c:70); Pro7 defines them in CParser.c as TypeStruct. data_00588454 (defined in CInline.c, used in CParser, CInline and CTemplateNew) is cparser_fileoffset.tokenline (0x5eb1c6 = cparser_fileoffset+4). Pro7's CParser globals block (newp_fobj ... Xunex_func) is spread over other units.

Structure: Deferred, in Pro7's order apart from CParser_IsAltiVecPrefix and CParser_PrintUniqueID. The range ends with two foreign functions, gecko_load_store_register (0x4ea490, PCodeUtilities.c) and Select_Gecko_Load_Store (0x4ea630, Operands.c), and starts after PCodeUtilities' insert_load_immediate_after and load_immediate (0x4e3d00, 0x4e3d80). PCodeUtilities' rows are thus split around CParser, so the neighbouring unit boundaries need a look. Missing Pro7 functions: CParser_FindPublicRuntimeObject, CParser_InsertTryBlock, CParser_RemoveTryBlock, CParser_TryFuncDecl, is_cfm_type, CParser_IsVTableObject, CParser_FindOverloadFunc (probably inlined into callers by hand or under other names).

Hacks:
- CParser.c:56-57 private va_start/va_arg macros
- CParser.c:103-109, :367-374 struct-field fragments as separate globals
- CParser.c:1580-1600 raw token numbers (262..286, 372, -3) in place of TK_ names; TryIsDeclaration tests dtype->type != TYPETEMPLATE through TypeIntegral->integral (gc1's guessed types)
- CParser.c:1610 _Setjmp in place of setjmp
- CParser.c:487-490 lift_value_0 temporary
- CParser.c:723 lalloc(68U) and :858 galloc(24): literal sizes (AGENTS: write sizeof, check against the push)
- CParser.c:655, :750, :794 volatile DeclInfo parameters (no TODO)
- CParser.c:1545-1572 and :1692-1712 duplicate try-block save/restore

Next:
1. Fold the fragments into catchinfostruct, ptmstruct and cparser_fileoffset: bind each struct's start, delete the fragment bindings, and define the structs as Pro7's TypeStruct where 2.6's .bss says (a functional bug in the equivalent compiler today)
2. Rename data_00580dc0/data_005dcc70 to uniqueid/uniquenamespacename and make them static once nothing lifted uses them
3. Replace save/restore and CParser_SaveState/RestoreState with Pro7's CParser_InsertTryBlock/RemoveTryBlock
4. Rewrite TryIsDeclaration, isdeclaration and islookaheaddeclaration from Pro7 (TK_ names, IS_TYPE_TEMPLATE)
5. Replace the volatile parameters and va macros
6. Check the PCodeUtilities/Operands split around this range

### src/frontend/CSOM.c

C, effort M, confidence medium; 37/37 exact, linked, deferred. gc1 CSOM.c bodies under Pro7 public names, deferred.

Links from source (37/37), but it carries gc1's static helpers and names. 16 of Pro7's CSOM_* statics (NameTranslate, FindClassType, GetTokenTableIndex, GenerateMigrateData, GenerateSlotUsage, GenerateSignature, ...) are absent, and invented ones (MakeKinds, MakeWords, MakeOverrides, FlushData, GetOwner, setnumber, CSOM_CopyName, CSOM_004e38b0_inline1, CSOM_004e4a30_inline1) stand in. Two unidentified 2.6 stubs sit at the top of the file.

Names: Placeholders: fn_004e67a0 (empty stub at 0x525eb0) and CSOM_NoOp (0x525ec0, clears data_005e7322, a flag InlineAsm.c also uses). Both are OPEN in docs/names-2.6.md. Static helpers have gc1 or address-derived names.

Data: data_005e7322 is a placeholder COMMON shared with InlineAsm.c and FuncLevelAsmPPC.c: name it from Wii publics or hash order.

Structure: ok (deferred); check whether the two stubs belong to CSOM at all (Pro7 has nothing there)

Hacks:
- src/frontend/CSOM.c:256-266 fn_004e67a0 / CSOM_NoOp stubs
- src/frontend/CSOM.c:78,111 address-named inline helpers
- src/frontend/CSOM.c:128,217-244 gc1 helper names where Pro7 has CSOM_* statics

Next:
1. Map each gc1 helper to its Pro7 CSOM_* static (align_reference.py --rev) and rename, keeping the code identical
2. Identify data_005e7322 (Wii publics / data_refs) and the two stubs
3. Relink check after each rename

### src/frontend/CTemplateClass.c

C, effort L, confidence high; 30/31 exact, not linked. gc1-shaped CTemplateClass.c (invented types, magic numbers) in binary order, non-deferred; matches but is not Pro7's form.

31/32 exact, but it is written with gc1/invented types and numbers: TemplateContext, TemplateClassDeclaration, ClassTemplateSpecialization, `flags & 256U`, `lalloc(0x14)`, `unsigned char` returns. It is in binary order, where the original was deferred: the binary runs Pro7's order backwards, from CTempl_InstantiateTemplateClass first to GetMasterTemplate last. It also holds a foreign function: CTemplateClass_ParseDouble at 0x567e40 is a strtod wrapper reading MSL's TLS errno, called only by CMachine.c:1119. Wii evidence calls it `ScanFloat` (module ScanFloatX86), a separate object between CTemplateFunc and CTemplateClass.

Functions:
- `CTemplClass_CopyObject` 98.8: one register (edx/ecx) in a switch

Names: Pro7 function names, except invented helpers master_template_inline (Pro7 CTemplClass_GetMasterTemplate inlined), add_object_mapping, qualtest and CTemplateClass_ParseDouble (really ScanFloat, OPEN in names-2.6.md). Pro7's SetupActionErrorRef, RestoreActionErrorRef, NewDefAction, InsertTemplateAction, TypeParamCompare, PartialClassIsMoreSpecialized, CopyObjMemberVarPath, CopyObjTypeTag, CompleteObjectInit and CopyUsingDecl are absent (inlined or written out).

Data: Little data of its own. raw_at is 224 bytes in 5 runs.

Structure: Should be -inline auto,deferred in Pro7's order (AGENTS: a deferred trial broke functions). CTemplateClass_ParseDouble should go to its own unit (ScanFloatX86.c or similar) at 0x567e40.

Hacks:
- src/frontend/CTemplateClass.c:1784 foreign function CTemplateClass_ParseDouble (ScanFloat)
- src/frontend/CTemplateClass.c:17-26 master_template_inline: a private inline copy of CTemplClass_GetMasterTemplate with `flags & 256U` and CError_FATAL(42)
- src/frontend/CTemplateClass.c:398 `lalloc(0x14)`: a literal size (AGENTS: write sizeof)
- src/frontend/CTemplateClass.c:1099 stale comment about the CERROR_FILE string

Next:
1. Split CTemplateClass_ParseDouble into its own source as ScanFloat (check Wii's module and name) and fix CMachine's call
2. Rewrite from Pro7's CTemplateClass.c with Pro7's types (TemplClass, TemplateAction, TypeDeduce, TemplPartialSpec), in Pro7's order, deferred; reuse the matching bodies' 2.6 differences
3. Replace literal sizes and flag numbers with sizeof and Pro7's constants

### src/frontend/CTemplateNew.c

C, effort L, confidence high; 24/25 exact, not linked. Pro7 CTemplateNew.c logic with gc1/invented template types, written in binary order (non-deferred).

24/25 exact, but it is not in Pro7's shape. Functions run in binary order (Pro7's reversed: the original was deferred), built with plain -inline auto. Types are invented (TypeClassExt800, CTStateElem, TemplateSpecializationData, ...). The file also defines InlineAsmRegistersPPC's and InlineAsmMnemonicsPPC's literals and .bss, generated from the original's data.

Functions:
- `CTempl_ParseTemplArgs` 92.9: shape: three extra stores in the original's prologue/loop (a local array/struct initialised in 2.6) plus registers

Names: Function names are Pro7's. Globals do not use Pro7's names: class_template_list (ctempl_templates), templateFunctions (ctempl_templatefuncs), template_recordbrowseinfo (CBrowse's gForceSourceLoc), source_line (CParser's symdecltoken.tokenoffset), data_00588454 (cparser_fileoffset.tokenline: a second binding inside a struct). Invented inline helpers: TemplateObjectInstance_Source, CTempl_GenMemberInstance26, and an unused CTemplateNew_ShouldWarnUnsupportedInlineAsmRegister (line 1884). The comment at line 195 names fn_004763e0.

Data: Lines 20-45 'Strings/Globals generated from the original's .data/.bss' are another unit's data: "r", "f", "v", "S", "cr", registration_message_format, inlineAsmRegisterHashTable, secondary_registration_hash, inlineAsmRegisterEntry, asmOperandPatternLookup, the DAT_00584cb4..cc fragment arrays and data_0058243x all belong to InlineAsmRegistersPPC.c/InlineAsmMnemonicsPPC.c. ctempl_parseparse and ctempl_scanfuncparams are Pro7 statics, global here.

Structure: Should be -inline auto,deferred in Pro7's order (AGENTS: a deferred trial broke functions, so the types and shapes need fixing first). CTempl_ParseTemplArgType from Pro7 is present; Pro7's CTempl_MemberParseLex/MemberParseTempl/ParseTemplateMember/ParseMemberFunction/GenMemberInstance are absent (inlined in 2.6?).

Hacks:
- src/frontend/CTemplateNew.c:20-45 InlineAsm units' strings and .bss defined here (fake fragment arrays `void *DAT_00584cb4[4/sizeof(void *)]`)
- src/frontend/CTemplateNew.c:1884 unused invented inline CTemplateNew_ShouldWarnUnsupportedInlineAsmRegister
- src/frontend/CTemplateNew.c:50-54 a one-field `struct ParserPosition` stand-in
- src/frontend/CTemplateNew.c:220,291 data_00588454 binding inside cparser_fileoffset
- src/frontend/CTemplateNew.c:65 and elsewhere: magic flag tests (`object->flags & 4`, `& 2`)

Next:
1. Move the InlineAsm literals and .bss to their units (InlineAsmRegistersPPC.c / InlineAsmMnemonicsPPC.c), as real statics, string literals or COMMONs as the .bss evidence says
2. Rename the globals to Pro7's (ctempl_templates, ctempl_templatefuncs, gForceSourceLoc, symdecltoken.tokenoffset, cparser_fileoffset.tokenline)
3. Move the template types to Pro7's (TemplClass, TemplClassInst, TemplArg, TemplFuncInstance) in templates.h: shared work across the four CTemplate units
4. Then rewrite in Pro7's order -inline auto,deferred, -sym off kept

### src/frontend/Unmangle.c

C, effort M, confidence high; 13/13 exact, linked. gc1's Unmangle.c (about 75% gc1 text, no Pro7 text): Ghidra-style, all exact.

All 13 rows are exact, but the source is gc1's transliteration rather than Pro7's file: hand-inlined buffer helpers (put_2, put_c, PutChar, AppendText, put, scan, scan_component, CopyTemplate), _Setjmp, `""[0] == '.'` tests, an int* context in place of a jmp_buf, and a gc1 MOutBuf. The binary runs MWUnmangle (lowest) to Unmangle_TemplateParams (highest), the reverse of Pro7's order, so 2.6 built it deferred. Ours is written in binary order with plain -inline auto. The link is blocked by MSL data (locale.c/float.c data in its .data range, and the lconv field), which is a known issue.

Names: The 13 rows carry Pro7's names. The helpers are invented, where Pro7's are Unmangle_Error, Unmangle_BufferInit, Unmangle_BufferAppendChar, Unmangle_BufferAppendString, Unmangle_BufferTerminate and Unmangle_ClassName (all inlined in 2.6). #define false/true at :24-25.

Data: Only literals. .data placed, but its range takes MSL's locale.c and float.c data (unit_status: undefined ___lconv; wcstold.c.obj .data size mismatch).

Structure: Should be deferred, in Pro7's order (TemplateParams first ... MWUnmangle last); it is currently non-deferred, in binary order. Signatures differ from Pro7's (int *context in place of jmp_buf, char * in place of const char *, MOutBuf in place of UnmangleBuffer). 5 clang errors.

Hacks:
- Unmangle.c:24-25 #define false 0 / true 1
- Unmangle.c:43, :47 `""[0] == '.'` / `""[0] == '_'` Ghidra artefacts
- Unmangle.c:57, :190, :212 _Setjmp
- Unmangle.c:158-1210 hand-written inline copies of Pro7's buffer helpers
- Unmangle.c:17 stale comment about a 'type extension at 0x0e'
- 5 clang errors

Next:
1. Rewrite from Pro7's unsorted/Unmangle.c with Pro7's statics and UnmangleBuffer, built -inline auto,deferred, and check that all 13 stay exact
2. Fix the MSL locale.c lconv field and data_units mapping (AGENTS' note) so the unit can link

### src/msl/critical_regions_win32.c

C, effort S, confidence high; 4/4 exact, linked. Ghidra-style hand decompilation of MSL critical_regions.win32.c.

Links, but the body is raw decompiler output (iVar1, lpCriticalSection, `return;`, DAT_0057d228) where lib/msl/MSL_Win32/Src/critical_regions.win32.c has the real 4-function source with `static CRITICAL_SECTION cs[num_critical_regions]`.

Names: DAT_0057d228 is MSL's static `cs`

Data: static array correct in size (5), placeholder name

Hacks:
- src/msl/critical_regions_win32.c:5: DAT_0057d228 placeholder
- src/msl/critical_regions_win32.c:8-33: decompiler-style bodies (iVar1, no braces style)

Next:
1. Replace with `#include <critical_regions.win32.c>` (MSL_Win32/Src) like the other MSL units; if the int region parameter differs (the repo takes unsigned), keep the library and note the difference

### src/msl/strtoul.c

C, effort S, confidence high; 4/4 exact, linked. MSL/runtime library source via #include wrapper plus a placeholder-named atoi.

Links, but MSL's atoi (strtol(s, 0, 10), 0x4216d0) is defined by hand as fn_00421a50. PCodeInfo.c reaches it through `#define atoi fn_00421a50`, a cast-call alias of the kind AGENTS.md says to remove.

Functions:
- `fn_00421a50` 100.0: exact, but this is MSL's atoi under a placeholder name

Names: fn_00421a50 is atoi (MSL strtoul.c defines atoi as strtol(str, NULL, 10))

Hacks:
- src/msl/strtoul.c:6: fn_00421a50 placeholder for atoi
- src/backend/PCodeInfo.c:74-75: extern fn_00421a50 + `#define atoi fn_00421a50` alias

Next:
1. Rename fn_00421a50 to atoi (rename.py, symbols.txt too); check lib/msl strtoul.c and use its atoi if it compiles the same
2. Drop the alias in PCodeInfo.c and include <stdlib.h>

### src/optimizer/Alias.c

C, effort M, confidence high; 11/21 exact, not linked. Pro7 Alias.c plus 2.6 additions written from the disassembly.

11/21 exact; the bodies are Pro7's or 2.6 helpers decoded from the binary (fn_005121e0 94%, fn_00512b30 99%, make_alias 97.5%, update_alias_value 89%). 5 rows are unbuilt (must_alias_object, may_alias_object, may_alias_worst_case, is_safe_const, fn_005118f0), although UseDefChains, Scheduler and CodeGen call them. It reaches the 1.x use-def globals through `#define` aliases and a fake-offset `reg_Defs` macro, and it is built non-deferred in binary order.

Functions:
- `must_alias_object` 0.0: unbuilt (Wii name; Pro7's uniquely_aliases region)
- `may_alias_object` 0.0: unbuilt (Pro7 has it)
- `may_alias_worst_case` 0.0: unbuilt (Pro7: may_alias_alias(a->alias, b->alias))
- `is_safe_const` 0.0: unbuilt (Pro7's first function)
- `fn_005118f0` 0.0: unbuilt 2.6 addition (a virtual object's alias via CParser_GetUniqueName; CodeGen.c:672 calls it)
- `fn_00512670` 67.1: 2.6 split of Pro7's addresspropagatestouse body; shape, probably deferred inlining

Names: Placeholders: fn_005121e0, fn_00512370, fn_00512670, fn_00512b30, fn_00511780 (at 0x5131c0) and fn_005118f0 (at 0x513290). The last two carry another version's addresses, which misleads. All are 2.6 additions with no Pro7 or Wii name: they need invented names with TODOs. data_005e2fdc is an unnamed static. Uses/Defs/reg_Defs/n_real_gprs are macro aliases to the 1.x names.

Data: Pro7's statics (aliases, n_aliases, n_gathered_aliases, alias_hash) are declared extern and defined as globals. They should become static once nothing lifted uses them. worst_case_memory_type(_global) is initialised data. raw_named lists 10 names (most are other units' COMMONs); raw_at 640 bytes.

Structure: Should be deferred: the binary has Pro7's order reversed (update_all_alias_values lowest, is_safe_const highest), with 2.6's helpers placed where Pro7's statics were. The source is in binary order.

Hacks:
- Alias.c:62-70 a private copy of the Propagation record type (also include/compiler/Propagation.h as CopyPropConfig, with 2.6's extra mask field: the two disagree in size)
- Alias.c:99-100 `#define Uses cm_entries` / `#define Defs code_motion_entries` aliases
- Alias.c:103 `#define reg_Defs ((&code_motion_register_definition_heads) - 4)` fake array by pointer arithmetic
- Alias.c:101 `#define n_real_gprs (n_real_registers[4])`
- Alias.c:77-97 externs for its own and other units' functions/globals in the .c
- Alias.c:321 data_00587fe4[...].definition_sets[2] for Pro7's usedefinfo[...].defvec8

Next:
1. Write the 5 unbuilt functions (Pro7's may_alias_object, may_alias_worst_case, is_safe_const; must_alias_object and fn_005118f0 from the disassembly)
2. Reorder into Pro7's order and build deferred
3. Once UseDefChains.h has Pro7's Defs/Uses/reg_Defs/usedefinfo, remove the aliases; move Propagation into one header with 2.6's 32-byte layout
4. Invent TODO-marked names for the six fn_ rows (e.g. the gather/propagate helpers) via name_review

### src/optimizer/CIRTransform.c

C, effort S, confidence high; 3/3 exact, linked, deferred. 1.x-style stand-in for Pro7 Common/CIRTransform.c's only live function; boundary and build flag wrong.

3/4 exact (two empty stubs plus Bv_IsEmpty). CIRTrans_TransformOpAss (42.6%) is a wrong body. The original is Pro7's: it reports CError_Error(142) and returns nullnode() when the left side is not EINDIRECT, then runs Pro7's inlined MultiAccessOperand setup (memclrw of the 0x14-byte mop, assert 527). Ours CError_FATALs instead and calls through casted function pointers. Two layout problems: the original orders TransformOpAss < Cleanup < Setup, the reverse of Pro7's file, so the unit is deferred; and Bv_IsEmpty at 0x52bcc0 is IroBitVect's first function (Pro7's last, deferred), not this unit's.

Functions:
- `CIRTrans_TransformOpAss` 42.6: wrong body (no CError_Error(142)/nullnode branch, not Pro7's MultiAccessOperand shape, cast-call hacks): rewrite from Pro7 with CIRTrans_SetupMultiAccessOperand/GetMultiAccessOperand/InitMultiAccessExpression as statics

Names: Pro7 names for the rows. The rtfunc statics are named cirtrans_rtfunc8/4/2 (invented); Pro7 has StrangeRuntimeFunction statics.

Data: Pro7's runtime-function type statics are kept for .data (comment says so). 2 anonymous raw items (72 bytes).

Structure: Should be -inline auto,deferred in Pro7's order (Setup, Cleanup, ..., TransformOpAss; dead functions as needed for .data). Move Bv_IsEmpty to IroBitVect.c; the unit's .text should end at 0x52bcc0.

Hacks:
- CIRTransform.c:76-148 `((ENode * (*)(Object *)) create_objectnode)` style cast calls throughout
- CIRTransform.c:56-62 anonymous 'state' struct standing in for Pro7's MultiAccessOperand

Next:
1. Rewrite from Pro7 Common/CIRTransform.c in Pro7's order, deferred, with its statics (MultiAccessOperand helpers, StrangeRuntimeFunction tables) so .data lines up
2. Move Bv_IsEmpty to IroBitVect.c (assign_source/move_function) and fix the split boundary

### src/optimizer/CodeMotion.c

C, effort M, confidence high; 12/19 exact, not linked. Pro7 middle (exact) between 1.x use-def code and 1.x tail.

12/19 exact: simpleunswitchloop..maymove are Pro7's. Lines ~66-660 are row-less 1.x use-def code (propagate_use_sets, solve_definition_sets, compute_block_definition_and_use_sets, build_use_definition_entries, assign_definition_and_use_starts, COpt_00524b20), used by UseDefChains' 1.x computeusedefchains. The tail functions are 1.x: they test the 1.x operand kinds (kind 0/1/9, PCOp_FPR/PCOp_VR) and use the separate per-class head scalars instead of reg_Defs[rclass]. isloopinvariant is 13%. moveinvariantsfromloop calls a 1.x inlined maymove twin (CanMove524d90 / has_use_outside_loop_in_preheader_set).

Functions:
- `isloopinvariant` 13.3: 1.x body (PCOp_FPR/PCOp_VR kinds, per-class scalars)
- `uniquelyreachesalluses` 29.4: 1.x body (kind 0/1/9 branches); Pro7 uses reg_Uses[rclass]
- `moveinvariantcomputation` 30.9: 1.x body
- `uniquelyreachesuse` 35.1: 1.x body with fn_00526950_inline1
- `isuniquedefinition` 49.6: 1.x body
- `moveinvariantsfromloop` 67.6: calls the 1.x CanMove524d90/has_use_outside_loop_in_preheader_set instead of Pro7's maymove/isliveonexit
- `simpleunswitchloop` 96.0: near miss

Names: 1.x use-def globals stand in for Pro7's UseDefChains.h: code_motion_entries=Defs, cm_entries=Uses, codeMotionEntryCount=number_of_Defs, data_00587e38=number_of_Uses, data_00587fe4=usedefinfo, code_motion_register_definition_heads/data_00587f04/register_definition_heads=reg_Defs[GPR/FPR/VR], code_motion_register_use_heads/codeMotionUseEntryHeads/register_use_entry_heads=reg_Uses[GPR/FPR/VR], gCodeMotionObjectTree_005880ac=objectusedefs, gCodeMotionAllocationList_005870fc=objectusedeflist. Here also: gCodeMotionChanged (movedloopinvariantcode), gCodeMotionCounter_005880b8 (unswitchedinvariantcode), DAT_00574ce8 (srawi_addze_isloopinvariant's function-local `static PCode *oldNextInstr`), data_005871a4 (LoopDetection's loopdetection_nblocks), CMDefInfo, CMIdentity/CMKind/CMInit helpers, *524d90 helper names.

Data: Defines a block of 1.x COMMONs that are UseDefChains' and LoopDetection's (loopsinflowgraph, register heads, object tree). DAT_00574ce8 should be a function-local static. .data/.bss placed.

Structure: Should be deferred: the binary has Pro7's order reversed (moveloopinvariantcode lowest, isloopinvariant highest). The 1.x use-def block belongs to no row.

Hacks:
- CodeMotion.c:66-660 row-less 1.x use-def implementation
- CodeMotion.c:59-65 CM_BIT/CM_SET/CM_CLR/CM_TEST private bit macros (Pro7: bitvectorgetbit)
- CodeMotion.c:672-693 *524d90 inline helpers named after 1.x addresses
- CodeMotion.c:1483 `#define CM_REG_USES ((&register_use_entry_heads) - 2)` fake array
- CodeMotion.c:1545 has_use_outside_loop_in_preheader_set: 1.x duplicate of isliveonexit
- CodeMotion.c:1581 fn_00526950_inline1
- CodeMotion.c:1332 DAT_00574ce8 global for a function-local static
- CodeMotion.c:721 a `0x00020460` flag literal (Pro7: fIsCall|fIsPtrOp|fIsVolatile|fSideEffects)

Next:
1. After the UseDefChains.h conversion, rewrite the 6 tail functions from Pro7 and replace moveinvariantsfromloop's helper with maymove
2. Delete the 1.x use-def block, is_object_access_compatible and has_use_outside_loop_in_preheader_set
3. Pro7 order, deferred; the hand-unrolled moveinvariantsfromloops then becomes Pro7's plain recursion
4. Make oldNextInstr a function-local static (binding _oldNextInstr$N)

### src/optimizer/IroLinearForm.c

C, effort M, confidence high; 34/34 exact, linked. Pro7 logic in binary order (not deferred) under 1.x names and duplicated helpers.

34/34 exact and links, but the source is not clean. Functions are written in the binary's order with plain -inline auto, although the original is Pro7's file deferred. There are at least eight invented or 1.x helper names, several of them identical bodies (NewENode, IRO_NewNode, new_enode, NewNode, NewIRStat, NewPendingStmt, NewIrNode, NewInsn/new_linear). It uses a 1.x `struct IroSavedState` where Pro7 has IROLinearIRSave, and a `List12` fake struct for Pro7's null-check temp list.

Names: Invented or 1.x: IRO_MarkSubExpr/IRO_MarkAllSubExprs (Pro7 MarkSubExpr/MarkAllSubExprs), NewIrNode/NewIRStat/NewPendingStmt (Pro7 NewStatement), NewNode/NewENode/IRO_NewNode/new_enode (Pro7 IRO_NewENode), NewInsn/new_linear/append_linear (Pro7 IRO_NewLinear/LinkLinear), LookupTemporary (Pro7 GetTempFromtheList), IRO_RenumberInts_Inline (TODO-marked). Pro7 functions with no counterpart named: AssignDangerousArgumentToTemp, PutTempOnNullCheckList. List12 should be Pro7's struct, and IroSavedState Pro7's IROLinearIRSave (flag0/flag1/flag2/word1c are DisableDueToAsm/IRO_IsLeafFunction/IRO_FunctionHasReturn/statement_insertion_point).

Data: Statics as the original (it links). `lalloc(0x1e)`/`memset(.., 0x1e)` literal sizes for ENode/Statement: AGENTS warns about literal sizes hiding overruns; check against sizeof.

Structure: Should be deferred in Pro7's order (docs/names-2.6.md 'Deferred units': reversed in the original). The binary-order layout links only while nothing is inlined across it.

Hacks:
- IroLinearForm.c:1 `#define LINEARFORM_FATAL(line) CError_Internal("IroLinearForm.c", line)`: CError_FATAL already uses __FILE__
- IroLinearForm.c:83 '(from IrOptimizer.bak.c's file scope)' comment
- IroLinearForm.c:205 `#define LOWER_ROOT_EXPRESSION` alias macro
- IroLinearForm.c:224-266 five identical lalloc(0x1e)+memset helpers
- IroLinearForm.c:270 `struct IroSavedState` 1.x fake struct (also duplicated in IroTransform.c)
- include/compiler/IroLinearForm.h:123 `struct List12` fake struct
- 2 clang errors (tu_metrics)

Next:
1. Rewrite in Pro7's order with -inline auto,deferred (AGENTS loop step 3), keeping bodies that match; use Pro7's names for helpers and IROLinearIRSave in IroLinearForm.h
2. Replace the literal 0x1e sizes with sizeof and compare the pushes
3. Fix the 2 clang errors

### src/optimizer/IroLoop.c

C, effort M, confidence high; 30/33 exact, not linked, deferred. Pro7 IroLoop.c in binary order (not deferred); Reducable is a non-Pro7 body missing 2.6 code.

29/33 exact. Reducable (78.9%) is not Pro7's shape: it is a nodeKind/variableNode rewrite 290 lines long, and it is 165 bytes shorter than the original. The original has a 2.6 branch it lacks: two copts flag tests (copts+0x81, copts+0xf1), an is_unsigned/size check against a type's size, a divisibility check (idiv, then return 0 unless the remainder is 0), and assert line 0x369 where ours has 0x34d. CheckLoopAddress (79.7) and ExtractLoopInfo (84.9) are Pro7 bodies with a spilled local: shape problems. RearrangeInvarianceInAddressExpr is 99.5.

Functions:
- `Reducable` 78.9: not Pro7's body, and missing 2.6's copts-gated unsigned/size check and divisibility test: rewrite from Pro7 plus the disassembly

Names: fn_004a8990 (0x4a8990) is an unnamed IRO_WalkTree visitor. TreeContains and NewAddendSum are invented and lack TODOs. data_005876bc is IroUnrollLoop's LoopTemp. LoopCandidate/loop_candidates (IroLoop.h:59) are Pro7's LoopList/LoopList_First. LoopTailNum, LoopExitSuccessor and LoopTail are extern-only with a 'not in the original's names' comment, but Pro7 defines them in IroLoop.c.

Data: Pro7's statics (RisList, AllKills, KnownBounds, Times, PredInt, FirstAddendLinear, LastAddendLinear) are globals 'while lifted functions use them'. Pro7's InLoop_Exits, InLoop_Tails, LoopExitNumber, LoopTailNum, LoopExitSuccessor, LoopTail and IRO_LoopMemRefFirst are raw named (11 named, 7 anonymous / 1 KB). Define Pro7's globals here.

Structure: Should be deferred in Pro7's order (docs/names-2.6.md: reversed in the original); the AGENTS loop's IroExprRegeneration split is already done.

Hacks:
- IroLoop.c:36 `BitVector *data_005876bc`
- IroLoop.c:179 `fn_004a8990` placeholder name
- IroLoop.c:45-48 externs of Pro7's own globals

Next:
1. Rewrite Reducable from Pro7's, then add 2.6's branch from the disassembly (identify the copts members at +0x81/+0xf1)
2. Convert the unit to Pro7's order with -inline auto,deferred, keeping matching bodies
3. Define Pro7's globals (InLoop_Exits ... IRO_LoopMemRefFirst) here, name fn_004a8990 with a TODO, rename the loop list to Pro7's
4. Then the CheckLoopAddress/ExtractLoopInfo spills

### src/optimizer/IroPointerAnalysis.c

C, effort L, confidence medium; 187/196 exact, not linked. Pro7 IroPointerAnalysis.c with IroPointerAnalysisADTs.c #included, not deferred, laid out by first use.

142/157 rows exact. No 1.x code: every body present is Pro7's. Eight rows (about 13 KB) have no source and are taken from the original: PointerAnalysis_ParseFunctionModifiesSpecifier, PointerAnalysis_ParseEntryPointsToSpecifier, ParseLocationSetSet, AssignEachInPointsToFunctionAction, GetPTF, EvalAssignAction2, FillInAppropriateMappingsWithExtParamAction, StoreReturnedLocations. They are only `extern`-declared ('analysis functions still taken from the original'). The other inexact rows are near-misses: MatchPTF 88.9, ParseLocationSet 92.1, GetActualLocsOfExtendedParam 97.8, ShouldAddNewPointsToEntryToFunction 98.3, PointsToFunction_Add 98.4, LocationSets_Overlap 98.6, Lookup 98.7, EvalExpr 99.6. Most of the unit's state (stUnknown, stPTFList, stExtParamSet, stCurrentProc ..., Pro7's statics) is only `extern`, so the data stays raw.

Functions:
- `PointerAnalysis_ParseFunctionModifiesSpecifier` 0.0: unbuilt: write from Pro7
- `PointerAnalysis_ParseEntryPointsToSpecifier` 0.0: unbuilt: write from Pro7
- `ParseLocationSetSet` 0.0: unbuilt: write from Pro7
- `AssignEachInPointsToFunctionAction` 0.0: unbuilt: write from Pro7
- `GetPTF` 0.0: unbuilt: write from Pro7
- `EvalAssignAction2` 0.0: unbuilt: write from Pro7
- `FillInAppropriateMappingsWithExtParamAction` 0.0: unbuilt: write from Pro7
- `StoreReturnedLocations` 0.0: unbuilt: write from Pro7

Names: Mostly Pro7 names; Pro7's own placeholders (x15, x18, flag37, *_sub_48xxxx) are kept. data_005dc1f8 (IroPointerAnalysis.c:1799) is Pro7's function-local `static int userBreakCounter` in EvalProcAction. docs/names-2.6.md leaves two rows doubtful: 0x473aa0 ObjectSets_Equal (? PointsToFunctions_Equal) and 0x481d40 ObjectSet_AddUnique (? PARegionSet_SimpleAdd, Wii's name).

Data: 17 named and 163 anonymous raw items (8.8 KB): the static state is extern-only. stUnknown, stUnknownPTF, stCallingContextStack, stParamObjs, stAbortPointerAnalysis, stCurrentProc, stExtParamSet, stPTFList, stMaxPassCount, stExceptionFNode, stUnknownLs/Mb, stEvalProcActionParams and Pro7's dummy memory block/location set are never defined in source. They should become Pro7's statics once no lifted function uses them; until then define them as globals bound with the usual comment.

Structure: Not deferred. docs/names-2.6.md finds no Pro7 order (Pro7's ADTs are CW_INLINE, emitted after their first user), so the binary-order layout with `inline` is plausible. The unit's text starts at 0x471e90, not 0x477b80: splits.txt gives the first 39 functions to IroFlowgraph. IroPointerAnalysisADTs.c redefines CERROR_FILE twice (lines 6 and 2816), the pattern AGENTS lists as left.

Hacks:
- IroPointerAnalysis.c:39-78 extern declarations of the unit's own statics and of the eight missing functions
- IroPointerAnalysis.c:1783 `#define IROLF_4000 0x4000` and :1784 private Bv_IsBitSet macro (BitVector.h's)
- IroPointerAnalysis.c:1799 `extern int data_005dc1f8`: Pro7's local static userBreakCounter
- IroPointerAnalysisADTs.c:6,2816 CERROR_FILE defines
- IroPointerAnalysisADTs.c:28 `#define CInt64_Neg CInt64_Neg` no-op alias

Next:
1. Write the eight missing functions from Pro7 (GetPTF, EvalAssignAction2 and the specifier parsers first: the biggest)
2. Define the static state (globals bound, with the lifted-code comment) so the data is no longer raw; make it static once every function matches
3. Fix the split start (0x471e90)
4. Then the near-misses: MatchPTF and ParseLocationSet first
5. Settle the two doubtful row names

### src/optimizer/IroTransform.c

C, effort M, confidence high; 26/31 exact, not linked, deferred. Pro7 logic in Pro7 order, deferred, but spelled through 1.x-style alias helpers and scaffolding.

26/31 exact and does not link. The remaining five (DoTransformations 85.3, TransformMonadicSelfAssignmentToDiadicSelfAssignment 91.0, DoTransformations11 95.5, DoTransformations24 97.4, FoldConstantsinAssociativeExprs 97.6) differ in registers and spills, not bodies. The function order follows Pro7, but Pro7's static helpers are replaced by duplicate stand-ins: replace_const, swapkids, RemoveCommonOp*, and fix26/fix30/replace_left/replace_right/CopyTypeInfo/copyExpressionMetadata, which are copies of ReplaceExprWithLeftChild/RightChild. isTrueConstant/isFalseConstant wrap IRO_IsConstantOne/Zero, and recordSimplification is an empty stub. Three more problems: `#define CERROR_FILE "unknown.c"` is wrong (the original's assert file string _@2284 reads "IroTransform.c"), DoLinearize is read through a copts offset macro, and the unit's static tables are never defined.

Names: fn_0045b720 (0x45b720, between DoTransformations22 and 23) is a 2.6 addition: the same 'insert a conversion, bool via two ELOGNOTs' helper as IROUseDef's IRO_AddTypeConversion (itself an invented name). Give both one invented name with a TODO. Rename the helpers to Pro7's: replace_const->ReplaceExprWithConst, swapkids->IRO_SwitchChildren, RemoveCommonOp{LeftLeft,...}->PickCommonsubAt{LeftLeft,...}. Fold fix26/replace_left into ReplaceExprWithLeftChild and fix30/replace_right into ReplaceExprWithRightChild, and drop isTrueConstant/isFalseConstant/recordSimplification/CopyTypeInfo/copyExpressionMetadata/CInt64_IsZeroInline/set_shift_constant13/MutEnumByte. Check after each step that the matches hold: 2.6 may really inline two copies.

Data: Pro7's `static ENodeType AssignmentOp/ComplementaryOp/ComplementaryOpLogical[MAXEXPR]` are only `extern UInt8` here, from IroTransform.h. They are never defined, so the built compiler takes them raw. Pro7's ExprType/IndirectRef/FirstTime/OperandConst/OperandObject globals are defined in IroCSE.c (ExprType as UInt8, FirstTime as char; Pro7 has ENodeType/Boolean). `char reverse_op_format[]` is a generated string global, not the literal at its use. The 30 anonymous raw items (2872 bytes) in .data are mostly the inexact functions' jump tables.

Structure: Order and deferred flag are right (Pro7 reversed in the binary). FrontendTransformSelfAssignmentToAssignment uses a private `struct IroSavedState` (a 1.x fake with flag0/flag1/flag2/word1c/List12) duplicated from IroLinearForm.c, where Pro7 has IROLinearIRSave in IroLinearForm.h.

Hacks:
- IroTransform.c:1 `#define CERROR_FILE "unknown.c"`: wrong, the original's string is "IroTransform.c" (0x5aa6e0). The inlined PromotedIntegralType assert (line 3681) therefore references the wrong literal, which is why TransformMonadic shows only an inferred reference
- IroTransform.c:64 `#define IroTransform_PreLinearize (*((UInt8 *)&copts + 0x2f3))`: copts 0x5eb092 + 0x2f3 = 0x5eb385 = _DoLinearize; use `DoLinearize` (IrOptimizer.h)
- IroTransform.c:65 REPLACE_CONST alias macro
- IroTransform.c:47-62 private 1.x `struct IroSavedState` and extern prototypes of IroLinearForm functions
- IroTransform.c:2270-2275 `CError_Internal("IroTransform.c", N)` written out instead of CError_ASSERT, a workaround for the bad CERROR_FILE
- IroTransform.c:85-310 duplicate static helpers (fix26, fix30, replace_left, replace_right, CopyTypeInfo, copyExpressionMetadata ...) that copy Pro7's
- IroTransform.c:45 `char reverse_op_format[]` generated string global
- IroTransform.c:70 `typedef enum { MutEnumByte_one = 1 } MutEnumByte;` dead stand-in

Next:
1. Delete the CERROR_FILE define, turn the explicit CError_Internal calls back into CError_ASSERT, and use DoLinearize; then check TransformMonadicSelfAssignmentToDiadicSelfAssignment
2. Move Pro7's IROLinearIRSave into IroLinearForm.h and use it in both units
3. Collapse the alias helpers into Pro7's statics (ReplaceExprWithConst, IRO_SwitchChildren, PickCommonsubAt*, switchFatherLeftMonadic, GetTypeSize), checking each with verify
4. Define AssignmentOp/ComplementaryOp/ComplementaryOpLogical as Pro7's statics; move ExprType/IndirectRef/FirstTime/OperandConst/OperandObject here with Pro7's types (COMMONs: check the hash order)
5. Then the register near-misses: DoTransformations (decl_order) and TransformMonadic

### src/optimizer/IroUnrollLoop.c

C, effort L, confidence high; 20/22 exact, not linked, deferred. Pro7-derived bodies in binary order (not deferred) with invented helpers; three rows missing.

19/22 rows exact, and the unit cannot link: UnrollForLoop (2027 bytes), UnrollWhileLoop (3239) and PatternMatchLoop (1134) have no source and are only extern-declared, so the relink fails with an undefined _UnrollWhileLoop. The present bodies match, but the file carries 1.x-flavoured scaffolding: private helpers (newconstant, constantref, CInt64_SetInt13, visit, IRO_MakeLong), a fake `IelStepConst` struct with `char pad[0xe]`, and invented loop-list names. The original is deferred (Pro7's order reversed), but the file is written in binary order with plain -inline auto.

Functions:
- `UnrollForLoop` 0.0: no source: write from Pro7 (with its inlined helpers GenInitialAssignment, GenNewInduction, BuildLoopExitTest, BuildEarlyLoopExitTest, NoOpBlock, IRO_LinearizeForLoopPostLoop)
- `UnrollWhileLoop` 0.0: no source: write from Pro7
- `PatternMatchLoop` 0.0: no source: write from Pro7

Names: combine_nonoverlapping_shifts (0x4a4190) is Pro7's CheckConstant (pending 'rename' in name review). The loop list is Pro7's `LoopList` (flags, bv, next, fnode, xE) and `LoopList_First`. The repo has LoopCandidate/loop_candidates, `value0e` for xE, and BitVector `data_005876bc` (defined in IroLoop.c) for Pro7's LoopTemp. UnrollStandardLoop is invented. Parameters carry 1.x names (holder, context, statement).

Data: 4 anonymous raw items (456 bytes, likely the missing functions' jump tables). `extern UInt8 VectorPhaseCalledFromUnroll` re-declares IrOptimizer's Boolean instead of including IrOptimizer.h.

Structure: Should be deferred in Pro7's order (docs/names-2.6.md: reversed in the original). Writing the three missing functions in Pro7's order is the moment to switch.

Hacks:
- IroUnrollLoop.c:47-51 `IelStepConst { char pad[0xe]; SInt32 lo; }` fake struct, used as `((IelStepConst **)info)[7]->lo` at :1424
- IroUnrollLoop.c:55-103 private helpers duplicating IroLoop/IroUtil ones (IRO_MakeLong is Pro7's IroUtil.h CW_INLINE)
- IroUnrollLoop.c:52 extern re-declaration of VectorPhaseCalledFromUnroll with a different type spelling
- IroUnrollLoop.c:1278-1279 extern declarations of the missing rows

Next:
1. Rewrite from Pro7 in Pro7's order with -inline auto,deferred, adding UnrollForLoop, UnrollWhileLoop and PatternMatchLoop and keeping the 19 matching bodies
2. Rename combine_nonoverlapping_shifts to CheckConstant, LoopCandidate to LoopList, data_005876bc to LoopTemp
3. Replace IelStepConst with the real IROLinear/ENode access

### src/optimizer/LoadDeletion.c

C, effort S, confidence high; 4/4 exact, linked, deferred. Pro7 rows plus 1.x load-liveness code; deletedeadloads elsewhere.

Its 3 rows are exact (Pro7's deleteload, loadpropagatestouse, is_load). The file also holds 3 row-less 1.x functions (LoadDeletion_BuildLoadLivenessSets etc.) and VectorArraysToRegs' static (data_00582c9c, Pro7's converted_arrays). Pro7's deletedeadloads and load_immediate_prop record are elsewhere: the function in ConstantPropagation.c, the record raw as data_005ce810. The binary shows the unit is exactly Pro7's LoadDeletion.c deferred: 0x5770e0 deletedeadloads, deleteload, loadpropagatestouse, is_load, with .data 0x5d1ac0-0x5d1b08 (strings plus the 32-byte record).

Names: Pro7 names for the rows. 1.x: LoadDeletion_* helpers, block_record_counts, load_liveness_record_start, data_00582c9c (actually VectorArraysToRegs' converted_arrays).

Data: The relink exclusion says it: '.data is 0 bytes, the unit's 28; declares as COMMON what the original has in an object's .bss (_data_00582c9c)'. The split's .data range 0x5d1aec-0x5d1b08 is off by 0x2c; it should be 0x5d1ac0-0x5d1b08. deletedloads should be defined here (Pro7).

Structure: Should be deferred, with the range starting at 0x5770e0 (deletedeadloads). AGENTS.md's note that LoadDeletion is the tail of VectorArraysToRegs.c does not fit the binary: VectorArraysToRegs' own object starts at 0x5771f0 (vectorarraystoregs) with its assert-name .data at 0x5d1b08.

Hacks:
- LoadDeletion.c:44 defines data_00582c9c, which is VectorArraysToRegs' static
- LoadDeletion.c:48-238 row-less 1.x LoadDeletion_* functions
- deleteload uses literal flag 0x100 (Pro7 fSideEffects)

Next:
1. Replace with Pro7's LoadDeletion.c (statics, load_immediate_prop with 2.6's 32-byte layout, deletedeadloads), -inline auto,deferred
2. Move deletedeadloads' row source here; fix the .text/.data splits (0x5770e0 / 0x5d1ac0); drop the data_005ce810 binding
3. Move data_00582c9c to VectorArraysToRegs.c as `static Boolean converted_arrays`

### src/optimizer/LoopDetection.c

C, effort M, confidence high; 13/17 exact, not linked. gc1/1.x bodies close to Pro7, non-deferred, with hand-unrolled recursion.

13/17 exact, but the source is the 1.x decomp's shape: binary order, insertpreheaderblocks hand-unrolled 7 levels deep to imitate deferred self-inlining, 1.x helper statics (AddNode/AddCount), CE_ASSERT. analyzeloop (29%) lacks Pro7's fPCBlockFlag2000 marking and checkcountingloop (48.6%) lacks Pro7's `args[2].kind != PCOp_LABEL` test. Both should be Pro7's bodies. insertpreheaderblock (31%) is a deferred-inlining mismatch of Pro7's insertpreheaderbefore/makepreheaderblock.

Functions:
- `analyzeloop` 29.2: 1.x body missing Pro7's fPCBlockFlag2000 branches (2.6 stores 0x2000 into block flags)
- `insertpreheaderblock` 31.1: 1.x shape; Pro7 splits it into makepreheaderblock/insertpreheaderbefore, which a deferred build inlines
- `checkcountingloop` 48.6: missing Pro7's PCOp_LABEL check (visible in the diff) and 1.x use-def accessors
- `computedominators` 92.7: shape; check Pro7's form under deferred

Names: Loop members invented (has_call, uses_count_register, skip_leaf_pass_4f, has_block_flag_40, has_memory_barrier for Pro7's x4D/x4E/x4F/x52/x57; meaningful but untracked). Globals: selected_nodes (Pro7 loopheaders), predecessor_bitset_node_count (nloopheaders), data_00582c64 (loopstack), data_005871a4 (loopdetection_nblocks, defined in CodeMotion.c), SelectedNode (BlockList). code_00522b40_inline1 is named after a 1.x address.

Data: Pro7's statics dominators/loopheaders/nloopheaders/loopstack are globals here. .data (assert name) and .bss placed.

Structure: Should be deferred: the binary has Pro7's order reversed (analyzeloopsinflowgraph lowest, computedominators highest). The hand-unrolled loops in insertpreheaderblocks (LoopDetection.c:601) and analyzeloops are what deferred self-inlining produces from Pro7's plain recursion.

Hacks:
- LoopDetection.c:49-53 CE_ASSERT macro
- LoopDetection.c:601-660 insertpreheaderblocks hand-unrolled 7 levels with `register` locals
- LoopDetection.c:591 code_00522b40_inline1 (1.x address name)
- LoopDetection.c:863-876 AddNode/AddCount 1.x statics
- LoopDetection.c:813 lalloc(0x58) literal size for Loop (use sizeof)

Next:
1. Rewrite in Pro7's order from Pro7's LoopDetection.c with -inline auto,deferred, keeping 2.6's extra Loop fields (0x55/0x56 stores in analyzeloop)
2. Rename statics to Pro7's (loopheaders, nloopheaders, loopstack, loopdetection_nblocks) and Loop's fields to Pro7's or documented names

### src/optimizer/StructMoves.c

C, effort M, confidence high; 6/12 exact, not linked. Pro7 StructMoves bodies with gaps and an invented helper.

6/12 exact. The bodies present are Pro7's with 2.6 additions (use_fp/soft_float 8-byte path), but 3 rows have no body (move_block_via_load_store_sequence, store_word_of_small_struct, store_small_block_from_reg_pair; Pro7 has all three). The step computation is an invented GetStep inline rather than Pro7's inline ternaries, which is the known blocker for move_block_via_inline_loop and load_small_block_into_reg_pair. Written in binary order, non-deferred.

Functions:
- `move_block_via_load_store_sequence` 0.0: unbuilt: no body (comment at StructMoves.c:127); Pro7 has it
- `store_word_of_small_struct` 0.0: unbuilt; only an extern at StructMoves.c:353
- `store_small_block_from_reg_pair` 0.0: unbuilt
- `move_block_via_inline_loop` 66.3: shape: GetStep stand-in for Pro7's step code; non-deferred
- `load_small_block_into_reg_pair` 71.0: shape/deferred inlining; compare with Pro7's

Names: Pro7 names. Operand kinds partly as raw numbers (make_addressable: optype 11U/9U).

Data: No data of its own beyond literals; raw_at 36 bytes.

Structure: Should be deferred: the binary has Pro7's order reversed (store_small_block_from_reg_pair lowest, make_addressable highest). Source is in binary order with forward prototypes (StructMoves.c:54-56).

Hacks:
- StructMoves.c:24 GetStep: invented helper (has a '2.6:' comment but is not the original's code)
- StructMoves.c:38/42 make_addressable compares optype with literals 11U/9U instead of OpndType_* names
- StructMoves.c:163 move_block_via_load_store: the else-branch body is not indented (pasted-in block)
- StructMoves.c:350-353 extern prototype of the unit's own store_word_of_small_struct in the .c

Next:
1. Lay the file out in Pro7's order with -inline auto,deferred and Pro7's static functions
2. Add Pro7's move_block_via_load_store_sequence, store_word_of_small_struct and store_small_block_from_reg_pair, plus 2.6's FP/soft-float branches from the disassembly
3. Replace GetStep with Pro7's inline step computation

### src/optimizer/UseDefChains.c

C, effort L, confidence high; 7/13 exact, not linked. 2.6 bodies (Pro7 + sparse bit-vectors) under placeholders; 1.x computeusedefchains.

7/13 exact. The static rows are Pro7's functions with 2.6's sparse kill/gen sets, decoded from the binary, but still named fn_XXXXXXXX (the comments give Pro7's names). computeusedefchains (42%) is the 1.x driver: it calls the 1.x use-def code in CodeMotion.c (assign_definition_and_use_starts, build_use_definition_entries, solve_definition_sets, ...) instead of this unit's own functions. canidate_reaches_use (Wii name, 2.6 addition) is unbuilt. This unit's header defines the 1.x use-def model every optimizer unit shares (see names).

Functions:
- `computeusedefchains` 42.0: 1.x driver calling CodeMotion.c's 1.x use-def code
- `canidate_reaches_use` 0.0: unbuilt 2.6 addition (leaf, 288 bytes); Alias.c calls it
- `fn_0056eb10` 50.7: computeusedeflists: 2.6 body, check against Pro7 + deferred
- `fn_0056e560` 65.4: computelocalusedefinfo: shape/deferred
- `fn_0056f470` 63.6: addobjectusedef: written `inline` with a literal 0x20 size

Names: 1.x use-def globals stand in for Pro7's UseDefChains.h: code_motion_entries=Defs, cm_entries=Uses, codeMotionEntryCount=number_of_Defs, data_00587e38=number_of_Uses, data_00587fe4=usedefinfo, code_motion_register_definition_heads/data_00587f04/register_definition_heads=reg_Defs[GPR/FPR/VR], code_motion_register_use_heads/codeMotionUseEntryHeads/register_use_entry_heads=reg_Uses[GPR/FPR/VR], gCodeMotionObjectTree_005880ac=objectusedefs, gCodeMotionAllocationList_005870fc=objectusedeflist. Rows still placeholders: fn_0056e460 (2.6 sparse transfer helper), fn_0056e560 computelocalusedefinfo, fn_0056eb10 computeusedeflists, fn_0056f000 precomputeusedefcounts, fn_0056f220 precomputememoryusedefs, fn_0056f2f0 addaliasusedef, fn_0056f470 addobjectusedef. CMObjectUseDef/CMUseDefInfo/CodeMotionDenseRun/CodeMotionSparseSet are invented type names.

Data: usedefinfo (data_00587fe4) is declared twice with two types: CodeMotion.h's CodeMotionDataflowState and UseDefChains.h's CMUseDefInfo through the CM_USEDEFINFO cast. Pro7's 9 globals all exist under 1.x names; most are defined in CodeMotion.c and GlobalOptimizer.c, not here. Unit .data/.bss not placed.

Structure: Should be deferred: the binary has Pro7's order reversed (computeusedefchains ... findobjectusedef highest). canidate_reaches_use and precedes (0x56e010, 0x56e130) sit below computeusedefchains, so they are defined after it at the end of 2.6's file.

Hacks:
- UseDefChains.h:35 `#define CM_USEDEFINFO ((CMUseDefInfo *)data_00587fe4)` type-punned view
- UseDefChains.h:41-42 CM_REG_USES2/CM_REG_DEFS2 `((&x) - N)` fake arrays (undefined behaviour; AGENTS.md lists such stand-ins as hacks)
- UseDefChains.h:59 cm_findobjectusedef: a private inline copy of findobjectusedef over a second struct type
- UseDefChains.c:127 `#pragma push` + inline_depth(2) never popped
- UseDefChains.c:409/425 `#pragma auto_inline off/reset` around findobjectusedef
- UseDefChains.c:48 precedes(void **, void *) walking `*p` as nextPCode
- UseDefChains.c:118 oalloc(0x20)/memclrw(list, 0x20) literal sizes

Next:
1. Convert UseDefChains.h to Pro7's (UseOrDef, RegUseOrDef, ObjectUseDef with 2.6's counts, UseDefInfo with 2.6's sparse sets; Defs/Uses/reg_Defs[RegClassMax]/reg_Uses[RegClassMax]/usedefinfo/objectusedefs/objectusedeflist), bind the arrays in every version (AGENTS.md: an array element and a scalar compile differently), and migrate every optimizer unit at once
2. Write computeusedefchains from Pro7 + disassembly, and canidate_reaches_use from the disassembly
3. Rename the fn_ rows to the Pro7 names in the comments; deferred build in Pro7 order
4. Delete CodeMotion.c's 1.x use-def block

### src/optimizer/VectorArraysToRegs.c

C, effort M, confidence high; 8/9 exact, not linked, deferred. mixed: some Pro7 bodies, some IDA-pseudocode stand-ins.

3/9 exact (vectorarraystoregs, convert_arrays_to_registers, computeglobalvectorpropinfo). The rest are IDA-pseudocode in shape: fn_*_inline1 helpers with `(int)` pointer compares, do-while loops, ClearRec poking byte 8, `flags = 2051`, a hard-coded CError_Internal("VectorArraysToRegs.c", 661), and the 1.x PCOp_VR kind. checkvectorstoreorload (38.5%), computeaddilist (54%) and scanforlocalvectorarrays (58%) need Pro7's bodies.

Functions:
- `checkvectorstoreorload` 38.5: pseudocode stand-in; Pro7 has it
- `computeaddilist` 54.4: pseudocode with 1.x use-def accessors; Pro7 has it
- `scanforlocalvectorarrays` 58.1: pseudocode (ClearRec, casts); Pro7 has it
- `convert_array_to_register` 67.2: pseudocode (flags literal, PCOp_VR, explicit CError_Internal)
- `scaninstructions` 88.0: pseudocode shape

Names: Invented for Pro7's: load_index_count (number_of_ADDIs), load_index_entries (ADDIs), VectorArrayEntry (ADDI), AggregateRecord (LocalVectorArray), codeMotionBits/CodeMotionBits (vectorpropinfo/VectorPropInfo), load_index_entry_counts (naddsinblock), block_entry_start (firstaddinblock), data_00582c9c (converted_arrays). Inline helpers named after 1.x addresses (VectorArraysToRegs_0052d0a0_inline1, fn_0052d8d0_inline1, fn_0052d650_inline1).

Data: Pro7's 6 statics are globals (3 marked 'global while lifted'); converted_arrays is defined in LoadDeletion.c. The .data range 0x5d1b08-0x5d2c50 holds MachineSimulationAltiVec's (7450) tables from 0x5d1b20 on; the unit's own .data is only the assert file name (0x5d1b08-0x5d1b20). raw_at 372 bytes.

Structure: Should be deferred: the binary has Pro7's order reversed (vectorarraystoregs lowest, scanforlocalvectorarrays highest). The source has Pro7's top half in mixed order.

Hacks:
- VectorArraysToRegs.c:206,278,290 fn_*_inline1 duplicates of lookup_vector_array_object
- VectorArraysToRegs.c:267-276 ClearRec writes `((char *)r)[8]` (a byte offset into the record)
- VectorArraysToRegs.c:245/253 `instruction->flags = 2051` literal
- VectorArraysToRegs.c:259 CError_Internal("VectorArraysToRegs.c", 661) instead of CError_FATAL
- VectorArraysToRegs.c:642 stray `extern unsigned char data_005ce9b2[]`

Next:
1. Rewrite from Pro7's VectorArraysToRegs.c with Pro7's types and statics, deferred
2. Take converted_arrays from LoadDeletion.c; fix the .data split so machine7450's tables go to MachineSimulationAltiVec.c

## B: near-misses and polish

### src/backend/BitVectors.c

B, effort S, confidence medium; 8/8 exact, linked. 2.6 bitvector functions, partly decompiler-shaped, binary order.

Links. Pro7's bitvector functions plus 2.6's range variants. It is not in Pro7's shape: bitvectorchanged reads like decompiler output (empty/equal/more temporaries), there is an invented BitRange26 type, and stale '(from CRTTI.c's file scope)' comments and empty #ifdef __MWERKS__ blocks remain. The names table says the original is deferred (reversed), and that the unit starts at 0x57d150 with bitvector_compress, bitvector_empty_compressed and init_compressed_bitvectors, which are filed under SpillCode's range.

Names: bitvectorcreate_range and bitvectorinitialize_range, with the BitRange26 type and BitRange_WordCount helper, look invented: check Wii's publics. bitrange_bytes_allocated is extern with only an address comment.

Data: No data of its own; bitrange_bytes_allocated raw.

Structure: Boundary: the three compressed-bitvector functions at 0x57d150 belong here (names table). Pro7 order and deferred to match the original's layout.

Hacks:
- src/backend/BitVectors.c:~95 bitvectorchanged in decompiler shape (unsigned temporaries 'empty', 'equal', 'more')
- src/backend/BitVectors.c:1-6 BitRange26 typedef under a '(from CRTTI.c's file scope)' comment
- src/backend/BitVectors.c:150-154 empty #ifdef __MWERKS__ blocks
- src/backend/BitVectors.c:58 extern SInt32 bitrange_bytes_allocated with an address comment

Next:
1. Move the 0x57d150 compressed-bitvector functions in (move_function.py)
2. Rewrite bitvectorchanged in Pro7's form and check it stays exact
3. Name the range type and functions from Wii, or TODO-mark them; switch to deferred in Pro7 order

### src/backend/CodeGen.c

B, effort M, confidence high; 37/43 exact, not linked, deferred. Pro7's CodeGen.c with 2.6 EABI additions read from the disassembly, deferred.

Pro7's file in Pro7's order, built -inline auto,deferred; 37/43 exact. The six inexact functions are shape problems: diff_function shows the same instructions with registers and stack slots moved, not missing code. The unit fails the relink because of them. It also still hosts about 25 COMMONs that belong to other units (TOC's toclist/codelabellist/exceptionlist, blank_name, gStackFrameSize ...), left behind by the 1.x code.

Functions:
- `CodeGen_CollapseVectorExpression` 51.1: shape: 450 '=' / 215 '~' lines in the diff, so the stack-slot layout and registers differ but the instructions are the same; not a wrong body (AGENTS agrees)
- `CodeGen_UpdateObject` 91.6: shape: one `or byte ptr [esi+0x14]` / extra `mov eax,eax`
- `CodeGen_ParsePragma` 93.0: shape (registers)
- `parse_section_pragma` 96.7: shape (registers)
- `move_assigned_argument` 99.6: one register
- `pragma_scheduling` 100.0: literal order only

Names: Rows have Pro7/Wii names. TODO-marked invented names: rt_ptr_glue, the __TARGET_CPU__/__PPC_EABI__ objects, and two helpers. Placeholders worth fixing: data_00588274, gUseVirtualRegisterNumbers_00587f00, gHasAltivecFrame, gVectorArrayConversion, data_005884ff (AGENTS: a Pro7 local in FunctionCalls), gStackFrameSize, plus calls to fn_005118f0 (Alias) and fn_0049f230 (a TOC.h macro alias of createIndirect, CodeGen.c:1756).

Data: Its own globals are real. Lines 166-193 define about 25 COMMONs of other units 'until those units are rewritten'. Among them, blank_name is really toc0.name (it is bound at 0x5ea526, 10 bytes into toc0 at 0x5ea51c; see TOC). 73 raw anonymous items (5.1 KB) sit in its .data range: the literals and jump tables of the inexact functions. They resolve when the unit links.

Structure: ok (deferred, Pro7's order)

Hacks:
- src/backend/CodeGen.c:104 orphan TODO comment ('made-up name: set_vr_reg_flags' result') with no declaration under it
- src/backend/CodeGen.c:166-193 other units' COMMONs defined here (toclist, codelabellist, exceptionlist, blank_name, gStackFrameSize, data_00588274, data_005884ff)
- src/backend/CodeGen.c:1756 fn_0049f230(...) is `#define fn_0049f230(o,a,b) ((Object *)createIndirect(o,a,b))` in include/compiler/TOC.h:57, a forbidden alias macro

Next:
1. Use fn_0049f230 -> createIndirect (with Pro7's Object * return) once TOC is rewritten
2. Move the foreign COMMONs to their units as TOC/Exceptions/FunctionCalls are rewritten; drop gStackFrameSize/data_00588274 if 2.6 has no such variable
3. Register near-misses with decl_order.py; CollapseVectorExpression's locals order/types

### src/backend/DumpIR.c

B, effort S, confidence high; 2/2 exact, linked. 2.6's empty DumpIR stubs.

Links; two empty functions. The file defines a COptimizer flag under a placeholder name as COMMON.

Names: data_00588513 (set by COptimizer at :328/:588) needs its real name; it is a tentative definition in the wrong unit. current_statement_number is fine.

Data: No data of its own.

Hacks:
- src/backend/DumpIR.c:43 unsigned char data_00588513: placeholder COMMON defined here but used by COptimizer
- 39 includes for two empty functions

Next:
1. Name data_00588513 (Wii data publics, hash-order check) and define it where 2.6 does
2. Trim includes

### src/backend/GCCInlineAsm.c

B, effort S, confidence high; 3/3 exact, linked. Pro7's GCCInlineAsm.c, binary order, non-deferred.

Links. Pro7's bodies, but the operand view is a private GCCAsmOperand struct under a local #pragma pack(2), not the header's IAOperand. The file is in binary order without deferred, though the names table says the original is reversed (deferred). The unk4/unk6 member names are Pro7's own.

Names: Pro7 names (InlineAsm_gcc_parse, gcc_replace_arg_st_asm, gcc_parse_name); unk6/unk4 are inherited from Pro7.

Data: .data placed.

Structure: Pro7 order with deferred (the names table marks it 'to change').

Hacks:
- src/backend/GCCInlineAsm.c:49-80 private typedef GCCAsmOperand under #pragma pack(push,2): a copy of IAOperand that belongs in InlineAsm.h
- 18 braceless ifs; 43 includes

Next:
1. Move the operand type to InlineAsm.h as Pro7's IAOperand
2. Switch to Pro7 order deferred; trim includes

### src/backend/InterferenceGraph.c

B, effort S, confidence medium; 3/6 exact, not linked, deferred. Pro7's InterferenceGraph.c, deferred.

3/6 exact. The inexact ones are register and type shapes, not wrong bodies: the original keeps register numbers as `short` (mov bp/di, word ptr; movsx), keeps the instr walker and `vec` in stack slots in eliminatedeadcode (frame 0x10 vs our 8), and buildinterferencematrix has an extra opcode range test (`sub eax, 0xaa`) that may be a 2.6 case.

Functions:
- `eliminatedeadcode` 52.5: shape: the original reads op->data.reg.reg into a short and spills instr/block; try short locals and declaration order
- `buildinterferencematrix` 73.0: mostly shape; check for a 2.6 opcode test (cmp to 0xaa after a jbe) Pro7 lacks
- `coalescenodes` 96.4: near-miss

Names: Pro7 names.

Data: Placed; register_class_format, _CALLER_SP_, first_fe_temporary_register etc. are other units' and raw.

Next:
1. Use `short reg` locals where the original does movsx from a word register
2. Read buildinterferencematrix's extra opcode compare in the disassembly

### src/backend/Intrinsics.c

B, effort S, confidence high; 29/30 exact, not linked, deferred. Pro7's Intrinsics.c in 2.6 shape, deferred, with 2.6's ids 311-314 and generated type tables.

A whole Pro7 rewrite, documented in AGENTS: 29/30 exact (metrics count 30 rows; AGENTS says 28/29). Only rlwimi_intrinsic (99.7%) differs, by one reload of `output`, and that blocks the link. Clean otherwise.

Functions:
- `rlwimi_intrinsic` 99.7: shape: 2.6 reloads output before output->reg = op1.reg; probably checkconstintarg's inlined checks differ

Names: Pro7 names throughout. Two TODO-marked invented names: va_info_intrinsic and its counts struct (5669, 5680).

Data: Generated tables match the original's .data byte for byte (AGENTS). Raw items are only the st* type objects owned elsewhere, plus one 20-byte item.

Structure: ok (deferred, Pro7's order)

Next:
1. Try reshaping checkconstintarg (the TODO in AGENTS) to recover rlwimi_intrinsic's reload; the unit then links

### src/backend/LiveVariables.c

B, effort S, confidence high; 3/4 exact, not linked, deferred. Pro7's LiveVariables.c with 2.6's global-register additions, deferred.

3/4 exact; computeglobalinout is 87.8% (a loop-shape/register difference: the original tests `test eax, eax` where ours compares registers, and the spill of a local differs). Clean, Pro7 names, CERROR_FILE gone.

Functions:
- `computeglobalinout` 87.8: shape (loop test form / local copy)

Names: Pro7 names.

Data: No own data; reg_info_list (Wii COMMON) and used_virtual_registers raw in the build.

Next:
1. Try Pro7's loop variants (a `changed` flag form, the local's type) on computeglobalinout

### src/backend/MachineSimulation7400.c

B, effort S, confidence high; 6/7 exact, not linked. Pro7's MachineSimulation7400.c with 2.6 tables.

6/7 exact; m7400_can_issue is 99.1% (one spill slot, [esp+8] for [esp+4]). Statics are kept global with #define'd unique names while can_issue is lifted, as AGENTS describes. Data is defined from source with no raw items.

Names: Pro7 names, #define'd to m7400_* (the documented pattern for model statics).

Data: Fully defined; placed. Statics stay global for the lifted can_issue.

Structure: ok (the binary is Pro7 reversed and matches as built)

Hacks:
- src/backend/MachineSimulation7400.c:656 bare `// TODO: stack swap issue` (say what is known: one spill slot)

Next:
1. Try decl_order/try_variants on can_issue's spill slot; once it is exact, make the statics static and the unit links

### src/backend/PCode.c

B, effort S, confidence high; 17/17 exact, linked. Pro7's PCode.c in binary order, built non-deferred.

Links. Pro7 bodies, but laid out in binary order without deferred. The original was deferred: the names table says reversed. To compensate, emitpcode calls a private copy of appendpcode (append_instruction) instead of inlining the real one. The file also has a dead private enum of opcode and operand constants and a hand-rolled va_start.

Names: Pro7 names. pccomputepredecessors and pclabel are 'plausible' (Wii names them new_basic_block and pclabelblock by order: check).

Data: .bss placed (depthfirstorder static added per AGENTS).

Structure: Should be Pro7 order with -inline auto,deferred; then the append_instruction copy can go.

Hacks:
- src/backend/PCode.c:102 static inline append_instruction: private copy of appendpcode
- src/backend/PCode.c:~396 va_list computed by hand ((char *)&opcode + ...) instead of va_start; also :408 (va_list)&op
- src/backend/PCode.c:~11-40 unused enum of OperandKind_/PCode_ opcode constants (stand-ins for PCodeInfo.h)
- src/backend/PCode.c:342 comma-expression TODO (documented)
- 58 includes; 'from TOC.c's file scope' comments

Next:
1. Rewrite in Pro7 order deferred and drop append_instruction
2. Use va_start
3. Delete the dead enum; trim includes

### src/backend/Peephole.c

B, effort S, confidence medium; 44/49 exact, not linked, deferred. Pro7's Peephole.c in 2.6 shape, deferred, with 2.6 additions.

42/49 exact, deferred, Pro7 names. Three helpers are 2.6-specific with invented names, each marked TODO. What is left is register and frame near-misses; adjustforward (61.8%, 3.8 KB) differs mainly in register choice and a rotate-mask computation (54 added and 57 removed lines out of ~1000), and MR_CMPI's frame is 0x20 where ours is 8 (locals or an inlined helper missing).

Functions:
- `adjustforward` 61.8: shape: register and short/int types in the mask computation; not a wrong body
- `MR_CMPI` 77.9: shape: the original's frame is 0x20 vs ours 8, so locals or an inlined helper are missing

Names: Pro7 names. Made-up names with TODOs: usedordefinedbetween, clear_base_register, LI_L_S.

Data: .bss placed; reg_info_list (Wii COMMON) still raw.

Structure: ok (deferred, Pro7 order)

Hacks:
- the tu_metrics unk_member count (28) did not reproduce on grep: no unkNN members; the three invented helpers carry TODOs

Next:
1. Look at MR_CMPI's missing frame locals
2. decl_order/types on adjustforward, ADDI_L_S and L_S

### src/backend/RegisterInfo.c

B, effort S, confidence high; 23/23 exact, linked, deferred. Pro7's RegisterInfo.c with 2.6's global register variables, deferred.

Links. Pro7 bodies, but data names and copts access are still stand-ins: two copts offset macros, statics left as data_ names although the comment names them, and an unnamed per-class count array. 55 includes.

Names: Statics data_005dca90 and data_005dca70 are Pro7's used_regs_before_coloring and save_state (the comment says so) but are not renamed. data_005e6cc0[5] is the per-class count of registers available for global register variables (29 - first nonvolatile): name it (Wii data publics). data_005e713d and data_005e70f5 are copts members.

Data: Placed. 22 raw named, mostly other units' globals; data_005e6cc0 is bound under its placeholder.

Structure: ok (deferred, Pro7 order)

Hacks:
- src/backend/RegisterInfo.c:67-68 #define data_005e70f5/data_005e713d as (*(UInt8 *)((char *)&copts + 0x13/0x5c)): copts offset macros (data_005e70f5 unused)
- src/backend/RegisterInfo.c:80-81 statics with placeholder names
- src/backend/RegisterInfo.c:63 leftover 'Globals (generated from the original's .bss)' banner and the '(from Intrinsics.c's file scope)' comment
- 55 includes

Next:
1. Rename the statics to used_regs_before_coloring/save_state
2. Replace the copts offset macros with COpts members
3. Name data_005e6cc0
4. tools/includes.py trim

### src/backend/Registers.c

B, effort S, confidence high; 8/8 exact, linked. Pro7's Registers.c in binary order, built non-deferred.

Links. Pro7 bodies plus 2.6's error-219 check. The header comment says the unit is built -inline auto,deferred in reverse order, but config/sources.json builds it with plain -inline auto and the source is in binary order. Either the comment or the layout is wrong; the names table says the original is reversed (deferred).

Names: Pro7 names, confirmed.

Data: No .data or .bss of its own (other units' globals are raw).

Structure: Should be Pro7 order with deferred, as the names table says; the comment is stale.

Hacks:
- src/backend/Registers.c:1-7 stale 'built -inline auto,deferred' comment
- '(from RegisterInfo.c's file scope)' leftover comments; 7 braceless bodies; 43 includes

Next:
1. Rewrite in Pro7 order deferred (it should still link) or fix the comment
2. Add braces; trim includes

### src/backend/SpillCode.c

B, effort S, confidence high; 3/11 exact, not linked, deferred. Pro7's SpillCode.c with 2.6 paired-single spills, deferred.

3/8 of its own functions exact; the inexact ones (77-89%) are register-allocation shapes (spillcall keeps the instr parameter in its stack slot and walks with ebp). Its range also holds three no-source BitVectors functions (bitvector_compress, bitvector_empty_compressed, init_compressed_bitvectors at 0x57d150-0x57d350, Wii's BitVectors module), which keep it from linking.

Functions:
- `spillcall` 77.3: shape (parameter reloaded from the stack; inc order)
- `insertspillcode` 79.1: shape
- `insert_store_spilled_register` 83.5: shape / 2.6 paired-single branch
- `spillinstruction` 88.6: shape
- `insert_load_spilled_register` 89.1: shape / 2.6 paired-single branch

Names: Pro7 names; one invented static marked TODO (line 23).

Data: Placed; stvec2x32float etc. raw from other units.

Structure: Boundary: 0x57d150-0x57d350 (3 rows) is BitVectors.c's (docs/names-2.6.md says so); BitVectors' range should start at 0x57d150 and those functions need source there (UseDefChains.c calls bitvector_compress).

Next:
1. Write the three bitvector_* functions in BitVectors.c (deferred order) and move the unit boundary
2. decl_order/try_variants on spillcall and the insert_* functions

### src/backend/StackFrame.c

B, effort M, confidence high; 26/41 exact, not linked, deferred. Pro7's StackFrame.c with 2.6's EABI frame and #pragma interrupt code, deferred.

Pro7's file and order, built deferred; 26/41 exact. The 15 inexact functions are 75-97% similar and the diffs are register and spill-slot swaps (can_add_displ_to_local: one parameter loaded late), not missing code. Problems remain outside the source: StackFrame.h still declares 1.x/invented symbols that nothing defines, and those are called from other units.

Functions:
- `compress_data_area` 75.4: shape (101 '=' / 29 '~')
- `can_add_displ_to_local` 79.5: shape: the original loads displ late into esi
- `assign_local_memory` 80.0: shape (a few +/- lines)
- `compute_interrupt_frame` 80.6: shape; 2.6-only code
- `save/restore_nonvolatile_GPRs/FPRs` 85.4: shape / inlining (use_helper_function)

Names: Pro7 names. TODO-marked invented names for 2.6's interrupt code (frame_sizes_computed, the interrupt save-area statics, three helpers).

Data: .bss is Pro7's statics with 2.6's interrupt statics (AGENTS). Raw items are other units' (reg_state, coloring, ...) plus 8 small anonymous items.

Structure: ok (deferred, Pro7's order)

Hacks:
- include/compiler/StackFrame.h:113-120 eight StackFrameEABI_* prototypes: six are referenced nowhere, and StackFrameEABI_GetTypeAlignment is called by BE_symbol.c:706 and FunctionCalls.c:476 but defined nowhere
- include/compiler/StackFrame.h:121-125 outgoing_argument_size, gStackFrameSize, data_005884ff, fn_004a9f70/fn_004a9f90 (declared, never defined: 1.x names for Pro7's parametersbase/localsbase, still called by Exceptions.c, PCodeAssembly.c, ObjGen_PPC_EABI.c)
- src/backend/StackFrame.c:1444 use_helper_function needs #pragma dont_inline (TODO)

Next:
1. Strip StackFrame.h of the StackFrameEABI_*/fn_004a9f70/fn_004a9f90/gStackFrameSize declarations as their callers are rewritten (callers should use local_offset_32/16 and CMach_*Alignment)
2. Register near-misses with decl_order.py, starting with the small ones (can_add_displ_to_local)

### src/backend/Switch.c

B, effort S, confidence high; 9/9 exact, linked, deferred. Pro7's Switch.c, deferred.

Links, deferred in Pro7 order. The file statics keep placeholder and invented names, though the comment lists Pro7's names for each. Small private helpers stand in for header ones. One clang error.

Names: Statics data_00581150 (caselabels), case_ranges (caseranges), switch_case_count (ncases), case_range_count (nranges_minus1), data_00581160 (min), data_00581168 (max), switchtable_base (first), data_00581178 (selector_gpr), switchRegHi (selector_gprHi), switch_expr_type (selector_type), default_case_label (defaultlabel), switchtable_max (range): rename all to Pro7's. treecompare's and I8_treecompare's parameters differ from Pro7's (int/unsigned vs SInt32).

Data: Placed. Statics are static.

Hacks:
- src/backend/Switch.c:44-55 statics with data_ names
- src/backend/Switch.c:61 SetSignedCaseValue: a private helper (Pro7's CInt64_SetLong)
- src/backend/Switch.c:165 NextGPR32: private copy of Pro7's ALLOC_GPR
- 1 clang error (not inspected: clang_check not run in a read-only audit); 15 braceless ifs

Next:
1. Rename the statics to Pro7's
2. Use CInt64_SetLong and ALLOC_GPR from headers
3. Fix the clang error

### src/driver/CCompiler.c

B, effort S, confidence high; 4/4 exact, linked. Pro7 CCompiler.c with 2.6 licence/EABI changes.

Links. Leftovers: `data_00588258` is Pro7's `license_cookie`, `data_005e73cf` is Pro7's `crippled`, `struct DriverSettings pFrontEnd` is a byte-array stand-in for the front-end panel, and `cc_tolower` is a private inline copy of 1.3's tolower.

Names: data_00588258 -> license_cookie, data_005e73cf -> crippled (Pro7 CCompiler.c globals)

Hacks:
- src/driver/CCompiler.c:24-25: data_00588258 / extern data_005e73cf placeholders
- src/driver/CCompiler.c:28: private cc_tolower (1.3 inline) copy
- include/driver/CCompiler.h:26: struct DriverSettings {b[9]; name[32]; options[15]} stand-in

Next:
1. rename.py --data data_00588258 license_cookie, data_005e73cf crippled (check COMMON hash order; crippled must be defined here as Pro7 does)
2. Replace DriverSettings with the pref_structs.h front-end panel type
3. Use ctype's tolower if it compiles the same

### src/driver/CLDependencies.c

B, effort S, confidence medium; 20/20 exact, linked. Pro7 CLDependencies.c.

Links; Pro7's CLDependencies.c (the int/SInt32 parameter fixes documented in AGENTS.md). One DropinFileRecord and one CLTarget mention.

Names: DropinFileRecord -> File, CLTarget -> Target

### src/driver/CLLoadAndCache.c

B, effort S, confidence medium; 3/3 exact, linked. Pro7 CLLoadAndCache.c.

Links from source; Pro7's file and names. Uses the repo's StorageHandle stand-in for Pro7's Handle (6x); otherwise clean.

Names: Pro7's names; StorageHandle -> Handle pending (repo-wide)

Next:
1. Follow the repo-wide StorageHandle -> Handle rename

### src/driver/CLMain.c

B, effort S, confidence high; 28/28 exact, linked. Pro7 CLMain.c with 2.6 changes.

Links. Pro7's file; TODOs document the file-scope statics and an empty `(void) 0` test. The volatile on Main_Driver's setjmp result is legitimate. Uses the repo's StorageHandle stand-in for Pro7's Handle (1x); otherwise clean.

Names: Pro7's names; StorageHandle -> Handle pending (repo-wide)

Next:
1. Follow the repo-wide StorageHandle -> Handle rename

### src/driver/CLPlugins.c

B, effort S, confidence high; 40/40 exact, linked. Pro7 CLPlugins.c.

Links; Pro7's file with Wii-confirmed names. One TODO documents 2.6's static order. Uses the repo's StorageHandle stand-in for Pro7's Handle (1x); otherwise clean.

Names: Pro7's names; StorageHandle -> Handle pending (repo-wide)

Next:
1. Follow the repo-wide StorageHandle -> Handle rename

### src/driver/CLPrefs.c

B, effort S, confidence medium; 5/5 exact, linked. Pro7 CLPrefs.c.

Links from source; Pro7's file and names. Uses the repo's StorageHandle stand-in for Pro7's Handle (5x); otherwise clean.

Names: Pro7's names; StorageHandle -> Handle pending (repo-wide)

Next:
1. Follow the repo-wide StorageHandle -> Handle rename

### src/driver/CLTarg.c

B, effort S, confidence medium; 4/4 exact, linked. Pro7 CLTarg.c.

Links. Pro7's CLTarg.c bodies, but the target type is the repo's `CLTarget` where Pro7 calls it `Target` (gTarg). CLIO, CLFileOps, CLParserCallbacks and both _V10 units use it too.

Names: CLTarget -> Pro7's Target

Next:
1. Rename struct CLTarget to Target (cmdline.h/CLTarg.h), checking member names against Pro7's

### src/driver/CLToolExec.c

B, effort S, confidence medium; 7/7 exact, linked. Pro7 CLToolExec.c.

Links; Pro7's CLToolExec.c bodies, with DropinFileRecord for File in four signatures.

Names: DropinFileRecord -> File

### src/driver/CLWriteObjectFile.c

B, effort S, confidence medium; 2/2 exact, linked. Pro7 CLWriteObjectFile.c.

Links; Pro7's bodies, but its API takes DropinFileRecord (Pro7's File).

Names: DropinFileRecord -> File

### src/driver/COSToolsCLT.c

B, effort S, confidence medium; 24/24 exact, linked. Pro7 COSToolsCLT.c.

Links from source; Pro7's file and names. Uses the repo's StorageHandle stand-in for Pro7's Handle (3x); otherwise clean.

Names: Pro7's names; StorageHandle -> Handle pending (repo-wide)

Next:
1. Follow the repo-wide StorageHandle -> Handle rename

### src/driver/CWParserPluginsPrivate.cpp

B, effort S, confidence medium; 16/16 exact, linked. Pro7 CWParserPluginsPrivate.cpp.

Links (-inline off, one unit). Function names are already Pro7's, so the header TODO asking for them is stale. Parameters are still `void *`/int stand-ins for the SDK's CWPluginContext/CWCommandLineArgs etc.

Names: Pro7's; StorageHandle stands for Pro7's Handle (repo-wide)

Hacks:
- src/driver/CWParserPluginsPrivate.cpp:5: stale TODO about function names
- void* context parameters and local fnptr typedefs instead of SDK types

Next:
1. Drop the stale TODO; give parameters the SDK's types from Pro7's CWParserPluginsPrivate.cpp

### src/driver/CWPluginsPrivate.cpp

B, effort S, confidence medium; 29/29 exact, linked. Pro7 CWPluginsPrivate.cpp.

Links; Pro7's CWPluginsPrivate.cpp. It uses the stand-ins ExportedRecord (CWProjectFileInfo) and FileOpenOptions (CWNewProjectEntryInfo).

Names: ExportedRecord -> CWProjectFileInfo, FileOpenOptions -> CWNewProjectEntryInfo

### src/driver/CWSecretPluginCallbacks.c

B, effort S, confidence medium; 4/4 exact, linked. Pro7 CWSecretPluginCallbacks.cpp.

Links; Pro7's CWSecretPluginCallbacks.cpp built as .c. ValidateContext carries a unit prefix as a static. Uses the repo's StorageHandle stand-in for Pro7's Handle (2x); otherwise clean.

Names: Pro7's names; StorageHandle -> Handle pending (repo-wide)

Next:
1. Follow the repo-wide StorageHandle -> Handle rename

### src/driver/ClientGlue.c

B, effort S, confidence medium; 10/10 exact, linked. Pro7 ClientGlue.c.

Links; Pro7's file with Pro7 names (StorageHandle in one signature). Uses the repo's StorageHandle stand-in for Pro7's Handle (1x); otherwise clean.

Names: Pro7's names; StorageHandle -> Handle pending (repo-wide)

Next:
1. Follow the repo-wide StorageHandle -> Handle rename

### src/driver/CodeGenOptPPC.c

B, effort S, confidence medium; 4/4 exact, linked. Pro7 CodeGenOptPPC.c.

Links. Pro7's CodeGenOptPPC.c with the EABI setup. A few COpts members still have offset names (copts.f27, fd4, fd5, f25), and one panel field is pp.unknown34[3].

Names: copts.f25/f27/fd4/fd5 and PPC EABI Project panel unknown34: name them from the compileroptions pragma table

Hacks:
- src/driver/CodeGenOptPPC.c:444-450: copts.f27/fd5/fd4/f25 offset-named members

Next:
1. Name the four COpts members from 2.6's pragma table (OPT_OFFSET) as for the others

### src/driver/DropInCompilerLinkerPrivate.cpp

B, effort S, confidence medium; 18/18 exact, linked. Pro7 DropInCompilerLinkerPrivate.cpp.

Links; Pro7's file. A TODO refers to context fields at 0x152/0x156 (Pro7's sourcetext/sourcetextsize in CWCompilerLinkerContext), which the fake PluginC class hides.

Names: Pro7's; StorageHandle stands for Pro7's Handle (repo-wide)

Next:
1. Resolve with Pro7's CWCompilerLinkerContext (see CLPluginRequests)

### src/driver/Help.c

B, effort S, confidence medium; 18/18 exact, linked. Pro7 Help.c.

Links; Pro7's Help.c. clang reports 1 error. Uses the repo's StorageHandle stand-in for Pro7's Handle (1x); otherwise clean.

Names: Pro7's names; StorageHandle -> Handle pending (repo-wide)

Hacks:
- 1 clang error

Next:
1. Follow the repo-wide StorageHandle -> Handle rename

### src/driver/IO.c

B, effort S, confidence medium; 2/2 exact, linked. Pro7 IO.c.

Links from source; Pro7's file and names. Uses the repo's StorageHandle stand-in for Pro7's Handle (3x); otherwise clean.

Names: Pro7's names; StorageHandle -> Handle pending (repo-wide)

Next:
1. Follow the repo-wide StorageHandle -> Handle rename

### src/driver/Memory.c

B, effort S, confidence medium; 15/15 exact, linked. Pro7 MacEmul/Memory.c.

Links; Pro7's MacEmul/Memory.c. StorageHandle (19 uses) is the repo's name for Pro7's Handle/OSHandle-backed handle type. A repo-wide rename is pending (AGENTS.md: 'Pro7's Handle for the repo's StorageHandle *').

Names: Pro7's; StorageHandle stands for Pro7's Handle (repo-wide)

Next:
1. Rename StorageHandle to Pro7's Handle across the driver (MacEmul types)

### src/driver/OptimizerHelpers.c

B, effort S, confidence medium; 3/3 exact, linked. Pro7 OptimizerHelpers.c.

Links from source; Pro7's file and names. Uses the repo's StorageHandle stand-in for Pro7's Handle (2x); otherwise clean.

Names: Pro7's names; StorageHandle -> Handle pending (repo-wide)

Next:
1. Follow the repo-wide StorageHandle -> Handle rename

### src/driver/Parameter.c

B, effort S, confidence high; 55/55 exact, linked. Pro7 Parameter.c.

Links. It defines `CERROR_FILE "unknown.c"`, and a TODO says the original asserts with "Parameter.c". AGENTS.md lists this as still to read from the original.

Hacks:
- src/driver/Parameter.c:1: CERROR_FILE "unknown.c"
- src/driver/Parameter.c:24: TODO about the assert file name

Next:
1. Read the assert's file-name literal in the original's .data and drop CERROR_FILE

### src/driver/ParserFace.c

B, effort S, confidence high; 14/14 exact, linked. Pro7 ParserFace.c.

Links; Pro7's file (dead CWParser_GetTargetList kept for data). Uses the repo's StorageHandle stand-in for Pro7's Handle (4x); otherwise clean.

Names: Pro7's names; StorageHandle -> Handle pending (repo-wide)

Next:
1. Follow the repo-wide StorageHandle -> Handle rename

### src/driver/ParserGlue-eabi-ppc-cc.c

B, effort S, confidence high; 4/4 exact, linked. Pro7 ParserGlue with generated 2.6 option tables.

Links; the option tables are generated (.opt). Small leftovers: `Boolean data_0058851d` is Pro7's `setLinkerOutputFilename` (ToolHelpers' global), and the EABI panel structs keep unknownXX fields.

Names: data_0058851d -> setLinkerOutputFilename (Pro7 parser.h); pref_structs.h unknownXX fields

Hacks:
- src/driver/ParserGlue-eabi-ppc-cc.c:81: data_0058851d placeholder

Next:
1. Rename data_0058851d to setLinkerOutputFilename and declare it as Pro7 does (ToolHelpers.c, COMMON: check hash order)

### src/driver/ParserHelpers-cc.c

B, effort S, confidence medium; 5/5 exact, linked. Pro7 ParserHelpers-cc.c.

Links. Uses the repo's StorageHandle stand-in for Pro7's Handle (2x); otherwise clean.

Names: Pro7's names; StorageHandle -> Handle pending (repo-wide)

Next:
1. Follow the repo-wide StorageHandle -> Handle rename

### src/driver/ParserHelpers.c

B, effort S, confidence high; 12/12 exact, linked. Pro7 ParserHelpers.c.

Links. It reads `data_0054a0b8`, which is Pro7's `lastStage` (ToolHelpers' static/global, .bss 0x5eb1d8). It keeps a TODO'd `volatile mode` workaround in AddAccessPathList.

Names: data_0054a0b8 -> lastStage (Pro7 ToolHelpers.c)

Hacks:
- src/driver/ParserHelpers.c:145-148: volatile int mode + copy through t (TODO, accepted)
- src/driver/ParserHelpers.c:230,251: data_0054a0b8

Next:
1. Rename data_0054a0b8 to lastStage everywhere (ParserHelpers.h, ToolHelpers*.c)

### src/driver/Projects.c

B, effort S, confidence medium; 7/7 exact, linked. Pro7 Projects.c.

Links; Pro7's Projects.c (dead overlay/segment functions kept for data). It uses ExportedRecord/FileOpenOptions stand-ins for the SDK's CWProjectFileInfo/CWNewProjectEntryInfo.

Names: ExportedRecord, FileOpenOptions -> SDK types

### src/driver/Resources.c

B, effort S, confidence high; 29/29 exact, linked. Pro7 MacEmul/Resources.c in 2.6 shape.

28/29 exact. ReadResourceFork is 97.2%: only two spill slots ([esp] vs [esp+4]) swapped. The gotos are Pro7's. Data and .bss are placed; the dead OS_MacDumpResourceFork is accounted for.

Functions:
- `ReadResourceFork` 97.2: shape: two stack slots swapped (local declaration order/types)

Names: Pro7's; StorageHandle for Handle

Structure: ok, binary order

Next:
1. decl_order.py on ReadResourceFork (swap locals sharing the spill area: data_offs vs ref_offs/offs)

### src/driver/StringUtils.c

B, effort S, confidence high; 8/8 exact, linked. Pro7 OSLib/StringUtils.c.

Links; Pro7's file. Uses the repo's StorageHandle stand-in for Pro7's Handle (1x); otherwise clean.

Names: Pro7's names; StorageHandle -> Handle pending (repo-wide)

Next:
1. Follow the repo-wide StorageHandle -> Handle rename

### src/driver/Targets.c

B, effort S, confidence high; 4/4 exact, linked. Pro7 Targets.c (ParserTool functions).

Links. Two smells: a private `va_start` macro computing `(char *)&parm + ...` (AGENTS.md says to use stdarg's va_start), and `#define CERROR_FILE "options.c"`, a mis-split marker listed in AGENTS.md as still to resolve.

Hacks:
- src/driver/Targets.c:28: private va_start macro
- src/driver/Targets.c:64-65: CERROR_FILE "options.c" (asserts belong to Pro7's options.c?)

Next:
1. Use <stdarg.h> va_start
2. Find where the 'options.c' asserting code belongs (Pro7 options.c, maybe a separate unit in this range) and move it

### src/driver/TextUtils.c

B, effort S, confidence medium; 4/4 exact, linked. Pro7 MacEmul/TextUtils.c.

Links from source; Pro7's file and names. Uses the repo's StorageHandle stand-in for Pro7's Handle (2x); otherwise clean.

Names: Pro7's names; StorageHandle -> Handle pending (repo-wide)

Next:
1. Follow the repo-wide StorageHandle -> Handle rename

### src/driver/ToolHelpers-cc.c

B, effort S, confidence high; 2/2 exact, linked. Pro7 ToolHelpers-cc.c.

Links; it uses the placeholders data_00588530 (outputOrdering) and data_0054a0b8 (lastStage).

Names: data_00588530 -> outputOrdering, data_0054a0b8 -> lastStage

Next:
1. Follow the ToolHelpers.c renames

### src/driver/Utils.c

B, effort S, confidence high; 6/7 exact, not linked. Pro7 Utils.c.

6/7 exact. Utils_SpellList is 99.2%, the only difference being the register for one constant (`x = 1` in eax vs ecx). To steer registers the source adds dead locals (x, y, c, d) and an invented `static inline resetflags` that assigns two dead ints, with no TODO.

Functions:
- `Utils_SpellList` 99.2: register-only near-miss; dead-local scaffolding

Names: Pro7's

Hacks:
- src/driver/Utils.c:37-41: invented resetflags() dead-store helper, no TODO
- src/driver/Utils.c:52-58: unused locals x, y, c, d

Next:
1. Run decl_order.py/try_variants on Utils_SpellList from Pro7's plain body; if the scaffolding stays, add a TODO

### src/driver/WarningHelpers.c

B, effort S, confidence medium; 2/2 exact, linked. Pro7 WarningHelpers.c.

Links; Pro7's WarningHelpers.c. clang reports 1 error (fix rather than silence). Uses the repo's StorageHandle stand-in for Pro7's Handle (2x); otherwise clean.

Names: Pro7's names; StorageHandle -> Handle pending (repo-wide)

Hacks:
- 1 clang error

Next:
1. Follow the repo-wide StorageHandle -> Handle rename

### src/frontend/CABI.c

B, effort M, confidence high; 36/37 exact, not linked, deferred. Pro7 CABI.c in 2.6 shape, deferred.

35/37 exact, with Pro7's order and names, and clean. CABI_CopyConAssignCB (93.5%, 4.5 KB) and CABI_TransDestructor (96%) are register and spill near-misses. Because CopyConAssignCB is so large, the unit's byte share is only 69.8%.

Functions:
- `CABI_CopyConAssignCB` 93.5: shape: a spill slot ([esp+0x3c]) and a few reloads differ; registers
- `CABI_TransDestructor` 96.0: registers

Names: Pro7's.

Data: Statics are Pro7's at pack(2). raw_at is 204 bytes.

Next:
1. Hoist block locals and run decl_order --swaps on CABI_CopyConAssignCB (it is most of the unit's bytes)
2. Then relink_units --only

### src/frontend/CBrowse.c

B, effort S, confidence high; 21/21 exact, linked, deferred. Pro7 CBrowse.c, deferred.

Links from source (21/21) in Pro7's order. Its two main statics are still placeholder names with Pro7 equivalents; otherwise clean.

Names: data_005884f5 is Pro7's `gUseNameTable` (COMMON, also in CBrowse.h:34). data_00581ba8 is Pro7's `static GList gBrowseData`. Both are used about 30 times in src/frontend/CBrowse.c.

Data: gBrowseData is already static; only its name is a placeholder. data_005884f5 is a bound COMMON.

Hacks:
- src/frontend/CBrowse.c:11-12 placeholder names data_005884f5/data_00581ba8

Next:
1. rename.py --data data_005884f5 gUseNameTable; rename data_00581ba8 to gBrowseData (bindings too), then relink check

### src/frontend/CClass.c

B, effort S, confidence high; 67/67 exact, linked, deferred. Pro7 CClass.c in 2.6 shape, deferred.

66/67 exact, with Pro7's order and names. CClass_OptimizeBitFieldAccess (83.5%) differs only in register and stack-slot allocation: one local lives in [esp] where ours uses [esp+4], plus an edi/edx swap. Two dead stand-in functions for 1.x CExpr code remain, though CExpr is now Pro7's.

Functions:
- `CClass_OptimizeBitFieldAccess` 83.5: shape: stack-slot and register allocation (ours puts a local at [esp+4], the original at [esp]); try locals' order and types

Names: Pro7's. data_00587140 (line 2075) is Pro7's `cobjc_currentclass` (defined in CObjC.c:1172).

Data: Statics are Pro7's at pack(2). One anonymous raw_at run of 536 bytes in .data.

Hacks:
- src/frontend/CClass.c:2499-2506 dead CClass_ResetBasePathDepth/CClass_GetBasePathLevel stand-ins (no callers outside CClass.h:75-76): they add .text the original lacks
- src/frontend/CClass.c:2075 data_00587140 placeholder for cobjc_currentclass

Next:
1. Delete CClass_ResetBasePathDepth/GetBasePathLevel and their CClass.h prototypes
2. Rename data_00587140 -> cobjc_currentclass (with CObjC)
3. decl_order / try_variants on CClass_OptimizeBitFieldAccess, then relink_units --only

### src/frontend/CExpr.c

B, effort S, confidence high; 87/90 exact, not linked, deferred. Pro7 CExpr.c in 2.6 shape, deferred.

87/90 exact, with Pro7's order and names. The three inexact functions (98-99.9%) are register-only. Other units' COMMONs are defined here (documented), and the shared placeholders data_0058850e, data_005e7196 and data_0055d5c0 remain.

Functions:
- `CExpr_ParseNameResultExpr` 98.2: registers
- `do_typecast` 99.5: registers (esi/edi)
- `pointercompare` 99.9: one store ordered differently

Names: Pro7's. data_0058850e (defined at line 72) is Pro7's temp_reference_init. data_0055d5c0 is sttemplexpr. data_005e7196 is copts.old_argmatch.

Data: Lines 61-72 define Pro7 CParser's COMMONs (rt_ptmf_*, converted_expr, tksize, ispascalstring, ptmstruct, non_type_template_argument_mode) under a TODO. Moving them is layout-neutral for COMMON, but check the names against hash order. raw_at is 2312 bytes in 5 runs.

Hacks:
- src/frontend/CExpr.c:61-72 other units' COMMONs defined here (documented)
- src/frontend/CExpr.c:74 local prototype for CMach_RoundedSizeOf because CMachine.c has a stand-in signature (documented)
- src/frontend/CExpr.c:129-157 `#pragma dont_inline` block: check it has a reason

Next:
1. Register fixes on the three near-misses
2. Rename data_0058850e/data_0055d5c0 (shared with CExpr2)
3. Move the COMMONs to CParser.c once that is Pro7's; fix CMach_RoundedSizeOf's signature in CMachine

### src/frontend/CExpr2.c

B, effort M, confidence high; 81/85 exact, not linked, deferred. Pro7 CExpr2.c in 2.6 shape, deferred.

81/85 exact, with every Pro7 function present in Pro7's order. The four inexact functions differ only in registers or stores; none needs a new body. What is left: Pro7's statics are still global under 1.x names because those lifted functions use them, plus several COMMON placeholders whose Pro7 names are known.

Functions:
- `CExpr_ConstructObject` 84.8: registers only (51 ~ lines, no +/-): shape (decl order, int vs SInt32)
- `scannew` 86.7: shape: the original has two extra `mov [ecx+0x12], eax` stores (a statement repeated in both branches?) plus registers
- `CExpr_MakeFunctionCall` 89.7: registers only
- `user_assign_check` 90.5: registers only

Names: Function names are Pro7's. The placeholders all have known Pro7 names: data_0058850e -> temp_reference_init (defined in CExpr.c), data_0058852b -> illegalimplicitconversion, data_0058757c -> cinit_tempnodefunc (declared in CFunc.h), DAT_00587fd8 -> name_obj_check, data_0055d5c0 -> sttemplexpr, data_00580748/50/54 -> cexpr_esearch_callback / cexpr_left_conversion_type / cexpr_right_conversion_type, and data_005e7196 -> copts.old_argmatch (a second binding inside copts). IS_FUNC_LIST and IS_TEMPL_FUNC_LIST (line 3647) are invented, with a TODO.

Data: Pro7's statics conversion_score, array_bound, bestComparison, expr_search_types and data_0058074x are global and bound while lifted code uses them (TODO at line 56). right_operand_arg and operator_operand_arg are real. raw_at is 1160 bytes in 5 anonymous runs.

Structure: ok (deferred, Pro7 order)

Hacks:
- src/frontend/CExpr2.c:56-69 Pro7 statics kept global under 1.x names (documented)
- src/frontend/CExpr2.c:1548 CExpr_MakeObjRefNode written out by hand (documented)
- src/frontend/CExpr2.c:1984,2585,2700,3484,4447 data_005e7196 for copts.old_argmatch: two bindings at one address

Next:
1. Rename the placeholders to their Pro7 names (temp_reference_init, illegalimplicitconversion, cinit_tempnodefunc, name_obj_check, sttemplexpr): one rename each across CExpr, CExpr2, CFunc, CInline and CTemplate*
2. Fix scannew first: the two missing stores look like a statement duplicated in 2.6's branches
3. decl_order or integer types for the other three; then make the statics static and use Pro7's names

### src/frontend/CExprConvMatch.c

B, effort S, confidence high; 26/27 exact, not linked, deferred. Pro7 CExprConvMatch.c in 2.6 shape, deferred.

25/27 exact, with Pro7's order and names. CExpr_CheckBinaryBuiltin (97.8%) and CExpr_FuncArgMatch (98.2%) differ only in registers. The `x11/x12/x13` members the metrics flag are Pro7's own names.

Functions:
- `CExpr_CheckBinaryBuiltin` 97.8: registers
- `CExpr_FuncArgMatch` 98.2: registers (ecx/edx)

Names: Pro7's. data_005e7196 (copts.old_argmatch) at line 1710.

Data: No statics of note. raw_at is 648 bytes in 5 runs (jump tables/literals not yet placed).

Hacks:
- src/frontend/CExprConvMatch.c:107 local prototype of CDecl_NewRefPointerType because CDecl.c defines it with a stand-in signature (documented)
- src/frontend/CExprConvMatch.c:1710 data_005e7196 binding inside copts

Next:
1. decl_order on the two near-misses
2. Fix CDecl_NewRefPointerType's signature in CDecl and drop the local prototype

### src/frontend/CMangler.c

B, effort S, confidence high; 18/18 exact, linked, deferred. Pro7 CMangler.c in 2.6 shape, deferred.

17/18 exact. The last function, CMangler_TemplateInstanceName, is 99.0% with one register swap (ebx for edi). Pro7's order and names. The 2.6 paired-single integral type at index 19 is unnamed (TODO).

Functions:
- `CMangler_TemplateInstanceName` 99.0: one register (test ebx vs edi): shape or declaration order

Names: Pro7's. The TODO at line 445 asks for names of 2.6's integral types after IT_LONGDOUBLE.

Data: .data placed. 1624 bytes in 3 runs are raw_at (probably the operator/type-code string tables): check that they come from compiled literals once the unit links.

Hacks:
- src/frontend/CMangler.c:445 unnamed 2.6 integral types (documented)

Next:
1. decl_order.py / try_variants on CMangler_TemplateInstanceName
2. Then relink_units --only CMangler.c and check the raw_at data

### src/frontend/CPrep.c

B, effort S, confidence high; 89/92 exact, not linked, deferred. Pro7's CPrep.c in 2.6 shape (deferred); about 54% of its non-trivial lines are Pro7's text and about 3% are gc1's.

This is a real Pro7 rewrite: 88 of 92 functions are exact, with Pro7's static names (kept global for lifted code, with the standard comment) and the 2.6 additions marked TODO. The four inexact functions differ only in registers or spill slots. The .data is incomplete: 2.6's CPrep .data holds the literals "compiler", "exestr", "linker" and "user" from Pro7's dead pragma_comment, and our source has no such function.

Functions:
- `CPrep_PragmaOnce` 79.6: shape: a local is spilled to [esp+4] where 2.6 keeps it in bl, plus register renumbering
- `pragma_on_off_reset` 91.9: shape: registers only
- `prepinclude` 96.6: shape: registers only
- `CPrep_GetQuotedFileName` 96.8: shape (2.6 addition; the TODO at :2782 says the Pascal-to-C helper's length is reloaded)

Names: Pro7 names throughout. Five invented 2.6 helpers, all marked TODO: CPrep_GetQuotedEnvVar, envM, cprep_extrafile, CPrep_GetQuotedFileName and the file_cannot_opened_name buffer. Placeholder: CPrep.c:1665 calls fn_004e67a0, which is Pro7's empty CleanupAssembler() (Pro7 CPrep.c:1543); the repo files that 1-byte function under CSOM.c at 0x525eb0. Rename it and give it its real unit.

Data: The statics are Pro7's, in Pro7's order, and kept global while lifted functions use them. compileroptions[] is 2.6's table. .data/.bss are placed. The referenced data still raw comes from other units (stsignedlonglong, cint64_zero/one, GListErrorProc).

Structure: Deferred, in Pro7's order apart from four functions out of order: cannotopenerror, CPrep_Reconfig, pragma_push, pragma_pop. Pro7 functions missing from the source: getfiledata, CPrep_StreamGetTemplate, CPrep_RemoveTokens, CPrep_ErrorMessage, CPrep_ErrorMessageVA, prepoffset, prepoffset2, pragma_comment and pragma_precompile_target. pragma_precompile_target is written out by hand inside preppragma at :3938. pragma_comment is dead in Pro7 too, but its literals are in 2.6's .data.

Hacks:
- CPrep.c:2504 poptokenseqInline: a private copy of poptokenseq (TODO)
- CPrep.c:2782 CPrep_PToCString: an inline helper (TODO)
- CPrep.c:3938 pragma_precompile_target written out by hand inside preppragma
- CPrep.c:1665 fn_004e67a0 (CleanupAssembler) placeholder
- CPrep.c:1283, :1468 TODOs about a signed/unsigned compare and UPDATE_STATEMENT_LINE's SInt32

Next:
1. Add Pro7's dead pragma_comment (and check the other missing Pro7 functions against .data literals) so the unit's .data matches
2. Put cannotopenerror, CPrep_Reconfig, pragma_push and pragma_pop in Pro7's order
3. Try Pro7's static pragma_precompile_target in place of the hand-written copy in preppragma
4. Rename fn_004e67a0 to CleanupAssembler and move it to its unit (Pro7's InlineAsm)
5. Run decl_order.py on CPrep_PragmaOnce, pragma_on_off_reset and prepinclude

### src/frontend/CPrepTokenizer.c

B, effort S, confidence high; 68/69 exact, not linked, deferred. Pro7's CPrepTokenizer.c (about 73% Pro7 text, no gc1-only lines), deferred.

Pro7's file: all 72 functions in Pro7's order. 67 of 69 rows are exact. tapos and tquot differ only in registers and in how a byte or short value is widened. Nothing else stands out.

Functions:
- `tapos` 85.9: shape: registers; 2.6 widens with 'and edi, 0xff' and 'and eax, 0xffff' where ours uses movzx, so look at the integer type of the character local (int vs short/char)
- `tquot` 97.7: shape: registers

Names: Pro7's names. One static (prepnextstringchar_foundnl) is kept global with the standard comment.

Data: .data/.bss placed. Raw referenced data belongs to other units (stchar, stfloat, cint64_zero, InlineAsm_labelref, was_escchar and CPrep_SkipNewCommentChar, which are defined elsewhere).

Structure: ok (deferred, Pro7 order, -sym off as configured)

Next:
1. Try the integer types of tapos's character/value locals (int vs UInt8/UInt16) as in AGENTS' int/long notes
2. Run decl_order.py on tapos and tquot

### src/frontend/CPreprocess.c

B, effort S, confidence high; 7/7 exact, linked, deferred. Pro7's CPreprocess.c (about 87% Pro7 text) plus two 2.6 helpers.

This is Pro7's file with 2.6's additions: 6 of 7 rows are exact, and the remaining one is at 98.6%. The two 2.6 helpers carry invented names marked TODO. The assert file name is now "CPreprocess.c", so AGENTS' list of "unknown.c" units is out of date for this unit.

Functions:
- `CPrep_PreprocessDumpFileInfo` 98.6: shape: one instruction (a 2.6 detail of the newline test or the filename copy)

Names: Pro7's, plus CPrep_PreprocessDumpDirective and CPrep_PreprocessDumpDirectiveEnd and the statics pp_text_start/pp_text_depth (invented, TODO).

Data: Two statics kept global for lifted code. 1.7 KB of anonymous raw data remains in the range (the keyword table and format strings), placed by make_splits.

Structure: ok (deferred, Pro7 order)

Hacks:
- CPreprocess.c:180, :195 invented names (TODO'd)
- CPreprocess.c local comments '// r16' and '// r15' (Pro7 register notes)

Next:
1. Fix CPrep_PreprocessDumpFileInfo's last instruction (compare the newline test with Pro7's)
2. Make pp_text_start/pp_text_depth static once it links
3. Update AGENTS' 'unknown.c' list

### src/frontend/CRTTI.c

B, effort S, confidence medium; 14/14 exact, linked, deferred. Pro7 CRTTI.c in 2.6 shape, deferred.

14/14 exact, in Pro7's order with Pro7's names. It does not link because of data mechanics. The split's .data starts at 0x5d7cfc marked align:4, a misplaced boundary: an object's .data starts 8-aligned. The link fails on an undefined lbl_005D7D1C inside that range and on SpillCode's .data size, and AGENTS notes that the compiled .data has one jump table more than the original's.

Names: Pro7's. The metrics' unk_member count is a false positive (`(SInt8)` switch casts).

Data: .data range 0x5d7cfc-0x5d7f88 align:4 (suspect start). raw_at is 696 bytes in 4 runs. stbool and stchar are raw named (CMachine's).

Structure: ok (deferred, Pro7 order); the .data boundary with the previous object (SpillCode?) is wrong

Next:
1. Find which switch in CRTTI (ParseTypeID's nested switches at lines 839-851, or the casts at 245/474/564/594) gives the extra jump table: 2.6 probably has an if-chain or fewer cases there
2. Fix the .data start (8-aligned) and SpillCode's .data extent with make_splits, then relink_units --only CRTTI.c

### src/frontend/CTemplateFunc.c

B, effort S, confidence high; 24/24 exact, linked, deferred. Pro7 CTemplateFunc.c bodies with invented template type names, deferred.

23/24 exact, with Pro7's order and function names. CTempl_GetCreateFuncInstance (99.8%) has one scheduling difference. What is left is the template types' invented names (CTStateElem, TemplateSpecializationData, TemplateContext) and the copts.old_argmatch double binding.

Functions:
- `CTempl_GetCreateFuncInstance` 99.8: one `mov esi, eax` scheduled earlier: an assignment order

Names: Function names are Pro7's. Types are invented (see CTemplateTools).

Data: data_005e7196 at line 1163 (copts.old_argmatch) is documented as needed for 2.0's COpts layout.

Hacks:
- src/frontend/CTemplateFunc.c:1163 data_005e7196 binding inside copts (documented)
- 1 clang error (tu_metrics)

Next:
1. Fix the near-miss (statement order)
2. Rename the template types with the other CTemplate units
3. Fix the clang error

### src/frontend/CTemplateTools.c

B, effort M, confidence high; 42/43 exact, not linked, deferred. Pro7 CTemplateTools.c bodies with gc1/invented template type names, deferred.

41/43 exact, in Pro7's order and deferred. The bodies are Pro7's line for line, but every template type carries an invented name: CTStateElem (Pro7 TemplArg), TemplateMatchState (DeduceInfo), TypeClassExt800 (TemplClassInst), TypeClassTemplate (TemplClass), TemplateContext (TypeDeduce). Pro7's CTempTool_GetPTMTemplArgExpr, ParamHasDefaultArg and DeduceExprCheck are absent, probably inlined.

Functions:
- `CTemplTool_InitDeduceInfo` 78.9: shape, not a wrong body: the original loads the 3rd parameter (args) in the prologue (AGENTS: such a parameter is often typed as the struct it dereferences) and orders the slot stores differently; Pro7's `param = param->next; i++` order may matter too
- `CTemplTool_DeduceTemplDepType` 97.4: registers

Names: Function names are Pro7's. Types are invented (see summary). data_0055d5c0 (line 829) is Pro7's sttemplexpr.

Data: No statics of note. raw_at is 1088 bytes in 4 runs.

Hacks:
- src/frontend/CTemplateTools.c:829 data_0055d5c0 placeholder for sttemplexpr

Next:
1. Rename the template types to Pro7's across the four CTemplate units (templates.h, CTemplate*.h)
2. Try a typed third parameter and Pro7's loop order on InitDeduceInfo

### src/frontend/CompilerTools.c

B, effort S, confidence high; 48/48 exact, linked, deferred. Pro7 CompilerTools.c, deferred (2.6's split: LinkErrorsPPC_EABI.c and COSToolsCLT.c are separate).

Links from source (48/48), in Pro7's order and names. Three things are left: a dead function under an invented name, a private lower-case helper, and three per-function `#pragma sym off` blocks with no explanation.

Names: Pro7 names throughout. `CompilerTools_ConvertCStringToPString` (src/frontend/CompilerTools.c:26, declared in CompilerTools.h:79, no callers, no row) is Pro7's `CTool_CtoPstr` under an invented name, with a void return where Pro7 returns `unsigned char *`.

Data: Statics are Pro7's, in reverse order, under pack(2) (has a TODO). GListErrorProc and name_hash_nodes are raw COMMONs (fine).

Structure: ok (deferred, Pro7 order)

Hacks:
- src/frontend/CompilerTools.c:918-969 `#pragma sym off`/`reset` around freelheap, freeaheap and freeoheap, with no TODO saying why (AGENTS: a pragma after `}` already applies to that function, so check what it really covers)
- src/frontend/CompilerTools.c:1097 `CTOOLS_LOWER`: a private inline in place of MSL's tolower macro
- src/frontend/CompilerTools.c:26 dead `CompilerTools_ConvertCStringToPString` (Pro7's CTool_CtoPstr)
- src/frontend/CompilerTools.c:9 `#pragma pack(2)` for .bss layout (has a TODO)

Next:
1. Rename CompilerTools_ConvertCStringToPString to CTool_CtoPstr with Pro7's signature, or delete it if 2.6's .data does not need it
2. Document or remove the sym off pragmas (test with relink_units --only)
3. Replace CTOOLS_LOWER with the ctype.h tolower that 2.6 inlined, if it still links

### src/msl/ThreadLocalData.c

B, effort S, confidence high; 5/5 exact, linked. MSL Win32 ThreadLocalData.c (updater's), adapted to 2.x's error path.

Links. A copy of the library source with 2.x's differences (stderr message, exit 7), documented in the file. It keeps one TODO'd literal-padding workaround.

Hacks:
- src/msl/ThreadLocalData.c:55: "\0\0\0" padded literal (TODO, accepted small workaround)

### src/msl/time_win32.c

B, effort S, confidence medium; 4/4 exact, linked. MSL/runtime library source via #include wrapper plus one hand-written function.

Links. Includes MSL's time.win32.c, then adds subtract_time_zone_bias by hand: the library copy lacks it, so 2.6's MSL revision differs here. The name looks invented and has no TODO.

Names: subtract_time_zone_bias: no MSL/Wii evidence quoted, no TODO

Hacks:
- src/msl/time_win32.c:4: hand-written function appended to a library include, name unverified

Next:
1. Check Wii publics/MSL later revisions for the real name of 0x... subtract_time_zone_bias; add a TODO if invented

### src/optimizer/AddPropagation.c

B, effort S, confidence high; 4/4 exact, linked, deferred. Pro7 AddPropagation.c, deferred.

Links from source (4/4); Pro7's four functions in 2.6 shape, and add_prop is defined here. What is left is naming: 1.x types (AddPropagationEntry for Pro7's Candidate), the precedes cast, and is_add/addpropagatestouse/propagateandremoveadd kept global for other versions' lifted code (with a comment).

Names: Pro7 function names. Types and globals are 1.x: struct AddPropagationEntry, cm_entries, code_motion_entries, data_00587fe4 (see CopyPropagation).

Data: .data placed (strings, add_prop, assert file name, jump table). propagatedadds stays extern on purpose (COMMON layout), with a comment.

Hacks:
- AddPropagation.c:192 precedes((void **)candidateInstr, useInstr) cast
- AddPropagation.c:47-53 extern declarations of other units' globals in the .c

Next:
1. Adopt Pro7's Candidate/Propagation types and UseDefChains names with the shared header cleanup
2. Make is_add etc. static once no version lifts this unit

### src/optimizer/COptimizer.c

B, effort S, confidence high; 12/12 exact, linked, deferred. Pro7 COptimizer.c, deferred, 2.6 shape.

Links from source (12/12) and is Pro7's file in Pro7 order, built deferred. One placeholder COMMON name is left: data_00588513 is Pro7's copt_isleaffunction. With that renamed it is an A.

Names: Pro7 names. Placeholder: data_00588513 (COptimizer.c:328,588; COptimizer.h:51; binding _data_00588513 0x5eb38e) is Pro7's `copt_isleaffunction` (set in COpt_Optimizer, cleared at EFUNCCALL). Check the name against its COMMON neighbours' mwld_hash before renaming.

Data: Its statics are Pro7's (the dead CSE's included). .data/.bss placed. stvoid is CParser's.

Hacks:
- COptimizer.c:328/588 data_00588513 placeholder for copt_isleaffunction

Next:
1. Rename data_00588513 -> copt_isleaffunction (rename.py --data; check the hash order and grep for member clashes)

### src/optimizer/CopyPropagation.c

B, effort S, confidence high; 9/9 exact, linked, deferred. Pro7 CopyPropagation.c (with Propagation.c folded in), deferred.

Links from source (9/9), Pro7's file in 2.6 shape. It still reaches the 1.x use-def globals through macro aliases and a fake-offset `reg_Uses`. Its propagation record is raw data: `data_005ce5f8` (0x5d18b8), with the "COPY"/"COPIES"/"c%ld" strings at 0x5d18a0, lies in CTemplateClass's .data range. The original's object .data starts at 0x5d18a0, not 0x5d18d8.

Names: 1.x use-def globals stand in for Pro7's UseDefChains.h: code_motion_entries=Defs, cm_entries=Uses, codeMotionEntryCount=number_of_Defs, data_00587e38=number_of_Uses, data_00587fe4=usedefinfo, code_motion_register_definition_heads/data_00587f04/register_definition_heads=reg_Defs[GPR/FPR/VR], code_motion_register_use_heads/codeMotionUseEntryHeads/register_use_entry_heads=reg_Uses[GPR/FPR/VR], gCodeMotionObjectTree_005880ac=objectusedefs, gCodeMotionAllocationList_005870fc=objectusedeflist. Placeholders here: data_005ce5f8 (copy_prop), data_005e662c (propagatedcopies), data_005ea87c_recursive macro. Candidates is typed as the 1.x struct AddPropagationEntry (Pro7 Candidate).

Data: The static copy_prop record and its strings are not compiled (lifted bytes in the neighbour's range). The splits give CTemplateClass.c 0x5d17d8-0x5d18d8, which includes them.

Structure: ok (deferred, Pro7 order); .data boundary wrong (see data)

Hacks:
- CopyPropagation.c:62-64 #define aliases (data_005ea87c_recursive, cp_firstcandidateinblock, cp_ncandidatesinblock) for 1.x names
- CopyPropagation.c:67 `#define reg_Uses ((&register_use_entry_heads) - 2)` fake array by pointer arithmetic
- CopyPropagation.c:418/527 `extern char data_005ce5f8[]` cast to CopyPropConfig* instead of defining `static Propagation copy_prop`
- CopyPropagation.c:457 / UseDefChains.c:48 precedes typed `(void **, void *)` with casts (Pro7: `int precedes(PCode *, PCode *)`)
- CopyPropagation.c:415-418 extern declarations of globals in the .c

Next:
1. Define copy_prop (and its strings) in the source, fix the .data split to start at 0x5d18a0 (make_splits/data_at), and drop the data_005ce5f8 binding
2. Switch to Pro7's UseDefChains.h names when that header is converted; remove the macros
3. Type precedes as Pro7's

### src/optimizer/IROUseDef.c

B, effort M, confidence high; 22/24 exact, not linked, deferred. Pro7 IROUseDef.c in Pro7 order, deferred, plus a 2.6 helper.

14/24 exact. All ten inexact functions are register or spill near-misses, the worst being MarkRemovableLoopDefs at 76.7%, a 114-byte function with only eax/ecx swapped. The bodies are Pro7's. The main blocker is data: Pro7's statics (NumDefs, FirstDef, LastDef, NumUses, Reaches, MightReachAnyUse, alldefs, alluses, defset, useset) are plain globals here. MWCC caches a non-address-taken static in a register across calls (AGENTS: IRO_SplitLifetimes needs NumDefs static), so these functions cannot match while the globals stay.

Names: IRO_AddTypeConversion (0x460a40) is a 2.6 addition with an invented name and no TODO. IroTransform's fn_0045b720 is the same helper without the list parameter.

Data: The 'Globals (generated from the original's .bss)' block should be Pro7's statics. It also defines IRO_FirstVar and IRO_NumVars, which Pro7 defines in IroVars.c, with IRO_NumVars typed `unsigned int` where Pro7 has SInt32. NumUses is `int` where Pro7 has SInt32. AssignmentFoldingFunction (Pro7: static) and IRO_NonAssignmentOp (Pro7: defined here) are extern only, taken raw.

Structure: ok (deferred, Pro7 order).

Hacks:
- IROUseDef.c:26-40 generated globals that are Pro7's statics
- IROUseDef.c:43 `extern AssignmentFoldingFunc AssignmentFoldingFunction[];`: never defined

Next:
1. Make all Pro7 statics static at once and define AssignmentFoldingFunction and IRO_NonAssignmentOp here (Pro7), then re-score: the near-misses should mostly fall to that
2. Move IRO_FirstVar and IRO_NumVars (SInt32) to IroVars.c
3. Run decl_order on what is left (MarkUsesByFunctionCall 92.2, IRO_GetSelfAssignmentVal 95.0)
4. TODO-mark the invented IRO_AddTypeConversion name

### src/optimizer/IroBitVect.c

B, effort S, confidence high; 12/12 exact, linked. Pro7 IroBitVect.c written in binary order (not deferred).

11/11 exact and links, but its first function is elsewhere. Bv_IsEmpty (0x52bcc0, just below Bv_IsSubset) is filed under CIRTransform.c, and the original is Pro7's file deferred: Pro7's last function, Bv_IsEmpty, comes first in the binary. The current non-deferred, binary-order file links only because nothing is inlined across it.

Names: Pro7 names. Pro7's dead Bv_AllocVectorLocal is absent.

Structure: Take Bv_IsEmpty from CIRTransform.c. Put the file in Pro7's order with -inline auto,deferred (docs/names-2.6.md marks it 'to change'). The .text should start at 0x52bcc0.

Next:
1. Move Bv_IsEmpty here, reorder to Pro7's order, build deferred, re-run make_splits and relink

### src/optimizer/IroDump.c

B, effort S, confidence high; 8/8 exact, linked, deferred. Pro7 IroDump.c, deferred, with 2.6's dead-function shapes.

8/8 exact and links. One placeholder is left: the log-file name is built from `data13_005e7064`. That address is cparams + 0x92, i.e. `cparams.sourcefile` (the CompilerLinkerParamBlk's main source file).

Names: Pro7 names. Placeholder: data13_005e7064 (IroDump.c:24, used at :480). It is cparams.sourcefile (0x5eb014 = cparams 0x5eaf82 + 0x92).

Data: Statics are Pro7's (DumpFile, nodenames). The binding _data13_005e7064 should go once the use is a field.

Hacks:
- IroDump.c:24 `extern PFile data13_005e7064;`: a fragment of cparams standing in for `cparams.sourcefile`

Next:
1. Replace `&data13_005e7064` with `&cparams.sourcefile`, delete the extern and the _data13_005e7064 binding in every version, then verify

### src/optimizer/IroEval.c

B, effort S, confidence high; 14/14 exact, linked, deferred. Pro7 IroEval.c, deferred.

14/14 exact and links. What remains is header hygiene: IRO_MakeULong is a private `static inline` copy of the helper Pro7 keeps in IroUtil.h. A stale comment calls the `static` IsAssociativeENodeType global-for-lifted-code.

Names: Pro7 names. The unk_member hits (x0/x4/x8) are Pro7's own placeholder members.

Data: IsAssociativeENodeType is static (correct). The cint64_* constants are CInt64.c's.

Hacks:
- IroEval.c:20 private copy of Pro7's IroUtil.h `IRO_MakeULong` (CW_INLINE there)
- IroEval.c:27 stale comment '(global while a lifted function still uses it)' on a now-static array

Next:
1. Move IRO_MakeULong/IRO_MakeLong into include/compiler/IroUtil.h as Pro7 has them; drop the private copies here and in IroUnrollLoop.c; check every version
2. Drop the stale comment

### src/optimizer/IroExprRegeneration.c

B, effort M, confidence medium; 18/21 exact, not linked, deferred. Pro7 IroExprRegeneration.c, deferred.

18/21 exact. GeneratePossibleCondAss (77.9) is a register and frame near-miss: the original keeps two more locals in its frame ([esp+0x14], [esp+0x30]), a known blocker. RebuildPossibleLogicalExpression (93.5) and RebuildPossibleCondExpression (97.6) differ in spills. The bodies are Pro7's.

Names: Pro7 names, with Regen_ prefixes on GetExprUses/GetExprKills because IroPropagate has the same statics (legitimate per AGENTS). IROFlowgraph_sub_4C2xxx are Pro7's names. IsConditionalBranch and CheckUnorderedRegionForUses are not in Pro7: check whether they are invented.

Data: scuai/scseai are Pro7's statics kept global for lifted code (commented).

Structure: ok (deferred). The IRONodes queue and its IROFlowgraph_sub_* helpers are private copies here; Pro7 keeps them in IroFlowgraph.h.

Hacks:
- IroExprRegeneration.c:~5-40 private copy of IroFlowgraph.h's IRONodes type and helpers

Next:
1. Move IRONodes and its inline helpers to include/compiler/IroFlowgraph.h and verify
2. GeneratePossibleCondAss: look for the two extra locals (a parameter copy or a split variable)
3. Make scuai/scseai static once all three match

### src/optimizer/IroFlowgraph.c

B, effort S, confidence high; 6/6 exact, linked, deferred. Pro7 IroFlowgraph.c, deferred.

5/6 exact. IRO_BuildFlowgraph is a register-only near-miss (93.9). The split is wrong: IroFlowgraph's .text runs to 0x477b80 and swallows 39 functions of the pointer analysis (0x471e90-0x477b80: PointerAnalysis_ParseFunctionModifiesSpecifier, the ADT functions ...), so the unit's range can never link as one object.

Names: Pro7 names (x26/x3C are Pro7's).

Data: Pro7's globals.

Structure: Unit boundary: IroFlowgraph should end at 0x471e90, where IroPointerAnalysis's object begins (its first row, PointerAnalysis_ParseFunctionModifiesSpecifier, has no source, so make_splits does not anchor the PA unit there).

Next:
1. Fix the IroFlowgraph/IroPointerAnalysis boundary in make_splits (or config) at 0x471e90
2. Run decl_order on IRO_BuildFlowgraph

### src/optimizer/IroPropagate.c

B, effort S, confidence high; 8/9 exact, not linked, deferred. Pro7 IroPropagate.c, deferred.

8/9 exact. IRO_CopyAndConstantPropagation (97.5%) differs by a single reload (the original reloads [esp+0xc] before a store at +0x64a): a type or shape detail. The body is Pro7's.

Names: Pro7 names (x20/x1a are Pro7's).

Data: IRO_FirstAssign/IRO_LastAssign/IRO_NumAssign as Pro7, but listed under a 'generated' comment. `char propagation_dump_message[]` (IroPropagate.c:22) is a global standing in for the IRO_Dump literal that Pro7 writes inline at its use (Pro7 IroPropagate.c:382); 2.6 apparently dropped the call but kept the literal, so it belongs in the dead or kept code as a literal.

Hacks:
- IroPropagate.c:22 generated string global instead of a literal at its use

Next:
1. Find the reload in IRO_CopyAndConstantPropagation (try an enum-typed field or Pro7's local order)
2. Turn propagation_dump_message back into a literal where 2.6 had it, checking .data

### src/optimizer/LoopOptimization.c

B, effort S, confidence high; 13/17 exact, not linked. Pro7 LoopOptimization.c bodies, non-deferred.

13/17 exact; the bodies are Pro7's with renamed locals (unrollunknownBDNZ 67%, unrollloopconditional 88%, rewriteunknownloopwithBDNZ 94.7%, unrollloop 97.1%). Problems: built non-deferred in binary order; a 1.x duplicate of skiplooptest (fn_0052b1a0) that optimizeloop calls, with a binding at skiplooptest's address; a `#define NULL 0`; and ConstantPropagation's propagateconstants as its last function.

Functions:
- `unrollunknownBDNZ` 67.1: shape (Pro7 body, renamed locals); retry deferred
- `unrollloopconditional` 88.0: shape
- `rewriteunknownloopwithBDNZ` 94.7: near miss
- `unrollloop` 97.1: near miss: literal opcodes 82..85 and oalloc(0x2c) for Pro7's PC_CMP.. and sizeof(PCodeBlock)

Names: Pro7 names (rows renamed already). Loop fields invented (see LoopDetection). liveonexit/inductionvariables/last_virtual_GPR are Pro7's statics kept global. optimizedloop_full_unroll/optimizedloop_trans_regs are defined in GlobalOptimizer.c as gArrayToRegister*.

Data: .data/.bss placed (Candidates, n_real_registers, data_00587fe4 are other units').

Structure: Should be deferred: the binary has Pro7's order reversed (optimizeloops lowest, computeliveonexit highest). propagateconstants (0x575c20) is ConstantPropagation's last Pro7 function: in a deferred unit it comes first in the next object, so the LoopOptimization/ConstantPropagation boundary is 0x575c20.

Hacks:
- LoopOptimization.c:57 `#define NULL 0`
- LoopOptimization.c:59-63 CE_ASSERT macro
- LoopOptimization.c:429-431 empty `if (loop->has_indexed_load) {} else if (...) {}` standing in for Pro7's insertupdateinstructions call (an empty function in 2.6?)
- LoopOptimization.c:1413 fn_0052b1a0: 1.x copy of skiplooptest called by optimizeloop; bindings.json:1665 `_fn_0052b1a0` at 0x575770 = skiplooptest's address (two names at one address)
- LoopOptimization.c:1639 propagateconstants belongs to ConstantPropagation.c
- LoopOptimization.c:1385-1391 literal opcodes 83/85/82/84 and oalloc(0x2c)

Next:
1. Make optimizeloop call skiplooptest; delete fn_0052b1a0 and its binding
2. Move propagateconstants to ConstantPropagation.c; reorder to Pro7's order, deferred (insertupdateinstructions/ispowerof2/deleteloop/lookup_array_object as Pro7's statics)
3. Remove `#define NULL 0` and CE_ASSERT; define optimizedloop_full_unroll/_trans_regs here

### src/optimizer/StrengthReduction.c

B, effort S, confidence high; 18/18 exact, linked, deferred. Pro7 StrengthReduction.c, deferred.

Links from source (18/18), Pro7's file in Pro7 order, deferred. What is left is the shared 1.x use-def and induction-variable type names, plus two documented TODO workarounds.

Names: Function names are Pro7's. Types are 1.x (CMRegisterNode, CodeMotionCandidate for Pro7's BasicInductionVar/InductionVar). gStrengthReductionChanged (Pro7 strengthreducedloops) is defined in GlobalOptimizer.c.

Data: .data placed; reg_Defs declared as an array here (StrengthReduction.c:20) while the other units use scalars.

Hacks:
- StrengthReduction.c:13 TODO: local copies of bitvectorgetbit/FITS_IN_SHORT/HIGH_PART/LOW_PART pending headers
- StrengthReduction.c:16-20 TODO extern reg_Defs[RegClassMax] beside the scalar heads
- StrengthReduction.c:256-259 TODO comma expression standing in for a statement count (documented workaround)

Next:
1. Adopt Pro7's induction-variable type names and UseDefChains.h with the shared conversion
2. Move strengthreducedloops' definition here under its Pro7 name (check the COMMON hash order)

### src/runtime/ExceptionX86.cpp

B, effort S, confidence medium; 26/26 exact, linked. copy of the Pro 5.3 updater's ExceptionX86.cpp, trimmed to 2.6.

Links. A pasted copy of lib/runtime/ExceptionX86.cpp, minus what 2.6 moved to exchand, rather than an #include. clang reports 24 errors (x86 inline asm). It defines exchand's list head as `data_0057d000`.

Names: 19 #defines give the statics unit-prefixed names (convention). data_0057d000 is a placeholder

Hacks:
- src/runtime/ExceptionX86.cpp:84: `extern "C" ExceptionTableHeader *data_0057d000` placeholder
- 24 clang errors (inline asm)

Next:
1. Name data_0057d000 (TODO if invented)
2. Consider #including lib/runtime/ExceptionX86.cpp with #if'd differences instead of a full copy

### src/runtime/NewHand.cpp

B, effort S, confidence high; 1/1 exact, linked. MSL NewHand.cpp via #include (older revision than 2.6's).

Does not link. Unresolved `?__throws_bad_alloc@std@@3DA`: 2.6's NewHand defines `char __throws_bad_alloc` at .data 0x5a9008, inside this unit's range, and lib/runtime's revision does not. It also re-emits bad_alloc/exception inline members that newop.cpp owns.

Names: #define default_new_handler NewHand_default_new_handler (unit-prefix convention)

Data: missing `__throws_bad_alloc` (1 byte, initialised; .data 0x5a9008)

Next:
1. Add `namespace std { char __throws_bad_alloc = 1; }` (check the byte's value) after the #include, or patch lib's NewHand.cpp the way download_tool.py patches time.c
2. Same COMDAT handling as newop.cpp for the exception classes

### src/runtime/exchand.cpp

B, effort S, confidence medium; 3/3 exact, linked. later revision of the runtime's exchand, written from the disassembly.

Links. 2.6 links a later exchand than lib/runtime's (stdout message and an exception-table registry), so it was written by hand and the file says so. Its statics and string still have address names.

Names: data_0057d004/data_0057d008 (lookup init flags), data_0057d000 (table list head, defined in ExceptionX86.cpp), data_00536030 (a string literal)

Data: statics are static (good). The message string is a named global where it is probably a literal at its use

Hacks:
- src/runtime/exchand.cpp:17: data_00536030 string as a named global
- src/runtime/exchand.cpp:20-21: data_0057d004/8 placeholder statics

Next:
1. Give the statics descriptive names (TODO invented)
2. Make the message a literal at its use if the .data still matches

### src/runtime/newop.cpp

B, effort M, confidence medium; 4/4 exact, linked. MSL newop.cpp via #include.

Does not link to the original. MSL's own operator new matches, but the object also emits std::exception's inline virtuals (dtor, vector-deleting dtor, what), which the original has once at 0x401a70/0x401dc0. Its own bad_alloc functions (0x426f50..) are rows with no source. This is link mechanics with weak/inline functions, not code.

Names: ok (MSL's)

Data: .data 152 bytes vs the unit's 80 (vtables of the extra exception classes)

Structure: Rows ??1bad_alloc, ??_Ebad_alloc, ?what@bad_alloc have no source but are this object's (inline members it emits)

Next:
1. Teach relink/make_splits that inline virtual members (exception's dtor/what) are COMDAT-like: the first object wins, so drop them from later objects' comparison
2. Assign the three bad_alloc rows to src/runtime/newop.cpp

### src/runtime/setupargs.c

B, effort S, confidence low; 3/3 exact, linked. runtime setupargs, hand-written (no lib copy used).

Links. A hand-written version of the runtime's argument setup with __argc/__argv (checked against COMMON hash order per AGENTS.md). It reads cleanly. Check whether lib/runtime has a setupargs.c that could be #included instead.

Next:
1. If lib/ has setupargs.c, try the #include wrapper

## A: done

### src/backend/Coloring.c

A, effort S, confidence high; 5/5 exact, linked, deferred. Pro7's Coloring.c, deferred.

Links. Pro7's file, deferred, in Pro7's order. The unk_member hits are Pro7's own IGNode fields (x10, x12, x14).

### src/backend/FuncLevelAsmPPC.c

A, effort S, confidence high; 4/4 exact, linked, deferred. Pro7's FuncLevelAsmPPC.c with 2.6 additions, deferred.

Links. Pro7's file in Pro7 order, deferred, with 2.6's additions documented in AGENTS. One shared placeholder global.

### src/backend/MachineSimulation601.c

A, effort S, confidence high; 6/6 exact, linked, deferred. Pro7's MachineSimulation file, deferred, with tables taken from the original's bytes.

Links. Pro7's file with its statics #define'd to unit-prefixed names (the form AGENTS sanctions, because binding names are global) and the MachineInfo defined before the timing table as 2.6 has it.

### src/backend/MachineSimulation603.c

A, effort S, confidence high; 6/6 exact, linked, deferred. Pro7's MachineSimulation file, deferred, with tables taken from the original's bytes.

Links. Pro7's file with its statics #define'd to unit-prefixed names (the form AGENTS sanctions, because binding names are global) and the MachineInfo defined before the timing table as 2.6 has it.

### src/backend/MachineSimulation603e.c

A, effort S, confidence high; 6/6 exact, linked, deferred. Pro7's MachineSimulation file, deferred, with tables taken from the original's bytes.

Links. Pro7's file with its statics #define'd to unit-prefixed names (the form AGENTS sanctions, because binding names are global) and the MachineInfo defined before the timing table as 2.6 has it.

### src/backend/MachineSimulation604.c

A, effort S, confidence high; 6/6 exact, linked, deferred. Pro7's MachineSimulation file, deferred, with tables taken from the original's bytes.

Links. Pro7's file with its statics #define'd to unit-prefixed names (the form AGENTS sanctions, because binding names are global) and the MachineInfo defined before the timing table as 2.6 has it.

### src/backend/MachineSimulation750.c

A, effort S, confidence high; 6/6 exact, linked, deferred. Pro7's MachineSimulation file, deferred, with tables taken from the original's bytes.

Links. Pro7's file with its statics #define'd to unit-prefixed names (the form AGENTS sanctions, because binding names are global) and the MachineInfo defined before the timing table as 2.6 has it.

### src/backend/MachineSimulation821.c

A, effort S, confidence high; 6/6 exact, linked, deferred. Pro7's MachineSimulation file, deferred, with tables taken from the original's bytes.

Links. Pro7's file with its statics #define'd to unit-prefixed names (the form AGENTS sanctions, because binding names are global) and the MachineInfo defined before the timing table as 2.6 has it.

### src/backend/PPCError.c

A, effort S, confidence high; 4/4 exact, linked, deferred. Pro7's PPCError.c, deferred.

Links. Pro7's file with 2.6's message enum; one TODO about ErrorTerm's VA helper.

### src/backend/ScanFloatX86.c

A, effort S, confidence low; 1/1 exact, linked. new this round (not audited).

Added in the link round after the audit; not graded by an auditor.

### src/driver/Arguments.c

A, effort S, confidence high; 25/25 exact, linked. Pro7 Arguments.c.

Links; Pro7's Arguments.c (respfilehandle and the response-file buffer order are fixed). A TODO notes that 2.6's isspace is MSL's inline C++ form, which ctype.h lacks.

### src/driver/CLAccessPaths.c

A, effort S, confidence medium; 18/18 exact, linked. Pro7 Project/CLAccessPaths.c.

Links from source; Pro7's file and names.

### src/driver/CLErrors.c

A, effort S, confidence high; 9/9 exact, linked. Pro7 CLErrors.c.

Links; Pro7's file.

### src/driver/CLOverlays.c

A, effort S, confidence medium; 17/17 exact, linked. Pro7 Project/CLOverlays.c.

Links from source; Pro7's file and names.

### src/driver/CLProj.c

A, effort S, confidence medium; 2/2 exact, linked. Pro7 Project/CLProj.c.

Links from source; Pro7's file and names.

### src/driver/CLSegs.c

A, effort S, confidence medium; 8/8 exact, linked. Pro7 Project/CLSegs.c.

Links from source; Pro7's file and names.

### src/driver/CLStaticMain.c

A, effort S, confidence high; 1/1 exact, linked. Pro7 CLStaticMain.c.

Links; Pro7's file. The raw named data are its CMDLINE_BUILD_DATE/TIME strings.

### src/driver/ErrMgr.c

A, effort S, confidence medium; 2/2 exact, linked. Pro7 MacEmul/ErrMgr.c.

Links from source; Pro7's file and names.

### src/driver/FileHandles.c

A, effort S, confidence medium; 5/5 exact, linked. Pro7 OSLib/FileHandles.c.

Links from source; Pro7's file and names.

### src/driver/Files.c

A, effort S, confidence medium; 22/22 exact, linked. Pro7 MacEmul/Files.c.

Links from source; Pro7's file and names.

### src/driver/Generic.c

A, effort S, confidence medium; 12/12 exact, linked. Pro7 OSLib/Generic.c.

Links from source; Pro7's file and names.

### src/driver/LowMem.c

A, effort S, confidence medium; 2/2 exact, linked. Pro7 MacEmul/LowMem.c.

Links from source; Pro7's file and names.

### src/driver/MacFileTypes.c

A, effort S, confidence medium; 6/6 exact, linked. Pro7 OSLib/MacFileTypes.c.

Links from source; Pro7's file and names.

### src/driver/MacSpecs.c

A, effort S, confidence medium; 15/15 exact, linked. Pro7 OSLib/MacSpecs.c.

Links; Pro7's MacSpecs.c with 2.x additions. OS_ShortenSpec/OS_CanonNameCase are invented names, and the file says so in comments.

### src/driver/MemUtils.c

A, effort S, confidence medium; 5/5 exact, linked. Pro7 OSLib/MemUtils.c.

Links from source; Pro7's file and names.

### src/driver/ParserErrors.c

A, effort S, confidence medium; 13/13 exact, linked. Pro7 ParserErrors.c.

Links from source; Pro7's file and names.

### src/driver/ResourceStrings.c

A, effort S, confidence high; 2/2 exact, linked. Pro7 ResourceStrings.c.

Links; Pro7's file.

### src/driver/StaticParserGlue.c

A, effort S, confidence high; 2/2 exact, linked. Pro7 StaticParserGlue.c.

Links; Pro7's file.

### src/driver/StdTargetWarningHelpers-cc.c

A, effort S, confidence medium; 2/2 exact, linked. Pro7 StdTargetWarningHelpers-cc.c.

Links from source; Pro7's file and names.

### src/driver/StringExtras.c

A, effort S, confidence medium; 4/4 exact, linked. Pro7 OSLib/StringExtras.c.

Links from source; Pro7's file and names.

### src/driver/cc-eabi-ppc-mw.c

A, effort S, confidence medium; 5/5 exact, linked. 2.x static plugin table (Wii module cc-eppc-mw).

Links. Small and clean; it has no data to place.

### src/driver/cc-eabi-ppc.c

A, effort S, confidence medium; 17/17 exact, linked. Pro7 CCompiler-plugin glue (cc-mach-ppc analogue) with EABI names.

Links. The unk_member metric is noise; names follow the Pro7 analogues.

### src/driver/libimp-eabi-ppc.c

A, effort S, confidence medium; 11/11 exact, linked. Pro7 libimp-mach-ppc analogue.

Links.

### src/frontend/CScope.c

A, effort S, confidence high; 68/68 exact, linked, deferred. Pro7 CScope.c in 2.6 shape, deferred.

Links from source (68/68), with Pro7's order, names and bodies. One documented shape TODO (line 1593: int vs short compares) and the 2-byte packing at the end of the file for .bss.

### src/msl/abort_exit.c

A, effort S, confidence high; 4/4 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/alloc_g.c

A, effort S, confidence high; 20/20 exact, linked. MSL/runtime library source via #include wrapper, with unit-prefix #defines for statics.

Library source through an #include wrapper; links. The #defines only give the library's statics unique global row names (functions.json names are global), the documented convention.

### src/msl/ansi_files.c

A, effort S, confidence high; 5/5 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/ansifp_x86.c

A, effort S, confidence high; 3/3 exact, linked. reconstruction of MSL's sourceless ansifp_x86.obj.

Links. A careful reconstruction of an object the library ships without source; documented. The unk_member hits are false positives (__HI/__LO masks).

### src/msl/arith.c

A, effort S, confidence high; 7/7 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/assert.c

A, effort S, confidence high; 1/1 exact, linked. MSL assert.c, 2.6's console branch, hand-written.

Links. One function with the console branch that 2.6's MSL has and lib/msl's copy lacks. It is documented.

### src/msl/buffer_io_g.c

A, effort S, confidence high; 6/6 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/char_io.c

A, effort S, confidence high; 5/5 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/ctype.c

A, effort S, confidence high; 3/3 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/direct_io_g.c

A, effort S, confidence high; 2/2 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/extras.c

A, effort S, confidence high; 2/2 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/fcntl_win32_g.c

A, effort S, confidence high; 1/1 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/file_io.c

A, effort S, confidence high; 5/5 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/file_io_win32_g.c

A, effort S, confidence high; 7/7 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/file_pos_g.c

A, effort S, confidence high; 4/4 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/mbstring.c

A, effort S, confidence high; 3/3 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/mem.c

A, effort S, confidence high; 4/4 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/misc_io.c

A, effort S, confidence high; 2/2 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/pool_alloc_win32_g.c

A, effort S, confidence high; 2/2 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/printf_g.c

A, effort S, confidence high; 15/15 exact, linked. MSL/runtime library source via #include wrapper, with unit-prefix #defines for statics.

Library source through an #include wrapper; links. The #defines only give the library's statics unique global row names (functions.json names are global), the documented convention.

### src/msl/qsort_g.c

A, effort S, confidence high; 1/1 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/scanf.c

A, effort S, confidence high; 6/6 exact, linked. MSL/runtime library source via #include wrapper, with unit-prefix #defines for statics.

Library source through an #include wrapper; links. The #defines only give the library's statics unique global row names (functions.json names are global), the documented convention.

### src/msl/setjmp_x86.c

A, effort S, confidence high; 2/2 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/signal.c

A, effort S, confidence high; 2/2 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/startup_win32.c

A, effort S, confidence high; 2/2 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/string.c

A, effort S, confidence high; 11/11 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/string_io.c

A, effort S, confidence high; 3/3 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/strtold.c

A, effort S, confidence high; 2/2 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/sysenv_win32.c

A, effort S, confidence high; 1/1 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/time_g.c

A, effort S, confidence high; 17/17 exact, linked. MSL/runtime library source via #include wrapper (patched later-revision time/gmtime/localtime).

Links. MSL time.c through the include path with lib/msl/extra/time.c's patched functions; static-name #defines. A TODO asks for the real later MSL revision of time.c.

### src/msl/unistd_win32.c

A, effort S, confidence high; 4/4 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/wchar_io.c

A, effort S, confidence high; 4/4 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/wcstold.c

A, effort S, confidence high; 1/1 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/wcstoul.c

A, effort S, confidence high; 2/2 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/wctype.c

A, effort S, confidence high; 2/2 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/msl/wscanf.c

A, effort S, confidence high; 4/4 exact, linked. MSL/runtime library source via #include wrapper, with unit-prefix #defines for statics.

Library source through an #include wrapper; links. The #defines only give the library's statics unique global row names (functions.json names are global), the documented convention.

### src/optimizer/IrOptimizer.c

A, effort S, confidence high; 6/6 exact, linked, deferred. Pro7 IrOptimizer.c, deferred.

6/6 exact and links. Pro7's file in Pro7's order, built deferred. Pro7's dead IRO_Cleanup is absent, and the linked data needs nothing from it.

### src/optimizer/IroCSE.c

A, effort S, confidence high; 18/18 exact, linked, deferred. Pro7 IroCSE.c, deferred.

18/18 exact and links. It is Pro7's file minus four dead functions. The only smell is its 'Globals (generated)' block, which defines IroTransform's globals and gives IRO_NumExprs the wrong type.

### src/optimizer/IroEmptyLoop.c

A, effort S, confidence high; 7/7 exact, linked, deferred. Pro7 IroEmptyLoop.c, deferred.

7/7 exact and links. The 'volatile' hit is a dump string, and x28/x30/LoopFlags_* are Pro7's placeholders.

### src/optimizer/IroJump.c

A, effort S, confidence high; 6/6 exact, linked, deferred. Pro7 IroJump.c, deferred.

6/6 exact and links. Pro7's file; the gotos are Pro7's.

### src/optimizer/IroMalloc.c

A, effort S, confidence high; 12/12 exact, linked, deferred. Pro7 IroMalloc.c (MSL alloc.c's allocator), deferred.

12/12 exact and links. The 85 unk_member hits are Pro7's x0/x14/xC/unk field names for MSL's allocator structs. Readability polish could take MSL alloc.c's member names; that is not a defect.

### src/optimizer/IroRangePropagation.c

A, effort S, confidence high; 5/5 exact, linked, deferred. Pro7 IroRangePropagation.c, deferred.

5/5 exact and links.

### src/optimizer/IroSubable.c

A, effort S, confidence high; 2/2 exact, linked, deferred. Pro7 IroSubable.c, deferred.

2/2 exact and links. Pro7's dead IRO_IsVectorTempCandidate is absent, and the data does not need it.

### src/optimizer/IroUtil.c

A, effort S, confidence high; 55/55 exact, linked, deferred. Pro7 IroUtil.c, deferred, with 2.6's IRO_FindFirst split.

55/55 exact and links. 2.6's IRO_FindFirst renumbers and then calls a recursive helper, written here as `static FindFirst`, an invented name without a TODO. One TODO-documented workaround remains in IRO_UnsignedType.

### src/optimizer/IroVars.c

A, effort S, confidence high; 23/23 exact, linked, deferred. Pro7 IroVars.c, deferred.

23/23 exact and links. Two documented TODO workarounds (IroVars.c:1158 argobj copy, :1288 hi) are the acceptable kind.

### src/runtime/LongLongx86.c

A, effort S, confidence high; 5/5 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/runtime/NMWException.cpp

A, effort S, confidence high; 5/5 exact, linked. MSL/runtime library source via #include wrapper, with unit-prefix #defines for statics.

Library source through an #include wrapper; links. The #defines only give the library's statics unique global row names (functions.json names are global), the documented convention.

### src/runtime/arrcondes.cpp

A, effort S, confidence high; 1/1 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/runtime/delarrop.cpp

A, effort S, confidence high; 1/1 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/runtime/delop.cpp

A, effort S, confidence high; 1/1 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/runtime/globdest.c

A, effort S, confidence high; 1/1 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/runtime/maincrt.c

A, effort S, confidence high; 1/1 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/runtime/runinit.c

A, effort S, confidence high; 1/1 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.

### src/runtime/stackall.c

A, effort S, confidence high; 2/2 exact, linked. MSL/runtime library source via #include wrapper.

Two-line wrapper that #includes the library's own source; links byte for byte. Nothing to do.
