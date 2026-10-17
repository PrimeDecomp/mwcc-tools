"""clang on the sources: the one clang command line for a source (compile_commands.json, diagnostics, syntax trees).

The sources are compiled by MWCC; clang reads them for navigation, diagnostics and the syntax trees the editing tools
work on (csrc). A source's clang command is derived from its MWCC compilation (build.compilation), so both see the
same include directories and VERSION: a 32-bit Windows clang (i386-pc-windows-msvc, the MS extensions) without MSVC's
identity (the SDK and MSL headers test _MSC_VER before __MWERKS__), with __MWERKS__ defined as the compiler's own
headers expect. Nothing is silenced: what clang says of a source is what a modern compiler will.

The project's layout is .clang-format's, applied by format_source() to every source a transaction writes.
"""

from __future__ import annotations

import fnmatch
import functools
import json
import subprocess
import sys
from pathlib import Path

from . import build
from .project import ROOT, Error, Version, write_atomic

CASE = ROOT / "build" / "clangd" / "case"
COMMANDS = ROOT / "compile_commands.json"
FORMATTED = (".c", ".cpp", ".h")


def _case_shim(directory: Path) -> Path | None:
    """The directory of lowercase links to DIRECTORY's files that are not lowercase: lib/'s Win32 SDK headers are
    WINNT.H and the like, included as winnt.h, which only a case-insensitive file system (MWCC's) finds."""
    shim = CASE / directory.relative_to(ROOT)
    for path in directory.iterdir():
        if path.is_file() and path.name != path.name.lower():
            link = shim / path.name.lower()
            if not link.is_symlink():
                link.parent.mkdir(parents=True, exist_ok=True)
                link.symlink_to(path)
    return shim if shim.is_dir() else None


def arguments(compilation: build.Compilation) -> list[str]:
    """clang's arguments for COMPILATION's source (the program first; without the source and -c)."""
    flags = compilation.args
    cpp = compilation.source.endswith((".cpp", ".cp"))
    # (MWCC's __option(NAME) is whether an option is on, as the headers' #if tests it: x86's byte order and long long,
    # and in C++ the built-in bool and wchar_t, which clang has whatever -bool says; every other option off)
    options = ["little_endian", "longlong"] + (["cplusplus", "bool", "wchar_type"] if cpp else [])
    if "-Cpp_exceptions" in flags and flags[flags.index("-Cpp_exceptions") + 1] == "on":
        options.append("exceptions")
    args = [
        "clang",
        "--target=i386-pc-windows-msvc",
        "-fms-extensions",
        "-fno-ms-compatibility",
        "-U_MSC_VER",
        "-U_MSC_FULL_VER",
        "-U_MSC_BUILD",
        "-D__MWERKS__=0x2407",
        "-D__INTEL__",
        "-D__option(x)=__option_##x",
        *(f"-D__option_{name}=1" for name in options),
        *(["-xc++", "-std=gnu++98"] if cpp else ["-xc"]),
    ]
    # (MWCC's -align N caps structure member alignment at N, 4 by default (a double is 4-aligned): clang's
    # -fpack-struct=N, which #pragma pack(pop) returns to as MWCC's does)
    align = flags[flags.index("-align") + 1] if "-align" in flags else "4"
    args.append("-fpack-struct=" + {"byte": "1", "packed": "1"}.get(align, align))
    system = False
    for flag in flags:
        if flag == "-i-":
            # (MWCC: the include directories after -i- are system ones. The MSL and runtime wrappers include lib/'s
            # file of their own name with <...>, which searches only these)
            system = True
        elif flag.startswith("-I"):
            directory = ROOT / flag[2:]
            args.append(("-isystem" if system else "-I") + str(directory))
            shim = _case_shim(directory) if directory.is_dir() and flag[2:].startswith("lib/") else None
            if shim:
                args.append(("-isystem" if system else "-I") + str(shim))
        elif flag.startswith("-D"):
            args.append(flag)
    return args


