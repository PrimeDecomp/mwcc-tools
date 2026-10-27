# mwldeppc.exe (GC 2.7 toolset) survey

First pass at reverse engineering the linker, before any code is written. Nothing here is decompiled yet; it comes
from the PE headers, the string tables, and running the real linker.

## The binary

| | |
| --- | --- |
| file | `build/compilers/GC/2.7/mwldeppc.exe` (748032 bytes), PE32 x86, ImageBase `0x400000`, entry `0x1000` |
| banner | `Metrowerks Linker for Embedded PowerPC`, **Version 3.0.4**, runtime built Aug 13 2004 10:40:59 |
| sections | `.text` 0x4a000 (532925 bytes), `.exc`, `.rdata`, `.data`, `.rsrc`, `.CRT`, `.idata`, `.bss` (146769), `.reloc` |
| imports | KERNEL32 (65), ADVAPI32 (registry), USER32, VERSION, `LMGR326B.dll` (3 ordinals: 189, 190, 191, the license manager) |
| runtime | the MW Win32 C runtime and MSL (`MsDos.c`, `Files.c`, MSL `printf`/`time`, `std::exception`), so the linker is itself CodeWarrior-built C++ |

The 2.7 toolset's other binaries sit beside it: `mwcceppc.exe`, `mwasmeppc.exe`, `lmgr326b.dll`.

## Structure, from the source file names in the strings

The executable is the CodeWarrior command-line driver ("CmdLine") with the linker plugin compiled in. The plugin
callbacks are the same `UCB*` / `CWDROPIN` interface the IDE uses (`CLDropinCallbacks_V10.cpp`).

| layer | files |
| --- | --- |
| driver and options | `Arguments.c`, `Option.c`, `Parameter.c`, `Targets.c`, `CLIO.c` (message styles), `CLToolExec.c`, `CLProj.c`, `CLFiles.c`, `CLFileOps.c`, `CLAccessPaths.c`, `CLSegs.c`, `CLOverlays.c`, `CLDependencies.c`, `CLBrowser.c`, `CLLicenses.c`, `CLPlugins.c`, `CLPluginRequests.cpp`, `CLWriteObjectFile.c`, `CLCompilerLinkerDropin_V10.cpp`, `CLDropinCallbacks_V10.cpp` |
| host OS | `MsDos.c`, `Files.c`, `MacSpecs.c`, `MacFileTypes.c`, `Resources.c` (Mac resource-fork emulation for the `STR#` strings), `StringUtils.c` |
| linker proper | `LinkerFacePPC_EABI.c` (plugin entry, pref panels), `ELF_linker.c` (symbol resolution, dead-stripping, map file), `ELF_gen.c` (layout and ELF writing), `ELF_PPC_EABI.c` (EABI sections, SDA, linker-generated symbols), `ELF_Endian.c`, `LinkerCmdFile.c` (LCF parser), `LinkSym.c`, `LK_symbol.c` |
| debug info | `DWARF_line.c`, `EPPC_DWARF2link.c`, `EPPC_DWARF2util.c` (merging `.debug_*` from the inputs) |
| disassembler | `DumpELF.c`, `DumpELFCodePPC.c`, `DumpDWARF.c`, `PrepDumpELF.c` (`-dis`) |
| runtime hooks | `__init_cpp_exceptions.cpp` (the linker embeds knowledge of the MSL objects it needs) |

The compiler repo's `src/backend/BE_elf.c`, `ELF_Endian.c` and `DWARF.c` write the same ELF/DWARF 1 structures, and are the
reference for reading object files. `ELF_Endian.c` exists under the same name in both programs.

## Command line

The full `-help` output is in [mwldeppc-help.txt](mwldeppc-help.txt). Option groups:

* **General** (shared driver): `-help`, `-version`, `-timing`, `-progress`, `-v`, `-search`, `-lines`, `-wraplines`,
  `-maxerrors`, `-maxwarnings`, `-msgstyle {mpw,std,gcc,IDE,parseable}`, `-[no]stderr`, `-nostdlib`/`-defaults`, `-l`/`-L`/`-lr`,
  `-dis`, `-nofail`, `-o`, `@response` files.
* **Linker**: `-m|-main`, `-nomain|-noentry`, `-map [file]`, `-mapunused`, `-listclosure`, `-listdwarf`, `-[no]srec`
  (`-sreceol`, `-sreclength`, `-sortsrec`), `-lessram|-moreram`, `-linkmode`, `-lcf <file>`, `-codeaddr`, `-dataaddr`,
  `-sdataaddr`, `-sdata2addr`, `-stackaddr`, `-heapaddr`, `-romaddr`, `-rambuffer`, `-application`, `-library`,
  `-partial|-r`, `-opt_partial|-r1`, `-resolved_partial|-r2`, `-strip_partial`, `-sym`/`-g` (DWARF 1 or 2), `-fp`, `-proc`,
  `-sdata`, `-sdata2`, `-heapsize`, `-stacksize`, `-model`, `-abi`, `-tune_relocations`, `-[no]char`.
* **Environment**: `MWLibraries`, `MWLibraryFiles`, `MWEABIPPCLibraries`, `MWEABIPPCLibraryFiles`, `MWLinkOptions`,
  `MWLinkEABIPPCOptions`, `LM_LICENSE_FILE`, `TMPDIR`, `COMSPEC`, `MW_CYGDRIVE_PREFIX`, `MW_CYGWIN_ROOT`.
* Default entry symbol `__start`, default output `a.out`, default map `<outfile>.MAP`.

## Linker command file (LCF)

Keywords found in `LinkerCmdFile.c`'s tables: `MEMORY { name : origin = , length = }`, `SECTIONS { ... }`, `FORCEACTIVE`,
`FORCEFILES`, `EXCLUDEFILES`, `INCLUDEDWARF`, `ENTRY`, `SHORTEN_NAMES_FOR_TOR_101`; section attributes `ZTEXT ZCODE MIXED
CONST`, `INTERNAL_LOAD`, `EXCLUDE_FILE`, `SHORT`, `PROVIDE`, `CONSTRUCTORS`, `GROUP`, `ALIGN`, `ROMADDR`, `SIZEOF`,
`SIZEOF_HEADERS`; ELF-quirk switches `CONFIG_SYS_V_ABI`, `CONFIG_SUSE_LINUX`, `CONFIG_YELLOW_DOG_LINUX`,
`CATS_BSS_MOD`, `CATS_HEADER_MOD`, `DWARF_DIAGNOSTICS`, `DATA_TYPE_CONVERTS`, `DYNAMIC_LINKER`, `EXTERNAL_SYMBOL`,
`INTERNAL_SYMBOL`, `COMMON_ORDER`, `DYNSYM_ORDER`. Memory-map output names `addr_mode` `external|internal`.

## Linker-generated symbols and sections

From `ELF_PPC_EABI.c` and `LinkSym.c`: `_SDA_BASE_`, `_SDA2_BASE_`, `_ABS_SDA_BASE_`, `_ABS_SDA2_BASE_`,
`_stack_addr`, `_stack_end`, `_heap_addr`, `_heap_end`, `_nbfunctions`, `_f_mwerks_fixup`/`_e_mwerks_fixup`,
`_rom_copy_info`, `_bss_init_info`, `_eti_init_info` (exception table index), the `_pic_*` variants, `_ctors`,
`_dtors`, `_DYNAMIC`, `_GLOBAL_OFFSET_TABLE_`, `.startof.<sec>`, `.phdr_N`, `.sec_N`. The C++ runtime glue:
the linker requires `__init_cpp_exceptions`, `__fini_cpp_exceptions` (and the references to them),
`__destroy_global_chain`, `__register_global_object`, and complains "runtime sources `global_destructor_chain.c` and
`__init_cpp_exceptions.cpp` both need to be updated to latest version" when they are missing or old.
Section names in the order the string table lists them (a hint at the ELF writer's order, not verified against output): `.compress.init .init .compress.text .text .compress.fini .fini .rodata .ctors
.dtors .data .sdata2 .sbss2 .PPC.EMB.sdata0 .PPC.EMB.sbss0 .sbss .sdata`, then DWARF, `.symtab`, `.strtab`, `.shstrtab`.
Dead-stripping (`-opt_partial`, `-strip_partial`, `FORCEACTIVE`) and the `>>> EXCLUDED / UNREFERENCED DUPLICATE` map lines
are in `ELF_linker.c`.

## Map file format

Observed with a small program: [example-a.map](example-a.map). It starts `Link map of <entry>`, then one block per
output section (`<name> section layout`, columns `Starting address / Size / Virtual address / File offset`, then one row
per symbol with an alignment column and the owning object), then `Memory map:`, `Linker generated symbols:` and, with
`-mapunused`, `UNUSED` rows. The format strings are at file offsets 0x95a1c to 0x95b98 and 0x990e4 to 0x99190.

## The oracle and the replica

(The replica, the Rust crate in `mwld/`, and the scripts that only compared it with the real linker (`lcf_oracle.py`,
`rel_oracle.py`, `partial_addend_check.py`, `walk_fuzz.py`) are not part of this branch; the findings below came from them.)

The real linker runs on this machine, so it is the test oracle. `mwld/` is a Rust crate (`cargo build --release`) that replicates a link, with or without a linker
command file (`src/link.rs` loads and strips, `layout.rs` places, `writer.rs` writes the ELF, `map.rs` the map), and `tools/linker_oracle.py LINKER_DIR OUT --runtime DIR [--mwld EXE]` links eleven small programs with both and
compares the ELF files (`tools/elfdiff.py`, an independent Python reader) and the link maps. All eleven are byte-identical, both ELF and map: dead
code, two units, `-sdata 0` and `-sdata 64` data, per-symbol alignment, floats and switch tables, pointer data, C++
with exceptions and RTTI, an archive made by the real linker's `-library`, an object compiled with `-pragma "cats off"`,
and an object from the 1.3.2 compiler (`.comment` version 10). Every program is compiled by a Metrowerks compiler
and linked with the same two MSL runtime objects (Echoes') as `__init_cpp_exceptions` and `global_destructor_chain`. 

What the real linker does, as found by those links:

* **Dead stripping is by symbol.** An input section is cut into one atom per sized symbol; atoms nothing refers to are
  dropped and the rest are packed, each at its own alignment. That alignment is in the object's `.comment` section
  (a 44-byte header, then 8 bytes per symbol: alignment, two flag bytes, padding), not in the ELF. A section with a
  symbol whose second flag byte is `0x10`, or with a symbol named `...data.0` (the compiler's mark of a section start),
  is kept whole. `.ctors` and `.dtors` atoms
  of an object with anything else live are roots (an object nothing needs is dropped whole, `.ctors$10` hooks
  included), as are a symbol's own atom when the compiler flagged it `#pragma force_active` (second `.comment` flag
  byte `0x08`), the entry and the runtime's `__init_cpp_exceptions_reference` and `__fini_cpp_exceptions_reference`
  (not `__destroy_global_chain_reference`: a link with only `global_destructor_chain.o` drops it); a weak
  definition another object overrides loses its atom. An `extabindex` entry lives when the function it describes does
  and keeps its `extab`.
* **Layout.** `.text` starts at 0x10000; the output sections go in the order `.compress.init .init .compress.text
  .text .compress.fini .fini .rodata .ctors .dtors extab extabindex .BINARY .data .got .sdata .sbss .sdata2 .sbss2
  .PPC.EMB.sdata0 .PPC.EMB.sbss0 .bss`, each at its alignment (the largest of its inputs' `sh_addralign`, and its size
  is rounded up to 4), an empty one at the running address. An input section starts at its own `sh_addralign` (the map
  shows a `*fill*` row). The file offsets are the running position; a `PROGBITS` section after a `NOBITS` one does not
  keep the address-to-offset difference.
