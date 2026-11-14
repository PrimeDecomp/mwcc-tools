# Layouts of compilers that are not versions here

These are layouts of executables that `include/version.h` does not list, so neither build reads them. They are
for the compiler of the Prime games' Wii builds, which sits between GC 3.0a5.2 and Wii 1.0RC1. The plan is in
[../../docs/wii-retro-compiler.md](../../docs/wii-retro-compiler.md).

- `Wii_1_0RC1/`: Wii 1.0RC1 (`build/compilers/Wii/1.0RC1/mwcceppc.exe`), from its CodeView (`tools/wii_layout.py`).
  - `functions.json`: each function's name, address, size, CodeView module and unit.
  - `symbols.txt` and `splits.txt`: in decomp-toolkit's formats, as under `config/`.
  - `units.json`: the module-to-unit table, and the pieces of a unit's section outside its `splits.txt` range.

  A module that has a source here is named by that source (`src/optimizer/IroLoop.c`). The others are named
  `wii/<module>`. CodeView names no static function, so a static is `fn_<address>`.
  - `names_from_2_7.json`: names for 49 of RC1's statics, matched from 2.7 (`tools/wii_chain_names.py`).
- `GC_3_0a5_2/`: GC 3.0a5.2, which has no symbols. Its names are carried over from RC1 by relocation-masked
  matching (`tools/wii_remap.py`): 5557 of RC1's 6073 functions are found. Each function's `how` says how it was
  found: `exact`, `propagated` (call graph), `order` (by position between found functions: a guess) or `fuzzy`
  (byte similarity: a guess). A function with no RC1 counterpart is `new`, or `new+between` when it lies between two
  functions of one module and takes that module. An RC1 static keeps its RC1 name, so `fn_005bb650` in 3.0a5.2 is
  at another address.
- `wii-rc1-differences.md`: the functions that differ between the two, per module (`tools/wii_diff_report.py`).

Regenerate with:

```sh
python tools/wii_layout.py Wii_1_0RC1 build/compilers/Wii/1.0RC1/mwcceppc.exe --write
```

`tools/wii_patch_struct_copy.py` writes a copy of RC1 that copies structs as the Prime games' Wii compiler does,
without RC1's legacy stack layout. Its effect on Echoes is in the plan.
