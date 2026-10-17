# GC 2.6 against GC 2.7

`config/GC_2_6` is the split and relink of the 2.6 executable. Its version resource is 2.4.7.107, and 2.7's is
2.4.7.108, so the two are consecutive builds. `VERSION_GC_2_6` is 7, and 2.7 was renumbered to 8. 2.6 is carried over
from 2.7, the closer of the two built versions. 2.0 to 2.5 are not built.

The original is at `build/compilers/GC/2.6/mwcceppc.exe` (SHA-1 `1110a7a9…`). It is the compiler of Paper Mario: The
Thousand-Year Door, and the primary version of NWPlayer123's CC0 GC 2.x decompilation.

## How the configuration was made

```
mwcc-tools remap 2.7 2.6 --symbols
mwcc-tools derive-splits GC_2_6 --reference GC_2_7
mwcc-tools configure --version GC_2_6 --mode link --split-only && ninja build/GC_2_6/ok
```

The results:

* 3640 of 3683 functions are found unchanged in size and shape, with relocated operands masked.
* 3235 of 3236 bindings are carried. The one lost is `_text_section_name`.
* The split-only relink reproduces the original (SHA-1 `1110a7a9…`) on the first try.
* The import table is 2.7's: the same three DLLs, functions and hints. So `config.json` uses `config/kernel32_27.def`.

The 43 functions remap does not find, by the source file they have in 2.7:

| file | functions | bytes |
| --- | ---: | ---: |
| src/frontend/CExpr.c | 3 | 22472 |
| src/backend/CodeGen.c | 1 | 6829 |
| src/backend/ObjGen_PPC_EABI.c | 4 | 5324 |
| src/backend/BE_symbol.c | 2 | 3332 |
| src/backend/BE_elf.c | 4 | 3023 |
| src/backend/PCodeUtilities.c | 1 | 721 |
| src/frontend/CParser.c | 1 | 119 |
| stubs of 1 to 11 bytes in 22 other units | 27 | 125 |
| total | 43 | 41945 |

The decompilation's notes say that 2.7 differs from 2.6 in the object writer (`ObjGen_PPC_EABI`, `BE_symbol`,
`BE_elf`: a hashed symbol table and one symbol per DWARF entry) and in assert lines. The table agrees with that. The
CExpr.c and CodeGen.c functions were not examined further.

Four symbols were renamed by hand after the derivation. Their bytes are unchanged, and the split-only relink still
matches.

* `inline_call_expression` is `_fn_00561260` (2.7's name) at 0x560760.
  * remap matches it, but names a carried address-named function by its new address (`fn_00560760`).
  * The hooks link against 2.7's name, so `functions.json` and `symbols.txt` use that name again.
  * With relocations masked, it is byte for byte 2.7's function. Its call and address operands are the only bytes
    that differ.
* `_class_data_parts` (0x5E0208) and `_iro_pointer_analysis` (0x5EB18A) were `lbl_` symbols, as in 2.7 before its
  rename. A global the hooks import needs a global name.

No other symbol needed this. Of the 42 `fn_` names that the Rust sources, `Hooks.c`, 2.7's `config.json` and the
guard catalogs use, only `fn_0055ed70` is missing from 2.6. Only the 1.3.2 hooks use it.

## What this means for the oracle

**The hooked code is 2.7's.** With relocations masked, every one of the 31 functions in `config.json`'s
`oracle.hooks` is byte for byte the same as 2.7's. These include the allocator (`_fn_00506cd0`), select/simplify,
the scheduler, the frame layout, the inliner (`can_inline`, the cost walk, `inline_call_expression`, dispatch), the
scalarizer and the CSE hooks. The struct offsets the hooks read are therefore 2.7's:

* `CInlineInfo`'s `kind` is at 0xa4. `inline_call_expression` has no non-relocated byte that differs from 2.7's.
* `COpts`, from the retail pragma tables (`{name*, u16 offset}`, the same table bytes in both versions):

  | field | 2.6 | 2.7 |
  | --- | ---: | ---: |
  | `inlineLimit` | 0x6c | 0x6c |
  | `inline_bottom_up` (traced as `auto=`) | 0x76 | 0x76 |
  | `auto_inline` | 0x8d | 0x8d |
  | `always_inline` | 0xdc | 0xdc |

* Checked through the 2.6 oracle, with the same records as the 2.7 oracle for the same inputs:
  * `-inline level=5` gives `limit=5`.
  * `#pragma always_inline on` gives `always=1`.
  * `#pragma inline_bottom_up off` gives `auto=0`.
  * `inl.begin` gives `kind=6` for ordinary inline functions.

* The PCode opcode numbers are 2.7's (and so 1.3.2's): the assembler's mnemonic table (stride 20, `{name*, u16 PC_op, ...}`)
  maps the same 1058 mnemonics to the same numbers.