def write_compile_commands(version: Version | str | None = None) -> int:
    """Write compile_commands.json (for clangd) with VERSION's command for each source; the number of sources."""
    entries = [
        {"directory": str(ROOT), "file": str(ROOT / c.source), "arguments": [*arguments(c), "-c", c.source]}
        for c in build.compilations(version)
    ]
    text = json.dumps(entries, indent=1) + "\n"
    if not COMMANDS.exists() or COMMANDS.read_text() != text:
        write_atomic(COMMANDS, text)
    return len(entries)


def run(
    compilation: build.Compilation, extra: list[str], text: str | bytes | None = None, timeout: float = 600
) -> subprocess.CompletedProcess:
    """clang on the source with EXTRA arguments; on TEXT instead of the file when given (read as the source, its
    directory searched for its own includes)."""
    args = arguments(compilation)
    if text is None:
        command = [*args, *extra, compilation.source]
        data = None
    else:
        command = [*args, f"-I{ROOT / Path(compilation.source).parent}", *extra, "-"]
        data = text.encode("latin-1") if isinstance(text, str) else text
    try:
        return subprocess.run(command, cwd=ROOT, input=data, capture_output=True, timeout=timeout)
    except FileNotFoundError:
        raise Error("clang is not installed (the editing tools read the sources with it)") from None


def errors(compilation: build.Compilation, text: str | bytes | None = None) -> list[str]:
    """clang's errors for the source (or TEXT as the source), each "file:line:col: error: ..." with the root's prefix
    removed. Implicit declarations are errors: MWCC takes them too, and they change the code around them."""
    result = run(
        compilation,
        ["-fsyntax-only", "-ferror-limit=0", "-Werror=implicit-function-declaration", "-Werror=implicit-int"],
        text,
    )
    prefix = str(ROOT) + "/"
    return [line.removeprefix(prefix) for line in result.stderr.decode("latin-1").splitlines() if ": error:" in line]


def syntax_tree(compilation: build.Compilation, text: str | bytes | None = None) -> dict:
    """clang's syntax tree of the source (or TEXT as the source): -ast-dump=json's translation unit."""
    result = run(compilation, ["-fsyntax-only", "-ferror-limit=0", "-Xclang", "-ast-dump=json"], text)
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError:
        raise Error(
            f"clang gave no syntax tree for {compilation.source}:\n{result.stderr.decode('latin-1')[-2000:]}"
        ) from None


@functools.cache
def _format_ignored() -> tuple[str, ...]:
    """.clang-format-ignore's patterns (clang-format reads it only for files named on its command line, not stdin)."""
    try:
        lines = (ROOT / ".clang-format-ignore").read_text().splitlines()
    except FileNotFoundError:
        return ()
    return tuple(line.strip().replace("**", "*") for line in lines if line.strip() and not line.startswith("#"))


def formatted(path: Path) -> bool:
    """Whether .clang-format lays out PATH: the project's own C and C++, not lib/ and the library wrappers."""
    try:
        name = path.relative_to(ROOT).as_posix()
    except ValueError:
        return False
    return path.suffix in FORMATTED and not any(fnmatch.fnmatch(name, p) for p in _format_ignored())


def format_source(path: Path, data: bytes) -> bytes:
    """DATA, PATH's new contents, laid out by .clang-format (pyproject.toml's pinned clang-format)."""
    command = [str(Path(sys.executable).parent / "clang-format"), f"--assume-filename={path}"]
    try:
        result = subprocess.run(command, cwd=ROOT, input=data, capture_output=True, timeout=60)
    except FileNotFoundError:
        raise Error("clang-format is not installed (uv sync installs pyproject.toml's pinned version)") from None
    if result.returncode:
        raise Error(f"clang-format failed on {path.relative_to(ROOT)}: {result.stderr.decode(errors='replace')}")
    return result.stdout
