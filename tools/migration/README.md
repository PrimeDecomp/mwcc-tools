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

* `benchmark.py`, `benchmark_worker.py`, `capture_baseline.py`, `capture_colouring.py`, `capture_header_impact.py`,
  `capture_regdiff.py`, `capture_tu_tools.py`, `compare_backends.py`, `verify_link.py` and `verify_producer.py`
  import a separate pinned checkout of the old Python tools (`--oracle-root`, `--reference`).
* `compare_backends.py` and `verify_producer.py` also run the removed `oracle` command (`oracle compile-trace`). They
  stay for the helpers other scripts import: `difference` and `normalize`, and `full_trace`, `graph_trace` and
  `trace_line_endings` (`test_producer_trace.py` tests these).

They run only against the commit they were written for. The scripts that only drove removed commands
(`oracle compile-trace`, `oracle regdiff`, `colouring`, `inline-diff`, `header-impact`, `source-nodes`,
`source-arenas`, `source-search`) were deleted; `mwcc-tools call oracle.trace` ([docs/api.md](../../docs/api.md))
replaces the traced compile.

Keep reference checkouts and retained evidence directories unchanged; a script that captures new evidence writes to
a new output directory.
