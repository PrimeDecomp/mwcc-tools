use serde_json::Value;
use std::{fs, path::PathBuf, process::Command};

fn fixture(name: &str) -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../tests/fixtures/oracle")
        .join(name)
}

#[test]
fn provenance_inspect_accepts_capture_json_and_keeps_coverage_explicit() {
    let scratch = tempfile::tempdir().unwrap();
    let path = scratch.path().join("capture.json");
    let trace = "pv.coalesce.begin schema=1 pass=1 cls=4 count=40 physical=32 first=32 last=40 special=1 creation=missing link=test__Fv\npv.coalesce.candidate pass=1 candidate=1 instruction=100 destination=35 source=36\npv.coalesce.check pass=1 candidate=1 condition=copy_flag site=10f taken=1 eflags=0\npv.coalesce.end pass=1 candidates=1\n";
    fs::write(
        &path,
        serde_json::to_vec(&serde_json::json!({"compilation":{"trace":trace},"trace":"unrelated fallback","report":{"ignored":[1,{"trace":"not compiler evidence"}]}})).unwrap(),
    )
    .unwrap();
    let run = |extra: &[&str]| {
        Command::new(env!("CARGO_BIN_EXE_mwcc-tools"))
            .args(["provenance", "inspect"])
            .arg(&path)
            .args(extra)
            .output()
            .unwrap()
    };
    let output = run(&["--json", "--link=test__Fv"]);
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let value: Value = serde_json::from_slice(&output.stdout).unwrap();
    assert_eq!(value["format_version"], 2);
    assert_eq!(value["decisions"]["state"], "satisfied");
    assert_eq!(value["lifecycle"]["state"], "indeterminate");
    assert!(!run(&["--link", "absent"]).status.success());
    fs::write(&path, "ra.before function=old link=old\nra.end\n").unwrap();
    let old: Value = serde_json::from_slice(&run(&["--json"]).stdout).unwrap();
    assert_eq!(old["decisions"]["state"], "indeterminate");
    fs::write(&path, trace.replace("eflags=0", "eflags=40")).unwrap();
    let bad = run(&["--json"]);
    assert!(!bad.status.success());
    assert!(bad.stdout.is_empty());
}

#[test]
fn provenance_link_filter_keeps_graphs_and_function_coverage_in_the_same_view() {
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
    let output = Command::new(env!("CARGO_BIN_EXE_mwcc-tools"))
        .args(["provenance", "inspect"])
        .arg(path)
        .args(["--link", "second", "--json"])
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let report: Value = serde_json::from_slice(&output.stdout).unwrap();
    assert_eq!(report["history"]["functions"].as_array().unwrap().len(), 1);
    for collection in ["graphs", "functions"] {
        let entries = report["graphs"][collection].as_array().unwrap();
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0]["link"], "second");
    }
    assert_eq!(report["graphs"]["observations"]["state"], "satisfied");
}

