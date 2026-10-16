//! Bounds-checked ELF32 big-endian readers for PowerPC compiler outputs.
use anyhow::{Context, Result, ensure};
use serde::Serialize;
use std::collections::BTreeMap;

fn bytes(data: &[u8], offset: usize, length: usize) -> Result<&[u8]> {
    let end = offset.checked_add(length).context("ELF offset overflow")?;
    data.get(offset..end).context("ELF range outside file")
}
fn u16be(data: &[u8], offset: usize) -> Result<u16> {
    Ok(u16::from_be_bytes(bytes(data, offset, 2)?.try_into()?))
}
fn u32be(data: &[u8], offset: usize) -> Result<u32> {
    Ok(u32::from_be_bytes(bytes(data, offset, 4)?.try_into()?))
}
fn string(data: &[u8], offset: usize) -> Result<String> {
    let tail = data
        .get(offset..)
        .context("ELF string offset outside table")?;
    let end = tail
        .iter()
        .position(|b| *b == 0)
        .context("unterminated ELF string")?;
    Ok(tail[..end].iter().map(|b| char::from(*b)).collect())
}

#[derive(Clone, Debug)]
pub struct Section<'a> {
    pub name: String,
    pub kind: u32,
    pub data: &'a [u8],
    pub size: u32,
    pub link: usize,
    pub info: usize,
    pub alignment: u32,
    pub flags: u32,
    pub entry_size: u32,
}

#[derive(Debug)]
pub struct Elf<'a> {
    pub sections: Vec<Section<'a>>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct Symbol {
    pub name: String,
    pub section: usize,
    pub value: u32,
    pub size: u32,
    pub kind: u8,
    pub binding: u8,
}

#[derive(Clone, Debug, Serialize)]
pub struct Relocation {
    pub section: usize,
    pub offset: u32,
    pub kind: u8,
    pub symbol: Symbol,
    pub addend: i32,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[serde(untagged)]
pub enum SectionContents {
    Data(Vec<u8>),
    Uninitialized(u32),
}

impl<'a> Elf<'a> {
    pub fn parse(data: &'a [u8]) -> Result<Self> {
        ensure!(data.starts_with(b"\x7fELF"), "not an ELF file");
        ensure!(
            bytes(data, 4, 3)? == [1, 2, 1],
            "expected ELF32 big-endian version 1"
        );
        ensure!(u16be(data, 18)? == 20, "expected PowerPC ELF");
        let offset = u32be(data, 32)? as usize;
        let width = u16be(data, 46)? as usize;
        let count = u16be(data, 48)? as usize;
        let names_index = u16be(data, 50)? as usize;
        ensure!(
            width >= 40 && count > 0 && names_index < count,
            "invalid ELF section table"
        );
        bytes(
            data,
            offset,
            width
                .checked_mul(count)
                .context("ELF section count overflow")?,
        )?;
        let name_header = offset + width * names_index;
        let names = bytes(
            data,
            u32be(data, name_header + 16)? as usize,
            u32be(data, name_header + 20)? as usize,
        )?;
        let mut sections = Vec::with_capacity(count);
        for i in 0..count {
            let at = offset + i * width;
            let kind = u32be(data, at + 4)?;
            let size = u32be(data, at + 20)?;
            let contents = if kind == 8 {
                &[][..]
            } else {
                bytes(data, u32be(data, at + 16)? as usize, size as usize)?
            };
            sections.push(Section {
                name: string(names, u32be(data, at)? as usize)?,
                kind,
                data: contents,
                size,
                link: u32be(data, at + 24)? as usize,
                info: u32be(data, at + 28)? as usize,
                alignment: u32be(data, at + 32)?,
                flags: u32be(data, at + 8)?,
                entry_size: u32be(data, at + 36)?,
            });
        }
        Ok(Self { sections })
    }
    pub fn symbols(&self) -> Result<Vec<Symbol>> {
        let mut result = vec![];
        for section in &self.sections {
            if section.kind != 2 {
                continue;
            }
            let strings = self
                .sections
                .get(section.link)
                .context("invalid ELF symbol string table")?;
            let width = if section.entry_size == 0 {
                16
            } else {
                section.entry_size as usize
            };
            ensure!(
                width >= 16 && section.data.len() % width == 0,
                "invalid ELF symbol table size"
            );
            for at in (0..section.data.len()).step_by(width) {
                let info = section.data[at + 12];
                result.push(Symbol {
                    name: string(strings.data, u32be(section.data, at)? as usize)?,
                    value: u32be(section.data, at + 4)?,
                    size: u32be(section.data, at + 8)?,
                    kind: info & 15,
                    binding: info >> 4,
                    section: u16be(section.data, at + 14)? as usize,
                });
            }
        }
        Ok(result)
    }
    pub fn relocations(&self) -> Result<Vec<Relocation>> {
        let mut out = vec![];
        for section in &self.sections {
            if section.kind != 4 {
                continue;
            }
            ensure!(
                section.info < self.sections.len(),
                "invalid ELF relocation target section"
            );
            let symtab = self
                .sections
                .get(section.link)
                .context("invalid ELF relocation symbol table")?;
            ensure!(
                symtab.kind == 2,
                "ELF relocation does not link to a symbol table"
            );
            let strings = self
                .sections
                .get(symtab.link)
                .context("invalid ELF symbol string table")?;
            let symbol_width = if symtab.entry_size == 0 {
                16
            } else {
                symtab.entry_size as usize
            };
            ensure!(
                symbol_width >= 16 && symtab.data.len() % symbol_width == 0,
                "invalid ELF symbol table size"
            );
            let width = if section.entry_size == 0 {
                12
            } else {
                section.entry_size as usize
            };
            ensure!(
                width >= 12 && section.data.len() % width == 0,
                "invalid ELF relocation table size"
            );
            for at in (0..section.data.len()).step_by(width) {
                let info = u32be(section.data, at + 4)?;
                let symbol_at = (info >> 8) as usize * symbol_width;
                let data = bytes(symtab.data, symbol_at, 16)?;
                let symbol = Symbol {
                    name: string(strings.data, u32be(data, 0)? as usize)?,
                    value: u32be(data, 4)?,
                    size: u32be(data, 8)?,
                    kind: data[12] & 15,
                    binding: data[12] >> 4,
                    section: u16be(data, 14)? as usize,
                };
                out.push(Relocation {
                    section: section.info,
                    offset: u32be(section.data, at)?,
                    kind: info as u8,
                    symbol,
                    addend: u32be(section.data, at + 8)? as i32,
                });
            }
        }
        Ok(out)
    }
    pub fn function_bytes(&self, symbol: &Symbol) -> Result<&'a [u8]> {
        ensure!(symbol.kind == 2, "not a function symbol");
        let section = self
            .sections
            .get(symbol.section)
            .context("invalid ELF function section")?;
        bytes(section.data, symbol.value as usize, symbol.size as usize)
    }
}

