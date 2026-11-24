# Wii 1.0RC1 against GC 3.0a5.2

The Wii 1.0RC1 `mwcceppc.exe` (6073 functions, laid out from its CodeView in `research/Wii_1_0RC1`) was mapped onto the GC 3.0a5.2 one
(no symbols) by relocation-masked code matching (`tools/wii_remap.py`; layout in `research/GC_3_0a5_2`).
3550 functions (973515 bytes) are identical once relocations and call targets are masked, 2007 are found in 3.0a5.2
but differ (1218671 bytes in RC1, 1082758 in 3.0a5.2), 516 (299049 bytes) have no counterpart in 3.0a5.2.
A further 429 functions of 3.0a5.2 (262731 bytes)
have no RC1 counterpart.

How to read the numbers: "identical" means the bytes are the same except for relocated addresses and call/jmp operands, so a changed callee
does not make the caller differ. "differs" means the function was found by call-graph propagation (`propagated`), by its position between two
found functions (`order`) or by byte similarity (`fuzzy`), so the pairing of the `order` and `fuzzy` ones is a guess; the last two columns say how many.
A function whose size changed by only a few bytes is typically a compiler-wide change (a new register or an inlined helper), a function
that doubled is a rewrite. Functions that moved between modules are counted in the RC1 module they have there.

## Reduced Wii 1.0RC1 behavior

Run `mwcc-tools corpus --oracle-root . --version Wii_1_0RC1 --canaries gc2/research/Wii_1_0RC1/repros`
with `--exe gc2/build/Wii_1_0RC1/clang/mwcceppc.exe --details --keep DIR` from the repository root.
The fixtures in this directory document open diffs.

- [if-ranges.c](Wii_1_0RC1/repros/if-ranges.c) reproduces the guard in Rhythm Heaven
  Fever's `FaderFlash.cpp`. With `opt_optimizecontrolflow` enabled, stock groups the
  consecutive signed values into two signed bounds at `-O0` through `-O3`. At `-O4,p`
  it uses a subtraction and one unsigned compare. Stock retains three comparisons
  with the pragma disabled. The gap and volatile accesses control for different inputs
  and repeated side effects. Inspect stock `IRO_OptimizeIfs` at `0x61cda0` and its CFG
  visitor at `0x61d1c0`. The source's bounded interval pass already handles the negated
  disjunction at `-O4,p`.
- For translation units with only generated `@nnnn` string-table differences, compare
  the allocation sequence around stock `CParser_GetUniqueID` at `0x457190` and
  `CParser_PrintUniqueID` at `0x457110`. The Rhythm Heaven Fever MyChecker variants
  share a nonuniform shift in their IDs.

## RC1 modules, by differing bytes

