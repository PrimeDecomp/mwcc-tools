//! Argument evidence and its equality/order relationships to backend history.
use crate::{graph_history::Graph, provenance::Report, runner::Compilation, trace};
use anyhow::{Context, Result, bail, ensure};
use serde_json::{Map, Value, json};
use std::collections::BTreeMap;

#[derive(Default)]
struct Names {
    pointers: BTreeMap<u32, u32>,
    calls: BTreeMap<u32, u32>,
    episodes: BTreeMap<u32, u32>,
}
fn id(map: &mut BTreeMap<u32, u32>, raw: u32) -> u32 {
    super::ordinal(map, raw)
}
impl Names {
    fn pointer(&mut self, raw: &str) -> Result<Value> {
        Ok(json!(id(&mut self.pointers, u32::from_str_radix(raw, 16)?)))
    }
    fn operands(&mut self, instruction: &trace::Instruction) -> Result<Value> {
        // The existing instruction normalizer validates supported operand
        // tags. Keep raw registers here: the whole-function birth stream also
        // retains them. Only live pointer fields are alpha-renamed.
        super::Names::default().instruction(instruction)?;
        let mut operands = vec![];
        for operand in &instruction.operands {
            let mut raw = operand.raw;
            let offsets: &[usize] = match raw[0] {
                0 | 2 | 3 => &[6],
                4 => &[2, 6],
                7 => &[4, 8],
                1 | 6 => &[],
                _ => bail!("unsupported argument-state PCode operand"),
            };
            for at in offsets {
                let p = u32::from_le_bytes(raw[*at..*at + 4].try_into()?);
                raw[*at..*at + 4].copy_from_slice(&id(&mut self.pointers, p).to_le_bytes());
            }
            operands.push(raw);
        }
        Ok(json!(operands))
    }
    fn record(&mut self, record: &trace::Record) -> Result<Value> {
        let allowed: &[&str] = match record.kind.as_str() {
            "pv.call.begin" => &[
                "schema",
                "function",
                "function_object",
                "call",
                "parent_call",
                "parent_expression",
                "item",
                "kind",
                "callee_node",
                "callee_kind",
                "callee_object",
                "callee_known",
                "callee_complete",
                "callee_hex",
                "return_type",
                "type_kind",
                "type_size",
                "output",
                "scope",
                "ordinal_kind",
            ],
            "pv.argument.bind" => &[
                "schema",
                "function",
                "call",
                "ordinal",
                "list",
                "context",
                "node",
                "kind",
                "type",
                "type_kind",
                "type_size",
                "hascall",
                "flags",
                "gpr",
                "second_gpr",
                "fpr",
                "vector",
                "stack_offset",
                "evaluated",
            ],
            "pv.argument.begin" => &[
                "schema",
                "function",
                "call",
                "ordinal",
                "episode",
                "parent_episode",
                "stage",
                "context",
                "node",
                "operand",
                "births_before",
                "block",
                "tail",
            ],
            "pv.argument.operand" => &[
                "schema",
                "function",
                "call",
                "ordinal",
                "episode",
                "stage",
                "operand",
                "kind",
                "cls",
                "register",
                "second",
                "index",
                "object",
                "displacement",
                "immediate",
            ],
            "pv.argument.birth" => &[
                "schema", "function", "call", "ordinal", "episode", "value", "cls", "register",
            ],
            "pv.argument.instruction" => &[
                "schema", "function", "call", "ordinal", "episode", "index", "p", "op", "flags",
                "n", "line", "ops",
            ],
            "pv.argument.end" => &[
                "schema",
                "function",
                "call",
                "ordinal",
                "episode",
                "births_after",
                "block",
                "tail",
                "instructions",
                "pcode_complete",
            ],
            "pv.call.end" => &[
                "schema",
                "function",
                "call",
                "arguments",
                "episodes",
                "births",
                "operands",
                "instructions",
                "complete",
            ],
            "pv.argument.gap" => &["schema", "function", "call", "ordinal", "episode", "reason"],
            _ => bail!("unknown call-argument state record"),
        };
        ensure!(
            record.fields.keys().all(|k| allowed.contains(&k.as_str())),
            "unknown call-argument state field"
        );
        ensure!(
            record.fields.get("schema").is_none_or(|v| v == "1"),
            "unsupported call-argument state schema"
        );
        if record.kind == "pv.call.begin" {
            ensure!(
                record.get("scope")? == "guarded_regular_call_arguments_v1"
                    && record.get("ordinal_kind")? == "lowered_argument_context_order",
                "unsupported call-argument state scope or ordinal semantics"
            );
        }
        let instruction = (record.kind == "pv.argument.instruction")
            .then(|| trace::instruction(record))
            .transpose()?;
        let mut fields = Map::new();
        for (key, text) in &record.fields {
            let value = match key.as_str() {
                "function" => json!("selected"),
                "call" | "parent_call" => json!(id(&mut self.calls, text.parse()?)),
                "episode" | "parent_episode" => json!(id(&mut self.episodes, text.parse()?)),
                "function_object" | "item" | "callee_node" | "callee_object" | "return_type"
                | "output" | "list" | "context" | "node" | "type" | "operand" | "block"
                | "tail" | "object" | "p" => self.pointer(text)?,
                "ops" => self.operands(
                    instruction
                        .as_ref()
                        .context("ops outside argument instruction")?,
                )?,
                _ => json!(text),
            };
            fields.insert(key.clone(), value);
        }
        Ok(json!({"kind":record.kind,"fields":fields}))
    }
}
fn event(timeline: &mut BTreeMap<usize, Vec<Value>>, position: usize, value: Value) {
    timeline.entry(position).or_default().push(value);
}

