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
        put(
            root.path(),
            &format!("config/{version}/names.txt"),
            b"names\n",
        );
    }
    for target in release::TARGETS {
        notices(
            root.path(),
            &format!("licenses/dependencies/{target}"),
            target,
        );
    }
    root
}

fn notices(root: &Path, directory: &str, target: &str) {
    put(
        root,
        &format!("{directory}/notice.txt"),
        b"actual license bytes\n",
    );
    put(root, &format!("{directory}/inventory.json"), &serde_json::to_vec(&json!({
        "format_version": 1, "target": target,
        "cargo_lock_sha256": release::sha256(b"fixture\n"),
        "rustc": ["test toolchain"], "files": {"notice.txt": release::sha256(b"actual license bytes\n")}
    })).unwrap());
}

fn elf() -> Vec<u8> {
    let mut bytes = vec![0; 20];
    bytes[..6].copy_from_slice(b"\x7fELF\x02\x01");
    bytes[18] = 62;
    bytes
}

fn macho(target: &str) -> Vec<u8> {
    let mut bytes = vec![0; 60];
    bytes[..4].copy_from_slice(b"\xcf\xfa\xed\xfe");
    let cpu: u32 = if target == "aarch64-apple-darwin" {
        0x0100_000c
    } else {
        0x0100_0007
    };
    bytes[4..8].copy_from_slice(&cpu.to_le_bytes());
    let subtype: u32 = if target == "aarch64-apple-darwin" {
        0
    } else {
        3
    };
    bytes[8..12].copy_from_slice(&subtype.to_le_bytes());
    bytes[12..16].copy_from_slice(&2u32.to_le_bytes()); // MH_EXECUTE
    bytes[16..20].copy_from_slice(&1u32.to_le_bytes());
    bytes[20..24].copy_from_slice(&24u32.to_le_bytes());
    bytes[32..36].copy_from_slice(&0x8000_0028u32.to_le_bytes()); // LC_MAIN
    bytes[36..40].copy_from_slice(&24u32.to_le_bytes());
    bytes[40..48].copy_from_slice(&56u64.to_le_bytes());
    bytes[56..60].copy_from_slice(&0xd65f_03c0u32.to_le_bytes());
    bytes
}

#[test]
fn both_macos_architectures_package_reproducibly_with_canonical_paths() {
    let root = fixture();
    for target in ["aarch64-apple-darwin", "x86_64-apple-darwin"] {
        assert!(release::TARGETS.contains(&target));
        let binary = macho(target);
        let first = release::assemble(root.path(), &binary, target, None).unwrap();
        assert_eq!(
            first,
            release::assemble(root.path(), &binary, target, None).unwrap()
        );
        let mut archive = zip::ZipArchive::new(Cursor::new(first)).unwrap();
        let metadata: serde_json::Value =
            serde_json::from_reader(archive.by_name("release.json").unwrap()).unwrap();
        assert_eq!(metadata["target"], target);
        assert_eq!(metadata["executable"], "bin/mwcc-tools");
        assert_eq!(metadata["executable_sha256"], release::sha256(&binary));
        assert!(
            archive
                .by_name(&format!("licenses/dependencies/{target}/inventory.json"))
                .is_ok()
        );
        let mut packaged = vec![];
        let mut executable = archive.by_name("bin/mwcc-tools").unwrap();
        assert_eq!(executable.unix_mode().unwrap() & 0o777, 0o755);
        executable.read_to_end(&mut packaged).unwrap();
        assert_eq!(packaged, binary);
    }
}

#[test]
fn macos_rejects_wrong_architecture_nonexecutables_fat_swapped_and_truncated_headers() {
    let root = fixture();
    for target in ["aarch64-apple-darwin", "x86_64-apple-darwin"] {
        let valid = macho(target);
        let other = if target == "aarch64-apple-darwin" {
            "x86_64-apple-darwin"
        } else {
            "aarch64-apple-darwin"
        };
        let mut invalid = vec![elf(), macho(other)];
        for magic in [
            b"\xfe\xed\xfa\xcf",
            b"\xce\xfa\xed\xfe",
            b"\xca\xfe\xba\xbe",
            b"\xbe\xba\xfe\xca",
            b"\xca\xfe\xba\xbf",
            b"\xbf\xba\xfe\xca",
        ] {
            let mut bytes = valid.clone();
            bytes[..4].copy_from_slice(magic);
            invalid.push(bytes);
        }
        for (offset, value) in [
            (4, 7),
            (12, 1),
            (12, 6),
            (28, 1),
            (16, 0),
            (16, 2),
            (16, u32::MAX),
            (20, 0),
            (20, 16),
            (20, 25),
            (20, u32::MAX),
            (36, 0),
            (36, 4),
            (36, 12),
            (36, 32),
        ] {
            let mut bytes = valid.clone();
            bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
            invalid.push(bytes);
        }
        for length in [0, 4, 20, 31, 32, 39, 55] {
            invalid.push(valid[..length].to_vec());
        }
        for bytes in invalid {
            let error = release::assemble(root.path(), &bytes, target, None).unwrap_err();
            assert!(error.to_string().contains("Mach-O"), "{target}: {error:#}");
        }
    }
}

