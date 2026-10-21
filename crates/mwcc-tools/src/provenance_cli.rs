//! The oracle's maintenance commands: the audits, the counter table and the probe guards.
use anyhow::{Context, Result, bail};

/// `provenance counter-sites [--write] [--root DIR]`: the WebAssembly oracle's counter table, checked against (or
/// written to) its file.
fn counter_sites(args: &[String]) -> Result<()> {
    use mwcc_oracle_core::counter_sources;
    let mut root = std::env::current_dir()?;
    let mut write = false;
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--write" => write = true,
            "--root" => root = crate::option_value(args, &mut i)?.into(),
            other => anyhow::bail!("unknown counter-sites argument {other}"),
        }
        i += 1;
    }
    let table = counter_sources::generate(&root)?;
    let text = counter_sources::render(&table);
    let path = root.join(counter_sources::OUTPUT);
    println!(
        "{} increments, {} writes without one, {} inlined functions",
        table.sites.len(),
        table.none.len(),
        table.inlined.len()
    );
    if write {
        std::fs::write(&path, text)?;
        println!("wrote {}", path.display());
    } else if std::fs::read_to_string(&path)
        .unwrap_or_default()
        .replace('\r', "")
        != text
    {
        anyhow::bail!(
            "{} is stale: run provenance counter-sites --write",
            path.display()
        );
    }
    Ok(())
}

pub fn run(args: &[String]) -> Result<()> {
    if args.first().map(String::as_str) == Some("counter-sites") {
        return counter_sites(&args[1..]);
    }
    if args.first().map(String::as_str) == Some("derive-guards") {
        let mut root = std::env::current_dir()?;
        let mut version = None;
        let mut names = vec![];
        let mut i = 1;
        while i < args.len() {
            match args[i].as_str() {
                option @ ("--root" | "--version") => {
                    let value = crate::option_value(args, &mut i)?;
                    if option == "--root" {
                        root = value.into();
                    } else {
                        version = Some(value.clone());
                    }
                }
                name => names.push(name.to_owned()),
            }
            i += 1;
        }
        let version = version.context("derive-guards needs --version")?;
        let guards = mwcc_oracle_core::source_arena_probe::derive_guards(&root, &version, &names)?;
        println!("{}", serde_json::to_string_pretty(&guards)?);
        return Ok(());
    }
    if matches!(
        args.first().map(String::as_str),
        Some("audit-counters" | "audit-dispatch" | "audit-source-arenas")
    ) {
        let mut root = std::env::current_dir()?;
        let mut version = "GC_2_7".to_owned();
        let mut i = 1;
        while i < args.len() {
            let option = &args[i];
            let value = crate::option_value(args, &mut i)?;
            match option.as_str() {
                "--root" => root = value.into(),
                "--version" => version = value.clone(),
                x => bail!("unknown audit option {x}"),
            }
            i += 1;
        }
        let audit = if args[0] == "audit-source-arenas" {
            serde_json::to_value(mwcc_oracle_core::source_arena_audit::capture(
                &root, &version,
            )?)?
        } else if args[0] == "audit-dispatch" {
            serde_json::to_value(mwcc_oracle_core::dispatch_audit::capture(&root, &version)?)?
        } else {
            serde_json::to_value(mwcc_oracle_core::backend_audit::capture(&root, &version)?)?
        };
        println!("{}", serde_json::to_string_pretty(&audit)?);
        return Ok(());
    }
    bail!(
        "use provenance audit-counters|audit-dispatch|audit-source-arenas, counter-sites or derive-guards (provenance.inspect and provenance.compare are methods)"
    )
}
