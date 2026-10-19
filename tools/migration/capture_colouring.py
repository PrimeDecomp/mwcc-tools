"""Freeze independent Python colouring/search and RNG contracts without compilers."""
import argparse
import importlib
import json
from pathlib import Path
import random
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.oracle_root / "tools"))
    oracle = importlib.import_module("oracle")
    colouring = importlib.import_module("colouring")

    def graph(k, mask, edges, coalesced=()):
        r = oracle.Round(4, k, 32, 64)
        r.mask = mask
        for a, b in edges:
            for x, y in ((a, b), (b, a)):
                if x >= 32:
                    r.nodes.setdefault(x, {"p": hex(x), "degree": 0, "flags": 4 if x in coalesced else 0,
                                           "color": dict(coalesced).get(x, -1), "neighbors": []})
                    if y not in r.nodes[x]["neighbors"]:
                        r.nodes[x]["neighbors"].append(y)
        for node in r.nodes.values():
            node["neighbors"].sort()
            node["degree"] = len(node["neighbors"])
        order, _ = colouring.simplify(r)
        r.order = [(v, 0) for v in order]
        r.colors = {v: (c, 2) for v, c in colouring.replay(r, "GPR")["colour"].items()}
        return r

    def serialized(r):
        return {"class": r.cls, "k": r.k, "start": r.start, "count": r.count,
                "nodes": {v: {"pointer": n["p"], "degree": n["degree"], "flags": n["flags"],
                              "color": n["color"], "neighbors": n["neighbors"]} for v, n in r.nodes.items()},
                "order": r.order, "mask": r.mask, "mask_after": r.mask_after, "ok": r.ok,
                "colors": r.colors, "color_order": list(r.colors)}

    class Allocation:
        function = link = "fixture"
        def __init__(self, r):
            self.r = r
        def last_round(self, cls):
            return self.r
        def register_count(self, cls):
            return None
        def name(self, cls, v):
            return None

    clique = graph(8, 9, [(v, p) for v in range(32, 38) for p in (0, 3)]
                   + [(a, b) for a in range(32, 38) for b in range(a+1, 38)])
    drop = graph(4, 9, [(32, 0), (32, 3), (32, 33), (32, 34), (33, 0), (33, 3), (34, 6)], {34: 6})
    cases = []
    configs = [("reverse-six", clique, {v: 63-v for v in range(32, 38)}, [], 600, 3),
               ("drop-coalesced", drop, {32: 30, 33: 31}, ["drop", "degree", "unlink"], 200, 4)]
    for name, r, wants, kinds, iterations, restarts in configs:
        for seed in (1, 2, -42, 123456789012345678901234567890):
            problem = colouring.Problem(Allocation(r), "GPR", wants)
            options = {"kinds": kinds, "iterations": iterations, "seed": str(seed), "nodes": None,
                       "fixed": [], "keep": 8, "restarts": restarts}
            result = colouring.search(problem, kinds=kinds, iterations=iterations, seed=seed, restarts=restarts)
            cases.append({"name": f"{name}/{seed}", "round": serialized(r), "targets": wants,
                          "options": options, "force": colouring.force(problem), "search": result})
    rng_cases = []
    for seed in (0, 1, -42, 123456789012345678901234567890):
        rng = random.Random(seed)
        operations = []
        for _ in range(100):
            operations.append({"random_bits": struct.unpack(">Q", struct.pack(">d", rng.random()))[0],
                               "below": [rng.randrange(n) for n in (1, 2, 3, 16, 29, 65537)],
                               "sample_small": rng.sample(range(21), 2), "sample_large": rng.sample(range(23), 2),
                               "bits": [rng.getrandbits(width) for width in (0, 1, 31, 32, 33, 64)]})
        rng_cases.append({"seed": str(seed), "operations": operations})
    directory = ROOT / "tests/fixtures/colouring"
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "search.json").write_text(json.dumps(cases, indent=2) + "\n", encoding="utf-8")
    (directory / "random.json").write_text(json.dumps(rng_cases, indent=2) + "\n", encoding="utf-8")
    decoder_rng = random.Random(17)
    words = {decoder_rng.getrandbits(32) for _ in range(1000)}
    words.update((op << 26) | (7 << 21) | (a << 16) | (13 << 11) | (xo << 1)
                 for op in (4, 31, 59, 63) for a in (0, 9) for xo in range(1024))
    decoded = [{"word": w, "fields": colouring.register_fields(w), "shape": colouring.shape(w),
                "token": oracle.instruction_token(w), "r1_offset": oracle.r1_offset(w)} for w in sorted(words)]
    (directory / "decoder.json").write_text(json.dumps(decoded) + "\n", encoding="utf-8")
    target_cases = []
    saved = {}
    for version in ("GC_1_3_2", "GC_2_7"):
        for name in ("registers", "pipeline", "pressure", "templates"):
            prefix = ROOT / f"tests/fixtures/oracle/{version}/{name}"
            compiled = {"lines": prefix.with_suffix(".trace").read_text().splitlines(),
                        "object": prefix.with_suffix(".o").read_bytes()}
            allocations = colouring.allocations(compiled)
            saved[f"{version}/{name}"] = allocations
            for index, allocation in enumerate(allocations):
                ours = allocation.words or []
                permuted = []
                for word in ours:
                    for cls, field, reg, _ in colouring.register_fields(word) or []:
                        if reg >= 14:
                            shift = colouring.SHIFTS[field]
                            word = (word & ~(31 << shift)) | ((45-reg) << shift)
                    permuted.append(word)
                for cls in ("GPR", "FPR"):
                    if allocation.last_round(cls) is None:
                        continue
                    for variant, theirs in (("identity", ours), ("permuted", permuted), ("reverse", list(reversed(permuted)))):
                        wants = {} if variant != "reverse" else {min(allocation.last_round(cls).nodes): 0}
                        target_cases.append({"case": f"{version}/{name}", "allocation": index, "class": cls,
                                             "variant": variant, "words": theirs, "wants": wants,
                                             "expected": colouring.derive_targets(allocation, theirs, cls, wants)})
    (directory / "targets.json").write_text(json.dumps(target_cases, indent=2) + "\n", encoding="utf-8")
    remaps = []
    for name in ("registers", "pipeline", "pressure", "templates"):
        first, second = f"GC_1_3_2/{name}", f"GC_2_7/{name}"
        for index, (a, b) in enumerate(zip(saved[first], saved[second])):
            result = colouring.remap(a, b)
            for cls in ("GPR", "FPR"):
                remaps.append({"first": first, "second": second, "allocation": index, "class": cls,
                               "expected": {"class": cls, "mapping": {v: w for (c, v), w in result["mapping"].items() if c == cls},
                                            "changed": {v: w for (c, v), w in result["mapping"].items() if c == cls and v != w},
                                            "unsure": {v: w for (c, v), w in result["unsure"].items() if c == cls},
                                            "paired": result["paired"], "instructions": result["instructions"],
                                            "graph": colouring.graph_difference(a, b, result["mapping"], cls)}})
    (directory / "remaps.json").write_text(json.dumps(remaps, indent=2) + "\n", encoding="utf-8")
    assembly = []
    for allocation in saved["GC_1_3_2/registers"]:
        assembly.append(f'.fn "{allocation.link}", global')
        for index, word in enumerate(allocation.words):
            for _, field, reg, _ in colouring.register_fields(word) or []:
                if reg >= 14:
                    shift = colouring.SHIFTS[field]
                    word = (word & ~(31 << shift)) | ((45-reg) << shift)
            assembly.append(f"/* {index*4:08X} {word.to_bytes(4, 'big').hex(' ').upper()} */ .4byte 0x{word:08x}")
        assembly.append(f'.endfn "{allocation.link}"')
    (directory / "registers-target.s").write_text("\n".join(assembly) + "\n", encoding="utf-8")
    import subprocess
    prefix = "tests/fixtures/oracle/GC_1_3_2/registers"
    common = ["--load", prefix, "--function", "across_calls", "--version", "GC_1_3_2"]
    target = ["--target-asm", "tests/fixtures/colouring/registers-target.s"]
    r = saved["GC_1_3_2/registers"][0].last_round("GPR")
    nodes = sorted(r.nodes)
    commands = [["verify", *common], ["target", *common, *target], ["force", *common, *target],
                ["replay", *common, *target, "--move", f"{nodes[-1]}:before:{nodes[0]}",
                 "--degree", f"{nodes[0]}:+1"],
                ["search", *common, *target, "--seed", "2", "--iterations", "240", "--restarts", "3"],
                ["remap", *common, "--load", "tests/fixtures/oracle/GC_2_7/registers"]]
    cli_cases = []
    for command in commands:
        py = [sys.executable, str((args.oracle_root / "tools/colouring.py").resolve()), *command]
        text = subprocess.run(py, cwd=ROOT, capture_output=True, text=True, timeout=120)
        doc = subprocess.run([*py, "--json"], cwd=ROOT, capture_output=True, text=True, timeout=120)
        if text.returncode or doc.returncode:
            raise RuntimeError(f"CLI baseline failed: {text.stderr}{doc.stderr}")
        cli_cases.append({"args": command, "exit_code": doc.returncode, "json": json.loads(doc.stdout), "text": text.stdout})
    (directory / "cli.json").write_text(json.dumps(cli_cases, indent=2) + "\n", encoding="utf-8")
    print(f"Captured {len(cases)} search cases and {len(rng_cases)} mixed RNG sequences")
    print(f"Captured {len(decoded)} instruction decodings, {len(target_cases)} target cases and {len(remaps)} remaps")


if __name__ == "__main__":
    main()
