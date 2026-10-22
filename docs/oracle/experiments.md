# Experiments

Part of the [oracle documentation](../oracle.md).

An **experiment** compiles and scores many candidate versions of a unit's files against its target, without
touching the project: every compile reads its candidate's files through an **overlay** in place of the disk's. It
is the `experiment.run` method ([api.md](../api.md#experimentrun)), `Unit.experiment` in the Python package, and the engine under
`source.variants` and `unit.inline_sweep`.

```python
from mwcc_tools import Engine, Unit

with Engine(project='/path/to/project') as engine:
    unit = Unit(engine, 'main/MetroidPrime/CControlMapper')
    for row in unit.experiment('build/experiments/mapper-1', space={'variants': {'path': 'mapper.variants'}},
                               symbols=['GetDigitalInput__14CControlMapperCFQ214CControlMapper9ECommandsRC11CFinalInputQ214CControlMapper11EFilterType']):
        print(row['name'], row['status'], row['fuzzy'])
    print(unit.last_summary['control'])
```

## Plans

A plan says which candidates to try (its **space**), how each is evaluated (its **stages**), in what order (its
**strategy**) and how far (its **budget**):

| field | |
|---|---|
| `output` | the run's directory: new, or one to resume |
| `files` | the files candidates may change, relative to the project (default: the unit's source) |
| `baseline` | the baseline's files in place of the disk's (`{path: text}`, or `null` for absent) |
| `space` | where the candidates come from (below) |
| `stages` | default `["compile", {"score": {}}]` |
| `strategy` | `enumerate` (every candidate, in the space's order) |
| `budget` | `candidates`, `compiles`, `seconds`, and a `cancel_file` whose presence stops the run |
| `symbols` | the functions each row scores (by name or demangled name) |
| `functions` | each row gives every target function's match too |
| `jobs` | candidates evaluated at once (default the processors, at most 8) |
| `keep_objects`, `diff` | keep each candidate's object, and its objdiff diff (`diff.json`), in its directory |
| `timeout_ms` | a compile's limit |

The spaces:

- `{"list": [CANDIDATE, ...]}`: explicit candidates, each `{name, files, edits, inline_max_size, suggested}`:
  complete files, exact edits of the baseline's (`{path: [[old, new], ...]}`), or both, and a flag. `suggested`
  is the model edit a `trace.realize` variant realizes ([colouring.md](colouring.md#tracerealize)); its row keeps it.
- `{"variants": {"text": ...}}` or `{"variants": {"path": ...}}`: the variants format (below), applied to `file`
  (default the unit's source).
- `{"axes": [[SET, ...], ...]}`: one choice from each axis (the Cartesian product), each a named edit set
  `{name, edits}`; a candidate's name joins its choices' with ` + `.
- `{"combinations": {"k": K, "up_to": BOOL, "sets": [SET, ...]}}`: every combination of K of the sets (with
  `up_to`, of 1 to K).
- `{"inline_max_size": [N, ...]}`: the baseline with each `inline_max_size(N)` in place of the flags' last one.

Every candidate is rendered into complete files before any is evaluated: an edit whose old text does not occur
exactly once, or a file outside `files`, makes that candidate a `compile_error` row with its reason, and the
others run.

## The variants format

The format of the kit's try-variants: each variant a name and exact replacements (whitespace included), several
per variant:

```text
### hoist the load
@@@ old
    f32 x = mInput->GetAxis(0);
@@@ new
    const f32 x = mInput->GetAxis(0);
```

The old text must occur exactly once in the file. A block's final newline belongs to the separator, so an old
block can end mid-line. A replacement of text with itself is a warning (`source.variants` reports it).

`source.variants` runs a variants file, complete candidate files, or a plan's files against a unit, and reports
every changed function. With `apply`, it writes the best candidate into the working source in the source's newline
style; it refuses when the file changed since the run started. That is the only write to the tree any method makes,
and it is followed by a real build.

## Stages

Each candidate goes through the stages, cheapest first, and any can stop it:

- `compile`: the unit compiled with the candidate's files, as its build compiles it (its flags, its compiler, in
  process). A failure is a `compile_error` row with the compiler's messages. Candidates compile persistently
  where they can (below).
- `score`: the object scored against the target (`unit.score`'s scoring: objdiff-core 3.8.2 under objdiff.json's
  options and the unit's symbol mappings). `prune_if_fuzzy_below` and `prune_if_symbol_below` stop a candidate
  whose unit (or first symbol) scores below a threshold; its row says `pruned`.

## The run's directory

| file | |
|---|---|
| `manifest.json` | the plan, the unit, the declared files (and which were absent), the symbols, the compiler's argv |
| `original/`, `baseline/` | the files as on disk, and the baseline's |
| `NNNN/` | each candidate: its files, `variant.json`, `build.log`, and with `keep_objects` its object and `diff.json` |
| `results.jsonl` | one row per finished candidate, flushed as it finishes |
| `coverage.json` | the run's summary (below), written when it ends |
| `evaluations.jsonl` | the Python package's `run_variants` with `evaluate`: each row as evaluated (python/README.md) |
| `run.lock` | held while the run is going: a second run of the directory waits for it (a minute at most) |

A row has the candidate's `name`, `directory`, `status` (`ok`, `compile_error`, `evaluation_error`), `suggested`, its `error`,
the `scores` of the plan's symbols (each with `strict`: `exact`, or the strict level's first difference), `fuzzy`, `pruned`, `object_sha1`, `cached` (the compile cache answered it),
`fast` (a persistent compiler compiled it), its `timing`, and `duplicate_of` when an earlier candidate has the same files and flag: such a row is that one's,
not evaluated again, and it does not count against `budget.compiles`.

The **control** is the unmodified baseline, compiled first and compared with the build's object
(`identical_to_build`, meaningful without baseline files), and compiled again at the end with the cache bypassed
(`deterministic`). A cancelled run skips the repeat. The summary gives the candidates, the rows evaluated and
resumed, the statuses, the duplicates, the cache hits, what the persistent compilers did (`persistent`: their
compiles, trusted failures, rechecks, `mismatches`, starts), the control, why the run stopped (a budget, a cancel),
its timing and `productive`.

**Resume:** a run in an existing directory checks that the plan is the same (its output, budget and jobs may
differ) and evaluates only the candidates without a row. Nothing needs restoring: the tree was never edited.

## Caches

- **The compile cache** (the session's): a compile's object, keyed by the compiler module and its build, the
  directory, the program, the arguments (the output path left out), the environment, and the overlay's hashes.
  An entry records every file the compiler consulted (opened, listed, or looked for and not found), and is valid
  while each is unchanged: its size and time, or its hash when those changed. It holds 256 MB, least recently used
  first. Traced compiles, compiles out of process, and cancelled or timed-out compiles are not cached. A hit takes
  about 7 ms against 57-67 ms for a compile on Echoes. `unit.compile` and `unit.score` take `cache: false` to
  bypass it, and the control's repeat always does.
- **The score cache** (each target's): the score of an object, keyed by the object's hash and the options, for the
  last 4096 objects. Equal objects (a candidate that changed nothing the compiler keeps) are scored once.

There is no cache on disk: a new session starts cold.

## Persistent compiles

A candidate's source is cut at the last top-level line before its first difference from the control's source
(a `\r\n` and a `\n` line end are the same there). The text before the cut is a context that a persistent
compiler parses once (`mwcc_wasm::PersistPool`, the session's). Each candidate's text after the cut is compiled
from a snapshot taken at that point. A context gets its first persistent compiler on its third request,
started in the background while the requests compile normally. At most 8 persistent compilers are alive per
session (each is a snapshot and a module).

- The objects and messages equal a normal compile's, with `-g` and `-sym on` too: the line numbers after the cut
  are the source's.
- A failure is trusted as it is when its console has the compiler's `Error:` report. One in 8 is still compiled
  normally, and so are a trap, a timeout and a failure without a report. When a normal compile accepts what a
  persistent one rejected, that persistent compiler is dropped.
- The control is never compiled persistently: it is the check against the build.
- A cut that is not at the top level only fails to start; that context's candidates then compile normally.
- The gain is the context's share of the source. On Echoes' CStateManager, with variants of a declaration in the
  middle, a candidate takes 0.85 s against 1.7 s, and a failure 10-23 ms.
- A persistent compiler idle for 2 minutes is dropped. Each holds its snapshot and its module's memory: about
  70 MB for CStateManager's context (a 29 MB snapshot).
- `MWCC_PERSIST=0` in the server's environment turns it off, and `MWCC_PERSIST_VERIFY=1` compiles each persistent
  candidate normally too and compares (the summary's `verified` and `mismatches`). `MWCC_PERSIST_LOG` prints each
  cut, and why a start or a persistent compile failed, on stderr.

## Timing

Each result's `timing` splits its milliseconds by stage: `load`, `compile`, `score`, `trace`, `model`, `queue`
and `other`. An experiment's summary adds `wall_ms`, `jobs`, and `productive`: (compile + score + model) / (wall
time × jobs), the share of the workers' time spent compiling and scoring.

## The overlay and its limits

The overlay replaces files for the compiler only: a path relative to the project, and its text or `null`
(absent). The compiler then sees the candidate's files at their real paths, so `__FILE__`, `#include "..."`
lookups next to the source, and the include paths are those of the build.

- **Precompiled headers:** a unit whose flags use `-prefix X.mch`, or whose edge precompiles, refuses an overlaid
  header ("header H is overlaid, but U uses the precompiled header X, which may have it"): the header would not
  be read. Overlaying the source is fine.
- **Shift JIS:** an edge run by sjiswrap compiles in process with the same transcoding.

## Without a module

A compiler the oracle has no WebAssembly module for (Prime's GC/1.3.2, and any other) runs as an executable,
natively on Windows and through wibo elsewhere, through sjiswrap where the edge runs it so. Then:

- a candidate that changes only the source is compiled from a scratch copy, with `-i <the source's directory>`
  before `-c` so its includes resolve as in the build. `__FILE__` is the scratch copy's path, so a unit whose
  code embeds `__FILE__` (asserts, logging) differs from the build in its data: compare its control, which goes
  through the same copy;
- an overlaid header is an error;
- compiles are not cached.

A traced compile of such a unit (`oracle.trace`, `oracle.regdiff`, `inline.*`, `tu.*`, `trace.*`) runs its
version's native oracle (`build/<version>/oracle/mwcceppc.exe`, GC 1.3 and 1.3.2), the source form's runner with the
unit's source, flags and directory. The project's compiler must be the original the oracle is built from (the SHA-1 of
`config/<version>/config.json`). Each traced compile is a process, and the native oracle traces every function
whatever `function` selects: on Windows, 0.1 s for Prime's `CArchMsgParmInt32Int32VoidPtr`, 13 s for its `main`
and 20 s for `CPlayer` (an 11 MB trace). Then:

- `files` become the runner's overrides: the source as a scratch copy of the same name (with `-i <the source's
  directory>` before the flags; `__FILE__` names the copy, as above), a header as a copy in a shadow tree put before
  the include directory it is under. A file given as absent, one not on disk, and a header under no include directory
  are errors; a header included relative to its includer, not through the include path, is not replaced. A unit that
  uses a precompiled header refuses an overlaid header, and a unit that makes one (`-precompile`) is not traced;
- the oracle does not run through sjiswrap: a unit whose edge runs it so compiles alike only while its source and
  headers are ASCII (a difference says so);
- without `files` and `inline_max_size`, the object is compared with the build's (`compared_with: build`); with
  them, with the stock compiler's of the same compile (`stock_compiler`). The build's object of a unit whose edge
  cleans its extab with dtk after the compile has been through that, and the oracle's has not, as with a module.

## Score parity

`unit.score`'s scores are objdiff-cli 3.8.2's: the same objdiff-core, with `similar` pinned to 3.1.1 (objdiff-cli
3.8.2's lock; 3.2 aligns long functions differently). On 20 Echoes units the fuzzy match, every function's match and
the diff document (`Target::diff_document`, also `oracle.regdiff`'s) equal objdiff-cli's. report.json uses objdiff's
report options, so its numbers can differ from a diff's where those options differ.

## The strict level

Each paired function is also compared by mwdec's exact rules (mwcc-compare's `strict`, a port of mwdec's
comparator): masked words, relocations per word, generated names compared by the bytes they refer to and mapped one
to one, literals in other objects read through the module's extern indexes (its target objects, and its objects as
linked: the build's where it has one). A function's `strict` gives `exact`, its `class` and `classes` (`size`,
`code`, `reloc-layout`, `target-name`, `unresolved`, `literal`), a score and notes; the profile's `reloc` counts the
classes where the masked code is the same. A session builds a module's extern indexes once (main's in 0.5-0.7 s on
Echoes).

- On every Echoes function with a built object (28,239: 26,170 exact), the port's verdict, classes, score and
  number of notes equal mwdec's comparator's.
- It adds 13-17 ms to a score of CStateManager or CPlayer (90 ms without it).
- A target placeholder (`fn_<address>`) is proven against a function our object defines; proving one by compiling
  its definition needs a `PlaceholderProver`, which mwcc-tools does not have.
