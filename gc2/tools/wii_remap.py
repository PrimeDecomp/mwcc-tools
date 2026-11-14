#!/usr/bin/env python3
"""Map the functions of a laid-out mwcceppc.exe (FROM) onto another executable (TO).

Same approach as port_version.port (relocation-masked exact match, call-graph propagation, link order), but FROM
may be a research layout (research/X/functions.json + an exe path) or a config version (config/GC_2_7), and TO
needs no config. Nothing here is a version of this project; results go to research/.

    wii_remap.py --from research/Wii_1_0RC1 --from-exe build/compilers/Wii/1.0RC1/mwcceppc.exe \\
                 --to-exe build/compilers/GC/3.0a5.2/mwcceppc.exe --write research/GC_3_0a5_2

Paths are relative to gc2/ or to the repository root (mwcc-tools), or absolute. A FROM that is a config directory
(config/GC_2_7) needs only --from; its exe is config.json's "original".
"""

import argparse
import bisect
import collections
import difflib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import formats, project
import port_version
from port_version import K, Image

REPO = project.ROOT.parent


def resolve(p) -> Path:
    p = Path(p)
    if p.is_absolute():
        return p
    return project.ROOT / p if (project.ROOT / p).exists() or not (REPO / p).exists() else REPO / p


def load_from(layout: str, exe: str | None):
    d = resolve(layout)
    functions = json.loads((d / "functions.json").read_text())
    if exe is None:
        exe = json.loads((d / "config.json").read_text())["original"]
        exe = REPO / exe
    for f in functions:
        f.setdefault("module", Path(f["source"]).stem if f.get("source") else "")
        f.setdefault("unit", f.get("source", ""))
    functions.sort(key=lambda f: int(f["address"], 16))
    return resolve(exe), functions


PAD = (0x00, 0x90, 0xCC)  # padding between functions: zeros (GC 2.x), nops (3.0a5.2), int3


class PaddedImage(Image):
    """Image whose function-boundary discovery also knows nop/int3 padding (port_version's knows zeros only)."""

    def call_targets(self):
        import struct

        targets = set()
        t = self.text
        for i in range(len(t) - 5):
            if t[i] == 0xE8:
                d = struct.unpack_from("<i", t, i + 1)[0]
                if d == 0:
                    continue
                target = self.start + i + 5 + d
                if self.start < target < self.end and target % 16 == 0:
                    before = t[target - self.start - 3 : target - self.start]
                    if (before[2] in PAD and t[target - self.start] not in PAD) or before[2] == 0xC3 or before[0] == 0xC2:
                        targets.add(target)
        return targets

    def padded_starts(self):
        starts = set()
        t = self.text
        for address in range(self.start + 16, self.end, 16):
            i = address - self.start
            if t[i] in PAD or t[i - 1] not in PAD:
                continue
            j = i - 1
            while j > 0 and t[j] in PAD and i - j < 16:
                j -= 1
            if t[j] == 0xC3 or t[j - 1] == 0xC2 or t[j - 2] == 0xC2:
                starts.add(address)
        return starts

    def size_to(self, address, next_start):
        end = next_start - self.start
        i = address - self.start
        while end > i + 1 and self.text[end - 1] in PAD:
            end -= 1
        if end - i >= 2 and self.text[end - 2] == 0xC2:
            end += 1
        elif self.text[end - 1] == 0xC2:
            end += 2
        for fixup in range(self.start + end - 3, self.start + end):
            if fixup in self.relocs:
                end = fixup - self.start + 4
        return min(end, next_start - self.start) - i


def make_image(path: Path) -> Image:
    return PaddedImage(path)  # project.ROOT / absolute path is the absolute path


def aligned_votes(A, B, a, sa, b, sb):
    """Call-target correspondences read from the matching blocks of two functions that differ."""
    if not sa or not sb or sa * sb > 4_000_000:
        return []
    x = A.masked[a - A.start : a - A.start + sa]
    y = B.masked[b - B.start : b - B.start + sb]
    ra, rb = A.references(a, sa), B.references(b, sb)
    out = []
    for block in difflib.SequenceMatcher(None, x, y, autojunk=False).get_matching_blocks():
        for offset, (kind, target) in ra.items():
            if kind == "rel" and block.a <= offset and offset + 4 <= block.a + block.size:
                other = rb.get(block.b + offset - block.a)
                if other and other[0] == "rel":
                    out.append((target, other[1]))
    return out


