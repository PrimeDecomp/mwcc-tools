//! Conditional correspondence at an observed graph boundary. Source names and
//! instruction alignment are hypotheses, never semantic lifetime identities.
use super::state;
use crate::{
    evidence::{Evaluation, EvaluationState, EvidenceGap},
    graph_history::{Graph, Merge, Node},
    provenance::Report,
    sequence,
    value_history::{AddressPart, Birth, Function, ObjectPart, Phase, ValueId},
    version::Version,
};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet};

#[derive(Clone, Copy, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Boundary {
    BeforeSimplify,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Selection {
    pub link: String,
    pub class: u8,
    pub round: usize,
    pub boundary: Boundary,
}
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq, PartialOrd, Ord)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum Anchor {
    NamedObject {
        name: Vec<u8>,
        part: String,
        type_kind: i32,
        type_size: i32,
    },
    NormalizeSymbol {
        symbol: Vec<u8>,
        part: String,
        displacement: i32,
        operand_kind: u8,
        datatype: u8,
        storage: u16,
    },
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct AnchorObservation {
    pub anchor: Anchor,
    pub first_values: Vec<ValueId>,
    pub second_values: Vec<ValueId>,
    pub first_nodes: Vec<u32>,
    pub second_nodes: Vec<u32>,
    pub usable: bool,
    pub limitations: Vec<String>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum Basis {
    PhysicalRegister,
    Anchor { anchor: Anchor },
    InstructionVote { votes: usize },
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Hypothesis {
    pub first: u32,
    pub second: u32,
    pub basis: Vec<Basis>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Conflict {
    pub kind: String,
    pub first: Vec<u32>,
    pub second: Vec<u32>,
    pub detail: String,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct RootProjection {
    pub value: ValueId,
    pub original: u32,
    pub root: u32,
    pub chain: Vec<Merge>,
    pub evidence: Evaluation,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct NodeState {
    pub node: Node,
    pub anchors: Vec<Anchor>,
    pub unknown_origins: Vec<ValueId>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Side {
    pub selection: Selection,
    pub graph_id: u32,
    pub function: u32,
    pub physical_limit: u32,
    pub start: u32,
    pub count: u32,
    pub nodes: BTreeMap<u32, NodeState>,
    pub observed_merges: Vec<Merge>,
    pub observed_roots: Vec<RootProjection>,
    pub coverage: Evaluation,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct NodeChange {
    pub first: u32,
    pub second: u32,
    pub stored_degree: [i32; 2],
    pub adjacency_degree: [usize; 2],
    pub raw_neighbor_count: [usize; 2],
    pub visible: [bool; 2],
    pub flags: [u32; 2],
    pub colors: [i32; 2],
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct NeighborDifference {
    pub first: u32,
    pub second: u32,
    pub neighbor_in_second_space: u32,
    pub first_occurrences: usize,
    pub second_occurrences: usize,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct BoundaryEdge {
    pub node: u32,
    pub neighbor: u32,
    pub occurrences: usize,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Comparison {
    pub format_version: u32,
    pub version: Version,
    pub first: Side,
    pub second: Side,
    pub coverage: Evaluation,
    pub anchors: Vec<AnchorObservation>,
    pub hypotheses: Vec<Hypothesis>,
    pub mapping: Vec<Hypothesis>,
    pub conflicts: Vec<Conflict>,
    pub unmatched_first: Vec<u32>,
    pub unmatched_second: Vec<u32>,
    pub node_changes: Vec<NodeChange>,
    pub neighbor_differences: Vec<NeighborDifference>,
    pub boundary_first: Vec<BoundaryEdge>,
    pub boundary_second: Vec<BoundaryEdge>,
    pub paired_instructions: usize,
    pub instruction_counts: [usize; 2],
    pub lifetime_correspondence: Evaluation,
    pub elimination: Evaluation,
}
fn known() -> Evaluation {
    Evaluation {
        state: EvaluationState::Satisfied,
        reasons: vec![],
    }
}
fn gap(detail: &str) -> Evaluation {
    Evaluation::indeterminate(EvidenceGap::AmbiguousCorrespondence, detail)
}
fn combine(values: impl IntoIterator<Item = Evaluation>) -> Evaluation {
    let mut r = known();
    for e in values {
        if e.state != EvaluationState::Satisfied {
            r.state = EvaluationState::Indeterminate;
        }
        r.reasons.extend(e.reasons);
    }
    r
}
fn selected<'a>(
    report: &'a Report,
    s: &Selection,
) -> Result<(&'a Function, &'a Graph, Evaluation)> {
    ensure!(s.class < 5 && !s.link.is_empty(), "invalid graph selection");
    let g = state::graph(report, &s.link, s.class, s.round)?;
    ensure!(
        s.boundary == Boundary::BeforeSimplify
            && g.boundary == "before_simplify"
            && g.phase == Phase::Allocation,
        "requested observed boundary is unavailable"
    );
    let f = report
        .history
        .functions
        .iter()
        .find(|f| f.id == g.function)
        .context("graph function history missing")?;
    let counts = report
        .graphs
        .functions
        .iter()
        .find(|f| f.function == g.function)
        .context("function graph-count evidence missing")?;
    let coverage = combine([
        g.observations.clone(),
        counts.observations.clone(),
        f.counter_observations.clone(),
    ]);
    Ok((f, g, coverage))
}
fn anchors(b: &Birth) -> Vec<Anchor> {
    let mut out = vec![];
    if let Some(o) = &b.object
        && o.name_known
        && o.name_complete
        && !o.name_bytes.is_empty()
    {
        out.push(Anchor::NamedObject {
            name: o.name_bytes.clone(),
            part: match o.part {
                ObjectPart::Single => "single",
                ObjectPart::FirstWord => "first_word",
                ObjectPart::SecondWord => "second_word",
            }
            .into(),
            type_kind: o.type_kind,
            type_size: o.type_size,
        });
    }
    if let Some(m) = &b.materialization
        && m.symbol_known
        && m.symbol_complete
        && !m.symbol_bytes.is_empty()
    {
        out.push(Anchor::NormalizeSymbol {
            symbol: m.symbol_bytes.clone(),
            part: match m.part {
                AddressPart::High => "high",
                AddressPart::Low => "low",
            }
            .into(),
            displacement: m.displacement,
            operand_kind: m.kind,
            datatype: m.datatype,
            storage: m.storage,
        });
    }
    out
}
#[derive(Default)]
struct Origins {
    values: BTreeMap<Anchor, Vec<ValueId>>,
    nodes: BTreeMap<Anchor, BTreeSet<u32>>,
    invalid: BTreeMap<Anchor, Vec<String>>,
}
fn origins(f: &Function, g: &Graph) -> Origins {
    let mut out = Origins::default();
    // Count the whole function, including later generations/rounds, so selecting
    // a graph cannot turn a repeated source name into a unique source identity.
    for birth in f.births.iter().filter(|b| b.class == g.class) {
        for anchor in anchors(birth) {
            if matches!(anchor, Anchor::NormalizeSymbol { .. })
                && f.materialization_observations.state != EvaluationState::Satisfied
            {
                out.invalid.entry(anchor.clone()).or_default().push(
                    "function guarded Normalize-materialization capture is incomplete".into(),
                );
            }
            out.values.entry(anchor.clone()).or_default().push(birth.id);
            for node in g.nodes.values().filter(|n| {
                n.binding
                    .direct
                    .iter()
                    .chain(&n.binding.contributors)
                    .any(|id| *id == birth.id)
            }) {
                out.nodes
                    .entry(anchor.clone())
                    .or_default()
                    .insert(node.register);
                if node.binding.evidence.state != EvaluationState::Satisfied {
                    out.invalid.entry(anchor.clone()).or_default().push(format!(
                        "node {} has incomplete/ambiguous birth binding",
                        node.register
                    ));
                }
            }
        }
    }
    out
}
fn side(f: &Function, g: &Graph, s: &Selection, coverage: Evaluation) -> Side {
    let births: BTreeMap<_, _> = f.births.iter().map(|b| (b.id, b)).collect();
    let nodes = g
        .nodes
        .iter()
        .map(|(v, n)| {
            let mut keys = BTreeSet::new();
            let mut unknown = BTreeSet::new();
            for id in n.binding.direct.iter().chain(&n.binding.contributors) {
                let found = births.get(id).map(|b| anchors(b)).unwrap_or_default();
                if found.is_empty() {
                    unknown.insert(*id);
                }
                keys.extend(found);
            }
            (
                *v,
                NodeState {
                    node: n.clone(),
                    anchors: keys.into_iter().collect(),
                    unknown_origins: unknown.into_iter().collect(),
                },
            )
        })
        .collect();
    let present: BTreeSet<_> = g
        .nodes
        .values()
        .flat_map(|n| n.binding.direct.iter().chain(&n.binding.contributors))
        .copied()
        .collect();
    let observed_roots = f
        .births
        .iter()
        .filter(|b| b.class == g.class && present.contains(&b.id))
        .map(|b| {
            let mut current = b.register;
            let mut chain = vec![];
            let mut evidence = known();
            for m in &g.merges {
                if m.child == current
                    && m.trace_position > b.trace_position
                    && m.child_values.contains(&b.id)
                {
                    evidence = combine([evidence, m.binding.clone()]);
                    chain.push(m.clone());
                    current = m.parent;
                }
            }
            for n in g.nodes.values().filter(|n| {
                n.binding
                    .direct
                    .iter()
                    .chain(&n.binding.contributors)
                    .any(|id| *id == b.id)
            }) {
                evidence = combine([evidence, n.binding.evidence.clone()]);
            }
            RootProjection {
                value: b.id,
                original: b.register,
                root: current,
                chain,
                evidence,
            }
        })
        .collect();
    Side {
        selection: s.clone(),
        graph_id: g.id,
        function: g.function,
        physical_limit: g.physical_limit,
        start: g.start,
        count: g.count,
        nodes,
        observed_merges: g.merges.clone(),
        observed_roots,
        coverage,
    }
}
fn instruction_shape(i: &crate::trace::Instruction, g: &Graph, version: Version) -> String {
    let names = crate::version::opcode_names(version);
    let operands: Vec<_> = i
        .operands
        .iter()
        .map(|o| {
            if o.is_register() {
                let identity = if o.raw[1] == g.class && o.register() < g.physical_limit {
                    o.register().to_string()
                } else {
                    "*".into()
                };
                format!("reg:{}:{}:{identity}", o.raw[1], o.effect())
            } else {
                o.text()
            }
        })
        .collect();
    format!(
        "{}:{}:{}:{}",
        i.opcode,
        names.get(&i.opcode).map_or("?", String::as_str),
        i.flags,
        operands.join(",")
    )
}
fn votes(a: &Graph, b: &Graph, version: Version) -> (BTreeMap<(u32, u32), usize>, usize) {
    let aa: Vec<_> = a
        .instructions
        .iter()
        .map(|i| instruction_shape(i, a, version))
        .collect();
    let bb: Vec<_> = b
        .instructions
        .iter()
        .map(|i| instruction_shape(i, b, version))
        .collect();
    let mut votes = BTreeMap::new();
    let mut paired = 0;
    for (x, y, n) in sequence::matching_blocks_with_autojunk(&aa, &bb, false) {
        for k in 0..n {
            paired += 1;
            for (p, q) in a.instructions[x + k]
                .operands
                .iter()
                .zip(&b.instructions[y + k].operands)
            {
                if p.is_register()
                    && q.is_register()
                    && p.raw[1] == a.class
                    && q.raw[1] == b.class
                    && p.register() >= a.physical_limit
                    && q.register() >= b.physical_limit
                {
                    *votes.entry((p.register(), q.register())).or_default() += 1;
                }
            }
        }
    }
    (votes, paired)
}
fn counts(values: impl IntoIterator<Item = u32>) -> BTreeMap<u32, usize> {
    let mut out = BTreeMap::new();
    for v in values {
        *out.entry(v).or_default() += 1;
    }
    out
}
fn boundaries(g: &Graph, map: &BTreeMap<u32, u32>) -> Vec<BoundaryEdge> {
    let mut out = vec![];
    for (v, n) in &g.nodes {
        for (neighbor, occurrences) in counts(n.recorded_neighbors.iter().copied()) {
            if !map.contains_key(v)
                || (neighbor >= g.physical_limit && !map.contains_key(&neighbor))
            {
                out.push(BoundaryEdge {
                    node: *v,
                    neighbor,
                    occurrences,
                });
            }
        }
    }
    out
}
pub fn compare(
    first: &Report,
    first_selection: &Selection,
    second: &Report,
    second_selection: &Selection,
    version: Version,
) -> Result<Comparison> {
    ensure!(version.has_oracle(), "unsupported oracle compiler version");
    ensure!(
        first_selection.class == second_selection.class,
        "graph classes differ"
    );
    let (af, a, ac) = selected(first, first_selection)?;
    let (bf, b, bc) = selected(second, second_selection)?;
    let physical = if a.physical_limit == b.physical_limit {
        known()
    } else {
        gap("observed physical-register boundaries differ")
    };
    let coverage = combine([ac.clone(), bc.clone(), physical]);
    let ao = origins(af, a);
    let bo = origins(bf, b);
    let keys: BTreeSet<_> = ao.values.keys().chain(bo.values.keys()).cloned().collect();
    let mut observations = vec![];
    let mut hypotheses: BTreeMap<(u32, u32), Vec<Basis>> = BTreeMap::new();
    if a.physical_limit == b.physical_limit {
        for v in a
            .nodes
            .keys()
            .filter(|v| **v < a.physical_limit && b.nodes.contains_key(v))
        {
            hypotheses
                .entry((*v, *v))
                .or_default()
                .push(Basis::PhysicalRegister);
        }
    }
    for anchor in keys {
        let av = ao.values.get(&anchor).cloned().unwrap_or_default();
        let bv = bo.values.get(&anchor).cloned().unwrap_or_default();
        let an: Vec<_> = ao
            .nodes
            .get(&anchor)
            .into_iter()
            .flatten()
            .copied()
            .collect();
        let bn: Vec<_> = bo
            .nodes
            .get(&anchor)
            .into_iter()
            .flatten()
            .copied()
            .collect();
        let mut limitations: Vec<_> = ao
            .invalid
            .get(&anchor)
            .into_iter()
            .flatten()
            .chain(bo.invalid.get(&anchor).into_iter().flatten())
            .cloned()
            .collect();
        if av.len() != 1 || bv.len() != 1 {
            limitations.push(
                "anchor does not identify one complete observed birth in each whole function"
                    .into(),
            );
        }
        if an.len() != 1 || bn.len() != 1 {
            limitations.push("anchor does not identify one graph node in each capture".into());
        }
        let usable = limitations.is_empty();
        if usable {
            hypotheses
                .entry((an[0], bn[0]))
                .or_default()
                .push(Basis::Anchor {
                    anchor: anchor.clone(),
                });
        }
        observations.push(AnchorObservation {
            anchor,
            first_values: av,
            second_values: bv,
            first_nodes: an,
            second_nodes: bn,
            usable,
            limitations,
        });
    }
    let (instruction_votes, paired_instructions) = votes(a, b, version);
    for ((x, y), votes) in instruction_votes {
        hypotheses
            .entry((x, y))
            .or_default()
            .push(Basis::InstructionVote { votes });
    }
    let mut targets: BTreeMap<u32, BTreeSet<u32>> = BTreeMap::new();
    let mut sources: BTreeMap<u32, BTreeSet<u32>> = BTreeMap::new();
    let mut conflicts = vec![];
    for &(x, y) in hypotheses.keys() {
        targets.entry(x).or_default().insert(y);
        sources.entry(y).or_default().insert(x);
    }
    for (x, ys) in &targets {
        if ys.len() > 1 {
            conflicts.push(Conflict {
                kind: "competing_targets".into(),
                first: vec![*x],
                second: ys.iter().copied().collect(),
                detail: "anchor/instruction hypotheses disagree; no hypothesis wins by vote count"
                    .into(),
            });
        }
    }
    for (y, xs) in &sources {
        if xs.len() > 1 {
            conflicts.push(Conflict {
                kind: "many_to_one_hypotheses".into(),
                first: xs.iter().copied().collect(),
                second: vec![*y],
                detail: "colliding cross-capture hypotheses are not an observed merge".into(),
            });
        }
    }
    let mut mapping = vec![];
    for (&(x, y), basis) in &hypotheses {
        let complete = a.nodes.get(&x).zip(b.nodes.get(&y)).is_some_and(|(x, y)| {
            x.binding.evidence.state == EvaluationState::Satisfied
                && y.binding.evidence.state == EvaluationState::Satisfied
        });
        if !complete {
            conflicts.push(Conflict {
                kind: "incomplete_node_binding".into(),
                first: vec![x],
                second: vec![y],
                detail:
                    "a graph node is absent or has incomplete/ambiguous birth-generation binding"
                        .into(),
            });
        }
        if coverage.state == EvaluationState::Satisfied
            && complete
            && targets[&x].len() == 1
            && sources[&y].len() == 1
        {
            mapping.push(Hypothesis {
                first: x,
                second: y,
                basis: basis.clone(),
            });
        }
    }
    let map: BTreeMap<_, _> = mapping.iter().map(|p| (p.first, p.second)).collect();
    let back: BTreeMap<_, _> = mapping.iter().map(|p| (p.second, p.first)).collect();
    let mut node_changes = vec![];
    let mut neighbor_differences = vec![];
    for (x, y) in &map {
        let an = &a.nodes[x];
        let bn = &b.nodes[y];
        node_changes.push(NodeChange {
            first: *x,
            second: *y,
            stored_degree: [an.stored_degree, bn.stored_degree],
            adjacency_degree: [an.adjacency_degree, bn.adjacency_degree],
            raw_neighbor_count: [an.recorded_neighbors.len(), bn.recorded_neighbors.len()],
            visible: [an.visible_in_instructions, bn.visible_in_instructions],
            flags: [an.flags, bn.flags],
            colors: [an.color, bn.color],
        });
        let left = counts(an.recorded_neighbors.iter().filter_map(|n| {
            if *n < a.physical_limit {
                Some(*n)
            } else {
                map.get(n).copied()
            }
        }));
        let right = counts(
            bn.recorded_neighbors
                .iter()
                .copied()
                .filter(|n| *n < b.physical_limit || back.contains_key(n)),
        );
        for n in left
            .keys()
            .chain(right.keys())
            .copied()
            .collect::<BTreeSet<_>>()
        {
            let l = left.get(&n).copied().unwrap_or(0);
            let r = right.get(&n).copied().unwrap_or(0);
            if l != r {
                neighbor_differences.push(NeighborDifference {
                    first: *x,
                    second: *y,
                    neighbor_in_second_space: n,
                    first_occurrences: l,
                    second_occurrences: r,
                });
            }
        }
    }
    Ok(Comparison {
        format_version: 1,
        version,
        first: side(af, a, first_selection, ac),
        second: side(bf, b, second_selection, bc),
        coverage,
        anchors: observations,
        hypotheses: hypotheses
            .into_iter()
            .map(|((first, second), basis)| Hypothesis {
                first,
                second,
                basis,
            })
            .collect(),
        unmatched_first: a
            .nodes
            .keys()
            .filter(|v| !map.contains_key(v))
            .copied()
            .collect(),
        unmatched_second: b
            .nodes
            .keys()
            .filter(|v| !back.contains_key(v))
            .copied()
            .collect(),
        mapping,
        conflicts,
        node_changes,
        neighbor_differences,
        boundary_first: boundaries(a, &map),
        boundary_second: boundaries(b, &back),
        paired_instructions,
        instruction_counts: [a.instructions.len(), b.instructions.len()],
        lifetime_correspondence: gap(
            "source-name/symbol anchors and instruction votes are conditional correspondence, not semantic lifetime proof",
        ),
        elimination: Evaluation::indeterminate(
            EvidenceGap::BackendLifecycleCoverage,
            "unmatched, invisible and merged nodes do not prove semantic elimination",
        ),
    })
}
