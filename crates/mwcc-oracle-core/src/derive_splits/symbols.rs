use super::*;
use crate::pe::u32le;
use regex::Regex;
pub fn make_symbols(
    image: &Image<'_>,
    order: &[String],
    ranges: &Ranges,
    owners: &IndexMap<u32, usize>,
    refs: &References,
    reference: &[Symbol],
) -> Result<Vec<String>> {
    let mut lookup = vec![];
    for (unit, entries) in ranges {
        for r in entries {
            lookup.push((r.start, r.end, r.section.as_str(), unit.as_str()));
        }
    }
    lookup.sort();
    let unit_at = |address: u32| {
        let mut k = lookup.partition_point(|r| r.0 <= address);
        while let Some(previous) = k.checked_sub(1) {
            let range = &lookup[previous];
            if address < range.1 {
                return Some(range.3);
            }
            k = previous;
        }
        None
    };
    let mut sources: IndexMap<u32, BTreeSet<String>> = IndexMap::new();
    for at in &image.fixups {
        if let Ok(data) = image.pe.read(*at, 4)
            && let Some(unit) = unit_at(*at)
        {
            sources
                .entry(u32le(data, 0)?)
                .or_default()
                .insert(unit.into());
        }
    }
    for (address, units) in refs {
        sources
            .entry(*address)
            .or_default()
            .extend(units.iter().map(|u| order[*u].clone()));
    }
    let generated = Regex::new(r"^_?(data|DAT|fn|lbl|s|FUN)_[0-9A-Fa-f]{8}$|^_?@\d+$")?;
    let mut bindings: Vec<_> = image.bindings.iter().collect();
    bindings.sort_by_key(|(name, _)| generated.is_match(name));
    let mut bound = IndexMap::new();
    for (name, address) in bindings {
        bound.entry(*address).or_insert_with(|| name.clone());
    }
    let undecorate = Regex::new(r"@\d+$")?;
    let mut from_reference = IndexMap::new();
    for symbol in reference {
        if symbol.options.get("type").and_then(Value::as_str) == Some("function") {
            let plain = undecorate.replace(&symbol.name, "");
            let plain = plain.strip_prefix('_').unwrap_or(&plain).to_owned();
            from_reference
                .entry(plain)
                .or_insert_with(|| symbol.name.clone());
        }
    }
    let function_starts: BTreeSet<_> = image.functions.iter().map(start).collect();
    let mut functions = vec![];
    for f in &image.functions {
        let address = start(f);
        let raw = f["name"].as_str().context("function lacks name")?;
        let slot = if size(f) == 6 {
            image.thunk(address)?
        } else {
            None
        };
        let imported = slot.and_then(|slot| image.imports.iter().find(|i| i.slot == slot));
        let name = if let Some(imported) = imported {
            imported.symbol.replacen("__imp__", "_", 1)
        } else if let Some(name) = bound.get(&address) {
            name.clone()
        } else if raw.starts_with('?') {
            raw.into()
        } else {
            image
                .decorated
                .get(raw)
                .or_else(|| from_reference.get(raw))
                .cloned()
                .unwrap_or_else(|| format!("_{raw}"))
        };
        functions.push((address, size(f), name));
    }
    let data_names: BTreeSet<_> = bound
        .iter()
        .filter(|(a, _)| !function_starts.contains(a))
        .map(|(_, n)| n.as_str())
        .collect();
    for (address, _, name) in &mut functions {
        if data_names.contains(name.as_str()) && bound.get(address) != Some(name) {
            *name = format!("{name}__{address:08X}");
        }
    }
    let mut counts = IndexMap::new();
    for (_, _, name) in &functions {
        *counts.entry(name.clone()).or_insert(0) += 1;
    }
    let mut lines: Vec<(String, u32, String)> = vec![];
    for (address, size, mut name) in functions {
        let unit = &order[owners[&address]];
        let shared = sources
            .get(&address)
            .is_some_and(|s| s.iter().any(|u| u != unit));
        let mut scope = "global";
        if counts[&name] > 1 {
            if shared {
                name = format!("{name}__{address:08X}");
            } else {
                scope = "local";
            }
        }
        lines.push((
            ".text".into(),
            address,
            format!(
                "{name} = .text:0x{address:08X}; // type:function size:0x{size:X} scope:{scope}"
            ),
        ));
    }
    let targets: BTreeSet<_> = bound.keys().chain(sources.keys()).copied().collect();
    let mut taken = IndexMap::new();
    for (unit, entries) in ranges {
        for r in entries {
            if r.section == ".text" || RUNTIME.contains(&r.section.as_str()) {
                continue;
            }
            let here: Vec<_> = targets.range(r.start..r.end).copied().collect();
            for (k, address) in here.iter().copied().enumerate() {
                let stop = here.get(k + 1).copied().unwrap_or(r.end);
                let name = bound.get(&address);
                if name.is_some() && function_starts.contains(&address) {
                    continue;
                }
                let shared = sources
                    .get(&address)
                    .is_some_and(|s| s.iter().any(|u| u != unit));
                let scope = if name.is_some() || shared {
                    "global"
                } else {
                    "local"
                };
                let mut name = name
                    .cloned()
                    .unwrap_or_else(|| format!("lbl_{address:08X}"));
                if taken.get(&name).is_some_and(|a| *a != address) {
                    name = format!("{name}__{address:08X}");
                }
                taken.insert(name.clone(), address);
                lines.push((
                    r.section.clone(),
                    address,
                    format!(
                        "{name} = {}:0x{address:08X}; // type:object size:0x{:X} scope:{scope}",
                        r.section,
                        stop - address
                    ),
                ));
            }
        }
    }
    for symbol in reference {
        if RUNTIME.contains(&symbol.section.as_str()) {
            let moved: u32 =
                (i64::from(symbol.address) + image.deltas[&symbol.section]).try_into()?;
            let options = symbol
                .options
                .iter()
                .map(|(k, v)| {
                    format!(
                        "{k}:{}",
                        v.as_str().map_or_else(
                            || if *v == true {
                                "True".into()
                            } else {
                                v.to_string()
                            },
                            str::to_owned
                        )
                    )
                })
                .collect::<Vec<_>>()
                .join(" ");
            let entry = format!(
                "{} = {}:0x{moved:08X}; // {options}",
                symbol.name, symbol.section
            );
            if !lines
                .iter()
                .any(|(_, a, text)| *a == moved && text.starts_with(&format!("{} = ", symbol.name)))
            {
                lines.push((symbol.section.clone(), moved, entry));
            }
        }
    }
    for import in &image.imports {
        lines.push((
            ".idata".into(),
            import.slot,
            format!(
                "{} = .idata:0x{:08X}; // type:object size:0x4 scope:global",
                import.symbol, import.slot
            ),
        ));
    }
    lines.sort_by_key(|(_, address, _)| *address);
    Ok(lines.into_iter().map(|(_, _, text)| text).collect())
}
