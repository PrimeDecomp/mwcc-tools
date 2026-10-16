"""Build the hybrid producer natively and compare migrated trace records with frozen C hooks."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def graph_trace(trace, include_unknown=False):
    """Canonicalize observed identities, including typed PCode pointer fields only."""
    pointers = {}
    pcode_pointers = {}
    function = None
    pending_statements = False
    result = []
    def pointer(value):
        if value == "00000000":
            return value
        if value not in pcode_pointers:
            pcode_pointers[value] = f"pcode#{len(pcode_pointers)}"
        return pcode_pointers[value]
    def pcode_identity(match):
        return pointer(match.group(1))
    def operand(raw):
        data = bytes.fromhex(raw)
        assert len(data) == 12, raw
        # PCode.h's register/alternate views have Object* at +6; label values
        # have PCodeLabel* at +2. Kind 7 is the separate label-difference view.
        offsets = ([6] if data[0] in (0, 2, 3, 4) else [])
        if data[0] == 4:
            offsets.append(2)
        if data[0] == 7:
            offsets = [4, 8]
        output, start = [], 0
        for offset in sorted(offsets):
            output.append(data[start:offset].hex())
            value = int.from_bytes(data[offset:offset+4], "little")
            output.append("[" + pointer(f"{value:08x}") + "]")
            start = offset + 4
        output.append(data[start:].hex())
        return "".join(output)
    def identity(match):
        value = match.group(1)
        if value not in pointers:
            pointers[value] = f"node#{len(pointers)}"
        return pointers[value]
    for line in trace.splitlines():
        if line.startswith("bk.stmt ") and not pending_statements:
            pcode_pointers.clear()
            function = None
            pending_statements = True
        if line.startswith("bk.list phase=raw "):
            context = line.split(" link=", 1)[1]
            if not pending_statements and function != context:
                pcode_pointers.clear()
            function = context
            pending_statements = False
        if line.startswith(("st.codegen ", "st.optimized ", "ra.before ", "ra.after ")):
            context = line.split(" link=", 1)[1]
            if function != context:
                pcode_pointers.clear()
                function = context
        if line.startswith("ra.before "):
            pointers.clear()
            result.append(line)
        elif line.startswith(("st.codegen ", "st.optimized ", "ra.after ", "ra.end", "st.end")):
            result.append(line)
        elif line.startswith(("ra.i ", "st.i ", "sc.i ", "bk.i ")):
            head, operands = line.split(" ops=", 1)
            head = re.sub(r"(?<=p=)([0-9a-f]{8})(?= )", pcode_identity, head)
            result.append(head + " ops=" + re.sub(r"\S+", lambda match: operand(match.group()), operands))
        elif line.startswith(("ra.block ", "st.block ", "sc.block ", "bk.block ")):
            line = re.sub(r"(?<=p=)([0-9a-f]{8})(?= )", pcode_identity, line)
            if " succ=" in line:
                head, values = line.split(" succ=", 1)
                line = head + " succ=" + ",".join(pointer(value) if value else "" for value in values.split(","))
            result.append(line)
        elif line.startswith("bk.stmt "):
            result.append(re.sub(r"((?:before|after)=)([0-9a-f]{8})(?=:)",
                lambda match: match.group(1) + pointer(match.group(2)), line))
        elif line.startswith("bk.pair "):
            result.append(re.sub(r"(?<=[ab]=)([0-9a-f]{8})(?= )", pcode_identity, line))
        elif line.startswith(("bk.list ", "bk.merge ")):
            result.append(line)
        elif line.startswith(("sc.node ", "sc.pick ")):
            line = re.sub(r"(?<=i=)([0-9a-f]{8})(?= )", pcode_identity, line)
            line = re.sub(r"([0-9a-f]{8})(?=:)", pcode_identity, line)
            line = re.sub(r"(?<=pick=)([0-9a-f]{8})$", pcode_identity, line)
            result.append(line)
        elif line.startswith("sc.after order="):
            head, values = line.split("order=", 1)
            result.append(head + "order=" + ",".join(pointer(value) if value else "" for value in values.split(",")))
        elif line.startswith("fr."):
            result.append(line)
        elif line.startswith(("lc.", "ce.", "pp.", "inl.")):
            result.append(line)
        elif line.startswith(("ra.graph ", "ra.node ", "ra.order ", "ra.select ", "lc.call ")):
            line = re.sub(r"(?<=p=)([0-9a-f]{8})(?= )", identity, line)
            line = re.sub(r"([0-9a-f]{8})(?=:)", identity, line)
            result.append(line)
        elif include_unknown:
            result.append(line)
    return result


def full_trace(trace):
    """Compare every output line; only the already typed identities are remapped."""
    result = graph_trace(trace, include_unknown=True)
    assert len(result) == len(trace.splitlines()), "trace comparison dropped output records"
    return result


def trace_line_endings(trace):
    return [whole[len(body):] for body, whole in zip(trace.splitlines(), trace.splitlines(keepends=True))]


def known_schedule_candidates(lines):
    count = 0
    for line in lines:
        if not line.startswith("sc.pick "):
            continue
        ready = line.split(" ready=", 1)[1].split(" pick=", 1)[0]
        for candidate in ready.split(","):
            if candidate:
                fields = candidate.split(":")
                assert len(fields) == 7, candidate
                count += int(int(fields[-2]) >= 0 and int(fields[-1]) >= 0)
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--existing-root", type=Path)
    parser.add_argument("--skip-build", action="store_true", help="compare an already built --existing-root without changing its build files")
    parser.add_argument("--version", action="append")
    parser.add_argument("--canaries", type=Path)
    parser.add_argument("--rustc", type=Path)
    parser.add_argument("--wibo", type=Path, help="Linux loader to copy into a new proof tree; overrides MWCC_WIBO and reference/PATH discovery")
    parser.add_argument("--from-ninja", type=Path)
    parser.add_argument("--source")
    parser.add_argument("--project-version", default="GC_2_7")
    parser.add_argument("--project-only", action="store_true")
    parser.add_argument("--evidence-name", default="producer-verification.json")
    args = parser.parse_args()
    if args.skip_build and not args.existing_root:
        parser.error("--skip-build requires --existing-root")
    if args.existing_root and args.wibo:
        parser.error("existing proof trees use their private build/tools/wibo; --wibo selects a loader for a new tree")
    binary, reference = args.binary.resolve(), args.oracle_root.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    stage = args.existing_root.resolve() if args.existing_root else Path(tempfile.mkdtemp(prefix="rust-producer-", dir=args.output.resolve()))
    if not args.existing_root:
        for directory in ["config", "include", "src", "crates/mwcc-oracle-hooks"]:
            shutil.copytree(ROOT / directory, stage / directory)
        shutil.copy2(ROOT / "rust-toolchain.toml", stage / "rust-toolchain.toml")
        for directory in ["lib", "build/compilers", "tools/lmgr_stub"]:
            shutil.copytree(reference / directory, stage / directory)
        if os.name != "nt":
            (stage / "build/tools").mkdir(parents=True)
            requested_loader = args.wibo or os.environ.get("MWCC_WIBO")
            loader = Path(requested_loader).resolve() if requested_loader else reference / "build/tools/wibo"
            if not requested_loader and not loader.is_file():
                discovered = shutil.which("wibo")
                if not discovered:
                    raise RuntimeError("wibo not found; set --wibo or MWCC_WIBO")
                loader = Path(discovered)
            if not loader.is_file():
                raise RuntimeError(f"requested wibo loader missing: {loader}")
            shutil.copy2(loader, stage / "build/tools/wibo")
            (stage / "build/tools/wibo").chmod(0o755)
    env = os.environ.copy()
    if args.rustc:
        env["RUSTC"] = str(args.rustc.resolve())
    if os.name != "nt":
        # Use the same locally pinned loader for the frozen C and hybrid roots;
        # a global old wibo may lack APIs needed by project flags such as -multibyte.
        env["MWCC_WIBO"] = str(stage / "build/tools/wibo")
    evidence = dict(binary_sha256=sha(binary), reference_root=str(reference), native_build=not args.skip_build,
                    producer_selection="existing-tree" if args.skip_build else "default-rust", versions=[])
    evidence_path = stage / args.evidence_name
    if evidence_path.exists():
        previous = json.loads(evidence_path.read_text(encoding="utf-8"))
        evidence["previous_runs"] = [*previous.get("previous_runs", []),
                                    {key: value for key, value in previous.items() if key != "previous_runs"}]
    print(f"retained hybrid producer tree: {stage}", flush=True)

    def save():
        evidence_path.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")

    def run(name, command, *, json_output=False):
        result = subprocess.run([str(a) for a in command], cwd=stage, env=env, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=900)
        prefix = stage / name
        suffix = 1
        while prefix.with_name(prefix.name + ".stdout").exists():
            suffix += 1
            prefix = stage / f"{name}-{suffix}"
        stdout = prefix.with_name(prefix.name + ".stdout")
        stderr = prefix.with_name(prefix.name + ".stderr")
        stdout.write_text(result.stdout, encoding="utf-8")
        stderr.write_text(result.stderr, encoding="utf-8")
        evidence.setdefault("logs", []).append(dict(name=name, stdout=stdout.name, stderr=stderr.name,
            exit_code=result.returncode, stdout_sha256=sha(stdout), stderr_sha256=sha(stderr)))
        save()
        if result.returncode:
            raise RuntimeError(f"{name}: exit {result.returncode}\n{result.stdout[-5000:]}\n{result.stderr[-5000:]}")
        return json.loads(result.stdout) if json_output else result.stdout

    spec = importlib.util.spec_from_file_location("reference_coff", reference / "tools/compare.py")
    module = importlib.util.module_from_spec(spec)
    import sys
    sys.path.insert(0, str(reference / "tools"))
    spec.loader.exec_module(module)
    fixtures = ROOT / "tests/fixtures/oracle"
    cases = json.loads((fixtures / "manifest.json").read_text())["cases"]
    # A C shim has the same public API/imports. Compare the linked object to a
    # fresh standalone Rust build as well, rather than inferring ownership from
    # symbol names. This never changes existing build outputs in --skip-build.
    trace_check = Path(tempfile.mkdtemp(prefix="trace-object-check-", dir=stage)) / "OracleTrace.obj"
    run("validate-trace-object", [binary, "build-hooks", trace_check, "--root", ROOT, "--trace-shim"])
    evidence["trace_reference"] = dict(path=str(trace_check.relative_to(stage)), sha256=sha(trace_check))
    save()
    for version in args.version or ["GC_1_3", "GC_1_3_2", "GC_2_7"]:
        target = f"build/{version}/oracle/mwcceppc.exe"
        if not args.skip_build:
            run(f"configure-{version}", [binary, "configure", "--root", stage, "--version", version, "--oracle"])
            ninja = (stage / "build.ninja").read_text()
            assert "--oracle --producer rust" in ninja
            assert "compiled/src/oracle/Hooks.c.obj" not in ninja
            assert "OracleTrace.obj: mwcc" not in ninja
            run(f"build-{version}", ["ninja", "-j", "8", target])
        if os.name != "nt":
            evidence["wibo_sha256"] = sha(stage / "build/tools/wibo")
        rust_object = stage / f"build/{version}/oracle/compiled/rust/hooks.obj"
        c_object = stage / f"build/{version}/oracle/compiled/src/oracle/Hooks.c.obj"
        frontend_object = stage / f"build/{version}/oracle/compiled/rust/frontend.obj"
        frontend_names = {
            "GC_1_3": {"_can_inline", "_inline_call_expression", "_CInline_DispatchNextDeferredNode"},
            "GC_1_3_2": {"_fn_0055ed70", "_fn_0055f350", "_fn_0055a7f0"},
            "GC_2_7": {"_fn_00560c80", "_fn_00561260", "_fn_0055c730"}}[version] | {"_fn_0055e540", "_fn_0055e600"}
        migrated = {"_fn_00460c90", "_fn_00507070", "_fn_00506f50", "_fn_004f7fb0", "_schedule_block", "_select_ready_coloring_node",
                    "_fn_00436390", "_COptimizer_Optimize", "_Peephole_MergeAdjacentBlocks", "_fn_00454030", "_create_temp_object",
                    "_fn_004601f0", "_fn_00460780", "_IRO_CommonSubs", "_move_common_sub", "_IroDump_Print",
                    "_fn_00506cd0", "_generate_inline_statements", "_inline_statement",
                    "_CInline_AddFunctionPrecNode", "_CInline_AddSpecialization", "_CTemplateNew_InstantiateInlineTemplateObject",
                    "_CTemplateNew_InstantiateFunction", "_fn_0050f240", "_generate_inline_code", "_setup_inline_locals_and_arguments"}
        support = {"_OracleRust_LinkName", "_OracleRust_PrintInstruction", "_OracleRust_Snapshot", "_OracleRust_DescribeExpression", "_cse_vregs",
                   "_OracleRust_OriginFind", "_OracleRust_OriginWalk", "_OracleRust_PrintChain", "_OracleRust_InlineLine", "_OracleRust_BackendProbe", "_OracleRust_CounterProbeV5", "_OracleRust_ExpressionProbeV1"}
        assert not c_object.exists(), "C observation hooks still compiled in Rust producer"
        for path, expected in [(rust_object, migrated | support), (frontend_object, frontend_names)]:
            symbols, _ = module.read_object(path)
            defined = {s["name"] for s in symbols.values() if s["section"] > 0 and s["storage"] == 2}
            assert defined == expected, (path, defined)
        config = json.loads((stage / f"config/{version}/config.json").read_text())
        assert set(config["oracle"]["hooks"]) == migrated | frontend_names
        check_frontend = trace_check.parent / f"{version}-frontend.obj"
        run(f"validate-frontend-{version}", [binary, "build-hooks", check_frontend, "--root", ROOT, "--frontend-version", version])
        assert sha(check_frontend) == sha(frontend_object)
        trace_object = stage / f"build/{version}/oracle/OracleTrace.obj"
        assert sha(trace_object) == sha(trace_check), "trace output is not the current independently built Rust writer"
        trace_symbols, _ = module.read_object(trace_object)
        assert {s["name"] for s in trace_symbols.values() if s["section"] > 0 and s["storage"] == 2} == {"_OracleTrace_Enabled", "_OracleTrace_Printf"}
        if not args.skip_build:
            assert f"build build/{version}/oracle/OracleTrace.obj: rust_trace " in (stage / "build.ninja").read_text()
        item = dict(version=version, producer_sha256=sha(stage / target), rust_object_sha256=sha(rust_object),
                    trace_object_sha256=sha(trace_object), frontend_object_sha256=sha(frontend_object),
                    frontend_reference=str(check_frontend.relative_to(stage)), hook_inventory_count=len(migrated | frontend_names), cases=[])
        evidence["versions"].append(item)
        save()
        print(f"{version}: {'retained' if args.skip_build else 'native'} hybrid build and C/Rust definition ownership pass", flush=True)
        records = 0
        frame_records = 0
        layout_records = 0
        pcode_records = schedule_records = 0
        known_candidates = 0
        block_records = 0
        scalar_records = cse_records = propagation_records = 0
        inline_records = chained_calls = binding_records = 0
        instantiation_kinds = {}
        selected = [] if args.project_only else [c for c in cases if c["version"] == version]
        if args.from_ninja and args.source and version == args.project_version:
            selected.append(dict(id=args.source, source=args.source, project=True))
        for case in selected:
            for all_traces in [False, True]:
                documents = []
                for label, root in [("C", reference), ("Rust", stage)]:
                    command = [binary, "oracle", "compile-trace", "--oracle-root", root, "--version", version]
                    if case.get("project"):
                        command += ["--from-ninja", args.from_ninja.resolve()]
                    else:
                        command += ["--working-directory", fixtures]
                    if all_traces:
                        command += ["--schedule", "--inline-cost", "--full-cost", "--instantiation", "--locals", "--blocks", "--cse", "--stages"]
                    command += [case["source"]] if case.get("project") else [fixtures / case["source"], "--", *case["flags"]]
                    document = run(f"{version}-{Path(case['source']).stem}-{int(all_traces)}-{label}", command, json_output=True)
                    assert document["compilation"]["identical"] is True
                    documents.append(document["compilation"])
                a, b = documents
                assert a["source_sha256"] == b["source_sha256"] and a["flags"] == b["flags"], f"{case['id']}: compile inputs changed"
                assert a["trace_options"] == b["trace_options"], case["id"]
                assert a["object_sha256"] == b["object_sha256"] and a["diagnostics"] == b["diagnostics"], case["id"]
                left, right = full_trace(a["trace"]), full_trace(b["trace"])
                assert left == right, f"{case['id']}: migrated trace differs (retained raw documents)"
                assert trace_line_endings(a["trace"]) == trace_line_endings(b["trace"]), f"{case['id']}: trace line endings differ"
                records += sum(line.startswith(("ra.graph ", "ra.node ", "ra.order ", "ra.select ")) for line in left)
                frame_records += sum(line.startswith("fr.") for line in left)
                layout_records += sum(line.startswith("fr.layout ") for line in left)
                pcode_records += sum(line.startswith(("ra.i ", "ra.block ", "st.i ", "st.block ", "bk.i ")) for line in left)
                schedule_records += sum(line.startswith("sc.") for line in left)
                known_candidates += known_schedule_candidates(left)
                block_records += sum(line.startswith("bk.") for line in left)
                scalar_records += sum(line.startswith("lc.") for line in left)
                binding_records += sum(line.startswith("lc.arg ") for line in left)
                for line in left:
                    if line.startswith("tp."):
                        kind = line.split(" ", 1)[0]
                        instantiation_kinds[kind] = instantiation_kinds.get(kind, 0) + 1
                cse_records += sum(line.startswith("ce.") for line in left)
                propagation_records += sum(line.startswith("pp.") for line in left)
                inline_records += sum(line.startswith("inl.") for line in left)
                chained_calls += sum(" chain=" in line and bool(line.split(" chain=", 1)[1]) for line in left)
                item["cases"].append(dict(source=case["source"], all_traces=all_traces, records=len(left), object_sha256=a["object_sha256"]))
                save()
        assert records > 0, "no positive graph observation coverage"
        item["graph_records"] = records
        assert frame_records > 0 and layout_records > 0, "no positive frame observation coverage"
        item["frame_records"] = frame_records
        item["frame_layout_records"] = layout_records
        assert pcode_records > 0 and schedule_records > 0 and known_candidates > 0, "no positive PCode/scheduler/callback coverage"
        item["pcode_records"], item["schedule_records"] = pcode_records, schedule_records
        item["known_schedule_candidates"] = known_candidates
        assert block_records > 0, "no positive block-observer coverage"
        item["block_records"] = block_records
        assert scalar_records > 0 and cse_records > 0 and propagation_records > 0, "no positive scalar/CSE/propagation coverage"
        item.update(scalar_records=scalar_records, cse_records=cse_records, propagation_records=propagation_records)
        assert inline_records > 0 and chained_calls > 0, "no positive inline/origin-chain coverage"
        item.update(inline_records=inline_records, chained_calls=chained_calls)
        assert binding_records > 0, "no positive inline-argument binding coverage"
        item["binding_records"] = binding_records
        assert all(instantiation_kinds.get(kind, 0) > 0 for kind in ["tp.queue", "tp.func", "tp.gen", "tp.emit"]), "no positive instantiation-observer coverage"
        item["instantiation_kinds"] = instantiation_kinds
        print(f"{version}: {len(item['cases'])} compilations agree; graph={records}, frame={frame_records}, PCode={pcode_records}, schedule={schedule_records}, blocks={block_records}, scalar={scalar_records}, CSE={cse_records}, propagation={propagation_records}, inline={inline_records}, chained={chained_calls}", flush=True)
        if args.canaries:
            corpus = run(f"canaries-{version}", [binary, "corpus", "--oracle-root", stage, "--version", version,
                         "--canaries", args.canaries.resolve(), "--all-traces", "--jobs", "8", "--json"], json_output=True)
            assert corpus["identical"] == corpus["total"] and corpus["total"] > 0
            item["canaries"] = {key: corpus[key] for key in ["total", "identical"]}
            print(f"{version}: {corpus['identical']}/{corpus['total']} canaries identical", flush=True)
        save()
    evidence["complete"] = True
    save()


if __name__ == "__main__":
    main()
