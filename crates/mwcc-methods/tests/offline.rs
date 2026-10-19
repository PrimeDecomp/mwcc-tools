//! The methods on saved traces, captures and fixtures: no compiler runs. These are the expectations the removed
//! oracle, colouring, provenance and source-guided commands' tests held, through the methods.
use mwcc_methods::{Session, call};
use mwcc_oracle_core::{
    runner::{CheckStatus, Compilation, TraceOptions},
    source_guided::state,
    version::Version,
};
use serde_json::{Value, json};
use std::{
    fs,
    path::{Path, PathBuf},
};

fn root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn fixture(name: &str) -> PathBuf {
    root().join("tests/fixtures/oracle").join(name)
}

fn session() -> Session {
    Session::new(root(), None)
}

/// METHOD's only result, or its error.
fn one(session: &Session, method: &str, params: Value) -> anyhow::Result<Value> {
    let results = std::sync::Mutex::new(vec![]);
    call(session, method, params, &|| false, &|r| {
        results.lock().unwrap().push(r)
    })?;
    let mut results = results.into_inner().unwrap();
    assert_eq!(results.len(), 1, "{method}: {results:?}");
    Ok(results.remove(0))
}

fn error(session: &Session, method: &str, params: Value) -> String {
    format!(
        "{:#}",
        one(session, method, params).expect_err("the call should fail")
    )
}

#[test]
fn source_search_state_distinguishes_normalization_gaps_from_invalid_captures() {
    let session = session();
    let scratch = tempfile::tempdir().unwrap();
    let capture = scratch.path().join("capture.json");
    let object = scratch.path().join("object.o");
    let bytes = fs::read(fixture("GC_2_7/pipeline.o")).unwrap();
    fs::write(&object, &bytes).unwrap();
    let instruction = "p=123 op=403 flags=122 n=1 line=2 ops=010001009203000000000000";
    let trace = format!(
        "pv.function.begin schema=1 function=1 parent=0 c0=4 c1=8 c2=32 c3=32 c4=32 coverage=direct_counter_writes link=pipeline__Fii\nra.before function=pipeline link=pipeline__Fii\nra.block p=100 index=0 weight=1 flags=0 succ=\nra.i {instruction}\nra.end\npv.graph.begin schema=1 function=1 graph=1 births=0 cls=4 physical=32 start=32 count=32 phase=3 boundary=before_simplify\npv.graph.instruction {instruction}\npv.graph.end function=1 graph=1 nodes=0 instructions=1 complete=1\nra.graph cls=4 k=29 start=32 count=32\nra.after function=pipeline link=pipeline__Fii\nra.block p=100 index=0 weight=1 flags=0 succ=\nra.i {instruction}\nra.end\npv.function.end function=1 births=0 graphs=1 c0=4 c1=8 c2=32 c3=32 c4=32\n"
    );
    let mut compilation = Compilation {
        version: Version::Gc27,
        source: "model.cpp".into(),
        oracle: "model".into(),
        producer_sha256: "model".into(),
        original_sha256: Some("stock".into()),
        source_sha256: state::digest(b"model source"),
        flags: vec![],
        trace_options: TraceOptions {
            backend: true,
            ..TraceOptions::default()
        },
        object_sha256: state::digest(&bytes),
        check_status: CheckStatus::Verified,
        identical: Some(true),
        differences: vec![],
        diagnostics: String::new(),
        trace: trace.clone(),
        trace_present: true,
        original_fallback: false,
        object: vec![],
    };
    let save = |c: &Compilation| {
        fs::write(
            &capture,
            serde_json::to_vec(&json!({"compilation": c})).unwrap(),
        )
        .unwrap()
    };
    let params = |round: u32| json!({"capture": capture, "object": object, "link": "pipeline__Fii", "class": 4, "round": round});
    let run = |round| one(&session, "source_search.state", params(round));
    save(&compilation);
    let report = run(0).unwrap()["document"].clone();
    assert_eq!(report["normalization"]["state"], "satisfied");
    assert_eq!(report["fingerprint"].as_str().unwrap().len(), 64);
    compilation.trace = trace
        .lines()
        .filter(|line| !line.starts_with("ra."))
        .collect::<Vec<_>>()
        .join("\n");
    save(&compilation);
    let report = run(0).unwrap()["document"].clone();
    assert_eq!(report["normalization"]["state"], "indeterminate");
    assert!(report["fingerprint"].is_null());
    compilation.trace = trace.replace("010001009203000000000000", "630001009203000000000000");
    save(&compilation);
    let report = run(0).unwrap()["document"].clone();
    assert_eq!(report["normalization"]["state"], "indeterminate");
    assert!(report["fingerprint"].is_null());
    compilation.trace = trace;
    save(&compilation);
    assert!(run(99).is_err());
    fs::write(&object, b"changed object").unwrap();
    assert!(error(&session, "source_search.state", params(0)).contains("object bytes"));
}

