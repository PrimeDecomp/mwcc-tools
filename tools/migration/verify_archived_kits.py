"""Verify immutable kit archives and their original oracle pins on isolated real units.

No game checkout is configured, built or edited. Each run retains its copied inputs,
the original scripts, command logs and a content-hashed result manifest.
"""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile

from compare_backends import difference, normalize

ROOT = Path(__file__).resolve().parents[2]
CASES = {
    "prime": ("main/GuiSys/CGuiFrame", "Draw", "GM8E01_00", "GC_1_3_2",
              "f00b8a8201d0dca13c2206fce61472b02d83b01e"),
    "echoes": ("main/Kyoto/Particles/CElementGen", "RenderParticles", "G2ME01", "GC_2_7",
               "b75c4e5e0b4bc97ae06c26ea9d6aad8379ac1722"),
}


def sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def host_ninja(data):
    """Translate Windows edge/version paths for the legacy Linux parser; leave flags intact."""
    return re.sub(rb"(?m)^(?:build [^\r\n]*: mwcc\w* [^\r\n]*|  mw_version = [^\r\n]*)",
                  lambda match: match.group().replace(b"\\", b"/"), data)


def unpack(archive, destination, label):
    digest = sha(archive)
    if archive.with_suffix(archive.suffix + ".sha256").read_text().split()[0] != digest:
        raise RuntimeError(f"{label}: archive sidecar mismatch")
    prefix = label + "-agent-workflow"
    with zipfile.ZipFile(archive) as bundle:
        if bundle.testzip() is not None:
            raise RuntimeError(f"{label}: archive CRC failure")
        names = bundle.namelist()
        if len(set(names)) != len(names):
            raise RuntimeError(f"{label}: duplicate archive entries")
        for name in names:
            parts = Path(name.replace("\\", "/")).parts
            if not parts or parts[0] != prefix or not (destination / name).resolve().is_relative_to(destination):
                raise RuntimeError(f"{label}: unsafe archive entry {name!r}")
        bundle.extractall(destination)
    kit = destination / prefix
    manifest = json.loads((kit / "bundle-manifest.json").read_text())
    expected = set()
    for row in manifest["files"]:
        path = (kit / row["path"]).resolve()
        if not path.is_relative_to(kit) or path.stat().st_size != row["bytes"] or sha(path) != row["sha256"]:
            raise RuntimeError(f"{label}: manifest mismatch {row['path']}")
        expected.add(row["path"])
    actual = {p.relative_to(kit).as_posix() for p in kit.rglob("*") if p.is_file()}
    if actual != expected | {"bundle-manifest.json"}:
        raise RuntimeError(f"{label}: manifest does not cover the archive")
    external = json.loads((kit / "external-manifest.json").read_text())
    # The two kits deliberately have different external-manifest schemas.
    pins = []
    def walk(value):
        if isinstance(value, dict):
            if value.get("origin", value.get("url", "")).removesuffix(".git") == "https://github.com/henriquegemignani/mwcc":
                pins.append(value.get("revision", value.get("commit")))
            for child in value.values():
                walk(child)
        elif isinstance(value, list):
            for child in value:
                walk(child)
    walk(external)
    if CASES[label][4] not in pins:
        raise RuntimeError(f"{label}: expected producer pin is absent: {pins}")
    return kit, dict(path=str(archive), sha256=digest, files=len(expected), producer_pin=CASES[label][4])


