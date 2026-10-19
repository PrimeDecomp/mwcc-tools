//! Fixed-size guarded request/commit islands for retail compiler arenas.
//! Guard catalog derives from the read-only 66-body arena split audit. Installing
//! observers is not a claim of complete allocation/lifetime/source provenance.
use crate::{
    coff::{Builder, Object, ObjectSymbol, Relocation},
    disassembly,
    executable::{self, Original},
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

pub const HANDLER: &str = "_OracleRust_ArenaProbeV1";
pub const GLOBALS: &str = "_OracleRust_ArenaGlobals";

#[derive(Clone, Debug, Deserialize, Serialize)]
struct Definition {
    value: u32,
    section: i16,
    kind: u16,
    storage: u8,
    auxiliary_hex: String,
}
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct Guard {
    pub name: String,
    pub address: u32,
    pub size: u32,
    pub code_sha256: String,
    pub normalized_sha256: String,
    pub relocation_identity_sha256: String,
    definition: Definition,
}
#[derive(Clone, Debug, Deserialize)]
struct Catalog {
    guards: Vec<Guard>,
    statistics_table: Guard,
    #[serde(default)]
    incoming_groups: Vec<Guard>,
}
fn catalogs() -> &'static BTreeMap<String, Catalog> {
    static VALUE: OnceLock<BTreeMap<String, Catalog>> = OnceLock::new();
    VALUE.get_or_init(|| {
        serde_json::from_str(include_str!("source_arena_guards.json"))
            .expect("checked-in arena catalog")
    })
}
fn version_name(version: u8) -> Option<&'static str> {
    match version {
        1 => Some("GC_1_3"),
        2 => Some("GC_1_3_2"),
        3 => Some("GC_2_7"),
        4 => Some("GC_2_6"),
        _ => None,
    }
}
fn version_number(version: &str) -> Result<u8> {
    match version {
        "GC_1_3" => Ok(1),
        "GC_1_3_2" => Ok(2),
        "GC_2_7" => Ok(3),
        "GC_2_6" => Ok(4),
        _ => anyhow::bail!("unsupported arena probe version"),
    }
}

/// Pair numbers and pool indices are part of the ArenaProbeV1 wire contract.
#[derive(Clone, Copy)]
struct Pair {
    id: u8,
    function: &'static str,
    pool: u32,
    request: u32,
    request_len: usize,
    commit: u32,
    commit_len: usize,
    raw: u32,
    reserve: u32,
    result: u32,
}
const PAIRS: &[Pair] = &[
    Pair {
        id: 1,
        function: "_galloc",
        pool: 4,
        request: 5,
        request_len: 6,
        commit: 0x20,
        commit_len: 0x11,
        raw: 4,
        reserve: 4,
        result: 7,
    },
    Pair {
        id: 2,
        function: "_lalloc",
        pool: 3,
        request: 5,
        request_len: 6,
        commit: 0x20,
        commit_len: 0x11,
        raw: 4,
        reserve: 4,
        result: 7,
    },
    Pair {
        id: 3,
        function: "_aalloc",
        pool: 2,
        request: 5,
        request_len: 6,
        commit: 0x20,
        commit_len: 0x11,
        raw: 4,
        reserve: 4,
        result: 7,
    },
    Pair {
        id: 4,
        function: "_oalloc",
        pool: 1,
        request: 5,
        request_len: 6,
        commit: 0x20,
        commit_len: 0x11,
        raw: 4,
        reserve: 4,
        result: 7,
    },
    Pair {
        id: 5,
        function: "_InitNameHash",
        pool: 4,
        request: 1,
        request_len: 10,
        commit: 0x1e,
        commit_len: 0x19,
        raw: 8192,
        reserve: 8200,
        result: 7,
    },
    Pair {
        id: 6,
        function: "_GetHashNameNode",
        pool: 4,
        request: 0x7a,
        request_len: 6,
        commit: 0x95,
        commit_len: 0x12,
        raw: 1,
        reserve: 1,
        result: 0,
    },
    Pair {
        id: 7,
        function: "_GetHashNameNode",
        pool: 4,
        request: 0x109,
        request_len: 6,
        commit: 0x124,
        commit_len: 0x11,
        raw: 0,
        reserve: 0,
        result: 7,
    },
    Pair {
        id: 8,
        function: "_GetHashNameNodeExport",
        pool: 4,
        request: 0x7a,
        request_len: 6,
        commit: 0x95,
        commit_len: 0x12,
        raw: 1,
        reserve: 1,
        result: 0,
    },
    Pair {
        id: 9,
        function: "_GetHashNameNodeExport",
        pool: 4,
        request: 0x129,
        request_len: 6,
        commit: 0x144,
        commit_len: 0x11,
        raw: 0,
        reserve: 0,
        result: 7,
    },
];
/// Canonical retail coordinate validation; does not touch the filesystem.
pub fn valid_site(version: u8, pair: u8, commit: bool, site: u32) -> bool {
    let Some(c) = version_name(version).and_then(|v| catalogs().get(v)) else {
        return false;
    };
    let Some(p) = PAIRS.iter().find(|p| p.id == pair) else {
        return false;
    };
    c.guards
        .iter()
        .find(|g| g.name == p.function)
        .is_some_and(|g| g.address + if commit { p.commit } else { p.request } == site)
}
pub fn valid_callback_site(version: u8, site: u32) -> bool {
    version_name(version)
        .and_then(|v| catalogs().get(v))
        .and_then(|c| {
            c.guards
                .iter()
                .find(|g| g.name == "_select_or_allocate_pool_node")
        })
        .is_some_and(|g| g.address + 0x6a == site)
}