| module | unit | identical | differs | RC1-only | differing bytes (RC1 / 3.0a5.2) | RC1-only bytes | order+fuzzy |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CPrepLexer | wii/CPrepLexer | 58 | 58 | 5 | 41902 / 37763 | 8594 | 26 |
| InstrSelection | src/backend/InstrSelection.c | 39 | 21 | 26 | 40155 / 44730 | 27187 | 16 |
| CPrepScanner | wii/CPrepScanner | 82 | 40 | 17 | 39875 / 33368 | 48797 | 28 |
| CFunc | src/frontend/CFunc.c | 28 | 50 | 2 | 38582 / 33438 | 1151 | 14 |
| CDecl | src/frontend/CDecl.c | 16 | 56 | 7 | 36663 / 32557 | 7173 | 33 |
| CExprGen | wii/CExprGen | 23 | 47 | 0 | 31508 / 28480 | 0 | 11 |
| CInit | src/frontend/CInit.c | 24 | 31 | 1 | 30333 / 24887 | 141 | 11 |
| CInline | src/frontend/CInline.c | 28 | 46 | 15 | 29962 / 28678 | 2310 | 27 |
| CParser | src/frontend/CParser.c | 59 | 54 | 0 | 28540 / 24485 | 0 | 21 |
| CPrec | src/frontend/CPrec.c | 37 | 35 | 3 | 28296 / 26447 | 3757 | 14 |
| InlineAsmPPC | src/backend/InlineAsmPPC.c | 18 | 21 | 1 | 27937 / 21359 | 403 | 13 |
| CExprConvMatch | src/frontend/CExprConvMatch.c | 4 | 28 | 6 | 25610 / 21669 | 4175 | 17 |
| Intrinsics | src/backend/Intrinsics.c | 23 | 14 | 1 | 24253 / 24401 | 247 | 11 |
| CTemplateTools | src/frontend/CTemplateTools.c | 27 | 48 | 3 | 24128 / 22350 | 289 | 20 |
| CError | src/frontend/CError.c | 36 | 21 | 1 | 23012 / 21675 | 360 | 3 |
| CExpr | src/frontend/CExpr.c | 15 | 38 | 1 | 22819 / 18129 | 255 | 12 |
| DWARF | src/backend/DWARF.c | 14 | 27 | 3 | 21843 / 21341 | 1386 | 19 |
| EPPC_DWARF2 | wii/EPPC_DWARF2 | 8 | 24 | 26 | 21676 / 17040 | 24438 | 17 |
| CScope | src/frontend/CScope.c | 23 | 50 | 7 | 21558 / 18802 | 656 | 21 |
| CABI | src/frontend/CABI.c | 15 | 38 | 2 | 20570 / 19429 | 441 | 16 |
| CodeGen | src/backend/CodeGen.c | 17 | 26 | 9 | 20438 / 18160 | 12197 | 12 |
| ObjGen_PPC_EABI | src/backend/ObjGen_PPC_EABI.c | 29 | 35 | 1 | 19735 / 17377 | 119 | 12 |
| IroLoop | src/optimizer/IroLoop.c | 22 | 32 | 0 | 19598 / 17200 | 0 | 12 |
| IroPointerAnalysis | src/optimizer/IroPointerAnalysis.c | 152 | 48 | 1 | 17349 / 16640 | 118 | 14 |
| CPrep | src/frontend/CPrep.c | 73 | 35 | 13 | 16949 / 15425 | 4310 | 22 |
| CMachine | src/backend/CMachine.c | 19 | 32 | 1 | 16867 / 15444 | 261 | 13 |
| IroExprRegeneration | src/optimizer/IroExprRegeneration.c | 8 | 16 | 1 | 16554 / 14411 | 322 | 9 |
| CTemplateNew | src/frontend/CTemplateNew.c | 6 | 23 | 0 | 15994 / 14833 | 0 | 18 |
| CExpr2 | src/frontend/CExpr2.c | 14 | 25 | 0 | 15699 / 12436 | 0 | 8 |
| TOC | src/backend/TOC.c | 12 | 17 | 3 | 15144 / 14200 | 1494 | 11 |
| IroUtil | src/optimizer/IroUtil.c | 84 | 35 | 1 | 14819 / 15192 | 303 | 10 |
| PCodeAssembly | src/backend/PCodeAssembly.c | 0 | 7 | 1 | 14373 / 9269 | 242 | 4 |
| PCodeInfo | src/backend/PCodeInfo.c | 6 | 16 | 5 | 13897 / 10796 | 1218 | 7 |
| CMiddleLayer | wii/CMiddleLayer | 13 | 31 | 3 | 13490 / 12749 | 611 | 16 |
| CClass | src/frontend/CClass.c | 17 | 52 | 6 | 13235 / 12812 | 999 | 24 |
| CException | src/frontend/CException.c | 17 | 30 | 4 | 12935 / 13162 | 766 | 19 |
| PeepholePatterns | wii/PeepholePatterns | 17 | 17 | 67 | 12503 / 8425 | 28029 | 16 |
| RegisterInfo | src/backend/RegisterInfo.c | 11 | 22 | 3 | 12459 / 7464 | 369 | 11 |
| CTemplateClass | src/frontend/CTemplateClass.c | 14 | 20 | 1 | 12289 / 11352 | 329 | 6 |
| Operands | src/backend/Operands.c | 17 | 19 | 1 | 12052 / 11558 | 372 | 5 |
| IroAggregateAssignment | wii/IroAggregateAssignment | 10 | 15 | 1 | 11163 / 10553 | 649 | 7 |
| CTemplateFunc | src/frontend/CTemplateFunc.c | 8 | 20 | 3 | 10535 / 7736 | 898 | 2 |
| IroUnrollLoop | src/optimizer/IroUnrollLoop.c | 12 | 10 | 0 | 10175 / 10192 | 0 | 6 |
| PeepholeForward | wii/PeepholeForward | 6 | 7 | 0 | 10155 / 8917 | 0 | 6 |
| StackFrame | src/backend/StackFrame.c | 18 | 20 | 9 | 9913 / 7911 | 11292 | 14 |
| ConstantPropagation | src/optimizer/ConstantPropagation.c | 3 | 2 | 1 | 9843 / 1337 | 826 | 1 |
| CodeGenOptPPC | src/driver/CodeGenOptPPC.c | 1 | 10 | 3 | 9438 / 4370 | 159 | 1 |
| SFPE_PPC_EABI | src/backend/SFPE_PPC_EABI.c | 0 | 7 | 2 | 9371 / 6746 | 4933 | 4 |
| IROUseDef | src/optimizer/IROUseDef.c | 12 | 7 | 2 | 9196 / 9211 | 2581 | 3 |
| InlineAsm | src/backend/InlineAsm.c | 15 | 12 | 0 | 8539 / 5810 | 0 | 1 |
| LoopOptimization | src/optimizer/LoopOptimization.c | 13 | 4 | 0 | 8532 / 7063 | 0 | 0 |
| FunctionCalls | src/optimizer/FunctionCalls.c | 1 | 4 | 0 | 8373 / 7103 | 0 | 3 |
| IroEval | src/optimizer/IroEval.c | 5 | 9 | 6 | 8008 / 8004 | 2700 | 8 |
| IroPropagate | src/optimizer/IroPropagate.c | 14 | 6 | 1 | 7551 / 7150 | 4489 | 2 |
| IroTransform | src/optimizer/IroTransform.c | 48 | 8 | 1 | 7473 / 7336 | 212 | 2 |
| IroLoopInvariants | wii/IroLoopInvariants | 12 | 23 | 0 | 7442 / 7767 | 0 | 17 |
| CIRStreamType | wii/CIRStreamType | 3 | 10 | 0 | 7415 / 6266 | 0 | 8 |
| CodeMotion | src/optimizer/CodeMotion.c | 7 | 9 | 2 | 7232 / 4802 | 620 | 2 |
| PCodeStatsPPC | wii/PCodeStatsPPC | 4 | 4 | 0 | 6833 / 5249 | 0 | 1 |
| IroFlowgraph | src/optimizer/IroFlowgraph.c | 0 | 20 | 0 | 6359 / 6943 | 0 | 16 |
| CIRStreamCallGraph | wii/CIRStreamCallGraph | 5 | 11 | 0 | 5621 / 5074 | 0 | 2 |
| CMangler | src/frontend/CMangler.c | 12 | 10 | 0 | 5497 / 5258 | 0 | 2 |
| CCompiler | src/driver/CCompiler.c | 11 | 3 | 0 | 5238 / 5350 | 0 | 3 |
| CExprTools | wii/CExprTools | 31 | 15 | 0 | 5225 / 4806 | 0 | 2 |
| IroDependsKills | wii/IroDependsKills | 8 | 5 | 0 | 4991 / 4475 | 0 | 0 |
| StructMoves | src/optimizer/StructMoves.c | 11 | 6 | 2 | 4800 / 4756 | 923 | 2 |
| Alias | src/optimizer/Alias.c | 6 | 15 | 12 | 4768 / 4061 | 3357 | 1 |
| CRTTI | src/frontend/CRTTI.c | 7 | 11 | 0 | 4670 / 4326 | 0 | 4 |
| BE_elf | src/backend/BE_elf.c | 24 | 10 | 0 | 4618 / 4435 | 0 | 5 |
| LocalCSE | wii/LocalCSE | 7 | 8 | 3 | 4542 / 4862 | 1212 | 0 |
| IroCodePositioning | wii/IroCodePositioning | 1 | 9 | 9 | 4508 / 4567 | 5401 | 9 |
| InterferenceGraph | src/backend/InterferenceGraph.c | 2 | 7 | 0 | 4397 / 4355 | 0 | 1 |
| IrOptimizer | src/optimizer/IrOptimizer.c | 1 | 2 | 0 | 4351 / 3967 | 0 | 1 |
| CMemberPointer | wii/CMemberPointer | 4 | 14 | 0 | 4335 / 4129 | 0 | 6 |
| CTemplateFuncInst | wii/CTemplateFuncInst | 8 | 7 | 0 | 4288 / 3845 | 0 | 1 |
| CompilerTools | src/frontend/CompilerTools.c | 33 | 30 | 0 | 3744 / 3693 | 0 | 5 |
| PCodeUtilities | src/backend/PCodeUtilities.c | 21 | 16 | 3 | 3613 / 3741 | 521 | 3 |
| CBrowse | src/frontend/CBrowse.c | 13 | 8 | 0 | 3114 / 3016 | 0 | 6 |
| IroLifetime | wii/IroLifetime | 6 | 1 | 0 | 3095 / 3095 | 0 | 1 |
| BE_symbol | src/backend/BE_symbol.c | 13 | 13 | 0 | 3014 / 2945 | 0 | 8 |
| CTypeTools | wii/CTypeTools | 22 | 13 | 2 | 2886 / 2517 | 70 | 5 |
| PCodeStats | wii/PCodeStats | 10 | 3 | 1 | 2845 / 3068 | 1136 | 2 |
| IroCSE | src/optimizer/IroCSE.c | 2 | 7 | 0 | 2828 / 2881 | 0 | 1 |
| Spill | wii/Spill | 1 | 6 | 0 | 2820 / 2890 | 0 | 6 |
| UseDefChains | src/optimizer/UseDefChains.c | 10 | 5 | 0 | 2807 / 2806 | 0 | 2 |
| IroLinearForm | src/optimizer/IroLinearForm.c | 38 | 1 | 0 | 2658 / 2658 | 0 | 1 |
| Switch | src/backend/Switch.c | 4 | 5 | 0 | 2524 / 2462 | 0 | 5 |
| FuncLevelAsmPPC | src/backend/FuncLevelAsmPPC.c | 5 | 4 | 1 | 2383 / 2177 | 1082 | 3 |
| Scheduler | src/backend/Scheduler.c | 6 | 6 | 0 | 2231 / 1288 | 0 | 4 |
| CLibrary | wii/CLibrary | 5 | 7 | 0 | 2106 / 2089 | 0 | 6 |
| IroJump | src/optimizer/IroJump.c | 0 | 8 | 12 | 2041 / 2037 | 10092 | 8 |
| CPreprocessor | wii/CPreprocessor | 16 | 4 | 0 | 1928 / 1895 | 0 | 1 |
| IroCounterLoop | wii/IroCounterLoop | 0 | 4 | 1 | 1787 / 2116 | 329 | 0 |
| IroEmptyLoop | src/optimizer/IroEmptyLoop.c | 1 | 4 | 0 | 1769 / 1769 | 0 | 4 |
| IroVars | src/optimizer/IroVars.c | 15 | 6 | 2 | 1728 / 1744 | 2422 | 4 |
| ProductLicensing | wii/ProductLicensing | 0 | 2 | 0 | 1718 / 1747 | 0 | 1 |
| AliasPropagationPPC | wii/AliasPropagationPPC | 2 | 3 | 1 | 1636 / 1310 | 282 | 1 |
| MachineSimulation603 | src/backend/MachineSimulation603.c | 3 | 4 | 1 | 1566 / 1581 | 20 | 4 |
| InlineAsmRegistersPPC | src/backend/InlineAsmRegistersPPC.c | 2 | 3 | 2 | 1503 / 1503 | 525 | 2 |
| CIRStream | wii/CIRStream | 33 | 7 | 1 | 1485 / 1333 | 120 | 2 |
| IroNonRegLoopAccesses | wii/IroNonRegLoopAccesses | 3 | 3 | 0 | 1471 / 1471 | 0 | 1 |
| IroType | wii/IroType | 19 | 4 | 0 | 1470 / 1460 | 0 | 0 |
| CLToolExec | src/driver/CLToolExec.c | 6 | 1 | 0 | 1396 / 1260 | 0 | 0 |
| Unmangle | src/frontend/Unmangle.c | 11 | 1 | 0 | 1135 / 1135 | 0 | 0 |
| CLDropinCallbacks_V10 | src/driver/CLDropinCallbacks_V10.cpp | 51 | 3 | 2 | 1103 / 920 | 636 | 3 |
| GCCInlineAsm | src/backend/GCCInlineAsm.c | 3 | 2 | 0 | 1072 / 957 | 0 | 1 |
| CIRTransform | src/optimizer/CIRTransform.c | 0 | 4 | 10 | 1037 / 771 | 6925 | 2 |
| PCode | src/backend/PCode.c | 22 | 4 | 0 | 1014 / 961 | 0 | 1 |
| IroStrengthReduction | wii/IroStrengthReduction | 7 | 3 | 0 | 964 / 908 | 0 | 2 |
| AliasPropagation | wii/AliasPropagation | 2 | 3 | 0 | 933 / 872 | 0 | 1 |
| Exceptions | src/backend/Exceptions.c | 8 | 1 | 0 | 925 / 668 | 0 | 1 |
| CPreprocess | src/frontend/CPreprocess.c | 8 | 2 | 0 | 899 / 932 | 0 | 2 |
| InlineAsmMnemonicsPPC | src/backend/InlineAsmMnemonicsPPC.c | 1 | 2 | 0 | 898 / 898 | 0 | 1 |
| Arguments | src/driver/Arguments.c | 24 | 3 | 0 | 876 / 868 | 0 | 0 |
| IroLoopReversal | wii/IroLoopReversal | 5 | 2 | 0 | 715 / 717 | 0 | 1 |
| Peephole | src/backend/Peephole.c | 4 | 3 | 0 | 705 / 659 | 0 | 0 |
| LoopDetection | src/optimizer/LoopDetection.c | 18 | 2 | 2 | 638 / 638 | 732 | 0 |
| PPCError | src/backend/PPCError.c | 0 | 4 | 1 | 616 / 526 | 12 | 0 |
| IroSubable | src/optimizer/IroSubable.c | 1 | 1 | 1 | 588 / 351 | 3 | 0 |
| MachineSimulation7400 | src/backend/MachineSimulation7400.c | 8 | 1 | 0 | 507 / 523 | 0 | 1 |
| MachineSimulationAltiVec | src/backend/MachineSimulationAltiVec.c | 9 | 1 | 0 | 507 / 507 | 0 | 1 |
| MachineSimulation601 | src/backend/MachineSimulation601.c | 6 | 2 | 0 | 465 / 469 | 0 | 2 |
| IroLibrary | wii/IroLibrary | 3 | 3 | 0 | 464 / 448 | 0 | 3 |
| MachineSimulationZen | wii/MachineSimulationZen | 0 | 1 | 6 | 460 / 379 | 1295 | 1 |
| MachineSimulation750 | src/backend/MachineSimulation750.c | 5 | 2 | 1 | 458 / 458 | 59 | 2 |
| MachineSimulation604 | src/backend/MachineSimulation604.c | 5 | 3 | 0 | 442 / 446 | 0 | 3 |
| VectorArraysToRegs | src/optimizer/VectorArraysToRegs.c | 8 | 1 | 0 | 421 / 421 | 0 | 0 |
| Coloring | src/backend/Coloring.c | 0 | 1 | 3 | 415 / 383 | 1772 | 1 |
| IroLoopRotation | wii/IroLoopRotation | 0 | 1 | 0 | 410 / 410 | 0 | 0 |
| CodeGen_RegisterTokens | wii/CodeGen_RegisterTokens | 0 | 3 | 0 | 354 / 184 | 0 | 3 |
| ELF_Lib_Import | src/driver/ELF_Lib_Import.c | 12 | 1 | 0 | 337 / 337 | 0 | 1 |
| IroExprs | wii/IroExprs | 6 | 2 | 0 | 314 / 316 | 0 | 1 |
| StrengthReduction | src/optimizer/StrengthReduction.c | 19 | 1 | 0 | 279 / 31 | 0 | 0 |
| CLLicenses | src/driver/CLLicenses.c | 5 | 5 | 1 | 275 / 246 | 1383 | 2 |
| COSToolsWin32 | wii/COSToolsWin32 | 43 | 11 | 0 | 270 / 238 | 0 | 2 |
| LiveRegisters | wii/LiveRegisters | 8 | 2 | 0 | 256 / 256 | 0 | 0 |
| IroDependenceAnalysis | wii/IroDependenceAnalysis | 14 | 2 | 0 | 234 / 235 | 0 | 0 |
| IroRangePropagation | src/optimizer/IroRangePropagation.c | 7 | 1 | 0 | 212 / 212 | 0 | 1 |
| CodeMotionPPC | wii/CodeMotionPPC | 5 | 1 | 0 | 206 / 206 | 0 | 1 |
| BitVectors | src/backend/BitVectors.c | 7 | 4 | 1 | 196 / 192 | 63 | 0 |
| DumpUtils | wii/DumpUtils | 12 | 2 | 0 | 163 / 176 | 0 | 0 |
| SourceText | wii/SourceText | 2 | 1 | 0 | 124 / 125 | 0 | 0 |
| CCallGraph | wii/CCallGraph | 14 | 1 | 0 | 118 / 118 | 0 | 0 |
| LoadDeletion | src/optimizer/LoadDeletion.c | 2 | 2 | 0 | 105 / 70 | 0 | 2 |
| IroSerializeAssignments | wii/IroSerializeAssignments | 2 | 1 | 0 | 103 / 100 | 0 | 1 |
| ParserHelpers | src/driver/ParserHelpers.c | 10 | 1 | 2 | 96 / 86 | 28 | 0 |
| imp-elf | wii/imp-elf | 4 | 7 | 0 | 96 / 96 | 0 | 6 |
| cc-eppc | wii/cc-eppc | 5 | 6 | 0 | 90 / 90 | 0 | 6 |
| CompIDECallbacks | wii/CompIDECallbacks | 3 | 4 | 0 | 60 / 60 | 0 | 4 |
| MachineSimulation821 | src/backend/MachineSimulation821.c | 5 | 1 | 2 | 59 / 47 | 383 | 1 |
| COptimizer | src/optimizer/COptimizer.c | 13 | 1 | 1 | 57 / 57 | 14 | 1 |
| CLMain | src/driver/CLMain.c | 41 | 4 | 0 | 47 / 47 | 0 | 2 |
| CLIO | src/driver/CLIO.c | 35 | 6 | 1 | 42 / 42 | 8 | 0 |
| IroMalloc | src/optimizer/IroMalloc.c | 13 | 2 | 0 | 34 / 34 | 0 | 1 |
| ClientGlue | src/driver/ClientGlue.c | 7 | 3 | 0 | 31 / 31 | 0 | 0 |
| EnodeInfoHandler | wii/EnodeInfoHandler | 3 | 1 | 0 | 31 / 31 | 0 | 1 |
| MemUtils | src/driver/MemUtils.c | 5 | 2 | 0 | 30 / 30 | 0 | 0 |
| Resources | src/driver/Resources.c | 26 | 3 | 0 | 30 / 30 | 0 | 0 |
| cobraPragmas | wii/cobraPragmas | 3 | 1 | 0 | 30 / 26 | 0 | 1 |
| Option | src/driver/Option.c | 35 | 4 | 0 | 29 / 29 | 0 | 0 |
| alloc | wii/alloc | 29 | 2 | 0 | 27 / 27 | 0 | 0 |
| NMWException | src/runtime/NMWException.cpp | 3 | 2 | 2 | 26 / 26 | 24 | 0 |
| CLFiles | src/driver/CLFiles.c | 13 | 2 | 0 | 26 / 26 | 0 | 0 |
| Memory | src/driver/Memory.c | 11 | 3 | 0 | 26 / 26 | 0 | 0 |
| IroDump | src/optimizer/IroDump.c | 5 | 2 | 0 | 24 / 24 | 0 | 2 |
| misc_io | src/msl/misc_io.c | 0 | 2 | 0 | 24 / 24 | 0 | 0 |
| CLPluginRequests | src/driver/CLPluginRequests.cpp | 20 | 2 | 0 | 23 / 23 | 0 | 0 |
| Help | src/driver/Help.c | 17 | 1 | 0 | 23 / 23 | 0 | 0 |
| setupargs | src/runtime/setupargs.c | 4 | 1 | 0 | 21 / 21 | 0 | 0 |
| DWARF_GlobalRefs | wii/DWARF_GlobalRefs | 4 | 1 | 0 | 18 / 18 | 0 | 0 |
| LMGR8C | wii/LMGR8C | 0 | 3 | 0 | 18 / 18 | 0 | 0 |
| VERSION | wii/VERSION | 0 | 3 | 0 | 18 / 18 | 0 | 0 |
| CLCompilerLinkerDropin_V10 | src/driver/CLCompilerLinkerDropin_V10.cpp | 28 | 1 | 1 | 16 / 16 | 8 | 0 |
| CLPlugins | src/driver/CLPlugins.c | 44 | 1 | 0 | 16 / 16 | 0 | 0 |
| MsDos | src/driver/MsDos.c | 69 | 3 | 0 | 15 / 15 | 0 | 0 |
| newop | src/runtime/newop.cpp | 2 | 2 | 0 | 15 / 15 | 0 | 2 |
| onetimeinit | wii/onetimeinit | 0 | 1 | 0 | 15 / 15 | 0 | 0 |
| ExceptionX86 | src/runtime/ExceptionX86.cpp | 18 | 2 | 2 | 12 / 12 | 18 | 2 |
| CInt64 | src/frontend/CInt64.c | 44 | 2 | 0 | 12 / 12 | 0 | 1 |
| EPPC_LinkUtilities | wii/EPPC_LinkUtilities | 10 | 1 | 0 | 12 / 12 | 0 | 1 |
| LoopDetectionPPC | wii/LoopDetectionPPC | 9 | 1 | 0 | 12 / 12 | 0 | 0 |
| MsgUtils | wii/MsgUtils | 24 | 2 | 0 | 12 / 12 | 0 | 2 |
| ParserGlue-eppc-cc | wii/ParserGlue-eppc-cc | 3 | 1 | 0 | 12 / 12 | 0 | 0 |
| CLPluginAPI | wii/CLPluginAPI | 4 | 1 | 0 | 11 / 11 | 0 | 0 |
| CLPrefs | src/driver/CLPrefs.c | 7 | 1 | 0 | 11 / 11 | 0 | 0 |
| cc-eppc-mw | wii/cc-eppc-mw | 4 | 1 | 0 | 11 / 11 | 0 | 0 |
| stackall | src/runtime/stackall.c | 1 | 1 | 0 | 11 / 11 | 0 | 0 |
| Parameter | src/driver/Parameter.c | 42 | 1 | 12 | 9 / 9 | 37 | 0 |
| CWPluginsPrivate | src/driver/CWPluginsPrivate.cpp | 53 | 1 | 0 | 7 / 7 | 0 | 0 |
| CLDependencies | src/driver/CLDependencies.c | 20 | 1 | 0 | 6 / 6 | 0 | 0 |
| CLParserCallbacks_v1 | src/driver/CLParserCallbacks_v1.cpp | 11 | 1 | 0 | 6 / 6 | 0 | 0 |
| KERNEL32 | wii/KERNEL32 | 0 | 1 | 0 | 6 / 6 | 0 | 0 |
| PCodeListing | src/backend/PCodeListing.c | 0 | 1 | 0 | 6 / 6 | 0 | 0 |
| TranslatorUtils | wii/TranslatorUtils | 14 | 2 | 0 | 6 / 6 | 0 | 1 |
| WS2_32 | wii/WS2_32 | 0 | 1 | 0 | 6 / 10 | 0 | 0 |
| startup.win32 | wii/startup.win32 | 5 | 1 | 0 | 6 / 6 | 0 | 0 |
| IroLoopVector | wii/IroLoopVector | 4 | 1 | 0 | 3 / 3 | 0 | 0 |
| DumpIR | src/backend/DumpIR.c | 0 | 2 | 0 | 2 / 2 | 0 | 1 |
| buffer_io | wii/buffer_io | 4 | 2 | 0 | 2 / 2 | 0 | 0 |
| BuildAgentClient | wii/BuildAgentClient | 9 | 1 | 0 | 1 / 1 | 0 | 1 |
| TargetOptimizerHelpers-eppc | wii/TargetOptimizerHelpers-eppc | 2 | 1 | 0 | 1 / 1 | 0 | 0 |
| SWCH_Rules | wii/SWCH_Rules | 0 | 0 | 20 | 0 / 0 | 13305 | 0 |
| VLE_AfterColoring | wii/VLE_AfterColoring | 0 | 0 | 16 | 0 / 0 | 11969 | 0 |
| SWCH_DecisionTree | wii/SWCH_DecisionTree | 0 | 0 | 39 | 0 / 0 | 4403 | 0 |
| VLE_BeforeColoring | wii/VLE_BeforeColoring | 0 | 0 | 10 | 0 / 0 | 2832 | 0 |
| AddPropagation | src/optimizer/AddPropagation.c | 2 | 0 | 2 | 0 / 0 | 2460 | 0 |
| MachineSimulationELF | wii/MachineSimulationELF | 0 | 0 | 9 | 0 / 0 | 2021 | 0 |
| VLE_CodeGen | wii/VLE_CodeGen | 0 | 0 | 4 | 0 / 0 | 1949 | 0 |
| MachineSimulation603e | src/backend/MachineSimulation603e.c | 1 | 0 | 9 | 0 / 0 | 1893 | 0 |
| SWCH_Set | wii/SWCH_Set | 0 | 0 | 2 | 0 / 0 | 973 | 0 |
| ToolHelpers | src/driver/ToolHelpers.c | 4 | 0 | 1 | 0 / 0 | 688 | 0 |
| SWCH_ValueSet | wii/SWCH_ValueSet | 0 | 0 | 3 | 0 / 0 | 643 | 0 |
| SWCH_Cell | wii/SWCH_Cell | 0 | 0 | 4 | 0 / 0 | 559 | 0 |
| SWCH_Interface | wii/SWCH_Interface | 0 | 0 | 5 | 0 / 0 | 552 | 0 |
| SWCH_Main | wii/SWCH_Main | 0 | 0 | 2 | 0 / 0 | 348 | 0 |
| VLE_Utils | wii/VLE_Utils | 0 | 0 | 3 | 0 / 0 | 149 | 0 |
| Registers | src/backend/Registers.c | 11 | 0 | 1 | 0 / 0 | 127 | 0 |
| InlineAsmRegisters | src/backend/InlineAsmRegisters.c | 3 | 0 | 1 | 0 / 0 | 78 | 0 |
| ParserFace | src/driver/ParserFace.c | 13 | 0 | 2 | 0 / 0 | 30 | 0 |
| BuildAgentClientDispatch | wii/BuildAgentClientDispatch | 18 | 0 | 0 | 0 / 0 | 0 | 0 |
| BuildAgentCommon | wii/BuildAgentCommon | 10 | 0 | 0 | 0 / 0 | 0 | 0 |
| BuildAgentProtocol | wii/BuildAgentProtocol | 56 | 0 | 0 | 0 / 0 | 0 | 0 |
| BuildAgentSocketAPI | wii/BuildAgentSocketAPI | 8 | 0 | 0 | 0 / 0 | 0 | 0 |
| CIRStreamTools | wii/CIRStreamTools | 17 | 0 | 0 | 0 / 0 | 0 | 0 |
| CLAccessPaths | src/driver/CLAccessPaths.c | 23 | 0 | 0 | 0 / 0 | 0 | 0 |
| CLBrowser | src/driver/CLBrowser.c | 9 | 0 | 0 | 0 / 0 | 0 | 0 |
| CLErrors | src/driver/CLErrors.c | 9 | 0 | 0 | 0 / 0 | 0 | 0 |
| CLFileOps | src/driver/CLFileOps.c | 25 | 0 | 0 | 0 / 0 | 0 | 0 |
| CLIncludeFileCache | src/driver/CLIncludeFileCache.c | 9 | 0 | 0 | 0 / 0 | 0 | 0 |
| CLLoadAndCache | src/driver/CLLoadAndCache.c | 3 | 0 | 0 | 0 / 0 | 0 | 0 |
| CLOverlays | src/driver/CLOverlays.c | 17 | 0 | 0 | 0 / 0 | 0 | 0 |
| CLProj | src/driver/CLProj.c | 2 | 0 | 0 | 0 / 0 | 0 | 0 |
| CLSegs | src/driver/CLSegs.c | 8 | 0 | 0 | 0 / 0 | 0 | 0 |
| CLStaticMain | src/driver/CLStaticMain.c | 1 | 0 | 0 | 0 / 0 | 0 | 0 |
| CLTarg | src/driver/CLTarg.c | 12 | 0 | 0 | 0 / 0 | 0 | 0 |
| CLWriteObjectFile | src/driver/CLWriteObjectFile.c | 2 | 0 | 0 | 0 / 0 | 0 | 0 |
| CWParserPluginsPrivate | src/driver/CWParserPluginsPrivate.cpp | 17 | 0 | 0 | 0 / 0 | 0 | 0 |
| CWSecretPluginCallbacks | src/driver/CWSecretPluginCallbacks.cpp | 4 | 0 | 0 | 0 / 0 | 0 | 0 |
| CleanUpIR | wii/CleanUpIR | 12 | 0 | 0 | 0 / 0 | 0 | 0 |
| CopyPropagation | src/optimizer/CopyPropagation.c | 6 | 0 | 0 | 0 / 0 | 0 | 0 |
| DateTimeUtils | src/driver/DateTimeUtils.c | 1 | 0 | 0 | 0 / 0 | 0 | 0 |
| DeadCodeElimination | wii/DeadCodeElimination | 1 | 0 | 0 | 0 / 0 | 0 | 0 |
| DropInCompilerLinkerPrivate | src/driver/DropInCompilerLinkerPrivate.cpp | 27 | 0 | 0 | 0 / 0 | 0 | 0 |
| ELF_Endian | src/backend/ELF_Endian.c | 8 | 0 | 0 | 0 / 0 | 0 | 0 |
| FileHandles | src/driver/FileHandles.c | 5 | 0 | 0 | 0 / 0 | 0 | 0 |
| Files | src/driver/Files.c | 17 | 0 | 0 | 0 / 0 | 0 | 0 |
| Generic | src/driver/Generic.c | 14 | 0 | 0 | 0 / 0 | 0 | 0 |
| GlobalOptimizer | src/optimizer/GlobalOptimizer.c | 3 | 0 | 0 | 0 / 0 | 0 | 0 |
| IO | src/driver/IO.c | 2 | 0 | 0 | 0 / 0 | 0 | 0 |
| IRFlowgraph | wii/IRFlowgraph | 6 | 0 | 0 | 0 / 0 | 0 | 0 |
| IroBitVect | src/optimizer/IroBitVect.c | 16 | 0 | 0 | 0 / 0 | 0 | 0 |
| IroScalarization | wii/IroScalarization | 14 | 0 | 0 | 0 / 0 | 0 | 0 |
| LinkErrorsPPC_EABI | src/driver/LinkErrorsPPC_EABI.c | 5 | 0 | 0 | 0 / 0 | 0 | 0 |
| LongLongx86 | src/runtime/LongLongx86.c | 2 | 0 | 0 | 0 / 0 | 0 | 0 |
| MacFileTypes | src/driver/MacFileTypes.c | 6 | 0 | 0 | 0 / 0 | 0 | 0 |
| MacSpecs | src/driver/MacSpecs.c | 15 | 0 | 0 | 0 / 0 | 0 | 0 |
| NewHand | src/runtime/NewHand.cpp | 1 | 0 | 0 | 0 / 0 | 0 | 0 |
| OptimizerHelpers | src/driver/OptimizerHelpers.c | 3 | 0 | 0 | 0 / 0 | 0 | 0 |
| ParserErrors | src/driver/ParserErrors.c | 11 | 0 | 0 | 0 / 0 | 0 | 0 |
| ParserHelpers-cc | src/driver/ParserHelpers-cc.c | 7 | 0 | 0 | 0 / 0 | 0 | 0 |
| Projects | src/driver/Projects.c | 7 | 0 | 0 | 0 / 0 | 0 | 0 |
| Propagation | wii/Propagation | 5 | 0 | 0 | 0 / 0 | 0 | 0 |
| ResourceStrings | src/driver/ResourceStrings.c | 2 | 0 | 0 | 0 / 0 | 0 | 0 |
| ScanFloatX86 | src/backend/ScanFloatX86.c | 1 | 0 | 0 | 0 / 0 | 0 | 0 |
| StaticParserGlue | src/driver/StaticParserGlue.c | 2 | 0 | 0 | 0 / 0 | 0 | 0 |
| StdTargetWarningHelpers-cc | src/driver/StdTargetWarningHelpers-cc.c | 2 | 0 | 0 | 0 / 0 | 0 | 0 |
| StringExtras | src/driver/StringExtras.c | 4 | 0 | 0 | 0 / 0 | 0 | 0 |
| StringUtils | src/driver/StringUtils.c | 8 | 0 | 0 | 0 / 0 | 0 | 0 |
| Targets | src/driver/Targets.c | 4 | 0 | 0 | 0 / 0 | 0 | 0 |
| TextUtils | src/driver/TextUtils.c | 3 | 0 | 0 | 0 / 0 | 0 | 0 |
| ThreadLocalData | src/msl/ThreadLocalData.c | 6 | 0 | 0 | 0 / 0 | 0 | 0 |
| ToolHelpers-cc | src/driver/ToolHelpers-cc.c | 2 | 0 | 0 | 0 / 0 | 0 | 0 |
| Utils | src/driver/Utils.c | 7 | 0 | 0 | 0 / 0 | 0 | 0 |
| WarningHelpers | src/driver/WarningHelpers.c | 2 | 0 | 0 | 0 / 0 | 0 | 0 |
| Win32Shortcut | wii/Win32Shortcut | 1 | 0 | 0 | 0 / 0 | 0 | 0 |
| abort_exit_win32 | wii/abort_exit_win32 | 4 | 0 | 0 | 0 / 0 | 0 | 0 |
| ansi_files | src/msl/ansi_files.c | 5 | 0 | 0 | 0 / 0 | 0 | 0 |
| ansifp_x86 | src/msl/ansifp_x86.c | 3 | 0 | 0 | 0 / 0 | 0 | 0 |
| arith | src/msl/arith.c | 5 | 0 | 0 | 0 / 0 | 0 | 0 |
| assert.win32 | wii/assert.win32 | 2 | 0 | 0 | 0 / 0 | 0 | 0 |
| bsearch | wii/bsearch | 1 | 0 | 0 | 0 / 0 | 0 | 0 |
| char_io | src/msl/char_io.c | 1 | 0 | 0 | 0 / 0 | 0 | 0 |
| cobraError | wii/cobraError | 4 | 0 | 0 | 0 / 0 | 0 | 0 |
| cpprtl | wii/cpprtl | 1 | 0 | 0 | 0 / 0 | 0 | 0 |
| ctype | src/msl/ctype.c | 3 | 0 | 0 | 0 / 0 | 0 | 0 |
| delarrop | src/runtime/delarrop.cpp | 1 | 0 | 0 | 0 / 0 | 0 | 0 |
| delop | src/runtime/delop.cpp | 1 | 0 | 0 | 0 / 0 | 0 | 0 |
| destroy | wii/destroy | 1 | 0 | 0 | 0 / 0 | 0 | 0 |
| direct_io | wii/direct_io | 4 | 0 | 0 | 0 / 0 | 0 | 0 |
| exchand | src/runtime/exchand.cpp | 2 | 0 | 0 | 0 / 0 | 0 | 0 |
| excrtl | wii/excrtl | 5 | 0 | 0 | 0 / 0 | 0 | 0 |
| file_io | src/msl/file_io.c | 8 | 0 | 0 | 0 / 0 | 0 | 0 |
| file_io.win32 | wii/file_io.win32 | 14 | 0 | 0 | 0 / 0 | 0 | 0 |
| file_pos | wii/file_pos | 4 | 0 | 0 | 0 / 0 | 0 | 0 |
| globdest | src/runtime/globdest.c | 3 | 0 | 0 | 0 / 0 | 0 | 0 |
| locale | src/msl/locale.c | 5 | 0 | 0 | 0 / 0 | 0 | 0 |
| maincrt | src/runtime/maincrt.c | 1 | 0 | 0 | 0 / 0 | 0 | 0 |
| mbstring | src/msl/mbstring.c | 8 | 0 | 0 | 0 / 0 | 0 | 0 |
| mem | src/msl/mem.c | 4 | 0 | 0 | 0 / 0 | 0 | 0 |
| pool_alloc.win32 | wii/pool_alloc.win32 | 2 | 0 | 0 | 0 / 0 | 0 | 0 |
| printf | wii/printf | 16 | 0 | 0 | 0 / 0 | 0 | 0 |
| qsort | wii/qsort | 1 | 0 | 0 | 0 / 0 | 0 | 0 |
| runinit | src/runtime/runinit.c | 2 | 0 | 0 | 0 / 0 | 0 | 0 |
| scanf | src/msl/scanf.c | 5 | 0 | 0 | 0 / 0 | 0 | 0 |
| setjmp.x86 | wii/setjmp.x86 | 2 | 0 | 0 | 0 / 0 | 0 | 0 |
| signal | src/msl/signal.c | 1 | 0 | 0 | 0 / 0 | 0 | 0 |
| signal.win32 | wii/signal.win32 | 3 | 0 | 0 | 0 / 0 | 0 | 0 |
| string | src/msl/string.c | 13 | 0 | 0 | 0 / 0 | 0 | 0 |
| strtold | src/msl/strtold.c | 2 | 0 | 0 | 0 / 0 | 0 | 0 |
| strtoul | src/msl/strtoul.c | 5 | 0 | 0 | 0 / 0 | 0 | 0 |
| sysenv.win32 | wii/sysenv.win32 | 8 | 0 | 0 | 0 / 0 | 0 | 0 |
| time | wii/time | 17 | 0 | 0 | 0 / 0 | 0 | 0 |
| time.win32 | wii/time.win32 | 4 | 0 | 0 | 0 / 0 | 0 | 0 |
| wchar_io | src/msl/wchar_io.c | 1 | 0 | 0 | 0 / 0 | 0 | 0 |
| total | | 3550 | 2007 | 516 | 1218671 / 1082758 | 299049 | 907 |

