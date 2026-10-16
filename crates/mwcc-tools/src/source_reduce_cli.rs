use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::source_guided::{reduction, search::Budget};
use std::path::PathBuf;

pub fn run(args: &[String]) -> Result<()> {
    if args.first().map(String::as_str) == Some("replay") {
        ensure!(args.len() == 3, "use source-reduce replay DIRECTORY ID");
        let output = PathBuf::from(&args[1]);
        let report = reduction::replay(
            &output,
            args[2].parse()?,
            &Budget {
                candidates: 1,
                compilations: 3,
                seconds: 600,
                cancel_file: output.join("CANCEL"),
            },
        )?;
        println!("{}", serde_json::to_string_pretty(&report)?);
        return Ok(());
    }
    ensure!(
        args.first().map(String::as_str) == Some("run"),
        "use source-reduce run PLAN --output DIRECTORY [--resume] [--candidates N] [--compilations N] [--seconds N] [--cancel-file PATH]"
    );
    let plan = reduction::load_plan(&PathBuf::from(
        args.get(1).context("source-reduce needs a plan")?,
    ))?;
    let mut output = None;
    let mut resume = false;
    let mut candidates = 100;
    let mut compilations = 100;
    let mut seconds = 600;
    let mut cancel = None;
    let mut i = 2;
    while i < args.len() {
        let option = &args[i];
        i += 1;
        if option == "--resume" {
            resume = true;
            continue;
        }
        let value = args.get(i).context("source-reduce option needs a value")?;
        i += 1;
        match option.as_str() {
            "--output" => output = Some(PathBuf::from(value)),
            "--candidates" => candidates = value.parse()?,
            "--compilations" => compilations = value.parse()?,
            "--seconds" => seconds = value.parse()?,
            "--cancel-file" => cancel = Some(PathBuf::from(value)),
            _ => bail!("unknown source-reduce option {option}"),
        }
    }
    let output = output.context("source-reduce needs --output")?;
    let report = reduction::run(
        &plan,
        &output,
        &Budget {
            candidates,
            compilations,
            seconds,
            cancel_file: cancel.unwrap_or_else(|| output.join("CANCEL")),
        },
        resume,
    )?;
    println!("{}", serde_json::to_string_pretty(&report)?);
    Ok(())
}
