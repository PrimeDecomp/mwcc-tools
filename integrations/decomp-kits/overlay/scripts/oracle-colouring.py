#!/usr/bin/env python3
"""Legacy kit launcher for all six native colouring commands and saved traces."""
from _mwcc_native import dispatch

if __name__ == "__main__":
    raise SystemExit(dispatch("colouring", "unit"))
