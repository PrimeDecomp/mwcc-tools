//! Integrity fixtures deliberately contain non-executable fake compilers. Export
//! and offline verification must succeed without invoking any retained program.
use anyhow::Result;
use mwcc_oracle_core::{
    runner::{CheckStatus, Compilation, TraceOptions},
    source_guided::{
        inputs, reduction,
        reproducer::{self, RunKind},
        search,
        state::digest,
    },
    version::Version,
};
use serde_json::{Value, json};
use std::{
    fs,
    path::{Path, PathBuf},
};

struct Fixture {
    temp: tempfile::TempDir,
    run: PathBuf,
    project: PathBuf,
    candidate_key: String,
}
fn put(path: &Path, bytes: &[u8]) -> Result<()> {
    fs::create_dir_all(path.parent().unwrap())?;
    fs::write(path, bytes)?;
    Ok(())
}
fn json_file(path: &Path, value: &impl serde::Serialize) -> Result<()> {
    put(path, &serde_json::to_vec_pretty(value)?)
}
fn fixture(kind: RunKind, script: bool) -> Result<Fixture> {
    let temp = tempfile::tempdir()?;
    let root = temp.path().canonicalize()?;
    let project = root.join("project");
    fs::create_dir(&project)?;
    let run = root.join("retained");
    fs::create_dir(&run)?;
    fs::write(run.join("run.lock"), b"")?;
    let compiler = root.join("producer");
    let version = Version::Gc27;
    let oracle = compiler.join("build/GC_2_7/oracle/mwcceppc.exe");
    let stock = compiler.join("build/compilers/GC/2.7/mwcceppc.exe");
    let mut runtime = vec![];
    for (path, bytes) in [
        (&oracle, b"MZ-fake-oracle".as_slice()),
        (&stock, b"MZ-fake-stock".as_slice()),
    ] {
        put(path, bytes)?;
        runtime.push(inputs::FileInput {
            path: path.clone(),
            sha256: digest(bytes),
        });
    }
    #[cfg(unix)]
    {
        let path = compiler.join("build/tools/wibo");
        put(&path, b"not an executable launcher")?;
        runtime.push(inputs::FileInput {
            path,
            sha256: digest(b"not an executable launcher"),
        });
    }
    let header = project.join("input.h");
    put(&header, b"#define VALUE 1\n")?;
    let traces = TraceOptions {
        backend: true,
        ..Default::default()
    };
    let mut keys = vec![];
    for (index, bytes) in [
        b"int value=1; //\xff\n".as_slice(),
        b"int value=1; //\xff\n".as_slice(),
        b"int value=0; //\xfe\n".as_slice(),
    ]
    .into_iter()
    .enumerate()
    {
        let source = if index == 0 {
            project.join("unit.c")
        } else {
            run.join(format!("sources/{index}/unit.c"))
        };
        put(&source, bytes)?;
        let mut flags = vec!["-nodefaults".into(), "-i".into(), ".".into()];
        if index > 0 {
            flags.extend(["-i".into(), project.to_string_lossy().into_owned()]);
        }
        let dependencies = vec![
            inputs::FileInput {
                path: source.clone(),
                sha256: digest(bytes),
            },
            inputs::FileInput {
                path: header.clone(),
                sha256: digest(b"#define VALUE 1\n"),
            },
        ];
        let snapshot = inputs::Snapshot {
            format_version: 1,
            source: source.clone(),
            source_sha256: digest(bytes),
            working_directory: project.clone(),
            flags: flags.clone(),
            trace_options: serde_json::to_value(&traces)?,
            runtime: runtime.clone(),
            environment_sha256: digest(b"env"),
            dependencies,
            preprocessed_sha256: digest(b"pp"),
            stock_preprocessed_sha256: digest(b"pp"),
            reusable: true,
            cache_limitations: vec![],
        };
        for input in snapshot.dependencies.iter().chain(&runtime) {
            put(
                &run.join("input-files").join(&input.sha256),
                &fs::read(&input.path)?,
            )?;
        }
        let object = if index < 2 {
            b"baseline object".as_slice()
        } else {
            b"candidate object".as_slice()
        };
        let compilation = Compilation {
            version,
            source: source.to_string_lossy().into_owned(),
            oracle: oracle.to_string_lossy().into_owned(),
            producer_sha256: runtime[0].sha256.clone(),
            original_sha256: Some(runtime[1].sha256.clone()),
            source_sha256: digest(bytes),
            flags,
            trace_options: traces.clone(),
            object_sha256: digest(object),
            check_status: CheckStatus::Verified,
            identical: Some(true),
            differences: vec![],
            diagnostics: String::new(),
            trace: String::new(),
            trace_present: true,
            original_fallback: false,
            object: object.into(),
        };
        let key = inputs::key(&snapshot)?;
        let capture = serde_json::to_vec(&json!({"inputs":snapshot,"compilation":compilation}))?;
        put(&run.join("cache").join(&key).join("capture.json"), &capture)?;
        put(&run.join("cache").join(&key).join("object.o"), object)?;
        json_file(
            &run.join("cache").join(&key).join("complete.json"),
            &json!({"capture_sha256":digest(&capture)}),
        )?;
        keys.push(key);
    }
    let check = if script {
        let program = project.join("checker.py");
        put(
            &program,
            b"raise RuntimeError('must never execute during export')\n",
        )?;
        Some(inputs::CheckCommand {
            program,
            arguments: vec![],
            inputs: vec![],
            timeout_seconds: 5,
        })
    } else {
        None
    };
    let check_inputs = check
        .as_ref()
        .map(|c| inputs::checker_inputs(c, &project))
        .transpose()?;
    let analyzer = digest(&fs::read(std::env::current_exe()?)?);
    let analysis = digest(&serde_json::to_vec(
        &json!({"analyzer_sha256":analyzer,"check_inputs":check_inputs,"target":null}),
    )?);
    let plan: search::Plan = serde_json::from_value(
        json!({"format_version":1,"build":{"oracle_root":compiler,"working_directory":project,"source":"unit.c","version":"GC_2_7","flags":["-nodefaults"],"from_ninja":null,"traces":traces,"timeout_seconds":10},"contract":{"format_version":1,"selection":{"link":"test","class":4,"round":0},"objective":{"kind":"symbol_address_pairs","symbol":"symbol","minimum":0,"maximum":0},"relocations":[],"purpose":if script{"source_transformation"}else{"mechanism_reproducer"}},"transformations":[],"maximum_depth":0,"generation_limit":1,"semantic_check":check,"target":null}),
    )?;
    let plan = match kind {
        RunKind::Search => serde_json::to_value(&plan)?,
        RunKind::Reduction => serde_json::to_value(reduction::Plan {
            format_version: 1,
            build: plan.build,
            contract: plan.contract,
            reductions: vec![],
            delete_lines: true,
            semantic_check: plan.semantic_check,
            target: None,
        })?,
    };
    // Typed plan field ordering is the persisted plan hash contract.
    let plan_sha = match kind {
        RunKind::Search => digest(&serde_json::to_vec(
            &serde_json::from_value::<search::Plan>(plan.clone())?,
        )?),
        RunKind::Reduction => digest(&serde_json::to_vec(&serde_json::from_value::<
            reduction::Plan,
        >(plan.clone())?)?),
    };
    json_file(&run.join("plan.json"), &plan)?;
    let outcome = json!({"id":1,"status":"observed","compilation_cache_hit":false,"artifact":keys[2],"artifact_directory":null,"fingerprint":null,"fingerprint_error":null,"duplicate_state_of":null,"evaluation":null,"semantic_check":null,"independent_verification":null,"error":null});
    let progress = match kind {
        RunKind::Search => {
            json!({"format_version":1,"plan_sha256":plan_sha,"baseline_source_sha256":digest(b"int value=1; //\xff\n"),"baseline_input_sha256":keys[1],"original_input_sha256":keys[0],"analysis_sha256":analysis,"baseline_artifact":keys[1],"baseline_calibration":null,"queue":[],"outcomes":[outcome],"next":1,"groups":{},"ranking":[],"status":"verified_success","generation_truncated":false,"compilations":3,"cache_hits":0,"error":null})
        }
        RunKind::Reduction => {
            json!({"format_version":1,"plan_sha256":plan_sha,"analysis_sha256":analysis,"baseline_source_sha256":digest(b"int value=1; //\xff\n"),"baseline_artifact":keys[1],"original_input_sha256":keys[0],"baseline_evaluation":null,"best":{"id":1,"parent":0,"source_sha256":digest(b"int value=0; //\xfe\n"),"bytes":19,"recipe":"fixture"},"best_artifact":keys[2],"cursor":{"recipe":0,"chunk_lines":1,"start_line":0},"pending":null,"outcomes":[{"candidate":{"id":1,"parent":0,"source_sha256":digest(b"int value=0; //\xfe\n"),"bytes":19,"recipe":"fixture"},"status":"accepted","artifact":keys[2],"compilation_cache_hit":false,"evaluation":null,"verification":null,"error":null}],"status":"verified_reduction","compilations":3,"cache_hits":0,"error":null})
        }
    };
    json_file(&run.join("progress.json"), &progress)?;
    Ok(Fixture {
        temp,
        run,
        project,
        candidate_key: keys.pop().unwrap(),
    })
}
fn rewrite_manifest(bundle: &Path, edit: impl FnOnce(&mut Value)) -> Result<()> {
    let mut manifest: Value = serde_json::from_slice(&fs::read(bundle.join("manifest.json"))?)?;
    edit(&mut manifest);
    json_file(&bundle.join("manifest.json"), &manifest)?;
    json_file(
        &bundle.join("complete.json"),
        &json!({"format_version":1,"manifest_sha256":digest(&fs::read(bundle.join("manifest.json"))?)}),
    )
}

