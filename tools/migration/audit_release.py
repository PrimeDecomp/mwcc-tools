"""Independently inspect a native release and run it outside its source checkout."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import tempfile
import zipfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--oracle-root", type=Path, help="Also check compilation with the installed oracle for all three versions")
    args = parser.parse_args()
    archive = args.archive.resolve()
    root = args.source_root.resolve()
    expected_hash = archive.with_suffix(".zip.sha256").read_text().split()[0]
    assert digest(archive.read_bytes()) == expected_hash
    with zipfile.ZipFile(archive) as bundle:
        names = bundle.namelist()
        assert len(names) == len(set(names)) and names == sorted(names)
        assert bundle.testzip() is None
        for info in bundle.infolist():
            name = PurePosixPath(info.filename)
            assert not name.is_absolute() and ".." not in name.parts and "\\" not in info.filename
            assert info.date_time == (1980, 1, 1, 0, 0, 0)
        meta = json.loads(bundle.read("release.json"))
        assert meta["format_version"] == 1 and meta["package"] == "mwcc-tools"
        assert meta["protocols"] == dict(compilation=1, register_difference=1, oracle_report=6)
        assert set(names) == set(meta["files"]) | {"release.json"}
        assert bundle.getinfo(meta["executable"]).external_attr >> 16 & 0o777 == 0o755
        for name, entry in meta["files"].items():
            data = bundle.read(name)
            assert len(data) == entry["bytes"] and digest(data) == entry["sha256"], name
        for name, sha in meta["source_files"].items():
            path = PurePosixPath(name)
            assert not path.is_absolute() and ".." not in path.parts and "\\" not in name
            assert digest((root / name).read_bytes()) == sha, name
        catalogue = json.dumps(meta["source_files"], sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()
        assert digest(catalogue) == meta["source_catalogue_sha256"]
        assert digest(args.binary.read_bytes()) == meta["executable_sha256"] == digest(bundle.read(meta["executable"]))
        inventory = json.loads(bundle.read(f"licenses/dependencies/{meta['target']}/inventory.json"))
        assert inventory["cargo_lock_sha256"] == meta["cargo_lock_sha256"] == digest((root / "Cargo.lock").read_bytes())
        assert inventory["target"] == meta["target"] and inventory["rustc"] == meta["notice_inventory_rustc"]
        for name, sha in inventory["files"].items():
            assert digest(bundle.read(f"licenses/dependencies/{meta['target']}/{name}")) == sha
        assert (os.name == "nt") == ("windows" in meta["target"]), "Run on the archive's native host"
        with tempfile.TemporaryDirectory(prefix="mwcc standalone release ") as temporary:
            working = Path(temporary)
            bundle.extractall(working)
            executable = working / meta["executable"]
            executable.chmod(0o755)
            commands = []
            def run(binary, *arguments):
                result = subprocess.run([str(binary), *map(str, arguments)], cwd=working, capture_output=True, timeout=120, check=True)
                assert not result.stderr, result.stderr.decode(errors="replace")
                commands.append(dict(arguments=list(map(str, arguments)), stdout_sha256=digest(result.stdout)))
                return result.stdout
            assert run(executable, "--version").decode().strip() == f"mwcc-tools {meta['version']}"
            assert b"oracle compile-trace" in run(executable, "--help")
            traces = 0
            for version in ("GC_1_3", "GC_1_3_2", "GC_2_7"):
                trace = working / f"{version}.trace"
                trace.write_bytes((root / f"tests/fixtures/oracle/{version}/pipeline.trace").read_bytes())
                arguments = ("oracle", "analyze-trace", trace, "--version", version, "--raw")
                expected = json.loads(run(args.binary.resolve(), *arguments))
                actual = json.loads(run(executable, *arguments))
                assert expected == actual and actual["schema_version"] == 6
                traces += 1
            compilations = []
            if args.oracle_root:
                oracle = args.oracle_root.resolve()
                source = root / "tests/fixtures/oracle/sources/pipeline.cpp"
                for version in ("GC_1_3", "GC_1_3_2", "GC_2_7"):
                    actual = json.loads(run(executable, "oracle", "compile-trace", source,
                                            "--oracle-root", oracle, "--version", version,
                                            "--", "-nodefaults", "-proc", "gekko", "-O4,p"))
                    assert actual["format_version"] == 1 and actual["report"]["schema_version"] == 6
                    compilation = actual["compilation"]
                    assert compilation["check_status"]["status"] == "verified", compilation["check_status"]
                    producer_hash = digest((oracle / f"build/{version}/oracle/mwcceppc.exe").read_bytes())
                    assert compilation["producer_sha256"] == producer_hash
                    assert compilation["source_sha256"] == digest(source.read_bytes())
                    compilations.append(dict(version=version, producer_sha256=producer_hash,
                                             object_sha256=compilation["object_sha256"], check_status="verified"))
    evidence = dict(complete=True, archive_sha256=expected_hash, binary_sha256=meta["executable_sha256"],
                    source_catalogue_sha256=meta["source_catalogue_sha256"], target=meta["target"],
                    files=len(names), standalone_trace_cases=traces, checked_compilations=compilations, commands=commands)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(f"release audit: {len(names)} files, matching metadata/notices, {traces} standalone trace cases, {len(compilations)} checked compilations")


if __name__ == "__main__":
    main()
