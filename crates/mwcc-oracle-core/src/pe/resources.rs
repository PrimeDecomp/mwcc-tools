use super::{Pe, Section, bytes, u16le, u32le};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use std::collections::BTreeSet;
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(untagged)]
pub enum ResourceName {
    Number(u32),
    Name(String),
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Resource {
    pub kind: ResourceName,
    pub name: ResourceName,
    pub language: u16,
    pub codepage: u32,
    pub data: Vec<u8>,
}
fn label(data: &[u8], value: u32) -> Result<ResourceName> {
    if value & 0x80000000 == 0 {
        return Ok(ResourceName::Number(value));
    }
    let at = (value & 0x7fffffff) as usize;
    let length = u16le(data, at)? as usize;
    let units: Vec<_> = bytes(data, at + 2, length * 2)?
        .chunks_exact(2)
        .map(|b| u16::from_le_bytes(b.try_into().unwrap()))
        .collect();
    Ok(ResourceName::Name(String::from_utf16(&units)?))
}
pub fn resources(pe: &Pe<'_>, section: &Section) -> Result<Vec<Resource>> {
    let data = bytes(
        pe.data,
        section.file_offset as usize,
        section.file_size as usize,
    )?;
    enum Work {
        Directory(usize, Vec<ResourceName>, BTreeSet<usize>),
        Leaf(usize, Resource),
    }
    let mut stack = vec![Work::Directory(0usize, vec![], BTreeSet::new())];
    let mut out = vec![];
    while let Some(work) = stack.pop() {
        let (offset, path, mut visited) = match work {
            Work::Directory(offset, path, visited) => (offset, path, visited),
            Work::Leaf(start, entry) => {
                out.push((start, entry));
                continue;
            }
        };
        ensure!(visited.insert(offset), "cyclic PE resource directory");
        ensure!(
            path.len() <= 2,
            "PE resource directory exceeds type/name/language depth"
        );
        let count = u16le(data, offset + 12)? as usize + u16le(data, offset + 14)? as usize;
        bytes(
            data,
            offset + 16,
            count.checked_mul(8).context("resource entry overflow")?,
        )?;
        for i in (0..count).rev() {
            let at = offset + 16 + i * 8;
            let name = u32le(data, at)?;
            let target = u32le(data, at + 4)?;
            if target & 0x80000000 != 0 {
                let mut path = path.clone();
                path.push(label(data, name)?);
                stack.push(Work::Directory(
                    (target & 0x7fffffff) as usize,
                    path,
                    visited.clone(),
                ));
            } else {
                ensure!(path.len() == 2, "PE resource leaf lacks type/name");
                let at = target as usize;
                let rva = u32le(data, at)?;
                let size = u32le(data, at + 4)? as usize;
                let page = u32le(data, at + 8)?;
                let start = rva
                    .checked_sub(section.virtual_address - pe.image_base)
                    .context("resource payload before its section")?
                    as usize;
                let payload = bytes(data, start, size)?.to_vec();
                let language = u16::try_from(name).context("resource language does not fit u16")?;
                stack.push(Work::Leaf(
                    start,
                    Resource {
                        kind: path[0].clone(),
                        name: path[1].clone(),
                        language,
                        codepage: page,
                        data: payload,
                    },
                ));
            }
        }
    }
    // Keep depth-first directory order for entries sharing a payload offset.
    out.sort_by_key(|(start, _)| *start);
    Ok(out.into_iter().map(|(_, r)| r).collect())
}
fn encode(out: &mut Vec<u8>, value: &ResourceName) -> Result<()> {
    match value {
        ResourceName::Number(value) => {
            out.extend(0xffffu16.to_le_bytes());
            out.extend(
                u16::try_from(*value)
                    .context("resource identifier exceeds u16")?
                    .to_le_bytes(),
            );
        }
        ResourceName::Name(value) => {
            for unit in value.encode_utf16() {
                out.extend(unit.to_le_bytes());
            }
            out.extend([0; 2]);
        }
    }
    Ok(())
}
pub fn res_file(entries: &[Resource]) -> Result<Vec<u8>> {
    let mut out = vec![];
    let null = Resource {
        kind: ResourceName::Number(0),
        name: ResourceName::Number(0),
        language: 0,
        codepage: 0,
        data: vec![],
    };
    for entry in std::iter::once(&null).chain(entries) {
        ensure!(
            entry.data.len() <= u32::MAX as usize,
            "resource payload too large"
        );
        let mut head = vec![];
        encode(&mut head, &entry.kind)?;
        encode(&mut head, &entry.name)?;
        head.resize((head.len() + 3) & !3, 0);
        out.extend((entry.data.len() as u32).to_le_bytes());
        out.extend((8 + head.len() as u32 + 16).to_le_bytes());
        out.extend(head);
        out.extend([0; 4]);
        out.extend(0x30u16.to_le_bytes());
        out.extend(entry.language.to_le_bytes());
        out.extend([0; 8]);
        out.extend(&entry.data);
        out.resize((out.len() + 3) & !3, 0);
    }
    Ok(out)
}
