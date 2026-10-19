"""Capture download/extraction contracts using synthetic inputs and the pinned Python implementation."""
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "tests/fixtures/download"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def archive(entries, compression=zipfile.ZIP_DEFLATED):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w", compression=compression) as output:
        for name, data in entries.items():
            info = zipfile.ZipInfo(name, date_time=(2000, 1, 1, 0, 0, 0))
            info.compress_type = compression
            output.writestr(info, data)
    return stream.getvalue()


def record(name, lba, size, directory=False):
    name = name.encode()
    data = bytearray(33 + len(name) + (not len(name) % 2))
    data[0] = len(data)
    struct.pack_into("<I", data, 2, lba)
    struct.pack_into(">I", data, 6, lba)
    struct.pack_into("<I", data, 10, size)
    struct.pack_into(">I", data, 14, size)
    data[25] = 2 if directory else 0
    data[28:32] = b"\x01\x00\x00\x01"
    data[32] = len(name)
    data[33:33 + len(name)] = name
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    args = parser.parse_args()
    source = args.oracle_root / "tools/download_tool.py"
    spec = importlib.util.spec_from_file_location("reference_download", source)
    reference = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reference)
    OUT.mkdir(parents=True, exist_ok=True)
    manifest = dict(reference_commit=subprocess.check_output(
        ["git", "-C", str(args.oracle_root), "rev-parse", "HEAD"], text=True).strip(),
        reference_sha256=sha(source.read_bytes()))
    urls = []
    for system, machine in [("Windows", "AMD64"), ("Windows", "ARM64"), ("Linux", "x86_64"),
                            ("Linux", "aarch64"), ("Linux", "arm64"), ("Darwin", "arm64"), ("Darwin", "x86_64")]:
        reference.platform.system = lambda: system
        reference.platform.machine = lambda: machine
        urls.append(dict(system=system, machine=machine, objdiff=reference.objdiff_url(), wibo=reference.wibo_url()))
    manifest["urls"] = urls

    # The directory entry follows sector padding, and the stored member crosses request blocks.
    payload = bytes(range(256)) * 1100
    member = "Tools/Command Line Tools/mwcc.exe"
    zipped = archive({member: payload, "unused.bin": b"unused"}, zipfile.ZIP_STORED)
    image = bytearray(384 * 2048)
    image[16 * 2048:16 * 2048 + 7] = b"\x01CD001\x01"
    image[16 * 2048 + 156:16 * 2048 + 190] = record("\0", 20, 4096, True)
    entry = record("CODEWA~1", 22, 2048, True)
    image[21 * 2048:21 * 2048 + len(entry)] = entry
    entry = record("CODEWA~1.ZIP;1", 127, len(zipped))
    image[22 * 2048:22 * 2048 + len(entry)] = entry
    image[127 * 2048:127 * 2048 + len(zipped)] = zipped
    (OUT / "disc.iso").write_bytes(image)
    discs = []
    for raw in (False, True):
        data = bytes(image) if not raw else b"".join(
            b"\xa5" * 24 + image[i:i + 2048] + b"\x5a" * 280 for i in range(0, len(image), 2048))
        requests = []

        def fetch(url, headers=None):
            first, last = map(int, headers["Range"].removeprefix("bytes=").split("-"))
            requests.append([first, last])
            return data[first:last + 1]

        reference.fetch = fetch
        disc = reference.Disc("fixture", raw)
        file = disc.file("CODEWA~1/CODEWA~1.ZIP;1")
        decoded = zipfile.ZipFile(file).read(member)
        assert decoded == payload
        # Repeating the extraction must reuse the same disc block cache.
        count = len(requests)
        assert zipfile.ZipFile(disc.file("CODEWA~1/CODEWA~1.ZIP;1")).read(member) == payload
        assert len(requests) == count
        discs.append(dict(raw=raw, requests=requests, member_sha256=sha(decoded)))
    manifest["discs"] = discs
    manifest["disc_member"] = member

    entries = {}
    for _, prefix, files in reference.LIB:
        for relative in files or ["fixture.h", "nested/fixture.c"]:
            entries[prefix + "/" + relative] = ("fixture: " + prefix + "/" + relative + "\r\n").encode()
    entries["unrelated/ignored.c"] = b"ignored"
    entries["Win32-x86 Support/Headers/Win32 SDK/ignored.h"] = b"ignored"

    class FakeDisc:
        def __init__(self, *_):
            pass

        def file(self, name):
            assert name == "CODEWA~1.ZIP;1"
            return io.BytesIO(archive(entries))

    def updater(root):
        destination = root / "Win32-x86_Support/Libraries/Runtime/(Sources)/nested/runtime.c"
        destination.parent.mkdir(parents=True)
        destination.write_bytes(b"fixture updater runtime\r\n")
        return root

    reference.Disc = FakeDisc
    reference.pro53_updater = updater
    patches = {}
    original_patch = reference.patch
    reference.patch = lambda path, *replacements: patches.update({path.name: replacements})
    with tempfile.TemporaryDirectory() as temporary:
        reference.lib(Path(temporary) / "ok")
    reference.patch = original_patch
    for _, prefix, files in reference.LIB:
        for relative in files or []:
            if relative in patches:
                entries[prefix + "/" + relative] = b"// synthetic patch input\r\n" + b"\r\n".join(
                    old for old, _ in patches[relative]) + b"\r\n"
    (OUT / "library.zip").write_bytes(archive(entries))
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        reference.lib(root / "ok")
        manifest["library_files"] = {p.relative_to(root).as_posix(): sha(p.read_bytes())
                                     for p in sorted(root.rglob("*")) if p.is_file()}

    updater = bytearray(512)
    offsets = [17, 211]
    updater_members = {}
    for offset, name, content in zip(offsets, ["data1.cab", "data1.hdr"], [b"fixture cabinet", b"fixture header"]):
        compressor = zlib.compressobj(wbits=-15)
        compressed = compressor.compress(content) + compressor.flush()
        name = name.encode()
        header = struct.pack("<I5H3I2H", 0x04034B50, 20, 0, 8, 0, 0,
                             zlib.crc32(content), len(compressed), len(content), len(name), 0)
        updater[offset:offset + len(header) + len(name) + len(compressed)] = header + name + compressed
        updater_members[name.decode()] = sha(content)
    (OUT / "updater.bin").write_bytes(updater)
    manifest.update(updater_offsets=offsets, updater_members=updater_members,
                    files={p.name: sha(p.read_bytes()) for p in sorted(OUT.iterdir()) if p.name != "manifest.json"})
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"captured {len(discs)} disc layouts, {len(manifest['library_files'])} library outputs, {len(urls)} URL mappings")


if __name__ == "__main__":
    main()
