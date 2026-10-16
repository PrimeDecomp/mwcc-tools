use std::{fs, process::Command};
fn trace() -> String {
    "pv.function.begin schema=1 function=1 parent=0 c0=4 c1=8 c2=32 c3=32 c4=32 coverage=direct_counter_writes link=case__Fv\npv.value.birth schema=1 function=1 value=1 cls=4 register=32 count=33 physical=32 site=400028 expression=0 phase=0 line=3 category=unknown\npv.value.object function=1 value=1 object=00000001 info=123 part=1 datatype=1 storage=0 flags_before=0 type_kind=1 type_size=4 name_known=1 name_complete=1 name_hex=666c6167\npv.graph.begin schema=1 function=1 graph=1 births=1 cls=4 physical=32 start=32 count=33 phase=3 boundary=before_simplify\npv.graph.node v=32 visible=0 deg=0 flags=0 color=-1 n=\npv.graph.end function=1 graph=1 nodes=1 instructions=0 complete=1\npv.function.end function=1 births=1 graphs=1 ranges=0 c0=4 c1=8 c2=32 c3=32 c4=33\n".into()
}
#[test]
fn compare_cli_requires_raw_version_checks_capture_identity_and_returns_structured_coverage() {
    let directory = tempfile::tempdir().unwrap();
    let raw = directory.path().join("raw.trace");
    let capture = directory.path().join("capture.json");
    fs::write(&raw, trace()).unwrap();
    fs::write(
        &capture,
        serde_json::to_vec(
            &serde_json::json!({"compilation":{"version":"GC_2_7","trace":trace()}}),
        )
        .unwrap(),
    )
    .unwrap();
    let invoke = |first: &std::path::Path, second: &std::path::Path, options: &[&str]| {
        Command::new(env!("CARGO_BIN_EXE_mwcc-tools"))
            .arg("provenance")
            .arg("compare")
            .arg(first)
            .arg(second)
            .args(["--link", "case__Fv", "--json"])
            .args(options)
            .output()
            .unwrap()
    };
    let missing = invoke(&raw, &capture, &[]);
    assert!(!missing.status.success());
    assert!(String::from_utf8_lossy(&missing.stderr).contains("require --version"));
    for (first, options) in [(&raw, vec!["--version", "GC_2_7"]), (&capture, vec![])] {
        let result = invoke(first, &capture, &options);
        assert!(
            result.status.success(),
            "{}",
            String::from_utf8_lossy(&result.stderr)
        );
        let result: serde_json::Value = serde_json::from_slice(&result.stdout).unwrap();
        assert_eq!(result["format_version"], 1);
        assert_eq!(result["coverage"]["state"], "satisfied");
        assert_eq!(result["mapping"].as_array().unwrap().len(), 1);
        assert_eq!(result["elimination"]["state"], "indeterminate");
    }
    let wrong = invoke(&capture, &capture, &["--version", "GC_1_3"]);
    assert!(!wrong.status.success());
    assert!(String::from_utf8_lossy(&wrong.stderr).contains("disagrees"));
    assert!(
        !invoke(&capture, &capture, &["--first-round", "1"])
            .status
            .success()
    );
    let partial = directory.path().join("partial.trace");
    fs::write(&partial, trace().replace("complete=1", "complete=0")).unwrap();
    let result = invoke(&raw, &partial, &["--version", "GC_2_7"]);
    assert!(result.status.success());
    let result: serde_json::Value = serde_json::from_slice(&result.stdout).unwrap();
    assert_eq!(result["coverage"]["state"], "indeterminate");
    assert!(result["mapping"].as_array().unwrap().is_empty());
}
