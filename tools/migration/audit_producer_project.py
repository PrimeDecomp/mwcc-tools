"""Recheck retained C/Rust project traces, inputs, sources and producer objects."""
import argparse
from collections import Counter
import json
from pathlib import Path
from verify_producer import ROOT, full_trace, known_schedule_candidates, sha, trace_line_endings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--manifest", default="echoes-blocks-verification.json")
    parser.add_argument("--source-root", type=Path)
    args = parser.parse_args()
    stage = args.directory.resolve()
    manifest_path = stage / args.manifest
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    assert manifest["complete"]
    logs = {entry["name"]: entry for entry in manifest["logs"]}
    review = dict(manifest_sha256=sha(manifest_path), verifier_sha256=sha(ROOT / "tools/migration/verify_producer.py"), versions=[])
    for version in manifest["versions"]:
        counts, known, chained, cases = Counter(), 0, 0, []
        for case in version["cases"]:
            documents = []
            for label in ["C", "Rust"]:
                name = f"{version['version']}-{Path(case['source']).stem}-{int(case['all_traces'])}-{label}"
                entry = logs[name]
                assert entry["exit_code"] == 0, name
                for stream in ["stdout", "stderr"]:
                    assert sha(stage / entry[stream]) == entry[stream + "_sha256"], name
                documents.append(json.loads((stage / entry["stdout"]).read_text(encoding="utf-8"))["compilation"])
            a, b = documents
            assert a["identical"] is True and b["identical"] is True
            for field in ["source_sha256", "flags", "trace_options", "object_sha256", "diagnostics"]:
                assert a[field] == b[field], (case, field)
            assert b["producer_sha256"] == version["producer_sha256"]
            left, right = full_trace(a["trace"]), full_trace(b["trace"])
            assert left == right, case
            assert trace_line_endings(a["trace"]) == trace_line_endings(b["trace"]), case
            counts.update(line.split(" ", 1)[0] for line in left)
            known += known_schedule_candidates(left)
            chained += sum(" chain=" in line and bool(line.split(" chain=", 1)[1]) for line in left)
            cases.append(dict(source=case["source"], all_traces=case["all_traces"],
                source_sha256=a["source_sha256"], object_sha256=a["object_sha256"], records=len(left)))
        obj = stage / f"build/{version['version']}/oracle/compiled/rust/hooks.obj"
        assert sha(obj) == version["rust_object_sha256"]
        assert sha(stage / f"build/{version['version']}/oracle/mwcceppc.exe") == version["producer_sha256"]
        if "frontend_object_sha256" in version:
            assert sha(stage / f"build/{version['version']}/oracle/compiled/rust/frontend.obj") == version["frontend_object_sha256"]
            assert sha(stage / version["frontend_reference"]) == version["frontend_object_sha256"]
            assert version["hook_inventory_count"] == 31
        if "trace_object_sha256" in version:
            trace = stage / f"build/{version['version']}/oracle/OracleTrace.obj"
            expected_trace = stage / manifest["trace_reference"]["path"]
            assert sha(trace) == sha(expected_trace) == version["trace_object_sha256"] == manifest["trace_reference"]["sha256"]
        assert known > 0
        inline = sum(count for kind, count in counts.items() if kind.startswith("inl."))
        if "inline_records" in version:
            assert inline == version["inline_records"] and chained == version["chained_calls"]
            assert inline > 0 and chained > 0
        if "instantiation_kinds" in version:
            assert {kind: count for kind, count in counts.items() if kind.startswith("tp.")} == version["instantiation_kinds"]
        if "binding_records" in version:
            assert counts["lc.arg"] == version["binding_records"] > 0
        review["versions"].append(dict(version=version["version"], cases=cases, counts=dict(counts),
            known_schedule_candidates=known, inline_records=inline, chained_calls=chained, rust_object_sha256=sha(obj)))
    sources = [stage / "src/oracle/Hooks.c", stage / "src/oracle/OracleTrace.c", stage / "rust-toolchain.toml",
               stage / "config/sources.json", stage / "crates/mwcc-oracle-hooks/Cargo.toml",
               *[stage / f"config/{v['version']}/config.json" for v in manifest["versions"]],
               *sorted((stage / "crates/mwcc-oracle-hooks/src").glob("*.rs")),
               *sorted(p for p in (stage / "include").rglob("*") if p.is_file())]
    review["source_sha256"] = {}
    for source in sources:
        name = source.relative_to(stage).as_posix()
        review["source_sha256"][name] = sha(source)
        if args.source_root:
            assert sha(args.source_root / name) == sha(source), name
    (stage / "project-log-audit.json").write_text(json.dumps(review, indent=2) + "\n", encoding="utf-8")
    print(f"{stage}: project traces, inputs, sources and object hashes revalidated")


if __name__ == "__main__":
    main()
