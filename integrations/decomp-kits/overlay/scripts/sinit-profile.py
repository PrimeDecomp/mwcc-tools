#!/usr/bin/env python3
"""Block-profile one function (default: the `__sinit_` initializer) in each object kept by try-variants.

A function that is only a long run of stores (a static-table initializer such as __sinit_ScriptLoader_cpp)
can be 100% instruction-for-instruction and still differ in size, frame or saved registers, because the
compiler's block structure changed. This prints, per kept object, the instruction count, the `stmw`
register, the frame size, how many `lwz`/`mr` were emitted, and the lengths of the long store runs, so
variants can be ranked by the properties that matter without reading the disassembly
(the `mwcc_tools` package's `unit.sinit_profile`).

    python scripts/try-variants.py -u <unit> --variants v.txt --keep out/
    python scripts/sinit-profile.py out/ [TARGET.o] [--function __sinit_]

KEEPDIR is try-variants' --keep directory, or an experiment's run directory kept with `keep_objects` (its
control first, then each candidate by its index and name). TARGET.o is the original object
(build/<version>/obj/<unit>.o). Requires mwcc-tools (MWCC_ORACLE_ROOT); see EXTERNALS.md.
"""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _mwcc_tools_path import engine, load, run  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('keepdir', type=Path, help='directory written by try-variants --keep')
    parser.add_argument('target', nargs='?', type=Path, help='original object to profile as well')
    parser.add_argument('--function', default='__sinit_', help='symbol prefix of the function to profile')
    args = parser.parse_args()

    def body():
        tools = load()
        params = dict(directory=str(args.keepdir.resolve()), function=args.function)
        if args.target:
            params['target'] = str(args.target.resolve())
        with engine() as session:
            result = session.call_one('unit.sinit_profile', **params)
        print('\n'.join(tools.reports.sinit_profile(result)))

    run(body)


if __name__ == '__main__':
    main()
