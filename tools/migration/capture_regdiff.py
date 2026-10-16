"""Capture kit regdiff contracts with synthetic ELF objects and objdiff output."""
import argparse
from contextlib import redirect_stdout
import hashlib
import importlib
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/fixtures/oracle"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--reference-script", type=Path, required=True)
    parser.add_argument("--objdiff", type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.oracle_root / "tools"))
    sys.path.insert(0, str(args.reference_script.parent))
    oracle = importlib.import_module("oracle")
    colouring = importlib.import_module("colouring")
    spec = importlib.util.spec_from_file_location("regdiff_reference", args.reference_script)
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    manifest = json.loads((FIXTURES / "manifest.json").read_text())
    for case in manifest["cases"]:
        prefix = FIXTURES / case["id"]
        data = prefix.with_suffix(".o").read_bytes()
        edited = bytearray(data)
        offset = struct.unpack_from(">I", data, 0x20)[0]
        width = struct.unpack_from(">H", data, 0x2e)[0]
        locations = set()
        for _, (section, start, size) in oracle.object_symbols(data).items():
            section_at = struct.unpack_from(">I", data, offset+section*width+16)[0]
            locations.update(range(section_at+start, section_at+start+size, 4))
        for at in sorted(locations):
            word = struct.unpack_from(">I", data, at)[0]
            for cls, field, reg, _ in colouring.register_fields(word) or []:
                if cls in ("GPR", "FPR") and reg >= 14:
                    shift = colouring.SHIFTS[field]
                    word = word & ~(31 << shift) | (45-reg) << shift
            displacement = oracle.r1_offset(word)
            if displacement is not None and displacement >= 8:
                mask = 0xfff if word >> 26 in (56, 57, 60, 61) else 0xffff
                word = word & ~mask | (displacement+16) & mask
            struct.pack_into(">I", edited, at, word)
        lines = prefix.with_suffix(".trace").read_text().splitlines()
        allocations = oracle.parse_trace(lines)[0]
        with tempfile.TemporaryDirectory() as temporary:
            project = Path(temporary)
            target, base, output = project / "target.o", project / "base.o", project / "diff.json"
            target.write_bytes(edited)
            base.write_bytes(data)
            subprocess.run([str(args.objdiff.resolve()), "diff", "-c", "functionRelocDiffs=data_value", "-1", str(target),
                            "-2", str(base), "-o", str(output), "--format", "json"], check=True, capture_output=True)
            diff = json.loads(output.read_text())
            prefix.with_suffix(".regdiff-input.json").write_text(json.dumps(diff, indent=2)+"\n", encoding="utf-8")
            (project / "objdiff.json").write_text(json.dumps({"units": [{"name": "synthetic", "base_path": "base.o",
                                            "target_path": "target.o", "metadata": {"source_path": "source.cpp"}}]}))
            def run(command, **kwargs):
                if "--format" in command:
                    Path(command[command.index("-o")+1]).write_text(json.dumps(diff))
                else:
                    Path(command[command.index("-o")+1]).write_bytes(data)
                    Path(kwargs["env"]["MWCC_ORACLE_TRACE_FILE"]).write_text("\n".join(lines)+"\n")
                return subprocess.CompletedProcess(command, 0, "", "")
            references = []
            for symbol in helper.function_symbols(diff["right"]):
                index, chosen, how = helper.select_allocation(allocations, symbol["name"], [s["name"] for s in helper.function_symbols(diff["right"])])
                if chosen is None:
                    continue
                analysis = oracle.analyze(*chosen[:3], oracle.opcode_names(case["version"]))
                blocks = helper.explanation_blocks(oracle.explain(analysis))
                stream = io.StringIO()
                with patch.object(helper, "ROOT", project), patch.object(helper, "ninja_rule", return_value=("GC/"+case["version"][3:].replace("_", "."), [])), \
                     patch.object(helper, "load_oracle", return_value=oracle), patch.object(helper.subprocess, "run", side_effect=run), \
                     patch.object(sys, "argv", ["regdiff", "--oracle-root", str(args.oracle_root.resolve()), "--unit", "synthetic", "--function", symbol["name"], "--full", "--pcode", "--inlining"]), redirect_stdout(stream):
                    helper.main()
                references.append({"query": symbol["name"], "index": index, "how": how,
                                   "explanations": [{"class": c, "vreg": v, "lines": ls} for (c,v),ls in blocks.items()],
                                   "text": stream.getvalue().splitlines()[2:]})
            prefix.with_suffix(".regdiff.json").write_text(json.dumps(references, indent=2)+"\n", encoding="utf-8")
    (FIXTURES.parent / "regdiff-reference.json").write_text(json.dumps({"helper_sha256": hashlib.sha256(args.reference_script.read_bytes()).hexdigest(),
        "oracle_commit": manifest["reference_commit"], "objdiff_sha256": hashlib.sha256(args.objdiff.read_bytes()).hexdigest()}, indent=2)+"\n")
    print(f"Captured regdiff contracts for {len(manifest['cases'])} synthetic objects")


if __name__ == "__main__":
    main()
