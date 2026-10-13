#!/usr/bin/env python3
"""python tools/inline_diff.py [options] A B: why one translation unit inlines a call another keeps. Both are compiled
with the oracle (--full-cost), and for every callee (by link name) both decide on, each one's decisions are printed
side by side: inlined, kept or kept by the inline depth limit, the compiler's estimate and the whole cost, the rounds
(the inline depth), the functions, the inlined bodies the call is in, the calls at the depth limit and the sensitive
terms (calls costed as their body, or as a call for want of one, that would flip the decision or move it by 1 or 2).

  --from-ninja FILE  the build.ninja that builds A and B (their flags and version; else flags after --)
  --version V        the compiler version (default: the rule's, else GC_1_3_2)
  --override P=R     as tools/oracle.py's, for both; repeatable
  --callee TEXT      only the callees whose link name contains TEXT
  --all              every shared callee, not only those decided differently
  --target [DIR]     check against the target: the functions each TU emits (and why an inline one is) against the
                     `.fn LINK` lines of its target asm (DIR/**/asm/<source without src/>.s; by default the
                     build/<id> directory --from-ninja's build.ninja names most). Shows the callees emitted in one
                     and not the other (with --all, every callee, with its target state), then each TU's functions
                     emitted differently (a weak function another TU of the module holds is not a difference)
  --json             the comparison as JSON"""
import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import oracle  # noqa: E402

SHOWN = 6


def decisions_by_link(document):
    """{callee link name: [the decisions about it, each with the function it is in]}."""
    out = {}
    groups = [(f"{f['function']} ({f['link']})", f["inlining"]) for f in document["functions"]]
    groups.append(("(after the last function)", document.get("inlining_after_last_function", [])))
    for function, decisions in groups:
        for d in decisions:
            if d.get("kind") == "decision":
                out.setdefault(d["link"], []).append(dict(d, function=function))
    return out


def summary(decisions):
    """One TU's decisions about one callee, together."""
    outcomes = {}
    for d in decisions:
        outcomes[d["outcome"]] = outcomes.get(d["outcome"], 0) + d["count"]
    costs = sorted({d["cost"] for d in decisions if "cost" in d})
    whole = sorted({d["full_cost"] for d in decisions if "full_cost" in d})
    terms = {}
    for d in decisions:
        for s in d.get("sensitivity", ()):
            key = (tuple(s["path"]), s["kind"], s["delta"])
            terms[key] = dict(s, flips=s["flips"] or terms.get(key, {}).get("flips", False))
    return {"outcomes": outcomes, "costs": costs, "full_costs": whole,
            "rounds": sorted({r for d in decisions for r in d.get("depths", [])}),
            "functions": sorted({d["function"] for d in decisions}),
            "reasons": sorted({d["reason"] for d in decisions if "cost" not in d}),
            "chains": sorted({tuple(d.get("chain", ())) for d in decisions if d.get("chain")}),
            "limit_paths": sorted({tuple(p) for d in decisions for p in d.get("limit_paths", ())}),
            "sensitivity": sorted(terms.values(), key=lambda s: (not s["flips"], abs(s["delta"]), s["path"]))}


def span(values):
    if not values:
        return "-"
    return str(values[0]) if len(values) == 1 else f"{values[0]}..{values[-1]}"


def section(title, lines, more):
    if not lines:
        return []
    shown = lines[:SHOWN * 2]
    out = [f"      {title}:"] + [f"         {line}" for line in shown]
    if len(lines) > len(shown):
        out.append(f"         ... {len(lines) - len(shown)} more {more}")
    return out


def report(name, s, target=None):
    outcomes = ", ".join(f"{k} x{v}" for k, v in sorted(s["outcomes"].items()))
    out = [f"   {name}: {outcomes}; estimate {span(s['costs'])}, whole cost {span(s['full_costs'])}, "
           f"rounds {', '.join(map(str, s['rounds'])) or '-'}" + (f"   {target}" if target else ""),
           f"      in {', '.join(s['functions'])}"]
    out.extend(f"      because: {r}" for r in s["reasons"])
    out.extend(section("inside", oracle.tree(s["chains"]), "lines"))
    out.extend(section("at the depth limit [limit +1]", oracle.tree(s["limit_paths"]), "lines"))
    out.extend(section("sensitive", [oracle.sensitivity_text(t) for t in s["sensitivity"]], "terms"))
    return out


