"""Revalidate trace ABI logs, linked Rust ownership and independent linker images."""
import argparse
import json
from pathlib import Path
from verify_link import image_bytes
from verify_producer import ROOT, sha
import hashlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-root", type=Path, required=True)
    parser.add_argument("--abi-root", type=Path, required=True)
    parser.add_argument("--link-results", type=Path, required=True)
    parser.add_argument("--oracle-root", type=Path, required=True)
    args = parser.parse_args()
    stage, abi = args.native_root.resolve(), args.abi_root.resolve()
    manifest = json.loads((stage / "producer-verification.json").read_text())
    direct = json.loads((abi / "trace-writer-verification.json").read_text())
    links = json.loads(args.link_results.read_text())
    assert manifest["complete"] and direct["complete"]
    assert {v["version"] for v in manifest["versions"]} == {"GC_1_3", "GC_1_3_2", "GC_2_7"}
    assert len(direct["versions"]) == len(links) == 3
    for name, digest in direct["sources"].items():
        assert sha(ROOT / name.replace("\\", "/")) == digest, name
    assert sha(args.oracle_root / "src/oracle/OracleTrace.c") == direct["c_reference_sha256"]
    expected = manifest["trace_reference"]
    assert sha(stage / expected["path"]) == expected["sha256"] == direct["trace_object_sha256"]
    result = dict(native_manifest_sha256=sha(stage / "producer-verification.json"),
        abi_manifest_sha256=sha(abi / "trace-writer-verification.json"), link_manifest_sha256=sha(args.link_results), versions=[])
    for version in manifest["versions"]:
        name = version["version"]
        check = next(v for v in direct["versions"] if v["version"] == name)
        linked = next(v for v in links if v["version"] == name)
        assert check["cases"] == list(range(24)) and check["c_layout"] == [84, 2, 168, 4, 4, 4, 80]
        assert len(check["runs"]) == 28
        for run in check["runs"]:
            assert run["exit_code"] == 0
            for stream in ["stdout", "stderr"]:
                assert sha(abi / name / f"{run['name']}.{stream}") == run[stream + "_sha256"]
        assert sha(abi / name / "runtime.exe") == check["runtime_sha256"]
        assert sha(abi / name / "caller.obj") == check["rust_caller_sha256"]
        trace = stage / f"build/{name}/oracle/OracleTrace.obj"
        assert sha(trace) == version["trace_object_sha256"] == expected["sha256"]
        executable = stage / f"build/{name}/oracle/mwcceppc.exe"
        assert sha(executable) == version["producer_sha256"]
        normalized = hashlib.sha256(image_bytes(executable)).hexdigest()
        assert normalized == linked["sha256"] and linked["identical"] is True and linked["timestamp_normalized"] is True
        assert version["canaries"] == {"total": 2313, "identical": 2313}
        result["versions"].append(dict(version=name, trace_object_sha256=sha(trace), producer_sha256=sha(executable),
            normalized_image_sha256=normalized, direct_cases=24, hook_abi_cases=linked["direct_abi_cases"], canaries=version["canaries"]))
    (stage / "trace-output-audit.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"{stage}: ABI logs, current Rust object ownership and native/Python linked images revalidated")


if __name__ == "__main__":
    main()
