#!/usr/bin/env python3
"""Triage nonmatching functions by the kind of residual left, to choose the tool for each.

For every nonmatching function of the selected units, classifies the rows objdiff marks as differing:

  register   same opcode, operands differ only in registers   -> scripts/oracle-regdiff.py, reggie regswap
  stack      same opcode, r1-relative offsets differ           -> local/temporary declaration order
  other      opcode, length, branch or relocation differences  -> source structure / inlining / types

and reports per function: match %, size, rows of each kind, and a verdict (register-only, stack-only,
register+stack, structural). Missing target functions (no counterpart in our object) are listed too.

The current sources are compiled in memory (the `mwcc_tools` package's `unit.residuals`), so a stale build
object is never read; `--built` scores the objects as built instead.

Examples:
  python scripts/residual-scan.py -u main/MetroidPrime/CRagDoll
  python scripts/residual-scan.py --below 95 --only register-only      # every TU below 95% fuzzy
  python scripts/residual-scan.py --units-file list.txt --json

Requires mwcc-tools (MWCC_ORACLE_ROOT); see EXTERNALS.md.
"""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _mwcc_tools_path import engine, load, run  # noqa: E402

VERDICTS = ['register-only', 'stack-only', 'register+stack', 'structural', 'missing']


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('-u', '--unit', action='append', default=[], help='objdiff unit name (repeatable)')
    parser.add_argument('--units-file', type=Path, help='file with one unit name per line (or NAME after a tab)')
    parser.add_argument('--below', type=float, help='every unit whose fuzzy match in report.json is below this')
    parser.add_argument('--only', choices=VERDICTS, help='show only this verdict')
    parser.add_argument('--built', action='store_true', help='score the objects as built, not the sources')
    parser.add_argument('--json', action='store_true')
    parser.add_argument('--jobs', type=int, default=8)
    args = parser.parse_args()
    units = list(args.unit)
    if args.units_file:
        units += [line.split('\t')[0 if '\t' not in line else 1].strip()
                  for line in args.units_file.read_text().splitlines()
                  if line.strip() and not line.startswith('#')]
    if not units and args.below is None:
        parser.error('give -u, --units-file or --below')

    def body():
        tools = load()
        params = dict(built=args.built, jobs=args.jobs)
        if units:
            params['units'] = list(dict.fromkeys(units))
        if args.below is not None:
            params['below'] = args.below
        if args.only:
            params['only'] = args.only
        with engine() as session:
            result = session.call_one('unit.residuals', **params)
        if args.json:
            print(json.dumps(result['rows'], indent=1))
            for failure in result.get('failed', []):
                print(f"{failure['unit']}: {failure['error']}", file=sys.stderr)
            return
        print('\n'.join(tools.reports.residuals(result)))

    run(body)


if __name__ == '__main__':
    main()
