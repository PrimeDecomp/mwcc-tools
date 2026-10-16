//! One evidence-aware evaluator for search, reduction and corpus predicates.
use super::state;
use crate::{
    allocation_category::Category,
    evidence::{Evaluation, EvaluationState, EvidenceGap, Reason},
    graph_history::Graph,
    provenance::Report,
    runner::{CheckStatus, Compilation},
    value_history::{AddressPart, Birth, ObjectPart},
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
#[serde(deny_unknown_fields)]
pub struct PromotedElementAnchor {
    pub name: String,
    pub element_index: u32,
    pub element_size: i32,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
pub enum CopyOutcome {
    Coalesced,
    Rejected { condition: String },
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
pub enum Objective {
    /// An explicit ordinal definition in the observed allocation input stream.
    /// This does not infer C++ initialization, dominance or execution order.
    ScalarizedArrayDefinition {
        value: PromotedElementAnchor,
        definition: usize,
    },
    /// A whole source array element rewritten by the guarded frontend
    /// scalarizer, initialized through the existing LI/copy recognizer.
    ScalarizedArrayInitialization {
        value: PromotedElementAnchor,
        constant: i32,
        source_line: Option<i32>,
    },
    /// The ordinal incoming register-copy decision for an observed promoted
    /// element. This is not an automatic source-initializer/dominance binding.
    PromotedCopyCoalescing {
        value: PromotedElementAnchor,
        copy: usize,
        outcome: CopyOutcome,
    },
    /// Observe the current category without requiring a transition or fixed
    /// instruction structure. Useful when preserving an established mechanism.
    RangeCategory {
        value: ObjectAnchor,
        target: Category,
    },
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
    if let Objective::PromotedCopyCoalescing { value, outcome, .. } = &c.objective {
        ensure!(
            matches!(c.selection.class, 3 | 4)
                && !value.name.is_empty()
                && value.element_index < 8
                && value.element_size > 0,
            "promoted copy predicate requires a scalar FPR/GPR array name, element index and positive element size"
        );
        if let CopyOutcome::Rejected { condition } = outcome {
            ensure!(
                matches!(
                    condition.as_str(),
                    "interference"
                        | "destination_below_window"
                        | "destination_above_window"
                        | "source_below_window"
                        | "source_above_window"
                        | "gpr_special_register"
                ),
                "unsupported promoted register-copy rejection condition"
            );
        }
    }
    if let Objective::ScalarizedArrayInitialization { value, .. } = &c.objective {
        ensure!(
            c.selection.class == 4 && !value.name.is_empty() && value.element_size > 0,
            "scalarized initialization currently requires a named GPR array element and positive element size"
        );
    }
    if let Objective::ScalarizedArrayDefinition { value, .. } = &c.objective {
        ensure!(
            matches!(c.selection.class, 3 | 4) && !value.name.is_empty() && value.element_size > 0,
            "scalarized definition requires a named FPR/GPR array element and positive element size"
        );
    }
    Ok(())
}
fn possible_scalarized_element(
    binding: &crate::scalarization::Binding,
    value: &PromotedElementAnchor,
) -> bool {
    let source = &binding.source;
    let name = if source.name_known && source.name_complete {
        source.name_bytes == value.name.as_bytes()
    } else {
        value.name.as_bytes().starts_with(&source.name_bytes)
    };
    let offset = value.element_index.checked_mul(value.element_size as u32);
    name && source.type_kind == 12
        && binding.flags & 3 == 0
        && binding.byte_size == value.element_size
        && offset == u32::try_from(binding.byte_offset).ok()
        && (!source.element_known
            || (source.element_type_size == value.element_size
                && binding.temporary.type_kind == source.element_type_kind))
}
fn promoted_copy(
    input: &Input<'_>,
    graph: &Graph,
    value: &PromotedElementAnchor,
    ordinal: usize,
    outcome: &CopyOutcome,
) -> Result<(Evaluation, Value)> {
    let function = input
        .provenance
        .history
        .functions
        .iter()
        .find(|f| f.id == graph.function)
        .context("missing promoted-element function")?;
    ensure!(
        function.array_promotion_observations.state == EvaluationState::Satisfied
            && function.counter_observations.state == EvaluationState::Satisfied,
        "complete guarded scalar-promotion and counter capture is required"
    );
    let matches: Vec<_> = function
        .births
        .iter()
        .filter(|b| {
            b.class == graph.class
                && b.array_promotion.as_ref().is_some_and(|a| {
                    a.name_known
                        && a.name_complete
                        && a.name_bytes == value.name.as_bytes()
                        && a.element_index == value.element_index
                        && a.element_size == value.element_size
                })
        })
        .collect();
    ensure!(
        matches.len() == 1,
        "promoted-element anchor must identify one complete observed birth in the whole function"
    );
    let birth = matches[0];
    ensure!(
        birth.generation == 0
            && function
                .births
                .iter()
                .filter(|b| b.class == birth.class && b.register == birth.register)
                .count()
                == 1,
        "promoted register number has reused/ambiguous generations"
    );
    let node = graph
        .nodes
        .get(&birth.register)
        .context("promoted element has no original graph node")?;
    ensure!(
        node.binding.evidence.state == EvaluationState::Satisfied
            && node.binding.direct == [birth.id],
        "promoted graph node does not bind exactly its unique observed birth"
    );
    let pass = input
        .provenance
        .passes
        .iter()
        .filter(|p| {
            p.class == graph.class
                && p.link == graph.link
                && p.begin_position < graph.begin_position
                && p.header
                    .get("function_id")
                    .and_then(|id| id.parse::<u32>().ok())
                    == Some(graph.function)
        })
        .max_by_key(|p| p.begin_position)
        .context("no selected-function coalescer pass precedes the graph")?;
    ensure!(
        birth.trace_position < pass.begin_position
            && pass.ended
            && pass
                .end_position
                .is_some_and(|end| end < graph.begin_position)
            && pass.decisions.state == EvaluationState::Satisfied,
        "promoted copy requires a complete coalescer pass after its birth"
    );
    let bounds = graph
        .range_bounds
        .as_ref()
        .context("selected graph lacks allocation-window evidence")?;
    ensure!(
        bounds.evidence.state == EvaluationState::Satisfied,
        "selected graph allocation-window evidence is incomplete"
    );
    let header = |name: &str| -> Result<i64> {
        pass.header
            .get(name)
            .with_context(|| format!("coalescer lacks {name}"))?
            .parse()
            .with_context(|| format!("coalescer has invalid {name}"))
    };
    ensure!(
        header("count")? == i64::from(graph.count)
            && header("physical")? == i64::from(graph.physical_limit)
            && header("first")? == i64::from(bounds.first)
            && header("last")? == i64::from(bounds.last),
        "coalescer bounds disagree with the selected graph"
    );
    ensure!(
        pass.candidates
            .iter()
            .all(|c| c.trace_position < graph.begin_position
                && c.decision_position
                    .is_some_and(|p| p < graph.begin_position)),
        "coalescer decisions extend beyond the selected graph boundary"
    );
    let number = |candidate: &crate::provenance::Candidate, name: &str| -> Result<u32> {
        candidate
            .operands
            .get(name)
            .with_context(|| format!("copy candidate lacks {name}"))?
            .parse()
            .with_context(|| format!("copy candidate has invalid {name}"))
    };
    let mut incoming = vec![];
    for candidate in &pass.candidates {
        if number(candidate, "destination")? != birth.register {
            continue;
        }
        let is_copy = candidate
            .checks
            .iter()
            .any(|check| check.condition == "copy_flag" && !check.taken);
        if !is_copy {
            continue;
        }
        if number(candidate, "destination_kind")? != 0
            || number(candidate, "source_kind")? != 0
            || number(candidate, "destination_class")? != u32::from(graph.class)
            || number(candidate, "source_class")? != u32::from(graph.class)
        {
            continue;
        }
        let opcode = number(candidate, "opcode")?;
        ensure!(
            number(candidate, "operands")? >= 2 && number(candidate, "source")? < graph.count,
            "incoming copy has invalid operand count or source register"
        );
        let expected = if graph.class == 4 { "mr" } else { "fmr" };
        ensure!(
            crate::version::opcode_names(input.compilation.version)
                .get(&(opcode as i32))
                .is_some_and(|name| name == expected),
            "incoming promoted register-copy opcode is inconsistent with its selected class"
        );
        ensure!(
            candidate.evidence.state == EvaluationState::Satisfied,
            "incoming promoted copy decision is incomplete"
        );
        incoming.push(candidate);
    }
    let selected = incoming.get(ordinal).copied();
    let result = match selected {
        None if incoming.is_empty() => gap(
            EvidenceGap::BackendLifecycleCoverage,
            "no incoming register-copy is bound to the promoted birth; an invisible/ghost promotion node is not source-lifetime or coalescing-success evidence",
        ),
        None => false_result(
            "the complete selected pass has no incoming register-copy at the requested ordinal",
        ),
        Some(selected) => {
            let matched = match (outcome, &selected.decision) {
                (
                    CopyOutcome::Coalesced,
                    crate::provenance::Decision::Merged { .. }
                    | crate::provenance::Decision::AlreadySameRoot { .. },
                ) => true,
                (
                    CopyOutcome::Rejected { condition },
                    crate::provenance::Decision::Rejected { condition: actual },
                ) => condition == actual,
                _ => false,
            };
            if matched {
                known()
            } else {
                false_result(
                    "the observed incoming-copy decision differs from the requested outcome",
                )
            }
        }
    };
    Ok((
        result,
        json!({"value":birth.id,"register":birth.register,"array_origin":birth.array_promotion,"node_binding":node.binding,"pass":pass.id,"window":bounds,"copy_ordinal":ordinal,"incoming_copies":incoming,"selected":selected,"expected":outcome,"scope":"ordinal_observed_incoming_register_copy_at_selected_coalescer_pass","semantic_limit":"No automatic source-initializer, dominance, full lifetime or physical-register claim."}),
    ))
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
fn delta(c: &Contract, baseline: &Input<'_>, candidate: &Input<'_>) -> (Value, Evaluation) {
    use super::correspondence::{self, Boundary, Selection};
    let selected = Selection {
        link: c.selection.link.clone(),
        class: c.selection.class,
        round: c.selection.round,
        boundary: Boundary::BeforeSimplify,
    };
    let comparison = if baseline.compilation.version == candidate.compilation.version {
        correspondence::compare(
            baseline.provenance,
            &selected,
            candidate.provenance,
            &selected,
            candidate.compilation.version,
        )
    } else {
        Err(anyhow::anyhow!("compiler versions differ"))
    };
    match comparison {
        Ok(report) => {
            let mut added = vec![];
            let mut removed = vec![];
            if report.coverage.state == EvaluationState::Satisfied {
                for change in &report.neighbor_differences {
                    if change.first_occurrences == 0 && change.second_occurrences > 0 {
                        added.push((change.second, change.neighbor_in_second_space));
                    }
                    if change.first_occurrences > 0 && change.second_occurrences == 0 {
                        removed.push((change.second, change.neighbor_in_second_space));
                    }
                }
            }
            let coverage = report.coverage.clone();
            (
                json!({"format_version":1,"method":"conditional_provenance_and_instruction_hypotheses","paired_instructions":report.paired_instructions,"instruction_counts":report.instruction_counts,"correspondence":report.mapping,
                "inferred_edges_added":added,"inferred_edges_removed":removed,"neighbor_multiplicity_changes":report.neighbor_differences,"boundary_first":report.boundary_first,"boundary_second":report.boundary_second,
                "elimination":report.elimination,"provenance_correspondence":report}),
                coverage,
            )
        }
        Err(error) => {
            let coverage = gap(
                EvidenceGap::MissingGraphSnapshot,
                &format!("observed-boundary comparison unavailable: {error:#}"),
            );
            (
                json!({"format_version":1,"method":"conditional_provenance_and_instruction_hypotheses","inferred_edges_added":[],"inferred_edges_removed":[],"neighbor_multiplicity_changes":[],"boundary_first":[],"boundary_second":[],"provenance_correspondence":null,"coverage":coverage,"error":format!("{error:#}"),"elimination":gap(EvidenceGap::BackendLifecycleCoverage,"missing comparison cannot prove elimination")}),
                coverage,
            )
        }
    }
}
fn observed_definition(
    input: &Input<'_>,
    g: &Graph,
    b: &Birth,
    ordinal: usize,
) -> Result<(Evaluation, Value)> {
    ensure!(
        matches!(g.class, 3 | 4) && g.physical_limit == 32,
        "observed definition requires a FPR/GPR graph"
    );
    let function = input
        .provenance
        .history
        .functions
        .iter()
        .find(|f| f.id == g.function)
        .context("definition function missing")?;
    ensure!(
        function
            .births
            .iter()
            .filter(|v| v.class == b.class && v.register == b.register)
            .count()
            == 1,
        "definition register has reused/ambiguous generations"
    );
    let trace = crate::trace::parse(&input.compilation.trace)?;
    let allocations = trace
        .allocations
        .iter()
        .filter(|a| a.before.link == g.link)
        .collect::<Vec<_>>();
    ensure!(
        allocations.len() == 1,
        "definition link does not identify one allocation capture"
    );
    let allocation = allocations[0];
    let definitions = allocation
        .before
        .blocks
        .iter()
        .enumerate()
        .flat_map(|(block_index, block)| {
            block
                .instructions
                .iter()
                .enumerate()
                .filter(move |(_, i)| defines_register(i, g.class, b.register))
                .map(move |(instruction_index, i)| (block_index, instruction_index, block, i))
        })
        .collect::<Vec<_>>();
    let candidates=definitions.iter().enumerate().map(|(ordinal,(block_index,index,block,instruction))|json!({"ordinal":ordinal,"block_ordinal":block_index,"block_index":block.index,"instruction_index":index,"instruction":instruction})).collect::<Vec<_>>();
    let Some((_, index, block, selected)) = definitions.get(ordinal).copied() else {
        return Ok((
            if definitions.is_empty() {
                gap(
                    EvidenceGap::BackendLifecycleCoverage,
                    "no observed definition is bound to this scalarized birth; source execution/lifetime is unknown",
                )
            } else {
                false_result("no observed definition at requested ordinal")
            },
            json!({"definitions":candidates,"definition":ordinal}),
        ));
    };
    ensure!(
        allocation
            .before
            .blocks
            .iter()
            .flat_map(|b| &b.instructions)
            .filter(|i| i.pointer == selected.pointer)
            .count()
            == 1,
        "selected definition instruction identity is ambiguous"
    );
    ensure!(
        selected
            .operands
            .iter()
            .filter(|o| o.defines() && o.raw[1] == g.class)
            .count()
            == 1,
        "selected definition has multiple class outputs"
    );
    let names = crate::version::opcode_names(input.compilation.version);
    let is_copy = names
        .get(&selected.opcode)
        .is_some_and(|name| matches!(name.as_str(), "mr" | "fmr"));
    let mut copy_decision = None;
    let (producer, copy) = if is_copy {
        ensure!(
            names
                .get(&selected.opcode)
                .is_some_and(|name| name == if g.class == 4 { "mr" } else { "fmr" }),
            "copy opcode does not match selected register class"
        );
        let sources = selected
            .operands
            .iter()
            .filter(|o| o.uses() && o.raw[1] == g.class)
            .collect::<Vec<_>>();
        ensure!(
            sources.len() == 1,
            "selected copy has no unique register source"
        );
        let source = sources[0].register();
        let pass = input
            .provenance
            .passes
            .iter()
            .filter(|p| {
                p.class == g.class
                    && p.link == g.link
                    && p.begin_position < g.begin_position
                    && p.header
                        .get("function_id")
                        .and_then(|v| v.parse::<u32>().ok())
                        == Some(g.function)
            })
            .max_by_key(|p| p.begin_position)
            .context("selected copy has no observed coalescer pass")?;
        ensure!(
            pass.ended
                && pass.end_position.is_some_and(|end| end < g.begin_position)
                && pass.decisions.state == EvaluationState::Satisfied,
            "selected copy coalescer pass is incomplete or after graph"
        );
        let number = |key: &str| -> Result<u32> {
            pass.header
                .get(key)
                .with_context(|| format!("copy pass lacks {key}"))?
                .parse()
                .with_context(|| format!("invalid copy pass {key}"))
        };
        ensure!(
            number("count")? == g.count && number("physical")? == g.physical_limit,
            "copy pass does not bind the selected graph count/class"
        );
        let matches = pass
            .candidates
            .iter()
            .filter(|c| c.instruction == selected.pointer)
            .collect::<Vec<_>>();
        ensure!(
            matches.len() == 1,
            "copy instruction has missing/ambiguous deciding candidate"
        );
        let candidate = matches[0];
        ensure!(
            candidate.evidence.state == EvaluationState::Satisfied
                && candidate
                    .decision_position
                    .is_some_and(|p| p < g.begin_position),
            "copy deciding evidence incomplete"
        );
        let operand = |key: &str| -> Result<u32> {
            candidate
                .operands
                .get(key)
                .with_context(|| format!("copy candidate lacks {key}"))?
                .parse()
                .with_context(|| format!("invalid copy candidate {key}"))
        };
        ensure!(
            operand("destination_kind")? == 0
                && operand("source_kind")? == 0
                && operand("destination_class")? == u32::from(g.class)
                && operand("source_class")? == u32::from(g.class)
                && operand("destination")? == b.register
                && operand("source")? == source
                && operand("opcode")? == selected.opcode as u32
                && candidate.checks.iter().any(|c| c.condition == "copy_flag"),
            "coalescer candidate disagrees with selected register copy"
        );
        copy_decision = Some(json!({"pass":pass.id,"candidate":candidate}));
        match candidate.decision {
            crate::provenance::Decision::Merged { .. } => ensure!(
                g.merges.iter().any(|m| m.pass == pass.id
                    && m.candidate == candidate.id
                    && m.binding.state == EvaluationState::Satisfied),
                "copy merge lacks complete graph lifetime binding"
            ),
            crate::provenance::Decision::AlreadySameRoot { .. } => {}
            _ => {
                return Ok((
                    false_result("selected definition copy was not observed coalesced"),
                    json!({"definitions":candidates,"definition":ordinal,"copy_decision":copy_decision}),
                ));
            }
        }
        let producer = block.instructions[..index]
            .iter()
            .rev()
            .take_while(|i| i.flags & (1 | 8 | 0x100) == 0)
            .find(|i| defines_register(i, g.class, source))
            .context(
                "selected copy has no observed same-block producer before a control barrier",
            )?;
        ensure!(
            producer
                .operands
                .iter()
                .filter(|o| o.defines() && o.raw[1] == g.class)
                .count()
                == 1,
            "copy producer has multiple class outputs"
        );
        (producer, Some(selected))
    } else {
        (selected, None)
    };
    let (evaluation, mut observations) =
        definition_projection(input, g, b, allocation, producer, copy)?;
    if let Some(object) = observations.as_object_mut()
        && let Some(producer) = object.remove("initialization")
    {
        object.insert("producer".into(), producer);
    }
    observations["definitions"] = json!(candidates);
    observations["definition"] = json!(ordinal);
    observations["selected_definition"] = json!(selected);
    observations["copy_decision"] = json!(copy_decision);
    observations["scope"] = json!(
        "ordinal_in_observed_ra_before_block_instruction_stream; no C++ initialization, dominance or execution-order claim"
    );
    Ok((evaluation, observations))
}
fn initializer_block(blocks: &[crate::trace::Block]) -> Result<&crate::trace::Block> {
    let key = |s: &str| -> Result<u32> {
        let p = u32::from_str_radix(s, 16)?;
        ensure!(p != 0, "null CFG block identity");
        Ok(p)
    };
    let mut nodes = BTreeMap::new();
    for block in blocks {
        ensure!(
            nodes.insert(key(&block.pointer)?, block).is_none(),
            "duplicate CFG block identity"
        );
    }
    let mut incoming = nodes
        .keys()
        .map(|p| (*p, 0usize))
        .collect::<BTreeMap<_, _>>();
    for block in blocks {
        ensure!(
            block.successors.len() <= 1,
            "initialization CFG branches; dominance is unknown"
        );
        for next in &block.successors {
            *incoming
                .get_mut(&key(next)?)
                .context("initialization CFG has an unobserved external edge")? += 1;
        }
    }
    let entries = incoming
        .iter()
        .filter(|(_, n)| **n == 0)
        .map(|(p, _)| *p)
        .collect::<Vec<_>>();
    ensure!(
        entries.len() == 1 && incoming.values().all(|n| *n <= 1),
        "initialization CFG lacks one linear entry"
    );
    let mut visited = BTreeSet::new();
    let mut next = Some(entries[0]);
    while let Some(p) = next {
        ensure!(visited.insert(p), "initialization CFG contains a cycle");
        let block = nodes[&p];
        next = block.successors.first().map(|p| key(p)).transpose()?;
    }
    ensure!(
        visited.len() == blocks.len(),
        "initialization CFG has unreachable or cyclic blocks"
    );
    let body = blocks
        .iter()
        .filter(|b| !b.instructions.is_empty())
        .collect::<Vec<_>>();
    ensure!(
        body.len() == 1,
        "initialization needs one instruction-bearing block; cross-block dominance is unknown"
    );
    Ok(body[0])
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
    // Recorded block order is not a dominance or C++ declaration order proof.
    // Keep this recognizer deliberately narrow; later definitions have their
    // own explicit observed-definition predicate.
    let block = initializer_block(&allocations[0].before.blocks)?;
    let (index, instruction) = block
        .instructions
        .iter()
        .enumerate()
        .find(|(_, i)| defines(i, b.register))
        .context("original value has no observed first definition")?;
    ensure!(
        block.instructions[..index]
            .iter()
            .all(|i| i.flags & (1 | 8 | 0x100) == 0
                && !i
                    .operands
                    .iter()
                    .any(|o| o.uses() && o.raw[1] == g.class && o.register() == b.register)),
        "first definition follows a control barrier or earlier use; initialization is unknown"
    );
    ensure!(
        line.is_none_or(|l| instruction.line == Some(l)),
        "requested source line does not identify the original first definition"
    );
    let mut initializers = vec![];
    if immediate(instruction) {
        initializers.push((instruction, None));
    }
    if names.get(&instruction.opcode).is_some_and(|n| n == "mr") && instruction.flags & 0x10 != 0 {
        let sources = instruction
            .operands
            .iter()
            .filter(|o| o.uses() && o.raw[1] == g.class)
            .collect::<Vec<_>>();
        ensure!(
            sources.len() == 1,
            "first copy has no unique register source"
        );
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
    ensure!(
        initializers.len() == 1,
        "expected one immediate initializer bound to the original named value, directly or through one local copy"
    );
    let (initializer, copy) = initializers[0];
    definition_projection(input, g, b, allocations[0], initializer, copy)
}
fn defines_register(i: &crate::trace::Instruction, class: u8, register: u32) -> bool {
    i.operands
        .iter()
        .any(|o| o.defines() && o.raw[1] == class && o.register() == register)
}
fn compatible_instruction(
    before: &crate::trace::Instruction,
    after: &crate::trace::Instruction,
) -> bool {
    before.opcode == after.opcode
        && before.operands.len() == after.operands.len()
        && before
            .operands
            .iter()
            .zip(&after.operands)
            .all(|(before, after)| {
                let mut a = before.raw;
                let mut b = after.raw;
                if before.kind() == 0 && after.kind() == 0 {
                    let physical = match before.raw[1] {
                        0 => 4,
                        1 => 8,
                        _ => 32,
                    };
                    if before.register() < physical && before.register() != after.register() {
                        return false;
                    }
                    a[4..6].fill(0);
                    b[4..6].fill(0);
                    // Allocation's last-use bit may change; read/write roles remain.
                    a[2] &= !4;
                    b[2] &= !4;
                }
                a == b
            })
}
fn definition_projection(
    input: &Input<'_>,
    g: &Graph,
    b: &Birth,
    allocation: &crate::trace::Allocation,
    initializer: &crate::trace::Instruction,
    copy: Option<&crate::trace::Instruction>,
) -> Result<(Evaluation, Value)> {
    let r = root(g, b)?;
    let defines = |i: &crate::trace::Instruction, register| defines_register(i, g.class, register);
    let current = g
        .instructions
        .iter()
        .filter(|i| i.pointer == initializer.pointer)
        .collect::<Vec<_>>();
    let copy_survives = copy.is_some_and(|copy| {
        g.instructions
            .iter()
            .chain(allocation.after.blocks.iter().flat_map(|b| &b.instructions))
            .any(|i| i.pointer == copy.pointer)
    });
    if current.len() != 1
        || !defines(current[0], r)
        || !compatible_instruction(initializer, current[0])
        || copy_survives
    {
        return Ok((
            false_result(
                "the initialization still requires a copy into the anchored allocation root",
            ),
            json!({"root":r,"value":b.id,"initialization":initializer,"copy":copy,"copy_survives":copy_survives}),
        ));
    }
    let after: Vec<_> = allocation
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
    let rounds: Vec<_> = allocation
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
    let same =
        compatible_instruction(initializer, final_instruction) && registers[0] as i32 == assigned;
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
        "captured_object".into(),
        if state::digest(&baseline.compilation.object) == baseline.compilation.object_sha256
            && state::digest(&candidate.compilation.object) == candidate.compilation.object_sha256
        {
            known()
        } else {
            gap(
                EvidenceGap::BuildInputCoverage,
                "object bytes do not match the compiler capture",
            )
        },
    );
    constraints.insert(
        "captured_provenance".into(),
        if baseline.provenance.trace_sha256 == state::digest(baseline.compilation.trace.as_bytes())
            && candidate.provenance.trace_sha256
                == state::digest(candidate.compilation.trace.as_bytes())
        {
            known()
        } else {
            gap(
                EvidenceGap::BuildInputCoverage,
                "provenance report is not bound to the exact compiler capture trace",
            )
        },
    );
    constraints.insert(
        "compiler_context".into(),
        if baseline.compilation.version.has_oracle()
            && candidate.compilation.version.has_oracle()
            && !baseline.compilation.producer_sha256.is_empty()
            && !candidate.compilation.producer_sha256.is_empty()
            && baseline
                .compilation
                .original_sha256
                .as_ref()
                .is_some_and(|s| !s.is_empty())
            && candidate
                .compilation
                .original_sha256
                .as_ref()
                .is_some_and(|s| !s.is_empty())
            && baseline.compilation.version == candidate.compilation.version
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
            [baseline.compilation, candidate.compilation].map(|compilation| {
                match &compilation.check_status {
                    CheckStatus::Verified => match compilation.identical {
                        Some(true) => known(),
                        Some(false) => false_result(
                            "verified stock-check status contradicts non-identical output",
                        ),
                        None => gap(
                            EvidenceGap::CompilationCoverage,
                            "verified stock-check status lacks the identical-output result",
                        ),
                    },
                    CheckStatus::Skipped | CheckStatus::OriginalUnavailable => gap(
                        EvidenceGap::CompilationCoverage,
                        "stock-equivalence evidence is unavailable",
                    ),
                    _ => false_result("compiler output is not stock-equivalent"),
                }
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
            let (changes, comparison_coverage) = delta(c, baseline, candidate);
            constraints.insert("observed_boundary_comparison".into(), comparison_coverage);
            let tested = (|| -> Result<(Evaluation, Value)> {
                if let Objective::ScalarizedArrayInitialization { value, .. }
                | Objective::ScalarizedArrayDefinition { value, .. } = &c.objective
                {
                    let f = candidate
                        .provenance
                        .history
                        .functions
                        .iter()
                        .find(|f| f.id == b.function)
                        .context("missing scalarized array function")?;
                    let selected = candidate
                        .provenance
                        .scalarization
                        .backend_values
                        .iter()
                        .filter(|v| {
                            v.value.function == f.id
                                && v.class == b.class
                                && v.array_element.as_ref().is_some_and(|e| {
                                    e.name == value.name.as_bytes()
                                        && e.element_index == value.element_index
                                        && e.element_size == value.element_size
                                })
                        })
                        .collect::<Vec<_>>();
                    ensure!(
                        selected.len() == 1
                            && selected[0].observations.state == EvaluationState::Satisfied,
                        "source element lacks one complete frontend-to-backend scalarization binding"
                    );
                    let source_matches = candidate
                        .provenance
                        .scalarization
                        .passes
                        .iter()
                        .filter(|p| p.function == Some(f.id))
                        .flat_map(|p| &p.bindings)
                        .filter(|binding| possible_scalarized_element(binding, value))
                        .count();
                    ensure!(
                        source_matches == 1,
                        "source element has multiple or unresolved possible scalarization creations in this function"
                    );
                    let birth = f
                        .births
                        .iter()
                        .find(|birth| birth.id == selected[0].value)
                        .context("scalarized backend birth missing")?;
                    ensure!(
                        birth.trace_position < b.begin_position,
                        "scalarized birth does not precede selected graph"
                    );
                    let (evaluation, mut observations) = match &c.objective {
                        Objective::ScalarizedArrayInitialization {
                            constant,
                            source_line,
                            ..
                        } => direct_initialization(candidate, b, birth, *constant, *source_line)?,
                        Objective::ScalarizedArrayDefinition { definition, .. } => {
                            observed_definition(candidate, b, birth, *definition)?
                        }
                        _ => unreachable!(),
                    };
                    observations["source_element"] = serde_json::to_value(selected[0])?;
                    observations["anchor_scope"] = json!(
                        "guarded_whole_array_element_to_exact_scalar_Object_and_backend_birth"
                    );
                    return Ok((evaluation, observations));
                }
                if let Objective::PromotedCopyCoalescing {
                    value,
                    copy,
                    outcome,
                } = &c.objective
                {
                    return promoted_copy(candidate, b, value, *copy, outcome);
                }
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
                    Objective::RangeCategory { value, .. }
                    | Objective::ChangeRangeCategory { value, .. }
                    | Objective::DirectInitialization { value, .. }
                    | Objective::EliminateTemporary { value } => value,
                    Objective::SymbolAddressPairs { .. }
                    | Objective::PromotedCopyCoalescing { .. }
                    | Objective::ScalarizedArrayInitialization { .. }
                    | Objective::ScalarizedArrayDefinition { .. } => unreachable!(),
                };
                let from = anchor(baseline, a, selector)?;
                let to = anchor(candidate, b, selector)?;
                match &c.objective {
                    Objective::RangeCategory { target, .. } => {
                        let to_root = root(b, to)?;
                        let observed = category(b, to_root)?;
                        let result = if observed.evidence.state != EvaluationState::Satisfied {
                            observed.evidence.clone()
                        } else if observed.category == Some(*target) {
                            known()
                        } else {
                            false_result(
                                "the anchored value is not in the requested current allocation range",
                            )
                        };
                        Ok((
                            result,
                            json!({"baseline_value":from.id,"candidate_value":to.id,"candidate_root":to_root,"candidate_category":observed,"target":target,"anchor_scope":"unique_complete_observed_object_name_type_and_pair_part","category_scope":"observed_root_at_selected_graph","semantic_limit":"An observed allocation range is not a source role, lifetime or array-promotion proof; no instruction preservation is required."}),
                        ))
                    }
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
                    Objective::SymbolAddressPairs { .. }
                    | Objective::PromotedCopyCoalescing { .. }
                    | Objective::ScalarizedArrayInitialization { .. }
                    | Objective::ScalarizedArrayDefinition { .. } => unreachable!(),
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
