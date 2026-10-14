# The oracle executable (GC 1.3, 1.3.2 and 2.7)

The oracle is the original `mwcceppc.exe`, relinked with trace wrappers around some of its functions. It produces
the same objects, messages and exit code as the original; the wrappers only print. From the trace, `tools/oracle.py`
explains why the compiler assigned each register, which calls it inlined, and how it laid out the stack frame.

The original's own code runs: the oracle is not built from this repository's C sources of the allocator and the
inliner, which are not byte-exact for 1.3 and 1.3.2 (2.7 has no sources at all). The wrappers are in
`src/oracle/Hooks.c` and `src/oracle/OracleTrace.c`.

More detail:

* [Registers and scheduling](oracle/registers.md): the register report, `--pcode`, `--exchange`, `--schedule`.
* [Inlining](oracle/inlining.md): `--inline-cost`, `--near`, `--full-cost`, `--instantiation`.
* [Locals and the stack frame](oracle/locals.md): `--locals`, and comparing a frame with the target's.
* [Basic blocks](oracle/blocks.md): `--blocks`, where code generation cuts blocks and why they are not merged.
* [Common subexpressions](oracle/cse.md): `--cse`, `--ir`, the order of the optimizer's temporaries and their registers.
* [Comparing translation units](oracle/tu-tools.md): `tools/inline_diff.py`, `tools/data_diff.py`,
  `tools/inline_window.py` and `tools/header_impact.py`.
* [Trace format, JSON and Python API](oracle/trace.md).

## Build

```
python configure.py --version GC_1_3_2 --mode link --oracle
ninja build/GC_1_3_2/oracle/mwcceppc.exe
```

* `GC_1_3` and `GC_2_7` work the same way. `configure.py` writes `build.ninja` for one version, so re-run it when
  switching versions.
* `ninja` downloads the original compiler to `build/compilers/GC/<version>/`. Do not `ninja -t clean` the oracle
  target: it removes the downloaded compilers.
* The wrapped functions are listed in `oracle.hooks` in `config/<version>/config.json` (retail symbol names). Keep that
  list consistent with the wrappers in `src/oracle/Hooks.c`.
* The licence stub `lmgr326b.dll` is copied beside the executable. To run the oracle on another Windows machine, copy
  `build/<version>/oracle/mwcceppc.exe` and `lmgr326b.dll`, plus `build/compilers/GC/<version>/` for the check below.
* On Linux or WSL, the build and `tools/oracle.py` run the executable through wibo (`build/tools/wibo`, or a `wibo` on
  the PATH). Neither binfmt_misc nor the executable bit is needed.

## Explain a source: `tools/oracle.py`

```
python tools/oracle.py [OPTIONS] SOURCE -- [COMPILER FLAGS...]
python tools/oracle.py --from-ninja ../prime/build.ninja src/GuiSys/CGuiFrame.cpp --function Draw
```

**Flags and version.**

* Give the compiler's flags after `--`. The older form, flags right after SOURCE, still works.
* `--from-ninja FILE` takes the flags, the working directory and the version from the rule that builds SOURCE in a
  decomp project's `build.ninja`. SOURCE is relative to that file's directory, or absolute. Giving flags as well is an
  error.
* The version comes from the rule's `mw_version` (`GC/2.7` is `GC_2_7`), or `--version`. It defaults to `GC_1_3_2`.

**The check.** The script also compiles SOURCE with the original compiler and compares the objects. The first line of
the report says whether they are equal. If it says NO, it lists the sections and functions that differ, and the
explanation is of a different object: do not trust it. `--no-check` skips the check.

**The report**, per function (`Think (Think__10CPowerBombFfR13CStateManager)`), has three parts:

* **Inlining:** each call inlined or kept, with the cost estimate against the threshold and the reason. A decision
  made several times is shown once, with its count, inline depths and source lines. See [Inlining](oracle/inlining.md).
* **Register allocation**, per register class: the colouring order, and for each virtual register its physical
  register and why it got it. See [Registers](oracle/registers.md).
* **Stack frame:** its layout, and the locals and temporaries in memory, by offset from r1 (`r1+28`, as the code
  addresses them).

**Options.** Each view is traced only when asked for; the object is the same either way.

