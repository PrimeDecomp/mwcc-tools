# GC 2.7 against GC 1.3.2

`config/GC_2_7` is the split and relink of the 2.7 executable (Ver 2.4.7 build 108, `VERSION_GC_2_7` = 8). It is
carried over from 1.3.2 directly. 2.6 (build 107) was later carried over from 2.7; see
[gc26-differences.md](gc26-differences.md). 2.0 to 2.5 are not built. To add one, insert it in order in
`build_config.rs` `VERSIONS` and `include/version.h`, and renumber the versions after it.

## How the configuration was made

```
mwcc-tools remap 1.3.2 2.7 --symbols
mwcc-tools derive-splits GC_2_7 --reference GC_1_3_2
mwcc-tools configure --version GC_2_7 --mode link --split-only && ninja build/GC_2_7/ok
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
  `PC_` numbers. The native oracle report uses the same opcode names.

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
  | `inlineBottomUp` | 0x72 | 0x76 |
  | `alwaysinline` | 0xd8 | 0xdc |

* These were found by pairing the operands of the functions matched between the two versions that fall in `copts`'s
  range (`remap.data_pairs`).
* Their values were checked through the oracle: `-inline level=5` gives `limit=5`, and `#pragma always_inline on`
  gives `always=1`.
* The globals `data_00582468` (the inline depth, at 0x5E3B4C) and `data_005dfb68` (the cost threshold, at 0x5E3AE0)
  are carried.

**Names from the GC 2.x decompilation**

