"""Compare every generated split object with the pinned Python implementation."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--version", action="append")
    parser.add_argument("--keep", type=Path)
    args = parser.parse_args()
    root = args.oracle_root.resolve()
    versions = args.version or ["GC_1_1", "GC_1_1p1", "GC_1_2_5", "GC_1_2_5n", "GC_1_3", "GC_1_3_2", "GC_2_7"]
    results = []
    with tempfile.TemporaryDirectory(prefix="split-parity-") as temporary:
        for version in versions:
            project = Path(temporary) / version
            config_dir = project / "config" / version
            config_dir.mkdir(parents=True)
            source_dir = root / "config" / version
            for name in ("symbols.txt", "splits.txt"):
                shutil.copyfile(source_dir / name, config_dir / name)
            config = json.loads((source_dir / "config.json").read_text())
            config["original"] = str((root / config["original"]).resolve())
            (config_dir / "config.json").write_text(json.dumps(config))
            python = subprocess.run([sys.executable, str(root / "tools/split.py"), version], cwd=project, capture_output=True, text=True, timeout=600)
            native_dir = project / "native"
            rust = subprocess.run([str(args.binary.resolve()), "split", version, "--root", str(project), "--output", str(native_dir)], capture_output=True, text=True, timeout=600)
            if python.returncode or rust.returncode:
                raise RuntimeError(f"{version}: Python {python.stderr}; Rust {rust.stderr}")
            expected_dir = project / "build" / version / "obj"
            files = sorted(p.relative_to(expected_dir).as_posix() for p in expected_dir.rglob("*") if p.is_file())
            actual = sorted(p.relative_to(native_dir).as_posix() for p in native_dir.rglob("*") if p.is_file())
            if files != actual:
                raise RuntimeError(f"{version}: file lists differ")
            digest = hashlib.sha256()
            for name in files:
                expected = (expected_dir / name).read_bytes()
                found = (native_dir / name).read_bytes()
                if found != expected:
                    raise RuntimeError(f"{version}/{name}: bytes differ (Rust {len(found)}, Python {len(expected)})")
                digest.update(name.encode()+b"\0"+hashlib.sha256(found).digest())
            result = {"version": version, "files": len(files), "identical": True, "tree_sha256": digest.hexdigest()}
            results.append(result)
            if args.keep:
                args.keep.parent.mkdir(parents=True, exist_ok=True)
                args.keep.write_text(json.dumps(results, indent=2)+"\n")
            print(f"{version}: {len(files)} split artifacts are byte-identical", flush=True)
    if args.keep:
        args.keep.parent.mkdir(parents=True, exist_ok=True)
        args.keep.write_text(json.dumps(results, indent=2)+"\n")


if __name__ == "__main__":
    main()
