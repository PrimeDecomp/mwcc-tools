//! Guarded regular-call argument observations. Lowered-context ordinals are not
//! source argument numbers, source lifetimes, or final ABI register bindings.
use crate::{
    evidence::{Evaluation, EvaluationState, EvidenceGap},
    operand_observation::{self, Observation},
    trace::{self, Instruction, Record},
    value_history,
};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use std::collections::BTreeSet;

pub const SCOPE: &str = "guarded_regular_call_arguments_v1";
pub const ORDINAL_KIND: &str = "lowered_argument_context_order";

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Stage {
    Register,
    Stack,
    Dispatch,
    ForceGpr,
}
impl Stage {
    fn parse(value: &str) -> Option<Self> {
        match value {
            "register" => Some(Self::Register),
            "stack" => Some(Self::Stack),
            "dispatch" => Some(Self::Dispatch),
            "force_gpr" => Some(Self::ForceGpr),
            _ => None,
        }
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum OperandStage {
    DispatchReturn,
    ForceBefore,
    ForceAfter,
    HelperReturn,
}
impl OperandStage {
    fn parse(value: &str) -> Option<Self> {
        match value {
            "dispatch_return" => Some(Self::DispatchReturn),
            "force_before" => Some(Self::ForceBefore),
            "force_after" => Some(Self::ForceAfter),
            "helper_return" => Some(Self::HelperReturn),
            _ => None,
        }
    }
    fn accepts(self, stage: Stage) -> bool {
        matches!(
            (self, stage),
            (Self::DispatchReturn, Stage::Dispatch)
                | (Self::ForceBefore | Self::ForceAfter, Stage::ForceGpr)
                | (Self::HelperReturn, Stage::Register | Stage::Stack)
        )
    }
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Callee {
    pub node: String,
    pub kind: i32,
    pub object: String,
    pub name_known: bool,
    pub name_complete: bool,
    pub name_bytes: Vec<u8>,
    pub name_text: Option<String>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Argument {
    pub trace_position: usize,
    pub ordinal: u32,
    pub list: String,
    pub context: String,
    pub node: String,
    pub kind: u8,
    pub type_pointer: String,
    pub type_kind: i32,
    pub type_size: i32,
    pub has_call: bool,
    pub flags: u16,
    /// Observed planning slots, which can be -1. These are not final locations.
    pub gpr: i16,
    pub second_gpr: i16,
    pub fpr: i16,
    pub vector: i16,
    pub stack_offset: i32,
    /// Only the observed context bit at bind time. False emits no implicit GPR0.
    pub evaluated: bool,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct OperandEvent {
    pub stage: OperandStage,
    pub observation: Observation,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Birth {
    pub trace_position: usize,
    pub value: value_history::ValueId,
    pub class: u8,
    pub register: u32,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct InstructionEvent {
    pub trace_position: usize,
    pub index: u32,
    pub instruction: Instruction,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Episode {
    pub id: u32,
    pub ordinal: u32,
    pub parent_episode: Option<u32>,
    pub stage: Stage,
    pub begin_position: usize,
    pub end_position: Option<usize>,
    pub context: String,
    pub node: String,
    pub operand: String,
    /// Exact independent codegen dispatch joined by interval/node/output when
    /// this is a dispatch episode; absent for other helper stages.
    pub dispatch_expression: Option<u32>,
    pub births_before: u32,
    pub births_after: Option<u32>,
    pub block_before: String,
    pub tail_before: String,
    pub block_after: Option<String>,
    pub tail_after: Option<String>,
    pub operands: Vec<OperandEvent>,
    pub births: Vec<Birth>,
    pub instructions: Vec<InstructionEvent>,
    pub expected_instructions: Option<u32>,
    pub pcode_complete: bool,
    pub gaps: Vec<Record>,
    pub observations: Evaluation,
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct Totals {
    pub arguments: u32,
    pub episodes: u32,
    pub births: u32,
    pub operands: u32,
    pub instructions: u32,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Call {
    pub id: u32,
    pub function: u32,
    pub function_object: String,
    pub parent_call: Option<u32>,
    pub parent_expression: Option<u32>,
    pub begin_position: usize,
    pub end_position: Option<usize>,
    pub item: String,
    pub kind: u8,
    pub callee: Callee,
    pub return_type: String,
    pub type_kind: i32,
    pub type_size: i32,
    pub output: String,
    pub scope: String,
    pub ordinal_kind: String,
    pub arguments: Vec<Argument>,
    pub episodes: Vec<Episode>,
    pub expected: Option<Totals>,
    pub complete: bool,
    pub gaps: Vec<Record>,
    pub observations: Evaluation,
}
impl Call {
    pub fn observed_totals(&self) -> Totals {
        Totals {
            arguments: self.arguments.len() as u32,
            episodes: self.episodes.len() as u32,
            births: self.episodes.iter().map(|e| e.births.len() as u32).sum(),
            operands: self.episodes.iter().map(|e| e.operands.len() as u32).sum(),
            instructions: self
                .episodes
                .iter()
                .map(|e| e.instructions.len() as u32)
                .sum(),
        }
    }
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct FunctionSummary {
    pub function: u32,
    pub begin_position: usize,
    pub end_position: Option<usize>,
    pub scope: Option<String>,
    pub end_scope: Option<String>,
    pub expected_calls: Option<u32>,
    pub expected: Option<Totals>,
    pub observed_calls: u32,
    pub observed: Totals,
    /// Reconciliation of independently sampled event counters. This does not
    /// require every unrelated call's bounded PCode interval to be complete.
    #[serde(default = "missing_event_totals")]
    pub event_totals: Evaluation,
    pub observations: Evaluation,
}
fn missing_event_totals() -> Evaluation {
    gap("independent regular-call event totals are unavailable")
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct History {
    pub format_version: u32,
    pub functions: Vec<FunctionSummary>,
    pub calls: Vec<Call>,
    pub gaps: Vec<Record>,
    pub observations: Evaluation,
}
impl Default for History {
    fn default() -> Self {
        Self {
            format_version: 1,
            functions: vec![],
            calls: vec![],
            gaps: vec![],
            observations: gap("no complete guarded regular-call argument capture"),
        }
    }
}
pub fn gap(detail: impl Into<String>) -> Evaluation {
    Evaluation::indeterminate(EvidenceGap::CallArgumentCoverage, detail)
}
pub fn is_record(kind: &str) -> bool {
    kind.starts_with("pv.call.") || kind.starts_with("pv.argument.")
}
fn pointer(r: &Record, key: &str, nullable: bool) -> Result<String> {
    let v = r.get(key)?;
    ensure!(
        !v.is_empty() && v.len() <= 8 && v.bytes().all(|b| b.is_ascii_hexdigit()),
        "invalid {key} pointer"
    );
    let n = u32::from_str_radix(v, 16)?;
    ensure!(nullable || n != 0, "null {key} pointer");
    Ok(format!("{n:08x}"))
}
fn bit(r: &Record, key: &str) -> Result<bool> {
    let n = r.number::<u8>(key)?;
    ensure!(n <= 1, "invalid {key} bit");
    Ok(n == 1)
}
fn optional_id(r: &Record, key: &str) -> Result<Option<u32>> {
    Ok(match r.number::<u32>(key)? {
        0 => None,
        n => Some(n),
    })
}
fn totals(r: &Record, function: bool) -> Result<Option<Totals>> {
    let keys = if function {
        [
            "argument_count",
            "argument_episodes",
            "argument_births",
            "argument_operands",
            "argument_instructions",
        ]
    } else {
        [
            "arguments",
            "episodes",
            "births",
            "operands",
            "instructions",
        ]
    };
    if keys.iter().any(|k| !r.fields.contains_key(*k)) {
        return Ok(None);
    }
    Ok(Some(Totals {
        arguments: r.number(keys[0])?,
        episodes: r.number(keys[1])?,
        births: r.number(keys[2])?,
        operands: r.number(keys[3])?,
        instructions: r.number(keys[4])?,
    }))
}
fn callee(r: &Record) -> Result<Callee> {
    let name_known = bit(r, "callee_known")?;
    let name_complete = bit(r, "callee_complete")?;
    let hex = r.get("callee_hex")?;
    ensure!(
        hex.is_ascii() && hex.len().is_multiple_of(2) && hex.len() <= 512,
        "invalid callee name hex"
    );
    let name_bytes = (0..hex.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&hex[i..i + 2], 16).map_err(Into::into))
        .collect::<Result<Vec<_>>>()?;
    ensure!(
        name_known || (!name_complete && name_bytes.is_empty()),
        "unknown callee has name evidence"
    );
    let node = pointer(r, "callee_node", true)?;
    let kind = r.number("callee_kind")?;
    let object = pointer(r, "callee_object", true)?;
    ensure!(
        !name_known || (kind == 56 && node != "00000000" && object != "00000000"),
        "cached callee name lacks a direct function-reference Object"
    );
    ensure!(
        !name_complete || !name_bytes.is_empty(),
        "complete cached callee link is empty"
    );
    ensure!(
        !name_bytes.contains(&0),
        "cached callee link contains a terminator"
    );
    Ok(Callee {
        node,
        kind,
        object,
        name_known,
        name_complete,
        name_text: String::from_utf8(name_bytes.clone()).ok(),
        name_bytes,
    })
}
fn argument(r: &Record) -> Result<Argument> {
    Ok(Argument {
        trace_position: r.line,
        ordinal: r.number("ordinal")?,
        list: pointer(r, "list", false)?,
        context: pointer(r, "context", false)?,
        node: pointer(r, "node", false)?,
        kind: r.number("kind")?,
        type_pointer: pointer(r, "type", false)?,
        type_kind: r.number("type_kind")?,
        type_size: r.number("type_size")?,
        has_call: bit(r, "hascall")?,
        flags: r.number("flags")?,
        gpr: r.number("gpr")?,
        second_gpr: r.number("second_gpr")?,
        fpr: r.number("fpr")?,
        vector: r.number("vector")?,
        stack_offset: r.number("stack_offset")?,
        evaluated: bit(r, "evaluated")?,
    })
}

/// Parse with the exact value history from the same trace. Provenance::parse
/// supplies it once, avoiding a second large lifecycle parse.
pub fn parse(text: &str, values: &value_history::History) -> Result<History> {
    let mut result = History::default();
    let mut calls = Vec::<usize>::new();
    let mut episodes = Vec::<(usize, usize)>::new();
    // The compiler can generate a nested function while outer call/argument
    // observers are suspended. The producer's active() is function-scoped.
    let mut function_contexts = Vec::new();
    let mut last_call = 0;
    let mut last_episode = 0;
    for (position, line) in text.lines().enumerate() {
        let Some(kind) = trace::record_kind(line) else {
            continue;
        };
        if !is_record(kind) && !matches!(kind, "pv.function.begin" | "pv.function.end" | "pv.gap") {
            continue;
        }
        let mut r = trace::records(line)
            .pop()
            .context("empty call argument record")?;
        r.line = position + 1;
        if matches!(kind, "pv.function.begin" | "pv.function.end") {
            let fid = r.number("function")?;
            if kind == "pv.function.begin" {
                function_contexts.push((
                    fid,
                    std::mem::take(&mut calls),
                    std::mem::take(&mut episodes),
                ));
                result.functions.push(FunctionSummary {
                    function: fid,
                    begin_position: r.line,
                    end_position: None,
                    scope: r.fields.get("call_arguments").cloned(),
                    end_scope: None,
                    expected_calls: None,
                    expected: None,
                    observed_calls: 0,
                    observed: Totals::default(),
                    event_totals: missing_event_totals(),
                    observations: gap("missing regular-call function totals or capability"),
                });
            } else if let Some(f) = result.functions.iter_mut().find(|f| f.function == fid) {
                f.end_position = Some(r.line);
                f.end_scope = r.fields.get("call_arguments").cloned();
                f.expected_calls = r.fields.get("call_count").map(|v| v.parse()).transpose()?;
                f.expected = totals(&r, true)?;
                if let Some((active, suspended_calls, suspended_episodes)) = function_contexts.pop()
                {
                    ensure!(
                        active == fid,
                        "call-argument function context is unbalanced"
                    );
                    calls = suspended_calls;
                    episodes = suspended_episodes;
                }
            }
            continue;
        }
        if kind == "pv.gap" {
            if let Some(&i) = calls.last() {
                result.calls[i].gaps.push(r)
            } else {
                result.gaps.push(r)
            }
            continue;
        }
        if r.number::<u32>("schema")? != 1 {
            if let Some(&i) = calls.last() {
                result.calls[i].gaps.push(r)
            } else {
                result.gaps.push(r)
            }
            continue;
        }
        let fid = r.number::<u32>("function")?;
        let cid = r.number::<u32>("call")?;
        ensure!(fid != 0 && cid != 0, "zero function/call identity");
        if kind == "pv.call.begin" {
            ensure!(cid > last_call, "nonmonotonic call identity");
            last_call = cid;
            let parent_call = optional_id(&r, "parent_call")?;
            ensure!(
                parent_call == calls.last().map(|&i| result.calls[i].id),
                "call parent disagrees with active call stack"
            );
            if let Some(&i) = calls.last() {
                ensure!(
                    result.calls[i].function == fid,
                    "nested call crosses function"
                );
            }
            let call = Call {
                id: cid,
                function: fid,
                function_object: pointer(&r, "function_object", false)?,
                parent_call,
                parent_expression: optional_id(&r, "parent_expression")?,
                begin_position: r.line,
                end_position: None,
                item: pointer(&r, "item", true)?,
                kind: r.number("kind")?,
                callee: callee(&r)?,
                return_type: pointer(&r, "return_type", true)?,
                type_kind: r.number("type_kind")?,
                type_size: r.number("type_size")?,
                output: pointer(&r, "output", true)?,
                scope: r.get("scope")?.into(),
                ordinal_kind: r.get("ordinal_kind")?.into(),
                arguments: vec![],
                episodes: vec![],
                expected: None,
                complete: false,
                gaps: vec![],
                observations: gap("call has no complete supported end"),
            };
            calls.push(result.calls.len());
            result.calls.push(call);
            continue;
        }
        let Some(ci) = calls.iter().copied().find(|&i| result.calls[i].id == cid) else {
            result.gaps.push(r);
            continue;
        };
        ensure!(
            result.calls[ci].function == fid,
            "record outside its current function"
        );
        if kind == "pv.call.end" {
            ensure!(
                calls.last() == Some(&ci),
                "call end disagrees with active call stack"
            );
            // A missing episode end is incomplete evidence, not a fabricated
            // balanced end. Preserve the episode with end_position=None.
            episodes.retain(|&(i, _)| i != ci);
            let c = &mut result.calls[ci];
            c.end_position = Some(r.line);
            c.expected = totals(&r, false)?;
            c.complete = bit(&r, "complete")?;
            calls.pop();
            continue;
        }
        if kind == "pv.argument.bind" {
            ensure!(
                calls.last() == Some(&ci),
                "argument binding is outside current call"
            );
            let a = argument(&r)?;
            let c = &mut result.calls[ci];
            ensure!(
                a.ordinal as usize == c.arguments.len(),
                "nonsequential lowered argument context ordinal"
            );
            ensure!(
                c.arguments
                    .iter()
                    .all(|v| v.context != a.context && v.list != a.list),
                "duplicate lowered argument context/list identity"
            );
            c.arguments.push(a);
            continue;
        }
        if kind == "pv.argument.begin" {
            ensure!(
                calls.last() == Some(&ci),
                "argument begin is outside current call"
            );
            let Some(stage) = Stage::parse(r.get("stage")?) else {
                result.calls[ci].gaps.push(r);
                continue;
            };
            let ordinal = r.number::<u32>("ordinal")?;
            let Some(a) = result.calls[ci].arguments.get(ordinal as usize) else {
                result.calls[ci].gaps.push(r);
                continue;
            };
            let id = r.number::<u32>("episode")?;
            ensure!(id > last_episode, "nonmonotonic argument episode identity");
            last_episode = id;
            let parent_episode = optional_id(&r, "parent_episode")?;
            ensure!(
                parent_episode
                    == episodes
                        .last()
                        .map(|&(i, j)| result.calls[i].episodes[j].id),
                "argument episode parent disagrees with active stack"
            );
            let context = pointer(&r, "context", false)?;
            let node = pointer(&r, "node", false)?;
            ensure!(
                context == a.context && node == a.node,
                "argument episode node/context disagrees with binding"
            );
            let operand = pointer(&r, "operand", false)?;
            ensure!(
                u32::from_str_radix(&context, 16)?.checked_add(8)
                    == Some(u32::from_str_radix(&operand, 16)?),
                "argument Operand is not its context output"
            );
            let e = Episode {
                id,
                ordinal,
                parent_episode,
                stage,
                begin_position: r.line,
                end_position: None,
                context,
                node,
                operand,
                dispatch_expression: None,
                births_before: r.number("births_before")?,
                births_after: None,
                block_before: pointer(&r, "block", true)?,
                tail_before: pointer(&r, "tail", true)?,
                block_after: None,
                tail_after: None,
                operands: vec![],
                births: vec![],
                instructions: vec![],
                expected_instructions: None,
                pcode_complete: false,
                gaps: vec![],
                observations: gap("episode is incomplete"),
            };
            let ei = result.calls[ci].episodes.len();
            result.calls[ci].episodes.push(e);
            episodes.push((ci, ei));
            continue;
        }
        let Some((ec, ei)) = episodes.last().copied() else {
            result.calls[ci].gaps.push(r);
            continue;
        };
        if ec != ci {
            result.calls[ci].gaps.push(r);
            continue;
        }
        let e = &mut result.calls[ec].episodes[ei];
        ensure!(
            r.number::<u32>("episode")? == e.id && r.number::<u32>("ordinal")? == e.ordinal,
            "record outside its current argument episode"
        );
        match kind {
            "pv.argument.gap" => e.gaps.push(r),
            "pv.argument.operand" => {
                let Some(stage) = OperandStage::parse(r.get("stage")?) else {
                    e.gaps.push(r);
                    continue;
                };
                ensure!(
                    stage.accepts(e.stage),
                    "operand stage disagrees with argument episode"
                );
                let mut observation = operand_observation::parse(&r)?;
                let operand = pointer(&r, "operand", false)?;
                ensure!(
                    operand == e.operand,
                    "operand observation identity differs from active episode"
                );
                observation.operand = Some(operand);
                ensure!(
                    e.operands.iter().all(|v| v.stage != stage),
                    "duplicate operand stage within episode"
                );
                e.operands.push(OperandEvent { stage, observation });
            }
            "pv.argument.birth" => {
                let value = r.number::<u32>("value")?;
                let class = r.number::<u8>("cls")?;
                let register = r.number::<u32>("register")?;
                ensure!(
                    value != 0 && class < 5,
                    "invalid argument birth identity/class"
                );
                e.births.push(Birth {
                    trace_position: r.line,
                    value: value_history::ValueId {
                        function: fid,
                        sequence: value,
                    },
                    class,
                    register,
                });
            }
            "pv.argument.instruction" => {
                let index = r.number::<u32>("index")?;
                ensure!(
                    index as usize == e.instructions.len(),
                    "nonsequential argument instruction interval"
                );
                let mut instruction = trace::instruction(&r)?;
                instruction.pointer = pointer(&r, "p", false)?;
                ensure!(
                    e.instructions
                        .iter()
                        .all(|v| v.instruction.pointer != instruction.pointer),
                    "repeated instruction in argument interval"
                );
                e.instructions.push(InstructionEvent {
                    trace_position: r.line,
                    index,
                    instruction,
                });
            }
            "pv.argument.end" => {
                e.end_position = Some(r.line);
                e.births_after = Some(r.number("births_after")?);
                e.block_after = Some(pointer(&r, "block", true)?);
                e.tail_after = Some(pointer(&r, "tail", true)?);
                e.expected_instructions = r
                    .fields
                    .get("instructions")
                    .map(|v| v.parse())
                    .transpose()?;
                e.pcode_complete = bit(&r, "pcode_complete")?;
                episodes.pop();
            }
            _ => e.gaps.push(r),
        }
    }
    finalize(&mut result, values)?;
    Ok(result)
}

fn finalize(result: &mut History, values: &value_history::History) -> Result<()> {
    let mut owned = BTreeSet::new();
    let intervals: Vec<_> = result
        .calls
        .iter()
        .flat_map(|c| {
            c.episodes
                .iter()
                .map(move |e| (c.function, e.id, e.begin_position, e.end_position))
        })
        .collect();
    for c in &mut result.calls {
        let function = values.functions.iter().find(|f| f.id == c.function);
        for e in &mut c.episodes {
            let mut valid = e.end_position.is_some()
                && e.gaps.is_empty()
                && e.pcode_complete
                && e.block_before != "00000000"
                && e.block_after.as_ref() == Some(&e.block_before)
                && e.expected_instructions == Some(e.instructions.len() as u32);
            let expected_stages: &[_] = match e.stage {
                Stage::Register | Stage::Stack => &[OperandStage::HelperReturn],
                Stage::Dispatch => &[OperandStage::DispatchReturn],
                Stage::ForceGpr => &[OperandStage::ForceBefore, OperandStage::ForceAfter],
            };
            valid &= expected_stages.iter().all(|stage| {
                e.operands.iter().any(|v| {
                    v.stage == *stage && v.observation.evidence.state == EvaluationState::Satisfied
                })
            });
            valid &= e
                .operands
                .iter()
                .all(|v| v.observation.evidence.state == EvaluationState::Satisfied);
            valid &= e.stage != Stage::ForceGpr
                || e.operands
                    .iter()
                    .map(|v| v.stage)
                    .eq([OperandStage::ForceBefore, OperandStage::ForceAfter]);
            if e.stage == Stage::Dispatch {
                let matching: Vec<_> = function
                    .into_iter()
                    .flat_map(|f| &f.expressions)
                    .filter(|x| {
                        x.scope == "codegen_dispatch"
                            && !x.abandoned
                            && x.dispatch.is_some()
                            && same_pointer(&x.node, &e.node)
                            && x.trace_position < e.begin_position
                            && x.end_position
                                .zip(e.end_position)
                                .is_some_and(|(a, b)| a > b)
                            && x.result.as_ref().is_some_and(|r| {
                                r.operand
                                    .as_deref()
                                    .is_some_and(|p| same_pointer(p, &e.operand))
                                    && e.operands.iter().any(|v| {
                                        v.stage == OperandStage::DispatchReturn
                                            && r.trace_position < v.observation.trace_position
                                            && same_observation(r, &v.observation)
                                    })
                            })
                    })
                    .collect();
                if matching.len() == 1 {
                    e.dispatch_expression = Some(matching[0].id)
                } else {
                    valid = false
                }
            }
            if let Some(f) = function {
                let before = f
                    .births
                    .iter()
                    .filter(|b| b.trace_position < e.begin_position)
                    .count() as u32;
                let after = f
                    .births
                    .iter()
                    .filter(|b| e.end_position.is_some_and(|end| b.trace_position < end))
                    .count() as u32;
                valid &= e.births_before == before && e.births_after == Some(after);
                for b in &e.births {
                    ensure!(
                        owned.insert(b.value),
                        "birth claimed by more than one argument episode"
                    );
                    let actual = f
                        .births
                        .iter()
                        .find(|v| v.id == b.value)
                        .context("argument birth has no backend birth")?;
                    ensure!(
                        actual.class == b.class
                            && actual.register == b.register
                            && actual.trace_position > e.begin_position
                            && actual.trace_position < b.trace_position
                            && e.end_position.is_none_or(|end| b.trace_position < end),
                        "argument birth contradicts backend identity or episode interval"
                    );
                    let nearest = intervals
                        .iter()
                        .filter(|(function, _, begin, end)| {
                            *function == c.function
                                && *begin < actual.trace_position
                                && end.is_none_or(|end| actual.trace_position < end)
                        })
                        .max_by_key(|(_, _, begin, _)| *begin);
                    ensure!(
                        nearest.is_some_and(|(_, id, _, _)| *id == e.id),
                        "birth was not owned by the nearest active argument episode"
                    );
                }
            } else {
                valid = false
            }
            // A complete same-block interval ends at the last retained PCode;
            // zero instructions means the observed tail did not move.
            valid &= if let Some(last) = e.instructions.last() {
                e.tail_after.as_ref() == Some(&last.instruction.pointer)
            } else {
                e.tail_after.as_ref() == Some(&e.tail_before)
            };
            if valid {
                e.observations = Evaluation::satisfied()
            }
        }
        let valid_function = function.is_some_and(|f| {
            f.counter_observations.state == EvaluationState::Satisfied
                && f.function_object
                    .as_ref()
                    .and_then(|p| u32::from_str_radix(p, 16).ok())
                    == u32::from_str_radix(&c.function_object, 16).ok()
                && c.begin_position > f.begin_position
                && c.end_position
                    .zip(f.end_position)
                    .is_some_and(|(a, b)| a < b)
        });
        let expression_valid = c.parent_expression.is_none_or(|id| {
            function.is_some_and(|f| {
                f.expressions.iter().any(|e| {
                    e.id == id
                        && e.trace_position < c.begin_position
                        && e.end_position.is_some_and(|end| end > c.begin_position)
                        && same_pointer(&e.node, &c.item)
                        && e.result.as_ref().is_some_and(|r| {
                            r.operand
                                .as_deref()
                                .is_some_and(|p| same_pointer(p, &c.output))
                        })
                })
            })
        });
        let primary = c.arguments.iter().all(|a| {
            matches!(a.flags & 0x27, 1 | 2 | 4 | 32)
                && c.episodes.iter().any(|e| {
                    e.ordinal == a.ordinal
                        && e.stage
                            == if a.flags & 0x27 == 4 {
                                Stage::Stack
                            } else {
                                Stage::Register
                            }
                })
        });
        let valid_pointers = [&c.item, &c.return_type, &c.output]
            .iter()
            .all(|v| v.as_str() != "00000000");
        if c.end_position.is_some()
            && c.complete
            && c.scope == SCOPE
            && c.ordinal_kind == ORDINAL_KIND
            && matches!(c.kind, 54 | 55)
            && c.gaps.is_empty()
            && c.expected == Some(c.observed_totals())
            && c.episodes
                .iter()
                .all(|e| e.observations.state == EvaluationState::Satisfied)
            && primary
            && valid_function
            && expression_valid
            && valid_pointers
        {
            c.observations = Evaluation::satisfied()
        }
    }
    // Every actual counter birth inside an episode must have exactly one owner,
    // including births owned by nested-call/nested-helper episodes.
    for c in &mut result.calls {
        if let Some(f) = values.functions.iter().find(|f| f.id == c.function) {
            for e in &mut c.episodes {
                if f.births.iter().any(|b| {
                    b.trace_position > e.begin_position
                        && e.end_position.is_some_and(|end| b.trace_position < end)
                        && !owned.contains(&b.id)
                }) {
                    e.observations = gap("argument episode omitted a backend birth");
                    c.observations = gap("argument episode omitted a backend birth");
                }
            }
        }
    }
    // A parent cannot declare its nested call observations complete if a child
    // scope is incomplete, even when its exclusive event totals happen to match.
    for i in (0..result.calls.len()).rev() {
        if result.calls[i].observations.state != EvaluationState::Satisfied
            && let Some(parent) = result.calls[i].parent_call
            && let Some(p) = result.calls.iter_mut().find(|c| c.id == parent)
        {
            p.observations = gap("nested regular call is incomplete")
        }
    }
    for f in &mut result.functions {
        let selected: Vec<_> = result
            .calls
            .iter()
            .filter(|c| c.function == f.function)
            .collect();
        f.observed_calls = selected.len() as u32;
        for c in &selected {
            let n = c.observed_totals();
            f.observed.arguments += n.arguments;
            f.observed.episodes += n.episodes;
            f.observed.births += n.births;
            f.observed.operands += n.operands;
            f.observed.instructions += n.instructions;
        }
        let local_gap = result
            .gaps
            .iter()
            .any(|r| r.line > f.begin_position && f.end_position.is_none_or(|end| r.line < end));
        let unknown_call_stream = selected.iter().any(|c| {
            c.scope != SCOPE
                || c.ordinal_kind != ORDINAL_KIND
                || c.gaps
                    .iter()
                    .chain(c.episodes.iter().flat_map(|e| &e.gaps))
                    .any(|r| {
                        r.kind != "pv.argument.gap"
                            || r.fields.get("schema").is_none_or(|s| s != "1")
                    })
        });
        if f.scope.as_deref() == Some(SCOPE)
            && values.functions.iter().any(|value| {
                value.id == f.function
                    && value.counter_observations.state == EvaluationState::Satisfied
            })
            && matches!(f.end_scope.as_deref(), Some(SCOPE) | Some("unknown"))
            && f.end_position.is_some()
            && f.expected_calls == Some(f.observed_calls)
            && f.expected == Some(f.observed)
            && !local_gap
            && !unknown_call_stream
        {
            f.event_totals = Evaluation::satisfied();
        }
        if f.event_totals.state == EvaluationState::Satisfied
            && f.end_scope.as_deref() == Some(SCOPE)
            && selected
                .iter()
                .all(|c| c.observations.state == EvaluationState::Satisfied)
        {
            f.observations = Evaluation::satisfied()
        }
        if f.event_totals.state != EvaluationState::Satisfied {
            for c in &mut result.calls {
                if c.function == f.function {
                    c.observations =
                        gap("missing or inconsistent independent function call totals/capability")
                }
            }
        }
    }
    if !result.functions.is_empty()
        && result.gaps.is_empty()
        && result
            .functions
            .iter()
            .all(|f| f.observations.state == EvaluationState::Satisfied)
        && result
            .calls
            .iter()
            .all(|c| c.observations.state == EvaluationState::Satisfied)
    {
        result.observations = Evaluation::satisfied()
    }
    Ok(())
}

fn same_pointer(first: &str, second: &str) -> bool {
    u32::from_str_radix(first, 16)
        .ok()
        .zip(u32::from_str_radix(second, 16).ok())
        .is_some_and(|(a, b)| a == b)
}
fn same_observation(a: &Observation, b: &Observation) -> bool {
    a.evidence.state == EvaluationState::Satisfied
        && b.evidence.state == EvaluationState::Satisfied
        && a.kind == b.kind
        && a.class == b.class
        && a.register == b.register
        && a.second == b.second
        && a.index == b.index
        && a.object
            .as_ref()
            .zip(b.object.as_ref())
            .map_or(a.object.is_none() && b.object.is_none(), |(a, b)| {
                same_pointer(a, b)
            })
        && a.displacement == b.displacement
        && a.immediate == b.immediate
}