def remap(A: Image, functions, B: Image, log=print):
    by_address = {int(f["address"], 16): f for f in functions}
    order = [int(f["address"], 16) for f in functions]
    position = {a: i for i, a in enumerate(order)}

    index = collections.defaultdict(list)
    for i in range(len(B.masked) - K):
        index[B.masked[i : i + K]].append(i)

    mapped, how = {}, {}

    # 1. exact masked match; bodies that occur several times on both sides are paired in order
    groups = collections.defaultdict(list)
    for a in order:
        size = by_address[a]["size"]
        if size >= K:
            groups[A.masked[a - A.start : a - A.start + size]].append(a)
    for body, members in groups.items():
        size = len(body)
        hits = [c for c in index.get(body[:K], []) if B.masked[c : c + size] == body]
        if len(hits) > 256:
            continue
        # drop overlapping hits (a body can repeat inside padding or itself)
        hits.sort()
        if len(hits) == len(members):
            for a, c in zip(members, hits):
                mapped[a] = B.start + c
                how[a] = "exact"

    claimed = set(mapped.values())
    starts_extra = B.call_targets() | B.padded_starts()

    def votes_from_exact():
        calls = collections.defaultdict(collections.Counter)
        for a, b in mapped.items():
            if how[a] != "exact":
                continue
            size = by_address[a]["size"]
            ra, rb = A.references(a, size), B.references(b, size)
            for offset, (kind, target) in ra.items():
                if kind == "rel" and offset in rb and rb[offset][0] == "rel":
                    calls[target][rb[offset][1]] += 1
        return calls

    exact_votes = votes_from_exact()
    aligned_cache = {}

    def estimate_sizes():
        starts = sorted(starts_extra | set(mapped.values()))
        est = {b: B.size_to(b, starts[i + 1] if i + 1 < len(starts) else B.end) for i, b in enumerate(starts)}
        return starts, est

    for rnd in range(8):
        before = len(mapped)
        # 2. call-graph propagation (exact pairs, then aligned votes of pairs that differ)
        starts, est = estimate_sizes()
        votes = collections.defaultdict(collections.Counter)
        for t, c in exact_votes.items():
            votes[t].update(c)
        for a, b in list(mapped.items()):
            if how[a] == "exact":
                continue
            key = (a, b)
            if key not in aligned_cache:
                aligned_cache[key] = aligned_votes(A, B, a, by_address[a]["size"], b, est.get(b, 0))
            for t, tb in aligned_cache[key]:
                votes[t][tb] += 1
        claimed = set(mapped.values())
        for target, vs in sorted(votes.items()):
            if target not in by_address or target in mapped:
                continue
            ((b, n),) = vs.most_common(1)
            if b in claimed or n * 2 <= sum(vs.values()) or not (B.start <= b < B.end):
                continue
            mapped[target] = b
            how[target] = "propagated"
            claimed.add(b)

        # 3. link order: unmapped functions between two anchors, when counts agree
        starts, est = estimate_sizes()
        anchors = [(a, mapped[a]) for a in order if a in mapped]
        for (a1, b1), (a2, b2) in zip(anchors, anchors[1:]):
            if b2 <= b1:
                continue
            gap = [a for a in order[position[a1] + 1 : position[a2]] if a not in mapped]
            if not gap:
                continue
            candidates = [c for c in starts[bisect.bisect_right(starts, b1) : bisect.bisect_left(starts, b2)] if c not in claimed]
            if len(candidates) == len(gap):
                for a, b in zip(gap, candidates):
                    mapped[a] = b
                    how[a] = "order"
                    claimed.add(b)
        log(f"  round {rnd}: {len(mapped)} mapped (+{len(mapped) - before})")
        if len(mapped) == before:
            break

    # 4. fuzzy: in a gap between anchors with different counts, monotone alignment by byte similarity
    starts, est = estimate_sizes()
    anchors = [(a, mapped[a]) for a in order if a in mapped]
    anchors = [(None, B.start)] + anchors + [(None, B.end)]
    fuzzy = 0
    for (a1, b1), (a2, b2) in zip(anchors, anchors[1:]):
        if b2 <= b1:
            continue
        lo = position[a1] + 1 if a1 is not None else 0
        hi = position[a2] if a2 is not None else len(order)
        gap = [a for a in order[lo:hi] if a not in mapped]
        cands = [c for c in starts[bisect.bisect_right(starts, b1 if a1 is not None else b1 - 1) : bisect.bisect_left(starts, b2)] if c not in claimed]
        if not gap or not cands or len(gap) * len(cands) > 900:
            continue
        score = {}
        for i, a in enumerate(gap):
            sa = by_address[a]["size"]
            x = A.masked[a - A.start : a - A.start + sa]
            for j, c in enumerate(cands):
                sb = est.get(c, 0)
                if not sa or not sb or min(sa, sb) * 2 < max(sa, sb) or sa * sb > 4_000_000 or sa < 24:
                    continue
                y = B.masked[c - B.start : c - B.start + sb]
                s = difflib.SequenceMatcher(None, x, y, autojunk=False)
                if s.real_quick_ratio() < 0.75 or s.quick_ratio() < 0.75:
                    continue
                r = s.ratio()
                if r >= 0.75:
                    score[i, j] = r
        if not score:
            continue
        # best monotone assignment (DP over gap x cands)
        n, m = len(gap), len(cands)
        best = [[0.0] * (m + 1) for _ in range(n + 1)]
        for i in range(n - 1, -1, -1):
            for j in range(m - 1, -1, -1):
                best[i][j] = max(best[i + 1][j], best[i][j + 1], score.get((i, j), -1) + best[i + 1][j + 1])
        i = j = 0
        while i < n and j < m:
            s = score.get((i, j))
            if s is not None and best[i][j] == s + best[i + 1][j + 1]:
                mapped[gap[i]] = cands[j]
                how[gap[i]] = "fuzzy"
                claimed.add(cands[j])
                fuzzy += 1
                i += 1
                j += 1
            elif best[i][j] == best[i + 1][j]:
                i += 1
            else:
                j += 1
    log(f"  fuzzy: +{fuzzy}")
    return mapped, how, by_address, order


