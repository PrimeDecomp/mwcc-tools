//! Reproducible distributions of an already-built native executable.
//!
//! The source catalogue identifies the supplied checkout. It does not assert
//! that an arbitrary executable was built from that checkout.
use anyhow::{Context, Result, ensure};
use serde_json::{Value, json};
use sha2::{Digest, Sha256};
use std::{collections::BTreeMap, fs, io::Write, path::Path};
use zip::{CompressionMethod, ZipWriter, write::SimpleFileOptions};

pub const TARGETS: [&str; 2] = ["x86_64-pc-windows-msvc", "x86_64-unknown-linux-gnu"];

pub fn sha256(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}

fn collect(root: &Path, relative: &str, files: &mut BTreeMap<String, Vec<u8>>) -> Result<()> {
    let path = root.join(relative);
    let metadata = fs::symlink_metadata(&path).with_context(|| path.display().to_string())?;
    ensure!(
        !metadata.file_type().is_symlink(),
        "symlink in release input: {relative}"
    );
    if metadata.is_dir() {
        for child in fs::read_dir(path)? {
            let name = child?
                .file_name()
                .into_string()
                .map_err(|_| anyhow::anyhow!("non-UTF-8 release input"))?;
            collect(root, &format!("{relative}/{name}"), files)?;
        }
    } else {
        ensure!(
            metadata.is_file(),
            "not a regular release input: {relative}"
        );
        files.insert(relative.into(), fs::read(path)?);
    }
    Ok(())
}

/// Validate notices and build an archive in memory before replacing any output.
pub fn assemble(
    root: &Path,
    binary: &[u8],
    target: &str,
    revision: Option<&str>,
) -> Result<Vec<u8>> {
    ensure!(
        TARGETS.contains(&target),
        "unsupported release target {target}"
    );
    if let Some(revision) = revision {
        ensure!(
            revision.len() == 40 && revision.bytes().all(|c| c.is_ascii_hexdigit()),
            "source revision must be a complete Git commit ID"
        );
    }
    validate_binary(binary, target)?;
    let mut source = BTreeMap::new();
    for path in [
        "Cargo.toml",
        "Cargo.lock",
        "rust-toolchain.toml",
        "crates",
        "include/compiler/PCode.h",
        "include/compiler/enode.h",
    ] {
        collect(root, path, &mut source)?;
    }
    let mut originals = BTreeMap::new();
    for version in crate::build_config::VERSIONS {
        let path = format!("config/{version}/config.json");
        collect(root, &path, &mut source)?;
        let config: Value = serde_json::from_slice(&source[&path])?;
        originals.insert(
            version,
            config["sha1"]
                .as_str()
                .context("missing stock compiler SHA-1")?
                .to_owned(),
        );
    }
    for version in ["GC_1_3", "GC_1_3_2", "GC_2_7"] {
        collect(root, &format!("config/{version}/symbols.txt"), &mut source)?;
    }
    let hashes: BTreeMap<_, _> = source
        .iter()
        .map(|(path, bytes)| (path, sha256(bytes)))
        .collect();
    let source_digest = sha256(&serde_json::to_vec(&hashes)?);
    let mut files = BTreeMap::new();
    for path in [
        "LICENSE",
        "licenses/README.md",
        "licenses/capstone-rs-MIT.txt",
        "licenses/capstone-sys-MIT.txt",
        "licenses/capstone-5.0.9-LICENSE.txt",
        "licenses/capstone-5.0.9-LICENSE_LLVM.txt",
    ] {
        collect(root, path, &mut files)?;
    }
    let notices = format!("licenses/dependencies/{target}");
    collect(root, &notices, &mut files)?;
    let inventory: Value = serde_json::from_slice(&files[&format!("{notices}/inventory.json")])?;
    ensure!(
        inventory["format_version"] == 1 && inventory["target"] == target,
        "notice inventory target/format mismatch"
    );
    ensure!(
        inventory["cargo_lock_sha256"] == sha256(&source["Cargo.lock"]),
        "notice inventory has a stale Cargo.lock hash"
    );
    let expected = inventory["files"]
        .as_object()
        .context("missing notice file inventory")?;
    ensure!(
        files
            .keys()
            .filter(|p| p.starts_with(&format!("{notices}/")))
            .count()
            == expected.len() + 1,
        "notice file inventory is incomplete"
    );
    for (path, digest) in expected {
        let bytes = files
            .get(&format!("{notices}/{path}"))
            .context("notice file missing")?;
        ensure!(digest == &sha256(bytes), "notice hash mismatch: {path}");
    }
    for (from, to) in [
        (
            "integrations/decomp-kits/overlay/.agents/docs/oracle-native.md",
            "docs/oracle-native.md",
        ),
        ("docs/oracle-rust-release.md", "README.md"),
        ("crates/mwcc-oracle-hooks/README.md", "docs/producer.md"),
    ] {
        files.insert(to.into(), fs::read(root.join(from))?);
    }
    let executable = if target.contains("windows") {
        "bin/mwcc-tools.exe"
    } else {
        "bin/mwcc-tools"
    };
    files.insert(executable.into(), binary.to_vec());
    let manifest: BTreeMap<_, _> = files
        .iter()
        .map(|(path, bytes)| (path, json!({"bytes": bytes.len(), "sha256": sha256(bytes)})))
        .collect();
    let metadata = json!({
        "format_version": 1, "package": "mwcc-tools", "version": env!("CARGO_PKG_VERSION"), "target": target,
        "executable": executable, "executable_sha256": sha256(binary),
        "source_revision": revision, "source_catalogue_sha256": source_digest, "source_files": hashes,
        "source_identity_scope": "Supplied checkout input bytes; executable identity is recorded separately. A revision alone does not attest a clean checkout or a reproducible compilation.",
        "cargo_lock_sha256": inventory["cargo_lock_sha256"], "notice_inventory_rustc": inventory["rustc"],
        "protocols": {"compilation": 1, "register_difference": 1, "oracle_report": crate::SCHEMA_VERSION},
        "stock_compiler_sha1": originals,
        "external_components": ["Original compiler executables", "Built oracle checkout for compilation", "Historical linker and runtime for construction", "Ninja for project builds", "objdiff for register differences", "wibo 1.2.0 on Linux", "Rust 1.96.0 and i686-pc-windows-msvc target for building observation hooks"],
        "files": manifest
    });
    files.insert("release.json".into(), serde_json::to_vec_pretty(&metadata)?);
    let mut archive = ZipWriter::new(std::io::Cursor::new(Vec::new()));
    for (name, bytes) in files {
        let options = SimpleFileOptions::default()
            .compression_method(CompressionMethod::Deflated)
            .last_modified_time(zip::DateTime::default())
            .unix_permissions(if name == executable { 0o755 } else { 0o644 });
        archive.start_file(name, options)?;
        archive.write_all(&bytes)?;
    }
    Ok(archive.finish()?.into_inner())
}

