"""Compare native and pinned Python split/relink orchestration in isolated projects."""
import argparse
from contextlib import nullcontext
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


LIBRARY_OBJECTS = {"cexc.obj": "lib/runtime/cexc.obj", "ccinit.obj": "lib/runtime/ccinit.obj"}
VERSIONS = ["GC_1_1", "GC_1_1p1", "GC_1_2_5", "GC_1_2_5n", "GC_1_3", "GC_1_3_2", "GC_2_7"]


def copy(root, project, name):
    target = project / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(root / name, target)


def run(command, cwd):
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, timeout=600)
    if result.returncode:
        raise RuntimeError(f"{command[0]} failed ({result.returncode}):\n{result.stdout[-6000:]}\n{result.stderr[-6000:]}")
    return result


def image_bytes(path):
    # Oracle links retain the current linker timestamp. Compare every other byte.
    data = bytearray(path.read_bytes())
    stamp = struct.unpack_from("<I", data, 0x3c)[0] + 8
    data[stamp:stamp+4] = bytes(4)
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--version", action="append")
    parser.add_argument("--oracle", action="store_true")
    parser.add_argument("--compiled", choices=("exact", "size", "undefined"),
                        help="exercise fitting compiled objects or size/symbol fallback (GC 1.3.2 by default)")
    parser.add_argument("--keep", type=Path)
    parser.add_argument("--retain-builds", action="store_true", help="retain isolated link/runtime trees for ABI failure diagnosis")
    parser.add_argument("--diagnose-abi", action="store_true", help="print the first mismatching serialized ABI bytes")
    parser.add_argument("--rust-hook", type=Path, help="replace migrated C hook definitions with this Rust object")
    parser.add_argument("--compiled-oracle-root", type=Path, help="use current native oracle objects, including the separate Rust trace writer")
    parser.add_argument("--rust-frontend", action="store_true", help="link the per-version frontend object from --compiled-oracle-root instead of C observation hooks")
    parser.add_argument("--wibo", type=Path, help="explicit Linux loader, overriding MWCC_WIBO and automatic discovery")
    parser.add_argument("--canaries", type=Path, help="run all canaries with the Rust ABI spike before deleting its isolated link tree")
    args = parser.parse_args()
    root, binary = args.oracle_root.resolve(), args.binary.resolve()
    sys.path.insert(0, str(root / "tools"))
    split = importlib.import_module("split")
    compare = importlib.import_module("compare")
    reference_link = importlib.import_module("link")
    if args.rust_hook and not args.oracle:
        parser.error("--rust-hook requires --oracle")
    if args.compiled_oracle_root and not (args.oracle and args.rust_hook):
        parser.error("--compiled-oracle-root requires --oracle and --rust-hook")
    if args.rust_frontend and not args.compiled_oracle_root:
        parser.error("--rust-frontend requires --compiled-oracle-root")
    if args.compiled_oracle_root:
        args.compiled_oracle_root = args.compiled_oracle_root.resolve()
    if args.canaries and not args.rust_hook:
        parser.error("--canaries requires --rust-hook")
    if args.rust_hook:
        args.rust_hook = args.rust_hook.resolve()
        symbols, sections = compare.read_object(args.rust_hook)
        defined = {s["name"] for s in symbols.values() if s["section"] > 0 and s["storage"] == 2}
        undefined = {s["name"] for s in symbols.values() if s["section"] == 0 and s["storage"] == 2}
        graph_group = {"_fn_00460c90", "_fn_00507070", "_fn_00506f50"}
        pcode_group = graph_group | {"_fn_004f7fb0", "_schedule_block", "_select_ready_coloring_node",
            "_OracleRust_LinkName", "_OracleRust_PrintInstruction", "_OracleRust_Snapshot"}
        block_group = pcode_group | {"_fn_00436390", "_COptimizer_Optimize", "_Peephole_MergeAdjacentBlocks"}
        scalar_group = block_group | {"_fn_00454030", "_create_temp_object", "_fn_004601f0",
            "_fn_00460780", "_IRO_CommonSubs", "_move_common_sub", "_IroDump_Print",
            "_OracleRust_DescribeExpression", "_cse_vregs"}
        origin_group = scalar_group | {"_fn_00506cd0", "_generate_inline_statements", "_inline_statement",
            "_OracleRust_OriginFind", "_OracleRust_OriginWalk", "_OracleRust_PrintChain", "_OracleRust_InlineLine", "_OracleRust_BackendProbe", "_OracleRust_CounterProbeV2", "_OracleRust_ExpressionProbeV1"}
        instantiation_group = origin_group | {"_CInline_AddFunctionPrecNode", "_CInline_AddSpecialization",
            "_CTemplateNew_InstantiateInlineTemplateObject", "_CTemplateNew_InstantiateFunction", "_fn_0050f240", "_generate_inline_code"}
        binding_hooks = defined == instantiation_group | {"_setup_inline_locals_and_arguments"}
        instantiation_hooks = defined == instantiation_group or binding_hooks
        origin_hooks = defined == origin_group or instantiation_hooks
        scalar_hooks = defined == scalar_group or origin_hooks
        block_hooks = defined == block_group or scalar_hooks
        pcode_hooks = defined == pcode_group or block_hooks
        frame_hook = defined == graph_group | {"_fn_004f7fb0"} or pcode_hooks
        graph_hooks = defined == graph_group or frame_hook
        assert defined == {"_fn_00460c90"} or graph_hooks, defined
        expected = {"_fn_00460c90__retail", "_getenv", "_OracleTrace_Printf"}
        if graph_hooks:
            expected |= {"_fn_00507070__retail", "_fn_00506f50__retail", "_fn_004fcb70", "_fn_004ceda0", "_fn_004fcb20"}
        if frame_hook:
            expected |= {"_fn_004f7fb0__retail", "_locals", "_temporary_objects"}
        if pcode_hooks:
            expected |= {"_CMangler_GetLinkName", "_sprintf", "_gPCodeBlocks", "_gVirtualRegistersActive",
                         "_schedule_block__retail", "_select_ready_coloring_node__retail"}
        if block_hooks:
            expected |= {"_fn_00436390__retail", "_COptimizer_Optimize__retail", "_Peephole_MergeAdjacentBlocks__retail",
                         "_gCurrentStatement", "_gCurrentBlock"}
        if scalar_hooks:
            expected |= {"_fn_00454030__retail", "_create_temp_object__retail", "_fn_004601f0__retail",
                "_fn_00460780__retail", "_IRO_CommonSubs__retail", "_move_common_sub__retail", "_iro_dump_output",
                "_INT_005882b8", "_class_data_parts", "_iro_pointer_analysis", "_data_005875b8", "_linear_head",
                "_strncmp", "_fopen", "_vfprintf"}
        if origin_hooks:
            expected |= {"_fn_00506cd0__retail", "_generate_inline_statements__retail", "_inline_statement__retail", "_memset"}
        if instantiation_hooks:
            expected |= {"_CInline_AddFunctionPrecNode__retail", "_CInline_AddSpecialization__retail",
                "_CTemplateNew_InstantiateInlineTemplateObject__retail", "_CTemplateNew_InstantiateFunction__retail",
                "_fn_0050f240__retail", "_generate_inline_code__retail", "_pendingInlineWork", "_pending_prec_nodes",
                "_deferredInlineNodes", "_cscope_currentfunc", "_data_005dfb62"}
        if binding_hooks:
            expected |= {"_setup_inline_locals_and_arguments__retail", "_data_0058245a"}
        assert undefined == expected, undefined
        print(f"Rust ABI object: {len(sections)} sections, {len(defined)} exports, only the expected C imports", flush=True)
    if args.oracle and args.compiled:
        parser.error("--compiled exercises retail fallback; it cannot be combined with --oracle")
    versions = args.version or (["GC_1_3_2"] if args.compiled else
                               ["GC_1_3", "GC_1_3_2", "GC_2_7"] if args.oracle else VERSIONS)
    results = []
    builds = (nullcontext(tempfile.mkdtemp(prefix="link-parity-")) if args.retain_builds
              else tempfile.TemporaryDirectory(prefix="link-parity-"))
    with builds as temporary:
        if args.retain_builds:
            print(f"retained ABI/link tree: {temporary}", flush=True)
        for version in versions:
            config = json.loads((root / "config" / version / "config.json").read_text())
            original_relative = config["original"]
            sections, units = split.read_splits(root / "config" / version / "splits.txt")
            inputs = [f"build/{version}/obj/sections.obj"]
            if ".rsrc" in sections:
                inputs.append(f"build/{version}/obj/resources.res")
            oracle_config = config.get("oracle", {})
            for unit, _ in units:
                if args.oracle and unit in oracle_config.get("units", []):
                    inputs.append(f"build/{version}/oracle/compiled/{unit}.obj")
                else:
                    inputs.append(LIBRARY_OBJECTS.get(unit, f"build/{version}/obj/{unit}.obj"))
            imports = config.get("imports", ["lib/win32sdk/KERNEL32.LIB"])
            inputs += [f"build/{Path(p).stem}.lib" if p.endswith(".def") else p for p in imports]
            inputs.append("build/lmgr326b.lib")
            if args.oracle:
                inputs.append(f"build/{version}/oracle/OracleTrace.obj")
                inputs += [f"build/{version}/oracle/compiled/{u}.obj" for u in oracle_config.get("extra", [])
                           if not (args.rust_frontend and u == "src/oracle/Hooks.c")]
                if args.rust_frontend:
                    inputs.append(f"build/{version}/oracle/compiled/rust/frontend.obj")
            config["original"] = str((root / config["original"]).resolve())
            outputs = []
            logs = []
            selected_unit = None
            for backend in ("python", "rust"):
                project = Path(temporary) / version / backend
                config_dir = project / "config" / version
                config_dir.mkdir(parents=True)
                for name in ("symbols.txt", "splits.txt"):
                    copy(root, project, f"config/{version}/{name}")
                (config_dir / "config.json").write_text(json.dumps(config))
                for name in ("build/compilers/pro53/mwld.exe", "build/compilers/pro53/lmgr326b.dll",
                             "tools/lmgr_stub/lmgr326b.dll"):
                    if (root / name).is_file():
                        copy(root, project, name)
                if sys.platform != "win32":
                    launcher = project / "build/tools/wibo"
                    launcher.parent.mkdir(parents=True, exist_ok=True)
                    requested_loader = args.wibo or os.environ.get("MWCC_WIBO")
                    candidates = ([Path(requested_loader).resolve()] if requested_loader else
                        ([args.compiled_oracle_root / "build/tools/wibo"] if args.compiled_oracle_root else [])
                        + [root / "build/tools/wibo", Path(shutil.which("wibo") or "missing-wibo")])
                    loader = next((path for path in candidates if path.is_file()), None)
                    if loader is None:
                        raise RuntimeError(f"wibo loader missing: {candidates}")
                    shutil.copyfile(loader, launcher)
                    launcher.chmod(0o755)
                if backend == "python":
                    run([sys.executable, str(root / "tools/split.py"), version], project)
                else:
                    run([str(binary), "split", version, "--root", str(project)], project)
                for name in inputs:
                    if not name.startswith(f"build/{version}/obj/") and not name.startswith("build/"):
                        copy(root, project, name)
                    elif "/oracle/" in name:
                        copy(args.compiled_oracle_root or root, project, name)
                for definition in ["config/lmgr326b.def", *[p for p in imports if p.endswith(".def")]]:
                    output = project / "build" / (Path(definition).stem + ".lib")
                    if backend == "python":
                        run([sys.executable, str(root / "tools/implib.py"), str(root / definition), str(output)], project)
                    else:
                        run([str(binary), "implib", str(root / definition), str(output)], project)
                output = f"build/{version}/" + ("oracle/" if args.oracle else "") + "mwcceppc.exe"
                (project / output).parent.mkdir(parents=True, exist_ok=True)
                backend_inputs = inputs.copy()
                if args.rust_hook:
                    hook = f"build/{version}/oracle/compiled/src/oracle/Hooks.c.obj"
                    if not args.rust_frontend:
                        assert hook in backend_inputs
                        reference_link.undefine_symbols(project / hook, project / hook, defined)
                    rust = f"build/{version}/oracle/compiled/rust/lc_call.obj"
                    (project / rust).parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(args.rust_hook, project / rust)
                    if args.rust_frontend:
                        backend_inputs.insert(backend_inputs.index(f"build/{version}/oracle/compiled/rust/frontend.obj"), rust)
                    else:
                        backend_inputs.append(rust)
                if args.compiled:
                    if not config.get("validate_units"):
                        raise RuntimeError(f"{version}: compiled fallback requires validate_units")
                    for unit, _ in units:
                        if selected_unit is not None and unit != selected_unit:
                            continue
                        path = f"build/{version}/obj/{unit}.obj"
                        if path not in inputs:
                            continue
                        symbols, object_sections = compare.read_object(project / path)
                        used = {index for section in object_sections if not section["name"].startswith(".debug")
                                for _, index, _ in section["relocs"]}
                        undefined = next((index for index, symbol in symbols.items()
                                          if index in used and symbol["storage"] == 2
                                          and symbol["section"] == 0 and symbol["value"] == 0), None)
                        if undefined is None:
                            continue
                        selected_unit = unit
                        compiled = f"build/{version}/compiled/{unit}.obj"
                        data = bytearray((project / path).read_bytes())
                        if args.compiled == "size":
                            data = (Path(__file__).resolve().parents[2] / "tests/fixtures/binary/auxiliary.obj").read_bytes()
                        elif args.compiled == "undefined":
                            symbol_offset = struct.unpack_from("<I", data, 8)[0] + 18 * undefined
                            data[symbol_offset:symbol_offset+8] = b"_misgate"
                        (project / compiled).parent.mkdir(parents=True, exist_ok=True)
                        (project / compiled).write_bytes(data)
                        backend_inputs = [compiled if p == path else p for p in inputs]
                        break
                    if selected_unit is None:
                        raise RuntimeError(f"{version}: no unit with a referenced undefined symbol")
                command = ([sys.executable, str(root / "tools/link.py")] if backend == "python" else
                           [str(binary), "link", "--root", "."])
                logs.append(run([*command, *(["--oracle"] if args.oracle else []), version, output, *backend_inputs], project))
                outputs.append(project / output)
            python, native = outputs
            if args.oracle:
                expected, actual = image_bytes(python), image_bytes(native)
                # Hooked/supplement/patched COFF bytes must also agree before linking.
                for folder in ("hooked", "supplement", "patched"):
                    directories = [p.parent / folder for p in outputs]
                    trees = [{p.relative_to(d).as_posix(): p.read_bytes() for p in d.rglob("*") if p.is_file()}
                             for d in directories]
                    if trees[0] != trees[1]:
                        raise RuntimeError(f"{version}: {folder} objects differ")
            else:
                expected, actual = python.read_bytes(), native.read_bytes()
                if hashlib.sha1(actual).hexdigest() != config["sha1"]:
                    raise RuntimeError(f"{version}: retail SHA-1 differs")
                if (python.parent / "ok").read_bytes() != (native.parent / "ok").read_bytes():
                    raise RuntimeError(f"{version}: completion markers differ")
            if expected != actual:
                raise RuntimeError(f"{version}: linked images differ ({len(expected)} vs {len(actual)} bytes)")
            if logs[0].stdout != logs[1].stdout or logs[0].stderr != logs[1].stderr:
                raise RuntimeError(f"{version}: linker diagnostics differ: {logs[0].stderr!r} vs {logs[1].stderr!r}")
            if args.compiled == "undefined" and "1 compiled units are linked as split, for the symbols" not in logs[1].stderr:
                raise RuntimeError(f"{version}: undefined-symbol fallback did not execute")
            if args.compiled == "size" and "1 compiled units do not fit" not in logs[1].stderr:
                raise RuntimeError(f"{version}: size fallback did not execute")
            if args.compiled == "exact" and "1 of " not in (native.parent / "ok").read_text():
                raise RuntimeError(f"{version}: fitting compiled unit was not accepted")
            result = {"version": version, "oracle": args.oracle, "bytes": len(actual), "identical": True,
                      "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                      "sha256": hashlib.sha256(actual).hexdigest(),
                      "timestamp_normalized": args.oracle, "compiled_case": args.compiled,
                      "compiled_unit": selected_unit}
            if args.compiled_oracle_root:
                result["compiled_oracle_root"] = str(args.compiled_oracle_root)
                result["compiled_oracle_inputs"] = {name: hashlib.sha256((args.compiled_oracle_root / name).read_bytes()).hexdigest()
                                                   for name in inputs if "/oracle/" in name}
                if args.rust_frontend:
                    result["frontend_object_sha256"] = hashlib.sha256((args.compiled_oracle_root / f"build/{version}/oracle/compiled/rust/frontend.obj").read_bytes()).hexdigest()
            if args.rust_hook:
                result["rust_hook_sha256"] = hashlib.sha256(args.rust_hook.read_bytes()).hexdigest()
                # Prove the packed prefix and unsigned-char return assumptions with
                # the historical source compiler used by this version.
                layout = project / "layout.c"
                layout.write_text('#include <stddef.h>\n#include "compiler/IroLoop.h"\n'
                    'unsigned long RustAbiLayout[] = {sizeof(Boolean), sizeof(void *), sizeof(IROLinear), '
                    'offsetof(IROLinear, index), offsetof(IROLinear, flags)};\n')
                source_config = json.loads((root / "config/sources.json").read_text())["src/backend/Coloring.c"]
                compiler = config.get("compilers", {}).get(source_config["compiler"], source_config["compiler"])
                executable = root / f"build/compilers/{compiler}/mwcc.exe"
                launch = [str(executable)] if sys.platform == "win32" else [str(project / "build/tools/wibo"), str(executable)]
                flags = source_config["flags"]
                env = os.environ.copy()
                env["MWCIncludes"] = str(root / "include/libc")
                version_index = ["GC_1_0", "GC_1_1", "GC_1_1p1", "GC_1_2_5", "GC_1_2_5n", "GC_1_3", "GC_1_3_2", "GC_2_7"].index(version)
                checked = subprocess.run([*launch, *flags, f"-DVERSION={version_index}", "-I" + str(root / "include"),
                    "-c", str(layout), "-o", str(project / "layout.obj")], cwd=project, env=env, capture_output=True, text=True, timeout=60)
                assert checked.returncode == 0, checked.stdout + checked.stderr
                layout_symbols, layout_sections = compare.read_object(project / "layout.obj")
                symbol = next(s for s in layout_symbols.values() if s["name"] == "_RustAbiLayout")
                section = layout_sections[symbol["section"] - 1]
                values = struct.unpack_from("<5I", section["data"], symbol["value"])
                assert values[:2] == (1, 4) and values[3:] == (8, 2), values
                result["c_layout"] = list(values)
                if graph_hooks:
                    probe = project / "graph-layout.c"
                    copy(root, project, "src/oracle/Hooks.c")
                    probe.write_text('#include <stddef.h>\n#define MWCC_ORACLE_TRACE 1\n#include "'
                        + 'src/oracle/Hooks.c"\n'
                        + 'struct RustAbiNodeAlignment { char tag; OracleNode node; };\n'
                        + 'unsigned long RustAbiGraphLayout[] = {sizeof(OracleNode), '
                        + ', '.join(f'offsetof(OracleNode, {field})' for field in ["next", "f4", "f8", "spill_cost", "virtual_register", "degree", "color", "flags", "neighbor_count", "neighbors"])
                        + ', offsetof(struct RustAbiNodeAlignment, node)};\n')
                    checked = subprocess.run([*launch, *flags, f"-DVERSION={version_index}", "-I" + str(root / "include"),
                        "-c", str(probe), "-o", str(project / "graph-layout.obj")], cwd=project, env=env, capture_output=True, text=True, timeout=60)
                    assert checked.returncode == 0, checked.stdout + checked.stderr
                    symbols, sections = compare.read_object(project / "graph-layout.obj")
                    symbol = next(s for s in symbols.values() if s["name"] == "_RustAbiGraphLayout")
                    values = struct.unpack_from("<12I", sections[symbol["section"] - 1]["data"], symbol["value"])
                    assert values == (28, 0, 4, 8, 12, 16, 18, 20, 22, 24, 26, 2), values
                    result["c_graph_layout"] = list(values)
                if frame_hook:
                    probe = Path(__file__).resolve().parent / "abi/frame_layout.c"
                    checked = subprocess.run([*launch, *flags, f"-DVERSION={version_index}", "-I" + str(root / "include"),
                        "-c", str(probe), "-o", str(project / "frame-layout.obj")], cwd=project, env=env, capture_output=True, text=True, timeout=60)
                    assert checked.returncode == 0, checked.stdout + checked.stderr
                    symbols, sections = compare.read_object(project / "frame-layout.obj")
                    symbol = next(s for s in symbols.values() if s["name"] == "_RustAbiFrameLayout")
                    values = struct.unpack_from("<33I", sections[symbol["section"] - 1]["data"], symbol["value"])
                    assert values == (4, 1, 8, 0, 4, 2, 8, 0, 4, 1, 68, 2, 1, 10, 14, 42, 46, 4, 2,
                                      12, 10, 2, 2, 6, 2, 4, 2, 42, 38, 2, 35, 1, 2), values
                    result["c_frame_layout"] = list(values)
                if pcode_hooks:
                    probe = Path(__file__).resolve().parent / "abi/pcode_layout.c"
                    checked = subprocess.run([*launch, *flags, f"-DVERSION={version_index}", "-I" + str(root / "include"),
                        "-c", str(probe), "-o", str(project / "pcode-layout.obj")], cwd=project, env=env, capture_output=True, text=True, timeout=60)
                    assert checked.returncode == 0, checked.stdout + checked.stderr
                    symbols, sections = compare.read_object(project / "pcode-layout.obj")
                    symbol = next(s for s in symbols.values() if s["name"] == "_RustAbiPCodeLayout")
                    values = struct.unpack_from("<76I", sections[symbol["section"] - 1]["data"], symbol["value"])
                    assert values == (66, 2, 0, 4, 32, 2, 20, 4, 34, 2, 28, 4, 36, 12, 48, 2, 0, 4, 20, 4, 28, 4,
                        36, 4, 42, 2, 16, 4, 8, 2, 0, 4, 4, 4, 26, 2, 0, 4, 8, 4, 12, 4, 16, 2, 18, 2, 20, 2,
                        22, 2, 24, 2, 10, 2, 0, 4, 4, 4, 8, 2, 40, 2, 20, 4, 2, 1, 10, 4, 42, 4, 50, 4, 10, 3, 4, 6), values
                    result["c_pcode_layout"] = list(values)
                    frozen = (root / "src/oracle/Hooks.c").read_text()
                    helpers = frozen[frozen.index("static const char *link_name("):frozen.index("static void snapshot(")]
                    wanted = frozen[frozen.index("static int wanted("):frozen.index("#if VERSION >= VERSION_GC_1_3", frozen.index("static int wanted("))]
                    scheduler = frozen[frozen.index("extern void schedule_block__retail("):frozen.index("\n#endif", frozen.index("extern void schedule_block__retail("))]
                    reference_source = helpers + wanted + scheduler
                    (project / "pcode_reference.h").write_text(reference_source)
                    result["c_pcode_reference_sha256"] = hashlib.sha256(reference_source.encode()).hexdigest()
                if block_hooks:
                    probe = Path(__file__).resolve().parent / "abi/blocks_layout.c"
                    checked = subprocess.run([*launch, *flags, f"-DVERSION={version_index}", "-I" + str(root / "include"),
                        "-c", str(probe), "-o", str(project / "blocks-layout.obj")], cwd=project, env=env, capture_output=True, text=True, timeout=60)
                    assert checked.returncode == 0, checked.stdout + checked.stderr
                    symbols, sections = compare.read_object(project / "blocks-layout.obj")
                    symbol = next(s for s in symbols.values() if s["name"] == "_RustAbiBlocksLayout")
                    values = struct.unpack_from("<18I", sections[symbol["section"] - 1]["data"], symbol["value"])
                    assert values == (30, 2, 22, 4, 40, 2, 24, 12, 42, 2, 20, 4, 32, 2, 1, 264, 1, 0), values
                    result["c_blocks_layout"] = list(values)
                    maybe = frozen[frozen.index("static const char *maybe_link("):frozen.index("static void tp_context(")]
                    block_source = maybe + frozen[frozen.index("extern struct Statement *gCurrentStatement;"):frozen.index("/* ce.*:")]
                    (project / "blocks_reference.h").write_text(block_source)
                    result["c_blocks_reference_sha256"] = hashlib.sha256(block_source.encode()).hexdigest()
                if scalar_hooks:
                    probe = Path(__file__).resolve().parent / "abi/scalar_cse_layout.c"
                    checked = subprocess.run([*launch, *flags, f"-DVERSION={version_index}", "-I" + str(root / "include"),
                        "-c", str(probe), "-o", str(project / "scalar-cse-layout.obj")], cwd=project, env=env, capture_output=True, text=True, timeout=60)
                    assert checked.returncode == 0, checked.stdout + checked.stderr
                    symbols, sections = compare.read_object(project / "scalar-cse-layout.obj")
                    symbol = next(s for s in symbols.values() if s["name"] == "_RustAbiScalarCseLayout")
                    values = struct.unpack_from("<92I", sections[symbol["section"] - 1]["data"], symbol["value"])
                    assert values == (56,0,1,1,1,8,2,10,4,18,4,32,4,36,4,52,4,
                        42,2,2,4,4,8,4,30,0,1,14,4,14,4,18,4,14,4,18,4,18,4,
                        8,4,4,0,4,42,4,1,6,4,18,26,4,30,4,34,4,34,2,4,11,1,
                        8,4,4,22,4,38,2,37,1,34,1,6,0,1,2,4,1,4,1,2,3,56,4,8,48,15,41,54,50,9,1), values
                    result["c_scalar_cse_layout"] = list(values)
                    reference_source = (
                        frozen[frozen.index("extern FILE *iro_dump_output;"):frozen.index("/* lc.*:")]
                        + frozen[frozen.index("static Boolean in_scalarize;"):frozen.index("/* lc.call:")]
                        + frozen[frozen.index("static void describe_expression("):frozen.index("extern ENode *setup_inline_locals_and_arguments__retail")]
                        + frozen[frozen.index("extern struct IROLinear *linear_head;"):frozen.rindex("\n#endif\n\n#endif")])
                    (project / "scalar_cse_reference.h").write_text(reference_source)
                    result["c_scalar_cse_reference_sha256"] = hashlib.sha256(reference_source.encode()).hexdigest()
                if origin_hooks:
                    probe = Path(__file__).resolve().parent / "abi/origins_layout.c"
                    checked = subprocess.run([*launch, *flags, f"-DVERSION={version_index}", "-I" + str(root / "include"), "-I" + str(project),
                        "-c", str(probe), "-o", str(project / "origins-layout.obj")], cwd=project, env=env, capture_output=True, text=True, timeout=60)
                    assert checked.returncode == 0, checked.stdout + checked.stderr
                    symbols, sections = compare.read_object(project / "origins-layout.obj")
                    symbol = next(s for s in symbols.values() if s["name"] == "_RustAbiOriginsLayout")
                    count = next(s for s in symbols.values() if s["name"] == "_RustAbiOriginsCount")
                    assert struct.unpack_from("<I", sections[count["section"] - 1]["data"], count["value"])[0] == 104
                    values = struct.unpack_from("<104I", sections[symbol["section"] - 1]["data"], symbol["value"])
                    assert values == (12,2,0,4,4,4,8,4,65536,786432,30,0,1,14,4,18,4,14,4,18,4,22,4,14,4,18,4,
                        8,0,4,4,4,30,0,4,4,1,10,4,22,4,0,1,2,3,4,5,6,7,8,48,49,
                        *range(9,48),58,57,53,76,54,55,56,4,5,6,7,8,15), values
                    result["c_origins_layout"] = list(values)
                    start = frozen.index("void ALLOCATE_REGISTERS(Object *function)")
                    allocation = frozen[start:frozen.index("\n}", start) + 3]
                    reference_source = (frozen[frozen.index("#define ORIGIN_SIZE "):frozen.index("/* inl.can:")]
                        + "\n" + allocation)
                    (project / "origins_reference.h").write_text(reference_source)
                    result["c_origins_reference_sha256"] = hashlib.sha256(reference_source.encode()).hexdigest()
                if instantiation_hooks:
                    probe = Path(__file__).resolve().parent / "abi/instantiation_layout.c"
                    checked = subprocess.run([*launch, *flags, f"-DVERSION={version_index}", "-I" + str(root / "include"), "-I" + str(project),
                        "-c", str(probe), "-o", str(project / "instantiation-layout.obj")], cwd=project, env=env, capture_output=True, text=True, timeout=60)
                    assert checked.returncode == 0, checked.stdout + checked.stderr
                    symbols, sections = compare.read_object(project / "instantiation-layout.obj")
                    symbol = next(s for s in symbols.values() if s["name"] == "_RustAbiInstantiationLayout")
                    values = struct.unpack_from("<14I", sections[symbol["section"] - 1]["data"], symbol["value"])
                    assert values == (16,2,4,4,0,4,1,1,0xffffffff,4,4,4,4,4), values
                    result["c_instantiation_layout"] = list(values)
                    reference_source = (frozen[frozen.index("static void tp_context(void)"):frozen.index("static int has_body(")]
                        + frozen[frozen.index("void CInline_AddFunctionPrecNode("):frozen.index("Boolean DISPATCH(void)")]
                        + frozen[frozen.index("extern void fn_0050f240__retail("):frozen.index("/* The optimizer's own dump")])
                    (project / "instantiation_reference.h").write_text(reference_source)
                    result["c_instantiation_reference_sha256"] = hashlib.sha256(reference_source.encode()).hexdigest()
                if binding_hooks:
                    probe = Path(__file__).resolve().parent / "abi/bindings_layout.c"
                    checked = subprocess.run([*launch, *flags, f"-DVERSION={version_index}", "-I" + str(root / "include"),
                        "-c", str(probe), "-o", str(project / "bindings-layout.obj")], cwd=project, env=env, capture_output=True, text=True, timeout=60)
                    assert checked.returncode == 0, checked.stdout + checked.stderr
                    symbols, sections = compare.read_object(project / "bindings-layout.obj")
                    symbol = next(s for s in symbols.values() if s["name"] == "_RustAbiBindingsLayout")
                    count = next(s for s in symbols.values() if s["name"] == "_RustAbiBindingsCount")
                    assert struct.unpack_from("<I", sections[count["section"] - 1]["data"], count["value"])[0] == 44
                    values = struct.unpack_from("<44I", sections[symbol["section"] - 1]["data"], symbol["value"])
                    assert values == (0,2,2,4,12,2,0,4,4,4,8,4,16,2,0,4,4,4,13,1,14,1,6,4,22,4,0,4,4,4,12,4,10,4,6,11,32,256,41,8,48,54,56,4), values
                    result["c_bindings_layout"] = list(values)
                    reference_source = frozen[frozen.index("ENode *setup_inline_locals_and_arguments("):frozen.index("/* bk.*:")]
                    (project / "bindings_reference.h").write_text(reference_source)
                    result["c_bindings_reference_sha256"] = hashlib.sha256(reference_source.encode()).hexdigest()
                runtime_source = Path(__file__).resolve().parent / "abi/runtime.c"
                result["abi_sources_sha256"] = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in sorted(runtime_source.parent.glob("*.c"))}
                checked = subprocess.run([*launch, *flags, f"-DVERSION={version_index}", *(["-DRUST_ABI_GRAPH_HOOKS"] if graph_hooks else []), *(["-DRUST_ABI_FRAME_HOOK"] if frame_hook else []), *(["-DRUST_ABI_PCODE"] if pcode_hooks else []), *(["-DRUST_ABI_BLOCKS"] if block_hooks else []), *(["-DRUST_ABI_SCALAR_CSE"] if scalar_hooks else []), *(["-DRUST_ABI_ORIGINS"] if origin_hooks else []), *(["-DRUST_ABI_DIAGNOSTICS"] if args.diagnose_abi else []), "-I" + str(root / "include"), "-I" + str(project), "-I" + str(runtime_source.parent),
                    *(["-DRUST_ABI_INSTANTIATION"] if instantiation_hooks else []), *(["-DRUST_ABI_BINDINGS"] if binding_hooks else []), "-c", str(runtime_source), "-o", str(project / "runtime.obj")], cwd=project, env=env, capture_output=True, text=True, timeout=60)
                assert checked.returncode == 0, checked.stdout + checked.stderr
                (project / "kernel.def").write_text("LIBRARY KERNEL32.dll\nEXPORTS\n\tExitProcess@4 @0\n" +
                    ("\tGetStdHandle@4 @1\n\tWriteFile@20 @2\n" if args.diagnose_abi else ""))
                run([str(binary), "implib", str(project / "kernel.def"), str(project / "kernel.lib")], project)
                linker = project / "build/compilers/pro53/mwld.exe"
                link_launch = [str(linker)] if sys.platform == "win32" else [str(project / "build/tools/wibo"), str(linker)]
                run([*link_launch, "-nostdlib", "-subsystem", "console", "-m", "_main", "runtime.obj",
                     str(args.rust_hook), "kernel.lib", "-o", "runtime.exe"], project)
                execute = [str(project / "runtime.exe")] if sys.platform == "win32" else [str(project / "build/tools/wibo"), str(project / "runtime.exe")]
                run(execute, project)
                result["direct_abi_cases"] = 448 if binding_hooks else 376 if instantiation_hooks else 280 if origin_hooks else 160 if scalar_hooks else 72 if block_hooks else 30 if pcode_hooks else 12 if frame_hook else 9 if graph_hooks else 7
                if args.canaries:
                    copy(root, project, original_relative)
                    license_file = str(Path(original_relative).with_name("lmgr326b.dll"))
                    if (root / license_file).is_file():
                        copy(root, project, license_file)
                    # Validate the isolated execution environment before the full corpus.
                    run([str(binary), "corpus", "--oracle-root", str(project), "--version", version,
                        "--canaries", str(args.canaries.resolve()), "--all-traces", "--limit", "1", "--jobs", "1", "--json"], project)
                    canaries = run([str(binary), "corpus", "--oracle-root", str(project), "--version", version,
                        "--canaries", str(args.canaries.resolve()), "--all-traces", "--jobs", "8", "--json"], project)
                    report = json.loads(canaries.stdout)
                    assert report["identical"] == report["total"] and report["total"] > 0
                    result["canaries"] = {key: report[key] for key in ("identical", "total")}
            results.append(result)
            if args.keep:
                args.keep.parent.mkdir(parents=True, exist_ok=True)
                args.keep.write_text(json.dumps(results, indent=2)+"\n")
            print(f"{version}: {'oracle (timestamp normalized)' if args.oracle else 'retail'} link agrees, {len(actual)} bytes", flush=True)


if __name__ == "__main__":
    main()
