"""Exercise the native oracle's completed text views against real Python commands."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
from compare_backends import difference

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--keep", type=Path)
    parser.add_argument("--from-ninja", type=Path)
    parser.add_argument("--source")
    args = parser.parse_args()
    manifest = json.loads((ROOT / "tests/fixtures/oracle/manifest.json").read_text())
    commands = [("cse", ["--cse"]), ("cse", ["--ir", "all"]),
                ("registers", ["--exchange", "r41,r42", "--function", "across_calls"]),
                ("pipeline", ["--inline-cost", "nested", "--full-cost"]),
                ("templates", ["--instantiation", "adjust"]), ("pipeline", ["--blocks", "--pcode"]),
                ("pipeline", ["--blocks", "--cut-limit", "0", "--merge-limit", "none"]),
                ("pipeline", ["--blocks", "--cut-limit", "none", "--merge-limit", "0"]),
                ("pipeline", ["--locals"]), ("templates", ["--locals"])]
    results = []
    for version in (("project",) if args.from_ninja else ("GC_1_3", "GC_1_3_2", "GC_2_7")):
        if args.from_ninja:
            if not args.source:
                parser.error("--from-ninja requires --source")
            commands = [(args.source, ["--locals"])]
        for index, (source, view) in enumerate(commands):
            if args.from_ninja:
                common = ["--from-ninja", str(args.from_ninja.resolve()), *view, source]
            else:
                case = next(c for c in manifest["cases"] if c["id"] == f"{version}/{source}")
                filename = str((ROOT / "tests/fixtures/oracle" / case["source"]).resolve())
                common = ["--version", version, *view, filename, "--", *case["flags"]]
            python = subprocess.run([sys.executable, str(args.oracle_root / "tools/oracle.py"), *common], capture_output=True, text=True, timeout=120)
            rust = subprocess.run([str(args.binary.resolve()), "oracle", "--oracle-root", str(args.oracle_root.resolve()), *common], capture_output=True, text=True, timeout=120)
            def normalize(text):
                lines = text.splitlines()
                if lines:
                    lines[0] = lines[0].replace("\\", "/")
                return lines
            found = difference(normalize(rust.stdout), normalize(python.stdout))
            if python.returncode or rust.returncode:
                raise RuntimeError(f"{version}/{view}: command failed: Python {python.stderr}, Rust {rust.stderr}")
            if args.keep:
                args.keep.mkdir(parents=True, exist_ok=True)
                prefix = args.keep / f"{version}-{index}"
                Path(f"{prefix}.python.txt").write_text(python.stdout, encoding="utf-8")
                Path(f"{prefix}.rust.txt").write_text(rust.stdout, encoding="utf-8")
            if found:
                raise RuntimeError(f"{version}/{view}: {found}")
            results.append({"version": version, "source": source, "view": view, "equivalent": True, "lines": len(rust.stdout.splitlines())})
    if args.keep:
        (args.keep / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(f"{len(results)} native oracle text commands agree, including patched-limit runs")


if __name__ == "__main__":
    main()
