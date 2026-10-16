//! Explicit integration gate. Run with MWCC_ORACLE_TEST_ROOT set to a built
//! reference checkout: cargo test --test live -- --ignored
use mwcc_oracle_core::{
    runner::{self, CheckStatus, CompileRequest, TraceOptions},
    trace,
    version::Version,
};
use serde_json::Value;
use std::{env, fs, path::PathBuf, time::Duration};

#[test]
#[ignore = "requires built original/oracle compilers; set MWCC_ORACLE_TEST_ROOT"]
fn original_and_oracle_objects_agree_for_all_synthetic_versions() {
    let root =
        PathBuf::from(env::var_os("MWCC_ORACLE_TEST_ROOT").expect("set MWCC_ORACLE_TEST_ROOT"));
    let fixtures = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../tests/fixtures/oracle")
        .canonicalize()
        .unwrap();
    let manifest: Value =
        serde_json::from_slice(&fs::read(fixtures.join("manifest.json")).unwrap()).unwrap();
    for case in manifest["cases"].as_array().unwrap() {
        let request = CompileRequest {
            root: root.clone(),
            working_directory: fixtures.clone(),
            source: fixtures
                .join(case["source"].as_str().unwrap())
                .display()
                .to_string(),
            flags: case["flags"]
                .as_array()
                .unwrap()
                .iter()
                .map(|v| v.as_str().unwrap().to_owned())
                .collect(),
            version: Version::parse(case["version"].as_str().unwrap()).unwrap(),
            options: TraceOptions {
                backend: false,
                source_arenas: false,
                schedule: true,
                inline_cost: true,
                full_cost: true,
                instantiation: true,
                locals: true,
                blocks: true,
                cse: true,
                stages: false,
            },
            overrides: vec![],
            check: true,
            original_fallback: false,
            cut_limit: None,
            merge_limit: None,
            keep_object: None,
            timeout: Duration::from_secs(60),
        };
        let output = runner::compile(&request).unwrap();
        assert_eq!(output.check_status, CheckStatus::Verified, "{}", case["id"]);
        let is_empty = case["source"].as_str().unwrap().ends_with("empty.c");
        assert_eq!(output.trace_present, !is_empty);
        let parsed = trace::parse(&output.trace).unwrap();
        assert_eq!(parsed.allocations.is_empty(), is_empty);
        let mut unchecked = request;
        unchecked.check = false;
        assert_eq!(
            runner::compile(&unchecked).unwrap().check_status,
            CheckStatus::Skipped
        );
    }
}

#[test]
#[ignore = "requires a generated project build.ninja; set MWCC_NINJA_TEST_FILE"]
fn reads_real_project_compile_rules() {
    let file =
        PathBuf::from(env::var_os("MWCC_NINJA_TEST_FILE").expect("set MWCC_NINJA_TEST_FILE"));
    let rules = mwcc_oracle_core::ninja::parse(&fs::read_to_string(file).unwrap()).unwrap();
    assert!(!rules.is_empty());
    assert!(rules.iter().all(|r| !r.flags.is_empty()));
}

#[test]
#[ignore = "requires built original/oracle compilers; set MWCC_ORACLE_TEST_ROOT"]
fn source_and_header_overrides_are_effective_and_leave_inputs_unchanged() {
    let root =
        PathBuf::from(env::var_os("MWCC_ORACLE_TEST_ROOT").expect("set MWCC_ORACLE_TEST_ROOT"));
    let scratch = tempfile::TempDir::new().unwrap();
    let project = scratch.path().join("project");
    fs::create_dir_all(project.join("include")).unwrap();
    fs::create_dir_all(project.join("src")).unwrap();
    fs::write(
        project.join("include/header.hpp"),
        "inline int value() { return 1; }\n",
    )
    .unwrap();
    fs::write(
        project.join("src/unit.cpp"),
        "#include \"header.hpp\"\nint test() { return value(); }\n",
    )
    .unwrap();
    let header = fs::read(project.join("include/header.hpp")).unwrap();
    let source = fs::read(project.join("src/unit.cpp")).unwrap();
    let replacement = scratch.path().join("replacement.hpp");
    fs::write(&replacement, "inline int value() { return 2; }\n").unwrap();
    let mut request = CompileRequest {
        root,
        working_directory: project.clone(),
        source: "src/unit.cpp".into(),
        flags: vec![
            "-nodefaults".into(),
            "-proc".into(),
            "gekko".into(),
            "-O4,p".into(),
            "-i".into(),
            "include".into(),
        ],
        version: Version::Gc132,
        options: TraceOptions::default(),
        overrides: vec![],
        check: true,
        original_fallback: false,
        cut_limit: None,
        merge_limit: None,
        keep_object: None,
        timeout: Duration::from_secs(60),
    };
    let baseline = runner::compile(&request).unwrap();
    assert_eq!(baseline.check_status, CheckStatus::Verified);
    request
        .overrides
        .push(format!("include/header.hpp={}", replacement.display()));
    let overridden = runner::compile(&request).unwrap();
    assert_eq!(overridden.check_status, CheckStatus::Verified);
    assert_ne!(baseline.object, overridden.object);
    let before = mwcc_oracle_core::elf::Elf::parse(&baseline.object).unwrap();
    let after = mwcc_oracle_core::elf::Elf::parse(&overridden.object).unwrap();
    let before_symbol = before
        .symbols()
        .unwrap()
        .into_iter()
        .find(|s| s.kind == 2 && s.name.starts_with("test"))
        .unwrap();
    let after_symbol = after
        .symbols()
        .unwrap()
        .into_iter()
        .find(|s| s.name == before_symbol.name)
        .unwrap();
    assert_ne!(
        before.function_bytes(&before_symbol).unwrap(),
        after.function_bytes(&after_symbol).unwrap()
    );
    let candidate = scratch.path().join("candidate.cpp");
    fs::write(&candidate, "int different() { return 3; }\n").unwrap();
    request.overrides = vec![format!("src/unit.cpp={}", candidate.display())];
    let overridden = runner::compile(&request).unwrap();
    assert_eq!(overridden.check_status, CheckStatus::Verified);
    assert_ne!(baseline.object, overridden.object);
    assert_eq!(fs::read(project.join("src/unit.cpp")).unwrap(), source);
    assert_eq!(
        fs::read(project.join("include/header.hpp")).unwrap(),
        header
    );
    request.source = "bad.cpp".into();
    fs::write(project.join("bad.cpp"), "int invalid( {\n").unwrap();
    request.overrides.clear();
    assert!(
        runner::compile(&request)
            .unwrap_err()
            .to_string()
            .contains("oracle compiler failed")
    );
}

