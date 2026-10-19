#!/usr/bin/env python3
"""Explain a function's register differences against the target with the oracle
(the `mwcc_tools` package's `oracle.regdiff`).

    python scripts/oracle-regdiff.py --unit main/Kyoto/Particles/CElementGen --function RenderParticles --pcode
    python scripts/oracle-regdiff.py --unit main/MetroidPrime/CRagDoll --function Update --candidate my.cpp --json

Each register-only difference is mapped to the oracle's virtual registers, their definitions and the reason for
their colours. The unit (or the candidate, or `--file PATH=FILE`) is compiled in memory and compared with the
target; `--json` gives the structured document. See .agents/docs/oracle-native.md.
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
    parser.add_argument('-f', '--function', required=True, help='mangled, demangled or short name')
    add_file_options(parser)
    parser.add_argument('--inline-max-size', type=int, help="in place of the flags' inline_max_size")
    parser.add_argument('--inlining', action='store_true', help="also the function's inlining")
    parser.add_argument('--pcode', action='store_true', help="also the function's pcode before allocation")
    parser.add_argument('--full', action='store_true', help='every vreg, not only the differing ones')
    parser.add_argument('--json', action='store_true', help="print regdiff's document as JSON")
    args = parser.parse_args()

    def body():
        tools = load()
        with engine() as session:
            unit = tools.Unit(session, args.unit)
            params = dict(handle=unit.handle, files=files(args, unit), function=args.function, text=not args.json,
                          inlining=args.inlining, pcode=args.pcode, full=args.full)
            if args.inline_max_size is not None:
                params['inline_max_size'] = args.inline_max_size
            result = session.call_one('oracle.regdiff', **params)
        if args.json:
            print(json.dumps(result['document'], indent=1))
        else:
            print('\n'.join(result.get('text') or []))

    run(body)


if __name__ == '__main__':
    main()
