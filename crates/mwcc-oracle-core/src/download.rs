//! Historical tool acquisition, including ranged ISO access and guarded library patches.
use anyhow::{Context, Result, bail, ensure};
use std::{
    fs,
    io::{Read, Seek, Write},
    path::{Component, Path, PathBuf},
    time::{Duration, SystemTime},
};
mod disc;
mod http;
mod library;
pub use disc::{Disc, DiscFile};
pub use http::{Fetch, HttpFetcher};
pub use library::{LIB, LibrarySelection, extract_library, patch_library};
pub const MAX_BYTES: u64 = 1024 * 1024 * 1024;
pub const OBJDIFF_TAG: &str = "v3.8.1";
pub const WIBO_TAG: &str = "1.2.0";
pub const COMPILERS_TAG: &str = "20250812";
pub const PRO4_ISO: &str = "https://archive.org/download/cwpro4/CW_PRO_R4.ISO";
pub const PRO5_BIN: &str =
    "https://archive.org/download/cwpro5/Tools%20-%20Windows%2095%20NT/CW_PRO5.bin";
pub const PRO6_ISO: &str = "https://archive.org/download/codewarrior-6.0/CW_Tools_6.0.iso";
pub const PRO53_UPDATER: &str = "https://archive.org/download/ftp-metrowerks-updates-archive/ftp_metrowerks_updates.7z/Metrowerks/CWWindows5/Pro5.3_Factory_Upd_NoLibs.exe";
pub fn objdiff_url(system: &str, machine: &str) -> Result<String> {
    let system = match system {
        "Windows" => "windows",
        "Linux" => "linux",
        "Darwin" => "macos",
        _ => bail!("unsupported objdiff host {system}"),
    };
    let machine = machine.to_lowercase();
    let machine = match machine.as_str() {
        "amd64" => "x86_64",
        "arm64" | "aarch64" if system == "macos" => "arm64",
        "arm64" => "aarch64",
        other => other,
    };
    Ok(format!(
        "https://github.com/encounter/objdiff/releases/download/{OBJDIFF_TAG}/objdiff-cli-{system}-{machine}{}",
        if system == "windows" { ".exe" } else { "" }
    ))
}
pub fn wibo_url(system: &str, machine: &str) -> String {
    format!(
        "https://github.com/decompals/wibo/releases/download/{WIBO_TAG}/wibo-{}",
        if system == "Darwin" {
            "macos".to_owned()
        } else {
            machine.to_lowercase()
        }
    )
}
pub fn checked_relative(name: &str) -> Result<PathBuf> {
    let name = name.replace('\\', "/");
    let path = Path::new(&name);
    ensure!(
        !name.contains(':')
            && path
                .components()
                .all(|c| matches!(c, Component::Normal(_) | Component::CurDir)),
        "archive member escapes its destination: {name}"
    );
    Ok(path.to_owned())
}
fn write(path: &Path, data: &[u8]) -> Result<()> {
    let parent = path.parent().context("download output has no directory")?;
    fs::create_dir_all(parent)?;
    let mut temporary = tempfile::NamedTempFile::new_in(parent)?;
    temporary.write_all(data)?;
    temporary.persist(path)?;
    Ok(())
}
fn touch(path: &Path) -> Result<()> {
    fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(path)?
        .set_times(
            fs::FileTimes::new()
                .set_accessed(SystemTime::now())
                .set_modified(SystemTime::now()),
        )?;
    Ok(())
}
pub fn zip_member<R: Read + Seek>(reader: R, member: &str) -> Result<Vec<u8>> {
    let mut archive = zip::ZipArchive::new(reader)?;
    let mut file = archive.by_name(member)?;
    ensure!(file.size() <= MAX_BYTES, "ZIP member is too large");
    let mut data = vec![];
    file.read_to_end(&mut data)?;
    Ok(data)
}
pub fn extract_zip<R: Read + Seek>(reader: R, destination: &Path) -> Result<()> {
    let mut archive = zip::ZipArchive::new(reader)?;
    let mut total = 0u64;
    for i in 0..archive.len() {
        let mut member = archive.by_index(i)?;
        let relative = checked_relative(member.name())?;
        ensure!(
            member.unix_mode().is_none_or(|m| m & 0xf000 != 0xa000),
            "ZIP symbolic links are not supported"
        );
        let path = destination.join(relative);
        if member.is_dir() {
            fs::create_dir_all(path)?;
            continue;
        }
        total = total
            .checked_add(member.size())
            .context("ZIP size overflow")?;
        ensure!(total <= MAX_BYTES, "ZIP contents are too large");
        let mut data = vec![];
        member.read_to_end(&mut data)?;
        write(&path, &data)?;
    }
    Ok(())
}
pub fn updater_entries(data: &[u8], offsets: &[usize]) -> Result<Vec<(String, Vec<u8>)>> {
    use crate::pe::{bytes, u16le, u32le};
    let mut result = vec![];
    for offset in offsets.iter().copied() {
        ensure!(
            bytes(data, offset, 4)? == b"PK\x03\x04",
            "updater ZIP header not found"
        );
        ensure!(
            u16le(data, offset + 8)? == 8,
            "updater member is not deflated"
        );
        let packed = u32le(data, offset + 18)? as usize;
        let size = u32le(data, offset + 22)? as u64;
        ensure!(size <= MAX_BYTES, "updater member is too large");
        let length = u16le(data, offset + 26)? as usize;
        let extra = u16le(data, offset + 28)? as usize;
        let name = std::str::from_utf8(bytes(data, offset + 30, length)?)?.to_owned();
        checked_relative(&name)?;
        let body = bytes(data, offset + 30 + length + extra, packed)?;
        let mut unpacked = vec![];
        flate2::read::DeflateDecoder::new(body)
            .take(MAX_BYTES + 1)
            .read_to_end(&mut unpacked)?;
        ensure!(
            unpacked.len() as u64 == size
                && crc32fast::hash(&unpacked) == u32le(data, offset + 14)?,
            "updater member length or CRC differs"
        );
        result.push((name, unpacked));
    }
    Ok(result)
}
fn program(name: &str) -> Option<PathBuf> {
    std::env::split_paths(&std::env::var_os("PATH")?)
        .flat_map(|p| {
            if cfg!(windows) {
                vec![p.join(name), p.join(format!("{name}.exe"))]
            } else {
                vec![p.join(name)]
            }
        })
        .find(|p| p.is_file())
}
fn command_output(
    command: &mut std::process::Command,
    timeout: Duration,
) -> Result<std::process::Output> {
    use std::{process::Stdio, thread, time::Instant};
    let stdout = crate::temporary::diagnostic_log()?;
    let stderr = crate::temporary::diagnostic_log()?;
    crate::process_tree::prepare(command);
    let mut child = command
        .stdout(Stdio::from(stdout.reopen()?))
        .stderr(Stdio::from(stderr.reopen()?))
        .spawn()
        .with_context(|| format!("launch {command:?}"))?;
    let began = Instant::now();
    let status = loop {
        if let Some(status) = child.try_wait()? {
            break status;
        }
        if began.elapsed() >= timeout {
            crate::process_tree::terminate(&mut child)?;
            bail!(
                "download extraction command timed out after {} seconds: {command:?}",
                timeout.as_secs_f64()
            );
        }
        thread::sleep(Duration::from_millis(10));
    };
    Ok(std::process::Output {
        status,
        stdout: fs::read(stdout.path())?,
        stderr: fs::read(stderr.path())?,
    })
}
fn translated(path: &Path, timeout: Duration) -> Result<String> {
    let output = command_output(
        std::process::Command::new("wsl")
            .args(["-e", "wslpath", "-u"])
            .arg(path),
        timeout,
    )?;
    ensure!(
        output.status.success(),
        "wslpath failed: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    Ok(String::from_utf8(output.stdout)?.trim().into())
}
fn unpack_updater<F: Fetch>(
    workspace: &Path,
    root: &Path,
    fetcher: &mut F,
    files: &[&str],
    flatten: bool,
    timeout: Duration,
) -> Result<PathBuf> {
    let kept = workspace.join("build/downloads/Pro5.3_Factory_Upd_NoLibs.exe");
    let data = if kept.is_file() {
        fs::read(&kept)?
    } else {
        let data = fetcher.fetch(PRO53_UPDATER, None)?;
        write(&kept, &data)?;
        data
    };
    for (name, data) in updater_entries(&data, &[100746, 40487531])? {
        write(&root.join(checked_relative(&name)?), &data)?;
    }
    let local = program("unshield");
    let wsl = local.is_none() && cfg!(windows) && program("wsl").is_some();
    ensure!(
        local.is_some() || wsl,
        "unshield is required (brew install unshield / apt install unshield)"
    );
    let mut command = if let Some(local) = local {
        std::process::Command::new(local)
    } else {
        let status = command_output(
            std::process::Command::new("wsl").args(["-e", "which", "unshield"]),
            timeout,
        )?;
        ensure!(
            status.status.success(),
            "unshield is required in WSL (wsl sudo apt install unshield)"
        );
        let mut command = std::process::Command::new("wsl");
        command.args(["-e", "unshield"]);
        command
    };
    let path = |p: PathBuf| -> Result<String> {
        if wsl {
            translated(&p, timeout)
        } else {
            Ok(p.to_string_lossy().into_owned())
        }
    };
    command.args(["-g", "Win CC++ - FU2"]);
    if flatten {
        command.arg("-j");
    }
    command
        .arg("-d")
        .arg(path(root.join("files"))?)
        .arg("x")
        .arg(path(root.join("data1.cab"))?)
        .args(files);
    let output = command_output(&mut command, timeout)?;
    ensure!(
        output.status.success(),
        "unshield extraction failed: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    Ok(root.join("files/Win_CC++_-_FU2"))
}
fn copy_tree(source: &Path, target: &Path) -> Result<()> {
    fs::create_dir_all(target)?;
    for entry in fs::read_dir(source)? {
        let entry = entry?;
        let destination = target.join(entry.file_name());
        if entry.file_type()?.is_dir() {
            copy_tree(&entry.path(), &destination)?;
        } else {
            ensure!(
                entry.file_type()?.is_file(),
                "unsupported extracted file type"
            );
            fs::copy(entry.path(), destination)?;
        }
    }
    Ok(())
}
fn tool_from_disc<F: Fetch>(
    fetcher: &mut F,
    url: &str,
    raw: bool,
    archive: &str,
    member: &str,
) -> Result<Vec<u8>> {
    let mut disc = Disc::new(fetcher, url, raw);
    zip_member(disc.file(archive)?, member)
}
pub fn run_with<F: Fetch>(root: &Path, tool: &str, output: &Path, fetcher: &mut F) -> Result<()> {
    run_with_timeout(root, tool, output, fetcher, Duration::from_secs(300))
}
fn run_with_timeout<F: Fetch>(
    root: &Path,
    tool: &str,
    output: &Path,
    fetcher: &mut F,
    timeout: Duration,
) -> Result<()> {
    ensure!(
        [
            "objdiff-cli",
            "wibo",
            "compilers",
            "pro4",
            "pro5",
            "pro53",
            "pro6",
            "mwld",
            "lib"
        ]
        .contains(&tool),
        "unknown downloadable tool {tool}"
    );
    let output = root.join(output);
    let parent = output
        .parent()
        .context("download output has no directory")?;
    fs::create_dir_all(parent)?;
    if cfg!(windows) && ["pro5", "pro53", "pro6", "mwld"].contains(&tool) {
        let stub = fs::read(root.join("tools/lmgr_stub/lmgr326b.dll"))?;
        let destination = parent.join("lmgr326b.dll");
        // A sibling compiler can already have this DLL loaded while Ninja downloads
        // the linker. Windows denies rewriting loaded DLLs, even with identical bytes.
        if !fs::read(&destination).is_ok_and(|existing| existing == stub) {
            write(&destination, &stub).context("replace compiler license stub")?;
        }
    }
    if ["compilers", "pro4", "pro5", "pro53", "pro6", "mwld", "lib"].contains(&tool)
        && output.exists()
    {
        touch(&output)?;
        return Ok(());
    }
    if tool == "compilers" {
        let data = fetcher.fetch(
            &format!("https://files.decomp.dev/compilers_{COMPILERS_TAG}.zip"),
            None,
        )?;
        extract_zip(std::io::Cursor::new(data), parent)?;
        touch(&output)?;
        return Ok(());
    }
    if tool == "lib" {
        let mut disc = Disc::new(fetcher, PRO5_BIN, true);
        extract_library(disc.file("CODEWA~1.ZIP;1")?, parent)?;
        let temporary = tempfile::tempdir()?;
        let extracted = unpack_updater(root, temporary.path(), fetcher, &[], false, timeout)?;
        copy_tree(
            &extracted.join("Win32-x86_Support/Libraries/Runtime/(Sources)"),
            &parent.join("runtime53"),
        )?;
        patch_library(parent)?;
        touch(&output)?;
        return Ok(());
    }
    let system = if cfg!(windows) {
        "Windows"
    } else if cfg!(target_os = "macos") {
        "Darwin"
    } else {
        "Linux"
    };
    let data = match tool {
        "objdiff-cli" => fetcher.fetch(&objdiff_url(system, std::env::consts::ARCH)?, None)?,
        "wibo" => fetcher.fetch(&wibo_url(system, std::env::consts::ARCH), None)?,
        "pro4" => tool_from_disc(
            fetcher,
            PRO4_ISO,
            false,
            "CODEWA~1/CODEWA~1.ZIP;1",
            "Tools/Command Line Tools/mwcc.exe",
        )?,
        "pro5" => tool_from_disc(
            fetcher,
            PRO5_BIN,
            true,
            "CODEWA~1.ZIP;1",
            "Tools/Command Line Tools/mwcc.exe",
        )?,
        "pro6" => tool_from_disc(
            fetcher,
            PRO6_ISO,
            true,
            "CODEWA~1.ZIP;1",
            "Other Metrowerks Tools/Command Line Tools/mwcc.exe",
        )?,
        "pro53" | "mwld" => {
            let name = if tool == "pro53" {
                "mwcc.exe"
            } else {
                "mwld.exe"
            };
            let temporary = tempfile::tempdir()?;
            let extracted =
                unpack_updater(root, temporary.path(), fetcher, &[name], true, timeout)?;
            fs::read(extracted.join(name))?
        }
        _ => unreachable!(),
    };
    write(&output, &data)?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(&output, fs::Permissions::from_mode(0o755))?;
    }
    Ok(())
}
pub fn run(root: &Path, tool: &str, output: &Path, timeout: Duration) -> Result<()> {
    run_with_timeout(root, tool, output, &mut HttpFetcher::new(timeout)?, timeout)
}
