//! Compile the source-linked Windows oracle's C and Rust hook objects.

use crate::{
    producer,
    wasm_build::{Unit, WII_RC1},
    wasm_oracle::{self, HookBuild, HookObjects},
};
use anyhow::{Context, Result, ensure};
use serde::Deserialize;
use std::{collections::BTreeMap, fs, path::Path, time::Duration};

#[derive(Deserialize)]
struct Plan {
    version: String,
    units: Vec<Unit>,
    objects: BTreeMap<String, String>,
}

pub fn compile(root: &Path, plan: &Path, rustc: &Path, timeout: Duration) -> Result<()> {
    let plan: Plan = serde_json::from_slice(&fs::read(plan)?)?;
    let version = &plan.version;
    ensure!(version == WII_RC1, "no Windows source oracle for {version}");
    let directory = format!("gc2/build/{version}/oracle");
    let work = format!("{directory}/hooks");
    let output = format!("{directory}/mwcceppc.exe");
    let source_root = root.join("gc2");
    let rust_ir = [format!("{work}/hooks.ll"), format!("{work}/frontend.ll")];
    let trace = format!("{work}/trace.obj");
    producer::build_source_windows(root, Path::new(&rust_ir[0]), rustc, timeout, None)?;
    producer::build_source_windows(root, Path::new(&rust_ir[1]), rustc, timeout, Some(version))?;
    producer::build_trace(root, Path::new(&trace), rustc, timeout)?;
    let clang = "clang";
    let HookObjects {
        outcome,
        mut replaced,
        extra,
        variadic,
    } = wasm_oracle::compile_hooks(HookBuild {
        root,
        source_root: &source_root,
        version,
        work: &work,
        output: &output,
        clang,
        units: &plan.units,
        rust_ir: &rust_ir,
        wasm: false,
        combined: false,
    })?;

    let mut arguments = plan
        .units
        .iter()
        .find(|unit| unit.name == "src/optimizer/IroDump.c")
        .context("IroDump.c is missing from the Windows oracle plan")?
        .arguments
        .clone();
    let bridge = format!("{work}/oracle.c.obj");
    arguments.extend([
        format!("-DORACLE_IRO_DUMP={}", variadic.0),
        format!("-DORACLE_IRO_DUMP_ARGUMENTS={}", variadic.1),
        "-c".into(),
        root.join(wasm_oracle::ORACLE_C)
            .to_string_lossy()
            .into_owned(),
        "-o".into(),
        root.join(&bridge).to_string_lossy().into_owned(),
    ]);
    wasm_oracle::run_tool(&source_root, clang, &arguments)?;

    let objects = plan
        .units
        .iter()
        .map(|unit| {
            replaced
                .remove(&unit.name)
                .or_else(|| plan.objects.get(&unit.name).cloned())
                .with_context(|| format!("no Windows object for {}", unit.name))
        })
        .chain(extra.into_iter().map(Ok))
        .chain([Ok(trace), Ok(bridge)])
        .collect::<Result<Vec<_>>>()?;
    fs::write(
        root.join(format!("{directory}/objects.json")),
        serde_json::to_vec(&objects)?,
    )?;
    fs::write(
        root.join(format!("{work}/hooks.json")),
        serde_json::to_vec_pretty(&outcome)?,
    )?;
    println!(
        "{version}: {} hooks, {} names read",
        outcome.hooks.len(),
        outcome.imports.len()
    );
    Ok(())
}
