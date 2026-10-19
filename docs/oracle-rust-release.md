# mwcc-tools native package

This archive holds one `mwcc-tools` executable for one host. Packages are published on GitHub Releases as
`mwcc-tools-VERSION-TARGET.zip` with a `.zip.sha256` sidecar:

```text
gh release download v0.1.0 --pattern 'mwcc-tools-0.1.0-x86_64-pc-windows-msvc.zip*' --dir downloads
```

Outside a checkout, add `--repo OWNER/REPOSITORY`. Pull requests and branch pushes also produce packages as Actions
artifacts; these are test builds.

| Host | Target |
|---|---|
| Windows x64 | `x86_64-pc-windows-msvc` |
| Linux x64 | `x86_64-unknown-linux-gnu` |
| macOS Apple Silicon | `aarch64-apple-darwin` |
| macOS Intel | `x86_64-apple-darwin` |

The build host sets the system-library requirements; the target name does not promise older Linux or macOS
releases.

## Contents

- `bin/mwcc-tools` (`.exe` on Windows);
- `README.md` (this file), `docs/oracle-native.md` (the kit workflow) and `docs/producer.md` (building the
  observation hooks);
- `LICENSE`, the Capstone notices and the target's locked dependency and Rust library notices. Keep them when
  redistributing the executable;
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

The `Rust tools` workflow (`.github/workflows/rust.yml`) builds, tests and audits the four packages. Only a pushed
`vVERSION` tag publishes, and `VERSION` must equal `workspace.package.version` in `Cargo.toml`. Any failed matrix
job stops publication; only the tag job has `contents: write`.

1. Update the workspace version and `Cargo.lock` together. If the lockfile or toolchain changed, regenerate and
   check the dependency notices. Commit.
2. Wait for the four host checks to pass.
3. Tag that commit and push the tag:

   ```text
   git tag -a v0.1.0 -m "mwcc-tools 0.1.0"
   git push origin v0.1.0
   ```

The tag run rebuilds the packages, checks their hashes, identities and audits, checks that the tag still names the
built commit, then creates a draft release, uploads the eight files and publishes it. A version with a prerelease
suffix is marked as a prerelease. Releases are never overwritten: if publication fails after the draft exists,
inspect it, delete it only if it was never published, and rerun. Never move a published tag; make a new version.

### Build a local package

```text
cargo build --locked --release -p mwcc-tools
python tools/migration/capture_notices.py --target TARGET --output build/notices/TARGET
python tools/migration/capture_notices.py --target TARGET --output build/notices/TARGET --check
mwcc-tools package-release build/releases/mwcc-tools-0.1.0-TARGET.zip --binary PATH_TO_BUILT_EXECUTABLE --target TARGET --notices build/notices/TARGET
```

Run from the repository or pass `--root`; pass `--source-revision COMMIT` to record a pin. The packager rejects a
stale lockfile hash, changed, missing or extra notice files, symlink inputs and a binary for the wrong
architecture. Entries are sorted with fixed timestamps, so the same inputs give the same archive bytes (the Rust
build itself is not claimed reproducible). `tools/migration/audit_release.py` checks an archive independently:
contents, notices, extraction and offline execution outside the checkout.
