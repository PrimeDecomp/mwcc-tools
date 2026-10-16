use mwcc_oracle_core::{coff::Object, executable, investigate, pe::Pe};
use serde_json::{Value, json};
use sha2::{Digest, Sha256};
use std::{fs, path::PathBuf};
fn fixtures() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/investigate")
}
#[test]
fn masked_searches_match_python() {
    let root = fixtures();
    let cases: Vec<Value> =
        serde_json::from_slice(&fs::read(root.join("find.json")).unwrap()).unwrap();
    for (index, case) in cases.iter().enumerate() {
        let blob: Vec<u8> = serde_json::from_value(case["blob"].clone()).unwrap();
        let code: Vec<u8> = serde_json::from_value(case["code"].clone()).unwrap();
        let wild = serde_json::from_value(case["wild"].clone()).unwrap();
        assert_eq!(
            json!(investigate::find(
                &blob,
                &code,
                &wild,
                case["limit"].as_u64().unwrap() as usize
            )),
            case["matches"],
            "case {index}"
        );
    }
}
#[test]
fn investigation_workflows_match_python() {
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
            hash.as_str().unwrap()
        );
    }
    let metadata: Value =
        serde_json::from_slice(&fs::read(fixtures.join("project.json")).unwrap()).unwrap();
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    for (version, file, key) in [
        ("fixtureA", "from.pe", "functions_from"),
        ("fixtureB", "to.pe", "functions_to"),
    ] {
        fs::create_dir_all(root.join(format!("build/compilers/GC/{version}"))).unwrap();
        fs::copy(
            fixtures.join(file),
            root.join(format!("build/compilers/GC/{version}/mwcceppc.exe")),
        )
        .unwrap();
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
        fs::write(
            config.join("functions.json"),
            serde_json::to_vec(&metadata[key]).unwrap(),
        )
        .unwrap();
        fs::write(
            config.join("bindings.json"),
            serde_json::to_vec(&metadata["bindings"]).unwrap(),
        )
        .unwrap();
        fs::write(
            config.join("splits.txt"),
            metadata["splits"].as_str().unwrap(),
        )
        .unwrap();
    }
    let mut sources = vec![];
    for (source, file) in metadata["objects"].as_object().unwrap() {
        sources.push(source.clone());
        let data = fs::read(fixtures.join(file.as_str().unwrap())).unwrap();
        assert_eq!(
            json!(investigate::functions_of(&Object::parse(&data).unwrap()).unwrap()),
            metadata["functions"][source]
        );
        let path = root.join(format!("build/GC_fixtureA/compiled/{source}.obj"));
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, data).unwrap();
        assert_eq!(
            json!(investigate::bindings_from(root, "fixtureA", source).unwrap()),
            metadata["inferred"][source]
        );
    }
    assert_eq!(
        investigate::locate(root, "fixtureA", &sources).unwrap(),
        metadata["locate"].as_str().unwrap()
    );
    assert_eq!(
        investigate::bindfrom(root, "fixtureA").unwrap(),
        metadata["bindfrom"].as_str().unwrap()
    );
    let bound: Value =
        serde_json::from_slice(&fs::read(root.join("config/GC_fixtureA/bindings.json")).unwrap())
            .unwrap();
    assert_eq!(bound, metadata["bound"]);
    let image = fs::read(fixtures.join("from.pe")).unwrap();
    let pe = Pe::parse(&image).unwrap();
    let fixups = executable::base_relocations(&pe).unwrap();
    for case in metadata["masked"].as_array().unwrap() {
        assert_eq!(
            json!(
                investigate::masked(
                    &pe,
                    &fixups,
                    case["address"].as_u64().unwrap() as u32,
                    case["size"].as_u64().unwrap() as usize
                )
                .unwrap()
            ),
            case["bytes"]
        );
    }
    assert_eq!(
        json!(investigate::funcmatch(root, "fixtureA", "fixtureB").unwrap()),
        metadata["matched"]
    );
}