#[test]
fn provenance_inspect_reads_captures_and_keeps_coverage_explicit() {
    let session = session();
    let scratch = tempfile::tempdir().unwrap();
    let path = scratch.path().join("capture.json");
    let trace = "pv.coalesce.begin schema=1 pass=1 cls=4 count=40 physical=32 first=32 last=40 special=1 creation=missing link=test__Fv\npv.coalesce.candidate pass=1 candidate=1 instruction=100 destination=35 source=36\npv.coalesce.check pass=1 candidate=1 condition=copy_flag site=10f taken=1 eflags=0\npv.coalesce.end pass=1 candidates=1\n";
    fs::write(
        &path,
        serde_json::to_vec(&json!({"compilation":{"trace":trace},"trace":"unrelated fallback","report":{"ignored":[1,{"trace":"not compiler evidence"}]}})).unwrap(),
    )
    .unwrap();
    let inspect = |link: Option<&str>| {
        one(
            &session,
            "provenance.inspect",
            json!({"trace_path": path, "link": link, "text": true}),
        )
    };
    let value = inspect(Some("test__Fv")).unwrap();
    let document = &value["document"];
    assert_eq!(document["format_version"], 2);
    assert_eq!(document["decisions"]["state"], "satisfied");
    assert_eq!(document["lifecycle"]["state"], "indeterminate");
    assert!(
        value["text"]
            .as_array()
            .unwrap()
            .iter()
            .any(|l| l.as_str().unwrap().contains("test__Fv: pass 1"))
    );
    assert!(inspect(Some("absent")).is_err());
    fs::write(&path, "ra.before function=old link=old\nra.end\n").unwrap();
    assert_eq!(
        inspect(None).unwrap()["document"]["decisions"]["state"],
        "indeterminate"
    );
    fs::write(&path, trace.replace("eflags=0", "eflags=40")).unwrap();
    assert!(inspect(None).is_err());
}

#[test]
fn provenance_link_filter_keeps_graphs_and_function_coverage_in_the_same_view() {
    let session = session();
    let scratch = tempfile::tempdir().unwrap();
    let path = scratch.path().join("graphs.trace");
    let mut trace = String::new();
    for (id, link) in [(1, "first"), (2, "second")] {
        trace += &format!(
            "pv.function.begin schema=1 function={id} parent=0 c0=4 c1=8 c2=32 c3=32 c4=32 coverage=direct_counter_writes link={link}\n\
            pv.graph.begin schema=1 function={id} graph=1 births=0 cls=4 physical=32 start=32 count=32 phase=3 boundary=before_simplify\n\
            pv.graph.end function={id} graph=1 nodes=0 instructions=0 complete=1\n\
            pv.function.end function={id} births=0 graphs=1 c0=4 c1=8 c2=32 c3=32 c4=32\n"
        );
    }
    fs::write(&path, trace).unwrap();
    let report = one(
        &session,
        "provenance.inspect",
        json!({"trace_path": path, "link": "second"}),
    )
    .unwrap()["document"]
        .clone();
    assert_eq!(report["history"]["functions"].as_array().unwrap().len(), 1);
    for collection in ["graphs", "functions"] {
        let entries = report["graphs"][collection].as_array().unwrap();
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0]["link"], "second");
    }
    assert_eq!(report["graphs"]["observations"]["state"], "satisfied");
}

