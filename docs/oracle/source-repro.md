# Retained source reproducer bundles

`mwcc-tools source-repro` exports one retained search or reduction candidate with
its original calibration case, private baseline, raw sources and dependencies,
retained object bytes, compiler runtime files, contract and declared checker
inputs. Export reads the retained content-addressed input/cache files; it does
not need to reopen the original source checkout. It does need the original
analysis executable and any configured checker/proof/target files to remain
available and match the saved analysis identity.

```text
mwcc-tools source-repro export search search-run 1 --output short/bundle --run-analyzer frozen/original-mwcc-tools.exe
mwcc-tools source-repro export reduction reduction-run 1 --output short/reduced-bundle --run-analyzer frozen/original-mwcc-tools.exe
mwcc-tools source-repro verify short/bundle
mwcc-tools source-repro verify short/bundle --expected-sha256 MANIFEST_SHA256
short/bundle/tools/mwcc-tools.exe source-repro replay short/bundle --output short/replay --compilations 3 --seconds 600
```

On Linux, use the bundled `tools/mwcc-tools` executable. `--run-analyzer` defaults
to the exporting executable. Supply the actual original executable when it
differs, including a frozen test executable for runs produced by integration
tests. Reduction candidate `0` selects its captured baseline; other IDs select
retained outcomes. A failed or incomplete outcome without a capture cannot be
exported. Neither export nor offline verification executes bundled programs.
Bundle and replay parent directories must already exist; the final output
directory must be new.

The native API is
`source_guided::reproducer::{export, verify, replay}`. `export` takes `RunKind`,
the retained run, candidate ID, a new bundle directory and optional original
analyzer path. `replay` takes a bundle, a new output directory and `search::Budget`.
Replay uses a cancellation file, by default the output path with extension
`CANCEL`. Three fresh compilation requests are required; input scanning and
compiler/checker subprocesses also observe the time/cancellation bounds. File
hashing and copying occur between cancellation checks.

## Identity and successful reproduction

The manifest distinguishes the **original analyzer** from the **replay
analyzer**. The first is retained as an identity receipt and blob, bound to the
original declared checker/proof inputs and target. It is never executed by
replay. The second is the exporting tool, copied into `tools/` and pinned by SHA.
Replay requires that exact executable. Current predicates are reevaluated from
fresh traces by that pinned tool; an exported original analysis result is not
treated as a new result.

Replay creates independent relocated workspaces for the original case, private
baseline and candidate. Before and after each compilation it rescans inputs,
requires unchanged raw dependency content and exact closure, and requires every
runtime input to resolve inside the materialized compiler tree. Each new object
must match its retained object hash exactly and pass the existing stock compiler
equivalence check. Retained runs do not contain a separate stock-output object
hash; the bundle does not claim to add that evidence.
Original/private-baseline comparable sections must still calibrate. The candidate
must satisfy the saved objective, relocation/coverage gates and any configured
semantic checker. Indeterminate evidence cannot produce success.

Commands emit JSON. `verify` reports `verified_integrity`, which establishes
content/shape integrity only. Replay reports `reproduced` only after all gates
pass. `failed_reproduction`, `predicate_not_satisfied` and `cancelled` are explicit
non-success application results, persisted in `OUTPUT/replay.json`. A successful
process exit alone does not establish reproduction. Integrity/preflight errors
produce a nonzero process exit. Fresh captures and objects remain under
`OUTPUT/cases/{original,baseline,candidate}` for inspection.

## Schema 1 and bounded support

The bundle contains `manifest.json`, `complete.json`, `blobs/<sha256>` and the
pinned replay executable. The completion receipt is written last and hashes the
manifest. Every referenced blob, raw capture, source mapping, compiler identity,
selected original progress entry and original plan/purpose/checker binding is
verified. Missing or modified files fail. Optional `--expected-sha256` pins a
manifest hash obtained through a trusted channel; the local receipt itself is
not an authenticity signature. Failed exports can leave partial output; require
successful offline verification before treating that directory as a bundle.

