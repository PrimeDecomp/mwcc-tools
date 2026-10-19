use super::Relocation;
use crate::pe::{bytes, u16le, u32le};
use anyhow::{Context, Result, ensure};
use indexmap::IndexMap;
use std::{borrow::Cow, collections::BTreeMap};
#[derive(Clone, Debug)]
pub struct ObjectSymbol {
    pub name: String,
    pub value: u32,
    pub section: i16,
    pub kind: u16,
    pub storage: u8,
    pub auxiliary: Vec<[u8; 18]>,
    pub offset: usize,
}
#[derive(Clone, Debug)]
pub struct ObjectSection<'a> {
    pub name: String,
    pub flags: u32,
    pub size: u32,
    pub raw_offset: u32,
    pub data: Cow<'a, [u8]>,
    pub relocations: Vec<Relocation>,
    pub relocation_offset: u32,
}
#[derive(Clone, Debug)]
pub struct Object<'a> {
    pub data: &'a [u8],
    pub symbols: IndexMap<u32, ObjectSymbol>,
    pub sections: Vec<ObjectSection<'a>>,
    pub symbol_offset: u32,
    pub symbol_count: u32,
    pub string_offset: usize,
    pub string_size: u32,
}
fn string(data: &[u8], offset: usize) -> Result<String> {
    let data = data
        .get(offset..)
        .context("COFF string offset outside table")?;
    let end = data
        .iter()
        .position(|b| *b == 0)
        .context("unterminated COFF string")?;
    Ok(data[..end].iter().map(|b| char::from(*b)).collect())
}
fn symbol_name(raw: &[u8], strings: &[u8]) -> Result<String> {
    if raw[..4] == [0; 4] {
        string(strings, u32le(raw, 4)? as usize)
    } else {
        let end = raw.iter().rposition(|b| *b != 0).map_or(0, |n| n + 1);
        Ok(raw[..end].iter().map(|b| char::from(*b)).collect())
    }
}
impl<'a> Object<'a> {
    pub fn parse(data: &'a [u8]) -> Result<Self> {
        ensure!(
            u16le(data, 0)? == 0x14c && u16le(data, 16)? == 0,
            "expected relocatable i386 COFF"
        );
        let count = u16le(data, 2)? as usize;
        bytes(
            data,
            20,
            count
                .checked_mul(40)
                .context("COFF section count overflow")?,
        )?;
        let symbol_offset = u32le(data, 8)?;
        let symbol_count = u32le(data, 12)?;
        let string_offset = (symbol_offset as usize)
            .checked_add(
                (symbol_count as usize)
                    .checked_mul(18)
                    .context("COFF symbol count overflow")?,
            )
            .context("COFF table overflow")?;
        let string_size = if symbol_offset == 0 && symbol_count == 0 {
            0
        } else {
            u32le(data, string_offset)?
        };
        ensure!(
            string_size == 0 || string_size >= 4,
            "invalid COFF string table length"
        );
        let strings = if string_size == 0 {
            &[][..]
        } else {
            bytes(data, string_offset, string_size as usize)?
        };
        let mut symbols = IndexMap::new();
        let mut index = 0u32;
        while index < symbol_count {
            let at = symbol_offset as usize + index as usize * 18;
            let raw = bytes(data, at, 18)?;
            let auxiliary_count = u32::from(raw[17]);
            let next = index
                .checked_add(1 + auxiliary_count)
                .context("COFF auxiliary count overflow")?;
            ensure!(
                next <= symbol_count,
                "COFF auxiliary records exceed symbol table"
            );
            let mut auxiliary = vec![];
            for n in 0..auxiliary_count {
                auxiliary.push(bytes(data, at + 18 + n as usize * 18, 18)?.try_into()?);
            }
            symbols.insert(
                index,
                ObjectSymbol {
                    name: symbol_name(&raw[..8], strings)?,
                    value: u32le(raw, 8)?,
                    section: i16::from_le_bytes(raw[12..14].try_into()?),
                    kind: u16le(raw, 14)?,
                    storage: raw[16],
                    auxiliary,
                    offset: at,
                },
            );
            index = next;
        }
        let mut sections = vec![];
        for i in 0..count {
            let at = 20 + 40 * i;
            let raw = bytes(data, at, 8)?;
            let end = raw.iter().position(|b| *b == 0).unwrap_or(8);
            let name = if raw.first() == Some(&b'/') && raw[1..end].iter().all(u8::is_ascii_digit) {
                let offset = std::str::from_utf8(&raw[1..end])?.parse::<usize>()?;
                string(strings, offset)?
            } else {
                raw[..end].iter().map(|b| char::from(*b)).collect()
            };
            let size = u32le(data, at + 16)?;
            let raw_offset = u32le(data, at + 20)?;
            let relocation_offset = u32le(data, at + 24)?;
            let mut relocation_count = u32::from(u16le(data, at + 32)?);
            let flags = u32le(data, at + 36)?;
            let contents = if raw_offset != 0 && flags & 0x80 == 0 {
                Cow::Borrowed(bytes(data, raw_offset as usize, size as usize)?)
            } else {
                ensure!(
                    size <= 512 * 1024 * 1024,
                    "COFF uninitialized section too large"
                );
                Cow::Owned(vec![0; size as usize])
            };
            let mut first = relocation_offset as usize;
            if relocation_count == 0xffff && flags & 0x01000000 != 0 {
                let total = u32le(data, first)?;
                ensure!(
                    total > 0 && u32le(data, first + 4)? == 0 && u16le(data, first + 8)? == 0,
                    "invalid extended COFF relocation count"
                );
                relocation_count = total - 1;
                first += 10;
            }
            bytes(
                data,
                first,
                (relocation_count as usize)
                    .checked_mul(10)
                    .context("COFF relocation table overflow")?,
            )?;
            let mut relocations = vec![];
            for r in 0..relocation_count as usize {
                let offset = first + r * 10;
                let symbol = u32le(data, offset + 4)?;
                ensure!(
                    symbols.contains_key(&symbol),
                    "COFF relocation names missing or auxiliary symbol {symbol}"
                );
                relocations.push(Relocation {
                    offset: u32le(data, offset)?,
                    symbol,
                    kind: u16le(data, offset + 8)?,
                });
            }
            sections.push(ObjectSection {
                name,
                flags,
                size,
                raw_offset,
                data: contents,
                relocations,
                relocation_offset,
            });
        }
        Ok(Self {
            data,
            symbols,
            sections,
            symbol_offset,
            symbol_count,
            string_offset,
            string_size,
        })
    }
}
#[derive(Clone, Debug)]
pub struct BuildSection {
    pub name: String,
    pub flags: u32,
    pub data: Vec<u8>,
    pub size: u32,
    pub relocations: Vec<Relocation>,
}
#[derive(Clone, Debug)]
pub struct Builder {
    pub sections: Vec<BuildSection>,
    pub index: IndexMap<String, u32>,
    pub section_symbols: BTreeMap<i16, u32>,
    symbols: Vec<u8>,
    strings: Vec<u8>,
}
impl Default for Builder {
    fn default() -> Self {
        Self {
            sections: vec![],
            index: IndexMap::new(),
            section_symbols: BTreeMap::new(),
            symbols: vec![],
            strings: vec![0; 4],
        }
    }
}
impl Builder {
    pub fn add_symbol(
        &mut self,
        name: &str,
        value: u32,
        section: i16,
        kind: u16,
        storage: u8,
        auxiliary: &[u8],
    ) -> Result<u32> {
        ensure!(
            auxiliary.len().is_multiple_of(18) && auxiliary.len() / 18 <= u8::MAX as usize,
            "invalid COFF auxiliary bytes"
        );
        ensure!(
            name.chars().all(|c| (c as u32) <= 255),
            "COFF symbol is not Latin-1"
        );
        let encoded: Vec<_> = name.chars().map(|c| c as u8).collect();
        let index = u32::try_from(self.symbols.len() / 18)?;
        if encoded.len() <= 8 {
            self.symbols.extend(&encoded);
            self.symbols
                .resize(self.symbols.len() + 8 - encoded.len(), 0);
        } else {
            self.symbols.extend([0; 4]);
            self.symbols
                .extend(u32::try_from(self.strings.len())?.to_le_bytes());
            self.strings.extend(encoded);
            self.strings.push(0);
        }
        self.symbols.extend(value.to_le_bytes());
        self.symbols.extend(section.to_le_bytes());
        self.symbols.extend(kind.to_le_bytes());
        self.symbols.extend([storage, (auxiliary.len() / 18) as u8]);
        self.symbols.extend(auxiliary);
        Ok(index)
    }
    pub fn external(&mut self, name: &str) -> Result<u32> {
        if let Some(index) = self.index.get(name) {
            return Ok(*index);
        }
        let index = self.add_symbol(name, 0, 0, 0, 2, &[])?;
        self.index.insert(name.into(), index);
        Ok(index)
    }
    pub fn section(
        &mut self,
        name: &str,
        flags: u32,
        data: Vec<u8>,
        size: u32,
        relocations: Vec<Relocation>,
    ) -> Result<i16> {
        let number = i16::try_from(self.sections.len() + 1)?;
        let mut aux = vec![];
        aux.extend(size.to_le_bytes());
        aux.extend(u16::try_from(relocations.len())?.to_le_bytes());
        aux.extend([0; 12]);
        let symbol = self.add_symbol(name, 0, number, 0, 3, &aux)?;
        self.section_symbols.insert(number, symbol);
        self.sections.push(BuildSection {
            name: name.into(),
            flags,
            data,
            size,
            relocations,
        });
        Ok(number)
    }
    pub fn refresh_section_definitions(&mut self) -> Result<()> {
        // Callers may fill relocations or rewrite contents after creating a
        // section. Keep its auxiliary definition consistent with the header.
        for (&number, &symbol) in &self.section_symbols {
            let section = &self.sections[number as usize - 1];
            let at = symbol as usize * 18 + 18;
            self.symbols[at..at + 4].copy_from_slice(&section.size.to_le_bytes());
            self.symbols[at + 4..at + 6]
                .copy_from_slice(&u16::try_from(section.relocations.len())?.to_le_bytes());
        }
        Ok(())
    }
    pub fn finish(mut self) -> Result<Vec<u8>> {
        let header_size = 20 + 40 * self.sections.len();
        let mut body = vec![];
        let mut headers = vec![];
        for section in &self.sections {
            let data_offset = if section.data.is_empty() {
                0
            } else {
                u32::try_from(header_size + body.len())?
            };
            body.extend(&section.data);
            let relocation_offset = if section.relocations.is_empty() {
                0
            } else {
                u32::try_from(header_size + body.len())?
            };
            for r in &section.relocations {
                ensure!(
                    (r.symbol as usize) < self.symbols.len() / 18,
                    "relocation names missing symbol"
                );
                body.extend(r.offset.to_le_bytes());
                body.extend(r.symbol.to_le_bytes());
                body.extend(r.kind.to_le_bytes());
            }
            let raw = if section.name.len() <= 8 {
                section.name.as_bytes().to_vec()
            } else {
                let offset = self.strings.len();
                self.strings.extend(section.name.as_bytes());
                self.strings.push(0);
                format!("/{offset}").into_bytes()
            };
            ensure!(
                raw.len() <= 8,
                "COFF long-section offset does not fit header"
            );
            headers.extend(&raw);
            headers.resize(headers.len() + 8 - raw.len(), 0);
            headers.extend([0; 8]);
            headers.extend(section.size.to_le_bytes());
            headers.extend(data_offset.to_le_bytes());
            headers.extend(relocation_offset.to_le_bytes());
            headers.extend([0; 4]);
            headers.extend(u16::try_from(section.relocations.len())?.to_le_bytes());
            headers.extend([0; 2]);
            headers.extend(section.flags.to_le_bytes());
        }
        let length = u32::try_from(self.strings.len())?.to_le_bytes();
        self.strings[..4].copy_from_slice(&length);
        let mut out = vec![];
        out.extend(0x14cu16.to_le_bytes());
        out.extend(u16::try_from(self.sections.len())?.to_le_bytes());
        out.extend([0; 4]);
        out.extend(u32::try_from(header_size + body.len())?.to_le_bytes());
        out.extend(u32::try_from(self.symbols.len() / 18)?.to_le_bytes());
        out.extend([0; 4]);
        out.extend(headers);
        out.extend(body);
        out.extend(self.symbols);
        out.extend(self.strings);
        Ok(out)
    }
}
