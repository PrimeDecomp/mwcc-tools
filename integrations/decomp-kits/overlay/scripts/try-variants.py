#!/usr/bin/env python3
"""Compile source variants of one unit in parallel and score them against the target.

Each variant is the unit source with exact-text replacements applied (or a complete candidate file). Variants
compile in memory through mwcc-tools (the `mwcc_tools` package's `source.variants`), with the unit's own
build.ninja flags, so the build, the source file and other agents are untouched, and several run at once. The
unmodified source is compiled the same way first and its object compared with the build's byte for byte, so a
variant's score is comparable with the build.

Variants file format (exact text, whitespace included; several replacements per variant are allowed):

  ### name of the variant
  @@@ old
  text to replace (must occur exactly once in the source)
  @@@ new
  replacement text
  @@@ old
  ...

Examples:
  python scripts/try-variants.py -u main/MetroidPrime/CRagDoll -f Prime --variants v.txt
  python scripts/try-variants.py -u main/MetroidPrime/CRagDoll --candidate a.cpp --candidate b.cpp
  python scripts/try-variants.py -u main/MetroidPrime/CRagDoll -f Prime --variants v.txt --keep out/   # keep sources+objects

Reports, per variant: the selected function's match (and delta), the unit's size-weighted fuzzy match, and every
other function whose match changed (so a variant that helps one function but hurts another is visible).
Requires mwcc-tools (MWCC_ORACLE_ROOT); see EXTERNALS.md.
"""
import argparse
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _mwcc_tools_path import engine, load, run  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('-u', '--unit', required=True)
    parser.add_argument('-f', '--function', help='function to report first (mangled, demangled or short name)')
    parser.add_argument('--variants', type=Path, help='variants file (see the format above)')
    parser.add_argument('--candidate', type=Path, action='append', default=[], help='complete candidate source')
    parser.add_argument('--jobs', type=int, default=min(8, os.cpu_count() or 4))
    parser.add_argument('--keep', type=Path,
                        help='keep each variant source/object/console under this directory (a new one)')
    parser.add_argument('--apply', metavar='NAME',
                        help='after scoring, write this variant (name, or candidate path) into the source file')
    args = parser.parse_args()
    if not args.variants and not args.candidate:
        parser.error('give --variants and/or --candidate')

    def body():
        tools = load()
        params = dict(jobs=args.jobs)
        if args.variants:
            params['variants_path'] = str(args.variants.resolve())
        if args.candidate:
            params['candidates'] = [dict(name=str(p), path=str(p.resolve())) for p in args.candidate]
        for key in ('function', 'apply'):
            if getattr(args, key):
                params[key] = getattr(args, key)
        if args.keep:
            params['keep'] = str(args.keep.resolve())
        with engine() as session:
            unit = tools.Unit(session, args.unit)
            result = session.call_one('source.variants', handle=unit.handle, **params)
        print('\n'.join(tools.reports.try_variants(result)))

    run(body)


if __name__ == '__main__':
    main()
