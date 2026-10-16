use crate::trace::Record;
use anyhow::{Context, Result};
use serde_json::{Value, json};
use std::collections::BTreeMap;

fn integer(record: &Record, key: &str, default: i64) -> Result<i64> {
    record.fields.get(key).map_or(Ok(default), |value| {
        value
            .parse()
            .with_context(|| format!("trace line {}: invalid {key}", record.line))
    })
}
fn calls(text: Option<&String>) -> Vec<(String, String)> {
    text.filter(|s| !s.is_empty()).map_or_else(Vec::new, |s| {
        s.split('|')
            .map(|s| {
                if s.starts_with(['!', '?']) {
                    (s[..1].into(), s[1..].into())
                } else {
                    (String::new(), s.into())
                }
            })
            .collect()
    })
}

pub fn body_costs(events: &[Record]) -> Result<BTreeMap<String, i64>> {
    let mut output: BTreeMap<String, i64> = BTreeMap::new();
    let mut pending: BTreeMap<i64, Vec<i64>> = BTreeMap::new();
    for record in events {
        let nest = integer(record, "nest", 0)?;
        if record.kind == "inl.cost" && nest > 0 {
            pending
                .entry(nest)
                .or_default()
                .push(integer(record, "cost", 0)? - integer(record, "running", 0)?);
        } else if record.kind == "inl.expr" {
            let costs = pending.remove(&(nest + 1)).unwrap_or_default();
            for ((_, link), cost) in calls(record.fields.get("calls"))
                .into_iter()
                .filter(|(mark, _)| mark.is_empty())
                .zip(costs)
            {
                output
                    .entry(link)
                    .and_modify(|c| *c = (*c).min(cost))
                    .or_insert(cost);
            }
        }
    }
    Ok(output)
}

fn cost_terms(
    nodes: &[Value],
    above: &[String],
    out: &mut Vec<(Vec<String>, String, Option<i64>)>,
) {
    for node in nodes {
        let parts = node["parts"].as_array().map(Vec::as_slice).unwrap_or(&[]);
        if node["what"] == "body" {
            let mut path = above.to_vec();
            if let Some(link) = node["of"].as_str() {
                path.push(link.into());
                out.push((path.clone(), "body".into(), node["cost"].as_i64()));
            }
            cost_terms(parts, &path, out);
        } else {
            if let Some(calls) = node["calls"].as_array() {
                for call in calls {
                    let mark = call[0].as_str().unwrap_or("");
                    if !mark.is_empty() {
                        let mut path = above.to_vec();
                        path.push(call[1].as_str().unwrap_or("?").into());
                        out.push((
                            path,
                            if mark == "!" { "limit" } else { "no body" }.into(),
                            None,
                        ));
                    }
                }
            }
            cost_terms(parts, above, out);
        }
    }
}

fn sensitivity(
    record: &Value,
    terms: &[(Vec<String>, String, Option<i64>)],
    bodies: &BTreeMap<String, i64>,
) -> Vec<Value> {
    let mut out: Vec<Value> = vec![];
    for (path, kind, cost) in terms {
        let delta = if kind == "body" {
            cost.map(|c| 1 - c)
        } else if kind == "no body" {
            path.last().and_then(|p| bodies.get(p)).map(|c| c - 1)
        } else {
            None
        };
        let Some(delta) = delta else {
            continue;
        };
        let threshold = record["threshold"].as_i64().unwrap_or(0);
        let flips = if record["inline"] == true {
            record["cost"].as_i64().unwrap_or(0) + delta > threshold
        } else {
            record["depth_limited"] != true
                && record["full_cost"].as_i64().unwrap_or(0) + delta <= threshold
        };
        if flips || delta.abs() <= 2 {
            if let Some(existing) = out
                .iter_mut()
                .find(|v| v["path"] == json!(path) && v["kind"] == *kind && v["delta"] == delta)
            {
                existing["count"] = json!(existing["count"].as_u64().unwrap() + 1);
            } else {
                out.push(
                    json!({"path": path, "kind": kind, "delta": delta, "flips": flips, "count": 1}),
                );
            }
        }
    }
    out.sort_by_key(|s| (s["flips"] != true, s["delta"].as_i64().unwrap().abs()));
    out
}

