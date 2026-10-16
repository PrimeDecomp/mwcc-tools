use super::Object;
use crate::pe::u32le;
use anyhow::{Context, Result, ensure};
use indexmap::IndexMap;
use std::collections::{BTreeMap, BTreeSet};
pub fn section_sizes(data: &[u8]) -> Result<IndexMap<String, u64>> {
    let object = Object::parse(data)?;
    let mut out = IndexMap::new();
    for section in object.sections {
        let name = section.name.split('$').next().unwrap();
        if name != ".debug" {
            *out.entry(name.into()).or_default() += u64::from(section.size);
        }
    }
    out.retain(|_, size| *size != 0);
    Ok(out)
}
pub fn defined_symbols(data: &[u8]) -> Result<BTreeSet<String>> {
    Ok(Object::parse(data)?
        .symbols
        .into_values()
        .filter(|s| s.section > 0 && s.storage == 2)
        .map(|s| s.name)
        .collect())
}
pub fn undefine_symbols(data: &[u8], names: &BTreeSet<String>) -> Result<Vec<u8>> {
    let object = Object::parse(data)?;
    let mut out = data.to_vec();
    for symbol in object.symbols.values() {
        if names.contains(&symbol.name) && symbol.storage == 2 {
            out[symbol.offset + 8..symbol.offset + 14].fill(0);
        }
    }
    Ok(out)
}
pub fn hook_symbols(data: &[u8], names: &BTreeSet<String>) -> Result<Vec<u8>> {
    let object = Object::parse(data)?;
    let mut out = data.to_vec();
    let mut table =
        data[object.string_offset..object.string_offset + object.string_size as usize].to_vec();
    ensure!(table.len() >= 4, "hook object lacks string table");
    let mut added = vec![];
    let mut moved = BTreeMap::new();
    for (&index, symbol) in &object.symbols {
        if !names.contains(&symbol.name) || symbol.section <= 0 {
            continue;
        }
        let retail = format!("{}__retail", symbol.name);
        let encoded: Vec<_> = retail.chars().map(|c| c as u8).collect();
        out[symbol.offset..symbol.offset + 4].fill(0);
        out[symbol.offset + 4..symbol.offset + 8]
            .copy_from_slice(&u32::try_from(table.len())?.to_le_bytes());
        table.extend(encoded);
        table.push(0);
        moved.insert(
            index,
            object.symbol_count + u32::try_from(added.len() / 18)?,
        );
        added.extend([0; 4]);
        added.extend(u32::try_from(table.len())?.to_le_bytes());
        added.extend([0; 6]);
        added.extend(0x20u16.to_le_bytes());
        added.extend([2, 0]);
        table.extend(symbol.name.chars().map(|c| c as u8));
        table.push(0);
    }
    ensure!(
        !moved.is_empty(),
        "object defines none of the requested hook symbols"
    );
    for section in &object.sections {
        for (index, relocation) in section.relocations.iter().enumerate() {
            let Some(target) = moved.get(&relocation.symbol) else {
                continue;
            };
            let field = if section.raw_offset != 0
                && relocation
                    .offset
                    .checked_add(4)
                    .is_some_and(|end| end <= section.size)
            {
                Some(u32le(
                    data,
                    section.raw_offset as usize + relocation.offset as usize,
                )?)
            } else {
                None
            };
            if matches!(relocation.kind, 6 | 20) && field == Some(0) {
                ensure!(
                    section.flags & 0x01000000 == 0,
                    "hook mutation of extended relocations is not supported"
                );
                let at = section.relocation_offset as usize + 10 * index + 4;
                out.get_mut(at..at + 4)
                    .context("relocation outside object")?
                    .copy_from_slice(&target.to_le_bytes());
            }
        }
    }
    let count = object
        .symbol_count
        .checked_add(u32::try_from(added.len() / 18)?)
        .context("hook symbol count overflow")?;
    out[12..16].copy_from_slice(&count.to_le_bytes());
    let length = u32::try_from(table.len())?.to_le_bytes();
    table[..4].copy_from_slice(&length);
    let mut result = out[..object.string_offset].to_vec();
    result.extend(added);
    result.extend(table);
    result.extend(&out[object.string_offset + object.string_size as usize..]);
    Ok(result)
}
