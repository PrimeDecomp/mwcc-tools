# The linker's matching decompilation

Two linkers are built as the compiler versions are: the original is split into one object per unit and relinked with
the CodeWarrior Pro 5.3 linker, and the build checks the result against the original's SHA-1.

| config | linker | code | SHA-1 | units linked from source |
| --- | --- | ---: | --- | ---: |
| `LD_2_6` | GC 2.6 `mwldeppc.exe` (2003-07-14) | 435030 | `d419965d331b36f0350975846c8453aa1f5dfad3` | 18 of 129 |
| `LD_1_0` | GC 1.0 `mwldeppc.exe` (2000-03-21) | 346030 | `27346c8d5ad163db4964a12bbfdf711fa7aaffd3` | 37 of 123 |

```
python configure.py --version LD_2_6
ninja
```

2.6 is the one to decompile: it is the closest to 2.7 (the linker the Rust replica in `mwld/` reproduces) and stands for
2.0 to 2.6. 1.0 was the first, as its shared code was already Matching; it stays as it is.

## Which linker

The linkers up to GC 2.6 were linked by the same Metrowerks x86 linker as the compilers (PE linker version 2.50, the
same section order), and their driver, C library and runtime are the compiler's code. Found with the relocated operands
wildcarded (`tools/crossmatch.py`):

| linker | built | code | compiler code found in it | of Matching sources |
| --- | --- | ---: | ---: | ---: |
| 1.0 | 2000-03-21 | 346030 | 158 KB (1.0 compiler) | 114 KB |
| 1.1 | 2001-02-07 | 418742 | 164 KB (1.1) | |
| 1.2.5 | 2001-06-12 | 423638 | 25 KB (1.2.5) | |
| 1.3 | 2002-01-10 | 428870 | 163 KB (1.3) | |
| 1.3.2 | 2002-05-07 | 427894 | 159 KB (2.7) | 42 KB (1.3) |
| 2.0, 2.5, 2.6 | 2002-09 to 2003-07 | 435030 | 159 KB (2.7) | 42 KB (1.3) |
| 2.7 | 2004-08-13 | 532925 | 2 KB (2.7) | |

The 2.x linkers' shared code is the 2.7 compiler's, whose sources are not Matching yet (the compiler's 2.7 build links
none from source): the 18 units 2.6 links from source are mostly the runtime and MSL.

2.0, 2.5 and 2.6 are one program: 97% of the 2.6 linker's code is found unchanged in 2.0 and 2.5, and 80% in 1.3.2. Only
about 31 KB of the 1.0 linker's functions are found in 2.6: its linker code was rewritten or rebuilt since.

The 2.7 linker (3.0.4) was built and linked by a later toolchain (PE linker version 3.0, `.exc` before `.rdata`, other
code generation: `cmp eax, 0` for `test eax, eax`, a 0xCCCCCCCC-filled frame): none of the compilers this repository
downloads (Pro 4 to 6) builds its code. CodeWarrior for Windows 9.x is the candidate (archive.org
`metrowerks-code-warrior-for-windows-9.4`: the 9.2 installer and the 9.4 update; 9.4's `mwld.exe` is from 2004-07-29,
its `mwcc.exe` from 2005-01-05). It is not tried yet.

## How the configurations were made

```
python tools/crossmatch.py LD_2_6 GC_2_7 GC_1_3_2 GC_1_3
python tools/find_functions.py LD_2_6
python tools/attribute.py LD_2_6
python tools/derive_splits.py LD_2_6 --reference GC_2_7
ninja all_source build/LD_2_6/obj/ok
python tools/ghidra_functions.py LD_2_6 GHIDRA_LIST
python tools/objnames.py LD_2_6
python tools/derive_splits.py LD_2_6 --reference GC_2_7
python tools/probe_units.py LD_2_6
```

(`LD_1_0`: `crossmatch.py LD_1_0 GC_1_0 GC_1_1`, and `--reference GC_1_1`.)

- `crossmatch.py` finds the compilers' functions in the linker, the first list placing and naming them and the others
  naming what it leaves unnamed: 2.6, 1021 functions (164 KB) and 476 data bindings; 1.0, 1027 functions (164 KB) and
  421 bindings.
- `find_functions.py` finds the other functions by decoding the code: call targets, and function pointers that are not
  switch-table entries nor inside a listed function. 2.6: 418 more (262 KB, so some of them are several functions);
  1.0: 507.
- `attribute.py` gives a function the source its assertions name. The linker's own files, under `src/linker/`, in 2.6's
  link order: `ELF_linker.c`, `DumpELF.c`, `PrepDumpELF.c`, `ELF_gen.c`, `LinkerCmdFile.c`, `ELF_PPC_EABI.c`,
  `LinkSym.c`, `DumpDWARF.c`, `DWARF_line.c` (1.0: `ELF_linker.c`, `ELF_gen.c`, `DumpELF.c`, `PrepDumpELF.c`,
  `LinkerCmdFile.c`, `ELF_PPC_EABI.c`, `DumpDWARF.c`, `DWARF_line.c`). In 2.6 the name `"ELF_linker.c"` comes right after
  a 3-byte `"ELF"` with no terminator. Files without assertions (`LinkerFacePPC_EABI.c`, `DumpELFCodePPC.c`...) are not
  told apart yet: their code is in the units around them (2.6's last 70 KB is in `DWARF_line.c`), and the boundaries
  between the linker's units are the cheapest ones the assignment found, not known ones.
- `ghidra_functions.py` adds the function starts Ghidra's analysis of the program (`mwld-gc-2-6`) has and the list
  does not, inside functions without a source: 9 (Ghidra has 1271 functions, 1252 of them at the list's starts).
- `objnames.py` names the functions of a unit as its compiled object does, where the object's function is found in
  the unit's range: the split units and a unit linked from source then agree on the names between them (the sources
  call some functions by their address in the compiler, `fn_00406610`).
- `probe_units.py` links the original with one unit at a time from source: those that give the original are listed as
  Matching, and together they give it too.
- `derive_splits.py` draws the units. The runtime's exception tables (`.rdata` and `.exc`) are the reference compiler's,
  but the 1.0 linker has `operator new` before `_chkstk`, so the ranges are laid out again in the linker's unit order.
  An object no code references, between two units' objects, is the next unit's when that unit's data points to it (the
  strings and constants just before the tables that point to them: MSL's locale strings after DWARF_line.c). A
  static that only `.data` points to is the unit's whose `.data` has the pointer (ELF_gen.c's buffer after
  PrepDumpELF.c's statics, MSL's file buffers). An object of a known size (`object_sizes` in `config.json`, by its
  bound name) has one symbol, and the references into it are that symbol and an offset: LinkSym.c's table of the
  predefined symbols, which ELF_linker.c, ELF_gen.c and ELF_PPC_EABI.c read at its start, +4 and +0x16.

The shared sources are built for a linker as for its compiler version (`compiler_version`: the compilers, flags and
symbol settings of `config/GC_2_7` for 2.6, `config/GC_1_0` for 1.0), with `-DVERSION` of that version and
`-DLINKER`. 2.6 imports what the 2.7 compiler does, less `GetSystemDefaultLangID`.

## Ghidra

The decomp server has `mwld-gc-2-6` (and `mwld-gc-1-3-2`, `mwld-gc-2-7`). Its functions and data are named from
`config/LD_2_6` by the catalog script `apply-name-list`: 901 functions (103 of them ones Ghidra had not made) and 171
data labels, as `IMPORTED` names. `python tools/ghidra_names.py LD_2_6` writes its argument lists again (each chunk with
its SHA-1, which the script checks before it changes anything) after the names change.

## The linker's own units

They are built with the driver's compiler: Pro 6 (`pro53` in `config/sources.json`, which `config/GC_2_7` maps to
`pro6`), `-O4,p -inline auto,deferred -sym off -Cpp_exceptions off`. Pro 5.3 gives other code. Deferred mode emits a
file's functions in the reverse of their source order, so the assertions' line numbers decrease with the address: a
source lists its unit's functions from the highest address to the lowest.

`python tools/funcdiff.py LD_2_6 src/linker/LinkerCmdFile.c [NAME...]` compares the compiled object's functions with
the original function by function (each function's address is the `/* 0x... */` comment before it), and prints the
instructions of the ones named side by side.

The goal for the linker's own units is code that does what the original's does, not its bytes. Two lists in
`config/LD_2_6/config.json` say how a unit's source is used:

- `matching`: the unit's object is the original's bytes, and it goes into the SHA-exact `build/LD_2_6/mwldeppc.exe`.
- `equivalent`: the unit is equivalent, but not identical. It is linked from source, with the Matching ones, into a
  second executable, `build/LD_2_6/equivalent/mwldeppc.exe` (`ninja equivalent`, part of the default build), which
  is not checked against the SHA-1. Its other units' code moves where the unit's size differs, and the relink
  relocates it.

The equivalent executable is checked by what it does:

```
python tools/linker_equivalence.py build/compilers/GC/2.6 build/LD_2_6/equivalent/mwldeppc.exe OUT --runtime DIR
```

This links the same programs with the original 2.6 linker and the rebuilt one, and compares the ELF files, the link
maps, the exit statuses and the messages. The programs are `tools/linker_oracle.py`'s 14 scenarios (`cpp-exceptions`
does not apply to 2.6: the original fails it), 29 command-file scenarios, 13 S-record scenarios, 3 with debug
information, 5 archives, 43 dumps, 20 programs with dead-stripped debug information, 61 links of objects and
their file lists, 40 sweeps of the disassembler and 31 runs of the driver's command line alone:

- MEMORY with every spelling of origin and length;
- GROUP, ALIGN and BIND, section contents with file patterns, `.` and assignments;
- every operator, ADDR, SIZEOF, NEXT and symbols;
- comments, FORCEACTIVE, FORCEFILES, EXCLUDEFILES and SHORTEN_NAMES_FOR_TOR_101;
- a section bound over another, a stack over the code, and a stack and a heap that end before they start (the
  warnings after the layout);
- 13 files with errors, whose messages are compared;
- the linker-defined symbols, which the map lists: one the command file assigns twice, predefined ones it assigns
  (one of them twice), section symbols it assigns before the linker makes them, and an output section whose name has
  two dots, and a ROM image (its `_rom` symbols);
- `-srec` with and without a file name, `-sreclength` 7, 8, 9, 255 and 256, `-sreceol` mac, dos, unix and a bad
  keyword, and code at other addresses. The S-record files are compared too;
- objects compiled with `-g`, linked with `-g`, `-sym on` and neither (DWARF 1 and its line tables);
- `-library` archives: global, weak (C++) and no global symbols, names of 15, 16 and more characters, objects of
  odd sizes. The archives are compared, and each linker links a program with the original's archive;
- `-dis` dumps (`-show only,debug`, with `-relocate`, `-show all`) of objects compiled with `-g` (C types,
  modifiers, arrays, enumerations, locations; C++ classes, namespaces, templates) and of a program linked with
  `-g`. The dumps' text is compared;
- `-dis` code dumps: `-show only,code` with and without `extended`, `nobinary`, `detail`, `-fmt nox`, a linked
  program, special registers for 6 processors and device control registers for the 401 and 403;
- `-dis` of archives (members with long names, an empty member and a text member), of two files, written to a file
  with `-o` (the files are compared), and of objects of another machine or ELF version, of the other byte order and
  not ELF;
- `-dis` of the rest: the default dump, `only,data`, `only,tables`, the exception tables of an object compiled with
  C++ exceptions (`only,data,exceptions` and `xtables`), `all,detail`, a linked program's headers, a function too
  big for the short `.mwcats` record, a ROM image (`-romaddr`, `-rambuffer`: its `.PPC.EMB.seginfo`), and an
  object whose `.comment` and `.mwcats.text` are renamed (a section dumped in hex at the end, another `.debug*`),
  and one whose `.comment` is named `.dynamic`;
- programs compiled with `-g` whose dead code the linker strips with its debug information: dead functions before,
  between and after kept ones, files of dead code only, static functions and data, C++ classes and inline functions
  (one copy of a weak function dropped), optimized code, an object made by a partial link of three (several compile
  units, an earlier one dropped whole), a link without `-g` and a partial link. Each is compiled by 2.6 and by 3.0a5.2,
  whose objects have a symbol for each `.debug` entry and each function's lines. One of them is linked over an
  ELF file and a map that are there already and longer;
- the objects' loading: a function two objects define, common symbols merged and against a smaller definition, an
  object that defines `_stack_addr` and `_SDA_BASE_`, code that uses the predefined linker-defined symbols, a
  missing entry point, C++ constructors and destructors, code that refers to a symbol the command file defines as a
  function's address plus 4, small data past the 64K the small data base reaches, and `.sbss` before `.sdata`;
- the PowerPC EABI side: asm code with each relocation the compiler makes (ADDR16 and its halves, REL14, ADDR24),
  linked low and out of range; a small data reference to data that is not small; sections bound outside their
  memory area; objects of different EABI compliance; the command file's contents mixing kinds of sections (BSS
  with others, small data with others, code in `.sbss`, sections it can't name, kinds that can be mixed), a
  member of a file that is no archive; `.text`, `.bss` and `.sdata` without output sections; sections of the
  program's own (code, data, one of another type than its output section, an `.abs.` section), and `__start` in a
  code section of its own;
