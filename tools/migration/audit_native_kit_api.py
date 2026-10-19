"""Audit native API acceptance and its retained behavior references without compiling."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from compare_backends import difference, normalize
from verify_archived_kits import CASES, ROOT, sha, snapshot, write_json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--reference-proof", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--prime-project", type=Path, required=True)
    parser.add_argument("--echoes-project", type=Path, required=True)
    args = parser.parse_args()
    root, reference = args.root.resolve(), args.reference_proof.resolve()
    proof = root.parent
    evidence = json.loads((root / "native-api-verification.json").read_text())
    local = json.loads((proof / "verification.json").read_text())
    assert evidence["complete"] and evidence["binary_sha256"] == sha(args.binary) == local["binary_sha256"]
    assert evidence["verifier_sha256"] == sha(ROOT / "tools/migration/verify_native_kit_api.py")
    assert evidence["archive_manifest_sha256"] == sha(proof / "verification.json")
    assert evidence["reference_manifest_sha256"] == sha(reference / "verification.json")
    assert evidence["reference_audit_sha256"] == sha(reference / "audit.json")
    assert json.loads((reference / "audit.json").read_text())["verification_sha256"] == sha(reference / "verification.json")
    def equal(a, b):
        assert difference(a, b) is None, difference(a, b)
    for label in CASES:
        case = next(c for c in local["cases"] if c["kit"] == label)
        project = proof / (label + "-project")
        for path, digest in case.get("staged_input_hashes", case["input_hashes"]).items():
            assert sha(project / path) == digest, path
        game = getattr(args, label + "_project").resolve()
        _, inputs = snapshot(game, CASES[label][0])
        for path, digest in case["input_hashes"].items():
            assert inputs.get(path, sha(game / path)) == digest, path
        status = subprocess.check_output(["git", "status", "--porcelain=v1", "--untracked-files=normal"], cwd=game)
        assert hashlib.sha256(status).hexdigest() == case["game_status_sha256"]
        assert sha(Path(local["native_root"]) / f"build/{case['compiler']}/oracle/mwcceppc.exe") == case["rust_producer_sha256"]
        for variant in ("source", "candidate"):
            for left, right in (("run-python", "run-rust"), ("checked-" + variant + "-c", "checked-" + variant + "-rust")):
                prefix = label + "-" + variant + "-" if left.startswith("run-") else label + "-"
                docs = [json.loads((proof / f"{prefix}{mode}.stdout").read_text()) for mode in (left, right)]
                equal(normalize(docs[0]), normalize(docs[1]))
                if left.startswith("checked-"):
                    assert all(d["identical"] is True for d in docs)
    checks = {c["name"]: c for c in evidence["checks"]}
    expected = {f"{label}-calibration-{mode}" for label in CASES for mode in (False, True)} | {"legacy-saved-colouring", "compilation-envelope"}
    assert set(checks) == expected and len(evidence["checks"]) == 6
    for label in CASES:
        for mode in (False, True):
            name = f"{label}-calibration-{mode}"
            path = root / (name + ".json")
            target = reference / f"{label}-project/calibration-{mode}-python.json"
            assert sha(path) == checks[name]["json_sha256"] and sha(target) == checks[name]["reference_sha256"]
            value = json.loads(path.read_text())
            assert value and len(value) == checks[name]["rows"]
            equal(value, json.loads(target.read_text()))
    colour = checks["legacy-saved-colouring"]
    assert sha(root / "legacy.trace") == colour["trace_sha256"] and sha(root / "legacy.o") == colour["object_sha256"]
    equal(json.loads((root / "legacy-saved-colouring.stdout").read_text()), json.loads((reference / "echoes-colouring-python.stdout").read_text()))
    envelope = json.loads((root / "compilation-envelope.stdout").read_text())
    assert envelope["format_version"] == 1 and envelope["report"]["schema_version"] == 6
    assert envelope["compilation"]["check_status"]["status"] == "verified"
    assert envelope["compilation"]["source_sha256"] == sha(ROOT / "tests/fixtures/oracle/sources/cse.c")
    assert envelope["compilation"]["producer_sha256"] == sha(Path(local["native_root"]) / "build/GC_1_3/oracle/mwcceppc.exe")
    logs = {row["name"]: row for row in evidence["logs"]}
    assert set(logs) == expected and len(evidence["logs"]) == 6
    for directory, rows in ((root, evidence["logs"]), (proof, local["logs"])):
        for row in rows:
            assert row["exit_code"] == 0
            for stream in ("stdout", "stderr"):
                assert sha(directory / f"{row['name']}.{stream}") == row[stream + "_sha256"]
    write_json(root / "audit.json", dict(complete=True, native_api_checks=6, cached_compile_comparisons=8,
               game_inputs_and_status_unchanged=True, verification_sha256=sha(root / "native-api-verification.json"),
               auditor_sha256=sha(__file__)))
    print("native API acceptance audited: 6 direct checks, 8 cached compile comparisons, inputs/status unchanged")


if __name__ == "__main__":
    main()
