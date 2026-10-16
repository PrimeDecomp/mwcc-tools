//! Kit inline calibration: compare per-decision costs with target instance emission.
use anyhow::{Context, Result, ensure};
use indexmap::IndexMap;
use regex::Regex;
use serde_json::{Value, json};
use std::{collections::BTreeSet, sync::LazyLock};
fn array(v: &Value) -> &[Value] {
    v.as_array().map_or(&[], Vec::as_slice)
}
fn text(v: &Value) -> &str {
    v.as_str().unwrap_or("")
}
fn number(v: &Value) -> i64 {
    v.as_i64().unwrap_or(0)
}
fn show(v: &Value) -> String {
    if v.is_null() {
        "None".into()
    } else if let Some(s) = v.as_str() {
        s.into()
    } else {
        v.to_string()
    }
}
pub fn demangle_type(text: &str) -> Result<String> {
    fn part(text: &str) -> Result<(&str, &str)> {
        let n = text.chars().take_while(|c| c.is_ascii_digit()).count();
        ensure!(n > 0, "invalid mangled type {text}");
        let length = text[..n].parse::<usize>()?;
        let rest = &text[n..];
        let part = rest
            .get(..length)
            .context("mangled type length exceeds name")?;
        Ok((part, &rest[length..]))
    }
    if text.starts_with('Q') && text.as_bytes().get(1).is_some_and(u8::is_ascii_digit) {
        let count = usize::from(text.as_bytes()[1] - b'0');
        let mut rest = &text[2..];
        let mut parts = vec![];
        for _ in 0..count {
            let (p, r) = part(rest)?;
            parts.push(p);
            rest = r;
        }
        return Ok(parts.join("::"));
    }
    if text.starts_with(|c: char| c.is_ascii_digit()) {
        Ok(part(text)?.0.into())
    } else {
        Ok(text.into())
    }
}
pub fn element_type(name: &str) -> Result<Option<String>> {
    static INSTANCE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^\w+<(.*)>__").unwrap());
    static ITERATOR: LazyLock<Regex> =
        LazyLock::new(|| Regex::new(r"^Q24rstl\d+(?:const_)?pointer_iterator<(.*)").unwrap());
    let Some(c) = INSTANCE.captures(name) else {
        return Ok(None);
    };
    let mut arg = c[1].to_owned();
    if let Some(c) = ITERATOR.captures(&arg) {
        arg = c[1].into();
    }
    let arg = arg.strip_prefix('P').unwrap_or(&arg);
    Ok(Some(demangle_type(arg)?))
}
pub fn walk(breakdown: &Value) -> Vec<(&Value, usize)> {
    let mut stack: Vec<_> = array(&breakdown["statements"])
        .iter()
        .rev()
        .map(|v| (v, 0))
        .collect();
    let mut out = vec![];
    while let Some((node, depth)) = stack.pop() {
        out.push((node, depth));
        stack.extend(array(&node["parts"]).iter().rev().map(|v| (v, depth + 1)));
    }
    out
}
pub fn summarize(breakdown: &Value, width: usize) -> Vec<String> {
    walk(breakdown)
        .iter()
        .filter(|(n, _)| n["what"] != "body")
        .map(|(node, depth)| {
            format!(
                "{:6}  {}{}",
                number(&node["cost"]),
                "  ".repeat(*depth),
                text(&node["what"]).chars().take(width).collect::<String>()
            )
        })
        .collect()
}
fn mentions(breakdown: &Value, element: &str) -> bool {
    let short = element
        .split('<')
        .next()
        .unwrap()
        .rsplit("::")
        .next()
        .unwrap();
    walk(breakdown)
        .iter()
        .any(|(n, _)| text(&n["what"]).contains(short))
}
fn same_overload(instance: &str, breakdown: &Value) -> bool {
    array(&breakdown["statements"])
        .iter()
        .any(|s| text(&s["what"]).contains("pointer_iterator"))
        == instance
            .split(">__")
            .next()
            .unwrap()
            .contains("pointer_iterator")
}
pub fn first_template_argument(text: &str) -> Result<&str> {
    let start = text.find('<').context("type has no template argument")? + 1;
    let mut depth = 0;
    for (i, c) in text[start..].char_indices() {
        match c {
            '<' => depth += 1,
            '>' if depth == 0 => return Ok(&text[start..start + i]),
            '>' => depth -= 1,
            ',' if depth == 0 => return Ok(&text[start..start + i]),
            _ => (),
        }
    }
    Ok(&text[start..])
}
pub fn mangle_qualified(name: &str) -> String {
    let parts: Vec<_> = name.split("::").collect();
    let encoded = parts
        .iter()
        .map(|p| format!("{}{p}", p.chars().count()))
        .collect::<String>();
    if parts.len() == 1 {
        encoded
    } else {
        format!("Q{}{encoded}", parts.len())
    }
}
pub fn decision_instance(
    callee: &str,
    breakdown: &Value,
) -> Result<Option<(String, Option<String>)>> {
    static ITERATOR: LazyLock<Regex> =
        LazyLock::new(|| Regex::new(r"rstl::(?:const_)?pointer_iterator<").unwrap());
    for s in array(&breakdown["statements"]) {
        let what = text(&s["what"]);
        if let Some(m) = ITERATOR.find(what) {
            return Ok(Some((
                format!("{callee}<Q24rstl"),
                Some(format!(
                    "pointer_iterator<{},",
                    first_template_argument(&what[m.start() + 6..])?
                )),
            )));
        }
    }
    for s in array(&breakdown["statements"]) {
        if let Some(owner) = text(&s["what"]).strip_suffix("::__dt()") {
            return Ok(Some((
                format!("{callee}<{}>__4rstlFP", mangle_qualified(owner)),
                None,
            )));
        }
    }
    Ok(None)
}
pub fn rows(
    unit: &str,
    target: &Value,
    document: &Value,
    callee: &str,
    near: i64,
) -> Result<Vec<Value>> {
    let prefix = format!("{callee}<");
    let target_functions = array(&target["functions"]);
    let missing: Vec<_> = target_functions
        .iter()
        .filter(|f| f.get("fuzzy_match_percent").is_none() && text(&f["name"]).starts_with(&prefix))
        .map(|f| text(&f["name"]))
        .collect();
    let target_names: BTreeSet<_> = target_functions.iter().map(|f| text(&f["name"])).collect();
    let functions = array(&document["functions"]);
    let extra: BTreeSet<_> = functions
        .iter()
        .map(|f| text(&f["link"]))
        .filter(|l| l.starts_with(&prefix) && !target_names.contains(l))
        .collect();
    let decisions: Vec<_> = functions
        .iter()
        .flat_map(|f| {
            array(&f["inlining"])
                .iter()
                .map(move |d| (text(&f["link"]), d))
        })
        .filter(|(_, d)| d["event"] == "decision" && text(&d["callee"]).contains(callee))
        .collect();
    let mut out = vec![];
    for (kind, instances, need) in [
        ("missing", missing, "call"),
        ("extra", extra.into_iter().collect(), "inline"),
    ] {
        for instance in instances {
            let element = element_type(instance)?;
            let found: Vec<_> = decisions
                .iter()
                .filter(|(_, d)| {
                    element
                        .as_ref()
                        .is_some_and(|element| mentions(&d["breakdown"], element))
                        && same_overload(instance, &d["breakdown"])
                })
                .collect();
            let costs: BTreeSet<_> = found.iter().map(|(_, d)| number(&d["cost"])).collect();
            let ok = if found.is_empty() {
                None
            } else {
                Some(found.iter().all(|(_, d)| {
                    (number(&d["cost"]) > number(&d["threshold"])) == (need == "call")
                }))
            };
            out.push(json!({"unit":unit,"kind":kind,"instance":instance,"element":element,"need":need,"costs":costs,"threshold":found.first().map(|(_,d)|&d["threshold"]),"ok":ok,"decisions":found.iter().map(|(caller,d)|json!({"caller":caller,"cost":d["cost"],"inline":d["inline"],"breakdown":d.get("breakdown").cloned().unwrap_or(json!({}))})).collect::<Vec<_>>()}));
        }
    }
    for (caller, d) in decisions {
        ensure!(
            d["cost"].is_i64() && d["threshold"].is_i64(),
            "{unit}: callee {callee} has no cost/threshold; cannot calibrate that decision"
        );
        if (number(&d["cost"]) - number(&d["threshold"])).abs() <= near {
            out.push(json!({"unit":unit,"kind":"near","instance":caller,"element":null,"need":if number(&d["cost"])>number(&d["threshold"]){"call"}else{"inline"},"costs":[d["cost"]],"threshold":d["threshold"],"ok":null,"decisions":[{"caller":caller,"cost":d["cost"],"inline":d["inline"],"breakdown":d.get("breakdown").cloned().unwrap_or(json!({}))}]}));
        }
    }
    Ok(out)
}
pub fn report(rows: &[Value], near: i64, breakdown: bool) -> Vec<String> {
    let mut out = vec![];
    for row in rows {
        if row["kind"] == "near" {
            continue;
        }
        let status = match row["ok"].as_bool() {
            Some(true) => "ok",
            Some(false) => "WRONG",
            None => "no decision found",
        };
        let delta = if row["ok"] == false {
            let max = array(&row["costs"]).iter().map(number).max().unwrap_or(0);
            if row["need"] == "call" {
                format!(" (needs +{} or more)", number(&row["threshold"]) + 1 - max)
            } else {
                format!(" (needs -{} or more)", max - number(&row["threshold"]))
            }
        } else {
            String::new()
        };
        let costs = format!(
            "[{}]",
            array(&row["costs"])
                .iter()
                .map(|c| c.to_string())
                .collect::<Vec<_>>()
                .join(", ")
        );
        out.push(format!(
            "{:8} {}: {}  target needs {} (cost {} {}); ours {costs}: {status}{delta}",
            text(&row["kind"]),
            text(&row["unit"]),
            show(&row["element"]),
            text(&row["need"]),
            if row["need"] == "call" { ">" } else { "<=" },
            show(&row["threshold"])
        ));
        if breakdown && let Some(d) = array(&row["decisions"]).first() {
            out.push(format!("          in {}:", text(&d["caller"])));
            out.extend(
                summarize(&d["breakdown"], 110)
                    .iter()
                    .map(|l| format!("          {l}")),
            );
        }
    }
    let close: Vec<_> = rows.iter().filter(|r| r["kind"] == "near").collect();
    if !close.is_empty() {
        out.push(format!("\ndecisions within {near} of the threshold (whatever the target needs, a header change that moves them crosses it):"));
        for row in close {
            out.push(format!(
                "  {}: {} vs {} in {}",
                text(&row["unit"]),
                number(&row["costs"][0]),
                number(&row["threshold"]),
                text(&row["instance"]).chars().take(100).collect::<String>()
            ));
        }
    }
    out
}
pub fn binary_table(
    documents: &[(String, Value)],
    symbols: &BTreeSet<String>,
    callee: &str,
) -> Result<IndexMap<String, Value>> {
    let prefix = format!("{callee}<");
    let mut table: IndexMap<String, Value> = IndexMap::new();
    for (unit, document) in documents {
        for d in array(&document["functions"])
            .iter()
            .flat_map(|f| array(&f["inlining"]))
        {
            if d["event"] != "decision" || !text(&d["callee"]).contains(callee) {
                continue;
            }
            ensure!(
                d["cost"].is_i64() && d["threshold"].is_i64(),
                "{unit}: decision has no cost/threshold"
            );
            let (key, present) = if let Some(link) = d["link"].as_str().filter(|s| !s.is_empty()) {
                (link.to_owned(), symbols.contains(link))
            } else {
                let Some((head, needle)) = decision_instance(callee, &d["breakdown"])? else {
                    continue;
                };
                let present = symbols
                    .iter()
                    .filter(|s| s.starts_with(&prefix))
                    .any(|s| s.starts_with(&head) && needle.as_ref().is_none_or(|n| s.contains(n)));
                (format!("{head}{}", needle.unwrap_or_default()), present)
            };
            let entry = table
                .entry(key)
                .or_insert(json!({"present":present,"costs":[],"units":[],"thresholds":[]}));
            entry["costs"]
                .as_array_mut()
                .unwrap()
                .push(d["cost"].clone());
            for (key, value) in [
                ("units", json!(unit)),
                ("thresholds", d["threshold"].clone()),
            ] {
                let values = entry[key].as_array_mut().unwrap();
                if !values.contains(&value) {
                    values.push(value);
                }
            }
        }
    }
    for entry in table.values_mut() {
        entry["units"]
            .as_array_mut()
            .unwrap()
            .sort_by(|a, b| text(a).cmp(text(b)));
        entry["thresholds"]
            .as_array_mut()
            .unwrap()
            .sort_by_key(number);
    }
    Ok(table)
}
pub fn binary_report(table: &IndexMap<String, Value>, near: i64) -> Vec<String> {
    let mut out = vec![format!(
        "{:8} {:>14}  instance (units)",
        "target", "ours min..max"
    )];
    let mut entries: Vec<_> = table.iter().collect();
    entries.sort_by_key(|(_, e)| {
        (
            e["present"] == true,
            array(&e["costs"]).iter().map(number).min().unwrap_or(0),
        )
    });
    for (key, e) in entries {
        let lo = array(&e["costs"]).iter().map(number).min().unwrap();
        let hi = array(&e["costs"]).iter().map(number).max().unwrap();
        let threshold = array(&e["thresholds"]).iter().map(number).min().unwrap();
        let present = e["present"] == true;
        if lo < threshold - near && hi < threshold - near && !present {
            continue;
        }
        let status = if present { "called" } else { "inlined" };
        let mut flag = if present && hi <= threshold {
            format!("  needs +{}", threshold + 1 - hi)
        } else if !present && hi > threshold {
            format!("  needs -{}", hi - threshold)
        } else if !present {
            format!("  room +{}", threshold - hi)
        } else {
            String::new()
        };
        if array(&e["thresholds"]).len() > 1 {
            flag.push_str(&format!(
                "  thresholds [{}]",
                array(&e["thresholds"])
                    .iter()
                    .map(|t| t.to_string())
                    .collect::<Vec<_>>()
                    .join(", ")
            ));
        }
        out.push(format!(
            "{status:8} {lo:6}..{hi:<6}  {} ({}){flag}",
            key.chars().take(110).collect::<String>(),
            array(&e["units"]).len()
        ));
    }
    out
}