#[test]
fn generated_notice_root_preserves_validation_and_previous_outputs() {
    let root = fixture();
    let generated = tempfile::tempdir().unwrap();
    let target = "aarch64-apple-darwin";
    notices(generated.path(), "host-specific", target);
    let notice_dir = generated.path().join("host-specific");
    let binary = macho(target);
    let canonical = release::assemble(root.path(), &binary, target, None).unwrap();
    assert_eq!(
        canonical,
        release::assemble_with_notices(root.path(), &binary, target, None, &notice_dir).unwrap()
    );
    let output = root.path().join("release.zip");
    let executable = root.path().join("mwcc-tools");
    fs::write(&executable, &binary).unwrap();
    release::write_with_notices(root.path(), &executable, target, None, &output, &notice_dir)
        .unwrap();
    assert_eq!(fs::read(&output).unwrap(), canonical);
    let checksum = fs::read(output.with_extension("zip.sha256")).unwrap();
    let inventory_path = notice_dir.join("inventory.json");
    let inventory = fs::read(&inventory_path).unwrap();
    for (field, value) in [
        ("target", json!("x86_64-apple-darwin")),
        ("cargo_lock_sha256", json!("stale")),
        ("files", json!({})),
        ("files", json!({"notice.txt":"incorrect hash"})),
    ] {
        let mut changed: serde_json::Value = serde_json::from_slice(&inventory).unwrap();
        changed[field] = value;
        fs::write(&inventory_path, serde_json::to_vec(&changed).unwrap()).unwrap();
        assert!(
            release::write_with_notices(
                root.path(),
                &executable,
                target,
                None,
                &output,
                &notice_dir
            )
            .is_err()
        );
        assert_eq!(fs::read(&output).unwrap(), canonical);
        assert_eq!(
            fs::read(output.with_extension("zip.sha256")).unwrap(),
            checksum
        );
    }
    fs::write(&inventory_path, &inventory).unwrap();
    put(&notice_dir, "unlisted.txt", b"extra");
    assert!(
        release::assemble_with_notices(root.path(), &binary, target, None, &notice_dir).is_err()
    );
    fs::remove_file(notice_dir.join("unlisted.txt")).unwrap();
    put(&notice_dir, "notice.txt", b"actual license bytes\n");
    fs::remove_file(&inventory_path).unwrap();
    let error =
        release::write_with_notices(root.path(), &executable, target, None, &output, &notice_dir)
            .unwrap_err();
    assert!(error.to_string().contains("inventory.json is missing"));
    assert_eq!(fs::read(&output).unwrap(), canonical);
    assert_eq!(
        fs::read(output.with_extension("zip.sha256")).unwrap(),
        checksum
    );
    let empty = generated.path().join("empty");
    fs::create_dir(&empty).unwrap();
    assert!(
        release::assemble_with_notices(root.path(), &binary, target, None, &empty)
            .unwrap_err()
            .to_string()
            .contains("inventory.json is missing")
    );
    fs::write(&inventory_path, &inventory).unwrap();
    put(&notice_dir, "notice.txt", b"changed contents");
    assert!(
        release::assemble_with_notices(root.path(), &binary, target, None, &notice_dir)
            .unwrap_err()
            .to_string()
            .contains("notice hash mismatch")
    );
}

#[cfg(unix)]
#[test]
fn generated_notice_symlinks_are_rejected() {
    let root = fixture();
    let generated = tempfile::tempdir().unwrap();
    let target = "x86_64-apple-darwin";
    notices(generated.path(), "notices", target);
    std::os::unix::fs::symlink(
        generated.path().join("notices"),
        generated.path().join("alias"),
    )
    .unwrap();
    for alias in ["alias", "alias/", "alias/."] {
        assert!(
            release::assemble_with_notices(
                root.path(),
                &macho(target),
                target,
                None,
                &generated.path().join(alias)
            )
            .is_err(),
            "symlink root {alias} accepted"
        );
    }
    fs::remove_file(generated.path().join("notices/notice.txt")).unwrap();
    put(
        generated.path(),
        "real-notice.txt",
        b"actual license bytes\n",
    );
    std::os::unix::fs::symlink(
        generated.path().join("real-notice.txt"),
        generated.path().join("notices/notice.txt"),
    )
    .unwrap();
    assert!(
        release::assemble_with_notices(
            root.path(),
            &macho(target),
            target,
            None,
            &generated.path().join("notices")
        )
        .is_err()
    );
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
