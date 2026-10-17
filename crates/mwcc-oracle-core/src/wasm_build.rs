//! `mwcc-tools wasm-build GC_2_7`: the compiler of a vendored tree (gc2's 2.7) built for wasm32 with clang and wasm-ld,
//! to run in process (crates/mwcc-wasm). Not part of the build: it needs LLVM, and nothing else uses its output. MSL's
//! headers are the library tree's (`gc2/lib`, downloaded).
//!
//! Each source is compiled with clang from the arguments the build gives it (its defines, include paths and
//! `-align`); the module is linked from them and from `wasm/src`, which stands in for what is x86 or Windows-only:
//!
//! * the startup (`src/runtime`, but for `setupargs.c`), which `start.c` replaces;
//! * MSL's x87 code: `x87.c` emulates `__dec2num` and `__num2dec`'s 80-bit arithmetic, and `msl_extras.c` has
//!   `extras.c`'s functions and `mem_funcs.c`'s copies in C;
//! * `setjmp` (`sjlj.c`, on wasm exceptions), `operator new` (`new.cpp`) and the license calls (`platform.c`).
//!
//! `wasm/include` has the C headers clang lacks and Windows headers made of gc2's `include/compiler/win32.h`; the
//! Win32 functions are the module's imports. A function called through a pointer to another type traps in wasm
//! where x86 drops the extra argument: those (ADAPT) are given the pointer's parameters, in a copy of their source.
//! The compiler folds floating-point constants on the x87, which `__dec2num` (the first literal read) leaves at 64-bit
//! precision: each result is rounded twice, to 64 bits and then to a double. Its folding (X87) calls x87.c's
//! emulation of that instead.
use crate::build_config;
use anyhow::{Context, Result, bail, ensure};
use regex::Regex;
use std::{
    fs,
    path::{Path, PathBuf},
    process::Command,
    sync::{
        Mutex,
        atomic::{AtomicUsize, Ordering},
    },
    thread,
};

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

/// Functions called through a pointer of another type: (source, pattern, replacement).
const ADAPT: &[(&str, &str, &str)] = &[
    // (cprep_tokenize's handlers are called with the character)
    (
        "src/frontend/CPrepTokenizer.c",
        r"(?m)^static short (t\w+)\(void\) \{",
        "static short ${1}(short unused) {",
    ),
    // (CompareParams's are called with the parameter alone)
    (
        "src/driver/Parameter.c",
        r"CompareParam_SetString\(SETSTRING_T\* p, int flags\)",
        "CompareParam_SetString(SETSTRING_T* p)",
    ),
];

/// The folding of floating-point constants, made the x87's (x87.c's `__x87_double`): (source, pattern, replacement).
const X87: &[(&str, &str, &str)] = &[
    (
        "src/backend/CMachine.c",
        r"(?m)^Float CMach_CalcFloatDiadic\(",
        "double __x87_double(int op, double a, double b);\nFloat CMach_CalcFloatDiadic(",
    ),
    (
        "src/backend/CMachine.c",
        r"left\.data\.value ([-+*/])= right\.data\.value;",
        "left.data.value = __x87_double('$1', left.data.value, right.data.value);",
    ),
    (
        "src/backend/CMachine.c",
        r"flt\.data\.value = 1\.0 / flt\.data\.value;",
        "flt.data.value = __x87_double('/', 1.0, flt.data.value);",
    ),
];

pub struct Request<'a> {
    pub root: &'a Path,
    pub version: &'a str,
    pub clang: &'a str,
    pub wasm_ld: &'a str,
}

pub struct Outcome {
    pub output: PathBuf,
    pub objects: usize,
}

