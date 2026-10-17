# Per-unit audit of 2.6's data (October 2026)

A snapshot, like [audit-2.6.md](audit-2.6.md) (the code audit): every unit of GC_2_6's `.data`, `.bss`, `.rdata`
and `.exc`, measured on October 15, 2026 against the original. `tools/status.py GC_2_6` and `tools/relink.py
GC_2_6 --only SOURCE` give the current state; this file says what each unit's data needs and where its bytes
belong.

How it was measured, per unit: the compiled object's sections laid out as mwld lays them out (index order,
aligned, dropping the jump tables of functions the linker drops, as `relink.py` does) against the unit's ranges in
`splits.txt`; the bytes compared with relocated words masked (the object's relocations and the original's base
relocations); the string literals of both compared as multisets; the jump tables of each (an object section all of
whose relocations point at one function, and the original's relocated words pointing into `.text` but not at a
function start); the data placed by exact functions (`compare.json`'s placements); the `.bss` the object defines
plus the globals the source keeps COMMON for lifted code, against the range; every symbols.txt name and every
binding in the range; and every byte of each data section that no unit owns.

Grades (data only; the code's grade is the code audit's):

- **A**: the unit links, so its data is byte-identical. Some still carry placeholder names in symbols.txt (below).
- **B**: the data is ready. Sizes, bytes and literals agree, the jump tables differ only where a function is
  inexact, and the `.bss` fits once the globals held for lifted code become `static`. The code is all that is left.
- **C**: one or two concrete data fixes: a wrong cut between units, a literal, statics never defined, a dead
  function whose literals 2.6 kept.
- **D**: the data is a stand-in (fragments, generated blocks, 1.x tables) and is rewritten with the code.

Counts: 186 A, 20 B, 31 C, 8 D.

## Summary

| | Units | .data bytes | .bss bytes |
|---|---|---|---|
| A (links) | 186 | 219054 | 16012 |
| B (ready) | 20 | 47143 | 1810 |
| C (targeted fixes) | 31 | 77888 | 12263 |
| D (rewrite with the code) | 8 | 7752 | 2864 |

- **Every byte has an owner.** The only stretches of a data section outside every unit's range are the zero padding
  at the ends of `.rdata` (467 bytes), `.exc` (68), `.data` (76) and `.CRT` (504). The ownership problems are inside
  ranges: three cuts are wrong (IroDump, DumpIR, MachineSimulationAltiVec; the CodeGen/CPrep `.bss` cut probably too),
  and 32 KB at the head of MsDos' range belong to nothing known.
- **The B units' data needs nothing.** 47 KB of `.data` already agree byte for byte; these units link when their
  code does.
- **Most of what keeps C units from linking is small**: 8 units declare their own statics `extern` and define them
  nowhere; 6 units differ only by literals; 4 have their data in a neighbour's range.
- **The held globals are the largest single class of `.bss` work, and they resolve with the code.** 261 globals in
  30 units are kept COMMON (with the "global while a lifted function of this unit still uses them" comment) where
  2.6 has statics. Each becomes `static` once its unit's last lifted user is exact; nothing to do before then.

(October 17, 2026: down to three in every version, none of which anything can name; docs/agents-notes.md, "Every
symbol is a real name or the compiler's". Findings 7 and 8 below are done.)

Placeholder names in symbols.txt (`lbl_`, `data_`, `DAT_`, `_registration_name_`, `_initializer_data_`, `PTR_`),
by section: .data 976 (463 `lbl_`, 302 `_registration_name_`, 183 `data_`, 14 `DAT_`, 10 `_initializer_data_`, 4
`PTR_fn_`), .bss 1104 (917 `lbl_`, 172 `data_`, 14 `DAT_`, 1 `PTR_`), .rdata 65 (`lbl_`), .tls 1. 712 of them are
in units that link; they are naming work, not link work (finding 7). The `@stringBase0` symbols (3, in OpcodeInfo,
InlineAsmMnemonicsPPC and InlineAsmRegistersPPC) are not placeholders: it is MWCC's own name for a unit's pooled
strings (`#pragma pool_strings on`; CInit.c makes it), and all three units link.

## Findings across units

1. **IroDump's `.data` starts at 0x5a9838, not 0x5a9a08.** `"freturns("` sits at 0x5a9a00..0x5a9a0a, across the cut,
   and the 464 bytes before it are Pro7's `IRO_SpellType` literals in its switch order (`void`, `bool`, `char` ...
   `struct `, `class `), first in the object because IroDump is deferred and `IRO_SpellType` is near the end of Pro7's
   file. IroDump links today only because InstrSelection's split object supplies those bytes. AGENTS.md's note that
   2.6's dead `IRO_SpellType` has "(" and ")" where Pro7 has "freturns(" (and none of the basic type names) came from
   the wrong cut: the function is probably Pro7's as written. Move the cut, give IroDump Pro7's `IRO_SpellType`, and
   check the "(" ")" literals in the current range against what else uses them. InstrSelection's own data is then
   1032 bytes, which is what its object has.
2. **Exceptions' range holds DumpIR's data.** Exceptions' own `.data` is its first 216 bytes (0x5bc5c0..0x5bc698,
   which match); the 2384 bytes after are Pro7's `unsorted/DumpIR.c` literals (ENode names from `EPOSTINC`, the
   `EAT_*` action spellings, the dump formats), which no code references. DumpIR.c (the next unit, 2 stubs, links)
   has no `.data` range. Give DumpIR Pro7's dead dump functions and a `.data` range from 0x5bc698; Exceptions then
   lacks no data.
