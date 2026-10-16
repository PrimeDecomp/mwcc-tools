use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::{evidence::EvaluationState, provenance};
use std::fs;

pub fn run(args: &[String]) -> Result<()> {
    ensure!(
        args.first().map(String::as_str) == Some("inspect"),
        "use provenance inspect TRACE [--link NAME] [--json]"
    );
    let path = args
        .get(1)
        .context("provenance inspect needs a raw trace or compile-trace JSON file")?;
    let mut link = None;
    let mut json = false;
    let mut i = 2;
    while i < args.len() {
        match args[i].as_str() {
            "--json" => json = true,
            "--link" => {
                i += 1;
                link = Some(args.get(i).context("--link needs a mangled name")?);
            }
            x => bail!("unknown provenance option {x}"),
        }
        i += 1;
    }
    let input = fs::read_to_string(path)?;
    let trace = if input.trim_start().starts_with('{') {
        let value: serde_json::Value = serde_json::from_str(&input)?;
        value
            .pointer("/compilation/trace")
            .or_else(|| value.get("trace"))
            .and_then(|v| v.as_str())
            .context("JSON lacks compilation.trace or trace")?
            .to_owned()
    } else {
        input
    };
    let mut report = provenance::parse(&trace)?;
    if let Some(link) = link {
        report.passes.retain(|p| p.link == *link);
        ensure!(!report.passes.is_empty(), "no coalescing pass for {link}");
        // Filtering is a view; the report-wide coverage remains conservative.
    }
    if json {
        println!("{}", serde_json::to_string_pretty(&report)?);
    } else {
        for pass in &report.passes {
            println!(
                "{}: pass {}, class {}, {} candidates, {}",
                pass.link,
                pass.id,
                pass.class,
                pass.candidates.len(),
                if pass.decisions.state == EvaluationState::Satisfied {
                    "decisions complete"
                } else {
                    "incomplete evidence"
                }
            );
            for candidate in &pass.candidates {
                let explanation = match &candidate.decision {
                    provenance::Decision::Rejected { condition } => {
                        format!("rejected: {condition}")
                    }
                    provenance::Decision::AlreadySameRoot { root } => {
                        format!("already the same root ({root}); copy unlinked")
                    }
                    provenance::Decision::Merged { parent, child } => {
                        format!("merged {child} into {parent}; parent write confirmed")
                    }
                    provenance::Decision::Indeterminate => "missing deciding observation".into(),
                };
                println!(
                    "  {} @ {}: {}{}",
                    candidate.id,
                    candidate.instruction,
                    explanation,
                    if candidate.evidence.state == EvaluationState::Satisfied {
                        ""
                    } else {
                        " (incomplete evidence)"
                    }
                );
            }
        }
        println!("Creation/reuse/category coverage: missing");
    }
    Ok(())
}
