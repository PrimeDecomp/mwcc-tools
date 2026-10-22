# mwcc-tools native package

This archive holds one `mwcc-tools` executable for one host. Packages are published on GitHub Releases as
`mwcc-tools-VERSION-TARGET.zip` with a `.zip.sha256` sidecar:

```text
gh release download v0.2.0 --pattern 'mwcc-tools-0.2.0-x86_64-pc-windows-msvc.zip*' --dir downloads
```

Outside a checkout, add `--repo OWNER/REPOSITORY`. Pull requests and branch pushes also produce packages as Actions
artifacts; these are test builds.

| Host | Target |
|---|---|
| Windows x64 | `x86_64-pc-windows-msvc` |
| Linux x64 | `x86_64-unknown-linux-gnu` |
| macOS Apple Silicon | `aarch64-apple-darwin` |

GC 2.7's WebAssembly compiler is published beside them as `mwcc-wasm-GC_2_7-VERSION.zip` (with a `.zip.sha256`):
`build/GC_2_7/wasm/mwcc.wasm` and its `.version`, which `mwcc-tools` runs in process, and `mwcceppc.wasm`,
`oracle.wasm` and `persist.wasm`, which mwdec loads. Unpack it into the checkout `MWCC_ORACLE_ROOT` names.

The build host sets the system-library requirements; the target name does not promise older Linux or macOS
releases.

## Contents

- `bin/mwcc-tools` (`.exe` on Windows);
- `README.md` (this file), `docs/oracle-native.md` (the kit workflow) and `docs/producer.md` (building the
  observation hooks);
- `LICENSE`, the Capstone notices and the target's locked dependency and Rust library notices (standard licence
  texts, labelled, for crates that ship none). Keep them when redistributing the executable;
- `release.json`: the executable's hash, protocol versions, stock compiler identities, a catalogue of the source
  inputs and every file's hash.

No compiler binaries, game source, game objects or traces are included.

## Verify

Check the `.zip.sha256` sidecar, then the file hashes in `release.json`. The sidecar detects changed bytes; it is
not a signature. The optional source revision in `release.json` is informational, not an attestation that the
executable was built from a clean commit.

## Use

Add `bin` to `PATH`, or run the executable directly. `mwcc-tools --help` lists the commands; `mwcc-tools methods`
lists the methods, and `mwcc-tools call METHOD` or `mwcc-tools serve` runs them (see `docs/api.md` in the source
repository).

- Offline trace analysis and graph replay need only the executable.
- Compiling needs a built mwcc checkout, selected with `MWCC_ORACLE_ROOT` or `--oracle-root`, and the original
  compiler executables. The historical linker and runtime, Ninja, objdiff and wibo are not bundled.
- On Linux and macOS, compiling the x86 executables needs wibo 1.2.0 (`MWCC_WIBO` selects one). macOS uses the
  pinned `wibo-macos` asset, which is Intel-only, so Apple Silicon also needs Rosetta 2. The macOS compiler setup
  is not validated.
- Building the observation hooks needs Rust 1.96.0 with the `i686-pc-windows-msvc` target (`docs/producer.md`).

## Maintainers

### Publish a version

The `Build` workflow (`.github/workflows/build.yml`) runs fmt, clippy and the tests on Linux, and builds the three
packages and the GC 2.7 WebAssembly bundle, on every push and pull request (the builds as artifacts). A pushed `vVERSION` tag also
publishes them; `VERSION` must equal `workspace.package.version` in `Cargo.toml`. Any failed job stops publication;
only the publish job has `contents: write`.

1. Update the workspace version and `Cargo.lock` together, and commit.
2. Wait for the workflow to pass on that commit.
3. Tag that commit and push the tag:

   ```text
   git tag -a v0.2.0 -m "mwcc-tools 0.2.0"
   git push origin v0.2.0
   ```

The tag run creates the release with the four archives and their checksums. A version with a prerelease suffix is
marked as a prerelease. Never move a published tag; make a new version.

### Build a local package

```text
cargo build --locked --release -p mwcc-tools
cargo fetch --locked --target TARGET
python tools/migration/capture_notices.py --target TARGET --output build/notices/TARGET
mwcc-tools package-release build/releases/mwcc-tools-0.2.0-TARGET.zip --binary PATH_TO_BUILT_EXECUTABLE --target TARGET
```

Run from the repository or pass `--root`; `--notices DIR` overrides `build/notices/TARGET`, and
`--source-revision COMMIT` records a pin. The packager rejects a stale lockfile hash, changed, missing or extra
notice files, symlink inputs and a binary for the wrong architecture. Entries are sorted with fixed timestamps, so
the same inputs give the same archive bytes (the Rust build itself is not claimed reproducible).
`tools/migration/audit_release.py` checks an archive independently: contents, notices, extraction and offline
execution outside the checkout.
