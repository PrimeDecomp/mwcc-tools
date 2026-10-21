//! Inventory retail heap references needed for lifetime-safe source provenance.
//!
//! This is a static reference audit, not an allocation history. In particular,
//! address-taking instructions and writes through aliases still need inspection.
use crate::{
    backend_audit::{self, ReferenceAudit},
    executable::{self, Original},
};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use std::{fs, path::Path};

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct StorageSymbol {
    pub name: String,
    pub address: u32,
    pub size: u32,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Arena {
    pub name: String,
    pub base: u32,
    pub size: u32,
    pub storage_symbols: Vec<StorageSymbol>,
    #[serde(flatten)]
    pub references: ReferenceAudit,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Audit {
    pub format_version: u32,
    pub version: String,
    pub compiler_sha256: String,
    pub symbol_metadata_sha256: String,
    pub split_metadata_sha256: String,
    pub local_lock_address: u32,
    pub local_lock: ReferenceAudit,
    pub failure_callback_address: u32,
    pub failure_callback: ReferenceAudit,
    pub arenas: Vec<Arena>,
    pub allocation_coverage_complete: bool,
    pub reset_coverage_complete: bool,
    pub limitations: Vec<String>,
}

const ARENA_SIZE: u32 = 20;
const POOLS: [(&str, &str); 5] = [
    ("heap", "_heap_pool"),
    ("optimizer", "_data_0057fdac"),
    ("block", "_block_pool"),
    ("local", "_data_0057fd84"),
    ("global", "_galloc_pool"),
];

/// Audit all five pool descriptors, the local-heap lock, and the OOM callback.
/// Pool descriptors are split over metadata symbols; the first symbol's size
/// alone does not describe the descriptor. Original::read checks the configured
/// retail image checksum before any references are decoded.
pub fn capture(root: &Path, version: &str) -> Result<Audit> {
    let lock_address = match version {
        "GC_1_3" => 0x5db510,
        "GC_1_3_2" => 0x5dd510,
        "GC_2_6" | "GC_2_7" => 0x5df568,
        _ => anyhow::bail!("unsupported source arena version {version}"),
    };
    let original = Original::read(root, version)?;
    let pe = original.pe()?;
    let metadata = fs::read(root.join(format!("config/{version}/symbols.txt")))?;
    let split_metadata = fs::read(root.join(format!("config/{version}/splits.txt")))?;
    let symbols = executable::read_symbols(std::str::from_utf8(&metadata)?)?;
    let splits = executable::read_splits(std::str::from_utf8(&split_metadata)?)?;
    let lock = symbols
        .iter()
        .find(|s| s.name == "_data_0057fdd8")
        .context("missing local heap lock metadata")?;
    ensure!(
        lock.address == lock_address
            && lock.section == ".bss"
            && executable::size(&lock.options)? == 2,
        "local heap lock metadata layout changed"
    );
    let callback = symbols
        .iter()
        .find(|s| s.name == "_data_0057fdd4")
        .context("missing heap failure callback metadata")?;
    ensure!(
        callback.address == lock_address + 2
            && callback.section == ".bss"
            && executable::size(&callback.options)? == 4,
        "heap failure callback metadata layout changed"
    );
    let mut arenas = vec![];
    for (index, (name, symbol_name)) in POOLS.iter().enumerate() {
        let base = lock_address + 6 + index as u32 * ARENA_SIZE;
        let symbol = symbols
            .iter()
            .find(|s| s.name == *symbol_name)
            .with_context(|| format!("missing {name} pool metadata"))?;
        ensure!(
            symbol.address == base
                && symbol.section == ".bss"
                && executable::size(&symbol.options)? == 4,
            "{name} pool metadata layout changed"
        );
        let end = base + ARENA_SIZE;
        let storage_symbols = symbols
            .iter()
            .filter(|s| s.section == symbol.section && (base..end).contains(&s.address))
            .map(|s| {
                Ok(StorageSymbol {
                    name: s.name.clone(),
                    address: s.address,
                    size: executable::size(&s.options)?,
                })
            })
            .collect::<Result<Vec<_>>>()?;
        arenas.push(Arena {
            name: (*name).into(),
            base,
            size: ARENA_SIZE,
            storage_symbols,
            references: backend_audit::references(&pe, &symbols, &splits, base..end)?,
        });
    }
    Ok(Audit {
        format_version: 1,
        version: version.into(),
        compiler_sha256: crate::hash::sha256_hex(&original.data),
        symbol_metadata_sha256: crate::hash::sha256_hex(&metadata),
        split_metadata_sha256: crate::hash::sha256_hex(&split_metadata),
        local_lock_address: lock_address,
        local_lock: backend_audit::references(
            &pe,
            &symbols,
            &splits,
            lock_address..lock_address + 2,
        )?,
        failure_callback_address: lock_address + 2,
        failure_callback: backend_audit::references(
            &pe,
            &symbols,
            &splits,
            lock_address + 2..lock_address + 6,
        )?,
        arenas,
        allocation_coverage_complete: false,
        reset_coverage_complete: false,
        limitations: vec![
            "Direct PE HIGHLOW references only; register-derived aliases, bulk writes, and allocator call targets require separate inspection".into(),
            "Function guards describe PE bytes with absolute relocations and direct calls normalized; they do not validate split COFF relocation target identities".into(),
            "A pool descriptor reference is not an observed allocation, reset, lifetime, node construction, or transformation edge".into(),
            "Lock state affects whether freelheap resets the local arena; a reset call alone does not prove invalidation".into(),
            "The optimizer-pool selection in ResetGallocAndPoolMemory is not assumed to reset the optimizer arena".into(),
            "The failure callback can execute arbitrary compiler code; reentrancy and failed allocation paths need runtime coverage".into(),
        ],
    })
}