## Modules to watch

| module | identical | differs | RC1-only | verdict |
| --- | ---: | ---: | ---: | --- |
| Coloring | 0 | 1 | 3 | changed (4 of 4 functions) |
| CopyPropagation | 6 | 0 | 0 | unchanged |
| InlineAsmRegisters | 3 | 0 | 1 | changed (1 of 4 functions) |
| InlineAsmRegistersPPC | 2 | 3 | 2 | changed (5 of 7 functions) |
| IrOptimizer | 1 | 2 | 0 | changed (2 of 3 functions) |
| IroJump | 0 | 8 | 12 | changed (20 of 20 functions) |
| IroLoop | 22 | 32 | 0 | changed (32 of 54 functions) |
| IroLoopInvariants | 12 | 23 | 0 | changed (23 of 35 functions) |
| IroLoopReversal | 5 | 2 | 0 | changed (2 of 7 functions) |
| IroLoopRotation | 0 | 1 | 0 | changed (1 of 1 functions) |
| IroLoopVector | 4 | 1 | 0 | changed (1 of 5 functions) |
| LiveRegisters | 8 | 2 | 0 | changed (2 of 10 functions) |
| LocalCSE | 7 | 8 | 3 | changed (11 of 18 functions) |
| Registers | 11 | 0 | 1 | changed (1 of 12 functions) |
| SWCH_Cell | 0 | 0 | 4 | changed (4 of 4 functions) |
| SWCH_DecisionTree | 0 | 0 | 39 | changed (39 of 39 functions) |
| SWCH_Interface | 0 | 0 | 5 | changed (5 of 5 functions) |
| SWCH_Main | 0 | 0 | 2 | changed (2 of 2 functions) |
| SWCH_Rules | 0 | 0 | 20 | changed (20 of 20 functions) |
| SWCH_Set | 0 | 0 | 2 | changed (2 of 2 functions) |
| SWCH_ValueSet | 0 | 0 | 3 | changed (3 of 3 functions) |
| Scheduler | 6 | 6 | 0 | changed (6 of 12 functions) |
| Spill | 1 | 6 | 0 | changed (6 of 7 functions) |
| StackFrame | 18 | 20 | 9 | changed (29 of 47 functions) |
| StructMoves | 11 | 6 | 2 | changed (8 of 19 functions) |
| VLE_AfterColoring | 0 | 0 | 16 | changed (16 of 16 functions) |
| VLE_BeforeColoring | 0 | 0 | 10 | changed (10 of 10 functions) |

