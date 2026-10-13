#!/usr/bin/env python3
"""python tools/inline_window.py --from-ninja FILE UNIT... : the inline limits (inline_max_size) a translation unit's
target agrees with. Each UNIT (a source, or an objdiff.json unit name or its end) is compiled with the oracle
(--full-cost), and for every function the target also has, each callee's calls are counted in our object and in the
target asm. Where they agree, the costs bound the limit as it is; where the target makes fewer calls, it inlined
calls we keep, so the limit must be at least their cost; where it makes more, it kept calls we inline, so the limit
must be below theirs. The window is the range of limits that agrees with every such call. A window that leaves out
the current limit, or is empty, points to the build settings, not the source.

The bounds are first-order: inlining a call changes the costs of the calls around it, and calls the depth limit
decides, or with no body, are left out (listed). Calls that are not counted per call site (several calls of one
callee in a function, some inlined and some not) give the bound that holds for at least one of them.

  --from-ninja FILE  the build.ninja that builds the units (flags, version, and the target directory)
  --target DIR       the dtk build directory with the target asm (default: the build/<id> FILE names most)
  --version V        the compiler version (default: the rule's)
  --override P=R     as tools/oracle.py's; repeatable
  --verbose          every bound, not only those that set the window
  --json             the windows as JSON"""
import argparse
import json
import re
import struct
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import oracle  # noqa: E402
import inline_diff  # noqa: E402


def object_calls(data):
    """{function link name: Counter(callee link name)} of the calls (R_PPC_REL24 relocations) in an ELF32 big-endian
    object."""
    shoff, = struct.unpack_from(">I", data, 0x20)
    shentsize, shnum, _ = struct.unpack_from(">HHH", data, 0x2E)
    headers = [struct.unpack_from(">IIIIIIIIII", data, shoff + i * shentsize) for i in range(shnum)]
    symbols = []
    for _, kind, _, _, offset, size, link, _, _, entsize in headers:
        if kind == 2:  # (SHT_SYMTAB)
            strings = headers[link][4]
            for at in range(offset, offset + size, entsize or 16):
                name_offset, value, symbol_size, info, _, index = struct.unpack_from(">IIIBBH", data, at)
                name = data[strings + name_offset:data.index(b"\0", strings + name_offset)].decode("latin-1")
                symbols.append((name, value, symbol_size, info & 0xf, index))
    functions = [(index, value, value + size, name) for name, value, size, kind, index in symbols if kind == 2]
    out = {}
    for _, kind, _, _, offset, size, _, info, _, entsize in headers:
        if kind != 4:  # (SHT_RELA)
            continue
        for at in range(offset, offset + size, entsize or 12):
            where, rinfo, _ = struct.unpack_from(">IIi", data, at)
            if rinfo & 0xff != 10:  # (R_PPC_REL24: bl, b)
                continue
            callee = symbols[rinfo >> 8][0]
            for index, start, end, name in functions:
                if index == info and start <= where < end:
                    out.setdefault(name, Counter())[callee] += 1
                    break
    return out


def asm_calls(path):
    """{function link name: Counter(callee link name)} of the `bl` in a target asm file."""
    out, current = {}, None
    for line in path.read_text(errors="replace").splitlines():
        m = re.match(r'\s*\.(fn|endfn)\s+("[^"]*"|[^\s,]+)', line)
        if m:
            current = m.group(2).strip('"') if m.group(1) == "fn" else None
            if current:
                out.setdefault(current, Counter())
            continue
        if current:
            m = re.search(r'\bbl\s+("[^"]*"|[^\s"]+)', line.split("*/")[-1])
            if m:
                out[current][m.group(1).strip('"')] += 1
    return out


