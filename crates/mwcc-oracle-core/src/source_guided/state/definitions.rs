//! Inputs read by definition/initializer predicates, beyond the final graph.
use super::Names;
use crate::{graph_history::Graph, provenance::Report, runner::Compilation, trace};
use anyhow::{Context, Result, ensure};
use serde_json::{Value, json};
use std::collections::{BTreeMap, BTreeSet};

fn pointer(map: &mut BTreeMap<u32, u32>, text: &str) -> Result<u32> {
    Ok(super::ordinal(map, u32::from_str_radix(text, 16)?))
}
fn instruction_identity(map: &mut BTreeMap<String, u32>, text: &str) -> Result<u32> {
    u32::from_str_radix(text, 16)?;
    // Predicate instruction joins compare exact trace strings. Keep their
    // equality relation even for differently padded spellings of one address.
    let next = map.len() as u32 + 1;
    Ok(*map.entry(text.to_owned()).or_insert(next))
}
fn fields(actual: &BTreeMap<String, String>, allowed: &[&str]) -> Result<()> {
    ensure!(
        actual.keys().all(|k| allowed.contains(&k.as_str())),
        "unknown definition-state fields"
    );
    Ok(())
}
fn decimal(fields: &BTreeMap<String, String>, key: &str) -> Result<u32> {
    fields
        .get(key)
        .with_context(|| format!("missing definition-state {key}"))?
        .parse()
        .with_context(|| format!("invalid definition-state {key}"))
}
fn snapshot(
    snapshot: &trace::Snapshot,
    names: &mut Names,
    instructions: &mut BTreeMap<String, u32>,
    blocks: &mut BTreeMap<u32, u32>,
) -> Result<Value> {
    let mut result = vec![];
    for block in &snapshot.blocks {
        let id = pointer(blocks, &block.pointer)?;
        let successors = block
            .successors
            .iter()
            .map(|p| pointer(blocks, p))
            .collect::<Result<Vec<_>>>()?;
        let mut body = vec![];
        for instruction in &block.instructions {
            body.push(json!({"identity":instruction_identity(instructions,&instruction.pointer)?, "line":instruction.line,"shape":names.instruction(instruction)?}));
        }
        result.push(json!({"identity":id,"index":block.index,"weight":block.weight,"successors":successors,"instructions":body}));
    }
    let rounds = snapshot.rounds.iter().map(|r| {
        // Register numbers are deliberately retained in rounds. This can miss
        // duplicates but preserves their binding to recorded union/color maps.
        let mut final_colours = r.colors.iter().filter_map(|(v,(c,_))| (*c>=0).then_some((*v,*c))).collect::<BTreeMap<_,_>>();
        crate::colouring::final_colours(r,&mut final_colours);
        json!({"class":r.class,"k":r.k,"start":r.start,"count":r.count,
            "nodes":r.nodes.iter().map(|(v,n)|json!({"register":v,"degree":n.degree,"flags":n.flags,"color":n.color,"neighbors":n.neighbors})).collect::<Vec<_>>(),
            "order":r.order,"mask":r.mask,"mask_after":r.mask_after,"ok":r.ok,"colors":r.colors,"color_order":r.color_order,"final_colours":final_colours})
    }).collect::<Vec<_>>();
    Ok(json!({"function":snapshot.function,"link":snapshot.link,"blocks":result,"rounds":rounds}))
}

