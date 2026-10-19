"""Source experiments on the engine: decomp_experiment's `run_variants` and `score_symbols`, and decomp_matrix's
`run_matrix`, `checked_replace`, `matrix_cases` and `refresh_coverage`.

Candidates are complete files, rendered in Python and compiled in process through the engine's overlay: the
working tree is never written, so nothing is restored or rebuilt and no lock is needed. See
docs/oracle/experiments.md for the run's directory and rows.
"""
from collections import Counter
from hashlib import sha256
import inspect
from itertools import product
import json
from math import prod
from pathlib import Path

from .engine import Engine, EngineError, Unit


def _text(value, path):
    if value is None or isinstance(value, str):
        return value
    if isinstance(value, (bytes, bytearray)):
        try:
            return bytes(value).decode('utf-8')
        except UnicodeDecodeError as exc:
            raise ValueError(f'{path} is not UTF-8 text (the compiler reads sources as UTF-8): {exc}') from exc
    raise ValueError(f'{path}: a file is text, bytes or None, not {type(value).__name__}')


def _f32(value):
    """A match percent as objdiff writes it: the shortest decimal of its f32 (the engine's JSON widens it)."""
    if not isinstance(value, float):
        return value
    import struct
    single = struct.unpack('f', struct.pack('f', value))[0]
    for digits in range(1, 10):
        short = float(f'{value:.{digits}g}')
        if struct.unpack('f', struct.pack('f', short))[0] == single:
            return short
    return value


def _scores(scores):
    return {q: dict(s, percent=_f32(s.get('percent'))) if isinstance(s, dict) else s for q, s in scores.items()}


def _posix_keys(mapping):
    """Normalize repo-relative path keys so backslash spellings name the same file."""
    result = {}
    for path, value in mapping.items():
        key = Path(path).as_posix()
        if key in result:
            raise ValueError('Duplicate path after normalization: ' + key)
        result[key] = value
    return result


def _files(files, root):
    files = [Path(p).as_posix() for p in files]
    if not files or len(files) != len(set(files)):
        raise ValueError('files must be a nonempty unique list')
    for name in files:
        path = root / name
        if Path(name).is_absolute() or '..' in Path(name).parts or path.resolve() != path:
            raise ValueError('Use ordinary repo-relative files without symlinks: ' + name)
        if path.exists() and not path.is_file():
            raise ValueError('Not a file: ' + name)
    return files


def _unit_of_build(build, root, unit):
    """The objdiff unit whose object BUILD (a ninja argv) names."""
    if not build or isinstance(build, (str, bytes)):
        raise ValueError('build must be a nonempty argv sequence')
    target = Path(build[-1]).as_posix()
    config = json.loads((root / 'objdiff.json').read_text(encoding='utf-8'))
    named = [u['name'] for u in config.get('units', [])
             if u.get('base_path') and Path(u['base_path']).as_posix() == target]
    if not named:
        raise ValueError(f'build names {target}, which is no objdiff unit\'s object (base_path): the engine '
                         'compiles units, not other ninja edges')
    if unit is not None and unit != named[0]:
        raise ValueError(f'build compiles {named[0]}, not unit {unit}')
    return named[0]


def _evaluate_takes_candidate(evaluate):
    try:
        parameters = inspect.signature(evaluate).parameters.values()
    except (TypeError, ValueError):
        return
    positional = [p for p in parameters if p.kind in (p.POSITIONAL_ONLY, p.POSITIONAL_OR_KEYWORD, p.VAR_POSITIONAL)]
    if not positional:
        raise TypeError('evaluate now takes the candidate: evaluate(candidate), with its name, directory, files, '
                        'object and diff paths (the working tree is no longer edited, so it cannot read it)')


