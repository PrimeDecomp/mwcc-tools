"""Compare a pinned kit's Python regdiff to the native implementation on a live unit."""
import argparse
from pathlib import Path
import subprocess
import sys
from compare_backends import difference


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--reference-script", type=Path, required=True)
    parser.add_argument("--keep", type=Path)
    parser.add_argument("args", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    common = args.args[1:] if args.args[:1] == ["--"] else args.args
    # Load the pinned helper unchanged; relocate only its project and objdiff discovery.
    bootstrap = '''import importlib.util, pathlib, sys
script, project, *arguments = sys.argv[1:]
sys.path.insert(0, str(pathlib.Path(script).parent))
spec = importlib.util.spec_from_file_location("regdiff_reference", script)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
module.ROOT = pathlib.Path(project)
import decomp_common
decomp_common.ROOT = pathlib.Path(project)
sys.argv = [script, *arguments]
module.main()
'''
    python = subprocess.run([sys.executable, "-c", bootstrap, str(args.reference_script.resolve()), str(args.project.resolve()),
                             "--oracle-root", str(args.oracle_root.resolve()), *common], capture_output=True, text=True, timeout=600)
    rust = subprocess.run([str(args.binary.resolve()), "oracle", "regdiff", "--project", str(args.project.resolve()),
                           "--oracle-root", str(args.oracle_root.resolve()), *common], capture_output=True, text=True, timeout=600)
    if args.keep:
        args.keep.mkdir(parents=True, exist_ok=True)
        for name, result in (("python", python), ("rust", rust)):
            (args.keep / f"{name}.txt").write_text(result.stdout, encoding="utf-8")
            (args.keep / f"{name}.stderr.txt").write_text(result.stderr, encoding="utf-8")
    if python.returncode or rust.returncode:
        raise RuntimeError(f"Python {python.returncode}: {python.stderr}; Rust {rust.returncode}: {rust.stderr}")
    def normalized(text):
        lines = text.splitlines()
        if lines:
            lines[0] = lines[0].replace("\\", "/")
        return lines
    found = difference(normalized(rust.stdout), normalized(python.stdout))
    if found:
        raise RuntimeError(found)
    print(f"native regdiff agrees with Python across {len(rust.stdout.splitlines())} lines")


if __name__ == "__main__":
    main()
