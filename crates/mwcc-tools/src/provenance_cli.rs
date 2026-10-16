use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::{evidence::EvaluationState, provenance};
use std::fs;

fn captured_version(input: &str) -> Result<Option<mwcc_oracle_core::version::Version>> {
    if !input.trim_start().starts_with('{') {
        return Ok(None);
    }
    let value: serde_json::Value = serde_json::from_str(input)?;
    let version = value
        .get("compilation")
        .and_then(|c| c.get("version"))
        .or_else(|| value.get("version"))
        .and_then(|v| v.as_str());
    version
        .map(mwcc_oracle_core::version::Version::parse)
        .transpose()
}
fn compare(args: &[String]) -> Result<()> {
    use mwcc_oracle_core::{
        source_guided::correspondence::{self, Boundary, Selection},
        version::Version,
    };
    let first = args
        .first()
        .context("provenance compare needs FIRST SECOND")?;
    let second = args.get(1).context("provenance compare needs SECOND")?;
    let mut link = None;
    let mut second_link = None;
    let mut class = 4;
    let mut first_round = 0;
    let mut second_round = 0;
    let mut version = None;
    let mut json = false;
    let mut i = 2;
    while i < args.len() {
        let option = &args[i];
        i += 1;
        if option == "--json" {
            json = true;
            continue;
        }
        let value = args
            .get(i)
            .context("provenance compare option needs a value")?;
        i += 1;
        match option.as_str() {
            "--link" => link = Some(value.clone()),
            "--second-link" => second_link = Some(value.clone()),
            "--class" => class = value.parse()?,
            "--first-round" => first_round = value.parse()?,
            "--second-round" => second_round = value.parse()?,
            "--version" => version = Some(Version::parse(value)?),
            other => bail!("unknown provenance compare option {other}"),
        }
    }
    let first = fs::read_to_string(first)?;
    let second = fs::read_to_string(second)?;
    let a_version = captured_version(&first)?;
    let b_version = captured_version(&second)?;
    ensure!(
        !matches!((a_version,b_version),(Some(a),Some(b)) if a!=b),
        "cross-version graph comparisons are unsupported"
    );
    let version = if let Some(v) = version {
        ensure!(
            a_version.is_none_or(|a| a == v) && b_version.is_none_or(|b| b == v),
            "--version disagrees with compiler capture"
        );
        v
    } else {
        ensure!(
            a_version.is_some() && a_version == b_version,
            "raw traces or missing/different compiler identities require --version"
        );
        a_version.unwrap()
    };
    let link = link.context("provenance compare requires --link NAME")?;
    let first_selection = Selection {
        link: link.clone(),
        class,
        round: first_round,
        boundary: Boundary::BeforeSimplify,
    };
    let second_selection = Selection {
        link: second_link.unwrap_or(link),
        class,
        round: second_round,
        boundary: Boundary::BeforeSimplify,
    };
    let report = correspondence::compare(
        &provenance::inspect(&first)?,
        &first_selection,
        &provenance::inspect(&second)?,
        &second_selection,
        version,
    )?;
    if json {
        println!("{}", serde_json::to_string_pretty(&report)?);
    } else {
        println!(
            "Observed before_simplify graphs: {} nodes -> {} nodes; capture {:?}",
            report.first.nodes.len(),
            report.second.nodes.len(),
            report.coverage.state
        );
        println!(
            "{} conditional injective pairs, {} unmatched first, {} unmatched second, {} conflicts",
            report.mapping.len(),
            report.unmatched_first.len(),
            report.unmatched_second.len(),
            report.conflicts.len()
        );
        println!(
            "{} mapped neighbor multiplicity differences; {} first and {} second unclassified boundary edges",
            report.neighbor_differences.len(),
            report.boundary_first.len(),
            report.boundary_second.len()
        );
        println!(
            "Source/symbol anchors and instruction votes are conditional. Semantic lifetime correspondence and elimination remain indeterminate."
        );
    }
    Ok(())
}

pub fn run(args: &[String]) -> Result<()> {
    if args.first().map(String::as_str) == Some("compare") {
        return compare(&args[1..]);
    }
    if matches!(
        args.first().map(String::as_str),
        Some("audit-counters" | "audit-dispatch")
    ) {
        let mut root = std::env::current_dir()?;
        let mut version = "GC_2_7".to_owned();
        let mut i = 1;
        while i < args.len() {
            let option = &args[i];
            i += 1;
            let value = args
                .get(i)
                .context("provenance audit option needs a value")?;
            match option.as_str() {
                "--root" => root = value.into(),
                "--version" => version = value.clone(),
                x => bail!("unknown audit option {x}"),
            }
            i += 1;
        }
        let audit = if args[0] == "audit-dispatch" {
            serde_json::to_value(mwcc_oracle_core::dispatch_audit::capture(&root, &version)?)?
        } else {
            serde_json::to_value(mwcc_oracle_core::backend_audit::capture(&root, &version)?)?
        };
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