#[test]
fn configure_defaults_to_rust_and_preserves_the_explicit_c_rollback() {
    let source = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..");
    let scratch = tempfile::tempdir().unwrap();
    let root = scratch.path();
    fs::create_dir(root.join("config")).unwrap();
    fs::copy(
        source.join("config/sources.json"),
        root.join("config/sources.json"),
    )
    .unwrap();
    for version in mwcc_oracle_core::build_config::VERSIONS {
        let directory = root.join("config").join(version);
        fs::create_dir_all(&directory).unwrap();
        fs::copy(
            source.join("config").join(version).join("config.json"),
            directory.join("config.json"),
        )
        .unwrap();
    }
    for version in ["GC_1_3", "GC_1_3_2", "GC_2_7"] {
        let directory = root.join("config").join(version);
        fs::create_dir_all(&directory).unwrap();
        for file in ["config.json", "splits.txt"] {
            fs::copy(
                source.join("config").join(version).join(file),
                directory.join(file),
            )
            .unwrap();
        }
        let configure = |extra: &[&str]| {
            Command::new(env!("CARGO_BIN_EXE_mwcc-tools"))
                .args(["configure", "--version", version, "--oracle", "--root"])
                .arg(root)
                .args(extra)
                .output()
                .unwrap()
        };
        let rust = configure(&[]);
        assert!(
            rust.status.success(),
            "{}",
            String::from_utf8_lossy(&rust.stderr)
        );
        let ninja = fs::read_to_string(root.join("build.ninja")).unwrap();
        assert!(ninja.contains("--oracle --producer rust"));
        assert!(ninja.contains("oracle/compiled/rust/hooks.obj"));
        assert!(ninja.contains("oracle/compiled/rust/frontend.obj"));
        assert!(!ninja.contains("compiled/src/oracle/Hooks.c.obj"));
        assert!(!ninja.contains("OracleTrace.obj: mwcc"));
        let c = configure(&["--producer=c"]);
        assert!(c.status.success(), "{}", String::from_utf8_lossy(&c.stderr));
        let fallback = fs::read_to_string(root.join("build.ninja")).unwrap();
        assert!(fallback.contains("--oracle --producer c"));
        assert!(fallback.contains("compiled/src/oracle/Hooks.c.obj"));
        assert!(fallback.contains("OracleTrace.obj: mwcc"));
        assert!(!fallback.contains("oracle/compiled/rust/"));
        for extra in [
            &["--producer", "unknown"][..],
            &["--rust-hooks", "--producer", "c"][..],
            &["--mode", "compare", "--producer", "c"][..],
        ] {
            assert!(!configure(extra).status.success());
            assert_eq!(
                fs::read_to_string(root.join("build.ninja")).unwrap(),
                fallback
            );
        }
    }
    let output = Command::new(env!("CARGO_BIN_EXE_mwcc-tools"))
        .args(["configure", "--producer", "rust", "--root"])
        .arg(root)
        .output()
        .unwrap();
    assert!(!output.status.success());
    assert!(String::from_utf8_lossy(&output.stderr).contains("--producer requires --oracle"));
}

#[test]
fn offline_json_and_saved_colourings_work_without_compilers() {
    let binary = env!("CARGO_BIN_EXE_mwcc-tools");
    let output = Command::new(binary)
        .args(["oracle", "analyze-trace"])
        .arg(fixture("GC_1_3_2/pipeline.trace"))
        .args([
            "--version",
            "GC_1_3_2",
            "--function=pipeline",
            "--json",
            "--raw",
        ])
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert!(output.stderr.is_empty());
    let document: Value = serde_json::from_slice(&output.stdout).unwrap();
    assert_eq!(document["schema_version"], 6);
    assert_eq!(document["identical"], Value::Null);
    assert_eq!(document["functions"].as_array().unwrap().len(), 1);
    let expected: Value =
        serde_json::from_slice(&fs::read(fixture("GC_1_3_2/pipeline.views.json")).unwrap())
            .unwrap();
    assert_eq!(document["raw"], expected["raw_selected"]["pipeline"]);
    let output = Command::new(binary)
        .args(["colouring", "verify", "--load"])
        .arg(fixture("GC_1_3_2/pipeline"))
        .arg("--json")
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let result: Value = serde_json::from_slice(&output.stdout).unwrap();
    assert_eq!(result["ok"], true);
    let expected: Value =
        serde_json::from_slice(&fs::read(fixture("GC_1_3_2/pipeline.colouring.json")).unwrap())
            .unwrap();
    assert_eq!(result["functions"], expected);
}

#[test]
fn bad_cli_fails_on_stderr() {
    let binary = env!("CARGO_BIN_EXE_mwcc-tools");
    for args in [
        vec!["oracle", "analyze-trace"],
        vec!["oracle", "--version", "bad", "file.cpp"],
        vec!["corpus", "--jobs", "0"],
        vec!["download", "pro4"],
        vec!["download", "pro4", "out", "--timeout=0"],
        vec!["download", "pro4", "out", "--timeout=NaN"],
        vec!["configure", "--timeout=1"],
        vec!["verify", "--jobs=0"],
        vec!["gates", "--timeout=-1"],
        vec!["build-hooks", "out", "--frontend-version"],
        vec!["build-hooks", "out", "--frontend-version", "GC_1_2_5"],
        vec![
            "build-hooks",
            "out",
            "--trace-shim",
            "--frontend-version",
            "GC_1_3",
        ],
    ] {
        let result = Command::new(binary).args(args).output().unwrap();
        assert!(!result.status.success());
        assert!(result.stdout.is_empty());
        assert!(!result.stderr.is_empty());
    }
}

