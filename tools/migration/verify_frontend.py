"""Compile historical layout probes and compare frozen C/Rust frontend observers."""
import argparse
import importlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
from verify_producer import ROOT, sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--wibo", type=Path)
    parser.add_argument("--layout-only", action="store_true")
    args = parser.parse_args()
    binary, reference = args.binary.resolve(), args.oracle_root.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix="frontend-", dir=args.output.resolve()))
    print(f"retained frontend ABI tree: {stage}", flush=True)
    sys.path.insert(0, str(reference / "tools"))
    compare = importlib.import_module("compare")
    loader = None
    if os.name != "nt":
        requested = args.wibo or os.environ.get("MWCC_WIBO")
        loader = Path(requested).resolve() if requested else reference / "build/tools/wibo"
        if not requested and not loader.is_file():
            loader = Path(shutil.which("wibo") or "missing-wibo")
        assert loader.is_file(), f"wibo loader missing: {loader}"
    def launch(path):
        return [path] if loader is None else [loader, path]
    frozen = (reference / "src/oracle/Hooks.c").read_text()
    c_reference = (frozen[frozen.index("/* 1.3.2 put three bytes"):frozen.index("#ifdef CAN_INLINE")]
        + frozen[frozen.index("static const char *callee_name("):frozen.index("/* Where the calls the inliner decides")]
        + frozen[frozen.index("Boolean CAN_INLINE("):frozen.index("\n#endif\n\n#if VERSION >= VERSION_GC_1_3", frozen.index("Boolean CAN_INLINE("))]
        + frozen[frozen.index("static int has_body("):frozen.index("void CInline_AddFunctionPrecNode(")]
        + frozen[frozen.index("Boolean DISPATCH(void)"):frozen.index("/* tp.emit:")])
    evidence = dict(complete=False, layout_only=args.layout_only, binary_sha256=sha(binary),
        reference_sha256=sha(reference / "src/oracle/Hooks.c"), versions=[],
        sources={p.relative_to(ROOT).as_posix(): sha(p) for p in [ROOT / "rust-toolchain.toml", ROOT / "crates/mwcc-oracle-hooks/Cargo.toml",
            ROOT / "crates/mwcc-oracle-hooks/src/lib.rs", ROOT / "crates/mwcc-oracle-hooks/src/frontend.rs", ROOT / "tools/migration/abi/frontend_layout.c"]})
    if loader: evidence["wibo_sha256"] = sha(loader)
    def save():
        (stage / "frontend-verification.json").write_text(json.dumps(evidence, indent=2) + "\n")
    for version, index in [("GC_1_3",5), ("GC_1_3_2",6), ("GC_2_7",7)]:
        project = stage / version
        (project / "src/oracle").mkdir(parents=True)
        shutil.copy2(reference / "src/oracle/Hooks.c", project / "src/oracle/Hooks.c")
        (project / "frontend_reference.h").write_text(c_reference)
        item = dict(version=version, runs=[], reference_sha256=sha(project / "frontend_reference.h"))
        evidence["versions"].append(item)
        def run(name, command, env=None):
            result = subprocess.run([str(c) for c in command], cwd=project, env=env, capture_output=True, timeout=180)
            for stream in ["stdout", "stderr"]: (project / f"{name}.{stream}").write_bytes(getattr(result, stream))
            item["runs"].append(dict(name=name, command=[str(c) for c in command], exit_code=result.returncode,
                stdout_sha256=sha(project / f"{name}.stdout"), stderr_sha256=sha(project / f"{name}.stderr")))
            save()
            assert result.returncode == 0, (version, name, result.returncode, result.stdout[-5000:], result.stderr[-5000:])
            if name == "cases":
                assert not result.stdout and not result.stderr, "ABI harness emitted a failure diagnostic"
        run("rust", [binary, "build-hooks", project / "frontend.obj", "--root", ROOT, "--frontend-version", version])
        item["frontend_object_sha256"] = sha(project / "frontend.obj")
        settings = json.loads((reference / "config/sources.json").read_text())["src/backend/Coloring.c"]
        config = json.loads((reference / f"config/{version}/config.json").read_text())
        compiler = reference / f"build/compilers/{config.get('compilers', {}).get(settings['compiler'], settings['compiler'])}/mwcc.exe"
        item.update(compiler_sha256=sha(compiler), compiler_flags=settings["flags"], linker_sha256=sha(reference / "build/compilers/pro53/mwld.exe"))
        env = dict(os.environ, MWCIncludes=str(reference / "include/libc"))
        compile = [*launch(compiler), *settings["flags"], f"-DVERSION={index}", "-I"+str(reference / "include"), "-I"+str(project), "-c"]
        run("layout", [*compile, ROOT / "tools/migration/abi/frontend_layout.c", "-o", project / "layout.obj"], env)
        syms, sects = compare.read_object(project / "layout.obj")
        field = next(s for s in syms.values() if s["name"] == "_RustAbiFrontendLayout")
        count = next(s for s in syms.values() if s["name"] == "_RustAbiFrontendCount")
        n = struct.unpack_from("<I", sects[count["section"]-1]["data"], count["value"])[0]
        values = struct.unpack_from(f"<{n}I", sects[field["section"]-1]["data"], field["value"])
        item["layout"] = values
        assert values == (2,1,6,4,10,4,14,4,18,4,42,4,0,4,4,4,22,4,0,1,2,2,14,4,14,4,18,4,18,4,18,4,22,4,0,4,4,4,12,2,4,4,34,1,2,1,4,1,61,62,30,69,53,56,6,16,128,114,104,213,160), values
        print(f"{version}: {n} historical layout probes pass", flush=True)
        if args.layout_only:
            save()
            continue
        runtime = ROOT / "tools/migration/abi/frontend_runtime.c"
        evidence["sources"][runtime.relative_to(ROOT).as_posix()] = sha(runtime)
        run("runtime", [*compile, runtime, "-o", project / "runtime.obj"], env)
        (project / "kernel.def").write_text("LIBRARY KERNEL32.dll\nEXPORTS\n\tExitProcess@4 @0\n\tGetStdHandle@4 @1\n\tWriteFile@20 @2\n")
        run("implib", [binary, "implib", project / "kernel.def", project / "kernel.lib"])
        run("link", [*launch(reference / "build/compilers/pro53/mwld.exe"), "-nostdlib", "-subsystem", "console", "-m", "_main",
            project / "runtime.obj", project / "frontend.obj", project / "kernel.lib", "-o", project / "runtime.exe"])
        item["runtime_sha256"] = sha(project / "runtime.exe")
        run("cases", launch(project / "runtime.exe"))
        item["cases"] = 256
        print(f"{version}: 256 C/Rust frontend ABI scenarios pass", flush=True)
        save()
    evidence["complete"] = True
    save()


if __name__ == "__main__":
    main()
