"""Prepare new native-oracle kits from immutable archived consumers.

Kit installation/build support remains Python; oracle execution and analysis use
the native executable. Neither original archives nor game checkouts are edited.
"""
import argparse
import ast
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile

from verify_archived_kits import ROOT, sha, unpack, write_json


def strip_classes(path, names):
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines(keepends=True)
    spans = [(node.lineno - 1, node.end_lineno) for node in ast.parse(text).body
             if isinstance(node, ast.ClassDef) and node.name in names]
    assert {node.name for node in ast.parse(text).body if isinstance(node, ast.ClassDef) and node.name in names} == set(names)
    for start, end in reversed(spans):
        del lines[start:end]
    path.write_text("".join(lines), encoding="utf-8")


def replace_oracle_pin(value, pin):
    if isinstance(value, dict):
        origin = value.get("origin", value.get("url", "")).removesuffix(".git")
        if origin == "https://github.com/henriquegemignani/mwcc":
            value.update(revision=pin, branch="codex/oracle-rust-migration", inspected="2026-10-16",
                         source_bundle="references/mwcc-oracle-rust.bundle",
                         note="Native mwcc-tools and all 31 Rust observation hooks. Use the bundled source pin and native API guide; historical Python imports are not an interface.")
        for child in list(value.values()):
            replace_oracle_pin(child, pin)
    elif isinstance(value, list):
        for child in value:
            replace_oracle_pin(child, pin)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("prime_archive", "echoes_archive", "windows_release", "linux_release", "source_bundle", "output"):
        parser.add_argument("--" + name.replace("_", "-"), type=Path, required=True)
    parser.add_argument("--macos-arm64-release", type=Path)
    parser.add_argument("--macos-intel-release", type=Path)
    parser.add_argument("--source-pin", required=True)
    parser.add_argument("--release-label", default="rust-preview")
    args = parser.parse_args()
    assert len(args.source_pin) == 40 and all(c in "0123456789abcdef" for c in args.source_pin)
    assert re.fullmatch(r"[a-z0-9][a-z0-9-]*", args.release_label)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    assert not (output / "prime-agent-workflow").exists() and not (output / "echoes-agent-workflow").exists(), "Use a new output directory"
    releases = {"x86_64-pc-windows-msvc": args.windows_release.resolve(),
                "x86_64-unknown-linux-gnu": args.linux_release.resolve()}
    for target, archive in (("aarch64-apple-darwin", args.macos_arm64_release),
                            ("x86_64-apple-darwin", args.macos_intel_release)):
        if archive:
            releases[target] = archive.resolve()
    artifacts = {}
    for target, archive in releases.items():
        assert archive.with_suffix(".zip.sha256").read_text().split()[0] == sha(archive)
        with zipfile.ZipFile(archive) as bundle:
            assert bundle.testzip() is None
            release = json.loads(bundle.read("release.json"))
            assert release["target"] == target and release["source_revision"] == args.source_pin
            assert release["protocols"] == dict(compilation=1, oracle_report=6, register_difference=1)
        artifacts[target] = dict(path=f"native/{target}.zip", sha256=sha(archive),
                                 executable_sha256=release["executable_sha256"], source_catalogue_sha256=release["source_catalogue_sha256"])
    source_hash = sha(args.source_bundle)
    with tempfile.TemporaryDirectory(prefix="native source bundle ") as temporary:
        heads = subprocess.check_output(["git", "bundle", "list-heads", str(args.source_bundle.resolve())], text=True, cwd=temporary)
    assert any(line.split()[0] == args.source_pin for line in heads.splitlines())
    evidence = dict(format_version=1, complete=False, source_pin=args.source_pin,
                    release_label=args.release_label, source_bundle_sha256=source_hash, kits=[])
    for label, archive in (("prime", args.prime_archive), ("echoes", args.echoes_archive)):
        kit, baseline = unpack(archive.resolve(), output, label)
        (kit / "native").mkdir()
        (kit / "references").mkdir(exist_ok=True)
        for target, release in releases.items():
            shutil.copyfile(release, kit / artifacts[target]["path"])
        shutil.copyfile(args.source_bundle, kit / "references/mwcc-oracle-rust.bundle")
        shutil.copyfile(ROOT / "integrations/decomp-kits/install-native.py", kit / "install-native.py")
        native = dict(format_version=1, source_revision=args.source_pin, artifacts=artifacts,
                      source_bundle=dict(path="references/mwcc-oracle-rust.bundle", sha256=source_hash),
                      protocols=dict(compilation=1, oracle_report=6, register_difference=1))
        write_json(kit / "native-manifest.json", native)
        overlay = kit / "overlay"
        shutil.copytree(ROOT / "integrations/decomp-kits/overlay", overlay, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns("__pycache__"))
        external = json.loads((kit / "external-manifest.json").read_text())
        replace_oracle_pin(external, args.source_pin)
        external["bundle_date"] = "2026-10-16"
        external["native_oracle"] = dict(manifest="native-manifest.json", source_revision=args.source_pin,
                                         source_bundle_sha256=source_hash, api="native executable and versioned JSON")
        write_json(kit / "external-manifest.json", external)
        guides_path = kit / "MCP-GUIDES.json"
        guides = json.loads(guides_path.read_text())
        guides["release"] = "2026-10-16"
        write_json(guides_path, guides)
        version = "GC_1_3_2" if label == "prime" else "GC_2_7"
        setup = f'''```text
python <kit>/install-native.py <project> --apply
git clone <kit>/references/mwcc-oracle-rust.bundle <mwcc-oracle>
git -C <mwcc-oracle> checkout --detach {args.source_pin}
rustup target add i686-pc-windows-msvc --toolchain 1.96.0
mwcc-tools configure --root <mwcc-oracle> --version {version} --oracle --producer rust
ninja -C <mwcc-oracle> build/{version}/oracle/mwcceppc.exe
```

`install-native.py` validates the host archive and installs its executable and notices
under `<project>/.agents/tools/mwcc/TARGET/`. Add its `bin` directory to PATH, or set
`MWCC_TOOLS` to the printed executable path. Set `MWCC_ORACLE_ROOT` to the absolute
source checkout path. On PowerShell use `$env:MWCC_TOOLS = 'ABSOLUTE_PATH'` and
`$env:MWCC_ORACLE_ROOT = 'ABSOLUTE_PATH'`; on POSIX shells use `export NAME='ABSOLUTE_PATH'`.
The optional old-name launchers discover the installed executable, but automation
should invoke `mwcc-tools` directly. Compiler tools/runtime are still external;
Ninja downloads pinned dependencies. Linux oracle execution uses wibo 1.2.0.
Building hooks requires Rust 1.96.0; the prebuilt host executable itself does not.

The complete source bundle provides the pin locally without assuming a public
remote already contains it. Its SHA-256 and the native artifacts are recorded in
`native-manifest.json`. Verify them before adoption. Original kit archives remain
unchanged. This oracle checkout shares no history with the separate MWCC reference.

Use [.agents/docs/oracle-native.md](overlay/.agents/docs/oracle-native.md) for native
unit commands, explicit check states, structured regdiff, saved graphs and calibration.
Report schema 6 and native envelopes version 1 are supported. Python oracle/helper
imports and human report text are not machine APIs. Existing unrelated kit helpers
and installation support still use Python.

'''
        externals_path = kit / "EXTERNALS.md"
        text = externals_path.read_text(encoding="utf-8")
        start = text.index("```sh\ngit clone --branch oracle https://github.com/henriquegemignani/mwcc.git")
        end = text.index("## Compiler debugger", start)
        externals_path.write_text(text[:start] + setup + text[end:], encoding="utf-8")
        for relative in ("README.md", "overlay/scripts/README.md", "overlay/.agents/docs/README.md", "overlay/.agents/LOCAL.example.md"):
            path = kit / relative
            if relative == "overlay/.agents/docs/README.md" and not path.exists():
                continue
            text = path.read_text(encoding="utf-8")
            notice = "\nNative oracle (2026-10-16): use `mwcc-tools` and versioned JSON; see "
            notice += ("`EXTERNALS.md` and `overlay/.agents/docs/oracle-native.md`." if relative == "README.md" else "`.agents/docs/oracle-native.md` and the kit's `EXTERNALS.md`.")
            notice += " Legacy-name Python launchers are optional; Python helper imports are not an API.\n"
            first, separator, rest = text.partition("\n")
            path.write_text(first + separator + notice + "\n" + rest, encoding="utf-8")
        readme = kit / "README.md"
        text = readme.read_text(encoding="utf-8")
        text = re.sub(r"(?m)^Release:? \*\*[^\n]+", f"Release: **2026-10-16 native** · `{label}-agent-workflow-mcp-2026-10-16-{args.release_label}.zip`", text, count=1)
        readme.write_text(text, encoding="utf-8")
        release_path = kit / "RELEASE.md"
        previous = release_path.read_text(encoding="utf-8") if release_path.exists() else ""
        release_path.write_text("# Release 2026-10-16 native\n\nNative oracle commands and versioned JSON replace Python oracle imports. Includes checked Windows/Linux native archives, a complete pinned source bundle, an installer, updated documentation and native contract tests. Rust is the default observation producer, with an explicit C rollback. Unrelated game workflows and historical research are retained. These are local release artifacts; public publication and whole-game rebuilds are separate actions.\n\n---\n\n" + previous, encoding="utf-8")
        tests = kit / ("tests/test_helpers.py" if label == "prime" else "overlay/scripts/tests/test_agent_tools.py")
        strip_classes(tests, ["InlineCalibrateTests"] + (["OracleColouringTests"] if label == "echoes" else []))
        if label == "prime":
            text = tests.read_text(encoding="utf-8")
            assert "'string-array-scan', 'inline-calibrate'" in text
            tests.write_text(text.replace("'string-array-scan', 'inline-calibrate'", "'string-array-scan'"), encoding="utf-8")
        else:
            selftest = overlay / "scripts/selftest.py"
            text = selftest.read_text(encoding="utf-8")
            old = "if result.returncode or 'usage:' not in result.stdout:"
            assert text.count(old) == 1
            text = text.replace(old, "marker = 'mwcc-tools' if name in ('oracle-run', 'oracle-regdiff', 'oracle-colouring', 'inline-calibrate') else 'usage:'\n        if result.returncode or marker not in result.stdout:")
            selftest.write_text(text, encoding="utf-8")
        bundle_test = kit / "tests/test_bundle.py"
        text = bundle_test.read_text(encoding="utf-8")
        for name in ("inline-calibrate.py", "oracle-regdiff.py", "oracle-run.py", "oracle-colouring.py"):
            text = text.replace(repr(name) + ", ", "")
        bundle_test.write_text(text, encoding="utf-8")
        shutil.copyfile(ROOT / "integrations/decomp-kits/test_native_oracle.py", kit / "tests/test_native_oracle.py")
        for version in ("GC_1_3", "GC_1_3_2", "GC_2_7"):
            path = kit / f"tests/native-fixtures/{version}.trace"
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / f"tests/fixtures/oracle/{version}/pipeline.trace", path)
        names = set(json.loads((kit / "bundle-files.json").read_text()))
        names.update(["install-native.py", "native-manifest.json", "references/mwcc-oracle-rust.bundle", "tests/test_native_oracle.py", "RELEASE.md"])
        names.update(a["path"] for a in artifacts.values())
        names.update(f"tests/native-fixtures/{version}.trace" for version in ("GC_1_3", "GC_1_3_2", "GC_2_7"))
        names.update(p.relative_to(kit).as_posix() for p in (overlay / ".agents/docs").rglob("oracle-native.md"))
        names.update(p.relative_to(kit).as_posix() for p in (overlay / "scripts").glob("*mwcc_native.py"))
        names.update(f"overlay/scripts/{name}" for name in ("oracle-run.py", "oracle-regdiff.py", "oracle-colouring.py", "inline-calibrate.py"))
        write_json(kit / "bundle-files.json", sorted(names))
        write_json(kit / "bundle-manifest.json", dict(schema_version=1, release="2026-10-16", files=[
            dict(path=name, bytes=(kit / name).stat().st_size, sha256=sha(kit / name)) for name in sorted(names)]))
        evidence["kits"].append(dict(kit=label, baseline=baseline, source=str(kit), files=len(names)))
        assert sha(archive) == baseline["sha256"]
    evidence["complete"] = True
    write_json(output / "adoption-inputs.json", evidence)
    print(output)


if __name__ == "__main__":
    main()