def build_layout(A, B, mapped, how, by_address):
    """TO's functions: mapped ones with FROM's name/module/unit, the rest found by padded starts/call targets."""
    starts = sorted(B.call_targets() | B.padded_starts() | set(mapped.values()))
    inverse = {b: a for a, b in mapped.items()}
    out = []
    for i, b in enumerate(starts):
        nxt = starts[i + 1] if i + 1 < len(starts) else B.end
        if b in inverse:
            a = inverse[b]
            f = by_address[a]
            size = f["size"] if how[a] == "exact" else B.size_to(b, nxt)
            e = dict(name=f["name"], address=f"0x{b:08x}", size=size, module=f.get("module", ""), unit=f.get("unit", ""))
            e["how"] = how[a]
            e["from"] = f["address"]
            out.append(e)
        else:
            if out and int(out[-1]["address"], 16) + out[-1]["size"] > b:
                continue
            out.append(dict(name=f"fn_{b:08x}", address=f"0x{b:08x}", size=B.size_to(b, nxt), module="", unit="", how="new"))
    # a new start right after a function that falls through into it is part of that function
    try:
        import capstone  # noqa: F401

        merged = [out[0]]
        for e in out[1:]:
            p = merged[-1]
            ps = int(p["address"], 16)
            if e["how"] == "new" and int(e["address"], 16) - (ps + p["size"]) < 16 and port_version.falls_through(B, ps, p["size"]):
                merged[-1] = dict(p, size=int(e["address"], 16) + e["size"] - ps)
                continue
            merged.append(e)
        out = merged
    except ImportError:
        pass
    return out


def fill_modules(out):
    """A `new` function between two functions of one module belongs to it."""
    for i, e in enumerate(out):
        if e["module"]:
            continue
        j = i - 1
        while j >= 0 and not out[j]["module"]:
            j -= 1
        k = i + 1
        while k < len(out) and not out[k]["module"]:
            k += 1
        if j >= 0 and k < len(out) and out[j]["module"] == out[k]["module"]:
            e["module"], e["unit"] = out[j]["module"], out[j]["unit"]
            e["how"] = e["how"] + "+between"
    return out


