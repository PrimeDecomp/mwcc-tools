# The oracle in a decomp kit

A kit's experiment scripts and oracle helpers are short clients of mwcc's Python package, `mwcc_tools`, which runs
`mwcc-tools serve`: each unit's compiler and flags come from its build.ninja edge, and the unit compiles in
memory. Candidates (`--candidate FILE`, or `--file PATH=FILE` for any source or header) are read in place of the
checkout's files, which are never changed, and no build object is overwritten. New automation should use the
package or `mwcc-tools call` and read JSON results rather than parse the scripts' reports.

## Install

Build mwcc-tools and its compiler module in a checkout of PrimeDecomp/mwcc-tools (the kit's `references/mwcc-tools.bundle`, or a
clone), and set `MWCC_ORACLE_ROOT` to it:

```text
cargo build --release --locked -p mwcc-tools
target/release/mwcc-tools configure --version GC_2_7
ninja build/GC_2_7/wasm/mwcc.wasm
```

`scripts/_mwcc_tools_path.py` imports the package from `$MWCC_ORACLE_ROOT/python`, so it always matches its
executable (`MWCC_TOOLS`, else `$MWCC_ORACLE_ROOT/target/release/mwcc-tools`, else PATH). Without them the scripts
stop with these commands. GC 2.6 and 2.7 have a module (`mwcc.wasm`); a unit of another version runs its compiler
as a process, and the oracle's traces need a module. A kit may instead install a released executable
(`install-native.py PROJECT --apply`, from the kit's `native-manifest.json`) and point `MWCC_TOOLS` at it.

## The scripts

Run them from the game checkout; `--help` gives each one's options.

| Script | Method | |
|---|---|---|
| `try-variants.py -u UNIT [-f FN] --variants v.txt [--candidate FILE] [--keep DIR] [--apply NAME]` | `source.variants` | source variants scored in parallel against the unmodified source, which is compared with the build object |
| `residual-scan.py -u UNIT... [--below N] [--only VERDICT] [--built] [--json]` | `unit.residuals` | each nonmatching function's residual: register, stack or structural |
| `inline-sweep.py N -u UNIT...` | `unit.inline_sweep` | the units scored at another `inline_max_size` |
| `sinit-profile.py KEEPDIR [TARGET.o]` | `unit.sinit_profile` | a function's block profile in each kept object |
| `oracle-run.py -u UNIT [-f FN] [--locals] [--schedule] [--blocks] [--cse] [--ir PASS] [--inline-cost TEXT] [--exchange A,B] [--json]` | `oracle.trace` | the compiler's views of a traced compile; the last line says whether the object equals the build's |
| `oracle-regdiff.py -u UNIT -f FN [--pcode] [--json]` | `oracle.regdiff` | register differences against the target, explained |
| `oracle-colouring.py {verify,target,force,search,replay,remap} -u UNIT \| --load PREFIX -f FN` | `trace.*` | what-if register colouring (verify the replay first; spills are not modelled) |
| `inline-calibrate.py --callee FN [-u UNIT] [--breakdown] [--binary] [--file PATH=FILE]` | `inline.calibrate` | the inliner's decisions against what the target implies |

`decomp_experiment` (`run_variants`, `score_symbols`) and `decomp_matrix` (`run_matrix`, `checked_replace`,
`matrix_cases`, `refresh_coverage`) are the package's functions with `root` set to the checkout:
`sys.path.insert(0, 'scripts'); from decomp_experiment import run_variants`. `evaluate` is
`evaluate(candidate)`, with the candidate's `name`, `directory`, `files`, `object` and `diff`.

## The package and one-off calls

```python
import sys
sys.path.insert(0, 'scripts')
from _mwcc_tools_path import engine, load
tools = load()
with engine() as session:
    unit = tools.Unit(session, 'main/Kyoto/Math/CAABox')
    score = unit.score(symbols=['GetPoint__6CAABoxCFi'])
    traced = session.call_one('oracle.trace', handle=unit.handle, function='GetPoint__6CAABoxCFi')
```

`mwcc-tools call METHOD KEY=VALUE ... --project .` runs one method and prints its results as JSON lines;
`mwcc-tools methods [NAME]` lists the methods and their schemas. In the source checkout, `docs/api.md` is the
reference, `python/README.md` the package's guide and `docs/oracle/experiments.md` the experiment plans.

Read checks explicitly: `oracle.trace`'s `identical` says whether a unit's object equals the build's (it is not
compared for a candidate), and a source's `compilation` records its stock-compiler check. A successful exit alone
does not establish equality.

## Development workflows

These methods need a freshly built mwcc-tools from the source checkout; paths below are relative to it.

| Task | Method | Reference |
| --- | --- | --- |
| Explain observed backend births and coalescing | `provenance.inspect` | `docs/oracle/provenance.md` |
| Find C++ edits that meet a graph objective | `source_search.run` | `docs/oracle/source-search.md` |
| Shrink a case while retaining a mechanism | `source_reduce.run` | `docs/oracle/source-reduce.md` |
| Export and freshly replay retained inputs | `source_repro.export`, `source_repro.replay` | `docs/oracle/source-repro.md` |
| Find matching mechanisms in existing source | `source_index.query` | `docs/oracle/source-index.md` |
| Inspect guarded historical allocations | `source_arenas.inspect` | `docs/oracle/source-arenas.md` |

Capture backend evidence with `oracle.trace` and `capture_path` on a source. Search and reduction plans specify
the objective, required symbolic relocations and semantic checker. Stock equality validates instrumentation; it
does not establish equivalent behavior between source variants. Missing observations do not prove elimination,
and pointer or register equality does not establish source ancestry.
