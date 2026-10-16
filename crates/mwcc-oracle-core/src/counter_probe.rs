//! Fixed-size call islands execute the original counter write and then observe it.
//! Original sections never grow, so jump tables and existing code views stay valid.
use crate::{
    backend_audit::{self, Reference},
    backend_probe,
    coff::{Builder, Object, Relocation},
    disassembly,
    executable::{self, Original},
};
use anyhow::{Context, Result, bail, ensure};
use capstone::{
    RegId,
    arch::{
        DetailsArchInsn,
        x86::{X86OperandType, X86Reg},
    },
};
use serde::Serialize;
use std::{
    collections::{BTreeMap, BTreeSet},
    fs,
    path::Path,
};

#[derive(Clone, Debug, Serialize)]
pub struct Site {
    pub address: u32,
    pub container: String,
    pub offset: u32,
    pub role: Role,
    pub selector: i32,
    pub bytes: Vec<u8>,
    pub field: Option<usize>,
    pub counter_class: Option<u8>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[repr(u32)]
#[serde(rename_all = "snake_case")]
pub enum Role {
    Increment = 1,
    RestoreHighWater = 2,
    Rewind = 3,
    InitialReset = 4,
    AllocationReset = 5,
    FunctionBegin = 6,
    FunctionEnd = 7,
}
#[derive(Debug)]
pub struct Installed {
    pub objects: BTreeMap<String, Vec<u8>>,
    pub islands: Vec<u8>,
    pub sites: Vec<Site>,
}
const COUNTS: &str = "_data_005e6a7c";
const PHYSICAL: &str = "_data_005e6778";
const HANDLER: &str = "_OracleRust_CounterProbe";
// Roles: increment, close/high-water restore, rewind, initial reset, allocation
// reset, function begin, function end. These are actual writes, not guessed types.
fn role(version: &str, r: &Reference) -> Result<Role> {
    if r.instruction.starts_with("inc ") {
        return Ok(Role::Increment);
    }
    match r.function.as_str() {
        "_fn_004fca00" => Ok(Role::RestoreHighWater),
        "_fn_004fcc60" => Ok(Role::InitialReset),
        "_fn_00506cd0" => Ok(Role::AllocationReset),
        "_fn_004fca40" if version == "GC_1_3" => Ok(Role::Rewind),
        "_fn_004fd4f0" if version == "GC_1_3_2" => Ok(Role::Rewind),
        "_fn_004fe270" if version == "GC_2_7" => Ok(Role::Rewind),
        _ => bail!("unclassified retail counter write at {:#x}", r.address),
    }
}
pub fn plan(root: &Path, version: &str) -> Result<Vec<Site>> {
    let audit = backend_audit::capture(root, version)?;
    ensure!(
        audit.unresolved.is_empty()
            && audit
                .references
                .iter()
                .all(|r| r.access_known && !r.address_taken && r.function_decoded_completely),
        "counter audit has unresolved references or aliases"
    );
    let original = Original::read(root, version)?;
    let pe = original.pe()?;
    let metadata = executable::read_symbols(&fs::read_to_string(
        root.join(format!("config/{version}/symbols.txt")),
    )?)?;
    let mut decoder = disassembly::decoder()?;
    decoder.set_detail(true)?;
    let mut sites = vec![];
    for r in audit.references.iter().filter(|r| r.write) {
        let instructions =
            decoder.disasm_count(pe.read(r.address, 15)?, u64::from(r.address), 1)?;
        let i = instructions
            .iter()
            .next()
            .context("missing counter instruction")?;
        let detail = decoder.insn_detail(i)?;
        let arch = detail.arch_detail();
        let x86 = arch.x86().context("not x86")?;
        let operands: Vec<_> = x86.operands().collect();
        ensure!(i.bytes().len()>=5 && operands.iter().all(|op|!matches!(op.op_type,X86OperandType::Reg(RegId(r)) if r==X86Reg::X86_REG_ESP as u16)),
            "counter island cannot relocate an ESP operand");
        let mem = operands
            .iter()
            .find_map(|op| match op.op_type {
                X86OperandType::Mem(m) if op.access.is_some_and(|a| a.is_writable()) => {
                    Some((op.size, m))
                }
                _ => None,
            })
            .context("counter write has no memory destination")?;
        ensure!(
            mem.0 == 4 && mem.1.base() == RegId(0),
            "counter write uses an unsupported memory base or width"
        );
        let (selector, counter_class) = if mem.1.index() == RegId(0) {
            let class = audit
                .counters
                .iter()
                .find(|(_, a)| **a == mem.1.disp() as u32)
                .map(|(n, _)| match n.as_str() {
                    "VR" => 2,
                    "FPR" => 3,
                    "GPR" => 4,
                    _ => unreachable!(),
                })
                .context("absolute write outside known counter slots")?;
            (class, Some(class as u8))
        } else {
            ensure!(
                mem.1.scale() == 4 && mem.1.disp() as u32 == audit.table_base,
                "unexpected indexed counter address"
            );
            let slot = match mem.1.index().0 as u32 {
                x if x == X86Reg::X86_REG_EDI => 0,
                x if x == X86Reg::X86_REG_ESI => 1,
                x if x == X86Reg::X86_REG_EBP => 2,
                x if x == X86Reg::X86_REG_EBX => 4,
                x if x == X86Reg::X86_REG_EDX => 5,
                x if x == X86Reg::X86_REG_ECX => 6,
                x if x == X86Reg::X86_REG_EAX => 7,
                _ => bail!("unsupported counter index register"),
            };
            (-1 - slot, None)
        };
        ensure!(
            r.relocations.len() == 1,
            "counter write does not have one absolute fixup"
        );
        sites.push(Site {
            address: r.address,
            container: r.function.clone(),
            offset: r.offset,
            role: role(version, r)?,
            selector,
            bytes: i.bytes().to_vec(),
            field: Some((r.relocations[0] - r.address) as usize),
            counter_class,
        });
    }
    let expected = match version {
        "GC_1_3" => 570,
        "GC_1_3_2" => 571,
        "GC_2_7" => 578,
        _ => unreachable!(),
    };
    ensure!(
        sites.len() == expected,
        "unexpected number of retail counter writes"
    );
    // The true generator is folded into larger metadata containers in later
    // versions. Its actual prologue, object store and epilogue were audited.
    let generator = match version {
        "GC_1_3" => 0x433590,
        "GC_1_3_2" => 0x433310,
        "GC_2_7" => 0x4333f0,
        _ => unreachable!(),
    };
    ensure!(
        pe.read(generator, 10)? == [0x53, 0x56, 0x57, 0x55, 0x81, 0xec, 0xb8, 0, 0, 0],
        "generator prologue guard"
    );
    let container = metadata
        .iter()
        .filter(|s| {
            executable::option(&s.options, "type") == Some("function") && s.address <= generator
        })
        .filter_map(|s| executable::size(&s.options).ok().map(|size| (s, size)))
        .find(|(s, size)| generator + 0xdbf <= s.address + size)
        .context("generator is outside named retail containers")?
        .0;
    let begin = generator + 0x9d;
    let end = generator + 0xdb4;
    ensure!(pe.read(begin, 1)? == [0xa3], "generator object-store guard");
    ensure!(
        pe.read(end, 11)? == [0x81, 0xc4, 0xb8, 0, 0, 0, 0x5d, 0x5f, 0x5e, 0x5b, 0xc3],
        "generator epilogue guard"
    );
    for (address, length, role, field) in [
        (begin, 5, Role::FunctionBegin, Some(1)),
        (end, 6, Role::FunctionEnd, None),
    ] {
        sites.push(Site {
            address,
            container: container.name.clone(),
            offset: address - container.address,
            role,
            selector: 0,
            bytes: pe.read(address, length)?.to_vec(),
            field,
            counter_class: None,
        });
    }
    sites.sort_by_key(|s| s.address);
    ensure!(
        sites
            .windows(2)
            .all(|p| p[0].address + p[0].bytes.len() as u32 <= p[1].address),
        "overlapping counter sites"
    );
    Ok(sites)
}

pub fn install(root: &Path, version: &str, inputs: &[(String, Vec<u8>)]) -> Result<Installed> {
    let sites = plan(root, version)?;
    let original = Original::read(root, version)?;
    let pe = original.pe()?;
    let fixups = executable::base_relocations(&pe)?;
    let metadata = executable::read_symbols(&fs::read_to_string(
        root.join(format!("config/{version}/symbols.txt")),
    )?)?;
    let mut islands = Builder::default();
    let code_section = islands.section(".text", 0x60500020, vec![], 0, vec![])?;
    let data_section = islands.section(".rdata", 0x40300040, vec![], 0, vec![])?;
    let counts = islands.external(COUNTS)?;
    let physical = islands.external(PHYSICAL)?;
    let handler = islands.external(HANDLER)?;
    let mut objects = BTreeMap::new();
    let mut installed = BTreeSet::new();
    for (path, bytes) in inputs {
        let object = Object::parse(bytes)?;
        let mut patches = vec![];
        let mut checked = BTreeSet::new();
        for site in &sites {
            let Some(symbol) = object
                .symbols
                .values()
                .find(|s| s.section > 0 && s.name == site.container)
            else {
                continue;
            };
            ensure!(
                installed.insert(site.address),
                "counter site defined in multiple input objects"
            );
            let section_index = symbol.section as usize - 1;
            let section = &object.sections[section_index];
            if checked.insert(site.container.as_str()) {
                let reference = metadata
                    .iter()
                    .find(|s| s.name == site.container)
                    .context("missing retail container metadata")?;
                let length = executable::size(&reference.options)? as usize;
                let mut expected = pe.read(reference.address, length)?.to_vec();
                let mut actual = section
                    .data
                    .get(symbol.value as usize..symbol.value as usize + length)
                    .context("retail container outside split section")?
                    .to_vec();
                let mut fields = BTreeMap::new();
                for &at in fixups.iter().filter(|&&at| {
                    reference.address <= at && at < reference.address + length as u32
                }) {
                    fields.insert((at - reference.address) as usize, 6);
                }
                for (at, target) in crate::x86::branches(&expected, reference.address)? {
                    if !(reference.address..reference.address + length as u32).contains(&target) {
                        fields.insert(at as usize, 20);
                    }
                }
                let mut seen = BTreeSet::new();
                for r in section
                    .relocations
                    .iter()
                    .filter(|r| symbol.value <= r.offset && r.offset < symbol.value + length as u32)
                {
                    let offset = (r.offset - symbol.value) as usize;
                    ensure!(
                        fields.get(&offset) == Some(&r.kind) && seen.insert(offset),
                        "unexpected retail container relocation"
                    );
                    actual
                        .get_mut(offset..offset + 4)
                        .context("retail container fixup crosses boundary")?
                        .fill(0);
                }
                ensure!(
                    seen.len() == fields.len(),
                    "missing retail container relocations"
                );
                for at in fields.keys() {
                    expected
                        .get_mut(*at..*at + 4)
                        .context("retail reference fixup crosses boundary")?
                        .fill(0);
                }
                ensure!(
                    actual == expected,
                    "retail container byte guard failed for {}",
                    site.container
                );
            }
            let offset = symbol
                .value
                .checked_add(site.offset)
                .context("counter offset overflow")? as usize;
            let actual = section
                .data
                .get(offset..offset + site.bytes.len())
                .context("counter site outside split section")?;
            let mut expected = site.bytes.clone();
            let mut copied = actual.to_vec();
            let original_reloc = if let Some(field) = site.field {
                expected[field..field + 4].fill(0);
                let mut masked = copied.clone();
                masked[field..field + 4].fill(0);
                ensure!(
                    expected == masked,
                    "retail counter instruction byte guard failed at {:#x}",
                    site.address
                );
                let fixup = section
                    .relocations
                    .iter()
                    .find(|r| r.offset as usize == offset + field && r.kind == 6)
                    .context("counter island lacks its absolute relocation")?;
                let target = &object.symbols[&fixup.symbol];
                ensure!(
                    target.storage == 2,
                    "counter instruction references a local-only symbol"
                );
                let target_address = metadata
                    .iter()
                    .find(|s| s.name == target.name)
                    .context("counter fixup lacks retail symbol metadata")?
                    .address;
                let addend = u32::from_le_bytes(copied[field..field + 4].try_into()?);
                let expected_address = u32::from_le_bytes(site.bytes[field..field + 4].try_into()?);
                ensure!(
                    target_address.wrapping_add(addend) == expected_address,
                    "counter/context relocation target changed"
                );
                let external = islands.external(&target.name)?;
                Some((field, external))
            } else {
                ensure!(copied == expected, "retail epilogue byte guard failed");
                None
            };
            ensure!(
                section
                    .relocations
                    .iter()
                    .filter(|r| (offset..offset + copied.len()).contains(&(r.offset as usize)))
                    .count()
                    == usize::from(original_reloc.is_some()),
                "unexpected relocation in counter patch"
            );
            let descriptor_offset = islands.sections[data_section as usize - 1].data.len() as u32;
            let descriptor_name = format!("_OracleRust_CounterDescriptor_{:08x}", site.address);
            let descriptor =
                islands.add_symbol(&descriptor_name, descriptor_offset, data_section, 0, 3, &[])?;
            let mut desc = vec![0; 12];
            desc.extend(site.selector.to_le_bytes());
            desc.extend((site.role as u32).to_le_bytes());
            let mut desc_relocations = vec![
                Relocation {
                    offset: descriptor_offset + 4,
                    symbol: counts,
                    kind: 6,
                },
                Relocation {
                    offset: descriptor_offset + 8,
                    symbol: physical,
                    kind: 6,
                },
            ];
            if !matches!(site.role, Role::FunctionBegin | Role::FunctionEnd) {
                let (field, external) = original_reloc.context("counter pointer missing")?;
                desc[..4].copy_from_slice(&copied[field..field + 4]);
                desc_relocations.push(Relocation {
                    offset: descriptor_offset,
                    symbol: external,
                    kind: 6,
                });
            }
            islands.sections[data_section as usize - 1]
                .data
                .extend(desc);
            islands.sections[data_section as usize - 1]
                .relocations
                .extend(desc_relocations);
            let island_offset = islands.sections[code_section as usize - 1].data.len() as u32;
            let island_name = format!("_OracleRust_CounterIsland_{:08x}", site.address);
            islands.add_symbol(&island_name, island_offset, code_section, 0x20, 2, &[])?;
            let mut emitted = vec![];
            let mut relocations = vec![];
            if site.role != Role::FunctionEnd {
                if let Some((field, external)) = original_reloc {
                    relocations.push(Relocation {
                        offset: island_offset + field as u32,
                        symbol: external,
                        kind: 6,
                    });
                }
                emitted.append(&mut copied);
            }
            let (probe, fields) = backend_probe::callback(site.address, descriptor, handler);
            let probe_offset = island_offset + emitted.len() as u32;
            relocations.extend(fields.into_iter().map(|mut r| {
                r.offset += probe_offset;
                r
            }));
            emitted.extend(probe);
            if site.role == Role::FunctionEnd {
                // Discard CALL's return slot, execute the exact retail stack
                // adjustment (including its flags), then resume at the POPs.
                emitted.extend([0x83, 0xc4, 4]);
                emitted.extend(actual);
                emitted.push(0xe9);
                let target = islands.external(&symbol.name)?;
                relocations.push(Relocation {
                    offset: island_offset + emitted.len() as u32,
                    symbol: target,
                    kind: 20,
                });
                emitted.extend((site.offset + site.bytes.len() as u32).to_le_bytes());
            } else {
                emitted.push(0xc3);
            }
            islands.sections[code_section as usize - 1]
                .data
                .extend(emitted);
            islands.sections[code_section as usize - 1]
                .relocations
                .extend(relocations);
            patches.push((section_index, offset, site.bytes.len(), island_name));
        }
        if patches.is_empty() {
            continue;
        }
        let mut rebuilt = Builder::default();
        for section in &object.sections {
            rebuilt.section(
                &section.name,
                section.flags,
                section.data.to_vec(),
                section.size,
                vec![],
            )?;
        }
        let mut symbols = BTreeMap::new();
        for (&index, s) in &object.symbols {
            let section_symbol = s.storage == 3
                && s.value == 0
                && s.section > 0
                && s.auxiliary.len() == 1
                && s.name == object.sections[s.section as usize - 1].name;
            let new = if section_symbol {
                rebuilt.section_symbols[&s.section]
            } else {
                ensure!(
                    s.auxiliary.is_empty(),
                    "unsupported counter input auxiliary symbol"
                );
                rebuilt.add_symbol(&s.name, s.value, s.section, s.kind, s.storage, &[])?
            };
            symbols.insert(index, new);
        }
        for (index, section) in object.sections.iter().enumerate() {
            rebuilt.sections[index].relocations = section
                .relocations
                .iter()
                .map(|r| Relocation {
                    offset: r.offset,
                    symbol: symbols[&r.symbol],
                    kind: r.kind,
                })
                .collect();
        }
        for (section, offset, length, name) in patches {
            let external = rebuilt.external(&name)?;
            let s = &mut rebuilt.sections[section];
            s.data[offset..offset + length].fill(0x90);
            s.data[offset] = 0xe8;
            s.data[offset + 1..offset + 5].fill(0);
            s.relocations
                .retain(|r| !(offset..offset + length).contains(&(r.offset as usize)));
            s.relocations.push(Relocation {
                offset: offset as u32 + 1,
                symbol: external,
                kind: 20,
            });
        }
        rebuilt.refresh_section_definitions()?;
        objects.insert(path.clone(), rebuilt.finish()?);
    }
    ensure!(
        installed.len() == sites.len(),
        "not all audited counter/context sites occur in retail link inputs ({} of {})",
        installed.len(),
        sites.len()
    );
    for section in &mut islands.sections {
        section.size = section.data.len().try_into()?;
    }
    islands.refresh_section_definitions()?;
    Ok(Installed {
        objects,
        islands: islands.finish()?,
        sites,
    })
}
