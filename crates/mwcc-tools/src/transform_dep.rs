//! dtk-template's `tools/transform_dep.py`, done by `run ninja` itself. The template runs
//! `$python tools/transform_dep.py $basefile.d $basefile.d` after each compile elsewhere than on Windows, and with
//! `--relative` everywhere for a portable `build.ninja` (`configure.py --portable-build-ninja`): it turns the Windows
//! paths mwcc writes in its `.d` file into the host's, and with `--relative` those in the build's directory into
//! paths relative to it. A Python process per compile was a sixth of an Echoes build's time in WSL.
//!
//! This does what the script does, to the byte. What it would do otherwise is left to the script: a Wine drive other
//! than `Z:`, a file that is not UTF-8 (on Windows, not ASCII), a line without a path, a path whose real path is not
//! there, and on Windows a path from the root of the current drive (`\x`: whether Python takes it for absolute
//! depends on its version) or a network one.

/// A run of the script: its input, its output, and whether with `--relative`.
#[derive(Debug, PartialEq)]
pub struct Script<'a> {
    pub input: &'a str,
    pub output: &'a str,
    pub relative: bool,
}

/// WORDS (a command's) as a run of the script, if they are one.
pub fn command<'a>(words: &'a [String]) -> Option<Script<'a>> {
    let (python, script, input, output, relative) = match words {
        [python, script, input, output] => (python, script, input, output, false),
        [python, script, input, output, flag] if flag == "--relative" => {
            (python, script, input, output, true)
        }
        _ => return None,
    };
    // (the names after their directories, a Windows command line's with backslashes)
    let name = |word: &'a str| word.rsplit(['/', '\\']).next().unwrap_or(word);
    let (python, script) = (name(python), name(script));
    (python.starts_with("python") && script == "transform_dep.py").then_some(Script {
        input,
        output,
        relative,
    })
}

/// The host as the script tells it.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Host {
    Windows,
    /// WSL (`microsoft-standard` in the kernel's release).
    Wsl,
    /// Another, where the script resolves drives through Wine's `dosdevices`.
    Other,
}

impl Host {
    pub fn this() -> Self {
        if cfg!(windows) {
            Host::Windows
        } else if std::fs::read_to_string("/proc/sys/kernel/osrelease")
            .is_ok_and(|release| release.contains("microsoft-standard"))
        {
            Host::Wsl
        } else {
            Host::Other
        }
    }
}

/// `os.path.realpath` of PATH, where it is there (None elsewhere: Python's would resolve what is there of it).
pub fn realpath(path: &str) -> Option<String> {
    let real = std::fs::canonicalize(path).ok()?;
    let real = real.to_str()?;
    // (on Windows, without the verbatim prefix, as Python gives it)
    Some(match real.strip_prefix(r"\\?\") {
        Some(rest) => match rest.strip_prefix(r"UNC\") {
            Some(share) => format!(r"\\{share}"),
            None => rest.to_owned(),
        },
        None => real.to_owned(),
    })
}

/// What `--relative` needs: the build's directory (`os.path.realpath(os.getcwd())`), and `os.path.realpath`.
pub struct Relative<'a> {
    pub root: &'a str,
    pub realpath: &'a dyn Fn(&str) -> Option<String>,
}

/// The script's `convert_path` (None: what this does not do).
fn convert_path(path: &str, host: Host) -> Option<String> {
    if host == Host::Windows {
        return Some(path.replace('\\', "/"));
    }
    let mut chars = path.chars();
    let first: String = chars.next()?.to_lowercase().collect();
    let rest = chars.as_str();
    if first == "z" {
        // (path[2:]: past the drive's colon)
        let tail: String = rest.chars().skip(1).collect();
        return Some(tail.replace('\\', "/"));
    }
    if host != Host::Wsl {
        return None;
    }
    // (os.path.join("/mnt", path[0:1] + path[2:]): an absolute second part replaces "/mnt")
    let joined = format!("{first}{}", rest.chars().skip(1).collect::<String>()).replace('\\', "/");
    Some(if joined.starts_with('/') {
        joined
    } else {
        format!("/mnt/{joined}")
    })
}

/// `posixpath.relpath` of two normal absolute paths.
fn posix_relpath(path: &str, start: &str) -> String {
    let path: Vec<&str> = path.split('/').filter(|p| !p.is_empty()).collect();
    let start: Vec<&str> = start.split('/').filter(|p| !p.is_empty()).collect();
    let common = path.iter().zip(&start).take_while(|(a, b)| a == b).count();
    let mut parts = vec![".."; start.len() - common];
    parts.extend(&path[common..]);
    if parts.is_empty() {
        ".".into()
    } else {
        parts.join("/")
    }
}

