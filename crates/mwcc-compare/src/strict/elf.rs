//! MWCC and dtk ELF objects as the strict comparator reads them (mwdec-obj's loader, CC0): functions, data symbols,
//! allocated sections and every defined symbol, with relocations resolved to the named symbol that contains their
//! target.
//!
//! - target objects (dtk's split of the original binary) relocate against named symbols (`fn_...`, `lbl_...`,
//!   `@123`, `...data.0`);
//! - our objects (MWCC's) may relocate against section symbols: those are resolved to the containing named symbol
//!   and an addend.
use anyhow::{Context, Result};
use object::{
    Object as _, ObjectSection, ObjectSymbol, RelocationFlags, RelocationTarget, SectionFlags,
    SectionIndex, SymbolKind, SymbolScope, elf,
};
use std::collections::{BTreeMap, HashMap};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum RelocKind {
    /// R_PPC_ADDR32
    Addr32,
    /// R_PPC_ADDR16_LO
    Addr16Lo,
    /// R_PPC_ADDR16_HI
    Addr16Hi,
    /// R_PPC_ADDR16_HA
    Addr16Ha,
    /// R_PPC_REL24 (`bl`, `b`)
    Rel24,
    /// R_PPC_REL14 (`bc`)
    Rel14,
    /// R_PPC_EMB_SDA21 (r2/r13 small data)
    EmbSda21,
    Other(u32),
}

