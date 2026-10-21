mod common;
use mwcc_oracle_core::{coff, pe};
use serde_json::{Value, json};
use std::{fs, path::PathBuf};
fn fixtures() -> PathBuf {
    common::fixtures("binary")
}
#[test]
fn pe_addresses_resources_and_search_match_python() {
    let root = fixtures();
    let data = fs::read(root.join("synthetic.pe")).unwrap();
    let pe = pe::Pe::parse(&data).unwrap();
    let e: Value = serde_json::from_slice(&fs::read(root.join("pe.json")).unwrap()).unwrap();
    assert_eq!(json!(pe.image_base), e["image_base"]);
    assert_eq!(json!(pe.sections), e["sections"]);
    for r in e["reads"].as_array().unwrap() {
        let address = r["address"].as_u64().unwrap() as u32;
        let size = r["size"].as_u64().unwrap() as usize;
        let data = pe.read(address, size).unwrap();
        assert_eq!(json!(data), r["bytes"]);
        assert!(pe.contains(address, data));
        assert_eq!(
            pe.offset_to_address(pe.address_to_offset(address).unwrap())
                .unwrap(),
            address
        );
    }
    for f in e["finds"].as_array().unwrap() {
        let bytes: Vec<u8> = serde_json::from_value(f["bytes"].clone()).unwrap();
        assert_eq!(
            json!(pe.find(&bytes, f["limit"].as_u64().map(|n| n as usize))),
            f["addresses"]
        );
    }
    assert!(pe.address_to_offset(0x402250).is_err());
    assert!(!pe.contains(0x402250, &[0]));
    assert!(pe.offset_to_address(0x800).is_err());
    let entries =
        pe::resources(&pe, pe.sections.iter().find(|s| s.name == ".rsrc").unwrap()).unwrap();
    assert_eq!(json!(entries), e["resources"]);
    assert_eq!(
        pe::res_file(&entries).unwrap(),
        fs::read(root.join("synthetic.res")).unwrap()
    );
}
#[test]
fn coff_and_import_archives_are_byte_exact() {
    let root = fixtures();
    let input: Value =
        serde_json::from_slice(&fs::read(root.join("coff-input.json")).unwrap()).unwrap();
    let sections = serde_json::from_value::<Vec<coff::Section>>(input["sections"].clone()).unwrap();
    let symbols = serde_json::from_value::<Vec<coff::Symbol>>(input["symbols"].clone()).unwrap();
    assert_eq!(
        coff::write(&sections, &symbols).unwrap(),
        fs::read(root.join("synthetic.obj")).unwrap()
    );
    let libraries: Vec<Value> =
        serde_json::from_slice(&fs::read(root.join("libraries.json")).unwrap()).unwrap();
    for l in libraries {
        let name = l["name"].as_str().unwrap();
        let (dll, exports) =
            coff::definition(&fs::read_to_string(root.join(format!("{name}.def"))).unwrap())
                .unwrap();
        assert_eq!(json!(dll), l["dll"]);
        assert_eq!(json!(exports), l["exports"]);
        assert_eq!(
            coff::import_library(&dll, &exports).unwrap(),
            fs::read(root.join(format!("{name}.lib"))).unwrap()
        );
    }
}
#[test]
fn malformed_pe_resources_return_errors() {
    let root = fixtures();
    let data = fs::read(root.join("synthetic.pe")).unwrap();
    for end in [0, 63, 128, 0x17f, 0x7ff] {
        assert!(pe::Pe::parse(&data[..end]).is_err());
    }
    let mut cyclic = data;
    cyclic[0x614..0x618].copy_from_slice(&0x80000000u32.to_le_bytes());
    let pe = pe::Pe::parse(&cyclic).unwrap();
    assert!(pe::resources(&pe, &pe.sections[2]).is_err());
}

