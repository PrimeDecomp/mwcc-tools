use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::{
    inlining, ninja, project, regdiff, report,
    runner::{self, CompileRequest, TraceOptions},
    trace,
    version::{self, Version},
};
use serde_json::{Value, json};
use std::{
    collections::BTreeMap,
    env, fs,
    path::{Path, PathBuf},
    process::Command,
    time::Duration,
};

fn path(root: &Path, relative: &str) -> PathBuf {
    root.join(relative.replace(['\\', '/'], std::path::MAIN_SEPARATOR_STR))
}
struct Unit {
    value: Value,
    request: CompileRequest,
    name: String,
    project: PathBuf,
    candidate: Option<PathBuf>,
}
fn unit(root: &Path, name: &str, oracle: &Path, candidate: Option<PathBuf>) -> Result<Unit> {
    let root = root.canonicalize().context("project directory missing")?;
    let config: Value = serde_json::from_slice(&fs::read(root.join("objdiff.json"))?)?;
    let unit = config["units"]
        .as_array()
        .context("objdiff.json has no units")?
        .iter()
        .find(|u| u["name"] == name)
        .with_context(|| format!("Unknown unit {name}"))?
        .clone();
    let source = unit["metadata"]["source_path"]
        .as_str()
        .filter(|s| !s.is_empty())
        .with_context(|| format!("{name} has no source"))?;
    let output = unit["base_path"]
        .as_str()
        .context("unit has no base_path")?;
    let (rule, _) = ninja::find_output_rule(&root.join("build.ninja"), output)?;
    let version = Version::parse(
        rule.mw_version
            .as_deref()
            .context("unit has no mw_version")?,
    )?;
    let mut flags = rule.flags;
    let compile_source = if let Some(candidate) = &candidate {
        flags.extend([
            "-i".into(),
            Path::new(source)
                .parent()
                .context("source has no parent")?
                .to_string_lossy()
                .into_owned(),
        ]);
        project::display_path(&candidate.canonicalize()?)
    } else {
        source.into()
    };
    Ok(Unit {
        value: unit,
        name: name.into(),
        project: root.clone(),
        candidate,
        request: CompileRequest {
            root: oracle.into(),
            working_directory: root,
            source: compile_source,
            flags,
            version,
            options: TraceOptions::default(),
            overrides: vec![],
            check: true,
            original_fallback: false,
            cut_limit: None,
            merge_limit: None,
            keep_object: None,
            timeout: Duration::from_secs(600),
        },
    })
}
#[derive(Default)]
struct Args {
    project: Option<PathBuf>,
    unit: Option<String>,
    query: Option<String>,
    oracle: Option<PathBuf>,
    candidate: Option<PathBuf>,
    objdiff: Option<PathBuf>,
    pcode: bool,
    inlining: bool,
    full: bool,
    json: bool,
    version: Option<Version>,
    trace: Option<PathBuf>,
    diff: Option<PathBuf>,
    extra: Vec<String>,
}
fn args(args: &[String], run: bool) -> Result<Args> {
    let mut out = Args::default();
    let mut i = 0;
    while i < args.len() {
        let option = &args[i];
        match option.as_str() {
            "--" => {
                out.extra.extend_from_slice(&args[i + 1..]);
                break;
            }
            "--pcode" => out.pcode = true,
            "--inlining" => out.inlining = true,
            "--full" => out.full = true,
            "--json" => out.json = true,
            "--fast" if run => out.extra.push("--no-check".into()),
            "--project" | "--unit" | "-u" | "--function" | "-f" | "--oracle-root"
            | "--candidate" | "--objdiff" | "--version" | "--trace" | "--diff-json" => {
                i += 1;
                let v = args
                    .get(i)
                    .with_context(|| format!("{option} requires a value"))?;
                match option.as_str() {
                    "--project" => out.project = Some(v.into()),
                    "--unit" | "-u" => out.unit = Some(v.clone()),
                    "--function" | "-f" => out.query = Some(v.clone()),
                    "--oracle-root" => out.oracle = Some(v.into()),
                    "--candidate" => out.candidate = Some(v.into()),
                    "--objdiff" => out.objdiff = Some(v.into()),
                    "--version" => out.version = Some(Version::parse(v)?),
                    "--trace" => out.trace = Some(v.into()),
                    "--diff-json" => out.diff = Some(v.into()),
                    _ => unreachable!(),
                }
            }
            other => bail!("unknown unit option {other}; pass oracle view options after --"),
        }
        i += 1;
    }
    Ok(out)
}
pub fn run_unit(arguments: &[String]) -> Result<()> {
    if arguments
        .iter()
        .any(|a| matches!(a.as_str(), "--help" | "-h"))
    {
        println!(
            "mwcc-tools oracle run-unit --project DIR --unit NAME [--candidate FILE] [--oracle-root ROOT] [--fast] -- ORACLE VIEW OPTIONS\n\nChecks object equality by default; --fast explicitly skips it."
        );
        return Ok(());
    }
    let args = args(arguments, true)?;
    let project = args.project.unwrap_or(env::current_dir()?);
    let oracle = args
        .oracle
        .or_else(|| env::var_os("MWCC_ORACLE_ROOT").map(PathBuf::from))
        .unwrap_or(env::current_dir()?);
    let unit = unit(
        &project,
        args.unit.as_deref().context("run-unit requires --unit")?,
        &oracle,
        args.candidate,
    )?;
    let mut compile = vec![
        "--oracle-root".into(),
        oracle.to_string_lossy().into_owned(),
        "--working-directory".into(),
        unit.project.to_string_lossy().into_owned(),
        "--version".into(),
        unit.request.version.name().into(),
    ];
    if let Some(query) = args.query {
        compile.extend(["--function".into(), query]);
    }
    if args.json {
        compile.push("--json".into());
    }
    if args.pcode {
        compile.push("--pcode".into());
    }
    compile.extend(args.extra);
    compile.push(unit.request.source);
    compile.push("--".into());
    compile.extend(unit.request.flags);
    super::compile_trace(&compile, true)
}
pub fn regdiff(arguments: &[String]) -> Result<()> {
    if arguments
        .iter()
        .any(|a| matches!(a.as_str(), "--help" | "-h"))
    {
        println!(
            "mwcc-tools oracle regdiff --project DIR --unit NAME --function QUERY [--candidate FILE] [--pcode] [--inlining] [--full] [--json]\n  Offline: --trace FILE --diff-json FILE --version V --function QUERY\nUses OBJDIFF_CLI or the project's build/tools/objdiff-cli with functionRelocDiffs=data_value."
        );
        return Ok(());
    }
    let args = args(arguments, false)?;
    let query = args
        .query
        .as_deref()
        .context("regdiff requires --function")?;
    let mut provenance = None;
    let (parsed, data, version, unit, compiled, same) = if let Some(trace) = &args.trace {
        let parsed = trace::parse(&fs::read_to_string(trace)?)?;
        let data: Value = serde_json::from_slice(&fs::read(
            args.diff
                .as_deref()
                .context("--trace requires --diff-json")?,
        )?)?;
        (
            parsed,
            data,
            args.version.unwrap_or(Version::Gc132),
            None,
            None,
            None,
        )
    } else {
        ensure!(args.diff.is_none(), "--diff-json requires --trace");
        let root = args.project.clone().unwrap_or(env::current_dir()?);
        let oracle = args
            .oracle
            .clone()
            .or_else(|| env::var_os("MWCC_ORACLE_ROOT").map(PathBuf::from))
            .unwrap_or(env::current_dir()?);
        let mut unit = unit(
            &root,
            args.unit.as_deref().context("regdiff requires --unit")?,
            &oracle,
            args.candidate.clone(),
        )?;
        unit.request.check = false;
        let compiled = runner::compile(&unit.request)?;
        let same = fs::read(path(
            &unit.project,
            unit.value["base_path"].as_str().unwrap(),
        ))
        .is_ok_and(|b| b == compiled.object);
        let scratch = tempfile::TempDir::new()?;
        let object = scratch.path().join("oracle.o");
        fs::write(&object, &compiled.object)?;
        let destination = scratch.path().join("diff.json");
        let objdiff = args
            .objdiff
            .clone()
            .or_else(|| env::var_os("OBJDIFF_CLI").map(PathBuf::from))
            .unwrap_or_else(|| {
                let p = unit.project.join("build/tools/objdiff-cli");
                if p.is_file() {
                    p
                } else {
                    p.with_extension("exe")
                }
            });
        let target = path(
            &unit.project,
            unit.value["target_path"]
                .as_str()
                .context("unit has no target_path")?,
        );
        let output = Command::new(&objdiff)
            .args(["diff", "-c", "functionRelocDiffs=data_value", "-1"])
            .arg(target)
            .arg("-2")
            .arg(&object)
            .arg("-o")
            .arg(&destination)
            .args(["--format", "json"])
            .current_dir(&unit.project)
            .output()
            .with_context(|| format!("launch {}", objdiff.display()))?;
        ensure!(
            output.status.success(),
            "objdiff failed: {}",
            String::from_utf8_lossy(&output.stderr)
        );
        let data: Value = serde_json::from_slice(&fs::read(destination)?)?;
        let parsed = trace::parse(&compiled.trace)?;
        let version = compiled.version;
        provenance = Some(
            json!({"producer_sha256":compiled.producer_sha256,"source_sha256":compiled.source_sha256,"object_sha256":compiled.object_sha256,"flags":compiled.flags,"project_object_identical":same,"relocation_policy":"functionRelocDiffs=data_value"}),
        );
        (
            parsed,
            data,
            version,
            Some(unit),
            Some(compiled),
            Some(same),
        )
    };
    let mut document = regdiff::analyze(&data, &parsed, version, query)?;
    let index = document["allocation_index"].as_u64().unwrap() as usize;
    let allocation = &parsed.allocations[index];
    if let Some(provenance) = provenance {
        document["compilation"] = provenance;
    }
    if args.json {
        document["version"] = json!(version);
        if args.pcode {
            document["pcode"] = json!(report::pcode(
                &allocation.before,
                version::opcode_names(version)
            ));
        }
        println!("{}", serde_json::to_string_pretty(&document)?);
        return Ok(());
    }
    if let (Some(unit), Some(_compiled)) = (&unit, &compiled) {
        let executable = unit
            .request
            .root
            .join(format!("build/{}/oracle/mwcceppc.exe", version.name()));
        let symbol = &document["symbol"];
        let readable = symbol["demangled_name"]
            .as_str()
            .filter(|s| !s.is_empty())
            .unwrap_or(symbol["name"].as_str().unwrap());
        println!(
            "oracle {} ({}); unit {}; {readable}",
            version.name(),
            project::display_path(&executable),
            unit.name
        );
        if let Some(candidate) = &unit.candidate {
            println!(
                "candidate: {} (compared with the target only; the project object is of the unit source)",
                candidate.display()
            );
        } else {
            println!(
                "oracle object equals the project object: {}",
                if same == Some(true) {
                    "yes"
                } else {
                    "NO (the project object is stale: run ninja; or the oracle differs and the explanation may not hold)"
                }
            );
        }
    }
    if document["target"].is_null() {
        println!("no target symbol paired with this function");
    } else {
        println!(
            "match {:.2}%  (trace allocation #{index}, by {})",
            document["symbol"]["match_percent"].as_f64().unwrap_or(0.0),
            document["selection"].as_str().unwrap()
        );
    }
    if args.inlining {
        let previous = if index == 0 {
            0
        } else {
            parsed.allocations[index - 1].inliner_position
        };
        let decisions = inlining::decisions(
            &parsed.inliner[previous..allocation.inliner_position],
            &BTreeMap::new(),
        )?;
        println!("\n== inlining (call: decision - reason) ==");
        let lines = report::inlining(&decisions, false);
        println!(
            "{}",
            if lines.is_empty() {
                "   no calls".into()
            } else {
                lines.join("\n")
            }
        );
    }
    if args.pcode {
        println!();
        println!(
            "{}",
            report::pcode(&allocation.before, version::opcode_names(version)).join("\n")
        );
    }
    let lines = regdiff::report(&document, args.full, args.inlining);
    if !lines.is_empty() {
        println!("{}", lines.join("\n"));
    }
    Ok(())
}

