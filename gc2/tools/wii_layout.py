#!/usr/bin/env python3
"""Lay out a Wii mwcceppc.exe from its CodeView: functions, symbols and units.

The Wii compilers are not versions of this project (include/version.h): their layouts are written to
research/<VERSION>/, which neither build reads. See ../../docs/wii-retro-compiler-research.md.
"""

import argparse
import bisect
import collections
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import formats, project, wii
from mw.names import c_name

SOURCE_DIRS = ("backend", "frontend", "optimizer", "driver", "runtime", "msl")


def gc2_sources() -> dict[str, str]:
    """A module name -> this project's source of it, for the modules that are already here."""
    out = {}
    for d in SOURCE_DIRS:
        for p in sorted((project.ROOT / "src" / d).glob("*.c*")):
            out.setdefault(p.stem, f"src/{d}/{p.name}")
    return out


def unit_name(module: str, sources: dict[str, str]) -> str:
    return sources.get(module, f"wii/{module}")


def layout(exe: Path):
    img = wii.read(exe)
    sources = gc2_sources()
    names = img.names()
    publics = {p.address: p for p in img.publics}
    statics = set(img.procedures) - {p.address for p in img.publics}

    functions, symbols, used = [], [], collections.Counter()
    for c in img.functions():
        name = names.get(c.address) or f"fn_{c.address:08x}"
        used[name] += 1
        if used[name] > 1:
            name = f"{name}_{c.address:08x}"
        unit = unit_name(c.module, sources)
        functions.append({"name": name, "address": f"0x{c.address:08x}", "size": c.size, "module": c.module, "unit": unit})
        public = publics.get(c.address)
        linker = public.name if public else name
        scope = "local" if c.address in statics else "global"
        symbols.append(
            formats.Symbol(linker, ".text", c.address, [("type", "function"), ("size", f"0x{c.size:X}"), ("scope", scope)])
        )
    for p in img.publics:
        if not p.code:
            symbols.append(formats.Symbol(p.name, p.section, p.address, [("type", "object"), ("scope", "global")]))
    symbols.sort(key=lambda s: s.address)

    # Units: one per module. A module's contributions to a section are merged into pieces where nothing of
    # another module lies between them; splits.txt takes one range per section, the largest piece, and
    # units.json lists the others.
    pieces = collections.defaultdict(lambda: collections.defaultdict(list))
    order, owner = [], []
    for c in img.contributions:
        if c.module and c.size:
            owner.append((c.address, c.module))
    owner.sort()
    starts = [a for a, _ in owner]
    for c in img.contributions:
        if not c.module or c.size == 0:
            continue
        if c.module not in pieces:
            order.append(c.module)
        ps = pieces[c.module][c.section]
        if ps:
            a, b = ps[-1]
            between = owner[bisect.bisect_left(starts, b) : bisect.bisect_left(starts, c.address)]
            if all(m == c.module for _, m in between):
                ps[-1] = [a, max(b, c.address + c.size)]
                continue
        ps.append([c.address, c.address + c.size])
    scattered, units = [], []
    for m in order:
        rs = []
        for s, ps in pieces[m].items():
            a, b = max(ps, key=lambda p: p[1] - p[0])
            rs.append(formats.Range(s, a, b))
            scattered += [(m, s, x, y) for x, y in ps if (x, y) != (a, b)]
        units.append(formats.Unit(unit_name(m, sources), rs))
    sections = {s.name: [] for s in img.pe.sections if s.name != ".reloc"}
    return img, functions, symbols, formats.Splits(sections, units), scattered, sources


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("version", help="the layout's name (its directory under research/), e.g. Wii_1_0RC1")
    parser.add_argument("exe", help="the Wii mwcceppc.exe, e.g. build/compilers/Wii/1.0RC1/mwcceppc.exe")
    parser.add_argument("--write", action="store_true", help="write research/VERSION/")
    args = parser.parse_args()
    exe = Path(args.exe)
    if not exe.is_absolute():
        exe = project.ROOT.parent / exe
    img, functions, symbols, splits, scattered, sources = layout(exe)

    named = sum(1 for f in functions if not f["name"].startswith("fn_"))
    known = {u.name for u in splits.units if not u.name.startswith("wii/")}
    print(f"{len(functions)} functions ({named} named), {len(splits.units)} units ({len(known)} with a source here)")
    print(f"{len(scattered)} pieces of a unit outside its splits.txt ranges (units.json)")
    if not args.write:
        return
    out = project.ROOT / "research" / args.version
    out.mkdir(parents=True, exist_ok=True)
    (out / "functions.json").write_text(json.dumps(functions, indent=1) + "\n")
    (out / "symbols.txt").write_text(formats.format_symbols(symbols))
    (out / "splits.txt").write_text(formats.format_splits(splits))
    (out / "units.json").write_text(
        json.dumps(
            {
                "modules": [
                    {"module": u_module, "unit": unit_name(u_module, sources)}
                    for u_module in dict.fromkeys(f["module"] for f in functions)
                ],
                "scattered": [
                    {"module": m, "section": s, "start": f"0x{a:08x}", "end": f"0x{b:08x}"} for m, s, a, b in scattered
                ],
            },
            indent=1,
        )
        + "\n"
    )
    print(f"wrote {project.relative(out)}")


if __name__ == "__main__":
    main()
