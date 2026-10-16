"""Shared native-tool discovery for legacy kit command names; no oracle imports."""
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys


def native_target():
    system, machine = platform.system(), platform.machine().lower()
    architecture = {"amd64": "x86_64", "x86_64": "x86_64",
                    "arm64": "aarch64", "aarch64": "aarch64"}.get(machine)
    target = {
        ("Windows", "x86_64"): "x86_64-pc-windows-msvc",
        ("Linux", "x86_64"): "x86_64-unknown-linux-gnu",
        ("Darwin", "x86_64"): "x86_64-apple-darwin",
        ("Darwin", "aarch64"): "aarch64-apple-darwin",
    }.get((system, architecture))
    if target is None:
        raise RuntimeError(f"no bundled tools target for {system}/{machine}")
    return target


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
    unsupported = None
    try:
        target = native_target()
    except RuntimeError as error:
        unsupported = str(error)
    else:
        installed_suffix = ".exe" if target == "x86_64-pc-windows-msvc" else ""
        project = Path(__file__).resolve().parents[1]
        path = project / ".agents/tools/mwcc" / target / "bin" / f"mwcc-tools{installed_suffix}"
        if path.is_file():
            return str(path.resolve())
    found = shutil.which("mwcc-tools")
    if found:
        return found
    if unsupported:
        raise RuntimeError(f"{unsupported}; set MWCC_TOOLS to a build for this host, or put mwcc-tools on PATH")
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
