//! The compiler of gc2's sources built for WebAssembly, to run in process (crates/mwcc-wasm): the build of GC 2.6 and
//! 2.7 (`build/<version>/wasm/mwcceppc.wasm`, one of `configure`'s default targets). It needs LLVM's clang and wasm-ld
//! (`$clang`, `$wasm_ld`), and nothing downloaded.
//!
//! Each source is compiled with clang from the arguments the build gives it (its defines, among them `VERSION`, its
//! include paths and `-align`); the module is linked from them and from `wasm/`'s sources, which stand in for what
//! is x86, Windows-only or Metrowerks':
//!
//! * the C library and its runtime (gc2's `src/msl` and `src/runtime`, which compile MSL's own sources from the
//!   downloaded `gc2/lib`): `wasm/libc`, which behaves as MSL does wherever the compiler can tell (see `libc.h`);
//! * the startup, `start.c`;
//! * MSL's x87 code: `x87.c` emulates `__dec2num` and `__num2dec`'s 80-bit arithmetic, and the double rounding of the
//!   compiler's constant folding (gc2's `CMACH_FOLD`);
//! * `setjmp` (`sjlj.c`, on wasm exceptions), `operator new` (`new.cpp`) and the license calls (`platform.c`).
//!
//! `wasm/include` has the C headers clang lacks and Windows headers made of gc2's `include/compiler/win32.h`; the
//! Win32 functions are the module's imports. What WebAssembly needs of the sources themselves is in gc2, under
//! `#ifdef __wasm__`.
use crate::{build_config, source_tree::SourceTree};
use anyhow::{Context, Result, ensure};
use std::path::Path;

/// The versions gc2's sources build (`VERSION`), and so the module.
pub const VERSIONS: [&str; 2] = mwcc_project::compilers::VERSIONS;

/// Sources the module does without (wasm/ replaces them).
const SKIP: &[&str] = &["src/driver/LicenseImports.c"];
const SKIP_DIRECTORIES: &[&str] = &["src/msl/", "src/runtime/"];

/// wasm/'s sources, each with the arguments of a source of the tree (the library's: a source of the compiler's
/// that sees gc2's C headers alone).
const PLATFORM: &[(&str, &str)] = &[
    ("wasm/libc/alloc.c", LIBRARY),
    ("wasm/libc/extras.c", LIBRARY),
    ("wasm/libc/printf.c", LIBRARY),
    ("wasm/libc/runtime.c", LIBRARY),
    ("wasm/libc/scan.c", LIBRARY),
    ("wasm/libc/stdio.c", LIBRARY),
    ("wasm/libc/string.c", LIBRARY),
    ("wasm/src/x87.c", LIBRARY),
    ("wasm/src/start.c", LIBRARY),
    ("wasm/src/sjlj.c", "src/driver/CLMain.c"),
    ("wasm/src/platform.c", "src/driver/CLPlugins.c"),
    ("wasm/src/new.cpp", "src/driver/CLDropinCallbacks_V10.cpp"),
];
const LIBRARY: &str = "src/driver/CLIO.c";
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

/// The compiler module every in-process run uses (crate::in_process): the oracle's hooks, which trace only when a
/// run asks (mwcc-wasm's `Request::trace`), and PERSIST_UNITS compiled with `MWCC_PERSIST`, so that a source with the
/// marker is a persistent compile (mwcc-wasm's `Persistent`) and any other compiles as usual. The stock, oracle and
/// persistent modules are the references it is compared with.
pub fn module_output(version: &str) -> String {
    mwcc_project::compilers::module_output(version)
}

/// The VS_VERSIONINFO of VERSION's driver (gc2's `mwcceppc.rc`, with its `VERSION`): what a run of the module gets for
/// the executable it stands for, written beside it (mwcc-wasm's `version_info_path`).
pub fn version_info(root: &Path, version: &str) -> Result<Vec<u8>> {
    let tree = SourceTree::gc2(version);
    let define = tree
        .version_define(root, version)?
        .context("gc2 has no version number")?;
    let gc2 = root.join("gc2");
    let resources = crate::source_only::compile_resources(
        &std::fs::read_to_string(gc2.join(crate::source_only::RESOURCES))?,
        &std::fs::read_to_string(gc2.join("include/version.h"))?,
        define,
        &std::fs::read(gc2.join(crate::source_only::RESOURCES.replace(".rc", ".ico")))?,
    )?;
    Ok(resources
        .into_iter()
        .next()
        .context("the resource script has no version information")?
        .data)
}

/// The persistent compiler (mwcc-wasm's `Persistent`): the module with PERSIST_UNITS compiled with `MWCC_PERSIST`,
/// which parses a context once and compiles many candidates after it. It exports its stack pointer, which the host
/// checks at each checkpoint.
pub fn persist_output(version: &str) -> String {
    format!("{}/persist.wasm", directory(version))
}

/// The define, and the units it changes: the checkpoint in C_Compiler and preprocessor (CPrep.c), the top-level loop
/// resumed after it (CParser.c).
pub const PERSIST: &str = "-DMWCC_PERSIST";
pub const PERSIST_UNITS: &[&str] = &["src/frontend/CPrep.c", "src/frontend/CParser.c"];

/// A source of the module: its name (the object's path under `obj/`), the file and clang's arguments.
pub struct Unit {
    pub name: String,
    pub input: String,
    pub arguments: Vec<String>,
}

