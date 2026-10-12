# The oracle executable (GC 1.3 and 1.3.2)

The oracle is the original `mwcceppc.exe` relinked with trace wrappers around the register allocation and the inliner.
It writes the same objects, messages and exit code as the original (the wrappers only print), and says why it
assigned the registers it did and which calls it inlined. It is **not** built from this repository's C sources of the
allocator and inliner: those are not byte-exact for 1.3/1.3.2, so the original's own code runs and the wrappers
(`src/oracle/Hooks.c`, `src/oracle/OracleTrace.c`) are linked around it.

It needs the original compiler, which `ninja` downloads to `build/compilers/GC/<version>/` (do not
`ninja -t clean` the oracle target: it removes the downloaded compilers). `tools/oracle.py` also runs that original
compiler, to check that the oracle's object is the same (`--no-check` skips it), so both are needed where it runs.

## Build

```
python configure.py --version GC_1_3_2 --mode link --oracle
ninja build/GC_1_3_2/oracle/mwcceppc.exe
```

`GC_1_3` works the same way. `configure.py` writes `build.ninja` for one version, so re-run it when switching.
Which functions are wrapped is `oracle.hooks` in `config/<version>/config.json` (retail symbol names; keep the
wrappers in `src/oracle/Hooks.c` and that list consistent). The licence stub `lmgr326b.dll` (`tools/lmgr_stub`) is
copied beside the executable, on every platform: to run the oracle on another Windows machine, copy
`build/<version>/oracle/mwcceppc.exe` and `lmgr326b.dll` (and `build/compilers/GC/<version>/` for the check).

The executable is a Windows program. On Linux or WSL the build runs it through wibo, and `tools/oracle.py` does the
same (`build/tools/wibo`, or a `wibo` on the PATH); it does not need binfmt_misc or the executable bit.

## Explain a source: `tools/oracle.py`

```
python tools/oracle.py [--version GC_1_3_2] [--function NAME] [--raw] [--pcode] [--no-check] SOURCE [COMPILER FLAGS...]
python tools/oracle.py --version GC_1_3_2 --from-ninja ../prime/build.ninja src/GuiSys/CGuiFrame.cpp
```

`--from-ninja FILE` takes the flags and the working directory from the rule that builds SOURCE in a decomp project's
`build.ninja`: the directory is the one that file is in (the project's root), and SOURCE is looked up as given, with
either `/` or `\` as the separator. Without it, give the flags after the source and the current directory is used. The
report, per function, lists

* the inliner's decisions: inlined (with the cost estimate against the threshold) or kept, and why (not declared
  inline, over budget, ...);
* the register assignment: each virtual register, the physical register it got and the reason (neighbours'
  registers, constraints, coalescing, spills), then the order the virtual registers are coloured in and how well a
  greedy colouring in that order reproduces the allocator's choices. The order is *assumed* (the highest-numbered
  virtual register first, as `src/backend/Coloring.c` selects), not traced: it is what explains a swap between two
  registers, since the one created later is coloured first and takes the lowest free register.

`--function` restricts the report, and `--raw`'s trace lines, to the functions whose name contains NAME. `--pcode`
prints the PCode the allocator was given, per block, with the virtual register numbers (`%r60` is virtual register 60)
that the register report names; that is how a number is traced back to the statement that defined it. The first line
says whether the object equals the original compiler's; when it says NO, the explanation is of a different object and
should not be trusted.

From Python, `tools/oracle.py` can be imported: `parse_trace(lines)` gives the snapshots before and after each
allocation and `opcode_names(version)` the opcode names, for views of your own.

The raw trace goes to standard error, or to the file named by `MWCC_ORACLE_TRACE_FILE` (set it to the null device when
only the object is wanted).

## Check the equivalence: `tools/corpus.py`

```
python tools/corpus.py --version GC_1_3_2 --canaries path/to/mwcc-rs/canaries --jobs 8
python tools/corpus.py --version GC_1_3_2 --prime path/to/prime --keep out-dir
```

Compiles every input with the original and the oracle and compares the sections that are not debug information (the
size of `.bss`), the messages and the exit code; prints `DIFF name: ...` per difference and `N/M identical`.
`--canaries` takes a directory of small programs (a first line `// flags: ...` overrides the baseline flags), `--prime`
a decomp project whose `build.ninja` rules that use this version's compiler are compiled with their own flags.
`--limit`, `--list FILE` and `--keep DIR` narrow the input or keep the objects of the inputs that differ. The exit
status is 0 when nothing differs. Last result: 2313/2313 canaries on both versions; Prime 604/604 (1.3.2) and 64/64
(1.3).

## Other tools

* `tools/locate.py VERSION SOURCE...`: where a compiled source's functions are in the executable.
* `tools/nearest.py VERSION SOURCE...`: the functions of the executable a not-yet-exact function resembles most.
* `tools/fdiff.py VERSION SOURCE FUNCTION ADDRESS`: instruction diff of a compiled function against the original's.
* `tools/disasm.py VERSION ADDRESS [SIZE]`: annotated disassembly of the original executable.
* `tools/gates.sh`: every version's relink/compare gate, one after the other.
* `docs/gc132-differences.md`: what changed between 1.3 and 1.3.2.
