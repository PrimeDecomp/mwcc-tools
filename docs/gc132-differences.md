# GC 1.3.2 against GC 1.3

`config/GC_1_3_2` is the split and relink of the 1.3.2 executable (build number of mwcceppc.exe 1.3.2, `VERSION_GC_1_3_2` = 6).
Its symbols were carried over from 1.3 by relocation-masked code matching (`mwcc-tools remap`): 3585 of the
4055 functions of 1.3 are found again unchanged in size and shape, 3288 of 3309 bindings of source files to addresses are
carried, 470 functions differ or are not found.

The 470, by the 1.3 source file they came from (functions, bytes):

| file | functions | bytes |
| --- | ---: | ---: |
| not bound to a source file in 1.3 | 116 | 170581 |
| src/optimizer/IroPointerAnalysis.c | 42 | 28378 |
| src/frontend/CPrec.c | 23 | 14576 |
| src/frontend/CPrep.c | 16 | 13485 |
| src/optimizer/IroTransform.c | 5 | 12587 |
| src/optimizer/IrOptimizer.c | 15 | 12267 |
| src/backend/InstrSelection.c | 23 | 11246 |
| src/backend/InlineAsmPPC.c | 9 | 9514 |
| src/backend/Operands.c | 14 | 8353 |
| src/backend/CMachine.c | 17 | 7819 |
| src/backend/PCodeAssembly.c | 2 | 5735 |
| src/optimizer/IroLoop.c | 6 | 5513 |
| src/frontend/CParser.c | 3 | 5388 |
| src/frontend/CInline.c | 12 | 5362 |
| src/backend/TOC.c | 8 | 5274 |
| src/backend/ObjGen_PPC_EABI.c | 18 | 5217 |
| src/backend/CodeGen.c | 5 | 4873 |
| the other 55 files (driver, libimp, BE_elf, flowgraph, ...) | 136 | 31378 |
| total (72 files, with the unbound ones) | 470 | 357546 |

What this means for the oracle:

* The register allocator is **not** among them: `Coloring_AllocateRegisters` (`_fn_00506cd0`), the interference graph,
  the spill code and the register file are the same code in 1.3 and 1.3.2 (`Registers.c` has one 78 byte function that
  differs). The allocation trace of the oracle is therefore comparable between the two.
* The inliner changed. `can_inline` (`_fn_0055ed70` in 1.3.2, `_can_inline` in 1.3) and `inline_call_expression`
  (`_fn_0055f350`, `_inline_call_expression`) changed; the body-cost walk (`fn_0055e540`, now at 0x55F0A0) is the same code
  moved. The options structure `copts` has three more bytes before `alwaysinline` (offset 0xd5 in 1.3, 0xd8 in 1.3.2;
  `inlineBottomUp` (`#pragma inline_bottom_up`) is at 0x72 and `inlineLimit` at 0x68 in both; `copts` is at 0x5E90E6 in 1.3.2 and the three are
  read at 0x5E91BE, 0x5E9158 and 0x5E914E by `fn_0055ed70`).
* The pointer analysis (`IroPointerAnalysis`) is the largest change in the optimizer, then the preprocessor and
  the parser support for precompiled headers (`CPrec`).
* Globals moved: `data_00582468` (the inline depth) and `data_005dfb68` (the cost threshold) have the names of 1.3
  and are at other addresses in 1.3.2; the oracle links to them by symbol.

`config/GC_1_3_2/symbols.txt` gives each function the name it has in 1.3 where the code is the same and `fn_XXXXXXXX`
(the 1.3 address) where it was moved and changed, so a `fn_0055e540` in 1.3.2 is at 0x55F0A0.

## Building from the GC 2.x sources

`config/GC_1_3_2/config.json` names `"source_tree": "gc2"`, and `gc2/` is mweppc's decompilation, which has 1.3.2 as
a version of its own (`VERSION_GC_1_3_2` in `gc2/include/version.h`, its differences from 2.0 under
`#if VERSION`): the build compiles `gc2/src` with 1.3.2's `VERSION` against mweppc's split and state of 1.3.2
(`gc2/config/GC_1_3_2`). The WebAssembly module is built from the same sources ([wasm.md](wasm.md)).

Before mweppc had 1.3.2 (until 92b4caa6), the build compiled the 2.7 sources with 2.7's `VERSION` against a split of
1.3.2 derived here from the 2.7 one (`mwcc-tools remap`, `derive-splits`, link-and-diff pairing, `relink-check`);
`git show 92b4caa6:docs/gc132-differences.md` has how.