| Option | Shows |
|---|---|
| `--function NAME` | Only the functions whose name or link name contains NAME. With no match, the closest names are listed. |
| `--pcode` | The PCode the allocator was given, with virtual register numbers (`%r60`). |
| `--exchange A,B` | Why two virtual registers were coloured in that order and got the registers they did. |
| `--schedule` | The instruction scheduler's picks per cycle, and why each other candidate lost. |
| `--inline-cost CALLEE` | The inliner's decisions about CALLEE, with the cost broken down by statement. |
| `--near N` | Every automatic inlining decision within N of the threshold: the ones a header change can flip. |
| `--full-cost` | The whole cost past the threshold, and which kept calls the inline depth limit decided. |
| `--instantiation TEXT` | When the bodies of matching functions were queued, parsed, emitted and compiled. |
| `--locals` | What the scalarizer did with each local aggregate, and our frame against the target's. |
| `--blocks` | Where code generation cut the function into blocks, and why the merge pass left them apart. |
| `--cut-limit N`, `--merge-limit N` | Compile with other block limits (patched copies of both compilers). See [Basic blocks](oracle/blocks.md). |
| `--keep-object PATH` | Also write the oracle's object to PATH. |
| `--cse` | The common subexpression temporaries in the order made, with the repeat that made each and its register. |
| `--ir LINE` | With `--cse`, the linear IR of a source line (or `all`) as each pass starts. |
| `--override PATH=REPLACEMENT` | Compile with REPLACEMENT in place of PATH, without touching the checkout (repeatable). |
| `--raw` | The trace lines (see [Trace format](oracle/trace.md)). |
| `--json` | Everything as one JSON document (see [Trace format](oracle/trace.md)). |

`--override` works for headers and for SOURCE itself. A header's REPLACEMENT is copied to a temporary directory that is
searched just before PATH's include directory. The check compiles the same replacement with the original.

## Check the equivalence: `tools/corpus.py`

```
python tools/corpus.py --version GC_1_3_2 --canaries path/to/mwcc-rs/canaries --jobs 8
python tools/corpus.py --version GC_1_3_2 --prime path/to/prime --keep out-dir
```

This compiles every input with the original and with the oracle, then compares the non-debug sections (and the size of
`.bss`), the messages and the exit code. It prints `DIFF name: ...` for each difference, then `N/M identical`, and
exits with 0 when nothing differs.

* `--canaries DIR`: small programs. A first line `// flags: ...` overrides the baseline flags.
* `--prime DIR`: a decomp project. The `build.ninja` rules that use this version's compiler are compiled with their own
  flags.
* `--limit`, `--list FILE` and `--keep DIR` narrow the input, or keep the objects of the inputs that differ.

Last result, the same with every trace on:

| Corpus | Versions |
|---|---|
| Canaries | 2313/2313 on all three |
| Metroid Prime | 604/604 (1.3.2), 64/64 (1.3) |
| Metroid Prime 2: Echoes | 729/729 (2.7), 31/31 (1.3.2) |

## Other tools

* `tools/inline_diff.py`: why one translation unit inlines a call another keeps. See
  [Comparing translation units](oracle/tu-tools.md).
* `tools/data_diff.py`: a translation unit's data layout against its target asm. See
  [Comparing translation units](oracle/tu-tools.md).
* `tools/inline_window.py`: the range of inline limits a unit's target agrees with. See
  [Comparing translation units](oracle/tu-tools.md).
* `tools/header_impact.py`: every function a header change moves, in every unit that includes it, against the
  target. See [Comparing translation units](oracle/tu-tools.md).
* `tools/locate.py VERSION SOURCE...`: where a compiled source's functions are in the executable.
* `tools/nearest.py VERSION SOURCE...`: the functions of the executable a not-yet-exact function resembles most.
* `tools/fdiff.py VERSION SOURCE FUNCTION ADDRESS`: an instruction diff of a compiled function against the original's.
* `tools/disasm.py VERSION ADDRESS [SIZE]`: annotated disassembly of the original executable.
* `tools/gates.sh`: every version's relink and compare gate, one after the other.
* `docs/gc132-differences.md`: what changed between 1.3 and 1.3.2.
* `docs/gc27-differences.md`: what changed between 1.3.2 and 2.7, and how 2.7's configuration was made.
