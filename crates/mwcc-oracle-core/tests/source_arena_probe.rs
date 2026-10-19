use mwcc_oracle_core::{
    coff::Object,
    source_arena_probe::{self, Site},
};
use std::{env, fs, path::Path};

fn collect(root: &Path, objects: &mut Vec<(String, Vec<u8>)>) {
    for entry in fs::read_dir(root).unwrap() {
        let path = entry.unwrap().path();
        if path.is_dir() {
            collect(&path, objects)
        } else if path.extension().is_some_and(|s| s == "obj") {
            objects.push((path.to_string_lossy().into(), fs::read(path).unwrap()));
        }
    }
}
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
fn unsupported_version_and_canonical_coordinates_fail_closed() {
    assert!(source_arena_probe::plan(Path::new("."), "unknown").is_err());
    assert!(source_arena_probe::install(Path::new("."), "unknown", &[]).is_err());
    assert!(source_arena_probe::valid_site(3, 1, false, 0x440d65));
    assert!(source_arena_probe::valid_site(3, 1, true, 0x440d80));
    assert!(source_arena_probe::valid_site(3, 5, false, 0x4414d1));
    assert!(source_arena_probe::valid_site(3, 9, false, 0x4417c9));
    assert!(source_arena_probe::valid_callback_site(3, 0x44113a));
    for (version, pair, commit, site) in [
        (0, 1, false, 0x440d65),
        (3, 10, false, 0x440d65),
        (3, 1, true, 0x440d65),
        (3, 1, false, 0x440d66),
        (1, 1, false, 0x440d65),
    ] {
        assert!(!source_arena_probe::valid_site(version, pair, commit, site));
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
fn guarded_arena_islands_preserve_inputs_and_reject_body_target_addend_and_incoming_tampering() {
    let directory = env::var_os("MWCC_ORACLE_TEST_ROOT").unwrap();
    let root = Path::new(&directory);
    for version in ["GC_1_3", "GC_1_3_2", "GC_2_7"] {
        let mut inputs = vec![];
        collect(&root.join(format!("build/{version}/obj")), &mut inputs);
        let installed = source_arena_probe::install(root, version, &inputs).unwrap();
        assert_eq!(
            installed.guards.len(),
            if version == "GC_2_7" { 23 } else { 22 }
        );
        assert_eq!(installed.sites.len(), 19);
        assert_eq!(installed.objects.len(), 1);
        assert_eq!(installed.sites.iter().filter(|s| s.action == 1).count(), 9);
        assert_eq!(installed.sites.iter().filter(|s| s.action == 2).count(), 9);
        assert_eq!(installed.sites.iter().filter(|s| s.action == 3).count(), 1);
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
                    if !ranges
                        .iter()
                        .any(|&(start, end)| (start..end).contains(&(r.offset as usize)))
                    {
                        let n = b.relocations.iter().find(|n| n.offset == r.offset).unwrap();
                        assert_eq!(r.kind, n.kind);
                        assert_eq!(identity(&before, r.symbol), identity(&after, n.symbol));
                    }
                }
                for &(start, end) in &ranges {
                    assert_eq!(b.data[start], 0xe8);
                    assert!(b.data[start + 5..end].iter().all(|v| *v == 0x90));
                    let r = b
                        .relocations
                        .iter()
                        .filter(|r| (start..end).contains(&(r.offset as usize)))
                        .collect::<Vec<_>>();
                    assert_eq!(r.len(), 1);
                    assert_eq!((r[0].offset, r[0].kind), (start as u32 + 1, 20));
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
                    && s.value == 0
                    && s.auxiliary.len() == 1
                    && s.section > 0
                    && s.name == before.sections[s.section as usize - 1].name)
                {
                    assert_eq!(s.auxiliary, n.auxiliary);
                }
            }
        }
        let islands = Object::parse(&installed.islands).unwrap();
        let globals = islands
            .symbols
            .values()
            .find(|s| s.name == source_arena_probe::GLOBALS)
            .unwrap();
        let data = &islands.sections[globals.section as usize - 1];
        assert_eq!(
            &data.data[8..12],
            &match version {
                "GC_1_3" => 1u32,
                "GC_1_3_2" => 2,
                _ => 3,
            }
            .to_le_bytes()
        );
        for (offset, name) in [(0, "_heap_pool"), (4, "_data_0057fdd8")] {
            let r = data
                .relocations
                .iter()
                .find(|r| r.offset == offset)
                .unwrap();
            assert_eq!(r.kind, 6);
            assert_eq!(islands.symbols[&r.symbol].name, name);
        }
        for site in &installed.sites {
            let descriptor = islands
                .symbols
                .values()
                .find(|s| s.name == format!("_OracleRust_ArenaDescriptor_{:08x}", site.address))
                .unwrap();
            let at = descriptor.value as usize;
            let fields = data.data[at..at + 36]
                .chunks_exact(4)
                .map(|v| u32::from_le_bytes(v.try_into().unwrap()))
                .collect::<Vec<_>>();
            assert_eq!(
                fields,
                [
                    0,
                    site.address,
                    site.action,
                    site.pair,
                    site.pool,
                    site.size_kind,
                    site.size_value,
                    site.result_kind,
                    site.result_value
                ]
            );
            let island = islands
                .symbols
                .values()
                .find(|s| s.name == format!("_OracleRust_ArenaIsland_{:08x}", site.address))
                .unwrap();
            let code = &islands.sections[island.section as usize - 1];
            let from = island.value as usize;
            // Commit store sequence precedes PUSHFD/PUSHAD; request/gap observer precedes retail work.
            if site.action == 2 {
                assert_eq!(&code.data[from..from + 2], &site.bytes[..2]);
                assert_eq!(
                    &code.data[from + site.bytes.len()..from + site.bytes.len() + 2],
                    &[0x9c, 0x60]
                );
            } else {
                assert_eq!(&code.data[from..from + 2], &[0x9c, 0x60]);
            }
            if site.action == 3 {
                let next = islands
                    .symbols
                    .values()
                    .filter(|s| {
                        s.section == island.section && s.storage == 2 && s.value > island.value
                    })
                    .map(|s| s.value as usize)
                    .min()
                    .unwrap_or(code.data.len());
                let bytes = &code.data[from..next];
                assert!(bytes.windows(4).any(|b| b == [0x8d, 0x64, 0x24, 4]));
                assert_eq!(bytes[bytes.len() - 5], 0xe9);
            }
        }
        let mut failures = 0;
        for g in &installed.guards {
            let (index, symbol, si) = inputs
                .iter()
                .enumerate()
                .find_map(|(i, (_, bytes))| {
                    let o = Object::parse(bytes).unwrap();
                    let s = o
                        .symbols
                        .values()
                        .find(|s| s.section > 0 && s.name == g.name)?
                        .clone();
                    Some((i, s.clone(), s.section as usize - 1))
                })
                .unwrap();
            let object = Object::parse(&inputs[index].1).unwrap();
            let section = &object.sections[si];
            let mut bad = inputs.clone();
            bad[index].1[section.raw_offset as usize + symbol.value as usize] ^= 1;
            assert!(source_arena_probe::install(root, version, &bad).is_err());
            failures += 1;
            if let Some((ri, r)) = section
                .relocations
                .iter()
                .enumerate()
                .find(|(_, r)| (symbol.value..symbol.value + g.size).contains(&r.offset))
            {
                let target = &object.symbols[&r.symbol];
                let replacement = *object.symbols.keys().find(|id| **id != r.symbol).unwrap();
                let at = section.raw_offset as usize + r.offset as usize;
                for (offset, value) in [
                    (
                        section.relocation_offset as usize + ri * 10 + 4,
                        replacement.to_le_bytes(),
                    ),
                    (
                        at,
                        (u32::from_le_bytes(
                            section.data[r.offset as usize..r.offset as usize + 4]
                                .try_into()
                                .unwrap(),
                        ) ^ 1)
                            .to_le_bytes(),
                    ),
                    (target.offset + 8, (target.value ^ 1).to_le_bytes()),
                ] {
                    let mut bad = inputs.clone();
                    bad[index].1[offset..offset + 4].copy_from_slice(&value);
                    assert!(
                        source_arena_probe::install(root, version, &bad).is_err(),
                        "{} mutation admitted",
                        g.name
                    );
                    failures += 1;
                }
            }
        }
        // A new foreign object may refer to a public helper entry, but not its stolen interior.
        let g = installed
            .guards
            .iter()
            .find(|g| g.name == "_galloc")
            .unwrap();
        let mut foreign = mwcc_oracle_core::coff::Builder::default();
        let target = foreign.external("_galloc").unwrap();
        foreign
            .section(
                ".data",
                0xc0300040,
                (0x21u32).to_le_bytes().to_vec(),
                4,
                vec![mwcc_oracle_core::coff::Relocation {
                    offset: 0,
                    symbol: target,
                    kind: 6,
                }],
            )
            .unwrap();
        let mut bad = inputs.clone();
        bad.push(("foreign.obj".into(), foreign.finish().unwrap()));
        assert!(
            source_arena_probe::install(root, version, &bad).is_err(),
            "foreign {}+0x21 admitted",
            g.name
        );
        failures += 1;
        println!(
            "{version}: {} guarded bodies, 19 islands; {failures} rejected mutations; original nonpatch bytes/identities preserved",
            installed.guards.len()
        );
    }
}
