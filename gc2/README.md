MWCC GC 2.x
===========

A decompilation of `mwcceppc.exe`, the Windows/x86 CodeWarrior compiler for GameCube, for the 2.x codegen line: the
compilers built from C/C++ 2.4.x by the CodeWarrior Pro 6 x86 compiler, which share one source.

| Version | Compiler | Linked from source | Functions exact | Notes |
|---|---|---|---|---|
| `GC_2_0` | 2.4.7 build 92 | 1197 / 4044 (29.6%) | 3193 / 3987 | |
| `GC_2_0p1` | 2.4.7 build 92 | 1197 / 4044 (29.6%) | 3192 / 3987 | 2.0 patched (bfbb) |
| `GC_2_5` | 2.4.7 build 105 | 1805 / 4057 (44.5%) | 3670 / 4042 | |
| **`GC_2_6`** | 2.4.7 build 107 | 2101 / 4080 (51.5%) | 3713 / 4074 | the primary version (TTYD) |
| `GC_2_7` | 2.4.7 build 108 | 1692 / 4065 (41.6%) | 3662 / 4036 | |

`GC_2_0p2` is not a release: a compiler for Battle for Bikini Bottom, whose retail build used a 2.0 that no release
matches. It is 2.0 with floating-point literals not const (what 2.0p1's patch does) and the memory rules BFBB's code shows (a store addressed by a global's symbol, and a load of a
floating-point literal, may touch any memory a pointer reaches; the scheduler orders direct accesses to globals that
way too) and two of 2.5's alias changes. On the BFBB decomps it matches as many functions as the best binary-patched
compiler (docs/agents-notes.md, "GC_2_0p2"). It has no original to match, so it is only built from the sources:
`ninja build/GC_2_0p2/source-only/mwcceppc.exe`.

"Linked from source" is the measure of progress: the functions the `--relink` build takes from compiled source, out of
every function of the original. A function counts once its whole unit links into the original byte for byte (all its
functions exact, its data, .bss and order the original's, `tools/relink.py`). The comparison's first line
(`build/VERSION/ok`) gives it in functions and bytes, and `tools/status.py VERSION --todo` lists the units nearest
to linking with what still blocks each one.

"Functions exact" counts the functions whose compiled C is the original's code byte for byte (relocations resolved).
Every other function of the original is *lifted*: its machine code, relocated, so that the compiler links from this
repository (the *equivalent compiler*). The 2.6, 2.5 and 2.7 equivalent compilers produce objects byte-identical to
the originals' on every source of [doldecomp/ttyd](https://github.com/doldecomp/ttyd) (129) the original compiles.

Where it comes from
-------------------

- [rayanht/mwcc](https://github.com/rayanht/mwcc) (GC 1.2.5, matching) and
  [inspiredrobot/mwcc](https://github.com/inspiredrobot/mwcc) before it: the project's structure and most of the C.
- [henriquegemignani/mwcc](https://github.com/henriquegemignani/mwcc/tree/build-1.3) `build-1.3`: the 1.3 behaviour of
  the sources, the new 1.3 units (pointer analysis, empty-loop removal), the linked executable and lifting.
- This repository: the sources resolved to the 2.x line (no 1.2.5 or 1.3 conditionals left: `VERSION` tests here are
  between 2.x versions), GC 2.6's layout ported from 1.3 (`tools/port_version.py`) and the siblings' from 2.6, 2.6's
  structure changes (the options struct, `COpts`, gained 8 bytes), and the tools below.

Ninji's decompilation of the CodeWarrior Pro 7 (2.4.5) Mac OS X compiler is the reference for code nobody has
decompiled here yet; it is not part of this repository.

Dependencies
============

- Python 3.11+, with [capstone](https://www.capstone-engine.org): `uv sync` (`pyproject.toml`), or
  `pip install capstone`
- [ninja](https://github.com/ninja-build/ninja)
- [unshield](https://github.com/twogood/unshield), to unpack the CodeWarrior Pro 5.3 updater
- for the editing tools and clangd only: [clang](https://clang.llvm.org); `uv sync` installs the pinned clang-format

ninja downloads the rest, each checked against the SHA-256 `tools/download_tool.py` pins: the original executables
(decomp.dev's compiler archive), the CodeWarrior Pro 4, 5, 5.3 and 6 compilers the sources build with and the Pro 5.3
linker (archive.org), MSL and the runtime's sources (`lib/`), [wibo](https://github.com/decompals/wibo) to run them,
and objdiff's command line for the progress report.

Building
========

```sh
python configure.py            # GC_2_6; --version GC_2_0 (or GC_2_0p1, GC_2_5, GC_2_7)
ninja                          # compile the sources and compare each function with the original's
ninja build/GC_2_6/mwcceppc.exe  # the equivalent compiler: the sources, lifted code and the original's data
ninja verify                   # every version: compared, laid out, relinked, its equivalent compiler linked
```

Nothing of the original executables is committed: they come from the
[decomp.dev compiler archive](https://files.decomp.dev); the lifted code and the original's data (for what the
sources do not define yet) are linked from it into `build/`.

`ninja` fails when a function of a Matching source differs. One `build.ninja` holds every version (`--version` picks
the default target), so `ninja verify` checks all five without reconfiguring; CI runs it on every push.
`python tools/verify.py` is `ninja verify` plus `tools/regressions.py` (nothing lost against your saved baseline).

Relinking the original
----------------------

```sh
python configure.py --relink   # GC_2_6 (--version for another)
ninja                          # split the original, link it, check its SHA-1 (build/GC_2_6/relink/ok)
```

As decomp-toolkit projects do (and [rayanht/mwcc](https://github.com/rayanht/mwcc) for GC 1.2.5): the original is
split into one COFF object per translation unit (`tools/split.py`, from `config/<version>/symbols.txt` and `splits.txt`),
and linked in the original's order with the original's linker, the Pro 5.3 updater's `mwld`, from the compiled object of
each Matching source that links into the original and the split objects of the others; the executable's SHA-1 must be
the original's. Linked from split objects alone it is the original. Every version relinks
(`ninja build/VERSION/relink/ok`; `ninja verify` relinks them all); `--relink` makes the selected one's the default
target and its `objdiff.json` a unit per translation unit.

- `tools/layout.py [VERSION...] [--check]`: write `symbols.txt` and `splits.txt` from `functions.json`,
  `bindings.json`, `config.json`'s placement hints and the compiled objects (which place units and data; names come
  from the configuration, and where the objects disagree with it, it warns). Run it after a change that moves code or
  data; `ninja verify` fails while they are out of date.
- `tools/relink.py [VERSION...]`: try each Matching source's object in its unit's place; those that do not link
  into the original go in `config/<version>/state.json`'s `relink_exclude`, with what differs. With
  `--only SOURCE...`, just those sources, on top of the ones that link, with where the linked bytes first differ;
  nothing is written.
- `tools/equivalent.py VERSION [--debug]` (`ninja build/VERSION/mwcceppc.exe`): the equivalent compiler, linked from
  the sources, the original's code for each function that is not exact yet, and the original's data for what the
  sources do not define yet; `tools/equivalent.py where VERSION ADDRESS...`, the function of it an address (a crash)
  is in.

objdiff and editors
-------------------

- **objdiff**: `python configure.py --relink && ninja`, then open the repository in objdiff. That `objdiff.json` has a
  unit per translation unit, each built by ninja, with line numbers wherever compiling the source with symbols on
  leaves its code as it is (most sources; `build/VERSION/objdiff/`). The default build writes an `objdiff.json` of a unit per function for
  the progress report (`ninja progress`); its objects are the comparison's output, so objdiff shows them without
  rebuilding.
- **clangd** (VS Code: the clangd extension, with Microsoft's C/C++ IntelliSense off): `configure.py` writes
  `compile_commands.json`, each source with the configured version's include paths and defines for an i386 clang. The compiler's sources parse clean; the MSL/runtime wrappers that include `lib/`'s file of their own
  name loop under clang, which cannot stop `"..."` includes searching the including file's directory as MWCC's `-i-`
  does.

Debugging information (IDA, Ghidra, Binary Ninja, debuggers)
------------------------------------------------------------

`python configure.py --relink && ninja` also writes, beside the exact relink (`build/VERSION/relink/mwcceppc.exe`),
**the original with debugging information**: its code and data byte for byte, with what the sources say of it.

| File | For | Has |
|---|---|---|
| `mwcceppc-debug.exe` + `mwcceppc-debug.pdb` | IDA, Binary Ninja, x64dbg, Ghidra (types, prototypes, variables, lines) | a PDB 7.0, named by the exe's debug directory |
| `mwcceppc-dwarf.exe` | Ghidra (everything, arguments and locals included), Binary Ninja, IDA | the same as DWARF 4, in sections of the exe |
| `mwcceppc-debug.txt` | you | counts, and the types the units define differently |

Both have a COFF symbol table of every symbol in `config/VERSION/symbols.txt` (statics and data too). Open one or the
other, not both: a tool would apply the information twice. Ghidra applies a PDB's arguments and locals only in
developer mode; its DWARF analyzer applies them.

What they hold (2.6):
- a module (compile unit) per translation unit of `splits.txt`, with its address ranges: `.text` read as the units;
- every function of `functions.json` (4080), at its address with its size, and its arguments where the call puts
  them, named as its definition in `src/` names them;
- for each function whose code the sources compile to (3207), the compiler's own debugging information: its locals
  (in the registers and stack slots the code keeps them) and line numbers; for the others, their prototype and first
  line (515: not decompiled yet, or a unit built `-sym off` whose code symbols on changes) or nothing more (358: no
  source);
- the types of every unit, merged as compilers write them (7813 records), and every global and static variable the
  units declare, typed (2005).

It comes from the objects compiled with symbols on (`build/VERSION/debug/compiled`): the CodeWarrior x86 compiler
writes CodeView 5 and has no DWARF; `tools/mw/codeview.py` reads and converts it, `tools/mw/pdb.py` and
`tools/mw/dwarf.py` write the PDB and the DWARF. `llvm-pdbutil dump` and `llvm-dwarfdump --verify` read both clean. `mwcceppc-debug.txt` lists
the types the units disagree on (the most units' definition keeps the name, the others are `NAME$2`...): each is a
header inconsistency to settle.

`build/VERSION/debug/mwcceppc.exe` (`ninja build/VERSION/debug/mwcceppc.exe`) is the
equivalent compiler linked from those objects with the linker's own CodeView (NB11): for debugging the decompilation's
code, which is not the original's.

Tools
=====

Every tool is a command over one library, `tools/mw/` (`tools/mw/__init__.py` lists what each module is the one place
for), and prints its usage with `--help`. Run them with the Python that has capstone (`.venv/bin/python` after
`uv sync`). Nothing a tool writes is half-written: edits to the configuration and the sources are one transaction,
refused when a file changed while the tool ran, and results computed from a stale comparison are refused (run
`ninja`).

**Where things stand**

- `tools/status.py VERSION [--unit S | --range A B | --todo [N]]`: each unit's functions, bytes and link state, or one
  unit's functions with the comparison's reason for each inexact one; `--todo`: the units that do not link from source
  yet, nearest first, with what blocks each. Start here.
- `tools/fn.py show VERSION NAME`: the original's code of a function, its callees named; `fn.py diff NAME`: the
  compiled code against it, with why it is not exact.
- `tools/crosscheck.py [VERSION]`: where the comparison and objdiff disagree on a function, and why.

**Working on a unit**

- `docs/audit-2.6.md`: every 2.6 unit graded A-D (October 2026) with what it needs: functions to rewrite and why,
  placeholder names, data, hacks, next steps. A snapshot; `status.py --todo` is the current state.

- `tools/evidence.py unit|align|units|pair|sweep|review|apply`: what the evidence says each function is (the
  original's asserts, Ninji's Pro 7 source, the Wii compilers' CodeView, MSL): one unit's functions with it, a unit
  beside its Pro 7 file, the unit table (`docs/units-2.6-table.md`), the list of names to check
  (`docs/names-2.6.md`), and the decisions made from it (`config/GC_2_6/name_review.json`) and their application.
- `tools/rename.py OLD NEW...`: rename functions and variables everywhere (every version's rows, bindings and symbols,
  and the code), all at once or not at all; `--unit OLD NEW` renames a source, `--member RECORD.OLD=NEW` a struct
  member at every use.
- `tools/unit.py check|order|move|assign`: one source: its functions compiled outside the build (what became exact or
  stopped being), its functions put in the original's order (ascending, or descending for a unit built
  `-inline auto,deferred`), functions moved into it in that order, rows given it as their source.
- `tools/fn.py variants NAME FILE`: alternative definitions of a function compiled in parallel and judged (and the
  unit's other functions a variant changes, through inlining); `fn.py decl-order NAME`: the order of its local
  declarations whose code is closest to the original's (MWCC's registers follow it).
- `tools/headers.py own HEADER`: make a header the one home of its declarations; `headers.py bisect SOURCE`: which
  changed file changed a source's code (MWCC's code can follow the declarations in scope).
- `tools/includes.py trim|add|fix|sort SOURCE...`: keep include lists to what a source uses (a removal must leave
  every version's object as it was).
- `tools/clang_check.py [FILTER]`: clang errors in the sources that are not in the saved baseline (implicit
  declarations included: MWCC takes them, and they change the code around them).
- `.venv/bin/clang-format -i FILE...`: the repository's layout (`.clang-format`; braces on every body included). Run
  it on what you edit by hand; the editing tools format what they write, and CI fails on unformatted sources.
- `tools/update_assert_lines.py [VERSION] [--write]`: the sources' assert line numbers where the original's differ.
- `tools/compilers.py host|deferred`: which host compiler built each unit, and how each unit would build
  `-inline auto,deferred` in the original's order.

The editing tools (`unit.py`, `fn.py`, `headers.py`, `includes.py`) find code with clang's syntax tree
(`tools/mw/csrc.py`): a function's comments, and the pragmas and `#if` that belong to it alone, move with it; a move
that would put it under other `#pragma` settings or in another `#if` branch is refused. They compile outside the
build in parallel and judge by the comparison's rule.

**After a change**

- `tools/verify.py`: every version built, compared and relinked (`ninja verify`), then `tools/regressions.py`.
- `tools/regressions.py [save] [VERSION...]`: functions exact in the saved baseline and not now (matched by address, so
  renames neither hide nor fake one), link progress then and now, and sources that no longer link; `save` takes a new
  baseline.
- `tools/update_state.py [VERSION...]`: the Matching sources (every function exact), from the last comparison.
- `tools/layout.py [VERSION...]`: `symbols.txt` and `splits.txt` (above); `tools/relink.py [VERSION...] [--only
  SOURCE...]`: which Matching sources link (above).
- `tools/equivalence.py CORPUS.json OUT [--version V]`: compile a corpus with the original and the equivalent compiler
  and compare the objects byte for byte; the test of anything that changes the sources without being exact.
  `tools/equivalence.py corpus PROJECT GC/2.6 OUT.json` makes a corpus of a decomp-toolkit project's sources.

**The configuration**

- `tools/validate.py [VERSION...]`: the configuration's invariants (ninja runs it before each comparison).
- `tools/bindings.py learn|audit|infer|refs`: bindings the original's code vouches for, bindings every reference
  disagrees with, the addresses one source's unbound names refer to, and the functions referencing an address (in the
  original or a Wii compiler).
- `tools/port_version.py FROM TO`: lay out another build of the same source from a laid-out one (functions, names,
  sources, address bindings).
- `tools/gen_options.py VERSION ROOT -o DIR`: the command line's option tables (the `.opt` files), from the original.

**Run by ninja**: `compare.py` (the comparison), `split.py`, `link.py`, `implib.py`, `equivalent.py` (the equivalent
compiler; `equivalent.py where VERSION ADDRESS...` names the function an address is in), `link_debug.py`,
`download_tool.py`, `transform_dep.py`.

Project structure
=================

- `config/sources.json`: each source's compiler and flags
- `config/<version>/config.json`: the original executable and its SHA-1, per-version compiler and flag overrides,
  placement hints for data nothing else places
- `config/<version>/state.json`: the Matching sources and the relink's exclusions (generated: `tools/update_state.py`,
  `tools/relink.py`)
- `config/<version>/functions.json`: each function's address, size, name and source
- `config/<version>/bindings.json`: the addresses of the data and functions the sources reference
- `config/<version>/symbols.txt`, `splits.txt`: the original's symbols and translation units (generated:
  `tools/layout.py`)
- `src/`: the compiler (`driver`, `frontend`, `optimizer`, `backend`), its C library and runtime (`msl`, `runtime`),
  and the placeholder data (`data`)
- `include/`: headers; `include/version.h` the version numbers

License
=======

The project's files are dedicated to the public domain under CC0 (`LICENSE`), as are the projects it comes from.
