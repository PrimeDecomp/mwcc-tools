//! The compiler of gc2's sources built for WebAssembly, to run in process (crates/mwcc-wasm): the build of GC 2.6 and
//! 2.7 (`build/<version>/wasm/mwcceppc.wasm`, one of `configure`'s default targets). It needs LLVM's clang and wasm-ld
//! (`$clang`, `$wasm_ld`), and MSL's headers from the library tree (`gc2/lib`, downloaded).
//!
//! Each source is compiled with clang from the arguments the build gives it (its defines, among them `VERSION`, its
//! include paths and `-align`); the module is linked from them and from `wasm/src`, which stands in for what is x86 or
//! Windows-only:
//!
//! * the startup (`src/runtime`, but for `setupargs.c`), which `start.c` replaces;
//! * MSL's x87 code: `x87.c` emulates `__dec2num` and `__num2dec`'s 80-bit arithmetic, and the double rounding of the
//!   compiler's constant folding (gc2's `CMACH_FOLD`); `msl_extras.c` has `extras.c`'s functions and `mem_funcs.c`'s
//!   copies in C;
//! * `setjmp` (`sjlj.c`, on wasm exceptions), `operator new` (`new.cpp`) and the license calls (`platform.c`).
//!
//! `wasm/include` has the C headers clang lacks and Windows headers made of gc2's `include/compiler/win32.h`; the
//! Win32 functions are the module's imports. What WebAssembly needs of the sources themselves is in gc2, under
//! `#ifdef __wasm__`.
use crate::{build_config, source_tree::SourceTree};
use anyhow::{Context, Result, ensure};
use std::path::Path;

/// The versions gc2's sources build (`VERSION`), and so the module.
pub const VERSIONS: [&str; 2] = ["GC_2_6", "GC_2_7"];

/// Sources the module does without (wasm/src replaces them).
const SKIP: &[&str] = &[
    "src/driver/LicenseImports.c",
    "src/msl/setjmp_x86.c",
    "src/msl/ansifp_x86.c",
    "src/msl/extras.c",
];
const SKIP_DIRECTORY: &str = "src/runtime/";
const KEEP: &str = "src/runtime/setupargs.c";

/// wasm/src's sources, each with the arguments of the source it stands in for (or beside).
const PLATFORM: &[(&str, &str)] = &[
    ("wasm/src/x87.c", "src/msl/mem.c"),
    ("wasm/src/msl_extras.c", "src/msl/mem.c"),
    ("wasm/src/sjlj.c", "src/driver/CLMain.c"),
    ("wasm/src/start.c", "src/runtime/setupargs.c"),
    ("wasm/src/platform.c", "src/driver/CLPlugins.c"),
    ("wasm/src/new.cpp", "src/driver/CLDropinCallbacks_V10.cpp"),
];
/// The tag `longjmp` throws (an assembly source).
const TAG: &str = "wasm/src/sjlj_tag.s";

pub fn supported(version: &str) -> bool {
    VERSIONS.contains(&version)
}

/// Whether ROOT has what VERSION's module is built from: gc2's config for it, and the sources wasm/src's borrow
/// their arguments from.
pub fn available(root: &Path, version: &str) -> bool {
    let tree = SourceTree::gc2(version);
    supported(version)
        && root.join(&tree.config_dir).join("config.json").is_file()
        && root.join("wasm/src").is_dir()
        && tree.sources(root).is_ok_and(|sources| {
            PLATFORM
                .iter()
                .all(|(_, like)| sources.get(*like).is_some())
        })
}

pub fn directory(version: &str) -> String {
    format!("build/{version}/wasm")
}

/// The module: the compiler as it is.
pub fn output(version: &str) -> String {
    format!("{}/mwcceppc.wasm", directory(version))
}

/// The oracle: the module with the Rust hooks linked in.
pub fn oracle_output(version: &str) -> String {
    format!("{}/oracle.wasm", directory(version))
}

/// A source of the module: its name (the object's path under `obj/`), the file and clang's arguments.
pub struct Unit {
    pub name: String,
    pub input: String,
    pub arguments: Vec<String>,
}

