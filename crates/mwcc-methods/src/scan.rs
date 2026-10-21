//! `project.scan`: instruction patterns over a project's units, the target's objects against the build's, and each
//! function's instructions as JSON (docs/scan.md).
use crate::{Call, Method};
use anyhow::{Context, Result};
use mwcc_compare::strict::elf::{self, Function};
use schemars::JsonSchema;
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::{
    collections::{BTreeMap, BTreeSet},
    path::PathBuf,
};

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct ScanParams {
    pub project: Option<PathBuf>,
    /// The units to scan (objdiff.json's names, or a part of them); none: every unit with a target and a built
    /// object.
    #[serde(default)]
    pub units: Vec<String>,
    /// The patterns to look for (default: all): `literal_reload` (one symbol, a literal or a global, loaded twice in a
    /// function) and `repeated_load` (the same load, of one base register and offset or of one symbol, twice with no
    /// store, call or write of the base register between them, in the order of the code: a branch between them is not
    /// followed).
    #[serde(default)]
    pub patterns: Vec<String>,
    /// Only the functions whose names have this.
    pub function: Option<String>,
    /// Each scanned function's instructions too (`{offset, text, symbol}`, target and ours), for scans of one's own.
    #[serde(default)]
    pub instructions: bool,
    /// Only the functions one side has a pattern in and the other has not (the whole of Echoes' scan is 2 MB).
    #[serde(default)]
    pub differing: bool,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct ScanResult {
    pub units: usize,
    /// For each pattern, the functions of the target and of ours that have it, and those that only one side has.
    #[schemars(with = "BTreeMap<String, Value>")]
    pub patterns: BTreeMap<String, Value>,
    /// With `instructions`: each function's, by unit.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    #[schemars(with = "Vec<Value>")]
    pub functions: Vec<Value>,
    /// Units skipped: no target, no built object, or an object that does not load.
    pub skipped: Vec<String>,
}

const PATTERNS: [&str; 2] = ["literal_reload", "repeated_load"];

/// One instruction: its offset, text and the symbol its relocation names.
struct Instruction {
    offset: u32,
    text: String,
    symbol: Option<String>,
}

fn instructions(f: &Function) -> Vec<Instruction> {
    let symbols: BTreeMap<u32, &str> = f
        .relocs
        .iter()
        .map(|r| (r.offset & !3, r.target.as_str()))
        .collect();
    f.words()
        .enumerate()
        .map(|(i, word)| {
            let offset = 4 * i as u32;
            Instruction {
                offset,
                text: elf::disassemble_word(word, offset),
                symbol: symbols.get(&offset).map(|s| s.to_string()),
            }
        })
        .collect()
}

fn mnemonic(text: &str) -> &str {
    text.split_whitespace().next().unwrap_or("")
}

fn is_load(text: &str) -> bool {
    let m = mnemonic(text);
    (m.starts_with('l') || m.starts_with("psq_l"))
        && !matches!(m, "li" | "lis" | "lmw" | "lwarx" | "lswi" | "lswx")
}

fn is_store(text: &str) -> bool {
    let m = mnemonic(text);
    m.starts_with("st") || m.starts_with("psq_st") || m.starts_with("dcb")
}

/// A load's destination and its base register and displacement (`lwz r3, 0x10(r31)`: `r3`, `r31`, `0x10`).
fn load_operands(text: &str) -> Option<(&str, &str, &str)> {
    let operands = text.split_once(' ')?.1;
    let (destination, address) = operands.split_once(", ")?;
    let (displacement, base) = address.strip_suffix(')')?.split_once('(')?;
    Some((destination.trim(), base, displacement))
}

/// Whether TEXT writes the register REGISTER (its first operand, for an instruction that writes one).
fn writes(text: &str, register: &str) -> bool {
    let m = mnemonic(text);
    if is_store(text) && !m.contains('u') || m.starts_with("cmp") || m.starts_with('b') {
        return false;
    }
    let operands = text.split_once(' ').map_or("", |o| o.1);
    let first = operands.split(", ").next().unwrap_or("");
    first == register
        || (m.ends_with('u') || m.ends_with("ux")) && load_operands(text).is_some_and(|o| o.1 == register)
}

/// The places PATTERN occurs in INSTRUCTIONS: each a list of its instructions' offsets and texts.
fn find(pattern: &str, instructions: &[Instruction]) -> Vec<Value> {
    let mut hits = vec![];
    match pattern {
        "literal_reload" => {
            let mut by_symbol: BTreeMap<&str, Vec<&Instruction>> = BTreeMap::new();
            for i in instructions.iter().filter(|i| is_load(&i.text)) {
                if let Some(symbol) = &i.symbol {
                    by_symbol.entry(symbol).or_default().push(i);
                }
            }
            for (symbol, loads) in by_symbol.into_iter().filter(|(_, l)| l.len() > 1) {
                hits.push(json!({
                    "symbol": symbol,
                    "loads": loads.iter().map(|i| json!([format!("{:#x}", i.offset), i.text])).collect::<Vec<_>>(),
                }));
            }
        }
        "repeated_load" => {
            for (at, first) in instructions.iter().enumerate() {
                if !is_load(&first.text) {
                    continue;
                }
                let Some((destination, base, _)) = load_operands(&first.text) else {
                    continue;
                };
                // A load of its own base (a pointer chase) or an update form changes what a second one reads.
                let m = mnemonic(&first.text);
                if first.symbol.is_none() && (base == destination || m.ends_with('u') || m.ends_with("ux")) {
                    continue;
                }
                for later in &instructions[at + 1..] {
                    if later.text == first.text && later.symbol == first.symbol {
                        hits.push(json!({
                            "load": first.text,
                            "symbol": first.symbol,
                            "at": [format!("{:#x}", first.offset), format!("{:#x}", later.offset)],
                        }));
                        break;
                    }
                    let m = mnemonic(&later.text);
                    if is_store(&later.text)
                        || m.starts_with("bl")
                        || m == "bctrl"
                        // A relocated load's base (r2, r13 or its high half's register) is not in its text.
                        || first.symbol.is_none() && writes(&later.text, base)
                    {
                        break;
                    }
                }
            }
        }
        _ => {}
    }
    hits
}

fn scan(call: &mut Call<'_>, params: ScanParams) -> Result<ScanResult> {
    let project = call.session.project(params.project.as_deref())?;
    let patterns: Vec<String> = if params.patterns.is_empty() {
        PATTERNS.iter().map(|p| p.to_string()).collect()
    } else {
        params.patterns.clone()
    };
    for p in &patterns {
        anyhow::ensure!(
            PATTERNS.contains(&p.as_str()),
            "unknown pattern {p} (the patterns: {})",
            PATTERNS.join(", ")
        );
    }
    let mut names = project.unit_names()?;
    if !params.units.is_empty() {
        names.retain(|n| params.units.iter().any(|u| n == u || n.contains(u.as_str())));
    }
    // (pattern -> side -> function -> hits)
    let mut found: BTreeMap<String, [BTreeMap<String, Vec<Value>>; 2]> = BTreeMap::new();
    let mut functions = vec![];
    let mut skipped = vec![];
    let mut scanned = 0;
    for name in &names {
        if (call.cancel)() {
            anyhow::bail!("cancelled");
        }
        let Ok(unit) = project.unit(name) else {
            skipped.push(format!("{name}: no compile edge"));
            continue;
        };
        let (Some(target), base) = (unit.target_path.clone(), unit.base_path.clone()) else {
            skipped.push(format!("{name}: no target"));
            continue;
        };
        let load = |path: &PathBuf| -> Result<elf::Object> {
            elf::load(&std::fs::read(path).with_context(|| format!("read {}", path.display()))?)
        };
        let (target, ours) = match (load(&target), load(&base)) {
            (Ok(t), Ok(o)) => (t, o),
            (Err(e), _) | (_, Err(e)) => {
                skipped.push(format!("{name}: {e:#}"));
                continue;
            }
        };
        scanned += 1;
        for (side, object) in [&target, &ours].into_iter().enumerate() {
            for f in &object.functions {
                if params.function.as_deref().is_some_and(|w| !f.name.contains(w)) {
                    continue;
                }
                let listed = instructions(f);
                for p in &patterns {
                    let hits = find(p, &listed);
                    if !hits.is_empty() {
                        found.entry(p.clone()).or_default()[side]
                            .insert(format!("{name}: {}", f.name), hits);
                    }
                }
                if params.instructions {
                    functions.push(json!({
                        "unit": name,
                        "side": if side == 0 { "target" } else { "ours" },
                        "function": f.name,
                        "instructions": listed.iter().map(|i| json!({
                            "offset": i.offset, "text": i.text, "symbol": i.symbol,
                        })).collect::<Vec<_>>(),
                    }));
                }
            }
        }
    }
    let patterns = found
        .into_iter()
        .map(|(p, [mut target, mut ours])| {
            let in_target: BTreeSet<String> = target.keys().cloned().collect();
            let in_ours: BTreeSet<String> = ours.keys().cloned().collect();
            if params.differing {
                target.retain(|f, _| !in_ours.contains(f));
                ours.retain(|f, _| !in_target.contains(f));
            }
            let value = json!({
                "target": target,
                "ours": ours,
                "target_only": in_target.difference(&in_ours).collect::<Vec<_>>(),
                "ours_only": in_ours.difference(&in_target).collect::<Vec<_>>(),
            });
            (p, value)
        })
        .collect();
    Ok(ScanResult {
        units: scanned,
        patterns,
        functions,
        skipped,
    })
}

pub(crate) fn methods() -> Vec<Method> {
    vec![crate::method!(
        "project.scan",
        "instruction patterns over a project's units, the targets' objects against the build's (literal_reload, repeated_load): the functions each side has them in, and those only one has; each function's instructions as JSON",
        ScanParams,
        ScanResult,
        scan
    )]
}

#[cfg(test)]
mod tests {
    use super::*;

    fn listing(lines: &[(&str, Option<&str>)]) -> Vec<Instruction> {
        lines
            .iter()
            .enumerate()
            .map(|(i, (text, symbol))| Instruction {
                offset: 4 * i as u32,
                text: text.to_string(),
                symbol: symbol.map(str::to_string),
            })
            .collect()
    }

    #[test]
    fn finds_reloads_and_repeated_loads() {
        let code = listing(&[
            ("lfs f1, 0x0(r0)", Some("@12")),
            ("lwz r3, 0x10(r31)", None),
            ("fadds f1, f1, f2", None),
            ("lwz r3, 0x10(r31)", None),
            ("lfs f2, 0x0(r0)", Some("@12")),
            ("lfs f3, 0x0(r0)", Some("@13")),
            ("stw r0, 0x8(r1)", None),
            ("lwz r3, 0x10(r31)", None),
            ("addi r31, r31, 0x4", None),
            ("lwz r3, 0x10(r31)", None),
            ("lwz r4, 0x0(r4)", None),
            ("lwz r4, 0x0(r4)", None),
            ("lwzu r5, 0x4(r6)", None),
            ("lwzu r5, 0x4(r6)", None),
        ]);
        let reloads = find("literal_reload", &code);
        assert_eq!(reloads.len(), 1);
        assert_eq!(reloads[0]["symbol"], "@12");
        let repeated = find("repeated_load", &code);
        // The store ends the first run, the write of r31 the second; the two @12 loads differ in their text.
        assert_eq!(repeated.len(), 1, "{repeated:?}");
        assert_eq!(repeated[0]["at"], json!(["0x4", "0xc"]));
    }
}
