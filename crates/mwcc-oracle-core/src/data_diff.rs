//! Relocation-aware data layout comparisons for PowerPC objects and target assembly.
use crate::{
    elf::{Elf, Symbol},
    sequence,
};
use anyhow::{Result, ensure};
use indexmap::IndexMap;
use serde_json::{Value, json};
use std::collections::{BTreeMap, BTreeSet};
mod asm;
mod report;
pub use asm::asm_layout;
pub use report::{line, report};
pub type Layout = IndexMap<String, Value>;
pub(super) fn a(v: &Value) -> &[Value] {
    v.as_array().map_or(&[], Vec::as_slice)
}
pub(super) fn n(v: &Value) -> i64 {
    v.as_i64().unwrap_or(0)
}
pub(super) fn s(v: &Value) -> &str {
    v.as_str().unwrap_or("")
}
pub(super) fn ignored(name: &str) -> bool {
    [
        ".text",
        ".init",
        "extab",
        "extabindex",
        ".comment",
        ".debug",
        ".line",
        ".mwcats",
        ".note",
        ".rela",
        ".symtab",
        ".strtab",
        ".shstrtab",
    ]
    .iter()
    .any(|p| name.starts_with(p))
}
pub fn anonymous(name: &str) -> bool {
    ["@", "lbl_", ".L_", "..."]
        .iter()
        .any(|p| name.starts_with(p))
}
fn hex(data: &[u8]) -> String {
    data.iter().map(|b| format!("{b:02x}")).collect()
}
fn repr(text: &str) -> String {
    let quote = if text.contains('\'') && !text.contains('"') {
        '"'
    } else {
        '\''
    };
    let mut out = quote.to_string();
    for c in text.chars() {
        match c {
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if c == quote => {
                out.push('\\');
                out.push(c);
            }
            c if c.is_control() && (c as u32) <= 255 => {
                out.push_str(&format!("\\x{:02x}", c as u32))
            }
            c => out.push(c),
        }
    }
    out.push(quote);
    out
}
fn symbols(offset: i64, relocations: &BTreeMap<i64, String>) -> String {
    let terms: Vec<_> = relocations
        .iter()
        .map(|(o, s)| {
            format!(
                "({}, {})",
                o - offset,
                repr(if anonymous(s) || s.contains('+') {
                    "*"
                } else {
                    s
                })
            )
        })
        .collect();
    format!(
        "({}{})",
        terms.join(", "),
        if terms.len() == 1 { "," } else { "" }
    )
}
fn general(value: f64) -> String {
    if value.is_nan() {
        return "nan".into();
    }
    if value.is_infinite() {
        return if value < 0.0 { "-inf" } else { "inf" }.into();
    }
    let e = format!("{value:.8e}");
    let (mantissa, exponent) = e.split_once('e').unwrap();
    let exponent = exponent.parse::<i32>().unwrap();
    if !(-4..9).contains(&exponent) {
        format!(
            "{}e{exponent:+03}",
            mantissa.trim_end_matches('0').trim_end_matches('.')
        )
    } else {
        let places = (8 - exponent).max(0) as usize;
        let s = format!("{value:.places$}");
        if s.contains('.') {
            s.trim_end_matches('0').trim_end_matches('.').into()
        } else {
            s
        }
    }
}
fn render(section: &str, data: &[u8], relocations: &BTreeMap<i64, String>) -> String {
    if relocations.is_empty()
        && matches!(data.len(), 4 | 8)
        && [".sdata2", ".sbss2", ".rodata"]
            .iter()
            .any(|p| section.starts_with(p))
    {
        let (kind, value) = if data.len() == 4 {
            ("float", f32::from_be_bytes(data.try_into().unwrap()) as f64)
        } else {
            ("double", f64::from_be_bytes(data.try_into().unwrap()))
        };
        return format!("{kind} {}", general(value));
    }
    let mut out = vec![];
    let mut at = 0;
    for (&offset, symbol) in relocations {
        let offset = offset as usize;
        if offset > at {
            out.push(hex(&data[at..offset.min(data.len())]));
        }
        out.push(format!("&{symbol}"));
        at = offset + 4;
    }
    if at < data.len() {
        out.push(if data[at..].iter().any(|b| *b != 0) {
            hex(&data[at..])
        } else {
            format!("zero*{}", data.len() - at)
        });
    }
    let text = out.join(" ");
    if text.chars().count() > 72 {
        format!("{}...", text.chars().take(69).collect::<String>())
    } else {
        text
    }
}
fn item(
    section: &str,
    offset: i64,
    name: &str,
    data: &[u8],
    relocations: &BTreeMap<i64, String>,
    users: &[String],
) -> Value {
    let symbols = symbols(offset, relocations);
    let relative = relocations
        .iter()
        .map(|(o, s)| (o - offset, s.clone()))
        .collect();
    json!({"section":section,"offset":offset,"size":data.len(),"name":name,"value":render(section,data,&relative),"key":format!("{}{symbols}",hex(data)),"bytes":hex(data),"symbols":symbols,"users":users})
}
fn strings_of(data: &[u8]) -> Option<Vec<String>> {
    if data.last() != Some(&0) {
        return None;
    }
    let end = data.iter().rposition(|b| *b != 0)? + 1;
    let parts: Vec<_> = data[..end].split(|b| *b == 0).collect();
    if parts.iter().any(|p| {
        p.is_empty()
            || p.iter()
                .any(|b| !((32..127).contains(b) || [9, 10, 13].contains(b)))
    }) {
        return None;
    }
    Some(
        parts
            .iter()
            .map(|p| p.iter().map(|b| char::from(*b)).collect())
            .collect(),
    )
}
pub(super) fn split(
    section: &str,
    offset: i64,
    name: &str,
    data: &[u8],
    relocations: &BTreeMap<i64, String>,
    users: &[String],
    padding: bool,
) -> Vec<Value> {
    if padding {
        return vec![
            json!({"section":section,"offset":offset,"size":data.len(),"name":"(padding)","value":format!("pad {}",data.len()),"key":format!("pad {}",data.len()),"bytes":hex(data),"symbols":"()","users":[]}),
        ];
    }
    let text = relocations.is_empty()
        && (!matches!(data.len(), 4 | 8)
            || name.starts_with("@stringBase")
            || (data.last() == Some(&0)
                && data.get(data.len() - 2) != Some(&0)
                && data.iter().take(3).all(|b| *b != 0)));
    if text && let Some(strings) = strings_of(data) {
        let mut at = offset;
        return strings.iter().map(|text|{let size=text.len()+1;let i=json!({"section":section,"offset":at,"size":size,"name":name,"value":serde_json::to_string(text).unwrap(),"key":format!("str {text}"),"bytes":format!("{}00",hex(text.as_bytes())),"symbols":"()","users":users});at+=size as i64;i}).collect();
    }
    vec![item(section, offset, name, data, relocations, users)]
}
pub(super) fn trim(items: &mut Vec<Value>) {
    while items.last().is_some_and(|i| i["name"] == "(padding)") {
        items.pop();
    }
}
pub fn object_layout(data: &[u8]) -> Result<Layout> {
    let elf = Elf::parse(data)?;
    let symbols = elf.symbols()?;
    let mut functions: Vec<_> = symbols.iter().filter(|s| s.kind == 2).collect();
    functions.sort_by_key(|s| (s.section, s.value, s.value.saturating_add(s.size), &s.name));
    let holder = |index: usize, offset: i64| {
        symbols.iter().find(|s| {
            s.section == index
                && matches!(s.kind, 0 | 1)
                && !s.name.is_empty()
                && i64::from(s.value) <= offset
                && offset < i64::from(s.value) + i64::from(s.size.max(1))
        })
    };
    let mut relocations: BTreeMap<usize, BTreeMap<i64, String>> = BTreeMap::new();
    let mut users: IndexMap<String, BTreeSet<(u64, String)>> = IndexMap::new();
    for r in elf.relocations()? {
        let mut name = r.symbol.name.clone();
        let mut addend = i64::from(r.addend);
        if r.symbol.kind == 3 {
            if let Some(h) = holder(r.symbol.section, addend) {
                name = h.name.clone();
                addend -= i64::from(h.value);
            } else {
                name = elf
                    .sections
                    .get(r.symbol.section)
                    .map_or("", |s| s.name.as_str())
                    .into();
            }
        }
        if addend != 0 {
            name = format!("{name}+{addend:#x}");
        }
        let target = name.split('+').next().unwrap().to_owned();
        if elf.sections[r.section].flags & 4 != 0 {
            if let Some(f) = functions.iter().find(|f| {
                f.section == r.section
                    && f.value <= r.offset
                    && r.offset < f.value.saturating_add(f.size)
            }) {
                users
                    .entry(target)
                    .or_default()
                    .insert((u64::from(f.value), f.name.clone()));
            }
        } else {
            relocations
                .entry(r.section)
                .or_default()
                .insert(i64::from(r.offset), name);
            if let Some(h) = holder(r.section, i64::from(r.offset)) {
                users
                    .entry(target)
                    .or_default()
                    .insert((1u64 << 32, format!("(data) {}", h.name)));
            }
        }
    }
    let used = |symbol: &str| -> Vec<String> {
        users
            .get(symbol)
            .into_iter()
            .flatten()
            .map(|(_, f)| f.clone())
            .collect()
    };
    let mut layout = Layout::new();
    for (index, section) in elf.sections.iter().enumerate() {
        if section.flags & 2 == 0 || section.flags & 4 != 0 || ignored(&section.name) {
            continue;
        }
        // Cap uninitialized materialization to a reasonable artifact size, rather than trusting malformed input.
        ensure!(
            section.size <= 512 * 1024 * 1024,
            "data section too large to materialize"
        );
        let zeros;
        let content = if section.kind == 8 {
            zeros = vec![0; section.size as usize];
            &zeros[..]
        } else {
            section.data
        };
        let mut objects: Vec<&Symbol> = symbols
            .iter()
            .filter(|s| s.section == index && matches!(s.kind, 0 | 1) && s.size != 0)
            .collect();
        objects.sort_by_key(|s| (s.value, s.size, &s.name, s.binding));
        let mut items = vec![];
        let mut at = 0;
        for object in objects {
            let value = object.value as usize;
            let end = value
                .checked_add(object.size as usize)
                .ok_or_else(|| anyhow::anyhow!("object range overflow"))?;
            ensure!(
                end <= content.len(),
                "object {} exceeds data section",
                object.name
            );
            if value > at {
                items.extend(split(
                    &section.name,
                    at as i64,
                    "",
                    &content[at..value],
                    &BTreeMap::new(),
                    &[],
                    true,
                ));
            }
            let mine = relocations
                .get(&index)
                .map(|r| {
                    r.range(value as i64..end as i64)
                        .map(|(o, s)| (*o, s.clone()))
                        .collect()
                })
                .unwrap_or_default();
            let mut parts = split(
                &section.name,
                value as i64,
                &object.name,
                &content[value..end],
                &mine,
                &used(&object.name),
                false,
            );
            if let Some(last) = parts.last_mut() {
                if object.binding == 2 {
                    last["weak"] = json!(true);
                } else if object.binding == 0 {
                    last["local"] = json!(true);
                }
            }
            items.extend(parts);
            at = at.max(end);
        }
        let mut anchors = vec![];
        for s in symbols
            .iter()
            .filter(|s| s.section == index && s.name.starts_with("..."))
        {
            for f in used(&s.name) {
                if !anchors.contains(&f) {
                    anchors.push(f);
                }
            }
        }
        for i in &mut items {
            if a(&i["users"]).is_empty() && !anchors.is_empty() && i["name"] != "(padding)" {
                i["users"] = json!(anchors);
                i["via"] = json!("an anchor of the section");
            }
        }
        trim(&mut items);
        layout.insert(
            section.name.clone(),
            json!({"align":section.alignment,"size":section.size,"items":items}),
        );
    }
    let common: Vec<_> = symbols.iter().filter(|s| s.section == 0xfff2).collect();
    if !common.is_empty() {
        let mut items = vec![];
        for s in common {
            ensure!(s.size <= 512 * 1024 * 1024, "common object too large");
            items.push(item(
                "(common)",
                0,
                &s.name,
                &vec![0; s.size as usize],
                &BTreeMap::new(),
                &used(&s.name),
            ));
        }
        layout.insert("(common)".into(), json!({"align":0,"size":0,"items":items}));
    }
    Ok(layout)
}
pub fn absorb(mine: &[Value], theirs: &mut [Value]) {
    let Some(last) = theirs.last_mut() else {
        return;
    };
    if mine.iter().any(|i| i["key"] == last["key"]) {
        return;
    }
    for i in mine {
        let bytes = s(&last["bytes"]);
        let prefix = s(&i["bytes"]);
        if let Some(rest) = bytes.strip_prefix(prefix)
            && i["symbols"] == last["symbols"]
            && !rest.is_empty()
            && rest.len() < 16
            && rest.chars().all(|c| c == '0')
        {
            for key in ["size", "value", "key", "bytes"] {
                last[key] = i[key].clone();
            }
            return;
        }
    }
}
fn named(i: &Value) -> bool {
    !s(&i["name"]).is_empty() && i["name"] != "(padding)" && !anonymous(s(&i["name"]))
}
pub fn annotate(mine: &mut Layout, theirs: &mut Layout) {
    fn one(one: &mut Layout, other: &Layout) {
        let where_: BTreeMap<_, _> = other
            .iter()
            .flat_map(|(n, s)| {
                a(&s["items"])
                    .iter()
                    .filter(|i| named(i))
                    .map(move |i| (s_(&i["name"]), n.as_str()))
            })
            .collect();
        for (name, section) in one {
            for i in section["items"].as_array_mut().unwrap() {
                if named(i)
                    && let Some(other) = where_.get(s(&i["name"])).filter(|n| **n != name)
                {
                    i["elsewhere"] = json!(other);
                }
            }
        }
    }
    fn s_(v: &Value) -> &str {
        s(v)
    }
    one(mine, theirs);
    one(theirs, mine);
}
pub fn linked(items: &[Value]) -> Vec<Value> {
    items
        .iter()
        .filter(|i| i.get("dropped").is_none() && i["name"] != "(padding)")
        .cloned()
        .collect()
}
pub fn compare(mine: &[Value], theirs: &[Value]) -> Vec<Value> {
    let target: Vec<_> = theirs.iter().map(|i| s(&i["key"])).collect();
    let oracle: Vec<_> = mine.iter().map(|i| s(&i["key"])).collect();
    let mut out = vec![];
    let mut i = 0;
    let mut j = 0;
    for (ai, bj, size) in sequence::matching_blocks_with_autojunk(&target, &oracle, false) {
        let tag = if i < ai && j < bj {
            Some("replace")
        } else if i < ai {
            Some("delete")
        } else if j < bj {
            Some("insert")
        } else {
            None
        };
        if let Some(tag) = tag {
            out.push(json!({"tag":tag,"target":theirs[i..ai],"oracle":mine[j..bj]}));
        }
        i = ai + size;
        j = bj + size;
        if size > 0 {
            out.push(json!({"tag":"equal","target":theirs[ai..i],"oracle":mine[bj..j]}));
        }
    }
    for d in &mut out {
        if d["tag"] == "equal" {
            continue;
        }
        for (side, other, items_) in [("target", "oracle", mine), ("oracle", "target", theirs)] {
            let other_keys: BTreeSet<_> = a(&d[other])
                .iter()
                .map(|i| s(&i["key"]).to_owned())
                .collect();
            for i in d[side].as_array_mut().unwrap() {
                let found: Vec<_> = items_.iter().filter(|j| j["key"] == i["key"]).collect();
                if found.len() == 1 && !other_keys.contains(s(&i["key"])) {
                    i["moved"] = found[0]["offset"].clone();
                }
            }
        }
    }
    out
}
pub fn made_early(layout: &mut Layout, functions: &[String], compiled: &[String]) {
    let order: BTreeMap<_, _> = functions
        .iter()
        .enumerate()
        .map(|(n, f)| (f.as_str(), n))
        .collect();
    for section in layout.values_mut() {
        let items = section["items"].as_array_mut().unwrap();
        for i in items.iter_mut() {
            i["users"]
                .as_array_mut()
                .unwrap()
                .sort_by_key(|u| order.get(s(u)).copied().unwrap_or(order.len()));
        }
        let literals: Vec<_> = items
            .iter()
            .enumerate()
            .filter_map(|(index, i)| {
                a(&i["users"])
                    .first()
                    .and_then(|u| order.get(s(u)))
                    .filter(|_| {
                        s(&i["value"]).starts_with("float") || s(&i["value"]).starts_with("double")
                    })
                    .map(|at| (index, *at))
            })
            .collect();
        for (n, (index, at)) in literals.iter().enumerate() {
            if let Some((later, _)) = literals[n + 1..].iter().find(|(_, a)| a < at) {
                let after = if n > 0 {
                    items[literals[n - 1].0]["users"][0].clone()
                } else {
                    Value::Null
                };
                let before = items[*later]["users"][0].clone();
                items[*index]["made"] = json!([after, before]);
                if let Some(end) = compiled.iter().position(|f| f == s(&before)) {
                    let start = compiled
                        .iter()
                        .position(|f| f == s(&after))
                        .map_or(0, |n| n + 1);
                    let candidates: Vec<_> = compiled
                        .get(start..end)
                        .unwrap_or(&[])
                        .iter()
                        .filter(|f| !order.contains_key(f.as_str()))
                        .collect();
                    items[*index]["candidates"] = json!(candidates);
                }
            }
        }
    }
}

