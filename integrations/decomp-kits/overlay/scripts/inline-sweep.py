#!/usr/bin/env python3
"""Score units at a scratch `inline_max_size` without touching the configuration.

For each unit this compiles its source in memory with the configured build.ninja flags, scores it with
objdiff's policy (size-weighted function match), compiles again with `inline_max_size(N)` in place of the
flags' value, scores again and prints the per-function changes (the `mwcc_tools` package's `unit.inline_sweep`).
Nothing is written: the build object is never overwritten, so this need not be serialized with builds.

This only measures. A per-object `inline_max_size` pragma is a project-policy exception: use it only
with the user's approval, and bound the value with `oracle`'s inline_window before proposing it.
A source pragma overrides the command-line value this substitutes, so such a unit measures
nothing here (the report says so). A folder-wide value changes units that are not in the sweep; sweep every
unit of the folder.

    python scripts/inline-sweep.py 100 -u main/Collision/CSpatialPrimitive -u main/Collision/CMRay
    python scripts/inline-sweep.py 250 --units-file units.txt

Requires mwcc-tools (MWCC_ORACLE_ROOT); see EXTERNALS.md.
"""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _mwcc_tools_path import engine, load, run  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('size', type=int, help='scratch inline_max_size')
    parser.add_argument('-u', '--unit', action='append', default=[], help='objdiff unit name (repeatable)')
    parser.add_argument('--units-file', type=Path, help='file with one unit name per line')
    args = parser.parse_args()
    names = list(args.unit)
    if args.units_file:
        names += [line.strip() for line in args.units_file.read_text().splitlines() if line.strip()]
    if not names:
        parser.error('give units with -u or --units-file')

    def body():
        tools = load()
        with engine() as session:
            result = session.call_one('unit.inline_sweep', units=list(dict.fromkeys(names)), sizes=[args.size])
        print('\n'.join(tools.reports.inline_sweep(result)))

    run(body)


if __name__ == '__main__':
    main()