def target_directory(from_ninja):
    """The build/<id> directory the build.ninja names most."""
    root = Path(from_ninja).resolve().parent
    ids = Counter(re.findall(r"build/(\w+)/", Path(from_ninja).read_text()))
    for build_id, _ in ids.most_common():
        if build_id != "compilers" and (root / "build" / build_id).is_dir():
            return root / "build" / build_id
    raise oracle.OracleError(f"no target build directory named in {from_ninja}; give --target DIR")


def target_functions(directory, source):
    """{link name: binding} of the functions in the target asm of SOURCE (as the rule spells it), or None."""
    relative = re.sub(r"^src/", "", source.replace("\\", "/"))
    relative = str(Path(relative).with_suffix(".s")).replace("\\", "/")
    found = sorted(Path(directory).glob(f"asm/{relative}")) + sorted(Path(directory).glob(f"*/asm/{relative}"))
    if not found:
        return None
    out = asm_functions(found[0])
    # (the linker keeps one copy of a weak function per module, in one TU: the module's other TUs say where)
    module = Path(str(found[0])[:str(found[0]).replace("\\", "/").rindex("/asm/") + 4])
    out[MODULE] = module_functions(module, found[0])
    # (the main program's linker strips the weak functions nothing references; a module's keeps them)
    out[STRIPPED] = module == Path(directory) / "asm"
    return out


MODULE = " module"
STRIPPED = " stripped"


def asm_functions(path):
    out = {}
    for line in path.read_text(errors="replace").splitlines():
        m = re.match(r'\s*\.fn\s+("[^"]*"|\S+),\s*(\w+)', line)
        if m:
            out[m.group(1).strip('"')] = m.group(2)
    return out


def module_functions(root, own, cache={}):
    """{link name: the asm file (under ROOT, the module's asm directory) of another TU that has it}."""
    if root not in cache:
        index = {}
        for path in sorted(root.rglob("*.s")):
            for link in asm_functions(path):
                index.setdefault(link, path)
        cache[root] = index
    return {link: path for link, path in cache[root].items() if path != own}


def emission(document):
    """({link name: why} of the functions this TU emits (every function compiled; why: what referenced an inline one,
    from the instantiation trace, else ""), in the order compiled."""
    why = {}
    for e in document.get("instantiation", []):
        if e["event"] == "emit" and e["link"] not in why:
            why[e["link"]] = e.get("why", "referenced") + (f" (in {e['from']})" if e.get("from") else "")
    return {f["link"]: why.get(f["link"], "") for f in document["functions"]}


def prediction(link, functions, emitted):
    """(the target state, mispredicted) of a callee: whether this TU emits it (EMITTED, from emission) as the target
    does (FUNCTIONS, from target_functions)."""
    if functions is None:
        return None, False
    binding = functions.get(link)
    state = f"target: out of line ({binding})" if binding else "target: not emitted"
    if (link in emitted) == bool(binding):
        return state + ("; emitted here too" if binding else ""), False
    if link in emitted:
        elsewhere = functions[MODULE].get(link)
        if elsewhere is not None:
            return state + f" (the module keeps its one copy in {elsewhere.name}); emitted here too", False
        if functions[STRIPPED]:
            return state + " (the main program strips unreferenced weak functions); emitted here", False
        return (state + " (nor anywhere in the module); MISPREDICTED: emitted here"
                + (f", {emitted[link]}" if emitted[link] else "")), True
    return state + "; MISPREDICTED: not emitted here", True


