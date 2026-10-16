"""Pinned Python reference worker for benchmark.py; no production callers."""
import gc
import importlib
import json
from pathlib import Path
import sys
import time


def main():
    manifest, phase, samples, batch, output, reference = sys.argv[1:]
    samples, batch = int(samples), int(batch)
    assert samples > 0 and batch >= 0
    data = json.loads(Path(manifest).read_text())
    sys.path.insert(0, str(Path(reference) / "tools"))
    oracle = importlib.import_module("oracle")
    colouring = importlib.import_module("colouring")
    version = data["version"]
    lines = [] if phase in ("compile_checked", "end_to_end_checked") else Path(data["trace"]).read_text().splitlines()
    allocations = [] if phase == "parse" or not lines else oracle.parse_trace(lines)[0]
    names = oracle.opcode_names(version)
    graphs = []
    for _, before, _, _ in allocations:
        for cls in ("GPR", "FPR"):
            rounds = [r for r in before.rounds if oracle.CLASSES[r.cls] == cls]
            if rounds:
                graphs.append((rounds[-1], cls, rounds[0].count))
    search = data["search"]
    obj = search["round"]
    r = oracle.Round(obj["class"], obj["k"], obj["start"], obj["count"])
    for key in ("mask", "mask_after", "ok"):
        setattr(r, key, obj[key])
    r.nodes = {int(v): {**n, "p": n["pointer"]} for v, n in obj["nodes"].items()}
    r.order = [tuple(v) for v in obj["order"]]
    r.colors = {int(v): tuple(c) for v, c in obj["colors"].items()}
    class Allocation:
        function = link = "benchmark"
        def last_round(self, cls):
            return r
        def register_count(self, cls):
            return search["count"]
        def name(self, cls, v):
            return None
    problem = colouring.Problem(Allocation(), search["class"], {int(v): t for v, t in search["targets"].items()})
    options = dict(search["options"], seed=int(search["options"]["seed"]))
    def compile(analyze):
        c = data["compile"]
        oracle.ROOT = Path(c["root"])
        import os
        previous = Path.cwd()
        try:
            os.chdir(c["working_directory"])
            compiled = oracle.compile_trace(c["source"], c["flags"], version=version, check=True)
            assert compiled["identical"] is True
            if analyze:
                return oracle.document_for(compiled)[0]
            import hashlib
            return {"object_sha256": hashlib.sha256(compiled["object"]).hexdigest(), "verified": True,
                    "trace_lines": len(compiled["lines"]), "trace_present": bool(compiled["lines"])}
        finally:
            os.chdir(previous)
    def run():
        if phase == "parse":
            return oracle.parse_trace(lines)
        if phase == "analyze":
            return [oracle.analyze(name, before, after, names) for name, before, after, _ in allocations]
        if phase == "replay":
            return [colouring.replay(r, cls, count) for r, cls, count in graphs]
        if phase == "search":
            return colouring.search(problem, **options)
        if phase in ("compile_checked", "end_to_end_checked"):
            return compile(phase == "end_to_end_checked")
        raise ValueError(phase)
    began = time.perf_counter_ns()
    result = run()
    del result
    if batch == 0:
        warmup = max(1, time.perf_counter_ns() - began)
        batch = max(1, min(1000, (100000000 + warmup - 1) // warmup))
    durations = []
    for _ in range(samples):
        gc.collect()
        began = time.perf_counter_ns()
        for _ in range(batch):
            result = run()
            del result
        durations.append(time.perf_counter_ns() - began)
    result = run()
    if phase == "parse":
        result = [{"link": before.link, "before_blocks": len(before.blocks), "after_blocks": len(after.blocks),
                   "rounds": [[r.cls, len(r.nodes), len(r.order)] for r in before.rounds]}
                  for _, before, after, _ in result[0]]
    Path(output).write_text(json.dumps(result), encoding="utf-8")
    print(json.dumps({"phase": phase, "samples_ns": durations, "batch": batch, "warmups": 1}))


if __name__ == "__main__":
    main()
