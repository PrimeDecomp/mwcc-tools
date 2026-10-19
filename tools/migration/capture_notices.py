"""Collect exact local dependency and Rust library notices for a release target.

This is a conservative inventory of the resolved Cargo graph, including build
dependencies; it does not assert that every listed crate is linked into the CLI.
Notices are copied verbatim from the locked sources. A crate that ships no licence
file, and that no other crate of its project covers, gets the standard text of each
licence it declares (MIT, Apache-2.0: the pinned toolchain's SPDX copies), labelled
as such; no licence alternative is chosen.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def run(*arguments):
    return subprocess.check_output(arguments, cwd=ROOT, text=True, encoding="utf-8")


# Crates of the wasmtime project that publish no licence file, and the crate of the
# same project whose verbatim LICENSE they carry instead (each of the project's
# crates in the graph that ships one ships the same file, under the same licence).
PROJECT_NOTICES = {
    "cranelift-assembler-x64": "cranelift-codegen",
    "cranelift-assembler-x64-meta": "cranelift-codegen",
    "cranelift-bitset": "cranelift-codegen",
    "cranelift-isle": "cranelift-codegen",
    "cranelift-srcgen": "cranelift-codegen",
    "pulley-interpreter": "wasmtime",
    "pulley-macros": "wasmtime",
    "wasmtime-internal-component-util": "wasmtime",
    "wasmtime-internal-core": "wasmtime",
    "wasmtime-internal-jit-debug": "wasmtime",
    "wasmtime-internal-jit-icache-coherence": "wasmtime",
    "wasmtime-internal-versioned-export-macros": "wasmtime",
}


# The licences whose standard text a crate without a licence file gets.
STANDARD = {"MIT": "MIT.txt", "Apache-2.0": "Apache-2.0.txt"}


def standard_texts(package, licenses):
    """The standard texts of PACKAGE's declared licences, or an error naming the one there is none for."""
    names = [n.strip("() ") for n in package["license"].replace("/", " OR ").split(" OR ")]
    unknown = [n for n in names if n not in STANDARD]
    assert not unknown, f"No licence file in {package['name']} {package['version']}, and no standard text for {unknown}"
    texts = []
    for name in names:
        data = (licenses / STANDARD[name]).read_bytes()
        if name == "MIT":
            holders = ", ".join(package.get("authors") or []) or f"the authors of {package['name']}"
            data = data.replace(b"Copyright (c) <year> <copyright holders>", f"Copyright (c) {holders}".encode())
        texts.append((name, data))
    return texts


def notice_files(base):
    return sorted(p for p in base.rglob("*") if p.is_file()
                  and p.name.upper().startswith(("LICENSE", "LICENCE", "COPYING", "NOTICE", "UNLICENSE")))