- the messages' limits: more than 100 errors of three kinds and more than 2000 warnings;
- the disassembler's sweeps: objects of instruction words (every primary opcode, every extended opcode of 19, 31, 59
  and 63 and the forms of 4, 30, 58 and 62, the conditional branches' BO and BI, every special, time base and device
  control register number, the traps' TO, rA 0 and rS equal to rB, 8192 random words, and the rotates' whole SH, MB
  and ME space), compiled as asm functions of placeholders and patched, dumped with `-dis` for one processor of each
  of the disassembler's processor masks (18), with and without extended mnemonics, and a little-endian object
  (`-little`). A branch whose BO ignores the condition prints an address as its BI (the original's buffer): the
  comparison takes such numbers as equal;
- the command file's file lists: FORCEFILES keeping an object's dead code, files a section and FORCEFILES name that
  are not in the link, a member pattern for an object, EXCLUDEFILES and INCLUDEDWARF of files that are not there,
  and INCLUDEDWARF of a linked program, whose debugging sections the output takes;
- the partial links that use the command file (`-r1`, `-r2`, one with an unresolved symbol, `-strip_partial`, it with
  FORCEFILES and INCLUDEDWARF, C++ constructors with both), and EXCLUDEFILES of a file that is not there, of an
  object (an error) and of a linked program, whose symbols that another object has weak are dropped. EXCLUDEFILES
  applies only to these: the driver sets its flag for `-r1`;
- the driver's command line alone (its messages and exit status): `-help` with each of its keywords (`usage`, `all`,
  `spaces`, `obsolete`, `ignored`, `deprecated`, `meaningless`, `compatible`, `nonormal`), an option's help by its
  name (known, unknown, too long), a search with and without results, a group, an empty group and a bad keyword;
  `-version`, an unknown option, an option without its argument, no inputs, an input, a library (`-l`) and a
  response file (`@`) that are not there; `-w` with `display`, `off`, `off` then `on`, `error` and a bad keyword.

DIR holds the MSL runtime's `global_destructor_chain.o` and `__init_cpp_exceptions.o`, compiled from source; Echoes'
`build/G2ME01/src/Runtime/` has them. With the equivalent units linked from source, all 272 scenarios are the
original's. A scenario whose comparison fails (an output the ELF reader cannot parse) counts as different. The original linker reads past the end of `error-brace`'s command file (a GROUP never
closed) and once crashed there in a full run (not in 40 runs of its own): a rerun settles such a difference. The 2.7 linker in its place differs in 24 of the first 34, which shows the check sees a different
linker; SRecord.c with its checksum not complemented differs in the S-record scenarios.

`LinkerCmdFile.c` (the command file's parser and expressions) is the first. Its range comes from its data:
`"LinkerCmdFile.c"` starts its `.data` (0x48a5b8), followed by the command file's keywords and operators and its
switch tables, up to ELF_PPC_EABI.c's `.ctors` at 0x48abf0. Its code is 0x4461b0 to 0x44a210 (29 functions, 16 KB):
0x44a210 is the first function that uses ELF_PPC_EABI.c's data. Its statics are 0x49a9d8 to 0x49aaf0 in `.bss`.

- It is `equivalent`. 27 of 29 functions are the original's bytes. The two that are not differ only in register
  allocation; their control flow is the original's:
  - `ParseLinkerCommandFile` (0x446760) is the original's size, with 40 instructions differing.
  - `ParseSections` (0x447970) is 16 bytes longer, with 47 instructions differing: the original keeps `p`, the
    section and the list's tail in registers and spills less (a frame of 0x20 against our 0x28).
- Its functions are named in `functions.json` after its object, and the command file's globals in the COMMON block are
  bound by their `data_` names in `bindings.json`.
- The source has the whole unit: the lexer, the parsers of MEMORY, SECTIONS, the file and symbol lists and the
  expressions, and the evaluation of an expression (`EvalExpr`...). An expression is a list of terms (`LCFExpr`:
  next, value, kind) that is reduced in place.
- The structs are packed: `LCFOperator`/`LCFExpr` are 10 bytes (`align = mac68k`), others `pack(2)`.
- Some helpers are `static inline` (a plain `static` one that is inlined is still emitted). `strcmp` against a literal
  is compiled inline (`repe cmpsb`).
- The object's `.data` (strings, then the switch tables) is the original's but for the 4 bytes of alignment before
  the next unit, and so is its `.bss`.
- `.bss`: each unit's statics come in link order: CompilerTools.c, DumpELF.c, PrepDumpELF.c, ELF_gen.c, SRecord.c,
  ELF_Endian.c, LinkerCmdFile.c (0x49a9d8), ELF_PPC_EABI.c (0x49aaf0), LinkSym.c, DumpDWARF.c, DWARF_line.c. The
  COMMON block, every unit's uninitialized globals, follows them at 0x49b0f0 and runs to the section's end.
  `derive_splits.py` puts a linker's `common` unit last for this. The reference compiler has its block before its last
  units, which put it after Unmangle.c here. LD_1_0's splits were made before this change and are left as they are; derived again, its
  DumpDWARF.c and DWARF_line.c would get statics of their own.

`SRecord.c` (the S-record writer of `-srec`) is the second. No string names it: the name is this repository's. Its
code is 0x442e00 to 0x4434f0 (9 functions), after ELF_gen.c; its `.data` starts at 0x4897a0 with the two options
(`-sreclength`'s record length and `-sreceol`'s end of line, set by the driver), its formats and its switch tables;
its statics (the record being built and its line) are 0x49a7b0 to 0x49a9d0.

- It is `equivalent`: 4 of 9 functions are the original's bytes. The others do the same with other code:
  `SRecord_OpenFile` inlines `SRecord_InitBuffer`, `SRecord_Start` and `SRecord_AddWord` are shorter, and
  `SRecord_AddBytes` and `SRecord_Header` differ in registers.
- A record is begun by `SRecord_Start` (type, address and the address bytes in the checksum), filled a byte at a time
  and ended (`SRecord_End`, inlined where a record fills) with its count and checksum. The linker writes S0, S3 and S7
  records only.
- Its functions and globals are named in `functions.json` and `bindings.json` (`SRecord_...`, `data_...`).
- 0x489820 to 0x4899b0, after its data, is data no code references (month lengths, day and month names, date and
  time formats). The derived splits give it to SRecord.c's `.data`, so the equivalent executable, which has SRecord.c's
  own data in its place, does not have it.
- After its code, `CompilerTools_GetTicks` (0x4434f0, COSToolsCLT.c's in 2.7) is alone, away from COSToolsCLT.c's
  code at 0x42eec0. A function's `"unit"` in `functions.json` names its unit outright, and it is the unit `GetTicks`.

`src/linker/ELF_Endian.c` (the byte order of the ELF structures) is the third, and `matching`: its 12 functions are
the original's bytes, and it is in the SHA-exact build. Its code is 0x443510 to 0x443910, its static 0x49a9d0; it was
part of `wcstold.c`'s range. The compiler's `src/backend/ELF_Endian.c` is the same file as the compiler builds it:
the linker's has five of its functions (the ELF header, section header and symbol, `ConvertThreeWords` for an
`Elf32_Rela`, `swap_conversion_blocks` for the line tables) and seven others: the program header, the `Elf32_Rel`,
words, two halves and two words, and three kinds of records that ELF_linker.c reads. The `"ELF_Endian.c"` string
(0x4899b0, the backend's assertion) is in SRecord.c's `.data` range. `derive_splits.py` puts a `src/linker/` function
in the unit its list gives, before the unit the compiler's objects define its name in, since they define these names
in `src/backend/ELF_Endian.c`.

`src/linker/Archive.c` (the archives of `-library`) is the fourth, `equivalent`; no string names it. Its code is
0x4529d0 to 0x4533e0 (7 functions), its `.data` 0x48b7a8 to 0x48b7d0 (`"!<arch>\n"`, the header's formats and a flag:
long names as `/offset`), its statics 0x49ab10 to 0x49ab20.

- It writes the archive (`Archive_Write`): `!<arch>`, the symbol table member `/` (the count and the members' offsets,
  big-endian, then the names of the global and weak symbols), the long names member `//` of the objects whose names
  are longer than 15 characters, then each object, its header named `name/` or `/offset`, on an even offset.
  `Archive_MemberName` and `Archive_EndLongNames` read the names of an archive the linker links (ELF_linker.c and
  PrepDumpELF.c call them).
- Its 7 functions do what the original's do, with other code (none is the original's bytes).
- The two functions before it, 0x452860 (a name's dots to underscores) and 0x4528d0 (the linker-generated symbols'
  table, with LinkSym.c's data and a static), are LinkSym.c's: `"LinkSym.c"` ends LinkSym.c's strings at 0x48b794.
- After it, 0x4533e0 is MSL's `qsort` (`src/msl/qsort.c`, 341 bytes, as 2.7's). Compiled, it is the original's
  but for two locals' stack slots, with each of the compilers, so it is `equivalent`.

`src/linker/DumpELFCodePPC.c` (the code of `-dis`) is the sixth, `equivalent`. No string names it; DumpELFCodePPC.c
is the CodeWarrior file for this role. Its code is 0x453540 to 0x453f90 (8 functions), its `.data` 0x48b7d0 to
0x48cb00 (the special and device control registers' tables, then its formats), its statics 0x49ab20 to 0x49aca0.

- `DumpCode_Section` prints a code section: `DumpCode_Instructions` disassembles each word with the disassembler
  (0x4586a0, in the unit after DWARF_line.c), prints a hunk line where a symbol starts, and the line in one of three
  layouts (the `-show` options). `DumpCode_Operand` is the disassembler's callback for operands: a relocation's
  symbol with `@l`/`@h`/`@ha`, or a register's name.
- The registers' names are looked up by number in two tables of {number, name, processors}, the processors from
  `-proc` (`DumpCode_Setup`); the names are printed only with `-show ...,extended`. The tables were made from the
  executable.
- 1 of its 8 functions is the original's bytes.

`src/linker/DumpDWARF.c` (DWARF 1's `.debug`, printed by `-dis -show debug` and converted to the other byte order
when the linker reads and writes it) is the fifth, `equivalent`. Its code is 0x453f90 to 0x456550 (8 functions), its
`.data` 0x48cb00 to 0x48dde0 (where DWARF_line.c's `".debug"` starts), its statics 0x49aca0 to 0x49acc0.

- `DumpDWARF` walks the entries (length, tag, attributes by form); `DumpDWARF_Block` the blocks (location
  expressions, type modifiers, array subscripts). The same walk converts the bytes without printing: DWARF_line.c
  calls it with printing off.
- Each value is checked against the section's bounds and converted before or after it is read. Which values are read
  only when printing matters: those are converted only then.
- The name functions (tags, attributes, operations, fundamental types) were made from Ghidra's code with the strings
  read from the executable. `DumpDWARF_FundamentalTypeName` rewrites Metrowerks' long long codes in the section, as
  the original does.
- `DumpDWARF_ReadDouble` converts the bytes of its own pointer argument, not the value's, as the original does.
- 1 of its 8 functions is the original's bytes.
- A dump of an object that is not relocated shows its references as 0, so the relocated and linked dumps are the
  scenarios that see the references' conversion.

`src/linker/DWARF_line.c` (DWARF 1 of dead-stripped code) is the seventh, `equivalent`; its assertions name it. Its
code is 0x456550 to 0x4585d0 (9 functions), its `.data` 0x48dde0 to 0x48df20, its static 0x49acc0 (the next
relocation to look at).

- As each object is read (`DWARF_line_StripDeadCode`), its `.debug` is converted with DumpDWARF, or, with `-g` and
  dead code, the entries of its dead functions and variables become padding to their siblings, the entries before
  them point past them, and their lines are removed from `.line`; the units after move back with their relocations.
  `DWARF_line_ReadEntry` reads an entry's tag and the relocations of its sibling, addresses and lines.
- Before the output is written (`DWARF_line_Compact`, from ELF_gen.c), the objects whose entries and line tables
  have their own symbols (`.dwarf.<tag>.<name>`, `.line.<name>`, which the 3.0 compilers write) lose those of the
  dropped code from `.debug` and `.line` piece by piece; a compile unit left empty is dropped
  (`DWARF_line_DropCompileUnit`), a kept one gets its size and its code's size (`DWARF_line_FixCompileUnit`), its last
  entry padded to 4 bytes (`DWARF_line_WritePadding`), and the line entries' addresses move back by the dropped code's
  size.
- 2 of its 9 functions are the original's bytes. Ghidra lost the switches of the large functions: they were read from
  the disassembly.
- Breaking the code is seen by the scenarios except in a few places: moving a later chunk's fixups in `.line` (the
  input side), and on the output side the values of the `.debug` pieces' symbols, the dropped compile unit's symbol
  and the sections' sizes, which the linker seems to make again from the pieces when it writes the output.

`src/linker/PrepDumpELF.c` (`-dis`: what is dumped, and where to) is the eighth, `equivalent`; its assertions name
it. Its code is 0x43aab0 to 0x43b2b0 (9 functions), its `.data` 0x4893d0 to 0x489448, its statics 0x49a6f8 to
0x49a728. The units around it were drawn again: ELF_linker.c ends at 0x434a70 (6 more functions, which use its
`.data`: the ELF magic), DumpELF.c is 0x434a70 to 0x43aab0 (its printer and strings are used up to there, and its
jump tables end at 0x4893d0), and ELF_gen.c starts at 0x43b2b0.

- `PrepDumpELF_Dump` dumps a file (`DumpELF.c`'s `DumpELF_Dump`, printing through `PrepDumpELF_Print`) or each
  ELF member of an archive (`PrepDumpELF_DumpArchive`, the `//` member holding the long names) into a text that
  becomes the driver's output, or is written to the output file when memory runs out (`PrepDumpELF_Flush`, the hook
  the allocator calls).
- Before them, the linker's file helpers: `PrepDumpELF_CreateFile` (an output file made empty, both its forks, with
  its type and creator; ELF_gen.c, SRecord.c and LinkSym.c call it) and two Pascal string functions the driver
  calls. Their place between DumpELF.c and PrepDumpELF.c's own functions puts them here.
- `data_0054c3d0` (1, the compiler's shared code reads it, `cc-eabi-ppc.c` has it in the compiler) is at the start
  of this file's `.data`, so it is defined here.
- 3 of its 9 functions are the original's bytes.
- Breaking the code is seen by the scenarios except: the type and creator of an output file, the line's truncation
  (no line is that long), the last end of line (the dumps end with one), the kinds of file `-dis` gives it, the bad
  file reports (DumpELF.c dumps the other machine's and version's objects), and the Pascal strings.

`src/linker/DumpELF.c` (`-dis` of one ELF image) is the ninth, `equivalent`; its assertions name it. Its code is
0x434a70 to 0x43aab0 (31 functions), its `.data` 0x487aa8 to 0x4893d0, its statics 0x49a6e0 to 0x49a6f8.

- `DumpELF_Dump` (9005 bytes) takes the image section by section: the file and program headers
  (`DumpELF_Header`, `DumpELF_ProgramHeaders`), the section headers, the string tables, the symbols (sorted by
  address for the code and data dumps), the segment information, the relocations (each one the link of the section
  it relocates), the exception tables (`extab`, `extabindex`), code (DumpELFCodePPC.c), data in hex
  (`DumpELF_HexDump`, with each symbol's name), the linker's records (sections of type 0xca2a82c2), `.line`,
  `.debug` (DumpDWARF.c) and the other `.debug*` sections, `.comment`, `.note` and `.dynamic`, then with `detail`
  each section left in hex. A file whose byte order is not the host's (a big-endian one: the host is the x86) is
  converted for the dump; the static flags say which parts. `-dis` gives the kind 5, which converts them all; the
  conversion back after the dump is for the kinds 1 and 2, which nothing gives.
- The original has the section lookups and the plain hex dump inline, five copies of the latter; here they are
  static helpers.
- The `-show` flags are the driver's options (0x4a2d6e to 0x4a2dc6), bound by address.
- 2 of its 31 functions are the original's bytes. The original has `DumpELF_SymbolName` inline in the symbol and
  relocation dumps, and a switch where `DumpELF_ProcessorIndex` has tables.
- Breaking the code is seen by the scenarios except where no input reaches it: REL relocations (PowerPC objects have
  RELA), `.note` sections, the printer used when none is given (PrepDumpELF.c always gives one), procedure records
  with exits, exception-table locals held in registers, the sort of a code section's relocations (the disassembly
  finds them in either order), the conversion back to the file's byte order (no `-dis` kind does it), and a section
  symbol of no section (`UND`).

`src/linker/LinkSym.c` (the linker-defined symbols) is the tenth, `equivalent`; its assertion names it. Its code is
0x452150 to 0x4529d0 (7 functions), its `.data` 0x48b578 to 0x48b7a8 (the predefined symbols' names and table, then
its strings: `"LinkSym.c"` is the last), its static 0x49ab08 (the list's last symbol). The list's head, 0x4a2934, is
in the COMMON block. Its first function was 0x452710: the four before it use its table and its strings, while the map
file's writer before them (0x451f40 to 0x452110, on the static 0x49aaf8) and the code before that use other data,
so they are ELF_PPC_EABI.c's for now (0x44e930 to 0x452150 had been LinkSym.c's, a boundary of the assignment).

- `LinkSym_Add` gives a name a value (the command file's assignments, through LinkerCmdFile.c), adding it to the
  list after the 12 predefined symbols (`_stack_addr` to `_eti_init_info`); one with a value already is reported
  and keeps it. `LinkSym_Find` and `LinkSym_FindIndex` look a symbol up by name and by its (negative) index.
- `LinkSym_AddSectionSymbols` makes an output section's `_f_NAME` (ADDR), `_f_NAME_rom` (its ROM address, not for
  `.bss` kinds) and `_e_NAME` (ADDR + SIZEOF), the name's dots made underscores (`LinkSym_Underscores`); one the
  command file assigned is reported and takes the section's value.
- `LinkSym_AddStartOf` (`.startof.NAME_N`) and `LinkSym_AddSegment` (`.phdr_N.sec_M`) are ELF_gen.c's, under a flag
  of the driver's that only the code clearing it touches: no option makes them.
- 2 of its 7 functions are the original's bytes.
- Breaking the code is seen by the scenarios except: the flag of the predefined tables' symbols and the indices of the other symbols (no scenario's output shows
  them), and a section symbol the command file assigned keeping its value (the section's value is written into the
  term the old value had, so that value goes in either case).

`src/linker/ELF_linker.c` (the link itself) is the eleventh, `equivalent`; its assertions name it. Its code is
0x42f600 to 0x434a70 (31 functions, 21 KB); it has no data of its own besides its strings and switch tables
(0x4876e8 to 0x487aa8), and its globals are the COMMON block's. It was written in three parts by address, and their
types merged: ELSymbol (a `.symtab` entry, whose first word becomes the symbol's piece), ELPiece (a piece of a section,
which is also a global symbol of the name hash), ELChunk (the pieces kept or dropped together), ELSection, ELObject
(Archive.c's ArchiveFile), ELFile (the project's file) and the command file's lists.

- `ELF_linker_Link` is the link: `ELF_linker_ReadFiles` reads the project's files (the objects, then the archives'
  members that are needed), `ELF_linker_LoadObjects` loads them (their sections, symbols, duplicates, commons, the
  `.BINARY` files and the linker-defined symbols), `ELF_linker_SplitSection` cuts a section into its chunks,
  `ELF_linker_DeadStrip` keeps what the entry point, the overlays, FORCEACTIVE, the constructors and destructors, the
  exception tables and the forced files need (`ELF_linker_KeepSymbol` writes the link map's tree), and
  `ELF_linker_End` closes the outputs. `ELF_linker_Library` is `-library`'s.
- `ELF_linker_ReadObjectOptions` reads an object's `.comment` and FORCEFILES; `ELF_linker_ExcludeFiles` and
  `ELF_linker_CheckIncludeDwarf` are EXCLUDEFILES' and INCLUDEDWARF's; `ELF_linker_LinkDebugPieces` ties the 3.0
  compilers' `.debug` and `.line` pieces to their code.
- Its first function, 0x42f600 (`ELF_linker_CallCallback`), is a callback wrapper like CPrep.c's before it, called
  only from the driver's file code: it is likely CPrep.c's, though it has no source there.
- `tools/derive_splits.py` reads the stdcall decoration of a linker-only source's functions from the linker's own
  compiled objects (0x42f600 is `__stdcall`).
- 3 of its 31 functions are the original's bytes. 12 static helpers are not original functions (the original repeats
  them inline).