def bounds(document, ours, theirs):
    """The bounds the target's calls set on the inline limit: dicts with `function`, `callee`, `side` ("low": the
    limit is at least `value`; "high": at most `value`), `why`, `ours` and `target` (the call counts), and the
    calls left out (`side` None)."""
    out = []
    for function in document["functions"]:
        link = function["link"]
        if link not in theirs:
            continue
        decisions = {}
        for d in function["inlining"]:
            if d.get("kind") == "decision":
                decisions.setdefault(d["link"], []).append(d)
        for callee, ds in decisions.items():
            mine, target = ours.get(link, Counter())[callee], theirs[link][callee]
            costed = [d for d in ds if "cost" in d and not d.get("depth_limited")]
            inlined = [d.get("full_cost", d["cost"]) for d in costed if d["outcome"] == "inlined"]
            kept = [d.get("full_cost", d["cost"]) for d in costed if d["outcome"] != "inlined"]
            row = {"function": function["function"], "link": link, "callee": ds[0]["callee"], "callee_link": callee,
                   "ours": mine, "target": target, "threshold": next((d["threshold"] for d in costed), None)}
            if not costed:
                if mine != target:
                    reasons = sorted({d.get("reason", d["outcome"]) for d in ds})
                    out.append(dict(row, side=None, why="; ".join(reasons)))
                continue
            if mine == target:
                if inlined:
                    out.append(dict(row, side="low", value=max(inlined), why="inlined in both"))
                if kept and mine:
                    out.append(dict(row, side="high", value=min(kept) - 1, why="kept in both"))
            elif target < mine:
                if kept:
                    out.append(dict(row, side="low", value=min(kept) if target else max(kept),
                                    why="the target inlines " + ("all of them" if not target else
                                                                 f"{mine - target} more")
                                    + ", where ours keeps them"))
                else:
                    out.append(dict(row, side=None, why="the target makes fewer calls, but ours keeps none for cost"))
            else:
                if inlined:
                    out.append(dict(row, side="high", value=max(inlined) - 1,
                                    why=f"the target keeps {target - mine} more, where ours inlines them"))
                else:
                    out.append(dict(row, side=None, why="the target makes more calls, but ours inlines none for cost"))
    return out


def window(rows):
    lows = [r for r in rows if r["side"] == "low"]
    highs = [r for r in rows if r["side"] == "high"]
    low = max((r["value"] for r in lows), default=None)
    high = min((r["value"] for r in highs), default=None)
    return low, high, lows, highs


def describe(r):
    counts = f"calls: ours {r['ours']}, target {r['target']}"
    return f"{r['callee']} in {r['function']} ({counts}): {r['why']}"


def report(source, rows, verbose):
    low, high, lows, highs = window(rows)
    current = next((r["threshold"] for r in rows if r.get("threshold") is not None), None)
    shown = (f"{low if low is not None else '-'}..{high if high is not None else '-'}")
    out = [f"== {source}: limit {current}, the target agrees with {shown}"]
    if low is not None and high is not None and low > high:
        out.append("   EMPTY: no single limit agrees with every call: the source differs (or the limit differs per "
                   "function); the bounds that cross:")
    elif current is not None and ((low is not None and current < low) or (high is not None and current > high)):
        out.append(f"   the current limit {current} is outside it: the target's build likely used another limit for "
                   "this unit (a setting, not the source)")
    for side, group, key in (("at least", lows, lambda r: -r["value"]), ("at most", highs, lambda r: r["value"])):
        group = sorted(group, key=key)
        binding = group if verbose else [r for r in group if not r["why"].endswith("in both")][:6] or group[:2]
        for r in binding:
            out.append(f"   {side} {r['value']}: {describe(r)}")
    skipped = [r for r in rows if r["side"] is None]
    for r in skipped[:8 if not verbose else None]:
        out.append(f"   not a limit question: {describe(r)}")
    if len(skipped) > 8 and not verbose:
        out.append(f"   ... {len(skipped) - 8} more not a limit question (--verbose)")
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("units", nargs="+")
    parser.add_argument("--from-ninja", required=True)
    parser.add_argument("--target")
    parser.add_argument("--version")
    parser.add_argument("--override", action="append", default=[])
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--json", action="store_true")
    arguments = parser.parse_args()
    directory = Path(arguments.target) if arguments.target else inline_diff.target_directory(arguments.from_ninja)
    results = []
    for unit in arguments.units:
        source = inline_diff.unit_source(arguments.from_ninja, unit)
        compiled = oracle.compile_trace(source, (), arguments.from_ninja, arguments.version, arguments.override,
                                       check=False, full_cost=True)
        document = oracle.document_for(compiled)[0]
        asm = inline_diff.target_asm(directory, document["source"])
        if asm is None:
            print(f"WARNING: no target asm for {document['source']} under {directory}", file=sys.stderr)
            continue
        rows = bounds(document, object_calls(compiled["object"]), asm_calls(asm))
        low, high, _, _ = window(rows)
        results.append({"source": document["source"], "low": low, "high": high, "bounds": rows})
        if not arguments.json:
            print("\n".join(report(document["source"], rows, arguments.verbose)))
    if arguments.json:
        print(json.dumps({"schema_version": oracle.SCHEMA_VERSION, "units": results}, indent=1))


if __name__ == "__main__":
    try:
        main()
    except oracle.OracleError as error:
        sys.exit(str(error))
