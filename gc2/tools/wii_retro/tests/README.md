# Wii_Retro native differential checks

Run these commands from the repository root on Windows. They keep generated files under
`build/wii-retro-tests/` by default. Test compilers need their runtime DLLs beside them.

## Function-template generation

Place stock Wii 1.0RC1 and its `lmgr8c.dll` in `build/compilers/Wii/1.0RC1/`.
Put `mwcc-tools`, Clang, and `lld-link` on PATH, or name them with `--tools`, `--clang`,
and `--linker`.

```sh
uv run --no-sync python -I gc2/tools/wii_retro/tests/function_instantiation.py
```

The check builds production, native-traced and C-traced compilers. It compares
callback sequences, state transitions, diagnostics and complete PowerPC objects
under ordinary compilation, file IPA, program IPA and debug information, with
C++ exceptions enabled.
An invalid instantiated body verifies cleanup after parser diagnostics.
The same fixture compiles class-template constructors, virtual members, and a
static initializer through the member-body worker.
It also checks that an early-parsed function template reaches the stored-body
worker. The early-parsed bodies include a dependent switch, local and static
declarations, local and global typedefs, and variable-length arrays of scalars
and class objects. A constructor with base and member initializers acquires a
virtual base at instantiation. A packed member function reads those fields
after substitution.
Two enclosing template instances use the same nested argument with different
enclosing types. An additional packed body exercises nested aggregate
initializers and cleanup for a dependent class object and arrays.
An early-parsed exception fixture includes a dependent catch type and a
catch-all handler.

Synthetic controls exercise extern suppression, definition fallback, weak/root
flags, packed-definition results, missing definitions, lexer entry tokens,
debug state, instantiation points and namespace cleanup. Callback mutations
check when the generator reloads object pointers and source information.
Each control also checks its expected result independently of trace equality.

## Local-register assignment

This links the production C passes with a fixture and compares callback order, object
identity, assigned registers, and the optimization flag with stock RC1 across 8,192 cases.
The two-version run doubles that count. Put Clang, `lld-link`, and `llvm-dlltool` on PATH.

```sh
uv run --no-sync python -I gc2/tools/wii_retro/tests/local_assignment.py
uv run --no-sync python -I gc2/tools/wii_retro/tests/local_assignment.py --gc build/compilers/GC/3.0a5.2/mwcceppc.exe
```

The x86 byte comparison uses a CodeWarrior Windows 9.2 or 9.4 `mwcc.exe`. Pass the
directory containing `LMGR8C.dll` and the `license.dat` supplied with the tools when
they are outside the current host setup. The test expects an exact initial-assignment body
and reports the reassignment byte count. Use `--gc` to add the earlier compiler as a control.

```sh
uv run --no-sync python -I gc2/tools/wii_retro/tests/local_assignment_host.py --compiler HOST/mwcc.exe --runtime-dir HOST_DLLS --license-file HOST_LICENSE/license.dat
```

## Register classification and allocation

This links the production implementation and compares it with RC1 for variable-info lookup,
type classification, and single and pair allocation. It also checks the physical register
scans and reservation state. It uses the same LLVM tools as the local-register fixture.

```sh
uv run --no-sync python -I gc2/tools/wii_retro/tests/register_info.py
```

## Precompiled headers

This builds the guarded template initialization fixture with both PCH producers and both
readers and compares the four complete PowerPC objects. Supply two Wii compiler executables.

```sh
uv run --no-sync python -I gc2/tools/wii_retro/tests/pch.py --baseline BASE/mwcceppc.exe --candidate build/Wii_Retro/mwcceppc.exe
```

## Echoes object comparison

Configure Echoes for one of `R32J01`, `R3ME01`, or `R3MP01` and prepare its generated includes
and tools. Pass the checkout path and the two compiler executables. The runner
reads the configured compiler commands from Ninja and compiles into fresh output directories.
It compares each complete object under the configured flags and with file IPA enabled for
C++ entries that did not already specify an IPA mode. The JSON report records the compiler
hashes and lists changed objects. Use `--expect-identical` to gate a source-only recovery.

```sh
uv run --no-sync python -I gc2/tools/wii_retro/tests/echoes_objects.py --echoes ECHOES --region R3ME01 --baseline BASE/mwcceppc.exe --candidate build/Wii_Retro/mwcceppc.exe --expect-identical
```

Repeat for each configured region. Use `--graph` to select a saved Ninja graph and
`--compiler-version` if the graph uses another path in place of `Wii/1.0RC1_retro`.
See the [Echoes instructions](../../../../docs/wii-retro-echoes.md) for installation
and the wider acceptance checks.

## Diagnostic DOL relink

Keep the link response file when building Echoes with `ninja -d keeprsp`. Take the candidate
`CSpatialPrimitive.o` from the object comparison's `file-ipa/candidate` directory. The diagnostic
relink substitutes that object in a copy of Echoes' response file and checks the resulting DOL
against the region's `build.sha1`.

```sh
uv run --no-sync python -I gc2/tools/wii_retro/tests/echoes_dol.py --echoes ECHOES --region R3ME01 --spatial FILE_IPA_CANDIDATE/src/Collision/CSpatialPrimitive.o --linker TOOLS/mwldeppc.exe
```
