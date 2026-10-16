use mwcc_oracle_core::{coff::Object, dispatch_audit, dispatch_probe};
use std::{env, fs, path::PathBuf};

#[test]
#[ignore = "requires MWCC_DISPATCH_TEST_ROOT with all three pinned split retail objects"]
fn retail_dispatch_islands_preserve_original_sections_and_reject_changed_inputs() {
    let root =
        PathBuf::from(env::var_os("MWCC_DISPATCH_TEST_ROOT").expect("set MWCC_DISPATCH_TEST_ROOT"));
    for version in ["GC_1_3", "GC_1_3_2", "GC_2_7"] {
        let input = fs::read(root.join(format!(
            "build/{version}/obj/src/backend/InstrSelection.c.obj"
        )))
        .unwrap();
        let installed = dispatch_probe::instrument(&root, version, &input).unwrap();
        assert_eq!(installed.audit.initialized_entries, 78);
        assert_eq!(installed.audit.initialization.writes.len(), 43);
        assert!(!installed.audit.coverage_complete);
        let old = Object::parse(&input).unwrap();
        let new = Object::parse(&installed.object).unwrap();
        assert_eq!(new.sections.len(), old.sections.len() + 2);
        let function = old
            .symbols
            .values()
            .find(|s| {
                s.name == dispatch_audit::INITIALIZER
                    && s.section > 0
                    && old.sections[s.section as usize - 1]
                        .data
                        .starts_with(&[0x30, 0xc9, 0x80, 0xf9, 0x4d])
            })
            .unwrap();
        let section = function.section as usize - 1;
        let fields = installed
            .audit
            .initialization
            .writes
            .iter()
            .map(|w| w.offset as usize + if w.slot.is_none() { 7 } else { 6 })
            .collect::<Vec<_>>();
        for (index, (a, b)) in old.sections.iter().zip(&new.sections).enumerate() {
            assert_eq!((&a.name, a.size, a.flags), (&b.name, b.size, b.flags));
            assert_eq!(a.relocations.len(), b.relocations.len());
            for (offset, (a, b)) in a.data.iter().zip(b.data.iter()).enumerate() {
                if index != section || !fields.iter().any(|f| (*f..*f + 4).contains(&offset)) {
                    assert_eq!(a, b);
                }
            }
            for (a, b) in a.relocations.iter().zip(&b.relocations) {
                assert_eq!((a.offset, a.kind), (b.offset, b.kind));
                if index != section || !fields.contains(&(a.offset as usize)) {
                    assert_eq!(old.symbols[&a.symbol].name, new.symbols[&b.symbol].name);
                } else {
                    assert!(
                        new.symbols[&b.symbol]
                            .name
                            .starts_with("_OracleExpressionEntry")
                    );
                }
            }
        }
        // Both a loop-bound change and a changed pointer addend must fail.
        let pointer = old.sections[section].raw_offset as usize;
        for offset in [4, 17] {
            let mut changed = input.clone();
            changed[pointer + offset] ^= 1;
            assert!(dispatch_probe::instrument(&root, version, &changed).is_err());
        }
        if version == "GC_2_7" {
            let write = installed
                .audit
                .initialization
                .writes
                .iter()
                .find(|w| w.slot == Some(4))
                .unwrap();
            let position = old.sections[section]
                .relocations
                .iter()
                .position(|r| r.offset == write.offset + 6)
                .unwrap();
            let initializer = old
                .symbols
                .iter()
                .find(|(_, s)| {
                    s.name == dispatch_audit::INITIALIZER && s.section == function.section
                })
                .unwrap()
                .0;
            let mut changed = input.clone();
            let at = old.sections[section].relocation_offset as usize + position * 10 + 4;
            changed[at..at + 4].copy_from_slice(&initializer.to_le_bytes());
            assert!(
                dispatch_probe::instrument(&root, version, &changed).is_err(),
                "same-named local functions must not substitute for each other"
            );
        }
        println!(
            "{version}: initializer, pointer targets, original sections and rejection guards pass"
        );
    }
}
