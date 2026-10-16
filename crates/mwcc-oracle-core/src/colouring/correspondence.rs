//! Evidence-aware comparison. Instruction votes are hypotheses, not lifecycle proof.
use super::target::Remapped;
use crate::evidence::EvidenceGap;
pub use crate::evidence::{Evaluation, EvaluationState};
use crate::{
    trace::{Allocation, Round, Snapshot},
    version::class_name,
};
use anyhow::{Result, ensure};
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet};

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SnapshotPhase {
    #[default]
    Before,
    After,
}

/// `round` is a zero-based occurrence within the requested class, not the
/// absolute index in the trace. None selects its last recorded occurrence.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct Selection {
    pub phase: SnapshotPhase,
    pub round: Option<usize>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct NodeObservation {
    pub register: u32,
    pub visible_in_instructions: bool,
    pub stored_degree: i32,
    pub adjacency_degree: usize,
    pub recorded_neighbors: Vec<u32>,
    pub neighbors: BTreeSet<u32>,
    pub flags: u32,
    pub color: i32,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct GraphSnapshot {
    pub function: String,
    pub link: String,
    pub phase: SnapshotPhase,
    pub class_round: usize,
    pub trace_round: usize,
    pub register_count: i32,
    pub declared_start: u32,
    pub declared_count: u32,
    pub nodes: BTreeMap<u32, NodeObservation>,
    pub graph_only: BTreeSet<u32>,
    pub instruction_only: BTreeSet<u32>,
    pub missing_neighbor_nodes: BTreeSet<u32>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum CorrespondenceState {
    InferredFromInstructions,
    AmbiguousVotes,
    CollidingTargets,
    PhysicalProjection,
    MissingTargetNode,
    Unmatched,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Candidate {
    pub register: u32,
    pub votes: usize,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Correspondence {
    pub first: u32,
    pub candidates: Vec<Candidate>,
    pub state: CorrespondenceState,
    /// Populated only for an unambiguous, injective instruction hypothesis.
    /// This is deliberately not called a proven value identity.
    pub inferred_second: Option<u32>,
}

pub type Edge = (u32, u32);

#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct GraphDelta {
    /// Compared edges use the second graph's numbers. Classification is
    /// conditional on the explicitly reported instruction correspondence.
    pub inferred_edges_added: BTreeSet<Edge>,
    pub inferred_edges_removed: BTreeSet<Edge>,
    /// Boundary edges retain their own graph's numbers. They are NOT classified
    /// as additions/removals when an endpoint lacks correspondence.
    pub boundary_first: BTreeSet<Edge>,
    pub boundary_second: BTreeSet<Edge>,
    pub unmatched_first: BTreeSet<u32>,
    pub unmatched_second: BTreeSet<u32>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Comparison {
    pub format_version: u32,
    pub class: String,
    pub selection_first: Selection,
    pub selection_second: Selection,
    pub first: Option<GraphSnapshot>,
    pub second: Option<GraphSnapshot>,
    pub paired_instructions: usize,
    pub instruction_counts: (usize, usize),
    pub correspondence: Vec<Correspondence>,
    pub inferred_mapping: BTreeMap<u32, u32>,
    pub delta: Option<GraphDelta>,
    pub limitations: Vec<String>,
    pub elimination: Evaluation,
}

fn snapshot(allocation: &Allocation, phase: SnapshotPhase) -> &Snapshot {
    match phase {
        SnapshotPhase::Before => &allocation.before,
        SnapshotPhase::After => &allocation.after,
    }
}

fn select<'a>(
    snapshot: &'a Snapshot,
    class: &str,
    selection: Selection,
) -> Result<Option<(usize, usize, &'a Round)>> {
    let rounds: Vec<_> = snapshot
        .rounds
        .iter()
        .enumerate()
        .filter(|(_, r)| class_name(r.class) == class)
        .collect();
    if let Some(round) = selection.round {
        ensure!(
            round < rounds.len(),
            "{class} {:?} class round {round} unavailable ({} recorded)",
            selection.phase,
            rounds.len()
        );
        let (index, data) = rounds[round];
        return Ok(Some((round, index, data)));
    }
    Ok(rounds
        .last()
        .map(|(index, data)| (rounds.len() - 1, *index, *data)))
}

fn observe(
    allocation: &Allocation,
    class: &str,
    selection: Selection,
) -> Result<Option<GraphSnapshot>> {
    let snapshot = snapshot(allocation, selection.phase);
    let Some((class_round, trace_round, round)) = select(snapshot, class, selection)? else {
        return Ok(None);
    };
    let visible: BTreeSet<_> = snapshot
        .blocks
        .iter()
        .flat_map(|b| &b.instructions)
        .flat_map(|i| &i.operands)
        .filter(|o| o.is_register() && o.class() == class && o.register() >= 32)
        .map(|o| o.register())
        .collect();
    let present: BTreeSet<_> = round.nodes.keys().copied().filter(|r| *r >= 32).collect();
    let nodes = round
        .nodes
        .iter()
        .map(|(register, node)| {
            let neighbors: BTreeSet<_> = node.neighbors.iter().copied().collect();
            (
                *register,
                NodeObservation {
                    register: *register,
                    visible_in_instructions: visible.contains(register),
                    stored_degree: node.degree,
                    adjacency_degree: neighbors.len(),
                    recorded_neighbors: node.neighbors.clone(),
                    neighbors,
                    flags: node.flags,
                    color: node.color,
                },
            )
        })
        .collect();
    let missing_neighbor_nodes = round
        .nodes
        .values()
        .flat_map(|n| &n.neighbors)
        .copied()
        .filter(|n| *n >= 32 && !round.nodes.contains_key(n))
        .collect();
    Ok(Some(GraphSnapshot {
        function: snapshot.function.clone(),
        link: snapshot.link.clone(),
        phase: selection.phase,
        class_round,
        trace_round,
        register_count: round.k,
        declared_start: round.start,
        declared_count: round.count,
        nodes,
        graph_only: present.difference(&visible).copied().collect(),
        instruction_only: visible.difference(&present).copied().collect(),
        missing_neighbor_nodes,
    }))
}

fn edges(snapshot: &GraphSnapshot) -> BTreeSet<Edge> {
    snapshot
        .nodes
        .iter()
        .flat_map(|(r, node)| node.neighbors.iter().map(|n| ((*r).min(*n), (*r).max(*n))))
        .collect()
}

/// Compare complete captured graph domains, retaining uncertainty. Existing ra/fr
/// records do not establish backend creation/merge coverage, so they can never
/// prove elimination. A later provenance layer must supply that evidence.
pub fn compare(
    first: &Allocation,
    second: &Allocation,
    remapped: &Remapped,
    class: &str,
    first_selection: Selection,
    second_selection: Selection,
) -> Result<Comparison> {
    ensure!(
        ["GPR", "FPR", "VR", "CR", "SPR"].contains(&class),
        "unsupported register class {class}"
    );
    let a = observe(first, class, first_selection)?;
    let b = observe(second, class, second_selection)?;
    let mut report = Comparison { format_version: 2, class: class.into(), selection_first: first_selection,
        selection_second: second_selection, first: a, second: b,
        paired_instructions: remapped.paired, instruction_counts: remapped.instructions, correspondence: vec![],
        inferred_mapping: BTreeMap::new(), delta: None,
        limitations: vec!["Instruction correspondence is inferred; backend creation and merge coverage is unavailable.".into(),
            "Instruction lists are snapshot-level, not per-round lifecycle evidence.".into()],
        elimination: Evaluation::indeterminate(EvidenceGap::BackendLifecycleCoverage,
            "Unmatched or instruction-invisible values are not proven eliminated.") };
    let (Some(a), Some(b)) = (&report.first, &report.second) else {
        report
            .limitations
            .push("A requested graph snapshot is missing.".into());
        return Ok(report);
    };
    // Conservatively reserve every candidate target, including votes that lose a
    // majority. Otherwise a competing ambiguous node could make a pair look unique.
    let mut owners: BTreeMap<u32, BTreeSet<u32>> = BTreeMap::new();
    for ((c, source), votes) in &remapped.votes {
        if c == class && a.nodes.contains_key(source) {
            for (target, _) in votes {
                owners.entry(*target).or_default().insert(*source);
            }
        }
    }
    for source in a.nodes.keys().copied().filter(|r| *r >= 32) {
        let candidates: Vec<_> = remapped
            .votes
            .get(&(class.into(), source))
            .into_iter()
            .flatten()
            .map(|(register, votes)| Candidate {
                register: *register,
                votes: *votes,
            })
            .collect();
        let state = if candidates.is_empty() {
            CorrespondenceState::Unmatched
        } else if candidates.len() != 1 {
            CorrespondenceState::AmbiguousVotes
        } else if candidates[0].register < 32 {
            CorrespondenceState::PhysicalProjection
        } else if !b.nodes.contains_key(&candidates[0].register) {
            CorrespondenceState::MissingTargetNode
        } else if owners[&candidates[0].register].len() != 1 {
            CorrespondenceState::CollidingTargets
        } else {
            CorrespondenceState::InferredFromInstructions
        };
        let inferred_second = (state == CorrespondenceState::InferredFromInstructions)
            .then(|| candidates[0].register);
        if let Some(target) = inferred_second {
            report.inferred_mapping.insert(source, target);
        }
        report.correspondence.push(Correspondence {
            first: source,
            candidates,
            state,
            inferred_second,
        });
    }
    let unmatched_first = a
        .nodes
        .keys()
        .copied()
        .filter(|r| *r >= 32 && !report.inferred_mapping.contains_key(r))
        .collect();
    let back: BTreeMap<_, _> = report
        .inferred_mapping
        .iter()
        .map(|(a, b)| (*b, *a))
        .collect();
    let unmatched_second = b
        .nodes
        .keys()
        .copied()
        .filter(|r| *r >= 32 && !back.contains_key(r))
        .collect();
    let mut delta = GraphDelta {
        unmatched_first,
        unmatched_second,
        ..GraphDelta::default()
    };
    let project = |r: u32| {
        if r < 32 {
            Some(r)
        } else {
            report.inferred_mapping.get(&r).copied()
        }
    };
    let mut first_edges = BTreeSet::new();
    for (x, y) in edges(a) {
        if let (Some(x), Some(y)) = (project(x), project(y)) {
            first_edges.insert((x.min(y), x.max(y)));
        } else {
            delta.boundary_first.insert((x, y));
        }
    }
    let mut second_edges = BTreeSet::new();
    for (x, y) in edges(b) {
        if (x < 32 || back.contains_key(&x)) && (y < 32 || back.contains_key(&y)) {
            second_edges.insert((x, y));
        } else {
            delta.boundary_second.insert((x, y));
        }
    }
    delta.inferred_edges_added = second_edges.difference(&first_edges).copied().collect();
    delta.inferred_edges_removed = first_edges.difference(&second_edges).copied().collect();
    if a.class_round != b.class_round {
        report
            .limitations
            .push("The selected class-round indices differ.".into());
    }
    if a.phase != b.phase {
        report
            .limitations
            .push("The selected instruction/graph snapshot phases differ.".into());
    }
    if !a.missing_neighbor_nodes.is_empty() || !b.missing_neighbor_nodes.is_empty() {
        report
            .limitations
            .push("Some virtual neighbors have no captured node record.".into());
    }
    report.delta = Some(delta);
    Ok(report)
}
