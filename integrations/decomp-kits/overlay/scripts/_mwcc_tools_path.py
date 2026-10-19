"""The `mwcc_tools` package of the mwcc checkout this kit requires, on sys.path.

The experiment scripts, the oracle helpers and `decomp_experiment`/`decomp_matrix` are clients of that package: it
runs `mwcc-tools serve`, which reads each unit's build.ninja edge and compiles it in process with the WebAssembly
compiler (`build/GC_2_7/wasm/mwcc.wasm`). The checkout is `MWCC_ORACLE_ROOT`; its package (`python/mwcc_tools`) is
imported from there, so it always matches its executable: `MWCC_TOOLS`, else the checkout's
`target/release/mwcc-tools`, else `mwcc-tools` on PATH. No prebuilt executable is shipped.

    from _mwcc_tools_path import ROOT, engine, run
    def main(): ... with engine() as e: ...
    run(main)   # missing tools and the engine's errors print to stderr and exit 1
"""
import os
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parent.parent
MODULE = Path('build/GC_2_7/wasm/mwcc.wasm')
BUILD = """mwcc-tools is required (see EXTERNALS.md). From the parent of your game checkout:

    git clone KIT/references/mwcc-tools.bundle mwcc     # the kit's pinned source, or a clone of PrimeDecomp/mwcc-tools
    cd mwcc
    cargo build --release --locked -p mwcc-tools
    target/release/mwcc-tools configure --version GC_2_7
    ninja build/GC_2_7/wasm/mwcc.wasm

then set MWCC_ORACLE_ROOT to that checkout (and MWCC_TOOLS only to use another executable)."""


class MissingTools(RuntimeError):
    """mwcc-tools, its package or its compiler module is missing; the message says how to build them."""


def oracle_root():
    value = os.environ.get('MWCC_ORACLE_ROOT')
    return Path(value).expanduser().resolve() if value else None


def executable(root=None):
    """The mwcc-tools executable, or None."""
    explicit = os.environ.get('MWCC_TOOLS')
    if explicit:
        path = Path(explicit).expanduser()
        return str(path) if path.is_file() else shutil.which(str(path))
    root = root or oracle_root()
    if root:
        for name in ('mwcc-tools.exe', 'mwcc-tools'):
            if (root / 'target' / 'release' / name).is_file():
                return str(root / 'target' / 'release' / name)
    return shutil.which('mwcc-tools')


def problems(package_only=False):
    """What is missing, as lines (empty when everything is there); PACKAGE_ONLY: only the package."""
    root = oracle_root()
    if root is None:
        return ['MWCC_ORACLE_ROOT is not set']
    found = []
    if not (root / 'python' / 'mwcc_tools' / '__init__.py').is_file():
        found.append(f'{root / "python" / "mwcc_tools"} is missing (MWCC_ORACLE_ROOT is not an mwcc checkout)')
    if package_only:
        return found
    if not executable(root):
        found.append('the mwcc-tools executable was not found (MWCC_TOOLS, '
                     f'{root / "target" / "release" / "mwcc-tools"}, or PATH)')
    if not (root / MODULE).is_file():
        found.append(f'{root / MODULE} is missing')
    return found


def load(package_only=False):
    """The `mwcc_tools` package; MissingTools when it, the executable or the module is missing (PACKAGE_ONLY: when
    the caller brings its own engine, only the package)."""
    missing = problems(package_only)
    if missing:
        raise MissingTools('\n'.join(missing) + '\n\n' + BUILD)
    path = str(oracle_root() / 'python')
    if path not in sys.path:
        sys.path.insert(0, path)
    import mwcc_tools
    return mwcc_tools


def engine(**options):
    """An Engine (`mwcc-tools serve`) on this checkout, with the checkout's executable."""
    tools = load()
    options.setdefault('binary', executable())
    return tools.Engine(project=ROOT, **options)


def add_file_options(parser):
    """--candidate and --file: files the compile reads in place of the checkout's (which is never changed)."""
    parser.add_argument('--candidate', type=Path, help="a complete source compiled in place of the unit's")
    parser.add_argument('--file', action='append', default=[], metavar='PATH=FILE',
                        help='read FILE in place of PATH (from the checkout: a source or a header); repeatable')


def files(args, unit=None):
    """The {path: text} of --candidate (the unit's source) and --file."""
    overlay = {}
    for item in args.file:
        path, _, file = item.partition('=')
        if not path or not file:
            raise SystemExit(f'--file needs PATH=FILE, not {item!r}')
        overlay[Path(path).as_posix()] = Path(file).read_text(encoding='utf-8')
    if args.candidate:
        overlay[Path(unit.info['source']).as_posix()] = args.candidate.read_text(encoding='utf-8')
    return overlay


def run(main):
    """Run MAIN; a missing tool or an engine error is a message on stderr and exit status 1."""
    try:
        result = main()
    except MissingTools as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
    except KeyboardInterrupt:
        raise SystemExit(130)
    except Exception as error:
        tools = sys.modules.get('mwcc_tools')
        if tools is not None and isinstance(error, tools.EngineError):
            print(f'mwcc-tools: {error}', file=sys.stderr)
            raise SystemExit(1)
        raise
    raise SystemExit(result or 0)