/// `ntpath.relpath` of two normal absolute paths with drive letters (None: on other drives, a ValueError).
fn nt_relpath(path: &str, start: &str) -> Option<String> {
    let split = |p: &str| -> (String, Vec<String>) {
        let (drive, rest) = p.split_at(2.min(p.len()));
        let parts = rest
            .split(['\\', '/'])
            .filter(|p| !p.is_empty())
            .map(String::from)
            .collect();
        (drive.to_lowercase(), parts)
    };
    let (path_drive, path) = split(path);
    let (start_drive, start) = split(start);
    if path_drive != start_drive {
        return None;
    }
    let common = path
        .iter()
        .zip(&start)
        .take_while(|(a, b)| a.to_lowercase() == b.to_lowercase())
        .count();
    let mut parts: Vec<&str> = vec![".."; start.len() - common];
    parts.extend(path[common..].iter().map(String::as_str));
    Some(if parts.is_empty() {
        ".".into()
    } else {
        parts.join("\\")
    })
}

/// The script's `make_relative` (None: what this does not do).
fn make_relative(path: &str, host: Host, relative: &Relative) -> Option<String> {
    let absolute = if host == Host::Windows {
        let bytes = path.as_bytes();
        if path.starts_with(['/', '\\']) {
            return None;
        }
        bytes.len() >= 3
            && bytes[0].is_ascii_alphabetic()
            && bytes[1] == b':'
            && matches!(bytes[2], b'/' | b'\\')
    } else {
        path.starts_with('/')
    };
    if !absolute {
        return Some(path.to_owned());
    }
    let real = (relative.realpath)(path)?;
    let (rel, sep) = if host == Host::Windows {
        match nt_relpath(&real, relative.root) {
            Some(rel) => (rel, "\\"),
            // (on another drive)
            None => return Some(path.to_owned()),
        }
    } else {
        (posix_relpath(&real, relative.root), "/")
    };
    if rel == ".." || rel.starts_with(&format!("..{sep}")) {
        return Some(path.to_owned());
    }
    Some(rel.replace('\\', "/"))
}

