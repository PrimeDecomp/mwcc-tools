# Wii_Retro native differential checks

Run from the repository root on Windows with stock Wii 1.0RC1 and its
`lmgr8c.dll` in `build/compilers/Wii/1.0RC1/`. Put `mwcc-tools`, Clang and
`lld-link` on PATH, or name them with `--tools`, `--clang` and `--linker`.

```sh
uv run --no-sync python -I gc2/tools/wii_retro/tests/function_instantiation.py
```

The check builds production, native-traced and C-traced compilers. It compares
callback sequences, state transitions, diagnostics and complete PowerPC objects
under ordinary compilation, file IPA, program IPA and debug information.
An invalid instantiated body verifies cleanup after parser diagnostics.

Synthetic controls exercise extern suppression, definition fallback, weak/root
flags, packed-definition results, missing definitions, lexer entry tokens,
debug state, instantiation points and namespace cleanup. Callback mutations
check when the generator reloads object pointers and source information.
Each control also checks its expected result independently of trace equality.

Generated compilers, objects and logs stay in
`build/wii-retro-tests/function-instantiation/`. Use `--out-dir` to change it.
The checks need no Echoes checkout and do not change the installed compiler.
