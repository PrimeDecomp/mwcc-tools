# mwcc_tools

The mwcc engine from Python: a client of `mwcc-tools serve`, with the standard library only. One `Engine` is one
server and its session: the projects, units, targets, compiled objects and traces it loads stay loaded while it
runs, so later calls are fast. The methods, their parameters and their results are in [docs/api.md](../docs/api.md).

The installers (the repository's README, Installation) make it importable, from the installation's `python` directory; in
a virtual environment, `pip install -e ~/.local/share/mwcc-tools/python` (`$env:LOCALAPPDATA\mwcc-tools\python` on
Windows). It also
installs with `pip install "git+https://github.com/PrimeDecomp/mwcc-tools@vVERSION#subdirectory=python"` (the
release of your `mwcc-tools`), or put this directory on `sys.path` (or `PYTHONPATH`). The engine runs `MWCC_TOOLS`
if it is set, else the `bin/mwcc-tools` of the installation this package is in, or the release build of the
checkout it is in (`target/release/mwcc-tools`), which match it, else `mwcc-tools` on the path. The server finds the mwcc checkout from `MWCC_ORACLE_ROOT`, else from where the binary was built.

## Engine

```python
from mwcc_tools import Engine

with Engine(project='/path/to/echoes') as engine:
    print(engine.version, len(engine.methods))
    for result in engine.call('unit.residuals', below=100.0):
        print(result['summary'])
    one = engine.call_one('oracle.analyze', trace_path='saved.trace', version='GC_2_7')
    print(engine.last_timing)
```

- `Engine(project=None, binary=None, command=None, env=None)` starts the server; `project` is the one a call names
  none of. It checks the server's schema version, and refuses one this package does not speak.
- `engine.call(method, **params)` iterates over a method's results (a dict each). Stopping early cancels the
  request; the results so far stand. `engine.call_one(method, **params)` gives a method's only result.
- A failed method raises `EngineError` with the server's message. Calls may come from several threads at once.
- `engine.last_timing` is the last call's time by stage (milliseconds).
- `engine.close()` ends the server (`with` does it).

## Unit

```python
from mwcc_tools import Engine, Unit

with Engine(project='/path/to/echoes') as engine:
    unit = Unit(engine, 'main/MetroidPrime/CControlMapper')
    print(unit.info['source'], unit.report['measures']['fuzzy_match_percent'])

    source = open('/path/to/echoes/' + unit.info['source'], encoding='utf-8').read()
    edited = source.replace('int i = 0;', 's32 i = 0;')
    compiled = unit.compile({unit.info['source']: edited})
    print(compiled['status'], compiled['object']['sha1'])

    score = unit.score({unit.info['source']: edited}, symbols=['GetMapping__14CControlMapperCFQ214CControlMapper9ECommands'])
    print(score['score']['fuzzy'])
    print(unit.score(built=True)['score']['fuzzy'])
```

- `Unit(engine, name, project=None)` loads the unit: `unit.handle`, `unit.info` (its source, flags, compiler,
  target and build object) and `unit.report` (its report.json entry, when the build has written one).
- `unit.compile(files=None, **options)`: the unit compiled with `files` (`{path relative to the project: text, or
  None for absent}`) in place of the disk's. Nothing is written to the tree. Options are `unit.compile`'s:
  `inline_max_size`, `trace`, `trace_path`, `object_path`, `timeout_ms`, `cache`.
- `unit.score(files=None, built=False, symbols=None, rows=False, **options)`: the object compiled with `files`,
  or the build's (`built`), or `object=PATH`, scored against the target as objdiff-cli 3.8.2 scores it.
- `unit.experiment(output, space, stages=None, strategy=None, budget=None, **plan)`: a plan run in `output`
  (docs/oracle/experiments.md). It yields each finished candidate's row; stopping early cancels the run, and the
  rows written stay. The summary is then `unit.last_summary`.

```python
variants = '''### const
@@@ old
    f32 x = GetAxis(0);
@@@ new
    const f32 x = GetAxis(0);
'''
for row in unit.experiment('build/experiments/mapper', {'variants': {'text': variants}},
                           budget={'seconds': 600}):
    print(row['name'], row['status'], row['fuzzy'], row['scores'])
print(unit.last_summary['control']['identical_to_build'])
```

The methods without a wrapper take the unit's handle:

```python
engine.call_one('source.variants', handle=unit.handle, variants_path='mapper.variants')
engine.call_one('unit.inline_sweep', handle=unit.handle, sizes=[16, 32, 64])
engine.call_one('unit.sinit_profile', handle=unit.handle)
engine.call_one('oracle.regdiff', handle=unit.handle, function='GetMapping', text=True)['text']
engine.call_one('inline.diff', a={'handle': unit.handle}, target=True, text=True)['text']
engine.call_one('tu.data_diff', handle=unit.handle, text=True)['text']
```

## Trace

```python
from mwcc_tools import Engine, Trace, Unit

with Engine(project='/path/to/echoes') as engine:
    unit = Unit(engine, 'main/MetroidPrime/CControlMapper')
    trace = Trace(engine, unit, function='GetDigitalInput')
    function = trace.functions[0]['function']
    report = trace.target(function)
    print('\n'.join(trace.target(function, text=True)['text']))
    wrong = report['result']['wrong']
    replayed = trace.replay(function, [['degree', wrong[0], 1], ['drop', wrong[0]]])
    many = trace.replay_many(function, [[['degree', v, 1]] for v in wrong])
    best = trace.search(function, iterations=2000, restarts=2)
```

- `Trace(engine, unit=None, **params)` loads a trace (`trace.load`): a `Unit` (or a unit's name) compiled with a
  trace, through `files` and with `function` keeping one function's records; or a saved trace, `load='PREFIX'`
  (PREFIX.trace, with PREFIX.o, PREFIX.source and PREFIX.manifest.json when there) and its `version`. `save='PREFIX'` saves a
  unit's trace. It gives `trace.handle`, `trace.functions` and `trace.version`.
- Its colouring methods take the function and `class_` (GPR, FPR, VR or CR; default GPR): `verify`, `target`,
  `force` and `replay` (with `wants`, registers the target has by vreg, and `text`), `replay_many` (each edit list
  replayed on the same graph: one result per list), `search` (`iterations`, `seed`, `restarts`, `top`,
  `fixed`, `nodes`, `kinds`) and `realize(function, edits, limit=None)`: one solution's edits as source variants,
  whose `candidates` are an experiment's space (`unit.experiment(output, {'list': result['candidates']})`), each
  labelled `suggested` (docs/oracle/colouring.md).
- Edits are lists: `['degree', 35, 1]`, `['move', 40, 'after', 37]`, `['drop', 35]`, `['link', 35, 40]`,
  `['unlink', 35, 40]`, `['neighbours', 35, 1]` and `['order', [35, 36, 37]]`. They apply in the order given.

`trace.remap` compares two traces' graphs: `engine.call_one('trace.remap', first=a.handle, second=b.handle,
function=...)`.

## Source experiments

The decomp kits' `decomp_experiment` and `decomp_matrix` modules are these functions with `root` set to the
checkout. Candidates are complete files rendered in Python and compiled through the engine; the working tree is
never written.

```python
from pathlib import Path
from mwcc_tools import run_variants, run_matrix, checked_replace

root = Path('/path/to/echoes')
source = 'src/Echoes/MetroidPrime/CControlMapper.cpp'
text = (root / source).read_text(encoding='utf-8')
rows = run_variants([('two', {source: checked_replace(text, 'mControlScheme == 1', 'mControlScheme == 2')})],
                    files=[source], build=['ninja', 'build/G2ME01/src/MetroidPrime/CControlMapper.o'],
                    output_dir='build/experiments/scheme', root=root, symbols=['GetDigitalInput'])
coverage = run_matrix({'scheme': ['1', '2']},
                      lambda c: {source: text.replace('mControlScheme == 1', 'mControlScheme == ' + c['scheme'])},
                      files=[source], build=['ninja', 'build/G2ME01/src/MetroidPrime/CControlMapper.o'],
                      output_dir='build/experiments/matrix', root=root, symbols=['GetDigitalInput'])
```

- `run_variants(variants, *, files, build, output_dir, root, unit=None, symbols=(), baseline=None, evaluate=None,
  engine=None, on_row=None, quiet=False, **plan)`: `variants` are `(name, {path: text, bytes or None})`; `files`
  every path they may change (`None` makes one absent); `build` the unit's ninja target, which names the unit (an
  edge that is no unit's object is refused before anything compiles); `output_dir` a new run directory, or one to
  resume. Each row scores `symbols` (as `score_symbols` gives them), or is what `evaluate(candidate)` returns:
  `candidate` has `name`, `directory`, `files` (the candidate's files there), `object` and `diff`. An evaluated
  row is written to the run's `evaluations.jsonl` (the engine's own rows are in `results.jsonl`); a duplicate
  candidate gets its original's evaluation, and a resumed run first evaluates the rows an interrupted one left.
  `plan` takes the rest of the plan (`budget`, `jobs`, `timeout_ms`, `keep_objects`, `diff`).
- `score_symbols(unit, symbols, *, root, engine=None)`: each symbol's `percent`, `original`, `emitted`,
  `differing`, `exact_instructions` and `strict` (the strict level's verdict: `exact`, or its first difference) for
  the build's object.
- `run_matrix(axes, render, *, files, build, output_dir, root, baseline=None, skip=None, context=None,
  max_cases=256, **options)`: every combination of `axes` rendered by `render(choices)` (and `skip(choices)`, a
  reason or `None`) before any compiles, written to `matrix.json`, then run with `run_variants` in `run/`;
  `coverage.json` is refreshed after each row, from the evaluated rows when there is an `evaluate`.
  `matrix_cases(axes)`, `refresh_coverage(output_dir)` and `checked_replace(text, old, new, count=1)` are its
  parts.

`mwcc_tools.reports` gives the kit scripts' printed reports from the methods' results: `try_variants`
(`source.variants`), `inline_sweep`, `residuals` and `sinit_profile` (the `unit.*` methods), each a list of lines.

## Tests

```
python -m unittest discover -s python/tests
```

They run against a fake server; with `MWCC_TEST_ECHOES` set to an Echoes checkout, they also run against the real
engine on it.
