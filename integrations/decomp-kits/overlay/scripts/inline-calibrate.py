#!/usr/bin/env python3
"""Legacy kit launcher for native inline calibration and per-unit compiler inference."""
from _mwcc_native import dispatch

if __name__ == "__main__":
    raise SystemExit(dispatch("inline-calibrate"))