/// clang's arguments for SOURCE (of the tree at PREFIX) from the build's ARGUMENTS for it.
fn clang_arguments(source: &str, prefix: &str, arguments: &[String]) -> Vec<String> {
    let cpp = source.ends_with(".cpp") || source.ends_with(".cp");
    let msl = [
        "src/msl/",
        "src/runtime/",
        "wasm/src/msl_",
        "wasm/src/x87",
        "wasm/src/start",
    ]
    .iter()
    .any(|p| source.starts_with(p));
    let mut options = vec!["little_endian", "longlong"];
    if cpp {
        options.extend(["cplusplus", "bool", "wchar_type"]);
    }
    let value = |name: &str| {
        arguments
            .iter()
            .position(|a| a == name)
            .and_then(|i| arguments.get(i + 1))
            .map(String::as_str)
    };
    if value("-Cpp_exceptions") == Some("on") {
        options.push("exceptions");
    }
    let mut out: Vec<String> = [
        "--target=wasm32-unknown-unknown",
        "-nostdinc",
        "-Iwasm/include",
        // (the spellings of its headers that a case-sensitive file system lacks)
        "-Iwasm/include-case",
        "-fms-extensions",
        "-fno-ms-compatibility",
        // (MWCC's enums are as small as their values allow, enumsalwaysint off: the layout the hooks read)
        "-fshort-enums",
        "-D__MWERKS__=0x2407",
        // (MSL's long double is double)
        "-Xclang",
        "-mlong-double-64",
        // (setjmp and longjmp on wasm exceptions, the standard ones wasmtime runs)
        "-mllvm",
        "-wasm-enable-sjlj",
        "-mllvm",
        "-wasm-use-legacy-eh=false",
        "-mexception-handling",
        "-D__option(x)=__option_##x",
    ]
    .iter()
    .map(|s| s.to_string())
    .collect();
    out.extend(options.iter().map(|o| format!("-D__option_{o}=1")));
    if cpp {
        out.extend(["-xc++", "-std=gnu++98", "-fno-exceptions", "-fno-rtti"].map(String::from));
    } else {
        out.extend(["-xc", "-std=gnu89"].map(String::from));
    }
    out.extend(
        [
            "-O2",
            "-g0",
            "-w",
            "-fno-builtin",
            "-ffunction-sections",
            "-fdata-sections",
        ]
        .map(String::from),
    );
    let align = match value("-align") {
        Some("byte" | "packed") => "1",
        Some(a) if a.parse::<u32>().is_ok() => a,
        _ => "4",
    };
    out.push(format!("-fpack-struct={align}"));
    let mut system = false;
    for a in arguments {
        if a == "-i-" {
            system = true;
        } else if let Some(path) = a.strip_prefix("-I") {
            out.push(format!("{}{path}", if system { "-isystem" } else { "-I" }));
        } else if a.starts_with("-D") {
            out.push(a.clone());
        }
    }
    out.extend([
        "-isystem".into(),
        format!("{prefix}include/libc"),
        "-include".into(),
        "wasm/include/stdarg.h".into(),
    ]);
    if msl {
        out.extend(["-include".into(), "ansi_prefix.Win32.h".into()]);
    }
    out
}

/// The module's sources for VERSION (TAG aside).
pub fn units(root: &Path, version: &str) -> Result<Vec<Unit>> {
    ensure!(
        supported(version),
        "{version}: only {} build from gc2's sources for WebAssembly",
        VERSIONS.join(" and ")
    );
    let tree = SourceTree::gc2(version);
    let sources = build_config::source_arguments(root, version, &tree)?;
    let prefix = &tree.prefix;
    let mut units: Vec<Unit> = sources
        .iter()
        .filter(|(s, _)| {
            (!s.starts_with(SKIP_DIRECTORY) || s == KEEP) && !SKIP.contains(&s.as_str())
        })
        .map(|(s, a)| Unit {
            name: s.clone(),
            input: format!("{prefix}{s}"),
            arguments: clang_arguments(s, prefix, a),
        })
        .collect();
    for (path, like) in PLATFORM {
        let arguments = &sources
            .iter()
            .find(|(s, _)| s == like)
            .with_context(|| format!("{like} is not a source of the tree"))?
            .1;
        units.push(Unit {
            name: path.to_string(),
            input: path.to_string(),
            arguments: clang_arguments(path, prefix, arguments),
        });
    }
    Ok(units)
}

/// A path as ninja reads it.
fn escape(path: &str) -> String {
    path.replace('$', "$$")
        .replace(' ', "$ ")
        .replace(':', "$:")
}

/// An argument as the build's shell reads it (the arguments have no quotes or spaces but for `-D__option(x)=...`).
fn quote(argument: &str) -> String {
    let argument = argument.replace('$', "$$");
    if argument.contains(['(', ')', '#', ' ']) {
        format!("\"{argument}\"")
    } else {
        argument
    }
}