#[test]
#[ignore = "requires built original/oracle compilers; set MWCC_ORACLE_TEST_ROOT"]
fn unavailable_original_and_stale_oracle_are_distinct() {
    let root =
        PathBuf::from(env::var_os("MWCC_ORACLE_TEST_ROOT").expect("set MWCC_ORACLE_TEST_ROOT"));
    let scratch = tempfile::TempDir::new().unwrap();
    let directory = scratch.path().join("build/GC_1_3_2/oracle");
    fs::create_dir_all(&directory).unwrap();
    fs::copy(
        root.join("build/GC_1_3_2/oracle/mwcceppc.exe"),
        directory.join("mwcceppc.exe"),
    )
    .unwrap();
    fs::copy(
        root.join("build/GC_1_3_2/oracle/lmgr326b.dll"),
        directory.join("lmgr326b.dll"),
    )
    .unwrap();
    fs::write(
        scratch.path().join("function.c"),
        "int result(int x) { return x+1; }\n",
    )
    .unwrap();
    let request = CompileRequest {
        root: scratch.path().to_owned(),
        working_directory: scratch.path().to_owned(),
        source: "function.c".into(),
        flags: vec![
            "-nodefaults".into(),
            "-proc".into(),
            "gekko".into(),
            "-O4,p".into(),
        ],
        version: Version::Gc132,
        options: TraceOptions::default(),
        overrides: vec![],
        check: true,
        original_fallback: false,
        cut_limit: None,
        merge_limit: None,
        keep_object: None,
        timeout: Duration::from_secs(60),
    };
    let result = runner::compile(&request).unwrap();
    assert_eq!(result.check_status, CheckStatus::OriginalUnavailable);
    assert_eq!(result.identical, None);
    fs::copy(
        root.join("build/compilers/GC/1.3.2/mwcceppc.exe"),
        directory.join("mwcceppc.exe"),
    )
    .unwrap();
    let error = runner::compile(&request).unwrap_err();
    assert!(error.to_string().contains("without a trace"), "{error:#}");
    fs::remove_file(directory.join("mwcceppc.exe")).unwrap();
    let original = scratch.path().join("build/compilers/GC/1.3.2");
    fs::create_dir_all(&original).unwrap();
    fs::copy(
        root.join("build/compilers/GC/1.3.2/mwcceppc.exe"),
        original.join("mwcceppc.exe"),
    )
    .unwrap();
    fs::copy(
        directory.join("lmgr326b.dll"),
        original.join("lmgr326b.dll"),
    )
    .unwrap();
    let mut fallback = request;
    fallback.original_fallback = true;
    let result = runner::compile(&fallback).unwrap();
    assert!(result.original_fallback);
    assert!(result.trace.is_empty());
    assert_eq!(result.check_status, CheckStatus::Verified);
}

#[test]
#[ignore = "requires built original compilers; set MWCC_ORACLE_TEST_ROOT"]
fn original_fallback_supports_pre_oracle_versions() {
    let root =
        PathBuf::from(env::var_os("MWCC_ORACLE_TEST_ROOT").expect("set MWCC_ORACLE_TEST_ROOT"));
    let scratch = tempfile::TempDir::new().unwrap();
    fs::write(
        scratch.path().join("fallback.c"),
        "int f(int x) { return x*3+1; }\n",
    )
    .unwrap();
    for version in [
        Version::Gc10,
        Version::Gc11,
        Version::Gc11p1,
        Version::Gc125,
        Version::Gc125n,
    ] {
        let request = CompileRequest {
            root: root.clone(),
            working_directory: scratch.path().into(),
            source: "fallback.c".into(),
            flags: vec![
                "-nodefaults".into(),
                "-proc".into(),
                "gekko".into(),
                "-O4,p".into(),
            ],
            version,
            options: TraceOptions::default(),
            overrides: vec![],
            check: true,
            original_fallback: true,
            cut_limit: None,
            merge_limit: None,
            keep_object: None,
            timeout: Duration::from_secs(60),
        };
        let output = runner::compile(&request).unwrap();
        assert!(output.original_fallback, "{}", version.name());
        assert!(!output.trace_present);
        assert_eq!(
            output.check_status,
            CheckStatus::Verified,
            "{}",
            version.name()
        );
    }
}
