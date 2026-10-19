//! Tagged retail Operand observations. Only fields defined by the observed tag
//! are interpreted; an observation does not establish semantic value lifetime.
use crate::{
    evidence::{Evaluation, EvaluationState, EvidenceGap},
    trace::Record,
};
use anyhow::{Result, ensure};
use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Observation {
    pub trace_position: usize,
    pub operand: Option<String>,
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

/// Parse the common tagged fields of a dispatch or argument operand record.
/// The caller validates the enclosing scope and any record-specific fields.
pub fn parse(record: &Record) -> Result<Observation> {
    let kind: u8 = record.number::<u32>("kind")?.try_into()?;
    let number = |field: &str| -> Result<Option<u32>> {
        record
            .fields
            .get(field)
            .map(|v| v.parse().map_err(Into::into))
            .transpose()
    };
    let signed = |field: &str| -> Result<Option<i32>> {
        record
            .fields
            .get(field)
            .map(|v| v.parse().map_err(Into::into))
            .transpose()
    };
    let class = number("cls")?.map(u8::try_from).transpose()?;
    let register = number("register")?;
    let second = number("second")?;
    let index = number("index")?;
    let object = record.fields.get("object").cloned();
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
        "observation disagrees with its Operand tag"
    );
    if let Some(object) = &object {
        ensure!(
            u32::from_str_radix(object, 16).is_ok(),
            "invalid Operand object identity"
        );
    }
    let operand = record
        .fields
        .get("operand")
        .map(|value| {
            ensure!(
                u32::from_str_radix(value, 16).is_ok(),
                "invalid output Operand identity"
            );
            Ok::<_, anyhow::Error>(value.clone())
        })
        .transpose()?;
    Ok(Observation {
        trace_position: record.line,
        operand,
        kind,
        class,
        register,
        second,
        index,
        object,
        displacement,
        immediate,
        evidence: if kind <= 11 {
            Evaluation {
                state: EvaluationState::Satisfied,
                reasons: vec![],
            }
        } else {
            Evaluation::indeterminate(EvidenceGap::BackendLifecycleCoverage, "unknown Operand tag")
        },
    })
}
