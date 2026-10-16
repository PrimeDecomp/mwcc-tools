use mwcc_oracle_core::{
    runner::{CheckStatus, Compilation, TraceOptions},
    source_guided::state,
    version::Version,
};
use std::{fs, path::PathBuf, process::Command};
fn trace() -> String {
    "pv.function.begin schema=1 function=1 parent=0 c0=4 c1=8 c2=32 c3=32 c4=32 coverage=direct_counter_writes link=pipeline__Fii\npv.category.initial function=1 births=0 i2=31 i3=31 i4=31\npv.category.window function=1 births=0 f2=32 f3=32 f4=32 l2=32 l3=32 l4=32\npv.value.birth schema=1 function=1 value=1 cls=4 register=32 count=33 physical=32 site=400028 expression=0 phase=0 line=3 category=unknown\npv.value.object function=1 value=1 object=00000001 info=123 part=1 datatype=1 storage=0 flags_before=0 type_kind=1 type_size=4 name_known=1 name_complete=1 name_hex=666c6167\npv.graph.begin schema=1 function=1 graph=1 births=1 cls=4 physical=32 start=32 count=33 phase=3 boundary=before_simplify category_ready=1 initial_last=31 first=32 last=32\npv.graph.node v=32 visible=0 deg=0 flags=0 color=-1 n=\npv.graph.end function=1 graph=1 nodes=1 instructions=0 complete=1\npv.function.end function=1 births=1 graphs=1 ranges=2 c0=4 c1=8 c2=32 c3=32 c4=33\n".into()
}
#[test]
fn retained_evaluation_checks_hashes_stock_semantics_and_explicit_baseline_triplet() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    let source = root.join("model.cpp");
    fs::write(&source, b"model").unwrap();
    let object = root.join("model.o");
    let bytes = fs::read(
        PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../../tests/fixtures/oracle/GC_2_7/pipeline.o"),
    )
    .unwrap();
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
        trace: trace(),
        trace_present: true,
        original_fallback: false,
        object: vec![],
    };
    let capture = root.join("capture.json");
    let plain = root.join("plain.json");
    fs::write(
        &capture,
        serde_json::to_vec(&serde_json::json!({"compilation":compilation})).unwrap(),
    )
    .unwrap();
    fs::write(&plain, serde_json::to_vec(&compilation).unwrap()).unwrap();
    let mut contract = serde_json::json!({"format_version":1,"selection":{"link":"pipeline__Fii","class":4,"round":0},"objective":{"kind":"range_category","value":{"name":"flag","part":"single","type_kind":1,"type_size":4},"target":"inside_coalescing_window"},"relocations":[],"purpose":"mechanism_reproducer"});
    let specification = root.join("contract.json");
    let invoke = |capture: &std::path::Path, options: &[&str]| {
        Command::new(env!("CARGO_BIN_EXE_mwcc-tools"))
            .args(["source-search", "evaluate"])
            .arg(&specification)
            .arg(capture)
            .arg("--source")
            .arg(&source)
            .arg("--object")
            .arg(&object)
            .args(options)
            .output()
            .unwrap()
    };
    let read = |output: std::process::Output| -> serde_json::Value {
        assert!(
            output.status.success(),
            "{}",
            String::from_utf8_lossy(&output.stderr)
        );
        serde_json::from_slice(&output.stdout).unwrap()
    };
    fs::write(&specification, serde_json::to_vec(&contract).unwrap()).unwrap();
    for file in [&capture, &plain] {
        let result = read(invoke(file, &[]));
        assert_eq!(result["overall"]["state"], "satisfied");
        assert!(result["graph_changes"]["provenance_correspondence"].is_object());
    }
    let result = read(invoke(
        &capture,
        &[
            "--baseline-capture",
            plain.to_str().unwrap(),
            "--baseline-source",
            source.to_str().unwrap(),
            "--baseline-object",
            object.to_str().unwrap(),
        ],
    ));
    assert_eq!(result["overall"]["state"], "satisfied");
    let incomplete = invoke(&capture, &["--baseline-capture", plain.to_str().unwrap()]);
    assert!(!incomplete.status.success());
    assert!(incomplete.stdout.is_empty());
    let mut altered = serde_json::to_value(&compilation).unwrap();
    altered["object_sha256"] = "wrong".into();
    fs::write(&plain, serde_json::to_vec(&altered).unwrap()).unwrap();
    let result = read(invoke(&plain, &[]));
    assert_eq!(
        result["constraints"]["captured_object"]["state"],
        "indeterminate"
    );
    altered = serde_json::to_value(&compilation).unwrap();
    altered["trace"] = trace().replace("complete=1", "complete=0").into();
    fs::write(&plain, serde_json::to_vec(&altered).unwrap()).unwrap();
    assert_eq!(
        read(invoke(&plain, &[]))["overall"]["state"],
        "indeterminate"
    );
    altered = serde_json::to_value(&compilation).unwrap();
    altered["identical"] = false.into();
    fs::write(&plain, serde_json::to_vec(&altered).unwrap()).unwrap();
    assert_eq!(
        read(invoke(&plain, &[]))["constraints"]["stock_equivalence"]["state"],
        "unsatisfied"
    );
    contract["objective"]["target"] = "before_coalescing_window".into();
    fs::write(&specification, serde_json::to_vec(&contract).unwrap()).unwrap();
    assert_eq!(
        read(invoke(&capture, &[]))["overall"]["state"],
        "unsatisfied"
    );
    contract["objective"]["target"] = "inside_coalescing_window".into();
    contract["purpose"] = "source_transformation".into();
    fs::write(&specification, serde_json::to_vec(&contract).unwrap()).unwrap();
    let result = read(invoke(&capture, &[]));
    assert_eq!(result["overall"]["state"], "indeterminate");
    assert_eq!(
        result["constraints"]["semantic_check"]["state"],
        "indeterminate"
    );
}
