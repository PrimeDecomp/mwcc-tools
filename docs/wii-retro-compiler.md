# Wii_Retro

Wii_Retro reproduces code generation observed in the Wii releases of the Prime games. The native build
relinks Wii 1.0RC1 with compatibility patches inferred from the shipped games. GC 3.0a5.2 provides
an earlier reference for the decompilation.

## Build Wii_Retro

Build `mwcc-tools` from this checkout and put it on your PATH along with uv, Clang, LLD
(`lld-link`), and Ninja. [mwcc-tools build instructions](../BUILDING.md)

Place Wii 1.0RC1's `mwcceppc.exe` and `lmgr8c.dll` in `build/compilers/Wii/1.0RC1/` and run
these commands from the repository root.

```sh
mwcc-tools configure --version Wii_Retro
ninja Wii_Retro
build/Wii_Retro/mwcceppc.exe -version
```

The executable reports `Wii_Retro` in its banner. Keep `lmgr8c.dll` beside it when copying it into a
project's compiler directory. To build directly into another directory, pass `--out-dir PATH` to
`uv run --no-sync python -I gc2/tools/wii_retro_link.py`.

The same Wii_Retro build serves Echoes Wii and retail Corruption. Corruption's Lua build
uses CodeWarrior's existing 7400 scheduling setting and keeps the 750 schedule for
`LuaPlus.cpp`.
[Corruption compiler research](wii-retro-compiler-research.md#corruption-lua-lzo-and-compiler-scheduling)

## Current patches

- **Struct-copy alignment.** Uses GC 3.x's operand alignment to choose copy widths while preserving
  RC1's stack layout.
- **Destroy-loop pretest.** Folds a duplicate entry guard in fixed-stride loops that call a
  function. The loop still tests for an empty range before entering the body.
- **Template vtable emission.** Emits referenced virtuals alongside out-of-line template helpers.
  Direct references can leave the vtable and its virtuals for the end of the file.
- **Deferred function exports.** Orders weak bodies using vtable slots, definition and
  reference order, and constructor cleanup order. Keeps class-instance members together and
  preserves serialized reference order where needed under file IPA.
- **File-IPA register order.** Adjusts virtual register assignment for inlined locals and
  arguments, propagated references, and optimizer-generated iterators when `-ipa file` is active.
- **Aggregate-copy scheduling.** Preserves memory order within mixed scalar and array member
  copies through both scheduler passes to limit register pressure.
- **7400 paired-single timing.** Models paired-single loads and stores with floating-point
  memory timing and preserves their scheduling barrier.

The register-order and aggregate-copy controls match all three Wii builds. The Echoes integration
sweep preserves every previous exact match, and the three rebuilt DOLs match their shipped hashes.
[Research and validation](wii-retro-compiler-research.md#native-relink-and-validation)

The relink includes recovered C implementations of register assignment and reservation, assignment and
conditional lowering, scheduler entry and dependency construction, PCode formatting and side effects,
alias analysis, deferred generation, call-graph management, IR reference packing, and template visitation.
The Windows validation runners cover function-template generation, register allocation,
precompiled headers, and Echoes object and DOL comparisons. [Test instructions](../gc2/tools/wii_retro/tests/README.md)

## Plan

1. Decompile Wii 1.0RC1 using its CodeView symbols and compare it with GC 3.0a5.2. Start with copy
   lowering, stack layout, loop handling, and scheduling. [Compiler layouts and diff map](../gc2/research/README.md)
2. Move the compatibility patches into the shared compiler sources as the relevant units are
   recovered. Build from those sources and add WebAssembly and oracle support.
3. Validate Echoes on all three Wii builds with the shared RSTL sources. Check exact functions,
   linked DOLs, and all 447 image hashes. [Echoes integration notes](wii-retro-echoes.md)

[Compiler research notes and original patch probes](wii-retro-compiler-research.md)
