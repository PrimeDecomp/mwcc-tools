#!/usr/bin/env python3
"""Legacy kit launcher for native oracle run-unit. Checks by default; --fast skips checking."""
from _mwcc_native import dispatch

if __name__ == "__main__":
    raise SystemExit(dispatch("oracle", "run-unit"))
