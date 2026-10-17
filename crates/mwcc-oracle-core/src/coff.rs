//! Deterministic i386 COFF and import-library construction for the historical linker.
use anyhow::{Result, ensure};
use regex::Regex;
use serde::{Deserialize, Serialize};
mod object;
mod patch;
pub use object::{BuildSection, Builder, Object, ObjectSection, ObjectSymbol};
pub use patch::{defined_symbols, hook_symbols, section_sizes, undefine_symbols};
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Relocation {
    pub offset: u32,
    pub symbol: u32,
    pub kind: u16,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Section {
    pub name: String,
    pub flags: u32,
    pub data: Vec<u8>,
    pub relocations: Vec<Relocation>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Symbol {
    pub name: String,
    pub value: u32,
    pub section: i16,
    pub storage: u8,
}
pub fn write(sections: &[Section], symbols: &[Symbol]) -> Result<Vec<u8>> {
    ensure!(
        sections.len() <= u16::MAX as usize,
        "too many COFF sections"
    );
    ensure!(symbols.len() <= u32::MAX as usize, "too many COFF symbols");
    let mut strings = vec![0; 4];
    let mut table = vec![];
    for symbol in symbols {
        let raw = symbol.name.as_bytes();
        if raw.len() <= 8 {
            table.extend(raw);
            table.resize(table.len() + 8 - raw.len(), 0);
        } else {
            ensure!(
                strings.len() <= u32::MAX as usize,
                "COFF string table too large"
            );
            table.extend([0; 4]);
            table.extend((strings.len() as u32).to_le_bytes());
            strings.extend(raw);
            strings.push(0);
        }
        table.extend(symbol.value.to_le_bytes());
        table.extend(symbol.section.to_le_bytes());
        table.extend(0u16.to_le_bytes());
        table.extend([symbol.storage, 0]);
    }
    let header_size = 20 + 40 * sections.len();
    let mut body = vec![];
    let mut headers = vec![];
    for section in sections {
        ensure!(section.name.len() <= 8, "COFF section name too long");
        ensure!(
            section.relocations.len() <= u16::MAX as usize,
            "too many COFF relocations"
        );
        ensure!(
            header_size + body.len() + section.data.len() <= u32::MAX as usize,
            "COFF data too large"
        );
        let offset = (header_size + body.len()) as u32;
        body.extend(&section.data);
        let relocation = if section.relocations.is_empty() {
            0
        } else {
            (header_size + body.len()) as u32
        };
        for r in &section.relocations {
            ensure!(
                (r.symbol as usize) < symbols.len(),
                "COFF relocation refers to missing symbol"
            );
            body.extend(r.offset.to_le_bytes());
            body.extend(r.symbol.to_le_bytes());
            body.extend(r.kind.to_le_bytes());
        }
        headers.extend(section.name.as_bytes());
        headers.resize(headers.len() + 8 - section.name.len(), 0);
        headers.extend([0; 8]);
        headers.extend((section.data.len() as u32).to_le_bytes());
        headers.extend(offset.to_le_bytes());
        headers.extend(relocation.to_le_bytes());
        headers.extend([0; 4]);
        headers.extend((section.relocations.len() as u16).to_le_bytes());
        headers.extend([0; 2]);
        headers.extend(section.flags.to_le_bytes());
    }
    ensure!(
        strings.len() <= u32::MAX as usize && header_size + body.len() <= u32::MAX as usize,
        "COFF table too large"
    );
    let length = (strings.len() as u32).to_le_bytes();
    strings[..4].copy_from_slice(&length);
    let mut out = vec![];
    out.extend(0x14cu16.to_le_bytes());
    out.extend((sections.len() as u16).to_le_bytes());
    out.extend([0; 4]);
    out.extend(((header_size + body.len()) as u32).to_le_bytes());
    out.extend((symbols.len() as u32).to_le_bytes());
    out.extend([0; 4]);
    out.extend(headers);
    out.extend(body);
    out.extend(table);
    out.extend(strings);
    Ok(out)
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Export {
    pub symbol: String,
    pub ordinal: u16,
    pub named: bool,
}
pub fn definition(text: &str) -> Result<(String, Vec<Export>)> {
    let library = Regex::new(r"(?m)^LIBRARY\s+(\S+)")?;
    let mut dll = library
        .captures(text)
        .ok_or_else(|| anyhow::anyhow!("definition has no LIBRARY"))?[1]
        .to_owned();
    if !dll.contains('.') {
        dll.push_str(".dll");
    }
    let export = Regex::new(r"(?m)^\s+([\w@]+)\s+@(\d+)(\s+NONAME)?\s*$")?;
    let exports = export
        .captures_iter(text)
        .map(|c| {
            Ok(Export {
                symbol: c[1].into(),
                ordinal: c[2].parse()?,
                named: c.get(3).is_none(),
            })
        })
        .collect::<Result<Vec<_>>>()?;
    Ok((dll, exports))
}
pub fn import_library(dll: &str, exports: &[Export]) -> Result<Vec<u8>> {
    import_library_of(dll, exports, false)
}
/// An import library for DLL, in the format Microsoft's LIB writes. Each export is data (its `__imp_` pointer), or
/// with THUNKS a function with its thunk as well.
pub fn import_library_of(dll: &str, exports: &[Export], thunks: bool) -> Result<Vec<u8>> {
    let base = dll.rsplit_once('.').map_or(dll, |(b, _)| b);
    let descriptor = format!("__IMPORT_DESCRIPTOR_{base}");
    let null_descriptor = "__NULL_IMPORT_DESCRIPTOR";
    let null_thunk = format!("\x7f{base}_NULL_THUNK_DATA");
    let mut name = dll.as_bytes().to_vec();
    name.push(0);
    if !name.len().is_multiple_of(2) {
        name.push(0);
    }
    let section = |name: &str, flags: u32, data: Vec<u8>, relocations: Vec<Relocation>| Section {
        name: name.into(),
        flags,
        data,
        relocations,
    };
    let symbol = |name: &str, value: u32, section: i16, storage: u8| Symbol {
        name: name.into(),
        value,
        section,
        storage,
    };
    let mut members = vec![
        (
            write(
                &[
                    section(
                        ".idata$2",
                        0xc0300040,
                        vec![0; 20],
                        vec![
                            Relocation {
                                offset: 12,
                                symbol: 2,
                                kind: 7,
                            },
                            Relocation {
                                offset: 0,
                                symbol: 3,
                                kind: 7,
                            },
                            Relocation {
                                offset: 16,
                                symbol: 4,
                                kind: 7,
                            },
                        ],
                    ),
                    section(".idata$6", 0xc0200040, name, vec![]),
                ],
                &[
                    symbol(&descriptor, 0, 1, 2),
                    symbol(".idata$2", 0xc0000040, 1, 104),
                    symbol(".idata$6", 0, 2, 3),
                    symbol(".idata$4", 0xc0000040, 0, 104),
                    symbol(".idata$5", 0xc0000040, 0, 104),
                    symbol(null_descriptor, 0, 0, 2),
                    symbol(&null_thunk, 0, 0, 2),
                ],
            )?,
            vec![descriptor],
        ),
        (
            write(
                &[section(".idata$3", 0xc0300040, vec![0; 20], vec![])],
                &[symbol(null_descriptor, 0, 1, 2)],
            )?,
            vec![null_descriptor.into()],
        ),
        (
            write(
                &[
                    section(".idata$5", 0xc0300040, vec![0; 4], vec![]),
                    section(".idata$4", 0xc0300040, vec![0; 4], vec![]),
                ],
                &[symbol(&null_thunk, 0, 1, 2)],
            )?,
            vec![null_thunk],
        ),
    ];
    for export in exports {
        let strings = format!("_{}\0{dll}\0", export.symbol);
        let mut header = vec![];
        header.extend(0u16.to_le_bytes());
        header.extend(0xffffu16.to_le_bytes());
        header.extend(0u16.to_le_bytes());
        header.extend(0x14cu16.to_le_bytes());
        header.extend([0; 4]);
        header.extend((strings.len() as u32).to_le_bytes());
        header.extend(export.ordinal.to_le_bytes());
        let kind = if thunks { 0u16 } else { 1 };
        header.extend((kind | if export.named { 3 << 2 } else { 0 }).to_le_bytes());
        header.extend(strings.as_bytes());
        let mut names = vec![format!("__imp__{}", export.symbol)];
        if thunks {
            names.push(format!("_{}", export.symbol));
        }
        members.push((header, names));
    }
    let symbol_members: Vec<_> = members
        .iter()
        .enumerate()
        .flat_map(|(i, (_, names))| names.iter().map(move |s| (s, i)))
        .collect();
    let first_size = 4
        + 4 * symbol_members.len()
        + symbol_members
            .iter()
            .map(|(s, _)| s.len() + 1)
            .sum::<usize>();
    let second_size = 4
        + 4 * members.len()
        + 4
        + 2 * symbol_members.len()
        + symbol_members
            .iter()
            .map(|(s, _)| s.len() + 1)
            .sum::<usize>();
    let mut offset = 8 + 60 + first_size + (first_size & 1) + 60 + second_size + (second_size & 1);
    let mut offsets = vec![];
    for (data, _) in &members {
        ensure!(offset <= u32::MAX as usize, "import library too large");
        offsets.push(offset as u32);
        offset += 60 + data.len() + (data.len() & 1);
    }
    let mut first = vec![];
    first.extend((symbol_members.len() as u32).to_be_bytes());
    for (_, i) in &symbol_members {
        first.extend(offsets[*i].to_be_bytes());
    }
    for (symbol, _) in &symbol_members {
        first.extend(symbol.as_bytes());
        first.push(0);
    }
    let mut ordered = symbol_members;
    ordered.sort();
    let mut second = vec![];
    second.extend((members.len() as u32).to_le_bytes());
    for offset in offsets {
        second.extend(offset.to_le_bytes());
    }
    second.extend((ordered.len() as u32).to_le_bytes());
    ensure!(
        members.len() <= u16::MAX as usize,
        "import archive has too many members"
    );
    for (_, i) in &ordered {
        second.extend((*i as u16 + 1).to_le_bytes());
    }
    for (symbol, _) in ordered {
        second.extend(symbol.as_bytes());
        second.push(0);
    }
    let mut out = b"!<arch>\n".to_vec();
    fn add(out: &mut Vec<u8>, name: &str, data: &[u8]) -> Result<()> {
        ensure!(name.len() <= 16, "archive member name too long");
        let header = format!(
            "{name:<16}{:<12}{:<6}{:<6}{:<8}{:<10}`\n",
            0,
            "",
            "",
            "0",
            data.len()
        );
        ensure!(header.len() == 60, "invalid archive header");
        out.extend(header.as_bytes());
        out.extend(data);
        if !data.len().is_multiple_of(2) {
            out.push(b'\n');
        }
        Ok(())
    }
    add(&mut out, "/", &first)?;
    add(&mut out, "/", &second)?;
    for (data, _) in members {
        add(&mut out, &format!("{dll}/"), &data)?;
    }
    Ok(out)
}
