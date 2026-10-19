"""Prove current producer sources emit the exact objects in retained runtime proofs.

Keep retained sources immutable. Rust-only source differences may be explained by
byte-identical fresh builds; C/header/config differences require a new proof tree.
"""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
from verify_producer import ROOT, sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--manifest", default="producer-verification.json")
    args = parser.parse_args()
    stage, binary = args.directory.resolve(), args.binary.resolve()
    manifest_path = stage / args.manifest
    manifest = json.loads(manifest_path.read_text())
    assert manifest["complete"] and manifest["versions"]
    proof = Path(tempfile.mkdtemp(prefix="current-source-check-", dir=stage))
    print(f"retained source-equivalence proof: {proof}", flush=True)
    evidence = dict(complete=False, binary_sha256=sha(binary),
        runtime_manifest_sha256=sha(manifest_path), sources={}, differences={}, builds=[], versions=[])

    def save():
        (proof / "source-equivalence.json").write_text(json.dumps(evidence, indent=2) + "\n")

    paths = ["src/oracle/Hooks.c", "src/oracle/OracleTrace.c", "rust-toolchain.toml", "config/sources.json",
        "crates/mwcc-oracle-hooks/Cargo.toml",
        *[f"config/{v['version']}/config.json" for v in manifest["versions"]],
        *[p.relative_to(stage).as_posix() for p in sorted((stage / "include").rglob("*")) if p.is_file()],
        *[p.relative_to(ROOT).as_posix() for p in sorted((ROOT / "crates/mwcc-oracle-hooks/src").glob("*.rs"))]]
    for name in paths:
        old, current = sha(stage / name), sha(ROOT / name)
        evidence["sources"][name] = dict(retained=old, current=current)
        if old != current:
            evidence["differences"][name] = dict(retained=old, current=current)
            assert name.startswith("crates/mwcc-oracle-hooks/src/") and name.endswith(".rs"), name
    save()
    for name, options in [("hooks.obj", []), ("trace.obj", ["--trace-shim"])]:
        command = [str(binary), "build-hooks", str(proof / name), "--root", str(ROOT), *options]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=300)
        for stream in ["stdout", "stderr"]:
            (proof / f"{name}.{stream}").write_bytes(getattr(result, stream))
        evidence["builds"].append(dict(command=command, exit_code=result.returncode,
            stdout_sha256=sha(proof / f"{name}.stdout"), stderr_sha256=sha(proof / f"{name}.stderr")))
        save()
        assert result.returncode == 0, result.stderr.decode(errors="replace")
    for version in manifest["versions"]:
        name = version["version"]
        for relative, fresh, field in [("compiled/rust/hooks.obj", "hooks.obj", "rust_object_sha256"),
                ("OracleTrace.obj", "trace.obj", "trace_object_sha256")]:
            assert sha(stage / f"build/{name}/oracle/{relative}") == sha(proof / fresh) == version[field]
        assert sha(stage / f"build/{name}/oracle/mwcceppc.exe") == version["producer_sha256"]
        if "frontend_object_sha256" in version:
            fresh = proof / f"{name}-frontend.obj"
            command = [str(binary), "build-hooks", str(fresh), "--root", str(ROOT), "--frontend-version", name]
            result = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=300)
            for stream in ["stdout", "stderr"]: (proof / f"{name}.{stream}").write_bytes(getattr(result, stream))
            evidence["builds"].append(dict(command=command, exit_code=result.returncode,
                stdout_sha256=sha(proof / f"{name}.stdout"), stderr_sha256=sha(proof / f"{name}.stderr")))
            save()
            assert result.returncode == 0, result.stderr.decode(errors="replace")
            assert sha(fresh) == sha(stage / f"build/{name}/oracle/compiled/rust/frontend.obj") == version["frontend_object_sha256"]
        evidence["versions"].append(dict(version=name, hooks_sha256=sha(proof / "hooks.obj"), trace_sha256=sha(proof / "trace.obj")))
    evidence["complete"] = True
    save()
    print(f"{len(evidence['versions'])} retained producers use exactly the objects emitted by current sources")


if __name__ == "__main__":
    main()
