# Replay a retained case from a bundle

`source_repro.export` exports a search or reduction candidate with its sources,
dependencies, compiler runtime, objects, contract and declared checker inputs.
Use it to replay the case locally after moving the bundle or disconnecting the
original checkout.

```sh
mwcc-tools call source_repro.export kind=search run=search-run id=1 output=short/bundle \
    run_analyzer=frozen/original-mwcc-tools.exe
mwcc-tools call source_repro.verify bundle=short/bundle
short/bundle/tools/mwcc-tools.exe call source_repro.replay bundle=short/bundle output=short/replay compilations=3 \
    seconds=600
```

For reductions, use `kind=reduction run=reduction-run id=ID`. On Linux run the bundled
`tools/mwcc-tools`. Bundle/replay parent directories must exist and final output
directories must be new. Keep Windows paths short for the legacy compiler.

Export reads retained content-addressed input/cache files; it need not reopen
the source checkout. The original analyzer and configured checker/proof/target
files must remain available and match the saved identity. `run_analyzer`
defaults to the exporting executable; supply the actual original executable
(including a frozen test binary) if different. Reduction ID `0` selects its
captured baseline. An outcome without a completed capture cannot be exported.

## Verify integrity, then replay

Export and offline `verify` do not execute bundled programs. Verification checks
`manifest.json`, the completion receipt, all referenced blobs, raw captures,
source mappings, compiler identities and original plan/checker bindings. Missing
or modified files fail. The completion receipt is written last; partial exports
are not valid bundles. To pin a separately trusted manifest identity:

```sh
mwcc-tools call source_repro.verify bundle=short/bundle expected_sha256=MANIFEST_SHA256
```

`verified_integrity` establishes content/shape integrity, not authenticity or
program safety. Replay executes the packaged compiler and configured native
checker with the user's privileges.

The manifest distinguishes the original analyzer (retained as an identity blob,
never executed by replay) from the replay analyzer (the exporting tool copied
into `tools/`). Replay requires that exact pinned executable and evaluates fresh
traces; a saved successful result is not accepted as fresh evidence.

Replay creates separate relocated workspaces for the original case, private
baseline and candidate. Three fresh compilation requests must pass:

- Input scans before and after require unchanged raw dependencies, exact closure
  and compiler runtime paths inside the materialized tree.
- Every object must match its retained hash and pass oracle/stock equivalence;
  original/private-baseline comparable sections must calibrate.
- The candidate must satisfy the saved objective, relocation/coverage gates and
  configured semantic checker. Indeterminate evidence cannot succeed.

Retained runs have no separate stock-output object hash; replay does not claim
to add that historical evidence. Captures and objects are saved under
`OUTPUT/cases/{original,baseline,candidate}`.

Each result's `document` is the JSON. Only `reproduced` establishes successful replay.
`failed_reproduction`, `predicate_not_satisfied` and `cancelled` are non-success
results saved in `OUTPUT/replay.json`; integrity/preflight failures are the call's error.
Use `seconds`, `compilations` and `cancel_file` to bound work. The default
cancel path is the output path with extension `CANCEL`. Scans and subprocesses
observe cancellation; copying/hashing checks it at operation boundaries.

## Supported inputs

Bundles use schema 1: `manifest.json`, `complete.json`, `blobs/<sha256>` and the
pinned replay executable. Supported relocation is deliberately bounded:

- **Same host platform:** recorded OS/architecture and a compatible OS runtime
  are required. Windows/Linux bundles are separate; the host OS and shared
  libraries are not packaged, and this is not a sandbox.
- **Closed source tree:** dependencies and include roots must fit inside the
  captured working directory. A private source override is relocated separately.
  Only recognized `-i`/`-I` include forms are rebased. Other path-bearing arguments,
  response files and output/preprocessing modes (including attached forms) are
  rejected. Fresh scans reject external includes and ambient fallbacks.
- **Captured runtime:** compiler/runtime bytes come from retained snapshots. On
  Linux the launcher is placed at `compiler/build/tools/wibo`; an inherited
  external `MWCC_WIBO` override is rejected. Non-reusable captured inputs fail.
- **Native checkers:** only declared checker/proof files and adjacent Windows
  native DLLs are packaged. Scripts, interpreters, shells and embedded command
  options are rejected. Undeclared checker resources may fail after relocation;
  transitive code/data/process closure is not proven. Unsupported packaging fails
  export rather than downgrading a checked transformation to a mechanism case.
- **Exact new results:** the original environment is an identity digest, not
  restored state. Replay makes new input snapshots, without old cache keys.
  Path-sensitive macros, debug data or environment changes that alter object
  bytes fail reproduction; equality is not waived.
- **New safe paths:** UTF-8 manifest paths cannot traverse or escape through
  symlinks/reparse points, collide, or use common Windows device names. Export
  stays outside the retained run; replay stays outside the bundle. Neither
  overwrites the original evidence.

Rust API: `source_guided::reproducer::{export,verify,replay}`. `export` takes
`RunKind`, run path, candidate ID, new bundle path and optional original analyzer;
`replay` takes a bundle, new output path and `search::Budget`.

See the [implementation status](../oracle-source-guided-status.md) for validated
native cases, retained evidence and untested replay scopes.