pub(super) fn normalize(g: &Graph, report: &Report, compilation: &Compilation) -> Result<Value> {
    if !report
        .call_arguments
        .functions
        .iter()
        .any(|f| f.function == g.function && (f.scope.is_some() || f.end_scope.is_some()))
        && !compilation
            .trace
            .lines()
            .any(|line| trace::record_kind(line).is_some_and(crate::call_arguments::is_record))
    {
        return Ok(Value::Null);
    }
    let function = report
        .history
        .functions
        .iter()
        .find(|f| f.id == g.function)
        .context("missing argument-state function")?;
    let mut names = Names::default();
    let mut timeline = BTreeMap::new();
    // These sidecars preserve alias partitions between the argument observer
    // and the independently observed backend history. No raw address is an
    // anchor and no pointer-looking padding is interpreted.
    let function_object = function
        .function_object
        .as_deref()
        .map(|s| names.pointer(s))
        .transpose()?;
    let mut expressions = vec![];
    for expression in &function.expressions {
        event(
            &mut timeline,
            expression.trace_position,
            json!(["expression_begin", expression.id]),
        );
        if let Some(end) = expression.end_position {
            event(&mut timeline, end, json!(["expression_end", expression.id]));
        }
        let result = expression.result.as_ref().map(|r| -> Result<Value> {
            event(&mut timeline,r.trace_position,json!(["expression_result",expression.id]));
            Ok(json!({"operand":r.operand.as_deref().map(|p|names.pointer(p)).transpose()?,"object":r.object.as_deref().map(|p|names.pointer(p)).transpose()?,
                "kind":r.kind,"class":r.class,"register":r.register,"second":r.second,"index":r.index,"displacement":r.displacement,"immediate":r.immediate,"evidence":r.evidence}))
        }).transpose()?;
        expressions.push(json!({"id":expression.id,"node":names.pointer(&expression.node)?,"parent":expression.parent,"result":result}));
    }
    let mut births = vec![];
    for birth in &function.births {
        event(
            &mut timeline,
            birth.trace_position,
            json!(["birth", birth.id.sequence]),
        );
        let object=birth.object.as_ref().map(|o| -> Result<Value> {Ok(json!({"object":names.pointer(&o.object)?,"info":names.pointer(&o.variable_info)?}))}).transpose()?;
        let materialization=birth.materialization.as_ref().map(|m| -> Result<Value> {
            event(&mut timeline,m.trace_position,json!(["materialization",birth.id.sequence]));
            Ok(json!({"operand":names.pointer(&m.operand)?,"object":names.pointer(&m.object)?,"matching_dispatch_output":m.matching_dispatch_output}))
        }).transpose()?;
        let array=birth.array_promotion.as_ref().map(|a| -> Result<Value> {
            event(&mut timeline,a.trace_position,json!(["array_promotion",birth.id.sequence]));
            Ok(json!({"promotion":names.pointer(&a.promotion)?,"object":names.pointer(&a.object)?}))
        }).transpose()?;
        let call=birth.call_result.as_ref().map(|c| -> Result<Value> {
            event(&mut timeline,c.trace_position,json!(["call_result",birth.id.sequence]));
            // These explicit pointers identify observed inputs/output storage;
            // no partially initialized output bytes or output tag are read.
            Ok(json!({"item":names.pointer(&c.item)?,"callee":names.pointer(&c.callee)?,"return_type":names.pointer(&c.return_type)?,"output":names.pointer(&c.output)?}))
        }).transpose()?;
        births.push(json!({"id":birth.id.sequence,"object":object,"materialization":materialization,"array":array,"call":call}));
    }
    let graph_instructions = g
        .instructions
        .iter()
        .map(|i| -> Result<Value> {
            Ok(json!({"pointer":names.pointer(&i.pointer)?,"operands":names.operands(i)?}))
        })
        .collect::<Result<Vec<_>>>()?;
    for graph in report
        .graphs
        .graphs
        .iter()
        .filter(|other| other.function == g.function)
    {
        event(
            &mut timeline,
            graph.begin_position,
            json!(["graph_begin", graph.class, graph.id == g.id]),
        );
        if let Some(end) = graph.end_position {
            event(
                &mut timeline,
                end,
                json!(["graph_end", graph.class, graph.id == g.id]),
            );
        }
    }
    let mut capability = Map::new();
    let mut totals = Map::new();
    let mut records = vec![];
    for (index, line) in compilation.trace.lines().enumerate() {
        let Some(kind) = trace::record_kind(line) else {
            continue;
        };
        if !kind.starts_with("pv.call.")
            && !kind.starts_with("pv.argument.")
            && !matches!(kind, "pv.function.begin" | "pv.function.end")
        {
            continue;
        }
        let mut record = trace::records(line)
            .pop()
            .context("missing argument-state record")?;
        if record.number::<u32>("function")? != function.id {
            continue;
        }
        record.line = index + 1;
        if matches!(kind, "pv.function.begin" | "pv.function.end") {
            let fields = if kind == "pv.function.begin" {
                &mut capability
            } else {
                &mut totals
            };
            for (key, value) in &record.fields {
                if key == "call_arguments" || key.starts_with("argument_") || key == "call_count" {
                    ensure!(
                        [
                            "call_arguments",
                            "call_count",
                            "argument_count",
                            "argument_episodes",
                            "argument_births",
                            "argument_operands",
                            "argument_instructions"
                        ]
                        .contains(&key.as_str()),
                        "unknown argument-state function summary field"
                    );
                    fields.insert(key.clone(), json!(value));
                }
            }
            event(&mut timeline, index + 1, json!([kind]));
        } else {
            event(
                &mut timeline,
                index + 1,
                json!(["argument_record", records.len()]),
            );
            records.push(names.record(&record)?);
        }
    }
    for fields in [&capability, &totals] {
        ensure!(
            fields
                .get("call_arguments")
                .is_none_or(|v| v == "guarded_regular_call_arguments_v1" || v == "unknown"),
            "unsupported argument-state function capability"
        );
    }
    let summary = report
        .call_arguments
        .functions
        .iter()
        .find(|f| f.function == g.function)
        .map(|f| json!({"observations":f.observations,"event_totals":f.event_totals}));
    let calls=report.call_arguments.calls.iter().filter(|c|c.function==g.function).map(|c| {
        json!({"id":id(&mut names.calls,c.id),"observations":c.observations,"episodes":c.episodes.iter().map(|e|json!({"id":id(&mut names.episodes,e.id),"dispatch_expression":e.dispatch_expression,"observations":e.observations})).collect::<Vec<_>>()})
    }).collect::<Vec<_>>();
    Ok(
        json!({"capability":capability,"totals":totals,"function_object":function_object,"records":records,"expressions":expressions,"births":births,"graph_instructions":graph_instructions,"coverage":summary,"calls":calls,"timeline":timeline.into_values().collect::<Vec<_>>()}),
    )
}