def write_layout(out, dest: Path):
    dest.mkdir(parents=True, exist_ok=True)
    fields = ("name", "address", "size", "module", "unit", "how")
    (dest / "functions.json").write_text(json.dumps([{k: e[k] for k in fields} for e in out], indent=1) + "\n")
    symbols = [
        formats.Symbol(e["name"], ".text", int(e["address"], 16), [("type", "function"), ("size", f"0x{e['size']:X}"), ("scope", "global")])
        for e in out
    ]
    (dest / "symbols.txt").write_text(formats.format_symbols(symbols))
    # units: per module the largest run of consecutive functions (.text only)
    runs = collections.defaultdict(list)
    prev = None
    for e in out:
        m = e["module"]
        a, b = int(e["address"], 16), int(e["address"], 16) + e["size"]
        if m and prev == m:
            runs[m][-1][1] = b
        elif m:
            runs[m].append([a, b])
        prev = m or None
    units, unit_of = [], {e["module"]: e["unit"] for e in out if e["module"]}
    for m, rs in sorted(runs.items(), key=lambda kv: kv[1][0][0]):
        a, b = max(rs, key=lambda r: r[1] - r[0])
        units.append(formats.Unit(unit_of[m], [formats.Range(".text", a, b)]))
    (dest / "splits.txt").write_text(formats.format_splits(formats.Splits({".text": []}, units)))
    (dest / "units.json").write_text(
        json.dumps(
            {
                "modules": [{"module": m, "unit": unit_of[m]} for m in dict.fromkeys(e["module"] for e in out if e["module"])],
                "scattered": [
                    {"module": m, "section": ".text", "start": f"0x{a:08x}", "end": f"0x{b:08x}"}
                    for m, rs in runs.items()
                    for a, b in rs
                    if (a, b) != max(rs, key=lambda r: r[1] - r[0])
                ],
            },
            indent=1,
        )
        + "\n"
    )


def link_order(order, mapped):
    anchors = [mapped[a] for a in order if a in mapped]
    mono = sum(1 for x, y in zip(anchors, anchors[1:]) if y > x)
    return mono, max(len(anchors) - 1, 1)


def run(from_dir, from_exe, to_exe, log=print):
    exe, functions = load_from(from_dir, from_exe)
    A, B = make_image(exe), make_image(resolve(to_exe))
    mapped, how, by_address, order = remap(A, functions, B, log)
    kinds = collections.Counter(how.values())
    mono, n = link_order(order, mapped)
    log(f"{from_dir} -> {to_exe}: {len(mapped)}/{len(functions)} mapped ({100 * len(mapped) / len(functions):.1f}%): "
        + ", ".join(f"{k} {v}" for k, v in kinds.most_common()))
    log(f"  link order kept by {mono}/{n} consecutive mapped pairs")
    return A, B, mapped, how, by_address, order


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--from", dest="src", required=True, help="laid-out FROM directory (research/X or config/X)")
    ap.add_argument("--from-exe", help="FROM's exe (not needed for a config version)")
    ap.add_argument("--to-exe", required=True)
    ap.add_argument("--write", help="directory to write functions.json etc. (research/NAME)")
    ap.add_argument("--names-for", help="a TO layout dir (research/X): name its fn_ functions after FROM's (exact/propagated only)")
    ap.add_argument("--names-out", help="with --names-for: where to write {to_address: {name, how}}")
    args = ap.parse_args()
    A, B, mapped, how, by_address, order = run(args.src, args.from_exe, args.to_exe)
    if args.names_for:
        to = json.loads((resolve(args.names_for) / "functions.json").read_text())
        names = {}
        inverse = {b: a for a, b in mapped.items()}
        for f in to:
            if not f["name"].startswith("fn_"):
                continue
            a = inverse.get(int(f["address"], 16))
            if a is None or how[a] not in ("exact", "propagated"):
                continue
            src = by_address[a]["name"]
            if src.startswith("fn_") or src.startswith("_fn_"):
                continue
            names[f["address"]] = {"name": src, "how": how[a]}
        print(f"  names for fn_ functions of {args.names_for}: {len(names)} of {sum(f['name'].startswith('fn_') for f in to)}")
        if args.names_out:
            resolve(args.names_out).write_text(json.dumps(names, indent=1) + "\n")
        return
    out = fill_modules(build_layout(A, B, mapped, how, by_address))
    kinds = collections.Counter(e["how"] for e in out)
    print(f"  TO functions: {len(out)}: " + ", ".join(f"{k} {v}" for k, v in kinds.most_common()))
    if args.write:
        write_layout(out, resolve(args.write))


if __name__ == "__main__":
    main()