/// clang's arguments for SOURCE (of the tree at PREFIX) from the build's ARGUMENTS for it.
fn clang_arguments(source: &str, prefix: &str, arguments: &[String]) -> Vec<String> {
    let cpp = source.ends_with(".cpp") || source.ends_with(".cp");
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
            !SKIP_DIRECTORIES.iter().any(|d| s.starts_with(d)) && !SKIP.contains(&s.as_str())
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

/// The define the oracle's objects are compiled with: gc2's probes (`include/compiler/OracleProbes.h`) call the
/// hooks. The stock module's objects are compiled without it.
pub const PROBES: &str = "-DORACLE_PROBES";

/// The directory of VERSION's objects: the stock module's, or (ORACLE) the oracle's.
fn objects_directory(version: &str, oracle: bool) -> String {
    format!(
        "{}/{}",
        directory(version),
        if oracle { "oracle-obj" } else { "obj" }
    )
}

/// The object of PERSIST_UNITS' UNIT in [`module_output`]: compiled with the probes and PERSIST.
pub fn oracle_persist_object(version: &str, unit: &str) -> String {
    format!("{}/oracle-persist-obj/{unit}.o", directory(version))
}

/// The module's objects, in link order: each unit's name and object (TAG's last). ORACLE: the oracle's.
pub fn objects(root: &Path, version: &str, oracle: bool) -> Result<Vec<(String, String)>> {
    let directory = objects_directory(version, oracle);
    let mut out: Vec<(String, String)> = units(root, version)?
        .into_iter()
        .map(|unit| {
            let object = format!("{directory}/{}.o", unit.name);
            (unit.name, object)
        })
        .collect();
    out.push((TAG.into(), format!("{directory}/{TAG}.o")));
    Ok(out)
}

/// VERSION's edges: each object, the stock module's and the oracle's, the stock module, and the persistent one (the
/// stock objects but PERSIST_UNITS', compiled again with PERSIST).
pub fn edges(root: &Path, version: &str) -> Result<Vec<String>> {
    let units = units(root, version)?;
    let mut out = vec![];
    let mut stock = vec![];
    for oracle in [false, true] {
        let directory = objects_directory(version, oracle);
        for unit in &units {
            let object = format!("{directory}/{}.o", unit.name);
            out.push(format!(
                "build {}: wasm_cc {}",
                escape(&object),
                escape(&unit.input)
            ));
            out.push(format!(
                "  cflags = {}",
                unit.arguments
                    .iter()
                    .map(String::as_str)
                    .chain(oracle.then_some(PROBES))
                    .map(quote)
                    .collect::<Vec<_>>()
                    .join(" ")
            ));
            if !oracle {
                stock.push(object);
            }
        }
        let tag = format!("{directory}/{TAG}.o");
        out.extend([
            format!("build {}: wasm_cc {TAG}", escape(&tag)),
            "  cflags = --target=wasm32-unknown-unknown -mexception-handling".into(),
        ]);
        if !oracle {
            stock.push(tag);
        }
    }
    out.push(format!(
        "build {}: wasm_ld {}",
        escape(&output(version)),
        stock
            .iter()
            .map(|o| escape(o))
            .collect::<Vec<_>>()
            .join(" ")
    ));
    let mut persist = stock.clone();
    for unit in units
        .iter()
        .filter(|u| PERSIST_UNITS.contains(&u.name.as_str()))
    {
        let object = format!("{}/persist-obj/{}.o", directory(version), unit.name);
        out.push(format!(
            "build {}: wasm_cc {}",
            escape(&object),
            escape(&unit.input)
        ));
        out.push(format!(
            "  cflags = {}",
            unit.arguments
                .iter()
                .map(String::as_str)
                .chain([PERSIST])
                .map(quote)
                .collect::<Vec<_>>()
                .join(" ")
        ));
        let stock_object = format!("{}/{}.o", objects_directory(version, false), unit.name);
        let at = persist.iter().position(|o| *o == stock_object).unwrap();
        persist[at] = object;
    }
    ensure!(
        persist.len() == stock.len()
            && persist
                .iter()
                .filter(|o| o.contains("/persist-obj/"))
                .count()
                == PERSIST_UNITS.len(),
        "the persistent module's units ({PERSIST_UNITS:?}) are not all among the module's"
    );
    for unit in units
        .iter()
        .filter(|u| PERSIST_UNITS.contains(&u.name.as_str()))
    {
        out.push(format!(
            "build {}: wasm_cc {}",
            escape(&oracle_persist_object(version, &unit.name)),
            escape(&unit.input)
        ));
        out.push(format!(
            "  cflags = {}",
            unit.arguments
                .iter()
                .map(String::as_str)
                .chain([PROBES, PERSIST])
                .map(quote)
                .collect::<Vec<_>>()
                .join(" ")
        ));
    }
    out.extend([
        format!(
            "build {}: wasm_ld {}",
            escape(&persist_output(version)),
            persist
                .iter()
                .map(|o| escape(o))
                .collect::<Vec<_>>()
                .join(" ")
        ),
        "  ldflags = --export=__stack_pointer".into(),
    ]);
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
        assert!(a.ends_with(&["-include".into(), "wasm/include/stdarg.h".into()]));
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