#[test]
fn native_download_cli_resolves_the_root_without_fetching_cached_artifacts() {
    let scratch = tempfile::tempdir().unwrap();
    fs::write(scratch.path().join("tool"), b"cached tool").unwrap();
    let output = Command::new(env!("CARGO_BIN_EXE_mwcc-tools"))
        .args([
            "download",
            "compilers",
            "tool",
            "declared-output",
            "--timeout=1",
            "--root",
        ])
        .arg(scratch.path())
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert!(output.stdout.is_empty());
    assert!(output.stderr.is_empty());
    assert_eq!(
        fs::read(scratch.path().join("tool")).unwrap(),
        b"cached tool"
    );
}

#[test]
fn verification_cli_rejects_failed_ninja_even_when_old_success_markers_exist() {
    let scratch = tempfile::tempdir().unwrap();
    fs::create_dir_all(scratch.path().join("config")).unwrap();
    fs::write(scratch.path().join("config/sources.json"), b"{}").unwrap();
    for version in mwcc_oracle_core::build_config::VERSIONS {
        fs::create_dir_all(scratch.path().join(format!("config/{version}"))).unwrap();
        fs::create_dir_all(scratch.path().join(format!("build/{version}"))).unwrap();
        fs::write(
            scratch.path().join(format!("config/{version}/config.json")),
            br#"{"original":"missing.exe","mode":"compare"}"#,
        )
        .unwrap();
        fs::write(
            scratch.path().join(format!("build/{version}/ok")),
            b"STALE success",
        )
        .unwrap();
    }
    let ninja = scratch.path().join(if cfg!(windows) {
        "failed-ninja.cmd"
    } else {
        "failed-ninja"
    });
    fs::write(
        &ninja,
        if cfg!(windows) {
            "@echo off\r\necho deliberate Ninja failure\r\nexit /b 3\r\n"
        } else {
            "#!/bin/sh\necho deliberate Ninja failure\nexit 3\n"
        },
    )
    .unwrap();
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(&ninja, fs::Permissions::from_mode(0o755)).unwrap();
    }
    let output = Command::new(env!("CARGO_BIN_EXE_mwcc-tools"))
        .args(["verify", "--root"])
        .arg(scratch.path())
        .arg("--ninja")
        .arg(ninja)
        .output()
        .unwrap();
    assert!(!output.status.success());
    let stdout = String::from_utf8(output.stdout).unwrap();
    assert_eq!(stdout.matches("FAILED").count(), 8, "{stdout}");
    assert_eq!(
        stdout.matches("deliberate Ninja failure").count(),
        8,
        "{stdout}"
    );
    assert!(!stdout.contains("STALE success"));
    assert!(
        fs::read_to_string(scratch.path().join("build.ninja"))
            .unwrap()
            .contains("version = GC_1_2_5")
    );
}

#[test]
fn binary_comparison_cli_keeps_mismatch_exit_and_artifacts() {
    let fixtures = fixture("").parent().unwrap().join("compare");
    let metadata: Value =
        serde_json::from_slice(&fs::read(fixtures.join("project.json")).unwrap()).unwrap();
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    fs::create_dir_all(root.join("config/FIXTURE")).unwrap();
    fs::create_dir_all(root.join("build/FIXTURE/compiled/src")).unwrap();
    for name in ["config", "functions", "bindings"] {
        fs::write(
            root.join(format!("config/FIXTURE/{name}.json")),
            serde_json::to_vec(&metadata[name]).unwrap(),
        )
        .unwrap();
    }
    fs::write(
        root.join("config/sources.json"),
        serde_json::to_vec(&metadata["sources"]).unwrap(),
    )
    .unwrap();
    fs::copy(fixtures.join("project.pe"), root.join("original.exe")).unwrap();
    for label in ["matched", "nonmatching"] {
        fs::copy(
            fixtures.join(format!("project-{label}.obj")),
            root.join(format!("build/FIXTURE/compiled/src/{label}.cpp.obj")),
        )
        .unwrap();
    }
    let binary = env!("CARGO_BIN_EXE_mwcc-tools");
    for action in ["extract", "compare"] {
        let result = Command::new(binary)
            .args(["compare", action, "FIXTURE", "--root=."])
            .current_dir(root)
            .output()
            .unwrap();
        assert!(
            result.status.success(),
            "{}",
            String::from_utf8_lossy(&result.stderr)
        );
        assert!(result.stderr.is_empty());
        if action == "compare" {
            assert_eq!(
                String::from_utf8(result.stdout)
                    .unwrap()
                    .replace("\r\n", "\n"),
                metadata["completion"].as_str().unwrap()
            );
        }
    }
    fs::remove_file(root.join("build/FIXTURE/ok")).unwrap();
    let mut config = metadata["config"].clone();
    config["matching"] = serde_json::json!(["src/matched.cpp", "src/nonmatching.cpp"]);
    fs::write(
        root.join("config/FIXTURE/config.json"),
        serde_json::to_vec(&config).unwrap(),
    )
    .unwrap();
    let result = Command::new(binary)
        .args(["compare", "compare", "FIXTURE"])
        .current_dir(root)
        .output()
        .unwrap();
    assert_eq!(result.status.code(), Some(1));
    assert!(result.stderr.is_empty());
    assert!(String::from_utf8_lossy(&result.stdout).contains("  not exact: src/nonmatching/b"));
    assert!(root.join("objdiff.json").is_file());
    assert!(!root.join("build/FIXTURE/ok").exists());
}

