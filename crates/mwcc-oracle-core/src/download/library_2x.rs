//! The MSL C library and runtime sources of the GC 2.x tree (`gc2/lib`): CodeWarrior Pro 5's, with the Pro 5.3
//! updater's over them, the revision 2.x links. A port of gc2/tools/download_tool.py's `lib`, checked against
//! the same digest of the files it writes.
use super::*;
use sha2::{Digest, Sha256};
use std::collections::BTreeMap;

/// The SHA-256 of the files written (`tree_digest`), as gc2/tools/download_tool.py pins it.
pub const LIB_2X_SHA256: &str = "0214ffb064993884b397f5459107c7ba6595effe9d9c6823735eadee7dd70e81";

const WIN32_SDK: &[&str] = &[
    "BaseTsd.h",
    "POPPACK.H",
    "PSHPACK2.H",
    "PSHPACK4.H",
    "PSHPACK8.H",
    "TCHAR.H",
    "WINBASE.H",
    "WINDEF.H",
    "WINERROR.H",
    "WINNT.H",
    "WINUSER.H",
    "sys/TYPES.H",
];

/// From the Pro 5 tools archive: (directory, archive directory, files: all when None).
const LIB: &[(&str, &str, Option<&[&str]>)] = &[
    (
        "msl/MSL_Common/Include",
        "MSL/MSL_C/MSL_Common/Include",
        None,
    ),
    ("msl/MSL_Common/Src", "MSL/MSL_C/MSL_Common/Src", None),
    (
        "msl/MSL_Cpp/MSL_Common/Include",
        "MSL/MSL_C++/MSL_Common/Include",
        None,
    ),
    (
        "msl/MSL_Cpp/MSL_Common/Src",
        "MSL/MSL_C++/MSL_Common/Src",
        None,
    ),
    ("msl/MSL_Win32/Include", "MSL/MSL_C/MSL_Win32/Include", None),
    ("msl/MSL_Win32/Src", "MSL/MSL_C/MSL_Win32/Src", None),
    ("msl/MSL_X86", "MSL/MSL_C/MSL_X86", None),
    (
        "msl/win32sdk",
        "Win32-x86 Support/Headers/Win32 SDK",
        Some(WIN32_SDK),
    ),
    (
        "msl/extra",
        "MSL/MSL_C/MSL_Common/Include",
        Some(&["os_enum.h"]),
    ),
    (
        "msl/extra",
        "Win32-x86 Support/Headers/Win32 SDK",
        Some(&["x86_prefix.h"]),
    ),
    ("msl/extra", "MSL/MSL_C/MSL_Common/Src", Some(&["printf.c"])),
    (
        "runtime",
        "Win32-x86 Support/Libraries/Runtime/(Sources)",
        None,
    ),
];

/// The updater's groups unpacked (unshield names their directories with underscores).
pub const GROUPS: &[&str] = &["Win CC++ - FU2", "Mac CC++ - FU2"];

/// The Pro 5.3 updater's files over those: (directory, group directory, directory in it, files: all when None).
const LIB53: &[(&str, &str, &str, Option<&[&str]>)] = &[
    (
        "msl/MSL_Common/Include",
        "Win_CC++_-_FU2",
        "MSL/MSL_C/MSL_Common/Include",
        None,
    ),
    (
        "msl/MSL_Common/Src",
        "Win_CC++_-_FU2",
        "MSL/MSL_C/MSL_Common/Src",
        None,
    ),
    (
        "msl/MSL_Win32/Include",
        "Win_CC++_-_FU2",
        "MSL/MSL_C/MSL_Win32/Include",
        None,
    ),
    (
        "msl/MSL_Win32/Src",
        "Win_CC++_-_FU2",
        "MSL/MSL_C/MSL_Win32/Src",
        None,
    ),
    ("msl/MSL_X86", "Win_CC++_-_FU2", "MSL/MSL_C/MSL_X86", None),
    (
        "msl/MSL_Cpp/MSL_Common/Include",
        "Mac_CC++_-_FU2",
        "MSL/MSL_C++/MSL_Common/Include",
        None,
    ),
    (
        "msl/win32sdk",
        "Win_CC++_-_FU2",
        "Win32-x86_Support/Headers/Win32_SDK",
        Some(WIN32_SDK),
    ),
    (
        "msl/extra",
        "Win_CC++_-_FU2",
        "MSL/MSL_C/MSL_Common/Include",
        Some(&["os_enum.h"]),
    ),
    (
        "msl/extra",
        "Win_CC++_-_FU2",
        "MSL/MSL_C/MSL_Common/Src",
        Some(&["printf.c"]),
    ),
    (
        "runtime",
        "Win_CC++_-_FU2",
        "Win32-x86_Support/Libraries/Runtime/(Sources)",
        None,
    ),
];