def score_symbols(unit, symbols, *, root, engine=None):
    """Each of SYMBOLS' entry (percent, original, emitted, differing, exact_instructions, strict) for the build's object of
    UNIT, no rounded scores. A symbol is found by its name, demangled name or short method name, then by a unique
    substring."""
    own = engine is None
    engine = engine or Engine(project=root)
    try:
        result = Unit(engine, unit).score(built=True, scores=list(symbols))
    except EngineError as exc:
        if 'missing comparison symbol' in str(exc):
            raise ValueError('Missing comparison symbol: ' + str(exc).rsplit(': ', 1)[-1]) from exc
        raise
    finally:
        if own:
            engine.close()
    return _scores(result.get('scores', {}))


def run_variants(variants, *, files, build, output_dir, root, unit=None, symbols=(), baseline=None,
                 evaluate=None, engine=None, on_row=None, quiet=False, **plan):
    """Run (name, {repo-relative file: text/bytes/None}) variants from one baseline; return the rows.

    files: all files the variants may change; None makes a file absent for that variant.
    baseline: optional file overrides; default is the working tree.
    build: the unit's ninja target, e.g. ['ninja', 'build/G2ME01/src/Example.o']: it names the unit compiled.
    evaluate: optional evaluate(candidate) returning JSON-serializable scores, where candidate has `name`,
              `directory`, `files` (the candidate's files there), `object` and `diff` paths; otherwise each row
              scores `symbols` with the project's objdiff policy.
    output_dir: a NEW directory (relative to root), or one to resume.
    on_row: called with each row as it finishes. Rows print as JSON lines unless `quiet`.
    plan: the rest of the plan (budget, jobs, timeout_ms, keep_objects, diff, functions).

    Never applies a candidate, commits, or changes the build. Ctrl-C cancels the run; the rows written stay.
    """
    root = Path(root).resolve()
    files = _files(files, root)
    unit = _unit_of_build(build, root, unit)
    if evaluate is not None:
        _evaluate_takes_candidate(evaluate)
        plan.setdefault('keep_objects', True)
        plan.setdefault('diff', True)
    elif not symbols:
        raise ValueError('Provide symbols to score, or an evaluate(candidate) callback')
    out = (root / Path(output_dir)).resolve()
    if any(root / p == out or out in (root / p).parents or (root / p) in out.parents for p in files):
        raise ValueError('output_dir must be separate from variant files')
    baseline = _posix_keys(baseline or {})
    if set(baseline) - set(files):
        raise ValueError('baseline contains files missing from files')
    candidates = []
    for name, replacements in variants:
        replacements = _posix_keys(replacements)
        if set(replacements) - set(files):
            raise ValueError('Variant contains files missing from files')
        candidates.append(dict(name=name, files={p: _text(v, p) for p, v in replacements.items()}))
    # (with evaluate, a row is final once evaluated: evaluations.jsonl has those rows, results.jsonl the engine's)
    evaluations = out / 'evaluations.jsonl'
    evaluated = {row['name']: row for row in _rows(evaluations)} if evaluate is not None else {}
    rows = []

    def finish(row):
        if isinstance(row.get('scores'), dict):
            row['scores'] = _scores(row['scores'])
        if evaluate is not None and row.get('status') == 'ok':
            original = evaluated.get(row.get('duplicate_of'))
            if original is not None:
                # (a duplicate has no object of its own: it is its original's evaluation)
                row = dict(row, status=original['status'],
                           **{k: original[k] for k in ('scores', 'error') if k in original})
            else:
                directory = Path(row['directory'])
                candidate = dict(name=row['name'], directory=directory,
                                 files={p: directory / p for p in files},
                                 object=directory / 'object.o',
                                 diff=directory / 'diff.json')
                try:
                    row['scores'] = evaluate(candidate)
                    json.dumps(row)
                except Exception as exc:  # noqa: BLE001 - one candidate's callback must not end the run
                    row = dict(name=row['name'], directory=row['directory'], status='evaluation_error',
                               error=str(exc)[:240])
        if evaluate is not None:
            evaluated[row['name']] = row
            with evaluations.open('a', encoding='utf-8') as stream:
                stream.write(json.dumps(row) + '\n')
        rows.append(row)
        if not quiet:
            print(json.dumps(row), flush=True)
        if on_row is not None:
            on_row(row)

    own = engine is None
    engine = engine or Engine(project=root)
    try:
        # (a resumed run first evaluates the rows the engine finished before an interrupted evaluation)
        if evaluate is not None and evaluations.exists():
            for row in _rows(out / 'results.jsonl'):
                if row['name'] not in evaluated:
                    finish(row)
        handle = Unit(engine, unit)
        for row in handle.experiment(
                str(out), {'list': candidates}, files=files, symbols=list(symbols),
                baseline={p: _text(v, p) for p, v in baseline.items()}, **plan):
            if evaluate is not None and not evaluations.exists():
                evaluations.touch()
            finish(row)
    finally:
        if own:
            engine.close()
    return rows