`SWCH_*` (switch lowering) and `VLE_*` (VLE instruction sets) are new in the Wii compiler: neither the module names nor any of their
assert strings are in the 3.0a5.2 executable, so their functions are RC1-only for real, not unmatched. `Coloring` shares its file name
with 3.0a5.2 but only one of its four functions was found (changed); the other three may be new helpers or rewritten ones.
`LocalCSE` has no assert string of that name in 3.0a5.2, yet 7 of its 18 functions match, so it existed under another file name.


## 3.0a5.2 functions with no RC1 counterpart

429 functions, 262731 bytes, the 40 largest below. The module is the neighbours' module where both agree (`+between`).

| address | size | in module |
| --- | ---: | --- |
| 0x00485ba0 | 14028 | CPrepScanner |
| 0x005d2ce0 | 9487 | EPPC_DWARF2 |
| 0x00622db0 | 8314 | ConstantPropagation |
| 0x00489ad0 | 7869 | CPrepScanner |
| 0x0048ecd0 | 7359 | CPrepScanner |
| 0x004942f0 | 6057 | CPrepScanner |
| 0x0048b990 | 5534 | CPrepScanner |
| 0x00484b20 | 4223 | CPrepScanner |
| 0x005d1400 | 4149 | EPPC_DWARF2 |
| 0x004f3070 | 3780 | InstrSelection |
| 0x0050be10 | 3690 | CDecl |
| 0x0048cf30 | 3468 | CPrepScanner |
| 0x005f2740 | 3407 | IroPropagate |
| 0x004feed0 | 2972 | InstrSelection |
| 0x004ccc00 | 2754 | CPrepLexer |
| 0x0052b670 | 2676 | CFunc |
| 0x00576710 | 2637 | InlineAsmPPC |
| 0x00603c80 | 2628 | IROUseDef |
| 0x004caa90 | 2603 | CPrepLexer |
| 0x00465dd0 | 2588 | CodeGen |
| 0x0057ac30 | 2573 | ? |
| 0x004f90f0 | 2537 | InstrSelection |
| 0x0048e380 | 2382 | CPrepScanner |
| 0x004ca160 | 2351 | CPrepLexer |
| 0x004f87d0 | 2335 | InstrSelection |
| 0x0046e230 | 2273 | CPrec |
| 0x00443f30 | 2245 | CPrep |
| 0x006220b0 | 2170 | AddPropagation |
| 0x005861b0 | 2162 | StackFrame |
| 0x00489270 | 2142 | CPrepScanner |
| 0x005d2440 | 2106 | EPPC_DWARF2 |
| 0x0060c190 | 2072 | IroCodePositioning |
| 0x0046a170 | 2062 | CodeGen |
| 0x005e0710 | 2027 | CExprConvMatch |
| 0x005b28a0 | 1967 | IroVars |
| 0x00607cf0 | 1947 | IroExprRegeneration |
| 0x00589600 | 1943 | StackFrame |
| 0x00530c00 | 1867 | CInit |
| 0x005d51f0 | 1846 | EPPC_DWARF2 |
| 0x00533e40 | 1779 | CInit |

By module (bytes):

| module | bytes |
| --- | ---: |
| CPrepScanner | 55165 |
| EPPC_DWARF2 | 19697 |
| InstrSelection | 14185 |
| (between modules / unknown) | 13814 |
| CPrepLexer | 11968 |
| PeepholePatterns | 11183 |
| CodeGen | 10702 |
| ConstantPropagation | 9139 |
| StackFrame | 8184 |
| CDecl | 8125 |
| CExprConvMatch | 6957 |
| IroCodePositioning | 5160 |
| CInit | 4987 |
| CFunc | 4784 |
| IroPropagate | 4612 |
| InlineAsmPPC | 4527 |
| CExpr | 4307 |
| CodeGenOptPPC | 4055 |
| CPrep | 3963 |
| CPrec | 3717 |
| CParser | 3611 |
| IroExprRegeneration | 2981 |
| CInline | 2868 |
| CExpr2 | 2844 |
| CTemplateFunc | 2771 |
