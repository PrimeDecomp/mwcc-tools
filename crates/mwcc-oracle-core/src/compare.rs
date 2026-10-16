//! Per-function retail comparison, target extraction and objdiff progress metadata.
use crate::{
    coff::Object,
    executable::{self, Original},
};
use anyhow::{Context, Result, ensure};
use indexmap::IndexMap;
use serde_json::{Value, json};
use std::{collections::BTreeSet, fs, path::Path};
mod resolve;
pub use resolve::{Resolution, c_symbol, function_section, resolve_function};

pub fn integer(value: &Value) -> Result<u32> {
    if let Some(v) = value.as_u64() {
        return Ok(v.try_into()?);
    }
    let text = value
        .as_str()
        .context("address is not a string or integer")?;
    Ok(
        if let Some(text) = text.strip_prefix("0x").or_else(|| text.strip_prefix("0X")) {
            u32::from_str_radix(text, 16)?
        } else {
            text.parse()?
        },
    )
}
fn text<'a>(row: &'a Value, key: &str) -> Result<&'a str> {
    row[key]
        .as_str()
        .with_context(|| format!("row lacks {key}"))
}
fn number(row: &Value, key: &str) -> Result<u32> {
    integer(&row[key])
}
pub struct Inventory {
    pub original: Original,
    pub rows: Vec<Value>,
}
pub fn inventory(root: &Path, version: &str) -> Result<Inventory> {
    let original = Original::read(root, version)?;
    let pe = original.pe()?;
    let functions: Vec<Value> = serde_json::from_slice(&fs::read(
        root.join("config").join(version).join("functions.json"),
    )?)?;
    let mut rows = vec![];
    for section in &pe.sections {
        let code = section.characteristics & 0x20000000 != 0;
        if !code && [".reloc", ".rsrc", ".idata", ".edata"].contains(&section.name.as_str()) {
            continue;
        }
        let start = section.virtual_address;
        let end = start
            .checked_add(section.virtual_size)
            .context("inventory section address overflow")?;
        let mut cursor = start;
        let mut selected = vec![];
        if code {
            for f in &functions {
                let a = number(f, "address")?;
                if (start..end).contains(&a) {
                    selected.push((a, f));
                }
            }
        }
        selected.sort_by_key(|(a, _)| *a);
        for (address, function) in selected {
            if address > cursor {
                rows.push(json!({"name":format!("unknown/{}/{cursor:08x}",section.name),"address":cursor,"size":address-cursor,"code":code}));
            }
            let unit = function.get("source").and_then(Value::as_str).map_or_else(
                || "unrecovered".into(),
                |s| {
                    Path::new(s)
                        .with_extension("")
                        .to_string_lossy()
                        .replace('\\', "/")
                },
            );
            let name = text(function, "name")?;
            let mut row = function.clone();
            row["address"] = json!(address);
            row["code"] = json!(true);
            row["name"] = json!(format!("{unit}/{name}"));
            row["symbol"] = json!(if name.starts_with('?') {
                name.to_owned()
            } else {
                format!("_{name}")
            });
            cursor = address
                .checked_add(number(function, "size")?)
                .context("inventory function address overflow")?;
            rows.push(row);
        }
        if cursor < end {
            rows.push(json!({"name":format!("unknown/{}/{cursor:08x}",section.name),"address":cursor,"size":end-cursor,"code":code}));
        }
    }
    for row in &mut rows {
        let section = pe.section_for_address(number(row, "address")?)?;
        row["bss"] = json!(section.characteristics & 0x80 != 0 && section.file_size == 0);
        let name = text(row, "name")?;
        let file = if cfg!(windows) {
            name.chars()
                .map(|c| {
                    if "<>:\"|?*".contains(c) {
                        format!("%{:02x}", c as u32)
                    } else {
                        c.to_string()
                    }
                })
                .collect::<String>()
        } else {
            name.to_owned()
        };
        row["target"] = json!(format!("build/{version}/target/{file}.obj"));
        if row.get("source").is_some() {
            row["base"] = json!(format!("build/{version}/base/{file}.obj"));
        }
    }
    Ok(Inventory { original, rows })
}
/// Legacy extraction objects intentionally carry a single symbol and no section auxiliary entries.
pub fn single_coff(data: &[u8], name: Option<&str>, code: bool, bss_size: u32) -> Result<Vec<u8>> {
    let name = name.filter(|n| !n.is_empty());
    let mut strings = vec![0u8; 4];
    let mut symbols = vec![];
    if let Some(name) = name {
        symbols.extend(0u32.to_le_bytes());
        symbols.extend(4u32.to_le_bytes());
        symbols.extend(0u32.to_le_bytes());
        symbols.extend(1i16.to_le_bytes());
        symbols.extend((if code { 0x20u16 } else { 0 }).to_le_bytes());
        symbols.extend([2, 0]);
        strings.extend(name.as_bytes());
        strings.push(0);
    }
    let length: u32 = strings.len().try_into()?;
    strings[..4].copy_from_slice(&length.to_le_bytes());
    let size = if bss_size != 0 {
        bss_size
    } else {
        data.len().try_into()?
    };
    let section = if code {
        ".text"
    } else if bss_size != 0 {
        ".bss"
    } else {
        ".data"
    };
    let flags: u32 = if code {
        0x60000020
    } else if bss_size != 0 {
        0xc0000080
    } else {
        0xc0000040
    };
    let offset: u32 = (60 + data.len()).try_into()?;
    let mut out = vec![];
    out.extend(0x14cu16.to_le_bytes());
    out.extend(1u16.to_le_bytes());
    out.extend(0u32.to_le_bytes());
    out.extend(offset.to_le_bytes());
    out.extend(u32::from(name.is_some()).to_le_bytes());
    out.extend([0; 4]);
    out.extend(section.as_bytes());
    out.resize(28, 0);
    out.extend([0; 8]);
    out.extend(size.to_le_bytes());
    out.extend((if bss_size != 0 { 0u32 } else { 60 }).to_le_bytes());
    out.extend([0; 12]);
    out.extend(flags.to_le_bytes());
    out.extend(data);
    out.extend(symbols);
    out.extend(strings);
    Ok(out)
}
fn write(root: &Path, path: &str, data: &[u8]) -> Result<()> {
    let path = root.join(path);
    fs::create_dir_all(path.parent().context("output lacks parent")?)?;
    fs::write(path, data)?;
    Ok(())
}
pub fn extract(root: &Path, version: &str) -> Result<()> {
    let inventory = inventory(root, version)?;
    let pe = inventory.original.pe()?;
    for row in inventory.rows {
        let bss = row["bss"] == true;
        let code = row["code"] == true;
        let address = number(&row, "address")?;
        let size = number(&row, "size")?;
        let data = if bss {
            &[][..]
        } else {
            pe.read(address, size as usize)?
        };
        let name = row["symbol"]
            .as_str()
            .map(str::to_owned)
            .or_else(|| code.then(|| format!("__unknown_{address:08x}")));
        write(
            root,
            text(&row, "target")?,
            &single_coff(data, name.as_deref(), code, if bss { size } else { 0 })?,
        )?;
    }
    let path = root.join(format!("build/{version}/target/ok"));
    fs::create_dir_all(path.parent().unwrap())?;
    fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(path)?;
    Ok(())
}
#[derive(Debug)]
pub struct Check {
    pub body: Option<Vec<u8>>,
    pub exact: bool,
}
pub fn check(root: &Path, version: &str, inventory: &Inventory) -> Result<IndexMap<String, Check>> {
    let pe = inventory.original.pe()?;
    let fixups = executable::base_relocations(&pe)?;
    let bindings: IndexMap<String, Value> = serde_json::from_slice(&fs::read(
        root.join(format!("config/{version}/bindings.json")),
    )?)?;
    let mut addresses: IndexMap<_, _> = bindings
        .into_iter()
        .map(|(n, v)| Ok((n, integer(&v)?)))
        .collect::<Result<_>>()?;
    let mut own: IndexMap<String, IndexMap<String, u32>> = IndexMap::new();
    let mut data: IndexMap<String, Vec<u8>> = IndexMap::new();
    for row in &inventory.rows {
        if let Some(symbol) = row["symbol"].as_str() {
            let address = number(row, "address")?;
            addresses.insert(symbol.into(), address);
            if let Some(source) = row["source"].as_str() {
                own.entry(source.into())
                    .or_default()
                    .insert(symbol.into(), address);
            }
        }
        if let Some(source) = row["source"].as_str()
            && !data.contains_key(source)
        {
            data.insert(
                source.into(),
                fs::read(root.join(format!("build/{version}/compiled/{source}.obj")))?,
            );
        }
    }
    let objects: IndexMap<_, _> = data
        .iter()
        .map(|(source, data)| Ok((source.as_str(), Object::parse(data)?)))
        .collect::<Result<_>>()?;
    let mut results = IndexMap::new();
    for row in &inventory.rows {
        let Some(source) = row["source"].as_str() else {
            continue;
        };
        let mut bindings = addresses.clone();
        bindings.extend(own[source].clone());
        let address = number(row, "address")?;
        let size = number(row, "size")?;
        let result = resolve_function(
            &objects[source],
            text(row, "symbol")?,
            address,
            &bindings,
            &pe,
            Some(size as usize),
        );
        let value = match result {
            Ok((body, resolutions)) => {
                let actual: BTreeSet<_> = resolutions
                    .iter()
                    .filter(|r| r.kind == 6)
                    .map(|r| u64::from(address) + r.offset as u64)
                    .collect();
                let expected: BTreeSet<_> = fixups
                    .iter()
                    .filter(|a| {
                        u64::from(**a) >= u64::from(address)
                            && u64::from(**a) < u64::from(address) + u64::from(size)
                    })
                    .map(|a| u64::from(*a))
                    .collect();
                let exact = body == pe.read(address, size as usize)? && actual == expected;
                Check {
                    body: Some(body),
                    exact,
                }
            }
            Err(_) => Check {
                body: None,
                exact: false,
            },
        };
        results.insert(text(row, "name")?.into(), value);
    }
    Ok(results)
}
pub fn matching(config: &Value, sources: &Value, source: &str) -> bool {
    if let Some(names) = config.get("matching") {
        names
            .as_array()
            .is_some_and(|names| names.iter().any(|n| n == source))
    } else {
        sources[source]["status"].as_str().unwrap_or("Matching") == "Matching"
    }
}
pub struct Comparison {
    pub summary: String,
    pub failed: bool,
}
pub fn compare(root: &Path, version: &str) -> Result<Comparison> {
    let inventory = inventory(root, version)?;
    let results = check(root, version, &inventory)?;
    let sources: Value = serde_json::from_slice(&fs::read(root.join("config/sources.json"))?)?;
    let mut units = vec![];
    let mut exact = 0;
    let mut linked = 0;
    let mut failed = vec![];
    let mut nonmatching = vec![];
    let mut patched = vec![];
    for row in &inventory.rows {
        let mut unit = json!({"name":row["name"],"target_path":row["target"],"metadata":{"complete":false,"progress_categories":[if row["code"] == true {"code"} else {"data"}]}});
        if let Some(source) = row["source"].as_str() {
            unit["metadata"]["source_path"] = json!(source);
            let name = text(row, "name")?;
            let result = &results[name];
            if let Some(body) = &result.body {
                write(
                    root,
                    text(row, "base")?,
                    &single_coff(body, Some(text(row, "symbol")?), true, 0)?,
                )?;
                unit["base_path"] = row["base"].clone();
            }
            exact += usize::from(result.exact);
            if row["binary_patch"].as_bool().unwrap_or(false) {
                patched.push(name);
            } else if result.exact && matching(&inventory.original.config, &sources, source) {
                unit["metadata"]["complete"] = json!(true);
                linked += 1;
            } else if !result.exact {
                if matching(&inventory.original.config, &sources, source) {
                    failed.push(name);
                } else {
                    nonmatching.push(name);
                }
            }
        }
        units.push(unit);
    }
    let config = json!({"$schema":"https://raw.githubusercontent.com/encounter/objdiff/main/config.schema.json","min_version":"3.8.0","custom_make":"ninja","build_target":false,"build_base":true,"watch_patterns":["src/**/*.c","src/**/*.cpp","include/**/*.h","config/**/*.json"],"units":units,"progress_categories":[{"id":"code","name":"Code"},{"id":"data","name":"Data"}]});
    write_json(root, "objdiff.json", &config)?;
    let total = inventory
        .rows
        .iter()
        .filter(|r| r.get("source").is_some())
        .count();
    let mut summary = vec![format!(
        "{version}: {exact}/{total} functions exact, {linked} linked"
    )];
    if !nonmatching.is_empty() {
        summary.push(format!(
            "  NonMatching: {}",
            nonmatching
                .iter()
                .map(|n| n.rsplit('/').next().unwrap())
                .collect::<Vec<_>>()
                .join(" ")
        ));
    }
    if !patched.is_empty() {
        summary.push(format!(
            "  binary patch: {}",
            patched
                .iter()
                .map(|n| n.rsplit('/').next().unwrap())
                .collect::<Vec<_>>()
                .join(" ")
        ));
    }
    summary.extend(failed.iter().map(|n| format!("  not exact: {n}")));
    let summary = summary.join("\n");
    if failed.is_empty() {
        write_text(
            root,
            &format!("build/{version}/ok"),
            &(summary.clone() + "\n"),
        )?;
    }
    Ok(Comparison {
        summary,
        failed: !failed.is_empty(),
    })
}
pub fn write_text(root: &Path, path: &str, text: &str) -> Result<()> {
    let text = if cfg!(windows) {
        text.replace('\n', "\r\n")
    } else {
        text.to_owned()
    };
    write(root, path, text.as_bytes())
}
pub fn write_json(root: &Path, path: &str, value: &Value) -> Result<()> {
    write_text(root, path, &(serde_json::to_string_pretty(value)? + "\n"))
}
/// Remove artificial unknown-range functions from an objdiff report's counts.
pub fn adjust_report(report: &mut Value) -> Result<()> {
    let mut removed = 0i64;
    for unit in report["units"]
        .as_array_mut()
        .context("report has no units")?
    {
        if unit["name"]
            .as_str()
            .is_some_and(|n| n.starts_with("unknown/"))
        {
            removed += unit["measures"]["total_functions"].as_i64().unwrap_or(0);
            unit.as_object_mut()
                .context("report unit is not an object")?
                .remove("functions");
            unit["measures"]["total_functions"] = json!(0);
            unit["measures"]["matched_functions_percent"] = json!(0.0);
        }
    }
    fn adjust(measures: &mut Value, removed: i64) {
        let total = measures["total_functions"].as_i64().unwrap_or(0) - removed;
        let matched = measures["matched_functions"].as_f64().unwrap_or(0.0);
        measures["total_functions"] = json!(total);
        measures["matched_functions_percent"] = json!(if total != 0 {
            100.0 * matched / total as f64
        } else {
            0.0
        });
    }
    ensure!(report["measures"].is_object(), "report has no measures");
    adjust(&mut report["measures"], removed);
    if let Some(categories) = report["categories"].as_array_mut() {
        for category in categories {
            if category["id"] == "code" {
                adjust(&mut category["measures"], removed);
            }
        }
    }
    Ok(())
}
