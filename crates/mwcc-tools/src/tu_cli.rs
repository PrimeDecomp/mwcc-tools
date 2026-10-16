use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::{
    document, inline_diff, project,
    runner::{self, Compilation},
    trace,
};
use serde_json::Value;
use std::{collections::BTreeSet, path::PathBuf};

#[derive(Clone, Default)]
pub(super) struct Options {
    pub(super) common: Vec<String>,
    flags: Vec<String>,
    sources: Vec<String>,
    pub(super) ninja: Option<PathBuf>,
    target: Option<Option<PathBuf>>,
    callee: Option<String>,
    all: bool,
    json: bool,
    verbose: bool,
    section: Option<String>,
}
fn parse(args: &[String]) -> Result<Options> {
    let mut out = Options::default();
    let mut i = 0;
    while i < args.len() {
        let option = &args[i];
        match option.as_str() {
            "--" => {
                out.flags.extend_from_slice(&args[i + 1..]);
                break;
            }
            "--json" => out.json = true,
            "--all" => out.all = true,
            "--verbose" => out.verbose = true,
            "--target" => {
                if args.get(i + 1).is_some_and(|s| !s.starts_with('-')) {
                    i += 1;
                    out.target = Some(Some(PathBuf::from(&args[i])));
                } else {
                    out.target = Some(None);
                }
            }
            "--callee" => {
                i += 1;
                out.callee = Some(args.get(i).context("--callee needs a value")?.clone());
            }
            "--section" => {
                i += 1;
                out.section = Some(args.get(i).context("--section needs a value")?.clone());
            }
            "--oracle-root"
            | "--working-directory"
            | "--from-ninja"
            | "--version"
            | "--override"
            | "--timeout" => {
                i += 1;
                let value = args
                    .get(i)
                    .with_context(|| format!("{option} needs a value"))?;
                if option == "--from-ninja" {
                    out.ninja = Some(value.into());
                }
                out.common.extend([option.clone(), value.clone()]);
            }
            option if option.starts_with('-') => bail!("unknown TU option {option}"),
            source => out.sources.push(source.into()),
        }
        i += 1;
    }
    ensure!(
        !out.sources.is_empty(),
        "a source or objdiff unit name is required"
    );
    Ok(out)
}
pub(super) fn compile(
    source: &str,
    options: &Options,
    instantiation: bool,
    fallback: bool,
) -> Result<(Compilation, Value)> {
    let source = project::unit_source(options.ninja.as_deref(), source)?;
    let mut args = options.common.clone();
    args.extend(["--no-check".into(), "--full-cost".into()]);
    if instantiation {
        args.push("--instantiation".into());
    }
    if fallback {
        args.push("--original-fallback".into());
    }
    args.push(source);
    if !options.flags.is_empty() {
        args.push("--".into());
        args.extend(options.flags.clone());
    }
    let request = super::parse_compile(&args, false)?.request;
    let compiled = runner::compile(&request)?;
    let trace = trace::parse(&compiled.trace)?;
    let document = document::build(
        &trace,
        compiled.version,
        &compiled.source,
        &compiled.oracle,
        compiled.identical,
        &compiled.differences,
        &Default::default(),
    )?;
    Ok((compiled, document))
}
fn directory(options: &Options, required: bool) -> Result<Option<PathBuf>> {
    if options.target.is_none() && !required {
        return Ok(None);
    }
    if let Some(Some(directory)) = &options.target {
        return Ok(Some(directory.clone()));
    }
    let ninja = options
        .ninja
        .as_deref()
        .context("--target without a directory needs --from-ninja")?;
    Ok(Some(project::target_directory(ninja)?))
}
pub fn inline_diff(args: &[String]) -> Result<()> {
    let options = parse(args)?;
    ensure!(
        options.sources.len() <= 2,
        "inline-diff takes A and optionally B"
    );
    let sources = [
        &options.sources[0],
        options.sources.get(1).unwrap_or(&options.sources[0]),
    ];
    let documents = [
        compile(sources[0], &options, options.target.is_some(), false)?.1,
        compile(sources[1], &options, options.target.is_some(), false)?.1,
    ];
    let directory = directory(&options, false)?;
    let mut targets = [None, None];
    if let Some(directory) = &directory {
        for i in 0..2 {
            targets[i] =
                inline_diff::target_functions(directory, documents[i]["source"].as_str().unwrap())?;
            if targets[i].is_none() {
                eprintln!(
                    "WARNING: no target asm for {} under {}",
                    documents[i]["source"].as_str().unwrap(),
                    directory.display()
                );
            }
        }
    }
    let out = inline_diff::compare(
        [&documents[0], &documents[1]],
        [targets[0].as_ref(), targets[1].as_ref()],
        options.callee.as_deref(),
        options.all,
        options.target.is_some(),
    );
    if options.json {
        println!("{}", serde_json::to_string_pretty(&out)?);
        return Ok(());
    }
    let decisions = documents.each_ref().map(inline_diff::decisions);
    let shared = decisions[0]
        .keys()
        .filter(|l| decisions[1].contains_key(*l))
        .collect::<BTreeSet<_>>()
        .len();
    let rows = out["callees"].as_array().unwrap();
    let which = if options.all {
        "all"
    } else if options.target.is_some() {
        "mispredicted against the target"
    } else {
        "decided differently"
    };
    println!(
        "A: {}\nB: {}\n{shared} callees decided in both; {which}: {}",
        out["a"].as_str().unwrap(),
        out["b"].as_str().unwrap(),
        rows.len()
    );
    for row in rows {
        println!(
            "== {}  [{}]",
            row["callee"].as_str().unwrap(),
            row["link"].as_str().unwrap()
        );
        println!("{}", inline_diff::summary_report("A", &row["a"]).join("\n"));
        println!("{}", inline_diff::summary_report("B", &row["b"]).join("\n"));
    }
    for i in 0..2 {
        if let Some(target) = &targets[i] {
            println!(
                "{}",
                inline_diff::emission_report(
                    if i == 0 { "A" } else { "B" },
                    &documents[i],
                    target,
                    &inline_diff::emission(&documents[i]),
                    options.callee.as_deref()
                )
                .join("\n")
            );
        }
    }
    Ok(())
}

