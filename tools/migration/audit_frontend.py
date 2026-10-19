"""Match retained frontend ABI executions and current sources to native objects."""
import argparse
import json
from pathlib import Path
from verify_producer import ROOT, sha, full_trace, trace_line_endings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-root", type=Path, required=True)
    parser.add_argument("--abi-root", type=Path, required=True)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--switch-root", type=Path)
    args = parser.parse_args()
    native, abi = args.native_root.resolve(), args.abi_root.resolve()
    native_path, abi_path = native / "producer-verification.json", abi / "frontend-verification.json"
    matrix, direct = json.loads(native_path.read_text()), json.loads(abi_path.read_text())
    assert matrix["complete"] and direct["complete"] and direct["layout_only"] is False
    assert {v["version"] for v in matrix["versions"]} == {"GC_1_3", "GC_1_3_2", "GC_2_7"}
    assert len(direct["versions"]) == 3
    assert sha(args.oracle_root / "src/oracle/Hooks.c") == direct["reference_sha256"]
    for name, digest in direct["sources"].items(): assert sha(ROOT / name) == digest, name
    evidence = dict(native_manifest_sha256=sha(native_path), abi_manifest_sha256=sha(abi_path), versions=[])
    for version in matrix["versions"]:
        name = version["version"]
        check = next(v for v in direct["versions"] if v["version"] == name)
        assert check["cases"] == 256 and len(check["layout"]) == 61
        assert [r["name"] for r in check["runs"]] == ["rust", "layout", "runtime", "implib", "link", "cases"]
        for run in check["runs"]:
            assert run["exit_code"] == 0
            for stream in ["stdout", "stderr"]: assert sha(abi / name / f"{run['name']}.{stream}") == run[stream + "_sha256"]
        assert not (abi / name / "cases.stdout").read_bytes() and not (abi / name / "cases.stderr").read_bytes()
        assert sha(abi / name / "runtime.exe") == check["runtime_sha256"]
        assert sha(abi / name / "frontend_reference.h") == check["reference_sha256"]
        obj = native / f"build/{name}/oracle/compiled/rust/frontend.obj"
        assert sha(obj) == sha(abi / name / "frontend.obj") == check["frontend_object_sha256"] == version["frontend_object_sha256"]
        assert sha(native / version["frontend_reference"]) == sha(obj)
        assert sha(native / f"build/{name}/oracle/mwcceppc.exe") == version["producer_sha256"]
        assert version["hook_inventory_count"] == 31
        assert not (native / f"build/{name}/oracle/compiled/src/oracle/Hooks.c.obj").exists()
        evidence["versions"].append(dict(version=name, frontend_object_sha256=sha(obj), direct_cases=256, layout_probes=61))
    if args.switch_root:
        stage = args.switch_root.resolve()
        path = stage / "frontend-switches.json"
        switches = json.loads(path.read_text())
        assert switches["complete"]
        assert len(switches["cases"]) == 15
        assert {(c["version"], c["mode"]) for c in switches["cases"]} == {(v["version"], mode) for v in matrix["versions"] for mode in ["default", "expression", "full", "both", "instantiation"]}
        for case in switches["cases"]:
            documents = []
            for run in case["runs"]:
                assert run["exit_code"] == 0
                prefix = f"{case['version']}-{case['mode']}-{run['label']}"
                for stream in ["stdout", "stderr"]: assert sha(stage / f"{prefix}.{stream}") == run[stream + "_sha256"]
                documents.append(json.loads((stage / f"{prefix}.stdout").read_text())["compilation"])
            assert len(documents) == 2
            a, b = documents
            assert a["identical"] is True and b["identical"] is True
            for field in ["trace_options", "source_sha256", "flags", "object_sha256", "diagnostics"]: assert a[field] == b[field]
            assert full_trace(a["trace"]) == full_trace(b["trace"])
            assert trace_line_endings(a["trace"]) == trace_line_endings(b["trace"])
            mode = case["mode"]
            assert a["trace_options"]["inline_cost"] == (mode in ["expression", "both"])
            assert a["trace_options"]["full_cost"] == (mode in ["full", "both"])
            assert a["trace_options"]["instantiation"] == (mode == "instantiation")
            assert ("inl.expr " in a["trace"]) == (mode in ["expression", "full", "both"])
            assert any(" full=" in line for line in a["trace"].splitlines() if line.startswith("inl.cost ")) == (mode in ["full", "both"])
            if mode == "instantiation": assert "tp.parse " in a["trace"]
            assert b["producer_sha256"] == sha(native / f"build/{case['version']}/oracle/mwcceppc.exe")
        evidence.update(switch_manifest_sha256=sha(path), checked_switch_modes=15)
    (native / "frontend-abi-audit.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"{native}: all frontend ABI logs, current sources and native objects revalidated")


if __name__ == "__main__":
    main()
