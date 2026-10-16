use mwcc_oracle_core::{build_config, release};
use serde_json::json;
use std::{
    fs,
    io::{Cursor, Read},
    path::Path,
};

fn put(root: &Path, path: &str, bytes: &[u8]) {
    let path = root.join(path);
    fs::create_dir_all(path.parent().unwrap()).unwrap();
    fs::write(path, bytes).unwrap();
}

fn fixture() -> tempfile::TempDir {
    let root = tempfile::tempdir().unwrap();
    for path in [
        "Cargo.toml",
        "Cargo.lock",
        "rust-toolchain.toml",
        "crates/core/src/lib.rs",
        "include/compiler/PCode.h",
        "include/compiler/enode.h",
        "LICENSE",
        "licenses/README.md",
        "licenses/capstone-rs-MIT.txt",
        "licenses/capstone-sys-MIT.txt",
        "licenses/capstone-5.0.9-LICENSE.txt",
        "licenses/capstone-5.0.9-LICENSE_LLVM.txt",
        "integrations/decomp-kits/overlay/.agents/docs/oracle-native.md",
        "docs/oracle-rust-release.md",
        "crates/mwcc-oracle-hooks/README.md",
    ] {
        put(root.path(), path, b"fixture\n");
    }
    for version in build_config::VERSIONS {
        put(
            root.path(),
            &format!("config/{version}/config.json"),
            br#"{"sha1":"0123456789012345678901234567890123456789"}"#,
        );
    }
    for version in ["GC_1_3", "GC_1_3_2", "GC_2_7"] {
        put(
            root.path(),
            &format!("config/{version}/symbols.txt"),
            b"symbols\n",
        );
    }
    let notices = "licenses/dependencies/x86_64-unknown-linux-gnu";
    put(
        root.path(),
        &format!("{notices}/notice.txt"),
        b"actual license bytes\n",
    );
    put(root.path(), &format!("{notices}/inventory.json"), &serde_json::to_vec(&json!({
        "format_version": 1, "target": "x86_64-unknown-linux-gnu",
        "cargo_lock_sha256": release::sha256(b"fixture\n"),
        "rustc": ["test toolchain"], "files": {"notice.txt": release::sha256(b"actual license bytes\n")}
    })).unwrap());
    root
}

fn elf() -> Vec<u8> {
    let mut bytes = vec![0; 20];
    bytes[..6].copy_from_slice(b"\x7fELF\x02\x01");
    bytes[18] = 62;
    bytes
}

#[test]
fn archive_is_reproducible_content_addressed_and_executable() {
    let root = fixture();
    let first = release::assemble(root.path(), &elf(), "x86_64-unknown-linux-gnu", None).unwrap();
    let second = release::assemble(root.path(), &elf(), "x86_64-unknown-linux-gnu", None).unwrap();
    assert_eq!(first, second);
    let mut archive = zip::ZipArchive::new(Cursor::new(&first)).unwrap();
    let mut text = String::new();
    archive
        .by_name("release.json")
        .unwrap()
        .read_to_string(&mut text)
        .unwrap();
    let meta: serde_json::Value = serde_json::from_str(&text).unwrap();
    assert_eq!(meta["protocols"]["oracle_report"], 6);
    assert_eq!(meta["executable_sha256"], release::sha256(&elf()));
    assert!(meta["source_revision"].is_null());
    assert_eq!(
        archive
            .by_name("bin/mwcc-tools")
            .unwrap()
            .unix_mode()
            .unwrap()
            & 0o777,
        0o755
    );
    put(
        root.path(),
        "crates/core/src/lib.rs",
        b"changed implementation\n",
    );
    let changed = release::assemble(root.path(), &elf(), "x86_64-unknown-linux-gnu", None).unwrap();
    assert_ne!(release::sha256(&first), release::sha256(&changed));
}

#[test]
fn invalid_inputs_do_not_replace_a_previous_archive() {
    let root = fixture();
    let output = root.path().join("release.zip");
    let binary = root.path().join("tools");
    fs::write(&output, b"previous release").unwrap();
    fs::write(&binary, elf()).unwrap();
    let notice = "licenses/dependencies/x86_64-unknown-linux-gnu/notice.txt";
    put(root.path(), notice, b"altered notice\n");
    assert!(
        release::write(
            root.path(),
            &binary,
            "x86_64-unknown-linux-gnu",
            None,
            &output
        )
        .is_err()
    );
    assert_eq!(fs::read(&output).unwrap(), b"previous release");
    put(root.path(), notice, b"actual license bytes\n");
    assert!(release::assemble(root.path(), &elf(), "x86_64-pc-windows-msvc", None).is_err());
    assert!(
        release::assemble(
            root.path(),
            &elf(),
            "x86_64-unknown-linux-gnu",
            Some("short-id")
        )
        .is_err()
    );
    put(root.path(), "Cargo.lock", b"changed lock\n");
    assert!(release::assemble(root.path(), &elf(), "x86_64-unknown-linux-gnu", None).is_err());
}
