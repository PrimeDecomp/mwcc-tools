"""Compare native Rust JSON analysis with the pinned Python oracle on a live source.

Game-derived outputs stay in the local --keep directory; no source is copied.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys


def normalize(document):
    document = json.loads(json.dumps(document))
    document["oracle"] = document["oracle"].replace("\\", "/")
    # Heap addresses are per-run identities. Both documents also give each
    # instruction's position; compare that identity and all scheduling decisions.
    for function in document["functions"]:
        for block in function["schedule"]:
            for pick in block["picks"]:
                for item in [pick, *pick["over"]]:
                    if item.get("position") is not None:
                        item["i"] = f"instruction#{item['position']}"
    return document


def difference(a, b, path="$"):
    if type(a) is not type(b):
        return f"{path}: types differ ({type(a).__name__}, {type(b).__name__})"
    if isinstance(a, dict):
        if a.keys() != b.keys():
            return f"{path}: keys differ ({sorted(a.keys() - b.keys())}, {sorted(b.keys() - a.keys())})"
        for key in a:
            found = difference(a[key], b[key], f"{path}.{key}")
            if found:
                return found
    elif isinstance(a, list):
        if len(a) != len(b):
            return f"{path}: lengths differ ({len(a)}, {len(b)})"
        for index, (x, y) in enumerate(zip(a, b)):
            found = difference(x, y, f"{path}[{index}]")
            if found:
                return found
    elif a != b:
        return f"{path}: {a!r} != {b!r}"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source")
    parser.add_argument("--from-ninja", type=Path, required=True)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--keep", type=Path)
    parser.add_argument("--text", action="store_true", help="compare default text, PCode and schedule reports")
    args = parser.parse_args()
    if args.text:
        common = ["--from-ninja", str(args.from_ninja.resolve()), "--schedule", "--pcode", "--full-cost", args.source]
        python = subprocess.run([sys.executable, str(args.oracle_root / "tools/oracle.py"), *common], capture_output=True, text=True, timeout=600)
        rust = subprocess.run([str(args.binary.resolve()), "oracle", "--oracle-root", str(args.oracle_root.resolve()), *common], capture_output=True, text=True, timeout=600)
        if python.returncode or rust.returncode:
            raise RuntimeError(f"text command failed: Python {python.stderr}, Rust {rust.stderr}")
        def lines(text):
            lines = text.splitlines()
            if lines:
                lines[0] = lines[0].replace("\\", "/")
            return lines
        found = difference(lines(rust.stdout), lines(python.stdout))
        if args.keep:
            args.keep.mkdir(parents=True, exist_ok=True)
            (args.keep / "python.txt").write_text(python.stdout, encoding="utf-8")
            (args.keep / "rust.txt").write_text(rust.stdout, encoding="utf-8")
            (args.keep / "text-result.json").write_text(json.dumps({"source": args.source, "equivalent": found is None, "difference": found}, indent=2), encoding="utf-8")
        if found:
            raise RuntimeError(found)
        print(f"{args.source}: text agrees ({len(lines(rust.stdout))} lines)")
        return
    spec = importlib.util.spec_from_file_location("oracle_reference", args.oracle_root / "tools/oracle.py")
    oracle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(oracle)
    compiled = oracle.compile_trace(args.source, from_ninja=args.from_ninja, schedule=True,
                                    inline_cost=True, full_cost=True, instantiation=True,
                                    scalarize=True, blocks=True, cse=True)
    if compiled["identical"] is not True:
        raise RuntimeError("Python oracle did not verify object equivalence")
    expected = oracle.document_for(compiled)[0]
    command = [str(args.binary.resolve()), "oracle", "--oracle-root", str(args.oracle_root.resolve()),
               "--from-ninja", str(args.from_ninja.resolve()), "--json", "--schedule", "--inline-cost", "all",
               "--full-cost", "--instantiation", "all", "--locals", "--blocks", "--cse", args.source]
    run = subprocess.run(command, capture_output=True, text=True, timeout=600)
    if run.returncode:
        raise RuntimeError(f"Rust oracle failed: {run.stderr}")
    actual = json.loads(run.stdout)
    found = difference(normalize(actual), normalize(expected))
    if args.keep:
        args.keep.mkdir(parents=True, exist_ok=True)
        (args.keep / "python.json").write_text(json.dumps(expected, indent=2), encoding="utf-8")
        (args.keep / "rust.json").write_text(json.dumps(actual, indent=2), encoding="utf-8")
        (args.keep / "result.json").write_text(json.dumps({"source": args.source, "version": compiled["version"],
                                                         "equivalent": found is None, "difference": found}, indent=2), encoding="utf-8")
    if found:
        raise RuntimeError(found)
    print(f"{compiled['version']} {args.source}: JSON agrees ({len(expected['functions'])} functions); both objects verified")


if __name__ == "__main__":
    main()