impl RelocKind {
    /// The kind of ELF `R_PPC_*` type R_TYPE.
    pub fn of(r_type: u32) -> Self {
        match r_type {
            1 => RelocKind::Addr32,
            4 => RelocKind::Addr16Lo,
            5 => RelocKind::Addr16Hi,
            6 => RelocKind::Addr16Ha,
            10 => RelocKind::Rel24,
            11 => RelocKind::Rel14,
            109 => RelocKind::EmbSda21,
            n => RelocKind::Other(n),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Reloc {
    /// The byte offset from the start of the function, data symbol or section.
    pub offset: u32,
    pub kind: RelocKind,
    /// The target symbol's name, as in the object's symbol table.
    pub target: String,
    pub addend: i64,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Binding {
    Global,
    Local,
    Weak,
}

#[derive(Clone, Debug)]
pub struct Function {
    /// The mangled name.
    pub name: String,
    pub binding: Binding,
    /// Its address in its section.
    pub address: u32,
    /// Big-endian machine code.
    pub code: Vec<u8>,
    pub relocs: Vec<Reloc>,
}

impl Function {
    pub fn words(&self) -> impl Iterator<Item = u32> + '_ {
        self.code
            .chunks_exact(4)
            .map(|c| u32::from_be_bytes([c[0], c[1], c[2], c[3]]))
    }
}

#[derive(Clone, Debug)]
pub struct DataSymbol {
    pub name: String,
    pub binding: Binding,
    /// `.data`, `.rodata`, `.sdata`, `.sdata2`, `.bss`, `.sbss`, ...
    pub section: String,
    pub size: u32,
    /// Empty in a bss-like section.
    pub bytes: Vec<u8>,
    pub relocs: Vec<Reloc>,
    /// Its address in its section.
    pub address: u32,
}

/// An allocated section, kept so that a literal can be read at any symbol and addend (a string pool's strings run
/// past the symbol's size).
#[derive(Clone, Debug, Default)]
pub struct Section {
    pub name: String,
    pub size: u32,
    /// Empty for a bss-like (NOBITS) section.
    pub bytes: Vec<u8>,
    pub executable: bool,
    /// The section's relocations, their offsets from its start.
    pub relocs: Vec<Reloc>,
}

/// A defined named symbol.
#[derive(Clone, Debug)]
pub struct SymbolDef {
    pub name: String,
    /// The section it is in.
    pub section: String,
    /// Its address in that section.
    pub address: u32,
    pub size: u32,
    pub binding: Binding,
    /// STT_FUNC.
    pub is_func: bool,
}

#[derive(Clone, Debug, Default)]
pub struct Object {
    pub functions: Vec<Function>,
    pub data: BTreeMap<String, DataSymbol>,
    /// Every symbol name (defined or not).
    pub all_symbols: Vec<String>,
    /// The allocated sections (code and data), in the object's order.
    pub sections: Vec<Section>,
    /// Every defined named symbol (functions, data, local labels), by section and address.
    pub symbols: Vec<SymbolDef>,
}

impl Object {
    /// The function named NAME (mangled).
    pub fn function(&self, name: &str) -> Option<&Function> {
        self.functions.iter().find(|f| f.name == name)
    }
}

#[derive(Clone)]
struct SymInfo {
    name: String,
    kind: SymbolKind,
    address: u32,
    size: u32,
    binding: Binding,
}

fn binding_of(sym: &object::Symbol<'_, '_>) -> Binding {
    if sym.is_weak() {
        Binding::Weak
    } else if sym.scope() == SymbolScope::Compilation || sym.is_local() {
        Binding::Local
    } else {
        Binding::Global
    }
}

/// The object DATA: every function (STT_FUNC in an executable section, of any binding) and data symbol, with their
/// relocations.
pub fn load(data: &[u8]) -> Result<Object> {
    let file = object::File::parse(data).context("parse the ELF object")?;

    // (the allocated sections)
    let mut sec_map: HashMap<SectionIndex, usize> = HashMap::new();
    let mut sections: Vec<Section> = Vec::new();
    for sec in file.sections() {
        let (alloc, exec) = match sec.flags() {
            SectionFlags::Elf { sh_flags } => (
                sh_flags & elf::SHF_ALLOC as u64 != 0,
                sh_flags & elf::SHF_EXECINSTR as u64 != 0,
            ),
            _ => (false, false),
        };
        if !alloc {
            continue;
        }
        let bytes = if sec.kind() == object::SectionKind::UninitializedData {
            Vec::new()
        } else {
            sec.data().unwrap_or(&[]).to_vec()
        };
        sec_map.insert(sec.index(), sections.len());
        sections.push(Section {
            name: sec.name().unwrap_or("").to_string(),
            size: sec.size() as u32,
            bytes,
            executable: exec,
            relocs: Vec::new(),
        });
    }

    // (the defined named symbols of each section, by address)
    let mut all_symbols = Vec::new();
    let mut per_sec: Vec<Vec<SymInfo>> = vec![Vec::new(); sections.len()];
    for sym in file.symbols() {
        let name = sym.name().unwrap_or("");
        if name.is_empty() || matches!(sym.kind(), SymbolKind::Section | SymbolKind::File) {
            continue;
        }
        all_symbols.push(name.to_string());
        let Some(si) = sym.section_index().and_then(|i| sec_map.get(&i).copied()) else {
            continue;
        };
        per_sec[si].push(SymInfo {
            name: name.to_string(),
            kind: sym.kind(),
            address: sym.address() as u32,
            size: sym.size() as u32,
            binding: binding_of(&sym),
        });
    }
    for v in per_sec.iter_mut() {
        // (sized symbols first at an address, then globals before locals)
        v.sort_by_key(|s| (s.address, s.size == 0, s.binding == Binding::Local));
    }

    let sec_names: Vec<String> = sections.iter().map(|s| s.name.clone()).collect();
    // (a section and offset: the named symbol that contains it, innermost first)
    let resolve = |si: usize, off: i64| -> (String, i64) {
        let mut best: Option<&SymInfo> = None;
        for s in &per_sec[si] {
            if (s.address as i64) > off {
                break;
            }
            let end = s.address as i64 + s.size as i64;
            let contains = off < end || (s.size == 0 && s.address as i64 == off);
            if contains {
                match best {
                    Some(b) if b.address >= s.address => {}
                    _ => best = Some(s),
                }
            }
        }
        match best {
            Some(s) => (s.name.clone(), off - s.address as i64),
            None => (sec_names[si].clone(), off),
        }
    };

    // (the relocations of each allocated section)
    let mut all_relocs: Vec<Vec<Reloc>> = vec![Vec::new(); sections.len()];
    for sec in file.sections() {
        let Some(&si) = sec_map.get(&sec.index()) else {
            continue;
        };
        let mut relocs = Vec::new();
        for (offset, rel) in sec.relocations() {
            let r_type = match rel.flags() {
                RelocationFlags::Elf { r_type } => r_type,
                _ => 0,
            };
            let addend = rel.addend();
            let (target, addend) = match rel.target() {
                RelocationTarget::Symbol(idx) => {
                    let sym = file.symbol_by_index(idx)?;
                    if sym.kind() == SymbolKind::Section {
                        match sym.section_index().and_then(|i| sec_map.get(&i).copied()) {
                            Some(tsi) => resolve(tsi, sym.address() as i64 + addend),
                            None => (sym.name().unwrap_or("?").to_string(), addend),
                        }
                    } else {
                        (sym.name().unwrap_or("?").to_string(), addend)
                    }
                }
                RelocationTarget::Section(idx) => match sec_map.get(&idx).copied() {
                    Some(tsi) => resolve(tsi, addend),
                    None => ("?section".to_string(), addend),
                },
                _ => ("?".to_string(), addend),
            };
            relocs.push(Reloc {
                offset: offset as u32,
                kind: RelocKind::of(r_type),
                target,
                addend,
            });
        }
        relocs.sort_by_key(|r| r.offset);
        all_relocs[si] = relocs;
    }
    for (s, r) in sections.iter_mut().zip(all_relocs) {
        s.relocs = r;
    }

    let relocs_in = |si: usize, start: u32, size: u32| -> Vec<Reloc> {
        let rs = &sections[si].relocs;
        let lo = rs.partition_point(|r| r.offset < start);
        rs[lo..]
            .iter()
            .take_while(|r| r.offset < start + size)
            .map(|r| Reloc {
                offset: r.offset - start,
                ..r.clone()
            })
            .collect()
    };
    let bytes_of = |si: usize, start: u32, size: u32| -> Vec<u8> {
        let b = &sections[si].bytes;
        let s = (start as usize).min(b.len());
        let e = (start as usize + size as usize).min(b.len());
        b[s..e].to_vec()
    };

    let mut symbols = Vec::new();
    for (si, syms) in per_sec.iter().enumerate() {
        for s in syms {
            symbols.push(SymbolDef {
                name: s.name.clone(),
                section: sections[si].name.clone(),
                address: s.address,
                size: s.size,
                binding: s.binding,
                is_func: s.kind == SymbolKind::Text,
            });
        }
    }

    let mut functions = Vec::new();
    let mut data = BTreeMap::new();
    for (si, syms) in per_sec.iter().enumerate() {
        let exec = sections[si].executable;
        for s in syms {
            if exec {
                if s.kind != SymbolKind::Text {
                    continue;
                }
                let size = if s.size != 0 {
                    s.size
                } else {
                    // (unsized: up to the next symbol or the section's end)
                    let next = syms
                        .iter()
                        .map(|o| o.address)
                        .filter(|&a| a > s.address)
                        .min();
                    next.unwrap_or(sections[si].size) - s.address
                };
                functions.push(Function {
                    name: s.name.clone(),
                    binding: s.binding,
                    address: s.address,
                    code: bytes_of(si, s.address, size),
                    relocs: relocs_in(si, s.address, size),
                });
            } else if s.kind != SymbolKind::Text {
                data.entry(s.name.clone()).or_insert_with(|| DataSymbol {
                    name: s.name.clone(),
                    binding: s.binding,
                    section: sections[si].name.clone(),
                    size: s.size,
                    bytes: bytes_of(si, s.address, s.size),
                    relocs: relocs_in(si, s.address, s.size),
                    address: s.address,
                });
            }
        }
    }

    Ok(Object {
        functions,
        data,
        all_symbols,
        sections,
        symbols,
    })
}

/// The instruction WORD at offset OFF of a function, disassembled (a direct branch's target as an offset in it).
pub fn disassemble_word(word: u32, off: u32) -> String {
    let ins = powerpc::Ins::new(word, powerpc::Extensions::gekko_broadway());
    let mut text = ins.simplified().to_string();
    if ins.is_direct_branch()
        && let Some(dest) = ins.branch_dest(off)
        && let Some(pos) = text.rfind([' ', ','])
    {
        text.truncate(pos + 1);
        text.push_str(&format!("0x{dest:x}"));
    }
    text
}
