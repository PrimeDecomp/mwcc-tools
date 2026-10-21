mod common;
use mwcc_oracle_core::{
    build_config,
    build_verify::{self, Backend, CommandResult, Mode, Request},
};
use serde_json::{Value, json};
use sha2::{Digest, Sha256};
use std::{
    fs,
    path::{Path, PathBuf},
    time::Duration,
};
fn fixtures() -> PathBuf {
    common::fixtures("build-verify")
}
fn cases() -> Vec<Value> {
    serde_json::from_slice(&fs::read(fixtures().join("cases.json")).unwrap()).unwrap()
}
fn stage(case: &Value) -> tempfile::TempDir {
    let root = tempfile::tempdir().unwrap();
    for version in build_config::VERSIONS {
        let config = root.path().join(format!("config/{version}"));
        let build = root.path().join(format!("build/{version}"));
        fs::create_dir_all(&config).unwrap();
        fs::create_dir_all(&build).unwrap();
        for (name, value) in [
            ("config", &case["config"]),
            ("functions", &case["functions"]),
        ] {
            fs::write(
                config.join(format!("{name}.json")),
                serde_json::to_vec(value).unwrap(),
            )
            .unwrap();
        }
        fs::write(
            build.join("report.json"),
            serde_json::to_vec(&case["report"]).unwrap(),
        )
        .unwrap();
        fs::write(
            build.join("ok"),
            format!("{version} synthetic completion\n"),
        )
        .unwrap();
        if version != "GC_1_0" {
            fs::write(config.join("splits.txt"), "fixture splits\n").unwrap();
        }
    }
    root
}
#[derive(Default)]
struct FakeBuild {
    name: String,
    active: String,
    calls: Vec<Value>,
    primary_failures: usize,
}
impl Backend for FakeBuild {
    fn configure(&mut self, _: &Path, request: &build_config::Request) -> anyhow::Result<()> {
        self.active = request.version.clone();
        self.calls
            .push(json!({"command":"configure", "version":self.active}));
        if self.active == build_config::PRIMARY && self.primary_failures > 0 {
            self.primary_failures -= 1;
            anyhow::bail!("synthetic configuration failure");
        }
        Ok(())
    }
    fn ninja(&mut self, _: &Request, targets: &[String]) -> anyhow::Result<CommandResult> {
        if self.name == "fatal_termination" {
            return Err(mwcc_oracle_core::process_tree::TerminationFailed(
                "synthetic tree termination failure".into(),
            )
            .into());
        }
        self.calls
            .push(json!({"command":"ninja", "version":self.active,"targets":targets}));
        let failed = self.active == "GC_1_1"
            && ((self.name == "failed_build" && targets.is_empty())
                || (self.name == "failed_progress" && targets == ["progress"]));
        Ok(CommandResult {
            success: !failed,
            output: if failed {
                "synthetic Ninja failure\n".into()
            } else {
                String::new()
            },
        })
    }
}
fn request(root: &Path, mode: Mode) -> Request {
    Request {
        root: root.to_owned(),
        mode,
        tools: "mwcc-tools".into(),
        ninja: "ninja".into(),
        jobs: None,
        timeout: Duration::from_secs(10),
    }
}
#[test]
fn verification_reports_and_sequential_calls_match_python() {
    let metadata: Value =
        serde_json::from_slice(&fs::read(fixtures().join("manifest.json")).unwrap()).unwrap();
    assert_eq!(
        metadata["reference_commit"],
        "b75c4e5e0b4bc97ae06c26ea9d6aad8379ac1722"
    );
    for (name, hash) in metadata["files"].as_object().unwrap() {
        assert_eq!(
            format!(
                "{:x}",
                Sha256::digest(fs::read(fixtures().join(name)).unwrap())
            ),
            hash.as_str().unwrap()
        );
    }
    for case in cases() {
        let root = stage(&case);
        let mut backend = FakeBuild {
            name: case["name"].as_str().unwrap().into(),
            ..Default::default()
        };
        let mut text = String::new();
        let report =
            build_verify::run_with(&request(root.path(), Mode::Verify), &mut backend, |step| {
                text.push_str(&step.text);
                text.push('\n');
            })
            .unwrap();
        assert_eq!(report.passed, case["exit_code"] == 0, "{}", backend.name);
        assert_eq!(json!(backend.calls), case["calls"], "{}", backend.name);
        assert_eq!(backend.active, build_config::PRIMARY);
        assert!(report.restoration_error.is_none());
        if ["matching", "differing"].contains(&backend.name.as_str()) {
            assert_eq!(text, case["stdout"], "{}", backend.name);
        } else {
            let failed = report.steps.iter().find(|s| !s.passed).unwrap();
            assert_eq!(failed.label, "GC_1_1");
            assert!(failed.text.contains("synthetic Ninja failure"));
            assert!(!failed.text.contains("synthetic completion"));
            assert_eq!(report.steps.iter().filter(|s| !s.passed).count(), 1);
        }
    }
}
#[test]
fn gates_preserve_the_full_matrix_and_restore_the_primary_configuration() {
    let steps = build_verify::plan(Mode::Gates);
    assert_eq!(
        steps.iter().map(|s| s.label()).collect::<Vec<_>>(),
        [
            "GC_1_1",
            "GC_1_1p1",
            "GC_1_2_5",
            "GC_1_2_5n",
            "GC_1_0",
            "GC_1_3",
            "GC_1_3 link",
            "GC_1_3_2 link",
            "GC_2_6 link",
            "GC_2_7 link"
        ]
    );
    assert_eq!(
        steps
            .iter()
            .filter(|s| s.split_only)
            .map(|s| s.version.as_str())
            .collect::<Vec<_>>(),
        ["GC_1_3_2", "GC_2_6", "GC_2_7"]
    );
    for step in &steps[6..] {
        assert_eq!(step.target, Some(format!("build/{}/ok", step.version)));
    }
    let root = stage(&cases()[0]);
    let mut backend = FakeBuild {
        name: "failed_build".into(),
        ..Default::default()
    };
    let report =
        build_verify::run_with(&request(root.path(), Mode::Gates), &mut backend, |_| {}).unwrap();
    assert!(!report.passed);
    assert_eq!(report.steps.len(), 10);
    assert_eq!(backend.active, build_config::PRIMARY);
    assert!(
        backend
            .calls
            .iter()
            .filter(|c| c["command"] == "ninja")
            .all(|c| c["targets"] != json!(["progress"]))
    );
}
#[test]
fn configuration_failure_is_reported_and_does_not_run_ninja_for_a_stale_graph() {
    let root = stage(&cases()[0]);
    let mut backend = FakeBuild {
        primary_failures: 1,
        ..Default::default()
    };
    let report =
        build_verify::run_with(&request(root.path(), Mode::Verify), &mut backend, |_| {}).unwrap();
    assert!(!report.passed);
    assert!(
        report
            .steps
            .last()
            .unwrap()
            .text
            .contains("configuration failure")
    );
    assert_eq!(
        backend
            .calls
            .iter()
            .filter(|c| c["command"] == "ninja" && c["version"] == build_config::PRIMARY)
            .count(),
        0
    );
    assert_eq!(
        backend
            .calls
            .iter()
            .filter(|c| c["command"] == "configure" && c["version"] == build_config::PRIMARY)
            .count(),
        2
    );
    assert!(report.restoration_error.is_none());
}
#[test]
fn report_inspection_preserves_matching_patch_and_missing_measure_rules() {
    let case = &cases()[0];
    let (summary, differences) = build_verify::inspect_report(
        &case["report"],
        &json!({"matching":null}),
        &case["functions"],
    )
    .unwrap();
    assert_eq!(differences, ["unlisted/ignored"]);
    assert_eq!(
        summary,
        "  code 99.99% matched, 84.32% linked; data 12.34% matched, 0.00% linked"
    );
    let (_, differences) =
        build_verify::inspect_report(&case["report"], &json!({"matching":[]}), &case["functions"])
            .unwrap();
    assert!(differences.is_empty());
    let (summary, _) =
        build_verify::inspect_report(&json!({"units":[], "measures":{}}), &json!({}), &json!([]))
            .unwrap();
    assert_eq!(
        summary,
        "  code 0.00% matched, 0.00% linked; data 0.00% matched, 0.00% linked"
    );
    assert!(
        build_verify::inspect_report(
            &json!({"units":[], "measures":{"matched_code_percent":"100"}}),
            &json!({}),
            &json!([])
        )
        .is_err()
    );
}

#[test]
fn failed_process_tree_cleanup_stops_configuration_of_further_versions() {
    let root = stage(&cases()[0]);
    let mut backend = FakeBuild {
        name: "fatal_termination".into(),
        ..Default::default()
    };
    let report =
        build_verify::run_with(&request(root.path(), Mode::Verify), &mut backend, |_| {}).unwrap();
    assert!(!report.passed);
    assert_eq!(report.steps.len(), 1);
    assert!(report.steps[0].text.contains("tree termination failure"));
    assert!(
        report
            .restoration_error
            .unwrap()
            .contains("could not be stopped")
    );
    assert_eq!(backend.calls.len(), 1);
}