#[derive(Clone, Debug, Serialize)]
pub struct Site {
    pub address: u32,
    pub container: String,
    pub offset: u32,
    pub bytes: Vec<u8>,
    /// 1=request, 2=commit, 3=actual indirect allocation-failure callback.
    pub action: u32,
    pub pair: u32,
    pub pool: u32,
    /// 0=immediate; 1=index in backend Registers.slot.
    pub size_kind: u32,
    pub size_value: u32,
    /// 0=none; 1=index in backend Registers.slot.
    pub result_kind: u32,
    pub result_value: u32,
}
#[derive(Debug)]
pub struct Installed {
    pub objects: BTreeMap<String, Vec<u8>>,
    pub islands: Vec<u8>,
    pub sites: Vec<Site>,
    pub guards: Vec<Guard>,
}
fn digest(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}
fn aux_hex(s: &ObjectSymbol) -> String {
    s.auxiliary
        .iter()
        .flatten()
        .map(|b| format!("{b:02x}"))
        .collect()
}

fn sites(c: &Catalog, read: impl Fn(u32, usize) -> Result<Vec<u8>>) -> Result<Vec<Site>> {
    let mut result = vec![];
    for p in PAIRS {
        let g = c
            .guards
            .iter()
            .find(|g| g.name == p.function)
            .context("arena pair helper missing")?;
        for commit in [false, true] {
            let offset = if commit { p.commit } else { p.request };
            let len = if commit { p.commit_len } else { p.request_len };
            let address = g.address + offset;
            result.push(Site {
                address,
                container: p.function.into(),
                offset,
                bytes: read(address, len)?,
                action: if commit { 2 } else { 1 },
                pair: u32::from(p.id),
                pool: p.pool,
                size_kind: u32::from(p.id != 5),
                size_value: if commit { p.reserve } else { p.raw },
                result_kind: u32::from(commit),
                result_value: if commit { p.result } else { 0 },
            });
        }
    }
    let g = c
        .guards
        .iter()
        .find(|g| g.name == "_select_or_allocate_pool_node")
        .context("arena selector missing")?;
    result.push(Site {
        address: g.address + 0x6a,
        container: g.name.clone(),
        offset: 0x6a,
        bytes: read(g.address + 0x6a, 6)?,
        action: 3,
        pair: 0,
        pool: 0,
        size_kind: 0,
        size_value: 0,
        result_kind: 0,
        result_value: 0,
    });
    result.sort_by_key(|s| s.address);
    ensure!(
        result.len() == 19
            && result
                .windows(2)
                .all(|s| s[0].address + s[0].bytes.len() as u32 <= s[1].address),
        "overlapping/incomplete arena sites"
    );
    Ok(result)
}

fn check_boundaries(code: &[u8], base: u32, sites: &[Site]) -> Result<()> {
    let decoder = disassembly::decoder()?;
    let decoded = decoder.disasm_all(code, u64::from(base))?;
    ensure!(
        decoded.iter().map(|i| i.bytes().len()).sum::<usize>() == code.len(),
        "incomplete arena container decoding"
    );
    let boundaries = decoded
        .iter()
        .map(|i| i.address() as u32)
        .chain(std::iter::once(base + code.len() as u32))
        .collect::<BTreeSet<_>>();
    for s in sites
        .iter()
        .filter(|s| s.container.is_empty() || (base..base + code.len() as u32).contains(&s.address))
    {
        ensure!(
            boundaries.contains(&s.address)
                && boundaries.contains(&(s.address + s.bytes.len() as u32)),
            "arena patch splits instruction"
        );
    }
    for insn in decoded.iter() {
        check_direct_target(
            insn.mnemonic().unwrap_or(""),
            insn.op_str().unwrap_or(""),
            sites,
        )?;
    }
    Ok(())
}

/// Verify pinned PE helper bodies and return the nineteen canonical patch sites.
pub fn plan(root: &Path, version: &str) -> Result<Vec<Site>> {
    version_number(version)?;
    let c = &catalogs()[version];
    let original = Original::read(root, version)?;
    let pe = original.pe()?;
    let metadata = executable::read_symbols(&fs::read_to_string(
        root.join(format!("config/{version}/symbols.txt")),
    )?)?;
    for g in c
        .guards
        .iter()
        .chain(&c.incoming_groups)
        .chain(std::iter::once(&c.statistics_table))
    {
        let s = metadata
            .iter()
            .find(|s| s.name == g.name)
            .context("missing arena helper metadata")?;
        // Some table metadata spans more than its six used entries.
        ensure!(
            s.address == g.address
                && (g.name == c.statistics_table.name || executable::size(&s.options)? == g.size),
            "arena helper address/size changed: {}",
            g.name
        );
        ensure!(
            digest(pe.read(g.address, g.size as usize)?) == g.code_sha256,
            "arena PE body guard failed: {}",
            g.name
        );
    }
    let result = sites(c, |a, n| Ok(pe.read(a, n)?.to_vec()))?;
    for g in &c.guards {
        check_boundaries(pe.read(g.address, g.size as usize)?, g.address, &result)?;
    }
    Ok(result)
}

