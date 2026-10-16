use mwcc_oracle_core::{coff::Object, compare, pe::Pe};
use serde_json::{Value, json};
use sha2::{Digest, Sha256};
use std::{fs, path::PathBuf};
fn fixtures() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/compare")
}
#[test]
fn reference_inputs_have_pinned_provenance() {
    let root = fixtures();
    let manifest: Value =
        serde_json::from_slice(&fs::read(root.join("manifest.json")).unwrap()).unwrap();
    assert_eq!(
        manifest["reference_commit"],
        "b75c4e5e0b4bc97ae06c26ea9d6aad8379ac1722"
    );
    for (name, hash) in manifest["files"].as_object().unwrap() {
        assert_eq!(
            format!("{:x}", Sha256::digest(fs::read(root.join(name)).unwrap())),
            hash.as_str().unwrap(),
            "{name}"
        );
    }
}
#[test]
fn relocation_and_literal_resolution_matches_python() {
    let root = fixtures();
    let cases: Vec<Value> =
        serde_json::from_slice(&fs::read(root.join("resolutions.json")).unwrap()).unwrap();
    for case in cases {
        let name = case["name"].as_str().unwrap();
        let object = fs::read(root.join(format!("{name}.obj"))).unwrap();
        let image = fs::read(root.join(format!("{name}.pe"))).unwrap();
        let pe = Pe::parse(&image).unwrap();
        let object = Object::parse(&object).unwrap();
        let addresses = serde_json::from_value(case["addresses"].clone()).unwrap();
        let result = compare::resolve_function(
            &object,
            case["symbol"].as_str().unwrap(),
            case["target_address"].as_u64().unwrap() as u32,
            &addresses,
            &pe,
            case["target_size"].as_u64().map(|s| s as usize),
        );
        if let Some(error) = case["error"].as_str() {
            assert_eq!(result.unwrap_err().to_string(), error, "{name}");
        } else {
            let (body, resolutions) = result.unwrap_or_else(|e| panic!("{name}: {e:#}"));
            assert_eq!(json!(body), case["body"], "{name}");
            assert_eq!(json!(resolutions), case["resolutions"], "{name}");
        }
    }
}
#[test]
fn extraction_objects_match_python() {
    let root = fixtures();
    let cases: Vec<Value> =
        serde_json::from_slice(&fs::read(root.join("single-coff.json")).unwrap()).unwrap();
    for case in cases {
        let data: Vec<u8> = serde_json::from_value(case["data"].clone()).unwrap();
        assert_eq!(
            compare::single_coff(
                &data,
                case["symbol"].as_str(),
                case["code"].as_bool().unwrap(),
                case["bss_size"].as_u64().unwrap() as u32
            )
            .unwrap(),
            fs::read(root.join(case["file"].as_str().unwrap())).unwrap()
        );
    }
}

fn platform_path(path: &str) -> String {
    if cfg!(windows) {
        path.into()
    } else {
        [
            ("%3c", "<"),
            ("%3e", ">"),
            ("%3a", ":"),
            ("%22", "\""),
            ("%7c", "|"),
            ("%3f", "?"),
            ("%2a", "*"),
        ]
        .into_iter()
        .fold(path.to_owned(), |p, (a, b)| p.replace(a, b))
    }
}
fn project() -> (tempfile::TempDir, Value) {
    let fixtures = fixtures();
    let metadata: Value =
        serde_json::from_slice(&fs::read(fixtures.join("project.json")).unwrap()).unwrap();
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    fs::create_dir_all(root.join("config/FIXTURE")).unwrap();
    for name in ["config", "functions", "bindings"] {
        fs::write(
            root.join(format!("config/FIXTURE/{name}.json")),
            serde_json::to_vec(&metadata[name]).unwrap(),
        )
        .unwrap();
    }
    fs::write(
        root.join("config/sources.json"),
        serde_json::to_vec(&metadata["sources"]).unwrap(),
    )
    .unwrap();
    fs::copy(fixtures.join("project.pe"), root.join("original.exe")).unwrap();
    fs::create_dir_all(root.join("build/FIXTURE/compiled/src")).unwrap();
    for label in ["matched", "nonmatching"] {
        fs::copy(
            fixtures.join(format!("project-{label}.obj")),
            root.join(format!("build/FIXTURE/compiled/src/{label}.cpp.obj")),
        )
        .unwrap();
    }
    (directory, metadata)
}
#[test]
fn complete_comparison_preserves_symbolic_fixups_and_patch_policy() {
    let (directory, metadata) = project();
    let root = directory.path();
    let inventory = compare::inventory(root, "FIXTURE").unwrap();
    let mut expected = metadata["rows"].clone();
    for row in expected.as_array_mut().unwrap() {
        for key in ["target", "base"] {
            if let Some(path) = row[key].as_str() {
                row[key] = json!(platform_path(path));
            }
        }
    }
    assert_eq!(json!(inventory.rows), expected);
    let checks = compare::check(root, "FIXTURE", &inventory).unwrap();
    for (name, expected) in metadata["checks"].as_object().unwrap() {
        let result = &checks[name];
        assert_eq!(json!(result.body), expected["body"], "{name}");
        assert_eq!(json!(result.exact), expected["exact"], "{name}");
    }
    // Both have identical instruction bytes; one lacks a required symbolic fixup,
    // the other invents one where the original has a numeric immediate.
    for name in ["src/nonmatching/b", "src/nonmatching/c"] {
        assert!(checks[name].body.is_some());
        assert!(!checks[name].exact);
        let row = inventory.rows.iter().find(|r| r["name"] == name).unwrap();
        let address = row["address"].as_u64().unwrap() as u32;
        assert_eq!(
            checks[name].body.as_deref().unwrap(),
            inventory.original.pe().unwrap().read(address, 5).unwrap()
        );
    }
    compare::extract(root, "FIXTURE").unwrap();
    let result = compare::compare(root, "FIXTURE").unwrap();
    assert!(!result.failed);
    assert_eq!(
        result.summary + "\n",
        metadata["completion"].as_str().unwrap()
    );
    let mut expected = metadata["objdiff"].clone();
    for unit in expected["units"].as_array_mut().unwrap() {
        for key in ["target_path", "base_path"] {
            if let Some(path) = unit[key].as_str() {
                unit[key] = json!(platform_path(path));
            }
        }
    }
    let actual: Value =
        serde_json::from_slice(&fs::read(root.join("objdiff.json")).unwrap()).unwrap();
    assert_eq!(actual, expected);
    for (path, hash) in metadata["objects"].as_object().unwrap() {
        assert_eq!(
            format!(
                "{:x}",
                Sha256::digest(fs::read(root.join(platform_path(path))).unwrap())
            ),
            hash.as_str().unwrap(),
            "{path}"
        );
    }
    let mut report = metadata["report_input"].clone();
    compare::adjust_report(&mut report).unwrap();
    assert_eq!(report, metadata["report_output"]);
}
#[test]
fn matching_failure_does_not_write_a_success_marker() {
    let (directory, metadata) = project();
    let root = directory.path();
    let mut config = metadata["config"].clone();
    config["matching"] = json!(["src/matched.cpp", "src/nonmatching.cpp"]);
    fs::write(
        root.join("config/FIXTURE/config.json"),
        serde_json::to_vec(&config).unwrap(),
    )
    .unwrap();
    let result = compare::compare(root, "FIXTURE").unwrap();
    assert!(result.failed);
    assert!(result.summary.contains("  not exact: src/nonmatching/b"));
    assert!(root.join("objdiff.json").is_file());
    assert!(!root.join("build/FIXTURE/ok").exists());
}
