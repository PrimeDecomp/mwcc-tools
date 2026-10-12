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
(`fn_XXXXXXXX`, named by address, among them three of the four functions the oracle hooks). A function carried that way
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

## What this means for the oracle

**Register allocator**

* `Coloring_AllocateRegisters` (`_fn_00506cd0`, at 0x508530) is the same code as in 1.3 and 1.3.2, and so are the
  interference graph and the spill code.
* The changes near it are in `CodeGen.c`: `allocate_saved_gprs`, `allocate_saved_vrs`, `bind_object_register`,
  `CodeGen_EnumerateArgumentRegisters`. There are also three small `Registers.c` helpers (`fn_004ced60`, `fn_004cfb10`,
  `fn_004cfb90`). These choose which callee-saved registers a function uses and how arguments are bound. They are the
  first place to look when 2.7 assigns registers differently from 1.3.2.
* The PCode opcode numbers are those of 1.3.2: the assembler's mnemonic table maps all 1058 mnemonics to the same
  `PC_` numbers. `tools/oracle.py`'s opcode names therefore apply.

**Inliner**

| function | 2.7 symbol | address | relation to 1.3.2 |
| --- | --- | ---: | --- |
| `can_inline` | `_fn_00560c80` | 0x560C80 | the same code as 1.3.2's `_fn_0055ed70` |
| the body-cost walk | `_fn_0055e540` | 0x560FB0 | the same code |
| `inline_call_expression` | `_fn_00561260` | 0x561260 | changed in one place only |

  The one change in `inline_call_expression` is that it reads `CInlineInfo`'s `kind` at 0xa4: 0xa0 in 1.3 (where the
  sources put it), 0xa2 in 1.3.2. `remap.py` does not find the function, because of that operand. It has the same size
  as 1.3.2's and the same instructions (a mnemonic comparison gives a ratio of 1.0).

**Options (`copts`, at 0x5EF092)**

* Four one-byte fields were added between offsets 0x51 and 0x68. Fields from 0x53 on moved by one to four bytes, and
  everything from 0x68 on moved by four.
* So, against 1.3.2, the fields the inliner reads moved as follows:

  | field | 1.3.2 | 2.7 |
  | --- | ---: | ---: |
  | `inlineLimit` | 0x68 | 0x6c |
  | `inlineAuto` | 0x72 | 0x76 |
  | `alwaysinline` | 0xd8 | 0xdc |

* These were found by pairing the operands of the functions matched between the two versions that fall in `copts`'s
  range (`remap.data_pairs`).
* Their values were checked through the oracle: `-inline level=5` gives `limit=5`, and `#pragma always_inline on`
  gives `always=1`.
* The globals `data_00582468` (the inline depth, at 0x5E3B4C) and `data_005dfb68` (the cost threshold, at 0x5E3AE0)
  are carried.

## Status

* `python tools/corpus.py --version GC_2_7 --canaries …`: 2313/2313 identical.
* `python tools/corpus.py --version GC_2_7 --prime path/to/echoes`: 729/729 identical. Metroid Prime 2: Echoes builds
  most of its units with 2.7. `--prime` takes the plain `mwcc` rules whose `mw_version` is `GC/2.7`.
