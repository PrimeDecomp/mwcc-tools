"""Capture redistributable synthetic oracle inputs and Python reference results.

Run with Python 3.11+, --oracle-root pointing to a built baseline checkout.
This script deliberately calls the reference Python implementation, not Rust.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/fixtures/oracle"
FLAGS = ["-nodefaults", "-proc", "gekko", "-align", "powerpc", "-enum", "int", "-fp", "hardware",
         "-O4,p", "-inline", "auto", "-nosyspath", "-RTTI", "off", "-fp_contract", "on", "-str", "reuse"]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    args = parser.parse_args()
    root = args.oracle_root.resolve()
    spec = importlib.util.spec_from_file_location("oracle_reference", root / "tools/oracle.py")
    oracle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(oracle)
    sys.path.insert(0, str(root / "tools"))
    colouring = importlib.import_module("colouring")
    manifest = {"format_version": 1,
                "reference_commit": subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip(),
                "python": sys.version.split()[0], "schema_version": oracle.SCHEMA_VERSION, "cases": [], "failures": []}
    for version in ("GC_1_3", "GC_1_3_2", "GC_2_7"):
        for source in sorted((FIXTURES / "sources").glob("*")):
            flags = FLAGS + (["-lang=c++"] if source.suffix == ".cpp" else [])
            compiled = oracle.compile_trace(str(source.resolve()), flags=flags, version=version,
                                            schedule=True, inline_cost=True, full_cost=True,
                                            instantiation=True, scalarize=True, blocks=True, cse=True)
            if compiled["identical"] is not True:
                raise RuntimeError(f"{version}/{source.name}: baseline oracle differs from original")
            lines = compiled["lines"]
            allocations, _ = oracle.parse_trace(lines)
            analysis = [oracle.analyze(name, before, after, oracle.opcode_names(version))
                        for name, before, after, _ in allocations]
            prefix = FIXTURES / version / source.stem
            prefix.parent.mkdir(parents=True, exist_ok=True)
            trace = ("\n".join(lines) + "\n").encode()
            prefix.with_suffix(".trace").write_bytes(trace)
            prefix.with_suffix(".o").write_bytes(compiled["object"])
            prefix.with_suffix(".allocation.json").write_text(json.dumps(analysis, indent=2) + "\n", encoding="utf-8")
            document = oracle.document_for(compiled)[0]
            document["source"] = f"sources/{source.name}"
            prefix.with_suffix(".json").write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
            prefix.with_suffix(".cse.json").write_text(json.dumps(oracle.cse_trace(lines), indent=2) + "\n", encoding="utf-8")
            views = {"near": oracle.document_for(compiled, near=8)[0]["near"],
                     "suggestions": {query: oracle.similar_functions(allocations, query)
                                     for query in ("accumualte", "does_not_exist")}}
            views["near_report"] = oracle.near_report(views["near"], 8)
            views["instantiation_report"] = oracle.instantiation_report(oracle.instantiation_events(lines, version), "")
            views["raw_selected"] = {query: oracle.raw_for(lines, query) for query in ("common", "pipeline", "does_not_exist")}
            prefix.with_suffix(".views.json").write_text(json.dumps(views, indent=2) + "\n", encoding="utf-8")
            colourings = []
            for allocation in colouring.allocations(compiled):
                for cls in ("GPR", "FPR", "VR"):
                    result = colouring.verify_function(allocation, cls)
                    if result is not None:
                        colourings.append(result)
            prefix.with_suffix(".colouring.json").write_text(json.dumps(colourings, indent=2) + "\n", encoding="utf-8")
            reports = []
            for analysis, (_, before, _, _) in zip(document["functions"], allocations):
                reports.append({"allocation": oracle.explain(analysis), "frame": oracle.frame_report(analysis),
                                "inlining": oracle.inlining_report(analysis["inlining"]),
                                "inlining_links": oracle.inlining_report(analysis["inlining"], links=True),
                                "costs": [oracle.cost_report(d, oracle.enode_names()) for d in analysis["inlining"]],
                                "schedule": oracle.schedule_report(analysis["function"], analysis["schedule"]),
                                "pcode": oracle.pcode_report(analysis["function"], before, oracle.opcode_names(version))})
            prefix.with_suffix(".reports.json").write_text(json.dumps(reports, indent=2) + "\n", encoding="utf-8")
            cse_records = oracle.cse_trace(lines)
            block_records = oracle.block_trace(lines)
            types, opcodes = oracle.enode_names(), oracle.opcode_names(version)
            advanced = {"cse": {}, "blocks": {}, "exchange": []}
            for link, record in cse_records.items():
                advanced["cse"][link] = {"plain": oracle.cse_report(link, record, types, version),
                                         "ir_all": oracle.cse_report(link, record, types, version, "all"),
                                         "ir_line": oracle.cse_report(link, record, types, version, "3"),
                                         "origins": [{"class": oracle.CLASSES.get(c, f"class{c}"), "number": reg,
                                                      "text": oracle.vreg_origin(record, oracle.CLASSES.get(c, f"class{c}"), reg, version, types)}
                                                     for reg, c, _ in record["vregs"].values() if reg >= 0]}
            def instruction_json(ins):
                return {"pointer": ins.pointer, "opcode": ins.opcode, "flags": ins.flags, "line": ins.line,
                        "operands": [{"raw": list(op.raw)} for op in ins.operands]}
            for link, record in block_records.items():
                serialized = dict(record, lists=[(phase, [dict(b, instructions=[instruction_json(i) for i in b["instructions"]])
                                                          for b in blocks]) for phase, blocks in record["lists"]])
                advanced["blocks"][link] = {"parsed": serialized, "cuts": oracle.block_cuts(record, opcodes),
                                            "cuts_none": oracle.block_cuts(record, opcodes, "none"),
                                            "plain": oracle.blocks_report(link, record, opcodes),
                                            "pcode": oracle.blocks_report(link, record, opcodes, pcode=True),
                                            "limit_zero": oracle.blocks_report(link, record, opcodes, limit=0)}
            for analysis in document["functions"]:
                pairs = [("bad", "33"), ("r99999", "f33")]
                for cls, info in analysis["classes"].items():
                    if len(info["vregs"]) >= 2:
                        prefix_ = {"GPR": "r", "FPR": "f", "VR": "v", "CR": "cr"}.get(cls, "r")
                        pairs.append((prefix_+str(info["vregs"][0]["vreg"]), prefix_+str(info["vregs"][-1]["vreg"])))
                advanced["exchange"].append({"link": analysis["link"], "pairs": [{"first": a, "second": b, "text": oracle.explain_exchange(analysis, a, b)} for a, b in pairs]})
            prefix.with_suffix(".advanced.json").write_text(json.dumps(advanced, indent=2) + "\n", encoding="utf-8")
            manifest["cases"].append({"id": f"{version}/{source.stem}", "source": f"sources/{source.name}",
                                      "source_sha256": digest(source.read_bytes()), "flags": flags,
                                      "version": version, "trace_sha256": digest(trace),
                                      "object_sha256": digest(compiled["object"]),
                                      "original_sha256": digest((root / "build/compilers/GC" / version[3:].replace("_", ".") / "mwcceppc.exe").read_bytes()),
                                      "producer_sha256": digest((root / f"build/{version}/oracle/mwcceppc.exe").read_bytes()),
                                      "identical": True})
        with tempfile.TemporaryDirectory(prefix="oracle-failure-baseline-") as temporary:
            sys.path.insert(0, str(root / "tools"))
            corpus = importlib.import_module("corpus")
            source = FIXTURES / "errors/invalid.c"
            outcomes = []
            for side, executable in (("original", root / "build/compilers/GC" / version[3:].replace("_", ".") / "mwcceppc.exe"),
                                     ("oracle", root / f"build/{version}/oracle/mwcceppc.exe")):
                output = Path(temporary) / f"{side}.o"
                run = subprocess.run([*oracle.launcher(executable), *FLAGS, "-maxerrors", "1", "-c", str(source.resolve()), "-o", str(output)],
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     env=dict(os.environ, MWCC_ORACLE_TRACE_FILE=os.devnull), timeout=60)
                diagnostics = corpus.normalise(run.stdout.decode("latin-1"), [])
                if run.returncode == 0 or output.exists():
                    raise RuntimeError(f"{version}/{side}: invalid source unexpectedly compiled")
                (FIXTURES / version / f"invalid.{side}.txt").write_text(diagnostics, encoding="utf-8")
                outcomes.append((run.returncode, diagnostics))
            if outcomes[0] != outcomes[1]:
                raise RuntimeError(f"{version}: failed compiler behavior differs")
            manifest["failures"].append({"id": f"{version}/invalid", "source": "errors/invalid.c", "exit_code": outcomes[0][0],
                                         "source_sha256": digest(source.read_bytes()), "flags": FLAGS + ["-maxerrors", "1"]})
    (FIXTURES / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Captured {len(manifest['cases'])} verified synthetic cases")


if __name__ == "__main__":
    main()
