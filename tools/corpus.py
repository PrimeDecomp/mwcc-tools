#!/usr/bin/env python3
"""python tools/corpus.py [options]: compile a corpus of sources with the original compiler and with the oracle
executable and report every input whose object (the sections that are not debug information), messages or exit code
differ.

  --version V       GC_1_3, GC_1_3_2 or GC_2_7 (default GC_1_3_2); the original is build/compilers/GC/<version>/mwcceppc.exe and
                    the other build/V/oracle/mwcceppc.exe (or --exe)
  --canaries DIR    a directory of small programs (zcanann/mwcc-rs's canaries/): each is compiled with the baseline flags
                    of that project's harness, those a `// flags:` line on top names replacing the baseline's
  --prime DIR       a decomp project's checkout (Metroid Prime): every `mwcc*` rule (the Shift-JIS ones too, the
                    source compiled as it is) of its build.ninja that uses this version's compiler, with its own flags,
                    compiled in that directory
  --jobs N          compiles in parallel (default: the CPU count)
  --limit N         the first N inputs only
  --list FILE       only the inputs named in FILE (one per line, the path as printed)
  --keep DIR        the objects and messages of the inputs that differ, for looking at

The exit status is 0 when nothing differs."""
import argparse
import concurrent.futures
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

BASELINE = [["-nodefaults"], ["-proc", "gekko"], ["-align", "powerpc"], ["-enum", "int"], ["-fp", "hardware"],
            ["-O4,p"], ["-inline", "auto"], ["-maxerrors", "1"], ["-nosyspath"], ["-RTTI", "off"],
            ["-fp_contract", "on"], ["-str", "reuse"]]


def family(option):
    return "-O" if option.startswith("-O") or option == "-opt" else option


def canary_flags(path):
    first = path.read_text(errors="replace").splitlines()[:1]
    extra = []
    if first and first[0].startswith("// flags:"):
        extra = shlex.split(first[0][len("// flags:"):])
    overridden = {family(a) for a in extra if a.startswith("-")}
    flags = [x for group in BASELINE if family(group[0]) not in overridden for x in group]
    if path.suffix == ".cpp":
        flags += ["-lang=c++"]
    return flags + extra


def canary_jobs(directory):
    return [(str(p.name), canary_flags(p), str(p), Path(directory)) for p in sorted(Path(directory).iterdir())
            if p.suffix in (".c", ".cpp")]


def prime_jobs(directory, compiler):
    """The `mwcc` rules of a ninja file that use COMPILER (like GC/1.3.2): [(source, flags, source, directory)]."""
    directory = Path(directory)
    text = (directory / "build.ninja").read_text(errors="replace").replace("\r\n", "\n")
    text = re.sub(r"\$\n\s*", " ", text)
    jobs = []
    for m in re.finditer(r"^build \S+: mwcc\w*\s+(\S+)[^\n]*\n((?:[ \t]+[^\n]*\n)*)", text, re.M):
        source, body = m.groups()
        variables = dict(re.findall(r"^[ \t]+(\w+) = (.*)$", body, re.M))
        if variables.get("mw_version", "").replace("\\", "/") != compiler:
            continue
        flags = shlex.split(variables.get("cflags", "").replace("$$", "$"), posix=True)
        source = source.replace("\\", "/")
        jobs.append((source, flags, source, directory))
    return jobs


SECTION_IGNORED = (".debug", ".line", ".comment")