pub(super) fn normalize(g: &Graph, report: &Report, compilation: &Compilation) -> Result<Value> {
    let mut names = Names::default();
    let mut instructions = BTreeMap::new();
    let mut graph_instruction_ids = vec![];
    for instruction in &g.instructions {
        names.instruction(instruction)?;
        graph_instruction_ids.push(instruction_identity(
            &mut instructions,
            &instruction.pointer,
        )?);
    }
    // Do not construct lossless Record maps for the unrelated whole-unit IR.
    let ra = compilation
        .trace
        .lines()
        .filter(|line| line.trim_start().starts_with("ra."))
        .collect::<Vec<_>>()
        .join("\n");
    let parsed = trace::parse(&ra)?;
    let mut selected_snapshot = false;
    let mut block_flags = vec![];
    for record in &parsed.records {
        let allowed: &[&str] = match record.kind.as_str() {
            "ra.before" | "ra.after" => &["function", "link"],
            "ra.block" => &["p", "index", "weight", "flags", "succ"],
            "ra.i" => &["p", "op", "flags", "n", "line", "ops"],
            "ra.end" => &[],
            "ra.graph" => &["cls", "k", "start", "count"],
            "ra.node" => &["v", "p", "deg", "flags", "color", "n"],
            "ra.order" => &["cls", "v"],
            "ra.select" => &["cls", "mask", "after", "ok", "v"],
            _ => anyhow::bail!("unknown allocation record in definition-state normalization"),
        };
        fields(&record.fields, allowed)?;
        if matches!(record.kind.as_str(), "ra.before" | "ra.after") {
            selected_snapshot = record
                .fields
                .get("link")
                .or_else(|| record.fields.get("function"))
                .is_some_and(|link| link == &g.link);
        } else if record.kind == "ra.end" {
            selected_snapshot = false;
        } else if selected_snapshot && record.kind == "ra.block" {
            block_flags.push(
                record
                    .fields
                    .get("flags")
                    .map(|s| u32::from_str_radix(s, 16))
                    .transpose()?,
            );
        }
    }
    let allocations = parsed
        .allocations
        .iter()
        .filter(|a| a.before.link == g.link)
        .collect::<Vec<_>>();
    ensure!(
        allocations.len() == 1,
        "definition-state requires one allocation capture for the selected link"
    );
    let allocation = allocations[0];
    let mut blocks = BTreeMap::new();
    let before = snapshot(
        &allocation.before,
        &mut names,
        &mut instructions,
        &mut blocks,
    )?;
    let after = snapshot(
        &allocation.after,
        &mut names,
        &mut instructions,
        &mut blocks,
    )?;
    let function = report
        .history
        .functions
        .iter()
        .find(|f| f.id == g.function)
        .context("missing definition-state function")?;
    let passes = report
        .passes
        .iter()
        .filter(|p| p.class == g.class && p.link == g.link && p.begin_position < g.begin_position)
        .collect::<Vec<_>>();
    let pass_ids = passes
        .iter()
        .enumerate()
        .map(|(i, p)| (p.id, i))
        .collect::<BTreeMap<_, _>>();
    // Merge/end records are summarized by Report rather than retaining their
    // original field maps. Check those maps here before trusting normalization.
    for line in compilation
        .trace
        .lines()
        .filter(|line| line.trim_start().starts_with("pv.coalesce."))
    {
        let kind = trace::record_kind(line).context("empty coalescer record")?;
        ensure!(
            [
                "pv.coalesce.begin",
                "pv.coalesce.candidate",
                "pv.coalesce.check",
                "pv.coalesce.merge",
                "pv.coalesce.end"
            ]
            .contains(&kind),
            "unknown coalescer normalization record"
        );
        if matches!(kind, "pv.coalesce.merge" | "pv.coalesce.end") {
            let record = trace::records(line)
                .pop()
                .context("empty coalescer record")?;
            if pass_ids.contains_key(&decimal(&record.fields, "pass")?) {
                fields(
                    &record.fields,
                    if kind == "pv.coalesce.merge" {
                        &["pass", "candidate", "parent", "child", "observed_parent"]
                    } else {
                        &["pass", "candidates"]
                    },
                )?;
            }
        }
    }
    let graphs = report
        .graphs
        .graphs
        .iter()
        .filter(|other| other.function == g.function && other.class == g.class)
        .collect::<Vec<_>>();
    let mut positions = BTreeSet::from([function.begin_position, g.begin_position]);
    positions.extend(function.end_position);
    for birth in &function.births {
        positions.insert(birth.trace_position);
        positions.extend(birth.object.as_ref().map(|o| o.trace_position));
        positions.extend(birth.materialization.as_ref().map(|o| o.trace_position));
        positions.extend(birth.array_promotion.as_ref().map(|o| o.trace_position));
        positions.extend(birth.call_result.as_ref().map(|o| o.trace_position));
    }
    for range in &function.ranges {
        positions.insert(range.trace_position);
    }
    for change in &function.changes {
        positions.insert(change.trace_position);
    }
    for graph in &graphs {
        positions.insert(graph.begin_position);
        positions.extend(graph.end_position);
    }
    for pass in &passes {
        positions.insert(pass.begin_position);
        positions.extend(pass.end_position);
        for candidate in &pass.candidates {
            positions.insert(candidate.trace_position);
            positions.extend(candidate.decision_position);
            for check in &candidate.checks {
                positions.insert(check.trace_position);
            }
        }
    }
    for merge in &g.merges {
        positions.insert(merge.trace_position);
    }
    let positions = positions
        .into_iter()
        .enumerate()
        .map(|(i, p)| (p, i))
        .collect::<BTreeMap<_, _>>();
    let position = |p: usize| positions[&p];
    // Whole-function selectors count even origins that do not survive in any
    // selected graph node. Preserve their metadata and literal pointer-equality
    // partitions; never turn a graph omission into uniqueness or known absence.
    let mut origin_identities = BTreeMap::new();
    let mut births = vec![];
    for birth in &function.births {
        let object=birth.object.as_ref().map(|o|Ok::<Value, anyhow::Error>(json!({"position":position(o.trace_position),"object":instruction_identity(&mut origin_identities,&o.object)?,"info":instruction_identity(&mut origin_identities,&o.variable_info)?,"part":o.part,"datatype":o.datatype,"storage":o.storage,"flags_before":o.flags_before,"type_kind":o.type_kind,"type_size":o.type_size,"name_known":o.name_known,"name_complete":o.name_complete,"name":o.name_bytes}))).transpose()?;
        let materialization=birth.materialization.as_ref().map(|m|Ok::<Value, anyhow::Error>(json!({"position":position(m.trace_position),"part":m.part,"operand":instruction_identity(&mut origin_identities,&m.operand)?,"object":instruction_identity(&mut origin_identities,&m.object)?,"kind":m.kind,"displacement":m.displacement,"datatype":m.datatype,"storage":m.storage,"symbol_known":m.symbol_known,"symbol_complete":m.symbol_complete,"symbol":m.symbol_bytes}))).transpose()?;
        let array=birth.array_promotion.as_ref().map(|a|Ok::<Value, anyhow::Error>(json!({"position":position(a.trace_position),"promotion":instruction_identity(&mut origin_identities,&a.promotion)?,"object":instruction_identity(&mut origin_identities,&a.object)?,"index":a.element_index,"element_size":a.element_size,"array_size":a.array_size,"element_count":a.element_count,"flags":a.flags&7,"datatype":a.datatype,"storage":a.storage,"type_kind":a.type_kind,"type_size":a.type_size,"name_known":a.name_known,"name_complete":a.name_complete,"name":a.name_bytes}))).transpose()?;
        // Call output storage and its raw bytes are explicitly partial. They
        // remain excluded, including inactive pointer-looking bytes.
        let call=birth.call_result.as_ref().map(|c|json!({"position":position(c.trace_position),"part":c.part,"kind":c.kind,"type_kind":c.type_kind,"type_size":c.type_size,"tree_known":c.tree_known,"tree_complete":c.tree_complete,"tree":c.tree_bytes}));
        births.push(json!({"class":birth.class,"register":birth.register,"generation":birth.generation,"site":birth.site,"phase":birth.phase,"line":birth.line,"category":birth.category,"position":position(birth.trace_position),"object":object,"materialization":materialization,"array_promotion":array,"call_result":call}));
    }
    let mut normalized_passes = vec![];
    for pass in &passes {
        fields(
            &pass.header,
            &[
                "schema",
                "pass",
                "cls",
                "count",
                "physical",
                "first",
                "last",
                "special",
                "function_id",
                "creation",
                "link",
            ],
        )?;
        let mut header = pass.header.clone();
        header.remove("pass");
        // The function ID is a capture-local identity. Preserve whether it
        // actually binds this selected function, including malformed/missing IDs.
        let function_binding = header
            .remove("function_id")
            .map(|id| id.parse::<u32>().map(|id| id == g.function))
            .transpose()?;
        let mut candidates = vec![];
        for candidate in &pass.candidates {
            fields(
                &candidate.operands,
                &[
                    "pass",
                    "candidate",
                    "instruction",
                    "flags",
                    "opcode",
                    "operands",
                    "destination_kind",
                    "destination_class",
                    "destination",
                    "source_kind",
                    "source_class",
                    "source",
                ],
            )?;
            let mut operands = candidate.operands.clone();
            for key in ["pass", "candidate", "instruction"] {
                operands.remove(key);
            }
            let mut checks = vec![];
            for check in &candidate.checks {
                fields(
                    &check.inputs,
                    &[
                        "pass",
                        "candidate",
                        "site",
                        "condition",
                        "taken",
                        "eflags",
                        "eax",
                        "ebx",
                        "ecx",
                        "edx",
                        "esi",
                        "edi",
                        "ebp",
                    ],
                )?;
                ensure!(
                    [
                        "copy_flag",
                        "destination_kind",
                        "register_class",
                        "roots_differ",
                        "interference",
                        "destination_below_window",
                        "destination_above_window",
                        "source_below_window",
                        "source_above_window",
                        "gpr_special_register"
                    ]
                    .contains(&check.condition.as_str()),
                    "unknown definition-state retail condition"
                );
                let roots = if check.condition == "roots_differ" {
                    Some(
                        ["edx", "ebx"]
                            .map(|key| {
                                check
                                    .inputs
                                    .get(key)
                                    .context("missing coalescer root")
                                    .and_then(|s| Ok(u32::from_str_radix(s, 16)? as u16))
                            })
                            .into_iter()
                            .collect::<Result<Vec<_>>>()?,
                    )
                } else {
                    None
                };
                // Other saved CPU registers are unused by the predicates and
                // may contain compiler pointers. They do not become anchors.
                checks.push(json!({"position":position(check.trace_position),"condition":check.condition,"taken":check.taken,"site":check.inputs.get("site"),"eflags":check.inputs.get("eflags"),"roots":roots}));
            }
            candidates.push(json!({"instruction":instruction_identity(&mut instructions,&candidate.instruction)?,"position":position(candidate.trace_position),"decision_position":candidate.decision_position.map(position),"operands":operands,"checks":checks,"decision":candidate.decision,"evidence":candidate.evidence}));
        }
        // Validate the known numeric fields even though their exact strings are
        // conservatively retained (e.g. a different spelling misses grouping).
        for key in [
            "schema", "pass", "cls", "count", "physical", "first", "last", "special",
        ] {
            decimal(&pass.header, key)?;
        }
        normalized_passes.push(json!({"header":header,"selected_function":function_binding,"begin":position(pass.begin_position),"end":pass.end_position.map(position),"ended":pass.ended,"decisions":pass.decisions,"candidates":candidates}));
    }
    let merges=g.merges.iter().map(|m|Ok::<Value, anyhow::Error>(json!({"pass":pass_ids.get(&m.pass).context("merge lacks a normalized definition-state pass")?,"candidate":m.candidate,"position":position(m.trace_position),"parent":m.parent,"child":m.child,"parent_values":m.parent_values.iter().map(|v|(v.function==g.function,v.sequence)).collect::<Vec<_>>(),"child_values":m.child_values.iter().map(|v|(v.function==g.function,v.sequence)).collect::<Vec<_>>(),"binding":m.binding}))).collect::<Result<Vec<_>>>()?;
    Ok(
        json!({"before":before,"after":after,"graph_instruction_ids":graph_instruction_ids,"block_flags":block_flags,"function_begin":position(function.begin_position),"function_end":function.end_position.map(position),
        "births":births,
        "coverage":{"ended":function.ended,"expected_graphs":function.expected_graphs,"expected_ranges":function.expected_ranges,"counter":function.counter_observations,"ranges":function.range_observations,"materialization_scope":function.materialization_scope,"expected_materializations":function.expected_materializations,"materializations":function.materialization_observations,"array_scope":function.array_promotion_scope,"expected_arrays":function.expected_array_promotions,"arrays":function.array_promotion_observations,"call_scope":function.call_result_scope,"expected_calls":function.expected_call_results,"calls":function.call_result_observations,"dispatch_scope":function.dispatch_scope,"expected_dispatches":function.expected_dispatches,"expected_dispatch_returns":function.expected_dispatch_returns,"dispatches":function.dispatch_observations},
        "ranges":function.ranges.iter().map(|r|json!({"position":position(r.trace_position),"birth_cutoff":r.birth_cutoff,"range":r.range})).collect::<Vec<_>>(),
        "counter_changes":function.changes.iter().map(|c|json!({"position":position(c.trace_position),"kind":c.kind,"class":c.class,"old":c.old,"after":c.after,"physical":c.physical_limit,"site":c.site,"phase":c.phase})).collect::<Vec<_>>(),
        "graphs":graphs.iter().map(|other|json!({"selected":other.id==g.id,"birth_cutoff":other.birth_cutoff,"begin":position(other.begin_position),"end":other.end_position.map(position)})).collect::<Vec<_>>(),
        "passes":normalized_passes,"merges":merges}),
    )
}