/// Same section filtering as corpus.py; NOBITS is compared by size, not offset.
pub fn comparable_sections(data: &[u8]) -> Result<BTreeMap<String, SectionContents>> {
    if !data.starts_with(b"\x7fELF") {
        return Ok(BTreeMap::from([(
            "<file>".into(),
            SectionContents::Data(data.to_vec()),
        )]));
    }
    let elf = Elf::parse(data)?;
    let mut result = BTreeMap::new();
    for section in elf.sections {
        if [".debug", ".line", ".comment"]
            .iter()
            .any(|prefix| section.name.starts_with(prefix))
        {
            continue;
        }
        let contents = if section.kind == 8 {
            SectionContents::Uninitialized(section.size)
        } else {
            SectionContents::Data(section.data.to_vec())
        };
        result.insert(section.name, contents);
    }
    Ok(result)
}

pub fn differences(mine: &[u8], theirs: &[u8]) -> Result<Vec<String>> {
    let a = comparable_sections(mine)?;
    let b = comparable_sections(theirs)?;
    let mut out = vec![];
    for name in a
        .keys()
        .chain(b.keys())
        .collect::<std::collections::BTreeSet<_>>()
    {
        if a.get(name) == b.get(name) {
            continue;
        }
        out.push(if !a.contains_key(name) || !b.contains_key(name) {
            format!(
                "section {name} is only in the {}'s object",
                if a.contains_key(name) {
                    "oracle"
                } else {
                    "original"
                }
            )
        } else {
            format!("section {name} differs")
        });
    }
    if mine.starts_with(b"\x7fELF") && theirs.starts_with(b"\x7fELF") {
        let a = Elf::parse(mine)?;
        let b = Elf::parse(theirs)?;
        let sa: BTreeMap<_, _> = a
            .symbols()?
            .into_iter()
            .filter(|s| s.kind == 2)
            .map(|s| (s.name.clone(), s))
            .collect();
        let sb: BTreeMap<_, _> = b
            .symbols()?
            .into_iter()
            .filter(|s| s.kind == 2)
            .map(|s| (s.name.clone(), s))
            .collect();
        for name in sa
            .keys()
            .chain(sb.keys())
            .collect::<std::collections::BTreeSet<_>>()
        {
            match (sa.get(name), sb.get(name)) {
                (Some(x), Some(y))
                    if x.size != y.size || a.function_bytes(x)? != b.function_bytes(y)? =>
                {
                    out.push(format!(
                        "function {name} differs ({} bytes against {})",
                        x.size, y.size
                    ))
                }
                (None, Some(_)) => {
                    out.push(format!("function {name} is only in the original's object"))
                }
                (Some(_), None) => {
                    out.push(format!("function {name} is only in the oracle's object"))
                }
                _ => (),
            }
        }
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn truncated_elf_is_an_error() {
        for n in 4..60 {
            assert!(Elf::parse(&[b"\x7fELF".as_slice(), &[0; 60]].concat()[..n]).is_err());
        }
    }
    #[test]
    fn non_elf_comparison_preserves_bytes() {
        assert_eq!(
            comparable_sections(b"plain").unwrap()["<file>"],
            SectionContents::Data(b"plain".to_vec())
        );
    }
}
