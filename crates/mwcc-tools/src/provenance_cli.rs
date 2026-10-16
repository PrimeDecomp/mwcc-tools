use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::{evidence::EvaluationState, provenance};
use std::fs;

pub fn run(args: &[String]) -> Result<()> {
    if args.first().map(String::as_str) == Some("audit-counters") {
        let mut root = std::env::current_dir()?;
        let mut version = "GC_2_7".to_owned();
        let mut i = 1;
        while i < args.len() {
            let option = &args[i];
            i += 1;
            let value = args.get(i).context("audit-counters option needs a value")?;
            match option.as_str() {
                "--root" => root = value.into(),
                "--version" => version = value.clone(),
                x => bail!("unknown audit option {x}"),
            }
            i += 1;
        }
        let audit = mwcc_oracle_core::backend_audit::capture(&root, &version)?;
        println!("{}", serde_json::to_string_pretty(&audit)?);
        return Ok(());
    }
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
    let mut report = provenance::inspect(&input)?;
    if let Some(link) = link {
        report.passes.retain(|p| p.link == *link);
        report.history.functions.retain(|f| f.link == *link);
        report.graphs.graphs.retain(|g| g.link == *link);
        report.graphs.functions.retain(|f| f.link == *link);
        ensure!(
            !report.passes.is_empty() || !report.history.functions.is_empty(),
            "no provenance for {link}"
        );
        // Filtering is a view; the report-wide coverage remains conservative.
    }
    if json {
        println!("{}", serde_json::to_string_pretty(&report)?);
    } else {
        for function in &report.history.functions {
            let reused = function
                .registers
                .iter()
                .filter(|r| r.values.len() > 1)
                .count();
            println!(
                "{}: function {}, {} births, {} reused register numbers, direct-counter observations {:?}",
                function.link,
                function.id,
                function.births.len(),
                reused,
                function.counter_observations.state
            );
        }
        for graph in &report.graphs.graphs {
            println!(
                "{}: graph {}, class {}, {} nodes, {} absent from current instructions, observations {:?}",
                graph.link,
                graph.id,
                graph.class,
                graph.nodes.len(),
                graph.graph_only.len(),
                graph.observations.state
            );
        }
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
        println!("Complete lifecycle/category/leaf-origin coverage: incomplete");
    }
    Ok(())
}