def collect(target):
    metadata = json.loads(run("cargo", "metadata", "--locked", "--offline", "--format-version", "1", "--filter-platform", target))
    resolved = {node["id"] for node in metadata["resolve"]["nodes"]}
    by_name = {}
    for package in metadata["packages"]:
        if package["id"] in resolved:
            by_name.setdefault(package["name"], []).append(package)
    licenses = Path(run("rustc", "--print", "sysroot").strip()) / "share/doc/rust/licenses"
    packages, blocks = [], {}
    for package in sorted(metadata["packages"], key=lambda p: (p["name"], p["version"])):
        if package["id"] not in resolved:
            continue
        base = Path(package["manifest_path"]).parent
        workspace = package["id"] in metadata["workspace_members"]
        carried = None
        if workspace:
            paths = [ROOT / "LICENSE"]
        else:
            paths = notice_files(base)
            if not paths and package["name"] in PROJECT_NOTICES:
                [donor] = by_name[PROJECT_NOTICES[package["name"]]]
                assert donor["license"] == package["license"], (package["name"], donor["name"])
                carried = f"{donor['name']} {donor['version']}"
                base = Path(donor["manifest_path"]).parent
                paths = [base / "LICENSE"]
                assert paths[0].is_file(), paths[0]
            if package.get("license_file"):
                explicit = (base / package["license_file"]).resolve()
                assert explicit.is_relative_to(base.resolve()), (package["name"], explicit)
                if explicit not in paths:
                    paths.append(explicit)
        assert package["license"] or package.get("license_file"), package["name"]
        notices = []
        if not paths:
            # (no licence file in the crate: its declared licences' standard texts, labelled)
            for name, data in standard_texts(package, licenses):
                sha256 = digest(data)
                notices.append(dict(path=None, standard=name, sha256=sha256))
                owner = (f"{package['name']} {package['version']}: the standard {name} text "
                         "(the crate ships no licence file; its Cargo.toml declares the licence)")
                block = blocks.setdefault(sha256, dict(data=data, owners=[]))
                block["owners"].append(owner)
        for path in sorted(paths, key=lambda p: p.relative_to(ROOT if workspace else base).as_posix()):
            data = path.read_bytes()
            assert data, path
            relative = path.relative_to(ROOT if workspace else base).as_posix()
            sha256 = digest(data)
            notice = dict(path=relative, sha256=sha256)
            owner = f"{package['name']} {package['version']}: {relative}"
            if carried:
                notice["carried_from"] = carried
                owner += f" (the project's, as {carried} ships it)"
            notices.append(notice)
            block = blocks.setdefault(sha256, dict(data=data, owners=[]))
            block["owners"].append(owner)
        packages.append(dict(name=package["name"], version=package["version"], license=package["license"],
            source="workspace" if workspace else package["source"], notices=notices))
    # Retain additional decoder notices even when they live beneath a vendored
    # source directory rather than beside the capstone-sys Cargo manifest.
    decoder = next(p for p in packages if p["name"] == "capstone-sys")
    assert all(any(n["path"] == name for n in decoder["notices"])
               for name in ["capstone/LICENSE.TXT", "capstone/LICENSE_LLVM.TXT"])
    header = (f"mwcc-tools third-party notices: {target}\n"
              "Resolved Cargo graph, including build dependencies. See inventory.json.\n"
              "The notices are copied verbatim from the locked sources, except those labelled as a\n"
              "standard licence text: those crates ship no licence file.\n\n").encode()
    sections = [header]
    for sha256, block in sorted(blocks.items()):
        sections.append(("=" * 72 + "\n" + "\n".join(sorted(block["owners"])) +
                         f"\nSHA-256: {sha256}\n" + "=" * 72 + "\n").encode())
        sections.extend([block["data"], b"\n\n"])
    return packages, blocks, b"".join(sections)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", help="defaults to the installed rustc host")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true", help="verify the existing inventory without changing it")
    args = parser.parse_args()
    rustc = run("rustc", "-vV")
    host = next(line.removeprefix("host: ") for line in rustc.splitlines() if line.startswith("host: "))
    target = args.target or host
    assert target == host, "Run this collector on the release target host; Rust library notices must come from that toolchain"
    packages, blocks, notices = collect(target)
    sysroot = Path(run("rustc", "--print", "sysroot").strip())
    rust_docs = sysroot / "share/doc/rust"
    rust_files = [rust_docs / "COPYRIGHT-library.html", *sorted((rust_docs / "licenses").glob("*"))]
    assert len(rust_files) > 1 and all(p.is_file() for p in rust_files), rust_docs
    # The library attribution is the library-specific notice, not the much
    # broader compiler/source-toolchain copyright inventory.
    outputs = {"THIRD-PARTY-NOTICES.txt": notices}
    for path in rust_files:
        outputs["rust/" + path.relative_to(rust_docs).as_posix()] = path.read_bytes()
    inventory = dict(format_version=1, target=target, cargo_lock_sha256=digest((ROOT / "Cargo.lock").read_bytes()),
        rustc=rustc.splitlines(), scope="Resolved Cargo graph including build dependencies; Rust standard-library notices",
        packages=packages, notice_blocks={sha256: sorted(block["owners"]) for sha256, block in sorted(blocks.items())},
        files={name: digest(data) for name, data in outputs.items()})
    outputs["inventory.json"] = (json.dumps(inventory, indent=2) + "\n").encode()
    if args.check:
        assert {p.relative_to(args.output).as_posix() for p in args.output.rglob("*") if p.is_file()} == set(outputs), "Inventory file set differs"
        for name, data in outputs.items():
            assert (args.output / name).read_bytes() == data, f"Stale or modified notice inventory: {name}"
        print(f"{target}: locked dependency and Rust library notices verified")
        return
    args.output.mkdir(parents=True, exist_ok=True)
    for name, data in outputs.items():
        path = args.output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    print(f"{target}: {len(packages)} resolved packages, {len(blocks)} verbatim notice blocks and {len(rust_files)} Rust library notice files")


if __name__ == "__main__":
    main()
