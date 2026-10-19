use mwcc_oracle_core::{derive_splits, executable, pe::Pe, remap_binary};
use serde_json::{Value, json};
use sha2::{Digest, Sha256};
use std::{fs, path::PathBuf};
fn fixtures() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/metadata")
}
fn stage() -> (tempfile::TempDir, Value) {
    let fixtures = fixtures();
    let metadata: Value =
        serde_json::from_slice(&fs::read(fixtures.join("project.json")).unwrap()).unwrap();
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    for (version, file, key) in [
        ("fixtureA", "from.pe", "functions_from"),
        ("fixtureB", "to.pe", "functions_to"),
    ] {
        let path = root.join(format!("build/compilers/GC/{version}/mwcceppc.exe"));
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::copy(fixtures.join(file), path).unwrap();
        let config = root.join(format!("config/GC_{version}"));
        fs::create_dir_all(&config).unwrap();
        fs::write(
            config.join("config.json"),
            serde_json::to_vec(
                &json!({"original":format!("build/compilers/GC/{version}/mwcceppc.exe")}),
            )
            .unwrap(),
        )
        .unwrap();
        for (name, key) in [("functions", key), ("bindings", "bindings")] {
            fs::write(
                config.join(format!("{name}.json")),
                serde_json::to_vec(&metadata[key]).unwrap(),
            )
            .unwrap();
        }
        fs::write(
            config.join("splits.txt"),
            metadata["reference_splits"].as_str().unwrap(),
        )
        .unwrap();
        fs::write(
            config.join("symbols.txt"),
            metadata["reference_symbols"].as_str().unwrap(),
        )
        .unwrap();
        for (source, file) in metadata["objects"].as_object().unwrap() {
            let path = root.join(format!("build/GC_{version}/compiled/{source}.obj"));
            fs::create_dir_all(path.parent().unwrap()).unwrap();
            fs::copy(fixtures.join(file.as_str().unwrap()), path).unwrap();
        }
    }
    fs::write(
        root.join("config/lmgr326b.def"),
        "LIBRARY lmgr326b.dll\nEXPORTS\n    license @1 NONAME\n",
    )
    .unwrap();
    fs::create_dir_all(root.join("lib/win32sdk")).unwrap();
    fs::write(
        root.join("lib/win32sdk/KERNEL32.LIB"),
        b"synthetic __imp__ExitProcess@4 import decoration",
    )
    .unwrap();
    (directory, metadata)
}
#[test]
fn monotone_assignment_matches_reference_ties() {
    let cases: Vec<Value> =
        serde_json::from_slice(&fs::read(fixtures().join("assignment.json")).unwrap()).unwrap();
    for (i, case) in cases.iter().enumerate() {
        let costs: Vec<Vec<f64>> = serde_json::from_value(case["costs"].clone()).unwrap();
        assert_eq!(
            json!(
                derive_splits::monotone_assignment(
                    &costs,
                    case["units"].as_u64().unwrap() as usize
                )
                .unwrap()
            ),
            case["owners"],
            "case {i}"
        );
    }
    assert!(derive_splits::monotone_assignment(&[vec![]], 0).is_err());
}
#[test]
fn complete_derivation_preserves_imports_common_and_runtime_layout() {
    let fixtures = fixtures();
    let manifest: Value =
        serde_json::from_slice(&fs::read(fixtures.join("manifest.json")).unwrap()).unwrap();
    assert_eq!(
        manifest["reference_commit"],
        "b75c4e5e0b4bc97ae06c26ea9d6aad8379ac1722"
    );
    for (name, hash) in manifest["files"].as_object().unwrap() {
        assert_eq!(
            format!(
                "{:x}",
                Sha256::digest(fs::read(fixtures.join(name)).unwrap())
            ),
            hash.as_str().unwrap(),
            "{name}"
        );
    }
    let (directory, metadata) = stage();
    let root = directory.path();
    let actual = derive_splits::derive(root, "fixtureB", "fixtureA", None).unwrap();
    assert_eq!(json!(actual), metadata["derived"]);
    let (definitions, decorated) = derive_splits::compiled_symbols(root, "fixtureB").unwrap();
    assert_eq!(
        json!({"definitions":definitions,"decorated":decorated}),
        metadata["compiled_symbols"]
    );
    let output = root.join("derived");
    derive_splits::run(root, "fixtureB", "fixtureA", None, Some(&output)).unwrap();
    assert_eq!(
        fs::read_to_string(output.join("splits.txt"))
            .unwrap()
            .replace("\r\n", "\n"),
        metadata["derived"]["splits"].as_str().unwrap()
    );
    assert_eq!(
        fs::read_to_string(output.join("symbols.txt"))
            .unwrap()
            .replace("\r\n", "\n"),
        metadata["derived"]["symbols"]
            .as_array()
            .unwrap()
            .iter()
            .map(|s| s.as_str().unwrap())
            .collect::<Vec<_>>()
            .join("\n")
            + "\n"
    );
}
#[test]
fn remapping_preserves_conflicts_carry_and_generated_names() {
    let patterns: Vec<Value> =
        serde_json::from_slice(&fs::read(fixtures().join("patterns.json")).unwrap()).unwrap();
    for (i, case) in patterns.iter().enumerate() {
        let blob: Vec<u8> = serde_json::from_value(case["blob"].clone()).unwrap();
        let code: Vec<u8> = serde_json::from_value(case["code"].clone()).unwrap();
        let wild = serde_json::from_value(case["wild"].clone()).unwrap();
        assert_eq!(
            json!(remap_binary::pattern_matches(
                &blob,
                &code,
                &wild,
                case["low"].as_u64().unwrap() as usize,
                case["high"].as_u64().unwrap() as usize
            )),
            case["matches"],
            "pattern {i}"
        );
    }
    let (directory, metadata) = stage();
    let root = directory.path();
    let data_a = fs::read(fixtures().join("from.pe")).unwrap();
    let data_b = fs::read(fixtures().join("to.pe")).unwrap();
    let a = Pe::parse(&data_a).unwrap();
    let b = Pe::parse(&data_b).unwrap();
    let fixups = executable::base_relocations(&a).unwrap();
    let mut known = indexmap::IndexMap::from([(0x402000, 0x412000), (0x402010, 0x412010)]);
    assert_eq!(
        remap_binary::carry(0x402008, &known, &a).unwrap(),
        Some(0x412008)
    );
    assert_eq!(remap_binary::carry(0x401fff, &known, &a).unwrap(), None);
    assert_eq!(remap_binary::carry(0x402011, &known, &a).unwrap(), None);
    known.insert(0x402010, 0x412018);
    assert_eq!(remap_binary::carry(0x402008, &known, &a).unwrap(), None);
    let cross_section = indexmap::IndexMap::from([(0x402000, 0x412000), (0x404000, 0x414000)]);
    assert_eq!(
        remap_binary::carry(0x402080, &cross_section, &a).unwrap(),
        None
    );
    let functions: Vec<Value> = serde_json::from_value(metadata["functions_from"].clone()).unwrap();
    let result = remap_binary::match_functions(&functions, &a, &fixups, &b).unwrap();
    assert_eq!(json!(result.found), metadata["remapped"]["found"]);
    assert_eq!(json!(result.missing), metadata["remapped"]["missing"]);
    let pairs = remap_binary::data_pairs(&functions, &result.found, &a, &fixups, &b).unwrap();
    assert_eq!(json!(pairs), metadata["remapped"]["pairs"]);
    for case in metadata["remapped"]["wildcards"].as_array().unwrap() {
        let (code, wild) = remap_binary::wildcards(
            &a,
            &fixups,
            case["address"].as_u64().unwrap() as u32,
            case["size"].as_u64().unwrap() as usize,
        )
        .unwrap();
        assert_eq!(json!(code), case["code"]);
        assert_eq!(json!(wild), case["wild"]);
    }
    for (symbols, key) in [(false, "without_symbols"), (true, "with_symbols")] {
        assert_eq!(
            remap_binary::run(root, "fixtureA", "fixtureB", symbols).unwrap(),
            metadata[key]["report"].as_str().unwrap()
        );
        for name in ["functions", "bindings"] {
            let actual: Value = serde_json::from_slice(
                &fs::read(root.join(format!("config/GC_fixtureB/{name}.json"))).unwrap(),
            )
            .unwrap();
            assert_eq!(actual, metadata[key][name]);
        }
    }
}