fn graph_trace() -> String {
    "pv.function.begin schema=1 function=1 parent=0 c0=4 c1=8 c2=32 c3=32 c4=32 coverage=direct_counter_writes link=case__Fv\npv.value.birth schema=1 function=1 value=1 cls=4 register=32 count=33 physical=32 site=400028 expression=0 phase=0 line=3 category=unknown\npv.value.object function=1 value=1 object=00000001 info=123 part=1 datatype=1 storage=0 flags_before=0 type_kind=1 type_size=4 name_known=1 name_complete=1 name_hex=666c6167\npv.graph.begin schema=1 function=1 graph=1 births=1 cls=4 physical=32 start=32 count=33 phase=3 boundary=before_simplify\npv.graph.node v=32 visible=0 deg=0 flags=0 color=-1 n=\npv.graph.end function=1 graph=1 nodes=1 instructions=0 complete=1\npv.function.end function=1 births=1 graphs=1 ranges=0 c0=4 c1=8 c2=32 c3=32 c4=33\n".into()
}

#[test]
fn provenance_compare_needs_a_version_for_raw_traces_and_gives_structured_coverage() {
    let session = session();
    let directory = tempfile::tempdir().unwrap();
    let raw = directory.path().join("raw.trace");
    let capture = directory.path().join("capture.json");
    fs::write(&raw, graph_trace()).unwrap();
    fs::write(
        &capture,
        serde_json::to_vec(&json!({"compilation":{"version":"GC_2_7","trace":graph_trace()}}))
            .unwrap(),
    )
    .unwrap();
    let compare = |first: &Path, second: &Path, extra: Value| {
        let mut params = json!({"first": first, "second": second, "link": "case__Fv"});
        params
            .as_object_mut()
            .unwrap()
            .extend(extra.as_object().unwrap().clone());
        one(&session, "provenance.compare", params).map(|r| r["document"].clone())
    };
    assert!(
        format!("{:#}", compare(&raw, &capture, json!({})).unwrap_err()).contains("need `version`")
    );
    for (first, extra) in [(&raw, json!({"version": "GC_2_7"})), (&capture, json!({}))] {
        let result = compare(first, &capture, extra).unwrap();
        assert_eq!(result["format_version"], 1);
        assert_eq!(result["coverage"]["state"], "satisfied");
        assert_eq!(result["mapping"].as_array().unwrap().len(), 1);
        assert_eq!(result["elimination"]["state"], "indeterminate");
    }
    assert!(
        format!(
            "{:#}",
            compare(&capture, &capture, json!({"version": "GC_1_3"})).unwrap_err()
        )
        .contains("disagrees")
    );
    assert!(compare(&capture, &capture, json!({"first_round": 1})).is_err());
    let partial = directory.path().join("partial.trace");
    fs::write(&partial, graph_trace().replace("complete=1", "complete=0")).unwrap();
    let result = compare(&raw, &partial, json!({"version": "GC_2_7"})).unwrap();
    assert_eq!(result["coverage"]["state"], "indeterminate");
    assert!(result["mapping"].as_array().unwrap().is_empty());
}

#[test]
fn source_index_origins_has_native_json_and_validates_its_parameters() {
    let session = session();
    let directory = tempfile::tempdir().unwrap();
    fs::create_dir(directory.path().join("entries")).unwrap();
    let inventory = one(
        &session,
        "source_index.origins",
        json!({"index": directory.path(), "version": "GC_2_7", "link": "example__Fv"}),
    )
    .unwrap()["document"]
        .clone();
    assert_eq!(inventory["format_version"], 1);
    assert_eq!(inventory["scanned_entries"], 0);
    assert!(inventory["entries"].as_array().unwrap().is_empty());
    for params in [
        json!({}),
        json!({"index": directory.path(), "version": "bogus"}),
        json!({"index": directory.path(), "bogus": "value"}),
    ] {
        assert!(one(&session, "source_index.origins", params).is_err());
    }
}

