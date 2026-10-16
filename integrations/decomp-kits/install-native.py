#!/usr/bin/env python3
"""Install the kit's checked native tools; compiler executables remain external."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import tempfile
import zipfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("--apply", action="store_true", help="write files (otherwise validate and show the destination)")
    args = parser.parse_args()
    if platform.machine().lower() not in ("amd64", "x86_64") or platform.system() not in ("Windows", "Linux"):
        parser.error("bundled tools support Windows/Linux x86-64; build mwcc-tools for other hosts")
    target = "x86_64-pc-windows-msvc" if os.name == "nt" else "x86_64-unknown-linux-gnu"
    kit = Path(__file__).resolve().parent
    manifest = json.loads((kit / "native-manifest.json").read_text())
    if manifest["format_version"] != 1:
        parser.error("unsupported native manifest format")
    artifact = manifest["artifacts"][target]
    archive = (kit / artifact["path"]).resolve()
    if not archive.is_relative_to(kit) or digest(archive.read_bytes()) != artifact["sha256"]:
        parser.error("native archive path/hash mismatch")
    project = args.project.expanduser().resolve()
    if not project.is_dir():
        parser.error("project directory does not exist")
    destination = project / ".agents/tools/mwcc" / target
    if not destination.resolve().is_relative_to(project):
        parser.error("native destination escapes project")
    with zipfile.ZipFile(archive) as bundle:
        names = bundle.namelist()
        if len(names) != len(set(names)) or bundle.testzip() is not None:
            parser.error("duplicate entries or invalid archive CRC")
        for name in names:
            path = PurePosixPath(name)
            if path.is_absolute() or ".." in path.parts or "\\" in name:
                parser.error("unsafe native archive path")
            if not (destination / name).resolve().is_relative_to(project):
                parser.error("native file destination escapes project")
        release = json.loads(bundle.read("release.json"))
        if (release["format_version"] != 1 or release["target"] != target
                or release["source_revision"] != manifest["source_revision"]
                or set(names) != set(release["files"]) | {"release.json"}):
            parser.error("native release identity/file set mismatch")
        payload = {}
        for name in names:
            data = bundle.read(name)
            if name != "release.json":
                entry = release["files"][name]
                if len(data) != entry["bytes"] or digest(data) != entry["sha256"]:
                    parser.error(f"native file hash mismatch: {name}")
            payload[name] = data
    executable = destination / release["executable"]
    if release["executable"] not in payload or digest(payload[release["executable"]]) != release["executable_sha256"]:
        parser.error("native executable identity mismatch")
    print(f"Validated {target}: {artifact['sha256']}")
    print(f"MWCC_TOOLS={executable}")
    if not args.apply:
        print("Use --apply to install these files and their notices.")
        return
    changes = 0
    for name, data in payload.items():
        path = destination / name
        if not path.is_file() or path.read_bytes() != data:
            path.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as temporary:
                temporary.write(data)
                temporary_name = Path(temporary.name)
            try:
                temporary_name.replace(path)
            finally:
                temporary_name.unlink(missing_ok=True)
            changes += 1
        path.chmod(0o755 if name == release["executable"] else 0o644)
    print(f"Installed native tools: {changes} file changes")


if __name__ == "__main__":
    main()
