# Using Wii_Retro in Echoes

Wii_Retro is the native relink for R32J01, R3ME01, and R3MP01. It uses stock Wii 1.0RC1 and its
runtime DLL as inputs. [Build requirements and patch list](wii-retro-compiler.md)

## Install the compiler

Use an Echoes compilers directory that already contains the stock tools. From the mwcc-tools
checkout, point the relink at its stock RC1 executable and at the separate Wii compiler
directory selected by Echoes' `retro_mw_version`.

```sh
uv run --no-sync python -I gc2/tools/wii_retro_link.py --stock COMPILERS/Wii/1.0RC1/mwcceppc.exe --out-dir COMPILERS/Wii/1.0RC1_retro
COMPILERS/Wii/1.0RC1_retro/mwcceppc.exe -version
```

Replace `COMPILERS` with the actual directory. The banner should say `Wii_Retro`. Keep
`lmgr8c.dll` beside `mwcceppc.exe`. The relink produces that pair. If your Echoes configuration
uses an assembler or linker from the same directory, keep its stock copy there.

Some Echoes checkouts generate `Wii/1.0RC1_retro` with a struct-copy-only patcher. For a local
trial, configure with `--compilers COMPILERS` first, then install the relink into that directory.
Check the banner again if Echoes regenerates its build graph. For a persistent build rule,
replace that patcher with the relink command and declare both output files. Give each compiler
revision its own path or use a fresh Echoes build directory so cached objects cannot mask a change.

## Compare results

Build R32J01, R3ME01, and R3MP01 with the same Echoes and RSTL revisions for the baseline and
candidate. Keep distinct output directories and compare exact functions as well as linked DOLs.
The [object comparison runner](../gc2/tools/wii_retro/tests/README.md#echoes-object-comparison)
compiles each configured region both ways and records which objects changed. The object sweeps
and diagnostic DOL relinks cover a subset of the Echoes acceptance suite, which also includes
all 447 image hashes. [Validation results](wii-retro-compiler-research.md#native-relink-and-validation)

If a result suggests a shared source change, test it across affected regions first. The original
`CModelFlags` alignment control and the earlier binary patch procedure are recorded with the
compiler research. [Struct-copy controls](wii-retro-compiler-research.md#results-2026-11-14)