/// 2.x's time.c is a little later than the updater's: time() returns GMT, gmtime() converts no further and
/// localtime() applies the time zone bias.
const TIME_PATCHES: &[(&[u8], &[u8])] = &[
    (
        b"\ttime_t\ttime = __get_time();\r\n\r\n\tif (timer)",
        b"\ttime_t\ttime = __get_time();\r\n\r\n\t__to_gm_time(&time);\r\n\r\n\tif (timer)",
    ),
    (b"\tif (!__to_gm_time(&time))\r\n\t\treturn(NULL);\r\n", b""),
    (
        b"\tstatic struct tm\ttm;\r\n\r\n\tif (!timer)\r\n\t\tclear_tm(&tm);\r\n\telse\r\n\t\t__time2tm(*timer, &tm);",
        b"\tstatic struct tm\ttm;\r\n\ttime_t\ttime;\r\n\r\n\tif (!timer)\r\n\t\tclear_tm(&tm);\r\n\telse\r\n\t{\r\n\t\ttime = *timer;\r\n\t\tsubtract_time_zone_bias((int *)&time);\r\n\t\t__time2tm(time, &tm);\r\n\t}",
    ),
    (
        b"#ifndef _No_Time_OS_Support              /* mea 970720 */",
        b"extern int subtract_time_zone_bias(int *);\r\n\r\n#ifndef _No_Time_OS_Support              /* mea 970720 */",
    ),
];

fn patched(data: &[u8], replacements: &[(&[u8], &[u8])], count: usize) -> Result<Vec<u8>> {
    let mut data = data.to_vec();
    for (old, new) in replacements {
        let at: Vec<_> = data
            .windows(old.len())
            .enumerate()
            .filter(|(_, w)| w == old)
            .map(|(i, _)| i)
            .collect();
        ensure!(at.len() == count, "lib: unexpected contents to patch");
        for i in at.into_iter().rev() {
            data.splice(i..i + old.len(), new.iter().copied());
        }
    }
    Ok(data)
}

fn digest(files: &BTreeMap<String, Vec<u8>>) -> String {
    let mut lines = String::new();
    for (path, data) in files {
        lines += &format!("{path} {:x}\n", Sha256::digest(data));
    }
    format!("{:x}", Sha256::digest(lines.as_bytes()))
}

fn walk(base: &Path, directory: &Path, out: &mut Vec<PathBuf>) -> Result<()> {
    for entry in fs::read_dir(directory)? {
        let entry = entry?;
        if entry.file_type()?.is_dir() {
            walk(base, &entry.path(), out)?;
        } else if entry.file_type()?.is_file() {
            out.push(entry.path().strip_prefix(base)?.to_path_buf());
        }
    }
    Ok(())
}

/// Write the tree into ROOT (gc2/lib): ARCHIVE is the Pro 5 tools archive, UPDATER the unpacked updater groups.
pub fn build_library_2x<R: Read + Seek>(archive: R, updater: &Path, root: &Path) -> Result<()> {
    let mut files = BTreeMap::new();
    let mut archive = zip::ZipArchive::new(archive)?;
    for (directory, source, wanted) in LIB {
        for i in 0..archive.len() {
            let mut member = archive.by_index(i)?;
            if member.is_dir() {
                continue;
            }
            let Some(relative) = member
                .name()
                .strip_prefix(source)
                .and_then(|s| s.strip_prefix('/'))
                .map(str::to_owned)
            else {
                continue;
            };
            if wanted.is_some_and(|w| !w.contains(&relative.as_str())) {
                continue;
            }
            let mut data = vec![];
            member.read_to_end(&mut data)?;
            files.insert(format!("{directory}/{relative}"), data);
        }
    }
    for (directory, group, source, wanted) in LIB53 {
        let base = updater.join(group).join(source);
        if !base.is_dir() {
            continue;
        }
        let mut paths = vec![];
        walk(&base, &base, &mut paths)?;
        paths.sort();
        for path in paths {
            let relative = path.to_string_lossy().replace('\\', "/");
            if wanted.is_some_and(|w| !w.iter().any(|f| f.eq_ignore_ascii_case(&relative))) {
                continue;
            }
            // (the name Pro 5's tree spells, where it has the file: case differs between the two)
            let target = format!("{directory}/{relative}");
            let target = files
                .keys()
                .find(|f| f.eq_ignore_ascii_case(&target))
                .cloned()
                .unwrap_or(target);
            files.insert(target, fs::read(base.join(&path))?);
        }
    }
    // (2.x's printf prints a null %s as "(null)"; its startup opens the standard handles untranslated)
    let printf = patched(
        &files["msl/extra/printf.c"],
        &[(
            b"buff_ptr = \"\";      /*97010mani@be */",
            b"buff_ptr = \"(null)\";      /*97010mani@be */",
        )],
        1,
    )?;
    files.insert("msl/extra/printf.c".into(), printf);
    let time = patched(&files["msl/MSL_Common/Src/time.c"], TIME_PATCHES, 1)?;
    files.insert("msl/extra/time.c".into(), time);
    let startup = patched(
        &files["msl/MSL_Win32/Src/startup.win32.c"],
        &[(b"->translate = 1;\t/* 961208 KO */", b"->translate = 0;")],
        3,
    )?;
    files.insert("msl/extra/startup.win32.c".into(), startup);
    let actual = digest(&files);
    ensure!(
        actual == LIB_2X_SHA256,
        "lib-2x: the files' SHA-256 is {actual}, not {LIB_2X_SHA256}: a download changed upstream"
    );
    for (path, data) in &files {
        write(&root.join(checked_relative(path)?), data)?;
    }
    Ok(())
}
