//! Guarded observations inserted into the retail coalescer, without replacing it.
use crate::{
    coff::{Builder, Object, Relocation},
    disassembly,
};
use anyhow::{Context, Result, ensure};
use sha2::{Digest, Sha256};
use std::collections::BTreeMap;

pub const FUNCTION: &str = "_SpillCode_CoalesceCopies";
pub const SITES: &[usize] = &[
    7, 0x107, 0x10f, 0x17b, 0x18b, 0x1d6, 0x244, 0x271, 0x280, 0x289, 0x291, 0x2b9, 0x2cb, 0x442,
];
const ABSOLUTE: &[usize] = &[
    10, 17, 35, 67, 77, 91, 105, 119, 133, 147, 161, 194, 210, 299, 356, 389, 422, 454, 512, 554,
    589, 596, 618, 634, 683, 693, 707, 777, 813, 867, 896, 973, 1017, 1034, 1053,
];
const CALLS: &[usize] = &[0x1d, 0x130, 0x169, 0x1dd, 0x3a2];
// Pointer fields copied with their existing COFF relocations, never retail VAs.
const GLOBALS: &[usize] = &[0xa, 0x11, 0x254, 0x26a, 0x27a, 0x2b5];
const HASH: &str = "cb3c226270f4100bb1ba61e5e2eefab55625383f5dcd401e7f561192e9cc6a53";

/// A foreign object must call the entry, never an old interior offset whose
/// meaning changes when probes or wider branches are inserted.
pub fn validate_incoming(data: &[u8]) -> Result<()> {
    let object = Object::parse(data)?;
    for section in &object.sections {
        for r in &section.relocations {
            if object.symbols[&r.symbol].name != FUNCTION {
                continue;
            }
            let field = section
                .data
                .get(r.offset as usize..r.offset as usize + 4)
                .context("incoming coalescer relocation outside section")?;
            ensure!(
                matches!(r.kind, 6 | 20) && field == [0; 4],
                "unsupported incoming relocation into retail coalescer interior"
            );
        }
    }
    Ok(())
}

