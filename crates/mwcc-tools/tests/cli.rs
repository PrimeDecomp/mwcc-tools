use serde_json::Value;
use std::{fs, path::PathBuf, process::Command};

fn fixture(name: &str) -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../tests/fixtures/oracle")
        .join(name)
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
    for version in ["GC_1_3", "GC_1_3_2", "GC_2_6", "GC_2_7"] {
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
            &["--rust-hooks"][..],
            &["--producer", "rust", "--producer", "c"][..],
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
fn bad_cli_fails_on_stderr() {
    let binary = env!("CARGO_BIN_EXE_mwcc-tools");
    for args in [
        vec!["corpus", "--jobs", "0"],
        vec!["oracle", "analyze-trace"],
        vec!["colouring", "verify"],
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
    assert_eq!(stdout.matches("FAILED").count(), 9, "{stdout}");
    assert_eq!(
        stdout.matches("deliberate Ninja failure").count(),
        9,
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
