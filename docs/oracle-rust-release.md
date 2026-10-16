# Native mwcc tools distribution

Download a versioned package and its `.zip.sha256` sidecar from GitHub Releases.
From a repository checkout, for example:

```text
gh release download v0.1.0 --pattern 'mwcc-tools-0.1.0-x86_64-pc-windows-msvc.zip*' --dir downloads
```

Use `x86_64-unknown-linux-gnu` for Linux, and substitute the desired release
version. Outside a checkout, add `--repo OWNER/REPOSITORY`. Pull requests and
branch pushes also provide versioned packages as Actions artifacts; these are
test builds, not published releases.

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

## Publish a version

The `Rust tools` workflow tests and audits both host packages before publishing.
Only a pushed `vVERSION` tag can publish, and `VERSION` must exactly match
`workspace.package.version` in `Cargo.toml`. A failed or cancelled matrix job
prevents publication. Build jobs have read-only repository permissions; only the
tag-only publication job has `contents: write`.

CI installs the pinned toolchain's documentation component to collect its actual
Rust library notices. `Cargo.lock` uses LF on both hosts so notice hashes agree
with fresh checkouts.

1. Update the workspace version and lockfile together. Check dependency notices
   if the lockfile or Rust toolchain changed, then commit the release inputs.
2. Let the branch's Windows and Linux Actions checks pass. Review their audited
   packages before tagging the same commit.
3. Create and push an annotated tag when ready to publish:

   ```text
   git tag -a v0.1.0 -m "mwcc-tools 0.1.0"
   git push origin v0.1.0
   ```

The tag run builds fresh packages named
`mwcc-tools-VERSION-TARGET.zip` and matching `.zip.sha256` files. It downloads
both versioned Actions artifacts, checks their hashes, package identities and
native audit results, and verifies that the remote tag still names the built
commit. The publication job uses GitHub CLI to create a draft, upload all four
files, then publish. Versions containing a prerelease suffix are marked as
prereleases and are not marked latest.

Existing releases are not overwritten. If publication fails after draft creation,
inspect the draft and failed run; a rerun will not replace it automatically.
Delete an incomplete draft only after confirming it was never published, then
rerun the failed job. Do not move a published version tag or replace its assets;
make a new version instead. Repository release immutability and tag protection
can enforce that policy.

These steps use [`gh release create --verify-tag`](https://cli.github.com/manual/gh_release_create),
[`gh release upload`](https://cli.github.com/manual/gh_release_upload) and
[`gh release edit`](https://cli.github.com/manual/gh_release_edit).

## Build a local package

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
Local package assembly does not publish a release. CI's native audit checks
archive contents, notices and offline execution on each host; it does not run
private game builds or establish complete producer/runtime validation.
