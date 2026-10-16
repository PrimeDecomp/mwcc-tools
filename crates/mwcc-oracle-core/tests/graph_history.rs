use mwcc_oracle_core::{
    evidence::{Evaluation, EvaluationState},
    graph_history,
    provenance::{self, Candidate, Decision, Pass},
    value_history::{self, ValueId},
};
use std::collections::BTreeMap;
const BEGIN: &str = "pv.function.begin schema=1 function=1 parent=0 c0=4 c1=8 c2=32 c3=32 c4=32 coverage=direct_counter_writes link=sample\n";
fn birth(id: u32, register: u32) -> String {
    format!(
        "pv.value.birth schema=1 function=1 value={id} cls=4 register={register} count={} physical=32 site=400000 expression=0 phase=0 line=-1 category=unknown\n",
        register + 1
    )
}
fn end(births: u32, count: u32) -> String {
    format!(
        "pv.function.end function=1 births={births} graphs=1 c0=4 c1=8 c2=32 c3=32 c4={count}\n"
    )
}
fn graph(id: u32, births: u32, registers: std::ops::Range<u32>, visible: Option<u32>) -> String {
    let mut s = format!(
        "pv.graph.begin schema=1 function=1 graph={id} births={births} cls=4 physical=32 start={} count={} phase=3 boundary=before_simplify\n",
        registers.start, registers.end
    );
    if let Some(r) = visible {
        let mut operand = [0u8; 12];
        operand[1] = 4;
        operand[4..6].copy_from_slice(&(r as u16).to_le_bytes());
        let hex: String = operand.iter().map(|b| format!("{b:02x}")).collect();
        s += &format!("pv.graph.instruction p=123 op=1 flags=0 n=1 line=2 ops={hex}\n");
    }
    for r in registers.clone() {
        s += &format!(
            "pv.graph.node v={r} visible={} deg=3 flags=0 color=-1 n=0,0,35,\n",
            u32::from(visible == Some(r))
        );
    }
    s += &format!(
        "pv.graph.end function=1 graph={id} nodes={} instructions={} complete=1\n",
        registers.len(),
        u32::from(visible.is_some())
    );
    s
}
fn satisfied() -> Evaluation {
    Evaluation {
        state: EvaluationState::Satisfied,
        reasons: vec![],
    }
}
#[test]
fn visibility_uses_the_graph_boundary_and_preserves_degree_evidence() {
    // The old before snapshot mentions 33; the actual graph instructions mention 32.
    let trace = format!(
        "{BEGIN}{}{}ra.before function=sample link=sample\nra.i p=1 op=1 flags=0 n=1 ops=000400002100000000000000\nra.end\n{}{}",
        birth(1, 32),
        birth(2, 33),
        graph(1, 2, 32..34, Some(32)),
        end(2, 34)
    );
    let report = provenance::parse(&trace).unwrap();
    let g = &report.graphs.graphs[0];
    assert_eq!(g.observations.state, EvaluationState::Satisfied);
    assert_eq!(g.graph_only.iter().copied().collect::<Vec<_>>(), vec![33]);
    assert!(g.nodes[&32].visible_in_instructions);
    assert_eq!(g.nodes[&32].stored_degree, 3);
    assert_eq!(g.nodes[&32].recorded_neighbors, vec![0, 0, 35]);
    assert_eq!(g.nodes[&32].adjacency_degree, 2);
    assert_eq!(
        g.missing_neighbor_nodes.iter().copied().collect::<Vec<_>>(),
        vec![35]
    );
    assert_eq!(
        g.nodes[&33].binding.direct,
        vec![ValueId {
            function: 1,
            sequence: 2
        }]
    );
    assert_eq!(
        g.nodes[&33].binding.evidence.state,
        EvaluationState::Satisfied
    );
    assert_eq!(report.lifecycle.state, EvaluationState::Indeterminate);
}
#[test]
fn reused_numbers_do_not_bind_to_future_births_or_pick_the_latest_generation() {
    let trace = format!(
        "{BEGIN}{}{}pv.counter.change function=1 role=3 cls=4 old=33 after=32 physical=32 site=400001 phase=0\n{}{}{}",
        birth(1, 32),
        graph(1, 1, 32..33, Some(32)),
        birth(2, 32),
        graph(2, 2, 32..33, Some(32)),
        end(2, 33)
    );
    let trace = trace.replace("graphs=1", "graphs=2");
    let report = provenance::parse(&trace).unwrap();
    let graphs = &report.graphs.graphs;
    assert_eq!(report.graphs.observations.state, EvaluationState::Satisfied);
    let missing = trace.replace(&graph(2, 2, 32..33, Some(32)), "");
    assert_eq!(
        provenance::parse(&missing)
            .unwrap()
            .graphs
            .observations
            .state,
        EvaluationState::Indeterminate
    );
    assert_eq!(graphs[0].nodes[&32].binding.direct.len(), 1);
    assert_eq!(
        graphs[0].nodes[&32].binding.evidence.state,
        EvaluationState::Satisfied
    );
    assert_eq!(graphs[1].nodes[&32].binding.direct.len(), 2);
    assert_eq!(
        graphs[1].nodes[&32].binding.evidence.state,
        EvaluationState::Indeterminate
    );
    assert_eq!(graphs[1].observations.state, EvaluationState::Satisfied);
    // A class reset starts a new number epoch; prior births must not leak into it.
    let reset_trace = trace.replace("role=3 cls=4", "role=5 cls=4");
    let reset = provenance::parse(&reset_trace).unwrap();
    assert_eq!(
        reset.graphs.graphs[1].nodes[&32].binding.direct,
        vec![ValueId {
            function: 1,
            sequence: 2
        }]
    );
}
#[test]
fn holes_partial_graphs_and_unsupported_captures_cannot_establish_bindings() {
    let trace = format!(
        "{BEGIN}{}pv.counter.change function=1 role=2 cls=4 old=33 after=34 physical=32 site=400001 phase=0\n{}{}",
        birth(1, 32),
        graph(1, 1, 32..34, Some(32)),
        end(1, 34)
    );
    let valid = provenance::parse(&trace).unwrap();
    assert_eq!(
        valid.graphs.graphs[0].nodes[&33].binding.evidence.state,
        EvaluationState::Indeterminate
    );
    for broken in [
        trace.replace("complete=1", "complete=0"),
        trace.replace("boundary=before_simplify", "boundary=unknown"),
        trace.replace("births=1 cls=4", "births=2 cls=4"),
        trace.replace("schema=1 function=1 graph=1", "schema=2 function=1 graph=1"),
        trace
            .lines()
            .filter(|l| !l.starts_with("pv.graph.end"))
            .collect::<Vec<_>>()
            .join("\n"),
        trace.replace("v=32 visible=1", "v=32 visible=0"),
        trace.replace("nodes=2", "nodes=3"),
        trace.replace("pv.graph.end", "pv.gap reason=unknown\npv.graph.end"),
    ] {
        let damaged = provenance::parse(&broken).unwrap();
        assert_eq!(
            damaged.graphs.observations.state,
            EvaluationState::Indeterminate
        );
        assert_eq!(
            damaged.graphs.graphs[0].nodes[&32].binding.evidence.state,
            EvaluationState::Indeterminate
        );
    }
    assert!(provenance::parse(&trace.replace("v=33", "v=32")).is_err());
    assert!(provenance::parse(&trace.replace("visible=1", "visible=2")).is_err());
    assert!(provenance::parse(&trace.replace("start=32", "start=40")).is_err());
    assert_eq!(
        provenance::parse(BEGIN).unwrap().graphs.observations.state,
        EvaluationState::Indeterminate
    );
}
#[test]
fn confirmed_merge_chains_add_contributors_without_erasing_child_births() {
    let trace = format!(
        "{BEGIN}{}{}{}merge1\nmerge2\n{}{}",
        birth(1, 32),
        birth(2, 33),
        birth(3, 34),
        graph(1, 3, 32..35, Some(32)),
        end(3, 35)
    );
    let position = |label: &str| trace.lines().position(|l| l == label).unwrap() + 1;
    let candidate = |id, at, parent, child| Candidate {
        id,
        trace_position: at,
        decision_position: Some(at),
        instruction: "123".into(),
        operands: BTreeMap::new(),
        checks: vec![],
        decision: Decision::Merged { parent, child },
        evidence: satisfied(),
    };
    let mut pass = Pass {
        id: 1,
        begin_position: 4,
        link: "sample".into(),
        class: 4,
        header: BTreeMap::from([("function_id".into(), "1".into())]),
        candidates: vec![
            candidate(1, position("merge1"), 33, 34),
            candidate(2, position("merge2"), 32, 33),
        ],
        ended: true,
        decisions: satisfied(),
    };
    let history = value_history::parse(&trace).unwrap();
    let result = graph_history::parse(&trace, &history, &[pass.clone()]).unwrap();
    let g = &result.graphs[0];
    assert_eq!(
        g.nodes[&32].binding.contributors,
        vec![
            ValueId {
                function: 1,
                sequence: 2
            },
            ValueId {
                function: 1,
                sequence: 3
            }
        ]
    );
    assert_eq!(
        g.nodes[&34].binding.direct,
        vec![ValueId {
            function: 1,
            sequence: 3
        }]
    );
    assert_eq!(g.merges[1].child_values.len(), 2);
    assert_eq!(
        g.nodes[&32].binding.evidence.state,
        EvaluationState::Satisfied
    );
    // A union whose write occurs after the graph is not evidence at that graph.
    pass.candidates[1].decision_position = Some(g.begin_position + 1);
    let future = graph_history::parse(&trace, &history, &[pass.clone()]).unwrap();
    assert!(future.graphs[0].nodes[&32].binding.contributors.is_empty());
    // Unknown birth of a merged virtual child is an explicit coverage gap.
    pass.candidates = vec![candidate(3, position("merge1"), 32, 40)];
    let missing = graph_history::parse(&trace, &history, &[pass]).unwrap();
    assert_eq!(
        missing.graphs[0].nodes[&32].binding.evidence.state,
        EvaluationState::Indeterminate
    );
    assert_eq!(
        missing.graphs[0].merges[0].binding.state,
        EvaluationState::Indeterminate
    );
}
#[test]
fn class_specific_physical_boundaries_are_used_for_cr_and_spr() {
    for (class, limit) in [(0, 4), (1, 8)] {
        let trace = format!(
            "{BEGIN}{}{}",
            graph(1, 0, limit..limit + 1, Some(limit)),
            end(0, 32)
        )
        .replace("cls=4", &format!("cls={class}"))
        .replace("physical=32", &format!("physical={limit}"))
        .replace("00040000", &format!("00{class:02x}0000"));
        let report = provenance::parse(&trace).unwrap();
        let g = &report.graphs.graphs[0];
        assert_eq!(g.observations.state, EvaluationState::Satisfied);
        assert!(!g.nodes[&limit].binding.physical);
        assert_eq!(
            g.nodes[&limit].binding.evidence.state,
            EvaluationState::Indeterminate
        );
        assert!(g.missing_neighbor_nodes.contains(&35));
    }
}
