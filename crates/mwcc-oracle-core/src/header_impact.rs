//! Compare code and compiler decisions for the dependents of changed headers.
use crate::{elf::Elf, ppc, project, sequence};
use anyhow::Result;
use indexmap::IndexMap;
use regex::Regex;
use serde_json::{Value, json};
use std::{
    collections::{BTreeMap, BTreeSet},
    path::{Component, Path, PathBuf},
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
fn truth(v: &Value) -> bool {
    v.as_bool().unwrap_or_else(|| n(v) != 0)
}
pub fn object_functions(data: &[u8]) -> Result<IndexMap<String, Vec<u32>>> {
    let elf = Elf::parse(data)?;
    let mut out = IndexMap::new();
    for symbol in elf.symbols()? {
        if symbol.kind == 2 && symbol.section < elf.sections.len() && symbol.size > 0 {
            out.insert(symbol.name.clone(), elf.function_words(&symbol)?);
        }
    }
    Ok(out)
}
fn resolved(root: &Path, name: &str) -> String {
    let path = root.join(name);
    let path = path.canonicalize().unwrap_or_else(|_| {
        let mut out = PathBuf::new();
        for c in path.components() {
            match c {
                Component::CurDir => (),
                Component::ParentDir => {
                    out.pop();
                }
                c => out.push(c.as_os_str()),
            }
        }
        out
    });
    project::display_path(&path)
        .replace('\\', "/")
        .to_lowercase()
}
pub fn dependents(root: &Path, changed: &[String], text: &str) -> IndexMap<String, Vec<String>> {
    let wanted: BTreeMap<_, _> = changed.iter().map(|c| (resolved(root, c), c)).collect();
    let mut out = IndexMap::new();
    let mut deps = vec![];
    let close = |deps: &[String], out: &mut IndexMap<String, Vec<String>>| {
        if let Some(source) = deps
            .first()
            .filter(|s| [".c", ".cpp", ".cp"].iter().any(|ext| s.ends_with(ext)))
        {
            let hits: BTreeSet<_> = deps
                .iter()
                .filter_map(|d| wanted.get(&resolved(root, d)).map(|s| (*s).clone()))
                .collect();
            if !hits.is_empty() {
                out.insert(source.clone(), hits.into_iter().collect());
            }
        }
    };
    for line in text.lines() {
        if !line.is_empty() && !line.starts_with(char::is_whitespace) {
            close(&deps, &mut out);
            deps.clear();
        } else if !line.trim().is_empty() {
            deps.push(line.trim().to_owned());
        }
    }
    close(&deps, &mut out);
    out
}
pub fn similarity(ours: Option<&[u32]>, theirs: Option<&[u32]>) -> Option<f64> {
    let ours = ours.filter(|s| !s.is_empty())?;
    let theirs = theirs.filter(|s| !s.is_empty())?;
    let ours: Vec<_> = ours.iter().map(|w| ppc::instruction_token(*w)).collect();
    let theirs: Vec<_> = theirs.iter().map(|w| ppc::instruction_token(*w)).collect();
    let matched: usize = sequence::matching_blocks_with_autojunk(&ours, &theirs, false)
        .iter()
        .map(|(_, _, size)| size)
        .sum();
    Some(2.0 * matched as f64 / (ours.len() + theirs.len()) as f64)
}
pub fn decisions(function: &Value) -> IndexMap<String, Value> {
    let mut out: IndexMap<String, (IndexMap<String, i64>, BTreeSet<i64>)> = IndexMap::new();
    for d in a(&function["inlining"]) {
        if d["kind"] == "decision" {
            let (outcomes, costs) = out.entry(s(&d["link"]).into()).or_default();
            *outcomes.entry(s(&d["outcome"]).into()).or_default() += n(&d["count"]);
            if let Some(cost) = d.get("full_cost").or_else(|| d.get("cost")) {
                costs.insert(n(cost));
            }
        }
    }
    out.into_iter()
        .map(|(link, (outcomes, costs))| (link, json!([outcomes, costs])))
        .collect()
}
pub fn frame(function: &Value) -> IndexMap<String, Value> {
    a(&function["frame"])
        .iter()
        .filter(|e| {
            truth(&e["used"])
                && !truth(&e["reg"])
                && !(n(&e["offset"]) == 0 && s(&e["name"]).starts_with('@'))
        })
        .map(|e| {
            (
                s(&e["name"]).into(),
                e.get("r1").unwrap_or(&e["offset"]).clone(),
            )
        })
        .collect()
}
pub fn paired<'a>(names: impl IntoIterator<Item = &'a String>) -> IndexMap<String, String> {
    static NUMBER: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"(\$|@)\d+").unwrap());
    let mut seen = BTreeMap::new();
    let mut out = IndexMap::new();
    for name in names {
        let key = NUMBER.replace_all(name, "$1").into_owned();
        let count = seen.entry(key.clone()).or_insert(0);
        *count += 1;
        out.insert(
            if *count == 1 {
                key
            } else {
                format!("{key}#{count}")
            },
            name.clone(),
        );
    }
    out
}
pub fn compare(
    objects: [&[u8]; 2],
    documents: [&Value; 2],
    asm: Option<&str>,
) -> Result<Vec<Value>> {
    let ours = [object_functions(objects[0])?, object_functions(objects[1])?];
    let docs = documents.map(|d| {
        a(&d["functions"])
            .iter()
            .map(|f| (s(&f["link"]), f))
            .collect::<IndexMap<_, _>>()
    });
    let keys = ours.each_ref().map(|o| paired(o.keys()));
    let mut out = vec![];
    for key in keys[0]
        .keys()
        .chain(keys[1].keys())
        .collect::<BTreeSet<_>>()
    {
        let links = [keys[0].get(key), keys[1].get(key)];
        let words = [
            links[0].and_then(|l| ours[0].get(l)),
            links[1].and_then(|l| ours[1].get(l)),
        ];
        if words[0] == words[1] {
            continue;
        }
        let link = links[1].or(links[0]).unwrap();
        let null = Value::Null;
        let fs = [
            links[0]
                .and_then(|l| docs[0].get(l.as_str()))
                .copied()
                .unwrap_or(&null),
            links[1]
                .and_then(|l| docs[1].get(l.as_str()))
                .copied()
                .unwrap_or(&null),
        ];
        let function = fs[1]["function"]
            .as_str()
            .filter(|s| !s.is_empty())
            .or_else(|| fs[0]["function"].as_str().filter(|s| !s.is_empty()))
            .unwrap_or(link);
        let mut row =
            json!({"link":link,"function":function,"size":words.map(|w|w.map(|w|w.len()*4))});
        if let Some(theirs) = asm.and_then(|s| ppc::target_words(s, link)) {
            row["similarity"] =
                json!(words.map(|w| similarity(w.map(Vec::as_slice), Some(&theirs))));
        }
        let ds = fs.map(decisions);
        let mut inlining = vec![];
        for callee in ds[0].keys().chain(ds[1].keys()).collect::<BTreeSet<_>>() {
            if ds[0].get(callee) != ds[1].get(callee) {
                inlining.push(
                    json!({"callee":callee,"before":ds[0].get(callee),"after":ds[1].get(callee)}),
                );
            }
        }
        row["inlining"] = json!(inlining);
        let frames = fs.map(frame);
        let slots:Vec<_>=frames[0].keys().chain(frames[1].keys()).collect::<BTreeSet<_>>().into_iter().filter(|name|frames[0].get(*name)!=frames[1].get(*name)).map(|name|json!({"name":name,"before":frames[0].get(name),"after":frames[1].get(name)})).collect();
        row["frame"] = json!(slots);
        let sizes = fs.map(|f| f["layout"]["size"].clone());
        if sizes[0] != sizes[1] {
            row["frame_size"] = json!(sizes);
        }
        out.push(row);
    }
    Ok(out)
}
fn shown_decision(d: &Value) -> String {
    if d.is_null() {
        return "no call".into();
    }
    let mut outcomes: Vec<_> = d[0].as_object().into_iter().flatten().collect();
    outcomes.sort_by_key(|(k, _)| *k);
    format!(
        "{}{}",
        outcomes
            .iter()
            .map(|(o, n)| format!("{o} x{n}"))
            .collect::<Vec<_>>()
            .join(", "),
        if a(&d[1]).is_empty() {
            String::new()
        } else {
            format!(
                " (cost {})",
                a(&d[1])
                    .iter()
                    .map(|c| c.to_string())
                    .collect::<Vec<_>>()
                    .join(", ")
            )
        }
    )
}
pub fn report(unit: &str, files: &[String], rows: &[Value]) -> Vec<String> {
    let mut out = vec![format!(
        "== {unit} (includes {}): {} function(s) changed",
        files.join(", "),
        rows.len()
    )];
    for row in rows {
        let mut line = format!("   {}", s(&row["function"]));
        let sizes = &row["size"];
        if sizes[0].is_null() || sizes[1].is_null() {
            line.push_str(if sizes[0].is_null() {
                " (only after)"
            } else {
                " (only before)"
            });
        } else if sizes[0] != sizes[1] {
            line.push_str(&format!(" ({} -> {} bytes)", sizes[0], sizes[1]));
        }
        if let (Some(a), Some(b)) = (row["similarity"][0].as_f64(), row["similarity"][1].as_f64()) {
            line.push_str(&format!(
                ": similarity to the target {:.2}% -> {:.2}% ({})",
                100.0 * a,
                100.0 * b,
                if b < a {
                    "WORSE"
                } else if b > a {
                    "better"
                } else {
                    "same"
                }
            ));
        }
        out.push(line);
        for d in a(&row["inlining"]) {
            out.push(format!(
                "      inline {}: {} -> {}",
                s(&d["callee"]),
                shown_decision(&d["before"]),
                shown_decision(&d["after"])
            ));
        }
        if let Some(sizes) = row.get("frame_size") {
            let shown = |v: &Value| {
                if v.is_null() {
                    "None".into()
                } else {
                    v.to_string()
                }
            };
            out.push(format!(
                "      frame {} -> {} bytes",
                shown(&sizes[0]),
                shown(&sizes[1])
            ));
        }
        for f in a(&row["frame"]).iter().take(8) {
            let at = |x: &Value| {
                if x.is_null() {
                    "-".into()
                } else {
                    format!("r1+{:#x}", n(x))
                }
            };
            out.push(format!(
                "      slot {}: {} -> {}",
                s(&f["name"]),
                at(&f["before"]),
                at(&f["after"])
            ));
        }
        if a(&row["frame"]).len() > 8 {
            out.push(format!(
                "      ... {} more slots moved",
                a(&row["frame"]).len() - 8
            ));
        }
        if a(&row["inlining"]).is_empty()
            && a(&row["frame"]).is_empty()
            && row.get("frame_size").is_none()
        {
            out.push("      (same inlining and frame: registers or scheduling)".into());
        }
    }
    out
}
