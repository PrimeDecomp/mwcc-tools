//! Infer version split metadata with the legacy monotone ownership model.
use crate::{
    compare,
    executable::{self, Range, Symbol},
    investigate,
};
use anyhow::{Context, Result, ensure};
use indexmap::IndexMap;
use serde::Serialize;
use serde_json::{Value, json};
use std::{collections::BTreeSet, fs, path::Path};
mod image;
mod symbols;
pub use image::{Image, Import, compiled_symbols, import_table};
const RUNTIME: [&str; 4] = [".rdata", ".exc", ".CRT", ".tls"];
pub type Ranges = IndexMap<String, Vec<Range>>;
pub type References = IndexMap<u32, BTreeSet<usize>>;
pub type Sections = IndexMap<String, IndexMap<String, String>>;
fn start(f: &Value) -> u32 {
    f["start"].as_u64().unwrap() as u32
}
fn size(f: &Value) -> u32 {
    f["size"].as_u64().unwrap() as u32
}
pub fn monotone_assignment(costs: &[Vec<f64>], unit_count: usize) -> Result<Vec<usize>> {
    if costs.is_empty() {
        return Ok(vec![]);
    }
    ensure!(unit_count > 0, "cannot assign items without units");
    ensure!(
        costs
            .iter()
            .all(|row| row.len() == unit_count && row.iter().all(|c| c.is_finite())),
        "invalid ownership cost matrix"
    );
    let mut best = vec![0.0; unit_count];
    let mut back = vec![];
    for cost in costs {
        let mut prefix = vec![0.0; unit_count];
        let mut argmin = vec![0; unit_count];
        let mut low = f64::INFINITY;
        let mut low_at = 0;
        for u in 0..unit_count {
            if best[u] <= low {
                low = best[u];
                low_at = u;
            }
            prefix[u] = low;
            argmin[u] = low_at;
        }
        let mut new = vec![0.0; unit_count];
        let mut choice = vec![0; unit_count];
        for u in 0..unit_count {
            let stay = best[u];
            let moved = prefix[u] + 0.01;
            if stay <= moved {
                new[u] = stay + cost[u];
                choice[u] = u;
            } else {
                new[u] = moved + cost[u];
                choice[u] = argmin[u];
            }
        }
        best = new;
        back.push(choice);
    }
    let mut u = (0..unit_count)
        .min_by(|a, b| best[*a].total_cmp(&best[*b]).then(a.cmp(b)))
        .unwrap();
    let mut owners = vec![0; costs.len()];
    for i in (0..costs.len()).rev() {
        owners[i] = u;
        u = back[i][u];
    }
    Ok(owners)
}
fn text_units(image: &Image<'_>) -> Result<(Vec<String>, IndexMap<u32, usize>)> {
    let mut labels = vec![];
    let mut run: Option<(String, u32, u32)> = None;
    for f in &image.functions {
        let address = start(f);
        let dll = if size(f) == 6 {
            image
                .thunk(address)?
                .and_then(|slot| image.imports.iter().find(|i| i.slot == slot))
                .map(|i| i.dll.as_str())
        } else {
            None
        };
        let label = if let Some(unit) = image
            .definitions
            .get(f["name"].as_str().context("function lacks name")?)
        {
            Some(unit.clone())
        } else if let Some(source) = f["source"].as_str().filter(|s| !s.is_empty()) {
            Some(source.into())
        } else if let Some(dll) = dll {
            if dll.to_uppercase().starts_with("LMGR") {
                Some("lmgr326b".into())
            } else {
                if run
                    .as_ref()
                    .is_none_or(|r| r.0 != dll || i64::from(address) - i64::from(r.2) > 16)
                {
                    run = Some((dll.into(), address, address));
                }
                let r = run.as_mut().unwrap();
                r.2 = address + 6;
                Some(format!("{}_stubs_{:08X}", dll.to_lowercase(), r.1))
            }
        } else {
            None
        };
        if dll.is_none() {
            run = None;
        }
        labels.push(label);
    }
    let mut medians: IndexMap<String, Vec<u32>> = IndexMap::new();
    for (label, f) in labels.iter().zip(&image.functions) {
        if let Some(label) = label {
            medians.entry(label.clone()).or_default().push(start(f));
        }
    }
    let median = |values: &Vec<u32>| {
        let n = values.len();
        if n.is_multiple_of(2) {
            u64::from(values[n / 2 - 1]) + u64::from(values[n / 2])
        } else {
            2 * u64::from(values[n / 2])
        }
    };
    let mut names: Vec<_> = medians.keys().cloned().collect();
    names.sort_by_key(|u| (median(&medians[u]), medians[u][0]));
    let indices: IndexMap<_, _> = names
        .iter()
        .enumerate()
        .map(|(i, n)| (n.as_str(), i))
        .collect();
    let costs: Vec<Vec<_>> = labels
        .iter()
        .map(|label| {
            (0..names.len())
                .map(|u| {
                    if label.as_ref().is_none_or(|l| indices[l.as_str()] == u) {
                        0.0
                    } else {
                        1.0
                    }
                })
                .collect()
        })
        .collect();
    let owners = monotone_assignment(&costs, names.len())?;
    Ok((
        names,
        image
            .functions
            .iter()
            .zip(owners)
            .map(|(f, u)| (start(f), u))
            .collect(),
    ))
}
fn add_range(
    ranges: &mut Ranges,
    unit: &str,
    section: &str,
    start: u32,
    end: u32,
    options: IndexMap<String, String>,
) -> Result<()> {
    ensure!(start <= end, "derived {section} range is inverted");
    ranges
        .get_mut(unit)
        .context("derived unit is missing")?
        .push(Range {
            section: section.into(),
            start,
            end,
            options,
        });
    Ok(())
}
pub fn format_splits(sections: &Sections, order: &[String], ranges: &Ranges) -> Result<String> {
    let mut lines = vec!["Sections:".to_owned()];
    for (name, options) in sections {
        lines.push(format!(
            "\t{name:<11} {}",
            options
                .iter()
                .map(|(k, v)| format!("{k}:{v}"))
                .collect::<Vec<_>>()
                .join(" ")
        ));
    }
    lines.push(String::new());
    for unit in order {
        let mut entries: Vec<_> = ranges[unit].iter().collect();
        if entries.is_empty() {
            continue;
        }
        for r in &entries {
            ensure!(
                sections.contains_key(&r.section),
                "range names an absent section: {}",
                r.section
            );
        }
        entries.sort_by_key(|r| (sections.get_index_of(&r.section).unwrap(), r.start));
        lines.push(format!("{unit}:"));
        for r in entries {
            let extra = r
                .options
                .iter()
                .map(|(k, v)| format!(" {k}:{v}"))
                .collect::<String>();
            lines.push(format!(
                "\t{:<11} start:0x{:08X} end:0x{:08X}{extra}",
                r.section, r.start, r.end
            ));
        }
        lines.push(String::new());
    }
    Ok(lines.join("\n"))
}
#[derive(Debug, Serialize)]
pub struct Derivation {
    pub functions: Vec<Value>,
    pub order: Vec<String>,
    pub ranges: Ranges,
    pub owners: IndexMap<u32, usize>,
    pub refs: References,
    pub everywhere: BTreeSet<u32>,
    pub sections: Sections,
    pub imports: Vec<Import>,
    pub splits: String,
    pub symbols: Vec<String>,
}
fn read_image(root: &Path, version: &str, exe: Option<&Path>) -> Result<Vec<u8>> {
    let config: Value = serde_json::from_slice(&fs::read(root.join(format!(
        "config/{}/config.json",
        investigate::folder(version)
    )))?)?;
    Ok(fs::read(
        root.join(
            exe.unwrap_or(Path::new(
                config["original"]
                    .as_str()
                    .context("config lacks original")?,
            )),
        ),
    )?)
}
pub fn derive(
    root: &Path,
    version: &str,
    reference: &str,
    exe: Option<&Path>,
) -> Result<Derivation> {
    let data = read_image(root, version, exe)?;
    let mut image = Image::new(root, version, &data)?;
    let reference_directory = root.join("config").join(investigate::folder(reference));
    let reference_splits =
        executable::read_splits(&fs::read_to_string(reference_directory.join("splits.txt"))?)?;
    let reference_symbols = executable::read_symbols(&fs::read_to_string(
        reference_directory.join("symbols.txt"),
    )?)?;
    let (names, mut owners) = text_units(&image)?;
    let mut order = names.clone();
    let reference_names: Vec<_> = reference_splits
        .units
        .iter()
        .map(|u| u.name.as_str())
        .collect();
    for (i, unit) in reference_names.iter().enumerate() {
        if names.iter().any(|n| n == unit) {
            continue;
        }
        let position = reference_names[..i]
            .iter()
            .rev()
            .find_map(|p| order.iter().position(|n| n == p))
            .map_or(0, |p| p + 1);
        order.insert(position, unit.to_string());
    }
    let index: IndexMap<_, _> = order
        .iter()
        .enumerate()
        .map(|(i, n)| (n.clone(), i))
        .collect();
    for owner in owners.values_mut() {
        *owner = index[&names[*owner]];
    }
    let unit_count = order.len();
    ensure!(unit_count > 0, "derived image has no units");
    let common = index.get("common").copied().unwrap_or(unit_count - 1);
    let (refs, everywhere) = image.references(&owners)?;
    let mut ranges: Ranges = order.iter().map(|n| (n.clone(), vec![])).collect();
    let text = image.section(".text")?;
    let text_end = image::section_end(text)?;
    let mut firsts = std::collections::BTreeMap::new();
    for f in &image.functions {
        firsts.entry(owners[&start(f)]).or_insert(start(f));
    }
    let code_units: Vec<_> = firsts.keys().copied().collect();
    for (k, u) in code_units.iter().copied().enumerate() {
        let start_range = if k == 0 {
            text.virtual_address
        } else {
            firsts[&u]
        };
        let end = code_units.get(k + 1).map_or(text_end, |u| firsts[u]);
        let mut groups: Vec<(u32, u32)> = vec![];
        for f in image.functions.iter().filter(|f| owners[&start(f)] == u) {
            let address = start(f);
            let natural = (address & address.wrapping_neg()).min(16);
            if groups.last().is_none_or(|g| g.0 != natural) {
                groups.push((natural, address));
            }
        }
        for (g, (natural, first)) in groups.iter().copied().enumerate() {
            let lo = if g == 0 { start_range } else { first };
            let hi = groups.get(g + 1).map_or(end, |g| g.1);
            let options = if natural < 16 {
                IndexMap::from([("align".into(), natural.to_string())])
            } else {
                IndexMap::new()
            };
            add_range(&mut ranges, &order[u], ".text", lo, hi, options)?;
        }
    }
    let reference_data = read_image(root, reference, None)?;
    let reference_pe = crate::pe::Pe::parse(&reference_data)?;
    let reference_base: IndexMap<_, _> = reference_pe
        .sections
        .iter()
        .map(|s| (s.name.as_str(), s.virtual_address))
        .collect();
    for unit in &reference_splits.units {
        for range in &unit.ranges {
            if RUNTIME.contains(&range.section.as_str()) {
                let delta = i64::from(image.section(&range.section)?.virtual_address)
                    - i64::from(
                        *reference_base
                            .get(range.section.as_str())
                            .context("reference runtime section is missing")?,
                    );
                add_range(
                    &mut ranges,
                    &unit.name,
                    &range.section,
                    (i64::from(range.start) + delta).try_into()?,
                    (i64::from(range.end) + delta).try_into()?,
                    range.options.clone(),
                )?;
            }
        }
    }
    for section in [".data", ".bss"] {
        let s = image.section(section)?;
        let low = s.virtual_address;
        let high = image::section_end(s)?;
        let mut addresses: Vec<_> = refs
            .keys()
            .copied()
            .filter(|a| *a >= low && *a < high)
            .collect();
        addresses.sort();
        let costs: Vec<Vec<_>> = addresses
            .iter()
            .map(|a| {
                (0..unit_count)
                    .map(|u| {
                        if section == ".bss" && u == common {
                            0.9
                        } else if refs[a].contains(&u) {
                            0.0
                        } else {
                            1.0
                        }
                    })
                    .collect()
            })
            .collect();
        let owned = monotone_assignment(&costs, unit_count)?;
        let mut by_unit: std::collections::BTreeMap<usize, Vec<u32>> =
            std::collections::BTreeMap::new();
        for (a, u) in addresses.into_iter().zip(owned) {
            by_unit.entry(u).or_default().push(a);
        }
        let units: Vec<_> = by_unit.keys().copied().collect();
        let starts: Vec<_> = units
            .iter()
            .enumerate()
            .map(|(k, u)| if k == 0 { low } else { by_unit[u][0] & !7 })
            .collect();
        for (k, u) in units.iter().copied().enumerate() {
            let end = starts.get(k + 1).copied().unwrap_or(high);
            if k + 1 == units.len() && section == ".bss" && end & 7 != 0 {
                add_range(
                    &mut ranges,
                    &order[u],
                    section,
                    starts[k],
                    end & !7,
                    IndexMap::new(),
                )?;
                add_range(
                    &mut ranges,
                    &order[u],
                    section,
                    end & !7,
                    end,
                    IndexMap::from([("common".into(), "1".into())]),
                )?;
            } else {
                add_range(
                    &mut ranges,
                    &order[u],
                    section,
                    starts[k],
                    end,
                    IndexMap::new(),
                )?;
            }
        }
    }
    let mut sections = IndexMap::new();
    for section in &image.pe.sections {
        if let Some(options) = reference_splits.sections.get(&section.name) {
            sections.insert(section.name.clone(), options.clone());
        } else if section.name != ".reloc" {
            sections.insert(
                section.name.clone(),
                IndexMap::from([("type".into(), "data".into()), ("align".into(), "4".into())]),
            );
        }
    }
    for name in RUNTIME {
        if let Ok(s) = image.section(name) {
            image.deltas.insert(
                name.into(),
                i64::from(s.virtual_address)
                    - i64::from(
                        *reference_base
                            .get(name)
                            .context("reference runtime section is missing")?,
                    ),
            );
        }
    }
    let symbols =
        symbols::make_symbols(&image, &order, &ranges, &owners, &refs, &reference_symbols)?;
    let splits = format_splits(&sections, &order, &ranges)?;
    Ok(Derivation {
        functions: image.functions,
        order,
        ranges,
        owners,
        refs,
        everywhere,
        sections,
        imports: image.imports,
        splits,
        symbols,
    })
}
pub fn run(
    root: &Path,
    version: &str,
    reference: &str,
    exe: Option<&Path>,
    out: Option<&Path>,
) -> Result<()> {
    let derived = derive(root, version, reference, exe)?;
    let default = format!("config/{}", investigate::folder(version));
    let out = out.unwrap_or(Path::new(&default));
    let output = root.join(out);
    fs::create_dir_all(&output)?;
    compare::write_text(&output, "splits.txt", &derived.splits)?;
    compare::write_text(&output, "symbols.txt", &(derived.symbols.join("\n") + "\n"))?;
    Ok(())
}
