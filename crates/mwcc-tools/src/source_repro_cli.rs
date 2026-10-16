use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::source_guided::{
    reproducer::{self, RunKind},
    search::Budget,
};
use std::path::PathBuf;

pub fn run(args: &[String]) -> Result<()> {
    match args.first().map(String::as_str) {
        Some("export") => {
            let kind = match args.get(1).map(String::as_str) {
                Some("search") => RunKind::Search,
                Some("reduction") => RunKind::Reduction,
                _ => bail!(
                    "use source-repro export search|reduction RUN ID --output NEW [--run-analyzer FILE]"
                ),
            };
            let root = PathBuf::from(args.get(2).context("export needs a run directory")?);
            let id = args
                .get(3)
                .context("export needs a candidate ID")?
                .parse()?;
            let mut output = None;
            let mut analyzer = None;
            let mut i = 4;
            while i < args.len() {
                let key = &args[i];
                i += 1;
                let value = args
                    .get(i)
                    .with_context(|| format!("{key} needs a value"))?;
                i += 1;
                match key.as_str() {
                    "--output" => {
                        ensure!(output.is_none(), "duplicate output");
                        output = Some(PathBuf::from(value));
                    }
                    "--run-analyzer" => {
                        ensure!(analyzer.is_none(), "duplicate run analyzer");
                        analyzer = Some(PathBuf::from(value));
                    }
                    _ => bail!("unknown export option {key}"),
                }
            }
            let manifest = reproducer::export(
                kind,
                &root,
                id,
                &output.context("export requires --output NEW")?,
                analyzer.as_deref(),
            )?;
            println!("{}", serde_json::to_string_pretty(&manifest)?);
        }
        Some("verify") => {
            ensure!(
                args.len() == 2 || (args.len() == 4 && args[2] == "--expected-sha256"),
                "use source-repro verify BUNDLE [--expected-sha256 HASH]"
            );
            let result = reproducer::verify(&PathBuf::from(&args[1]))?;
            if args.len() == 4 {
                ensure!(
                    args[3] == result.manifest_sha256,
                    "bundle manifest does not match supplied identity"
                );
            }
            println!("{}", serde_json::to_string_pretty(&result)?);
        }
        Some("replay") => {
            let root = PathBuf::from(args.get(1).context("replay needs a bundle")?);
            let mut output = None;
            let mut seconds = 600;
            let mut compilations = 3;
            let mut cancel = None;
            let mut i = 2;
            while i < args.len() {
                let key = &args[i];
                i += 1;
                let value = args
                    .get(i)
                    .with_context(|| format!("{key} needs a value"))?;
                i += 1;
                match key.as_str() {
                    "--output" => {
                        ensure!(output.is_none(), "duplicate output");
                        output = Some(PathBuf::from(value));
                    }
                    "--seconds" => seconds = value.parse()?,
                    "--compilations" => compilations = value.parse()?,
                    "--cancel-file" => cancel = Some(PathBuf::from(value)),
                    _ => bail!("unknown replay option {key}"),
                }
            }
            let output = output.context("replay requires --output NEW")?;
            let cancel_file = cancel.unwrap_or_else(|| output.with_extension("CANCEL"));
            let result = reproducer::replay(
                &root,
                &output,
                &Budget {
                    candidates: 1,
                    compilations,
                    seconds,
                    cancel_file,
                },
            )?;
            println!("{}", serde_json::to_string_pretty(&result)?);
        }
        _ => bail!(
            "use source-repro export search|reduction RUN ID --output NEW, verify BUNDLE, or replay BUNDLE --output NEW"
        ),
    }
    Ok(())
}