pub fn module_objects(
    root: &std::path::Path,
    own: &std::path::Path,
) -> Result<IndexMap<String, std::path::PathBuf>> {
    static OBJECT: std::sync::LazyLock<regex::Regex> = std::sync::LazyLock::new(|| {
        regex::Regex::new(r#"(?m)^\.obj\s+("[^"]*"|[^\s,]+)"#).unwrap()
    });
    let mut paths = vec![];
    crate::inline_diff::asm_paths(root, &mut paths)?;
    paths.sort();
    let mut index = IndexMap::new();
    for path in paths {
        let bytes = std::fs::read(&path)?;
        for c in OBJECT.captures_iter(&String::from_utf8_lossy(&bytes)) {
            index
                .entry(c[1].trim_matches('"').to_owned())
                .or_insert_with(|| path.clone());
        }
    }
    index.retain(|_, p| p != own);
    Ok(index)
}
pub fn prepare(
    mine: &mut Layout,
    theirs: &mut Layout,
    object: &[u8],
    trace: &crate::trace::Trace,
    asm: &str,
    module: &std::path::Path,
    own: &std::path::Path,
) -> Result<()> {
    for (name, ours) in mine.iter() {
        if let Some(theirs) = theirs.get_mut(name) {
            absorb(a(&ours["items"]), theirs["items"].as_array_mut().unwrap());
        }
    }
    annotate(mine, theirs);
    let elsewhere = module_objects(module, own)?;
    let functions = crate::inline_diff::asm_functions(asm);
    let mut text_order: IndexMap<_, _> = Elf::parse(object)?
        .symbols()?
        .into_iter()
        .filter(|s| s.kind == 2)
        .map(|s| (s.name, (s.section, s.value)))
        .collect();
    text_order.sort_by(|_, a, _, b| a.cmp(b));
    let text_order: Vec<_> = text_order.into_keys().collect();
    let mut compiled_order = vec![];
    for allocation in &trace.allocations {
        let link = if allocation.before.link.is_empty() {
            &allocation.before.function
        } else {
            &allocation.before.link
        };
        if text_order.contains(link) && !compiled_order.contains(link) {
            compiled_order.push(link.clone());
        }
    }
    let reverse = if compiled_order.is_empty() {
        compiled_order = text_order.clone();
        false
    } else {
        compiled_order.len() > 1
            && text_order
                .iter()
                .position(|f| f == compiled_order.first().unwrap())
                > text_order
                    .iter()
                    .position(|f| f == compiled_order.last().unwrap())
    };
    let mut order: Vec<_> = functions.keys().cloned().collect();
    if reverse {
        order.reverse();
    }
    made_early(theirs, &order, &compiled_order);
    let position: BTreeMap<_, _> = compiled_order
        .iter()
        .enumerate()
        .map(|(n, f)| (f.as_str(), n))
        .collect();
    for (name, section) in mine {
        for i in section["items"].as_array_mut().unwrap() {
            i["users"]
                .as_array_mut()
                .unwrap()
                .sort_by_key(|u| position.get(s(u)).copied().unwrap_or(position.len()));
            if name.starts_with(".ctors") || name.starts_with(".dtors") || i["name"] == "(padding)"
            {
                continue;
            }
            let users: Vec<_> = a(&i["users"])
                .iter()
                .map(s)
                .filter(|u| !u.starts_with("(data)"))
                .collect();
            if i["weak"] == true
                && let Some(elsewhere) = elsewhere.get(s(&i["name"]))
            {
                i["dropped"] = json!(
                    elsewhere
                        .strip_prefix(module)?
                        .to_string_lossy()
                        .replace('\\', "/")
                );
            } else if a(&i["users"]).is_empty() && i["local"] == true {
                i["dropped"] = json!("unreferenced");
            } else if i["local"] == true
                && !users.is_empty()
                && users.len() == a(&i["users"]).len()
                && !users.iter().any(|u| functions.contains_key(*u))
            {
                i["dropped"] = json!(format!(
                    "used by functions not in the target: {}",
                    users
                        .iter()
                        .take(2)
                        .map(|u| {
                            let chars: Vec<_> = u.chars().collect();
                            if chars.len() > 60 {
                                format!("{}...", chars[..57].iter().collect::<String>())
                            } else {
                                u.to_string()
                            }
                        })
                        .collect::<Vec<_>>()
                        .join(", ")
                ));
            }
        }
    }
    Ok(())
}