fn evaluation_trace() -> String {
    "pv.function.begin schema=1 function=1 parent=0 c0=4 c1=8 c2=32 c3=32 c4=32 coverage=direct_counter_writes link=pipeline__Fii\npv.category.initial function=1 births=0 i2=31 i3=31 i4=31\npv.category.window function=1 births=0 f2=32 f3=32 f4=32 l2=32 l3=32 l4=32\npv.value.birth schema=1 function=1 value=1 cls=4 register=32 count=33 physical=32 site=400028 expression=0 phase=0 line=3 category=unknown\npv.value.object function=1 value=1 object=00000001 info=123 part=1 datatype=1 storage=0 flags_before=0 type_kind=1 type_size=4 name_known=1 name_complete=1 name_hex=666c6167\npv.graph.begin schema=1 function=1 graph=1 births=1 cls=4 physical=32 start=32 count=33 phase=3 boundary=before_simplify category_ready=1 initial_last=31 first=32 last=32\npv.graph.node v=32 visible=0 deg=0 flags=0 color=-1 n=\npv.graph.end function=1 graph=1 nodes=1 instructions=0 complete=1\npv.function.end function=1 births=1 graphs=1 ranges=2 c0=4 c1=8 c2=32 c3=32 c4=33\n".into()
}

#[test]
fn source_search_evaluate_checks_hashes_stock_semantics_and_the_baseline_triplet() {
    let session = session();
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    let source = root.join("model.cpp");
    fs::write(&source, b"model").unwrap();
    let object = root.join("model.o");
    let bytes = fs::read(fixture("GC_2_7/pipeline.o")).unwrap();
    fs::write(&object, &bytes).unwrap();
    let compilation = Compilation {
        version: Version::Gc27,
        source: "model.cpp".into(),
        oracle: "model".into(),
        producer_sha256: "producer".into(),
        original_sha256: Some("stock".into()),
        source_sha256: state::digest(b"model"),
        flags: vec![],
        trace_options: TraceOptions {
            backend: true,
            ..Default::default()
        },
        object_sha256: state::digest(&bytes),
        check_status: CheckStatus::Verified,
        identical: Some(true),
        differences: vec![],
        diagnostics: String::new(),
        trace: evaluation_trace(),
        trace_present: true,
        original_fallback: false,
        object: vec![],
    };
    let capture = root.join("capture.json");
    let plain = root.join("plain.json");
    fs::write(
        &capture,
        serde_json::to_vec(&json!({"compilation": compilation})).unwrap(),
    )
    .unwrap();
    fs::write(&plain, serde_json::to_vec(&compilation).unwrap()).unwrap();
    let mut contract = json!({"format_version":1,"selection":{"link":"pipeline__Fii","class":4,"round":0},"objective":{"kind":"range_category","value":{"name":"flag","part":"single","type_kind":1,"type_size":4},"target":"inside_coalescing_window"},"relocations":[],"purpose":"mechanism_reproducer"});
    let specification = root.join("contract.json");
    let evaluate = |capture: &Path, extra: Value| {
        let mut params = json!({"contract": specification, "capture": capture, "source": source, "object": object});
        params
            .as_object_mut()
            .unwrap()
            .extend(extra.as_object().unwrap().clone());
        one(&session, "source_search.evaluate", params).map(|r| r["document"].clone())
    };
    let write = |contract: &Value| {
        fs::write(&specification, serde_json::to_vec(contract).unwrap()).unwrap()
    };
    write(&contract);
    for file in [&capture, &plain] {
        let result = evaluate(file, json!({})).unwrap();
        assert_eq!(result["overall"]["state"], "satisfied");
        assert!(result["graph_changes"]["provenance_correspondence"].is_object());
    }
    let result = evaluate(
        &capture,
        json!({"baseline_capture": plain, "baseline_source": source, "baseline_object": object}),
    )
    .unwrap();
    assert_eq!(result["overall"]["state"], "satisfied");
    assert!(evaluate(&capture, json!({"baseline_capture": plain})).is_err());
    let mut altered = serde_json::to_value(&compilation).unwrap();
    altered["object_sha256"] = "wrong".into();
    fs::write(&plain, serde_json::to_vec(&altered).unwrap()).unwrap();
    assert_eq!(
        evaluate(&plain, json!({})).unwrap()["constraints"]["captured_object"]["state"],
        "indeterminate"
    );
    altered = serde_json::to_value(&compilation).unwrap();
    altered["trace"] = evaluation_trace()
        .replace("complete=1", "complete=0")
        .into();
    fs::write(&plain, serde_json::to_vec(&altered).unwrap()).unwrap();
    assert_eq!(
        evaluate(&plain, json!({})).unwrap()["overall"]["state"],
        "indeterminate"
    );
    altered = serde_json::to_value(&compilation).unwrap();
    altered["identical"] = false.into();
    fs::write(&plain, serde_json::to_vec(&altered).unwrap()).unwrap();
    assert_eq!(
        evaluate(&plain, json!({})).unwrap()["constraints"]["stock_equivalence"]["state"],
        "unsatisfied"
    );
    contract["objective"]["target"] = "before_coalescing_window".into();
    write(&contract);
    assert_eq!(
        evaluate(&capture, json!({})).unwrap()["overall"]["state"],
        "unsatisfied"
    );
    contract["objective"]["target"] = "inside_coalescing_window".into();
    contract["purpose"] = "source_transformation".into();
    write(&contract);
    let result = evaluate(&capture, json!({})).unwrap();
    assert_eq!(result["overall"]["state"], "indeterminate");
    assert_eq!(
        result["constraints"]["semantic_check"]["state"],
        "indeterminate"
    );
}

