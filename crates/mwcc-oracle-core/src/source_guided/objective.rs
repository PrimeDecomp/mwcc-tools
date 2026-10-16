//! One evidence-aware evaluator for search, reduction and corpus predicates.
use super::state;
use crate::{
    allocation_category::Category,
    evidence::{Evaluation, EvaluationState, EvidenceGap, Reason},
    graph_history::Graph,
    provenance::Report,
    runner::{CheckStatus, Compilation},
    value_history::{AddressPart, Birth, ObjectPart},
    version::Version,
};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::collections::{BTreeMap, BTreeSet};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Selection {
    pub link: String,
    pub class: u8,
    pub round: usize,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ObjectAnchor {
    pub name: String,
    pub part: ObjectPart,
    pub type_kind: i32,
    pub type_size: i32,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
pub enum Objective {
    ChangeRangeCategory {
        value: ObjectAnchor,
        target: Category,
    },
    DirectInitialization {
        value: ObjectAnchor,
        constant: i32,
        source_line: Option<i32>,
    },
    EliminateTemporary {
        value: ObjectAnchor,
    },
    /// Count the guarded Normalize high/low creation mechanism. This does not
    /// assert absence of all address temporaries or semantic value elimination.
    SymbolAddressPairs {
        symbol: String,
        minimum: u32,
        maximum: u32,
    },
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RequiredRelocation {
    pub symbol: String,
    pub kind: Option<u8>,
    pub addend: Option<i32>,
    pub minimum: usize,
}
#[derive(Clone, Copy, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Purpose {
    MechanismReproducer,
    SourceTransformation,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Contract {
    pub format_version: u32,
    pub selection: Selection,
    pub objective: Objective,
    pub relocations: Vec<RequiredRelocation>,
    pub purpose: Purpose,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct EvaluationReport {
    pub format_version: u32,
    pub purpose: Purpose,
    pub semantic_validation: String,
    pub score: Score,
    pub constraints: BTreeMap<String, Evaluation>,
    pub objective: Evaluation,
    pub overall: Evaluation,
    pub observations: Value,
    pub graph_changes: Value,
    pub final_target_mismatch: Option<Value>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Score {
    pub eligible: bool,
    pub objective_cost: Option<u32>,
    pub inferred_edges_added: usize,
    pub inferred_edges_removed: usize,
}
pub struct Input<'a> {
    pub compilation: &'a Compilation,
    pub provenance: &'a Report,
    pub source: &'a [u8],
    pub semantic_check: Option<Evaluation>,
}
fn known() -> Evaluation {
    Evaluation {
        state: EvaluationState::Satisfied,
        reasons: vec![],
    }
}
fn false_result(detail: &str) -> Evaluation {
    Evaluation {
        state: EvaluationState::Unsatisfied,
        reasons: vec![Reason {
            code: EvidenceGap::ObjectivePredicate,
            detail: detail.into(),
        }],
    }
}
fn gap(code: EvidenceGap, detail: &str) -> Evaluation {
    Evaluation::indeterminate(code, detail)
}
fn combine(values: impl IntoIterator<Item = Evaluation>) -> Evaluation {
    let mut result = known();
    for value in values {
        if value.state == EvaluationState::Unsatisfied
            || (value.state == EvaluationState::Indeterminate
                && result.state == EvaluationState::Satisfied)
        {
            result.state = value.state;
        }
        result.reasons.extend(value.reasons);
    }
    result
}
pub fn validate(c: &Contract) -> Result<()> {
    ensure!(c.format_version == 1, "unsupported source-objective format");
    ensure!(
        c.selection.class < 5 && !c.selection.link.is_empty(),
        "invalid source-objective selection"
    );
    ensure!(
        c.relocations
            .iter()
            .all(|r| r.minimum > 0 && !r.symbol.is_empty()),
        "invalid relocation requirement"
    );
    if let Objective::SymbolAddressPairs {
        symbol,
        minimum,
        maximum,
    } = &c.objective
    {
        ensure!(
            !symbol.is_empty() && minimum <= maximum && c.selection.class == 4,
            "symbol address pairs require a symbol, ordered count bounds and GPR selection"
        );
    }
    Ok(())
}
fn symbol_address_pairs(input: &Input<'_>, graph: &Graph, symbol: &str) -> Result<Value> {
    let function = input
        .provenance
        .history
        .functions
        .iter()
        .find(|f| f.id == graph.function)
        .context("missing materialization function")?;
    ensure!(
        function.materialization_observations.state == EvaluationState::Satisfied
            && function.counter_observations.state == EvaluationState::Satisfied,
        "complete guarded Normalize-pair and counter capture is required"
    );
    let mut pairs = vec![];
    let mut high_count = 0;
    let mut low_count = 0;
    for low in function
        .births
        .iter()
        .filter(|b| b.id.sequence <= graph.birth_cutoff)
    {
        let Some(binding) = &low.materialization else {
            continue;
        };
        // Unknown symbols could be additional occurrences of the requested
        // symbol; they cannot be silently excluded from a negative/count bound.
        ensure!(
            binding.symbol_known && binding.symbol_complete,
            "a materialized symbol identity is incomplete"
        );
        if binding.part != AddressPart::Low {
            high_count += 1;
            continue;
        }
        low_count += 1;
        let high = function
            .births
            .get(
                (low.id.sequence as usize)
                    .checked_sub(2)
                    .context("address low half is the first birth")?,
            )
            .context("address low half has no preceding birth")?;
        let high_binding = high
            .materialization
            .as_ref()
            .context("address low half has no observed high half")?;
        ensure!(
            high_binding.part == AddressPart::High
                && high.class == 4
                && low.class == 4
                && high_binding.operand == binding.operand
                && high_binding.object == binding.object
                && high_binding.kind == binding.kind
                && high_binding.displacement == binding.displacement
                && high_binding.symbol_known
                && high_binding.symbol_complete
                && high_binding.symbol_bytes == binding.symbol_bytes,
            "address pair observations disagree"
        );
        if binding.symbol_bytes != symbol.as_bytes() {
            continue;
        }
        let node = graph.nodes.get(&low.register);
        // A reused raw number cannot identify this pair's graph state. Count
        // the creation but leave the objective indeterminate until it is bound.
        ensure!(
            node.is_some_and(
                |node| node.binding.evidence.state == EvaluationState::Satisfied
                    && node.binding.direct == [low.id]
            ),
            "address result lacks a unique graph birth binding"
        );
        let allocation_root = root(graph, low)?;
        pairs.push(json!({"high":high.id,"low":low.id,"source":binding,
            "result_register":low.register,"allocation_root":allocation_root,
            "node":node,"merged_to_physical":allocation_root < graph.physical_limit}));
    }
    ensure!(
        high_count == low_count,
        "incomplete address materialization pair at the selected boundary"
    );
    Ok(
        json!({"count":pairs.len(),"pairs":pairs,"scope":"guarded_normalize_pair_v1_before_selected_graph"}),
    )
}
fn anchor<'a>(input: &'a Input<'_>, g: &Graph, a: &ObjectAnchor) -> Result<&'a Birth> {
    let f = input
        .provenance
        .history
        .functions
        .iter()
        .find(|f| f.id == g.function)
        .context("missing function history")?;
    let matches: Vec<_> = f
        .births
        .iter()
        .filter(|b| {
            b.class == g.class
                && b.trace_position < g.begin_position
                && b.object.as_ref().is_some_and(|o| {
                    o.name_known
                        && o.name_complete
                        && o.name_bytes == a.name.as_bytes()
                        && o.part == a.part
                        && o.type_kind == a.type_kind
                        && o.type_size == a.type_size
                })
        })
        .collect();
    ensure!(
        matches.len() == 1,
        "object anchor must identify exactly one complete observed birth"
    );
    Ok(matches[0])
}
fn root(g: &Graph, b: &Birth) -> Result<u32> {
    let mut register = b.register;
    for merge in &g.merges {
        if merge.child == register {
            ensure!(
                merge.binding.state == EvaluationState::Satisfied,
                "merge origin binding is incomplete"
            );
            register = merge.parent;
        }
    }
    if register < g.physical_limit {
        let node = g
            .nodes
            .get(&b.register)
            .context("anchored value has no original graph node")?;
        ensure!(
            node.binding.evidence.state == EvaluationState::Satisfied
                && node.binding.direct.contains(&b.id),
            "physical projection has ambiguous birth binding"
        );
        return Ok(register);
    }
    let node = g
        .nodes
        .get(&register)
        .context("anchored root has no graph node")?;
    ensure!(
        node.binding.evidence.state == EvaluationState::Satisfied
            && node
                .binding
                .direct
                .iter()
                .chain(&node.binding.contributors)
                .any(|id| *id == b.id),
        "anchored root has incomplete or ambiguous lifetime binding"
    );
    Ok(register)
}
fn category(g: &Graph, register: u32) -> Result<crate::allocation_category::Observation> {
    if register < g.physical_limit {
        Ok(crate::allocation_category::Observation {
            category: Some(Category::Physical),
            evidence: known(),
        })
    } else {
        Ok(g.nodes
            .get(&register)
            .context("anchored category has no graph node")?
            .range_category
            .clone())
    }
}
fn relocation_check(input: &Input<'_>, c: &Contract) -> Evaluation {
    match state::relocations(&input.compilation.object, &c.selection.link) {
        Err(e) => gap(EvidenceGap::MissingRelocationEvidence, &format!("{e:#}")),
        Ok(relocations) => {
            if c.relocations.iter().all(|r| {
                relocations
                    .iter()
                    .filter(|v| {
                        v.symbol == r.symbol
                            && r.kind.is_none_or(|k| v.kind == k)
                            && r.addend.is_none_or(|a| v.addend == a)
                    })
                    .count()
                    >= r.minimum
            }) {
                known()
            } else {
                false_result("required symbolic relocations were not retained")
            }
        }
    }
}
fn delta(a: &Graph, b: &Graph, version: Version) -> Value {
    let remap = crate::colouring::target::remap_snapshots(
        &state::snapshot(a),
        &state::snapshot(b),
        version,
    );
    let mut mapping = BTreeMap::new();
    let mut votes = vec![];
    let mut inverse = BTreeMap::<u32, usize>::new();
    for ((class, from), candidates) in &remap.votes {
        if *class == crate::version::class_name(a.class) && candidates.len() == 1 {
            *inverse.entry(candidates[0].0).or_default() += 1;
            mapping.insert(*from, candidates[0].0);
        }
    }
    mapping.retain(|_, to| inverse[to] == 1);
    for ((class, from), candidates) in remap.votes {
        if class == crate::version::class_name(a.class) {
            votes.push(
                json!({"first":from,"candidates":candidates,"inferred_second":mapping.get(&from)}),
            );
        }
    }
    let edges = |g: &Graph| {
        g.nodes
            .iter()
            .flat_map(|(v, n)| n.recorded_neighbors.iter().map(|w| (*v, *w)))
            .collect::<BTreeSet<_>>()
    };
    let translated = |r: u32| {
        if r < a.physical_limit {
            Some(r)
        } else {
            mapping.get(&r).copied()
        }
    };
    let mut mapped = BTreeSet::new();
    let mut boundary = vec![];
    for (x, y) in edges(a) {
        if let (Some(x), Some(y)) = (translated(x), translated(y)) {
            mapped.insert((x, y));
        } else {
            boundary.push((x, y));
        }
    }
    let inverse: BTreeSet<_> = mapping.values().copied().collect();
    let mut second = BTreeSet::new();
    let mut boundary_second = vec![];
    for (x, y) in edges(b) {
        if (x < b.physical_limit || inverse.contains(&x))
            && (y < b.physical_limit || inverse.contains(&y))
        {
            second.insert((x, y));
        } else {
            boundary_second.push((x, y));
        }
    }
    json!({"method":"instruction_votes","paired_instructions":remap.paired,"instruction_counts":remap.instructions,"correspondence":votes,
        "inferred_edges_added":second.difference(&mapped).collect::<Vec<_>>(),"inferred_edges_removed":mapped.difference(&second).collect::<Vec<_>>(),
        "boundary_first":boundary,"boundary_second":boundary_second,"elimination":gap(EvidenceGap::BackendLifecycleCoverage,"instruction votes do not prove elimination")})
}
fn direct_initialization(
    input: &Input<'_>,
    g: &Graph,
    b: &Birth,
    constant: i32,
    line: Option<i32>,
) -> Result<(Evaluation, Value)> {
    ensure!(
        (2..=4).contains(&g.class) && g.physical_limit == 32,
        "direct initialization currently requires a VR/FPR/GPR allocation graph"
    );
    let r = root(g, b)?;
    let names = crate::version::opcode_names(input.compilation.version);
    let trace = crate::trace::parse(&input.compilation.trace)?;
    let allocations: Vec<_> = trace
        .allocations
        .iter()
        .filter(|a| a.before.link == g.link)
        .collect();
    ensure!(
        allocations.len() == 1,
        "allocation link does not identify one function"
    );
    let defines = |i: &crate::trace::Instruction, register| {
        i.operands
            .iter()
            .any(|o| o.defines() && o.raw[1] == g.class && o.register() == register)
    };
    let immediate = |i: &crate::trace::Instruction| {
        names.get(&i.opcode).is_some_and(|n| n == "li")
            && i.operands.iter().any(|o| {
                o.kind() == 2 && i32::from_le_bytes(o.raw[2..6].try_into().unwrap()) == constant
            })
    };
    let mut initializers = vec![];
    for block in &allocations[0].before.blocks {
        for (index, instruction) in block
            .instructions
            .iter()
            .enumerate()
            .filter(|(_, i)| defines(i, b.register) && line.is_none_or(|l| i.line == Some(l)))
        {
            if immediate(instruction) {
                initializers.push((instruction, None));
                continue;
            }
            if names.get(&instruction.opcode).is_some_and(|n| n == "mr")
                && instruction.flags & 0x10 != 0
            {
                let sources = instruction
                    .operands
                    .iter()
                    .filter(|o| o.uses() && o.raw[1] == g.class)
                    .collect::<Vec<_>>();
                if sources.len() != 1 {
                    continue;
                }
                let previous = block.instructions[..index]
                    .iter()
                    .rev()
                    .take_while(|i| i.flags & (1 | 8 | 0x100) == 0)
                    .find(|i| defines(i, sources[0].register()));
                if let Some(previous) = previous
                    && immediate(previous)
                {
                    initializers.push((previous, Some(instruction)));
                }
            }
        }
    }
    ensure!(
        initializers.len() == 1,
        "expected one immediate initializer bound to the original named value, directly or through one local copy"
    );
    let (initializer, copy) = initializers[0];
    let current = g
        .instructions
        .iter()
        .filter(|i| i.pointer == initializer.pointer)
        .collect::<Vec<_>>();
    let copy_survives =
        copy.is_some_and(|copy| g.instructions.iter().any(|i| i.pointer == copy.pointer));
    if current.len() != 1 || !defines(current[0], r) || copy_survives {
        return Ok((
            false_result(
                "the initialization still requires a copy into the anchored allocation root",
            ),
            json!({"root":r,"value":b.id,"initialization":initializer,"copy":copy,"copy_survives":copy_survives}),
        ));
    }
    let after: Vec<_> = allocations[0]
        .after
        .blocks
        .iter()
        .flat_map(|b| &b.instructions)
        .filter(|i| i.pointer == initializer.pointer)
        .collect();
    ensure!(
        after.len() == 1,
        "initialization is not uniquely visible after allocation"
    );
    let final_instruction = after[0];
    let registers: Vec<_> = final_instruction
        .operands
        .iter()
        .filter(|o| o.defines() && o.raw[1] == g.class)
        .map(|o| o.register())
        .collect();
    ensure!(
        registers.len() == 1 && registers[0] < g.physical_limit,
        "initialization has no unique final physical destination"
    );
    let rounds: Vec<_> = allocations[0]
        .before
        .rounds
        .iter()
        .filter(|r| r.class == g.class)
        .collect();
    let graphs: Vec<_> = input
        .provenance
        .graphs
        .graphs
        .iter()
        .filter(|v| v.function == g.function && v.class == g.class)
        .collect();
    ensure!(
        graphs.len() == rounds.len() && graphs.last().is_some_and(|last| last.id == g.id),
        "direct initialization needs the final complete graph round"
    );
    let round = rounds
        .get(
            input
                .provenance
                .graphs
                .graphs
                .iter()
                .filter(|v| {
                    v.function == g.function
                        && v.class == g.class
                        && v.begin_position < g.begin_position
                })
                .count(),
        )
        .context("missing matching allocation round")?;
    let mut colours: BTreeMap<_, _> = round
        .colors
        .iter()
        .filter_map(|(v, (c, _))| (*c >= 0).then_some((*v, *c)))
        .collect();
    crate::colouring::final_colours(round, &mut colours);
    let assigned = if r < g.physical_limit {
        r as i32
    } else {
        *colours
            .get(&r)
            .context("no recorded final color for anchored root")?
    };
    let same = final_instruction.opcode == initializer.opcode
        && registers[0] as i32 == assigned
        && final_instruction.operands.iter().any(|o| {
            o.kind() == 2 && i32::from_le_bytes(o.raw[2..6].try_into().unwrap()) == constant
        });
    let result = if same {
        known()
    } else {
        false_result("initialization is not directly in the anchored value's final register")
    };
    Ok((
        result,
        json!({"root":r,"value":b.id,"initialization":initializer,"copy":copy,"copy_survives":false,"final_instruction":final_instruction,"assigned_register":assigned}),
    ))
}
pub fn evaluate(
    c: &Contract,
    baseline: &Input<'_>,
    candidate: &Input<'_>,
) -> Result<EvaluationReport> {
    validate(c)?;
    let mut constraints = BTreeMap::new();
    constraints.insert(
        "captured_source".into(),
        if state::digest(baseline.source) == baseline.compilation.source_sha256
            && state::digest(candidate.source) == candidate.compilation.source_sha256
        {
            known()
        } else {
            gap(
                EvidenceGap::BuildInputCoverage,
                "source bytes do not match the compiler capture",
            )
        },
    );
    constraints.insert(
        "compiler_context".into(),
        if baseline.compilation.version == candidate.compilation.version
            && baseline.compilation.producer_sha256 == candidate.compilation.producer_sha256
            && baseline.compilation.original_sha256 == candidate.compilation.original_sha256
            && baseline.compilation.flags == candidate.compilation.flags
            && serde_json::to_value(&baseline.compilation.trace_options)?
                == serde_json::to_value(&candidate.compilation.trace_options)?
        {
            known()
        } else {
            false_result("compiler identities, ordered flags or trace options changed")
        },
    );
    constraints.insert(
        "stock_equivalence".into(),
        combine(
            [
                &baseline.compilation.check_status,
                &candidate.compilation.check_status,
            ]
            .map(|status| match status {
                CheckStatus::Verified => known(),
                CheckStatus::Skipped | CheckStatus::OriginalUnavailable => gap(
                    EvidenceGap::CompilationCoverage,
                    "stock-equivalence evidence is unavailable",
                ),
                _ => false_result("compiler output is not stock-equivalent"),
            }),
        ),
    );
    constraints.insert(
        "symbolic_relocations".into(),
        combine([
            relocation_check(baseline, c),
            relocation_check(candidate, c),
        ]),
    );
    constraints.insert(
        "semantic_check".into(),
        match (
            c.purpose,
            &baseline.semantic_check,
            &candidate.semantic_check,
        ) {
            (Purpose::MechanismReproducer, None, None) => known(),
            (_, Some(a), Some(b)) => combine([a.clone(), b.clone()]),
            _ => gap(
                EvidenceGap::MissingSemanticCheck,
                "source transformations require a successful configured semantic check",
            ),
        },
    );
    let selected = (|| -> Result<_> {
        Ok((
            state::graph(
                baseline.provenance,
                &c.selection.link,
                c.selection.class,
                c.selection.round,
            )?,
            state::graph(
                candidate.provenance,
                &c.selection.link,
                c.selection.class,
                c.selection.round,
            )?,
        ))
    })();
    let (objective, observations, graph_changes) = match selected {
        Err(e) => {
            let missing = gap(EvidenceGap::MissingGraphSnapshot, &format!("{e:#}"));
            constraints.insert("graph_capture".into(), missing.clone());
            (missing, json!({}), json!({}))
        }
        Ok((a, b)) => {
            let function_capture = |input: &Input<'_>, function| {
                input
                    .provenance
                    .graphs
                    .functions
                    .iter()
                    .find(|f| f.function == function)
                    .map(|f| f.observations.clone())
                    .unwrap_or_else(|| {
                        gap(
                            EvidenceGap::MissingGraphSnapshot,
                            "function graph-count coverage is missing",
                        )
                    })
            };
            constraints.insert(
                "graph_capture".into(),
                combine([
                    a.observations.clone(),
                    b.observations.clone(),
                    function_capture(baseline, a.function),
                    function_capture(candidate, b.function),
                ]),
            );
            let changes = delta(a, b, candidate.compilation.version);
            let tested = (|| -> Result<(Evaluation, Value)> {
                if let Objective::SymbolAddressPairs {
                    symbol,
                    minimum,
                    maximum,
                } = &c.objective
                {
                    let from = symbol_address_pairs(baseline, a, symbol)?;
                    let to = symbol_address_pairs(candidate, b, symbol)?;
                    let count = to["count"]
                        .as_u64()
                        .context("missing materialization count")?;
                    let result = if u64::from(*minimum) <= count && count <= u64::from(*maximum) {
                        known()
                    } else {
                        false_result(
                            "the observed symbolic-address pair count is outside the requested bounds",
                        )
                    };
                    return Ok((
                        result,
                        json!({"baseline":from,"candidate":to,
                        "minimum":minimum,"maximum":maximum,"symbol":symbol,
                        "semantic_limit":"Counts one guarded materialization path; does not prove complete temporary elimination."}),
                    ));
                }
                let selector = match &c.objective {
                    Objective::ChangeRangeCategory { value, .. }
                    | Objective::DirectInitialization { value, .. }
                    | Objective::EliminateTemporary { value } => value,
                    Objective::SymbolAddressPairs { .. } => unreachable!(),
                };
                let from = anchor(baseline, a, selector)?;
                let to = anchor(candidate, b, selector)?;
                match &c.objective {
                    Objective::ChangeRangeCategory { target, .. } => {
                        let from_root = root(a, from)?;
                        let to_root = root(b, to)?;
                        let x = category(a, from_root)?;
                        let y = category(b, to_root)?;
                        let instruction_state = combine([
                            if state::instructions(a)? == state::instructions(b)? {
                                known()
                            } else {
                                false_result("graph-boundary instruction structure changed")
                            },
                            if state::object_instructions(
                                &baseline.compilation.object,
                                &c.selection.link,
                            )? == state::object_instructions(
                                &candidate.compilation.object,
                                &c.selection.link,
                            )? {
                                known()
                            } else {
                                false_result(
                                    "final instruction structure or symbolic relocations changed",
                                )
                            },
                        ]);
                        constraints.insert("instructions_preserved".into(), instruction_state);
                        let result = if x.evidence.state != EvaluationState::Satisfied
                            || y.evidence.state != EvaluationState::Satisfied
                        {
                            combine([x.evidence.clone(), y.evidence.clone()])
                        } else {
                            if x.category != Some(*target) && y.category == Some(*target) {
                                known()
                            } else {
                                false_result(
                                    "the anchored value did not change into the requested allocation range",
                                )
                            }
                        };
                        Ok((
                            result,
                            json!({"baseline_value":from.id,"candidate_value":to.id,"baseline_root":from_root,"candidate_root":to_root,"baseline_category":x,"candidate_category":y,"anchor_scope":"unique_complete_observed_object_name_type_and_pair_part"}),
                        ))
                    }
                    Objective::DirectInitialization {
                        constant,
                        source_line,
                        ..
                    } => direct_initialization(candidate, b, to, *constant, *source_line),
                    Objective::EliminateTemporary { .. } => Ok((
                        gap(
                            EvidenceGap::BackendLifecycleCoverage,
                            "complete source-role/lifetime coverage is required to prove elimination; missing anchors are not elimination",
                        ),
                        json!({"baseline_value":from.id,"candidate_value":to.id}),
                    )),
                    Objective::SymbolAddressPairs { .. } => unreachable!(),
                }
            })();
            match tested {
                Ok((e, observations)) => (e, observations, changes),
                Err(e) => (
                    gap(EvidenceGap::AmbiguousCorrespondence, &format!("{e:#}")),
                    json!({}),
                    changes,
                ),
            }
        }
    };
    let overall = combine(constraints.values().cloned().chain([objective.clone()]));
    let eligible = constraints
        .values()
        .all(|c| c.state == EvaluationState::Satisfied);
    let score = Score {
        eligible,
        objective_cost: match objective.state {
            EvaluationState::Satisfied => Some(0),
            EvaluationState::Unsatisfied => Some(1),
            EvaluationState::Indeterminate => None,
        },
        inferred_edges_added: graph_changes["inferred_edges_added"]
            .as_array()
            .map_or(0, Vec::len),
        inferred_edges_removed: graph_changes["inferred_edges_removed"]
            .as_array()
            .map_or(0, Vec::len),
    };
    let semantic_validation = if c.purpose == Purpose::MechanismReproducer
        && baseline.semantic_check.is_none()
        && candidate.semantic_check.is_none()
    {
        "not_checked_mechanism_only"
    } else if constraints["semantic_check"].state == EvaluationState::Satisfied {
        "configured_check_passed"
    } else {
        "missing_or_failed_check"
    }
    .into();
    Ok(EvaluationReport {
        format_version: 1,
        purpose: c.purpose,
        semantic_validation,
        score,
        constraints,
        objective,
        overall,
        observations,
        graph_changes,
        final_target_mismatch: None,
    })
}