**Data moved, and the layout did not.**

* `copts` is at 0x5EB092, 0x4000 below 2.7's 0x5EF092. Every operand of the matched functions in its range moves by
  the same amount. 2.7's extra 0x4000 bytes are its object writer's 4096-bucket hash table.
* The inliner's globals did not move. `data_00582468` is at 0x5E3B4C, `data_005dfb68` at 0x5E3AE0 and
  `data_005dfb66` at 0x5E3ADE, as in 2.7.
* `_heap_pool` (0x5DF56E) and the arena lock (0x5DF568) are also at 2.7's addresses.

**Version numbers.**

* `Version::Gc26` is the oracle version `GC_2_6` / `GC/2.6`.
* Its Rust frontend feature is `frontend-gc26`. It shares 2.7's export and link names and offsets.
* In the arena and source-node trace headers (`pv.arena.begin version=`, `pv.source.begin version=`), 2.6 is 4. 2.7
  stays 3, so traces captured before this change still parse.
* `Hooks.c`'s `VERSION >= VERSION_GC_2_7` branches are now `VERSION_GC_2_6`. The C producer also builds, and passes
  the corpus on 2.6.

**Probe guards.** The native oracle link installs guarded probes. Each guard pins a helper's bytes in the original and
in its split object. The probes and what 2.6 got:

| probe | what was added for 2.6 |
| --- | --- |
| `source_arena_guards.json` | `GC_2_6` catalog |
| `source_node_guards.json` | `GC_2_6` catalog; the statement store at 0x4F36EE |
| `call_argument_probe.rs` | `GUARDS` |
| counter probe | its hashes and patch sites |
| `runner::patch_limits` | the block-limit patch sites (merge compare at file offset 0x10025D) |

Most of these values are derived rather than written by hand:

* `mwcc-tools provenance derive-guards --version V NAME[=SIZE]...` recomputes a guard from a version's split objects
  and original. This is the computation `check_guard` compares against. It reproduces 2.7's checked-in arena,
  source-node, call-argument and scalarizer identity catalogs exactly.
* All normalized hashes equal 2.7's.
* Most relocation identities equal 2.7's. The source-node helpers' identities differ (`CFunc_ParseFuncDef`,
  `parse_statement`, `fn_00513040`, `fn_00513240`), and so does `create_temp_object`'s. The identity hashes the
  relocation targets' names and object positions, so these were rederived rather than copied.
* The six switch tables of the source-node catalog are address-named (`lbl_005D0DC4`… in 2.7). They are 0x18 lower
  in 2.6 (`lbl_005D0DAC`…), as found by pairing `parse_statement`'s operands.

**Names.** `config/GC_2_6/names.txt` is made like 2.7's, from the decompilation's 2.6 `symbols.txt` (commit
`888bd76d`):

* A name is listed at its own address, inside the `fn_` symbol that holds it.
* It names 1090 of 2.6's 1091 `fn_XXXXXXXX` symbols in 1441 rows. `fn_004889b0` stays unnamed.
* The same script reproduces 2.7's table exactly from that commit's 2.7 `symbols.txt`.

## Status

* The split-only relink is the original (`ninja build/GC_2_6/ok`).
* `mwcc-tools corpus --version GC_2_6 --canaries …`, with all traces:
  * Rust hooks: 2313/2313 identical.
  * C producer: 2313/2313 identical.
* `--oracle-root` must be an absolute path. With a relative one, both compilers fail with "Cannot find my executable"
  and every input is reported as "messages differ".
* After the renumbering, the 2.7 oracle was rebuilt and still gives 2313/2313.
* Paper Mario: The Thousand-Year Door (doldecomp/ttyd at `62131fc`): 134/134 identical with all traces.
  * `mwcc-tools corpus --version GC_2_6 --project path/to/ttyd` was used. It takes the plain `mwcc` rules whose
    `mw_version` is `GC/2.6`.
  * Without the game, dtk-template's `configure.py` writes only its bootstrap graph. So the `build.ninja` used was
    generated from `configure.py`'s own `config.libs`: one `mwcc` edge per C object with existing source, carrying
    the flags `tools/project.py`'s `c_build` gives it.
  * Of the units with sources, 134 build with GC/2.6. 133 of them compile to an object with the retail compiler.
  * `src/manager/cardmgr.c` includes an asset extracted from the disc (`assets/_mariost_icon_tex.inc`), so both
    compilers stop on it the same way.
