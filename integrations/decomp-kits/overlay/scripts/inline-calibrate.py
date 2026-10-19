#!/usr/bin/env python3
"""Compare the inliner's decisions in our build with what the target implies
(the `mwcc_tools` package's `inline.calibrate`).

    python scripts/inline-calibrate.py --callee destroy_impl --unit main/Kyoto/Particles/CElementGen --breakdown
    python scripts/inline-calibrate.py --callee destroy_impl --file include/Kyoto/TToken.hpp=/path/to/TToken.hpp

It lists the instances whose decision disagrees with the target (one only the target has, or only ours) and
prints nothing when all agree; `--near N` also lists decisions within N of the threshold. Thresholds come from the
oracle's individual decisions. Without `--unit` it compiles the units with an instance only the target has;
`--binary` compares each instance across the whole binary; `--file` compiles every unit with a replacement header
without touching the checkout. See .agents/docs/oracle-native.md.
"""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _mwcc_tools_path import engine, run  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--callee', required=True, help='its name before the template arguments, e.g. rstl::vector')
    parser.add_argument('-u', '--unit', action='append', default=[], help='a unit to compile (repeatable)')
    parser.add_argument('--file', action='append', default=[], metavar='PATH=FILE',
                        help='read FILE in place of PATH in every unit (a header); repeatable')
    parser.add_argument('--near', type=int, help='how close to the limit a decision counts as near (default 3)')
    parser.add_argument('--binary', action='store_true', help='each instance across the binary')
    parser.add_argument('--breakdown', action='store_true', help="each row's cost terms")
    parser.add_argument('--jobs', type=int, help='units compiled at once (default 6)')
    parser.add_argument('--json', type=Path, metavar='PATH', help='also write the result as JSON here')
    args = parser.parse_args()

    def body():
        overlay = {}
        for item in args.file:
            path, _, file = item.partition('=')
            if not path or not file:
                raise SystemExit(f'--file needs PATH=FILE, not {item!r}')
            overlay[Path(path).as_posix()] = Path(file).read_text(encoding='utf-8')
        params = dict(callee=args.callee, files=overlay, binary=args.binary, breakdown=args.breakdown, text=True)
        if args.unit:
            params['units'] = args.unit
        for key in ('near', 'jobs'):
            if getattr(args, key) is not None:
                params[key] = getattr(args, key)
        with engine() as session:
            result = session.call_one('inline.calibrate', **params)
        text = result.pop('text', None) or []
        if text:
            print('\n'.join(text))
        if args.json:
            args.json.write_text(json.dumps(result, indent=1) + '\n', encoding='utf-8')

    run(body)


if __name__ == '__main__':
    main()