def emission_report(name, document, functions, emitted, callee=None):
    """The functions emitted here and not in the target, and the inline ones (weak or local) the target emits and
    this TU does not."""
    module = functions[MODULE]
    extra = [l for l in emitted if l not in functions and l not in module and (not callee or callee in l)]
    elsewhere = [l for l in emitted if l not in functions and l in module and (not callee or callee in l)]
    missing = [l for l, b in functions.items()
               if l not in (MODULE, STRIPPED) and b != "global" and l not in emitted and (not callee or callee in l)]
    out = [f"== {name}: {document['source']}: emitted differently from the target: {len(extra)} here only (nowhere "
           f"in the module), {len(missing)} there only; {len(elsewhere)} here whose copy the module keeps in another "
           "TU"]
    if functions[STRIPPED] and extra:
        out.append("   (the main program's linker strips the weak functions nothing references: those here only may "
                   "be that)")
    out.extend(f"   here only: {l}" + (f"\n      {emitted[l]}" if emitted[l] else "") for l in extra)
    out.extend(f"   there only ({functions[l]}): {l}" for l in missing)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("a")
    parser.add_argument("b")
    parser.add_argument("--from-ninja")
    parser.add_argument("--version")
    parser.add_argument("--override", action="append", default=[])
    parser.add_argument("--callee")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--target", nargs="?", const="")
    parser.add_argument("--json", action="store_true")
    argv = sys.argv[1:]
    flags = []
    if "--" in argv:
        flags = argv[argv.index("--") + 1:]
        argv = argv[:argv.index("--")]
    arguments = parser.parse_args(argv)
    documents = [oracle.analyze_source(source, flags, arguments.from_ninja, arguments.version, arguments.override,
                                       full_cost=True, instantiation=arguments.target is not None)
                 for source in (arguments.a, arguments.b)]
    targets = [None, None]
    if arguments.target is not None:
        if not arguments.target and not arguments.from_ninja:
            raise oracle.OracleError("--target without a directory needs --from-ninja")
        directory = Path(arguments.target) if arguments.target else target_directory(arguments.from_ninja)
        targets = [target_functions(directory, d["source"]) for d in documents]
        for d, t in zip(documents, targets):
            if t is None:
                print(f"WARNING: no target asm for {d['source']} under {directory}", file=sys.stderr)
    emitted = [emission(d) for d in documents]
    a, b = (decisions_by_link(d) for d in documents)
    rows = []
    for link in sorted(set(a) & set(b)):
        if arguments.callee and arguments.callee not in link:
            continue
        sa, sb = summary(a[link]), summary(b[link])
        (ta, wrong_a), (tb, wrong_b) = (prediction(link, targets[0], emitted[0]),
                                        prediction(link, targets[1], emitted[1]))
        differ = set(sa["outcomes"]) != set(sb["outcomes"])
        if not arguments.all and not (wrong_a or wrong_b if arguments.target is not None else differ):
            continue
        for s, t, wrong in ((sa, ta, wrong_a), (sb, tb, wrong_b)):
            if t:
                s["target"], s["mispredicted"] = t, wrong
        rows.append({"link": link, "callee": a[link][0]["callee"], "a": sa, "b": sb})
    if arguments.json:
        out = {"schema_version": oracle.SCHEMA_VERSION, "a": documents[0]["source"], "b": documents[1]["source"],
               "callees": rows}
        if arguments.target is not None:
            out["emitted"] = {"a": emitted[0], "b": emitted[1]}
            out["target"] = {k: t and {l: b for l, b in t.items() if l not in (MODULE, STRIPPED)}
                             for k, t in zip("ab", targets)}
        print(json.dumps(out, indent=1, default=list))
        return
    which = ("all" if arguments.all else "mispredicted against the target" if arguments.target is not None
             else "decided differently")
    print(f"A: {documents[0]['source']}\nB: {documents[1]['source']}\n"
          f"{len(set(a) & set(b))} callees decided in both; {which}: {len(rows)}")
    for row in rows:
        print(f"== {row['callee']}  [{row['link']}]")
        print("\n".join(report("A", row["a"], row["a"].get("target"))))
        print("\n".join(report("B", row["b"], row["b"].get("target"))))
    for name, document, functions, e in zip("AB", documents, targets, emitted):
        if functions is not None:
            print("\n".join(emission_report(name, document, functions, e, arguments.callee)))


if __name__ == "__main__":
    try:
        main()
    except oracle.OracleError as error:
        sys.exit(str(error))