def _rows(path):
    """The rows of a .jsonl file (none when it is missing; an interrupted last line is left out)."""
    if not Path(path).exists():
        return []
    lines = Path(path).read_text(encoding='utf-8').splitlines()
    rows = []
    for i, line in enumerate(lines):
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            if i != len(lines) - 1:
                raise
    return rows


def checked_replace(text, old, new, *, count=1):
    """Reject missing/ambiguous anchors and no-op edits (str or bytes)."""
    if not old or old == new or not isinstance(count, int) or count < 1:
        raise ValueError('Require a nonempty anchor, a changed replacement and positive count')
    actual = text.count(old)
    if actual != count:
        raise ValueError(f'Expected {count} occurrences of anchor; found {actual}')
    return text.replace(old, new)


def matrix_cases(axes, *, max_cases=256):
    """Enumerate every combination of named string choices, without pruning."""
    if not axes or not isinstance(max_cases, int) or max_cases < 1:
        raise ValueError('Require nonempty axes and a positive max_cases')
    choices = {}
    for name, values in axes.items():
        if not isinstance(name, str) or not name or isinstance(values, (str, bytes)):
            raise ValueError('Axes require nonempty names and sequences of choice names')
        values = list(values)
        if (not values or any(not isinstance(v, str) or not v for v in values)
                or len(values) != len(set(values))):
            raise ValueError('Choices must be nonempty, unique strings: ' + name)
        choices[name] = values
    total = prod(map(len, choices.values()))
    if total > max_cases:
        raise ValueError(f'{total} combinations exceed max_cases={max_cases}; narrow axes or raise explicitly')
    return [dict(id=f'{i:04d}', choices=dict(zip(choices, values)), status='untested')
            for i, values in enumerate(product(*choices.values()))]


def _write(path, data):
    path = Path(path)
    if data is None:
        path.unlink(missing_ok=True)
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data.encode('utf-8') if isinstance(data, str) else data)


def _json(path, value):
    _write(path, json.dumps(value, indent=2, sort_keys=True) + '\n')


def _snapshot_id(snapshot):
    hashes = {p: None if data is None else sha256(data).hexdigest()
              for p, data in sorted(snapshot.items())}
    return sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest()


def refresh_coverage(output_dir):
    """Reconcile persisted results, including after an interrupted run; no builds."""
    out = Path(output_dir)
    plan = json.loads((out / 'matrix.json').read_text(encoding='utf-8'))
    cases = {case['id']: case for case in plan['cases']}
    results = out / 'run/results.jsonl'
    incomplete_result = False
    # (with evaluate, a compiled case counts once it is evaluated: evaluations.jsonl has its final row)
    evaluations = out / 'run/evaluations.jsonl'
    evaluated = {row['name']: row for row in _rows(evaluations)} if evaluations.exists() else None
    if results.exists():
        lines = results.read_text(encoding='utf-8').splitlines()
        for i, line in enumerate(lines):
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                if i != len(lines) - 1:
                    raise
                incomplete_result = True
                break
            if evaluated is not None and row['status'] == 'ok':
                row = evaluated.get(row['name'])
                if row is None:
                    continue
            case = cases[row['name']]
            case['status'] = row['status']
            case['result'] = row
    counts = dict(Counter(case['status'] for case in cases.values()))
    coverage = dict(total=len(cases), scored=counts.get('ok', 0), counts=counts,
                    all_scored=counts.get('ok', 0) == len(cases),
                    incomplete_result=incomplete_result,
                    duplicate_sources=sum('duplicate_of' in c for c in cases.values()),
                    cases=list(cases.values()))
    _json(out / 'coverage.json', coverage)
    return coverage


