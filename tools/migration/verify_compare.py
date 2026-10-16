"""Compare native extraction/check/report with the pinned Python tool on full versions."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run(command, project):
    return subprocess.run(command, cwd=project, capture_output=True, text=True, timeout=1800)


def tree(directory):
    return {p.relative_to(directory).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in directory.rglob("*") if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--version", action="append")
    parser.add_argument("--objdiff", type=Path)
    parser.add_argument("--keep", type=Path)
    args = parser.parse_args()
    root, binary = args.oracle_root.resolve(), args.binary.resolve()
    results = []
    with tempfile.TemporaryDirectory(prefix="compare-parity-") as temporary:
        for version in args.version or ["GC_1_0", "GC_1_3"]:
            configs = root / "config" / version
            config = json.loads((configs / "config.json").read_text())
            config["original"] = str((root / config["original"]).resolve())
            functions = json.loads((configs / "functions.json").read_text())
            projects, runs = [], []
            for backend in ("python", "rust"):
                project = Path(temporary) / version / backend
                destination = project / "config" / version
                destination.mkdir(parents=True)
                (destination / "config.json").write_text(json.dumps(config))
                for name in ("functions.json", "bindings.json"):
                    shutil.copyfile(configs / name, destination / name)
                shutil.copyfile(root / "config/sources.json", project / "config/sources.json")
                for source in dict.fromkeys(f["source"] for f in functions if "source" in f):
                    name = f"build/{version}/compiled/{source}.obj"
                    (project / name).parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(root / name, project / name)
                if args.objdiff:
                    tool = project / "build/tools" / ("objdiff-cli.exe" if sys.platform == "win32" else "objdiff-cli")
                    tool.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(args.objdiff, tool)
                    tool.chmod(0o755)
                for action in (["extract", "compare", "report"] if args.objdiff else ["extract", "compare"]):
                    command = ([sys.executable, str(root / "tools/compare.py"), action, version] if backend == "python"
                               else [str(binary), "compare", action, version, "--root", "."])
                    result = run(command, project)
                    if result.returncode or (result.stderr and action != "report"):
                        raise RuntimeError(f"{version}/{backend}/{action}: exit {result.returncode}\n{result.stdout}\n{result.stderr}")
                    if action == "compare":
                        runs.append(result)
                projects.append(project)
            if runs[0].stdout != runs[1].stdout:
                raise RuntimeError(f"{version}: summaries differ:\nPython {runs[0].stdout}\nRust {runs[1].stdout}")
            for name in ("objdiff.json", f"build/{version}/ok", *([f"build/{version}/report.json"] if args.objdiff else [])):
                first, second = [(p / name).read_text() for p in projects]
                if name.endswith(".json"):
                    first, second = json.loads(first), json.loads(second)
                if first != second:
                    raise RuntimeError(f"{version}: {name} differs")
            counts = {}
            for folder in ("target", "base"):
                first, second = [tree(p / "build" / version / folder) for p in projects]
                if first != second:
                    differences = [n for n in dict.fromkeys([*first, *second]) if first.get(n) != second.get(n)]
                    raise RuntimeError(f"{version}: {folder} differs: {differences[:20]}")
                counts[folder] = len(first)
            result = {"version": version, "identical": True, "counts": counts,
                      "report_checked": bool(args.objdiff), "summary": runs[1].stdout.rstrip(),
                      "objdiff_sha256":hashlib.sha256(args.objdiff.read_bytes()).hexdigest() if args.objdiff else None}
            results.append(result)
            if args.keep:
                args.keep.parent.mkdir(parents=True, exist_ok=True)
                args.keep.write_text(json.dumps(results, indent=2)+"\n")
            print(f"{version}: extraction, base objects, objdiff metadata and checks agree: {runs[1].stdout.splitlines()[0]}", flush=True)


if __name__ == "__main__":
    main()
