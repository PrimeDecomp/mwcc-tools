"""Recheck retained archive-gate inputs, logs and comparisons without compiling again."""
import argparse
import hashlib
import json
from pathlib import Path
import tarfile

from compare_backends import difference, normalize
from verify_archived_kits import CASES, host_ninja, sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--objdiff", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    evidence = json.loads((root / "verification.json").read_text())
    assert evidence.get("format_version") == 1 and evidence.get("complete") is True
    assert evidence["binary_sha256"] == sha(args.binary)
    assert evidence["objdiff_sha256"] == sha(args.objdiff)
    assert evidence["verifier_sha256"] == sha(root / "verifier.py")
    assert {c["kit"] for c in evidence["cases"]} == set(CASES) and len(evidence["cases"]) == 2
    expected_logs = set()
    def equal(left, right, name):
        found = difference(left, right)
        assert not found, f"{name}: {found}"
    def read(name):
        return (root / f"{name}.stdout").read_text(encoding="utf-8")
    def lines(text):
        result = text.splitlines()
        assert result
        result[0] = result[0].replace("\\", "/")
        return result
    total_inputs = total_checks = 0
    for case in evidence["cases"]:
        label = case["kit"]
        assert case.get("complete") is True and case.get("inputs_unchanged") is True
        assert (case["unit"], case["function"], case["compiler"], case["archive"]["producer_pin"]) == (
            CASES[label][0], CASES[label][1], CASES[label][3], CASES[label][4])
        archive = Path(case["archive"]["path"])
        assert sha(archive) == case["archive"]["sha256"]
        assert archive.with_suffix(archive.suffix + ".sha256").read_text().split()[0] == sha(archive)
        project = root / (label + "-project")
        staged_inputs = case.get("staged_input_hashes", case["input_hashes"])
        assert set(staged_inputs) == set(case["input_hashes"])
        for name, digest in staged_inputs.items():
            path = (project / name).resolve()
            assert path.is_relative_to(project) and sha(path) == digest, name
            if name != "build.ninja":
                assert digest == case["input_hashes"][name]
        if case.get("ninja_paths_adjusted_for_host"):
            old_ninja = root / (label + "-original-build.ninja")
            assert sha(old_ninja) == case["input_hashes"]["build.ninja"]
            assert host_ninja(old_ninja.read_bytes()) == (project / "build.ninja").read_bytes()
        else:
            assert staged_inputs["build.ninja"] == case["input_hashes"]["build.ninja"]
        assert sha(project / "candidate.cpp") == case["candidate_sha256"]
        total_inputs += len(case["input_hashes"])
        assert sha(root / f"{label}-pinned-oracle/build/{case['compiler']}/oracle/mwcceppc.exe") == case["pinned_producer_sha256"]
        if "wibo_sha256" in evidence:
            assert sha(root / f"{label}-pinned-oracle/build/tools/wibo") == evidence["wibo_sha256"]
        if "pin_source_archive_sha256" in case:
            source_archive = root / (label + "-pin.tar")
            assert sha(source_archive) == case["pin_source_archive_sha256"]
            with tarfile.open(source_archive) as source:
                assert source.pax_headers["comment"] == case["archive"]["producer_pin"]
                for member in source.getmembers():
                    if member.isfile():
                        path = (root / (label + "-pinned-oracle") / member.name).resolve()
                        assert path.is_relative_to(root / (label + "-pinned-oracle"))
                        assert sha(path) == hashlib.sha256(source.extractfile(member).read()).hexdigest(), member.name
        assert sha(Path(evidence["native_root"]) / f"build/{case['compiler']}/oracle/mwcceppc.exe") == case["rust_producer_sha256"]
        checks = {c["name"]: c for c in case["checks"]}
        expected_checks = {"source-run", "candidate-run", "source-regdiff", "candidate-regdiff",
                           "checked-rust-producer-source", "checked-rust-producer-candidate",
                           "calibration-binary-False", "calibration-binary-True"}
        if label == "echoes":
            expected_checks.add("legacy-saved-colouring")
        assert set(checks) == expected_checks and len(checks) == len(case["checks"])
        total_checks += len(checks)
        expected_logs.update({f"{label}-configure-pin", f"{label}-build-pin"})
        for variant in ("source", "candidate"):
            docs = []
            texts = []
            for mode in ("python", "rust"):
                prefix = f"{label}-{variant}"
                name = prefix + "-run-" + mode
                expected_logs.add(name)
                doc = json.loads(read(name))
                assert doc["identical"] is None and len(doc["functions"]) == checks[variant + "-run"]["functions"]
                docs.append(normalize(doc))
                assert sha(project / f"{prefix}-{mode}.o") == checks[variant + "-run"]["object_sha256"]
                name = prefix + "-regdiff-" + mode
                expected_logs.add(name)
                text = lines(read(name))
                assert len(text) == checks[variant + "-regdiff"]["lines"]
                texts.append(text)
            equal(docs[0], docs[1], f"{label} {variant} document")
            equal(texts[0], texts[1], f"{label} {variant} regdiff")
            docs = []
            for mode in ("c", "rust"):
                name = f"{label}-checked-{variant}-{mode}"
                expected_logs.add(name)
                doc = json.loads(read(name))
                assert doc["identical"] is True
                assert len(doc["functions"]) == checks["checked-rust-producer-" + variant]["functions"]
                docs.append(normalize(doc))
            equal(docs[0], docs[1], f"{label} checked {variant} producer")
        assert checks["source-run"]["object_sha256"] != checks["candidate-run"]["object_sha256"]
        unit = next(u for u in json.loads((project / "objdiff.json").read_text())["units"] if u["name"] == case["unit"])
        was_current = checks["source-run"]["object_sha256"] == sha(project / unit["base_path"])
        assert case["project_object_was_current"] is was_current
        expected = "yes" if was_current else "NO"
        for mode in ("python", "rust"):
            assert any(line.startswith("oracle object equals the project object: " + expected)
                       for line in read(f"{label}-source-regdiff-{mode}").splitlines())
        for binary in (False, True):
            tables, texts = [], []
            for mode in ("python", "rust"):
                name = f"{label}-calibration-{binary}-{mode}"
                expected_logs.add(name)
                texts.append(read(name).splitlines())
                tables.append(json.loads((project / f"calibration-{binary}-{mode}.json").read_text()))
            check = checks[f"calibration-binary-{binary}"]
            assert tables[0] and len(tables[0]) == check["rows"] and len(texts[0]) == check["lines"]
            equal(tables[0], tables[1], f"{label} {binary} calibration table")
            equal(texts[0], texts[1], f"{label} {binary} calibration report")
        if label == "echoes":
            expected_logs.update({label + "-colouring-python", label + "-colouring-rust"})
            equal(json.loads(read(label + "-colouring-python")), json.loads(read(label + "-colouring-rust")), label + " colouring")
            check = checks["legacy-saved-colouring"]
            assert sha(project / "saved.trace") == check["trace_sha256"]
            assert sha(project / "saved.o") == check["object_sha256"]
        # Revalidate the original archived file manifest too; imported source is unchanged.
        kit = root / (label + "-agent-workflow")
        original = json.loads((kit / "bundle-manifest.json").read_text())
        assert len(original["files"]) == case["archive"]["files"]
        for file in original["files"]:
            path = (kit / file["path"]).resolve()
            assert path.is_relative_to(kit) and path.stat().st_size == file["bytes"] and sha(path) == file["sha256"]
    logs = {row["name"]: row for row in evidence["logs"]}
    assert set(logs) == expected_logs and len(logs) == len(evidence["logs"])
    for name, row in logs.items():
        assert row["exit_code"] == 0, name
        for suffix in ("stdout", "stderr"):
            assert sha(root / f"{name}.{suffix}") == row[suffix + "_sha256"], (name, suffix)
    result = dict(complete=True, checks=total_checks, commands=len(logs), copied_inputs=total_inputs,
                  verification_sha256=sha(root / "verification.json"),
                  auditor_sha256=sha(__file__),
                  input_digest=hashlib.sha256(json.dumps([c["input_hashes"] for c in evidence["cases"]], sort_keys=True).encode()).hexdigest())
    (root / "audit.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"archived kits: {total_checks} comparisons, {len(logs)} successful commands and {total_inputs} copied inputs verified")


if __name__ == "__main__":
    main()
