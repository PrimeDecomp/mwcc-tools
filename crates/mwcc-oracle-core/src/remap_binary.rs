//! Carry executable function metadata and bindings using nearby wildcard matches.
use crate::{
    compare, executable, investigate,
    pe::{Pe, u32le},
    x86,
};
use anyhow::{Context, Result};
use indexmap::IndexMap;
use regex::Regex;
use serde::Serialize;
use serde_json::{Value, json};
use std::{collections::BTreeSet, fs, path::Path};
fn address(f: &Value) -> Result<u32> {
    executable::hex(
        f["address"]
            .as_str()
            .context("function lacks hexadecimal address")?,
    )
}
pub fn wildcards(
    pe: &Pe<'_>,
    fixups: &[u32],
    address: u32,
    size: usize,
) -> Result<(Vec<u8>, BTreeSet<usize>)> {
    let code = pe.read(address, size)?.to_vec();
    let mut wild = BTreeSet::new();
    for at in fixups
        .iter()
        .copied()
        .filter(|a| *a >= address && u64::from(*a) < u64::from(address) + size as u64)
    {
        wild.extend(at as usize - address as usize..(at as usize - address as usize + 4).min(size));
    }
    if let Ok(branches) = x86::branches(&code, address) {
        for (field, _) in branches {
            wild.extend(field as usize..(field as usize + 4).min(size));
        }
    }
    Ok((code, wild))
}
/// Python's fixed-width DOTALL pattern search, including short/all-wild patterns.
pub fn pattern_matches(
    blob: &[u8],
    code: &[u8],
    wild: &BTreeSet<usize>,
    low: usize,
    high: usize,
) -> Vec<usize> {
    let high = high.min(blob.len());
    if low > high || code.len() > high - low {
        return vec![];
    }
    let mut runs = vec![];
    let mut start = None;
    for i in 0..=code.len() {
        if i == code.len() || wild.contains(&i) {
            if let Some(begin) = start.take() {
                runs.push(begin..i);
            }
        } else if start.is_none() {
            start = Some(i);
        }
    }
    (low..=high - code.len())
        .filter(|at| {
            runs.iter()
                .all(|r| blob[*at + r.start..*at + r.end] == code[r.clone()])
        })
        .collect()
}
#[derive(Debug, Serialize)]
pub struct Matches {
    pub found: IndexMap<u32, u32>,
    pub missing: Vec<Value>,
}
pub fn match_functions(
    functions: &[Value],
    a: &Pe<'_>,
    fix_a: &[u32],
    b: &Pe<'_>,
) -> Result<Matches> {
    let text_b = b
        .sections
        .iter()
        .find(|s| s.name == ".text")
        .context("destination has no .text")?;
    let start_b = text_b.virtual_address;
    let blob = b.read(start_b, text_b.virtual_size as usize)?;
    let start_a = a
        .sections
        .iter()
        .find(|s| s.name == ".text")
        .context("source has no .text")?
        .virtual_address;
    let mut delta = i64::from(start_b) - i64::from(start_a);
    let mut sorted = functions
        .iter()
        .map(|f| Ok((address(f)?, f)))
        .collect::<Result<Vec<_>>>()?;
    sorted.sort_by_key(|(a, _)| *a);
    let mut found = IndexMap::new();
    let mut missing = vec![];
    let mut taken = BTreeSet::new();
    for (address, f) in sorted {
        let size = f["size"].as_u64().context("function lacks size")? as usize;
        let (code, wild) = wildcards(a, fix_a, address, size)?;
        let guess = i64::from(address) + delta - i64::from(start_b);
        let low = (guess - 0x1800).max(0) as usize;
        let high = (guess + size as i64 + 0x1800).min(blob.len() as i64).max(0) as usize;
        let candidates: Vec<_> = pattern_matches(blob, &code, &wild, low, high)
            .into_iter()
            .filter(|p| !taken.contains(p))
            .collect();
        if candidates.is_empty() || candidates.len() > 1 && size.saturating_sub(wild.len()) < 8 {
            missing.push(f.clone());
            continue;
        }
        let best = *candidates
            .iter()
            .min_by_key(|p| (**p as i64 - guess).unsigned_abs())
            .unwrap();
        taken.insert(best);
        let moved = start_b
            .checked_add(best.try_into()?)
            .context("remapped address overflow")?;
        found.insert(address, moved);
        delta = i64::from(moved) - i64::from(address);
    }
    Ok(Matches { found, missing })
}
pub fn data_pairs(
    functions: &[Value],
    found: &IndexMap<u32, u32>,
    a: &Pe<'_>,
    fixups: &[u32],
    b: &Pe<'_>,
) -> Result<IndexMap<u32, u32>> {
    let mut pairs = IndexMap::new();
    let mut conflicts = BTreeSet::new();
    for f in functions {
        let address = address(f)?;
        let size = f["size"].as_u64().context("function lacks size")?;
        let Some(other) = found.get(&address) else {
            continue;
        };
        for at in fixups
            .iter()
            .copied()
            .filter(|at| *at >= address && u64::from(*at) + 4 <= u64::from(address) + size)
        {
            let value_a = u32le(a.read(at, 4)?, 0)?;
            let value_b = u32le(
                b.read(
                    other
                        .checked_add(at - address)
                        .context("paired fixup address overflow")?,
                    4,
                )?,
                0,
            )?;
            if *pairs.entry(value_a).or_insert(value_b) != value_b {
                conflicts.insert(value_a);
            }
        }
    }
    pairs.retain(|k, _| !conflicts.contains(k));
    Ok(pairs)
}
pub fn carry(address: u32, pairs: &IndexMap<u32, u32>, pe: &Pe<'_>) -> Result<Option<u32>> {
    if let Some(value) = pairs.get(&address) {
        return Ok(Some(*value));
    }
    let mut known: Vec<_> = pairs.keys().copied().collect();
    known.sort();
    let k = known.partition_point(|a| *a <= address);
    if k == 0 || k >= known.len() {
        return Ok(None);
    }
    let low = known[k - 1];
    let high = known[k];
    let delta = i64::from(pairs[&low]) - i64::from(low);
    if delta == i64::from(pairs[&high]) - i64::from(high)
        && std::ptr::eq(
            pe.section_for_address(address)?,
            pe.section_for_address(low)?,
        )
        && std::ptr::eq(pe.section_for_address(low)?, pe.section_for_address(high)?)
    {
        return Ok(Some(
            (i64::from(address) + delta)
                .try_into()
                .context("carried address overflow")?,
        ));
    }
    Ok(None)
}
pub fn run(root: &Path, from: &str, to: &str, symbols: bool) -> Result<String> {
    let dotted = |v: &str| {
        investigate::folder(v)
            .strip_prefix("GC_")
            .unwrap()
            .replace('_', ".")
    };
    let version_a = dotted(from);
    let version_b = dotted(to);
    let data_a = fs::read(root.join(format!("build/compilers/GC/{version_a}/mwcceppc.exe")))?;
    let data_b = fs::read(root.join(format!("build/compilers/GC/{version_b}/mwcceppc.exe")))?;
    let a = Pe::parse(&data_a)?;
    let b = Pe::parse(&data_b)?;
    executable::base_relocations(&b)?;
    let fix_a = executable::base_relocations(&a)?;
    let source = root.join("config").join(investigate::folder(from));
    let mut functions: Vec<Value> =
        serde_json::from_slice(&fs::read(source.join("functions.json"))?)?;
    if symbols {
        let listed: BTreeSet<_> = functions.iter().map(address).collect::<Result<_>>()?;
        let regex =
            Regex::new(r"^(\S+) = \.text:0x([0-9A-Fa-f]+);.*type:function size:0x([0-9A-Fa-f]+)")?;
        for line in fs::read_to_string(source.join("symbols.txt"))?.lines() {
            if let Some(c) = regex.captures(line) {
                let address = u32::from_str_radix(&c[2], 16)?;
                if !listed.contains(&address) {
                    functions.push(json!({"name":c[1].strip_prefix('_').unwrap_or(&c[1]),"address":format!("0x{address:08x}"),"size":u32::from_str_radix(&c[3],16)?,"carried":true}));
                }
            }
        }
    }
    let bindings: IndexMap<String, String> =
        serde_json::from_slice(&fs::read(source.join("bindings.json"))?)?;
    let matches = match_functions(&functions, &a, &fix_a, &b)?;
    let mut pairs = data_pairs(&functions, &matches.found, &a, &fix_a, &b)?;
    pairs.extend(matches.found.clone());
    let generated = Regex::new(r"^fn_[0-9a-f]{8}$")?;
    let mut output = vec![];
    for f in &functions {
        let address = address(f)?;
        if let Some(moved) = matches.found.get(&address) {
            let mut row = f.clone();
            row["address"] = json!(format!("0x{moved:08x}"));
            let carried = row
                .as_object_mut()
                .context("function is not an object")?
                .remove("carried")
                .is_some_and(|v| v == true);
            if carried && generated.is_match(f["name"].as_str().context("function lacks name")?) {
                row["name"] = json!(format!("fn_{moved:08x}"));
            }
            output.push(row);
        }
    }
    output.sort_by_key(|f| address(f).unwrap());
    let target = format!("config/{}", investigate::folder(to));
    compare::write_json(root, &format!("{target}/functions.json"), &json!(output))?;
    let mut carried = IndexMap::new();
    let mut lost = vec![];
    for (name, text) in &bindings {
        if let Some(address) = carry(executable::hex(text)?, &pairs, &a)? {
            carried.insert(name.clone(), format!("0x{address:08x}"));
        } else {
            lost.push(name);
        }
    }
    compare::write_json(root, &format!("{target}/bindings.json"), &json!(carried))?;
    let mut report = format!(
        "{} of {} functions found in {to}; {} differ or are not found\n{} of {} bindings carried; {} lost\n",
        matches.found.len(),
        functions.len(),
        matches.missing.len(),
        carried.len(),
        bindings.len(),
        lost.len()
    );
    for f in matches.missing {
        report.push_str(&format!(
            "  {} {} ({} bytes) {}\n",
            f["address"].as_str().unwrap_or("?"),
            f["name"].as_str().unwrap_or("?"),
            f["size"],
            f["source"].as_str().unwrap_or("")
        ));
    }
    for name in lost {
        report.push_str(&format!("  binding {name} = {}\n", bindings[name]));
    }
    Ok(report)
}
