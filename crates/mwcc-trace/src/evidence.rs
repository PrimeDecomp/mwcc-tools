//! Shared result semantics for source search, reduction and corpus predicates.
use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum EvaluationState {
    Satisfied,
    Unsatisfied,
    Indeterminate,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum EvidenceGap {
    CoalescingCoverage,
    BackendLifecycleCoverage,
    AllocationCategoryCoverage,
    AmbiguousCorrespondence,
    MissingGraphSnapshot,
    MissingGraphNode,
    MissingRelocationEvidence,
    MissingSemanticCheck,
    ObjectivePredicate,
    CompilationCoverage,
    BuildInputCoverage,
    CompilerStateCoverage,
    ArrayPromotionCoverage,
    CallResultCoverage,
    CallArgumentCoverage,
    SourceArenaCoverage,
    SourceNodeCoverage,
    ScalarizationCoverage,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Reason {
    pub code: EvidenceGap,
    pub detail: String,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Evaluation {
    pub state: EvaluationState,
    pub reasons: Vec<Reason>,
}

impl Evaluation {
    /// Satisfied, with no reasons.
    pub fn satisfied() -> Self {
        Self {
            state: EvaluationState::Satisfied,
            reasons: Vec::new(),
        }
    }

    pub fn indeterminate(code: EvidenceGap, detail: impl Into<String>) -> Self {
        Self {
            state: EvaluationState::Indeterminate,
            reasons: vec![Reason {
                code,
                detail: detail.into(),
            }],
        }
    }
}
