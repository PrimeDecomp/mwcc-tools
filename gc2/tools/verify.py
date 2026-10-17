#!/usr/bin/env python3
"""python tools/verify.py: build and compare every version (`ninja verify`), then check for regressions against the
saved baseline (tools/regressions.py). The configured version and mode are left as they are."""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

if sys.argv[1:2] in (["-h"], ["--help"]):
    print(__doc__)
    sys.exit()
built = subprocess.run(["ninja", "verify"], cwd=ROOT).returncode
checked = subprocess.run([sys.executable, str(ROOT / "tools/regressions.py")], cwd=ROOT).returncode
sys.exit(built or checked)
