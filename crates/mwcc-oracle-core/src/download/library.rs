use super::*;
pub struct LibrarySelection {
    pub directory: &'static str,
    pub source: &'static str,
    pub files: Option<&'static [&'static str]>,
}
pub const LIB: [LibrarySelection; 15] = [
    LibrarySelection {
        directory: "msl/MSL_Common/Include",
        source: "MSL/MSL_C/MSL_Common/Include",
        files: None,
    },
    LibrarySelection {
        directory: "msl/MSL_Common/Src",
        source: "MSL/MSL_C/MSL_Common/Src",
        files: None,
    },
    LibrarySelection {
        directory: "msl/MSL_Cpp/MSL_Common/Include",
        source: "MSL/MSL_C++/MSL_Common/Include",
        files: None,
    },
    LibrarySelection {
        directory: "msl/MSL_Cpp/MSL_Common/Src",
        source: "MSL/MSL_C++/MSL_Common/Src",
        files: None,
    },
    LibrarySelection {
        directory: "msl/MSL_Win32/Include",
        source: "MSL/MSL_C/MSL_Win32/Include",
        files: None,
    },
    LibrarySelection {
        directory: "msl/MSL_Win32/Src",
        source: "MSL/MSL_C/MSL_Win32/Src",
        files: None,
    },
    LibrarySelection {
        directory: "msl/MSL_X86",
        source: "MSL/MSL_C/MSL_X86",
        files: None,
    },
    LibrarySelection {
        directory: "msl/win32sdk",
        source: "Win32-x86 Support/Headers/Win32 SDK",
        files: Some(&[
            "BaseTsd.h",
            "EXCPT.H",
            "IMM.H",
            "MCX.H",
            "POPPACK.H",
            "PSHPACK1.H",
            "PSHPACK2.H",
            "PSHPACK4.H",
            "PSHPACK8.H",
            "TCHAR.H",
            "WINBASE.H",
            "WINCON.H",
            "WINDEF.H",
            "WINDOWS.H",
            "WINERROR.H",
            "WINGDI.H",
            "WINNETWK.H",
            "WINNLS.H",
            "WINNT.H",
            "WINREG.H",
            "WINSVC.H",
            "WINUSER.H",
            "WINVER.H",
            "sys/TYPES.H",
        ]),
    },
    LibrarySelection {
        directory: "extra",
        source: "MSL/MSL_C/MSL_Common/Include",
        files: Some(&["os_enum.h"]),
    },
    LibrarySelection {
        directory: "extra",
        source: "Win32-x86 Support/Headers/Win32 SDK",
        files: Some(&["x86_prefix.h"]),
    },
    LibrarySelection {
        directory: "extra",
        source: "MSL/MSL_C/MSL_Common/Src",
        files: Some(&["printf.c", "time.c"]),
    },
    LibrarySelection {
        directory: "extra",
        source: "MSL/MSL_C/MSL_Win32/Src",
        files: Some(&["startup.win32.c", "ThreadLocalData.c", "time.win32.c"]),
    },
    LibrarySelection {
        directory: "extra",
        source: "Win32-x86 Support/Libraries/Runtime/(Sources)",
        files: Some(&["exchand.cpp"]),
    },
    LibrarySelection {
        directory: "runtime",
        source: "Win32-x86 Support/Libraries/Runtime/(Sources)",
        files: None,
    },
    LibrarySelection {
        directory: "win32sdk",
        source: "Win32-x86 Support/Libraries/Win32 SDK",
        files: Some(&["KERNEL32.LIB"]),
    },
];
pub fn extract_library<R: Read + Seek>(reader: R, root: &Path) -> Result<()> {
    let mut archive = zip::ZipArchive::new(reader)?;
    let mut total = 0u64;
    for selection in &LIB {
        for i in 0..archive.len() {
            let mut member = archive.by_index(i)?;
            if member.is_dir() {
                continue;
            }
            let Some(relative) = member
                .name()
                .strip_prefix(selection.source)
                .and_then(|s| s.strip_prefix('/'))
            else {
                continue;
            };
            if selection
                .files
                .is_some_and(|names| !names.contains(&relative))
            {
                continue;
            }
            let target = root
                .join(selection.directory)
                .join(checked_relative(relative)?);
            total = total
                .checked_add(member.size())
                .context("library size overflow")?;
            ensure!(total <= MAX_BYTES, "library contents are too large");
            let mut data = vec![];
            member.read_to_end(&mut data)?;
            write(&target, &data)?;
        }
    }
    Ok(())
}
fn patch(path: &Path, replacements: &[(&[u8], &[u8])]) -> Result<()> {
    let mut data = fs::read(path)?;
    for (old, new) in replacements {
        ensure!(!old.is_empty(), "empty patch pattern");
        let at: Vec<_> = data
            .windows(old.len())
            .enumerate()
            .filter(|(_, w)| w == old)
            .map(|(i, _)| i)
            .collect();
        ensure!(at.len() == 1, "{}: unexpected contents", path.display());
        let at = at[0];
        data.splice(at..at + old.len(), new.iter().copied());
    }
    write(path, &data)
}
pub fn patch_library(root: &Path) -> Result<()> {
    let extra = root.join("extra");
    patch(
        &extra.join("printf.c"),
        &[(
            b"buff_ptr = \"\";      /*97010mani@be */",
            b"buff_ptr = \"(null)\";      /*97010mani@be */",
        )],
    )?;
    patch(&extra.join("time.c"),&[
        (b"\ttime_t\ttime = __get_time();",b"\ttime_t\ttime = __get_time();\r\n\r\n\t__to_gm_time(&time);"),
        (b"\t\t__time2tm(*timer, &tm);",b"\t{\r\n\t\ttime_t t = *timer;\r\n\r\n\t\tsubtract_time_zone_bias(&t);\r\n\t\t__time2tm(t, &tm);\r\n\t}"),
        (b"struct tm * localtime(const time_t * timer)",b"int subtract_time_zone_bias(time_t * time);\r\n\r\nstruct tm * localtime(const time_t * timer)")])?;
    patch(&extra.join("time.win32.c"),&[(b"/*  Change Record",b"int subtract_time_zone_bias(time_t * time)\r\n{\r\n\tTIME_ZONE_INFORMATION tzi;\r\n\r\n\tif (GetTimeZoneInformation(&tzi) == TIME_ZONE_ID_UNKNOWN)\r\n\t\treturn 0;\r\n\t*time -= (tzi.Bias * 60);\r\n\treturn(1);\r\n}\r\n\r\n/*  Change Record")])?;
    let startup: Vec<_> = (0..3)
        .map(|i| {
            (
                format!("_HandleTable[{i}]->translate = 1;"),
                format!("_HandleTable[{i}]->translate = 0;"),
            )
        })
        .collect();
    patch(
        &extra.join("startup.win32.c"),
        &startup
            .iter()
            .map(|(a, b)| (a.as_bytes(), b.as_bytes()))
            .collect::<Vec<_>>(),
    )?;
    patch(&extra.join("ThreadLocalData.c"),&[(b"\t    MessageBox(NULL, TEXT(\"Could not get thread local data\"), TEXT(\"MW Win32 Runtime\"), MB_OK);\r\n\t    exit(0);",
        b"\t    static char *message = \"Could not get thread local data\\n\";\r\n\t    DWORD written;\r\n\t    HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);\r\n\r\n\t    WriteFile(handle, message, strlen(message), &written, NULL);\r\n\t    exit(7);")])?;
    patch(&extra.join("exchand.cpp"),&[(b"#include <stdio.h>\t// 960711: Wouldn't compile without it.\r\n",b"#include <stdio.h>\t// 960711: Wouldn't compile without it.\r\n#include <string.h>\r\n"),
        (b"    MessageBox(NULL, buffer, TEXT(\"Unhandled Exception\"), MB_OK | MB_TASKMODAL);",b"    DWORD written;\r\n    HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);\r\n\r\n    WriteFile(handle, buffer, strlen(buffer), &written, NULL);")])?;
    Ok(())
}
