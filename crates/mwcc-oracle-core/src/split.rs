//! Reconstruct the retail image as relocatable unit objects for exact relinking.
use crate::{
    coff::{Builder, Relocation},
    executable::{self, Splits, Symbol, option, size},
    pe::{self, Pe},
    x86,
};
use anyhow::{Context, Result, ensure};
use indexmap::IndexMap;
use serde_json::{Value, json};
use std::{
    collections::BTreeMap,
    fs,
    path::{Path, PathBuf},
};
#[derive(Clone)]
struct Target {
    name: String,
    section: String,
    address: u32,
    options: IndexMap<String, Value>,
    unit: Option<String>,
    size: u32,
}
type Ranges = BTreeMap<String, Vec<(u32, u32, String)>>;
fn unit_at<'a>(ranges: &'a Ranges, section: &str, address: u32) -> Option<&'a str> {
    ranges
        .get(section)?
        .iter()
        .find(|(start, end, _)| {
            *start <= address && address < *end || *start == address && address == *end
        })
        .map(|(_, _, u)| u.as_str())
}
fn resolve<'a>(value: u32, unit: &str, targets: &'a [Target], pe: &Pe<'_>) -> Option<&'a Target> {
    let hi = targets.partition_point(|t| t.address <= value);
    let usable = targets[hi.saturating_sub(64)..hi]
        .iter()
        .enumerate()
        .filter(|(_, t)| {
            (t.address == value
                || u64::from(t.address) <= u64::from(value)
                    && u64::from(value) < u64::from(t.address) + u64::from(t.size))
                && (t.unit.as_deref() == Some(unit) || option(&t.options, "scope") != Some("local"))
        })
        .max_by_key(|(i, t)| {
            (
                t.address,
                t.unit.as_deref() == Some(unit),
                std::cmp::Reverse(*i),
            )
        });
    if let Some((_, t)) = usable {
        return Some(t);
    }
    let section = &pe.section_for_address(value).ok()?.name;
    targets[hi.saturating_sub(512)..(hi + 512).min(targets.len())]
        .iter()
        .filter(|t| t.section == *section && t.unit.as_deref() == Some(unit))
        .min_by_key(|t| t.address.abs_diff(value))
}
fn store(root: &Path, name: &str, data: &[u8]) -> Result<PathBuf> {
    let path = root.join(name);
    fs::create_dir_all(path.parent().context("output has no directory")?)?;
    fs::write(&path, data)?;
    Ok(path)
}
fn target(symbol: &Symbol, ranges: &Ranges) -> Result<Target> {
    Ok(Target {
        name: symbol.name.clone(),
        section: symbol.section.clone(),
        address: symbol.address,
        options: symbol.options.clone(),
        unit: unit_at(ranges, &symbol.section, symbol.address).map(str::to_owned),
        size: size(&symbol.options)?,
    })
}
pub fn generate(
    pe: &Pe<'_>,
    symbols: &[Symbol],
    splits: &Splits,
    destination: &Path,
) -> Result<Vec<PathBuf>> {
    let fixups = executable::base_relocations(pe)?;
    let mut ranges = Ranges::new();
    for unit in &splits.units {
        for range in &unit.ranges {
            ranges.entry(range.section.clone()).or_default().push((
                range.start,
                range.end,
                unit.name.clone(),
            ));
        }
    }
    for range in ranges.values_mut() {
        range.sort();
    }
    let mut targets: Vec<_> = symbols
        .iter()
        .filter(|s| option(&s.options, "type") != Some("label"))
        .map(|s| target(s, &ranges))
        .collect::<Result<_>>()?;
    targets.sort_by_key(|t| t.address);
    let mut written = vec![];
    for unit in &splits.units {
        let mut coff = Builder::default();
        let mut local: BTreeMap<(String, u32), u32> = BTreeMap::new();
        let mut pending: Vec<Vec<(u32, Target, u16)>> = vec![];
        let bss_start = unit
            .ranges
            .iter()
            .find(|r| r.section == ".bss")
            .map(|r| r.start);
        for range in &unit.ranges {
            let section = &range.section;
            if range.options.get("common").is_some_and(|s| !s.is_empty()) {
                let tail: Vec<_> = symbols
                    .iter()
                    .filter(|s| {
                        s.section == *section && range.start <= s.address && s.address < range.end
                    })
                    .collect();
                let entries: Vec<_> = tail
                    .iter()
                    .map(|s| Ok((s.name.clone(), size(&s.options)?)))
                    .collect::<Result<_>>()?;
                let layout = executable::common_layout(&entries, range.start)?;
                if tail.iter().all(|s| layout.get(&s.name) == Some(&s.address)) {
                    for symbol in tail {
                        let index =
                            coff.add_symbol(&symbol.name, size(&symbol.options)?, 0, 0, 2, &[])?;
                        local.insert((symbol.name.clone(), symbol.address), index);
                    }
                } else {
                    let slots = executable::common_names(range.end - range.start)?
                        .iter()
                        .map(|name| coff.add_symbol(name, 1, 0, 0, 2, &[]))
                        .collect::<Result<Vec<_>>>()?;
                    for symbol in tail {
                        let mut aux = vec![];
                        aux.extend(slots[(symbol.address - range.start) as usize].to_le_bytes());
                        aux.extend(3u32.to_le_bytes());
                        aux.extend([0; 10]);
                        let index = coff.add_symbol(&symbol.name, 0, 0, 0, 105, &aux)?;
                        local.insert((symbol.name.clone(), symbol.address), index);
                        coff.index.insert(symbol.name.clone(), index);
                    }
                }
                continue;
            }
            let defaults = splits
                .sections
                .get(section)
                .with_context(|| format!("split section {section} lacks defaults"))?;
            let kind = range
                .options
                .get("type")
                .or_else(|| defaults.get("type"))
                .context("split section lacks type")?;
            let align = range
                .options
                .get("align")
                .or_else(|| defaults.get("align"))
                .map_or(Ok(4), |s| s.parse::<u32>())?;
            let flags = executable::section_flags(kind)? | ((32 - align.leading_zeros()) << 20);
            let members: Vec<_> = symbols
                .iter()
                .filter(|s| {
                    s.section == *section
                        && (range.start <= s.address && s.address < range.end
                            || range.start == s.address && s.address == range.end)
                        && unit_at(&ranges, section, s.address) == Some(&unit.name)
                })
                .collect();
            let mut functions: Vec<_> = members
                .iter()
                .filter(|s| option(&s.options, "type") == Some("function"))
                .map(|s| Ok((s.address, size(&s.options)?)))
                .collect::<Result<_>>()?;
            functions.sort();
            let mut pieces = vec![(range.start, range.end)];
            if kind == "code" && !functions.is_empty() && functions[0].0 == range.start {
                pieces.clear();
                for (i, (address, size)) in functions.iter().enumerate() {
                    let following = functions
                        .get(i + 1)
                        .map_or(range.end, |(address, _)| *address);
                    let mut stop = address
                        .checked_add(*size)
                        .context("function range overflow")?;
                    let gap = following.saturating_sub(stop);
                    if gap >= 16 || gap > 0 && pe.read(stop, gap as usize)?.iter().any(|b| *b != 0)
                    {
                        stop = following;
                    }
                    pieces.push((*address, stop));
                }
            }
            for (piece_start, piece_end) in pieces {
                ensure!(piece_end >= piece_start, "reversed function piece");
                let mut content = if kind == "bss" {
                    vec![]
                } else {
                    pe.read(piece_start, (piece_end - piece_start) as usize)?
                        .to_vec()
                };
                let mut relocations = vec![];
                if kind != "bss" {
                    let lo = fixups.partition_point(|v| *v < piece_start);
                    let hi = fixups.partition_point(|v| *v < piece_end);
                    for &at in &fixups[lo..hi] {
                        let value = pe::u32le(pe.data, pe.address_to_offset(at)?)?;
                        let mut resolved = resolve(value, &unit.name, &targets, pe).cloned();
                        if resolved.is_none()
                            && pe.section_for_address(value).is_err()
                            && let Some((_, holder)) = members
                                .iter()
                                .enumerate()
                                .filter(|(_, s)| {
                                    s.address <= at
                                        && piece_start <= s.address
                                        && s.address < piece_end
                                })
                                .max_by_key(|(i, s)| (s.address, std::cmp::Reverse(*i)))
                        {
                            resolved = Some(target(holder, &ranges)?);
                        }
                        let mut resolved = resolved.with_context(|| {
                            format!(
                                "{}: no symbol for 0x{value:08x} referenced at 0x{at:08x}",
                                unit.name
                            )
                        })?;
                        if resolved.section == ".bss"
                            && option(&resolved.options, "scope") == Some("local")
                            && unit_at(&ranges, ".bss", resolved.address) == Some(&unit.name)
                            && let Some(start) = bss_start
                        {
                            resolved = Target {
                                name: ".bss".into(),
                                section: ".bss".into(),
                                address: start,
                                options: IndexMap::from([("scope".into(), json!("local"))]),
                                unit: Some(unit.name.clone()),
                                size: 0,
                            };
                        }
                        let offset = (at - piece_start) as usize;
                        let field = content
                            .get_mut(offset..offset + 4)
                            .context("absolute fixup crosses split piece")?;
                        field.copy_from_slice(&value.wrapping_sub(resolved.address).to_le_bytes());
                        relocations.push((at - piece_start, resolved, 6));
                    }
                }
                if kind == "code" {
                    for symbol in &members {
                        if option(&symbol.options, "type") != Some("function")
                            || !(piece_start..piece_end).contains(&symbol.address)
                        {
                            continue;
                        }
                        let length = size(&symbol.options)?;
                        for (field, value) in x86::branches(
                            pe.read(symbol.address, length as usize)?,
                            symbol.address,
                        )? {
                            if (symbol.address..symbol.address + length).contains(&value) {
                                continue;
                            }
                            let resolved=resolve(value,&unit.name,&targets,pe).with_context(||format!("{}: branch at 0x{:08x} to 0x{value:08x} is not to a symbol",unit.name,symbol.address+field))?;
                            let offset = symbol.address + field - piece_start;
                            let field = content
                                .get_mut(offset as usize..offset as usize + 4)
                                .context("relative fixup crosses split piece")?;
                            field.copy_from_slice(
                                &value.wrapping_sub(resolved.address).to_le_bytes(),
                            );
                            relocations.push((offset, resolved.clone(), 20));
                        }
                    }
                }
                let rename = if kind == "code" {
                    members
                        .last()
                        .and_then(|s| option(&s.options, "rename"))
                        .unwrap_or(section)
                } else {
                    range
                        .options
                        .get("rename")
                        .map_or(section.as_str(), String::as_str)
                };
                let placeholders = relocations
                    .iter()
                    .map(|(offset, _, kind)| Relocation {
                        offset: *offset,
                        symbol: 0,
                        kind: *kind,
                    })
                    .collect();
                let number = coff.section(
                    rename,
                    flags,
                    content,
                    piece_end - piece_start,
                    placeholders,
                )?;
                pending.push(relocations);
                if kind == "bss" && section == ".bss" {
                    local.insert((".bss".into(), piece_start), coff.section_symbols[&number]);
                }
                for symbol in &members {
                    if !(piece_start <= symbol.address && symbol.address < piece_end
                        || piece_start == symbol.address && symbol.address == piece_end)
                    {
                        continue;
                    }
                    let scope = option(&symbol.options, "scope").unwrap_or("global");
                    if kind == "bss" && scope == "local" {
                        continue;
                    }
                    let bits = if option(&symbol.options, "type") == Some("function") {
                        0x20
                    } else {
                        0
                    };
                    let index = coff.add_symbol(
                        &symbol.name,
                        symbol.address - piece_start,
                        number,
                        bits,
                        if scope == "local" { 3 } else { 2 },
                        &[],
                    )?;
                    local.insert((symbol.name.clone(), symbol.address), index);
                }
            }
        }
        for (section, relocations) in pending.into_iter().enumerate() {
            let mut resolved = vec![];
            for (offset, target, kind) in relocations {
                let index = if let Some(index) = local.get(&(target.name.clone(), target.address)) {
                    *index
                } else {
                    ensure!(
                        option(&target.options, "scope") != Some("local"),
                        "{}: reference to {}, local to another unit",
                        unit.name,
                        target.name
                    );
                    coff.external(&target.name)?
                };
                resolved.push(Relocation {
                    offset,
                    symbol: index,
                    kind,
                });
            }
            coff.sections[section].relocations = resolved;
        }
        written.push(store(
            destination,
            &format!("{}.obj", unit.name),
            &coff.finish()?,
        )?);
    }
    let mut order = Builder::default();
    for (section, options) in &splits.sections {
        let kind = options.get("type").context("split section lacks type")?;
        let align = options.get("align").map_or(Ok(4), |s| s.parse::<u32>())?;
        order.section(
            section,
            executable::section_flags(kind)? | ((32 - align.leading_zeros()) << 20),
            vec![],
            0,
            vec![],
        )?;
    }
    let mut imports: Vec<_> = symbols.iter().filter(|s| s.section == ".idata").collect();
    imports.sort_by_key(|s| (s.address, &s.name));
    for symbol in imports {
        order.external(&symbol.name)?;
    }
    written.push(store(destination, "sections.obj", &order.finish()?)?);
    if splits.sections.contains_key(".rsrc") {
        let section = pe
            .sections
            .iter()
            .find(|s| s.name == ".rsrc")
            .context("split lists absent resource section")?;
        written.push(store(
            destination,
            "resources.res",
            &pe::res_file(&pe::resources(pe, section)?)?,
        )?);
    }
    written.push(store(destination, "ok", &[])?);
    Ok(written)
}
pub fn run(root: &Path, version: &str, destination: Option<&Path>) -> Result<Vec<PathBuf>> {
    run_from(
        root,
        version,
        destination,
        &format!("config/{version}"),
        &format!("build/{version}"),
    )
}
/// Split with the symbols and splits of VERSION's source tree (crate::source_tree), into its build directory.
pub fn run_source(root: &Path, version: &str, destination: Option<&Path>) -> Result<Vec<PathBuf>> {
    let tree = crate::source_tree::SourceTree::of(root, version, false)?;
    run_from(
        root,
        version,
        destination,
        &tree.config_dir,
        &tree.build_dir,
    )
}
fn run_from(
    root: &Path,
    version: &str,
    destination: Option<&Path>,
    config: &str,
    build: &str,
) -> Result<Vec<PathBuf>> {
    let original = executable::Original::read(root, version)?;
    let symbols =
        executable::read_symbols(&fs::read_to_string(root.join(config).join("symbols.txt"))?)?;
    let splits =
        executable::read_splits(&fs::read_to_string(root.join(config).join("splits.txt"))?)?;
    let default = root.join(build).join("obj");
    generate(
        &original.pe()?,
        &symbols,
        &splits,
        destination.unwrap_or(&default),
    )
}
