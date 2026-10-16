use crate::{
    SCHEMA_VERSION, allocation, inlining, locals, schedule,
    trace::Trace,
    version::{self, Version},
};
use anyhow::Result;
use serde_json::{Value, json};

pub fn instantiation(trace: &Trace, version: Version) -> Result<Vec<Value>> {
    let mut events: Vec<Value> = vec![];
    for r in &trace.records {
        let position = r.line - 1;
        if r.kind.starts_with("tp.") && r.kind != "tp.parse.begin" {
            let event = r.kind.trim_start_matches("tp.");
            let mut e = json!({"kind":"instantiation","event":event,"link":r.fields.get("link").map_or("?",String::as_str),"position":position});
            for field in ["from", "expanding"] {
                if let Some(v) = r.fields.get(field).filter(|v| v.as_str() != "-") {
                    e[field] = json!(v);
                }
            }
            if matches!(event, "queue" | "parse") {
                e["queue_kind"] = json!(r.fields.get("kind").map_or(Ok(0), |v| v.parse::<i64>())?);
            }
            for field in ["new", "body", "result"] {
                if let Some(v) = r.fields.get(field) {
                    e[field] = json!(v == "1");
                }
            }
            if let Some(caller) = r.fields.get("caller") {
                let name = version::function_at(version, u32::from_str_radix(caller, 16)?);
                let caller = name.map_or_else(|| format!("0x{caller}"), str::to_owned);
                e["caller"] = json!(caller);
                e["why"]=json!(match name {
                    Some("fn_005114e0")=>"a call or address of it left in the code after inlining".into(),
                    Some("CExcept_CheckStackRefs")=>"the destructor of an object live in a statement (its exception actions), even when every call of it was inlined".into(),
                    Some("emit_object")=>"referenced by data emitted (a vtable, an initializer)".into(),
                    _=>format!("referenced by {caller}")});
            }
            events.push(e);
        } else if r.kind == "inl.can" {
            let chain: Vec<_> = r
                .fields
                .get("chain")
                .filter(|s| !s.is_empty())
                .map_or_else(Vec::new, |s| s.split('|').collect());
            events.push(json!({"kind":"instantiation","event":"decision","link":r.fields.get("link").map_or("?",String::as_str),"position":position,
                "inline":r.fields.get("result").is_some_and(|s|s=="1"),"body":r.fields.get("body").is_some_and(|s|s=="1"),
                "depth":r.fields.get("depth").map_or(Ok(0),|v|v.parse::<i64>())?,"chain":chain}));
        } else if r.kind == "ra.before" {
            let link = r
                .fields
                .get("link")
                .filter(|s| !s.is_empty())
                .or_else(|| r.fields.get("function"))
                .map_or("?", String::as_str);
            for e in events.iter_mut().rev() {
                if e["event"] == "compiled" {
                    break;
                }
                if e["event"] == "decision" {
                    e["in"] = json!(link);
                }
            }
            events.push(
                json!({"kind":"instantiation","event":"compiled","link":link,"position":position}),
            );
        }
    }
    Ok(events)
}

#[derive(Clone, Debug, Default)]
pub struct Selection<'a> {
    pub function: Option<&'a str>,
    pub link: Option<&'a str>,
    pub near: Option<i64>,
}

