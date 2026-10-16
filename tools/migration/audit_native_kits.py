"""Audit a new kit's archive, source pin, native assets and optional real-unit API."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

from verify_archived_kits import CASES, sha, unpack, write_json
from audit_release import native_target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--label", choices=CASES, required=True)
    parser.add_argument("--source-pin", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--project-proof", type=Path)
    parser.add_argument("--oracle-root", type=Path)
    args = parser.parse_args()
    assert bool(args.project_proof) == bool(args.oracle_root)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    # Reuse the generic archive validator with the explicitly requested new pin.
    CASES[args.label] = (*CASES[args.label][:-1], args.source_pin)
    kit, archive = unpack(args.archive.resolve(), output, args.label)
    native = json.loads((kit / "native-manifest.json").read_text())
    assert native["format_version"] == 1 and native["source_revision"] == args.source_pin
    source = kit / native["source_bundle"]["path"]
    assert source.resolve().is_relative_to(kit)
    assert sha(source) == native["source_bundle"]["sha256"]
    with tempfile.TemporaryDirectory(prefix="native bundle audit ") as temporary:
        heads = subprocess.check_output(["git", "bundle", "list-heads", str(source)], cwd=temporary, text=True)
    assert args.source_pin in [line.split()[0] for line in heads.splitlines()]
    for target, artifact in native["artifacts"].items():
        path = kit / artifact["path"]
        assert path.resolve().is_relative_to(kit)
        assert sha(path) == artifact["sha256"]
        with zipfile.ZipFile(path) as bundle:
            assert bundle.testzip() is None
            release = json.loads(bundle.read("release.json"))
            assert release["source_revision"] == args.source_pin and release["target"] == target
            assert release["executable_sha256"] == artifact["executable_sha256"]
            assert release["protocols"] == native["protocols"]
            assert set(bundle.namelist()) == set(release["files"]) | {"release.json"}
            for name, entry in release["files"].items():
                data = bundle.read(name)
                assert len(data) == entry["bytes"] and hashlib.sha256(data).hexdigest() == entry["sha256"]
    project = output / "installed project"
    project.mkdir()
    result = subprocess.run([sys.executable, str(kit / "install-native.py"), str(project), "--apply"],
                            cwd=project, check=True, capture_output=True, text=True, timeout=120)
    (output / "install.stdout").write_text(result.stdout)
    executable = Path(next(line.removeprefix("MWCC_TOOLS=") for line in result.stdout.splitlines() if line.startswith("MWCC_TOOLS=")))
    target = native_target()
    assert sha(executable) == native["artifacts"][target]["executable_sha256"]
    evidence = dict(complete=False, archive=archive, source_pin=args.source_pin,
                    native_manifest_sha256=sha(kit / "native-manifest.json"), executable_sha256=sha(executable), target=target)
    if args.project_proof:
        proof = args.project_proof.resolve()
        verification = json.loads((proof / "verification.json").read_text())
        case = next(c for c in verification["cases"] if c["kit"] == args.label)
        project = proof / f"{args.label}-project"
        inputs = case.get("staged_input_hashes", case["input_hashes"])
        for name, digest in inputs.items():
            assert sha(project / name) == digest, name
        units = json.loads((project / "objdiff.json").read_text())["units"]
        unit = next(u for u in units if u["name"] == CASES[args.label][0])
        command = [str(executable), "oracle", "compile-trace", unit["metadata"]["source_path"],
                   "--from-ninja", "build.ninja", "--oracle-root", str(args.oracle_root.resolve())]
        with (output / "unit.stdout").open("wb") as stdout, (output / "unit.stderr").open("wb") as stderr:
            subprocess.run(command, cwd=project, check=True, stdout=stdout, stderr=stderr, timeout=600)
        assert not (output / "unit.stderr").read_bytes()
        document = json.loads((output / "unit.stdout").read_text())
        assert document["format_version"] == 1 and document["report"]["schema_version"] == 6
        compilation = document["compilation"]
        assert compilation["check_status"]["status"] == "verified"
        assert len(document["report"]["functions"]) == (35 if args.label == "prime" else 136)
        for name, digest in inputs.items():
            assert sha(project / name) == digest, name
        evidence["real_unit"] = dict(unit=unit["name"], functions=len(document["report"]["functions"]),
                                     check_status="verified", object_sha256=compilation["object_sha256"],
                                     producer_sha256=compilation["producer_sha256"], inputs=len(inputs),
                                     stdout_sha256=sha(output / "unit.stdout"), command=command)
    evidence["complete"] = True
    write_json(output / "audit.json", evidence)
    print(f"{args.label}: native archive/pin/assets/install verified" + ("; real unit stock equality verified" if args.project_proof else ""))


if __name__ == "__main__":
    main()
