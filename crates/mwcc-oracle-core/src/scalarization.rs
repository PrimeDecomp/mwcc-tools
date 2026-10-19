//! Observed frontend aggregate-to-scalar creation. Exact Object joins are scoped
//! to a function and capture interval; names alone never bind backend values.
use crate::{
    evidence::{Evaluation, EvaluationState, EvidenceGap},
    trace::{self, Record},
    value_history::{Birth, Function},
};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
mod normalization;
pub use normalization::normalized_function;

pub const SCOPE: &str = "guarded_temp_returns_v1";

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Source {
    pub object: String,
    pub variable_info: String,
    pub type_pointer: String,
    pub type_kind: i32,
    pub type_size: i32,
    pub datatype: u8,
    pub storage: u16,
    pub name_known: bool,
    pub name_complete: bool,
    pub name_bytes: Vec<u8>,
    pub name_text: Option<String>,
    pub element_known: bool,
    pub element_type_kind: i32,
    pub element_type_size: i32,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Temporary {
    pub object: String,
    pub variable_info: String,
    pub type_pointer: String,
    pub type_kind: i32,
    pub type_size: i32,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Binding {
    pub ordinal: u32,
    pub trace_position: usize,
    pub site: u32,
    pub part: String,
    pub source_linear: String,
    pub source_index: u32,
    pub source_kind: u8,
    pub variable: String,
    pub source: Source,
    pub byte_offset: i32,
    pub byte_size: i32,
    pub bit_first: i32,
    pub bit_last: i32,
    pub flags: u8,
    pub temporary: Temporary,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Pass {
    pub schema: u32,
    pub function: Option<u32>,
    pub id: u32,
    pub begin_position: usize,
    pub end_position: Option<usize>,
    pub function_object: String,
    pub link: String,
    pub scope: String,
    pub bindings: Vec<Binding>,
    pub expected_bindings: Option<u32>,
    pub gaps: Vec<Record>,
    pub observations: Evaluation,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct History {
    pub format_version: u32,
    pub passes: Vec<Pass>,
    pub gaps: Vec<Record>,
    pub observations: Evaluation,
    #[serde(default)]
    pub backend_values: Vec<BackendValue>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct BackendValue {
    pub value: crate::value_history::ValueId,
    pub class: u8,
    pub register: u32,
    pub pass: Option<u32>,
    pub binding: Option<u32>,
    pub array_element: Option<ArrayElement>,
    pub observations: Evaluation,
}
/// Independent frontend totals sampled at function completion.
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Summary {
    pub scope: String,
    pub passes: u32,
    pub bindings: u32,
    pub first_pass: u32,
    pub last_pass: u32,
}
pub fn summary(record: &Record) -> Result<Option<Summary>> {
    if !record.fields.contains_key("scalar_passes")
        && !record.fields.contains_key("scalar_bindings")
        && !record.fields.contains_key("scalar_first_pass")
        && !record.fields.contains_key("scalar_last_pass")
    {
        return Ok(None);
    }
    record
        .fields
        .get("scalarization")
        .map(|scope| {
            Ok(Summary {
                scope: scope.clone(),
                passes: record.number("scalar_passes")?,
                bindings: record.number("scalar_bindings")?,
                first_pass: record.number("scalar_first_pass")?,
                last_pass: record.number("scalar_last_pass")?,
            })
        })
        .transpose()
}
pub fn gap(detail: impl Into<String>) -> Evaluation {
    Evaluation::indeterminate(EvidenceGap::ScalarizationCoverage, detail)
}
fn known() -> Evaluation {
    Evaluation {
        state: EvaluationState::Satisfied,
        reasons: vec![],
    }
}
impl Default for History {
    fn default() -> Self {
        Self {
            format_version: 1,
            passes: vec![],
            gaps: vec![],
            observations: gap("no guarded scalarization capture"),
            backend_values: vec![],
        }
    }
}
pub fn is_record(kind: &str) -> bool {
    kind.starts_with("pv.scalar.")
}
fn pointer(r: &Record, field: &str) -> Result<String> {
    let value = u32::from_str_radix(r.get(field)?, 16)?;
    ensure!(value != 0, "null scalarization {field}");
    Ok(format!("{value:08x}"))
}
fn same_pointer(a: &str, b: &str) -> bool {
    matches!((u32::from_str_radix(a,16),u32::from_str_radix(b,16)),(Ok(a),Ok(b)) if a!=0 && a==b)
}
fn bit(r: &Record, field: &str) -> Result<bool> {
    let value: u8 = r.number(field)?;
    ensure!(value <= 1, "invalid scalarization {field} bit");
    Ok(value == 1)
}
fn name(r: &Record) -> Result<(bool, bool, Vec<u8>)> {
    let known = bit(r, "source_name_known")?;
    let complete = bit(r, "source_name_complete")?;
    let encoded = r.get("source_name_hex")?;
    ensure!(
        encoded.is_ascii() && encoded.len().is_multiple_of(2) && encoded.len() <= 512,
        "invalid scalarization name bytes"
    );
    let bytes = (0..encoded.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&encoded[i..i + 2], 16).map_err(Into::into))
        .collect::<Result<Vec<_>>>()?;
    ensure!(
        known || (!complete && bytes.is_empty()),
        "unknown scalarization name has contents or completeness"
    );
    Ok((known, complete, bytes))
}
fn binding(r: &Record) -> Result<Binding> {
    let site = u32::from_str_radix(r.get("site")?, 16)?;
    ensure!(
        matches!(site, 0x4608a8 | 0x460b28 | 0x461748),
        "unsupported scalarization return site"
    );
    let (name_known, name_complete, name_bytes) = name(r)?;
    let source = Source {
        object: pointer(r, "source_object")?,
        variable_info: pointer(r, "source_info")?,
        type_pointer: pointer(r, "source_type")?,
        type_kind: r.number("source_type_kind")?,
        type_size: r.number("source_type_size")?,
        datatype: r.number("source_datatype")?,
        storage: r.number("source_storage")?,
        name_known,
        name_complete,
        name_text: String::from_utf8(name_bytes.clone()).ok(),
        name_bytes,
        element_known: bit(r, "source_element_known")?,
        element_type_kind: r.number("source_element_type_kind")?,
        element_type_size: r.number("source_element_type_size")?,
    };
    ensure!(
        !source.element_known
            || (source.type_kind == 12
                && source.element_type_size > 0
                && source.type_size > 0
                && source.type_size % source.element_type_size == 0),
        "invalid scalarization source array type"
    );
    ensure!(
        source.element_known || (source.element_type_kind == -1 && source.element_type_size == -1),
        "unknown scalarization element has type data"
    );
    let temporary = Temporary {
        object: pointer(r, "temporary")?,
        variable_info: pointer(r, "temporary_info")?,
        type_pointer: pointer(r, "temporary_type")?,
        type_kind: r.number("temporary_type_kind")?,
        type_size: r.number("temporary_type_size")?,
    };
    ensure!(
        source.datatype == 1
            && matches!(source.type_kind, 4 | 5 | 12)
            && matches!(temporary.type_kind, 1 | 2 | 11),
        "scalarization source or temporary kind is outside the guarded scope"
    );
    let result = Binding {
        ordinal: r.number("binding")?,
        trace_position: r.line,
        site,
        part: pointer(r, "part")?,
        source_linear: pointer(r, "source_linear")?,
        source_index: r.number("source_index")?,
        source_kind: r.number("source_kind")?,
        variable: pointer(r, "variable")?,
        source,
        byte_offset: r.number("byte_offset")?,
        byte_size: r.number("byte_size")?,
        bit_first: r.number("bit_first")?,
        bit_last: r.number("bit_last")?,
        flags: r.number("flags")?,
        temporary,
    };
    ensure!(
        result.flags & 1 == 0
            && result.source.object != result.temporary.object
            && result.byte_offset >= 0
            && result.byte_size > 0
            && result.byte_size == result.temporary.type_size
            && result.bit_first >= 0
            && result.bit_last >= result.bit_first,
        "invalid scalarization creation interval or type"
    );
    ensure!(
        result
            .byte_offset
            .checked_add(result.byte_size)
            .is_some_and(|end| end <= result.source.type_size),
        "scalarization interval exceeds source object"
    );
    if result.flags & 2 == 0 {
        ensure!(
            result.byte_offset.checked_mul(8) == Some(result.bit_first)
                && result
                    .byte_offset
                    .checked_add(result.byte_size)
                    .and_then(|n| n.checked_mul(8))
                    .and_then(|n| n.checked_sub(1))
                    == Some(result.bit_last),
            "non-bitfield scalarization interval disagrees with bit bounds"
        );
    }
    Ok(result)
}
pub fn parse(text: &str) -> Result<History> {
    let mut result = History::default();
    let mut active: Option<usize> = None;
    for (index, line) in text
        .lines()
        .enumerate()
        .filter(|(_, line)| line.trim_start().starts_with("pv.scalar."))
    {
        let mut record = trace::records(line)
            .pop()
            .context("empty scalarization record")?;
        record.line = index + 1;
        if record.kind == "pv.scalar.begin" {
            ensure!(active.is_none(), "nested scalarization capture");
            let id: u32 = record.number("pass")?;
            ensure!(
                id != 0 && result.passes.last().is_none_or(|p| p.id < id),
                "nonmonotonic scalarization pass identity"
            );
            let scope = record.get("coverage")?.to_owned();
            let schema = record.number::<u32>("schema")?;
            let function = record
                .fields
                .get("function")
                .map(|n| n.parse::<u32>())
                .transpose()?;
            let valid = schema == 1 && scope == SCOPE && function.is_some_and(|n| n != 0);
            let mut pass = Pass {
                schema,
                function,
                id,
                begin_position: record.line,
                end_position: None,
                function_object: pointer(&record, "function_object")?,
                link: record.get("link")?.into(),
                scope,
                bindings: vec![],
                expected_bindings: None,
                gaps: vec![],
                observations: gap("scalarization pass has no complete end"),
            };
            if !valid {
                pass.gaps.push(record);
            }
            active = Some(result.passes.len());
            result.passes.push(pass);
            continue;
        }
        let Some(index) = active else {
            result.gaps.push(record);
            continue;
        };
        let pass = &mut result.passes[index];
        let function = record
            .fields
            .get("function")
            .map(|n| n.parse::<u32>())
            .transpose()?;
        ensure!(
            record.number::<u32>("pass")? == pass.id
                && pointer(&record, "function_object")? == pass.function_object
                && function == pass.function,
            "scalarization record outside its active pass/function"
        );
        if record.kind == "pv.scalar.gap" {
            pass.gaps.push(record);
            continue;
        }
        if pass.schema != 1 || pass.scope != SCOPE || record.number::<u32>("schema")? != 1 {
            if record.kind == "pv.scalar.end" {
                pass.end_position = Some(record.line);
                active = None;
            }
            pass.gaps.push(record);
            continue;
        }
        match record.kind.as_str() {
            "pv.scalar.binding" => {
                let value = binding(&record)?;
                ensure!(
                    value.ordinal as usize == pass.bindings.len() + 1,
                    "nonsequential scalarization binding"
                );
                pass.bindings.push(value);
            }
            "pv.scalar.end" => {
                pass.end_position = Some(record.line);
                pass.expected_bindings = Some(record.number("bindings")?);
                if pass.scope == SCOPE
                    && record.get("coverage")? == SCOPE
                    && pass.expected_bindings.map(|n| n as usize) == Some(pass.bindings.len())
                    && pass.gaps.is_empty()
                    && !pass.link.is_empty()
                    && pass.link != "?"
                {
                    pass.observations = known();
                }
                active = None;
            }
            _ => pass.gaps.push(record),
        }
    }
    if !result.passes.is_empty()
        && active.is_none()
        && result.gaps.is_empty()
        && result
            .passes
            .iter()
            .all(|p| p.observations.state == EvaluationState::Satisfied)
    {
        result.observations = known();
    }
    Ok(result)
}

/// A whole source-array element. A field within an array element, bitfield or
/// unknown target type cannot acquire this role from a coincidental byte offset.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct ArrayElement {
    pub name: Vec<u8>,
    pub element_index: u32,
    pub element_size: i32,
    pub element_type_kind: i32,
    pub array_size: i32,
    pub element_count: i32,
}
impl Binding {
    pub fn array_element(&self) -> Result<ArrayElement> {
        let s = &self.source;
        ensure!(
            s.element_type_size > 0
                && s.type_size > 0
                && s.type_size % s.element_type_size == 0
                && self.byte_offset >= 0
                && self
                    .byte_offset
                    .checked_add(self.byte_size)
                    .is_some_and(|end| end <= s.type_size),
            "invalid source-array extent"
        );
        ensure!(
            s.name_known
                && s.name_complete
                && !s.name_bytes.is_empty()
                && s.type_kind == 12
                && s.element_known,
            "scalarized source lacks a complete named array identity"
        );
        ensure!(
            self.flags & 3 == 0
                && self.byte_size == s.element_type_size
                && self.byte_offset % s.element_type_size == 0
                && self.temporary.type_kind == s.element_type_kind
                && self.temporary.type_size == self.byte_size
                && self.byte_offset.checked_mul(8) == Some(self.bit_first)
                && self
                    .byte_offset
                    .checked_add(self.byte_size)
                    .and_then(|n| n.checked_mul(8))
                    .and_then(|n| n.checked_sub(1))
                    == Some(self.bit_last),
            "scalarized interval is not one complete array element"
        );
        Ok(ArrayElement {
            name: s.name_bytes.clone(),
            element_index: (self.byte_offset / s.element_type_size) as u32,
            element_size: s.element_type_size,
            element_type_kind: s.element_type_kind,
            array_size: s.type_size,
            element_count: s.type_size / s.element_type_size,
        })
    }
}

#[derive(Clone, Debug, Serialize)]
pub struct ObjectOrigin<'a> {
    pub pass: Option<u32>,
    pub binding: Option<&'a Binding>,
    pub evidence: Evaluation,
}
/// Join a backend birth to the exact temporary Object/VarInfo observed at the
/// guarded frontend return, requiring unique creation and backend generation.
pub fn object_origin<'a>(
    history: &'a History,
    functions: &[Function],
    birth: &Birth,
) -> ObjectOrigin<'a> {
    let resolve = || -> Result<(u32, &'a Binding)> {
        let function = functions
            .iter()
            .find(|f| f.id == birth.id.function)
            .context("birth function history missing")?;
        let function_object = function
            .function_object
            .as_ref()
            .context("backend function lacks Object identity")?;
        let object = birth
            .object
            .as_ref()
            .context("backend birth lacks Object binding")?;
        ensure!(
            function.counter_observations.state == EvaluationState::Satisfied,
            "backend direct-counter capture incomplete"
        );
        let end = function
            .end_position
            .context("function has no complete end")?;
        let passes = history
            .passes
            .iter()
            .filter(|p| {
                p.function_object == *function_object
                    && p.function == Some(function.id)
                    && p.link == function.link
                    && p.begin_position > function.begin_position
                    && p.begin_position < end
            })
            .collect::<Vec<_>>();
        ensure!(
            !passes.is_empty()
                && passes
                    .iter()
                    .all(|p| p.end_position.is_some_and(|pass_end| pass_end < end)
                        && p.observations.state == EvaluationState::Satisfied),
            "matching frontend scalarization capture incomplete"
        );
        let first_birth = function
            .births
            .iter()
            .map(|b| b.trace_position)
            .min()
            .context("function has no backend births")?;
        ensure!(
            passes
                .iter()
                .all(|p| p.end_position.is_some_and(|end| end < first_birth)),
            "frontend scalarization passes overlap backend register creation"
        );
        let summary = function
            .scalarization
            .as_ref()
            .context("function end lacks independent frontend scalarization totals")?;
        ensure!(
            summary.scope == SCOPE
                && summary.passes as usize == passes.len()
                && summary.bindings as usize
                    == passes.iter().map(|p| p.bindings.len()).sum::<usize>()
                && passes.first().map(|p| p.id) == Some(summary.first_pass)
                && passes.last().map(|p| p.id) == Some(summary.last_pass),
            "frontend scalarization capture disagrees with function-end totals/identities"
        );
        ensure!(
            history.gaps.is_empty(),
            "unscoped frontend scalarization gaps"
        );
        let matches = passes
            .iter()
            .flat_map(|p| p.bindings.iter().map(move |b| (p.id, b)))
            .filter(|(_, b)| same_pointer(&b.temporary.object, &object.object))
            .collect::<Vec<_>>();
        ensure!(
            matches.len() == 1,
            "temporary Object has missing or ambiguous frontend creation"
        );
        let (pass, binding) = matches[0];
        ensure!(
            passes
                .iter()
                .find(|p| p.id == pass)
                .and_then(|p| p.end_position)
                .is_some_and(|end| end < birth.trace_position),
            "scalarization creation/pass completion does not precede backend birth"
        );
        ensure!(
            same_pointer(&binding.temporary.variable_info, &object.variable_info)
                && binding.temporary.type_kind == object.type_kind
                && binding.temporary.type_size == object.type_size,
            "frontend temporary and backend Object/type binding disagree"
        );
        ensure!(
            function
                .births
                .iter()
                .filter(|b| b
                    .object
                    .as_ref()
                    .is_some_and(
                        |o| same_pointer(&o.object, &object.object) && o.part == object.part
                    ))
                .count()
                == 1,
            "backend Object has ambiguous birth generations"
        );
        Ok((pass, binding))
    };
    match resolve() {
        Ok((pass, binding)) => ObjectOrigin {
            pass: Some(pass),
            binding: Some(binding),
            evidence: known(),
        },
        Err(error) => ObjectOrigin {
            pass: None,
            binding: None,
            evidence: gap(format!("{error:#}")),
        },
    }
}

/// Derive exact backend joins while all function contexts are still available.
/// A possible temporary with incomplete evidence remains an explicit gap.
pub fn bind_backend(history: &mut History, functions: &[Function]) {
    let mut values = vec![];
    for function in functions {
        let Some(function_object) = &function.function_object else {
            continue;
        };
        for birth in &function.births {
            let Some(object) = &birth.object else {
                continue;
            };
            let observed = history
                .passes
                .iter()
                .filter(|p| {
                    p.function_object == *function_object
                        && p.function == Some(function.id)
                        && p.link == function.link
                        && p.begin_position > function.begin_position
                        && function
                            .end_position
                            .is_some_and(|end| p.begin_position < end)
                })
                .any(|p| {
                    p.bindings
                        .iter()
                        .any(|b| same_pointer(&b.temporary.object, &object.object))
                });
            if !observed {
                continue;
            }
            let origin = object_origin(history, functions, birth);
            values.push(BackendValue {
                value: birth.id,
                class: birth.class,
                register: birth.register,
                pass: origin.pass,
                binding: origin.binding.map(|b| b.ordinal),
                array_element: origin.binding.and_then(|b| b.array_element().ok()),
                observations: origin.evidence,
            });
        }
    }
    history.backend_values = values;
}
