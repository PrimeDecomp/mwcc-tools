//! Observed register births and counter reuse. A number can denote multiple value
//! generations; this model never equates its latest birth with every graph use.
use crate::{
    evidence::{Evaluation, EvaluationState, EvidenceGap},
    trace::{self, Record},
};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
pub struct ValueId {
    pub function: u32,
    pub sequence: u32,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Phase {
    CodeGeneration,
    Optimizer,
    AfterOptimizer,
    Allocation,
    AfterAllocation,
    Unknown { code: u32 },
}
impl From<u32> for Phase {
    fn from(code: u32) -> Self {
        match code {
            0 => Self::CodeGeneration,
            1 => Self::Optimizer,
            2 => Self::AfterOptimizer,
            3 => Self::Allocation,
            4 => Self::AfterAllocation,
            code => Self::Unknown { code },
        }
    }
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Birth {
    pub id: ValueId,
    pub trace_position: usize,
    pub class: u8,
    pub register: u32,
    pub generation: u32,
    pub physical_limit: u32,
    pub site: u32,
    pub phase: Phase,
    pub expression: Option<u32>,
    pub line: Option<i32>,
    pub category: Option<String>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Expression {
    pub id: u32,
    pub trace_position: usize,
    pub node: String,
    pub kind: i32,
    pub line: Option<i32>,
    pub scope: String,
    pub complete: bool,
    pub tree_bytes: Vec<u8>,
    pub tree_text: Option<String>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ChangeKind {
    CloseTemps,
    Rewind,
    InitialReset,
    AfterAllocationReset,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct CounterChange {
    pub kind: ChangeKind,
    pub trace_position: usize,
    pub class: u8,
    pub old: u32,
    pub after: u32,
    pub physical_limit: u32,
    pub site: u32,
    pub phase: Phase,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct RegisterHistory {
    pub class: u8,
    pub register: u32,
    pub values: Vec<ValueId>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Function {
    pub id: u32,
    pub begin_position: usize,
    pub end_position: Option<usize>,
    pub parent: u32,
    pub link: String,
    pub births: Vec<Birth>,
    pub expressions: Vec<Expression>,
    pub changes: Vec<CounterChange>,
    pub registers: Vec<RegisterHistory>,
    pub ended: bool,
    pub counter_observations: Evaluation,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct History {
    pub format_version: u32,
    pub functions: Vec<Function>,
    pub orphans: Vec<Record>,
    pub gaps: Vec<Record>,
    /// Completeness within the declared direct-counter scope, not full lifecycle.
    pub counter_observations: Evaluation,
}
pub fn is_record(kind: &str) -> bool {
    matches!(
        kind,
        "pv.function.begin"
            | "pv.function.end"
            | "pv.expression.begin"
            | "pv.expression.end"
            | "pv.value.birth"
            | "pv.counter.change"
            | "pv.counter.orphan"
    )
}
fn gap(detail: &str) -> Evaluation {
    Evaluation::indeterminate(EvidenceGap::BackendLifecycleCoverage, detail)
}
fn known() -> Evaluation {
    Evaluation {
        state: EvaluationState::Satisfied,
        reasons: vec![],
    }
}
fn n(r: &Record, key: &str) -> Result<u32> {
    r.number(key)
}
fn h(r: &Record, key: &str) -> Result<u32> {
    Ok(u32::from_str_radix(r.get(key)?, 16)?)
}
fn line(r: &Record) -> Result<Option<i32>> {
    let value = r.number::<i32>("line")?;
    Ok((value >= 0).then_some(value))
}
fn counts(r: &Record) -> Result<[u32; 5]> {
    Ok([
        n(r, "c0")?,
        n(r, "c1")?,
        n(r, "c2")?,
        n(r, "c3")?,
        n(r, "c4")?,
    ])
}
struct State {
    index: usize,
    counts: [u32; 5],
    registers: BTreeMap<(u8, u32), Vec<ValueId>>,
    expressions: Vec<u32>,
    valid: bool,
}

pub fn parse(text: &str) -> Result<History> {
    let mut history = History {
        format_version: 1,
        functions: vec![],
        orphans: vec![],
        gaps: vec![],
        counter_observations: gap("no birth/counter capture"),
    };
    let mut states: BTreeMap<u32, State> = BTreeMap::new();
    let mut stack = vec![];
    for (index, line_text) in text
        .lines()
        .enumerate()
        .filter(|(_, l)| l.trim_start().starts_with("pv."))
    {
        let mut r = trace::records(line_text)
            .pop()
            .context("empty provenance record")?;
        r.line = index + 1;
        if r.kind == "pv.gap" {
            for active in &stack {
                if let Some(state) = states.get_mut(active) {
                    state.valid = false;
                }
            }
            history.gaps.push(r);
            continue;
        }
        if r.kind == "pv.counter.orphan" {
            history.orphans.push(r);
            continue;
        }
        if !is_record(&r.kind) {
            continue;
        }
        let function = n(&r, "function")?;
        if r.kind == "pv.function.begin" {
            ensure!(
                function != 0 && !states.contains_key(&function),
                "duplicate function observation ID"
            );
            let parent = n(&r, "parent")?;
            ensure!(
                parent == stack.last().copied().unwrap_or(0),
                "function context parent disagrees with stack"
            );
            let valid = n(&r, "schema")? == 1
                && r.get("coverage")? == "direct_counter_writes"
                && r.get("link")? != "?";
            states.insert(
                function,
                State {
                    index: history.functions.len(),
                    counts: counts(&r)?,
                    registers: BTreeMap::new(),
                    expressions: vec![],
                    valid,
                },
            );
            history.functions.push(Function {
                id: function,
                begin_position: r.line,
                end_position: None,
                parent,
                link: r.get("link")?.into(),
                births: vec![],
                expressions: vec![],
                changes: vec![],
                registers: vec![],
                ended: false,
                counter_observations: gap("function has no end record"),
            });
            stack.push(function);
            continue;
        }
        ensure!(
            stack.last() == Some(&function),
            "value record outside its active function"
        );
        let state = states
            .get_mut(&function)
            .context("unknown function observation")?;
        let f = &mut history.functions[state.index];
        match r.kind.as_str() {
            "pv.function.end" => {
                state.valid &= n(&r, "births")? as usize == f.births.len()
                    && counts(&r)? == state.counts
                    && state.expressions.is_empty();
                f.ended = true;
                f.end_position = Some(r.line);
                stack.pop();
                if state.valid {
                    f.counter_observations = known();
                }
                let final_counts = state.counts;
                if let Some(parent) = stack.last() {
                    states.get_mut(parent).unwrap().counts = final_counts;
                }
            }
            "pv.expression.begin" => {
                let id = n(&r, "expression")?;
                ensure!(
                    id as usize == f.expressions.len() + 1,
                    "nonsequential expression origin ID"
                );
                let encoded = r.get("tree_hex")?;
                ensure!(encoded.is_ascii(), "expression hex is not ASCII");
                ensure!(
                    encoded.len().is_multiple_of(2),
                    "invalid expression hex length"
                );
                let tree_bytes = (0..encoded.len())
                    .step_by(2)
                    .map(|i| u8::from_str_radix(&encoded[i..i + 2], 16))
                    .collect::<std::result::Result<Vec<_>, _>>()?;
                f.expressions.push(Expression {
                    id,
                    trace_position: r.line,
                    node: r.get("node")?.into(),
                    kind: r.number("kind")?,
                    line: line(&r)?,
                    scope: r.get("scope")?.into(),
                    complete: n(&r, "complete")? != 0,
                    tree_text: String::from_utf8(tree_bytes.clone()).ok(),
                    tree_bytes,
                });
                state.expressions.push(id);
            }
            "pv.expression.end" => {
                ensure!(
                    state.expressions.pop() == Some(n(&r, "expression")?),
                    "expression scope end disagrees with stack"
                );
            }
            "pv.value.birth" => {
                state.valid &= n(&r, "schema")? == 1;
                let sequence = n(&r, "value")?;
                ensure!(
                    sequence as usize == f.births.len() + 1,
                    "nonsequential value creation ID"
                );
                let class: u8 = n(&r, "cls")?.try_into()?;
                ensure!(class < 5, "unknown register class");
                let register = n(&r, "register")?;
                let count = n(&r, "count")?;
                ensure!(
                    register.checked_add(1) == Some(count),
                    "birth register disagrees with observed counter"
                );
                state.valid &= state.counts[class as usize].checked_add(1) == Some(count);
                state.counts[class as usize] = count;
                let expression = n(&r, "expression")?;
                ensure!(
                    expression as usize <= f.expressions.len(),
                    "birth references an unknown expression origin"
                );
                let id = ValueId { function, sequence };
                let values = state.registers.entry((class, register)).or_default();
                let generation = values.len().try_into()?;
                values.push(id);
                let category = r.get("category")?;
                f.births.push(Birth {
                    id,
                    trace_position: r.line,
                    class,
                    register,
                    generation,
                    physical_limit: n(&r, "physical")?,
                    site: h(&r, "site")?,
                    phase: n(&r, "phase")?.into(),
                    expression: (expression != 0).then_some(expression),
                    line: line(&r)?,
                    category: (category != "unknown").then(|| category.to_owned()),
                });
            }
            "pv.counter.change" => {
                let class: u8 = n(&r, "cls")?.try_into()?;
                ensure!(class < 5, "unknown counter class");
                let old = n(&r, "old")?;
                let after = n(&r, "after")?;
                state.valid &= old == state.counts[class as usize];
                state.counts[class as usize] = after;
                let kind = match n(&r, "role")? {
                    2 => ChangeKind::CloseTemps,
                    3 => ChangeKind::Rewind,
                    4 => ChangeKind::InitialReset,
                    5 => ChangeKind::AfterAllocationReset,
                    _ => anyhow::bail!("unknown counter-change role"),
                };
                state.valid &= match kind {
                    ChangeKind::CloseTemps => after >= old,
                    ChangeKind::Rewind => after <= old,
                    ChangeKind::InitialReset | ChangeKind::AfterAllocationReset => {
                        after == n(&r, "physical")?
                    }
                };
                f.changes.push(CounterChange {
                    kind,
                    trace_position: r.line,
                    class,
                    old,
                    after,
                    physical_limit: n(&r, "physical")?,
                    site: h(&r, "site")?,
                    phase: n(&r, "phase")?.into(),
                });
            }
            _ => {}
        }
    }
    for state in states.into_values() {
        if !state.valid {
            history.functions[state.index].counter_observations =
                gap("unsupported or discontinuous observations in this function");
        }
        history.functions[state.index].registers = state
            .registers
            .into_iter()
            .map(|((class, register), values)| RegisterHistory {
                class,
                register,
                values,
            })
            .collect();
    }
    if !history.functions.is_empty()
        && stack.is_empty()
        && history.orphans.is_empty()
        && history.gaps.is_empty()
        && history
            .functions
            .iter()
            .all(|f| f.counter_observations.state == EvaluationState::Satisfied)
    {
        history.counter_observations = known();
    } else {
        history.counter_observations =
            gap("incomplete direct-counter/function capture, discontinuity or orphan writes");
    }
    Ok(history)
}
