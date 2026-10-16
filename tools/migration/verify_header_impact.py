"""Exercise dependency selection, overrides and Git revisions on a synthetic project."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from compare_backends import difference

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--keep", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    oracle = args.oracle_root.resolve()
    with tempfile.TemporaryDirectory(prefix="header-impact-live-") as temporary:
        project = Path(temporary)
        for relative in ("src", "include", "build/TEST/asm"):
            (project / relative).mkdir(parents=True)
        original = "inline int factor(int x) { return x*3+1; }\n"
        changed = "inline int factor(int x) { return x*7-2; }\n"
        (project / "include/header.hpp").write_text(original)
        for name in ("a", "b"):
            (project / f"src/{name}.cpp").write_text(f'#include "header.hpp"\nint {name}(int x) {{ return factor(x); }}\n')
            (project / f"build/TEST/asm/{name}.s").write_text(f".fn {name}__Fi, global\n/* 0000 38 63 00 03 */ addi r3, r3, 3\n.endfn {name}__Fi\n")
        (project / "deps.py").write_text('''import pathlib, sys
source, output = sys.argv[1:]
path = pathlib.Path(output)
path.parent.mkdir(parents=True, exist_ok=True)
path.write_text("dependency stub")
path.with_suffix(path.suffix+".d").write_text(f"{output}: {source} include/header.hpp\\n")
''')
        (project / "build.ninja").write_text(f'''python = "{sys.executable}"
mw_version = GC/1.3.2
cflags = -nodefaults -proc gekko -O4,p -inline auto -i include -lang=c++
rule mwcc
  command = $python deps.py $in $out
  depfile = $out.d
  deps = gcc
build build/TEST/src/a.o: mwcc src/a.cpp
  cflags = -nodefaults -proc gekko -O4,p -inline auto -i include -lang=c++
build build/TEST/src/b.o: mwcc src/b.cpp
  cflags = -nodefaults -proc gekko -O4,p -inline auto -i include -lang=c++
''')
        subprocess.run(["ninja"], cwd=project, check=True, capture_output=True)
        candidate = project / "candidate.hpp"
        candidate.write_text(changed)
        subprocess.run(["git", "init", "-q"], cwd=project, check=True)
        subprocess.run(["git", "add", "include/header.hpp", "src"], cwd=project, check=True)
        subprocess.run(["git", "-c", "user.name=Migration Gate", "-c", "user.email=migration@example.invalid",
                        "-c", "commit.gpgsign=false", "commit", "-qm", "synthetic baseline"], cwd=project, check=True)
        cases = [("override", ["--override", f"include/header.hpp={candidate}"]),
                 ("filtered", ["--unit", "a.cpp", "--override", f"include/header.hpp={candidate}"]),
                 ("git", ["--git", "HEAD"])]
        for name, options in cases:
            if name == "git":
                (project / "include/header.hpp").write_text(changed)
            for text in (False, True):
                common = ["--from-ninja", str(project / "build.ninja"), "--jobs", "2", "--all", *options]
                if not text:
                    common += ["--json"]
                python = subprocess.run([sys.executable, str(oracle / "tools/header_impact.py"), *common], capture_output=True, text=True, timeout=120)
                rust = subprocess.run([str(binary), "header-impact", "--oracle-root", str(oracle), *common], capture_output=True, text=True, timeout=120)
                if python.returncode or rust.returncode:
                    raise RuntimeError(f"{name}: Python {python.stderr}; Rust {rust.stderr}")
                expected = python.stdout.splitlines() if text else json.loads(python.stdout)
                actual = rust.stdout.splitlines() if text else json.loads(rust.stdout)
                found = difference(actual, expected)
                if found:
                    raise RuntimeError(f"{name}/{text}: {found}\nPython: {python.stdout}\nRust: {rust.stdout}")
                if not text:
                    expected_count = 1 if name == "filtered" else 2
                    if len(actual["units"]) != expected_count or any(u["error"] or len(u["functions"]) != 1 for u in actual["units"]):
                        raise RuntimeError(f"{name}: expected {expected_count} units with one actual changed function each: {actual}")
                if args.keep:
                    args.keep.mkdir(parents=True, exist_ok=True)
                    (args.keep / f"{name}-{'text' if text else 'json'}.txt").write_text(rust.stdout, encoding="utf-8")
        if (project / "include/header.hpp").read_text() != changed or candidate.read_text() != changed:
            raise RuntimeError("an input changed unexpectedly")
    print("header-impact: override, unit-filter and Git workflows match Python in JSON/text")


if __name__ == "__main__":
    main()
