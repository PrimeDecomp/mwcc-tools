MWCC GC 2.x
===========

A decompilation of `mwcceppc.exe`, the Windows/x86 CodeWarrior compiler for GameCube, for the 2.x codegen line: the
compilers built from C/C++ 2.4.x by the CodeWarrior Pro 6 x86 compiler, which share one source.

| Version | Compiler | Linked from source (functions) | Bytes | Notes |
|---|---|---|---|---|
| `GC_2_0` | 2.4.7 build 92 | 2850 / 4068 (70.1%) | 54.3% | |
| `GC_2_0p1` | 2.4.7 build 92 | 2850 / 4068 (70.1%) | 54.3% | 2.0 patched (bfbb) |
| `GC_2_5` | 2.4.7 build 105 | 2860 / 4079 (70.1%) | 54.3% | |
| **`GC_2_6`** | 2.4.7 build 107 | 2860 / 4080 (70.1%) | 54.4% | the primary version (TTYD) |
| `GC_2_7` | 2.4.7 build 108 | 2848 / 4087 (69.7%) | 54.2% | |

`GC_2_0p2` is not a release: a compiler for Battle for Bikini Bottom, whose retail build used a 2.0 that no release
matches. It is 2.0 with floating-point literals not const (what 2.0p1's patch does), the memory rules BFBB's code
shows (a store addressed by a global's symbol, and a load of a floating-point literal, may touch any memory a pointer
reaches; the scheduler orders direct accesses to globals that way too) and two of 2.5's alias changes. On the BFBB
decomps it matches as many functions as the best binary-patched compiler (docs/agents-notes.md, "GC_2_0p2"). It has
no original to match, so it is only built from the sources: `ninja build/GC_2_0p2/source-only/mwcceppc.exe`.

"Linked from source" is the measure of progress: the functions the `--relink` build takes from compiled source, out of
every function of the original. A function counts once its whole unit links into the original byte for byte (all its
functions exact, its data, .bss and order the original's, `tools/relink.py`). The comparison's first line
(`build/VERSION/ok`) gives it in functions and bytes, with the count of exact functions, and `tools/status.py VERSION
--todo` lists the units nearest to linking with what still blocks each.

A function is *exact* when its compiled C is the original's code byte for byte (relocations resolved). Every other
function of the original is *lifted*: its machine code, relocated, so that the compiler links from this repository
(the *equivalent compiler*). The 2.6, 2.5 and 2.7 equivalent compilers produce objects byte-identical to the
originals' on every source of [doldecomp/ttyd](https://github.com/doldecomp/ttyd) (129) the original compiles.

Where it comes from
-------------------

- [rayanht/mwcc](https://github.com/rayanht/mwcc) (GC 1.2.5, matching) and
  [inspiredrobot/mwcc](https://github.com/inspiredrobot/mwcc) before it: the project's structure and most of the C.
- [henriquegemignani/mwcc](https://github.com/henriquegemignani/mwcc/tree/build-1.3) `build-1.3`: the 1.3 behaviour of
  the sources, the 1.3 units (pointer analysis, empty-loop removal), the linked executable and lifting.
- This repository: the sources resolved to the 2.x line (`VERSION` tests are between 2.x versions), GC 2.6's layout
  ported from 1.3 (`tools/port_version.py`) and the other versions' from 2.6, and the tools below.

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
and objdiff's command line for the progress report. Nothing of the original executables is committed.

Building
========

```sh
python configure.py              # GC_2_6; --version GC_2_0 (or GC_2_0p1, GC_2_5, GC_2_7)
ninja                            # compile the sources and compare each function with the original's
ninja build/GC_2_6/mwcceppc.exe  # the equivalent compiler: the sources, lifted code and the original's data
ninja verify                     # every version: compared, laid out, relinked, its equivalent compiler linked
```

`ninja` fails when a function of a Matching source differs. One `build.ninja` holds every version (`--version` picks
the default target), so `ninja verify` checks them all without reconfiguring; CI runs it on every push.
`python tools/verify.py` is `ninja verify` plus `tools/regressions.py` (nothing lost against your saved baseline).

Relinking the original
----------------------

```sh
python configure.py --relink   # GC_2_6 (--version for another)
ninja                          # split the original, link it, check its SHA-1 (build/GC_2_6/relink/ok)
```

As in decomp-toolkit projects, the original is split into one COFF object per translation unit (`tools/split.py`, from
`config/<version>/symbols.txt` and `splits.txt`) and linked in the original's order with the Pro 5.3 updater's `mwld`,
from the compiled object of each Matching source that links and the split objects of the others; the executable's
SHA-1 must be the original's. Every version relinks (`ninja build/VERSION/relink/ok`; `ninja verify` relinks them
all); `--relink` makes the selected one's the default target and its `objdiff.json` a unit per translation unit.

objdiff and editors
-------------------

- **objdiff**: `python configure.py --relink && ninja`, then open the repository in objdiff: a unit per translation
  unit, with line numbers wherever compiling the source with symbols on leaves its code as it is
  (`build/VERSION/objdiff/`). The default build's `objdiff.json` has a unit per function, for the progress report
  (`ninja progress`).
- **clangd** (VS Code: the clangd extension, with Microsoft's C/C++ IntelliSense off): `configure.py` writes
  `compile_commands.json`, each source with the configured version's include paths and defines for an i386 clang. The
  MSL/runtime wrappers that include `lib/`'s file of their own name loop under clang, which cannot stop `"..."`
  includes searching the including file's directory as MWCC's `-i-` does.

Debugging information (IDA, Ghidra, Binary Ninja, debuggers)
------------------------------------------------------------

`python configure.py --relink && ninja` also writes, beside the exact relink (`build/VERSION/relink/mwcceppc.exe`),
**the original with debugging information**: its code and data byte for byte, with what the sources say of it.

| File | For | Has |
|---|---|---|
| `mwcceppc-debug.exe` + `mwcceppc-debug.pdb` | IDA, Binary Ninja, x64dbg, Ghidra (types, prototypes, variables, lines) | a PDB 7.0, named by the exe's debug directory |
| `mwcceppc-dwarf.exe` | Ghidra (everything, arguments and locals included), Binary Ninja, IDA | the same as DWARF 4, in sections of the exe |
| `mwcceppc-debug.txt` | you | counts, and the types the units define differently |

Both have a COFF symbol table of every symbol in `config/VERSION/symbols.txt`. Open one or the other, not both: a tool
would apply the information twice. Ghidra applies a PDB's arguments and locals only in developer mode; its DWARF
analyzer applies them.

They hold a module per translation unit of `splits.txt`; every function of `functions.json` with its arguments, named
as `src/` names them; for each function the sources compile to, its locals and line numbers (for the others, the
prototype and first line, or nothing without a source); and every unit's types and global and static variables. It
comes from the objects compiled with symbols on (`build/VERSION/debug/compiled`): the CodeWarrior x86 compiler writes
CodeView 5, which `tools/mw/codeview.py` reads and `tools/mw/pdb.py` and `tools/mw/dwarf.py` convert.
`mwcceppc-debug.txt` lists the types the units disagree on (the most units' definition keeps the name, the others are
`NAME$2`...): each is a header inconsistency to settle.

`ninja build/VERSION/debug/mwcceppc.exe` is the equivalent compiler linked from those objects with the linker's own
CodeView (NB11): for debugging the decompilation's code, which is not the original's.

Tools
=====

Every tool is a command over one library, `tools/mw/` (`tools/mw/__init__.py` lists what each module is the one place
for), and prints its usage with `--help`. Run them with the Python that has capstone (`.venv/bin/python` after
`uv sync`). Edits to the configuration and the sources are one transaction, refused when a file changed while the tool
ran, and results computed from a stale comparison are refused (run `ninja`). docs/agents-notes.md has the workflow and
the pitfalls; docs/matching-guide.md the compiler behaviour.

**Where things stand**

- `tools/status.py VERSION [--unit S | --range A B | --todo [N]]`: each unit's functions, bytes and link state; `--todo`
  the units that do not link yet, nearest first, with what blocks each. Start here.
- `tools/fn.py show VERSION NAME`: the original's code of a function, its callees named; `fn.py diff NAME
  [--version V]`: the compiled code against it, with why it is not exact.
- `tools/crosscheck.py [VERSION]`: where the comparison and objdiff disagree on a function, and why.

**Working on a unit**

- `tools/fn.py variants NAME FILE`, `fn.py decl-order NAME`: alternative bodies compiled and judged in parallel; the
  order of local declarations closest to the original's.
- `tools/unit.py check|order|move|assign`: one source compiled outside the build; its functions put in the original's
  order; functions moved into it; rows given it as their source.
- `tools/evidence.py unit|align|units|pair|sweep|review|apply`: what the evidence says each function is (asserts,
  Ninji's Pro 7 source, the Wii compilers' CodeView, MSL), the unit table (`docs/units-2.6-table.md`), the names to
  check (`docs/names-2.6.md`) and the decisions (`config/GC_2_6/name_review.json`).
- `tools/rename.py OLD NEW...`: rename functions and variables everywhere, all at once or not at all; `--unit` renames
  a source, `--member RECORD.OLD=NEW` a struct member.
- `tools/headers.py own|bisect`, `tools/includes.py trim|add|fix|sort`: headers as the units' APIs; include lists kept
  to what a source uses.
- `tools/clang_check.py [FILTER]`: clang errors not in the saved baseline (implicit declarations included).
- `.venv/bin/clang-format -i FILE...`: the repository's layout (`.clang-format`); CI fails on unformatted sources.
- `tools/update_assert_lines.py [VERSION] [--write]`, `tools/compilers.py host|deferred`: assert line numbers; which
  host compiler and inlining mode built each unit.

The editing tools find code with clang's syntax tree (`tools/mw/csrc.py`): a function's comments, and the pragmas and
`#if` that belong to it alone, move with it; a move that would change its `#pragma` settings or `#if` branch is
refused.

**After a change**

- `tools/verify.py`; `tools/regressions.py [save] [VERSION...]`: functions exact in the saved baseline and not now,
  link progress then and now; `save` takes a new baseline.
- `tools/update_state.py`, `tools/layout.py [--check]`, `tools/relink.py [--only SOURCE...]`: the Matching sources,
  `symbols.txt`/`splits.txt`, and which Matching sources link (into `state.json`'s `relink_exclude`).
- `tools/equivalence.py CORPUS.json OUT [--version V]`: a corpus compiled by the original and the equivalent compiler,
  compared byte for byte; `equivalence.py corpus PROJECT GC/2.6 OUT.json` makes a corpus of a decomp-toolkit
  project's sources.
- `tools/equivalent.py where VERSION ADDRESS...`: the function of the equivalent compiler an address (a crash) is in.

**The configuration**

- `tools/validate.py [VERSION...]`: the configuration's invariants (ninja runs it before each comparison).
- `tools/bindings.py learn|audit|infer|refs`: bindings the original's code vouches for, bindings every reference
  disagrees with, the addresses a source's unbound names refer to, and who references an address (or a Wii public).
- `tools/port_version.py FROM TO`: lay out another build of the same source from a laid-out one.
- `tools/gen_options.py VERSION ROOT -o DIR`: the command line's option tables (the `.opt` files), from the original.

Run by ninja: `compare.py`, `split.py`, `link.py`, `implib.py`, `equivalent.py`, `link_debug.py`, `download_tool.py`,
`transform_dep.py`.

Project structure
=================

- `config/sources.json`: each source's compiler and flags
- `config/<version>/config.json`: the original executable and its SHA-1, per-version compilers and flag overrides,
  data placement for what nothing else places
- `config/<version>/state.json`: the Matching sources and the relink's exclusions (generated: `tools/update_state.py`,
  `tools/relink.py`)
- `config/<version>/functions.json`: each function's address, size, name and source
- `config/<version>/bindings.json`: the addresses of the data and functions the sources reference
- `config/<version>/symbols.txt`, `splits.txt`: the original's symbols and translation units (generated:
  `tools/layout.py`)
- `src/`: the compiler (`driver`, `frontend`, `optimizer`, `backend`), its C library and runtime (`msl`, `runtime`),
  and the import libraries the source-only build links with (`imports`)
- `include/`: headers; `include/version.h` the versions
- `docs/`: [agents-notes.md](docs/agents-notes.md) (working on the decompilation),
  [matching-guide.md](docs/matching-guide.md) (MWCC's behaviour), the 2.6 ledgers (audit, data audit, names,
  near-misses, review, units)

License
=======

The project's files are dedicated to the public domain under CC0 (`LICENSE`), as are the projects it comes from.