pub fn analyze(trace: &Trace, version: Version, selection: &Selection<'_>) -> Result<Vec<Value>> {
    let bodies = inlining::body_costs(&trace.inliner)?;
    let passes = locals::scalarize(&trace.records)?;
    let bindings = locals::bindings(&trace.records)?;
    let has_locals = trace.records.iter().any(|r| r.kind.starts_with("lc."));
    let mut functions = vec![];
    let mut start = 0;
    for a in &trace.allocations {
        let events = &trace.inliner[start..a.inliner_position];
        start = a.inliner_position;
        if !selection
            .function
            .is_none_or(|f| a.before.function.contains(f) || a.before.link.contains(f))
            || selection.link.is_some_and(|f| a.before.link != f)
        {
            continue;
        }
        let mut analysis = allocation::analyze(a, version);
        analysis["schema_version"] = json!(SCHEMA_VERSION);
        analysis["inlining"] = json!(inlining::decisions(events, &bodies)?);
        analysis["schedule"] = json!(schedule::analyze(
            &a.before.schedule,
            version::opcode_names(version)
        )?);
        if has_locals {
            analysis["locals"] = json!(passes.get(&a.before.link).cloned().unwrap_or_default());
            analysis["inline_bindings"] =
                json!(bindings.get(&a.before.link).cloned().unwrap_or_default());
        }
        functions.push(analysis);
    }
    Ok(functions)
}

pub fn near(functions: &[Value], after: &[Value], margin: i64) -> Vec<Value> {
    let mut output = vec![];
    for (function, link, decisions) in functions
        .iter()
        .map(|f| {
            (
                f["function"].clone(),
                f["link"].clone(),
                f["inlining"].as_array().map(Vec::as_slice).unwrap_or(&[]),
            )
        })
        .chain(std::iter::once((Value::Null, Value::Null, after)))
    {
        for d in decisions {
            if d["event"] != "decision" {
                continue;
            }
            let Some(cost) = d["cost"].as_i64() else {
                continue;
            };
            let Some(threshold) = d["threshold"].as_i64() else {
                continue;
            };
            if (cost - threshold).abs() > margin {
                continue;
            }
            let mut n = json!({"kind":"near","function":function,"function_link":link,"callee":d["callee"],"link":d["link"],"inline":d["inline"],"cost":cost,"threshold":threshold,
                "margin":threshold-cost,"count":d["count"],"lines":d.get("lines").cloned().unwrap_or(json!([])),"depths":d.get("depths").cloned().unwrap_or(json!([])),"chain":d.get("chain").cloned().unwrap_or(json!([])),
                "outcome":d["outcome"],"limit_paths":d.get("limit_paths").cloned().unwrap_or(json!([])),"sensitivity":d.get("sensitivity").cloned().unwrap_or(json!([]))});
            if let Some(full) = d["full_cost"].as_i64() {
                n["full_cost"] = json!(full);
                n["full_margin"] = json!(threshold - full);
                n["depth_limited"] = d.get("depth_limited").cloned().unwrap_or(json!(false));
            }
            output.push(n);
        }
    }
    output
}

pub fn build(
    trace: &Trace,
    version: Version,
    source: &str,
    oracle: &str,
    identical: Option<bool>,
    differences: &[String],
    selection: &Selection<'_>,
) -> Result<Value> {
    let functions = analyze(trace, version, selection)?;
    let mut document = json!({"schema_version":SCHEMA_VERSION,"oracle":oracle,"version":version.name(),"source":source,"identical":identical,"differences":differences,"functions":functions});
    let last = trace.allocations.last().map_or(0, |a| a.inliner_position);
    let after = if selection.function.is_none() && selection.link.is_none() {
        inlining::decisions(
            &trace.inliner[last..],
            &inlining::body_costs(&trace.inliner)?,
        )?
    } else {
        vec![]
    };
    if !after.is_empty() {
        document["inlining_after_last_function"] = json!(after);
    }
    if let Some(function) = selection.function.filter(|_| functions.is_empty()) {
        document["similar"] = json!(
            crate::sequence::similar_functions(trace, function, 12)
                .into_iter()
                .map(|(function, link)| json!({"function":function,"link":link}))
                .collect::<Vec<_>>()
        );
    }
    if let Some(margin) = selection.near {
        document["near"] = json!(near(&functions, &after, margin));
    }
    if trace.records.iter().any(|r| r.kind.starts_with("tp.")) {
        document["instantiation"] = json!(instantiation(trace, version)?);
    }
    Ok(document)
}
