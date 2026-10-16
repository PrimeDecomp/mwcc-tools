//! Replace only guarded initializer target relocations with machine-state
//! preserving entry/return islands. Retail call arguments and bodies stay intact.
use crate::{
    backend_probe,
    coff::{Builder, Object, Relocation},
    dispatch_audit::{self, Audit},
    executable,
};
use anyhow::{Context, Result, ensure};
use sha2::{Digest, Sha256};
use std::{collections::BTreeMap, fs, path::Path};

pub const HANDLER: &str = "_OracleRust_ExpressionProbeV1";
pub struct Installed {
    pub object: Vec<u8>,
    pub audit: Audit,
}
pub fn instrument(root: &Path, version: &str, data: &[u8]) -> Result<Installed> {
    let audit = dispatch_audit::capture(root, version)?;
    ensure!(
        audit.version == version && audit.initialized_entries == 78,
        "wrong dispatch audit"
    );
    let metadata = executable::read_symbols(&fs::read_to_string(
        root.join(format!("config/{version}/symbols.txt")),
    )?)?;
    let object = Object::parse(data)?;
    let candidates = object
        .symbols
        .values()
        .filter(|s| s.name == dispatch_audit::INITIALIZER && s.section > 0 && s.value == 0)
        .filter(|s| {
            object.sections[s.section as usize - 1]
                .data
                .starts_with(&[0x30, 0xc9, 0x80, 0xf9, 0x4d])
        })
        .collect::<Vec<_>>();
    ensure!(
        candidates.len() == 1,
        "initializer byte guard must identify one COFF function"
    );
    let function = candidates[0];
    ensure!(
        function.section > 0 && function.value == 0,
        "unexpected expression initializer placement"
    );
    let section_index = function.section as usize - 1;
    let section = &object.sections[section_index];
    ensure!(
        section.data.len() == audit.initialization.size as usize,
        "expression initializer size changed"
    );
    let relocations = section
        .relocations
        .iter()
        .filter(|r| r.offset < audit.initialization.size)
        .map(|r| (r.offset, r))
        .collect::<BTreeMap<_, _>>();
    ensure!(
        relocations.len() == audit.initialization.writes.len() * 2,
        "expression initializer relocation count changed"
    );
    let mut normalized = section.data[..audit.initialization.size as usize].to_vec();
    let mut targets = BTreeMap::<u32, (u32, u32)>::new();
    let mut fields = BTreeMap::new();
    let original = executable::Original::read(root, version)?;
    let pe = original.pe()?;
    let fixups = executable::base_relocations(&pe)?;
    for write in &audit.initialization.writes {
        let (destination, target) = if write.slot.is_none() {
            (write.offset + 3, write.offset + 7)
        } else {
            (write.offset + 2, write.offset + 6)
        };
        for (at, expected) in [
            (destination, audit.table_base + write.slot.unwrap_or(0) * 4),
            (target, write.target),
        ] {
            let relocation = relocations
                .get(&at)
                .context("expression initializer field has no relocation")?;
            ensure!(
                relocation.kind == 6,
                "expression initializer field is not an absolute pointer"
            );
            let symbol = &object.symbols[&relocation.symbol];
            let addend = u32::from_le_bytes(section.data[at as usize..at as usize + 4].try_into()?);
            ensure!(
                metadata
                    .iter()
                    .filter(|s| s.name == symbol.name && s.address.wrapping_add(addend) == expected)
                    .count()
                    == 1,
                "expression initializer relocation target changed: {}+{addend:x}",
                symbol.name
            );
            if at == target && metadata.iter().filter(|s| s.name == symbol.name).count() > 1 {
                // GC 2.7 has two local functions with the same metadata name.
                // Name/offset equality cannot distinguish their COFF symbols.
                let source = metadata
                    .iter()
                    .find(|s| s.name == symbol.name && s.address.wrapping_add(addend) == expected)
                    .unwrap();
                ensure!(symbol.section > 0, "ambiguous external dispatch target");
                let length = executable::size(&source.options)? as usize;
                let body = &object.sections[symbol.section as usize - 1];
                let start = symbol.value as usize;
                let mut actual = body
                    .data
                    .get(start..start + length)
                    .context("ambiguous dispatch target has wrong body extent")?
                    .to_vec();
                let mut expected_body = pe.read(source.address, length)?.to_vec();
                let mut expected_fields = BTreeMap::new();
                for address in fixups
                    .iter()
                    .filter(|&&a| source.address <= a && a < source.address + length as u32)
                {
                    expected_fields.insert(*address - source.address, 6);
                }
                for (field, target) in crate::x86::branches(&expected_body, source.address)? {
                    if !(source.address..source.address + length as u32).contains(&target) {
                        expected_fields.insert(field, 20);
                    }
                }
                let mut observed = 0;
                for r in body
                    .relocations
                    .iter()
                    .filter(|r| symbol.value <= r.offset && r.offset < symbol.value + length as u32)
                {
                    let field = r.offset - symbol.value;
                    ensure!(
                        expected_fields.get(&field) == Some(&r.kind),
                        "ambiguous dispatch target relocation changed"
                    );
                    actual[field as usize..field as usize + 4].fill(0);
                    expected_body[field as usize..field as usize + 4].fill(0);
                    observed += 1;
                }
                ensure!(
                    observed == expected_fields.len() && actual == expected_body,
                    "ambiguous dispatch target body changed"
                );
            }
            normalized[at as usize..at as usize + 4].fill(0);
            if at == target {
                ensure!(
                    targets
                        .get(&write.target)
                        .is_none_or(|old| *old == (relocation.symbol, addend)),
                    "inconsistent dispatch target aliases"
                );
                targets.insert(write.target, (relocation.symbol, addend));
                fields.insert(at, write.target);
            }
        }
    }
    ensure!(
        format!("{:x}", Sha256::digest(normalized)) == audit.initialization.normalized_sha256,
        "expression initializer body changed"
    );
    let mut builder = Builder::default();
    for section in &object.sections {
        builder.section(
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
            builder.section_symbols[&s.section]
        } else {
            ensure!(
                s.auxiliary.is_empty(),
                "unsupported expression initializer auxiliary symbol"
            );
            builder.add_symbol(&s.name, s.value, s.section, s.kind, s.storage, &[])?
        };
        symbols.insert(index, new);
    }
    for (index, section) in object.sections.iter().enumerate() {
        builder.sections[index].relocations = section
            .relocations
            .iter()
            .map(|r| Relocation {
                offset: r.offset,
                symbol: symbols[&r.symbol],
                kind: r.kind,
            })
            .collect();
    }
    let code = builder.section(".text$pvx", 0x60500020, vec![], 0, vec![])?;
    let descriptors = builder.section(".rdata$pvx", 0x40300040, vec![], 0, vec![])?;
    let handler = builder.external(HANDLER)?;
    let mut thunks = BTreeMap::new();
    for (ordinal, (retail, (target, addend))) in targets.into_iter().enumerate() {
        let start = builder.sections[code as usize - 1].data.len() as u32;
        let entry = builder.add_symbol(
            &format!("_OracleExpressionEntry{ordinal}"),
            start,
            code,
            0x20,
            3,
            &[],
        )?;
        let desc_at = builder.sections[descriptors as usize - 1].data.len() as u32;
        let descriptor = builder.add_symbol(
            &format!("_OracleExpressionDescriptor{ordinal}"),
            desc_at,
            descriptors,
            0,
            3,
            &[],
        )?;
        let (mut bytes, mut relocations) = backend_probe::callback(1, descriptor, handler);
        bytes.push(0xe9);
        relocations.push(Relocation {
            offset: bytes.len() as u32,
            symbol: symbols[&target],
            kind: 20,
        });
        bytes.extend(addend.to_le_bytes());
        let exit = builder.add_symbol(
            &format!("_OracleExpressionReturn{ordinal}"),
            start + bytes.len() as u32,
            code,
            0x20,
            3,
            &[],
        )?;
        // After native RET, reserve its old return-address slot. The exit
        // observer restores the real caller address without clobbering native
        // return registers, flags, x87, MXCSR or XMM state.
        bytes.extend([0x68, 0, 0, 0, 0]);
        let base = bytes.len() as u32;
        let (exit_bytes, exit_relocations) = backend_probe::callback(2, descriptor, handler);
        bytes.extend(exit_bytes);
        relocations.extend(exit_relocations.into_iter().map(|mut r| {
            r.offset += base;
            r
        }));
        bytes.push(0xc3);
        let output = &mut builder.sections[code as usize - 1];
        output.data.extend(bytes);
        output
            .relocations
            .extend(relocations.into_iter().map(|mut r| {
                r.offset += start;
                r
            }));
        let output = &mut builder.sections[descriptors as usize - 1];
        output.data.extend(retail.to_le_bytes());
        output.data.extend(0u32.to_le_bytes());
        output.relocations.push(Relocation {
            offset: desc_at + 4,
            symbol: exit,
            kind: 6,
        });
        thunks.insert(retail, entry);
    }
    for (field, target) in fields {
        let output = &mut builder.sections[section_index];
        output.data[field as usize..field as usize + 4].fill(0);
        output
            .relocations
            .iter_mut()
            .find(|r| r.offset == field)
            .unwrap()
            .symbol = thunks[&target];
    }
    for section in &mut builder.sections[object.sections.len()..] {
        section.size = section.data.len().try_into()?;
    }
    builder.refresh_section_definitions()?;
    Ok(Installed {
        object: builder.finish()?,
        audit,
    })
}