#[test]
fn oracle_analyze_and_saved_colourings_work_without_compilers() {
    let session = session();
    let document = one(
        &session,
        "oracle.analyze",
        json!({"trace_path": fixture("GC_1_3_2/pipeline.trace"), "version": "GC_1_3_2", "function": "pipeline", "raw": true}),
    )
    .unwrap();
    assert_eq!(document["schema_version"], 6);
    assert_eq!(document["identical"], Value::Null);
    assert_eq!(document["functions"].as_array().unwrap().len(), 1);
    let expected: Value =
        serde_json::from_slice(&fs::read(fixture("GC_1_3_2/pipeline.views.json")).unwrap())
            .unwrap();
    assert_eq!(document["raw"], expected["raw_selected"]["pipeline"]);
    let trace = one(
        &session,
        "trace.load",
        json!({"load": fixture("GC_1_3_2/pipeline")}),
    )
    .unwrap()["trace"]
        .clone();
    let result = one(&session, "trace.verify", json!({"trace": trace})).unwrap()["result"].clone();
    assert_eq!(result["ok"], true);
    let expected: Value =
        serde_json::from_slice(&fs::read(fixture("GC_1_3_2/pipeline.colouring.json")).unwrap())
            .unwrap();
    assert_eq!(result["functions"], expected);
}

#[test]
fn offline_regdiff_matches_the_archived_helper() {
    let session = session();
    let root = fixture("");
    let manifest: Value =
        serde_json::from_slice(&fs::read(root.join("manifest.json")).unwrap()).unwrap();
    for case in manifest["cases"].as_array().unwrap() {
        let id = case["id"].as_str().unwrap();
        let cases: Vec<Value> =
            serde_json::from_slice(&fs::read(root.join(format!("{id}.regdiff.json"))).unwrap())
                .unwrap();
        for e in cases {
            let result = one(
                &session,
                "oracle.regdiff",
                json!({
                    "trace_path": root.join(format!("{id}.trace")),
                    "diff_path": root.join(format!("{id}.regdiff-input.json")),
                    "version": case["version"],
                    "function": e["query"],
                    "full": true, "pcode": true, "inlining": true, "text": true,
                }),
            )
            .unwrap();
            let lines: Vec<String> = serde_json::from_value(result["text"].clone()).unwrap();
            let expected: Vec<String> = serde_json::from_value(e["text"].clone()).unwrap();
            if lines != expected {
                let mismatch = (0..lines.len().max(expected.len()))
                    .find(|i| lines.get(*i) != expected.get(*i))
                    .unwrap();
                panic!(
                    "{id}/{} line {mismatch}: {:?} != {:?} ({} vs {} lines)",
                    e["query"],
                    lines.get(mismatch),
                    expected.get(mismatch),
                    lines.len(),
                    expected.len()
                );
            }
            let document = &result["document"];
            assert_eq!(document["allocation_index"], e["index"]);
            assert_eq!(document["selection"], e["how"]);
            let explanations: Vec<_> = document["explanations"]
                .as_array()
                .unwrap()
                .iter()
                .map(|v| json!({"class": v["class"], "vreg": v["vreg"], "lines": v["lines"]}))
                .collect();
            assert_eq!(
                json!(explanations),
                e["explanations"],
                "{id}/{}",
                e["query"]
            );
        }
    }
}

