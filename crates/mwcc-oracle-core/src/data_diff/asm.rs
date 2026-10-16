use super::{Layout, a, ignored, item, split, trim};
use anyhow::{Context, Result, bail, ensure};
use indexmap::IndexMap;
use regex::Regex;
use serde_json::{Value, json};
use std::{collections::BTreeMap, sync::LazyLock};
fn unquote(s: &str) -> &str {
    if s.starts_with('"') && s.len() >= 2 {
        &s[1..s.len() - 1]
    } else {
        s
    }
}
fn integer(s: &str) -> Result<i64> {
    let s = s.trim();
    let (sign, s) = if let Some(s) = s.strip_prefix('-') {
        (-1, s)
    } else if let Some(s) = s.strip_prefix('+') {
        (1, s)
    } else {
        (1, s)
    };
    Ok(sign
        * if let Some(s) = s.strip_prefix("0x") {
            i64::from_str_radix(s, 16)?
        } else {
            s.parse::<i64>()?
        })
}
fn section<'a>(layout: &'a mut Layout, name: &str) -> &'a mut Value {
    layout
        .entry(name.into())
        .or_insert(json!({"align":0,"size":0,"items":[],"at":0}))
}
fn flush(layout: &mut Layout, name: Option<&str>, loose: &mut Vec<u8>) {
    if let Some(name) = name
        && !loose.is_empty()
    {
        let s = section(layout, name);
        let at = s["at"].as_i64().unwrap();
        s["items"].as_array_mut().unwrap().extend(split(
            name,
            at,
            "",
            loose,
            &BTreeMap::new(),
            &[],
            true,
        ));
        s["at"] = json!(at + loose.len() as i64);
    }
    loose.clear();
}
fn escaped(raw: &str) -> Result<Vec<u8>> {
    let mut out = vec![];
    let mut chars = raw.chars().peekable();
    while let Some(c) = chars.next() {
        let c = if c != '\\' {
            c as u32
        } else {
            let c = chars
                .next()
                .context("trailing backslash in assembly string")?;
            match c {
                'a' => 7,
                'b' => 8,
                'f' => 12,
                'n' => 10,
                'r' => 13,
                't' => 9,
                'v' => 11,
                '\\' => 92,
                '\'' => 39,
                '"' => 34,
                '\n' => continue,
                'x' | 'u' | 'U' => {
                    let count = match c {
                        'x' => 2,
                        'u' => 4,
                        _ => 8,
                    };
                    let digits: String = chars.by_ref().take(count).collect();
                    ensure!(digits.len() == count, "truncated assembly string escape");
                    u32::from_str_radix(&digits, 16)?
                }
                '0'..='7' => {
                    let mut digits = c.to_string();
                    for _ in 0..2 {
                        if chars.peek().is_some_and(|c| matches!(c, '0'..='7')) {
                            digits.push(chars.next().unwrap());
                        } else {
                            break;
                        }
                    }
                    u32::from_str_radix(&digits, 8)?
                }
                c => {
                    out.push(b'\\');
                    c as u32
                }
            }
        };
        ensure!(c <= 255, "assembly string is not Latin-1");
        out.push(c as u8);
    }
    Ok(out)
}
struct Object {
    name: String,
    offset: i64,
    content: Vec<u8>,
    relocations: BTreeMap<i64, String>,
}
pub fn asm_layout(text: &str) -> Result<Layout> {
    static MARKER: LazyLock<Regex> =
        LazyLock::new(|| Regex::new(r#"^\.(fn|endfn)\s+("[^"]*"|[^\s,]+)"#).unwrap());
    static USER: LazyLock<Regex> =
        LazyLock::new(|| Regex::new(r#"("[^"]+"|[\w.$<>@,]+?)@(?:ha|h|l|sda21)\b"#).unwrap());
    static SECTION: LazyLock<Regex> = LazyLock::new(|| {
        Regex::new(r"^\.(text|data|rodata|bss|sdata|sbss|sdata2|sbss2)$|^\.section\s+([^\s,]+)")
            .unwrap()
    });
    static ALIGN: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^\.balign\s+(\d+)").unwrap());
    static COMMON: LazyLock<Regex> =
        LazyLock::new(|| Regex::new(r#"^\.comm\s+("[^"]*"|[^\s,]+),\s*(\w+)"#).unwrap());
    static OBJ: LazyLock<Regex> =
        LazyLock::new(|| Regex::new(r#"^\.obj\s+("[^"]*"|[^\s,]+)"#).unwrap());
    static DIRECTIVE: LazyLock<Regex> = LazyLock::new(|| {
        Regex::new(r"^\.(4byte|2byte|byte|float|double|skip|string|rel)\s+(.*)$").unwrap()
    });
    static INTEGER: LazyLock<Regex> =
        LazyLock::new(|| Regex::new(r"^-?(0x[0-9A-Fa-f]+|\d+)$").unwrap());
    let mut layout = Layout::new();
    let mut users: IndexMap<String, Vec<String>> = IndexMap::new();
    let mut name: Option<String> = None;
    let mut function: Option<String> = None;
    let mut current: Option<Object> = None;
    let mut loose = vec![];
    for (number, line) in text.lines().enumerate() {
        let text = if line.trim_start().starts_with("/*") {
            line.split_once("*/").map_or(line, |(_, s)| s).trim()
        } else {
            line.trim()
        };
        if text.is_empty() || text.starts_with('#') {
            continue;
        }
        if let Some(c) = MARKER.captures(text) {
            function = if &c[1] == "fn" {
                Some(unquote(&c[2]).into())
            } else {
                None
            };
            continue;
        }
        if let Some(function) = &function {
            for c in USER.captures_iter(text) {
                let name = unquote(
                    c[1].split_whitespace()
                        .last()
                        .unwrap()
                        .rsplit(',')
                        .next()
                        .unwrap(),
                );
                users.entry(name.into()).or_default().push(function.clone());
            }
            continue;
        }
        if let Some(c) = SECTION.captures(text) {
            flush(&mut layout, name.as_deref(), &mut loose);
            let section_name = if let Some(s) = c.get(1) {
                format!(".{}", s.as_str())
            } else {
                c[2].into()
            };
            section(&mut layout, &section_name);
            name = Some(section_name);
            continue;
        }
        if let Some(c) = ALIGN.captures(text)
            && let Some(name) = &name
            && current.is_none()
        {
            let s = section(&mut layout, name);
            if a(&s["items"]).is_empty() && loose.is_empty() {
                s["align"] = json!(s["align"].as_i64().unwrap().max(c[1].parse()?));
            }
            continue;
        }
        if let Some(c) = COMMON.captures(text) {
            let size = integer(&c[2])?;
            ensure!(
                (0..=512 * 1024 * 1024).contains(&size),
                "invalid assembly common size"
            );
            section(&mut layout, "(common)")["items"]
                .as_array_mut()
                .unwrap()
                .push(item(
                    "(common)",
                    0,
                    unquote(&c[1]),
                    &vec![0; size as usize],
                    &BTreeMap::new(),
                    &[],
                ));
            continue;
        }
        if let Some(c) = OBJ.captures(text)
            && let Some(name) = &name
        {
            flush(&mut layout, Some(name), &mut loose);
            current = Some(Object {
                name: unquote(&c[1]).into(),
                offset: section(&mut layout, name)["at"].as_i64().unwrap(),
                content: vec![],
                relocations: BTreeMap::new(),
            });
            continue;
        }
        if text.starts_with(".endobj")
            && let Some(obj) = current.take()
        {
            let name = name.as_deref().context("object has no assembly section")?;
            let s = section(&mut layout, name);
            s["items"].as_array_mut().unwrap().extend(split(
                name,
                obj.offset,
                &obj.name,
                &obj.content,
                &obj.relocations,
                &[],
                obj.name.starts_with("gap_"),
            ));
            s["at"] = json!(obj.offset + obj.content.len() as i64);
            continue;
        }
        let Some(c) = DIRECTIVE.captures(text) else {
            continue;
        };
        if name.as_deref().is_none_or(|s| s == ".text") {
            continue;
        }
        let kind = &c[1];
        let argument = &c[2];
        let (out, relocations) = if let Some(obj) = &mut current {
            (&mut obj.content, Some((&mut obj.relocations, obj.offset)))
        } else {
            (&mut loose, None)
        };
        match kind {
            "string" => {
                let arg = argument.trim();
                ensure!(
                    arg.len() >= 2,
                    "invalid assembly string on line {}",
                    number + 1
                );
                out.extend(escaped(&arg[1..arg.len() - 1])?);
                out.push(0);
            }
            "float" => out.extend((argument.parse::<f64>()? as f32).to_be_bytes()),
            "double" => out.extend(argument.parse::<f64>()?.to_be_bytes()),
            "skip" => {
                let size = integer(argument)?;
                ensure!(
                    (0..=512 * 1024 * 1024).contains(&size),
                    "invalid assembly skip size"
                );
                out.resize(
                    out.len()
                        .checked_add(size as usize)
                        .context("assembly size overflow")?,
                    0,
                );
            }
            "rel" => {
                if let Some((relocations, offset)) = relocations {
                    relocations.insert(
                        offset + out.len() as i64,
                        format!("{}+?", unquote(argument.split(',').next().unwrap().trim())),
                    );
                }
                out.extend([0; 4]);
            }
            "4byte" | "2byte" | "byte" => {
                let size = if kind == "4byte" {
                    4
                } else if kind == "2byte" {
                    2
                } else {
                    1
                };
                let mut relocations = relocations;
                for value in argument.split(',').map(str::trim) {
                    if INTEGER.is_match(value) {
                        let bytes = integer(value)?.to_be_bytes();
                        out.extend(&bytes[8 - size..]);
                    } else {
                        if let Some((relocations, offset)) = &mut relocations {
                            relocations.insert(*offset + out.len() as i64, unquote(value).into());
                        }
                        out.resize(out.len() + size, 0);
                    }
                }
            }
            _ => bail!("unsupported assembly data directive {kind}"),
        }
    }
    flush(&mut layout, name.as_deref(), &mut loose);
    static SYMBOL: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"'([^']+)'\)").unwrap());
    for section in layout.values() {
        for i in a(&section["items"]) {
            for c in SYMBOL.captures_iter(super::s(&i["symbols"])) {
                users
                    .entry(c[1].into())
                    .or_default()
                    .push(format!("(data) {}", super::s(&i["name"])));
            }
        }
    }
    for section in layout.values_mut() {
        section["size"] = section.as_object_mut().unwrap().remove("at").unwrap();
        let items = section["items"].as_array_mut().unwrap();
        for i in items.iter_mut() {
            let mut list = vec![];
            for user in users.get(super::s(&i["name"])).into_iter().flatten() {
                if !list.contains(user) {
                    list.push(user.clone());
                }
            }
            i["users"] = json!(list);
        }
        trim(items);
    }
    layout.retain(|name, _| name != ".text" && !ignored(name));
    Ok(layout)
}
