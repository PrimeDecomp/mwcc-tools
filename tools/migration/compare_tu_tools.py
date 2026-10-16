"""Run a TU tool through the pinned Python and native Rust implementations."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
from compare_backends import difference


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--keep", type=Path)
    parser.add_argument("--text", action="store_true")
    parser.add_argument("command", choices=("inline-diff", "inline-window", "data-diff", "header-impact"))
    parser.add_argument("args", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    common = args.args
    if common[:1] == ["--"]:
        common = common[1:]
    if not args.text:
        common = ["--json", *common]
    python = subprocess.run([sys.executable, str(args.oracle_root / "tools" / (args.command.replace("-", "_") + ".py")), *common],
                            capture_output=True, text=True, timeout=600)
    rust = subprocess.run([str(args.binary.resolve()), args.command, "--oracle-root", str(args.oracle_root.resolve()), *common],
                          capture_output=True, text=True, timeout=600)
    if args.keep:
        args.keep.mkdir(parents=True, exist_ok=True)
        for name, output in (("python", python), ("rust", rust)):
            (args.keep / f"{name}.txt").write_text(output.stdout, encoding="utf-8")
            (args.keep / f"{name}.stderr.txt").write_text(output.stderr, encoding="utf-8")
    if python.returncode or rust.returncode:
        raise RuntimeError(f"Python exited {python.returncode}: {python.stderr}; Rust exited {rust.returncode}: {rust.stderr}")
    expected = python.stdout.splitlines() if args.text else json.loads(python.stdout)
    actual = rust.stdout.splitlines() if args.text else json.loads(rust.stdout)
    found = difference(actual, expected)
    if found:
        raise RuntimeError(found)
    print(f"{args.command}: {'text' if args.text else 'JSON'} agrees with Python")


if __name__ == "__main__":
    main()
