"""Shared native-tool discovery for legacy kit command names; no oracle imports."""
import os
from pathlib import Path
import shutil
import subprocess
import sys


def oracle_root(arguments):
    for i, argument in enumerate(arguments):
        if argument == "--":
            break
        if argument == "--oracle-root" and i+1 < len(arguments):
            return Path(arguments[i+1]).expanduser()
        if argument.startswith("--oracle-root="):
            return Path(argument.split("=", 1)[1]).expanduser()
    value = os.environ.get("MWCC_ORACLE_ROOT")
    return Path(value).expanduser() if value else None


def executable(arguments):
    explicit = os.environ.get("MWCC_TOOLS")
    if explicit:
        path = Path(explicit).expanduser()
        found = str(path) if path.is_file() else shutil.which(str(path))
        if not found:
            raise RuntimeError(f"MWCC_TOOLS executable not found: {explicit}")
        return found
    root = oracle_root(arguments)
    suffix = ".exe" if os.name == "nt" else ""
    if root:
        for relative in ("bin", "target/release", "build/bin"):
            path = root / relative / f"mwcc-tools{suffix}"
            if path.is_file():
                return str(path.resolve())
    if sys.platform in ("win32", "linux"):
        target = "x86_64-pc-windows-msvc" if os.name == "nt" else "x86_64-unknown-linux-gnu"
        project = Path(__file__).resolve().parents[1]
        path = project / ".agents/tools/mwcc" / target / "bin" / f"mwcc-tools{suffix}"
        if path.is_file():
            return str(path.resolve())
    found = shutil.which("mwcc-tools")
    if found:
        return found
    raise RuntimeError("mwcc-tools was not found; set MWCC_TOOLS to a native release executable, or run cargo build --release --locked in MWCC_ORACLE_ROOT")


def dispatch(*command):
    arguments = sys.argv[1:]
    try:
        native = executable(arguments)
        project = Path(__file__).resolve().parents[1]
        # The native command owns unit selection, Ninja parsing, compilation and reports.
        return subprocess.run([native, *command, "--project", str(project), *arguments], cwd=project).returncode
    except (OSError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        return 1