#[test]
fn offline_regdiff_matches_archived_helper() {
    let root = fixture("");
    let manifest: Value =
        serde_json::from_slice(&fs::read(root.join("manifest.json")).unwrap()).unwrap();
    let binary = env!("CARGO_BIN_EXE_mwcc-tools");
    for case in manifest["cases"].as_array().unwrap() {
        let id = case["id"].as_str().unwrap();
        let cases: Vec<Value> =
            serde_json::from_slice(&fs::read(root.join(format!("{id}.regdiff.json"))).unwrap())
                .unwrap();
        for e in cases {
            let common = vec![
                "oracle".to_owned(),
                "regdiff".into(),
                "--trace".into(),
                root.join(format!("{id}.trace"))
                    .to_string_lossy()
                    .into_owned(),
                "--diff-json".into(),
                root.join(format!("{id}.regdiff-input.json"))
                    .to_string_lossy()
                    .into_owned(),
                "--version".into(),
                case["version"].as_str().unwrap().into(),
                "--function".into(),
                e["query"].as_str().unwrap().into(),
                "--full".into(),
                "--pcode".into(),
                "--inlining".into(),
            ];
            let output = Command::new(binary).args(&common).output().unwrap();
            assert!(
                output.status.success(),
                "{}",
                String::from_utf8_lossy(&output.stderr)
            );
            let lines: Vec<_> = String::from_utf8(output.stdout)
                .unwrap()
                .lines()
                .map(str::to_owned)
                .collect();
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
            let output = Command::new(binary)
                .args(&common)
                .arg("--json")
                .output()
                .unwrap();
            assert!(
                output.status.success(),
                "{}",
                String::from_utf8_lossy(&output.stderr)
            );
            let document: Value = serde_json::from_slice(&output.stdout).unwrap();
            assert_eq!(document["allocation_index"], e["index"]);
            assert_eq!(document["selection"], e["how"]);
            let explanations: Vec<_> = document["explanations"]
                .as_array()
                .unwrap()
                .iter()
                .map(
                    |v| serde_json::json!({"class":v["class"],"vreg":v["vreg"],"lines":v["lines"]}),
                )
                .collect();
            assert_eq!(
                serde_json::json!(explanations),
                e["explanations"],
                "{id}/{}",
                e["query"]
            );
        }
    }
}

#[test]
fn colouring_commands_preserve_behavior_and_remap_reports_versioned_uncertainty() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..");
    let cases: Value =
        serde_json::from_slice(&fs::read(root.join("tests/fixtures/colouring/cli.json")).unwrap())
            .unwrap();
    for case in cases.as_array().unwrap() {
        let args: Vec<_> = case["args"]
            .as_array()
            .unwrap()
            .iter()
            .map(|v| v.as_str().unwrap())
            .collect();
        for json_output in [true, false] {
            let mut command = Command::new(env!("CARGO_BIN_EXE_mwcc-tools"));
            command.current_dir(&root).arg("colouring").args(&args);
            if json_output {
                command.arg("--json");
            }
            let output = command.output().unwrap();
            assert_eq!(
                output.status.code(),
                Some(case["exit_code"].as_i64().unwrap() as i32),
                "{args:?}: {}",
                String::from_utf8_lossy(&output.stderr)
            );
            if json_output {
                let actual: Value = serde_json::from_slice(&output.stdout).unwrap();
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
                } else {
                    assert_eq!(actual, case["json"], "{args:?}");
                }
            } else {
                if args[0] == "remap" {
                    assert!(
                        String::from_utf8(output.stdout)
                            .unwrap()
                            .contains("elimination indeterminate")
                    );
                    continue;
                }
                assert_eq!(
                    String::from_utf8(output.stdout)
                        .unwrap()
                        .replace("\r\n", "\n"),
                    case["text"].as_str().unwrap().replace("\r\n", "\n"),
                    "{args:?}"
                );
            }
        }
    }
}

