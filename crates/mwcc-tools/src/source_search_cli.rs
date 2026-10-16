use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::{
    runner::Compilation,
    source_guided::{
        search::{self, Budget},
        state,
    },
};
use std::{fs, path::PathBuf};
pub fn run(args: &[String]) -> Result<()> {
    if args.first().map(String::as_str) == Some("state") {
        let path = args
            .get(1)
            .context("source-search state needs a capture JSON file")?;
        let mut object = None;
        let mut link = None;
        let mut class = 4;
        let mut round = 0;
        let mut i = 2;
        while i < args.len() {
            let option = &args[i];
            let value = args.get(i + 1).context("state option needs a value")?;
            match option.as_str() {
                "--object" => object = Some(value),
                "--link" => link = Some(value),
                "--class" => class = value.parse()?,
                "--round" => round = value.parse()?,
                _ => bail!("unknown state option {option}"),
            }
            i += 2;
        }
        let mut capture: serde_json::Value = serde_json::from_slice(&fs::read(path)?)?;
        let mut compilation: Compilation = serde_json::from_value(capture["compilation"].take())?;
        compilation.object = fs::read(object.context("state needs --object FILE")?)?;
        let report = state::inspect(
            &compilation,
            link.context("state needs --link NAME")?,
            class,
            round,
        )?;
        println!("{}", serde_json::to_string_pretty(&report)?);
        return Ok(());
    }
    if args.first().map(String::as_str) == Some("replay") {
        let output = PathBuf::from(
            args.get(1)
                .context("source-search replay needs a run directory")?,
        );
        let id = args
            .get(2)
            .context("replay needs a candidate ID")?
            .parse()?;
        ensure!(args.len() == 3, "use source-search replay DIRECTORY ID");
        let report = search::replay(
            &output,
            id,
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
        "use source-search run PLAN --output DIRECTORY [--resume] [--candidates N] [--compilations N] [--seconds N] [--cancel-file PATH]"
    );
    let path = PathBuf::from(
        args.get(1)
            .context("source-search run needs a plan JSON file")?,
    );
    let plan = search::load_plan(&path)?;
    let mut output = None;
    let mut resume = false;
    let mut candidates = 100usize;
    let mut compilations = 100usize;
    let mut seconds = 600u64;
    let mut cancel = None;
    let mut i = 2;
    while i < args.len() {
        let option = &args[i];
        i += 1;
        if option == "--resume" {
            resume = true;
            continue;
        }
        let value = args.get(i).context("source-search option needs a value")?;
        i += 1;
        match option.as_str() {
            "--output" => output = Some(PathBuf::from(value)),
            "--candidates" => candidates = value.parse()?,
            "--compilations" => compilations = value.parse()?,
            "--seconds" => seconds = value.parse()?,
            "--cancel-file" => cancel = Some(PathBuf::from(value)),
            _ => bail!("unknown source-search option {option}"),
        }
    }
    let output = output.context("source-search needs --output")?;
    let cancel_file = cancel.unwrap_or_else(|| output.join("CANCEL"));
    let report = search::run(
        &plan,
        &output,
        &Budget {
            candidates,
            compilations,
            seconds,
            cancel_file,
        },
        resume,
    )?;
    fs::write(
        output.join("last-status.json"),
        serde_json::to_vec_pretty(&report)?,
    )?;
    println!("{}", serde_json::to_string_pretty(&report)?);
    Ok(())
}
