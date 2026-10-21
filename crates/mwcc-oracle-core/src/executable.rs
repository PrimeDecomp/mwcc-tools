//! Version configurations and decomp-toolkit symbol/split metadata for host PE tools.
use crate::pe::{Pe, bytes, u16le, u32le};
use anyhow::{Context, Result, bail, ensure};
use indexmap::IndexMap;
use regex::Regex;
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::{fs, path::Path};
pub fn hex(value: &str) -> Result<u32> {
    Ok(u32::from_str_radix(
        value
            .strip_prefix("0x")
            .or_else(|| value.strip_prefix("0X"))
            .unwrap_or(value),
        16,
    )?)
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Symbol {
    pub name: String,
    pub section: String,
    pub address: u32,
    pub options: IndexMap<String, Value>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Range {
    pub section: String,
    pub start: u32,
    pub end: u32,
    pub options: IndexMap<String, String>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Unit {
    pub name: String,
    pub ranges: Vec<Range>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Splits {
    pub sections: IndexMap<String, IndexMap<String, String>>,
    pub units: Vec<Unit>,
}
/// The symbols of `config/VERSION/symbols.txt` under ROOT.
pub fn retail_symbols(root: &Path, version: &impl std::fmt::Display) -> Result<Vec<Symbol>> {
    read_symbols(&fs::read_to_string(
        root.join(format!("config/{version}/symbols.txt")),
    )?)
}

pub fn read_symbols(text: &str) -> Result<Vec<Symbol>> {
    let regex = Regex::new(r"^(\S+) = ([^:\s]+):0x([0-9A-Fa-f]+);(?: // (.*))?$")?;
    let mut out = vec![];
    for (number, line) in text.lines().enumerate() {
        let line = line.trim();
        if line.is_empty() {
            continue;
        }
        let c = regex
            .captures(line)
            .with_context(|| format!("symbols line {}: cannot parse: {line}", number + 1))?;
        let options = c
            .get(4)
            .map_or("", |s| s.as_str())
            .split_whitespace()
            .map(|o| {
                o.split_once(':').map_or_else(
                    || (o.into(), Value::Bool(true)),
                    |(k, v)| (k.into(), Value::String(v.into())),
                )
            })
            .collect();
        out.push(Symbol {
            name: c[1].into(),
            section: c[2].into(),
            address: hex(&c[3])?,
            options,
        });
    }
    Ok(out)
}
pub fn read_splits(text: &str) -> Result<Splits> {
    let mut out = Splits {
        sections: IndexMap::new(),
        units: vec![],
    };
    let mut current = None;
    for (number, line) in text.lines().enumerate() {
        if line.trim().is_empty() {
            continue;
        }
        if !line.starts_with(char::is_whitespace) {
            let name = line.trim_end();
            ensure!(
                name.ends_with(':'),
                "splits line {}: expected a unit or Sections:",
                number + 1
            );
            if name == "Sections:" {
                current = None;
            } else {
                current = Some(out.units.len());
                out.units.push(Unit {
                    name: name[..name.len() - 1].into(),
                    ranges: vec![],
                });
            }
            continue;
        }
        let mut fields = line.split_whitespace();
        let section = fields.next().unwrap();
        let mut options: IndexMap<String, String> = IndexMap::new();
        for field in fields {
            let (k, v) = field.split_once(':').context("split option lacks ':'")?;
            options.insert(k.into(), v.into());
        }
        if let Some(index) = current {
            let start = hex(&options
                .shift_remove("start")
                .context("split range has no start")?)?;
            let end = hex(&options
                .shift_remove("end")
                .context("split range has no end")?)?;
            ensure!(end >= start, "split range ends before its start");
            out.units[index].ranges.push(Range {
                section: section.into(),
                start,
                end,
                options,
            });
        } else {
            out.sections.insert(section.into(), options);
        }
    }
    Ok(out)
}
pub struct Original {
    pub config: Value,
    pub data: Vec<u8>,
}
impl Original {
    pub fn read(root: &Path, version: &str) -> Result<Self> {
        let config: Value = serde_json::from_slice(&fs::read(
            root.join("config").join(version).join("config.json"),
        )?)?;
        let path = root.join(
            config["original"]
                .as_str()
                .context("config has no original executable")?,
        );
        let data = fs::read(&path).with_context(|| format!("read {}", path.display()))?;
        let actual = crate::hash::sha1_hex(&data);
        ensure!(
            config["sha1"] == actual,
            "{}: SHA-1 is not {}",
            path.display(),
            config["sha1"]
        );
        Pe::parse(&data)?;
        Ok(Self { config, data })
    }
    pub fn pe(&self) -> Result<Pe<'_>> {
        Pe::parse(&self.data)
    }
}
pub fn base_relocations(pe: &Pe<'_>) -> Result<Vec<u32>> {
    let coff = u32le(pe.data, 0x3c)? as usize + 4;
    let rva = u32le(pe.data, coff + 20 + 96 + 5 * 8)?;
    let size = u32le(pe.data, coff + 20 + 96 + 5 * 8 + 4)?;
    if size == 0 {
        return Ok(vec![]);
    }
    let address = pe
        .image_base
        .checked_add(rva)
        .context("relocation directory address overflow")?;
    let mut offset = pe.address_to_offset(address)?;
    let end = offset
        .checked_add(size as usize)
        .context("relocation directory size overflow")?;
    bytes(pe.data, offset, size as usize)?;
    let mut fixups = vec![];
    while offset < end {
        let page = u32le(pe.data, offset)?;
        let block = u32le(pe.data, offset + 4)? as usize;
        ensure!(
            block >= 8 && block.is_multiple_of(2) && offset + block <= end,
            "invalid PE base relocation block"
        );
        for i in 0..(block - 8) / 2 {
            let entry = u16le(pe.data, offset + 8 + 2 * i)?;
            if entry >> 12 == 3 {
                fixups.push(
                    pe.image_base
                        .checked_add(page)
                        .and_then(|a| a.checked_add(u32::from(entry & 0xfff)))
                        .context("base relocation address overflow")?,
                );
            }
        }
        offset += block;
    }
    fixups.sort();
    Ok(fixups)
}
/// The FIXUPS (sorted, as [`base_relocations`] returns them) at ADDRESS..ADDRESS + SIZE.
pub fn fixups_in(fixups: &[u32], address: u32, size: u64) -> &[u32] {
    let end = u64::from(address) + size;
    let low = fixups.partition_point(|a| *a < address);
    let high = fixups.partition_point(|a| u64::from(*a) < end);
    &fixups[low..high.max(low)]
}
pub fn section_flags(kind: &str) -> Result<u32> {
    Ok(match kind {
        "code" => 0x60000020,
        "data" => 0xc0000040,
        "rodata" => 0x40000040,
        "bss" => 0xc0000080,
        _ => bail!("unknown split section type {kind}"),
    })
}
pub fn size(options: &IndexMap<String, Value>) -> Result<u32> {
    options.get("size").map_or(Ok(0), |v| {
        hex(v.as_str().context("symbol size is not hexadecimal text")?)
    })
}
pub fn option<'a>(options: &'a IndexMap<String, Value>, key: &str) -> Option<&'a str> {
    options.get(key).and_then(Value::as_str)
}
pub fn mwld_hash(name: &str) -> Result<u32> {
    let mut hash = 0u32;
    for c in name.strip_prefix('_').unwrap_or(name).chars() {
        ensure!((c as u32) <= 255, "linker hash name is not Latin-1");
        hash = (hash << 1) ^ (c as u32);
        hash = (hash & 0xfff) ^ (hash >> 12);
    }
    Ok(hash)
}
pub fn common_layout(entries: &[(String, u32)], start: u32) -> Result<IndexMap<String, u32>> {
    let align = |size: u32| size.wrapping_neg() & size;
    let mut entries: Vec<_> = entries
        .iter()
        .map(|(name, size)| Ok((name, *size, align(*size).min(8), mwld_hash(name)?)))
        .collect::<Result<_>>()?;
    entries.sort_by_key(|(_, _, align, hash)| (std::cmp::Reverse(*align), *hash));
    let mut out = IndexMap::new();
    let mut cursor = start;
    for (name, size, align, _) in entries {
        ensure!(align != 0, "zero-sized COMMON symbol");
        cursor = cursor
            .checked_add(align - 1)
            .context("COMMON address overflow")?
            & !(align - 1);
        out.insert(name.clone(), cursor);
        cursor = cursor.checked_add(size).context("COMMON size overflow")?;
    }
    Ok(out)
}
pub fn common_names(count: u32) -> Result<Vec<String>> {
    let mut names = vec![];
    let mut last = None;
    ensure!(count <= 4096, "too many monotonically hashed COMMON names");
    for i in 0..count {
        let prefix = format!("_common_{i}_");
        let (suffix, hash) = common_suffix(mwld_hash(&prefix)? as u16, last)?;
        let name = format!("{prefix}{suffix}");
        names.push(name);
        last = Some(hash);
    }
    Ok(names)
}

/// The reference enumerates decimal suffixes. Breadth-first search over the
/// 4096 hash states gives the same smallest integer without an unbounded loop.
fn common_suffix(seed: u16, last: Option<u32>) -> Result<(String, u32)> {
    let step = |hash: u16, digit: u8| {
        let x = (u32::from(hash) << 1) ^ u32::from(b'0' + digit);
        ((x & 0xfff) ^ (x >> 12)) as u16
    };
    let zero = step(seed, 0);
    if last.is_none_or(|last| u32::from(zero) > last) {
        return Ok(("0".into(), u32::from(zero)));
    }
    #[derive(Clone, Copy)]
    enum Parent {
        Start(u8),
        After(u16, u8),
    }
    let mut parents = [None; 4096];
    let mut queue = std::collections::VecDeque::new();
    for digit in 1..10 {
        let hash = step(seed, digit);
        if parents[hash as usize].is_none() {
            parents[hash as usize] = Some(Parent::Start(digit));
            queue.push_back(hash);
        }
    }
    while let Some(hash) = queue.pop_front() {
        if last.is_none_or(|last| u32::from(hash) > last) {
            let mut digits = vec![];
            let mut cursor = hash;
            loop {
                match parents[cursor as usize].unwrap() {
                    Parent::Start(digit) => {
                        digits.push(b'0' + digit);
                        break;
                    }
                    Parent::After(previous, digit) => {
                        digits.push(b'0' + digit);
                        cursor = previous;
                    }
                }
            }
            digits.reverse();
            return Ok((String::from_utf8(digits)?, u32::from(hash)));
        }
        for digit in 0..10 {
            let next = step(hash, digit);
            if parents[next as usize].is_none() {
                parents[next as usize] = Some(Parent::After(hash, digit));
                queue.push_back(next);
            }
        }
    }
    bail!(
        "no increasing COMMON hash exists after {}",
        last.unwrap_or(0)
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn common_hash_search_matches_decimal_enumeration() {
        let root = Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../../tests/fixtures/binary/common-suffixes.json");
        let cases: Vec<Value> = serde_json::from_slice(&fs::read(root).unwrap()).unwrap();
        for case in cases {
            let seed = case["seed"].as_u64().unwrap() as u16;
            let threshold = case["threshold"].as_u64().map(|v| v as u32);
            let (suffix, hash) = common_suffix(seed, threshold).unwrap();
            assert_eq!(suffix, case["suffix"].as_str().unwrap());
            assert_eq!(serde_json::json!(hash), case["hash"]);
        }
        assert!(common_suffix(0, Some(4095)).is_err());
        let names = common_names(3).unwrap();
        let hashes = names
            .iter()
            .map(|n| mwld_hash(n).unwrap())
            .collect::<Vec<_>>();
        assert!(hashes.windows(2).all(|h| h[0] < h[1]));
    }
}
