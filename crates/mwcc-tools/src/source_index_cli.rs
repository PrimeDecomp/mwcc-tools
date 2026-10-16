use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::source_guided::index::{self, Ingest, Query};
use std::{
    fs,
    path::{Path, PathBuf},
};

fn read(path: &str) -> Result<serde_json::Value> {
    Ok(serde_json::from_slice(&fs::read(path)?)?)
}
fn resolve(path: &mut PathBuf, base: &Path) {
    if !path.is_absolute() {
        *path = base.join(&*path);
    }
}
pub fn run(args: &[String]) -> Result<()> {
    let command=args.first().map(String::as_str).context("use source-index ingest SPEC --index DIR | query QUERY --index DIR | verify QUERY ID --index DIR --output DIR")?;
    let spec = args
        .get(1)
        .context("source-index requires a JSON specification")?;
    let mut i = 2;
    let id = if command == "verify" {
        i = 3;
        Some(args.get(2).context("verify needs an entry ID")?)
    } else {
        None
    };
    let mut root = None;
    let mut output = None;
    let mut seconds = 600;
    let mut cancel = None;
    while i < args.len() {
        let value = args
            .get(i + 1)
            .context("source-index option needs a value")?;
        match args[i].as_str() {
            "--index" => root = Some(PathBuf::from(value)),
            "--output" => output = Some(PathBuf::from(value)),
            "--seconds" => seconds = value.parse()?,
            "--cancel-file" => cancel = Some(PathBuf::from(value)),
            other => bail!("unknown source-index option {other}"),
        }
        i += 2;
    }
    let root = root.context("source-index requires --index DIR")?;
    let result = match command {
        "ingest" => {
            ensure!(
                output.is_none() && cancel.is_none(),
                "ingest does not accept verification options"
            );
            let mut ingest: Ingest = serde_json::from_value(read(spec)?)?;
            let path = Path::new(spec).canonicalize()?;
            let base = path.parent().unwrap();
            for path in [
                &mut ingest.capture,
                &mut ingest.object,
                &mut ingest.target,
                &mut ingest.source,
                &mut ingest.build.oracle_root,
                &mut ingest.build.working_directory,
            ] {
                resolve(path, base);
            }
            if let Some(path) = &mut ingest.snapshot {
                resolve(path, base);
            }
            if let Some(path) = &mut ingest.build.from_ninja {
                resolve(path, base);
            }
            serde_json::to_value(index::ingest(&root, &ingest)?)?
        }
        "query" => {
            ensure!(
                output.is_none() && cancel.is_none(),
                "query does not accept verification options"
            );
            let query: Query = serde_json::from_value(read(spec)?)?;
            serde_json::to_value(index::query(&root, &query)?)?
        }
        "verify" => {
            let output = output.context("verify requires --output DIR")?;
            let cancel = cancel.unwrap_or_else(|| output.join("CANCEL"));
            let query: Query = serde_json::from_value(read(spec)?)?;
            serde_json::to_value(index::verify(
                &root,
                id.unwrap(),
                &query,
                &output,
                seconds,
                &cancel,
            )?)?
        }
        _ => bail!("unknown source-index command {command}"),
    };
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}
