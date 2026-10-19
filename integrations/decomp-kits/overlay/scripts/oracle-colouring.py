#!/usr/bin/env python3
"""What-if register colouring for a unit's function (the `mwcc_tools` package's `trace.*` methods).

    python scripts/oracle-colouring.py verify --unit main/Kyoto/Particles/CElementGen --function RenderParticles__11CElementGenFv --save base
    python scripts/oracle-colouring.py target --load base --function RenderParticles__11CElementGenFv
    python scripts/oracle-colouring.py replay --load base --function RenderParticles__11CElementGenFv --degree 35:+1
    python scripts/oracle-colouring.py search --load base --function RenderParticles__11CElementGenFv --seed 2
    python scripts/oracle-colouring.py remap --load base --load other --function RenderParticles__11CElementGenFv

The trace is the unit's (compiled in memory, through `--candidate`/`--file`), or one saved with `--save PREFIX`
and read back with `--load PREFIX` (PREFIX.trace, .o and .manifest.json). Commands: verify, target, force, search,
replay, remap. Run `verify` first: the others are only as good as the replay it checks. Spills are not modelled; a
graph edit is a hypothesis about the numbering, not evidence that particular source produces it.
See .agents/docs/oracle-native.md.
"""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _mwcc_tools_path import add_file_options, engine, files, load, run  # noqa: E402

COMMANDS = ['verify', 'target', 'force', 'search', 'replay', 'remap']


def numbers(text):
    return [int(v) for v in text.split(',') if v.strip()]


def edits(args):
    """The --order/--move/... options as trace.replay's edits."""
    out = []
    for text in args.order:
        out.append(['order', numbers(text)])
    for text in args.move:
        node, where, anchor = (text.split(':') + ['', ''])[:3]
        if where not in ('before', 'after'):
            raise SystemExit(f'--move needs V:before|after:W, not {text!r}')
        out.append(['move', int(node), where, int(anchor)])
    out += [['drop', int(v)] for v in args.drop]
    for kind in ('degree', 'neighbours'):
        for text in getattr(args, kind):
            node, _, delta = text.partition(':')
            if not delta:
                raise SystemExit(f'--{kind} needs V:+1, not {text!r}')
            out.append([kind, int(node), int(delta)])
    for kind in ('unlink', 'link'):
        for text in getattr(args, kind):
            pair = numbers(text)
            if len(pair) != 2:
                raise SystemExit(f'--{kind} needs A,B, not {text!r}')
            out.append([kind, *pair])
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('command', choices=COMMANDS)
    parser.add_argument('-u', '--unit', help="the unit to trace (or --load)")
    add_file_options(parser)
    parser.add_argument('--load', action='append', default=[], metavar='PREFIX',
                        help='a saved trace (remap: two, or the unit and one)')
    parser.add_argument('--save', metavar='PREFIX', help="save the unit's trace as PREFIX.trace, .o, .manifest.json")
    parser.add_argument('--version', default='GC_2_7', help="a saved trace's compiler (default GC_2_7)")
    parser.add_argument('--target-asm', type=Path, help="the target's asm (default: the unit's)")
    parser.add_argument('-f', '--function', help='the function (an exact link name for overloads)')
    parser.add_argument('--class', dest='class_', help='GPR (default), FPR, VR or CR')
    parser.add_argument('--want', action='append', default=[], metavar='V=REGISTER',
                        help='a register the target has for vreg V, over the target asm')
    edit = parser.add_argument_group('replay edits (repeatable)')
    edit.add_argument('--order', action='append', default=[], metavar='V,W,...')
    edit.add_argument('--move', action='append', default=[], metavar='V:before|after:W')
    edit.add_argument('--drop', action='append', default=[], metavar='V')
    edit.add_argument('--degree', action='append', default=[], metavar='V:+1')
    edit.add_argument('--neighbours', action='append', default=[], metavar='V:+1')
    edit.add_argument('--unlink', action='append', default=[], metavar='A,B')
    edit.add_argument('--link', action='append', default=[], metavar='A,B')
    search = parser.add_argument_group('search')
    search.add_argument('--iterations', type=int)
    search.add_argument('--seed')
    search.add_argument('--restarts', type=int)
    search.add_argument('--top', type=int, help='the solutions kept')
    search.add_argument('--fixed', help='vregs whose number stays (V,W,...)')
    search.add_argument('--nodes', help='the vregs renumbered (V,W,...)')
    search.add_argument('--kinds', help='the edit kinds tried (default drop,degree,unlink)')
    remap = parser.add_argument_group('remap')
    for side in ('first', 'second'):
        remap.add_argument(f'--{side}-round', type=int, help=f"the {side} trace's round (zero-based; default its last)")
        remap.add_argument(f'--{side}-snapshot', choices=['before', 'after'])
    parser.add_argument('--json', action='store_true', help='print the result as JSON')
    args = parser.parse_args()
    wanted = 2 if args.command == 'remap' else 1
    if len(args.load) + bool(args.unit) != wanted:
        parser.error(f'{args.command} takes {"two traces" if wanted == 2 else "one trace"}: --unit and/or --load')
    if args.save and not args.unit:
        parser.error('--save needs --unit')

    def body():
        tools = load()
        with engine() as session:
            traces = []
            if args.unit:
                unit = tools.Unit(session, args.unit)
                params = dict(handle=unit.handle, files=files(args, unit))
                if args.save:
                    params['save'] = str(Path(args.save).resolve())
                if args.target_asm:
                    params['target_asm'] = str(args.target_asm.resolve())
                traces.append(session.call_one('trace.load', **params)['trace'])
            for prefix in args.load:
                params = dict(load=str(Path(prefix).resolve()), version=args.version)
                if args.target_asm:
                    params['target_asm'] = str(args.target_asm.resolve())
                traces.append(session.call_one('trace.load', **params)['trace'])
            common = {key: value for key, value in (('function', args.function), ('class', args.class_))
                      if value is not None}
            common['text'] = not args.json
            if args.command == 'remap':
                params = dict(common, first=traces[0], second=traces[1])
                for key in ('first_round', 'second_round', 'first_snapshot', 'second_snapshot'):
                    if getattr(args, key) is not None:
                        params[key] = getattr(args, key)
                result = session.call_one('trace.remap', **params)
            else:
                params = dict(common, trace=traces[0])
                if args.command != 'verify':
                    wants = {}
                    for item in args.want:
                        vreg, _, register = item.partition('=')
                        if not register:
                            raise SystemExit(f'--want needs V=REGISTER, not {item!r}')
                        wants[vreg] = register
                    params['wants'] = wants
                if args.command == 'replay':
                    params['edits'] = edits(args)
                if args.command == 'search':
                    for key in ('iterations', 'seed', 'restarts', 'top'):
                        if getattr(args, key) is not None:
                            params[key] = getattr(args, key)
                    for key in ('fixed', 'nodes'):
                        if getattr(args, key):
                            params[key] = numbers(getattr(args, key))
                    if args.kinds:
                        params['kinds'] = args.kinds.split(',')
                result = session.call_one('trace.' + args.command, **params)
        if args.json:
            print(json.dumps(result['result'], indent=1))
        else:
            print('\n'.join(result.get('text') or []))

    run(body)


if __name__ == '__main__':
    main()
