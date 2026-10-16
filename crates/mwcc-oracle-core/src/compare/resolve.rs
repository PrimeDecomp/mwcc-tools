//! Resolve one i386 COFF function using retail bindings and verified literal contents.
use crate::{
    coff::{Object, ObjectSection},
    pe::{Pe, u32le},
};
use anyhow::{Context, Result, bail, ensure};
use indexmap::IndexMap;
use regex::Regex;
use serde::Serialize;
use std::{collections::BTreeSet, sync::OnceLock};

pub fn c_symbol(name: &str) -> String {
    static DECORATED: OnceLock<Regex> = OnceLock::new();
    DECORATED
        .get_or_init(|| Regex::new(r"^[_@]([A-Za-z_]\w*)@\d+$").unwrap())
        .captures(name)
        .map_or_else(|| name.to_owned(), |c| format!("_{}", &c[1]))
}
pub fn function_section<'a>(
    object: &'a Object<'_>,
    name: &str,
) -> Result<(&'a ObjectSection<'a>, usize, usize, i16)> {
    let mut found: Vec<_> = object
        .symbols
        .values()
        .filter(|s| s.name == name && s.section > 0)
        .collect();
    if found.is_empty() {
        found = object
            .symbols
            .values()
            .filter(|s| s.section > 0 && c_symbol(&s.name) == name)
            .collect();
    }
    ensure!(found.len() == 1, "candidate function not emitted uniquely");
    let symbol = found[0];
    let section = object
        .sections
        .get(symbol.section as usize - 1)
        .context("function section outside object")?;
    let begin = symbol.value as usize;
    let end = object
        .symbols
        .values()
        .filter(|s| {
            s.section == symbol.section
                && s.value > symbol.value
                && (s.kind & 0x20 != 0 || s.storage == 2)
        })
        .map(|s| s.value as usize)
        .min()
        .unwrap_or(section.data.len());
    Ok((section, begin, end, symbol.section))
}
#[derive(Debug, Serialize)]
pub struct Resolution {
    pub offset: usize,
    pub symbol: String,
    pub index: u32,
    pub address: i64,
    pub kind: u16,
    pub addend: u32,
}
fn slice(data: &[u8], start: usize, end: usize) -> &[u8] {
    data.get(start..end.min(data.len())).unwrap_or_default()
}
fn cstring(data: &[u8]) -> &[u8] {
    &data[..data.iter().position(|b| *b == 0).unwrap_or(data.len())]
}
fn printable(data: &[u8], controls: bool) -> bool {
    data.iter()
        .all(|b| (32..127).contains(b) || controls && [8, 9, 10, 13].contains(b))
}
fn contains(pe: &Pe<'_>, address: i64, bytes: &[u8]) -> bool {
    u32::try_from(address).is_ok_and(|a| pe.contains(a, bytes))
}
pub fn resolve_function(
    object: &Object<'_>,
    name: &str,
    target_address: u32,
    addresses: &IndexMap<String, u32>,
    pe: &Pe<'_>,
    target_size: Option<usize>,
) -> Result<(Vec<u8>, Vec<Resolution>)> {
    let (sec, begin, end, section_index) = function_section(object, name)?;
    let original = if let Some(size) = target_size.filter(|s| *s != 0) {
        pe.read(target_address, size)?
    } else {
        &[]
    };
    let referenced: BTreeSet<_> = original
        .windows(4)
        .map(|w| u32::from_le_bytes(w.try_into().unwrap()))
        .collect();
    let locate = |needle: &[u8], shift: u32| {
        let found: Vec<_> = pe
            .find(needle, Some(2))
            .into_iter()
            .map(i64::from)
            .collect();
        if found.len() > 1 && !referenced.is_empty() {
            let narrowed: Vec<_> = referenced
                .iter()
                .map(|v| i64::from(*v) - i64::from(shift))
                .filter(|a| contains(pe, *a, needle))
                .collect();
            if narrowed.len() == 1 {
                return narrowed;
            }
        }
        found
    };
    let mut body = slice(&sec.data, begin, end).to_vec();
    let mut resolutions = vec![];
    for relocation in &sec.relocations {
        let offset = relocation.offset as usize;
        if !(begin..end).contains(&offset) {
            continue;
        }
        let local = offset - begin;
        ensure!(
            local + 4 <= body.len(),
            "relocation crosses function boundary"
        );
        let dest = object
            .symbols
            .get(&relocation.symbol)
            .context("relocation names missing or auxiliary symbol")?;
        let kind = relocation.kind;
        let addend = u32le(&body, local)?;
        let literal = if dest.section > 0 {
            Some(
                object
                    .sections
                    .get(dest.section as usize - 1)
                    .context("relocation section outside object")?,
            )
        } else {
            None
        };
        let static_data = dest.storage == 3 && literal.is_some_and(|s| s.flags & 0x20 == 0);
        let mut own = static_data;
        if let Some(bound) = addresses.get(&dest.name).filter(|_| own) {
            let literal = literal.unwrap();
            let following = object
                .symbols
                .values()
                .filter(|s| s.section == dest.section && s.value > dest.value)
                .map(|s| s.value as usize)
                .min()
                .unwrap_or(literal.data.len());
            let text = cstring(slice(&literal.data, dest.value as usize, following));
            own = if !text.is_empty() {
                printable(text, false) && !pe.contains(*bound, text)
            } else {
                literal.flags & 0x80 == 0
                    && slice(&literal.data, dest.value as usize, dest.value as usize + 1) == [0]
                    && !pe.contains(*bound, &[0])
            };
        }
        let mut referred = None;
        if static_data && kind == 6 && local + 4 <= original.len() {
            let text = cstring(slice(
                &literal.unwrap().data,
                dest.value as usize,
                usize::MAX,
            ));
            let there = u32le(original, local)?.wrapping_sub(addend);
            let mut terminated = text.to_vec();
            terminated.push(0);
            if !text.is_empty() && printable(text, true) && pe.contains(there, &terminated) {
                referred = Some(i64::from(there));
            }
        }
        let address = if let Some(address) = referred {
            address
        } else if let Some(address) = addresses
            .get(&dest.name)
            .or_else(|| addresses.get(&c_symbol(&dest.name)))
            .filter(|_| !own)
        {
            i64::from(*address)
        } else if dest.section == section_index && (begin..end).contains(&(dest.value as usize)) {
            i64::from(target_address) + i64::from(dest.value) - begin as i64
        } else if let Some(literal) = literal.filter(|_| dest.section != section_index) {
            ensure!(
                literal.flags & 0x20 == 0,
                "unbound local function: {}",
                dest.name
            );
            let literal_end = object
                .symbols
                .values()
                .filter(|s| {
                    s.section == dest.section && s.value > dest.value && !s.name.starts_with('.')
                })
                .map(|s| s.value as usize)
                .min()
                .unwrap_or(literal.data.len());
            let mut payload = slice(&literal.data, dest.value as usize, literal_end).to_vec();
            for r in &literal.relocations {
                let at = r.offset as usize;
                if at + 4 <= dest.value as usize || at >= literal_end {
                    continue;
                }
                let position = at as i64 - i64::from(dest.value);
                if position < 0
                    || position as usize + 4 > payload.len()
                    || ![6, 7].contains(&r.kind)
                {
                    payload.clear();
                    break;
                }
                let position = position as usize;
                let reference = object
                    .symbols
                    .get(&r.symbol)
                    .context("literal relocation names missing symbol")?;
                let table_addend = u32le(&payload, position)?;
                let mut resolved = if let Some(a) = addresses
                    .get(&reference.name)
                    .or_else(|| addresses.get(&c_symbol(&reference.name)))
                {
                    i64::from(*a) + i64::from(table_addend)
                } else if reference.section == section_index
                    && (begin as u64..end as u64)
                        .contains(&(u64::from(reference.value) + u64::from(table_addend)))
                {
                    i64::from(target_address) + i64::from(reference.value) + i64::from(table_addend)
                        - begin as i64
                } else if reference.section == -1 {
                    i64::from(reference.value) + i64::from(table_addend)
                } else {
                    payload.clear();
                    break;
                };
                if r.kind == 7 {
                    resolved -= i64::from(pe.image_base);
                }
                payload[position..position + 4].copy_from_slice(&(resolved as u32).to_le_bytes());
            }
            let last_entry = literal
                .relocations
                .iter()
                .filter(|r| r.offset >= dest.value && (r.offset as usize) < literal_end)
                .map(|r| r.offset as usize + 4 - dest.value as usize)
                .max();
            if let Some(last) = last_entry
                .filter(|last| payload.len() > *last && payload[*last..].iter().all(|b| *b == 0))
            {
                payload.truncate(last);
            }
            let matches = if !payload.is_empty() && literal.flags & 0x80 == 0 {
                locate(&payload, 0)
            } else {
                vec![]
            };
            if matches.len() == 1 {
                matches[0]
            } else {
                let start = dest.value as usize + addend as usize;
                let tail = slice(&literal.data, start, usize::MAX);
                let string = tail
                    .iter()
                    .position(|b| *b == 0)
                    .filter(|n| *n > 0)
                    .map_or(&[][..], |n| &tail[..=n]);
                let mut hits = if kind == 6
                    && !string.is_empty()
                    && printable(&string[..string.len() - 1], true)
                    && !literal
                        .relocations
                        .iter()
                        .any(|r| (start..start + string.len()).contains(&(r.offset as usize)))
                {
                    locate(string, addend)
                } else {
                    vec![]
                };
                if hits.len() != 1 && local + 4 <= original.len() {
                    let raw = i64::from(u32le(original, local)?);
                    let derived = match kind {
                        6 => Some(raw - i64::from(addend)),
                        20 => Some(
                            raw + i64::from(target_address) + local as i64 + 4 - i64::from(addend),
                        ),
                        _ => None,
                    };
                    if let Some(derived) = derived {
                        let derived = derived as u32;
                        let same = if payload.is_empty() {
                            true
                        } else {
                            pe.read(derived, payload.len())
                                .map_or_else(|_| payload.iter().all(|b| *b == 0), |b| b == payload)
                        };
                        if same {
                            hits = vec![i64::from(derived) + i64::from(addend)];
                        }
                    }
                }
                if hits.len() != 1 && kind == 6 && local + 4 <= original.len() {
                    let raw = u32le(original, local)?;
                    let item_end = object
                        .symbols
                        .values()
                        .filter(|s| s.section == dest.section && s.value as usize > start)
                        .map(|s| s.value as usize)
                        .min()
                        .unwrap_or(literal.data.len());
                    let item = slice(&literal.data, start, item_end);
                    let relocated = literal.relocations.iter().any(|r| {
                        (0..4).any(|k| (start..item_end).contains(&(r.offset as usize + k)))
                    });
                    if !item.is_empty()
                        && !relocated
                        && pe.read(raw, item.len()).is_ok_and(|b| b == item)
                    {
                        hits = vec![i64::from(raw)];
                    }
                }
                ensure!(
                    hits.len() == 1,
                    "unbound literal: {} ({} retail blocks, {} strings)",
                    dest.name,
                    matches.len(),
                    hits.len()
                );
                hits[0] - i64::from(addend)
            }
        } else if dest.section == -1 {
            i64::from(dest.value)
        } else {
            bail!("unbound relocation: {}", dest.name)
        };
        let value = match kind {
            6 => address + i64::from(addend),
            7 => address - i64::from(pe.image_base) + i64::from(addend),
            20 => address + i64::from(addend) - (i64::from(target_address) + local as i64 + 4),
            _ => bail!("unsupported i386 relocation {kind}"),
        };
        body[local..local + 4].copy_from_slice(&(value as u32).to_le_bytes());
        resolutions.push(Resolution {
            offset: local,
            symbol: dest.name.clone(),
            index: relocation.symbol,
            address,
            kind,
            addend,
        });
    }
    Ok((body, resolutions))
}
