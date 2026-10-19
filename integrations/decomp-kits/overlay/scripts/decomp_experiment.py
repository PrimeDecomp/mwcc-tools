"""Reusable source experiments on this checkout: the `mwcc_tools` package's `run_variants` and `score_symbols`.

See the kit's experiments guide for examples, and .agents/docs/oracle-native.md for the rest of the package. Candidates are rendered in Python and compiled in process through mwcc-tools (`build` names the unit's
ninja edge); the working tree is never written, so nothing is restored or rebuilt, and no lock is needed.
`evaluate` is `evaluate(candidate)`, with the candidate's `name`, `directory`, `files`, `object` and `diff`
paths. Rows stream as candidates finish; Ctrl-C cancels the run and keeps the rows written so far. Requires
mwcc-tools (MWCC_ORACLE_ROOT); see EXTERNALS.md.

    sys.path.insert(0, 'scripts'); from decomp_experiment import run_variants
"""
from _mwcc_tools_path import ROOT, load


def score_symbols(unit, symbols, *, root=ROOT, engine=None):
    """Each of SYMBOLS' entry (percent, original, emitted, differing, exact_instructions) for UNIT's build object;
    no rounded scores."""
    return load(package_only=engine is not None).score_symbols(unit, symbols, root=root, engine=engine)


def run_variants(variants, *, files, build, output_dir, root=ROOT, **options):
    """Run (name, {repo-relative file: text/bytes/None}) variants from one baseline; return the rows.

    files: all files the variants may change; None makes a file absent for that variant.
    build: the unit's ninja target, e.g. ['ninja', 'build/G2ME01/src/Example.o'].
    output_dir: a NEW directory (relative to root), or one to resume.
    options: unit, symbols, baseline, evaluate(candidate), on_row, quiet, engine, and the plan's budget, jobs,
    timeout_ms, keep_objects and diff (the package's run_variants).

    Never applies a candidate, commits, or changes the build or the regression baseline.
    """
    tools = load(package_only=options.get('engine') is not None)
    return tools.run_variants(variants, files=files, build=build, output_dir=output_dir, root=root, **options)
