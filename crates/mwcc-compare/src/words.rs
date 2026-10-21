//! A PowerPC object's functions as instruction words and the relocations on them, read with `object`.
use anyhow::Result;
use object::{
    Object as _, ObjectSection as _, ObjectSymbol as _, RelocationFlags, SectionIndex, SymbolKind,
};
use std::collections::HashMap;

use crate::strict::{elf::RelocKind, reloc_mask};

/// A function's code: its words (big-endian) and its relocations (offset in the function, ELF `R_PPC_*` type).
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Code {
    pub words: Vec<u32>,
    pub relocations: Vec<(u32, u32)>,
}

impl Code {
    /// The words with each relocated field zeroed (mwdec's `masked_words`).
    pub fn masked(&self) -> Vec<u32> {
        let mut words = self.words.clone();
        for &(offset, kind) in &self.relocations {
            if let Some(word) = words.get_mut((offset / 4) as usize) {
                *word &= !mask(kind);
            }
        }
        words
    }
}

/// The bits of an instruction word a relocation of ELF type KIND covers ([`reloc_mask`] of its kind).
pub fn mask(kind: u32) -> u32 {
    reloc_mask(RelocKind::of(kind))
}

/// Each function of the object DATA (a text symbol with a size) by name; of two with one name, the first.
pub fn functions(data: &[u8]) -> Result<HashMap<String, Code>> {
    let file = object::File::parse(data)?;
    // (each section's relocations once, by offset)
    let mut relocations: HashMap<SectionIndex, Vec<(u64, u32)>> = HashMap::new();
    for section in file.sections() {
        let mut list: Vec<(u64, u32)> = section
            .relocations()
            .map(|(offset, r)| {
                let kind = match r.flags() {
                    RelocationFlags::Elf { r_type } => r_type,
                    _ => 0,
                };
                (offset, kind)
            })
            .collect();
        list.sort_unstable();
        relocations.insert(section.index(), list);
    }
    let mut out = HashMap::new();
    for symbol in file.symbols() {
        if symbol.kind() != SymbolKind::Text || symbol.size() == 0 {
            continue;
        }
        let (Ok(name), Some(index)) = (symbol.name(), symbol.section_index()) else {
            continue;
        };
        if out.contains_key(name) {
            continue;
        }
        let section = file.section_by_index(index)?;
        let bytes = section.data()?;
        let (start, size) = (symbol.address() - section.address(), symbol.size());
        let Some(code) = bytes.get(start as usize..(start + size) as usize) else {
            continue;
        };
        let words = code
            .chunks_exact(4)
            .map(|w| u32::from_be_bytes([w[0], w[1], w[2], w[3]]))
            .collect();
        let list = relocations.get(&index).map_or(&[][..], Vec::as_slice);
        let from = list.partition_point(|(offset, _)| *offset < start);
        let relocations = list[from..]
            .iter()
            .take_while(|(offset, _)| *offset < start + size)
            .map(|&(offset, kind)| ((offset - start) as u32, kind))
            .collect();
        out.insert(name.to_owned(), Code { words, relocations });
    }
    Ok(out)
}