Supported scope is deliberately limited:

- Replay uses the recorded host OS and architecture plus a compatible OS runtime.
  Windows and Linux bundles are separate; this is not cross-platform compiler
  packaging or an OS sandbox.
- Raw compiler dependencies and include roots must fit within the captured
  working directory. A retained private source override may live outside it and
  is relocated separately. Only recognized `-i`/`-I` include forms are rebased.
  Other path-bearing arguments, response files and output/preprocessing modes
  are rejected, including attached forms. Fresh scans reject external includes
  or ambient dependency fallbacks. Non-reusable captured inputs are rejected.
- Compiler executable/runtime files come from retained snapshots. On Linux the
  captured launcher is packaged as `compiler/build/tools/wibo`; an inherited
  external `MWCC_WIBO` override is rejected. This does not package the host OS,
  its shared libraries, or prove arbitrary compiler process behavior is isolated.
- Checkers must be native executables. The closure consists of explicitly
  declared checker/proof files and adjacent native DLLs on Windows. Scripts,
  known interpreters/shells and embedded command options are rejected. This does
  **not** prove a complete transitive checker code/data/process closure. A checker
  that needs undeclared resources may fail after relocation. Unsupported checker
  packaging fails export; a checked transformation is never downgraded to a
  mechanism reproducer.
- The original environment is retained as an identity digest, not restored.
  Replay captures new snapshots in the current environment; old compilation cache
  keys are not reused. Path-sensitive macros, debug metadata or environment
  changes that alter object bytes fail reproduction; equality is never waived.
- Paths are UTF-8 in the manifest. Traversal, symlinks/reparse escapes, duplicate
  paths, file/directory collisions and common Windows device names are rejected.
  Windows legacy compiler path limits remain: use short bundle/output roots.
- Export and replay require new caller-owned directories outside the original
  retained run/bundle respectively. They do not overwrite originals. A replay
  executes the packaged compiler and configured native checker with the user's
  privileges; offline verification does not establish those programs are safe.

## Retained validation

The final native proof uses the `GC_2_7` standalone linear flag-initialization
mechanism fixture on Windows and Linux with sealed V5 producers. Each host
exported and verified a historical retained run with distinct original and replay
analyzer identities. A fresh private search then produced a successful candidate,
exported it, renamed its original project and retained-run directories out of
their original paths, moved the bundle, and ran the pinned tool from that bundle.
Both replays reported `reproduced` after three fresh stock-verified captures.
Independent closure receipts checked exact source/dependency content and retained
object hashes for all three roles, every runtime path/hash inside the relocated
compiler tree, and equal actual baseline/candidate ordered flags. Removing a blob
failed verification; restoring it restored integrity.

| Host | Frozen replay analyzer SHA prefix | Local proof receipts |
| --- | --- | --- |
| Windows | `012fd34b713c` | `build/rpw-v3/summary.json`, `closure-receipt.json`, `commands.json` |
| Linux | `8a08c25e11d9` | `build/rpl-v2/summary.json`, `closure-receipt.json`, `commands.json` |

Frozen integration tests passed five cases on each host; logs are retained under
`build/source-guided/portable-repro-audit/{windows,linux}-integrity-final.log`.
They cover search and reduction exports after removing original source inputs,
raw non-UTF-8 source bytes, missing/tampered blobs and analyzer copies, incomplete
receipts, traversal and attached compiler modes, original plan/purpose/selection
binding, and native-checker argument tampering without executing exported files.
The relative-include regression also verifies that identical actual flags resolve
within each private workspace.

This live proof is one `GC_2_7` mechanism-only source-search case per host. It does
not claim a live reduction replay, checked-transformation replay, full game
translation-unit relocation, cross-platform bundle replay, or complete
transitive checker/runtime closure. Full hashes, commands and exact paths are in
the local receipts. No historical run or sealed producer was modified.