#[test]
fn auxiliary_coff_building_and_hooks_match_python() {
    use std::collections::BTreeSet;
    let root = fixtures();
    let e: Value = serde_json::from_slice(&fs::read(root.join("auxiliary.json")).unwrap()).unwrap();
    let mut b = coff::Builder::default();
    b.section(".text", 0x60500020, vec![0xe8, 0, 0, 0, 0, 0xc3], 6, vec![])
        .unwrap();
    let entry = b.add_symbol("_entry", 0, 1, 0x20, 2, &[]).unwrap();
    let external = b.external("_external_with_a_long_name").unwrap();
    b.sections[0].relocations = vec![coff::Relocation {
        offset: 1,
        symbol: entry,
        kind: 20,
    }];
    b.section(".bss", 0xc0300080, vec![], 16, vec![]).unwrap();
    b.section(
        ".data",
        0xc0300040,
        vec![4, 0, 0, 0, 0, 0, 0, 0],
        8,
        vec![
            coff::Relocation {
                offset: 0,
                symbol: entry,
                kind: 6,
            },
            coff::Relocation {
                offset: 4,
                symbol: entry,
                kind: 6,
            },
        ],
    )
    .unwrap();
    b.section(
        ".debug$long_section",
        0x42000040,
        b"debug".to_vec(),
        5,
        vec![],
    )
    .unwrap();
    let mut aux = entry.to_le_bytes().to_vec();
    aux.extend(3u32.to_le_bytes());
    aux.extend([0; 10]);
    let weak = b.add_symbol("_weak", 0, 0, 0, 105, &aux).unwrap();
    assert_eq!(
        json!({"entry":entry,"external":external,"weak":weak}),
        e["indices"]
    );
    let bytes = b.finish().unwrap();
    assert_eq!(bytes, fs::read(root.join("auxiliary.obj")).unwrap());
    let parsed = coff::Object::parse(&bytes).unwrap();
    let symbols:serde_json::Map<_,_>=parsed.symbols.iter().map(|(i,s)|(i.to_string(),json!({"name":s.name,"value":s.value,"section":s.section,"type":s.kind,"storage":s.storage}))).collect();
    assert_eq!(json!(symbols), e["symbols"]);
    assert_eq!(parsed.sections[3].name, ".debug$long_section");
    assert_eq!(parsed.symbols[&weak].auxiliary.len(), 1);
    assert_eq!(parsed.sections[1].data.as_ref(), &[0; 16]);
    assert_eq!(json!(coff::defined_symbols(&bytes).unwrap()), e["defined"]);
    let names = BTreeSet::from(["_entry".into()]);
    assert_eq!(
        coff::undefine_symbols(&bytes, &names).unwrap(),
        fs::read(root.join("undefined.obj")).unwrap()
    );
    assert_eq!(
        coff::hook_symbols(&bytes, &names).unwrap(),
        fs::read(root.join("hooked.obj")).unwrap()
    );
    let hooked = fs::read(root.join("hooked.obj")).unwrap();
    let parsed = coff::Object::parse(&hooked).unwrap();
    assert!(parsed.symbols.values().any(|s| s.name == "_entry__retail"));
    assert_eq!(parsed.sections[2].relocations[0].symbol, entry);
    assert_ne!(parsed.sections[2].relocations[1].symbol, entry);
}

#[test]
fn x86_lengths_and_build_metadata_match_python() {
    use mwcc_oracle_core::{executable, x86};
    let root = fixtures();
    let cases: Vec<Value> =
        serde_json::from_slice(&fs::read(root.join("branches.json")).unwrap()).unwrap();
    for (i, e) in cases.iter().enumerate() {
        let code: Vec<u8> = serde_json::from_value(e["code"].clone()).unwrap();
        assert_eq!(
            json!(x86::branches(&code, e["base"].as_u64().unwrap() as u32).unwrap()),
            e["branches"],
            "case {i}"
        );
    }
    for code in [&[0x66][..], &[0x0f], &[0xe8, 0, 0], &[0xff]] {
        assert!(x86::branches(code, 0x401000).is_err());
    }
    let e: Value = serde_json::from_slice(&fs::read(root.join("metadata.json")).unwrap()).unwrap();
    assert_eq!(
        json!(
            executable::read_symbols(&fs::read_to_string(root.join("symbols.txt")).unwrap())
                .unwrap()
        ),
        e["symbols"]
    );
    assert_eq!(
        json!(
            executable::read_splits(&fs::read_to_string(root.join("splits.txt")).unwrap()).unwrap()
        ),
        e["splits"]
    );
    assert_eq!(
        json!(executable::common_names(2).unwrap()),
        e["common_names"]
    );
    assert_eq!(
        json!(
            executable::common_layout(
                &[
                    ("_one".into(), 1),
                    ("_word".into(), 4),
                    ("_double".into(), 8),
                    ("_odd".into(), 3)
                ],
                0x403001
            )
            .unwrap()
        ),
        e["common_layout"]
    );
    assert!(executable::read_symbols("not a symbol").is_err());
    assert!(executable::read_splits("src/unit.c\n").is_err());
}
