# Rust migration status

Completed locally on 2026-10-16 at
`9e67176bcb7310559bb74455026f6643fd505e40`.

The host tools, compiler construction commands, all 31 original observation hooks
and oracle kit callers now use Rust. The supported integration boundary is
`mwcc-tools` and versioned JSON. Python imports and human report text are not APIs.

The migrated Python implementations and their colouring test have been removed
from the working tree. Historical comparisons use a separate pinned reference
checkout; [independent validators](../tools/migration/README.md) remain available.

## What ships

- Native Windows and Linux executables for tracing, reports, register analysis,
  graph replay/search, translation-unit comparisons and inline calibration.
- Native build, download, split, link, compare and binary-investigation commands.
- Rust observation hooks selected by `configure --oracle`; `--producer c` selects
  the retained rollback.
- Prime and Echoes kits with native callers, documentation, executable packages
  and a complete source bundle.

The original compiler, historical linker/runtime, Ninja, objdiff and Linux's wibo
remain external dependencies. Rust hooks observe the original compiler; they do
not replace its optimizer or allocator.

## Validation

Windows and Linux passed 81 offline tests, formatting, all-target Clippy,
36 producer fixture compilations and 26 real configuration comparisons per host.
Packaged executables passed offline analysis and verified compilation outside the
checkout. Installed Prime and Echoes kits compiled their real test units with
stock equality; cross-host objects agreed.

See the [acceptance audit](oracle-rust-completion.md) for scope and the
[release manifest](oracle-rust-release-manifest.json) for exact artifact identities.
Detailed intermediate milestones are in the [migration history](oracle-rust-history.md).

## Start here

| Task | Documentation |
|---|---|
| Build and use the oracle | [Oracle guide](oracle.md) |
| Use it from a decomp kit | [Native kit workflow](../integrations/decomp-kits/overlay/.agents/docs/oracle-native.md) |
| Package or install tools | [Distribution guide](oracle-rust-release.md) |
| Review performance | [Migration benchmarks](oracle-rust-benchmarks.md) |
| Use the newer source-guided tools | [Development status](oracle-source-guided-status.md) |

The source-guided provenance, search, reducer and index extensions started after
this release. Their implementation and validation are tracked separately.
