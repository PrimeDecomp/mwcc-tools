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
    pub object: Option<ObjectBinding>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ObjectPart {
    Single,
    FirstWord,
    SecondWord,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ObjectBinding {
    pub trace_position: usize,
    pub object: String,
    pub variable_info: String,
    pub part: ObjectPart,
    pub datatype: u8,
    pub storage: u16,
    pub flags_before: u8,
    pub type_kind: i32,
    pub type_size: i32,
    pub name_known: bool,
    pub name_complete: bool,
    pub name_bytes: Vec<u8>,
    pub name_text: Option<String>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum RangeKind {
    Initial { last: [i32; 3] },
    Window { first: [i32; 3], last: [i32; 3] },
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct RangeEvent {
    pub trace_position: usize,
    pub birth_cutoff: u32,
    pub range: RangeKind,
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
    pub parent: Option<u32>,
    pub dispatch: Option<Dispatch>,
    pub result: Option<DispatchResult>,
    pub end_position: Option<usize>,
    pub abandoned: bool,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Dispatch {
    /// Address in the pinned retail image, not a live compiler pointer.
    pub retail_target: u32,
    /// Exact stack argument bits; unused upper bits are not interpreted.
    pub requested: [u32; 2],
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct DispatchResult {
    pub trace_position: usize,
    pub kind: u8,
    pub class: Option<u8>,
    pub register: Option<u32>,
    pub second: Option<u32>,
    pub index: Option<u32>,
    pub object: Option<String>,
    pub displacement: Option<i32>,
    pub immediate: Option<i32>,
    /// Tag-defined output observation, not a semantic lifetime binding.
    pub evidence: Evaluation,
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
    pub expected_graphs: Option<u32>,
    pub expected_ranges: Option<u32>,
    pub ranges: Vec<RangeEvent>,
    pub range_observations: Evaluation,
    pub counter_observations: Evaluation,
    pub dispatch_scope: Option<String>,
    pub expected_dispatches: Option<u32>,
    pub expected_dispatch_returns: Option<u32>,
    pub dispatch_observations: Evaluation,
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
            | "pv.expression.result"
            | "pv.value.birth"
            | "pv.value.object"
            | "pv.category.initial"
            | "pv.category.window"
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
fn bytes(r: &Record, field: &str) -> Result<Vec<u8>> {
    let encoded = r.get(field)?;
    ensure!(
        encoded.is_ascii() && encoded.len().is_multiple_of(2),
        "invalid {field} hex"
    );
    (0..encoded.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&encoded[i..i + 2], 16).context("invalid byte hex"))
        .collect()
}
fn bit(r: &Record, field: &str) -> Result<bool> {
    let value = n(r, field)?;
    ensure!(value <= 1, "invalid {field} bit");
    Ok(value == 1)
}
struct State {
    index: usize,
    counts: [u32; 5],
    registers: BTreeMap<(u8, u32), Vec<ValueId>>,
    expressions: Vec<u32>,
    valid: bool,
    ranges_valid: bool,
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
                    ranges_valid: true,
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
                expected_graphs: None,
                expected_ranges: None,
                ranges: vec![],
                range_observations: gap("no complete range-marker capture"),
                counter_observations: gap("function has no end record"),
                dispatch_scope: r.fields.get("dispatch").cloned(),
                expected_dispatches: None,
                expected_dispatch_returns: None,
                dispatch_observations: gap("no guarded dispatch summary"),
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
                f.expected_graphs = r
                    .fields
                    .get("graphs")
                    .map(|value| value.parse())
                    .transpose()?;
                f.expected_ranges = r.fields.get("ranges").map(|v| v.parse()).transpose()?;
                f.expected_dispatches =
                    r.fields.get("dispatches").map(|v| v.parse()).transpose()?;
                f.expected_dispatch_returns = r
                    .fields
                    .get("dispatch_returns")
                    .map(|v| v.parse())
                    .transpose()?;
                if state.ranges_valid
                    && f.expected_ranges.map(|v| v as usize) == Some(f.ranges.len())
                {
                    f.range_observations = known();
                }
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
                let parent = state.expressions.last().copied();
                if let Some(declared) = r.fields.get("parent") {
                    ensure!(
                        declared.parse::<u32>()? == parent.unwrap_or(0),
                        "expression parent disagrees with active scope"
                    );
                }
                let dispatch = if r.get("scope")? == "codegen_dispatch" {
                    let target = h(&r, "target")?;
                    ensure!(target != 0, "dispatch has no retail target");
                    Some(Dispatch {
                        retail_target: target,
                        requested: [h(&r, "a")?, h(&r, "b")?],
                    })
                } else {
                    None
                };
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
                    parent,
                    dispatch,
                    result: None,
                    end_position: None,
                    abandoned: false,
                });
                state.expressions.push(id);
            }
            "pv.expression.end" => {
                ensure!(
                    state.expressions.pop() == Some(n(&r, "expression")?),
                    "expression scope end disagrees with stack"
                );
                f.expressions[n(&r, "expression")? as usize - 1].end_position = Some(r.line);
                let abandoned = r.fields.get("abandoned").is_some_and(|v| v == "1");
                f.expressions[n(&r, "expression")? as usize - 1].abandoned = abandoned;
                state.valid &= !abandoned;
            }
            "pv.expression.result" => {
                let id = n(&r, "expression")?;
                ensure!(
                    state.expressions.last() == Some(&id),
                    "dispatch result outside its active scope"
                );
                let expression = f
                    .expressions
                    .get_mut(id as usize - 1)
                    .context("result has no expression")?;
                ensure!(
                    expression.dispatch.is_some() && expression.result.is_none(),
                    "result is repeated or not a dispatch result"
                );
                let kind: u8 = n(&r, "kind")?.try_into()?;
                let number = |field: &str| -> Result<Option<u32>> {
                    r.fields
                        .get(field)
                        .map(|v| v.parse().map_err(Into::into))
                        .transpose()
                };
                let signed = |field: &str| -> Result<Option<i32>> {
                    r.fields
                        .get(field)
                        .map(|v| v.parse().map_err(Into::into))
                        .transpose()
                };
                let class = number("cls")?.map(u8::try_from).transpose()?;
                let register = number("register")?;
                let second = number("second")?;
                let index = number("index")?;
                let object = r.fields.get("object").cloned();
                let displacement = signed("displacement")?;
                let immediate = signed("immediate")?;
                let expected_class = match kind {
                    0..=3 | 9 | 10 => Some(4),
                    5 => Some(3),
                    6 => Some(2),
                    7 => Some(1),
                    _ => None,
                };
                ensure!(
                    class == expected_class
                        && register.is_some() == expected_class.is_some()
                        && second.is_some() == (kind == 3)
                        && index.is_some() == matches!(kind, 2 | 10)
                        && object.is_some() == matches!(kind, 8 | 11)
                        && displacement.is_some() == matches!(kind, 1 | 8 | 9 | 11)
                        && immediate.is_some() == (kind == 4),
                    "dispatch result disagrees with its Operand tag"
                );
                if let Some(object) = &object {
                    ensure!(
                        u32::from_str_radix(object, 16).is_ok(),
                        "invalid dispatch object identity"
                    );
                }
                expression.result = Some(DispatchResult {
                    trace_position: r.line,
                    kind,
                    class,
                    register,
                    second,
                    index,
                    object,
                    displacement,
                    immediate,
                    evidence: if kind <= 11 {
                        known()
                    } else {
                        gap("unknown dispatch output tag")
                    },
                });
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
                    object: None,
                });
            }
            "pv.value.object" => {
                let sequence = n(&r, "value")?;
                ensure!(
                    sequence != 0 && sequence as usize == f.births.len(),
                    "object binding is not attached to the current birth"
                );
                let birth = f.births.last_mut().unwrap();
                ensure!(birth.object.is_none(), "duplicate object binding");
                let name_bytes = bytes(&r, "name_hex")?;
                let name_known = bit(&r, "name_known")?;
                let name_complete = bit(&r, "name_complete")?;
                ensure!(
                    name_known || (name_bytes.is_empty() && !name_complete),
                    "unknown name has claimed name evidence"
                );
                birth.object = Some(ObjectBinding {
                    trace_position: r.line,
                    object: r.get("object")?.into(),
                    variable_info: r.get("info")?.into(),
                    part: match n(&r, "part")? {
                        1 => ObjectPart::Single,
                        2 => ObjectPart::FirstWord,
                        3 => ObjectPart::SecondWord,
                        _ => anyhow::bail!("unknown object binding part"),
                    },
                    datatype: n(&r, "datatype")?.try_into()?,
                    storage: n(&r, "storage")?.try_into()?,
                    flags_before: n(&r, "flags_before")?.try_into()?,
                    type_kind: r.number("type_kind")?,
                    type_size: r.number("type_size")?,
                    name_known,
                    name_complete,
                    name_text: if name_known {
                        String::from_utf8(name_bytes.clone()).ok()
                    } else {
                        None
                    },
                    name_bytes,
                });
            }
            "pv.category.initial" | "pv.category.window" => {
                let birth_cutoff = n(&r, "births")?;
                ensure!(
                    birth_cutoff as usize == f.births.len(),
                    "category marker birth cutoff disagrees with capture"
                );
                let array = |prefix: &str| -> Result<[i32; 3]> {
                    Ok([
                        r.number(&format!("{prefix}2"))?,
                        r.number(&format!("{prefix}3"))?,
                        r.number(&format!("{prefix}4"))?,
                    ])
                };
                let range = if r.kind == "pv.category.initial" {
                    let last = array("i")?;
                    state.ranges_valid &= (0..3)
                        .all(|i| last[i] == state.counts[i + 2].wrapping_sub(1) as i16 as i32);
                    RangeKind::Initial { last }
                } else {
                    let first = array("f")?;
                    let last = array("l")?;
                    state.ranges_valid &= (0..3).all(|i| {
                        first[i] == state.counts[i + 2] as i16 as i32
                            && last[i] == state.counts[i + 2] as i32
                    });
                    RangeKind::Window { first, last }
                };
                f.ranges.push(RangeEvent {
                    trace_position: r.line,
                    birth_cutoff,
                    range,
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
        let function = &mut history.functions[state.index];
        let dispatches = function
            .expressions
            .iter()
            .filter(|e| e.dispatch.is_some())
            .collect::<Vec<_>>();
        if state.valid
            && function.ended
            && function.dispatch_scope.as_deref() == Some("guarded_table_v1")
            && function.expected_dispatches.map(|n| n as usize) == Some(dispatches.len())
            && function.expected_dispatch_returns == function.expected_dispatches
            && dispatches
                .iter()
                .all(|e| e.end_position.is_some() && !e.abandoned && e.result.is_some())
        {
            function.dispatch_observations = known();
        }
        if !state.valid {
            history.functions[state.index].counter_observations =
                gap("unsupported or discontinuous observations in this function");
            history.functions[state.index].range_observations =
                gap("unsupported or discontinuous function capture");
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
