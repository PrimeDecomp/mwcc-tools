//! Guarded backend creation paths. These observations retain source objects and
//! direct lowering inputs without claiming complete inline or lifetime ancestry.
use crate::{
    evidence::{Evaluation, EvaluationState, EvidenceGap},
    trace::Record,
    value_history::{Birth, Function},
};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ArrayPromotion {
    pub trace_position: usize,
    pub promotion: String,
    pub object: String,
    pub element_index: u32,
    pub element_size: i32,
    pub array_size: i32,
    pub element_count: i32,
    pub flags: u8,
    pub datatype: u8,
    pub storage: u16,
    pub type_kind: i32,
    pub type_size: i32,
    pub name_known: bool,
    pub name_complete: bool,
    pub name_bytes: Vec<u8>,
    pub name_text: Option<String>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ResultPart {
    Single,
    FirstWord,
    SecondWord,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct CallResult {
    pub trace_position: usize,
    pub part: ResultPart,
    pub item: String,
    pub kind: u8,
    /// Capture-local function-reference ENode, not a function Object or a
    /// resolved qualified symbol.
    pub callee: String,
    pub return_type: String,
    pub type_kind: i32,
    pub type_size: i32,
    pub output: String,
    pub output_kind: u8,
    /// Observed before the retail output-register stores. Inactive bytes and
    /// pointers are raw evidence, never state-fingerprint or identity inputs.
    pub output_bytes: Vec<u8>,
    pub output_complete: bool,
    pub tree_known: bool,
    pub tree_complete: bool,
    pub tree_bytes: Vec<u8>,
    pub tree_text: Option<String>,
}
fn pointer(record: &Record, field: &str) -> Result<String> {
    ensure!(
        u32::from_str_radix(record.get(field)?, 16)? != 0,
        "null {field} origin pointer"
    );
    Ok(record.get(field)?.into())
}
fn bit(record: &Record, field: &str) -> Result<bool> {
    let value = record.number::<u8>(field)?;
    ensure!(value <= 1, "invalid {field} bit");
    Ok(value == 1)
}
fn bytes(record: &Record, field: &str, limit: usize) -> Result<Vec<u8>> {
    let text = record.get(field)?;
    ensure!(
        text.is_ascii() && text.len().is_multiple_of(2) && text.len() <= limit * 2,
        "invalid {field} hex"
    );
    (0..text.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&text[i..i + 2], 16).context("invalid origin byte"))
        .collect()
}
/// The guarded scalar optimizer has separate FPR/GPR increments. A VMX route
/// is deliberately outside this scope.
pub fn array_class(site: u32) -> Option<u8> {
    match site {
        0x571072 | 0x571d32 | 0x573ff2 => Some(3),
        0x571085 | 0x571d45 | 0x574005 => Some(4),
        _ => None,
    }
}
pub fn call_role(site: u32) -> Option<(u8, ResultPart)> {
    match site {
        0x526c87 | 0x5277e7 | 0x528677 | 0x526d1d | 0x52787d | 0x52870d => {
            Some((3, ResultPart::Single))
        }
        0x526cd4 | 0x527834 | 0x5286c4 => Some((2, ResultPart::Single)),
        0x526dc2 | 0x527922 | 0x5287b2 => Some((4, ResultPart::FirstWord)),
        0x526dd2 | 0x527932 | 0x5287c2 => Some((4, ResultPart::SecondWord)),
        0x526e2d | 0x52798d | 0x52881d => Some((4, ResultPart::Single)),
        _ => None,
    }
}
fn attached(record: &Record, birth: &Birth) -> Result<()> {
    ensure!(
        record.number::<u32>("schema")? == 1,
        "unsupported backend origin schema"
    );
    ensure!(
        record.number::<u32>("function")? == birth.id.function
            && record.number::<u32>("value")? == birth.id.sequence
            && record.number::<u32>("register")? == birth.register
            && record.line > birth.trace_position,
        "backend origin does not match its current birth"
    );
    Ok(())
}
pub fn array(record: &Record, birth: &Birth) -> Result<ArrayPromotion> {
    attached(record, birth)?;
    ensure!(
        array_class(birth.site) == Some(birth.class),
        "array origin disagrees with guarded birth site/class"
    );
    let element_index = record.number::<u32>("element_index")?;
    let element_size = record.number::<i32>("element_size")?;
    let array_size = record.number::<i32>("array_size")?;
    let element_count = record.number::<i32>("element_count")?;
    ensure!(
        element_size > 0
            && (1..=8).contains(&element_count)
            && element_index < element_count as u32
            && element_size.checked_mul(element_count) == Some(array_size),
        "invalid promoted array element bounds"
    );
    let name_known = bit(record, "name_known")?;
    let name_complete = bit(record, "name_complete")?;
    let name_bytes = bytes(record, "name_hex", 256)?;
    ensure!(
        name_known || (!name_complete && name_bytes.is_empty()),
        "unknown array name has contents or complete flag"
    );
    let flags = record.number::<u8>("flags")?;
    let type_kind = record.number::<i32>("type_kind")?;
    let type_size = record.number::<i32>("type_size")?;
    ensure!(
        flags & 1 == 0
            && (flags & 2 != 0) == (birth.class == 3)
            && type_kind == 12
            && type_size == array_size,
        "array origin flags or source type disagree with scalar promotion"
    );
    Ok(ArrayPromotion {
        trace_position: record.line,
        promotion: pointer(record, "promotion")?,
        object: pointer(record, "object")?,
        element_index,
        element_size,
        array_size,
        element_count,
        flags,
        datatype: record.number("datatype")?,
        storage: record.number("storage")?,
        type_kind,
        type_size,
        name_known,
        name_complete,
        name_text: String::from_utf8(name_bytes.clone()).ok(),
        name_bytes,
    })
}
pub fn call(record: &Record, birth: &Birth) -> Result<CallResult> {
    attached(record, birth)?;
    let part = match record.number::<u8>("part")? {
        1 => ResultPart::Single,
        2 => ResultPart::FirstWord,
        3 => ResultPart::SecondWord,
        _ => anyhow::bail!("unknown call result part"),
    };
    ensure!(
        call_role(birth.site) == Some((birth.class, part)),
        "call origin disagrees with guarded birth site/class/part"
    );
    let kind = record.number("kind")?;
    ensure!(
        matches!(kind, 54 | 55),
        "call origin is not a direct or indirect call node"
    );
    let output_bytes = bytes(record, "output_hex", 24)?;
    let output_kind = record.number("output_kind")?;
    let output_complete = bit(record, "output_complete")?;
    let expected_kind = match (birth.class, part) {
        (3, ResultPart::Single) => 5,
        (2, ResultPart::Single) => 6,
        (4, ResultPart::Single) => 0,
        (4, ResultPart::FirstWord | ResultPart::SecondWord) => 3,
        _ => anyhow::bail!("unsupported call result class/part"),
    };
    ensure!(
        output_bytes.len() == 24
            && output_bytes[0] == output_kind
            && output_kind == expected_kind
            && !output_complete,
        "call birth output is a partial 24-byte observation"
    );
    let tree_known = bit(record, "tree_known")?;
    let tree_complete = bit(record, "tree_complete")?;
    let tree_bytes = bytes(record, "tree_hex", 2047)?;
    ensure!(
        !tree_complete && (tree_known || tree_bytes.is_empty()),
        "call origin does not establish complete source ancestry"
    );
    Ok(CallResult {
        trace_position: record.line,
        part,
        item: pointer(record, "item")?,
        kind,
        callee: pointer(record, "callee")?,
        return_type: pointer(record, "return_type")?,
        type_kind: record.number("type_kind")?,
        type_size: record.number("type_size")?,
        output: pointer(record, "output")?,
        output_kind,
        output_bytes,
        output_complete,
        tree_known,
        tree_complete,
        tree_text: String::from_utf8(tree_bytes.clone()).ok(),
        tree_bytes,
    })
}
fn known() -> Evaluation {
    Evaluation {
        state: EvaluationState::Satisfied,
        reasons: vec![],
    }
}
pub fn array_gap() -> Evaluation {
    Evaluation::indeterminate(
        EvidenceGap::ArrayPromotionCoverage,
        "no complete guarded scalar array-promotion capture",
    )
}
pub fn call_gap() -> Evaluation {
    Evaluation::indeterminate(
        EvidenceGap::CallResultCoverage,
        "no complete guarded call-result capture",
    )
}
pub fn finish(function: &mut Function, valid: bool) {
    if valid
        && function.ended
        && function.array_promotion_scope.as_deref() == Some("guarded_scalar_v1")
        && function.expected_array_promotions.map(|n| n as usize)
            == Some(
                function
                    .births
                    .iter()
                    .filter(|b| b.array_promotion.is_some())
                    .count(),
            )
        && function
            .births
            .iter()
            .filter(|b| array_class(b.site).is_some())
            .all(|b| b.array_promotion.is_some())
    {
        function.array_promotion_observations = known();
    }
    if valid
        && function.ended
        && function.call_result_scope.as_deref() == Some("guarded_call_result_v1")
        && function.expected_call_results.map(|n| n as usize)
            == Some(
                function
                    .births
                    .iter()
                    .filter(|b| b.call_result.is_some())
                    .count(),
            )
        && function
            .births
            .iter()
            .filter(|b| call_role(b.site).is_some())
            .all(|b| b.call_result.is_some())
        && call_pairs(function)
    {
        function.call_result_observations = known();
    }
}
fn call_pairs(function: &Function) -> bool {
    function.births.iter().enumerate().all(|(index, birth)| {
        let Some(result) = &birth.call_result else {
            return true;
        };
        if result.part == ResultPart::Single {
            return true;
        }
        let (first, second) = if result.part == ResultPart::FirstWord {
            (Some(birth), function.births.get(index + 1))
        } else {
            (
                index.checked_sub(1).and_then(|i| function.births.get(i)),
                Some(birth),
            )
        };
        let (Some(first), Some(second)) = (first, second) else {
            return false;
        };
        let (Some(a), Some(b)) = (&first.call_result, &second.call_result) else {
            return false;
        };
        a.part == ResultPart::FirstWord
            && b.part == ResultPart::SecondWord
            && first.class == 4
            && second.class == 4
            && first.register.checked_add(1) == Some(second.register)
            && first.site.checked_add(0x10) == Some(second.site)
            && a.item == b.item
            && a.callee == b.callee
            && a.return_type == b.return_type
            && a.type_kind == b.type_kind
            && a.type_size == b.type_size
            && a.output == b.output
    })
}
