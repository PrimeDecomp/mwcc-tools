//! Evidence from guarded retail branches, distinct from inferred graph changes.
use crate::{
    evidence::{Evaluation, EvaluationState, EvidenceGap},
    trace::{self, Record},
};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::collections::BTreeMap;

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Check {
    pub trace_position: usize,
    pub condition: String,
    pub taken: bool,
    /// Raw saved inputs are retained for auditing the actual retail predicate.
    pub inputs: BTreeMap<String, String>,
}
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum Decision {
    Rejected { condition: String },
    AlreadySameRoot { root: u16 },
    Merged { parent: u16, child: u16 },
    Indeterminate,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Candidate {
    /// A pass-local observation ID, not a stable value/lifetime ID.
    pub id: u32,
    pub trace_position: usize,
    pub decision_position: Option<usize>,
    pub instruction: String,
    pub operands: BTreeMap<String, String>,
    pub checks: Vec<Check>,
    pub decision: Decision,
    pub evidence: Evaluation,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Pass {
    pub id: u32,
    pub begin_position: usize,
    #[serde(default)]
    pub end_position: Option<usize>,
    pub link: String,
    pub class: u8,
    pub header: BTreeMap<String, String>,
    pub candidates: Vec<Candidate>,
    pub ended: bool,
    pub decisions: Evaluation,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Report {
    pub format_version: u32,
    /// Digest of the exact trace parsed into this report. Empty in older reports.
    #[serde(default)]
    pub trace_sha256: String,
    pub passes: Vec<Pass>,
    pub decisions: Evaluation,
    pub lifecycle: Evaluation,
    pub history: crate::value_history::History,
    pub graphs: crate::graph_history::GraphHistory,
}
fn integer(r: &Record, field: &str) -> Result<u32> {
    r.get(field)?
        .parse()
        .with_context(|| format!("line {}: invalid {field}", r.line))
}
fn hex(fields: &BTreeMap<String, String>, name: &str) -> Option<u32> {
    u32::from_str_radix(fields.get(name)?, 16).ok()
}
fn known() -> Evaluation {
    Evaluation {
        state: EvaluationState::Satisfied,
        reasons: vec![],
    }
}
fn gap(detail: &str) -> Evaluation {
    Evaluation::indeterminate(EvidenceGap::CoalescingCoverage, detail)
}

/// Inspect a raw trace or compile-trace JSON without allocating the unrelated
/// report tree contained in large translation-unit capture documents.
pub fn inspect(input: &str) -> Result<Report> {
    if input.trim_start().starts_with('{') {
        #[derive(Deserialize)]
        struct Body {
            trace: Option<String>,
        }
        #[derive(Deserialize)]
        struct Capture {
            compilation: Option<Body>,
            trace: Option<String>,
        }
        let capture: Capture = serde_json::from_str(input)?;
        let trace = capture
            .compilation
            .and_then(|c| c.trace)
            .or(capture.trace)
            .context("JSON lacks compilation.trace or trace")?;
        parse(&trace)
    } else {
        parse(input)
    }
}

pub fn parse(text: &str) -> Result<Report> {
    let mut passes: Vec<Pass> = vec![];
    let mut supported = true;
    let mut gaps = false;
    // Whole-unit captures contain large PCode/IR streams. Parse only the relevant
    // family while retaining original line numbers for malformed-record errors.
    for (line_index, line) in text
        .lines()
        .enumerate()
        .filter(|(_, line)| line.trim_start().starts_with("pv."))
    {
        let mut r = trace::records(line)
            .pop()
            .context("empty provenance record")?;
        r.line = line_index + 1;
        if crate::value_history::is_record(&r.kind) || crate::graph_history::is_record(&r.kind) {
            continue;
        }
        if r.kind == "pv.gap" {
            gaps = true;
            continue;
        }
        if r.kind == "pv.coalesce.begin" {
            let id = integer(&r, "pass")?;
            ensure!(
                passes.iter().all(|p| p.id != id),
                "duplicate coalescing pass {id}"
            );
            supported &= integer(&r, "schema")? == 1;
            passes.push(Pass {
                id,
                begin_position: r.line,
                end_position: None,
                link: r.get("link")?.into(),
                class: integer(&r, "cls")?.try_into()?,
                header: r.fields,
                candidates: vec![],
                ended: false,
                decisions: gap("pass has no end record"),
            });
            continue;
        }
        if !r.kind.starts_with("pv.coalesce.") {
            supported = false;
            continue;
        }
        let id = integer(&r, "pass")?;
        let pass = passes
            .last_mut()
            .context("coalescing record without begin")?;
        ensure!(
            pass.id == id && !pass.ended,
            "coalescing record outside active pass {id}"
        );
        if r.kind == "pv.coalesce.end" {
            ensure!(
                integer(&r, "candidates")? as usize == pass.candidates.len(),
                "candidate count differs at pass end"
            );
            pass.ended = true;
            pass.end_position = Some(r.line);
            continue;
        }
        let id = integer(&r, "candidate")?;
        if r.kind == "pv.coalesce.candidate" {
            ensure!(
                id as usize == pass.candidates.len() + 1,
                "nonsequential coalescing candidate"
            );
            pass.candidates.push(Candidate {
                id,
                trace_position: r.line,
                decision_position: None,
                instruction: r.get("instruction")?.into(),
                operands: r.fields,
                checks: vec![],
                decision: Decision::Indeterminate,
                evidence: gap("no deciding retail observation"),
            });
            continue;
        }
        let candidate = pass
            .candidates
            .last_mut()
            .context("coalescing observation without candidate")?;
        ensure!(
            candidate.id == id && candidate.decision == Decision::Indeterminate,
            "observation after terminal coalescing decision"
        );
        match r.kind.as_str() {
            "pv.coalesce.check" => {
                let condition = r.get("condition")?.to_owned();
                let taken = integer(&r, "taken")?;
                ensure!(taken <= 1, "invalid branch outcome");
                let expected_site = match condition.as_str() {
                    "copy_flag" => 0x10f,
                    "destination_kind" => 0x17b,
                    "register_class" => 0x18b,
                    "roots_differ" => 0x1d6,
                    "interference" => 0x244,
                    "destination_below_window" => 0x271,
                    "destination_above_window" => 0x280,
                    "source_below_window" => 0x289,
                    "source_above_window" => 0x291,
                    "gpr_special_register" => 0x2b9,
                    _ => {
                        supported = false;
                        0
                    }
                };
                ensure!(
                    hex(&r.fields, "site") == Some(expected_site),
                    "branch site does not match condition"
                );
                let flags = hex(&r.fields, "eflags").context("missing/invalid saved flags")?;
                let zf = flags & 0x40 != 0;
                let signed = (flags & 0x80 != 0) != (flags & 0x800 != 0);
                let actual = match condition.as_str() {
                    "gpr_special_register" => zf,
                    "destination_below_window" | "source_below_window" => signed,
                    "destination_above_window" | "source_above_window" => !zf && !signed,
                    _ => !zf,
                };
                ensure!(
                    actual == (taken == 1),
                    "branch outcome contradicts saved flags"
                );
                if condition == "roots_differ" && taken == 0 {
                    candidate.decision = Decision::AlreadySameRoot {
                        root: hex(&r.fields, "edx").context("missing root")? as u16,
                    };
                } else if condition != "roots_differ" && taken == 1 {
                    candidate.decision = Decision::Rejected {
                        condition: condition.clone(),
                    };
                }
                candidate.checks.push(Check {
                    trace_position: r.line,
                    condition,
                    taken: taken == 1,
                    inputs: r.fields,
                });
                if candidate.decision != Decision::Indeterminate {
                    candidate.decision_position = Some(r.line);
                }
            }
            "pv.coalesce.merge" => {
                let parent: u16 = integer(&r, "parent")?.try_into()?;
                let child: u16 = integer(&r, "child")?.try_into()?;
                ensure!(
                    integer(&r, "observed_parent")? == u32::from(parent),
                    "union write was not observed"
                );
                ensure!(parent < child, "unexpected retail union root ordering");
                candidate.decision = Decision::Merged { parent, child };
                candidate.decision_position = Some(r.line);
            }
            _ => supported = false,
        }
    }
    for p in &mut passes {
        let schema_supported =
            p.header.get("schema").is_some_and(|v| v == "1") && (2..=4).contains(&p.class);
        for c in &mut p.candidates {
            c.evidence = if schema_supported && valid_path(p.class, &p.header, c) {
                known()
            } else {
                gap("incomplete or inconsistent retail decision path")
            };
        }
        if schema_supported
            && p.ended
            && p.link != "?"
            && p.candidates
                .iter()
                .all(|c| c.evidence.state == EvaluationState::Satisfied)
        {
            p.decisions = known();
        }
    }
    let decisions = if !passes.is_empty()
        && supported
        && !gaps
        && passes
            .iter()
            .all(|p| p.decisions.state == EvaluationState::Satisfied)
    {
        known()
    } else {
        gap("no complete supported coalescing capture, or missing decisions/context")
    };
    let history = crate::value_history::parse(text)?;
    let graphs = crate::graph_history::parse(text, &history, &passes)?;
    Ok(Report {
        format_version: 2,
        trace_sha256: format!("{:x}", Sha256::digest(text.as_bytes())),
        passes,
        decisions,
        lifecycle: Evaluation::indeterminate(
            EvidenceGap::BackendLifecycleCoverage,
            "direct counter observations and statement roots do not establish complete lifecycle bindings, semantic categories or full expression/inline origins",
        ),
        history,
        graphs,
    })
}

fn valid_path(class: u8, header: &BTreeMap<String, String>, c: &Candidate) -> bool {
    let mut expected = vec![
        "copy_flag",
        "destination_kind",
        "register_class",
        "roots_differ",
        "interference",
    ];
    // Window branches are bypassed if either root is a physical register.
    let Some(roots) = c.checks.iter().find(|x| x.condition == "roots_differ") else {
        return matches!(c.decision, Decision::Rejected { .. }) && prefix_path(&expected, c);
    };
    let Some(physical) = header.get("physical").and_then(|v| v.parse::<u32>().ok()) else {
        return false;
    };
    let Some(a) = hex(&roots.inputs, "edx") else {
        return false;
    };
    let Some(b) = hex(&roots.inputs, "ebx") else {
        return false;
    };
    let (a, b) = (a as u16, b as u16);
    if roots.taken != (a != b) {
        return false;
    }
    match c.decision {
        Decision::Merged { parent, child } if parent != a.min(b) || child != a.max(b) => {
            return false;
        }
        Decision::AlreadySameRoot { root } if root != a || a != b => return false,
        _ => {}
    }
    if u32::from(a) >= physical && u32::from(b) >= physical {
        expected.extend([
            "destination_below_window",
            "destination_above_window",
            "source_below_window",
            "source_above_window",
        ]);
    }
    if class == 4 {
        expected.push("gpr_special_register");
    }
    prefix_path(&expected, c)
}
fn prefix_path(expected: &[&str], c: &Candidate) -> bool {
    if c.checks.len() > expected.len()
        || !c
            .checks
            .iter()
            .zip(expected)
            .all(|(a, b)| a.condition == *b)
    {
        return false;
    }
    match &c.decision {
        Decision::Rejected { condition } => c
            .checks
            .last()
            .is_some_and(|x| x.condition == *condition && x.taken),
        Decision::AlreadySameRoot { .. } => c
            .checks
            .last()
            .is_some_and(|x| x.condition == "roots_differ" && !x.taken),
        Decision::Merged { .. } => {
            c.checks.len() == expected.len()
                && c.checks
                    .iter()
                    .all(|x| x.taken == (x.condition == "roots_differ"))
        }
        Decision::Indeterminate => false,
    }
}
