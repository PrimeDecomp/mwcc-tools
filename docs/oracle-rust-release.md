# Native mwcc tools distribution

Add this archive's `bin` directory to PATH, or invoke its `mwcc-tools` executable
directly. `mwcc-tools --help` lists commands. The native kit guide in
`docs/oracle-native.md` documents unit compilation, structured checks, register
differences, graph work and inline calibration. New integrations should use native
commands and versioned JSON rather than Python imports or human report text.

Offline trace analysis and graph replay require only the executable. Compiler
execution additionally needs a built oracle checkout, selected with
`MWCC_ORACLE_ROOT` or `--oracle-root`. Original compiler executables, the historical
linker/runtime, Ninja, objdiff and Linux's wibo are external components and are
not bundled. Use wibo 1.2.0; `MWCC_WIBO` can select it explicitly. Building Rust
observation hooks requires Rust 1.96.0 and its `i686-pc-windows-msvc` target.
The bundled `docs/producer.md` describes that source build.

The supported distributions are `x86_64-pc-windows-msvc` and
`x86_64-unknown-linux-gnu`. A Linux executable's libc requirements are those of
its build host; the target name is not a promise of compatibility with older
Linux distributions. Test on the deployment host before adopting a package.

`release.json` records executable identity, versioned JSON protocols, stock
compiler identities, a source-input catalogue and matching dependency notices.
The optional source revision is informational: the catalogue identifies supplied
source bytes, and the executable has its own hash. These are not an attestation
that an arbitrary executable was built from a clean commit. No compiler binaries,
game source, game objects or private traces are included.

The archive contains the project license, Capstone notices and the complete
locked dependency/Rust library notices for its target. Keep these when
redistributing the executable. Verify the `.zip.sha256` sidecar and then the
individual file hashes in `release.json`. A sidecar detects changed bytes; it is
not a signature or publisher authentication.

## Building a package from the repository

```text
cargo build --locked --release -p mwcc-tools
mwcc-tools package-release build/releases/mwcc-tools-0.1.0-TARGET.zip --binary PATH_TO_BUILT_EXECUTABLE --target TARGET
```

Run from the repository, or supply `--root`. Supply `--source-revision` with a full
commit ID when recording a release pin. Use the target's own locked notice
inventory; regenerate/check it after dependency or toolchain changes. The packager
rejects stale lockfile hashes, changed/missing/extra notice files, symlink inputs
and a binary with the wrong architecture/container. It sorts ZIP entries, fixes
timestamps and preserves executable permissions, so the same inputs produce
the same archive bytes. This does not claim reproducible Rust compilation.

Use `tools/migration/audit_release.py` in the source repository for independent
archive/metadata checks, extraction and offline execution outside the checkout.
Release assembly creates local artifacts; publishing them is a separate action.
