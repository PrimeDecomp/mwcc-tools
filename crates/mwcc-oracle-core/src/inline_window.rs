//! First-order inline-limit bounds from object and target call counts.
use crate::elf::Elf;
use anyhow::Result;
use indexmap::IndexMap;
use regex::Regex;
use serde_json::{Value, json};
use std::{collections::BTreeSet, sync::LazyLock};
type Calls = IndexMap<String, IndexMap<String, i64>>;
fn a(v: &Value) -> &[Value] {
    v.as_array().map_or(&[], Vec::as_slice)
}
fn n(v: &Value) -> i64 {
    v.as_i64().unwrap_or(0)
}
fn s(v: &Value) -> &str {
    v.as_str().unwrap_or("")
}
pub fn object_calls(data: &[u8]) -> Result<Calls> {
    let elf = Elf::parse(data)?;
    let functions: Vec<_> = elf.symbols()?.into_iter().filter(|s| s.kind == 2).collect();
    let mut out = Calls::new();
    for r in elf.relocations()? {
        if r.kind != 10 {
            continue;
        }
        if let Some(f) = functions.iter().find(|f| {
            f.section == r.section
                && f.value <= r.offset
                && r.offset < f.value.saturating_add(f.size)
        }) {
            *out.entry(f.name.clone())
                .or_default()
                .entry(r.symbol.name)
                .or_default() += 1;
        }
    }
    Ok(out)
}
pub fn asm_calls(text: &str) -> Calls {
    static MARKER: LazyLock<Regex> =
        LazyLock::new(|| Regex::new(r#"^\s*\.(fn|endfn)\s+("[^"]*"|[^\s,]+)"#).unwrap());
    static CALL: LazyLock<Regex> =
        LazyLock::new(|| Regex::new(r#"\bbl\s+("[^"]*"|[^\s"]+)"#).unwrap());
    let mut out = Calls::new();
    let mut current = None;
    for line in text.lines() {
        if let Some(c) = MARKER.captures(line) {
            current = if &c[1] == "fn" {
                Some(c[2].trim_matches('"').to_owned())
            } else {
                None
            };
            if let Some(current) = &current {
                out.entry(current.clone()).or_default();
            }
            continue;
        }
        if let Some(current) = &current
            && let Some(c) = CALL.captures(line.rsplit("*/").next().unwrap())
        {
            *out.get_mut(current)
                .unwrap()
                .entry(c[1].trim_matches('"').to_owned())
                .or_default() += 1;
        }
    }
    out
}
pub fn bounds(document: &Value, ours: &Calls, theirs: &Calls) -> Vec<Value> {
    let mut out = vec![];
    for f in a(&document["functions"]) {
        let link = s(&f["link"]);
        let Some(their_calls) = theirs.get(link) else {
            continue;
        };
        let mut decisions: IndexMap<&str, Vec<&Value>> = IndexMap::new();
        for d in a(&f["inlining"]) {
            if d["kind"] == "decision" {
                decisions.entry(s(&d["link"])).or_default().push(d);
            }
        }
        for (callee, ds) in decisions {
            let mine = ours
                .get(link)
                .and_then(|c| c.get(callee))
                .copied()
                .unwrap_or(0);
            let target = their_calls.get(callee).copied().unwrap_or(0);
            let costed: Vec<_> = ds
                .iter()
                .filter(|d| d.get("cost").is_some() && d["depth_limited"] != true)
                .copied()
                .collect();
            let cost = |d: &&Value| n(d.get("full_cost").unwrap_or(&d["cost"]));
            let inlined: Vec<_> = costed
                .iter()
                .filter(|d| d["outcome"] == "inlined")
                .map(cost)
                .collect();
            let kept: Vec<_> = costed
                .iter()
                .filter(|d| d["outcome"] != "inlined")
                .map(cost)
                .collect();
            let row = json!({"function":f["function"],"link":link,"callee":ds[0]["callee"],"callee_link":callee,"ours":mine,"target":target,"threshold":costed.first().map(|d|&d["threshold"])});
            let mut push = |side: Option<&str>, value: Option<i64>, why: String| {
                let mut row = row.clone();
                row["side"] = json!(side);
                if let Some(value) = value {
                    row["value"] = json!(value);
                }
                row["why"] = json!(why);
                out.push(row);
            };
            if costed.is_empty() {
                if mine != target {
                    let reasons: BTreeSet<_> = ds
                        .iter()
                        .map(|d| d.get("reason").unwrap_or(&d["outcome"]))
                        .map(s)
                        .collect();
                    push(
                        None,
                        None,
                        reasons.into_iter().collect::<Vec<_>>().join("; "),
                    );
                }
                continue;
            }
            if mine == target {
                if let Some(value) = inlined.iter().max() {
                    push(Some("low"), Some(*value), "inlined in both".into());
                }
                if mine != 0
                    && let Some(value) = kept.iter().min()
                {
                    push(Some("high"), Some(value - 1), "kept in both".into());
                }
            } else if target < mine {
                if let Some(value) = if target == 0 {
                    kept.iter().max()
                } else {
                    kept.iter().min()
                } {
                    push(
                        Some("low"),
                        Some(*value),
                        format!(
                            "the target inlines {}, where ours keeps them",
                            if target == 0 {
                                "all of them".into()
                            } else {
                                format!("{} more", mine - target)
                            }
                        ),
                    );
                } else {
                    push(
                        None,
                        None,
                        "the target makes fewer calls, but ours keeps none for cost".into(),
                    );
                }
            } else if let Some(value) = inlined.iter().max() {
                push(
                    Some("high"),
                    Some(value - 1),
                    format!(
                        "the target keeps {} more, where ours inlines them",
                        target - mine
                    ),
                );
            } else {
                push(
                    None,
                    None,
                    "the target makes more calls, but ours inlines none for cost".into(),
                );
            }
        }
    }
    out
}
pub fn window(rows: &[Value]) -> (Option<i64>, Option<i64>) {
    (
        rows.iter()
            .filter(|r| r["side"] == "low")
            .map(|r| n(&r["value"]))
            .max(),
        rows.iter()
            .filter(|r| r["side"] == "high")
            .map(|r| n(&r["value"]))
            .min(),
    )
}
fn describe(r: &Value) -> String {
    format!(
        "{} in {} (calls: ours {}, target {}): {}",
        s(&r["callee"]),
        s(&r["function"]),
        n(&r["ours"]),
        n(&r["target"]),
        s(&r["why"])
    )
}
pub fn report(source: &str, rows: &[Value], verbose: bool) -> Vec<String> {
    let (low, high) = window(rows);
    let current = rows.iter().find_map(|r| r["threshold"].as_i64());
    let shown = |v: Option<i64>| v.map_or("-".into(), |v| v.to_string());
    let mut out = vec![format!(
        "== {source}: limit {}, the target agrees with {}..{}",
        current.map_or("None".into(), |v| v.to_string()),
        shown(low),
        shown(high)
    )];
    if low.zip(high).is_some_and(|(l, h)| l > h) {
        out.push("   EMPTY: no single limit agrees with every call: the source differs (or the limit differs per function); the bounds that cross:".into());
    } else if current.is_some_and(|c| low.is_some_and(|l| c < l) || high.is_some_and(|h| c > h)) {
        out.push(format!("   the current limit {} is outside it: the target's build likely used another limit for this unit (a setting, not the source)",current.unwrap()));
    }
    for (side, label) in [("low", "at least"), ("high", "at most")] {
        let mut group: Vec<_> = rows.iter().filter(|r| r["side"] == side).collect();
        group.sort_by_key(|r| {
            if side == "low" {
                -n(&r["value"])
            } else {
                n(&r["value"])
            }
        });
        let binding = if verbose {
            group
        } else {
            let differing: Vec<_> = group
                .iter()
                .filter(|r| !s(&r["why"]).ends_with("in both"))
                .take(6)
                .copied()
                .collect();
            if differing.is_empty() {
                group.into_iter().take(2).collect()
            } else {
                differing
            }
        };
        for row in binding {
            out.push(format!(
                "   {label} {}: {}",
                n(&row["value"]),
                describe(row)
            ));
        }
    }
    let skipped: Vec<_> = rows.iter().filter(|r| r["side"].is_null()).collect();
    for r in skipped.iter().take(if verbose { usize::MAX } else { 8 }) {
        out.push(format!("   not a limit question: {}", describe(r)));
    }
    if skipped.len() > 8 && !verbose {
        out.push(format!(
            "   ... {} more not a limit question (--verbose)",
            skipped.len() - 8
        ));
    }
    out
}
