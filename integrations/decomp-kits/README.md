# Native oracle integration

Kit automation should call `mwcc-tools` and read its structured results. The
[native oracle guide](overlay/.agents/docs/oracle-native.md) covers installation
and common tasks:

```text
mwcc-tools oracle run-unit --project PROJECT --unit UNIT -- --locals
mwcc-tools oracle regdiff --project PROJECT --unit UNIT --function FUNCTION --json
mwcc-tools colouring unit --project PROJECT --unit UNIT search --function FUNCTION --seed 2
mwcc-tools inline-calibrate --project PROJECT --callee CALLEE --unit UNIT --breakdown
```

## Install and configure

Run the kit's `install-native.py PROJECT --apply`. It validates the host archive,
hashes and source identity, installs the executable and notices, and prints the
`MWCC_TOOLS` path. Add that executable's directory to PATH for direct commands.
Set `MWCC_ORACLE_ROOT` to the checkout containing the built oracle compilers.
The kit's `EXTERNALS.md` describes compiler setup.

The installer and native-tool discovery support Windows/Linux x64 and macOS
Intel/Apple Silicon. A kit must include the matching target in its manifest;
older archives may contain only Windows/Linux packages. A standalone macOS
release on PATH or selected by `MWCC_TOOLS` also works.

The optional old-name Python launchers dispatch to `MWCC_TOOLS`, the installed
native copy, an executable under the oracle root, or PATH. A missing explicit
executable or failed native command is an error; there is no Python fallback.
Legacy Python imports and report wording are not API requirements.

## Read checks explicitly

`run-unit` checks object equality with the stock compiler by default. For
automation, use `oracle compile-trace` and require
`compilation.check_status.status == "verified"`. `mismatch`, `skipped`,
`original_unavailable` and `original_failed` do not establish equality.
Regdiff's saved project-object comparison is a separate result. Candidates and
header overrides compile privately with the unit's flags and include order.

## Pinned kits and development tools

Each kit's `native-manifest.json` identifies its shipped executable and source
pin; the complete Git source bundle makes that pin available locally. Archived
releases remain immutable. Commands documented as development features require
current tools and a matching freshly built Rust producer; editing this overlay
does not add them to an existing archive.

Development workflows are documented in the source checkout:
[provenance](../../docs/oracle/provenance.md),
[source search](../../docs/oracle/source-search.md),
[reduction](../../docs/oracle/source-reduce.md),
[retained reproducers](../../docs/oracle/source-repro.md), and
[matched-source indexing](../../docs/oracle/source-index.md).

## Maintain the integration

`tools/migration/adopt_native_kits.py` derives new kit sources from archived
releases. `verify_native_kit_api.py` checks direct native behavior;
`audit_native_kits.py` checks packaged files, source identity and installation.
The archived-kit verifiers retain legacy regression evidence without making
legacy APIs the adoption contract. See the
[migration status](../../docs/oracle-rust-status.md) for release pins
and validation results.

Supply `--macos-arm64-release` and/or `--macos-intel-release` when assembling a
new kit with macOS assets. The kit audit selects the native host target and checks
its installation; existing archives remain unchanged.
