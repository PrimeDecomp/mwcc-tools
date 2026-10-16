#!/usr/bin/env python3
"""Legacy kit launcher for native register differences; --json exposes structured evidence."""
from _mwcc_native import dispatch

if __name__ == "__main__":
    raise SystemExit(dispatch("oracle", "regdiff"))
