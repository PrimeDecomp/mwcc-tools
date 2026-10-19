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

`config/GC_1_3_2/config.json` names `"source_tree": "gc2"` and `"source_version": "GC_2_7"`. The build compiles
`gc2/src` with `-DVERSION` of 2.7 (`gc2/include/version.h` has no 1.3.2), against a split of 1.3.2 in
`gc2/config/GC_1_3_2`. 130 of its 242 units link into the original's SHA-1, and the others link as split. The split was
made from the bundle's 2.7 configuration:

1. `mwcc-tools remap 2.7 1.3.2 --symbols`, run in a scratch root that holds `gc2/config/GC_2_7` as `config/GC_2_7`.
2. More names, chained through our own pairing: a symbol that `config/GC_2_7` and `config/GC_1_3_2` both name pairs a
   2.7 address with a 1.3.2 address, and the bundle's 2.7 name at that address names the 1.3.2 one.
3. `mwcc-tools derive-splits GC_1_3_2 --reference GC_2_7`, still in the scratch root.
4. Unit by unit, where both splits list the same number of symbols in a section (and of the same sizes, outside code),
   the 1.3.2 symbols take the 2.7 names in address order.
5. Call targets and absolute operands of the paired functions vote for more address pairs, which renamed 1591
   symbols.
6. Link-and-diff pairing, unit by unit: link one excluded unit's compiled object on top of the accepted ones, with a
   stub object that defines each name the link lacks as an absolute address far outside the image. Where the result's
   bytes hold a stub address, the original's bytes at the same place give the real one. A name found that way renames
   the symbol at that address, or splits the symbol that contains it (the shortened symbol ends at its last
   instruction, not at the zero padding). 33 names, four of them import thunks (`_lp_checkout`, `_lp_checkin`,
   `_lp_errstring`, `_GetFileVersionInfoSizeA@8`) and one function at an unaligned address: `split` keeps a function
   that its section's alignment cannot place in the piece before it.
7. Split boundaries: where one unit's compiled `.data` or `.bss` is longer than its split range by what the next unit's
   is shorter (or has none), the boundary moves to the compiled size (`ctype.c`/`ansifp_x86.c`,
   `LinkErrorsPPC_EABI.c`/`DateTimeUtils.c`, `CInt64.c`/`Intrinsics.c`, `COptimizer.c`/`CMangler.c`,
   `CException.c`/`CSOM.c`, `string.c`/`ansi_files.c`, `ELF_Lib_Import.c`/`MacFileTypes.c`).
8. `mwcc-tools relink-check GC_1_3_2 --write`. It retries the units it rejects until no more link, since a unit can
   need one accepted after it.

Of the 210 Matching candidates, 80 do not link. Some still lack names or split boundaries; others compile to different
code or data. LiveVariables and CBrowse,
for instance, read fields of `copts` 4 bytes off, since the sources are built with 2.7's `VERSION`.
