#!/usr/bin/env python3
"""Compile Echoes' configured Wii units with two compilers and compare the objects."""

import argparse
import ctypes
import hashlib
import json
import os
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
REGIONS = ("R32J01", "R3ME01", "R3MP01")
RULES = ("mwcc", "mwcc_sjis", "mwcc_extab", "mwcc_sjis_extab")


def split_windows_command(command):
    split = ctypes.windll.shell32.CommandLineToArgvW
    split.argtypes = (ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int))
    split.restype = ctypes.POINTER(ctypes.c_wchar_p)
    free = ctypes.windll.kernel32.LocalFree
    free.argtypes = (ctypes.c_void_p,)
    free.restype = ctypes.c_void_p
    count = ctypes.c_int()
    argv = split(command, ctypes.byref(count))
    if not argv:
        raise ctypes.WinError()
    try:
        return argv[:count.value]
    finally:
        free(argv)


@dataclass
class Job:
    output: Path
    directory: Path
    compile: list[str]
    compiler_index: int
    output_index: int
    extab: list[str]
    is_cpp: bool

    def compile_with(self, compiler, output, force_file_ipa):
        output.parent.mkdir(parents=True, exist_ok=True)
        argv = self.compile.copy()
        argv[self.compiler_index] = str(compiler)
        argv[self.output_index] = str(output)
        if force_file_ipa and self.is_cpp and "-ipa" not in argv:
            argv.extend(("-ipa", "file"))
        self.run(argv)
        if self.extab:
            self.run([*self.extab, str(output), str(output)])

    def run(self, argv):
        result = subprocess.run(argv, cwd=self.directory, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode:
            log = result.stdout.decode(errors="replace").rstrip()
            raise ValueError(f"{self.output}: {argv[0]} exited with {result.returncode}\n{log}")


def configured_units(args, graph):
    data = subprocess.check_output([args.ninja, "-f", str(graph), "-t", "compdb", *RULES], cwd=args.echoes)
    compiler_suffix = "/" + Path(args.compiler_version).as_posix().strip("/").lower() + "/mwcceppc.exe"
    jobs = {}
    for entry in json.loads(data):
        object_file = Path(entry["output"])
        components = object_file.parts
        if args.region not in components or object_file.suffix != ".o":
            continue
        tail = components[components.index(args.region) + 1:]
        if not tail or tail[0] != "src":
            continue
        argv = split_windows_command(entry["command"])
        if argv[:2] in (["cmd", "/c"], ["cmd.exe", "/c"]):
            argv = argv[2:]
        compilers = [i for i, arg in enumerate(argv) if ("/" + Path(arg).as_posix().lower()).endswith(compiler_suffix)]
        if not compilers:
            continue
        compiler_index, = compilers
        directory = Path(entry["directory"])
        if compiler_index == 1 and Path(argv[0]).name.lower() == "sjiswrap.exe":
            argv[0] = str(directory / argv[0])
        elif compiler_index != 0:
            raise ValueError(f"{object_file}: unsupported compiler wrapper")

        extab = []
        if "&&" in argv:
            chain = argv.index("&&")
            clean = argv[chain + 1:]
            argv = argv[:chain]
            if (len(clean) != 7 or Path(clean[0]).name.lower() != "dtk.exe"
                    or clean[1:4] != ["extab", "clean", "--padding"]
                    or any(Path(path) != object_file for path in clean[-2:])):
                raise ValueError(f"{object_file}: unsupported post-processing command")
            extab = [str(directory / clean[0]), *clean[1:-2]]

        argv = [arg for arg in argv if arg != "-MMD"]
        output_index = argv.index("-o") + 1
        output = Path(*tail)
        if output in jobs or ".." in output.parts:
            raise ValueError(f"{object_file}: duplicate or invalid output path")
        is_cpp = Path(entry["file"]).suffix.lower() in (".cp", ".cc", ".cpp", ".cxx")
        jobs[output] = Job(output, directory, argv, compiler_index, output_index, extab, is_cpp)
    if not jobs:
        raise ValueError(f"no {args.region} units using {args.compiler_version} in {graph}")
    return [jobs[output] for output in sorted(jobs)]


def compare(args):
    args.echoes = args.echoes.resolve()
    graph = args.graph.resolve() if args.graph else args.echoes / "build.ninja"
    jobs = configured_units(args, graph)
    compilers = {label: path.resolve(strict=True) for label, path in (
        ("baseline", args.baseline), ("candidate", args.candidate),
    )}
    args.out_dir.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=args.region + "-", dir=args.out_dir.resolve()))
    print(f"Comparing {len(jobs)} {args.region} objects per mode in {work}", flush=True)
    modes = ("configured", "file-ipa") if args.mode == "both" else (args.mode,)
    report = {
        "region": args.region,
        "graph": str(graph),
        "graph_sha1": hashlib.sha1(graph.read_bytes()).hexdigest(),
        "compilers": {label: {"path": str(path), "sha1": hashlib.sha1(path.read_bytes()).hexdigest()}
                      for label, path in compilers.items()},
        "modes": {},
    }
    for mode in modes:
        def check(job):
            objects = []
            for label, compiler in compilers.items():
                output = work / mode / label / job.output
                job.compile_with(compiler, output, force_file_ipa=mode == "file-ipa")
                objects.append(output.read_bytes())
            return None if objects[0] == objects[1] else job.output.as_posix()

        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            changed = [result for result in pool.map(check, jobs) if result is not None]
        report["modes"][mode] = {"compared": len(jobs), "changed": changed}
        print(f"{mode}: {len(jobs)} compared, {len(changed)} changed", flush=True)
    (work / "comparison.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if args.expect_identical and any(result["changed"] for result in report["modes"].values()):
        raise ValueError(f"comparison found changed objects; see {work / 'comparison.json'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--echoes", type=Path, required=True, help="configured Echoes checkout")
    parser.add_argument("--graph", type=Path, help="Ninja graph to use; defaults to Echoes' build.ninja")
    parser.add_argument("--region", choices=REGIONS, required=True)
    parser.add_argument("--baseline", type=Path, required=True, help="baseline mwcceppc.exe")
    parser.add_argument("--candidate", type=Path, required=True, help="candidate mwcceppc.exe")
    parser.add_argument("--compiler-version", default="Wii/1.0RC1_retro", help="compiler path in the Ninja graph")
    parser.add_argument("--mode", choices=("configured", "file-ipa", "both"), default="both")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "build/wii-retro-tests/echoes")
    parser.add_argument("--ninja", default="ninja")
    parser.add_argument("--jobs", type=int, default=min(12, os.cpu_count() or 1))
    parser.add_argument("--expect-identical", action="store_true", help="fail if any object changes")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("this check uses the Windows Echoes Ninja graph and compiler wrappers")
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    try:
        compare(args)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Echoes object comparison: {error}\n")


if __name__ == "__main__":
    main()