def snapshot(project, unit_name):
    config = json.loads((project / "objdiff.json").read_text())
    unit = next(u for u in config["units"] if u["name"] == unit_name)
    paths = {"build.ninja", "objdiff.json", unit["base_path"], unit["target_path"]}
    directories = {"src", "include", "libc", "config"}
    ninja = re.sub(r"\$\r?\n\s*", "", (project / "build.ninja").read_text())
    block = None
    for variant in (unit["base_path"], unit["base_path"].replace("/", "\\")):
        block = re.search(r"^build " + re.escape(variant) + r": mwcc\w* [^\n]*\n((?:  [^\n]*\n)+)", ninja, re.M)
        if block:
            break
    if block is None:
        raise RuntimeError(f"no compile edge for {unit['base_path']}")
    flags = re.search(r"^  cflags = (.*)$", block.group(1), re.M)
    if flags is None:
        raise RuntimeError(f"no cflags for {unit['base_path']}")
    arguments = iter(shlex.split(flags.group(1)))
    for arg in arguments:
        if arg in ("-i", "-I"):
            name = next(arguments).replace("\\", "/")
        elif arg.startswith(("-i", "-I")) and len(arg) > 2 and arg[:3] not in ("-in", "-ir"):
            name = arg[2:].replace("\\", "/")
        else:
            continue
        path = (project / name).resolve()
        if path.is_dir():
            if not path.is_relative_to(project):
                raise RuntimeError(f"external include directory cannot be isolated: {path}")
            directories.add(path.relative_to(project).as_posix())
    for name in sorted(directories):
        if (project / name).is_dir():
            paths.update(p.relative_to(project).as_posix() for p in (project / name).rglob("*") if p.is_file())
    return unit, {p: sha(project / p) for p in sorted(paths)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("prime", "echoes"):
        parser.add_argument(f"--{name}-archive", type=Path, required=True)
        parser.add_argument(f"--{name}-project", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--oracle-root", type=Path, required=True, help="frozen b75c4e5 reference with build dependencies")
    parser.add_argument("--native-root", type=Path, required=True, help="fully Rust producer proof tree")
    parser.add_argument("--objdiff", type=Path, required=True)
    parser.add_argument("--wibo", type=Path)
    parser.add_argument("--pin-archive", action="append", default=[], metavar="COMMIT=TAR",
                        help="git archive tar, for hosts unable to read the source checkout's Git metadata")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    for key in ("binary", "oracle_root", "native_root", "objdiff", "output", "prime_archive", "echoes_archive", "prime_project", "echoes_project"):
        setattr(args, key, getattr(args, key).resolve())
    args.output.mkdir(parents=True, exist_ok=True)
    pin_archives = {}
    for item in args.pin_archive:
        pin, filename = item.split("=", 1)
        if pin in pin_archives or pin not in {c[4] for c in CASES.values()}:
            parser.error(f"duplicate or unexpected --pin-archive commit: {pin}")
        pin_archives[pin] = Path(filename).read_bytes()
    # Validate the Git export's embedded commit before copying project inputs.
    for pin in {c[4] for c in CASES.values()}:
        if pin not in pin_archives:
            pin_archives[pin] = subprocess.check_output(["git", "archive", pin], cwd=ROOT)
        with tarfile.open(fileobj=io.BytesIO(pin_archives[pin])) as source:
            if source.pax_headers.get("comment") != pin:
                parser.error(f"source archive is not a Git export of {pin}")
    stage = Path(tempfile.mkdtemp(prefix="archived-kits-", dir=args.output))
    shutil.copy2(__file__, stage / "verifier.py")
    evidence = dict(format_version=1, binary_sha256=sha(args.binary), objdiff_sha256=sha(args.objdiff),
                    verifier_sha256=sha(stage / "verifier.py"), reference_root=str(args.oracle_root),
                    native_root=str(args.native_root), cases=[], logs=[])
    manifest = stage / "verification.json"
    environment = dict(os.environ, MWCC_TOOLS=str(args.binary), OBJDIFF_CLI=str(args.objdiff), PYTHONDONTWRITEBYTECODE="1")
    if os.name != "nt":
        if not args.wibo or not args.wibo.is_file():
            parser.error("Linux validation requires an explicit --wibo")
        environment["MWCC_WIBO"] = str(args.wibo.resolve())
        evidence["wibo_sha256"] = sha(args.wibo)
    def save():
        write_json(manifest, evidence)
    def run(name, command, cwd, oracle):
        result = subprocess.run([str(a) for a in command], cwd=cwd,
                                env=dict(environment, MWCC_ORACLE_ROOT=str(oracle)),
                                capture_output=True, timeout=900)
        stdout, stderr = stage / f"{name}.stdout", stage / f"{name}.stderr"
        if stdout.exists() or stderr.exists():
            raise RuntimeError(f"refusing to overwrite {name} logs")
        stdout.write_bytes(result.stdout)
        stderr.write_bytes(result.stderr)
        evidence["logs"].append(dict(name=name, command=[str(a) for a in command], cwd=str(cwd), oracle=str(oracle),
                                     exit_code=result.returncode, stdout_sha256=sha(stdout), stderr_sha256=sha(stderr)))
        save()
        if result.returncode:
            raise RuntimeError(f"{name}: exit {result.returncode}; see {stderr}\n{result.stderr.decode(errors='replace')[-1500:]}")
        return result.stdout.decode("utf-8")
    def equal(name, left, right):
        found = difference(left, right)
        if found:
            raise RuntimeError(f"{name}: {found}")
    print(f"retained archived-kit validation: {stage}", flush=True)
    save()
    for label, (unit_name, function, game_version, compiler, pin) in CASES.items():
        kit, archive = unpack(getattr(args, label + "_archive"), stage, label)
        original = getattr(args, label + "_project")
        unit, inputs = snapshot(original, unit_name)
        status = subprocess.check_output(["git", "status", "--porcelain=v1", "--untracked-files=normal"], cwd=original)
        project = stage / (label + "-project")
        project.mkdir()
        for relative in inputs:
            target = project / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(original / relative, target)
        ninja_adjusted = False
        if os.name != "nt":
            ninja_input = (project / "build.ninja").read_bytes()
            (stage / (label + "-original-build.ninja")).write_bytes(ninja_input)
            adjusted = host_ninja(ninja_input)
            (project / "build.ninja").write_bytes(adjusted)
            ninja_adjusted = adjusted != ninja_input
        # Reports are inputs to calibration, never regenerated here.
        report = Path("build") / game_version / "report.json"
        if (original / report).is_file():
            shutil.copy2(original / report, project / report)
            inputs[report.as_posix()] = sha(original / report)
        shutil.copytree(kit / "overlay/scripts", project / "scripts")
        shutil.copytree(ROOT / "integrations/decomp-kits/overlay/scripts", project / "native-launchers",
                        ignore=shutil.ignore_patterns("__pycache__"))
        candidate = project / "candidate.cpp"
        candidate.write_bytes((project / unit["metadata"]["source_path"]).read_bytes() +
                              b"\nint mwcc_archive_candidate_probe() { return 173; }\n")
        entry = dict(kit=label, archive=archive, unit=unit_name, function=function, compiler=compiler,
                     pin_source_archive_sha256=hashlib.sha256(pin_archives[pin]).hexdigest(),
                     game_revision=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=original, text=True).strip(),
                     game_status_sha256=hashlib.sha256(status).hexdigest(), input_hashes=inputs,
                     staged_input_hashes={**inputs, "build.ninja": sha(project / "build.ninja")},
                     ninja_paths_adjusted_for_host=ninja_adjusted,
                     candidate_sha256=sha(candidate), checks=[])
        evidence["cases"].append(entry)
        save()
        # Build both original pins in private trees, including their own loader.
        # Old Python launchers do not understand MWCC_WIBO, so sharing a frozen
        # root's loader would silently test different loader versions on Linux.
        oracle = stage / (label + "-pinned-oracle")
        oracle.mkdir()
        data = pin_archives[pin]
        (stage / (label + "-pin.tar")).write_bytes(data)
        with tarfile.open(fileobj=io.BytesIO(data)) as source:
            source.extractall(oracle, filter="data")
        for directory in ("lib", "build/compilers", "tools/lmgr_stub"):
            shutil.copytree(args.oracle_root / directory, oracle / directory, dirs_exist_ok=True)
        if os.name != "nt":
            (oracle / "build/tools").mkdir(parents=True)
            shutil.copy2(args.wibo, oracle / "build/tools/wibo")
            (oracle / "build/tools/wibo").chmod(0o755)
        run(label + "-configure-pin", [args.binary, "configure", "--root", oracle, "--version", compiler,
                                      "--mode", "link", "--oracle", "--producer", "c"], oracle, oracle)
        run(label + "-build-pin", ["ninja", "-j", "8", f"build/{compiler}/oracle/mwcceppc.exe"], oracle, oracle)
        entry["pinned_producer_sha256"] = sha(oracle / f"build/{compiler}/oracle/mwcceppc.exe")
        entry["rust_producer_sha256"] = sha(args.native_root / f"build/{compiler}/oracle/mwcceppc.exe")
        # Verify original archived dispatch and native dispatch on that kit's pin.
        for variant, options in (("source", []), ("candidate", ["--candidate", candidate])):
            prefix = label + "-" + variant
            objects = []
            for mode, folder in (("python", "scripts"), ("rust", "native-launchers")):
                obj = project / f"{prefix}-{mode}.o"
                extra = ["--json", "--locals", "--schedule", "--blocks", "--keep-object", obj]
                # Legacy run-unit always disabled equality; native checks by default.
                if mode == "rust":
                    extra.append("--no-check")
                text = run(prefix + "-run-" + mode, [sys.executable, project / folder / "oracle-run.py",
                           "-u", unit_name, *options, "--", *extra], project, oracle)
                doc = json.loads(text)
                if doc.get("identical") is not None or not doc.get("functions"):
                    raise RuntimeError(f"{prefix}: empty or incorrectly checked legacy report")
                if mode == "python":
                    reference = doc
                else:
                    equal(prefix + " JSON", normalize(doc), normalize(reference))
                objects.append(sha(obj))
            equal(prefix + " object", objects[0], objects[1])
            if variant == "source":
                entry["project_object_was_current"] = objects[0] == inputs[unit["base_path"]]
            entry["checks"].append(dict(name=variant + "-run", object_sha256=objects[0], functions=len(reference["functions"])))
            if variant == "candidate" and objects[0] == entry["checks"][0]["object_sha256"]:
                raise RuntimeError(f"{label}: candidate did not change the object")
            text = []
            for mode, folder in (("python", "scripts"), ("rust", "native-launchers")):
                report_text = run(prefix + "-regdiff-" + mode, [sys.executable, project / folder / "oracle-regdiff.py",
                                  "-u", unit_name, "-f", function, *options, "--full", "--pcode", "--inlining"], project, oracle)
                # Only the platform's representation of the executable path may differ.
                lines = report_text.splitlines()
                lines[0] = lines[0].replace("\\", "/")
                text.append(lines)
            equal(prefix + " regdiff", text[0], text[1])
            if variant == "source":
                expected = "yes" if entry["project_object_was_current"] else "NO"
                if not any(line.startswith("oracle object equals the project object: " + expected) for line in text[0]):
                    raise RuntimeError(f"{label}: regdiff did not report the project's object state")
            entry["checks"].append(dict(name=variant + "-regdiff", lines=len(text[0])))
            save()
        # Verify new default equality and complete Rust hooks against the b75 C baseline.
        for variant, options in (("source", []), ("candidate", ["--candidate", candidate])):
            docs = []
            for mode, root in (("c", args.oracle_root), ("rust", args.native_root)):
                text = run(f"{label}-checked-{variant}-{mode}", [sys.executable, project / "native-launchers/oracle-run.py",
                           "-u", unit_name, *options, "--", "--json", "--locals", "--schedule", "--blocks"], project, root)
                doc = json.loads(text)
                if doc.get("identical") is not True:
                    raise RuntimeError(f"{label}: new default failed object equality")
                docs.append(doc)
            equal(label + " checked Rust producer " + variant, normalize(docs[0]), normalize(docs[1]))
            entry["checks"].append(dict(name="checked-rust-producer-" + variant, functions=len(docs[0]["functions"])))
        # Pick a real costed callee without accidentally including an always-inline
        # overload (which the legacy calibration helper cannot handle).
        decisions = [d for f in docs[0]["functions"] for d in f["inlining"] if d.get("event") == "decision"]
        callees = sorted({d["callee"].rsplit("::", 1)[-1] for d in decisions
                          if "cost" in d and not d["callee"].rsplit("::", 1)[-1].startswith("__")}, key=lambda c: (len(c), c))
        callee = next((c for c in callees if all("cost" in d for d in decisions if c in d["callee"])), None)
        if not callee:
            raise RuntimeError(f"{label}: no unambiguous costed calibration callee")
        for binary in (False, True):
            tables, reports = [], []
            for mode, folder in (("python", "scripts"), ("rust", "native-launchers")):
                output = project / f"calibration-{binary}-{mode}.json"
                command = [sys.executable, project / folder / "inline-calibrate.py", "--callee", callee,
                           "-u", unit_name, "--compiler", compiler, "--version", game_version, "--near", "1000000",
                           "--breakdown", "--json", output]
                if binary:
                    command.append("--binary")
                reports.append(run(f"{label}-calibration-{binary}-{mode}", command, project, oracle).splitlines())
                tables.append(json.loads(output.read_text()))
            if not tables[0]:
                raise RuntimeError(f"{label}: calibration exercised no decisions")
            equal(f"{label} calibration {binary} JSON", tables[0], tables[1])
            equal(f"{label} calibration {binary} report", reports[0], reports[1])
            entry["checks"].append(dict(name=f"calibration-binary-{binary}", callee=callee, rows=len(tables[0]), lines=len(reports[0])))
            save()
        # Old saved traces remain readable with explicit version inference from the unit.
        if (project / "scripts/oracle-colouring.py").is_file():
            saved = project / "saved"
            reports = []
            for mode, folder in (("python", "scripts"), ("rust", "native-launchers")):
                command = [sys.executable, project / folder / "oracle-colouring.py", "-u", unit_name,
                           "verify", "--function", function, "--json"]
                command += ["--save", saved] if mode == "python" else ["--load", saved]
                reports.append(json.loads(run(label + "-colouring-" + mode, command, project, oracle)))
            equal(label + " saved colouring", reports[0], reports[1])
            entry["checks"].append(dict(name="legacy-saved-colouring", trace_sha256=sha(Path(str(saved) + ".trace")),
                                        object_sha256=sha(Path(str(saved) + ".o"))))
        # Confirm the input copies and the original game tree were not changed.
        _, after = snapshot(original, unit_name)
        if report.as_posix() in inputs:
            after[report.as_posix()] = sha(original / report)
        equal(label + " original game inputs", inputs, after)
        equal(label + " original game status", status,
              subprocess.check_output(["git", "status", "--porcelain=v1", "--untracked-files=normal"], cwd=original))
        for relative, digest in entry["staged_input_hashes"].items():
            if sha(project / relative) != digest:
                raise RuntimeError(f"{label}: staged input changed: {relative}")
        entry["inputs_unchanged"] = True
        entry["complete"] = True
        save()
        print(f"{label}: {len(entry['checks'])} archived/native compatibility checks pass", flush=True)
    evidence["complete"] = True
    save()
    print(f"archived-kit compatibility verified: {manifest}")


if __name__ == "__main__":
    main()
