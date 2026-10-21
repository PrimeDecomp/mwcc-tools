mod common;
use common::collect;
use mwcc_oracle_core::{
    coff::Object,
    source_node_probe::{self, Site},
};
use std::{env, fs, path::Path};

fn identity(object: &Object<'_>, index: u32) -> (String, u32, i16, u16, u8, Vec<[u8; 18]>) {
    let s = &object.symbols[&index];
    (
        s.name.clone(),
        s.value,
        s.section,
        s.kind,
        s.storage,
        s.auxiliary.clone(),
    )
}
#[test]
fn closed_source_site_coordinates() {
    assert!(source_node_probe::plan(Path::new("."), "unknown").is_err());
    assert!(source_node_probe::install(Path::new("."), "unknown", &[]).is_err());
    for (v, site) in [(1, 0x4f1c9e), (2, 0x4f274e), (3, 0x4f360e)] {
        assert!(source_node_probe::valid_site(v, site));
        assert!(!source_node_probe::valid_site(v, site + 1));
        assert!(!source_node_probe::valid_site(v, site - 1));
        assert!(!source_node_probe::valid_site(0, site));
        assert!(!source_node_probe::valid_site(4, site));
    }
}

fn selected<'a>(before: &Object<'a>, sites: &[Site], section: usize) -> Vec<(usize, usize)> {
    sites
        .iter()
        .filter_map(|s| {
            let symbol = before
                .symbols
                .values()
                .find(|n| n.name == s.container && n.section == section as i16 + 1)?;
            let start = (symbol.value + s.offset) as usize;
            Some((start, start + s.bytes.len()))
        })
        .collect()
}

#[test]
#[ignore = "requires pinned original split objects in MWCC_ORACLE_TEST_ROOT; read only"]
fn current_source_and_arena_positive_installs_all_versions() {
    let directory = env::var_os("MWCC_ORACLE_TEST_ROOT").unwrap();
    let root = Path::new(&directory);
    for version in ["GC_1_3", "GC_1_3_2", "GC_2_7"] {
        let mut inputs = vec![];
        collect(&root.join(format!("build/{version}/obj")), &mut inputs);
        let source = source_node_probe::install(root, version, &inputs).unwrap();
        let arena = mwcc_oracle_core::source_arena_probe::install(root, version, &inputs).unwrap();
        assert_eq!((source.sites.len(), source.objects.len()), (1, 1));
        assert_eq!((arena.sites.len(), arena.objects.len()), (19, 1));
        println!("{version}: current crossing-start guard permits exact source1/arena19 installs");
    }
}

