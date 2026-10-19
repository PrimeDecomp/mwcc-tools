#!/usr/bin/env python3
"""Trace a unit with the WebAssembly oracle, with its build.ninja flags, and print the compiler's views
(the `mwcc_tools` package's `oracle.trace`).

    python scripts/oracle-run.py --unit main/Kyoto/Particles/CElementGen --locals --schedule
    python scripts/oracle-run.py --unit main/MetroidPrime/CRagDoll --candidate cand.cpp --blocks --pcode
    python scripts/oracle-run.py --unit main/Kyoto/Math/CAABox --function GetPoint__6CAABoxCFi --json > trace.json

Without a view option it prints each function's inlining, allocation and frame (with `--pcode` and `--schedule`
on request). A candidate (or `--file PATH=FILE`) is compiled in place of the checkout's file, which is never
changed. The last line says whether the oracle's object equals the build's: read it before trusting an
explanation (a candidate is not compared). See .agents/docs/oracle-native.md.
"""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _mwcc_tools_path import add_file_options, engine, files, load, run  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('-u', '--unit', required=True)
    add_file_options(parser)
    parser.add_argument('--inline-max-size', type=int, help="in place of the flags' inline_max_size")
    parser.add_argument('-f', '--function', help='only this function (mangled, demangled or short name)')
    views = parser.add_argument_group('views (one of near, instantiation, cse/ir, blocks and locals)')
    views.add_argument('--near', type=int, metavar='N', help='inlining decisions within N cost units of the limit')
    views.add_argument('--instantiation', metavar='TEXT', help='instantiations of templates whose names have TEXT')
    views.add_argument('--cse', action='store_true', help="the common subexpression pass")
    views.add_argument('--ir', metavar='PASS', help="the common subexpression pass's IR after PASS")
    views.add_argument('--blocks', action='store_true')
    views.add_argument('--locals', action='store_true')
    views.add_argument('--exchange', metavar='A,B', help='what an exchange of two registers would change')
    views.add_argument('--inline-cost', metavar='TEXT', help='inlining decisions about callees whose names have TEXT')
    views.add_argument('--pcode', action='store_true')
    views.add_argument('--schedule', action='store_true')
    views.add_argument('--raw', action='store_true', help="the trace's raw lines")
    parser.add_argument('--json', action='store_true', help='print the result (the schema-6 document) as JSON')
    parser.add_argument('--trace', type=Path, metavar='PATH', help='also write the trace (and the object) here')
    args = parser.parse_args()

    def body():
        tools = load()
        with engine() as session:
            unit = tools.Unit(session, args.unit)
            params = dict(handle=unit.handle, files=files(args, unit))
            if args.inline_max_size is not None:
                params['inline_max_size'] = args.inline_max_size
            if args.function:
                params['function'] = args.function
            if args.trace:
                params['trace_path'] = str(args.trace.resolve())
            if not args.json:
                chosen = {key: getattr(args, key) for key in ('near', 'instantiation', 'ir', 'exchange', 'inline_cost')
                          if getattr(args, key) is not None}
                chosen.update({key: True for key in ('cse', 'blocks', 'locals', 'pcode', 'schedule', 'raw')
                               if getattr(args, key)})
                params['views'] = dict(chosen, text=True)
            result = session.call_one('oracle.trace', **params)
        if args.json:
            print(json.dumps(result, indent=1))
            return
        print('\n'.join(result.get('text') or []))
        identical = result.get('identical')
        if identical is None:
            print("check: not compared with the build's object (a candidate, another inline_max_size, or no build)")
        elif identical:
            print("check: the oracle's object is identical to the build's")
        else:
            reason = '; '.join(result.get('differences') or []) or 'it differs'
            print(f"check: the oracle's object is NOT the build's ({reason}): rebuild the unit, or the source "
                  "changed since the build")

    run(body)


if __name__ == '__main__':
    main()