NWPlayer123's CC0 GC 2.x decompilation (at commit `888bd76d`) names every function and most globals of the same 2.7
executable. Its function names fill [`names.txt`](oracle.md#function-names). Its names for the globals and functions
the hooks use confirm what the trace takes them to be:

| our symbol | 2.7 address | its name | the hooks' use |
| --- | ---: | --- | --- |
| `data_00582468` | 0x5E3B4C | `cinline_level` | the inliner's round (`inl.can depth=`) |
| `data_005dfb68` | 0x5E3AE0 | `inline_max_size` | the cost threshold |
| `data_005dfb66` | 0x5E3ADE | `recursive_inline` | set when the callee is the function being compiled (`--full-cost`) |
| `data_005dfb62` | 0x5E3ADA | `expanding_function` | `tp.* expanding=` |
| `data_0058245a` | 0x5E3B58 | `local_aobjects` | the inline formals' slots (`lc.arg`) |
| `data_005875b8` | 0x5EA5A4 | `FunctionName` | |
| `_fn_00507070` | 0x5088D0 | `simplifygraph` | `ra.graph`, `ra.node`, `ra.order` |
| `_fn_00506f50` | 0x5087B0 | `colorgraph` | `ra.select` |
| `_fn_00506cd0` | 0x508530 | `colorinstructions` | allocation entry |
| `_fn_004f7fb0` | 0x4F9800 | `compute_frame_sizes` | `fr.*` |
| `_fn_0055e600` | 0x561070 | `CInline_EstimateSizeOfExpr` | the body cost |
| `_fn_0050f240` | 0x55CEE0 | `CInline_ObjectAddrRef` | `tp.emit` |
| `_fn_005114e0` | 0x55F000 | `CInline_ExportCheck` | a `tp.emit` caller: calls left after inlining |
| `_fn_00460780` | 0x461620 | `IRO_ScalarizeClassDataMembers` | `lc.pass`, `lc.part` |
| `_fn_004601f0` | 0x461090 | `CheckAddressConsistency` | `lc.use` |
| `_fn_00460c90` | 0x461B30 | `FunctionCallMightUseOrKillAnyAddressedVar` | `lc.call` |
| `_fn_00436390` | 0x434AF0 | `expressionstatement` | `bk.stmt` |
| `_fn_00454030` | 0x454AA0 | `IRO_SetupDump` | optimizer dump activation |

The audit against its headers (and the retail code) found the hooks' 2.7 struct offsets right: COpts, CInlineInfo,
ENode, Object, Type, Statement, PCode, IGNode, the scheduler's nodes and the frame globals. One name was wrong in
every version: the byte the hooks trace as `auto=` (`copts.inlineBottomUp`, 0x72, 0x76 in 2.7) is
`#pragma inline_bottom_up`, as the compilers' own pragma tables name it. `auto_inline` is at 0x89 (0x8d in 2.7)
and is not traced.

What its sources and notes say about the 2.x inliner, not yet checked here through the oracle:

* `-inline auto` (`auto_inline`) makes a function not declared `inline` a candidate (`FUNC_FLAGS_800`) when
  `CInline_GenFunc` finds it small: at most 30 statements other than NOPs and labels, and at most 1024 bytes of
  locals (its `src/frontend/CInline.c`, `CInline_IsSmallFunction`; Pro7's limit was 15).
* `inline_bottom_up` is on by default. It runs the cost-threshold loop (`inline_max_size`, then lowered while the
  total is over `inline_max_total_size`), which is the oracle's cost explanation.
* `CI_FuncData` keeps the back-end options a function was defined with, and deferred code generation restores them,
  so a function compiled from its saved body can be scheduled and optimized with other options than the current
  pragmas (its `include/compiler/CInline.h`).
* 2.7 differs from 2.6 in the object writer (a hashed symbol table and one symbol per DWARF entry) and in assert lines
  (its `docs/agents-notes.md`, "The version round"). Its analyses of 2.6's allocator, scheduler, inliner and frames
  apply to 2.7.

In 1.3.2 and 2.7, two `fn_` names are each given to two functions (`scope:local`): 1.3.2's `fn_004ab460` and
`fn_005114e0`, and 2.7's `fn_00453e10` and `fn_004a5b70`. One of each pair is the version's own address name, and
the other is carried from 1.3. No hook uses them. The expression-dispatch audit looks for `fn_00453e10`, but it picks
the function by size and byte hash. Any other match on the name alone would also take the other function. The `tp.emit` caller
explanation matches on `CInline_ExportCheck` from `names.txt` for that reason.

## Status

* `mwcc-tools corpus --version GC_2_7 --canaries …`: 2313/2313 identical.
* `mwcc-tools corpus --version GC_2_7 --prime path/to/echoes`: 729/729 identical. Metroid Prime 2: Echoes builds
  most of its units with 2.7. `--prime` takes the plain `mwcc` rules whose `mw_version` is `GC/2.7`.

## Building from the GC 2.x sources

`config/GC_2_7/config.json` names `"source_tree": "gc2"`. The build (not the oracle) then compiles `gc2/src` with
`gc2/config/GC_2_7` (its splits, symbols, per-source flags and compilers, and Matching from its `state.json`) and links
in `build/GC_2_7/gc2`:

```
mwcc-tools configure --version GC_2_7
ninja all_source build/GC_2_7/gc2/obj/ok
ninja
```

220 of the 253 units link from source into the original's SHA-1, and the other 33 link as split. A compiled unit
can carry more than its range, which the linker drops, so the section-size check of `link` is skipped for a source tree
and the SHA-1 is the check. `gc2/lib` comes from the `lib-2x` download (Pro 5 MSL, then the Pro 5.3 updater's Windows
and Mac groups, then three patches; checked against a SHA-256 of the tree).

`mwcc-tools relink-check GC_2_7 [--write]` finds which of the Matching units link. It tries them with trial links and
halves any group whose link does not give the original. With `--write` it stores the units that fail as
`relink_exclude` in the tree's `state.json`.

### The compiler of the sources alone

```
ninja all_source build/GC_2_7/gc2/source-only/mwcceppc.exe
mwcc-tools corpus --version GC_2_7 --canaries … --exe build/GC_2_7/gc2/source-only/mwcceppc.exe
```

`mwcc-tools source-only GC_2_7` (the ninja target runs it; it is not built by default) links every compiled object of
the tree (248, those that do not match too) and nothing of the original's. It is a port of gc2's `tools/equivalent.py
--source-only`:

* The inputs are the runtime objects of `gc2/lib`, then the objects in `gc2/config/sources.json`'s order, then import
  libraries with thunks made from `gc2/src/imports/*.def`, then resources compiled from `gc2/src/driver/mwcceppc.rc`.
  The resource compiler is a small one for the subset that script uses (its VERSIONINFO data is llvm-rc's, byte for
  byte), so neither LLVM nor gc2's Python is needed. gc2's license stand-in, `gc2/tools/lmgr_stub/lmgr326b.dll`, is
  copied beside it.
* COMDAT sections the compiler marks "no duplicates" are made "any": `LicenseImports.c`'s thunks for the license
  functions are the import library's too (6185 COMDATs).
* A name nothing defines is an error, unless only functions the original does not have refer to it. Those references
  are dropped and those functions start with `ud2` (10 functions, among them `CIRTrans_TransExpr` and
  `GetOutputFileDirectory`; the linker drops them anyway).

Its imports are the original's 66, in the definitions' order. The canary corpus gives 2313/2313 identical with it.
1.3.2 has no source-only compiler: it builds 2.7's sources, which would make it 2.7's.
With the Rust hooks linked in, the same compiler is an oracle (`ninja build/GC_2_7/gc2/source-oracle/mwcceppc.exe`).
See [oracle.md](oracle.md#gc-27-without-the-original-compiler).
