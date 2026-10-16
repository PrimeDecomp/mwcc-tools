//! Graphs and instruction visibility observed at the same backend boundary.
//! Birth candidates retain every possible generation; observed unions add
//! contributors without asserting cross-variant semantic equivalence.
use crate::{
    evidence::{Evaluation, EvaluationState, EvidenceGap},
    provenance::{Decision, Pass},
    trace::{self, Instruction, Record},
    value_history::{ChangeKind, Function, History, Phase, ValueId},
};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet};

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Binding {
    /// Births of this raw number before the observation in its allocation epoch.
    pub direct: Vec<ValueId>,
    /// Births contributed through confirmed coalescer parent writes.
    pub contributors: Vec<ValueId>,
    pub physical: bool,
    /// Completeness of capture-local number binding, not source equivalence.
    pub evidence: Evaluation,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Node {
    pub register: u32,
    pub visible_in_instructions: bool,
    pub stored_degree: i32,
    pub flags: u32,
    pub color: i32,
    /// Preserve duplicates and order separately from the unique adjacency degree.
    pub recorded_neighbors: Vec<u32>,
    pub adjacency_degree: usize,
    pub binding: Binding,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Merge {
    pub pass: u32,
    pub candidate: u32,
    pub trace_position: usize,
    pub parent: u32,
    pub child: u32,
    pub parent_values: Vec<ValueId>,
    pub child_values: Vec<ValueId>,
    pub binding: Evaluation,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Graph {
    pub function: u32,
    pub link: String,
    pub id: u32,
    pub begin_position: usize,
    pub end_position: Option<usize>,
    pub birth_cutoff: u32,
    pub class: u8,
    pub physical_limit: u32,
    pub start: u32,
    pub count: u32,
    pub phase: Phase,
    pub boundary: String,
    pub instructions: Vec<Instruction>,
    pub nodes: BTreeMap<u32, Node>,
    pub graph_only: BTreeSet<u32>,
    pub instruction_only: BTreeSet<u32>,
    pub missing_neighbor_nodes: BTreeSet<u32>,
    pub merges: Vec<Merge>,
    pub observations: Evaluation,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct GraphHistory {
    pub format_version: u32,
    pub graphs: Vec<Graph>,
    pub functions: Vec<FunctionCoverage>,
    /// Snapshot and visibility coverage only; origins have separate evidence.
    pub observations: Evaluation,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct FunctionCoverage {
    pub function: u32,
    pub link: String,
    pub expected_graphs: Option<u32>,
    pub captured_graphs: usize,
    pub observations: Evaluation,
}
pub fn is_record(kind: &str) -> bool {
    kind.starts_with("pv.graph.")
}
fn gap(detail: &str) -> Evaluation {
    Evaluation::indeterminate(EvidenceGap::MissingGraphSnapshot, detail)
}
fn known() -> Evaluation {
    Evaluation {
        state: EvaluationState::Satisfied,
        reasons: vec![],
    }
}
fn number(r: &Record, field: &str) -> Result<u32> {
    r.number(field)
}
fn unknown_binding() -> Binding {
    Binding {
        direct: vec![],
        contributors: vec![],
        physical: false,
        evidence: Evaluation::indeterminate(
            EvidenceGap::BackendLifecycleCoverage,
            "no observed birth for this number in its allocation epoch",
        ),
    }
}
struct Pending {
    graph: Graph,
    valid: bool,
}

pub fn parse(text: &str, history: &History, passes: &[Pass]) -> Result<GraphHistory> {
    let mut result = GraphHistory {
        format_version: 1,
        graphs: vec![],
        functions: vec![],
        observations: gap("no graph-phase observations"),
    };
    let mut pending: Option<Pending> = None;
    let mut ordinal = BTreeMap::<u32, u32>::new();
    let mut global_gap = false;
    for (index, line) in text
        .lines()
        .enumerate()
        .filter(|(_, l)| l.trim_start().starts_with("pv."))
    {
        let mut r = trace::records(line)
            .pop()
            .context("empty graph observation")?;
        r.line = index + 1;
        if r.kind == "pv.gap" {
            global_gap = true;
            if let Some(p) = &mut pending {
                p.valid = false;
            }
            continue;
        }
        if !is_record(&r.kind) {
            if let Some(p) = &mut pending {
                p.valid = false;
            }
            continue;
        }
        if r.kind == "pv.graph.begin" {
            // A missing end never turns a prior partial graph into a success.
            if let Some(p) = pending.take() {
                result.graphs.push(p.graph);
            }
            let function = number(&r, "function")?;
            let f = history
                .functions
                .iter()
                .find(|f| f.id == function)
                .context("graph references unknown function context")?;
            let id = number(&r, "graph")?;
            let next = ordinal.entry(function).or_default();
            let mut valid = id == *next + 1 && number(&r, "schema")? == 1;
            *next = id;
            let class: u8 = number(&r, "cls")?.try_into()?;
            ensure!(class < 5, "unsupported graph register class");
            let start = number(&r, "start")?;
            let count = number(&r, "count")?;
            ensure!(
                start <= count && count <= 65536,
                "invalid graph register range"
            );
            let physical_limit = number(&r, "physical")?;
            ensure!(
                physical_limit <= 65536,
                "invalid physical-register boundary"
            );
            let birth_cutoff = number(&r, "births")?;
            valid &= f.begin_position < r.line
                && f.end_position.is_none_or(|end| end > r.line)
                && f.births
                    .iter()
                    .filter(|b| b.trace_position < r.line)
                    .count()
                    == birth_cutoff as usize
                && r.get("boundary")? == "before_simplify";
            pending = Some(Pending {
                valid,
                graph: Graph {
                    function,
                    link: f.link.clone(),
                    id,
                    begin_position: r.line,
                    end_position: None,
                    birth_cutoff,
                    class,
                    physical_limit,
                    start,
                    count,
                    phase: number(&r, "phase")?.into(),
                    boundary: r.get("boundary")?.into(),
                    instructions: vec![],
                    nodes: BTreeMap::new(),
                    graph_only: BTreeSet::new(),
                    instruction_only: BTreeSet::new(),
                    missing_neighbor_nodes: BTreeSet::new(),
                    merges: vec![],
                    observations: gap("graph has no end record"),
                },
            });
            continue;
        }
        let p = pending
            .as_mut()
            .context("graph detail without graph begin")?;
        match r.kind.as_str() {
            "pv.graph.instruction" => {
                let instruction = trace::instruction(&r)?;
                p.valid &= number(&r, "n")? as usize == instruction.operands.len();
                p.graph.instructions.push(instruction);
            }
            "pv.graph.node" => {
                let register = number(&r, "v")?;
                ensure!(
                    (p.graph.start..p.graph.count).contains(&register),
                    "graph node outside declared range"
                );
                ensure!(
                    !p.graph.nodes.contains_key(&register),
                    "duplicate graph node"
                );
                let visible = number(&r, "visible")?;
                ensure!(visible <= 1, "invalid graph visibility bit");
                let neighbors = r
                    .get("n")?
                    .split(',')
                    .filter(|s| !s.is_empty())
                    .map(str::parse::<u32>)
                    .collect::<std::result::Result<Vec<_>, _>>()?;
                ensure!(
                    neighbors.iter().all(|n| *n < 65536),
                    "invalid neighbor register"
                );
                let adjacency_degree = neighbors.iter().collect::<BTreeSet<_>>().len();
                p.graph.nodes.insert(
                    register,
                    Node {
                        register,
                        visible_in_instructions: visible != 0,
                        stored_degree: r.number("deg")?,
                        flags: u32::from_str_radix(r.get("flags")?, 16)?,
                        color: r.number("color")?,
                        recorded_neighbors: neighbors,
                        adjacency_degree,
                        binding: unknown_binding(),
                    },
                );
            }
            "pv.graph.end" => {
                ensure!(
                    number(&r, "function")? == p.graph.function
                        && number(&r, "graph")? == p.graph.id,
                    "graph end disagrees with active graph"
                );
                let complete = number(&r, "complete")?;
                ensure!(complete <= 1, "invalid graph completion flag");
                p.valid &= complete == 1
                    && number(&r, "nodes")? as usize == p.graph.nodes.len()
                    && number(&r, "instructions")? as usize == p.graph.instructions.len()
                    && p.graph.nodes.len() == (p.graph.count - p.graph.start) as usize;
                p.graph.end_position = Some(r.line);
                let visible: BTreeSet<_> = p
                    .graph
                    .instructions
                    .iter()
                    .flat_map(|i| &i.operands)
                    .filter(|o| o.is_register() && o.raw[1] == p.graph.class)
                    .map(|o| o.register())
                    .collect();
                p.valid &= p
                    .graph
                    .nodes
                    .values()
                    .all(|n| n.visible_in_instructions == visible.contains(&n.register));
                p.graph.graph_only = p
                    .graph
                    .nodes
                    .values()
                    .filter(|n| !n.visible_in_instructions)
                    .map(|n| n.register)
                    .collect();
                p.graph.instruction_only = visible
                    .into_iter()
                    .filter(|v| *v >= p.graph.physical_limit && !p.graph.nodes.contains_key(v))
                    .collect();
                p.graph.missing_neighbor_nodes = p
                    .graph
                    .nodes
                    .values()
                    .flat_map(|n| &n.recorded_neighbors)
                    .copied()
                    .filter(|n| *n >= p.graph.physical_limit && !p.graph.nodes.contains_key(n))
                    .collect();
                if p.valid {
                    p.graph.observations = known();
                }
                let p = pending.take().unwrap();
                result.graphs.push(p.graph);
            }
            _ => p.valid = false,
        }
    }
    if let Some(p) = pending {
        result.graphs.push(p.graph);
    }
    for graph in &mut result.graphs {
        let f = history
            .functions
            .iter()
            .find(|f| f.id == graph.function)
            .unwrap();
        bind(graph, f, passes);
    }
    for function in &history.functions {
        let graphs: Vec<_> = result
            .graphs
            .iter()
            .filter(|g| g.function == function.id)
            .collect();
        result.functions.push(FunctionCoverage {
            function: function.id,
            link: function.link.clone(),
            expected_graphs: function.expected_graphs,
            captured_graphs: graphs.len(),
            observations: if function.ended
                && function.expected_graphs.map(|v| v as usize) == Some(graphs.len())
                && graphs
                    .iter()
                    .all(|g| g.observations.state == EvaluationState::Satisfied)
            {
                known()
            } else {
                gap("function-end graph count is missing or disagrees with complete graph captures")
            },
        });
    }
    if !global_gap
        && !result.functions.is_empty()
        && result
            .functions
            .iter()
            .all(|g| g.observations.state == EvaluationState::Satisfied)
    {
        result.observations = known();
    } else {
        result.observations =
            gap("missing, unsupported, inconsistent or incomplete graph-phase capture");
    }
    Ok(result)
}

fn bind(graph: &mut Graph, f: &Function, passes: &[Pass]) {
    let epoch = f
        .changes
        .iter()
        .filter(|c| {
            c.class == graph.class
                && c.trace_position < graph.begin_position
                && matches!(
                    c.kind,
                    ChangeKind::InitialReset | ChangeKind::AfterAllocationReset
                )
        })
        .map(|c| c.trace_position)
        .max()
        .unwrap_or(f.begin_position);
    let mut direct = BTreeMap::<u32, Vec<ValueId>>::new();
    for birth in &f.births {
        if birth.class == graph.class
            && birth.trace_position > epoch
            && birth.trace_position < graph.begin_position
            && birth.id.sequence <= graph.birth_cutoff
        {
            direct.entry(birth.register).or_default().push(birth.id);
        }
    }
    let mut contributors = BTreeMap::<u32, BTreeSet<ValueId>>::new();
    let mut missing_contributors = BTreeSet::<u32>::new();
    let id_register: BTreeMap<_, _> = f.births.iter().map(|b| (b.id, b.register)).collect();
    for pass in passes.iter().filter(|p| {
        p.class == graph.class
            && p.begin_position > epoch
            && p.begin_position < graph.begin_position
            && p.header
                .get("function_id")
                .and_then(|id| id.parse::<u32>().ok())
                == Some(graph.function)
    }) {
        for c in &pass.candidates {
            let Some(decision_position) = c.decision_position else {
                continue;
            };
            if c.evidence.state != EvaluationState::Satisfied
                || decision_position >= graph.begin_position
            {
                continue;
            }
            if let Decision::Merged { parent, child } = c.decision {
                let values = |register: u32| -> BTreeSet<_> {
                    direct
                        .get(&register)
                        .into_iter()
                        .flatten()
                        .copied()
                        .filter(|id| {
                            f.births[(id.sequence - 1) as usize].trace_position < decision_position
                        })
                        .chain(contributors.get(&register).into_iter().flatten().copied())
                        .collect()
                };
                let parent_values = values(u32::from(parent));
                let child_values = values(u32::from(child));
                let ambiguous = parent_values.iter().chain(&child_values).any(|id| {
                    direct
                        .get(&id_register[id])
                        .is_some_and(|values| values.len() > 1)
                });
                let missing = (u32::from(parent) >= graph.physical_limit
                    && parent_values.is_empty())
                    || (u32::from(child) >= graph.physical_limit && child_values.is_empty())
                    || missing_contributors.contains(&u32::from(parent))
                    || missing_contributors.contains(&u32::from(child));
                if missing {
                    missing_contributors.insert(u32::from(parent));
                }
                contributors
                    .entry(u32::from(parent))
                    .or_default()
                    .extend(child_values.iter().copied());
                graph.merges.push(Merge {
                    pass: pass.id,
                    candidate: c.id,
                    trace_position: decision_position,
                    parent: u32::from(parent),
                    child: u32::from(child),
                    parent_values: parent_values.into_iter().collect(),
                    child_values: child_values.into_iter().collect(),
                    binding: if ambiguous {
                        Evaluation::indeterminate(
                            EvidenceGap::AmbiguousCorrespondence,
                            "merge roots have multiple possible birth generations",
                        )
                    } else if missing {
                        unknown_binding().evidence
                    } else if f.counter_observations.state != EvaluationState::Satisfied {
                        Evaluation::indeterminate(
                            EvidenceGap::BackendLifecycleCoverage,
                            "merge origins have incomplete direct-counter observations",
                        )
                    } else if graph.observations.state != EvaluationState::Satisfied {
                        gap("graph observation is incomplete")
                    } else {
                        known()
                    },
                });
            }
        }
    }
    for node in graph.nodes.values_mut() {
        node.binding.physical = node.register < graph.physical_limit;
        node.binding.direct = direct.get(&node.register).cloned().unwrap_or_default();
        node.binding.contributors = contributors
            .get(&node.register)
            .map(|ids| ids.iter().copied().collect())
            .unwrap_or_default();
        let ambiguous = node.binding.direct.len() > 1
            || node
                .binding
                .contributors
                .iter()
                .any(|id| direct.get(&id_register[id]).is_some_and(|v| v.len() > 1));
        node.binding.evidence = if ambiguous {
            Evaluation::indeterminate(
                EvidenceGap::AmbiguousCorrespondence,
                "raw register reuse leaves multiple possible birth generations",
            )
        } else if missing_contributors.contains(&node.register) {
            Evaluation::indeterminate(
                EvidenceGap::BackendLifecycleCoverage,
                "an observed merge has a contributor without a captured birth",
            )
        } else if f.counter_observations.state != EvaluationState::Satisfied {
            Evaluation::indeterminate(
                EvidenceGap::BackendLifecycleCoverage,
                "direct-counter observations for this function are incomplete",
            )
        } else if graph.observations.state != EvaluationState::Satisfied {
            gap("graph observation is incomplete")
        } else if node.binding.physical || !node.binding.direct.is_empty() {
            known()
        } else {
            unknown_binding().evidence
        };
    }
}
