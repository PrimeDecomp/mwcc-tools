#!/usr/bin/env python3
"""Name the unnamed `fn_` statics of research/Wii_1_0RC1 from GC 2.7, through GC 3.0a3 ... 3.0a5.2.

Chain: GC 2.7 -> 3.0a3 -> 3.0a3.2 -> 3.0a3.3 -> 3.0a3.4 -> 3.0a5 -> 3.0a5.2 (each by wii_remap), then
RC1 -> 3.0a5.2 (the same mapping research/GC_3_0a5_2 was written from). Together with the direct
2.7 -> RC1 names (wii_remap --names-for) this writes research/Wii_1_0RC1/names_from_2_7.json.
"""

import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import wii_remap as w

STEPS = ["3.0a3", "3.0a3.2", "3.0a3.3", "3.0a3.4", "3.0a5", "3.0a5.2"]
G = "build/compilers/GC/"
RC1 = "research/Wii_1_0RC1"
STRONG = ("exact", "propagated")


def main():
    quiet = lambda *a: None
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        src, exe = "config/GC_2_7", None
        for i, v in enumerate(STEPS):
            A, B, mapped, how, by, order = w.run(src, exe, G + v + "/mwcceppc.exe", quiet)
            out = w.fill_modules(w.build_layout(A, B, mapped, how, by))
            (tmp / "functions.json").write_text(json.dumps(out))
            src, exe = str(tmp), G + v + "/mwcceppc.exe"
            print(v, "named", sum(not e["name"].startswith("fn_") for e in out), "of", len(out))
        chain = {e["address"]: e for e in out if not e["name"].startswith("fn_") and e["how"] in STRONG}
    A, B, mapped, how, by, order = w.run(RC1, "build/compilers/Wii/1.0RC1/mwcceppc.exe", G + "3.0a5.2/mwcceppc.exe", quiet)
    rc1 = json.loads((w.resolve(RC1) / "functions.json").read_text())
    names = json.loads((w.resolve(RC1) / "names_from_2_7.json").read_text()) if (w.resolve(RC1) / "names_from_2_7.json").exists() else {}
    names = {k: v for k, v in names.items() if "via" not in v["how"]}
    n = 0
    for f in rc1:
        if not f["name"].startswith("fn_") or f["address"] in names:
            continue
        a = int(f["address"], 16)
        if a not in mapped or how[a] not in STRONG:
            continue
        c = chain.get(f"0x{mapped[a]:08x}")
        if c:
            names[f["address"]] = {"name": c["name"], "how": f"via 3.0a5.2: rc1 {how[a]}, chain {c['how']}"}
            n += 1
    print(f"+{n} via 3.0a5.2, {len(names)} in total")
    (w.resolve(RC1) / "names_from_2_7.json").write_text(json.dumps(dict(sorted(names.items())), indent=1) + "\n")


if __name__ == "__main__":
    main()
