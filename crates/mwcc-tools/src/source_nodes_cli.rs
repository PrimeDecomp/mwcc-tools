use anyhow::{Context, Result, ensure};
use mwcc_oracle_core::source_nodes;
pub fn run(args: &[String]) -> Result<()> {
    let command = args
        .first()
        .context("source-nodes needs inspect, snapshot, write or clone")?;
    let path = args
        .get(1)
        .context("source-nodes needs a raw trace or compilation capture")?;
    let history = source_nodes::parse(&crate::source_arenas_cli::load_trace(path)?)?;
    if command == "inspect" {
        ensure!(
            args.len() == 2 || (args.len() == 3 && args[2] == "--json"),
            "source-nodes inspect CAPTURE [--json]"
        );
        println!("{}", serde_json::to_string_pretty(&history)?);
        return Ok(());
    }
    ensure!(
        args.len() == 3,
        "source-nodes snapshot|write|clone CAPTURE ID"
    );
    let id = args[2].parse::<u32>()?;
    let prepared = history.prepare();
    let result = match command.as_str() {
        "snapshot" => serde_json::to_value(prepared.snapshot(id))?,
        "write" => serde_json::to_value(prepared.source_write(id))?,
        "clone" => serde_json::to_value(prepared.clone_edge(id))?,
        _ => anyhow::bail!("unknown source-nodes command {command}"),
    };
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}