pub fn inline_window(args: &[String]) -> Result<()> {
    use mwcc_oracle_core::{SCHEMA_VERSION, inline_window};
    let options = parse(args)?;
    ensure!(
        options.ninja.is_some(),
        "inline-window requires --from-ninja FILE"
    );
    let directory = directory(&options, true)?.unwrap();
    let mut results = vec![];
    for source in &options.sources {
        let (compiled, document) = compile(source, &options, false, false)?;
        let Some(asm) = project::target_asm(&directory, &compiled.source)? else {
            eprintln!(
                "WARNING: no target asm for {} under {}",
                compiled.source,
                directory.display()
            );
            continue;
        };
        let text = std::fs::read(asm)?;
        let rows = inline_window::bounds(
            &document,
            &inline_window::object_calls(&compiled.object)?,
            &inline_window::asm_calls(&String::from_utf8_lossy(&text)),
        );
        let (low, high) = inline_window::window(&rows);
        if !options.json {
            println!(
                "{}",
                inline_window::report(&compiled.source, &rows, options.verbose).join("\n")
            );
        }
        results.push(
            serde_json::json!({"source":compiled.source,"low":low,"high":high,"bounds":rows}),
        );
    }
    if options.json {
        println!(
            "{}",
            serde_json::to_string_pretty(
                &serde_json::json!({"schema_version":SCHEMA_VERSION,"units":results})
            )?
        );
    }
    Ok(())
}

pub fn data_diff(args: &[String]) -> Result<()> {
    use mwcc_oracle_core::{SCHEMA_VERSION, data_diff};
    let options = parse(args)?;
    ensure!(options.sources.len() == 1, "data-diff takes one source");
    let directory = directory(&options, true)?.unwrap();
    let (compiled, _) = compile(&options.sources[0], &options, false, true)?;
    let mut mine = data_diff::object_layout(&compiled.object)?;
    let path = project::target_asm(&directory, &compiled.source)?.with_context(|| {
        format!(
            "no target asm for {} under {}",
            compiled.source,
            directory.display()
        )
    })?;
    let bytes = std::fs::read(&path)?;
    let text = String::from_utf8_lossy(&bytes);
    let mut theirs = data_diff::asm_layout(&text)?;
    let trace = trace::parse(&compiled.trace)?;
    let module = path
        .ancestors()
        .find(|p| p.file_name().is_some_and(|n| n == "asm"))
        .context("target path has no asm directory")?;
    data_diff::prepare(
        &mut mine,
        &mut theirs,
        &compiled.object,
        &trace,
        &text,
        module,
        &path,
    )?;
    let names: Vec<_> = mine
        .keys()
        .chain(theirs.keys().filter(|k| !mine.contains_key(*k)))
        .filter(|k| options.section.as_ref().is_none_or(|n| n == *k))
        .collect();
    if options.json {
        let sections:serde_json::Map<_,_>=names.iter().map(|n|{let a=mine.get(*n);let b=theirs.get(*n);((*n).clone(),serde_json::json!({"oracle":a,"target":b,"differences":a.zip(b).map(|(a,b)|data_diff::compare(&data_diff::linked(a["items"].as_array().unwrap()),&data_diff::linked(b["items"].as_array().unwrap())))}))}).collect();
        println!(
            "{}",
            serde_json::to_string_pretty(
                &serde_json::json!({"schema_version":SCHEMA_VERSION,"source":compiled.source,"target":project::display_path(&path),"sections":sections})
            )?
        );
    } else {
        println!(
            "{} against {}",
            compiled.source,
            project::display_path(&path)
        );
        let lines: Vec<_> = names
            .iter()
            .flat_map(|n| data_diff::report(n, mine.get(*n), theirs.get(*n), options.all))
            .collect();
        println!(
            "{}",
            if lines.is_empty() {
                "the data is laid out as in the target".into()
            } else {
                lines.join("\n")
            }
        );
    }
    Ok(())
}