def run_matrix(axes, render, *, files, build, output_dir, root, baseline=None, skip=None, context=None,
               max_cases=256, **runner_options):
    """Render all cases before compiling any, then run them with run_variants.

    render(choices) returns the usual {relative_file: full_text/bytes/None}.
    skip(choices) returns None or a nonempty reason. Rendering failures are recorded.
    context is caller-supplied provenance (version, revision, compiler flags, etc.).
    A candidate equal to an earlier one is reported (`duplicate_of`) and given that one's row.
    All callbacks must be pure with respect to the working files/build configuration.
    """
    cases = matrix_cases(axes, max_cases=max_cases)
    root = Path(root).resolve()
    files = _files(files, root)
    unit = _unit_of_build(build, root, runner_options.pop('unit', None))
    build = list(build)
    out = (root / output_dir).resolve()
    if any(root / p == out or out in (root / p).parents or (root / p) in out.parents for p in files):
        raise ValueError('output_dir must be separate from variant files')

    def normalize(replacements):
        result = {Path(p).as_posix(): v.encode() if isinstance(v, str) else v
                  for p, v in replacements.items()}
        if set(result) - set(files) or any(v is not None and not isinstance(v, bytes)
                                         for v in result.values()):
            raise ValueError('Replacements require declared files and str/bytes/None values')
        return result

    original = {p: (root / p).read_bytes() if (root / p).exists() else None for p in files}
    start = {**original, **normalize(baseline or {})}
    plan = dict(cases=cases, files=files, build=build, context=context or {},
                root=str(root), baseline_sha256=_snapshot_id(start))
    json.dumps(plan)  # Reject unserializable provenance before creating output.
    out.mkdir(parents=True, exist_ok=False)
    _json(out / 'matrix.json', plan)  # The entire planned space exists before rendering.
    for p, content in start.items():
        _write(out / 'baseline' / p, content)
    seen = {}
    variants = []
    try:
        for case in cases:
            try:
                reason = skip(dict(case['choices'])) if skip else None
                if reason is not None:
                    if not isinstance(reason, str) or not reason.strip():
                        raise ValueError('skip must return None or a nonempty reason')
                    case.update(status='skipped', reason=reason)
                    continue
                candidate = {**start, **normalize(render(dict(case['choices'])))}
                digest = _snapshot_id(candidate)
                case.update(source_sha256=digest, baseline_equal=candidate == start,
                            absent=[p for p, v in candidate.items() if v is None])
                if digest in seen:
                    case['duplicate_of'] = seen[digest]
                else:
                    seen[digest] = case['id']
                for p, content in candidate.items():
                    _write(out / 'candidates' / case['id'] / p, content)
                variants.append((case['id'], candidate))
            except Exception as exc:  # noqa: BLE001 - a case's rendering failure is its status
                case.update(status='generation_error', error=f'{type(exc).__name__}: {exc}')
            finally:
                _json(out / 'matrix.json', plan)
        if variants:
            run_variants(variants, files=files, build=build, root=root, baseline=start, unit=unit,
                         output_dir=out / 'run', on_row=lambda _: refresh_coverage(out), **runner_options)
    except BaseException as exc:
        _json(out / 'runner-error.json', dict(type=type(exc).__name__, error=str(exc)))
        raise
    finally:
        refresh_coverage(out)
    return refresh_coverage(out)
