#!/usr/bin/env python3
"""python tools/header_impact.py --from-ninja FILE (--git REV | --override PATH=EDITED...) : what a change to shared
files (headers, mostly) does to every translation unit that includes them. Each unit whose dependencies (ninja's deps
log) include a changed file is compiled with the oracle before and after the change, and the functions whose code
differs are listed with what moved: the inliner's decisions and costs, the stack frame, and, with the target asm,
how close the function is to the target before and after.

  --from-ninja FILE  the decomp project's build.ninja (flags, version, deps log, target asm)
  --git REV          the change: the working tree's files against REV's (`git diff --name-only REV`, in FILE's
                     directory). Before is REV's version of each changed file, after the working tree's
  --override P=R     the change: P (relative to FILE's directory) replaced by R after; repeatable. Before is the
                     checkout
  --unit TEXT        only the units whose source contains TEXT; repeatable
  --target DIR       the dtk build directory with the target asm (default: the build/<id> FILE names most)
  --jobs N           units compiled at once (default 4)
  --all              also the units with no function changed
  --json             the report as JSON

The similarity to the target is difflib's ratio over the instruction words, with frame offsets, relocated fields and
branch targets masked: not objdiff's score, but it moves the same way."""
import argparse
import difflib
import json
import re
import struct
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import oracle  # noqa: E402
import inline_diff  # noqa: E402


def object_functions(data):
    """{link name: instruction words} of the functions in an ELF32 big-endian object."""
    shoff, = struct.unpack_from(">I", data, 0x20)
    shentsize, shnum, _ = struct.unpack_from(">HHH", data, 0x2E)
    headers = [struct.unpack_from(">IIIIIIIIII", data, shoff + i * shentsize) for i in range(shnum)]
    out = {}
    for _, kind, _, _, offset, size, link, _, _, entsize in headers:
        if kind != 2:  # (SHT_SYMTAB)
            continue
        strings = headers[link][4]
        for at in range(offset, offset + size, entsize or 16):
            name_offset, value, symbol_size, info, _, index = struct.unpack_from(">IIIBBH", data, at)
            if info & 0xf == 2 and index < shnum and symbol_size:
                name = data[strings + name_offset:data.index(b"\0", strings + name_offset)].decode("latin-1")
                base = headers[index][4] + value
                out[name] = list(struct.unpack_from(f">{symbol_size // 4}I", data, base))
    return out


def dependents(root, changed):
    """{source (as its rule spells it): [the changed files it depends on]}, from ninja's deps log in ROOT."""
    text = subprocess.run(["ninja", "-t", "deps"], cwd=root, capture_output=True, text=True).stdout
    wanted = {str((root / c).resolve()).lower(): c for c in changed}
    out, deps = {}, []

    def close():
        if deps and deps[0].endswith((".c", ".cpp", ".cp")):
            hits = [wanted[d] for d in (str((root / x).resolve()).lower() for x in deps) if d in wanted]
            if hits:
                out[deps[0]] = sorted(set(hits))

    for line in text.splitlines():
        if line and not line[0].isspace():
            close()
            deps = []
        elif line.strip():
            deps.append(line.strip())
    close()
    return out


def similarity(ours, theirs):
    if not ours or not theirs:
        return None
    return difflib.SequenceMatcher(None, [oracle.instruction_token(w) for w in ours],
                                   [oracle.instruction_token(w) for w in theirs], autojunk=False).ratio()


def decisions(function):
    """{callee link name: (outcomes, costs)} of a --json function's inliner decisions."""
    out = {}
    for d in function.get("inlining", ()):
        if d.get("kind") == "decision":
            outcomes, costs = out.setdefault(d["link"], ({}, set()))
            outcomes[d["outcome"]] = outcomes.get(d["outcome"], 0) + d["count"]
            if "full_cost" in d or "cost" in d:
                costs.add(d.get("full_cost", d.get("cost")))
    return out


def frame(function):
    return {e["name"]: e.get("r1", e["offset"]) for e in function.get("frame") or ()
            if e["used"] and not e["reg"] and not (e["offset"] == 0 and e["name"].startswith("@"))}


def paired(names):
    """{key: name}: NAMES keyed without the compiler's local numbers (`__arraydtor$89`, `@12`), so a function
    renumbered by an unrelated change pairs with itself; the same key again gets its ordinal."""
    out, seen = {}, {}
    for name in names:
        key = re.sub(r"(\$|@)\d+", r"\1", name)
        seen[key] = seen.get(key, 0) + 1
        out[key if seen[key] == 1 else f"{key}#{seen[key]}"] = name
    return out


def compare(before, after, asm):
    """The functions whose code differs between BEFORE and AFTER (compile_trace results with their documents)."""
    ours = [object_functions(c["object"]) for c, _ in (before, after)]
    docs = [{f["link"]: f for f in d["functions"]} for _, d in (before, after)]
    keys = [paired(o) for o in ours]
    out = []
    for key in sorted(set(keys[0]) | set(keys[1])):
        la, lb = keys[0].get(key), keys[1].get(key)
        a, b = ours[0].get(la), ours[1].get(lb)
        if a == b:
            continue
        link = lb or la
        fa, fb = docs[0].get(la, {}), docs[1].get(lb, {})
        row = {"link": link, "function": fb.get("function") or fa.get("function") or link,
               "size": [4 * len(a) if a else None, 4 * len(b) if b else None]}
        if asm is not None:
            theirs = oracle.target_words(asm, link)
            if theirs:
                row["similarity"] = [similarity(a, theirs), similarity(b, theirs)]
        da, db = decisions(fa), decisions(fb)
        inlining = []
        for callee in sorted(set(da) | set(db)):
            if da.get(callee) != db.get(callee):
                inlining.append({"callee": callee, "before": da.get(callee), "after": db.get(callee)})
        row["inlining"] = inlining
        ra, rb = frame(fa), frame(fb)
        row["frame"] = [{"name": n, "before": ra.get(n), "after": rb.get(n)} for n in sorted(set(ra) | set(rb))
                        if ra.get(n) != rb.get(n)]
        sizes = [(f.get("layout") or {}).get("size") for f in (fa, fb)]
        if sizes[0] != sizes[1]:
            row["frame_size"] = sizes
        out.append(row)
    return out