fn validate_binary(bytes: &[u8], target: &str) -> Result<()> {
    if target.contains("windows") {
        ensure!(
            bytes.len() >= 64 && &bytes[..2] == b"MZ",
            "release executable is not PE"
        );
        let pe = u32::from_le_bytes(bytes[60..64].try_into()?) as usize;
        ensure!(
            bytes.get(pe..pe.saturating_add(6)) == Some(b"PE\0\0\x64\x86"),
            "release executable is not x86-64 PE"
        );
    } else {
        ensure!(
            bytes.len() >= 20 && &bytes[..6] == b"\x7fELF\x02\x01" && &bytes[18..20] == b"\x3e\0",
            "release executable is not x86-64 little-endian ELF"
        );
    }
    Ok(())
}

pub fn write(
    root: &Path,
    binary: &Path,
    target: &str,
    revision: Option<&str>,
    output: &Path,
) -> Result<String> {
    let bytes = assemble(root, &fs::read(binary)?, target, revision)?;
    let digest = sha256(&bytes);
    let parent = output
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(Path::new("."));
    fs::create_dir_all(parent)?;
    let name = output
        .file_name()
        .and_then(|n| n.to_str())
        .context("archive must have a UTF-8 filename")?;
    let mut temp = tempfile::NamedTempFile::new_in(parent)?;
    temp.write_all(&bytes)?;
    temp.persist(output)?;
    fs::write(
        output.with_extension("zip.sha256"),
        format!("{digest}  {name}\n"),
    )?;
    Ok(digest)
}