/// Only accepts the audited split retail function. A mismatch aborts linking.
pub fn instrument(data: &[u8]) -> Result<Vec<u8>> {
    let object = Object::parse(data)?;
    let function = object
        .symbols
        .values()
        .find(|s| s.name == FUNCTION)
        .context("retail object lacks coalescer")?;
    ensure!(
        function.section > 0 && function.value == 0,
        "unexpected coalescer placement"
    );
    let section_index = function.section as usize - 1;
    let section = &object.sections[section_index];
    ensure!(
        section.data.len() == 0x44a,
        "unexpected retail coalescer size"
    );
    let mut normalized = section.data.to_vec();
    let relocations: BTreeMap<_, _> = section
        .relocations
        .iter()
        .map(|r| (r.offset as usize, r))
        .collect();
    ensure!(
        relocations.len() == ABSOLUTE.len() + CALLS.len(),
        "unexpected coalescer relocations"
    );
    for (&at, r) in &relocations {
        ensure!(
            ABSOLUTE.contains(&at) && r.kind == 6 || CALLS.contains(&at) && r.kind == 20,
            "unexpected coalescer relocation at {at:#x}"
        );
        normalized
            .get_mut(at..at + 4)
            .context("coalescer relocation outside code")?
            .fill(0);
    }
    ensure!(
        format!("{:x}", Sha256::digest(&normalized)) == HASH,
        "retail coalescer byte guard failed"
    );
    let decoder = disassembly::decoder()?;
    let instructions = decoder.disasm_all(&section.data, 0)?;
    ensure!(
        instructions.iter().map(|i| i.bytes().len()).sum::<usize>() == section.data.len(),
        "incomplete coalescer decoding"
    );
    let mut positions = BTreeMap::new();
    let mut size = 0usize;
    // Fixed-size probes; short jumps are widened before computing all targets.
    for i in instructions.iter() {
        let at = i.address() as usize;
        positions.insert(at, size);
        if SITES.contains(&at) {
            size += probe(0, 0, 0).0.len();
        }
        size += match i.bytes()[0] {
            0x70..=0x7f => 6,
            0xeb => 5,
            _ => i.bytes().len(),
        };
    }
    ensure!(
        SITES.iter().all(|at| positions.contains_key(at)),
        "probe is not an instruction boundary"
    );
    let mut builder = Builder::default();
    // Keep section numbering stable for every original symbol and relocation.
    for s in &object.sections {
        builder.section(&s.name, s.flags, s.data.to_vec(), s.size, vec![])?;
    }
    let descriptor_section = builder.section(
        ".rdata",
        0x40300040,
        vec![0; GLOBALS.len() * 4],
        (GLOBALS.len() * 4) as u32,
        vec![],
    )?;
    let descriptor = builder.add_symbol(
        "_OracleRust_CoalescingGlobals",
        0,
        descriptor_section,
        0,
        3,
        &[],
    )?;
    let handler = builder.external("_OracleRust_BackendProbe")?;
    let mut symbols = BTreeMap::new();
    for (&index, s) in &object.symbols {
        let is_section = s.storage == 3
            && s.value == 0
            && s.section > 0
            && s.auxiliary.len() == 1
            && s.name == object.sections[s.section as usize - 1].name;
        let new = if is_section {
            builder.section_symbols[&s.section]
        } else {
            ensure!(
                s.auxiliary.is_empty(),
                "unsupported auxiliary symbol in retail probe object"
            );
            let value = if s.section == function.section {
                *positions
                    .get(&(s.value as usize))
                    .context("symbol outside rewritten function")? as u32
            } else {
                s.value
            };
            builder.add_symbol(&s.name, value, s.section, s.kind, s.storage, &[])?
        };
        symbols.insert(index, new);
    }
    for (n, s) in object.sections.iter().enumerate() {
        if n == section_index {
            continue;
        }
        builder.sections[n].relocations = s
            .relocations
            .iter()
            .map(|r| Relocation {
                offset: r.offset,
                symbol: symbols[&r.symbol],
                kind: r.kind,
            })
            .collect();
        for r in &s.relocations {
            let target = &object.symbols[&r.symbol];
            if target.section == function.section {
                let field = s
                    .data
                    .get(r.offset as usize..r.offset as usize + 4)
                    .context("bad incoming probe relocation")?;
                ensure!(
                    field == [0; 4] && target.value == 0,
                    "incoming relocation into rewritten function interior"
                );
            }
        }
    }
    let mut code = Vec::with_capacity(size);
    let mut new_relocations = vec![];
    for i in instructions.iter() {
        let at = i.address() as usize;
        ensure!(code.len() == positions[&at], "probe position mismatch");
        if SITES.contains(&at) {
            let (bytes, fields) = probe(at as u32, descriptor, handler);
            let start = code.len() as u32;
            new_relocations.extend(fields.into_iter().map(|mut r| {
                r.offset += start;
                r
            }));
            code.extend(bytes);
        }
        let start = code.len();
        let bytes = i.bytes();
        let branch = match bytes {
            [op @ 0x70..=0x7f, displacement] => {
                Some((vec![0x0f, op + 0x10], i64::from(*displacement as i8)))
            }
            [0xeb, displacement] => Some((vec![0xe9], i64::from(*displacement as i8))),
            [0xe9, a, b, c, d] => {
                Some((vec![0xe9], i64::from(i32::from_le_bytes([*a, *b, *c, *d]))))
            }
            [0x0f, op @ 0x80..=0x8f, a, b, c, d] => Some((
                vec![0x0f, *op],
                i64::from(i32::from_le_bytes([*a, *b, *c, *d])),
            )),
            _ => None,
        };
        ensure!(!matches!(bytes[0], 0xe0..=0xe3), "unsupported loop branch");
        if let Some((opcode, displacement)) = branch {
            ensure!(
                !relocations.range(at..at + bytes.len()).any(|_| true),
                "relocated local branch"
            );
            let target = (at as i64 + bytes.len() as i64 + displacement) as usize;
            let mapped = *positions
                .get(&target)
                .context("branch target outside instruction boundaries")?;
            let end = start + opcode.len() + 4;
            code.extend(opcode);
            code.extend(i32::try_from(mapped as i64 - end as i64)?.to_le_bytes());
        } else {
            code.extend(bytes);
            for (&field, r) in relocations.range(at..at + bytes.len()) {
                ensure!(
                    field + 4 <= at + bytes.len(),
                    "relocation crosses instruction"
                );
                new_relocations.push(Relocation {
                    offset: (start + field - at) as u32,
                    symbol: symbols[&r.symbol],
                    kind: r.kind,
                });
            }
        }
    }
    ensure!(code.len() == size, "rewritten size mismatch");
    builder.sections[section_index].data = code;
    builder.sections[section_index].size = size as u32;
    builder.sections[section_index].relocations = new_relocations;
    for (index, &field) in GLOBALS.iter().enumerate() {
        let r = relocations[&field];
        let s = &mut builder.sections[descriptor_section as usize - 1];
        s.data[index * 4..index * 4 + 4].copy_from_slice(&section.data[field..field + 4]);
        s.relocations.push(Relocation {
            offset: (index * 4) as u32,
            symbol: symbols[&r.symbol],
            kind: r.kind,
        });
    }
    builder.refresh_section_definitions()?;
    builder.finish()
}

fn probe(site: u32, descriptor: u32, handler: u32) -> (Vec<u8>, Vec<Relocation>) {
    // Save all GPRs, flags, x87, MXCSR and XMM registers. FXSAVE is supported by
    // the SSE2 i686 producer target. The callback gets the pre-probe register view.
    let mut code = vec![
        0x9c, 0x60, 0x89, 0xe0, 0x81, 0xec, 0x10, 0x02, 0, 0, 0x83, 0xe4, 0xf0, 0x0f, 0xae, 0x04,
        0x24, 0x50, 0x68,
    ];
    let descriptor_at = code.len() as u32;
    code.extend(0u32.to_le_bytes());
    code.push(0x68);
    code.extend(site.to_le_bytes());
    code.extend([0x50, 0xfc, 0xe8]);
    let call_at = code.len() as u32;
    code.extend(0u32.to_le_bytes());
    code.extend([
        0x83, 0xc4, 0x0c, 0x58, 0x0f, 0xae, 0x0c, 0x24, 0x89, 0xc4, 0x61, 0x9d,
    ]);
    (
        code,
        vec![
            Relocation {
                offset: descriptor_at,
                symbol: descriptor,
                kind: 6,
            },
            Relocation {
                offset: call_at,
                symbol: handler,
                kind: 20,
            },
        ],
    )
}
