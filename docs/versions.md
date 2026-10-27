# Another compiler version: `unit.compile` and `unit.score`'s `compiler`

```sh
mwcc-tools call unit.score project=PROJECT unit=UNIT compiler=GC/2.0p1
mwcc-tools call unit.compile project=PROJECT unit=UNIT compiler=GC/1.3.2 object_path=other.o
```

`compiler` compiles the unit with another of the project's compilers, by its directory under `build/compilers`
(`GC/2.0p1`, `GC/2.5`, `GC/1.3.2`...) or an executable's path (relative to the project). The unit's flags, source
and working directory are its build's. `unit.score` scores the object against the target as it scores the unit's own:
whether the target was built by another version (literals that are not const in 2.0p1's `TOC.c`, an older
inliner, scheduler or allocator) can be checked function by function.

Only GC 1.3.2, 2.6 and 2.7 have modules: the others run as a process (natively on Windows, through wibo elsewhere),
slower, and with only the unit's source given in `files` (an overlaid header is refused). The compile cache does not
keep them. A traced compile of a GC 1.3 unit runs its version's native oracle
([experiments.md](oracle/experiments.md#without-a-module)); `compiler` does not apply to it.

## What may not compile

The project's flags and headers are written for its own compiler. Differences seen so far:

| Compiler | What | Way round |
|---|---|---|
| GC 2.0, 2.0p1 | Reported by the Echoes session: a C++98 static assert of a template's constant (`CHECK_SIZEOF`, `extern int x[check_sizeof<T, N>::value]`) failed with "illegal constant expression" in one unit (Echoes' `main/Kyoto/DolphinCDvdFile`, which uses it, compiles). | Give the unit's source in `files` with the macro defined empty before its includes (a process compile takes no other file). |

Add a row when a version rejects something the project's own compiler takes.
