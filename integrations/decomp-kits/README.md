# The oracle in decomp kits

`overlay/` is what a kit installs into a game checkout: the experiment scripts and oracle helpers on mwcc's
Python package (`overlay/scripts`), and the [guide](overlay/.agents/docs/oracle-native.md) to them. The scripts
are project-neutral; the Echoes kit ships them as they are.

```text
python scripts/try-variants.py -u UNIT -f FUNCTION --variants v.txt
python scripts/residual-scan.py -u UNIT
python scripts/oracle-run.py -u UNIT --locals --schedule
python scripts/oracle-regdiff.py -u UNIT -f FUNCTION --pcode
python scripts/oracle-colouring.py verify -u UNIT -f FUNCTION
python scripts/inline-calibrate.py --callee CALLEE -u UNIT --breakdown
```

## Install and configure

The scripts need mwcc-tools, its package and the compiler module, from the checkout `MWCC_ORACLE_ROOT` names
(the guide gives the build commands); without them they stop with those commands. A kit's installer should check
for them as the Echoes kit's `install.py` does (`_mwcc_tools_path.problems()`).

`install-native.py PROJECT --apply` installs a released executable from a kit's `native-manifest.json`: it
validates the host archive, hashes and source identity, installs the executable and notices, and prints the
`MWCC_TOOLS` path. It supports Windows/Linux x64 and macOS Intel/Apple Silicon; a kit must include the matching
target in its manifest.

## Pinned kits and development tools

Each kit pins the mwcc commit it was tested with (its source bundle, and the Echoes kit's `selftest.py` checks the
checkout is at or after it). Archived releases remain immutable; editing this overlay does not change them.

Development workflows are documented in the source checkout:
[provenance](../../docs/oracle/provenance.md),
[source search](../../docs/oracle/source-search.md),
[reduction](../../docs/oracle/source-reduce.md),
[retained reproducers](../../docs/oracle/source-repro.md), and
[matched-source indexing](../../docs/oracle/source-index.md).

## Maintain the integration

`tools/migration/adopt_native_kits.py` derives kit sources from archived releases; `audit_native_kits.py` checks
packaged files, source identity and installation. Supply `--macos-arm64-release` and/or `--macos-intel-release`
when assembling a kit with macOS assets.
