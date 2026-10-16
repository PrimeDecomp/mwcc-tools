use mwcc_oracle_core::{call_argument_probe, coff::Object};
use std::{env, fs, path::Path};
fn collect(root: &Path, objects: &mut Vec<(String, Vec<u8>)>) {
    for item in fs::read_dir(root).unwrap() {
        let path = item.unwrap().path();
        if path.is_dir() {
            collect(&path, objects)
        } else if path.extension().is_some_and(|v| v == "obj") {
            objects.push((path.to_string_lossy().into(), fs::read(path).unwrap()))
        }
    }
}
#[test]
fn unsupported_version_is_rejected_without_inputs() {
    assert!(call_argument_probe::install(Path::new("."), "unknown", &[]).is_err());
}
#[test]
#[ignore = "requires pinned split originals in MWCC_ORACLE_TEST_ROOT; read only"]
fn guarded_argument_retarget_preserves_other_bytes_and_rejects_corruption() {
    let root = env::var_os("MWCC_ORACLE_TEST_ROOT").unwrap();
    let root = Path::new(&root);
    for version in ["GC_1_3", "GC_1_3_2", "GC_2_7"] {
        let mut inputs = vec![];
        collect(&root.join(format!("build/{version}/obj")), &mut inputs);
        let installed = call_argument_probe::install(root, version, &inputs).unwrap();
        assert_eq!(installed.objects.len(), 1);
        assert_eq!(
            installed.guards.len(),
            if version == "GC_1_3" { 9 } else { 8 }
        );
        let (path, changed) = installed.objects.iter().next().unwrap();
        let before = Object::parse(&inputs.iter().find(|(p, _)| p == path).unwrap().1).unwrap();
        let after = Object::parse(changed).unwrap();
        let helper = before
            .symbols
            .values()
            .find(|s| s.section > 0 && s.name == "_load_argument_registers")
            .unwrap();
        let si = helper.section as usize - 1;
        let at = helper.value as usize + 0x2ee;
        for (index, (a, b)) in before.sections.iter().zip(&after.sections).enumerate() {
            assert_eq!((&a.name, a.size, a.flags), (&b.name, b.size, b.flags));
            assert_eq!(a.data.len(), b.data.len());
            for (n, (x, y)) in a.data.iter().zip(b.data.iter()).enumerate() {
                if index != si || !(at..at + 4).contains(&n) {
                    assert_eq!(x, y)
                }
            }
            for r in &a.relocations {
                let other = b.relocations.iter().find(|q| q.offset == r.offset).unwrap();
                assert_eq!(r.kind, other.kind);
                let old = &before.symbols[&r.symbol];
                let new = &after.symbols[&other.symbol];
                if index == si && r.offset as usize == at {
                    assert_eq!(new.name, call_argument_probe::FORCE_HANDLER)
                } else {
                    assert_eq!(
                        (&old.name, old.section, old.value),
                        (&new.name, new.section, new.value)
                    );
                }
            }
        }
        for s in before.symbols.values().filter(|s| s.storage == 2) {
            let n = after.symbols.values().find(|n| n.name == s.name).unwrap();
            assert_eq!((s.section, s.value, s.kind), (n.section, n.value, n.kind));
        }
        let island = Object::parse(&installed.islands).unwrap();
        assert!(
            island
                .symbols
                .values()
                .any(|s| s.section > 0 && s.name == call_argument_probe::FORCE_RETAIL)
        );
        let mut failures = 0;
        for guard in &installed.guards {
            let (index, symbol, si) = inputs
                .iter()
                .enumerate()
                .find_map(|(i, (_, b))| {
                    let o = Object::parse(b).unwrap();
                    let s = o
                        .symbols
                        .values()
                        .find(|s| s.section > 0 && s.name == guard.name)?
                        .clone();
                    Some((i, s.clone(), s.section as usize - 1))
                })
                .unwrap();
            let o = Object::parse(&inputs[index].1).unwrap();
            let section = &o.sections[si];
            let start = section.raw_offset as usize + symbol.value as usize;
            let mut bad = inputs.clone();
            bad[index].1[start] ^= 1;
            assert!(call_argument_probe::install(root, version, &bad).is_err());
            failures += 1;
            if let Some((ri, r)) = section
                .relocations
                .iter()
                .enumerate()
                .find(|(_, r)| (symbol.value..symbol.value + guard.size).contains(&r.offset))
            {
                let target = &o.symbols[&r.symbol];
                let replacement = *o.symbols.keys().find(|id| **id != r.symbol).unwrap();
                for (position, bytes) in [
                    (
                        section.relocation_offset as usize + ri * 10 + 4,
                        replacement.to_le_bytes(),
                    ),
                    (
                        section.raw_offset as usize + r.offset as usize,
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
                    bad[index].1[position..position + 4].copy_from_slice(&bytes);
                    assert!(
                        call_argument_probe::install(root, version, &bad).is_err(),
                        "{version} {} mutation admitted",
                        guard.name
                    );
                    failures += 1;
                }
            }
        }
        println!(
            "{version}: {} guarded bodies; {failures} rejected mutations; one Force relocation changed",
            installed.guards.len()
        );
    }
}
