//! Locate actual retail references to virtual-register counters before installing
//! lifecycle probes. Direct references are evidence; unresolved aliases are gaps.
use crate::{
    disassembly,
    executable::{self, Original},
    pe,
};
use anyhow::{Context, Result, ensure};
use capstone::{
    RegId,
    arch::{DetailsArchInsn, x86::X86OperandType},
};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{
    collections::{BTreeMap, BTreeSet},
    fs,
    path::Path,
};

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Reference {
    pub address: u32,
    pub function: String,
    pub function_address: u32,
    pub offset: u32,
    pub unit: Option<String>,
    pub instruction: String,
    pub bytes: String,
    pub relocations: Vec<u32>,
    pub targets: Vec<u32>,
    pub read: bool,
    pub write: bool,
    pub address_taken: bool,
    pub indexed: bool,
    pub access_known: bool,
    pub width: Option<u8>,
    pub function_decoded_completely: bool,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct FunctionGuard {
    pub address: u32,
    pub size: u32,
    pub code_sha256: String,
    pub normalized_sha256: String,
    pub normalized_fields: Vec<u32>,
    pub decoded_bytes: usize,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Unresolved {
    pub address: u32,
    pub target: u32,
    pub reason: String,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Audit {
    pub format_version: u32,
    pub version: String,
    pub compiler_sha256: String,
    pub symbol_metadata_sha256: String,
    pub counters: BTreeMap<String, u32>,
    pub table_base: u32,
    pub references: Vec<Reference>,
    pub functions: BTreeMap<String, FunctionGuard>,
    pub unresolved: Vec<Unresolved>,
    pub lifecycle_coverage_complete: bool,
    pub limitations: Vec<String>,
}

pub fn capture(root: &Path, version: &str) -> Result<Audit> {
    ensure!(
        ["GC_1_3", "GC_1_3_2", "GC_2_7"].contains(&version),
        "unsupported backend audit version {version}"
    );
    let original = Original::read(root, version)?;
    let pe = original.pe()?;
    let metadata = fs::read(root.join(format!("config/{version}/symbols.txt")))?;
    let symbols = executable::read_symbols(std::str::from_utf8(&metadata)?)?;
    let splits = executable::read_splits(&fs::read_to_string(
        root.join(format!("config/{version}/splits.txt")),
    )?)?;
    let mut counters = BTreeMap::new();
    for class in ["VR", "FPR", "GPR"] {
        let name = format!("_gUsedVirtualRegisters{class}");
        counters.insert(
            class.to_owned(),
            symbols
                .iter()
                .find(|s| s.name == name)
                .with_context(|| format!("missing {name}"))?
                .address,
        );
    }
    let table_base = counters["VR"]
        .checked_sub(8)
        .context("counter table underflow")?;
    ensure!(
        counters["FPR"] == table_base + 12 && counters["GPR"] == table_base + 16,
        "unexpected counter layout"
    );
    let fixups = executable::base_relocations(&pe)?;
    let mut relevant = BTreeMap::new();
    let mut unresolved = vec![];
    for &address in &fixups {
        let target = pe::u32le(pe.read(address, 4)?, 0)?;
        if (table_base..table_base + 20).contains(&target) {
            if pe.section_for_address(address)?.characteristics & 0x20 != 0 {
                relevant.insert(address, target);
            } else {
                unresolved.push(Unresolved {
                    address,
                    target,
                    reason:
                        "counter address stored outside executable code; alias analysis required"
                            .into(),
                });
            }
        }
    }
    let mut decoder = disassembly::decoder()?;
    decoder.set_detail(true)?;
    let mut references = vec![];
    let mut functions = BTreeMap::new();
    let mut accounted = BTreeSet::new();
    for symbol in symbols
        .iter()
        .filter(|s| executable::option(&s.options, "type") == Some("function"))
    {
        let size = executable::size(&symbol.options)?;
        let end = symbol
            .address
            .checked_add(size)
            .context("function end overflow")?;
        let fields: Vec<_> = relevant
            .range(symbol.address..end)
            .map(|(&a, &v)| (a, v))
            .collect();
        if fields.is_empty() {
            continue;
        }
        let code = pe.read(symbol.address, size as usize)?;
        let instructions = decoder.disasm_all(code, u64::from(symbol.address))?;
        let decoded_bytes = instructions.iter().map(|i| i.bytes().len()).sum();
        let mut normalized = code.to_vec();
        let mut normalized_fields = vec![];
        for &at in fixups.iter().filter(|&&a| symbol.address <= a && a < end) {
            let offset = (at - symbol.address) as usize;
            normalized
                .get_mut(offset..offset + 4)
                .context("absolute relocation crosses function end")?
                .fill(0);
            normalized_fields.push(offset as u32);
        }
        for i in instructions.iter() {
            if i.bytes().first() == Some(&0xe8) && i.bytes().len() == 5 {
                let offset = (i.address() - u64::from(symbol.address)) as usize + 1;
                normalized[offset..offset + 4].fill(0);
                normalized_fields.push(offset as u32);
            }
            let address = i.address() as u32;
            let inside: Vec<_> = fields
                .iter()
                .copied()
                .filter(|(a, _)| address <= *a && *a < address + i.bytes().len() as u32)
                .collect();
            if inside.is_empty() {
                continue;
            }
            accounted.extend(inside.iter().map(|(a, _)| *a));
            let detail = decoder.insn_detail(i)?;
            let arch = detail.arch_detail();
            let x86 = arch.x86().context("expected x86 details")?;
            let (mut read, mut write, mut address_taken, mut indexed, mut access_known) =
                (false, false, false, false, false);
            let mut width = None;
            for operand in x86.operands() {
                match operand.op_type {
                    X86OperandType::Mem(mem)
                        if inside.iter().any(|(_, t)| mem.disp() as u32 == *t) =>
                    {
                        indexed = mem.base() != RegId(0) || mem.index() != RegId(0);
                        address_taken = i.mnemonic() == Some("lea");
                        width = Some(operand.size);
                        if let Some(access) = operand.access {
                            access_known = true;
                            read = access.is_readable() && !address_taken;
                            write = access.is_writable() && !address_taken;
                        }
                    }
                    X86OperandType::Imm(value)
                        if inside.iter().any(|(_, t)| value as u32 == *t) =>
                    {
                        address_taken = true;
                        access_known = true;
                    }
                    _ => {}
                }
            }
            references.push(Reference {
                address,
                function: symbol.name.clone(),
                function_address: symbol.address,
                offset: address - symbol.address,
                unit: splits
                    .units
                    .iter()
                    .find(|u| {
                        u.ranges
                            .iter()
                            .any(|r| r.section == ".text" && r.start <= address && address < r.end)
                    })
                    .map(|u| u.name.clone()),
                instruction: format!(
                    "{} {}",
                    i.mnemonic().unwrap_or("?"),
                    i.op_str().unwrap_or("")
                ),
                bytes: i.bytes().iter().map(|b| format!("{b:02x}")).collect(),
                relocations: inside.iter().map(|(a, _)| *a).collect(),
                targets: inside.iter().map(|(_, t)| *t).collect(),
                read,
                write,
                address_taken,
                indexed,
                access_known,
                width,
                function_decoded_completely: decoded_bytes == code.len(),
            });
        }
        normalized_fields.sort_unstable();
        normalized_fields.dedup();
        functions.insert(
            symbol.name.clone(),
            FunctionGuard {
                address: symbol.address,
                size,
                code_sha256: format!("{:x}", Sha256::digest(code)),
                normalized_sha256: format!("{:x}", Sha256::digest(normalized)),
                normalized_fields,
                decoded_bytes,
            },
        );
    }
    for (address, target) in relevant.into_iter().filter(|(a, _)| !accounted.contains(a)) {
        unresolved.push(Unresolved {
            address,
            target,
            reason: "reference outside a decoded instruction in a named function".into(),
        });
    }
    references.sort_by_key(|r| r.address);
    Ok(Audit{format_version:1,version:version.into(),compiler_sha256:format!("{:x}",Sha256::digest(&original.data)),
        symbol_metadata_sha256:format!("{:x}",Sha256::digest(metadata)),counters,table_base,references,functions,unresolved,
        lifecycle_coverage_complete:false,limitations:vec![
            "Direct PE HIGHLOW references only: register-derived aliases, bulk writes and dataflow require further audit".into(),
            "A counter write is not yet a classified creation, reset, rewind or restored high-water mark".into(),
            "Hardware register class is not the requested semantic allocation category".into(),
        ]})
}