/// The guard a split object's definition of `name` gives, checked against the original's bytes: how the
/// checked-in catalogs are made, and how a new version's are derived from them (`provenance derive-guards`).
pub fn derive_guard(
    object: &Object<'_>,
    name: &str,
    address: u32,
    size: u32,
    original: &[u8],
) -> Result<Guard> {
    let found = object
        .symbols
        .values()
        .filter(|s| s.section > 0 && s.name == name)
        .collect::<Vec<_>>();
    ensure!(
        found.len() == 1,
        "duplicate/missing arena split definition: {name}"
    );
    let s = found[0];
    let section = object
        .sections
        .get(s.section as usize - 1)
        .context("arena definition section missing")?;
    let end = s
        .value
        .checked_add(size)
        .context("arena guard extent overflow")?;
    let mut actual = section
        .data
        .get(s.value as usize..end as usize)
        .context("arena split extent missing")?
        .to_vec();
    let mut expected = original.to_vec();
    let mut ids = Sha256::new();
    let mut covered = BTreeSet::new();
    ensure!(
        !section
            .relocations
            .iter()
            .any(|r| { r.offset < s.value && u64::from(r.offset) + 4 > u64::from(s.value) }),
        "relocation crosses guarded arena region start: {name}"
    );
    let mut relocs = section
        .relocations
        .iter()
        .filter(|r| (s.value..end).contains(&r.offset))
        .collect::<Vec<_>>();
    relocs.sort_by_key(|r| r.offset);
    for r in relocs {
        let at = (r.offset - s.value) as usize;
        ensure!(
            matches!(r.kind, 6 | 20)
                && at + 4 <= actual.len()
                && (at..at + 4).all(|n| covered.insert(n)),
            "overlapping/invalid arena relocation"
        );
        let target = object
            .symbols
            .get(&r.symbol)
            .context("missing arena relocation target")?;
        ids.update((at as u32).to_le_bytes());
        ids.update(r.kind.to_le_bytes());
        ids.update((target.name.len() as u32).to_le_bytes());
        ids.update(target.name.as_bytes());
        ids.update(target.value.to_le_bytes());
        ids.update(target.section.to_le_bytes());
        ids.update(target.kind.to_le_bytes());
        ids.update([target.storage]);
        ids.update((target.auxiliary.len() as u32).to_le_bytes());
        for a in &target.auxiliary {
            ids.update(a);
        }
        ids.update(&actual[at..at + 4]);
        actual[at..at + 4].fill(0);
        expected[at..at + 4].fill(0);
    }
    ensure!(actual == expected, "arena split body changed: {name}");
    Ok(Guard {
        name: name.into(),
        address,
        size,
        code_sha256: digest(original),
        normalized_sha256: digest(&actual),
        relocation_identity_sha256: format!("{:x}", ids.finalize()),
        definition: Definition {
            value: s.value,
            section: s.section,
            kind: s.kind,
            storage: s.storage,
            auxiliary_hex: aux_hex(s),
        },
    })
}

/// Derive guards for `names` (`NAME` or `NAME=SIZE`, for a guard shorter than its metadata) from
/// `version`'s symbols, original executable and split objects (`build/<version>/obj`).
pub fn derive_guards(root: &Path, version: &str, names: &[String]) -> Result<Vec<Guard>> {
    let original = Original::read(root, version)?;
    let pe = original.pe()?;
    let metadata = executable::read_symbols(&fs::read_to_string(
        root.join(format!("config/{version}/symbols.txt")),
    )?)?;
    let mut objects = vec![];
    let mut pending = vec![root.join(format!("build/{version}/obj"))];
    while let Some(directory) = pending.pop() {
        for entry in fs::read_dir(&directory)
            .with_context(|| format!("split objects {}", directory.display()))?
        {
            let path = entry?.path();
            if path.is_dir() {
                pending.push(path);
            } else if path.extension().is_some_and(|e| e == "obj") {
                objects.push(fs::read(path)?);
            }
        }
    }
    let objects = objects
        .iter()
        .map(|bytes| Object::parse(bytes))
        .collect::<Result<Vec<_>>>()?;
    names
        .iter()
        .map(|request| {
            let (name, size) = match request.split_once('=') {
                Some((name, size)) => (name, Some(executable::hex(size)?)),
                None => (request.as_str(), None),
            };
            let matching = metadata
                .iter()
                .filter(|s| s.name == name)
                .collect::<Vec<_>>();
            ensure!(matching.len() == 1, "duplicate/missing metadata: {name}");
            let symbol = matching[0];
            let size = match size {
                Some(size) => size,
                None => executable::size(&symbol.options)?,
            };
            let defining = objects
                .iter()
                .filter(|o| o.symbols.values().any(|s| s.section > 0 && s.name == name))
                .collect::<Vec<_>>();
            ensure!(
                defining.len() == 1,
                "duplicate/missing split definition: {name}"
            );
            derive_guard(
                defining[0],
                name,
                symbol.address,
                size,
                pe.read(symbol.address, size as usize)?,
            )
        })
        .collect()
}

