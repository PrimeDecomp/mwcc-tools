use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::{document, inline_calibrate as calibrate, project, runner, trace};
use serde_json::Value;
use std::{
    collections::BTreeSet,
    env, fs,
    path::{Path, PathBuf},
    sync::{
        Mutex,
        atomic::{AtomicUsize, Ordering},
    },
};
fn symbols(root: &Path, version: &str) -> Result<BTreeSet<String>> {
    let directory = root.join("config").join(version);
    let mut paths = vec![directory.join("symbols.txt")];
    let rels = directory.join("rels");
    if rels.is_dir() {
        let mut more = vec![];
        for entry in fs::read_dir(rels)? {
            let path = entry?.path().join("symbols.txt");
            if path.is_file() {
                more.push(path);
            }
        }
        more.sort();
        paths.extend(more);
    }
    let mut out = BTreeSet::new();
    for path in paths {
        let text = fs::read_to_string(&path).with_context(|| format!("read {}", path.display()))?;
        out.extend(
            text.lines()
                .map(|l| l.split(" = ").next().unwrap().trim().to_owned()),
        );
    }
    Ok(out)
}
pub fn run(args: &[String]) -> Result<()> {
    if args.iter().any(|a| matches!(a.as_str(), "--help" | "-h")) {
        println!(
            "mwcc-tools inline-calibrate --project DIR --callee NAME [--unit UNIT] [--override PATH=FILE] [--near N] [--version GAME] [--compiler V] [--jobs N] [--breakdown] [--json FILE] [--binary]\nCompiler versions are inferred per unit unless --compiler overrides them. Equality is checked."
        );
        return Ok(());
    }
    let mut root = env::current_dir()?;
    let mut callee = None;
    let mut units = vec![];
    let mut overrides = vec![];
    let mut near = 3i64;
    let mut version = None;
    let mut compiler = None;
    let mut jobs = std::thread::available_parallelism()
        .map_or(4, usize::from)
        .min(6);
    let mut breakdown = false;
    let mut destination = None;
    let mut binary = false;
    let mut oracle = env::var_os("MWCC_ORACLE_ROOT")
        .map(PathBuf::from)
        .unwrap_or(env::current_dir()?);
    let mut i = 0;
    while i < args.len() {
        let option = &args[i];
        match option.as_str() {
            "--breakdown" => breakdown = true,
            "--binary" => binary = true,
            "--project" | "--callee" | "--unit" | "-u" | "--override" | "--near" | "--version"
            | "--compiler" | "--jobs" | "--json" | "--oracle-root" => {
                i += 1;
                let v = args
                    .get(i)
                    .with_context(|| format!("{option} needs a value"))?;
                match option.as_str() {
                    "--project" => root = v.into(),
                    "--callee" => callee = Some(v.clone()),
                    "--unit" | "-u" => units.push(v.clone()),
                    "--override" => overrides.push(v.clone()),
                    "--near" => near = v.parse()?,
                    "--version" => version = Some(v.clone()),
                    "--compiler" => compiler = Some(v.clone()),
                    "--jobs" => jobs = v.parse::<usize>()?.max(1),
                    "--json" => destination = Some(PathBuf::from(v)),
                    "--oracle-root" => oracle = v.into(),
                    _ => unreachable!(),
                }
            }
            other => bail!("unknown inline-calibrate option {other}"),
        }
        i += 1;
    }
    let callee = callee.context("inline-calibrate requires --callee")?;
    root = root.canonicalize()?;
    let ninja = root.join("build.ninja");
    let version = if let Some(version) = version {
        version
    } else {
        project::target_directory(&ninja)?
            .file_name()
            .context("target directory has no build identifier")?
            .to_string_lossy()
            .into_owned()
    };
    let report: Value = serde_json::from_slice(&fs::read(
        root.join("build").join(&version).join("report.json"),
    )?)?;
    let prefix = format!("{callee}<");
    let mut wanted: Vec<_> = report["units"]
        .as_array()
        .context("report.json missing units")?
        .iter()
        .filter(|u| {
            u["metadata"]["source_path"].is_string()
                && (units.is_empty() || units.iter().any(|n| u["name"] == *n))
                && (binary
                    || !units.is_empty()
                    || u["functions"].as_array().into_iter().flatten().any(|f| {
                        f.get("fuzzy_match_percent").is_none()
                            && f["name"].as_str().is_some_and(|s| s.starts_with(&prefix))
                    }))
        })
        .cloned()
        .collect();
    wanted.sort_by(|a, b| a["name"].as_str().cmp(&b["name"].as_str()));
    ensure!(
        binary || !wanted.is_empty(),
        "no unit has a target-only {callee} instance"
    );
    let results: Mutex<Vec<Option<Result<Value>>>> =
        Mutex::new((0..wanted.len()).map(|_| None).collect());
    let cursor = AtomicUsize::new(0);
    let compile = |unit: &Value| -> Result<Value> {
        let mut args = vec![
            "--oracle-root".into(),
            oracle.to_string_lossy().into_owned(),
            "--from-ninja".into(),
            ninja.to_string_lossy().into_owned(),
            "--inline-cost".into(),
        ];
        if let Some(compiler) = &compiler {
            args.extend(["--version".into(), compiler.clone()]);
        }
        for item in &overrides {
            args.extend(["--override".into(), item.clone()]);
        }
        args.push(
            unit["metadata"]["source_path"]
                .as_str()
                .context("unit has no source")?
                .into(),
        );
        let request = super::parse_compile(&args, false)?.request;
        let compiled = runner::compile(&request)?;
        let trace = trace::parse(&compiled.trace)?;
        document::build(
            &trace,
            compiled.version,
            &compiled.source,
            &compiled.oracle,
            compiled.identical,
            &compiled.differences,
            &Default::default(),
        )
    };
    std::thread::scope(|scope| {
        for _ in 0..jobs.min(wanted.len()) {
            let compile = &compile;
            let wanted = &wanted;
            let results = &results;
            let cursor = &cursor;
            scope.spawn(move || {
                loop {
                    let index = cursor.fetch_add(1, Ordering::Relaxed);
                    let Some(unit) = wanted.get(index) else {
                        break;
                    };
                    results.lock().unwrap()[index] = Some(compile(unit));
                }
            });
        }
    });
    let results = results
        .into_inner()
        .map_err(|_| anyhow::anyhow!("calibration worker results poisoned"))?;
    let mut documents = vec![];
    for (unit, result) in wanted.iter().zip(results) {
        let name = unit["name"].as_str().unwrap();
        match result.context("worker omitted calibration result")? {
            Ok(document) => {
                if document["identical"] == false && !binary {
                    println!(
                        "{name}: WARNING the oracle object differs from the original compiler's"
                    );
                }
                documents.push((name.to_owned(), document));
            }
            Err(error) => println!(
                "{name}: oracle failed: {}",
                format!("{error:#}")
                    .chars()
                    .rev()
                    .take(600)
                    .collect::<String>()
                    .chars()
                    .rev()
                    .collect::<String>()
            ),
        }
    }
    let output = if binary {
        let table = calibrate::binary_table(&documents, &symbols(&root, &version)?, &callee)?;
        println!("{}", calibrate::binary_report(&table, near).join("\n"));
        serde_json::to_value(table)?
    } else {
        let mut rows = vec![];
        for (name, document) in &documents {
            let target = wanted.iter().find(|u| u["name"] == *name).unwrap();
            rows.extend(calibrate::rows(name, target, document, &callee, near)?);
        }
        let lines = calibrate::report(&rows, near, breakdown);
        if !lines.is_empty() {
            println!("{}", lines.join("\n"));
        }
        serde_json::to_value(rows)?
    };
    if let Some(destination) = destination {
        fs::write(destination, serde_json::to_vec_pretty(&output)?)?;
    }
    Ok(())
}
