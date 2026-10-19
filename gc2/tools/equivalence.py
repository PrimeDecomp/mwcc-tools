#!/usr/bin/env python3
"""python tools/equivalence.py CORPUS.json OUTDIR [--version V | ORIGINAL CANDIDATE]: compile a corpus with the
original compiler and a candidate, and compare the objects byte for byte. By default the candidate is the equivalent
compiler of V (default GC_2_6: build/V/mwcceppc.exe, brought up to date with ninja first) and the original V's.

CORPUS.json: {"root": DIR, "jobs": [{"source": PATH, "cflags": [...], "sjis": bool}, ...]}: each source compiled in
ROOT with its flags, through sjiswrap (ROOT/build/tools/sjiswrap.exe) when "sjis". Each job's outputs are
OUTDIR/{original,candidate}/<job number>.o; OUTDIR/results.json has every job's result. A difference is a behaviour
of the original the candidate does not have. Exits 1 unless every job's objects are identical: a job the original
fails tests nothing, and that is a failure of the corpus.

The compilers run under $WIBO (default build/tools/wibo). sjiswrap needs a wibo that can load it: a decomp-toolkit
project's own (WIBO=PROJECT/build/tools/wibo).

python tools/equivalence.py corpus PROJECT MW_VERSION OUT.json: a corpus of the sources a decomp-toolkit project
builds with MW_VERSION (as its build.ninja names it, e.g. GC/2.6): one job per source.
"""

import argparse
import concurrent.futures
import json
import os
import shlex
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import build, config, project
import equivalent


def compile_one(root, compiler, job, output):
    command = build.windows_prefix()
    if job.get("sjis"):
        command.append(str(Path(root) / "build/tools/sjiswrap.exe"))
    command += [str(compiler), *job["cflags"], "-c", job["source"], "-o", str(output)]
    output.unlink(missing_ok=True)
    try:
        result = subprocess.run(
            command,
            cwd=root,
            capture_output=True,
            text=True,
            errors="replace",
            timeout=900,
            env=dict(os.environ, MWCIncludes="."),
        )
    except subprocess.TimeoutExpired:
        return "timeout"
    if result.returncode or not output.exists():
        return (result.stdout + result.stderr)[-600:] or f"exit status {result.returncode}"
    return None


def run(corpus_path, out, original, candidate):
    corpus = json.loads(Path(corpus_path).read_text())
    for side in ("original", "candidate"):
        (out / side).mkdir(parents=True, exist_ok=True)
        for old in (out / side).glob("*.o"):
            old.unlink()

    def one(numbered):
        number, job = numbered
        a, b = out / "original" / f"{number}.o", out / "candidate" / f"{number}.o"
        result = dict(job=number, source=job["source"])
        failure = compile_one(corpus["root"], original, job, a)
        if failure:
            return dict(result, result="original fails", log=failure)
        failure = compile_one(corpus["root"], candidate, job, b)
        if failure:
            return dict(result, result="candidate fails", log=failure)
        return dict(result, result="identical" if a.read_bytes() == b.read_bytes() else "different")

    with concurrent.futures.ThreadPoolExecutor(int(os.environ.get("JOBS", 0)) or os.cpu_count() or 4) as pool:
        results = list(pool.map(one, enumerate(corpus["jobs"])))
    project.write_atomic(
        out / "results.json",
        json.dumps(
            dict(
                original=str(original),
                candidate=str(candidate),
                candidate_sha1=project.digest(Path(candidate).read_bytes()),
                results=results,
            ),
            indent=1,
        )
        + "\n",
    )
    counts = {}
    for r in results:
        counts[r["result"]] = counts.get(r["result"], 0) + 1
    for r in results:
        if r["result"] != "identical":
            print(
                f"{r['result']}: job {r['job']}, {r['source']}"
                + (f": {' '.join(r['log'].split())[-300:]}" if r.get("log") else "")
            )
    print(", ".join(f"{n} {what}" for what, n in sorted(counts.items())), f"(of {len(results)}; {out}/results.json)")
    return all(r["result"] == "identical" for r in results) and results


def dtk_corpus(root, mw_version, output):
    """The jobs of a decomp-toolkit project's build.ninja: every mwcc edge built with MW_VERSION."""
    root = Path(root).resolve()
    text = (root / "build.ninja").read_text().replace("$\n", "")
    jobs, edge = [], None

    def flush():
        if edge and edge["vars"].get("mw_version") == mw_version:
            jobs.append(
                dict(source=edge["source"], cflags=shlex.split(edge["vars"]["cflags"]), sjis="sjis" in edge["rule"])
            )

    for line in text.splitlines():
        if line.startswith("build "):
            flush()
            edge = None
            _, _, rest = line[6:].partition(":")
            fields = rest.split()
            if fields and fields[0].startswith("mwcc"):
                edge = dict(rule=fields[0], source=fields[1], vars={})
        elif line.startswith("  ") and edge is not None and " = " in line:
            key, _, value = line.strip().partition(" = ")
            edge["vars"][key] = " ".join(value.split())
        elif not line.startswith("  "):
            flush()
            edge = None
    flush()
    # (a source built twice under one version is one test)
    unique = list({job["source"]: job for job in jobs}.values())
    if not unique:
        raise project.Error(f"{root}/build.ninja has no mwcc edge with mw_version = {mw_version}")
    project.write_atomic(output, json.dumps(dict(root=str(root), jobs=unique), indent=1) + "\n")
    print(f"{len(unique)} jobs")


def main():
    args = sys.argv[1:]
    try:
        if args[:1] == ["corpus"]:
            if len(args) != 4:
                sys.exit(__doc__)
            dtk_corpus(*args[1:])
            return
        parser = argparse.ArgumentParser(usage=__doc__.split("\n")[0][len("python tools/equivalence.py ") :])
        parser.add_argument("corpus")
        parser.add_argument("out")
        parser.add_argument("compilers", nargs="*", metavar="ORIGINAL CANDIDATE")
        parser.add_argument("--version", default=project.PRIMARY)
        options = parser.parse_args(args)
        if options.compilers:
            if len(options.compilers) != 2:
                parser.error("give both ORIGINAL and CANDIDATE, or neither")
            original, candidate = (Path(c).resolve() for c in options.compilers)
        else:
            v = project.version(options.version)
            original, candidate = config.version_config(v).original, equivalent.output_path(v)
            result = subprocess.run(
                ["ninja", project.relative(candidate)], cwd=project.ROOT, capture_output=True, text=True
            )
            if result.returncode:
                raise project.Error(f"ninja {project.relative(candidate)} failed:\n{result.stdout[-3000:]}")
        if project.digest(Path(original).read_bytes()) == project.digest(Path(candidate).read_bytes()):
            # (the original against itself tests nothing)
            raise project.Error(f"{candidate} is the original compiler itself")
        if not run(options.corpus, Path(options.out), original, candidate):
            sys.exit(1)
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
