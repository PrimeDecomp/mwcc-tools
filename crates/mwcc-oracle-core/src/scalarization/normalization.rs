//! Capture-independent scalarization facts for whole-function state comparison.
use super::{History, Pass};
use crate::{trace::Record, value_history::Function};
use anyhow::{Context, Result, bail, ensure};
use serde_json::{Map, Value, json};
use std::collections::BTreeMap;

#[derive(Default)]
struct Names {
    pointers: BTreeMap<u32, u32>,
    functions: BTreeMap<u32, u32>,
    passes: BTreeMap<u32, u32>,
    bindings: BTreeMap<(u32, u32), u32>,
    values: BTreeMap<(u32, u32), u32>,
    linear_indices: BTreeMap<u32, u32>,
}
fn ordinal<K: Ord>(names: &mut BTreeMap<K, u32>, key: K) -> u32 {
    let next = names.len() as u32 + 1;
    *names.entry(key).or_insert(next)
}
impl Names {
    fn pointer(&mut self, text: &str) -> Result<Value> {
        let raw = u32::from_str_radix(text.strip_prefix("0x").unwrap_or(text), 16)
            .context("invalid scalarization pointer during normalization")?;
        Ok(if raw == 0 {
            Value::Null
        } else {
            json!(ordinal(&mut self.pointers, raw))
        })
    }
    fn function(&mut self, id: Option<u32>, function: &Function) -> Value {
        match id {
            None => Value::Null,
            Some(0) => json!({"missing": true}),
            Some(id) if id == function.id => json!({"selected": true}),
            Some(id) => json!({"other": ordinal(&mut self.functions, id)}),
        }
    }
    fn pass(&mut self, id: u32, passes: &[&Pass]) -> Value {
        let matches: Vec<_> = passes
            .iter()
            .enumerate()
            .filter_map(|(i, pass)| (pass.id == id).then_some(i))
            .collect();
        if !matches.is_empty() {
            json!({"matches": matches})
        } else if id == 0 {
            json!({"missing": true})
        } else {
            json!({"unmatched": ordinal(&mut self.passes, id)})
        }
    }
    fn binding(&mut self, pass: Option<u32>, binding: u32, passes: &[&Pass]) -> Value {
        let matches: Vec<_> = passes
            .iter()
            .enumerate()
            .filter(|(_, p)| Some(p.id) == pass)
            .flat_map(|(i, p)| {
                p.bindings
                    .iter()
                    .enumerate()
                    .filter_map(move |(j, b)| (b.ordinal == binding).then_some([i, j]))
            })
            .collect();
        if matches.is_empty() {
            json!({"unmatched": ordinal(&mut self.bindings, (pass.unwrap_or(0), binding))})
        } else {
            json!({"matches": matches})
        }
    }
    fn gap(&mut self, record: &Record, passes: &[&Pass], function: &Function) -> Result<Value> {
        ensure!(
            record.kind == "pv.scalar.gap" && record.number::<u32>("schema")? == 1,
            "unsupported scalarization gap record during normalization"
        );
        let mut fields = Map::new();
        for (key, text) in &record.fields {
            let value = match key.as_str() {
                "function" => self.function(Some(text.parse()?), function),
                "pass" => self.pass(text.parse()?, passes),
                "function_object" => self.pointer(text)?,
                "site" => self.site(u32::from_str_radix(text, 16)?)?,
                "schema" | "reason" => json!(text),
                _ => bail!("unsupported scalarization gap field {key} during normalization"),
            };
            fields.insert(key.clone(), value);
        }
        Ok(json!({"kind": record.kind, "fields": fields}))
    }
    fn site(&mut self, site: u32) -> Result<Value> {
        Ok(match site {
            0 => Value::Null,
            0x4608a8 | 0x460b28 | 0x461748 => json!("guarded_create_temp_return"),
            _ => bail!("unsupported scalarization site during normalization"),
        })
    }
}
fn pointer_equal(a: &str, b: &str) -> bool {
    matches!((u32::from_str_radix(a, 16), u32::from_str_radix(b, 16)),
        (Ok(a), Ok(b)) if a != 0 && a == b)
}
fn relevant(pass: &Pass, function: &Function) -> bool {
    pass.function == Some(function.id)
        || function.function_object.as_ref().is_some_and(|object| {
            pointer_equal(&pass.function_object, object)
                && pass
                    .end_position
                    .is_none_or(|end| end >= function.begin_position)
                && function
                    .end_position
                    .is_none_or(|end| pass.begin_position <= end)
        })
}
fn event(timeline: &mut BTreeMap<usize, Vec<Value>>, position: usize, value: Value) {
    timeline.entry(position).or_default().push(value);
}

