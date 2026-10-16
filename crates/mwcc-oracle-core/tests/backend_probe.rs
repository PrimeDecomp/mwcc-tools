use mwcc_oracle_core::{backend_probe, coff::Object};
use std::{env, fs, path::PathBuf};

#[test]
fn coalescer_references_must_target_the_entry() {
    use mwcc_oracle_core::coff::{Builder, Relocation};
    let make = |addend: u32, kind: u16| {
        let mut b = Builder::default();
        let f = b.external(backend_probe::FUNCTION).unwrap();
        b.section(
            ".text",
            0x60500020,
            addend.to_le_bytes().to_vec(),
            4,
            vec![Relocation {
                offset: 0,
                symbol: f,
                kind,
            }],
        )
        .unwrap();
        b.finish().unwrap()
    };
    backend_probe::validate_incoming(&make(0, 20)).unwrap();
    backend_probe::validate_incoming(&make(0, 6)).unwrap();
    assert!(backend_probe::validate_incoming(&make(1, 20)).is_err());
    assert!(backend_probe::validate_incoming(&make(0, 7)).is_err());
}

#[test]
#[ignore = "requires split retail objects in MWCC_ORACLE_TEST_ROOT"]
fn retail_probe_rewrite_preserves_other_sections_and_refuses_byte_or_layout_drift() {
    let root =
        PathBuf::from(env::var_os("MWCC_ORACLE_TEST_ROOT").expect("set MWCC_ORACLE_TEST_ROOT"));
    for version in ["GC_1_3", "GC_1_3_2", "GC_2_7"] {
        let input = fs::read(root.join(format!(
            "build/{version}/obj/src/backend/InterferenceGraph.c.obj"
        )))
        .unwrap();
        let original = Object::parse(&input).unwrap();
        // Include callers in other units, not just the rewritten object.
        fn walk(path: &std::path::Path) {
            for entry in fs::read_dir(path).unwrap() {
                let path = entry.unwrap().path();
                if path.is_dir() {
                    walk(&path);
                } else if path.extension().is_some_and(|v| v == "obj") {
                    backend_probe::validate_incoming(&fs::read(path).unwrap()).unwrap();
                }
            }
        }
        walk(&root.join(format!("build/{version}/obj")));
        let function = original
            .symbols
            .values()
            .find(|s| s.name == backend_probe::FUNCTION)
            .unwrap();
        let section = function.section as usize - 1;
        let output = backend_probe::instrument(&input).unwrap();
        assert_eq!(
            output,
            fs::read(root.join(format!(
                "build/{version}/oracle/probes/InterferenceGraph.c.obj"
            )))
            .unwrap(),
            "current native rewrite differs from the object in the retained runtime proof"
        );
        let instrumented = Object::parse(&output).unwrap();
        assert_eq!(instrumented.sections.len(), original.sections.len() + 1);
        for (index, s) in original.sections.iter().enumerate() {
            if index == section {
                continue;
            }
            let new = &instrumented.sections[index];
            assert_eq!(s.name, new.name);
            assert_eq!(s.flags, new.flags);
            assert_eq!(s.data, new.data);
            let describe = |object: &Object<'_>, rs: &[mwcc_oracle_core::coff::Relocation]| {
                rs.iter()
                    .map(|r| (r.offset, r.kind, object.symbols[&r.symbol].name.clone()))
                    .collect::<Vec<_>>()
            };
            assert_eq!(
                describe(&original, &s.relocations),
                describe(&instrumented, &new.relocations)
            );
        }
        let rewritten = &instrumented.sections[section];
        assert!(rewritten.data.len() > original.sections[section].data.len());
        let probes = rewritten
            .relocations
            .iter()
            .filter(|r| instrumented.symbols[&r.symbol].name == "_OracleRust_BackendProbe")
            .count();
        assert_eq!(probes, backend_probe::SITES.len());
        assert_eq!(instrumented.sections.last().unwrap().relocations.len(), 6);
        let mut drift = input.clone();
        drift[original.sections[section].raw_offset as usize] ^= 1;
        assert!(backend_probe::instrument(&drift).is_err());
        let mut layout = input.clone();
        // Remove one relocation from the section header without changing bytes.
        let count = 20 + section * 40 + 32;
        layout[count..count + 2].copy_from_slice(&0u16.to_le_bytes());
        assert!(backend_probe::instrument(&layout).is_err());
    }
}
