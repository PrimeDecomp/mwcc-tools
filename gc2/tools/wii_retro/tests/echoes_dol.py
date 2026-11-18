#!/usr/bin/env python3
"""Compare an Echoes DOL hash after substituting the candidate spatial object."""

import argparse
import hashlib
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
OBJECT = "CSpatialPrimitive.o"


def verify(args):
    echoes = args.echoes.resolve()
    build = echoes / args.build_dir / args.region
    responses = sorted(build.glob("*.elf.rsp"))
    if len(responses) != 1:
        raise ValueError(f"expected one .elf.rsp in {build}; build Echoes with ninja -d keeprsp")
    linker_script = (build / "ldscript.lcf").resolve(strict=True)
    paths = [echoes / line.strip().strip('"') for line in responses[0].read_text(encoding="utf-8").splitlines()
             if line.strip()]
    if sum(path.name == OBJECT for path in paths) != 1:
        raise ValueError(f"expected one {OBJECT} in {responses[0]}")
    replacement = args.spatial.resolve(strict=True)
    paths = [replacement if path.name == OBJECT else path.resolve(strict=True) for path in paths]
    expected, *_ = (echoes / "config" / args.region / "build.sha1").read_text(encoding="ascii").split()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=args.region + "-", dir=args.out_dir.resolve()))
    rsp = work / "candidate.elf.rsp"
    rsp.write_text("".join(f'"{path.as_posix()}"\n' for path in paths), encoding="utf-8")
    elf = work / "candidate.elf"
    dol = work / "candidate.dol"
    linker = args.linker.resolve(strict=True)
    dtk = (args.dtk or echoes / args.build_dir / "tools/dtk.exe").resolve(strict=True)
    subprocess.run([str(linker), "-fp", "hardware", "-nodefaults", "-lcf", str(linker_script),
                    "-o", str(elf), f"@{rsp}"], check=True)
    subprocess.run([str(dtk), "elf2dol", str(elf), str(dol)], check=True)
    actual = hashlib.sha1(dol.read_bytes()).hexdigest()
    if actual != expected:
        raise ValueError(f"{dol}: SHA-1 {actual}; expected {expected}")
    print(f"{args.region} matched DOL SHA-1 {actual} after replacing {OBJECT}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--echoes", type=Path, required=True, help="built Echoes checkout")
    parser.add_argument("--region", choices=("R32J01", "R3ME01", "R3MP01"), required=True)
    parser.add_argument("--spatial", type=Path, required=True, help="candidate CSpatialPrimitive.o with file IPA")
    parser.add_argument("--linker", type=Path, required=True, help="mwldeppc.exe used to link the Echoes region")
    parser.add_argument("--build-dir", type=Path, default=Path("build"), help="Echoes build directory")
    parser.add_argument("--dtk", type=Path, help="dtk.exe if outside the Echoes build directory")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "build/wii-retro-tests/dol")
    try:
        verify(parser.parse_args())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Echoes DOL comparison: {error}\n")


if __name__ == "__main__":
    main()