/// An edit as the colouring commands spelled it (`43:before:32`, `32:+1`), as the methods take it.
fn edit(kind: &str, text: &str) -> Value {
    let parts: Vec<&str> = text.split(':').collect();
    let number = |s: &str| s.trim_start_matches('+').parse::<i64>().unwrap();
    match kind {
        "move" => json!(["move", number(parts[0]), parts[1], number(parts[2])]),
        _ => json!([kind, number(parts[0]), number(parts[1])]),
    }
}

#[test]
fn colouring_methods_keep_the_commands_results_and_remap_reports_versioned_uncertainty() {
    let session = session();
    let cases: Value = serde_json::from_slice(
        &fs::read(root().join("tests/fixtures/colouring/cli.json")).unwrap(),
    )
    .unwrap();
    for case in cases.as_array().unwrap() {
        let args: Vec<&str> = case["args"]
            .as_array()
            .unwrap()
            .iter()
            .map(|v| v.as_str().unwrap())
            .collect();
        let value = |name: &str| {
            args.iter()
                .position(|a| *a == name)
                .map(|i| args[i + 1].to_owned())
        };
        let loads: Vec<_> = args
            .iter()
            .enumerate()
            .filter(|(_, a)| **a == "--load")
            .map(|(i, _)| args[i + 1])
            .collect();
        let mut traces = vec![];
        for load in &loads {
            let mut params = json!({"load": root().join(load), "version": value("--version")});
            if let Some(asm) = value("--target-asm") {
                params["target_asm"] = json!(root().join(asm));
            }
            if loads.len() > 1 {
                params["version"] = Value::Null;
            }
            traces.push(one(&session, "trace.load", params).unwrap()["trace"].clone());
        }
        let function = value("--function");
        let mut params = json!({"trace": traces[0], "function": function, "text": true});
        let method = match args[0] {
            "replay" => {
                let mut edits = vec![];
                for (i, a) in args.iter().enumerate() {
                    if let Some(kind) = a.strip_prefix("--")
                        && ["move", "degree", "drop", "link", "unlink", "neighbours"]
                            .contains(&kind)
                    {
                        edits.push(edit(kind, args[i + 1]));
                    }
                }
                params["edits"] = json!(edits);
                "trace.replay"
            }
            "search" => {
                params["seed"] = json!(value("--seed"));
                params["iterations"] =
                    json!(value("--iterations").map(|v| v.parse::<u64>().unwrap()));
                params["restarts"] = json!(value("--restarts").map(|v| v.parse::<u64>().unwrap()));
                "trace.search"
            }
            "remap" => {
                params = json!({"first": traces[0], "second": traces[1], "function": function, "text": true});
                "trace.remap"
            }
            other => {
                if other == "verify" {
                    params.as_object_mut().unwrap().remove("wants");
                }
                match other {
                    "verify" => "trace.verify",
                    "target" => "trace.target",
                    "force" => "trace.force",
                    _ => unreachable!(),
                }
            }
        };
        let result = one(&session, method, params).unwrap_or_else(|e| panic!("{args:?}: {e:#}"));
        let actual = &result["result"];
        let text = result["text"]
            .as_array()
            .unwrap()
            .iter()
            .map(|l| format!("{}\n", l.as_str().unwrap()))
            .collect::<String>();
        if args[0] == "remap" {
            assert_eq!(actual["format_version"], 2);
            assert_eq!(actual["elimination"]["state"], "indeterminate");
            assert_eq!(
                actual["elimination"]["reasons"][0]["code"],
                "backend_lifecycle_coverage"
            );
            assert_eq!(actual["paired_instructions"], case["json"]["paired"]);
            assert_eq!(actual["instruction_counts"], case["json"]["instructions"]);
            for (source, target) in actual["inferred_mapping"].as_object().unwrap() {
                assert_eq!(target, &case["json"]["mapping"][source]);
            }
            assert!(text.contains("elimination indeterminate"));
        } else {
            assert_eq!(actual, &case["json"], "{args:?}");
            assert_eq!(
                text,
                case["text"].as_str().unwrap().replace("\r\n", "\n"),
                "{args:?}"
            );
        }
    }
}