#[test]
#[ignore = "requires pinned original split objects in MWCC_ORACLE_TEST_ROOT; read only"]
fn source_island_preserves_inputs_and_rejects_complete_prerequisite_tampering() {
    let directory = env::var_os("MWCC_ORACLE_TEST_ROOT").unwrap();
    let root = Path::new(&directory);
    for version in ["GC_1_3", "GC_1_3_2", "GC_2_7"] {
        let mut inputs = vec![];
        collect(&root.join(format!("build/{version}/obj")), &mut inputs);
        let installed = source_node_probe::install(root, version, &inputs).unwrap();
        assert_eq!(installed.guards.len(), 18);
        assert_eq!(installed.sites.len(), 1);
        assert_eq!(installed.objects.len(), 1);
        for (path, changed) in &installed.objects {
            let before = Object::parse(&inputs.iter().find(|(p, _)| p == path).unwrap().1).unwrap();
            let after = Object::parse(changed).unwrap();
            assert_eq!(before.sections.len(), after.sections.len());
            for (si, (a, b)) in before.sections.iter().zip(&after.sections).enumerate() {
                assert_eq!((&a.name, a.flags, a.size), (&b.name, b.flags, b.size));
                assert_eq!(a.data.len(), b.data.len());
                let ranges = selected(&before, &installed.sites, si);
                for (offset, (old, new)) in a.data.iter().zip(b.data.iter()).enumerate() {
                    if !ranges
                        .iter()
                        .any(|&(start, end)| (start..end).contains(&offset))
                    {
                        assert_eq!(old, new, "nonpatch byte changed");
                    }
                }
                for r in &a.relocations {
                    let n = b.relocations.iter().find(|n| n.offset == r.offset).unwrap();
                    assert_eq!(r.kind, n.kind);
                    assert_eq!(identity(&before, r.symbol), identity(&after, n.symbol));
                }
                assert_eq!(b.relocations.len(), a.relocations.len() + ranges.len());
                for &(start, end) in &ranges {
                    assert_eq!(&b.data[start..end], &[0xe8, 0, 0, 0, 0, 0x90]);
                    let r = b
                        .relocations
                        .iter()
                        .find(|r| r.offset == start as u32 + 1)
                        .unwrap();
                    assert_eq!(r.kind, 20);
                    assert!(
                        after.symbols[&r.symbol]
                            .name
                            .starts_with("_OracleRust_SourceIsland_")
                    );
                }
            }
            for s in before.symbols.values() {
                let n = after
                    .symbols
                    .values()
                    .find(|n| {
                        n.name == s.name
                            && n.value == s.value
                            && n.section == s.section
                            && n.storage == s.storage
                    })
                    .unwrap();
                assert_eq!(s.kind, n.kind);
                if !(s.storage == 3
                    && s.section > 0
                    && s.value == 0
                    && s.auxiliary.len() == 1
                    && s.name == before.sections[s.section as usize - 1].name)
                {
                    assert_eq!(s.auxiliary, n.auxiliary);
                }
            }
        }
        let islands = Object::parse(&installed.islands).unwrap();
        let data = islands
            .sections
            .iter()
            .find(|s| s.name == ".rdata")
            .unwrap();
        assert!(data.relocations.is_empty());
        let site = &installed.sites[0];
        assert_eq!(
            data.data
                .chunks_exact(4)
                .map(|v| u32::from_le_bytes(v.try_into().unwrap()))
                .collect::<Vec<_>>(),
            [site.version, site.address, 1, 4]
        );
        let code = &islands
            .sections
            .iter()
            .find(|s| s.name == ".text")
            .unwrap()
            .data;
        assert_eq!(&code[..8], &[0x89, 0x43, 0xa, 0x8b, 0x43, 0xa, 0x9c, 0x60]);
        assert!(code.windows(4).any(|b| b == [0x0f, 0xae, 0x04, 0x24]));
        assert!(code.windows(4).any(|b| b == [0x0f, 0xae, 0x0c, 0x24]));
        assert!(code.ends_with(&[0x89, 0xc4, 0x61, 0x9d, 0xc3]));
        let mut failures = 0;
        for g in &installed.guards {
            let (index, symbol) = inputs
                .iter()
                .enumerate()
                .find_map(|(i, (_, b))| {
                    let o = Object::parse(b).unwrap();
                    Some((
                        i,
                        o.symbols
                            .values()
                            .find(|s| s.section > 0 && s.name == g.name)?
                            .clone(),
                    ))
                })
                .unwrap();
            let object = Object::parse(&inputs[index].1).unwrap();
            let section = &object.sections[symbol.section as usize - 1];
            let mut bad = inputs.clone();
            bad[index].1[section.raw_offset as usize + symbol.value as usize] ^= 1;
            assert!(
                source_node_probe::install(root, version, &bad).is_err(),
                "body {}",
                g.name
            );
            failures += 1;
            // Every switch-table entry is independently corrupted, not just the first.
            let relocs = section
                .relocations
                .iter()
                .enumerate()
                .filter(|(_, r)| (symbol.value..symbol.value + g.size).contains(&r.offset))
                .collect::<Vec<_>>();
            let count = if g.name.starts_with("lbl_") {
                assert_eq!(relocs.len() * 4, g.size as usize);
                relocs.len()
            } else {
                usize::from(!relocs.is_empty())
            };
            for (ri, r) in relocs.into_iter().take(count) {
                let replacement = *object.symbols.keys().find(|id| **id != r.symbol).unwrap();
                let target = &object.symbols[&r.symbol];
                for (at, replacement) in [
                    (
                        section.raw_offset as usize + r.offset as usize,
                        (u32::from_le_bytes(
                            section.data[r.offset as usize..r.offset as usize + 4]
                                .try_into()
                                .unwrap(),
                        ) ^ 1)
                            .to_le_bytes(),
                    ),
                    (
                        section.relocation_offset as usize + ri * 10 + 4,
                        replacement.to_le_bytes(),
                    ),
                    (target.offset + 8, (target.value ^ 1).to_le_bytes()),
                ] {
                    let mut bad = inputs.clone();
                    bad[index].1[at..at + 4].copy_from_slice(&replacement);
                    assert!(
                        source_node_probe::install(root, version, &bad).is_err(),
                        "relocation {}",
                        g.name
                    );
                    failures += 1;
                }
            }
        }
        for (kind, addend) in [(6, 0x238f), (20, 0x2393), (7, 0)] {
            let mut foreign = mwcc_oracle_core::coff::Builder::default();
            let target = foreign.external("_parse_statement").unwrap();
            foreign
                .section(
                    ".data",
                    0xc0300040,
                    (addend as u32).to_le_bytes().to_vec(),
                    4,
                    vec![mwcc_oracle_core::coff::Relocation {
                        offset: 0,
                        symbol: target,
                        kind,
                    }],
                )
                .unwrap();
            let mut bad = inputs.clone();
            bad.push(("foreign.obj".into(), foreign.finish().unwrap()));
            assert!(
                source_node_probe::install(root, version, &bad).is_err(),
                "incoming kind {kind}"
            );
            failures += 1;
        }
        // A private config references the immutable original image by absolute
        // path. Only private metadata is altered; sealed inputs stay read-only.
        let scratch = tempfile::tempdir().unwrap();
        let config_dir = scratch.path().join("config").join(version);
        fs::create_dir_all(&config_dir).unwrap();
        let mut config: serde_json::Value = serde_json::from_slice(
            &fs::read(root.join("config").join(version).join("config.json")).unwrap(),
        )
        .unwrap();
        config["original"] = root
            .join(config["original"].as_str().unwrap())
            .canonicalize()
            .unwrap()
            .to_string_lossy()
            .into_owned()
            .into();
        fs::write(
            config_dir.join("config.json"),
            serde_json::to_vec(&config).unwrap(),
        )
        .unwrap();
        let metadata =
            fs::read_to_string(root.join("config").join(version).join("symbols.txt")).unwrap();
        fs::write(config_dir.join("symbols.txt"), &metadata).unwrap();
        source_node_probe::plan(scratch.path(), version).unwrap();
        for name in ["_cscope_currentfunc", "_evalMode", "_alloc_state"] {
            for alter_size in [false, true] {
                let changed = metadata
                    .lines()
                    .map(|line| {
                        if !line.starts_with(&format!("{name} = ")) {
                            return line.to_string();
                        }
                        if alter_size {
                            let at = line.find("size:0x").unwrap() + 7;
                            let end = line[at..]
                                .find(char::is_whitespace)
                                .map_or(line.len(), |n| at + n);
                            format!("{}ff{}", &line[..at], &line[end..])
                        } else {
                            let at = line.find(":0x").unwrap() + 3;
                            format!("{}00000000{}", &line[..at], &line[at + 8..])
                        }
                    })
                    .collect::<Vec<_>>()
                    .join("\n");
                fs::write(config_dir.join("symbols.txt"), changed).unwrap();
                assert!(
                    source_node_probe::plan(scratch.path(), version).is_err(),
                    "global {name} metadata tampering"
                );
                failures += 1;
            }
        }
        println!(
            "{version}: 12 bodies + 6 tables; one committed-store island; {failures} rejected mutations; nonpatch bytes and identities preserved"
        );
    }
}