def elf_sections(data):
    """{name: bytes (or the size of a .bss)} of an ELF32 big-endian object's sections that are not debug information."""
    if data[:4] != b"\x7fELF":
        return {"<file>": data}
    shoff, = struct.unpack_from(">I", data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from(">HHH", data, 0x2E)
    headers = [struct.unpack_from(">IIIIIIIIII", data, shoff + i * shentsize) for i in range(shnum)]
    strings = data[headers[shstrndx][4]:headers[shstrndx][4] + headers[shstrndx][5]]
    out = {}
    for name_offset, kind, _, _, offset, size, *_ in headers:
        name = strings[name_offset:strings.index(b"\0", name_offset)].decode("latin-1")
        if name.startswith(SECTION_IGNORED):
            continue
        out[name] = size if kind == 8 else data[offset:offset + size]  # (NOBITS: only the size is compared)
    return out


def normalise(text, replacements):
    text = text.replace("\r", "")
    for old, new in replacements:
        text = text.replace(old, new)
    return re.sub(r"### \S*mwcceppc\.exe", "### mwcceppc.exe", text)


def compile_one(executable, flags, source, directory, output):
    if output.exists():
        output.unlink()
    command = [str(executable), *flags, "-c", source, "-o", str(output)]
    try:
        run = subprocess.run(command, cwd=directory, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
        text, code = run.stdout.decode("latin-1"), run.returncode
    except subprocess.TimeoutExpired:
        text, code = "timeout", -1
    return text, code, output.read_bytes() if output.exists() else b""


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", default="GC_1_3_2")
    parser.add_argument("--exe")
    parser.add_argument("--canaries")
    parser.add_argument("--prime")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--limit", type=int)
    parser.add_argument("--list")
    parser.add_argument("--keep")
    arguments = parser.parse_args()
    original = ROOT / f"build/compilers/GC/{arguments.version[3:].replace('_', '.')}/mwcceppc.exe"
    oracle = Path(arguments.exe) if arguments.exe else ROOT / f"build/{arguments.version}/oracle/mwcceppc.exe"
    for exe in (original, oracle):
        if not exe.exists():
            sys.exit(f"{exe} does not exist")
    jobs = []
    if arguments.canaries:
        jobs += canary_jobs(arguments.canaries)
    if arguments.prime:
        jobs += prime_jobs(arguments.prime, original.parent.relative_to(ROOT / "build/compilers").as_posix())
    if arguments.list:
        wanted = set(Path(arguments.list).read_text().split())
        jobs = [j for j in jobs if j[0] in wanted]
    if arguments.limit:
        jobs = jobs[:arguments.limit]
    if not jobs:
        sys.exit("no inputs: give --canaries and/or --prime")
    work = Path(tempfile.mkdtemp(prefix="corpus-"))
    keep = Path(arguments.keep) if arguments.keep else None

    def check(item):
        index, (name, flags, source, directory) = item
        real_o, ours_o = work / f"{index}.real.o", work / f"{index}.oracle.o"
        r_text, r_code, r_data = compile_one(original, flags, source, directory, real_o)
        o_text, o_code, o_data = compile_one(oracle, flags, source, directory, ours_o)
        problems = []
        if r_code != o_code:
            problems.append(f"exit {r_code} vs {o_code}")
        if normalise(r_text, []) != normalise(o_text, []):
            problems.append("messages differ")
        a, b = elf_sections(r_data), elf_sections(o_data)
        if a != b:
            names = sorted(n for n in set(a) | set(b) if a.get(n) != b.get(n))
            problems.append("sections differ: " + ", ".join(names))
        if problems and keep:
            keep.mkdir(parents=True, exist_ok=True)
            stem = re.sub(r"[^\w.-]", "_", name)
            (keep / f"{stem}.real.o").write_bytes(r_data)
            (keep / f"{stem}.oracle.o").write_bytes(o_data)
            (keep / f"{stem}.real.txt").write_text(r_text)
            (keep / f"{stem}.oracle.txt").write_text(o_text)
        return name, problems

    different = 0
    total = len(jobs)
    # the oracle's trace would otherwise go to standard error, which is part of the messages compared
    os.environ["MWCC_ORACLE_TRACE_FILE"] = os.devnull
    with concurrent.futures.ThreadPoolExecutor(arguments.jobs) as pool:
        for done, (name, problems) in enumerate(pool.map(check, enumerate(jobs)), 1):
            if problems:
                different += 1
                print(f"DIFF {name}: " + "; ".join(problems))
            if done % 200 == 0:
                print(f"... {done}/{total}", flush=True)
    shutil.rmtree(work, ignore_errors=True)
    print(f"{total - different}/{total} identical" + (f", {different} differ" if different else ""))
    sys.exit(1 if different else 0)


if __name__ == "__main__":
    main()
