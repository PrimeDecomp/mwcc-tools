#!/usr/bin/env python3
"""python tools/inline_diff.py [options] A B: why one translation unit inlines a call another keeps. Both are compiled
with the oracle (--full-cost), and for every callee (by link name) both decide on, each one's decisions are printed
side by side: inlined, kept or kept by the inline depth limit, the compiler's estimate and the whole cost, the rounds
(the inline depth), the functions, the inlined bodies the call is in and the calls at the depth limit.

  --from-ninja FILE  the build.ninja that builds A and B (their flags and version; else flags after --)
  --version V        the compiler version (default: the rule's, else GC_1_3_2)
  --override P=R     as tools/oracle.py's, for both; repeatable
  --callee TEXT      only the callees whose link name contains TEXT
  --all              every shared callee, not only those decided differently
  --json             the comparison as JSON"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import oracle  # noqa: E402


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
    return {"outcomes": outcomes, "costs": costs, "full_costs": whole,
            "rounds": sorted({r for d in decisions for r in d.get("depths", [])}),
            "functions": sorted({d["function"] for d in decisions}),
            "reasons": sorted({d["reason"] for d in decisions if "cost" not in d}),
            "chains": sorted({tuple(d.get("chain", ())) for d in decisions if d.get("chain")}),
            "limit_paths": sorted({tuple(p) for d in decisions for p in d.get("limit_paths", ())})}


def span(values):
    if not values:
        return "-"
    return str(values[0]) if len(values) == 1 else f"{values[0]}..{values[-1]}"


def report(name, s):
    outcomes = ", ".join(f"{k} x{v}" for k, v in sorted(s["outcomes"].items()))
    out = [f"   {name}: {outcomes}; estimate {span(s['costs'])}, whole cost {span(s['full_costs'])}, "
           f"rounds {', '.join(map(str, s['rounds'])) or '-'}",
           f"      in {', '.join(s['functions'])}"]
    out.extend(f"      because: {r}" for r in s["reasons"])
    out.extend(f"      inside: {' > '.join(c)}" for c in s["chains"][:6])
    if len(s["chains"]) > 6:
        out.append(f"      ... {len(s['chains']) - 6} more chains")
    out.extend(f"      at the depth limit: {' > '.join(p)}  [limit +1]" for p in s["limit_paths"][:6])
    if len(s["limit_paths"]) > 6:
        out.append(f"      ... {len(s['limit_paths']) - 6} more calls at the depth limit")
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
    parser.add_argument("--json", action="store_true")
    argv = sys.argv[1:]
    flags = []
    if "--" in argv:
        flags = argv[argv.index("--") + 1:]
        argv = argv[:argv.index("--")]
    arguments = parser.parse_args(argv)
    documents = [oracle.analyze_source(source, flags, arguments.from_ninja, arguments.version, arguments.override,
                                       full_cost=True)
                 for source in (arguments.a, arguments.b)]
    a, b = (decisions_by_link(d) for d in documents)
    rows = []
    for link in sorted(set(a) & set(b)):
        if arguments.callee and arguments.callee not in link:
            continue
        sa, sb = summary(a[link]), summary(b[link])
        if not arguments.all and set(sa["outcomes"]) == set(sb["outcomes"]):
            continue
        rows.append({"link": link, "callee": a[link][0]["callee"], "a": sa, "b": sb})
    if arguments.json:
        print(json.dumps({"schema_version": oracle.SCHEMA_VERSION, "a": documents[0]["source"],
                          "b": documents[1]["source"], "callees": rows}, indent=1, default=list))
        return
    print(f"A: {documents[0]['source']}\nB: {documents[1]['source']}\n"
          f"{len(set(a) & set(b))} callees decided in both; {'all' if arguments.all else 'decided differently'}: "
          f"{len(rows)}")
    for row in rows:
        print(f"== {row['callee']}  [{row['link']}]")
        print("\n".join(report("A", row["a"])))
        print("\n".join(report("B", row["b"])))


if __name__ == "__main__":
    main()