/// The ninja rules of the module's build (once per build.ninja): clang and wasm-ld are `CLANG` and `WASM_LD`'s, or
/// found on the path.
pub fn rules() -> Vec<String> {
    let tool = |variable: &str, default: &str| {
        let value = std::env::var(variable).unwrap_or_else(|_| default.into());
        quote(&value.replace('\\', "/"))
    };
    let mut out = vec![
        format!("clang = {}", tool("CLANG", "clang")),
        format!("wasm_ld = {}", tool("WASM_LD", "wasm-ld")),
    ];
    out.extend(
        [
            "rule wasm_cc",
            "  command = $clang $cflags -MD -MF $out.d -c $in -o $out",
            "  description = CLANG $in",
            "  depfile = $out.d",
            "  deps = gcc",
            "rule wasm_ld",
        ]
        .map(String::from),
    );
    out.extend([
        format!(
            "  command = $wasm_ld {} $ldflags @$out.rsp -o $out",
            LINK_FLAGS.join(" ")
        ),
        "  rspfile = $out.rsp".into(),
        "  rspfile_content = $in".into(),
        "  description = WASM-LD $out".into(),
    ]);
    out
}

/// wasm-ld's arguments for the module (and the oracle's, crate::wasm_oracle). gnu89's extern inline functions are
/// defined in each unit that includes them: any one is taken.
pub const LINK_FLAGS: [&str; 6] = [
    "--no-entry",
    "--export=_start",
    "--allow-undefined",
    "--allow-multiple-definition",
    "-z",
    "stack-size=8388608",
];

/// The module's objects, in link order: each unit's name and object (TAG's last).
pub fn objects(root: &Path, version: &str) -> Result<Vec<(String, String)>> {
    let directory = directory(version);
    let mut out: Vec<(String, String)> = units(root, version)?
        .into_iter()
        .map(|unit| {
            let object = format!("{directory}/obj/{}.o", unit.name);
            (unit.name, object)
        })
        .collect();
    out.push((TAG.into(), format!("{directory}/obj/{TAG}.o")));
    Ok(out)
}

/// VERSION's edges: each object, and the module. LIBRARY is the library tree's download target.
pub fn edges(root: &Path, version: &str, library: &str) -> Result<Vec<String>> {
    let directory = directory(version);
    let mut out = vec![];
    let mut objects = vec![];
    for unit in units(root, version)? {
        let object = format!("{directory}/obj/{}.o", unit.name);
        out.push(format!(
            "build {}: wasm_cc {} || {}",
            escape(&object),
            escape(&unit.input),
            escape(library)
        ));
        out.push(format!(
            "  cflags = {}",
            unit.arguments
                .iter()
                .map(|a| quote(a))
                .collect::<Vec<_>>()
                .join(" ")
        ));
        objects.push(object);
    }
    let tag = format!("{directory}/obj/{TAG}.o");
    out.extend([
        format!("build {}: wasm_cc {TAG}", escape(&tag)),
        "  cflags = --target=wasm32-unknown-unknown -mexception-handling".into(),
    ]);
    objects.push(tag);
    out.push(format!(
        "build {}: wasm_ld {}",
        escape(&output(version)),
        objects
            .iter()
            .map(|o| escape(o))
            .collect::<Vec<_>>()
            .join(" ")
    ));
    out.push(String::new());
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_builds_arguments_become_clangs() {
        let arguments: Vec<String> = [
            "-c",
            "-align",
            "byte",
            "-DVERSION=5",
            "-Igc2/include",
            "-i-",
            "-Igc2/include/libc",
        ]
        .map(String::from)
        .to_vec();
        let a = clang_arguments("src/msl/mem.c", "gc2/", &arguments);
        assert!(a.contains(&"-fpack-struct=1".into()));
        assert!(a.contains(&"-DVERSION=5".into()));
        assert!(a.contains(&"-Igc2/include".into()));
        assert!(a.contains(&"-isystemgc2/include/libc".into()));
        assert!(a.ends_with(&["-include".into(), "ansi_prefix.Win32.h".into()]));
        let b = clang_arguments("src/driver/x.cpp", "gc2/", &arguments);
        assert!(b.contains(&"-D__option_cplusplus=1".into()) && b.contains(&"-xc++".into()));
    }

    #[test]
    fn the_option_macro_is_quoted_for_the_shell() {
        assert_eq!(
            quote("-D__option(x)=__option_##x"),
            "\"-D__option(x)=__option_##x\""
        );
        assert_eq!(quote("-Igc2/include"), "-Igc2/include");
    }
}