- Breaking the code is seen by the scenarios except: `.BINARY` files (no scenario links one); in
  `ELF_linker_SplitSection`, a chunk kept whole for a symbol without a `.comment` entry, the offsets of symbols of no
  size in the sized ones they are in, and a section's alignment given to its first piece (the 2.6 compiler's objects
  have a `.comment` entry for every symbol, no labels of no size, and pieces aligned as their sections); a needed
  duplicate swapped with an archive member's definition; the `.dtors` chunks kept whole (2.6 C++ registers its
  destructors at run time); the padding between commons (the merged commons do not need it); and the mark on a
  linker-defined symbol's piece (its own flag already keeps it from being kept twice). The first sabotage run had 21
  of 39 breaks not seen; the link scenarios were added for them.

`src/linker/LK_symbol.c` (the symbols by name, and the link map file) is the twelfth, `equivalent`. No assertion
names it: the survey's list of the linker's sources has an `LK_symbol.c`, and this is the symbol code. Its code is
0x451570 to 0x452150 (16 functions), its `.data` 0x48b4a0 to 0x48b578 (`"_Undefined_Symbol_"` and the other names of
nameless section symbols, the map's section layout lines), its static 0x49aaf8 (the map's buffer). Before it,
0x451410 and its jump table (0x48b470) use ELF_PPC_EABI.c's data, so they are that unit's; the functions from
0x44e930 to 0x451570, which the assignment had given to the next unit, are tagged ELF_PPC_EABI.c's too, since they
use its data. `getbit` (0x44ca50, CompilerTools.c's in the compiler) is in ELF_PPC_EABI.c's range, called only from
it.

- `LK_symbol_Add` makes a symbol in the name hash (4096 chains, 0x49e1c0, the COMMON block's) and the list of all
  symbols; `LK_symbol_Find` (an object's own symbol, else the global one), `LK_symbol_FindGlobal`,
  `LK_symbol_FindInFile` and `LK_symbol_FindAny` look names up; `LK_symbol_ClearTable` empties the hash and the list.
- `LK_symbol_Name` and `LK_symbol_ObjectName` give a symbol's name; `LK_symbol_Alignment` its alignment, from its
  `.comment` entry or its kind and size.
- `LK_symbol_OpenMap`, `LK_symbol_Print`, `LK_symbol_FlushMap` and `LK_symbol_CloseMap` are the link map file's;
  `LK_symbol_PrintLayoutHeader` and `LK_symbol_PrintLayoutLine` write its section layout.
- ELF_linker.c's types are in `include/linker/ELF_linker.h` now, which LK_symbol.c shares, with the COMMON block's
  globals and both units' functions.
- 7 of its 16 functions are the original's bytes.
- Breaking the code is seen by the scenarios except: the hash function (a different hash only changes the order of a
  chain, which every lookup searches by name), the table's clear (one link a process), the map closed (the system
  closes it at the exit), the map written without a map (its file is 0) and the 0x1400-byte clamp (no line is that
  long); the buffer's flush at the end (the map's buffer is written as it fills: `SRecord_InitBuffer` makes none
  when the output is written as it is made); the message when the map file cannot be made (the driver checks the
  path first); `LK_symbol_FindAny` (only the map's unreferenced duplicates use it); the alignment of `.comment`
  versions 1 and 2 (`ELF_linker_ReadObjectOptions` never makes 1, and makes 2 for a comment of versions 3 to 5,
  which none of the compilers here writes: 1.0 to 1.2.5 write 8, the later ones 10), and of a section symbol without one; and the names of nameless section symbols of no section and of
  commons. The FORCEFILES scenario's object has variables of each size, since a forced object's symbols are aligned
  by their size (its comment version is made 0).

`src/linker/ELF_gen.c` (the output) is the thirteenth, `equivalent`; its assertions name it. Its code is 0x43b2b0
to 0x442e00 (50 functions, 31 KB: 0x43c2b0, 0x43c7c0, 0x43d380 and 0x441560, which functions.json had inside the
functions before them, are functions of their own), its `.data` 0x489448 to 0x4897a0 (the padding's size and
pointer, its strings and a switch table), its `.bss` 0x49a728 to 0x49a7b0 (the 0x80 zeros the padding starts as,
and the last local symbol of `.symtab`'s order). Nothing outside it uses its data. It was written in three parts by
address, on the shared header's types.

- `ELF_gen_WriteFile` writes the ELF file: the header, the program headers (`ELF_gen_WriteProgramHeader`), the
  sections' contents, the other sections, the section headers, the overlap, stack and heap checks, the link map's
  memory map, then the header and program headers again. `ELF_gen_SetLinkSymbols` and
  `ELF_gen_CheckLinkSymbols` give the linker-defined symbols their values and sections.
- The layout: `ELF_gen_LayoutSection`, `ELF_gen_LayoutBssSection` (with the 64K check of the small data sections),
  `ELF_gen_LayoutAssignment`, `ELF_gen_LayoutContents` (a section's items: assignments, chunks, `.ctors` and
  `.dtors` and their end words, the map's lines), `ELF_gen_EvalAssignment`, `ELF_gen_LayoutDwarf`,
  `ELF_gen_LayoutRecords`, and the exception table index (`ELF_gen_SizeExtabIndex`, `ELF_gen_MapExtabIndex`,
  `ELF_gen_SortExceptionEntries`, `ELF_gen_WriteExtabIndex`).
- The sections: `ELF_gen_WriteSection`, `ELF_gen_WriteFill`, `ELF_gen_WriteAssignment`, `ELF_gen_WriteDebug`,
  `ELF_gen_WriteRecords`, `ELF_gen_WriteComment`, `ELF_gen_WriteSegInfo`, the relocations
  (`ELF_gen_SizeRelocations`, `ELF_gen_WriteRelocations`, `ELF_gen_ApplyRelocations` and the records' and
  extabindex's), the symbols (`ELF_gen_OrderSymbols`, `ELF_gen_WriteSymbols`, `ELF_gen_ResolveSymbol`,
  `ELF_gen_SymbolAddress`), the string tables and `ELF_gen_NumberSections`. Most are reached through a callback of
  each output section (`o54`), which ELF_PPC_EABI.c sets.
- The output's buffer: `ELF_gen_OpenOutput`, `ELF_gen_Write`, `ELF_gen_WriteByte`, `ELF_gen_WriteString`,
  `ELF_gen_AddToBuffer`, `ELF_gen_WritePadding`, `ELF_gen_SkipPadding`, `ELF_gen_WriteBuffer`,
  `ELF_gen_FlushOutput` and `ELF_gen_CloseOutput` (Archive.c, LK_symbol.c and SRecord.c use them too). A pad
  longer than the 0x80 zeros makes a bigger buffer filled with `'0'`, as the original does.
- The shared header's output section (ELOutput, LinkerCmdFile.c's LCFSection) has its whole 0xd0 bytes now: its
  words 0x58 to 0x74 are the section's program header, 0x78 to 0x9c its section header. Words not named yet are
  `oNN`, by offset.
- An original function that compares a whole word must get one: `fn_0044afc0` (an output section by its index)
  was first declared with an `unsigned short`, whose upper half the call left as it was, and the predefined tables'
  symbols lost their sections.
- 5 of its 50 functions are the original's bytes. Its static helpers (`ELGA_`, `ELGB_`, `ELGC_`, by part) are
  not original functions (the original repeats them inline).
- The sabotage was 79 breaks, two in each function (its first comparison turned round, its first constant made
  one more). 27 are not seen: the messages of a failed write (10; no scenario's write fails), an output kind and
  the segment symbols the 2.6 driver never selects, the order error of the small data sections (the linker
  reports their order with another message before), a flag of the exception entries' targets, the flags and the absolute section
  of an object's symbol a relocation resolves to (the compilers make no absolute symbols), a header written a byte
  longer (the program headers overwrite it), two assertions, a symbol whose section has no output, the marks on
  sections that are numbered later (3) and the procedure records' padding (they are aligned). The first run also
  showed that a scenario whose output could not be read stopped the whole run, which the sabotage took for unseen;
  each scenario's comparison is guarded now. Four links were added for the rest: the predefined symbols used,
  INCLUDEDWARF of a linked program, small data past 64K and `.sbss` before `.sdata`; and three command files for
  `ELF_gen_WriteFile`'s overlap, stack and heap warnings, which no break had reached. Every string the file has is
  the original's (`"heap_addr"` and `"heap_end"` are without their underscore there too). The
  SHORTEN_NAMES_FOR_TOR_101 handling of names over 1000 characters is not reached: the compilers cut names at 255.

`src/linker/ELF_PPC_EABI.c` (the PowerPC EABI side of the link) is the fourteenth, `equivalent`; its assertions
name it. Its code is 0x44a210 to 0x451570 (39 functions, 28 KB), its `.data` 0x48abf0 to 0x48b4a0 (strings and
switch tables), its `.bss` the word 0x49aaf0 (the size of the linker's own data). CompilerTools.c's `getbit`
(0x44ca50) is among its functions, called only from it: the source has an equivalent copy of it, static. It was
written in four parts by address.

- The output sections: `ELF_PPC_EABI_ReadCommandFile` starts the output (it reads the command file, or
  `ELF_PPC_EABI_MakeOutputSections` makes the default SECTIONS), `ELF_PPC_EABI_NewOutput` makes an output section
  with its `.rela` and `.mwcats` sections, `ELF_PPC_EABI_PrepareOutputs` and `ELF_PPC_EABI_SetSectionType` give
  them their kinds and ELF headers, and the `.ctors` and `.dtors` items of the runtime's objects are added.
- Each input section's output section: `ELF_PPC_EABI_AssignSection`, `ELF_PPC_EABI_ChooseOutput` and
  `ELF_PPC_EABI_ScoreOutput` (the best match of the command file's items), `ELF_PPC_EABI_AddInput` (its alignment
  and flags), `ELF_PPC_EABI_SectionKind`.
- The layout: `ELF_PPC_EABI_MakeMemoryAreas`, `ELF_PPC_EABI_PlaceSection`, `ELF_PPC_EABI_AdvanceMemory`,
  `ELF_PPC_EABI_DefineSmallDataBases` (`_SDA_BASE_`, `_SDA2_BASE_`), the ROM copy and BSS init tables
  (`ELF_PPC_EABI_LayoutLinkerSymbols`, `ELF_PPC_EABI_WriteLinkerData`), and `ELF_PPC_EABI_PrepareLayout` and
  `ELF_PPC_EABI_PrepareWrite`, which set the output sections' callbacks to ELF_gen.c's functions.
- `ELF_PPC_EABI_LinkSymbolValue` gives a linker-defined symbol its value; `ELF_PPC_EABI_ApplyRelocations` applies
  the PowerPC relocations (with their range checks), `ELF_PPC_EABI_CheckSdaReference` the small data rules,
  `ELF_PPC_EABI_RelocateSection` a record section's; `ELF_PPC_EABI_CheckCompliance` and
  `ELF_PPC_EABI_CheckComment` compare the objects' `.comment` EABI compliance.
- The header has the types the two units share now: an expression's term (ELTerm), a MEMORY area (ELMemory), an
  item's list of input sections (ELSectionRef) and a list of output sections (ELOutputNode).
- 5 of its 39 functions are the original's bytes. Every string is the original's.
- The sabotage was 99 breaks: two in each function (its first comparison turned round, its first constant made one
  more), each message's first call given the next number, and each relocation type of
  `ELF_PPC_EABI_ApplyRelocations` moved to an unused one. 55 were not seen at first; 29 link scenarios were added
  for them (the relocations of asm code linked low and out of range, small data references to data that is not
  small, sections bound outside their memory area, objects of different EABI compliance, the command file's
  contents mixing kinds of sections, output sections missing, the program's own sections, `__start` in a section of
  its own), and 41 are still not seen. The compilers make no relocations of types 7, 0x1a and 0x18 outside the
  debugging sections (DWARF_line.c relocates those), no loaded section aligned to 1, no `.comment` of version 3 in
  byte 0xf or older than 8, and no symbol the relocations' flag tests tell apart (0x10 against 0x11). A missing
  linker-defined symbol (0xf), one made without a value (0x63), an item without file or section (0x62), an unknown
  relocation (0x3c) and a section of no kind (0x5e) are not reached; ELF_linker.c reports a member of a file that is
  no archive (0x61) before. A message's first call is sometimes behind another (0x5d, 0x67, 0x8b, 0x80: the scenarios
  reach the later ones). The rest do not change what the linker does: flags set to 2 instead of 1 and booleans
  returned as 2, allocations a byte or a word longer, GROUP outputs (kind 5) never matched by name or index anyway,
  the `.comment` mode of a normal link (only partial links write a `.comment`), `__start` found in any file, the
  init memory area of ROM images and the section-index and compressed-code (0x40000000) checks.

`src/driver/LibImportMessages.c` (the linker's messages) is the fifteenth, `equivalent`. It is the 2.x driver's
version of the file the 1.x compilers match: its code is 0x42dfb0 to 0x42e764 (13 functions, the last of the file
first), and its additions are under `#ifdef LINKER` (configure.py defines it for a linker's units), so the
compilers' builds preprocess to the same tokens as before (checked for the 8 compiler versions). The source has no
address comments: it is the compilers' file too.

- The 1.x functions, with the linker's own printing functions (0x420270 to 0x4202f0) and its `%s` names unmangled
  into 0x800 bytes: `format_string`, `CompilerTools_ReportDiagnostic` (warnings, at most 2000),
  `CompilerTools_ReportLimitedDiagnostic` (errors, at most 100, then message 25, "too many link errors"),
  `CompilerTools_FormatMessageAndLongjmp`, `CompilerTools_DispatchMessageBufferByMode`.
- The linker's: `format_message`, `CompilerTools_ReportUndefined` and `CompilerTools_ReportDuplicate` (an undefined
  symbol's error and a symbol defined twice, two lines each), `CompilerTools_ReportMessage` (printed, not counted),
  `CompilerTools_BufferDiagnostic` and `CompilerTools_ReportBufferedDiagnostic` (a message kept in the buffer, then
  printed with the next as its second line: the small data errors), `CompilerTools_Abort` (back to the link's start
  with status 1) and `CompilerTools_ResetDiagnosticCounts`. The linker sources call them by these names now.
- Scenarios: more than 100 undefined symbols, symbols defined twice and small data errors, more than 2000 warnings
  (code sections the command file does not name), and a symbol the command file uses that is not there.
- 9 of its 13 functions are the original's bytes (`tools/funcdiff.py` takes the addresses of a file without address
  comments from `functions.json`). Its strings are the original's.
- The sabotage was 29 breaks: two in each function (its first comparison turned round, its first constant made one
  more) and 10 by hand (the two limits, the messages of the second lines and at the limit, the buffered line's
  order, the reset). 4 are not seen: a full buffer's last byte (no message is 0x800 long), the message number
  `CompilerTools_ReportUndefined` gives at the limit (the limit's message is 25 whatever the number), the counts'
  reset after the link, and the progress messages' text (`CompilerTools_ReportMessage`: the command line does not
  show them).

`src/linker/PPCDisassembler.c` (the PowerPC disassembler of `-dis`'s code dumps) is the sixteenth, `equivalent`:
0x4585d0 to 0x46b356, 24 functions, its data 0x48dfd0 to 0x492280 and its state 0x49acf8 to 0x49b0f0.
`include/linker/PPCDisassembler.h` has what DumpELFCodePPC.c calls: `PPCDis_Disassemble` and the operands' callback.

- `PPCDis_Disassemble` takes one instruction word (either byte order) and writes its mnemonic, operands and comment
  for the processors and options the dump gives; a word no handler takes is `dc.l` with its bytes as characters.
  Each primary opcode has a handler; `PPCDis_Opcode31`, `PPCDis_Opcode63` (59 too) and `PPCDis_Opcode30` search
  tables of extended opcodes for theirs. The handlers: `PPCDis_DForm` (the D-form opcodes, their extended mnemonics
  li, lis, subi, nop, cmpwi, tw<cond>i), `PPCDis_XForm` (31's and 63's X-form), `PPCDis_IntegerArithmetic` (XO-form),
  `PPCDis_MoveSpecialRegister` (mtcrf, mfspr, mtspr, mftb, mfdcr, mtdcr, with the registers' names for each
  processor), `PPCDis_FloatAForm`, `PPCDis_Opcode19` (bclr, bcctr, the CR logical instructions), `PPCDis_Opcode4`
  (Gekko's paired singles), `PPCDis_Branch`, `PPCDis_BranchConditional`, `PPCDis_SystemCall`, `PPCDis_Rotate`
  (rlwinm and its extended mnemonics), `PPCDis_RotateMD`, `PPCDis_RotateMDS`, `PPCDis_Sradi`, `PPCDis_LoadDoubleword`,
  `PPCDis_Isel` and `PPCDis_Mtfsf`.
- The data is the original's, generated from it: one array, `PPCDis_Data`, with the original's words (its pointers
  the addresses of their targets, its 23 switch tables zeros), and a macro `data_XXXXXXXX` for each object at its
  offset, so the tables of mnemonics, forms, extended opcodes and register names keep their layout. MSL's
  `locale.c` and `float.c` data before it are units of data of their own (`data_units` in `config.json`).
- The original's quirks are kept: the non-extended `bc` with BO 0x10 or 0x12 prints its buffer's address as BI
  (4828665), opcode 62 disassembles as `ld`/`ldu`, `mcrfs` takes crfS from bits 11-13, form 9's floating loads and
  stores with update add "; POWER instruction".
- Pro 6 loses the offset into `PPCDis_Data` in `switch ((signed char)data_0048e0ec[opcode])` (it indexes the array's
  start): `PPCDis_DForm` reads the forms through a pointer, `F_forms`. Every other reference keeps its offset (the
  compiled object's relocations to `PPCDis_Data` with offset 0 are this one and `data_0048dfd0`'s).
- It was written by parts (the core, then the handlers in six parts at once) and checked by the 40 sweeps: every
  word of each is the original's text, for each processor mask, with and without extended mnemonics, and in both
  byte orders. 2 of its 24 functions are the original's bytes; its strings are the original's.
- The sabotage was 36 breaks (two in each function: its first comparison turned round, its first constant made one
  more), against the sweeps and the code dumps. 6 are not seen, and no dump reaches them: `PPCDis_LoadDoubleword`'s
  two (64-bit instructions need option 0x40000000, which the dump never sets), `PPCDis_Register`'s rtoc (the dump
  always sets 0x4000000, "0x" for hex) and its test of a number over 31 (register fields are 5 bits),
  `PPCDis_AddOperand`'s limit one more (no instruction's operands are 0x1fe characters long), and
  `PPCDis_Opcode19`'s search one entry longer (the entry past its table never matches).

Two of the driver's shared sources are the seventeenth and eighteenth: `src/driver/Help.c` (`-help`'s columns and
option lists) and `src/driver/AssertionFailure.c` (an internal assertion's message). Help.c is `matching`: its code is
the original's, and the 2.x linker's `Help_FormatOption` has its static flags buffer, `data_0054dd04`, without an
initializer (in `.bss`; under `#if defined(LINKER) && VERSION >= VERSION_GC_2_7`, so the compilers and LD_1_0
preprocess to the same tokens). AssertionFailure.c is `equivalent` as it is: its one function pushes its arguments in
another order.

- The driver scenarios run the linker with options only and compare its messages and exit status.
- The sabotage of Help.c was 19 breaks (its functions' first comparison turned round and first constant made one
  more), against the driver scenarios; 4 hang (seen). 6 are not seen: the indentation's limits in `advance_column`
  and `fn_0042ae0a` (`help_width / 8`: no option's help is nested within 10 columns of the edge),
  `Help_PrintOptionList`'s test of the tool (the linker's `fn_0041c8ba` is 0x40, never 0x100),
  `Help_PrintOptionUsageNotes`'s parser mode 1 made 2 (the linker's is neither), the out-of-memory exit and
  `Help_PrintRepeatedCharLine`'s buffer filled one byte further (the line is cut at the width). AssertionFailure.c is
  reached only by a failed assertion.
- MacSpecs.c and Generic.c, the other shared driver units that link, have functions of other sizes (another version
  of the code).

`src/driver/StaticParserGlue.c` (the parser's messages, "Parser Strings") is `matching`: the linkers from 1.2.5 on
have two of its messages in their 2.x form, "No library file found matching 'lib%s{%s}' or '%s'" and "Cannot open %s
file '%s'" (the 1.0 and 1.1 linkers and the compilers have "Library file 'lib%s{%s}' not found" and "Cannot open
response file '%s'"), under `#if defined(LINKER) && VERSION >= VERSION_GC_1_2_5`. The compilers' builds and LD_1_0
preprocess to the same tokens as before (checked for the 8 compiler versions). The driver scenarios of a missing
library and a missing response file print them.

`src/driver/Targets.c` and MSL's `src/msl/misc_io.c` are `matching` as they are: the strings after Targets.c's data,
`"%s: "` and `"%s\n"`, are misc_io.c's (`perror`'s, which the link drops; nothing refers to them), so the split gave
them to Targets.c. `data_units` in `config.json` gives them to misc_io.c (an entry without "before" adds ranges to a
unit the functions have already).

More of MSL by the same means:

- `src/msl/unistd.win32.c` is `matching`: its data starts 8 bytes into the range the split gave alloc.c (`data_units`
  gives them back), and alloc.c's static `unlink(Block *)` (0x404310) was named `unlink` in `functions.json`, so the
  split exported it and the link kept unistd.win32.c's own `unlink`; it is `fn_00404310` now.
- `src/msl/string.c` is `equivalent`: `strerror` (0x405060) and `__strerror` (0x405080) were given to ClientGlue.c
  (their source is string.c now, and with them their strings and buffer); its code is the original's but for their
  registers (eax for ecx: no compiler at hand gives the original's).
- CLErrors.c (it links now that `_unlink` is not exported twice), ctype.c, ansifp_x86.c, MacFileTypes.c and wcstold.c
  gave the original on their own and do together: they are `matching` (buffer_io.c too at first, while its range was
  `__convert_from_newlines` only: below).
- `src/msl/wcstoul.c`, `src/msl/arith.c` and `src/msl/time.win32.c` are `matching`. time.c's last three functions
  (strftime among them) and their switch tables were wcstoul.c's (they had no source); arith.c's `div`, `__div` and
  `__mod` were named for their `long` twins (the same code: the link keeps one of each), so the compiled time.c's
  calls did not resolve; the start of file_pos.c (`__fseek`, `fseek`) was time.c's and the start of time.c
  (`__stdio_atexit`) file_pos.c's.
- `src/msl/signal.c` and `src/msl/abort_exit.c` are `matching`: the first word of signal.c's `.bss` (0x497288) is
  alloc.c's pool list (`data_units`), and 0x402140 is `atexit`, not `__atexit` (the same code; the link keeps the one
  the code calls). A probe of abort_exit.c alone still differs: the split's signal.c `.bss` starts at 0x49728c, which
  the link aligns to 8.
- `src/linker/TargetWarningHelpers-ppc-ld.c` is new, `equivalent` (in `src/linker/`, as the compilers' builds take no
  file there): the linker's own warning option (0x422440 to
  0x422490, before arith.c, and its string 0x4833c0, which the split had given arith.c and time.win32.c),
  `set_warning_option` ('Aw' of `-w off`: no linker warnings, a flag in ParserGlue-eabi-ppc-cc.c's data,
  `data_0046fb69` by `bindings.json`) and `ToolHelpers_ld_PrintLinkerWarningOptions` (`-w display`). The compilers'
  TargetWarningHelpers-ppc-cc.c has the C warnings instead. No compiler at hand gives `set_warning_option`'s code
  (the original compares the option as an int after `movsx` and returns with `mov eax, 1`). The sabotage (the option
  and its comparison) is seen by `driver-warnings-off`. file_pos.c and time.c still differ (time.c's strftime is
  another version, 1935 bytes for 1228).

Two more shared sources are `matching` with the 2.x linker's differences under `#if defined(LINKER) && VERSION >=
VERSION_GC_2_7` (the compilers and LD_1_0 preprocess to the same tokens as before):

- `src/driver/ParserFace.c`: the parser plugin's version is 12 (11 in the compilers), the flags after it are 1, 0,
  1, 0 with a word of zeros after them, `data_00543380` has no initializer (a common symbol), and `xmalloc` and
  `ToolHelpers_ResizeBuffer` are not in it.
- `src/driver/TargetOptimizer-ppc-eabi.c`: the linker has no scheduling options, so not
  `TargetOptimizer_ppc_eabi_SetOption`, `TargetOptimizer_ppc_eabi_ReportScheduling` and their data.

`tools/probe_units.py` links the `matching` units from their compiled objects now, as the exact build does: the
split's signal.c alone does not give the original (its `.bss` is aligned, above), so every other probe differed in the
section headers. With that, `src/msl/float.c` gives the original too.

MSL's file I/O and the plugins' glue, by the same means:

- `src/msl/string_io.c` is `matching`: the split had given it 0x40c300 to 0x40c570, which is buffer_io.c's
  (`__convert_to_newlines`, `__prep_buffer`, `__load_buffer`, `__flush_buffer` and `setvbuf`, and 0x40c2f0 is
  `__convert_from_newlines`: `__flush_buffer` calls it). Its own are `__read_string`, `__write_string` and
  `__close_string` (0x40c570, 0x40c580, 0x40c590: ansi_files.c's `__init_file` stores them) and its file modes at
  0x4791c0 (`data_units`: `__open_string_file`, which the link drops, uses them).
- `src/msl/char_io.c` gives the original with them.
- buffer_io.c, `src/msl/ansi_files.c`, `src/msl/file_io.c`, `src/msl/file_io.win32.c` and `src/msl/fcntl.win32.c`
  (new for LD_2_6: `open`, 0x421270, the range the split gave Utils.c first) are `equivalent`: their code is the
  original's but for registers and the order of a few instructions (file_io.c's `__get_file_modes` is 0x130 bytes for
  0x135). None of pro5, pro5.3 and pro6 gives the original's at -O4,p, -O4,s, -O3 or -O2. On the way:
  `__files` is 0x150 bytes (`object_sizes`: four FILEs, which other units refer to at offsets), 0x4053c0 is
  `__init_file`, 0x40c720 is `fopen` (not `__handle_open`, the same code), 0x40c750 `freopen`, 0x40c810
  `__get_file_modes`, 0x40c130 `__open_file`, and file_io.c's data word (0x4791c8) and file_io.win32.c's strings
  (".\MSL", ".tmp", 0x4791a8) were file_io.win32.c's and libimp's (`data_units`).
- `src/driver/libimp-eabi-ppc-mw.c` is `matching`: the linker has the library importer as a static plugin too, at
  0x40c040 to 0x40c0f6 (its eleven functions in the compilers' order) with its data at 0x478fb8 to 0x4791a8 (the split
  had it as cc-eabi-ppc-mw.c's, libimp's and file_io.win32.c's). The 2.x linker's plugin version is 12, and its
  plugin's version is four shorts, 2.4.1 build 32 (the compilers' is four bytes, 2.3.3), under `#if defined(LINKER) &&
  VERSION >= VERSION_GC_2_7`; 0x420660 is libimp-eabi-ppc.c's `fn_0041ec70`.
- `src/driver/CLStaticPlugins.c` is `matching`: the linker's plugin subtype is 'Link' (under the same condition), and
  it registers cc-eabi-ppc-mw.c's plugin (`fn_0040bed0`, `fn_0040bf10`: 0x40c010 and 0x40c020, the linker's own
  versions) and libimp's.
- The compilers and LD_1_0 preprocess libimp-eabi-ppc-mw.c and CLStaticPlugins.c to the same tokens as before.
- `src/driver/ParserHelpers.c` is `equivalent`. It has two functions the compilers' links drop (nothing calls them
  there, though their strings, ".lib|.a" and "lib%s%*.*s", stay): `ParserHelpers_AddUnixLibraryFile` (0x40cfbc,
  `-l`: libNAME with each extension, then NAME, in the access paths, or message 0x31) and `ParserHelpers_AddFileList`
  (0x40d0fe: a list of files added to the project, a directory's message 0x10 and a missing file's 0x2c). Both are the
  original's bytes; they are written under `#if defined(LINKER) && VERSION >= VERSION_GC_2_7`.
  `ParserHelpers_ParsePathList` of the 2.x linker passes a kind of path, `(option == 1 ? 1 : 0) | (mode != 0 ? 2 :
  0)`, for `option`; no spelling tried gives the original's registers (it keeps the kind in memory). The compilers
  and LD_1_0 preprocess it to the same tokens as before.
- 14 new scenarios, `library-search-*` and `library-files-*`, link library-basic's program against its archive
  found by `-l` (as libNAME.lib, libNAME.a, NAME itself, missing, too long a name) in `-L` paths (one, and a list with
  a missing directory), in `MWLibraries` and `MWEABIPPCLibraries` (the latter first), with two archives of the same
  name to show the order (`-L` paths before the environment's, either way round), and named by `MWLibraryFiles` (one,
  a `;` list with missing files, a `:` list, a directory). The environment's paths and files need the link without
  `-nodefaults`.
- The sabotage was 6 breaks (in the three functions: their first comparison turned round, their first constant made
  one more), against those scenarios and the driver's. Seen: `-l`'s extensions cut at the first character (6
  scenarios) and the file list's separator turned round (3). Not seen: the long name's limit one more (249 is as
  long as 248), the file list's length limit one more, and the kind's `option == 1` turned round or made `option ==
  2` (it chooses the system or the user list, and the `-L` paths come before the environment's in either).

`src/linker/ld-eabi-ppc-mw.c` is new and `matching`: the 2.x linker's static plugin, "Embedded PPC Linker", where the
compilers' cc-eabi-ppc-mw.c has the compiler's (0x40bf80 to 0x40c035, with its data 0x476780 to 0x478fb8, which the
split named cc-eabi-ppc-mw.c after GC_2_7's). It has the compiler plugin's getters, one plugin (subtype 'Link',
version 12; files .doc, .lcf, untyped text and .bin; five panels, "PPC EABI CodeGen" to "CmdLine Extras EPPC"; the
version 2.4.1 build 32 as four shorts; the archive and ELF signatures, the ELF one's length 0x104), its entry
`fn_0041e670` (in the range the split gives libimp-eabi-ppc.c), and the "Linker Errors" strings (resource 11001).
Those are the compilers' 136 with message 49 the small data relocation's and seven more (the missing command file,
the compressed sections, the entry point's section, the small data thresholds, an alignment not a power of 2). The
entries are in `include/driver/LinkerErrors.h` now, which cc-eabi-ppc-mw.c includes: the compilers and LD_1_0
preprocess it to the same tokens as before, and the linker's differences are under `#if defined(LINKER) && VERSION >=
VERSION_GC_2_7`. 0x40bfb0 and 0x40bfc0 are `copy_global_to_value` and `get_stored_value`. Every scenario goes through
the plugin's registration; as a `matching` unit the exact build checks it.
- The sabotage was 8 breaks in lib/msl's sources, against all the scenarios. Seen: `__write_file` taking a full
  write for an error (every scenario), `fopen` opening for reading, `open` always translating newlines,
  `__flush_buffer` writing a byte less and `__find_unopened_file` never finding a free FILE (the dumps to a file).
  Not seen: `__prep_buffer`'s buffer a byte shorter (the output is the same), `__flush_all` skipping the open files
  (the messages are flushed as they are written) and stdout as handle 2 (the scenarios take stdout and stderr
  together).

`src/driver/libimp-eabi-ppc.c` is `matching`, from 0x420370 to 0x421270 (the split had given it 0x41dcc0 too: that is
the linker plugin's glue, `src/linker/ld-eabi-ppc.c` now, with its data to 0x4830d0 and its `.bss` to 0x499be8, still
the original's bytes). The reference was [mweppc](https://github.com/doldecomp/mweppc), whose `src/driver/ELF_Lib_Import.c` is the
2.x compilers' matching importer (`IsELF`, `ImportObjectLibrary`, `ELF_Lib_Import_setup_param_block`,
`store_compile_results`, `ELFImport_main`, `Linker_UserBreakCheck`, `IsMWLibraryFile`, `Link_GetFileSpecTextHandle`,
`Link_GetFileText`, `DeleteArchiveInfo`, `Inspect_ELF_Header`, `Import_Object_File`, `Get_AR_Name`,
`term_ar_string_table`, `Import_Archive`, `Import_File`, in that order). Ours was the 1.3 compiler's; the 2.x
differences are under `#if VERSION >= VERSION_GC_2_7` (the 2.7 compiler's importer has them too: compiled for it, all
21 functions are its code, where 15 were):

- the import returns a new handle of the file's first 8 bytes (the archive signature, or the object's own), and the
  heap is the entry point's (`initheaps` before it, `releasegheap` after); `copts` is 264 bytes;
- the handles are freed with `COS_FreeHandle`, the reader's extra bytes are an `SInt32`, and `Inspect_ELF_Header`
  reports the wrong machine directly (no inline helpers); the 1.3 pragmas around the reader and the header check
  are not the 2.x's;
- the CU is laid out natively (CPrep.h leaves `options align = mac68k` on: the 1.3 layout has the main file at 0x92,
  2.x at 0x94), its browse options 4-aligned, and ends with the target's name (0x10c bytes; addresses are masked in
  function comparisons, so only the `.bss` size showed it); the break check's tick count has no initializer.

The linker's alone, under `#if defined(LINKER) && VERSION >= VERSION_GC_2_7`: `initialize_plugin_context` first
points `active_plugin_cu` (0x4a2900) at the importer's CU; the glue's functions reach the active plugin's context
through it. The compilers' message functions (`fn_0041e970` and the like) are not in the linker: LibImportMessages.c
calls the glue's copies there, so the link drops them.

Two names were wrong in the linker's configuration, and the linker's own units used them: 0x42e950 is `galloc` (the
`galloc_pool`'s allocator; `oalloc` is the same code for another pool, and the linker has no other), and COSToolsCLT.c
has one more function in the linker than in the compilers, `COS_GetHandleSize` (0x42efc0, before the locks), so
0x42efd0 to 0x42eff0 are `COS_LockHandle`, `COS_LockHandleHi` and `COS_UnlockHandle`. Archive.c, PrepDumpELF.c and
the linker's other units call them by those names now, and ELF_linker.c calls `COS_GetHandleSize` where it called
`Memory_GetHandleSize` itself. The compilers preprocess the shared sources to the same tokens as before at VERSION 0
to 6 (and 0 with LINKER); at 7 libimp-eabi-ppc.c and the units that include its header (libimp-eabi-ppc-mw.c,
LibImportMessages.c, ELF_Endian.c: the CU's layout) differ on purpose, and GC_2_7 still builds the original.

- The sabotage was 4 breaks in the importer, against all the scenarios. Seen: an archive not taken for one (20
  scenarios: its message 0x1e) and the PowerPC machine refused (220: message 54 for every object). Not seen: the
  import's handle a byte short and the code size one more (they are the object data the IDE shows; the command line
  prints neither).

mweppc's sources rank the rest: compiled as its GC_2_6 build does (its compilers and flags, its include
directories, VERSION 3), their functions give 90% to 100% of the code of most of the driver's units that do not
link (Arguments.c, Utils.c, Option.c, ErrMgr.c, CLToolExec.c, the V10 dropin, Resources.c, CLPlugins.c...), where
ours give from 0% (CLStaticMain.c) to the same. Two more from them:

- `src/driver/CLStaticMain.c` is `matching`: the 2.x `main` returns its status (1.3's calls `exit`), and is built
  without instruction scheduling (mweppc's `-opt noschedule`; here `#pragma scheduling off`). `build_date` is
  0x46e01c (its binding was `data_0053601c`).
- `src/runtime/arrcondes.cpp` is `matching`: the 2.x tools' runtime is the Pro 5.3 updater's (`lib/runtime53`, as
  setupargs.c's is for every version), so its wrapper includes that one at VERSION 7 (`version.h` for the
  version's macros: it has no tokens). Only `__destroy_new_array` is linked: the exceptions' deleting destructors
  call it.
- Both preprocess to the same tokens as before at VERSION 0 to 6 (and 0 with LINKER). The sabotage: `main`'s
  status one more (every scenario) and the user break's message for status 1 (62, the errors) are seen; no
  destructor calls in `__destroy_new_array` is not (it runs only after a failed `new`).

Four more, `matching`:

- `src/driver/ErrMgr.c`: the 2.x table of system errors has -113, "invalid handle detected". Its data starts at
  0x489ac8; 0x489aa0 to 0x489ac8 (the new handler's pointer, "bad_alloc", "exception") is NewHand.cpp's
  (`data_units`).
- `src/driver/Utils.c`: the 2.x `Utils_FormatOptions` is mweppc's `Utils_SpellList`, written as it is there.
- `src/driver/Arguments.c`: the 2.x token dispatch (`fn_0040fbe1`, 0x41b5b5, its name in functions.json now) is a
  switch whose default is a fatal "Unknown token (%d)" (Arguments.c, line 787). Its statics then still differed: an
  `int` after a `char` at 2. CPrep.h leaves `options align = mac68k` on (its pushes are one more than its pops: the
  reset after `CompilerLinkerOptions` pops its `packed`, not the `mac68k` before it), and the alignment of a unit's
  variables is the one in effect at its end. At VERSION 7, CPrep.h pops it at its end now: no other unit changed,
  and the exact build and every scenario are the same.
- `src/runtime/runinit.c` gives the original with them.
- The compilers preprocess the three sources to the same tokens as before at VERSION 0 to 6. The sabotage: the
  help's sticky marker (10 scenarios) and its [no] prefix (12) seen; the `=` and `,` tokens changed not seen (they
  are only in the arguments handed to a plugin), nor ErrMgr.c's texts for -43 and -113 (no scenario prints a
  system error's text).

Seven more give the original, `matching`: maincrt.c, BuildDate.c, WarningHelpers.c, ToolHelpers.c, CLSegs.c,
extras.c and ThreadLocalData.c. What they needed:

- `src/driver/BuildDate.c`: the 2.x linker's build date and time ("Jul 14 2003", "14:20:31"), under LINKER at
  VERSION 7. Its data (0x46e000 to 0x46e020) is a unit of its own before maincrt.c (`data_units`, "before"), and
  maincrt.c's starts at 0x46e020.
- `src/driver/WarningHelpers.c`: the linker's `-w display` lists the linker's warnings
  (`ToolHelpers_ld_PrintLinkerWarningOptions`), not the C language's.
- `src/msl/extras.c`: its data is 0x4745d8 to 0x474630 (wchar_io.c's ended at 0x474608, Resources.c's started
  there), two `data_units` ranges, one per unit the references had given it to.
- maincrt.c's `_CRTStartup` (0x401de0) and `_pCmdLine` (0x46f840) are named; the split puts 0x401dd0 to 0x401fb0
  (`_CRTStartup`) in startup.win32.c now, out of ThreadLocalData.c, which then gives the original.
- `tools/derive_splits.py`: a name only decorates a function when an external symbol has it (ParserFace.c's static
  cdecl `xmalloc` had left the driver's `xmalloc@8` undecorated), and the symbols are named with the units' order
  before the data units go in. A data unit put early (BuildDate.c, first) had shifted every unit's index by one: the
  labels that other units refer to became local, and the others global (and alloc.c's label, global then, was in both
  of its .bss ranges). Older data units had shifted the units after them the same way.
- The compilers preprocess BuildDate.c and WarningHelpers.c to the same tokens as before at VERSION 0 to 6. The
  sabotage: the build date a day later (the version and the help), "no warnings at all" changed and the linker's
  list dropped (`-w off,display`) are seen.

`src/driver/Option.c` is `matching`, from mweppc's `Options_DoParse` and its statics (at VERSION 7):

- `parse_option_list` clears its match flags first (`definitionFlags`, read after a token that matched no option),
  skips the token of a nameless list's default only when the default has a name, and counts each error in its own
  branch (1.3's share one count through `do { } while (0)`, which the 2.x code is not laid out as).
- The options as they are added are a static list (`internalset`), not two globals: Option.c's .bss is 0x98 bytes
  (two lists and the 32 list pointers).
- The sort of the options (`Option_FinalizeOptionLists`) comes before `add_option`.
- The compilers preprocess it to the same tokens as before at VERSION 0 to 6 (and 0 with LINKER). Three scenarios
  give a list an empty first entry (`-w ,display`, `-help ,usage`, `-show ,only`). The sabotage: the match flags
  not cleared is seen (those three). Not seen: the nameless default's token, and an unknown option or a missing
  argument not counted as an error (the error's message already fails the run, and the list's parse stops there
  either way).

Three more, `matching`, from mweppc (at VERSION 7):

- `src/driver/Generic.c`: `OS_MatchPath` (the wildcards of an input's name, ParserHelpers.c's) takes names of 255
  characters (1.3's 63): its buffers are 256 bytes.
- `src/driver/CLLoadAndCache.c`: the conversion of a text's lone LFs to CRs (`CLPrefs_ConvertLoneLFToCR`, mweppc's
  `FixTextHandle`) is its first function, not CLPrefs.c's (functions.json's source too). Its data ends at 0x489a68:
  0x489a68 to 0x489a78 ("!std::bad_alloc!") is NewHand.cpp's (`data_units`).
- `src/driver/Files.c`: `FSMakeFSSpec` keeps the name of a file it cannot make a spec of (cut to 63 characters);
  1.3's clears it. The linker's has a flush of a volume that does nothing (`fn_00412de0`, 4 bytes, which the ELF
  writer calls; under LINKER), and the unused "%*.*s" is Memory.c's in 2.x, not Files.c's.
- The compilers preprocess the three (and CLPrefs.c) to the same tokens as before at VERSION 0 to 6. Eight
  scenarios: inputs named by wildcards (`*.o`, `?.o`, a pattern of 64 characters, one that matches nothing), and
  command files with Unix and Mac ends of line (with and without an error). The sabotage: the wildcard cut at 63
  characters is seen (the long pattern; its whole path must stay under 260 characters, so OUT's under 190 or so). Not
  seen: a lone LF kept, or made a space (the linker reads its command file without the text loader), the missing
  file's name cut at 62, and the flush failing (its result is not used).

Two more:

- `src/driver/ToolHelpers-cc.c` is `matching`: the 2.x linker has none of the compiler's "Calling linker option"
  stub and its strings (under LINKER at VERSION 7); its data is "CWParserSetOutputFileDirectory" alone. The
  sabotage (the two "given twice" messages of the compiler's `-o`) is not seen: the linker's own option tables do
  not use these handlers.
- `src/msl/startup.win32.c` (`_CRTStartup`, 0x401dd0 to 0x401fb0, and `__get_MSL_init_count`, their source in
  functions.json now) is `equivalent`: the code differs from the original only in its registers (the same size and
  operations; another build of MSL's). The sabotage: stderr's handle that of stdout is seen (the unclosed brace's
  error, 1 scenario); stdout translated and the first environment string dropped are not.

Three more, `matching`, from mweppc (at VERSION 7):

- `src/driver/Parameter.c`: `format_num_parm` (mweppc's `DescHelpParam_Number`) clears the name and the value
  apart and gives each case of the default its own branch.
- `src/driver/MacSpecs.c`: mweppc's directory map (`AddDirMapEntry`, `FindOrAdd`, `ResolveVolDir`,
  `OS_OSSpec_To_FSSpec`). `FindOrAdd` refuses an empty path and stops at the last component (1.3's stepped past the
  end of the string), and a name of 64 characters or more becomes its short name (`OS_ShortenSpec`, 0x411ca0, a
  new function, through `GetShortPathNameA`) where 1.3's failed. Its directory buffers are two of 260 (1.3's one of
  520).
- `src/driver/CLToolExec.c`: the linker's command line passes a disassembly's output file with `-o` when it is
  written to disk, and the verbose command line is set off by blank lines. 0x418df0 is CLFileOps.cpp's
  `fn_00419e90` (its 1.3 name), and `optsCmdLine` and `optsCompiler` are bound (0x4a2910, 0x4a2a94).
- The compilers preprocess the three to the same tokens as before at VERSION 0 to 6. The sabotage: the number's
  name and its default in decimal (the help, 10 scenarios each), the long name not shortened (the 64-character
  wildcard, 2) and the directory cut at 16 characters (252) are seen. Not seen: the empty path taken, and the two
  CLToolExec.c breaks (the linker runs no linker of its own).

`src/driver/CLPrefs.c` is `matching` too. In 2.x the case-blind string compares are StringExtras.c's (mweppc's
`ustrcmp` and `ustrncmp`: `CLIO_CompareStringsIgnoreCase` at 0x4154c0, `fn_004050e0` at 0x415510; 1.3's are CLIO.c's
and ClientGlue.c's), and the new preference panel (`create_data_block`, mweppc's `PrefPanel_New`) is CLPrefs.c's
first function, not CLIO.c's; functions.json gives them those sources. StringExtras.c still gives the original. The
compilers preprocess the four files to the same tokens as before at VERSION 0 to 6. The sabotage: a compare that
finds any two strings equal is seen (215 scenarios); a case-sensitive compare, a prefix compare past the end, and a
panel's data a byte short are not. A panel's data not copied at all slowed the suite past its 25-minute limit, so that
run gave no verdict.

`src/linker/ld-eabi-ppc.c`, the linker plugin's glue (0x41dcc0 to 0x420370), is `equivalent`, written from the
original's code (there is no source of it):
- The entry, `fn_0041e670`: a link (request 0; the license first, as the compiler's), a disassembly (1) or the target's
  output for the IDE (2). It reads the five panels ("PPC EABI Project", "Linker", "CodeGen", "Disassembler" and
  "CmdLine Extras EPPC") into statics, then the linker's settings from them: the output kind, the addresses and sizes
  when there is no command file, the ROM image, the S-record and map files (named after the output, `.mot` and
  `.MAP`, when the panel names none) and the processor. The CU it points `active_plugin_cu` at is its own
  `linker_cu` (0x4a2784, bound).
- The processor's number maps by the CodeGen panel's version: each version's switch falls into the next older one's
  (the jumps say so, not Ghidra), so from version 3 to 9 the 1-2 table decides. The 2.6 driver's panel is version 11,
  which takes the number as it is: the cascade is not reached from the command line.
- The project's files: an object or an archive read from the disk into 14-byte members (`ELMember`, the importer's
  `FileInputNode` packed), the IDE's data of a file, a file linked as its bytes, a file sharing another's data
  (`EL_FILE_DUPLICATE`, 0x08; `ELFile` now names its data's id, its index and the original), and a linker command
  file's text. Then the messages and break checks the linker's code calls.
- The driver's helpers it calls are still in the original's units (`fn_00413620`, a Toolbox `EqualString`;
  `fn_0041a460`, `fn_0041d800`, `fn_0042f0d0`, `fn_0042f280`, `fn_0042f400`, `fn_0042f570`); their decorations come
  from this unit's object.
- `tools/linker_equivalence.py` has a new family, `addr-*`: links without a command file, with each address and size
  the glue copies, a ROM image, the unused symbols, and the map and S-record files named after the output (14
  scenarios, identical).
- The sabotage (against the `addr`, S-record, library and dump scenarios): the code address ignored (4 scenarios), the
  stack address not rounded (1), the S-record length off by one (13), an archive's pad byte not skipped (5), the heap
  size halved (31), the unused symbols not listed (1), the ROM address dropped (1) and the disassembly always showing
  code (14) are seen. Not seen: the second small data address dropped (the original's output does not change with
  `-sdata2addr` either) and an object read from the disk left to be linked whole.

`src/linker/GetTicks.c` is `matching`: 2.x has `CompilerTools_GetTicks` (0x4434f0) in a unit of its own among the
linker's (functions.json's `unit`), and COSToolsCLT.c leaves it out for the 2.x linker. A frozen tick count is not seen
(it only paces the IDE's break checks).

The rest of MSL is `equivalent`: printf.c, alloc.c, wchar_io.c, pool_alloc.win32.c, file_pos.c, time.c, wctype.c,
wscanf.c, scanf.c, locale.c, and direct_io.c, whose `fread` and `fwrite` (0x405cf0 and 0x405fc0, unnamed until now and
counted in wchar_io.c's range) functions.json names. The suite gives 297 identical scenarios with them. The sabotage
(the library's sources: `lib/extra/printf.c` is the one printf.c includes): hex printed as octal (7 of 7 scenarios
tried), `sscanf`'s decimal read as hex (the archive members' sizes: 2) and `fwrite` writing nothing to the console (6)
are seen.

The import stubs (`version.dll_stubs_*`, `lmgr326b`) cannot leave the equivalent link yet: the linker makes a stub
for an import only when the reference to it is a function's, as the compiler's objects mark them and the split ones
do not, so they go when their callers (ResourceStrings.c, CLLicenses.c) come from source. `configure.py` leaves an
`equivalent` unit with no source out of that link.

The C++ runtime's ExceptionX86.cpp, newop.cpp and NewHand.cpp, the runtime's setupargs.c, and the driver's
ClientGlue.c, MemUtils.c and CLOverlays.c are `equivalent`. Two functions are elsewhere in 2.x: `fn_00412660` (a
case-blind string compare, stdcall) is MsDos.c's, after `OS_MakeSpec`, and `MemUtils_CallPluginEntry` is
CLPluginRequests.cpp's, before `initialize_plugin_request`; ClientGlue.c and MemUtils.c leave them out for 2.x.
NewHand.cpp has the `bad_alloc` flag (`throwsbad.cpp`) in the 2.x linker, which newop.cpp's code reads. The suite gives
297 identical scenarios. The sabotage: the command line never split into arguments (setupargs.c; the build includes
`lib/runtime53`'s copy, and ninja has no header dependencies, so the wrapper is touched) is seen in 2 of 2 scenarios.

The rest of the driver and the frontend's units the linker has are `equivalent`: Resources.c,
ResourceStrings.c, CLPlugins.c, CLMain.c, ParserErrors.c, MsDos.c, StringUtils.c, TextUtils.c, CLIO.c,
CLPluginRequests.cpp, CLLicenses.c, CLFileOps.cpp, DropInCompilerLinkerPrivate.c, CWParserPluginsPrivate.c,
CWPluginsPrivate.c, Projects.c, CLFiles.c, CLDropinCallbacks_V10.cpp, CLCompilerLinkerDropin_V10.cpp, CLAccessPaths.c,
CLDependencies.c, CLWriteObjectFile.c, CLBrowser.c, CompilerTools.c, COSToolsCLT.c, CPrep.c and Unmangle.c. The 2.x
linker's driver is newer than the 2.7 compiler's our sources match; what it has that they lack, under
`defined(LINKER) && VERSION >= VERSION_GC_2_7`:
- data of their units the sources only declared (`data_005d89be`, the version resource, in ResourceStrings.c;
  `data_005d89e5` and `data_005d89e6` in CLMain.c; `data_005d9494` in MsDos.c), and `optsCmdLine`, `optsEnvir` and
  `optsCompiler` in the COMMON block, their versions 0;
- CWParserPluginsPrivate13.cpp's `GetPanelExtra`, `SetParserEntry` and `CallParserCallback3` in CWParserPluginsPrivate.c;
- the parser's callbacks with 2.x's second one, the swap of the user paths not kept as system ones (CLFileOps.cpp;
  nothing in the linker calls it, but the later entries are found by their place), and `add_access_path` and
  `ToolHelpers_cc_AddAccessPath` reading their flags bit by bit (bit 1: kept as a system path);
- `UCBFreeObjectData` freeing the object data itself, as 1.x does;
- the banner from the version resource the shell gives the parser (`ToolHelpers_cc_PrintVersion`; the compiler's
  sources had the resource's query paths without their backslashes, fixed for every version);
- the helpers the linker plugin's glue calls: an output name built from a default and the project kind's extension
  (`fn_0040d952`, ParserErrors.c), `EqualString` (TextUtils.c; without case it compares from the length byte, so not
  the last character), a bounded Pascal copy (StringUtils.c), the shell's file data callback (DropIn...Private.c),
  `HOpenRF` (Resources.c), `FSMakeFSSpec` (COSToolsCLT.c), and in CPrep.c two file specs compared, `CWSetTargetInfo`
  by the API version and a file's data id; `CWPluginsPrivate_GetOutputFileDirectory`, `AppendGListByte` and
  `Unmangle_UnmangleSymbolName` (functions.json names them) where the glue had their addresses; and the unmangler's
  check of the symbol prefix (empty in the linker).

The sabotage: the banner without the version resource is seen (3 of 3 `driver-*` scenarios tried); the swap made a
no-op is not (nothing calls it).

Of the 110 functions of these units whose bytes are not the original's, compared by their calls, data and immediates
(the comparison of each unit's functions with the original's disassembly, by five reviews of a few units each), about
half differed beyond the code generation: newer 2.x code, which mweppc's driver mostly has. Ported, under the same
condition:
- MsDos.c: `OS_IsFullPath` with the cygdrive prefix (`MW_CYGDRIVE_PREFIX`) and `//x/` paths, names of 255 characters
  (`OS_MakeNameSpec`, `OS_ReadDir`'s long names), and one more error code; CLIO.c: line ends and the flush at `-v -v`
  in its text output, gcc's level names and a `#warning`'s or `#error`'s source line in its messages;
- CLPlugins.c: plugins of API version 11, their requests carrying the plugin's version text (the banner reads it), the
  unsupported linker flags 0x5fe4000, a file mapping of no type printed `<none>`, and a plugin with no command-line
  callbacks given every argument; CLMain.c: the CmdLine panel upgraded from 0x1000 and 0x1001 step by step, warned
  about once and only at `-plugindebug`, and two messages of 2.x's table (73 with the file's fate as an argument, 106);
- CLFileOps.cpp: "All specified files were ignored" not failing the link, the files and access paths listed at
  `-v -v`, output names of 255 characters and a kept object's name; CLPluginRequests.cpp: the main text released after
  a request; CLAccessPaths.c: a subdirectory's name buffer of 256 and the skip of `(...)` directories only;
  CLDependencies.c: the cached records' shortcuts and the include stack's path (empty in the linker);
- CLDropinCallbacks_V10.cpp: a file already in the project ignored (message 106 at `-v -v`), message 73 saying whether
  the file goes to the linker, the main text not freed by the plugin's release, a new text document written to its
  file (or `-stdout`'s), the license flags; CLCompilerLinkerDropin_V10.cpp: its internal errors' lines; CLBrowser.c:
  the precompiled text's place in the include cache; Unmangle.c: no vector types, and a function's return type after
  `_` skipped.

The linker's parser glue is its own, `src/linker/ParserGlue-eabi-ppc-ld.c` (`equivalent`; the units had the
compiler's in its place): its option tables are written by `tools/gen_options.py` (a port of mweppc's generator) from
the original's data, its panels' structs from `include/driver/PrefPanels.h` (PProject's project kind and output name
named), and the three parse steps by hand: the system library paths before the files, and after them the output name
by the project's kind (`fn_0040d952`), the `-strip_partial` check, the system libraries (%MWEABIPPCLibraryFiles% or
%MWLibraryFiles%) and the default entry point. TargetWarningHelpers-ppc-ld.c reads the linker panel's field for its
warnings. The suite gives 304 identical scenarios: `wildcard-long` and `dump-not-elf` now among them, and new ones for
an object given twice (`link-object-twice`, with `-v -v`), a file no plugin maps, an unresolved C++ name, gcc's message
style (a short message: gcc's lines start with the linker's path, whose length the wrapping counts) and `-plugindebug`.
The sabotage: the library kind taken as a partial link is seen in 2 of 2 `library-*` scenarios tried, no system library
paths in 5 of 5, the duplicate check removed in 2 of 3 (not `link-duplicate`, of two sources).

The equivalent executable's resources are source too: `src/linker/mwldeppc.rc` (config.json `"resources"`), compiled
by LLVM's `llvm-rc` (`-C 1252`; configure finds it on the path or in LLVM's default folder) into the same version
information bytes as the original's. The original's icon is left out: nothing reads it. Its version.dll and license
library calls go through the import libraries' pointers (`VERSION_IMPORT` in `include/compiler/win32.h` and
`LMGR_IMPORT` in `include/driver/CLLicenses.h`, `__declspec(dllimport)` in the 2.x linker only), so their stub units
are listed `equivalent` with no source and left out. The sabotage: another copyright year, or another company name, in
the resource is seen in 2 of 3 `driver-*` scenarios tried (the version number is the plugin's, not the resource's).

The COMMON block, the uninitialized globals the units only declare (or define tentatively), is source as well:
`src/linker/common.s` (config.json `"assembly"`, a link input and its source), assembled by clang for i386 COFF. `tools/common_source.py` writes it
from splits.txt's `common` ranges and symbols.txt: every name of the original's block at its offset, as large as the
room up to the next name, zeros. The layout is the original's because the units' code relies on it: ParserFace.c's
`initialize_cmdline_environment` clears 0x198 bytes from `pluginPrivateContext` on (the parser's state, `pTool`
among it), DumpELF.c clears the disassembler's 0x5a bytes of settings from `data_004a2d6e`, ELF_PPC_EABI.c the
output's 0x62 bytes of state from `data_004a2dd2`, the output file's name (`data_0049bed6`) is its spec's
(`data_0049bed0`) 6 bytes in, and the ELF header's `e_shstrndx` (`data_004a28ce`) is `data_004a289c`'s. Variables
defined each by the units' declared size and left to the linker's placement crash (`pTool` cleared). Without
`sections.obj` (config.json `"order_sections": false`) the equivalent link orders its sections itself, which changes
nothing it does. The sabotage: `e_shstrndx` moved out of the ELF header is seen in 5 of 5 scenarios tried; the
output file's name moved out of its spec in `link-output-unwritable` (a read-only output: "Can't write application
'readonly.elf'", the one message that names it), not in `dead-code`.

The two objects of the runtime library the link took as they ship are assembly too: `src/runtime/cexc.s`, the C++
exception table's bounds (an empty start in `.exc$AAA`, a -1 end in `.exc$CCC`), and `src/runtime/ccinit.s`, the
static initializers' (a null word in `.CRT$XCA` and one in `.CRT$XCZ`), the same sections, flags and names. Neither
can show in the suite: the linker has no static initializers (nothing between the bounds) and throws no C++
exception in any scenario; a -1 end made 0, or the initializers' start moved after them, is seen in none of 4
scenarios tried. The sections they bound are the original's: `.CRT` is the two null words alone in both executables,
`.exc` 55 records and the -1 in both.

The imports are assembly as well, in place of the import libraries: `src/runtime/imports/kernel32.s`, `version.s`
and `lmgr326b.s` hold each DLL's import tables themselves, the long form an import library's objects stand for (the
descriptor in `.idata$2`, the null descriptor in `.idata$3`, the lookup table in `.idata$4`, the address table in
`.idata$5` with each entry named `__imp__NAME`, the hints and names in `.idata$6`). `tools/import_source.py` writes
them from `config/kernel32_26.def` (the 2.7 compiler's list without `IsDBCSLeadByte`, which the 2.6 linker does not
import), `version.def` and `lmgr326b.def`. Every input of the equivalent link is now compiled or assembled from
`src`. The two executables import the same functions, by the same names and hints or ordinals (in another order of
entries). The sabotage: VerQueryValueA imported by GetFileVersionInfoA's name is seen in `driver-version`; the license
library's ordinals swapped in none of 3 (the tests' license library is a stub whose functions all succeed).

The two executables import the same functions of the same DLLs. The original's link kept time.c whole, `time()`
among it, which nothing calls (its one caller in the sources, CPrep.c's `initialize_preprocessor`, is in neither
executable); the equivalent link leaves out what nothing refers to, so time.c (in the linker only) keeps a pointer
to `time()`, and with it `__get_time` and its `GetLocalTime` import. The suite gives 305 identical scenarios, with
`link-output-unwritable`.

For the MSL file units (`equivalent`, above), mweppc builds the 2.x compilers' MSL with the real Pro 5.3 compiler and
the Pro 5.3 updater's MSL sources (`lib`: the Pro 5 archive's, overlaid). That does not give the linker's either: the
linker's MSL code is another build (its `__prep_buffer` keeps the file in `ebx` and rotates the scratch registers;
`-opt nocolor` gives the first but not the second, and no `-opt` level or sub-option of pro5 gives more functions
than the default).

## The 2.7 linker, equivalent

`config/LD_2_7` builds the 2.7 linker (3.0.4) from source alone: its config's `"units_from": "LD_2_6"` takes 2.6's
units, which `configure.py` (mode `source`, the only one for it) compiles for 2.7, assembles where the config's
`"assembly"` gives a unit's source (the COMMON block, the runtime's table bounds), and links with the imports'
assembly (`"imports"`) and the resources (`src/linker/mwldeppc27.rc`, 2.7's version information, 3.0.4). There is
no split of the 2.7 executable and no exact build: the 2.7 linker was built by a later toolchain (above), so
`build/LD_2_7/equivalent/mwldeppc.exe` is checked only by what it does:

```
python configure.py --version LD_2_7
ninja
MWCIncludes=include/libc python tools/linker_equivalence.py build/compilers/GC/2.7 build/LD_2_7/equivalent/mwldeppc.exe OUT --runtime DIR
```

The sources tell the linkers apart by `LINKER`: `configure.py` passes `-DLINKER=NN` for the linker of GC N.N
(`LINKER=10`, `13`, `26`, `27`; the compilers have none), so 2.7's behaviour is under `#if LINKER >= 27` and 2.6's stays as
it is. The 2.6 and 1.0 exact builds and the 2.6 suite (305 identical) are unchanged by it.

What 2.7 does differently, from the suite (at first, 2.6's behaviour gave 70 of 306 scenarios the 2.7 linker's; now 320 of 324):
the link map (a file offset column; no symbol closure unless `-listclosure`; `*fill*` rows), the sections a link
without a command file makes (`.ctors`, `.dtors` with their `$00`/`$99` bounds, where 2.6 puts them in `.rodata`),
the disassembler's register names and processor masks (the e500's registers: SVR, IVOR32), its messages, `-help`
(new options: `-listclosure`, `-listdwarf`, `-sortsrec`, `-linkmode`, `-abi`, `-tune_relocations`, `-char`,
`-gdwarf-2`, processors), the library search and archives. The original crashes on `link-includedwarf`
(0xc0000005). Done so far:

- `DumpELF_ExceptionTable` prints the whole "Saved Registers:" label (2.6 is one space short).
- The banner (`BuildDate.c`'s date, the resources' 3.0.4) and the usage line `-version` prints before the help hint
  (`Projects.c`).
- The command line: `src/linker/ParserGlue-eabi-ppc-ld27.c` replaces 2.6's glue (the config's `"replace"` maps a
  unit of `units_from` to the one built in its place). Its option tables are `tools/gen_options.py`'s, from the 2.7
  executable (`--define LINKER=27` lays out the panels as 2.7's, `include/driver/PrefPanels.h`: the Linker panel's
  `linkmode`, `listdwarf`, `listclosure` and `sortsrec`, the Project panel's 256-byte output name, `abi`, `dwarf`,
  `tunerelocations` and `abiconfig`, the Extras panel's binary file name; the CodeGen panel and the new "C/C++
  Compiler" panel are raw bytes). Before the options, 2.7 puts `%MWLinkEABIPPCOptions%` (or `%MWLinkOptions%`)
  among the command line's (`ParserHelpers_AddEnvironmentOptions`). Every `driver-*` help scenario is now 2.7's.
- The plugin: `src/linker/ld-eabi-ppc27.c` replaces `ld-eabi-ppc.c`. It reads the Project panel as 2.7's (0x244
  bytes; the output's name at +0x3b from its version 8, older than 9 reported as old), and rounds the stack and heap
  addresses to 16 bytes (2.6: 8). The panels' new settings (ABI, DWARF 2, `-listclosure`...) go in with the code
  that reads them.
- The disassembler: `src/linker/PPCDisassembler27.c` replaces `PPCDisassembler.c` (its data is the 2.7
  executable's, `tools/gen_disdata.py`, a packed struct, as 2.7 aligns its objects on 2 bytes). 2.7 knows
  processor masks up to 0x8000000 (none: 0xff6fffd), the e500's SPE, cache, TLB and vector instructions (isel, evsel,
  mbar, icbtls, dstte...), the 405's multiply-accumulate instructions, the context instructions, more special and
  device control registers with mnemonics of their own, and gqrN for the psq instructions. `DumpELFCodePPC.c` has
  2.7's register tables and processor masks under `#if LINKER >= 27`. Every `sweep-*` and `dump-*` scenario is 2.7's
  but `dump-rom`, `dump-rom-default` (the link itself) and `dump-not-elf` (the driver's message); 2.7 has no
  `-proc e500`, so `sweep-e500*` compare its usage error (the suite now drops the linker's path from sweeps).
- The constructors (`ELF_linker.c`, `ELF_PPC_EABI.c`): 2.7 has a "Linker Generated Symbol File", an object it
  makes after the link's (its section table is at 0x497f84, 0x2e bytes an entry). `EL_LoadGenerated` makes the part
  of it the suite sees: `.ctors$00` with `_ctors` and `.ctors$99` with `_ctors$99` (a zero word), the same for
  `.dtors`. A section `NAME$NN` (two digits) is an input of `NAME`'s: it goes where `NAME` would, shares `NAME`'s
  item, and is placed among its item's sections by NN (0 first; 50 for any other name). 2.6's linker-defined
  `_ctors`/`_dtors` and its `__init_cpp_exceptions.o` items are gone. The dead stripping keeps `_ctors` when the
  `.ctors` roots kept something new or an exception root was kept, and a terminator with its start. A `.ctors` or
  `.dtors` chunk is rooted by its named symbols (a section symbol only when it is alone), so a losing weak duplicate
  keeps nothing.
- The other generated pieces, kept as 2.6's linker symbols: `_eti_init_info` shows as the generated file's
  extabindex piece, and keeping it writes extabindex even empty; a command file's `.init` takes its type. The
  symbols list has 2.7's table (`SIZEOF_HEADERS`, the file offset after the program headers, and `_ABS_SDA_BASE_`,
  `_ABS_SDA2_BASE_`). A link without a command file has 2.7's `.got` (no layout in the map).
- The link map: file offsets (an archive member by its name in the archive), `*fill*` rows for the alignments, the
  closure only with `-listclosure`, DWARF layouts only with `-listdwarf`, debug rows only with contents, a layout
  for every command file section. Empty sections keep their aligned file offset and ROM address.
- The output: section sizes rounded to a word, a command file's `ALIGN` in the section header too, relocations only
  from objects with procedure records (`.mwcats`), written before the records.
- The output: an executable has no DWARF relocations and no symbols of the DWARF sections (only a partial link
  has them); its S-records fill their lines across a section's pieces (`SRecord_Flush` ends a section's last).
- A partial link: relocations of every object, the `.comment` after them (2.7's header: version 13, 3.0, the char
  signedness at +0x16; the generated symbols word aligned, a weak one's flags cleared, an undefined one byte
  aligned), each piece's relocations by offset, extabindex's after its inputs', the DWARF sections' after the
  others' in their headers' order (none with `INCLUDEDWARF`), zeros up to an empty section's alignment. The symbols:
  an undefined one among its object's, the generated file's after the command file's. An archive member's date is
  the time it was made (the suite blanks it).
- The command file: BSS may go in a section of data (written as zeros) when no section names it; an output section
  named like a section takes it from another's `*(name)` item, another file's `file (name)` item takes it after
  that file's; a small data section takes the standard sections an item names, and warns about data in BSS (2.7's
  message, with the file). `ALIGN(n)` in an expression; message 164 for a section not allowed in another;
  a linker-defined symbol's expression evaluated each time (an assignment's three times: laid out, written, used,
  so an undefined name is reported three times). A segment's flags are all its inputs'. An empty
  section's ROM address is aligned past the padding its file offset takes. Without `-sdata2addr`, `.sdata2` is in
  the code's memory. 2.7's texts of the shared messages, its `UNUSED` map line.
- `_rom_copy_info` and `_bss_init_info`, when made, are the Linker Generated Symbol File's `.init` piece (with its
  section symbol, last of the locals, and its map lines), after `_SDA2_BASE_` in the symbols. A small data
  relocation to a linker generated symbol (absolute) is message 49's. An empty segment is at its aligned address.
- A link's output file is made after the layout's preparation (so the kept warnings come first). The linker
  plugin maps no other text files (they are passed to the linker). A partial link keeps `_ctors` and `_dtors` with
  any kept entry. An object's own definition that an excluded file's overrode is kept, as a global of no type and
  no `.comment` flags.
- A FORCEFILES object's `.comment` entries have no alignment (flags 0xff). `-v` says the writing of each section
  written (the string and symbol tables too, the procedure records only with some). The plugin asks for the
  `C/C++ Compiler` panel too.
- A GROUP's section is placed past the padding its file offset takes, then aligned. A ROM image: each loaded
  section at its ROM address in its header, its RAM segment without file contents, BSS with one segment, an empty
  section at the ROM's next address, `.PPC.EMB.seginfo` byte aligned and written last. An S-record that padding
  filled takes the next piece at its next address.
- "All specified files were ignored" is a warning. A prelinked file's section symbol whose pieces are all dropped is
  not laid out (its value stays 0); the padding between two chunks of one section is `**fill**` in the map.
- A BIND address before its area's next address is message 96 (an overlap). An environment variable's missing
  directory is not reported. An output section (or its item) of another kind does not take a section by its name.
- A compile unit's code size (its `high_pc`) is that of its own chunk, not of the code kept after it (a prelinked
  object's units). The Linker Generated Symbol File's extabindex is placed like an object's (`EL_PlaceGeneratedIndex`),
  so a command file without one is reported for it when no object's was.
- The compliance check comes before the kept warnings; a missing `FORCEACTIVE` name is reported by the dead
  stripping, not when the names are kept.
- The messages: 2.7's table from 138 on (`include/driver/LinkerErrors.h`); the floating point (and char
  signedness, version) warnings are kept until the layout, one per file, an archive's members together
  (`EPEC_KeepWarning`, 2.7's 0x46c1f0).
- From Echoes' links (its `main.elf` and its modules' `-r`/`-r1` links, replayed with both linkers by a script of
  the scratchpad's; each has a `link-*` scenario): commons get `.bss` with its section symbol in partial links too
  (`ELF_linker_MakeCommonSections` always; the section symbol made when the objects are read). Without the C++
  runtime the Linker Generated Symbol File has a `.dtors$10` of its own, `__destroy_global_chain_reference` (a word
  relocated to `__destroy_global_chain`, made when `__fini_cpp_exceptions` is not defined), local while the link
  reads it and global once it is kept. A weak symbol's `.comment` entry clears only
  its `.comment` flags (a force active one stays). A label at the end of a function, the start of the next, is an
  entry of the next; an entry's file column is past its owner. A program's `.comment` records come with a section of
  type 0xca2a82c2 (`.mwcats`, dtk's `.note.split`). In a program without `-g` an object's empty section has no section
  symbol (a data file an older compiler compiled with `-g` has one kept). An object's locals past its `.symtab`'s
  `sh_info` are not written (dtk writes 0 for its split `.bss` objects). The tables' section symbol comes before the
  generated file's locals, the generated symbols before `_eti_init_info`. A symbol's relocations are walked in offset
  order. A `FORCEACTIVE` name nothing defines is in the map's first lines too. With `-listclosure`, `_eti_init_info`
  is the generated file's object and its labels' sections are not listed.
- From the links of bfbb, bfbb-vibe, Pikmin 2, Mario Party 4 and 5 and The Thousand-Year Door (all equivalent with
  both linkers: 1, 1, 1, 199, 269 and 63 links): the C++ runtime check is AddCppRuntimeRoots' (0x436b90). An older
  runtime's references (in `.ctors`/`.dtors`, not their `$NN` sections) are warned about (message 183) and replaced
  by the generated file's in `.ctors$10`, `.dtors$10` and `.dtors$15`, made too when `__init_cpp_exceptions`,
  `__fini_cpp_exceptions` or `__destroy_global_chain` is not defined (their targets are then undefined); a reference
  missing is an error that ends the link. Its root records (walked by 0x43e320) keep the `.ctors$10` and `.dtors$15`
  references with the exception tables' kept entries, the `.dtors$10` one with `__register_global_object` when that
  is defined, else with `.dtors`' kept entries. The older references are not roots, not even force active by
  `.comment` (dtk's); a local one of an object without `.comment` is dropped from its list, its relocations given to
  its section's symbol, which keeps the section. Exception tables kept without `_eti_init_info` are an error
  (message 184, 0x4452b0); the "missing runtime file" one (125) is not given. In a partial link without `-r1`,
  `_eti_init_info` is not defined (its references are not found). A partial link without `-g` has no DWARF, and
  lists an object's section whose symbols are all dropped (an empty `.text`, a losing weak duplicate's `.rodata`) at
  its offset. A global of a size comes before the labels at its address; a common of no size goes after the one at
  its address (dtk's `__unknown_common_bss_symbol`). A partial link without `-strip_partial` has every object's file
  symbol; a section symbol is left out only for a loaded section the layout does not place.
- Not done: with `-g`, 2.7 keeps a data file's empty `.text` through its compile unit's `.debug` entry (the 3.0
  compiler's objects), where this linker does not. Under parallel links, both 2.6 linkers (the original and the
  rebuilt one) sometimes cannot write the map ("Can't write"); a rerun works, and 2.7 does not do it.

## The 1.3.2 linker, equivalent

`config/LD_1_3_2` builds the GC 1.3.2 linker (2.4.2.81, 2002-05-07; Metroid Prime's, the same executable as
1.3.2r's) from 2.6's units, as `LD_2_7` does (`"units_from": "LD_2_6"`; its resources are
`src/linker/mwldeppc13.rc`). The sources have its behaviour under `#if LINKER == 13`. There is no split and no exact
build: `build/LD_1_3_2/equivalent/mwldeppc.exe` is checked by what it does:

```
python configure.py --version LD_1_3_2
ninja
MWCIncludes=include/libc python tools/linker_equivalence.py build/compilers/GC/1.3.2 build/LD_1_3_2/equivalent/mwldeppc.exe OUT --runtime DIR
```

At first, 2.6's behaviour gave 224 of the 324 scenarios the 1.3.2 linker's; now all but `cpp-exceptions` (which
the original fails) are identical, and the replayed link of Prime's `rs5mp1_p.elf` (R3ME01_00, 1271 objects) is
equivalent. `tools/crossmatch.py` (against LD_2_6) placed 1330 of 1.3.2's functions; the changed ones are mostly in
ELF_gen.c, ELF_PPC_EABI.c, ELF_linker.c, DWARF_line.c, ld-eabi-ppc.c and DumpELF.c. What 1.3.2 does differently:

- The banner's build date and version (`BuildDate.c`, the resources); `.comment`'s header says version 2.4.0
  (its byte 0xe is 0, 2.6's 7).
- `-dump`'s procedure records have no entry and offset columns.
- With full paths, an object's name is the file's name, then its path past the drive, at most 255 characters
  (1.3.2 builds the path in a Pascal string still holding the name, 0x43a690).
- The procedure records (`.mwcats`) of every object are read, its sections kept or not (2.6: the kept sections of
  forced objects), so a dropped object's `.mwcats.text` has a section symbol too. Their relocations' offsets in a
  partial link do not skip the dropped records.
- The DWARF sections are not in the link map. There is no piece-by-piece stripping of the 3.0 compilers' DWARF
  (their `.dwarf.*` and `.line.*` symbols; 2.6's `newDwarf` objects): their sections are kept by chunk, as older
  objects' are, and every relocation of a DWARF section is applied, its pieces and targets kept or not.
- A small data relocation to a linker generated symbol is checked (message 50), as in 2.7.

## Next

- 2.6's shared units: 75 did not link and 21 differed at first. Of them, AssertionFailure.c, string.c, buffer_io.c,
  ansi_files.c, file_io.c, file_io.win32.c, fcntl.win32.c and ParserHelpers.c are `equivalent`, and Help.c, StaticParserGlue.c,
  Targets.c, ParserFace.c, TargetOptimizer-ppc-eabi.c, misc_io.c, unistd.win32.c, CLErrors.c, wcstoul.c, arith.c,
  time.win32.c, signal.c, abort_exit.c, float.c, string_io.c, char_io.c, libimp-eabi-ppc-mw.c, CLStaticPlugins.c,
  libimp-eabi-ppc.c, CLStaticMain.c, arrcondes.cpp, ErrMgr.c, Utils.c, Arguments.c, runinit.c, maincrt.c, BuildDate.c,
  WarningHelpers.c, ToolHelpers.c, CLSegs.c, extras.c, ThreadLocalData.c, Option.c, Generic.c,
  CLLoadAndCache.c, Files.c, ToolHelpers-cc.c, Parameter.c, MacSpecs.c and CLToolExec.c `matching`, startup.win32.c `equivalent`, and the linker's own ld-eabi-ppc-mw.c in cc-eabi-ppc-mw.c's place (above). The linker
  plugin's glue, `src/linker/ld-eabi-ppc.c`, is `equivalent` (above). The equivalent linker is now built entirely
  from source: its units, resources, imports and COMMON block (below), nothing from `build/LD_2_6/obj`.
  [mweppc](https://github.com/doldecomp/mweppc)
  has the 2.x compilers' driver, MSL and runtime sources, matching: the first reference for any shared unit. `python tools/probe_units.py LD_2_6` lists the rest. The driver's sources match the 1.3 compiler (`config/GC_1_3`, compare mode) only in part, and
  the 2.x driver is the 2.7 compiler's; matching them serves the compiler's 1.3.2 and 2.7 builds too.
- The linker's own code (0x42f600 to 0x46b356) is all in units now. Its `equivalent` units could come closer to
  the original's bytes (`tools/funcdiff.py`), or be cleaned up (the disassembler's parts each have their own helpers
  for the same operands).
- 2.0 and 2.5 as configurations of their own, from 2.6's (`tools/remap.py`'s method).
- 1.0's shared units: 45 do not link, 15 differ. A first test for the 15: the linker inherits 1.0's
  `"flags": {"auto,deferred": "on"}` (the 1.0 compiler does not auto-inline); probe them with `config/sources.json`'s
  own flags, as 1.1 builds them.
