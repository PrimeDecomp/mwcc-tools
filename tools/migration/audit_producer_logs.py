"""Revalidate retained producer fixture/corpus outputs without recompiling or changing them."""
import argparse
import hashlib
import json
from pathlib import Path
from verify_producer import ROOT, full_trace, known_schedule_candidates, trace_line_endings


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    stage = args.directory.resolve()
    manifest = stage / "producer-verification.json"
    recorded = json.loads(manifest.read_text(encoding="utf-8")) if manifest.exists() else {}
    built_versions = {v["version"]: v for v in recorded.get("versions", [])}
    logs = {}
    for entry in recorded.get("logs", []):
        if entry["exit_code"] == 0:
            for stream in ["stdout", "stderr"]:
                assert sha(stage / entry[stream]) == entry[stream + "_sha256"], entry
            logs[entry["name"]] = stage / entry["stdout"]
    def log(name):
        # Older retained roots may have lost their fixture manifest when a
        # project-only run replaced it. Their original raw logs remain usable.
        return logs.get(name, stage / (name + ".stdout"))
    fixtures = ROOT / "tests/fixtures/oracle"
    cases = json.loads((fixtures / "manifest.json").read_text())["cases"]
    evidence = dict(revalidated_from_retained_outputs=True, versions=[])
    for version in ["GC_1_3", "GC_1_3_2", "GC_2_7"]:
        built = built_versions.get(version)
        if built:
            assert sha(stage / f"build/{version}/oracle/mwcceppc.exe") == built["producer_sha256"]
            assert sha(stage / f"build/{version}/oracle/compiled/rust/hooks.obj") == built["rust_object_sha256"]
            if "frontend_object_sha256" in built:
                assert sha(stage / f"build/{version}/oracle/compiled/rust/frontend.obj") == built["frontend_object_sha256"]
                assert sha(stage / built["frontend_reference"]) == built["frontend_object_sha256"]
                assert built["hook_inventory_count"] == 31
                assert not (stage / f"build/{version}/oracle/compiled/src/oracle/Hooks.c.obj").exists()
            if "trace_object_sha256" in built:
                expected = recorded["trace_reference"]
                assert sha(stage / expected["path"]) == expected["sha256"] == built["trace_object_sha256"]
                assert sha(stage / f"build/{version}/oracle/OracleTrace.obj") == expected["sha256"]
        item = dict(version=version, cases=[], graph_records=0, frame_records=0, frame_layout_records=0,
                    pcode_records=0, schedule_records=0, known_schedule_candidates=0, block_records=0,
                    block_kinds={}, block_reasons={}, scalar_records=0, cse_records=0, propagation_records=0,
                    scalar_kinds={}, cse_kinds={}, propagation_kinds={}, inline_records=0, chained_calls=0, instantiation_kinds={})
        for case in [c for c in cases if c["version"] == version]:
            for all_traces in [False, True]:
                prefix = f"{version}-{Path(case['source']).stem}-{int(all_traces)}"
                files = [log(f"{prefix}-{label}") for label in ["C", "Rust"]]
                a, b = [json.loads(path.read_text(encoding="utf-8"))["compilation"] for path in files]
                assert a["identical"] is True and b["identical"] is True, prefix
                if built:
                    assert b["producer_sha256"] == built["producer_sha256"], prefix
                assert a["object_sha256"] == b["object_sha256"] and a["diagnostics"] == b["diagnostics"], prefix
                assert a["flags"] == b["flags"] and a["trace_options"] == b["trace_options"], prefix
                assert a["source_sha256"] == b["source_sha256"] == sha(fixtures / case["source"]), prefix
                left, right = full_trace(a["trace"]), full_trace(b["trace"])
                assert left == right, prefix
                assert trace_line_endings(a["trace"]) == trace_line_endings(b["trace"]), prefix
                item["graph_records"] += sum(line.startswith(("ra.graph ", "ra.node ", "ra.order ", "ra.select ")) for line in left)
                item["frame_records"] += sum(line.startswith("fr.") for line in left)
                item["frame_layout_records"] += sum(line.startswith("fr.layout ") for line in left)
                item["pcode_records"] += sum(line.startswith(("ra.i ", "ra.block ", "st.i ", "st.block ", "bk.i ")) for line in left)
                item["schedule_records"] += sum(line.startswith("sc.") for line in left)
                item["known_schedule_candidates"] += known_schedule_candidates(left)
                item["inline_records"] += sum(line.startswith("inl.") for line in left)
                item["chained_calls"] += sum(" chain=" in line and bool(line.split(" chain=", 1)[1]) for line in left)
                for line in left:
                    if line.startswith("tp."):
                        kind = line.split(" ", 1)[0]
                        item["instantiation_kinds"][kind] = item["instantiation_kinds"].get(kind, 0) + 1
                for family, field in [("lc.", "scalar"), ("ce.", "cse"), ("pp.", "propagation")]:
                    for line in left:
                        if line.startswith(family):
                            item[field + "_records"] += 1
                            kind = line.split(" ", 1)[0]
                            counts = item[field + "_kinds"]
                            counts[kind] = counts.get(kind, 0) + 1
                for line in left:
                    if line.startswith("bk."):
                        item["block_records"] += 1
                        kind = line.split(" ", 1)[0]
                        item["block_kinds"][kind] = item["block_kinds"].get(kind, 0) + 1
                        if kind == "bk.pair":
                            why = line.split(" why=", 1)[1]
                            item["block_reasons"][why] = item["block_reasons"].get(why, 0) + 1
                item["cases"].append(dict(source=case["source"], all_traces=all_traces, records=len(left),
                    log_sha256={p.name: sha(p) for p in files}, producer_sha256=b["producer_sha256"]))
        assert item["graph_records"] > 0 and item["frame_records"] > 0 and item["frame_layout_records"] > 0
        assert item["pcode_records"] > 0 and item["schedule_records"] > 0 and item["known_schedule_candidates"] > 0
        assert all(item["block_kinds"].get(kind, 0) > 0 for kind in ["bk.stmt", "bk.list", "bk.block", "bk.merge", "bk.pair", "bk.i"])
        assert all(item["scalar_kinds"].get(kind, 0) > 0 for kind in ["lc.pass", "lc.part", "lc.use"])
        assert all(item["cse_kinds"].get(kind, 0) > 0 for kind in ["ce.pass", "ce.end", "ce.l", "ce.move", "ce.temp", "ce.object", "ce.vreg"])
        assert all(item["propagation_kinds"].get(kind, 0) > 0 for kind in ["pp.copy", "pp.expression", "pp.dead"])
        assert item["inline_records"] > 0 and item["chained_calls"] > 0
        if built and "instantiation_kinds" in built:
            assert item["instantiation_kinds"] == built["instantiation_kinds"]
            assert all(item["instantiation_kinds"].get(kind, 0) > 0 for kind in ["tp.queue", "tp.func", "tp.gen", "tp.emit"])
        if built and "binding_records" in built:
            assert item["scalar_kinds"]["lc.arg"] == built["binding_records"] > 0
        corpus_file = log(f"canaries-{version}")
        corpus = json.loads(corpus_file.read_text(encoding="utf-8"))
        assert corpus["total"] > 0 and corpus["identical"] == corpus["total"]
        assert all(not outcome["problems"] for outcome in corpus["outcomes"])
        item["canaries"] = dict(total=corpus["total"], identical=corpus["identical"], log_sha256=sha(corpus_file))
        evidence["versions"].append(item)
        print(f"{version}: {len(item['cases'])} compilations, graph={item['graph_records']}, frame={item['frame_records']}, PCode={item['pcode_records']}, schedule={item['schedule_records']}, scalar={item['scalar_records']}, CSE={item['cse_records']}, propagation={item['propagation_records']}, inline={item['inline_records']}, chained={item['chained_calls']}, {corpus['identical']}/{corpus['total']} canaries verified")
    output = args.output or stage / "fixture-log-audit.json"
    output.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
