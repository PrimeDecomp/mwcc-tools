use mwcc_oracle_core::{
    coff::{self, Relocation, Section, Symbol},
    producer,
};
use std::{fs, path::Path, time::Duration};
fn object(exports: &[&str], imports: &[&str], relocation: u16) -> Vec<u8> {
    let symbols: Vec<_> = exports
        .iter()
        .map(|name| Symbol {
            name: (*name).into(),
            value: 0,
            section: 1,
            storage: 2,
        })
        .chain(imports.iter().map(|name| Symbol {
            name: (*name).into(),
            value: 0,
            section: 0,
            storage: 2,
        }))
        .collect();
    coff::write(
        &[Section {
            name: ".text".into(),
            flags: 0x60500020,
            data: vec![0; 4],
            relocations: vec![Relocation {
                offset: 0,
                symbol: exports.len() as u32,
                kind: relocation,
            }],
        }],
        &symbols,
    )
    .unwrap()
}
#[test]
fn producer_surface_rejects_missing_hooks_runtime_helpers_and_unsupported_relocations() {
    let exports: Vec<_> = producer::HOOKS
        .into_iter()
        .chain(producer::SUPPORT)
        .collect();
    let valid = object(&exports, &producer::IMPORTS, 0x14);
    producer::validate(&valid).unwrap();
    assert!(producer::validate(&valid[..20]).is_err());
    assert!(producer::validate(&object(&producer::HOOKS, &producer::IMPORTS, 0x14)).is_err());
    assert!(producer::validate(&object(&exports[..2], &producer::IMPORTS, 0x14)).is_err());
    let mut imports = producer::IMPORTS.to_vec();
    imports.push("_rust_begin_unwind");
    assert!(producer::validate(&object(&exports, &imports, 0x14)).is_err());
    assert!(producer::validate(&object(&exports, &producer::IMPORTS, 0x999)).is_err());
    let trace = object(&producer::TRACE_EXPORTS, &producer::TRACE_IMPORTS, 0x14);
    producer::validate_trace(&trace).unwrap();
    for version in ["GC_1_3", "GC_1_3_2", "GC_2_7"] {
        let exports = producer::frontend_hooks(version).unwrap();
        let imports = producer::frontend_imports(version).unwrap();
        let imported: Vec<_> = imports.iter().map(String::as_str).collect();
        let frontend = object(&exports, &imported, 0x14);
        producer::validate_frontend(&frontend, version).unwrap();
        assert!(producer::validate(&frontend).is_err());
        assert!(producer::validate_frontend(&frontend, "GC_1_2_5").is_err());
        assert!(
            producer::validate_frontend(&object(&exports[..4], &imported, 0x14), version).is_err()
        );
        assert!(
            producer::validate_frontend(&object(&exports, &imported[..19], 0x14), version).is_err()
        );
        assert!(producer::validate_frontend(&object(&exports, &imported, 0x999), version).is_err());
        let other = if version == "GC_1_3" {
            "GC_2_7"
        } else {
            "GC_1_3"
        };
        assert!(producer::validate_frontend(&frontend, other).is_err());
    }
    assert!(producer::validate(&trace).is_err());
    assert!(producer::validate_trace(&valid).is_err());
    assert!(
        producer::validate_trace(&object(
            &producer::TRACE_EXPORTS[..1],
            &producer::TRACE_IMPORTS,
            0x14
        ))
        .is_err()
    );
    assert!(
        producer::validate_trace(&object(
            &producer::TRACE_EXPORTS,
            &producer::TRACE_IMPORTS[..4],
            0x14
        ))
        .is_err()
    );
    assert!(
        producer::validate_trace(&object(
            &producer::TRACE_EXPORTS,
            &producer::TRACE_IMPORTS,
            0x999
        ))
        .is_err()
    );
}
#[test]
fn failed_producer_compilation_preserves_the_previous_artifact() {
    let root = tempfile::tempdir().unwrap();
    fs::create_dir_all(root.path().join("crates/mwcc-oracle-hooks/src")).unwrap();
    fs::write(
        root.path().join("crates/mwcc-oracle-hooks/src/lib.rs"),
        "#![no_std]\n",
    )
    .unwrap();
    fs::write(root.path().join("out.obj"), b"previous checked object").unwrap();
    assert!(
        producer::build(
            root.path(),
            Path::new("out.obj"),
            &root.path().join("missing-rustc"),
            Duration::from_secs(1)
        )
        .is_err()
    );
    assert_eq!(
        fs::read(root.path().join("out.obj")).unwrap(),
        b"previous checked object"
    );
}
