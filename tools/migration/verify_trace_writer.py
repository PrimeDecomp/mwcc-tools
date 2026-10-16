"""Validate the separate Rust trace writer against the frozen C output API."""
import argparse
import hashlib
import importlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--trace-object", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--wibo", type=Path)
    args = parser.parse_args()
    binary, reference, trace = args.binary.resolve(), args.oracle_root.resolve(), args.trace_object.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    run_root = Path(tempfile.mkdtemp(prefix="trace-writer-", dir=args.output.resolve()))
    print(f"retained trace-writer ABI tree: {run_root}", flush=True)
    sys.path.insert(0, str(reference / "tools"))
    compare = importlib.import_module("compare")
    symbols, sections = compare.read_object(trace)
    assert {s["name"] for s in symbols.values() if s["section"] > 0 and s["storage"] == 2} == {"_OracleTrace_Enabled", "_OracleTrace_Printf"}
    assert {s["name"] for s in symbols.values() if s["section"] == 0 and s["storage"] == 2} == {"_getenv", "_fopen", "_vfprintf", "_fflush", "___files"}
    assert all(kind in [0, 6, 0x14] for section in sections for _, _, kind in section["relocs"])
    loader = None
    if os.name != "nt":
        requested = args.wibo or os.environ.get("MWCC_WIBO")
        if requested:
            loader = Path(requested).resolve()
            assert loader.is_file(), f"Explicit loader does not exist: {loader}"
        else:
            loader = reference / "build/tools/wibo"
            if not loader.is_file():
                located = shutil.which("wibo")
                assert located, "Select the supported runner with --wibo or MWCC_WIBO"
                loader = Path(located)
    def launch(executable):
        return [str(executable)] if os.name == "nt" else [str(loader), str(executable)]
    evidence = dict(trace_object_sha256=sha(trace), binary_sha256=sha(binary), versions=[],
        sources={str(p.relative_to(ROOT)): sha(p) for p in [ROOT / "tools/migration/abi/trace_runtime.c", ROOT / "tools/migration/abi/trace_layout.c",
            ROOT / "tools/migration/abi/trace_caller.rs", ROOT / "crates/mwcc-oracle-hooks/src/trace_output.rs",
            ROOT / "crates/mwcc-oracle-hooks/src/lib.rs", ROOT / "crates/mwcc-oracle-hooks/Cargo.toml"]},
        c_reference_sha256=sha(reference / "src/oracle/OracleTrace.c"))
    if loader:
        evidence["wibo_sha256"] = sha(loader)
    def save():
        (run_root / "trace-writer-verification.json").write_text(json.dumps(evidence, indent=2) + "\n")
    for version, index in [("GC_1_3", 5), ("GC_1_3_2", 6), ("GC_2_7", 7)]:
        project = run_root / version
        (project / "src/oracle").mkdir(parents=True, exist_ok=True)
        shutil.copyfile(reference / "src/oracle/OracleTrace.c", project / "src/oracle/OracleTrace.c")
        item = dict(version=version, runs=[], cases=[])
        evidence["versions"].append(item)
        def run(name, command, env=None):
            result = subprocess.run([str(c) for c in command], cwd=project, env=env, capture_output=True, timeout=120)
            for stream in ["stdout", "stderr"]:
                (project / f"{name}.{stream}").write_bytes(getattr(result, stream))
            item["runs"].append(dict(name=name, exit_code=result.returncode,
                stdout_sha256=sha(project / f"{name}.stdout"), stderr_sha256=sha(project / f"{name}.stderr")))
            save()
            assert result.returncode == 0, (version, name, result.returncode, result.stdout[-4000:], result.stderr[-4000:])
        settings = json.loads((reference / "config/sources.json").read_text())["src/backend/Coloring.c"]
        config = json.loads((reference / f"config/{version}/config.json").read_text())
        compiler = config.get("compilers", {}).get(settings["compiler"], settings["compiler"])
        item.update(compiler_sha256=sha(reference / f"build/compilers/{compiler}/mwcc.exe"),
                    linker_sha256=sha(reference / "build/compilers/pro53/mwld.exe"), compiler_flags=settings["flags"], version_index=index)
        env = dict(os.environ, MWCIncludes=str(reference / "include/libc"))
        compile = [*launch(reference / f"build/compilers/{compiler}/mwcc.exe"), *settings["flags"], f"-DVERSION={index}",
            "-I" + str(reference / "include"), "-I" + str(project), "-c"]
        run("layout", [*compile, ROOT / "tools/migration/abi/trace_layout.c", "-o", project / "layout.obj"], env)
        syms, sects = compare.read_object(project / "layout.obj")
        symbol = next(s for s in syms.values() if s["name"] == "_RustAbiTraceLayout")
        values = struct.unpack_from("<7I", sects[symbol["section"] - 1]["data"], symbol["value"])
        assert values == (84, 2, 168, 4, 4, 4, 80), values
        item["c_layout"] = list(values)
        run("runtime", [*compile, ROOT / "tools/migration/abi/trace_runtime.c", "-o", project / "runtime.obj"], env)
        # Run rustc from the workspace so rustup uses its pinned toolchain.
        caller = project / "caller.obj"
        built = subprocess.run(["rustc", "--edition", "2024", "--target", "i686-pc-windows-msvc", "--crate-type", "lib", "--emit=obj",
            "-D", "warnings", "-C", "panic=abort", "-C", "opt-level=2", "-C", "debuginfo=0", "-C", "codegen-units=1",
            str(ROOT / "tools/migration/abi/trace_caller.rs"), "-o", str(caller)], cwd=ROOT, capture_output=True, timeout=120)
        assert built.returncode == 0, built.stdout + built.stderr
        item["rust_caller_sha256"] = sha(caller)
        (project / "kernel.def").write_text("LIBRARY KERNEL32.dll\nEXPORTS\n\tExitProcess@4 @0\n\tGetEnvironmentVariableA@12 @1\n")
        run("implib", [binary, "implib", project / "kernel.def", project / "kernel.lib"])
        run("link", [*launch(reference / "build/compilers/pro53/mwld.exe"), "-nostdlib", "-subsystem", "console", "-m", "_main",
            project / "runtime.obj", trace, caller, project / "kernel.lib", "-o", project / "runtime.exe"])
        item["runtime_sha256"] = sha(project / "runtime.exe")
        for case in range(24):
            run(f"case-{case}", launch(project / "runtime.exe"), dict(os.environ, ORACLE_ABI_TRACE_CASE=str(case)))
            item["cases"].append(case)
        print(f"{version}: 7 FILE/ABI probes and 24 fresh-process C/Rust trace-writer scenarios pass", flush=True)
    evidence["complete"] = True
    save()


if __name__ == "__main__":
    main()
