"""Validate native kit APIs using retained behavior evidence, without requiring legacy APIs.

This completes native acceptance when an archived Python command cannot run on a
host, using audited reference results rather than reproducing that command's API.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from compare_backends import difference
from verify_archived_kits import CASES, ROOT, sha, snapshot, write_json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive-proof", type=Path, required=True)
    parser.add_argument("--reference-proof", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--prime-project", type=Path, required=True)
    parser.add_argument("--echoes-project", type=Path, required=True)
    parser.add_argument("--wibo", type=Path)
    args = parser.parse_args()
    proof, reference, binary = args.archive_proof.resolve(), args.reference_proof.resolve(), args.binary.resolve()
    local = json.loads((proof / "verification.json").read_text())
    baseline = json.loads((reference / "verification.json").read_text())
    audit = json.loads((reference / "audit.json").read_text())
    assert baseline["complete"] and audit["complete"]
    assert audit["verification_sha256"] == sha(reference / "verification.json")
    assert sha(binary) == local["binary_sha256"]
    # Retained source/candidate comparisons are behavioral evidence, even when a
    # subsequent legacy command prevented the old aggregate from completing.
    for label in CASES:
        case = next(c for c in local["cases"] if c["kit"] == label)
        names = {c["name"] for c in case["checks"]}
        assert {"source-run", "candidate-run", "source-regdiff", "candidate-regdiff",
                "checked-rust-producer-source", "checked-rust-producer-candidate"} <= names
        project = proof / (label + "-project")
        for name, digest in case.get("staged_input_hashes", case["input_hashes"]).items():
            assert sha(project / name) == digest, name
        _, originals = snapshot(getattr(args, label + "_project").resolve(), CASES[label][0])
        for name, digest in case["input_hashes"].items():
            if name not in originals:
                originals[name] = sha(getattr(args, label + "_project") / name)
            assert originals[name] == digest, (label, name)
    for row in local["logs"]:
        assert row["exit_code"] == 0
        for stream in ("stdout", "stderr"):
            assert sha(proof / f"{row['name']}.{stream}") == row[stream + "_sha256"]
    output = Path(tempfile.mkdtemp(prefix="native-api-", dir=proof))
    environment = dict(os.environ, MWCC_ORACLE_ROOT=local["native_root"])
    if args.wibo:
        environment["MWCC_WIBO"] = str(args.wibo.resolve())
        assert sha(args.wibo) == local["wibo_sha256"]
    evidence = dict(complete=False, binary_sha256=sha(binary), verifier_sha256=sha(__file__),
                    archive_manifest_sha256=sha(proof / "verification.json"),
                    reference_manifest_sha256=sha(reference / "verification.json"),
                    reference_audit_sha256=sha(reference / "audit.json"), checks=[], logs=[])
    def save():
        write_json(output / "native-api-verification.json", evidence)
    def equal(left, right, name):
        found = difference(left, right)
        if found:
            raise RuntimeError(f"{name}: {found}")
    def run(name, command):
        result = subprocess.run([str(binary), *map(str, command)], cwd=ROOT, env=environment,
                                capture_output=True, timeout=900)
        for stream in ("stdout", "stderr"):
            (output / f"{name}.{stream}").write_bytes(getattr(result, stream))
        evidence["logs"].append(dict(name=name, command=list(map(str, command)), exit_code=result.returncode,
                                      stdout_sha256=sha(output / f"{name}.stdout"), stderr_sha256=sha(output / f"{name}.stderr")))
        save()
        if result.returncode:
            raise RuntimeError(f"{name}: {result.stderr.decode(errors='replace')[-1500:]}")
        return result.stdout.decode()
    print(f"retained native API acceptance: {output}", flush=True)
    save()
    for label, (_, _, game_version, compiler, _) in CASES.items():
        case = next(c for c in baseline["cases"] if c["kit"] == label)
        project = proof / (label + "-project")
        expected_project = reference / (label + "-project")
        for binary_mode in (False, True):
            check = next(c for c in case["checks"] if c["name"] == f"calibration-binary-{binary_mode}")
            json_path = output / f"{label}-calibration-{binary_mode}.json"
            command = ["inline-calibrate", "--project", project, "--callee", check["callee"], "--unit", case["unit"],
                       "--compiler", compiler, "--version", game_version, "--near", "1000000", "--breakdown", "--json", json_path]
            if binary_mode:
                command.append("--binary")
            name = f"{label}-calibration-{binary_mode}"
            report = run(name, command).splitlines()
            expected_file = expected_project / f"calibration-{binary_mode}-python.json"
            expected = json.loads(expected_file.read_text())
            actual = json.loads(json_path.read_text())
            assert actual
            equal(actual, expected, name)
            # Preserve report evidence for inspection; prose equality is not an API requirement.
            evidence["checks"].append(dict(name=name, rows=len(actual), report_lines=len(report),
                                           json_sha256=sha(json_path), reference_sha256=sha(expected_file)))
            save()
    for suffix in ("trace", "o"):
        shutil.copy2(reference / f"echoes-project/saved.{suffix}", output / f"legacy.{suffix}")
    colour = json.loads(run("legacy-saved-colouring", ["colouring", "unit", "--project", proof / "echoes-project",
                            "--unit", CASES["echoes"][0], "verify", "--function", CASES["echoes"][1],
                            "--load", output / "legacy", "--json"]))
    expected = reference / "echoes-colouring-python.stdout"
    equal(colour, json.loads(expected.read_text()), "legacy saved colouring")
    evidence["checks"].append(dict(name="legacy-saved-colouring", reference_sha256=sha(expected),
                                   trace_sha256=sha(output / "legacy.trace"), object_sha256=sha(output / "legacy.o")))
    # Exercise the documented native envelope and check state without Python imports.
    envelope = json.loads(run("compilation-envelope", ["oracle", "compile-trace", ROOT / "tests/fixtures/oracle/sources/cse.c",
                          "--version", "GC_1_3", "--", "-nodefaults", "-proc", "gekko", "-O4,p"]))
    assert envelope["format_version"] == 1 and envelope["report"]["schema_version"] == 6
    assert envelope["compilation"]["check_status"]["status"] == "verified"
    for field in ("producer_sha256", "source_sha256", "object_sha256"):
        assert len(envelope["compilation"][field]) == 64
    evidence["checks"].append(dict(name="compilation-envelope", check_status="verified"))
    # Native commands only read the copied projects; verify all input copies again.
    for case in local["cases"]:
        project = proof / (case["kit"] + "-project")
        for name, digest in case.get("staged_input_hashes", case["input_hashes"]).items():
            assert sha(project / name) == digest, name
    evidence["complete"] = True
    save()
    print(f"native API acceptance: {len(evidence['checks'])} direct-command checks pass")


if __name__ == "__main__":
    main()
