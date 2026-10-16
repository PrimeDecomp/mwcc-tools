"""Exercise native gates, verification and oracle construction in an isolated source tree.

Only sources/metadata and an optional explicitly identified updater cache are copied.
All compilers, libraries and compiled outputs are acquired or built by native Ninja rules.
Python is the independent validation wrapper; generated build commands must not invoke it.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--existing-root", type=Path, help="resume validation of a retained tree after a terminal failure")
    parser.add_argument("--cached-updater", type=Path)
    parser.add_argument("--canaries", type=Path)
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--ninja", default="ninja")
    parser.add_argument("--actions", nargs="+", choices=["gates", "verify", "oracles"], default=["gates", "verify", "oracles"])
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    args.source_root = args.source_root.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    stage = args.existing_root.resolve() if args.existing_root else Path(tempfile.mkdtemp(prefix="native-build-", dir=args.output.resolve()))
    evidence = dict(native_sha256=sha(args.binary), source_root=str(args.source_root), resumed=bool(args.existing_root), actions=[])
    if args.existing_root:
        previous = json.loads((stage / "native-build-verification.json").read_text(encoding="utf-8"))
        evidence["input_sha256"] = previous.get("input_sha256", {})
        evidence["previous_attempts"] = [*previous.get("previous_attempts", []),
                                          {key: previous[key] for key in ("native_sha256", "actions")}]
        for key in ("seeded_updater_sha256", "updater_cache"):
            if key in previous:
                evidence[key] = previous[key]
    if not args.existing_root:
        for directory in ["config", "include", "src"]:
            shutil.copytree(args.source_root / directory, stage / directory)
        (stage / "tools/lmgr_stub").mkdir(parents=True)
        shutil.copy2(args.source_root / "tools/lmgr_stub/lmgr326b.dll", stage / "tools/lmgr_stub/lmgr326b.dll")
        evidence["input_sha256"] = {p.relative_to(stage).as_posix(): sha(p) for p in sorted(stage.rglob("*")) if p.is_file()}
        if args.cached_updater:
            destination = stage / "build/downloads/Pro5.3_Factory_Upd_NoLibs.exe"
            destination.parent.mkdir(parents=True)
            shutil.copy2(args.cached_updater, destination)
            evidence["seeded_updater_sha256"] = sha(destination)
    print(f"retained native build tree: {stage}", flush=True)
    updater = stage / "build/downloads/Pro5.3_Factory_Upd_NoLibs.exe"
    if updater.is_file():
        evidence["updater_cache"] = dict(path=updater.relative_to(stage).as_posix(), sha256=sha(updater))

    def save():
        (stage / "native-build-verification.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")

    def run(name, command, *, json_output=False):
        began = time.monotonic()
        log_path = stage / f"native-{name}.log"
        suffix = 1
        while log_path.exists():
            suffix += 1
            log_path = stage / f"native-{name}-{suffix}.log"
        print(f"{name}: starting", flush=True)
        with log_path.open("w", encoding="utf-8") as log:
            process = subprocess.Popen([str(a) for a in command], cwd=stage, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
            for line in process.stdout:
                log.write(line)
                log.flush()
                if not json_output:
                    print(line, end="", flush=True)
            code = process.wait()
        evidence["actions"].append(dict(name=name, command=[str(a) for a in command], exit_code=code,
                                        seconds=time.monotonic() - began, log=log_path.name, log_sha256=sha(log_path)))
        save()
        assert code == 0, f"{name} failed; retained log: {log_path}"
        print(f"{name}: passed", flush=True)
        return log_path

    save()
    for action in args.actions:
        if action != "oracles":
            run(action, [args.binary, action, "--root", stage, "--ninja", args.ninja, "--jobs", args.jobs, "--timeout", "900"])
            ninja = (stage / "build.ninja").read_text(encoding="utf-8")
            assert "$python" not in ninja and "tools/" + "download_tool.py" not in ninja
            assert "version = GC_1_2_5" in ninja
            continue
        for version in ["GC_1_3", "GC_1_3_2", "GC_2_7"]:
            run(f"configure-{version}-oracle", [args.binary, "configure", "--root", stage, "--version", version, "--mode", "link", "--oracle"])
            ninja = (stage / "build.ninja").read_text(encoding="utf-8")
            assert "$python" not in ninja and ".py " not in ninja
            target = f"build/{version}/oracle/mwcceppc.exe"
            run(f"build-{version}-oracle", [args.ninja, "-j", args.jobs, target])
            evidence.setdefault("oracle_sha256", {})[version] = sha(stage / target)
            save()
            if args.canaries:
                log = run(f"canaries-{version}", [args.binary, "corpus", "--oracle-root", stage, "--version", version,
                           "--canaries", args.canaries.resolve(), "--all-traces", "--jobs", args.jobs, "--json"], json_output=True)
                report = json.loads(log.read_text(encoding="utf-8"))
                assert report["total"] > 0 and report["identical"] == report["total"]
                evidence.setdefault("canaries", {})[version] = dict(total=report["total"], identical=report["identical"])
                print(f"{version}: {report['identical']}/{report['total']} canaries identical", flush=True)
                save()
        run("restore-primary", [args.binary, "configure", "--root", stage])
    evidence["complete"] = True
    save()
    print("requested native build gates passed", flush=True)


if __name__ == "__main__":
    main()