#[test]
fn remap_exposes_round_selection_and_missing_snapshots_without_false_success() {
    let saved = fixture("GC_2_7/pipeline");
    let run = |extra: &[&str]| {
        Command::new(env!("CARGO_BIN_EXE_mwcc-tools"))
            .args(["colouring", "remap", "--load"])
            .arg(&saved)
            .arg("--load")
            .arg(&saved)
            .args(["--function", "pipeline", "--json"])
            .args(extra)
            .output()
            .unwrap()
    };
    let output = run(&["--first-round=0", "--second-round=0"]);
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let report: Value = serde_json::from_slice(&output.stdout).unwrap();
    assert_eq!(report["format_version"], 2);
    assert_eq!(report["first"]["class_round"], 0);
    assert!(!report["first"]["nodes"].as_object().unwrap().is_empty());
    assert_eq!(report["elimination"]["state"], "indeterminate");
    let output = run(&["--second-snapshot=after"]);
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let report: Value = serde_json::from_slice(&output.stdout).unwrap();
    assert_eq!(report["selection_second"]["phase"], "after");
    assert!(report["second"].is_null() && report["delta"].is_null());
    assert_eq!(report["elimination"]["state"], "indeterminate");
    let output = run(&["--first-round", "999"]);
    assert!(!output.status.success() && output.stdout.is_empty());
    assert!(String::from_utf8_lossy(&output.stderr).contains("class round 999 unavailable"));
}

#[test]
fn saved_manifest_detects_wrong_version_and_changed_inputs() {
    use sha2::{Digest, Sha256};
    let scratch = tempfile::TempDir::new().unwrap();
    let prefix = scratch.path().join("saved");
    let trace = fs::read(fixture("GC_1_3_2/pipeline.trace")).unwrap();
    let object = fs::read(fixture("GC_1_3_2/pipeline.o")).unwrap();
    fs::write(prefix.with_extension("trace"), &trace).unwrap();
    fs::write(prefix.with_extension("o"), &object).unwrap();
    let meta = serde_json::json!({"format_version":1,"trace_sha256":format!("{:x}",Sha256::digest(&trace)),"compilation":{"version":"GC_1_3_2","source":"pipeline.cpp","object_sha256":format!("{:x}",Sha256::digest(&object))}});
    fs::write(
        prefix.with_extension("manifest.json"),
        serde_json::to_vec(&meta).unwrap(),
    )
    .unwrap();
    let run = |version: Option<&str>| {
        let mut command = Command::new(env!("CARGO_BIN_EXE_mwcc-tools"));
        command
            .args(["colouring", "verify", "--json", "--load"])
            .arg(&prefix);
        if let Some(v) = version {
            command.args(["--version", v]);
        }
        command.output().unwrap()
    };
    assert!(run(None).status.success());
    let mismatch = run(Some("GC_2_7"));
    assert!(!mismatch.status.success());
    assert!(String::from_utf8_lossy(&mismatch.stderr).contains("--version disagrees"));
    fs::write(prefix.with_extension("trace"), b"changed trace").unwrap();
    let modified = run(None);
    assert!(!modified.status.success());
    assert!(String::from_utf8_lossy(&modified.stderr).contains("saved trace differs"));
    fs::write(prefix.with_extension("trace"), &trace).unwrap();
    fs::write(prefix.with_extension("o"), b"changed object").unwrap();
    let modified = run(None);
    assert!(!modified.status.success());
    assert!(String::from_utf8_lossy(&modified.stderr).contains("saved object differs"));
}
