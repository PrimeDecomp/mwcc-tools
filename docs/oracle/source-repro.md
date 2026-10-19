# Replay a retained case from a bundle

`source_repro.export` packages a search or reduction candidate with its sources, dependencies, compiler runtime,
objects, contract and declared checker inputs, to replay it after moving the bundle or without the original
checkout.

```sh
mwcc-tools call source_repro.export kind=search run=search-run id=1 output=short/bundle \
    run_analyzer=frozen/original-mwcc-tools.exe
mwcc-tools call source_repro.verify bundle=short/bundle
mwcc-tools call source_repro.verify bundle=short/bundle expected_sha256=MANIFEST_SHA256
short/bundle/tools/mwcc-tools.exe call source_repro.replay bundle=short/bundle output=short/replay compilations=3 \
    seconds=600
```

- For reductions, `kind=reduction`; ID `0` is the captured baseline. An outcome without a completed capture cannot
  be exported.
- On Linux run the bundled `tools/mwcc-tools`. Parent directories must exist and outputs must be new. Keep Windows
  paths short for the compiler.
- Export reads the retained content-addressed files. The original analyzer and checker/proof/target files must
  still match their saved identity; `run_analyzer` defaults to the exporting executable.

Rust: `source_guided::reproducer::{export, verify, replay}`.

## Verify, then replay

Export and `verify` run no bundled program. `verify` checks `manifest.json`, the completion receipt
(`complete.json`, written last), every blob, raw captures, source mappings, compiler identities and the plan/checker
bindings. `verified_integrity` is content integrity, not authenticity or safety: replay runs the packaged compiler
and checker with the user's privileges.

The original analyzer is kept as an identity only; replay uses the exporting tool copied into `tools/` and
evaluates fresh traces. It builds the original case, private baseline and candidate in separate relocated
workspaces; all three must pass:

- input scans before and after, with the dependency closure and runtime inside the materialized tree;
- each object matching its retained hash and stock equivalence, with original and baseline calibrating;
- the candidate satisfying the objective, relocation/coverage gates and semantic checker.

Results are saved in `OUTPUT/replay.json` and `OUTPUT/cases/{original,baseline,candidate}`. Only `reproduced` is
success; `failed_reproduction`, `predicate_not_satisfied` and `cancelled` are results, integrity and preflight
failures are errors. The default cancel file is the output path with extension `CANCEL`.

## Supported bundles

Schema 1: `manifest.json`, `complete.json`, `blobs/<sha256>` and the replay executable. Relocation is bounded:

- **Same host platform.** Windows and Linux bundles are separate; the OS and shared libraries are not packaged and
  this is not a sandbox.
- **Closed source tree.** Dependencies and include roots must be inside the captured working directory. Only
  `-i`/`-I` include forms are rebased; other path arguments, response files and output/preprocessing modes are
  rejected, as are external includes.
- **Captured runtime.** On Linux the launcher goes to `compiler/build/tools/wibo`; an inherited `MWCC_WIBO` is
  rejected.
- **Native checkers only.** Declared checker/proof files and adjacent Windows DLLs are packaged; scripts,
  interpreters and shells are rejected at export.
- **Exact results.** The environment is an identity digest, not restored state. Path-sensitive macros or debug
  data that change object bytes fail reproduction.
- **Safe paths.** Manifest paths cannot traverse, escape through links, collide or use Windows device names.
  Neither export nor replay overwrites the original evidence.
