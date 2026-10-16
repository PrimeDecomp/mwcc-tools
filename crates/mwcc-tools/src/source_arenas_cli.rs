use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::{source_arenas, source_lifetimes::PoolDomain};
use serde::Deserialize;
use std::fs;

pub(crate) fn load_trace(path: &str) -> Result<String> {
    let input = fs::read_to_string(path)?;
    if input.trim_start().starts_with('{') {
        #[derive(Deserialize)]
        struct Capture {
            trace: Option<String>,
            compilation: Option<Trace>,
        }
        #[derive(Deserialize)]
        struct Trace {
            trace: String,
        }
        let value: Capture = serde_json::from_str(&input)?;
        Ok(value
            .compilation
            .map(|c| c.trace)
            .or(value.trace)
            .context("capture lacks trace")?)
    } else {
        Ok(input)
    }
}
pub fn run(args: &[String]) -> Result<()> {
    let command = args
        .first()
        .context("source-arenas needs inspect or resolve")?;
    let path = args
        .get(1)
        .context("source-arenas needs a raw trace or compile capture")?;
    let history = source_arenas::parse(&load_trace(path)?)?;
    if command == "inspect" {
        ensure!(
            args.len() == 2 || (args.len() == 3 && args[2] == "--json"),
            "source-arenas inspect CAPTURE [--json]"
        );
        println!("{}", serde_json::to_string_pretty(&history)?);
        return Ok(());
    }
    if command == "resolve-many" {
        ensure!(
            args.len() == 4 && args[2] == "--queries",
            "source-arenas resolve-many CAPTURE --queries QUERIES.json"
        );
        let queries: Vec<source_arenas::Query> = serde_json::from_slice(&fs::read(&args[3])?)?;
        let results = history.prepare().resolve_many(&queries);
        println!("{}", serde_json::to_string_pretty(&results)?);
        return Ok(());
    }
    ensure!(
        command == "resolve",
        "unknown source-arenas command {command}"
    );
    let (mut position, mut pool, mut address, mut extent) = (None, None, None, None);
    let mut i = 2;
    while i < args.len() {
        let value = args
            .get(i + 1)
            .context("source-arenas resolve option requires a value")?;
        match args[i].as_str() {
            "--position" => position = Some(value.parse::<usize>()?),
            "--pool" => {
                pool = Some(match value.as_str() {
                    "heap" => PoolDomain::Heap,
                    "optimizer" => PoolDomain::Optimizer,
                    "block" => PoolDomain::Block,
                    "local" => PoolDomain::Local,
                    "global" => PoolDomain::Global,
                    other => bail!("unknown arena pool {other}"),
                })
            }
            "--address" => address = Some(mwcc_oracle_core::executable::hex(value)?),
            "--extent" => extent = Some(value.parse::<u32>()?),
            other => bail!("unknown source-arenas resolve option {other}"),
        }
        i += 2;
    }
    let result = history.resolve_at(
        position.context("missing --position (one-based trace line)")?,
        pool.context("missing --pool")?,
        address.context("missing --address (hex)")?,
        extent.context("missing --extent")?,
    );
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}