fn add(
    mut record: Value,
    decisions: &mut Vec<Value>,
    seen: &mut BTreeMap<String, usize>,
) -> Result<()> {
    let object = record.as_object_mut().unwrap();
    let depth = object.remove("depth");
    let line = object.remove("line");
    let breakdown = object.remove("breakdown");
    let terms = object.remove("sensitivity");
    let key = serde_json::to_string(&object.iter().collect::<BTreeMap<_, _>>())?;
    let index = if let Some(index) = seen.get(&key) {
        *index
    } else {
        let index = decisions.len();
        seen.insert(key, index);
        if let Some(b) = breakdown {
            object.insert("breakdown".into(), b);
        }
        if let Some(t) = terms {
            object.insert("sensitivity".into(), t);
        }
        object.insert("count".into(), json!(0));
        decisions.push(record);
        index
    };
    let d = &mut decisions[index];
    d["count"] = json!(d["count"].as_u64().unwrap() + 1);
    for (field, value) in [("depths", depth), ("lines", line)] {
        if let Some(v) = value {
            if d.get(field).is_none() {
                d[field] = json!([]);
            }
            let values = d[field].as_array_mut().unwrap();
            if !values.contains(&v) {
                values.push(v);
            }
        }
    }
    Ok(())
}

pub fn decisions(events: &[Record], bodies: &BTreeMap<String, i64>) -> Result<Vec<Value>> {
    let mut output = vec![];
    let mut seen = BTreeMap::new();
    let mut pending: BTreeMap<i64, Vec<Value>> = BTreeMap::new();
    let mut begins: Vec<&Record> = vec![];
    let mut cost: Option<(Record, Option<Value>)> = None;
    for r in events {
        match r.kind.as_str() {
            "inl.expr" => {
                let nest = integer(r, "nest", 0)?;
                let mut parts = pending.remove(&(nest + 1)).unwrap_or_default();
                let calls = calls(r.fields.get("calls"));
                for (part, (_, link)) in parts
                    .iter_mut()
                    .filter(|p| p["what"] == "body")
                    .zip(calls.iter().filter(|(m, _)| m.is_empty()))
                {
                    part["of"] = json!(link);
                }
                let mut node = json!({"kind":"breakdown", "what":r.fields.get("what").map_or("?",String::as_str), "type":integer(r,"type",-1)?,
                    "cost":integer(r,"cost",0)?, "parts":parts, "calls":calls});
                if let Some(link) = r.fields.get("link") {
                    let flags =
                        u32::from_str_radix(r.fields.get("flags").map_or("0", String::as_str), 16)?;
                    node["link"] = json!(link);
                    node["implicit"] = json!(flags & 0x100 != 0);
                    node["ctor"] = json!(flags & 0x1000 != 0);
                    node["dtor"] = json!(flags & 0x2000 != 0);
                    node["candidate"] = json!(r.fields.get("cand").is_some_and(|s| s == "1"));
                    if r.fields.get("limit").is_some_and(|s| s == "1") {
                        node["depth_limit"] = json!(true);
                    }
                }
                pending.entry(nest).or_default().push(node);
            }
            "inl.cost" => {
                let nest = integer(r, "nest", 0)?;
                let mut statements = pending.remove(&nest).unwrap_or_default();
                if nest > 0 {
                    let split = statements
                        .iter()
                        .rposition(|p| p["what"] == "body")
                        .map_or(0, |n| n + 1);
                    let body = statements.split_off(split);
                    statements.push(json!({"kind":"breakdown","what":"body","type":-1,"cost":integer(r,"cost",0)?-integer(r,"running",0)?, "nstmts":integer(r,"nstmts",0)?,"parts":body}));
                    pending.insert(nest, statements);
                    continue;
                }
                let breakdown = if !statements.is_empty() || r.fields.contains_key("nest") {
                    Some(
                        json!({"running":integer(r,"running",0)?,"nstmts":integer(r,"nstmts",0)?,"statements":statements}),
                    )
                } else {
                    None
                };
                cost = Some((r.clone(), breakdown));
            }
            "inl.can" => {
                let f = &r.fields;
                let name = f.get("function").map_or("?", String::as_str);
                let result = f.get("result").is_some_and(|s| s == "1");
                let is = |key: &str, v: &str| f.get(key).is_some_and(|s| s == v);
                let chain: Vec<_> = f
                    .get("chain")
                    .filter(|s| !s.is_empty())
                    .map_or_else(Vec::new, |s| s.split('|').collect());
                let mut record = json!({"kind":"decision","event":"decision","callee":name,"link":f.get("link").map_or(name,String::as_str),"inline":result,"depth":integer(r,"depth",0)?,"chain":chain});
                let line = integer(r, "line", -1)?;
                if line > 0 {
                    record["line"] = json!(line);
                }
                let reason = if is("inline_bit", "0") && !result {
                    "not declared inline (and not a method defined in its class)".into()
                } else if result && is("always", "1") {
                    "inline always (-inline all): every candidate is accepted".into()
                } else if is("body", "0") {
                    "no body is available at this point (declared later, or never defined here)"
                        .into()
                } else if is("auto", "1") {
                    if let Some((c, breakdown)) = cost.as_ref().filter(|_| !is("inline_bit", "0")) {
                        let estimate = integer(c, "cost", 0)?;
                        let threshold = integer(c, "threshold", 0)?;
                        record["cost"] = json!(estimate);
                        record["threshold"] = json!(threshold);
                        let full = c.fields.contains_key("full");
                        if full {
                            record["full_cost"] = json!(integer(c, "full", 0)?);
                            record["depth_limited"] =
                                json!(c.fields.get("limit").is_some_and(|s| s == "1"));
                        }
                        if let Some(breakdown) = breakdown
                            .as_ref()
                            .filter(|b| b["statements"].as_array().is_some_and(|s| !s.is_empty()))
                        {
                            record["breakdown"] = breakdown.clone();
                            let mut terms = vec![];
                            cost_terms(
                                breakdown["statements"].as_array().unwrap(),
                                &[],
                                &mut terms,
                            );
                            let mut paths: Vec<Vec<String>> = vec![];
                            for (p, k, _) in &terms {
                                if k == "limit" && !paths.contains(p) {
                                    paths.push(p.clone());
                                }
                            }
                            if !paths.is_empty() {
                                record["limit_paths"] = json!(paths);
                            }
                            if full {
                                let terms = sensitivity(&record, &terms, bodies);
                                if !terms.is_empty() {
                                    record["sensitivity"] = json!(terms);
                                }
                            }
                        }
                        let full_cost = record["full_cost"].as_i64();
                        let mut reason = format!(
                            "estimated cost {estimate} {} the threshold {threshold}",
                            if result { "is within" } else { "exceeds" }
                        );
                        if record["depth_limited"] == true
                            && full_cost.is_some_and(|c| c <= threshold)
                        {
                            reason = format!(
                                "a call in it is at the inline depth limit, which makes the estimate {estimate} (the threshold {threshold} plus one, and what follows); without it the whole cost is {}, within the threshold",
                                full_cost.unwrap()
                            );
                        } else if let Some(full_cost) = full_cost.filter(|c| *c != estimate) {
                            reason += &format!(" (whole cost {full_cost}");
                            if record["depth_limited"] == true {
                                reason += ", without the calls at the inline depth limit, which also keep it";
                            }
                            reason += ")";
                        }
                        if result && integer(r, "nstmts", 0)? > 0 {
                            reason += &format!(" ({} statements)", integer(r, "nstmts", 0)?);
                        }
                        reason
                    } else {
                        "auto-inline decision".into()
                    }
                } else {
                    format!(
                        "depth {} with {} statements ({} the limit for this depth)",
                        f.get("depth").map_or("None", String::as_str),
                        f.get("nstmts").map_or("None", String::as_str),
                        if result { "within" } else { "over" }
                    )
                };
                record["reason"] = json!(reason);
                record["outcome"] = json!(if result {
                    "inlined"
                } else if record["depth_limited"] == true
                    && record["full_cost"].as_i64() <= record["threshold"].as_i64()
                {
                    "limit"
                } else {
                    "kept"
                });
                add(record, &mut output, &mut seen)?;
                cost = None;
            }
            "inl.begin" => begins.push(r),
            "inl.end" => {
                let name = r.fields.get("function").map_or("?", String::as_str);
                let mut record = json!({"kind":"expansion","event":"expansion","callee":name,"expanded":r.fields.get("expanded").is_none_or(|s|s!="0")});
                if let Some(begin) = begins.pop() {
                    record["link"] = json!(begin.fields.get("link").map_or(name, String::as_str));
                    let line = integer(begin, "line", -1)?;
                    if line > 0 {
                        record["line"] = json!(line);
                    }
                }
                add(record, &mut output, &mut seen)?;
            }
            _ => (),
        }
    }
    Ok(output)
}