pub fn output(version: &str) -> String {
    format!("build/{version}/wasm/mwcceppc.wasm")
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
        "-fms-extensions",
        "-fno-ms-compatibility",
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

pub fn run(request: &Request) -> Result<Outcome> {
    let Request {
        root,
        version,
        clang,
        wasm_ld,
    } = *request;
    ensure!(
        version == "GC_2_7",
        "only GC_2_7's tree builds for wasm (its x87 emulation and adaptations are 2.7's)"
    );
    let (tree, sources) = build_config::source_arguments(root, version)?;
    ensure!(tree.vendored(), "{version} has no source tree");
    let prefix = tree.prefix.clone();
    // (MSL's headers are the library tree's)
    let library = format!("{prefix}lib/ok.pro53");
    ensure!(
        root.join(&library).is_file(),
        "{library} is missing: the library tree is needed (ninja {library})"
    );
    let directory = format!("build/{version}/wasm");
    let objects = root.join(&directory).join("obj");
    // (path in the module's build, path to compile, the build's arguments)
    let mut units: Vec<(String, String, Vec<String>)> = sources
        .iter()
        .filter(|(s, _)| {
            (!s.starts_with(SKIP_DIRECTORY) || s == KEEP) && !SKIP.contains(&s.as_str())
        })
        .map(|(s, a)| (s.clone(), format!("{prefix}{s}"), a.clone()))
        .collect();
    for (path, like) in PLATFORM {
        let arguments = sources
            .iter()
            .find(|(s, _)| s == like)
            .with_context(|| format!("{like} is not a source of the tree"))?
            .1
            .clone();
        units.push((path.to_string(), path.to_string(), arguments));
    }
    let next = AtomicUsize::new(0);
    let failures = Mutex::new(vec![]);
    let compile = |(name, path, arguments): &(String, String, Vec<String>)| -> Result<()> {
        let object = objects.join(format!("{name}.o"));
        fs::create_dir_all(object.parent().unwrap())?;
        let mut command = Command::new(clang);
        command
            .current_dir(root)
            .args(clang_arguments(name, &prefix, arguments));
        let mut input = path.clone();
        let adaptations: Vec<_> = ADAPT
            .iter()
            .chain(X87)
            .filter(|(s, _, _)| s == name)
            .collect();
        if !adaptations.is_empty() {
            let mut text: String = fs::read(root.join(path))?
                .iter()
                .map(|b| char::from(*b))
                .collect();
            for (_, pattern, replacement) in adaptations {
                let pattern = Regex::new(pattern)?;
                ensure!(
                    pattern.is_match(&text),
                    "{name}: nothing to adapt for {pattern}"
                );
                text = pattern.replace_all(&text, *replacement).into_owned();
            }
            let adapted = root.join(&directory).join("adapted").join(name);
            fs::create_dir_all(adapted.parent().unwrap())?;
            fs::write(
                &adapted,
                text.chars().map(|c| c as u32 as u8).collect::<Vec<_>>(),
            )?;
            // (its includes are found beside the original)
            let beside = Path::new(path).parent().unwrap_or(Path::new("."));
            command.arg("-iquote").arg(beside);
            input = adapted.to_string_lossy().into_owned();
        }
        let output = command
            .arg("-c")
            .arg(&input)
            .arg("-o")
            .arg(&object)
            .output();
        let output = output.with_context(|| format!("run {clang}"))?;
        if !output.status.success() {
            failures.lock().unwrap().push(format!(
                "{name}:\n{}",
                String::from_utf8_lossy(&output.stderr)
            ));
        }
        Ok(())
    };
    let errors = Mutex::new(vec![]);
    thread::scope(|scope| {
        for _ in 0..thread::available_parallelism().map_or(4, usize::from) {
            scope.spawn(|| {
                while let Some(unit) = units.get(next.fetch_add(1, Ordering::Relaxed)) {
                    if let Err(e) = compile(unit) {
                        errors.lock().unwrap().push(e);
                    }
                }
            });
        }
    });
    if let Some(error) = errors.into_inner().unwrap().into_iter().next() {
        return Err(error);
    }
    let failures = failures.into_inner().unwrap();
    if !failures.is_empty() {
        bail!(
            "{} of {} sources do not compile:\n{}",
            failures.len(),
            units.len(),
            failures.join("\n")
        );
    }
    let tag = objects.join("wasm/src/sjlj_tag.o");
    let status = Command::new(clang)
        .current_dir(root)
        .args([
            "--target=wasm32-unknown-unknown",
            "-mexception-handling",
            "-c",
            "wasm/src/sjlj_tag.s",
            "-o",
        ])
        .arg(&tag)
        .status()
        .with_context(|| format!("run {clang}"))?;
    ensure!(status.success(), "clang fails on wasm/src/sjlj_tag.s");
    let mut list: Vec<String> = units
        .iter()
        .map(|(name, _, _)| {
            objects
                .join(format!("{name}.o"))
                .to_string_lossy()
                .replace('\\', "/")
        })
        .collect();
    list.push(tag.to_string_lossy().replace('\\', "/"));
    let response = root.join(&directory).join("objects.txt");
    fs::write(&response, list.join("\n"))?;
    let output = root.join(self::output(version));
    // (gnu89's extern inline functions are defined in each unit that includes them: any one is taken)
    let status = Command::new(wasm_ld)
        .current_dir(root)
        .args([
            "--no-entry",
            "--export=_start",
            "--allow-undefined",
            "--allow-multiple-definition",
            "-z",
            "stack-size=8388608",
        ])
        .arg(format!("@{}", response.display()))
        .arg("-o")
        .arg(&output)
        .status()
        .with_context(|| format!("run {wasm_ld}"))?;
    ensure!(status.success(), "{wasm_ld} fails");
    Ok(Outcome {
        output: output.strip_prefix(root).unwrap_or(&output).to_path_buf(),
        objects: list.len(),
    })
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
    fn folding_calls_the_x87s_arithmetic() {
        let mut text =
            "Float CMach_CalcFloatDiadic(Type* type) {\n    left.data.value *= right.data.value;\n"
                .to_owned();
        for (_, pattern, replacement) in X87.iter().take(2) {
            text = Regex::new(pattern)
                .unwrap()
                .replace_all(&text, *replacement)
                .into_owned();
        }
        assert!(text.starts_with(
            "double __x87_double(int op, double a, double b);\nFloat CMach_CalcFloatDiadic("
        ));
        assert!(
            text.contains(
                "left.data.value = __x87_double('*', left.data.value, right.data.value);"
            )
        );
    }

    #[test]
    fn adaptations_give_the_pointers_parameters() {
        let (_, pattern, replacement) = ADAPT[0];
        let text = "static short tille(void) {\nstatic short tnumb(short t) {\n";
        let adapted = Regex::new(pattern).unwrap().replace_all(text, replacement);
        assert_eq!(
            adapted,
            "static short tille(short unused) {\nstatic short tnumb(short t) {\n"
        );
    }
}
