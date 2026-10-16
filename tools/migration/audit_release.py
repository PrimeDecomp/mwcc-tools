"""Independently inspect a native release and run it outside its source checkout."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import platform
import struct
import subprocess
import tempfile
import zipfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def native_target(system=None, machine=None):
    """Match the running host explicitly, rather than grouping every POSIX OS."""
    system = platform.system() if system is None else system
    machine = platform.machine() if machine is None else machine
    architecture = {"amd64": "x86_64", "x86_64": "x86_64",
                    "arm64": "aarch64", "aarch64": "aarch64"}.get(machine.lower())
    target = {
        ("Windows", "x86_64"): "x86_64-pc-windows-msvc",
        ("Linux", "x86_64"): "x86_64-unknown-linux-gnu",
        ("Darwin", "x86_64"): "x86_64-apple-darwin",
        ("Darwin", "aarch64"): "aarch64-apple-darwin",
    }.get((system, architecture))
    if target is None:
        raise RuntimeError(f"unsupported native release audit host {system}/{machine}")
    return target


def validate_native_binary(data, target):
    """Independently check the target's executable format and CPU before launch."""
    if target == "x86_64-pc-windows-msvc":
        assert len(data) >= 64 and data[:2] == b"MZ", "release executable is not PE"
        pe = struct.unpack_from("<I", data, 60)[0]
        assert pe + 26 <= len(data) and data[pe:pe+4] == b"PE\0\0", "truncated PE header"
        machine, = struct.unpack_from("<H", data, pe+4)
        optional_size, flags = struct.unpack_from("<HH", data, pe+20)
        assert machine == 0x8664, "release executable is not x86-64 PE"
        assert optional_size >= 2 and pe+24+optional_size <= len(data), "truncated PE optional header"
        assert data[pe+24:pe+26] == b"\x0b\x02" and flags & 2 and not flags & 0x2000, "PE is not a 64-bit executable"
        return dict(format="pe", architecture="x86_64")
    if target == "x86_64-unknown-linux-gnu":
        assert len(data) >= 64 and data[:7] == b"\x7fELF\x02\x01\x01", "release executable is not little-endian ELF64"
        kind, machine, version = struct.unpack_from("<HHI", data, 16)
        assert machine == 62 and kind in (2, 3) and version == 1, "ELF is not an x86-64 executable"
        return dict(format="elf", architecture="x86_64")
    cpu = {"x86_64-apple-darwin": 0x01000007,
           "aarch64-apple-darwin": 0x0100000c}.get(target)
    assert cpu is not None, "unsupported release target"
    assert len(data) >= 32 and data[:4] == b"\xcf\xfa\xed\xfe", "release executable is not thin little-endian Mach-O64"
    observed_cpu, _, kind, count, size = struct.unpack_from("<IIIII", data, 4)
    assert observed_cpu == cpu and kind == 2, "Mach-O target CPU or executable type mismatch"
    assert count > 0 and count <= size//8 and 32+size <= len(data), "invalid Mach-O load-command extent"
    position = 32
    for _ in range(count):
        assert position+8 <= 32+size, "truncated Mach-O load command"
        _, command_size = struct.unpack_from("<II", data, position)
        assert command_size >= 8 and command_size % 8 == 0 and position+command_size <= 32+size, "invalid Mach-O load command size"
        position += command_size
    assert position == 32+size, "Mach-O load-command count/extent mismatch"
    return dict(format="mach_o", architecture="x86_64" if cpu == 0x01000007 else "aarch64")


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
        host_target = native_target()
        assert meta["target"] == host_target, f"run {meta['target']} audit on its native host/architecture, not {host_target}"
        executable_identity = validate_native_binary(bundle.read(meta["executable"]), meta["target"])
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
                    native_host=dict(system=platform.system(), machine=platform.machine(), target=host_target),
                    executable_identity=executable_identity,
                    files=len(names), standalone_trace_cases=traces, checked_compilations=compilations, commands=commands)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(f"release audit: {len(names)} files, matching metadata/notices, {traces} standalone trace cases, {len(compilations)} checked compilations")


if __name__ == "__main__":
    main()