/// Preserve all function-scoped creation evidence, including unbound temporaries
/// and ambiguous joins. Pointer/ID spellings and absolute trace positions never
/// appear in the result. A relative event sequence retains boundary/order ties.
pub fn normalized_function(history: &History, function: &Function) -> Result<Value> {
    let passes: Vec<_> = history
        .passes
        .iter()
        .filter(|p| relevant(p, function))
        .collect();
    let mut names = Names::default();
    let function_object = function
        .function_object
        .as_ref()
        .map(|p| names.pointer(p))
        .transpose()?;
    let mut timeline = BTreeMap::new();
    event(
        &mut timeline,
        function.begin_position,
        json!(["function_begin"]),
    );
    if let Some(end) = function.end_position {
        event(&mut timeline, end, json!(["function_end"]));
    }
    // Birth order/identity stays meaningful even when no graph node retains it.
    for birth in &function.births {
        event(
            &mut timeline,
            birth.trace_position,
            json!(["birth", birth.class, birth.register, birth.generation]),
        );
    }
    let mut normalized_passes = vec![];
    for (i, pass) in passes.iter().enumerate() {
        ensure!(
            pass.schema == 1 && pass.scope == super::SCOPE,
            "unsupported scalarization pass during normalization"
        );
        event(&mut timeline, pass.begin_position, json!(["pass_begin", i]));
        if let Some(end) = pass.end_position {
            event(&mut timeline, end, json!(["pass_end", i]));
        }
        let pass_object = names.pointer(&pass.function_object)?;
        let pass_function = names.function(pass.function, function);
        let mut bindings = vec![];
        for (j, binding) in pass.bindings.iter().enumerate() {
            event(
                &mut timeline,
                binding.trace_position,
                json!(["binding", i, j]),
            );
            let source = &binding.source;
            let temporary = &binding.temporary;
            bindings.push(json!({
                "ordinal_matches_order": binding.ordinal as usize == j + 1,
                "site": names.site(binding.site)?,
                "part": names.pointer(&binding.part)?,
                "source_linear": names.pointer(&binding.source_linear)?,
                "source_index": ordinal(&mut names.linear_indices, binding.source_index),
                "source_kind": binding.source_kind,
                "variable": names.pointer(&binding.variable)?,
                "source": {
                    "object": names.pointer(&source.object)?,
                    "variable_info": names.pointer(&source.variable_info)?,
                    "type": names.pointer(&source.type_pointer)?,
                    "type_kind": source.type_kind, "type_size": source.type_size,
                    "datatype": source.datatype, "storage": source.storage,
                    "name_known": source.name_known, "name_complete": source.name_complete,
                    "name_bytes": source.name_bytes,
                    "element_known": source.element_known,
                    "element_type_kind": source.element_type_kind,
                    "element_type_size": source.element_type_size
                },
                "interval": [binding.byte_offset, binding.byte_size, binding.bit_first, binding.bit_last],
                "flags": binding.flags & 3,
                "temporary": {
                    "object": names.pointer(&temporary.object)?,
                    "variable_info": names.pointer(&temporary.variable_info)?,
                    "type": names.pointer(&temporary.type_pointer)?,
                    "type_kind": temporary.type_kind, "type_size": temporary.type_size
                }
            }));
        }
        let mut gaps = vec![];
        for (j, gap) in pass.gaps.iter().enumerate() {
            event(&mut timeline, gap.line, json!(["pass_gap", i, j]));
            gaps.push(names.gap(gap, &passes, function)?);
        }
        normalized_passes.push(json!({
            "schema": pass.schema, "scope": pass.scope,
            "function": pass_function, "function_object": pass_object,
            "link": pass.link, "link_matches_function": pass.link == function.link,
            "expected_bindings": pass.expected_bindings,
            "observations": pass.observations, "bindings": bindings, "gaps": gaps
        }));
    }
    let summary = function.scalarization.as_ref().map(|summary| {
        json!({
            "scope": summary.scope, "passes": summary.passes, "bindings": summary.bindings,
            "first_pass": names.pass(summary.first_pass, &passes),
            "last_pass": names.pass(summary.last_pass, &passes)
        })
    });
    let mut backend = vec![];
    for value in history.backend_values.iter().filter(|value| {
        value.value.function == function.id
            || value
                .pass
                .is_some_and(|id| passes.iter().any(|p| p.id == id))
    }) {
        let mut births = vec![];
        for birth in function
            .births
            .iter()
            .filter(|birth| birth.id == value.value)
        {
            let object = birth
                .object
                .as_ref()
                .map(|object| -> Result<Value> {
                    Ok(json!({
                        "object": names.pointer(&object.object)?,
                        "variable_info": names.pointer(&object.variable_info)?,
                        "part": object.part, "type_kind": object.type_kind,
                        "type_size": object.type_size, "datatype": object.datatype,
                        "storage": object.storage, "flags_before": object.flags_before,
                        "name_known": object.name_known, "name_complete": object.name_complete,
                        "name_bytes": object.name_bytes
                    }))
                })
                .transpose()?;
            births.push(json!({"class": birth.class, "register": birth.register,
                "generation": birth.generation, "object": object}));
        }
        let unresolved = births.is_empty().then(|| {
            ordinal(
                &mut names.values,
                (value.value.function, value.value.sequence),
            )
        });
        backend.push(json!({
            "function": names.function(Some(value.value.function), function),
            "births": births, "unresolved_value": unresolved,
            "class": value.class, "register": value.register,
            "pass": value.pass.map(|id| names.pass(id, &passes)),
            "binding": value.binding.map(|id| names.binding(value.pass, id, &passes)),
            "array_element": value.array_element, "observations": value.observations
        }));
    }
    let mut gaps = vec![];
    for (i, gap) in history.gaps.iter().enumerate() {
        event(&mut timeline, gap.line, json!(["unscoped_gap", i]));
        gaps.push(names.gap(gap, &passes, function)?);
    }
    Ok(json!({
        "format_version": 1, "history_format_version": history.format_version,
        "function_object": function_object, "function_ended": function.ended,
        "counter_observations": function.counter_observations,
        "summary": summary, "observations": history.observations,
        "passes": normalized_passes, "backend_values": backend, "gaps": gaps,
        "timeline": timeline.into_values().collect::<Vec<_>>()
    }))
}
