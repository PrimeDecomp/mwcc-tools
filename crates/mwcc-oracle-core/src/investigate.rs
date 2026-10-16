//! Relocation-masked function location, binding inference and cross-version matching.
use crate::{
    coff::Object,
    compare, executable,
    pe::{Pe, u32le},
    x86,
};
use anyhow::{Context, Result, ensure};
use indexmap::IndexMap;
use serde::Serialize;
use serde_json::Value;
use std::{
    collections::{BTreeMap, BTreeSet},
    fs,
    path::{Path, PathBuf},
};
pub fn folder(version: &str) -> String {
    if version.starts_with("GC_") {
        version.into()
    } else {
        format!("GC_{}", version.replace('.', "_"))
    }
}
fn configuration(root: &Path, version: &str) -> Result<Value> {
    Ok(serde_json::from_slice(&fs::read(root.join(format!(
        "config/{}/config.json",
        folder(version)
    )))?)?)
}
fn image(root: &Path, version: &str) -> Result<Vec<u8>> {
    let config = configuration(root, version)?;
    Ok(fs::read(
        root.join(
            config["original"]
                .as_str()
                .context("config lacks original")?,
        ),
    )?)
}
#[derive(Debug, Serialize)]
pub struct FunctionRelocation {
    pub offset: u32,
    pub symbol: String,
    pub kind: u16,
    pub addend: i32,
}
#[derive(Debug, Serialize)]
pub struct Function {
    pub name: String,
    pub code: Vec<u8>,
    pub wild: BTreeSet<usize>,
    pub relocations: Vec<FunctionRelocation>,
}
pub fn functions_of(object: &Object<'_>) -> Result<Vec<Function>> {
    let mut out = vec![];
    for (k, section) in object.sections.iter().enumerate() {
        if section.flags & 0x20 == 0 || section.size == 0 {
            continue;
        }
        let name = object
            .symbols
            .values()
            .find(|s| {
                s.section as usize == k + 1
                    && s.value == 0
                    && [2, 3].contains(&s.storage)
                    && s.kind & 0xf0 == 0x20
            })
            .map_or_else(|| format!("section{}", k + 1), |s| s.name.clone());
        let mut wild = BTreeSet::new();
        let mut relocations = vec![];
        for r in &section.relocations {
            wild.extend(r.offset as usize..r.offset as usize + 4);
            relocations.push(FunctionRelocation {
                offset: r.offset,
                symbol: object
                    .symbols
                    .get(&r.symbol)
                    .map_or_else(|| "?".into(), |s| s.name.clone()),
                kind: r.kind,
                addend: u32le(&section.data, r.offset as usize)? as i32,
            });
        }
        out.push(Function {
            name,
            code: section.data.to_vec(),
            wild,
            relocations,
        });
    }
    Ok(out)
}
pub fn find(blob: &[u8], code: &[u8], wild: &BTreeSet<usize>, limit: usize) -> Vec<usize> {
    let mut best = 0..0;
    let mut start = None;
    for i in 0..=code.len() {
        if i == code.len() || wild.contains(&i) {
            if let Some(begin) = start.take()
                && i - begin > best.len()
            {
                best = begin..i;
            }
        } else if start.is_none() {
            start = Some(i);
        }
    }
    if best.len() < 4 || code.len() > blob.len() || limit == 0 {
        return vec![];
    }
    let literal = &code[best.clone()];
    let mut out = vec![];
    for (at, window) in blob.windows(literal.len()).enumerate() {
        if window != literal || at < best.start {
            continue;
        }
        let start = at - best.start;
        if start + code.len() > blob.len() {
            continue;
        }
        if code
            .iter()
            .enumerate()
            .all(|(i, b)| wild.contains(&i) || blob[start + i] == *b)
        {
            out.push(start);
            if out.len() >= limit {
                break;
            }
        }
    }
    out
}
fn infer(pe: &Pe<'_>, functions: &[Function]) -> Result<IndexMap<String, i64>> {
    let text = pe
        .sections
        .iter()
        .find(|s| s.name == ".text")
        .context("image has no .text section")?;
    let blob = pe.read(text.virtual_address, text.virtual_size as usize)?;
    let mut found = IndexMap::new();
    let mut conflicts = BTreeSet::new();
    for function in functions {
        let places = find(blob, &function.code, &function.wild, 2);
        if places.len() != 1 {
            continue;
        }
        let base = u64::from(text.virtual_address) + places[0] as u64;
        for r in &function.relocations {
            let value = i64::from(u32le(blob, places[0] + r.offset as usize)?);
            let address = match r.kind {
                6 => value - i64::from(r.addend),
                20 => {
                    (value + base as i64 + i64::from(r.offset) + 4 - i64::from(r.addend))
                        & 0xffffffff
                }
                _ => continue,
            };
            let entry = found.entry(r.symbol.clone()).or_insert(address);
            if *entry != address {
                conflicts.insert(r.symbol.clone());
            }
        }
    }
    found.retain(|symbol, _| !conflicts.contains(symbol));
    Ok(found)
}
pub fn bindings_from(root: &Path, version: &str, source: &str) -> Result<IndexMap<String, i64>> {
    let data = image(root, version)?;
    let pe = Pe::parse(&data)?;
    let object = fs::read(root.join(format!("build/{}/compiled/{source}.obj", folder(version))))?;
    infer(&pe, &functions_of(&Object::parse(&object)?)?)
}
pub fn locate(root: &Path, version: &str, sources: &[String]) -> Result<String> {
    let data = image(root, version)?;
    let pe = Pe::parse(&data)?;
    let text = pe
        .sections
        .iter()
        .find(|s| s.name == ".text")
        .context("image has no .text section")?;
    let blob = pe.read(text.virtual_address, text.virtual_size as usize)?;
    let splits = executable::read_splits(&fs::read_to_string(
        root.join(format!("config/{}/splits.txt", folder(version))),
    )?)?;
    let mut ranges = vec![];
    for unit in &splits.units {
        for range in &unit.ranges {
            if range.section == ".text" {
                ranges.push((range.start, range.end, unit.name.as_str()));
            }
        }
    }
    ranges.sort();
    let mut output = String::new();
    for source in sources {
        output.push_str(&format!("{source}:\n"));
        let mut inside = 0;
        let mut outside = 0;
        let mut missing = 0;
        let object =
            fs::read(root.join(format!("build/{}/compiled/{source}.obj", folder(version))))?;
        for function in functions_of(&Object::parse(&object)?)? {
            let found = find(blob, &function.code, &function.wild, 4);
            if found.is_empty() {
                missing += 1;
                output.push_str(&format!(
                    "  {:48} {:6}  not found\n",
                    function.name,
                    function.code.len()
                ));
                continue;
            }
            let address = text
                .virtual_address
                .checked_add(found[0].try_into()?)
                .context("located address overflow")?;
            let k = ranges.partition_point(|r| r.0 <= address);
            let unit = k
                .checked_sub(1)
                .and_then(|k| (address < ranges[k].1).then_some(ranges[k].2));
            let mark = if unit == Some(source.as_str()) {
                inside += 1;
                String::new()
            } else {
                outside += 1;
                format!("  <- {}", unit.unwrap_or("None"))
            };
            output.push_str(&format!(
                "  {:48} {:6}  0x{address:08x}{}{mark}\n",
                function.name,
                function.code.len(),
                if found.len() > 1 { "  (ambiguous)" } else { "" }
            ));
        }
        let own = ranges
            .iter()
            .filter(|r| r.2 == source)
            .map(|r| format!("0x{:08x}-0x{:08x}", r.0, r.1))
            .collect::<Vec<_>>()
            .join(", ");
        output.push_str(&format!(
            "  {inside} in its unit, {outside} in others, {missing} not found; unit range {own}\n"
        ));
    }
    Ok(output)
}
fn object_paths(directory: &Path, out: &mut Vec<PathBuf>) -> Result<()> {
    if !directory.is_dir() {
        return Ok(());
    }
    for entry in fs::read_dir(directory)? {
        let entry = entry?;
        let file_type = entry.file_type()?;
        if file_type.is_dir() {
            object_paths(&entry.path(), out)?;
        } else if file_type.is_file() && entry.path().extension().is_some_and(|s| s == "obj") {
            out.push(entry.path());
        }
    }
    Ok(())
}
fn signed_hex(value: i64, width: usize) -> String {
    if value < 0 {
        format!(
            "-{:0width$x}",
            value.unsigned_abs(),
            width = width.saturating_sub(1)
        )
    } else {
        format!("{value:0width$x}")
    }
}
pub fn bindfrom(root: &Path, version: &str) -> Result<String> {
    let folder = folder(version);
    let path = format!("config/{folder}/bindings.json");
    let mut bindings: IndexMap<String, String> =
        serde_json::from_slice(&fs::read(root.join(&path))?)?;
    let compiled = root.join(format!("build/{folder}/compiled"));
    let mut objects = vec![];
    object_paths(&compiled, &mut objects)?;
    objects.sort();
    let mut added = 0;
    let mut conflicts = vec![];
    for object in objects {
        let source = object
            .strip_prefix(&compiled)?
            .to_string_lossy()
            .replace('\\', "/");
        let source = source.strip_suffix(".obj").unwrap();
        for (symbol, address) in bindings_from(root, version, source)? {
            if symbol.is_empty() || ["_@", "@", ".", "$"].iter().any(|p| symbol.starts_with(p)) {
                continue;
            }
            if let Some(bound) = bindings.get(&symbol) {
                let bound_address =
                    i64::from_str_radix(bound.strip_prefix("0x").unwrap_or(bound), 16)?;
                if bound_address != address {
                    conflicts.push((
                        symbol,
                        bound.clone(),
                        format!(
                            "{}0x{:x}",
                            if address < 0 { "-" } else { "" },
                            address.unsigned_abs()
                        ),
                        source.to_owned(),
                    ));
                }
            } else {
                bindings.insert(symbol, format!("0x{}", signed_hex(address, 8)));
                added += 1;
            }
        }
    }
    bindings.sort_keys();
    compare::write_json(root, &path, &serde_json::to_value(bindings)?)?;
    let mut output = format!(
        "{added} symbols bound; {} at another address\n",
        conflicts.len()
    );
    for (symbol, bound, address, source) in conflicts.into_iter().take(40) {
        output.push_str(&format!("  conflict {symbol} {bound} {address} {source}\n"));
    }
    Ok(output)
}
pub fn masked(pe: &Pe<'_>, fixups: &[u32], address: u32, size: usize) -> Result<Vec<u8>> {
    let mut code = pe.read(address, size)?.to_vec();
    for at in fixups
        .iter()
        .copied()
        .filter(|a| *a >= address && u64::from(*a) < u64::from(address) + size as u64)
    {
        code[at as usize - address as usize..(at as usize - address as usize + 4).min(size)]
            .fill(0);
    }
    if let Ok(branches) = x86::branches(&code, address) {
        for (field, _) in branches {
            code[field as usize..field as usize + 4].fill(0);
        }
    }
    Ok(code)
}
#[derive(Debug, Serialize)]
pub struct FunctionMatches {
    pub result: IndexMap<u32, u32>,
    pub ambiguous: IndexMap<u32, Vec<u32>>,
    pub total: usize,
}
pub fn funcmatch(root: &Path, from: &str, to: &str) -> Result<FunctionMatches> {
    let versions = [from, to].map(|v| folder(v).strip_prefix("GC_").unwrap().replace('_', "."));
    let data_a = fs::read(root.join(format!("build/compilers/GC/{}/mwcceppc.exe", versions[0])))?;
    let data_b = fs::read(root.join(format!("build/compilers/GC/{}/mwcceppc.exe", versions[1])))?;
    let a = Pe::parse(&data_a)?;
    let b = Pe::parse(&data_b)?;
    let fix_a = executable::base_relocations(&a)?;
    let fix_b = executable::base_relocations(&b)?;
    let functions = |version: &str| -> Result<Vec<Value>> {
        Ok(serde_json::from_slice(&fs::read(root.join(format!(
            "config/{}/functions.json",
            folder(version)
        )))?)?)
    };
    let fa = functions(from)?;
    let fb = functions(to)?;
    let mut by_bytes: BTreeMap<Vec<u8>, Vec<u32>> = BTreeMap::new();
    for f in fb {
        let address = compare::integer(&f["address"])?;
        let size = f["size"]
            .as_u64()
            .context("function lacks size")?
            .try_into()?;
        by_bytes
            .entry(masked(&b, &fix_b, address, size)?)
            .or_default()
            .push(address);
    }
    let mut result = IndexMap::new();
    let mut ambiguous = IndexMap::new();
    for f in &fa {
        let address = compare::integer(&f["address"])?;
        let size = f["size"]
            .as_u64()
            .context("function lacks size")?
            .try_into()?;
        let bytes = masked(&a, &fix_a, address, size)?;
        if let Some(candidates) = by_bytes.get(&bytes) {
            ensure!(!candidates.is_empty(), "empty function candidate group");
            if candidates.len() == 1 {
                result.insert(address, candidates[0]);
            } else {
                ambiguous.insert(address, candidates.clone());
            }
        }
    }
    Ok(FunctionMatches {
        result,
        ambiguous,
        total: fa.len(),
    })
}
