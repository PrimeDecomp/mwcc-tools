"""Explicit Cartesian source experiments on this checkout: the `mwcc_tools` package's `run_matrix`,
`checked_replace`, `matrix_cases` and `refresh_coverage`. See the kit's experiments guide.

run_matrix renders every case before compiling any, writes `matrix.json`, and refreshes `coverage.json` after
each row; a case equal to an earlier one is reported (`duplicate_of`). Requires mwcc-tools (MWCC_ORACLE_ROOT).
"""
from _mwcc_tools_path import ROOT, load


def checked_replace(text, old, new, *, count=1):
    """Reject missing/ambiguous anchors and no-op edits (str or bytes)."""
    return load(package_only=True).checked_replace(text, old, new, count=count)


def matrix_cases(axes, *, max_cases=256):
    """Enumerate every combination of named string choices, without pruning."""
    return load(package_only=True).matrix_cases(axes, max_cases=max_cases)


def refresh_coverage(output_dir):
    """Reconcile persisted results, including after an interrupted run; no builds."""
    return load(package_only=True).refresh_coverage(output_dir)


def run_matrix(axes, render, *, files, build, output_dir, root=ROOT, **options):
    """Render all cases before compiling any, then run them with run_variants.

    render(choices) returns the usual {relative_file: full_text/bytes/None}; options: baseline, skip(choices)
    (None or a nonempty reason), context (provenance), max_cases, and run_variants' options.
    All callbacks must be pure with respect to the working files/build configuration.
    """
    tools = load(package_only=options.get('engine') is not None)
    return tools.run_matrix(axes, render, files=files, build=build, output_dir=output_dir, root=root, **options)