pub fn colouring_unit(arguments: &[String]) -> Result<()> {
    let commands = ["verify", "target", "force", "search", "replay", "remap"];
    if arguments
        .iter()
        .any(|a| matches!(a.as_str(), "--help" | "-h"))
        && !arguments.iter().any(|a| commands.contains(&a.as_str()))
    {
        println!(
            "mwcc-tools colouring unit --project DIR --unit NAME [--candidate FILE] [--oracle-root ROOT] COMMAND [COLOURING OPTIONS]"
        );
        return Ok(());
    }
    let index = arguments
        .iter()
        .position(|a| commands.contains(&a.as_str()))
        .context("colouring unit requires verify, target, force, search, replay or remap")?;
    let options = args(&arguments[..index], false)?;
    let project = options.project.unwrap_or(env::current_dir()?);
    let oracle = options
        .oracle
        .or_else(|| env::var_os("MWCC_ORACLE_ROOT").map(PathBuf::from))
        .unwrap_or(env::current_dir()?);
    let unit = unit(
        &project,
        options
            .unit
            .as_deref()
            .context("colouring unit requires --unit")?,
        &oracle,
        options.candidate,
    )?;
    let mut child = arguments[index..].to_vec();
    child.extend([
        "--oracle-root".into(),
        oracle.to_string_lossy().into_owned(),
        "--from-ninja".into(),
        unit.project
            .join("build.ninja")
            .to_string_lossy()
            .into_owned(),
    ]);
    if !child
        .iter()
        .any(|a| a == "--version" || a.starts_with("--version="))
    {
        child.extend(["--version".into(), unit.request.version.name().into()]);
    }
    if let Some(candidate) = unit.candidate {
        child.extend([
            "--override".into(),
            format!(
                "{}={}",
                unit.value["metadata"]["source_path"].as_str().unwrap(),
                project::display_path(&candidate.canonicalize()?)
            ),
        ]);
    }
    if !child
        .iter()
        .any(|a| a == "--load" || a.starts_with("--load="))
    {
        child.push(
            unit.value["metadata"]["source_path"]
                .as_str()
                .unwrap()
                .to_owned(),
        );
    }
    super::colouring_command(&child)
}
