//! Categories defined by observed retail register ranges, not inferred source
//! roles such as address/call-result/array-promotion temporaries.
use crate::{
    evidence::{Evaluation, EvaluationState, EvidenceGap},
    trace::Record,
    value_history::{Function, RangeKind},
};
use anyhow::{Result, ensure};
use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Category {
    Physical,
    InitialObjectRange,
    BeforeCoalescingWindow,
    InsideCoalescingWindow,
    AfterCoalescingWindow,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Observation {
    pub category: Option<Category>,
    pub evidence: Evaluation,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Bounds {
    pub physical_limit: u32,
    pub initial_last: i32,
    pub first: i32,
    pub last: i32,
    pub initial_marker: Option<usize>,
    pub window_marker: Option<usize>,
    pub evidence: Evaluation,
}
fn gap(detail: &str) -> Evaluation {
    Evaluation::indeterminate(EvidenceGap::AllocationCategoryCoverage, detail)
}
pub fn unknown() -> Observation {
    Observation {
        category: None,
        evidence: gap("no complete range-category observations"),
    }
}
impl Bounds {
    pub fn classify(&self, register: u32) -> Observation {
        if self.evidence.state != EvaluationState::Satisfied {
            return Observation {
                category: None,
                evidence: self.evidence.clone(),
            };
        }
        let category = if register < self.physical_limit {
            Category::Physical
        } else if i64::from(register) <= i64::from(self.initial_last) {
            Category::InitialObjectRange
        } else if i64::from(register) < i64::from(self.first) {
            Category::BeforeCoalescingWindow
        } else if i64::from(register) <= i64::from(self.last) {
            Category::InsideCoalescingWindow
        } else {
            Category::AfterCoalescingWindow
        };
        Observation {
            category: Some(category),
            evidence: Evaluation::satisfied(),
        }
    }
}
pub fn from_graph(
    r: &Record,
    function: &Function,
    class: u8,
    physical_limit: u32,
) -> Result<Option<Bounds>> {
    ensure!(
        class < 5 && physical_limit <= 65536,
        "unsupported category register class or physical boundary"
    );
    if !r.fields.contains_key("category_ready") {
        return Ok(None);
    }
    let ready: u32 = r.number("category_ready")?;
    ensure!(ready <= 1, "invalid category readiness bit");
    let mut bounds = Bounds {
        physical_limit,
        initial_last: r.number("initial_last")?,
        first: r.number("first")?,
        last: r.number("last")?,
        initial_marker: None,
        window_marker: None,
        evidence: gap("range markers or bounds are incomplete"),
    };
    if class < 2 {
        return Ok(Some(bounds));
    }
    let index = usize::from(class - 2);
    let initial = function
        .ranges
        .iter()
        .rev()
        .find(|e| e.trace_position < r.line && matches!(e.range, RangeKind::Initial { .. }));
    let window = function
        .ranges
        .iter()
        .rev()
        .find(|e| e.trace_position < r.line && matches!(e.range, RangeKind::Window { .. }));
    if let (Some(initial), Some(window)) = (initial, window) {
        bounds.initial_marker = Some(initial.trace_position);
        bounds.window_marker = Some(window.trace_position);
        let RangeKind::Initial { last } = initial.range else {
            unreachable!()
        };
        let RangeKind::Window { first, .. } = window.range else {
            unreachable!()
        };
        if ready == 1
            && function.range_observations.state == EvaluationState::Satisfied
            && initial.trace_position < window.trace_position
            && last[index] == bounds.initial_last
            && first[index] == bounds.first
            && bounds.initial_last >= physical_limit as i32 - 1
            && bounds.initial_last < bounds.first
            && bounds.first >= physical_limit as i32
            && bounds.first <= bounds.last
            && bounds.last <= 65536
        {
            bounds.evidence = Evaluation::satisfied();
        }
    }
    Ok(Some(bounds))
}
