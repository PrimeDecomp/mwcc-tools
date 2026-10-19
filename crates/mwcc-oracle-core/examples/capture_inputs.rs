//! Retain exact compiler inputs for an independently checked source case.
//! Build this example, then invoke its binary directly (not via `cargo run`,
//! which changes the inherited environment).
use anyhow::{Context, Result, ensure};
use mwcc_oracle_core::{
    runner::{Compilation, CompileRequest},
    source_guided::{inputs, state},
};
use serde::Deserialize;
use std::{env, fs, path::PathBuf, time::Duration};
#[derive(Deserialize)]
struct Capture {
    compilation: Compilation,
}
fn main() -> Result<()> {
    let arguments = env::args().skip(1).collect::<Vec<_>>();
    ensure!(
        arguments.len() == 5,
        "use capture_inputs CAPTURE.json ORACLE_ROOT WORKING_DIRECTORY SOURCE OUTPUT"
    );
    let capture: Capture = serde_json::from_slice(&fs::read(&arguments[0])?)?;
    let c = capture.compilation;
    ensure!(
        c.check_status == mwcc_oracle_core::runner::CheckStatus::Verified,
        "capture was not stock-verified"
    );
    let source = PathBuf::from(&arguments[3]).canonicalize()?;
    ensure!(
        state::digest(&fs::read(&source)?) == c.source_sha256,
        "source differs from its capture"
    );
    let request = CompileRequest {
        root: PathBuf::from(&arguments[1]).canonicalize()?,
        working_directory: PathBuf::from(&arguments[2]).canonicalize()?,
        source: source.to_str().context("source path is not UTF-8")?.into(),
        flags: c.flags,
        version: c.version,
        options: c.trace_options,
        overrides: vec![],
        check: true,
        original_fallback: false,
        cut_limit: None,
        merge_limit: None,
        keep_object: None,
        timeout: Duration::from_secs(60),
    };
    let output = PathBuf::from(&arguments[4]);
    ensure!(
        !output.exists(),
        "retain previous inputs; use a new output directory"
    );
    let snapshot = inputs::capture(&request, &output.join("scans"), &|| false)?;
    let producer = request
        .root
        .join(format!("build/{}/oracle/mwcceppc.exe", c.version.name()))
        .canonicalize()?;
    let stock = request
        .root
        .join(format!(
            "build/compilers/GC/{}/mwcceppc.exe",
            c.version.compiler_directory()
        ))
        .canonicalize()?;
    ensure!(
        snapshot
            .runtime
            .iter()
            .any(|f| f.path == producer && f.sha256 == c.producer_sha256)
            && snapshot
                .runtime
                .iter()
                .any(|f| f.path == stock && Some(&f.sha256) == c.original_sha256.as_ref()),
        "compiler identities differ from their capture"
    );
    fs::create_dir_all(output.join("files"))?;
    for file in snapshot.dependencies.iter().chain(&snapshot.runtime) {
        let bytes = fs::read(&file.path)?;
        ensure!(
            state::digest(&bytes) == file.sha256,
            "input changed while being retained"
        );
        fs::write(output.join("files").join(&file.sha256), bytes)?;
    }
    let path = output.join("inputs.json");
    fs::write(&path, serde_json::to_vec_pretty(&snapshot)?)?;
    println!("{}", path.display());
    Ok(())
}
