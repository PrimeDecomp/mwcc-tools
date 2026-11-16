# Wii_Retro

Wii_Retro models the compiler Retro used for the Wii releases of the Prime games. The native build
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

## Current patches

- **Struct-copy alignment.** Uses GC 3.x's operand alignment to choose copy widths while preserving
  RC1's stack layout.
- **Destroy-loop pretest.** Removes the entry guard when the loop body calls a function and the
  guard and loop test compare the same registers.
- **Template vtable emission.** Emits class-template instances' vtables and referenced virtual
  functions after the relevant top-level declaration.
- **File-IPA register order.** Adjusts virtual register assignment for inlined locals and
  arguments, propagated references, and optimizer-generated iterators when `-ipa file` is active.

All 30 audited register-order cases match R32J01, R3ME01, and R3MP01 under file IPA. The full R3ME01
file-IPA sweep gained 43 exact functions with zero exact losses. All 898 R3ME01 objects built with
the configured flags match the earlier three-patch compiler byte for byte.
[Research and validation](wii-retro-compiler-research.md#file-ipa-and-inlined-local-register-order)

## Plan

1. Decompile Wii 1.0RC1 using its CodeView symbols and compare it with GC 3.0a5.2. Start with copy
   lowering, stack layout, loop handling, and scheduling. [Compiler layouts and diff map](../gc2/research/README.md)
2. Move the compatibility patches into the shared compiler sources as the relevant units are
   recovered. Build from those sources and add WebAssembly and oracle support.
3. Validate Echoes on all three Wii builds with the shared RSTL sources. Check exact functions,
   linked DOLs, and all 447 image hashes. [Echoes integration notes](wii-retro-echoes.md)

[Compiler research notes and original patch probes](wii-retro-compiler-research.md)
