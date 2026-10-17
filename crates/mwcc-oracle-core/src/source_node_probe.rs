//! Guarded ordinary-expression snapshot and prerequisites for frontend wrappers.
//! This observes one initialized Statement root slot, not source lifetimes or
//! declaration-initializer semantics. Clone and mode wrappers are linked later.
use crate::{
    coff::{Builder, Object, Relocation},
    executable::{self, Original},
    source_arena_probe::{self as shared, CodeWindow, Guard},
};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{
    collections::{BTreeMap, BTreeSet},
    fs,
    path::Path,
    sync::OnceLock,
};

pub const HANDLER: &str = "_OracleRust_SourceProbeV1";
const STORE: &[u8] = &[0x89, 0x43, 0x0a, 0x8b, 0x43, 0x0a];
const OFFSET: u32 = 0x238e;
#[derive(Clone, Debug, Deserialize)]
struct Global {
    name: String,
    address: u32,
    metadata_size: u32,
    read_width: u32,
}
#[derive(Clone, Debug, Deserialize)]
struct Catalog {
    guards: Vec<Guard>,
    tables: Vec<Guard>,
    globals: Vec<Global>,
}
fn catalogs() -> &'static BTreeMap<String, Catalog> {
    static VALUE: OnceLock<BTreeMap<String, Catalog>> = OnceLock::new();
    VALUE.get_or_init(|| {
        serde_json::from_str(include_str!("source_node_guards.json"))
            .expect("checked-in source node guards")
    })
}
fn version_number(version: &str) -> Result<u32> {
    match version {
        "GC_1_3" => Ok(1),
        "GC_1_3_2" => Ok(2),
        "GC_2_7" => Ok(3),
        "GC_2_6" => Ok(4),
        _ => anyhow::bail!("unsupported source node probe version"),
    }
}
/// Canonical retail store coordinates; no filesystem access or runtime VA claim.
pub fn valid_site(version: u8, site: u32) -> bool {
    matches!(
        (version, site),
        (1, 0x4f1c9e) | (2, 0x4f274e) | (3, 0x4f360e) | (4, 0x4f36ee)
    )
}
#[derive(Clone, Debug, Serialize)]
pub struct Site {
    pub address: u32,
    pub container: String,
    pub offset: u32,
    pub bytes: Vec<u8>,
    pub version: u32,
    pub kind: u32,
    pub statement_slot: u32,
}
impl Site {
    fn window(&self) -> CodeWindow {
        CodeWindow {
            address: self.address,
            length: self.bytes.len() as u32,
            container: self.container.clone(),
        }
    }
}
#[derive(Debug)]
pub struct Installed {
    pub objects: BTreeMap<String, Vec<u8>>,
    pub islands: Vec<u8>,
    pub sites: Vec<Site>,
    pub guards: Vec<Guard>,
}
/// Validate all prerequisite bodies, six complete switch tables and the exact
/// metadata fragments containing frontend context globals before planning.
pub fn plan(root: &Path, version: &str) -> Result<Vec<Site>> {
    let number = version_number(version)?;
    let c = &catalogs()[version];
    ensure!(
        c.guards.len() == 12 && c.tables.len() == 6 && c.globals.len() == 3,
        "incomplete source node guard catalog"
    );
    let original = Original::read(root, version)?;
    let pe = original.pe()?;
    let metadata = executable::read_symbols(&fs::read_to_string(
        root.join(format!("config/{version}/symbols.txt")),
    )?)?;
    for g in c.guards.iter().chain(&c.tables) {
        let matching = metadata
            .iter()
            .filter(|s| s.name == g.name)
            .collect::<Vec<_>>();
        ensure!(
            matching.len() == 1,
            "duplicate/missing source helper metadata: {}",
            g.name
        );
        let s = matching[0];
        ensure!(
            s.address == g.address && executable::size(&s.options)? == g.size,
            "source helper metadata changed: {}",
            g.name
        );
        ensure!(
            format!("{:x}", Sha256::digest(pe.read(g.address, g.size as usize)?)) == g.code_sha256,
            "source PE guard changed: {}",
            g.name
        );
    }
    for g in &c.globals {
        let matching = metadata
            .iter()
            .filter(|s| s.name == g.name)
            .collect::<Vec<_>>();
        ensure!(
            matching.len() == 1
                && matching[0].address == g.address
                && executable::size(&matching[0].options)? == g.metadata_size
                && ((g.name == "_cscope_currentfunc" && g.read_width == 4)
                    || (g.name != "_cscope_currentfunc" && g.read_width == 1)),
            "source context global metadata changed: {}",
            g.name
        );
    }
    let g = c
        .guards
        .iter()
        .find(|g| g.name == "_parse_statement")
        .context("source statement guard missing")?;
    let address = g
        .address
        .checked_add(OFFSET)
        .context("source site overflow")?;
    ensure!(
        valid_site(number as u8, address) && pe.read(address, STORE.len())? == STORE,
        "source store coordinate/bytes changed"
    );
    let site = Site {
        address,
        container: g.name.clone(),
        offset: OFFSET,
        bytes: STORE.to_vec(),
        version: number,
        kind: 1,
        statement_slot: 4,
    };
    shared::check_window_boundaries(
        pe.read(g.address, g.size as usize)?,
        g.address,
        &[site.window()],
    )?;
    Ok(vec![site])
}
/// All bodies/table entries and relocation targets/addends are validated before
/// emitting any patch. Original object extents and nonpatch contents are kept.
pub fn install(root: &Path, version: &str, inputs: &[(String, Vec<u8>)]) -> Result<Installed> {
    let sites = plan(root, version)?;
    let c = &catalogs()[version];
    let original = Original::read(root, version)?;
    let pe = original.pe()?;
    let guards = c
        .guards
        .iter()
        .chain(&c.tables)
        .cloned()
        .collect::<Vec<_>>();
    let mut checked = BTreeSet::new();
    for (_, bytes) in inputs {
        let object = Object::parse(bytes)?;
        for g in &guards {
            if object
                .symbols
                .values()
                .any(|s| s.section > 0 && s.name == g.name)
            {
                ensure!(
                    checked.insert(g.name.clone()),
                    "duplicate source prerequisite definition"
                );
                shared::check_guard(&object, g, pe.read(g.address, g.size as usize)?)?;
            }
        }
    }
    ensure!(
        checked.len() == guards.len(),
        "missing guarded source prerequisites/tables"
    );
    shared::validate_incoming_windows(
        root,
        version,
        inputs,
        &sites.iter().map(Site::window).collect::<Vec<_>>(),
        &c.guards,
    )?;
    let site = &sites[0];
    let mut islands = Builder::default();
    let text = islands.section(".text", 0x60500020, vec![], 0, vec![])?;
    let data = islands.section(".rdata", 0x40300040, vec![], 0, vec![])?;
    let handler = islands.external(HANDLER)?;
    let descriptor = islands.add_symbol("_OracleRust_SourceDescriptor", 0, data, 0, 3, &[])?;
    for value in [site.version, site.address, site.kind, site.statement_slot] {
        islands.sections[data as usize - 1]
            .data
            .extend(value.to_le_bytes());
    }
    let name = format!("_OracleRust_SourceIsland_{:08x}", site.address);
    islands.add_symbol(&name, 0, text, 0x20, 2, &[])?;
    // Commit the initialized root and restore EAX's original post-store value
    // before observing; neither displaced instruction changes flags or ESP.
    let (callback, mut relocs) = shared::callback(descriptor, handler);
    for r in &mut relocs {
        r.offset += STORE.len() as u32;
    }
    let section = &mut islands.sections[text as usize - 1];
    section.data.extend(STORE);
    section.data.extend(callback);
    section.data.push(0xc3);
    section.relocations = relocs;
    let mut changed = BTreeMap::new();
    for (path, bytes) in inputs {
        let object = Object::parse(bytes)?;
        let Some(symbol) = object
            .symbols
            .values()
            .find(|s| s.section > 0 && s.name == site.container)
        else {
            continue;
        };
        ensure!(changed.is_empty(), "duplicate source patch input");
        let si = symbol.section as usize - 1;
        let start = symbol.value + site.offset;
        let end = start + STORE.len() as u32;
        let source = &object.sections[si];
        ensure!(
            source.data.get(start as usize..end as usize) == Some(STORE),
            "source patch bytes changed"
        );
        ensure!(
            !source
                .relocations
                .iter()
                .any(|r| r.offset < end && r.offset.saturating_add(4) > start),
            "source store overlaps relocation"
        );
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
        for (&index, symbol) in &object.symbols {
            let section_symbol = symbol.storage == 3
                && symbol.value == 0
                && symbol.section > 0
                && symbol.auxiliary.len() == 1
                && symbol.name == object.sections[symbol.section as usize - 1].name;
            let new = if section_symbol {
                rebuilt.section_symbols[&symbol.section]
            } else {
                rebuilt.add_symbol(
                    &symbol.name,
                    symbol.value,
                    symbol.section,
                    symbol.kind,
                    symbol.storage,
                    &symbol
                        .auxiliary
                        .iter()
                        .flatten()
                        .copied()
                        .collect::<Vec<_>>(),
                )?
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
        let external = rebuilt.external(&name)?;
        let section = &mut rebuilt.sections[si];
        section.data[start as usize..end as usize].fill(0x90);
        section.data[start as usize] = 0xe8;
        section.data[start as usize + 1..start as usize + 5].fill(0);
        section.relocations.push(Relocation {
            offset: start + 1,
            symbol: external,
            kind: 20,
        });
        rebuilt.refresh_section_definitions()?;
        changed.insert(path.clone(), rebuilt.finish()?);
    }
    ensure!(changed.len() == 1, "source store not installed");
    for section in &mut islands.sections {
        section.size = section.data.len().try_into()?;
    }
    islands.refresh_section_definitions()?;
    Ok(Installed {
        objects: changed,
        islands: islands.finish()?,
        sites,
        guards,
    })
}