/// The script's `import_d_file` of TEXT (with `--relative`, RELATIVE), with its lines ended as the script writes them
/// on HOST.
pub fn transform(text: &[u8], host: Host, relative: Option<&Relative>) -> Option<String> {
    // (the script reads it in the locale's encoding: UTF-8, but not on Windows)
    if host == Host::Windows && !text.is_ascii() {
        return None;
    }
    let text = std::str::from_utf8(text).ok()?;
    // (Python's text mode: \r\n and \r are line ends)
    let text = text.replace("\r\n", "\n").replace('\r', "\n");
    let mut out = String::new();
    for (index, line) in text.split_inclusive('\n').enumerate() {
        if index == 0 {
            match line.strip_suffix(" \\\n") {
                Some(head) => {
                    out.push_str(&head.replace('\\', "/"));
                    out.push_str(" \\\n");
                }
                None => out.push_str(&line.replace('\\', "/")),
            }
            continue;
        }
        let (path, suffix) = match line.strip_suffix(" \\\n") {
            Some(_) => {
                // (line.lstrip()[:-3])
                let trimmed = line.trim_start();
                let n = trimmed.chars().count().saturating_sub(3);
                (trimmed.chars().take(n).collect::<String>(), " \\")
            }
            None => (line.trim().to_owned(), ""),
        };
        let mut path = convert_path(&path, host)?;
        if let Some(relative) = relative {
            path = make_relative(&path, host, relative)?;
        }
        out.push('\t');
        out.push_str(&path);
        out.push_str(suffix);
        out.push('\n');
    }
    Some(if host == Host::Windows {
        out.replace('\n', "\r\n")
    } else {
        out
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn words(line: &str) -> Vec<String> {
        line.split(' ').map(String::from).collect()
    }

    #[test]
    fn the_scripts_command_is_known() {
        assert_eq!(
            command(&words("/usr/bin/python3 tools/transform_dep.py a.d b.d")),
            Some(Script {
                input: "a.d",
                output: "b.d",
                relative: false
            })
        );
        assert_eq!(
            command(&words(
                r"C:\Python\python.exe tools/transform_dep.py a.d a.d --relative"
            )),
            Some(Script {
                input: "a.d",
                output: "a.d",
                relative: true
            })
        );
        assert_eq!(
            command(&words("python tools/transform_dep.py a.d a.d --other")),
            None
        );
        assert_eq!(command(&words("python tools/other.py a.d a.d")), None);
        assert_eq!(command(&words("build/tools/dtk extab clean a.o a.o")), None);
    }

    /// What the script writes for an in-process compile's `.d` file (checked against the script in WSL).
    #[test]
    fn paths_are_the_hosts() {
        let d = b"build\\G2ME01\\src\\A.o: src\\A.cpp \\\r\n\tZ:\\home\\x\\echoes\\include\\A.hpp \\\r\n\tZ:\\home\\x\\echoes\\libc\\string.h\r\n";
        assert_eq!(
            transform(d, Host::Other, None).unwrap(),
            "build/G2ME01/src/A.o: src/A.cpp \\\n\t/home/x/echoes/include/A.hpp \\\n\t/home/x/echoes/libc/string.h\n"
        );
        // (WSL: a drive other than Z: is under /mnt; elsewhere the script asks Wine)
        let d = b"a.o: a.c \\\n\tC:\\x\\a.h\n";
        assert_eq!(
            transform(d, Host::Wsl, None).unwrap(),
            "a.o: a.c \\\n\t/mnt/c/x/a.h\n"
        );
        assert_eq!(transform(d, Host::Other, None), None);
        assert_eq!(
            transform(d, Host::Windows, None).unwrap(),
            "a.o: a.c \\\r\n\tC:/x/a.h\r\n"
        );
        // (a line without a path fails the script: left to it)
        assert_eq!(transform(b"a.o: a.c \\\n\n", Host::Wsl, None), None);
        assert_eq!(transform(b"a.o: \xff\n", Host::Wsl, None), None);
    }

    #[test]
    fn relative_paths_are_the_builds() {
        // (a real path: one in the build's directory is relative to it, another stays as it was)
        let posix = |path: &str| -> Option<String> {
            match path {
                "/home/x/echoes/include/A.hpp" => Some("/data/echoes/include/A.hpp".into()),
                "/usr/include/b.h" => Some("/usr/include/b.h".into()),
                "/data/echoes" => Some("/data/echoes".into()),
                _ => None,
            }
        };
        let relative = Relative {
            root: "/data/echoes",
            realpath: &posix,
        };
        let d = b"a.o: a.c \\\n\tZ:\\home\\x\\echoes\\include\\A.hpp \\\n\tZ:\\usr\\include\\b.h \\\n\tZ:\\data\\echoes\n";
        assert_eq!(
            transform(d, Host::Other, Some(&relative)).unwrap(),
            "a.o: a.c \\\n\tinclude/A.hpp \\\n\t/usr/include/b.h \\\n\t.\n"
        );
        // (one whose real path is not there: left to the script)
        assert_eq!(
            transform(b"a.o: a.c \\\n\tZ:\\gone.h\n", Host::Other, Some(&relative)),
            None
        );
        // (Windows: names compared ignoring case, another drive left as it is, a relative path kept)
        let nt = |path: &str| -> Option<String> {
            match path {
                "C:/Echoes/Include/A.hpp" => Some(r"C:\echoes\include\A.hpp".into()),
                "D:/sdk/b.h" => Some(r"D:\sdk\b.h".into()),
                _ => None,
            }
        };
        let relative = Relative {
            root: r"C:\Echoes",
            realpath: &nt,
        };
        let d = b"a.o: a.c \\\r\n\tC:\\Echoes\\Include\\A.hpp \\\r\n\tD:\\sdk\\b.h \\\r\n\tsrc\\c.h\r\n";
        assert_eq!(
            transform(d, Host::Windows, Some(&relative)).unwrap(),
            "a.o: a.c \\\r\n\tinclude/A.hpp \\\r\n\tD:/sdk/b.h \\\r\n\tsrc/c.h\r\n"
        );
        assert_eq!(
            transform(
                b"a.o: a.c \\\r\n\t\\x\\b.h\r\n",
                Host::Windows,
                Some(&relative)
            ),
            None
        );
        assert_eq!(posix_relpath("/a/b", "/a/c/d"), "../../b");
        assert_eq!(nt_relpath(r"C:\a\B", r"c:\A\c"), Some(r"..\B".into()));
    }
}
