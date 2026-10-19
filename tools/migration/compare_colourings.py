"""Compare a complete colouring command against the pinned Python implementation."""
import argparse
import json
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--keep", type=Path)
    parser.add_argument("--text", action="store_true")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("a colouring command is required after --")
    if not args.text and "--json" not in command:
        at = command.index("--") if "--" in command else len(command)
        command[at:at] = ["--json"]
    python = subprocess.run([sys.executable, str(args.oracle_root / "tools/colouring.py"), *command], capture_output=True, text=True, timeout=1800)
    rust = subprocess.run([str(args.binary.resolve()), "colouring", "--oracle-root", str(args.oracle_root.resolve()), *command], capture_output=True, text=True, timeout=1800)
    if not python.stdout or not rust.stdout:
        raise RuntimeError(f"command failed: Python {python.stderr}, Rust {rust.stderr}")
    a = rust.stdout.splitlines() if args.text else json.loads(rust.stdout)
    b = python.stdout.splitlines() if args.text else json.loads(python.stdout)
    from compare_backends import difference
    found = difference(a, b)
    if python.returncode != rust.returncode:
        found = f"exit code differs: Python {python.returncode}, Rust {rust.returncode}"
    if args.keep:
        args.keep.mkdir(parents=True, exist_ok=True)
        suffix = "txt" if args.text else "json"
        (args.keep / f"python.{suffix}").write_text(python.stdout, encoding="utf-8")
        (args.keep / f"rust.{suffix}").write_text(rust.stdout, encoding="utf-8")
        (args.keep / "result.json").write_text(json.dumps({"command": command, "equivalent": found is None,
                                                         "difference": found, "exit_code": rust.returncode}, indent=2), encoding="utf-8")
    if found:
        raise RuntimeError(found)
    print(f"colouring {command[0]}: output agrees; exit {rust.returncode}")


if __name__ == "__main__":
    main()