#[test]
fn remap_gives_round_selection_and_missing_snapshots_without_false_success() {
    let session = session();
    let saved = fixture("GC_2_7/pipeline");
    let first = one(&session, "trace.load", json!({"load": saved})).unwrap()["trace"].clone();
    let second = one(&session, "trace.load", json!({"load": saved})).unwrap()["trace"].clone();
    let remap = |extra: Value| {
        let mut params = json!({"first": first, "second": second, "function": "pipeline"});
        params
            .as_object_mut()
            .unwrap()
            .extend(extra.as_object().unwrap().clone());
        one(&session, "trace.remap", params).map(|r| r["result"].clone())
    };
    let report = remap(json!({"first_round": 0, "second_round": 0})).unwrap();
    assert_eq!(report["format_version"], 2);
    assert_eq!(report["first"]["class_round"], 0);
    assert!(!report["first"]["nodes"].as_object().unwrap().is_empty());
    assert_eq!(report["elimination"]["state"], "indeterminate");
    let report = remap(json!({"second_snapshot": "after"})).unwrap();
    assert_eq!(report["selection_second"]["phase"], "after");
    assert!(report["second"].is_null() && report["delta"].is_null());
    assert_eq!(report["elimination"]["state"], "indeterminate");
    assert!(
        format!("{:#}", remap(json!({"first_round": 999})).unwrap_err())
            .contains("class round 999 unavailable")
    );
}

#[test]
fn a_saved_manifest_catches_the_wrong_version_and_changed_files() {
    use sha2::{Digest, Sha256};
    let session = session();
    let scratch = tempfile::TempDir::new().unwrap();
    let prefix = scratch.path().join("saved");
    let trace = fs::read(fixture("GC_1_3_2/pipeline.trace")).unwrap();
    let object = fs::read(fixture("GC_1_3_2/pipeline.o")).unwrap();
    fs::write(prefix.with_extension("trace"), &trace).unwrap();
    fs::write(prefix.with_extension("o"), &object).unwrap();
    let meta = json!({"format_version":1,"trace_sha256":format!("{:x}",Sha256::digest(&trace)),"compilation":{"version":"GC_1_3_2","source":"pipeline.cpp","object_sha256":format!("{:x}",Sha256::digest(&object))}});
    fs::write(
        prefix.with_extension("manifest.json"),
        serde_json::to_vec(&meta).unwrap(),
    )
    .unwrap();
    let load = |version: Option<&str>| {
        one(
            &session,
            "trace.load",
            json!({"load": prefix, "version": version}),
        )
    };
    assert!(load(None).is_ok());
    assert!(format!("{:#}", load(Some("GC_2_7")).unwrap_err()).contains("disagrees"));
    fs::write(prefix.with_extension("trace"), b"changed trace").unwrap();
    assert!(format!("{:#}", load(None).unwrap_err()).contains("saved trace differs"));
    fs::write(prefix.with_extension("trace"), &trace).unwrap();
    fs::write(prefix.with_extension("o"), b"changed object").unwrap();
    assert!(format!("{:#}", load(None).unwrap_err()).contains("saved object differs"));
}

#[test]
fn the_api_reference_is_the_schemas() {
    let path = root().join("docs/api.md");
    let document = fs::read_to_string(&path).unwrap().replace("\r\n", "\n");
    let updated = mwcc_methods::reference::updated(&document).expect("docs/api.md's markers");
    if std::env::var_os("MWCC_UPDATE_DOCS").is_some() {
        fs::write(&path, &updated).unwrap();
    }
    assert!(
        updated == document,
        "docs/api.md is stale: run `mwcc-tools methods --markdown --write docs/api.md` (or this test with MWCC_UPDATE_DOCS=1)"
    );
}