* **Linker-made pieces.** `.ctors$00` (`_ctors`) and `.ctors$99` (`_ctors$99`, 4 zero bytes), likewise `.dtors`, exist
  only when the section has live input other than the runtime's `$10` and `$15` hooks, or when an exception table is
  live (`extabindex` kept; with none the sections are just the hooks). The suffixed input sections go between them in
  name order, an unsuffixed `.ctors` after the suffixed. A live `extabindex` ends with 32 bytes, `_eti_init_info`; with
  no exception table live but code that refers to `_eti_init_info` (the runtime's `__init_cpp_exceptions`) there is an
  empty `extabindex` section and `_eti_init_info` has size 0 and value 0. A live `extab` needs
  `__init_cpp_exceptions` (the real linker fails without it). `.mwcats.text` exists only when a kept object has one.
  The 32 bytes of `_eti_init_info` are: the table's start and end, the first function's address and the covered code's length,
  then 16 zero bytes. A global without a size (the `_savegpr_N` entry points inside `__save_gpr`) is kept in the symbol
  table and the map only if code refers to it or it is the entry. `_SDA_BASE_` and `_SDA2_BASE_` are the start of `.sdata` and `.sdata2` plus 0x8000;
  `_stack_addr` is 0x3dfff0 and the heap is below the stack, both fixed; `_nbfunctions` counts the kept functions.
* **The ELF.** One `PT_LOAD` per non-empty section, `e_flags` 0x80000000, entry at the symbol. The symbol table is, for
  each object that has a kept section: its `FILE` symbol, the section symbols of what was kept (the `extabindex` one
  has value 0), the kept locals; then the four section symbols of the linker's own pieces, all the globals in object
  order, and `_ctors`, `_ctors$99`, `_dtors`, `_dtors$99`, `_eti_init_info`. `st_other` is cleared. The relocations of
  the objects that have a `.mwcats.text` or a `.note.split` (a section the dtk splitter writes) are kept, sorted by
  address, with the address and symbol index of the output (`.rela.text`, `.relaextabindex`, `.rela.data`...);
  `.mwcats.text` itself is rebuilt for the kept functions. The rule is the section: an object compiled with `-pragma
  "cats off"` (as Echoes' runtime is) has neither and none of its relocations are kept, though the compiler and linker are the same.
  Sections: the loadable ones, `.rela*`, `.mwcats.text`, `.symtab`, `.shstrtab`, `.strtab` in the file in the order
  `.shstrtab`, `.strtab`, `.symtab`, relocations, `.mwcats.text`, with the section header table at the next multiple
  of 16.
* **The map.** The format of `mwld/src/map.rs`; the quirks are that a section's size is the input
  section's size before stripping, that a `*fill*` row shows the address after the fill, that the first `extabindex`
  listing is empty and the second has only symbol rows with the file offset of the end of the section, and that the
  `_f_X_rom` address is aligned for the input sections there were even if all were stripped.

Real objects. `tools/linker_oracle.py` only builds small programs; the Echoes objects (1062 of them, the MSL, math and
engine code the game was split into) were also linked: from a random function, the objects its undefined symbols need
(the two runtime objects and a stub for `__register_fragment` and `__unregister_fragment` added, `-nostdlib`), by both
linkers. Of 360 random links that the real linker accepts, all are identical, ELF and map (the earlier 16 differences
were three rules found since: an `R_PPC_EMB_SDA21` relocation may point 2 bytes into its instruction; active data
with sized symbols is dropped like any other, while a section with no sized symbol, or a `.ctors`/`.dtors` hook, is kept
whole; and the map's file offset of a symbol without size inside an atom is the atom's end, as the real linker prints
it). Entry names
longer than 63 characters are an error in the real linker. The generating code is in the linker: `FUN_00436b90` in the Ghidra program `mwld-gc-2-7` (the
PPC EABI symbol and hook generator; the strings `_ctors`, `_dtors`, `_eti_init_info` and the three `*_reference` hooks
are used only there). It decides which of the linker's own `.ctors$10`, `.dtors$10` and `.dtors$15` hook pieces to
add when the runtime's functions are not defined by an object; the replica does not port it and so does not make
those pieces (a link with a constructor but no `__init_cpp_exceptions.o` is not reproduced).

What it does not know (the replica stops, or is untested): `-opt_partial` and the other partial
links, `-sym` and DWARF merging, `-romaddr`, overlays, `-listclosure`, `-g` (accepted and ignored), multiply-defined and other error
messages (it prints only the first undefined symbol), and `MWLibraries`. A library search is done for `-Ldir` / `-lname` as the real linker does it (checked against it): `-L` takes its directory attached to it (`-L dir` is a usage error: `Argument(s) expected`); `-lname` is looked for only in the `-L` directories, in the order given and not in the current directory, as `libname.dylib`, `libname.a` and then `name`; failing that the error is `No library file found matching 'libname{.dylib|.a}' or 'name'`. The library is an input where the `-l` stood. Objects with no
`.comment` section, such as `mwasmeppc.exe`'s, are refused: one linked differently (the real linker kept less of the
runtime, and ordered the symbols by another rule) and its alignments and flags are unknown. The tests use the 2.7
linker, the 2.7 and 1.3.2 compilers, and the Echoes runtime objects, so that is the scope of "byte-identical".

## Linker command files and the whole game

`-lcf FILE` (`mwld/src/lcf.rs`, `layout.rs`) supports `MEMORY { name : origin = N, length = N }`, one `GROUP` of
output sections with empty bodies (`.text ALIGN(0x20):{}`), symbol assignments after it (`+ - * & | ~ << >>`,
`SIZEOF(.section)`, other symbols), and `FORCEACTIVE { names }` (the names contain `<>,`, so the block is cut out
before tokenizing). `-fp`, `-nodefaults`, `-sdata`, `-sdata2`, `-g`, `-mapunused` and `@rspfile` are accepted.
`tools/lcf_oracle.py` runs one link with both linkers and compares. Metroid Prime 2's whole DOL (970 objects,
`-fp hardware -nodefaults -lcf build/G2ME01/ldscript.lcf`, 6.6 MB) is byte-identical, ELF and map; the replica takes 0.4 s.
(The repository's own `main.elf` is from older objects and differs, so the real linker has to be run again.) What it needed:

* **Layout.** The output sections are in the order of the command file, starting at the `origin`; each starts at
  max(its `ALIGN`, its inputs' alignment); an empty one does not advance the address. `_f_X_rom` is
  align(`_f_X`, max(widest input alignment, the file's `ALIGN`)). With a command file the symbol list (map, symbol
  table) follows only the file's sections; assigned symbols that are new come first in "Linker generated symbols", and
  those the linker has (`_stack_addr`, `_stack_end`) take the new value in place. An empty `.bss`, `.stack` and `.rodata` are
  left out of the memory map (an empty `.rodata` was seen with the cpp-exceptions objects and the Echoes-style file); an empty `.bss` (and `.stack`) has a `_rom` symbol, a non-empty `.bss` does not.
  Ghidra: `FUN_00443c30` creates these symbols for every placed section (kind 1 records, `_f_X` and `_e_X` from the
  section record's address fields, tied to the output file's symbol table); the third, `_f_X_rom`, is made only
  when option `DAT_004d5d4f` is set and the section's ROM address (`+0x4e`) differs from its run address (`+0x46`), so
  the rule above for `.bss` is that comparison and not a name test. `FUN_00443800` marks each section's first
  `_f_X` definition (`+0xce`) before that. (Both are in `FUN_00443f20`, the output symbol-table builder, behind the
  `DAT_004d5d2c` and `DAT_004d5d38` options.)
* **Common symbols.** Each object's commons make a `.bss` piece of their own, owned by the object, placed after
  every object's ordinary `.bss` in object order; their offsets follow the symbol order, each at its alignment (the
  symbol's value), with no padding. The piece's section symbol comes after the object's locals, and an object that is
  only commons has no `FILE` symbol.
* **`_rom_copy_info` and `_bss_init_info`** are generated at the end of `.init` ("Linker Generated Symbol File") when
  code refers to them: 12-byte entries (ROM address, address, size) for each section that has contents (`.init` too),
  then 12 zero bytes; 8-byte entries (address, size) for each non-empty bss section, then 8 zero bytes. That piece
  has a section symbol before `.ctors$00`'s.
* **`ABS` symbols.** Undefined symbols the linker defines (`_SDA_BASE_`, `_stack_addr`, those the file assigns) are
  global `NOTYPE`/`SHN_ABS` symbols after the object globals, in the order of the "Linker generated symbols" list,
  plus the symbols an emitted assignment uses (`_db_stack_addr` for `__ArenaLo`), before `_rom_copy_info`.
* **Weak duplicates.** A definition another object overrides is dropped even when it is flagged force-active (the
  duplicate inline functions of a catch-all object stay out of the output and out of the map's symbol rows, though
  the piece's size in the map still counts them).
  Ghidra (`FUN_00438650`, the symbol loop of the object loader): a global that finds an earlier definition of its
  name (`FUN_00435180`) is not entered; a record (type, bind, object index; 0xc bytes) is appended to the
  earlier symbol's list of duplicates and the loop continues *before* the force-active registration
  (`FUN_004429e0`), which is why a flagged duplicate is not a root. The record is made while the objects load, so it
  does not depend on the link mode: a plain `-r` link drops the same atoms (the ScriptGui `.preplf` check: the weak
  `__dt__` of an `rstl::vector` and `reserve` removed, 0xf8 bytes closed up, the map still at the input size).
  `FUN_00435180` decides a name that meets an earlier one: it looks the name up (`FUN_0045c2a0`) and, by the kind of
  both and by whether either object was built weak or the project is a plain or a partial link (`DAT_004d527a`,
  `DAT_004d5279`), calls `FUN_00434fa0(winner, loser, ...)`; for two weak definitions the earlier wins (tested with
  an inline function in two objects in both link orders: the first object's copy stays), and the other `ReportError(0x3f)`
  case is a common symbol meeting a larger one. `FUN_0045ab20` makes the 0xc-byte duplicate record (kind 8).
  Symbol flag bits seen there: byte `+0xc` low nibble is the kind (5 and 6 are the `.data`/`.sdata`-style classes
  chosen from the section name's suffix), `& 8` force-active, `& 0x10` the loaded/active mark.
* **Names.** A symbol may be defined local in an object and listed there as undefined global; the reference binds to the
  local. An unsized symbol inside an atom that has a sized symbol gets the atom's end as its file offset, and its
  row goes after the sized symbol's, even when it is at the atom's start.

## Partial links and the REL modules

The game's REL modules are linked twice (`build.ninja`): `.preplf` is `-sdata 0 -sdata2 0 -r` on the module's objects, and
`.plf` is `-sdata 0 -sdata2 0 -r1 -lcf <module>/ldscript.lcf -m _prolog -strip_partial` on the objects plus
`REL_Setup.o` (and usually `global_destructor_chain.o`), in the order of `build.ninja`. `tools/rel_oracle.py` runs
all of them against the real linker (`--step plf|preplf`). **All 86 `.plf` links and all 84 `.preplf` links are
byte-identical in ELF and map.**

* **`-r` / `-r1`** make an ET_REL file: addresses are 0, every relocation is kept (symbol indices remapped), a
  rebuilt `.comment` is written, and nothing is stripped (unreferenced globals and locals stay). Only `-r1` generates
  the `.ctors`/`.dtors` markers and `_eti_init_info`; plain `-r` ignores `-lcf` and `-strip_partial` is rejected
  without `-r1`. `-strip_partial` applies the normal reachability strip (roots: the entry and `FORCEACTIVE`, locals
  too) with undefined-symbol errors turned off.
* **LCF section bodies.** `.data : { *(.data) *(extabindex) *(extab) }` merges those input sections into the output
  section. A command file without `MEMORY` means origin 0. In a partial link an output the file does not place is
  listed after the file's sections (empty) when an object has input for it, and live atoms of it are dropped here (the
  real linker reroutes them to another output with a warning, which is not replicated).
* **Generated markers.** When live code refers to `_ctors` or `_dtors`, `.ctors$00/$99` and `.dtors$00/$99` pieces are
  generated (a `.dtors$10` piece, `__destroy_global_chain_reference`, gets a relocation to `__destroy_global_chain`,
  which is listed as undefined when nothing defines it). They have section symbols after the locals and global symbols
  at the end of the symtab; `.comment` header byte 15 is 3 and their entries have alignment 4.
* **Symbol table.** An undefined symbol is listed where the first object that discovers it (see below) uses it.
* **`>>> SYMBOL NOT FOUND` order (map header) and the undefined-symbol order.** One walk, which decides both:
  1. A forced name no *global* definition has comes first of all (a forced *local*, such as a `__sinit_X_cpp`, is
     a root and still listed).
  2. Roots in order: the entry, then the `FORCEACTIVE` names. From each root the walk goes depth first through
     the atoms it refers to; the relocations of an atom are followed in order of their offset (not the order of the
     object's relocation table), and an undefined name is listed when its relocation is reached.
  3. Then the leftover live atoms start walks: code atoms in reverse index order, then the other atoms (data) in index
     order, then the `.ctors`/`.dtors` atoms. An atom that code or data names through a *local* symbol is not a leftover
     root (it is reached through its user), whereas one named through a global symbol is.
* **Plain `-r` walk.** Plain `-r` has no entry and no strip, so every atom stays except a weak definition another
  object overrides (the atom is dropped, the later ones close up, yet the map still shows each piece at its input
  section's size). Only the order of the walk is left, and it is not the one above:
  1. The roots are the force-active atoms (`.comment` flag 0x08), objects in descending index, within one object the
     lowest symbol index first. (Ghidra: `FUN_00438650` loads an object and `FUN_004429e0` prepends each force-active
     symbol to the list `DAT_004c0bd8`, so the list is last-in first-out; `FUN_0043e110` splices the command file's
     `FORCEACTIVE` names and then that list into the roots; `FUN_0043e320` drives the walk.)
  2. A walk (`FUN_0043c490`) is depth first and enters a relocation target at once, whatever object or section it is
     in, relocations in offset order. The first frame of a section whose own relocations are done goes on through the
     rest of that section's atoms in *address* order, taking the unseen ones; later frames of that section do not.
     In the walks of rule 3 a frame rest-walks only when it is the walk's own first atom, when its atom is the first
     one of its section, or when no enclosing frame is in its section (`synthvoice.o`, reached by a call from
     `synthmacros.o`, is walked in address order from `mcmdEndOfMacro`'s callee): `musyx/runtime/hardware.o` (`snd_handle_irq` reaches `hwSetTimeOffset`, which must not
     rest-walk `.text`) needs the first condition, fuzz seed 59 (a callee that is the section's first function) the second.
  3. After the roots (`FUN_0043dc40`) come the `.ctors`, `.sinit` and `.dtors` sections over the objects, then
     (`FUN_0043de50`) the first atom of every section, object by object in command-line order, sections ascending;
     the loop repeats until nothing is left.
  This was found with tiny objects against the real linker and `tools/walk_fuzz.py` (the NOT FOUND list of the
  real and replica linkers agreed on 300 of 300 random sets), then confirmed by the sweep (86 of 86 `.preplf` identical).
* Weak against strong, checked against the real linker (scenarios `weak-strong`, `weak-only`, `partial-weak` of
  `tools/linker_oracle.py`): a strong definition beats a weak one whichever object comes first, among weak ones the first
  wins, and the loser's atom is dropped (zeroed on writing when it was part of a whole-section atom); the replica's
  output and map are identical in all of them.
* The walker in Ghidra (`FUN_0043c490(file, symbol, ...)`, read in full): it marks the symbol (`+0xd |= 0x80`), loops
  over the relocations of the symbol's atom in section order and, for each target symbol not yet marked, resolves it
  (`FUN_0045c7a0` name, `FUN_0045c430`/`FUN_0045adb0` definition lookup), prints `>>> SYMBOL NOT FOUND` when nothing
  defines it (counting it in `DAT_004d575c`), and otherwise calls itself on the definition (`FUN_00471230` records the
  reference first). After its relocations an atom whose flag bit 3 is clear, and whose bit 1 is set or `DAT_004d5d3a`
  is zero, sets bit 3, marks its symbol and walks every later symbol of its section (those after its own index in
  the section's address-ordered symbol array, if unmarked and defined), and then walks `local_18`, the result of
  `FUN_0043c440`, which is only computed for a section without relocations. So the "rest of the section" is per
  atom and starts after the atom, not at the section's start; replacing the replica's rule (the first frame of a
  section to finish, with the exceptions above) by this literal reading broke the `.preplf` sweep (0 of 86) and the
  fuzzer, because bit 1 and `DAT_004d5d3a` (and which atoms get bit 1 through `FUN_0043c440`) are not modelled.
  Two more reads: the rest-walk stops at the end of the atom's *chunk* (`local_34`, an index into the section's list of
  0x14-byte chunk records located by `FUN_0045dae0`, which compares the atom's address with each chunk's end), not at
  the end of the section, and `DAT_004d5d3a` is set in `InitLinkOptionsFromPrefs` (`FUN_004216b0`) and read in the walker,
  `FUN_0043de50`, `FUN_0043e320` and the driver, so it is a link option; `FUN_0043c440` returns the atom's own symbol when
  the symbol's kind nibble is 5 (a local), else the chunk's type-3 entry. The replica treats a section as one chunk.
  The structure behind the chunk (`FUN_0045dae0`, `FUN_0045d7e0`, `FUN_0045d950`; `FUN_0045d6e0` is the binary search
  over it): a section record keeps at `+0x14` a table with the symbol count (`[0]`), the symbols sorted by address
  (`[1]`), 0x14-byte chunk records (`[2]`: `+8` index of the chunk's first symbol, `+0xc` an offset added to that
  symbol's address) and their count (`[3]`); with one chunk the whole section is it, otherwise the chunk is the last
  record whose start is not after the atom (an atom exactly at a chunk start is searched for among that chunk's symbols).
  What makes a section several chunks was not found; `synthvoice.o`'s `.text` carries no compiler mark or flag that
  differs between functions, so it is not visible from the object.
  The fitted rule stays; decoding those two flags is the way to a faithful walk and would settle `seq.o` +
  `synthvoice.o`.
* Weak against strong in Ghidra (`FUN_00435180` resolves a name against the symbol table, `FUN_00434fa0(winner, loser,
  ...)` merges two records). The records hold a binding nibble at `+0xc >> 4` that `FUN_0045d670` folds into three
  classes (0 none/local, 1 weak-like: 1, 7, 9; 2 strong: 2, 3, 4, 8, 10) and a kind in the low three bits that
  `FUN_0045d6a0` folds into 0..4 (0 undefined). `FUN_00435180` is called in a reference mode (`param_3 == 1`) and a
  definition mode (`2`); in definition mode the decision depends on the class of the record already in the table,
  on whether either object's `.comment` flags (`+0x12`, masked with 1, or 3 when `DAT_004d527a`) mark it as compiler
  output of the project's own kind, on the options `DAT_004d5279` and `DAT_004d527f`, and on the symbol's section index
  `+0xe` being `-0xe` (0xfff2, a common symbol). `FUN_00434fa0` lets the winner take the loser's definition
  only when the winner is still undefined (`FUN_0045d6a0 == 0`): it copies the address, and sets the binding to the
  winner's class times 16 plus the loser's kind. A definition-after-definition of two strong symbols reaches
  `FUN_00431cd0` (the duplicate-symbol error). The tail of `FUN_00435180` (read in full) ends with the existing record as the canonical one in
  every remaining case (`FUN_00434fa0(existing, new)`), so a definition arriving later only fills a record that is
  still undefined; the orderings that matter for Echoes (strong first or last, weak first or second,
  two weak) are covered by the oracle scenarios above and the 86 links.
* Map sizes in Ghidra: `FUN_004421c0` is the whole link (load `FUN_00438650`, the walk `FUN_0043e320`, then layout
  `FUN_0044a700`, which skips the program-header block when `gPartialLink` is set and whose piece sizes are read back
  by the map). `FUN_00470880` places one output section: it takes the file position (`FUN_00443320`), aligns the section to the
  largest alignment of its pieces (`+0x9a`), pads with `FUN_004435c0`, and records the size as the file-position
  delta (`+0xda`), so a size in the map is bytes actually emitted plus padding. The end of the link (`FUN_00434490`) closes two buffered *binary* output
  streams with `FUN_0044f580` (flag `DAT_004d5d47`, handle `DAT_004d5748`; its buffer writers `FUN_0044f340` and
  `FUN_0044f3d0` are called from the ELF section writers `FUN_00447890`, `FUN_00448350`) and `FUN_0044f800`
  (`DAT_004d5d46`, `DAT_004b711c`, same shape), so they are the ELF/DOL outputs, not the map. The 0x44fc00..0x451500
  range (245 reads of the printf-like hook `DAT_004b7350`) is the verbose ELF dump (`FUN_0044fdc0`, "entry spndx value
  size bind type"). The map text is written elsewhere, through a hook the layout calls via `+0x5e` of the section
  record (`FUN_00470880`: `hook(section, 1)` after placing a section); where that hook is set was not found.
  `FUN_004431d0` only flushes the
  buffered output. Also ruled out as the map writer: `FUN_00446670`/`FUN_004468b0` (ETI size, "Failure in
  CalculateSizeETI"), `FUN_00445670` (per-section size pass), `FUN_00447c50` (file-position check of the ELF output) and
  `FUN_0044c200` (the `.rela` emitter, switch on output kind 0..3), and `FUN_00440220`, `FUN_00440590`, `FUN_004409c0`
  (each loops over the input files' DWARF sections, by section type `)`, `0`, `,`, and one of them tests for
  `.dwarf_loc`; these are the DWARF relocation/range passes behind the unsupported `-sym` option, not the map), and
  **The map text is found** (a `find-strings` Ghidra script, `substring`, lists a string and its referencing
  functions): `FUN_00464890` prints `Link map of %s`, `FUN_00464660` the `%s section layout` header with the column
  rules, `FUN_004646a0` a section row (`%08x %06x %08x %08x %2i %s \t%s %s`: offset, size, address, file offset,
  alignment, name, owner) and `FUN_004646d0` the same without the owner; the memory map and the generated symbols are
  printed inline by the layout function `FUN_0044a700`, after `FUN_0044a500`-style size checks (errors 0x2a overlap,
  0x78, 0x85/0x86 ROM overlap): `Memory map:` with `Starting Size File` rows `%15s  %08x %08x %08x` (name, address,
  size, file offset), or, when `DAT_004d5d4f` (the ROM/RAM option, `-romaddr`) is set, the five-column form
  `Starting Size File ROM RAM Buffer` (`%15s  %08x %08x %08x %08x %08x`), a row for each section record with a nonzero
  flag word (`+0x82`), and, when `DAT_004d527e` is set, only when the section has an output index (`+0xa2`, assigned
  in `FUN_00443f20` to the sections that have symbols or are forced: the `.sdata`/`.sbss`/`.sdata2` records when small
  data is on); `DAT_004d5d2f` 1 or 2 adds `%15s           %08x %08x` rows (line-number and similar debug tables);
  then, unless `DAT_004be280`, `Linker generated symbols:` with `%25s %08x` for each symbol of the generated lists
  (`DAT_004d56a8` chain, then the table starting at `_stack_addr`, 0x1e bytes each). Experiment: referencing
  `_f_rodata`/`SIZEOF(.rodata)`/`_f_stack` from the command file does not make an empty `.rodata`/`.stack` appear in
  the memory map, so what marks `+0xa2` for `.init`, `.sdata2` and `.sbss2` (listed when empty) is not the references.
  String search also shows the option parser's messages (all in the 0x48aXXX..0x48bXXX range, handler table at
  `0x48baa8`): `Unknown option '%s'`, `Option is obsolete`, `Option ignored`, `Option overrides the effect of '%s'`,
  `No library file found matching 'lib%s{%s}' or '%s'` (so `-l` searches `libX.a`/`libX.so`-style names along the `-L`
  paths, which the replica does not do: `-l` is "unsupported"), and `Redudant use of '%c%s%s'; linker always searches
  library paths`; the section symbols `_f_X`, `_f_X_rom`, `_e_X` have no format string (they are assembled by
  concatenation in the symbol generator, `FUN_00436b90` also holds `_rom_copy_info`, `_pic_rom_copy_info` and
  `_f_mwerks_fixup`).
  `FUN_00443f20` (the output symbol-table builder: marks each live symbol of every input file in three passes, numbers
  the output sections' symbols in section order (`+0xa2`), fills the partial-link `.symtab` pieces and calls
  `FUN_00443800`/`FUN_00443c30` for the optional extras; this is where the symbol numbering the replica reproduces comes
  from), and `FUN_00453540` (sets up the disassembler/dump engine, "Task should be DISASSEMBLE_DISK"), `FUN_00451e50`
  ("Exception Table Name / Saved Registers / Large Frame" dump) and `FUN_004519a0` (byte-swaps an exception table
  while reading it), and `FUN_00434490` (the end of the link: closes the ELF and DOL streams, frees every input file's
  symbol and section records, then `FUN_00464970`/`FUN_00464930`, the log/output teardown; nothing writes map text
  there either), and, inside the 0x451500..0x45c000 range, `FUN_00455250` (ELF `DT_*` tag names), `FUN_00457f00` (a
  floating-point formatter) and `FUN_00459430` (the C++ name demangler, `operator ...` names): the whole range is the disassembler and exception-table dumper, no map text either; the map text writer is described above (found later with a string search). The observed
  rule (a dropped weak atom's piece keeps its input size in the map) is verified by `partial-weak` and the weak sets of
  the `.preplf` sweep.
* A partial link keeps each relocation's addend as it is, also against a section symbol whose section lost atoms
  (an overridden weak definition ahead of the target): `tools/partial_addend_check.py` links two hand-made objects (the
  compilers never emit section symbols) and the real linker leaves the addend at the input offset, so the output
  points at the wrong atom. (A first version of the replica "fixed" it and differed from the real linker.)
* The map omits "Linker generated symbols" in a partial link.

`tools/rel_gap_scan.py` checks the objects of every Echoes module for the sections behind the gaps below: no module has
an object with `.mwcats.text`, `extab`, `extabindex`, `.debug` or `.line` (only 16 modules have `.dtors$10`, which is
handled), so those gaps cannot occur in Echoes' build; the 29 objects with `.mwcats.text` (compiled with the same
compiler) are in the DOL, which is identical. A small C++ program compiled with `-O4,p` does get one, and the real
`-r` link then writes `.mwcats.text` and `.rela.mwcats.text` (one entry per surviving function, its address in the
data and a relocation against the function's symbol), which the replica carries too (a section symbol per object,
`.rela.mwcats.text`; the `.comment` header records `-fp`).

Known gaps (none occur in the 86 `.plf` links): the lcf that merges `extab` and `extabindex` into `.data` (`*(extab) *(extabindex) *(.data)`) links with the
same ELF contents as the real linker (the `_eti_init_info` piece follows the `extabindex` inputs wherever they are,
section symbols of `extabindex` inputs have value 0, the segment's permissions are the OR of all the section's inputs' flags (see the corruption-proto note below), but
the map's `_f_stack_rom` differs there (real `0x80003700`, replica `0x80003600`; merging only `extab` or only `extabindex` is identical, as is the unmerged file; `0x3700` is what `0x3570 + 0x9c`, the two merged sections' sizes, would round to, but nothing else supports that reading), some `.dtors$15` cases of objects with
exception tables differ, and the undefined-symbol order of a plain `-r` link still differs
for some big objects (`musyx/runtime/seq.o` + `synthvoice.o`: the real linker reaches `voiceKill` only from
`synthKillVoicesByMacroReferences`, after that function's own relocations, although the address-order walk of
`.text` is past it earlier; deferring same-section targets of a rest-walking frame was tried and made
`synthmacros.o` + `synthvoice.o` differ again). 

## Metroid Prime (GM8E01_00, the 2.7 build)

The `main.elf` link of 826 objects is identical to the real linker's, ELF and map (with `-mapunused`). What it needed
beyond the Echoes build:

* **Runtimes without the `$10`/`$15` suffixes.** Prime's `__init_cpp_exceptions.o` and `global_destructor_chain.o` keep
  their hooks in plain `.ctors` and `.dtors`. The linker then drops those input pieces (the strip roots still mark the
  functions they name) and makes `.ctors$10` (`__init_cpp_exceptions_reference`), `.dtors$10`
  (`__destroy_global_chain_reference`) and `.dtors$15` (`__fini_cpp_exceptions_reference`) itself: 4 bytes each, holding
  the function's address, no relocation. All its pieces have section symbols.
* **A name that FORCEACTIVE lists counts as referenced**, so an entry such as `_savegpr_17` inside `__save_gpr` is a
  map row and an output symbol even when no object calls it.
* **Several `.mwcats.text` sections per object** (one per code section): the entries of all of them are carried, a
  section symbol is made for each that has a live entry, and an entry for code another object's weak definition
  replaced is dropped.
* **Overridden weak data in a live atom** (a vtable another object defines too, inside a whole-section `.data`): the
  bytes stay but are zero, and its relocations are not emitted.
* **`-mapunused`** adds a row `UNUSED <size> ........ ........    <name> <object>` for each sized symbol of a dead atom
  in a live object's section, in the object's own order, and for each global without size inside live code that
  nothing refers to (the `_savefpr_N` entries).

* **Whole-section atoms in the strip walk** (found with NESemuP's `.plf`, the one module whose map differed). A section
  the compiler marked with a `...data.0` label is one atom. In the strip walk (`-strip_partial`) a reference to
  something inside it by its own name (`write_pointer`) does not make the walk follow the section's relocations
  there and then; a reference through the `...data.0` mark does. The relocations are followed later, when the mark is
  reached or at the leftovers. In plain `-r` every walk enters at once. With it NESemuP's `.plf` and `.preplf` and all
  172 Echoes links are identical.

## Next

1. `-opt_partial`.
2. Decide between a matching decompilation (this repo's style, with `src/linker/`) and keeping the replica; the
   string table gives the file names and function boundaries for the former.

## The roots of the C++ runtime (`FUN_00436b90`, read through the `dtors` string search)

A string search for `dtors` leads to `FUN_00436b90`, which makes the linker's own references before the walk (for each output
section kind, in a `switch`). What the decompilation (read up to the `__fini_cpp_exceptions_reference` case; the tool cut the
rest) shows:

* For `__init_cpp_exceptions` it looks up `__init_cpp_exceptions_reference`; the section that holds that symbol is compared with
  the first 10 bytes of the name at 0x497e58 (a `.ctors` family name), and when it is not one, the defining *object* must be
  `__init_cpp_exceptions.o` or `global_destructor_chain.o`, or the linker reports error 0xb7. The same is done for
  `__destroy_global_chain` / `__destroy_global_chain_reference` against `.dtors` (7 bytes), and for `__fini_cpp_exceptions` /
  `__fini_cpp_exceptions_reference`.
* When such a pair is missing, a count is raised and the linker generates the `.ctors`/`.dtors` output itself (sizes `+4`
  and `+0xc`, symbols `_ctors`, `_dtors`, `_dtors$99`); `__register_global_object` found in the program gets a root of its own
  (a list node of type 2, the others type 3).
* The whole function is now read (`inspect-functions --max-code-chars=120000`; the earlier cut was the script's own 16000
  character default). Its stages: (1) the roots are made only when the link is not a plain `-r` (`gPartialLink == 0`) or is
  `-opt_partial`; (2) the three pairs are checked against the section names `.ctors$10` (`__init_cpp_exceptions_reference`),
  `.dtors$10` (`__destroy_global_chain_reference`) and `.dtors$15` (`__fini_cpp_exceptions_reference`), the same table the
  replica has in `link.rs`; a reference in its own section becomes a root (list node type 3, or type 2 with
  `__register_global_object`), one anywhere else raises the count that makes the linker generate the pair; (3) the generated
  `_ctors`, `_ctors$99`, `_dtors`, `_dtors$99` symbols and the `__init_cpp_exceptions_reference` family are made with
  `FUN_004365b0` / `FUN_004363d0` (symbol type 0x10, the `$99` ones with alignment 4); `_eti_init_info`
  (`_pic_eti_init_info` with the PIC ABI), `_GLOBAL_OFFSET_TABLE_` and `_DYNAMIC` are made in the same pass.
* So the `.dtors$15` cases that differ are not decided by the walk but by the section the `_reference` symbol is defined in; the
  replica's table agrees with the original, and what is still unchecked is a program that defines these symbols outside the
  runtime objects. This is read from the code, not yet reproduced against the real linker.

### Defining the runtime's hooks outside the runtime (checked against the real linker)

Single-object links with `-nostdlib` of an object that defines a hook (and its `_reference` symbol in a section of that name):

| defined | real linker |
|---|---|
| `__fini_cpp_exceptions` (with or without the reference, any section) | error `runtime sources 'global_destructor_chain.c' and '__init_cpp_exceptions.cpp' both need to be updated to latest version.` |
| `__fini_cpp_exceptions_reference` alone | links |
| `__destroy_global_chain` + reference, any section | links |
| `__init_cpp_exceptions` + reference in `.ctors$10`, `.dtors$10` or an ordinary section | links |
| `__init_cpp_exceptions` + reference in a section named exactly `.ctors` | the same error |

The replica reproduces the first row (a `__fini_cpp_exceptions` defined outside `__init_cpp_exceptions.o`); the `.ctors` row and
why the original's section test (`FUN_00436b90`, which should accept the matching section) rejects these is not understood, and
real programs do not define these symbols.

## What marks an output section for the memory map (`+0xa2`, found with a new `find-field-access` script)

The memory-map row condition is `+0x82 != 0 and (DAT_004d527e == 0 or +0xa2 != 0)`. The writers of the `+0xa2` flag of a section
record, and what they mean:

* `FUN_0046d330` (called once per input section of every object, the last loop of `FUN_00436b90`) classifies the input section
  into a kind (`.text`, `.rodata`, `.data`, `.sdata`, `.init`, ... through the switch on `param_3[2]`) and marks the output
  section record the kind belongs to. So an output section is "used" as soon as *any object has an input section of its kind*,
  live or dead, empty or not. This is why an empty `.init`, `.sdata2` or `.sbss2` row shows in the memory map: an object
  brought in one (an empty section the compiler leaves), and the replica's rule is the same (an output section with input
  is listed; an empty `.bss`, `.stack` and `.rodata` with a command file are the exceptions found against the real linker).
* `FUN_0044a700` (the layout) marks records 0x10, 0x11, 0x12 (the ELF symbol table, string table and section-name table;
  `FUN_00469cf0` makes them) unconditionally, 0x13 when `_rom` symbols are on (`DAT_004d5d4f`), and, when `DAT_004d5284` is set
  (the small data options in `FUN_00471320`), the sections named `.sdata`, `.sbss` and `.sdata2` (also making the
  `_SDA_BASE_` and `_SDA2_BASE_` records); `.sbss2` and `.init` get no such mark, only the one of the classifier.
* `FUN_004689c0`, `FUN_0044c200`, `FUN_0044ca90`, `FUN_0045ba30` and `FUN_0046a9f0` also set it (the first for ten section
  kinds at once, presumably when the default sections are created); not read.

## `-listclosure` (format found with the real linker; not implemented)

The option leaves the ELF as it is and puts the tree of what the dead-strip walk reaches right after the `Link map of` line,
one line per symbol the first time it is reached: depth spaces plus one, `depth]`, name, `(type,bind)` (`func`, `object`,
`notype`, ...; `local`, `global`, `weak`), `found in` and the object label (a generated symbol says `Linker Generated Symbol
File`). The depth-1 roots come in this order: the entry point, `__fini_cpp_exceptions_reference`,
`__destroy_global_chain_reference`, `__init_cpp_exceptions_reference` (each with what it reaches), then the local symbols the
constructor lists name (`@6`, `@12`), then the generated `_ctors`, `_ctors$99`, `_dtors`, `_dtors$99`. A weak duplicate of a
reached symbol prints `>>> UNREFERENCED DUPLICATE name` and `>>> (type,bind) found in object` instead. A first attempt that
printed a line whenever the replica's strip walk enters a symbol printed nothing for a normal link, because that walk is only
run for partial links and its roots (entry, forced names, leftovers) are not this list; the option is rejected until a walk
with the original's root order is written and checked on the whole of Echoes.

### `_f_stack_rom` in the doubly merged lcf, measured (superseded by the last section: the file position explains it)

With `*(extab) *(extabindex)` both in `.data`, `_f_stack` is `0x80003570` and the ROM address of the empty `.stack` was measured for
several `.stack ALIGN(n)` values (the replica gives `align(0x3570, n)`):

| n | real | replica |
|---|---|---|
| 0x4, 0x10 | 0x3570 | 0x3570 |
| 0x20 | 0x3580 | 0x3580 |
| 0x40 | 0x35c0 | 0x3580 |
| 0x80 | 0x3600 | 0x3580 |
| 0x100 | 0x3700 | 0x3600 |
| 0x200 | 0x3800 | 0x3600 |
| 0x1000 | 0x5000 | 0x4000 |

So the real value is the replica's plus `n` once `n >= 0x40` and plus nothing below. It is not `align(cursor, n)` for any one cursor
(`n = 0x80` and `n = 0x100` would need cursors on the two sides of `0x3600`), so the rule is something else, maybe an
alignment gap counted on both the run and the ROM side. Nothing in the other ELF or map output differs (the ELF is identical).
The Echoes and Prime lcf files keep the two sections apart and are not affected. Not resolved.

### Where the ROM address (`+0x4e`) and the extab pairing live (read, not resolved)

* `+0x4e` of a section record (the ROM address `_f_X_rom` uses) is written in `FUN_0046fa60` (the first call of the layout,
  which applies the command file): a section the file gives no ROM address gets the memory region's (`DAT_004d5684[6]`); the
  same function installs, per section kind (the `switch` on `+0xd7` minus 2), the layout callback at `+0x5e` and the
  `(+0x18..+0x24)` address-range record the callback uses; the other writers of `+0x4e` are `FUN_0046d330`, `FUN_004683b0` and
  `FUN_0045ff90`.
* Kind `0xd` (the exception table pair) is special: when `DAT_004d5d2e` is set it has its own callback (`FUN_0044ca90`, an
  extab/extabindex pairing routine that walks both tables' symbols and relocations), its own address-range record
  (`DAT_004d52fc..`) and a partner record at `+0xae` that gets the same range. That is the code a merged
  `*(extab) *(extabindex)` takes, and the likely place of the `_f_stack_rom` difference above; it is a large routine and not
  worked through. The Echoes and Prime command files do not merge them.

### Not the chunk builder (ruled out while looking for what makes a section several chunks)

`FUN_0045d7e0`, `FUN_0045d950` and `FUN_0045dae0` only read the chunk table (they pick the chunk of an atom from the table at
section record `+0x14`: `[3]` chunks, one chunk means the whole section). `FUN_0046d330`, which has a `push 0x14`, is not
the builder either: its tail (read in full) puts the input section into the output section record's list (`+4` of the
record at `+0x42`, a list of 12-byte nodes kept in order of the input section's rank, or, when `+0x21` is set, by name with
`FUN_00402d30`) and handles the `!`, `"`, `#`, `)`, `*`, `+`...`2` kinds (the special ELF and `.PPC.EMB` sections). Other
allocators of 0x14-byte records to look at: `FUN_0043afa0`, `FUN_0046c1f0`, `FUN_0045f190`, `FUN_00468690`,
`FUN_004496b0`, `FUN_00462810`. Not read.

Also ruled out as the chunk builder: `FUN_0046c1f0` (a list of (object, name, value) triples, one 0x14-byte node each),
`FUN_00468690` (creates the dynamic-linking ELF sections `.interp`, `.dynstr`, `.dynamic`, ... with kind numbers like 0x14: the
`push 0x14` is a kind) and `FUN_0045f190` (a name parser). The `GetSymbolAddressRecord` family (`FUN_0045a8c0`, `FUN_0045aa50`,
`FUN_0045cc20`, `FUN_0045cd20`) and `FUN_0043afa0`, `FUN_004496b0`, `FUN_00462810` are what is left to read.

Read in `FUN_004496b0` (the pass over an output section's input list that rewrites exception-table style records; it walks a
section's chunk table the same way the walker does): a 0x14-byte chunk record has a *kind* in bit 4 of its byte at `+5`. With
the bit set the chunk holds symbols (`+8` is the index of its first symbol; it ends where the next chunk's first symbol starts,
or at the symbol count `[0]`); with the bit clear it is a *gap* of `+0xc` bytes that no symbol covers. So the several chunks of
a section are runs of symbols separated by gaps, and the chunk of an atom is found by address among the symbol runs
(`FUN_0045d7e0`). What creates the gaps (and so makes `synthvoice.o`'s `.text` several chunks) is still not found: it is built
before the layout, probably when an object is loaded (`FUN_00467a70`, called with the section record when `+0xc` of the
record is nonzero, and `FUN_0043afa0` are the next to read).

`FUN_00467a70` is not the builder either: it walks the chunk tables of a section and of its relocation section together
(`piVar13` follows the `+0xc` link from a section record to its `.rela` record; the relocation table is chunked in step, chunk
`i` of one matching chunk `i` of the other, with 12-byte entries `{offset, symbol << 8 | type, addend}`) and rewrites the
words at relocations of type 1 or 0x18 with a resolved address (`FUN_004652c0`), warning `0x3c` for any other type on a symbol
that is marked used. So a section's chunks and its relocations' chunks are made together, when the object is loaded;
`FUN_0043afa0` (the loader of an input file, with many internal-error line numbers) is the one left to read for how.

`FUN_0043afa0` is the chunk builder (called with the object, a section type, and the section record; it returns at once for
section types 0, 1, 0x10 to 0x17 and 0x1c, and allocates the 0x10-byte or, for type `*`, 0x14-byte chunk header at `+0x14`).
What it does, from the decompilation:

* The symbols of the input section are sorted by address (a bubble sort that also swaps symbols at the same address by kind:
  a sized symbol goes before an empty one, kind 5 before 6, section symbols (kind 3) stay first of their address).
* One chunk is made for each *section symbol* (kind 3) of the sorted list, not one per gap: the chunk starts at that
  symbol's index and ends at the next section symbol (`+8` is the first index, `+0xc` the count or the byte length).
  So a section that is several chunks is an input section several objects' pieces were concatenated into (a `.mwcats.text`
  or an archive member merged by the compiler), and the gaps are the bytes between the end of one piece's last symbol and the
  next section symbol.
* The symbol records of the same section in each linked `.rela` are looked up by address and numbered in step (the loop over
  `+0xc` of the record), and the `%`/`&` types (0x25, 0x26) get the "padded" bit (`+5` bit 1) when a symbol does not start where
  the previous one ended.
* It reports internal error `0x33` for every state it does not expect (two sized symbols at one address, a section
  symbol with a size, a missing definition).

This explains the chunks but not the walk order of `seq.o` + `synthvoice.o`: the walk follows references between atoms,
and the chunk table is built after the atoms exist. That gap in the replica stays open.

`FUN_0044ca90(record, _, section, kind)` is not what decides `_f_stack_rom`: it reports internal error `0x33` unless the link is
partial (`gPartialLink`) and `DAT_004d5d2e` is set, so it only runs for `-r` links. It walks the input files' relocation
sections (`.rela*`) of the section and, for each relocation whose symbol is a defined, used extab/extabindex symbol, finds the
matching entry in the relocation table by address (`FUN_0044ca10`) and copies `{offset, symbol, addend}` triples across to the
partner section (`+0xae`) in step. That is the partial-link extab/extabindex pairing the replica already reproduces byte for byte
on the 86 Echoes `.plf` files; the full-link `_f_stack_rom` difference (real = replica + n for `.stack ALIGN(n)`, n >= 0x40, with
extab and extabindex both merged) has to come from the layout callbacks at `+0x5e` set by `FUN_0046fa60`, not from here.

`FUN_0046fa60` (the section setup, read in full this time) only chooses, per section kind (`+0xd7` minus 2), the layout callback
at `+0x5e` and four default words at `+0x18..+0x24` (the callbacks are `LAB_0044bd20` for most kinds, `LAB_0044bb20` for kinds
6, 8, 10 and 12, `LAB_0044c980` for 0xd when `DAT_004d5d2e` is set, `LAB_0044db90`, `LAB_0044d390`, `LAB_0044da50`, `FUN_0044d220`,
`LAB_0044de60`, `LAB_0044e280` for the others). It computes no address. The `_f_<section>_rom` names are not whole strings in
the binary (a search for `_rom` finds only `_rom_copy_info` and `_pic_rom_copy_info` in `FUN_00436b90`, and two table headings
in `FUN_00450930`), so they are assembled from a prefix and a suffix; the place that computes `_f_stack_rom` is the printer
`FUN_0044a700`, not these callbacks. The next read for the `ALIGN(n)` difference is that printer's handling of kind 0xd.

### The kind 0xd callback (read from the disassembly; Ghidra has no function there)

The `_f_stack_rom` search ended in `LAB_0044c980`, the layout callback of kind 0xd when `DAT_004d5d2e` is set (it is not a Ghidra
function, so `inspect-functions` refuses it; `disasm-slice` is a script that prints already-disassembled instructions from an
address). Its four phases are `0`: `FUN_004471e0(section)` then `FUN_004465e0(section, 1, 0, 0)`; `1`: internal error; `2` and `3`:
the generic `FUN_00447c50`, the chunk reader `FUN_004496b0` and `FUN_00447d60`. The generic callback `LAB_0044bd20` uses
`FUN_00445ce0` and `FUN_00446b50` instead, so the extab/extabindex pair is sized by its own routine:

`FUN_004471e0` walks each input file's extab section chunk by chunk and, for every symbol that is defined and used (`+0xd & 0x90`),
finds its relocations in the paired section and sizes the *exception table record* it points at by the record's own header:
kind 1 is `4 * [+0xc] + 0x14 + 4 * [+0x10]` bytes, kind 2 is `2 * byte[+1] + 8`, kind 3 is `4 * word[+2] + 0xc`; each is rounded up to
4 bytes, and for a record whose relocation symbol is itself defined and used the size also goes through `FUN_004435c0` and
`FUN_00443560(4)`, which add to the running total of a section's generated bytes. The section's size (`+0x72`) is the sum of those record
sizes, not the sum of the input sections' sizes. This is a per-record layout of the exception tables (so a record shared between
functions is counted by use), and it is what the replica does not model for the doubly merged file: the replica sums input section
sizes. It does not affect the Echoes and Prime links (neither merges them, so the pair takes the separate path, and the 86 `.plf`
and the DOL match byte for byte). `FUN_004465e0` copies `+0x72` to `+0x6e` and, unless the section is empty, calls `FUN_004435c0`
with the section's alignment; it is the common tail of the sizing.

Correction to the paragraph above: the record-sizing in `FUN_004471e0` is a *hypothesis* for the `_f_stack_rom` difference, not
a finding. The measurements (real = `align(0x3570, n) + n` for `.stack ALIGN(n)` with n >= 0x40, no difference below) are an
extra `n`, not a changed section size: `0x3570 + 0x9c` rounds to `0x3700` only for n = 0x100, and for n = 0x40 it would give
`0x3640`, not the measured `0x3600`. So the cause is not shown to be the record sizes, and the replica is left as it is.

Helpers read (all small): `FUN_00443320` is the current position of the linker's output cursor (`DAT_004d58f4`); `FUN_004435c0(n)`
adds `n` to it; `FUN_00443560(n)` aligns it up to a multiple of `n`, adds the padding, and returns the padding (0 when `n` is 0).
So the `FUN_004471e0` calls with `4` pad the generated-data stream and are not ROM addresses: they do not explain the extra `n` in
`_f_stack_rom`. `FUN_0045a8c0(symbol, kind)` makes a 0x14-byte *address record* of the given kind for a symbol (kind 1 only, else
internal error `0x33`), chained from `+8` of the symbol, and also creates the records of kinds `'\n'` and `'\v'` (the
`GetSymbolAddressRecord` family is these records: one per symbol and kind, found by `FUN_0043f260(symbol, kind)`).

`FUN_0045ff90` is the parser of one lcf section header (`.name ADDR : [AT(addr)] { ... }`): it reads `ALIGN(n)`,
`REGISTER(n)`, `INTERNAL_LOAD`, a `(...)` expression stored as a kind-1/2/3 *address expression* in the section record
(`+0x2c..`), and creates the run-address and ROM-address records itself (`FUN_00467f00(0, 0, -1, 1 or 2, kind)` into `+0x4a` and, when
`DAT_004d5638 == DAT_004d5778`, `+0x4e`). `FUN_004683b0` does the same for the sections the linker makes without a header (it copies
the defaults from the first section record `DAT_004d5684`: `+8`, `+0x10`, `+0x18`, the ROM words only when `DAT_004d5d4f` is set).
So the ROM address of `.stack` is an address *expression* evaluated later, and the extra `n` has to come from how that
expression is resolved against a section whose size and alignment the pair callback (`LAB_0044c980`) changed; neither parser
evaluates it. The evaluator (`FUN_00467f00`'s records, resolved through the `GetSymbolAddressRecord` family) is what is left.

The record family is now read, and none of it evaluates anything: `FUN_00467f00(a, b, c, list, kind)` allocates a 0x1e-byte
address-expression node and appends it to one of the three lists on the first section record (`+0xc`, `+0x14`, `+0x1c` of
`DAT_004d5684`: run, load and ROM); `FUN_0045a8c0` and `FUN_0045aa50` create the symbol's address record of kind 1 and 2 (`FUN_0045aa50`
also gives it a 0xc-byte body); `FUN_0045cc20` and `FUN_0045cd20` copy a five-word record into the symbol's record of the given kind,
creating it when missing. So the values are computed elsewhere, by whatever walks those three lists: the layout passes in
`FUN_0044a700` (callbacks with phases 0..3 at `+0x5e`, then the loop that stores `+0x66`/`+0x86` and calls `FUN_0044a660`) and
`FUN_0044b390`, which runs between them and is the next function to read for the `_f_stack_rom` rule.

`FUN_0044b390` (runs between the layout phases 1 and 3) stores the values of the linker-generated symbols into their records: for the
table at `PTR_s__stack_addr_0049cda0` (`_stack_addr`, `_stack_end`, ... 0x1e bytes each) and for the list at `DAT_004d56a8`, it asks
`GetLinkerGeneratedSymbolValue(id)` (`FUN_00464b30`) and writes the result into the symbol's address record (`+0xc`) and into the
symbol itself (`+4`); then, for the SDA/PIC/PID ABI (`gAbiIsSdaPicPid`), it fills the relocation tables of the ABI's
`_SDA_BASE_`/`_SDA2_BASE_` pairs from the sections' run and load addresses (the `0xc2`, `0xc6`, `0xca` address records of each used
section; `FUN_00432830` byte-swaps the word). `GetLinkerGeneratedSymbolValue` evaluates by id: -7/-8 `_SDA_BASE_`/`_SDA2_BASE_` from the
symbol's own value, -9/-10 as -7/-8, -6 `DAT_004d5620` (error 99 when -1), -5 `DAT_004d560c`, -4 `_heap_addr` (or `_stack_addr`'s record
plus `DAT_004d5768`), -3 `DAT_004d56ac`, -2 `_stack_addr` minus the stack size (`DAT_004d57b0`), -1 `DAT_004d5758`; a symbol an lcf assigned
(its value found through `FUN_00463d10`, the lcf assignment lookup) wins over all of these. So an lcf-written `_f_stack_rom` is read
back through this path, and the ROM address of a section with no assignment is the one its run address gives; the extra `n` is not
produced here either. The rule stays open; what the replica measures for it is in the table above.

### The generic size pass and `CalculateSizeETI` (`FUN_00445ce0`, `FUN_004468b0`)

`FUN_00445ce0(section)` (phase 0 of the generic callback) is the size of a section made of input sections. For each member of the
section's list: a member of type 6 goes to `FUN_00445450`; a member **named `extabindex`** goes to `FUN_004468b0` and is skipped
by the rest; otherwise, for type 1 members, each input's section chunks are placed by alignment (`FUN_0045c470` gives a symbol's
alignment; the running size `+0x72` is rounded up to it, either alone or, when `DAT_004d5275` is set, together with the load
base `+0x62`) and the gaps between symbols that are not in a chunk run are added (`+= symbol address - previous`). A `__tv_`
symbol adds 8 to `DAT_004d5688` (a count of table-of-vtable words). The size ends rounded up to 4.

`FUN_004468b0` is the linker's own `CalculateSizeETI` (its two error messages are "Failure in CalculateSizeETI:", printed with the
expected and found numbers before internal error `0x33`). It starts from the section's size so far (`+0x72`), rounded up to 4, and walks
the list `DAT_004d56dc` of per-object exception table index groups keeping only the entries whose target symbol is defined and used
(`0x90`), adding `align + size` of each target; for a full link or `-opt_partial` it then adds the size of the generated
`_eti_init_info` record (`(groups + 1) * 0x10` bytes, checked against `+8` of that record) and ends by *adding that whole total to
`+0x72`* (`+0x72 += total`, where the total already began from the old `+0x72`). That is, the extabindex part is sized from the
live exception table entries, not from the sum of the input sections' sizes, and starts from the size so far; whether the old size
is counted twice (as the code reads) or the field is reset first by a caller is not settled. The replica sizes `extabindex` as a
separate output section, which is the case Echoes and Prime take; the merged `*(extab) *(extabindex)` into `.data` goes through this
routine in the original, and its different size is the likely source of the `_f_stack_rom` difference (the extra `n` fits an
extra rounding step inside it, which this reading does not show). Not implemented.

Follow-up on the paragraph above: `FUN_004468b0` has one caller, `FUN_00445ce0`, for any body entry of the section's list *named*
`extabindex` (so it runs for `*(extabindex)` wherever it is written, merged or not; the Echoes and Prime command files give the
exception table index its own empty-bodied section, which skips the list altogether). If it is the first entry of a section the
old size is 0 and the double count is harmless; after `*(extab)` it would count the earlier size twice, which the measured `_f_stack`
(`0x80003570`, equal to the replica) contradicts, so `+0x72` must be rebuilt afterwards from the address ranges. The tail of
`FUN_00445ce0` after the chunk loop only adds each chunk's size (`+0xc` of the chunk record) when the chunk is flagged, and
`FUN_00446b50` (phase 1 of the generic callback) is the check that an output section and its partner (`+0xaa`) lie within 0xffff
bytes of each other (warning `0x87`, then internal error 0xd, when the order is wrong). The double count stays unsettled.

### Where the run, load and ROM addresses are fixed (`FUN_00470880`, `FUN_004703e0`)

`FUN_00470880(section)` runs in layout phase 1 (before the callback) and sets the section's three addresses. With `p` the *output
cursor* (`FUN_00443320`, the global `DAT_004d58f4`, the position in the output file) and `R` the section's memory region record
(`FUN_00470200(section, kind)`: kind 0 run, 1 load, 2 ROM; each has a base `+8`, a size `+0xc` and a cursor `+0x14` with its saved
copy `+0x18`): the run address `+0x62` is `R0.base + R0.size + R0.cursor`, an lcf expression's value when the header had one; the
load (`+0x66`) and ROM (`+0x6a`) addresses are the same sum over `R1` and `R2` (ROM only when `DAT_004d5638 == DAT_004d5778`);
each is then rounded up to the section's alignment `A` (`+0x9a`: the widest input alignment, at least 4 for a non-empty
data section with `DAT_004d5275`, at least the header's `ALIGN`), the padding is added to the region's cursor, and, when
`DAT_004d5275` is set (the "ROM layout" mode, true for the Echoes and Prime files), also to the output cursor with `FUN_004435c0`.
So `_f_X_rom` is `align(R2 cursor sum, A)`, as the replica has it.

`FUN_004703e0(section)` runs after the callback. When the section's size `+0x72` is 0 it **gives the padding back**: `+0x62` is lowered
by `R0.cursor - R0.saved` and the cursor restored, the output cursor goes back by `p_now - _DAT_004d59ba` (the output cursor
saved at phase 1), and the same for `+0x66` and `+0x6a` with `R1` and `R2` when `DAT_004d5d4f` is set. For a non-empty section it
instead hands the regions to `FUN_004702c0` to advance them by the size.

What this suggests for the merged `extab`/`extabindex` file: the pair callback `FUN_004471e0` calls `FUN_004435c0(size)` and
`FUN_00443560(4)`, which advance the *output cursor* without any matching rewind, so at the next section's `FUN_00470880` the
cursor `p` is not where the sections' sizes would put it. The give-back in `FUN_004703e0` is relative to `_DAT_004d59ba`, saved before
the callback, so the bytes that callback added stay in the output cursor and shift every later `align`. An empty `.stack` rounds its
ROM address from a cursor that has drifted, which would fit an extra `n` that appears only for `ALIGN(n)` large enough to
exceed the drift (n >= 0x40). This fits the measurements but is not proven, and it is not modelled: the replica has no output cursor
apart from the sum of the section sizes.

Checking the cursor-drift reading against the table: a drift `d` added to the cursor before one `align(0x3570 + d, n)` gives 0x3580 for
`n = 0x20` only if `d <= 0x10`, and 0x35c0 for `n = 0x40` only if `d > 0x10`; no single `d` fits both. The measured rows are exactly
`align(0x3570, n) + n` for `n >= 0x40` and `align(0x3570, n)` below, i.e. a second whole `n` that appears only when `n` exceeds
the size of the merged data (0x3570 - 0x3480 is not it either). So the drift reading is **refuted as stated**: whatever adds the extra
`n` is not a fixed byte count in the output cursor. The give-back in `FUN_004703e0` is also not what the numbers show (an empty
section would return to the unaligned `0x3570`, yet both linkers print an aligned `_f_stack_rom` for the plain case), so the empty
`.stack` is not treated as size 0 there. The rule stays unresolved and unmodelled.

### Two strong definitions of one name (checked against the real linker)

`foo` defined as a global function in `a.o` and again in `b.o` (both on the command line) gives the real linker
`multiply-defined: 'foo' in b.o` / `Previously defined in a.o` (the second object named first, then the first), then goes on to its
other errors before `Link failed.`; the replica reports this first error and stops. A weak definition never conflicts, and
commons merge; the error is not yet checked for members pulled from archives (the replica keeps the first definition there, as before).

### `-fp efpu`, `-fp spfp`, `-fp fmadd` (checked against the real linker)

A full link of one object plus the two runtime objects gives the same ELF and the same map with `-fp hardware`, `fmadd`, `efpu`,
`spfp`, `soft` and `none` (the real linker only prints warnings, "Floating point settings of project file ... does not match
project settings", about whichever objects disagree). The replica now accepts the three for full links and ignores them, as it does
`soft`/`none`/`hardware`. A partial link (`-r`) records the setting in one byte of `.comment` (at file offset 337 of a one-object
link): none 0, soft 1, hardware 2, and measured with the real linker, `fmadd` 2 (the same as hardware) and `efpu`/`spfp` 3. The
replica writes those, and its partial links of one object are byte-identical for all six values.

### `-opt_partial`, `-romaddr`, `-sym`, `-listclosure` (probed with the real linker)

`mwldeppc.exe -help` lists `-opt_partial | -r1` as one option, and the real linker's output with `-opt_partial` is byte-identical (ELF
and map) to `-r1`; the replica now takes it as the same flag. Note that `-r1 -m _start` without `-lcf` on a one-function object
makes the real linker add the `.ctors$00`, `_ctors`, `.ctors$99`, `_ctors$99` (and `.dtors`) pieces to the output, which the
replica now does the same for every `-r1` link, with or without `-lcf` (checked byte for byte on ELF and map). `-romaddr ADDR` changes both the ELF and the map
(a ROM image is made; not modelled), `-sym on` leaves the ELF the same but adds a `.line` row (`00000000 000001ec`) to the map for an
object without debug information, and `-listclosure` leaves the ELF and changes the map (see above). These three stay rejected.

### `>>> SYMBOL NOT FOUND` (Ghidra: `FUN_0043e110`, `FUN_0043e1b0`)

`FUN_0043e110` walks the list of FORCEACTIVE names (`DAT_004d5684 + 0x20`) in command-file order; a name with no definition is printed
through `FUN_00464750` with the format `>>> SYMBOL NOT FOUND: %s` and reported with `ReportError(0x74)`, and the link goes on. `FUN_0043e1b0`
does the same for the references of a generated-symbol record. The replica printed these lines only for `-r1 -strip_partial` and
`-opt_partial` full dead-stripping walks; a plain `-r1` link (section walk) now lists the missing forced names first too, which makes the
ELF and map identical to the real linker's for an lcf whose FORCEACTIVE names are absent from the objects.

### What Echoes' build asks of the linker

Every link rule in the Echoes `build.ninja` passes only `-fp hardware -nodefaults -o OUT @OUT.rsp` plus one of `-lcf ldscript.lcf` (the
DOL), `-sdata 0 -sdata2 0 -r` (`.preplf`) or `-sdata 0 -sdata2 0 -r1 -lcf … -m _prolog -strip_partial` (`.plf`). `-romaddr`,
`-listclosure`, overlays and linker-side `-sym` (the `-sym on` in the file is a compiler flag) are never used, so the replica rejecting them
does not affect it as a drop-in replacement; the options' names are not plain strings in the executable (a string search for
`listclos` and `romaddr` finds nothing), which is why they are documented by probing only.

### Overlays (Ghidra string search and probes)

The executable has the message `Overlays not supported on this target` (`0048b984`, referenced from a data table, so it is a per-target
handler rather than a call site), the parser entry points `CWParserAddOverlay1Group` / `CWParserAddOverlay1`, the layout code in
`CLOverlays.c` (`FUN_004259a0`..`FUN_00426090`) and the map heading `Overlay` (`FUN_0043e320`). An lcf with `OVERLAY ov1 : {}` is rejected by
the real parser with `':' expected` and `OVERLAY : { .ovl1 : {} }` with `'=' expected` (a form that takes an assignment first, not found),
so the real syntax was not reached; the message suggests the GC target refuses overlays even when the command file parses, which is why the
replica keeps rejecting them.

### Where `-romaddr` lives in the executable (leads, not read yet)

The option table writes `-` as `~~` and the names are in help strings (`0048d631` "set address for ROM image…", `0048d681` the
`-rambuffer` text), which is why searching for `romaddr` finds nothing. The ROM image shows up in four places: the map's memory-region
table heading `Starting Size File ROM RAM Buffer` (`00498e2c`, used by `FUN_0044a700` at `0044aeb3`, the layout and map printer the replica
already follows); the lcf keyword `ROMADDR` (`0049c234`, `FUN_0045f550`, the lcf section-attribute parser); and three errors: RAM-buffer
overlap with a ROM address (`004ac816`), ROM-image overlap with an executable address (`004ac8bc`) and "specify an uncompressed TEXT
section in ROM with the same name as the section of your entry point" (`004ace54`). Together with the probe (a `-romaddr` link changes both
ELF and map) this says the ROM image is a second set of addresses per section (the `+0x6a` ROM address of `FUN_00470880`) plus extra ELF
program headers and a memory-table in the map; none of that is modelled, and the Echoes DOL's lcf has no `ROMADDR`.

### `-listclosure` output (probed)

With `-listclosure` the map gains, right after `Link map of ENTRY`, one line per atom the dead-stripping walk visits, as
`<indent>N] name (type,bind) found in OBJECT ` where `N` is the walk depth (the line is indented by `N` spaces), `type` is `func`,
`object` or `notype`, `bind` is `global` or `local` and `OBJECT` is the file (`Linker Generated Symbol File` for the linker's own symbols).
For `x.o` (`_start` calls `g`), `y.o` (`g` calls `h`) and `z.o` it printed `1] _start`, ` 2] g`, `  3] h`, then one depth-1 line per
remaining root (the compiler's `@6`/`@5` locals of each object, then `_ctors`, `__init_cpp_exceptions_reference`, `_ctors$99`, `_dtors`,
`__fini_cpp_exceptions_reference`, `_dtors$99`) and the `>>> SYMBOL NOT FOUND` lines stay where they are in a map without the option
(before the closure lines are interleaved by discovery order: here the missing `__init_cpp_exceptions` follows its
`_reference`). The ELF is the same. This is the order of the walk `Link::walk` already follows to place the not-found names, so
the option is a matter of logging that walk (name chosen for an atom with several symbols, the extra roots of a full link) and is not
implemented, because the Echoes build never passes it and the details were checked on one three-object link only.

### `_f_stack_rom` is not specific to the merged lcf (correction, measured with a one-function object)

A 0x18-byte `.text` object whose `_start` returns `_f_stack_rom + _f_stack`, linked with the plain lcf (`.init`, `.text`, `.ctors`, `.dtors`,
`.rodata`, `.data`, `.bss`, `.sdata`, `.sbss`, `.sdata2`, `.sbss2` all empty, `.stack ALIGN(n)` last, `MEMORY` at `0x80003100`) gives, with
no `extab` merging at all, `_f_stack = 0x80003118` for every `n` (the empty `.stack` gets its padding back) and

| n | `_f_stack_rom` | `align(0x3118, n)` |
|---|---|---|
| 0x20 | 0x3120 | 0x3120 |
| 0x40 | 0x3140 | 0x3140 |
| 0x100 | 0x3200 | 0x3200 |
| 0x200 | 0x3400 | 0x3200 |
| 0x400 | 0x3800 | 0x3400 |
| 0x800 | 0x4000 | 0x3800 |
| 0x1000 | 0x5000 | 0x4000 |
| 0x2000, 0x4000, 0x8000 | 0x6000, 0x8000, 0x10000 | 0x4000, 0x4000, 0x8000 |

So above a threshold the real value is `align(cursor, n) + n`; the threshold is `n > 0x100` at cursor `0x3118` here and `n >= 0x40` at
cursor `0x3570` in the Echoes-like table above (`n = 0x100` is `+n` there but not here, the same aligned address `0x3200` does not decide it,
and neither does the gap `align - cursor`). The merged extab pair is therefore not the cause; the rule is in how the ROM cursor of an empty
trailing section is rounded (`FUN_00470880` / `FUN_004703e0`) and depends on something else that differs between the two links (the
amount of ROM laid out before it, perhaps). The replica used to stop with `no output symbol for '_f_stack_rom'` on this link, and for any `_f_X`, `_f_X_rom` or `_e_X` that code refers to: the relocations an object with a `.mwcats.text` keeps in the output were looked up among the output symbols, and the linker-defined ones had no entry. That is fixed (the symbol table now remembers the entry of each linker-defined symbol it writes), and with the fix the replica's `_f_stack_rom` is `align(cursor, n)`: equal to the real one for n up to 0x100 here, and short by `n` from 0x200 on (0x1000 gives 0x4000 against the real 0x5000). Echoes' code does not refer to these symbols, which is why neither shows in its links.

### Re-reading `FUN_00470880` / `FUN_004703e0` for the `_f_stack_rom` threshold (read, not resolved)

* `FUN_00470880` sets `+0x62` (run) from region record 0 (`start + size + cursor`), rounds it to `local_18`, the section alignment (`+0x9a`, at
  least the alignment of the input sections and, for kind 1, 4), and adds the padding to the record's cursor `+0x14`. When `DAT_004d5d4f == 0`
  the load `+0x66` and ROM `+0x6a` addresses are just copies of the run address. When it is set (a ROM layout), `+0x66` and `+0x6a` are
  rounded the same way from region records 1 and 2 (`FUN_00470200(section, 1|2)`); a pending address from the command file (`unaff_EBP`,
  a ROM address the file gave) replaces the record's own start when `DAT_004d5638 == DAT_004d5778`.
* `FUN_004703e0` is the give-back for an empty section (`+0x72 == 0`, not a partial link): it subtracts `cursor - saved` from `+0x62`
  and, when `DAT_004d5d4f` is set, from `+0x66` and `+0x6a` too, and restores each record's cursor. So with the ROM layout the empty `.stack`
  ends at the unrounded cursor on all three addresses, which is why `_f_stack` is `0x3118` for every `n`. How the reported
  `_f_stack_rom` is `align(cursor, n) + n` for large `n` is not in these two functions; the symbol comes from somewhere that rounds a
  further time (a candidate is the code that reads `+0x6a` for the map's `ROM` column and the `_f_X_rom` symbols). The replica's
  rounding of `align(run address, alignment)` agrees below the threshold.

A string search for where the `_f_X`, `_f_X_rom` and `_e_X` names are built (`_rom`, `_f%s`, `%s_rom`, `_e%s`, `_f_%s`, `_e_%s`) finds
only the literal `_e_mwerks_fixup` (`FUN_00436b90`), so the three names are made from pieces without a format string; the code that
chooses the `_rom` value is still to be found (by a cross-reference from the section record's `+0x6a`/`+0x4e` readers).

Reading the rest of `FUN_0044a700` (the whole link's layout-and-output driver): after the callbacks run, it writes each section's ELF
section header with `FUN_0044a660(section + 0x7a, address)` (a 0x28-byte header; the address is `+0x86`, or the load address `+0x66` when
there is a ROM layout (`DAT_004d5d4f`) and the section is a kind-1 section with `+0x7e != 8`), checks section overlaps (`ReportError` `0x2a`, `0x85`, `0x86`,
`0x78` against the stack and heap ranges), prints `Memory map:` (the `Starting Size File ROM RAM Buffer` heading when `DAT_004d5d4f` is set,
the plain `Starting Size File Offset` heading otherwise) and then the `Linker generated symbols:` list from `GetLinkerGeneratedSymbolValue`.
Readers of `+0x6a` are `FUN_00447890` and `FUN_00448350` (the S-record / section contents writer), the map's ROM column in this function,
`FUN_00470240` and the two layout functions above; none computes `_f_X_rom`, whose value therefore comes from the
linker-generated-symbol table (`FUN_0044b390`, `GetLinkerGeneratedSymbolValue`) and not from `+0x6a`.

### Code that refers to the linker's own symbols (probed with one-function objects)

A one-function object that only takes the address of a linker-defined symbol was linked by the real linker and the replica, with and
without an lcf. Findings, all now replicated (ELF and map identical for the no-lcf link):

* A reference to `_rom_copy_info` makes the linker generate only that table (`12 * (sections copied + 1)` bytes, `0x24` for `.init` and
  `.text`); a reference to `_bss_init_info` alone generates only the clear table (8 bytes with no bss section); with both,
  `_bss_init_info` follows at the end of `_rom_copy_info`. The replica used to make both whenever either was named. An `.init` made of only
  these tables is a word-aligned section with the flags of code (`AX`, segment `R X`).
* A reference to `_ctors` (or `_ctors$99`) makes the linker generate the `.ctors$00`/`_ctors`/`.ctors$99`/`_ctors$99` marker pieces even
  when no object has constructors; `_dtors` does the same for `.dtors`. They were ignored before.
* Relocations an object keeps in the output (it has `.mwcats.text`) to any of these symbols, to `_f_X`, `_f_X_rom`, `_e_X` or `_SDA_BASE_`
  point at the symbol's entry in the output symbol table; the replica used to stop with `no output symbol`.

With an lcf whose sections are almost all empty the maps differ in the file offsets of the empty sections (the real linker prints
`.ctors 0x70`, `.dtors`/`.sdata`/`.sbss`/`.sdata2 0x80` and leaves out `.data`, `.sbss2` at `0x6c`); that is not modelled.

### Small-object sweep (`tools/probe_sweep.py`)

`tools/probe_sweep.py [--mwld EXE] LINKER_DIR WORK_DIR` compiles 27 small C and C++ sources (weak and common symbols, small data, strings, -sym, -O4, vtables, templates, asm,
function pointers, statics, C++ constructors with and without exceptions, a throw, bss, a switch, a double, two functions) with the real
compiler and links each as a full link, with `-r` and with `-r1`, comparing exit status, ELF and map. It found two differences, both fixed:

* a plain `-r` map lists the `extabindex` section layout (after `.bss`, with the object's `@N` entry) whenever an exception table index
  stays, which the replica printed only for links that also generate the runtime's tables;
* `-r1` of an object with exception tables and no reference to `_eti_init_info` fails in the real linker with "runtime object
  '_eti_init_info' needs to be referenced when C++ Exceptions are used. Either turn off C++ Exceptions for all project and library files
  or include runtime file '__init_cpp_exceptions.cpp'.", with or without `-strip_partial`; the replica linked it.

### Command-file sweep (`LCF=… tools/probe_sweep.py`)

Linking the sweep's 27 objects with Echoes' `ldscript.lcf` found three more rules (full links):

* `>>> SYMBOL NOT FOUND` lines head the map of a full link too (a `-strip_partial` link, the `.plf`, prints none);
* an empty section's file offset is aligned to the widest alignment among its inputs (and to the command file's `ALIGN`);
* an empty `.data` is not listed in the memory map of a full link with a command file (the `.plf` lists it).

* the map lists `_f_bss_rom` for a `.bss` with no input section of an object, which includes one made only of common symbols;

The `_f_stack_rom` differences found here and the `-r1 -lcf` ELF layout are both explained later in this file (resolved).

`_f_stack_rom` of an empty `.stack ALIGN(n)` after the Echoes layout (sym_cpp.o, real linker; `c` is `_f_stack`; `extra` is real minus `align(c, n)`):

| `c` | extra for n = 0x10..0x40 | n = 0x80 | n >= 0x100 |
|---|---|---|---|
| 3168 | 0 | +0x80 | +n |
| 3188 | 0 | 0 | +n |
| 31c8 | 0 | 0 | +n |
| 3208 (`.sbss ALIGN(0x80)`) | 0 | 0 | 0 |
| 3308 (`.sbss ALIGN(0x100)`) | 0 | 0 | 0 |
| 3348 | 0 | +0x80 | +n |

The extra depends on the layout history and not on `(c, n)` alone (`c = 3308` and `c = 3348` have the same aligned base and
differ), so it is not a pad rule, a parity rule or `align(c + k, n)` for any `k` (each was tried against these rows).

### File offsets of empty sections in a partial link (`-r1 -lcf`)

A full link places each empty section at `align(end of the last section with file bytes, A)` and does not chain: with
`.sdata2 ALIGN(0x80)` the next empty `.sbss2` goes back to the lower offset. A partial link does chain: each empty section moves
the file position to its own aligned offset, so `.sbss2` takes `.sdata2`'s, and the empty `.stack ALIGN(0x100)` leaves the
position at the next 0x100 boundary, where `.shstrtab` then starts (the real ELF is larger for that reason). The replica now
keeps that position (`Layout::end`); every `-r1 -lcf` link of the probe sweep matches in ELF and map.

#### `_f_stack_rom` is a function of `_f_stack` alone (measured, sym_cpp.o, Echoes' lcf with `.data`, `.sdata`, `.sbss` alignments varied)

Every layout with the same `_f_stack` (`c`) gave the same `_f_stack_rom`, whatever `.data`, `.sdata`, `.sbss`, `.sdata2` or `.sbss2`
alignments led to it (so those only matter through `c`). For `.stack ALIGN(0x100)` the value steps at these `c`: 0x3300 up to
`c = 0x3288`; 0x3400 for 0x32a8..0x3308; 0x3500 for 0x3328..0x3408; 0x3600 for 0x3428..0x3508 (the replica gives `align(c, n)`).
For `ALIGN(0x80)` the steps are at about 0x31e8, 0x3228, 0x32a8, 0x3328, 0x33a8, 0x3428, 0x34a8, a period of 0x80 from 0x3228.
For `c >= 0x3228` with n = 0x80 (and `c >= 0x32a8` with n = 0x100) the value fits `align_down(c - 0x28, n) + 2n`; 0x28 is not
explained (the `.sbss` here is 8 bytes). Below that the value is `align(c, n)` plus either 0 or n, with no translation-invariant
rule. A formula for the whole range was not found; the replica keeps `align(c, n)`, which is right for n <= 0x40 and for the
Echoes DOL, whose map matches.

### `_f_stack_rom` resolved: the file position is aligned too (`FUN_00470880`, `FUN_004703e0`, measured on 1900 layouts)

With a command file the linker keeps a file position `p` next to the address cursor `cur` (`FUN_00443320`, the output cursor). For each
section, phase 1 of `FUN_00470880` aligns `p` to the section's alignment `A` (`FUN_00443560(A)`), adds the bytes that took to `cur`
(`R0.cursor += p' - p`, only in a full link), and then aligns `cur` itself to `A`; the file position does **not** take that second
padding (the map's File Offset column shows it). A section in the bss moves `cur` but not `p`, so the two drift, and the same
alignment is applied twice, in two coordinate systems whose origins are not multiples of `A` (the file offset of `.text` is 0xe0 at
0x80003100, a difference of 0x3020): for `A` above 0x20 that moves the address by up to `A` more than `align(cur, A)`, which is
the extra `n` of the earlier tables and also the reason a non-empty `.sbss ALIGN(0x80)` lands at 0x3200 and not 0x3180.
An empty section is given back afterwards (`FUN_004703e0`: `cur` and `p` return to their value before the section), but the ROM
address `_f_X_rom` of the section was taken from the padded address, so it is the address `place` gives. The earlier note that
the rule 'is not a function of `(c, n)`' was true: it also depends on `p`. `Layout::new` implements this (`place`) for links with a
command file; the probe sweep and a scan over `.data`, `.sdata`, `.sbss` and `.stack` alignments (1900 layouts of four objects)
match the real linker in ELF and map. A partial link does not give anything back and keeps no `cur`, so it is unchanged.

#### `synthvoice.o` read again (no chunk gaps in the object)

The Echoes `synthvoice.o` has one section symbol per section (`.text`, `.bss`, `.line`, `.debug`, `.sbss`, `.mwcats.text`), so the
one-chunk-per-section-symbol rule of `FUN_0043afa0` gives its `.text` a single chunk, and every function in it is a sized global
or local function symbol (types 2, no odd `st_other`). The object was compiled with `-sym on`: it carries `.line` and `.debug`
sections with relocations against the functions. Whatever makes the real linker reach `voiceKill` late in a plain `-r` walk is
therefore not visible in the symbol table; the debug relocations are the remaining lead. It only changes the order of the
undefined-symbol list of a plain `-r` link of big objects (the `.preplf` and `.plf` steps of Echoes match on all 86 files and the
DOL link does not print that list).

## Names in the Ghidra program

The functions worked out above are named in `mwld-gc-2-7` (each carries a plate comment with the formula or the role):
`LinkAll` (0x4421c0), `LayoutAndWriteOutput` (0x44a700), `WriteElfSectionHeader` (0x44a660),
`PlaceOutputSection` (0x470880: file position aligned first, its padding added to the address cursor, then the address aligned
without moving the file position), `FinishOutputSection` (0x4703e0: gives the padding back for an empty section in a full link),
`AdvanceRegionCursors` (0x4702c0), `GetSectionRegion` (0x470200), `RecordSectionAddresses` (0x470240),
`EvalSectionHeaderAddrExpr` (0x4705b0), `GetOutputCursor` / `AdvanceOutputCursor` / `AlignOutputCursor`
(0x443320 / 0x4435c0 / 0x443560), `StoreLinkerGeneratedSymbolValues` (0x44b390), `ReportForceActiveSymbolNotFound` (0x43e110),
`ParseLcfRomaddr` (0x45f550), `ParseLcfSectionHeader` (0x45ff90), `BuildSectionChunks` (0x43afa0),
`PairExtabExtabindexRelocs` (0x44ca90), `AddCppRuntimeRoots` (0x436b90), and a few helpers around them.

## Known limits and review notes

Not replicated (Echoes uses none of them; an unsupported option is rejected with an error): `-sym`/DWARF output, `-listclosure`,
`-listdwarf`, `-romaddr` and overlays. Open: the order of the undefined-symbol list of a plain `-r` link of `seq.o` with
`synthvoice.o` (the debug relocations are the lead; the `.plf`, `.preplf` and DOL outputs are unaffected).

A review pass over `link.rs` and `writer.rs` left these speed-ups unapplied, because none changes output and each needs a
restructuring that must be re-verified against every oracle: the archive loop re-resolves all symbols per loaded member; `atom_of`
is a linear `find` (a binary search would change the result for atoms that overlap, which the linker's own `find` order
resolves by taking the first); `.mwcats.text` entries are re-derived per use; `Link::relocs` clones (callers mutate `self`
while iterating, so a slice does not borrow-check).

### The walk's rest-walk is gated per chunk (`FUN_0043c490`, read again)

The tail of the walk function (named `WalkSymbolReferences` in Ghidra) decides the "rest-walk" of rule 2 with flags on the *chunk*
record (`iVar8`, the entry `BuildSectionChunks` made), not on the section: after the frame's relocations, if (chunk bit 1 is set,
or `DAT_004d5d3a` is 0) and the section kind is below 0x24 and chunk bit 3 is clear, it sets bit 3 and walks the remaining
symbols of that chunk in symbol-table order (up to the next chunk's first symbol), then the chunk's head symbol
(`ChunkHeadSymbol`, `FUN_0043c440`). Bit 1 is set when the head symbol is the walked symbol itself. So the replica's
"the first frame of a section rest-walks" is the single-chunk case. A section with several chunks rest-walks once per chunk,
which is the only difference left for `synthvoice.o`, and why the replica's per-section rules needed the extra conditions in
rule 2. Chunk boundaries come from `BuildSectionChunks`; what splits `synthvoice.o`'s `.text` into chunks is still not found.
Not yet used by the replica (the Echoes `.plf`, `.preplf` and DOL links match without it).

#### `seq.o` + `synthvoice.o` recompiled with `-sym off`: the debug relocations are not the lead

Both objects were recompiled from `extern/musyx/src/musyx/runtime/{seq,synthvoice}.c` with the Echoes flags (compiler 1.3.2,
`-sym on` and `-sym off`) and linked with `-r -m _start`. The ELF files are identical, and the NOT FOUND list has the same
names in both linkers for both settings; the order differs the same way. In the real list `macMakeInactive`, `hwBreak` and
`streamKill` come after `synthIdleWaitActive`/`_restgpr_23`, `hwIsActive` and `_savegpr_26`/`_restgpr_26`; the replica lists
those three right after `synthVoice`. So the debug sections do not cause it (the earlier note naming them as the lead is
wrong), and the difference reproduces from the sources with the Echoes flags, which allows bisecting the source. It stays
open.

## Corruption prototype (`G2MEAB`, 2.7): segment permissions are the OR of the inputs

`F:\programming\decomp\corruption-proto` links 1459 objects (`-fp hardware -nodefaults -mapunused -lcf build/G2MEAB/ldscript.lcf`,
no `-r` steps). The map was identical from the start; the ELF differed in one program header: the `.sdata2` segment is
read-write (`flags 6`) in the real file, but the replica gave read-only, because the first input (`auto_11_8079BA40_sdata2.o`)
is read-only while 90 of the 224 inputs are writable. The first input is not the rule, and neither is the last input (also
read-only). The OR of the inputs' flags is (it is what the section header already used): the link is now byte-identical, as are the
Echoes DOL, the Prime `main.elf`, the `.plf`/`.preplf` links and the four `extab`/`extabindex`-into-`.data` command files
(`*(extab) *(extabindex) *(.data)`, `*(.data) *(extab) *(extabindex)`, and the single-section variants) built over the same
objects. The earlier note that `*(.data) *(extab) *(extabindex)` stays read-only and that an AND is needed does not reproduce.