#[test]
fn exports_exact_retained_bytes_after_original_inputs_are_removed() -> Result<()> {
    for kind in [RunKind::Search, RunKind::Reduction] {
        let f = fixture(kind, false)?;
        fs::remove_dir_all(&f.project)?;
        let bundle = f.temp.path().join("bundle");
        let manifest = reproducer::export(kind, &f.run, 1, &bundle, None)?;
        assert_eq!(manifest.cases[2].input_sha256, f.candidate_key);
        assert!(reproducer::verify(&bundle)?.host_compatible);
        assert_eq!(manifest.capabilities["os_sandbox"], false);
        let source = manifest.cases[2]
            .files
            .iter()
            .find(|s| s.path == manifest.cases[2].source)
            .unwrap();
        assert_eq!(
            fs::read(bundle.join("blobs").join(&source.sha256))?,
            b"int value=0; //\xfe\n"
        );
        assert!(reproducer::export(kind, &f.run, 1, &bundle, None).is_err());
    }
    Ok(())
}
#[test]
fn missing_tampered_blobs_analyzer_and_incomplete_receipts_fail_closed() -> Result<()> {
    let f = fixture(RunKind::Search, false)?;
    let bundle = f.temp.path().join("bundle");
    let manifest = reproducer::export(RunKind::Search, &f.run, 1, &bundle, None)?;
    let path = bundle.join("blobs").join(&manifest.cases[2].object_sha256);
    let bytes = fs::read(&path)?;
    fs::remove_file(&path)?;
    assert!(reproducer::verify(&bundle).is_err());
    fs::write(&path, b"modified")?;
    assert!(reproducer::verify(&bundle).is_err());
    fs::write(&path, bytes)?;
    fs::write(bundle.join(&manifest.replay_analyzer.path), b"modified")?;
    assert!(reproducer::verify(&bundle).is_err());
    fs::write(
        bundle.join(&manifest.replay_analyzer.path),
        fs::read(bundle.join("blobs").join(&manifest.replay_analyzer.sha256))?,
    )?;
    assert!(reproducer::verify(&bundle).is_ok());
    fs::remove_file(bundle.join("complete.json"))?;
    assert!(reproducer::verify(&bundle).is_err());
    Ok(())
}
#[test]
fn rehashed_manifest_cannot_escape_paths_change_purpose_or_select_other_capture() -> Result<()> {
    let f = fixture(RunKind::Search, false)?;
    let bundle = f.temp.path().join("bundle");
    reproducer::export(RunKind::Search, &f.run, 1, &bundle, None)?;
    let original = fs::read(bundle.join("manifest.json"))?;
    for path in [
        "../escape.c",
        "project//unit.c",
        "project/./unit.c",
        "project/NUL.c",
        "F:/escape.c",
        "/escape.c",
    ] {
        fs::write(bundle.join("manifest.json"), &original)?;
        rewrite_manifest(&bundle, |m| m["cases"][0]["files"][0]["path"] = json!(path))?;
        assert!(reproducer::verify(&bundle).is_err(), "accepted {path}");
    }
    fs::write(bundle.join("manifest.json"), &original)?;
    rewrite_manifest(&bundle, |m| {
        m["contract"]["purpose"] = json!("source_transformation")
    })?;
    assert!(reproducer::verify(&bundle).is_err());
    fs::write(bundle.join("manifest.json"), &original)?;
    rewrite_manifest(&bundle, |m| m["origin"]["candidate"] = json!(7))?;
    assert!(reproducer::verify(&bundle).is_err());
    Ok(())
}
#[test]
fn unsupported_checker_and_original_tree_outputs_are_rejected_without_execution() -> Result<()> {
    let f = fixture(RunKind::Search, true)?;
    let bundle = f.temp.path().join("bundle");
    let error = reproducer::export(RunKind::Search, &f.run, 1, &bundle, None)
        .unwrap_err()
        .to_string();
    assert!(error.contains("native executable"), "{error}");
    assert!(!bundle.join("complete.json").exists());
    assert!(reproducer::export(RunKind::Search, &f.run, 1, &f.run.join("nested"), None).is_err());
    assert!(!f.run.join("nested").exists());
    // A native-shaped fake checker is retained but never launched by export or
    // verify. Its declared arguments must remain bound to the original plan.
    let plan: search::Plan = serde_json::from_slice(&fs::read(f.run.join("plan.json"))?)?;
    let check = plan.semantic_check.as_ref().unwrap();
    fs::write(
        &check.program,
        if cfg!(windows) {
            b"MZ-fake-native".as_slice()
        } else {
            b"\x7fELF-fake-native".as_slice()
        },
    )?;
    let captured = inputs::checker_inputs(check, &f.project)?;
    let analyzer = digest(&fs::read(std::env::current_exe()?)?);
    let analysis = digest(&serde_json::to_vec(
        &json!({"analyzer_sha256":analyzer,"check_inputs":captured,"target":null}),
    )?);
    let mut progress: Value = serde_json::from_slice(&fs::read(f.run.join("progress.json"))?)?;
    progress["analysis_sha256"] = json!(analysis);
    json_file(&f.run.join("progress.json"), &progress)?;
    let checked = f.temp.path().join("checked");
    reproducer::export(RunKind::Search, &f.run, 1, &checked, None)?;
    rewrite_manifest(&checked, |m| {
        m["checker"]["arguments"] = json!([{"kind":"literal","value":"altered"}])
    })?;
    assert!(reproducer::verify(&checked).is_err());
    Ok(())
}
#[test]
fn attached_path_and_output_modes_cannot_be_reintroduced_by_manifest_edits() -> Result<()> {
    let f = fixture(RunKind::Search, false)?;
    let bundle = f.temp.path().join("bundle");
    reproducer::export(RunKind::Search, &f.run, 1, &bundle, None)?;
    let original = fs::read(bundle.join("manifest.json"))?;
    for flag in [
        "-prefixF:/outside.h",
        "-include/tmp/ambient.h",
        "-oescape.o",
        "@response",
        "-I../outside",
    ] {
        fs::write(bundle.join("manifest.json"), &original)?;
        rewrite_manifest(&bundle, |m| {
            m["cases"][0]["flags"] = json!([{"kind":"literal","value":flag}])
        })?;
        assert!(reproducer::verify(&bundle).is_err(), "accepted {flag}");
    }
    Ok(())
}
