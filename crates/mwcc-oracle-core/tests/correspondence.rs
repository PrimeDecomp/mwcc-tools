use mwcc_oracle_core::{
    colouring::{
        correspondence::{self, CorrespondenceState, EvaluationState, Selection, SnapshotPhase},
        target::{self, Remapped},
    },
    trace::{Allocation, Block, Instruction, Node, Operand, Round, Snapshot},
    version::Version,
};
use std::collections::{BTreeMap, BTreeSet};

fn allocation(registers: &[u32], nodes: &[(u32, i32, &[u32])]) -> Allocation {
    let snapshot = Snapshot {
        function: "test".into(),
        link: "test__Fv".into(),
        blocks: vec![Block {
            successors: vec![],
            pointer: "block".into(),
            index: 0,
            weight: 1,
            instructions: registers
                .iter()
                .map(|r| {
                    let mut raw = [0; 12];
                    raw[1] = 4;
                    raw[4..6].copy_from_slice(&(*r as u16).to_le_bytes());
                    Instruction {
                        pointer: format!("instruction{r}"),
                        opcode: 1,
                        flags: 0,
                        operands: vec![Operand { raw }],
                        line: None,
                    }
                })
                .collect(),
        }],
        rounds: vec![Round {
            class: 4,
            k: 24,
            start: 32,
            count: 100,
            nodes: nodes
                .iter()
                .map(|(r, degree, neighbors)| {
                    (
                        *r,
                        Node {
                            pointer: format!("node{r}"),
                            degree: *degree,
                            flags: 0,
                            color: -1,
                            neighbors: neighbors.to_vec(),
                        },
                    )
                })
                .collect(),
            ..Default::default()
        }],
        frame: None,
        layout: None,
        schedule: vec![],
    };
    Allocation {
        before: snapshot.clone(),
        after: Snapshot {
            rounds: vec![],
            ..snapshot
        },
        inliner_position: 0,
    }
}

fn votes(rows: &[(u32, &[(u32, usize)])]) -> Remapped {
    Remapped {
        votes: rows
            .iter()
            .map(|(r, candidates)| (("GPR".into(), *r), candidates.to_vec()))
            .collect(),
        mapping: BTreeMap::new(),
        unsure: BTreeMap::new(),
        paired: 0,
        instructions: (0, 0),
    }
}

fn compare(a: &Allocation, b: &Allocation, votes: &Remapped) -> correspondence::Comparison {
    correspondence::compare(
        a,
        b,
        votes,
        "GPR",
        Selection::default(),
        Selection::default(),
    )
    .unwrap()
}

#[test]
fn graph_only_values_and_boundary_edges_are_retained_without_elimination_claims() {
    let a = allocation(&[32], &[(32, 9, &[33, 33, 3]), (33, 1, &[32])]);
    let b = allocation(&[42], &[(42, 1, &[3])]);
    let report = compare(&a, &b, &votes(&[(32, &[(42, 1)])]));
    assert_eq!(report.elimination.state, EvaluationState::Indeterminate);
    let graph = report.first.as_ref().unwrap();
    assert_eq!(graph.graph_only, BTreeSet::from([33]));
    assert_eq!(graph.nodes[&32].stored_degree, 9);
    assert_eq!(graph.nodes[&32].adjacency_degree, 2);
    assert_eq!(graph.nodes[&32].recorded_neighbors, [33, 33, 3]);
    let delta = report.delta.unwrap();
    assert_eq!(delta.unmatched_first, BTreeSet::from([33]));
    assert_eq!(delta.boundary_first, BTreeSet::from([(32, 33)]));
    assert!(delta.inferred_edges_removed.is_empty());
}