def shown_decision(d):
    if d is None:
        return "no call"
    outcomes, costs = d
    text = ", ".join(f"{o} x{n}" for o, n in sorted(outcomes.items()))
    return text + (f" (cost {', '.join(map(str, sorted(costs)))})" if costs else "")


def report(unit, files, rows):
    out = [f"== {unit} (includes {', '.join(files)}): {len(rows)} function(s) changed"]
    for row in rows:
        line = f"   {row['function']}"
        if row["size"][0] is None or row["size"][1] is None:
            line += " (only " + ("after" if row["size"][0] is None else "before") + ")"
        elif row["size"][0] != row["size"][1]:
            line += f" ({row['size'][0]} -> {row['size'][1]} bytes)"
        s = row.get("similarity")
        if s and s[0] is not None and s[1] is not None:
            mark = "WORSE" if s[1] < s[0] else "better" if s[1] > s[0] else "same"
            line += f": similarity to the target {100 * s[0]:.2f}% -> {100 * s[1]:.2f}% ({mark})"
        out.append(line)
        for d in row["inlining"]:
            out.append(f"      inline {d['callee']}: {shown_decision(d['before'])} -> {shown_decision(d['after'])}")
        if "frame_size" in row:
            out.append(f"      frame {row['frame_size'][0]} -> {row['frame_size'][1]} bytes")
        for f in row["frame"][:8]:
            def at(x):
                return "-" if x is None else f"r1+{x:#x}"
            out.append(f"      slot {f['name']}: {at(f['before'])} -> {at(f['after'])}")
        if len(row["frame"]) > 8:
            out.append(f"      ... {len(row['frame']) - 8} more slots moved")
        if not row["inlining"] and not row["frame"] and "frame_size" not in row:
            out.append("      (same inlining and frame: registers or scheduling)")
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--from-ninja", required=True)
    parser.add_argument("--git")
    parser.add_argument("--override", action="append", default=[])
    parser.add_argument("--unit", action="append", default=[])
    parser.add_argument("--target")
    parser.add_argument("--version")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--json", action="store_true")
    arguments = parser.parse_args()
    root = Path(arguments.from_ninja).resolve().parent
    if bool(arguments.git) == bool(arguments.override):
        raise oracle.OracleError("give the change: --git REV, or --override PATH=EDITED")
    directory = Path(arguments.target) if arguments.target else inline_diff.target_directory(arguments.from_ninja)
    with tempfile.TemporaryDirectory() as temp:
        before, after = [], list(arguments.override)
        if arguments.git:
            names = subprocess.run(["git", "diff", "--name-only", arguments.git, "--"], cwd=root,
                                   capture_output=True, text=True, check=True).stdout.split()
            changed = []
            for k, name in enumerate(names):
                old = subprocess.run(["git", "show", f"{arguments.git}:{name}"], cwd=root, capture_output=True)
                if old.returncode:
                    continue  # (new at REV..: no unit included it before)
                copy = Path(temp) / f"before{k}" / Path(name).name
                copy.parent.mkdir(parents=True)
                copy.write_bytes(old.stdout)
                before.append(f"{name}={copy}")
                changed.append(name)
        else:
            changed = [item.split("=", 1)[0] for item in arguments.override]
        units = dependents(root, changed)
        units = {u: f for u, f in units.items() if not arguments.unit or any(t in u for t in arguments.unit)}
        if not arguments.json:
            print(f"{len(changed)} changed file(s); {len(units)} unit(s) include them", flush=True)

        def run(unit):
            def compiled(overrides):
                c = oracle.compile_trace(unit, (), arguments.from_ninja, arguments.version,
                                         [o for o in overrides if o.split("=", 1)[0] in units[unit]
                                          or Path(o.split("=", 1)[0]).as_posix() == Path(unit).as_posix()],
                                         check=False, full_cost=True, original_fallback=True)
                return c, oracle.document_for(c)[0]
            try:
                return unit, compare(compiled(before), compiled(after), inline_diff.target_asm(directory, unit)), None
            except oracle.OracleError as error:
                return unit, None, str(error)

        results = []
        with ThreadPoolExecutor(max(1, arguments.jobs)) as pool:
            for unit, rows, error in pool.map(run, sorted(units)):
                results.append({"unit": unit, "files": units[unit], "functions": rows, "error": error})
                if arguments.json:
                    continue
                if error:
                    print(f"== {unit}: ERROR {error}", flush=True)
                elif rows or arguments.all:
                    print("\n".join(report(unit, units[unit], rows)), flush=True)
    if arguments.json:
        print(json.dumps({"schema_version": oracle.SCHEMA_VERSION, "changed": changed, "units": results}, indent=1,
                         default=sorted))
        return
    worse = [(r["unit"], f["function"]) for r in results for f in r["functions"] or ()
             if f.get("similarity") and None not in f["similarity"] and f["similarity"][1] < f["similarity"][0]]
    touched = sum(1 for r in results if r["functions"])
    print(f"{touched} of {len(results)} unit(s) changed; {len(worse)} function(s) further from the target"
          + "".join(f"\n   {u}: {f}" for u, f in worse))


if __name__ == "__main__":
    try:
        main()
    except oracle.OracleError as error:
        sys.exit(str(error))
