//! Translation-unit inlining and target-emission comparisons.
use crate::{SCHEMA_VERSION, project, report};
use anyhow::Result;
use indexmap::IndexMap;
use regex::Regex;
use serde_json::{Value, json};
use std::{
    collections::BTreeSet,
    fs,
    path::{Path, PathBuf},
    sync::LazyLock,
};
fn a(v: &Value) -> &[Value] {
    v.as_array().map_or(&[], Vec::as_slice)
}
fn n(v: &Value) -> i64 {
    v.as_i64().unwrap_or(0)
}
fn s(v: &Value) -> &str {
    v.as_str().unwrap_or("")
}
fn strings(v: &Value) -> Vec<String> {
    a(v).iter().map(|v| s(v).to_owned()).collect()
}
pub fn decisions(document: &Value) -> IndexMap<String, Vec<Value>> {
    let mut out: IndexMap<String, Vec<Value>> = IndexMap::new();
    for (function, ds) in a(&document["functions"])
        .iter()
        .map(|f| {
            (
                format!("{} ({})", s(&f["function"]), s(&f["link"])),
                a(&f["inlining"]),
            )
        })
        .chain(std::iter::once((
            "(after the last function)".into(),
            a(&document["inlining_after_last_function"]),
        )))
    {
        for d in ds {
            if d["kind"] == "decision" {
                let mut d = d.clone();
                d["function"] = json!(function);
                out.entry(s(&d["link"]).into()).or_default().push(d);
            }
        }
    }
    out
}
pub fn summary(ds: &[Value]) -> Value {
    let mut outcomes = IndexMap::<String, i64>::new();
    let mut costs = BTreeSet::new();
    let mut whole = BTreeSet::new();
    let mut rounds = BTreeSet::new();
    let mut functions = BTreeSet::new();
    let mut reasons = BTreeSet::new();
    let mut chains = BTreeSet::new();
    let mut paths = BTreeSet::new();
    let mut terms = IndexMap::new();
    for d in ds {
        *outcomes.entry(s(&d["outcome"]).into()).or_default() += n(&d["count"]);
        if let Some(cost) = d.get("cost") {
            costs.insert(n(cost));
        } else {
            reasons.insert(s(&d["reason"]).to_owned());
        }
        if let Some(cost) = d.get("full_cost") {
            whole.insert(n(cost));
        }
        rounds.extend(a(&d["depths"]).iter().map(n));
        functions.insert(s(&d["function"]).to_owned());
        if !a(&d["chain"]).is_empty() {
            chains.insert(strings(&d["chain"]));
        }
        paths.extend(a(&d["limit_paths"]).iter().map(strings));
        for term in a(&d["sensitivity"]) {
            let key = (
                strings(&term["path"]),
                s(&term["kind"]).to_owned(),
                n(&term["delta"]),
            );
            let mut term = term.clone();
            let previous: Option<&Value> = terms.get(&key);
            term["flips"] =
                json!(term["flips"] == true || previous.is_some_and(|t| t["flips"] == true));
            terms.insert(key, term);
        }
    }
    let mut terms: Vec<_> = terms.into_values().collect();
    terms.sort_by_key(|t| {
        (
            t["flips"] != true,
            n(&t["delta"]).abs(),
            strings(&t["path"]),
        )
    });
    json!({"outcomes":outcomes,"costs":costs,"full_costs":whole,"rounds":rounds,"functions":functions,"reasons":reasons,"chains":chains,"limit_paths":paths,"sensitivity":terms})
}
fn span(v: &Value) -> String {
    let a = a(v);
    if a.is_empty() {
        "-".into()
    } else if a.len() == 1 {
        a[0].to_string()
    } else {
        format!("{}..{}", a[0], a.last().unwrap())
    }
}
fn section(title: &str, lines: Vec<String>, more: &str) -> Vec<String> {
    if lines.is_empty() {
        return vec![];
    }
    let mut out = vec![format!("      {title}:")];
    out.extend(lines.iter().take(12).map(|l| format!("         {l}")));
    if lines.len() > 12 {
        out.push(format!("         ... {} more {more}", lines.len() - 12));
    }
    out
}
pub fn summary_report(name: &str, summary: &Value) -> Vec<String> {
    let mut outcomes: Vec<_> = summary["outcomes"]
        .as_object()
        .into_iter()
        .flatten()
        .collect();
    outcomes.sort_by_key(|(k, _)| *k);
    let rounds = a(&summary["rounds"])
        .iter()
        .map(|v| v.to_string())
        .collect::<Vec<_>>()
        .join(", ");
    let mut out = vec![
        format!(
            "   {name}: {}; estimate {}, whole cost {}, rounds {}{}",
            outcomes
                .iter()
                .map(|(k, v)| format!("{k} x{v}"))
                .collect::<Vec<_>>()
                .join(", "),
            span(&summary["costs"]),
            span(&summary["full_costs"]),
            if rounds.is_empty() { "-" } else { &rounds },
            summary["target"]
                .as_str()
                .map_or(String::new(), |s| format!("   {s}"))
        ),
        format!("      in {}", strings(&summary["functions"]).join(", ")),
    ];
    out.extend(
        a(&summary["reasons"])
            .iter()
            .map(|r| format!("      because: {}", s(r))),
    );
    if let Some(verdict) = summary["verdict"].as_str().filter(|s| !s.is_empty()) {
        out.push(format!("      verdict: {verdict}"));
    }
    out.extend(section(
        "inside",
        report::tree(a(&summary["chains"])),
        "lines",
    ));
    out.extend(section(
        "at the depth limit [limit +1]",
        report::tree(a(&summary["limit_paths"])),
        "lines",
    ));
    out.extend(section(
        "sensitive",
        a(&summary["sensitivity"])
            .iter()
            .map(report::sensitivity)
            .collect(),
        "terms",
    ));
    out
}
#[derive(Clone, Debug)]
pub struct TargetFunctions {
    pub functions: IndexMap<String, String>,
    pub module: IndexMap<String, PathBuf>,
    pub stripped: bool,
}
pub fn asm_functions(text: &str) -> IndexMap<String, String> {
    static FN: LazyLock<Regex> =
        LazyLock::new(|| Regex::new(r#"^\s*\.fn\s+("[^"]*"|\S+),\s*(\w+)"#).unwrap());
    text.lines()
        .filter_map(|line| {
            FN.captures(line)
                .map(|c| (c[1].trim_matches('"').into(), c[2].into()))
        })
        .collect()
}
pub(crate) fn asm_paths(root: &Path, out: &mut Vec<PathBuf>) -> Result<()> {
    for entry in fs::read_dir(root)? {
        let entry = entry?;
        let p = entry.path();
        if entry.file_type()?.is_dir() {
            asm_paths(&p, out)?;
        } else if p.extension().is_some_and(|s| s == "s") {
            out.push(p);
        }
    }
    Ok(())
}
pub fn target_functions(directory: &Path, source: &str) -> Result<Option<TargetFunctions>> {
    let Some(own) = project::target_asm(directory, source)? else {
        return Ok(None);
    };
    target_functions_at(directory, &own).map(Some)
}
/// The functions of the target asm OWN, under the build directory DIRECTORY, and the module's others.
pub fn target_functions_at(directory: &Path, own: &Path) -> Result<TargetFunctions> {
    let own = own.to_owned();
    let functions = asm_functions(&String::from_utf8_lossy(&fs::read(&own)?));
    let mut root = own.parent().unwrap();
    while root.file_name().is_some_and(|n| n != "asm") {
        root = root.parent().unwrap();
    }
    let mut paths = vec![];
    asm_paths(root, &mut paths)?;
    paths.sort();
    let mut module = IndexMap::new();
    // The first definition owns a weak symbol. If it is ours, a later TU must not claim it.
    for path in paths {
        for link in asm_functions(&String::from_utf8_lossy(&fs::read(&path)?)).keys() {
            module.entry(link.clone()).or_insert_with(|| path.clone());
        }
    }
    module.retain(|_, path| *path != own);
    Ok(TargetFunctions {
        functions,
        module,
        stripped: root == directory.join("asm"),
    })
}
pub fn emission(document: &Value) -> IndexMap<String, String> {
    let mut why = IndexMap::new();
    for e in a(&document["instantiation"]) {
        if e["event"] == "emit" {
            why.entry(s(&e["link"])).or_insert_with(|| {
                format!(
                    "{}{}",
                    e["why"].as_str().unwrap_or("referenced"),
                    e["from"]
                        .as_str()
                        .filter(|s| !s.is_empty())
                        .map_or(String::new(), |s| format!(" (in {s})"))
                )
            });
        }
    }
    a(&document["functions"])
        .iter()
        .map(|f| {
            (
                s(&f["link"]).to_owned(),
                why.get(s(&f["link"])).cloned().unwrap_or_default(),
            )
        })
        .collect()
}
pub fn prediction(
    link: &str,
    target: Option<&TargetFunctions>,
    emitted: &IndexMap<String, String>,
) -> (Option<String>, bool) {
    let Some(target) = target else {
        return (None, false);
    };
    let binding = target.functions.get(link);
    let state = binding.map_or("target: not emitted".into(), |b| {
        format!("target: out of line ({b})")
    });
    if emitted.contains_key(link) == binding.is_some() {
        return (
            Some(format!(
                "{state}{}",
                if binding.is_some() {
                    "; emitted here too"
                } else {
                    ""
                }
            )),
            false,
        );
    }
    if let Some(why) = emitted.get(link) {
        if let Some(elsewhere) = target.module.get(link) {
            return (
                Some(format!(
                    "{state} (the module keeps its one copy in {}); emitted here too",
                    elsewhere.file_name().unwrap().to_string_lossy()
                )),
                false,
            );
        }
        if target.stripped {
            return (
                Some(format!(
                    "{state} (the main program strips unreferenced weak functions); emitted here"
                )),
                false,
            );
        }
        return (
            Some(format!(
                "{state} (nor anywhere in the module); MISPREDICTED: emitted here{}",
                if why.is_empty() {
                    String::new()
                } else {
                    format!(", {why}")
                }
            )),
            true,
        );
    }
    (
        Some(format!("{state}; MISPREDICTED: not emitted here")),
        true,
    )
}
pub fn verdict(ds: &[Value], emitted_here: bool) -> Option<String> {
    if ds.is_empty() {
        return None;
    }
    let inlined: Vec<_> = ds.iter().filter(|d| d["outcome"] == "inlined").collect();
    let no_body: Vec<_> = ds
        .iter()
        .filter(|d| s(&d["reason"]).contains("no body"))
        .collect();
    let limit = ds
        .iter()
        .any(|d| d["outcome"] == "limit" || !a(&d["limit_paths"]).is_empty());
    let margin = ds
        .iter()
        .filter(|d| d.get("cost").is_some())
        .map(|d| n(&d["threshold"]) - d.get("full_cost").map_or(n(&d["cost"]), n))
        .min();
    let rounds = |ds: &[&Value]| -> BTreeSet<i64> {
        ds.iter()
            .flat_map(|d| {
                d.get("depths")
                    .map_or_else(|| vec![n(&d["depth"])], |r| a(r).iter().map(n).collect())
            })
            .collect()
    };
    let deepest = rounds(&ds.iter().collect::<Vec<_>>())
        .last()
        .copied()
        .unwrap_or(0);
    if !emitted_here {
        if inlined.len() < ds.len() {
            return None;
        }
        let mut out = vec![format!(
            "the target keeps a call this TU inlines every time ({}x, rounds {}, with its body)",
            inlined
                .iter()
                .map(|d| d["count"].as_i64().unwrap_or(1))
                .sum::<i64>(),
            rounds(&inlined)
                .iter()
                .map(|r| r.to_string())
                .collect::<Vec<_>>()
                .join(", ")
        )];
        out.push(if deepest == 0 && !limit {
            "depth would not explain it (round 0, no call at the depth limit)".into()
        } else {
            format!(
                "depth could: inlined at round {deepest}{}",
                if limit {
                    ", calls at the depth limit"
                } else {
                    ""
                }
            )
        });
        if let Some(m) = margin {
            out.push(format!(
                "cost would need +{} to keep it{}",
                m + 1,
                if m <= 4 {
                    " (close: look at the sensitive terms)"
                } else {
                    ""
                }
            ));
        }
        if deepest == 0 && !limit && margin.is_none_or(|m| m > 4) {
            out.push("most likely the callee is not inline as written there (not declared inline, defined out of line), or had no body yet at that point of the original".into());
        }
        return Some(out.join("; "));
    }
    if !no_body.is_empty() {
        return Some(format!(
            "kept here {}x for want of a body (declared later or never defined here): the original had its body by then, or the target's copy is from elsewhere",
            no_body
                .iter()
                .map(|d| d["count"].as_i64().unwrap_or(1))
                .sum::<i64>()
        ));
    }
    if limit {
        return Some("kept here by the inline depth limit: the original's nesting is shallower, or its bodies smaller".into());
    }
    margin.filter(|m|*m<0).map(|m|format!("kept here by cost (over by {}): the original's body, or its calls' bodies, cost less{}",-m,if -m<=4{" (close: look at the sensitive terms)"}else{""}))
}
pub fn compare(
    documents: [&Value; 2],
    targets: [Option<&TargetFunctions>; 2],
    callee: Option<&str>,
    all: bool,
    against_target: bool,
) -> Value {
    let decisions = documents.map(decisions);
    let emitted = documents.map(emission);
    let mut rows = vec![];
    for link in decisions[0]
        .keys()
        .filter(|l| decisions[1].contains_key(*l))
        .collect::<BTreeSet<_>>()
    {
        if callee.is_some_and(|c| !link.contains(c)) {
            continue;
        }
        let mut summaries = [summary(&decisions[0][link]), summary(&decisions[1][link])];
        let predictions = [
            prediction(link, targets[0], &emitted[0]),
            prediction(link, targets[1], &emitted[1]),
        ];
        let outcomes = |v: &Value| -> BTreeSet<String> {
            v["outcomes"].as_object().unwrap().keys().cloned().collect()
        };
        if !all
            && !(if against_target {
                predictions.iter().any(|(_, wrong)| *wrong)
            } else {
                outcomes(&summaries[0]) != outcomes(&summaries[1])
            })
        {
            continue;
        }
        for i in 0..2 {
            if let Some(t) = &predictions[i].0 {
                summaries[i]["target"] = json!(t);
                summaries[i]["mispredicted"] = json!(predictions[i].1);
            }
            if predictions[i].1
                && let Some(why) = verdict(&decisions[i][link], emitted[i].contains_key(link))
            {
                summaries[i]["verdict"] = json!(why);
            }
        }
        rows.push(json!({"link":link,"callee":decisions[0][link][0]["callee"],"a":summaries[0],"b":summaries[1]}));
    }
    let mut out = json!({"schema_version":SCHEMA_VERSION,"a":documents[0]["source"],"b":documents[1]["source"],"callees":rows});
    if against_target {
        out["emitted"] = json!({"a":emitted[0],"b":emitted[1]});
        out["target"] =
            json!({"a":targets[0].map(|t|&t.functions),"b":targets[1].map(|t|&t.functions)});
    }
    out
}
pub fn emission_report(
    name: &str,
    document: &Value,
    target: &TargetFunctions,
    emitted: &IndexMap<String, String>,
    callee: Option<&str>,
) -> Vec<String> {
    let matches = |l: &str| callee.is_none_or(|c| l.contains(c));
    let extra: Vec<_> = emitted
        .keys()
        .filter(|l| {
            !target.functions.contains_key(*l) && !target.module.contains_key(*l) && matches(l)
        })
        .collect();
    let elsewhere = emitted
        .keys()
        .filter(|l| {
            !target.functions.contains_key(*l) && target.module.contains_key(*l) && matches(l)
        })
        .count();
    let missing: Vec<_> = target
        .functions
        .iter()
        .filter(|(l, b)| *b != "global" && !emitted.contains_key(*l) && matches(l))
        .collect();
    let mut out = vec![format!(
        "== {name}: {}: emitted differently from the target: {} here only (nowhere in the module), {} there only; {elsewhere} here whose copy the module keeps in another TU",
        s(&document["source"]),
        extra.len(),
        missing.len()
    )];
    if target.stripped && !extra.is_empty() {
        out.push("   (the main program's linker strips the weak functions nothing references: those here only may be that)".into());
    }
    for link in extra {
        out.push(format!(
            "   here only: {link}{}",
            if emitted[link].is_empty() {
                String::new()
            } else {
                format!("\n      {}", emitted[link])
            }
        ));
    }
    let decisions = decisions(document);
    for (link, binding) in missing {
        out.push(format!("   there only ({binding}): {link}"));
        if let Some(why) = verdict(decisions.get(link).map_or(&[], Vec::as_slice), false) {
            out.push(format!("      verdict: {why}"));
        }
    }
    out
}
