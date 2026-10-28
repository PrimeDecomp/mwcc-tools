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

Only GC 1.2.5n, 1.3.2, 2.6 and 2.7 have modules: the others run as a process (natively on Windows, through wibo
elsewhere), slower, and with only the unit's source given in `files` (an overlaid header is refused). The compile
cache does not keep them. A traced compile of a GC 1.3 or 1.2.5n unit runs its version's native oracle (1.2.5n's
module has no trace hooks; [experiments.md](oracle/experiments.md#without-a-module)); `compiler` does not apply to
it.

## What may not compile

The project's flags and headers are written for its own compiler. Differences seen so far:

| Compiler | What | Way round |
|---|---|---|
| GC 2.0, 2.0p1 | Reported by the Echoes session: a C++98 static assert of a template's constant (`CHECK_SIZEOF`, `extern int x[check_sizeof<T, N>::value]`) failed with "illegal constant expression" in one unit (Echoes' `main/Kyoto/DolphinCDvdFile`, which uses it, compiles). | Give the unit's source in `files` with the macro defined empty before its includes (a process compile takes no other file). |

Add a row when a version rejects something the project's own compiler takes.

## GC 1.3.2's oracle

GC 1.3.2 (Metroid Prime) has a module like 2.7's (downloaded on first use, or unpacked in a checkout from
`mwcc-wasm-GC_1_3_2-*.zip`, [BUILDING.md](../BUILDING.md)): a unit built with the original 1.3.2 compiler (its size and SHA-1 are checked against
`config/GC_1_3_2/config.json`) traces in process:

```sh
mwcc-tools call oracle.trace project=../prime unit=main/Foo function=Draw views.text=true
mwcc-tools call oracle.regdiff project=../prime unit=main/Foo function=Draw text=true full=true
```

For a bare source, pass `version=GC_1_3_2` (the default).

The register counters' provenance (`pv.*`: value births, their objects, scalarization, call arguments, graphs) is
in a trace taken with the backend stream; `provenance.inspect` reports it:

```sh
mwcc-tools call oracle.trace project=../prime unit=main/Foo function=Draw 'streams={"backend":true}' trace_path=draw.trace
mwcc-tools call provenance.inspect trace_path=draw.trace text=true
```

A birth's `site` is where the allocator numbered the register, not where the value's temporary was made. Which
function made a temporary (`ce.object caller=`, the cse view's "made by") is the creator's call in the original, in
the WebAssembly oracle as in the x86 one (1.3.2's, 2.6's and 2.7's): each of gc2's calls of `create_temp_object`
names its site, which `crates/mwcc-oracle-hooks/src/temp_sites.rs` gives the original's return address of. A call
without one is "an unknown function". `mwcc-tools run build/compilers/GC/1.3.2/mwldeppc.exe ...`
(or a build's `--wrapper`) links with the 1.3.2 linker module. Differences between 1.3.2 and 2.7 are in
[gc27-differences.md](gc27-differences.md).
