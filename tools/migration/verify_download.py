"""Fetch pinned tools into an isolated tree and compare with the reference installation.

The updater can be explicitly seeded from an existing cache; other requested tools are fetched.
The output directory and its evidence manifest are retained for later fresh-build validation.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cached-updater", type=Path)
    parser.add_argument("--tools", nargs="+", default=["pro4", "pro5", "pro53", "pro6", "mwld", "lib", "compilers", "objdiff-cli"])
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    args.oracle_root = args.oracle_root.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix="native-download-", dir=args.output.resolve()))
    (stage / "tools/lmgr_stub").mkdir(parents=True)
    shutil.copy2(args.oracle_root / "tools/lmgr_stub/lmgr326b.dll", stage / "tools/lmgr_stub/lmgr326b.dll")
    records = dict(native_sha256=sha(args.binary), reference_root=str(args.oracle_root), tools=[])
    if args.cached_updater:
        destination = stage / "build/downloads/Pro5.3_Factory_Upd_NoLibs.exe"
        destination.parent.mkdir(parents=True)
        shutil.copy2(args.cached_updater, destination)
        records["seeded_updater_sha256"] = sha(destination)
    spec = importlib.util.spec_from_file_location("reference_download", args.oracle_root / "tools/download_tool.py")
    reference = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reference)
    paths = {"pro4": "build/compilers/pro4/mwcc.exe", "pro5": "build/compilers/pro5/mwcc.exe",
             "pro53": "build/compilers/pro53/mwcc.exe", "pro6": "build/compilers/pro6/mwcc.exe",
             "mwld": "build/compilers/pro53/mwld.exe", "lib": "lib/ok", "compilers": "build/compilers/.download-ok",
             "objdiff-cli": "build/objdiff-cli" + (".exe" if os.name == "nt" else ""), "wibo": "build/wibo"}
    print(f"retained validation tree: {stage}", flush=True)
    for tool in args.tools:
        began = time.monotonic()
        relative = paths[tool]
        subprocess.run([str(args.binary), "download", tool, relative, "--root", str(stage), "--timeout", "300"], check=True)
        result = dict(tool=tool, seconds=time.monotonic() - began)
        if tool in ("objdiff-cli", "wibo"):
            url = reference.objdiff_url() if tool == "objdiff-cli" else reference.wibo_url()
            expected = reference.get(url)
            actual = (stage / relative).read_bytes()
            assert actual == expected, f"{tool}: bytes differ from independent Python fetch"
            result.update(files=1, sha256=sha(stage / relative), url=url)
        elif tool in ("lib", "compilers"):
            directory = "lib" if tool == "lib" else "build/compilers"
            hashes = {}
            for path in sorted((stage / directory).rglob("*")):
                if not path.is_file() or path.name in (".download-ok", "lmgr326b.dll"):
                    continue
                name = path.relative_to(stage)
                expected = args.oracle_root / name
                assert expected.is_file(), f"reference installation lacks {name}"
                assert sha(path) == sha(expected), f"{name}: bytes differ"
                hashes[name.as_posix()] = sha(path)
            if tool == "lib":
                expected_names = {p.relative_to(args.oracle_root).as_posix()
                                  for p in (args.oracle_root / "lib").rglob("*") if p.is_file()}
                assert set(hashes) == expected_names, "library output inventory differs"
            result.update(files=len(hashes), file_sha256=hashes)
        else:
            assert sha(stage / relative) == sha(args.oracle_root / relative), f"{tool}: bytes differ"
            result.update(files=1, sha256=sha(stage / relative))
        records["tools"].append(result)
        (stage / "verification.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
        print(f"{tool}: {result['files']} files agree ({result['seconds']:.2f}s)", flush=True)
    print("requested downloads verified", flush=True)


if __name__ == "__main__":
    main()
