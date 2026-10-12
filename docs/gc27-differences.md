# GC 2.7 against GC 1.3.2

`config/GC_2_7` is the split and relink of the 2.7 executable (Ver 2.4.7 build 108, `VERSION_GC_2_7` = 7). It is
carried over from 1.3.2 directly; 2.0 to 2.6 are not built. To add one, insert it before 2.7 in `configure.py`
`VERSIONS` and `include/version.h` and renumber 2.7.

## How the configuration was made

```
python tools/remap.py 1.3.2 2.7 --symbols
python tools/derive_splits.py GC_2_7 --reference GC_1_3_2
python configure.py --version GC_2_7 --mode link --split-only && ninja build/GC_2_7/ok
```

`--symbols` also carries the 231 functions that 1.3.2's `symbols.txt` names but its `functions.json` does not list
(`fn_XXXXXXXX`, named by address). A function carried that way
is named by its address in 2.7: a name made from its 1.3.2 address can be the address of another function in 2.7. That
happened with `fn_0055ed70`, which made both of them local.

The results:

* 3473 of 3816 functions are found unchanged in size and shape, relocated operands masked; 343 differ or are not found.
* 3236 of 3288 bindings are carried. The 52 lost are mostly DWARF, ELF and object-writer data.
* The split-only relink is the original (SHA-1 `100dd3a2…`).
* The only new import is `GetSystemDefaultLangID`, so the import definitions are `config/kernel32_27.def`.

The 343, by the source file the function had in 1.3.2:

| file | functions | bytes |
| --- | ---: | ---: |
| not bound to a source file | 147 | 257109 |
| src/frontend/CExpr.c | 38 | 25737 |
| src/backend/DWARF.c | 31 | 18078 |
| src/frontend/CDecl.c | 12 | 9470 |
| src/optimizer/CodeMotion.c | 14 | 9307 |
| src/frontend/CTemplateClass.c | 14 | 7647 |
| src/frontend/CPreprocess.c | 2 | 4700 |
| src/backend/ObjGen_PPC_EABI.c | 14 | 3456 |
| src/frontend/CFunc.c | 4 | 3321 |
| src/backend/InlineAsmPPC.c | 1 | 3063 |
| src/backend/BE_elf.c | 9 | 2970 |
| src/backend/CodeGen.c | 6 | 2706 |
| src/frontend/CInit.c | 3 | 2250 |
| src/backend/InstrSelection.c | 3 | 1983 |
| src/optimizer/LoopDetection.c | 1 | 1921 |
| the other 25 files | 44 | 13516 |
| total | 343 | 367234 |
