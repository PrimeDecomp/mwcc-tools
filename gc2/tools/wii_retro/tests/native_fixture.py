"""Build and run a Win32 differential fixture against a hash-pinned stock image."""

import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1]))
from mw.pe import PEFile  # noqa: E402
from wii_retro_link import ROOT, read_config  # noqa: E402

RC1_STOCK = read_config("Wii_1_0RC1")
GC_SHA1 = "79683505e7fc6bb63e5c169f21623df272dd2262"


def stock_image(path, sha1):
    image = PEFile(path)
    if hashlib.sha1(image.data).hexdigest() != sha1:
        raise ValueError(f"{path}: expected a stock compiler with SHA-1 {sha1}")
    return image


def byte_array(name, data):
    yield f"static const unsigned char {name}[] = {{"
    for offset in range(0, len(data), 16):
        yield "    " + ", ".join(f"0x{byte:02x}" for byte in data[offset:offset + 16]) + ","
    yield "};"


def fixture_options(parser, name):
    parser.add_argument("--stock", type=Path, default=ROOT / RC1_STOCK["original"])
    parser.add_argument("--out-dir", type=Path, default=ROOT / "build/wii-retro-tests" / name)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--linker", default="lld-link")
    parser.add_argument("--dlltool", default="llvm-dlltool")


def fixture_directory(directory):
    directory.mkdir(parents=True, exist_ok=True)
    return Path(tempfile.mkdtemp(prefix="run-", dir=directory.resolve()))


def run_fixture(args, work, name, *sources):
    objects = []
    for source in sources:
        obj = work / (source.stem + ".obj")
        subprocess.run(
            [args.clang, "--target=i686-pc-windows-msvc", "-O2", "-ffreestanding", "-fno-builtin",
             "-fno-stack-protector", "-Wall", "-Wextra", "-I", str(HERE.parent), "-I", str(work),
             "-c", str(source), "-o", str(obj)],
            check=True,
        )
        objects.append(obj)
    exports = work / "kernel32.def"
    exports.write_text("LIBRARY kernel32.dll\nEXPORTS\nVirtualAlloc@16\nVirtualProtect@16\nExitProcess@4\n",
                       encoding="ascii")
    imports = work / "kernel32.lib"
    subprocess.run([args.dlltool, "-m", "i386", "-k", "-d", str(exports), "-l", str(imports)], check=True)
    exe = work / (name + ".exe")
    subprocess.run(
        [args.linker, "/nodefaultlib", "/entry:start", "/dynamicbase", "/base:0x19000000", "/safeseh:no",
         "/subsystem:console", f"/out:{exe}", *map(str, objects), str(imports)],
        check=True,
    )
    # A loaded DLL can occupy the address range required for the stock code.
    for _ in range(5):
        result = subprocess.run([str(exe)], check=False)
        if result.returncode != 13:
            break
    if result.returncode:
        raise ValueError(f"{name} failed at checkpoint {result.returncode:#x}; see {work}")