pub(crate) fn check_guard(object: &Object<'_>, g: &Guard, original: &[u8]) -> Result<()> {
    let actual = derive_guard(object, &g.name, g.address, g.size, original)?;
    let (d, e) = (&actual.definition, &g.definition);
    ensure!(
        d.value == e.value
            && d.section == e.section
            && d.kind == e.kind
            && d.storage == e.storage
            && d.auxiliary_hex == e.auxiliary_hex,
        "arena definition identity changed: {}",
        g.name
    );
    ensure!(
        actual.normalized_sha256 == g.normalized_sha256,
        "arena split body changed: {}",
        g.name
    );
    ensure!(
        actual.relocation_identity_sha256 == g.relocation_identity_sha256,
        "arena relocation target/addend identity changed: {}",
        g.name
    );
    Ok(())
}

// Exactly two cdecl arguments (Registers*, Descriptor*); the saved ESP is the
// original retail ESP minus 8 (island CALL return plus PUSHFD), before PUSHAD.
pub(crate) fn callback(descriptor: u32, handler: u32) -> (Vec<u8>, Vec<Relocation>) {
    let mut code = vec![
        0x9c, 0x60, 0x89, 0xe0, 0x81, 0xec, 0x10, 0x02, 0, 0, 0x83, 0xe4, 0xf0, 0x0f, 0xae, 0x04,
        0x24, 0x50, 0x68,
    ];
    let desc = code.len() as u32;
    code.extend(0u32.to_le_bytes());
    code.extend([0x50, 0xfc, 0xe8]);
    let call = code.len() as u32;
    code.extend(0u32.to_le_bytes());
    code.extend([
        0x83, 0xc4, 0x08, 0x58, 0x0f, 0xae, 0x0c, 0x24, 0x89, 0xc4, 0x61, 0x9d,
    ]);
    (
        code,
        vec![
            Relocation {
                offset: desc,
                symbol: descriptor,
                kind: 6,
            },
            Relocation {
                offset: call,
                symbol: handler,
                kind: 20,
            },
        ],
    )
}

fn section_base(
    object: &Object<'_>,
    section: i16,
    metadata: &BTreeMap<String, u32>,
) -> Result<Option<u32>> {
    let mut bases = BTreeSet::new();
    for s in object.symbols.values().filter(|s| s.section == section) {
        if let Some(address) = metadata.get(&s.name) {
            bases.insert(
                address
                    .checked_sub(s.value)
                    .context("arena section anchor underflow")?,
            );
        }
    }
    ensure!(bases.len() <= 1, "contradictory arena section anchors");
    Ok(bases.into_iter().next())
}
fn inside(sites: &[Site], address: u32) -> bool {
    sites
        .iter()
        .any(|s| s.address < address && address < s.address + s.bytes.len() as u32)
}

fn check_direct_target(mnemonic: &str, operand: &str, sites: &[Site]) -> Result<()> {
    let mnemonic = mnemonic.split_whitespace().last().unwrap_or("");
    ensure!(
        !matches!(mnemonic, "lcall" | "ljmp"),
        "unsupported far incoming arena branch"
    );
    if (mnemonic == "call" || mnemonic.starts_with('j') || mnemonic.starts_with("loop"))
        && let Some(hex) = operand.strip_prefix("0x")
    {
        let target = u32::from_str_radix(hex, 16).context("unsupported direct arena target")?;
        ensure!(
            !inside(sites, target),
            "incoming branch enters arena stolen interior"
        );
    }
    Ok(())
}

fn check_input_branches(
    section: &crate::coff::ObjectSection<'_>,
    base: u32,
    sites: &[Site],
    guarded_cos_group: bool,
) -> Result<()> {
    if section.flags & 0x20 == 0 {
        return Ok(());
    }
    if guarded_cos_group {
        ensure!(
            section.data.len() == 0x36
                && [11, 27, 43]
                    .into_iter()
                    .all(|at| section.data[at..at + 5] == [0; 5]),
            "guarded COS group padding changed"
        );
        // GC2.7's one split symbol includes four actual functions, separated by
        // five zero padding bytes. Linear decoding across that padding consumes
        // subsequent opcodes as operands, hiding the last CALL. The entire body
        // and all three COFF relocations were independently guarded above.
        for (start, end) in [(0, 11), (16, 27), (32, 43), (48, 54)] {
            let fragment = crate::coff::ObjectSection {
                name: section.name.clone(),
                flags: section.flags,
                size: (end - start) as u32,
                raw_offset: 0,
                relocation_offset: 0,
                data: std::borrow::Cow::Borrowed(&section.data[start..end]),
                relocations: section
                    .relocations
                    .iter()
                    .filter(|r| (start as u32..end as u32).contains(&r.offset))
                    .map(|r| Relocation {
                        offset: r.offset - start as u32,
                        symbol: r.symbol,
                        kind: r.kind,
                    })
                    .collect(),
            };
            check_input_branches(&fragment, base + start as u32, sites, false)?;
        }
        return Ok(());
    }
    let decoder = disassembly::decoder()?;
    let decoded = decoder.disasm_all(&section.data, u64::from(base))?;
    let consumed = decoded.iter().map(|i| i.bytes().len()).sum::<usize>();
    // These pinned split ranges omit the final byte of RET imm16, or the final
    // immediate of ROL BL,imm8. Neither incomplete suffix can hide a branch.
    // Every other incomplete instruction is rejected rather than skipped.
    let tail = &section.data[consumed..];
    ensure!(
        tail.is_empty() || matches!(tail, [0xc2, _] | [0xc0, 0xc3]),
        "undecoded incoming arena code: section={} base={:#x} decoded={} size={} tail={:02x?}",
        section.name,
        base,
        consumed,
        section.data.len(),
        tail
    );
    for i in decoded.iter() {
        let at = (i.address() - u64::from(base)) as u32;
        if section
            .relocations
            .iter()
            .any(|r| at <= r.offset && r.offset < at + i.bytes().len() as u32)
        {
            continue;
        }
        check_direct_target(i.mnemonic().unwrap_or(""), i.op_str().unwrap_or(""), sites)?;
    }
    Ok(())
}

