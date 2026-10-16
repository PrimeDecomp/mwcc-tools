"""Exercise native kit launchers on an isolated project, including candidates and saved traces."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--objdiff", type=Path, required=True)
    parser.add_argument("--calibrate-reference", type=Path, required=True)
    parser.add_argument("--keep", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="native-kit-live-") as temporary:
        project = Path(temporary)
        (project / "src").mkdir()
        (project / "build/TEST/obj").mkdir(parents=True)
        shutil.copytree(ROOT / "integrations/decomp-kits/overlay/scripts", project / "scripts", ignore=shutil.ignore_patterns("__pycache__"))
        source = project / "src/unit.cpp"
        source.write_text("extern int external(int);\ninline int factor(int x) { return x*3+1; }\nint across_calls(int x) { return external(x)+factor(x); }\n")
        candidate = project / "candidate.cpp"
        candidate.write_text("extern int external(int);\nint across_calls(int x) { return external(x)+x*7; }\n")
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        (project / "objdiff.json").write_text(json.dumps({"units": [{"name": "main/unit", "base_path": "build/TEST/base.o",
                                        "target_path": "build/TEST/obj/unit.o", "metadata": {"source_path": "src/unit.cpp"}}]}))
        (project / "build.ninja").write_text("mw_version = GC/1.3.2\nrule mwcc\n  command = unused\nbuild build/TEST/base.o: mwcc src/unit.cpp\n  cflags = -nodefaults -proc gekko -O4,p -lang=c++\n")
        environment = dict(os.environ, MWCC_TOOLS=str(args.binary.resolve()), MWCC_ORACLE_ROOT=str(args.oracle_root.resolve()), OBJDIFF_CLI=str(args.objdiff.resolve()))
        def run(script, *options, success=True, env=environment):
            result = subprocess.run([sys.executable, str(project / "scripts" / script), *options], env=env, cwd=project, capture_output=True, text=True, timeout=120)
            if bool(result.returncode == 0) != success:
                raise RuntimeError(f"{script}: exited {result.returncode}: {result.stderr}")
            return result
        base = project / "build/TEST/base.o"
        checked = json.loads(run("oracle-run.py", "-u", "main/unit", "--", "--json", "--keep-object", str(base)).stdout)
        if checked["identical"] is not True:
            raise RuntimeError("run-unit did not check compiler equality by default")
        shutil.copyfile(base, project / "build/TEST/obj/unit.o")
        fast = json.loads(run("oracle-run.py", "-u", "main/unit", "--fast", "--", "--json").stdout)
        if fast["identical"] is not None:
            raise RuntimeError("--fast did not explicitly skip checking")
        candidate_object = project / "candidate.o"
        candidate_report = run("oracle-run.py", "-u", "main/unit", "--candidate", str(candidate), "--", "--json", "--keep-object", str(candidate_object))
        candidate_document = json.loads(candidate_report.stdout)
        if candidate_document["source"] != str(candidate.resolve()):
            raise RuntimeError("candidate report changed the source path spelling")
        if candidate_document["identical"] is not True or candidate_object.read_bytes() == base.read_bytes():
            raise RuntimeError("candidate did not compile to different, verified code")
        regdiff = json.loads(run("oracle-regdiff.py", "-u", "main/unit", "-f", "across_calls", "--json").stdout)
        if regdiff["format_version"] != 1 or regdiff["compilation"]["project_object_identical"] is not True:
            raise RuntimeError("regdiff failed to verify the project object")
        regdiff_candidate = run("oracle-regdiff.py", "-u", "main/unit", "-f", "across_calls", "--candidate", str(candidate), "--full")
        if "candidate:" not in regdiff_candidate.stdout:
            raise RuntimeError("regdiff omitted candidate identity")
        save = project / "saved"
        colouring = json.loads(run("oracle-colouring.py", "-u", "main/unit", "verify", "--function", "across_calls", "--save", str(save), "--json").stdout)
        loaded = json.loads(run("oracle-colouring.py", "-u", "main/unit", "verify", "--function", "across_calls", "--load", str(save), "--json").stdout)
        if colouring != loaded:
            raise RuntimeError("saved colouring changed on reload")
        if not Path(f"{save}.manifest.json").is_file():
            raise RuntimeError("save omitted provenance")
        (project / "build/TEST/report.json").write_text(json.dumps({"units": [{"name": "main/unit", "metadata": {"source_path": "src/unit.cpp"}, "functions": []}]}))
        (project / "config/TEST").mkdir(parents=True)
        (project / "config/TEST/symbols.txt").write_text("factor__Fi = .text:0x80000000; // type:function size:0x10\n")
        bootstrap = '''import importlib.util, pathlib, sys
script, project, *arguments = sys.argv[1:]
spec = importlib.util.spec_from_file_location("calibrate_reference", script)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
module.ROOT = pathlib.Path(project)
sys.argv = [script, *arguments]
module.main()
'''
        for binary in (False, True):
            native_json, python_json = project / "native-calibrate.json", project / "python-calibrate.json"
            options = ["--callee", "factor", "-u", "main/unit", "--compiler", "GC_1_3_2", "--version", "TEST", "--near", "1000", "--breakdown"]
            if binary:
                options += ["--binary"]
            native = run("inline-calibrate.py", *options, "--json", str(native_json))
            python = subprocess.run([sys.executable, "-c", bootstrap, str(args.calibrate_reference.resolve()), str(project), *options, "--json", str(python_json)], env=environment, capture_output=True, text=True, timeout=120)
            if python.returncode or native.stdout.splitlines() != python.stdout.splitlines() or json.loads(native_json.read_text()) != json.loads(python_json.read_text()):
                raise RuntimeError(f"inline calibration {binary}: Python {python.stderr}\n{python.stdout}\nRust {native.stdout}")
            result = json.loads(native_json.read_text())
            if not result:
                raise RuntimeError("calibration gate did not exercise any cost decisions")
        failed = run("oracle-run.py", "-u", "main/unit", "--", "--json", success=False,
                     env=dict(environment, MWCC_TOOLS=str(project / "missing-native-tool")))
        if "MWCC_TOOLS executable not found" not in failed.stderr:
            raise RuntimeError("explicit missing native executable was not reported")
        if hashlib.sha256(source.read_bytes()).hexdigest() != digest:
            raise RuntimeError("unit source changed")
        if args.keep:
            args.keep.mkdir(parents=True, exist_ok=True)
            (args.keep / "regdiff.json").write_text(json.dumps(regdiff, indent=2))
            (args.keep / "candidate-regdiff.txt").write_text(regdiff_candidate.stdout)
            (args.keep / "colouring.json").write_text(json.dumps(colouring, indent=2))
    print("kit dispatch: checked/fast runs, candidates, regdiff, colouring save/load, calibration modes and explicit failures pass")


if __name__ == "__main__":
    main()
