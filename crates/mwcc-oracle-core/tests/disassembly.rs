use mwcc_oracle_core::disassembly;
use serde_json::{Value, json};
use sha2::{Digest, Sha256};
use std::{fs, path::PathBuf};
fn fixtures() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/disassembly")
}
#[test]
fn decoder_text_boundaries_and_shapes_match_python() {
    let root = fixtures();
    let decoder = disassembly::decoder().unwrap();
    let cases: Vec<Value> =
        serde_json::from_slice(&fs::read(root.join("instructions.json")).unwrap()).unwrap();
    for (index, case) in cases.iter().enumerate() {
        let code: Vec<u8> = serde_json::from_value(case["code"].clone()).unwrap();
        let wild = serde_json::from_value(case["wild"].clone()).unwrap();
        assert_eq!(
            json!(
                disassembly::decode(&decoder, &code, case["address"].as_u64().unwrap(), &wild)
                    .unwrap()
            ),
            case["instructions"],
            "case {index}"
        );
    }
    let cases: Vec<Value> =
        serde_json::from_slice(&fs::read(root.join("shapes.json")).unwrap()).unwrap();
    for case in cases {
        assert_eq!(
            disassembly::shape(
                case["text"].as_str().unwrap(),
                case["field"].as_bool().unwrap()
            ),
            case["shape"].as_str().unwrap()
        );
    }
}
#[test]
fn disassembly_workflows_match_legacy_reports() {
    let fixtures = fixtures();
    let manifest: Value =
        serde_json::from_slice(&fs::read(fixtures.join("manifest.json")).unwrap()).unwrap();
    assert_eq!(
        manifest["reference_commit"],
        "b75c4e5e0b4bc97ae06c26ea9d6aad8379ac1722"
    );
    assert_eq!(manifest["capstone_package"], "5.0.9");
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
    let metadata: Value =
        serde_json::from_slice(&fs::read(fixtures.join("project.json")).unwrap()).unwrap();
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    fs::create_dir_all(root.join("config/GC_fixtureA")).unwrap();
    fs::write(
        root.join("config/GC_fixtureA/config.json"),
        b"{\"original\":\"original.exe\"}",
    )
    .unwrap();
    fs::write(
        root.join("config/GC_fixtureA/symbols.txt"),
        metadata["symbols"].as_str().unwrap(),
    )
    .unwrap();
    fs::copy(fixtures.join("original.pe"), root.join("original.exe")).unwrap();
    for (source, file) in metadata["objects"].as_object().unwrap() {
        let path = root.join(format!("build/GC_fixtureA/compiled/{source}.obj"));
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::copy(fixtures.join(file.as_str().unwrap()), path).unwrap();
    }
    for case in metadata["commands"].as_array().unwrap() {
        let args: Vec<String> = serde_json::from_value(case["arguments"].clone()).unwrap();
        let hex = |s: &str| u32::from_str_radix(s.strip_prefix("0x").unwrap_or(s), 16).unwrap();
        let actual = match case["command"].as_str().unwrap() {
            "disasm" => disassembly::disasm(
                root,
                &args[0],
                hex(&args[1]),
                args.get(2).map(|s| hex(s) as usize),
            ),
            "fdiff" => disassembly::fdiff(root, &args[0], &args[1], &args[2], hex(&args[3])),
            _ => disassembly::nearest(root, &args[0], &args[1..]),
        }
        .unwrap();
        assert_eq!(
            actual,
            case["output"].as_str().unwrap(),
            "{} {:?}",
            case["command"],
            args
        );
    }
}