/// Internal code-window adapter shared by the two guarded installers.
#[derive(Clone, Debug)]
pub(crate) struct CodeWindow {
    pub address: u32,
    pub length: u32,
    pub container: String,
}
fn window_sites(windows: &[CodeWindow]) -> Vec<Site> {
    windows
        .iter()
        .map(|w| Site {
            address: w.address,
            container: w.container.clone(),
            offset: 0,
            bytes: vec![0; w.length as usize],
            action: 0,
            pair: 0,
            pool: 0,
            size_kind: 0,
            size_value: 0,
            result_kind: 0,
            result_value: 0,
        })
        .collect()
}
pub(crate) fn check_window_boundaries(
    code: &[u8],
    base: u32,
    windows: &[CodeWindow],
) -> Result<()> {
    check_boundaries(code, base, &window_sites(windows))
}
/// Full mapped-input branch/alias/relocation scan. Supplemental coarse code
/// groups are guarded independently before their private-entry decoding is used.
pub(crate) fn validate_incoming_windows(
    root: &Path,
    version: &str,
    inputs: &[(String, Vec<u8>)],
    windows: &[CodeWindow],
    containers: &[Guard],
) -> Result<()> {
    version_number(version)?;
    let c = &catalogs()[version];
    let original = Original::read(root, version)?;
    let pe = original.pe()?;
    let metadata = executable::read_symbols(&fs::read_to_string(
        root.join(format!("config/{version}/symbols.txt")),
    )?)?
    .into_iter()
    .map(|s| (s.name, s.address))
    .collect::<BTreeMap<_, _>>();
    let sites = window_sites(windows);
    let mut checked = BTreeSet::new();
    for (_, bytes) in inputs {
        let object = Object::parse(bytes)?;
        for g in &c.incoming_groups {
            if object
                .symbols
                .values()
                .any(|s| s.section > 0 && s.name == g.name)
            {
                ensure!(
                    checked.insert(g.name.clone()),
                    "duplicate guarded incoming group"
                );
                ensure!(
                    metadata.get(&g.name) == Some(&g.address)
                        && digest(pe.read(g.address, g.size as usize)?) == g.code_sha256,
                    "incoming group PE guard changed"
                );
                check_guard(&object, g, pe.read(g.address, g.size as usize)?)?;
            }
        }
    }
    ensure!(
        checked.len() == c.incoming_groups.len(),
        "missing guarded incoming group"
    );
    // Incoming relocations, including section symbols and aliases in other inputs.
    let mut aliases = metadata.clone();
    for (path, bytes) in inputs {
        let object = Object::parse(bytes)?;
        for (index, section) in object.sections.iter().enumerate() {
            let Some(base) = section_base(&object, index as i16 + 1, &metadata)? else {
                continue;
            };
            let group = c
                .incoming_groups
                .iter()
                .any(|g| g.address == base && g.size as usize == section.data.len());
            check_input_branches(section, base, &sites, group).with_context(|| {
                format!(
                    "arena incoming scan input={path} section={} symbols={:?}",
                    index + 1,
                    object
                        .symbols
                        .values()
                        .filter(|s| s.section == index as i16 + 1 && s.storage == 2)
                        .map(|s| s.name.as_str())
                        .collect::<Vec<_>>()
                )
            })?;
            for symbol in object
                .symbols
                .values()
                .filter(|s| s.section == index as i16 + 1 && s.name != section.name)
            {
                let address = base
                    .checked_add(symbol.value)
                    .context("arena alias address overflow")?;
                ensure!(
                    !inside(&sites, address),
                    "defined alias enters arena stolen interior"
                );
                if sites
                    .iter()
                    .any(|s| (s.address..=s.address + s.bytes.len() as u32).contains(&address))
                {
                    aliases.insert(symbol.name.clone(), address);
                }
            }
        }
    }
    for (_, bytes) in inputs {
        let object = Object::parse(bytes)?;
        for section in &object.sections {
            for r in &section.relocations {
                let target = object
                    .symbols
                    .get(&r.symbol)
                    .context("missing incoming arena relocation target")?;
                let address = if let Some(a) = aliases.get(&target.name) {
                    Some(*a)
                } else if target.section > 0 {
                    section_base(&object, target.section, &metadata)?
                        .and_then(|b| b.checked_add(target.value))
                } else {
                    None
                };
                if let Some(address) = address {
                    let raw = section
                        .data
                        .get(r.offset as usize..r.offset as usize + 4)
                        .context("incoming arena relocation outside section")?;
                    let addend = u32::from_le_bytes(raw.try_into()?);
                    if matches!(r.kind, 6 | 20) {
                        ensure!(
                            !inside(&sites, address.wrapping_add(addend)),
                            "incoming relocation enters arena stolen interior"
                        );
                    } else {
                        ensure!(
                            !containers
                                .iter()
                                .filter(|g| sites.iter().any(|s| s.container == g.name))
                                .any(|g| {
                                    (g.address..g.address + g.size).contains(&address)
                                        || (g.address..g.address + g.size)
                                            .contains(&address.wrapping_add(addend))
                                }),
                            "unsupported relocation targets patched arena helper"
                        );
                    }
                }
            }
        }
    }
    Ok(())
}

