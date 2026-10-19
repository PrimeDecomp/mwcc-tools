//! Direct references and guarded initialization of the retail expression table.
//! Retail bytes, not reconstructed array declarations or function names, establish
//! its extent. Two GC 2.7 local functions have the same metadata name.
use crate::{
    backend_audit, disassembly,
    executable::{self, Original},
};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{collections::BTreeMap, fs, path::Path};

pub const TABLE: &str = "_data_00560648";
pub const KINDS: u32 = 78;
pub const INITIALIZER: &str = "_fn_00453e10";
const INITIALIZER_HASH: &str = "bde2c0005c1ef9c6b01d9dc22ed4e3288f14f343ac6eac698a3b0ae83eb8d69a";
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Write {
    pub offset: u32,
    pub slot: Option<u32>,
    pub target: u32,
    pub target_name: String,
    pub target_offset: u32,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Layout {
    pub address: u32,
    pub size: u32,
    pub normalized_sha256: String,
    pub writes: Vec<Write>,
    pub targets: Vec<u32>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Instruction {
    pub address: u32,
    pub bytes: String,
    pub instruction: String,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Audit {
    pub format_version: u32,
    pub version: String,
    pub compiler_sha256: String,
    pub symbol_metadata_sha256: String,
    pub declaration_sha256: String,
    pub table_base: u32,
    pub initialized_entries: u32,
    pub initialization: Layout,
    pub direct: backend_audit::ReferenceAudit,
    pub contexts: BTreeMap<u32, Vec<Instruction>>,
    pub coverage_complete: bool,
    pub limitations: Vec<String>,
}
pub fn capture(root: &Path, version: &str) -> Result<Audit> {
    ensure!(
        ["GC_1_3", "GC_1_3_2", "GC_2_6", "GC_2_7"].contains(&version),
        "unsupported expression-dispatch version"
    );
    let original = Original::read(root, version)?;
    let pe = original.pe()?;
    let metadata = fs::read(root.join(format!("config/{version}/symbols.txt")))?;
    let symbols = executable::read_symbols(std::str::from_utf8(&metadata)?)?;
    let splits = executable::read_splits(&fs::read_to_string(
        root.join(format!("config/{version}/splits.txt")),
    )?)?;
    let table = symbols
        .iter()
        .find(|s| s.name == TABLE)
        .context("missing expression dispatch table")?
        .address;
    let declaration = fs::read(root.join("include/compiler/enode.h"))?;
    let direct = backend_audit::references(
        &pe,
        &symbols,
        &splits,
        table
            ..table
                .checked_add(KINDS * 4)
                .context("table range overflow")?,
    )?;
    let candidates = direct
        .functions
        .iter()
        .filter(|f| {
            f.name == INITIALIZER
                && f.guard.size == 449
                && f.guard.normalized_sha256 == INITIALIZER_HASH
        })
        .collect::<Vec<_>>();
    ensure!(
        candidates.len() == 1,
        "initializer byte guard must identify one retail function"
    );
    let guard = &candidates[0].guard;
    ensure!(
        guard.size == 449
            && guard.decoded_bytes == 449
            && guard.normalized_sha256 == INITIALIZER_HASH,
        "expression initializer guard failed"
    );
    let code = pe.read(guard.address, guard.size as usize)?;
    ensure!(
        code[..13]
            == [
                0x30, 0xc9, 0x80, 0xf9, 0x4d, 0x77, 0x15, 0x0f, 0xb6, 0xc1, 0xc7, 0x04, 0x85
            ]
            && code[21..28] == [0xfe, 0xc1, 0x80, 0xf9, 0x4d, 0x76, 0xeb]
            && code[448] == 0xc3,
        "expression initialization loop guard failed"
    );
    let word = |at| u32::from_le_bytes(code[at..at + 4].try_into().unwrap());
    ensure!(word(13) == table, "initializer uses a different table");
    let default = word(17);
    let mut targets = vec![default; KINDS as usize];
    let target_name = |address| {
        symbols
            .iter()
            .find(|s| {
                s.address == address && executable::option(&s.options, "type") == Some("function")
            })
            .or_else(|| {
                symbols
                    .iter()
                    .filter(|s| {
                        executable::option(&s.options, "type") == Some("function")
                            && s.address <= address
                    })
                    .filter_map(|s| {
                        executable::size(&s.options)
                            .ok()
                            .filter(|size| address - s.address < *size)
                            .map(|size| (s, size))
                    })
                    .min_by_key(|(_, size)| *size)
                    .map(|(s, _)| s)
            })
            .map(|s| (s.name.clone(), address - s.address))
            .with_context(|| format!("dispatch target {address:x} lacks a retail container"))
    };
    let (name, offset) = target_name(default)?;
    let mut writes = vec![Write {
        offset: 10,
        slot: None,
        target: default,
        target_name: name,
        target_offset: offset,
    }];
    let mut slots = std::collections::BTreeSet::new();
    for offset in (28..448).step_by(10) {
        ensure!(
            code[offset..offset + 2] == [0xc7, 0x05],
            "unexpected dispatch override"
        );
        let destination = word(offset + 2);
        ensure!(
            destination >= table && (destination - table).is_multiple_of(4),
            "unaligned dispatch override"
        );
        let slot = (destination - table) / 4;
        ensure!(
            slot < KINDS && slots.insert(slot),
            "dispatch override is out of range or repeated"
        );
        let target = word(offset + 6);
        targets[slot as usize] = target;
        let (name, target_offset) = target_name(target)?;
        writes.push(Write {
            offset: offset as u32,
            slot: Some(slot),
            target,
            target_name: name,
            target_offset,
        });
    }
    ensure!(
        direct
            .references
            .iter()
            .filter(|r| r.write)
            .all(|r| r.function == INITIALIZER)
            && direct.references.iter().filter(|r| r.write).count() == writes.len()
            && direct.unresolved.is_empty(),
        "dispatch table has unexplained writers/references"
    );
    let initialization = Layout {
        address: guard.address,
        size: guard.size,
        normalized_sha256: guard.normalized_sha256.clone(),
        writes,
        targets,
    };
    let decoder = disassembly::decoder()?;
    let mut contexts = BTreeMap::new();
    for function in &direct.functions {
        let guard = &function.guard;
        let instructions = decoder.disasm_all(
            pe.read(guard.address, guard.size as usize)?,
            u64::from(guard.address),
        )?;
        for reference in direct
            .references
            .iter()
            .filter(|r| r.function_address == guard.address)
        {
            let index = instructions
                .iter()
                .position(|i| i.address() == u64::from(reference.address))
                .context("reference not at instruction boundary")?;
            let context = instructions
                .iter()
                .skip(index.saturating_sub(8))
                .take(index.min(8) + 4)
                .map(|i| Instruction {
                    address: i.address() as u32,
                    bytes: i.bytes().iter().map(|b| format!("{b:02x}")).collect(),
                    instruction: format!(
                        "{} {}",
                        i.mnemonic().unwrap_or("?"),
                        i.op_str().unwrap_or("")
                    ),
                })
                .collect();
            contexts.insert(reference.address, context);
        }
    }
    Ok(Audit {
        format_version:1, version:version.into(), compiler_sha256:format!("{:x}",Sha256::digest(&original.data)),
        symbol_metadata_sha256:format!("{:x}",Sha256::digest(metadata)), declaration_sha256:format!("{:x}",Sha256::digest(declaration)),
        table_base:table, initialized_entries:KINDS, initialization, direct, contexts, coverage_complete:false,
        limitations:vec!["Retail initialization fills entries 0 through 77 inclusive; reconstructed array sizes are not authoritative".into(),
            "Direct relocated references do not resolve register-derived aliases or prove a call ABI".into(),
            "This audit installs no observers and does not establish exact expression provenance".into()],
    })
}
