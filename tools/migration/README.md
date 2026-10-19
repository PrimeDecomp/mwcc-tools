# Validation scripts

Python checks that run beside `mwcc-tools`, independently of it. They are not part of the tools.

In use:

| Script | Used by |
|---|---|
| `test_*.py` (`python -m unittest discover -s tools/migration -p 'test_*.py'`) | CI |
| `capture_notices.py` | CI and `licenses/README.md`: the third-party notices of a release target |
| `audit_release.py` | CI: checks a release archive |
| `verify_frontend.py`, `verify_trace_writer.py` and the probes in `abi/` | the hooks' ABI checks (`abi/README.md`) |
| `verify_source_arenas.py` | `docs/oracle/source-arenas.md`: runs `provenance audit-source-arenas` per version |
| `adopt_native_kits.py`, `verify_archived_kits.py` | deriving kit sources from archived kit releases, and checking those releases |

The others were written against the CLI and the Python tools as they were before the methods:

* `audit_native_kits.py`, `benchmark*.py`, `capture_colouring.py`, `capture_regdiff.py`, `compare_*.py`,
  `verify_backend_probes.py`, `verify_call_arguments.py`, `verify_frontend_switches.py`, `verify_native_kit_api.py`,
  `verify_oracle_views.py`, `verify_producer.py`, `verify_source_arena_runtime.py` and `verify_source_nodes.py` call
  commands that no longer exist (`oracle compile-trace`, `oracle regdiff`, `colouring`, `inline-diff`,
  `source-search state`, `source-nodes inspect`, `source-arenas inspect`).
* `verify_link.py`, `verify_producer.py`, `capture_baseline.py` and `compare_backends.py` import a separate pinned
  checkout of the old Python tools (`--oracle-root`, `--reference`).

They run only against the commit they were written for.

Keep reference checkouts and retained evidence directories unchanged; a script that captures new evidence writes to
a new output directory.