/// Validate every audited routine before changing any input. Original sections
/// retain their sizes and all non-patch contents, symbols and relocation targets.
pub fn install(root: &Path, version: &str, inputs: &[(String, Vec<u8>)]) -> Result<Installed> {
    let version_id = version_number(version)?;
    let c = &catalogs()[version];
    let sites = plan(root, version)?;
    let original = Original::read(root, version)?;
    let pe = original.pe()?;
    let metadata = executable::read_symbols(&fs::read_to_string(
        root.join(format!("config/{version}/symbols.txt")),
    )?)?
    .into_iter()
    .map(|s| (s.name, s.address))
    .collect::<BTreeMap<_, _>>();
    let lock = match version_id {
        1 => 0x5db510,
        2 => 0x5dd510,
        3 | 4 => 0x5df568,
        _ => unreachable!(),
    };
    ensure!(
        metadata.get("_heap_pool") == Some(&(lock + 6))
            && metadata.get("_data_0057fdd8") == Some(&lock),
        "arena globals ABI changed"
    );
    let mut checked = BTreeSet::new();
    for (_, bytes) in inputs {
        let object = Object::parse(bytes)?;
        for g in c
            .guards
            .iter()
            .chain(&c.incoming_groups)
            .chain(std::iter::once(&c.statistics_table))
        {
            if object
                .symbols
                .values()
                .any(|s| s.section > 0 && s.name == g.name)
            {
                ensure!(
                    checked.insert(g.name.clone()),
                    "duplicate guarded arena helper"
                );
                check_guard(&object, g, pe.read(g.address, g.size as usize)?)?;
            }
        }
    }
    ensure!(
        checked.len() == 23 + c.incoming_groups.len(),
        "missing guarded arena helpers/table in link inputs"
    );
    let windows = sites
        .iter()
        .map(|s| CodeWindow {
            address: s.address,
            length: s.bytes.len() as u32,
            container: s.container.clone(),
        })
        .collect::<Vec<_>>();
    validate_incoming_windows(root, version, inputs, &windows, &c.guards)?;
    let mut islands = Builder::default();
    let text = islands.section(".text", 0x60500020, vec![], 0, vec![])?;
    let data = islands.section(".rdata", 0x40300040, vec![], 0, vec![])?;
    let handler = islands.external(HANDLER)?;
    let heap = islands.external("_heap_pool")?;
    let lock_symbol = islands.external("_data_0057fdd8")?;
    let globals = islands.add_symbol(GLOBALS, 0, data, 0, 2, &[])?;
    islands.sections[data as usize - 1].data.extend([0; 8]);
    islands.sections[data as usize - 1]
        .data
        .extend(u32::from(version_id).to_le_bytes());
    islands.sections[data as usize - 1].relocations.extend([
        Relocation {
            offset: 0,
            symbol: heap,
            kind: 6,
        },
        Relocation {
            offset: 4,
            symbol: lock_symbol,
            kind: 6,
        },
    ]);
    let mut changed = BTreeMap::new();
    let mut installed = BTreeSet::new();
    for (path, bytes) in inputs {
        let object = Object::parse(bytes)?;
        let mut patches = vec![];
        for site in &sites {
            let Some(symbol) = object
                .symbols
                .values()
                .find(|s| s.section > 0 && s.name == site.container)
            else {
                continue;
            };
            ensure!(installed.insert(site.address), "duplicate arena patch");
            let si = symbol.section as usize - 1;
            let section = &object.sections[si];
            let start = symbol.value + site.offset;
            let end = start + site.bytes.len() as u32;
            let copied = section
                .data
                .get(start as usize..end as usize)
                .context("arena patch out of range")?
                .to_vec();
            let descriptor_offset = islands.sections[data as usize - 1].data.len() as u32;
            let descriptor = islands.add_symbol(
                &format!("_OracleRust_ArenaDescriptor_{:08x}", site.address),
                descriptor_offset,
                data,
                0,
                3,
                &[],
            )?;
            for value in [
                0,
                site.address,
                site.action,
                site.pair,
                site.pool,
                site.size_kind,
                site.size_value,
                site.result_kind,
                site.result_value,
            ] {
                islands.sections[data as usize - 1]
                    .data
                    .extend(value.to_le_bytes());
            }
            islands.sections[data as usize - 1]
                .relocations
                .push(Relocation {
                    offset: descriptor_offset,
                    symbol: globals,
                    kind: 6,
                });
            let offset = islands.sections[text as usize - 1].data.len() as u32;
            let name = format!("_OracleRust_ArenaIsland_{:08x}", site.address);
            islands.add_symbol(&name, offset, text, 0x20, 2, &[])?;
            let mut emitted = vec![];
            let mut relocations = vec![];
            let (probe, mut fields) = callback(descriptor, handler);
            if site.action == 2 {
                emitted.extend(&copied);
            }
            let probe_offset = emitted.len() as u32;
            for field in &mut fields {
                field.offset += offset + probe_offset;
            }
            relocations.extend(fields);
            emitted.extend(probe);
            if site.action == 3 {
                emitted.extend([0x8d, 0x64, 0x24, 0x04]);
            }
            let copied_offset = if site.action == 2 {
                0
            } else {
                let at = emitted.len() as u32;
                emitted.extend(&copied);
                at
            };
            for r in section
                .relocations
                .iter()
                .filter(|r| (start..end).contains(&r.offset))
            {
                ensure!(
                    matches!(r.kind, 6 | 20) && r.offset + 4 <= end,
                    "arena stolen relocation crosses boundary"
                );
                let target = &object.symbols[&r.symbol];
                let external = if target.storage == 2 {
                    islands.external(&target.name)?
                } else {
                    ensure!(
                        r.kind == 6 && target.section > 0,
                        "unsupported displaced arena section reference"
                    );
                    let anchor = object
                        .symbols
                        .values()
                        .find(|s| {
                            s.section == target.section
                                && s.storage == 2
                                && s.name == "_data_0057fdd8"
                        })
                        .context("arena section relocation lacks guarded lock anchor")?;
                    let at = (copied_offset + r.offset - start) as usize;
                    let addend = u32::from_le_bytes(emitted[at..at + 4].try_into()?)
                        .wrapping_add(target.value)
                        .wrapping_sub(anchor.value);
                    emitted[at..at + 4].copy_from_slice(&addend.to_le_bytes());
                    islands.external(&anchor.name)?
                };
                relocations.push(Relocation {
                    offset: offset + copied_offset + r.offset - start,
                    symbol: external,
                    kind: r.kind,
                });
            }
            if site.action == 3 {
                emitted.push(0xe9);
                let resume = islands.external(&site.container)?;
                relocations.push(Relocation {
                    offset: offset + emitted.len() as u32,
                    symbol: resume,
                    kind: 20,
                });
                emitted.extend((site.offset + site.bytes.len() as u32).to_le_bytes());
            } else {
                emitted.push(0xc3);
            }
            islands.sections[text as usize - 1].data.extend(emitted);
            islands.sections[text as usize - 1]
                .relocations
                .extend(relocations);
            patches.push((si, start, end, name));
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
        for (&index, symbol) in &object.symbols {
            let section_symbol = symbol.storage == 3
                && symbol.value == 0
                && symbol.section > 0
                && symbol.auxiliary.len() == 1
                && symbol.name == object.sections[symbol.section as usize - 1].name;
            let new = if section_symbol {
                rebuilt.section_symbols[&symbol.section]
            } else {
                let auxiliary = symbol
                    .auxiliary
                    .iter()
                    .flatten()
                    .copied()
                    .collect::<Vec<_>>();
                rebuilt.add_symbol(
                    &symbol.name,
                    symbol.value,
                    symbol.section,
                    symbol.kind,
                    symbol.storage,
                    &auxiliary,
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
        for (section, start, end, name) in patches {
            let external = rebuilt.external(&name)?;
            let s = &mut rebuilt.sections[section];
            s.data[start as usize..end as usize].fill(0x90);
            s.data[start as usize] = 0xe8;
            s.data[start as usize + 1..start as usize + 5].fill(0);
            s.relocations.retain(|r| !(start..end).contains(&r.offset));
            s.relocations.push(Relocation {
                offset: start + 1,
                symbol: external,
                kind: 20,
            });
        }
        rebuilt.refresh_section_definitions()?;
        ensure!(
            changed.insert(path.clone(), rebuilt.finish()?).is_none(),
            "duplicate arena input path"
        );
    }
    ensure!(
        installed.len() == sites.len(),
        "not every guarded arena site was installed"
    );
    for section in &mut islands.sections {
        section.size = section.data.len().try_into()?;
    }
    islands.refresh_section_definitions()?;
    Ok(Installed {
        objects: changed,
        islands: islands.finish()?,
        sites,
        guards: c.guards.iter().chain(&c.incoming_groups).cloned().collect(),
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn guard_rejects_relocation_fields_crossing_region_start() {
        let mut builder = Builder::default();
        let text = builder
            .section(".text", 0x60500020, vec![0x90; 8], 8, vec![])
            .unwrap();
        builder
            .add_symbol("_helper", 4, text, 0x20, 2, &[])
            .unwrap();
        let target = builder.external("_target").unwrap();
        let bytes = builder.finish().unwrap();
        let mut object = Object::parse(&bytes).unwrap();
        let guard = Guard {
            name: "_helper".into(),
            address: 0x1004,
            size: 4,
            code_sha256: digest(&[0x90; 4]),
            normalized_sha256: digest(&[0x90; 4]),
            relocation_identity_sha256: digest(&[]),
            definition: Definition {
                value: 4,
                section: text,
                kind: 0x20,
                storage: 2,
                auxiliary_hex: String::new(),
            },
        };
        check_guard(&object, &guard, &[0x90; 4]).unwrap();
        for kind in [6, 20, 7] {
            for offset in 1..4 {
                object.sections[0].relocations = vec![Relocation {
                    offset,
                    symbol: target,
                    kind,
                }];
                assert!(
                    check_guard(&object, &guard, &[0x90; 4])
                        .unwrap_err()
                        .to_string()
                        .contains("crosses guarded")
                );
            }
        }
        object.sections[0].relocations = vec![Relocation {
            offset: 0,
            symbol: target,
            kind: 6,
        }];
        check_guard(&object, &guard, &[0x90; 4]).unwrap();
    }
    #[test]
    fn canonical_sites_and_callback_abi_are_closed() {
        for version in 1..=4 {
            let c = &catalogs()[version_name(version).unwrap()];
            assert_eq!(c.guards.len(), 22);
            for p in PAIRS {
                let g = c.guards.iter().find(|g| g.name == p.function).unwrap();
                assert!(valid_site(version, p.id, false, g.address + p.request));
                assert!(valid_site(version, p.id, true, g.address + p.commit));
                assert!(!valid_site(version, p.id, true, g.address + p.commit + 1));
            }
            assert!(!valid_site(version, 0, false, 0));
        }
        assert!(!valid_site(0, 1, false, 0));
        let (bytes, relocs) = callback(1, 2);
        assert_eq!(relocs.len(), 2);
        assert!(bytes.ends_with(&[0x89, 0xc4, 0x61, 0x9d]));
        assert!(bytes.windows(3).any(|w| w == [0x83, 0xc4, 8]));
    }

    #[test]
    fn incoming_direct_targets_reject_interiors_including_prefixed_branches() {
        let site = Site {
            address: 0x1000,
            container: String::new(),
            offset: 0,
            bytes: vec![0x90; 6],
            action: 1,
            pair: 1,
            pool: 4,
            size_kind: 1,
            size_value: 4,
            result_kind: 0,
            result_value: 0,
        };
        for (mnemonic, target) in [
            ("call", "0x1001"),
            ("jmp", "0x1005"),
            ("bnd jmp", "0x1003"),
            ("loopne", "0x1002"),
        ] {
            assert!(check_direct_target(mnemonic, target, std::slice::from_ref(&site)).is_err());
        }
        for target in ["0x1000", "0x1006", "eax", "dword ptr [eax]"] {
            assert!(check_direct_target("call", target, std::slice::from_ref(&site)).is_ok());
        }
        let mut bytes = vec![0xe9];
        bytes.extend((0x1001i32 - 0x2005).to_le_bytes());
        assert!(check_boundaries(&bytes, 0x2000, std::slice::from_ref(&site)).is_err());
    }

    #[test]
    fn guarded_cos_private_entries_do_not_skip_hidden_final_call() {
        let base = 0x4421b0;
        let mut bytes = vec![];
        for _ in 0..3 {
            bytes.extend([
                0x8b, 0x44, 0x24, 4, 0x50, 0xe8, 0, 0, 0, 0, 0xc3, 0, 0, 0, 0, 0,
            ]);
        }
        bytes.push(0xe8);
        bytes.extend((0x41a4f0i32 - (base as i32 + 0x35)).to_le_bytes());
        bytes.push(0xc3);
        let section = crate::coff::ObjectSection {
            name: ".text".into(),
            flags: 0x60500020,
            size: 54,
            raw_offset: 0,
            relocation_offset: 0,
            data: std::borrow::Cow::Owned(bytes.clone()),
            relocations: vec![],
        };
        assert!(check_input_branches(&section, base, &[], false).is_err());
        check_input_branches(&section, base, &[], true).unwrap();
        let site = Site {
            address: 0x1000,
            container: String::new(),
            offset: 0,
            bytes: vec![0x90; 6],
            action: 1,
            pair: 1,
            pool: 4,
            size_kind: 1,
            size_value: 4,
            result_kind: 0,
            result_value: 0,
        };
        let mut bad = section.clone();
        bad.data.to_mut()[49..53]
            .copy_from_slice(&(0x1001i32 - (base as i32 + 0x35)).to_le_bytes());
        assert!(check_input_branches(&bad, base, std::slice::from_ref(&site), true).is_err());
        bad.data.to_mut()[11] = 0xeb;
        assert!(check_input_branches(&bad, base, &[], true).is_err());
    }
}
