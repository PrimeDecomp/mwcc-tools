# Independent validation tools

This directory contains migration validators, reference-fixture capture scripts,
benchmarks and retained-evidence audits. They remain Python because they provide
independent checks around the native tools; they are not the production oracle
or build implementation. Production commands are provided by `mwcc-tools`.

- Native validators such as `verify_native_build.py`, `audit_release.py`,
  `verify_source_arena_runtime.py` and `verify_source_nodes.py` inspect native
  outputs or exercise guarded compiler observations.
- Historical parity scripts such as `capture_baseline.py`, `compare_backends.py`,
  `verify_split.py`, `verify_link.py` and the Python benchmark workers compare
  against a **separate pinned Python reference checkout**. Select it explicitly
  with the script's `--oracle-root`, `--reference` or `--reference-script` option.
- Shared helpers and recorded fixtures support both kinds of check. Use each
  script's `--help` for its required compiler, fixture and output inputs.

For example:

```text
python tools/migration/verify_split.py --oracle-root /path/to/pinned-python-reference --binary /path/to/mwcc-tools
python -m unittest discover -s tools/migration -p test_producer_trace.py
```

The migrated root/tools Python implementations were removed from the current
checkout. They remain recoverable from Git history. Use the reference commit
recorded by the fixture or proof being checked, in its own checkout; do not
restore those implementations as active tools here. A current native producer
checkout does not substitute for a historical Python reference merely because
both scripts call their option `--oracle-root`.

Keep frozen reference checkouts and retained proof directories unchanged. Scripts
that build or capture new evidence should use a new caller-owned output tree.
Existing archived results retain their original analyzer, input and script hashes;
deleting obsolete implementations does not regenerate or revise that evidence.
