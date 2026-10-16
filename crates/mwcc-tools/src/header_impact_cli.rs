use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::{SCHEMA_VERSION, header_impact, project};
use serde_json::{Value, json};
use std::{
    fs,
    path::PathBuf,
    process::Command,
    sync::{
        Mutex,
        atomic::{AtomicUsize, Ordering},
    },
};

pub fn run(args: &[String]) -> Result<()> {
    let mut options = super::tu_cli::Options::default();
    let mut revision = None;
    let mut overrides = vec![];
    let mut filters = vec![];
    let mut target = None;
    let mut jobs = 4usize;
    let mut all = false;
    let mut json_output = false;
    let mut i = 0;
    while i < args.len() {
        let option = &args[i];
        match option.as_str() {
            "--all" => all = true,
            "--json" => json_output = true,
            "--git" | "--override" | "--unit" | "--target" | "--jobs" | "--from-ninja"
            | "--version" | "--oracle-root" | "--timeout" => {
                i += 1;
                let value = args
                    .get(i)
                    .with_context(|| format!("{option} needs a value"))?;
                match option.as_str() {
                    "--git" => revision = Some(value.clone()),
                    "--override" => overrides.push(value.clone()),
                    "--unit" => filters.push(value.clone()),
                    "--target" => target = Some(PathBuf::from(value)),
                    "--jobs" => jobs = value.parse::<usize>()?.max(1),
                    _ => {
                        if option == "--from-ninja" {
                            options.ninja = Some(value.into());
                        }
                        options.common.extend([option.clone(), value.clone()]);
                    }
                }
            }
            other => bail!("unknown header-impact option {other}"),
        }
        i += 1;
    }
    ensure!(
        revision.is_some() == overrides.is_empty(),
        "give the change: --git REV, or --override PATH=EDITED"
    );
    let ninja = options
        .ninja
        .as_deref()
        .context("header-impact requires --from-ninja FILE")?
        .canonicalize()?;
    let root = ninja.parent().context("Ninja file has no parent")?;
    let directory = target.map_or_else(|| project::target_directory(&ninja), Ok)?;
    let scratch = tempfile::TempDir::new()?;
    let mut before = vec![];
    let mut changed = vec![];
    if let Some(revision) = revision {
        let output = Command::new("git")
            .args(["diff", "--name-only", "-z", &revision, "--"])
            .current_dir(root)
            .output()
            .context("run git diff")?;
        ensure!(
            output.status.success(),
            "git diff failed: {}",
            String::from_utf8_lossy(&output.stderr)
        );
        for (k, name) in output
            .stdout
            .split(|b| *b == 0)
            .filter(|s| !s.is_empty())
            .enumerate()
        {
            let name = String::from_utf8(name.to_vec()).context("Git path is not UTF-8")?;
            let old = Command::new("git")
                .args(["show", &format!("{revision}:{name}")])
                .current_dir(root)
                .output()?;
            if !old.status.success() {
                continue;
            }
            let copy = scratch.path().join(format!("before{k}")).join(
                std::path::Path::new(&name)
                    .file_name()
                    .context("changed path has no filename")?,
            );
            fs::create_dir_all(copy.parent().unwrap())?;
            fs::write(&copy, old.stdout)?;
            before.push(format!("{name}={}", copy.display()));
            changed.push(name);
        }
    } else {
        for item in &overrides {
            changed.push(
                item.split_once('=')
                    .context("--override needs PATH=EDITED")?
                    .0
                    .to_owned(),
            );
        }
    }
    let output = Command::new("ninja")
        .args(["-t", "deps"])
        .current_dir(root)
        .output()
        .context("run ninja -t deps")?;
    ensure!(
        output.status.success(),
        "ninja -t deps failed: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    let units = header_impact::dependents(root, &changed, &String::from_utf8_lossy(&output.stdout));
    let mut units: Vec<_> = units
        .into_iter()
        .filter(|(unit, _)| filters.is_empty() || filters.iter().any(|f| unit.contains(f)))
        .collect();
    units.sort_by(|a, b| a.0.cmp(&b.0));
    if !json_output {
        println!(
            "{} changed file(s); {} unit(s) include them",
            changed.len(),
            units.len()
        );
    }
    let results: Mutex<Vec<Option<Value>>> = Mutex::new((0..units.len()).map(|_| None).collect());
    let cursor = AtomicUsize::new(0);
    let run = |unit: &str, files: &[String]| -> Result<Vec<Value>> {
        let compile = |overrides: &[String]| {
            let mut options = options.clone();
            for item in overrides {
                let path = item
                    .split_once('=')
                    .context("--override needs PATH=EDITED")?
                    .0;
                if files.iter().any(|f| f == path)
                    || path.replace('\\', "/") == unit.replace('\\', "/")
                {
                    options.common.extend(["--override".into(), item.clone()]);
                }
            }
            super::tu_cli::compile(unit, &options, false, true)
        };
        let (a, ad) = compile(&before)?;
        let (b, bd) = compile(&overrides)?;
        let asm = project::target_asm(&directory, unit)?
            .map(fs::read)
            .transpose()?
            .map(|b| String::from_utf8_lossy(&b).into_owned());
        header_impact::compare([&a.object, &b.object], [&ad, &bd], asm.as_deref())
    };
    std::thread::scope(|scope| {
        for _ in 0..jobs.min(units.len()) {
            let run = &run;
            let units = &units;
            let cursor = &cursor;
            let results = &results;
            scope.spawn(move||loop{let index=cursor.fetch_add(1,Ordering::Relaxed);let Some((unit,files))=units.get(index)else{break;};let row=match run(unit,files){Ok(functions)=>json!({"unit":unit,"files":files,"functions":functions,"error":null}),Err(error)=>json!({"unit":unit,"files":files,"functions":null,"error":format!("{error:#}")})};results.lock().unwrap()[index]=Some(row);});
        }
    });
    let results: Vec<_> = results
        .into_inner()
        .map_err(|_| anyhow::anyhow!("header-impact worker results poisoned"))?
        .into_iter()
        .map(|r| r.expect("worker left unit unprocessed"))
        .collect();
    if json_output {
        println!(
            "{}",
            serde_json::to_string_pretty(
                &json!({"schema_version":SCHEMA_VERSION,"changed":changed,"units":results})
            )?
        );
        return Ok(());
    }
    for result in &results {
        let unit = result["unit"].as_str().unwrap();
        if let Some(error) = result["error"].as_str() {
            println!("== {unit}: ERROR {error}");
        } else {
            let rows = result["functions"].as_array().unwrap();
            if all || !rows.is_empty() {
                let files = serde_json::from_value::<Vec<String>>(result["files"].clone())?;
                println!("{}", header_impact::report(unit, &files, rows).join("\n"));
            }
        }
    }
    let mut worse = vec![];
    let mut touched = 0;
    for result in &results {
        if let Some(rows) = result["functions"].as_array() {
            touched += usize::from(!rows.is_empty());
            for row in rows {
                if let (Some(a), Some(b)) =
                    (row["similarity"][0].as_f64(), row["similarity"][1].as_f64())
                    && b < a
                {
                    worse.push((
                        result["unit"].as_str().unwrap(),
                        row["function"].as_str().unwrap(),
                    ));
                }
            }
        }
    }
    println!(
        "{touched} of {} unit(s) changed; {} function(s) further from the target{}",
        results.len(),
        worse.len(),
        worse
            .iter()
            .map(|(u, f)| format!("\n   {u}: {f}"))
            .collect::<String>()
    );
    Ok(())
}