#[test]
fn majority_votes_do_not_hide_ambiguity_or_competing_target_owners() {
    let a = allocation(&[32, 33], &[(32, 0, &[]), (33, 0, &[])]);
    let b = allocation(&[42, 43], &[(42, 0, &[]), (43, 0, &[])]);
    let report = compare(
        &a,
        &b,
        &votes(&[(32, &[(42, 99), (43, 1)]), (33, &[(43, 1)])]),
    );
    assert!(report.inferred_mapping.is_empty());
    assert_eq!(
        report.correspondence[0].state,
        CorrespondenceState::AmbiguousVotes
    );
    assert_eq!(
        report.correspondence[1].state,
        CorrespondenceState::CollidingTargets
    );
    assert_eq!(report.correspondence[0].candidates.len(), 2);
}

#[test]
fn many_to_one_votes_are_not_claimed_to_be_observed_merges() {
    let a = allocation(&[32, 33], &[(32, 1, &[33]), (33, 1, &[32])]);
    let b = allocation(&[42], &[(42, 0, &[])]);
    let report = compare(&a, &b, &votes(&[(32, &[(42, 1)]), (33, &[(42, 1)])]));
    assert!(report.inferred_mapping.is_empty());
    assert!(
        report
            .correspondence
            .iter()
            .all(|r| r.state == CorrespondenceState::CollidingTargets)
    );
    assert_eq!(
        report.delta.as_ref().unwrap().unmatched_second,
        BTreeSet::from([42])
    );
    assert_eq!(report.elimination.state, EvaluationState::Indeterminate);
}

#[test]
fn explicit_class_rounds_change_graph_comparison_without_changing_instructions() {
    let a = allocation(&[32, 33], &[(32, 1, &[33]), (33, 1, &[32])]);
    let mut b = allocation(&[42, 43], &[(42, 1, &[43]), (43, 1, &[42])]);
    let mut next = b.before.rounds[0].clone();
    for node in next.nodes.values_mut() {
        node.neighbors.clear();
        node.degree = 0;
    }
    b.before.rounds.push(next);
    let inferred = target::remap(&a, &b, Version::Gc27);
    let default = compare(&a, &b, &inferred);
    assert_eq!(default.second.as_ref().unwrap().class_round, 1);
    assert_eq!(
        default.delta.unwrap().inferred_edges_removed,
        BTreeSet::from([(42, 43)])
    );
    let first = correspondence::compare(
        &a,
        &b,
        &inferred,
        "GPR",
        Selection::default(),
        Selection {
            phase: SnapshotPhase::Before,
            round: Some(0),
        },
    )
    .unwrap();
    assert!(first.delta.unwrap().inferred_edges_removed.is_empty());
    assert!(
        correspondence::compare(
            &a,
            &b,
            &inferred,
            "GPR",
            Selection::default(),
            Selection {
                phase: SnapshotPhase::Before,
                round: Some(2)
            }
        )
        .is_err()
    );
}

#[test]
fn physical_projections_missing_rounds_and_missing_neighbor_nodes_remain_explicit() {
    let a = allocation(&[32], &[(32, 2, &[3, 99])]);
    let b = allocation(&[3], &[(42, 0, &[])]);
    let inferred = target::remap(&a, &b, Version::Gc27);
    assert!(inferred.votes.is_empty()); // Virtual-to-physical changes break instruction shape matching.
    let report = compare(&a, &b, &votes(&[(32, &[(3, 1)])]));
    assert_eq!(
        report.correspondence[0].state,
        CorrespondenceState::PhysicalProjection
    );
    assert_eq!(
        report.first.as_ref().unwrap().missing_neighbor_nodes,
        BTreeSet::from([99])
    );
    assert_eq!(
        report.second.as_ref().unwrap().graph_only,
        BTreeSet::from([42])
    );
    let missing = correspondence::compare(
        &a,
        &b,
        &inferred,
        "GPR",
        Selection::default(),
        Selection {
            phase: SnapshotPhase::After,
            round: None,
        },
    )
    .unwrap();
    assert!(missing.second.is_none() && missing.delta.is_none());
    assert_eq!(missing.selection_second.phase, SnapshotPhase::After);
    assert_eq!(missing.elimination.state, EvaluationState::Indeterminate);
}
