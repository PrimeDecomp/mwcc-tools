use std::{fs, process::Command};

#[test]
fn origin_inventory_has_native_json_and_validates_options() {
    let directory = tempfile::tempdir().unwrap();
    fs::create_dir(directory.path().join("entries")).unwrap();
    let invoke = |options: &[&str]| {
        Command::new(env!("CARGO_BIN_EXE_mwcc-tools"))
            .args(["source-index", "origins"])
            .args(options)
            .output()
            .unwrap()
    };
    let path = directory.path().to_str().unwrap();
    let output = invoke(&[
        "--index",
        path,
        "--version",
        "GC_2_7",
        "--link",
        "example__Fv",
    ]);
    assert!(output.status.success());
    let inventory: serde_json::Value = serde_json::from_slice(&output.stdout).unwrap();
    assert_eq!(inventory["format_version"], 1);
    assert_eq!(inventory["scanned_entries"], 0);
    assert!(inventory["entries"].as_array().unwrap().is_empty());
    for options in [
        vec![],
        vec!["--index"],
        vec!["--index", path, "--version", "bogus"],
        vec!["--index", path, "--bogus", "value"],
    ] {
        let output = invoke(&options);
        assert!(!output.status.success());
        assert!(output.stdout.is_empty());
        assert!(!output.stderr.is_empty());
    }
}