3. **VectorArraysToRegs' range holds MachineSimulationAltiVec's tables.** VectorArraysToRegs' object has 24 bytes,
   which match the start of its range; the other 4400 (the 7450's unit names `V_CFX`, `V_PERM`, `LSU2` ... and its
   tables) are machine7450's. Move the cut to 0x5d1b20, then write AltiVec's tables the way the other machine models'
   were (from the original's bytes, Pro7's statics).
4. **MsDos' 32 KB of zeros have no owner.** 0x599418..0x5a1418 is 0x8000 zero bytes of initialized data between
   MSL's wctype.c and MsDos' literals; no code and no data word points into it, and neither Pro7's driver nor lib/msl
   has a 32 KB array. `config.json`'s `"data_head"` hands it to MsDos so that its own 312 bytes (which match) can be
   placed. Candidates: a data-only object (a dead `static char buf[0x8000] = {0}` with `explicit_zero_data`, like
   float.c), or a larger `__WCMAP_SIZE` in 2.6's MSL wide-char tables. Open.
5. **Statics declared `extern` and defined nowhere.** The source names them through bindings, so the equivalent
   compiler works, but the object has no `.bss` for them: IroTransform (`AssignmentOp`, `ComplementaryOp`,
   `ComplementaryOpLogical`, 232 bytes), IROUseDef (`AssignmentFoldingFunction`, Pro7's static, 305 bytes),
   IroPointerAnalysis (13 `st*` statics, 224 bytes), MachineSimulationAltiVec (15 `data_*`, 392 bytes),
   FunctionCalls (2 unnamed, 80 bytes), Peephole (one unnamed, 541 bytes), ELF_Endian (`DAT_005805e0`, the error
   handler), BE_symbol (`data_0055da80`), CodeMotion (`DAT_00574ce8`), CFunc (`data_005dcc80`), InlineAsmPPC (8
   bytes unnamed), InlineAsm (1028 bytes beside `ASMstmtnb`). Define each in its unit: `static` with Pro7's name where
   no lifted code reaches it, otherwise a global with the usual comment and its start bound.
6. **Literals that differ.** StackFrame has `_savevr_%d` and `_restvr_%d` where 2.6 has `_savevr%d` and `_restvr%d`
   (its only data difference). IroPropagate has an extra `Found propagation at %d from %d\n` and IroTransform an extra
   `ReverseOp For Monmin at: %d\n`: 2.6 lacks those dump calls (IroPropagate's 36 extra bytes are exactly that
   string). Dead functions whose literals 2.6 kept, so their bodies go back in the source: CPrep (`compiler`,
   `exestr`, `linker`, `user`: Pro7's `#pragma comment` kinds; `(void)`, `(...)`, `operator new`, `operator
   delete`), IroPointerAnalysis (an assert `home != NULL`), InlineAsm (Pro7's operand dump: ` imm(%ld)`,
   ` obj(%s+%ld)` ...), CObjC (`L_OBJC_CLASS_REFERENCES_%ld`). Missing because the code is 1.x: PCodeInfo
   (vformatpcode's 41 formats, 772 bytes), InlineAsmPPC (`eieio`, `fcmpo`, `volatile`, `warn_def_reserved_reg` ...),
   DWARF (`MW EABI PPC C-Compiler`, used by the inexact emit_compile_unit).
7. **Placeholder names in units that link (712).** Most are `lbl_` at addresses inside a unit's statics: MWCC's
   objects reference a static through its section symbol plus an offset and give it no symbol of its own, so
   `layout.py` has no name for it and labels every referenced address (IroEval's `IsAssociativeENodeType[]` is 76
   one-byte `lbl_`s, the machine models' latency tables 21-33 each, InlineAsmRegistersPPC 22, CompilerTools' heaps
   25, IroMalloc 14, CLMain 15). The debug objects (`build/GC_2_6/debug/compiled`, symbols on) carry each static's
   name and offset in their CodeView (`S_LDATA32`, which `tools/mw/codeview.py` reads): `layout.py` could name statics
   from them and drop the labels inside a named extent. The rest are bindings: 698 bindings have placeholder names,
   601 of them referenced only by exact functions, so they can take their real names now (Intrinsics' 311
   `_registration_name_*`, named after 1.x addresses, are its `intrinsics[]` entries; CMachine's 83; cc-eabi-ppc 22;
   CLDropinCallbacks_V10 17; CLIO 13; CPreprocess 12; libimp-eabi-ppc 11; CLCompilerLinkerDropin_V10 11). 97 are
   still reached by lifted code (49 of them COMMONs).
8. **COMMON names (80 placeholders).** The `common` unit has 80 placeholder-named COMMONs, 49 of them used by lifted
   code. mwld orders 4-byte COMMONs by `mwld_hash` of the name, so each real name can be checked against its
   neighbours (AGENTS.md: "COMMON hash order checks a name"), and Wii's data publics name many. A COMMON the original
   lacks moves the whole tail.
9. **Cuts that are not 8-aligned.** `.data`: IroTransform (0x5aa35c), CExpr2, CFunc, StructMoves, ConstantPropagation
   (and NewHand, IroUtil, IroFlowgraph, IroRangePropagation, which link: an object's `.data` can start 4-aligned, so
   this is a hint, not proof). `.bss`: MsDos, CodeGen (0x5dec22), CPrep (0x5ded92), Peephole (0x5e0ecb). The
   CodeGen/CPrep one is wrong: Pro7's CPrep `static short exprtk` lies in CodeGen's range, with 98 bytes of `lbl_`
   fragments.
10. **Jump tables follow the code.** Every jump-table difference left is a function that is inexact (or a 1.x body
   with other switches: ValueNumbering 1684 bytes against 2.6's 20, ConstantPropagation none against 272): nothing
   to fix in the data.

## Order of work

The data work that does not wait for code, smallest first: StackFrame's two literals, IroPropagate's and
IroTransform's dump strings, the `extern`-only statics (finding 5), the three wrong cuts (IroDump/InstrSelection,
Exceptions/DumpIR, VectorArraysToRegs/AltiVec) and the CodeGen/CPrep `.bss` cut, the dead functions of finding 6, then
the naming pass of findings 7 and 8 (tooling first: name statics from the debug objects). The rest of the C and D
units' data comes with their rewrites, which the code audit orders.

## Index

Sorted by data grade, worst first. Code: the code audit's grade. Exact: now. `.data`/`.bss`: the original's bytes in
the unit's ranges. Placeholders: placeholder names in those ranges. Held: globals kept COMMON for lifted code that
2.6 has as the unit's statics.

| Unit | Code | Data | Exact | .data | .bss | Placeholders | Held | What the data needs |
|---|---|---|---|---|---|---|---|---|
| backend/CMachine.c | D | D | 36/48 | 3544 | 16 | 172 | 4 | 1656 bytes short; ~90 `data_*` fragments of Pro7's `stvector*`/`stVUC_*` type objects and HashNameNodes, a block of string literals as global arrays, ten `static double` literal stand-ins |
| backend/DWARF.c | D | D | 13/38 | 512 | 440 | 19 | 7 | 116 bytes short (60 jump tables), the producer string `MW EABI PPC C-Compiler` missing, 19 placeholders |
| backend/InlineAsmPPC.c | D | D | 13/26 | 1376 | 8 | 39 |  | 376 bytes short, 9 literals missing (`eieio`, `fcmpo`, `volatile`, `warn_def_reserved_reg` ...), jump tables 568 of 908, 8 bytes of .bss with no definition, 39 placeholders |
| backend/MachineSimulationAltiVec.c | D | D | 4/7 | 28 | 392 | 15 |  | its 4.4 KB of tables sit in VectorArraysToRegs' range; its .bss is 15 `data_*` externs nothing defines |
| backend/TOC.c | D | D | 17/29 | 1960 | 0 | 14 |  | 628 bytes short (92 of them jump tables of inexact functions), a generated-data block, 14 placeholders |
| optimizer/ConstantPropagation.c | D | D | 1/4 | 276 | 16 | 1 | 4 | compiled object has no .data; 2.6 has a 272-byte jump table; .data range starts at 0x5d19ac (not 8-aligned) |
| optimizer/FunctionCalls.c | D | D | 1/5 | 16 | 80 | 2 |  | .bss is two unnamed statics (80 bytes) the source does not define at all |
| optimizer/ValueNumbering.c | D | D | 1/14 | 40 | 1912 | 12 | 2 | the 1.x object: 1684 bytes of jump tables 2.6 lacks (2.6 has 20), .bss 28 of 1912 |
| backend/BE_elf.c | C | C | 15/21 | 112 | 144 | 2 | 5 | 12 bytes short; the globals held for lifted code are larger than its .bss (393 against 144); `file_name` belongs to another unit's .bss |
| backend/BE_symbol.c | C | C | 13/18 | 20 | 8 | 1 |  | its one static (`data_0055da80`, 8 bytes) is only declared `extern` |
| backend/CodeGen.c | B | C | 36/43 | 2728 | 368 | 17 | 14 | the CodeGen/CPrep .bss cut (0x5ded92, unaligned) is wrong: 98 bytes of `lbl_` fragments and CPrep's `exprtk` in its range; jump tables 40 short (CollapseVectorExpression) |
| backend/ELF_Endian.c | C | C | 7/8 | 84 | 8 | 2 |  | its one static (`DAT_005805e0`, the error handler) is only declared `extern` |
| backend/Exceptions.c | D | C | 5/8 | 2600 | 56 | 79 | 5 | its own data is the first 216 bytes (they match); the 2384 after them, from 0x5bc698, are Pro7's DumpIR.c literals (the `EAT_*` and ENode spellings): DumpIR has no .data range; 79 `lbl_` |
| backend/InlineAsm.c | C | C | 19/23 | 240 | 1032 | 2 | 1 | 124 bytes short (Pro7's dump literals ` imm(%ld)`, ` obj(%s+%ld)` ...); its .bss is 1032 bytes where the source defines `ASMstmtnb` (4) |
| backend/InstrSelection.c | C | C | 41/83 | 1496 | 0 | 30 |  | its range holds IroDump's first 464 bytes (see below); without them the sizes agree (1032); 30 `lbl_`; `instrSelectionFileName` duplicates the assert literal |
| backend/ObjGen_PPC_EABI.c | C | C | 36/51 | 616 | 64 | 12 | 17 | 84 bytes short (80 of them jump tables of inexact functions), two generated-data blocks, 19 `data_` identifiers in the source |
| backend/Operands.c | D | C | 13/30 | 632 | 0 | 16 |  | 36 bytes short, `data_*` fragments for literals |
| backend/PCodeAssembly.c | D | C | 1/7 | 2248 | 8 | 9 | 1 | 152 bytes over, all jump tables of the inexact insertlongbranches; a generated-data block |
| backend/PCodeInfo.c | C | C | 10/11 | 1232 | 0 | 15 |  | 772 bytes short: vformatpcode's literals (the 1.x vformatpcode, 11.7%, lacks Pro7's formats) |
| backend/Peephole.c | B | C | 44/49 | 0 | 2445 | 1 | 3 | 541 bytes of .bss (one unnamed static at 0x5e0ecb, an odd address) nothing defines |
| backend/SFPE_PPC_EABI.c | C | C | 1/8 | 100 | 0 | 2 |  | 20 bytes short; the compare-mask table differs |
| backend/SpillCode.c | B | C | 3/11 | 384 | 24 | 8 | 3 | 260 bytes short (unnamed tables from 0x5d7cfc), 16 bytes of .bss unnamed |
| backend/StackFrame.c | B | C | 26/41 | 132 | 256 | 2 | 43 | the literals are `_savevr_%d`/`_restvr_%d` where 2.6 has `_savevr%d`/`_restvr%d`: its only data difference |
| driver/MsDos.c | C | C | 58/60 | 33080 | 524 | 5 |  | its own 312 bytes match; the 32 KB of zeros before them (0x599418) have no owner and no reference (config.json `data_head`) |
| frontend/CExpr2.c | B | C | 81/85 | 1372 | 192 | 12 | 7 | 144 bytes of non-table data short, first difference in the `*_operand_arg` structs (0x5b0a04) |
| frontend/CFunc.c | C | C | 49/51 | 484 | 48 | 6 | 10 | 6 bytes of .bss (`data_005dcc80`) only declared `extern` |
| frontend/CInline.c | C | C | 53/57 | 4840 | 144 | 28 | 24 | 16 bytes of .bss in `data_*` fragments; 28 placeholders |
| frontend/CObjC.c | C | C | 52/55 | 848 | 8 | 4 |  | 28 bytes short, `L_OBJC_CLASS_REFERENCES_%ld` and `super` missing, an extra `CObjC.c` literal |
| frontend/CPrec.c | D | C | 50/84 | 952 | 80 | 14 | 17 | 36 bytes short (28 of them jump tables of inexact functions) |
| frontend/CPrep.c | B | C | 89/92 | 5528 | 2006 | 5 | 42 | 220 bytes short: 8 literals no code references (`compiler`, `exestr`, `linker`, `user`; `(void)`, `(...)`, `operator new`, `operator delete`), so dead functions 2.6 kept; `exprtk` (Pro7's static) lies in CodeGen's .bss range |
| msl/locale.c | - | C | -/- | 60 | 0 | 2 |  | the compiled object has 48 bytes of .bss the original lacks |
| optimizer/Alias.c | C | C | 10/21 | 656 | 4008 | 5 | 4 | 8 bytes short, 34 bytes differ from 0x5be0c9 |
| optimizer/CodeMotion.c | C | C | 12/19 | 32 | 8 | 2 |  | 24 bytes over (a jump table of the inexact isloopinvariant); its .bss static `DAT_00574ce8` only declared `extern` |
| optimizer/IROUseDef.c | B | C | 22/24 | 608 | 352 |  | 10 | Pro7's `static AssignmentFoldingFunction[MAXEXPR]` (305 bytes) is only declared `extern` |
| optimizer/IroPointerAnalysis.c | C | C | 187/196 | 8648 | 224 | 17 |  | 60 bytes short, a dead function's `home != NULL` assert text missing, 13 statics (224 bytes of .bss) only declared `extern` |
| optimizer/IroPropagate.c | B | C | 8/9 | 236 | 0 |  |  | one extra literal, `Found propagation at %d from %d\n`: exactly the 36 bytes over |
| optimizer/IroTransform.c | C | C | 26/31 | 3364 | 232 | 28 |  | `AssignmentOp`, `ComplementaryOp`, `ComplementaryOpLogical` (232 bytes of .bss) are only declared `extern`; an extra literal `ReverseOp For Monmin at: %d\n` (+32) |
| optimizer/StructMoves.c | C | C | 6/12 | 132 | 0 | 2 |  | 20 bytes short (16 of them a jump table of an inexact function) |
| optimizer/VectorArraysToRegs.c | C | C | 8/9 | 4424 | 24 | 29 | 6 | its own 24 bytes match; the 4400 after them are MachineSimulationAltiVec's (move the cut to 0x5d1b20) |
| backend/InterferenceGraph.c | B | B | 3/6 | 20 | 8 |  | 2 |  |
| backend/Intrinsics.c | B | B | 29/30 | 31976 | 1264 | 626 |  | bytes match; 626 placeholder names (`_registration_name_*`, `lbl_*`) for the `intrinsics[]` table |
| backend/LiveVariables.c | B | B | 3/4 | 0 | 0 |  |  | no data |
| backend/MachineSimulation7400.c | B | B | 6/7 | 3323 | 232 |  | 4 |  |
| backend/Scheduler.c | D | B | 10/11 | 32 | 66 | 8 |  | 8 `lbl_` in .bss |
| driver/Utils.c | B | B | 6/7 | 8 | 0 |  |  |  |
| frontend/CABI.c | B | B | 36/37 | 188 | 24 |  | 5 |  |
| frontend/CDecl.c | C | B | 74/78 | 744 | 0 | 4 |  |  |
| frontend/CExpr.c | B | B | 87/90 | 2248 | 0 | 2 |  |  |
| frontend/CExprConvMatch.c | B | B | 26/27 | 632 | 0 |  |  |  |
| frontend/CPrepTokenizer.c | B | B | 68/69 | 3220 | 8 |  | 1 |  |
| frontend/CTemplateClass.c | C | B | 30/31 | 196 | 0 | 1 |  |  |
| frontend/CTemplateNew.c | C | B | 24/25 | 204 | 48 | 2 | 2 |  |
| frontend/CTemplateTools.c | B | B | 42/43 | 1084 | 0 | 2 |  |  |
| optimizer/IroExprRegeneration.c | B | B | 18/21 | 356 | 88 | 3 | 2 |  |
| optimizer/IroLoop.c | C | B | 30/33 | 880 | 40 |  | 9 | 9 statics held global for lifted code |
| optimizer/IroUnrollLoop.c | C | B | 20/22 | 1996 | 0 |  |  |  |
| optimizer/LoopDetection.c | C | B | 13/17 | 16 | 16 | 1 | 4 |  |
| optimizer/LoopOptimization.c | B | B | 13/17 | 20 | 16 |  | 3 |  |
| optimizer/UseDefChains.c | C | B | 7/13 | 0 | 0 |  |  | no data |
| backend/BitVectors.c | B | A | 8/8 | 0 | 0 |  |  |  |
| backend/Coloring.c | A | A | 5/5 | 16 | 2 |  |  |  |
| backend/DumpIR.c | B | A | 2/2 | 0 | 0 |  |  |  |
| backend/FuncLevelAsmPPC.c | A | A | 4/4 | 200 | 8 |  |  |  |
| backend/GCCInlineAsm.c | B | A | 3/3 | 44 | 0 |  |  |  |
| backend/InlineAsmMnemonicsPPC.c | C | A | 2/2 | 32241 | 0 |  |  |  |
| backend/InlineAsmRegisters.c | C | A | 3/3 | 0 | 256 | 7 |  |  |
| backend/InlineAsmRegistersPPC.c | D | A | 4/4 | 12200 | 568 | 22 |  |  |
| backend/MachineSimulation601.c | A | A | 6/6 | 2854 | 48 | 12 |  |  |
| backend/MachineSimulation603.c | A | A | 6/6 | 2854 | 120 | 27 |  |  |
| backend/MachineSimulation603e.c | A | A | 6/6 | 2960 | 120 | 26 |  |  |
| backend/MachineSimulation604.c | A | A | 6/6 | 2854 | 224 | 33 |  |  |
| backend/MachineSimulation750.c | A | A | 6/6 | 2854 | 144 | 31 |  |  |
| backend/MachineSimulation821.c | A | A | 6/6 | 2854 | 112 | 21 |  |  |
| backend/OpcodeInfo.c | - | A | -/- | 12583 | 0 |  |  |  |
| backend/PCode.c | B | A | 17/17 | 0 | 6 |  |  |  |
| backend/PCodeListing.c | C | A | 5/5 | 680 | 0 |  |  |  |
| backend/PCodeUtilities.c | C | A | 18/18 | 44 | 0 |  |  |  |
| backend/PPCError.c | A | A | 4/4 | 12 | 0 |  |  |  |
| backend/RegisterInfo.c | B | A | 23/23 | 368 | 34 | 2 |  |  |
| backend/Registers.c | B | A | 8/8 | 0 | 0 |  |  |  |
| backend/ScanFloatX86.c | A | A | 1/1 | 0 | 0 |  |  |  |
| backend/Switch.c | B | A | 9/9 | 20 | 60 | 8 |  |  |
| driver/Arguments.c | A | A | 25/25 | 404 | 572 | 5 |  |  |
| driver/CCompiler.c | B | A | 4/4 | 192 | 192 | 32 |  |  |
| driver/CLAccessPaths.c | A | A | 18/18 | 124 | 0 |  |  |  |
| driver/CLBrowser.c | C | A | 9/9 | 136 | 0 | 1 |  |  |
| driver/CLCompilerLinkerDropin_V10.cpp | C | A | 22/22 | 1656 | 0 | 11 |  |  |
| driver/CLDependencies.c | B | A | 20/20 | 204 | 8 | 2 |  |  |
| driver/CLDropinCallbacks_V10.cpp | C | A | 47/47 | 1734 | 0 | 17 |  |  |
| driver/CLErrors.c | A | A | 9/9 | 56 | 512 | 1 |  |  |
| driver/CLFileOps.c | C | A | 16/16 | 564 | 0 |  |  |  |
| driver/CLFiles.c | C | A | 14/14 | 60 | 0 |  |  |  |
| driver/CLIO.c | C | A | 39/39 | 756 | 1306 | 13 |  |  |
| driver/CLIncludeFileCache.c | C | A | 8/8 | 20 | 12 |  |  |  |
| driver/CLLicenses.c | C | A | 10/10 | 204 | 268 | 5 |  |  |
| driver/CLLoadAndCache.c | B | A | 3/3 | 20 | 0 |  |  |  |
| driver/CLMain.c | B | A | 28/28 | 5560 | 32 | 15 |  |  |
| driver/CLOverlays.c | A | A | 17/17 | 144 | 0 |  |  |  |
| driver/CLParserCallbacks_v1.cpp | C | A | 11/11 | 56 | 0 |  |  |  |
| driver/CLPluginRequests.cpp | C | A | 11/11 | 104 | 0 |  |  |  |
| driver/CLPlugins.c | B | A | 40/40 | 2036 | 51 | 10 |  |  |
| driver/CLPrefs.c | B | A | 5/5 | 88 | 4 | 1 |  |  |
| driver/CLProj.c | A | A | 2/2 | 32 | 0 | 1 |  |  |
| driver/CLSegs.c | A | A | 8/8 | 76 | 0 |  |  |  |
| driver/CLStaticMain.c | A | A | 1/1 | 228 | 0 |  |  |  |
| driver/CLTarg.c | B | A | 4/4 | 348 | 0 |  |  |  |
| driver/CLToolExec.c | B | A | 7/7 | 180 | 0 |  |  |  |
| driver/CLWriteObjectFile.c | B | A | 2/2 | 72 | 0 |  |  |  |
| driver/COSToolsCLT.c | B | A | 24/24 | 400 | 0 |  |  |  |
| driver/CWParserPluginsPrivate.cpp | B | A | 16/16 | 0 | 0 |  |  |  |
| driver/CWPluginsPrivate.cpp | B | A | 29/29 | 0 | 0 |  |  |  |
| driver/CWSecretPluginCallbacks.c | B | A | 4/4 | 0 | 0 |  |  |  |
| driver/ClientGlue.c | B | A | 10/10 | 52 | 0 |  |  |  |
| driver/CodeGenOptPPC.c | B | A | 4/4 | 508 | 2 |  |  |  |
| driver/DropInCompilerLinkerPrivate.cpp | B | A | 18/18 | 0 | 0 |  |  |  |
| driver/ELF_Lib_Import.c | C | A | 21/21 | 128 | 276 | 15 |  |  |
| driver/ErrMgr.c | A | A | 2/2 | 1052 | 0 |  |  |  |
| driver/FileHandles.c | A | A | 5/5 | 0 | 0 |  |  |  |
| driver/Files.c | A | A | 22/22 | 40 | 0 |  |  |  |
| driver/Generic.c | A | A | 12/12 | 44 | 1300 |  |  |  |
| driver/Help.c | B | A | 18/18 | 2824 | 320 | 6 |  |  |
| driver/IO.c | B | A | 2/2 | 212 | 0 |  |  |  |
| driver/LicenseImports.c | C | A | 3/3 | 0 | 0 |  |  |  |
| driver/LinkErrorsPPC_EABI.c | C | A | 5/5 | 116 | 2048 | 1 |  |  |
| driver/LowMem.c | A | A | 2/2 | 0 | 2 |  |  |  |
| driver/MacFileTypes.c | A | A | 6/6 | 36 | 4 | 1 |  |  |
| driver/MacSpecs.c | A | A | 15/15 | 132 | 1872 |  |  |  |
| driver/MemUtils.c | A | A | 5/5 | 116 | 0 |  |  |  |
| driver/Memory.c | B | A | 15/15 | 8 | 2 |  |  |  |
| driver/OptimizerHelpers.c | B | A | 3/3 | 692 | 0 |  |  |  |
| driver/Option.c | C | A | 38/38 | 332 | 152 | 4 |  |  |
| driver/Parameter.c | B | A | 55/55 | 704 | 0 |  |  |  |
| driver/ParserErrors.c | A | A | 13/13 | 24 | 1024 |  |  |  |
| driver/ParserFace.c | B | A | 14/14 | 184 | 0 | 5 |  |  |
| driver/ParserGlue-eabi-ppc-cc.c | B | A | 4/4 | 39516 | 0 |  |  |  |
| driver/ParserHelpers-cc.c | B | A | 5/5 | 364 | 0 |  |  |  |
| driver/ParserHelpers.c | B | A | 12/12 | 64 | 260 |  |  |  |
| driver/Projects.c | B | A | 7/7 | 272 | 0 |  |  |  |
| driver/ResourceStrings.c | A | A | 2/2 | 128 | 448 | 4 |  |  |
| driver/Resources.c | B | A | 29/29 | 560 | 20 |  |  |  |
| driver/StaticParserGlue.c | A | A | 2/2 | 3892 | 0 | 5 |  |  |
| driver/StdTargetWarningHelpers-cc.c | A | A | 2/2 | 800 | 0 |  |  |  |
| driver/StringExtras.c | A | A | 4/4 | 0 | 0 |  |  |  |
| driver/StringUtils.c | B | A | 8/8 | 48 | 256 |  |  |  |
| driver/TargetOptimizerHelpers-eabi-ppc.c | C | A | 3/3 | 240 | 0 | 2 |  |  |
| driver/Targets.c | B | A | 4/4 | 112 | 0 |  |  |  |
| driver/TextUtils.c | B | A | 4/4 | 48 | 0 |  |  |  |
| driver/ToolHelpers-cc.c | B | A | 2/2 | 76 | 0 |  |  |  |
| driver/ToolHelpers.c | C | A | 4/4 | 72 | 1 | 1 |  |  |
| driver/WarningHelpers.c | B | A | 2/2 | 204 | 0 |  |  |  |
| driver/cc-eabi-ppc-mw.c | A | A | 5/5 | 0 | 0 |  |  |  |
| driver/cc-eabi-ppc.c | A | A | 17/17 | 32964 | 0 | 22 |  |  |
| driver/libimp-eabi-ppc.c | A | A | 11/11 | 492 | 0 | 15 |  |  |
| frontend/CBrowse.c | B | A | 21/21 | 16 | 52 | 7 |  |  |
| frontend/CClass.c | B | A | 67/67 | 536 | 74 | 1 |  |  |
| frontend/CError.c | C | A | 40/40 | 1004 | 10 |  |  |  |
| frontend/CException.c | C | A | 41/41 | 1192 | 26 | 1 |  |  |
| frontend/CInit.c | C | A | 50/50 | 868 | 26 |  |  |  |
| frontend/CInt64.c | C | A | 46/46 | 116 | 265 | 7 |  |  |
| frontend/CMangler.c | B | A | 18/18 | 1624 | 0 |  |  |  |
| frontend/CParser.c | C | A | 89/89 | 1544 | 20 |  |  |  |
| frontend/CPreprocess.c | B | A | 7/7 | 2848 | 8 | 12 |  |  |
| frontend/CRTTI.c | B | A | 14/14 | 296 | 8 |  |  |  |
| frontend/CSOM.c | C | A | 37/37 | 596 | 16 |  |  |  |
| frontend/CScope.c | A | A | 68/68 | 216 | 22 |  |  |  |
| frontend/CTemplateFunc.c | B | A | 24/24 | 240 | 8 |  |  |  |
| frontend/CompilerTools.c | B | A | 48/48 | 28 | 106 | 26 |  |  |
| frontend/Unmangle.c | C | A | 13/13 | 2228 | 0 |  |  |  |
| msl/ThreadLocalData.c | B | A | 5/5 | 48 | 0 |  |  |  |
| msl/abort_exit.c | A | A | 4/4 | 20 | 512 | 3 |  |  |
| msl/alloc_g.c | A | A | 20/20 | 16 | 8 | 2 |  |  |
| msl/ansi_files.c | A | A | 5/5 | 336 | 768 | 3 |  |  |
| msl/ansifp_x86.c | A | A | 3/3 | 480 | 4 |  |  |  |
| msl/arith.c | A | A | 7/7 | 0 | 0 |  |  |  |
| msl/assert.c | A | A | 1/1 | 42 | 0 |  |  |  |
| msl/buffer_io_g.c | A | A | 6/6 | 0 | 0 |  |  |  |
| msl/char_io.c | A | A | 5/5 | 0 | 0 |  |  |  |
| msl/critical_regions_win32.c | C | A | 4/4 | 0 | 120 | 1 |  |  |
| msl/ctype.c | A | A | 3/3 | 768 | 0 |  |  |  |
| msl/direct_io_g.c | A | A | 2/2 | 0 | 0 |  |  |  |
| msl/extras.c | A | A | 2/2 | 84 | 0 |  |  |  |
| msl/fcntl_win32_g.c | A | A | 1/1 | 0 | 0 |  |  |  |
| msl/file_io.c | A | A | 5/5 | 4 | 0 | 1 |  |  |
| msl/file_io_win32_g.c | A | A | 7/7 | 20 | 280 | 2 |  |  |
| msl/file_pos_g.c | A | A | 4/4 | 0 | 0 |  |  |  |
| msl/float.c | - | A | -/- | 108 | 0 | 1 |  |  |
| msl/mbstring.c | A | A | 3/3 | 0 | 0 |  |  |  |
| msl/mem.c | A | A | 4/4 | 0 | 0 |  |  |  |
| msl/misc_io.c | A | A | 2/2 | 12 | 0 |  |  |  |
| msl/pool_alloc_win32_g.c | A | A | 2/2 | 0 | 0 |  |  |  |
| msl/printf_g.c | A | A | 15/15 | 752 | 0 |  |  |  |
| msl/qsort_g.c | A | A | 1/1 | 0 | 0 |  |  |  |
| msl/scanf.c | A | A | 6/6 | 360 | 0 |  |  |  |
| msl/setjmp_x86.c | A | A | 2/2 | 0 | 0 |  |  |  |
| msl/signal.c | A | A | 2/2 | 0 | 24 | 1 |  |  |
| msl/startup_win32.c | A | A | 2/2 | 8 | 4 | 1 |  |  |
| msl/string.c | A | A | 11/11 | 224 | 32 | 1 |  |  |
| msl/string_io.c | A | A | 3/3 | 4 | 0 | 1 |  |  |
| msl/strtold.c | A | A | 2/2 | 72 | 0 |  |  |  |
| msl/strtoul.c | C | A | 4/4 | 128 | 0 |  |  |  |
| msl/sysenv_win32.c | A | A | 1/1 | 0 | 0 |  |  |  |
| msl/time_g.c | A | A | 17/17 | 1004 | 36 | 1 |  |  |
| msl/time_win32.c | B | A | 4/4 | 96 | 0 |  |  |  |
| msl/unistd_win32.c | A | A | 4/4 | 24 | 0 |  |  |  |
| msl/wchar_io.c | A | A | 4/4 | 12 | 0 |  |  |  |
| msl/wcstold.c | A | A | 1/1 | 72 | 0 |  |  |  |
| msl/wcstoul.c | A | A | 2/2 | 128 | 0 |  |  |  |
| msl/wctype.c | A | A | 2/2 | 1624 | 0 |  |  |  |
| msl/wscanf.c | A | A | 4/4 | 8508 | 0 |  |  |  |
| optimizer/AddPropagation.c | B | A | 4/4 | 96 | 0 | 1 |  |  |
| optimizer/CIRTransform.c | C | A | 3/3 | 152 | 4 | 1 |  |  |
| optimizer/COptimizer.c | B | A | 12/12 | 480 | 331 | 2 |  |  |
| optimizer/CopyPropagation.c | B | A | 9/9 | 80 | 20 | 2 |  |  |
| optimizer/GlobalOptimizer.c | D | A | 3/3 | 412 | 0 |  |  |  |
| optimizer/IrOptimizer.c | A | A | 6/6 | 1052 | 2 |  |  |  |
| optimizer/IroBitVect.c | B | A | 12/12 | 16 | 0 |  |  |  |
| optimizer/IroCSE.c | A | A | 18/18 | 252 | 1 |  |  |  |
| optimizer/IroDump.c | B | A | 8/8 | 1992 | 312 | 75 |  | links, but its .data starts at 0x5a9838, not 0x5a9a08 (see below) |
| optimizer/IroEmptyLoop.c | A | A | 7/7 | 2432 | 0 |  |  |  |
| optimizer/IroEval.c | B | A | 14/14 | 384 | 77 | 76 |  |  |
| optimizer/IroFlowgraph.c | B | A | 6/6 | 172 | 0 |  |  |  |
| optimizer/IroJump.c | A | A | 6/6 | 236 | 0 |  |  |  |
| optimizer/IroLinearForm.c | C | A | 34/34 | 1548 | 12 |  |  |  |
| optimizer/IroMalloc.c | A | A | 12/12 | 16 | 56 | 14 |  |  |
| optimizer/IroRangePropagation.c | A | A | 5/5 | 572 | 8 | 2 |  |  |
| optimizer/IroSubable.c | A | A | 2/2 | 12 | 77 | 75 |  |  |
| optimizer/IroUtil.c | A | A | 55/55 | 1420 | 20 | 1 |  |  |
| optimizer/IroVars.c | A | A | 23/23 | 464 | 12 |  |  |  |
| optimizer/LoadDeletion.c | C | A | 4/4 | 70 | 0 |  |  |  |
| optimizer/StrengthReduction.c | B | A | 18/18 | 20 | 0 |  |  |  |
| runtime/ExceptionX86.cpp | B | A | 26/26 | 460 | 0 | 1 |  |  |
| runtime/LongLongx86.c | A | A | 5/5 | 0 | 0 |  |  |  |
| runtime/NMWException.cpp | A | A | 5/5 | 8 | 0 | 2 |  |  |
| runtime/NewHand.cpp | B | A | 1/1 | 92 | 0 | 1 |  |  |
| runtime/arrcondes.cpp | A | A | 1/1 | 0 | 0 |  |  |  |
| runtime/delarrop.cpp | A | A | 1/1 | 0 | 0 |  |  |  |
| runtime/delop.cpp | A | A | 1/1 | 0 | 0 |  |  |  |
| runtime/exchand.cpp | B | A | 3/3 | 53 | 5 | 3 |  |  |
| runtime/globdest.c | A | A | 1/1 | 0 | 0 |  |  |  |
| runtime/maincrt.c | A | A | 1/1 | 12 | 0 |  |  |  |
| runtime/newop.cpp | B | A | 4/4 | 80 | 0 |  |  |  |
| runtime/runinit.c | A | A | 1/1 | 0 | 0 |  |  |  |
| runtime/setupargs.c | B | A | 3/3 | 16 | 0 |  |  |  |
| runtime/stackall.c | A | A | 2/2 | 0 | 0 |  |  |  |
| runtime/throwsbad.cpp | - | A | -/- | 1 | 0 |  |  |  |
